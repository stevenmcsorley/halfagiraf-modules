#include "plugin.hpp"
#include "PalimpsestEngine.hpp"

#include <array>
#include <atomic>
#include <cmath>

using palimpsest::SpectralMemoryEngine;

struct Palimpsest : Module {
	enum ParamId {
		AGE_PARAM,
		IMPRINT_PARAM,
		EROSION_PARAM,
		GRAVITY_PARAM,
		DRIFT_PARAM,
		BLOOM_PARAM,
		TRACE_PARAM,
		SEAL_PARAM,
		WASH_PARAM,
		IN_L_TRIM_PARAM,
		IN_R_TRIM_PARAM,
		VOCT_TRIM_PARAM,
		CLOCK_TRIM_PARAM,
		EXCITE_TRIM_PARAM,
		RESET_TRIM_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN_L_INPUT,
		IN_R_INPUT,
		VOCT_INPUT,
		CLOCK_INPUT,
		EXCITE_INPUT,
		RESET_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUT_L_OUTPUT,
		OUT_R_OUTPUT,
		GHOST_L_OUTPUT,
		GHOST_R_OUTPUT,
		MOTION_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		SEAL_LIGHT,
		WASH_LIGHT,
		LIGHTS_LEN
	};

	SpectralMemoryEngine engine;
	dsp::BooleanTrigger sealButton;
	dsp::BooleanTrigger washButton;
	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger exciteTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::ClockDivider uiDivider;
	bool sealed = false;
	float washFlash = 0.f;

	std::atomic<float> uiBands[palimpsest::MEMORY_LAYERS][palimpsest::DISPLAY_BANDS];
	std::atomic<float> uiMotion;
	std::atomic<float> uiTrace;
	std::atomic<float> uiAge;
	std::atomic<bool> clearRequested;

	Palimpsest() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(AGE_PARAM, 0.f, 1.f, 0.35f, "Age", "%", 0.f, 100.f);
		configParam(IMPRINT_PARAM, 0.f, 1.f, 0.55f, "Imprint", "%", 0.f, 100.f);
		configParam(EROSION_PARAM, 0.f, 1.f, 0.18f, "Erosion", "%", 0.f, 100.f);
		configParam(GRAVITY_PARAM, 0.f, 1.f, 0.35f, "Spectral gravity", "%", 0.f, 100.f);
		configParam(DRIFT_PARAM, 0.f, 1.f, 0.18f, "Drift", "%", 0.f, 100.f);
		configParam(BLOOM_PARAM, 0.f, 0.995f, 0.4f, "Bloom feedback", "%", 0.f, 100.f);
		configParam(TRACE_PARAM, 0.f, 1.f, 0.55f, "Live / Ghost trace", "%", 0.f, 100.f);
		configButton(SEAL_PARAM, "Seal spectral memory");
		configButton(WASH_PARAM, "Wash spectral memory");
		configParam(IN_L_TRIM_PARAM, -1.f, 1.f, 0.f, "Left input gain", " dB", 0.f, 12.f);
		configParam(IN_R_TRIM_PARAM, -1.f, 1.f, 0.f, "Right input gain", " dB", 0.f, 12.f);
		configParam(VOCT_TRIM_PARAM, -1.f, 1.f, 0.f, "V/Oct depth", "%", 0.f, 100.f);
		configParam(CLOCK_TRIM_PARAM, -1.f, 1.f, 0.f, "Clock step size");
		configParam(EXCITE_TRIM_PARAM, -1.f, 1.f, 0.f, "Excite amount", "%", 0.f, 100.f);
		configParam(RESET_TRIM_PARAM, -1.f, 1.f, 0.f, "Reset amount", "%", 0.f, 100.f);

