#include "plugin.hpp"
#include "GrainloomAdapter.hpp"
#include "GrainloomSnapshot.hpp"
#include <atomic>
#include <memory>

struct GrainloomPitchQuantity : ParamQuantity {
  float getDisplayValue() override { return grainloom::Adapter::pitchLaw(getValue()); }
  void setDisplayValue(float semitones) override {
    float low = -1.f, high = 1.f;
    for (int i = 0; i < 24; ++i) {
      float midpoint = (low + high) * .5f;
      if (grainloom::Adapter::pitchLaw(midpoint) < semitones) low = midpoint;
      else high = midpoint;
    }
    setValue((low + high) * .5f);
  }
};

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
  static constexpr int displayColumns = 128;
  std::array<std::atomic<float>, displayColumns> uiWaveMin{}, uiWaveMax{};
  std::array<std::atomic<float>, displayColumns> uiWaveMinRight{}, uiWaveMaxRight{};
  std::atomic<float> uiBufferSeconds{4.f};
  int displayBlockCounter = 0, liveColumn = 0, liveSamples = 0;
  float liveMinimum = 0.f, liveMaximum = 0.f;
  float liveMinimumRight = 0.f, liveMaximumRight = 0.f;
  std::atomic<uint32_t> persistedState{26};
  std::atomic<int> restoredFlags{-1};
  bool previousCalibrate = false;
  uint64_t previousBlocks = 0;
  int restoringSamples = 0;
  bool restoreInProgress = false;

  Grainloom() {
    for (int i = 0; i < displayColumns; ++i) { uiWaveMin[i].store(0.f); uiWaveMax[i].store(0.f); uiWaveMinRight[i].store(0.f); uiWaveMaxRight[i].store(0.f); }
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
    configParam(TIME, 0.f, 1.f, 0.f, "Time / wavetable scan", "%", 0.f, 100.f);
    configParam<GrainloomPitchQuantity>(PITCH, -1.f, 1.f, 0.f, "Pitch (original interval detents)", " semitones");
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
    configButton(SEED, "Seed (hold four seconds, then release to latch)");
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
    snapshots->cancel(); adapter->reset();
    restoredFlags.store(-1); restoreInProgress = false; restoringSamples = 0;
    previousBlocks = 0; previousCalibrate = false;
    uiFrozen.store(false); uiLocked.store(true); uiSynth.store(false); uiQuality.store(3);
    persistedState.store(26);
    displayBlockCounter = liveColumn = liveSamples = 0;
    liveMinimum = liveMaximum = liveMinimumRight = liveMaximumRight = 0.f;
    uiBufferSeconds.store(4.f);
    for (int i = 0; i < displayColumns; ++i) { uiWaveMin[i].store(0.f); uiWaveMax[i].store(0.f); uiWaveMinRight[i].store(0.f); uiWaveMaxRight[i].store(0.f); }
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
    c.assignment = int(grainloom::bounded(params[ASSIGN].getValue(), 0.f, 3.f));
    for (int i = 0; i < 3; ++i) c.assignDepth[i] = params[ASSIGN_FEEDBACK_DEPTH + i].getValue();
    c.feedback = params[FEEDBACK].getValue(); c.reverb = params[REVERB].getValue(); c.mix = params[MIX].getValue();
    c.quality = int(grainloom::bounded(params[QUALITY].getValue(), 0.f, 3.f));
    c.gain = int(grainloom::bounded(params[GAIN].getValue(), 0.f, 255.f));
    c.freezeButton = params[FREEZE].getValue() > .5f; c.seedButton = params[SEED].getValue() > .5f;
    c.latchButton = params[LATCH].getValue() > .5f;
    c.seedConnected = inputs[SEED_GATE].isConnected();
    c.leftConnected = inputs[AUDIO_L].isConnected(); c.rightConnected = inputs[AUDIO_R].isConnected();
    c.gateOutput = params[GATE_MODE].getValue() > .5f;
    bool calibrate = params[CALIBRATE].getValue() > .5f;
    if (calibrate && !previousCalibrate) { params[GAIN].setValue(0.f); c.gain = 0; c.recalibrate = true; }
    previousCalibrate = calibrate;
    int restore = restoredFlags.exchange(-1);
    if (restore >= 0) {
      if (restore & 4) {
        adapter->core.configure((restore >> 3) & 3, true, true, true);
        adapter->resetBridge();
      }
      adapter->setFreeze(restore & 1); adapter->setSeedLocked(restore & 2);
    }
    bool pendingRestore = snapshots->pendingRestore();
    if (pendingRestore && ++restoringSamples >= int(args.sampleRate / 4000.f)) {
      restoringSamples = 0;
      if (snapshots->restoreTick(adapter->core)) {
        adapter->setFreeze(true);
        restoreInProgress = snapshots->pendingRestore();
        if (!restoreInProgress) adapter->resetBridge();
        outputs[OUT_L].setVoltage(0.f); outputs[OUT_R].setVoltage(0.f);
        return;
      }
    }
    if (pendingRestore || restoreInProgress) {
      outputs[OUT_L].setVoltage(0.f); outputs[OUT_R].setVoltage(0.f);
      return;
    }
    auto out = adapter->process(inputs[AUDIO_L].getVoltage() / 10.f, inputs[AUDIO_R].getVoltage() / 10.f,
      inputs[SEED_GATE].getVoltage(), inputs[FREEZE_GATE].getVoltage());
    float l = !outputs[OUT_R].isConnected() && !c.gateOutput ? (out.l + out.r) * .5f : out.l;
    outputs[OUT_L].setVoltage(10.f * l);
    outputs[OUT_R].setVoltage(c.gateOutput ? -10.f * out.r : 10.f * out.r);
    if (adapter->core.synthMode()) {
      liveMinimum = std::min(liveMinimum, out.l); liveMaximum = std::max(liveMaximum, out.l);
      liveMinimumRight = std::min(liveMinimumRight, out.r); liveMaximumRight = std::max(liveMaximumRight, out.r);
      if (++liveSamples >= std::max(1, int(args.sampleRate / 1000.f))) {
        uiWaveMin[liveColumn].store(liveMinimum); uiWaveMax[liveColumn].store(liveMaximum);
        uiWaveMinRight[liveColumn].store(liveMinimumRight); uiWaveMaxRight[liveColumn].store(liveMaximumRight);
        liveColumn = (liveColumn + 1) % displayColumns;
        liveSamples = 0; liveMinimum = liveMaximum = liveMinimumRight = liveMaximumRight = 0.f;
      }
    }
    lights[FREEZE_LIGHT].setBrightness(adapter->frozen() ? 1.f : 0.f);
    lights[SEED_LIGHT].setBrightness(c.seedButton || inputs[SEED_GATE].getVoltage() >= 1.f ? 1.f : 0.f);
    lights[LATCH_LIGHT].setBrightness(adapter->seedLocked() ? 1.f : 0.f);
    if (adapter->renderedBlocks() != previousBlocks) {
      previousBlocks = adapter->renderedBlocks();
      bool frozen = adapter->blockFrozen();
      if (!frozen) uiFrozen.store(false);
      if (params[SAVE_BUFFER].getValue() > .5f) snapshots->tick(adapter->core, frozen);
      else snapshots->cancel();
      if (frozen) uiFrozen.store(true);
      uiLocked.store(adapter->seedLocked());
      uiSynth.store(adapter->core.synthMode()); uiQuality.store(adapter->core.media());
      uiPosition.store(adapter->core.writePointer() / float(adapter->core.maxFrames()));
      uiMeter.store(adapter->core.inputMeter()); uiGain.store(adapter->core.gainAdjusting());
      uiBufferSeconds.store(adapter->core.maxFrames() / float(adapter->core.media() == 1 ? 24000 : adapter->core.rate()));
      if (!adapter->core.synthMode() && ++displayBlockCounter >= 132) {
        displayBlockCounter = 0;
        int frames = adapter->core.maxFrames();
        for (int column = 0; column < displayColumns; ++column) {
          float low = 0.f, high = 0.f, lowRight = 0.f, highRight = 0.f;
          for (int point = 0; point < 16; ++point) {
            int frame = int((int64_t(column * 16 + point) * frames) / (displayColumns * 16));
            auto sample = adapter->core.displaySample(frame);
            low = std::min(low, sample.l); high = std::max(high, sample.l);
            lowRight = std::min(lowRight, sample.r); highRight = std::max(highRight, sample.r);
          }
          uiWaveMin[column].store(grainloom::bounded(low, -1.f, 1.f));
          uiWaveMax[column].store(grainloom::bounded(high, -1.f, 1.f));
          uiWaveMinRight[column].store(grainloom::bounded(lowRight, -1.f, 1.f));
          uiWaveMaxRight[column].store(grainloom::bounded(highRight, -1.f, 1.f));
        }
      }
      persistedState.store((frozen ? 1u : 0u) | (adapter->seedLocked() ? 2u : 0u)
        | (adapter->core.synthMode() ? 4u : 0u) | (uint32_t(adapter->core.media()) << 3), std::memory_order_release);
    }
  }
  json_t* dataToJson() override {
    json_t* root = json_object();
    uint32_t state = persistedState.load(std::memory_order_acquire);
    json_object_set_new(root, "version", json_integer(1));
    json_object_set_new(root, "freeze", json_boolean(state & 1));
    json_object_set_new(root, "seedLocked", json_boolean(state & 2));
    json_object_set_new(root, "synthMode", json_boolean(state & 4));
    json_object_set_new(root, "effectiveMedia", json_integer((state >> 3) & 3));
    if ((state & 1) && !(state & 4) && params[SAVE_BUFFER].getValue() > .5f) {
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
    if (json_is_true(json_object_get(root, "synthMode"))) {
      int effectiveMedia = int(json_integer_value(json_object_get(root, "effectiveMedia")));
      flags |= 4 | (std::max(0, std::min(3, effectiveMedia)) << 3);
    }
    json_t* audio = json_object_get(root, "audio");
    const char* format = json_string_value(json_object_get(audio, "format"));
    json_int_t mediaValue = json_integer_value(json_object_get(audio, "media"));
    json_int_t writeValue = json_integer_value(json_object_get(audio, "write"));
    int media = mediaValue >= 0 && mediaValue < 4 ? int(mediaValue) : -1;
    int write = writeValue >= 0 && writeValue < 768000 ? int(writeValue) : -1;
    bool mono = json_is_true(json_object_get(audio, "mono"));
    int maxFrames[4] = {384000, 256000, 256000, 192000};
    if (format && std::strcmp(format, "beads-le-v1") == 0 && media >= 0 && media < 4 && write >= 0 && write < maxFrames[media] * (mono ? 2 : 1)) {
      auto* slot = snapshots->reserveRestore();
      if (slot) {
        json_t* bytes = json_object_get(audio, "bytes");
        bool valid = grainloom::decode(json_string_value(bytes), json_string_length(bytes), slot->bytes.data(), slot->bytes.size());
        valid = valid && grainloom::crc32(slot->bytes.data(), slot->bytes.size()) == uint32_t(json_integer_value(json_object_get(audio, "crc32")));
        if (valid) {
          // The packed audio formats accept every bit pattern, but the original
          // freeze-tail area contains IEEE floats and must reject poisoned data.
          for (size_t offset = slot->bytes.size() - 2048; offset < slot->bytes.size(); offset += 4) {
            float sample;
            std::memcpy(&sample, slot->bytes.data() + offset, sizeof(sample));
            if (!std::isfinite(sample)) { valid = false; break; }
          }
        }
        if (valid) {
          slot->media = media; slot->mono = mono; slot->write = write;
          // Keep the stored knob selection: a quality change made while frozen
          // remains pending until unfreeze, as in the firmware.
          snapshots->queueRestore(slot);
          flags = (flags & 7) | 1 | (slot->media << 3);
        } else slot->state.store(grainloom::SnapshotBank::FREE, std::memory_order_release);
      }
    }
    restoredFlags.store(flags);
    persistedState.store(uint32_t(flags), std::memory_order_release);
  }
};

