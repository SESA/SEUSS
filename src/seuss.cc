//          Copyright Boston University SESA Group 2013 - 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/Debug.h>

#include "SeussChannel.h"
#include "dsys/dsys.h"

#if __ebbrt__ // native

#include <iostream>

#include <ebbrt/Debug.h>

#include "SeussInvoker.h"

void AppMain() {
  // TODO: Block until the ZKGidMap is online
  ebbrt::event_manager->Spawn([]() {
    ebbrt::dsys::Init();
    seuss::Init();
  });
}

#else // hosted (linux)

#include <assert.h>
#include <iostream>
#include "openwhisk/openwhisk.h"

#include "SeussController.h"

void AppMain() {
  ebbrt::dsys::Init(); // Static Ebb constructor
  seuss::Init();
  //openwhisk::connect();
  openwhisk::test();
}
#endif