		configInput(IN_L_INPUT, "Left audio");
		configInput(IN_R_INPUT, "Right audio");
		configInput(VOCT_INPUT, "V/Oct transpose");
		configInput(CLOCK_INPUT, "Age clock");
		configInput(EXCITE_INPUT, "Ghost excite");
		configInput(RESET_INPUT, "Reset traversal");
		configOutput(OUT_L_OUTPUT, "Left mix");
		configOutput(OUT_R_OUTPUT, "Right mix");
		configOutput(GHOST_L_OUTPUT, "Left ghost");
		configOutput(GHOST_R_OUTPUT, "Right ghost");
		configOutput(MOTION_OUTPUT, "Spectral motion CV");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);

		uiDivider.setDivision(64);
		for (int layer = 0; layer < palimpsest::MEMORY_LAYERS; ++layer)
			for (int band = 0; band < palimpsest::DISPLAY_BANDS; ++band)
				uiBands[layer][band].store(0.f);
		uiMotion.store(0.f);
		uiTrace.store(0.55f);
		uiAge.store(0.35f);
		clearRequested.store(false);
	}

	void onSampleRateChange(const SampleRateChangeEvent& event) override {
		engine.setSampleRate(event.sampleRate);
	}

	void onReset() override {
		engine.clearMemory();
		engine.resetRuntime();
		sealed = false;
		washFlash = 0.f;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "formatVersion", json_integer(1));
		json_object_set_new(root, "sealed", json_boolean(sealed));
		json_object_set_new(root, "clockPosition", json_real(engine.clockPosition()));
		json_t* memory = json_array();
		for (int channel = 0; channel < 2; ++channel) {
			for (int layer = 0; layer < palimpsest::MEMORY_LAYERS; ++layer) {
				for (int bin = 0; bin < palimpsest::BIN_COUNT; ++bin) {
					float value = engine.memoryValue(channel, layer, bin);
					if (value <= 1e-7f)
						continue;
					json_t* entry = json_array();
					json_array_append_new(entry, json_integer(channel));
					json_array_append_new(entry, json_integer(layer));
					json_array_append_new(entry, json_integer(bin));
					json_array_append_new(entry, json_real(value));
					json_array_append_new(memory, entry);
				}
			}
		}
		json_object_set_new(root, "spectralMemory", memory);
		return root;
	}

	void dataFromJson(json_t* root) override {
		engine.clearMemory();
		if (json_t* value = json_object_get(root, "sealed"))
			sealed = json_boolean_value(value);
		if (json_t* value = json_object_get(root, "clockPosition"))
			engine.setClockPosition((float) json_real_value(value));
		if (json_t* memory = json_object_get(root, "spectralMemory")) {
			size_t index;
			json_t* entry;
			json_array_foreach(memory, index, entry) {
				if (!json_is_array(entry) || json_array_size(entry) < 4)
					continue;
				int channel = (int) json_integer_value(json_array_get(entry, 0));
				int layer = (int) json_integer_value(json_array_get(entry, 1));
				int bin = (int) json_integer_value(json_array_get(entry, 2));
				float value = (float) json_real_value(json_array_get(entry, 3));
				engine.setMemoryValue(channel, layer, bin, value);
			}
		}
	}

	void publishUi(float trace, float age, float motion) {
		for (int layer = 0; layer < palimpsest::MEMORY_LAYERS; ++layer)
			for (int band = 0; band < palimpsest::DISPLAY_BANDS; ++band)
				uiBands[layer][band].store(engine.displayBand(layer, band), std::memory_order_relaxed);
		uiMotion.store(motion, std::memory_order_relaxed);
		uiTrace.store(trace, std::memory_order_relaxed);
		uiAge.store(age, std::memory_order_relaxed);
	}

	void process(const ProcessArgs& args) override {
		if (clearRequested.exchange(false)) {
			engine.clearMemory();
			engine.resetRuntime();
		}
		if (sealButton.process(params[SEAL_PARAM].getValue() > 0.5f))
			sealed = !sealed;
		if (washButton.process(params[WASH_PARAM].getValue() > 0.5f)) {
			engine.requestWash(0.f);
			washFlash = 1.f;
		}

		float clockTrim = params[CLOCK_TRIM_PARAM].getValue();
		float clockVoltage = inputs[CLOCK_INPUT].getVoltage() * palimpsest::clamp(1.f + clockTrim, 0.05f, 2.f);
		if (clockTrigger.process(clockVoltage, 0.1f, 1.f))
			engine.advanceClock(clockTrim);
		float exciteTrim = params[EXCITE_TRIM_PARAM].getValue();
		float exciteVoltage = inputs[EXCITE_INPUT].getVoltage() * palimpsest::clamp(1.f + exciteTrim, 0.05f, 2.f);
		if (exciteTrigger.process(exciteVoltage, 0.1f, 1.f))
			engine.triggerExcite(exciteTrim);
		float resetTrim = params[RESET_TRIM_PARAM].getValue();
		float resetVoltage = inputs[RESET_INPUT].getVoltage() * palimpsest::clamp(1.f + resetTrim, 0.05f, 2.f);
		if (resetTrigger.process(resetVoltage, 0.1f, 1.f))
			engine.resetTraversal(resetTrim);

		palimpsest::Parameters p;
		p.age = params[AGE_PARAM].getValue();
		p.imprint = params[IMPRINT_PARAM].getValue();
		p.erosion = params[EROSION_PARAM].getValue();
		p.gravity = params[GRAVITY_PARAM].getValue();
		p.drift = params[DRIFT_PARAM].getValue();
		p.bloom = params[BLOOM_PARAM].getValue();
		p.trace = params[TRACE_PARAM].getValue();
		p.transposeVoltage = inputs[VOCT_INPUT].getVoltage();
		p.transposeDepth = 1.f + params[VOCT_TRIM_PARAM].getValue();
		p.sealed = sealed;
		p.excitePatched = inputs[EXCITE_INPUT].isConnected();

		float left = inputs[IN_L_INPUT].getVoltage();
		float right = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : left;
		palimpsest::Outputs result = engine.process(left, right,
			params[IN_L_TRIM_PARAM].getValue() * 12.f,
			params[IN_R_TRIM_PARAM].getValue() * 12.f, p);
		outputs[OUT_L_OUTPUT].setVoltage(result.mainLeft);
		outputs[OUT_R_OUTPUT].setVoltage(result.mainRight);
		outputs[GHOST_L_OUTPUT].setVoltage(result.ghostLeft);
		outputs[GHOST_R_OUTPUT].setVoltage(result.ghostRight);
		outputs[MOTION_OUTPUT].setVoltage(result.motion * 10.f);

		washFlash *= std::exp(-args.sampleTime * 7.f);
		lights[SEAL_LIGHT].setBrightnessSmooth(sealed ? 1.f : 0.f, args.sampleTime);
		lights[WASH_LIGHT].setBrightnessSmooth(washFlash, args.sampleTime);
		if (uiDivider.process() || engine.consumeFrameReady())
			publishUi(p.trace, p.age, result.motion);
	}
};

