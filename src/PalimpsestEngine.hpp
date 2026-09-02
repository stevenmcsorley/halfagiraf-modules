#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>

namespace palimpsest {

static const int FFT_SIZE = 1024;
static const int HOP_SIZE = 256;
static const int BIN_COUNT = FFT_SIZE / 2 + 1;
static const int MEMORY_LAYERS = 4;
static const int DISPLAY_BANDS = 48;
static const float PI = 3.14159265358979323846f;

inline float clamp(float value, float low, float high) {
	return std::max(low, std::min(high, value));
}

inline float lerp(float a, float b, float amount) {
	return a + (b - a) * amount;
}

inline bool finite(float value) {
	return std::isfinite(value);
}

inline float softClip(float value, float limit = 10.f) {
	if (!finite(value))
		return 0.f;
	return limit * std::tanh(value / limit);
}

struct Parameters {
	float age = 0.35f;
	float imprint = 0.55f;
	float erosion = 0.18f;
	float gravity = 0.35f;
	float drift = 0.18f;
	float bloom = 0.4f;
	float trace = 0.55f;
	float transposeVoltage = 0.f;
	float transposeDepth = 1.f;
	bool sealed = false;
	bool excitePatched = false;
};

struct Outputs {
	float mainLeft = 0.f;
	float mainRight = 0.f;
	float ghostLeft = 0.f;
	float ghostRight = 0.f;
	float motion = 0.f;
};

class Radix2FFT {
public:
	static void transform(std::array<std::complex<float>, FFT_SIZE>& data, bool inverse) {
		for (int i = 1, j = 0; i < FFT_SIZE; ++i) {
			int bit = FFT_SIZE >> 1;
			for (; j & bit; bit >>= 1)
				j ^= bit;
			j ^= bit;
			if (i < j)
				std::swap(data[i], data[j]);
		}

		for (int length = 2; length <= FFT_SIZE; length <<= 1) {
			float angle = (inverse ? 2.f : -2.f) * PI / (float) length;
			std::complex<float> root(std::cos(angle), std::sin(angle));
			for (int offset = 0; offset < FFT_SIZE; offset += length) {
				std::complex<float> twiddle(1.f, 0.f);
				for (int i = 0; i < length / 2; ++i) {
					std::complex<float> even = data[offset + i];
					std::complex<float> odd = data[offset + i + length / 2] * twiddle;
					data[offset + i] = even + odd;
					data[offset + i + length / 2] = even - odd;
					twiddle *= root;
				}
			}
		}

		if (inverse) {
			const float scale = 1.f / (float) FFT_SIZE;
			for (int i = 0; i < FFT_SIZE; ++i)
				data[i] *= scale;
		}
	}
};

class SpectralMemoryEngine {
public:
	SpectralMemoryEngine() {
		for (int i = 0; i < FFT_SIZE; ++i)
			window_[i] = 0.5f - 0.5f * std::cos(2.f * PI * (float) i / (float) FFT_SIZE);

		uint32_t random = 0x7182a91du;
		for (int bin = 0; bin < BIN_COUNT; ++bin) {
			random = random * 1664525u + 1013904223u;
			float unit = (float) ((random >> 8) & 0xffffffu) / 16777215.f;
			driftSeed_[bin] = unit * 2.f - 1.f;
			random = random * 1664525u + 1013904223u;
			unit = (float) ((random >> 8) & 0xffffffu) / 16777215.f;
			erosionSeed_[bin] = 0.25f + unit * 1.75f;
		}
		setSampleRate(48000.f);
		clearMemory();
		resetRuntime();
	}

	void setSampleRate(float sampleRate) {
		sampleRate_ = clamp(sampleRate, 8000.f, 384000.f);
		frameSeconds_ = (float) HOP_SIZE / sampleRate_;
		exciteDecay_ = std::exp(-1.f / (sampleRate_ * 0.82f));
	}

