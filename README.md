# VMCLiveLink

**Real-time performance meets Unreal Engine.**

Two Unreal Engine plugins for driving digital characters live, from VTubers to XR performers to virtual production:

- **[VMC Live Link](Plugins/VMCLiveLink/README.md)** receives the VMC (Virtual Motion Capture) protocol from apps such as VSeeFace and VirtualMotionCapture and streams it into Unreal's Live Link: body, face, tracked devices and camera.
- **[VRM Interchange](Plugins/VRMInterchange/README.md)** imports VRM avatars (`.vrm`, VRM 0.x and 1.0) with toon materials, expressions, spring bone physics for hair and clothes, an IK Rig for retargeting, and a ready-to-place Live Link character.

Together they take a VRoid or other VRM avatar from file to a live, performer-driven character in a few minutes. Each also works on its own.

**New here? Start with the [User Guide](docs/USER_GUIDE.md).**

## Quick start

1. Copy `Plugins/VMCLiveLink` and `Plugins/VRMInterchange` (from a Git LFS clone, see [Getting the source](#getting-the-source)) into your project's `Plugins` folder and open the project. Let Unreal rebuild the plugin modules; that needs Visual Studio 2022 with the C++ workload.
2. When the editor offers to register the VRM import pipelines, click **Register**.
3. Drag a `.vrm` file into the Content Browser and import it.
4. Open **Window → Virtual Production → Live Link** and add a **VMC Live Link Source**. Point your VMC sender at this computer, port 39539, and start it.
5. Once data arrives, select the `VMC_Subject` subject, set its remapper's **Reference Skeleton** to the imported mesh and click **Map Bones From Humanoid Metadata**.
6. Place the imported `BP_LL_VRM_<name>` actor, set its **Subject** to `VMC_Subject`, and press **Simulate**.

The [User Guide](docs/USER_GUIDE.md) walks through each step, then facial expressions, spring bones, retargeting and troubleshooting.

## Requirements

- Unreal Engine 5.6, 5.7 or 5.8.
- Windows (Win64). It is the only platform the plugins are built and tested on; Mac and Linux can be added once they are built and tested there.

## Documentation

| Document | For |
|---|---|
| [User Guide](docs/USER_GUIDE.md) | Using both plugins, step by step: install, import, stream, expressions, spring bones, retargeting, troubleshooting. |
| [VMC Live Link README](Plugins/VMCLiveLink/README.md) | Reference: source settings, senders, the remapper, supported messages, coordinates, troubleshooting. |
| [VRM Interchange README](Plugins/VRMInterchange/README.md) | Reference: import options and settings, generated assets, spring bones, IK Rig, Live Link, materials, troubleshooting. |
| [CHANGELOG](CHANGELOG.md) | What changed in each version, and what to do after updating. |
| [Contributing](CONTRIBUTING.md) | Building, testing, CI and releases, and submitting changes. |
| [Architecture](docs/ARCHITECTURE.md) | Modules, data flow, threading, coordinates, versioning and tests. |
| [Editor test plan](docs/EDITOR_TESTS.md) | Step-by-step checks that need a person in the editor. |

## Getting the source

The repository uses [Git LFS](https://git-lfs.github.com/) for its binary assets, so a GitHub *Download ZIP* doesn't contain them. Clone it instead:

```bash
git clone https://github.com/lifelike-and-believable/VMCLiveLink.git
```

```bash
git lfs pull
```

Run `git lfs pull` inside the cloned `VMCLiveLink` folder. To build and test the plugins, see [CONTRIBUTING.md](CONTRIBUTING.md). Pull requests are built and tested by CI; release packages for Fab are built from `release/*` tags ([CI and releases](CONTRIBUTING.md#ci-and-releases)).

[![PR Build and Tests](https://github.com/lifelike-and-believable/VMCLiveLink/actions/workflows/pr-build.yml/badge.svg)](https://github.com/lifelike-and-believable/VMCLiveLink/actions/workflows/pr-build.yml)
[![Fab Plugin Builds](https://github.com/lifelike-and-believable/VMCLiveLink/actions/workflows/fab-plugin-build.yml/badge.svg)](https://github.com/lifelike-and-believable/VMCLiveLink/actions/workflows/fab-plugin-build.yml)

## Support

Report bugs and request features on [GitHub Issues](https://github.com/lifelike-and-believable/VMCLiveLink/issues). [CONTRIBUTING.md](CONTRIBUTING.md#reporting-issues) lists what to include.

## License

Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
