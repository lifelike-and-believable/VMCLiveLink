# Changelog

All notable changes to the VMC Live Link and VRM Interchange plugins. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and both plugins follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html). The two plugins ship separately but share version numbers and this file. One deviation: 1.0.0 is grouped by the refactor plan's phases, with Added, Changed and Fixed under each phase.

## [Unreleased]

### Added
- **VMC:** **Steady Frame Times** (on by default): the source gives frames evenly spaced times that follow the sender's rate, instead of the times they arrived. When frames arrive unevenly (a busy machine), Live Link's read time no longer lurches or steps back, so the avatar moves more smoothly. VMC sources now default Live Link's **Engine Time Offset** to 30 ms and **Max Number of Frames to Buffered** to 30, for about 50 to 70 ms from the sender to the avatar at 60 fps (smoothness plan, S-1 and O3a). A source saved in a preset with a different Engine Time Offset keeps it. `VMC.Stats` also prints how far frame times are behind their arrivals.
- **VMC:** `VMC.Stats` prints timing since the last report: the frames' arrival intervals (p50, p95, min, max), and how Live Link read the subject each tick. That covers how often it held the newest frame, how often its read time went backwards, how far behind the newest frame it read, and its smooth and clock offsets. These are the measurements for the [smoothness plan](Planning%20Docs/VMC_Smoothness_and_Performance_Plan_2026-10.md) (O1).
- **VRM:** the generated Live Link AnimBlueprint has a **VRM Expressions** node, after its Live Link Pose node and set to the character's avatar description, so a VMC stream's expressions reach the face without editing the AnimBlueprint.
- Fab packages for UE 5.6, 5.7 and 5.8: the release workflow makes a combined package and one per plugin for each engine, and checks that each names its engine.
- CI builds and tests every pull request on UE 5.6, 5.7 and 5.8, and fails if any tracked package was saved by an engine newer than 5.6 (`scripts/check_content_versions.ps1`). Pull requests from forks now fail the required check instead of skipping it, and editing a pull request no longer starts a run.
- Both plugins build and pass their tests on UE 5.7 and 5.8 as well as 5.6. On 5.8, VRM imports create their bones as Interchange joint nodes, so the bones keep their bind poses and the import no longer reports an error, and importing into an actor Blueprint no longer trips an engine check. The README, User Guide, plugin READMEs and contributor docs say so ([plan](Planning%20Docs/UE_5.7_5.8_Support_Plan_2026-10.md)).
- A [User Guide](docs/USER_GUIDE.md) for both plugins: install, import a VRM avatar, stream VMC to it, expressions, spring bones, retargeting and troubleshooting. The root README now leads with it, and both plugin READMEs were corrected against the code (generated asset names and folders, option names, defaults, the Live Link actor's **Subject**, and several missing settings).
- **Use Spring Centers** on the spring bone node. Turned off, springs that name a `center` bone are simulated in the node's Simulation Space instead, so moving and turning the character swings them (VRoid Studio gives every spring the `Root` center, which makes the character's own motion add nothing).

### Changed
- The `.uplugin` files in the repository no longer set `EngineVersion`; each Fab package sets it for its engine. With `5.6.0` there, UE 5.7 and later refused to load the plugins from source.
- The VRM Interchange project settings **Generate Post Process AnimBP**, **Assign Post Process ABP** and **Overwrite Existing Spring Assets** default to on. That is what imports already did (see Fixed), so new imports behave as before, but these settings now take effect when changed. A project that turned them off in its config now gets them off. The pipeline classes' own defaults are unchanged, so pipeline assets a studio saved from them load as saved; in the import dialog, though, every VRM pipeline (a studio's own included) starts from the project settings for these options.
- VRM import pipelines created in code (or as Blueprint subclasses) no longer take the project settings when they are created; they start from the class defaults. Call `ApplyProjectSettings` on them, or use the plugin's pipeline assets, to follow the project settings.
- The VRM import dialog starts each new import from the project settings, not from the choices made in the dialog last time. A reimport still uses the choices made when the asset was imported.

