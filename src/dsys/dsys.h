// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef DSYS_DSYS_H_
#define DSYS_DSYS_H_

/** dsys.h
  * Distributed system interface
  */

#include "Controller.h"
#include "MemberSet.h"

namespace dsys {

#ifdef __ebbrt__
  using IpAddr =  ebbrt::Ipv4Address;
#else
  using IpAddr =  boost::asio::ip::address_v4;
#endif
  using MemberId = std::string;

static inline void Init(){
  auto tr = new Controller(Controller::global_id);
  Controller::Create(tr, Controller::global_id);
  controller->Init();
#if __ebbrt__ 
  controller->Join();
#endif
}

/** get_member_address Returns IpAddr of Member */
static inline IpAddr get_member_ip(MemberId mid) {
#ifdef __ebbrt__
  std::array<uint8_t, 4> binary = {0, 0, 0, 0};
  auto count = 0;
  auto last = 0;
  auto next = mid.find('.', last);
  while (count < 4) {
    auto subs = mid.substr(last, next - last);
    sscanf(subs.c_str(), "%hhu", &binary[count]);
    last = next + 1;
    next = mid.find('.', last);
    count++;
  }
  return ebbrt::Ipv4Address(binary);
#else /* Linux */
  return boost::asio::ip::address_v4::from_string(mid);
#endif
}
}

#endif //DSYS_DSYS_H_