struct PalimpsestKnob : app::SvgKnob {
	PalimpsestKnob() {
		minAngle = -0.76f * M_PI;
		maxAngle = 0.76f * M_PI;
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/palimpsest-knob.svg")));
	}
};

struct PalimpsestTraceKnob : app::SvgKnob {
	PalimpsestTraceKnob() {
		minAngle = -0.76f * M_PI;
		maxAngle = 0.76f * M_PI;
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/palimpsest-trace-knob.svg")));
	}
};

struct PalimpsestTrim : app::SvgKnob {
	PalimpsestTrim() {
		minAngle = -0.75f * M_PI;
		maxAngle = 0.75f * M_PI;
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/palimpsest-trim.svg")));
	}
};

struct PalimpsestSealButton : app::SvgSwitch {
	PalimpsestSealButton() {
		momentary = true;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/palimpsest-seal-0.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/palimpsest-seal-1.svg")));
	}
};

struct PalimpsestWashButton : app::SvgSwitch {
	PalimpsestWashButton() {
		momentary = true;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/palimpsest-wash-0.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/palimpsest-wash-1.svg")));
	}
};

template <typename TBase, bool WashIcon>
struct PalimpsestSquareLight : TBase {
	PalimpsestSquareLight() {
		this->box.size = mm2px(Vec(6.4f, 6.4f));
		this->bgColor = nvgRGBA(0x12, 0x12, 0x10, 0xff);
		this->borderColor = nvgRGBA(0, 0, 0, 0);
	}
	void drawBackground(const widget::Widget::DrawArgs&) override {
		// The switch SVG supplies the engraved icon and dark glass. Keeping this
		// layer transparent prevents the lamp overlay from hiding that artwork.
	}
	void drawLight(const widget::Widget::DrawArgs& args) override {
		const float cx = this->box.size.x * 0.5f;
		const float cy = this->box.size.y * 0.48f;
		if (this->color.a > 0.f) {
			NVGpaint glow = nvgRadialGradient(args.vg, cx, cy, 0.f, this->box.size.x * 0.78f,
				this->color, color::mult(this->color, 0.f));
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0.f, 0.f, this->box.size.x, this->box.size.y, mm2px(0.7f));
			nvgFillPaint(args.vg, glow);
			nvgFill(args.vg);
		}

