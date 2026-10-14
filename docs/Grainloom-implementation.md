# Grainloom implementation record

Worktree: `D:/VCV_dev/halfagiraf-grainloom`; branch: `feature/grainloom-beads-port`.
Base: published halfagiraf 2.0.2, commit `3541f29430eeca862c3f68d0306fe6c75f43f083`.

## Sources and licence

Beads: https://github.com/pichenettes/eurorack/tree/d8f473915232af74347762b6365202bfd26f73f6/beads
stmlib: https://github.com/pichenettes/stmlib/tree/e3bd7c9cc00e4364166f9905c0509b6ffd0535ec
The Beads DSP, resources and stmlib dependencies retain Emilie Gillet's MIT notices.
The surrounding halfagiraf plugin remains GPL-3.0-or-later. Hardware artwork is excluded.
`vendor/mutable/provenance.json` records each imported file and its original SHA-256.
Hardware firmware is retained for mapping reference but is excluded from compilation.

## Stage 1 results

Existing deterministic tests and the Windows Rack SDK 2.6.6 package build pass.
`make grainloom-upstream-test` compiles the original DSP without changing its source.
It calls the original grain-fade assertions, trigger generator, trigger FM, audio-rate
trigger, Processor and Synth render tests. Generated fixture audio substitutes for
unavailable external music files. These renders are test artefacts, not sonic-parity
evidence. The original main function disables all tests, so a separate runner calls them.
Modern compiler flags supply `<cstdio>`, Windows math constants and the upstream
`TEST` portable arithmetic path. The upstream makefile has an obsolete warning flag.

Compiled sources: processor, synth, delay_clock, delay_controller, trigger_generator,
mu_law, resources, stmlib random and units. The DSP header dependency closure contains
all grain/player/envelope, packed delay storage, filters, reverb, wow/flutter, limiter,
randomisation and wavetable components. Gain control needs a host codec frame shim.
STM32 GPIO, ADC, DMA, codec driver, flash, interrupts and normalization probing are
hardware services to be replaced by the host bridge.

## Wavetable discrepancy

The manual lists eight banks. Released `Synth::Init` initializes its quantizer with
`kNumBanks = 12`; `Synth::Render` feeds the feedback parameter directly to that
quantizer. Resources include 192 waves (12 x 16), with A-H plus four FM banks I-L.
There is no UI restriction to eight in the released firmware. Grainloom follows this
source mapping; all twelve are intended to be reachable.

## Verification still required

Host adapter, UI, persistence, audio comparisons, stress tests and platform builds
are subsequent stages. Sonic parity and production readiness are not established.

## Host bridge and state implementation

`GrainloomCore.hpp` owns the original 3 x 256 KiB bank allocation and 32 Kiword
reverb store. Processor, Synth and GainControl use the firmware's zero-initialized
object layout. The DSP runs 12, 8 or 6 frames per block at 48, 32 or 24 kHz;
Synth always runs 48 kHz. Clean tape retains its separate internal 2:1 converters.

`GrainloomResampler.hpp` uses fixed 128-tap Blackman-windowed sinc tables, 512
fractional phases and a 2048-frame tagged history. Both channels share timestamps.
Kernel generation and state reset use bounded loops and no heap operations.
No rate converter is allocated on the callback. Host-rate changes reset only the
bridge; original media changes reset the selected DSP configuration as in firmware.
Equal rates bypass conversion. CV mapping follows `beads.cc`, `cv_reader.h` and
settings calibration constants; hardware ADC/noise/probe circuitry is omitted.

The bridge samples pots/CV at 4 kHz, supplies the original per-sample freeze and
gate arrays and preserves the 2.5 ms SEED input delay. Its non-native audio/gate
history has 64 input samples of delay; output conversion adds 64 native samples
and one DSP block. Continuous control values use the latest 4 kHz control snapshot.
Their phase relative to input conversion is a documented host difference.

