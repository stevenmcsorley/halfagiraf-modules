RACK_DIR ?= ../Rack-SDK

FLAGS +=
CFLAGS +=
CXXFLAGS +=
LDFLAGS +=

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += res
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
