#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

// One point is a synchronized view of the signal at each important stage of
// Bad Sector's real DSP path. The UI never invents display data: it only reads
// these samples, which are published by the audio thread at a reduced rate.
struct BsScopeFrame {
	float input = 0.f;
	float aligned = 0.f;
	float bentBroken = 0.f;
	float corrupted = 0.f;
	float output = 0.f;
};

static constexpr int BS_SCOPE_POINTS = 256;
static_assert((BS_SCOPE_POINTS & (BS_SCOPE_POINTS - 1)) == 0,
	"scope ring size must be a power of two");

inline float bsScopeSanitize(float v) {
	if (!std::isfinite(v)) return 0.f;
	return v < -4.f ? -4.f : (v > 4.f ? 4.f : v);
}

inline uint16_t bsScopeChecksum(const BsScopeFrame* frames, int count) {
	uint16_t checksum = 0;
	for (int i = 0; i < count; i++) {
		float v = bsScopeSanitize(frames[i].output);
		int sample = (int) std::lround(v * 8192.f);
		uint16_t word = (uint16_t)(sample & 0xffff);
		checksum = (uint16_t)((checksum << 1) | (checksum >> 15));
		checksum ^= word;
	}
	return checksum;
}

// Per-slot sequence counters make the five values an atomic snapshot without
// ever blocking the real-time audio thread. The UI retries a slot if it catches
// the writer halfway through publishing it.
struct BsScopeTelemetry {
	std::array<std::atomic<uint32_t>, BS_SCOPE_POINTS> sequence;
	std::array<std::atomic<float>, BS_SCOPE_POINTS> input;
	std::array<std::atomic<float>, BS_SCOPE_POINTS> aligned;
	std::array<std::atomic<float>, BS_SCOPE_POINTS> bentBroken;
	std::array<std::atomic<float>, BS_SCOPE_POINTS> corrupted;
	std::array<std::atomic<float>, BS_SCOPE_POINTS> output;
	std::atomic<uint32_t> writeCount;
	std::atomic<bool> filled;

	BsScopeTelemetry() : writeCount(0), filled(false) {
		clear();
	}

	void clear() {
		for (int i = 0; i < BS_SCOPE_POINTS; i++) {
			sequence[i].store(0, std::memory_order_relaxed);
			input[i].store(0.f, std::memory_order_relaxed);
			aligned[i].store(0.f, std::memory_order_relaxed);
			bentBroken[i].store(0.f, std::memory_order_relaxed);
			corrupted[i].store(0.f, std::memory_order_relaxed);
			output[i].store(0.f, std::memory_order_relaxed);
		}
		writeCount.store(0, std::memory_order_release);
		filled.store(false, std::memory_order_release);
	}

	void publish(const BsScopeFrame& frame) {
		uint32_t write = writeCount.load(std::memory_order_relaxed);
		int slot = (int)(write & (BS_SCOPE_POINTS - 1));
		uint32_t seq = sequence[slot].load(std::memory_order_relaxed);
		if (seq & 1u) seq++;
		sequence[slot].store(seq + 1u, std::memory_order_release);
		input[slot].store(bsScopeSanitize(frame.input), std::memory_order_relaxed);
		aligned[slot].store(bsScopeSanitize(frame.aligned), std::memory_order_relaxed);
		bentBroken[slot].store(bsScopeSanitize(frame.bentBroken), std::memory_order_relaxed);
		corrupted[slot].store(bsScopeSanitize(frame.corrupted), std::memory_order_relaxed);
		output[slot].store(bsScopeSanitize(frame.output), std::memory_order_relaxed);
		sequence[slot].store(seq + 2u, std::memory_order_release);
		if (write + 1u >= (uint32_t)BS_SCOPE_POINTS)
			filled.store(true, std::memory_order_release);
		writeCount.store(write + 1u, std::memory_order_release);
	}

	int snapshot(std::array<BsScopeFrame, BS_SCOPE_POINTS>& frames) const {
		uint32_t write = writeCount.load(std::memory_order_acquire);
		int count = filled.load(std::memory_order_acquire)
			? BS_SCOPE_POINTS : (int)write;
		uint32_t first = write - (uint32_t)count;
		for (int i = 0; i < count; i++) {
			int slot = (int)((first + (uint32_t)i) & (BS_SCOPE_POINTS - 1));
			BsScopeFrame frame;
			bool valid = false;
			for (int attempt = 0; attempt < 3 && !valid; attempt++) {
				uint32_t before = sequence[slot].load(std::memory_order_acquire);
				if (before & 1u) continue;
				frame.input = input[slot].load(std::memory_order_relaxed);
				frame.aligned = aligned[slot].load(std::memory_order_relaxed);
				frame.bentBroken = bentBroken[slot].load(std::memory_order_relaxed);
				frame.corrupted = corrupted[slot].load(std::memory_order_relaxed);
				frame.output = output[slot].load(std::memory_order_relaxed);
				uint32_t after = sequence[slot].load(std::memory_order_acquire);
				valid = before == after && !(after & 1u);
			}
			frames[i] = valid ? frame : BsScopeFrame{};
		}
		return count;
	}
};
