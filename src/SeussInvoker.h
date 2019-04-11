//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef SEUSS_INVOKER_H
#define SEUSS_INVOKER_H

#ifndef __ebbrt__
#error THIS IS EBBRT-NATIVE CODE
#endif

#include <ebbrt/Clock.h>
#include <ebbrt/Debug.h>
#include <ebbrt/Future.h>
#include <ebbrt/MulticoreEbb.h>
#include <ebbrt/SpinLock.h>
#include <ebbrt/UniqueIOBuf.h>
#include <ebbrt/native/Multiboot.h>
#include <ebbrt/native/Net.h>

#include "umm/src/Umm.h"

#include "InvocationSession.h"
#include "Seuss.h"

namespace seuss {

// cores * limit = total concurrent requests 
const uint8_t default_concurrency_limit = 1; 
const uint16_t default_instance_reuse_limit = 300; // hot start reuse 
const uint32_t default_snapmap_limit = 32768; // snapshot cache size

void Init();

class Invoker;

/*  suess::InvokerRoot
 *  Shared ebb responsible for the Invokers work queue and segregating IO
 * processing
 */
class InvokerRoot {
public:
  InvokerRoot() {}
  void Bootstrap();
  size_t AddWork(Invocation i);
  bool GetWork(Invocation &i);
  ebbrt::EbbRef<Invoker> ebb_;
  umm::UmSV *GetBaseSV();
  umm::UmSV *GetSnapshot(size_t id);
  bool SetSnapshot(size_t id, umm::UmSV *);
  bool CacheIsFull() { return snapmap_.size() >= default_snapmap_limit; }

private:
  const std::string umi_rump_config_ =
      R"({"cmdline":"bin/node-default /nodejsActionBase/app.js", "net":{"if":"ukvmif0","cloner":"true","type":"inet","method":"static","addr":"169.254.1.0","mask":"16", "gw":"169.254.1.0"}})";
  umm::UmSV *base_um_env_;
  umm::UmSV *preinit_env_;
  bool is_bootstrapped_{false}; // Have we created a base snapshot?
  ebbrt::SpinLock qlock_;
  ebbrt::SpinLock snaplock_;
  // map tid to Invocation{} .
  std::unordered_map<uint64_t, Invocation> request_map_;
  // Queue requests by tid
  std::queue<uint64_t> request_queue_;
  // Shared snapshot cache
  std::unordered_map<size_t, umm::UmSV *> snapmap_;
  friend class Invoker;
}; // end class InvokerRoot

/*  suess::Invoker
 *  Per-core ebb responsible for initializing the
 *  UM instances, executing the function code and, eventually, caching and
 *  redeploying instance snapshots.
 */
class Invoker : public ebbrt::MulticoreEbb<Invoker, InvokerRoot> {
public:
  static const ebbrt::EbbId global_id = ebbrt::GenerateStaticEbbId("Invoker");
  explicit Invoker(const InvokerRoot &root)
      : root_(const_cast<InvokerRoot &>(root)), core_(ebbrt::Cpu::GetMine()),
        base_port_(49160), port_(0), request_concurrency_(0) {
    port_range_ = ((1 << 16) - base_port_ - 1);
  };

  /* Start a new Invocation */
  void Invoke(Invocation i);

  /* Resolve a pending Invocation*/
  void Resolve(InvocationStats istats, const std::string ret_args);

  /* Add invocation request to work queue (but do no work) */
  void Queue(Invocation i);

  /* Initialize invoker on this core*/
  void Init();

  /* Wake up, there's work to do! */
  void Poke();

private:
  /* Boot from the base snapshot and capture a new snapshot for this function*/
  bool process_cold_start(Invocation i);
  /* Boot from function-specific snapshot */
  bool process_warm_start(Invocation i);
  /* Connective to an active instance for this function */
  bool process_hot_start(Invocation i);
  
  /* Returns a new session handler with the callbacks set */
  InvocationSession *new_invocation_session(seuss::InvocationStats *istats,
                               const size_t fid,
                               const umm::umi::id umi_id,
                               const std::string args,
                               const std::string code = std::string());
  /* Concurrency management (i.e., instances blocked on IO )*/
  uint16_t request_concurrency_limit_ = default_concurrency_limit;
  /* Hot start management  */
  bool hot_instances_are_enabled() { return hot_instance_limit_; }
  /* Return true if instance was saved */
  bool save_hot_instance(size_t fid, umm::umi::id id);
  bool hot_instance_exists(size_t fid);
  bool hot_instance_can_be_saved(umm::umi::id id);
  bool hot_instance_can_be_reused(umm::umi::id id);
  umm::umi::id get_hot_instance(size_t fid);
  //TODO:(jmcadden): rename spicy -> hot
  uint16_t hot_instance_limit_ = 0;
  uint16_t hot_instance_reuse_limit_ = default_instance_reuse_limit;
  

  InvokerRoot &root_;
  size_t core_; 
  const uint16_t base_port_;
  uint16_t port_range_; // port should be between base_port_ and MAX_UINT_16
  std::atomic<std::uint16_t> port_;
  uint16_t get_internal_port() {
    port_ += ebbrt::Cpu::Count();
    size_t port_offset = (size_t)ebbrt::Cpu::GetMine();
    return base_port_ + ((port_ + port_offset) % port_range_);
  }
  /* Counters */
  uint64_t invctr_ = 0;
  std::atomic<std::uint8_t> request_concurrency_;
  // Arg code pair
  typedef std::tuple<size_t, std::string, std::string> invocation_request;
  // map tid to (arg, code) pairs.
  std::unordered_map<uint64_t, invocation_request> request_map_;
  // Queue requests by tid
  std::queue<uint64_t> request_queue_;
  // Hot & Spicy starts
  std::unordered_map<size_t, umm::umi::id> stalled_instance_map_;
  std::deque<size_t> stalled_instance_fifo_;
  std::unordered_map<umm::umi::id, uint16_t> stalled_instance_usage_count_;
};

constexpr auto invoker = ebbrt::EbbRef<Invoker>(Invoker::global_id);

} // namespace seuss

#endif // SEUSS_INVOKER
