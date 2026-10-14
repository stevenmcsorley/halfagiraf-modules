#include "../../src/GrainloomAdapter.hpp"
#include <cstdio>
#include <cstring>
#include <memory>

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  FILE* file = std::fopen(argv[1], "wb"); if (!file) return 3;
  constexpr int frames = 48000 * 18;
  unsigned char header[44] = {};
  auto u32 = [&](int offset, uint32_t value) { for (int i = 0; i < 4; ++i) header[offset + i] = uint8_t(value >> (i * 8)); };
  auto u16 = [&](int offset, uint16_t value) { header[offset] = uint8_t(value); header[offset + 1] = uint8_t(value >> 8); };
  std::memcpy(header, "RIFF", 4); u32(4, frames * 4 + 36); std::memcpy(header + 8, "WAVEfmt ", 8);
  u32(16, 16); u16(20, 1); u16(22, 2); u32(24, 48000); u32(28, 192000); u16(32, 4); u16(34, 16);
  std::memcpy(header + 36, "data", 4); u32(40, frames * 4); std::fwrite(header, 1, 44, file);
  std::unique_ptr<grainloom::Adapter> a(new grainloom::Adapter());
  auto& c = a->controls; c.leftConnected = c.rightConnected = true;
  c.density = .21f; c.size = .69f; c.shape = .54f;
  c.random[0] = .42f; c.random[1] = .05f; c.random[2] = -.12f;
  c.feedback = .23f;
  for (int i = 0; i < frames; ++i) {
    double t = i / 48000.;
    float in = .10f * std::sin(2. * 3.141592653589793 * 110. * t)
      + .07f * std::sin(2. * 3.141592653589793 * 164.8138 * t)
      + .05f * std::sin(2. * 3.141592653589793 * 220. * t);
    in *= .6f + .4f * std::sin(t * .7) * std::sin(t * .7);
    c.mix = t < 2. ? 0.f : .82f; c.reverb = t < 2. ? 0.f : .46f;
    c.time = .12f + .2f * (.5f + .5f * std::sin(t * .31));
    c.freezeButton = i == 48000 * 9;
    if (t > 13.) c.size = 1.f;
    auto out = a->process(in, in * .92f, 0.f, 0.f);
    int16_t pcm[2] = {int16_t(out.l * 32767.f), int16_t(out.r * 32767.f)};
    std::fwrite(pcm, sizeof(int16_t), 2, file);
  }
  std::fclose(file);
}