		// Draw the pictogram last so it can never be obscured by the lamp layer.
		NVGcolor ink = WashIcon ? nvgRGBA(0x83, 0xe8, 0xef, 0xe8)
			: nvgRGBA(0xe4, 0xb5, 0x5d, 0xe8);
		nvgStrokeColor(args.vg, ink);
		nvgStrokeWidth(args.vg, mm2px(0.32f));
		nvgLineCap(args.vg, NVG_ROUND);
		nvgLineJoin(args.vg, NVG_ROUND);
		if (WashIcon) {
			// Keep the droplet and ripples as three separate silhouettes. The
			// earlier concentric arcs met the bottom of the droplet and read as an
			// overlapping glyph at Rack scale.
			const float dropCy = cy - mm2px(0.35f);
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, cx, dropCy - mm2px(1.55f));
			nvgBezierTo(args.vg, cx - mm2px(0.45f), dropCy - mm2px(0.65f),
				cx - mm2px(0.9f), dropCy - mm2px(0.1f), cx - mm2px(0.9f), dropCy + mm2px(0.38f));
			nvgBezierTo(args.vg, cx - mm2px(0.9f), dropCy + mm2px(1.05f),
				cx + mm2px(0.9f), dropCy + mm2px(1.05f), cx + mm2px(0.9f), dropCy + mm2px(0.38f));
			nvgBezierTo(args.vg, cx + mm2px(0.9f), dropCy - mm2px(0.1f),
				cx + mm2px(0.45f), dropCy - mm2px(0.65f), cx, dropCy - mm2px(1.55f));
			nvgStroke(args.vg);

			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, cx - mm2px(1.25f), cy + mm2px(1.05f));
			nvgBezierTo(args.vg, cx - mm2px(0.8f), cy + mm2px(1.65f),
				cx + mm2px(0.8f), cy + mm2px(1.65f), cx + mm2px(1.25f), cy + mm2px(1.05f));
			nvgStroke(args.vg);
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, cx - mm2px(1.85f), cy + mm2px(1.55f));
			nvgBezierTo(args.vg, cx - mm2px(1.15f), cy + mm2px(2.35f),
				cx + mm2px(1.15f), cy + mm2px(2.35f), cx + mm2px(1.85f), cy + mm2px(1.55f));
			nvgStroke(args.vg);
		}
		else {
			// Wax-seal disc with a ring of short rays.
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, cx, cy, mm2px(1.42f));
			nvgStroke(args.vg);
			for (int ray = 0; ray < 10; ++ray) {
				float angle = ray * 2.f * M_PI / 10.f;
				float inner = mm2px(1.67f);
				float outer = mm2px(2.05f);
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, cx + std::cos(angle) * inner, cy + std::sin(angle) * inner);
				nvgLineTo(args.vg, cx + std::cos(angle) * outer, cy + std::sin(angle) * outer);
				nvgStroke(args.vg);
			}
		}
	}
	void drawHalo(const widget::Widget::DrawArgs&) override {}
};

struct PalimpsestPort : app::PortWidget {
	NVGcolor accent = nvgRGBA(0xb9, 0x8a, 0x3d, 0xff);
	PalimpsestPort() { box.size = mm2px(Vec(5.5f, 5.5f)); }
	void draw(const DrawArgs& args) override {
		const float cx = box.size.x * 0.5f;
		const float cy = box.size.y * 0.5f;
		const float r = mm2px(2.35f);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, cx + 0.6f, cy + 0.8f, r);
		nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 0xa0));
		nvgFill(args.vg);
		NVGpaint metal = nvgLinearGradient(args.vg, cx - r, cy - r, cx + r, cy + r,
			nvgRGBA(0xf1, 0xee, 0xe2, 0xff), nvgRGBA(0x54, 0x56, 0x55, 0xff));
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, cx, cy, r);
		nvgFillPaint(args.vg, metal);
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, accent);
		nvgStrokeWidth(args.vg, mm2px(0.18f));
		nvgStroke(args.vg);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, cx, cy, r * 0.68f);
		nvgFillColor(args.vg, nvgRGBA(0x17, 0x18, 0x18, 0xff));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGBA(0xd6, 0xd1, 0xc4, 0xc8));
		nvgStrokeWidth(args.vg, mm2px(0.14f));
		nvgStroke(args.vg);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, cx, cy, r * 0.39f);
		nvgFillColor(args.vg, nvgRGBA(0x02, 0x03, 0x03, 0xff));
		nvgFill(args.vg);
		app::PortWidget::draw(args);
	}
};

