# Changelog

All notable changes to the VMC Live Link and VRM Interchange plugins. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and both plugins follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html). The two plugins ship separately but share version numbers and this file.

## [Unreleased]

## [1.0.0] - 2026-09-29

The first release, after a review and refactor of both plugins (the [plan](Planning%20Docs/Code_Review_and_Refactor_Plan_2026-09.md)). Built and tested on Windows (Win64) with UE 5.6. The changes are grouped by the plan's phases.

### Phase 0: Guardrails

#### Added
- Pull requests are built (Editor without unity files, Game Development and Shipping) and the automation tests run headless on every pull request.
- Synthetic VRM test fixtures, made by `scripts/make_vrm_fixtures.py` and validated with cgltf.
- `scripts/vmc_sender.py`: sends, records and replays VMC streams, for testing without a real sender.
- Log categories for each plugin (`LogVMCLiveLink`, `LogVRMInterchange`, `LogVRMSpring`, ...), replacing `LogTemp`.

### Phase 1: Correctness

#### Fixed
- **VMC:** `/VMC/Ext/Root/Pos` is read as the specification defines it (with its name), and message arguments are type-checked. Bones are published in the Unity humanoid hierarchy with stable indices. A frame is published on each `/VMC/Ext/Blend/Apply`, and curves the sender didn't resend hold their value (or read 0, if set).
- **VMC:** a source keeps working after its settings change, and shuts down cleanly. Subject settings created by the source no longer replace ones the user made.
- **VMC:** the curve normalizer no longer overwrites curves the stream sends; presets no longer accumulate stale entries.
- **VRM:** joints are mapped by the skin's joint list, so files with several skins, unnamed joints or duplicate names import correctly.
- **VRM:** node transforms, rigid (unskinned) meshes and bind poses that differ from the rest pose import in the right place.
- **VRM:** VRM 0.x and 1.0 files are told apart, and both face +Y like the UE mannequin.
- **VRM:** one coordinate conversion for the mesh, skeleton and spring bones, so spring colliders sit on the right body parts.
- **VRM:** colour textures are sRGB; normal maps are linear with normal-map compression and the green channel flipped.
- **Spring bones:** VRM 1.0 spring data is read to the specification (per-joint parameters); VRM 0.x bone groups include every descendant bone.
- **Spring bones:** the anim node survives data swaps, teleports and LOD changes.
- **Spring bones:** spring data from older plugin versions is versioned; data that can't be upgraded is flagged for reimport.
- The editor no longer edits project settings or content at startup.

### Phase 2: Spring solver

#### Changed
- The spring bone solver follows the VRM 1.0 reference (UniVRM, three-vrm), with fixed sub-steps so the result doesn't depend on the frame rate, world-space inertia, and center-bone springs.
- Colliders (sphere, capsule, plane, and inside shapes) are resolved per chain with precomputed bones.

#### Added
- Compile-time validation in the spring bone AnimGraph node: missing spring data, empty data, data that needs a reimport, and bones missing from the skeleton.

### Phase 3: Architecture

#### Changed
- **VMC:** packets are received and frames built on a thread of their own (optional), with each frame timestamped on arrival. The protocol parser, frame assembler and source are separate and tested.
- **VMC:** all renaming happens in the remapper, through immutable workers.
- **VRM:** a file is read and parsed once per import (the new `VRMCore` module).
- **VRM:** the import pipelines share a base class and act on the assets their own import created.
- Both plugins are declared Win64 only, the platform they are built and tested on.

### Phase 4: Features

#### Added
- **VRM:** an avatar description asset (humanoid map, expressions, look-at, first person, licence) made next to each imported mesh, with a licence notice on import.
- **VRM:** the VRM Expressions AnimGraph node, which turns VRM 0.x or 1.0 expression curves into the avatar's morph targets, with binary expressions and overrides.
- **VRM:** an IK Rig generated from the humanoid map, with chains named like the UE mannequin's.
- **VRM:** MToon and unlit materials on generated toon master materials (shade and toon ramp, rim, matcap, emission, alpha modes, texture transform, outlines).
- **VRM:** morph target normals, and morph targets for every mesh, including unnamed ones.
- **VRM and VMC:** the humanoid map is written onto the mesh as metadata, and the VMC remapper can map a stream onto the mesh from it, without either plugin depending on the other.
- **VMC:** tracked devices and the sender's camera as Live Link subjects; `/VMC/Ext/OK` sender state in the status; the v2.1 root form (its scale and offset are read but not applied); an allowlist of senders and Lock to First Sender.

### Phase 5: Performance

#### Changed
- **VRM:** morph target payloads reuse the base mesh (an import with many morph targets went from seconds to under 0.1 s in the benchmark), and texture decoding is faster.
- Benchmarks for the VMC hot path, the spring solver, mesh payloads and texture decoding.

### Phase 6: Usability

#### Added
- **VMC:** a creation panel with every setting, a status line (frame rate, jitter, sender, port in use), and the `VMC.Stats` console command.
- **VMC:** the remapper's details panel: grouped settings, Mapping Tools buttons, and a Live Mapping table of every received name and whether it reaches the mesh.
- **VRM:** an import notification with Show in Content Browser, and a message log page per import listing its warnings by file.
- **VRM:** spring data editing tools: scale every joint's parameters, reset a spring to the file's values, and reimport the springs from the source file.

### Phase 7: Documentation and release

#### Added
- A README for the VMC Live Link plugin: quick start, sender setup, settings, the remapper, supported messages, coordinates and troubleshooting.
- `docs/ARCHITECTURE.md`, `CONTRIBUTING.md` and this changelog.
- Comments on every public header, with units and spaces for geometric fields.
- Documentation and support links in both plugin descriptors; README files packaged with the plugins.

#### Changed
- Superseded planning documents moved to `docs/archive/`.
- Both plugins are version 1.0.0.

[Unreleased]: https://github.com/lifelike-and-believable/VMCLiveLink/compare/release/1.0.0...HEAD
[1.0.0]: https://github.com/lifelike-and-believable/VMCLiveLink/releases/tag/release%2F1.0.0
