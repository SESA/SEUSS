#include <iostream>
#include "Controller.h"

void
dsys::Controller::MemberSetEventMemberAdd(MemberId id) {
  std::cout << "<dsys> Member added (" << id << ")" << std::endl;
}

void
dsys::Controller::MemberSetEventMemberDelete(MemberId id) {
  std::cout << "<dsys> Member removed (" << id << ")" << std::endl;
}

