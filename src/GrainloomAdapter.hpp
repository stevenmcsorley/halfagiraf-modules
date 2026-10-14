#pragma once
#include "GrainloomCore.hpp"
#include "GrainloomResampler.hpp"

namespace grainloom {
struct Controls {
  float time = 0.f, pitch = 0.f, size = .65f, shape = .5f, density = .3f;
  float random[4] = {};
  float cv[4] = {};
  bool connected[4] = {};
  float densityCv = 0.f;
  float feedback = 0.f, mix = .5f, reverb = 0.f;
  float assignCv = 0.f;
  float assignDepth[3] = {1.f, 0.f, .5f};
  int assignment = 0;
  int quality = 3;
  int gain = 0;
  bool freezeButton = false, seedButton = false, latchButton = false;
  bool seedConnected = false, leftConnected = false, rightConnected = false;
  bool gateOutput = false;
  bool recalibrate = false;
};

class Adapter {
public:
  Core core;
  Controls controls;
  Adapter() { timeHysteresis_.Init(.001f); setHostRate(48000.0); }
  void setHostRate(double rate) {
    hostRate_ = std::max(8000.0, std::min(384000.0, std::isfinite(rate) ? rate : 48000.0));
    if (!kernelsReady_ || kernelHostRate_ != hostRate_) {
      const int rates[3] = {24000, 32000, 48000};
      for (int i = 0; i < 3; ++i) {
        inputSrc_[i].configure(hostRate_, rates[i]);
        outputSrc_[i].configure(rates[i], hostRate_);
      }
      kernelsReady_ = true;
      kernelHostRate_ = hostRate_;
    }
    resetBridge();
  }
  double hostRate() const { return hostRate_; }
  bool frozen() const { return frozen_; }
  bool blockFrozen() const { return blockFrozen_; }
  bool seedLocked() const { return seedLocked_; }
  void setSeedLocked(bool locked) { seedLocked_ = locked; }
  double latencySeconds() const {
    return (hostRate_ == core.rate() ? 0.0 : 64.0 / hostRate_ + 64.0 / core.rate()) + core.blockSize() / double(core.rate());
  }
  uint64_t renderedBlocks() const { return blocks_; }
  void reset() {
    core.reset();
    timeHysteresis_.Init(.001f);
    const float defaults[4] = {0.f, 0.f, .65f, .5f};
    for (int i = 0; i < 4; ++i) {
      smoothedBase_[i] = defaults[i]; smoothedRandom_[i] = smoothedCv_[i] = 0.f;
    }
    smoothedDensity_ = .3f; assignCv_ = 0.f;
    disconnectedSeconds_ = heldSeconds_ = 0.;
    frozen_ = freezeLatched_ = false; seedLocked_ = true;
    previousSeedButton_ = previousFreezeButton_ = previousLatchButton_ = false;
    previousSeedVoltage_ = previousFreezeVoltage_ = false;
    blankSamples_ = 0; blocks_ = 0;
    resetBridge();
  }
  void resetBridge() {
    converter_ = core.rate() == 24000 ? 0 : core.rate() == 32000 ? 1 : 2;
    inputSrc_[converter_].reset();
    outputSrc_[converter_].reset();
    hostIndex_ = internalIndex_ = 0;
    blockFill_ = 0;
    gates_.fill(Gates());
    gateTags_.fill(-1);
    blockFrozen_ = false;
    previousGateIndex_ = -1;
  }
  Resampler::Frame process(float left, float right, float seedVoltage, float freezeVoltage) {
    // Button semantics from Ui::OnSwitchPressed/Released; a 1.5 s hold latches.
    if (controls.seedButton && !previousSeedButton_) { seedLocked_ = false; heldSeconds_ = 0.0; }
    if (controls.seedButton) {
      heldSeconds_ += 1.0 / hostRate_;
      if (heldSeconds_ >= 1.5) seedLocked_ = true;
    }
    if (controls.latchButton && !previousLatchButton_) seedLocked_ = true;
    if (controls.freezeButton && !previousFreezeButton_) {
      if (controls.seedButton) seedLocked_ = true;
      else freezeLatched_ = !freezeLatched_;
    }
    previousSeedButton_ = controls.seedButton;
    previousFreezeButton_ = controls.freezeButton;
    previousLatchButton_ = controls.latchButton;
    previousSeedVoltage_ = hysteresis(seedVoltage, previousSeedVoltage_);
    previousFreezeVoltage_ = hysteresis(freezeVoltage, previousFreezeVoltage_);
    frozen_ = freezeLatched_ || previousFreezeVoltage_;

    bool hasAudio = controls.leftConnected || controls.rightConnected;
    bool requestedMono = !controls.leftConnected || !controls.rightConnected;
    bool desiredSynth = core.synthMode();
    if (hasAudio) { disconnectedSeconds_ = 0.0; desiredSynth = false; }
    else if (!frozen_) {
      disconnectedSeconds_ += 1.0 / hostRate_;
      if (disconnectedSeconds_ >= 10.0) desiredSynth = true;
    }
    int quality = std::max(0, std::min(3, controls.quality));
    if (!frozen_ && (quality != core.media() || (hasAudio && requestedMono != core.mono()) || desiredSynth != core.synthMode())) {
      bool resetGain = requestedMono != core.mono() || desiredSynth != core.synthMode();
      core.configure(quality, requestedMono, desiredSynth, resetGain);
      resetBridge();
      blankSamples_ = int(hostRate_ * .25);
    }
    if (controls.recalibrate) { core.restartGain(); controls.recalibrate = false; }

    Resampler::Frame input;
    input.l = bounded(left, -1.f, .999999f);
    input.r = bounded(right, -1.f, .999999f);
    inputSrc_[converter_].push(hostIndex_, input);
    int slot = int(hostIndex_ & (Resampler::capacity - 1));
    Gates gate;
    gate.seed = previousSeedVoltage_;
    gate.freeze = frozen_;
    gates_[slot] = gate;
    gateTags_[slot] = hostIndex_;

    double internalTime = hostIndex_ * (double(core.rate()) / hostRate_);
    while (internalIndex_ <= int64_t(std::floor(internalTime + 1e-9))) {
      double sourceTime = internalIndex_ * (hostRate_ / core.rate());
      bool native = hostRate_ == core.rate();
      Resampler::Frame resampled = native ? inputSrc_[converter_].get(internalIndex_) : inputSrc_[converter_].at(sourceTime - 64.0);
      int64_t gateIndex = int64_t(std::floor(sourceTime - (native ? 0.0 : 64.0)));
      Gates state;
      // A new SRC history must not unfreeze a held/frozen recording or create
      // a falling/rising SEED pair while the host changes sample rate.
      if (gateIndex < 0) {
        state.seed = previousSeedVoltage_;
        state.freeze = frozen_;
      }
      int gs = int(gateIndex & (Resampler::capacity - 1));
      if (gateIndex >= 0 && gateTags_[gs] == gateIndex) state = gates_[gs];
      // Preserve a host trigger narrower than one internal sample interval.
      for (int64_t edge = std::max(previousGateIndex_ + 1, gateIndex - 48); edge <= gateIndex; ++edge) {
        int es = int(edge & (Resampler::capacity - 1));
        if (edge >= 0 && gateTags_[es] == edge) {
          state.seed = state.seed || gates_[es].seed;
          state.freeze = state.freeze || gates_[es].freeze;
        }
      }
      previousGateIndex_ = gateIndex;
      inputBlock_[blockFill_].l = resampled.l;
      inputBlock_[blockFill_].r = resampled.r;
      seedBlock_[blockFill_] = state.seed;
      freezeBlock_[blockFill_] = state.freeze;
      ++blockFill_;
      ++internalIndex_;
      if (blockFill_ == core.blockSize()) {
        mapControls();
        core.process(inputBlock_.data(), seedBlock_.data(), freezeBlock_.data(), outputBlock_.data(), blankSamples_ <= 0);
        int n = core.blockSize();
        blockFrozen_ = true;
        for (int i = 0; i < n; ++i) blockFrozen_ = blockFrozen_ && freezeBlock_[i];
        for (int i = 0; i < n; ++i) {
          Resampler::Frame frame;
          frame.l = outputBlock_[i].l; frame.r = outputBlock_[i].r;
          outputSrc_[converter_].push(internalIndex_ - n + i, frame);
        }
        blockFill_ = 0;
        ++blocks_;
      }
    }
    bool native = hostRate_ == core.rate();
    double outputTime = internalTime - core.blockSize() - (native ? 0.0 : 64.0);
    Resampler::Frame out = native ? outputSrc_[converter_].get(int64_t(outputTime)) : outputSrc_[converter_].at(outputTime);
    ++hostIndex_;
    if (blankSamples_ > 0) { --blankSamples_; return Resampler::Frame(); }
    out.l = bounded(out.l, -1.f, .999999f);
    out.r = bounded(out.r, -1.f, .999999f);
    return out;
  }
  void setFreeze(bool frozen) { freezeLatched_ = frozen; frozen_ = frozen; }
  static float attenurandomizer(float position) {
    float v = bounded(position, -1.f, 1.f) * .5f;
    return (2.f / .47f) * v * std::max(std::fabs(v) - .03f, 0.f);
  }
  static float pitchLaw(float position) {
    float v = bounded(position, -1.f, 1.f);
    float sign = v < 0.f ? -1.f : 1.f;
    float d = std::fabs(v) * 6.f;
    int integral = std::min(6, int(d));
    float fraction = std::max((d - integral - 1.f / 3.f) * 1.5f, 0.f);
    static const float intervals[8] = {0.f, 1.f, 3.f, 4.f, 7.f, 12.f, 24.f, 24.f};
    return (intervals[integral] + (intervals[integral + 1] - intervals[integral]) * stmlib::SmoothStep(fraction)) * sign;
  }
private:
  void mapControls() {
    beads::RandomizedParameter* p[4] = {&core.parameters.time, &core.parameters.pitch, &core.parameters.size, &core.parameters.shape};
    float base[4] = {bounded(controls.time, 0.f, 1.f), pitchLaw(controls.pitch), bounded(controls.size, 0.f, 1.f), bounded(controls.shape, 0.f, 1.f)};
    for (int i = 0; i < 4; ++i) {
      float coefficient = i == 0 || i == 2 || i == 3 ? .5f : 1.f;
      smoothedCv_[i] += coefficient * (bounded(controls.cv[i], i == 1 ? -60.f : -1.6f, i == 1 ? 60.f : 1.6f) - smoothedCv_[i]);
      smoothedBase_[i] += .05f * (base[i] - smoothedBase_[i]);
      smoothedRandom_[i] += .05f * (attenurandomizer(controls.random[i]) - smoothedRandom_[i]);
      p[i]->base = smoothedBase_[i];
      if (i == 0) {
        timeHysteresis_.Process(smoothedBase_[i]);
        p[i]->base = timeHysteresis_.value();
      }
      p[i]->attenuverter = smoothedRandom_[i];
      p[i]->has_cv = controls.connected[i];
      p[i]->cv = controls.connected[i] ? smoothedCv_[i] : 0.f;
    }
    core.parameters.monophonic_mode = false;
    smoothedDensity_ += .05f * (bounded(controls.density, 0.f, 1.f) - smoothedDensity_);
    core.density.base = smoothedDensity_;
    core.density.cv = bounded(controls.densityCv, -1.f, 1.f);
    assignCv_ += .1f * (bounded(controls.assignCv, -1.f, 1.f) - assignCv_);
    float bases[3] = {controls.feedback, controls.mix, controls.reverb};
    float result[3];
    for (int i = 0; i < 3; ++i) {
      float amount = controls.assignment == 3 ? bounded(controls.assignDepth[i], 0.f, 1.f) : controls.assignment == i ? 1.f : 0.f;
      result[i] = bounded(bases[i] * 1.005f + (assignCv_ - .002f) * amount, 0.f, 1.f);
    }
    core.mix.feedback = result[0]; core.mix.dry_wet = result[1]; core.mix.reverb_amount = result[2];
    core.mix.output_gate = controls.gateOutput;
    core.seedState = seedLocked_ ? beads::SEED_BUTTON_STATE_LOCKED : controls.seedButton ? beads::SEED_BUTTON_STATE_ON : beads::SEED_BUTTON_STATE_OFF;
    core.seedConnected = controls.seedConnected;
    core.leftConnected = controls.leftConnected;
    core.rightConnected = controls.rightConnected;
    core.gainSetting = uint8_t(std::max(0, std::min(255, controls.gain)));
  }
  static bool hysteresis(float voltage, bool state) {
    return std::isfinite(voltage) && (state ? voltage > .1f : voltage >= 1.f);
  }
  struct Gates { bool seed = false; bool freeze = false; };
  std::array<Resampler, 3> inputSrc_, outputSrc_;
  int converter_ = 2;
  bool kernelsReady_ = false;
  double kernelHostRate_ = 0.;
  stmlib::HysteresisFilter timeHysteresis_;
  std::array<Gates, Resampler::capacity> gates_{};
  std::array<int64_t, Resampler::capacity> gateTags_{};
  std::array<beads::FloatFrame, Core::maxBlock> inputBlock_{}, outputBlock_{};
  std::array<bool, Core::maxBlock> seedBlock_{}, freezeBlock_{};
  double hostRate_ = 48000.0, disconnectedSeconds_ = 0.0, heldSeconds_ = 0.0;
  int64_t hostIndex_ = 0, internalIndex_ = 0;
  int64_t previousGateIndex_ = -1;
  uint64_t blocks_ = 0;
  int blockFill_ = 0, blankSamples_ = 0;
  float smoothedBase_[4] = {0.f, 0.f, .65f, .5f};
  float smoothedRandom_[4] = {}, smoothedCv_[4] = {};
  float smoothedDensity_ = .3f, assignCv_ = 0.f;
  bool frozen_ = false, freezeLatched_ = false, seedLocked_ = true;
  bool blockFrozen_ = false;
  bool previousSeedButton_ = false, previousFreezeButton_ = false, previousLatchButton_ = false;
  bool previousSeedVoltage_ = false, previousFreezeVoltage_ = false;
};
}
