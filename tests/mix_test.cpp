// Latency-aligned Mix policy regression test.
// Build & run: g++ -std=c++11 mix_test.cpp -o mix_test && ./mix_test
#include "../src/BsMix.hpp"
#include <cmath>
#include <cstdio>

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

static bool near(float a, float b, float e = 1e-5f) {
	return std::fabs(a - b) <= e;
}

int main() {
	BsMixGains g = bsMixGains(0.f);
	CHECK(near(g.liveDry, 1.f) && near(g.bufferedDry, 0.f) && near(g.wet, 0.f),
	      "fully dry must remain a live zero-latency monitor");

	g = bsMixGains(0.05f);
	CHECK(near(g.liveDry, 0.475f) && near(g.bufferedDry, 0.475f) && near(g.wet, 0.05f),
	      "5%% transition gains were %.3f %.3f %.3f", g.liveDry, g.bufferedDry, g.wet);

	g = bsMixGains(0.1f);
	CHECK(near(g.liveDry, 0.f) && near(g.bufferedDry, 0.9f) && near(g.wet, 0.1f),
	      "10%% Mix must be fully latency-aligned");

	g = bsMixGains(0.5f);
	CHECK(near(g.liveDry, 0.f) && near(g.bufferedDry, 0.5f) && near(g.wet, 0.5f),
	      "50%% Mix must combine the same-time dry/wet pair equally");
	float unchangedBufferedSample = 0.37f;
	float unchangedOutput = unchangedBufferedSample * g.bufferedDry
	                      + unchangedBufferedSample * g.wet;
	CHECK(near(unchangedOutput, unchangedBufferedSample),
	      "aligned unchanged audio must stay unity gain at 50%%");

	g = bsMixGains(1.f);
	CHECK(near(g.liveDry, 0.f) && near(g.bufferedDry, 0.f) && near(g.wet, 1.f),
	      "fully wet endpoint");

	// A reloaded patch has no serialized audio history. Even at fully wet it
	// must pass live audio until the first complete TIME window is available.
	g = bsPrimedMixGains(1.f, 0.f);
	CHECK(near(g.liveDry, 1.f) && near(g.bufferedDry, 0.f) && near(g.wet, 0.f),
	      "empty startup buffer must not silence a fully-wet saved patch");
	g = bsPrimedMixGains(1.f, 0.5f);
	CHECK(near(g.liveDry, 0.5f) && near(g.bufferedDry, 0.f) && near(g.wet, 0.5f),
	      "startup transition must crossfade live to wet smoothly");
	g = bsPrimedMixGains(0.5f, 1.f);
	CHECK(near(g.liveDry, 0.f) && near(g.bufferedDry, 0.5f) && near(g.wet, 0.5f),
	      "primed startup must recover the requested latency-aligned Mix");

	CHECK(!bsBufferCaptureReady(23999, 24000),
	      "partial TIME history was accepted as a valid capture");
	CHECK(bsBufferCaptureReady(24000, 24000),
	      "complete TIME history did not prime the buffer");
	CHECK(!bsEffectiveFreeze(true, false),
	      "restored Freeze must not lock an empty buffer");
	CHECK(bsEffectiveFreeze(true, true),
	      "restored Freeze must engage after buffer priming");

	for (int i = 0; i <= 1000; ++i) {
		float mix = i / 1000.f;
		g = bsMixGains(mix);
		CHECK(g.liveDry >= 0.f && g.bufferedDry >= 0.f && g.wet >= 0.f,
		      "negative gain at %.3f", mix);
		CHECK(near(g.liveDry + g.bufferedDry + g.wet, 1.f, 2e-5f),
		      "gain sum %.6f at %.3f", g.liveDry + g.bufferedDry + g.wet, mix);
		if (mix >= 0.1f)
			CHECK(near(g.liveDry, 0.f), "live dry leaked above alignment range at %.3f", mix);

		for (int j = 0; j <= 100; ++j) {
			float primed = j / 100.f;
			BsMixGains startup = bsPrimedMixGains(mix, primed);
			CHECK(startup.liveDry >= 0.f && startup.bufferedDry >= 0.f && startup.wet >= 0.f,
			      "negative startup gain at mix %.3f, prime %.2f", mix, primed);
			CHECK(near(startup.liveDry + startup.bufferedDry + startup.wet, 1.f, 2e-5f),
			      "startup gain sum %.6f at mix %.3f, prime %.2f",
			      startup.liveDry + startup.bufferedDry + startup.wet, mix, primed);
		}
	}

	if (!fails)
		std::printf("ALL PASS: aligned Mix plus restart-safe buffer priming and deferred Freeze\n");
	return fails ? 1 : 0;
}
