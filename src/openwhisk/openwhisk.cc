// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#include <chrono>
#include <iostream>
#include <thread>
#include <cstdlib>

#include "openwhisk.h"
#include <ebbrt/Cpu.h>
#include <ebbrt/EventManager.h>

#include "../SeussController.h" // for test()

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

using namespace std::chrono_literals;

void openwhisk::test() {

  const std::string amjson =
      R"({"rootControllerIndex":{"instance":0},"activationId":"20f9fcfd0c3a4348b9fcfd0c3aa348c7","revision":"1-37bd2b1385457427f565988814b177a3","transid":["b7019b67c95d4cf924fda18c8f746e02",1532474133193],"content":{"mykey":"myval"},"blocking":false,"action":{"path":"user_9801","name":"29523","version":"0.0.1"},"user":{"subject":"user_9801","authkey":"e4f86424-008e-45a5-afc2-2cf493e401bd:qA3v5mW8g9k1I7cRBsbDECK81y0WFoUdhOKpc57cbnozAkAVkhHmY0hVIMGc6aCu","rights":["READ","PUT","DELETE","ACTIVATE"],"limits":{},"namespace":"user_9801"}})";

  msg::ActivationMessage am(amjson);

  std::cout << "Start activation test" << std::endl;
  auto action_cpu = ebbrt::Cpu::GetByIndex(thread::action);

  ebbrt::event_manager->Spawn(
      [am]() {
        uint16_t args;
        const std::string code =
            R"(function main(args) { return {done:true, arg:args.mykey}; })";
        /* FOR EACH STDIN, INVOKE THE FUNCTION N MANY TIMES */
        while (std::cin >> args) {
          if(!args)
             continue;
          std::cout << "Invoking test function " << args << " time(s)..." << std::endl;
          for(uint16_t i=0; i<args; i++){
            std::cout << "INV #" << (i+1) << " of " << args << std::endl;
            auto am_tmp = am;
            am_tmp.transid_.id_ = rand(); // OpenWhisk transaction id (unique)
            auto cmf = seuss::controller->ScheduleActivation(am_tmp, code);
            cmf.Then([i](auto f) {
              auto cm = f.Get();
              std::cout << "INV #" << (i+1) << " returned successfully. " //<< cm.to_json()
                        << std::endl;
            });
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
      },
      action_cpu->get_context(), true);
}
