//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "SeussController.h"
#include "SeussChannel.h"

#include <ebbrt/Debug.h>

void seuss::Init(){
  auto rep = new SeussChannel(SeussChannel::global_id);
  SeussChannel::Create(rep, SeussChannel::global_id);
}

seuss::Controller::Controller(ebbrt::EbbId ebbid){}

ebbrt::Future<openwhisk::msg::CompletionMessage>
seuss::Controller::ScheduleActivation(const openwhisk::msg::ActivationMessage &am, std::string code) {
  ebbrt::Promise<openwhisk::msg::CompletionMessage> promise;

  std::cout << "Scedualing Activation: " << am.to_json() << std::endl
            << "```" << std::endl
            << code << std::endl
            << "```" << std::endl;
#if 0
        // Create a response
        msg::CompletionMessage cm(am);
        cm.response_.duration_ = 999;
        cm.response_.start_ = 0;
        cm.response_.end_ = 0;
        cm.response_.status_code_ = 0;
#endif
  return promise.GetFuture();
}

