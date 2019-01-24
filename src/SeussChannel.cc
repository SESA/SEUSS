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

void seuss::SeussChannel::SendReply(ebbrt::Messenger::NetworkId nid, InvocationStats istats, std::string args) {
#ifdef __ebbrt__ /* Native (EbbRT) */
  if ((size_t)ebbrt::Cpu::GetMine() != io_core) {
    ebbrt::event_manager->SpawnRemote([=]() { SendReply(nid, istats, args); },
                                      io_core);
    return;
  }
#endif
  // New IOBuf for the outgoing message
  auto buf =
      MakeUniqueIOBuf(sizeof(MsgHeader) + args.size());
  auto dp = buf->GetMutDataPointer();
  // Complete the message header
  auto &hdr = dp.Get<MsgHeader>();
  hdr.type = MsgType::reply;
  hdr.record = istats;
  hdr.record.args_size = args.size(); // Is this nescessary? 
  // Copy in msg payload
  hdr.len = args.size();
  auto str_ptr = reinterpret_cast<char *>(dp.Data());
  if (args.size() > 0) {
    args.copy(str_ptr, args.size());
  }
  SendMessage(nid, std::move(buf));
}

void seuss::SeussChannel::SendRequest(ebbrt::Messenger::NetworkId nid, InvocationStats istats, std::string args, std::string code) {
  // New IOBuf for the outgoing message
  auto buf =
      MakeUniqueIOBuf(sizeof(MsgHeader) + args.size() + code.size());
  auto dp = buf->GetMutDataPointer();
  // Complete the message header
  auto &hdr = dp.Get<MsgHeader>();
  hdr.type = MsgType::request;
  hdr.record = istats; 
  // Copy in msg payloads
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
  // Create a new Invocation record
  Invocation i;
  auto buf_len = buf->ComputeChainDataLength();

  // Set the IO core of the messenger
  if (unlikely((size_t)ebbrt::Cpu::GetMine() != io_core)) {
    io_core = (size_t)ebbrt::Cpu::GetMine();
  }

  // check the header to ditermine the message type
  assert(buf_len >= sizeof(MsgHeader));
  auto dp = buf->GetDataPointer();
  auto hdr = dp.Get<MsgHeader>();
  i.info = hdr.record;

  // Extract the message payload(s)
  if (hdr.len > 0) {
    // If we have a chained IO buf allocate a continous buffer and copy in
    // msg
    if (buf->IsChained()) {
      msg_buf = (uint8_t *)malloc(buf_len);
      dp.Get(buf_len, msg_buf);
    } else {
      msg_buf = const_cast<uint8_t *>(dp.Data());
    }
    kassert(hdr.len >= hdr.record.args_size);
    // extract the activation arguments
    if (hdr.record.args_size) {
      i.args.assign(reinterpret_cast<const char *>(msg_buf),
                  hdr.record.args_size);
    }
    // Any additional payload data treat as function code
    if (hdr.len > hdr.record.args_size) {
      i.code.assign(
          reinterpret_cast<const char *>(msg_buf + hdr.record.args_size),
          (hdr.len - hdr.record.args_size));
    }
  }

  // Process the message type
  switch (hdr.type) {
#ifdef __ebbrt__ /* Native (EbbRT) */
  case MsgType::ping:
    kprintf("SeussChannel - Ping!\n");
    break;
  case MsgType::request:
    /* Call the invoker to spawn the action */
    seuss::invoker->Queue(i);
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
    seuss::controller->ResolveActivation(hdr.record, i.args);
    break;
#endif
  } // end switch(hdr.type)
};

