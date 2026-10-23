#pragma once
#include "../../plugin.hpp"
#include <atomic>
#include "../core/FV1.hpp"
#include "../presets/OriginalBank.hpp"
#include "../presets/CompleteBank.hpp"
#include "../presets/CatalogueExtras.hpp"
#include "../core/Firmware.hpp"
#include <osdialog.h>
#include "Composition.hpp"
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// FX Aid Pro — recreation of the Happy Nerding FX Aid Pro (14HP multi-effect).
// The same real FV-1 virtual machine as the standard FX Aid, but with the Pro's
// 200-program bank, OLED-style display, input gain, sample-rate reduction (a
// variable FV-1 clock), three internal LFOs and 10 user presets. Firmware WAVs
// exported from fxaid.app's Pro editor carry the program NAMES and control
// labels inside each 640-byte record, so no fingerprint catalog is needed.
// ─────────────────────────────────────────────────────────────────────────────

static const int PRO_N = 246; // Eight original slots, complete 200-program bank, 38 catalogue additions.
static const float PRO_CLOCK = 32768.f;        // nominal FV-1 clock (Hz)

// One record inside the Pro firmware image: 128-byte header + 512-byte program.
static const int PRO_REC = 640;
static const int PRO_HDR = 128;

struct ProLFO {
	// shapes: 0 Off, 1 Triangle, 2 Ramp, 3 Saw, 4 S&H, 5 Smooth random, 6 Sine, 7 Square
	int shape = 0;
	float rate = 0.3f;    // 0..1 knob
	float depth = 0.5f;   // 0..1
	int target = 0;       // 0 Off, 1 Pot1, 2 Pot2, 3 Pot3, 4 SRR
	float phase = 0.f;
	float sh = 0.f;       // sample&hold / smooth-random current value
	float shTarget = 0.f;
	uint32_t rng = 0x1234567u;

	float frand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / float(0x1000000); }

	void restart() { phase = 0.f; }

	// advance by dt seconds, return bipolar -1..1
	float process(float dt) {
		if (shape == 0) return 0.f;
		float hz = 0.02f + rate * rate * 20.f;
		float pinc = hz * dt;
		phase += pinc;
		bool wrapped = false;
		while (phase >= 1.f) { phase -= 1.f; wrapped = true; }
		switch (shape) {
			case 1: return 1.f - 4.f * std::fabs(phase - 0.5f);          // triangle
			case 2: return 2.f * phase - 1.f;                            // ramp up
			case 3: return 1.f - 2.f * phase;                           // saw down
			case 4:                                                      // S&H random steps
				if (wrapped) sh = frand() * 2.f - 1.f;
				return sh;
			case 5:                                                      // smooth random
				if (wrapped) shTarget = frand() * 2.f - 1.f;
				sh += (shTarget - sh) * clamp(pinc * 6.f, 0.f, 1.f);
				return sh;
			case 6: return std::sin(2.f * M_PI * phase);                 // sine
			case 7: return phase < 0.5f ? 1.f : -1.f;                    // square
		}
		return 0.f;
	}
};

struct SonavyreEngine : Module {
	enum ParamId {
		KNOB1_PARAM, KNOB2_PARAM, KNOB3_PARAM,
		DW_PARAM, INGAIN_PARAM, SRR_PARAM,
		PROG_PARAM, PREV_PARAM, NEXT_PARAM, LFO_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CV1_INPUT, CV2_INPUT, CV3_INPUT,
		DW_CV_INPUT, SRR_CV_INPUT,
		L_INPUT, R_INPUT,
		INPUTS_LEN
	};
	enum OutputId { L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { CLIP_LIGHT, LFO_LIGHT, LIGHTS_LEN };

	std::atomic<float> meterL{0.f}, meterR{0.f}, outputPower{0.f};
	float analysisPowerSum=0.f;
	unsigned analysisCount=0;
	std::atomic<float> scope[128];
	unsigned scopeWrite = 0, scopeDivider = 0;
	std::atomic<unsigned> scopeCursor{0};
	bool animate = true;
	SonavyreComposition chain;
	FV1 vm;
	uint32_t bank[PRO_N][128];
	std::string slotNames[PRO_N];
	std::string slotLabels[PRO_N][3];
	int nSlots = PRO_N;
	int prog = 0;
	bool customBank = false;
	int srrRoute = 0;   // 0 Sample rate, 1 Pot1, 2 Pot2, 3 Pot3, 4 Program, 5 LFO restart