	void resetRuntime() {
		writeIndex_ = 0;
		hopCounter_ = 0;
		frameReady_ = false;
		clockPosition_ = 0.5f;
		driftPhase_ = 0.f;
		exciteEnvelope_ = 0.f;
		washEnvelope_ = 0.f;
		motion_ = 0.f;
		framePeak_ = 0.f;
		for (int channel = 0; channel < 2; ++channel) {
			channels_[channel].input.fill(0.f);
			channels_[channel].output.fill(0.f);
			channels_[channel].dry.fill(0.f);
			channels_[channel].phase.fill(0.f);
			channels_[channel].dcInput = 0.f;
			channels_[channel].dcOutput = 0.f;
		}
	}

	void clearMemory() {
		for (int channel = 0; channel < 2; ++channel) {
			for (int layer = 0; layer < MEMORY_LAYERS; ++layer)
				channels_[channel].memory[layer].fill(0.f);
			channels_[channel].currentMagnitude.fill(0.f);
			channels_[channel].energy = 0.f;
			channels_[channel].centroid = 0.f;
		}
		for (int layer = 0; layer < MEMORY_LAYERS; ++layer)
			display_[layer].fill(0.f);
		memoryActive_ = false;
	}

	void triggerExcite(float trim) {
		float strength = clamp(1.f + trim, 0.05f, 2.f);
		exciteEnvelope_ = std::max(exciteEnvelope_, clamp(strength, 0.f, 1.35f));
	}

	void advanceClock(float trim) {
		float normalized = clamp(trim * 0.5f + 0.5f, 0.f, 1.f);
		float step = 0.04f + 0.17f * normalized;
		clockPosition_ += step;
		clockPosition_ -= std::floor(clockPosition_);
	}

	void resetTraversal(float trim) {
		float depth = clamp(1.f + trim, 0.f, 1.f);
		clockPosition_ = lerp(clockPosition_, 0.5f, depth);
		driftPhase_ *= 1.f - depth;
		for (int channel = 0; channel < 2; ++channel) {
			for (int bin = 0; bin < BIN_COUNT; ++bin)
				channels_[channel].phase[bin] *= 1.f - depth;
		}
	}

	void requestWash(float trim) {
		washEnvelope_ = std::max(washEnvelope_, clamp(1.f + trim, 0.08f, 2.f));
	}

	Outputs process(float inputLeftVolts, float inputRightVolts,
		float leftGainDb, float rightGainDb, const Parameters& parameters) {
		Parameters p = sanitized(parameters);
		float leftGain = std::pow(10.f, clamp(leftGainDb, -12.f, 12.f) / 20.f);
		float rightGain = std::pow(10.f, clamp(rightGainDb, -12.f, 12.f) / 20.f);
		float liveLeft = finite(inputLeftVolts) ? clamp(inputLeftVolts * leftGain, -20.f, 20.f) : 0.f;
		float liveRight = finite(inputRightVolts) ? clamp(inputRightVolts * rightGain, -20.f, 20.f) : 0.f;

		Channel& left = channels_[0];
		Channel& right = channels_[1];
		float wetLeft = left.output[writeIndex_];
		float wetRight = right.output[writeIndex_];
		left.output[writeIndex_] = 0.f;
		right.output[writeIndex_] = 0.f;

		float dryLeft = left.dry[writeIndex_];
		float dryRight = right.dry[writeIndex_];
		left.dry[writeIndex_] = liveLeft;
		right.dry[writeIndex_] = liveRight;
		left.input[writeIndex_] = clamp(liveLeft / 5.f, -3.f, 3.f);
		right.input[writeIndex_] = clamp(liveRight / 5.f, -3.f, 3.f);
		framePeak_ = std::max(framePeak_, std::max(std::fabs(liveLeft), std::fabs(liveRight)));

		writeIndex_ = (writeIndex_ + 1) & (FFT_SIZE - 1);
		if (++hopCounter_ >= HOP_SIZE) {
			hopCounter_ = 0;
			// A genuinely empty, disconnected memory should be effectively free.
			// Once audio arrives or memory exists, the complete STFT path resumes.
			if (framePeak_ > 1e-5f || memoryActive_ || washEnvelope_ > 1e-5f)
				processSpectralFrame(p);
			framePeak_ = 0.f;
			frameReady_ = true;
		}

		exciteEnvelope_ *= exciteDecay_;
		float gate = p.excitePatched ? clamp(exciteEnvelope_, 0.f, 1.f) : 1.f;
		wetLeft *= gate;
		wetRight *= gate;

		wetLeft = dcBlock(left, wetLeft) * 5.f;
		wetRight = dcBlock(right, wetRight) * 5.f;
		wetLeft = softClip(wetLeft, 10.f);
		wetRight = softClip(wetRight, 10.f);

		float dryGain = std::cos(p.trace * PI * 0.5f);
		float wetGain = std::sin(p.trace * PI * 0.5f);
		Outputs result;
		result.ghostLeft = wetLeft;
		result.ghostRight = wetRight;
		result.mainLeft = softClip(dryLeft * dryGain + wetLeft * wetGain, 10.f);
		result.mainRight = softClip(dryRight * dryGain + wetRight * wetGain, 10.f);
		result.motion = clamp(motion_ * 34.f, 0.f, 1.f);
		return result;
	}

