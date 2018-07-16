#include <ebbrt-zookeeper/ZKGlobalIdMap.h>
#include <ebbrt/EventManager.h>

void ebbrt::InstallGlobalIdMap(){
  ebbrt::kprintf("Installing ZooKeeper GlobalIdMap\n");
  ebbrt::ZKGlobalIdMap::Create(ebbrt::kGlobalIdMapId);
#ifdef __ebbrt__
  zkglobal_id_map->Init();
#else
	// Do this asynchronously to allow clean bring-up of CPU context 
  ebbrt::event_manager->Spawn([]() { zkglobal_id_map->Init();/*.Block();*/ });
#endif
}
