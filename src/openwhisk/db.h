// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef SEUSS_OPENWHISK_DB_H_
#define SEUSS_OPENWHISK_DB_H_

#include "msg.h"

#include <boost/program_options.hpp>
namespace po = boost::program_options;

namespace openwhisk {
namespace db {

po::options_description couchdb_po(); 
bool couchdb_process_po(po::variables_map &vm);
std::string get_action(msg::Action action);

} // end namespace db
} // end namespace openwhisk
#endif // SEUSS_OPENWHISK_MSG_H_
