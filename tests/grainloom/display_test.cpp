// Display hookup only: original recorded formats, bank boundaries, no DSP writes.
#include "../../src/GrainloomCore.hpp"
#include <cassert>
#include <memory>
#include <iostream>
template<beads::Resolution resolution, int channels>
static void verify(grainloom::Core& core) {
  using Item = beads::Item<resolution, channels>;
  int perBank = grainloom::Core::bankBytes / sizeof(Item);
  for (int frame : {0, perBank * Item::bundle_size - 1, perBank * Item::bundle_size, core.maxFrames() - 1}) {
    int itemIndex = frame / Item::bundle_size;
    int offset = (itemIndex / perBank) * grainloom::Core::bankBytes + (itemIndex % perBank) * sizeof(Item);
    Item item{}; beads::FloatFrame signal; signal.l = .25f; signal.r = -.125f;
    item.Store(frame % Item::bundle_size, signal);
    std::memcpy(core.buffer() + offset, &item, sizeof(Item));
    auto expected = item.value(frame % Item::bundle_size);
    uint32_t randomBefore = core.rng;
    auto actual = core.displaySample(frame);
    assert(actual.l == expected.l && actual.r == expected.r && randomBefore == core.rng);
    assert(std::memcmp(core.buffer() + offset, &item, sizeof(Item)) == 0);
  }
}
int main() {
  std::unique_ptr<grainloom::Core> core(new grainloom::Core());
  for (int quality = 0; quality < 4; ++quality) for (bool mono : {false, true}) {
    core->configure(quality, mono, false, true);
    if (quality == 3) { if (mono) verify<beads::RESOLUTION_16_BIT,1>(*core); else verify<beads::RESOLUTION_16_BIT,2>(*core); }
    else if (quality == 0) { if (mono) verify<beads::RESOLUTION_8_BIT,1>(*core); else verify<beads::RESOLUTION_8_BIT,2>(*core); }
    else { if (mono) verify<beads::RESOLUTION_12_BIT,1>(*core); else verify<beads::RESOLUTION_12_BIT,2>(*core); }
  }
  std::cout << "Display readback: all 8 formats, bank boundaries, decoder equality and no state mutation pass\n";
}
