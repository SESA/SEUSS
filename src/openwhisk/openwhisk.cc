// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#include <thread>

#include <ebbrt/EventManager.h>
#include <ebbrt/Cpu.h>
#include "openwhisk.h"

po::options_description openwhisk::program_options() {
  po::options_description options("OpenWhisk");
  options.add(kafka::program_options());
  options.add(couchdb::program_options());
  return options;
}

bool openwhisk::process_program_options(po::variables_map &vm) {
  return (couchdb::init(vm) && kafka::init(vm));
};

void openwhisk::connect() {
  auto ping_cpu = ebbrt::Cpu::GetByIndex(thread::ping);
  ebbrt::event_manager->Spawn([]() { kafka::ping_producer_loop(); },
                              ping_cpu->get_context(), true);
  auto action_cpu = ebbrt::Cpu::GetByIndex(thread::action);
  ebbrt::event_manager->Spawn([]() { kafka::activation_consumer_loop(); },
                              action_cpu->get_context(), true);
  return;
}
