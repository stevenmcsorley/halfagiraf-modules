#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace grainloom {
// Fixed storage, 128-tap Blackman-windowed sinc with 512 fractional phases.
// The caller supplies the delayed source timestamp; both channels use one phase.
class Resampler {
public:
  static constexpr int taps = 128;
  static constexpr int phases = 512;
  static constexpr int capacity = 2048;
  struct Frame { float l = 0.f; float r = 0.f; };
  void configure(double inputRate, double outputRate) {
    const double pi = 3.14159265358979323846;
    const double cutoff = .92 * std::min(1.0, outputRate / inputRate);
    for (int p = 0; p <= phases; ++p) {
      double sum = 0.0;
      for (int i = 0; i < taps; ++i) {
        double x = i - 63.0 - p / double(phases);
        double sinc = std::fabs(x) < 1e-12 ? cutoff : std::sin(pi * cutoff * x) / (pi * x);
        double window = .42 - .5 * std::cos(2.0 * pi * i / (taps - 1)) + .08 * std::cos(4.0 * pi * i / (taps - 1));
        kernel_[p][i] = float(sinc * window);
        sum += sinc * window;
      }
      for (float& k : kernel_[p]) k = float(k / sum);
    }
    reset();
  }
  void reset() { frames_.fill(Frame()); tags_.fill(-1); }
  void push(int64_t index, Frame frame) {
    int slot = int(index & (capacity - 1));
    frames_[slot] = frame;
    tags_[slot] = index;
  }
  Frame get(int64_t index) const {
    if (index < 0) return Frame();
    int slot = int(index & (capacity - 1));
    return tags_[slot] == index ? frames_[slot] : Frame();
  }
  Frame at(double position) const {
    int64_t center = int64_t(std::floor(position));
    double phase = (position - center) * phases;
    int p = std::min(phases - 1, int(phase));
    float fraction = float(phase - p);
    Frame out;
    for (int i = 0; i < taps; ++i) {
      Frame sample = get(center + i - 63);
      float coefficient = kernel_[p][i] + fraction * (kernel_[p + 1][i] - kernel_[p][i]);
      out.l += sample.l * coefficient;
      out.r += sample.r * coefficient;
    }
    return out;
  }
private:
  std::array<std::array<float, taps>, phases + 1> kernel_{};
  std::array<Frame, capacity> frames_{};
  std::array<int64_t, capacity> tags_{};
};
}
