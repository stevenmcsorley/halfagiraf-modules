# Grainloom panel

The revised panel uses graphite, warm-metal section borders, smooth dark knobs
with cyan pointers, a stereo cyan buffer display and restrained orange accents.
It is 28 HP. Typography is rendered with Rack's bundled fonts.

## Functional inventory

- TIME, PITCH, SIZE and SHAPE, each with its own RANDOM / CV attenurandomizer.
- DENSITY, FEEDBACK, REVERB and DRY / WET.
- QUALITY, assignable CV DEPTH and ASSIGN destination, manual GAIN.
- FREEZE, SEED, LATCH and AUTO gain-calibration buttons.
- FREEZE, SEED and LATCH lights retain their original state bindings.
- Ten inputs: stereo audio, freeze, seed, density, time, pitch, size, shape,
  assignable CV. Two audio outputs retain the optional right-output grain gates.
- Context-menu gate routing, frozen-buffer persistence and combined CV depths
  remain unchanged. Parameter and port identifiers are unchanged.

Unlike the concept, the real module has one socket per CV input, not duplicate
parameter and bottom-row sockets. QUALITY remains the implemented selector knob,
not four invented buttons. Manual gain and CV assignment remain visible.

## Display

The audio thread publishes 128 bounded min/max columns per channel using the
original recording-format decoders. The UI reads atomic telemetry only.
The displayed waveform is a sampled overview, not an exact oscilloscope.
The vertical marker is the actual recording write position; it does not claim
to represent individual grain positions. The capacity reflects the current media
and mono/stereo buffer size. Wavetable mode shows the actual stereo output
envelope instead. The meter follows the existing input-level telemetry.

Display readback does not change the original DSP algorithms. Buffer scans run
at about 30 Hz with fixed work and no allocation. No synthetic waveform or
decorative grain animation is used. Browser previews have no engine instance,
so their waveform is correctly flat.

## Verification

Windows x64 package build and Linux x64 compilation passed with Rack SDK 2.6.6.
Rack 2.6.6's native screenshot renderer produced the checked 100% and 200%
previews in isolated Linux user storage; the user's running Windows Rack,
normal installation and the earlier effects-module work were not modified.

The targeted Rack module harness passed audio, freeze/JSON reload and bounded
feedback checks, with added assertions for both waveform channels and digital
buffer capacity. The display decoder test passed all eight media/channel
formats, bank boundaries, original-decoder equality and no-state-mutation checks.
No extended test suites or macOS builds were run for this panel change.

Screenshots: [200%](Grainloom.png), [100%](Grainloom-100.png).
