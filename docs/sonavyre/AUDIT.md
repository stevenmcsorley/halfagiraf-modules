# SONAVYRE engineering and provenance audit

Status: development checkpoint, not a release. The full bank is now bundled following the project owner’s explicit in-session confirmation of rights on 2026-10-23. SONAVYRE is provisional pending trademark clearance.

## Baselines and preservation

Inspected both complete clones, manifests, recent history, Makefiles, module registration, program arrays, firmware decoder, DSP, original module widgets, tests and licences before editing.

- Source: https://github.com/stevenmcsorley/halfagiraf-vcv at `1f00b67`.
- Family: https://github.com/stevenmcsorley/halfagiraf-modules at `b9721d4`.
- Untouched source checkout: `../upstream-halfagiraf-vcv` relative to the family checkout.
- Work branch: `feature/sonavyre`. No remote changes, publication, or store submission.
- Existing five family module implementations and identifiers are unchanged. Registration is additive. Original `AR-Helical/FXAid`, `AR-Helical/FXAidXL`, and `AR-Helical/FXAidPro` identifiers remain in the original repository, with its original panels and state format. The last source slug has display name Myriad.

## What actually exists

`src/FV1.hpp` is a 128-instruction interpreter, with 32768 samples of delay memory, written against the FV-1 instruction semantics. It is copied byte-for-byte to `src/sonavyre/core/FV1.hpp`. Individual effect algorithms have not been replaced.

The standard source bank has 32 slots: eight original assembled algorithms and 24 explicit bypass slots. Algorithms are Room Reverb, Plate Reverb, Stereo Delay, Ping-Pong Delay, Chorus, Flanger, Tremolo and Pitch Shift. The eight actual effects retain factory slots 0–7 for development-patch compatibility. The complete 200-program bank is appended at slots 8–207. The copied original bank header retains the complete original data for provenance; the host excludes its 24 bypass slots and exposes 246 actual programs in total. The 38 additional editor-catalogue programs occupy slots 208–245. Exact binary comparison confirms that the Pro bank contains 200 of the editor’s 238 unique program binaries; all 38 remaining binaries are now included without alterations.

`FXAidCatalog.hpp` contains 238 fingerprints and names/control metadata. This is recognition metadata, not 238 bundled algorithms. The recognition catalogue is not copied into the new product; named-record bank metadata supplies labels for the added programs.

`FXAidProBank.hpp` contains 200 actual FV-1 binaries and names/control labels extracted from third-party firmware, as explicitly stated by its source comment. The project owner subsequently confirmed that they have the rights and explicitly instructed bundling. The original header is now copied byte-for-byte as `presets/CompleteBank.hpp`, with all 200 binaries and their names/control labels retained. Only the missing 38 program binaries and their names/parameter labels are taken from the editor data; the firmware-image template/container is not copied. `CatalogueExtras.hpp` is reproducible using `tools/gen_sonavyre_catalogue.py`, which also supports `--check` against the untouched upstream source. This record preserves provenance and the basis for inclusion; it is not independent legal advice or a claim that the firmware was newly authored by this project.

The generator referenced by the standard-bank source comment (`gen_fxaid_bank.py`) and its preferred-form SpinASM sources are absent from this checkout. Their provenance and corresponding-source availability must be resolved before releasing those programs under GPL obligations. The eight originals are included in development builds under the repository's stated GPL licence; this is not a concluded release clearance.

## Architecture and identifiers

- Family plugin `halfagiraf`, module `Sonavyre`.
- Independent Pro plugin `halfagiraf-sonavyre-pro`, module `SonavyrePro`.
- Shared `engine/Engine.hpp` adapts the original advanced host, CV maps, interpolation/variable clock, gain/mix, three LFOs and ten patch preset slots. WAV import and the audio firmware-listen path were subsequently removed at the owner’s request; existing patch bank data remains compatible.
- `engine/Composition.hpp` adds two preallocated FV-1 instances. Serial runs the two enabled stages in sequence after the base engine. Parallel sums enabled branches at unity-normalized average. Stage wet/dry and parameters, combination mix, bypass and output guard are real processing. Disabled stages leave the original path untouched. Stage programs use the currently loaded bank. There is no feedback route or extra audio output.
- `presets/Library.hpp` is a separate UI-thread disk library at Rack's user directory `halfagiraf/sonavyre/library.json`, shared intentionally across both development editions. Factory bank data is never edited by library operations. Each saved module snapshot includes bank data when imported, LFO settings and combination state. Bank imports must be used only with appropriate rights.
- `ui/Interface.hpp` supplies both widgets, own SVG knobs/ports/buttons/screws, program browser/search, category/favourite selection, named preset operations, LFO controls and processing menus. Pro includes interactive stage-enable boxes and measured stereo indicators.
- Scope analysis publishes decimated stereo-summed samples through atomic floats. It does not claim to show proprietary internal grain/FFT state. The UI energy field now uses chronologically read output samples to drive ribbon/particle height, movement and illumination. It is an artistic output-energy view, not a frequency-calibrated spectrum or simulated internal DSP activity. Current meter display is instantaneous absolute output, not calibrated RMS or true peak. Decorative panel lines are static authored vectors, not fake audio telemetry.

