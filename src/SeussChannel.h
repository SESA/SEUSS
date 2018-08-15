//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef SEUSS_CHANNEL_H
#define SEUSS_CHANNEL_H

#include <unordered_map>

#include <ebbrt/IOBuf.h>
#include <ebbrt/Messenger.h>
#include <ebbrt/Message.h>
#include <ebbrt/SharedEbb.h>
#include <ebbrt/UniqueIOBuf.h>

#include "dsys/dsys.h"

enum SeussMsgType : uint8_t {
  ping = 0,
  request,
  reply,
};

struct SeussMsgHeader {
  SeussMsgType type;
  uint64_t id;
  size_t msg_len;
};

//TODO(jmcadden): make this a multi-node Ebb
class SeussChannel : public ebbrt::Messagable<SeussChannel>,
                     public ebbrt::SharedEbb<SeussChannel> {
public:
  static const ebbrt::EbbId global_id =
      ebbrt::GenerateStaticEbbId("SeussChannel");
  SeussChannel(ebbrt::EbbId ebbid);

  void Ping(ebbrt::Messenger::NetworkId nid);

  void SendRequest(ebbrt::Messenger::NetworkId nid, uint64_t id, std::string code);

	void SendReply();

  void ReceiveMessage(ebbrt::Messenger::NetworkId nid,
                      std::unique_ptr<ebbrt::IOBuf> &&buf);

private:
  std::mutex m_;
  std::unordered_map<uint32_t, ebbrt::Promise<void>> promise_map_;
  uint32_t id_{0};
};

constexpr auto seuss_channel =
    ebbrt::EbbRef<SeussChannel>(SeussChannel::global_id);

#endif
