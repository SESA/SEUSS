//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <algorithm> /* std::remove */
#include <sstream> /* std::ostringstream */

#include <ebbrt/Debug.h>
#include <ebbrt/Cpu.h>
#include <ebbrt/EventManager.h>
#include <ebbrt/Runtime.h>
#include <ebbrt/Messenger.h>
#include <ebbrt/UniqueIOBuf.h>

#include "SeussInvoker.h"
#include "SeussChannel.h"

#include "InvocationSession.h"
#include "umm/src/UmManager.h"

#define kprintf ebbrt::kprintf
using ebbrt::kprintf_force;

void seuss::Init(){
  auto rep = new SeussChannel(SeussChannel::global_id);
  SeussChannel::Create(rep, SeussChannel::global_id);
  umm::UmManager::Init();
  auto invoker_root = new InvokerRoot();
  invoker_root->ebb_ = Invoker::Create(invoker_root, Invoker::global_id);

	// Init invoker on each core
  size_t num_cpus = ebbrt::Cpu::Count();
  for (size_t i = 0; i < num_cpus; i++) {
    ebbrt::Promise<void> p; 
    auto f = p.GetFuture();
    ebbrt::event_manager->SpawnRemote(
        [&p]() {
          seuss::invoker->Init();
          p.SetValue();
        },
        i);
    f.Block();
  }

  invoker_root->Bootstrap();
  kprintf_force(GREEN "\nFinished initialization of Seuss Invoker (*)\n" RESET);
}

/* class seuss::InvokerRoot */
size_t seuss::InvokerRoot::AddWork(seuss::Invocation i) {
  std::lock_guard<ebbrt::SpinLock> guard(qlock_);
  Invocation record = i; 
  auto tid = i.info.transaction_id;
  // insert records into the hash tables
  bool inserted;
  std::tie(std::ignore, inserted) =
      request_map_.emplace(tid, std::move(record));
  // Assert there was no collision on the key
  assert(inserted);
  request_queue_.push(tid);

  // Inform all the cores about the work starting at a random offset
  size_t num_cpus = ebbrt::Cpu::Count();
  size_t offset = tid % num_cpus;
  for (size_t i = 0; i < num_cpus; i++) {
    auto core = (i + offset) % num_cpus;
    if (core != ebbrt::Cpu::GetMine()) {
      ebbrt::event_manager->SpawnRemote([this]() { ebb_->Poke(); }, core);
    }
  }
  return 0;
}

bool seuss::InvokerRoot::GetWork(Invocation& i) {
  std::lock_guard<ebbrt::SpinLock> guard(qlock_);
  if (request_queue_.empty())
    return false;
  auto tid = request_queue_.front();
  request_queue_.pop();
  auto req = request_map_.find(tid);
  // TODO: fail gracefully, drop request
  assert(req != request_map_.end());
  i = req->second;
  request_map_.erase(tid);
  // Invoke the function
  // kprintf("Core %u: Pulling request #%lu from queue (qlen=%d)\n",
          // (size_t)ebbrt::Cpu::GetMine(), i.info.transaction_id, request_queue_.size());
  return true;
}

umm::UmSV* seuss::InvokerRoot::GetBaseSV() {
  kassert(is_bootstrapped_);
  return base_um_env_;
}

umm::UmSV* seuss::InvokerRoot::GetSnapshot(size_t fid) {
  kassert(is_bootstrapped_);
  auto cache_result = snapmap_.find(fid);
  if(cache_result == snapmap_.end()){
    return nullptr;
  }
  return cache_result->second;
}

bool seuss::InvokerRoot::SetSnapshot(size_t fid, umm::UmSV* sv) {
  kassert(is_bootstrapped_);
  auto cache_result = snapmap_.find(fid);
  if (cache_result != snapmap_.end()) {
    /* CACHE HIT */
    kprintf_force(RED "Wasted Snapshot for fid #%u\n" RESET, fid);
    delete sv;
    return false;
  }
  {
    //TODO: lock?
    bool inserted;
    std::tie(std::ignore, inserted) =
        snapmap_.emplace(fid, sv);
    // Assert there was no collision on the key
    assert(inserted);
    kprintf_force(YELLOW "Snapshot created for fid #%u\n" RESET, fid);
  }
  return true;
}

