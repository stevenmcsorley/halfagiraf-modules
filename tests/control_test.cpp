// Control-law regression coverage for the current Bad Sector behaviour:
// integer Repeat counts, unipolar CV attenuation and mono-to-stereo spread.
#include "../src/BsRepeat.hpp"
#include "../src/BsControlLaws.hpp"
#include <cmath>
#include <cstdio>

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static bool near(float a, float b, float e = 1e-6f) {
	return std::fabs(a - b) <= e;
}

int main() {
	CHECK(bsRepeatCount(0.f) == 1, "Repeat CCW is not 1");
	CHECK(bsRepeatCount(1.f) == 1024, "Repeat CW is not 1024");
	for (int count = 1; count <= 1024; count++) {
		float exponent = std::log((float) count) / std::log(2.f);
		float knob = std::pow(exponent / 10.f, 2.f / 3.f);
		CHECK(bsRepeatCount(knob) == count,
		      "integer Repeat %d is unreachable (got %d at %.8f)",
		      count, bsRepeatCount(knob), knob);
	}
	CHECK(bsBreakMoreSubsections(10, 0.f) == 11, "More Subsections does not rise above 10");
	CHECK(bsBreakMoreSubsections(10, 1.f) == 40, "More Subsections high endpoint is not 40");
	CHECK(bsBreakAudioRateSubsections(0.f) == 32, "Audio Rate low endpoint is not 32");
	CHECK(bsBreakAudioRateSubsections(1.f) == 128, "Audio Rate high endpoint is not 128");

	CHECK(near(bsCvAttenuation(0.f), 0.f), "CV CCW does not block");
	CHECK(near(bsCvAttenuation(0.5f), 0.5f), "CV midpoint is not half depth");
	CHECK(near(bsCvAttenuation(1.f), 1.f), "CV CW does not pass fully");

	float l = 1.f, r = -1.f;
	bsStereoEnhance(l, r, 0.f);
	CHECK(near(l, 0.f) && near(r, 0.f), "stereo CCW is not centred mono");
	l = 1.f; r = -1.f;
	bsStereoEnhance(l, r, 0.5f);
	CHECK(near(l, 0.5f) && near(r, -0.5f), "stereo midpoint does not use half side");
	l = 1.f; r = -1.f;
	bsStereoEnhance(l, r, 1.f);
	CHECK(near(l, 1.f) && near(r, -1.f), "stereo CW is not original independent stereo");

	if (!fails)
		printf("ALL PASS: all integer Repeats reachable, unipolar CV, mono-to-independent stereo\n");
	return fails ? 1 : 0;
}
