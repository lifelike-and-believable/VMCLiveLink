#!/usr/bin/env python3
"""
Generate small, deterministic VRM (GLB) fixtures for the VRMInterchange automation tests.

Each fixture targets specific importer behaviour and is written with a sidecar JSON file of
expected values computed here from the same scene description:

  vrm0_minimal                 VRM 0.x: humanoid, one blend shape group, one spring bone group
                               whose chain root is listed but whose descendants are not, and a
                               sphere collider with a non-zero offset. Vectors use the VRM 0.x
                               {"x","y","z"} object form.
  vrm1_minimal                 VRM 1.0: VRMC_vrm humanoid and expressions, VRMC_springBone using
                               the spec "shape" form, per-joint parameters that differ along the
                               chain, and a collider carrying VRMC_springBone_extended_collider.
  multi_skin                   Two skinned meshes with two skins whose joint lists are ordered
                               differently from each other and from the node order. One bone
                               appears only in the second skin.
  rigid_accessory              A non-skinned mesh node parented to a joint, with a node transform
                               (translation, rotation and scale).
  unnamed_and_duplicate_nodes  Joints with no name and joints that share a name.
  armature_transform           Joints below a non-joint "Armature" node that carries a rotation
                               and a scale.
  bind_pose_offset             A skinned mesh authored in a different pose from the node rest pose
                               (inverse bind matrices that are not the inverse joint world
                               transforms). Skinning must move its vertices into the rest pose.
  morph_targets                Two meshes whose morph targets share a name ("Shared") and each
                               have an unnamed target; one target carries NORMAL deltas.
  spring_tails                 A VRM 1.0 spring ending in a tail node that is not a skin joint,
                               below a non-joint grouping node. Both must become bones.
  spring_tails_vrm0            A VRM 0.x spring group whose subtree ends in a leaf no skin lists.
                               It must become a bone.
  mtoon_materials              VRM 1.0 with four materials: two VRMC_materials_mtoon (world and
                               screen outlines, a shading shift texture, a texture transform),
                               one KHR_materials_unlit (masked, double-sided) and one glTF PBR.
                               Two 1x1 PNG images.

All geometry is in glTF space (right-handed, Y up, metres). Expected values in the sidecars are
in glTF space too; tests convert them with the importer's conversion before comparing.

Usage:
  python scripts/make_vrm_fixtures.py [output_dir]
Default output: Plugins/VRMInterchange/Tests/Fixtures
The output is byte-for-byte reproducible.
"""

import json
import math
import os
import struct
import sys
import zlib

# ---------------------------------------------------------------------------------------------
# Minimal math (column vectors, glTF conventions: quaternion (x, y, z, w), matrices column-major
# when serialized).
# ---------------------------------------------------------------------------------------------


def quat_axis_angle(axis, degrees):
    x, y, z = axis
    n = math.sqrt(x * x + y * y + z * z)
    s = math.sin(math.radians(degrees) / 2.0) / n
    return [x * s, y * s, z * s, math.cos(math.radians(degrees) / 2.0)]


def mat_identity():
    return [[1.0 if r == c else 0.0 for c in range(4)] for r in range(4)]


def mat_mul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def mat_trs(t=(0, 0, 0), q=(0, 0, 0, 1), s=(1, 1, 1)):
    x, y, z, w = q
    r = [
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ]
    m = mat_identity()
    for row in range(3):
        for col in range(3):
            m[row][col] = r[row][col] * s[col]
        m[row][3] = t[row]
    return m


def mat_apply(m, p):
    return [m[r][0] * p[0] + m[r][1] * p[1] + m[r][2] * p[2] + m[r][3] for r in range(3)]


def mat_inverse(m):
    # General 4x4 inverse (Gauss-Jordan); matrices here are small and well conditioned.
    a = [row[:] + [1.0 if i == j else 0.0 for j in range(4)] for i, row in enumerate(m)]
    for col in range(4):
        pivot = max(range(col, 4), key=lambda r: abs(a[r][col]))
        a[col], a[pivot] = a[pivot], a[col]
        pv = a[col][col]
        a[col] = [v / pv for v in a[col]]
        for r in range(4):
            if r != col:
                f = a[r][col]
                a[r] = [vr - f * vc for vr, vc in zip(a[r], a[col])]
    return [row[4:] for row in a]


def column_major(m):
    return [m[r][c] for c in range(4) for r in range(4)]


def rnd(v, digits=6):
    if isinstance(v, (list, tuple)):
        return [rnd(x, digits) for x in v]
    r = round(v, digits)
    return 0.0 if r == 0 else r  # avoid -0.0 in JSON


# ---------------------------------------------------------------------------------------------
# Scene description
# ---------------------------------------------------------------------------------------------


