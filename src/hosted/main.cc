//          Copyright Boston University SESA Group 2013 - 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <cinttypes> // PRId64, etc
#include <csignal>
#include <iostream>
#include <stdlib.h> // exit
#include <thread> 
#include <chrono>  

using std::string;
using std::cout;
using std::endl;

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
namespace po = boost::program_options;
namespace fs = boost::filesystem;

#include "cppkafka/utils/buffered_producer.h"
#include "cppkafka/consumer.h"
#include "cppkafka/configuration.h"
#include "cppkafka/producer.h"
#include "cppkafka/configuration.h"
#include "cppkafka/metadata.h"
#include "cppkafka/topic.h"

using cppkafka::BrokerMetadata;
using cppkafka::BufferedProducer;
using cppkafka::Configuration;
using cppkafka::Exception;
using cppkafka::GroupInformation;
using cppkafka::GroupMemberInformation;
using cppkafka::MemberAssignmentInformation;
using cppkafka::Message;
using cppkafka::MessageBuilder;
using cppkafka::Metadata;
using cppkafka::Producer;
using cppkafka::Topic;
using cppkafka::TopicMetadata;

#include <ebbrt/Cpu.h> // ebbrt::Cpu::EarlyInit
#include "common.h"

string native_binary_path;
uint8_t native_instances=1;
string zookeeper_hosts;

namespace { // local
static char* ExecName = 0;
string brokers;
string topic_name;
string couchdb;
}

/** Boost Program Options */
po::options_description ebbrt_po(){
  po::options_description options("EbbRT configuration");
  options.add_options()("natives,n", po::value<uint8_t>(&native_instances), "Native instances");
  options.add_options()("elf32,b", po::value<string>(&native_binary_path), "Native binary");
  options.add_options()("zookeeper,z", po::value<string>(&zookeeper_hosts), "Zookeeper Hosts");
  return options;
}

po::options_description kafka_po(){
    po::options_description options("Kafka");
    options.add_options()
        ("kafka-brokers,k",  po::value<string>(&brokers), 
                       "the kafka broker list")
        ("kafka-topic,t",    po::value<string>(&topic_name),
                       "the topic in which to write to")
        ;
  return options;
}

po::options_description couchdb_po(){
    po::options_description options("CouchDB");
    options.add_options()
        ("couchdb-server,cdb",  po::value<string>(&couchdb), 
                       "CouchDB server");
  return options;
}

bool  kafka_process_po(po::variables_map &vm) {
  if (vm.count("kafka-brokers")) {
    std::cout << "Kafka Hosts: " << vm["kafka-brokers"].as<string>() << std::endl;
  }
  if (vm.count("kafka-topic")) {
    std::cout << "Kafka Topic: " << vm["kafka-topic"].as<string>() << std::endl;
  }
  return true;
}

bool  ebbrt_process_po(po::variables_map &vm) {
	bool spawn = false;
  if (vm.count("natives")) {
    std::cout << "Native instances to spawn: " << vm["natives"].as<uint8_t>() << std::endl;
  }
  if (vm.count("elf32")) {
    std::cout << "Native binary to boot: " << vm["elf32"].as<string>() << std::endl;
    auto bindir = fs::system_complete(ExecName).parent_path() /
                  vm["elf32"].as<string>();
		native_binary_path = bindir.string();
		spawn = true;
  }
  if (vm.count("zookeeper")) {
    std::cout << "Zookeeper Hosts: " << vm["zookeeper"].as<string>() << std::endl;
  }
  return spawn;
}

