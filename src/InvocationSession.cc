//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#include <iostream>

#include "SeussInvoker.h"

#define kprintf ebbrt::kprintf
using ebbrt::kprintf_force;

/* class seuss::InvocationSession */

void seuss::InvocationSession::Connected() {
    // We've established a connection with the instance
    is_connected_ = true;
    // Trigger 'WhenConnected().Then()' logic on a new event context
    ebbrt::event_manager->SpawnLocal([this]() { when_connected_.SetValue(); });
}

void seuss::InvocationSession::Close() {
  kprintf("InvocationSession closed!\n");
  is_connected_ = false;
  
    Pcb().Disconnect();
  // Trigger 'WhenClosed().Then()' logic on a new event context
  ebbrt::event_manager->SpawnLocal([this]() {
    when_closed_.SetValue();
  });
}

void seuss::InvocationSession::Abort() {
  kprintf_force("InvocationSession aborted!\n");
  is_connected_ = false;
  // Trigger 'WhenAborted().Then()' logic on a new event context
  ebbrt::event_manager->SpawnLocal([this]() { when_aborted_.SetValue(); });
}

void seuss::InvocationSession::Finish(std::string response) {
  kprintf("InvocationSession finished!\n");
  // Force disconnect of the TCP connection
  seuss::invoker->Resolve(istats_, response);
}

ebbrt::SharedFuture<void> seuss::InvocationSession::WhenConnected(){
  return when_connected_.GetFuture().Share();
}

ebbrt::SharedFuture<void> seuss::InvocationSession::WhenInitialized(){
  return when_initialized_.GetFuture().Share();
}

ebbrt::SharedFuture<void> seuss::InvocationSession::WhenAborted(){
  return when_aborted_.GetFuture().Share();
}

ebbrt::SharedFuture<void> seuss::InvocationSession::WhenClosed(){
  return when_closed_.GetFuture().Share();
}

ebbrt::SharedFuture<std::string> seuss::InvocationSession::WhenFinished(){
  return when_finished_.GetFuture().Share();
}

void seuss::InvocationSession::Receive(std::unique_ptr<ebbrt::MutIOBuf> b) {
  size_t response_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                             ebbrt::clock::Wall::Now() - command_clock_)
                             .count();

  //TODO: support chain and incomplete buffers (i.e., large replies)
  kassert(!b->IsChained());

  /* construct a string of the response message */
  std::string reply(reinterpret_cast<const char *>(b->Data()), b->Length());
  std::string response = reply.substr(reply.find_first_of("{"));
  std::string http_status = reply.substr(0, reply.find_first_of("\r"));

  /* Verify if the http request was successful */
  if(http_status != "HTTP/1.1 200 OK"){
    istats_.exec.status = 1; /* INVOCATION FAILED */
    kprintf_force(RED "INVOCATION FAILED :( %s\n", response.c_str());
    Finish(response);
  }
  /* An {"OK":true} response signals a completed INIT */
  if (response == R"({"OK":true})" && !is_initialized_) {
    istats_.exec.init_time = response_time;
    // Trigger 'WhenInitialized().Then()' logic on a new event context
    ebbrt::event_manager->SpawnLocal(
        [this]() {
          is_initialized_ = true;
          when_initialized_.SetValue();
        });
  }
  /* Any other response signals a completed RUN */
  else {
    istats_.exec.run_time = response_time;
    istats_.exec.status = 0; /* INVOCATION SUCCESSFUL */
    Finish(response);
  }
}

void seuss::InvocationSession::SendHttpRequest(std::string path,
                                               std::string payload) {
  kassert(payload.size() > 0);
  std::string msg;
  if( path == "/init")
    msg = http_post_request(path, payload, false);
  else
    msg = http_post_request(path, payload, false);
  auto buf = ebbrt::MakeUniqueIOBuf(msg.size());
  auto dp = buf->GetMutDataPointer();
  auto str_ptr = reinterpret_cast<char *>(dp.Data());
  msg.copy(str_ptr, msg.size());
  command_clock_ = ebbrt::clock::Wall::Now();
#ifndef NDEBUG /// DEBUG OUTPUT
  std::cout << msg << std::endl;
#endif
  Send(std::move(buf));
}

void seuss::InvocationSession::reset_pcb_internal(){
  ebbrt::NetworkManager::TcpPcb *pcb = (ebbrt::NetworkManager::TcpPcb *) &Pcb();
  *pcb = ebbrt::NetworkManager::TcpPcb();
}

std::string seuss::InvocationSession::http_post_request(std::string path,
                                                        std::string msg, bool keep_alive=false) {
  std::ostringstream payload;
  std::ostringstream ret;

  // construct json payload formatted for OpenWhisk ActonRunner
  // TODO: avoid locking operation
  msg.erase(std::remove(msg.begin(), msg.end(), '\n'), msg.end());
  payload << "{\"value\": ";
  if (path == "/init") {
    payload << "{\"main\":\"main\", \"code\":\"" << msg << "\"}}";
  } else {
    payload << msg << "}";
  }

  // build http message header + body
  auto body = payload.str();
  // TODO: avoid locking operation
  ret << "POST " << path << " HTTP/1.0\r\n"
      << "Content-Type: application/json\r\n";
  if (keep_alive)
    ret << "Connection: keep-alive\r\n";
  ret << "content-length: " << body.size() << "\r\n\r\n"
      << body;
  return ret.str();
}

