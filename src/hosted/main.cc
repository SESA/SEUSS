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
#include <boost/program_options.hpp>
namespace po = boost::program_options;

/** EbbRT */
#include <ebbrt/Cpu.h> // ebbrt::Cpu::EarlyInit
#include "../dsys/dsys.h"
#include "../openwhisk/openwhisk.h"


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

int main(int argc, char **argv) {
  std::cout << "********************************************" << std::endl;
  std::cout << "  SEUSS OpenWhisk Invoker                   "<< std::endl;
  std::cout << "********************************************" << std::endl;

  /* program options */
  po::variables_map povm;
  po::options_description po("Default configuration");
  po.add_options()("help", "Help message"); // Default

  // ebbrt dsys instance options 
  po.add(ebbrt::dsys::program_options()); 

  // openwhisk options - database and messaging service
  po.add(openwhisk::program_options());

  /* process command line arguments */
  try {
    po::store(po::parse_command_line(argc, argv, po), povm);
    po::notify(povm);
    if (povm.count("help")) {
      /** display help menu and exit */
      std::cout << po << std::endl;
      return 0;
    }
  } catch (std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl << po << std::endl;
    return 1;
  }

  /** Initialize openwhisk with input arguments */
  if (!openwhisk::process_program_options(povm)) {
    std::cerr << "Error: openwhisk initialization failed" << std::endl; 
  }

  /** Initialize ebbrt dsys with input arguments */
  if (!ebbrt::dsys::process_program_options(povm)) {
    std::cerr << "Error: EbbRT initialization failed " << std::endl; 
  }

  /** Start EbbRT runtime */
  void *status;
  pthread_t tid =
      ebbrt::Cpu::EarlyInit((1 + ebbrt::dsys::initial_instance_count));
  pthread_join(tid, &status);
  return 0;
}
