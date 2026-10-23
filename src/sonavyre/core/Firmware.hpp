#pragma once
#include <rack.hpp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Shared fxaid.app firmware decoder. The firmware WAV is a Mutable Instruments
// stm-audio-bootloader QPSK stream: 48 kHz, 6 kHz carrier, 12 kbit/s, 256-byte
// packets + CRC32, LCG-scrambled payload. fxaidDecodeImage() returns the whole
// descrambled STM32 image; callers locate the bank inside it (the standard
// FX Aid finds a 32×512-byte FV-1 bank; the Pro parses 200 named 640-byte
// records). Used by both FXAid.cpp and FXAidPro.cpp.
// ─────────────────────────────────────────────────────────────────────────────

static inline uint32_t fxaidCrc32(const uint8_t* data, size_t len) {
	static uint32_t table[256];
	static bool init = false;
	if (!init) {
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			table[i] = c;
		}
		init = true;
	}
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < len; i++)
		c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

// Demodulate QPSK audio (mono, 48 kHz, roughly ±1) and descramble into the
// full firmware image. Returns true on success.
static inline bool fxaidDecodeImage(const std::vector<float>& audio,
                                    std::vector<uint8_t>& image, std::string& err) {
	size_t start = 0;
	while (start < audio.size() && std::fabs(audio[start]) < 0.05f)
		start++;
	if (start + 48000 >= audio.size()) {
		err = "No carrier found in file";
		return false;
	}

	const int SYM = 8;
	float sin8[SYM], cos8[SYM];
	for (int j = 0; j < SYM; j++) {
		sin8[j] = std::sin(2.f * M_PI * 6000.f * j / 48000.f);
		cos8[j] = std::cos(2.f * M_PI * 6000.f * j / 48000.f);
	}

	size_t nsym = (audio.size() - start) / SYM;
	std::vector<uint8_t> syms(nsym);
	for (size_t k = 0; k < nsym; k++) {
		const float* p = &audio[start + k * SYM];
		float I = 0.f, Q = 0.f;
		for (int j = 0; j < SYM; j++) {
			Q += p[j] * sin8[j];
			I += p[j] * cos8[j];
		}
		syms[k] = (uint8_t)(((I > 0.f) ? 2 : 0) | ((Q > 0.f) ? 1 : 0));
	}

	// resolve QPSK phase ambiguity: the long leading blank must decode to 0
	static const uint8_t ROTS[4][4] = {
		{0, 1, 2, 3}, {1, 3, 0, 2}, {3, 2, 1, 0}, {2, 0, 3, 1}
	};
	uint8_t blank = syms[10];
	const uint8_t* rot = ROTS[0];
	for (int r = 0; r < 4; r++)
		if (ROTS[r][blank] == 0) { rot = ROTS[r]; break; }
	for (size_t k = 0; k < nsym; k++)
		syms[k] = rot[syms[k]];

	// sync pattern: 0x99 ×4, 0xCC ×4 as 2-bit symbols (MSB first)
	uint8_t sync[32];
	int si = 0;
	for (int b = 0; b < 8; b++) {
		uint8_t byte = (b < 4) ? 0x99 : 0xCC;
		for (int shift = 6; shift >= 0; shift -= 2)
			sync[si++] = (byte >> shift) & 3;
	}

	std::vector<uint8_t> payload;
	size_t k = 0;
	int badCrc = 0;
	while (k + 32 + 1040 <= nsym) {
		bool match = true;
		for (int m = 0; m < 32; m++)
			if (syms[k + m] != sync[m]) { match = false; break; }
		if (!match) { k++; continue; }
		size_t d = k + 32;
		uint8_t bytes[260];
		for (int b = 0; b < 260; b++) {
			bytes[b] = (syms[d] << 6) | (syms[d + 1] << 4) | (syms[d + 2] << 2) | syms[d + 3];
			d += 4;
		}
		uint32_t crc = ((uint32_t)bytes[256] << 24) | ((uint32_t)bytes[257] << 16)
		             | ((uint32_t)bytes[258] << 8) | bytes[259];
		// keep bad packets in place: dropping one shifts the descramble keystream
		if (fxaidCrc32(bytes, 256) != crc)
			badCrc++;
		payload.insert(payload.end(), bytes, bytes + 256);
		k = d;
	}

	if (payload.empty()) {
		err = badCrc ? "All packets failed CRC" : "No data packets found";
		return false;
	}

	// descramble: LCG keystream, seed 0, continuous across packets
	uint32_t state = 0;
	for (size_t i = 0; i < payload.size(); i++) {
		payload[i] ^= (state >> 24) & 0xFF;
		state = state * 1664525u + 1013904223u;
	}

	image.swap(payload);
	err = badCrc ? string::f("%d packets failed CRC", badCrc) : "";
	return true;
}

