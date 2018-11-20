//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <algorithm> /* std::remove */
#include <sstream> /* std::ostringstream */

#include <ebbrt/Debug.h>
#include <ebbrt/Cpu.h>
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

          // HACK(tommyu)
          // umm::manager->bootstrapping = true;
          seuss::invoker->Bootstrap();
          // umm::manager->bootstrapping = false;

          p.SetValue();
        },
        i);
    f.Block(); // sequential initialization
  }
  kprintf_force(GREEN "\nFinished initialization of all Seuss invoker cores \n" RESET);
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

  // Inform all the cores about the work
  size_t num_cpus = ebbrt::Cpu::Count();
  for (size_t i = 0; i < num_cpus; i++) {
    // XXX: spawning the work to self potentially introduces starvation of the core
    ebbrt::event_manager->SpawnRemote([this]() { ebb_->Poke(); }, i);
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


/* class seuss::Invoker */

void seuss::Invoker::Bootstrap() {
  // THIS SHOULD RUN AT MOST ONCE PER-CORE 
  kassert(!is_bootstrapped_);

  kprintf_force("Bootstrapping Invoker on core #%d nid #%d\n",
                (size_t)ebbrt::Cpu::GetMine(), ebbrt::Cpu::GetMyNode().val());

  // Port naming kludge
  base_port_ = 49160 + (size_t)ebbrt::Cpu::GetMine();
  auto sv = umm::ElfLoader::createSVFromElf(&_sv_start);
  auto umi = std::make_unique<umm::UmInstance>(sv);

  std::string opts_ =
      R"({"cmdline":"bin/node-default /nodejsActionBase/app.js",
 "net":{"if":"ukvmif0","cloner":"true","type":"inet","method":"static","addr":"169.254.1.0","mask":"16", "gw":"169.254.1.0"}})";
  kprintf(RED "CONFIG= %s", opts_.c_str());

  uint64_t argc = Solo5BootArguments(sv.GetRegionByName("usr").start,
                                     SOLO5_USR_REGION_SIZE, opts_);

  // Set the IP address
  umi->SetArguments(argc);
  // Load instance and set breakpoint for snapshot creation
  umm::manager->Load(std::move(umi));
  ebbrt::Future<umm::UmSV*> snap_f = umm::manager->SetCheckpoint(
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
  umm::manager->runSV();
  // Return once manager->Halt() is called
  umm::manager->Unload();
  is_bootstrapped_ = true;
  return;
}


bool seuss::Invoker::process_warm_start(seuss::Invocation i) {

  // kprintf(YELLOW "Processing WARM start \n" RESET);
  uint64_t tid = i.info.transaction_id;
  size_t fid = i.info.function_id;
  const std::string args = i.args;
  const std::string code = i.code;

  // TODO: this in each start instead of here?
  umsesh_ = create_session(tid, fid);

  ebbrt::Future<umm::UmSV*> hot_sv_f = umm::manager->SetCheckpoint(
      umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  // When you have the sv, cache it for future use.
  hot_sv_f.Then([this, fid](ebbrt::Future<umm::UmSV*> f) {
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
        // kprintf(YELLOW "Connected, sending init\n" RESET);
        umsesh_->SendHttpRequest("/init", code, true /* keep_alive */);
      });

  umsesh_->WhenInitialized().Then(
      [this, args](auto f) {
        // kprintf(YELLOW "Finished function init, sending run args: %s\n" RESET, args.c_str());
        umsesh_->SendHttpRequest("/run", args, false);
      });

  // Halt when closed or aborted
  umsesh_->WhenClosed().Then([this](auto f) {
    // kprintf(YELLOW "Connection Closed...\n" RESET);
    ebbrt::event_manager->SpawnLocal(
        [this] {
          umm::manager->Halt(); /* Return to back to init_code_and_snap */
        },
        /* force async */ true);
  });

  umsesh_->WhenAborted().Then([this](auto f) {
    kprintf(RED "SESSION ABORTED...\n" RESET);
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
        umsesh_->Pcb().Connect(umm::UmInstance::CoreLocalIp(), 8080, base_port_+=ebbrt::Cpu::Count());
      },
      /* force async */ true);

  /* Load up the base snapshot environment */
  // kprintf(YELLOW "Loading up base env\n" RESET);
  auto umi2 = std::make_unique<umm::UmInstance>(*base_um_env_);
  umm::manager->Load(std::move(umi2));
  /* Boot the snapshot */
  is_running_ = true;

#if WARM_PATH_PERF
  auto d = umm::manager->ctr.CreateTimeRecord(std::string("run warm"));
#endif

  umm::manager->runSV(); // blocks until umm::manager->Halt() is called
  // printf(RED "Num pg faults during warm start %lu\n" RESET, umm::manager->pg_ft_count);

#if WARM_PATH_PERF
  umm::manager->ctr.add_to_list(d);
#endif

  /* RETURN HERE AFTER HALT */
  delete umsesh_;
  umsesh_ = nullptr;
  umm::manager->Unload();
  is_running_ = false;
  kprintf(YELLOW "Finished WARM start \n" RESET);
  return true;
}

bool seuss::Invoker::process_hot_start(seuss::Invocation i) {

  // kprintf(RED "Processing HOT start \n" RESET);
  uint64_t tid = i.info.transaction_id;
  size_t fid = i.info.function_id;
  const std::string args = i.args;
  const std::string code = i.code;

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
        // Start a new TCP connection with the http request
        kprintf(RED "Hot start connect ...\n" RESET);
        umsesh_->Pcb().Connect(umm::UmInstance::CoreLocalIp(), 8080, base_port_+=ebbrt::Cpu::Count());
      },
      /* force async */ true);

  umsesh_->WhenConnected().Then(
      [this, args](auto f) {
        // kprintf(RED "Connection open, sending run...\n" RESET);
        umsesh_->SendHttpRequest("/run", args, false); });

  // Halt when closed
  umsesh_->WhenClosed().Then([this](auto f) {
    // kprintf(GREEN "Connection Closed...\n" RESET);
    ebbrt::event_manager->SpawnLocal(
        [this] {
          umm::manager->Halt(); /* Return to back to init_code_and_snap */
        },
        /* force async */ true);
  });
  umsesh_->WhenAborted().Then([this](auto f) {
    kprintf(RED "SESSION ABORTED...\n" RESET);
    ebbrt::event_manager->SpawnLocal(
        [this] {
          umm::manager->Halt(); /* Return to back to init_code_and_snap */
        },
        /* force async */ true);
  });