class Node:
    def __init__(self, name, parent=None, t=None, r=None, s=None):
        self.name = name  # None means "no name" in the file
        self.parent = parent
        self.t, self.r, self.s = t, r, s
        self.mesh = None
        self.skin = None
        self.index = -1

    def local(self):
        return mat_trs(self.t or (0, 0, 0), self.r or (0, 0, 0, 1), self.s or (1, 1, 1))


class Scene:
    def __init__(self):
        self.nodes = []
        self.meshes = []  # dicts: name, positions, joints (per vertex, local skin joint index), weights, targets
        self.skins = []   # lists of Node
        self.skin_bind = []  # per skin: matrix taking rest-pose world space to the mesh's bind space
        self.bin = bytearray()
        self.buffer_views = []
        self.accessors = []
        self.images = []     # PNG bytes, one image each
        self.textures = []   # dicts, written as they are
        self.materials = []  # dicts, written as they are

    def node(self, *args, **kwargs):
        n = Node(*args, **kwargs)
        n.index = len(self.nodes)
        self.nodes.append(n)
        return n

    def world(self, node):
        m = node.local()
        p = node.parent
        while p is not None:
            m = mat_mul(p.local(), m)
            p = p.parent
        return m

    # -- binary helpers ---------------------------------------------------------------------

    def _align(self):
        while len(self.bin) % 4:
            self.bin.append(0)

    def accessor(self, values, component, type_, count, fmt, minmax=False):
        self._align()
        offset = len(self.bin)
        for v in values:
            self.bin += struct.pack("<" + fmt, *(v if isinstance(v, (list, tuple)) else [v]))
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(self.bin) - offset}
        self.buffer_views.append(view)
        acc = {"bufferView": len(self.buffer_views) - 1, "componentType": component, "count": count, "type": type_}
        if minmax:
            acc["min"] = [min(v[i] for v in values) for i in range(len(values[0]))]
            acc["max"] = [max(v[i] for v in values) for i in range(len(values[0]))]
        self.accessors.append(acc)
        return len(self.accessors) - 1

    # -- mesh construction ------------------------------------------------------------------

    def triangle_mesh(self, name, bone_triangles, skin_joints=None, targets=None):
        """bone_triangles: list of (joint_node_or_None, [p0, p1, p2]) in the mesh node's space.
        skin_joints: the skin's joint list; each triangle is weighted 100% to its joint."""
        positions, joints, weights = [], [], []
        for joint, tri in bone_triangles:
            for p in tri:
                positions.append(list(p))
                if skin_joints is not None:
                    joints.append([skin_joints.index(joint), 0, 0, 0])
                    weights.append([1.0, 0.0, 0.0, 0.0])
        mesh = {"name": name, "positions": positions, "joints": joints, "weights": weights,
                "targets": targets or [], "triangles": bone_triangles}
        self.meshes.append(mesh)
        return len(self.meshes) - 1

    # -- serialization ----------------------------------------------------------------------

    def gltf(self, extensions=None, extensions_used=None):
        mesh_json = []
        for m in self.meshes:
            count = len(m["positions"])
            attrs = {"POSITION": self.accessor(m["positions"], 5126, "VEC3", count, "3f", minmax=True)}
            normals = [[0.0, 0.0, 1.0]] * count
            attrs["NORMAL"] = self.accessor(normals, 5126, "VEC3", count, "3f")
            if m["joints"]:
                attrs["JOINTS_0"] = self.accessor(m["joints"], 5123, "VEC4", count, "4H")
                attrs["WEIGHTS_0"] = self.accessor(m["weights"], 5126, "VEC4", count, "4f")
            indices = list(range(count))
            prim = {"attributes": attrs, "indices": self.accessor(indices, 5125, "SCALAR", count, "I"), "mode": 4}
            if m.get("material") is not None:
                prim["material"] = m["material"]
            mj = {"name": m["name"], "primitives": [prim]}
            if m["targets"]:
                prim["targets"] = []
                for t in m["targets"]:
                    target = {"POSITION": self.accessor(t["deltas"], 5126, "VEC3", count, "3f", minmax=True)}
                    if "normals" in t:
                        target["NORMAL"] = self.accessor(t["normals"], 5126, "VEC3", count, "3f")
                    prim["targets"].append(target)
                mj["extras"] = {"targetNames": [t["name"] for t in m["targets"]]}
            mesh_json.append(mj)

        skins_json = []
        for joints, bind in zip(self.skins, self.skin_bind):
            ibms = [column_major(mat_inverse(mat_mul(bind, self.world(j)))) for j in joints]
            acc = self.accessor(ibms, 5126, "MAT4", len(joints), "16f")
            skins_json.append({"joints": [j.index for j in joints], "inverseBindMatrices": acc})

        nodes_json = []
        for n in self.nodes:
            nj = {}
            if n.name is not None:
                nj["name"] = n.name
            children = [c.index for c in self.nodes if c.parent is n]
            if children:
                nj["children"] = children
            if n.t:
                nj["translation"] = list(n.t)
            if n.r:
                nj["rotation"] = list(n.r)
            if n.s:
                nj["scale"] = list(n.s)
            if n.mesh is not None:
                nj["mesh"] = n.mesh
            if n.skin is not None:
                nj["skin"] = n.skin
            nodes_json.append(nj)

        images_json = []
        for png in self.images:
            self._align()
            offset = len(self.bin)
            self.bin += png
            self.buffer_views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(png)})
            images_json.append({"bufferView": len(self.buffer_views) - 1, "mimeType": "image/png"})

        self._align()
        doc = {
            "asset": {"version": "2.0", "generator": "VMCLiveLink scripts/make_vrm_fixtures.py"},
            "scene": 0,
            "scenes": [{"nodes": [n.index for n in self.nodes if n.parent is None]}],
            "nodes": nodes_json,
            "meshes": mesh_json,
            "buffers": [{"byteLength": len(self.bin)}],
            "bufferViews": self.buffer_views,
            "accessors": self.accessors,
        }
        if skins_json:
            doc["skins"] = skins_json
        if images_json:
            doc["images"] = images_json
        if self.textures:
            doc["textures"] = self.textures
        if self.materials:
            doc["materials"] = self.materials
        if extensions:
            doc["extensions"] = extensions
            doc["extensionsUsed"] = extensions_used or sorted(extensions.keys())
        return doc

    def write(self, path, extensions=None, extensions_used=None):
        doc = self.gltf(extensions, extensions_used)
        js = json.dumps(doc, separators=(",", ":"), sort_keys=True).encode("utf-8")
        while len(js) % 4:
            js += b" "
        binary = bytes(self.bin)
        total = 12 + 8 + len(js) + 8 + len(binary)
        with open(path, "wb") as f:
            f.write(struct.pack("<III", 0x46546C67, 2, total))
            f.write(struct.pack("<II", len(js), 0x4E4F534A))
            f.write(js)
            f.write(struct.pack("<II", len(binary), 0x004E4942))
            f.write(binary)

    # -- expectations -----------------------------------------------------------------------

    def expected_bones(self, joints):
        """Keyed by node index (names may be missing or duplicated)."""
        return {str(j.index): {"node": j.index, "name": j.name,
                               "world_position_gltf": rnd(mat_apply(self.world(j), [0, 0, 0]))}
                for j in joints}

    def expected_vertices(self, mesh_node):
        """Expected world position and dominant bone for every vertex of a mesh node."""
        mesh = self.meshes[mesh_node.mesh]
        out = []
        skinned = mesh_node.skin is not None
        node_world = self.world(mesh_node)
        v = 0
        for joint, tri in mesh["triangles"]:
            for p in tri:
                # glTF ignores the transform of a skinned mesh node. Skinned vertices are in bind
                # space; the joint matrix (joint world x inverse bind) takes them to the rest pose.
                if skinned:
                    world = mat_apply(mat_mul(self.world(joint), mat_inverse(mat_mul(self.skin_bind[mesh_node.skin], self.world(joint)))), p)
                else:
                    world = mat_apply(node_world, p)
                bone = joint if joint is not None else nearest_joint_ancestor(mesh_node)
                out.append({"mesh_node": mesh_node.name, "vertex": v,
                            "dominant_bone_node": bone.index,
                            "world_position_gltf": rnd(world)})
                v += 1
        return out


