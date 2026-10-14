#include "plugin.hpp"
#include "GrainloomAdapter.hpp"
#include "GrainloomSnapshot.hpp"
#include <atomic>
#include <memory>

struct Grainloom : Module {
  enum ParamId { TIME, PITCH, SIZE, SHAPE, TIME_RANDOM, PITCH_RANDOM, SIZE_RANDOM, SHAPE_RANDOM,
    DENSITY, FEEDBACK, REVERB, MIX, FREEZE, SEED, LATCH, QUALITY, ASSIGN, DEPTH, GAIN, CALIBRATE,
    GATE_MODE, SAVE_BUFFER, ASSIGN_FEEDBACK_DEPTH, ASSIGN_MIX_DEPTH, ASSIGN_REVERB_DEPTH, PARAMS_LEN };
  enum InputId { TIME_CV, PITCH_CV, SIZE_CV, SHAPE_CV, DENSITY_CV, ASSIGN_CV,
    AUDIO_L, AUDIO_R, FREEZE_GATE, SEED_GATE, INPUTS_LEN };
  enum OutputId { OUT_L, OUT_R, OUTPUTS_LEN };
  enum LightId { FREEZE_LIGHT, SEED_LIGHT, LATCH_LIGHT, LIGHTS_LEN };
  std::unique_ptr<grainloom::Adapter> adapter{new grainloom::Adapter()};
  std::unique_ptr<grainloom::SnapshotBank> snapshots{new grainloom::SnapshotBank()};
  std::atomic<bool> uiFrozen{false}, uiLocked{true}, uiSynth{false}, uiGain{false};
  std::atomic<float> uiPosition{0.f}, uiMeter{0.f};
  std::atomic<int> uiQuality{3};
  std::atomic<int> restoredFlags{-1};
  bool previousCalibrate = false;
  uint64_t previousBlocks = 0;
  int restoringSamples = 0;
  bool restoreInProgress = false;

