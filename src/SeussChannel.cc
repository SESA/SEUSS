//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "SeussChannel.h"

#include <ebbrt/Debug.h>

// This is *IMPORTANT*, it allows the messenger to resolve remote HandleFaults
EBBRT_PUBLISH_TYPE(, SeussChannel);

using namespace ebbrt;

SeussChannel::SeussChannel(ebbrt::EbbId ebbid)
    : ebbrt::Messagable<SeussChannel>(ebbid) {}

void SeussChannel::Ping(ebbrt::Messenger::NetworkId nid){
  auto buf = MakeUniqueIOBuf(sizeof(SeussMsg), true);
  SendMessage(nid, std::move(buf));
};

void SeussChannel::ReceiveMessage(ebbrt::Messenger::NetworkId nid,
                    std::unique_ptr<ebbrt::IOBuf> &&buf){
  kassert(buf->ComputeChainDataLength() >= sizeof(SeussMsg));
  auto msg_header =
      reinterpret_cast<const SeussMsg*>(buf->Data());
  switch(msg_header->type){
    case SeussMsgType::ping:
      kprintf_force("SeussChannel - PING!");
      break;
    case SeussMsgType::request:
      kprintf_force("SeussChannel - Request");
      break;
    case SeussMsgType::reply:
      kprintf_force("SeussChannel - Reply");
      break;
  }
};
