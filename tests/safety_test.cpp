// Non-finite signal containment regression test.
// Build & run: g++ -std=c++11 safety_test.cpp -o safety_test && ./safety_test
#include "../src/BsSignalSafety.hpp"
#include <cmath>
#include <cstdio>
#include <limits>

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); fails++; } } while (0)

int main() {
	float nan = std::numeric_limits<float>::quiet_NaN();
	float inf = std::numeric_limits<float>::infinity();
	CHECK(bsSanitizeAudio(nan) == 0.f, "NaN was not silenced");
	CHECK(bsSanitizeAudio(inf) == 0.f, "+Inf was not silenced");
	CHECK(bsSanitizeAudio(-inf) == 0.f, "-Inf was not silenced");
	CHECK(bsSanitizeAudio(9.f) == 8.f, "positive corrupt value was not contained");
	CHECK(bsSanitizeAudio(-9.f) == -8.f, "negative corrupt value was not contained");
	CHECK(bsSanitizeAudio(0.25f) == 0.25f, "valid audio was changed");
	CHECK(bsFiniteAudioState(1.f, 2.f, 3.f, 4.f), "valid filter state rejected");
	CHECK(!bsFiniteAudioState(1.f, nan, 3.f, 4.f), "poisoned filter state accepted");
	if (!fails)
		std::printf("ALL PASS: non-finite samples are contained and poisoned state is detectable\n");
	return fails ? 1 : 0;
}
