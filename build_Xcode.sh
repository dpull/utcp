#!/bin/sh
#set -x
set -e

if [ ! -d build ];then
    mkdir build
fi

cd build

cmake -GXcode ..

cd -
