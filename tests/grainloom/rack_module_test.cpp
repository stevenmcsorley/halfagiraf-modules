// Host harness only: instantiate the Engine that Rack normally supplies.
// The shipped plugin uses rack.hpp's public API exclusively.
#include <engine/Engine.hpp>
#include <context.hpp>
#undef PRIVATE
#include "../../src/Grainloom.cpp"
#include <cassert>
#include <iostream>
Plugin* pluginInstance = nullptr;
int main() {
  rack::Context context;
  context.engine = new rack::engine::Engine;
  rack::contextSet(&context);
  std::unique_ptr<Grainloom> a(new Grainloom()), b(new Grainloom());
  Module::ProcessArgs args{}; args.sampleRate = 48000.f; args.sampleTime = 1.f / 48000.f;
  a->inputs[Grainloom::AUDIO_L].channels = 1;
  a->inputs[Grainloom::AUDIO_R].channels = 1;
  a->outputs[Grainloom::OUT_L].channels = 1;
  a->outputs[Grainloom::OUT_R].channels = 1;
  for (int i = 0; i < 48000; ++i) {
    a->inputs[Grainloom::AUDIO_L].setVoltage(3.f * std::sin(i * .031f));
    a->inputs[Grainloom::AUDIO_R].setVoltage(2.f * std::sin(i * .039f));
    a->process(args);
  }
  a->params[Grainloom::FREEZE].setValue(1.f); a->process(args);
  a->params[Grainloom::FREEZE].setValue(0.f);
  for (int i = 0; i < 24; ++i) a->process(args);
  assert(a->uiFrozen.load());
  json_t* saved = a->dataToJson();
  assert(json_object_get(saved, "audio"));
  b->dataFromJson(saved);
  // Saving before the first engine callback must retain the queued recording.
  json_t* queuedSave = b->dataToJson();
  assert(json_object_get(queuedSave, "audio"));
  assert(json_equal(json_object_get(saved, "audio"), json_object_get(queuedSave, "audio")));
  json_decref(queuedSave);
  // A serializer can also pin the immutable payload during chunked restoration.
  for (int i = 0; i < 120; ++i) b->process(args);
  queuedSave = b->dataToJson();
  assert(json_equal(json_object_get(saved, "audio"), json_object_get(queuedSave, "audio")));
  json_decref(queuedSave);
  for (int i = 0; i < 24000; ++i) b->process(args);
  assert(b->adapter->frozen());
  assert(std::memcmp(a->adapter->core.buffer(), b->adapter->core.buffer(), grainloom::Core::bufferBytes - 2048) == 0);
  assert(a->adapter->core.writePointer() == b->adapter->core.writePointer());
  json_decref(saved);
  b->onReset(Module::ResetEvent{});
  assert(!b->adapter->frozen() && b->adapter->seedLocked());
  assert(b->params[Grainloom::QUALITY].getValue() == 3.f);
  for (int i = 0; i < 1000; ++i) b->process(args);
  assert(!b->adapter->frozen());
  json_t* invalid = a->dataToJson();
  json_t* invalidAudio = json_object_get(invalid, "audio");
  assert(invalidAudio);
  json_object_set_new(invalidAudio, "crc32", json_integer(json_integer_value(json_object_get(invalidAudio, "crc32")) ^ 1));
  b->dataFromJson(invalid);
  for (auto& slot : b->snapshots->slots) assert(slot.state.load() != grainloom::SnapshotBank::RESTORE);
  json_decref(invalid);
  a->params[Grainloom::GATE_MODE].setValue(1.f);
  a->params[Grainloom::DENSITY].setValue(.5f);
  a->adapter->setSeedLocked(false);
  a->inputs[Grainloom::SEED_GATE].channels = 1;
  float gatePeak = 0.f;
  for (int i = 0; i < 48000; ++i) {
    a->inputs[Grainloom::SEED_GATE].setVoltage(i % 12000 < 240 ? 10.f : 0.f);
    a->process(args);
    gatePeak = std::max(gatePeak, a->outputs[Grainloom::OUT_R].getVoltage());
  }
  assert(std::fabs(gatePeak - 8.f) < .001f);
  b->onReset(Module::ResetEvent{});
  b->adapter->core.configure(2, true, true, true);
  b->params[Grainloom::QUALITY].setValue(2.f);
  b->adapter->resetBridge(); b->adapter->setFreeze(true);
  for (int i = 0; i < 48; ++i) b->process(args);
  assert(b->uiSynth.load());
  json_t* synthState = b->dataToJson();
  a->onReset(Module::ResetEvent{});
  a->params[Grainloom::QUALITY].setValue(2.f);
  a->dataFromJson(synthState);
  for (int i = 0; i < 48; ++i) a->process(args);
  assert(a->adapter->core.synthMode() && a->adapter->frozen());
  assert(a->adapter->core.media() == 2);
  json_decref(synthState);
  a->params[Grainloom::GAIN].setValue(255.f);
  a->params[Grainloom::FEEDBACK].setValue(1.f);
  for (int i = 0; i < 48000; ++i) {
    a->process(args);
    assert(std::isfinite(a->outputs[Grainloom::OUT_L].getVoltage()));
    assert(std::fabs(a->outputs[Grainloom::OUT_L].getVoltage()) <= 10.f);
  }
  std::cout << "Rack module creation, audio, freeze/JSON reload and bounded feedback pass\n";
}
