# VRM test fixtures

Small synthetic VRM files used by the VRMInterchange automation tests (`VRM.*`). Each `<name>.vrm` has a `<name>.expected.json` with the values an importer should produce, in glTF space (Y up, metres).

- Generate: `python scripts/make_vrm_fixtures.py` (standard library only; output is reproducible)
- Check: `python scripts/check_vrm_fixtures.py` (needs a C compiler). Validates every file with the bundled cgltf (`cgltf_validate`), confirms the expected values match glTF semantics, and confirms the files match a fresh generator run.

Do not edit these files by hand. Change the generator and regenerate.

| Fixture | What it exercises |
|---|---|
| `vrm0_minimal` | VRM 0.x humanoid, blend shape group, spring group listing only its chain root, collider with an `{x,y,z}` offset |
| `vrm1_minimal` | VRM 1.0 humanoid and expressions, spec `shape` colliders, per-joint spring parameters, extended (inside) collider |
| `multi_skin` | Two skins with different joint orders; one bone only in the second skin |
| `rigid_accessory` | Non-skinned mesh parented to a joint, with translation, rotation and scale |
| `unnamed_and_duplicate_nodes` | Joints with no name and joints sharing a name |
| `armature_transform` | Joints under a non-joint node with rotation and scale |
