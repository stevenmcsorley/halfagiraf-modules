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

## Host bridge and state implementation

`GrainloomCore.hpp` owns the original 3 x 256 KiB bank allocation and 32 Kiword
reverb store. Processor, Synth and GainControl use the firmware's zero-initialized
object layout. The DSP runs 12, 8 or 6 frames per block at 48, 32 or 24 kHz;
Synth always runs 48 kHz. Clean tape retains its separate internal 2:1 converters.

`GrainloomResampler.hpp` uses fixed 128-tap Blackman-windowed sinc tables, 512
fractional phases and a 2048-frame tagged history. Both channels share timestamps.
All three internal-rate converter pairs are precomputed on host-rate setup;
quality changes reset histories rather than regenerating kernels.
Kernel generation and state reset use bounded loops and no heap operations.
No rate converter is allocated on the callback. Host-rate changes reset only the
bridge; original media changes reset the selected DSP configuration as in firmware.
Equal rates bypass conversion. CV mapping follows `beads.cc`, `cv_reader.h` and
settings calibration constants; hardware ADC/noise/probe circuitry is omitted.
Rate changes preserve Schmitt gate state and supply the held state while new
resampling history fills. A five-rate regression test verifies that a frozen
recording and write pointer are unchanged, even with gates between thresholds.
The released firmware disables `ENABLE_ADVANCED_FEATURES`; its experimental
monophonic-grain and trim combinations are therefore not exposed as hardware modes.

The bridge samples pots/CV at 4 kHz, supplies the original per-sample freeze and
gate arrays and preserves the 2.5 ms SEED input delay. Its non-native audio/gate
history has 64 input samples of delay; output conversion adds 64 native samples
and one DSP block. Continuous control values use the latest 4 kHz control snapshot.
Their phase relative to input conversion is a documented host difference.

Snapshots use three preallocated ownership slots, acquired with atomic compare
and exchange. The first fully frozen DSP block makes one fixed 786432-byte copy
before publishing its saved state. The largest observed copy in the local
100-freeze test was 30.2 microseconds; this is not a universal worst-case bound.
Serialization pins an immutable slot with an atomic reader count and performs
base64/CRC/JSON on the caller thread. Restore validates exact lengths, format,
media, mono flag, write range and CRC before queuing. The engine restores 4096
bytes per block while muted (192 blocks, approximately 48 ms). Queued and partially
restored slots remain saveable before playback resumes. Frozen synth mode and its
effective quality are persisted separately. No compression is performed. Format v1 is the
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
- `stmlib/test/wav_writer.h`: frees two temporary conversion buffers after writing.
  This repairs leaks in the offline upstream harness only, not the sound engine.
- `beads/gain_control.h`: host builds supply the codec's two int32 frame fields;
  the gain algorithm itself is unmodified.
- `wavetable_grain.h` and `wavetable_voice.h`: skip the successor-wave read when
  the interpolation weight is exactly zero. This avoids reading past the last
  waveform of bank 12 and preserves the intended interpolation result.
- MinGW compiles only Grainloom and its imported sources with `-mno-ms-bitfields`;
  native 12-bit stereo and mono bundle sizes are asserted as 3 bytes. Without it,
  Windows used 4-byte storage and crashed in vintage/tape processing.
- Host builds explicitly use `-ffp-contract=off`. Apple Silicon's implicit fused
  evaluation initially caused an isolated cassette-mode peak error of 0.00074333
  in the exact-phase offline reference test; RMS error was 0.0000032331. Explicit
  unfused evaluation passed the unchanged peak/RMS limits on every architecture.
  This is a numerical-portability policy, not proof of hardware instruction parity.
  The pinned firmware targets STM32H7/Cortex-M7 at 400 MHz (the title of commit
  `da488e7` incorrectly says Cortex-M4). Its original compiler/FPU rounding and
  analog codec have not been reproduced or measured. Fused versus unfused
  rounding can move a sample across an original media quantizer's threshold.

Original hashes remain in provenance.json; git diffs identify the host patches.
Other module DSP and compiler flags are unchanged.

## Evidence collected locally

- `make test`: all ten existing deterministic suites pass.
- `make grainloom-upstream-test`: original grain fade assertions and render
  routines complete using generated fixtures. Most upstream tests are renders,
  so completion is not a quality or parity assertion.
