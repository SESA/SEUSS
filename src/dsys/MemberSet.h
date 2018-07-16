//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef DSYS_MEMBERSET_H_
#define DSYS_MEMBERSET_H_

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <ebbrt-zookeeper/ZKGlobalIdMap.h>
#include <ebbrt/EbbRef.h>       /* EbbId */
#include <ebbrt/EventManager.h> /* Spawn */
#include <ebbrt/Message.h>      /* LocalNetworkId */
#include <ebbrt/Messenger.h>    /* LocalNetworkId */

#ifndef __ebbrt__
#include <boost/asio.hpp>
#endif

namespace ebbrt {

  using MemberId = std::string;

/** MemberSet<T> defines a type, T, which uses the ZKGlobalIdMap 
  * to manage global membership list. Action hooks provide reactive
  * operations to changes to the membership list.
  */
template <typename T> class MemberSet : public ebbrt::ZooKeeper::Watcher {
public:

  explicit MemberSet(EbbId id, bool auto_join = false) : ebbid_(id) {
    parent_path_ = tag_;
    if (auto_join)
      Join();
  }
  virtual void MemberSetEventSetCreate() {
    std::cout << "Unhandled MemberSet Event:" << __func__ << std::endl;
  };
  virtual void MemberSetEventSetDelete() {
    std::cout << "Unhandled MemberSet Event:" << __func__ << std::endl;
  };
  virtual void MemberSetEventMemberAdd(MemberId id) {
    std::cout << "Unhandled MemberSet Event:" << __func__ << std::endl;
  };
  virtual void MemberSetEventMemberDelete(MemberId id) {
    std::cout << "Unhandled MemberSet Event:" << __func__ << std::endl;
  };

  void WatchHandler(int type, int state, const char *path) override {
    switch (static_cast<ZooKeeper::Event>(type)) {
    case ZooKeeper::Event::Create:
      ebbrt::event_manager->Spawn(
          [this]() { this->MemberSetEventSetCreate(); });
      break;
    case ZooKeeper::Event::Delete:
      ebbrt::event_manager->Spawn(
          [this]() { this->MemberSetEventSetDelete(); });
      break;
    case ZooKeeper::Event::ChildChange:
      ebbrt::event_manager->Spawn([this]() {
        ZKGlobalIdMap::ZKOptArgs args;
        args.path = tag_;
        ebbrt::zkglobal_id_map->List(ebbid_, args).Then([this](auto vec) {
          auto latest_members = vec.Get();
          std::vector<std::string> diff;
          if (latest_members.size() > member_id_cache_.size()) {
            /* add members */
            std::set_difference(latest_members.begin(), latest_members.end(),
                                member_id_cache_.begin(),
                                member_id_cache_.end(),
                                std::inserter(diff, diff.begin()));
            for (auto i : diff) {
              member_id_cache_.push_back(i);
              ebbrt::event_manager->Spawn(
                  [this, i]() { this->MemberSetEventMemberAdd(i); });
            }
          } else if (latest_members.size() < member_id_cache_.size()) {
            /* delete members */
            std::set_difference(member_id_cache_.begin(),
                                member_id_cache_.end(), latest_members.begin(),
                                latest_members.end(),
                                std::inserter(diff, diff.begin()));
            for (auto i : diff) {
              member_id_cache_.erase(std::remove(member_id_cache_.begin(),
                                                 member_id_cache_.end(), i),
                                     member_id_cache_.end());
              ebbrt::event_manager->Spawn(
                  [this, i]() { this->MemberSetEventMemberDelete(i); });
            }
          } else if (latest_members.size() > 0 || member_id_cache_.size() > 0) {
            std::cout << "MemberSet: warning - event for unchanged set sizes"
                      << " new: " << latest_members.size()
                      << " old: " << member_id_cache_.size() << std::endl;
          }
        });
      });
      break;
    case ZooKeeper::Event::Change:
    case ZooKeeper::Event::Session:
    case ZooKeeper::Event::Error:
    default:
      std::cout << "MemberSet Event #" << type << ": " << path << std::endl;
    }
    ebbrt::zkglobal_id_map->SetWatcher(ebbid_, this, tag_);
  }

  void Init() {
    /* initialize the set record */
    std::cout << "MemSet Init called:" << ebbid_ << std::endl;
    ZKGlobalIdMap::ZKOptArgs args;
    args.path = tag_;
    ebbrt::zkglobal_id_map->Exists(ebbid_, args).Then([this, args](auto b) {
      if (b.Get() == false) {
        ebbrt::zkglobal_id_map->Set(ebbid_, args);
      }
      ebbrt::zkglobal_id_map->SetWatcher(ebbid_, this, tag_);
      initialized_ = true;
    });
  }

  void Join() {
    /* join the set */
    mid_ = messenger->LocalNetworkId().ToString();
    //std::cout << "memberset join" << mid_ << std::endl;
    ZKGlobalIdMap::ZKOptArgs args;
    args.path = member_path_from_id_(mid_);
    args.data = messenger->LocalNetworkId().ToBytes();;
    args.flags = ZooKeeper::Flag::Ephemeral;
    auto f = ebbrt::zkglobal_id_map->Set(ebbid_, args);
  }

  std::vector<MemberId> GetMemberList() {
    /* return memberlist */
    return member_id_cache_;
  }
  MemberId GetMemberId() {
    /* return member id */
    return mid_;
  }

private:
  void add_member(MemberId mid) {}
  void delete_member(MemberId mid) {}
  bool confirm_member(MemberId mid) { return false; }

  std::string member_path_from_id_(MemberId mid) {
    std::stringstream ss;
    ss << parent_path_ << "/" << mid;
    return ss.str();
  }
  EbbId ebbid_;
  bool initialized_ = false;
  bool joined_ = false;
  const std::string tag_ = "members";
  std::string parent_path_; // znode parent path
  std::vector<MemberId> member_id_cache_;
  MemberId mid_; // membership id
};

} // namespace ebbrt
#endif // DSYS_MEMBERSET_H_
