#include "../../src/GrainloomAdapter.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <limits>

static void resamplerTest() {
  std::unique_ptr<grainloom::Resampler> src(new grainloom::Resampler());
  for (double rate : {44100., 48000., 88200., 96000., 192000.}) {
    src->configure(rate, 24000.);
    double pass = 0., stop = 0.;
    for (int i = 0; i < 40000; ++i) {
      grainloom::Resampler::Frame f;
      f.l = std::sin(2. * 3.141592653589793 * 1000. * i / rate);
      f.r = std::sin(2. * 3.141592653589793 * 17000. * i / rate);
      src->push(i, f);
      if (i > 1000) {
        auto o = src->at(i - 64.);
        pass += o.l * o.l;
        stop += o.r * o.r;
      }
    }
    double rejection = 10. * std::log10(stop / pass);
    std::cout << "SRC " << rate << "->24000 rejection=" << rejection << " dB\n";
    assert(rejection < -60.);
  }
}

static void directParity() {
  std::unique_ptr<grainloom::Core> a(new grainloom::Core()), b(new grainloom::Core());
  for (int quality = 0; quality < 4; ++quality) {
    a->configure(quality, false, false, true);
    b->configure(quality, false, false, true);
    a->mix.reverb_amount = b->mix.reverb_amount = .45f;
    a->mix.feedback = b->mix.feedback = .35f;
    bool seed[12] = {}, frozen[12] = {};
    beads::FloatFrame input[12], first[12], second[12];
    for (int block = 0; block < 12000; ++block) {
      for (int i = 0; i < a->blockSize(); ++i) {
        input[i].l = .3f * std::sin(float(block * a->blockSize() + i) * .03f);
        input[i].r = .2f * std::sin(float(block * a->blockSize() + i) * .041f);
        frozen[i] = block > 7000;
      }
      a->process(input, seed, frozen, first);
      b->process(input, seed, frozen, second);
      for (int i = 0; i < a->blockSize(); ++i) {
        assert(first[i].l == second[i].l && first[i].r == second[i].r);
      }
    }
  }
  std::cout << "Deterministic per-instance DSP state passes (not yet an independent upstream comparison)\n";
}

static void nativeAdapterParity() {
  std::unique_ptr<grainloom::Adapter> adapter(new grainloom::Adapter());
  std::unique_ptr<grainloom::Core> reference(new grainloom::Core());
  adapter->controls.leftConnected = adapter->controls.rightConnected = true;
  beads::FloatFrame input[12], referenceOut[12];
  std::array<beads::FloatFrame, 256> expected{};
  bool seed[12] = {}, frozen[12] = {};
  for (int sample = 0; sample < 48000 * 3; ++sample) {
    float l = .23f * std::sin(sample * .031f), r = .19f * std::sin(sample * .047f);
    input[sample % 12].l = l; input[sample % 12].r = r;
    auto actual = adapter->process(l, r, 0.f, 0.f);
    if (sample % 12 == 11) {
      reference->parameters = adapter->core.parameters;
      reference->density = adapter->core.density;
      reference->mix = adapter->core.mix;
      reference->process(input, seed, frozen, referenceOut);
      for (int i = 0; i < 12; ++i) expected[(sample - 11 + i) & 255] = referenceOut[i];
    }
    if (sample >= 12) {
      auto want = expected[(sample - 12) & 255];
      assert(actual.l == want.l && actual.r == want.r);
    }
  }
  std::cout << "48 kHz adapter versus direct original-domain DSP: sample-identical after 12 frames\n";
}

int main() {
  std::cout << "Packed 12-bit stereo bytes=" << sizeof(beads::Item<beads::RESOLUTION_12_BIT, 2>) << std::endl;
  assert(sizeof(beads::Item<beads::RESOLUTION_12_BIT, 2>) == 3);
  beads::Item<beads::RESOLUTION_12_BIT, 2> packed{};
  packed.l = 0x123; packed.r = -0x345;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&packed);
  uint32_t expectedPacked = 0x123u | ((uint32_t(-0x345) & 0xfffu) << 12);
  assert(bytes[0] == uint8_t(expectedPacked) && bytes[1] == uint8_t(expectedPacked >> 8) && bytes[2] == uint8_t(expectedPacked >> 16));
  beads::WavetableGrain boundary;
  boundary.Init(48000.f);
  boundary.Start(11, 16, 1.f, .005f, 8000.f, .5f, .5f, .5f, 0.f);
  for (int i = 0; i < 10000; ++i) {
    beads::FloatFrame out{};
    boundary.Render(&out);
    assert(std::isfinite(out.l) && std::isfinite(out.r));
  }
  resamplerTest();
  directParity();
  nativeAdapterParity();
  auto start = std::chrono::steady_clock::now();
  std::unique_ptr<grainloom::Adapter> adapter(new grainloom::Adapter());
  for (double rate : {44100., 48000., 88200., 96000., 192000.}) {
    for (int quality = 0; quality < 4; ++quality) {
      adapter->setHostRate(rate);
      adapter->controls.quality = quality;
      adapter->controls.leftConnected = adapter->controls.rightConnected = true;
      double energy = 0.;
      uint64_t before = adapter->renderedBlocks();
      for (int sample = 0; sample < int(rate * 2); ++sample) {
        float input = .2f * std::sin(2. * 3.141592653589793 * sample * 220. / rate);
        auto out = adapter->process(input, input * .8f, 0.f, 0.f);
        assert(std::isfinite(out.l) && std::isfinite(out.r));
        assert(std::fabs(out.l) <= 1.f && std::fabs(out.r) <= 1.f);
        energy += out.l * out.l + out.r * out.r;
      }
      assert(energy > 1.);
      assert(adapter->renderedBlocks() - before >= 7990 && adapter->renderedBlocks() - before <= 8001);
      auto out = adapter->process(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 0.f, 0.f);
      assert(std::isfinite(out.l) && std::isfinite(out.r));
      std::cout << "Host " << rate << " media " << quality << " energy=" << energy << " latency=" << adapter->latencySeconds() * 1000. << "ms\n";
    }
  }
  adapter->controls.leftConnected = adapter->controls.rightConnected = false;
  adapter->setHostRate(48000.);
  for (int i = 0; i < 48000 * 11; ++i) adapter->process(0.f, 0.f, 0.f, 0.f);
  assert(adapter->core.synthMode());
  double synthEnergy = 0.;
  for (int i = 0; i < 48000; ++i) {
    auto out = adapter->process(0.f, 0.f, 0.f, 0.f);
    synthEnergy += out.l * out.l + out.r * out.r;
  }
  assert(synthEnergy > 1.);
  std::cout << "Automatic wavetable transition and output pass\n";
  auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::cout << "Adapter bytes=" << sizeof(grainloom::Adapter) << " offline matrix time=" << elapsed << "s\n";
}
