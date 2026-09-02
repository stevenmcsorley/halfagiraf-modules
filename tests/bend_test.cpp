// Macro Bend policy regression test.
// Build & run: g++ -std=c++11 bend_test.cpp -o bend_test && ./bend_test
#include "../src/BsBend.hpp"
#include <cmath>
#include <cstdio>

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

static bool near(float a, float b, float e = 1e-5f) {
	return std::fabs(a - b) <= e;
}

int main() {
	// The manual's six cumulative spans fill in order.
	CHECK(near(bsBendZoneAmount(0.f, 1), 0.f), "zero must disable Bend");
	CHECK(near(bsBendZoneAmount(1.f / 12.f, 1), 0.5f), "Reverse half-span");
	CHECK(near(bsBendZoneAmount(1.f / 6.f, 1), 1.f), "Reverse full-span");
	CHECK(near(bsBendZoneAmount(1.f / 6.f, 2), 0.f), "Octaves starts after Reverse");
	CHECK(near(bsBendZoneAmount(1.f / 3.f, 2), 1.f), "Octaves full at one third");
	CHECK(near(bsBendZoneAmount(0.5f, 3), 1.f), "2 Octaves full at noon");
	CHECK(near(bsBendZoneAmount(2.f / 3.f, 4), 1.f), "Tape Stop full");
	CHECK(near(bsBendZoneAmount(5.f / 6.f, 5), 1.f), "Slew full");
	CHECK(near(bsBendZoneAmount(1.f, 6), 1.f), "Everything full");

	// Exact setting recovered from the supplied A/B patch. The previous
	// 0.5 multiplier produced 34.2% octave changes here; the reference-led
	// policy is intentionally sparse at about 8.2%.
	const float sampleAmount = 0.2807227373f;
	CHECK(near(bsBendOctaveChance(sampleAmount), 0.08212037f, 1e-4f),
	      "sample setting octave chance %.6f", bsBendOctaveChance(sampleAmount));

	// Each interval is selected directly; the one- and two-octave ranges are
	// alternatives, so they can never multiply into an invented 3-octave jump.
	BsBendDecision d = bsBendDecision(1.f / 3.f,
		1.f, 1.f, 1.f, 0.f, 0.f, 1.f);
	CHECK(near(d.speed, 0.5f) && !d.reverse && !d.tapeStop,
	      "one-octave down decision");
	d = bsBendDecision(1.f / 3.f,
		1.f, 1.f, 1.f, 0.f, 1.f, 1.f);
	CHECK(near(d.speed, 2.f), "one-octave up decision");
	d = bsBendDecision(0.5f,
		1.f, 0.f, 0.f, 0.f, 1.f, 1.f);
	CHECK(near(d.speed, 0.25f), "two-octave down decision");
	d = bsBendDecision(0.5f,
		1.f, 0.f, 1.f, 0.f, 0.f, 1.f);
	CHECK(near(d.speed, 4.f), "two-octave up decision");

	// The final span is not dead travel: it ramps all prior event chances.
	CHECK(bsBendReverseChance(1.f) > bsBendReverseChance(5.f / 6.f),
	      "Everything must raise reverse density");
	CHECK(bsBendOctaveChance(1.f) > bsBendOctaveChance(5.f / 6.f),
	      "Everything must raise octave density");
	CHECK(bsBendTwoOctaveChance(1.f) > bsBendTwoOctaveChance(5.f / 6.f),
	      "Everything must raise two-octave density");
	CHECK(bsBendTapeStopChance(1.f) > bsBendTapeStopChance(5.f / 6.f),
	      "Everything must raise tape-stop density");
	d = bsBendDecision(5.f / 6.f,
		1.f, 1.f, 1.f, 1.f, 1.f, 1.f);
	CHECK(near(d.slew, 1.f), "Slew must be full before Everything");

	if (!fails)
		std::printf("ALL PASS: cumulative Bend zones, sparse early octaves, exact intervals, active Everything span\n");
	return fails ? 1 : 0;
}
