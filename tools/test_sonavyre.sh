#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${RACK_DIR:?Set RACK_DIR to an absolute Rack SDK directory}"
tests=(parity host library)
if [[ "${1:-}" == "--self-contained" ]]; then
  # CI has the distributable source, not access to the owner's private baseline.
  tests=(library)
else
  python3 tools/prepare_sonavyre_reference.py
  python3 tools/gen_sonavyre_catalogue.py --check
fi
python3 tools/gen_sonavyre_editions.py --check
mkdir -p build/sonavyre-tests
common=(-std=c++11 -O2 "-I$RACK_DIR/include" "-I$RACK_DIR/dep/include" "-L$RACK_DIR" -lRack "-Wl,-rpath,$RACK_DIR")
for test in "${tests[@]}"; do
  "${CXX:-g++}" "tests/sonavyre/$test.cpp" "${common[@]}" -o "build/sonavyre-tests/$test"
done
if [[ "${1:-}" != "--self-contained" ]]; then
  build/sonavyre-tests/parity
  build/sonavyre-tests/host
fi
library_dir=$(mktemp -d /tmp/sonavyre-library.XXXXXX)
build/sonavyre-tests/library "$library_dir"

for edition in 0 1; do
  "${CXX:-g++}" tests/sonavyre/editions.cpp -DSONAVYRE_EDITION="$edition" "${common[@]}" -o "build/sonavyre-tests/edition-$edition"
  "build/sonavyre-tests/edition-$edition"
done
