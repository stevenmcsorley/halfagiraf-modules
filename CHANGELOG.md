# Changelog

## 2.1.0 — 14 October 2026

- Adds Grainloom, using the original open-source Beads granular, delay and wavetable DSP.
- Adds host-rate conversion, original recording qualities, attenurandomizers and frozen recording snapshots.
- Adds the graphite/cyan panel and live stereo recording overview.
- Matches the firmware's four-second SEED release latch and quiet-tail wavetable entry.

## 2.0.2

- Fixes Rack static-analysis findings in Entwine, Constellate, and Palimpsest support code.
- Hardens Entwine WAV loading and wavetable bounds handling.
- Keeps Entwine's decorative cloud layer visible in module-browser and library previews.
- Preserves existing module DSP behaviour while making bounds and initialization explicit.

## 2.0.1

- Removes Palimpsest while its audio engine is reworked.

## 2.0.0

- First unified release of the halfagiraf Modules plugin.
- Includes Bad Sector, MOD1, Entwine, Constellate, and Palimpsest.
- Preserves the established module slugs, DSP behavior, artwork, and Bad Sector factory presets from the standalone projects.
- Builds native packages for Windows x64, Linux x64, macOS Intel, and macOS Apple Silicon.
