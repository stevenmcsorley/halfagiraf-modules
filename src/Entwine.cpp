#include "plugin.hpp"
#include "EntwineData.hpp"
#include <osdialog.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>

using namespace entwine_data;

// ---- helpers ----
static inline float fract(float x) { return x - std::floor(x); }
static inline float wrap01(float x) { return fract(x); }
static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Tolerant RIFF/WAV reader: 16/24/32-bit PCM or float32, any channel count
// mixed down to mono. Used for custom wavetable loading.
static bool readWavMonoH(const std::string& path, std::vector<float>& out) {
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f) return false;
	auto rd32 = [&]() -> long { uint8_t b[4]; if (std::fread(b, 1, 4, f) != 4) return -1L; return (long) (b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t) b[3] << 24)); };
	auto rd16 = [&]() -> long { uint8_t b[2]; if (std::fread(b, 1, 2, f) != 2) return -1L; return (long) (b[0] | (b[1] << 8)); };
	char id[5] = {};
	if (std::fread(id, 1, 4, f) != 4 || std::strncmp(id, "RIFF", 4)) { std::fclose(f); return false; }
	rd32();
	if (std::fread(id, 1, 4, f) != 4 || std::strncmp(id, "WAVE", 4)) { std::fclose(f); return false; }
	int channels = 1, bits = 16, format = 1;
	bool gotFmt = false;
	while (std::fread(id, 1, 4, f) == 4) {
		long sz = rd32();
		if (sz < 0) break;
		if (!std::strncmp(id, "fmt ", 4)) {
			format = (int) rd16();
			channels = std::max(1, (int) rd16());
			rd32(); rd32(); rd16();
			bits = (int) rd16();
			if (sz > 16) std::fseek(f, sz - 16, SEEK_CUR);
			gotFmt = true;
		}
		else if (!std::strncmp(id, "data", 4) && gotFmt) {
			int bytesPer = bits / 8;
			if (bytesPer < 2 || bytesPer > 4) { std::fclose(f); return false; }
			long frames = sz / (bytesPer * channels);
			out.clear();
			out.reserve(frames);
			std::vector<uint8_t> buf((size_t) bytesPer * channels);
			for (long i = 0; i < frames; i++) {
				if (std::fread(buf.data(), 1, buf.size(), f) != buf.size()) break;
				float acc = 0.f;
				for (int c = 0; c < channels; c++) {
					const uint8_t* p = &buf[(size_t) c * bytesPer];
					float v = 0.f;
					if (format == 3 && bits == 32) { float fv; std::memcpy(&fv, p, 4); v = fv; }
					else if (bits == 16) { int16_t sv = (int16_t) (p[0] | (p[1] << 8)); v = sv / 32768.f; }
					else if (bits == 24) { int32_t sv = p[0] | (p[1] << 8) | (p[2] << 16); if (sv & 0x800000) sv -= 0x1000000; v = sv / 8388608.f; }
					else if (bits == 32) { int32_t sv; std::memcpy(&sv, p, 4); v = sv / 2147483648.f; }
					acc += v;
				}
				out.push_back(acc / channels);
			}
			std::fclose(f);
			return !out.empty();
		}
		else {
			std::fseek(f, sz + (sz & 1), SEEK_CUR);
		}
	}
	std::fclose(f);
	return false;
}

// Per-voice deterministic RNG (xorshift32).
struct Rng {
	uint32_t s;
	void seed(uint32_t v) { s = v ? v : 0x9E3779B9u; }
	float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / float(0x1000000); }
};

// One generative voice ("Unit").
struct Voice {
	bool active = false;
	float pState = 0.f, dState = 0.f;      // autoregressive state (0..1)
	float voct = 0.f, targetVoct = 0.f;    // pitch (V/Oct) with glide
	float phase = 0.f;                     // oscillator phase
	float envPhase = 1.f;                  // >=1 => needs (re)trigger
	float durationSec = 0.3f;
	float peakPos = 0.5f;
	float noteVol = 0.8f;
	float ic1 = 0.f, ic2 = 0.f;            // per-voice SVF state
	int midiNote = -1;                     // currently-sounding MIDI note (-1 = none)
	bool midiGateOn = false;               // is a MIDI gate currently held
	Rng rng;

	void init(uint32_t seed) {
		rng.seed(seed);
		pState = rng.next(); dState = rng.next(); phase = rng.next();
		envPhase = 1.f; voct = targetVoct = 0.f; ic1 = ic2 = 0.f;
		midiNote = -1; midiGateOn = false;
	}
};

struct Entwine : Module {
	enum ParamId {
		POLY_PARAM, ROOT_PARAM, SCALE_PARAM, GLIDE_PARAM, SPREAD_PARAM,
		COUPLING_PARAM, WAVE_PARAM, ENV_PARAM,
		PULSAR_RESEED_PARAM, QUASAR_RESEED_PARAM, LOCK_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		ROOT_INPUT, POLY_INPUT, GLIDE_INPUT, SPREAD_INPUT, WAVE_INPUT,
		ENV_INPUT, COUPLING_INPUT, LOCK_INPUT, PULSAR_RESEED_INPUT, QUASAR_RESEED_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		PULSAR_OUTPUT, QUASAR_OUTPUT,
		PULSAR_VOCT_OUTPUT, PULSAR_GATE_OUTPUT, QUASAR_VOCT_OUTPUT, QUASAR_GATE_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		SCALE_LIGHT_R, SCALE_LIGHT_G, SCALE_LIGHT_B,
		PULSAR_LIGHT, QUASAR_LIGHT, LOCK_LIGHT,
		ENUMS(COUPLING_LIGHTS, 3),
		LIGHTS_LEN
	};

