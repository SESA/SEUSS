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
  auto buf = MakeUniqueIOBuf(sizeof(SeussMsgHeader), true);
  SendMessage(nid, std::move(buf));
};

void SeussChannel::SendRequest(ebbrt::Messenger::NetworkId nid, uint64_t id,
                               std::string code) {
  auto buf = MakeUniqueIOBuf(sizeof(SeussMsgHeader) + code.size());
  auto dp = buf->GetMutDataPointer();
  auto& msg_header = dp.Get<SeussMsgHeader>();
  //auto msg_header =
  //    reinterpret_cast<const SeussMsgHeader*>(buf->MutData());
  msg_header.type = SeussMsgType::request;
  msg_header.id = id;
  msg_header.msg_len = code.size();
  auto str_ptr = reinterpret_cast<char *>(dp.Data());
  code.copy(str_ptr, code.size());
  SendMessage(nid, std::move(buf));
}

void SeussChannel::ReceiveMessage(ebbrt::Messenger::NetworkId nid,
                    std::unique_ptr<ebbrt::IOBuf> &&buf){
  kassert(buf->ComputeChainDataLength() >= sizeof(SeussMsgHeader));
  auto dp = buf->GetDataPointer();
  auto& msg_header = dp.Get<SeussMsgHeader>();
  std::string msg;
  switch(msg_header.type){
    case SeussMsgType::ping:
      kprintf_force("SeussChannel - PING!\n");
      break;
    case SeussMsgType::request:
      kprintf_force("SeussChannel - Request\n");
      msg.assign(reinterpret_cast<const char *>(dp.Data()), msg_header.msg_len);
      break;
    case SeussMsgType::reply:
      kprintf_force("SeussChannel - Reply\n");
      msg.assign(reinterpret_cast<const char *>(dp.Data()), msg_header.msg_len);
      break;
  }
  if(msg_header.msg_len > 0){
    std::cout << "MSG: [" << msg << "]" << std::endl;
  }
};
