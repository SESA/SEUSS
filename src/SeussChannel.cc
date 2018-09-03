//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "SeussChannel.h"
#ifdef __ebbrt__
#include "SeussInvoker.h"
#else
#include "SeussController.h"
#endif

#include <ebbrt/Debug.h>

// This is *IMPORTANT*, it allows the messenger to resolve remote HandleFaults
EBBRT_PUBLISH_TYPE(seuss, SeussChannel);

using namespace ebbrt;

seuss::SeussChannel::SeussChannel(ebbrt::EbbId ebbid)
    : ebbrt::Messagable<SeussChannel>(ebbid) {}

void seuss::SeussChannel::Ping(ebbrt::Messenger::NetworkId nid){
  // Ping msg has no body and all header fields == 0
  auto buf = MakeUniqueIOBuf(sizeof(MsgHeader), true);
  SendMessage(nid, std::move(buf));
};

void seuss::SeussChannel::SendReply(ebbrt::Messenger::NetworkId nid, ActivationRecord ar, std::string args) {
  // New IOBuf for the outgoing message
  auto buf =
      MakeUniqueIOBuf(sizeof(MsgHeader) + args.size());
  auto dp = buf->GetMutDataPointer();
  // Complete the message header
  auto &hdr = dp.Get<MsgHeader>();
  hdr.type = MsgType::reply;
  hdr.record = ar;
  hdr.record.args_size = args.size(); // Is this nescessary? 
  // Copy in msg payload
  hdr.len = args.size();
  auto str_ptr = reinterpret_cast<char *>(dp.Data());
  if (args.size() > 0) {
    args.copy(str_ptr, args.size());
  }
  SendMessage(nid, std::move(buf));
}

void seuss::SeussChannel::SendRequest(ebbrt::Messenger::NetworkId nid, uint64_t tid,
                               size_t fid, std::string args, std::string code) {
  // New IOBuf for the outgoing message
  auto buf =
      MakeUniqueIOBuf(sizeof(MsgHeader) + args.size() + code.size());
  auto dp = buf->GetMutDataPointer();
  // Complete the message header
  auto &hdr = dp.Get<MsgHeader>();
  hdr.type = MsgType::request;
  hdr.record.transaction_id = tid; 
  hdr.record.function_id = fid; 
  hdr.record.args_size = args.size();
  // Copy in msg payload
  hdr.len = args.size() + code.size();
  auto str_ptr = reinterpret_cast<char *>(dp.Data());
  if (args.size() > 0) {
    args.copy(str_ptr, args.size());
    str_ptr += args.size();
  }
  if (code.size() > 0) {
    code.copy(str_ptr, code.size());
    str_ptr += code.size();
  }
  SendMessage(nid, std::move(buf));
}

void seuss::SeussChannel::ReceiveMessage(ebbrt::Messenger::NetworkId nid,
                    std::unique_ptr<ebbrt::IOBuf> &&buf){
  uint8_t *msg_buf;
  std::string args;
  std::string code;
	auto buf_len = buf->ComputeChainDataLength();

  // check the header to ditermine the message type
  kassert(buf->Length() >= sizeof(MsgHeader));
  auto dp = buf->GetDataPointer();
  auto& hdr = dp.Get<MsgHeader>();

  // Extract the message payload(s)
  if (hdr.len > 0) {
    // If we have a chained IO buf allocate a continous buffer and copy in msg
    if (buf->IsChained()) {
      msg_buf = (uint8_t *)malloc(buf_len);
      dp.Get(buf_len, msg_buf);
    } else {
      msg_buf = const_cast<uint8_t *>(dp.Data());
    }
    kassert(hdr.len >= hdr.record.args_size);
    // extract the activation arguments
    if (hdr.record.args_size) {
      args.assign(reinterpret_cast<const char *>(msg_buf),
                  hdr.record.args_size);
    }
    // Any additional payload data treat as function code
    if (hdr.len > hdr.record.args_size) {
      code.assign(
          reinterpret_cast<const char *>(msg_buf + hdr.record.args_size),
          (hdr.len - hdr.record.args_size));
    }
  }

  // Process the message type
  switch (hdr.type) {
#ifdef __ebbrt__ /* Native (EbbRT) */
  case MsgType::ping:
    kprintf_force("SeussChannel - ping!\n");
    break;
  case MsgType::request:
    /* Call the invoker to spawn the action */
    ebbrt::event_manager->SpawnRemote(
        [hdr, args, code]() {
          seuss::invoker->Invoke(hdr.record.transaction_id,
                                 hdr.record.function_id, args, code);
        },
        (size_t)count_ % ebbrt::Cpu::Count());
    // XXX: Only invoke on a single core (core 0) ..for now!
    //count_++;
    break;
  case MsgType::reply:
    kabort("Received invocation reply on EbbRT (native)!?\n");
    break;
#else /* Hosted (Linux) */
  case MsgType::ping:
    kprintf_force("SeussChannel - pong!\n");
    break;
  case MsgType::request:
    kabort("Received invocation request on Linux !?\n");
    break;
  case MsgType::reply:
    seuss::controller->ResolveActivation(hdr.record, args);
    break;
#endif
  } // end switch(hdr.type)
};