Snapshots use three preallocated ownership slots, acquired with atomic compare
and exchange. After 25 ms stable freeze, 4096 bytes per block copy the recording;
192 blocks finish in 48 ms. Serialization owns an immutable slot and performs
base64/CRC/JSON on the caller thread. Restore validates exact lengths, format,
media, mono flag, write range and CRC before queuing. The engine restores 4096
bytes per block while muted. No compression is performed. Format v1 is the
little-endian packed Beads allocation (including its float freeze tail); every
supported architecture is little-endian with IEEE-754 float. Running grains and
reverb state are intentionally restarted, matching the scope of hardware backup.

## Imported-source changes

- `stmlib/stmlib.h` and `stmlib/dsp/dsp.h`: `GRAINLOOM_HOST` selects the existing
  portable arithmetic and disables STM32 RAM section attributes, without enabling
  the upstream TEST debug logging on the audio thread.
- `stmlib/utils/random.h/.cc`: host RNG storage uses a thread-local accessor. The
  wrapper restores/saves each module's original LCG state around each DSP block.
  Generator constants and calls are preserved. A first static-member TLS version
  triggered GCC UBSan diagnostics; the accessor version passes the tested matrix.
- `beads/gain_control.h`: host builds supply the codec's two int32 frame fields;
  the gain algorithm itself is unmodified.
- `wavetable_grain.h` and `wavetable_voice.h`: skip the successor-wave read when
  the interpolation weight is exactly zero. This avoids reading past the last
  waveform of bank 12 and preserves the intended interpolation result.
- MinGW compiles only Grainloom and its imported sources with `-mno-ms-bitfields`;
  native 12-bit stereo and mono bundle sizes are asserted as 3 bytes. Without it,
  Windows used 4-byte storage and crashed in vintage/tape processing.

Original hashes remain in provenance.json; git diffs identify the host patches.
Other module DSP and compiler flags are unchanged.

## Evidence collected locally

- `make test`: all ten existing deterministic suites pass.
- `make grainloom-upstream-test`: original grain fade assertions and six render
  routines complete using generated fixtures. Most upstream tests are renders,
  so completion is not a quality or parity assertion.
- `make grainloom-test`: all 20 host-rate/media cases produce finite bounded
  audio, about 4000 blocks/second, and automatic ten-second synth transition.
- 48 kHz bridge versus direct native Core: sample-identical after 12 frames.
- Independent `reference_render.cpp` build against the pristine pinned upstream
  checkout equals the host render byte-for-byte across all four media, freeze,
  delay transition, reverb, randomisation and all twelve wavetable banks.
  SHA-256: `1604f71423ee941f7f25f3d17738bd76546d16c5a082d4f2c0e2aad0bf33a533`.
- SRC test: 1 kHz passband vs 17 kHz stopband while converting to 24 kHz,
  measured rejection 75.4-110.0 dB over the five required host rates.
- Snapshot test: all eight quality/channel combinations restore exact bytes;
  malformed base64 and CRC checks are exercised.
- Rack-linked Windows test: module creation, live DSP, freeze JSON saving/reload
  and extreme feedback stay bounded and pass.
- Linux GCC AddressSanitizer/UndefinedBehaviorSanitizer matrix passed after the
  TLS correction. Further boundary/stress scenarios are being added.
- Windows SDK 2.6.6 plugin DLL and .vcvplugin package build successfully.

Adapter storage is 1,494,304 bytes; three snapshot slots add approximately
2.36 MiB. Total persistent per-module DSP/state storage is approximately 3.68 MiB,
excluding Rack widgets. The Windows offline rate/media matrix ran in about
1.55 seconds for 52 seconds of simulated audio, including construction and rate
setup, on this workstation. That is an aggregate offline measurement, not Rack's
live per-module CPU meter or worst-case callback timing.

## Acceptance work still outstanding

Cross-platform CI, extended timing/pitch and trigger-distribution tests, musical
fixture comparisons beyond sine input, long-session/multi-thread stress,
module reset/persistence edge cases, and final panel
review remain. No physical Beads recording or measured analog codec reference
has been used, so hardware-level sonic parity is not yet certified.
