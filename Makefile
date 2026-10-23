RACK_DIR ?= ../Rack-SDK
.DEFAULT_GOAL := all
include grainloom.mk

FLAGS +=
CFLAGS +=
CXXFLAGS +=
LDFLAGS +=

SOURCES += $(filter-out src/Palimpsest.cpp,$(wildcard src/*.cpp))
SOURCES += src/sonavyre/Sonavyre.cpp
SOURCES += $(BEADS_HOST_SOURCES)

$(patsubst %,build/%.o,$(BEADS_HOST_SOURCES)) build/src/Grainloom.cpp.o: CXXFLAGS += -DGRAINLOOM_HOST -Ivendor/mutable -include cstdio -Wno-unused-local-typedefs $(BEADS_LAYOUT_FLAGS) -fno-unsafe-math-optimizations -fno-finite-math-only -ffp-contract=off

DISTRIBUTABLES += res
DISTRIBUTABLES += docs/sonavyre
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

.PHONY: test
test:
	mkdir -p build/tests
	@set -e; for source in tests/*.cpp; do \
		name=$$(basename "$$source" .cpp); \
		$(CXX) -std=c++11 -O2 -Wall -Wextra -pedantic "$$source" -o "build/tests/$$name"; \
		"build/tests/$$name"; \
	done

include $(RACK_DIR)/plugin.mk