	static const int CHANNELS = 2;   // Pulsar, Quasar
	static const int VOICES = 8;     // per channel
	Voice voices[CHANNELS][VOICES];

	dsp::BooleanTrigger pulsarReseedTrig, quasarReseedTrig;
	dsp::SchmittTrigger syncTrig;
	int syncCount = 0;
	float lightPhase = 0.f;
	float pulsarMeter = 0.f, quasarMeter = 0.f;

	// MIDI output: Pulsar voices on channel 1, Quasar voices on channel 2.
	midi::Output midiOut;
	bool midiMono = false;     // send only 1 voice per channel (clean for KeyStep etc.)
	float midiGate = 0.6f;     // note length as fraction of the step -> gives gate gaps
	void sendMidi(int chan, bool on, int note, int vel) {
		if (midiOut.getDeviceId() < 0) return;
		midi::Message m;
		m.bytes[0] = (on ? 0x90 : 0x80) | (chan & 0x0F);
		m.bytes[1] = (uint8_t) clamp(note, 0, 127);
		m.bytes[2] = (uint8_t) clamp(vel, 0, 127);
		midiOut.sendMessage(m);
	}

	// Additional MIDI-controlled settings without front-panel controls.
	int filterType = 0;          // 0 Off, 1 LP, 2 BP, 3 HP  (ccFilterType)
	float filterCutoff = 1.f;    // 0..1 normalized                (ccCutoff)
	float filterQ = 0.f;         // 0..1 normalized                (ccQ)
	int wtIndex = 0;             // 0..63 wavetable frame offset    (ccIndex)
	float noteLength = 1.f;      // duration multiplier             (ccLength)

	// Default per-note dynamics.
	static constexpr float VOL_CENTER = 75.f / 127.f;
	static constexpr float VOL_WIDTH  = 50.f / 127.f;

	// Autoregressive coefficients set how strongly the previous duration steers the
	// next pitch versus the innovation.
	static constexpr float AR_COEF = 0.30f;
	static constexpr float AR_INNOV = 0.35f;

	// Sixty-four frames form eight presets of eight. Wave morphs within the selected
	// preset, while preset selection lives in the context menu.
	int wtPreset = 0;
	bool lastMono = false;
	int lastScaleIdx = -1;

	// Panel nebula: it breathes with the voices and lights up as the controls move.
	float uiGlow = 0.f, cloudPhase = 0.f, uiCoupling = 0.78f;
	float uiLastParam[8] = {};
	bool uiFirst = true;
	float uiPulsar = 0.f, uiQuasar = 0.f;

