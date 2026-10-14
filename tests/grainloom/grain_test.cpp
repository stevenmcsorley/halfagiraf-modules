#include "../../src/GrainloomCore.hpp"
#include <cassert>
#include <iostream>
#include <memory>

static void recordingBoundaries() {
  std::unique_ptr<grainloom::Core> core(new grainloom::Core());
  std::unique_ptr<uint8_t[]> frozenBytes(new uint8_t[grainloom::Core::bufferBytes]);
  for (int media = 0; media < 4; ++media) {
    for (bool mono : {false, true}) {
      core->configure(media, mono, false, true);
      core->density.base = .5f; core->seedState = beads::SEED_BUTTON_STATE_OFF;
      core->mix.dry_wet = 0.f;
      int n = core->blockSize(), ratio = media == 1 ? 2 : 1;
      int recorded = n / ratio, capacity = core->maxFrames();
      beads::FloatFrame input[12] = {}, output[12] = {};
      bool seed[12] = {}, freeze[12] = {};
      for (int i = 0; i < n; ++i) { input[i].l = .12f; input[i].r = -.17f; }
      int pointer = core->writePointer();
      for (int block = 0; block < 2 * capacity / recorded + 10; ++block) {
        core->process(input, seed, freeze, output);
        pointer = (pointer + capacity - recorded) % capacity;
        assert(core->writePointer() == pointer);
      }
      std::memcpy(frozenBytes.get(), core->buffer(), grainloom::Core::bufferBytes);
      for (int i = 0; i < n; ++i) freeze[i] = true;
      for (int block = 0; block < 1000; ++block) {
        core->process(input, seed, freeze, output);
        assert(core->writePointer() == pointer);
      }
      // The intentional float freeze tail changes, but the recording does not.
      assert(std::memcmp(frozenBytes.get(), core->buffer(), grainloom::Core::bufferBytes - 2048) == 0);
    }
  }
  std::cout << "All eight media/channel buffers wrap at their original capacity and remain unchanged when frozen\n";
}

int main() {
  recordingBoundaries();
  using Line = beads::DelayLine<beads::RESOLUTION_16_BIT, 8192, 1, 65536, 1>;
  std::array<uint8_t, 65536> memory{};
  Line line; uint8_t* banks[] = {memory.data()}; line.Init(banks);
  for (int i = 0; i < 8192; ++i) {
    beads::FloatFrame frame{}; frame.l = frame.r = (i - 4096) / 10240.f;
    line.Write(frame, false);
  }
  beads::FloatFrame silence{}; line.Write(silence, true);
  for (float speed : {-1.f, 1.f, 2.f}) {
    beads::DelayGrain grain; grain.Init(48000.f);
    beads::GrainEnvelope envelope; envelope.Init(48000.f); envelope.Start(1000.f, .5f, 0.f);
    grain.Start(&line, 2048.f, speed, 1000.f, .5f, 1.f, 0.f, 0.f);
    assert(grain.remaining_samples() == 1000);
    for (int i = 0; i < 1000; ++i) {
      float delay = 2048.f - speed * (i + 1);
      while (delay < 0.f) delay += 8192.f;
      auto expected = line.Read<beads::INTERPOLATION_HERMITE>(delay);
      float gain = envelope.Render();
      beads::FloatFrame actual{};
      grain.Render<beads::HEAD_CROSSING_POLICY_WRAP, false>(&line, &actual);
      assert(std::fabs(actual.l - expected.l * gain) < 1e-6f);
      assert(actual.r == 0.f);
    }
  }
  beads::DelayGranularPlayer player; player.Init(48000.f);
  beads::Parameters params{}; params.size.base = .85f; params.time.base = .5f; params.shape.base = .5f;
  beads::TriggerGenerator::Output trigger{}; trigger.trigger = true; trigger.regularity = 1.f;
  int peakVoices = 0;
  for (int i = 0; i < 10000; ++i) {
    beads::FloatFrame out{};
    player.Render(&line, trigger, params, &out);
    peakVoices = std::max(peakVoices, player.CountActiveGrains());
    assert(player.CountActiveGrains() <= 32);
    assert(std::isfinite(out.l) && std::isfinite(out.r));
  }
  assert(peakVoices > 10);
  assert(std::fabs(player.ComputeGrainDuration(.999f) - 4.f * 48000.f) < 2000.f);
  std::cout << "Original forward/reverse/double-speed interpolation and envelopes match; voice stealing bounded at " << peakVoices << " voices\n";
}
