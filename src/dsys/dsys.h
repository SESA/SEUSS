// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef DSYS_DSYS_H_
#define DSYS_DSYS_H_

#include <string>
#include <boost/program_options.hpp>
namespace po = boost::program_options;

/** dsys.h
  * Interface to the distributed system of the ebbrt instance 
  */

#include "Controller.h"
#include "MemberSet.h"

namespace ebbrt {
namespace dsys {

// Global configuration
extern std::string  native_binary_path;
extern std::string  zookeeper_host;
extern uint16_t initial_instance_count;
extern bool local_init;

#ifdef __ebbrt__
  using IpAddr =  ebbrt::Ipv4Address;
#else
  using IpAddr =  boost::asio::ip::address_v4;
#endif
  using MemberId = std::string;

void Init();


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

/* Program Options */
po::options_description program_options();
bool process_program_options(po::variables_map &vm);




} // namespace dsys
} // namespace ebbrt

#endif //DSYS_DSYS_H_
