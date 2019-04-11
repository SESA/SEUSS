//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef SEUSS_INVOCATION_SESSION_H
#define SEUSS_INVOCATION_SESSION_H

#include <ebbrt/Future.h>
#include <ebbrt/native/NetTcpHandler.h>
#include <ebbrt/Timer.h>

#include "Seuss.h"

namespace seuss {

class InvocationSession : public ebbrt::TcpHandler, public ebbrt::Timer::Hook {
public:
  InvocationSession(ebbrt::NetworkManager::TcpPcb pcb, uint16_t src_port )
      : ebbrt::TcpHandler(std::move(pcb)), src_port_(src_port) { 
    Install();  // Install PCB to TcpHandler
    Pcb().BindCpu((size_t)ebbrt::Cpu::GetMine()); // Bind connection to *this* core
  }

  /* InvocationSession Methods */

  /* Setup a new session connection */
  void Connect();

  /** Timeout event handler */
  void Fire() override;

// HACK!
#if 0
  void Reconnect(uint16_t src_port) {
    Pcb().Connect(umm::UmInstance::CoreLocalIp(), 8080, src_port);
  }
#endif

  /* Sends an openwhisk NodeJsAction HTTP request */ 
  void SendHttpRequest(std::string path, std::string payload, bool keep_alive=false);

  /* Signal that the invocation has finished; true=success, false=failure*/
  void Finish(bool);

  void reset_pcb_internal();

  /* ebbrt::Future-based lambda handlers */
  ebbrt::SharedFuture<void> WhenAborted();
  ebbrt::SharedFuture<void> WhenClosed();
  ebbrt::SharedFuture<void> WhenConnected();
  ebbrt::SharedFuture<void> WhenExecuted();
  ebbrt::SharedFuture<bool> WhenFinished(); // returns status (bool)
  ebbrt::SharedFuture<void> WhenInitialized();

  /* ebbrt::TcpHandler Callbacks */

  /* Callback when the connection is aborted */
  void Abort();

  /* Callback when the connection is closed */ 
  void Close();

  /* Callback when the connection is established */ 
  void Connected();

  /* Callback for when data is received on the connection */ 
  void Receive(std::unique_ptr<ebbrt::MutIOBuf> b);

  /* Get the reply string from the previous request */
  std::string GetReply() { return reply_; }

  /* Return the sender port of the connection */
  uint16_t SrcPort(){ return src_port_; }

  size_t  get_runtime() {
    auto tp =  run_start_time_;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               ebbrt::clock::Wall::Now() - tp)
        .count();
  }

  size_t get_inittime() {
    auto tp = init_start_time_;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               ebbrt::clock::Wall::Now() - tp)
        .count();
  }

private:
  /* event hooks */
  ebbrt::Promise<void> when_aborted_;
  ebbrt::Promise<void> when_closed_;
  ebbrt::Promise<void> when_connected_;
  ebbrt::Promise<void> when_executed_;
  ebbrt::Promise<void> when_initialized_;
  ebbrt::Promise<bool> when_finished_; 
  /* session members */
  bool is_connected_{false};
  bool is_initialized_{false};
  uint16_t src_port_{0}; // dedicated sender port
  /* time */
  void enable_timer(ebbrt::clock::Wall::time_point now);
  void disable_timer(); 
  bool timer_set = false;
  ebbrt::clock::Wall::time_point timeout_; // TIMEOUT
  ebbrt::clock::Wall::time_point init_start_time_;
  ebbrt::clock::Wall::time_point run_start_time_;
  /* helper methods */
  std::string http_post_request(std::string path, std::string payload, bool keep_alive);
  std::string previous_request_;
  std::string reply_;
}; // end class InvocationSession
} // end namespace seuss
#endif 
