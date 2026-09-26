# VRM Interchange Plugin

**Import VRM avatars into Unreal Engine with full support for Spring Bones, IK Rigs, and Live Link.**

The VRM Interchange plugin is a comprehensive VRM (.vrm) importer for Unreal Engine 5.6+ that leverages the modern Interchange framework. It imports VRM avatars with complete support for skeletal meshes, textures, blend shapes (morph targets), and advanced features like physics-based spring bones, IK rigs, and Live Link integration.

## Features

### Core Import Capabilities
- **VRM Format Support**: Imports VRM 0.x and VRM 1.0 files (glTF 2.0-based avatar format)
- **Skeletal Mesh**: Skeleton built from the skin joints. Bone rest rotations are reset to identity (see [Coordinate System](#coordinate-system))
- **Textures**: Embedded PNG/JPEG textures imported and assigned to material instances, with colour space and compression set by use: base colour and emissive are sRGB; normal maps are linear with normal-map compression and the green channel flipped from glTF (+Y) to Unreal (-Y); metallic-roughness and occlusion are linear masks. An image a material uses in more than one way is imported once per use
- **Blend Shapes**: Morph target support for facial expressions
- **Materials**: Basic material instances with texture assignments (glTF factors and MToon parameters are not yet applied)

### Spring Bones (Physics Hair/Clothing)
- **Automatic Spring Data Generation**: Parses VRM spring bone configurations into data assets
- **Real-time Physics Simulation**: Verlet-style spring bone solver (see [Spring Bone Simulation Details](#spring-bone-simulation-details) for current limitations)
- **Collider Support**: Sphere, capsule and plane colliders, including inside colliders
- **Animation Blueprint Integration**: Optional post-process AnimBP for automatic spring simulation
- **Customizable Parameters**: Stiffness, gravity, damping, and more

### IK Rig Integration
- **Automatic IK Rig Setup**: Generates IK Rig assets by duplicating a template
- **Preview Mesh Assignment**: Automatically configures preview meshes
- **VRM-to-UE5 Bone Mapping**: The template's retarget chains assume VRoid-style bone names (`J_Bip_*`)

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

5. **Register the VRM import pipelines.** The first time the editor starts with the plugin, a notification offers to register them. Click **Register**, or use **Project Settings > Plugins > VRM Interchange > Register VRM Import Pipelines** at any time. This adds the spring bone, IK Rig, Live Link and material pipelines to the `.vrm` entry in **Project Settings > Interchange** and saves that setting. The plugin does not change your project settings unless you ask it to.

### Requirements

- **Unreal Engine**: 5.6 or later
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

After import, you'll find:

```
Content/
└── YourCharacterName/
    ├── YourCharacterName_SkeletalMesh (Skeletal Mesh)
    ├── YourCharacterName_Skeleton (Skeleton asset)
    ├── YourCharacterName_PhysicsAsset (Physics Asset)
    ├── Materials/ (Generated materials)
    ├── Textures/ (Imported textures)
    ├── SpringBones/
    │   ├── YourCharacterName_SpringData (Spring configuration)
    │   └── ABP_SpringBones_YourCharacterName (Optional AnimBP)
    ├── IKRigDefinition/
    │   └── IK_Rig_VRM_YourCharacterName (IK Rig asset)
    └── LiveLink/
        ├── BP_LL_YourCharacterName (Character Actor)
        ├── Animation/
        │   └── ABP_LL_YourCharacterName (Live Link AnimBP)
        └── RetargetActor_YourCharacterName (Retargeting actor)
```

## Configuration

### Project Settings

Configure default import behavior in **Edit → Project Settings → Plugins → VRM Interchange**:

#### Spring Bones Settings
- **Generate Spring Bone Data**: Parse and create spring bone data assets during import (default: enabled)
- **Generate Post Process AnimBP**: Create and assign an AnimBP for spring simulation (default: disabled)
- **Assign Post Process ABP**: Automatically assign the generated AnimBP to the skeletal mesh (default: disabled)
- **Overwrite Existing Spring Assets**: Replace existing assets on re-import (default: disabled)
- **Overwrite Existing Post Process ABP**: Replace existing AnimBPs on re-import (default: disabled)
- **Reuse Post Process ABP On Reimport**: Reuse existing AnimBP when re-importing (default: enabled)

#### IK Rig Settings
- **Generate IK Rig Assets**: Create IK Rig definitions from templates (default: enabled)

#### Live Link Settings
- **Generate Live Link Actor Scaffold**: Create Actor and AnimBP scaffolds for Live Link (default: enabled)

### Per-Import Settings

During each import, you can override project settings in the Interchange import dialog:

1. Expand the pipeline sections in the import dialog
2. Adjust settings for **VRM Spring Bones**, **VRM IK Rig**, and **VRM Live Link** pipelines
3. These override the project defaults for this import only

## Using Spring Bones

### Automatic Setup (Recommended)

1. Enable **Generate Spring Bone Data** in project settings
2. Optionally enable **Generate Post Process AnimBP** and **Assign Post Process ABP**
3. Import your VRM file
4. The spring bone system will be automatically configured and applied

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

The generated IK Rig assets enable:
- **Animation Retargeting**: Transfer animations between different skeletons
- **IK Chains**: Inverse kinematics for procedural animation
- **Runtime Poses**: Control character poses programmatically

### Basic Usage

1. Open the generated `IK_Rig_VRM_YourCharacterName` asset
2. The rig is pre-configured with VRM bone chains
3. Use with IK Retargeter assets to transfer animations
4. Reference in your Animation Blueprints for IK control

## Using Live Link

### Automatic Scaffold

When **Generate Live Link Actor Scaffold** is enabled, the plugin creates:
- **Character Actor BP**: Ready-to-place character with skeletal mesh
- **Animation BP**: Live Link-enabled animation blueprint
- **Retarget Actor**: For animation retargeting workflows

### Setting Up VMC Protocol

1. Place the generated `BP_LL_YourCharacterName` actor in your level
2. Open **Window → Live Link**
3. Add a **VMC Live Link Source** (requires VMCLiveLink plugin)
4. Configure your external VMC application to send to Unreal's IP and port
5. The character will animate in real-time with incoming motion data

### Customizing the Live Link Setup

The generated blueprints are templates you can extend:
- Add custom animation layers in the AnimBP
- Modify the character actor for gameplay logic
- Adjust Live Link subject selection and bone mapping

## Advanced Topics

### Re-importing VRM Files

When re-importing:
1. Existing assets are updated based on overwrite settings
2. Spring bone data is regenerated from the VRM file
3. Manual edits to generated blueprints are preserved
4. Texture and material updates are applied

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

The node's **Simulation** settings change this: **Simulation Space** (World, or Component to ignore the character's motion), **Substep Hz** (default 60) and **Max Delta Time** (default 0.1 s). Its **Debug** settings draw that node's colliders and joints, like the console commands below.

Compiling the AnimBlueprint warns when the node has no spring data, when the data needs a reimport, and when the data names bones the skeleton doesn't have (usually spring data from another character). At runtime those joints and colliders are skipped, with one log warning per asset.

The solver itself (`FVRMSpringSolver`) has no anim graph dependencies and is tested on its own.

Debug visualization is available via console commands:

*To access the console in Unreal Engine, press the tilde key (`~`) in the editor, or open the Output Log window and enter commands there.*
```
vrm.SpringBones.DrawColliders 1    // Enable collider debug draw
vrm.SpringBones.DrawColliders 0    // Disable collider debug draw
vrm.SpringBones.DrawSprings 1      // Enable spring debug draw
vrm.SpringBones.DrawSprings 0      // Disable spring debug draw
```

## Troubleshooting

### VRM Pipelines Missing From the Import Dialog
- Run **Project Settings > Plugins > VRM Interchange > Register VRM Import Pipelines**

### Import Dialog Doesn't Appear
- Ensure the Interchange and InterchangeEditor plugins are enabled
- Check that the file has a `.vrm` extension
- Verify the VRM file is valid (not corrupted)

### Spring Bones Not Animating
- Check that the VRM Spring Bones AnimGraph node is enabled
- Verify the Spring Data asset is assigned
- Ensure the Spring Data asset is not empty (check SpringConfig)
- Confirm bone names in Spring Data match your skeleton
- If the Spring Data asset shows **Needs Reimport**, it was made by an older plugin version (colliders in the old axes, or VRM 0.x chains without their descendant bones). Reimport the VRM file

### IK Rig Not Generated
- Enable **Generate IK Rig Assets** in project settings
- Ensure IKRig plugin is enabled in your project
- Check that the template IK Rig asset exists in the plugin content

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
   - The glTF to Unreal coordinate conversion; the only module that uses cgltf

2. **VRMInterchange** (Runtime)
   - `UVRMTranslator`: Interchange translator for .vrm files
   - Generates Interchange node graphs from the parsed model
   - Spring bone parser

3. **VRMInterchangeEditor** (Editor)
   - Post-import pipelines for Spring Bones, IK Rig, and Live Link
   - Project settings integration
   - Asset generation and wiring

4. **VRMSpringBonesRuntime** (Runtime)
   - `FAnimNode_VRMSpringBones`: Animation node for spring simulation
   - `UVRMSpringBoneData`: Data asset for spring configuration
   - Physics solver implementation

5. **VRMSpringBonesEditor** (Editor)
   - `UAnimGraphNode_VRMSpringBones`: AnimGraph node wrapper
   - Editor customizations and debugging tools

### File Format

VRM is a 3D avatar format based on glTF 2.0, specifically designed for VR applications. It includes:
- Standard glTF structure (meshes, materials, textures)
- VRM-specific extensions for humanoid avatars
- Spring bone physics metadata
- Blend shape clip definitions

### Coordinate System

glTF (and so VRM) uses Y-up, right-handed coordinates in metres. UE uses Z-up, left-handed coordinates in centimetres. The importer's net conversion for positions, normals and morph deltas is:

```
UE (X, Y, Z) = glTF (X, Z, Y) * 100
```

This swaps Y and Z, which also converts handedness. VRM 1.0 models face +Z in glTF, which becomes +Y in UE, the direction the UE mannequin faces. VRM 0.x models face −Z, so they also get a 180° turn about UE Z and face +Y too. Files without VRM extensions import as generic glTF with the VRM 1.0 facing. Spring colliders and gravity use the same conversion as the mesh. Bone rest rotations are then reset to identity, so each bone's local transform is a pure translation. This matches how VMC streams local rotations, but it means bone orientations differ from the source file.

## Known Limitations

- **VRM Version**: Supports VRM 0.x and VRM 1.0 files.
- **Materials**: MToon and glTF material factors (base colour, emissive, alpha mode, double-sided) are not applied.
- **Expressions**: VRM expressions (blend shape groups) are not imported. Morph targets are imported individually by name.
- **Texture Formats**: Embedded textures must be PNG or JPEG

See `Planning Docs/Code_Review_and_Refactor_Plan_2026-09.md` for the plan that addresses these.

## Support and Contribution

This plugin is part of the VMCLiveLink project. For issues, feature requests, or contributions:
- GitHub: [atgoldberg/VMCLiveLink](https://github.com/atgoldberg/VMCLiveLink)
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
