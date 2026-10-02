#!/bin/sh
# Builds run8088 from the emulator's own i8086core. CXX defaults to g++.
here=$(cd "$(dirname "$0")" && pwd)
src="$here/../../src"
${CXX:-g++} -std=c++11 -O2 -I"$src" -I"$src/libs" \
    "$here/run8088.cpp" "$src/emulator/devices/cpu/i8086core.cpp" \
    -o "$here/run8088"
