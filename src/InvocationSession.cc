//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#include <iostream>

#include "SeussInvoker.h"

#define kprintf ebbrt::kprintf
using ebbrt::kprintf_force;

/* seuss::InvocationSession */

void seuss::InvocationSession::Connect() {
  // Kick off the connection with the UMI 
  ebbrt::kbugon(is_connected_); // Or maybe just return?
  ebbrt::kbugon(!src_port_);
  Pcb().Connect(umm::UmInstance::CoreLocalIp(), 8080, src_port_);
  auto now = ebbrt::clock::Wall::Now();
  timeout_ = now + std::chrono::milliseconds(5000); // 5000ms
  enable_timer(now);
}

void seuss::InvocationSession::Connected() {
  // We've established a connection with the instance
  //kprintf(GREEN "SESSION ESTABLISHED (%u)\n" RESET, src_port_);
  is_connected_ = true;
  disable_timer();
  auto now = ebbrt::clock::Wall::Now();
  timeout_ = now + std::chrono::seconds(60); // 60 seconds to finish invocation
  enable_timer(now);
  ebbrt::event_manager->SpawnLocal([this]() { when_connected_.SetValue(); });
}

void seuss::InvocationSession::Close() {
  //kprintf(YELLOW "SESSION CLOSED (%u)\n" RESET, src_port_);
  disable_timer();
  is_connected_ = false;
  Pcb().Disconnect();
  ebbrt::event_manager->SpawnLocal([this]() { when_closed_.SetValue(); });
}

void seuss::InvocationSession::Abort() {
  disable_timer();
  is_connected_ = false;
  ebbrt::event_manager->SpawnLocal([this]() { when_aborted_.SetValue(); });
}

void seuss::InvocationSession::Finish(bool status) {
  disable_timer();
  when_finished_.SetValue(status);
}

void seuss::InvocationSession::Receive(std::unique_ptr<ebbrt::MutIOBuf> b) {
  ebbrt::kbugon(b->IsChained());

  /* construct a string of the response message */
  std::string reply(reinterpret_cast<const char *>(b->Data()), b->Length());
  std::string response = reply.substr(reply.find_first_of("{"));
  std::string http_status = reply.substr(0, reply.find_first_of("\r"));

  /* Verify if the http request was successful */
  if (http_status != "HTTP/1.1 200 OK") {
    kprintf_force(RED "REQUEST FAILED!\n REQUEST: %s\nRESPONSE: %s\n",
                  previous_request_.c_str(), response.c_str());
    reply_ = response;
    when_aborted_.SetValue();
    // XXX: Not sure about this disconnect
    Pcb().Disconnect();
  }
  /* An {"OK":true} response signals a completed INIT */
  if (response == R"({"OK":true})" && !is_initialized_) {
    is_initialized_ = true;
    when_initialized_.SetValue();
  } else {
    /* Any other response signals a completed RUN */
    reply_ = response;
    when_executed_.SetValue();
  }
}

void seuss::InvocationSession::enable_timer(ebbrt::clock::Wall::time_point now) {
  if (timer_set || (now >= timeout_)) {
    return;
  }
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(timeout_ - now);
  ebbrt::timer->Start(*this, duration, /* repeat = */ false);
  timer_set = true;
}

void seuss::InvocationSession::disable_timer() {
  if (timer_set) {
    ebbrt::timer->Stop(*this);
  }
  timer_set = false;
  timeout_ = ebbrt::clock::Wall::time_point(); // clear timer
}

void seuss::InvocationSession::Fire() {
  if (timer_set) {
    kprintf_force(RED "\nC%d: InvocationSession Timed Out\n" RESET,
                  (size_t)ebbrt::Cpu::GetMine());
  }
  timer_set = false;
  // Confirm time is valid and has expired  
  auto now = ebbrt::clock::Wall::Now();
  if (timeout_ != ebbrt::clock::Wall::time_point() && now >= timeout_) {
    timeout_ = ebbrt::clock::Wall::time_point(); // clear the time
    // Abort the connection, causing the InvocationSession to fail
    Abort();
  }
}

void seuss::InvocationSession::SendHttpRequest(std::string path,
                                               std::string payload, bool keep_alive) {
  kassert(payload.size() > 0);
  std::string msg;
  msg = http_post_request(path, payload, keep_alive);
  auto buf = ebbrt::MakeUniqueIOBuf(msg.size());
  auto dp = buf->GetMutDataPointer();
  auto str_ptr = reinterpret_cast<char *>(dp.Data());
  msg.copy(str_ptr, msg.size());

  if (path == "/init") {
    init_start_time_ = ebbrt::clock::Wall::Now();
  } else if (path == "/run") {
    run_start_time_ = ebbrt::clock::Wall::Now();
  }

  Send(std::move(buf));
}

void seuss::InvocationSession::reset_pcb_internal(){
  //HACK: overwrite the old Pcb with a new one, destoying the old one
  ebbrt::NetworkManager::TcpPcb *pcb = (ebbrt::NetworkManager::TcpPcb *) &Pcb();
  *pcb = ebbrt::NetworkManager::TcpPcb();
  Install();
}

std::string
seuss::InvocationSession::http_post_request(std::string path, std::string msg,
                                            bool keep_alive = false) {
  // construct json payload formatted for the OpenWhisk ActonRunner
  std::ostringstream payload;
  std::ostringstream ret;

  msg.erase(std::remove(msg.begin(), msg.end(), '\n'), msg.end());
  payload << "{\"value\": ";
  if (path == "/init" || path == "/preInit") {
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
  ret << "content-length: " << body.size() << "\r\n\r\n" << body;
  std::string return_val = ret.str();
  previous_request_ = return_val;
  return return_val;
}

/* Future-based Lambda Handlers */

ebbrt::SharedFuture<void> seuss::InvocationSession::WhenAborted(){
  return when_aborted_.GetFuture().Share();
}

ebbrt::SharedFuture<void> seuss::InvocationSession::WhenConnected(){
  return when_connected_.GetFuture().Share();
}

ebbrt::SharedFuture<void> seuss::InvocationSession::WhenClosed(){
  return when_closed_.GetFuture().Share();
}

ebbrt::SharedFuture<void> seuss::InvocationSession::WhenExecuted(){
  return when_executed_.GetFuture().Share();
}

ebbrt::SharedFuture<bool> seuss::InvocationSession::WhenFinished(){
  return when_finished_.GetFuture().Share();
}

ebbrt::SharedFuture<void> seuss::InvocationSession::WhenInitialized(){
  return when_initialized_.GetFuture().Share();
}


