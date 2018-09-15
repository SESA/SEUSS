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
#include <ebbrt/native/Net.h>

#include "umm/src/Umm.h"

#include "Seuss.h"
#include "InvocationSession.h"

namespace seuss {

void Init();

/*  suess::Invoker 
 *  Per-core ebb responsible for initializing the
 *  UM instances, executing the function code and, eventually, caching and
 *  redeploying instance snapshots.
 */
class Invoker : public ebbrt::MulticoreEbb<Invoker> {
public:
  static const ebbrt::EbbId global_id = ebbrt::GenerateStaticEbbId("Invoker");
  Invoker(){};
  /* Bootstrap the instance on this core */
  void Bootstrap();
  /* Invoke code on an uninitialized instance */
  void Invoke(uint64_t tid, size_t fid, const std::string args,
              const std::string code);
  // Resolve invocation request
  void Resolve(InvocationStats istats, const std::string ret_args);

private:
  void deploy_queued_request();
  bool process_warm_start(size_t fid, uint64_t tid, std::string code, std::string args);
  bool process_hot_start(size_t fid, uint64_t tid, std::string args);
  void queue_invocation(uint64_t tid, size_t fid, const std::string args,
                                       const std::string code);
  InvocationSession* create_session(uint64_t tid, size_t fid);
  bool is_bootstrapped_{false}; // Have we created a base snapshot
  bool is_running_{false};      // Have we booted the snapshot

  // Session specific state 
  size_t fid_{0};
  InvocationSession *umsesh_{nullptr};
  uint16_t base_port_;

  // Arg code pair
  typedef std::tuple<size_t, std::string, std::string> invocation_request;
  // map tid to (arg, code) pairs.
  std::unordered_map<uint64_t, invocation_request> request_map_;
  // Queue requests by tid
  std::queue<uint64_t> request_queue_;
  umm::UmSV base_um_env_;
  // Lookup sv by fid.
  std::unordered_map<size_t, umm::UmSV> um_sv_map_;
  const std::string umi_config_ = R"({"cmdline":"bin/node-default /nodejsActionBase/app.js",
 "net":{"if":"ukvmif0","cloner":"true","type":"inet","method":"static","addr":"169.254.1.1","mask":"16"}})";

};

constexpr auto invoker = ebbrt::EbbRef<Invoker>(Invoker::global_id);

} // namespace seuss

#endif // SEUSS_INVOKER
