#include "../../src/GrainloomSnapshot.hpp"
#include <cassert>
#include <iostream>
#include <memory>

int main() {
  std::unique_ptr<grainloom::Core> source(new grainloom::Core()), restored(new grainloom::Core());
  std::unique_ptr<grainloom::SnapshotBank> bank(new grainloom::SnapshotBank());
  for (int media = 0; media < 4; ++media) {
    for (bool mono : {false, true}) {
      source->configure(media, mono, false, true);
      for (int i = 0; i < grainloom::Core::bufferBytes; ++i) source->buffer()[i] = uint8_t(i * 71 + media);
      bank->tick(*source, false);
      for (int i = 0; i < 400; ++i) bank->tick(*source, true);
      auto* saved = bank->claim(); assert(saved);
      assert(std::memcmp(saved->bytes.data(), source->buffer(), grainloom::Core::bufferBytes) == 0);
      auto text = grainloom::encode(saved->bytes.data(), saved->bytes.size());
      auto* queued = bank->reserveRestore(); assert(queued);
      assert(grainloom::decode(text.data(), text.size(), queued->bytes.data(), queued->bytes.size()));
      assert(grainloom::crc32(saved->bytes.data(), saved->bytes.size()) == grainloom::crc32(queued->bytes.data(), queued->bytes.size()));
      text[15] = '!';
      assert(!grainloom::decode(text.data(), text.size(), queued->bytes.data(), queued->bytes.size()));
      std::memcpy(queued->bytes.data(), saved->bytes.data(), saved->bytes.size());
      queued->media = saved->media; queued->mono = saved->mono; queued->write = saved->write;
      bank->queueRestore(queued);
      int copied = 0;
      while (bank->restoreTick(*restored)) { ++copied; if (!bank->restoring()) break; }
      assert(copied == 192);
      assert(restored->media() == media && restored->mono() == mono);
      assert(std::memcmp(source->buffer(), restored->buffer(), grainloom::Core::bufferBytes) == 0);
      bank->release(saved);
    }
  }
  bank->cancel();
  auto* older = bank->reserveRestore(); assert(older);
  older->bytes.fill(1); older->media = 3; older->mono = false; older->write = 11;
  bank->queueRestore(older);
  auto* newer = bank->reserveRestore(); assert(newer && newer != older);
  newer->bytes.fill(2); newer->media = 3; newer->mono = false; newer->write = 17;
  bank->queueRestore(newer);
  auto* firstReader = bank->claim(); auto* secondReader = bank->claim();
  assert(firstReader == newer && secondReader == newer);
  for (int i = 0; i < 192; ++i) assert(bank->restoreTick(*restored));
  assert(!bank->pendingRestore() && restored->writePointer() == 17);
  assert(!bank->restoreTick(*restored));
  for (int i = 0; i < grainloom::Core::bufferBytes; ++i) assert(restored->buffer()[i] == 2);
  bank->release(firstReader); bank->release(secondReader);
  std::cout << "Frozen snapshots: all 8 media/channel combinations, exact restore, base64 validation and CRC pass\n";
}
