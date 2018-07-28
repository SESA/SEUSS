// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef SEUSS_OPENWHISK_KAFKA_H_
#define SEUSS_OPENWHISK_KAFKA_H_

#include <boost/program_options.hpp>
#include "cppkafka/configuration.h"

namespace openwhisk {

constexpr size_t ping_freq_ms = 1000;

void ping_producer_loop(const cppkafka::Configuration& config, uint64_t invoker_id);
void activation_consumer_loop(const cppkafka::Configuration& config, uint64_t invoker_id);
bool kafka_init(boost::program_options::variables_map &vm);


} // end namespace openwhisk
#endif // SEUSS_OPENWHISK_MSG_H_
