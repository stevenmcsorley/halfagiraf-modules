MUTABLE_ROOT := vendor/mutable
GRAINLOOM_TEST_DIR := build/grainloom/$(shell $(CXX) -dumpmachine)
BEADS_SOURCES := $(wildcard $(MUTABLE_ROOT)/beads/dsp/*.cc) $(MUTABLE_ROOT)/beads/resources.cc $(MUTABLE_ROOT)/stmlib/utils/random.cc $(MUTABLE_ROOT)/stmlib/dsp/units.cc
BEADS_FLAGS := -std=c++11 -O2 -DTEST -D_USE_MATH_DEFINES -include cstdio -I$(MUTABLE_ROOT) -Wno-unused-local-typedefs -Wno-sign-compare
BEADS_HOST_SOURCES := $(BEADS_SOURCES) $(MUTABLE_ROOT)/beads/gain_control.cc
BEADS_HOST_FLAGS := -std=c++11 -O2 -DGRAINLOOM_HOST -D_USE_MATH_DEFINES -include cstdio -I$(MUTABLE_ROOT) -Wno-unused-local-typedefs -Wno-sign-compare
ifneq (,$(findstring mingw,$(shell $(CXX) -dumpmachine)))
BEADS_FLAGS += -mno-ms-bitfields
BEADS_HOST_FLAGS += -mno-ms-bitfields
BEADS_LAYOUT_FLAGS := -mno-ms-bitfields
endif

.PHONY: grainloom-rack-test
grainloom-rack-test:
	mkdir -p $(GRAINLOOM_TEST_DIR)
	$(CXX) $(BEADS_HOST_FLAGS) -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include tests/grainloom/rack_module_test.cpp $(BEADS_HOST_SOURCES) -L$(RACK_DIR) -lRack -o $(GRAINLOOM_TEST_DIR)/rack_module_test
	$(GRAINLOOM_TEST_DIR)/rack_module_test

.PHONY: grainloom-test
grainloom-test:
	mkdir -p $(GRAINLOOM_TEST_DIR)
	$(CXX) $(BEADS_HOST_FLAGS) tests/grainloom/adapter_test.cpp $(BEADS_HOST_SOURCES) -o $(GRAINLOOM_TEST_DIR)/adapter_test
	$(GRAINLOOM_TEST_DIR)/adapter_test
	$(CXX) $(BEADS_HOST_FLAGS) tests/grainloom/snapshot_test.cpp $(BEADS_HOST_SOURCES) -o $(GRAINLOOM_TEST_DIR)/snapshot_test
	$(GRAINLOOM_TEST_DIR)/snapshot_test
	$(CXX) $(BEADS_HOST_FLAGS) tests/grainloom/reference_render.cpp $(BEADS_SOURCES) -o $(GRAINLOOM_TEST_DIR)/host_reference
	$(CXX) $(BEADS_FLAGS) tests/grainloom/reference_render.cpp $(BEADS_SOURCES) -o $(GRAINLOOM_TEST_DIR)/upstream_reference
	$(GRAINLOOM_TEST_DIR)/host_reference $(GRAINLOOM_TEST_DIR)/host.f32
	$(GRAINLOOM_TEST_DIR)/upstream_reference $(GRAINLOOM_TEST_DIR)/upstream.f32
	cmp $(GRAINLOOM_TEST_DIR)/host.f32 $(GRAINLOOM_TEST_DIR)/upstream.f32

.PHONY: grainloom-upstream-test
grainloom-upstream-test:
	mkdir -p build/grainloom/reference/audio_samples
	$(CXX) $(BEADS_FLAGS) tests/grainloom/upstream_runner.cpp $(BEADS_SOURCES) -o build/grainloom/upstream_runner
	cd build/grainloom/reference && ../upstream_runner