// Minimal tolerant WAV reader (16/24/32-bit PCM + float32), mono mixdown,
// resampled to 48 kHz.
static inline bool fxaidReadWav48k(const std::string& path, std::vector<float>& out) {
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f) return false;
	auto rd32 = [&]() { uint8_t b[4]; if (std::fread(b, 1, 4, f) != 4) return -1L; return (long)(b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24)); };
	auto rd16 = [&]() { uint8_t b[2]; if (std::fread(b, 1, 2, f) != 2) return -1L; return (long)(b[0] | (b[1] << 8)); };
	char id[5] = {};
	if (std::fread(id, 1, 4, f) != 4 || std::strncmp(id, "RIFF", 4)) { std::fclose(f); return false; }
	rd32();
	if (std::fread(id, 1, 4, f) != 4 || std::strncmp(id, "WAVE", 4)) { std::fclose(f); return false; }
	int channels = 1, bits = 16, format = 1, sampleRate = 48000;
	bool gotFmt = false;
	std::vector<float> raw;
	while (std::fread(id, 1, 4, f) == 4) {
		long sz = rd32();
		if (sz < 0) break;
		if (!std::strncmp(id, "fmt ", 4)) {
			format = (int)rd16();
			channels = (int)rd16();
			sampleRate = (int)rd32();
			rd32(); rd16();
			bits = (int)rd16();
			if (sz > 16) std::fseek(f, sz - 16, SEEK_CUR);
			gotFmt = true;
		}
		else if (!std::strncmp(id, "data", 4) && gotFmt) {
			int bytesPer = bits / 8;
			long frames = sz / (bytesPer * channels);
			raw.reserve(frames);
			std::vector<uint8_t> buf(bytesPer * channels);
			for (long i = 0; i < frames; i++) {
				if (std::fread(buf.data(), 1, buf.size(), f) != buf.size()) break;
				float acc = 0.f;
				for (int c = 0; c < channels; c++) {
					const uint8_t* p = &buf[c * bytesPer];
					float v = 0.f;
					if (format == 3 && bits == 32) { float fv; std::memcpy(&fv, p, 4); v = fv; }
					else if (bits == 16) { int16_t s = (int16_t)(p[0] | (p[1] << 8)); v = s / 32768.f; }
					else if (bits == 24) { int32_t s = p[0] | (p[1] << 8) | (p[2] << 16); if (s & 0x800000) s -= 0x1000000; v = s / 8388608.f; }
					else if (bits == 32) { int32_t s; std::memcpy(&s, p, 4); v = s / 2147483648.f; }
					acc += v;
				}
				raw.push_back(acc / channels);
			}
			break;
		}
		else {
			std::fseek(f, sz + (sz & 1), SEEK_CUR);
		}
	}
	std::fclose(f);
	if (raw.empty()) return false;
	if (sampleRate == 48000) { out.swap(raw); return true; }
	double ratio = 48000.0 / sampleRate;
	out.resize((size_t)(raw.size() * ratio));
	for (size_t i = 0; i < out.size(); i++) {
		double x = i / ratio;
		size_t i0 = (size_t)x;
		float fr = (float)(x - i0);
		float a = raw[std::min(i0, raw.size() - 1)];
		float b = raw[std::min(i0 + 1, raw.size() - 1)];
		out[i] = a + (b - a) * fr;
	}
	return true;
}
