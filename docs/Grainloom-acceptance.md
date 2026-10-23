# Grainloom release-candidate acceptance — 14 October 2026

Implementation: `b1d2a23bbf8c16e399cdde461377fd7153f23f24`,
branch `feature/grainloom-beads-port`.
[Exact-code CI](https://github.com/stevenmcsorley/halfagiraf-modules/actions/runs/38603327943)
completed successfully. No merge, tag, public release or Library submission.

## Verdict

### Maintainer release decision

After local testing, the maintainer approved the 2.1.0 public release on
14 October 2026. This is a release decision, not a new measured hardware comparison
or a completed per-fixture listening survey. Earlier control-response concerns
are not erased by that approval. A subsequent isolated Rack-linked probe found
small primary-control steps reach DSP state in approximately 19-31 ms; this is
not a measurement of perceptual equivalence. The exact tested code remains
`b1d2a23`; subsequent release preparation changes documentation only.

The release contains the four published modules plus Grainloom. the earlier effects-module work stays
in its separate uncommitted worktree and is not changed or included.

### Original acceptance verdict

**GO for release-candidate audition; NO-GO for unconditional production sign-off.**
The automated checks below pass. Human listening, Mac desktop checks and the
bundle choice remain outstanding. No physical Beads hardware was available.
There is no claim of measured analog or instruction-level hardware identity.

## Verified changes

- Production graphite/cyan panel, original component SVGs, stereo display telemetry.
- Firmware-correct four-second SEED latch on release.
- Firmware-correct quiet-tail synth-entry timer, reset by freeze and codec mute.
- Safe 64-bit JSON range checks before recording-pointer conversion.
- Display format/bank-boundary tests, actual Rack engine acceptance harness,
  deterministic musical render tool, audit and listening documentation.
- Original Beads DSP algorithms unchanged by this pass.

## Exact tests

| Check | Actual result |
|---|---|
| Windows `make test` | Existing ten deterministic suites pass. |
| Windows `grainloom-test` | 20 host-rate/media references, native render comparison, buffers, grains and updated seed/freeze timing pass. |
| Windows `grainloom-rack-test` | Actual Rack-linked module audio, JSON freeze reload, reset and gate/feedback checks pass, including display channels. |
| Windows/Linux `grainloom-acceptance-rack` | Full Module JSON duplication; invalid 64-bit write pointer rejection; left/right mono normalization; unpatched-right fold-down; four wired engines with external clock, feedback and five host-rate changes pass. |
| Windows/Linux `grainloom-display-test` | Eight recording formats, bank boundaries, original decoder equality and no state mutation pass. |
| Windows/Linux `grainloom-stress-test` | Ten simulated minutes of feedback/freeze/media modulation and concurrent engines/snapshot readers pass. |
| Linux `grainloom-pristine-test` | Fresh pinned source native render is byte-identical to host DSP. |
| Linux `grainloom-upstream-test` | Selected original assertions/render routines complete; not every disabled original routine is exercised. |
| Linux `grainloom-sanitize-test` | Address/undefined sanitizers pass for adapter, snapshot, grain/buffer and rate references. |
| Exact-code native CI | Windows x64, Linux x64, Mac x64 and Mac arm64 tests/builds/packages all pass; engine/safety job passes. |
| Listening renderer | 36 processed and nine dry stereo WAVs; all finite with nonzero processed energy; two complete renders have identical hashes for all 45 files. |

Local command logs: `build/grainloom/rc-final-windows.txt`,
`rc-current-windows.txt`, `rc-linux-results.txt`, `rc-linux-rack.txt`.
Initial acceptance-harness failures were missing plugin registration and Rack RNG
initialization (duplicate-ID allocation loop). The harness was corrected to
initialize the real host services; no DSP test threshold was relaxed.

## Host and presentation evidence

Windows normal Rack loaded the earlier revised-panel build during installation.
The exact corrected candidate was tested with the actual Rack library harness,
not installed over the user's running session. Windows' global single-instance
restriction prevents a second isolated desktop session while normal Rack is open.

Linux Rack 2.6.6 under WSLg loaded the current isolated plugin and
`docs/patches/Grainloom-demo.vcv`, including VCO, Grainloom, 8vert and AUDIO-2,
and exited normally via its native screenshot command. Panel screenshots at
100%/200% are actual Rack renders, not mockups. Linux lacks ALSA MIDI/JACK devices
in this environment; no physical audio-device audition is claimed.
Mac packages ran native tests in CI; Mac desktop/UI/audio-device testing is not performed.

## Performance and safety

The actual Rack engine harness runs four concurrent media configurations,
external clock, density 0.92, feedback 0.95 and reverb 0.85. It measures 256-frame
blocks over four simulated seconds at each rate, with frozen transitions.
It is not a physical audio-driver deadline measurement.

| Host rate | Windows elapsed for 4 s | Windows p99 / maximum block |
|---|---:|---:|
| 44.1 kHz | 0.301 s | 638 / 952 us |
| 48 kHz | 0.197 s | 363 / 509 us |
| 88.2 kHz | 0.438 s | 484 / 729 us |
| 96 kHz | 0.444 s | 449 / 795 us |
| 192 kHz | 0.733 s | 370 / 764 us |

Output peaks for the four sinks: 5.980, 5.419, 5.956 and 5.488 V; no
non-finite output, >10 V output or output subnormals in this check.
These are measured test bounds, not proof that every patch avoids intentional
media limiting or denormals internally.

Linux p99 blocks were approximately 380-612 us. Individual WSL scheduling spikes
reached 1.983 ms, exceeding the 1.333 ms budget at 192 kHz/256 frames.
Windows' final run stayed below its block budgets, but earlier maxima also varied.
There is no demonstrated deterministic DSP overrun; scheduling and physical
audio-device testing remain necessary before claiming a worst-case guarantee.

Adapter storage remains 2,676,048 bytes; snapshots 2,359,384 bytes.
The module object is 2,472 bytes including the approximately 2 KiB stereo atomic
display arrays. Combined persistent storage is about 4.805 MiB, excluding widgets,
allocator overhead and save-thread JSON temporaries. Native stress recorded
36.2 us largest snapshot copy on Windows and 34.3 us on Linux; not universal bounds.

## Packages and attribution

Packages are downloaded to `build/platform-packages/b1d2a23/`.
Inspection confirms halfagiraf slug/version, all four published module IDs plus
Grainloom, matching panel, four component SVGs and Beads/MIT plus GPL notices.
Original wavetable resources are linked into the DSP binary, not an external
file dependency. Actual architectures: PE32+ x86-64, ELF x86-64, Mach-O x86-64,
Mach-O arm64.

```text
win-x64   c2863ccc7ce6893e2c40fd60758af2277697a2e1d38a5bec5b13a69ae54b194b
lin-x64   7a1ffbc72d53520589c78d07c4cb412b2f74c419f87875c33555e6b12efba779
mac-x64   804160b5dc7b48269f80a34be4d7128cceeb3db872b1226c57c8716b8402be96
mac-arm64 f2153594c583ffceb11102ec1c3898a775960e4af8f140af37c8386226d066c7
```

Manifest/registration use existing `halfagiraf` and `Grainloom` identifiers.
Original Beads branding/panel is not copied. The code retains MIT provenance
and the surrounding GPL-3.0-or-later licence. Reviewed against
[Rack licensing/ethics](https://vcvrack.com/manual/PluginLicensing) and
[manifest requirements](https://vcvrack.com/manual/Manifest).
Submission still requires an approved release revision and the existing plugin
update workflow, not another standalone plugin.

## Listening and bundle decisions

See [listening instructions](Grainloom-listening.md) and
[control audit](Grainloom-control-audit.md). The user-provided commercial vocal
input and derived WAVs stay in ignored local build storage. They must not be
published without the sample owner's redistribution permission.
No human approval is recorded yet. No hardware measurement is recorded.

Current main is published 2.0.2 (`3541f29`). The original separate worktree has
uncommitted the earlier effects-module work source, tests and assets. None were modified, discarded or
silently folded into this candidate. This package retains Bad Sector, MOD1,
Entwine and Constellate plus Grainloom; it does not contain the earlier effects-module work.
Before installing it over any bundle containing the earlier effects-module work, reconcile the approved
the earlier effects-module work snapshot into the release branch and repeat combined-package validation.
The user's choice about including that uncommitted implementation is pending.

Normal Rack settings were separately backed up for the local installation test.
Automatic Library checks are temporarily off because sync hides unpublished
Grainloom. This is a testing workaround, not shipped plugin behaviour. Once the
Library accepts Grainloom, re-enable automatic checks.
