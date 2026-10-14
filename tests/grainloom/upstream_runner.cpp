// Calls the original tests; the original main() has every test commented out.
#include <cstdio>
#include <cstdlib>
#include <iostream>
#define main beads_disabled_upstream_main
#include "../../vendor/mutable/beads/test/beads_test.cc"
#undef main

static void fixture(const char* path) {
  FILE* file = fopen(path, "wb");
  if (!file) { std::perror(path); std::exit(1); }
  unsigned char header[48] = {};
  fwrite(header, 1, sizeof(header), file);
  uint32_t state = 1;
  for (int i = 0; i < 48000 * 40; ++i) {
    state = state * 1664525u + 1013904223u;
    float noise = int32_t(state) / 2147483648.f;
    float envelope = std::exp(-float(i % 12000) / 1800.f);
    int16_t stereo[2] = {
      int16_t(9000.f * std::sin(i * 2.f * 3.14159265358979323846f * 261.626f / 48000.f) + 3500.f * envelope * noise),
      int16_t(8000.f * std::sin(i * 2.f * 3.14159265358979323846f * 329.628f / 48000.f) + 3000.f * envelope * noise)
    };
    fwrite(stereo, sizeof(int16_t), 2, file);
  }
  fclose(file);
}

int main() {
  fixture("audio_samples/kettel.wav");
  fixture("audio_samples/talking_heads.wav");
  buffers[0] = block_1;
  buffers[1] = block_1 + 256 * 1024;
  buffers[2] = block_2;
  TestGrainFade();
  TestTriggerGenerator();
  TestTriggerGeneratorExpoFM();
  TestAudioRateTriggerGenerator();
  TestProcessor();
  TestSynth();
  std::cout << "Original Beads grain-fade assertions and trigger, processor, synth renders completed\n";
}
