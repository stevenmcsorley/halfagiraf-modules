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
  enum { FREE, WRITING, READY, READING, RESTORE };
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
      if (writing_ >= 0) { slots[writing_].state.store(FREE, std::memory_order_release); writing_ = -1; }
      offset_ = 0; stableBlocks_ = 0; captured_ = false;
      return;
    }
    if (captured_ || ++stableBlocks_ < 100) return;
    if (writing_ < 0) {
      for (int i = 0; i < 3; ++i) {
        int expected = FREE;
        if (slots[i].state.compare_exchange_strong(expected, WRITING)) { writing_ = i; break; }
      }
      if (writing_ < 0) {
        for (int i = 0; i < 3; ++i) {
          int expected = READY;
          if (slots[i].state.compare_exchange_strong(expected, WRITING)) { writing_ = i; break; }
        }
      }
      if (writing_ < 0) return;
      slots[writing_].media = core.media(); slots[writing_].mono = core.mono();
      slots[writing_].write = core.writePointer(); offset_ = 0;
    }
    constexpr size_t chunk = 4096;
    std::memcpy(slots[writing_].bytes.data() + offset_, core.buffer() + offset_, chunk);
    offset_ += chunk;
    if (offset_ == Core::bufferBytes) {
      slots[writing_].generation = ++generation_;
      slots[writing_].state.store(READY, std::memory_order_release);
      writing_ = -1; captured_ = true;
    }
  }
  Slot* claim() {
    int newest = -1; uint32_t generation = 0;
    for (int i = 0; i < 3; ++i) {
      int expected = READY;
      if (slots[i].state.compare_exchange_strong(expected, READING)) {
        if (slots[i].generation >= generation) {
          if (newest >= 0) slots[newest].state.store(READY, std::memory_order_release);
          newest = i; generation = slots[i].generation;
        } else slots[i].state.store(READY, std::memory_order_release);
      }
    }
    return newest < 0 ? nullptr : &slots[newest];
  }
  void release(Slot* slot) { slot->state.store(READY, std::memory_order_release); }
  Slot* reserveRestore() {
    for (auto& slot : slots) {
      int expected = FREE;
      if (slot.state.compare_exchange_strong(expected, WRITING)) return &slot;
    }
    return nullptr;
  }
  bool restoreTick(Core& core) {
    if (restoring_ < 0) {
      for (int i = 0; i < 3; ++i) {
        int expected = RESTORE;
        if (slots[i].state.compare_exchange_strong(expected, READING)) {
          restoring_ = i; restoreOffset_ = 0;
          core.restore(slots[i].media, slots[i].mono, slots[i].write);
          break;
        }
      }
    }
    if (restoring_ < 0) return false;
    std::memcpy(core.buffer() + restoreOffset_, slots[restoring_].bytes.data() + restoreOffset_, 4096);
    restoreOffset_ += 4096;
    if (restoreOffset_ == Core::bufferBytes) {
      slots[restoring_].generation = ++generation_;
      slots[restoring_].state.store(READY, std::memory_order_release);
      restoring_ = -1; captured_ = true;
    }
    return true;
  }
  bool restoring() const { return restoring_ >= 0; }
private:
  int writing_ = -1, restoring_ = -1, stableBlocks_ = 0;
  size_t offset_ = 0, restoreOffset_ = 0;
  uint32_t generation_ = 0;
  bool captured_ = false;
};
}
