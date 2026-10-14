#pragma once
#include "GrainloomCore.hpp"
#include <atomic>
#include <string>
#include <vector>

namespace grainloom {
inline uint32_t crc32(const uint8_t* bytes, size_t size) {
  uint32_t crc = ~uint32_t(0);
  for (size_t i = 0; i < size; ++i) {
    crc ^= bytes[i];
    for (int j = 0; j < 8; ++j) crc = (crc >> 1) ^ (0xedb88320u & uint32_t(-int32_t(crc & 1)));
  }
  return ~crc;
}
inline std::string encode(const uint8_t* bytes, size_t size) {
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string text; text.reserve((size + 2) / 3 * 4);
  for (size_t i = 0; i < size; i += 3) {
    uint32_t word = uint32_t(bytes[i]) << 16;
    if (i + 1 < size) word |= uint32_t(bytes[i + 1]) << 8;
    if (i + 2 < size) word |= bytes[i + 2];
    text += alphabet[(word >> 18) & 63]; text += alphabet[(word >> 12) & 63];
    text += i + 1 < size ? alphabet[(word >> 6) & 63] : '=';
    text += i + 2 < size ? alphabet[word & 63] : '=';
  }
  return text;
}
inline bool decode(const char* text, size_t length, uint8_t* bytes, size_t size) {
  if (!text || length != (size + 2) / 3 * 4 || size % 3 != 0) return false;
  size_t out = 0;
  for (size_t i = 0; i < length; i += 4) {
    uint32_t word = 0;
    for (int j = 0; j < 4; ++j) {
      unsigned char c = text[i + j];
      int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26
        : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
      if (v < 0) return false;
      word = (word << 6) | uint32_t(v);
    }
    bytes[out++] = uint8_t(word >> 16); bytes[out++] = uint8_t(word >> 8); bytes[out++] = uint8_t(word);
  }
  return out == size;
}

// Three owned slots: the callback writes one, serialization claims an immutable
// completed slot, restoration queues a third. No callback waits on a reader.
class SnapshotBank {
public:
  // Low bits are ownership; upper bits count immutable readers. Restore and
  // serialization may read the same payload, but a writer requires zero readers.
  enum { FREE = 0, WRITING = 1, READY = 2, RESTORE = 4, RESTORING = 8,
    FLAG_MASK = 15, READER_ONE = 256 };
  struct Slot {
    std::atomic<int> state{FREE};
    std::array<uint8_t, Core::bufferBytes> bytes{};
    int media = 3, write = 0;
    bool mono = false;
    uint32_t generation = 0;
  };
  std::array<Slot, 3> slots;

  void tick(Core& core, bool frozen) {
    if (!frozen || core.synthMode()) {
      for (auto& slot : slots) {
        int expected = READY;
        slot.state.compare_exchange_strong(expected, FREE);
      }
      captured_ = false;
      return;
    }
    if (captured_) return;
    Slot* target = nullptr;
    for (int pass = 0; pass < 2 && !target; ++pass) {
      for (auto& slot : slots) {
        int expected = pass == 0 ? FREE : READY;
        if (slot.state.compare_exchange_strong(expected, WRITING)) { target = &slot; break; }
      }
    }
    if (!target) return;
    target->media = core.media(); target->mono = core.mono(); target->write = core.writePointer();
    // Fixed, preallocated copy at the first fully frozen DSP block. No CRC,
    // encoding or serialization occurs here, and immediate saves retain audio.
    std::memcpy(target->bytes.data(), core.buffer(), Core::bufferBytes);
    target->generation = nextGeneration_.fetch_add(1) + 1;
    target->state.store(READY, std::memory_order_release);
    latest_.store(target->generation, std::memory_order_release);
    captured_ = true;
  }
  Slot* claim() {
    uint32_t wanted = latest_.load(std::memory_order_acquire);
    for (auto& slot : slots) {
      for (int attempt = 0; attempt < 8; ++attempt) {
        int expected = slot.state.load(std::memory_order_acquire);
        int flag = expected & FLAG_MASK;
        if ((flag != READY && flag != RESTORE && flag != RESTORING) || (expected >> 8) >= 65535) break;
        if (slot.state.compare_exchange_strong(expected, expected + READER_ONE)) {
          if (slot.generation == wanted) return &slot;
          release(&slot);
          break;
        }
      }
    }
    return nullptr;
  }
  void release(Slot* slot) { slot->state.fetch_sub(READER_ONE, std::memory_order_release); }

  Slot* reserveRestore() {
    for (int flag : {FREE, READY}) {
      for (auto& slot : slots) {
        int expected = flag;
        if (slot.state.compare_exchange_strong(expected, WRITING)) return &slot;
      }
    }
    return nullptr;
  }
  void queueRestore(Slot* slot) {
    slot->generation = nextGeneration_.fetch_add(1) + 1;
    slot->state.store(RESTORE, std::memory_order_release);
    latest_.store(slot->generation, std::memory_order_release);
  }
  bool restoreTick(Core& core) {
    if (restoring_ < 0) {
      int newest = -1;
      uint32_t generation = 0;
      for (int i = 0; i < 3; ++i) {
        if ((slots[i].state.load(std::memory_order_acquire) & FLAG_MASK) == RESTORE
          && slots[i].generation >= generation) { newest = i; generation = slots[i].generation; }
      }
      if (newest < 0) return false;
      // Multiple rapid patch loads must not replay an older queued recording
      // after the newest one. Existing readers keep their immutable payload.
      for (int i = 0; i < 3; ++i) {
        if (i != newest && (slots[i].state.load(std::memory_order_acquire) & FLAG_MASK) == RESTORE
          && slots[i].generation <= generation)
          slots[i].state.fetch_xor(RESTORE ^ READY, std::memory_order_acq_rel);
      }
      // Only this callback changes the low bits after queue publication.
      // Concurrent serializers change only the upper reader count.
      slots[newest].state.fetch_xor(RESTORE ^ RESTORING, std::memory_order_acq_rel);
      restoring_ = newest; restoreOffset_ = 0;
      core.restore(slots[newest].media, slots[newest].mono, slots[newest].write);
    }
    std::memcpy(core.buffer() + restoreOffset_, slots[restoring_].bytes.data() + restoreOffset_, 4096);
    restoreOffset_ += 4096;
    if (restoreOffset_ == Core::bufferBytes) {
      slots[restoring_].state.fetch_xor(RESTORING ^ READY, std::memory_order_release);
      restoring_ = -1; captured_ = true;
    }
    return true;
  }
  bool restoring() const { return restoring_ >= 0; }
  bool pendingRestore() const {
    for (const auto& slot : slots) {
      int flag = slot.state.load(std::memory_order_acquire) & FLAG_MASK;
      if (flag == RESTORE || flag == RESTORING) return true;
    }
    return false;
  }
  void cancel() {
    restoring_ = -1; restoreOffset_ = 0; captured_ = false;
    for (auto& slot : slots) {
      int word = slot.state.load(std::memory_order_acquire);
      int flag = word & FLAG_MASK;
      if (flag == RESTORE || flag == RESTORING)
        slot.state.fetch_xor(flag ^ READY, std::memory_order_acq_rel);
      int expected = READY;
      slot.state.compare_exchange_strong(expected, FREE);
    }
  }
private:
  int restoring_ = -1;
  size_t restoreOffset_ = 0;
  std::atomic<uint32_t> nextGeneration_{0}, latest_{0};
  bool captured_ = false;
};
}
