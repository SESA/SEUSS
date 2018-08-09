// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#include <iostream>
#include "Controller.h"

#include "../SeussChannel.h"

void
ebbrt::dsys::Controller::MemberSetEventMemberAdd(MemberId id) {
  std::cout << "<dsys> Member added (" << id << ")" << std::endl;
#ifndef __ebbrt__
  seuss_channel->Ping(ebbrt::Messenger::NetworkId(get_member_ip(id)));
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
  auto node_desc = ebbrt::node_allocator->AllocateNode(binary_path);
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
