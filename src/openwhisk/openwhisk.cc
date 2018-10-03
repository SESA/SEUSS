// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#include <chrono>
#include <string>
#include <iostream>
#include <thread>
#include <cstdlib>

#include <cstdlib>
#include <algorithm>

#include "openwhisk.h"
#include <ebbrt/Cpu.h>
#include <ebbrt/EventManager.h>

#include "../SeussController.h" // for test()

std::string openwhisk::mode = "";
using namespace std;

po::options_description openwhisk::program_options() {
  po::options_description options("OpenWhisk configuration");
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

vector<std::string> genRevisionVec(int runs, int fns){
  // Create vec of size runs with fns many unique stings repeated runs / fns times

  kassert(runs % fns == 0);

  // Use this to generate the fns many ran names.

  // This is the final string we will return;
  vector<std::string> ret(runs);

  // fns many unique strings.
  for(int i=0; i<fns; i++){
    std::ostringstream rand_name;
    rand_name << rand();
    std::string tmp = rand_name.str();
    cout << "@ " << tmp << endl;

    // Inserted repeats many times.
    int repeats = runs / fns;
    cout << "repeats " << repeats << endl;
    for(int j=0; j<repeats; j++){
      ret[i*repeats + j] = tmp;
    }
  }
  std::random_shuffle( ret.begin(), ret.end() );
  return ret;
}

void openwhisk::test() {

  const std::string amjson =
      R"({"rootControllerIndex":{"instance":0},"activationId":"20f9fcfd0c3a4348b9fcfd0c3aa348c7","revision":"1-37bd2b1385457427f565988814b177a3","transid":["b7019b67c95d4cf924fda18c8f746e02",1532474133193],"content":{"mykey":"myval"},"blocking":false,"action":{"path":"user_9801","name":"29523","version":"0.0.1"},"user":{"subject":"user_9801","authkey":"e4f86424-008e-45a5-afc2-2cf493e401bd:qA3v5mW8g9k1I7cRBsbDECK81y0WFoUdhOKpc57cbnozAkAVkhHmY0hVIMGc6aCu","rights":["READ","PUT","DELETE","ACTIVATE"],"limits":{},"namespace":"user_9801"}})";

  msg::ActivationMessage am(amjson);

  am.content_ = std::string(R"({"spin":"27"})");
  std::cout << "Start activation test" << std::endl;
  auto action_cpu = ebbrt::Cpu::GetByIndex(thread::action);

  ebbrt::event_manager->Spawn(
      [am]() {
        const std::string code = R"(function main(args) { var spin=0; var count = 0; if(args.spin) spin=args.spin; var max = 1<<spin; for (var line=1; line<max; line++) { count++; } return {done:true, c:count}; })";

        /* FOR EACH STDIN, INVOKE THE FUNCTION N MANY TIMES */
        do {

          std::cout << "Rounds fns ||ism sleep(ms)" << std::endl;

          string benchmark_config;
          getline(cin, benchmark_config);

          if(benchmark_config.empty()){
            cout << "try again" << endl;
            continue;
          }

          bench b(benchmark_config);
          b.dump_bench();

          if(!b.vecGood()){
            cout << "try again" << endl;
            continue;
          }

          if (!b.runs){
            cout << "Zero runs, doing nothing" << endl;
            continue;
          }
          // auto start = std::chrono::high_resolution_clock::now();

          std::cout << "Invoking test functions " << b.runs << " time(s)..." << std::endl;

          int concurrencyPool = b.parallel;

          // Generate a vector of function names to execute.
          vector<std::string> revisionVec = genRevisionVec(b.runs, b.fns);

          kassert(revisionVec.size() == (unsigned)b.runs);

          int i = 0;
          for(const std::string& re : revisionVec){
            i++;


            std::ostringstream rand_name;
            rand_name << rand();
            auto am_tmp = am;

            // OpenWhisk transaction id (unique)
            am_tmp.transid_.name_ = rand_name.str();

            // Assign function name;
            am_tmp.revision_ = re;
            cout << "running function " << re << endl;


            // Wait for someone to finish. Don't pull so hard on the cache line.
            while(concurrencyPool < 1)
              std::this_thread::sleep_for(std::chrono::milliseconds(1));

            // Sleep before sending request.
            std::this_thread::sleep_for(std::chrono::milliseconds(b.sleep));

            // Schedule activation, take from pool;
            concurrencyPool -= 1;
            auto cmf = seuss::controller->ScheduleActivation(am_tmp, code);

            cmf.Then([i, &concurrencyPool](auto f) {
              auto cm = f.Get();
              if (cm.response_.status_code_ == 0) {
                std::cout << "#" << (i) << " SUCCESS "
                          << cm.response_.annotations_
                          << ", duration: " << cm.response_.duration_
                          << std::endl;
              } else {
                std::cout << "#" << (i) << " FAILED " << std::endl;
              }
              concurrencyPool += 1;
              }); // then
          } // for loop

          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        } while (true); }, action_cpu->get_context(), true);
}
