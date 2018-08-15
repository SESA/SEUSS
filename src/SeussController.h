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
#include <ebbrt/Messenger.h>
#include <ebbrt/Message.h>
#include <ebbrt/SharedEbb.h>
#include <ebbrt/UniqueIOBuf.h>

#include "dsys/dsys.h"

#include "openwhisk/msg.h"

namespace seuss {

static Init();

class Controller : public ebbrt::SharedEbb<Controller> {
public:
  static const ebbrt::EbbId global_id =
      ebbrt::GenerateStaticEbbId("Controller");
  // Constructor
  Controller(ebbrt::EbbId ebbid);

  ebbrt::Future<openwhisk::msg::CompletionMessage>
  ScheduleActivation(const openwhisk::msg::ActivationMessage &am,
                     std::string code);

private:
  std::mutex m_;
  std::unordered_map<uint32_t, ebbrt::Promise<void>> promise_map_;
  uint32_t id_{0};
};

constexpr auto controller =
    ebbrt::EbbRef<Controller>(Controller::global_id);

} // namespace seuss

#endif // SEUSS_CONTROLLER
