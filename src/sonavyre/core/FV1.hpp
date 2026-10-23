#pragma once
#include <cstdint>
#include <cmath>
#include <cstring>

// A Spin Semiconductor FV-1 virtual machine.
// Runs 128-instruction programs against 32768 samples of delay RAM at a
// nominal 32768 Hz sample rate. Written from the FV-1 datasheet / AN-0001
// semantics; instruction bit layout matches the SpinASM/asfv1 encoding
// (32-bit big-endian words, opcode in bits [4:0]).

struct FV1 {
	static const int RAM_SIZE = 32768;
	static const int PROG_LEN = 128;

	uint32_t prog[PROG_LEN] = {};
	float ram[RAM_SIZE] = {};

	// registers 0x00..0x3F (POT0=0x10.. DACR=0x17, REG0=0x20..)
	float regs[64] = {};

	float acc = 0.f;
	float pacc = 0.f;
	float lr = 0.f;   // last delay-ram read
	int dp = 0;       // decrementing delay pointer
	bool firstRun = true;

	// LFOs
	float sinPhase[2] = {0.f, 0.f};
	uint32_t rampPos[2] = {0, 0};
	// values latched by CHO ... REG
	float choLfo = 0.f;

	void clearState() {
		std::memset(ram, 0, sizeof(ram));
		std::memset(regs, 0, sizeof(regs));
		acc = pacc = lr = 0.f;
		dp = 0;
		sinPhase[0] = sinPhase[1] = 0.f;
		rampPos[0] = rampPos[1] = 0;
		firstRun = true;
	}

	void loadProgram(const uint32_t* p) {
		std::memcpy(prog, p, sizeof(prog));
		clearState();
	}

	static inline float s1_14(uint32_t v) { // 16-bit, -2..+2
		int32_t x = (int32_t)(v & 0xFFFF);
		if (x & 0x8000) x -= 0x10000;
		return x / 16384.f;
	}
	static inline float s1_9(uint32_t v) { // 11-bit, -2..+2
		int32_t x = (int32_t)(v & 0x7FF);
		if (x & 0x400) x -= 0x800;
		return x / 512.f;
	}
	static inline float s_10(uint32_t v) { // 11-bit, -1..+1
		int32_t x = (int32_t)(v & 0x7FF);
		if (x & 0x400) x -= 0x800;
		return x / 1024.f;
	}
	static inline float s4_6(uint32_t v) { // 11-bit, -16..+16
		int32_t x = (int32_t)(v & 0x7FF);
		if (x & 0x400) x -= 0x800;
		return x / 64.f;
	}

	inline float readRam(int addr) {
		float v = ram[(dp + addr) & (RAM_SIZE - 1)];
		lr = v;
		return v;
	}
	inline void writeRam(int addr, float v) {
		// delay RAM is 14-bit; it saturates at full scale
		if (v > 0.99993896f) v = 0.99993896f;
		if (v < -1.f) v = -1.f;
		ram[(dp + addr) & (RAM_SIZE - 1)] = v;
	}
	// fractional read with linear interpolation (for CHO)
	inline float readRamF(float addr) {
		int i0 = (int)std::floor(addr);
		float f = addr - i0;
		float a = ram[(dp + i0) & (RAM_SIZE - 1)];
		float b = ram[(dp + i0 + 1) & (RAM_SIZE - 1)];
		float v = a + (b - a) * f;
		lr = v;
		return v;
	}

	// ramp LFO helpers. rampPos advances by freq (16-bit signed) per sample;
	// the pointer wraps over the selected range (512/1024/2048/4096).
	inline int rampRange(int n) const {
		int sel = (int)(regs[0x05 + 2 * n] * 32768.f + 0.5f);
		// range register is written as 0.25/0.5/1.0 style fractions by
		// programs; the hardware ranges are 4096, 2048, 1024, 512.
		if (sel >= 32767) return 4096;
		if (sel >= 16384) return 2048;
		if (sel >= 8192) return 1024;
		return 512;
	}

