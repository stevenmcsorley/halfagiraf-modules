# Grainloom validation handoff

Status: implemented release candidate, not a published release and not certified
as hardware-identical. Branch: `feature/grainloom-beads-port`.
Verified code commit: `da488e7ab50082454be4619e9d6ba5b039da5fdf`.
The original dirty `halfagiraf-modules` worktree, including Myriad work, and the
normal Rack user installation were not replaced.

## Files and integration

- `src/Grainloom.cpp`: Rack module, controls, original panel drawing and state.
- `src/GrainloomCore.hpp`: original DSP ownership, gain, codec conversion and RNG.
- `src/GrainloomAdapter.hpp`, `GrainloomResampler.hpp`: host audio/CV/gate bridge.
- `src/GrainloomSnapshot.hpp`: fixed ownership slots, validation and frozen storage.
- `res/Grainloom.svg`, `docs/Grainloom.png`: original panel and actual Rack render.
- `vendor/mutable/`: pinned source, resources, firmware mapping references,
  dependency closure and original-hash provenance.
- `tests/grainloom/`, `grainloom.mk`: native/reference/safety tests and audio demo.
- `plugin.json`, `src/plugin.cpp`, `src/plugin.hpp`, `Makefile`: registration/build.
- `.github/workflows/release.yml`: four native build/test targets and sanitizers.
- `LICENSE-Beads.txt`, README, changelog, user manual and implementation record.

Existing module DSP, identifiers and presets are unchanged. The plugin slug
remains `halfagiraf`; Grainloom's slug is `Grainloom`.

## Ported original components and features

Processor, granular/delay players and grains, envelopes, scheduling, interpolation,
voice stealing, delay clock/controller, randomization, packed media storage,
original rate converters, mu-law, wow/flutter, limiting, reverb, wavetable synth,
all original resources and gain control compile and run. Hardware drivers are
reference-only, not linked into the plugin.

Implemented controls/functions include time, pitch, size, shape, density, four
cable-sensitive attenurandomizers, feedback, reverb, mixing, assignable CV and
depths, freeze, seed/hold/latch/clock/gate behaviours, all four quality modes,
manual/automatic input gain, infinite-grain delay, mono/stereo recording, mono
output fold-down, right-output grain gates and ten-second wavetable entry.
All twelve released-source wavetable banks remain accessible.

Frozen audio has versioned, bounded, checksum-validated patch storage. Frozen
synth mode, effective quality and latch state are restored; Rack persists the
ordinary parameters. Immediate and partially restored patch re-saving are tested.

## Deviations and hardware assumptions

The host replaces ADCs, jack normalization probes, interrupts, DMA, codec and
flash with Rack connection detection and a preallocated bridge. CV is sampled
at 4 kHz; its phase relative to the delayed audio conversion is documented.
Running grains and reverb tails restart on reload; only frozen recording/state
is preserved. Audio conversion is 10 V/full-scale, gates use Rack thresholds and
right-output triggers have positive polarity. Non-native bridge latency is
approximately 1.92-4.37 ms; equal rates add 0.25 ms, plus original tape latency.

Host-specific source patches, packed-layout flags and explicit unfused floating
evaluation are listed in [the implementation record](Grainloom-implementation.md).
Unfused evaluation is a cross-platform policy, not a claim to reproduce the
STM32H7 firmware compiler's exact instructions. No generic granular/reverb engine
or replacement wavetable data was used. MIT attribution is retained; the plugin
remains GPL-3.0-or-later. No original logo or hardware panel was copied.

## Commands and actual results

| Command/check | Result |
|---|---|
| `make test` | All ten existing deterministic suites pass. |
| `make grainloom-test` | All 20 rate/quality cases, native-domain equality, gates/rate changes, all eight buffers/snapshots, grain/voice/trigger and offline audio comparisons pass. |
| `make grainloom-pristine-test` | Fresh exact-revision source checkout matches the host native render byte-for-byte. |
| `make grainloom-upstream-test` | Original assertions and selected trigger/processor/synth/delay/freeze render routines complete. Not every disabled upstream routine is run. |
| `make grainloom-stress-test` | Ten simulated minutes of feedback, modulation, freeze and quality changes; allocation prohibited during processing. Concurrent independent engines and immutable snapshot readers pass. |
| `make grainloom-sanitize-test` | Linux GCC AddressSanitizer and UndefinedBehaviorSanitizer pass for adapter, snapshots, grain/buffer boundaries and rate reference. |
| `make grainloom-rack-test` | Actual Rack-linked Windows module creation/audio, reset, JSON restoration, invalid CRC rejection, frozen synth and gate output pass. Requires installed Rack DLLs on PATH. |
| `make grainloom-audio-demo` | Generates an 18-second stereo WAV: chord, granular/reverb, freeze, then infinite-size transition. |
| `make dist` / CI | Packages produced on all four native architectures. |
| Actual Windows package load | Rack loaded halfagiraf 2.1.0, rendered Grainloom assets and exited normally in an isolated user directory. |

