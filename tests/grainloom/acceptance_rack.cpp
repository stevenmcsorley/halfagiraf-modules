#include <engine/Engine.hpp>
#include <context.hpp>
#undef PRIVATE
#include "../../src/Grainloom.cpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <vector>
Plugin* pluginInstance = nullptr;

struct Source : Module {
  double phase = 0.;
  Source() { config(0, 0, 3, 0); }
  void process(const ProcessArgs& a) override {
    phase += a.sampleTime;
    outputs[0].setVoltage(2.f * std::sin(phase * 691.15038379));
    outputs[1].setVoltage(1.5f * std::sin(phase * 1035.1));
    outputs[2].setVoltage(std::fmod(phase, .25) < .005 ? 10.f : 0.f);
  }
};
struct Sink : Module {
  double energy = 0.; float peak = 0.; uint64_t subnormals = 0;
  Sink() { config(0, 2, 0, 0); }
  void process(const ProcessArgs&) override {
    for (auto& input : inputs) {
      float v = input.getVoltage(); assert(std::isfinite(v)); assert(std::fabs(v) <= 10.001f);
      energy += v * v; peak = std::max(peak, std::fabs(v));
      subnormals += std::fpclassify(v) == FP_SUBNORMAL;
    }
  }
};
int main() {
  std::cout << std::unitbuf;
  settings::threadCount = 1;
  random::init();
  Context context; context.engine = new engine::Engine; contextSet(&context);
  auto* engine = context.engine;
  Plugin metadata; metadata.slug = "halfagiraf"; metadata.version = "2.1.0";
  metadata.addModel(modelGrainloom);
  plugin::plugins.push_back(&metadata);
  // Full Rack Module JSON, rather than only the custom data subtree.
  std::unique_ptr<Grainloom> original(static_cast<Grainloom*>(modelGrainloom->createModule()));
  original->params[Grainloom::QUALITY].setValue(2.f);
  original->params[Grainloom::DEPTH].setValue(.37f);
  original->params[Grainloom::SAVE_BUFFER].setValue(1.f);
  original->inputs[Grainloom::AUDIO_L].channels = 1;
  Module::ProcessArgs args{}; args.sampleRate = 48000; args.sampleTime = 1.f / 48000;
  for (int i = 0; i < 48000; ++i) {
    original->inputs[Grainloom::AUDIO_L].setVoltage(2.f * std::sin(i * .03f)); original->process(args);
  }
  original->adapter->setFreeze(true);
  for (int i = 0; i < 128; ++i) original->process(args);
  json_t* saved = original->toJson();
  assert(json_object_get(json_object_get(saved, "data"), "audio"));
  std::unique_ptr<Grainloom> duplicate(static_cast<Grainloom*>(modelGrainloom->createModule()));
  duplicate->fromJson(saved);
  for (int i = 0; i < 4800; ++i) duplicate->process(args);
  assert(duplicate->params[Grainloom::DEPTH].getValue() == .37f);
  assert(duplicate->adapter->core.media() == 2 && duplicate->adapter->core.mono());
  assert(duplicate->adapter->frozen());
  assert(std::memcmp(original->adapter->core.buffer(), duplicate->adapter->core.buffer(), grainloom::Core::bufferBytes - 2048) == 0);
  json_t* poisoned = json_deep_copy(json_object_get(saved, "data"));
  json_object_set_new(json_object_get(poisoned, "audio"), "write", json_integer((json_int_t(1) << 32) + 17));
  duplicate->onReset(Module::ResetEvent{}); duplicate->dataFromJson(poisoned);
  assert(!duplicate->snapshots->pendingRestore());
  json_decref(poisoned); json_decref(saved);
  std::cout << "Full JSON duplication and invalid-pointer rejection pass\n";
  // Either audio jack must normalize to the same mono recording.
  std::unique_ptr<Grainloom> monoLeft(new Grainloom), monoRight(new Grainloom);
  monoLeft->inputs[Grainloom::AUDIO_L].channels = 1;
  monoRight->inputs[Grainloom::AUDIO_R].channels = 1;
  for(auto* m:{monoLeft.get(),monoRight.get()}) {
    m->outputs[Grainloom::OUT_L].channels=1;
    m->outputs[Grainloom::OUT_R].channels=1;
    m->params[Grainloom::GAIN].setValue(64.f);
  }
  for(int i=0;i<48000;++i) {
    float v=2.f*std::sin(i*.037f);
    monoLeft->inputs[Grainloom::AUDIO_L].setVoltage(v);
    monoRight->inputs[Grainloom::AUDIO_R].setVoltage(v);
    monoLeft->process(args); monoRight->process(args);
    assert(monoLeft->outputs[Grainloom::OUT_L].getVoltage()==monoRight->outputs[Grainloom::OUT_L].getVoltage());
    assert(monoLeft->outputs[Grainloom::OUT_R].getVoltage()==monoRight->outputs[Grainloom::OUT_R].getVoltage());
  }
  assert(monoLeft->adapter->core.mono() && monoRight->adapter->core.mono());
  assert(std::memcmp(monoLeft->adapter->core.buffer(),monoRight->adapter->core.buffer(),grainloom::Core::bufferBytes)==0);
  monoLeft->outputs[Grainloom::OUT_R].channels=0;
  for(int i=0;i<1000;++i) {
    monoLeft->process(args);monoRight->process(args);
    float fold=(monoRight->outputs[Grainloom::OUT_L].getVoltage()+monoRight->outputs[Grainloom::OUT_R].getVoltage())*.5f;
    assert(std::fabs(monoLeft->outputs[Grainloom::OUT_L].getVoltage()-fold)<.00001f);
  }
  std::cout << "Left/right mono normalization and unpatched-right fold-down pass\n";
  // Actual Rack Engine cables, real module processing, four simultaneous effects.
  Source source; engine->addModule(&source);
  std::vector<std::unique_ptr<Grainloom>> modules;
  std::vector<std::unique_ptr<Sink>> sinks;
  std::vector<std::unique_ptr<engine::Cable>> cables;
  auto connect = [&](Module* from, int output, Module* to, int input) {
    std::unique_ptr<engine::Cable> cable(new engine::Cable);
    cable->outputModule = from; cable->outputId = output; cable->inputModule = to; cable->inputId = input;
    engine->addCable(cable.get()); cables.push_back(std::move(cable));
  };
  for (int i = 0; i < 4; ++i) {
    modules.emplace_back(static_cast<Grainloom*>(modelGrainloom->createModule()));
    auto* module = modules.back().get(); engine->addModule(module);
    module->params[Grainloom::QUALITY].setValue(float(i));
    module->params[Grainloom::GAIN].setValue(64.f);
    module->params[Grainloom::FEEDBACK].setValue(.95f); module->params[Grainloom::REVERB].setValue(.85f);
    module->params[Grainloom::DENSITY].setValue(.92f);
    connect(&source, 0, module, Grainloom::AUDIO_L); connect(&source, 1, module, Grainloom::AUDIO_R);
    connect(&source, 2, module, Grainloom::SEED_GATE);
    sinks.emplace_back(new Sink); engine->addModule(sinks.back().get());
    connect(module, Grainloom::OUT_L, sinks.back().get(), 0); connect(module, Grainloom::OUT_R, sinks.back().get(), 1);
  }
  for (float rate : {44100.f, 48000.f, 88200.f, 96000.f, 192000.f}) {
    engine->setSampleRate(rate);
    std::vector<double> timing; timing.reserve(int(rate * 4 / 256) + 1);
    auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < int(rate * 4); frame += 256) {
      auto begin = std::chrono::steady_clock::now(); engine->stepBlock(256);
      timing.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count());
    }
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::sort(timing.begin(), timing.end());
    std::cout << "Rack engine rate=" << rate << " effects=4 audio=4s elapsed=" << seconds
      << "s p99_block=" << timing[size_t(timing.size() * .99)] << "us max_block=" << timing.back() << "us\n";
    for (auto& module : modules) {
      module->adapter->setFreeze(true);
      engine->stepBlock(256);
      assert(module->adapter->frozen() && module->uiFrozen.load());
    }
  }
  for (auto& sink : sinks) { assert(sink->energy > 1.); std::cout << "Sink peak=" << sink->peak << "V output_subnormals=" << sink->subnormals << "\n"; }
  for (auto& cable : cables) engine->removeCable(cable.get());
  for (auto& sink : sinks) engine->removeModule(sink.get());
  for (auto& module : modules) engine->removeModule(module.get());
  engine->removeModule(&source);
  std::cout << "Full JSON duplication, invalid-pointer rejection, wired clock/stereo/feedback and rate-change integration pass\n";
  std::cout << "Persistent bytes=" << sizeof(grainloom::Adapter) + sizeof(grainloom::SnapshotBank)
    << " module_object_bytes=" << sizeof(Grainloom) << "\n";
  plugin::plugins.clear();
  metadata.models.clear();
  modelGrainloom->plugin = nullptr;
}
