// #include <ebbrt/native/Perf.h>
#ifndef SEUSS_COUNTER_H
#define SEUSS_COUNTER_H

#include <ebbrt/native/Debug.h>
#include <ebbrt/native/Perf.h>
#include <list>
#include <string>
#define CYAN "\033[36m" /* Cyan */
#define RESET "\033[0m"

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
  Counter() { init_ctrs(); }
  // void start_all();
  // void stop_all();
  // void clear_all();
  // void init_ctrs();
  // void reset_all();
  // void dump_list();
  // void add_to_list(TimeRecord &r);
  // void print_ctrs();
  // void clear_list();

  void start_all() {
    cycles.Start();
    ins.Start();
    ref_cycles.Start();
  }
  void stop_all() {
    cycles.Stop();
    ins.Stop();
    ref_cycles.Stop();
  }
  void clear_all() {
    cycles.Clear();
    ins.Clear();
    ref_cycles.Clear();
  }
  void init_ctrs() {
    cycles = ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::cycles);
    ins = ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::instructions);
    ref_cycles =
        ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::reference_cycles);

    stop_all();
    clear_all();
    start_all();
  }
  void reset_all() {
    stop_all();
    clear_all();
  }
  void dump_list() {
    uint64_t cycles_total = 0;
    uint64_t ins_total = 0;
    uint64_t ref_total = 0;

    for (const auto &e : ctr_list) {
      cycles_total += e.cycles_;
      ins_total += e.ins_;
      ref_total += e.ref_cycles_;
    }

    for (const auto &e : ctr_list) {
      ebbrt::kprintf_force(CYAN "%10s:\tCyc:%6llu\tIns:%6llu\tRef:%6llu\tIns/"
                                "Cyc:%6.3f\tCyc\%:%3.3f%\n" RESET,
                           e.s_.c_str(), e.cycles_ / 100000, e.ins_ / 100000,
                           e.ref_cycles_ / 10000,
                           (float)e.ins_ / (float)e.cycles_,
                           100 * (float)e.cycles_ / (float)cycles_total);
    }
    printf(RESET "totals:" CYAN "\t\t\t %lu \t\t%lu \t\t%lu \n" RESET,
           cycles_total / 100000, ins_total / 100000, ref_total / 10000);
  }

  void add_to_list(TimeRecord &r) {
    // Subract old from current.
    // ebbrt::kprintf_force("clock: %llu, old: %llu \n", cycles.Read(),
    // r.cycles_);
    r.cycles_ = cycles.Read() - r.cycles_;
    r.ins_ = ins.Read() - r.ins_;
    r.ref_cycles_ = ref_cycles.Read() - r.ref_cycles_;
    ctr_list.emplace_back(r);
  }

  void print_ctrs() {
    stop_all();
    // ebbrt::kprintf_force( RED "Run %d\t", ++inv_num);

    // Unit of 100,000, 100,000, 10,000;
    ebbrt::kprintf_force(
        CYAN "Cyc:%llu \t Ins:%llu \t Ref:%llu \t Ins/Cyc:%f\%\n" RESET,
        cycles.Read() / 100000, ins.Read() / 100000, ref_cycles.Read() / 10000,
        100 * (float)ins.Read() / (float)cycles.Read());
    start_all();
  }

  void clear_list() { ctr_list.clear(); }

  ebbrt::perf::PerfCounter cycles;
  ebbrt::perf::PerfCounter ins;
  ebbrt::perf::PerfCounter ref_cycles;

  std::list<TimeRecord> ctr_list;
};

TimeRecord::TimeRecord(Counter *ctr, std::string s) : s_(s) {
  ebbrt::kprintf_force("TimeRecord cons\n");
  cycles_ = ctr->cycles.Read();
  ins_ = ctr->ins.Read();
  ref_cycles_ = ctr->ref_cycles.Read();
}

} // end ns seuss
#endif
