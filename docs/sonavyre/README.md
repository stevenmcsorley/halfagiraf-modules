# SONAVYRE and SONAVYRE PRO

Three editions of halfagiraf's Visual Stereo Effects Engine. The full production specification is **not yet complete**. Read [the audit and remaining work](AUDIT.md) before distributing any build.

## Build

From the family root:

```sh
make RACK_DIR=/path/to/Rack-SDK -j4
make -C products/sonavyre-studio RACK_DIR=/absolute/path/to/Rack-SDK -j4
make -C products/sonavyre-pro RACK_DIR=/absolute/path/to/Rack-SDK -j4
```

Pro shares the source tree; its resource preparation copies only SONAVYRE assets and legal documents. Both products use normal Rack SDK build rules. SDK headers/libraries are not bundled. Windows/macOS compilation is not verified yet.

## Development controls

SONAVYRE's triangle provides the original three normalized engine parameters; names appear in the display and tooltips. The lower controls are input gain (middle is unity), program, wet/dry and engine clock. Pro puts wet/dry and the three engine parameters across its main row. Clock CV is routable in the context menu. SONAVYRE Free, included in the family plugin, has **12 factory effects**: Room Reverb, Plate Reverb, Stereo Delay, Ping-Pong Delay, Chorus, Flanger, Tremolo, Pitch Shift, Reverb Black Hole, Reverb Plate Classic, Reverb Hall Medium and Reverb Cloud. SONAVYRE STUDIO is a separately packaged paid edition with **64 factory programs** and the compact panel. SONAVYRE PRO retains **246 factory program slots**, its wider interface and the composition engine. Prices remain provisional until commercial onboarding with VCV. All editions retain core parameters, CV, three internal LFOs, user presets and visualisation. Free and Studio compile only their selected program banks; there is no online activation or audio degradation.

Factory catalogue identities are persisted in new patches and presets, so shared effects map correctly between editions. Recall of a named preset using an unavailable or custom-bank effect is refused with an explanation. Opening an older full development patch directly in a curated edition falls back to Room Reverb for unavailable effects; use PRO to preserve the full development sound. The first eight original slots are unchanged. Custom-bank patch restoration remains supported by PRO; curated editions always restore their built-in banks. Back up development patches before migrating.

WAV program-bank import and audio-input firmware listening have been removed from all editions at the owner’s request. Preset JSON import/export and restoration of existing patch bank data remain supported.

Three internal LFOs retain shape, target, rate and depth controls in the menu. Previous/next buttons and LFO restart remain accessible.

Click the display to browse/edit. Pro's sidebar opens factory, disk user library, favourites and supported effect categories. Search filters the effect menu. Use the preset library's name field and press Enter to create or rename. Save edits changes a user snapshot; factory originals remain read-only. Duplicate and export/import use module-state JSON wrapped in `sonavyre-preset-v1`. Rack's normal module presets are also available.

In PRO, the combination menu enables two additional effects after the main processor, with serial/parallel mode, per-stage program/parameters/wet/dry and combination mix/bypass. The Pro screen's stage boxes toggle the corresponding real stages. Combination state is included in patch and named preset data. Previously saved custom-bank patches remain readable.

All editions retain stereo I/O, three engine CV inputs, wet/dry CV and clock CV. There are no MIDI, auxiliary audio or invented expansion ports. The flowing ribbon and particle field responds to measured output energy; animation can be disabled from the menu. It is an artistic energy visualization rather than a calibrated spectrum. Right-click **Visual sensitivity** offers Normal, Enhanced (default), High and Very high; this setting is saved in the patch and affects only graphics. Rack supplies module bypass.

User library path: `<Rack user directory>/halfagiraf/sonavyre/library.json`. Ten legacy-style preset slots also remain available within each patch. Reset does not erase those user slots or the disk library. Back up user libraries before development testing.

## Evidence

See [validation results](VALIDATION.md), [licensing/provenance](AUDIT.md), and [screenshots](screenshots/). Screenshots are rendered by the actual Rack application, not concept art.
