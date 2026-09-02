// Offline regression tests for the production external-clock engine in
// src/BsClock.hpp. No Rack SDK is required.
// Build & run: g++ -std=c++11 clock_test.cpp -o clock_test && ./clock_test
#include "../src/BsClock.hpp"
#include <cstdio>
#include <cmath>
#include <vector>

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

static bool nearSample(int actual, int expected, int tolerance = 1) {
	return std::abs(actual - expected) <= tolerance;
}

static std::vector<int> runSteady(int d, int inputSamples, int durationSamples) {
	const float dt = 0.001f;
	BsExternalClock c;
	c.inputPeriod = inputSamples * dt;
	std::vector<int> ticks;
	for (int s = 0; s <= durationSamples; ++s) {
		bool edge = (s % inputSamples) == 0;
		if (c.process(dt, d, edge, false).tick)
			ticks.push_back(s);
	}
	return ticks;
}

int main() {
	const int inputSamples = 120; // divisible by 2, 3, 4 and 8
	const int expectedOutputSamples[9] = {
		1920, 960, 480, 240, 120, 60, 40, 30, 15
	};

	// External mode waits for a real edge; free-run only has meaning after a
	// clock rate has actually been learned.
	{
		BsExternalClock c;
		int ticks = 0;
		for (int s = 0; s < 2000; ++s)
			if (c.process(0.001f, 4, false, false).tick) ticks++;
		CHECK(ticks == 0, "external clock emitted %d ticks before its first edge", ticks);
	}

	// Every divide/multiply setting emits immediately on the first edge and
	// then follows its exact grid under a stable external clock.
	for (int d = 0; d < 9; ++d) {
		int step = expectedOutputSamples[d];
		std::vector<int> ticks = runSteady(d, inputSamples, step * 3 + 4);
		CHECK(ticks.size() >= 4, "division %d: only %d ticks", d, (int)ticks.size());
		for (int i = 0; i < 4 && i < (int)ticks.size(); ++i)
			CHECK(nearSample(ticks[i], i * step),
			      "division %d: tick %d at %d, want %d", d, i, ticks[i], i * step);
	}

	// Once the input disappears, output must continue at the learned period
	// with no initial 10%-late beat and no short recovery interval.
	{
		const float dt = 0.001f;
		BsExternalClock c;
		c.inputPeriod = inputSamples * dt;
		std::vector<int> ticks;
		for (int s = 0; s <= 720; ++s) {
			bool edge = (s == 0 || s == 120 || s == 240);
			BsClockResult r = c.process(dt, 4, edge, false);
			if (r.tick) ticks.push_back(s);
			if (s == 720)
				CHECK(r.clockLost, "clock-loss indicator did not assert after four beats");
		}
		const int want[] = {0, 120, 240, 360, 480, 600, 720};
		CHECK(ticks.size() == 7, "clock loss: %d ticks, want 7", (int)ticks.size());
		for (int i = 0; i < 7 && i < (int)ticks.size(); ++i)
			CHECK(nearSample(ticks[i], want[i]), "clock loss: tick %d at %d, want %d", i, ticks[i], want[i]);
	}

	// A slightly late edge after the predictor fires re-phases the future grid
	// without creating an adjacent/double output tick.
	{
		const float dt = 0.001f;
		BsExternalClock c;
		c.inputPeriod = inputSamples * dt;
		std::vector<int> ticks;
		for (int s = 0; s <= 380; ++s) {
			bool edge = (s == 0 || s == 120 || s == 252);
			if (c.process(dt, 4, edge, false).tick) ticks.push_back(s);
		}
		bool tickAtLateEdge = false;
		for (int t : ticks) if (t == 252) tickAtLateEdge = true;
		CHECK(!tickAtLateEdge, "late edge produced an extra tick at 252");
		for (int i = 1; i < (int)ticks.size(); ++i)
			CHECK(ticks[i] - ticks[i - 1] > 1, "late edge: adjacent ticks at %d/%d", ticks[i - 1], ticks[i]);
	}

	// A real tempo slowdown is not mistaken for harmless jitter. Once the
	// edge is a quarter-cycle or more from the predictor, that edge becomes
	// the new downbeat rather than being ignored until the following pulse.
	{
		const float dt = 0.001f;
		BsExternalClock c;
		c.inputPeriod = inputSamples * dt;
		bool tickAtNewDownbeat = false;
		for (int s = 0; s <= 300; ++s) {
			bool edge = (s == 0 || s == 120 || s == 300);
			BsClockResult r = c.process(dt, 4, edge, false);
			if (s == 300) tickAtNewDownbeat = r.tick;
		}
		CHECK(tickAtNewDownbeat, "slower source edge at 300 did not become the new downbeat");
	}

	// Changing x1 -> x8 between edges anchors the new multiplier to the most
	// recent input edge. Changing just after sample 30 makes 45 the next
	// 15-sample boundary; the ratio change itself must not fire at sample 31.
	{
		const float dt = 0.001f;
		BsExternalClock c;
		c.inputPeriod = inputSamples * dt;
		std::vector<int> ticks;
		for (int s = 0; s <= 76; ++s) {
			int d = s < 31 ? 4 : 8;
			bool edge = s == 0;
			if (c.process(dt, d, edge, false).tick) ticks.push_back(s);
		}
		CHECK(ticks.size() >= 4, "ratio change: only %d ticks", (int)ticks.size());
		const int want[] = {0, 45, 60, 75};
		for (int i = 0; i < 4 && i < (int)ticks.size(); ++i)
			CHECK(nearSample(ticks[i], want[i]), "ratio change: tick %d at %d, want about %d", i, ticks[i], want[i]);
	}

	// External Reset is consumed on the next edge and makes that edge the
	// downbeat even when a divided clock would not normally emit there.
	{
		const float dt = 0.001f;
		BsExternalClock c;
		c.inputPeriod = inputSamples * dt;
		bool resetTick = false, consumed = false;
		for (int s = 0; s <= 600; ++s) {
			bool edge = (s % inputSamples) == 0;
			bool reset = edge && s == 360;
			BsClockResult r = c.process(dt, 0, edge, reset);
			if (s == 360) { resetTick = r.tick; consumed = r.consumedReset; }
		}
		CHECK(resetTick && consumed, "divided-clock reset was not applied on its next edge");
	}

	if (!fails)
		std::printf("ALL PASS: external divisions/multipliers, live ratio changes, reset, late-edge suppression and clock-loss free-run\n");
	else
		std::printf("%d FAILURES\n", fails);
	return fails ? 1 : 0;
}
