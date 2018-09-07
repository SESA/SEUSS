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
          kprintf("Core %d: Begin Seuss Invoker\n", i);
          seuss::invoker->Bootstrap();
          p.SetValue();
        },
        i);
    f.Block();
  }
  kprintf_force("\n Finished initialization of all Seuss Invoker cores \n");
}

/* class seuss::InvocationSession */

void seuss::InvocationSession::Connected() {
    // We've established a connection with the instance
    set_connected_.SetValue();
    is_connected_ = true;
    // Send INIT request with code to be initialized 
    if (!function_code_.empty())
      SendHttpRequest("/init", function_code_);
}

void seuss::InvocationSession::Receive(std::unique_ptr<ebbrt::MutIOBuf> b) {
  size_t response_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                             ebbrt::clock::Wall::Now() - clock_)
                             .count();

  //TODO: support chain and incomplete buffers (i.e., large replies)
  kassert(!b->IsChained());

  /* construct a string of the response message */
  std::string reply(reinterpret_cast<const char *>(b->Data()), b->Length());
  std::string response = reply.substr(reply.find_first_of("{"));
  std::string http_status = reply.substr(0, reply.find_first_of("\r"));

  /* Verify if the http request was successful */
  if(http_status != "HTTP/1.1 200 OK"){
    ar_.stats.status = 1; /* INVOCATION FAILED */
    Finish(response);
  }
  /* An {"OK":true} response signals a completed INIT */
  if (response == R"({"OK":true})" && !is_initialized_) {
    is_initialized_ = true;
    ar_.stats.init_time = response_time;
    // Start the RUN request 
    if (!run_args_.empty())
      SendHttpRequest("/run", run_args_);
  }
  /* Any other response signals a completed RUN */
  else {
    ar_.stats.run_time = response_time;
    ar_.stats.status = 0; /* INVOCATION SUCCESSFUL */
    Finish(response);
  }
}

void seuss::InvocationSession::Finish(std::string response) {
  // Force disconnect of the TCP connection
  Pcb().Disconnect();
  seuss::invoker->Resolve(ar_, response);
}

void seuss::InvocationSession::Close() {
  kprintf_force("InvocationSession closed!\n");
}

void seuss::InvocationSession::Abort() {
  kprintf_force("InvocationSession aborted!\n");
}

void seuss::InvocationSession::SendHttpRequest(std::string path,
                                               std::string payload) {
  kassert(payload.size() > 0);
  std::string msg = http_post_request(path, payload);
  auto buf = ebbrt::MakeUniqueIOBuf(msg.size());
  auto dp = buf->GetMutDataPointer();
  auto str_ptr = reinterpret_cast<char *>(dp.Data());
  msg.copy(str_ptr, msg.size());
  clock_ = ebbrt::clock::Wall::Now();
  Send(std::move(buf));
}

std::string seuss::InvocationSession::http_post_request(std::string path,
                                                        std::string msg) {
  std::ostringstream payload;
  std::ostringstream ret;

  // construct json payload formatted for OpenWhisk ActonRunner
  msg.erase(std::remove(msg.begin(), msg.end(), '\n'), msg.end());
  payload << "{\"value\": ";
  if (path == "/init") {
    payload << "{\"main\":\"main\", \"code\":\"" << msg << "\"}}";
  } else {
    payload << msg << "}";
  }

  // build http message header + body
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
  kprintf_force("Bootstrapping Invoker on core #%d\n", (size_t)ebbrt::Cpu::GetMine());

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
  return;
}

void seuss::Invoker::Invoke(uint64_t tid, size_t fid, const std::string args,
                            const std::string code) {
  kassert(is_bootstrapped_);

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
  kprintf("Core %d: Start invocation #%d for function #%d\n", (size_t)ebbrt::Cpu::GetMine(), tid);

  // Create a new session this invocation 
  fid_ = fid;
  ebbrt::NetworkManager::TcpPcb pcb;
  // TODO: Remove arguments from Session constructor and set them via Method call
  // TODO: Or do it all via promise/futures
  ActivationRecord ac={0}; 
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
    // Invoke the function 
    kprintf("CORE %u: Pulling request #%lu from queue (qlen=%d)\n", (size_t)ebbrt::Cpu::GetMine(), tid, request_queue_.size());
    Invoke(tid, 0, args, code);
  }
}

void seuss::Invoker::Resolve(seuss::ActivationRecord ar, std::string ret) {
  seuss_channel->SendReply(
      ebbrt::Messenger::NetworkId(ebbrt::runtime::Frontend()), ar, ret);
  delete umsesh_;
  umsesh_ = nullptr;
  umm::manager->Halt();
}

