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

  // Initialize a seuss invoker on each core 
  size_t my_cpu = ebbrt::Cpu::GetMine();
  size_t num_cpus = ebbrt::Cpu::Count();
  kassert(my_cpu == 0);
  for (auto i = my_cpu; i < num_cpus; i++) {
    ebbrt::Promise<void> p; 
    auto f = p.GetFuture();
    ebbrt::event_manager->SpawnRemote(
        [i, &p]() {
          kprintf("Begin seuss invoker on core #%d\n", i);
          seuss::invoker->Bootstrap();
          p.SetValue();
        },
        i);
    f.Block();
  }
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

  size_t event_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                        ebbrt::clock::Wall::Now() - clock_)
                        .count();

  kprintf_force("InvocationSession received response : len=%d\n links=%d",
                       b->ComputeChainDataLength(), b->CountChainElements());

  //TODO: support chain and incomplete buffers (i.e., large replies)
  kassert(!b->IsChained());

  std::string reply(reinterpret_cast<const char *>(b->Data()), b->Length());
  std::string proto_header = reply.substr(0, reply.find_first_of("\r"));

  // TODO: handle failure case
  if(proto_header != "HTTP/1.1 200 OK"){
    std::cout << "FAILED REQUEST: " << reply << std::endl;
    if (is_initialized_)
      std::cout << "*** RUN ***: " << std::endl;
    else
      std::cout << "*** INIT ***: " << std::endl;
    std::cout << function_code_ << std::endl;
    std::cout << "*** ARGS **" << std::endl << run_args_ << std::endl;
    std::cout << "*** ***" << std::endl;
    ebbrt::kabort("HTTP request failed. ");
  }

  std::string response = reply.substr(reply.find_first_of("{"));

  std::cout << "RESPONSE: " << response << std::endl;

  // An "OK":true response signals a finished INIT 
  if(is_initialized_ == false && response == R"({"OK":true})"){
    is_initialized_ = true;
    // Set init_time for the function
    ar_.stats.init_time = event_time;
    // always RUN right after INIT is complete
    if(!run_args_.empty())
      SendHttpRequest("/run", run_args_);
  } else {
    // Force disconnect of the TCP connection
    Pcb().Disconnect();
    ar_.stats.run_time = event_time;
    // finished RUN, send results back to the invoker to resolve 
    seuss::invoker->Resolve(ar_, response);
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

  // start timer
  clock_ = ebbrt::clock::Wall::Now();
  Send(std::move(buf));
}

std::string seuss::InvocationSession::http_post_request(std::string path,
                                                   std::string msg) {
  std::ostringstream payload;
  std::ostringstream ret;

  // strip newlines from msg
  msg.erase(std::remove(msg.begin(), msg.end(), '\n'), msg.end());
  
  kassert(msg.back() == '}');

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
  kassert(!is_bootstrapped_);
  kprintf_force("Bootstrap invocation instance on core #%d\n", (size_t)ebbrt::Cpu::GetMine());

  std::ostringstream optstream;
  optstream << R"({"cmdline":"bin/node-default /nodejsActionBase/app.js",
 "net":{"if":"ukvmif0","cloner":"true","type":"inet","method":"static","addr":"169.254.1.)"
       << (size_t)ebbrt::Cpu::GetMine() << R"(","mask":"16"}})";
  std::string opts  = optstream.str();

  auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);
  auto umi = std::make_unique<umm::UmInstance>(sv);
  uint64_t argc = Solo5BootArguments(sv.GetRegionByName("usr").start,
                                     SOLO5_USR_REGION_SIZE, opts);

  // Set the IP address
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

  if (fid) {
    std::cout << "Begining invocation #" << tid << " [" << code << "][" << args
              << "]" << std::endl;
  } else {
    std::cout << "Restoring invocation #" << tid << " [" << code << "][" << args
              << "]" << std::endl;
  }

  // Queue up any concurrent calls to Invoke on this core!
  if (is_running_) {
    // Core is busy, queue up this invocation request
    auto record = std::make_pair(args, code);
    bool inserted;
    // insert records into the hash tables
    std::tie(std::ignore, inserted) =
        request_map_.emplace(tid, std::move(record));
    // Assert there was no collision on the key
    assert(inserted);
    request_queue_.push(tid);
    return;
  }

  // We assume the core does NOT have a running UM instance
  // TODO: verify that umm::manager->Status() == empty
  kassert(!umsesh_);

  // Create a new session for invoking this function
  fid_ = fid;
  ebbrt::NetworkManager::TcpPcb pcb;

  // TODO: Remove arguments from Session constructor and set them via Method call
  // TODO: Or do it all via promise/futures
  ActivationRecord ac; 
  ac.transaction_id = tid;
  ac.function_id = fid;
  umsesh_ = new InvocationSession(std::move(pcb), ac, args, code);

  // Load up the base environment
  auto umi2 = std::make_unique<umm::UmInstance>(base_um_env_);
  umm::manager->Load(std::move(umi2));

  // Asynchronously try an setup a connection with the running UMI 
  ebbrt::event_manager->SpawnLocal(
      [this] {
        // Start a new TCP connection with the http request
        // XXX: FIXED IP ADDRESS (NO MULTICORE)!
        size_t my_cpu = ebbrt::Cpu::GetMine();
        std::array<uint8_t, 4> umip = {{169, 254, 1,(uint8_t)my_cpu}};
        umsesh_->Pcb().Connect(ebbrt::Ipv4Address(umip), 8080);
      },
      /* force async */ true);

  /* Boot the snapshot */
  is_running_ = true;
  umm::manager->runSV(); // blocks until umm::manager->Halt() is called 
  /* After instance is halted */
  std::cout << "Unloading core #" << (size_t)ebbrt::Cpu::GetMine() << std::endl;
  umm::manager->Unload();
  is_running_ = false;

  // If there's a queued request, let's deploy it
  if(!request_queue_.empty()){
    auto tid = request_queue_.front();
    request_queue_.pop();
    auto req = request_map_.find(tid);
    // TODO: fail gracefully, drop request
    assert(req != request_map_.end());
    auto req_vals = req->second;
    auto args = req_vals.first;
    auto code = req_vals.second;
    request_map_.erase(tid);
    // Invoke this function right away
    std::cout << "Invoking queue request on core #" << (size_t)ebbrt::Cpu::GetMine()
              << " (queue len: " << request_queue_.size() << ")" << std::endl;
    Invoke(tid, 0, args, code);
  }
}

void seuss::Invoker::Resolve(seuss::ActivationRecord ar, std::string ret) {
  auto tid = ar.transaction_id;
  std::cout << "Finished RUN #" << tid << std::endl;
  seuss_channel->SendReply(
      ebbrt::Messenger::NetworkId(ebbrt::runtime::Frontend()), ar, ret);
  delete umsesh_;
  umsesh_ = nullptr;
  umm::manager->Halt();
}

