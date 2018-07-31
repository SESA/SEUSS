//          Copyright Boston University SESA Group 2013 - 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <cinttypes> // PRId64, etc
#include <csignal>
#include <iostream>
#include <stdlib.h> // exit
#include <thread> 
#include <chrono>  

using std::string;
using std::cout;
using std::endl;

/** boost */
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
namespace po = boost::program_options;
namespace fs = boost::filesystem;


/** EbbRT */
#include <ebbrt/Cpu.h> // ebbrt::Cpu::EarlyInit
#include "common.h"
#include "../openwhisk/kafka.h"
#include "../openwhisk/db.h"


string native_binary_path;
uint8_t native_instances=1;
string zookeeper_hosts;

namespace { // local
static char *ExecName = 0;
} // end local namespace

/** Boost Program Options */
po::options_description ebbrt_po(){
  po::options_description options("EbbRT configuration");
  options.add_options()("natives,n", po::value<uint8_t>(&native_instances),
                        "EbbRT native instances");
  options.add_options()("elf32,b", po::value<string>(&native_binary_path),
                        "EbbRT native binary");
  options.add_options()("zookeeper,z", po::value<string>(&zookeeper_hosts),
                        "Zookeeper Hosts");
  return options;
}

po::options_description kafka_po(){
    po::options_description options("Kafka");
    options.add_options()
        ("kafka-brokers,k",  po::value<string>(), 
                       "Kafka brokers")
        ("kafka-topic,t",    po::value<uint64_t>(),
                       "Invoker Id")
        ;
  return options;
}

bool ebbrt_process_po(po::variables_map &vm) {
  bool spawn = false;
  if (vm.count("natives")) {
    std::cout << "Native instances to spawn: " << vm["natives"].as<uint8_t>() << std::endl;
  }
  if (vm.count("elf32")) {
    std::cout << "Native binary to boot: " << vm["elf32"].as<string>() << std::endl;
    auto bindir = fs::system_complete(ExecName).parent_path() /
                  vm["elf32"].as<string>();
		native_binary_path = bindir.string();
		spawn = true;
  }
  if (vm.count("zookeeper")) {
    std::cout << "Zookeeper Hosts: " << vm["zookeeper"].as<string>() << std::endl;
  }
  return spawn;
}

#if 0
void msg_test(){
  std::string sample_json_action =
      R"({"rootControllerIndex":{"instance":0},"activationId":"1397779f549b428897779f549ba288c0","revision":"1-d3cede740fe6b72ccba5c9d164a91a86","transid":["4bc341e87a5cfe55c62ae0a1695af0f0",1532293812971],"content":{},"blocking":true,"action":{"path":"guest","name":"4914","version":"0.0.1"},"user":{"subject":"guest","authkey":"23bc46b1-71f6-4ed5-8c54-816aa4f8c502:123zO3xZCLrMN6v2BKK1dXYFpXlPkccOFqm12CdAsMgRU4VrNZ9lyGVCGuMDGIwP","rights":["READ","PUT","DELETE","ACTIVATE"],"limits":{},"namespace":"guest"}})";

  cout << "Sample JSON input:" << endl << sample_json_action << endl;
  openwhisk::msg::ActivationMessage AM(sample_json_action);
  cout << "ActivationMessage JSON output:" << endl << AM.to_json() << endl;
}

std::string couchdb_test(openwhisk::msg::Action action){
  std::string function_code = "";
  pt_init();

  std::string couchdb_address = "http://www."+db_host+":"+db_port+"/whisk_kumowhisks/"+action.path_+"%2F"+action.name
  pt_response_t* response = NULL;
  response = pt_unparsed_get("http://localhost:5984/pillowtalk_basics");

#if 0
  pt_node_t* root = pt_map_new();
  pt_map_set(root,"_id",pt_string_new("star_wars"));

  pt_node_t* movies = pt_map_new();
  pt_map_set(root,"movies",movies);

  // build movie subdocument
  pt_node_t* ep4 = pt_map_new();

  // build characters array
  pt_node_t* ep4_chars = pt_array_new();
  pt_array_push_back(ep4_chars,pt_string_new("Luke Skywalker"));
  pt_array_push_back(ep4_chars,pt_string_new("Han Solo"));
  pt_array_push_back(ep4_chars,pt_string_new("Obi Wan Kenobi"));

  pt_map_set(ep4,"characters", ep4_chars);

  pt_map_set(movies,"Star Wars Episode IV",ep4);

  pt_response_t* response = NULL;
  response = pt_delete("http://localhost:5984/pillowtalk_basics");
  pt_free_response(response);
  response = pt_put("http://localhost:5984/pillowtalk_basics",NULL);
  pt_free_response(response);
  response = pt_put("http://localhost:5984/pillowtalk_basics/star_wars",root);
  assert(response->response_code == 201);
  pt_free_response(response);

  pt_free_node(root);

  response = pt_get("http://localhost:5984/pillowtalk_basics/star_wars");
  assert(response->response_code == 200);

  pt_node_t* doc = response->root;
  const char* id = pt_string_get(pt_map_get(doc,"_id"));
  assert(!strcmp(id,"star_wars"));

  pt_node_t* ep4_node = pt_map_get(pt_map_get(doc,"movies"),"Star Wars Episode IV");
  pt_node_t* characters_node = pt_map_get(ep4_node,"characters");
  int array_len = pt_array_len(characters_node);
  assert(array_len == 3);

  pt_map_set(ep4_node,"year",pt_string_new("1977"));
  pt_array_push_back(characters_node,pt_string_new("Princess Leia"));
  pt_response_t* put_response = pt_put("http://localhost:5984/pillowtalk_basics/star_wars", doc);

  pt_free_response(response);
  pt_free_response(put_response);

  pt_cleanup();
      std::cout << "FINISHED PT TEST" << std::endl;
#endif
  return funtion_code;
}
#endif


int main(int argc, char **argv) {
  void *status;
  ExecName = argv[0];
    std::cout << "********************************************" << std::endl;
    std::cout << "  Loading SEUSS OpenWhisk Invoker "<< std::endl;
    std::cout << "********************************************" << std::endl;

  uint32_t port;
  /* process program options */
  po::options_description po("Default configuration");
  po::variables_map povm;
  po.add_options()("help", "Help message"); // Default
  po.add_options()("port,p", po::value<uint32_t>(&port), "Port");
  po.add(ebbrt_po()); // EbbRT program options
  po.add(kafka_po()); // Kafka (cppkafka) program options
  po.add(openwhisk::db::couchdb_po()); // CouchDB (pillowtalk) program options
  try {
    po::store(po::parse_command_line(argc, argv, po), povm);
    po::notify(povm); // Run all "po::notify" functions
  } catch (std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl << po << std::endl;
    return 1;
  }

  /** display help menu and exit */
  if (povm.count("help")) {
    std::cout << po << std::endl;
    return 0;
  }

	/** deploy kafka test */ 
  if (!openwhisk::kafka_init(povm)) {
    std::cerr << "Error: kafka init " << std::endl; 
  }

	/** couchdb settings */ 
  if (!openwhisk::db::couchdb_process_po(povm)) {
    std::cerr << "Error: couchdb init " << std::endl; 
  }

  std::cerr << "Warning, entering while(1)..." << std::endl; 
  while(1);

#if 0
  /** deploy ebbrt runtime  & backends */
  if (!ebbrt_process_po(povm)) {
    // 
    std::cerr << "Error: ebbrt init " << std::endl; 
  }
  pthread_t tid = ebbrt::Cpu::EarlyInit(1);
  pthread_join(tid, &status);
#endif
  return 0;
}