	Entwine() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		// 0-8 per stream; the maximum is raised to 16 in mono mode.
		configParam(POLY_PARAM, 0.f, 16.f, 4.f, "Polyphony", " voices");
		getParamQuantity(POLY_PARAM)->snapEnabled = true;
		configParam(ROOT_PARAM, -2.f, 2.f, 0.f, "Root", " oct");
		configParam(SCALE_PARAM, 0.f, NUM_SCALES - 1, 0.f, "Scale");
		getParamQuantity(SCALE_PARAM)->snapEnabled = true;
		configParam(GLIDE_PARAM, 0.f, 1.f, 0.1f, "Glide");
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.4f, "Spread");
		// Coupling spans audio-rate textures through long generative phrases.
		configParam(COUPLING_PARAM, 0.f, 1.f, 0.78f, "Coupling (note length)");
		configParam(WAVE_PARAM, 0.f, 1.f, 0.f, "Wave");
		configParam(ENV_PARAM, 0.f, 1.f, 0.5f, "Env (attack/decay)");
		configButton(PULSAR_RESEED_PARAM, "Reseed Pulsar");
		configButton(QUASAR_RESEED_PARAM, "Reseed Quasar");
		configSwitch(LOCK_PARAM, 0.f, 1.f, 0.f, "Lock", {"Off", "On"});

		configInput(ROOT_INPUT, "Root V/Oct");
		configInput(POLY_INPUT, "Poly CV (clock here with Poly at 0 = sync mode; Coupling divides)");
		configInput(GLIDE_INPUT, "Glide CV");
		configInput(SPREAD_INPUT, "Spread CV");
		configInput(WAVE_INPUT, "Wave CV");
		configInput(ENV_INPUT, "Env CV");
		configInput(COUPLING_INPUT, "Coupling CV");
		configInput(LOCK_INPUT, "Lock gate");
		configInput(PULSAR_RESEED_INPUT, "Reseed Pulsar trigger");
		configInput(QUASAR_RESEED_INPUT, "Reseed Quasar trigger");
		configOutput(PULSAR_OUTPUT, "Pulsar audio");
		configOutput(QUASAR_OUTPUT, "Quasar audio");
		configOutput(PULSAR_VOCT_OUTPUT, "Pulsar V/Oct");
		configOutput(PULSAR_GATE_OUTPUT, "Pulsar gate");
		configOutput(QUASAR_VOCT_OUTPUT, "Quasar V/Oct");
		configOutput(QUASAR_GATE_OUTPUT, "Quasar gate");

		for (int c = 0; c < CHANNELS; c++)
			for (int v = 0; v < VOICES; v++)
				voices[c][v].init(0x1234u + c * 101 + v * 7919);
	}

	void onReset() override {
		filterType = 0; filterCutoff = 1.f; filterQ = 0.f; wtIndex = 0; noteLength = 1.f;
		for (int c = 0; c < CHANNELS; c++)
			for (int v = 0; v < VOICES; v++)
				voices[c][v].init(0x1234u + c * 101 + v * 7919);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "filterType", json_integer(filterType));
		json_object_set_new(root, "filterCutoff", json_real(filterCutoff));
		json_object_set_new(root, "filterQ", json_real(filterQ));
		json_object_set_new(root, "wtIndex", json_integer(wtIndex));
		json_object_set_new(root, "noteLength", json_real(noteLength));
		json_object_set_new(root, "midi", midiOut.toJson());
		json_object_set_new(root, "midiMono", json_boolean(midiMono));
		json_object_set_new(root, "midiGate", json_real(midiGate));
		json_object_set_new(root, "wtPath", json_string(wtPath.c_str()));
		json_object_set_new(root, "scalePath", json_string(scalePath.c_str()));
		return root;
	}
	void dataFromJson(json_t* root) override {
		if (json_t* j = json_object_get(root, "filterType")) filterType = (int) json_integer_value(j);
		if (json_t* j = json_object_get(root, "filterCutoff")) filterCutoff = (float) json_real_value(j);
		if (json_t* j = json_object_get(root, "filterQ")) filterQ = (float) json_real_value(j);
		if (json_t* j = json_object_get(root, "wtIndex")) wtIndex = (int) json_integer_value(j);
		if (json_t* j = json_object_get(root, "noteLength")) noteLength = (float) json_real_value(j);
		if (json_t* j = json_object_get(root, "midi")) midiOut.fromJson(j);
		if (json_t* j = json_object_get(root, "midiMono")) midiMono = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "midiGate")) midiGate = (float) json_real_value(j);
		if (json_t* j = json_object_get(root, "wtPath")) {
			const char* p = json_string_value(j);
			if (p && *p) loadWavetable(p);
		}
		if (json_t* j = json_object_get(root, "scalePath")) {
			const char* p = json_string_value(j);
			if (p && *p) loadScales(p);
		}
	}

	// User wavetable and scale files loaded from disk.
	float userWT[WT_FRAMES][WT_LEN] = {};
	int userWtFrames = 0;
	bool wtUserLoaded = false;
	std::string wtPath;
	ScaleDef userScales[NUM_SCALES];
	bool scalesUserLoaded = false;
	std::string scalePath;

	int activeWtFrames() const { return wtUserLoaded ? userWtFrames : WT_FRAMES; }
	const ScaleDef& scaleAt(int idx) const {
		return (scalesUserLoaded ? userScales : SCALES)[clamp(idx, 0, NUM_SCALES - 1)];
	}

	// Slice a mono WAV into 256-sample wavetable frames.
	bool loadWavetable(const std::string& path) {
		std::vector<float> s;
		if (!readWavMonoH(path, s)) return false;
		int frames = (int) (s.size() / WT_LEN);
		if (frames < 1) return false;
		frames = std::min(frames, (int) WT_FRAMES);
		for (int f = 0; f < frames; f++)
			for (int i = 0; i < WT_LEN; i++)
				userWT[f][i] = clampf(s[(size_t) f * WT_LEN + i], -1.f, 1.f);
		userWtFrames = frames;
		wtUserLoaded = true;
		wtPath = path;
		return true;
	}

	// Parse scale.txt: per line "cAr cAg cAb cBr cBg cBb flag offsets..."
	bool loadScales(const std::string& path) {
		FILE* fp = std::fopen(path.c_str(), "r");
		if (!fp) return false;
		ScaleDef parsed[NUM_SCALES];
		int n = 0;
		char line[512];
		while (n < NUM_SCALES && std::fgets(line, sizeof(line), fp)) {
			int v[31], k = 0;
			char* tok = std::strtok(line, " \t\r\n");
			while (tok && k < 31) { v[k++] = atoi(tok); tok = std::strtok(nullptr, " \t\r\n"); }
			if (k < 8) continue;   // needs colours + flag + at least one offset
			ScaleDef& d = parsed[n];
			for (int i = 0; i < 3; i++) { d.colA[i] = (unsigned char) clamp(v[i], 0, 255); d.colB[i] = (unsigned char) clamp(v[3 + i], 0, 255); }
			d.flag = v[6];
			d.count = std::min(k - 7, 24);
			for (int i = 0; i < 24; i++) d.offsets[i] = (i < d.count) ? v[7 + i] : 0;
			n++;
		}
		std::fclose(fp);
		if (n < 1) return false;
		// Unfilled slots retain the embedded defaults.
		for (int i = 0; i < NUM_SCALES; i++)
			userScales[i] = (i < n) ? parsed[i] : SCALES[i];
		scalesUserLoaded = true;
		scalePath = path;
		return true;
	}

	float wtSample(float phase, float morph) {
		const float (*tab)[WT_LEN] = wtUserLoaded ? userWT : WAVETABLE;
		int nf = activeWtFrames();
		float fp = phase * WT_LEN;
		int i0 = (int) fp;
		int i1 = (i0 + 1) & (WT_LEN - 1);
		float fr = fp - i0;
		int m0 = (int) morph;
		if (m0 < 0) m0 = 0;
		if (m0 > nf - 1) m0 = nf - 1;
		int m1 = m0 < nf - 1 ? m0 + 1 : m0;
		float mf = morph - m0;
		float a = lerpf(tab[m0][i0], tab[m0][i1], fr);
		float b = lerpf(tab[m1][i0], tab[m1][i1], fr);
		return lerpf(a, b, mf);
	}

	// Extend a scale table above its listed offsets.
	// The Root-Emphasize tables (flag = 1) span TWO octaves: a root-only lowest octave
	// (0, 7) then the full scale from 12 up — "the lowest octave uses only root-related
	// notes. Higher octaves follow the full scale." So the repeat has to start at the first
	// offset >= 12. Wrapping the whole table every 12 semitones duplicated pitches
	// (degree 2 and degree 9 both returned 12) and stalled the top of the Spread range.
	int scaleSemitone(int scaleIdx, int degree) {
		const ScaleDef& S = scaleAt(scaleIdx);
		int n = S.count;
		if (n < 1) return 0;
		if (degree < 0) degree = 0;
		int k = 0;
		while (k < n && S.offsets[k] < 12) k++;
		if (k >= n) k = 0;              // no root emphasis (whole tone, chromatic)
		if (degree < k) return S.offsets[degree];
		int m = n - k;                  // notes per octave in the repeating part
		if (m < 1) return S.offsets[degree % n];
		int j = degree - k;
		return S.offsets[k + (j % m)] + 12 * (j / m);
	}

	// Coupling maps exponentially from audio-rate textures to long note durations.
	static float couplingMul(float knob01) {
		return std::exp(lerpf(std::log(0.002f), std::log(300.f), clampf(knob01, 0.f, 1.f)));
	}
	static constexpr float DUR_MIN = 0.0004f;
	static constexpr float DUR_MAX = 30.f;

	// Spread controls the pitch range calculated from duration, from the root alone
	// to roughly six octaves above it.
	int spreadRange(int scaleIdx, float spread) {
		const ScaleDef& S = scaleAt(scaleIdx);
		int k = 0; while (k < S.count && S.offsets[k] < 12) k++;
		int m = (k >= S.count) ? S.count : S.count - k;
		if (m < 1) m = 1;
		int maxDeg = k + m * 6;           // ~6 octaves above the emphasized root notes
		return 1 + (int) std::round(spread * (maxDeg - 1));
	}

	// One autoregressive step. Each voice's previous pitch shapes its next duration,
	// while its previous duration shapes the next pitch. Innovation keeps the sequence
	// moving; regression gives it continuity instead of white-noise jumps.
	void recalcVoice(Voice& vo, int scaleIdx, float rootVoct, float spread,
	                 float couplingX, float env, float lengthMul, bool locked) {
		int range = spreadRange(scaleIdx, spread);

		if (!locked) {
			// The previous duration steers the next pitch, producing a correlated path
			// through the selected spread instead of unrelated jumps.
			vo.pState = wrap01(vo.pState + (vo.dState - 0.5f) * AR_COEF
			                             + (vo.rng.next() - 0.5f) * AR_INNOV);
		}

		int deg = (int) (vo.pState * range);
		if (deg >= range) deg = range - 1;
		vo.targetVoct = rootVoct + scaleSemitone(scaleIdx, deg) / 12.f;

		// The other half of the cross-coupling: this note's pitch sets its duration.
		float freqHz = clampf(dsp::FREQ_C4 * std::pow(2.f, vo.targetVoct), 1.f, 20000.f);
		vo.durationSec = clampf(std::sqrt(freqHz) * couplingX * 0.001f * lengthMul, DUR_MIN, DUR_MAX);

		// Remember where this note's duration sits within the range currently reachable, so
		// the next regression has a meaningful 0..1 regressor.
		float fLo = clampf(dsp::FREQ_C4 * std::pow(2.f, rootVoct + scaleSemitone(scaleIdx, 0) / 12.f), 1.f, 20000.f);
		float fHi = clampf(dsp::FREQ_C4 * std::pow(2.f, rootVoct + scaleSemitone(scaleIdx, range - 1) / 12.f), 1.f, 20000.f);
		float sLo = std::sqrt(fLo), sHi = std::sqrt(fHi);
		vo.dState = clampf((std::sqrt(freqHz) - sLo) / (sHi - sLo + 1e-6f), 0.f, 1.f);

		vo.peakPos = clampf(env, 0.02f, 0.98f);
		vo.noteVol = clampf(VOL_CENTER + (vo.rng.next() - 0.5f) * 2.f * VOL_WIDTH, 0.05f, 1.2f);
		vo.envPhase = 0.f;
		vo.phase = 0.f;
	}

	void process(const ProcessArgs& args) override {
		float dt = args.sampleTime;

		// When Quasar is unused, Pulsar combines both eight-voice banks in mono mode.
		// Any Quasar audio, pitch, or gate connection keeps the streams independent.
		bool quasarUsed = outputs[QUASAR_OUTPUT].isConnected()
		               || outputs[QUASAR_VOCT_OUTPUT].isConnected()
		               || outputs[QUASAR_GATE_OUTPUT].isConnected();
		bool mono = !quasarUsed;
		if (mono != lastMono) {
			lastMono = mono;
			if (ParamQuantity* pq = getParamQuantity(POLY_PARAM)) {
				pq->maxValue = mono ? 16.f : 8.f;
				if (!mono && params[POLY_PARAM].getValue() > (float) VOICES)
					params[POLY_PARAM].setValue((float) VOICES);
			}
		}

		// With Poly at zero, the Poly input becomes a clock and Coupling selects
		// the clock division.
		float couplingKnobRaw = params[COUPLING_PARAM].getValue();
		bool syncMode = ((int) std::round(params[POLY_PARAM].getValue()) == 0)
		             && inputs[POLY_INPUT].isConnected();
		bool syncTickNow = false;
		int poly;
		if (syncMode) {
			poly = mono ? 2 * VOICES : VOICES;   // all voices, stepped by the clock
			if (syncTrig.process(inputs[POLY_INPUT].getVoltage(), 0.1f, 1.f)) {
				static const int divs[8] = {1, 2, 3, 4, 6, 8, 12, 16};
				int d = divs[clamp((int) (couplingKnobRaw * 8.f), 0, 7)];
				if (++syncCount >= d) { syncCount = 0; syncTickNow = true; }
			}
		} else {
			syncCount = 0;
			poly = (int) std::round(params[POLY_PARAM].getValue());
			if (inputs[POLY_INPUT].isConnected())
				poly += (int) std::round(inputs[POLY_INPUT].getVoltage() * 0.8f);
			poly = (int) clampf((float) poly, 0.f, mono ? 2.f * VOICES : (float) VOICES);
		}

		float rootVoct = params[ROOT_PARAM].getValue()
		               + (inputs[ROOT_INPUT].isConnected() ? inputs[ROOT_INPUT].getVoltage() : 0.f);
		int scaleIdx = (int) clampf(std::round(params[SCALE_PARAM].getValue()), 0.f, NUM_SCALES - 1.f);

		float glide = clampf(params[GLIDE_PARAM].getValue() + inputs[GLIDE_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float spread = clampf(params[SPREAD_PARAM].getValue() + inputs[SPREAD_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float couplingKnob = clampf(params[COUPLING_PARAM].getValue() + inputs[COUPLING_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float couplingX = couplingMul(couplingKnob);
		float wave = clampf(params[WAVE_PARAM].getValue() + inputs[WAVE_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float env = clampf(params[ENV_PARAM].getValue() + inputs[ENV_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		// Wave smoothly morphs between the eight frames in a preset.
		float morph = clampf(wtPreset * 8.f + wave * 7.f + wtIndex, 0.f, activeWtFrames() - 1.f);

		bool locked = params[LOCK_PARAM].getValue() > 0.5f
		           || (inputs[LOCK_INPUT].isConnected() && inputs[LOCK_INPUT].getVoltage() >= 1.f);

		// With Lock on, scale changes land immediately. With Lock off, the new scale is
		// picked up at the next natural recalculation.
		bool scaleChanged = (scaleIdx != lastScaleIdx);
		lastScaleIdx = scaleIdx;

		// Each Reseed button/input starts a fresh trajectory for its own stream.
		bool reseedPulsar = pulsarReseedTrig.process(params[PULSAR_RESEED_PARAM].getValue() > 0.5f
			|| (inputs[PULSAR_RESEED_INPUT].isConnected() && inputs[PULSAR_RESEED_INPUT].getVoltage() >= 1.f));
		bool reseedQuasar = quasarReseedTrig.process(params[QUASAR_RESEED_PARAM].getValue() > 0.5f
			|| (inputs[QUASAR_RESEED_INPUT].isConnected() && inputs[QUASAR_RESEED_INPUT].getVoltage() >= 1.f));

		// per-voice SVF coefficients (shared cutoff/Q), computed once per sample
		float k = 0.f, a1 = 0.f, a2 = 0.f, a3 = 0.f;
		if (filterType) {
			float fc = 20.f * std::pow(900.f, filterCutoff);       // 20 Hz .. ~18 kHz
			fc = clampf(fc, 20.f, args.sampleRate * 0.45f);
			float Q = 0.5f + filterQ * 9.5f;
			float g = std::tan(M_PI * fc / args.sampleRate);
			k = 1.f / Q;
			a1 = 1.f / (1.f + g * (g + k));
			a2 = g * a1;
			a3 = g * a2;
		}

		float out[CHANNELS] = {0.f, 0.f};

		for (int c = 0; c < CHANNELS; c++) {
			bool forceReseed = (c == 0) ? reseedPulsar : reseedQuasar;
			for (int v = 0; v < VOICES; v++) {
				Voice& vo = voices[c][v];
				// In mono mode all 16 voices sound and are summed to Pulsar, so the second bank
				// fills only once the first is full.
				bool shouldBeActive = mono ? ((c * VOICES + v) < poly) : (v < poly);
				if (shouldBeActive && !vo.active) { vo.active = true; vo.envPhase = 1.f; }
				if (!shouldBeActive) {
					if (vo.midiNote >= 0) { sendMidi(c, false, vo.midiNote, 0); vo.midiNote = -1; vo.midiGateOn = false; }
					vo.active = false; continue;
				}

				// In mono mode only voice 0 of each channel emits MIDI; silence any others.
				// At low Coupling durations reach audio rate, which would flood the MIDI port
				// with thousands of notes a second, so notes shorter than 30ms don't articulate.
				bool midiThisVoice = (!midiMono || v == 0) && vo.durationSec > 0.03f;
				if (!midiThisVoice && vo.midiNote >= 0) { sendMidi(c, false, vo.midiNote, 0); vo.midiNote = -1; vo.midiGateOn = false; }

				if (forceReseed) vo.envPhase = 1.f;

				// Lock freezes the calculation, but a scale change still lands immediately:
				// re-quantize the held pitch without restarting the note.
				if (locked && scaleChanged && !forceReseed) {
					int range = spreadRange(scaleIdx, spread);
					int deg = (int) (vo.pState * range);
					if (deg >= range) deg = range - 1;
					vo.targetVoct = rootVoct + scaleSemitone(scaleIdx, deg) / 12.f;
				}

				if (vo.envPhase >= 1.f) {
					// In sync mode a finished voice holds until the divided clock lands.
					if (syncMode && !syncTickNow && !forceReseed) {
						vo.envPhase = 1.f;
					} else {
						recalcVoice(vo, scaleIdx, rootVoct, spread, couplingX, env, noteLength, locked && !forceReseed);
						// MIDI: articulate the new note (Pulsar=ch1, Quasar=ch2)
						if (midiThisVoice) {
							if (vo.midiNote >= 0) sendMidi(c, false, vo.midiNote, 0);
							int note = clamp((int) std::round(vo.targetVoct * 12.f + 60.f), 0, 127);
							int vel = clamp((int) std::round(vo.noteVol * 100.f), 1, 127);
							sendMidi(c, true, note, vel);
							vo.midiNote = note; vo.midiGateOn = true;
						}
					}
				}

				// MIDI gate-off partway through the note -> leaves a gap so downstream gates retrigger
				if (vo.midiGateOn && vo.envPhase >= midiGate) {
					sendMidi(c, false, vo.midiNote, 0);
					vo.midiGateOn = false; vo.midiNote = -1;
				}

				// Glide is capped by the current note duration: long notes can reach the
				// full glide time while short notes scale it down naturally.
				float tau = glide * glide * std::min(1.f, vo.durationSec);
				float glideCoef = (tau < 1e-5f) ? 1.f : (1.f - std::exp(-dt / tau));
				vo.voct += (vo.targetVoct - vo.voct) * glideCoef;
				float freq = dsp::FREQ_C4 * std::pow(2.f, vo.voct);
				freq = clampf(freq, 1.f, 20000.f);

				vo.phase += freq * dt;
				if (vo.phase >= 1.f) vo.phase -= std::floor(vo.phase);
				float s = wtSample(vo.phase, morph);

				// Per-voice filter controlled from the context menu or MIDI.
				if (filterType) {
					float v0 = s;
					float v3 = v0 - vo.ic2;
					float v1 = a1 * vo.ic1 + a2 * v3;
					float v2 = vo.ic2 + a2 * vo.ic1 + a3 * v3;
					vo.ic1 = 2.f * v1 - vo.ic1;
					vo.ic2 = 2.f * v2 - vo.ic2;
					s = (filterType == 1) ? v2 : (filterType == 2) ? v1 : (v0 - k * v1 - v2);
				}

				float ph = vo.envPhase;
				float e = (ph < vo.peakPos) ? (ph / vo.peakPos) : (1.f - ph) / (1.f - vo.peakPos);
				e = clampf(e, 0.f, 1.f);

				out[c] += s * e * vo.noteVol;
				vo.envPhase += dt / vo.durationSec;
			}
		}

		// In mono mode both banks are summed to Pulsar.
		float pulsar = 5.f * std::tanh((mono ? (out[0] + out[1]) : out[0]) * 0.4f);
		float quasar = mono ? 0.f : 5.f * std::tanh(out[1] * 0.4f);
		outputs[PULSAR_OUTPUT].setVoltage(pulsar);
		outputs[QUASAR_OUTPUT].setVoltage(quasar);

		// Per-stream mono pitch and gate CV from voice zero.
		Voice& p0 = voices[0][0];
		Voice& q0 = voices[1][0];
		outputs[PULSAR_VOCT_OUTPUT].setVoltage(p0.active ? p0.voct : 0.f);
		outputs[PULSAR_GATE_OUTPUT].setVoltage((p0.active && p0.envPhase < midiGate) ? 10.f : 0.f);
		outputs[QUASAR_VOCT_OUTPUT].setVoltage(q0.active ? q0.voct : 0.f);
		outputs[QUASAR_GATE_OUTPUT].setVoltage((q0.active && q0.envPhase < midiGate) ? 10.f : 0.f);

		pulsarMeter += (std::fabs(pulsar) / 5.f - pulsarMeter) * 20.f * dt;
		quasarMeter += (std::fabs(quasar) / 5.f - quasarMeter) * 20.f * dt;

		// Nebula animation state.
		// The glow reads each control's EFFECTIVE value — knob plus CV — so a modulation
		// source lights the clouds exactly as a hand on the knob does. It integrates the
		// distance travelled rather than testing a per-sample threshold (which a slow move
		// never crosses), so the glow follows both how far and how fast things are moving:
		// a hand sweep, a stepped V/Oct note, or a fast LFO all read differently.
		{
			float eff[8] = {
				poly / (float)(2 * VOICES),
				rootVoct * 0.25f,
				scaleIdx / (float)(NUM_SCALES - 1),
				glide, spread, couplingKnob, wave, env
			};
			float change = 0.f;
			for (int i = 0; i < 8; i++) {
				if (uiFirst) uiLastParam[i] = eff[i];
				change += std::abs(eff[i] - uiLastParam[i]);
				uiLastParam[i] = eff[i];
			}
			uiFirst = false;
			uiGlow = clampf(uiGlow + change * 4.f - dt * 1.1f, 0.f, 1.f);
			cloudPhase += dt * 0.35f;
			if (cloudPhase > 1e6f) cloudPhase = 0.f;
			uiCoupling = couplingKnob;
			// Pulsar feeds the left plumes, Quasar the right. In mono mode Pulsar carries everything,
			// so the right side follows it too rather than going dark.
			uiPulsar = pulsarMeter;
			uiQuasar = mono ? pulsarMeter : quasarMeter;
		}
		lightPhase += dt;
		if (lightPhase >= 0.03f) {
			lightPhase = 0.f;
			const ScaleDef& S = scaleAt(scaleIdx);
			lights[SCALE_LIGHT_R].setBrightness(S.colA[0] / 152.f);
			lights[SCALE_LIGHT_G].setBrightness(S.colA[1] / 152.f);
			lights[SCALE_LIGHT_B].setBrightness(S.colA[2] / 152.f);
			lights[PULSAR_LIGHT].setBrightness(pulsarMeter);
			lights[QUASAR_LIGHT].setBrightness(quasarMeter);
			lights[LOCK_LIGHT].setBrightness(locked ? 1.f : 0.f);
			float act = clampf((uiPulsar + uiQuasar) * 0.7f + uiGlow * 0.4f, 0.f, 1.f);
			lights[COUPLING_LIGHTS + 0].setBrightness(act * (0.35f + 0.65f * uiCoupling));
			lights[COUPLING_LIGHTS + 1].setBrightness(act);
			lights[COUPLING_LIGHTS + 2].setBrightness(act * (1.f - 0.55f * uiCoupling));
		}
	}
};

// The nebula printed on the panel, brought to life: it drifts continuously, swells with the
// voices, and flares when a main control is moved. Drawn additively over the panel art but
// under the knobs, so the printed clouds show through when it is dim.
struct EntwineClouds : Widget {
	Entwine* module = nullptr;

	void draw(const DrawArgs& args) override {
		if (!module) return;
		float lvlL = clampf(module->uiPulsar, 0.f, 1.f);
		float lvlR = clampf(module->uiQuasar, 0.f, 1.f);
		float g = clampf(module->uiGlow, 0.f, 1.f);

		// Coupling biases the nebula's colour: long notes drift warm, audio-rate goes cold.
		float warm = clampf(module->uiCoupling, 0.f, 1.f);

		static const struct { float x, y, r, cr, cg, cb; } BLOBS[] = {
			{17.f,  70.f,  13.f, 0.97f, 0.65f, 0.78f},
			{24.f,  65.5f,  8.f, 0.90f, 0.54f, 0.82f},
			{11.f,  75.f,   9.5f, 0.90f, 0.54f, 0.82f},
			{27.f,  76.f,   7.f, 0.97f, 0.65f, 0.78f},
			{62.f,  64.f,   9.f, 0.90f, 0.54f, 0.82f},
			{69.f,  62.5f,  6.f, 0.97f, 0.65f, 0.78f},
			{64.f,  73.f,  13.f, 0.81f, 0.93f, 0.96f},
			{73.f,  77.f,   8.5f, 0.81f, 0.93f, 0.96f},
			{40.64f, 81.f, 18.f, 0.85f, 0.90f, 0.95f},
		};
		nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
		for (const auto& b : BLOBS) {
			// Each plume breathes with the channel it sits over; the low mist under the knob
			// takes both.
			float lvl = (b.x < 40.f) ? lvlL : (b.x > 51.f) ? lvlR : 0.5f * (lvlL + lvlR);
			float a = clampf(0.06f + 0.60f * lvl + 0.6f * g, 0.f, 1.f);
			if (a < 0.01f) continue;
			Vec p = mm2px(Vec(b.x, b.y));
			float r = mm2px(Vec(b.r, 0.f)).x;
			float ph = module->cloudPhase * 2.f * M_PI + b.x * 0.21f + b.y * 0.07f;
			// the voices push the plumes outward as they sound
			float rr = r * (1.f + 0.07f * std::sin(ph) + 0.10f * lvl);
			float ba = a * (0.42f + 0.12f * std::sin(ph * 0.7f));
			float cr = lerpf(b.cr * 0.75f, b.cr, warm);
			float cb = lerpf(b.cb, b.cb * 0.8f, warm);
			NVGpaint pt = nvgRadialGradient(args.vg, p.x, p.y, rr * 0.08f, rr,
				nvgRGBAf(cr, b.cg, cb, ba), nvgRGBAf(cr, b.cg, cb, 0.f));
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, p.x, p.y, rr);
			nvgFillPaint(args.vg, pt);
			nvgFill(args.vg);
		}
		nvgGlobalCompositeOperation(args.vg, NVG_SOURCE_OVER);
	}
};

// ---------------- Widget ----------------
struct EntwineWidget : ModuleWidget {
	EntwineWidget(Entwine* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Entwine.svg")));

		// added before the controls so it renders behind them
		EntwineClouds* clouds = new EntwineClouds();
		clouds->module = module;
		clouds->box.pos = Vec(0, 0);
		clouds->box.size = box.size;
		addChild(clouds);

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		const float X1 = 9.0f, X2 = 25.0f, XC = 40.64f, X4 = 56.3f, X5 = 72.3f;

		// Lock toggle at top left and its gate input at top right.
		addParam(createParamCentered<CKSS>(mm2px(Vec(X1, 12.f)), module, Entwine::LOCK_PARAM));
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(X1 + 6.f, 9.f)), module, Entwine::LOCK_LIGHT));

		// Top CV jacks: Lock, Root, Glide
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X5, 15.f)), module, Entwine::LOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X2, 15.f)), module, Entwine::ROOT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X4, 15.f)), module, Entwine::GLIDE_INPUT));

		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(X1, 28.f)), module, Entwine::POLY_PARAM));
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(XC, 26.f)), module, Entwine::SCALE_PARAM));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(mm2px(Vec(XC + 10.5f, 19.f)), module, Entwine::SCALE_LIGHT_R));
		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(X5, 28.f)), module, Entwine::SPREAD_PARAM));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X2, 40.f)), module, Entwine::ROOT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X4, 40.f)), module, Entwine::GLIDE_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X1, 42.f)), module, Entwine::POLY_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X5, 42.f)), module, Entwine::SPREAD_INPUT));

		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(X1 + 3.f, 62.f)), module, Entwine::WAVE_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(X5 - 3.f, 62.f)), module, Entwine::ENV_PARAM));

		addParam(createParamCentered<RoundHugeBlackKnob>(mm2px(Vec(XC, 72.f)), module, Entwine::COUPLING_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X1 + 3.f, 78.f)), module, Entwine::WAVE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X5 - 3.f, 78.f)), module, Entwine::ENV_INPUT));

		// Independent Pulsar and Quasar Reseed trigger inputs.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(29.4f, 87.f)), module, Entwine::PULSAR_RESEED_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(51.88f, 87.f)), module, Entwine::QUASAR_RESEED_INPUT));

		// Blue activity lights below Coupling.
		for (int i = 0; i < 3; i++)
			addChild(createLightCentered<SmallLight<BlueLight>>(
				mm2px(Vec(XC, 84.9f + i * 2.3f)), module, Entwine::COUPLING_LIGHTS + i));

		// Coupling CV input.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(XC, 97.f)), module, Entwine::COUPLING_INPUT));

		// Bottom row: Pulsar Reseed | Pulsar audio | Quasar audio | Quasar Reseed.
		addParam(createParamCentered<VCVButton>(mm2px(Vec(X1, 112.f)), module, Entwine::PULSAR_RESEED_PARAM));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(X5, 112.f)), module, Entwine::QUASAR_RESEED_PARAM));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X2, 112.f)), module, Entwine::PULSAR_OUTPUT));
		addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(X2, 104.f)), module, Entwine::PULSAR_LIGHT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X4, 112.f)), module, Entwine::QUASAR_OUTPUT));
		addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(X4, 104.f)), module, Entwine::QUASAR_LIGHT));

		// Mono pitch and gate CV outputs for each stream.
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(9.f, 100.f)), module, Entwine::PULSAR_VOCT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(18.5f, 100.f)), module, Entwine::PULSAR_GATE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(63.f, 100.f)), module, Entwine::QUASAR_VOCT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(72.3f, 100.f)), module, Entwine::QUASAR_GATE_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Entwine* m = dynamic_cast<Entwine*>(module);
		if (!m) return;

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Custom data"));
		menu->addChild(createMenuItem(
			m->wtUserLoaded ? string::f("Wavetable: %d frames (custom)", m->userWtFrames) : "Load wavetable (.wav, 256-sample frames)...",
			"", [m]() {
				char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, NULL);
				if (path) { m->loadWavetable(path); std::free(path); }
			}));
		if (m->wtUserLoaded)
			menu->addChild(createMenuItem("Restore default wavetable", "", [m]() {
				m->wtUserLoaded = false; m->userWtFrames = 0; m->wtPath.clear();
			}));
		menu->addChild(createMenuItem(
			m->scalesUserLoaded ? "Scales: custom scale.txt loaded" : "Load scales (scale.txt)...",
			"", [m]() {
				char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, NULL);
				if (path) { m->loadScales(path); std::free(path); }
			}));
		if (m->scalesUserLoaded)
			menu->addChild(createMenuItem("Restore default scales", "", [m]() {
				m->scalesUserLoaded = false; m->scalePath.clear();
			}));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("MIDI output (Pulsar=ch1, Quasar=ch2)"));
		menu->addChild(createSubmenuItem("Driver", "", [m](Menu* sub) {
			for (int id : midi::getDriverIds())
				sub->addChild(createCheckMenuItem(midi::getDriver(id)->getName(), "",
					[m, id]() { return m->midiOut.getDriverId() == id; },
					[m, id]() { m->midiOut.setDriverId(id); }));
		}));
		menu->addChild(createSubmenuItem("Device", "", [m](Menu* sub) {
			for (int id : m->midiOut.getDeviceIds())
				sub->addChild(createCheckMenuItem(m->midiOut.getDeviceName(id), "",
					[m, id]() { return m->midiOut.getDeviceId() == id; },
					[m, id]() { m->midiOut.setDeviceId(id); }));
		}));
		menu->addChild(createBoolPtrMenuItem("Monophonic (1 voice per channel)", "", &m->midiMono));
		menu->addChild(createSubmenuItem("Gate length", "", [m](Menu* sub) {
			static const float vals[] = {0.2f, 0.4f, 0.6f, 0.8f, 0.95f};
			for (float v : vals)
				sub->addChild(createCheckMenuItem(string::f("%d%%", (int)(v * 100)), "",
					[m, v]() { return std::abs(m->midiGate - v) < 1e-3f; },
					[m, v]() { m->midiGate = v; }));
		}));

		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Wavetable preset",
			{"1", "2", "3", "4", "5", "6", "7", "8"}, &m->wtPreset));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Voice shaping"));

		menu->addChild(createIndexPtrSubmenuItem("Filter type",
			{"Off", "Low-pass", "Band-pass", "High-pass"}, &m->filterType));

		menu->addChild(createSubmenuItem("Filter cutoff", "", [m](Menu* sub) {
			static const float vals[] = {0.15f, 0.3f, 0.45f, 0.6f, 0.75f, 0.9f, 1.0f};
			for (float v : vals)
				sub->addChild(createCheckMenuItem(string::f("%d%%", (int)(v * 100)), "",
					[m, v]() { return std::abs(m->filterCutoff - v) < 1e-3f; },
					[m, v]() { m->filterCutoff = v; }));
		}));
		menu->addChild(createSubmenuItem("Filter resonance", "", [m](Menu* sub) {
			static const float vals[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
			for (float v : vals)
				sub->addChild(createCheckMenuItem(string::f("%d%%", (int)(v * 100)), "",
					[m, v]() { return std::abs(m->filterQ - v) < 1e-3f; },
					[m, v]() { m->filterQ = v; }));
		}));
		menu->addChild(createSubmenuItem("Wavetable index", "", [m](Menu* sub) {
			for (int i = 0; i <= 48; i += 8)
				sub->addChild(createCheckMenuItem(string::f("+%d", i), "",
					[m, i]() { return m->wtIndex == i; },
					[m, i]() { m->wtIndex = i; }));
		}));
		menu->addChild(createSubmenuItem("Note length", "", [m](Menu* sub) {
			static const float vals[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
			for (float v : vals)
				sub->addChild(createCheckMenuItem(string::f("%gx", v), "",
					[m, v]() { return std::abs(m->noteLength - v) < 1e-3f; },
					[m, v]() { m->noteLength = v; }));
		}));
	}
};

Model* modelEntwine = createModel<Entwine, EntwineWidget>("Entwine");