#if HOT_PATH_PERF
  auto b = umm::manager->ctr.CreateTimeRecord(std::string("create ins"));
#endif

  auto umi2 = std::make_unique<umm::UmInstance>(*(cache_result->second));

#if HOT_PATH_PERF
  umm::manager->ctr.add_to_list(b);
#endif


  umm::manager->Load(std::move(umi2));

  /* Boot the snapshot */
  is_running_ = true;

#if HOT_PATH_PERF
  auto d = umm::manager->ctr.CreateTimeRecord(std::string("run"));
#endif
  // umm::manager->pg_ft_count = 0;
  umm::manager->runSV(); // blocks until umm::manager->Halt() is called
  // printf(RED "Num pg faults during hot start %lu\n" RESET, umm::manager->pg_ft_count);
#if HOT_PATH_PERF
  umm::manager->ctr.add_to_list(d);
#endif
  /* After instance is halted */
  /* RETURN HERE AFTER HALT */
  delete umsesh_;
  umsesh_ = nullptr;

#if HOT_PATH_PERF
  auto e = umm::manager->ctr.CreateTimeRecord(std::string("unload"));
#endif
  umm::manager->Unload();
#if HOT_PATH_PERF
  umm::manager->ctr.add_to_list(e);
#endif
  is_running_ = false;
  kprintf(RED "Finished HOT start \n" RESET);
  return true;
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

void seuss::Invoker::Queue(seuss::Invocation i) {
  root_.AddWork(i);
  return;
}

void seuss::Invoker::Poke(){
  kassert(is_bootstrapped_);
  if (is_running_)
    return;
  Invocation i;
  if(root_.GetWork(i)){
    Invoke(i);
  }
}

bool ctr_init[] = {false, false, false};
void seuss::Invoker::Invoke(seuss::Invocation i) {
  kassert(is_bootstrapped_);
  uint64_t tid = i.info.transaction_id;
  size_t fid = i.info.function_id;
  const std::string args = i.args;
  const std::string code = i.code;

  kprintf("invoker_core_%d received invocation: (%u, %u)\n CODE: %s\n ARGS: %s\n",
          (size_t)ebbrt::Cpu::GetMine(), tid, fid, code.c_str(), args.c_str());

  /* Queue the invocation if the core if busy */
  if (is_running_) {
    root_.AddWork(i);
    return;
  }

  // We assume the core does NOT have a running UM instance
  // TODO: verify that umm::manager->Status() == empty
  kassert(!umsesh_);
  kprintf_force("invoker_core_%d starting invocation: (%u, %u)\n",
          (size_t)ebbrt::Cpu::GetMine(), tid, fid);

#if PERF
  if(! ctr_init[ebbrt::Cpu::GetMine()]){
    ctr_init[ebbrt::Cpu::GetMine()] = true;
    kprintf_force(CYAN "Init CTRS!!!\n" RESET);
    // Anything added before this should be dropped.
    umm::manager->ctr.init_ctrs();
  }
  umm::manager->ctr.reset_all();
  umm::manager->ctr.start_all();
#endif

  /* Check for a snapshot in the cache */
  auto cache_result = um_sv_map_.find(fid);
  if (cache_result == um_sv_map_.end()) {
    /* CACHE MISS */

#if WARM_PATH_PERF
    auto b = umm::manager->ctr.CreateTimeRecord(std::string("WARM TOT"));
#endif

    process_warm_start(i);

#if WARM_PATH_PERF
    umm::manager->ctr.add_to_list(b);
#endif

  }else{
    /* CACHE HIT */
#if HOT_PATH_PERF
    auto b = umm::manager->ctr.CreateTimeRecord(std::string("HOT TOT"));
#endif

    process_hot_start(i);

#if HOT_PATH_PERF
    umm::manager->ctr.add_to_list(b);
#endif

  }

  kprintf("invoker_core_%d finished invocation: (%u, %u)\n",
          (size_t)ebbrt::Cpu::GetMine(), tid, fid);

#if PERF
  umm::manager->ctr.stop_all();
  umm::manager->ctr.dump_list();
  umm::manager->ctr.clear_list();
#endif

  /* Check the shared queue for additional work to be done */
  Invocation next_i;
  if (root_.GetWork(next_i)) {
    kprintf("invoker_core_%d invoking from queue\n");
    ebbrt::event_manager->SpawnLocal([this, next_i]() { Invoke(next_i); }, true);
  }


}

void seuss::Invoker::Resolve(seuss::InvocationStats istats, std::string ret) {
  seuss_channel->SendReply(
      ebbrt::Messenger::NetworkId(ebbrt::runtime::Frontend()), istats, ret);
}

