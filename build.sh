#!/bin/sh


cmake -S . -B build -GNinja -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT"/scripts/buildsystems/vcpkg.cmake
