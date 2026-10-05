# Changelog

All notable changes to the VMC Live Link and VRM Interchange plugins. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and both plugins follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html). The two plugins ship separately but share version numbers and this file. One deviation: 1.0.0 is grouped by the refactor plan's phases, with Added, Changed and Fixed under each phase.

## [Unreleased]

### Added
- **Use Spring Centers** on the spring bone node. Turned off, springs that name a `center` bone are simulated in the node's Simulation Space instead, so moving and turning the character swings them (VRoid Studio gives every spring the `Root` center, which makes the character's own motion add nothing).

### Fixed
- Spring nodes that no skin lists weren't made bones: VRoid Studio's `J_Sec_*_end` tails (22 in a typical avatar) and the leaves of a VRM 0.x bone group. In VRM 1.0 a joint whose tail is missing isn't simulated, so the last bone of each of those chains stayed still, and the spring data reported bones not in the skeleton. The skeleton now includes every spring node below a skin joint, with any nodes between it and that joint (also for collider shapes given only in `VRMC_node_collider`, and a `center` written as `{ "node": n }`). Reimport affected VRM files. A node between a spring node and its joint can sit above other joints, so a reimport can change existing bones' parents as well as add bones; skin joints keep their names.
- VRM imports created their MToon material instances without a parent material (no textures, every slot on the default material) and made no avatar description. The material and avatar description pipelines were registered as class paths, which Interchange can't instantiate; they now ship as pipeline assets in `DefaultPipelines`, and registering the pipelines again replaces the old class-path entries.

## [1.0.0] - 2026-09-29

The first release, after a review and refactor of both plugins (the [plan](Planning%20Docs/Code_Review_and_Refactor_Plan_2026-09.md)). Built and tested on Windows (Win64) with UE 5.6. The changes are grouped by the plan's phases.

### Phase 0: Guardrails

#### Added
- Pull requests are built (Editor without unity files, Game Development and Shipping) and the automation tests run headless on every pull request.
- Synthetic VRM test fixtures, made by `scripts/make_vrm_fixtures.py` and validated with cgltf.
- `scripts/vmc_sender.py`: sends, records and replays VMC streams, for testing without a real sender.
- Log categories for each plugin (`LogVMCLiveLink`, `LogVRMInterchange`, `LogVRMSpring`), replacing `LogTemp`.

### Phase 1: Correctness

#### Fixed
- **VMC:** `/VMC/Ext/Root/Pos` is read as the specification defines it (with its name, and the v2.1 form, whose scale and offset are read but not applied), and message arguments are type-checked. Bones are published in the Unity humanoid hierarchy with stable indices. A frame is published on each `/VMC/Ext/Blend/Apply`, and curves the sender didn't resend hold their value (or read 0 with **Zero Missing Curves**).
- **VMC:** removing a source right after creating it no longer crashes, and a source shuts down cleanly. Subject settings created by the source no longer replace ones the user made.
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
- **VMC:** packets are received and frames built on a thread of their own (on by default), with each frame timestamped on arrival. The protocol parser, frame assembler and source are separate and tested. Editing a source's settings applies them to the running source (a new port or subject restarts the listener).
- **VMC:** all renaming happens in the remapper, through immutable workers.
- **VRM:** a file is read and parsed once per import (the new `VRMCore` module).
- **VRM:** the import pipelines share a base class and act on the assets their own import created.
- Both plugins support Win64 only, the platform they are built and tested on.

### Phase 4: Features

#### Added
- **VRM:** an avatar description asset (humanoid map, expressions, look-at, first person, licence) made next to each imported mesh, with a licence notice on import.
- **VRM:** the VRM Expressions AnimGraph node, which turns VRM 0.x or 1.0 expression curves into the avatar's morph targets, with binary expressions and overrides.
- **VRM:** an IK Rig generated from the humanoid map, with chains named like the UE mannequin's.
- **VRM:** MToon and unlit materials on generated toon master materials (shade and toon ramp, rim, matcap, emission, alpha modes, texture transform, outlines).
- **VRM:** morph target normals, and morph targets for every mesh, including unnamed ones.
- **VRM and VMC:** the humanoid map is written onto the mesh as metadata, and the VMC remapper can map a stream onto the mesh from it, without either plugin depending on the other.
- **VMC:** tracked devices and the sender's camera as Live Link subjects (opt-in: **Device Subjects**, **Camera Subject**); `/VMC/Ext/OK` sender state in the status; an allowlist of senders and Lock to First Sender.

### Phase 5: Performance

#### Changed
- **VRM:** morph target payloads reuse the base mesh: building the payloads for 60 morph targets went from 5.85 s to 85 ms in the benchmark. Texture decoding is about 10% faster.

#### Added
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
[1.0.0]: https://github.com/lifelike-and-believable/VMCLiveLink/releases