	ProLFO lfo[3];

	// 10 user presets
	struct Preset {
		bool used = false;
		float pots[3] = {0.5f, 0.5f, 0.5f};
		float dw = 0.5f, srr = 1.f, ingain = 0.5f;
		int prog = 0;
		ProLFO lfo[3];
	} presets[10];

	// variable-rate FV-1 clock
	float vmPhase = 0.f;
	float prevInL = 0.f, prevInR = 0.f;
	float vmOutL = 0.f, vmOutR = 0.f, vmOutPL = 0.f, vmOutPR = 0.f;
	float clipEnv = 0.f;

	// firmware update listen mode
	bool updateMode = false;
	std::vector<float> updateBuf;
	double updatePhase = 0.0;
	int updateSilence = 0;
	bool updateHeard = false;
	int updateFlash = 0;
	std::string firmwareStatus;

	// display state
	float dispPots[3] = {0.5f, 0.5f, 0.5f};
	float dispSrr = 1.f;

	dsp::SchmittTrigger prevTrig, nextTrig, lfoTrig, srrGate;
	dsp::BooleanTrigger progKnobTrig;
	float bothHeld = 0.f;
	int lastProgKnob = -1;

	SonavyreEngine() {
		for (auto& v : scope) v.store(0.f);
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(KNOB1_PARAM, 0.f, 1.f, 0.5f, "Parameter 1");
		configParam(KNOB2_PARAM, 0.f, 1.f, 0.5f, "Parameter 2");
		configParam(KNOB3_PARAM, 0.f, 1.f, 0.5f, "Parameter 3");
		configParam(DW_PARAM, 0.f, 1.f, 0.5f, "Dry/wet", "%", 0.f, 100.f);
		configParam(INGAIN_PARAM, 0.f, 1.f, 0.5f, "Input gain");
		configParam(SRR_PARAM, 0.f, 1.f, 1.f, "Sample rate (down = reduction / more time)");
		configParam(PROG_PARAM, 0.f, PRO_N - 1, 0.f, "Program");
		paramQuantities[PROG_PARAM]->snapEnabled = true;
		configButton(PREV_PARAM, "Previous program");
		configButton(NEXT_PARAM, "Next program");
		configButton(LFO_PARAM, "Restart LFOs");
		configInput(CV1_INPUT, "Parameter 1 CV");
		configInput(CV2_INPUT, "Parameter 2 CV");
		configInput(CV3_INPUT, "Parameter 3 CV");
		configInput(DW_CV_INPUT, "Dry/wet CV");
		configInput(SRR_CV_INPUT, "SRR input (routable)");
		configInput(L_INPUT, "Left audio");
		configInput(R_INPUT, "Right audio");
		configOutput(L_OUTPUT, "Left audio");
		configOutput(R_OUTPUT, "Right audio");
		configBypass(L_INPUT, L_OUTPUT);
		configBypass(R_INPUT, R_OUTPUT);
		loadDefaultBank();
		setProgram(0);
	}

	void loadDefaultBank() {
		for (int p = 0; p < 8; p++) {
			std::memcpy(bank[p], FXAID_DEFAULT_BANK[p], sizeof(bank[p]));
			slotNames[p] = FXAID_DEFAULT_NAMES[p];
			for (int c = 0; c < 3; c++)
				slotLabels[p][c] = FXAID_DEFAULT_LABELS[p][c];
		}
		for (int p = 0; p < FXAIDPRO_BANK_SIZE; p++) {
			std::memcpy(bank[p + 8], FXAIDPRO_DEFAULT_BANK[p], sizeof(bank[p + 8]));
			slotNames[p + 8] = FXAIDPRO_DEFAULT_NAMES[p];
			for (int c = 0; c < 3; c++) slotLabels[p + 8][c] = FXAIDPRO_DEFAULT_LABELS[p][c];
		}
		for (int p=0;p<38;p++) {
			std::memcpy(bank[p+208],SONAVYRE_EXTRA_BANK[p],sizeof(bank[p+208]));
			slotNames[p+208]=SONAVYRE_EXTRA_NAMES[p];
			for(int c=0;c<3;c++)slotLabels[p+208][c]=SONAVYRE_EXTRA_LABELS[p][c];
		}
		chain.invalidate();
		nSlots = PRO_N;
		customBank = false;
	}

