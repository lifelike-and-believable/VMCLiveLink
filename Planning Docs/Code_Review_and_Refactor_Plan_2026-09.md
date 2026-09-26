# VMCLiveLink and VRMInterchange: Code Review and Implementation Plan

**Date:** 2026-09-25
**Scope:** `Plugins/VMCLiveLink` (runtime and editor modules) and `Plugins/VRMInterchange` (VRMInterchange, VRMInterchangeEditor, VRMSpringBonesRuntime, VRMSpringBonesEditor), plus build, CI and documentation.
**Baseline commit:** `d1c7c20` (main)
**Target engine:** UE 5.6 (per both `.uplugin` files)

This document has two parts:

- **Part A: Review findings.** Each finding has an ID, a severity, file/line evidence, and the impact.
- **Part B: Implementation plan.** Tasks grouped into phases. Each task lists the findings it resolves, the files involved, the steps, and acceptance criteria. A coding agent should be able to pick up any task whose dependencies are done.

> **Progress (2026-09-26):** 23 plan tasks are merged (P0.1 to P0.5 with P0.5 in part, P1.1 to P1.9, P1.11 to P1.18, and P7.1), plus two unplanned fixes. Phase 1 is done except P1.10. `main` builds and passes all 39 VRM and VMC tests. The CI runner intermittently blocks freshly built DLLs (X-07) and needs an owner fix. See [B.10](#b10-implementation-progress).

> **How this review was done.** Every first-party source file (about 6,000 lines, excluding `cgltf.h`) was read in full. The review environment has no Unreal Engine install, so nothing was compiled or run. Findings marked **[Verify]** depend on external specs or runtime behaviour and must be confirmed in the editor (or against the spec) before the fix is written. The others follow directly from the code.

---

## Contents

- [Part A: Review findings](#part-a-review-findings)
  - [A.0 Executive summary](#a0-executive-summary)
  - [A.1 VMCLiveLink: OSC source and Live Link push](#a1-vmclivelink-osc-source-and-live-link-push)
  - [A.2 VMCLiveLink: remapper, mapping asset, factories](#a2-vmclivelink-remapper-mapping-asset-factories)
  - [A.3 VRMInterchange: translator (mesh, skeleton, textures, materials)](#a3-vrminterchange-translator)
  - [A.4 VRMInterchange: spring bone parser and data](#a4-vrminterchange-spring-bone-parser-and-data)
  - [A.5 VRMSpringBonesRuntime: solver anim node](#a5-vrmspringbonesruntime-solver-anim-node)
  - [A.6 VRMInterchangeEditor: pipelines and editor module](#a6-vrminterchangeeditor-pipelines-and-editor-module)
  - [A.7 Cross-cutting: build, CI, tests, docs](#a7-cross-cutting-build-ci-tests-docs)
- [Part B: Implementation plan](#part-b-implementation-plan)
  - [B.0 Rules for the implementing agent](#b0-rules-for-the-implementing-agent)
  - [Phase 0: Guardrails](#phase-0-guardrails)
  - [Phase 1: Critical correctness fixes](#phase-1-critical-correctness-fixes)
  - [Phase 2: Spring solver rework](#phase-2-spring-solver-rework)
  - [Phase 3: Architectural refactoring](#phase-3-architectural-refactoring)
  - [Phase 4: Functional features](#phase-4-functional-features)
  - [Phase 5: Optimization](#phase-5-optimization)
  - [Phase 6: Usability](#phase-6-usability)
  - [Phase 7: Documentation](#phase-7-documentation)
  - [B.8 Decisions needed from the owner](#b8-decisions-needed-from-the-owner)
  - [B.9 Dependency graph and suggested order](#b9-dependency-graph-and-suggested-order)
  - [B.10 Implementation progress](#b10-implementation-progress)
- [Appendix](#appendix)

---

# Part A: Review findings

**Severity scale**

| Level | Meaning |
|---|---|
| **P0** | Crash, memory safety, or output that is wrong for common, spec-conformant input |
| **P1** | Functionally incorrect or missing behaviour users will hit; data loss; silent modification of user content |
| **P2** | Architecture, performance, maintainability, or robustness problems |
| **P3** | Polish, naming, dead code, cosmetic UI |

## A.0 Executive summary

The plugins cover a lot of ground (VMC ingest, remapping, VRM import, spring physics, asset scaffolding), and the code is readable. However, several core paths only work for the specific files and senders they were tested with. Most of the P0/P1 issues follow one pattern: the implementation matches a test fixture or a single exporter rather than the VMC or glTF/VRM specification.

The ten most important findings:

1. **VMC `/VMC/Ext/Root/Pos` is parsed with the wrong argument layout** (expects 7 floats; the spec sends a string name plus 7 floats, plus 6 more in v2.1). Root motion from spec-conformant senders is likely dropped entirely. (VMC-01) **[Verify]**
2. **The Live Link skeleton is flat and its indices get corrupted.** Every bone is parented to index 0, and inserting `root` at index 0 afterwards shifts every index and produces two roots. (VMC-03, VMC-04)
3. **The VRM translator reads `JOINTS_0` as glTF node indices** instead of indices into `skin.joints`, and **ignores every skin except `skins[0]`**. Skin weights are wrong for any file where joint order differs from node order or where meshes use different skins (typical for UniVRM/VRoid exports). (T-01, T-02)
4. **Spring colliders and gravity are in the wrong coordinate frame.** Collider offsets, capsule tails and plane normals are scaled but never axis-converted. Gravity uses a different axis mapping than the mesh. (SP-01)
5. **VRM 0.x spring chains are never expanded.** Only the root bone of each `boneGroup` is simulated, with a synthetic 7 cm tail. (SP-02)
6. **The spring solver runs in component space, lags one frame per chain link, and is frame-rate dependent.** Hair does not react to character motion, and behaviour changes with FPS. The README claims deterministic sub-stepping, which does not exist. (SR-01 to SR-03)
7. **The VMC source replaces the user's subject settings on every connect.** It unconditionally calls `CreateSubject` with fresh defaults, which discards remapper configuration from Live Link presets. (VMC-05)
8. **The default "MetaHuman curve normalizer" overwrites real data.** It copies `mouthSmileLeft` over `mouthSmileRight` and replaces `mouthPucker`, and it is on by default. (VMC-07)
9. **The editor changes user projects without asking.** On every editor start the module rewrites and saves the project's Interchange settings. A static global hook loads and reparents any material instance named `MI_VRM_*` whenever the asset registry reports it, including during the startup scan. (PE-03, PE-04)
10. **Import pipelines can attach generated assets to the wrong character.** They locate "the imported mesh" by scanning folders, falling back to the parent folder (for example `/Game`) and taking the first result. (PE-02)

Architecturally, the biggest opportunity is a **shared VRM avatar description** (humanoid bone map, expressions, VRM version, meta). Today VRMInterchange never imports the humanoid map or the expressions, so VMCLiveLink has to guess mappings from bone-name heuristics, and the IK Rig generator depends on a template that only matches VRoid naming. Importing this data once and letting both plugins consume it removes most of the guesswork (X-01).

---

## A.1 VMCLiveLink: OSC source and Live Link push

Files: `Plugins/VMCLiveLink/Source/VMCLiveLink/Private/VMCLiveLinkSource.cpp` (abbreviated `Source.cpp`), `Public/VMCLiveLinkSource.h`, `Private/VMCLiveLinkSourceFactory.cpp`.

### VMC-01 (P0) `/VMC/Ext/Root/Pos` argument layout does not match the VMC spec **[Verify]**
- `Source.cpp:40-56` (`ReadFloat7`) requires exactly 7 float arguments. `Source.cpp:202-206` drops the message otherwise.
- The VMC protocol defines `/VMC/Ext/Root/Pos` as `(string){name} (float)px py pz qx qy qz qw`. v2.1 appends `(float)sx sy sz ox oy oz` (scale and offset), which gives 8, 11 or 14 arguments.
- **Impact:** with VSeeFace, the VirtualMotionCapture app, Warudo and similar senders, root position and rotation never arrive. The code comment at `Source.cpp:393-396` says root data comes from "the bundled sample sender", which suggests the parser was written to match a custom test sender.
- **Fix direction:** accept 8/11/14 arguments with a leading string. Optionally keep the 7-float form as a legacy fallback. Apply v2.1 scale/offset when present (at minimum, parse and expose it).

### VMC-02 (P0) Use-after-free risk in `ReceiveClient`
- `Source.cpp:102-105` queues `AsyncTask(GameThread, [this]{ EnsureSubjectSettingsWithDefaults(); })` with a raw `this`. If the source is removed before the task runs (for example, created and immediately removed, or a preset swap), the task dereferences a freed object.
- `ReceiveClient` already runs on the game thread, and the Apply handler already calls `EnsureSubjectSettingsWithDefaults()` synchronously (`Source.cpp:257-260`), so the deferred call is redundant.
- There is no destructor that calls `StopOSC()`. The OSC delegate is bound with `AddRaw(this, ...)` (`Source.cpp:146`), so a source destroyed without `RequestSourceShutdown` leaves a dangling delegate.
- **Fix direction:** remove the `AsyncTask`, or capture `AsWeak()`. Add a destructor that calls `StopOSC()`.

### VMC-03 (P1) Inserting `root` at index 0 corrupts the hierarchy
- Bones are appended as they arrive: the first bone gets parent `-1`, every later bone gets parent `0` (`Source.cpp:190-199`).
- When `Root/Pos` arrives, `root` is inserted at index 0 (`Source.cpp:224-230`). The existing `BoneParents` values are not re-indexed, so every former "parent 0" now points at `root`, and the former first bone (usually `Hips`) keeps parent `-1`. The result has two roots.
- `PendingPose` is keyed by name, so rotations survive, but the static data advertises a broken skeleton.

### VMC-04 (P1) The Live Link skeleton has no real hierarchy
- Every non-root bone is parented to bone 0 (`Source.cpp:192-193`). The VMC `Bone/Pos` values are **local** transforms relative to each bone's Unity humanoid parent, so any consumer that composes the hierarchy gets wrong results. This includes the Live Link debug skeleton, component-space nodes, Take Recorder baking, and IK Retargeter-from-Live-Link setups.
- Name-matched retargeting (Live Link Pose node) partly hides this because it copies local transforms by name.
- **Fix direction:** use a static Unity `HumanBodyBones` parent table (VMC bone names are Unity humanoid names) and assign parents by name. Unknown names fall back to `root`/`Hips`.

### VMC-05 (P1) Subject settings are replaced on every connect
- `EnsureSubjectSettingsWithDefaults` (`Source.cpp:536-572`) always builds a new `ULiveLinkSubjectSettings` with a fresh default remapper and calls `Client->CreateSubject(Preset)`. The comment says "Bootstrap the subject settings if they don't exist yet", but nothing checks whether they already exist.
- **Impact:** a Live Link preset that stores a configured remapper, or a remapper the user configured before the stream started, is replaced by defaults when the source connects.
- **Fix direction:** call `Client->GetSubjectSettings(Key)` first and only create or attach defaults when there is no subject or no remapper.

### VMC-06 (P1) Names are remapped twice
- The source applies `CachedBoneMap`/`CachedCurveMap` to the names it publishes (`Source.cpp:302-304`). Live Link then runs the subject's remapper, whose worker applies `BoneNameMap`/`CurveNameMap` again (`VMCLiveLinkRemapper.h`, `RemapStaticData`).
- Identity-like maps hide this, but chained maps (A to B and B to C) or maps whose targets collide with source names produce wrong results. Ref-pose offsets are looked up by the *mapped* name (`Source.cpp:424-425`), which couples the source to the remapper.
- **Fix direction:** publish raw VMC names from the source and do all renaming in the remapper (Live Link's intended design). Move ref-offset handling into the remapper worker or a dedicated pose-processing step.

### VMC-07 (P1) The curve "normalizer" overwrites real data and is on by default
- `VMCLiveLinkRemapper.h:97-111`: when `mouthSmileLeft` exists it writes the same value into **both** smile curves, which erases asymmetric smiles from ARKit sources. It also always sets `mouthPucker = mouthFunnel * 0.5`, which overwrites the real pucker value.
- `bEnableMetaHumanCurveNormalizer = true` by default (lines 37 and 206).
- Blink mirroring only runs when one side is missing, which is correct. The smile and pucker logic has no such guard.
- Each `Get`/`Set` call does a linear `IndexOfByKey` over all property names, every frame.
- **Fix direction:** default the normalizer to off, only synthesize a curve when the target is absent, cache indices when static data changes, and document each rule.

### VMC-08 (P1) The VMC/VRM preset maps several sources onto one target
- `VMCLiveLinkRemapper.cpp:282-305`: `Joy` and `I` both map to `mouthSmileLeft`; `Blink` and `Blink_L` both map to `eyeBlinkLeft`. The static data then contains duplicate property names, and which value wins is undefined.
- Presets use `FindOrAdd` and never clear existing entries, so switching presets accumulates stale mappings.
- There is no VRM 1.0 expression preset (`happy`, `angry`, `sad`, `relaxed`, `surprised`, `aa`, `ih`, `ou`, `ee`, `oh`, `blink`, `blinkLeft`, `blinkRight`, `lookUp`, ...), which newer senders use.

### VMC-09 (P1) `UVMCLiveLinkRemapper::Initialize` overrides user state and can hitch
- `VMCLiveLinkRemapper.cpp:46-73`: every `Initialize` seeds identity maps, calls `GuessPreset`, **assigns `Preset` and calls `ApplyPreset`**, then `SeedFromReferenceSkeleton`. This can scan the asset registry and **synchronously load every mapping asset** (`AutoDetectAndApplyMapping`, lines 445-519).
- **Impact:** the user's chosen preset and hand edits get overwritten on re-init (editor restart, preset load, subject re-creation). Loading assets synchronously during Live Link initialization can hitch the game thread.
- `GuessPreset` reads the reference mesh through `GetBoneNames(TSoftObjectPtr)` (lines 20-32), which silently returns nothing when the mesh isn't loaded, so VRoid detection depends on load order.

### VMC-10 (P1) The remapper worker is mutated while in use
- `SyncWorker()` (`VMCLiveLinkRemapper.cpp:82-91`) assigns `TMap`s on the live `FVMCLiveLinkRemapperWorker` that Live Link uses to remap frames. Frames are pushed with `*_AnyThread` APIs, so this is a data race.
- **Fix direction:** treat workers as immutable snapshots. Build a new worker and swap the shared pointer, then set `bDirty`.

### VMC-11 (P1) Curves are zeroed every frame
- `Source.cpp:275-276` resets `PendingCurves` after each `Blend/Apply`, and `PushFrame` fills missing curves with 0 (`Source.cpp:367-368`). Senders that send only changed blendshapes, or that split blendshapes across bundles, cause flicker to zero.
- **Fix direction:** hold the last value by default, with an option to zero.

### VMC-12 (P1) Static and frame data are pushed in the wrong order, and too often
- In the Apply handler (`Source.cpp:266-282`), when a new curve name appears in the same bundle, a frame with N+1 property values can be pushed against static data with N names, and static data is republished *after* the frame. Static data can be pushed up to three times per Apply.
- Each static push resets the subject's frame buffer in Live Link, so extra pushes add jitter.
- **Fix direction:** decide whether static data is dirty *before* building the frame, push static data at most once, then push the frame.

### VMC-13 (P2) The hot path allocates and copies a lot
Per Apply (60 to 120 Hz):
- `RefreshStaticMapsFromSettings()` (`Source.cpp:485-534`) looks up subject settings, **copies both maps**, hashes them, and calls `ReferenceSkeleton.LoadSynchronous()` (line 511), which can block if the mesh isn't loaded.
- `PushFrame()` (`Source.cpp:327-356`) copies 7 containers, including 4 `TMap`s, under the lock, then does name-keyed `TMap` lookups for every bone and curve.

Per message:
- `Msg.GetAddress().GetFullPath()` allocates an `FString`, and the address is compared as a string (`Source.cpp:174`).
- `FName(*Bone)` hashes the string into the global name table.
- `BoneNames.Contains()` and `CurveNameToIndex.Contains()` then do further lookups.

Also: rotation is set twice in `PushFrame` (lines 383-388 and 410-417 repeat the same logic).

### VMC-14 (P2) The threading model is unclear
- The comment at `Source.cpp:252-256` says OSC dispatch happens on the game thread, which would make `DataGuard` unnecessary. Yet the code locks in some places and not others (`bStaticSent`, `bForceStaticNext`, `LastSeenRemapper` and the cached maps are touched outside the lock). `PushStaticData` takes the lock and is also called while the caller holds it (line 281, re-entrant through `FCriticalSection`, which works but hides intent).
- **Fix direction:** choose one model. Recommended: parse on the OSC receive thread into a lock-free or double-buffered frame, and push with the `_AnyThread` APIs, so latency no longer depends on game frame rate. Otherwise keep everything on the game thread and remove the locks. Document the choice.

### VMC-15 (P2) Frames have no timing information
- Frames are pushed without setting `WorldTime`/`MetaData.SceneTime`, and `/VMC/Ext/T` (sender time) is ignored. When several bundles are processed in one game tick they get the same timestamp, so Live Link interpolation and buffering can't smooth jitter.

### VMC-16 (P2) The user can't see stream health
- `GetSourceStatus()` only returns "Stopped", "Waiting for first frame" or "Receiving data" (lines 122-130). It never reports "no data for N seconds" after data stops. `GetSourceMachineName()` always returns "Local/Network". There are no message or frame-rate counters.
- If the port is already in use, the only feedback is a log line (`Source.cpp:150`). The source shows "Stopped" with no reason.

### VMC-17 (P2) Configuration is limited and duplicated
- `YawOffsetDeg`, `bUseRefOffsets`, `bPreferIncomingTranslations` and the bind address cannot be set from the UI or the connection string (always 0, `true`, `false`, `0.0.0.0`).
- There is no `ULiveLinkSourceSettings` subclass, so settings can't be edited after creation.
- The connection-string parsing is copy-pasted four times (`SourceFactory.cpp:17-91`) with no validation (`port=0`, `port=70000` and non-numeric values are all accepted).
- The source has four overlapping constructors (`Source.cpp:76-95`).
- `UVMCLiveLinkSettings::DefaultReferenceSkeleton` is declared and shown in Project Settings but never read.

### VMC-18 (P3) Garbled UI labels
- `SourceFactory.cpp:192` shows `"Unity?UE coords"` and line 201 shows `"Meters?cm"`. These look like an arrow character lost to an encoding conversion.

### VMC-19 (P2) Unsupported VMC messages
- Ignored: `/VMC/Ext/OK` (loaded/calibration state), `/VMC/Ext/T` (time), `/VMC/Ext/Tra/Pos`, `/VMC/Ext/Hmd/Pos`, `/VMC/Ext/Con/Pos` (trackers and devices), and `/VMC/Ext/Cam` (camera).
- There is no filtering by sender IP when several senders use the same port.

### VMC-20 (P2) Input validation and exposure
- Argument types are never checked: `GetFloat()`/`GetString()` are called without `IsFloat()`/`IsString()` checks (`Source.cpp:34-36, 238-239`). Malformed packets produce zeros or empty names instead of being rejected.
- The listener binds `0.0.0.0` (all interfaces) with no allowlist. For a live-show tool on shared networks, a bind-address option and a sender allowlist are useful. Rate limiting is already tracked in `PRODUCTION_READINESS_ACTION_ITEMS.md` §2.1.

### VMC-21 (P2) Windows-only platform allowlist
- `VMCLiveLink.uplugin` limits both modules to `Win64`. OSC and Live Link are cross-platform, so macOS and Linux editors are excluded for no stated reason.

---

## A.2 VMCLiveLink: remapper, mapping asset, factories

### VMC-22 (P2) Editor-only code lives in the runtime module
- `VMCLiveLinkRemapper.cpp` includes `Editor.h`, `AssetToolsModule.h` and `Factories/DataAssetFactory.h`, and `VMCLiveLink.Build.cs` adds `UnrealEd`/`AssetTools`/`Slate` when `bBuildEditor`. This works but mixes concerns. Asset creation, auto-detect and signature capture belong in `VMCLiveLinkEditor`, for example through an `IDetailCustomization` for `UVMCLiveLinkRemapper`.
- `SaveCurrentMappingTo(Asset, bool)` is marked `CallInEditor` but takes parameters, so it never appears as a button.

### VMC-23 (P2) Mapping-asset signature and auto-detect
- `ComputeSignature` (`VMCLiveLinkMappingAsset.cpp:17-38`) hashes with `GetTypeHash(FString)`, whose algorithm is not guaranteed to stay the same across engine versions. The hash is **persisted** in assets (`SkeletonSignatures`), so an engine upgrade could silently break matching. Use an explicit algorithm such as `FCrc::StrCrc32` or CityHash64 on normalized names, and store a version.
- Auto-detect calls `AD.GetAsset()` on every mapping asset (`VMCLiveLinkRemapper.cpp:464, 493`). Expose signatures as an `AssetRegistrySearchable` tag and filter by tag before loading.
- `MatchesMesh` is `BlueprintCallable` but declared inside `#if WITH_EDITOR` (`VMCLiveLinkMappingAsset.h:29-38`). A Blueprint that calls it will fail to compile or cook in non-editor builds.

### VMC-24 (P2) Deprecated or fragile editor APIs
- `FAssetTypeActions_Base` (`AssetTypeActions_VMCLiveLinkMappingAsset.*`) is superseded by `UAssetDefinition` in UE 5.2+.
- `UVMCLiveLinkRetargetActorFactory` duplicates a Blueprint with `StaticDuplicateObject` (`VMCLiveLinkRetargetActorFactory.cpp:36`) and logs to `LogTemp`. Use `IAssetTools::DuplicateAsset`, which handles Blueprint generated classes and redirectors correctly.

### VMC-25 (P3) Dead code and hygiene
- Bone mappings are commented out in `SeedCurves_VMC_VRM` (lines 307-312).
- `UVMCLiveLinkSettings` has no `VMCLIVELINK_API`, which is fine unless another module needs it. `ELLRemapPreset` has a stray blank line before `enum class`.
- There are leftover authoring comments such as `// new`, `// NEW` and `// <-- important`.

### VMC-26 (P2) No tests
- VMCLiveLink has no automation tests: nothing covers message parsing, coordinate conversion, hierarchy building, remapping or connection-string parsing.

---

## A.3 VRMInterchange: translator

File: `Plugins/VRMInterchange/Source/VRMInterchange/Private/VRMTranslator.cpp` (abbreviated `Tr.cpp`).

### T-01 (P0) `JOINTS_0` is interpreted as node indices
- `ReadJointsWeights` (`Tr.cpp:1179-1208`) looks up each joint value in `NodeToBone`, which maps **node index to joint index** (built in `BuildNodeToBoneMap`, lines 814-824). In glTF 2.0, `JOINTS_n` values are **indices into `skin.joints`**, not node indices.
- Missing lookups silently become bone 0.
- **Impact:** skinning is only correct when `skin.joints[i] == i` for all i. Otherwise vertices bind to unrelated bones.

### T-02 (P0) Only `skins[0]` is used
- `Tr.cpp:977-978` assumes one skin. UniVRM (and so VRoid Studio) commonly writes one skin per SkinnedMeshRenderer, each with its own `joints` list and order. **[Verify with a VRoid export]**
- **Impact:** primitives belonging to other skins are weighted against the wrong joint list, and bones that appear only in other skins (often hair and cloth bones used by spring chains) are **missing from the skeleton**.
- **Fix direction:** build the skeleton from the union of all skins' joints (preferably from the full node hierarchy under the scene root). For each mesh node, remap `JOINTS_n` through **that node's** `skin.joints` to the unified bone index.

### T-03 (P1) Meshes are imported per mesh instead of per mesh node
- `MergePrimitivesFromMeshes` iterates `Data->meshes` (`Tr.cpp:831`). Consequences:
  - Non-skinned mesh nodes (accessories, rigid props under the head) ignore their node's global transform and are bound 100% to bone 0 rather than to their parent joint.
  - A mesh referenced by several nodes is imported once. A mesh referenced by no node is imported anyway.
- **Fix direction:** walk the scene's nodes. For skinned nodes, use skin data. For rigid nodes, transform vertices by the node's global bind transform and weight them to the nearest ancestor joint.

### T-04 (P1) The bind pose is derived from joint-local TRS only
- `PopulateBonesFromSkin` (`Tr.cpp:1499-1578`) walks up to the nearest ancestor *that is a joint* but uses only the node's own local TRS (line 1540). Transforms of intermediate non-joint nodes (for example an `Armature` node with scale or rotation) and of the root's ancestors are dropped. Node scale is discarded (line 1575). `inverseBindMatrices` are ignored.
- **Impact:** wrong bone positions whenever non-joint ancestors carry transforms, and mesh/skeleton disagreement when the rest pose is not the bind pose.
- The "reference pose fix" zeroes all bone rotations. That suits VRM 0.x T-pose and VMC local rotations, but it is a significant design choice that needs documenting and testing (see decision D-3).

### T-05 (P1) VRM 0.x and 1.0 are imported with the same forward axis **[Verify]**
- The VRM 1.0 spec states that models face **+Z** in glTF space. VRM 0.x models face **−Z**. The translator never checks the VRM version (no `VRM` vs `VRMC_vrm` extension check) and applies one fixed conversion to both, so one of the two will import rotated 180° about up.
- The README's troubleshooting entry "Mesh Orientation Wrong" suggests users have hit this.
- **Fix direction:** detect the version in the translator and apply a 180° yaw for the version that needs it, consistently for mesh, skeleton, morphs and spring data.

### T-06 (P1) Normal and data textures are imported as sRGB colour
- `GetTexturePayloadData` always creates images with `bSRGB = true` (`Tr.cpp:766`). Normal, metallic-roughness and occlusion maps must be linear.
- glTF normal maps use the OpenGL (+Y) convention and UE expects DirectX (−Y), so the green channel must be flipped. This is not done.
- **Fix direction:** tag each texture's usage from the materials that reference it, and set sRGB off, `TC_Normalmap` compression and green flip on the texture factory node (`UInterchangeTexture2DFactoryNode` custom attributes) instead of in the payload.

### T-07 (P1) Most material data is ignored
- MToon (VRM 0.x `materialProperties`, VRM 1.0 `VRMC_materials_mtoon`) is not read. Base colour factor, emissive factor, alpha mode/cutoff and double-sided are parsed (`Tr.cpp:1271-1345`) but **never applied** to the material instance. `KHR_texture_transform` and `COLOR_0` are ignored.
- A "character" MI is created (`Tr.cpp:244-265`) but the per-material MIs are only reparented to it later by the name-matching hook (PE-04).
- `SetCustomParent` is tried with two path formats on every material (`Tr.cpp:250-265, 315-330`). One verified format is enough.

### T-08 (P1) Morph targets: missing normals, unsafe grouping, no VRM expressions
- Only position deltas are read (`Tr.cpp:1445-1452`). Normal deltas are dropped, which causes shading artifacts on expressions.
- Unnamed targets fall back to `morph_%d` **per mesh** and are then merged by name across **different meshes** (`Tr.cpp:1376-1379`), which joins unrelated shapes.
- VRM expressions (VRM 0.x `blendShapeMaster.blendShapeGroups`, VRM 1.0 `VRMC_vrm.expressions`) are not imported, so there is no data-driven link from expression names (`Joy`, `A`, `Blink_L`, `happy`, `aa`) to morph targets. This is exactly what VMC `Blend/Val` sends (see X-01).

### T-09 (P1) Fallback joint names are inconsistent and duplicates are not handled
- Three different fallbacks for unnamed joints: `Joint_%d` (`Tr.cpp:1518`), `Bone_%d` for the scene node (line 204), and `"Bone"` for payload `JointNames` (lines 526, 639). Skinning for unnamed joints binds to names that don't exist in the skeleton.
- Duplicate node names are not made unique, and UE skeletons require unique bone names.

### T-10 (P2) The translator keeps mutable state
- A `const` translator mutates `mutable FVRMParsedModel Parsed` (`VRMTranslator.h:128`). Payload keys are parsed back into indices from strings (`Tr.cpp:425-445, 700-715`).
- `MakeNodeUid` uses only the file's base name, so two files with the same base name in one import get colliding UIDs.

### T-11 (P2) Performance
- Per vertex, a `TArray<FBoneWeight>` is heap-allocated for skin weights (`Tr.cpp:544, 656`).
- Each morph payload rebuilds a full `FMeshDescription`: every vertex, every triangle, normals, UVs **and skin weights** (`Tr.cpp:455-569`). That is O(morphs × vertices) work and memory, and VRoid models commonly have 50+ morphs.
- Images are decoded to RGBA and swizzled by hand (`Tr.cpp:752-763`). `GetRaw(ERGBFormat::BGRA, 8, ...)` does this directly. 16-bit PNGs are truncated to 8-bit.
- The source file is read or parsed up to **five times per import**: cgltf in the translator, MD5 of the whole file, the JSON chunk extraction, two JSON deserializations of the same string, and cgltf again in `ResolveBoneNamesFromFile` (spring pipeline).
- The base-mesh and morph-mesh builders are near-duplicates of each other.

### T-12 (P2) Input validation
- The result of `cgltf_validate` is ignored (`Tr.cpp:808`).
- `buffer_view` bounds and null `buffer->data` are not checked before `Memcpy` (`Tr.cpp:1245-1248`).
- Files are not checked for being VRM at all (no `VRM`/`VRMC_vrm` extension check), and there is no bone-count check (bone indices are `uint16`).

### T-13 (P2) Coordinate conversion is split across files and inconsistent
- Positions go through `GltfToUE_Vector` → `RefFix` mirror → +90° rotation (`Tr.cpp:95-134`). The composite works out to `(x, y, z) → (x, z, y)`. `GltfToUE_Quat` runs and its result is then discarded when rotations are zeroed (line 1575).
- The spring parser converts gravity with `(x, y, z) → (z, x, y)` (`VRMSpringBonesParser.cpp:79-82`), a *different* mapping. Any gravity direction that isn't straight down ends up wrong relative to the mesh.
- **Fix direction:** one header (`VRMCoordinateConversion.h`) with documented, unit-tested functions for positions, directions, rotations and scale, used by the translator, the parser and the runtime.

### T-14 (P3) Smaller issues
- `GetSupportedAssetTypes` advertises `Animations`, but none are produced (`Tr.cpp:158`).
- There are 21 `LogTemp` sites across the plugins. The runtime module only defines `LogVRMSpring`, so a `LogVRMInterchange` category is needed.
- `VRMInterchangeModule.cpp` includes `Engine.h`, a monolithic header.

---

## A.4 VRMInterchange: spring bone parser and data

Files: `VRMInterchange/Private/VRMSpringBonesParser.cpp` (abbreviated `Parser.cpp`), `VRMSpringBonesRuntime/Public/VRMSpringBonesTypes.h`, `VRMSpringBoneData.*`.

### SP-01 (P0) Collider geometry and gravity are not converted to UE axes
- Sphere and capsule offsets, capsule tails, and plane offsets and normals are read raw in glTF node-local space (`Parser.cpp:100-121`). The pipeline then scales them by 100 but never axis-converts them (`VRMSpringBonesPostImportPipeline.cpp:302-333`). The runtime applies them directly with `NodeXf.TransformPosition(S.Offset)`.
- Because imported bones have identity rest rotation (T-04), a glTF node-local offset must be rotated by that node's **original global rest rotation** and then axis-converted. For VRM 0.x nodes with non-identity rotations, both steps matter.
- Gravity direction uses the mismatched mapping described in T-13.
- **Impact:** colliders sit on the wrong axes (for example, a head collider offset "up" becomes "right"), so hair passes through the head or pushes off in the wrong direction.

### SP-02 (P1) VRM 0.x chains are never expanded to descendants
- VRM 0.x `secondaryAnimation.boneGroups[].bones` lists **chain root** nodes, and the spec simulates every descendant. The parser adds only the listed roots as joints (`Parser.cpp:664-674`).
- The code meant to supply the hierarchy (the "rich" `ParseSpringBonesFromFile` overload, `Parser.cpp:756-765`) is a stub that returns empty parent/children maps, so `BuildResolvedChildren` never runs.
- **Impact:** only root bones move, each with a synthetic 7 cm tail (`AnimNode_VRMSpringBones.cpp:227`).

### SP-03 (P1) VRM 1.0 per-joint parameters are collapsed per spring
- In VRM 1.0, `stiffness`, `dragForce`, `gravityPower`, `gravityDir` and `hitRadius` belong to each **joint**. The parser takes the first joint's values for the whole spring (`Parser.cpp:507-521`). The runtime also ignores `FVRMSpringJoint::HitRadius` and uses the spring-level value (`AnimNode_VRMSpringBones.cpp:292, 343-345`).
- **Impact:** tapered hair (stiff root, soft tip) loses its authored falloff.

### SP-04 (P1) Extended colliders are read from the wrong place, and the parser targets a non-spec schema
- `VRMC_springBone_extended_collider` lives on the **collider object**: `colliders[i].extensions.VRMC_springBone_extended_collider.shape`. The parser looks for it under the **shape** object (`Parser.cpp:136-149`), so spec-conformant extended colliders are never found.
- When both are present, the spec says the extended shape replaces the base shape (the base shape is only a fallback for older readers). `ParseOneShapeObject` would add both.
- The main VRM 1.0 path looks for a non-spec `"shapes"` array first (line 343) and only then the spec's singular `"shape"` object (line 359). The automation tests encode the non-spec form (`VRMIntegrationTests.cpp:34-44` uses `"shapes": [...]`).

### SP-05 (P2) Speculative schema variants
- `VRMC_node_collider`, `{ "type": "sphere" }` and `"shapes"` are handled, but no known exporter produces them. They add code paths that tests don't cover. Keep the spec forms, log unknown forms at `Verbose`, and remove the rest (or put them behind a clearly named "lenient" option).

### SP-06 (P2) API and data hygiene
- The header declares `ParseSpringBonesFromJson(..., TMap<int32, TArray<int32>>&, ...)` (`VRMSpringBonesParser.h:19`), which is **never defined**. Any caller gets a link error.
- `VRMINTERCHANGE_API struct FVRMValidationResult` puts the export macro in the wrong position (`VRMSpringBonesValidation.h:35`).
- `FVRMSpringConfig::RawJson` stores the **entire glTF JSON** (often several MB) as a `UPROPERTY` in a runtime data asset. It is cooked, shipped and loaded with the asset.
- `CenterNodeIndex`/`CenterBoneName`, `NodeParent`/`NodeChildren`, `ResolvedChildNodeIndexPerJoint`, `EditRevision` and `GetEffectiveHash()` are stored but not used at runtime.
- `BuildResolvedChildren` indexes `Cfg.Joints[JointIdx]` without bounds checks.
- The JSON is deserialized twice (`Parser.cpp:700` and `719`).

### SP-08 (P1) VRM 0.x vectors are read with the wrong JSON shape
- VRM 0.x `secondaryAnimation` stores vectors as objects: `gravityDir: {"x":0,"y":-1,"z":0}` and collider `offset: {"x":..,"y":..,"z":..}`. `ReadVec3` (`Parser.cpp:68-74`) only accepts arrays and otherwise returns its default.
- **Impact:** every VRM 0.x collider offset parses as zero (colliders sit on the bone origin), and every VRM 0.x gravity direction silently becomes the default. Found while writing the `vrm0_minimal` fixture (P0.2).
- **Fix direction:** accept both forms in `ReadVec3` (array, or object with x/y/z). Fixed together with P1.11/P1.13.

### SP-07 (P2) Changing the data will break saved assets without versioning
- Fixing SP-01/02/03 changes the meaning of data already saved in `UVRMSpringBoneData` assets (units, axes, per-joint parameters). There is no custom version or `PostLoad` upgrade path, so existing user assets would change behaviour silently.

---

## A.5 VRMSpringBonesRuntime: solver anim node

File: `VRMSpringBonesRuntime/Private/AnimNode_VRMSpringBones.cpp` (abbreviated `Node.cpp`).

### SR-01 (P1) Simulation is in component space and ignores `center`
- Tail state (`CurrentTail`, `PrevTail`) is stored in component space (`Node.cpp:320-356`). When the character walks, turns or jumps, the component moves with it, so the chains get no inertia. VRM specifies world-space simulation (or simulation relative to the spring's `center` node).
- `ExternalVelocity` (`AnimNode_VRMSpringBones.h:69-74`) is a manual workaround. `CenterBoneName` is resolved at import and then ignored.

### SR-02 (P1) Chain propagation lags and ignores parent rotation
- A child joint's head is set to the parent's **previous** tail (`ParentState.PrevTail`, `Node.cpp:320`). After the parent's update, `PrevTail` holds the parent's old position, so every link lags one frame behind its parent.
- The rest direction comes from the **input pose** rotation (`Node.cpp:323-324`). `CSPose` is never updated with the parent's simulated rotation, so each joint swings as an independent pendulum instead of inheriting its parent's motion as the VRM reference does.
- Every joint's translation is overwritten with the lagged head (`Node.cpp:370`), which can stretch or compress bones.

### SR-03 (P1) Integration depends on frame rate, and sub-stepping is missing
- Stiffness is applied as an unscaled lerp toward the animated target (`Node.cpp:328-332`, with a comment noting that `DeltaTime` scaling was removed). Inertia is not normalized for varying `dt`. Gravity is scaled by `dt`. The spring therefore behaves differently at 30, 60 and 120 FPS.
- There is no fixed-step sub-stepping and no `dt` clamp, so a long hitch (for example `dt = 0.5 s`) can make chains explode.
- `bPauseSimulation` sets `dt = 0`, but inertia and stiffness still integrate (`Node.cpp:393-394`), so "pause" still moves the chains.
- The VRM README claims "Deterministic Sub-stepping: consistent results regardless of frame rate". That is not implemented.

### SR-04 (P1) A second evaluation in the same frame returns no pose
- `bEvalCalledThisFrame` (`Node.cpp:383, 415`) makes a second `EvaluateSkeletalControl_AnyThread` call in the same frame return early with **empty** `OutBoneTransforms`, so the springs snap to the input pose. This happens with multiple evaluations (editor previews, some URO/LOD paths, graphs that evaluate twice). If `UpdateInternal` doesn't run in a frame, the flag stays `true` and the springs switch off.
- **Fix direction:** cache the last result and re-emit it, or remove the guard and make simulation idempotent per update.

### SR-05 (P0) Out-of-bounds access when data changes; no reset
- `JointStates` is only rebuilt when its count differs (`Node.cpp:151`). `BuildMappings` only runs in `CacheBones`. If `SpringData` changes at runtime (the pin is exposed and `BlueprintReadWrite`), `Spring.JointIndices` from the new asset index into arrays sized for the old one. `JointStates[Spring.JointIndices[ChainPos-1]]` (`Node.cpp:306`) is unchecked and can go out of bounds.
- `ResetDynamics(ETeleportType)` isn't implemented, so teleports and camera cuts drag chains across the level.
- Joints whose bones were invalid at initialization (for example, LOD-stripped) keep `WorldBoneLength = 0` when they become valid again.
- Edits to the data asset (`EditRevision`) aren't detected, so PIE and preview need a recompile to pick them up.

### SR-06 (P2) Per-frame cost
- `FCSPose<FCompactPose> CSPose = Context.Pose;` copies the whole component-space pose every evaluation (`Node.cpp:275`).
- `ResolveCollisions` builds and `Initialize()`s an `FBoneReference` (a name search) **for every collider, for every joint, every frame** (`Node.cpp:562-566`).
- A collider whose `BoneName` doesn't resolve silently falls back to `ComponentTM`, which places it at the character's origin (`Node.cpp:559-567`).
- `JointToSpring` is rebuilt as a `TMap` during initialization. Fine once, but it should move to `BuildMappings`.

### SR-07 (P2) Deviations from the VRM reference algorithm
- Joint hit radius is clamped to half the bone length (`Node.cpp:343-345`), which is not in the spec.
- Stiffness pulls position toward the animated tail. The spec applies stiffness as a force along the parent-rotated rest direction, scaled by `dt`.
- The length constraint is applied twice per step (lines 339 and 352), which is harmless but redundant.

### SR-08 (P3) Header and editor hygiene
- A garbled character in the comment "Per-joint" (header line 13), a forward declaration of the nonexistent `UVRMSpringBonesData` (line 10), and the doc comment "Spring configuration asset..." copied onto `ExternalVelocity` and `ExternalVelocityScale`.
- `UAnimGraphNode_VRMSpringBones` doesn't implement `ValidateAnimNodeDuringCompilation`, so there is no compile-time warning for a missing `SpringData` or a skeleton mismatch. It also doesn't customize details and has no preview draw hooks.

---

## A.6 VRMInterchangeEditor: pipelines and editor module

### PE-01 (P1) Import-dialog toggles can't turn features off
- The spring pipeline ORs its per-import toggles with the project settings (`VRMSpringBonesPostImportPipeline.cpp:81-84, 150-151, 564`). When a project setting is `true`, unticking the box in the import dialog has no effect.
- The IK Rig and Live Link pipelines use AND semantics, so the three pipelines are inconsistent. Settings are also copied into the pipeline in `PostInitProperties`, so they are applied twice.

### PE-02 (P1) Generated assets can bind to the wrong character
- All three pipelines use `FindImportedSkeletalAssets(root)`, which returns `Meshes[0]` from a **recursive** folder scan. When that fails they retry at the **parent folder** (for example `/Game`) (`VRMSpringBonesPostImportPipeline.cpp:335-343, 497-507`, and the same code in the IK Rig and Live Link pipelines).
- The trigger is a global `UImportSubsystem::OnAssetPostImport` subscription. It matches on a path prefix, which also matches sibling folders (for example `/Game/Alice` matches `/Game/Alice2`).
- **Impact:** in projects with several characters, spring data, IK rigs and actor Blueprints can be created for, or assigned to, the wrong skeletal mesh.
- **Fix direction:** use Interchange's own post-import hook (`UInterchangePipelineBase::ExecutePostImportPipeline`, which receives the created asset for each factory node UID), or resolve the mesh from the factory node in the container. Don't use folder scanning.

### PE-03 (P1) The editor module edits project config on every start
- `FVRMInterchangeEditorModule::StartupModule` (`VRMInterchangeEditorModule.cpp:251-265`) edits `UInterchangeProjectSettings`: it adds per-translator pipelines, **removes** the generic assets pipeline for `.vrm`, forces dialog overrides, and calls `SaveConfig()`.
- **Impact:** config files change without the user asking, which causes source-control churn and unexpected diffs. A user who deliberately removes a pipeline gets it re-added on the next launch.

### PE-04 (P1) The global material-reparent hook modifies user content outside imports
- `VRMPostImportReparent.cpp` creates a static global object at DLL load. It registers `FCoreDelegates` handlers during static initialization and subscribes to `IAssetRegistry::OnAssetAdded` for **all** assets. That event also fires during the editor's startup asset scan.
- For any `UMaterialInstanceConstant` named `MI_VRM_*`, it **synchronously loads** the asset and reparents and dirties material instances by name prefix, which can include user-created assets that were never imported.
- The per-folder comparison is O(n²), and the `MIsByFolder` cache grows without bound.
- **Fix direction:** set the correct parent at translation time (the per-material MI node depends on the character MI node), or do it in a pipeline for the assets of the current import only. Delete the global hook.

### PE-05 (P1) The IK Rig only works for VRoid-style bone names
- `DuplicateTemplateIKRig` copies `/VRMInterchange/Animation/IK_Rig_VRMTemplate` (`VRMIKRigPostImportPipeline.cpp:186-219`). The template's retarget chains name specific bones, so any VRM whose bones are named differently gets a broken IK Rig.
- VRM files carry an explicit humanoid map (VRM 0.x `humanoid.humanBones`, VRM 1.0 `VRMC_vrm.humanoid.humanBones`) that the importer never parses.
- **Fix direction:** build the IK Rig with `UIKRigController` (retarget root, standard chains, optional goals) from the humanoid map.

### PE-06 (P2) "Overwrite existing" does not replace the existing asset
- The pipelines call `CreatePackage` and `StaticDuplicateObject(..., UniqueName)` into a package that may already contain an object with that name (`VRMSpringBonesPostImportPipeline.cpp:363-376`, `VRMIKRigPostImportPipeline.cpp:200-214`, `VRMLiveLinkPostImportPipeline.cpp:96`). The existing asset is never deleted, renamed or replaced, so this fails or leaves the package in a bad state.
- **Fix direction:** use `IAssetTools::DuplicateAsset` into a unique name, or use `ObjectTools` to replace properly.

### PE-07 (P2) Actor Blueprint wiring edits the CDO
- `AssignSkeletalMeshToActorBP`/`AssignAnimBPToActorBP` (`VRMLiveLinkPostImportPipeline.cpp:98-143`) look for components on the generated class CDO. Components added in the Blueprint editor (SCS) aren't on the CDO, so this only works if the template uses a native component, and otherwise fails silently.
- The property name `"VRM Character"` is hard-coded, and a missing property isn't reported.
- `bGenerateLiveLinkRetargetActor` ignores the project setting. `DeferredRetargetActorBPName` is set to `ABP_LL_VRM_To_UE5_%s` in one place (line 65) and `BP_LL_VRM_To_UE5_%s` in another (line 232).

### PE-08 (P2) The three pipelines duplicate each other
- `FindImportedSkeletalAssets`, `GetParentPackagePath`, `MakeCharacterBasePath`, `DuplicateTemplate`, and the register/unregister deferral code are copy-pasted across the three pipelines, several on single dense lines.

### PE-09 (P2) Build and include hazards
- `VRMInterchangeEditor/Public/VRMSpringBonesPostImportPipeline.h` is an **empty (0-byte)** file with the same name as the real `Private/` header. Which one an include picks depends on include-path order.
- `CGLTF_IMPLEMENTATION` is compiled in two modules (`VRMTranslator.cpp` and `VRMSpringBonesPostImportPipeline.cpp`). That duplicates code and risks duplicate symbols in monolithic editor builds.
- `VRMInterchange.build.cs` adds `InterchangeEditor`, `Slate`, `AssetTools` and other editor modules to the **runtime** translator module, with a comment pointing at a pipeline that lives in the editor module. Dependencies are listed twice.
- `VRMInterchangeEditor.build.cs` reaches into another module's `Public` folder through `PrivateIncludePaths` instead of depending on the module.

### PE-10 (P3) Dead code
- `NotifySpringDataCreated`/`NotifySpringDataSaved` are empty. There is an empty anonymous namespace (`VRMInterchangeEditorModule.cpp:14`), and a comment refers to a `HandlePreExit` that doesn't exist (line 270). The `VRMSpringBonesEditor` and `VRMSpringBonesRuntime` module classes are empty (that's fine, but `IMPLEMENT_MODULE(FDefaultModuleImpl, ...)` would do).

---

## A.7 Cross-cutting: build, CI, tests, docs

### X-01 (P1, architectural) There is no shared VRM avatar description
- The importer throws away the data that would link the plugins: the humanoid bone map, expressions (with morph binds, `isBinary` and override flags), the VRM version, look-at and first-person settings, and meta and licence information.
- As a result, VMCLiveLink guesses bone and curve mappings from name heuristics (`SeedBones_FromHumanoidLike`, presets). The IK Rig relies on template bone names. Spring data resolves bones by node name.
- VMC bone names are Unity `HumanBodyBones` (`Hips`, `LeftUpperLeg`, ...), which correspond one-to-one, ignoring case, with VRM humanoid names (`hips`, `leftUpperLeg`, ...). VMC `Blend/Val` names are VRM expression names. **An imported avatar description gives exact mappings for both, with no heuristics.**

### X-02 (P2) CI doesn't build pull requests or run tests
- `fab-plugin-build.yml` only runs on `release/*` tags and manual dispatch, on a self-hosted Windows runner. Pull requests are never compiled, and automation tests are never run.
- The copyright check only covers `PLUGIN_DIR` (VMCLiveLink). **16 VRMInterchange source files have no header** (the `VRMSpringBonesRuntime` files, several `VRMInterchangeEditor` files, and the tests).

### X-03 (P2) Tests are weak on the risky paths
- The existing tests exercise the JSON parser against non-spec fixtures (SP-04), check pipeline default values, and **reimplement** pipeline logic locally (`VRMSpringBonesNameResolveTests.cpp:24ff`) instead of calling it.
- Nothing tests skinning or joint mapping, the coordinate conversion, the spring solver's numerical behaviour, or anything in VMCLiveLink.

### X-04 (P2) Documentation is inaccurate or missing
- `Plugins/VRMInterchange/README.md` claims deterministic sub-stepping (line 201), "proper bone orientations" and "Hierarchical Propagation", which the code doesn't back up. Its coordinate-system section describes the three-step conversion instead of the net mapping.
- **VMCLiveLink has no plugin README**, no documented VMC message support matrix, no sender setup guides and no troubleshooting.
- `FilterPlugin.ini` is empty, so README and licence files are not packaged. Both `.uplugin` files have empty `DocsURL`/`SupportURL`, which Fab requires.
- The repo root has about 2,300 lines of planning and readiness documents plus 13 post-mortems, which makes it hard to tell what is current.
- There is no CHANGELOG, and both plugins are still `0.1.0`.

*Findings X-05 to X-08 were added on 2026-09-25, after the first CI runs (see B.10).*

### X-05 (P1) Existing tests had never run, and some depended on project config
- When the PR build first ran the automation tests, 6 of 12 failed on `main` (fixed in #110).
- Three asserted fixed default values for the spring pipeline toggles. The pipeline copies `UVRMInterchangeSettings` in `PostInitProperties`, and this project's `Config/DefaultGame.ini` turns those settings on, so the tests were checking the sample project's config.
- Three used a non-spec VRM 1.0 spring layout (a top-level `joints` array with `springs[].joints` as indices into it). After 00de610 the parser appended every joint twice for that layout without any test noticing.
- **Impact:** a test suite that isn't run gives false confidence. Tests that read project config break when the config changes.

### X-06 (P2) Deprecated engine API in the translator
- `VRMTranslator.h:117` overrides the deprecated `IInterchangeMeshPayloadInterface::GetMeshPayloadData(const FInterchangeMeshPayLoadKey&, const FTransform&)`. UE 5.6 warns (C4996) that this will stop compiling in the next release. This matters for #98's 5.7 and 5.8 support.

### X-07 (P2) CI infrastructure gaps
- **One runner:** there is one self-hosted Windows runner (EIGHTYGEE-DT). Each PR build takes 3 to 5 minutes and they queue serially, so seven PRs take about 35 minutes.
- **Spurious cancellation:** one run (36149565264, attempt 1) was cancelled by the runner itself 5 s into the Shipping build. There was no user, timeout or concurrency cancel, and the re-run passed. The likely cause is a console Ctrl+C on the runner machine.
- **Stacked PRs get no CI:** `pr-build.yml` only triggers for PRs that target `main`, so stacked PRs are never built until they are retargeted.
- **Deprecated Node:** the workflows use `actions/checkout@v4` and `actions/upload-artifact@v4`, which run on the deprecated Node 20.
- **Toolchain:** the runner's MSVC 14.51 is not UE 5.6's preferred toolchain (14.38), and UBT warns about it on every build.
- **Application Control block (recurring, owner action needed):** Windows on the runner intermittently blocks files the build has just created, with error 4551 ("An Application Control policy has blocked this file", Smart App Control or WDAC). It hit #115 (a plugin DLL), #121 twice (the project DLL, then UnrealBuildTool's compiled `*ModuleRules.dll`, so nothing built) and #123 (`VRMSpringBonesEditor.dll`). Each time a re-run passed or the next push was clean. Nothing in the repository can fix it. The owner should add an exclusion for the runner's work folder (`C:\actions-runner\_work`) or turn off Smart App Control on EIGHTYGEE-DT. Until then, a 4551 failure gets one PR comment and one re-run.
- **Superseded runs:** retargeting a PR, or pushing right after it, starts a new run and cancels the older one (job-level concurrency). The PR then shows cancelled runs next to the real one. Judge a PR by its newest `build-and-test` run on the head commit.
- **Tag named `main`:** the remote had a tag called `main` as well as the branch, so `git fetch origin main` fetched the tag, not the branch. (Deleted by the owner on 2026-09-25.)
- **VMC tests never ran:** the filter `VRM.;VMC.` was passed to `Automation RunTests`, where `;` ends the command. Only `VRM.` tests ran, and nothing reported it. Fixed by using `VRM.+VMC.` and failing when any prefix matches no tests.
- **Retargeting and squash merges:** retargeting a PR did not start a build (fixed in P0.5). Because PRs are squash-merged, a stacked PR must first merge its old base branch's final commit, then `main`. Merging `main` directly shows every file of the base PR as a conflict.
- **Silent bad auto-merge:** merging `main` (with #119 squashed in) into #120's branch, which already had #119's commits, auto-merged `VRMSpringBoneData.cpp` without a conflict but with `CopySpringParametersToJoints` defined twice. Only a diff against `main` caught it before the push (B.0 rule 13).

### X-08 (P2) Parallel PRs used symbols from each other
- #103 was branched from `main` but used `LogVRMInterchange`, which only #100 declares. It failed to compile in the PR build. The fix ported #100's two-line declaration into #103.
- With CI in place this is caught automatically, but only when each PR is built against its own base.

---

# Part B: Implementation plan

## B.0 Rules for the implementing agent

1. **One task per branch/PR.** Keep diffs minimal and scoped to the task's findings. Reference the task ID (for example `P1.7`) and the finding IDs in the PR description.
2. **Verify the API before using it.** For every UE 5.6 API you touch (Interchange nodes, `ILiveLinkClient`, `UOSCServer`, `UIKRigController`, AnimGraph node APIs), confirm the signature in the engine source or the official API reference. Don't guess. If you can't confirm, stop and say so in the PR.
3. **Resolve [Verify] items first.** For tasks that depend on a [Verify] finding, the first commit must add evidence: a spec excerpt, a failing test or fixture, or a captured OSC dump.
4. **Build matrix.** Before opening a PR, compile `VMCLiveLinkProjectEditor Win64 Development` and a Game target (`Development` and `Shipping`). Run `Automation RunTests VRM.+VMC.` headless (`+` separates filters; `;` ends the command). If you can't compile in your environment, say so explicitly in the PR and list what was verified statically.
5. **Asset compatibility.** Any change to a `USTRUCT`/`UCLASS` layout or the meaning of saved data must bump a custom version (`FVRMInterchangeCustomVersion`, `FVMCLiveLinkCustomVersion`) and include a `PostLoad`/`Serialize` upgrade path, plus a test that loads a pre-change asset or struct.
6. **Binary assets.** Avoid `.uasset` edits where code can do the job (for example, generate IK Rigs with the controller instead of editing the template). If a `.uasset` must change, describe the change precisely in the PR, because reviewers can't diff it.
7. **Logging.** Use the plugin log categories (`LogVMCLiveLink`, `LogVRMInterchange`, `LogVRMSpring`). No new `LogTemp`. Import problems go to an `FMessageLog("VRMInterchange")` page as well as the log.
8. **Headers.** New files start with the copyright header the CI enforces.
9. **Docs travel with code.** If behaviour changes, update the relevant README section and `CHANGELOG.md` in the same PR.
10. **Build against your own base.** Don't use a type, function or log category that only exists in another unmerged PR. Either stack on that PR (and say so) or port the minimal change unchanged, so the identical edits merge cleanly (X-08).
11. **Tests don't depend on project config.** Tests that read `UDeveloperSettings` or other config must compare against the loaded settings, or set the values they need and restore them. Never assert on values that `Config/*.ini` can change (X-05).
12. **CI must go green.** A PR is ready only when `PR Build and Tests` passes on its latest commit. Scripts called from CI must `exit 0` explicitly: PowerShell leaves `$LASTEXITCODE` unset after a script that doesn't exit, and `$null -ne 0` is true.
13. **Check a merge before pushing it.** After merging `main` into a branch (especially one stacked on a squash-merged PR), run `git diff origin/main HEAD --stat` and confirm it shows only this PR's change. Git can auto-merge two identical additions with different surrounding lines into a duplicate, with no conflict reported (X-07).

Task format below: **Resolves** (finding IDs) · **Depends on** · **Files** · **Steps** · **Acceptance**.

---

## Phase 0: Guardrails

Goal: make regressions visible before large changes land.

### P0.1 CI compiles pull requests and runs tests
- **Resolves:** X-02
- **Files:** `.github/workflows/` (new `pr-build.yml`), `scripts/`
- **Steps:**
  1. Add a workflow on `pull_request` (self-hosted Windows runner, same engine root as `fab-plugin-build.yml`) that runs `RunUAT BuildPlugin` for both plugins, or builds the project Editor target.
  2. Add a step that runs `UnrealEditor-Cmd.exe VMCLiveLinkProject.uproject -ExecCmds="Automation RunTests VRM.+VMC.;Quit" -unattended -nullrhi -nosplash -log` and fails on test failures (parse the automation report JSON with `-ReportExportPath`).
  3. Extend the copyright verification to `Plugins/VRMInterchange`, then add the missing headers to the 16 files (use `scripts/add_copyright_headers.py`).
- **Acceptance:** a PR that breaks compilation or a test goes red. The header check covers both plugins and passes.

### P0.2 Synthetic VRM test fixtures
- **Resolves:** enables tests for T-01 to T-05, T-09, SP-01 to SP-04
- **Files:** `Plugins/VRMInterchange/Source/VRMInterchangeEditor/Private/Tests/Fixtures/` (or `Plugins/VRMInterchange/Tests/Fixtures/`), `scripts/make_vrm_fixtures.py`
- **Steps:** write a deterministic Python generator (standard library only, plus `struct`) that emits small `.vrm` (GLB) files:
  - `vrm0_minimal.vrm`: VRM 0.x extension, one skin, humanoid map, one `boneGroup` with a 3-bone chain, sphere colliders with non-zero offsets.
  - `vrm1_minimal.vrm`: `VRMC_vrm` plus `VRMC_springBone` using the **spec** `shape` form, per-joint parameters that differ along the chain, one extended collider (`colliders[i].extensions.VRMC_springBone_extended_collider`).
  - `multi_skin.vrm`: two meshes with two skins whose `joints` lists are ordered differently and **not** equal to node indices.
  - `rigid_accessory.vrm`: a non-skinned mesh node parented to a joint, with a non-identity node transform.
  - `unnamed_and_duplicate_nodes.vrm`: joints with missing and duplicate names.
  - `armature_transform.vrm`: a non-joint `Armature` ancestor with rotation and scale.
  - Each fixture includes known expected values (bone world positions, per-vertex expected bone index) in a sidecar `.json`.
- **Acceptance:** fixtures are committed (small, license-free), and the generator reproduces them byte-for-byte.

### P0.3 VMC test harness
- **Resolves:** enables tests for VMC-01 to VMC-12, VMC-26
- **Files:** `scripts/vmc_sender.py` (python-osc), `Plugins/VMCLiveLink/Source/VMCLiveLink/Private/Tests/`
- **Steps:**
  1. A sender script that can (a) replay a recorded OSC capture and (b) generate spec-conformant bundles (`Root/Pos` with 8/11/14 arguments, a full humanoid `Bone/Pos` set, `Blend/Val` plus `Blend/Apply`, `/VMC/Ext/T`, `/VMC/Ext/OK`), with switches for "partial blendshapes" and "7-float legacy root".
  2. Record at least one real capture each from VSeeFace and the VirtualMotionCapture app, if licences allow. Store the `.bin` dumps under `Plugins/VMCLiveLink/Tests/Captures/`.
- **Acceptance:** the script runs locally against the plugin, and captures exist for replay-based tests (added in P3.1).

### P0.4 Logging categories
- **Resolves:** T-14 (logging part)
- **Steps:** add `LogVRMInterchange` (runtime translator) and `LogVRMInterchangeEditor`, and replace all 21 `LogTemp` uses.
- **Acceptance:** `grep -rn LogTemp Plugins/` returns nothing.

### P0.5 CI hardening (added 2026-09-25)
- **Resolves:** X-07, X-06
- **Steps:**
  1. Build stacked PRs too: change `pr-build.yml`'s `pull_request.branches` to `['**']`, or drop the filter. Keep the rule that fork PRs never run on the self-hosted runner.
  2. Move to `actions/checkout@v5` and `actions/upload-artifact@v5` (Node 24).
  3. Owner: delete the `main` tag (`git push origin :refs/tags/main`) unless it is intentional. **Done 2026-09-25.**
  4. Owner: install MSVC 14.38 on the runner, or accept the warning. Consider a second runner if PR volume stays high. **Decision 2026-09-25: accept the toolchain warning for now (no hands-on access to the runner).** Revisit if the toolchain causes a real build difference.
  5. Move `UVRMTranslator` to the non-deprecated `GetMeshPayloadData(const FInterchangeMeshPayLoadKey&)` overload (X-06). The mesh global transform then comes from the payload key or the pipeline. Verify this in 5.6, 5.7 and 5.8, as #98 targets them.
  6. Optional: fail the build on new C4996 warnings in plugin code (not engine headers), so deprecations are caught as they appear.
- **Acceptance:** a PR stacked on another PR gets a `PR Build and Tests` run. No Node 20 warning. No C4996 from plugin sources.
- **Status:** steps 1, 2 and 5 merged in #112. The test-filter fix (X-07) followed separately. Step 3 done by the owner; step 4 resolved by accepting the warning. Step 6 is open.

---

## Phase 1: Critical correctness fixes

Goal: targeted fixes for P0/P1 bugs with minimal architectural change. Each task adds regression tests using the Phase 0 fixtures.

### P1.1 Parse `Root/Pos` per the VMC spec
- **Resolves:** VMC-01, part of VMC-20 · **Depends on:** P0.3
- **Files:** `VMCLiveLinkSource.cpp`
- **Steps:** after confirming the spec layout (attach the spec reference in the PR), accept `(string name, 7 floats)`, optionally followed by `(3 scale, 3 offset)`. Keep 7-float as a legacy fallback that logs once. Check argument types (`IsString()`/`IsFloat()`) for `Bone/Pos`, `Root/Pos` and `Blend/Val`, and drop malformed messages with a rate-limited `Verbose` log. Store scale and offset in the pending root state (apply scale to root translation if the owner confirms, see D-5).
- **Acceptance:** tests feed 7/8/11/14-argument messages and wrong-type arguments and assert the correct root transform or rejection. Replaying the VSeeFace capture yields non-identity root motion.

### P1.2 Source lifetime safety
- **Resolves:** VMC-02
- **Steps:** delete the `AsyncTask` in `ReceiveClient` (or capture `AsWeak()` and check it). Add `~FVMCLiveLinkSource() { StopOSC(); }`. Make sure `RequestSourceShutdown` is idempotent.
- **Acceptance:** an automation test creates a source, removes it in the same tick, and pumps the task graph with no crash (run under ASan/stomp allocator if available).

### P1.3 Humanoid hierarchy and stable bone indices
- **Resolves:** VMC-03, VMC-04
- **Files:** `VMCLiveLinkSource.cpp/.h`, new `Private/VMCHumanoid.h/.cpp`
- **Steps:**
  1. Add a static table of the 55 Unity `HumanBodyBones` names and their humanoid parents (Hips → root, Spine → Hips, ..., fingers), matched case-insensitively.
  2. Publish a **fixed skeleton**: `root` at index 0, then humanoid bones in table order with parent indices from the table. Unknown incoming names are appended and parented to `Hips`, or to `root` when `Hips` is absent, with a one-time warning.
  3. Never insert at the front. Only append. Keep `BoneName → index` in a `TMap` built once.
- **Acceptance:** a unit test checks the parent array against the table, with exactly one root, for a full humanoid stream both with and without `Root/Pos`. Republishing doesn't change existing indices.

### P1.4 Keep user subject settings
- **Resolves:** VMC-05
- **Steps:** in `EnsureSubjectSettingsWithDefaults`, query `Client->GetSubjectSettings(Key)`. If settings exist and have a remapper, do nothing. If settings exist without a remapper, attach the default remapper to the **existing** settings object. Only call `CreateSubject` when the subject doesn't exist.
- **Acceptance:** a test pre-creates the subject with a custom remapper (as a preset load would), connects the source, and checks the remapper instance is unchanged.

### P1.5 Apply-cycle ordering and curve persistence
- **Resolves:** VMC-11, VMC-12
- **Steps:**
  1. In `Blend/Apply`, compute `bNeedStatic = !bStaticSent || bForceStaticNext || bStaticCurvesDirty` **before** building the frame. Push static data once when needed, then the frame.
  2. Keep last curve values between applies (`PendingCurves` is no longer reset). Add a per-source option `bZeroMissingCurves` (default false).
- **Acceptance:** a test sends curve A on frame 1, then A and B on frame 2, and asserts one static push per change, property counts that always match static data, and A held at its last value on frame 3 when A isn't sent.

### P1.6 Non-destructive normalizer and clean presets
- **Resolves:** VMC-07, VMC-08
- **Steps:**
  1. Default `bEnableMetaHumanCurveNormalizer = false`. Smile spreading only fills the **missing** side. Funnel → pucker only runs when `mouthPucker` isn't present in the static data. Cache property indices in the worker when static data changes.
  2. `ApplyPreset` clears preset-owned entries before seeding (track which keys a preset added, or rebuild the map as "user overrides" plus "preset").
  3. Fix the VMC/VRM preset so each target has at most one source. Add a `VRM1` preset with the VRM 1.0 expression names.
  4. Add an editor-time validation warning when two sources map to the same target.
- **Acceptance:** tests show that asymmetric smile values pass through unchanged, that switching presets A → B leaves no A-only keys, and that no preset produces duplicate targets.

### P1.7 Correct joint mapping, multiple skins and names in the translator
- **Resolves:** T-01, T-02, T-09 · **Depends on:** P0.2
- **Files:** `VRMTranslator.cpp/.h`
- **Steps:**
  1. Build the bone list from the **union of all skins' joints**, preferably every node in the scene hierarchy under the lowest common ancestor of the joints, so spring and collider nodes that aren't skin joints also exist. Store `NodeIndex → BoneIndex`.
  2. For each **mesh node** with a skin, map `JOINTS_n[k]` → `skin.joints[JOINTS_n[k]]` (a node pointer) → `NodeIndex` → `BoneIndex`. Reject out-of-range values with a warning, and don't silently use bone 0.
  3. Support `JOINTS_1`/`WEIGHTS_1` (8 influences), or at least warn when they're present.
  4. Name unnamed nodes `Node_<index>` once. Make duplicate names unique (`Name`, `Name_1`, ...). Use the **same** final names for scene joint nodes, payload `JointNames` and `NodeToBoneMap`.
- **Acceptance:** with `multi_skin.vrm` and `unnamed_and_duplicate_nodes.vrm`, each vertex's dominant bone matches the sidecar JSON, skeleton names are unique, and payload joint names equal the skeleton names.

### P1.8 Node transforms, rigid meshes and bind pose
- **Resolves:** T-03, T-04 · **Depends on:** P1.7
- **Steps:**
  1. Compute node global transforms from the full node hierarchy, including non-joint ancestors and scale.
  2. Iterate mesh **nodes**. Skinned primitives use the skin (glTF says the skinned mesh node's own transform is ignored). Rigid primitives are transformed by the node's global transform and weighted 100% to the nearest ancestor bone.
  3. Validate `inverseBindMatrices` against the computed rest pose. If they differ beyond a tolerance, either bake the bind pose from the IBMs (preferred) or warn clearly.
  4. Keep the identity-rotation "reference pose" behaviour behind a documented function until decision D-3 is made.
- **Acceptance:** `rigid_accessory.vrm` places the accessory at the expected world position, bound to its parent bone. `armature_transform.vrm` bone world positions match the sidecar.

### P1.9 VRM version detection and forward axis
- **Resolves:** T-05, T-12 (VRM check) · **Depends on:** P0.2, T-13 work in P1.11
- **Steps:**
  1. **[Verify]** Import one VRM 0.x file and one VRM 1.0 file from the same author (for example the VRoid sample exported both ways) and record their facing. Attach screenshots to the PR.
  2. Detect the version (`extensions.VRM` → 0.x, `extensions.VRMC_vrm` → 1.0, neither → warn "not a VRM file, importing as generic glTF").
  3. Apply a 180° yaw for the version that needs it, consistently to vertices, normals, morph deltas, bones and spring data (through the shared conversion header).
- **Acceptance:** both fixture versions import facing the same UE direction (+Y, matching the mannequin). Spring colliders still line up (visual check with `vrm.SpringBones.DrawColliders 1`).
- **Status (#116, merged):** steps 2 and 3 are done, covered by `VRM.Coordinates.Facing`. Step 1 could not be done in the implementing environment, which has no editor. Its place was taken by the reference implementation: three-vrm's `VRMUtils.rotateVRM0` turns VRM 0.x scenes 180° about Y, which confirms that VRM 0.x faces −Z. The editor import with screenshots and the collider visual check are still owed (B.10). Files with neither extension import with the VRM 1.0 facing.

### P1.10 Texture colour space and normal maps
- **Resolves:** T-06
- **Steps:** during `Translate`, record each image's usage from the materials (base colour, emissive → sRGB; normal → linear + `TC_Normalmap` + flip green; metallic-roughness and occlusion → linear, `TC_Masks`). Set these on the `UInterchangeTexture2DFactoryNode` custom attributes (confirm the 5.6 attribute names). Remove the hard-coded `bSRGB=true` from the payload. If an image has conflicting uses, duplicate the texture node per usage.
- **Acceptance:** after importing a fixture with a normal map, the texture asset has sRGB off, normal-map compression and the green flip. A lit sphere test scene shows correct bumps (screenshot).
- **Status (#125, merged):** done. `ComputeTextureUsages` records each image's uses from the materials (Color, Normal, Data flags). Each use gets its own texture node and payload key (`Tex_<i>`, `Tex_<i>_Normal`, `Tex_<i>_Data`), so an image used two ways is imported twice with the right settings. Colour is sRGB; normal maps are linear with `TC_Normalmap`, and the green channel is flipped in the pixel data rather than with the texture's flag; data textures are linear with `TC_Masks`. Covered by `VRM.Textures.Usage` and `VRM.Textures.Decode`. The first build failed to link (`FImportImage` needs `InterchangeImport` in the editor module's dependencies). The lit-sphere check is still an editor check.

### P1.11 One coordinate-conversion module; convert spring geometry
- **Resolves:** T-13, SP-01 · **Depends on:** P1.8
- **Files:** new `VRMInterchange/Public/VRMCoordinateConversion.h/.cpp`, `VRMTranslator.cpp`, `VRMSpringBonesParser.cpp`, spring pipeline
- **Steps:**
  1. Implement `ToUEPosition`, `ToUEDirection`, `ToUERotation`, `ToUEScale` and a `FVRMAxisConvention { EVRMVersion; float Scale; }`, all documented with the net mapping. Unit-test that a triangle's handedness and winding survive the conversion and that the conversions are self-consistent (`R * v` equals the converted rotation applied to the converted vector).
  2. Replace `GltfToUE_*`, `RefFix_*` and the parser's `GltfToUE_Dir`.
  3. Collider conversion: for a collider on node N, `offset_UE = ToUEDirection(R_N_global_original * offset_gltf) * Scale`, where `R_N_global_original` is the glTF node's original global rest rotation (identity bones in UE mean offsets are expressed in axis-aligned bone space). Apply the same to capsule tails, plane offsets, plane normals (no scale) and gravity direction (no scale).
  4. Move all unit and axis conversion into the parse step (not the pipeline), so `bConvertToUEUnits` is removed or becomes only a scale override.
- **Acceptance:** for `vrm0_minimal.vrm`, a head collider authored with offset `(0, 0.1, 0)` (glTF up) ends up 10 cm **above** the head bone in UE. Gravity `(1, 0, 0)` in glTF maps to the same UE axis as the mesh's +X. Tests cover both.
- **Status (#115, merged):** all four steps are done. The helpers live in a header only (`VRMCoordinateConversion.h`); no `.cpp` was needed. `bConvertToUEUnits` was removed outright. Two findings came up along the way:
  - VRM 0.x collider offsets have Z negated relative to VRM 1.0 (three-vrm: "z is opposite in VRM0.0"). Gravity is not negated.
  - VRM 1.0 springs without `gravityDir` used a UE-space default, which the new conversion would have turned sideways. The default is now glTF down, `(0, -1, 0)`, before conversion.
  SP-08 (`ReadVec3` object form, P1.13 step 6) was fixed here too.
- **Deviation:** this merged without P1.16. Spring data assets imported before #115 keep their old values (wrong axes, and VRM 0.x offsets and gravity read as defaults) and nothing flags them. Until P1.16 lands, reimport VRM files after updating.

### P1.12 Expand VRM 0.x chains
- **Resolves:** SP-02 · **Depends on:** P1.7 (bone list includes all descendants)
- **Steps:** parse the node hierarchy (parent and children) in the parser, or take it from the translator, once P3.3 lands. For each VRM 0.x `boneGroup` root, add joints depth-first for every descendant. Branches become separate chains (one `FVRMSpring` per root-to-leaf path, sharing parameters), matching UniVRM. Delete the stub overload and `BuildResolvedChildren`, or make them actually work.
- **Acceptance:** `vrm0_minimal.vrm` produces a 3-joint chain from a single listed root. Every joint resolves to a bone.
- **Status (#120, merged):** done, with one change to the branch rule. "One spring per root-to-leaf path" would put the joints above a branch point in several springs, and the solver would simulate them more than once. Instead a chain follows the first child to a leaf, and every other child starts a chain of its own with the group's settings. No joint is in two chains; the branch point's tail is its first child, as in three-vrm. Mesh nodes end a chain, and a listed root inside an earlier root's subtree is not added again. The hierarchy overload now returns the node maps, so `BuildResolvedChildren` works (the runtime does not use its output yet). Covered by `VRM.SpringBones.Parse.VRM0Chains` and `VRM.Fixtures.SpringChains`.

### P1.13 Spec-correct VRM 1.0 parsing (per-joint parameters, extended colliders)
- **Resolves:** SP-03, SP-04, SP-05, SP-06 (link error), SP-08 · **Depends on:** P0.2
- **Steps:**
  1. Add per-joint `Stiffness`, `Drag`, `GravityPower`, `GravityDir` and `HitRadius` to `FVRMSpringJoint`. VRM 0.x fills them from the group, VRM 1.0 from each joint. Keep the spring-level fields only as editor "apply to all" helpers, or remove them (requires versioning, see P1.16).
  2. Read the collider `shape` object (spec). Read `colliders[i].extensions.VRMC_springBone_extended_collider.shape` and, when present, **replace** the base shape.
  3. Move non-spec variants behind `bLenientSchema` (default on for one release, with a `Verbose` log naming the variant), or delete them (owner decision D-6).
  4. Rewrite the tests to use spec JSON. Keep a separate test for each lenient variant if they're kept.
  5. Remove the undefined `ParseSpringBonesFromJson(... TArray<int32> ...)` declaration. (The `FVRMValidationResult` export macro is already fixed in PR #98.)
  6. Make `ReadVec3` accept the VRM 0.x `{x,y,z}` object form as well as arrays (SP-08). **Done in #115 (P1.11).**
- **Note (2026-09-25):** #110 moved the existing tests to the spec layout. When the top-level `joints` handling is removed here, add a test showing that a numeric `springs[].joints` entry is rejected or warned about, rather than silently appended (X-05).
- **Acceptance:** spec fixtures parse with correct per-joint values and extended colliders. The old non-spec tests either move to "lenient" tests or are deleted.
- **Status (#119, merged):** all steps done. The solver reads each joint's parameters. The spring-level fields show the first joint's values and, when edited, apply to every joint of the spring. Non-spec layouts sit behind the console variable `vrm.SpringBones.LenientSchema` (default on, decision D-6); each use is logged at `Log`, not `Verbose`, so files that depend on them show up. The non-spec top-level `joints` array is no longer read, and a numeric `springs[].joints` entry is warned about and skipped. Covered by the `VRM.SpringBones.Parse.VRM1*` tests and `VRM.Fixtures.SpringJointParameters`.

### P1.14 Anim node robustness
- **Resolves:** SR-04, SR-05 · **Can land before Phase 2**
- **Steps:**
  1. Track the `SpringData` pointer and `GetEffectiveHash()`. When either changes, rebuild mappings and states on the next evaluate (inside the anim thread, safely).
  2. Bounds-check every `JointStates[...]`/`JointBoneRefs[...]` access, including the parent index.
  3. Replace `bEvalCalledThisFrame` with a cached `LastOutBoneTransforms` that is re-emitted when evaluate runs again without a new update.
  4. Implement `ResetDynamics(ETeleportType)`, which reinitializes tails from the current pose, and handle `InitialState` properly.
  5. When a bone becomes valid after an LOD change, initialize its state before simulating it.
- **Acceptance:** tests swap `SpringData` at runtime with no crash (fuzz with random joint indices), evaluate twice per frame with the same pose output both times, and show a teleport does not stretch chains.
- **Status (#122, merged):** all steps done. The per-frame work is split out (`BeginFrame`, `RebuildForBones`, `EvaluateInternal`) so tests run the node on a skeleton built in code, without an AnimBlueprint (`VRM.SpringBones.Node.*`). The LOD test found a crash that was on `main`: `FBoneReference::HasValidSetup()` only checks the skeleton, so a bone a lower LOD strips passed with compact index -1 and was read out of bounds. The node now uses `IsValidToEvaluate(BoneContainer)`. Tests that build poses must hold an `FMemMark`.

### P1.15 Pipeline toggles and target resolution
- **Resolves:** PE-01, PE-02 (interim)
- **Steps:**
  1. Project settings only seed the defaults of new pipeline instances (`PostInitProperties`). `ExecutePipeline` reads only the instance flags, the same way in all three pipelines.
  2. Remove the parent-folder fallback. Match the created object exactly: `InCreatedObject`'s package must be under `ContentBasePath/<BaseName>/` with a `/` boundary check, and prefer the object passed to the callback over a folder scan.
  3. (The full fix is P3.4, which moves to `ExecutePostImportPipeline`.)
- **Acceptance:** importing two characters `Alice` and `Alice2` into sibling folders creates each character's assets bound to its own mesh. Unticking "Generate Spring Bone Data" in the dialog prevents generation even when the project setting is on.
- **Status (#123, merged):** done in all three pipelines. Settings seed defaults only (`PostInitProperties`); `ExecutePipeline` reads instance flags. `VRMPipelineTargets.h` decides which mesh is this import's: the mesh's recorded source file must be this one (same full path or same file name), or with none recorded the mesh must be inside `<ContentBasePath>/<file name>`. The parent-folder fallback is gone. `UAssetImportData` stores the source path relative to the package and resolves it on read; in the test it came back under the engine folder, so a full-path comparison alone would reject every mesh. Covered by `VRM.Pipeline.Targets.*` and `VRM.Pipeline.Toggles`; the real two-character import is still an editor check.

### P1.16 Versioned data assets
- **Resolves:** SP-07 · **Must land with or before** P1.11, P1.12 and P1.13 merge
- **Note (2026-09-25):** P1.11 merged first, so the `ConvertedColliderAxes` entry now covers assets that already exist. Any asset without the new version was imported before #115 and needs a reimport. Do P1.16 next, before P1.12 or P1.13 change the data again.
- **Status (#118, merged; entries added by #119 and #120):** `FVRMSpringDataCustomVersion` has `ConvertedColliderAxes`, `PerJointParameters` (upgraded on load by copying each spring's values to its joints) and `ExpandedVRM0Chains` (VRM 0.x assets with springs are flagged). `bNeedsReimport` is saved with the asset, so re-saving an old asset doesn't clear it. The flag is logged on load and reported when an AnimBlueprint that uses the asset compiles. **Not done:** the one-click "Reimport from source" action. Spring data is generated by the skeletal mesh import, so there is no separate reimport to trigger; the warning names the file instead.
- **Steps:** add `FVRMSpringDataCustomVersion` with entries such as `ConvertedColliderAxes`, `PerJointParameters` and `ExpandedVRM0Chains`. In `UVRMSpringBoneData::Serialize`/`PostLoad`, upgrade older assets where possible (for example, copy spring-level parameters into joints). Where an upgrade isn't possible (axes can't be fixed without the source file), set a `bNeedsReimport` flag, log a clear warning with a one-click "Reimport from source" action (the source path is stored in `SourceFilename`), and mark the asset in the AnimGraph node's compile validation.
- **Acceptance:** a test loads a struct serialized with the old version and checks that the upgrade result or warning is produced.

### P1.17 Stop editing user config and content at startup
- **Resolves:** PE-03, PE-04 (interim)
- **Steps:**
  1. Remove the calls from `StartupModule`. Add a Project Settings button **"Register VRM import pipelines"** (in `UVRMInterchangeSettings` with `CallInEditor`, or a details customization) that performs the registration once, shows what changed, and doesn't remove the generic assets pipeline unless the user ticks an option. Optionally show a one-time notification with that button when the pipelines aren't registered.
  2. Delete `VRMPostImportReparent.cpp`. Until P3.4, set per-material MI parents at translation time: the per-material `UInterchangeMaterialInstanceNode` uses the character MI node as its parent, if 5.6 supports node-UID parents **[Verify]**. Otherwise do the reparenting inside the pipeline for the MIs created by this import only.
- **Acceptance:** launching the editor twice produces no config diffs. Creating an asset named `MI_VRM_Test` by hand is not modified. Imported MIs still end up parented to the character MI.

### P1.18 Small correctness and hygiene items
- **Resolves:** VMC-18, T-12, T-14, PE-09 (empty header), SR-08
- **Steps:** fix the garbled labels (`"Unity → UE coordinates"`, `"Meters → centimeters"`; make sure the source file is saved as UTF-8 with BOM, or use `→`). Check `cgltf_validate`'s result and buffer-view bounds. Delete the 0-byte `Public/VRMSpringBonesPostImportPipeline.h`. Remove `Animations` from supported asset types. Fix the header comments.
- **Acceptance:** compiles. The UI shows the arrows. A malformed fixture (truncated buffer) fails the import with a clear message and doesn't crash.

---

## Phase 2: Spring solver rework

Goal: a spring solver that follows the VRM specification, doesn't depend on frame rate, and has tests. **Depends on:** P1.11, P1.12, P1.13, P1.14, P1.16.

### P2.1 Extract a pure solver core
- **Resolves:** SR-01 to SR-03, SR-06, SR-07 (structure)
- **Files:** new `VRMSpringBonesRuntime/Public/VRMSpringSolver.h`, `Private/VRMSpringSolver.cpp`; `AnimNode_VRMSpringBones.cpp`
- **Design:**
  ```text
  FVRMSpringSolver (plain C++, no anim-graph types)
    Init(const FVRMSpringConfig&, const TArray<int32>& BoneIndexPerJoint,
         const TArray<int32>& ParentBoneIndex, TConstArrayView<FTransform> RestLocal)
    Reset(TConstArrayView<FTransform> CurrentComponentPose, const FTransform& ComponentToWorld)
    Step(float Dt, TArrayView<FTransform> InOutComponentPose, const FTransform& ComponentToWorld,
         const FVRMSpringExternalForces&)
  ```
  The anim node becomes a thin adapter: it maps compact pose indices, copies only the affected bones in, and writes results out.
- **Algorithm** (the VRM 1.0 reference, per joint, processed root to tip within each chain):
  ```text
  worldPos(head) = parent's *updated* world transform ⊗ localRest(head)
  inertia      = (currentTail - prevTail) * (1 - drag)
  stiffForce   = parentWorldRot * localRestRot * boneAxis * stiffness * dt
  external     = gravityDir * gravityPower * dt  (+ optional external velocity * dt)
  nextTail     = currentTail + inertia + stiffForce + external
  nextTail     = head + normalize(nextTail - head) * boneLength       // length constraint
  nextTail     = resolveCollisions(nextTail)                           // per-joint hitRadius
  nextTail     = head + normalize(nextTail - head) * boneLength
  prevTail = currentTail; currentTail = nextTail
  rotation     = FromToRotation(parentWorldRot*localRestRot*boneAxis, nextTail-head) * (parentWorldRot*localRestRot)
  write rotation; update this joint's world transform so children use it
  ```
  - Simulate in **world space**, or in the `center` bone's space when `center` is set (transform `prevTail`/`currentTail` by the center's delta each step).
  - **Fixed sub-steps:** `Accumulator += min(dt, MaxDt)`, then step at `1/SubstepHz` (default 60 Hz, configurable, maximum N steps per frame). Interpolation between steps is optional.
  - Pause means don't step and re-emit the last pose.
  - Only rotations are written. Translations come from the parent chain, so bone lengths never change.
- **Acceptance:**
  - Unit tests on the solver core, independent of the anim graph:
    1. A single chain hanging under gravity settles to within 1 mm of the analytic direction.
    2. After 2 simulated seconds, results at 30, 60 and 144 Hz input rates agree within tolerance (sub-stepping).
    3. Moving the root laterally produces trailing motion in world space (fails before the change).
    4. A sphere collider keeps tails outside radius + hitRadius.
    5. Center space: moving the center with the root produces no inertia.
  - Visual parity check against UniVRM or three-vrm on the same model and motion (record GIFs in the PR).
- **Status (#126, merged):** done, with these choices:
  - The solver takes a flat list of bones and their component-space transforms (`Init(Setup)`, `Reset(BonesCS, ComponentToWorld)`, `Step(Dt, BonesCS, ComponentToWorld, ExternalVelocity)`), not the `RestLocal` array in the design above. Rest data (bone axis and length) is captured from the pose at reset.
  - The rest rotation each step is the joint's *animated* local rotation under its parent's simulated rotation, not the import-time rest rotation. With no animation on the spring bones this is the reference behaviour; with animation, the springs follow it.
  - The stiffness force is `stiffness * dt` in metres as in the reference, so ×100 in cm. This changes how springs feel more than anything else in the task.
  - In VRM 1.0 the last joint of a spring only marks the tail and is not rotated (as in three-vrm). VRM 0.x chains end in a 7 cm virtual tail. The hit radius is no longer clamped to half the bone length.
  - Interpolation between steps is not done.

  Acceptance tests 1 to 5 are `VRM.SpringBones.Solver.Gravity`, `.FrameRate`, `.WorldInertia`, `.SphereCollider` and `.CenterSpace`, plus `.Substeps` (accumulator and hitch cap). Their thresholds were set with a Python model of the same algorithm before the first CI run. The one failure on that run was the sphere test measuring the first frame, where the tail starts inside the sphere. **Not done:** the visual parity check (needs the editor).

### P2.2 Collider precomputation
- **Resolves:** SR-06
- **Steps:** resolve collider bone indices in `BuildMappings` into `TArray<FCompactPoseBoneIndex>`. Unresolved colliders are **disabled** with a one-time warning instead of being placed at the origin. Compute collider world transforms once per step (not per joint). Remove the `FCSPose` copy.
- **Acceptance:** Unreal Insights shows no `FBoneReference::Initialize` in the per-frame path. Timing for a 200-joint, 30-collider model is recorded in the PR (see P5 targets).
- **Status (#126 and #127, merged):** the steps landed with P2.1: collider bones are resolved when the mappings are built, a collider on a bone the skeleton doesn't have is left out, collider transforms are computed once per step, and the per-joint `FCSPose` lookups are gone. #127 added the one-time warning (once per asset, listing the bones) and the node-index fallback for collider bone names. **Not done:** the Insights capture and timing (needs the editor).

### P2.3 AnimGraph node editor quality
- **Resolves:** SR-08
- **Steps:** implement `ValidateAnimNodeDuringCompilation`: warn when `SpringData` is missing, when data bones aren't in the target skeleton (list the first N), and when `bNeedsReimport` is set. Push live edits to the preview instance through `CopyNodeDataToPreviewNode`. Add a details category for sub-step Hz, max dt, simulation space and debug draw toggles (the CVars stay).
- **Acceptance:** compiling an AnimBP with mismatched data shows actionable warnings in the compiler results.
- **Status (#127, merged):** compile warnings for missing spring data, data with no springs, bones the skeleton doesn't have (first five listed) and `bNeedsReimport`. The missing-data warning only fires when the Spring Data pin is hidden: the plugin's template AnimBlueprint gets its data through the graph (the pipeline sets its `SpringConfig` variable), which the check can't see, and the first version warned on it in CI. Node settings: Simulation Space (World or Component), Substep Hz, Max Delta Time, and debug draw toggles. **Not done:** `CopyNodeDataToPreviewNode`; its 5.6 signature and call site couldn't be confirmed without engine source, and a wrong `override` breaks the build. Check in the editor whether preview edits already apply.

---

## Phase 3: Architectural refactoring

### P3.1 Split VMCLiveLink into parser, state and source
- **Resolves:** VMC-13, VMC-14, VMC-15, VMC-17 (part), VMC-26 · **Depends on:** P1.1 to P1.5, D-1
- **Target structure** (all under `VMCLiveLink/Private` unless noted):
  - `VMCProtocol.h/.cpp`: pure functions `ParseMessage(const FOSCMessage&) -> TVariant<FVMCBonePos, FVMCRootPos, FVMCBlendVal, FVMCBlendApply, FVMCTime, FVMCStatus, FVMCTrackerPos, FVMCUnknown>`. Match addresses against precomputed `FOSCAddress` constants or by length plus compare, with no `FString` allocation per message.
  - `VMCFrameAssembler.h/.cpp`: owns the index-based skeleton from P1.3 (`TArray<FTransform> LocalPose` indexed by bone), a curve name table plus `TArray<float>` values, and the dirty flags for static data. Produces `(FLiveLinkStaticDataStruct?, FLiveLinkFrameDataStruct)` on Apply.
  - `FVMCLiveLinkSource`: lifecycle, settings, the OSC server, status and counters, and pushing to the client.
  - `Public/VMCLiveLinkSourceSettings.h`: a `ULiveLinkSourceSettings` subclass with port, bind address, `bUnityToUE`, `bMetersToCm`, yaw offset, `bZeroMissingCurves`, sender allowlist and timeout. Implement `GetSettingsClass()`/`InitializeSettings()`/`OnSettingsChanged()`. Changing the port restarts the listener.
  - A single connection-string codec (`FVMCConnectionSettings::FromString/ToString`) with validation and round-trip tests.
- **Threading (per D-1):** recommended option is to receive on the OSC thread (confirm whether `UOSCServer` can dispatch off the game thread in 5.6 **[Verify]**; otherwise use `FUdpSocketReceiver` with the OSC plugin's packet parsing), assemble on that thread, and push with `_AnyThread` calls. That gives one writer, no `DataGuard`, and a game thread that is never involved.
- **Timestamps:** set `WorldTime` from the receive time (`FPlatformTime::Seconds()`). If `/VMC/Ext/T` is present, map the sender clock to `MetaData.SceneTime` (frame rate from settings) so Live Link buffering and interpolation modes work.
- **Acceptance:** replay tests (P0.3 captures) give identical output before and after the refactor (golden frames). Parser unit tests cover every address. There are no per-message heap allocations (checked with `LLM` or a counting allocator in a test).

### P3.2 Single place for remapping; immutable workers; editor code in the editor module
- **Resolves:** VMC-06, VMC-09, VMC-10, VMC-22, VMC-23, VMC-24 · **Depends on:** P3.1
- **Steps:**
  1. The source publishes raw VMC names only (remove `CachedBoneMap`/`CachedCurveMap` and `RefreshStaticMapsFromSettings`). Reference-pose translation offsets move into the remapper worker's `RemapFrameData` (they depend on the target skeleton, which is remapper knowledge). The worker receives the ref translations when it is built.
  2. `CreateWorker()` builds a new immutable worker from the current properties. `PostEditChangeProperty` and the API setters mark `bDirty`, and Live Link calls `CreateWorker` again. Delete `SyncWorker`.
  3. `Initialize` no longer guesses or applies presets. Auto-detect runs only from an explicit button or when `bAutoDetectMappingFromReference` is on **and** the maps are empty. It is asynchronous: gather candidates from the asset registry by tag, then load candidates asynchronously.
  4. Move `CreateAndAssignNewMappingAsset`, `SaveCurrentMappingToAssignedAsset`, signature capture and auto-detect UI into `VMCLiveLinkEditor` as an `IDetailCustomization` for `UVMCLiveLinkRemapper` with real buttons. Remove `UnrealEd`/`AssetTools` from the runtime `Build.cs`.
  5. Mapping asset: add `UPROPERTY(AssetRegistrySearchable)` signature tag(s) and switch to a stable hash (`FCrc::StrCrc32` over sorted normalized names, plus `SignatureVersion`). Keep a `PostLoad` that recomputes signatures from `ExampleReferenceMeshes` when the version is old.
  6. Move `MatchesMesh` out of `WITH_EDITOR`, or drop `BlueprintCallable`.
  7. Migrate `FAssetTypeActions_VMCLiveLinkMappingAsset` to `UAssetDefinition_VMCLiveLinkMappingAsset`. Replace `StaticDuplicateObject` in the retarget actor factory with `IAssetTools::DuplicateAsset`.
- **Acceptance:** a Game (non-editor) target builds without `UnrealEd`. Re-initializing a subject leaves user maps and preset untouched. A test checks that a worker created before an edit keeps the old maps (snapshot semantics).

### P3.3 Parse each VRM file once, into a shared document model
- **Resolves:** T-10, T-11 (parsing), SP-06 (duplicate parse, `RawJson`), PE-09 (cgltf), X-01 (foundation)
- **Design:**
  - Add a runtime module **`VRMCore`** (in the VRMInterchange plugin) containing:
    - `FVRMDocument`: the result of **one** `cgltf_parse` + `load_buffers`, plus the VRM extension JSON parsed once. Holds nodes (names, parents, local TRS, global rest), skins, meshes, materials, images, VRM version, humanoid map, expressions, spring config, look-at, first-person and meta.
    - The **only** `CGLTF_IMPLEMENTATION` in the codebase. Other modules call `VRMCore` APIs and don't include `cgltf.h`.
    - `VRMCoordinateConversion` (from P1.11).
  - The translator builds `FVRMDocument`, emits Interchange nodes, and additionally emits a custom **`UInterchangeVRMNode`** (a `UInterchangeBaseNode` subclass) that carries the VRM-specific data (spring config, humanoid, expressions, meta) as attributes or a serialized blob.
  - Pipelines read `UInterchangeVRMNode` from the node container in `ExecutePipeline`. **No pipeline opens the source file.**
  - Drop `FVRMSpringConfig::RawJson` (or make it `WITH_EDITORONLY_DATA` and opt-in for debugging).
  - Remove `mutable` translator state. Keep `FVRMDocument` in a `TSharedPtr` owned by the translator between `Translate` and the payload calls, and look payloads up by typed index kept alongside the key.
- **Acceptance:** Insights or file-read counters show the `.vrm` file is read once per import. Each module contains cgltf at most once. Import output is identical to before the change (compare generated asset properties).

### P3.4 Pipeline base class and Interchange-native post-import
- **Resolves:** PE-02, PE-04, PE-06, PE-07, PE-08 · **Depends on:** P3.3
- **Steps:**
  1. `UVRMPipelineBase : UInterchangePipelineBase` with shared helpers: character name and folder resolution, `CreateOrReplaceAsset<T>()` (AssetTools with a unique name, or a proper replace through `ObjectTools`), and template duplication through `IAssetTools::DuplicateAsset`.
  2. Replace the `UImportSubsystem` delegates with `ExecutePostImportPipeline(BaseNodeContainer, FactoryNodeKey, CreatedAsset, bIsAReimport)`. Act when `CreatedAsset` is this import's `USkeletalMesh`/`USkeleton`. This removes `bDeferred*` state and folder scans.
  3. Actor Blueprint wiring: modify the **SCS node template** (`USimpleConstructionScript::GetAllNodes()` → `ComponentTemplate`) or the inherited component override (`UInheritableComponentHandler`), then `FBlueprintEditorUtils::MarkBlueprintAsModified` and compile. Replace the property-name lookups with a small interface (`IVRMCharacterSetup`) implemented by the template Blueprints, or a C++ base actor class (`AVRMLiveLinkCharacter`) that exposes `SetVRMCharacterMesh()`.
  4. Material parents: finish P1.17 by setting parents inside the pipeline or translator for this import's MIs only.
- **Acceptance:** all pipeline tests pass using `ExecutePostImportPipeline`. Overwrite mode replaces existing assets without errors. Asset wiring works with SCS-based templates.

### P3.5 Build files and platforms
- **Resolves:** PE-09, PE-10, VMC-21
- **Steps:** remove editor modules from the runtime `VRMInterchange.build.cs` and deduplicate its dependency lists. Replace `PrivateIncludePaths` hacks with module dependencies. Decide platform allowlists (D-2): add `Mac` and `Linux` to editor modules at minimum if they compile. Make the empty module classes `FDefaultModuleImpl`.
- **Acceptance:** `BuildPlugin` succeeds for every allowed platform available to CI. Include-what-you-use builds (`bUseUnity = false` in a CI job) succeed.

---

## Phase 4: Functional features

### P4.1 VRM avatar description asset
- **Resolves:** X-01, T-08 (expressions), enables P4.2 to P4.4 · **Depends on:** P3.3
- **Steps:** add `UVRMAvatarDescription` (runtime, `VRMCore`), generated per import next to the mesh. It contains:
  - `EVRMVersion`, meta (title, author, licence URLs, allowed-usage flags), and a thumbnail texture reference.
  - `TMap<EVRMHumanBone, FName> HumanoidToBone` (a UENUM of the VRM 1.0 human bone set, with 0.x names mapped onto it).
  - `TArray<FVRMExpression>`: name, preset, `isBinary`, override flags (blink, lookAt, mouth), morph binds (morph target name and weight), and material colour/UV binds (stored, even if applying them is deferred).
  - Look-at settings, first-person mesh annotations, and a reference to the spring data asset.
- Show licence and meta in the import summary notification (VRM licences often restrict usage).
- **Acceptance:** fixtures produce descriptions with complete humanoid maps. Expression binds point at morph targets that exist on the mesh.

### P4.2 Driving expressions from VMC `Blend/Val`
- **Resolves:** X-01 (runtime side), VMC-08 (the real fix) · **Depends on:** P4.1
- **Steps:** add an AnimGraph node **"VRM Expressions"** (`FAnimNode_VRMExpressions`, in `VRMCore` or a new `VRMExpressionsRuntime` module) that:
  - Reads expression weights from curves on the input pose (for example `Joy` or `happy` from Live Link) or from exposed pins.
  - Expands them through `UVRMAvatarDescription` into morph-target curve values (a weighted sum of binds, `isBinary` rounding, override rules: when blink or mouth overrides are active, suppress lower-priority expressions per spec).
  - Maps VRM 0.x and 1.0 expression names onto each other (Joy ↔ happy, A ↔ aa, and so on) so either sender version works with either avatar version.
- Update the Live Link template ABP to use this node (a template asset change, described in the PR).
- **Acceptance:** replaying a VSeeFace capture drives the correct morph targets on a VRoid VRM with no hand-authored curve map.

### P4.3 IK Rig generated from the humanoid map
- **Resolves:** PE-05 · **Depends on:** P4.1, P3.4
- **Steps:** generate the `UIKRigDefinition` with `UIKRigController`: set the retarget root (hips), add the standard chains (spine, neck, head, arms, legs, fingers, using the same chain names as the UE5 mannequin IK Rig so retargeters auto-map), and optionally add leg and arm goals. If humanoid data is missing, fall back to the current template duplication.
- **Acceptance:** fixtures with **non-VRoid bone names** get complete chains. An automatic retargeter to the UE5 mannequin maps every standard chain.

### P4.4 Creating a VMCLiveLink mapping from a VRM avatar
- **Resolves:** X-01 (VMCLiveLink side) · **Depends on:** P4.1, P3.2, D-4
- **Steps:** (per D-4) add an optional plugin dependency from VMCLiveLink to VRMInterchange's `VRMCore`, or put the integration in a small third plugin. Features:
  - Remapper field `AvatarDescription`. When set, `BoneNameMap` = Unity `HumanBodyBones` name → skeleton bone from `HumanoidToBone` (case-insensitive humanoid match), and the reference skeleton comes from the avatar's mesh.
  - Curve handling: pass expression names through (P4.2 expands them), or optionally expand into morph curves in the remapper for users who don't use the AnimGraph node.
  - Editor button "Create Mapping Asset from VRM Avatar".
- **Acceptance:** with a VRM imported and a VMC stream running, choosing the avatar description is the only setup step needed for correct body and face.

### P4.5 Materials
- **Resolves:** T-07 · **Depends on:** P3.3, D-7
- **Steps:**
  1. Apply glTF PBR factors (base colour, emissive, metallic, roughness), alpha mode and cutoff (blend mode through static switches or separate parent materials), and double-sided, as MI parameters.
  2. Parse MToon (0.x `materialProperties` and 1.0 `VRMC_materials_mtoon`) into a documented parameter set on `M_VRM_Master` (or a dedicated `M_VRM_MToon`): shade colour and texture, shading shift/toony, rim, matcap, and outline width and colour (outline as an optional second material or a post-process).
  3. `KHR_texture_transform` (UV scale and offset parameters) and `COLOR_0` (a vertex-colour switch).
- **Acceptance:** a material comparison scene (screenshots in the PR) against three-vrm or UniVRM renders of the same model shows matching alpha and cutout, and a recognizable toon ramp.

### P4.6 Morph targets
- **Resolves:** T-08 (normals and grouping)
- **Steps:** read `NORMAL` deltas where present (without them, let UE recompute and document the trade-off). Merge unnamed targets only **within** the same mesh, and name them `<MeshName>_morph_<i>`. Warn about name collisions between meshes whose shapes differ.
- **Acceptance:** an expression with normal deltas shades correctly. Unnamed targets across meshes are no longer merged.

### P4.7 VMC protocol coverage
- **Resolves:** VMC-19 · **Depends on:** P3.1
- **Steps:**
  - `/VMC/Ext/OK`: surface the loaded and calibration state in `GetSourceStatus` ("Sender: calibrating", and so on).
  - `/VMC/Ext/T`: used for timestamps (P3.1).
  - `/VMC/Ext/Tra|Hmd|Con/Pos`: optional (settings toggle) additional Live Link subjects with the Transform role, named `<Subject>_<serial>`.
  - `/VMC/Ext/Cam`: optional camera subject.
  - A sender allowlist or "lock to first sender" option.
- **Acceptance:** replayed captures create tracker subjects when enabled, and the status text reflects `/VMC/Ext/OK`.

---

## Phase 5: Optimization

Do this after correctness, so each change can be measured against golden outputs. Record before and after numbers from Unreal Insights in each PR.

| ID | Area | Change | Target |
|---|---|---|---|
| P5.1 | VMC hot path | Delivered by P3.1: no per-message allocation, index-based pose, one static push per change, no map copies or hashing per Apply, no `LoadSynchronous` in the stream path | < 20 µs game-thread cost per Apply at 55 bones and 60 curves (or 0 µs on the game thread with off-thread assembly) |
| P5.2 | Remapper worker | Precompute `SourceIndex → TargetIndex` arrays when static data changes; normalizer uses cached indices | Remap cost O(bones + curves) with no hashing per frame |
| P5.3 | Spring solver | Delivered by P2.1/P2.2: no pose copy, precomputed collider bones, contiguous SoA joint arrays; optionally `ParallelFor` over independent chains when joints > threshold (measure first, since the anim thread is already parallel across instances) | 200 joints, 30 colliders: < 0.15 ms per character per frame on the reference CPU (document the CPU) |
| P5.4 | Translator skin weights | Fixed-size `TStaticArray<FBoneWeight, MAX_TOTAL_INFLUENCES>` or `FBoneWeights::Create(TArrayView)` from a stack array; no per-vertex heap allocation | 30 % less translate time on a 50k-vertex model |
| P5.5 | Morph payloads | Confirm (in engine source) the minimum `FMeshDescription` a MORPHTARGET payload needs. If positions with matching vertex IDs are enough, emit only vertex positions (no triangles, UVs or skin weights). Otherwise share the base topology. Run morph payload building in parallel if Interchange calls payloads concurrently (check thread safety of the shared document) | Import time for a 60-morph VRoid model reduced by ≥ 40 % |
| P5.6 | Textures | Decode straight to BGRA8 (`GetRaw(ERGBFormat::BGRA, 8)`), keep 16-bit PNGs as `TSF_RGBA16`, and don't store compressed bytes plus decoded bytes longer than needed | Lower peak memory during import (report the number) |
| P5.7 | File I/O | Delivered by P3.3: one read and parse per import; MD5 from the in-memory buffer | One file read per import |
| P5.8 | Editor startup | Delivered by P1.17: no asset-registry hooks or synchronous loads at startup | No VRM-related synchronous loads in the startup trace |

---

## Phase 6: Usability

### P6.1 VMC source creation panel and status
- **Resolves:** VMC-16, VMC-17, VMC-18 · **Depends on:** P3.1
- **Steps:**
  - The creation panel exposes every `UVMCLiveLinkSourceSettings` field (through a details view of a transient settings object, not hand-built Slate). Validate the port range and warn when the port is in use (try to bind before creating).
  - The status text shows the state ("Listening on :39539", "Receiving 60.0 fps from 192.168.1.20", "No data for 5 s", "Port in use"), and `GetSourceMachineName` returns the last sender's IP.
  - Add tooltips that explain each coordinate option with the actual axis mapping.
  - Add a debug console command `VMC.Stats` that prints message rates per address and the unknown addresses seen.
- **Acceptance:** a manual test script (in the PR) covers port-in-use, sender stopping and resuming, and settings edits after creation.

### P6.2 Remapper details panel
- **Resolves:** VMC-22 (UX), VMC-09
- **Steps:** group the details into Source → Mapping (preset, mapping asset, avatar description) → Normalizer (off by default, with an explanation) → Tools (Detect from subject, Apply asset, Save to asset, Create asset). Show a live table of incoming → outgoing names, highlighting unmapped and duplicate targets.
- **Acceptance:** a user can see why a curve isn't reaching the mesh without reading logs.

### P6.3 Import experience
- **Resolves:** PE-01, PE-03 (UX), T-12
- **Steps:**
  - One "VRM Import" pipeline category in the dialog with clearly named toggles (Spring bones, IK Rig, Live Link actor, Retarget actor, Avatar description). Tooltips say what gets created and where.
  - After import, show one `FNotificationInfo` listing the created assets (clickable to sync the Content Browser) and the VRM licence summary, plus an `FMessageLog("VRMInterchange")` page for warnings (unresolved bones, lenient schema variants, bind-pose mismatch, non-VRM glTF).
  - Reimport: clearly state what is preserved (user-edited spring parameters, if P1.16 tracks per-field overrides) and what is regenerated.
- **Acceptance:** importing the fixtures produces one notification and a message-log page with no log-only warnings.

### P6.4 Spring data editing
- **Resolves:** SR-05 (live edits), SP-06
- **Steps:** changes to `UVRMSpringBoneData` update running previews and PIE instances immediately (hash-based rebuild from P1.14). Add editor actions "Scale all stiffness/drag/gravity", "Reset spring to source values" and "Reimport from source". Add a debug-draw toggle in the node details.
- **Acceptance:** editing stiffness in the asset changes the preview viewport without recompiling the ABP.

---

## Phase 7: Documentation

Each item can be done alongside the phase that changes the behaviour it describes. Documentation-only fixes of current inaccuracies (P7.1) can land immediately.

### P7.1 Fix inaccurate claims now
- **Resolves:** X-04
- **Steps:** in `Plugins/VRMInterchange/README.md`, remove or qualify "Deterministic Sub-stepping", "Hierarchical Propagation" and "proper bone orientations" until Phase 2 lands. State the net axis mapping. Add "Known limitations" entries for MToon, expressions, VRM 0.x chain expansion and component-space simulation, and remove each entry as it's fixed.
- **Acceptance:** every feature claim in the README maps to code that exists.

### P7.2 VMCLiveLink plugin README
- **Steps:** create `Plugins/VMCLiveLink/README.md` covering:
  - Quick start (5 steps: enable the plugin, add the source, set the port, configure the sender, add a Live Link Pose node).
  - **Sender setup guides:** VSeeFace, VirtualMotionCapture, Warudo, VMC4B/Blender. Include the ports and which options to enable.
  - **Supported message matrix** (address, arguments, supported yes/no/partial, notes).
  - Coordinate conventions (Unity → UE mapping, units, yaw offset).
  - The remapper (presets, mapping assets, avatar description, normalizer rules).
  - Troubleshooting (no data, firewall, port in use, T-pose only, mirrored limbs, flickering curves).
  - Performance notes.
- **Acceptance:** a new user can go from install to a moving character using only this README.

### P7.3 Architecture and developer docs
- **Steps:** add `docs/ARCHITECTURE.md` with module diagrams for both plugins, the data flow (OSC → parser → assembler → Live Link → remapper → AnimBP; `.vrm` → `FVRMDocument` → Interchange nodes → pipelines → assets), the threading model, coordinate conventions (with a worked example), asset versioning and the test strategy. Add `CONTRIBUTING.md` (build, test, fixtures, header rule, PR checklist from B.0), which is already outlined in `PRODUCTION_READINESS_ACTION_ITEMS.md` §2.4.
- **Acceptance:** documents exist, are linked from the root README, and describe the post-refactor structure.

### P7.4 API documentation
- **Steps:** add Doxygen-style comments to every public header (`Public/`), covering purpose, threading, ownership and units (cm vs m, local vs component vs world). Required for `ILiveLinkSource` overrides, the remapper API, `UVRMSpringBoneData`/`FVRMSpring*` (units and spaces per field) and `UVRMAvatarDescription`.
- **Acceptance:** each public type and function has a comment. Unit and space are documented for every geometric field.

### P7.5 Packaging and release documentation
- **Steps:** populate both `FilterPlugin.ini` files (`/README.md`, `/LICENSE*`, `/Docs/...`). Fill `DocsURL`/`SupportURL` in both `.uplugin` files. Add `CHANGELOG.md` (Keep a Changelog format) with a section per phase. Bump versions (`0.2.0` after Phase 1, `1.0.0` when Phases 1 to 3 plus P7 are done).
- **Acceptance:** a packaged plugin zip contains README and LICENSE. The Fab readiness checklist items for URLs are satisfied.

### P7.5b Archive stale planning documents
- **Steps:** move `postmortems/`, `Planning Docs/Opus4.1 Improvements/`, `Planning Docs/SpringBones_Rewrite_Dev_Plan/`, `VRM_SpringBones_Physics_Analysis.md` and the superseded readiness docs into `docs/archive/`, each with a one-line header ("Historical; superseded by ..."). Keep this plan and the Fab readiness report current.
- **Acceptance:** the root directory contains only README, CHANGELOG, CONTRIBUTING, LICENSE and current plans.

---

## B.8 Decisions needed from the owner

| ID | Decision | Recommendation |
|---|---|---|
| D-1 | VMCLiveLink threading: game-thread dispatch (simple) or receive-thread assembly (lower latency, frame-rate independent) | Receive thread (P3.1). It suits a live-performance tool. |
| D-2 | Supported platforms and engine versions (5.6 only, or 5.5 to 5.7) | Editor: Win64, Mac, Linux. Runtime: all desktop. Support the current and previous engine version and add a CI matrix. |
| D-3 | Keep the "identity bone rotation" reference pose, or preserve source bind rotations | Keep identity for now (it matches VMC local rotations and the VRM 1.0 normalized pose), but make it explicit and documented, and apply it consistently to spring data (P1.11). Revisit if non-VRM glTF support becomes a goal. |
| D-4 | Dependency direction between the plugins for avatar-driven mapping (P4.4) | VRMInterchange's `VRMCore` stays independent. VMCLiveLink takes an **optional** plugin dependency, or a tiny third "VMC-VRM Bridge" plugin holds the integration. |
| D-5 | Apply VMC v2.1 root scale and offset | Parse always. Apply scale to root translation behind a setting (default on) because senders use it for avatar height calibration. |
| D-6 | Keep lenient, non-spec spring schema parsing | Keep for one release behind a setting that logs when used, then remove. |
| D-7 | Material fidelity target: PBR approximation or MToon parity | Basic MToon (shade colour and ramp, rim, outline) on a dedicated master material. Full parity is a separate project. |
| D-8 | Test assets: may real third-party VRM or VMC captures be committed? | Commit only synthetic fixtures and your own captures. Keep third-party samples in a local-only folder that tests skip when it is absent. |

---

## B.9 Dependency graph and suggested order

```text
Phase 0:  P0.1 ─┬─ P0.2 ─┬─ P1.7 ─ P1.8 ─┬─ P1.9
                │        │               ├─ P1.11 ─┬─ P1.12
                │        │               │         └─ P1.13 ── (P1.16 must merge with these)
                │        └─ P1.10        │
                └─ P0.3 ─ P1.1..P1.6     └─ P3.3 ─┬─ P3.4 ─ P4.3
                          │                       ├─ P4.1 ─┬─ P4.2
                          └─ P3.1 ─ P3.2 ─────────┼────────┴─ P4.4
                                    └─ P4.7, P6.1 └─ P4.5, P4.6
P1.14 ─┐
P1.11..P1.13, P1.16 ─┴─ P2.1 ─ P2.2 ─ P2.3
P1.15, P1.17, P1.18, P0.4, P7.1: independent, can start immediately
Phase 5 items are measured inside the phases that deliver them; P5.4–P5.6 standalone after P3.3.
Phase 7 docs accompany each behaviour change; P7.1 immediately.
```

**Suggested first sprint** (low risk, high value): P0.1, P0.4, P1.2, P1.4, P1.6, P1.17, P1.18, P7.1. Then P0.2/P0.3, which unblock the P0 bug fixes P1.1, P1.3, P1.7 and P1.11.

## B.10 Implementation progress

*Last updated 2026-09-26 (night).* Phases 1 and 2 are complete, and every PR opened for the plan so far is merged. `main` builds Editor, Game Development and Shipping on UE 5.6, and all 47 automation tests pass.

### Status by task

| Task | PR | Status | Notes |
|---|---|---|---|
| P0.1 CI builds PRs and runs tests | #104 | Merged | |
| P0.2 Synthetic VRM fixtures | #105 | Merged | Seventh fixture `bind_pose_offset` added in #109 |
| P0.3 VMC test sender | #106 | Merged | |
| P0.4 Logging categories | #100 | Merged | |
| P0.5 CI hardening | #112 | Merged (steps 1, 2, 5) | Step 3 done (tag deleted); step 4: toolchain warning accepted; step 6 open |
| P1.1, P1.3, P1.5 VMC receive path | #107 | Merged | |
| P1.2, P1.4 Source lifetime, subject settings | #101 | Merged | |
| P1.6 Normalizer and presets | #102 | Merged | |
| P1.7 Joint mapping, multiple skins | #108 | Merged | |
| P1.8 Node transforms, rigid meshes, bind pose | #109 | Merged | First build failed on a shadowed local (C4456) in the new test |
| P1.9 Version detection and forward axis | #116 | Merged | Stacked on #115. T-05 confirmed from three-vrm, not in the editor |
| P1.11 One conversion module; spring geometry | #115 | Merged | Also fixed SP-08. Merged before P1.16 (see P1.11 "Deviation"). Test step hit Windows error 4551 once (X-07) |
| P1.17 No startup edits | #103 | Merged | First build failed to compile (X-08) |
| P1.18 Hygiene | #100 | Merged | |
| P7.1 README corrections and this plan | #99, #111 | Merged | |
| (unplanned) Six stale tests | #110 | Merged | X-05 |
| (unplanned) Test filter so VMC tests run | #113 | Merged | X-07. The first run with it: 19/19 tests (15 VRM, 4 VMC) |
| P1.16 Versioned spring data | #118 | Merged | One-click reimport action not done (see P1.16 status) |
| P1.13 Spec VRM 1.0 spring parsing | #119 | Merged | Lenient schema is a console variable, on by default |
| P1.12 Expand VRM 0.x chains | #120 | Merged | One chain per branch rather than per root-to-leaf path (see P1.12 status). The `main` merge needed a manual fix (rule 13) |
| P1.14 Anim node robustness | #122 | Merged | Its new LOD test found an out-of-bounds read on `main` (see P1.14 status) |
| P1.15 Pipeline toggles and targets | #123 | Merged | Source-file match accepts the file name, because the stored path doesn't round-trip (see P1.15 status) |
| P1.10 Texture colour space and normal maps | #125 | Merged | First build failed to link (missing `InterchangeImport` dependency) |
| P2.1 Solver core | #126 | Merged | First run: the sphere test measured the first frame (see P2.1 status) |
| P2.2 Collider precomputation | #126, #127 | Merged | Insights timing not done |
| P2.3 Node editor quality | #127 | Merged | `CopyNodeDataToPreviewNode` not done. First run warned on the template AnimBlueprint |
| (plan updates) | #114, #117, #121, #124, this PR | Merged | |

**Not started:** Phases 3 to 7.

**Recommended next:**
1. Owner: decide D-1 (VMCLiveLink threading). P3.1 depends on it.
2. Phase 3, starting with P3.1 (split VMCLiveLink into parser, state and source) once D-1 is decided. P3.3 (parse each VRM file once) doesn't depend on D-1 and could go first.
3. Owner: fix the runner's Application Control block (X-07).
4. The editor checks below, most of all the spring solver comparison: P2.1 changed how springs behave.

### How the merges went

- Each PR was merged (squash) once its PR build was green and it merged cleanly into the current `main`. When `main` had changed files the PR also touched, the PR was rebuilt on top of `main` first.
- **Three PRs needed fixes their first build exposed:**
  - #104: the header script's exit code.
  - #103: it used a symbol from another unmerged PR.
  - #109: a shadowed local variable in the new test.
- **Stacked PRs (#107, #108, #109, #116)** were moved onto `main` after their bases merged. With squash merges, the old base branch's final commit has to be merged in before `main` (X-07).
- **Draft PRs** can't be merged through the API (405). Mark a PR ready for review before merging it.
- **#98** (the owner's UE 5.7/5.8 PR, not part of this plan) still needs `main` merged in. #104 and #112 both changed `fab-plugin-build.yml`.

### Results so far, and what changed because of them

- **The first CI runs found problems nobody had seen:**
  - the header step always failed (rule 12);
  - six tests failed on `main` (X-05);
  - #103 didn't compile on its own (X-08);
  - the VMC tests never ran (X-07).

  All are fixed or fixed in this PR. The general lesson is that code that has never been compiled or run needs a fix round once it is.
- **New rules:** B.0 rules 10 to 13. New task: P0.5.
- **Runner:** one unexplained cancellation (X-07), not seen again. The Application Control block (error 4551) has now hit four builds (#115, #121 twice, #123). Re-runs cleared it each time. It did not recur on #125 to #127, but nothing has changed on the runner, so expect it again (X-07).
- **Model the expected numbers before the first CI run:** for P2.1 a short Python model of the solver set the test thresholds, and the one failure was a test measuring a frame the model wasn't checked on. Each CI round trip costs minutes; a local model catches most threshold mistakes first.
- **Check new editor warnings against the plugin's own content:** P2.3's first version warned on every compile of the plugin's template AnimBlueprint. CI caught it only because the test run compiles that asset and reports "passed with warnings". Read the test summary's warning count, not only pass/fail.
- **Tests that exercise real engine behaviour pay off:** the P1.14 node tests found an LOD crash already on `main`, and the P1.15 test showed that `UAssetImportData` doesn't round-trip a source path, which would have made the new matching reject every mesh. Both came from running code in the editor rather than from reading it. Where a task's acceptance can be run in a test (even with a skeleton or mesh built in code), do that rather than leave it to an editor check.
- **Spring data moved to parse time:** after P1.11 the parser returns UE axes and centimetres. The pipeline doesn't touch geometry, so there is one place to check.
- **Ordering slip:** P1.11 went in before P1.16, against the dependency note on P1.16. The new code is correct, but existing assets are not flagged. Lesson: before starting a task, check the "Must land with or before" notes of other tasks as well as the task's own "Depends on" line. P1.16 landed next (#118) and now flags those assets.
- **Stacking to avoid conflicts:** P1.12 touched the same parser as P1.13, so it was branched from P1.13's PR and moved onto `main` after that merged. That worked, but the merge produced a duplicate function without reporting a conflict (X-07). B.0 rule 13 now requires a diff against `main` before pushing a merged branch.

### Verification still owed

- **In the editor (P2.1 to P2.3):** compare springs against UniVRM or three-vrm on the same model and motion, and record GIFs; stiffness changed the most. Walking or turning a character should swing hair and skirts without External Velocity. A spring with a `center` should not lag when its center moves. Compile an AnimBlueprint that uses another character's spring data: the compiler lists the missing bones. Check whether edits to the node in the AnimBlueprint editor reach the preview. Record Insights timing for a large model (P2.2).
- **In the editor (P1.10):** a lit sphere with an imported normal map shows correct bumps, and the texture assets have the expected settings.
- **In the editor (P1.14, P1.15):** in PIE, swap spring data on a running character, teleport it and change LODs: no crash, no stretched chains. Import `Alice.vrm` and `Alice2.vrm` into the same folder: each character's spring data, IK Rig and Live Link assets bind to its own mesh. Untick "Generate Spring Bone Data" with the project setting on: no spring data.
- **In the editor:** import real VRoid VRM 0.x and 1.0 models (P1.7, P1.8, P1.9, P1.11 to P1.13). Check that both face +Y like the mannequin, and that spring colliders sit on the right body parts (`vrm.SpringBones.DrawColliders 1`). On VRM 0.x, whole hair strands should move, not only their root bone (P1.12). On VRM 1.0, the tip joints of a chain should move more freely than the roots when the file gives them lower stiffness (P1.13). Open a spring data asset imported before #115: it should show **Needs Reimport** and warn when its AnimBlueprint compiles (P1.16). Also receive from a real VMC sender (P1.1, P1.3, P1.5), using `scripts/vmc_sender.py` or VSeeFace. These are not covered by automated tests.
- **[Verify] items not yet confirmed:**
  - VMC-01: the `Root/Pos` layout, checked against a real sender.
  - T-02: one skin per mesh in VRoid exports.
  - T-05: VRM 0.x vs 1.0 facing. P1.9 relied on three-vrm's `rotateVRM0` rather than an editor import; confirm it with the import above.

---

# Appendix

## Appendix A: VMC message reference (for P1.1, P3.1, P4.7)

| Address | Arguments (per spec; **[Verify]** against protocol.vmc.info) | Current handling |
|---|---|---|
| `/VMC/Ext/OK` | int loaded [, int calibrationState, int calibrationMode [, int trackingStatus]] | Ignored |
| `/VMC/Ext/T` | float time | Ignored |
| `/VMC/Ext/Root/Pos` | string name, float p.xyz, float q.xyzw [, float s.xyz, float o.xyz] | Only 7 floats accepted (VMC-01) |
| `/VMC/Ext/Bone/Pos` | string name, float p.xyz, float q.xyzw | Supported. Parent assignment is wrong (VMC-03/04) |
| `/VMC/Ext/Blend/Val` | string name, float value | Supported. Values zeroed each Apply (VMC-11) |
| `/VMC/Ext/Blend/Apply` | (none) | Supported. Push ordering is wrong (VMC-12) |
| `/VMC/Ext/Cam` | string name, float p.xyz, float q.xyzw, float fov | Ignored |
| `/VMC/Ext/Hmd/Pos`, `/Con/Pos`, `/Tra/Pos` | string serial, float p.xyz, float q.xyzw | Ignored |

## Appendix B: Coordinate conventions (for P1.11)

- **Unity (VMC stream):** left-handed, Y-up, Z-forward, metres. The current source mapping is `UE = (-x, z, y) * 100` and `q = (-qx, qz, qy, qw)`. That is a proper rotation (determinant +1), consistent with a character facing +Y in UE. Keep it, and document it with a test.
- **glTF (VRM file):** right-handed, Y-up, metres. VRM 1.0 faces +Z. VRM 0.x faces −Z (three-vrm `rotateVRM0`; editor check owed). P1.9 applies a 180° yaw about UE Z to VRM 0.x, so every version faces UE +Y. The current translator's net mapping is `(x, y, z) → (x, z, y)` (a reflection, as needed for the handedness change). The spring parser's old gravity mapping `(x, y, z) → (z, x, y)` disagreed with it (T-13). P1.11 replaced it; `VRMCoordinateConversion.h` is now the only mapping.
- **Rule for spring geometry:** convert every geometric quantity with the **same** function as the mesh, after rotating node-local vectors by the node's original global rest rotation (because imported bones have identity rest rotation).

## Appendix C: Finding-to-task index

| Finding | Task(s) | Finding | Task(s) |
|---|---|---|---|
| VMC-01 | P1.1 | T-01, T-02, T-09 | P1.7 |
| VMC-02 | P1.2 | T-03, T-04 | P1.8 |
| VMC-03, VMC-04 | P1.3 | T-05 | P1.9 |
| VMC-05 | P1.4 | T-06 | P1.10 |
| VMC-06 | P3.2 | T-07 | P4.5 |
| VMC-07, VMC-08 | P1.6, P4.2 | T-08 | P4.1, P4.2, P4.6 |
| VMC-09, VMC-10 | P3.2 | T-10, T-11 | P3.3, P5.4–P5.7 |
| VMC-11, VMC-12 | P1.5 | T-12 | P1.18, P1.9 |
| VMC-13, VMC-14, VMC-15 | P3.1, P5.1 | T-13 | P1.11 |
| VMC-16, VMC-17 | P3.1, P6.1 | T-14 | P0.4, P1.18 |
| VMC-18 | P1.18 | SP-01 | P1.11 |
| VMC-19 | P4.7 | SP-02 | P1.12 |
| VMC-20 | P1.1, P4.7 | SP-03, SP-04, SP-05, SP-08 | P1.13 |
| VMC-21 | P3.5 | SP-06 | P1.13, P3.3 |
| VMC-22 to VMC-24 | P3.2, P6.2 | SP-07 | P1.16 |
| VMC-25 | P3.2 (cleanup) | SR-01 to SR-03, SR-07 | P2.1 |
| VMC-26 | P0.3, P3.1 | SR-04, SR-05 | P1.14 |
| PE-01 | P1.15, P6.3 | SR-06 | P2.2 |
| PE-02 | P1.15, P3.4 | SR-08 | P1.18, P2.3 |
| PE-03, PE-04 | P1.17, P3.4 | X-01 | P3.3, P4.1–P4.4 |
| PE-05 | P4.3 | X-02 | P0.1 |
| PE-06 to PE-08 | P3.4 | X-03 | P0.2, P0.3, per-task tests |
| PE-09, PE-10 | P1.18, P3.3, P3.5 | X-04 | P7.1–P7.5b |
| | | X-05 | #110 (done), B.0 rule 11, P1.13 |
| | | X-06, X-07 | P0.5 (X-07 Application Control: owner, if it recurs) |
| | | X-08 | B.0 rule 10 |
| | | X-07 (silent bad merge) | B.0 rule 13 |
