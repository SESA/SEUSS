//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <algorithm> /* std::remove */
#include <sstream> /* std::ostringstream */

#include <ebbrt/Debug.h>
#include <ebbrt/Cpu.h>
#include <ebbrt/EventManager.h>
#include <ebbrt/Future.h>
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
  kprintf_force(GREEN "\nFinished initialization of Seuss Invoker(#)\n" RESET);
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
    if (loc != std::string::npos) {
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
    if (loc != std::string::npos) {
      auto clim_str = cl.substr((loc + zkstr.size()));
      auto gap = clim_str.find(";");
      if (gap != std::string::npos) {
        clim_str = clim_str.substr(0, gap);
      }
      hot_instance_limit_ = atoi(clim_str.c_str());
      // check for reuse
      zkstr = std::string("Rlim=");
      loc = cl.find(zkstr);
      if (loc != std::string::npos) {
        auto rlim_str = cl.substr((loc + zkstr.size()));
        auto gap = rlim_str.find(";");
        if (gap != std::string::npos) {
          rlim_str = rlim_str.substr(0, gap);
        }
        hot_instance_reuse_limit_ = atoi(rlim_str.c_str());
      }
    }
  }
  kprintf("invoker_core_%d is online\n", core_);
  if ((size_t)ebbrt::Cpu::GetMine() == 0) {
    kprintf_force("invoker_core instance concurrency limit: %d\n",
                  request_concurrency_limit_);
    kprintf_force(
        "invoker_core instance reuse limits: %d idle / %d reuses\n",
        hot_instance_limit_, hot_instance_reuse_limit_);
  }
}

void seuss::Invoker::Queue(seuss::Invocation i) {
  root_.AddWork(i);
  return;
}

// Poke() is the stupid/arbitrary way we schedule functions deployments
void seuss::Invoker::Poke(){
  Invocation i;
  // Proceed only if we have capacity on this core to do so
  if (request_concurrency_ >= request_concurrency_limit_) {
    return;
  }
  if (root_.GetWork(i)) {
    Invoke(i);
  }
}

void seuss::Invoker::Invoke(seuss::Invocation i) {

  ++request_concurrency_;
  ++invctr_;

  if (process_hot_start(i)) {
    //break;
  } else if (process_warm_start(i)) {
    //break;
  } else if (process_cold_start(i)) {
    //break;
  }else{
    // All attempts failed :(
    kprintf_force(RED "ERROR: Unable to process invocation\n" RESET);
    ebbrt::kabort();
  }

  --request_concurrency_;
  ebbrt::event_manager->SpawnLocal([]() { seuss::invoker->Poke(); }, true);
}

void seuss::Invoker::Resolve(seuss::InvocationStats istats, std::string ret) {
  // Pass along to output channel
  seuss_channel->SendReply(
      ebbrt::Messenger::NetworkId(ebbrt::runtime::Frontend()), istats, ret);
}

bool seuss::Invoker::process_cold_start(seuss::Invocation i) {

  auto istats = i.info; // Invocation Statistics 
  const std::string args = i.args;
  const std::string code = i.code;
  const size_t fid = istats.function_id;
  ebbrt::clock::Wall::time_point operation_start_time;

  /* Load up the base snapshot environment */
  auto base_env = root_.GetBaseSV();

  /* Create new UM instance for this invocation */
  auto umi = std::make_unique<umm::UmInstance>(*base_env);
  auto umi_id = umi->Id();
  kprintf_force("C(%d)[%d]" CYAN "cold start" RESET ": %s, %u, %u\n",
                core_, invctr_, istats.activation_id, fid,
                umi_id);

  /* Snapshotting */
  ebbrt::Future<umm::UmSV*> hot_sv_f = umi->SetCheckpoint(
      umm::ElfLoader::GetSymbolAddress("uv_uptime"));
  // When you have the sv, cache it.
  hot_sv_f.Then([this, fid](ebbrt::Future<umm::UmSV *> f) {
    root_.SetSnapshot(fid, std::move(f.Get()));
  });

  /* Make a new TCP connection with the instance */
  InvocationSession *umsesh =
      new_invocation_session(&istats, fid, umi_id, args, code);
  umi->RegisterPort(umsesh->SrcPort());

  /* Block flow control until the instance is loaded */
  umm::manager->Load(std::move(umi)).Block(); 

  /* Start run of the instance */
  ebbrt::event_manager->SpawnLocal([umi_id]() { umm::manager->Start(umi_id); });

  /* Spawn a new event to make a connection with the instance */
  ebbrt::event_manager->SpawnLocal(
      [this, umsesh] {
        // Start a new TCP connection with the http request
        umsesh->Connect();
      },
      /* async */ true);

  // Block flow control until session has finished 
  umsesh->WhenFinished().Block();
  auto status = umsesh->WhenFinished().Get();
  if (status)
    kprintf("C(%d) " CYAN "cold finish" RESET ": %s, %u, %u\n",
            (size_t)ebbrt::Cpu::GetMine(), istats.activation_id, fid, umi_id);
  delete umsesh;
  return status;
}

