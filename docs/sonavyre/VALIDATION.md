# Development validation — Linux x64, Rack 2.6.6

These results validate a development checkpoint, not the full mission or release acceptance.

## Passed

- Family plugin and independent Pro plugin built with `/home/dev/Rack-SDK`; both loaded concurrently in the actual Rack 2.6.6 application with a private user directory and Xvfb display. Logs record model and widget creation for both unique slugs. No existing installed plugin directories were overwritten.
- Copied `FV1.hpp` is byte-identical to the untouched upstream file.
- VM comparison: eight original effects × three parameter settings × 32768 stereo frames = **786432 stereo frames bit-identical** to the original interpreter.
- Host comparison: eight original effects × three host rates × 8192 stereo frames = **196608 stereo frames exactly equal** to the untouched advanced module's processing path, with stereo stimulus, parameter CV and reduced engine clock. This does not yet cover every LFO/routing shape variant.
- Composition: disabled-path identity, finite/nonzero serial and parallel impulse processing, and settings JSON round-trip.
- Preset host: save/recall, patch data round-trip and preservation of ten patch preset slots through module reset.
- Disk library: create, reload in a new instance, rename, favourite, duplicate and delete. Test uses an isolated temporary user directory. GUI file-dialog import/export and failure workflows are not yet acceptance-tested.
- Existing family `make test` suite passed, including Bad Sector, Constellate and other existing fixtures. Existing modules' implementation files are unchanged.
- Actual running Rack patch: Entwine provides stereo audio to both new modules; Pro restores its chorus/delay combination enabled. Scopes and stereo indicators respond to measured signals. See `validation.vcv` and `screenshots/Rack-running-linux.png`. The fallback engine is running; this is not a sound-card loopback or listening test.
- Initial checkpoint excluded the extracted bank. The owner has now confirmed rights and requested inclusion; the complete 200-program header is bundled, unchanged, as `CompleteBank.hpp`. The editor’s firmware-image template remains excluded; its 38 additional actual program binaries are now included as `CatalogueExtras.hpp`.

## Reproduce focused checks

Keep the original checkout at `../upstream-halfagiraf-vcv`, then run from the family root:

```sh
RACK_DIR=/absolute/path/to/Rack-SDK tools/test_sonavyre.sh
make RACK_DIR=/absolute/path/to/Rack-SDK test
```

The comparison executable includes the untouched original host and its original data solely for local regression testing. It is not part of the plugin build or distribution. The focused script is currently a Linux verification script; Windows/macOS validation is outstanding.

## Limits

No whole-product performance/latency acceptance, audio-device listening validation, restart UI acceptance, sanitizer stress pass, Windows/macOS build, release archive, or store submission is claimed. The running screenshot's CPU/GUI measurements include Entwine, both effects modules, extra FV-1 stages, software-display overhead and the current machine load; they are not isolated module benchmarks. The new vector panels remain development artwork requiring polish and reference review.

See `AUDIT.md` for explicit engineering and licensing release blockers.

The additional `Rack-module-browser-linux.png` capture shows both new products coexisting with the original family in Rack's native browser. An attempted scripted GUI preset-name entry did not complete; disk persistence is verified by the focused library test, while interactive name-entry acceptance remains outstanding.


## UI and full-bank revision — 2026-10-23

- Added the original 200 programs without changing their binary instructions, names or parameter labels. Appended them to the original eight programs, preserving existing development-patch program indices. Both editions expose the same 246-program factory bank.
- Added a **Big reverbs** filter (existing Black Hole, Cloud, Infinite, Hall, Space and Shimmer programs).
- Moved Pro previous/next controls completely outside the display and away from stage controls; moved/relabelled LFO restart. Moved the free module’s CV row out of the screen/central-knob gap and labelled all three controls separately.
- Restored the established right-edge family giraffe and centred footer wordmark. Knob labels now follow actual effect parameter metadata rather than generic numbers.
- Replaced the thin scope with a bounded UI-only ribbon/particle field driven by chronological atomic output samples and measured RMS envelope. It does not claim to measure internal grains or a calibrated spectrum. Animation toggle retains a stationary field.
- Added `audition.vcv` selecting Black Hole and Infinite Dark reverbs, with Entwine input and combination stages bypassed for direct audition. The prior validation patch is retained.
- Updated parity coverage: **2,248,704 stereo VM frames across all 246 programs**, and **562,176 host stereo frames** against the original host at 44.1/48/96 kHz. Host comparison includes all 238 added programs, bank/metadata identity, original-eight compatibility, preset persistence and reset preservation. Disk-library checks remain unchanged.

The revised builds were loaded together in Rack 2.6.6 on the desktop with Black Hole and Infinite Dark selected. `screenshots/Rack-208-effects-ui.png` records the actual running UI. All 246 bank entries and their original parameter metadata are present; the eight original development slots remain unchanged.


The final revision also includes the editor catalogue’s 38 binaries absent from the 200-program bank. `gen_sonavyre_catalogue.py --check` verifies their bytes and labels against the untouched editor data. Total coverage is now 2,248,704 VM stereo frames and 562,176 host stereo frames across 246 factory programs. The 38 added programs are appended, so the previous 208 program indices remain stable. The final running screenshot is `screenshots/Rack-246-effects-ui.png`.

Label-spacing follow-up: lowered the small-knob row by 1.5 mm, moved the lower socket row and labels down to preserve separation, lifted the two side parameter names above their decorative rings, and placed the central parameter name above its knob. No DSP or bank data changed. Both Linux plugin builds passed; existing full-bank parity results still apply.

Visual-response tuning: increased quiet-signal sensitivity, waveform detail and attack speed; added a soft-knee energy mapping to retain movement on louder material. A persisted Visual sensitivity menu offers 1×, 2.5× (default), 5× and 10× analysis gain, with no audio-gain change. Both Linux builds compile; the audio processing path is unchanged.

## WAV import removal

Both editions omit WAV bank import and the hidden audio firmware-listen gesture. The unused copied decoder is removed; the original repository remains unchanged. Host regression additionally holds both navigation buttons for 60000 frames, verifies unchanged program/bank and finite audio, then checks normal next-program navigation. Existing custom-bank patch restoration and preset JSON import/export remain supported.

## Edition separation, 23 October 2026

Linux builds succeed for Free (12), Studio (64) and unchanged full PRO (246). All three plugin/module pairs load together in actual Rack Free 2.6.6 using a dedicated QA profile. `screenshots/editions-rack.png` records the real UI. The full VM regression still passes 2248704 stereo frames bit-identically, and the host regression still passes 562176 stereo frames exactly against the original host. Disk preset regression passes. New focused checks pass for every selected Free/Studio binary, finite audio, program navigation, required reverbs, cross-edition catalogue mapping, curated-bank boundaries, disabled compositions and reset. Windows/macOS SONAVYRE builds remain unverified; previous Grainloom platform validation does not establish these new builds.