struct GrainloomLargeKnob : SvgKnob {
  GrainloomLargeKnob() { setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/grainloom-large-knob.svg"))); minAngle = -.83f * M_PI; maxAngle = .83f * M_PI; shadow->opacity = .28f; }
};
struct GrainloomMediumKnob : SvgKnob {
  GrainloomMediumKnob() { setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/grainloom-medium-knob.svg"))); minAngle = -.83f * M_PI; maxAngle = .83f * M_PI; shadow->opacity = .28f; }
};
struct GrainloomSmallKnob : SvgKnob {
  GrainloomSmallKnob() { setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/grainloom-small-knob.svg"))); minAngle = -.83f * M_PI; maxAngle = .83f * M_PI; shadow->opacity = .2f; }
};
struct GrainloomButton : SvgSwitch {
  GrainloomButton() { momentary = true; auto svg = Svg::load(asset::plugin(pluginInstance, "res/components/grainloom-button.svg")); addFrame(svg); addFrame(svg); }
};
struct GrainloomScrew : SvgScrew {
  GrainloomScrew() { setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/screw.svg"))); }
};
struct GrainloomCyanLight : GrayModuleLightWidget {
  GrainloomCyanLight() { addBaseColor(nvgRGB(45, 218, 232)); }
};
struct GrainloomOrangeLight : GrayModuleLightWidget {
  GrainloomOrangeLight() { addBaseColor(nvgRGB(245, 133, 37)); }
};
struct GrainloomPanel : TransparentWidget {
  Grainloom* module = nullptr;
  void label(NVGcontext* vg, int font, const char* text, float x, float y, float size, NVGcolor color, float spacing = .12f) {
    nvgFontFaceId(vg, font); nvgFontSize(vg, mm2px(size));
    nvgTextLetterSpacing(vg, mm2px(spacing)); nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, color); nvgText(vg, mm2px(x), mm2px(y), text, nullptr);
  }
  void ticks(NVGcontext* vg, float x, float y, float radius, int parameter) {
    float value = module ? module->getParamQuantity(parameter)->getScaledValue() : -1.f;
    for (int i = 0; i <= 28; ++i) {
      float angle = -.83f * M_PI + i * (1.66f * M_PI / 28);
      nvgBeginPath(vg); nvgCircle(vg, mm2px(x + std::sin(angle) * radius), mm2px(y - std::cos(angle) * radius), mm2px(.19f));
      nvgFillColor(vg, i / 28.f <= value ? nvgRGB(65,220,233) : nvgRGB(145,132,109)); nvgFill(vg);
    }
  }
  void draw(const DrawArgs& args) override {
    auto font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
    auto title = APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
    if (!font || !title) return;
    auto vg = args.vg; nvgSave(vg);
    NVGcolor white = nvgRGB(232,230,219), metal = nvgRGB(181,163,131);
    label(vg,font->handle,"halfagiraf",71.12f,3.9f,2.f,metal,.7f);
    label(vg,title->handle,"GRAINLOOM",71.12f,9.f,6.5f,white,.65f);
    label(vg,font->handle,"QUALITY",128,3.4f,1.9f,metal);
    const char* buttons[3] = {"FREEZE","SEED","LATCH"};
    for(int i=0;i<3;++i) label(vg,font->handle,buttons[i],14+17*i,28,2.2f,white);
    const char* grains[4] = {"TIME","PITCH","SIZE","SHAPE"};
    const char* texture[4] = {"DENSITY","FEEDBACK","REVERB","DRY / WET"};
    const int tp[4] = {Grainloom::DENSITY,Grainloom::FEEDBACK,Grainloom::REVERB,Grainloom::MIX};
    for(int i=0;i<4;++i) {
      float x=18+34*i;
      label(vg,font->handle,grains[i],x,39.4f,2.7f,white);
      ticks(vg,x,52,9.65f,Grainloom::TIME+i);
      label(vg,font->handle,"RANDOM / CV",x,62.5f,1.95f,metal);
      label(vg,font->handle,texture[i],x,77.4f,2.3f,white);
      ticks(vg,x,87,7.75f,tp[i]);
    }
    const char* utility[4]={"DEPTH","ASSIGN","GAIN","AUTO"};
    const float ux[4]={48,75,103,128};
    for(int i=0;i<4;++i) label(vg,font->handle,utility[i],ux[i],98.6f,2.f,metal);
    label(vg,font->handle,"AUDIO IN",14.25f,109.4f,1.8f,metal);
    label(vg,font->handle,"MODULATION / CONTROL",72,109.4f,1.8f,metal);
    label(vg,font->handle,"AUDIO OUT",129,109.4f,1.8f,metal);
    const char* ports[12]={"IN L","IN R","FREEZE","SEED","DENSITY","TIME","PITCH","SIZE","SHAPE","ASSIGN","OUT L","OUT R"};
    for(int i=0;i<12;++i) label(vg,font->handle,ports[i],8+11.5f*i,113.3f,2.2f,white,.02f);
    nvgRestore(vg);
  }
  void drawLayer(const DrawArgs& args,int layer) override {
    if(layer!=1) return;
    auto font=APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf")); if(!font)return;
    auto vg=args.vg; nvgSave(vg);
    NVGcolor cyan=nvgRGB(67,226,239);
    bool frozen=module&&module->uiFrozen.load(), synth=module&&module->uiSynth.load();
    int quality=module?std::max(0,std::min(3,module->uiQuality.load())):3;
    const char* names[4]={"CASSETTE","TAPE","VINTAGE","DIGITAL"};
    label(vg,font->handle,synth?"WAVETABLE":frozen?"FROZEN":"RECORDING",71,18.7f,2.2f,cyan);
    label(vg,font->handle,names[quality],126,18.7f,2.2f,cyan);
    if(module) {
      char duration[32]; std::snprintf(duration,sizeof(duration),"%.1fs",module->uiBufferSeconds.load());
      label(vg,font->handle,duration,102,18.7f,1.8f,cyan);
    }
    for(int channel=0;channel<2;++channel) {
      float center=channel?31.2f:24.7f;
      label(vg,font->handle,channel?"R":"L",60,center,1.7f,cyan);
      nvgScissor(vg,mm2px(62),mm2px(center-2.7f),mm2px(69),mm2px(5.4f));
      nvgBeginPath(vg);
      for(int i=0;i<Grainloom::displayColumns;++i) {
        float low=module?(channel?module->uiWaveMinRight[i].load():module->uiWaveMin[i].load()):0;
        float high=module?(channel?module->uiWaveMaxRight[i].load():module->uiWaveMax[i].load()):0;
        nvgRect(vg,mm2px(62+i*69.f/Grainloom::displayColumns),mm2px(center-high*2.6f),mm2px(.35f),mm2px(std::max(.06f,(high-low)*2.6f)));
      }
      nvgFillColor(vg,cyan);nvgFill(vg); nvgResetScissor(vg);
    }
    if(module&&!synth) {
      float x=62+grainloom::bounded(module->uiPosition.load(),0.f,1.f)*69;
      nvgBeginPath(vg);nvgMoveTo(vg,mm2px(x),mm2px(22));nvgLineTo(vg,mm2px(x),mm2px(34));
      nvgStrokeColor(vg,cyan);nvgStrokeWidth(vg,mm2px(.25f));nvgStroke(vg);
    }
    float meter=module?grainloom::bounded(module->uiMeter.load(),0.f,1.f):0;
    for(int i=0;i<16;++i) {
      nvgBeginPath(vg);nvgRect(vg,mm2px(133),mm2px(33.8f-i*.7f),mm2px(1.3f),mm2px(.5f));
      nvgFillColor(vg,i/16.f<meter?cyan:nvgRGB(17,49,54));nvgFill(vg);
    }
    if(module&&module->uiGain.load()) label(vg,font->handle,"LEARN",103,106.9f,1.3f,cyan);
    nvgRestore(vg);
  }
};

struct GrainloomWidget : ModuleWidget {
  GrainloomWidget(Grainloom* module) {
    setModule(module);setPanel(createPanel(asset::plugin(pluginInstance,"res/Grainloom.svg")));
    auto* panel=createWidget<GrainloomPanel>(Vec());panel->box.size=box.size;panel->module=module;addChild(panel);
    for(float x:{5.f,132.2f})for(float y:{0.f,123.5f})addChild(createWidget<GrainloomScrew>(mm2px(Vec(x,y))));
    for(int i=0;i<4;++i) {
      float x=18+34*i;
      addParam(createParamCentered<GrainloomLargeKnob>(mm2px(Vec(x,52)),module,Grainloom::TIME+i));
      addParam(createParamCentered<GrainloomSmallKnob>(mm2px(Vec(x,67.4f)),module,Grainloom::TIME_RANDOM+i));
    }
    const int texture[4]={Grainloom::DENSITY,Grainloom::FEEDBACK,Grainloom::REVERB,Grainloom::MIX};
    for(int i=0;i<4;++i)addParam(createParamCentered<GrainloomMediumKnob>(mm2px(Vec(18+34*i,87)),module,texture[i]));
    const int buttons[3]={Grainloom::FREEZE,Grainloom::SEED,Grainloom::LATCH};
    for(int i=0;i<3;++i)addParam(createParamCentered<GrainloomButton>(mm2px(Vec(14+17*i,21.5f)),module,buttons[i]));
    addChild(createLightCentered<SmallLight<GrainloomCyanLight>>(mm2px(Vec(14,15.8f)),module,Grainloom::FREEZE_LIGHT));
    addChild(createLightCentered<SmallLight<WhiteLight>>(mm2px(Vec(31,15.8f)),module,Grainloom::SEED_LIGHT));
    addChild(createLightCentered<SmallLight<GrainloomOrangeLight>>(mm2px(Vec(48,15.8f)),module,Grainloom::LATCH_LIGHT));
    addParam(createParamCentered<GrainloomSmallKnob>(mm2px(Vec(128,9.4f)),module,Grainloom::QUALITY));
    const int utility[3]={Grainloom::DEPTH,Grainloom::ASSIGN,Grainloom::GAIN};
    const float ux[3]={48,75,103};
    for(int i=0;i<3;++i)addParam(createParamCentered<GrainloomSmallKnob>(mm2px(Vec(ux[i],103.5f)),module,utility[i]));
    addParam(createParamCentered<GrainloomButton>(mm2px(Vec(128,103.5f)),module,Grainloom::CALIBRATE));
    const int inputs[10]={Grainloom::AUDIO_L,Grainloom::AUDIO_R,Grainloom::FREEZE_GATE,Grainloom::SEED_GATE,Grainloom::DENSITY_CV,Grainloom::TIME_CV,Grainloom::PITCH_CV,Grainloom::SIZE_CV,Grainloom::SHAPE_CV,Grainloom::ASSIGN_CV};
    for(int i=0;i<10;++i)addInput(createInputCentered<PJ301MPort>(mm2px(Vec(8+11.5f*i,119.3f)),module,inputs[i]));
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(123,119.3f)),module,Grainloom::OUT_L));
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(134.5f,119.3f)),module,Grainloom::OUT_R));
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
