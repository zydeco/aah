#!/bin/sh

cd capstone

if [ "$1" == clean ]; then
    rm -f ../lib/libcapstone-aah.a ../lib/libcapstone-ios.a
    make clean
    exit 0
fi

# mac build
if [ ! -f "../lib/libcapstone-aah.a" ]; then
    make clean
    CAPSTONE_ARCHS="aarch64" CAPSTONE_SHARED=no CAPSTONE_STATIC=yes CAPSTONE_BUILD_CORE_ONLY=yes ./make.sh mac-universal-no && mv libcapstone.a ../lib/libcapstone-aah.a
fi

# iOS build
if [ ! -f "../lib/libcapstone-ios.a" ]; then
    make clean
    CAPSTONE_ARCHS="aarch64 x86" CAPSTONE_SHARED=no CAPSTONE_STATIC=yes CAPSTONE_BUILD_CORE_ONLY=yes ./make.sh ios arm64 && mv libcapstone.a ../lib/libcapstone-ios.a
fi