def nearest_joint_ancestor(node):
    p = node.parent
    while p is not None and not getattr(p, "is_joint", False):
        p = p.parent
    return p


def tri_at(center, size=0.02):
    x, y, z = center
    return [[x - size, y, z], [x + size, y, z], [x, y + size, z]]


def png_1x1(rgba):
    """A 1x1 RGBA PNG."""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    header = struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)
    pixels = zlib.compress(b"\x00" + bytes(rgba), 9)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", pixels) + chunk(b"IEND", b"")


def mark_joints(joints):
    for j in joints:
        j.is_joint = True


def vrm0_meta():
    return {"title": "fixture", "version": "1", "author": "VMCLiveLink tests", "allowedUserName": "Everyone",
            "licenseName": "CC0"}


def vrm1_meta():
    return {"name": "fixture", "version": "1", "authors": ["VMCLiveLink tests"],
            "licenseUrl": "https://vrm.dev/licenses/1.0/", "avatarPermission": "everyone",
            "commercialUsage": "personalNonProfit"}


# ---------------------------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------------------------


def base_humanoid(scene):
    root = scene.node("Root")
    hips = scene.node("Hips", root, t=[0, 1.0, 0])
    spine = scene.node("Spine", hips, t=[0, 0.2, 0])
    head = scene.node("Head", spine, t=[0, 0.4, 0])
    hair1 = scene.node("Hair1", head, t=[0, 0.1, -0.05])
    hair2 = scene.node("Hair2", hair1, t=[0, -0.1, 0])
    hair3 = scene.node("Hair3", hair2, t=[0, -0.1, 0])
    return root, hips, spine, head, hair1, hair2, hair3