struct PalimpsestOutputPort : PalimpsestPort {
	PalimpsestOutputPort() { accent = nvgRGBA(0xdc, 0xae, 0x5d, 0xff); }
};

struct PalimpsestDisplay : TransparentWidget {
	Palimpsest* module = nullptr;
	float previewPhase = 0.f;

	static NVGcolor traceColor(int layer, unsigned char alpha = 255) {
		static const unsigned char colors[4][3] = {
			{0x20, 0xd5, 0xec}, {0xff, 0xb9, 0x24}, {0xf0, 0x36, 0xa0}, {0x81, 0xe6, 0x45}
		};
		return nvgRGBA(colors[layer][0], colors[layer][1], colors[layer][2], alpha);
	}

	float bandValue(int layer, int band) const {
		if (module)
			return module->uiBands[layer][band].load(std::memory_order_relaxed);
		float x = (float) band / palimpsest::DISPLAY_BANDS;
		return 0.22f + 0.3f * std::pow(std::sin(x * 15.f + layer * 1.9f), 2.f)
			* std::exp(-x * (0.8f + layer * 0.2f));
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		float w = box.size.x;
		float h = box.size.y;
		const float plotLeft = mm2px(5.1f);
		const float plotRight = w - mm2px(2.5f);
		const float plotTop = mm2px(1.8f);
		const float plotBottom = h - mm2px(1.8f);
		const float plotWidth = plotRight - plotLeft;
		const float plotHeight = plotBottom - plotTop;
		nvgSave(vg);
		nvgScissor(vg, RECT_ARGS(args.clipBox));
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0.5f, 0.5f, w - 1.f, h - 1.f, mm2px(1.5f));
		NVGpaint glass = nvgLinearGradient(vg, 0.f, 0.f, 0.f, h,
			nvgRGBA(0x05, 0x0b, 0x0e, 0xff), nvgRGBA(0x01, 0x03, 0x04, 0xff));
		nvgFillPaint(vg, glass);
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGBA(0xc6, 0x8d, 0x31, 0xd0));
		nvgStrokeWidth(vg, 1.f);
		nvgStroke(vg);

		// Fine phosphor grid and scan-line texture are static; all bright traces
		// below are mapped directly from the four DSP memory layers.
		for (int i = 1; i < 8; ++i) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, plotLeft + plotWidth * i / 8.f, plotTop);
			nvgLineTo(vg, plotLeft + plotWidth * i / 8.f, plotBottom);
			nvgStrokeColor(vg, nvgRGBA(0x70, 0x85, 0x80, 0x0d));
			nvgStrokeWidth(vg, 0.5f);
			nvgStroke(vg);
		}
		for (int i = 1; i < 6; ++i) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, plotLeft, plotTop + plotHeight * i / 6.f);
			nvgLineTo(vg, plotRight, plotTop + plotHeight * i / 6.f);
			nvgStrokeColor(vg, nvgRGBA(0x70, 0x85, 0x80, 0x0d));
			nvgStrokeWidth(vg, 0.5f);
			nvgStroke(vg);
		}

		std::shared_ptr<Font> font = APP->window->loadFont(
			asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (font) {
			static const char* frequencyLabels[9] = {"20k", "10k", "5k", "2k", "1k", "500", "200", "100", "20"};
			nvgFontFaceId(vg, font->handle);
			nvgFontSize(vg, mm2px(1.03f));
			nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, nvgRGBA(0xd7, 0xa2, 0x46, 0xb0));
			for (int i = 0; i < 9; ++i) {
				float y = plotTop + plotHeight * i / 8.f;
				nvgText(vg, plotLeft - mm2px(0.55f), y, frequencyLabels[i], nullptr);
				nvgBeginPath(vg);
				nvgMoveTo(vg, plotRight, y);
				nvgLineTo(vg, plotRight + mm2px(0.9f), y);
				nvgStrokeColor(vg, nvgRGBA(0xd7, 0xa2, 0x46, 0x9a));
				nvgStrokeWidth(vg, 0.55f);
				nvgStroke(vg);
			}
		}
		nvgBeginPath(vg);
		nvgMoveTo(vg, plotRight, plotTop);
		nvgLineTo(vg, plotRight, plotBottom);
		nvgStrokeColor(vg, nvgRGBA(0xc9, 0x91, 0x32, 0x8c));
		nvgStrokeWidth(vg, 0.55f);
		nvgStroke(vg);

		float motion = module ? module->uiMotion.load(std::memory_order_relaxed) : 0.28f;
		float selected = (module ? module->uiAge.load(std::memory_order_relaxed) : 0.35f) * 3.f;
		for (int memoryLayer = 3; memoryLayer >= 0; --memoryLayer) {
			float distance = std::fabs(selected - memoryLayer);
			float emphasis = 0.32f + 0.68f * std::exp(-distance * 1.8f);
			float layerEnergy = 0.f;
			for (int band = 0; band < palimpsest::DISPLAY_BANDS; ++band)
				layerEnergy += bandValue(memoryLayer, band);
			layerEnergy /= palimpsest::DISPLAY_BANDS;
			float presence = std::sqrt(palimpsest::clamp(layerEnergy * 7.f, 0.f, 1.f));

			// Translate real log-band energy into the descending, hand-traced
			// spectral envelopes of the concept artwork. The fine detail is seeded
			// deterministically and its depth comes from this layer's real energy.
			auto spectralY = [=](int band, float persistenceOffset) {
				float xNorm = band / (palimpsest::DISPLAY_BANDS - 1.f);
				float value = palimpsest::clamp(bandValue(memoryLayer, band), 0.f, 1.f);
				float shaped = std::pow(value, 0.68f);
				float etched = std::sin(band * 2.137f + memoryLayer * 0.91f)
					+ 0.56f * std::sin(band * 4.731f + memoryLayer * 2.17f)
					+ 0.28f * std::sin(band * 8.113f + memoryLayer * 0.37f);
				float yLane = plotTop + plotHeight * (0.105f + memoryLayer * 0.245f);
				float downwardTilt = xNorm * plotHeight * (0.045f + memoryLayer * 0.004f);
				float spectrum = shaped * plotHeight * 0.17f;
				float detail = etched * presence * plotHeight * 0.011f * (1.f - xNorm * 0.38f);
				return palimpsest::clamp(yLane + downwardTilt - spectrum - detail
					+ persistenceOffset, plotTop, plotBottom);
			};

			// Faint data-derived persistence strokes reproduce the layered LCD
			// handwriting without introducing an unrelated free-running animation.
			for (int trail = 0; trail < 3; ++trail) {
				nvgBeginPath(vg);
				for (int band = 0; band < palimpsest::DISPLAY_BANDS; ++band) {
					float x = plotLeft + plotWidth * band / (palimpsest::DISPLAY_BANDS - 1.f);
					float drift = (trail - 1.f) * plotHeight * (0.012f + motion * 0.006f)
						+ std::sin(band * (0.19f + trail * 0.027f) + trail * 1.7f)
						* presence * plotHeight * 0.006f;
					float y = spectralY(band, drift);
					if (band == 0) nvgMoveTo(vg, x, y); else nvgLineTo(vg, x, y);
				}
				nvgStrokeColor(vg, traceColor(memoryLayer,
					(unsigned char) ((13.f + presence * 30.f) * emphasis)));
				nvgStrokeWidth(vg, 0.42f);
				nvgStroke(vg);
			}
			for (int glow = 2; glow >= 0; --glow) {
				nvgBeginPath(vg);
				for (int band = 0; band < palimpsest::DISPLAY_BANDS; ++band) {
					float x = plotLeft + plotWidth * band / (palimpsest::DISPLAY_BANDS - 1.f);
					float y = spectralY(band, 0.f);
					if (band == 0) nvgMoveTo(vg, x, y); else nvgLineTo(vg, x, y);
				}
				unsigned char alpha = glow == 0 ? (unsigned char) (125.f + presence * 65.f + emphasis * 60.f)
					: (unsigned char) ((12.f + motion * 32.f) * emphasis);
				nvgStrokeColor(vg, traceColor(memoryLayer, alpha));
				nvgStrokeWidth(vg, glow == 0 ? 0.58f + emphasis * 0.54f : 1.8f + glow * 1.8f);
				nvgLineCap(vg, NVG_ROUND);
				nvgLineJoin(vg, NVG_ROUND);
				nvgStroke(vg);
			}

			// Small phosphor knots call out genuine local spectral peaks.
			for (int band = 2; band < palimpsest::DISPLAY_BANDS - 2; ++band) {
				float value = bandValue(memoryLayer, band);
				if (value < 0.08f || value <= bandValue(memoryLayer, band - 1)
					|| value < bandValue(memoryLayer, band + 1))
					continue;
				float x = plotLeft + plotWidth * band / (palimpsest::DISPLAY_BANDS - 1.f);
				float y = spectralY(band, 0.f);
				nvgBeginPath(vg);
				nvgCircle(vg, x, y, mm2px(0.18f + value * 0.12f));
				nvgFillColor(vg, traceColor(memoryLayer, 0xe0));
				nvgFill(vg);
			}
		}
		nvgRestore(vg);
	}
};

