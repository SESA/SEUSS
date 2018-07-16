//          Copyright Boston University SESA Group 2013 - 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#if __ebbrt__ // native

#include <iostream>
#include <ebbrt/Debug.h>

#include "dsys/dsys.h"

void AppMain() {
  ebbrt::kprintf("Entered App Main \n");
  // TODO: Block until the ZKGidMap is online
  ebbrt::event_manager->Spawn([]() {
    dsys::Init();
  });
}

#else // hosted (linux)

#include <iostream>
#include <assert.h>
#include <ebbrt/Debug.h>
#include <ebbrt/hosted/NodeAllocator.h>

static int ALLOCATED_NODES = 0;

using std::string;
using std::cout;
using std::endl;

#include "hosted/common.h"
#include "dsys/dsys.h"

void AppMain() {

  string path = native_binary_path;
  uint8_t NumNodes = native_instances;
  assert(path.size() > 0);
  struct timeval START_TIME;
  gettimeofday(&START_TIME, NULL);

  dsys::Init();

  try {
    for (int i = 0; i < NumNodes; i++) {
      auto node_desc = ebbrt::node_allocator->AllocateNode(path);
      node_desc.NetworkId().Then(
          [START_TIME, NumNodes](ebbrt::Future<ebbrt::Messenger::NetworkId> f) {
            f.Get();
            ALLOCATED_NODES++;
            if (ALLOCATED_NODES == NumNodes) {
              struct timeval END_TIME;
              gettimeofday(&END_TIME, NULL);
              std::printf(
                  "ALLOCATION TIME: %lf seconds\n",
                  (END_TIME.tv_sec - START_TIME.tv_sec) +
                      ((END_TIME.tv_usec - START_TIME.tv_usec) / 1000000.0));
            }
          });
    }
  } catch (std::runtime_error &e) {
    std::cout << e.what() << std::endl;
    exit(1);
  }
}
#endif 