bool seuss::Invoker::process_warm_start(seuss::Invocation i) {

  auto istats = i.info; // Invocation Statistics 
  const std::string args = i.args;
  const size_t fid = istats.function_id;

  /* Check snapshot cache for function-specific snapshot */
  auto cached_snap = root_.GetSnapshot(fid);
  if (cached_snap == nullptr) {
    return false;
  }

  /* Create new UM instance for this invocation */
  auto umi = std::make_unique<umm::UmInstance>(*cached_snap);
  auto umi_id = umi->Id();
  kprintf_force("C(%d)[%d] " YELLOW "warm start" RESET ": %s, %u, %u\n",
                core_, invctr_, istats.activation_id, fid,
                umi_id);

  /* Make a new invocation session with the instance */
  InvocationSession *umsesh =
      new_invocation_session(&istats, fid, umi_id, args /*no code*/);
  umi->RegisterPort(umsesh->SrcPort());

  /* Block flow control until the instance is loaded */
  umm::manager->Load(std::move(umi)).Block(); 

  /* Start run of the instance */
  ebbrt::event_manager->SpawnLocal([umi_id]() { umm::manager->Start(umi_id); });

  /* Start new event to make a connection with the instance */
  ebbrt::event_manager->SpawnLocal(
      [this, umsesh] {
        // Start a new TCP connection with the http request
        umsesh->Connect();
      },
      /* async */ true);

  // Block flow control until session has finished 
  umsesh->WhenFinished().Block();
  auto status = umsesh->WhenFinished().Get();
  if (status)
    kprintf("C(%d) " YELLOW "warm finish" RESET ": %s, %u, %u\n",
            core_, istats.activation_id, fid, umi_id);
  delete umsesh;
  return status;
}

bool seuss::Invoker::hot_instance_exists(size_t fid) {
  auto it = stalled_instance_map_.find(fid);
  if (it != stalled_instance_map_.end()) {
    return true; 
  }
  return false; 
}