	void setProgram(int p) {
		if (nSlots < 1) return;
		prog = clamp(p, 0, nSlots - 1);
		vm.loadProgram(bank[prog]);
		if (paramQuantities[KNOB1_PARAM]) {
			paramQuantities[KNOB1_PARAM]->name = slotNames[prog] + " – " + slotLabels[prog][0];
			paramQuantities[KNOB2_PARAM]->name = slotNames[prog] + " – " + slotLabels[prog][1];
			paramQuantities[KNOB3_PARAM]->name = slotNames[prog] + " – " + slotLabels[prog][2];
		}
	}

	// Parse the 200 named 640-byte records out of a decoded Pro firmware image.
	bool parseProImage(const std::vector<uint8_t>& img, std::string& err) {
		// Locate the record table: each record's program (record+128) must be
		// 128 legal FV-1 words, and the header name must be printable ASCII.
		auto validProgAt = [&](size_t off) {
			if (off + 512 > img.size()) return false;
			for (size_t i = 0; i < 512; i += 4)
				if ((img[off + i + 3] & 0x1F) > 0x14) return false;
			return true;
		};
		auto looksLikeRecord = [&](size_t base) {
			if (base + PRO_REC > img.size()) return false;
			// header byte 0 is a zero lead-in; name starts at +1 with ≥1 ASCII char
			uint8_t c = img[base + 1];
			if (c < 0x20 || c > 0x7e) return false;
			return validProgAt(base + PRO_HDR);
		};
		size_t base = img.size();
		if (looksLikeRecord(0xb018)) base = 0xb018;   // known Pro layout
		if (base == img.size()) {
			for (size_t i = 0; i + PRO_REC <= img.size(); i += 4)
				if (looksLikeRecord(i) && looksLikeRecord(i + PRO_REC)) { base = i; break; }
		}
		if (base == img.size()) { err = "No Pro program table found"; return false; }

		int count = 0;
		for (int r = 0; r < FXAIDPRO_BANK_SIZE; r++) {
			size_t rec = base + (size_t)r * PRO_REC;
			if (rec + PRO_REC > img.size()) break;
			if (!validProgAt(rec + PRO_HDR)) break;
			const uint8_t* h = &img[rec];
			auto cstr = [&](int off, int max) {
				std::string s;
				for (int i = 0; i < max && h[off + i]; i++) {
					uint8_t ch = h[off + i];
					if (ch >= 0x20 && ch <= 0x7e) s += (char)ch;
				}
				return s;
			};
			slotNames[r] = cstr(1, 30);
			if (slotNames[r].empty()) slotNames[r] = string::f("Slot %d", r + 1);
			slotLabels[r][0] = cstr(0x21, 14);
			slotLabels[r][1] = cstr(0x31, 14);
			slotLabels[r][2] = cstr(0x41, 14);
			for (int c = 0; c < 3; c++)
				if (slotLabels[r][c].empty()) slotLabels[r][c] = "-";
			const uint8_t* pw = h + PRO_HDR;
			for (int i = 0; i < 128; i++)
				bank[r][i] = ((uint32_t)pw[i * 4] << 24) | ((uint32_t)pw[i * 4 + 1] << 16)
				           | ((uint32_t)pw[i * 4 + 2] << 8) | pw[i * 4 + 3];
			count++;
		}
		if (count < 1) { err = "No programs decoded"; return false; }
		chain.invalidate();
		nSlots = count;
		return true;
	}

