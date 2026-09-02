# halfagiraf Modules

Five instruments, processors, and modulation sources for VCV Rack 2 by halfagiraf.

This is the single official plugin package for **Bad Sector**, **MOD1**, **Entwine**, **Constellate**, and **Palimpsest**. Each module keeps its established module slug, controls, DSP behavior, artwork, module-local serialization, and presets.

| Module | Purpose | Manual |
| --- | --- | --- |
| **Bad Sector** | Stereo buffer corruption and beat-locked broken playback | [Read the manual](docs/BadSector.md) |
| **MOD1** | Three-channel smooth or stepped random CV | [Read the manual](docs/MOD1.md) |
| **Entwine** | 16-voice autoregressive generative synthesizer | [Read the manual](docs/Entwine.md) |
| **Constellate** | Four-channel relational event memory | [Read the manual](docs/Constellate.md) |
| **Palimpsest** | Stereo spectral-memory instrument | [Read the manual](docs/Palimpsest.md) |

## Modules

### Bad Sector

<p align="center"><img src="docs/badsector.png" width="360" alt="Bad Sector in VCV Rack"/></p>

Records a beat-aligned stereo buffer and transforms it through synchronized Bend, Break, and Corrupt processes. Its timing grid remains locked while repeats, playback speed, direction, and traversal change.

[![Bad Sector ambient demo over London](https://i.ytimg.com/vi/EV3uuw9fdKU/hqdefault.jpg)](https://www.youtube.com/watch?v=EV3uuw9fdKU)

### MOD1

<p align="center"><img src="docs/MOD1-in-VCV-Rack.png" width="150" alt="MOD1 in VCV Rack"/></p>

Produces three independently scaled bipolar random voltages with smooth interpolation or stepped sample-and-hold motion, driven by its internal clock or an external clock.

### Entwine

<p align="center"><img src="docs/Entwine-in-VCV-Rack.png" width="300" alt="Entwine in VCV Rack"/></p>

Generates interlaced Pulsar and Quasar voice streams whose pitch and duration recursively influence their next values. It can run autonomously, synchronize to a clock, or control other voices through V/Oct and gates.

### Constellate

<p align="center"><img src="docs/Constellate.png" width="300" alt="Constellate in VCV Rack"/></p>

Learns relationships between four trigger streams and produces coherent variations from that event history. Its display, transition paths, and THREAD output are driven by the actual learned model.

[Watch the 15-second Constellate demo](docs/Constellate-demo.mp4)

### Palimpsest

<p align="center"><img src="docs/Palimpsest.png" width="280" alt="Palimpsest in VCV Rack"/></p>

Continuously analyzes stereo input into four progressively older spectral layers, then resynthesizes that memory as an evolving, pitch-responsive ambient voice.

## Installation

The preferred installation method is the official VCV Library. Until review is complete, download the `.vcvplugin` matching your computer from the [latest GitHub release](https://github.com/stevenmcsorley/halfagiraf-modules/releases/latest), place it in Rack's matching `plugins-<OS>-<CPU>` folder, and restart Rack.

Do not install the unified package alongside any of the earlier standalone development packages. They contain the same module slugs under different plugin slugs and are retained only as source history.

## Building and testing

With the VCV Rack SDK in the adjacent `Rack-SDK` directory:

```sh
make test
make -j4
make dist
```

The deterministic test suite covers Bad Sector's clocking, repeat grid, selector, control laws, mix, signal safety, and display telemetry as well as the Constellate and Palimpsest engines.

## Identity and license

- Plugin slug: `halfagiraf-modules`
- Plugin name: `halfagiraf Modules`
- Module slugs: `BadSector`, `MOD1-RandomCV`, `Entwine`, `Constellate`, `Palimpsest`
- License: GPL-3.0-or-later

Panel artwork © halfagiraf. See the individual module manuals for attribution and implementation details.
