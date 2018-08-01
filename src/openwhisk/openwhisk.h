// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef SEUSS_OPENWHISK_OPENWHISK_H_
#define SEUSS_OPENWHISK_OPENWHISK_H_

#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include "kafka.h"
#include "db.h"

namespace openwhisk {

/* Program Options */
po::options_description program_options(){
  po::options_description options("OpenWhisk");
  options.add_options()("kafka-brokers,k", po::value<string>(),"kafka host")
                       ("kafka-topic,t", po::value<uint64_t>(), "invoker Id");
  options.add(db::couchdb_po());
  return options;
}
bool process_program_options(po::variables_map &vm){
  return true;
};


} // end namespace openwhisk
#endif // SEUSS_OPENWHISK_OPENWHISK_H_
