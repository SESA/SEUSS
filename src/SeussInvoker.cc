//          Copyright Boston University SESA Group 2013 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <ebbrt/Debug.h>
#include <ebbrt/Cpu.h>
#include <ebbrt/UniqueIOBuf.h>


#include "SeussInvoker.h"
#include "SeussChannel.h"

#include "umm/src/Umm.h"

void seuss::Init(){
  auto rep = new SeussChannel(SeussChannel::global_id);
  SeussChannel::Create(rep, SeussChannel::global_id);
  umm::UmManager::Init();
  // Begin seuss invoker on each core
  size_t my_cpu = ebbrt::Cpu::GetMine();
  size_t num_cpus = ebbrt::Cpu::Count();
  kassert(my_cpu == 0);
  for (auto i = my_cpu; i <= num_cpus; i++) {
    ebbrt::event_manager->SpawnRemote(
        [i]() {
          kprintf("Begin seuss invoker on core #%d\n", i);
          seuss::invoker->Begin();
        },
        i);
  }
}

void seuss::InvocationSession::Close(){
  ebbrt::kprintf_force("InvocationSession closed!\n");
}

void seuss::InvocationSession::Connected() { set_connected.SetValue(); }

void seuss::InvocationSession::Abort() {
  ebbrt::kprintf_force("InvocationSession aborted!\n");
}
void seuss::InvocationSession::Receive(std::unique_ptr<ebbrt::MutIOBuf> b) {}

#if 0
    const std::string code =
        R"({"value": {"main":"main", "code":"function main(msg){console.log(msg);}"}})";
    auto buf = ebbrt::MakeUniqueIOBuf(run.size());
    auto dp = buf->GetMutDataPointer();
    auto str_ptr = reinterpret_cast<char *>(dp.Data());
    run.copy(str_ptr, run.size());
    Send(std::move(buf));
#endif

void seuss::Invoker::Begin() {
  // Generated UM Instance from the linked in Elf
  auto snap = umm::ElfLoader::CreateInstanceFromElf(&_sv_start);
  umm::manager->Load(std::move(snap));
  // Set breakpoint for snapshot
  ebbrt::Future<umm::UmSV> snap_f = umm::manager->SetCheckpoint(
      umm::ElfLoader::GetSymbolAddress("uv_uptime"));

  // Start TCP connection after snapshot breakpoint
  snap_f.Then([this](ebbrt::Future<umm::UmSV> snap_f) {
    ebbrt::NetworkManager::TcpPcb pcb;
    std::array<uint8_t, 4> umip = {{169, 254, 1, 0}};
    pcb.Connect(ebbrt::Ipv4Address(umip), 8080);
    umsesh_ = new InvocationSession(std::move(pcb));
    umsesh_->Install();
  }); // End snap_f.Then(...)
  umm::manager->Kickoff();
}