void kafka_test() {
		cout << "Running the Kafka test..." << endl;
    // Stop processing on SIGINT
    signal(SIGINT, [](int) { exit(0); });

    // Construct the configuration
    Configuration config = {
        { "metadata.broker.list", brokers }
    };

#if 0
    BufferedProducer<string> producer(config);
    // Set a produce success callback
    producer.set_produce_success_callback([](const Message& msg) {
        cout << "Successfully produced message with payload " << msg.get_payload() << endl;
    });
    // Set a produce failure callback
    producer.set_produce_failure_callback([](const Message& msg) {
        cout << "Failed to produce message with payload " << msg.get_payload() << endl;
        // Return false so we stop trying to produce this message
        return false;
    });
#endif

    // Create the producer
    Producer producer(config);
    Metadata metadata = producer.get_metadata();

    cout << "Found the following brokers: " << endl;
    for (const BrokerMetadata& broker : metadata.get_brokers()) {
        cout << "* " << broker.get_host() << endl;
    }
    cout << endl;
    cout << "Found the following topics: " << endl;
    for (const TopicMetadata& topic : metadata.get_topics()) {
        cout << "* " << topic.get_name() << ": " << topic.get_partitions().size()
             << " partitions" << endl;
    }

    cout << "Entering ping loop..." << endl;
  while(1){
    // Create a message builder for this topic
    MessageBuilder builder("health");
    auto pl = std::string("{\"instance\":\"0\",\"name\":\"seuss_invoker0\"}");
    builder.payload(pl);
    producer.produce(builder);

    std::this_thread::sleep_for (std::chrono::seconds(1));
  }
  //std::thread hl (kafka_health_loop);
  //hl.join();  

#if 0
    // Now read lines and write them into kafka
    string line;
    while (getline(cin, line)) {
        // Set the payload on this builder
        builder.payload(line);

        // Actually produce the message we've built
        producer.produce(builder);
    }
    // Construct the configuration
    Configuration config = {
        { "metadata.broker.list", brokers },
        { "group.id", group_id },
        // Disable auto commit
        { "enable.auto.commit", false }
    };

    // Create the consumer
    Consumer consumer(config);

    // Print the assigned partitions on assignment
    consumer.set_assignment_callback([](const TopicPartitionList& partitions) {
        cout << "Got assigned: " << partitions << endl;
    });

    // Print the revoked partitions on revocation
    consumer.set_revocation_callback([](const TopicPartitionList& partitions) {
        cout << "Got revoked: " << partitions << endl;
    });

    // Subscribe to the topic
    consumer.subscribe({ topic_name });

    cout << "Consuming messages from topic " << topic_name << endl;

    // Now read lines and write them into kafka
    while (running) {
        // Try to consume a message
        Message msg = consumer.poll();
        if (msg) {
            // If we managed to get a message
            if (msg.get_error()) {
                // Ignore EOF notifications from rdkafka
                if (!msg.is_eof()) {
                    cout << "[+] Received error notification: " << msg.get_error() << endl;
                }
            }
            else {
                // Print the key (if any)
                if (msg.get_key()) {
                    cout << msg.get_key() << " -> ";
                }
                // Print the payload
                cout << msg.get_payload() << endl;
                // Now commit the message
                consumer.commit(msg);
            }
        }
    }
#endif
}


int main(int argc, char **argv) {
  void *status;
  ExecName = argv[0];

  uint32_t port;
  /* process program options */
  po::options_description po("Default configuration");
  po::variables_map povm;
  po.add_options()("help", "Help message"); // Default
  po.add_options()("port,p", po::value<uint32_t>(&port), "Port");
  po.add(ebbrt_po()); // EbbRT program options
  po.add(kafka_po()); // Kafka (cppkafka) program options
  po.add(couchdb_po()); // CouchDB (pillowtalk) program options
  try {
    po::store(po::parse_command_line(argc, argv, po), povm);
    po::notify(povm); // Run all "po::notify" functions
  } catch (std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl << po << std::endl;
    return 1;
  }

  /** display help menu and exit */
  if (povm.count("help")) {
    std::cout << po << std::endl;
    return 0;
  }

  /** deploy ebbrt runtime  & backends */
  if (ebbrt_process_po(povm)) {
    pthread_t tid = ebbrt::Cpu::EarlyInit(1);
    pthread_join(tid, &status);
    return 0;
  }

	/** deploy kafka test */ 
  if (kafka_process_po(povm)) {
    kafka_test();
  }

  return 0;
}
