#include "Counter.h"
#include "umm/src/umm-common.h"

#include <ebbrt/native/Debug.h>

#define kprintf ebbrt::kprintf

namespace seuss {

  TimeRecord::TimeRecord(Counter *ctr, std::string s) : s_(s) {
  kprintf_force("TimeRecord cons\n");
  cycles_ = ctr->cycles.Read();
  ins_ = ctr->ins.Read();
  ref_cycles_ = ctr->ref_cycles.Read();

}

  void Counter::start_all() {
  cycles.Start();
  ins.Start();
  ref_cycles.Start();
}
  void Counter::stop_all() {
  cycles.Stop();
  ins.Stop();
  ref_cycles.Stop();
}
  void Counter::clear_all() {
  cycles.Clear();
  ins.Clear();
  ref_cycles.Clear();
}
void Counter::init_ctrs() {
  cycles =
    ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::cycles);
  ins =
    ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::instructions);
  ref_cycles =
    ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::reference_cycles);

  stop_all();
  clear_all();
  start_all();
}
void Counter::reset_all() {
  stop_all();
  clear_all();
}

//   stop_all();
//   kprintf_force( RED "Run %d\t", ++inv_num);

//   // Unit of 100,000, 100,000, 10,000;
//   kprintf_force(CYAN "Cyc:%llu \t Ins:%llu \t Ref:%llu \t Ins/Cyc:%f\n"
//   RESET,
//                 cycles.Read() / 100000, ins.Read() / 100000,
//                 ref_cycles.Read() / 10000,
//                 (float)ins.Read() / (float)cycles.Read());

//   start_all();
// }

void Counter::dump_list() {
  uint64_t cycles_total = 0;
  uint64_t ins_total = 0;
  uint64_t ref_total = 0;

  for (const auto &e : ctr_list) {
    cycles_total += e.cycles_;
    ins_total += e.ins_;
    ref_total += e.ref_cycles_;
  }

  for (const auto &e : ctr_list) {
    kprintf_force(
        CYAN "%10s:\tCyc:%6llu\tIns:%6llu\tRef:%6llu\tIns/Cyc:%6.3f\tCyc\%:%3.3f%\n" RESET,
        e.s_.c_str(), e.cycles_ / 100000, e.ins_ / 100000, e.ref_cycles_ / 10000,
        (float)e.ins_ / (float)e.cycles_,
        100*(float) e.cycles_ / (float) cycles_total);
  }
  printf(RESET "totals:" CYAN "\t\t %lu \t\t%lu \t\t%lu \n" RESET, cycles_total / 100000, ins_total / 100000, ref_total / 10000);
}

void Counter::add_to_list(TimeRecord &r) {
  // Subract old from current.
  // kprintf_force("clock: %llu, old: %llu \n", cycles.Read(), r.cycles_);
  r.cycles_ = cycles.Read() - r.cycles_;
  r.ins_ = ins.Read() - r.ins_;
  r.ref_cycles_ = ref_cycles.Read() - r.ref_cycles_;
  ctr_list.emplace_back(r);
}

  void Counter::print_ctrs() {
  stop_all();
  // kprintf_force( RED "Run %d\t", ++inv_num);

  // Unit of 100,000, 100,000, 10,000;
  kprintf_force(CYAN "Cyc:%llu \t Ins:%llu \t Ref:%llu \t Ins/Cyc:%f\%\n" RESET,
                cycles.Read() / 100000, ins.Read() / 100000,
                ref_cycles.Read() / 10000,
                100 * (float)ins.Read() / (float)cycles.Read());
  start_all();
}
  void Counter::clear_list() {
    ctr_list.clear();
  }
}
