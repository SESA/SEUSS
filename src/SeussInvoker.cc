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
  kprintf_force(GREEN "\nFinished initialization of Seuss Invoker(!)\n" RESET);
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
  kassert(inserted);
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
  kassert(req != request_map_.end());
  i = req->second;
  request_map_.erase(tid);
  return true;
}

umm::UmSV* seuss::InvokerRoot::GetBaseSV() {
  kassert(is_bootstrapped_);
  return base_um_env_;
}

umm::UmSV* seuss::InvokerRoot::GetSnapshot(size_t fid) {
  kassert(is_bootstrapped_);
  std::lock_guard<ebbrt::SpinLock> guard(qlock_);
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
    kprintf(RED "Wasted Snapshot for fid #%u\n" RESET, fid);
    delete sv;
    return false;
  }
  // Do we have room for another snapshot?
  if (snapmap_.size() >= default_snapmap_limit) {
    // Here is where we could evict a snapshot
    kprintf(YELLOW "No room left for snapshot #%u\n" RESET, fid);
    delete sv;
    return false;
  }
  // Save the snapshot into the snapmap
  { 
    //TODO: lock?
    bool inserted;
    std::tie(std::ignore, inserted) =
        snapmap_.emplace(fid, sv);
    // Assert there was no collision on the key
    kassert(inserted);
    kprintf(YELLOW "Snapshot created for fid #%u\n" RESET, fid);
  }
  return true;
}

#if 0
bool seuss::InvokerRoot::DeleteSnapshot(size_t fid) {
  kassert(is_bootstrapped_);
  auto cache_result = snapmap_.find(fid);
  // confirm snapshot exists
  kbugon(cache_result == snapmap_.end());
  {
    // Assert there was no collision on the key
    snapmap_.erase(cache_result);
    kprintf(YELLOW "Snapshot deleted for fid #%u\n" RESET, fid);
  }
  return true;
}

size_t seuss::InvokerRoot::EvictSnapshot() {
  kbugon(snapmap_.size() == 0);
  // Evict a random entry
  uint64_t seed = ebbrt::random::Get() % snapmap_.size();
  kbugon(seed >= snapmap_.size());
  auto random_snap = std::next(std::begin(snapmap_), seed);
  kbugon(cache_result == snapmap_.end());
  auto fid = cache_result->first;
  kprintf(YELLOW "Evicting snapshot for fid #%u\n" RESET, fid);
  delete cache_result->second;
  snapmap_.erase(cache_result);
}
#endif

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

/* End seuss::InvokerRoot */

/* Start seuss::Invoker */

void seuss::Invoker::Init() {
  // Pre-allocate event stacks
  ebbrt::event_manager->PreAllocateStacks(256);

  // args from the multiboot command line
  auto cl = std::string(ebbrt::multiboot::CmdLine());

  // invoker core concurrency_limit
  {
    auto zkstr = std::string("Clim=");
    auto loc = cl.find(zkstr);
    if (loc == std::string::npos) {
      request_concurrency_limit_ = default_concurrency_limit;
    } else {
      auto clim_str = cl.substr((loc + zkstr.size()));
      auto gap = clim_str.find(";");
      if (gap != std::string::npos) {
        clim_str = clim_str.substr(0, gap);
      }
      request_concurrency_limit_ = atoi(clim_str.c_str());
    }
  }
  // invoker core spicy start limit
  {
    auto zkstr = std::string("Slim=");
    auto loc = cl.find(zkstr);
    if (loc == std::string::npos) {
      spicy_limit_ = 0;
      spicy_reuse_limit_ = 0;
    }else{
      auto clim_str = cl.substr((loc + zkstr.size()));
      auto gap = clim_str.find(";");
      if (gap != std::string::npos) {
        clim_str = clim_str.substr(0, gap);
      }
      spicy_limit_ = atoi(clim_str.c_str());
      // check for reuse
      zkstr = std::string("Rlim=");
      loc = cl.find(zkstr);
      if (loc == std::string::npos) {
        spicy_reuse_limit_ = default_instance_reuse_limit;
      }else{
        auto rlim_str = cl.substr((loc + zkstr.size()));
        auto gap = rlim_str.find(";");
        if (gap != std::string::npos) {
          rlim_str = rlim_str.substr(0, gap);
        }
        spicy_reuse_limit_ = atoi(rlim_str.c_str());
      }
    }
  }
  kprintf("invoker_core_%d is online\n", (size_t)ebbrt::Cpu::GetMine());
  if ((size_t)ebbrt::Cpu::GetMine() == 0) {
    kprintf_force("invoker_core instance concurrency limit: %d\n",
                  request_concurrency_limit_);
    kprintf_force(
        "invoker_core instance reuse limits: %d idle / %d reuses\n",
        spicy_limit_, spicy_reuse_limit_);
  }
}

