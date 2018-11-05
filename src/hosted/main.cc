//          Copyright Boston University SESA Group 2013 - 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#include <cinttypes> // PRId64, etc
#include <csignal>
#include <iostream>
#include <fstream> // std::ifstream
#include <stdlib.h> // exit
#include <thread> 
#include <chrono>  

using std::string;
using std::cout;
using std::endl;

#include <boost/program_options.hpp>
namespace po = boost::program_options;

/** EbbRT */
#include <ebbrt/Cpu.h> // ebbrt::Cpu::EarlyInit
#include "../dsys/dsys.h"
#include "../openwhisk/openwhisk.h"

int main(int argc, char **argv) {
  std::cout << "********************************************" << std::endl;
  std::cout << "  SEUSS OpenWhisk Invoker                   "<< std::endl;
  std::cout << "********************************************" << std::endl;

  /* program options */
  po::variables_map povm;
  po::options_description po("Default configuration");
  po.add_options()("help", "Help message"); // Default

  // Seuss Invoker mode program options
  po.add_options()("mode", po::value<std::string>(&openwhisk::mode)->default_value("default"),
                   "Seuss Invoker mode (default, benchmark, null)");
  po.add_options()("invoker-delay,d", po::value<uint64_t>()->default_value(0), "Sleep time between invocations (ms)");
  po.add_options()("file,f", po::value<std::string>(),
                        "javascript function (benchmark mode)");

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

  if (povm.count("mode")) {
    std::cout << "Seuss Invoker Mode: " << openwhisk::mode << std::endl;
  }

  if (openwhisk::mode == "default" || openwhisk::mode == "null") {
    /** Initialize openwhisk with input arguments */
    if (!openwhisk::process_program_options(povm)) {
      std::cerr << "OpenWhisk initialization failed" << std::endl;
      std::exit(1);
    }
  }

  if (povm.count("file") && openwhisk::mode == "benchmark") {
    auto bindir = fs::current_path() / povm["file"].as<std::string>();
		std::string function_path = bindir.string();
    std::cout << "Target function: " << function_path  << std::endl;

    // read in file to string
    std::ifstream infile(function_path);
    if(!infile.good()){
      std::cerr << "Unable to open file: " <<  function_path << std::endl;
      std::exit(1);
    }
    std::stringstream file_buffer;
    file_buffer << infile.rdbuf();
    openwhisk::function = file_buffer.str();
    std::cout << "--------------------------------------------" << std::endl
              << openwhisk::function << std::endl
              << "--------------------------------------------" << std::endl;
  }

  if(openwhisk::mode == "default" || openwhisk::mode == "benchmark"){
    /** Initialize ebbrt dsys with input arguments */
    if (!ebbrt::dsys::process_program_options(povm)) {
      std::cerr << "EbbRT initialization failed " << std::endl;
      std::exit(1);
    }
  }

  /** Start EbbRT runtime */
  void *status;
  pthread_t tid =
      ebbrt::Cpu::EarlyInit((1 + openwhisk::thread_count + ebbrt::dsys::native_instance_count));
  pthread_join(tid, &status);
  return 0;
}
