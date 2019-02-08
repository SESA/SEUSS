// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#include <iostream>
#include "Controller.h"

#ifndef __ebbrt__
#include "../SeussController.h"
#endif

void
ebbrt::dsys::Controller::MemberSetEventMemberAdd(MemberId id) {
  std::cout << "<dsys> Member added (" << id << ")" << std::endl;
#ifndef __ebbrt__
  seuss::controller->RegisterNode(ebbrt::Messenger::NetworkId(get_member_ip(id)));
#endif
}

void
ebbrt::dsys::Controller::MemberSetEventMemberDelete(MemberId id) {
  std::cout << "<dsys> Member removed (" << id << ")" << std::endl;
}

#ifndef __ebbrt__
void
ebbrt::dsys::Controller::AllocateNativeInstances(std::string binary_path) {

  kassert(!binary_path.empty());
  struct timeval START_TIME;
  gettimeofday(&START_TIME, NULL);
  ebbrt::NodeAllocator::NodeArgs args;
  args.cpus = native_core_count;
  args.ram = native_memory_gb;
  args.numa = native_numa_count;
  // Set minimum of 2GB per VCPU 
  if(args.ram < (args.cpus*2))
      args.ram = (args.cpus * 2);
  // Set minimum of 1 cpu per NUMA node 
  if(args.cpus < (args.numa))
      args.numa = (args.cpus);

  // Set CmdArgs for the InvokerCore Ebb
  ebbrt::node_allocator->AppendArgs("Clim=" + std::to_string(native_invoker_core_concurrency_limit));
  ebbrt::node_allocator->AppendArgs("Slim=" + std::to_string(native_invoker_core_spicy_limit));
  if(native_invoker_core_spicy_limit && native_invoker_core_spicy_reuse)
    ebbrt::node_allocator->AppendArgs("Rlim=" + std::to_string(native_invoker_core_spicy_reuse));

  auto node_desc = ebbrt::node_allocator->AllocateNode(binary_path, args);
  node_desc.NetworkId().Then([START_TIME](
      ebbrt::Future<ebbrt::Messenger::NetworkId> f) {
      struct timeval END_TIME;
      gettimeofday(&END_TIME, NULL);
      std::printf("ALLOCATION TIME: %lf seconds\n",
                  (END_TIME.tv_sec - START_TIME.tv_sec) +
                      ((END_TIME.tv_usec - START_TIME.tv_usec) / 1000000.0));
  });
  node_descriptors_.emplace_back(std::move(node_desc));
}
#endif
