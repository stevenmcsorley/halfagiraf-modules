#include "../../src/GrainloomAdapter.hpp"
#include <cassert>
#include <iostream>
#include <memory>

static int count(float rate, float density, beads::SeedButtonState button,
  float externalHz, float gateDuty, int seconds) {
  beads::TriggerGenerator generator; generator.Init(rate);
  stmlib::Random::Seed(1234);
  stmlib::GateFlags previous = stmlib::GATE_FLAG_LOW;
  int triggers = 0;
  for (int sample = 0; sample < int(rate * seconds); ++sample) {
    float phase = std::fmod(sample * externalHz / rate, 1.f);
    bool gate = externalHz > 0.f && phase < gateDuty;
    previous = stmlib::ExtractGateFlags(previous, gate);
    beads::TriggerGenerator::Output out;
    generator.Render(button, externalHz > 0.f ? &previous : nullptr, density, 0.f, 0.f, &out, 1);
    if (out.trigger) ++triggers;
    assert(std::isfinite(out.phase) && out.fractional_sample >= 0.f && out.fractional_sample <= 1.f);
  }
  return triggers;
}
int main() {
  for (float rate : {24000.f, 32000.f, 48000.f}) {
    int periodic = count(rate, .25f, beads::SEED_BUTTON_STATE_LOCKED, 0.f, 0.f, 10);
    assert(periodic >= 81 && periodic <= 83);
    int random = count(rate, .85f, beads::SEED_BUTTON_STATE_LOCKED, 0.f, 0.f, 100);
    assert(random > 900 && random < 1600);
    assert(count(rate, .5f, beads::SEED_BUTTON_STATE_LOCKED, 0.f, 0.f, 10) == 0);
    assert(count(rate, .5f, beads::SEED_BUTTON_STATE_OFF, 2.f, .05f, 10) == 20);
    assert(count(rate, .5f, beads::SEED_BUTTON_STATE_OFF, .05f, .05f, 10) == 1);
    int divided = count(rate, .2f, beads::SEED_BUTTON_STATE_LOCKED, 2.f, .05f, 10);
    assert(divided >= 4 && divided <= 6);
    int probability = count(rate, .8f, beads::SEED_BUTTON_STATE_LOCKED, 10.f, .05f, 100);
    assert(probability > 280 && probability < 440);
    int gated = count(rate, .25f, beads::SEED_BUTTON_STATE_OFF, .5f, .5f, 10);
    assert(gated >= 40 && gated <= 50);
    int audio = count(rate, 1.f, beads::SEED_BUTTON_STATE_LOCKED, 100.f, .5f, 10);
    assert(audio >= 990 && audio <= 1010);
    std::cout << "Triggers " << rate << ": periodic=" << periodic << " random=" << random
      << " division=" << divided << " probability=" << probability << " gated=" << gated << " audio=" << audio << "\n";
  }
  std::unique_ptr<grainloom::Adapter> a(new grainloom::Adapter());
  a->controls.leftConnected = a->controls.rightConnected = true;
  a->controls.seedButton = true;
  a->process(0, 0, 0, 0); assert(!a->seedLocked());
  for (int i = 0; i < 73000; ++i) a->process(0, 0, 0, 0);
  assert(a->seedLocked());
  a->controls.seedButton = false; a->process(0, 0, 0, 0);
  a->controls.seedButton = true; a->process(0, 0, 0, 0); assert(!a->seedLocked());
  a->controls.freezeButton = true; a->process(0, 0, 0, 0); assert(a->seedLocked());
  a->controls.seedButton = a->controls.freezeButton = false; a->process(0, 0, 0, 0);
  a->controls.freezeButton = true; a->process(0, 0, 0, 0); assert(a->frozen());
  int media = a->core.media(); a->controls.quality = 0;
  for (int i = 0; i < 2400; ++i) a->process(0, 0, 0, 0);
  assert(a->core.media() == media);
  std::cout << "SEED hold, latch combination and frozen quality restrictions pass\n";
}
