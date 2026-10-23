# Development validation — Linux x64, Rack 2.6.6

These results validate a development checkpoint, not the full mission or release acceptance.

## Passed

- Family plugin and independent Pro plugin built with `/home/dev/Rack-SDK`; both loaded concurrently in the actual Rack 2.6.6 application with a private user directory and Xvfb display. Logs record model and widget creation for both unique slugs. No existing installed plugin directories were overwritten.
- Copied `FV1.hpp` is byte-identical to the untouched upstream file.
- VM comparison: eight original effects × three parameter settings × 32768 stereo frames = **786432 stereo frames bit-identical** to the original interpreter.
- Host comparison: eight original effects × three host rates × 8192 stereo frames = **196608 stereo frames exactly equal** to the untouched advanced module's processing path, with stereo stimulus, parameter CV and reduced engine clock. This does not yet cover every LFO/routing shape or external firmware variant.
- Composition: disabled-path identity, finite/nonzero serial and parallel impulse processing, and settings JSON round-trip.
- Preset host: save/recall, patch data round-trip and preservation of ten patch preset slots through module reset.
- Disk library: create, reload in a new instance, rename, favourite, duplicate and delete. Test uses an isolated temporary user directory. GUI file-dialog import/export and failure workflows are not yet acceptance-tested.
- Existing family `make test` suite passed, including Bad Sector, Constellate and other existing fixtures. Existing modules' implementation files are unchanged.
- Actual running Rack patch: Entwine provides stereo audio to both new modules; Pro restores its chorus/delay combination enabled. Scopes and stereo indicators respond to measured signals. See `validation.vcv` and `screenshots/Rack-running-linux.png`. The fallback engine is running; this is not a sound-card loopback or listening test.
- No extracted Pro firmware bank or offline firmware editor data is staged into the new products.

## Reproduce focused checks

Keep the original checkout at `../upstream-halfagiraf-vcv`, then run from the family root:

```sh
RACK_DIR=/absolute/path/to/Rack-SDK tools/test_sonavyre.sh
make RACK_DIR=/absolute/path/to/Rack-SDK test
```

The comparison executable includes the untouched original host and its original data solely for local regression testing. It is not part of the plugin build or distribution. The focused script is currently a Linux verification script; Windows/macOS validation is outstanding.

## Limits

No whole-product performance/latency acceptance, audio-device listening validation, real external-firmware import fixture, restart UI acceptance, sanitizer stress pass, Windows/macOS build, release archive, or store submission is claimed. The running screenshot's CPU/GUI measurements include Entwine, both effects modules, extra FV-1 stages, software-display overhead and the current machine load; they are not isolated module benchmarks. The new vector panels remain development artwork requiring polish and reference review.

See `AUDIT.md` for explicit engineering and licensing release blockers.

The additional `Rack-module-browser-linux.png` capture shows both new products coexisting with the original family in Rack's native browser. An attempted scripted GUI preset-name entry did not complete; disk persistence is verified by the focused library test, while interactive name-entry acceptance remains outstanding.
