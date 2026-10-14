# Grainloom listening checks

Human sign-off is pending. Automated finite-output checks are not listening approval.
The local files are in `build/grainloom/listening/`; the public repository contains
the renderer, not the commercial vocal input or its derived audio.

[Demo patch](patches/Grainloom-demo.vcv): VCO through Grainloom, two conservative
8vert level controls and AUDIO-2. Choose your own audio driver/device in AUDIO-2.
No user's device name is stored. Open as a separate patch; do not overwrite your
existing work. Try TIME/SIZE, then FREEZE, followed by modest pitch randomization.

There are nine fixtures, each with a dry input and four processed versions:
drums, pad, transients, reverse grains, frozen texture, feedback, wavetable,
vocals and clocked delay. Drums/pad/transients are deterministic generated sources.
The vocal uses the user-provided 133 BPM loop. These are musical review fixtures,
not physical Beads comparisons. The separate pristine-source tests provide the
algorithmic reference evidence.

Listen at a low initial volume. Start with digital, then vintage, tape and cassette.
Compare each wet file against its dry source. Do not mistake intended cassette
noise, reversed attacks, rectangular-envelope clicks or repeat taps for a failure.

- Drums: onset clarity, stereo placement and rhythmic response.
- Vocals: recognisable syllables, pitched grains, sibilants and unexpected harshness.
- Pad: sustained texture, stereo continuity and reverb decay.
- Transients: unintended pops or buffer-edge discontinuities.
- Reverse: audible backwards envelope/content, not just a normal delayed pad.
- Frozen: recording freezes at four seconds; texture continues afterward.
- Feedback: unstable escalation, harsh limiting or persistent DC. Report it rather
  than assuming high feedback must sound clean.
- Wavetable: inputs are disconnected; expect roughly ten seconds of silence before
  the internal sound starts (plus codec transition mute). Verify all four qualities.
- Clocked delay: SIZE is infinite; a 2 Hz clock drives SEED.

Please record pass/fail/comments per fixture and quality, with headphones/speakers
and audio-interface settings. Hardware comparison is explicitly not performed.

## Reproduction

Convert the private stereo vocal to 48 kHz f32le with ffmpeg:

```sh
ffmpeg -i "your-vocal.wav" -ar 48000 -ac 2 -f f32le vocal-input.f32
make -f grainloom.mk grainloom-listening-render
build/grainloom/<compiler-target>/listening_render output-directory vocal-input.f32
```

The output directory must exist. The renderer writes 32-bit stereo float WAVs,
prints peak/RMS/near-limit counts and rejects non-finite output.
`manifest.json` in the local listening folder records SHA-256 and file sizes.
Preserve the converted input hash and compiler version when comparing subsequent
runs; different compilers are not required to produce identical float bytes.
