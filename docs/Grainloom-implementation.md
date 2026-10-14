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
