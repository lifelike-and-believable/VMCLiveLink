# User Guide

This guide takes you from installing the two plugins to a VRM avatar in Unreal Engine that moves, makes faces and swings its hair live, driven by a VMC sender such as VSeeFace or VirtualMotionCapture. It is for artists and technical artists working in the editor; you don't write any C++.

- **VRM Interchange** imports `.vrm` avatars: the skeletal mesh, materials, expressions, spring bones (hair and clothes physics), an IK Rig for retargeting, and a ready-made Live Link character.
- **VMC Live Link** receives the VMC protocol (motion capture over the network) and publishes it to Unreal's Live Link, so the avatar can follow a performer.

Each plugin works on its own: VMC Live Link can drive any skeletal mesh, and VRM Interchange imports avatars whether or not you stream to them. Each step below links to the plugin's README for the details: the [VRM Interchange README](../Plugins/VRMInterchange/README.md) and the [VMC Live Link README](../Plugins/VMCLiveLink/README.md).

## Contents

1. [What you need](#1-what-you-need)
2. [Install the plugins](#2-install-the-plugins)
3. [Register the VRM import pipelines](#3-register-the-vrm-import-pipelines)
4. [Import a VRM avatar](#4-import-a-vrm-avatar)
5. [Connect a VMC sender](#5-connect-a-vmc-sender)
6. [Drive the avatar](#6-drive-the-avatar)
7. [Facial expressions](#7-facial-expressions)
8. [Spring bones](#8-spring-bones)
9. [Other characters and retargeting](#9-other-characters-and-retargeting)
10. [Save your setup](#10-save-your-setup)
11. [Reimporting and updating](#11-reimporting-and-updating)
12. [Troubleshooting](#12-troubleshooting)

## 1. What you need

- **Unreal Engine 5.6, 5.7 or 5.8** on **Windows** (Win64). The plugins are built and tested on each of those engines, on Windows only.
- **To build the plugins from this repository:** Visual Studio 2022 with the *Game development with C++* workload, and a clone made with Git LFS (see [Getting the source](../README.md#getting-the-source)). A GitHub *Download ZIP* doesn't contain the assets. A packaged release that includes `Binaries` needs neither.
- A **VRM avatar** (`.vrm`, VRM 0.x or 1.0), for example one exported from VRoid Studio.
- A **VMC sender**: an app that tracks you and sends VMC, such as VSeeFace or VirtualMotionCapture, on this computer or another one on the same network. To try things without one, the repository's `scripts/vmc_sender.py` sends a test stream (see [Testing Without a Sender](../Plugins/VMCLiveLink/README.md#testing-without-a-sender)).

## 2. Install the plugins

1. Put the `VMCLiveLink` and `VRMInterchange` folders in your project's `Plugins` folder (`YourProject/Plugins/`). Create the folder if it doesn't exist. A packaged release comes in one version per engine: use the one for your engine. `VRMCombined-UE5.7-…zip` has both plugins (`VMCLiveLink-…` and `VRMInterchange-…` have one each); inside, the plugin folders are under `UE5.7/Plugins/`, so copy those folders, not the zip's top folder.
2. Open the project. Built from the repository, Unreal says the plugin modules are missing and offers to rebuild them: click **Yes**. This needs Visual Studio (section 1).
3. Both plugins are enabled by default. To check, open **Edit → Plugins** and search for *VMC Live Link* and *VRM Interchange*. They turn on what they need (Interchange, Live Link, OSC, IK Rig).

## 3. Register the VRM import pipelines

VRM Interchange imports through Unreal's Interchange framework, which runs a list of *pipelines* for each file type. The plugin's pipelines (spring bones, IK Rig, Live Link, materials, avatar description) have to be added to that list once per project. The plugin never changes your project settings on its own, so it asks first:

1. When the editor starts, a notification offers to register the VRM import pipelines. Click **Register**.
2. If you missed it, use **Edit → Project Settings → Plugins → VRM Interchange → Register VRM Import Pipelines**.

The notification comes back at every start while the pipelines aren't registered, or after a plugin update changes them. **Don't ask again** stops it; the button in Project Settings still works.

## 4. Import a VRM avatar

1. Make a folder in the Content Browser, for example `Characters`, and drag your `.vrm` file into it.
2. The import dialog opens. Its VRM options are in the **VRM Import** category of each VRM pipeline: **Spring Bones**, **IK Rig**, **Live Link Actor**, **Retarget Actor**, **Avatar Description** and the **Materials** options. The defaults suit a first import; hover an option for what it creates. Check that **Spring Bones: Post-Process AnimBlueprint** and **Spring Bones: Assign Post-Process AnimBlueprint** are ticked (they are by default); without them the springs don't run (section 8).
3. Click **Import**.

When it finishes, a notification lists what was created and the avatar's licence and usage permissions. **Show in Content Browser** selects the new assets; **Show N problems** opens the **VRM Import** message log with any warnings from the import.

### What you get

For `Alice.vrm` imported into `Characters`, you get a folder `Characters/Alice` with:

| Asset | What it is |
|---|---|
| `SkeletalMeshes/Alice` | The skeletal mesh, with its skeleton and physics asset. Bones are named as in the file. |
| `Alice_Avatar` | The avatar description: the humanoid bone map, the expressions, look-at and licence. |
| `Materials/MI_VRM_Alice_*` | One material instance per VRM material, under a shared parent (`MI_VRM_Alice__MToon` for toon materials), so you can tune them all in one place. |
| `Textures/` | The avatar's textures. |
| `SpringBones/Alice_SpringData` | The spring bone settings from the file. |
| `SpringBones/PP_ABP_VRMSpringBones_Alice` | A post-process Animation Blueprint that runs the springs wherever the mesh is used. |
| `IKRigDefinition/IK_Rig_VRM_Alice` | An IK Rig for retargeting (section 9). |
| `LiveLink/BP_LL_VRM_Alice` | The Live Link character: drop it in a level and point it at a subject (section 6). |
| `LiveLink/Animation/ABP_LL_VRM_Alice` | Its Animation Blueprint. |
| `LiveLink/BP_LL_VRM_To_UE5_Alice` | A retarget actor that puts the character's live pose on a UE5 mannequin you supply (section 9). |

The toon (MToon) master materials are shared by every VRM you import and are created once, in `/Game/VRMInterchange/Materials`. **Save them** along with the avatar (**File → Save All**). Don't edit them; edit the material instances instead.

The [VRM Interchange README](../Plugins/VRMInterchange/README.md#what-gets-created) lists every asset and folder, and [Materials](../Plugins/VRMInterchange/README.md#materials) explains how the toon shading and outlines work.

## 5. Connect a VMC sender

1. Open **Window → Virtual Production → Live Link**.
2. Click **+ Add Source → VMC Live Link Source**. The defaults suit most senders: port **39539**, every network interface, subject **`VMC_Subject`**. Click **Create**.
3. The source's status reads *Listening on :39539, waiting for data*.
4. In your sender, turn on VMC sending and point it at this computer: `127.0.0.1` if it runs on the same computer, otherwise this computer's IP address, and port 39539.
   - **VSeeFace:** **Settings → General settings → OSC/VMC protocol**, enable the **VMC protocol sender**.
   - **VirtualMotionCapture:** **Settings → Detailed settings**, enable **OSC motion sending**.
   - Other senders: see [Sender Setup](../Plugins/VMCLiveLink/README.md#sender-setup).
5. The status changes to *Receiving 60.0 fps from …*, and the subject `VMC_Subject` gets a green dot.

If the sender is on another computer and nothing arrives, allow *Unreal Editor* through the Windows firewall on private networks.

## 6. Drive the avatar

The stream uses the Unity humanoid bone names every VMC sender uses (`Hips`, `LeftUpperArm`, ...). The subject's **remapper** renames them to your avatar's bones.

1. With the sender running (the subject appears only once data arrives), select `VMC_Subject` in the Live Link panel. In its details, find the **Remapper**: its settings are listed under it, and its **Mapping Tools** and **Live Mapping** sections are at the top of the details.
2. Set **Target → Reference Skeleton** to your avatar's skeletal mesh (`SkeletalMeshes/Alice`).
3. In **Mapping Tools**, click **Map Bones From Humanoid Metadata**. VRM Interchange stored the avatar's humanoid map on the mesh, so every bone maps exactly.
4. Open **Live Mapping**: every bone and curve received is listed, with a note on any that don't reach the mesh. Tick **Only names that don't reach the mesh**. The body bones should be gone. What is left is humanoid bones your avatar doesn't have (VRoid avatars have no `Jaw`), which drive nothing, and the expression curves, which section 7 connects.
5. Drag `LiveLink/BP_LL_VRM_Alice` into the level. Select it and, in the Details panel, set **Subject** to `VMC_Subject`. (It starts empty.)
6. Press **Simulate** (or **Play**). The avatar follows the performer.

To see it move without Simulate, select the actor's skeletal mesh component and tick **Update Animation in Editor** (in **Skeletal Mesh**, under the advanced options). That box isn't saved with the level.

If the avatar faces the wrong way, set the source's **Yaw Offset Deg** to 180. The [VMC Live Link README](../Plugins/VMCLiveLink/README.md#source-settings) describes every source setting, and [The Remapper](../Plugins/VMCLiveLink/README.md#the-remapper) describes its presets and mapping assets.

## 7. Facial expressions

VMC senders send expressions by name: VRM 0.x names such as `Joy`, `A` and `Blink_L`, or VRM 1.0 names such as `happy`, `aa` and `blinkLeft`, depending on the avatar loaded in the sender. The **VRM Expressions** node turns them into your avatar's morph targets, whichever version either side uses.

The import adds it: `LiveLink/Animation/ABP_LL_VRM_Alice` has a **VRM Expressions** node after its **Live Link Pose** node, with its **Avatar Description** set to `Alice_Avatar`. (The import adds it only to an AnimBP it makes, and only when the avatar has expressions; an AnimBP it reuses keeps your graph. In an AnimBP from an earlier version of the plugin, add the node by hand, after the **Live Link Pose** node, and set its **Avatar Description**.)

Leave the expression names unmapped in the remapper (its **Curve Name Map** empty, **Preset** on *None / Manual*): the node expects the senders' own names.

For a MetaHuman or another ARKit face instead of a VRM avatar, map the expressions with a remapper preset and turn on its **Normalizer**; see [The Remapper](../Plugins/VMCLiveLink/README.md#the-remapper).

## 8. Spring bones

Spring bones make hair, skirts and other soft parts swing. The import sets them up: `PP_ABP_VRMSpringBones_Alice` is the mesh's post-process Animation Blueprint, so the springs run wherever the mesh is used, including under the Live Link character.

To see the springs and colliders, type in the console (**`~`**):

```
vrm.SpringBones.DrawSprings 1
vrm.SpringBones.DrawColliders 1
```

### Making the springs react to the whole character

VRM files can give a spring a *center* bone, and the springs then move with that bone instead of lagging behind it. VRoid Studio gives every spring the `Root` bone as its center, so the hair follows the body's own motion (a head turn, a hop) but doesn't swing when the whole character moves or turns. To have it swing then too:

1. Open `SpringBones/PP_ABP_VRMSpringBones_Alice` and select the **VRM Spring Bones** node.
2. In **Spring | Simulation**, untick **Use Spring Centers** and leave **Simulation Space** on **World**.
3. If the node's **External Velocity** is connected to the character's movement, set **External Velocity Scale** to 0, or movement counts twice.
4. Compile and save.

### Tuning

Open `Alice_SpringData` to tune the springs; changes reach a running preview or Play session at once. Its **Editing Tools** can scale every joint's stiffness, drag and gravity together, reset a spring to the file's values, or read the file again. See [Using Spring Bones](../Plugins/VRMInterchange/README.md#using-spring-bones) for each setting.

## 9. Other characters and retargeting

**Retarget the avatar's live pose to another character.** `LiveLink/BP_LL_VRM_To_UE5_Alice` shows the VMC-driven pose on a UE5 mannequin. The plugin doesn't ship the mannequin: add it to your project (for example `SKM_Manny` from the Third Person template). Place the actor, then in its Details set **UE5 Character** to the mannequin mesh and **Subject** to `VMC_Subject`. Its retargeter assumes VRoid bone names (`J_Bip_*`); for other avatars, make an IK Retargeter from `IK_Rig_VRM_Alice` (next paragraph).

**Retarget animations to or from the avatar.** `IKRigDefinition/IK_Rig_VRM_Alice` has retarget chains named like the UE5 mannequin's IK Rig (`Spine`, `LeftArm`, `LeftIndex`, ...). Create an **IK Retargeter** between it and the mannequin's IK Rig, and the chains map automatically. See [Using IK Rigs](../Plugins/VRMInterchange/README.md#using-ik-rigs).

**Drive a non-VRM character from VMC.** VMC sends each bone's rotation relative to a T-pose with unrotated bones, as VRM avatars have. Renaming bones is enough for those. Most other rigs, the UE5 mannequin included, have rotated bones at rest and would twist: drive a VRM-style skeleton and retarget it instead. **VMC Retarget Actor** in the Content Browser's **Add** menu gives you a template to start from. See [Meshes Whose Rest Pose Isn't a T-Pose](../Plugins/VMCLiveLink/README.md#meshes-whose-rest-pose-isnt-a-t-pose-with-unrotated-bones).

## 10. Save your setup

The Live Link source and the remapper's settings live in the editor session. To keep them:

1. In the Live Link panel, click **Presets → Save As Preset**.
2. To load it at every start, set it in **Project Settings → Plugins → Live Link → Default Live Link Preset**.

A subject restored from a preset keeps the maps saved in it. With empty maps and a **Reference Skeleton** set, it maps itself when it is created: from the mesh's humanoid map for a VRM avatar, or from a saved **mapping asset** for other rigs. Humanoid maps are read only in the editor; a packaged game uses the maps saved in the preset.

## 11. Reimporting and updating

**Reimport** (right-click the skeletal mesh → **Reimport**) after you change the `.vrm` file. In the import dialog, each VRM option's **Update Existing** decides what happens to the assets made last time: on, they are updated in place, and the actors and Animation Blueprints keep your edits; off, new copies with `_1`-style names are made. The spring data and avatar description are replaced from the file, so edits to them are lost.

**Actors: Update Existing** is off by default: tick it before reimporting, or you get `_1` copies of the actors and Animation Blueprints, without your edits. Spring Bones, IK Rig and Avatar Description update in place by default.

**After updating the plugins:**
- If the editor offers to register the VRM import pipelines again, click **Register**.
- Read the [CHANGELOG](../CHANGELOG.md). Some fixes apply only to new imports and ask you to reimport, for example the spring tail bones of VRoid avatars or materials that imported without textures.

## 12. Troubleshooting

| Symptom | What to do |
|---|---|
| The import dialog has no VRM options. | Register the pipelines (section 3). |
| Every material shows a grey grid and no textures. | Register the pipelines again, then reimport. See [the README](../Plugins/VRMInterchange/README.md#materials-import-without-textures-or-no-avatar-description). |
| The Live Link status stays at *waiting for data*. | Check the sender's address and port, the firewall, and that the sender is sending. See [Troubleshooting](../Plugins/VMCLiveLink/README.md#the-status-stays-at-waiting-for-data). |
| The status says the port is in use. | Another app is listening on 39539. Give the source and the sender another port. |
| The subject is receiving but the avatar doesn't move. | Set the actor's **Subject** (section 6, step 5), press **Simulate**, and check the remapper's **Live Mapping**. |
| The avatar stays in a T-pose, or only its root moves. | The bones aren't mapped: click **Map Bones From Humanoid Metadata** (section 6). |
| Limbs twist or bend the wrong way. | The mesh's bones are rotated at rest: retarget instead (section 9). |
| The avatar faces the wrong way. | Set the source's **Yaw Offset Deg** to 180. |
| The avatar stutters or lurches although the status shows a good frame rate. | The computer is probably too busy (check Task Manager's CPU): the sender then sends its frames in bursts. Cap the editor's frame rate (`t.MaxFPS 60`, or 30), turn off XR Animator's rendering or lower its camera resolution, and close other busy apps. `VMC.Stats` shows how evenly the frames arrive ([README](../Plugins/VMCLiveLink/README.md#jitter-or-stutter)). |
| The avatar moves in steps. | In the Live Link panel, set the subject's **Interpolation** to **Animation Interpolation** (a subject in a preset saved by an earlier version may have **None**), then save the preset. If the viewport stops while the sender is focused, untick **Editor Preferences → General → Performance → Use Less CPU when in Background**. |
| The face doesn't move. | Check the AnimBP's **VRM Expressions** node has the avatar's `<Mesh>_Avatar` (section 7), and leave expression names unmapped. |
| Hair doesn't swing when the character moves or turns. | Untick **Use Spring Centers** (section 8). |
| Hair tips don't swing on a VRoid avatar imported with 1.0.0. | Reimport it with the current version. |

For anything else, the Output Log's `LogVMCLiveLink` and `LogVRM*` categories and the **VRM Import** message log say what went wrong. The plugin READMEs have longer troubleshooting sections: [VMC Live Link](../Plugins/VMCLiveLink/README.md#troubleshooting) and [VRM Interchange](../Plugins/VRMInterchange/README.md#troubleshooting). Report problems on [GitHub Issues](https://github.com/lifelike-and-believable/VMCLiveLink/issues).
