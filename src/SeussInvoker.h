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

const uint8_t default_concurrency_limit = 12;      // 15*12=180
const uint16_t default_instance_reuse_limit = 300; // spicy hot starts

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

private:
  const std::string umi_rump_config_ =
      R"({"cmdline":"bin/node-default /nodejsActionBase/app.js", "net":{"if":"ukvmif0","cloner":"true","type":"inet","method":"static","addr":"169.254.1.0","mask":"16", "gw":"169.254.1.0"}})";
  umm::UmSV *base_um_env_;
  bool is_bootstrapped_{false}; // Have we created a base snapshot?
  ebbrt::SpinLock qlock_;
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
      : root_(const_cast<InvokerRoot &>(root)), base_port_(49160), port_(0),
        request_concurrency_(0) {
    port_range_ = ((1 << 16) - base_port_ - 1);
  };

  /* Invoke code on an uninitialized instance */
  void Invoke(Invocation i);
  /* Add request to work queue but do no work */
  void Queue(Invocation i);
  /* Initialize invoker on this core*/
  void Init();
  /* Wake core up, there's work to do! */
  void Poke();
  // TODO: Remove method and do resolution within Invoke(...)
  void Resolve(InvocationStats istats, const std::string ret_args);

private:
  bool process_wrong_start(Invocation i);
  /* Boot from the base snapshot and capture a new snapshot for this function*/
  bool process_warm_start(Invocation i);
  /* Boot from function-specific snapshot */
  bool process_hot_start(Invocation i);
  /* Connective to an active instance for this function */
  bool process_spicy_start(Invocation i);

  /* Create a new session handler */
  InvocationSession *create_session(InvocationStats i);

  /* Concurrent requests (i.e., for blocked IO )*/
  uint16_t request_concurrency_limit_;

  /* Spicy Hot settings */
  bool spicy_is_enabled() { return spicy_limit_; }
  void save_ready_instance(size_t fid, umm::umi::id id);
  void clear_ready_instance(size_t fid);
  bool instance_can_be_reused(umm::umi::id id);
  bool check_ready_instance(size_t fid);
  umm::umi::id get_ready_instance(size_t fid);
  void garbage_collect_ready_instance();
  uint16_t spicy_limit_ = 0;
  uint16_t spicy_reuse_limit_;

  InvokerRoot &root_;
  const uint16_t base_port_;
  uint16_t port_range_; // port should be between base_port_ and MAX_UINT_16
  std::atomic<std::uint16_t> port_;
  std::atomic<std::uint8_t> request_concurrency_;
  uint16_t get_internal_port() {
    port_ += ebbrt::Cpu::Count();
    size_t port_offset = (size_t)ebbrt::Cpu::GetMine();
    return base_port_ + ((port_ + port_offset) % port_range_);
  }
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