	bool parseStandardImage(const std::vector<uint8_t>& img, std::string& err){
	// Locate the 32 x 512-byte FV-1 bank inside the firmware image.
	// The 4 bytes at image offset 0x6800 are a content-dependent checksum, so
	// the bank cannot be found by a fixed marker. Instead: a bank is 4096
	// big-endian words that all decode to legal FV-1 opcodes (0x00..0x14).
	auto validBankAt = [&](size_t off) {
		if (off + 16384 > img.size()) return false;
		for (size_t i = 0; i < 16384; i += 4)
			if ((img[off + i + 3] & 0x1F) > 0x14)
				return false;
		return true;
	};
	size_t bankOff = img.size();
	// 1) the known layout of the current firmware image
	if (validBankAt(0x6818))
		bankOff = 0x6818;
	// 2) the CV-config anchor (00 ff 00 ff 00 x4) directly precedes the bank
	if (bankOff == img.size()) {
		static const uint8_t ANCHOR[20] = {
			0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00,
			0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00
		};
		for (size_t i = 0; i + 20 + 16384 <= img.size(); i++) {
			if (!std::memcmp(&img[i], ANCHOR, 20) && validBankAt(i + 20)) {
				bankOff = i + 20;
				break;
			}
		}
	}
	// 3) brute scan for any valid bank position
	if (bankOff == img.size()) {
		for (size_t i = 0; i + 16384 <= img.size(); i += 4) {
			if (validBankAt(i)) {
				bankOff = i;
				break;
			}
		}
	}
	if (bankOff == img.size()) {
		err = "Program bank not found in firmware image";
		return false;
	}
	const uint8_t* b = &img[bankOff];
	for (int p = 0; p < 32; p++)
		for (int i = 0; i < 128; i++) {
			const uint8_t* w = b + p * 512 + i * 4;
			bank[p][i] = ((uint32_t)w[0] << 24) | ((uint32_t)w[1] << 16) | ((uint32_t)w[2] << 8) | w[3];
		}
	chain.invalidate();
	nSlots=32;
	for(int p=0;p<32;p++){slotNames[p]=string::f("Imported program %d",p+1);for(int c=0;c<3;c++)slotLabels[p][c]=string::f("Parameter %d",c+1);}
	err.clear();return true;
	}

	void applyFirmware(const std::vector<float>& audio48k) {
		std::vector<uint8_t> img;
		std::string err;
		if (fxaidDecodeImage(audio48k, img, err) && (parseProImage(img, err) || parseStandardImage(img, err))) {
			customBank = true;
			setProgram(clamp(prog, 0, nSlots - 1));
			firmwareStatus = string::f("Loaded %d programs", nSlots) + (err.empty() ? "" : " (" + err + ")");
			updateFlash = 96000;
		}
		else {
			firmwareStatus = "Update failed: " + err;
			updateFlash = -96000;
		}
	}

	void loadFirmwareFile(const std::string& path) {
		std::vector<float> audio;
		if (!fxaidReadWav48k(path, audio)) { firmwareStatus = "Could not read WAV"; return; }
		applyFirmware(audio);
	}

	void savePreset(int i) {
		Preset& pr = presets[i];
		pr.used = true;
		for (int c = 0; c < 3; c++) pr.pots[c] = params[KNOB1_PARAM + c].getValue();
		pr.dw = params[DW_PARAM].getValue();
		pr.srr = params[SRR_PARAM].getValue();
		pr.ingain = params[INGAIN_PARAM].getValue();
		pr.prog = prog;
		for (int l = 0; l < 3; l++) pr.lfo[l] = lfo[l];
	}
	void recallPreset(int i) {
		Preset& pr = presets[i];
		if (!pr.used) return;
		for (int c = 0; c < 3; c++) params[KNOB1_PARAM + c].setValue(pr.pots[c]);
		params[DW_PARAM].setValue(pr.dw);
		params[SRR_PARAM].setValue(pr.srr);
		params[INGAIN_PARAM].setValue(pr.ingain);
		for (int l = 0; l < 3; l++) { int ph = 0; lfo[l] = pr.lfo[l]; (void)ph; }
		setProgram(pr.prog);
		params[PROG_PARAM].setValue(pr.prog);
		lastProgKnob = pr.prog;
	}

