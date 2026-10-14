#!/usr/bin/env bash
set -euo pipefail
# Independent source checkout: never compile the host-patched vendor copy here.
compiler="${1:-g++}"
target="$($compiler -dumpmachine)"
root="build/grainloom/pristine-source"
fetch_revision() {
  local directory="$1" url="$2" revision="$3"
  mkdir -p "$directory"
  if [ ! -e "$directory/.git" ]; then
    git -C "$directory" init -q
    git -C "$directory" remote add origin "$url"
  fi
  if ! git -C "$directory" cat-file -e "$revision^{commit}" 2>/dev/null; then
    git -C "$directory" fetch --depth 1 origin "$revision"
  fi
  git -C "$directory" checkout --detach "$revision"
  test "$(git -C "$directory" rev-parse HEAD)" = "$revision"
}
fetch_revision "$root" https://github.com/pichenettes/eurorack.git d8f473915232af74347762b6365202bfd26f73f6
fetch_revision "$root/stmlib" https://github.com/pichenettes/stmlib.git e3bd7c9cc00e4364166f9905c0509b6ffd0535ec
flags=()
case "$target" in *mingw*) flags+=(-mno-ms-bitfields);; esac
output="build/grainloom/$target"
mkdir -p "$output"
"$compiler" -std=c++11 -O2 -ffp-contract=off -DTEST -D_USE_MATH_DEFINES -include cstdio \
  -Wno-unused-local-typedefs -Wno-sign-compare "${flags[@]}" -I"$root" \
  tests/grainloom/reference_render.cpp "$root"/beads/dsp/*.cc \
  "$root/beads/resources.cc" "$root/stmlib/utils/random.cc" \
  "$root/stmlib/dsp/units.cc" -o "$output/pristine_reference"
"$output/pristine_reference" "$output/pristine.f32"
cmp "$output/pristine.f32" "$output/host.f32"
echo "Pristine pinned Beads DSP and host render are byte-identical"
sha256sum "$output/pristine.f32"
