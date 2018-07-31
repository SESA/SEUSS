#include <iostream>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <vector>

#include "db.h"
#include "kafka.h"
#include "msg.h"

#include "cppkafka/configuration.h"
#include "cppkafka/consumer.h"
#include "cppkafka/group_information.h"
#include "cppkafka/metadata.h"
#include "cppkafka/producer.h"
#include "cppkafka/topic.h"

using std::exception;
using std::string;
using std::cout;
using std::endl;
using std::vector;

namespace po = boost::program_options;

using cppkafka::BrokerMetadata;
using cppkafka::Configuration;
using cppkafka::Consumer;
using cppkafka::Exception;
using cppkafka::GroupInformation;
using cppkafka::GroupMemberInformation;
using cppkafka::MemberAssignmentInformation;
using cppkafka::Message;
using cppkafka::MessageBuilder;
using cppkafka::Metadata;
using cppkafka::Producer;
using cppkafka::TopicMetadata;
using cppkafka::TopicPartition;
using cppkafka::TopicPartitionList;

void openwhisk::ping_producer_loop(const Configuration &config, uint64_t invoker_id) {

  Producer kafka_producer(config);
  msg::PingMessage ping;
  ping.name_.instance_ = invoker_id;
  cout << "kafka: Sending heartbeat to OpenWhisk at a rate of 1 every " << ping_freq_ms << "ms." << endl;
  cout << "Ping msg: " << ping.to_json() << endl;

  // Optional: dump kafka state
  try {
    Metadata metadata = kafka_producer.get_metadata();
    cout << "Kafka brokers: " << endl;
    for (const BrokerMetadata &broker : metadata.get_brokers()) {
      cout << "* " << broker.get_host() << endl;
    }
    cout << endl;
    cout << "Kafka topics: " << endl;
    for (const TopicMetadata &topic : metadata.get_topics()) {
      cout << "* " << topic.get_name() << ": " << topic.get_partitions().size()
           << " partitions" << endl;
    }

    // Fetch the group information
    vector<GroupInformation> groups = [&]() {
      return kafka_producer.get_consumer_groups();
    }();

    if (groups.empty()) {
      cout << "Found no consumers" << endl;
    }
    cout << "Found the following consumers: " << endl;
    for (const GroupInformation &group : groups) {
      cout << "* \"" << group.get_name() << "\" having the following ("
           << group.get_members().size() << ") members: " << endl;
      for (const GroupMemberInformation &info : group.get_members()) {
        cout << "    - " << info.get_member_id() << " @ "
             << info.get_client_host();
        MemberAssignmentInformation assignment(info.get_member_assignment());
        cout << " has assigned: " << assignment.get_topic_partitions();
        cout << endl;
      }
      cout << endl;
    }
  } catch (const Exception &ex) {
    cout << "Error fetching group information: " << ex.what() << endl;
  }

  while (1) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ping_freq_ms));
    // Create a heartbeat message
  try {
    MessageBuilder builder("health");
    auto pl = ping.to_json();
    builder.payload(pl);
    kafka_producer.produce(builder);
  } catch (const Exception &ex) {
    cout << "Error fetching group information: " << ex.what() << endl;
  }
  }
}

void openwhisk::activation_consumer_loop(const Configuration& config, uint64_t invoker_id ){

  // Create the invoker topic and consumer
  Consumer kafka_consumer(config);
  Producer kafka_producer(config);
  std::string default_topic = "invoker"+std::to_string(invoker_id);
  cout << "kafka: consumer subscribe to:" << default_topic << endl;
  kafka_consumer.subscribe({ default_topic });

  // Stream in Activation messages 
  while (1) {
    // Try to consume a message
    Message msg = kafka_consumer.poll(); // XXX: I assume this blocks..?
    if (msg) {
      cout << "kafka: Activation message received! " << endl;
      // If we managed to get a message
      if (msg.get_error()) {
        // Ignore EOF notifications from rdkafka
        if (!msg.is_eof()) {
          cout << "[+] Received error notification: " << msg.get_error()
               << endl;
        }
      } else {
        // Print the key (if any)
        if (msg.get_key()) {
          cout << "MSG KEY: " << msg.get_key() << " -> ";
        }
        // Print the payload
        std::string amjson = msg.get_payload();
        kafka_consumer.commit(msg);
        msg::ActivationMessage am(amjson); 
        cout << "MSG ORIG: " << amjson << endl;
        cout << "MSG PARS: " << am.to_json() << endl;
        // Create a response
        msg::CompletionMessage cm(am);
        cm.response_.duration_ = 999;
        cm.response_.start_ = 0;
        cm.response_.end_ = 0;
        cm.response_.status_code_ = 0;
        // Pull data from DB
        auto code = db::get_action(am.action_);
        cout << endl << "```" << endl << code << endl << "```" << endl;

        // Send response
        MessageBuilder builder("completed0");
        auto pl = cm.to_json();
        builder.payload(pl);
        cout << "Completed response: " << pl << endl;
        kafka_producer.produce(builder);
      }
    }
  }
}

bool openwhisk::kafka_init(po::variables_map &vm) {
  string brokers;
  uint64_t invoker_id = 0;
  // TODO: make the invoker id option required 
  if (vm.count("kafka-brokers"))
    brokers = vm["kafka-brokers"].as<string>();
  if (vm.count("kafka-topic"))
    invoker_id = vm["kafka-topic"].as<uint64_t>();

  Configuration config = {{"metadata.broker.list", brokers},
                          {"group.id", invoker_id}};

  std::cout << "kafka: hosts " << brokers << std::endl;
  std::cout << "kafka: invoker #" << std::to_string(invoker_id) << std::endl;
  if (brokers.empty() ) {
    std::cerr << "kafka: Error - incomplete configuration " << std::endl;
    return false;
  }

  /** New producer and topic confiuguration */
  Producer kafka_producer(config);
  std::string default_topic = "invoker"+std::to_string(invoker_id);
  cout << "kafka: create new topic: " << default_topic << endl;
  kafka_producer.get_topic(default_topic);

  /** Start prodcucer/consumer loops */
  std::thread t(ping_producer_loop, config, invoker_id);
  t.detach();
  std::thread t1(activation_consumer_loop, config, invoker_id);
  t1.detach();
  return true;
}
