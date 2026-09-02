// MOD1 Random CV — a VCV adaptation of the open-source HAGIWO MOD1 platform.
// It provides 3 random CV outputs, each with an amount (POT) and an offset
// (context menu), clocked internally or from F1.
//
// The jack directions are FIXED BY THE HARDWARE, not by firmware (MOD1_Circuit.jpg in
// modulove/MOD1):
//   F1 -> A3/D17 only, with no reconstruction capacitor. A3 has no PWM on an ATmega328P,
//         so F1 is INPUT-ONLY.
//   F2 -> A4 + D9   \  each has a 1uF cap forming an RC reconstruction filter with its 1k
//   F3 -> A5 + D10   > series resistor, so these can be driven as PWM outputs — or read as
//   F4 -> D11       /  analog inputs. Three outputs is the hardware maximum.
// This implementation uses the hardware-compatible topology F1 = clock input and
// F2/F3/F4 = CV outputs.
//
// Panel: POT1/2/3, a Smooth/S&H switch, one button, one LED, F1..F4 jacks.
#include "plugin.hpp"
#include <cmath>

struct MOD1 : Module {
	enum ParamId { AMT1_PARAM, AMT2_PARAM, AMT3_PARAM, SH_PARAM, BUTTON_PARAM, PARAMS_LEN };
	enum InputId { CLOCK_INPUT, INPUTS_LEN };                       // F1 (input-only)
	enum OutputId { CV1_OUTPUT, CV2_OUTPUT, CV3_OUTPUT, OUTPUTS_LEN }; // F2, F3, F4
	enum LightId { LED_LIGHT, LIGHTS_LEN };

	static const int NCV = 3;

	// persisted settings
	float offset[NCV] = {0.f, 0.f, 0.f};   // volts, per-CV centre shift
	float rateHz = 0.4f;                    // internal clock rate

	// runtime
	float phase = 0.f;
	float from[NCV] = {0.f, 0.f, 0.f};
	float to[NCV]   = {0.f, 0.f, 0.f};
	float cur[NCV]  = {0.f, 0.f, 0.f};
	uint32_t rng = 0x1BADF00Du;
	dsp::PulseGenerator trigPulse;
	dsp::BooleanTrigger btnTrig;
	dsp::SchmittTrigger clockTrig;
	float ledLevel = 0.f;
	float clkPhase = 0.f, clkPeriod = 2.5f, sinceClk = 0.f;
	bool haveClk = false;

	float rndBip() { // -1..1
		rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
		return ((rng & 0xFFFFFF) / float(0x1000000)) * 2.f - 1.f;
	}
	void newTargets() { for (int i = 0; i < NCV; i++) { from[i] = to[i]; to[i] = rndBip(); } }

