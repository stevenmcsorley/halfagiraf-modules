#pragma once

#include <algorithm>

// Macro Bend is a clock-division decision, not a free-running modulation.
// The six equal knob spans expose the cumulative manual palette:
// Reverse, Octaves, 2 Octaves, Tape Stop, Slew, Everything.
inline float bsBendZoneAmount(float amount, int zone) {
	amount = std::max(0.f, std::min(1.f, amount));
	return std::max(0.f, std::min(1.f, amount * 6.f - (zone - 1)));
}

inline float bsBendReverseChance(float amount) {
	float everything = bsBendZoneAmount(amount, 6);
	return bsBendZoneAmount(amount, 1) * (0.35f + 0.30f * everything);
}

inline float bsBendOctaveChance(float amount) {
	float everything = bsBendZoneAmount(amount, 6);
	// The first octave range is deliberately sparse. At the user's measured
	// 0.2807 setting this is about 8%, matching the reference's occasional
	// varispeed gesture instead of the old 34% fast/slow churn.
	return bsBendZoneAmount(amount, 2) * (0.12f + 0.18f * everything);
}

inline float bsBendTwoOctaveChance(float amount) {
	float everything = bsBendZoneAmount(amount, 6);
	return bsBendZoneAmount(amount, 3) * (0.40f + 0.30f * everything);
}

inline float bsBendTapeStopChance(float amount) {
	float everything = bsBendZoneAmount(amount, 6);
	return bsBendZoneAmount(amount, 4) * (0.30f + 0.50f * everything);
}

struct BsBendDecision {
	float speed = 1.f;
	bool reverse = false;
	bool tapeStop = false;
	float slew = 0.f;
};

// Rolls are passed in so the policy remains pure and regression-testable.
// A decision applies unchanged for the complete clock division.
inline BsBendDecision bsBendDecision(float amount,
	float reverseRoll,
	float twoOctaveRoll, float twoOctaveDirectionRoll,
	float octaveRoll, float octaveDirectionRoll,
	float tapeStopRoll) {
	BsBendDecision out;
	out.reverse = reverseRoll < bsBendReverseChance(amount);
	if (twoOctaveRoll < bsBendTwoOctaveChance(amount))
		out.speed = twoOctaveDirectionRoll < 0.5f ? 0.25f : 4.f;
	else if (octaveRoll < bsBendOctaveChance(amount))
		out.speed = octaveDirectionRoll < 0.5f ? 0.5f : 2.f;
	out.tapeStop = tapeStopRoll < bsBendTapeStopChance(amount);
	out.slew = bsBendZoneAmount(amount, 5);
	return out;
}
