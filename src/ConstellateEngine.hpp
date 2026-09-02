#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace constellate {

struct Choice {
	int event = 0;
	float confidence = 0.f;
	float probability = 0.25f;
};

// HOLD freezes only the learner. Routing, playback, and generation remain live.
inline bool learningActive(bool learningEnabled, bool holdEnabled) {
	return learningEnabled && !holdEnabled;
}

inline float effectiveMorph(float manual, float cvVoltage, float attenuverter) {
	float value = manual + cvVoltage * 0.1f * attenuverter;
	return value < 0.f ? 0.f : (value > 1.f ? 1.f : value);
}

// A compact variable-order Markov model for four event streams. Learning and
// playback keep separate histories so generated choices never contaminate the
// model being learned from the patch.
struct Engine {
	static const int CHANNELS = 4;
	static const int MAX_ORDER = 4;
	static const int MAX_CONTEXTS = 256; // 4^4

	float weights[MAX_ORDER][MAX_CONTEXTS][CHANNELS];
	float baseWeights[CHANNELS];
	float delayMean[CHANNELS][CHANNELS];
	float delayWeight[CHANNELS][CHANNELS];
	int learnHistory[MAX_ORDER]; // newest first
	int playHistory[MAX_ORDER];  // newest first
	int learnLength = 0;
	int playLength = 0;
	int lastObserved = -1;
	double lastObservedTime = -1.0;
	float globalInterval = 0.5f;
	float globalIntervalWeight = 0.f;
	uint32_t rngState = 0xC057E11Au;
	uint32_t observations = 0;

	Engine() {
		clear();
	}

	static float clamp01(float v) {
		return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
	}

	void clear() {
		std::memset(weights, 0, sizeof(weights));
		std::memset(baseWeights, 0, sizeof(baseWeights));
		std::memset(delayWeight, 0, sizeof(delayWeight));
		for (int from = 0; from < CHANNELS; ++from)
			for (int to = 0; to < CHANNELS; ++to)
				delayMean[from][to] = 0.5f;
		for (int i = 0; i < MAX_ORDER; ++i) {
			learnHistory[i] = 0;
			playHistory[i] = 0;
		}
		learnLength = 0;
		playLength = 0;
		lastObserved = -1;
		lastObservedTime = -1.0;
		globalInterval = 0.5f;
		globalIntervalWeight = 0.f;
		rngState = 0xC057E11Au;
		observations = 0;
	}

	void reseed(uint32_t seed) {
		rngState = seed ? seed : 0xC057E11Au;
	}

	float random01() {
		rngState ^= rngState << 13;
		rngState ^= rngState >> 17;
		rngState ^= rngState << 5;
		return (rngState & 0xFFFFFFu) / float(0x1000000u);
	}

	static int contextIndex(const int* history, int order) {
		int index = 0;
		for (int i = order - 1; i >= 0; --i)
			index = index * CHANNELS + history[i];
		return index;
	}

	static void push(int* history, int& length, int event) {
		for (int i = MAX_ORDER - 1; i > 0; --i)
			history[i] = history[i - 1];
		history[0] = event;
		length = std::min(length + 1, MAX_ORDER);
	}

	void pushPlayback(int event) {
		if (event >= 0 && event < CHANNELS)
			push(playHistory, playLength, event);
	}

	void resetPlaybackToLearned() {
		playLength = learnLength;
		for (int i = 0; i < MAX_ORDER; ++i)
			playHistory[i] = learnHistory[i];
	}