void seuss::Invoker::Queue(seuss::Invocation i) {
  root_.AddWork(i);
  return;
}

// Poke() is the stupid/arbitrary way we schedule functions deployments
void seuss::Invoker::Poke(){
  Invocation i;
  if (spicy_is_enabled()) {
    // Proceed only if core is doing no work
    if (request_concurrency_ == 0) {
      if (root_.GetWork(i)) {
        Invoke(i);
      }
    }
    return;
  }
  // Proceed only if we have capacity on this core to do so
  if (request_concurrency_ >= request_concurrency_limit_) {
    return;
  }
  if (root_.GetWork(i)) {
    Invoke(i);
  }
}

void seuss::Invoker::Invoke(seuss::Invocation i) {
  size_t fid = i.info.function_id;
  const std::string args = i.args;
  const std::string code = i.code;
  ++request_concurrency_;

  /* Check for a snapshot in the cache */
  auto cached_snap = root_.GetSnapshot(fid); 
  if (cached_snap == nullptr) {
    /* snapshot cache miss */
    process_warm_start(i);
  } else if (spicy_is_enabled() && check_ready_instance(fid)) {
    process_spicy_start(i);
    return;
  } else {
    /* snapshot cache hit */
    process_hot_start(i);
  }
}

void seuss::Invoker::Resolve(seuss::InvocationStats istats, std::string ret) {
  seuss_channel->SendReply(
      ebbrt::Messenger::NetworkId(ebbrt::runtime::Frontend()), istats, ret);
}

bool seuss::Invoker::process_warm_start(seuss::Invocation i) {

  // kprintf(YELLOW "Processing WARM start \n" RESET);
  uint64_t tid = i.info.transaction_id;
  size_t fid = i.info.function_id;
  const std::string aid  = i.info.activation_id;
  const std::string args = i.args;
  const std::string code = i.code;

  /* Load up the base snapshot environment */
  // kprintf(YELLOW "Loading up base env\n" RESET);
  auto base_env = root_.GetBaseSV();
  auto umi = std::make_unique<umm::UmInstance>(*base_env);
  auto umi_id = umi->Id();
  kprintf_force("C(%d): warm start %s, %u, %u\n",
          (size_t)ebbrt::Cpu::GetMine(), aid.c_str(), fid, umi_id);

  /* Snapshotting */
  ebbrt::Future<umm::UmSV*> hot_sv_f = umi->SetCheckpoint(
      umm::ElfLoader::GetSymbolAddress("uv_uptime"));
  // When you have the sv, cache it for future use.
  hot_sv_f.Then([this, fid](ebbrt::Future<umm::UmSV *> f) {
    root_.SetSnapshot(fid, std::move(f.Get()));
  });

  auto umsesh = create_session(i.info);

  /* Setup the operation handlers of the InvocationSession */
  umsesh->WhenConnected().Then(
      [this, umsesh, code](auto f) {
        // kprintf(YELLOW "Connected, sending init\n" RESET);
        umsesh->SendHttpRequest("/init", code, true /* keep_alive */);
      });

  umsesh->WhenInitialized().Then(
      [this, umsesh, args](auto f) {
        // kprintf(YELLOW "Finished function init, sending run args: %s\n" RESET, args.c_str());
        umsesh->SendHttpRequest("/run", args, false /* keep_alive */);
      });

  // Halt when closed or aborted
  umsesh->WhenClosed().Then([this, umi_id, fid](auto f) {
    this->request_concurrency_--;
    ebbrt::event_manager->SpawnLocal([this]() { this->Poke(); }, true);
    if (this->spicy_is_enabled()) {
      /* To enable magma hot starts we need to keep this instance booted */
      if (!(this->check_ready_instance(fid)) &&
          this->instance_can_be_reused(umi_id)) {
        /* No existing instance exists for this function on this core*/
        auto umi = umm::manager->GetInstance(umi_id);
        kassert(umi);
        umi->EnableYield();
        this->save_ready_instance(fid, umi_id);
        umm::manager->SignalYield(umi_id);
        return;
      }
    }
    // Otherwise continue and halt the instance 
    ebbrt::event_manager->SpawnLocal(
        [this, umi_id] { umm::manager->SignalHalt(umi_id); },
        /* force async */ true);

  });

  umsesh->WhenAborted().Then([this, umi_id](auto f) {
    ebbrt::event_manager->SpawnLocal(
        [this, umi_id] { umm::manager->SignalHalt(umi_id); },
        /* force async */ true);
  });

  umm::manager->Load(std::move(umi)).Block(); // block until core is loaded

  /* Spawn a new event to make a connection with the instance */
  ebbrt::event_manager->SpawnLocal(
      [this, umsesh] {
        // Start a new TCP connection with the http request
        umsesh->Connect();
      },
      /* force async */ true);

  /* Boot the snapshot */
  umm::manager->Start(umi_id); // block until call to manager->Halt()
  /* RETURN HERE AFTER HALT */
  delete umsesh;
   kprintf(YELLOW "C(%d): warm start finished %s, %u, %u\n" RESET,
                 (size_t)ebbrt::Cpu::GetMine(), aid.c_str(), fid, umi_id);
   return true;
}

