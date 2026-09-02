// Bad Sector — a stereo buffer-corruption and broken-playback processor.
//
// v2 panel architecture: six large controls (TIME, REPEAT, MIX, MICRO,
// DAMAGE, CV AMT). DAMAGE is one knob editing three independently stored
// values (Bend / Break / Corrupt) selected by an illuminated square button
// by snapping the virtual knob to the selected value; CV AMT is the same
// pattern for three unipolar CV attenuators.
//
// Layout constants mirror gen_panel.py — keep them in sync.
#include "plugin.hpp"
#include "BsSelector.hpp"
#include "BsGrid.hpp"
#include "BsClock.hpp"
#include "BsBend.hpp"
#include "BsMix.hpp"
#include "BsRepeat.hpp"
#include "BsControlLaws.hpp"
#include "BsDisplayTelemetry.hpp"
#include "BsSignalSafety.hpp"
#include <cmath>
#include <vector>

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// mode colours: Bend cyan / Break amber / Corrupt red-orange
static const float SEL_COL[3][3] = {
	{0.15f, 0.85f, 1.f}, {1.f, 0.66f, 0.08f}, {1.f, 0.22f, 0.04f}
};

struct DbRng {
	uint32_t s = 0xC0DEBEEFu;
	void seed(uint32_t v) { s = v ? v : 1u; }
	uint32_t u32() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
	float f() { return (u32() & 0xFFFFFF) / float(0x1000000); }
	float bip() { return f() * 2.f - 1.f; }
};

// TPT state-variable filter (DJ Filter corrupt)
struct SVF {
	float ic1 = 0.f, ic2 = 0.f;
	void reset() { ic1 = ic2 = 0.f; }
	void process(float in, float g, float k, float& lp, float& hp) {
		float a1 = 1.f / (1.f + g * (g + k));
		float a2 = g * a1, a3 = g * a2;
		float v3 = in - ic2;
		float v1 = a1 * ic1 + a2 * v3;
		float v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2.f * v1 - ic1;
		ic2 = 2.f * v2 - ic2;
		lp = v2; hp = in - k * v1 - v2;
	}
};