def body_mesh(scene, root, joints, name="Body", bind=None):
    """One triangle per joint at the joint's position. bind (optional) is a matrix taking the rest
    pose to the pose the mesh was authored in; the vertices and inverse bind matrices use it."""
    bind = bind or mat_identity()
    tris = [(j, tri_at(mat_apply(mat_mul(bind, scene.world(j)), [0, 0, 0]))) for j in joints]
    mesh_index = scene.triangle_mesh(name, tris, skin_joints=joints)
    node = scene.node(name, root)
    node.mesh = mesh_index
    node.skin = len(scene.skins)
    scene.skins.append(joints)
    scene.skin_bind.append(bind)
    return node


def make_vrm0(out_dir):
    s = Scene()
    root, hips, spine, head, hair1, hair2, hair3 = base_humanoid(s)
    joints = [hips, spine, head, hair1, hair2, hair3]
    mark_joints(joints)
    body = body_mesh(s, root, joints)
    # One morph target: moves the head triangle up 1 cm.
    count = len(s.meshes[body.mesh]["positions"])
    deltas = [[0.0, 0.01, 0.0] if 6 <= i < 9 else [0.0, 0.0, 0.0] for i in range(count)]
    s.meshes[body.mesh]["targets"] = [{"name": "Fcl_ALL_Joy", "deltas": deltas}]

    ext = {"VRM": {
        "exporterVersion": "fixture", "specVersion": "0.0", "meta": vrm0_meta(),
        "humanoid": {"humanBones": [{"bone": "hips", "node": hips.index},
                                    {"bone": "spine", "node": spine.index},
                                    {"bone": "head", "node": head.index}]},
        "blendShapeMaster": {"blendShapeGroups": [
            {"name": "Joy", "presetName": "joy", "isBinary": False,
             "binds": [{"mesh": 0, "index": 0, "weight": 100}], "materialValues": []}]},
        "secondaryAnimation": {
            "boneGroups": [{"comment": "hair", "stiffiness": 0.5, "gravityPower": 0.2,
                            "gravityDir": {"x": 0, "y": -1, "z": 0}, "dragForce": 0.4, "center": -1,
                            "hitRadius": 0.02, "bones": [hair1.index], "colliderGroups": [0]}],
            "colliderGroups": [{"node": head.index,
                                "colliders": [{"offset": {"x": 0, "y": 0.1, "z": 0}, "radius": 0.08}]}]},
    }}
    s.write(os.path.join(out_dir, "vrm0_minimal.vrm"), ext)

    return {
        "vrm_version": "0.x",
        "bones": s.expected_bones(joints),
        "vertices": s.expected_vertices(body),
        "humanoid": {"hips": hips.index, "spine": spine.index, "head": head.index},
        "springs": {
            "chains": [{"name": "hair", "joint_nodes": [hair1.index, hair2.index, hair3.index],
                        "note": "VRM 0.x lists only the chain root; descendants must be added by the importer"}],
            "joint_params": {str(n.index): {"stiffness": 0.5, "drag": 0.4, "gravity_power": 0.2,
                                            "gravity_dir_gltf": [0, -1, 0], "hit_radius_m": 0.02}
                             for n in (hair1, hair2, hair3)},
            "colliders": [{"node": head.index, "sphere_offset_gltf": [0, 0.1, 0], "radius_m": 0.08}],
        },
        "morph_targets": ["Fcl_ALL_Joy"],
        "expressions": {"Joy": [{"morph": "Fcl_ALL_Joy", "weight": 1.0}]},
    }