bool seuss::Invoker::process_hot_start(seuss::Invocation i) {

    uint64_t tid = i.info.transaction_id;
    size_t fid = i.info.function_id;
    const std::string aid = i.info.activation_id;
    const std::string args = i.args;
    const std::string code = i.code;

    /* Check snapshot cache for function-specific snapshot */
    auto cached_snap = root_.GetSnapshot(fid);
    if (unlikely(cached_snap == nullptr)) {
      kprintf_force(RED "Error: no snapshot for fid %u\n" RESET, fid);
      ebbrt::kabort();
  }
  auto umi = std::make_unique<umm::UmInstance>(*cached_snap);
  auto umi_id = umi->Id();
  kprintf_force("C(%d): hot start %s, %u, %u\n",
          (size_t)ebbrt::Cpu::GetMine(), aid.c_str(), fid, umi_id);

  // Create session
  auto umsesh = create_session(i.info);

  /* Setup the operation handlers of the InvocationSession */
  umsesh->WhenConnected().Then(
      [this, umsesh, args](auto f) {
        // kprintf(RED "Connection open, sending run...\n" RESET);
        umsesh->SendHttpRequest("/run", args, false); });

  // Halt when closed
  umsesh->WhenClosed().Then([this, umi_id, fid](auto f) {
    this->request_concurrency_--;
    ebbrt::event_manager->SpawnLocal([this]() { this->Poke(); }, true);
    if (this->spicy_is_enabled()) {
      /* To enable spicy hot starts we need to keep this instance booted */
      if (!(this->check_ready_instance(fid)) &&
          this->instance_can_be_reused(umi_id)) {
        /* No existing instance exists for this function on this core*/
        auto umi = umm::manager->GetInstance(umi_id);
        kassert(umi);
        umi->EnableYield();
        this->save_ready_instance(fid, umi_id);
        umm::manager->SignalYield(umi_id);
        return;
      }
    }
    // Otherwise continue and halt the instance 
    ebbrt::event_manager->SpawnLocal(
        [this, umi_id] {
           //kprintf(GREEN "Connection Closed...\n" RESET);
          umm::manager->SignalHalt(umi_id); 
        },
        /* force async */ true);
  });
  umsesh->WhenAborted().Then([this, umi_id](auto f) {
    ebbrt::event_manager->SpawnLocal(
        [this, umi_id] {
          kprintf_force(RED "SESSION ABORTED...\n" RESET);
          umm::manager->SignalHalt(umi_id); /* Return to back to init_code_and_snap */
        },
        /* force async */ true);
  });

  /* Boot the snapshot */
  umm::manager->Load(std::move(umi)).Block(); // block until core is loaded
  //kprintf(RED "UMI #%d is loaded and about to begin hot start\n" RESET, umi_id);

  /* Spawn a new event to make a connection with the instance */
  ebbrt::event_manager->SpawnLocal(
      [this, umsesh] {
        // Start a new TCP connection with the http request
        umsesh->Connect();
      },
      /* force async */ true);

  /* Boot the snapshot */
  umm::manager->Start(umi_id);// block until call to manager->Halt() 
  /* RETURN HERE AFTER HALT */
  kprintf("C(%d): hot start finished %s, %u, %u\n" RESET,
          (size_t)ebbrt::Cpu::GetMine(), aid.c_str(), fid, umi_id);
  delete umsesh;
  return true;
}

bool seuss::Invoker::check_ready_instance(size_t fid) {
  auto it = stalled_instance_map_.find(fid);
  if (it != stalled_instance_map_.end()) {
    return true; 
  }
  return false; 
}

umm::umi::id seuss::Invoker::get_ready_instance(size_t fid) {
  auto it = stalled_instance_map_.find(fid);
  if (it != stalled_instance_map_.end()) {
    umm::umi::id ret = it->second;
    stalled_instance_map_.erase(fid);
    // remove instance from fifo
    auto loc = std::find(stalled_instance_fifo_.begin(), stalled_instance_fifo_.end(), fid);
    if( loc != stalled_instance_fifo_.end()){
      stalled_instance_fifo_.erase(loc);
    } 
    return ret;
  }
  ebbrt::kabort("Tried to get a non-existant instance\n");
  return umm::umi::null_id;
}

