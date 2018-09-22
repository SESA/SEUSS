// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "dsys.h"

std::string  ebbrt::dsys::native_binary_path;
std::string  ebbrt::dsys::zookeeper_host;
uint16_t ebbrt::dsys::native_instance_count;
uint16_t ebbrt::dsys::native_core_count;
uint16_t ebbrt::dsys::native_numa_count;
uint16_t ebbrt::dsys::native_memory_gb;
bool ebbrt::dsys::local_init;

void ebbrt::dsys::Init(){
  auto tr = new Controller(Controller::global_id);
  Controller::Create(tr, Controller::global_id);
  controller->Init();
#if __ebbrt__ 
  controller->Join();
#else
  auto count = native_instance_count;
  while(count--){
    controller->AllocateNativeInstances(native_binary_path);
  }
#endif
  local_init = true;
}

#ifndef __ebbrt__
po::options_description ebbrt::dsys::program_options() {
  po::options_description options("EbbRT configuration");
  options.add_options()("natives,n", po::value<uint16_t>(&native_instance_count)->default_value(1),
                        "native instance count");
  options.add_options()("cores,c", po::value<uint16_t>(&native_core_count)->default_value(2),
                        "native instance core amount");
  options.add_options()("ram,m", po::value<uint16_t>(&native_memory_gb)->default_value(4),
                        "native instance memory (GB)");
  options.add_options()("numa", po::value<uint16_t>(&native_numa_count)->default_value(1),
                        "native instance numa nodes");
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
  }
  // native binary path
  if (vm.count("elf32")) {
    auto bindir = fs::current_path() / vm["elf32"].as<std::string>();
		native_binary_path = bindir.string();
    std::cout << "Native binary path: " << native_binary_path  << std::endl;
  }

  // SEUSS-SPECIFIC SETTINGS
  if( native_memory_gb < (native_core_count * 4))
    native_memory_gb = (native_core_count * 4);
 
  // VM configuration
  std::cout << "Native instance config: cores=" << native_core_count 
            << ", ram=" << native_memory_gb << "GB, numa=" << native_numa_count << std::endl;

  // zookeeper host
  if (vm.count("zookeeper")) {
    std::cout << "Zookeeper Hosts: " << vm["zookeeper"].as<std::string>() << std::endl;
    zookeeper_host =  vm["zookeeper"].as<std::string>();
  }
  return true;
}
#endif
