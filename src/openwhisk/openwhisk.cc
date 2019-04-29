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
std::string openwhisk::function = "";
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

  assert(runs % fns == 0);

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

  msg::ActivationMessage am(amjson);
  am.content_ = std::string(R"({"spin":"27"})");
  auto action_cpu = ebbrt::Cpu::GetByIndex(thread::action);

  ebbrt::event_manager->Spawn(
      [am]() {
        std::string code = openwhisk::function;
        if( code.empty()){
          code = default_function;
        }

        /* FOR EACH STDIN, INVOKE THE FUNCTION N MANY TIMES */
        std::cout << "Seuss Benchmark. Input rounds, "
                     "users, worker threads, and send delay (ms) "
                     "(default: 0 1 1 0)."
                  << std::endl;
        do {
          std::cout << "<rounds, users, threads, sleep(ms)> ('q' to "
                       "quit): ";

          string benchmark_config;
          getline(cin, benchmark_config);

          if(benchmark_config == "q"){
            cout << "Exiting benchmark" << endl;
            std::exit(0);
          }

          if(benchmark_config.empty()){
            cout << "Empty input... Try again (or 'q' to quit)" << endl;
            continue;
          }

          bench b(benchmark_config);
          b.dump_bench();
          if (!b.vecGood()) {
            continue;
          }

          if(!seuss::controller->Ready()){
            cout << "No backend found. Wait a bit and try again (or 'q' to quit)" << endl;
            continue;
          }

          // auto start = std::chrono::high_resolution_clock::now();

          std::cout << "Invoking test functions " << b.runs << " time(s)..." << std::endl;

          int concurrencyPool = b.parallel;

          // Generate a vector of function names to execute.
          vector<std::string> revisionVec = genRevisionVec(b.runs, b.fns);

          assert(revisionVec.size() == (unsigned)b.runs);

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

            // Wait for someone to finish. Don't pull so hard on the cache line.
            while(concurrencyPool < 1)
              std::this_thread::sleep_for(std::chrono::milliseconds(1));

            // Sleep before sending request.
            std::this_thread::sleep_for(std::chrono::milliseconds(b.sleep));

            // Schedule activation, take from pool;
            concurrencyPool -= 1;
            auto start_time = std::chrono::system_clock::now();
            auto cmf = seuss::controller->ScheduleActivation(am_tmp, code);

            cmf.Then([i, &start_time, &concurrencyPool](auto f) {
              auto cm = f.Get();
              auto end_time = std::chrono::system_clock::now();
              auto duration = end_time - start_time;
              auto duration_milli = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
              // CSV format: <id, duratin, start time, end time, status, times, result>
              std::cout << (i) << ", ";
              std::cout << duration_milli.count() << ", ";
              std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(start_time.time_since_epoch()).count() << ", ";
              std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end_time.time_since_epoch()).count() << ", ";

                // String out double-quotes
                auto ann = cm.response_.annotations_;
                ann.erase(remove(ann.begin(), ann.end(), '\"'), ann.end());
                auto res=cm.response_.result_;
                res.erase(remove(res.begin(), res.end(), '\"'), res.end());
                std::string status="1";
              if (cm.response_.status_code_ == 0) {
                status = "0";
              }
              std::cout << status << ",\"" << ann
                        << "{key:runTime,value:" << cm.response_.duration_
                        << "}" << res << "\"" << std::endl;
              concurrencyPool += 1;
            }); // then
          }     // for loop

          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        } while (true); }, action_cpu->get_context(), true);
}
