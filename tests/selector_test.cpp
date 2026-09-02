// Offline tests for the production DAMAGE / CV AMT selector state machine:
// stored values, cycling, virtual-knob snap recall, and preset restore.
// Build & run:  g++ -std=c++11 -I../src selector_test.cpp -o sel_test && ./sel_test
#include "../src/BsSelector.hpp"
#include <cstdio>
#include <cmath>

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

int main() {
	// 1. knob edits the active channel directly
	BsSelector s;
	s.reset(0.2f, 0.5f, 0.8f);
	CHECK(std::fabs(s.track(0.3f) - 0.3f) < 1e-6f, "caught knob edits channel 0");
	CHECK(std::fabs(s.vals[0] - 0.3f) < 1e-6f, "value stored");

	// 2. advancing stores the old value, recalls the new one, and cycles
	float recalled = s.advanceSnap(0.35f);
	CHECK(s.sel == 1, "advance to channel 1");
	CHECK(std::fabs(s.vals[0] - 0.35f) < 1e-6f, "value stored while leaving channel");
	CHECK(std::fabs(recalled - 0.5f) < 1e-6f, "next channel recalled");
	s.advanceSnap(recalled); s.advanceSnap(0.8f);
	CHECK(s.sel == 0, "cycles back to 0");

	// 3. tracking after a snap edits only the newly selected channel
	s.reset(0.2f, 0.9f, 0.5f);
	recalled = s.advanceSnap(0.2f);
	CHECK(std::fabs(recalled - 0.9f) < 1e-6f, "switch recalls channel 1");
	CHECK(std::fabs(s.track(0.95f) - 0.95f) < 1e-6f, "recalled channel edits immediately");
	CHECK(std::fabs(s.vals[1] - 0.95f) < 1e-6f, "channel 1 follows");

	// 4. preset restore snaps exactly to the stored selected channel
	BsSelector r;
	r.reset(0.1f, 0.2f, 0.3f);
	r.sel = 2;
	CHECK(std::fabs(r.selectedSnap() - 0.3f) < 1e-6f, "restore recalls selected value");

	// 5. untouched channels are never disturbed
	r.track(0.4f);
	CHECK(std::fabs(r.vals[0] - 0.1f) < 1e-6f && std::fabs(r.vals[1] - 0.2f) < 1e-6f,
	      "inactive channels untouched");

	if (fails == 0) printf("ALL PASS (5 groups): selector storage, cycling and virtual-knob snap recall\n");
	return fails ? 1 : 0;
}
