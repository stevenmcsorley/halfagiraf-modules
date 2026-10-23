# SONAVYRE engineering and provenance audit

Status: development checkpoint, not a release. SONAVYRE is provisional pending trademark clearance.

## Baselines and preservation

Inspected both complete clones, manifests, recent history, Makefiles, module registration, program arrays, firmware decoder, DSP, original module widgets, tests and licences before editing.

- Source: https://github.com/stevenmcsorley/halfagiraf-vcv at `1f00b67`.
- Family: https://github.com/stevenmcsorley/halfagiraf-modules at `b9721d4`.
- Untouched source checkout: `../upstream-halfagiraf-vcv` relative to the family checkout.
- Work branch: `feature/sonavyre`. No remote changes, publication, or store submission.
- Existing five family module implementations and identifiers are unchanged. Registration is additive. Original `AR-Helical/FXAid`, `AR-Helical/FXAidXL`, and `AR-Helical/FXAidPro` identifiers remain in the original repository, with its original panels and state format. The last source slug has display name Myriad.

## What actually exists

`src/FV1.hpp` is a 128-instruction interpreter, with 32768 samples of delay memory, written against the FV-1 instruction semantics. It is copied byte-for-byte to `src/sonavyre/core/FV1.hpp`. Individual effect algorithms have not been replaced.

The standard source bank has 32 slots: eight original assembled algorithms and 24 explicit bypass slots. Algorithms are Room Reverb, Plate Reverb, Stereo Delay, Ping-Pong Delay, Chorus, Flanger, Tremolo and Pitch Shift. Only the eight actual effects are presented as factory effects. The copied original bank header retains the complete original data for provenance; the host exposes only eight entries.

`FXAidCatalog.hpp` contains 238 fingerprints and names/control metadata. This is recognition metadata, not 238 bundled algorithms. It is not copied into the new product.

`FXAidProBank.hpp` contains 200 actual FV-1 binaries and names/control labels extracted from third-party firmware, as explicitly stated by its source comment. Neither the bank nor the offline editor's firmware data is copied into the new product or staged assets. This bank remains available in the untouched checkout for comparison. No independent redistribution permission was found.

The generator referenced by the standard-bank source comment (`gen_fxaid_bank.py`) and its preferred-form SpinASM sources are absent from this checkout. Their provenance and corresponding-source availability must be resolved before releasing those programs under GPL obligations. The eight originals are included in development builds under the repository's stated GPL licence; this is not a concluded release clearance.

## Architecture and identifiers

- Family plugin `halfagiraf`, module `Sonavyre`.
- Independent Pro plugin `halfagiraf-sonavyre-pro`, module `SonavyrePro`.
- Shared `engine/Engine.hpp` adapts the original advanced host, CV maps, interpolation/variable clock, gain/mix, three LFOs, firmware import and ten patch preset slots.
- `engine/Composition.hpp` adds two preallocated FV-1 instances. Serial runs the two enabled stages in sequence after the base engine. Parallel sums enabled branches at unity-normalized average. Stage wet/dry and parameters, combination mix, bypass and output guard are real processing. Disabled stages leave the original path untouched. Stage programs use the currently loaded bank. There is no feedback route or extra audio output.
- `presets/Library.hpp` is a separate UI-thread disk library at Rack's user directory `halfagiraf/sonavyre/library.json`, shared intentionally across both development editions. Factory bank data is never edited by library operations. Each saved module snapshot includes bank data when imported, LFO settings and combination state. Bank imports must be used only with appropriate rights.
- `ui/Interface.hpp` supplies both widgets, own SVG knobs/ports/buttons/screws, program browser/search, category/favourite selection, named preset operations, LFO controls and processing menus. Pro includes interactive stage-enable boxes and measured stereo indicators.
- Scope analysis publishes decimated stereo-summed samples through atomic floats. It does not claim to show proprietary internal grain/FFT state. Current meter display is instantaneous absolute output, not calibrated RMS or true peak. Decorative panel lines are static authored vectors, not fake audio telemetry.