	void onReset() override {
		loadDefaultBank();
		setProgram(0);
		srrRoute = 0;
		updateMode = false;
		for (int l = 0; l < 3; l++) lfo[l] = ProLFO();
		// User presets deliberately survive module reset.
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "chain", chain.toJson());
		json_object_set_new(r, "animate", json_boolean(animate));
		json_object_set_new(r, "prog", json_integer(prog));
		json_object_set_new(r, "srrRoute", json_integer(srrRoute));
		json_object_set_new(r, "customBank", json_boolean(customBank));
		json_t* lfos = json_array();
		for (int l = 0; l < 3; l++) {
			json_t* lj = json_object();
			json_object_set_new(lj, "shape", json_integer(lfo[l].shape));
			json_object_set_new(lj, "rate", json_real(lfo[l].rate));
			json_object_set_new(lj, "depth", json_real(lfo[l].depth));
			json_object_set_new(lj, "target", json_integer(lfo[l].target));
			json_array_append_new(lfos, lj);
		}
		json_object_set_new(r, "lfos", lfos);
		json_t* prs = json_array();
		for (int i = 0; i < 10; i++) {
			json_t* pj = json_object();
			json_object_set_new(pj, "used", json_boolean(presets[i].used));
			if (presets[i].used) {
				json_t* pots = json_array();
				for (int c = 0; c < 3; c++) json_array_append_new(pots, json_real(presets[i].pots[c]));
				json_object_set_new(pj, "pots", pots);
				json_object_set_new(pj, "dw", json_real(presets[i].dw));
				json_object_set_new(pj, "srr", json_real(presets[i].srr));
				json_object_set_new(pj, "ingain", json_real(presets[i].ingain));
				json_object_set_new(pj, "prog", json_integer(presets[i].prog));
				json_t* pl = json_array();
				for (int l = 0; l < 3; l++) {
					json_t* lj = json_object();
					json_object_set_new(lj, "shape", json_integer(presets[i].lfo[l].shape));
					json_object_set_new(lj, "rate", json_real(presets[i].lfo[l].rate));
					json_object_set_new(lj, "depth", json_real(presets[i].lfo[l].depth));
					json_object_set_new(lj, "target", json_integer(presets[i].lfo[l].target));
					json_array_append_new(pl, lj);
				}
				json_object_set_new(pj, "lfo", pl);
			}
			json_array_append_new(prs, pj);
		}
		json_object_set_new(r, "presets", prs);
		if (customBank) {
			std::string hex;
			hex.reserve(nSlots * 128 * 8);
			char tmp[9];
			for (int p = 0; p < nSlots; p++)
				for (int i = 0; i < 128; i++) {
					std::snprintf(tmp, sizeof(tmp), "%08x", bank[p][i]);
					hex += tmp;
				}
			json_object_set_new(r, "nSlots", json_integer(nSlots));
			json_object_set_new(r, "bank", json_string(hex.c_str()));
			json_t* nm = json_array();
			json_t* lb = json_array();
			for (int p = 0; p < nSlots; p++) {
				json_array_append_new(nm, json_string(slotNames[p].c_str()));
				json_t* three = json_array();
				for (int c = 0; c < 3; c++) json_array_append_new(three, json_string(slotLabels[p][c].c_str()));
				json_array_append_new(lb, three);
			}
			json_object_set_new(r, "names", nm);
			json_object_set_new(r, "labels", lb);
		}
		return r;
	}

