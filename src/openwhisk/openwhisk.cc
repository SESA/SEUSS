// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "openwhisk.h"

po::options_description openwhisk::program_options() {
  po::options_description options("OpenWhisk");
  options.add(kafka::program_options());
  options.add(couchdb::program_options());
  return options;
}

bool openwhisk::process_program_options(po::variables_map &vm) {
  return (couchdb::init(vm) && kafka::init(vm));
};