All development capabilities are available in both editions. Commercial restrictions have deliberately not been introduced before full verification.

## Legal and distribution findings

Both repositories declare GPL-3.0-or-later. Keep upstream attribution and licence, include corresponding source and necessary build material when conveying binaries. A paid build of this derivative is not made proprietary by naming or artwork changes.

Legacy manufacturer/product names are retained here as provenance and identification, not in the new panels or product menus. Third-party firmware copyright is separate from the repository's licence. Public download availability does not establish permission to redistribute.

SONAVYRE panel/components are newly authored vector assets, with the established family mark reused from the family's panel generator. Uploaded concept images are reference-only and are not bundled. DejaVu Sans supplies the path-baked panel glyphs, and Rack supplies the runtime font; font notices are retained in `LICENSE-DejaVu.txt`. The new widgets do not use stock Grayscale component artwork. Existing family assets retain their pre-existing provenance; this work is not a new clearance of the entire family.

VCV's current primary guidance was checked on 2026-10-23:

- https://vcvrack.com/manual/PluginLicensing — GPL-compatible plugin distribution; a commercial non-GPL licence does not erase copied GPL obligations. Grayscale component artwork normally has non-commercial terms. Library ethics guidelines require independent identity/layout.
- https://github.com/VCVRack/library — open-source submission uses the Library issue process; commercial onboarding is arranged directly with VCV.
- https://vcvrack.com/manual/Manifest — unique identifiers, Rack 2 versioning and licence metadata.

No publication or paid-store action has been taken. No contact was sent to anyone.

## Release blockers and unfinished requirements

This checkpoint is not the requested completed production product.

1. Resolve rights to extracted firmware, catalogue data and preferred-form sources for the eight original programs. There is no legal basis established here to bundle the 200-program bank. Do not claim 200 included factory effects.
2. Replace inherited unsynchronised host state changes between UI/audio threads. The copied host updates parameter-name strings on program selection and allocates/captures/decodes firmware in its audio-listen path. These remain production blockers. The new visualization uses atomic data and the new composition allocates its VMs only at construction, but that does not fix the inherited host path.
3. Implement click-free program/bank transitions and an immutable bank handoff. Verify loading is transactional and does not leave partial data on malformed images. Current import parser is development code, not a hardened untrusted-file boundary.
4. Complete graphical stage parameter editing, effect-order swapping, stage gain UI, full undo/redo for all edits, proper preset dirty-state display, recently-used tracking and durable factory/user identity. Only preset recall currently uses Rack history; the graph enables/disables real stages, while detailed edits use menus.
5. Complete preset validation, concurrent library edit protection, robust disk-write failure UX, import size limits and migration/version validation. Factory favourites currently identify slot indices, so imported banks need stable fingerprint identities before release. The library name field commits on Enter.
6. Verify all reset scopes, including complete current-effect restore, factory-library confirmation and imported-bank handling. Parameter reset and module initialization exist, and module reset preserves ten user slots and the disk library. Complete combination reset is still required.
7. Add category-specific visualizations, chronological scope snapshots, calibrated/held meters, CPU reporting if available, animation reduction beyond the current scope toggle, and finish artwork/hitbox/zoom/contrast review against both uploaded references. Current screenshots are development UI, not final artwork acceptance.
8. Test all original modulation/routing/firmware variants, actual import files, patch restart through Rack, named-library GUI workflows, sustained audio/CPU use and realtime sanitizer/stress scenarios.
9. Run actual Windows and macOS builds and Rack loading. Only Linux x64 is built here. Use the SDK Makefile path on each platform; do not claim cross-platform validation from portable-looking C++.
10. Establish explicit final edition capabilities only after complete development verification; prepare compliant installable release packages and corresponding source together. No release packages are produced at this checkpoint.
