# Editor Test Plan

Step-by-step checks for what the automated tests can't confirm: behaviour you have to see in the editor, real VRM files and real VMC senders, menu locations, and third-party apps. Each check says which open question it answers (a task, finding or [Verify] item in the [refactor plan](../Planning%20Docs/Code_Review_and_Refactor_Plan_2026-09.md), "Verification still owed").

How to use it:

- Work through the sections in order; later ones reuse assets and set-up from earlier ones. Sections are independent enough to split across sessions.
- Each check ends with **Pass if**. Record the result in the [results table](#results) as Pass, Fail (with what you saw) or Skipped (with why). A screenshot or short clip helps for anything visual.
- Report failures as GitHub issues, one per check, titled with the check's ID (for example "E-B3: warning filed under the wrong file"), with the output log.

## What you need

- The project built and open in UE 5.6 on Windows (see [CONTRIBUTING.md](../CONTRIBUTING.md)).
- **VRM files:**
  - a VRoid Studio export in **VRM 0.x** and one in **VRM 1.0** (the same character in both is ideal), with hair and skirt springs and about 60 blend shapes;
  - a VRM whose bones are *not* named by VRoid (`J_Bip_*`), for example a UniVRM export of a custom rig;
  - a plain `.glb` without VRM extensions.
- **Senders:** VSeeFace (webcam tracking is enough); VirtualMotionCapture with at least one tracker or controller if you have SteamVR hardware (else skip those checks).
- Python 3 for `scripts/vmc_sender.py`.
- Reference renders for the material check (E-D1): the same VRM in three-vrm (<https://pixiv.github.io/three-vrm/>) or UniVRM.
- Optional: a spring data asset and a VRM import saved before #115 and #137 (an older checkout of the project), for the upgrade checks E-F6 and E-A6.

Throughout, "the output log" is **Window → Output Log**, and "the Live Link panel" is **Window → Virtual Production → Live Link**.

---

## S. Set-up

**E-S1. Register the import pipelines.** *(P1.17, P4 pipelines)*
1. Open the project. A notification offers to register the VRM import pipelines.
2. Click **Register**. Open **Project Settings → Interchange** and find the `.vrm` translator's pipelines.

Pass if the notification appears once, the plugin changed no project setting before you clicked, and afterwards the list has the spring bone, IK Rig, Live Link, material and avatar description pipelines.

---

## A. Importing VRM files

**E-A1. Import both VRoid versions.** *(P1.7, P1.8, P1.9, P1.11, T-02, T-05, P3.3)*
1. Drag the VRM 0.x file into a new folder `/Game/Test/V0`, and the VRM 1.0 file into `/Game/Test/V1`. Keep the import dialog's defaults.
2. Place both skeletal meshes in the default level, side by side, next to the UE mannequin (`SKM_Manny` from the Third Person template, or any mannequin).
3. Look at them from the front, then turn on **Show → Advanced → Bones**.

Pass if both characters face the same way as the mannequin (+Y), neither is mirrored (the parting of the hair and any asymmetric accessory are on the same side as in the VRoid preview), the skin deforms nowhere (no stretched vertices at hands, hair or skirt), rigid accessories sit where they belong, and the skeletons have the expected bones. This closes **T-05** (VRM 0.x and 1.0 facing) and **T-02** (one skin per mesh in VRoid exports: note in the results whether the import log reports more than one skin).

**E-A2. Spring colliders on the body.** *(P1.11)*
1. Open `/Game/Test/V1/.../LiveLink/ABP_LL_...` (or any AnimBP with the spring bone node) and preview the mesh; or place the Live Link actor in the level and press **Simulate**.
2. In the console: `vrm.SpringBones.DrawColliders 1` and `vrm.SpringBones.DrawSprings 1`.

Pass if the collider spheres and capsules sit on the head, chest, arms and legs where the VRoid preview shows them, for both versions, and the spring chains run along the hair and skirt.

**E-A3. Plain glTF.**
1. Import the `.glb` into `/Game/Test/Glb`.

Pass if it imports as a skeletal mesh facing +Y, with no VRM assets (no spring data or avatar description), and its import report page (E-B1) shows a warning about it.

**E-A4. Reimport.** *(P3.3)*
1. Right-click the VRM 1.0 skeletal mesh → **Reimport**.

Pass if it reimports without errors and the mesh, textures and materials look the same as after E-A1.

**E-A5. Two characters with similar names.** *(P1.15)*
1. Copy the VRM 1.0 file to `Alice.vrm` and `Alice2.vrm`. Import both into `/Game/Test/Names`.
2. Open each character's spring data, IK Rig and Live Link actor.

Pass if each character's assets refer to its own mesh (Alice's to Alice, Alice2's to Alice2).

**E-A6. Pipeline toggle.** *(P1.15)*
1. In **Project Settings → Plugins → VRM Interchange**, make sure spring bone generation is on.
2. Import the VRM 1.0 file into `/Game/Test/NoSprings`; in the import dialog, untick **Spring Bones** (category **VRM Import**).

Pass if no spring data asset is made.

**E-A7. Assets saved by older versions.** *(P3.3, optional)*
1. With a spring data asset saved before #137 (it had `RawJson`), open it.

Pass if it opens and shows its springs.

---

## B. Import report and messages

**E-B1. Notification and message log.** *(P6.3)*
1. Import the VRM 1.0 file into `/Game/Test/Report`.
2. Click **Show in Content Browser** on the notification.
3. Open **Window → Message Log** and choose **VRM Import**.

Pass if one notification appears saying what was imported, Show in Content Browser selects the new assets, and the VRM Import log has a page for the import listing its assets and warnings (or "No warnings."). Repeat with the `.glb`: its page shows its warning. Note in the results whether the listing is called "VRM Import" (CONTRIBUTING and ARCHITECTURE name it so).

**E-B2. Two files at once.** *(P6.3)*
1. Drag the VRM 0.x and VRM 1.0 files into `/Game/Test/Pair` in one drag.

Pass if one notification covers both files and the message log page has a section per file, each with its own warnings.

**E-B3. A warning logged on a worker thread is filed under its own file.** *(the open question from #177's review: thread-local attribution)*

Texture payloads are built on Interchange's worker threads, so a warning logged while decoding a texture shows whether the capture sees worker-thread log lines on the logging thread.

1. Make a copy of a fixture with a damaged texture. From the repository root:
   ```
   python -c "d=open('Plugins/VRMInterchange/Tests/Fixtures/mtoon_materials.vrm','rb').read(); open('BrokenTexture.vrm','wb').write(d.replace(b'\x89PNG', b'\x89PNX', 1))"
   ```
   This changes one byte of the first embedded PNG's signature, so the file still loads but that image can't be decoded.
2. Drag `BrokenTexture.vrm` and the VRM 1.0 file into `/Game/Test/Threads` **in one drag**.
3. Open the VRM Import message log page for this import. Also search the output log for `Could not decode texture`.

Pass if the "Could not decode texture ..." warning appears **in BrokenTexture.vrm's section** and not in the other file's section. If it appears under "Logged during the import, outside any one file's import:", the attribution assumption is wrong (the log line reached the capture on another thread): record it as a Fail. If it appears nowhere on the page but is in the output log, also Fail. Delete `BrokenTexture.vrm` afterwards.

---

## C. Generated assets

**E-C1. Avatar description and licence.** *(P4.1)*
1. After E-A1, open `<Mesh>_Avatar` in each folder.

Pass if it shows the humanoid bone map, the expressions (VRM 0.x: Joy, A, Blink_L, ...; VRM 1.0: happy, aa, blinkLeft, ...), look-at and meta, its Spring Data points at the character's spring data, and a licence notification appeared during import.

**E-C2. IK Rig and retargeting.** *(P4.3)*
1. Import the non-VRoid VRM into `/Game/Test/Custom`.
2. Open its `IK_Rig_VRM_<mesh>`.
3. Create an IK Retargeter from it to the mannequin's IK Rig (`IK_Mannequin`), and preview a mannequin animation.

Pass if the retarget root is the hips, the chains are named like the mannequin's (Spine, Neck, Head, LeftArm, LeftLeg, LeftIndex, ...), every chain maps automatically, and the VRM follows the animation.

**E-C3. Live Link and retarget actors.** *(P3.4)*
1. Open the Live Link actor and the retarget actor in `/Game/Test/V1/.../LiveLink/`.
2. Place the Live Link actor in the level.

Pass if the mesh and AnimBlueprint are set on both and the placed actor shows the character.

**E-C4. Update Existing.** *(P3.4)*
1. Reimport the VRM 1.0 file into the same folder with **Actors: Update Existing**, **IK Rig: Update Existing** and **Spring Bones: Update Existing** ticked.
2. Reimport again with them unticked.

Pass if the first reimport updates the same assets (no new ones appear), and the second creates new assets with unique names.

---

## D. Materials and textures

**E-D1. MToon look.** *(P4.5, D-7)*
1. In the default level (it has an atmosphere sun light), look at both imported VRoid characters.
2. Compare with the three-vrm or UniVRM render of the same models: toon ramp and shade colour, cutout hair and transparent parts, rim, and outlines (both world and screen width, if the models have them).
3. Save all, restart the editor and reopen the level.

Pass if the shading is recognisably the same (record screenshots side by side), transparent and cutout parts sort and clip correctly, outlines appear, and `M_VRM_MToon` and `M_VRM_MToonOutline` in `/Game/VRMInterchange/Materials` load after the restart.

**E-D2. Fallback light.** *(P4.5)*
1. Make an empty level with only a point light and place a character.

Pass if the character is lit from the direction set by the material instance's **FallbackLightDirection** (change it on the character's `MI_VRM_<Name>__MToon` to confirm).

**E-D3. Normal maps.** *(P1.10)*
1. Find an imported normal-map texture (a VRM whose material has one, or the non-VRoid VRM).
2. Check the texture's settings, then apply it to a material on a sphere under a moving light.

Pass if the texture is linear with **Normalmap** compression, and the bumps light from the correct side (not inverted).

---

## E. Morph targets and expressions

**E-E1. Morph target normals.** *(P4.6)*
1. Open the VRM 1.0 skeletal mesh; in the **Morph Target Preview**, set a mouth shape (for example `Fcl_MTH_A`) to 1 under a side light.

Pass if the mouth's shading follows the new shape (no dark or flat patches). A morph without normal deltas keeps its shading.

**E-E2. VRM Expressions node.** *(P4.2)*
1. In the VRM 0.x character's Live Link AnimBP, add a **VRM Expressions** node after the Live Link Pose node, and set its Avatar Description to the character's `<Mesh>_Avatar`. Compile.
2. Stream VSeeFace to the editor (see G), or replay a VSeeFace capture: `python scripts/vmc_sender.py replay <capture>.vmcrec`.
3. Blink, smile and speak.
4. Trigger an expression that overrides blinking (in VSeeFace, an expression hotkey such as Joy or Angry, if the model's expression has overrideBlink).

Pass if the face follows with no curve map on the remapper, and blinking stops while the overriding expression is on.

---

## F. Spring bones

**E-F1. Motion.** *(P2.1, P2.3, P1.12, P1.13)*
1. Place the VRM 0.x and 1.0 Live Link actors and press **Simulate**; move and turn each actor with the gizmo, or play an animation.

Pass if hair and skirts swing when the character moves or turns (no External Velocity needed), whole hair strands move rather than only their roots (VRM 0.x, P1.12), and on VRM 1.0 the tip joints move more freely where the file gives them lower stiffness (P1.13).

**E-F2. Compared with a reference.** *(P2.1)*
1. Record the same model and motion in UniVRM or three-vrm, and in the editor.

Pass if the swing, damping and rest direction look alike. Note which parameter differs most (stiffness has differed most before).

**E-F3. Center bones.** *(P2.1)*
1. With a model whose springs use a center (VRM 1.0 `center`, often the skirt), move the center bone's parent quickly.

Pass if those springs don't lag behind the center.

**E-F4. Robustness in Play.** *(P1.14)*
1. In **Play**, swap the spring data on a running character (for example in the level Blueprint, set the node's Spring Data to the other character's), teleport the character, and change its LOD (`r.ForceLOD 1`, then `r.ForceLOD -1`).

Pass if there's no crash and no stretched chains.

**E-F5. Wrong spring data.** *(P2.3)*
1. In an AnimBP, set the spring node's Spring Data to another character's asset and compile.

Pass if the compiler lists the bones that aren't on the skeleton. Also note whether edits to the node's settings in the AnimBP editor reach the preview (not checked yet).

**E-F6. Old spring data.** *(P1.16, optional)*
1. Open a spring data asset imported before #115.

Pass if it shows **Needs Reimport** and its AnimBlueprint's compile warns about it.

**E-F7. Editing tools.** *(P6.4)*
1. Open a spring data asset while a preview or Simulate runs.
2. In **Editing Tools**, scale stiffness by 2 and click **Apply**; reset spring 0 with **Reset Spring to File Values**; click **Reimport from Source**. Undo each (Ctrl+Z).

Pass if each change shows in the running preview without recompiling, each undo restores the previous values, and Reimport from Source posts a message log page.

---

## G. VMC Live Link

**E-G1. README quick start, from nothing.** *(P7.2 acceptance)*

Ideally done by someone who hasn't used the plugin.
1. In a fresh level, follow only the [VMC Live Link README](../Plugins/VMCLiveLink/README.md#quick-start) Quick Start, using the VRM 1.0 character and `python scripts/vmc_sender.py send --port 39539` as the sender.
2. Note every place where the README was unclear, wrong or missing a step.

Pass if the character moves by the end of step 5. Record specifically whether **Update Animation in Editor** was needed for the placed character to move outside Play, and what else was.

**E-G2. Add-menu locations.** *(P7.2 review)*
1. In the Content Browser, open **Add** (the + button) and look for *VMC LiveLink Mapping Asset* and *VMC Retarget Actor*.

Pass if both are found. Record the category each is under (VMC LiveLink, Animation or Blueprint).

**E-G3. VSeeFace.** *(P1.1, P1.3, P1.5, P4.4, VMC-01)*
1. In VSeeFace, **Settings → General settings**, OSC/VMC protocol section: enable the VMC protocol sender, address `127.0.0.1`, port 39539. Record the exact menu names.
2. With the E-G1 set-up, set the remapper's **Reference Skeleton** to the VRoid VRM 0.x mesh and click **Map Bones From Humanoid Metadata**.
3. Move and turn your head, raise your hands (if hand tracking is on), lean.

Pass if the body follows with no other set-up (P4.4), the head turns the right way (not mirrored), and root motion arrives: lean or step and the hips move. This confirms **VMC-01** (the `Root/Pos` layout from a real sender). Also record a capture for later checks: point VSeeFace at port 39540 and run `python scripts/vmc_sender.py record --listen 39540 --out vseeface.vmcrec` for 30 s, then `python scripts/vmc_sender.py dump vseeface.vmcrec --limit 3` and note the `Root/Pos` argument count.

**E-G4. Receive thread comparison.** *(P3.1, D-1)*
1. With `python scripts/vmc_sender.py send --fps 60` and then with VSeeFace, read the source's status (frame rate and jitter) with **Receive Thread** on, then off.
2. Repeat each with the editor window in the background (another window focused).

Pass if both paths receive; record the four fps and jitter readings.

**E-G5. Live settings edits.** *(P3.1, P6.1)*
While receiving, in the source's details: change the port (and the sender's), rename the subject, reapply a preset on the remapper.

Pass if receiving resumes after each change, the renamed subject appears and the old one goes, and the character keeps moving after pointing its AnimBP at the new subject name.

**E-G6. Status texts and VMC.Stats.** *(P6.1)*
1. Start a second program on port 39539 first (for example `python -c "import socket,time; s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('0.0.0.0',39539)); time.sleep(600)"`), then add a VMC source on 39539.
2. Stop that program, change the source's port to 39540 and back.
3. Start and stop the sender.
4. Run `VMC.Stats` in the console while receiving.

Pass if the status reads *Port 39539 in use* in step 1, *Listening on :39539, waiting for data* after step 2, *Receiving ... fps from 127.0.0.1* while sending, and *No data for N s* after stopping; and VMC.Stats prints per-address rates.

**E-G7. Firewall and a second computer.** *(README troubleshooting)*
1. Send from another computer on the network (VSeeFace or the script with `--host <this PC's IP>`).

Pass if Windows asks to allow Unreal Editor on the first listen, and after allowing it on private networks, data arrives. Record the prompt's wording.

**E-G8. Non-ASCII blend shape names.** *(P3.1)*
1. Use a VRM whose blend shapes have Japanese names in VSeeFace (or a capture of one).

Pass if the Live Mapping table shows the names intact (not `?`).

**E-G9. Lock to First Sender.** *(P4.7)*
1. Turn on **Lock to First Sender**. Start `python scripts/vmc_sender.py send --host 127.0.0.1`.
2. Start a second sender that reaches the editor from another address: `python scripts/vmc_sender.py send --host <this PC's LAN IP>`.

Pass if the status names the first sender, says *ignoring 1 other sender*, and the subject doesn't jump between the two.

**E-G10. Calibration state.** *(P4.7)*
1. In VirtualMotionCapture, send while starting a calibration.

Pass if the source's status shows *sender: calibrating* during calibration and the note goes when it ends.

**E-G11. Devices and camera.** *(P4.7, needs SteamVR hardware)*
1. In VirtualMotionCapture, **Settings → Detailed settings**: enable OSC motion sending. Record the exact menu names.
2. On the source, turn on **Device Subjects** and **Camera Subject**.
3. Add a Live Link Controller component (Transform role) to a cube and pick a tracker subject; and one (Camera role) on a Cine Camera Actor with the camera subject.

Pass if subjects `VMC_Subject_<serial>` and `VMC_Subject_<camera>` appear, the cube follows the tracker, and the camera follows VirtualMotionCapture's camera with its field of view.

**E-G12. Other senders.** *(P7.2 README, third-party claims)*

Pass or Skip each, recording what you found:
- **Warudo:** can it send VMC (built in or through a plug-in)? If so, does the character move?
- **VMC4B / Blender:** does VMC4B only receive? Is there a Blender add-on that sends VMC, and does it work?

---

## H. Remapper

**E-H1. Live Mapping table.** *(P6.2)*
1. Stream VSeeFace. Set the remapper's Reference Skeleton to the VRM 0.x mesh.
2. Map two incoming curves to the same name in the Curve Name Map (for example `Joy` and `Fun` both to `Fcl_ALL_Joy`).
3. Set the Reference Skeleton to the VRM 1.0 mesh (which lacks some VRoid morph targets).
4. Tick **Only names that don't reach the mesh**.

Pass if the table lists every received name, marks the two curves *Another curve has this name too*, marks missing targets *No morph target of this name on the reference mesh*, and the filter shows only those.

**E-H2. Mapping Tools and undo.** *(P3.2)*
1. Click each button in **Mapping Tools**: Apply Preset (VMC / VRoid), Seed From Subject, Map Bones From Humanoid Metadata, Create Mapping Asset..., Save to Mapping Asset, Apply Mapping Asset. Undo after each.

Pass if each does what its tooltip says and each undo restores the maps.

**E-H3. Save, then auto-detect on a fresh subject.** *(P3.2, P7.2 review)*
1. Save a mapping asset for the VRM 0.x mesh (Create Mapping Asset...).
2. Remove the source, add a new one, and on its subject's new remapper set the Reference Skeleton to the same mesh and click **Auto-Detect Mapping**.
3. Save a Live Link preset with that Reference Skeleton set, restart the editor and load the preset.

Pass if Auto-Detect Mapping applies the saved asset in step 2, and the subject from the preset has its maps without clicking anything in step 3.

---

## I. Documentation checks

**E-I1. Editor paths named in the docs.** *(P7.3 review)*

Pass or Fail each:
- **Tools → Test Automation** opens the automation window (CONTRIBUTING, "Run the tests").
- The Message Log listing is called **VRM Import** (checked in E-B1).
- **Project Settings → Plugins → VMC Live Link** has Default Remapper Class and Default Reference Skeleton.
- **Project Settings → Plugins → Live Link → Default Live Link Preset** exists (VMC README, Quick Start).

---

## J. Performance

**E-J1. Large import.** *(P5)*
1. Start Unreal Insights with the memory and CPU channels (`-trace=cpu,memory`), import the large VRoid model (about 60 morph targets).

Pass if the import finishes; record the import time and peak memory, and compare with the benchmark (60 morph-target payloads in about 85 ms).

**E-J2. Spring bones cost.** *(P2.2)*
1. With Insights running, simulate the large model with its springs for 10 s.

Pass if the spring node's time per frame is well under 1 ms; record it (the benchmark: about 0.08 ms for 200 joints and 30 colliders).

---

## Results

Copy this table into the issue or document where you record the run.

| ID | Result | Notes |
|---|---|---|
| E-S1 | | |
| E-A1 | | T-05: facing; T-02: skins per mesh |
| E-A2 | | |
| E-A3 | | |
| E-A4 | | |
| E-A5 | | |
| E-A6 | | |
| E-A7 | | |
| E-B1 | | listing name |
| E-B2 | | |
| E-B3 | | worker-thread attribution |
| E-C1 | | |
| E-C2 | | |
| E-C3 | | |
| E-C4 | | |
| E-D1 | | screenshots |
| E-D2 | | |
| E-D3 | | |
| E-E1 | | |
| E-E2 | | |
| E-F1 | | |
| E-F2 | | parameter that differs most |
| E-F3 | | |
| E-F4 | | |
| E-F5 | | node edits reach preview? |
| E-F6 | | |
| E-F7 | | |
| E-G1 | | Update Animation in Editor needed? |
| E-G2 | | categories |
| E-G3 | | VMC-01: Root/Pos argument count; VSeeFace menu names |
| E-G4 | | fps and jitter, four readings |
| E-G5 | | |
| E-G6 | | |
| E-G7 | | firewall prompt |
| E-G8 | | |
| E-G9 | | |
| E-G10 | | |
| E-G11 | | VirtualMotionCapture menu names |
| E-G12 | | Warudo, VMC4B |
| E-H1 | | |
| E-H2 | | |
| E-H3 | | |
| E-I1 | | |
| E-J1 | | time, peak memory |
| E-J2 | | ms per frame |
