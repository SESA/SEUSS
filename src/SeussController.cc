//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#include <iostream>
#include <sstream> /* std::ostringstream */

#include "SeussChannel.h"
#include "SeussController.h"

#include <ebbrt/Debug.h>
#include <ebbrt/Future.h>

using namespace std;

void seuss::Init() {
  { // Initialize the controller
    auto rep = new Controller(Controller::global_id);
    Controller::Create(rep, Controller::global_id);
  }
  { // Initialize the channel
    auto rep = new SeussChannel(SeussChannel::global_id);
    SeussChannel::Create(rep, SeussChannel::global_id);
  }
}

seuss::Controller::Controller(ebbrt::EbbId ebbid) {}

void seuss::Controller::RegisterNode(ebbrt::Messenger::NetworkId nid) {

  _nids.push_back(nid);
  int cpu_num = ebbrt::Cpu::GetPhysCpus();
  auto index = (cpu_num - (int)_nids.size()) % cpu_num;
  auto cpu_i = ebbrt::Cpu::GetByIndex(index);
  auto ctxt = cpu_i->get_context();

  cout << "CPU: " << index << " was reserved for " << nid.ToString() << endl;

  _frontEnd_cpus_map.insert(make_pair(nid.ToString(), index));
  ebbrt::event_manager->SpawnRemote(
      [this, nid]() { /*seuss_channel->Ping(nid);*/ }, ctxt);
}

ebbrt::Future<openwhisk::msg::CompletionMessage>
seuss::Controller::ScheduleActivation(
    const openwhisk::msg::ActivationMessage &am, std::string code ) {

  auto start = std::chrono::high_resolution_clock::now();

  // TODO: cache the code locally?
  if(code.empty())
    code = openwhisk::couchdb::get_action(am.action_);

  auto args = am.content_;
  uint64_t tid = am.transid_.id_; // OpenWhisk transaction id (unique)
  auto fid = std::hash<std::string>{}(am.revision_);

  //std::cout << "CONTROLLER: scheduling activation on core #"
  //          << (size_t)ebbrt::Cpu::GetMine() 
           // << ": " << am.to_json()
           // << std::endl
           // << "``` CODE:" << std::endl
           // << code << std::endl
           // << "``` ARGS:" << std::endl
           // << args << std::endl << "```" 
           //<< std::endl;

  /* Capture a record of this Activation */
  ebbrt::Promise<openwhisk::msg::CompletionMessage> promise;
  auto ret = promise.GetFuture();
  auto record = std::make_tuple(std::move(promise), am, start);
  {
    std::lock_guard<std::mutex> guard(m_);
    bool inserted;
    // insert records into the hash tables
    std::tie(std::ignore, inserted) =
        record_map_.emplace(tid, std::move(record));
    // Assert there was no collision on the key
    assert(inserted);
  }

  /* Schedule this activation on a back-end node */
  kassert(!_nids.empty()); // verify we have a backend node
  // XXX: Safety checks, what is the state of the backed?
  // XXX: Always grab the first node from the list (SINGLE BACKEND)
  auto nid = _nids.front();

  /* Send the event via IO thread for this backend node */
  auto nid_io_cpu = _frontEnd_cpus_map[nid.ToString()];
  ebbrt::event_manager->SpawnRemote(
      [this, nid, tid, fid, code, args]() {
        std::cout << "Sending Activation request on core "
                  << (size_t)ebbrt::Cpu::GetMine() << std::endl;
        seuss_channel->SendRequest(nid, tid, fid, args, code);
      },
      ebbrt::Cpu::GetByIndex(nid_io_cpu)->get_context());

  return ret;
}

void seuss::Controller::ResolveActivation(seuss::ActivationRecord ar, std::string res){
  std::ostringstream annotations;
  // Capture the ending time
  auto end_time = std::chrono::high_resolution_clock::now();
  // Lookup activation in the table
  auto tid = ar.transaction_id;
  std::lock_guard<std::mutex> guard(m_);
  auto it = record_map_.find(tid);
  assert(it != record_map_.end());
  auto record_tuple = std::move(it->second);
  openwhisk::msg::CompletionMessage cm(std::get<1>(record_tuple));

  auto start_time = std::get<2>(record_tuple);
  size_t total_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                          end_time - start_time)
                          .count();
  auto wait_time = total_time - ar.stats.run_time - ar.stats.init_time;
  // annotations (waitTime, initTime)
  annotations << R"({"key":"waitTime","value":)" << wait_time << R"(},{"key":"initTime","value":)"<< ar.stats.init_time << R"(})";
  cm.response_.annotations_ = annotations.str();
  cm.response_.duration_ = ar.stats.run_time;
  cm.response_.start_ = 0;
  cm.response_.end_ = 0;
  cm.response_.status_code_ = 0; // alwasy success
  cm.response_.result_ = res;
  std::get<0>(record_tuple).SetValue(cm);
  record_map_.erase(it);
}
