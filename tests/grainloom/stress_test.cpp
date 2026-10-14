#include "../../src/GrainloomAdapter.hpp"
#include "../../src/GrainloomSnapshot.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <cstdlib>
#include <new>

static thread_local bool prohibitAllocation = false;
void* operator new(std::size_t size) {
  if (prohibitAllocation) throw std::bad_alloc();
  if (void* memory = std::malloc(size ? size : 1)) return memory;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }

static void pitchTracking() {
  std::unique_ptr<grainloom::Adapter> a(new grainloom::Adapter());
  for (double rate : {44100., 48000., 88200., 96000., 192000.}) {
    for (int octave = -1; octave <= 2; ++octave) {
      a->core.configure(3, true, true, true);
      a->controls.quality = 3; a->controls.mix = 0.f;
      a->controls.connected[1] = true; a->controls.cv[1] = 12.f * octave;
      a->setHostRate(rate);
      float previous = 0.f; int crossings = 0;
      double firstCrossing = 0., lastCrossing = 0.;
      for (int i = 0; i < int(rate * 3.5); ++i) {
        auto out = a->process(0, 0, 0, 0);
        if (i >= int(rate * .5) && previous <= 0.f && out.l > 0.f) {
          // Period measurement avoids the +/- one-cycle error of counting a
          // fixed three-second window, which exceeds six cents at low notes.
          double crossing = i - 1.0 - previous / double(out.l - previous);
          if (crossings++ == 0) firstCrossing = crossing;
          lastCrossing = crossing;
        }
        previous = out.l;
      }
      double frequency = (crossings - 1) * rate / (lastCrossing - firstCrossing);
      double expected = 130.81 * std::pow(2., octave);
      double cents = 1200. * std::log2(frequency / expected);
      std::cout << "Pitch " << rate << " octave " << octave << " " << frequency << "Hz error=" << cents << " cents\n";
      std::cout.flush();
      assert(std::fabs(cents) < 6.);
    }
  }
}
static double longRun(uint32_t seed, bool modulate) {
  std::unique_ptr<grainloom::Adapter> a(new grainloom::Adapter());
  a->controls.leftConnected = a->controls.rightConnected = true;
  a->core.rng = seed;
  double energy = 0.;
  uint32_t random = seed;
  prohibitAllocation = true;
  for (int i = 0; i < 48000 * (modulate ? 600 : 10); ++i) {
    if (modulate && i % 2400 == 0) {
      random = random * 1664525u + 1013904223u;
      a->controls.time = (random & 65535) / 65535.f;
      a->controls.size = ((random >> 16) & 65535) / 65535.f;
      a->controls.pitch = std::sin(i * .007f);
      a->controls.feedback = 1.f; a->controls.reverb = .9f;
      a->controls.density = std::fmod(i * .00073f, 1.f);
      a->controls.freezeButton = !a->controls.freezeButton;
      a->controls.quality = (i / 48000) % 4;
    }
    float in = .25f * std::sin(i * .027f);
    auto out = a->process(in, in * .7f, i % 480 < 3 ? 10.f : 0.f, 0.f);
    assert(std::isfinite(out.l) && std::isfinite(out.r));
    assert(std::fabs(out.l) <= 1.f && std::fabs(out.r) <= 1.f);
    energy += out.l * out.l + out.r * out.r;
  }
  prohibitAllocation = false;
  return energy;
}
static void independentThreads() {
  double expectedA = longRun(1, false), expectedB = longRun(31415, false);
  double actualA = 0., actualB = 0.;
  std::thread first([&] { actualA = longRun(1, false); });
  std::thread second([&] { actualB = longRun(31415, false); });
  first.join(); second.join();
  assert(expectedA == actualA && expectedB == actualB);
}
static void concurrentSnapshots() {
  std::unique_ptr<grainloom::Core> core(new grainloom::Core());
  std::unique_ptr<grainloom::SnapshotBank> bank(new grainloom::SnapshotBank());
  std::atomic<bool> running{true}; std::atomic<int> checks{0};
  std::thread reader([&] {
    while (running.load()) {
      auto* slot = bank->claim();
      if (!slot) { std::this_thread::yield(); continue; }
      uint8_t expected = slot->bytes[0];
      for (uint8_t value : slot->bytes) assert(value == expected);
      ++checks; bank->release(slot);
    }
  });
  double maximumCopyMicros = 0.;
  for (int freeze = 0; freeze < 100; ++freeze) {
    bank->tick(*core, false);
    std::memset(core->buffer(), freeze, grainloom::Core::bufferBytes);
    auto start = std::chrono::steady_clock::now();
    bank->tick(*core, true);
    maximumCopyMicros = std::max(maximumCopyMicros, std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
    for (int block = 0; block < 400; ++block) bank->tick(*core, true);
  }
  running.store(false); reader.join(); assert(checks > 0);
  std::cout << "Concurrent immutable snapshot checks=" << checks << "\n";
  std::cout << "Largest observed fixed snapshot copy=" << maximumCopyMicros << "us\n";
}
int main() {
  pitchTracking(); independentThreads(); concurrentSnapshots();
  auto start = std::chrono::steady_clock::now();
  double energy = longRun(42, true); assert(energy > 1.);
  double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::cout << "600s modulated feedback/freeze/media stress passed in " << elapsed << "s\n";
  std::cout << "Adapter storage=" << sizeof(grainloom::Adapter) << " snapshot storage=" << sizeof(grainloom::SnapshotBank) << " bytes\n";
}
