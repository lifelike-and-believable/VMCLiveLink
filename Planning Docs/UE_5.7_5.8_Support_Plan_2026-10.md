# UE 5.7 and 5.8 support plan

October 2026. Goal: both plugins build, pass their tests and ship on Fab for UE 5.6, 5.7 and 5.8 (Win64), from one source tree on `main`.

This plan is based on building and testing the code at `60407da` against the engines installed on the CI runner machine: UE 5.6.1, 5.7.4 and 5.8.2. [Section 1](#1-what-the-probe-found) gives the results, [section 2](#2-decisions-for-the-owner) the decisions needed, and [section 3](#3-work) the work, as pull requests with acceptance criteria.

## 1. What the probe found

Each plugin was built with `RunUAT BuildPlugin`, which is what Fab and `scripts/build_fab.ps1` run: Editor, plus Game in Development and Shipping, with the engine's latest include order. Then the test project was built against each engine and the `VRM.+VMC.` tests were run headless, as CI runs them. The fixes in 1.1 were applied to copies of the code so the builds could continue past the first failure.

| | UE 5.7.4 | UE 5.8.2 |
|---|---|---|
| VMC Live Link, as is | 9 errors (3 files) | 9 errors (3 files) |
| VRM Interchange, as is | 1 error; more hidden behind it | 3 errors; more hidden behind them |
| Both, with the fixes in 1.1 | build | build |
| Plugin deprecation warnings (C4996) | 0 | 6 places (1.2) |
| Third-party `cgltf.h` CRT warnings (C4996) | 13 | 13 |
| Project targets as they are | fail (1.4) | fail (1.4) |
| `VRM.+VMC.` tests | 113 passed, 1 failed (flaky, 1.3) | 112 passed, 2 failed (1.3) |
| Editor target without unity (CI's build) | builds, with the fixes | builds, with the fixes |
| Plugins load with the committed `.uplugin` (`EngineVersion` 5.6.0) | not tried; same code as 5.8 | no: skipped (1.4) |

VMC Live Link needs no engine-specific code: three missing includes. VRM Interchange needs those kinds of fixes too, plus three changes that differ by engine (5.7's material resource lookup, and 5.8's Interchange joint nodes and skinned-mesh check). And both plugins' descriptors name engine 5.6.0, which makes 5.7 and 5.8 refuse to load them from source (1.4).

### 1.1 Compile errors

The newer engines include fewer headers transitively, and 5.8 treats an export macro in the wrong place as an error.

| File | Error | Fix | 5.6 |
|---|---|---|---|
| `VMCLiveLink/Private/VMCUdpReceiver.cpp`, `VRMInterchange/Private/VRMImportMessages.cpp` | `FPlatformTime` undeclared | include `HAL/PlatformTime.h` | same code |
| `VMCLiveLink/Private/VMCLiveLinkRemapper.cpp` | `FAssetData` undefined | include `AssetRegistry/AssetData.h` | same code |
| `VMCLiveLink/Private/Tests/VMCMappingAssetTests.cpp` | `NewObject` no overload (`GetTransientPackage` undeclared) | include `UObject/Package.h` | same code |
| `VRMSpringBonesRuntime/Private/AnimNode_VRMSpringBones.cpp` | `TAutoConsoleVariable` undeclared | include `HAL/IConsoleManager.h` | same code |
| `VRMInterchange/Public/VRMSpringBonesValidation.h:14` | C4091, `VRMINTERCHANGE_API struct FVRMValidationResult` | `struct VRMINTERCHANGE_API FVRMValidationResult` (the macro was ignored, so the struct was never exported) | same code |
| `VRMInterchangeEditor/Private/Tests/VRMMToonMaterialTests.cpp:41` | `GetMaterialResource` takes an `EShaderPlatform` from 5.7 (a feature level in 5.6) | `GMaxRHIShaderPlatform` from 5.7 | guard at 5.7 |

"Same code" means the fix compiles on 5.6 unchanged; it hasn't been built on 5.6 yet (U1 does that).

### 1.2 Deprecations in 5.8

None of these are errors yet. Each warning says the code will stop compiling in the next release.

| Where | Deprecated | Replacement | Available from |
|---|---|---|---|
| `VRMInterchangeModule.cpp:17,28`, `VRMInterchangeEditorModule.cpp:55,73` | `FCoreDelegates::OnPostEngineInit` | `FCoreDelegates::GetOnPostEngineInit()` | 5.8: guard |
| `VRMMToonMaterial.cpp:278-279`, `VRMMToonMaterialTests.cpp:80-81` | `UMaterial::bUsedWithSkeletalMesh`, `bUsedWithMorphTargets` | `GetUsageByFlag` to read (exported in all three engines, so the test needs no guard). To write, `SetUsageByFlag` (exported only in 5.8; in 5.7 the plugin can't link to it). | reads: same code; writes: guard at 5.8, keeping the flags below it |
| `VRMTranslator.cpp:118-119,133-134` | `UInterchangeSceneNode::SetCustomBindPoseLocalTransform`, `FSceneNodeStaticData::GetJointSpecializeTypeString` | Create joints as `UInterchangeJointNode` and call `SetBindPoseLocalTransform` | 5.8: guard (1.3) |

Writing the material usage flags: `SetMaterialUsage(bool&, EMaterialUsage)` exists in all three engines, but in 5.6 it is meant for a material in use and can queue a recompile, and in 5.8 it is a compatibility wrapper. The generated master materials are built in code before they are compiled, so keep writing the flags directly below 5.8 and use `SetUsageByFlag` from 5.8.

The `cgltf.h` warnings (`fopen`, `strcpy`, `strncpy`) appear from 5.7 and come from the third-party header. `cgltf.h` already defines `_CRT_SECURE_NO_WARNINGS`, too late: the CRT headers came in earlier through the PCH. Wrap the implementation include (in `VRMCore` only) in `THIRD_PARTY_INCLUDES_START` / `THIRD_PARTY_INCLUDES_END`, which disables C4996 in all three engines. Don't edit the header.

### 1.3 Test failures

**5.8, `VRM.Materials.Translate`: affects every VRM import.** On 5.8 the deprecated `SetCustomBindPoseLocalTransform` is a stub: it logs an error and stores nothing (`InterchangeSceneNode.cpp:527-531`), and 5.8's skeleton and mesh code reads bind poses only from `UInterchangeJointNode`. So on 5.8 every VRM import reports an error in the message log and the import notification, and its skeleton is built without the bind poses the file gives. The test caught the error because it fails on logged errors; no test caught the missing bind pose, so the current tests don't check it. The fix is the joint-node change in 1.2: from 5.8, `VRMTranslator` creates the root joint and each bone as `UInterchangeJointNode` and sets their bind pose with `SetBindPoseLocalTransform`. This is the change most likely to alter what a 5.8 import produces. A test of the bind pose (U1) and an editor check (U6) confirm the skeleton, bind pose and spring bones import as on 5.6.

**5.8, `VRM.Pipeline.ActorWiring.ConstructionScript`: affects imports that make an actor Blueprint.** `VRMPipeline::SetActorBlueprintMesh` (`VRMActorBlueprintWiring.cpp:115-116`) writes `SkeletalMeshAsset` and `SkinnedAsset` straight to the component template, on purpose (a template isn't registered, so the setters' runtime work doesn't apply). 5.8 added a check that the skinned asset isn't changed behind the component's back, and it fires an ensure: `KnownSkinnedAsset == CurrentSkinnedAsset`. `NotifyIfSkinnedAssetChanged()`, which would record the change, is new in 5.8 and `protected`, so the plugin can't call it, and `USkeletalMeshComponent::RefreshSkeletalMeshAsset()` (which calls it) isn't exported. A candidate, guarded at 5.8: after the writes, call the template's `PostEditChangeProperty` with the `SkinnedAsset` property, which is public and calls the notify first (`SkinnedMeshComponent.cpp:1636-1638`); the module is editor-only. Done in U1 (#192): the call is safe on an unregistered template (no construction script rerun, no propagation to instances), and the test passes on 5.8.

**5.7, `VMC.Diagnostics.Source` ("VMC.Stats counts packets"): intermittent.** It failed once in the full run and passed in two runs on its own, and passed on 5.8. On 5.6 it passed 10 runs out of 10 on its own (one editor launch per run; each run's report overwrote the last, so only the console output of that loop records all ten). Failing only in the full run points to timing under load. The test sends UDP packets and checks the counter; it likely reads the counter before the receive thread has counted them. Three engines means three times as many runs, so a flaky test turns CI red three times as often: it gets its own fix (U4) before the CI matrix is required.

### 1.4 Build configuration

- **The plugins' `EngineVersion` must go from the source descriptors.** Both `.uplugin` files say `"EngineVersion": "5.6.0"`. 5.7 and 5.8 treat that as an incompatible plugin: the editor asks whether to load it, and under `-unattended` the answer is No, so both plugins are skipped and the tests find nothing to run (tried on 5.8: "Skipping load of 'VRMInterchange'", "No automation tests matched"). A user who copies the source into a 5.7 project gets the same prompt. The probe missed it at first because its copies set `EngineVersion` per engine. Remove the field from the source descriptors; `build_fab.ps1` already writes it into each package. The project's `EngineAssociation` of 5.6 doesn't matter here: 5.8 ran the tests on the project with it unchanged.
- **The project targets fail on 5.7 and 5.8.** `Source/VMCLiveLinkProject*.Target.cs` pin `DefaultBuildSettings = BuildSettingsVersion.V5` and `IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6`. On 5.7, with an installed engine, UBT refuses V5 because it changes `UndefinedIdentifierWarningLevel` for a target that shares build products with `UnrealEditor`. Two ways to fix it ([D-10](#2-decisions-for-the-owner)):
  - `BuildSettingsVersion.Latest` and `EngineIncludeOrderVersion.Latest`. These are V5 and `Unreal5_6` in 5.6, so the 5.6 build is unchanged. This is what the probe used.
  - Pin per engine with the defines UBT gives rules files (`#if UE_5_7_OR_LATER`). A future engine then can't change the settings without a code change.
- **The plugins must compile under each engine's latest include order.** BuildPlugin and Fab use it, whatever the project's targets say. CI builds the project targets today, so it would miss that: U2 adds a BuildPlugin step.
- **Long paths.** The probe's first attempt failed in a deep folder: from 5.7, BuildPlugin's intermediate paths exceed 260 characters when the package folder is about 150 characters deep. The runner's work folder is shallower, but `build_fab.ps1` puts `<Plugin>-UE5_8_0-Packaged\HostProject\Plugins\<Plugin>\Intermediate\...` under it. U3 checks the deepest path and shortens the stage folder names if needed.

### 1.5 Content

Every tracked `.uasset` and `.umap` (83 files, both plugins and the test project) is saved at UE5 package file version 1017, the newest 5.6 writes. 5.7 and 5.8 write 1018 (`IMPORT_TYPE_HIERARCHIES`), and 5.6 can't load a package saved at 1018. The 5.6 assets loaded and passed the tests in 5.7 and 5.8 unchanged.

So the content must stay saved from 5.6, the oldest supported engine. Opening the project in 5.7 or 5.8 and saving (or letting the editor re-save on load) breaks 5.6 for that asset. The riskiest are the `DefaultPipelines` assets: since PR #189 they apply the project settings when they load, so an editor session can mark them dirty. U2 makes this a CI check.

## 2. Decisions for the owner

All five were decided on 2026-10-07, as recommended.

| ID | Decision | Recommendation |
|---|---|---|
| D-9 | Amend D-2 ("Engine versions stay 5.6"): support 5.6, 5.7 and 5.8. | **Decided 2026-10-07: as recommended.** Yes. The cost measured above is small, and 5.6 can be dropped later without code changes beyond deleting guards. Proposed rule: support the three newest engine releases; when a new one is added, drop the oldest (and re-save content from the new oldest). |
| D-10 | Project target settings: `Latest`, or pinned per engine. | **Decided 2026-10-07: as recommended.** `Latest`. It keeps 5.6 identical and needs no edit per engine. A future engine that breaks the build shows up in CI, which is where it would be fixed anyway. |
| D-11 | Fab: one listing per plugin with a package per engine (three uploads each), or support only 5.7 and later on Fab. | **Decided 2026-10-07: as recommended.** A package per engine. Fab takes one package per engine version on the same listing, and `build_fab.ps1` already makes one per engine. |
| D-12 | CI cost: all three engines on every PR (about three times today's 6 minutes, the runner runs jobs one at a time), or the oldest engine fully and the others plugin-only. | **Decided 2026-10-07: as recommended.** All three on every PR, at first. The test failures above only showed up by running the tests on each engine. Reconsider if the runner queue becomes a problem. |
| D-13 | Required checks on `main`. | **Decided 2026-10-07: as recommended.** Keep `build-and-test` as the only required check, made an aggregator of the engine jobs (U2), so the branch protection rule doesn't change. |

## 3. Work

Each item is one pull request, in this order. Each follows CONTRIBUTING (review, CI green, CHANGELOG, docs).

### U1. Source compatibility

All of 1.1, 1.2 and 1.3 except the flaky test, and the target change from 1.4.

- The includes and the export macro move.
- `EngineVersion` removed from both source `.uplugin` files (1.4).
- Engine guards with `UE_VERSION_NEWER_THAN_OR_EQUAL(5, 7, 0)` / `(5, 8, 0)` from `Misc/EngineVersionComparison.h`, for: the test's `GetMaterialResource`, `OnPostEngineInit`, writing the material usage flags, the joint nodes in `VRMTranslator`, and the skinned-asset notification in the actor wiring (1.3). Each guard has a comment naming the engine change, so it can be deleted when that engine becomes the oldest.
- `THIRD_PARTY_INCLUDES_START` / `_END` around the cgltf implementation in `VRMCore`.
- `Target.cs` as decided in D-10.
- A test that the translator gives every bone as a joint, with its parent and bind pose, in the form the running engine reads (`VRM.Skeleton.Translate`; the current tests passed on 5.8 without bind poses, so they don't check it). No test builds the skeleton from an import (that needs an import harness the tests don't have), so U6 checks the imported skeleton and bind pose in the editor.

Acceptance:
- CI (5.6) green, with `0 passed with warnings`.
- Built locally on 5.7 and 5.8, the CI way (Editor without unity, Game Development and Shipping) and with BuildPlugin. `VRM.+VMC.` pass on both with no warnings, and the 5.8 build has no C4996 from plugin code. Recorded in the PR, since CI doesn't build them yet.
- The source `.uplugin` files have no `EngineVersion` (the packages set it, U3), and `EngineAssociation` stays `5.6`: the project is opened with the oldest engine.
- The 5.7 and 5.8 test runs used the descriptors as committed, with no edits to the copies.

### U2. CI on three engines

**Done in the U2 pull request, with three changes from the list below.** (1) No per-engine checkout folders: CI already rebuilds the plugins from a clean checkout in about 6 minutes per engine, and keeping folders between runs (`clean: false`) would let files deleted on a branch linger and be compiled. (2) No BuildPlugin step: its purpose was to build with each engine's latest include order, which the project targets now do (`Latest`, D-10), and on the runner BuildPlugin inherits IncrediBuild's intermittent refusals (U1). (3) The content check is a step of the oldest engine's job, not a job of its own, so it needs no checkout of its own.

`pr-build.yml`:
- A matrix job per engine (`5.6`, `5.7`, `5.8`), each building and testing as today. Each engine gets its own checkout folder (`actions/checkout` with `path: ue-5.7` and so on, and `clean: false`, since the default `git clean -ffdx` deletes `Intermediate` and `Binaries`; the steps use that folder as `working-directory`), so builds stay incremental. Sharing one folder would rebuild everything each time the engine changes.
- Each engine job keeps today's job-level `if:` (the fork guard and the title-edit skip), so no fork code runs on the runner and a title edit doesn't start three builds. Its concurrency group includes the engine (`pr-build-<PR>-${{ matrix.engine }}`), or the three legs would cancel each other. Its test report artifact is named per engine (`automation-report-5.7`), or the second upload fails with a name conflict.
- A BuildPlugin step per engine for each plugin, so the latest include order is built (1.4). Build into a short path.
- A content version check: a small script reads the package file version from the header of every tracked `.uasset` and `.umap` and fails on anything newer than 5.6 writes (UE5 version 1017), naming the files. Runs once, not per engine, as its own job with the same `if:`. Also usable locally.
- `build-and-test` becomes an aggregator: it `needs` the engine jobs and the content check, with `if: ${{ !cancelled() && (<today's condition>) }}`. Without `!cancelled()` the implicit `success()` would skip it when an engine fails, and a skipped required check counts as passing, so a red PR could merge. Its first step fails unless every `needs.<job>.result` is `success`. Its concurrency group is its own. It posts the one success comment, with each engine's test counts read from the downloaded per-engine report artifacts (job outputs and `GITHUB_ENV` don't carry three matrix legs' values). One comment, not three, so the Auto-fix monitor wakes once. The required check keeps its name, so branch protection doesn't change.
- The engine roots in one place (a matrix list), and CONTRIBUTING's CI runner section lists the three engine folders.

Acceptance: a PR shows three engine jobs, the content check and `build-and-test`; deliberately breaking 5.8 only (a temporary guarded `#error`, reverted before merge) makes `build-and-test` fail, not skip, and GitHub refuses the merge; a second push cancels only the superseded legs; the success comment lists three engines' counts; a docs-only edit to the PR title still skips as today; the content check fails on a 5.7-saved asset (tried locally on a copy).

### U3. Fab packages for three engines

**Done in the U3 pull request.** The release is made through the REST API (the `create-release` and `upload-release-asset` actions are archived), as a draft until every zip is uploaded; the zip step checks each package's `EngineVersion`; and `build_fab.ps1` turns off IncrediBuild for UnrealBuildTool through its environment override `UnrealBuildTool_BuildConfiguration__bAllowXGE` (IncrediBuild on the build machine refuses intermittently, and BuildPlugin passes no `-NoXGE` on). Path lengths: the runner's work folder is about 50 characters, against the probe's 150 that overflowed; the PR records the manual run on the runner.

- `fab-plugin-build.yml`: `ENGINE_ROOTS` and `ENGINE_VERSIONS` list the three engines (`5.6.0,5.7.0,5.8.0`). `build_fab.ps1` already builds and zips one package per engine with its `EngineVersion` set.
- The "Prepare zip files" step assumes one engine: it names the combined folder from the first version only and extracts every engine's zip of a plugin into the same folder, so the last one wins. Group by engine: one combined zip and one zip per plugin for each engine, named with the engine (`VMCLiveLink_UE5_7_Fab.zip` already is).
- The steps after it hard-code three timestamped zip names: the artifact upload (with `if-no-files-found: error`) and the three release-asset steps. They take the per-engine set.
- Check the deepest packaged path stays under 260 characters (1.4).
- Run the workflow manually and install each package into a clean project on its engine (an editor check, U6).

Acceptance: a manual run produces nine zips (two plugins and the combined package, for each of three engines), each `.uplugin` has its engine's `EngineVersion`, and each package loads in its engine.

### U4. Make `VMC.Diagnostics.Source` deterministic

Wait for the receive thread to count the packets (poll the stats with a timeout, or count synchronously in the test) instead of reading once. Acceptance: 20 runs in a row pass on each engine.

This can land before U2, and should: U2's matrix triples its runs.

### U5. Documentation

- README, User Guide, CONTRIBUTING (requirements, the build commands' engine folder, the CI runner's engines, Fab), ARCHITECTURE, EDITOR_TESTS, both plugin READMEs (and their 5.6 links to Epic's docs), FAB_MARKETPLACE_READINESS: "UE 5.6, 5.7 and 5.8".
- CONTRIBUTING code guidelines: check engine APIs in every supported engine, not just 5.6; the guard convention; content is saved from the oldest engine (and why, and what the CI check does); open the project with the oldest engine.
- REVIEW.md and `.claude/agents/code-reviewer.md`: the same three checks in the review checklist.
- `Planning Docs/Code_Review_and_Refactor_Plan_2026-09.md`: D-2 marked amended, pointing here.
- CHANGELOG: under Added.

Can go in with U2 or U3 if small.

### U6. Editor checks and release

On 5.7 and 5.8, with the Fab packages from U3 installed in a clean project:
- Import a VRM 0.x and a VRM 1.0 avatar (VRoid Studio's, with spring bones): no errors in the import's message log page; skeleton, materials, morph targets, avatar description, IK Rig and post-process AnimBP as in 5.6.
- Spring bones move as in 5.6, with Use Spring Centers on and off.
- A VMC stream (`scripts/vmc_sender.py send`) drives the avatar through Live Link and the retarget actor.

Add these as engine columns to the relevant rows of `docs/EDITOR_TESTS.md` and record the results there. Then release as 1.1.0 (both plugins), tagging `release/1.1.0`, which runs U3's workflow.

## 4. Rules from now on

- **Content is saved from the oldest supported engine.** Open the project with it. CI rejects anything newer.
- **Engine differences go behind `UE_VERSION_NEWER_THAN_OR_EQUAL` guards,** with a comment naming the change, in the one place the difference is. No source branches per engine.
- **New engine:** probe it the way this plan did (BuildPlugin and the tests), add it to the CI matrix and the Fab list, and drop the oldest (D-9): remove its guards and re-save content from the new oldest engine.

## 5. Not verified yet

- ~~The fixes in 1.1 and the `Target.cs` change haven't been built on 5.6.~~ Done in U1 (#192): built and tested on 5.6, 5.7 and 5.8.
- ~~The actor wiring fix (1.3) and the bind-pose test are proposals.~~ Done in U1 (#192). The test covers the translator's joint nodes; no test builds a skeleton from an import, so the imported skeleton and bind pose are checked in the editor (U6).
- The CI runner: the runner folder at `C:\actions-runner` on this machine is registered to an older repository location, so confirm the machine that runs this repository's jobs has UE 5.7 and 5.8 at `C:\Program Files\Epic Games\UE_5.7` and `UE_5.8` before U2.
- The 5.7 and 5.8 builds were the plugins' BuildPlugin builds and the project's Editor target (with and without unity). The Game targets were built only through BuildPlugin.
- Whether Fab still requires anything per engine beyond `EngineVersion` (for example, an engine-specific listing field) wasn't checked.
- Nothing ran in an interactive editor: U6.