### Fixed
- `scripts/vmc_sender.py record` stamped each packet with the time it started waiting for it rather than when it arrived, so a replay sent every packet one interval early. `replay` and `send` slept with `time.sleep`, which on Windows before Python 3.11 rounds to 15.6 ms. Packets are now stamped on arrival and sent to within a millisecond.
- **VMC:** a new VMC subject had no interpolation (the Live Link panel showed **Interpolation: None**), so the avatar held each pose until the next arrived and moved in steps at the sender's rate. The source now creates its subject as Live Link creates one: with the animation role's settings class, pre-processors and interpolation from **Project Settings → Live Link**, and **Animation Interpolation** (which blends bone transforms) unless an animation-specific processor is set there. Unlike Live Link, it doesn't fall back to the project-wide default processor, which blends only curves. A subject that already exists, from a preset or the user's settings, keeps its own.
- **VRM:** an import no longer saves the Blueprints it generates or updates (the Live Link actor and AnimBlueprint, the retarget actor, the spring post-process AnimBlueprint) when the Blueprint editor's **Save on Compile** is on.
- **VMC:** the remapper's bone and curve names never reached the avatar. Live Link (UE 5.6 to 5.8) evaluates a subject with the names its source sent, not with the copy a remapper renamed, so an avatar driven through the VMC remapper stayed in its reference pose (and moved for one frame whenever the remapper changed). The VMC source now applies its subject's VMC remapper itself, to the static data and frames it sends, so the mapped names, rest translations and normalizer curves reach the Live Link Pose node.
- **VMC:** the remapper's **Mapping Tools** buttons (**Map Bones From Humanoid Metadata**, **Apply Preset**, **Auto-Detect Mapping**, ...) and **Live Mapping** table weren't shown in the Live Link panel, where the remapper is shown inline in the subject's settings: they appeared only in a details panel of the remapper itself. They are now at the end of the subject's details whenever its remapper is a VMC remapper.
- VMC Live Link read no humanoid metadata while Play In Editor was running, so **Map Bones From Humanoid Metadata** did nothing, and a subject created in PIE (whose remapper maps itself on creation) got an empty bone map. The metadata was read through the editor's asset scripting functions, which refuse to run during PIE; it is now read from the mesh's package directly. VRM Interchange writes the metadata the same way, so a VRM imported during a PIE session also gets it. Meshes imported before need no reimport: their metadata was saved.
- The VRM import pipelines ignored some project settings. The plugin's spring bone pipeline asset had been saved with its post-process AnimBlueprint and **Update Existing** options on, and those saved values replaced the project settings on every import (the IK Rig pipeline asset did the same for its **Update Existing**). The plugin's pipeline assets now take the project settings when they load, and again when a setting is edited. Copies of a pipeline (a reimport, the later files of an "Import All") also no longer lose a choice that equals the default.
- Spring nodes that no skin lists weren't made bones: VRoid Studio's `J_Sec_*_end` tails (22 in a typical avatar) and the leaves of a VRM 0.x bone group. In VRM 1.0 a joint whose tail is missing isn't simulated, so the last bone of each of those chains stayed still, and the spring data reported bones not in the skeleton. The skeleton now includes every spring node below a skin joint, with any nodes between it and that joint (also for collider shapes given only in `VRMC_node_collider`, and a `center` written as `{ "node": n }`). Reimport affected VRM files. A node between a spring node and its joint can sit above other joints, so a reimport can change existing bones' parents as well as add bones; skin joints keep their names.
- VRM imports created their MToon material instances without a parent material (no textures, every slot on the default material) and made no avatar description. The material and avatar description pipelines were registered as class paths, which Interchange can't instantiate; they now ship as pipeline assets in `DefaultPipelines`. In a project registered with 1.0.0, run **Project Settings > Plugins > VRM Interchange > Register VRM Import Pipelines** again (it replaces the old class-path entries), then reimport the VRM files.

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

[Unreleased]: https://github.com/lifelike-and-believable/VMCLiveLink/compare/54b69ea...HEAD
[1.0.0]: https://github.com/lifelike-and-believable/VMCLiveLink/releases
