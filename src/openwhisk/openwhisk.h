// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef SEUSS_OPENWHISK_OPENWHISK_H_
#define SEUSS_OPENWHISK_OPENWHISK_H_

#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include "cppkafka/configuration.h"
#include "msg.h"

namespace openwhisk {

constexpr size_t ping_freq_ms = 1000;

namespace kafka {
bool init(po::variables_map &vm);
po::options_description program_options();
void ping_producer_loop(const cppkafka::Configuration &config,
                        uint64_t invoker_id);
void activation_consumer_loop(const cppkafka::Configuration &config,
                              uint64_t invoker_id);
} // end namespace kafka

namespace couchdb {
bool init(po::variables_map &vm);
po::options_description program_options();
std::string get_action(msg::Action action);
} // end namespace couchdb

/* Program Options */
po::options_description program_options(); 
bool process_program_options(po::variables_map &vm);

} // end namespace openwhisk
#endif // SEUSS_OPENWHISK_OPENWHISK_H_
