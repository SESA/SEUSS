//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef SEUSS_CONTROLLER_H
#define SEUSS_CONTROLLER_H

#if __ebbrt__
#error THIS IS LINUX-ONLY CODE
#endif

#include <unordered_map>

#include <ebbrt/IOBuf.h>
#include <ebbrt/Message.h>
#include <ebbrt/Messenger.h>
#include <ebbrt/SharedEbb.h>
#include <ebbrt/UniqueIOBuf.h>

#include "dsys/dsys.h"

#include "openwhisk/openwhisk.h"

namespace seuss {

void Init();

class Controller : public ebbrt::SharedEbb<Controller> {
public:
  static const ebbrt::EbbId global_id =
      ebbrt::GenerateStaticEbbId("Controller");
  // Constructor
  Controller(ebbrt::EbbId ebbid);

  ebbrt::Future<openwhisk::msg::CompletionMessage>
  ScheduleActivation(const openwhisk::msg::ActivationMessage &am,
                     std::string code = std::string());
  void  ResolveActivation(uint64_t tid, std::string res);

  // Node allocation functions
  void RegisterNode(ebbrt::Messenger::NetworkId nid);

private:
  typedef std::pair<ebbrt::Promise<openwhisk::msg::CompletionMessage>,
                    openwhisk::msg::ActivationMessage>
      activation_record;
  std::vector<ebbrt::Messenger::NetworkId> _nids;
  std::unordered_map<std::string, size_t>
      _frontEnd_cpus_map; // maps str(ip) to cpu index
  std::mutex m_;
  std::unordered_map<uint64_t, activation_record> record_map_;
};

constexpr auto controller = ebbrt::EbbRef<Controller>(Controller::global_id);

} // namespace seuss

#endif // SEUSS_CONTROLLER
