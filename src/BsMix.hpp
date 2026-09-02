#pragma once

#include <algorithm>

struct BsMixGains {
	float liveDry = 0.f;
	float bufferedDry = 0.f;
	float wet = 0.f;
};

inline float bsMixClamp01(float x) {
	return std::max(0.f, std::min(1.f, x));
}

// Wet playback necessarily uses the division that has just been captured.
// Cross the dry monitor onto that same division in the first 10% of MIX so
// useful blended settings do not combine live audio with audio one TIME
// period earlier. Fully dry remains a genuine zero-latency monitor.
inline BsMixGains bsMixGains(float mix) {
	mix = bsMixClamp01(mix);
	float align = bsMixClamp01(mix * 10.f);
	align = align * align * (3.f - 2.f * align);  // smoothstep
	float dry = 1.f - mix;
	BsMixGains out;
	out.liveDry = dry * (1.f - align);
	out.bufferedDry = dry * align;
	out.wet = mix;
	return out;
}

// Rack patches do not serialize multi-megabyte audio history. On creation,
// patch reload, sample-rate change or Clear Buffer, the latency-aligned dry
// path and wet path therefore both point at silence until a complete TIME
// window has been recorded. Keep a unity live monitor during that interval,
// then crossfade into the requested Mix without changing its steady state.
inline BsMixGains bsPrimedMixGains(float mix, float primedFade) {
	BsMixGains requested = bsMixGains(mix);
	float ready = bsMixClamp01(primedFade);
	ready = ready * ready * (3.f - 2.f * ready);  // smoothstep
	BsMixGains out;
	out.liveDry = (1.f - ready) + requested.liveDry * ready;
	out.bufferedDry = requested.bufferedDry * ready;
	out.wet = requested.wet * ready;
	return out;
}

inline bool bsBufferCaptureReady(int recordedSamples, int sectionLength) {
	return sectionLength > 0 && recordedSamples >= sectionLength;
}

// A restored Freeze request must wait until the non-serialized buffer has
// acquired valid audio, otherwise it freezes an empty buffer forever.
inline bool bsEffectiveFreeze(bool requested, bool bufferPrimed) {
	return requested && bufferPrimed;
}
