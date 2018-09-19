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

#include "InvocationSession.h"
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
  kprintf_force(GREEN "\nFinished initialization of all Seuss invoker cores \n" RESET);
}

/* class suess::Invoker */

void seuss::Invoker::Bootstrap() {
  kassert(!is_bootstrapped_);
  kprintf_force("Bootstrapping Invoker on core #%d\n", (size_t)ebbrt::Cpu::GetMine());

  // Port naming kludge
  base_port_ = 49160 + (size_t)ebbrt::Cpu::GetMine(); 
  auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);
  auto umi = std::make_unique<umm::UmInstance>(sv);
  uint64_t argc = Solo5BootArguments(sv.GetRegionByName("usr").start,
                                     SOLO5_USR_REGION_SIZE, umi_config_);

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

void seuss::Invoker::queue_invocation(uint64_t tid, size_t fid, const std::string args,
                                     const std::string code) {
   // Core is busy, queue up this invocation request
   auto record = std::make_tuple(fid, args, code);
   bool inserted;
   // insert records into the hash tables
   std::tie(std::ignore, inserted) =
     request_map_.emplace(tid, std::move(record));
   // Assert there was no collision on the key
   assert(inserted);
   request_queue_.push(tid);
}

bool seuss::Invoker::process_warm_start(size_t fid, uint64_t tid, std::string code,
                                        std::string args) {

  kprintf(YELLOW "Processing WARM start \n" RESET);

  // TODO: this in each start instead of here?
  umsesh_ = create_session(tid, fid);

  ebbrt::Future<umm::UmSV> hot_sv_f = umm::manager->SetCheckpoint(
      umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  // When you have the sv, cache it for future use.
  hot_sv_f.Then([this, fid](ebbrt::Future<umm::UmSV> f) {
    // Capture snapshot
    bool inserted;
    std::tie(std::ignore, inserted) =
        um_sv_map_.emplace(fid, std::move(f.Get()));
    // Assert there was no collision on the key
    assert(inserted);
      kprintf_force(YELLOW "Snapshot created for fid #%u\n" RESET, fid);
  }); // End hot_sv_f.Then(...)

  /* Setup the asyncronous operations on the InvocationSession */
  umsesh_->WhenConnected().Then(
      [this, code](auto f) {
        kprintf(YELLOW "Connected, sending init\n" RESET);
        umsesh_->SendHttpRequest("/init", code);
      });

  umsesh_->WhenInitialized().Then(
      [this, args](auto f) {
        kprintf(YELLOW "Finished function init\n" RESET);
      });

  // Halt when closed
  umsesh_->WhenClosed().Then([this](auto f) {
    kprintf(YELLOW "Connection Closed...\n" RESET);
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
        kprintf(YELLOW "Warm start connect \n" RESET);
        std::array<uint8_t, 4> umip = {{169, 254, 1, 1}};
        umsesh_->Pcb().Connect(ebbrt::Ipv4Address(umip), 8080, base_port_+=ebbrt::Cpu::Count());
      },
      /* force async */ true);

  /* Load up the base snapshot environment */
  kprintf(YELLOW "Loading up base env\n" RESET);
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
  kprintf(YELLOW "Finished WARM start \n" RESET);
  return true;
}

bool seuss::Invoker::process_hot_start(size_t fid, uint64_t tid, std::string args) {

  kprintf(RED "Processing HOT start \n" RESET);

  // TODO: this in each start instead of here?
  umsesh_ = create_session(tid, fid);

  /* Check snapshot cache for function-specific snapshot */
  kprintf(RED "Searching for fn %d\n" RESET, fid);
  auto cache_result = um_sv_map_.find(fid);
  if(unlikely(cache_result == um_sv_map_.end())){
    kprintf_force(RED "Error: no snapshot for fid %u\n" RESET, fid);
		ebbrt::kabort();
  }

  kprintf("invoker_core %d: Invocation cache HIT for function #%u\n",
          (size_t)ebbrt::Cpu::GetMine(), fid);

  /* Spawn a new event to make a connection with the instance */
  ebbrt::event_manager->SpawnLocal(
      [this] {
        kprintf(RED "Hot start connect ...\n" RESET);
        // Start a new TCP connection with the http request
        std::array<uint8_t, 4> umip = {{169, 254, 1, 1}};
        umsesh_->Pcb().Connect(ebbrt::Ipv4Address(umip), 8080, base_port_+=ebbrt::Cpu::Count());
      },
      /* force async */ true);

  umsesh_->WhenConnected().Then(
      [this, args](auto f) {
        kprintf(RED "Connection open, sending run...\n" RESET);
        umsesh_->SendHttpRequest("/run", args); });

  // Halt when closed
  umsesh_->WhenClosed().Then([this](auto f) {
    kprintf(GREEN "Connection Closed...\n" RESET);
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
  return true;
}

void seuss::Invoker::deploy_queued_request(){
  auto tid = request_queue_.front();
  request_queue_.pop();
  auto req = request_map_.find(tid);
  // TODO: fail gracefully, drop request
  assert(req != request_map_.end());
  auto req_vals = req->second;
  size_t fid = std::get<0>(req_vals);
  std::string args = std::get<1>(req_vals);
  std::string code = std::get<2>(req_vals);
  request_map_.erase(tid);
  // Invoke the function
  kprintf("Core %u: Pulling request #%lu from queue (qlen=%d)\n",
          (size_t)ebbrt::Cpu::GetMine(), tid, request_queue_.size());
  // TODO: Weird recursion, why not drive from outside?
  Invoke(tid, fid, args, code);
}

seuss::InvocationSession* seuss::Invoker::create_session(uint64_t tid, size_t fid) {
  fid_ = fid;
  // NOTE: old way of stack allocating pcb and istats.
  // ebbrt::NetworkManager::TcpPcb pcb;
  // InvocationStats istats = {0};

  auto pcb = new ebbrt::NetworkManager::TcpPcb;
  auto isp = new InvocationStats;

  *isp = {0};
  (*isp).transaction_id = tid;
  (*isp).function_id = fid;
  // TODO: leak
  return new InvocationSession(std::move(*pcb), *isp);
}

void seuss::Invoker::Invoke(uint64_t tid, size_t fid, const std::string args,
                            const std::string code) {
  kassert(is_bootstrapped_);

  kprintf("invoker_core_%d received invocation: (%u, %u)\n",
          (size_t)ebbrt::Cpu::GetMine(), tid, fid);

  /* Queue the invocation if the core if busy */
  if (is_running_) {
    queue_invocation(tid, fid, args, code);
    return;
  }

  // We assume the core does NOT have a running UM instance
  // TODO: verify that umm::manager->Status() == empty
  kassert(!umsesh_);
  kprintf("invoker_core_%d starting invocation: (%u, %u)\n",
          (size_t)ebbrt::Cpu::GetMine(), tid, fid);

  // Create a new session this invocation
  // TODO: is stack allocated what we want?
  // {
  //   fid_ = fid;
  //   ebbrt::NetworkManager::TcpPcb pcb;
  //   InvocationStats istats = {0};
  //   istats.transaction_id = tid;
  //   istats.function_id = fid;
  //   umsesh_ = new InvocationSession(std::move(pcb), istats);
  // }


  /* Check for a snapshot cache MISS */
  auto cache_result = um_sv_map_.find(fid);
  if (cache_result == um_sv_map_.end()) {
    /* CACHE MISS */
    process_warm_start(fid, tid, code, args);
  }
  process_hot_start(fid, tid, args);
  kprintf_force("invoker_core_%d finished invocation: (%u, %u)\n",
          (size_t)ebbrt::Cpu::GetMine(), tid, fid);
  // Make sure any pending shit from warm start runs.
  // ebbrt::event_manager->SpawnLocal(
  //     [this, fid, args]() {
  //       kprintf_force(MAGENTA "Invoke hot start!\n" RESET);
        // process_hot_start(fid, args);

        // If there's a queued request, let's deploy it
        // TODO: control this in an outter loop? Weird recursion inside.
  if (!request_queue_.empty())
    deploy_queued_request();
      // }, /* force async */ true);
}

void seuss::Invoker::Resolve(seuss::InvocationStats istats, std::string ret) {
  seuss_channel->SendReply(
      ebbrt::Messenger::NetworkId(ebbrt::runtime::Frontend()), istats, ret);
  }

