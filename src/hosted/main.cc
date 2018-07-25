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
#include "../openwhisk/msg.h"
#include "../openwhisk/kafka.h"


string native_binary_path;
uint8_t native_instances=1;
string zookeeper_hosts;

namespace { // local
static char *ExecName = 0;


// CouchDB
string db_host;
string db_password;
string db_port;
string db_protocol;
string db_provider;
string db_username;
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

po::options_description couchdb_po() {
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


bool couchdb_process_po(po::variables_map &vm) {
	// TODO: print out CouchDB arguments
	return true;
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

void msg_test(){
  std::string sample_json_action =
      R"({"rootControllerIndex":{"instance":0},"activationId":"1397779f549b428897779f549ba288c0","revision":"1-d3cede740fe6b72ccba5c9d164a91a86","transid":["4bc341e87a5cfe55c62ae0a1695af0f0",1532293812971],"content":{},"blocking":true,"action":{"path":"guest","name":"4914","version":"0.0.1"},"user":{"subject":"guest","authkey":"23bc46b1-71f6-4ed5-8c54-816aa4f8c502:123zO3xZCLrMN6v2BKK1dXYFpXlPkccOFqm12CdAsMgRU4VrNZ9lyGVCGuMDGIwP","rights":["READ","PUT","DELETE","ACTIVATE"],"limits":{},"namespace":"guest"}})";

  cout << "Sample JSON input:" << endl << sample_json_action << endl;
  openwhisk::msg::ActivationMessage AM(sample_json_action);
  cout << "ActivationMessage JSON output:" << endl << AM.to_json() << endl;
}

int main(int argc, char **argv) {
  void *status;
  ExecName = argv[0];

  uint32_t port;
  /* process program options */
  po::options_description po("Default configuration");
  po::variables_map povm;
  po.add_options()("help", "Help message"); // Default
  po.add_options()("port,p", po::value<uint32_t>(&port), "Port");
  po.add(ebbrt_po()); // EbbRT program options
  po.add(kafka_po()); // Kafka (cppkafka) program options
  po.add(couchdb_po()); // CouchDB (pillowtalk) program options
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

  msg_test();

  /** deploy ebbrt runtime  & backends */
  if (ebbrt_process_po(povm)) {
    pthread_t tid = ebbrt::Cpu::EarlyInit(1);
    pthread_join(tid, &status);
    return 0;
  }

	/** deploy kafka test */ 
  if (openwhisk::kafka_init(povm)) {
    return 1;
  }

	/** couchdb settings */ 
  if (couchdb_process_po(povm)) {
    return 1;
  }

  return 0;
}