- `make grainloom-test`: all 20 host-rate/media cases produce finite bounded
  audio, about 4000 blocks/second, and automatic ten-second synth transition.
- 48 kHz bridge versus direct native Core: sample-identical after 12 frames.
- Offline host-rate reference: all 20 media/rate cases compare the streaming
  adapter with a batch native-Core renderer and independently evaluated
  double-precision, exact-rational-phase sinc conversion. Control snapshots are
  shared to isolate the audio adapter, not independently validate the UI mapping.
  Inputs include stereo impulses, chirps, noise and percussion, with freeze.
  Equal-rate cases are sample-identical. The largest Windows converted-path peak
  error was 0.000115391 full scale; maximum RMS error was 0.0000112443. Assertions
  require peak < 0.0002 and RMS < 0.00002, accounting for float accumulation,
  fractional-phase table interpolation and original media quantization. These
  tolerances do not permit changes to the native DSP, which has its own exact
  pristine-source comparison.
- Independent `reference_render.cpp` build against the pristine pinned upstream
  checkout equals the host render byte-for-byte across all four media, freeze,
  delay transition, reverb, randomisation and all twelve wavetable banks.
  Fixtures include separated stereo impulses, chirps, fixed-seed noise, chords
  and a synthesized percussion loop. `make grainloom-pristine-test` reproducibly
  fetches the exact original revisions into the ignored build directory and
  repeats the independent comparison.
  Windows SHA-256: `61474d38c8e3cec3b6699da1d641d29df039daa228b2302dce45e652c6db1dcc`.
  Other compilers can produce different float bytes; comparisons are between
  host and pristine builds made with the same compiler, not cross-compiler hashes.
- SRC test: 1 kHz passband vs 17 kHz stopband while converting to 24 kHz,
  measured rejection 75.4-110.0 dB over the five required host rates.
- Snapshot test: all eight quality/channel combinations restore exact bytes;
  malformed base64 and CRC checks, multiple readers and newest-queued-state
  selection are exercised. All eight recording capacities wrap correctly over
  two full cycles and stop recording without changing their audio when frozen.
- Rack-linked Windows test: creation, DSP, immediate freeze saving, queued/partial
  restore re-saving, JSON reload, corrupted-checksum rejection, reset, frozen
  synth restoration, right-output gates and extreme feedback pass.
- Linux GCC AddressSanitizer/UndefinedBehaviorSanitizer matrix passed after the
  TLS correction. Further boundary/stress scenarios are being added.
- Windows SDK 2.6.6 plugin DLL and .vcvplugin package build successfully.

Adapter storage is 2,676,048 bytes; three snapshot slots add 2,359,384 bytes.
Total persistent per-module DSP/state storage is approximately 4.80 MiB,
excluding Rack widgets. The Windows offline rate/media matrix ran in about
1.28 seconds for 52 seconds of simulated audio, including construction and rate
setup, on this workstation. That is an aggregate offline measurement, not Rack's
live per-module CPU meter or worst-case callback timing. The ten-minute modulated
48 kHz stress run completed in 11.44 seconds with allocation prohibited on its
processing thread. Independent concurrent instances matched sequential renders;
concurrent immutable snapshot checks passed. All five host rates tracked
65.405-523.24 Hz within 0.008 cents in the wavetable pitch test. Original trigger
tests produced identical counts at 24/32/48 kHz for periodic, stochastic, clock
division, clock probability, gated and audio-rate modes. Original forward/reverse
and double-speed interpolation/envelope checks and 32-voice stealing tests pass.

## Acceptance work still outstanding

Final code CI [run 38576656901](https://github.com/stevenmcsorley/halfagiraf-modules/actions/runs/38576656901)
passed for Windows x64, Linux x64, macOS Intel and Apple Silicon at `da488e7`.
Every platform ran the native adapter, buffer, grain, trigger and rate-reference
tests before packaging. Linux also passed pristine-source comparison, original
render routines, ten existing suites, stress and address/undefined sanitizers.
Downloaded packages have the correct binary architecture, manifest, panel and
licences. The actual Windows CI package loaded and rendered Grainloom in Rack
using a separate user directory, then exited normally.

Recorded drum-loop/spoken-word comparisons, extended human audition, and
full hardware control/analog measurements remain. The included screenshot was
rendered in actual Rack with an isolated user directory, not a panel mock-up.
No physical Beads recording or measured analog codec reference
has been used, so hardware-level sonic parity is not yet certified.
