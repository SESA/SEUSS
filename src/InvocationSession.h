//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef SEUSS_INVOCATION_SESSION_H
#define SEUSS_INVOCATION_SESSION_H

#include <ebbrt/Future.h>
#include <ebbrt/native/NetTcpHandler.h>

#include "Seuss.h"

namespace seuss {

class InvocationSession : public ebbrt::TcpHandler {
public:
  InvocationSession(ebbrt::NetworkManager::TcpPcb pcb, InvocationStats istats, uint16_t src_port )
      : ebbrt::TcpHandler(std::move(pcb)), istats_(istats), src_port_(src_port) { 
    Install();  // Install PCB to TcpHandler
    Pcb().BindCpu((size_t)ebbrt::Cpu::GetMine()); // Bind connection to *this* core
  }

  /* InvocationSession Methods */

  /* Setup a new session connection */
  void Connect();

// HACK!
  void Reconnect(uint16_t src_port) {
    Pcb().Connect(umm::UmInstance::CoreLocalIp(), 8080, src_port);
  }

  /* Sends an openwhisk NodeJsAction HTTP request */ 
  void SendHttpRequest(std::string path, std::string payload, bool keep_alive=false);


  /* Finished the invocation session, pass reponse to Invoker */ 
  void Finish(std::string Response);

  void reset_pcb_internal();

  /* ebbrt::Future-based lambda handlers */
  ebbrt::SharedFuture<void> WhenClosed();
  ebbrt::SharedFuture<void> WhenAborted();
  ebbrt::SharedFuture<void> WhenConnected();
  ebbrt::SharedFuture<void> WhenInitialized();
  ebbrt::SharedFuture<void> WhenFinished();

  /* ebbrt::TcpHandler Callbacks */

  /* Callback when the connection is aborted */
  void Abort();

  /* Callback when the connection is closed */ 
  void Close();

  /* Callback when the connection is established */ 
  void Connected();

  /* Callback for when data is received on the connection */ 
  void Receive(std::unique_ptr<ebbrt::MutIOBuf> b);

private:
  /* event hooks */
  ebbrt::Promise<void> when_aborted_;
  ebbrt::Promise<void> when_closed_;
  ebbrt::Promise<void> when_connected_;
  ebbrt::Promise<void> when_initialized_;
  ebbrt::Promise<void> when_finished_; 
  /* session members */
  bool is_connected_{false};
  bool is_initialized_{false};
  InvocationStats istats_;
  uint16_t src_port_{0}; // dedicated sender port
  ebbrt::clock::Wall::time_point command_clock_;
  /* helper methods */
  std::string http_post_request(std::string path, std::string payload, bool keep_alive);
  std::string previous_request_;
}; // end class InvocationSession
} // end namespace seuss
#endif 