	bool consumeFrameReady() {
		bool ready = frameReady_;
		frameReady_ = false;
		return ready;
	}

	float displayBand(int layer, int band) const {
		if (layer < 0 || layer >= MEMORY_LAYERS || band < 0 || band >= DISPLAY_BANDS)
			return 0.f;
		return display_[layer][band];
	}

	float memoryValue(int channel, int layer, int bin) const {
		if (channel < 0 || channel > 1 || layer < 0 || layer >= MEMORY_LAYERS || bin < 0 || bin >= BIN_COUNT)
			return 0.f;
		return channels_[channel].memory[layer][bin];
	}

	void setMemoryValue(int channel, int layer, int bin, float value) {
		if (channel < 0 || channel > 1 || layer < 0 || layer >= MEMORY_LAYERS || bin < 0 || bin >= BIN_COUNT)
			return;
		channels_[channel].memory[layer][bin] = finite(value) ? clamp(value, 0.f, 4.f) : 0.f;
		if (channels_[channel].memory[layer][bin] > 1e-7f)
			memoryActive_ = true;
	}

	float memoryEnergy() const {
		double total = 0.0;
		for (int channel = 0; channel < 2; ++channel)
			for (int layer = 0; layer < MEMORY_LAYERS; ++layer)
				for (int bin = 1; bin < BIN_COUNT; ++bin)
					total += channels_[channel].memory[layer][bin];
		return (float) (total / (2.0 * MEMORY_LAYERS * (BIN_COUNT - 1)));
	}

	float clockPosition() const {
		return clockPosition_;
	}

	void setClockPosition(float value) {
		clockPosition_ = clamp(value, 0.f, 1.f);
	}

private:
	struct Channel {
		std::array<float, FFT_SIZE> input;
		std::array<float, FFT_SIZE> output;
		std::array<float, FFT_SIZE> dry;
		std::array<std::complex<float>, FFT_SIZE> fft;
		std::array<float, BIN_COUNT> currentMagnitude;
		std::array<std::array<float, BIN_COUNT>, MEMORY_LAYERS> memory;
		std::array<float, BIN_COUNT> phase;
		float energy = 0.f;
		float centroid = 0.f;
		float dcInput = 0.f;
		float dcOutput = 0.f;
	};

