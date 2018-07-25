// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef SEUSS_OPENWHISK_KAFKA_H_
#define SEUSS_OPENWHISK_KAFKA_H_

#include <boost/program_options.hpp>

namespace openwhisk {

void heartbeat();
uint8_t kafka_init(boost::program_options::variables_map &vm);


} // end namespace openwhisk
#endif // SEUSS_OPENWHISK_MSG_H_
