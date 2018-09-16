// #include <ebbrt/native/Perf.h>
#ifndef SEUSS_COUNTER_H
#define SEUSS_COUNTER_H

#include <list>
#include <string>
#include <ebbrt/native/Perf.h>
#include <ebbrt/native/Debug.h>

#define kprintf_force ebbrt::kprintf_force

namespace seuss {

class Counter;

class TimeRecord {
public:
  TimeRecord(Counter *ctr, std::string s);

  std::string s_;
  uint64_t cycles_;
  uint64_t ins_;
  uint64_t ref_cycles_;
  float cyc_per_ins_;
};

class Counter {
public:
  void start_all();
  void stop_all();
  void clear_all();
  void init_ctrs();
  void reset_all();
  void dump_list();
  void add_to_list(TimeRecord &r);
  void print_ctrs();

  ebbrt::perf::PerfCounter cycles =
      ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::cycles);
  ebbrt::perf::PerfCounter ins =
      ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::instructions);
  ebbrt::perf::PerfCounter ref_cycles =
      ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::reference_cycles);
  std::list<TimeRecord> ctr_list;
};

} // end ns seuss
#endif