struct BadSector : Module {
	enum ParamId {
		BUFFER_PARAM, REPEAT_PARAM, MIX_PARAM, DAMAGE_PARAM, CVAMT_PARAM, MICRO_PARAM,
		DMGSEL_PARAM, CVSEL_PARAM, MODE_PARAM, CLOCKBTN_PARAM, FREEZE_PARAM, PARAMS_LEN
	};
	enum InputId {
		IN_L_INPUT, IN_R_INPUT,
		BUFFER_CV_INPUT, REPEAT_CV_INPUT, MIX_CV_INPUT,
		BEND_CV_INPUT, BREAK_CV_INPUT, CORRUPT_CV_INPUT,
		BEND_GATE_INPUT, BREAK_GATE_INPUT, CORRUPT_GATE_INPUT, FREEZE_GATE_INPUT,
		CLOCK_INPUT, RESET_INPUT, INPUTS_LEN
	};
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, OUTPUTS_LEN };
	enum LightId {
		ENUMS(DMGSEL_LIGHT, 3), ENUMS(CVSEL_LIGHT, 3),
		ENUMS(MODE_LIGHT, 3), ENUMS(CLK_LIGHT, 3), ENUMS(FRZ_LIGHT, 3),
		DOT_DMG_LIGHT, DOT_CV_LIGHT, LIGHTS_LEN
	};

	// "over a minute of stereo audio"
	static constexpr float MAX_SECONDS = 64.f;
	std::vector<float> bufL, bufR;
	int bufLen = 0, writeHead = 0;

	// playback
	// double: at high buffer addresses float only resolves 0.25-sample
	// steps, which detunes fractional speeds and drifts loop points
	double readPos[2] = {0.0, 0.0};
	int sectionStart = 0, sectionLen = 4800;
	int curSub[2] = {0, 0};
	int samplesSinceTick = 0;
	int subsActive[2] = {-1, -1};
	int lastWin[2] = {-1, -1};
	int recordedSamples = 0;
	bool bufferPrimed = false;
	float bufferPrimedFade = 0.f;
	float speed[2] = {1.f, 1.f};
	float speedTarget[2] = {1.f, 1.f};
	float speedSlew[2] = {0.f, 0.f};
	bool revNow[2] = {false, false};

	// the two shared three-channel editors
	BsSelector damage;   // bend / break / corrupt amounts (0..1)
	BsSelector cvAmt;    // bend / break / corrupt CV attenuation (0..1)

	// state
	int freezeHead = 0;
	bool wasFreezeActive = false;
	bool macro = true;
	// Bad Sector has no dedicated Bend/Break buttons, so these must start on;
	// their amount knobs are the visible enable controls on this panel.
	bool frozen = false, bendOn = true, breakOn = true;
	bool microRev = false;
	bool microSilence = false;   // Traverse default
	int corruptSel = 0;
	float windowing = 0.02f;
	float stereoWidth = 1.f;
	bool stereoUnique = true;    // Bad Sector default/restore = Unique
	float ledBrightness = 1.f;
	bool gatesMomentary = false;
	bool freezeMomentary = false;
	bool originalCorruptOnly = false;  // current firmware default: all 5 effects
	bool microInMacro = false;   // MICRO knob as global varispeed under the Macro automation
	bool freezeMixWet = false;
	bool freezeTogglePending = false;
	bool freezeButtonWasHigh = false;
	bool resetDivisionPending = false;

	// clock
	bool extClock = false;
	float internalPhase = 0.f;
	float timeKnobSmooth = 0.5f;
	BsExternalClock externalClock;
	float divBlip = 0.f, clkBlink = 0.f;

	// macro per-clock decisions (per channel when stereoUnique)
	float macroSpeed[2] = {1.f, 1.f};
	bool macroRev[2] = {false, false};
	float macroSilence[2] = {0.f, 0.f};
	float tapeStop[2] = {0.f, 0.f};
	int breakSubs[2] = {0, 0};
	// corrupt state
	float decHoldL = 0.f, decHoldR = 0.f; int decCount = 0;
	float dropEnv = 1.f; int dropTimer = 0;
	SVF djL, djR;
	float vinylPhase = 0.f;
	float vinylLpL = 0.f, vinylLpR = 0.f;
	float dcPrevInL = 0.f, dcPrevInR = 0.f, dcPrevOutL = 0.f, dcPrevOutR = 0.f;

	// telemetry for the reactive checksum artwork
	float uiBend = 0.f, uiBreak = 0.f, uiCorrupt = 0.f;
	float uiMicroOct = 0.f, uiTravBlip = 0.f;
	float uiClockPhase = 0.f, uiSubPhase = 0.f, uiSpeed = 1.f;
	bool uiMicroRev = false;
	bool uiReverse = false;
	uint32_t uiTickSerial = 0;
	int uiSlices = 1;
	int uiDivIdx = -1;   // -1 = internal clock
	BsScopeTelemetry uiScope;
	int uiScopeCounter = 0;

	DbRng rng;
	dsp::BooleanTrigger dmgSelBtn, cvSelBtn, modeBtn, clockBtn;
	dsp::SchmittTrigger clockTrig, resetTrig, bendGate, breakGate, corruptGate, freezeGate;

	BadSector() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(BUFFER_PARAM, 0.f, 1.f, 0.5f, "Time (16s .. 80Hz, or clock div/mult)");
		configParam(REPEAT_PARAM, 0.f, 1.f, 0.f, "Repeat (integer subdivisions, up to audio rate)");
		configParam(MIX_PARAM, 0.f, 1.f, 0.5f, "Mix (latency-aligned above 10%)", "%", 0.f, 100.f);
		configParam(DAMAGE_PARAM, 0.f, 1.f, 0.f, "Damage (selected channel: Bend/Break/Corrupt)");
		configParam(CVAMT_PARAM, 0.f, 1.f, 1.f, "CV attenuation (selected channel)");
		configParam(MICRO_PARAM, 0.f, 1.f, 0.5f, "Micro playback speed (+/-3 oct)");
		configButton(DMGSEL_PARAM, "Damage channel (Bend/Break/Corrupt)");
		configButton(CVSEL_PARAM, "CV amount channel (Bend/Break/Corrupt)");
		configButton(MODE_PARAM, "Mode (Macro / Micro)");
		configButton(CLOCKBTN_PARAM, "Clock source (internal / external)");
		configButton(FREEZE_PARAM, "Freeze");
		configInput(IN_L_INPUT, "Left audio (normals to both channels)");
		configInput(IN_R_INPUT, "Right audio");
		configInput(BUFFER_CV_INPUT, "Time CV");
		configInput(REPEAT_CV_INPUT, "Repeat CV");
		configInput(MIX_CV_INPUT, "Mix CV");
		configInput(BEND_CV_INPUT, "Bend CV (1V/oct Micro pitch in Micro mode)");
		configInput(BREAK_CV_INPUT, "Break CV");
		configInput(CORRUPT_CV_INPUT, "Corrupt CV (<=0V disables Corrupt)");
		configInput(BEND_GATE_INPUT, "Bend gate (Macro on/off; Micro reverse)");
		configInput(BREAK_GATE_INPUT, "Break gate (Macro on/off; Micro traverse/silence)");
		configInput(CORRUPT_GATE_INPUT, "Corrupt gate (next corrupt effect)");
		configInput(FREEZE_GATE_INPUT, "Freeze gate");
		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset (resync clock / divisions)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		damage.reset(0.f, 0.f, 0.f);
		cvAmt.reset(1.f, 1.f, 1.f);
		rng.seed(random::u32());
		alloc(48000.f);
	}

	void alloc(float sr) {
		bufLen = (int)(sr * MAX_SECONDS);
		bufL.assign(bufLen, 0.f); bufR.assign(bufLen, 0.f);
		writeHead = 0; readPos[0] = readPos[1] = 0.f; sectionStart = 0;
		recordedSamples = 0; bufferPrimed = false; bufferPrimedFade = 0.f;
		uiScope.clear(); uiScopeCounter = 0;
	}
	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		if ((int)(e.sampleRate * MAX_SECONDS) != bufLen) alloc(e.sampleRate);
	}
	void resetAutomation() {
		for (int c = 0; c < 2; c++) {
			macroSpeed[c] = speed[c] = speedTarget[c] = 1.f;
			macroRev[c] = revNow[c] = false;
			macroSilence[c] = tapeStop[c] = speedSlew[c] = 0.f;
			breakSubs[c] = 0;
		}
	}
	void restoreDefaults() {
		// Match the hardware settings where they map to this panel. Unlike the
		// hardware, Bad Sector has no dedicated Bend/Break buttons, so leaving
		// them off would make their visible amount controls appear broken.
		windowing = 0.02f; bendOn = true; breakOn = true; frozen = false;
		macro = true; stereoUnique = true;
		gatesMomentary = false; freezeMomentary = false;
		freezeMixWet = false; freezeTogglePending = false;
		resetDivisionPending = false;
		resetAutomation();
	}
	void onReset() override {
		std::fill(bufL.begin(), bufL.end(), 0.f); std::fill(bufR.begin(), bufR.end(), 0.f);
		writeHead = 0; readPos[0] = readPos[1] = 0.f; sectionStart = 0;
		curSub[0] = curSub[1] = 0;
		samplesSinceTick = 0; subsActive[0] = subsActive[1] = -1;
		lastWin[0] = lastWin[1] = -1;
		recordedSamples = 0; bufferPrimed = false; bufferPrimedFade = 0.f;
		restoreDefaults(); extClock = false; stereoWidth = 1.f;
		internalPhase = 0.f; timeKnobSmooth = 0.5f; externalClock.reset();
		freezeHead = 0; wasFreezeActive = false;
		ledBrightness = 1.f;
		microRev = false; microSilence = false; corruptSel = 0;
		originalCorruptOnly = false; microInMacro = false;
		freezeButtonWasHigh = false;
		damage.reset(0.f, 0.f, 0.f);
		cvAmt.reset(1.f, 1.f, 1.f);
		decHoldL = decHoldR = 0.f; decCount = 0;
		dropEnv = 1.f; dropTimer = 0;
		djL.reset(); djR.reset();
		vinylLpL = vinylLpR = 0.f;
		dcPrevInL = dcPrevInR = dcPrevOutL = dcPrevOutR = 0.f;
		uiScope.clear(); uiScopeCounter = 0;
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "macro", json_boolean(macro));
		json_object_set_new(r, "frozen", json_boolean(frozen));
		json_object_set_new(r, "bendOn", json_boolean(bendOn));
		json_object_set_new(r, "breakOn", json_boolean(breakOn));
		json_object_set_new(r, "enableStateVersion", json_integer(1));
		json_object_set_new(r, "microRev", json_boolean(microRev));
		json_object_set_new(r, "microSilence", json_boolean(microSilence));
		json_object_set_new(r, "extClock", json_boolean(extClock));
		json_object_set_new(r, "stereoUnique", json_boolean(stereoUnique));
		json_object_set_new(r, "corruptSel", json_integer(corruptSel));
		json_object_set_new(r, "windowing", json_real(windowing));
		json_object_set_new(r, "stereoWidth", json_real(stereoWidth));
		json_object_set_new(r, "ledBrightness", json_real(ledBrightness));
		json_object_set_new(r, "gatesMomentary", json_boolean(gatesMomentary));
		json_object_set_new(r, "freezeMomentary", json_boolean(freezeMomentary));
		json_object_set_new(r, "originalCorruptOnly", json_boolean(originalCorruptOnly));
		json_object_set_new(r, "microInMacro", json_boolean(microInMacro));
		json_object_set_new(r, "controlLawVersion", json_integer(1));
		json_t* dv = json_array();
		json_t* av = json_array();
		for (int i = 0; i < 3; i++) {
			json_array_append_new(dv, json_real(damage.vals[i]));
			json_array_append_new(av, json_real(cvAmt.vals[i]));
		}
		json_object_set_new(r, "damageVals", dv);
		json_object_set_new(r, "cvAmtVals", av);
		json_object_set_new(r, "damageSel", json_integer(damage.sel));
		json_object_set_new(r, "cvAmtSel", json_integer(cvAmt.sel));
		return r;
	}
	void dataFromJson(json_t* r) override {
		if (json_t* j = json_object_get(r, "macro")) macro = json_boolean_value(j);
		if (json_t* j = json_object_get(r, "frozen")) frozen = json_boolean_value(j);
		if (json_t* j = json_object_get(r, "bendOn")) bendOn = json_boolean_value(j);
		if (json_t* j = json_object_get(r, "breakOn")) breakOn = json_boolean_value(j);
		// One-time migration from the release that incorrectly defaulted both
		// hidden enable states off. Once saved with version 1, user-selected
		// context-menu/gate states persist normally again.
		if (!json_object_get(r, "enableStateVersion")) {
			bendOn = true;
			breakOn = true;
		}
		if (json_t* j = json_object_get(r, "microRev")) microRev = json_boolean_value(j);
		if (json_t* j = json_object_get(r, "microSilence")) microSilence = json_boolean_value(j);
		if (json_t* j = json_object_get(r, "extClock")) extClock = json_boolean_value(j);
		if (json_t* j = json_object_get(r, "stereoUnique")) stereoUnique = json_boolean_value(j);
		if (json_t* j = json_object_get(r, "corruptSel")) corruptSel = clamp((int) json_integer_value(j), 0, 4);
		if (json_t* j = json_object_get(r, "windowing")) windowing = (float) json_real_value(j);
		if (json_t* j = json_object_get(r, "stereoWidth")) stereoWidth = clampf((float) json_real_value(j), 0.f, 1.f);
		if (json_t* j = json_object_get(r, "ledBrightness")) ledBrightness = clampf((float) json_real_value(j), 0.05f, 1.f);
		if (json_t* j = json_object_get(r, "gatesMomentary")) gatesMomentary = json_boolean_value(j);
		if (json_t* j = json_object_get(r, "freezeMomentary")) freezeMomentary = json_boolean_value(j);
		if (json_t* j = json_object_get(r, "originalCorruptOnly")) originalCorruptOnly = json_boolean_value(j);
		if (json_t* j = json_object_get(r, "microInMacro")) microInMacro = json_boolean_value(j);
		bool legacyControlLaws = !json_object_get(r, "controlLawVersion");
		json_t* dv = json_object_get(r, "damageVals");
		json_t* av = json_object_get(r, "cvAmtVals");
		for (int i = 0; i < 3; i++) {
			if (dv && json_array_size(dv) == 3) damage.vals[i] = (float) json_real_value(json_array_get(dv, i));
			if (av && json_array_size(av) == 3) cvAmt.vals[i] = clampf((float) json_real_value(json_array_get(av, i)), 0.f, 1.f);
			if (legacyControlLaws)
				cvAmt.vals[i] = clampf(cvAmt.vals[i] * 2.f - 1.f, 0.f, 1.f);
		}
		// The previous control called zero width "unaltered stereo" and only
		// widened from there. The hardware curve ends at unaltered stereo, so
		// legacy patches migrate to that closest non-boosted endpoint.
		if (legacyControlLaws) stereoWidth = 1.f;
		if (json_t* j = json_object_get(r, "damageSel")) damage.sel = clamp((int) json_integer_value(j), 0, 2);
		if (json_t* j = json_object_get(r, "cvAmtSel")) cvAmt.sel = clamp((int) json_integer_value(j), 0, 2);
		if (originalCorruptOnly && corruptSel >= 3) corruptSel = 0;
		// snap the knobs to the restored channels' stored values
		params[DAMAGE_PARAM].setValue(damage.selectedSnap());
		params[CVAMT_PARAM].setValue(cvAmt.selectedSnap());
	}

	float readBuf(const std::vector<float>& b, double pos) {
		if (!std::isfinite(pos) || bufLen < 2)
			return 0.f;
		pos -= std::floor(pos / bufLen) * bufLen;
		int i0 = (int) pos; float fr = (float)(pos - i0);
		int i1 = i0 + 1; if (i1 >= bufLen) i1 = 0;
		return bsSanitizeAudio(lerpf(bsSanitizeAudio(b[i0]), bsSanitizeAudio(b[i1]), fr));
	}

	void resetInputSignalState() {
		dcPrevInL = dcPrevInR = dcPrevOutL = dcPrevOutR = 0.f;
	}

	void resetCorruptSignalState() {
		decHoldL = decHoldR = 0.f;
		decCount = 0;
		dropEnv = 1.f;
		dropTimer = 0;
		djL.reset();
		djR.reset();
		vinylLpL = vinylLpR = 0.f;
	}

	void applyCorrupt(int effect, float amt, float& l, float& r, float sr) {
		if (amt <= 0.001f) return;
		switch (effect) {
			case 0: {  // Decimate — fixed bit/downsample variations in shuffled order
				// The hardware control is deliberately not a linear crusher. Each
				// region recalls a fixed failure mode; the ordering alternates bit
				// depth, sample hold, hiss and drive so sweeping it finds distinct
				// broken-device colours rather than one steadily worsening effect.
				struct Variation { float bits; int hold; float hiss; float drive; };
				static const Variation V[16] = {
					{15.f,  1, 0.0008f, 1.0f}, {10.f,  1, 0.0000f, 1.0f},
					{14.f,  4, 0.0015f, 1.0f}, { 7.f,  2, 0.0005f, 1.1f},
					{12.f,  8, 0.0000f, 1.0f}, { 5.f,  1, 0.0025f, 1.3f},
					{ 9.f,  5, 0.0040f, 1.0f}, {13.f, 16, 0.0010f, 1.1f},
					{ 6.f,  4, 0.0070f, 1.5f}, {11.f, 24, 0.0030f, 1.1f},
					{ 4.f,  2, 0.0100f, 1.8f}, { 8.f, 32, 0.0060f, 1.4f},
					{ 3.f,  6, 0.0150f, 2.5f}, { 5.f, 48, 0.0100f, 2.0f},
					{ 2.f, 16, 0.0200f, 3.5f}, { 2.f, 55, 0.0300f, 5.0f}
				};
				const Variation& v = V[clamp((int)(amt * 16.f), 0, 15)];
				int hold = v.hold;
				if (--decCount <= 0) { decCount = hold; decHoldL = l; decHoldR = r; }
				float q = std::pow(2.f, v.bits - 1.f);
				l = std::round((decHoldL * v.drive + rng.bip() * v.hiss) * q) / q;
				r = std::round((decHoldR * v.drive + rng.bip() * v.hiss) * q) / q;
			} break;
			case 1: {  // Dropout — left: fewer but longer; right: more but shorter
				if (--dropTimer <= 0) {
					float gapLen  = lerpf(0.35f, 0.02f, amt) * sr;
					float betweenLen = lerpf(1.2f, 0.05f, amt) * sr;
					if (dropEnv < 0.5f) { dropEnv = 1.f; dropTimer = (int)(betweenLen * (0.5f + rng.f())); }
					else                { dropEnv = 0.f; dropTimer = (int)(gapLen * (0.5f + rng.f())); }
				}
				l *= dropEnv; r *= dropEnv;
			} break;
			case 2: {  // Destroy — soft saturation then absolute devastation
				if (amt < 0.5f) {
					float d = 1.f + amt * 6.f;
					l = std::tanh(l * d) / std::sqrt(d);
					r = std::tanh(r * d) / std::sqrt(d);
				} else {
					float d = 1.f + (amt - 0.5f) * 60.f;
					l = clampf(l * d, -1.f, 1.f);
					r = clampf(r * d, -1.f, 1.f);
				}
			} break;
			case 3: {  // DJ Filter — LP below noon, HP above
				float lp, hp, dummy;
				if (amt < 0.48f) {
					float t = amt / 0.48f;
					float fc = 30.f * std::pow(666.f, t);
					float g = std::tan(M_PI * clampf(fc, 20.f, 18000.f) / sr);
					djL.process(l, g, 0.8f, lp, dummy); l = lp;
					djR.process(r, g, 0.8f, lp, dummy); r = lp;
				} else if (amt > 0.52f) {
					float t = (amt - 0.52f) / 0.48f;
					float fc = 20.f * std::pow(600.f, t);
					float g = std::tan(M_PI * clampf(fc, 20.f, 18000.f) / sr);
					djL.process(l, g, 0.8f, dummy, hp); l = hp;
					djR.process(r, g, 0.8f, dummy, hp); r = hp;
				}
			} break;
			default: {  // Vinyl Sim — dust, pops and colouring
				if (rng.f() < amt * 0.0008f) {
					float c = rng.bip() * amt * 0.8f;
					l += c; r += c * 0.7f;
				}
				if (rng.f() < amt * 0.02f) {
					float c = rng.bip() * amt * 0.15f;
					l += c; r += c;
				}
				vinylPhase += 0.55f / sr; if (vinylPhase >= 1.f) vinylPhase -= 1.f;
				float fc = lerpf(18000.f, 3500.f, amt);
				float a = 1.f - std::exp(-2.f * (float) M_PI * fc / sr);
				vinylLpL += a * (l - vinylLpL);
				vinylLpR += a * (r - vinylLpR);
				float col = 1.f - amt * 0.15f;
				l = vinylLpL * (col + amt * 0.05f * std::sin(2.f * M_PI * vinylPhase));
				r = vinylLpR * (col + amt * 0.05f * std::sin(2.f * M_PI * vinylPhase + 0.7f));
			} break;
		}
	}

	// Every clock division, Macro mode rolls new manipulations. Both Bend and
	// Break use CUMULATIVE knob zones.
	void rollMacro(float bendAmt, float breakAmt, int repeats, bool bendEnabled, bool breakEnabled) {
		int nCh = stereoUnique ? 2 : 1;
		if (bendEnabled && bendAmt > 0.001f) {
			// Manual model: ONE playback speed + direction decision per clock
			// division, whole-division scope, palette growing through the knob
			// zones Reverse / Octaves / 2 Octaves / Tape Stop / Slew /
			// Everything. No sub-division patterns, no continuous wobble —
			// Repeat provides the stutters.
			for (int c = 0; c < nCh; c++) {
				BsBendDecision d = bsBendDecision(bendAmt,
					rng.f(), rng.f(), rng.f(), rng.f(), rng.f(), rng.f());
				macroSpeed[c] = d.speed;
				macroRev[c] = d.reverse;
				if (d.tapeStop) tapeStop[c] = 1.f;
				speedSlew[c] = d.slew;   // 0..1, scaled by period at use
			}
			if (!stereoUnique) {
				macroSpeed[1] = macroSpeed[0]; macroRev[1] = macroRev[0];
				tapeStop[1] = tapeStop[0]; speedSlew[1] = speedSlew[0];
			}
		} else {
			macroSpeed[0] = macroSpeed[1] = 1.f;
			macroRev[0] = macroRev[1] = false;
			speedSlew[0] = speedSlew[1] = 0.f;
		}

		if (breakEnabled && breakAmt > 0.001f) {
			int z = (int) std::ceil(breakAmt * 6.f);
			float top = clampf(breakAmt * 6.f - (z - 1), 0.f, 1.f);
			auto za = [&](int k) { return (k < z) ? 1.f : (k == z ? top : 0.f); };
			for (int c = 0; c < nCh; c++) {
				// Extra repeats stay integer clock subdivisions, including counts
				// such as the manual's explicit 10-repeat example.
				int subs = std::max(1, repeats);
				if (z >= 1 && rng.f() < za(1) * 0.5f) subs = std::max(subs, 2);
				if (z >= 3 && rng.f() < za(3) * 0.6f)
					subs = std::max(subs, bsBreakMoreSubsections(repeats, rng.f()));
				if (z >= 4 && rng.f() < za(4) * 0.5f)
					subs = std::max(subs, bsBreakAudioRateSubsections(rng.f()));
				if (z >= 2 && rng.f() < za(2) * 0.7f) curSub[c] = (int)(rng.f() * subs);
				macroSilence[c] = (z >= 5) ? za(5) * 0.9f * rng.f() : 0.f;
				breakSubs[c] = subs;
			}
			if (!stereoUnique) {
				curSub[1] = curSub[0]; macroSilence[1] = macroSilence[0]; breakSubs[1] = breakSubs[0];
			}
		} else {
			macroSilence[0] = macroSilence[1] = 0.f;
			breakSubs[0] = breakSubs[1] = 0;
		}
	}

	void process(const ProcessArgs& args) override {
		float dt = args.sampleTime, sr = args.sampleRate;
		if (bufLen < 8) return;

		// ---- shared editors: selector buttons recall the stored value (the
		// knob pointer snaps to show it), then the knob edits directly ----
		if (dmgSelBtn.process(params[DMGSEL_PARAM].getValue() > 0.5f)) {
			params[DAMAGE_PARAM].setValue(
				damage.advanceSnap(params[DAMAGE_PARAM].getValue()));
		}
		if (cvSelBtn.process(params[CVSEL_PARAM].getValue() > 0.5f)) {
			params[CVAMT_PARAM].setValue(
				cvAmt.advanceSnap(params[CVAMT_PARAM].getValue()));
		}
		damage.track(params[DAMAGE_PARAM].getValue());
		cvAmt.track(params[CVAMT_PARAM].getValue());

		// Hardware unipolar attenuation: CCW blocks CV, CW passes it fully.
		float att[3];
		for (int i = 0; i < 3; i++) att[i] = bsCvAttenuation(cvAmt.vals[i]);

		// ---- effective control values ----
		// A short slew on the manual control prevents abrupt acquisition-period
		// jumps while turning TIME. CV remains unslewed so deliberate clocked
		// division changes retain their timing precision.
		float timeKnob = params[BUFFER_PARAM].getValue();
		float timeSlew = 1.f - std::exp(-dt / 0.04f);
		timeKnobSmooth += (timeKnob - timeKnobSmooth) * timeSlew;
		float timeN = clampf(timeKnobSmooth + inputs[BUFFER_CV_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float repeatsN = clampf(params[REPEAT_PARAM].getValue() + inputs[REPEAT_CV_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float mixN = clampf(params[MIX_PARAM].getValue() + inputs[MIX_CV_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float bendN = clampf(damage.vals[0] + inputs[BEND_CV_INPUT].getVoltage() * 0.1f * att[0], 0.f, 1.f);
		float breakN = clampf(damage.vals[1] + inputs[BREAK_CV_INPUT].getVoltage() * 0.1f * att[1], 0.f, 1.f);
		float corruptN = damage.vals[2];
		if (inputs[CORRUPT_CV_INPUT].isConnected()) {
			float cv = inputs[CORRUPT_CV_INPUT].getVoltage();
			corruptN = (cv <= 0.f) ? 0.f : clampf(damage.vals[2] + cv * 0.1f * att[2], 0.f, 1.f);
		}

		// ---- buttons + gates ----
		if (modeBtn.process(params[MODE_PARAM].getValue() > 0.5f)) macro = !macro;
		if (clockBtn.process(params[CLOCKBTN_PARAM].getValue() > 0.5f)) {
			extClock = !extClock;
			// Fresh state on a source switch: the first external edge is an
			// authoritative downbeat; internal restarts a full period.
			externalClock.reset();
			internalPhase = 0.f;
			resetDivisionPending = false;
		}
		bool freezeButtonHigh = params[FREEZE_PARAM].getValue() > 0.5f;
		bool freezePress = !freezeButtonWasHigh && freezeButtonHigh;
		bool freezeRelease = freezeButtonWasHigh && !freezeButtonHigh;
		freezeButtonWasHigh = freezeButtonHigh;
		if (!freezeMomentary && freezeRelease)
			freezeTogglePending = !freezeTogglePending;
		(void) freezePress;

		bool bendGateHigh = inputs[BEND_GATE_INPUT].getVoltage() >= 0.4f;
		bool breakGateHigh = inputs[BREAK_GATE_INPUT].getVoltage() >= 0.4f;
		bool corruptGateHigh = inputs[CORRUPT_GATE_INPUT].getVoltage() >= 0.4f;
		bool freezeGateHigh = inputs[FREEZE_GATE_INPUT].getVoltage() >= 0.4f;
		bool bendGateEdge = bendGate.process(inputs[BEND_GATE_INPUT].getVoltage(), 0.1f, 0.4f);
		bool breakGateEdge = breakGate.process(inputs[BREAK_GATE_INPUT].getVoltage(), 0.1f, 0.4f);
		bool corruptGateEdge = corruptGate.process(inputs[CORRUPT_GATE_INPUT].getVoltage(), 0.1f, 0.4f);
		bool freezeGateEdge = freezeGate.process(inputs[FREEZE_GATE_INPUT].getVoltage(), 0.1f, 0.4f);

		if (!gatesMomentary) {
			if (bendGateEdge) { if (macro) bendOn = !bendOn; else microRev = !microRev; }
			if (breakGateEdge) { if (macro) breakOn = !breakOn; else microSilence = !microSilence; }
			if (freezeGateEdge) freezeTogglePending = !freezeTogglePending;
			if (corruptGateEdge)
				corruptSel = (corruptSel + 1) % (originalCorruptOnly ? 3 : 5);
		}
		bool bendEnabled = bendOn || (gatesMomentary && bendGateHigh);
		bool breakEnabled = breakOn || (gatesMomentary && breakGateHigh);
		bool reverseEnabled = microRev || (gatesMomentary && bendGateHigh);
		bool silenceEnabled = microSilence || (gatesMomentary && breakGateHigh);
		if (originalCorruptOnly && corruptSel >= 3) corruptSel = 0;
		int corruptEffect = corruptSel;
		if (gatesMomentary && corruptGateHigh)
			corruptEffect = (corruptEffect + 1) % (originalCorruptOnly ? 3 : 5);

		// ---- clock ----
		bool tick = false;
		float period;
		bool firstExternalEdge = false;
		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 0.4f)) {
			if (extClock) resetDivisionPending = true;
			else { internalPhase = 0.f; tick = true; }
		}
		bool clockLost = false;
		if (extClock) {
			int d = clamp((int)(timeN * 8.99f), 0, 8);
			uiDivIdx = d;
			bool edge = clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 0.4f);
			bool resetOnEdge = edge && resetDivisionPending;
			BsClockResult cr = externalClock.process(dt, d, edge, resetOnEdge);
			period = cr.period;
			tick = cr.tick;
			clockLost = cr.clockLost;
			firstExternalEdge = cr.firstEdge;
			if (cr.divisionChanged) divBlip = 0.35f;
			if (cr.consumedReset) resetDivisionPending = false;
		} else {
			period = 16.f * std::pow(1.f / 1280.f, timeN);
			internalPhase += dt / period;
			if (internalPhase >= 1.f) { internalPhase -= std::floor(internalPhase); tick = true; }
			uiDivIdx = -1;
		}
		if (tick) uiTickSerial++;
		uiClockPhase = std::fmod((float)(extClock ? externalClock.phase : internalPhase), 1.f);
		if (uiClockPhase < 0.f) uiClockPhase += 1.f;

		// Every integer count is reachable. The exponential curve keeps useful
		// low stutter counts in the first half and audio rate near the top.
		int repeats = bsRepeatCount(repeatsN);

		if (tick && freezeTogglePending) {
			frozen = !frozen;
			freezeTogglePending = false;
			if (frozen && mixN < 0.02f) freezeMixWet = true;
			if (!frozen) freezeMixWet = false;
		}
		bool momentaryFreeze = (freezeMomentary && freezeButtonHigh)
			|| (gatesMomentary && freezeGateHigh);
		bool freezeRequested = frozen || momentaryFreeze;
		bool freezeActive = bsEffectiveFreeze(freezeRequested, bufferPrimed);
		if (momentaryFreeze && mixN < 0.02f) freezeMixWet = true;
		if (!freezeRequested) freezeMixWet = false;
		if (freezeActive && !wasFreezeActive) freezeHead = writeHead;
		wasFreezeActive = freezeActive;

		if (tick) {
			// each tick acquires the just-completed division; that is what
			// gets mangled during this division (always beat-aligned audio)
			if (!freezeActive) {
				sectionLen = (!firstExternalEdge && samplesSinceTick > 32 && samplesSinceTick < bufLen)
					? samplesSinceTick
					: clamp((int)(period * sr), 32, bufLen - 1);
				sectionStart = writeHead - sectionLen;
				while (sectionStart < 0) sectionStart += bufLen;
			}
			else {
				// frozen: the window reaches BACK from the freeze point, so
				// lengthening Time digs into older audio history
				sectionLen = clamp((int)(period * sr), 32, bufLen - 1);
				sectionStart = freezeHead - sectionLen;
				while (sectionStart < 0) sectionStart += bufLen;
			}
			if (!bufferPrimed && bsBufferCaptureReady(recordedSamples, sectionLen))
				bufferPrimed = true;
			samplesSinceTick = 0;
			curSub[0] = curSub[1] = 0;
			readPos[0] = readPos[1] = sectionStart;
			tapeStop[0] = tapeStop[1] = 0.f;
			lastWin[0] = lastWin[1] = -1;
			subsActive[0] = subsActive[1] = -1;
			if (macro) rollMacro(bendN, breakN, repeats, bendEnabled, breakEnabled);
			clkBlink = 1.f;
		}
		// Saturation also makes the first external edge after a long wait use
		// the estimated clock period instead of an overflowed sample count.
		samplesSinceTick = std::min(samplesSinceTick + 1, bufLen);

		// ---- write (background, always, unless frozen) ----
		float rawInL = bsSanitizeAudio(inputs[IN_L_INPUT].getVoltage() * 0.2f);
		float rawInR = inputs[IN_R_INPUT].isConnected()
			? bsSanitizeAudio(inputs[IN_R_INPUT].getVoltage() * 0.2f) : rawInL;
		if (!bsFiniteAudioState(dcPrevInL, dcPrevInR, dcPrevOutL, dcPrevOutR))
			resetInputSignalState();
		float dcPole = std::exp(-2.f * (float) M_PI * 5.f / sr);
		float inL = bsSanitizeAudio(rawInL - dcPrevInL + dcPole * dcPrevOutL);
		float inR = bsSanitizeAudio(rawInR - dcPrevInR + dcPole * dcPrevOutR);
		dcPrevInL = rawInL; dcPrevInR = rawInR; dcPrevOutL = inL; dcPrevOutR = inR;
		if (!freezeActive) {
			bufL[writeHead] = inL; bufR[writeHead] = inR;
			if (++writeHead >= bufLen) writeHead = 0;
			recordedSamples = std::min(recordedSamples + 1, bufLen);
		}

		// ---- playback ----
		float microOct = params[MICRO_PARAM].getValue() * 6.f - 3.f;
		if (inputs[BEND_CV_INPUT].isConnected() && !macro)
			microOct += inputs[BEND_CV_INPUT].getVoltage();   // 1V/oct in Micro
		float microSpeed = std::pow(2.f, clampf(microOct, -3.f, 3.f));
		float wet[2] = {0.f, 0.f};
		float bufferedDry[2] = {0.f, 0.f};
		float subPhase[2] = {0.f, 0.f};
		const std::vector<float>* channelBuf[2] = {&bufL, &bufR};
		for (int c = 0; c < 2; c++) {
			if (!std::isfinite(readPos[c]) || !std::isfinite(speed[c])
			    || !std::isfinite(speedTarget[c]) || !std::isfinite(speedSlew[c])) {
				readPos[c] = sectionStart;
				speed[c] = speedTarget[c] = 1.f;
				speedSlew[c] = 0.f;
			}
			int target = std::max(1, macro && breakSubs[c] > 0 ? breakSubs[c] : repeats);
			target = clamp(target, 1, std::max(1, sectionLen / 4));
			int elapsedT = samplesSinceTick - 1;
			// If TIME is lengthened mid-cycle, the next acquisition boundary
			// moves later. Loop the previous beat-aligned section until it arrives
			// instead of holding the final window envelope at zero (the audible
			// dropout that used to occur while turning TIME).
			int gridT = bsGridPlaybackTime(elapsedT, sectionLen);
			// Unprocessed audio from the same acquired division as the wet head.
			// MIX uses this instead of live input after its short dry-end
			// transition, removing the one-TIME-period double image at 50%.
			bufferedDry[c] = readBuf(*channelBuf[c], sectionStart + (double) gridT);
			int winIdx = 0;
			// Resolve a pending Repeat change before deriving Bend's pattern,
			// direction, speed, traverse position or envelope. The previous
			// order could start a new grid window using the old grid's reverse
			// decision, then flip direction one sample later.
			bool retrigger = bsGridAdvance(gridT, sectionLen, target,
				subsActive[c], lastWin[c], winIdx);
			int subs = subsActive[c];
			curSub[c] = clamp(curSub[c], 0, subs - 1);
			double subLen = (double) sectionLen / subs;

			if (macro) {
				// one whole-division gesture (manual behaviour); the MICRO
				// knob can optionally transpose the whole mangling
				speedTarget[c] = macroSpeed[c] * (microInMacro ? microSpeed : 1.f);
				revNow[c] = macroRev[c];
				if (tapeStop[c] > 0.f) {
					tapeStop[c] = std::max(0.f, tapeStop[c] - dt / clampf(period, 0.05f, 4.f));
					speedTarget[c] *= tapeStop[c] * tapeStop[c];
				}
			} else {
				speedTarget[c] = microSpeed;
				revNow[c] = reverseEnabled;
				speedSlew[c] = 0.f;
			}
			// slew scaled to the division so glides complete musically at any
			// tempo; the 4ms floor de-zippers hard varispeed switches
			float slewSec = std::max(0.004f, speedSlew[c] * 0.35f * clampf(period, 0.05f, 4.f));
			speed[c] += (speedTarget[c] - speed[c]) * (1.f - std::exp(-dt / slewSec));

			float silence = macro ? macroSilence[c] : (silenceEnabled ? breakN * 0.9f : 0.f);
			int want = curSub[c];
			if (!macro && !silenceEnabled)
				want = clamp((int)(breakN * subs), 0, subs - 1);   // Traverse

			// TIME-GRID RETRIGGER: each window restarts the slice on the wall
			// clock, so stutter transients land on the grid at ANY speed or
			// direction — content-based wrapping made pitched repeats drift
			// against the beat (audible immediately on drums)
			if (retrigger) {
				if (want != curSub[c]) {
					curSub[c] = want;
					if (!macro) uiTravBlip = 1.f;   // hardware: gold blip on traverse
				}
				double ss = sectionStart + curSub[c] * subLen;
				readPos[c] = revNow[c] ? (ss + subLen - 1.0) : ss;
			}
			double subStart = sectionStart + curSub[c] * subLen;
			double rel = readPos[c] - subStart;
			rel -= std::floor(rel / subLen) * subLen;
			readPos[c] = subStart + rel;
			// envelope/silence phase follows the exact TIME window
			int ws = bsGridStart(winIdx, sectionLen, subs);
			int wl = std::max(1, bsGridStart(winIdx + 1, sectionLen, subs) - ws);
			subPhase[c] = clampf((float)(gridT - ws) / (float) wl, 0.f, 1.f);
			wet[c] = readBuf(*channelBuf[c], readPos[c]);
			// the manual's "vinyl clicks and pops" are the natural hard-edge
			// discontinuities from reverses and pitch jumps (tamed by Glitch
			// Windowing) — no synthetic crackle or wobble is injected
			readPos[c] += (double) speed[c] * (revNow[c] ? -1.0 : 1.0);

			if (silence > 0.f && subPhase[c] > (1.f - silence)) wet[c] = 0.f;
			if (windowing > 0.001f) {
				float w = clampf(subPhase[c] / windowing, 0.f, 1.f)
				        * clampf((1.f - subPhase[c]) / windowing, 0.f, 1.f);
				// peak-normalize so full windowing still reaches full volume
				if (windowing > 0.5f) {
					float pk = 0.5f / windowing;
					w /= pk * pk;
				}
				wet[c] *= w;
			}
		}
		float wetL = wet[0], wetR = wet[1];

		bsStereoEnhance(wetL, wetR, stereoWidth);
		float bentBrokenL = wetL, bentBrokenR = wetR;

		applyCorrupt(corruptEffect, corruptN, wetL, wetR, sr);
		if (!std::isfinite(wetL) || !std::isfinite(wetR)) {
			// Stateful corrupt filters used to remain NaN forever after one bad
			// upstream sample. Recover immediately without resetting musical state.
			wetL = wetR = 0.f;
			resetCorruptSignalState();
		}

		// telemetry for the reactive artwork
		uiMicroOct = microOct;
		uiMicroRev = reverseEnabled;
		uiTravBlip = std::max(0.f, uiTravBlip - dt * 4.f);
		uiSubPhase = subPhase[0];
		uiSpeed = speed[0];
		uiReverse = revNow[0];
		uiSlices = std::max(1, subsActive[0]);
		uiBend = macro ? (bendEnabled ? bendN : 0.f) : std::fabs(microOct) / 3.f;
		uiBreak = macro ? (breakEnabled ? breakN : 0.f) : breakN;
		uiCorrupt = corruptN;

		// Percentage-linear mix. Above 10%, dry and wet both reference the same
		// acquired division, so an unchanged buffer is unity gain at 50% rather
		// than a live signal plus a one-division-late echo.
		float mix = freezeMixWet ? 1.f : mixN;
		float primeSlew = 1.f - std::exp(-dt / 0.02f);
		bufferPrimedFade += ((bufferPrimed ? 1.f : 0.f) - bufferPrimedFade) * primeSlew;
		BsMixGains mg = bsPrimedMixGains(mix, bufferPrimedFade);
		float outL = bsSanitizeAudio(inL * mg.liveDry + bufferedDry[0] * mg.bufferedDry + wetL * mg.wet);
		float outR = bsSanitizeAudio(inR * mg.liveDry + bufferedDry[1] * mg.bufferedDry + wetR * mg.wet);
		float sentOutL = clampf(outL, -1.4f, 1.4f);
		float sentOutR = clampf(outR, -1.4f, 1.4f);
		outputs[OUT_L_OUTPUT].setVoltage(sentOutL * 5.f);
		outputs[OUT_R_OUTPUT].setVoltage(sentOutR * 5.f);

		// Feed the LCD from the real audio path at roughly 3 kHz. Each point is
		// time-coherent across all five stages, so the display shows precisely
		// what Bend/Break, Corrupt and Mix did to this sample. The audio thread
		// publishes lock-free data; the UI never reads the circular audio buffer.
		int scopeDecimation = std::max(1, (int)(sr / 3000.f));
		if (++uiScopeCounter >= scopeDecimation) {
			uiScopeCounter = 0;
			BsScopeFrame frame;
			frame.input = 0.5f * (inL + inR);
			frame.aligned = 0.5f * (bufferedDry[0] + bufferedDry[1]);
			frame.bentBroken = 0.5f * (bentBrokenL + bentBrokenR);
			frame.corrupted = 0.5f * (wetL + wetR);
			frame.output = 0.5f * (sentOutL + sentOutR);
			uiScope.publish(frame);
		}

		// ---- LEDs ----
		clkBlink = std::max(0.f, clkBlink - dt * 6.f);
		divBlip = std::max(0.f, divBlip - dt * 3.f);
		auto setLed = [&](int id, float value) {
			lights[id].setBrightnessSmooth(value * ledBrightness, dt);
		};

		// selector buttons: mode colour (virtual knobs snap on selection)
		float dBlink = damage.caught ? 1.f : (0.35f + 0.65f * (std::sin(args.frame * 0.0006f) > 0.f ? 1.f : 0.f));
		float aBlink = cvAmt.caught ? 1.f : (0.35f + 0.65f * (std::sin(args.frame * 0.0006f) > 0.f ? 1.f : 0.f));
		// the Bend channel is inert in Micro (manual speed replaces the
		// automation) — dim its colour there so the knob never reads as dead
		float dAct = (!macro && damage.sel == 0) ? 0.25f : 1.f;
		float aAct = (!macro && cvAmt.sel == 0) ? 0.25f : 1.f;
		for (int i = 0; i < 3; i++) {
			setLed(DMGSEL_LIGHT + i, SEL_COL[damage.sel][i] * dBlink * dAct);
			setLed(CVSEL_LIGHT + i, SEL_COL[cvAmt.sel][i] * aBlink * aAct);
		}
		// dots: the selected channel's stored value at a glance
		setLed(DOT_DMG_LIGHT, 0.1f + 0.9f * damage.vals[damage.sel]);
		setLed(DOT_CV_LIGHT, 0.1f + 0.9f * att[cvAmt.sel]);

		setLed(MODE_LIGHT + 0, 0.f);
		setLed(MODE_LIGHT + 1, macro ? 0.f : 1.f);
		setLed(MODE_LIGHT + 2, macro ? 1.f : 0.f);

		float cr = 0.f, cg = 0.f, cb = 0.f;
		float pulse = 0.3f + 0.7f * clkBlink;
		if (divBlip > 0.f) { cr = divBlip; cg = divBlip * 0.65f; }
		else if (!extClock) { cb = pulse; }
		else if (clockLost) { cr = cg = cb = 0.15f; }
		else { cr = cg = cb = pulse; }
		setLed(CLK_LIGHT + 0, cr); setLed(CLK_LIGHT + 1, cg); setLed(CLK_LIGHT + 2, cb);

		setLed(FRZ_LIGHT + 0, 0.f);
		setLed(FRZ_LIGHT + 1, freezeActive ? 0.8f : 0.f);
		setLed(FRZ_LIGHT + 2, freezeActive ? 1.f : (freezeTogglePending ? 0.25f : 0.f));
	}
};

// ------------------------------------------------------ custom hardware ----
struct BsScrew : app::SvgScrew {
	BsScrew() { setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/screw.svg"))); }
};
struct BsPort : app::SvgPort {
	BsPort() { setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/port.svg"))); }
};
struct BsSqButton : app::SvgSwitch {
	BsSqButton() {
		momentary = true;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/sqbtn_0.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/sqbtn_1.svg")));
	}
};
struct BsSqButtonSmall : app::SvgSwitch {
	BsSqButtonSmall() {
		momentary = true;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/sqbtn_s0.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/sqbtn_s1.svg")));
	}
};
// square light insert for the square buttons
struct BsSqLight : RedGreenBlueLight {
	BsSqLight() { box.size = mm2px(math::Vec(4.6f, 4.6f)); }
	void drawBackground(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, mm2px(0.45f));
		nvgFillColor(args.vg, bgColor);
		nvgFill(args.vg);
	}
	void drawLight(const DrawArgs& args) override {
		if (color.a <= 0.f) return;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, mm2px(0.45f));
		nvgFillColor(args.vg, color);
		nvgFill(args.vg);
	}
	// the default size-scaled halo floods the panel from these large squares
	void drawHalo(const DrawArgs& args) override {}
};
struct BsSqLightSmall : BsSqLight {
	BsSqLightSmall() { box.size = mm2px(math::Vec(3.4f, 3.4f)); }
};
struct CyanLight : GrayModuleLightWidget {
	CyanLight() { addBaseColor(nvgRGB(0x35, 0xd3, 0xe0)); }
};

// ------------------------------------------ real signal/data display ----
// This is the active LCD renderer. It retains the broken-data visual language
// of the original artwork, but every moving mark is derived from synchronized
// samples published by the DSP above. There is no UI RNG or free-running phase.
struct BsSignalDataArt : TransparentWidget {
	BadSector* module = nullptr;
	static constexpr float X0 = 28.5f, X1 = 52.8f, Y0 = 16.5f, Y1 = 49.5f;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		NVGcontext* vg = args.vg;
		float W = box.size.x, H = box.size.y;
		if (W <= 1.f || H <= 1.f) return;

		std::array<BsScopeFrame, BS_SCOPE_POINTS> frames;
		int count = module ? module->uiScope.snapshot(frames) : 0;
		float bend = module ? clampf(module->uiBend, 0.f, 1.f) : 0.f;
		float brk = module ? clampf(module->uiBreak, 0.f, 1.f) : 0.f;
		float corrupt = module ? clampf(module->uiCorrupt, 0.f, 1.f) : 0.f;
		float clockPhase = module ? clampf(module->uiClockPhase, 0.f, 0.999999f) : 0.f;
		float subPhase = module ? clampf(module->uiSubPhase, 0.f, 0.999999f) : 0.f;
		float traverse = module ? module->uiTravBlip : 0.f;
		bool reverse = module && module->uiReverse;
		bool frozen = module && module->wasFreezeActive;
		int slices = module ? clamp(module->uiSlices, 1, 128) : 1;

		float outSq = 0.f, peak = 0.f;
		for (int i = 0; i < count; i++) {
			outSq += frames[i].output * frames[i].output;
			peak = std::max(peak, std::fabs(frames[i].output));
		}
		float outRms = count > 0 ? std::sqrt(outSq / count) : 0.f;
		uint16_t checksum = bsScopeChecksum(frames.data(), count);

		float pad = mm2px(0.8f);
		float headerH = mm2px(4.8f);
		float bodyY = pad + headerH + mm2px(0.8f);
		float bodyBottom = H - pad;
		float bodyH = std::max(1.f, bodyBottom - bodyY);
		float bodyW = W - 2.f * pad;

		nvgSave(vg);
		nvgScissor(vg, RECT_ARGS(args.clipBox));

		// The same dark glass and effect-coloured frame as the previous design.
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0.5f, 0.5f, W - 1.f, H - 1.f, mm2px(1.f));
		NVGpaint glass = nvgLinearGradient(vg, 0.f, 0.f, 0.f, H,
			nvgRGBA(0x06, 0x0d, 0x12, 0xee), nvgRGBA(0x02, 0x05, 0x08, 0xf8));
		nvgFillPaint(vg, glass);
		nvgFill(vg);
		NVGcolor edge = nvgRGBA(0x35, 0xd3, 0xe0, 0xa8);
		if (corrupt > brk && corrupt > bend) edge = nvgRGBA(0xff, 0x38, 0x0a, 0xb8);
		else if (brk > bend) edge = nvgRGBA(0xff, 0xa8, 0x15, 0xb0);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0.5f, 0.5f, W - 1.f, H - 1.f, mm2px(1.f));
		nvgStrokeColor(vg, edge);
		nvgStrokeWidth(vg, 1.1f);
		nvgStroke(vg);

		// Header: the light is a real output level meter; the checksum is formed
		// from the actual output samples currently visible on the LCD.
		nvgBeginPath(vg);
		nvgRoundedRect(vg, pad, pad, bodyW, headerH, mm2px(0.55f));
		nvgFillColor(vg, nvgRGBA(0x0d, 0x18, 0x20, 0xf0));
		nvgFill(vg);
		float ledX = pad + mm2px(1.25f), ledY = pad + headerH * 0.5f;
		float level = clampf(outRms * 3.f + peak * 0.35f, 0.f, 1.f);
		NVGpaint halo = nvgRadialGradient(vg, ledX, ledY, 0.f, mm2px(1.4f),
			nvgRGBA(0x35, 0xd3, 0xe0, (unsigned char)(0xb0 * level)),
			nvgRGBA(0x35, 0xd3, 0xe0, 0x00));
		nvgBeginPath(vg);
		nvgCircle(vg, ledX, ledY, mm2px(1.4f));
		nvgFillPaint(vg, halo);
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, ledX, ledY, mm2px(0.42f));
		nvgFillColor(vg, nvgRGBA(0x73, 0xf4, 0xff,
			(unsigned char)(0x20 + level * 0xdf)));
		nvgFill(vg);

		std::shared_ptr<Font> font = APP->window->loadFont(
			asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (font) {
			static const char* DIVS[9] = {"/16", "/8", "/4", "/2", "x1", "x2", "x3", "x4", "x8"};
			static const char* FX[5] = {"DEC", "DRP", "DST", "DJF", "VYL"};
			char txt[64];
			if (module && !module->macro)
				snprintf(txt, sizeof(txt), "MIC %+.1f %04X%s", module->uiMicroOct,
					(unsigned)checksum, reverse ? " R" : "");
			else if (module)
				snprintf(txt, sizeof(txt), "%s %s %04X%s",
					module->uiDivIdx < 0 ? "INT" : DIVS[clamp(module->uiDivIdx, 0, 8)],
					FX[clamp(module->corruptSel, 0, 4)], (unsigned)checksum, frozen ? " F" : "");
			else
				snprintf(txt, sizeof(txt), "NO DATA 0000");
			nvgFontFaceId(vg, font->handle);
			nvgFontSize(vg, mm2px(1.95f));
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, nvgRGBA(0xff, 0x54, 0x44, 0xf0));
			nvgText(vg, W * 0.56f, ledY + 0.2f, txt, NULL);
		}

		// Stable packet lanes retain the previous data-field style. Nothing moves
		// along them unless real samples or the real clock/read heads move.
		const int rows = 13;
		float rowH = bodyH / rows;
		for (int row = 0; row < rows; row++) {
			float y = bodyY + (row + 0.5f) * rowH;
			nvgBeginPath(vg);
			nvgRect(vg, pad, y, bodyW, std::max(0.55f, rowH * 0.10f));
			nvgFillColor(vg, nvgRGBA(0x24, 0x55, 0x60, 0x58));
			nvgFill(vg);
		}

		// Repeat/Break grid: exact active DSP subdivision count, capped only for
		// legibility. These divisions reset with the same clock as the audio.
		int shownSlices = std::min(slices, 16);
		for (int i = 1; i < shownSlices; i++) {
			float gx = pad + bodyW * i / shownSlices;
			nvgBeginPath(vg);
			nvgMoveTo(vg, gx, bodyY);
			nvgLineTo(vg, gx, bodyBottom);
			nvgStrokeColor(vg, nvgRGBA(0xff, 0xa8, 0x15,
				(unsigned char)(0x28 + brk * 0x78)));
			nvgStrokeWidth(vg, 0.75f);
			nvgStroke(vg);
		}

		auto waveY = [&](float sample) {
			return bodyY + bodyH * 0.5f - clampf(sample, -1.4f, 1.4f) * bodyH * 0.32f;
		};
		if (count > 1) {
			// Actual sample packets. Their row is the quantized final output value;
			// colour is chosen by the real stage that changed that sample most.
			int columns = std::min(count, 72);
			float cellW = bodyW / columns;
			for (int col = 0; col < columns; col++) {
				int index = col * (count - 1) / std::max(1, columns - 1);
				const BsScopeFrame& f = frames[index];
				float bendBreakDelta = std::fabs(f.bentBroken - f.aligned);
				float corruptDelta = std::fabs(f.corrupted - f.bentBroken);
				float activity = std::max(std::fabs(f.input), std::fabs(f.aligned));
				activity = std::max(activity, std::fabs(f.bentBroken));
				activity = std::max(activity, std::fabs(f.corrupted));
				activity = std::max(activity, std::fabs(f.output));
				activity = std::max(activity, std::max(bendBreakDelta, corruptDelta));
				if (activity < 0.001f) continue;
				float x = pad + col * cellW + cellW * 0.12f;
				int row = clamp((int)((1.f - (clampf(f.output, -1.4f, 1.4f) / 1.4f + 1.f) * 0.5f) * rows), 0, rows - 1);
				float y = bodyY + (row + 0.5f) * rowH;
				NVGcolor color = nvgRGBA(0x78, 0xe8, 0xf2, 0xc8);
				if (corruptDelta > 0.006f && corruptDelta >= bendBreakDelta)
					color = nvgRGBA(0xff, 0x38, 0x0a, 0xe8);
				else if (bendBreakDelta > 0.006f && brk >= bend)
					color = nvgRGBA(0xff, 0xa8, 0x15, 0xdf);
				else if (bendBreakDelta > 0.006f)
					color = nvgRGBA(0x26, 0xd9, 0xff, 0xe2);
				nvgBeginPath(vg);
				nvgRoundedRect(vg, x, y - rowH * 0.28f,
					std::max(1.f, cellW * 0.76f), std::max(1.f, rowH * 0.56f), 0.7f);
				nvgFillColor(vg, color);
				nvgFill(vg);

				// The vertical split is the measured displacement introduced between
				// aligned buffer, Bend/Break, and Corrupt at this exact sample.
				if ((col & 1) == 0 && (bendBreakDelta > 0.003f || corruptDelta > 0.003f)) {
					nvgBeginPath(vg);
					nvgMoveTo(vg, x + cellW * 0.38f, waveY(f.aligned));
					nvgLineTo(vg, x + cellW * 0.38f, waveY(f.bentBroken));
					nvgStrokeColor(vg, brk >= bend ? nvgRGBA(0xff, 0xa8, 0x15, 0x88)
						: nvgRGBA(0x26, 0xd9, 0xff, 0x88));
					nvgStrokeWidth(vg, 1.f);
					nvgStroke(vg);
					nvgBeginPath(vg);
					nvgMoveTo(vg, x + cellW * 0.38f, waveY(f.bentBroken));
					nvgLineTo(vg, x + cellW * 0.38f, waveY(f.corrupted));
					nvgStrokeColor(vg, nvgRGBA(0xff, 0x38, 0x0a, 0x98));
					nvgStrokeWidth(vg, 1.f);
					nvgStroke(vg);
				}
			}

			// Five synchronized traces show the whole data path: live input,
			// captured/aligned audio, Bend+Break, Corrupt, and the exact post-Mix
			// output sent to the jacks.
			auto drawTrace = [&](float BsScopeFrame::*field, NVGcolor color, float width) {
				nvgBeginPath(vg);
				for (int i = 0; i < count; i++) {
					float x = pad + bodyW * i / (count - 1);
					float y = waveY(frames[i].*field);
					if (i == 0) nvgMoveTo(vg, x, y); else nvgLineTo(vg, x, y);
				}
				nvgStrokeColor(vg, color);
				nvgStrokeWidth(vg, width);
				nvgStroke(vg);
			};
			drawTrace(&BsScopeFrame::input, nvgRGBA(0x8c, 0x93, 0xa1, 0x4c), 0.6f);
			drawTrace(&BsScopeFrame::aligned, nvgRGBA(0x72, 0x88, 0x90, 0x70), 0.75f);
			drawTrace(&BsScopeFrame::bentBroken,
				brk > bend ? nvgRGBA(0xff, 0xa8, 0x15, 0x94) : nvgRGBA(0x26, 0xd9, 0xff, 0x94), 0.9f);
			if (corrupt > 0.001f)
				drawTrace(&BsScopeFrame::corrupted, nvgRGBA(0xff, 0x38, 0x0a, 0xa8), 0.95f);
			drawTrace(&BsScopeFrame::output, nvgRGBA(0x9b, 0xfb, 0xff, 0xe8), 1.15f);

			// The packet is the low byte of the newest real output sample. Its bits
			// travel at the exact read-window phase and reverse with playback.
			float packetTravel = reverse ? 1.f - subPhase : subPhase;
			float packetX = -mm2px(8.f) + packetTravel * (W + mm2px(16.f));
			const BsScopeFrame& newest = frames[count - 1];
			int packetByte = ((int)std::lround(clampf(newest.output, -1.f, 1.f) * 127.f)) & 0xff;
			float packetY = waveY(newest.output);
			float newestCorrupt = std::fabs(newest.corrupted - newest.bentBroken);
			for (int bit = 0; bit < 8; bit++) {
				bool high = (packetByte & (1 << bit)) != 0;
				float cellX = packetX + bit * mm2px(1.7f);
				nvgBeginPath(vg);
				nvgRoundedRect(vg, cellX, packetY, mm2px(1.15f), mm2px(1.15f), 0.7f);
				nvgFillColor(vg, newestCorrupt > 0.006f && bit == 5
					? nvgRGBA(0xff, 0x54, 0x44, high ? 0xf4 : 0x54)
					: nvgRGBA(0x9b, 0xfb, 0xff, high ? 0xf0 : 0x38));
				nvgFill(vg);
			}
		}

		// Exact division and read-window heads. They share the DSP phases, so
		// their resets cannot drift away from the audible repeat boundaries.
		float scanY = bodyY + clockPhase * bodyH;
		nvgBeginPath(vg);
		nvgRect(vg, pad, scanY - mm2px(1.f), bodyW, mm2px(2.f));
		nvgFillColor(vg, nvgRGBA(0x35, 0xd3, 0xe0, 0x42));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgMoveTo(vg, pad, scanY);
		nvgLineTo(vg, W - pad, scanY);
		nvgStrokeColor(vg, traverse > 0.f ? nvgRGBA(0xff, 0xc2, 0x3e, 0xff)
			: nvgRGBA(0x73, 0xf4, 0xff, 0xd8));
		nvgStrokeWidth(vg, 1.4f);
		nvgStroke(vg);
		float scanX = pad + (reverse ? 1.f - subPhase : subPhase) * bodyW;
		nvgBeginPath(vg);
		nvgMoveTo(vg, scanX, bodyY);
		nvgLineTo(vg, scanX, bodyBottom);
		nvgStrokeColor(vg, nvgRGBA(0xff, 0x54, 0x44, 0xc8));
		nvgStrokeWidth(vg, 1.25f);
		nvgStroke(vg);

		nvgRestore(vg);
	}
};

