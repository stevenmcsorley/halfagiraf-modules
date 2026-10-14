# Grainloom control acceptance audit

Audit basis: pinned Beads firmware `beads.cc`, `ui.cc/.h`, `cv_reader.cc/.h`,
original DSP, and the [official manual](https://pichenettes.github.io/mutable-instruments-documentation/modules/beads/manual/).
This is a source and automated-test audit, not physical-hardware acceptance.

| Rack function | Firmware implementation / result |
|---|---|
| TIME | Original 0..1 pot smoothing, 0.001 hysteresis and RandomizedParameter passed through. |
| PITCH | Original nonlinear interval table, +/-24 semitones, full-CW attenurandomizer 1 V/oct; wavetable root CV retains original independent tracking. |
| SIZE / SHAPE | Original normalized controls and grain/delay mappings; player captures new-grain parameters. No replacement envelope or reverse logic. |
| Four RANDOM / CV knobs | Original deadband/quadratic pot law; actual cable detection, positive external modulation and negative CV-controlled randomization passed to original RandomizedParameter. |
| DENSITY CV / knob | Original modulation scaling and smoothing, TriggerGenerator and DelayController; midpoint/periodic/random, clock probability/division, gated/single/audio-rate tests. |
| FEEDBACK / MIX / REVERB | Firmware 1.005 base factor and -0.002 assignment offset; original media limiter, mixing curves and reverb. |
| ASSIGN / DEPTH | Real destination selector, VCV-native global depth and original combined-destination depths; context-menu sliders replace hardware hidden pot gestures. |
| FREEZE button / jack | Button latch OR external Schmitt gate; recording versus synth freeze handled by original DSP. Quality/mono changes deferred while frozen. |
| SEED button / jack | Original four-second hold latches on release (corrected from earlier 1.5-second approximation); 2.5 ms external input delay; original trigger generator. |
| LATCH | Explicit Rack shortcut for firmware SEED+FREEZE lock. |
| QUALITY | Four actual recording paths and original sample rates; selector knob replaces cycling button. LCD displays effective, not pending, media. |
| GAIN / AUTO | Original automatic/manual algorithm; explicit Rack controls replace held quality-button gesture. |
| Audio L / R input | Rack connection detection replaces hardware normalization probing. Either input normalizes mono; both inputs record stereo. |
| Audio L / R output | 10 V full-scale conversion and original limiting. Unpatched R folds to averaged stereo L; gate option retains original processor gate path with positive Rack polarity. |
| Save frozen audio menu | Versioned packed little-endian recording, bounded restoration, CRC validation. Rack patch persistence replaces firmware flash backup; running grains/reverb restart. |
| Right output gates menu | Replaces hardware ASSIGN+SEED gesture without substituting the trigger algorithm. |
| Combined CV menu sliders | Explicit storage of three individual modulation depths; no ordinary attenuverter substituted for attenurandomizers. |
| Freeze / seed / latch LEDs | Real frozen/button/external-gate/locked states. Dedicated latch lamp replaces the hardware pulsing SEED lock indication; cosmetic timing/palette is not hardware-identical. |
| LCD / knob dots | Actual recording overview, write position, media, gain meter and knob values. No active-grain-position telemetry or invented animation. |

## Corrections from this acceptance pass

The SEED hold/release correction remains a wrapper change; original DSP is unchanged.
Wavetable entry now restarts its timer while frozen or muted and whenever a rendered
tail exceeds 0.01 full scale, matching `Process` and `MonitorStateChange`.
A freeze/unfreeze timing regression test is added. Original pristine DSP comparison
remains separate from host-control tests.

The official manual describes eight wavetable banks; the released synth quantizer
and 192 original waves expose twelve. This follows source rather than inventing
replacement waveforms. Hardware calibration, firmware update, factory test and
disabled experimental monophonic/trim gestures are not Rack features.

## Limits of this evidence

Hardware ADC offsets, component tolerances, analog codec response and firmware FPU
rounding are not measured. Host control snapshots have documented phase differences
relative to resampled audio. Numerical comparisons do not certify every UI gesture,
timing edge case or subjective musical quality. Human listening is still required.
