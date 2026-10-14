MUTABLE_ROOT := vendor/mutable
BEADS_SOURCES := $(wildcard $(MUTABLE_ROOT)/beads/dsp/*.cc) $(MUTABLE_ROOT)/beads/resources.cc $(MUTABLE_ROOT)/stmlib/utils/random.cc $(MUTABLE_ROOT)/stmlib/dsp/units.cc
BEADS_FLAGS := -std=c++11 -O2 -DTEST -D_USE_MATH_DEFINES -include cstdio -I$(MUTABLE_ROOT) -Wno-unused-local-typedefs -Wno-sign-compare

.PHONY: grainloom-upstream-test
grainloom-upstream-test:
	mkdir -p build/grainloom/reference/audio_samples
	$(CXX) $(BEADS_FLAGS) tests/grainloom/upstream_runner.cpp $(BEADS_SOURCES) -o build/grainloom/upstream_runner
	cd build/grainloom/reference && ../upstream_runner
