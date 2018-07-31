// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#include <string>
#include <stdio.h>

#include <pillowtalk.h>
#include <yajl/yajl_tree.h>

#include "db.h"

using namespace std;

namespace {
string db_host;
string db_password;
string db_port;
string db_protocol;
string db_provider;
string db_username;
string db_address;
} // end local namespace

po::options_description openwhisk::db::couchdb_po() {
  po::options_description options("CouchDB");
  options.add_options()("db_host", po::value<string>(&db_host), "CouchDB host");
  options.add_options()("db_password", po::value<string>(&db_password),
                        "CouchDB password");
  options.add_options()("db_port", po::value<string>(&db_port), "CouchDB port");
  options.add_options()("db_protocol", po::value<string>(&db_protocol),
                        "CouchDB protocol");
  options.add_options()("db_provider", po::value<string>(&db_provider),
                        "CouchDB provider");
  options.add_options()("db_username", po::value<string>(&db_username),
                        "CouchDB username");
  return options;
}

bool openwhisk::db::couchdb_process_po(po::variables_map &vm) {
  std::cout << "Database Config:" << std::endl;
  if (vm.count("db_host"))
    std::cout << "db_host: " << db_host << std::endl;
  if (vm.count("db_username"))
    std::cout << "db-username: " << db_username << std::endl;
  if (vm.count("db_password"))
    std::cout << "db-password: " << db_password << std::endl;
  if (vm.count("db_port"))
    std::cout << "db-port: " << db_port << std::endl;
  if (vm.count("db_protocol"))
    std::cout << "db-protocol: " << db_protocol << std::endl;
  if (vm.count("db_provider"))
    std::cout << "db-provider: " << db_provider << std::endl;
  
  if( db_protocol.empty() || db_host.empty() || db_username.empty() || db_password.empty() || db_port.empty())
    return false;

  db_address = db_protocol+"://"+db_username+":"+db_password+"@"+db_host+":"+db_port;
	return true;
}

std::string openwhisk::db::get_action( openwhisk::msg::Action action ){
  std::string function_code = "";
  pt_response_t* response = NULL;
  pt_init();
  std::string get_addr = db_address+"/whisk_kumowhisks/"+action.path_+"%2F"+action.name_;
  cout << "DB request addr: " << get_addr << endl;
  response = pt_unparsed_get(get_addr.c_str());

  cout << "Raw DB reponse: " << response->raw_json << endl;

  const char *json_code_path[] = {"exec", "code", (const char *)0};
  char errbuf[1024];
  yajl_val yv;

  auto yajl_node =
      yajl_tree_parse(response->raw_json, errbuf, response->raw_json_len);
  if (yajl_node == NULL) {
    fprintf(stderr, "DB response json parse_error: %s\n", errbuf);
    return function_code;
  }
  yv = yajl_tree_get(yajl_node, json_code_path, yajl_t_string);
  if (yv)
    function_code = YAJL_GET_STRING(yv);

  yajl_tree_free(yajl_node);
  return function_code;
}
