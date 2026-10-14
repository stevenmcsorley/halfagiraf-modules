// Compile this same program against untouched upstream and vendored host DSP.
// Float bytes are compared by the target; this does not compare analog codecs.
#include <cstdio>
#include <cstring>
#include <cmath>
#include "beads/dsp/processor.h"
#include "beads/dsp/synth.h"
#include "stmlib/utils/random.h"
static uint8_t memory[3 * 262144];
static uint16_t reverb[32768];
static beads::Processor processor;
static beads::Synth synth;

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  FILE* file = std::fopen(argv[1], "wb"); if (!file) return 3;
  uint8_t* banks[] = {memory, memory + 262144, memory + 524288};
  processor.Init(banks, reverb); synth.Init(reverb);
  beads::Parameters p{};
  p.size.base = .67f; p.time.base = .3f; p.shape.base = .41f;
  p.pitch.attenuverter = .17f; p.time.attenuverter = -.32f;
  beads::ModulatedParameter d{}; d.base = .18f;
  beads::Processor::MixParameters mix{};
  mix.feedback = .42f; mix.dry_wet = .77f; mix.reverb_amount = .47f;
  bool frozen[12] = {}, seed[12] = {};
  (void)seed;
  beads::FloatFrame input[12], output[12];
  for (int media = 0; media < 4; ++media) {
    processor.Configure(beads::Processor::Media(media), false);
    stmlib::Random::Seed(0x21);
    int n = int(processor.sample_rate()) / 4000;
    for (int block = 0; block < 20000; ++block) {
      if (block == 10000) p.size.base = 1.f;
      for (int i = 0; i < n; ++i) {
        float t = (block * n + i) / processor.sample_rate();
        input[i].l = .31f * std::sin(2.f * 3.14159265358979323846f * 261.626f * t);
        input[i].r = .23f * std::sin(2.f * 3.14159265358979323846f * 329.628f * t);
        frozen[i] = block > 6000 && block < 9000;
      }
      processor.Process(beads::SEED_BUTTON_STATE_LOCKED, nullptr, frozen, d, p, mix, input, output, n);
      if (std::fwrite(output, sizeof(beads::FloatFrame), n, file) != size_t(n)) return 4;
    }
    p.size.base = .67f;
  }
  for (int bank = 0; bank < 12; ++bank) {
    mix.feedback = bank / 11.f;
    for (int block = 0; block < 2000; ++block) {
      for (int i = 0; i < 12; ++i) frozen[i] = false;
      synth.Render(beads::SEED_BUTTON_STATE_LOCKED, nullptr, frozen, beads::Synth::RESOLUTION_16_BIT, d, p, mix, output, 12);
      if (std::fwrite(output, sizeof(beads::FloatFrame), 12, file) != 12) return 5;
    }
  }
  std::fclose(file);
}
