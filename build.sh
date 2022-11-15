#!/bin/sh
#set -x
set -e

if [ ! -d build ];then
    mkdir build
fi

cd build

cmake -DCMAKE_BUILD_TYPE=Debug ..

make -j64

cd -
