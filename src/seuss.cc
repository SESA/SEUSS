//          Copyright Boston University SESA Group 2013 - 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#if __ebbrt__ // native

#include <ebbrt/Debug.h>
#include <iostream>

#include "dsys/dsys.h"

void AppMain() {
  ebbrt::kprintf("Entered App Main \n");
  // TODO: Block until the ZKGidMap is online
  ebbrt::event_manager->Spawn([]() { ebbrt::dsys::Init(); });
}

#else // hosted (linux)

#include <assert.h>
#include <ebbrt/Debug.h>
#include <iostream>

using std::string;
using std::cout;
using std::endl;

#include "dsys/dsys.h"

void AppMain() {

  ebbrt::dsys::Init(); // Static Ebb constructor

}
#endif
