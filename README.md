# VMCLiveLink

**Real-time performance meets Unreal Engine.**

VMCLiveLink is an Unreal Engine project that brings the **VMC (Virtual Motion Capture) protocol** directly into Unreal. It’s designed for creators who want to animate digital characters live — from VTubers to XR performers to virtual production teams.

## What It Does

- **Turns motion into presence.** Stream body and facial capture data straight into Unreal in real time.  
- **Bridges tools and stages.** Connects external mocap apps (like Virtual Motion Capture, VSeeFace, etc.) with Unreal’s Live Link system.  
- **Empowers performers.** Built for interactive avatars, live shows, and experiments in virtual puppetry.  

## Why It Matters

VMCLiveLink is more than a plugin — it’s part of a larger movement: treating technology like an *instrument* that artists can play. By lowering technical barriers, it enables performers and storytellers to focus on the message, the character, and the moment.

Whether you’re crafting an XR dance performance, streaming a VTuber show, or building a virtual stage for a live audience, VMCLiveLink gives you the bridge between raw performance and expressive digital presence.

## Plugins in This Repo

- **[VMCLiveLink](Plugins/VMCLiveLink)** — receives the VMC protocol over OSC and streams it into Unreal’s Live Link system.
- **[VRMInterchange](Plugins/VRMInterchange)** — imports VRM avatars (spring bone physics, IK rigs, Live Link actor scaffolding). See its [README](Plugins/VRMInterchange/README.md) for details.

## Getting Started

1. Clone the repo (requires [Git LFS](https://git-lfs.github.com/)):
   ```bash
   git clone https://github.com/lifelike-and-believable/VMCLiveLink.git
   cd VMCLiveLink
   git lfs pull
   ```

## 🚀 Fab Plugin CI/CD

This repository includes automated GitHub Actions to keep the plugins Fab-ready.

### 🔎 Verify Build & Package
[![Fab Plugin Builds](https://github.com/lifelike-and-believable/VMCLiveLink/actions/workflows/fab-plugin-build.yml/badge.svg)](https://github.com/lifelike-and-believable/VMCLiveLink/actions/workflows/fab-plugin-build.yml)

Runs on tag push (`release/*`) or manually via **Actions → Fab Plugin Builds**:
- Verifies all source files in `Plugins/VMCLiveLink` have a valid copyright header.
- Builds both the VMCLiveLink and VRMInterchange plugins against the UE 5.6 engine root configured in the workflow.
- Produces Fab-ready zips (a combined package plus one per plugin) as downloadable artifacts.
- On a `release/*` tag push, also publishes those zips to a GitHub Release. Manual `workflow_dispatch` runs skip release creation and only produce the build artifacts.

**Usage:**
- To cut a release: push a tag matching `release/*` (e.g. `release/1.2.0`).
- To test a build without releasing: go to **Actions → Fab Plugin Builds → Run workflow** and run it against any branch. The engine root/version and plugin paths are fixed in the workflow file, not configurable per-run.

### 🛠️ Auto-fix Headers
[![Auto-fix Headers](https://github.com/lifelike-and-believable/VMCLiveLink/actions/workflows/header-autofix.yml/badge.svg)](https://github.com/lifelike-and-believable/VMCLiveLink/actions/workflows/header-autofix.yml)

Ensures every `.h/.cpp` file starts with:

```cpp
// Copyright (c) YYYY Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
```

**Usage:**
1. Go to **Actions → Auto-fix Headers → Run workflow**.
2. Fill in:
   - `plugin_dir` → path to the plugin folder (default: `Plugins/VMCLiveLink`)
   - `holder` → copyright holder text to enforce (default: `Lifelike & Believable Animation Design, Inc. | Athomas Goldberg`)
3. If any headers were missing or out of date, the workflow commits the fix on a new branch and opens a pull request.
