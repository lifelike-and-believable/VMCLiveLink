# VRM Interchange Plugin

**Import VRM avatars into Unreal Engine with full support for Spring Bones, IK Rigs, and Live Link.**

The VRM Interchange plugin is a comprehensive VRM (.vrm) importer for Unreal Engine 5.6+ that leverages the modern Interchange framework. It imports VRM avatars with complete support for skeletal meshes, textures, blend shapes (morph targets), and advanced features like physics-based spring bones, IK rigs, and Live Link integration.

The [User Guide](https://github.com/lifelike-and-believable/VMCLiveLink/blob/main/docs/USER_GUIDE.md) walks through importing an avatar and driving it live with [VMC Live Link](https://github.com/lifelike-and-believable/VMCLiveLink/blob/main/Plugins/VMCLiveLink/README.md), step by step; this README is the reference.

## Features

### Core Import Capabilities
- **VRM Format Support**: Imports VRM 0.x and VRM 1.0 files (glTF 2.0-based avatar format)
- **Skeletal Mesh**: Skeleton built from the skin joints, plus the spring bone nodes below them that no skin lists (spring tails such as VRoid's `J_Sec_*_end`) and any nodes between those and their joint. Such an in-between node can sit above existing joints and become their parent, so reimporting a VRM made with an earlier version can change bone parents as well as add bones. Skin joints keep their names; an added node that shares one is renamed. Bone rest rotations are reset to identity (see [Coordinate System](#coordinate-system))
- **Textures**: Embedded PNG/JPEG textures imported and assigned to material instances, with colour space and compression set by use: base colour, emissive, and the MToon shade, matcap and rim textures are sRGB; normal maps are linear with normal-map compression and the green channel flipped from glTF (+Y) to Unreal (-Y); metallic-roughness, occlusion, and the MToon shading shift and outline width textures are linear masks. An image a material uses in more than one way is imported once per use
- **Blend Shapes**: Morph target support for facial expressions
- **Materials**: MToon (VRM 0.x and 1.0) and unlit materials on a generated toon master material, with shade colour and toon ramp, rim and matcap, emission, alpha mode and cutoff, double-sided, texture transform, and outlines. glTF PBR materials get material instances with their textures. See [Materials](#materials)

### Spring Bones (Physics Hair/Clothing)
- **Automatic Spring Data Generation**: Parses VRM spring bone configurations into data assets
- **Real-time Physics Simulation**: Verlet-style spring bone solver (see [Spring Bone Simulation Details](#spring-bone-simulation-details) for current limitations)
- **Collider Support**: Sphere, capsule and plane colliders, including inside colliders
- **Animation Blueprint Integration**: Optional post-process AnimBP for automatic spring simulation
- **Customizable Parameters**: Stiffness, drag, gravity, hit radius and colliders

### IK Rig Integration
- **IK Rig from the humanoid map**: The retarget root and chains come from the VRM's humanoid bone map, so they fit whatever the bones are called
- **Mannequin chain names**: Chains are named like the UE5 mannequin's IK Rig (Spine, Neck, Head, LeftArm, LeftLeg, LeftIndex, ...), so an IK Retargeter maps them automatically
- **Preview Mesh Assignment**: Automatically configures preview meshes

### Live Link Support
- **Character Scaffold Generation**: Creates ready-to-use Actor and AnimBP blueprints
- **VMC Protocol Ready**: Compatible with VMC Live Link for real-time performance capture
- **Retargeting Support**: Includes retargeting actor setup for animation transfer

## Installation

### As a Plugin in Your Project

1. Copy the `VRMInterchange` plugin folder to your project's `Plugins` directory:
   ```
   YourProject/Plugins/VRMInterchange/
   ```

2. Right-click your `.uproject` file and select **Generate Visual Studio project files**

3. Open your project in Unreal Engine. The plugin will be enabled automatically.

4. If prompted, allow Unreal to rebuild the plugin modules.

5. **Register the VRM import pipelines.** While they aren't registered (or after a plugin update changes them), the editor shows a notification when it starts. Click **Register**, or use **Project Settings > Plugins > VRM Interchange > Register VRM Import Pipelines** at any time. **Don't ask again** stops the notification. Registering edits only the VRM translator's entry in **Project Settings > Interchange** (Assets pipeline stack): it puts the plugin's assets pipeline first in place of the generic one, adds the spring bone, IK Rig, Live Link, material and avatar description pipelines, and shows the import dialog for VRM textures. Then it saves that setting. The plugin does not change your project settings unless you ask it to.

### Requirements

- **Unreal Engine**: 5.6 (the version it is built and tested with; later versions are untested)
- **Platform**: Windows (Win64) only, for now: that is the only platform the plugin is built and tested on (the CI runner is Windows). The editor modules are allowlisted for Win64 in the `.uplugin`; the runtime modules have no platform list. Nothing in the code is known to be Windows-specific; Mac and Linux can be added once they are built and tested.
- **Dependencies**: 
  - Interchange (built-in)
  - InterchangeEditor (built-in)
  - IKRig (built-in)
  - LiveLink (built-in)

## Quick Start

### Importing a VRM File

1. **Drag and Drop**: Drag a `.vrm` file into the Content Browser
2. **Import Dialog**: The Interchange import dialog will appear with VRM-specific pipelines
3. **Configure Options**: 
   - Keep default settings for standard import
   - Enable/disable Spring Bones, IK Rig, or Live Link features as needed
4. **Import**: Click **Import** to complete the process

### What Gets Created

After importing `<FileName>.vrm`, you'll find:

```
<import folder>/
└── <FileName>/
    ├── <Mesh>_Avatar (Avatar description: humanoid bone map, expressions, look-at, licence)
    ├── SkeletalMeshes/
    │   ├── <Mesh> (Skeletal Mesh)
    │   ├── <Mesh>_Skeleton
    │   └── <Mesh>_PhysicsAsset
    ├── Materials/
    │   ├── MI_VRM_<FileName>_<Material> (one per VRM material)
    │   ├── MI_VRM_<FileName>__MToon (shared parent of the MToon and unlit materials)
    │   ├── MI_VRM_<FileName> (shared parent of the glTF PBR materials)
    │   └── MI_VRM_<FileName>__Outline (overlay material, when MToon outlines are used)
    ├── Textures/
    ├── SpringBones/
    │   ├── <Mesh>_SpringData (Spring configuration)
    │   └── PP_ABP_VRMSpringBones_<Mesh> (Post-process AnimBP that runs the springs)
    ├── IKRigDefinition/
    │   └── IK_Rig_VRM_<Mesh> (IK Rig asset)
    └── LiveLink/
        ├── BP_LL_VRM_<Mesh> (Live Link character actor)
        ├── BP_LL_VRM_To_UE5_<Mesh> (Retarget actor: the character's Live Link pose on a UE5 mannequin you supply)
        └── Animation/
            └── ABP_LL_VRM_<Mesh> (Live Link AnimBP)

/Game/VRMInterchange/Materials/ (shared by every VRM import; see Materials)
```

`<Mesh>` is the skeletal mesh's name, which is the file name. The shared parent material instances are made only when a material uses them. Folder names are the defaults; each pipeline has a folder option in the import dialog.

## Configuration

### Project Settings

Configure default import behavior in **Edit → Project Settings → Plugins → VRM Interchange**:

These are the defaults the import dialog's VRM options start from.

#### Spring Bones Settings
- **Generate Spring Bone Data**: Parse and create spring bone data assets during import (default: enabled)
- **Generate Post Process AnimBP**: Create a post-process AnimBP (`PP_ABP_VRMSpringBones_<Mesh>`) that runs the springs (default: disabled, but see the note below)
- **Assign Post Process ABP**: Set that AnimBP as the skeletal mesh's post-process AnimBP, so the springs run wherever the mesh is used (default: disabled, but see the note below)
- **Overwrite Existing Spring Assets**: On re-import, update the existing spring data asset in place, so AnimBlueprints that use it keep working (default: disabled, but see the note below; otherwise a new asset with a unique name is created)
- **Overwrite Existing Post Process ABP** / **Reuse Post Process ABP On Reimport**: If `PP_ABP_VRMSpringBones_<Mesh>` already exists, reuse it and give it the new spring data; your graph edits are kept. Either one turns this on. With both off, a new AnimBP with a unique name is made (defaults: disabled / enabled, so the existing AnimBP is reused)

**Note:** the plugin's spring bone pipeline asset turns on **Generate Post Process AnimBP**, **Assign Post Process ABP** and **Update Existing** whatever these settings say, so imports create and assign the post-process AnimBP and update the spring data in place. The IK Rig pipeline asset likewise turns on its **Update Existing**. Change these per import in the import dialog (**Spring Bones: Post-Process AnimBlueprint**, **Spring Bones: Assign Post-Process AnimBlueprint**, **Spring Bones: Update Existing**, **IK Rig: Update Existing**). See [Known Limitations](#known-limitations).

#### IK Rig Settings
- **Generate IK Rig Assets**: Create an IK Rig for each imported character, built from its humanoid map (default: enabled)

#### Live Link Settings
- **Generate Live Link Actor Scaffold**: Create the Live Link actor and AnimBP (default: enabled)

#### Avatar Description Settings
- **Generate Avatar Description**: Create the `<Mesh>_Avatar` asset (humanoid map, expressions, licence) and write the humanoid map onto the mesh (default: enabled)

#### Import Pipelines Settings
- **Prompt To Register Import Pipelines**: When the VRM pipelines aren't registered, offer to register them when the editor starts (default: enabled)
- **Register VRM Import Pipelines**: Registers them now

### Per-Import Settings

During each import, you can override project settings in the Interchange import dialog:

1. Expand the VRM pipelines in the import dialog. Their options are in the **VRM Import** category: **Spring Bones**, **IK Rig**, **Live Link Actor**, **Retarget Actor**, **Avatar Description** and the **Materials** options. Each tooltip says which assets it creates and in which folder.
2. These override the project defaults for this import only.

### After Import

When the import finishes, one notification lists:
- the assets the VRM pipelines created;
- those updated in place from the file (what refers to them keeps working; edits to them are replaced);
- those only pointed at the new mesh (existing Actor and Animation Blueprints; your edits are kept);
- the avatar's licence and usage permissions.

**Show in Content Browser** selects the assets. **Show N problems** (or **Show import log** when there are none) opens the **VRM Import** message log. It has one page for each report: normally one import, or several files imported together. For each file, the page lists every warning and error that import logged: unresolved bones, lenient spring layouts, non-VRM glTF files, and so on.

## Using Spring Bones

### Automatic Setup (Recommended)

1. Keep **Spring Bones**, **Spring Bones: Post-Process AnimBlueprint** and **Spring Bones: Assign Post-Process AnimBlueprint** ticked in the import dialog (all on by default; see the note under [Spring Bones Settings](#spring-bones-settings))
2. Import your VRM file
3. The mesh's post-process AnimBP (`PP_ABP_VRMSpringBones_<Mesh>`) then runs the springs wherever the mesh is used, including under the Live Link AnimBP

Without the post-process AnimBP, the spring data is made but nothing runs it: the generated Live Link AnimBP has no spring bone node. Add one yourself (see Manual Setup).

### Editing Spring Data

Open a spring data asset (`<Mesh>_SpringData`). Changes to it reach running previews and PIE at once, without recompiling the AnimBlueprint. Its **Editing Tools** section has:
- **Scale All:** multiplies every joint's stiffness, drag and gravity by the factors given. Only the quantities whose factor isn't 1 change.
- **Reset Spring to File Values:** puts one spring and its joints back to the values the VRM file gave them. Assets imported before this tool existed need a reimport first.
- **Reimport from Source:** reads the source VRM again and replaces the springs, joints and colliders. Its warnings go to the VRM Import message log.

Each tool can be undone.

### Manual Setup

If you prefer manual control:

1. Import with only **Generate Spring Bone Data** enabled
2. Open your character's Animation Blueprint
3. Add a **VRM Spring Bones** node in the AnimGraph
4. Connect it to your pose chain
5. In the node details, set:
   - **Spring Data**: Select the generated `_SpringData` asset
   - **Enable**: Check to activate simulation

### Spring Bone Parameters

The `VRMSpringBoneData` asset contains:
- **Joints**: stiffness, drag, gravity direction and power, and hit radius for every joint. The solver reads these. VRM 1.0 files set them per joint; VRM 0.x files set them per bone group, and every joint of the group gets a copy.
- **Springs**: the chains of joints, with their collider groups and center. A VRM 0.x bone group lists only the root bone of each chain; the importer adds every bone below it, and each branch becomes a chain of its own with the group's settings. A spring's stiffness, drag, gravity and hit radius fields are editing helpers: changing one applies it to every joint of that spring.
- **Node Hierarchy**: Bone parent-child relationships
- **Colliders**: Sphere, capsule and plane collision volumes. For VRM 1.0 colliders with the `VRMC_springBone_extended_collider` extension, the extended shape (including inside colliders and planes) replaces the base shape.

Edit these values in the Data Asset to fine-tune spring behavior.

Some VRM 1.0 files use layouts from before the spec was final: a collider `shapes` array, parameters on the spring instead of the joints, `drag` instead of `dragForce`. These are still read for now, and each use is logged. Set `vrm.SpringBones.LenientSchema 0` to ignore them and import only what the spec defines.

## Using IK Rigs

The generated IK Rig is for retargeting: use it with an IK Retargeter to transfer animations between this character and other skeletons.

### Basic Usage

1. Open the generated `IK_Rig_VRM_<Mesh>` asset
2. The retarget root is the hips, and there is one chain per clavicle, limb, finger, spine, neck and head that the VRM's humanoid map covers. Chains whose bones the file doesn't map (no fingers, say) are left out, and a chain whose bones the skeleton lacks is skipped with a warning in the Output Log.
3. Create an IK Retargeter from this rig to the UE5 mannequin's IK Rig: the chain names match, so the chains map automatically

The rig has retarget chains only, no IK goals or solvers. A file without a humanoid map (or whose map has no hips bone), or an import with **IK Rig: Build From Humanoid Map** unticked, gets a copy of the template IK Rig instead, whose chains assume VRoid bone names (`J_Bip_*`).

## Using Live Link

### Automatic Scaffold

When **Generate Live Link Actor Scaffold** is enabled, the plugin creates:
- **Character Actor BP** (`BP_LL_VRM_<Mesh>`): the character with its skeletal mesh, driven by Live Link
- **Animation BP** (`ABP_LL_VRM_<Mesh>`): a Live Link Pose AnimBlueprint

The **Retarget Actor** option (on by default, import dialog only) also creates `BP_LL_VRM_To_UE5_<Mesh>`, which retargets the character's Live Link pose to a UE5 mannequin. The plugin doesn't ship the mannequin: place the actor, then set its **UE5 Character** to a mannequin mesh in your project (such as `SKM_Manny`) and its **Subject** as below. Its retargeter assumes VRoid bone names (`J_Bip_*`); for other avatars, retarget with the generated IK Rig instead.

### Setting Up VMC Protocol

1. Place the generated `BP_LL_VRM_<Mesh>` actor in your level
2. Open **Window → Virtual Production → Live Link**
3. Add a **VMC Live Link Source** (requires VMCLiveLink plugin)
4. Configure your external VMC application to send to Unreal's IP and port
5. Select the placed actor and set its **Subject** (in the Details panel) to the source's subject name, `VMC_Subject` by default. It starts empty, so the actor follows no subject until you set it.
6. The character will animate in real-time with incoming motion data

The [VMC Live Link README](https://github.com/lifelike-and-believable/VMCLiveLink/blob/main/Plugins/VMCLiveLink/README.md) covers sender setup, the source settings, the remapper and troubleshooting. The [User Guide](https://github.com/lifelike-and-believable/VMCLiveLink/blob/main/docs/USER_GUIDE.md) walks through the whole setup.

### Customizing the Live Link Setup

The generated blueprints are templates you can extend:
- Add custom animation layers in the AnimBP
- Modify the character actor for gameplay logic
- Adjust Live Link subject selection and bone mapping

### Driving Facial Expressions

VMC senders send expressions (blend shapes) by name, such as `Joy`, `A` or `Blink_L` (VRM 0.x) or `happy`, `aa` or `blinkLeft` (VRM 1.0). The **VRM Expressions** AnimGraph node turns those curves into the avatar's morph target curves:

1. In the AnimBP, add a **VRM Expressions** node after the Live Link pose node.
2. Set its **Avatar Description** to the character's `<Mesh>_Avatar` asset.

Either version's names drive either version's avatar. The node follows the VRM 1.0 rules: binary expressions snap to 0 or 1, and an expression that overrides blink, look-at or mouth reduces those expressions while it is active. Curves that aren't expressions pass through unchanged. If a VMC remapper renames the expression curves (its curve map), the node sees the new names, so leave expression names unmapped when you use this node. The generated Live Link AnimBP doesn't contain this node yet; add it by hand.

### Humanoid Map Metadata (for VMC Live Link)

When the avatar description is generated, the importer also writes the humanoid map onto the skeletal mesh as editor metadata, so VMC Live Link (a separate plugin) can map a VMC stream onto the mesh without either plugin depending on the other:

| Key | Value |
|---|---|
| `VRM.Humanoid.<UnityBoneName>` | The skeleton bone for that humanoid bone, e.g. `VRM.Humanoid.LeftUpperArm` = `J_Bip_L_UpperArm` |
| `VRM.HumanoidVersion` | `1`, written only when at least one humanoid bone is mapped. A mesh without a humanoid map gets no keys at all, so a missing version just means "no map". |

Keys use Unity `HumanBodyBones` names (what VMC senders stream). VRM 1.0's thumb is one joint off from Unity's: `leftThumbMetacarpal` is written as `LeftThumbProximal`, and `leftThumbProximal` as `LeftThumbIntermediate`. A reimport replaces the keys. Other tools can write the same keys to make any skeletal mesh mappable.

## Materials

VRM materials are imported as material instances. Which master material they use depends on the material:

| VRM material | Master | Notes |
|---|---|---|
| MToon (`VRMC_materials_mtoon`, or VRM 0.x shader `VRM/MToon`) | `M_VRM_MToon` | VRM 0.x values are converted to VRM 1.0 ones the way UniVRM migrates 0.x files |
| Unlit (`KHR_materials_unlit`, or VRM 0.x `VRM/Unlit*`) | `M_VRM_MToon` with **UnlitShading** on | Base colour and emission, no lighting |
| glTF PBR (and `VRM_USE_GLTFSHADER`) | the plugin's `M_VRM_Master` | Textures only |

`M_VRM_MToon`, `M_VRM_MToonOutline` and the texture they use, `T_VRM_MToonWhiteMask`, are built by the plugin (in C++, since a plugin can't ship them otherwise) the first time a VRM with MToon or unlit materials is imported, in `/Game/VRMInterchange/Materials`. Save them with the imported assets. A newer plugin version rebuilds them in place if their graph changed; don't edit them, edit the instances.

`M_VRM_MToon` follows the MToon 1.0 lighting model:

- **Shade and toon ramp.** The lit and shade colours are mixed by `ShadingShiftFactor` (plus the shading shift texture) and `ShadingToonyFactor`.
- **Rim and matcap.** Parametric rim (colour, fresnel power, lift, lighting mix, rim mask) and the matcap texture.
- **Emission**, the normal map, and the base colour texture's texture transform, which applies to every texture.
- **Lighting.** It is an unlit material that computes its own toon lighting from the level's **atmosphere sun light** (a directional light with *Atmosphere Sun Light* on, as in the default level) and the sky's scattered light. In a level without one, it uses the **FallbackLightDirection** and **FallbackLightColor** parameters. It doesn't receive shadows, and point and spot lights don't light it.
- **Alpha.** Each instance's alpha mode and double-sidedness become its blend mode (opaque, masked or translucent) and two-sided overrides; the cutoff is the `AlphaCutoff` parameter.

The per-material instances are parented to the character instance of their master (`MI_VRM_<Name>__MToon` or `MI_VRM_<Name>`), so you can tune shared parameters, such as the fallback light, in one place.

**Outlines.** When MToon materials have outlines, `MI_VRM_<Name>__Outline` (on `M_VRM_MToonOutline`) is set as the skeletal mesh's **Overlay Material**. It draws the back faces pushed out along the normal, in metres (world mode) or as a fraction of the screen height (screen mode). A mesh has one overlay material, so the outline uses the settings of the material with the widest outline and covers the whole mesh. Clear the mesh's overlay material, or turn off **Materials: MToon Outline** in the import dialog, to remove it.

## Advanced Topics

### Re-importing VRM Files

When re-importing, each generated asset is updated in place only if its **Update Existing** option is on in the import dialog (Spring Bones, IK Rig, Actors, Avatar Description). **Actors: Update Existing** is off by default; the others are on. Otherwise a new copy with a unique name (`_1`, `_2`, ...) is made and the old one is left alone. Updated in place:
1. Spring data and the avatar description are replaced from the file (edits to them are lost).
2. The IK Rig is rebuilt.
3. The actors and AnimBlueprints are pointed at the new mesh, and your graph edits are kept.

The mesh, materials and textures are reimported by Interchange. A reimport can add bones (see Core Import Capabilities).

Spring data assets record the version of the importer that wrote them. After a plugin update that changes how spring data is converted, older assets are flagged **Needs Reimport** (shown in the asset's details, logged when the asset loads, and reported as a warning when an AnimBlueprint that uses it compiles). The springs still run, but may not match a fresh import. Reimport the VRM file to regenerate them. Re-saving an old asset does not clear the flag.

### Spring Bone Simulation Details

The spring bone solver follows the VRM 1.0 reference (UniVRM, three-vrm):
- Each joint's tail moves with inertia (reduced by drag), stiffness back toward the animated pose, gravity and the node's **External Velocity**, then is held at the bone's length and pushed out of the spring's sphere, capsule and plane colliders
- Joints are solved root to tip, and each joint's head follows its parent's simulated rotation
- Tails live in world space, so moving or turning the character adds inertia. A spring with a `center` bone keeps its tails in that bone's space instead, so the center's motion adds none
- The simulation runs in fixed 60 Hz steps, so it behaves the same at any frame rate. A frame longer than 0.1 s is simulated as 0.1 s
- In VRM 1.0 the last joint of a spring only marks the tail and isn't rotated. VRM 0.x chains end in a 7 cm virtual tail
- Only rotations are written, so bone lengths never change
- A collider on a bone the skeleton doesn't have is ignored

The node's **Simulation** settings change this: **Simulation Space** (World, or Component to ignore the character's motion), **Use Spring Centers**, **Substep Hz** (default 60) and **Max Delta Time** (default 0.1 s). Its **Debug** settings draw that node's colliders and joints, like the console commands below.

Some exporters give every spring a `center` (VRoid Studio uses the `Root` bone), so moving or turning the character doesn't swing anything. To have it swing them, turn off **Use Spring Centers** and leave **Simulation Space** on World. If **External Velocity** is fed from the character's movement, set **External Velocity Scale** to 0 as well, or movement counts twice.

Compiling the AnimBlueprint warns when the node has no spring data, when the data needs a reimport, and when the data names bones the skeleton doesn't have (usually spring data from another character). At runtime those joints and colliders are skipped, with one log warning per asset.

The solver itself (`FVRMSpringSolver`) has no anim graph dependencies and is tested on its own.

Debug visualization is available via console commands:

*To access the console in Unreal Engine, press the tilde key (`~`) in the editor, or open the Output Log window and enter commands there.*
```
vrm.SpringBones.DrawColliders 1    // Enable collider debug draw
vrm.SpringBones.DrawColliders 0    // Disable collider debug draw
vrm.SpringBones.DrawSprings 1      // Draw each joint's head and tail
vrm.SpringBones.DrawSprings 2      // Also draw each tail's velocity
vrm.SpringBones.DrawSprings 3      // Also draw the rest target
vrm.SpringBones.DrawSprings 0      // Disable spring debug draw
```

These commands, and the node's Debug settings, are not available in Shipping or Test builds.

## Troubleshooting

The plugin logs to `LogVRMInterchange` (import, materials, IK Rig, Live Link), `LogVRMSpring` (spring bone import), `LogVRMSpringBones` (the spring bone node) and `LogVRMSpringData` (spring data assets). Filter the Output Log by these. Import warnings are also in the **VRM Import** message log.

### VRM Pipelines Missing From the Import Dialog
- Run **Project Settings > Plugins > VRM Interchange > Register VRM Import Pipelines**

### Materials Import Without Textures, or No Avatar Description
- Symptoms: every material slot shows the default grid material, the Output Log has `No parent material was found` and `Cannot generate a pipeline instance because the pipeline asset /Script/VRMInterchangeEditor... type is unknown`, and no avatar description asset is made.
- Projects registered before the pipelines shipped as assets (1.0.0 and earlier builds) have the material and avatar description pipelines saved as class paths, which Interchange can't load. The editor offers to register the pipelines again when it starts. Click **Register** (or run **Project Settings > Plugins > VRM Interchange > Register VRM Import Pipelines**), then reimport the VRM file.

### Import Dialog Doesn't Appear
- Ensure the Interchange and InterchangeEditor plugins are enabled
- Check that the file has a `.vrm` extension
- Verify the VRM file is valid (not corrupted)

### Spring Bones Not Animating
- Check that the VRM Spring Bones AnimGraph node is enabled
- Verify the Spring Data asset is assigned
- Ensure the Spring Data asset is not empty (its **Springs** and **Joints** lists)
- If the springs run but don't swing when the character moves or turns, the file's springs probably name a `center` bone (VRoid Studio files do): see [Spring Bone Simulation Details](#spring-bone-simulation-details)
- Confirm bone names in Spring Data match your skeleton
- If the Spring Data asset shows **Needs Reimport**, it was made by an older plugin version (colliders and gravity in the old axes, or VRM 0.x chains without their descendant bones). Reimport the VRM file

### IK Rig Not Generated
- Enable **Generate IK Rig Assets** in project settings
- Ensure IKRig plugin is enabled in your project
- Check the Output Log for `IK Rig for '<mesh>'` warnings (chains whose bones aren't in the skeleton) or a warning that the humanoid map has no hips bone (the template is copied instead)
- With **IK Rig: Build From Humanoid Map** off, or for a file without a humanoid map, check that the template IK Rig asset (`/VRMInterchange/Animation/IK_Rig_VRMTemplate`) exists

### Live Link Actor Missing Components
- Verify LiveLink plugin is enabled
- Check that template blueprints exist in plugin Content folder
- Review the Output Log for any blueprint compilation errors

### Textures Import as Black
- VRM files use embedded or referenced textures
- Check that the VRM file contains valid image data
- Ensure textures are not in an unsupported format

### Mesh Orientation Wrong
- VRM uses a different coordinate system than UE5
- The importer automatically applies necessary transforms
- If orientation is still incorrect, check the VRM file's scene root transforms

## Technical Details

### Architecture

The plugin consists of five modules:

1. **VRMCore** (Runtime)
   - `FVRMDocument`: a .vrm/.glb/.gltf file read and parsed once (JSON, nodes, version, and geometry through cgltf); an import reads the file from this document instead of opening it again
   - `VRM::BuildParsedModel`: skeleton, mesh, morph targets, images and materials in Unreal space
   - `UVRMAvatarDescription` and the avatar parser (humanoid map, expressions, look-at, meta)
   - `FAnimNode_VRMExpressions`: the VRM Expressions anim node
   - The glTF to Unreal coordinate conversion; the only module that uses cgltf

2. **VRMInterchange** (Runtime)
   - `UVRMTranslator`: Interchange translator for .vrm files
   - Generates Interchange node graphs from the parsed model
   - Spring bone parser

3. **VRMInterchangeEditor** (Editor)
   - Post-import pipelines for spring bones, IK Rig, Live Link, materials (including the generated MToon materials) and the avatar description
   - Pipeline registration and project settings
   - The import notification and VRM Import message log
   - The spring data asset's Editing Tools

4. **VRMSpringBonesRuntime** (Runtime)
   - `FAnimNode_VRMSpringBones`: Animation node for spring simulation
   - `UVRMSpringBoneData`: Data asset for spring configuration
   - Physics solver implementation

5. **VRMSpringBonesEditor** (UncookedOnly)
   - `UAnimGraphNode_VRMSpringBones` and `UAnimGraphNode_VRMExpressions`: AnimGraph node wrappers, with the spring node's compile-time checks

### File Format

VRM is a 3D avatar format based on glTF 2.0, specifically designed for VR applications. It includes:
- Standard glTF structure (meshes, materials, textures)
- VRM-specific extensions for humanoid avatars
- Spring bone physics metadata
- Blend shape clip definitions

### Coordinate System

glTF (and so VRM) uses Y-up, right-handed coordinates in metres. UE uses Z-up, left-handed coordinates in centimetres. The importer's net conversion is:

```
Positions and morph deltas:  UE (X, Y, Z) = glTF (X, Z, Y) * 100
Normals:                     UE (X, Y, Z) = glTF (X, Z, Y)
```

This swaps Y and Z, which also converts handedness. VRM 1.0 models face +Z in glTF, which becomes +Y in UE, the direction the UE mannequin faces. VRM 0.x models face −Z, so they also get a 180° turn about UE Z and face +Y too. Files without VRM extensions import as generic glTF with the VRM 1.0 facing. Spring colliders and gravity use the same conversion as the mesh. Bone rest rotations are then reset to identity, so each bone's local transform is a pure translation. This matches how VMC streams local rotations, but it means bone orientations differ from the source file.

## Known Limitations

- **Import settings**: The plugin's spring bone pipeline asset turns on **Generate Post Process AnimBP**, **Assign Post Process ABP** and **Update Existing**, and the IK Rig pipeline asset its **Update Existing**, whatever the project settings say. Change them per import in the import dialog (**Spring Bones: Post-Process AnimBlueprint**, **Spring Bones: Assign Post-Process AnimBlueprint**, **Spring Bones: Update Existing**, **IK Rig: Update Existing**).
- **Materials**: MToon is basic (see [Materials](#materials)): no received shadows, only the atmosphere sun light lights it, one outline per mesh, and no UV animation, render queue offsets or transparent z-write. glTF PBR factors (base colour, emissive, metallic, roughness), alpha mode and double-sided are not applied to PBR materials. `COLOR_0` vertex colours are not imported (MToon ignores them).
- **Morph Targets**: Morph targets are imported by name. Targets with the same name are one morph target: a mesh's primitives share them, and two meshes that use the same name are merged, with a warning, because an expression bound to either would move both. An unnamed target is named `<MeshName>_morph_<index>` and kept to its own mesh. A target's NORMAL deltas, when the file has them, turn its normals; a target without them keeps the base normals, so it changes shape but not shading.
- **Texture Formats**: Embedded textures must be PNG or JPEG

See the [CHANGELOG](https://github.com/lifelike-and-believable/VMCLiveLink/blob/main/CHANGELOG.md) for what changes between versions.

## Support and Contribution

This plugin is part of the VMCLiveLink project. For issues, feature requests, or contributions:
- GitHub: [lifelike-and-believable/VMCLiveLink](https://github.com/lifelike-and-believable/VMCLiveLink)
- Issues: Use the GitHub issue tracker

## License

Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.

## Credits

- **Created by**: Lifelike & Believable Animation Design, Inc.
- **VRM Format**: Developed by VRM Consortium
- **glTF Parsing**: Uses cgltf library by Johannes Kuhlmann

## Additional Resources

- [VRM Format Specification](https://github.com/vrm-c/vrm-specification)
- [Unreal Engine Interchange Documentation](https://docs.unrealengine.com/5.6/en-US/interchange-framework-in-unreal-engine/)
- [IK Rig Documentation](https://docs.unrealengine.com/5.6/en-US/ik-rig-in-unreal-engine/)
- [Live Link Documentation](https://docs.unrealengine.com/5.6/en-US/live-link-in-unreal-engine/)