  Grainloom() {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
    configParam(TIME, 0.f, 1.f, 0.f, "Time / wavetable scan", "%", 0.f, 100.f);
    configParam(PITCH, -1.f, 1.f, 0.f, "Pitch (original interval detents)");
    configParam(SIZE, 0.f, 1.f, .65f, "Grain size / reverse / infinite delay");
    configParam(SHAPE, 0.f, 1.f, .5f, "Grain envelope shape");
    const char* names[4] = {"Time", "Pitch", "Size", "Shape"};
    for (int i = 0; i < 4; ++i) {
      configParam(TIME_RANDOM + i, -1.f, 1.f, 0.f, std::string(names[i]) + " attenurandomizer");
      configInput(TIME_CV + i, std::string(names[i]) + (i == 1 ? " CV (1 V/oct)" : " CV (5 V full range)"));
    }
    configParam(DENSITY, 0.f, 1.f, .3f, "Density / clock division / probability");
    configParam(FEEDBACK, 0.f, 1.f, 0.f, "Feedback / wavetable bank", "%", 0.f, 100.f);
    configParam(REVERB, 0.f, 1.f, 0.f, "Reverb", "%", 0.f, 100.f);
    configParam(MIX, 0.f, 1.f, .5f, "Dry / wet", "%", 0.f, 100.f);
    configButton(FREEZE, "Toggle Freeze (with Seed: latch)");
    configButton(SEED, "Seed (hold 1.5 seconds to latch)");
    configButton(LATCH, "Latch Seed");
    configSwitch(QUALITY, 0.f, 3.f, 3.f, "Recording quality", {"Dirty cassette", "Clean tape", "Vintage digital", "Digital"});
    configSwitch(ASSIGN, 0.f, 3.f, 0.f, "Assignable CV destination", {"Feedback", "Dry / wet", "Reverb", "All three"});
    configParam(DEPTH, 0.f, 1.f, 1.f, "Assignable CV depth", "%", 0.f, 100.f);
    configParam(GAIN, 0.f, 255.f, 0.f, "Input gain (0 = auto; 1-255 = manual)");
    getParamQuantity(GAIN)->snapEnabled = true;
    configButton(CALIBRATE, "Restart automatic gain calibration");
    configSwitch(GATE_MODE, 0.f, 1.f, 0.f, "Right output", {"Audio", "Grain gate"});
    configSwitch(SAVE_BUFFER, 0.f, 1.f, 1.f, "Save frozen audio in patch", {"Off", "On"});
    configParam(ASSIGN_FEEDBACK_DEPTH, 0.f, 1.f, 1.f, "Combined assignment: feedback depth", "%", 0.f, 100.f);
    configParam(ASSIGN_MIX_DEPTH, 0.f, 1.f, 0.f, "Combined assignment: mix depth", "%", 0.f, 100.f);
    configParam(ASSIGN_REVERB_DEPTH, 0.f, 1.f, .5f, "Combined assignment: reverb depth", "%", 0.f, 100.f);
    configInput(DENSITY_CV, "Density CV (5 V full range)");
    configInput(ASSIGN_CV, "Assignable CV (5 V full range)");
    configInput(AUDIO_L, "Audio left (either input can be mono)");
    configInput(AUDIO_R, "Audio right");
    configInput(FREEZE_GATE, "Freeze gate"); configInput(SEED_GATE, "Seed gate / clock");
    configOutput(OUT_L, "Audio left (stereo sum when right unpatched)");
    configOutput(OUT_R, "Audio right / grain gate");
    configBypass(AUDIO_L, OUT_L); configBypass(AUDIO_R, OUT_R);
  }
  void onSampleRateChange(const SampleRateChangeEvent& e) override { adapter->setHostRate(e.sampleRate); }
  void onReset(const ResetEvent& e) override {
    Module::onReset(e);
    adapter->setFreeze(false); adapter->setSeedLocked(true);
    adapter->core.configure(3, false, false, true); adapter->resetBridge();
  }
  void process(const ProcessArgs& args) override {
    auto& c = adapter->controls;
    c.time = params[TIME].getValue(); c.pitch = params[PITCH].getValue();
    c.size = params[SIZE].getValue(); c.shape = params[SHAPE].getValue();
    c.density = params[DENSITY].getValue();
    for (int i = 0; i < 4; ++i) {
      c.random[i] = params[TIME_RANDOM + i].getValue();
      c.cv[i] = inputs[TIME_CV + i].getVoltage() * (i == 1 ? 12.f : .2f);
      c.connected[i] = inputs[TIME_CV + i].isConnected();
    }
    c.densityCv = inputs[DENSITY_CV].getVoltage() * .2f;
    c.assignCv = inputs[ASSIGN_CV].getVoltage() * .2f * params[DEPTH].getValue();
    c.assignment = int(params[ASSIGN].getValue());
    for (int i = 0; i < 3; ++i) c.assignDepth[i] = params[ASSIGN_FEEDBACK_DEPTH + i].getValue();
    c.feedback = params[FEEDBACK].getValue(); c.reverb = params[REVERB].getValue(); c.mix = params[MIX].getValue();
    c.quality = int(params[QUALITY].getValue()); c.gain = int(params[GAIN].getValue());
    c.freezeButton = params[FREEZE].getValue() > .5f; c.seedButton = params[SEED].getValue() > .5f;
    c.latchButton = params[LATCH].getValue() > .5f;
    c.seedConnected = inputs[SEED_GATE].isConnected();
    c.leftConnected = inputs[AUDIO_L].isConnected(); c.rightConnected = inputs[AUDIO_R].isConnected();
    c.gateOutput = params[GATE_MODE].getValue() > .5f;
    bool calibrate = params[CALIBRATE].getValue() > .5f;
    if (calibrate && !previousCalibrate) { params[GAIN].setValue(0.f); c.gain = 0; c.recalibrate = true; }
    previousCalibrate = calibrate;
    int restore = restoredFlags.exchange(-1);
    if (restore >= 0) { adapter->setFreeze(restore & 1); adapter->setSeedLocked(restore & 2); }
    if (++restoringSamples >= int(args.sampleRate / 4000.f)) {
      restoringSamples = 0;
      if (snapshots->restoreTick(adapter->core)) {
        adapter->setFreeze(true);
        restoreInProgress = snapshots->restoring();
        if (!restoreInProgress) adapter->resetBridge();
        outputs[OUT_L].setVoltage(0.f); outputs[OUT_R].setVoltage(0.f);
        return;
      }
    }
    if (restoreInProgress) {
      outputs[OUT_L].setVoltage(0.f); outputs[OUT_R].setVoltage(0.f);
      return;
    }
    auto out = adapter->process(inputs[AUDIO_L].getVoltage() / 10.f, inputs[AUDIO_R].getVoltage() / 10.f,
      inputs[SEED_GATE].getVoltage(), inputs[FREEZE_GATE].getVoltage());
    float l = !outputs[OUT_R].isConnected() && !c.gateOutput ? (out.l + out.r) * .5f : out.l;
    outputs[OUT_L].setVoltage(10.f * l);
    outputs[OUT_R].setVoltage(c.gateOutput ? -10.f * out.r : 10.f * out.r);
    lights[FREEZE_LIGHT].setBrightness(adapter->frozen() ? 1.f : 0.f);
    lights[SEED_LIGHT].setBrightness(c.seedButton || inputs[SEED_GATE].getVoltage() >= 1.f ? 1.f : 0.f);
    lights[LATCH_LIGHT].setBrightness(adapter->seedLocked() ? 1.f : 0.f);
    if (adapter->renderedBlocks() != previousBlocks) {
      previousBlocks = adapter->renderedBlocks();
      snapshots->tick(adapter->core, adapter->frozen());
      uiFrozen.store(adapter->frozen()); uiLocked.store(adapter->seedLocked());
      uiSynth.store(adapter->core.synthMode()); uiQuality.store(adapter->core.media());
      uiPosition.store(adapter->core.writePointer() / float(adapter->core.maxFrames()));
      uiMeter.store(adapter->core.inputMeter()); uiGain.store(adapter->core.gainAdjusting());
    }
  }
  json_t* dataToJson() override {
    json_t* root = json_object();
    json_object_set_new(root, "version", json_integer(1));
    json_object_set_new(root, "freeze", json_boolean(uiFrozen.load()));
    json_object_set_new(root, "seedLocked", json_boolean(uiLocked.load()));
    if (uiFrozen.load() && !uiSynth.load() && params[SAVE_BUFFER].getValue() > .5f) {
      auto* slot = snapshots->claim();
      if (slot) {
        json_t* audio = json_object();
        json_object_set_new(audio, "format", json_string("beads-le-v1"));
        json_object_set_new(audio, "media", json_integer(slot->media));
        json_object_set_new(audio, "mono", json_boolean(slot->mono));
        json_object_set_new(audio, "write", json_integer(slot->write));
        json_object_set_new(audio, "crc32", json_integer(grainloom::crc32(slot->bytes.data(), slot->bytes.size())));
        std::string bytes = grainloom::encode(slot->bytes.data(), slot->bytes.size());
        json_object_set_new(audio, "bytes", json_stringn(bytes.data(), bytes.size()));
        json_object_set_new(root, "audio", audio);
        snapshots->release(slot);
      }
    }
    return root;
  }
  void dataFromJson(json_t* root) override {
    if (json_integer_value(json_object_get(root, "version")) != 1) return;
    int flags = json_is_true(json_object_get(root, "freeze")) ? 1 : 0;
    if (json_is_true(json_object_get(root, "seedLocked"))) flags |= 2;
    json_t* audio = json_object_get(root, "audio");
    const char* format = json_string_value(json_object_get(audio, "format"));
    int media = int(json_integer_value(json_object_get(audio, "media")));
    int write = int(json_integer_value(json_object_get(audio, "write")));
    bool mono = json_is_true(json_object_get(audio, "mono"));
    int maxFrames[4] = {384000, 256000, 256000, 192000};
    if (format && std::strcmp(format, "beads-le-v1") == 0 && media >= 0 && media < 4 && write >= 0 && write < maxFrames[media] * (mono ? 2 : 1)) {
      auto* slot = snapshots->reserveRestore();
      if (slot) {
        json_t* bytes = json_object_get(audio, "bytes");
        bool valid = grainloom::decode(json_string_value(bytes), json_string_length(bytes), slot->bytes.data(), slot->bytes.size());
        valid = valid && grainloom::crc32(slot->bytes.data(), slot->bytes.size()) == uint32_t(json_integer_value(json_object_get(audio, "crc32")));
        if (valid) {
          slot->media = media; slot->mono = mono; slot->write = write;
          params[QUALITY].setValue(float(media));
          slot->state.store(grainloom::SnapshotBank::RESTORE, std::memory_order_release);
          flags |= 1;
        } else slot->state.store(grainloom::SnapshotBank::FREE, std::memory_order_release);
      }
    }
    restoredFlags.store(flags);
  }
};

