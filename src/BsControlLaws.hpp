#pragma once

#include <algorithm>

inline float bsControlClamp01(float value) {
	return std::max(0.f, std::min(1.f, value));
}

// Hardware Shift+effect controls are unipolar attenuators: CCW blocks CV and
// CW passes it at full depth. They are not bipolar attenuverters.
inline float bsCvAttenuation(float stored) {
	return bsControlClamp01(stored);
}

// Hardware Stereo Enhancement moves the wet buffer from centred mono at CCW
// to its original independent left/right channels at CW. It never boosts the
// side component beyond the source stereo image.
inline void bsStereoEnhance(float& left, float& right, float spread) {
	spread = bsControlClamp01(spread);
	float mid = 0.5f * (left + right);
	float side = 0.5f * (left - right) * spread;
	left = mid + side;
	right = mid - side;
}
