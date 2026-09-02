#include "../src/PalimpsestEngine.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>

using namespace palimpsest;

static void testFftRoundTrip() {
	std::array<std::complex<float>, FFT_SIZE> data;
	std::array<std::complex<float>, FFT_SIZE> reference;
	for (int i = 0; i < FFT_SIZE; ++i) {
		float value = 0.43f * std::sin(2.f * PI * i / 29.f)
			+ 0.17f * std::cos(2.f * PI * i / 71.f);
		data[i] = reference[i] = std::complex<float>(value, 0.03f * value);
	}
	Radix2FFT::transform(data, false);
	Radix2FFT::transform(data, true);
	float maximumError = 0.f;
	for (int i = 0; i < FFT_SIZE; ++i)
		maximumError = std::max(maximumError, std::abs(data[i] - reference[i]));
	assert(maximumError < 2e-4f);
}

static float memoryChecksum(const SpectralMemoryEngine& engine) {
	double checksum = 0.0;
	for (int channel = 0; channel < 2; ++channel)
		for (int layer = 0; layer < MEMORY_LAYERS; ++layer)
			for (int bin = 0; bin < BIN_COUNT; ++bin)
				checksum += engine.memoryValue(channel, layer, bin)
					* (1.0 + channel * 0.1 + layer * 0.013 + bin * 0.00001);
	return (float) checksum;
}

static void feedTone(SpectralMemoryEngine& engine, Parameters parameters,
	int samples, float frequency, float amplitude) {
	for (int i = 0; i < samples; ++i) {
		float phase = 2.f * PI * frequency * i / 48000.f;
		float left = amplitude * std::sin(phase);
		float right = amplitude * std::sin(phase * 1.003f + 0.37f);
		Outputs result = engine.process(left, right, 0.f, 0.f, parameters);
		assert(std::isfinite(result.mainLeft));
		assert(std::isfinite(result.mainRight));
		assert(std::isfinite(result.ghostLeft));
		assert(std::isfinite(result.ghostRight));
		assert(std::isfinite(result.motion));
		assert(std::fabs(result.mainLeft) <= 10.001f);
		assert(std::fabs(result.mainRight) <= 10.001f);
		assert(result.motion >= 0.f && result.motion <= 1.f);
	}
}

static void testMemoryLifecycle() {
	SpectralMemoryEngine engine;
	Parameters parameters;
	parameters.imprint = 0.92f;
	parameters.erosion = 0.05f;
	parameters.bloom = 0.28f;
	parameters.trace = 1.f;
	feedTone(engine, parameters, 48000, 220.f, 4.f);
	float learnedEnergy = engine.memoryEnergy();
	assert(learnedEnergy > 1e-4f);

	parameters.sealed = true;
	float sealedChecksum = memoryChecksum(engine);
	feedTone(engine, parameters, 24000, 997.f, 5.f);
	float heldChecksum = memoryChecksum(engine);
	assert(std::fabs(heldChecksum - sealedChecksum) < 1e-6f);

	engine.requestWash(0.f);
	feedTone(engine, parameters, 96000, 0.f, 0.f);
	assert(engine.memoryEnergy() < learnedEnergy * 0.22f);
}

static void testTraversalAndSanitization() {
	SpectralMemoryEngine engine;
	for (int i = 0; i < 10000; ++i) {
		engine.advanceClock((i & 1) ? -1.f : 1.f);
		assert(engine.clockPosition() >= 0.f && engine.clockPosition() <= 1.f);
	}
	engine.setClockPosition(std::numeric_limits<float>::infinity());
	assert(std::isfinite(engine.clockPosition()));

	Parameters parameters;
	parameters.age = std::numeric_limits<float>::quiet_NaN();
	parameters.imprint = std::numeric_limits<float>::infinity();
	parameters.trace = -std::numeric_limits<float>::infinity();
	for (int i = 0; i < 4096; ++i) {
		Outputs result = engine.process(
			std::numeric_limits<float>::quiet_NaN(),
			std::numeric_limits<float>::infinity(),
			99.f, -99.f, parameters);
		assert(std::isfinite(result.mainLeft));
		assert(std::isfinite(result.mainRight));
	}
}

int main() {
	testFftRoundTrip();
	testMemoryLifecycle();
	testTraversalAndSanitization();
	std::cout << "Palimpsest engine tests passed\n";
	return 0;
}