void seuss::InvokerRoot::Bootstrap() {
  // THIS SHOULD RUN AT MOST ONCE 
  kassert(!is_bootstrapped_);
  kprintf("Bootstrapping InvokerRoot on core #%d nid #%d\n",
                (size_t)ebbrt::Cpu::GetMine(), ebbrt::Cpu::GetMyNode().val());

  // Port naming kludge
  auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);
  auto umi = std::make_unique<umm::UmInstance>(sv);

  // TODO: move this to header
  std::string opts_ = umi_rump_config_;
  kprintf(RED "CONFIG= %s" RESET, opts_.c_str());

  uint64_t argc = Solo5BootArguments(sv.GetRegionByName("usr").start,
                                     SOLO5_USR_REGION_SIZE, opts_);
  // Set the IP address
  umi->SetArguments(argc);
  // Load instance and set breakpoint for snapshot creation
  ebbrt::Future<umm::UmSV*> snap_f = umi->SetCheckpoint(
      umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  // Halt the instance after we've taking the snapshot
  snap_f.Then([this](ebbrt::Future<umm::UmSV*> snap_f) {
    // Capture snapshot 
    base_um_env_ = snap_f.Get();
    // Spawn asyncronously to allow the snapshot exception to clean up
    // correctly
    ebbrt::event_manager->SpawnLocal(
        []() {
          umm::manager->Halt(); /* Return to AppMain */
        },
        true);
  }); // End snap_f.Then(...)
  // Kick off the instance
  umm::manager->Run(std::move(umi));
  // Return once manager->Halt() is called

  // Kick off the invoker on all cores 
  size_t num_cpus = ebbrt::Cpu::Count();
  size_t my_cpu = ebbrt::Cpu::GetMine();
  for (size_t i = 0; i < num_cpus; i++) {
    if(i == my_cpu){
      ebbrt::event_manager->SpawnLocal([this]() { ebb_->Poke(); });
    }else{
      ebbrt::event_manager->SpawnRemote([this]() { ebb_->Poke(); }, i);
    }
  }
  is_bootstrapped_ = true;
  return;
}

/* class seuss::Invoker */

