// Grid-lock timing test — validates the REAL grid arithmetic (src/BsGrid.hpp,
// the same functions the module calls) against exact rational clock fractions,
// not against its own rounding:
//   1. exact tiling: representative integer repeat counts x awkward lengths
//      (44.1k/48k/96k beats, odd/jittered lengths) -> exactly `subs`
//      retriggers, each within one sample of the ideal fraction k*len/subs
//   2. the named failure cases of the old floored-window model
//   3. late clock edges and live mid-division Repeat changes (deterministic
//      + 300 randomized):
//      no back-to-back retriggers, every retrigger on the old or new grid
//   4. teeth: the floored-window model and the no-re-grid change scheme and
//      the content-wrap model all provably FAIL these assertions
// Build & run:  g++ -std=c++11 timing_test.cpp -o timing_test && ./timing_test
#include "../src/BsGrid.hpp"
#include "../src/BsRepeat.hpp"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstdlib>

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint32_t rs = 0xC0FFEE;
static float rf() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (rs & 0xFFFFFF) / float(0x1000000); }

static const int COUNTS[25] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16,
                               31, 32, 48, 64, 96, 127, 128, 255, 512,
                               777, 999, 1024};

int main() {
	// ---- 1. exact tiling across sample rates and awkward lengths ----
	const int LENS[10] = {22050, 24000, 23999, 44100, 48000,
	                      96000, 22051, 33075, 18375, 12347};
	for (int li = 0; li < 10; li++) {
		int len = LENS[li];
		for (int ri = 0; ri < 25; ri++) {
			int subs = COUNTS[ri];
			if (subs > len / 4) continue;      // module clamps the same way
			int lastWin = -1, retrigs = 0, prevT = -10;
			for (int t = 0; t < len; t++) {
				int w = bsGridIndex(t, len, subs);
				if (w != lastWin) {
					lastWin = w;
					double ideal = (double) w * len / subs;
					CHECK(std::fabs(t - ideal) < 1.0,
					      "len=%d subs=%d: boundary %d off ideal %.3f", len, subs, t, ideal);
					CHECK(t == bsGridStart(w, len, subs),
					      "len=%d subs=%d: boundary %d != winStart %d",
					      len, subs, t, bsGridStart(w, len, subs));
					CHECK(t - prevT > 1,
					      "len=%d subs=%d: adjacent retriggers %d,%d", len, subs, prevT, t);
					prevT = t;
					retrigs++;
				}
			}
			// EXACTLY the requested number of windows — no stub at beat end
			CHECK(retrigs == subs, "len=%d subs=%d: %d retriggers (want %d)",
			      len, subs, retrigs, subs);
		}
	}

	// ---- 2. the audit's named cases ----
	{
		// 120 BPM at 44.1 kHz, 4 repeats: floored model fired FIVE retriggers
		// (0, 5512, 11024, 16536, 22048); exact model must fire four quarters
		int len = 22050, subs = 4, lastWin = -1, retrigs = 0;
		for (int t = 0; t < len; t++) {
			int w = bsGridIndex(t, len, subs);
			if (w != lastWin) { lastWin = w; retrigs++; }
		}
		CHECK(retrigs == 4, "22050/4: %d retriggers (want 4)", retrigs);
	}
	{
		// 44100 / 1024: floored model produced ~1050 windows
		int len = 44100, subs = 1024, lastWin = -1, retrigs = 0;
		for (int t = 0; t < len; t++) {
			int w = bsGridIndex(t, len, subs);
			if (w != lastWin) { lastWin = w; retrigs++; }
		}
		CHECK(retrigs == 1024, "44100/1024: %d retriggers (want 1024)", retrigs);
	}
	{
		// 23999-sample measured beat, 3 repeats: no fourth tiny retrigger
		int len = 23999, subs = 3, lastWin = -1, retrigs = 0;
		for (int t = 0; t < len; t++) {
			int w = bsGridIndex(t, len, subs);
			if (w != lastWin) { lastWin = w; retrigs++; }
		}
		CHECK(retrigs == 3, "23999/3: %d retriggers (want 3)", retrigs);
	}
	{
		// A late next clock edge must hold the last window. Previously the raw
		// floor(t*subs/len) result invented windows 4,5,... after the division.
		int len = 24000, subs = 4, lastWin = -1, retrigs = 0;
		for (int t = 0; t < len + 12000; t++) {
			int w = bsGridIndex(t, len, subs);
			CHECK(w >= 0 && w < subs, "late edge: invalid window %d at %d", w, t);
			if (w != lastWin) { lastWin = w; retrigs++; }
		}
		CHECK(retrigs == subs, "late edge: %d retriggers (want %d)", retrigs, subs);
	}
	{
		// When TIME is lengthened mid-cycle, production loops the old acquired
		// section until the new boundary rather than parking at subPhase=1 (zero
		// gain with windowing). Its repeat sequence must remain coherent.
		int len = 12000, subs = 4, last = -1, retrigs = 0;
		for (int elapsed = 0; elapsed < len * 2 + len / 2; ++elapsed) {
			int t = bsGridPlaybackTime(elapsed, len);
			int w = bsGridIndex(t, len, subs);
			if (w != last) { last = w; retrigs++; }
			int ws = bsGridStart(w, len, subs);
			int wl = bsGridStart(w + 1, len, subs) - ws;
			float phase = (float)(t - ws) / wl;
			CHECK(phase >= 0.f && phase < 1.f,
			      "TIME transition parked envelope at %.3f", phase);
		}
		CHECK(retrigs == 10, "TIME transition: %d retriggers, want 10", retrigs);
	}
	// ---- 3. live Repeat change mid-division (module's re-grid logic) ----
	{
		// knob turns 2 -> 4 mid-window in a 24000-sample division. The change
		// applies at the next boundary (12000) and the window index is
		// recomputed on the NEW grid at that same instant, so the old scheme's
		// 12000,12001 double-retrigger cannot happen. Expected: 0, 12000, 18000.
		int len = 24000, changeAt = 7000;
		int subsActive = 2, lastWin = -1, nT = 0, trail[8] = {0};
		for (int t = 0; t < len; t++) {
			int target = (t >= changeAt) ? 4 : 2;
			int w = 0;
			if (bsGridAdvance(t, len, target, subsActive, lastWin, w)) {
				if (nT < 8) trail[nT] = t;
				nT++;
				if (t == 12000)
					CHECK(subsActive == 4 && w == 2,
					      "2->4/24000: re-grid ordering was subs=%d win=%d, want 4/2",
					      subsActive, w);
			}
		}
		CHECK(nT == 3 && trail[0] == 0 && trail[1] == 12000 && trail[2] == 18000,
		      "2->4/24000: got %d retrigs (%d %d %d), want 0 12000 18000",
		      nT, trail[0], trail[1], trail[2]);
	}
	{
		// The old active-boundary-only policy made 1 -> 10 wait until the end
		// of the complete TIME division. It must now switch at 7200, the first
		// requested-grid boundary after the change at 7001.
		int len = 24000, changeAt = 7001;
		int subsActive = 1, lastWin = -1, switchedAt = -1;
		for (int t = 0; t < len; t++) {
			int target = (t >= changeAt) ? 10 : 1;
			int before = subsActive, w = 0;
			if (bsGridAdvance(t, len, target, subsActive, lastWin, w)
					&& before != subsActive)
				switchedAt = t;
		}
		CHECK(switchedAt == 7200,
		      "1->10/24000: switched at %d, want requested-grid boundary 7200", switchedAt);
	}
	// randomized: any integer count -> any other, at a random moment, in a
	// random-length division. Every retrigger must land on a valid boundary
	// of either the active or requested grid, and never back-to-back.
	for (int trial = 0; trial < 300; trial++) {
		int len = 2000 + (int)(rf() * 94000.f);
		int maxSubs = std::max(1, std::min(1024, len / 4));
		int subsActive = 1 + (int)(rf() * maxSubs);
		int newTarget = 1 + (int)(rf() * maxSubs);
		int changeAt = (int)(rf() * len);
		int lastWin = -1, prevT = -10;
		for (int t = 0; t < len; t++) {
			int target = (t >= changeAt) ? newTarget : subsActive;
			int firingSubs = subsActive;
			int w = 0;
			if (bsGridAdvance(t, len, target, subsActive, lastWin, w)) {
				CHECK(bsGridIsBoundary(t, len, firingSubs) || bsGridIsBoundary(t, len, target),
				      "trial %d: retrigger %d on neither active %d nor target %d grid",
				      trial, t, firingSubs, target);
				CHECK(t - prevT > 1,
				      "trial %d len %d change %d: adjacent retriggers %d,%d (active-before %d target %d active-after %d)",
				      trial, len, changeAt, prevT, t, firingSubs, target, subsActive);
				prevT = t;
			}
		}
	}

	// ---- 4. teeth: the broken models fail these assertions ----
	{
		// floored fixed window on 22050/4 fires the documented 5th retrigger
		int len = 22050, subLenT = len / 4, lastWin = -1, retrigs = 0, lastT = -1;
		for (int t = 0; t < len; t++) {
			int w = t / subLenT;
			if (w != lastWin) { lastWin = w; retrigs++; lastT = t; }
		}
		CHECK(retrigs == 5 && lastT == 22048,
		      "floored model unexpectedly fixed (%d retrigs, last %d)", retrigs, lastT);
	}
	{
		// old change scheme (apply target withOUT re-gridding the index) on
		// 2->4/24000 produces the 12000,12001 one-sample double retrigger
		int len = 24000, changeAt = 7000;
		int subsActive = 2, lastWin = -1, prevT = -10;
		bool sawPair = false;
		for (int t = 0; t < len; t++) {
			int target = (t >= changeAt) ? 4 : 2;
			int w = bsGridIndex(t, len, subsActive);
			if (w != lastWin) {
				lastWin = w;
				if (target != subsActive) subsActive = target;   // no re-grid
				if (t - prevT == 1) sawPair = true;
				prevT = t;
			}
		}
		CHECK(sawPair, "no-re-grid change scheme unexpectedly safe");
	}
	{
		// content-wrap retriggers at speed 1.5 land off the rational grid
		int len = 48000, subs = 4;
		double subLen = (double) len / subs, spd = 1.5, rel = 0.0;
		bool offGrid = false;
		for (int t = 0; t < len; t++) {
			rel += spd;
			if (rel >= subLen) {
				rel -= subLen;
				if (t + 1 != bsGridStart(bsGridIndex(t + 1, len, subs), len, subs))
					offGrid = true;
			}
		}
		CHECK(offGrid, "content-wrap model unexpectedly on-grid");
	}

	if (fails == 0)
		printf("ALL PASS: exact rational integer grid — exact window counts, "
		       "responsive on-grid Repeat changes; all three broken models "
		       "provably fail the same assertions\n");
	else
		printf("%d FAILURES\n", fails);
	return fails ? 1 : 0;
}