	static Parameters sanitized(const Parameters& in) {
		Parameters p = in;
		p.age = clamp(finite(p.age) ? p.age : 0.f, 0.f, 1.f);
		p.imprint = clamp(finite(p.imprint) ? p.imprint : 0.f, 0.f, 1.f);
		p.erosion = clamp(finite(p.erosion) ? p.erosion : 0.f, 0.f, 1.f);
		p.gravity = clamp(finite(p.gravity) ? p.gravity : 0.f, 0.f, 1.f);
		p.drift = clamp(finite(p.drift) ? p.drift : 0.f, 0.f, 1.f);
		p.bloom = clamp(finite(p.bloom) ? p.bloom : 0.f, 0.f, 0.995f);
		p.trace = clamp(finite(p.trace) ? p.trace : 0.f, 0.f, 1.f);
		p.transposeVoltage = clamp(finite(p.transposeVoltage) ? p.transposeVoltage : 0.f, -5.f, 5.f);
		p.transposeDepth = clamp(finite(p.transposeDepth) ? p.transposeDepth : 1.f, 0.f, 2.f);
		return p;
	}

	float dcBlock(Channel& channel, float input) {
		float output = input - channel.dcInput + 0.995f * channel.dcOutput;
		channel.dcInput = input;
		channel.dcOutput = finite(output) ? output : 0.f;
		return channel.dcOutput;
	}

	void analyze(Channel& channel) {
		for (int i = 0; i < FFT_SIZE; ++i) {
			int index = (writeIndex_ + i) & (FFT_SIZE - 1);
			channel.fft[i] = std::complex<float>(channel.input[index] * window_[i], 0.f);
		}
		Radix2FFT::transform(channel.fft, false);

		double weighted = 0.0;
		double sum = 0.0;
		const float magnitudeScale = 4.f / (float) FFT_SIZE;
		for (int bin = 0; bin < BIN_COUNT; ++bin) {
			float magnitude = std::abs(channel.fft[bin]) * magnitudeScale;
			magnitude = finite(magnitude) ? clamp(magnitude, 0.f, 4.f) : 0.f;
			channel.currentMagnitude[bin] = magnitude;
			if (bin > 0) {
				sum += magnitude;
				weighted += magnitude * ((double) bin * sampleRate_ / FFT_SIZE);
			}
		}
		channel.energy = (float) (sum / (BIN_COUNT - 1));
		channel.centroid = sum > 1e-9 ? (float) (weighted / sum) : 0.f;
	}

	void updateMemory(Channel& channel, const Parameters& p, float& flux) {
		if (p.sealed && washEnvelope_ <= 1e-5f)
			return;

		float tau = 0.04f * std::pow(180.f, 1.f - p.imprint);
		float youngAlpha = 1.f - std::exp(-frameSeconds_ / tau);
		if (channel.energy > 1e-4f && memoryEnergyChannel(channel) < 1e-5f)
			youngAlpha = std::max(youngAlpha, 0.32f);
		const float layerTau[MEMORY_LAYERS - 1] = {0.65f, 4.5f, 28.f};
		float erosionAmount = p.erosion * p.erosion;
		float washDecay = washEnvelope_ > 1e-5f
			? std::exp(-frameSeconds_ * (1.4f + washEnvelope_ * 7.6f)) : 1.f;

		for (int bin = 0; bin < BIN_COUNT; ++bin) {
			float oldYoung = channel.memory[0][bin];
			float target = channel.currentMagnitude[bin] + oldYoung * p.bloom * 0.992f;
			float updated = oldYoung + youngAlpha * (target - oldYoung);
			float weak = 1.f - clamp(updated * 1.6f, 0.f, 1.f);
			float erosionRate = erosionAmount * erosionSeed_[bin] * (0.16f + weak * 2.4f);
			updated *= std::exp(-frameSeconds_ * erosionRate);
			updated *= washDecay;
			updated = clamp(finite(updated) ? updated : 0.f, 0.f, 4.f);
			channel.memory[0][bin] = updated;
			flux += std::fabs(updated - oldYoung);

			for (int layer = 1; layer < MEMORY_LAYERS; ++layer) {
				float alpha = 1.f - std::exp(-frameSeconds_ / layerTau[layer - 1]);
				float old = channel.memory[layer][bin];
				float value = old + alpha * (channel.memory[layer - 1][bin] - old);
				value *= std::exp(-frameSeconds_ * erosionRate * (0.6f + layer * 0.22f));
				value *= washDecay;
				channel.memory[layer][bin] = clamp(finite(value) ? value : 0.f, 0.f, 4.f);
				flux += std::fabs(value - old) * (1.f / (layer + 1.f));
			}
		}
	}