## Audio comparison evidence

The pristine native renderer uses stereo impulses, chirps, fixed-seed noise,
chords and synthesized percussion through all media, freeze, delay transitions,
reverb, randomization and twelve wavetable banks. Windows render SHA-256:
`61474d38c8e3cec3b6699da1d641d29df039daa228b2302dce45e652c6db1dcc`.
Comparisons are between equivalent builds on one compiler, not cross-platform
float hashes or measured hardware output.

The independent exact-rational-phase, double-precision offline SRC comparison
passes all 20 host-rate/media cases, with native cases sample-identical. It shares
control snapshots to isolate the audio adapter. Original native DSP has a separate
pristine-source comparison. Unchanged converted-path limits are peak < 0.0002
and RMS < 0.00002 full scale; largest observed final Apple Silicon values were
0.000115391 peak and 0.0000118509 RMS. Pitch tests across the five host rates
measured less than 0.008 cents error. SRC stopband tests measured 75.4-110 dB
rejection for 17 kHz while converting to 24 kHz.

An initial Apple Silicon cassette comparison failed its peak limit. The compiler
contraction policy was investigated and changed; neither threshold nor original
DSP algorithm was changed to obtain a pass. This failure and policy are documented.

## Performance

Local measurements: Intel Core i5-13600KF, Windows, optimized native tests.
Persistent adapter storage is 2,676,048 bytes; snapshot ownership storage is
2,359,384 bytes: approximately 4.80 MiB total before Rack widgets/serialization.
The 52-second rate/media simulation took about 1.28 seconds including setup.
Ten-minute modulated stress took approximately 10-11.44 seconds across runs.
The largest observed fixed snapshot copy was 30.2 microseconds over 100 freezes.
These are offline wall-time/storage measurements, not a universal worst-case
callback bound or a measured Rack live CPU percentage. JSON encoding allocates
additional temporary memory on the saving thread, not the audio callback.

## Platform packages

[Final CI](https://github.com/stevenmcsorley/halfagiraf-modules/actions/runs/38576656901)
is green. Packages were downloaded to `build/platform-packages/da488e7/` and
extracted for architecture/manifest/panel/licence inspection.

| Target | Native tests/build/package | Desktop runtime |
|---|---|---|
| Windows x64 | Pass; PE32+ x86-64 | Actual Rack load/render and linked module tests pass. |
| Linux x64 | Pass; ELF x86-64 | Native DSP/sanitizers pass; no desktop Rack session tested. |
| macOS Intel | Pass; Mach-O x86_64 | Native DSP tests pass; no desktop Rack session tested. |
| macOS Apple Silicon | Pass; Mach-O arm64 | Native DSP tests pass; no desktop Rack session tested. |

Package SHA-256 values:

```text
win-x64   88ef1338eed839f7812f26768ded00f08925971be454197130f11d0b36865136
lin-x64   1e17e433506d149959ae4bd24527722a8347817ec4ef937c61cd832aa571da47
mac-x64   214f51ffc7ae1c8197e0f58ce43144ea7b5fae6f7c7d2d71280feba4e46d1845
mac-arm64 b3460c872f4aed3ed9922feb0af1437528b005962bd2ccef241efd92011577c9
```

## Remaining acceptance work

- Extended human musical audition, including recorded drum loops and spoken word.
- Physical Beads/control/analog-codec comparison and original firmware compiler
  rounding measurements; unavailable in the current environment.
- Desktop Rack runtime checks on Linux and both Mac architectures.
- Universal worst-case callback profiling; current measurements are offline.
- Explicit approval before merging main or publishing a release.

These are not silently marked complete. The implementation is usable for review,
but full hardware-identical production certification is not claimed. No release
or Library submission was made by this task.

## Stage commits

```text
a8f150a  Pinned source, dependencies and original harness
25a7c5c  Native DSP bridge and frozen storage
c7e2f77  Rack controls, original panel, registration and trigger tests
f4e9d83  Windows CI jq dependency repair
67ee9ca  Parity, state ownership, buffer boundaries and stress
ff0362e  Windows byte-comparator dependency and safe discrete controls
1cd8276  Frozen buffer/gate preservation across host-rate changes
ccf0172  Independent offline audio bridge comparison
da488e7  Explicit floating-point contraction policy; verified package code
```
