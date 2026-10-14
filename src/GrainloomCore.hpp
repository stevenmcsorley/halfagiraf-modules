#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include "beads/dsp/processor.h"
#include "beads/dsp/synth.h"
#include "beads/gain_control.h"

namespace grainloom {
static_assert(sizeof(beads::Item<beads::RESOLUTION_12_BIT, 2>) == 3, "Beads requires packed 3-byte stereo frames");
static_assert(sizeof(beads::Item<beads::RESOLUTION_12_BIT, 1>) == 3, "Beads requires packed 3-byte mono bundles");
inline float bounded(float v, float low, float high) {
  return std::isfinite(v) ? std::max(low, std::min(high, v)) : 0.f;
}

// Firmware processing domain. Nothing in this class depends on Rack.
class Core {
public:
  static constexpr int bankBytes = 262144;
  static constexpr int bufferBytes = bankBytes * 3;
  static constexpr int maxBlock = 12;
  beads::Parameters parameters{};
  beads::ModulatedParameter density{};
  beads::Processor::MixParameters mix{};
  beads::SeedButtonState seedState = beads::SEED_BUTTON_STATE_LOCKED;
  bool seedConnected = false;
  bool leftConnected = true;
  bool rightConnected = true;
  uint8_t gainSetting = 0;
  uint32_t rng = 0x21;

  Core() { reset(); }
  void reset() {
    // Firmware objects live in zero-initialized BSS and depend on that state.
    std::memset(&processor_, 0, sizeof(processor_));
    std::memset(&synth_, 0, sizeof(synth_));
    std::memset(&gain_, 0, sizeof(gain_));
    uint8_t* banks[3] = {buffer_.data(), buffer_.data() + bankBytes, buffer_.data() + 2 * bankBytes};
    processor_.Init(banks, reverb_.data());
    synth_.Init(reverb_.data());
    gain_.Init(48000.f, beads::GainControl::SOFT_CLIPPING_MODE_OFF, true);
    parameters = beads::Parameters{};
    density = beads::ModulatedParameter{};
    mix = beads::Processor::MixParameters{};
    density.base = .3f;
    parameters.size.base = .65f;
    parameters.shape.base = .5f;
    mix.dry_wet = .5f;
    rng = 0x21; synthMode_ = false; gainSetting = 0;
    seedState = beads::SEED_BUTTON_STATE_LOCKED;
    seedConnected = false; leftConnected = rightConnected = true;
    previousSeed_ = stmlib::GATE_FLAG_LOW; seedDelay_.fill(false); seedIndex_ = 0;
  }
  int rate() const { return synthMode_ ? 48000 : int(processor_.sample_rate()); }
  int blockSize() const { return rate() / 4000; }
  int media() const { return int(processor_.media()); }
  bool mono() const { return processor_.mono(); }
  bool synthMode() const { return synthMode_; }
  int writePointer() { return *processor_.mutable_delay_write_ptr(); }
  int maxFrames() const {
    const int frames[4] = {384000, 256000, 256000, 192000};
    return frames[media()] * (mono() ? 2 : 1);
  }
  uint8_t* buffer() { return buffer_.data(); }
  bool gainAdjusting() const { return gain_.adjusting(); }
  float inputMeter() const { return gain_.vu_meter() / 255.f; }
  void restartGain() { gain_.Restart(); }
  void configure(int media, bool mono, bool synth, bool resetGain) {
    stmlib::Random::Seed(rng);
    processor_.Configure(beads::Processor::Media(std::max(0, std::min(3, media))), mono);
    synthMode_ = synth;
    gain_.Init(float(rate()), media == 0 ? beads::GainControl::SOFT_CLIPPING_MODE_ON
      : media == 1 ? beads::GainControl::SOFT_CLIPPING_MODE_DYNAMIC : beads::GainControl::SOFT_CLIPPING_MODE_OFF,
      resetGain);
    rng = stmlib::Random::state();
    previousSeed_ = stmlib::GATE_FLAG_LOW;
    seedDelay_.fill(false);
    seedIndex_ = 0;
  }
  void restore(int media, bool mono, int writePointer) {
    configure(media, mono, false, true);
    *processor_.mutable_delay_write_ptr() = std::max(0, std::min(maxFrames() - 1, writePointer));
  }
  // 2.5 ms firmware SEED delay; CV pots are sampled by the caller every block.
  void process(const beads::FloatFrame* input, const bool* seed, const bool* freeze,
    beads::FloatFrame* output, bool active = true) {
    const int n = blockSize();
    beads::Codec::Frame raw[maxBlock];
    beads::FloatFrame gained[maxBlock];
    stmlib::GateFlags flags[maxBlock];
    const int seedDelay = rate() / 400;
    for (int i = 0; i < n; ++i) {
      raw[i].l = int32_t(double(bounded(input[i].l, -1.f, .999999f)) * 2147483648.0);
      raw[i].r = int32_t(double(bounded(input[i].r, -1.f, .999999f)) * 2147483648.0);
      seedDelay_[seedIndex_] = seed[i];
      bool delayed = seedDelay_[(seedIndex_ + int(seedDelay_.size()) - seedDelay) % seedDelay_.size()];
      seedIndex_ = (seedIndex_ + 1) % seedDelay_.size();
      flags[i] = previousSeed_ = stmlib::ExtractGateFlags(previousSeed_, delayed);
    }
    if (!seedConnected) previousSeed_ = stmlib::GATE_FLAG_LOW;
    gain_.Process(active, gainSetting, !leftConnected, !rightConnected, raw, gained, n);
    if (!active) {
      for (int i = 0; i < n; ++i) output[i].l = output[i].r = 0.f;
      return;
    }
    stmlib::Random::Seed(rng);
    if (synthMode_) {
      synth_.Render(seedState, seedConnected ? flags : nullptr, freeze,
        beads::Synth::Resolution(media()), density, parameters, mix, output, n);
    } else {
      processor_.Process(seedState, seedConnected ? flags : nullptr, freeze,
        density, parameters, mix, gained, output, n);
    }
    rng = stmlib::Random::state();
    for (int i = 0; i < n; ++i) {
      output[i].l = bounded(output[i].l, -1.f, .999999f);
      output[i].r = bounded(output[i].r, -1.f, .999999f);
    }
  }
private:
  alignas(16) std::array<uint8_t, bufferBytes> buffer_{};
  alignas(16) std::array<uint16_t, 32768> reverb_{};
  beads::Processor processor_;
  beads::Synth synth_;
  beads::GainControl gain_;
  std::array<bool, 128> seedDelay_{};
  stmlib::GateFlags previousSeed_ = stmlib::GATE_FLAG_LOW;
  int seedIndex_ = 0;
  bool synthMode_ = false;
};
}
