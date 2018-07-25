#include <iostream>
#include <chrono>
#include <thread>

#include "kafka.h"
#include "msg.h"

#include "cppkafka/configuration.h"
#include "cppkafka/producer.h"
#include "cppkafka/metadata.h"
#include "cppkafka/topic.h"

using cppkafka::BrokerMetadata;
using cppkafka::Configuration;
using cppkafka::Metadata;
using cppkafka::MessageBuilder;
using cppkafka::Producer;
using cppkafka::TopicMetadata;

using std::string;
using std::cout;
using std::endl;

namespace po = boost::program_options;

Producer* kafka_producer;
uint8_t instance_id = 0;

void openwhisk::heartbeat() {
  
  msg::PingMessage ping;
  ping.name_.instance_ = instance_id;
  cout << "Starting 1s heartbeat..." << endl;
  cout << "Ping msg: " << ping.to_json() << endl;
  while (1) {
    // Create a heartbeat message 
    MessageBuilder builder("health");
    auto pl = ping.to_json();
    builder.payload(pl);
    kafka_producer->produce(builder);
    std::this_thread::sleep_for (std::chrono::seconds(1));
  }
}

uint8_t openwhisk::kafka_init(po::variables_map &vm){
  string brokers;
  if (vm.count("kafka-brokers"))
    brokers = vm["kafka-brokers"].as<string>();
  if (vm.count("kafka-topic"))
    instance_id = vm["kafka-topic"].as<uint64_t>();

  std::cout << "Kafka Hosts: " << brokers << std::endl;
  std::cout << "Invoker Id: " << std::to_string(instance_id)  << std::endl;
  
  if( brokers.empty() ){
    return 1;
  }

  // Construct the configuration
  Configuration config = {{"metadata.broker.list", brokers}};
  // Create the producer
  kafka_producer = new Producer(config);

  Metadata metadata = kafka_producer->get_metadata();
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

  /** Start heartbeat thread */
  std::thread t(heartbeat);
  t.detach();
  
  return 0;
}