	float memoryEnergyChannel(const Channel& channel) const {
		double sum = 0.0;
		for (int bin = 1; bin < BIN_COUNT; ++bin)
			sum += channel.memory[0][bin];
		return (float) (sum / (BIN_COUNT - 1));
	}

	float selectedMagnitude(const Channel& channel, float agePosition, float sourceBin) const {
		if (sourceBin < 0.f || sourceBin >= (float) (BIN_COUNT - 1))
			return 0.f;
		int layerA = std::min(MEMORY_LAYERS - 1, (int) std::floor(agePosition));
		int layerB = std::min(MEMORY_LAYERS - 1, layerA + 1);
		float layerMix = agePosition - layerA;
		int binA = (int) std::floor(sourceBin);
		int binB = std::min(BIN_COUNT - 1, binA + 1);
		float binMix = sourceBin - binA;
		float a = lerp(channel.memory[layerA][binA], channel.memory[layerA][binB], binMix);
		float b = lerp(channel.memory[layerB][binA], channel.memory[layerB][binB], binMix);
		return lerp(a, b, layerMix);
	}

	void synthesize(Channel& channel, const Parameters& p, float liveCentroid, float agePosition, int channelIndex) {
		double sum = 0.0;
		double weighted = 0.0;
		for (int bin = 1; bin < BIN_COUNT; ++bin) {
			float magnitude = selectedMagnitude(channel, agePosition, (float) bin);
			sum += magnitude;
			weighted += magnitude * ((double) bin * sampleRate_ / FFT_SIZE);
		}
		float memoryCentroid = sum > 1e-8 ? (float) (weighted / sum) : 0.f;
		float gravityRatio = 1.f;
		if (liveCentroid > 25.f && memoryCentroid > 25.f) {
			float targetRatio = clamp(liveCentroid / memoryCentroid, 0.35f, 2.85f);
			gravityRatio = std::pow(targetRatio, p.gravity * 0.78f);
		}
		float pitchRatio = std::pow(2.f, p.transposeVoltage * p.transposeDepth);
		float ratio = clamp(gravityRatio * pitchRatio, 0.125f, 8.f);

		channel.fft.fill(std::complex<float>(0.f, 0.f));
		channel.fft[0] = std::complex<float>(0.f, 0.f);
		for (int bin = 1; bin < BIN_COUNT - 1; ++bin) {
			float sourceBin = (float) bin / ratio;
			float magnitude = selectedMagnitude(channel, agePosition, sourceBin);
			float wander = std::sin(driftPhase_ * (0.72f + 0.08f * channelIndex)
				+ driftSeed_[bin] * 5.17f + channelIndex * 1.73f);
			float detune = driftSeed_[bin] * wander * p.drift * p.drift * 0.012f;
			float increment = 2.f * PI * (float) bin * HOP_SIZE / FFT_SIZE * (1.f + detune);
			channel.phase[bin] += increment;
			channel.phase[bin] = std::fmod(channel.phase[bin], 2.f * PI);
			float fftMagnitude = magnitude * FFT_SIZE * 0.5f;
			std::complex<float> value = std::polar(fftMagnitude, channel.phase[bin]);
			channel.fft[bin] = value;
			channel.fft[FFT_SIZE - bin] = std::conj(value);
		}
		channel.fft[FFT_SIZE / 2] = std::complex<float>(0.f, 0.f);
		Radix2FFT::transform(channel.fft, true);

		const float overlapScale = 2.f / 3.f;
		for (int i = 0; i < FFT_SIZE; ++i) {
			int index = (writeIndex_ + i) & (FFT_SIZE - 1);
			float sample = channel.fft[i].real() * window_[i] * overlapScale;
			if (finite(sample))
				channel.output[index] += clamp(sample, -4.f, 4.f);
		}
	}

