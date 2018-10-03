// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#include <unordered_map>
#include <string>
#include <iostream>
#include <stdio.h>

#include <pillowtalk.h>
#include <yajl/yajl_tree.h>

#include "openwhisk.h"

using namespace std;

namespace {
string couchdb_host;
string couchdb_password;
string couchdb_port;
string couchdb_protocol;
string couchdb_provider;
string couchdb_username;
string couchdb_address;
string couchdb_db_auth;
string couchdb_db_entity;
string couchdb_db_activation;
std::unordered_map<std::string, std::string> db_cache;
} // end local namespace

po::options_description openwhisk::couchdb::program_options() {
  po::options_description options("CouchDB");
  options.add_options()("couchdb_host", po::value<string>(&couchdb_host), "CouchDB host");
  options.add_options()("couchdb_password", po::value<string>(&couchdb_password),
                        "CouchDB password");
  options.add_options()("couchdb_port", po::value<string>(&couchdb_port), "CouchDB port");
  options.add_options()("couchdb_protocol", po::value<string>(&couchdb_protocol),
                        "CouchDB protocol");
  options.add_options()("couchdb_provider", po::value<string>(&couchdb_provider),
                        "CouchDB provider");
  options.add_options()("couchdb_username", po::value<string>(&couchdb_username),
                        "CouchDB username");
  options.add_options()("couchdb_db_auth", po::value<string>(&couchdb_db_auth),
                        "CouchDB Whisk Auth DB");
  options.add_options()("couchdb_db_entity", po::value<string>(&couchdb_db_entity),
                        "CouchDB Whisk Entity DB");
  options.add_options()("couchdb_db_activation", po::value<string>(&couchdb_db_activation),
                        "CouchDB Whisk Activation DB");
  return options;
}

bool openwhisk::couchdb::init(po::variables_map &vm) {
  std::cout << "CouchDB configuration:" << std::endl;
    std::cout << "  db_host:\t\t" << couchdb_host << std::endl;
    std::cout << "  db-username:\t\t"  << couchdb_username << std::endl;
    std::cout << "  db-password:\t\t" << couchdb_password << std::endl;
    std::cout << "  db-port:\t\t" << couchdb_port << std::endl;
    std::cout << "  db-protocol:\t\t" << couchdb_protocol << std::endl;
  
  if( couchdb_protocol.empty() || couchdb_host.empty() || couchdb_username.empty() || couchdb_password.empty() || couchdb_port.empty())
{
    std::cerr << "Error: CouchDB configuration incomplete" << std::endl;
    return false;
}

  couchdb_address = couchdb_protocol+"://"+couchdb_username+":"+couchdb_password+"@"+couchdb_host+":"+couchdb_port;
	return true;
}

std::string openwhisk::couchdb::get_action( openwhisk::msg::Action action ){
  std::string function_code = "";

  std::string key = action.path_ + action.name_ + action.version_;
  /* Check the cache for function code */
  auto cache_result = db_cache.find(key);
  if((cache_result != db_cache.end())){
    return cache_result->second;
  }

  pt_response_t* response = NULL;
  pt_init();
  std::string get_addr = couchdb_address+"/"+couchdb_db_entity+"/"+action.path_+"%2F"+action.name_;
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

  cout << "DB cache miss: " << key << endl;
  db_cache.emplace(key, function_code);
  return function_code;
}
