#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${RACK_DIR:?Set RACK_DIR to an absolute Rack SDK directory}"
mkdir -p build/sonavyre-tests
common=(-std=c++11 -O2 "-I$RACK_DIR/include" "-I$RACK_DIR/dep/include" "-L$RACK_DIR" -lRack "-Wl,-rpath,$RACK_DIR")
for test in parity host library; do
  "${CXX:-g++}" "tests/sonavyre/$test.cpp" "${common[@]}" -o "build/sonavyre-tests/$test"
done
build/sonavyre-tests/parity
build/sonavyre-tests/host
library_dir=$(mktemp -d /tmp/sonavyre-library.XXXXXX)
build/sonavyre-tests/library "$library_dir"
