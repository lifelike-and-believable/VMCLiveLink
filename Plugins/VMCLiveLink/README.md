# VMC Live Link Plugin

**Stream motion capture from VMC senders into Unreal Engine through Live Link.**

VMC Live Link receives the [VMC protocol](https://protocol.vmc.info/english) (OSC over UDP), which VSeeFace, VirtualMotionCapture and other VTuber and motion capture tools send. It publishes the stream as a Live Link subject, so an Animation Blueprint can drive a character with it. The body pose, facial expressions (blend shapes), tracked devices and the sender's camera are all supported.

- **Live Link source** with a settings panel, a status line that shows the frame rate, jitter and sender, and a `VMC.Stats` console command.
- **Remapper** that renames VMC's bones and curves for your mesh, with presets, reusable mapping assets and a live mapping table showing which names reach the mesh.
- **Low latency:** packets are received and frames built on a thread of their own, each frame timestamped when it arrives.

It works on its own. [VRM Interchange](https://github.com/lifelike-and-believable/VMCLiveLink/blob/main/Plugins/VRMInterchange/README.md), the companion plugin, imports VRM avatars that it can drive directly. The [User Guide](https://github.com/lifelike-and-believable/VMCLiveLink/blob/main/docs/USER_GUIDE.md) walks through using the two together, step by step; this README is the reference.

## Contents

- [Requirements](#requirements)
- [Quick Start](#quick-start)
- [Sender Setup](#sender-setup)
- [Source Settings](#source-settings)
- [The Remapper](#the-remapper)
- [Supported Messages](#supported-messages)
- [Coordinate Conventions](#coordinate-conventions)
- [Troubleshooting](#troubleshooting)
- [Performance](#performance)
- [Testing Without a Sender](#testing-without-a-sender)
- [Known Limitations](#known-limitations)

## Requirements

- **Unreal Engine** 5.6, 5.7 or 5.8 (the versions it is built and tested on).
- **Windows (Win64)** only for now: that is the only platform the plugin is built and tested on.
- The **Live Link** and **OSC** plugins, which ship with the engine. VMC Live Link enables them.

## Quick Start

1. **Enable the plugin.** Open **Edit → Plugins**, search for *VMC Live Link*, tick it and restart the editor. (It is enabled by default when installed in a project's `Plugins` folder.)
2. **Add the source.** Open **Window → Virtual Production → Live Link**. Click **+ Add Source → VMC Live Link Source**, check the settings (the defaults suit most senders: port 39539, every network interface, subject `VMC_Subject`) and click **Create**. The source's status reads *Listening on :39539, waiting for data*.
3. **Start the sender.** Point your VMC sender at this computer's IP address and port 39539 (see [Sender Setup](#sender-setup)). Use `127.0.0.1` if the sender runs on the same computer. The status changes to *Receiving 60.0 fps from …*, and the subject `VMC_Subject` appears with a green dot.
4. **Map the names to your mesh.** Select the subject in the Live Link panel. Its **Remapper** (a VMC Live Link Remapper, added automatically) is in the details, with its settings inline under it, and the remapper's **Mapping Tools** and **Live Mapping** are at the top of the subject's details:
   - Set **Target → Reference Skeleton** to the skeletal mesh you want to drive.
   - For a mesh imported with VRM Interchange, click **Mapping Tools → Map Bones From Humanoid Metadata**. Otherwise, see [The Remapper](#the-remapper).
   - Open **Live Mapping**: every bone and curve received is listed, with a note on any that don't reach the mesh.
5. **Drive the character.** In the mesh's Animation Blueprint, add a **Live Link Pose** node, set its subject to `VMC_Subject` and connect it to the output pose. Place the character in the level, select its skeletal mesh component and tick **Update Animation in Editor** (in **Skeletal Mesh**, under the advanced options; it isn't saved, so tick it again after reopening the level) so it animates outside Play; or press **Play** or **Simulate**. It moves as soon as data arrives.

To keep the source and the remapper settings between editor sessions, save a **Live Link preset** from the Live Link panel (**Presets → Save As Preset**). Set it as the default in **Project Settings → Plugins → Live Link → Default Live Link Preset** to load it at startup.

A mesh imported with VRM Interchange can use the generated Live Link actor and Animation Blueprint instead of step 5; see *Using Live Link* in the [VRM Interchange README](https://github.com/lifelike-and-believable/VMCLiveLink/blob/main/Plugins/VRMInterchange/README.md#using-live-link), and set the actor's **Subject** to the subject name.

## Sender Setup

What every sender needs:

- **Address:** the IP of the computer running Unreal (`127.0.0.1` for the same computer).
- **Port:** the source's port, 39539 by default. Each source listens on one port, so to receive two senders at once, give them different ports and add a source for each.
- **Firewall:** the first time the editor listens, Windows may ask whether to allow it on the network. Allow *Unreal Editor* on private networks, or packets from other computers won't arrive.

What the sender streams decides the remapper settings: all VMC senders use Unity humanoid bone names (`Hips`, `LeftUpperArm`, ...), but the expression names are those of the avatar loaded in the sender.

The menu names below are those of recent versions of each app; they may differ in yours.

### VSeeFace

In **Settings → General settings**, find the **OSC/VMC protocol** section and enable the **VMC protocol sender**. Enter the IP and port and keep VSeeFace running.

VSeeFace loads VRM 0.x avatars, so it sends VRM 0.x expression names (`Joy`, `A`, `Blink_L`, ...), plus any ARKit blend shapes the avatar has (for "perfect sync" avatars). Use the **VMC / VRM 0.x expressions** preset if you map expressions to ARKit names.

### VirtualMotionCapture

In **Settings → Detailed settings**, enable **OSC motion sending** and enter the address and port. VirtualMotionCapture also streams its tracked devices and camera; turn on **Device Subjects** or **Camera Subject** in the source settings to receive them.

### Warudo

Warudo is mainly a VMC receiver. If your version can send VMC (built in or through a plug-in), point the sender at this computer's address and port. Warudo streams aren't tested with this plugin; see Warudo's documentation.

### Blender (VMC4B and others)

VMC4B receives VMC into Blender; it doesn't send it. To send from Blender, use an add-on that sends VMC. It must send Unity humanoid bone names and end each frame with `/VMC/Ext/Blend/Apply` (see [Supported Messages](#supported-messages)). Streams from Blender haven't been tested with this plugin.

### Any Other Sender

The plugin follows the [VMC protocol specification](https://protocol.vmc.info/english). A sender works if it sends `/VMC/Ext/Bone/Pos` with Unity humanoid bone names and ends each frame with `/VMC/Ext/Blend/Apply`. Run `VMC.Stats` in the console to see what a sender is sending.

## Source Settings

Every setting is in the source's details in the Live Link panel, and in the creation panel. Edits apply to the running source. A change to the port, bind address, **Receive Thread**, the sender rules or the device and camera settings restarts the listener. A new subject name moves the data to a new subject, which gets a new remapper: set up its maps again, or apply a mapping asset. An invalid value is logged to the Output Log and the setting goes back to its previous value. Hover a setting in the editor for its tooltip.

| Setting | Default | What it does |
|---|---|---|
| **Port** | 39539 | UDP port the sender sends to. |
| **Bind Address** | 0.0.0.0 | IPv4 address to listen on. `0.0.0.0` listens on every network interface. |
| **Subject Name** | VMC_Subject | The Live Link subject the data is published as. |
| **Receive Thread** | On | Receives on a thread of the plugin's own, each frame timestamped on arrival. Off: messages arrive through the OSC plugin on the game thread, once per engine frame. |
| **Convert Unity to UE Axes** | On | Converts the sender's Unity axes to UE's (see [Coordinate Conventions](#coordinate-conventions)). Leave on for VMC senders. |
| **Convert Metres to Centimetres** | On | VMC sends metres; UE uses centimetres. Leave on for VMC senders. |
| **Yaw Offset Deg** | 0 | Extra turn of the root about UE's up axis. 180 turns the character round. |
| **Zero Missing Curves** | Off | Curves the sender didn't send since the last frame read 0. Off: they hold their last value, which suits senders that only send blend shapes that changed. |
| **Prefer Incoming Translations** | Off | Uses every bone's streamed translation. Off: only Hips does; the other bones get the reference skeleton's rest translations from the remapper (**Use Reference Translations**, with a Reference Skeleton set), or none (see [The Remapper](#the-remapper)). |
| **Allowed Senders** | Empty | Only packets from these IPv4 addresses are used. Empty: any sender. |
| **Lock to First Sender** | Off | Uses only the first sender heard from until the listener restarts (after an edit to the port, bind address or sender settings, say), so a second sender on the network can't take over the subject. |
| **Device Subjects** | Off | Publishes each tracker, headset and controller the sender streams as a Transform subject named `<Subject>_<serial>` (with `_2` added if two devices would share a name). |
| **Camera Subject** | Off | Publishes the sender's camera as a Camera subject named `<Subject>_<camera name>`, with its field of view. |

### Status

The Live Link panel's status column says what the source is doing:

| Status | Meaning |
|---|---|
| *Listening on :39539, waiting for data* | The port is open; nothing has arrived yet. |
| *Receiving 60.0 fps from 192.168.1.20, jitter 0.4 ms (receive thread)* | Frames are arriving. Jitter is the average variation in the time between frames. |
| *No data for 5 s (last from 192.168.1.20)* | Frames were arriving and have stopped. |
| *Port 39539 in use* | Another program (or another source) is listening on the port. |
| *Can't listen on 192.168.1.5:39539 (...)* | The bind address isn't one of this computer's, or the port is in use. |
| *Stopped* | Live Link has shut the source down. |

Notes may follow: what the sender says about itself (*sender: no avatar loaded*, *calibrating*, *tracking lost*, ...), *locked to this sender*, the number of device and camera subjects, or *ignoring N other senders*.

The creation panel warns before you click **Create** if the port can't be opened. With **Receive Thread** off, the OSC plugin doesn't report a port it can't open, so the status reads *Listening...* even when the port is taken.

### VMC.Stats

Type `VMC.Stats` in the editor's console (or the output log's command line) to print, for each VMC source, the packets and messages per second for each VMC address since the last report, and any addresses the plugin doesn't use. Packets refused by **Allowed Senders** or **Lock to First Sender** aren't counted. With **Receive Thread** off, the packet count is a message count.

## The Remapper

The source publishes VMC's own names: a `root` bone, the 55 Unity humanoid bones (`Hips`, `Spine`, `LeftUpperArm`, ...), any other bone the sender streams, and the expressions as curves. The **VMC Live Link Remapper** renames them to what your mesh uses. The VMC source applies it to everything it sends Live Link, so the renamed names are what the Live Link Pose node and the other consumers see; a remapper of another class on a VMC subject doesn't rename what Live Link evaluates (Live Link, UE 5.6 to 5.8, evaluates with the names the source sends). On another source's subject, this remapper is applied by Live Link as before, so the same limit applies there. A new VMC subject gets one automatically, with the interpolation and pre-processors **Project Settings → Live Link** gives the animation role (**Animation Interpolation** if none is set for it); you can pick a different remapper class in **Project Settings → Plugins → VMC Live Link → Default Remapper Class**.

Select the subject in the Live Link panel to edit its remapper. Its settings (Target, Mapping and Normalizer below) are shown inline under the subject's **Remapper**. The **Mapping Tools** buttons and the **Live Mapping** table are at the top of the subject's details. (A remapper shown in a details panel of its own has the same sections, in the order below.)

### Target

- **Reference Skeleton:** the mesh the stream drives. Its bones and morph targets are what the Live Mapping table checks names against, and the mapping tools map onto it. If empty, the project's **Default Reference Skeleton** (VMC Live Link project settings) is used for the Live Mapping table, the rest translations and **Map Bones From Humanoid Metadata**; the other mapping tools need this one set.
- **Use Reference Translations:** VMC senders send most bones with a rotation only. With this on, each bone gets the reference skeleton's rest translation, so the limbs have the mesh's proportions. `root` and `Hips` always use the stream's translations, since they carry the motion.

### Mapping

- **Bone Name Map** and **Curve Name Map:** incoming name to the name on the mesh. Names without an entry pass through unchanged.
- **Preset:** the naming scheme **Apply Preset** adds to the maps:

  | Preset | For |
  |---|---|
  | None / Manual | Nothing is added. |
  | ARKit (MetaHuman-friendly) | Senders that send ARKit names: maps them to themselves, so they are listed. |
  | VMC / VRM 0.x expressions (ARKit targets) | VRM 0.x expressions to ARKit curves: `Blink_L` → `eyeBlinkLeft`, `Joy` → `mouthSmileLeft`, `Sorrow` → `mouthFrownLeft`, `Fun` → `cheekPuff`, `A` → `jawOpen`, `U` → `mouthPucker`, `O` → `mouthFunnel`, and the brows. Turn on the **Normalizer** to add the right side of the smile. |
  | VMC / VRM 1.0 expressions (ARKit targets) | The same for VRM 1.0 names (`blinkLeft`, `happy`, `sad`, `aa`, `ou`, `oh`), without the brows. |
  | VMC / VRoid | Unity humanoid bones to VRoid Studio's skeleton (`Hips` → `J_Bip_C_Hips`, fingers included), and VRM 0.x expressions to VRoid's `Fcl_*` morph targets (`Joy` → `Fcl_ALL_Joy`, `A` → `Fcl_MTH_A`, `Blink_L` → `Fcl_EYE_Close_L`, ...). |
  | Rokoko (ARKit names) | ARKit names, plus Rokoko's `mouthSmile_L`/`_R`. |
  | Custom (JSON) | Nothing is added; for maps loaded with the remapper's `LoadCustomCurveMapFromJSON` Blueprint function (`{"Curves": {...}, "Bones": {...}}`). Applying it still removes the previous preset's unedited entries. |

  Expressions with no single ARKit equivalent (`I`, `E`, `Angry`, `Surprised`, ...) are left unmapped, so they pass through under their own names. Applying a different preset removes the previous preset's entries that you haven't edited.
- **Mapping Asset:** a saved pair of maps (below).
- **Auto Detect Mapping From Reference** (on by default): while both maps are empty, when the subject is created, the remapper maps itself for its **Reference Skeleton**: the selected **Mapping Asset** if it matches the mesh, else the mesh's humanoid metadata (a VRM Interchange mesh), else the mapping asset made for the mesh. This needs the remapper's own **Reference Skeleton** to be set at that point (for example, from a saved Live Link preset); otherwise use the buttons below.

To drive a VRM avatar's expressions by their own names, leave expression curves unmapped and use the **VRM Expressions** AnimGraph node from VRM Interchange, which handles both VRM versions' names.

### Mapping Tools

| Button | What it does |
|---|---|
| **Apply Preset** | Adds the selected preset's entries to the maps. Entries you edited are kept. With a Reference Skeleton set and data arriving, it also maps the body bones to UE mannequin-style names it finds on the mesh (`pelvis`, `spine_01`, `upperarm_l`, ...), replacing those bone entries. |
| **Seed From Subject** | Lists every name the subject is receiving in the maps (as itself, ready to edit) and applies the preset that fits them. |
| **Map Bones From Humanoid Metadata** | Replaces the bone map with the humanoid map stored on the reference skeleton. VRM Interchange writes it on import (`VRM.Humanoid.*` metadata), so this maps every bone exactly. Curves are left alone. |
| **Apply Mapping Asset** | Replaces the maps with the selected mapping asset's. Sets **Preset** to *Custom*. |
| **Auto-Detect Mapping** | Finds the mapping asset made for the reference skeleton and applies it; if none was made for it, applies the one whose bone names best match the mesh. Sets **Preset** to *Custom*. |
| **Save to Mapping Asset** | Saves the maps into the selected mapping asset, with the reference skeleton's signature if **Capture Signature On Save** is on. |
| **Create Mapping Asset...** | Creates a mapping asset from the current maps and the reference skeleton's signature. |

In the Live Link panel, the buttons can't be undone: Live Link creates subject settings without undo support, so **Edit → Undo** doesn't restore the maps. Save a Live Link preset or a mapping asset first if you may want them back. (In a details panel of the remapper itself, such as a remapper saved in an asset, each button is one undoable step; an asset made by **Create Mapping Asset...** stays.)

### Mapping Assets

A **VMC Live Link Mapping Asset** (Content Browser: the **Add** menu; search for *VMC LiveLink Mapping Asset*) stores a bone map and a curve map for reuse. It also stores the *signatures* of the skeletons it is for, computed from their bone names, so **Auto-Detect Mapping** can find it for a skeleton with the same bones, even under another name. Make one for each rig you drive. For a new subject, set the remapper's **Reference Skeleton** and click **Auto-Detect Mapping** to apply it; a subject restored from a Live Link preset with its Reference Skeleton set picks it up by itself.

### Normalizer

For ARKit targets such as MetaHuman, fed by senders that send only some ARKit curves. Off by default; turn on **Enable Meta Human Curve Normalizer**. When on, it adds:

- the missing side of `eyeBlinkLeft`/`eyeBlinkRight`, as a copy of the other side scaled by **Blink Mirror Strength**;
- the missing side of `mouthSmileLeft`/`mouthSmileRight`, scaled by **Joy To Smile Strength**;
- `mouthPucker` as half of `mouthFunnel`, when the stream has no `mouthPucker`.

It never changes a curve the stream sends.

### Live Mapping

The table lists every bone and curve the subject receives, what it is renamed to, and a note when it won't reach the mesh:

- **Another bone/curve has this name too:** two incoming names map to the same name, so only one of them drives it.
- **No bone of this name on the reference mesh:** the renamed bone doesn't exist on the mesh, so it is ignored.
- **No morph target of this name on the reference mesh:** the curve has no matching morph target. An Animation Blueprint may still use it.
- **Unmapped: passes through as sent.**
- **Added by the normalizer.**

Tick **Only names that don't reach the mesh** to list just the problems. The table is shown when one subject is selected, and refreshes itself while shown. It only checks names against the mesh when the remapper (or the project) has a Reference Skeleton.

### Meshes Whose Rest Pose Isn't a T-Pose With Unrotated Bones

VMC senders send each bone's rotation relative to a humanoid rest pose with no bone rotations (VRM's normalized T-pose). A mesh imported with VRM Interchange has this rest pose, so renaming is enough. A mesh whose bones are rotated at rest, such as the UE mannequin and most FBX rigs, twists if driven directly. Drive a VRM-style skeleton with the stream and retarget it to the mesh with an IK Retargeter instead. **VMC Retarget Actor**, in the Content Browser's **Add** menu, creates a copy of the plugin's template retarget actor to start from. The plugin's `Retargeting` folder also has an IK Rig for each skeleton (`IKR_VRM`, `IKR_UE5`) and a VRM-to-UE5 retargeter (`RTG_VRMToUE5`).

## Supported Messages

| Address | Arguments | Supported | Notes |
|---|---|---|---|
| `/VMC/Ext/Root/Pos` | name, position xyz, rotation xyzw | Yes | Published as the `root` bone. Also accepts 7 floats with no name, which older senders sent. |
| `/VMC/Ext/Root/Pos` (v2.1) | ..., scale xyz, offset xyz | Partial | Position and rotation are used; the mixed-reality scale and offset are ignored (logged once). |
| `/VMC/Ext/Bone/Pos` | name, position xyz, rotation xyzw | Yes | Unity humanoid names are placed in the humanoid hierarchy; any other bone is added under Hips. |
| `/VMC/Ext/Blend/Val` | name, value | Yes | Published as a curve of that name. |
| `/VMC/Ext/Blend/Apply` | none | Yes | Ends the frame: the pose and curves are pushed to Live Link. A sender that never sends it produces no frames. |
| `/VMC/Ext/OK` | loaded [, calibration state, calibration mode [, tracking]] | Yes | Shown in the source status (*no avatar loaded*, *calibrating*, *tracking lost*). |
| `/VMC/Ext/T` | time | Yes | Passed on as each frame's scene time, at 60 fps. Frames are timed (world time) by their arrival. |
| `/VMC/Ext/Hmd/Pos`, `Con/Pos`, `Tra/Pos` | serial, position xyz, rotation xyzw | Yes | Device subjects, when **Device Subjects** is on. |
| `/VMC/Ext/Hmd/Pos/Local`, `Con/Pos/Local`, `Tra/Pos/Local` | serial, position xyz, rotation xyzw | No | Counted by `VMC.Stats`, otherwise ignored; the world-space forms are used. |
| `/VMC/Ext/Cam` | name, position xyz, rotation xyzw, field of view | Yes | A Camera subject, when **Camera Subject** is on. |
| Anything else | | No | Keys, MIDI, settings and other addresses are ignored. `VMC.Stats` lists them. |

Messages can arrive one per packet or in OSC bundles, and bundles can be nested.

## Coordinate Conventions

VMC senders use Unity's space: left-handed, Y up, Z forward, X right, in metres. UE is left-handed too, with Z up, and uses centimetres. With both conversion settings on (the default):

| | Unity (sent) | UE (published) |
|---|---|---|
| Position | (x, y, z) m | (−x, z, y) × 100 cm |
| Rotation (quaternion) | (x, y, z, w) | (−x, z, y, w) |

This is a rotation of the axes, not a mirror, so left stays left. A character facing Unity +Z faces UE +Y, as the UE mannequin and meshes imported by VRM Interchange do.

**Worked example.** A sender puts the hips 1 m up and 0.5 m forward: Unity (0, 1, 0.5). The source publishes (0, 50, 100) cm: 50 cm along +Y (the character's forward) and 100 cm up. A head turned 30° to the character's right is a Unity rotation of +30° about Y, quaternion (0, 0.259, 0, 0.966). It becomes (0, 0, 0.259, 0.966): +30° about UE Z, which turns +Y (forward) toward −X, the character's right.

**Yaw offset** turns the root (and the device and camera subjects) about UE Z after the conversion; positive values turn +X toward +Y, like a positive UE yaw. Use 180 to turn the character to face −Y.

Bone transforms are local to the bone's parent in the humanoid hierarchy. The root transform and devices are in the sender's world space.

## Troubleshooting

The Output Log's `LogVMCLiveLink` category says when senders are refused, bones are added under Hips, settings are rejected or packets are malformed.

### The status stays at "waiting for data"

- Check that the sender is sending to this computer's IP and the source's port. `127.0.0.1` only works when both run on the same computer.
- If the sender is on another computer, allow *Unreal Editor* through the Windows firewall (private networks) and check that both computers are on the same network.
- Check that **Bind Address** is `0.0.0.0` or an address of this computer.
- If **Allowed Senders** is set, the sender's IP must be listed. The status says *ignoring N other senders* when packets are refused.
- The sender must end each frame with `/VMC/Ext/Blend/Apply`; without it no frame (and no subject) is made. `VMC.Stats` shows whether packets arrive and whether Apply is among them.

### The status says "Port 39539 in use"

Another program, or another VMC source, is listening on that port; for example, another app's own VMC receiver. Give this source a free port and point the sender at it.

### The subject is receiving but the character doesn't move

- In the editor viewport, a placed character only animates with **Update Animation in Editor** ticked on its skeletal mesh component (**Skeletal Mesh**, advanced; not saved with the level), or during **Play** or **Simulate**.
- The Animation Blueprint's **Live Link Pose** node must use the subject's name (`VMC_Subject` by default).
- Open the remapper's **Live Mapping** table (with its Reference Skeleton set). If the bones say *No bone of this name on the reference mesh*, the names don't match the mesh: map them (see [The Remapper](#the-remapper)).

### The character stays in a T-pose, or only the root moves

The bone names don't reach the mesh. Check the Live Mapping table, and for a VRM Interchange mesh click **Map Bones From Humanoid Metadata**.

### Limbs are twisted, mirrored or bent the wrong way

- The mesh's rest pose has rotated bones. Retarget instead of renaming (see [Meshes Whose Rest Pose Isn't a T-Pose With Unrotated Bones](#meshes-whose-rest-pose-isnt-a-t-pose-with-unrotated-bones)).
- **Convert Unity to UE Axes** must be on for VMC senders.
- Left and right are swapped: check the bone map for entries that map a `Left*` bone to a right bone.

### The character faces the wrong way, or is tiny or huge

Use **Yaw Offset Deg** to turn it (180 turns it round). A character sunk into the floor, whose root barely moves, means **Convert Metres to Centimetres** is off (with **Prefer Incoming Translations** on, the whole character is tiny instead). If the limbs have the wrong lengths, set the remapper's **Reference Skeleton**, turn on **Use Reference Translations** and turn off **Prefer Incoming Translations**.

### Expressions flicker or stick

- Expressions that flicker to 0: the sender sends only the blend shapes that changed, and **Zero Missing Curves** is on. Turn it off.
- Expressions that stay on after the face relaxes: with **Zero Missing Curves** off, a curve holds its last value until the sender sends a new one. If the sender stops sending a curve instead of sending 0, turn **Zero Missing Curves** on.
- Two curves that fight: the Live Mapping table shows *Another curve has this name too*. Remove one of the two map entries.

### Jitter or stutter

Keep **Receive Thread** on. Its status shows the jitter; a few milliseconds is normal over Wi-Fi. Prefer a wired network for the sender.

If the avatar moves in steps, check the subject's **Interpolation** in the Live Link panel: it should be **Animation Interpolation** (a subject saved in a preset by an earlier version may have **None**; set it and save the preset again). If the viewport stops while the sender's window is focused, untick **Editor Preferences → General → Performance → Use Less CPU when in Background**.

## Performance

- Building a frame from its messages costs about 10 µs of CPU for 55 bones and 60 curves (measured by the `VMC.Perf.StreamFrame` test, which leaves out the socket, packet parsing and the push to Live Link). It runs on the receive thread by default, so it costs the game thread nothing.
- On the receive thread, parsing makes no heap allocation per message.
- The remapper resolves its maps when the names change, not every frame, and copies each frame by index.
- Static data (the bone and curve names) is sent to Live Link only when a new name arrives or a setting changes.

## Testing Without a Sender

`scripts/vmc_sender.py` (in the repository; Python 3, standard library only) sends a spec-conformant VMC stream with all 55 humanoid bones and expressions:

```
python scripts/vmc_sender.py send --port 39539 --fps 60 --duration 10
```

It can also record a real sender's stream and replay it:

```
python scripts/vmc_sender.py record --listen 39540 --out capture.vmcrec
python scripts/vmc_sender.py replay capture.vmcrec --port 39539
```

Run it with `--help` for the other options (VRM 1.0 names, v2.1 root, packets without bundles).

## Known Limitations

- Windows (Win64) only for now.
- The v2.1 root scale and offset (mixed-reality calibration) and the `/Local` device messages are ignored.
- A source receives one sender stream on one port. Use one source per sender.
- Frames are timed by their arrival; the sender's `/VMC/Ext/T` only becomes their scene time, at an assumed 60 fps.
- The generated Animation Blueprints in VRM Interchange don't add the VRM Expressions node; add it by hand.
- Humanoid metadata (**Map Bones From Humanoid Metadata**, and its automatic use at subject creation) is read only in the editor, including during Play In Editor. A packaged game uses the maps saved in the Live Link preset or a mapping asset.

## License

Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
