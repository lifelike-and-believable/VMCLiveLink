#!/usr/bin/env python3
"""
Checks the VRM test fixtures against glTF semantics, independently of the Unreal importer.

Compiles scripts/check_vrm_fixtures.c against the bundled cgltf, runs it on each fixture
(parse + load buffers + cgltf_validate), and compares joint positions and per-vertex dominant
joints with the *.expected.json sidecars from make_vrm_fixtures.py. Also checks that the files
are reproducible.

Needs a C compiler on PATH (cc, clang or gcc). Usage:
  python scripts/check_vrm_fixtures.py [fixtures_dir]
"""

import filecmp
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
TOL = 1e-4


def close(a, b):
    return all(abs(x - y) <= TOL for x, y in zip(a, b))


def main():
    fixtures = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "Plugins", "VRMInterchange", "Tests", "Fixtures")
    cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not cc:
        print("No C compiler found; cannot run the cgltf check.")
        return 2

    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "check_vrm_fixtures")
        subprocess.run([cc, "-O1", "-w", "-I", os.path.join(ROOT, "Plugins", "VRMInterchange", "ThirdParty", "cgltf"),
                        os.path.join(ROOT, "scripts", "check_vrm_fixtures.c"), "-o", exe, "-lm"], check=True)

        regen = os.path.join(tmp, "regen")
        subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "make_vrm_fixtures.py"), regen],
                       check=True, stdout=subprocess.DEVNULL)

        for name in sorted(f for f in os.listdir(fixtures) if f.endswith(".vrm")):
            base = name[:-4]
            path = os.path.join(fixtures, name)
            for f in (name, base + ".expected.json"):
                if not filecmp.cmp(os.path.join(fixtures, f), os.path.join(regen, f), shallow=False):
                    failures.append(f"{f}: differs from a fresh run of make_vrm_fixtures.py")

            run = subprocess.run([exe, path], capture_output=True, text=True)
            if run.returncode != 0:
                failures.append(f"{name}: {run.stderr.strip()}")
                continue
            actual = json.loads(run.stdout)
            with open(os.path.join(fixtures, base + ".expected.json"), encoding="utf-8") as f:
                expected = json.load(f)

            for info in expected["bones"].values():
                got = actual["joints"].get(str(info["node"]))
                if got is None or not close(got, info["world_position_gltf"]):
                    failures.append(f"{name}: joint node {info['node']} ({info['name']}) at {got}, "
                                    f"expected {info['world_position_gltf']}")

            by_key = {(v["mesh_node"], v["vertex"]): v for v in actual["vertices"]}
            for v in expected["vertices"]:
                got = by_key.get((v["mesh_node"], v["vertex"]))
                if got is None:
                    failures.append(f"{name}: missing vertex {v['mesh_node']}#{v['vertex']}")
                elif got["bone_node"] != v["dominant_bone_node"] or not close(got["pos"], v["world_position_gltf"]):
                    failures.append(f"{name}: vertex {v['mesh_node']}#{v['vertex']} got bone {got['bone_node']} "
                                    f"at {got['pos']}, expected bone {v['dominant_bone_node']} at {v['world_position_gltf']}")
            legacy_wrong = sum(1 for v in actual["vertices"] if v["legacy_bone_node"] != v["bone_node"])
            print(f"checked {name}: {len(expected['bones'])} joints, {len(expected['vertices'])} vertices "
                  f"({legacy_wrong} bound to the wrong bone by the importer's joint mapping as of d1c7c20)")

    for f in failures:
        print("FAIL:", f)
    print("OK" if not failures else f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
