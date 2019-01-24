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
#include <ebbrt/UniqueIOBuf.h>
#include <ebbrt/Debug.h>
#include <ebbrt/Future.h>
#include <ebbrt/MulticoreEbb.h>
#include <ebbrt/native/Net.h>
#include <ebbrt/SpinLock.h>

#include "umm/src/Umm.h"

#include "Seuss.h"
#include "InvocationSession.h"

namespace seuss {

const uint8_t request_concurrency_limit = 12; // 15*12=180

void Init();

class Invoker;

/*  suess::InvokerRoot
 *  Shared ebb responsible for the Invokers work queue and segregating IO processing
 */
class InvokerRoot {
public:
  InvokerRoot(){}
  void Bootstrap();
  size_t AddWork(Invocation i);
  bool GetWork(Invocation& i);
  ebbrt::EbbRef<Invoker> ebb_;
  umm::UmSV* GetBaseSV();
  umm::UmSV* GetSnapshot(size_t id);
  bool SetSnapshot(size_t id, umm::UmSV*);
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
  std::unordered_map<size_t, umm::UmSV*> snapmap_;
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
      : root_(const_cast<InvokerRoot &>(root)), base_port_(49160),
        port_(0),
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
  // TODO(jmc): Avoid copies, make these internal functions an Invocation& 
  bool process_warm_start(Invocation i);
  bool process_hot_start(Invocation i);
  InvocationSession* create_session(uint64_t tid, size_t fid);
  InvokerRoot& root_;
  bool is_running_{false};      // Have we booted the snapshot
  const uint16_t base_port_;
  uint16_t port_range_; // port should be between base_port_ and MAX_UINT_16
  std::atomic<std::uint16_t> port_;
  std::atomic<std::uint8_t> request_concurrency_;
  uint16_t get_internal_port() {
    port_ += ebbrt::Cpu::Count();
    size_t port_offset = (size_t)ebbrt::Cpu::GetMine();
    return  base_port_ + ((port_ + port_offset) % port_range_);
  }
  // Arg code pair
  typedef std::tuple<size_t, std::string, std::string> invocation_request;
  // map tid to (arg, code) pairs.
  std::unordered_map<uint64_t, invocation_request> request_map_;
  // Queue requests by tid
  std::queue<uint64_t> request_queue_;
};

constexpr auto invoker = ebbrt::EbbRef<Invoker>(Invoker::global_id);

} // namespace seuss

#endif // SEUSS_INVOKER
