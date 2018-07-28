// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
#ifndef SEUSS_OPENWHISK_MSG_H_
#define SEUSS_OPENWHISK_MSG_H_

namespace openwhisk {
namespace msg {

struct TransactionId {
public:
  std::string name_;
  uint64_t id_;
  std::string to_json() const {
    return "[\"" + name_ + "\"," + std::to_string(id_) + "]";
  };
};

struct InstanceId {
  long long instance_;
  std::string name_;
  std::string to_json() const {
    if (name_.empty())
      return "{\"instance\":" + std::to_string(instance_) + "}";
    else
      return "{\"instance\":" + std::to_string(instance_) + ",\"name\":\"" +
             name_ + "\"}";
  };
};

struct Action {
  /* TODO: rights, limits */
  std::string path_;
  std::string name_;
  std::string version_;
  std::string to_json() const {
    return "{\"path\":\"" + path_ + "\",\"name\":\"" + name_ +
           "\",\"version\":\"" + version_ + "\"}";
  };
};

struct User {
  std::string subject_;
  std::string authkey_;
  std::string namespace_;
  std::string to_json() const {
    return "{\"subject\":\"" + subject_ + "\",\"authkey\":\"" + authkey_ +
           "\",\"namespace\":\"" + namespace_ + "\"}";
  };
};

/** Classes */

class PingMessage {
public:
  InstanceId name_;
  std::string to_json() const { return "{\"name\":" + name_.to_json() + "}"; };
};

class ActivationMessage {
public:
  ActivationMessage(){};
  explicit ActivationMessage(std::string json_input);
  TransactionId transid_;
  InstanceId rootControllerIndex_;
  std::string activationId_;
  std::string revision_;
  Action action_;
  User user_;
  bool blocking_;
  std::string to_json() const {
    return "{\"rootControllerIndex\":" + rootControllerIndex_.to_json() +
           ",\"activationId\":\"" + activationId_ + "\",\"revision\":\"" +
           revision_ + "\",\"transid\":" + transid_.to_json() + ",\"action\":" +
           action_.to_json() + ",\"blocking\":" + std::to_string(blocking_) +
           ",\"user\":" + user_.to_json() + "}";
  };
};

class Response {
public:
  /* TODO: logs[], annotations [{},] */
  /* metadata */
  std::string activationId_;
  std::string name_;
  std::string namespace_;
  std::string publish_ = "false";
  std::string subject_;
  std::string version_;
  /* execution data */
  long long duration_;
  long long start_;
  long long end_;
  long long status_code_;
  std::string to_json() const {
    return "{\"duration\":" + std::to_string(duration_) + ",\"name\":\"" +
           name_ + "\"" + ",\"subject\":\"" + subject_ + "\"" +
           ",\"activationId\":\"" + activationId_ + "\"" + ",\"publish\":" +
           publish_ + ",\"version\":\"" + version_ + "\"" +
           ",\"end\":" + std::to_string(end_) + ",\"start\":" +
           std::to_string(start_) + ",\"namespace\":\"" + namespace_ + "\"" + ",\"response\":{\"statusCode\":" +std::to_string(status_code_) +
           "},\"logs\":[],\"annotations\":[]}";
  };
};

class CompletionMessage {
public:
  CompletionMessage(){}
  explicit CompletionMessage(ActivationMessage);
  TransactionId transid_;
  Response response_;
  InstanceId invoker_;
  std::string to_json() const {
    return "{\"transid\":" + transid_.to_json() + ",\"response\":" +
           response_.to_json() + ",\"invoker\":" + invoker_.to_json() + "}";
  };
};

} // end namespace msg
} // end namespace openwhisk
#endif // SEUSS_OPENWHISK_MSG_H_