All development capabilities are available in both editions. Commercial restrictions have deliberately not been introduced before full verification.

## Legal and distribution findings

Both repositories declare GPL-3.0-or-later. Keep upstream attribution and licence, include corresponding source and necessary build material when conveying binaries. A paid build of this derivative is not made proprietary by naming or artwork changes.

Legacy manufacturer/product names are retained here as provenance and identification, not in the new panels or product menus. Third-party firmware copyright is separate from the repository’s licence. Inclusion relies on the project owner’s explicit confirmation of rights, not on public download availability. Retain the applicable authorizations and original notices with release documentation.

SONAVYRE panel/components are newly authored vector assets, with the established family mark reused from the family's panel generator. Uploaded concept images are reference-only and are not bundled. DejaVu Sans supplies the path-baked panel glyphs, and Rack supplies the runtime font; font notices are retained in `LICENSE-DejaVu.txt`. The new widgets do not use stock Grayscale component artwork. Existing family assets retain their pre-existing provenance; this work is not a new clearance of the entire family.

VCV's current primary guidance was checked on 2026-10-23:

- https://vcvrack.com/manual/PluginLicensing — GPL-compatible plugin distribution; a commercial non-GPL licence does not erase copied GPL obligations. Grayscale component artwork normally has non-commercial terms. Library ethics guidelines require independent identity/layout.
- https://github.com/VCVRack/library — open-source submission uses the Library issue process; commercial onboarding is arranged directly with VCV.
- https://vcvrack.com/manual/Manifest — unique identifiers, Rack 2 versioning and licence metadata.

No publication or paid-store action has been taken. No contact was sent to anyone.

## Release blockers and unfinished requirements

This checkpoint is not the requested completed production product.

1. The owner has authorized inclusion of the complete 200-program bank; that prior inclusion blocker is resolved for this work. Retain the applicable permissions/notices with release records. This authorization is also applied to the 38 additional program binaries in the existing editor data. Preferred-form source availability for the eight original assembled programs still needs documenting before a corresponding-source release.
2. Replace inherited unsynchronised host state changes between UI/audio threads. The copied host updates parameter-name strings on program selection. This remains a production blocker. The audio firmware capture/decoder path has now been removed. The new visualization uses atomic data and the new composition allocates its VMs only at construction, but that does not fix the inherited host path.
3. Implement click-free program/bank transitions and an immutable bank handoff. The WAV image parser has been removed. Harden the remaining PRO legacy-bank JSON restore before release.
4. Complete graphical stage parameter editing, effect-order swapping, stage gain UI, full undo/redo for all edits, proper preset dirty-state display, recently-used tracking and durable factory/user identity. Only preset recall currently uses Rack history; the graph enables/disables real stages, while detailed edits use menus.
5. Complete preset validation, concurrent library edit protection, robust disk-write failure UX, import size limits and migration/version validation. Factory favourites now use stable catalogue IDs; imported PRO banks still need stable fingerprint identities before release. The library name field commits on Enter.
6. Verify all reset scopes, including complete current-effect restore, factory-library confirmation and imported-bank handling. Parameter reset and module initialization exist, and module reset preserves ten user slots and the disk library. Complete combination reset is still required.
7. Expand the current responsive ribbon/particle visualization with category-specific modes, calibrated/held meters, CPU reporting if available, animation reduction beyond the current scope toggle, and finish artwork/hitbox/zoom/contrast review against both uploaded references. Current screenshots are development UI, not final artwork acceptance.
8. Test all original modulation/routing variants, preset JSON import files, patch restart through Rack, named-library GUI workflows, sustained audio/CPU use and realtime sanitizer/stress scenarios.
9. Run actual Windows and macOS builds and Rack loading. Only Linux x64 is built here. Use the SDK Makefile path on each platform; do not claim cross-platform validation from portable-looking C++.
10. The owner has now requested edition separation: Free 12, Studio 64, Pro 246. Linux candidate packages are produced locally; cross-platform and release clearance remain outstanding. All remain GPL-3.0-or-later; paid distribution is a price distinction, not a claim of proprietary licensing.

## Owner clarification — 23 October 2026

The owner reports that the original hardware developer has explicitly agreed to these emulations being coded and released, and requests no personal or hardware-brand endorsement/mention in release announcements because of the separate hardware business. Product branding and release marketing will use halfagiraf and SONAVYRE only. Required copyright/licence notices remain in legal documentation; no endorsement is implied. This records the owner's account, not an independently reviewed agreement.

The absent original assembly generator remains a provenance question, not a demonstrated build defect. It does not prevent compiling or validating the supplied program binaries. Multiplatform SDK builds proceed against the current sources; any final source-distribution review must distinguish actual missing redistribution obligations from unverified assumptions.
