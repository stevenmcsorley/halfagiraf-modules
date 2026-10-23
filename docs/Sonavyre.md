# SONAVYRE Free

*Visual Stereo Effects Engine — halfagiraf*

<p align="center"><img src="Sonavyre.png" width="270" alt="SONAVYRE Free in VCV Rack"/></p>

SONAVYRE Free combines 12 stereo effects with effect-specific controls, CV modulation, user presets and an audio-responsive display. It is part of the halfagiraf Modules collection, starting with version 2.2.0. Requires VCV Rack 2.6.6 or newer.

## Effects

| Effect | Sound |
|---|---|
| Room Reverb | Compact spaces and short ambience. |
| Plate Reverb | Smooth, spreading reverb tails. |
| Stereo Delay | Stereo echoes and rhythmic space. |
| Ping-Pong Delay | Alternating left/right echoes. |
| Chorus | Movement, width and thickening. |
| Flanger | Sweeping, comb-filtered motion. |
| Tremolo | Rhythmic changes in level. |
| Pitch Shift | Shifted pitches and layered textures. |
| Reverb Black Hole | Expansive, atmospheric reverb. |
| Reverb Plate Classic | Rich plate-style ambience. |
| Reverb Hall Medium | Spacious hall reflections. |
| Reverb Cloud | Diffuse, enveloping reverb textures. |

## Controls and connections

Click the display to browse effects and user presets. Turn **PROGRAM** or use **PREV/NEXT** to change the active effect. The three main controls and their display labels follow the selected effect.

**GAIN** adjusts input level; its centre position is unity. **MIX** blends the dry input and processed signal. **CLOCK** adjusts the processing clock; reducing it changes timing and adds texture. **SYNC** restarts the internal LFOs.

Connect audio to **IN L** and **IN R**, then take the stereo signal from **OUT L** and **OUT R**. With IN R unpatched, IN L feeds both channels. **CV 1–3** modulate the three effect parameters. **MIX CV** modulates wet/dry. **CLOCK CV** controls the clock or another destination selected through **Clock CV routing** in the context menu.

The context menu also provides three internal LFOs with selectable shape, rate, depth and destination. Rack's standard bypass remains available.

## Presets and reset

Use **Preset library → Save As / Create**, enter a name and press Enter to save a user preset. The library supports recall, duplication, renaming, editing, deletion and JSON import/export. Factory effects remain intact when you save your own settings. Favourite factory effects for quicker browsing.

User presets persist in the Rack user folder at `halfagiraf/sonavyre/library.json`. Rack patches also retain the current module settings. Ten additional preset slots are available in the context menu and are stored within the patch.

**Reset parameters** restores the three effect controls, input gain, mix and clock to their defaults. Rack's module reset returns the module to its initial factory configuration without deleting the saved user library or patch preset slots.

## Display

The flowing ribbons and particles respond to measured output audio. **Visual sensitivity** adjusts their response without changing the sound. Disable **Audio visualisation** in the context menu to reduce graphics work.

## Installation

Install the halfagiraf family package for your platform and search for **SONAVYRE** in Rack's module browser. See the [family installation instructions](../README.md#install).
