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

} // end namespace openwhisk
#endif // SEUSS_OPENWHISK_OPENWHISK_H_