// ---------------------------------------------------------------- widget ----
struct BadSectorWidget : ModuleWidget {
	// mm positions — keep in sync with gen_panel.py
	static constexpr float KX_L = 15.f, KX_R = 66.28f;
	static constexpr float KY1 = 25.f, KY2 = 46.5f, KY3 = 68.f;

	// context-menu slider bound to a module float
	struct FloatQ : Quantity {
		float* ptr; std::string name; float defVal;
		FloatQ(float* p, std::string n, float d) : ptr(p), name(n), defVal(d) {}
		void setValue(float v) override { *ptr = math::clamp(v, 0.f, 1.f); }
		float getValue() override { return *ptr; }
		float getDefaultValue() override { return defVal; }
		std::string getLabel() override { return name; }
		int getDisplayPrecision() override { return 2; }
	};

	BadSectorWidget(BadSector* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/BadSector.svg")));

		addChild(createWidget<BsScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<BsScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<BsScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<BsScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		BsSignalDataArt* art = new BsSignalDataArt();
		art->box.pos = mm2px(Vec(BsSignalDataArt::X0, BsSignalDataArt::Y0));
		art->box.size = mm2px(Vec(BsSignalDataArt::X1 - BsSignalDataArt::X0,
			BsSignalDataArt::Y1 - BsSignalDataArt::Y0));
		art->module = module;
		addChild(art);

		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(KX_L, KY1)), module, BadSector::BUFFER_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(KX_R, KY1)), module, BadSector::REPEAT_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(KX_L, KY2)), module, BadSector::MIX_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(KX_R, KY2)), module, BadSector::MICRO_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(KX_L, KY3)), module, BadSector::DAMAGE_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(KX_R, KY3)), module, BadSector::CVAMT_PARAM));

		// selectors with square lights + stored-value dots
		addParam(createParamCentered<BsSqButton>(mm2px(Vec(33.6f, 68.f)), module, BadSector::DMGSEL_PARAM));
		addChild(createLightCentered<BsSqLight>(mm2px(Vec(33.6f, 68.f)), module, BadSector::DMGSEL_LIGHT));
		addParam(createParamCentered<BsSqButton>(mm2px(Vec(47.7f, 68.f)), module, BadSector::CVSEL_PARAM));
		addChild(createLightCentered<BsSqLight>(mm2px(Vec(47.7f, 68.f)), module, BadSector::CVSEL_LIGHT));
		addChild(createLightCentered<TinyLight<CyanLight>>(mm2px(Vec(33.6f, 62.9f)), module, BadSector::DOT_DMG_LIGHT));
		addChild(createLightCentered<TinyLight<CyanLight>>(mm2px(Vec(47.7f, 62.9f)), module, BadSector::DOT_CV_LIGHT));

		// mode / clock / freeze
		addParam(createParamCentered<BsSqButtonSmall>(mm2px(Vec(34.f, 54.4f)), module, BadSector::MODE_PARAM));
		addChild(createLightCentered<BsSqLightSmall>(mm2px(Vec(34.f, 54.4f)), module, BadSector::MODE_LIGHT));
		addParam(createParamCentered<BsSqButtonSmall>(mm2px(Vec(40.64f, 54.4f)), module, BadSector::CLOCKBTN_PARAM));
		addChild(createLightCentered<BsSqLightSmall>(mm2px(Vec(40.64f, 54.4f)), module, BadSector::CLK_LIGHT));
		addParam(createParamCentered<BsSqButtonSmall>(mm2px(Vec(47.3f, 54.4f)), module, BadSector::FREEZE_PARAM));
		addChild(createLightCentered<BsSqLightSmall>(mm2px(Vec(47.3f, 54.4f)), module, BadSector::FRZ_LIGHT));

		// jacks — CV row, gate row, audio row
		static const float JX[6] = {10.2f, 22.86f, 35.52f, 48.18f, 60.84f, 73.5f};
		static const float CVY = 89.f, GATEY = 101.f, AUY = 116.5f;
		static const int cvIds[6] = {
			BadSector::BUFFER_CV_INPUT, BadSector::REPEAT_CV_INPUT, BadSector::MIX_CV_INPUT,
			BadSector::BEND_CV_INPUT, BadSector::BREAK_CV_INPUT, BadSector::CORRUPT_CV_INPUT
		};
		for (int i = 0; i < 6; i++)
			addInput(createInputCentered<BsPort>(mm2px(Vec(JX[i], CVY)), module, cvIds[i]));
		// gate jacks sit under their matching CV columns; FRZ takes the MIX column
		static const int gateIds[4] = {
			BadSector::FREEZE_GATE_INPUT, BadSector::BEND_GATE_INPUT,
			BadSector::BREAK_GATE_INPUT, BadSector::CORRUPT_GATE_INPUT
		};
		for (int i = 0; i < 4; i++)
			addInput(createInputCentered<BsPort>(mm2px(Vec(JX[i + 2], GATEY)), module, gateIds[i]));
		// bottom row on the same grid: audio pairs outside, clock/reset centred
		addInput(createInputCentered<BsPort>(mm2px(Vec(JX[0], AUY)), module, BadSector::IN_L_INPUT));
		addInput(createInputCentered<BsPort>(mm2px(Vec(JX[1], AUY)), module, BadSector::IN_R_INPUT));
		addInput(createInputCentered<BsPort>(mm2px(Vec(JX[2], AUY)), module, BadSector::CLOCK_INPUT));
		addInput(createInputCentered<BsPort>(mm2px(Vec(JX[3], AUY)), module, BadSector::RESET_INPUT));
		addOutput(createOutputCentered<BsPort>(mm2px(Vec(JX[4], AUY)), module, BadSector::OUT_L_OUTPUT));
		addOutput(createOutputCentered<BsPort>(mm2px(Vec(JX[5], AUY)), module, BadSector::OUT_R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		BadSector* m = getModule<BadSector>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Corrupt effect",
			{"Decimate", "Dropout", "Destroy", "DJ Filter", "Vinyl Sim"}, &m->corruptSel));
		menu->addChild(createBoolPtrMenuItem("Limit Corrupt to original 3 effects", "", &m->originalCorruptOnly));
		menu->addChild(createBoolPtrMenuItem("Macro: Bend enabled", "", &m->bendOn));
		menu->addChild(createBoolPtrMenuItem("Macro: Break enabled", "", &m->breakOn));
		menu->addChild(createBoolPtrMenuItem("Micro: reverse playback", "", &m->microRev));
		menu->addChild(createBoolPtrMenuItem("Micro: Break knob = silence (off = traverse)", "", &m->microSilence));
		menu->addChild(createBoolPtrMenuItem("MICRO knob active in Macro (global varispeed)", "", &m->microInMacro));
		menu->addChild(createBoolPtrMenuItem("Stereo: unique per channel", "", &m->stereoUnique));
		menu->addChild(createBoolPtrMenuItem("Gates: momentary (hold) instead of latching", "", &m->gatesMomentary));
		menu->addChild(createBoolPtrMenuItem("Freeze button: momentary", "", &m->freezeMomentary));
		auto addSlider = [&](float* ptr, const char* name, float def) {
			ui::Slider* s = new ui::Slider;
			s->quantity = new FloatQ(ptr, name, def);
			s->box.size.x = 200.f;
			menu->addChild(s);
		};
		addSlider(&m->windowing, "Glitch windowing", 0.02f);
		addSlider(&m->stereoWidth, "Stereo width", 1.f);
		addSlider(&m->ledBrightness, "LED brightness", 1.f);
		menu->addChild(createMenuItem("Clear buffer", "", [m]() {
			std::fill(m->bufL.begin(), m->bufL.end(), 0.f);
			std::fill(m->bufR.begin(), m->bufR.end(), 0.f);
			m->writeHead = 0;
			m->readPos[0] = m->readPos[1] = 0.f;
			m->sectionStart = 0;
			m->samplesSinceTick = 0;
			m->recordedSamples = 0;
			m->bufferPrimed = false;
			m->bufferPrimedFade = 0.f;
			m->uiScope.clear();
			m->uiScopeCounter = 0;
		}));
		menu->addChild(createMenuItem("Restore default settings", "", [m]() { m->restoreDefaults(); }));
	}
};

Model* modelBadSector = createModel<BadSector, BadSectorWidget>("BadSector");