	// One sample. Pots 0..1. Audio nominal -1..1.
	void run(float inL, float inR, float pot0, float pot1, float pot2,
	         float& outL, float& outR) {
		regs[0x10] = pot0;
		regs[0x11] = pot1;
		regs[0x12] = pot2;
		regs[0x14] = clip(inL); // the ADC saturates at full scale
		regs[0x15] = clip(inR);

		acc = 0.f;
		pacc = 0.f;

		int pc = 0;
		while (pc < PROG_LEN) {
			uint32_t w = prog[pc];
			pc++;
			uint32_t op = w & 0x1F;
			float prevAcc = acc;

			switch (op) {
				case 0x00: { // RDA C,A : acc += ram[A]*C
					int addr = (w >> 5) & 0x7FFF;
					float c = s1_9(w >> 21);
					acc += readRam(addr) * c;
				} break;
				case 0x01: { // RMPA C : acc += ram[ADDR_PTR]*C
					float c = s1_9(w >> 21);
					int addr = ((int)(regs[0x18] * 8388608.f)) >> 8;
					acc += readRam(addr & (RAM_SIZE - 1)) * c;
				} break;
				case 0x02: { // WRA A,C : ram[A]=acc, acc*=C
					int addr = (w >> 5) & 0x7FFF;
					float c = s1_9(w >> 21);
					writeRam(addr, acc);
					acc *= c;
				} break;
				case 0x03: { // WRAP A,C : ram[A]=acc, acc=acc*C+LR
					int addr = (w >> 5) & 0x7FFF;
					float c = s1_9(w >> 21);
					writeRam(addr, acc);
					acc = acc * c + lr;
				} break;
				case 0x04: { // RDAX REG,C
					int r = (w >> 5) & 0x3F;
					acc += regs[r] * s1_14(w >> 16);
				} break;
				case 0x05: { // RDFX REG,C : acc = (acc-reg)*C + reg
					int r = (w >> 5) & 0x3F;
					float c = s1_14(w >> 16);
					acc = (acc - regs[r]) * c + regs[r];
				} break;
				case 0x06: { // WRAX REG,C : reg=acc, acc*=C
					int r = (w >> 5) & 0x3F;
					regs[r] = acc;
					acc *= s1_14(w >> 16);
				} break;
				case 0x07: { // WRHX REG,C : reg=acc, acc=acc*C+pacc
					int r = (w >> 5) & 0x3F;
					regs[r] = acc;
					acc = acc * s1_14(w >> 16) + pacc;
				} break;
				case 0x08: { // WRLX REG,C : reg=acc, acc=(pacc-acc)*C+pacc
					int r = (w >> 5) & 0x3F;
					regs[r] = acc;
					acc = (pacc - acc) * s1_14(w >> 16) + pacc;
				} break;
				case 0x09: { // MAXX REG,C : acc = max(|reg*C|,|acc|)
					int r = (w >> 5) & 0x3F;
					float m = std::fabs(regs[r] * s1_14(w >> 16));
					float a = std::fabs(acc);
					acc = (m > a) ? m : a;
				} break;
				case 0x0A: { // MULX REG
					int r = (w >> 5) & 0x3F;
					acc *= regs[r];
				} break;
				case 0x0B: { // LOG C,D : acc = C*log2(|acc|)/16 + D
					float c = s1_14(w >> 16);
					float d = s4_6(w >> 5);
					float a = std::fabs(acc);
					if (a < 1e-9f) a = 1e-9f;
					acc = c * (std::log2(a) / 16.f) + d;
				} break;
				case 0x0C: { // EXP C,D : acc = C*exp2(acc*16) + D
					float c = s1_14(w >> 16);
					float d = s_10(w >> 5);
					float e = acc * 16.f;
					if (e > 0.f) e = 0.f; // acc>=0 saturates at 1.0 on hw
					acc = c * std::exp2(e) + d;
				} break;
				case 0x0D: { // SOF C,D : acc = C*acc + D
					acc = s1_14(w >> 16) * acc + s_10(w >> 5);
				} break;
				case 0x0E: { // AND M
					int32_t a = clip24((int32_t)(clip(acc) * 8388607.f));
					acc = andMask(a, (w >> 8) & 0xFFFFFF);
				} break;
				case 0x0F: { // OR M
					int32_t a = clip24((int32_t)(clip(acc) * 8388607.f));
					uint32_t u = ((uint32_t)a & 0xFFFFFF) | ((w >> 8) & 0xFFFFFF);
					acc = fromI24(u);
				} break;
				case 0x10: { // XOR M
					int32_t a = clip24((int32_t)(clip(acc) * 8388607.f));
					uint32_t u = ((uint32_t)a & 0xFFFFFF) ^ ((w >> 8) & 0xFFFFFF);
					acc = fromI24(u);
				} break;
				case 0x11: { // SKP FLAGS,N
					uint32_t flags = (w >> 27) & 0x1F;
					int n = (w >> 21) & 0x3F;
					bool skip = true; // SKP 0,N == unconditional JMP
					if (flags) {
						skip = false;
						if ((flags & 0x10) && !firstRun) skip = true;              // RUN
						if ((flags & 0x08) && ((acc < 0.f) != (pacc < 0.f))) skip = true; // ZRC
						if ((flags & 0x04) && std::fabs(acc) < (1.f / 8388608.f)) skip = true; // ZRO
						if ((flags & 0x02) && acc >= 0.f) skip = true;             // GEZ
						if ((flags & 0x01) && acc < 0.f) skip = true;              // NEG
					}
					else if (n == 0) {
						skip = false; // NOP (SKP 0,0)
					}
					if (skip) pc += n;
				} break;
				case 0x12: { // WLDS / WLDR
					if ((w >> 30) & 1) { // WLDR
						int n = (w >> 29) & 1;
						int32_t f = (int32_t)((w >> 13) & 0xFFFF);
						if (f & 0x8000) f -= 0x10000;
						int sel = (w >> 5) & 0x3; // 0=4096 1=2048 2=1024 3=512
						regs[0x04 + 2 * n] = f / 32768.f;
						regs[0x05 + 2 * n] = sel / 4.f;
						rampSel[n] = sel;
						rampMask[n] = 0x3FFFFFu >> sel;
						rampPos[n] = 0;
					}
					else { // WLDS
						int n = (w >> 29) & 1;
						int f = (w >> 20) & 0x1FF;
						int a = (w >> 5) & 0x7FFF;
						regs[0x00 + 2 * n] = f / 512.f;
						regs[0x01 + 2 * n] = a / 32768.f;
						sinPhase[n] = 0.f;
					}
				} break;
				case 0x13: { // JAM N
					int n = (w >> 6) & 1;
					rampPos[n] = 0;
				} break;
				case 0x14: { // CHO
					int type = (w >> 30) & 0x3;   // 0=RDA 2=SOF 3=RDAL
					int nsel = (w >> 21) & 0x3;   // SIN0/SIN1/RMP0/RMP1
					int flags = (w >> 24) & 0x3F; // SIN/COS/REG/COMPC/COMPA/RPTR2/NA
					int addr = (w >> 5) & 0xFFFF;
					doCho(type, nsel, flags, addr);
				} break;
				default:
					break;
			}
			pacc = prevAcc;
			if (!std::isfinite(acc)) acc = 0.f;
			// the FV-1 ALU saturates the S.23 accumulator at full scale;
			// reverb programs depend on this to bound their feedback loops
			if (acc > 0.99999988f) acc = 0.99999988f;
			if (acc < -1.f) acc = -1.f;
		}

		outL = clip(regs[0x16]);
		outR = clip(regs[0x17]);

		// advance time: pointer decrements, LFOs advance
		dp = (dp - 1) & (RAM_SIZE - 1);
		for (int n = 0; n < 2; n++) {
			// sin lfo: phase inc = RATE/2^17 rad/sample (RATE 0..511 ~ 0..20 Hz).
			// Programs modulate rate/amp by writing the LFO registers live.
			float rate = regs[0x00 + 2 * n] * 512.f;
			sinPhase[n] += rate / 131072.f;
			if (sinPhase[n] > 2.f * (float)M_PI) sinPhase[n] -= 2.f * (float)M_PI;
			if (sinPhase[n] < 0.f) sinPhase[n] += 2.f * (float)M_PI;
			// ramp lfo: 22-bit position decremented by rate>>12 (hardware exact)
			int32_t rraw = (int32_t)std::lround(regs[0x04 + 2 * n] * 8388608.f);
			rampPos[n] = (rampPos[n] - (uint32_t)(rraw >> 12)) & rampMask[n];
		}
		firstRun = false;
	}

