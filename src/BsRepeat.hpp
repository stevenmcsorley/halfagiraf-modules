#pragma once

#include <algorithm>
#include <cmath>

// Bad Sector's Repeat control is an integer subdivision count, not a
// restricted list of "musical" values. This curve retains Bad Sector's useful
// low-count knob resolution while making every integer from 1 through 1024
// reachable (including the manual's explicit 10-repeat example).
inline int bsRepeatCount(float normalized) {
	normalized = std::max(0.f, std::min(1.f, normalized));
	float count = std::pow(2.f, 10.f * std::pow(normalized, 1.5f));
	return std::max(1, std::min(1024, (int) std::round(count)));
}

// Macro Break's "More Subsections" zone chooses an integer strictly above
// the Repeat knob setting, up to four times the current count.
inline int bsBreakMoreSubsections(int current, float roll) {
	current = std::max(1, std::min(1024, current));
	if (current >= 1024) return 1024;
	roll = std::max(0.f, std::min(1.f, roll));
	int high = std::min(1024, std::max(current + 1, current * 4));
	int span = high - current;
	int offset = 1 + (int) (roll * span);
	return current + std::min(span, offset);
}

// The Audio Rate zone selects the same broad range previously represented by
// a few table entries, now without excluding the integers between them.
inline int bsBreakAudioRateSubsections(float roll) {
	roll = std::max(0.f, std::min(1.f, roll));
	return 32 + std::min(96, (int) (roll * 97.f));
}
