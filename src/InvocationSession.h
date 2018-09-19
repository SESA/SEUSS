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
  InvocationSession(ebbrt::NetworkManager::TcpPcb pcb, InvocationStats istats)
      : ebbrt::TcpHandler(std::move(pcb)), istats_(istats) { Install(); }

  /* Default handler for when the TCP connection is aborted */ 
  void Abort();

  /* Default handler for when the TCP connection is closed */ 
  void Close();

  /* Default handler call for when the TCP connection is established */ 
  void Connected();

  /* Sends an openwhisk NodeJsAction HTTP request */ 
  void SendHttpRequest(std::string path, std::string payload, bool keep_alive=false);

  /* Handler for receiving data on the TCP connection */ 
  void Receive(std::unique_ptr<ebbrt::MutIOBuf> b);

  /* Finished the invocation session, pass reponse to Invoker */ 
  void Finish(std::string Response);

  void reset_pcb_internal();
  /* Asyncronous hooks for event handlers */
  ebbrt::SharedFuture<void> WhenClosed();
  ebbrt::SharedFuture<void> WhenAborted();
  ebbrt::SharedFuture<void> WhenConnected();
  ebbrt::SharedFuture<void> WhenInitialized();
  ebbrt::SharedFuture<std::string> WhenFinished();
  InvocationStats GetStats(){ return istats_;}

private:
  /* event hooks */
  ebbrt::Promise<void> when_aborted_;
  ebbrt::Promise<void> when_closed_;
  ebbrt::Promise<void> when_connected_;
  ebbrt::Promise<void> when_initialized_;
  ebbrt::Promise<std::string> when_finished_; // Not sure about this
  /* session members */
  bool is_connected_{false};
  bool is_initialized_{false};
  InvocationStats istats_;
  ebbrt::clock::Wall::time_point command_clock_;
  /* helper methods */
  std::string http_post_request(std::string path, std::string payload, bool keep_alive);
}; // end class InvocationSession
} // end namespace seuss
#endif 