def make_vrm1(out_dir):
    s = Scene()
    root, hips, spine, head, hair1, hair2, hair3 = base_humanoid(s)
    joints = [hips, spine, head, hair1, hair2, hair3]
    mark_joints(joints)
    body = body_mesh(s, root, joints)
    count = len(s.meshes[body.mesh]["positions"])
    deltas = [[0.0, 0.01, 0.0] if 6 <= i < 9 else [0.0, 0.0, 0.0] for i in range(count)]
    s.meshes[body.mesh]["targets"] = [{"name": "Fcl_ALL_Joy", "deltas": deltas}]

    params = [(hair1, 1.0, 0.4, 0.0, 0.02), (hair2, 0.6, 0.5, 0.1, 0.015), (hair3, 0.3, 0.6, 0.2, 0.01)]
    ext = {
        "VRMC_vrm": {
            "specVersion": "1.0", "meta": vrm1_meta(),
            "humanoid": {"humanBones": {"hips": {"node": hips.index}, "spine": {"node": spine.index},
                                        "head": {"node": head.index}}},
            "expressions": {"preset": {"happy": {"isBinary": False,
                                                 "morphTargetBinds": [{"node": body.index, "index": 0, "weight": 1.0}]}}},
        },
        "VRMC_springBone": {
            "specVersion": "1.0",
            "colliders": [
                {"node": head.index, "shape": {"sphere": {"offset": [0, 0.1, 0], "radius": 0.08}}},
                {"node": spine.index, "shape": {"sphere": {"offset": [0, 0, 0], "radius": 0.05}},
                 "extensions": {"VRMC_springBone_extended_collider": {
                     "specVersion": "1.0",
                     "shape": {"sphere": {"offset": [0, 0, 0], "radius": 0.3, "inside": True}}}}},
            ],
            "colliderGroups": [{"name": "head", "colliders": [0, 1]}],
            "springs": [{"name": "hair", "colliderGroups": [0], "joints": [
                {"node": n.index, "stiffness": st, "dragForce": dr, "gravityPower": gp,
                 "gravityDir": [0, -1, 0], "hitRadius": hr} for n, st, dr, gp, hr in params]}],
        },
    }
    used = ["VRMC_springBone", "VRMC_springBone_extended_collider", "VRMC_vrm"]
    s.write(os.path.join(out_dir, "vrm1_minimal.vrm"), ext, used)

    return {
        "vrm_version": "1.0",
        "bones": s.expected_bones(joints),
        "vertices": s.expected_vertices(body),
        "humanoid": {"hips": hips.index, "spine": spine.index, "head": head.index},
        "springs": {
            "chains": [{"name": "hair", "joint_nodes": [hair1.index, hair2.index, hair3.index]}],
            "joint_params": {str(n.index): {"stiffness": st, "drag": dr, "gravity_power": gp,
                                            "gravity_dir_gltf": [0, -1, 0], "hit_radius_m": hr}
                             for n, st, dr, gp, hr in params},
            "colliders": [
                {"node": head.index, "sphere_offset_gltf": [0, 0.1, 0], "radius_m": 0.08, "inside": False},
                {"node": spine.index, "sphere_offset_gltf": [0, 0, 0], "radius_m": 0.3, "inside": True,
                 "note": "extended collider replaces the base (fallback) sphere of radius 0.05"},
            ],
        },
        "morph_targets": ["Fcl_ALL_Joy"],
        "expressions": {"happy": [{"morph": "Fcl_ALL_Joy", "weight": 1.0}]},
    }


def spring_bones(scene, nodes):
    """expected_bones plus each node's glTF parent, which must be its parent bone too."""
    bones = scene.expected_bones(nodes)
    for n in nodes:
        bones[str(n.index)]["parent_node"] = n.parent.index
    return bones


def make_spring_tails(out_dir):
    """VRM 1.0 spring nodes that aren't skin joints (VRoid's J_Sec_*_end): the spring ends in a
    tail node below a non-joint grouping node. Both must become bones."""
    s = Scene()
    root, hips, spine, head, hair1, hair2, hair3 = base_humanoid(s)
    group = s.node("HairTipGroup", hair3, t=[0, -0.05, 0])
    tail = s.node("Hair_end", group, t=[0, -0.05, 0])
    # A skin joint with the tail's name, later in the hierarchy walk than the tail: it must keep
    # the plain name (skin joints are named first) and the tail gets the suffix.
    namesake = s.node("Hair_end", spine, t=[0.1, 0.3, 0])
    joints = [hips, spine, head, hair1, hair2, hair3, namesake]
    mark_joints(joints)
    body = body_mesh(s, root, joints)
    ext = {
        "VRMC_vrm": {"specVersion": "1.0", "meta": vrm1_meta(),
                     "humanoid": {"humanBones": {"hips": {"node": hips.index}, "spine": {"node": spine.index},
                                                 "head": {"node": head.index}}}},
        "VRMC_springBone": {
            "specVersion": "1.0",
            "springs": [{"name": "hair", "joints": [{"node": n.index} for n in (hair1, hair2, hair3, tail)]}],
        },
    }
    s.write(os.path.join(out_dir, "spring_tails.vrm"), ext, ["VRMC_springBone", "VRMC_vrm"])
    return {
        "vrm_version": "1.0",
        "bones": s.expected_bones(joints),
        "spring_bones": spring_bones(s, [group, tail]),
        "expected_bone_names_by_node": {str(namesake.index): "Hair_end", str(tail.index): "Hair_end_1"},
        "vertices": s.expected_vertices(body),
        "note": "spring_bones are not skin joints; the importer must still make them bones "
                "(HairTipGroup sits between the tail and its nearest joint). The skin joint "
                "Hair_end keeps its name; the spring tail of the same name is renamed.",
    }


