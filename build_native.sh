#!/bin/bash

BUILD_DIR=$PWD/build_native
TC_FILE=$EBBRT_SYSROOT/usr/misc/ebbrt.cmake

if [[ ! -f "$TC_FILE" ]]; then echo "ERROR $TC_FILE not found. Exiting..."; exit 0; fi
rm -r CMakeFiles cmake_install.cmake CMakeCache.txt &> /dev/null
mkdir -p $BUILD_DIR
cd $BUILD_DIR
make clean
# Clear any local cmake state in root dir
if [ -n "$DEBUG" ]; then 
cmake -DCMAKE_TOOLCHAIN_FILE=$TC_FILE -DCMAKE_BUILD_TYPE=Debug ../ && make -j VERBOSE=1
else
cmake -DCMAKE_TOOLCHAIN_FILE=$TC_FILE -DCMAKE_BUILD_TYPE=Release ../ && make -j VERBOSE=1
fi
