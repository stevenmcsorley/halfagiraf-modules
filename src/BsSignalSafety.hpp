#pragma once

#include <cmath>

// One non-finite sample must never poison a stateful filter or circular
// buffer. Rack normally carries audio around +/-5 V; this normalized guard is
// deliberately generous while still containing corrupt upstream values.
inline float bsSanitizeAudio(float x) {
	if (!std::isfinite(x))
		return 0.f;
	if (x < -8.f)
		return -8.f;
	if (x > 8.f)
		return 8.f;
	return x;
}

inline bool bsFiniteAudioState(float a, float b, float c, float d) {
	return std::isfinite(a) && std::isfinite(b)
		&& std::isfinite(c) && std::isfinite(d);
}
