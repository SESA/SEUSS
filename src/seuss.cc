//          Copyright Boston University SESA Group 2013 - 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/Debug.h>

#include "SeussChannel.h"
#include "dsys/dsys.h"

#if __ebbrt__ // native

#include <ebbrt/Debug.h>
#include <iostream>


void AppMain() {
  ebbrt::kprintf("Entered App Main \n");
  // TODO: Block until the ZKGidMap is online
  ebbrt::event_manager->Spawn([]() {
    ebbrt::dsys::Init();
    auto rep = new SeussChannel(SeussChannel::global_id);
    SeussChannel::Create(rep, SeussChannel::global_id);
  });
}

#else // hosted (linux)

#include <assert.h>
#include <iostream>
#include "openwhisk/openwhisk.h"

using std::string;
using std::cout;
using std::endl;

void AppMain() {
  ebbrt::dsys::Init(); // Static Ebb constructor
  auto rep = new SeussChannel(SeussChannel::global_id);
  SeussChannel::Create(rep, SeussChannel::global_id);
  //openwhisk::kafka_init();
}
#endif