	void observe(int event, double nowSeconds) {
		if (event < 0 || event >= CHANNELS)
			return;

		if (lastObserved >= 0 && lastObservedTime >= 0.0) {
			float interval = (float) (nowSeconds - lastObservedTime);
			interval = std::max(0.002f, std::min(interval, 60.f));
			float& weight = delayWeight[lastObserved][event];
			float& mean = delayMean[lastObserved][event];
			float alpha = weight < 1.f ? 1.f : 0.12f;
			mean += (interval - mean) * alpha;
			weight = std::min(weight + 1.f, 10000.f);

			float globalAlpha = globalIntervalWeight < 1.f ? 1.f : 0.06f;
			globalInterval += (interval - globalInterval) * globalAlpha;
			globalInterval = std::max(0.01f, std::min(globalInterval, 30.f));
			globalIntervalWeight = std::min(globalIntervalWeight + 1.f, 10000.f);
		}

		for (int order = 1; order <= std::min(learnLength, MAX_ORDER); ++order) {
			int context = contextIndex(learnHistory, order);
			float* row = weights[order - 1][context];
			row[event] += 1.f;
			float total = row[0] + row[1] + row[2] + row[3];
			if (total > 10000.f)
				for (int i = 0; i < CHANNELS; ++i)
					row[i] *= 0.5f;
		}

		baseWeights[event] += 1.f;
		if (baseWeights[event] > 10000.f)
			for (int i = 0; i < CHANNELS; ++i)
				baseWeights[i] *= 0.5f;

		push(learnHistory, learnLength, event);
		lastObserved = event;
		lastObservedTime = nowSeconds;
		++observations;
	}

	bool hasSequence() const {
		return observations >= 2;
	}

	float rowTotal(int order, int context) const {
		if (order < 1 || order > MAX_ORDER || context < 0 || context >= MAX_CONTEXTS)
			return 0.f;
		const float* row = weights[order - 1][context];
		return row[0] + row[1] + row[2] + row[3];
	}

	Choice choose(int requestedOrder, float affinity, float drift) {
		if (playLength == 0 && learnLength > 0)
			resetPlaybackToLearned();

		float raw[CHANNELS] = {0.f, 0.f, 0.f, 0.f};
		bool contextual = false;
		int highest = std::min(std::max(requestedOrder, 1), std::min(playLength, MAX_ORDER));
		for (int order = highest; order >= 1; --order) {
			int context = contextIndex(playHistory, order);
			if (rowTotal(order, context) > 0.f) {
				for (int i = 0; i < CHANNELS; ++i)
					raw[i] = weights[order - 1][context][i];
				contextual = true;
				break;
			}
		}

		if (!contextual)
			for (int i = 0; i < CHANNELS; ++i)
				raw[i] = baseWeights[i];

		float total = raw[0] + raw[1] + raw[2] + raw[3];
		if (total <= 0.f)
			for (int i = 0; i < CHANNELS; ++i)
				raw[i] = 1.f;

		float exponent = 0.5f + clamp01(affinity) * 3.5f;
		float shaped[CHANNELS];
		float shapedTotal = 0.f;
		for (int i = 0; i < CHANNELS; ++i) {
			shaped[i] = std::pow(std::max(raw[i], 0.f), exponent);
			shapedTotal += shaped[i];
		}
		if (shapedTotal <= 0.f) {
			for (int i = 0; i < CHANNELS; ++i)
				shaped[i] = 1.f;
			shapedTotal = 4.f;
		}

		float probabilities[CHANNELS];
		float driftAmount = clamp01(drift);
		for (int i = 0; i < CHANNELS; ++i)
			probabilities[i] = (1.f - driftAmount) * (shaped[i] / shapedTotal) + driftAmount * 0.25f;

		float draw = random01();
		int event = CHANNELS - 1;
		float cumulative = 0.f;
		for (int i = 0; i < CHANNELS; ++i) {
			cumulative += probabilities[i];
			if (draw < cumulative) {
				event = i;
				break;
			}
		}

		Choice choice;
		choice.event = event;
		choice.probability = probabilities[event];
		choice.confidence = contextual
			? clamp01((probabilities[event] - 0.25f) / 0.75f)
			: 0.f;
		pushPlayback(event);
		return choice;
	}

	float transitionProbability(int from, int to) const {
		if (from < 0 || from >= CHANNELS || to < 0 || to >= CHANNELS)
			return 0.f;
		float total = rowTotal(1, from);
		return total > 0.f ? weights[0][from][to] / total : 0.f;
	}

	float transitionEvidence(int from, int to) const {
		if (from < 0 || from >= CHANNELS || to < 0 || to >= CHANNELS)
			return 0.f;
		return weights[0][from][to];
	}

	float intervalFor(int from, int to) const {
		if (from >= 0 && from < CHANNELS && to >= 0 && to < CHANNELS && delayWeight[from][to] > 0.f)
			return delayMean[from][to];
		return globalInterval;
	}
};

} // namespace constellate
