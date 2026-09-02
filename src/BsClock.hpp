#pragma once
#include <algorithm>
#include <cmath>

// Pure external-clock engine shared by Bad Sector and its clock tests.
// It follows incoming edges while continuously running the last measured
// phase in the background. If an edge is missed, predicted output ticks keep
// the grid moving; a late edge that arrives just after a predicted tick
// re-phases without producing a back-to-back trigger.

inline float bsClockClamp(float x, float lo, float hi) {
	return x < lo ? lo : (x > hi ? hi : x);
}

inline int bsClockDivisor(int d) {
	return d <= 3 ? (16 >> d) : 1;
}

inline int bsClockMultiplier(int d) {
	static const int MULT[4] = {2, 3, 4, 8};
	return d >= 5 ? MULT[d - 5] : 1;
}

inline float bsExtPeriodFor(int d, float inputPeriod) {
	d = std::max(0, std::min(8, d));
	if (d <= 3)
		return bsClockClamp(inputPeriod * bsClockDivisor(d), 0.001f, 120.f);
	if (d == 4)
		return bsClockClamp(inputPeriod, 0.001f, 30.f);
	return bsClockClamp(inputPeriod / bsClockMultiplier(d), 0.001f, 30.f);
}

struct BsClockResult {
	bool tick = false;
	bool divisionChanged = false;
	bool clockLost = false;
	bool consumedReset = false;
	bool firstEdge = false;
	float period = 0.5f;
};

struct BsExternalClock {
	double phase = 0.0;
	float inputPeriod = 0.5f;
	double sinceEdge = 0.0;
	bool haveEdge = false;
	bool haveOutputTick = false;
	int lastDiv = 4;
	int edgeCount = -1;

	void reset() {
		phase = 0.0;
		inputPeriod = 0.5f;
		sinceEdge = 0.0;
		haveEdge = false;
		haveOutputTick = false;
		lastDiv = 4;
		edgeCount = -1;
	}

	BsClockResult process(float dt, int d, bool edge, bool resetOnEdge) {
		BsClockResult out;
		d = std::max(0, std::min(8, d));

		double oldOutPeriod = bsExtPeriodFor(lastDiv, inputPeriod);
		if (d != lastDiv) {
			double newOutPeriod = bsExtPeriodFor(d, inputPeriod);
			if (haveEdge && d >= 4) {
				// Multiplications and x1 are anchored to the most recent input
				// edge. Move to the true phase of the newly selected grid.
				double cycles = sinceEdge / newOutPeriod;
				phase = cycles - std::floor(cycles);
			}
			else {
				// Divisions preserve real elapsed time since the last output.
				phase = std::max(0.0, std::min(0.999999, phase * oldOutPeriod / newOutPeriod));
			}
			lastDiv = d;
			oldOutPeriod = newOutPeriod;
			out.divisionChanged = true;
		}

		out.period = bsExtPeriodFor(d, inputPeriod);
		sinceEdge += dt;
		phase += dt / out.period;

		bool wasLost = haveEdge && sinceEdge >= inputPeriod * 4.f;
		if (edge) {
			bool firstInputEdge = !haveEdge;
			bool firstOrReturningEdge = firstInputEdge || wasLost;
			out.firstEdge = firstInputEdge;
			double measured = sinceEdge;
			double periodBeforeMeasure = out.period;

			// A long gap is not a valid tempo measurement. Keep free-running
			// at the last rate and learn the new rate from the next edge.
			if (haveEdge && !wasLost)
				inputPeriod = bsClockClamp((float) measured, 0.001f, 30.f);
			haveEdge = true;
			sinceEdge = 0.f;

			out.period = bsExtPeriodFor(d, inputPeriod);
			if (periodBeforeMeasure > 0.f)
				phase *= periodBeforeMeasure / out.period;

			if (resetOnEdge || firstOrReturningEdge) {
				edgeCount = 0;
				out.consumedReset = resetOnEdge;
			}
			else {
				edgeCount++;
			}

			bool edgeIsOutput = d >= 4
				|| resetOnEdge
				|| firstOrReturningEdge
				|| (edgeCount % bsClockDivisor(d)) == 0;
			if (edgeIsOutput) {
				// If the predictor fired less than a quarter-period ago, do not
				// double-trigger on a slightly late/reconnecting edge. Re-phase
				// from the edge either way so subsequent ticks follow the source.
				bool emit = resetOnEdge || !haveOutputTick || phase >= 0.25;
				phase = 0.0;
				if (emit) {
					out.tick = true;
					haveOutputTick = true;
				}
			}
		}

		// After the first real edge, the predictor is active continuously rather
		// than only after a timeout. That makes a disappearing cable continue at
		// the last rate without one late beat and a short recovery beat.
		if (haveEdge && !out.tick && phase >= 1.0) {
			phase -= std::floor(phase);
			out.tick = true;
			haveOutputTick = true;
		}

		out.clockLost = haveEdge && sinceEdge >= inputPeriod * 4.f;
		return out;
	}
};