	MOD1() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(AMT1_PARAM, 0.f, 1.f, 0.3f, "POT1 CV1 amount", "%", 0.f, 100.f);
		configParam(AMT2_PARAM, 0.f, 1.f, 0.3f, "POT2 CV2 amount", "%", 0.f, 100.f);
		configParam(AMT3_PARAM, 0.f, 1.f, 0.3f, "POT3 CV3 amount", "%", 0.f, 100.f);
		configSwitch(SH_PARAM, 0.f, 1.f, 0.f, "Motion", {"Smooth", "S&H (stepped)"});
		configButton(BUTTON_PARAM, "Button (tap: re-randomize now)");
		configInput(CLOCK_INPUT, "F1 Clock (internal clock when unpatched)");
		configOutput(CV1_OUTPUT, "F2 Random CV 1");
		configOutput(CV2_OUTPUT, "F3 Random CV 2");
		configOutput(CV3_OUTPUT, "F4 Random CV 3");
		for (int i = 0; i < NCV; i++) { to[i] = rndBip(); from[i] = to[i]; cur[i] = to[i]; }
	}

	void onReset() override {
		for (int i = 0; i < NCV; i++) offset[i] = 0.f;
		rateHz = 0.4f; phase = 0.f;
		for (int i = 0; i < NCV; i++) { to[i] = rndBip(); from[i] = to[i]; cur[i] = to[i]; }
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* offs = json_array();
		for (int i = 0; i < NCV; i++) json_array_append_new(offs, json_real(offset[i]));
		json_object_set_new(root, "offset", offs);
		json_object_set_new(root, "rateHz", json_real(rateHz));
		return root;
	}
	void dataFromJson(json_t* root) override {
		if (json_t* offs = json_object_get(root, "offset"))
			for (int i = 0; i < NCV && i < (int) json_array_size(offs); i++)
				offset[i] = (float) json_real_value(json_array_get(offs, i));
		if (json_t* r = json_object_get(root, "rateHz")) rateHz = (float) json_real_value(r);
	}

	void process(const ProcessArgs& args) override {
		float dt = args.sampleTime;
		bool stepped = params[SH_PARAM].getValue() > 0.5f;

		// button: re-randomize immediately
		if (btnTrig.process(params[BUTTON_PARAM].getValue() > 0.5f)) { newTargets(); trigPulse.trigger(1e-3f); phase = 0.f; }

		// F1 clocks the updates when patched; the internal clock takes over when it is not.
		// `phase` is the 0..1 ramp between the last target and the next, which the smooth
		// mode interpolates along — so it has to track the external period too.
		if (inputs[CLOCK_INPUT].isConnected()) {
			sinceClk += dt;
			if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
				if (haveClk) clkPeriod = clamp(sinceClk, 0.002f, 60.f);
				haveClk = true; sinceClk = 0.f;
				newTargets(); trigPulse.trigger(1e-3f); phase = 0.f;
			}
			phase = clamp(sinceClk / clkPeriod, 0.f, 1.f);
		} else {
			haveClk = false;
			phase += rateHz * dt;
			if (phase >= 1.f) { phase -= std::floor(phase); newTargets(); trigPulse.trigger(1e-3f); }
		}

		for (int i = 0; i < NCV; i++) {
			if (stepped) {
				cur[i] = to[i];
			} else {
				float s = phase * phase * (3.f - 2.f * phase);  // smoothstep
				cur[i] = from[i] + (to[i] - from[i]) * s;
			}
			float amt = params[AMT1_PARAM + i].getValue();
			float v = clamp(offset[i] + amt * cur[i] * 5.f, -10.f, 10.f);
			outputs[CV1_OUTPUT + i].setVoltage(v);
		}

		bool trig = trigPulse.process(dt);
		float target = trig ? 1.f : clamp(std::fabs(cur[0]), 0.f, 1.f);
		ledLevel += (target - ledLevel) * clamp(20.f * dt, 0.f, 1.f);
		lights[LED_LIGHT].setBrightness(ledLevel);
	}
};

struct MOD1Widget : ModuleWidget {
	MOD1Widget(MOD1* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/MOD1.svg")));

		addChild(createWidget<ScrewSilver>(Vec(0, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		const float XC = 10.16f;
		// Smooth / S&H switch (top)
		addParam(createParamCentered<CKSS>(mm2px(Vec(XC, 19.f)), module, MOD1::SH_PARAM));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(XC, 31.f)), module, MOD1::AMT1_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(XC, 49.f)), module, MOD1::AMT2_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(XC, 67.f)), module, MOD1::AMT3_PARAM));

		addParam(createParamCentered<VCVButton>(mm2px(Vec(XC, 82.f)), module, MOD1::BUTTON_PARAM));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(XC, 90.5f)), module, MOD1::LED_LIGHT));

		// F1 top-left is the input, with F2/F3/F4 as the three outputs.
		const float XLj = 6.2f, XRj = 14.1f;
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(XLj, 103.f)), module, MOD1::CLOCK_INPUT));    // F1
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(XRj, 103.f)), module, MOD1::CV1_OUTPUT));   // F2
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(XLj, 116.f)), module, MOD1::CV2_OUTPUT));   // F3
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(XRj, 116.f)), module, MOD1::CV3_OUTPUT));   // F4
	}

	void appendContextMenu(Menu* menu) override {
		MOD1* m = dynamic_cast<MOD1*>(module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Random voltage generator"));

		menu->addChild(createSubmenuItem("Clock rate", "", [m](Menu* sub) {
			static const float rates[] = {0.1f, 0.2f, 0.4f, 0.8f, 1.5f, 3.f};
			for (float r : rates)
				sub->addChild(createCheckMenuItem(string::f("%g Hz", r), "",
					[m, r]() { return std::abs(m->rateHz - r) < 1e-3f; },
					[m, r]() { m->rateHz = r; }));
		}));

		for (int i = 0; i < MOD1::NCV; i++) {
			menu->addChild(createSubmenuItem(string::f("CV%d offset", i + 1), "", [m, i](Menu* sub) {
				static const float offs[] = {-5.f, -3.f, -1.f, 0.f, 1.f, 3.f, 5.f};
				for (float o : offs)
					sub->addChild(createCheckMenuItem(string::f("%+g V", o), "",
						[m, i, o]() { return std::abs(m->offset[i] - o) < 1e-3f; },
						[m, i, o]() { m->offset[i] = o; }));
			}));
		}
	}
};

Model* modelMOD1 = createModel<MOD1, MOD1Widget>("MOD1-RandomCV");