	void dataFromJson(json_t* r) override {
		json_t* j;
		chain.fromJson(json_object_get(r, "chain"));
		if ((j = json_object_get(r, "animate"))) animate = json_is_true(j);
		if ((j = json_object_get(r, "srrRoute"))) srrRoute = clamp(int(json_integer_value(j)),0,5);
		if ((j = json_object_get(r, "lfos"))) {
			for (int l = 0; l < 3 && l < (int)json_array_size(j); l++) {
				json_t* lj = json_array_get(j, l);
				if (json_t* x = json_object_get(lj, "shape")) lfo[l].shape = clamp(int(json_integer_value(x)),0,7);
				if (json_t* x = json_object_get(lj, "rate")) lfo[l].rate = clamp(float(json_number_value(x)),0.f,1.f);
				if (json_t* x = json_object_get(lj, "depth")) lfo[l].depth = clamp(float(json_number_value(x)),0.f,1.f);
				if (json_t* x = json_object_get(lj, "target")) lfo[l].target = clamp(int(json_integer_value(x)),0,4);
			}
		}
		if ((j = json_object_get(r, "presets"))) {
			for (int i = 0; i < 10 && i < (int)json_array_size(j); i++) {
				json_t* pj = json_array_get(j, i);
				json_t* u = json_object_get(pj, "used");
				presets[i].used = u && json_boolean_value(u);
				if (!presets[i].used) continue;
				if (json_t* pots = json_object_get(pj, "pots"))
					for (int c = 0; c < 3 && c < (int)json_array_size(pots); c++)
						presets[i].pots[c] = json_number_value(json_array_get(pots, c));
				if (json_t* x = json_object_get(pj, "dw")) presets[i].dw = json_number_value(x);
				if (json_t* x = json_object_get(pj, "srr")) presets[i].srr = json_number_value(x);
				if (json_t* x = json_object_get(pj, "ingain")) presets[i].ingain = json_number_value(x);
				if (json_t* x = json_object_get(pj, "prog")) presets[i].prog = json_integer_value(x);
				if (json_t* pl = json_object_get(pj, "lfo"))
					for (int l = 0; l < 3 && l < (int)json_array_size(pl); l++) {
						json_t* lj = json_array_get(pl, l);
						if (json_t* x = json_object_get(lj, "shape")) presets[i].lfo[l].shape = clamp(int(json_integer_value(x)),0,7);
						if (json_t* x = json_object_get(lj, "rate")) presets[i].lfo[l].rate = clamp(float(json_number_value(x)),0.f,1.f);
						if (json_t* x = json_object_get(lj, "depth")) presets[i].lfo[l].depth = clamp(float(json_number_value(x)),0.f,1.f);
						if (json_t* x = json_object_get(lj, "target")) presets[i].lfo[l].target = clamp(int(json_integer_value(x)),0,4);
					}
			}
		}
		json_t* cb = json_object_get(r, "customBank");
		if (cb && json_boolean_value(cb)) {
			json_t* nj = json_object_get(r, "nSlots");
			json_t* bj = json_object_get(r, "bank");
			int n = nj ? json_integer_value(nj) : 0;
			if (bj && n > 0 && n <= PRO_N) {
				const char* hex = json_string_value(bj);
				if (hex && (int)std::strlen(hex) == n * 128 * 8) {
					for (int p = 0; p < n; p++)
						for (int i = 0; i < 128; i++) {
							char tmp[9];
							std::memcpy(tmp, hex + (p * 128 + i) * 8, 8);
							tmp[8] = 0;
							bank[p][i] = (uint32_t)std::strtoul(tmp, nullptr, 16);
						}
					nSlots = n;
					customBank = true;
					if (json_t* nm = json_object_get(r, "names"))
						for (int p = 0; p < n && p < (int)json_array_size(nm); p++)
							if(json_is_string(json_array_get(nm,p))) slotNames[p] = json_string_value(json_array_get(nm, p));
					if (json_t* lb = json_object_get(r, "labels"))
						for (int p = 0; p < n && p < (int)json_array_size(lb); p++) {
							json_t* three = json_array_get(lb, p);
							for (int c = 0; c < 3 && c < (int)json_array_size(three); c++)
								if(json_is_string(json_array_get(three,c))) slotLabels[p][c] = json_string_value(json_array_get(three, c));
						}
				}
			}
		}
		if ((j = json_object_get(r, "prog"))) {
			setProgram(json_integer_value(j));
			params[PROG_PARAM].setValue(prog);
			lastProgKnob = prog;
		}
	}