umm::umi::id seuss::Invoker::get_hot_instance(size_t fid) {
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

bool seuss::Invoker::hot_instance_can_be_reused(umm::umi::id id) {
  kassert(id);
  // Check hot instance limit on this core has been hit
  auto c = stalled_instance_map_.size();
  if (c >= hot_instance_limit_) {
    return false;
  }
  // Check reuse limit on instance 
  auto it = stalled_instance_usage_count_.find(id);
  if (it != stalled_instance_usage_count_.end()) {
    if (it->second <= hot_instance_reuse_limit_) {
      return true;
    }
    kprintf_force(CYAN "Reuse limit reached for instance %d\n" RESET, id);
    return false;
  }
  // No record of this instance
  return true;
}

bool seuss::Invoker::save_hot_instance(size_t fid, umm::umi::id umi_id){
  kassert(fid);
  kassert(umi_id);

  if(!hot_instance_can_be_reused(umi_id)){
    return false; // this instance hit its reuse limit
  }

  if(hot_instance_exists(fid)){
    return false; // an instance for this function already exists
  }

  auto it = stalled_instance_map_.find(fid);
  if (it != stalled_instance_map_.end()) {
    //Tried to double-save a spicy instance
    return false;
  }

  // Increase the use counter 
  auto it2 = stalled_instance_usage_count_.find(umi_id);
  if (it2 != stalled_instance_usage_count_.end()) {
    auto count = it2->second + 1;
    stalled_instance_usage_count_[umi_id] = count;
  } else { // no entry found, add one
    stalled_instance_usage_count_[umi_id] = 1;
  }

  // Signal instance to be yielded 
  auto umi = umm::manager->GetInstance(umi_id);
  ebbrt::kbugon(!umi);
  umi->SetInactive();
  umm::manager->SignalYield(umi_id); // ugly that this is 2 steps

  // Register UMI for future hot starts
  stalled_instance_map_.emplace(fid, umi_id);
  stalled_instance_fifo_.push_back(fid);
  return true;
}

bool seuss::Invoker::process_hot_start(seuss::Invocation i) {

  auto istats = i.info; // Invocation Statistics 
  const std::string args = i.args;
  const size_t fid = istats.function_id;

  if (!hot_instances_are_enabled()) {
    return false;
  }

  if (!hot_instance_exists(fid)) {
    return false;
  }

  /* Get UM instance for this function */
  auto umi_id = get_hot_instance(fid);
  auto umi = umm::manager->GetInstance(umi_id);
  if (!umi_id || !umi) {
    kprintf(YELLOW "WARNING: Hot instance thought to exist but was not found: "
                   "fid=%u umi_id=%u \n" RESET,
            fid, umi_id);
    return false;
  }
  kprintf_force("C(%d):%d[%d,%d] " RED "hot start" RESET " %s, %u, %u\n",
                core_, invctr_, request_concurrency_.load(),
                stalled_instance_fifo_.size(), istats.activation_id,
                fid, umi_id);

  /* Make a new invocation session with the instance */
  InvocationSession *umsesh =
      new_invocation_session(&istats, fid, umi_id, args /*no code*/);

  // Start connection in a separate event
  ebbrt::event_manager->SpawnLocal([umsesh] { umsesh->Connect(); }, true);

  /* Prep UMI and signal it to be schedule */
  umi->pfc.zero_ctrs();
  umi->RegisterPort(umsesh->SrcPort());
  umi->SetActive();
  umm::manager->SignalResume(umi_id);

  // Block flow control until session has closed/aborted
  umsesh->WhenFinished().Block();
  auto status = umsesh->WhenFinished().Get();
  if(status)
    kprintf("C(%d):[%d,%d] " RED "hot finish" RESET " %s, %u, %u\n",
            core_, request_concurrency_.load(),
            stalled_instance_fifo_.size(), istats.activation_id, fid, umi_id);
  delete umsesh;
  return status;
}

seuss::InvocationSession *
seuss::Invoker::new_invocation_session(seuss::InvocationStats *istats,
                               const size_t fid,
                               const umm::umi::id umi_id,
                               const std::string args,
                               const std::string code ) {

  auto pcb = new ebbrt::NetworkManager::TcpPcb;
  auto umsesh = new InvocationSession(std::move(*pcb), get_internal_port());

  umsesh->WhenConnected().Then([umsesh, args, code](auto f) {
    if (code != std::string()) {
      /* If we have code, initialize it and keep the connection alive */
      umsesh->SendHttpRequest("/init", code, true /* keep_alive */);
    } else {
      /* If not given code, start execution and signal the receiver to close the
       * connection after done */
      umsesh->SendHttpRequest("/run", args, false /* close connection */);
    }
  });

  /* When initialized send the run request */
  umsesh->WhenInitialized().Then([umsesh, istats, args](auto f) {
    // Record initialization time, send run operation
    istats->exec.init_time = umsesh->get_inittime();
    umsesh->SendHttpRequest("/run", args, false /* keep_alive */);
  });

  /* Resolved invocation after successful execution successfully */
  umsesh->WhenExecuted().Then([umsesh, istats](auto f) {
    istats->exec.status = 0; /* SUCCESSFUL */
    istats->exec.run_time = umsesh->get_runtime();
    // Alternatively, we could wait for the connection to close and do it then
    seuss::invoker->Resolve(*istats, umsesh->GetReply());
  });

  /* Finalize this invocation when connection has closed */
  umsesh->WhenClosed().Then([this, umsesh, fid, umi_id](auto f) {
    //FIXME: Close doesn't necessarily mean 'success'
    umsesh->Finish(true);
    // Try and save this instance for future hot starts
    if (! this->save_hot_instance(fid, umi_id)) {
      // Unable to save, so we kill the instance
      ebbrt::event_manager->SpawnLocal(
          [umi_id] { umm::manager->SignalHalt(umi_id); },
          /* async */ true);
    }
  });

  /* Something went wrong. Kill the instance */
  umsesh->WhenAborted().Then([this, umsesh, umi_id](auto f) {
    umsesh->Finish(false);
    ebbrt::event_manager->SpawnLocal(
        [this, umi_id] { umm::manager->SignalHalt(umi_id); },
        /* async */ true);
  });

  return umsesh;
}

