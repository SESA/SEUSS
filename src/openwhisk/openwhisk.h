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

#include <string>
#include <iostream>
#include <sstream>

using namespace std;

namespace openwhisk {

extern std::string mode; 

constexpr size_t thread_count = 2;

enum thread : size_t {
  ping = 1, 
  action = 2 
};

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

    bench(string str) : runs(1), fns(1), parallel(1), sleep(0){
      // Assume input str "runs users fns parallel" as decimal ints.
      ints = str_to_int_vec(str, ' ');

      runs     = ints[0];
      fns      = ints[1];
      parallel = ints[2];
      sleep    = ints[3];
    }

    bool vecGood(){
      if (ints.size() != expectedSize) {
        cout << "Wrong size string" << endl;
        return false;
      }
      return true;
    }

    void dump_bench() {
      cout << "runs: " << runs
           << "; fns: " << fns
           << "; parallel: " << parallel
           << "; sleep: " << sleep
           << ";" << endl;
    }
  };

} // end namespace openwhisk
#endif // SEUSS_OPENWHISK_OPENWHISK_H_
