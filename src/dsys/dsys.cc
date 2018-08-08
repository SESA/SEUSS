// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "dsys.h"

std::string  ebbrt::dsys::native_binary_path;
std::string  ebbrt::dsys::zookeeper_host;
uint16_t ebbrt::dsys::initial_instance_count;
bool ebbrt::dsys::local_init;

void ebbrt::dsys::Init(){
  auto tr = new Controller(Controller::global_id);
  Controller::Create(tr, Controller::global_id);
  controller->Init();
#if __ebbrt__ 
  controller->Join();
#else
  auto count = initial_instance_count;
  while(count--){
    controller->AllocateNativeInstances(native_binary_path);
  }
#endif
  local_init = true;
}

#ifndef __ebbrt__
po::options_description ebbrt::dsys::program_options() {
  po::options_description options("EbbRT configuration");
  options.add_options()("natives,n", po::value<uint16_t>(&initial_instance_count),
                        "native instance count");
  options.add_options()("elf32,b", po::value<std::string>(),
                        "native binary path");
  options.add_options()("zookeeper,z", po::value<std::string>(),
                        "Zookeeper Hosts");
  return options;
} 

bool ebbrt::dsys::process_program_options(po::variables_map &vm){

  // native instance count
  if (vm.count("natives")) {
    std::cout << "Native instances to spawn: " << vm["natives"].as<uint16_t>() << std::endl;
  }else{
    initial_instance_count = 0; 
  }

  // native binary path
  if (vm.count("elf32")) {
    std::cout << "Native binary to boot: " << vm["elf32"].as<std::string>() << std::endl;
    auto bindir = fs::current_path() / vm["elf32"].as<std::string>();
		native_binary_path = bindir.string();
  }

  // zookeeper host
  if (vm.count("zookeeper")) {
    std::cout << "Zookeeper Hosts: " << vm["zookeeper"].as<std::string>() << std::endl;
    zookeeper_host =  vm["zookeeper"].as<std::string>();
  }
  return true;
}
#endif