	void process(const ProcessArgs& args) override {
		float dt = args.sampleTime;

		// ── program browse: knob (snaps + loads), prev/next fine step ──
		int progKnob = (int)std::round(params[PROG_PARAM].getValue());
		progKnob = clamp(progKnob, 0, nSlots - 1);
		if (progKnob != lastProgKnob) { setProgram(progKnob); lastProgKnob = progKnob; }

		bool pPrev = params[PREV_PARAM].getValue() > 0.5f;
		bool pNext = params[NEXT_PARAM].getValue() > 0.5f;
		if (pPrev && pNext) {
			bothHeld += dt;
			if (bothHeld > 1.f && !updateMode) {
				updateMode = true;
				updateBuf.clear();
				updateBuf.reserve(48000u * 90u);
				updatePhase = 0.0; updateSilence = 0; updateHeard = false;
				firmwareStatus = "Listening for firmware...";
			}
		}
		else {
			bothHeld = 0.f;
			if (prevTrig.process(pPrev ? 1.f : 0.f) && !pNext) {
				if (updateMode) { updateMode = false; firmwareStatus = ""; }
				else { setProgram((prog + nSlots - 1) % nSlots); params[PROG_PARAM].setValue(prog); lastProgKnob = prog; }
			}
			if (nextTrig.process(pNext ? 1.f : 0.f) && !pPrev) {
				if (updateMode) { updateMode = false; firmwareStatus = ""; }
				else { setProgram((prog + 1) % nSlots); params[PROG_PARAM].setValue(prog); lastProgKnob = prog; }
			}
		}

		// ── input gain (0.5 = unity, 1.0 = +6 dB) ──
		float gain = params[INGAIN_PARAM].getValue() * 2.f;
		float inL = inputs[L_INPUT].getVoltage() / 5.f * gain;
		float inR = inputs[R_INPUT].isConnected() ? inputs[R_INPUT].getVoltage() / 5.f * gain : inL;

		// ── firmware listen mode ──
		if (updateMode) {
			float mono = (inputs[R_INPUT].isConnected() && !inputs[L_INPUT].isConnected()) ? inR : inL;
			updatePhase += 48000.0 * dt;
			while (updatePhase >= 1.0) { updatePhase -= 1.0; updateBuf.push_back(mono); }
			if (std::fabs(mono) > 0.05f) { updateHeard = true; updateSilence = 0; }
			else if (updateHeard) updateSilence++;
			bool full = updateBuf.size() >= 48000u * 90u;
			bool done = updateHeard && updateSilence > (int)(0.5f * args.sampleRate);
			if (full || done) { updateMode = false; applyFirmware(updateBuf); updateBuf.clear(); updateBuf.shrink_to_fit(); }
		}

		// ── SRR routable input ──
		float srrCV = inputs[SRR_CV_INPUT].getVoltage();
		bool srrToSrr = (srrRoute == 0);
		// LFO restart button / SRR-as-restart gate
		bool lfoBtn = params[LFO_PARAM].getValue() > 0.5f;
		if (lfoTrig.process(lfoBtn ? 1.f : 0.f) || (srrRoute == 5 && srrGate.process(srrCV > 1.f)))
			for (int l = 0; l < 3; l++) lfo[l].restart();

		// ── base pot values + per-pot CV ──
		float pots[3];
		for (int c = 0; c < 3; c++)
			pots[c] = clamp(params[KNOB1_PARAM + c].getValue() + inputs[CV1_INPUT + c].getVoltage() / 5.f, 0.f, 1.f);
		// SRR routed to a pot / program adds to that target
		if (srrRoute >= 1 && srrRoute <= 3) pots[srrRoute - 1] = clamp(pots[srrRoute - 1] + srrCV / 5.f, 0.f, 1.f);
		if (srrRoute == 4) { int p = clamp((int)std::round(srrCV / 5.f * (nSlots - 1)), 0, nSlots - 1); if (p != prog) { setProgram(p); } }

		// ── internal LFOs modulate pots / SRR ──
		float srrKnob = params[SRR_PARAM].getValue();
		float lfoLight = 0.f;
		float srrMod = 0.f;
		for (int l = 0; l < 3; l++) {
			float v = lfo[l].process(dt);
			lfoLight = std::max(lfoLight, std::fabs(v) * (lfo[l].shape ? 1.f : 0.f));
			if (lfo[l].target >= 1 && lfo[l].target <= 3)
				pots[lfo[l].target - 1] = clamp(pots[lfo[l].target - 1] + v * lfo[l].depth * 0.5f, 0.f, 1.f);
			else if (lfo[l].target == 4)
				srrMod += v * lfo[l].depth * 0.5f;
		}

		// ── sample-rate reduction: variable FV-1 clock ──
		// knob 1.0 = full 32768 Hz; down = octaves of reduction (more "time").
		float reductionOct = (1.f - srrKnob) * 3.5f - srrMod * 3.5f;
		if (srrToSrr) reductionOct += clamp(srrCV / 5.f, -1.f, 1.f) * 3.5f;   // +V = more time
		reductionOct = clamp(reductionOct, -1.f, 3.5f);
		float clockHz = clamp(PRO_CLOCK * std::pow(2.f, -reductionOct), 1000.f, 48000.f);

		for (int c = 0; c < 3; c++) dispPots[c] = pots[c];
		dispSrr = srrKnob;

		// ── D/W ──
		float dw = clamp(params[DW_PARAM].getValue() + inputs[DW_CV_INPUT].getVoltage() / 5.f, 0.f, 1.f);

		// ── run the FV-1 at the (variable) clock with linear interpolation ──
		float ratio = clockHz * dt;
		vmPhase += ratio;
		while (vmPhase >= 1.f) {
			vmPhase -= 1.f;
			float frac = 1.f - vmPhase / ratio;
			float l = prevInL + (inL - prevInL) * clamp(frac, 0.f, 1.f);
			float r = prevInR + (inR - prevInR) * clamp(frac, 0.f, 1.f);
			vmOutPL = vmOutL; vmOutPR = vmOutR;
			vm.run(l, r, pots[0], pots[1], pots[2], vmOutL, vmOutR);
		}
		prevInL = inL; prevInR = inR;
		float f = clamp(vmPhase / ratio, 0.f, 1.f);
		float wetL = vmOutPL + (vmOutL - vmOutPL) * f;
		float wetR = vmOutPR + (vmOutR - vmOutPR) * f;
		if (!std::isfinite(wetL) || !std::isfinite(wetR)) { vm.clearState(); wetL = wetR = 0.f; }

		float outL = (inL * (1.f - dw) + wetL * dw) * 5.f;
		float outR = (inR * (1.f - dw) + wetR * dw) * 5.f;
		chain.process(outL, outR, dt, bank, nSlots, clockHz);
		outputs[L_OUTPUT].setVoltage(outL);
		outputs[R_OUTPUT].setVoltage(outR);
		analysisPowerSum += (outL*outL + outR*outR) * .005f;
		if (++analysisCount == 256) { outputPower.store(analysisPowerSum/256.f, std::memory_order_relaxed); analysisPowerSum=0.f; analysisCount=0; }
		meterL.store(outL, std::memory_order_relaxed);
		meterR.store(outR, std::memory_order_relaxed);
		if (++scopeDivider == 32) { scopeDivider = 0; scope[scopeWrite++ % 128].store((outL + outR) * 0.05f, std::memory_order_relaxed); scopeCursor.store(scopeWrite, std::memory_order_release); }

		// ── lights ──
		float peak = std::max(std::fabs(outL), std::fabs(outR)) / 10.f;
		clipEnv = std::max(clipEnv * 0.999f, peak);
		if ((int)(args.frame % 64) == 0) {
			lights[CLIP_LIGHT].setBrightness(clipEnv > 0.98f ? 1.f : 0.f);
			lights[LFO_LIGHT].setBrightness(lfoLight);
			if (updateFlash > 0) { updateFlash -= 64; lights[LFO_LIGHT].setBrightness((updateFlash / 6000) % 2 ? 1.f : 0.f); }
			else if (updateFlash < 0) { updateFlash += 64; lights[CLIP_LIGHT].setBrightness((-updateFlash / 6000) % 2 ? 1.f : 0.f); }
		}
	}
};

