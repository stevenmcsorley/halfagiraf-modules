#include "../src/BsDisplayTelemetry.hpp"
#include <cmath>
#include <cstdio>
#include <limits>

static int failures = 0;

#define CHECK(condition, message) do { \
	if (!(condition)) { std::printf("FAIL: %s\n", message); failures++; } \
} while (0)

static bool near(float a, float b, float epsilon = 1e-6f) {
	return std::fabs(a - b) <= epsilon;
}

int main() {
	CHECK(bsScopeSanitize(std::numeric_limits<float>::quiet_NaN()) == 0.f,
		"NaN display telemetry must be silenced");
	CHECK(bsScopeSanitize(std::numeric_limits<float>::infinity()) == 0.f,
		"infinite display telemetry must be silenced");
	CHECK(bsScopeSanitize(8.f) == 4.f && bsScopeSanitize(-8.f) == -4.f,
		"display telemetry must be bounded");

	BsScopeTelemetry telemetry;
	for (int i = 0; i < 3; i++) {
		BsScopeFrame frame;
		frame.input = i + 0.1f;
		frame.aligned = i + 0.2f;
		frame.bentBroken = i + 0.3f;
		frame.corrupted = i + 0.4f;
		frame.output = i + 0.5f;
		telemetry.publish(frame);
	}
	std::array<BsScopeFrame, BS_SCOPE_POINTS> snapshot;
	int count = telemetry.snapshot(snapshot);
	CHECK(count == 3, "snapshot must expose every published frame before the ring fills");
	CHECK(near(snapshot[0].input, 0.1f) && near(snapshot[2].output, 2.5f),
		"snapshot must preserve chronological signal data");
	CHECK(near(snapshot[1].aligned, 1.2f)
		&& near(snapshot[1].bentBroken, 1.3f)
		&& near(snapshot[1].corrupted, 1.4f),
		"all DSP stages in a scope frame must stay synchronized");

	BsScopeFrame zeros[4];
	CHECK(bsScopeChecksum(zeros, 4) == 0,
		"silent real data must produce a silent checksum");
	BsScopeFrame changed[4];
	changed[2].output = 0.25f;
	CHECK(bsScopeChecksum(changed, 4) != 0,
		"the LCD checksum must react to actual output samples");

	telemetry.clear();
	for (int i = 0; i < 300; i++) {
		BsScopeFrame frame;
		frame.input = i * 0.01f;
		frame.aligned = frame.input + 0.001f;
		frame.bentBroken = frame.input + 0.002f;
		frame.corrupted = frame.input + 0.003f;
		frame.output = frame.input + 0.004f;
		telemetry.publish(frame);
	}
	count = telemetry.snapshot(snapshot);
	CHECK(count == BS_SCOPE_POINTS, "a full telemetry ring must expose exactly its capacity");
	CHECK(near(snapshot[0].input, 0.44f) && near(snapshot[count - 1].input, 2.99f),
		"wrapped telemetry must retain the newest real samples in time order");
	CHECK(near(snapshot[100].corrupted - snapshot[100].input, 0.003f, 2e-6f),
		"wrapped frames must not mix stages from different audio samples");

	telemetry.clear();
	CHECK(telemetry.snapshot(snapshot) == 0,
		"clearing the audio buffer must also clear the LCD data");

	if (failures == 0)
		std::printf("ALL PASS: real-audio LCD telemetry is bounded, synchronized, chronological and data-reactive\n");
	return failures ? 1 : 0;
}
