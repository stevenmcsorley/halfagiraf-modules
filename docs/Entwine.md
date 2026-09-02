# Entwine

**A 16-voice autoregressive synthesizer for VCV Rack 2**, by halfagiraf.

<p align="center">
  <img src="Entwine-in-VCV-Rack.png" width="320" alt="Entwine module in VCV Rack"/>
</p>

Entwine generates two interlaced streams, **Pulsar** and **Quasar**, whose pitch and
duration recursively influence their next values. It can work as a self-running
generative voice, follow an external clock in sync mode, or emit pitch, gate and
MIDI data to drive other instruments.

## Features

- Up to 16 autoregressive wavetable voices across Pulsar and Quasar.
- Independent stereo audio, V/Oct and gate outputs for both streams.
- Root, scale, glide, spread, wavetable, envelope and coupling controls with CV.
- Internal generative timing or external-clock synchronization.
- Lock and independent Reseed controls for the Pulsar and Quasar voice groups.
- 64 built-in wavetable frames arranged as eight selectable presets.
- Sixteen built-in scales, plus optional custom wavetable WAV and scale-file loading.
- Optional MIDI output with Pulsar on channel 1 and Quasar on channel 2.
- Context-menu filter, resonance, wavetable-index and note-length controls.

## File loading

Right-click the module to load a mono or multichannel PCM/float WAV as custom
256-sample wavetable frames, or load a compatible `scale.txt`. Custom file paths
are stored with the patch. Default wavetables and scales can be restored from the
same context menu.

## Installation

Download the unified `halfagiraf-modules` `.vcvplugin` file for your computer from the
[latest GitHub release](https://github.com/stevenmcsorley/halfagiraf-modules/releases/latest).

Copy the downloaded file into the matching Rack plugin folder, then restart Rack:

- Windows: `%LOCALAPPDATA%\Rack2\plugins-win-x64\`
- Linux: `~/.local/share/Rack2/plugins-lin-x64/`
- macOS Apple Silicon: `~/Library/Application Support/Rack2/plugins-mac-arm64/`
- macOS Intel: `~/Library/Application Support/Rack2/plugins-mac-x64/`

Rack will extract and load the package when it starts.

## Building

Requires the [VCV Rack SDK](https://vcvrack.com/manual/Building#setting-up-your-development-environment).
With `Rack-SDK` beside this repository:

```sh
make -j4
make dist
```

The editable panel is `res_text_backup/Entwine.svg`. Generate the Rack-compatible
text-as-path version after editing it with:

```sh
python tools/bake_svg_text.py res_text_backup/Entwine.svg res/Entwine.svg
```

## License

GPL-3.0-or-later. See [LICENSE](../LICENSE). Panel artwork © halfagiraf.