def make_spring_tails_vrm0(out_dir):
    """VRM 0.x: a bone group's subtree ends in a leaf no skin lists. It must become a bone."""
    s = Scene()
    root, hips, spine, head, hair1, hair2, hair3 = base_humanoid(s)
    tip = s.node("HairTip", hair3, t=[0, -0.1, 0])
    joints = [hips, spine, head, hair1, hair2, hair3]
    mark_joints(joints)
    body = body_mesh(s, root, joints)
    ext = {"VRM": {
        "exporterVersion": "fixture", "specVersion": "0.0", "meta": vrm0_meta(),
        "humanoid": {"humanBones": [{"bone": "hips", "node": hips.index},
                                    {"bone": "spine", "node": spine.index},
                                    {"bone": "head", "node": head.index}]},
        "secondaryAnimation": {"boneGroups": [{"comment": "hair", "bones": [hair1.index]}], "colliderGroups": []},
    }}
    s.write(os.path.join(out_dir, "spring_tails_vrm0.vrm"), ext)
    return {
        "vrm_version": "0.x",
        "bones": s.expected_bones(joints),
        "spring_bones": spring_bones(s, [tip]),
        "vertices": s.expected_vertices(body),
        "note": "HairTip is not a skin joint but is in the hair group's subtree; it must become a bone.",
    }


def make_multi_skin(out_dir):
    s = Scene()
    root = s.node("Root")
    hips = s.node("Hips", root, t=[0, 1.0, 0])
    spine = s.node("Spine", hips, t=[0, 0.2, 0])
    head = s.node("Head", spine, t=[0, 0.4, 0])
    arm = s.node("LeftArm", spine, t=[0.2, 0.3, 0])
    mark_joints([hips, spine, head, arm])
    # Joint lists in orders that match neither each other nor the node order.
    body = body_mesh(s, root, [head, hips, spine], name="Body")
    armmesh = body_mesh(s, root, [arm, spine], name="ArmMesh")
    ext = {"VRMC_vrm": {"specVersion": "1.0", "meta": vrm1_meta(),
                        "humanoid": {"humanBones": {"hips": {"node": hips.index}}}}}
    s.write(os.path.join(out_dir, "multi_skin.vrm"), ext)
    return {
        "vrm_version": "1.0",
        "bones": s.expected_bones([hips, spine, head, arm]),
        "vertices": s.expected_vertices(body) + s.expected_vertices(armmesh),
        "note": "LeftArm is a joint of the second skin only; it must still be in the skeleton.",
    }


def make_rigid_accessory(out_dir):
    s = Scene()
    root = s.node("Root")
    hips = s.node("Hips", root, t=[0, 1.0, 0])
    spine = s.node("Spine", hips, t=[0, 0.2, 0])
    head = s.node("Head", spine, t=[0, 0.4, 0])
    mark_joints([hips, spine, head])
    body = body_mesh(s, root, [hips, spine, head])
    hat = s.node("Hat", head, t=[0, 0.15, 0], r=quat_axis_angle([0, 1, 0], 90), s=[2, 2, 2])
    hat.mesh = s.triangle_mesh("Hat", [(None, [[0.0, 0.0, 0.0], [0.05, 0.0, 0.0], [0.0, 0.05, 0.0]])])
    ext = {"VRMC_vrm": {"specVersion": "1.0", "meta": vrm1_meta(),
                        "humanoid": {"humanBones": {"hips": {"node": hips.index}, "head": {"node": head.index}}}}}
    s.write(os.path.join(out_dir, "rigid_accessory.vrm"), ext)
    return {
        "vrm_version": "1.0",
        "bones": s.expected_bones([hips, spine, head]),
        "vertices": s.expected_vertices(body) + s.expected_vertices(hat),
        "note": "Hat is not skinned: its vertices use the Hat node's world transform and bind to Head.",
    }


def make_unnamed_and_duplicates(out_dir):
    s = Scene()
    root = s.node("Root")
    hips = s.node("Hips", root, t=[0, 1.0, 0])
    unnamed = s.node(None, hips, t=[0, 0.2, 0])
    dup_a = s.node("Bone", unnamed, t=[0, 0.2, 0])
    dup_b = s.node("Bone", unnamed, t=[0.1, 0.2, 0])
    joints = [hips, unnamed, dup_a, dup_b]
    mark_joints(joints)
    body = body_mesh(s, root, joints)
    ext = {"VRMC_vrm": {"specVersion": "1.0", "meta": vrm1_meta(),
                        "humanoid": {"humanBones": {"hips": {"node": hips.index}}}}}
    s.write(os.path.join(out_dir, "unnamed_and_duplicate_nodes.vrm"), ext)
    return {
        "vrm_version": "1.0",
        "bones": s.expected_bones(joints),
        "vertices": s.expected_vertices(body),
        "expected_bone_names_by_node": {
            str(hips.index): "Hips",
            str(unnamed.index): "Node_%d" % unnamed.index,
            str(dup_a.index): "Bone",
            str(dup_b.index): "Bone_1",
        },
        "note": "Bone names must be unique and identical between the skeleton and the mesh payload.",
    }