bool seuss::Invoker::instance_can_be_reused(umm::umi::id id){
  // Check if core limit has been hit
  auto c = stalled_instance_map_.size();
  if (c >= spicy_limit_) {
    return false;
  }
  // Check reuse limit on instance 
  auto it = stalled_instance_usage_count_.find(id);
  if (it != stalled_instance_usage_count_.end()) {
    if (it->second <= spicy_reuse_limit_) {
      return true;
    }
    kprintf_force(CYAN "Reuse limit reached for instance %d\n" RESET, id);
    return false;
  }
  // No record of this instance
  return true;
}

void seuss::Invoker::save_ready_instance(size_t fid, umm::umi::id id){
  auto it = stalled_instance_map_.find(fid);
  if (it != stalled_instance_map_.end()) {
    ebbrt::kabort("Tried to double-save a spicy instance\n");
    return;
  }

  // increase the usage count for this instance
  auto it2 = stalled_instance_usage_count_.find(id);
  if (it2 != stalled_instance_usage_count_.end()) {
    auto count = it2->second + 1;
    stalled_instance_usage_count_[id] = count;
  } else { // no previous entry in table
    stalled_instance_usage_count_[id] = 1;
  }

  // Save umi for reuse
  stalled_instance_map_.emplace(fid, id);
  stalled_instance_fifo_.push_back(fid);
}

void seuss::Invoker::garbage_collect_ready_instance() {
  if (stalled_instance_map_.empty())
    return;
  // Arbitrary garbage collection policy: remove the "first" instance
  auto first = stalled_instance_fifo_.front();
  stalled_instance_fifo_.pop_front();
  return clear_ready_instance(first);
}

void seuss::Invoker::clear_ready_instance(size_t fid) {
  auto id = get_ready_instance(fid);
  if(id){
    kprintf(RED "MAGMA: clearing UMI%u for F%u\n" RESET, id, fid);
    umm::manager->SignalHalt(id);
  }
  return;
}

bool seuss::Invoker::process_spicy_start(seuss::Invocation i) {

  size_t fid = i.info.function_id;
  const std::string aid  = i.info.activation_id;
  const std::string args = i.args;
  const std::string code = i.code;

  /* Get instance for this function */
  auto umi_id = get_ready_instance(fid);
  kassert(umi_id);
  kprintf_force("C(%d):[%d,%d] " RED "spicy hot" RESET" start %s, %u, %u\n",
          (size_t)ebbrt::Cpu::GetMine(), request_concurrency_.load(), stalled_instance_fifo_.size(), aid.c_str(), fid, umi_id);

  auto umi = umm::manager->GetInstance(umi_id);
  kassert(umi);

  uint16_t src_port = get_internal_port();
  umi->RegisterPort(src_port);
  umi->DisableYield();
  auto pcb = new ebbrt::NetworkManager::TcpPcb;
  auto umsesh = new InvocationSession(std::move(*pcb), i.info, src_port);

  /* Setup the operation handlers of the InvocationSession */
  umsesh->WhenConnected().Then(
      [this, umsesh, args](auto f) {
        umsesh->SendHttpRequest("/run", args, false); });

  umsesh->WhenClosed().Then([this, fid, umi_id](auto f) {
    this->request_concurrency_--;
    ebbrt::event_manager->SpawnLocal([this]() { this->Poke(); }, true);
    if (!(this->check_ready_instance(fid)) &&
        this->instance_can_be_reused(umi_id)) {
      /* No existing instance exists for this function on this core*/
      auto umi = umm::manager->GetInstance(umi_id);
      ebbrt::kbugon(!umi);
      umi->EnableYield();
      this->save_ready_instance(fid, umi_id);
      umm::manager->SignalYield(umi_id);
      return;
    }
    // Otherwise halt this instance 
    ebbrt::event_manager->SpawnLocal(
        [this, umi_id] { umm::manager->SignalHalt(umi_id); },
        /* force async */ true);
  });

  umsesh->WhenAborted().Then([this, umi_id](auto f) {
    ebbrt::event_manager->SpawnLocal(
        [this, umi_id] {
          kprintf_force(RED "MAGMA SESSION ABORTED...\n" RESET);
          umm::manager->SignalHalt(umi_id);
        },
        /* force async */ true);
  });

  /* Spawn a new event to make a connection with the instance */
  ebbrt::event_manager->SpawnLocal(
      [this, umsesh] {
        // Start a new TCP connection with the http request
        umsesh->Connect();
      },
      /* force async */ true);

  umm::manager->RequestActivation(umi_id);
  return true;
}

seuss::InvocationSession* seuss::Invoker::create_session(seuss::InvocationStats istats) {
  auto pcb = new ebbrt::NetworkManager::TcpPcb;
  auto src_port = get_internal_port();
  return new InvocationSession(std::move(*pcb), istats, src_port);
}

