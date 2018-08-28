//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <algorithm> /* std::remove */
#include <string>
#include <sstream> /* std::ostringstream */

#include <ebbrt/Debug.h>
#include <ebbrt/Cpu.h>
#include <ebbrt/Runtime.h>
#include <ebbrt/Messenger.h>
#include <ebbrt/UniqueIOBuf.h>


#include "SeussInvoker.h"
#include "SeussChannel.h"

#include "umm/src/Umm.h"

#define kprintf ebbrt::kprintf
using ebbrt::kprintf_force;

void seuss::Init(){
  auto rep = new SeussChannel(SeussChannel::global_id);
  SeussChannel::Create(rep, SeussChannel::global_id);
  umm::UmManager::Init();
  Invoker::Create(Invoker::global_id);

  // TODO: Bootstrap the invoker on each core 
  // XXX: We only bootstrap invoker on the Init core (core 0)
  seuss::invoker->Bootstrap();
#if 0
  // Begin seuss invoker on each core
  //size_t my_cpu = ebbrt::Cpu::GetMine();
  //size_t num_cpus = ebbrt::Cpu::Count();
  for (auto i = my_cpu; i < num_cpus; i++) {
    ebbrt::event_manager->SpawnRemote(
        [i]() {
          kprintf("Begin seuss invoker on core #%d\n", i);
          seuss::invoker->Bootstrap();
        },
        i);
  }
#endif 
}

/* class seuss::InvocationSession */

void seuss::InvocationSession::Connected() {
    // We've established a connection with the instance
    set_connected_.SetValue();
    is_connected_ = true;
    // Send code to be initialized 
    if (!function_code_.empty())
      SendHttpRequest("/init", function_code_);
}

void seuss::InvocationSession::Receive(std::unique_ptr<ebbrt::MutIOBuf> b) {

  kprintf_force("InvocationSession received response : len=%d\n links=%d",
                       b->ComputeChainDataLength(), b->CountChainElements());

  //TODO: support chain and incomplete buffers (i.e., large replies)
  kassert(!b->IsChained());

  std::string reply(reinterpret_cast<const char *>(b->Data()), b->Length());
  std::string proto_header = reply.substr(0, reply.find_first_of("\r"));

  // TODO: handle failure case
  kassert(proto_header == "HTTP/1.1 200 OK");

  std::string response = reply.substr(reply.find_first_of("{"));

  std::cout << "RESPONSE: " << response << std::endl;

  // An "OK":true response signals a finished INIT 
  if(is_initialized_ == false && response == R"({"OK":true})"){
    is_initialized_ = true;
    // always RUN right after INIT is complete
    if(!run_args_.empty())
      SendHttpRequest("/run", run_args_);
  } else {
    // finished RUN, send results back to the invoker to resolve 
    seuss::invoker->Resolve(run_id_, response);
  }
}

void seuss::InvocationSession::Close(){
  kprintf_force("InvocationSession closed!\n");
}

void seuss::InvocationSession::Abort() {
  kprintf_force("InvocationSession aborted!\n");
}

void seuss::InvocationSession::SendHttpRequest(std::string path, std::string payload) {
  kassert(payload.size() > 0);
  std::string msg = http_post_request(path, payload);
  auto buf = ebbrt::MakeUniqueIOBuf(msg.size());
  auto dp = buf->GetMutDataPointer();
  auto str_ptr = reinterpret_cast<char *>(dp.Data());
  msg.copy(str_ptr, msg.size());
  Send(std::move(buf));
}

std::string seuss::InvocationSession::http_post_request(std::string path,
                                                   std::string msg) {
  std::ostringstream payload;
  std::ostringstream ret;
  // strip newlines from msg
  msg.erase(std::remove(msg.begin(), msg.end(), '\n'), msg.end());
  payload << "{\"value\": ";
  if (path == "/init") {
    payload << "{\"main\":\"main\", \"code\":\"" << msg << "\"}}";
  } else {
    payload << msg << "}";
  }
  auto body = payload.str();
  ret << "POST " << path << " HTTP/1.0\r\n"
      << "Content-Type: application/json\r\n"
      << "Connection: keep-alive\r\n"
      << "content-length: " << body.size() << "\r\n\r\n"
      << body;
  return ret.str();
}

