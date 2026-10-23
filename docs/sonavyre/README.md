# SONAVYRE and SONAVYRE PRO

Development builds of halfagiraf's Visual Stereo Effects Engine and Advanced Visual Multi-Effects Studio. The full production specification is **not yet complete**. Read [the audit and remaining work](AUDIT.md) before distributing any build.

## Build

From the family root:

```sh
make RACK_DIR=/path/to/Rack-SDK -j4
make -C products/sonavyre-pro RACK_DIR=/absolute/path/to/Rack-SDK -j4
```

Pro shares the source tree; its resource preparation copies only SONAVYRE assets and legal documents. Both products use normal Rack SDK build rules. SDK headers/libraries are not bundled. Windows/macOS compilation is not verified yet.

## Development controls

SONAVYRE's triangle provides the original three normalized engine parameters; names appear in the display and tooltips. The lower controls are input gain (middle is unity), program, wet/dry and engine clock. Pro puts wet/dry and the three engine parameters across its main row. Clock CV is routable in the context menu. Both editions bundle 246 factory programs: the original eight unchanged, the complete 200-program bank and 38 additional program binaries from the existing editor catalogue, included on the owner’s explicit confirmation of rights. Use **Big reverbs** in the Pro sidebar or context menu for Black Hole, Cloud, Infinite, Hall and Shimmer programs. No free-edition limit is applied in these development builds.

Three internal LFOs retain shape, target, rate and depth controls in the menu. Previous/next buttons and LFO restart remain accessible.

Click the display to browse/edit. Pro's sidebar opens factory, disk user library, favourites and supported effect categories. Search filters the effect menu. Use the preset library's name field and press Enter to create or rename. Save edits changes a user snapshot; factory originals remain read-only. Duplicate and export/import use module-state JSON wrapped in `sonavyre-preset-v1`. Rack's normal module presets are also available.

The combination menu enables two additional effects after the main processor, with serial/parallel mode, per-stage program/parameters/wet/dry and combination mix/bypass. The Pro screen's stage boxes toggle the corresponding real stages. Combination state is included in patch and named preset data. Imported-bank programs can be used by these stages.

Both editions retain stereo I/O, three engine CV inputs, wet/dry CV and clock CV. There are no MIDI, auxiliary audio or invented expansion ports. The flowing ribbon and particle field responds to measured output energy; animation can be disabled from the menu. It is an artistic energy visualization rather than a calibrated spectrum. Rack supplies module bypass.

User library path: `<Rack user directory>/halfagiraf/sonavyre/library.json`. Ten legacy-style preset slots also remain available within each patch. Reset does not erase those user slots or the disk library. Back up user libraries before development testing.

## Evidence

See [validation results](VALIDATION.md), [licensing/provenance](AUDIT.md), and [screenshots](screenshots/). Screenshots are rendered by the actual Rack application, not concept art.