struct GrainloomPanel : TransparentWidget {
  Grainloom* module = nullptr;
  void label(NVGcontext* vg, int font, const char* text, float x, float y, float size, NVGcolor color) {
    nvgFontFaceId(vg, font); nvgFontSize(vg, mm2px(size));
    nvgTextLetterSpacing(vg, mm2px(.1f)); nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, color); nvgText(vg, mm2px(x), mm2px(y), text, nullptr);
  }
  void draw(const DrawArgs& args) override {
    auto font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
    if (!font) return;
    nvgSave(args.vg);
    NVGcolor white = nvgRGB(224, 224, 215), gold = nvgRGB(204, 159, 104), muted = nvgRGB(160, 170, 172);
    label(args.vg, font->handle, "G R A I N L O O M", 71.12f, 6.2f, 4.2f, gold);
    label(args.vg, font->handle, "FREEZE", 14, 26.5f, 1.65f, white);
    label(args.vg, font->handle, "SEED", 31, 26.5f, 1.65f, white);
    label(args.vg, font->handle, "LATCH", 48, 26.5f, 1.65f, white);
    const char* grain[4] = {"TIME", "PITCH", "SIZE", "SHAPE"};
    const char* texture[4] = {"DENSITY", "FEEDBACK", "REVERB", "DRY / WET"};
    for (int i = 0; i < 4; ++i) {
      float x = 20 + 34 * i;
      label(args.vg, font->handle, grain[i], x, 33.6f, 2.25f, white);
      label(args.vg, font->handle, "RANDOM / CV", x, 55.3f, 1.45f, muted);
      label(args.vg, font->handle, "CV", x, 68.4f, 1.5f, gold);
      label(args.vg, font->handle, texture[i], x, 83.8f, 1.9f, white);
    }
    const char* utility[7] = {"DENS CV", "ASSIGN CV", "DEPTH", "DEST", "QUALITY", "GAIN", "AUTO"};
    const float ux[7] = {10, 28, 46, 64, 84, 106, 126};
    for (int i = 0; i < 7; ++i) label(args.vg, font->handle, utility[i], ux[i], 104.8f, 1.4f, muted);
    const char* ports[6] = {"IN L", "IN R", "FREEZE", "SEED", "OUT L", "OUT R"};
    const float px[6] = {12, 30, 48, 66, 106, 126};
    for (int i = 0; i < 6; ++i) label(args.vg, font->handle, ports[i], px[i], 114.8f, 1.5f, i < 4 ? white : gold);
    label(args.vg, font->handle, "halfagiraf", 80, 123.5f, 1.7f, gold);
    nvgRestore(args.vg);
  }
  void drawLayer(const DrawArgs& args, int layer) override {
    if (layer != 1) return;
    auto font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
    if (!font) return;
    nvgSave(args.vg);
    const char* qualities[4] = {"CASSETTE", "CLEAN TAPE", "VINTAGE", "DIGITAL"};
    int quality = module ? module->uiQuality.load() : 3;
    bool frozen = module && module->uiFrozen.load();
    bool synth = module && module->uiSynth.load();
    label(args.vg, font->handle, synth ? "WAVETABLE" : frozen ? "FROZEN" : "RECORDING", 82, 17.4f, 1.8f, nvgRGB(234, 192, 127));
    label(args.vg, font->handle, qualities[quality], 119, 17.4f, 1.7f, nvgRGB(185, 205, 204));
    nvgBeginPath(args.vg); nvgRect(args.vg, mm2px(65), mm2px(21.5f), mm2px(66), mm2px(1.2f));
    nvgFillColor(args.vg, nvgRGB(43, 49, 49)); nvgFill(args.vg);
    float position = module ? module->uiPosition.load() : .35f;
    nvgBeginPath(args.vg); nvgRect(args.vg, mm2px(65 + position * 66), mm2px(20.8f), mm2px(.65f), mm2px(2.6f));
    nvgFillColor(args.vg, frozen ? nvgRGB(228, 172, 98) : nvgRGB(147, 197, 190)); nvgFill(args.vg);
    nvgRestore(args.vg);
  }
};

