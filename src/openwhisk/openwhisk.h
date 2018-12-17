// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef SEUSS_OPENWHISK_OPENWHISK_H_
#define SEUSS_OPENWHISK_OPENWHISK_H_

#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include <ebbrt/Future.h>

#include "cppkafka/configuration.h"
#include "msg.h"

#include <iostream>
#include <sstream>
#include <string>

using namespace std;

namespace openwhisk {

// arguments configure via the command line
extern std::string mode;
extern std::string function;

// a 'dummy' OpenWhisk activation (benchmark)
const std::string amjson =
    R"({"rootControllerIndex":{"instance":0},"activationId":"20f9fcfd0c3a4348b9fcfd0c3aa348c7","revision":"1-37bd2b1385457427f565988814b177a3","transid":["b7019b67c95d4cf924fda18c8f746e02",1532474133193],"content":{"mykey":"myval"},"blocking":false,"action":{"path":"user_9801","name":"29523","version":"0.0.1"},"user":{"subject":"user_9801","authkey":"e4f86424-008e-45a5-afc2-2cf493e401bd:qA3v5mW8g9k1I7cRBsbDECK81y0WFoUdhOKpc57cbnozAkAVkhHmY0hVIMGc6aCu","rights":["READ","PUT","DELETE","ACTIVATE"],"limits":{},"namespace":"user_9801"}})";
// default function to invoke (benchmark)
const std::string default_function =
    R"(function main(args) { var spin=0; var count = 0; if(args.spin) spin=args.spin; var max = 1<<spin; for (var line=1; line<max; line++) { count++; } return {done:true, c:count}; })";

// OpenWhisk integration settings 
constexpr size_t thread_count = 2;
enum thread : size_t { ping = 1, action = 2 };
constexpr size_t ping_freq_ms = 1000;

/* Kafka options & setup */
namespace kafka {
bool init(po::variables_map &vm);
po::options_description program_options();
void ping_producer_loop();
void activation_consumer_loop();
} // end namespace kafka

/* CouchDB options & setup */
namespace couchdb {
  bool init(po::variables_map &vm);
  po::options_description program_options();
  std::string get_action(msg::Action action);
} // end namespace couchdb

/* Openwhisk options & setup */
po::options_description program_options();
bool process_program_options(po::variables_map &vm);
void connect();

void test();

class bench {
  vector<int> str_to_int_vec(string str, char delimiter) {
    // https://www.quora.com/How-do-I-split-a-string-by-space-into-an-array-in-c++
    vector<int> internal;
    stringstream ss(str); // Turn the string into a stream.
    string tok;
    while (getline(ss, tok, delimiter)) {
      internal.push_back(stoi(tok));
    }
    return internal;
  }

public:
  int runs, fns, parallel, sleep;
  vector<int> ints;
  unsigned expectedSize = 4; // num toggles

  bench(string str) : runs(1), fns(1), parallel(1), sleep(0) {
    // Assume input str "runs users fns parallel" as decimal ints.
    ints = str_to_int_vec(str, ' ');
    if (ints.size() > 0)
      runs = ints[0];
    if (ints.size() > 1)
      fns = ints[1];
    if (ints.size() > 2)
      parallel = ints[2];
    if (ints.size() > 3)
      sleep = ints[3];
  }

  bool vecGood() {
    if (ints.size() > expectedSize) {
      cout << "error: too many inputs...";
      return false;
    }
    if (!runs) {
      return false;
    }
    return true;
  }

  void dump_bench() {
    cout << "runs: " << runs << "; fns: " << fns << "; parallel: " << parallel
         << "; sleep: " << sleep << ";" << endl;
  }
};

} // end namespace openwhisk
#endif // SEUSS_OPENWHISK_OPENWHISK_H_