def make_armature_transform(out_dir):
    s = Scene()
    root = s.node("Root")
    # Typical Blender export: armature rotated -90 about X with a 0.01 scale, joints in centimetres.
    armature = s.node("Armature", root, r=quat_axis_angle([1, 0, 0], -90), s=[0.01, 0.01, 0.01])
    hips = s.node("Hips", armature, t=[0, 0, 100])
    spine = s.node("Spine", hips, t=[0, 0, 20])
    head = s.node("Head", spine, t=[0, 0, 40])
    joints = [hips, spine, head]
    mark_joints(joints)
    body = body_mesh(s, root, joints)
    ext = {"VRMC_vrm": {"specVersion": "1.0", "meta": vrm1_meta(),
                        "humanoid": {"humanBones": {"hips": {"node": hips.index}}}}}
    s.write(os.path.join(out_dir, "armature_transform.vrm"), ext)
    return {
        "vrm_version": "1.0",
        "bones": s.expected_bones(joints),
        "vertices": s.expected_vertices(body),
        "note": "Armature is not a joint; its rotation and scale must still apply to the bone positions "
                "(Hips at (0,1,0), Spine at (0,1.2,0), Head at (0,1.6,0) in metres).",
    }


def make_bind_pose_offset(out_dir):
    s = Scene()
    root = s.node("Root")
    hips = s.node("Hips", root, t=[0, 1.0, 0])
    spine = s.node("Spine", hips, t=[0, 0.2, 0])
    head = s.node("Head", spine, t=[0, 0.4, 0])
    joints = [hips, spine, head]
    mark_joints(joints)
    # Mesh authored 0.5 m to the side and turned 90 degrees about Y relative to the node rest pose.
    bind = mat_trs(t=[0.5, 0, 0], q=quat_axis_angle([0, 1, 0], 90))
    body = body_mesh(s, root, joints, bind=bind)
    ext = {"VRMC_vrm": {"specVersion": "1.0", "meta": vrm1_meta(),
                        "humanoid": {"humanBones": {"hips": {"node": hips.index}}}}}
    s.write(os.path.join(out_dir, "bind_pose_offset.vrm"), ext)
    return {
        "vrm_version": "1.0",
        "bones": s.expected_bones(joints),
        "vertices": s.expected_vertices(body),
        "note": "Inverse bind matrices do not match the node rest pose; skinning with them puts each "
                "triangle back at its joint (Hips (0,1,0), Spine (0,1.2,0), Head (0,1.6,0)).",
    }


def make_morph_targets(out_dir):
    s = Scene()
    root = s.node("Root")
    hips = s.node("Hips", root, t=[0, 1.0, 0])
    head = s.node("Head", hips, t=[0, 0.6, 0])
    mark_joints([hips, head])
    face = body_mesh(s, root, [hips, head], name="Face")   # vertices 0-2 at Hips, 3-5 at Head
    hair = body_mesh(s, root, [head], name="Hair")         # vertices 6-8 (after Face's)

    def per_vertex(count, pick):
        return [pick(i) for i in range(count)]

    up = [0.0, 0.01, 0.0]
    zero = [0.0, 0.0, 0.0]
    # Face "Shared" lifts the head triangle and turns its normals from +Z to +Y
    # (base normal (0,0,1) + delta (0,1,-1)); the unnamed target moves the hips triangle along +X.
    s.meshes[face.mesh]["targets"] = [
        {"name": "Shared", "deltas": per_vertex(6, lambda i: up if i >= 3 else zero),
         "normals": per_vertex(6, lambda i: [0.0, 1.0, -1.0] if i >= 3 else zero)},
        {"name": "", "deltas": per_vertex(6, lambda i: [0.01, 0.0, 0.0] if i < 3 else zero)},
    ]
    s.meshes[hair.mesh]["targets"] = [
        {"name": "Shared", "deltas": per_vertex(3, lambda i: up)},
        {"name": "", "deltas": per_vertex(3, lambda i: [-0.01, 0.0, 0.0])},
    ]
    ext = {"VRMC_vrm": {"specVersion": "1.0", "meta": vrm1_meta(),
                        "humanoid": {"humanBones": {"hips": {"node": hips.index}, "head": {"node": head.index}}}}}
    s.write(os.path.join(out_dir, "morph_targets.vrm"), ext)
    return {
        "vrm_version": "1.0",
        "bones": s.expected_bones([hips, head]),
        "vertices": s.expected_vertices(face) + s.expected_vertices(hair),
        "morph_targets": {
            "Shared": {"meshes": ["Face", "Hair"], "delta_gltf": up, "moves_vertices": [3, 4, 5, 6, 7, 8],
                       "morphed_normal_gltf": {"vertices": [3, 4, 5], "normal": [0, 1, 0]}},
            "Face_morph_1": {"delta_gltf": [0.01, 0, 0], "moves_vertices": [0, 1, 2]},
            "Hair_morph_1": {"delta_gltf": [-0.01, 0, 0], "moves_vertices": [6, 7, 8]},
        },
        "note": "Unnamed targets are named <mesh>_morph_<index> and not merged across meshes. The two "
                "meshes' 'Shared' targets are merged, with a warning. Vertex indices are in merged "
                "order (Face, then Hair).",
    }