struct GrainloomWidget : ModuleWidget {
  GrainloomWidget(Grainloom* module) {
    setModule(module); setPanel(createPanel(asset::plugin(pluginInstance, "res/Grainloom.svg")));
    auto* panel = createWidget<GrainloomPanel>(Vec()); panel->box.size = box.size; panel->module = module; addChild(panel);
    addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
    addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
    addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    for (int i = 0; i < 4; ++i) {
      float x = 20 + 34 * i;
      addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(x, 43)), module, Grainloom::TIME + i));
      addParam(createParamCentered<Trimpot>(mm2px(Vec(x, 61.3f)), module, Grainloom::TIME_RANDOM + i));
      addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x, 74)), module, Grainloom::TIME_CV + i));
    }
    const int texture[4] = {Grainloom::DENSITY, Grainloom::FEEDBACK, Grainloom::REVERB, Grainloom::MIX};
    for (int i = 0; i < 4; ++i) addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(20 + 34 * i, 93)), module, texture[i]));
    const int buttons[3] = {Grainloom::FREEZE, Grainloom::SEED, Grainloom::LATCH};
    for (int i = 0; i < 3; ++i) {
      addParam(createParamCentered<VCVButton>(mm2px(Vec(14 + 17 * i, 20)), module, buttons[i]));
      addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(14 + 17 * i, 14.5f)), module, Grainloom::FREEZE_LIGHT + i));
    }
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10, 109.5f)), module, Grainloom::DENSITY_CV));
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(28, 109.5f)), module, Grainloom::ASSIGN_CV));
    const int utility[4] = {Grainloom::DEPTH, Grainloom::ASSIGN, Grainloom::QUALITY, Grainloom::GAIN};
    const float ux[4] = {46, 64, 84, 106};
    for (int i = 0; i < 4; ++i) addParam(createParamCentered<Trimpot>(mm2px(Vec(ux[i], 109.5f)), module, utility[i]));
    addParam(createParamCentered<VCVButton>(mm2px(Vec(126, 109.5f)), module, Grainloom::CALIBRATE));
    const int input[4] = {Grainloom::AUDIO_L, Grainloom::AUDIO_R, Grainloom::FREEZE_GATE, Grainloom::SEED_GATE};
    for (int i = 0; i < 4; ++i) addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12 + 18 * i, 120)), module, input[i]));
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(106, 120)), module, Grainloom::OUT_L));
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(126, 120)), module, Grainloom::OUT_R));
  }
  void appendContextMenu(Menu* menu) override {
    auto* m = getModule<Grainloom>(); if (!m) return;
    menu->addChild(new MenuSeparator);
    menu->addChild(createBoolMenuItem("Right output carries grain gates", "",
      [m]() { return m->params[Grainloom::GATE_MODE].getValue() > .5f; },
      [m](bool enabled) { m->params[Grainloom::GATE_MODE].setValue(enabled ? 1.f : 0.f); }));
    menu->addChild(createBoolMenuItem("Save frozen recording in patch", "",
      [m]() { return m->params[Grainloom::SAVE_BUFFER].getValue() > .5f; },
      [m](bool enabled) { m->params[Grainloom::SAVE_BUFFER].setValue(enabled ? 1.f : 0.f); }));
    menu->addChild(createSubmenuItem("Combined assignable CV depths", "", [m](Menu* submenu) {
      const char* names[3] = {"Feedback", "Dry / wet", "Reverb"};
      for (int i = 0; i < 3; ++i) {
        submenu->addChild(createMenuLabel(names[i]));
        auto* slider = new ui::Slider;
        slider->quantity = m->getParamQuantity(Grainloom::ASSIGN_FEEDBACK_DEPTH + i);
        slider->box.size.x = 200.f;
        submenu->addChild(slider);
      }
    }));
  }
};
Model* modelGrainloom = createModel<Grainloom, GrainloomWidget>("Grainloom");
