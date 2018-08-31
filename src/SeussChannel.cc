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
EBBRT_PUBLISH_TYPE(, SeussChannel);

using namespace ebbrt;

SeussChannel::SeussChannel(ebbrt::EbbId ebbid)
    : ebbrt::Messagable<SeussChannel>(ebbid) {}

void SeussChannel::SendReply(ebbrt::Messenger::NetworkId nid, uint64_t tid,
                               size_t fid, std::string args) {
  // New IOBuf for the outgoing message
  auto buf =
      MakeUniqueIOBuf(sizeof(SeussMsgHeader) + args.size());
  auto dp = buf->GetMutDataPointer();
  // Complete the message header
  auto &msg_header = dp.Get<SeussMsgHeader>();
  msg_header.type = SeussMsgType::reply;
  msg_header.tid = tid; 
  msg_header.fid = fid; 
  msg_header.args_len = args.size();
  msg_header.code_len = 0;
  auto str_ptr = reinterpret_cast<char *>(dp.Data());
  if (args.size() > 0) {
    args.copy(str_ptr, args.size());
  }
  SendMessage(nid, std::move(buf));
}

void SeussChannel::SendRequest(ebbrt::Messenger::NetworkId nid, uint64_t tid,
                               size_t fid, std::string args, std::string code) {
  // New IOBuf for the outgoing message
  auto buf =
      MakeUniqueIOBuf(sizeof(SeussMsgHeader) + args.size() + code.size());
  auto dp = buf->GetMutDataPointer();
  // Complete the message header
  auto &msg_header = dp.Get<SeussMsgHeader>();
  msg_header.type = SeussMsgType::request;
  msg_header.tid = tid; 
  msg_header.fid = fid; 
  msg_header.args_len = args.size();
  msg_header.code_len = code.size();
  // Copy in the payloads
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

void SeussChannel::ReceiveMessage(ebbrt::Messenger::NetworkId nid,
                    std::unique_ptr<ebbrt::IOBuf> &&buf){
	std::string msg;
  std::string args;
  std::string code;
  uint8_t *msg_buf;
	auto buf_len =buf->ComputeChainDataLength();
	auto segment_len = buf->Length();

  // check the header to ditermine the message type
  kassert(buf_len >= sizeof(SeussMsgHeader));
  auto dp = buf->GetDataPointer();
  auto& msg_header = dp.Get<SeussMsgHeader>();
  switch(msg_header.type){
    case SeussMsgType::ping:
      // TODO: pong?
      kprintf_force("SeussChannel - ping!\n");
      break;
    case SeussMsgType::request:
      // Copy msg into a continous buffer 
      msg_buf = (uint8_t*)malloc(buf_len);
      dp.Get(buf_len, msg_buf);
			msg = std::string(reinterpret_cast<const char *>(msg_buf), buf_len);
			//msg.assign(reinterpret_cast<const char *>(msg_buf), buf_len);
      // extract the payload strings
      if( msg_header.args_len){
        args.assign(reinterpret_cast<const char *>(msg_buf), msg_header.args_len);
      }
      if( msg_header.code_len){
        code.assign(reinterpret_cast<const char *>(msg_buf + msg_header.args_len),
                    msg_header.code_len);
      }
      if (buf_len != segment_len) {
        kprintf_force("MESSAGE MISMATCH #%lu len=%d clen=%d\n", msg_header.tid, segment_len,
                      buf_len);
				std::cout << "MSG:[" << msg  << "]" << std::endl;
				std::cout << "ARGS: [" << args << "]" << "  CODE: [" << code << "]" << std::endl;
      }
#ifdef __ebbrt__
      /* Call the invoker to spawn the action */
      ebbrt::event_manager->SpawnRemote(
          [msg_header, args, code]() {
            seuss::invoker->Invoke(msg_header.tid, msg_header.fid, args, code);
          },
          (size_t)count_ % ebbrt::Cpu::Count());
      // XXX: Only invoke on a single core (core 0) ..for now!
      //count_++;
#else
      kabort("Received invocation request on Linux !?\n");
#endif
      break;
    case SeussMsgType::reply:
      if (msg_header.args_len) {
        args.assign(reinterpret_cast<const char *>(dp.Data()),
                    msg_header.args_len);
      }
#ifdef __ebbrt__
      kabort("Received invocation reply on EbbRT (native)!?\n");
#else
      seuss::controller->ResolveActivation(msg_header.tid, args);
#endif
      break;
  }
};

void SeussChannel::Ping(ebbrt::Messenger::NetworkId nid){
  // Ping msg has no body and all header fields == 0
  auto buf = MakeUniqueIOBuf(sizeof(SeussMsgHeader), true);
  SendMessage(nid, std::move(buf));
};

