cmake_minimum_required(VERSION 2.6 FATAL_ERROR)
project("seuss-ebbrt" C CXX)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
set(CMAKE_CXX_FLAGS_DEBUG          "-O0 -g3")
set(CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE        "-O4 -flto -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g3 -DNDEBUG")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++14 -Wall ") #-Werror

## Sources
# shared 
set(SOURCES
      src/InstallGlobalIdMap.cc
      src/dsys/Controller.cc
      src/dsys/dsys.cc
      src/SeussChannel.cc
      src/seuss.cc )

# hosted-only
set(HOSTED_SOURCES
      ${SOURCES}
			src/openwhisk/couchdb.cc
			src/openwhisk/kafka.cc
			src/openwhisk/msg.cc
			src/openwhisk/openwhisk.cc
      src/SeussController.cc
      src/hosted/main.cc)

# native-only
set(BAREMETAL_SOURCES
      ${SOURCES}
      src/InvocationSession.cc
      src/SeussInvoker.cc
      )

# ebbrt-zookeeper is required by hosted & native
find_package(EbbRTZooKeeper REQUIRED)

## Baremetal  ========================================================
if( ${CMAKE_SYSTEM_NAME} STREQUAL "EbbRT")
	
  find_package(EbbRTSocket REQUIRED)

	# TODO(jmcadden): add logic to build and install libumm & target

  include_directories(${EBBRT-SOCKET_INCLUDE_DIRS}
    ${EBBRT-ZOOKEEPER_INCLUDE_DIRS})
  add_executable(seuss.elf ${BAREMETAL_SOURCES} )
  target_link_libraries(seuss.elf ${EBBRT-ZOOKEEPER_LIBRARIES} ${EBBRT-SOCKET_LIBRARIES} )
  target_link_libraries(seuss.elf ${CMAKE_CURRENT_SOURCE_DIR}/src/umm/build/libumm.a)
  target_link_libraries(seuss.elf ${CMAKE_CURRENT_SOURCE_DIR}/src/umm/tests/net_test/target.binelf )
	target_link_libraries(seuss.elf PUBLIC -T../src/umm/src/umm.lds )
  add_custom_command(TARGET seuss.elf POST_BUILD
    COMMAND objcopy -O elf32-i386 seuss.elf seuss.elf32 )
  set_directory_properties(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES seuss.elf32 )

## Hosted  ===========================================================
elseif( ${CMAKE_SYSTEM_NAME} STREQUAL "Linux" )
  find_package(EbbRT REQUIRED)
  find_package(Boost 1.53.0 REQUIRED COMPONENTS
    filesystem system coroutine context program_options)
  find_package(Capnp REQUIRED)
  find_package(TBB REQUIRED)
  find_package(Threads REQUIRED)

  ## Seuss dependencies 
  # cppkafka - requires librdkafka at CMAKE_PREFIX_PATH
  find_library(CPPKAFKA_LIBRARIES NAMES cppkafka)

  # pillowtalk - couchDB client
  find_library(PILLOWTALK_LIBRARIES NAMES pillowtalk)

  # yajl - json generator/parser
  find_path(YAJL_INCLUDE_DIR yajl/yajl_parse.h)
  find_file(YAJL_VERSION yajl/yajl_version.h)
  find_library(YAJL_LIBRARY NAMES yajl)
  message("-- Found yajl Include Dir:" ${YAJL_INCLUDE_DIR})
  message("-- Found yajl Library:" ${YAJL_LIBRARY})
  message("-- Found yajl Version File:" ${YAJL_VERSION})

	## seuss target
  include_directories(${EBBRT_INCLUDE_DIRS} ${RDKAFKA_INCLUDE_DIR} ${YAJL_INCLUDE_DIR})
  add_executable(seuss ${HOSTED_SOURCES})
  target_link_libraries(seuss ${CPPKAFKA_LIBRARIES} ${PILLOWTALK_LIBRARIES} ${YAJL_LIBRARY} ${EBBRT_LIBRARIES}
    ${CAPNP_LIBRARIES_LITE} ${CMAKE_THREAD_LIBS_INIT} ${EBBRT-ZOOKEEPER_LIBRARIES} 
    ${Boost_LIBRARIES} ${TBB_LIBRARIES} ${EBBRT_LIBRARIES}
  )
else()
  message(FATAL_ERROR "System name unsupported: ${CMAKE_SYSTEM_NAME}")
endif()
