#include "../../src/Grainloom.cpp"
#include <cassert>
#include <iostream>
Plugin* pluginInstance = nullptr;
int main() {
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
  for (int i = 0; i < 48000 / 2; ++i) a->process(args);
  assert(a->uiFrozen.load());
  json_t* saved = a->dataToJson();
  assert(json_object_get(saved, "audio"));
  b->dataFromJson(saved);
  for (int i = 0; i < 24000; ++i) b->process(args);
  assert(b->adapter->frozen());
  assert(std::memcmp(a->adapter->core.buffer(), b->adapter->core.buffer(), grainloom::Core::bufferBytes - 2048) == 0);
  assert(a->adapter->core.writePointer() == b->adapter->core.writePointer());
  json_decref(saved);
  a->params[Grainloom::GAIN].setValue(255.f);
  a->params[Grainloom::FEEDBACK].setValue(1.f);
  for (int i = 0; i < 48000; ++i) {
    a->process(args);
    assert(std::isfinite(a->outputs[Grainloom::OUT_L].getVoltage()));
    assert(std::fabs(a->outputs[Grainloom::OUT_L].getVoltage()) <= 10.f);
  }
  std::cout << "Rack module creation, audio, freeze/JSON reload and bounded feedback pass\n";
}
