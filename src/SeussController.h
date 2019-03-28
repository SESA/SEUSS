//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef SEUSS_CONTROLLER_H
#define SEUSS_CONTROLLER_H

#if __ebbrt__
#error THIS IS LINUX-ONLY CODE
#endif

#include <chrono>
#include <tuple>
#include <unordered_map>

#include <ebbrt/IOBuf.h>
#include <ebbrt/Message.h>
#include <ebbrt/Messenger.h>
#include <ebbrt/SharedEbb.h>
#include <ebbrt/UniqueIOBuf.h>

#include "dsys/dsys.h"

#include "openwhisk/openwhisk.h"

#include "Seuss.h"

namespace seuss {

void Init();

class Controller : public ebbrt::SharedEbb<Controller> {
public:
  static const ebbrt::EbbId global_id =
      ebbrt::GenerateStaticEbbId("Controller");
  // Constructor
  Controller(ebbrt::EbbId ebbid);

  // Return true if at least one backend is connected
  bool Ready();

  /* The starting point of a seuss activation 
  *  The waitTime of an activation begins when this method is called
  */
  ebbrt::Future<openwhisk::msg::CompletionMessage>
  ScheduleActivation(const openwhisk::msg::ActivationMessage &am,
                     std::string code = std::string());
  
/* The final stage of a suess activation
  *  The activation result is passed back to OpenWhisk 
  */
  void ResolveActivation(InvocationStats istats, std::string res);

  // Register an Invocation node
  void RegisterNode(ebbrt::Messenger::NetworkId nid);

private:
  typedef std::tuple<ebbrt::Promise<openwhisk::msg::CompletionMessage>,
                    openwhisk::msg::ActivationMessage,std::chrono::high_resolution_clock::time_point>
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