	void updateDisplay() {
		for (int layer = 0; layer < MEMORY_LAYERS; ++layer) {
			for (int band = 0; band < DISPLAY_BANDS; ++band) {
				float t0 = (float) band / DISPLAY_BANDS;
				float t1 = (float) (band + 1) / DISPLAY_BANDS;
				int first = 1 + (int) std::floor(std::pow((float) (BIN_COUNT - 2), t0));
				int last = 1 + (int) std::floor(std::pow((float) (BIN_COUNT - 2), t1));
				first = std::min(BIN_COUNT - 1, std::max(1, first));
				last = std::min(BIN_COUNT, std::max(first + 1, last));
				float peak = 0.f;
				for (int bin = first; bin < last; ++bin) {
					float stereo = 0.5f * (channels_[0].memory[layer][bin] + channels_[1].memory[layer][bin]);
					peak = std::max(peak, stereo);
				}
				display_[layer][band] = clamp(std::log1p(peak * 18.f) / std::log(19.f), 0.f, 1.f);
			}
		}
	}

	void processSpectralFrame(const Parameters& p) {
		analyze(channels_[0]);
		analyze(channels_[1]);
		float energySum = channels_[0].energy + channels_[1].energy;
		float liveCentroid = energySum > 1e-7f
			? (channels_[0].centroid * channels_[0].energy + channels_[1].centroid * channels_[1].energy) / energySum
			: 0.f;

		float flux = 0.f;
		updateMemory(channels_[0], p, flux);
		updateMemory(channels_[1], p, flux);
		flux /= (2.f * MEMORY_LAYERS * BIN_COUNT);
		float motionAlpha = 1.f - std::exp(-frameSeconds_ / 0.12f);
		motion_ += motionAlpha * (flux - motion_);

		float ageNormalized = clamp(p.age + (clockPosition_ - 0.5f) * 0.72f, 0.f, 1.f);
		float agePosition = ageNormalized * (MEMORY_LAYERS - 1.f);
		synthesize(channels_[0], p, liveCentroid, agePosition, 0);
		synthesize(channels_[1], p, liveCentroid, agePosition, 1);

		driftPhase_ += frameSeconds_ * (0.22f + p.drift * 0.88f);
		if (driftPhase_ > 2.f * PI)
			driftPhase_ -= 2.f * PI;
		washEnvelope_ *= std::exp(-frameSeconds_ * 0.62f);
		if (washEnvelope_ < 1e-5f)
			washEnvelope_ = 0.f;
		updateDisplay();
		memoryActive_ = memoryEnergy() > 1e-7f;
	}

	std::array<float, FFT_SIZE> window_;
	std::array<float, BIN_COUNT> driftSeed_;
	std::array<float, BIN_COUNT> erosionSeed_;
	std::array<std::array<float, DISPLAY_BANDS>, MEMORY_LAYERS> display_;
	Channel channels_[2];
	float sampleRate_ = 48000.f;
	float frameSeconds_ = (float) HOP_SIZE / 48000.f;
	float exciteDecay_ = 0.9999f;
	float exciteEnvelope_ = 0.f;
	float washEnvelope_ = 0.f;
	float clockPosition_ = 0.5f;
	float driftPhase_ = 0.f;
	float motion_ = 0.f;
	float framePeak_ = 0.f;
	int writeIndex_ = 0;
	int hopCounter_ = 0;
	bool frameReady_ = false;
	bool memoryActive_ = false;
};

} // namespace palimpsest