struct PalimpsestWidget : ModuleWidget {
	PalimpsestWidget(Palimpsest* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Palimpsest.svg")));

		PalimpsestDisplay* display = new PalimpsestDisplay;
		display->module = module;
		display->box.pos = mm2px(Vec(17.05f, 14.f));
		display->box.size = mm2px(Vec(37.05f, 38.8f));
		addChild(display);

		addParam(createParamCentered<PalimpsestKnob>(mm2px(Vec(8.3f, 22.1f)), module, Palimpsest::AGE_PARAM));
		addParam(createParamCentered<PalimpsestKnob>(mm2px(Vec(8.3f, 41.2f)), module, Palimpsest::IMPRINT_PARAM));
		addParam(createParamCentered<PalimpsestKnob>(mm2px(Vec(8.3f, 60.3f)), module, Palimpsest::EROSION_PARAM));
		addParam(createParamCentered<PalimpsestKnob>(mm2px(Vec(62.8f, 22.1f)), module, Palimpsest::GRAVITY_PARAM));
		addParam(createParamCentered<PalimpsestKnob>(mm2px(Vec(62.8f, 41.2f)), module, Palimpsest::DRIFT_PARAM));
		addParam(createParamCentered<PalimpsestKnob>(mm2px(Vec(62.8f, 60.3f)), module, Palimpsest::BLOOM_PARAM));
		addParam(createParamCentered<PalimpsestTraceKnob>(mm2px(Vec(35.56f, 69.1f)), module, Palimpsest::TRACE_PARAM));

		addParam(createLightParamCentered<LightButton<PalimpsestSealButton, PalimpsestSquareLight<YellowLight, false>>>(
			mm2px(Vec(23.f, 83.2f)), module, Palimpsest::SEAL_PARAM, Palimpsest::SEAL_LIGHT));
		addParam(createLightParamCentered<LightButton<PalimpsestWashButton, PalimpsestSquareLight<BlueLight, true>>>(
			mm2px(Vec(48.1f, 83.2f)), module, Palimpsest::WASH_PARAM, Palimpsest::WASH_LIGHT));

		const float inputX[6] = {7.f, 18.42f, 29.84f, 41.26f, 52.68f, 64.1f};
		const int inputIds[6] = {Palimpsest::IN_L_INPUT, Palimpsest::IN_R_INPUT, Palimpsest::VOCT_INPUT,
			Palimpsest::CLOCK_INPUT, Palimpsest::EXCITE_INPUT, Palimpsest::RESET_INPUT};
		const int trimIds[6] = {Palimpsest::IN_L_TRIM_PARAM, Palimpsest::IN_R_TRIM_PARAM, Palimpsest::VOCT_TRIM_PARAM,
			Palimpsest::CLOCK_TRIM_PARAM, Palimpsest::EXCITE_TRIM_PARAM, Palimpsest::RESET_TRIM_PARAM};
		for (int i = 0; i < 6; ++i) {
			addInput(createInputCentered<PalimpsestPort>(mm2px(Vec(inputX[i], 98.1f)), module, inputIds[i]));
			addParam(createParamCentered<PalimpsestTrim>(mm2px(Vec(inputX[i], 104.6f)), module, trimIds[i]));
		}

		const float outputX[5] = {9.2f, 22.4f, 35.56f, 48.75f, 61.95f};
		for (int i = 0; i < 5; ++i)
			addOutput(createOutputCentered<PalimpsestOutputPort>(mm2px(Vec(outputX[i], 117.4f)), module, Palimpsest::OUT_L_OUTPUT + i));
	}

	void appendContextMenu(Menu* menu) override {
		Palimpsest* module = dynamic_cast<Palimpsest*>(this->module);
		if (!module)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Spectral memory"));
		menu->addChild(createMenuItem("Clear all imprints", "", [module]() {
			module->clearRequested.store(true);
		}));
	}
};

Model* modelPalimpsest = createModel<Palimpsest, PalimpsestWidget>("Palimpsest");
