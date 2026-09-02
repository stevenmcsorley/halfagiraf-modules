#pragma once
#include <cstdint>

// Exact clock-phase repeat grid, shared by the module and tests/timing_test.
// Window k of `subs` spans [ceil(k*len/subs), ceil((k+1)*len/subs)) in whole
// samples: exactly `subs` windows tile a division of `len` samples, and every
// boundary sits within one sample of the ideal rational fraction k*len/subs.
// (A floored fixed window length accumulates early drift and fires an extra
// truncated retrigger whenever len is not divisible by subs.)

inline int bsGridIndex(int t, int len, int subs) {
	if (len < 1 || subs < 1 || t < 0) return 0;
	// A late/jittered clock edge can leave playback running beyond the
	// acquired division. Hold the final window instead of inventing windows
	// `subs`, `subs + 1`, ... while waiting for the next authoritative tick.
	if (t >= len) return subs - 1;
	return (int)(((int64_t) t * subs) / len);
}

inline int bsGridStart(int k, int len, int subs) {
	if (len < 1 || subs < 1 || k < 0) return 0;
	if (k > subs) k = subs;
	return (int)(((int64_t) k * len + subs - 1) / subs);
}

// While a Time change lengthens the current clock cycle, keep replaying the
// last acquired division instead of parking its window envelope at zero until
// the later boundary arrives. The new section is still acquired only on that
// authoritative boundary.
inline int bsGridPlaybackTime(int t, int len) {
	if (len < 1 || t < 0) return 0;
	return t % len;
}

inline bool bsGridIsBoundary(int t, int len, int subs) {
	if (len < 1 || subs < 1 || t < 0 || t >= len) return false;
	int win = bsGridIndex(t, len, subs);
	return t == bsGridStart(win, len, subs);
}

// Advance the live repeat grid. A pending repeat-count change is applied on
// the next boundary of EITHER the active grid or the requested grid. This
// keeps every switch on a rational TIME boundary while avoiding a full-period
// wait when turning Repeats up from 1 (up to 16 seconds in internal mode).
// The index is recomputed on the new grid in the switching sample.
inline bool bsGridAdvance(int t, int len, int targetSubs,
		int& activeSubs, int& lastWin, int& winIdx) {
	if (targetSubs < 1) targetSubs = 1;
	if (activeSubs < 1) activeSubs = targetSubs;

	int activeWin = bsGridIndex(t, len, activeSubs);
	bool activeBoundary = activeWin != lastWin;
	bool targetBoundary = targetSubs != activeSubs
		&& bsGridIsBoundary(t, len, targetSubs);
	bool targetBoundaryNext = targetSubs != activeSubs && t + 1 < len
		&& bsGridIsBoundary(t + 1, len, targetSubs);
	bool activeBoundaryPrevious = targetSubs != activeSubs && t > 0
		&& bsGridIsBoundary(t - 1, len, activeSubs)
		&& lastWin == bsGridIndex(t - 1, len, activeSubs);

	// Arbitrary integer grids occasionally place an old-grid boundary exactly
	// one sample before a requested-grid boundary. Skip that one-sample old
	// window and switch on the requested boundary instead of double-triggering.
	if (activeBoundary && !targetBoundary && targetBoundaryNext) {
		winIdx = activeWin;
		return false;
	}
	// A control/CV change can itself arrive exactly one sample after an old
	// boundary. Do not turn that into a second adjacent trigger either; wait
	// for the following boundary from either grid.
	if (!activeBoundary && targetBoundary && activeBoundaryPrevious) {
		winIdx = activeWin;
		return false;
	}

	if (targetSubs != activeSubs && (activeBoundary || targetBoundary)) {
		activeSubs = targetSubs;
		winIdx = bsGridIndex(t, len, activeSubs);
		lastWin = winIdx;
		return true;
	}

	winIdx = activeWin;
	if (!activeBoundary)
		return false;
	lastWin = activeWin;
	return true;
}