/* class suess::Invoker */

void seuss::Invoker::Bootstrap() {
  kprintf_force("Bootstrap invocation instance on core #%d\n", (size_t)ebbrt::Cpu::GetMine());

  // TODO: assert this hasent run yet

  // Generated UM Instance from the linked-in elf
  auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);
  auto umi = std::make_unique<umm::UmInstance>(sv);
  uint64_t argc = Solo5BootArguments(sv.GetRegionByName("usr").start, SOLO5_USR_REGION_SIZE);
  umi->SetArguments(argc);
  
  // Load instance and set breakpoint for snapshot creation
  umm::manager->Load(std::move(umi));
  ebbrt::Future<umm::UmSV> snap_f = umm::manager->SetCheckpoint(
      umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  // Halt the instance after we've taking the snapshot
  snap_f.Then([this](ebbrt::Future<umm::UmSV> snap_f) {
    // Capture snapshot 
    base_um_env_ = snap_f.Get();
    // Spawn asyncronously to allow the snapshot exception to clean up correctly
    ebbrt::event_manager->SpawnLocal(
        []() { umm::manager->Halt(); /* Return to AppMain */ }, true);
  }); // End snap_f.Then(...)
  // Kick off the instance
  umm::manager->runSV();
  // Return once manager->Halt() is called 
  umm::manager->Unload();
  is_bootstrapped_ = true;
  kprintf_force("Finish Invoker bootstrap on core #%d\n", (size_t)ebbrt::Cpu::GetMine());
  return;
}

void seuss::Invoker::Invoke(uint64_t tid, size_t fid, const std::string args,
                            const std::string code) {
  kassert(is_bootstrapped_);

  // XXX: Clean up ordering semantics here

  /* If the instance is already RUNNING we should either:
        1. Halt and re-deploy the base snapshot-ev
        2. process RUN request, assuming the same fid
        3. WAIT until current function finishes (could be a "faulty" request)
     BUT For now, we'll just abort...
  */

  // XXX: Deal with concurrent calls to invoke!
  kassert(!is_running_);

  // We assume the core does NOT have a running UM instance
  // i.e., umm::manager->Status() == empty

  kassert(!umsesh_);

  // Create a new session for invoking this function
  fid_ = fid;
  ebbrt::NetworkManager::TcpPcb pcb;

  // TODO: Remove arguments from Session constructor and set them via Method call
  // TODO: Or do it all via promise/futures
  umsesh_ = new InvocationSession(std::move(pcb), args, code, tid);
  umsesh_->Install();

  // Load up the base environment
  auto umi2 = std::make_unique<umm::UmInstance>(base_um_env_);
  umm::manager->Load(std::move(umi2));

  // Asynchronously try an setup a connection with the running UMI 
  ebbrt::event_manager->SpawnLocal(
      [this] {
        // Start a new TCP connection with the http request
        // XXX: FIXED IP ADDRESS (NO MULTICORE)!
        std::array<uint8_t, 4> umip = {{169, 254, 1, 0}};
        umsesh_->Pcb().Connect(ebbrt::Ipv4Address(umip), 8080);
      },
      /* force async */ true);

  /* Boot the snapshot */
  is_running_ = true;
  umm::manager->runSV(); // blocks until umm::manager->Halt() is called 
  /* After instance is halted */
  is_running_ = false;
  std::cout << "Unloading core #" << (size_t)ebbrt::Cpu::GetMine() << std::endl;
  umm::manager->Unload();
}

void seuss::Invoker::Resolve(uint64_t tid, std::string ret) {
  std::cout << "Finished RUN #" << tid << " with result: " << ret << std::endl;
  seuss_channel->SendReply(ebbrt::Messenger::NetworkId(ebbrt::runtime::Frontend()), tid, fid_, ret);
  // XXX: No need to clean up the TCP connection, its about to die anyway
  delete umsesh_;
  umsesh_ = nullptr;
  umm::manager->Halt();
  
}

