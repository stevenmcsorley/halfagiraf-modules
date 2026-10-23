# halfagiraf Library update candidate

Target: https://github.com/VCVRack/library/issues/965

The existing open issue already contains the Grainloom 2.1.0 submission at commit `b9721d4d67963a5e10caeec8e87290b404fc2225`. Do not create a second family issue. Leave that release/tag unchanged.

## Submission text (draft; not posted)

Version: 2.2.0

Source: https://github.com/stevenmcsorley/halfagiraf-modules

Commit: replace with the final reviewed, publicly reachable release commit after platform and licence clearance.

Adds SONAVYRE Free to the existing halfagiraf family, alongside Bad Sector, MOD1, Entwine, Constellate and Grainloom. Contains 12 stereo effects, including Reverb Black Hole, Reverb Cloud, Reverb Hall Medium and Reverb Plate Classic. Includes three effect controls, input gain, wet/dry, variable engine clock, original CV routing, LFOs, user presets, factory favourites and audio-responsive visualisation. Existing module slugs remain unchanged.

SONAVYRE STUDIO and SONAVYRE PRO are separate paid-plugin packages and are excluded from this family build.

## Before posting

- Review the actual release commit and obtain final owner publication approval, as requested in the original brief.
- Complete SONAVYRE-specific Windows x64, macOS Intel/Apple Silicon compilation and loading validation. The existing workflow builds the family on all four platforms; Grainloom's earlier successful runs do not validate SONAVYRE.
- Keep the family GPL-3.0-or-later licence and required legal notices. The owner has confirmed permission to develop and release the products, with no hardware affiliation or endorsement. Keep hardware/personal names out of release marketing. The absent original assembly generator is an inventory note, not an established release prohibition.
- Resolve the realtime/state/preset release blockers recorded in AUDIT.md. Current local packages are test candidates, not a production-readiness declaration.
- Push the reviewed commit, then post its full hash and version to the existing issue. Do not post a branch name as the release reference.

Official process: https://github.com/VCVRack/library#pushing-an-update
Commercial onboarding for Studio/Pro: contact@vcvrack.com, per https://github.com/VCVRack/library#adding-your-plugin-to-the-vcv-library-for-closed-source-freeware-and-commercial-plugins . No commercial email has been sent and no price has been committed.
