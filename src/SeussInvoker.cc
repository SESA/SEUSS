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

/* class suess::Invoker */

void seuss::Invoker::Bootstrap() {
  kassert(!is_bootstrapped_);
  kprintf_force("Bootstrapping Invoker on core #%d\n", (size_t)ebbrt::Cpu::GetMine());

  std::ostringstream optstream;
  // TODO: avoid locking operation
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

bool seuss::Invoker::process_warm_start(size_t fid, std::string code, std::string args){

    kprintf_force(YELLOW "Processing WARM start \n" RESET);

    ebbrt::Future<umm::UmSV> hot_sv_f = umm::manager->SetCheckpoint(
        umm::ElfLoader::GetSymbolAddress("uv_uptime"));

    hot_sv_f.Then([this, fid](ebbrt::Future<umm::UmSV> f) {
      // Capture snapshot
      bool inserted;
      std::tie(std::ignore, inserted) =
          um_sv_map_.emplace(fid, std::move(f.Get()));
      // Assert there was no collision on the key
      assert(inserted);
      kprintf_force(YELLOW "Cached initialized SV for future HOT starts.\n" RESET);
    }); // End hot_sv_f.Then(...)

    /* Setup the asyncronous operations on the InvocationSession */
    umsesh_->WhenConnected().Then(
        [this, code](auto f) { umsesh_->SendHttpRequest("/init", code); });

    umsesh_->WhenInitialized().Then(
        [this, args](auto f) { umsesh_->SendHttpRequest("/run", args); });

    umsesh_->WhenClosed().Then([this](auto f) {
      kprintf_force(GREEN "Connection Closed...\n" RESET);
      ebbrt::event_manager->SpawnLocal(
          [this] {
            umm::manager->Halt(); /* Return to back to init_code_and_snap */
          },
          /* force async */ true);
    });

    /* Spawn a new event to make a connection with the instance */
    ebbrt::event_manager->SpawnLocal(
        [this] {
          // Start a new TCP connection with the http request
          size_t my_cpu = ebbrt::Cpu::GetMine();
          std::array<uint8_t, 4> umip = {{169, 254, 1, (uint8_t)my_cpu}};
          umsesh_->Pcb().Connect(ebbrt::Ipv4Address(umip), 8080, 49159);
        },
        /* force async */ true);

    /* Load up the base snapshot environment */
    auto umi2 = std::make_unique<umm::UmInstance>(base_um_env_);
    umm::manager->Load(std::move(umi2));
    /* Boot the snapshot */
    is_running_ = true;
    umm::manager->runSV(); // blocks until umm::manager->Halt() is called

    /* RETURN HERE AFTER HALT */
    // XXX: memory leak
    umsesh_ = nullptr;
    umm::manager->Unload();
    is_running_ = false;
    kprintf_force(YELLOW "Finished WARM start \n" RESET);
    return true;
}

void seuss::Invoker::Invoke(uint64_t tid, size_t fid, const std::string args,
                            const std::string code) {
  kassert(is_bootstrapped_);

  kprintf("Core %d: Got invocation #%d for function #%u\n",
          (size_t)ebbrt::Cpu::GetMine(), tid, fid);

  /* Queue the invocation if the core if busy */
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
  kprintf("Core %d: Processing #%d for function #%u\n",
          (size_t)ebbrt::Cpu::GetMine(), tid, fid);
  // Create a new session this invocation
  fid_ = fid;
  ebbrt::NetworkManager::TcpPcb pcb;
  InvocationStats istats = {0};
  istats.transaction_id = tid;
  istats.function_id = fid;
  umsesh_ = new InvocationSession(std::move(pcb), istats);

  /* Check for a snapshot cache MISS */
  {
    auto cache_result = um_sv_map_.find(fid);
    if (cache_result == um_sv_map_.end()) {
      /* CACHE MISS */
      process_warm_start(fid, code, args);
      return; 
    } // end CACHE MISS
  }

    kprintf_force(RED "Processing HOT start \n" RESET);
  /* Check snapshot cache for function-specific snapshot */
  auto cache_result = um_sv_map_.find(fid);
  assert(cache_result != um_sv_map_.end());

  kprintf("Core %d: Invocation cache HIT for function #%d\n",
          (size_t)ebbrt::Cpu::GetMine(), fid);

  /* Spawn a new event to make a connection with the instance */
  ebbrt::event_manager->SpawnLocal(
      [this] {
      // Start a new TCP connection with the http request
      size_t my_cpu = ebbrt::Cpu::GetMine();
      std::array<uint8_t, 4> umip = {{169, 254, 1, (uint8_t)my_cpu}};
      umsesh_->Pcb().Connect(ebbrt::Ipv4Address(umip), 8080);
      },
      /* force async */ true);

  umsesh_->WhenConnected().Then(
      [this, args](auto f) { umsesh_->SendHttpRequest("/run", args); });

  umsesh_->WhenClosed().Then([this](auto f) {
    kprintf_force(GREEN "Connection Closed...\n" RESET);
    ebbrt::event_manager->SpawnLocal(
        [this] {
          umm::manager->Halt(); /* Return to back to init_code_and_snap */
        },
        /* force async */ true);
  });

  auto umi2 = std::make_unique<umm::UmInstance>(cache_result->second);
  umm::manager->Load(std::move(umi2));
  /* Boot the snapshot */
  is_running_ = true;
  umm::manager->runSV(); // blocks until umm::manager->Halt() is called 
  /* After instance is halted */
  /* RETURN HERE AFTER HALT */
  // XXX: memory leak
  umsesh_ = nullptr;
  umm::manager->Unload();
  is_running_ = false;
    kprintf_force(RED "Finished HOT start \n" RESET);

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
      kprintf("Core %u: Pulling request #%lu from queue (qlen=%d)\n",
              (size_t)ebbrt::Cpu::GetMine(), tid, request_queue_.size());

      Invoke(tid, 0, args, code);
  }
  }

  void seuss::Invoker::Resolve(seuss::InvocationStats istats, std::string ret) {
    seuss_channel->SendReply(
        ebbrt::Messenger::NetworkId(ebbrt::runtime::Frontend()), istats, ret);
#if 0
    delete umsesh_;
    umsesh_ = nullptr;
    umm::manager->Halt();
#endif
  }