bool seuss::Invoker::process_warm_start(seuss::Invocation i) {

  // kprintf(YELLOW "Processing WARM start \n" RESET);
  uint64_t tid = i.info.transaction_id;
  size_t fid = i.info.function_id;
  const std::string args = i.args;
  const std::string code = i.code;


  /* Load up the base snapshot environment */
  // kprintf(YELLOW "Loading up base env\n" RESET);
  auto base_env = root_.GetBaseSV();
  auto umi = std::make_unique<umm::UmInstance>(*base_env);

  ebbrt::Future<umm::UmSV*> hot_sv_f = umi->SetCheckpoint(
      umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  // When you have the sv, cache it for future use.
  hot_sv_f.Then([this, fid](ebbrt::Future<umm::UmSV *> f) {
    root_.SetSnapshot(fid, std::move(f.Get()));
  });

  auto umi_id = umi->Id();
  auto umsesh = create_session(tid, fid);

  /* Setup the asyncronous operations on the InvocationSession */
  umsesh->WhenConnected().Then(
      [this, umsesh, code](auto f) {
        // kprintf(YELLOW "Connected, sending init\n" RESET);
        umsesh->SendHttpRequest("/init", code, true /* keep_alive */);
      });

  umsesh->WhenInitialized().Then(
      [this, umsesh, args](auto f) {
        // kprintf(YELLOW "Finished function init, sending run args: %s\n" RESET, args.c_str());
        umsesh->SendHttpRequest("/run", args, false);
      });

  // Halt when closed or aborted
  umsesh->WhenClosed().Then([this, umi_id](auto f) {
    // kprintf(YELLOW "Connection Closed...\n" RESET);
    ebbrt::event_manager->SpawnLocal(
        [this, umi_id] {
          //kprintf(YELLOW "Connection Closed...\n" RESET);
          umm::manager->SignalHalt(umi_id); /* Return to back to init_code_and_snap */
        },
        /* force async */ true);
  });

  umsesh->WhenAborted().Then([this, umi_id](auto f) {
    ebbrt::event_manager->SpawnLocal(
        [this, umi_id] {
          kprintf(RED "SESSION ABORTED...\n" RESET);
          umm::manager->SignalHalt(umi_id); /* Return to back to init_code_and_snap */
        },
        /* force async */ true);
  });

  umm::manager->Load(std::move(umi)).Block(); // block until core is loaded 

  /* Spawn a new event to make a connection with the instance */
  ebbrt::event_manager->SpawnLocal(
      [this, umsesh] {
        // Start a new TCP connection with the http request
        //kprintf(YELLOW "Warm start connect \n" RESET);
				auto port = get_internal_port();
        umsesh->Pcb().Connect(umm::UmInstance::CoreLocalIp(), 8080, port);
      },
      /* force async */ true);

  /* Boot the snapshot */
  is_running_ = true;

  // umm::manager->Run(std::move(umi));
  umm::manager->Start(umi_id);// block until call to manager->Halt() 
  /* RETURN HERE AFTER HALT */
  delete umsesh;
  is_running_ = false;
  //kprintf(YELLOW "Finished WARM start \n" RESET);
  return true;
}

bool seuss::Invoker::process_hot_start(seuss::Invocation i) {

  // kprintf(RED "Processing HOT start \n" RESET);
  uint64_t tid = i.info.transaction_id;
  size_t fid = i.info.function_id;
  const std::string args = i.args;
  const std::string code = i.code;

  // TODO: this in each start instead of here?


  /* Check snapshot cache for function-specific snapshot */
  auto cached_snap = root_.GetSnapshot(fid); // um_sv_map_.find(fid);
  if (unlikely(cached_snap == nullptr)) {
    kprintf_force(RED "Error: no snapshot for fid %u\n" RESET, fid);
		ebbrt::kabort();
  }
  //kprintf("invoker_core %d: Invocation cache HIT for function #%u\n",
  //        (size_t)ebbrt::Cpu::GetMine(), fid);

  auto umsesh = create_session(tid, fid);
  auto umi = std::make_unique<umm::UmInstance>(*cached_snap);
  auto umi_id = umi->Id();

  umsesh->WhenConnected().Then(
      [this, umsesh, args](auto f) {
        // kprintf(RED "Connection open, sending run...\n" RESET);
        umsesh->SendHttpRequest("/run", args, false); });

  // Halt when closed
  umsesh->WhenClosed().Then([this, umi_id](auto f) {
    ebbrt::event_manager->SpawnLocal(
        [this, umi_id] {
           //kprintf(GREEN "Connection Closed...\n" RESET);
          umm::manager->SignalHalt(umi_id); /* Return to back to init_code_and_snap */
        },
        /* force async */ true);
  });
  umsesh->WhenAborted().Then([this, umi_id](auto f) {
    ebbrt::event_manager->SpawnLocal(
        [this, umi_id] {
          kprintf(RED "SESSION ABORTED...\n" RESET);
          umm::manager->SignalHalt(umi_id); /* Return to back to init_code_and_snap */
        },
        /* force async */ true);
  });

  /* Boot the snapshot */
  is_running_ = true;

  umm::manager->Load(std::move(umi)).Block(); // block until core is loaded
  //kprintf(RED "UMI #%d is loaded and about to begin hot start\n" RESET, umi_id);

  /* Spawn a new event to make a connection with the instance */
  ebbrt::event_manager->SpawnLocal(
      [this, umsesh] {
        // Start a new TCP connection with the http request
        //kprintf(RED "Hot start connect \n" RESET);
				auto port = get_internal_port();
        umsesh->Pcb().Connect(umm::UmInstance::CoreLocalIp(), 8080, port);
      },
      /* force async */ true);

  /* Boot the snapshot */
  is_running_ = true;

  // umm::manager->Run(std::move(umi));
  umm::manager->Start(umi_id);// block until call to manager->Halt() 
  /* RETURN HERE AFTER HALT */
  delete umsesh;
  is_running_ = false;
  //kprintf(RED "Finished HOT start \n" RESET);
  return true;
}

seuss::InvocationSession* seuss::Invoker::create_session(uint64_t tid, size_t fid) {
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

void seuss::Invoker::Queue(seuss::Invocation i) {
  root_.AddWork(i);
  return;
}

void seuss::Invoker::Poke(){
  Invocation i;
  if (request_concurrency_ == request_concurrency_limit)
    return;
  if(root_.GetWork(i)){
    Invoke(i);
  }
}

bool ctr_init[] = {false, false, false};
void seuss::Invoker::Invoke(seuss::Invocation i) {
  uint64_t tid = i.info.transaction_id;
  size_t fid = i.info.function_id;
  const std::string args = i.args;
  const std::string code = i.code;
  ++request_concurrency_;

  //kprintf("invoker_core_%d received invocation: (%u, %u)\n CODE: %s\n ARGS: %s\n",
  //        (size_t)ebbrt::Cpu::GetMine(), tid, fid, code.c_str(), args.c_str());

  kprintf_force("C(%d): start invocation tid=%u, fid=%u\n",
          (size_t)ebbrt::Cpu::GetMine(), tid, fid);

  /* Check for a snapshot in the cache */
  auto cached_snap = root_.GetSnapshot(fid); // um_sv_map_.find(fid);
  if (cached_snap == nullptr) {
    /* CACHE MISS */
    process_warm_start(i);
  }else{
    /* CACHE HIT */
    process_hot_start(i);
  }

  //kprintf_force("C(%d): finish invocation tid=%u, fid=%u\n",
  //        (size_t)ebbrt::Cpu::GetMine(), tid, fid);
  --request_concurrency_;

  /* Check the shared queue for additional work to be done */
  Invocation next_i;
  if (root_.GetWork(next_i)) {
    //kprintf_force("C(%D): invoking from queue\n",
    //              (size_t)ebbrt::Cpu::GetMine());
    ebbrt::event_manager->SpawnLocal([this, next_i]() { Invoke(next_i); }, true);
  }


}

void seuss::Invoker::Init() {
  // Pre-allocate event stacks
  ebbrt::event_manager->PreAllocateStacks(256);
  kprintf("invoker_core_%d is online\n", (size_t)ebbrt::Cpu::GetMine());
	// Could call umm::manager->slot_has_instance
}

void seuss::Invoker::Resolve(seuss::InvocationStats istats, std::string ret) {
  seuss_channel->SendReply(
      ebbrt::Messenger::NetworkId(ebbrt::runtime::Frontend()), istats, ret);
}

