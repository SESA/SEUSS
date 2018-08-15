//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef SEUSS_INVOKER_H
#define SEUSS_INVOKER_H

#ifndef __ebbrt__ 
#error THIS IS EBBRT-NATIVE CODE
#endif

#include <ebbrt/Debug.h>
#include <ebbrt/MulticoreEbb.h>
#include <ebbrt/Future.h>
#include <ebbrt/native/NetTcpHandler.h>
#include <ebbrt/native/Net.h>

namespace seuss {

void Init();

class InvocationSession : public ebbrt::TcpHandler {
public:
  InvocationSession(ebbrt::NetworkManager::TcpPcb pcb)
      : ebbrt::TcpHandler(std::move(pcb)) {
    is_connected = set_connected.GetFuture();
  }
  void Close();
  void Connected();
  void Abort();
  void Receive(std::unique_ptr<ebbrt::MutIOBuf> b);
  /* inherited: Send(std::unique_ptr<ebbrt::MutIOBuf> b);*/
  ebbrt::Future<void> is_connected;

private:
  const std::string init_msg = std::string("POST /init HTTP/1.0\r\n"
                                    "Content-Type:application/json\r\n"
                                    "Connection: keep-alive\r\n");
  const std::string run_msg = std::string("POST /run HTTP/1.0\r\n"
                                   "Content-Type:application/json\r\n"
                                   "Connection: keep-alive\r\n");
//                              "content-length: 74\r\n\r\n") + code;
  ebbrt::Promise<void> set_connected;
}; // InvocationSession

/* suess::Invoker - per-core action invocation object */
class Invoker : public ebbrt::MulticoreEbb<Invoker> {
public:
  static const ebbrt::EbbId global_id =
      ebbrt::GenerateStaticEbbId("Invoker");
  Invoker(){};
  void Begin();
  void Invoke();
  // void Connect() 

private:
  InvocationSession* umsesh_;
  std::mutex m_;
  std::unordered_map<uint32_t, ebbrt::Promise<void>> promise_map_;
  uint32_t id_{0};
};

constexpr auto invoker =
    ebbrt::EbbRef<Invoker>(Invoker::global_id);

} // namespace seuss

#endif // SEUSS_INVOKER
