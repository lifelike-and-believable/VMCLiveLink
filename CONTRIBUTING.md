# Contributing

This repository holds two Unreal Engine plugins, [VMC Live Link](Plugins/VMCLiveLink/README.md) and [VRM Interchange](Plugins/VRMInterchange/README.md), and a small project (`VMCLiveLinkProject.uproject`) to build and test them in. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) explains how they are built.

## Getting started

### Requirements

- Windows 10 or 11 (the plugins are built and tested on Win64 only).
- Unreal Engine 5.6.
- Visual Studio 2022 with the *Game development with C++* workload, as the engine requires.
- [Git LFS](https://git-lfs.github.com/): the `.uasset` files and the other binary types listed in `.gitattributes` are stored with it. (The small VRM fixtures and VMC captures are ordinary files.)
- Python 3 for the scripts in `scripts/` (standard library only), and gcc or clang on `PATH` for `scripts/check_vrm_fixtures.py` (MSVC's `cl` isn't supported).

### Clone

```bash
git clone https://github.com/lifelike-and-believable/VMCLiveLink.git
cd VMCLiveLink
git lfs pull
```

### Build

Right-click `VMCLiveLinkProject.uproject` and choose **Generate Visual Studio project files**, then build the `VMCLiveLinkProjectEditor` target (Development Editor, Win64), or open the project and let the editor build the plugins.

To build what CI builds, from a command prompt (set `UE` to your engine folder):

```bat
set UE=C:\Program Files\Epic Games\UE_5.6
"%UE%\Engine\Build\BatchFiles\Build.bat" VMCLiveLinkProjectEditor Win64 Development -Project="%CD%\VMCLiveLinkProject.uproject" -WaitMutex -DisableUnity
"%UE%\Engine\Build\BatchFiles\Build.bat" VMCLiveLinkProject Win64 Development -Project="%CD%\VMCLiveLinkProject.uproject" -WaitMutex
"%UE%\Engine\Build\BatchFiles\Build.bat" VMCLiveLinkProject Win64 Shipping -Project="%CD%\VMCLiveLinkProject.uproject" -WaitMutex
```

If IncrediBuild (XGE) is installed but not licensed, a build can fail at once with `Result: Failed (OtherCompilationError)` and no compiler error; add `-NoXGE` to build locally. A build that fails at once with `IOException: ... being used by another process` is waiting on another Unreal build on the same machine (they share the engine's build rules and UnrealBuildTool's log); retry once it finishes.

The Editor build runs without unity builds (`-DisableUnity`), so every file must include what it uses. The Shipping build has no log category objects and no development tests; code that uses either must compile there too.

### Run the tests

In the editor: open the **Test Automation** window (**Tools → Test Automation**), filter on `VRM.` or `VMC.`, and run.

Headless, as CI runs them:

```bat
"%UE%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%CD%\VMCLiveLinkProject.uproject" -ExecCmds="Automation RunTests VRM.+VMC.;Quit" -unattended -nullrhi -nosplash -log -ReportExportPath="%CD%\TestReport"
```

`+` separates test filters and `;` ends the command. The report in `TestReport\index.json` lists each test's result. A test that logs a warning passes "with warnings". CI fails only on failed tests, so a warning leaves it green: check the `Tests:` line of the **Run automation tests** step (it must read `0 passed with warnings`) and fix any warning before merging. The success comment CI posts on the pull request repeats these counts.

### CI and releases

- **PR Build and Tests** (`pr-build.yml`) runs on every pull request from a branch in this repository when it is opened, reopened or pushed to (whatever its base, so stacked pull requests are built too), and manually via **Actions → PR Build and Tests**. Editing a pull request doesn't run it, and a retargeted pull request keeps its commit's result: push a commit or use Run workflow to rebuild. Pull requests from forks aren't built and fail the check; a maintainer re-opens them from a branch here. (A fork's pull request runs its own copy of the workflow, so this holds only while that copy is unmodified: the repository's approval rule for fork workflows is what keeps fork code off the self-hosted runner.) For each supported engine (UE 5.6, 5.7 and 5.8), a job `build (<engine>)` runs the three builds above and the `VRM.+VMC.` tests, and uploads that engine's test report; the 5.6 job also checks the copyright headers and the content's package versions (below). Then `build-and-test` (on a GitHub-hosted runner) fails unless every engine's job passed, and on success comments on the pull request with the head commit and each engine's test counts (not for a manual run). `main` requires `build-and-test` before merging, so GitHub refuses a merge while any engine is failing or pending (admins can bypass it).
- **Fab Plugin Builds** (`fab-plugin-build.yml`) runs on a `release/*` tag push (e.g. `release/1.0.0`) or manually via **Actions → Fab Plugin Builds**. It checks the headers, builds both plugins against UE 5.6, and produces Fab-ready zips (a combined package plus one per plugin) as artifacts. A tag push also publishes them to a GitHub Release; a manual run only builds the artifacts.
- **Auto-fix Copyright Headers** (`header-autofix.yml`), run manually with a plugin folder and holder text, commits missing or outdated headers on a new branch and opens a pull request. Pull requests it opens don't trigger **PR Build and Tests**: run it on the branch (**Actions → PR Build and Tests → Run workflow**), or push a commit to the branch.

### CI runner

The builds run on a self-hosted Windows runner with UE 5.6, 5.7 and 5.8 at `C:\Program Files\Epic Games\UE_<version>`. The engine jobs run one after another on it, so a pull request takes about three times as long as one engine's build. If a build fails without a code cause:

- **`dubious ownership`, or `Not in a Git repository` in the Git LFS step:** the runner service's account changed (it runs as SYSTEM) and its work folder was created by another account. Delete `C:\actions-runner\_work\VMCLiveLink` on the runner so it is recreated, then re-run the job.
- **Error 4551 (Application Control blocked a DLL):** re-run the job; don't change the code.
- **The job was not started because it repeatedly failed to be acquired:** the runner didn't pick up the job (it was offline or busy). Re-run the job.

### Editor checks

What automated tests can't confirm (behaviour seen in the editor, real VRM files and senders) is in [docs/EDITOR_TESTS.md](docs/EDITOR_TESTS.md). Run the checks a change affects before release, and record the results.

### Test data

- **VRM fixtures** (`Plugins/VRMInterchange/Tests/Fixtures`) are generated by `python scripts/make_vrm_fixtures.py` and checked by `python scripts/check_vrm_fixtures.py`. Don't edit them by hand: change the generator and regenerate. See the fixtures' [README](Plugins/VRMInterchange/Tests/Fixtures/README.md).
- **VMC captures** (`Plugins/VMCLiveLink/Tests/Captures`) are made with `python scripts/vmc_sender.py write --out Plugins/VMCLiveLink/Tests/Captures/<name>.vmcrec`, or recorded from a sender with `record --out <file>`.
- Commit only synthetic data and captures of your own (decision D-8). No test reads third-party avatars or captures; keep them outside the repository.

`scripts/vmc_sender.py send` also streams VMC to a running editor, for trying changes without a real sender.

## Code guidelines

- **Follow the surrounding code:** Unreal's naming conventions (`F`, `U`, `A`, `E`, `T`, `I` prefixes, `b` for booleans), and the comment density and style of the file you are in.
- **Headers.** Every `.h` and `.cpp` starts with the copyright line CI checks:
  ```cpp
  // Copyright (c) YYYY Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
  ```
  `python scripts/add_copyright_headers.py Plugins/VMCLiveLink` (or `Plugins/VRMInterchange`) adds or fixes it, and `--verify` only checks. The **Auto-fix Copyright Headers** workflow does the same on GitHub.
- **No dependency between the plugins.** Each ships on its own and may depend only on plugins that ship with Unreal. Share data through documented conventions, such as the humanoid map metadata.
- **Check engine APIs.** Confirm each UE 5.6 API you use in the engine source or the API reference rather than from memory. Say in the pull request if you couldn't.
- **Logging.** Use the plugin log categories:
  - `LogVRMInterchange` (VRMCore, exported: every VRM module) and `LogVRMSpring` (VRMInterchange, exported: spring parsing and import).
  - `LogVMCLiveLink` (VMCLiveLink only: it is declared in a private header).
  - `LogVMCLiveLinkEditor`, `LogVRMSpringData` and `LogVRMSpringBones` are file-local (`DEFINE_LOG_CATEGORY_STATIC`) in the retarget actor factory, the spring data asset and the anim node. Elsewhere in those modules, use the module's exported category or add one.

  Don't add `LogTemp`. Import problems also go on the import report (`VRM::ImportMessages` collects warnings logged during an import).
- **Threading.** Say in a comment which thread a function or member is used on, when it isn't the game thread. See Threading for [VMC Live Link](docs/ARCHITECTURE.md#vmc-threading) and [VRM Interchange](docs/ARCHITECTURE.md#vrm-threading).
- **Saved data.** A change to a saved struct or class layout, or to what saved data means, adds a custom version entry and a `PostLoad` or `Serialize` upgrade, with a test that loads the old form. See [Asset versioning](docs/ARCHITECTURE.md#asset-versioning).
- **Content is saved from the oldest supported engine (UE 5.6).** An engine can't load a package a newer engine saved, so open the project with 5.6 when changing content, and don't save it from 5.7 or 5.8 (an editor that re-saves on load counts). CI's 5.6 job runs `scripts/check_content_versions.ps1`, which fails on any tracked `.uasset` or `.umap` newer than 5.6 writes; run it yourself after `git lfs pull`.
- **Binary assets.** Prefer code to `.uasset` edits (the IK Rigs and MToon materials are built in code for this reason). If a `.uasset` must change, describe the change in the pull request, since reviewers can't diff it.
- **Tests don't depend on project config.** A test that reads settings compares against the loaded values, or sets what it needs and restores it.
- **Docs travel with code.** A change in behaviour updates the plugin's README, the [User Guide](docs/USER_GUIDE.md) if it covers it, and the root `CHANGELOG.md` in the same pull request.
- **Interchange pipelines.** Each pipeline is registered in the project settings as an asset in `Plugins/VRMInterchange/Content/DefaultPipelines`: Interchange instantiates only pipeline assets, not classes. A new pipeline needs one.

## Submitting changes

1. **One task per branch and pull request.** Keep the diff to what the change needs.
2. **Build and test** as above: the Editor target without unity, the Game target in Development and Shipping, and the `VRM.+VMC.` tests with no failures or warnings. CI does this on every supported engine; locally, at least on the engine your change concerns.
3. **Review your diff** against the checklist in [REVIEW.md](REVIEW.md): threading, lifetimes, saved data, unity-build collisions, Shipping-only breaks, and tests that could pass without testing anything.
4. **Open the pull request** against `main`. Describe what changed and why, what you verified, and anything you couldn't verify (such as behaviour only visible in the editor).
5. **Review.** Every pull request is reviewed against `REVIEW.md` before it is merged. In this repository an automated reviewer (`.claude/agents/code-reviewer.md`) reviews each pull request; its findings, and what was done about each, are recorded on the pull request (in its description or a comment). Each finding must be fixed, or answered there, before merging. A blocking finding whose fix changes code gets a second review.
6. **CI** (`PR Build and Tests`, check `build-and-test`) must pass on the latest commit: header and content checks, the three builds and the tests, on every engine. Branch protection on `main` requires that check. It doesn't catch warnings: each engine's `Tests:` line, repeated in CI's success comment on the pull request, must read `0 passed with warnings`.
7. **Merge** by squash, once CI is green and every review finding is resolved. Branch protection doesn't require the branch to be up to date with `main`, so after merging `main` in, do the check below.

If you merge `main` into your branch, run `git diff origin/main HEAD --stat` before pushing and check that it shows only your change. Git can merge two identical additions into a duplicate without reporting a conflict.

## Reporting issues

Report bugs and request features on [GitHub Issues](https://github.com/lifelike-and-believable/VMCLiveLink/issues). For a bug, include:

- the engine version and which plugin;
- what you did, what you expected and what happened;
- the output log (`Saved/Logs`), filtered on the plugin's categories (`LogVMCLiveLink*` or `LogVRM*`);
- for VMC problems, the sender and its version, and the output of the `VMC.Stats` console command;
- for import problems, the import's page in the Message Log (the *VRM Import* listing) and, if you can share it, the `.vrm` file.

## License

The code in this repository is Copyright (c) Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved. Talk to the maintainers before contributing code, so the terms of the contribution are agreed first.