	int rampSel[2] = {0, 0};
	uint32_t rampMask[2] = {0x3FFFFFu, 0x3FFFFFu};

	static inline float clip(float v) {
		if (v > 0.99999f) return 0.99999f;
		if (v < -1.f) return -1.f;
		return v;
	}
	static inline int32_t clip24(int32_t a) {
		if (a > 8388607) return 8388607;
		if (a < -8388608) return -8388608;
		return a;
	}
	static inline float fromI24(uint32_t u) {
		int32_t x = (int32_t)(u & 0xFFFFFF);
		if (x & 0x800000) x -= 0x1000000;
		return x / 8388608.f;
	}
	static inline float andMask(int32_t a, uint32_t m) {
		uint32_t u = ((uint32_t)a & 0xFFFFFF) & m;
		return fromI24(u);
	}

	// CHO semantics per measured hardware behavior (ndf-zz/fv1testing):
	// - LFO value (S.23): sin = sin*amp/32768 (RDAL full range +/-1),
	//   ramp = 22-bit position (RDAL range 0..0.5)
	// - address offset = value>>10 (so sine depth = amp/4 delay addresses)
	// - interpolation coefficient = the value's low 10 bits as a 0..1 frac;
	//   COMPC gives 1-frac (paired taps sum to unity)
	// - ramp NA crossfade sweeps 0..1
	void doCho(int type, int nsel, int flags, int addrRaw) {
		bool isRamp = (nsel >= 2);
		int rn = nsel & 1;
		float lfoF;   // LFO value, normalized S.23
		float coeff;  // interpolation / crossfade coefficient (0..1)
		int off;      // delay address offset

		if (!isRamp) {
			// hardware sin starts at (sin,cos) = (0,-1)
			float amp = regs[0x01 + 2 * rn];
			float s = (flags & 0x01) ? -std::cos(sinPhase[rn]) : std::sin(sinPhase[rn]);
			lfoF = s * amp;
			float v = lfoF;
			if (flags & 0x08) v = -v;                       // COMPA
			float a = v * 8192.f;                           // value>>10 domain
			float fl = std::floor(a);
			off = (int)fl;
			coeff = a - fl;                                 // low bits as 0..1 frac
		}
		else {
			uint32_t mask = rampMask[rn];
			uint32_t pos = rampPos[rn] & mask;
			uint32_t p = (flags & 0x10) ? ((pos + ((mask + 1) >> 1)) & mask) : pos; // RPTR2
			if (flags & 0x08) p = mask - p;                 // COMPA
			lfoF = p / 8388608.f;
			off = (int)(p >> 10);
			coeff = (p & 0x3FF) / 1024.f;
			if (flags & 0x20) {                             // NA: 0..1 crossfade
				uint32_t half = mask >> 1;
				uint32_t xf = (p > half) ? (mask - p) : p;
				coeff = (float)(xf << rampSel[rn]) / 2097152.f;
				off = 0;
			}
		}
		float scale = (flags & 0x04) ? (1.f - coeff) : coeff; // COMPC

		if (type == 0) { // CHO RDA
			acc += readRam(addrRaw + off) * scale;
		}
		else if (type == 2) { // CHO SOF
			acc = acc * scale + s_10(addrRaw);
		}
		else if (type == 3) { // CHO RDAL
			acc = lfoF;
		}
	}
};