def make_mtoon_materials(out_dir):
    s = Scene()
    root = s.node("Root")
    hips = s.node("Hips", root, t=[0, 1.0, 0])
    head = s.node("Head", hips, t=[0, 0.6, 0])
    mark_joints([hips, head])
    names = ["Body", "Face", "Hair", "Accessory"]
    for i, name in enumerate(names):
        node = body_mesh(s, root, [hips, head], name=name)
        s.meshes[node.mesh]["material"] = i
    s.images = [png_1x1([255, 0, 0, 255]), png_1x1([128, 128, 128, 255])]
    s.textures = [{"source": 0}, {"source": 1}]
    s.materials = [
        {"name": "Body",
         "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1],
                                  "baseColorTexture": {"index": 0, "extensions": {"KHR_texture_transform": {"offset": [0.5, 0], "scale": [2, 1]}}}},
         "extensions": {"VRMC_materials_mtoon": {
             "specVersion": "1.0", "shadeColorFactor": [0.5, 0.25, 0.25], "shadeMultiplyTexture": {"index": 0},
             "shadingShiftFactor": -0.1, "shadingShiftTexture": {"index": 1, "scale": 0.5}, "shadingToonyFactor": 0.8,
             "parametricRimColorFactor": [1, 1, 1], "parametricRimFresnelPowerFactor": 3,
             "outlineWidthMode": "worldCoordinates", "outlineWidthFactor": 0.004,
             "outlineWidthMultiplyTexture": {"index": 1}, "outlineColorFactor": [0.1, 0, 0], "outlineLightingMixFactor": 0.5}}},
        {"name": "Face", "alphaMode": "BLEND",
         "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}},
         "extensions": {"VRMC_materials_mtoon": {
             "specVersion": "1.0", "outlineWidthMode": "screenCoordinates", "outlineWidthFactor": 0.001}}},
        {"name": "Hair", "alphaMode": "MASK", "alphaCutoff": 0.4, "doubleSided": True,
         "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}},
         "extensions": {"KHR_materials_unlit": {}}},
        {"name": "Accessory", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0}},
    ]
    ext = {"VRMC_vrm": {"specVersion": "1.0", "meta": vrm1_meta(),
                        "humanoid": {"humanBones": {"hips": {"node": hips.index}, "head": {"node": head.index}}}}}
    used = ["KHR_materials_unlit", "KHR_texture_transform", "VRMC_materials_mtoon", "VRMC_vrm"]
    s.write(os.path.join(out_dir, "mtoon_materials.vrm"), ext, used)
    return {
        "vrm_version": "1.0",
        "bones": s.expected_bones([hips, head]),
        "vertices": sum((s.expected_vertices(n) for n in s.nodes if n.mesh is not None), []),
        "materials": {
            "Body": {"master": "MToon", "alpha_mode": "OPAQUE", "outline": "world", "outline_width": 0.004,
                     "shading_shift_texture": True, "uv_offset": [0.5, 0], "uv_scale": [2, 1]},
            "Face": {"master": "MToon", "alpha_mode": "BLEND", "outline": "screen", "outline_width": 0.001},
            "Hair": {"master": "MToon", "unlit": True, "alpha_mode": "MASK", "alpha_cutoff": 0.4, "double_sided": True},
            "Accessory": {"master": "M_VRM_Master"},
        },
        "outline_material": "Body",
        "note": "The outline instance takes the widest outline (Body's). Image 1 is used only as data "
                "(shading shift and outline width), image 0 as colour.",
    }


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "Plugins", "VRMInterchange", "Tests", "Fixtures")
    os.makedirs(out_dir, exist_ok=True)
    makers = {
        "vrm0_minimal": make_vrm0,
        "vrm1_minimal": make_vrm1,
        "multi_skin": make_multi_skin,
        "rigid_accessory": make_rigid_accessory,
        "unnamed_and_duplicate_nodes": make_unnamed_and_duplicates,
        "armature_transform": make_armature_transform,
        "bind_pose_offset": make_bind_pose_offset,
        "morph_targets": make_morph_targets,
        "mtoon_materials": make_mtoon_materials,
        "spring_tails": make_spring_tails,
        "spring_tails_vrm0": make_spring_tails_vrm0,
    }
    for name, maker in makers.items():
        expected = maker(out_dir)
        with open(os.path.join(out_dir, name + ".expected.json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(expected, f, indent=2, sort_keys=True)
            f.write("\n")
        print("wrote", name)


if __name__ == "__main__":
    main()
