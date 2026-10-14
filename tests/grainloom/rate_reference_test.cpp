// Offline golden SRC evaluates exact rational phases in double precision,
// independently of the adapter's float, interpolated 512-phase streaming SRC.
// Native Core DSP is separately compared with the pristine pinned upstream.
#include "../../src/GrainloomAdapter.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

using Frame = grainloom::Resampler::Frame;
class OfflineSinc {
public:
  OfflineSinc(int inputRate, int outputRate) {
    int a = inputRate, b = outputRate;
    while (b) { int c = a % b; a = b; b = c; }
    denominator_ = outputRate / a;
    kernel_.resize(denominator_);
    double cutoff = .92 * std::min(1., double(outputRate) / inputRate);
    const double pi = 3.14159265358979323846;
    for (int phase = 0; phase < denominator_; ++phase) {
      double sum = 0.;
      for (int tap = 0; tap < 128; ++tap) {
        double x = tap - 63. - double(phase) / denominator_;
        double sinc = std::fabs(x) < 1e-12 ? cutoff : std::sin(pi * cutoff * x) / (pi * x);
        double window = .42 - .5 * std::cos(2. * pi * tap / 127.) + .08 * std::cos(4. * pi * tap / 127.);
        sum += kernel_[phase][tap] = sinc * window;
      }
      for (double& coefficient : kernel_[phase]) coefficient /= sum;
    }
  }
  Frame at(const std::vector<Frame>& signal, double position) const {
    int64_t center = int64_t(std::floor(position));
    int phase = int(std::llround((position - center) * denominator_));
    if (phase == denominator_) { phase = 0; ++center; }
    double left = 0., right = 0.;
    for (int tap = 0; tap < 128; ++tap) {
      int64_t index = center + tap - 63;
      if (index < 0 || index >= int64_t(signal.size())) continue;
      left += signal[index].l * kernel_[phase][tap];
      right += signal[index].r * kernel_[phase][tap];
    }
    Frame result; result.l = float(left); result.r = float(right); return result;
  }
private:
  int denominator_;
  std::vector<std::array<double, 128>> kernel_;
};
struct BlockControls {
  beads::Parameters parameters;
  beads::ModulatedParameter density;
  beads::Processor::MixParameters mix;
};
static void compare(int hostRate, int media) {
  std::unique_ptr<grainloom::Adapter> adapter(new grainloom::Adapter());
  std::unique_ptr<grainloom::Core> reference(new grainloom::Core());
  adapter->core.configure(media, false, false, true);
  reference->configure(media, false, false, true);
  int nativeRate = reference->rate(), blockSize = reference->blockSize();
  auto& c = adapter->controls;
  c.quality = media; c.leftConnected = c.rightConnected = true; c.gain = 64;
  c.time = .37f; c.size = .63f; c.shape = .41f; c.density = .16f;
  c.random[0] = .2f; c.random[1] = -.1f;
  c.feedback = .24f; c.mix = .76f; c.reverb = .31f;
  adapter->setHostRate(hostRate);
  int length = hostRate * 2;
  std::vector<Frame> input(length), actual(length);
  std::vector<BlockControls> blocks;
  uint32_t random = 12345;
  for (int i = 0; i < length; ++i) {
    double time = double(i) / hostRate;
    double beat = std::fmod(time, .25);
    random = random * 1664525u + 1013904223u;
    double noise = int32_t(random) / 2147483648.;
    input[i].l = float(.12 * std::sin(2. * 3.141592653589793 * (80. * time + 400. * time * time))
      + .06 * std::exp(-beat * 50.) * noise);
    input[i].r = float(.1 * std::sin(2. * 3.141592653589793 * 329.628 * time));
    if (i == 0) input[i].l = .5f;
    if (i == hostRate / 5) input[i].r = .4f;
    c.freezeButton = i == hostRate;
    uint64_t before = adapter->renderedBlocks();
    actual[i] = adapter->process(input[i].l, input[i].r, 0.f, 0.f);
    if (adapter->renderedBlocks() != before)
      blocks.push_back({adapter->core.parameters, adapter->core.density, adapter->core.mix});
  }
  bool native = hostRate == nativeRate;
  OfflineSinc inputConverter(hostRate, nativeRate), outputConverter(nativeRate, hostRate);
  std::vector<Frame> output(blocks.size() * blockSize);
  reference->gainSetting = 64;
  beads::FloatFrame nativeInput[12] = {}, nativeOutput[12] = {};
  bool seed[12] = {}, freeze[12] = {};
  for (size_t block = 0; block < blocks.size(); ++block) {
    reference->parameters = blocks[block].parameters;
    reference->density = blocks[block].density; reference->mix = blocks[block].mix;
    for (int i = 0; i < blockSize; ++i) {
      size_t sample = block * blockSize + i;
      double sourceTime = double(sample) * hostRate / nativeRate - (native ? 0. : 64.);
      Frame frame = native ? input[sample] : inputConverter.at(input, sourceTime);
      nativeInput[i].l = frame.l; nativeInput[i].r = frame.r;
      freeze[i] = int64_t(std::floor(sourceTime)) >= hostRate;
    }
    reference->process(nativeInput, seed, freeze, nativeOutput);
    for (int i = 0; i < blockSize; ++i) {
      output[block * blockSize + i].l = nativeOutput[i].l;
      output[block * blockSize + i].r = nativeOutput[i].r;
    }
  }
  double maximum = 0., squared = 0., energy = 0.;
  for (int i = 0; i < length; ++i) {
    double timestamp = double(i) * nativeRate / hostRate - blockSize - (native ? 0. : 64.);
    Frame expected;
    if (native) { if (timestamp >= 0.) expected = output[size_t(timestamp)]; }
    else expected = outputConverter.at(output, timestamp);
    expected.l = grainloom::bounded(expected.l, -1.f, .999999f);
    expected.r = grainloom::bounded(expected.r, -1.f, .999999f);
    double dl = actual[i].l - expected.l, dr = actual[i].r - expected.r;
    maximum = std::max(maximum, std::max(std::fabs(dl), std::fabs(dr)));
    squared += dl * dl + dr * dr; energy += expected.l * expected.l + expected.r * expected.r;
  }
  double rms = std::sqrt(squared / (2. * length));
  std::cout << "Offline reference " << hostRate << " media " << media << " peak error=" << maximum << " RMS=" << rms << "\n";
  assert(energy > 1.);
  // Tolerance is for finite polyphase table interpolation and float accumulation,
  // not a licence to replace or retune DSP algorithms. Native audio must be exact.
  if (native) assert(maximum == 0.);
  else { assert(maximum < 2e-4); assert(rms < 2e-5); }
}
int main() {
  for (int rate : {44100, 48000, 88200, 96000, 192000})
    for (int media = 0; media < 4; ++media) compare(rate, media);
}
