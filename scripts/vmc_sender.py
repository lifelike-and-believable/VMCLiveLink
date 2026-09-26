#!/usr/bin/env python3
"""
VMC protocol test sender, recorder and replayer for the VMCLiveLink plugin. Standard library only.

Modes
  send     Generate and send a spec-conformant VMC stream (default).
  record   Listen on a UDP port and record another sender's packets (e.g. VSeeFace) to a file.
  replay   Send a recorded file, preserving timing.
  dump     Decode and print a recorded file, or the packets `send` would produce.
  write    Write the generated stream to a recording file without sending it (test captures).

Generated stream (per frame, as one OSC bundle, following https://protocol.vmc.info):
  /VMC/Ext/OK        int loaded(1), int calibrationState(3), int calibrationMode(0), int trackingStatus(1)
  /VMC/Ext/T         float time
  /VMC/Ext/Root/Pos  string "root", 7 floats  [+ 3 scale + 3 offset with --v21]
  /VMC/Ext/Bone/Pos  string bone, 7 floats     (all 55 Unity HumanBodyBones)
  /VMC/Ext/Blend/Val string name, float value  (VRM 0.x names, or VRM 1.0 names with --vrm1-names)
  /VMC/Ext/Blend/Apply

Options that produce deliberately non-conformant or awkward streams, for testing:
  --legacy-root      Root/Pos as 7 floats with no name (what older VMCLiveLink parsers expected)
  --partial-blend    Send each blend shape only every other frame (tests hold-last-value)
  --no-bundle        Send each message as its own packet instead of one bundle per frame

Examples
  python scripts/vmc_sender.py send --port 39539 --fps 60 --duration 10
  python scripts/vmc_sender.py record --listen 39540 --out capture.vmcrec   (point the sender at 39540)
  python scripts/vmc_sender.py replay capture.vmcrec --port 39539
  python scripts/vmc_sender.py dump capture.vmcrec --limit 3
  python scripts/vmc_sender.py dump --generate --v21 --limit 1
  python scripts/vmc_sender.py write --frames 10 --out Plugins/VMCLiveLink/Tests/Captures/synthetic.vmcrec
  python scripts/vmc_sender.py selftest

Recording format (.vmcrec): repeated records of <float64 seconds since start><uint32 length><packet bytes>,
little-endian.
"""

import argparse
import math
import socket
import struct
import sys
import time

# ---------------------------------------------------------------------------------------------
# OSC 1.0 encoding / decoding
# ---------------------------------------------------------------------------------------------


def _pad(b):
    return b + b"\0" * ((4 - len(b) % 4) % 4)


def _osc_string(s):
    return _pad(s.encode("utf-8") + b"\0")


def osc_message(address, *args):
    tags = ","
    payload = b""
    for a in args:
        if isinstance(a, bool):
            tags += "T" if a else "F"
        elif isinstance(a, int):
            tags += "i"
            payload += struct.pack(">i", a)
        elif isinstance(a, float):
            tags += "f"
            payload += struct.pack(">f", a)
        elif isinstance(a, str):
            tags += "s"
            payload += _osc_string(a)
        else:
            raise TypeError("unsupported OSC argument %r" % (a,))
    return _osc_string(address) + _osc_string(tags) + payload


def osc_bundle(messages, timetag=1):
    out = _osc_string("#bundle") + struct.pack(">Q", timetag)
    for m in messages:
        out += struct.pack(">i", len(m)) + m
    return out


def _read_string(data, i):
    end = data.index(b"\0", i)
    s = data[i:end].decode("utf-8")
    i = end + 1
    i += (4 - i % 4) % 4
    return s, i


def osc_decode(data):
    """Returns a list of (address, [args]) for a message or bundle."""
    if data.startswith(b"#bundle\0"):
        out, i = [], 16
        while i < len(data):
            (size,) = struct.unpack(">i", data[i:i + 4])
            out += osc_decode(data[i + 4:i + 4 + size])
            i += 4 + size
        return out
    address, i = _read_string(data, 0)
    tags, i = _read_string(data, i)
    args = []
    for t in tags[1:]:
        if t == "i":
            args.append(struct.unpack(">i", data[i:i + 4])[0]); i += 4
        elif t == "f":
            args.append(struct.unpack(">f", data[i:i + 4])[0]); i += 4
        elif t == "s":
            s, i = _read_string(data, i); args.append(s)
        elif t in "TF":
            args.append(t == "T")
        else:
            raise ValueError("unsupported OSC type tag %r" % t)
    return [(address, args)]


# ---------------------------------------------------------------------------------------------
# Humanoid (Unity HumanBodyBones names; local rest positions in Unity space, metres)
# Parents match the table in Plugins/VMCLiveLink/Source/VMCLiveLink/Private/VMCHumanoid.cpp.
# ---------------------------------------------------------------------------------------------

HUMANOID = [
    # name, parent, local rest position (x right, y up, z forward)
    ("Hips", None, (0.0, 0.95, 0.0)),
    ("LeftUpperLeg", "Hips", (-0.09, -0.05, 0.0)), ("RightUpperLeg", "Hips", (0.09, -0.05, 0.0)),
    ("LeftLowerLeg", "LeftUpperLeg", (0.0, -0.42, 0.0)), ("RightLowerLeg", "RightUpperLeg", (0.0, -0.42, 0.0)),
    ("LeftFoot", "LeftLowerLeg", (0.0, -0.40, 0.0)), ("RightFoot", "RightLowerLeg", (0.0, -0.40, 0.0)),
    ("LeftToes", "LeftFoot", (0.0, -0.05, 0.12)), ("RightToes", "RightFoot", (0.0, -0.05, 0.12)),
    ("Spine", "Hips", (0.0, 0.10, 0.0)), ("Chest", "Spine", (0.0, 0.12, 0.0)),
    ("UpperChest", "Chest", (0.0, 0.12, 0.0)), ("Neck", "UpperChest", (0.0, 0.12, 0.0)),
    ("Head", "Neck", (0.0, 0.10, 0.0)), ("LeftEye", "Head", (-0.03, 0.06, 0.08)),
    ("RightEye", "Head", (0.03, 0.06, 0.08)), ("Jaw", "Head", (0.0, -0.02, 0.05)),
    ("LeftShoulder", "UpperChest", (-0.04, 0.08, 0.0)), ("RightShoulder", "UpperChest", (0.04, 0.08, 0.0)),
    ("LeftUpperArm", "LeftShoulder", (-0.10, 0.0, 0.0)), ("RightUpperArm", "RightShoulder", (0.10, 0.0, 0.0)),
    ("LeftLowerArm", "LeftUpperArm", (-0.26, 0.0, 0.0)), ("RightLowerArm", "RightUpperArm", (0.26, 0.0, 0.0)),
    ("LeftHand", "LeftLowerArm", (-0.24, 0.0, 0.0)), ("RightHand", "RightLowerArm", (0.24, 0.0, 0.0)),
]
for _side, _sign in (("Left", -1.0), ("Right", 1.0)):
    for _finger, _z in (("Thumb", 0.03), ("Index", 0.02), ("Middle", 0.0), ("Ring", -0.02), ("Little", -0.04)):
        HUMANOID.append((f"{_side}{_finger}Proximal", f"{_side}Hand", (_sign * 0.08, 0.0, _z)))
        HUMANOID.append((f"{_side}{_finger}Intermediate", f"{_side}{_finger}Proximal", (_sign * 0.03, 0.0, 0.0)))
        HUMANOID.append((f"{_side}{_finger}Distal", f"{_side}{_finger}Intermediate", (_sign * 0.02, 0.0, 0.0)))
assert len(HUMANOID) == 55

BLEND_VRM0 = ["Neutral", "A", "I", "U", "E", "O", "Blink", "Blink_L", "Blink_R", "Joy", "Angry", "Sorrow", "Fun"]
BLEND_VRM1 = ["neutral", "aa", "ih", "ou", "ee", "oh", "blink", "blinkLeft", "blinkRight", "happy", "angry", "sad", "relaxed"]


def quat_axis_angle(axis, radians):
    s = math.sin(radians / 2.0)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(radians / 2.0))


def frame_messages(frame, t, opts):
    msgs = [osc_message("/VMC/Ext/OK", 1, 3, 0, 1), osc_message("/VMC/Ext/T", float(t))]

    # Root walks a 1 m circle and turns to face along it.
    angle = t * 0.5
    root_pos = (math.cos(angle), 0.0, math.sin(angle))
    root_rot = quat_axis_angle((0.0, 1.0, 0.0), -angle)
    if opts.legacy_root:
        msgs.append(osc_message("/VMC/Ext/Root/Pos", *map(float, root_pos + root_rot)))
    elif opts.v21:
        msgs.append(osc_message("/VMC/Ext/Root/Pos", "root", *map(float, root_pos + root_rot + (1.0, 1.0, 1.0) + (0.0, 0.0, 0.0))))
    else:
        msgs.append(osc_message("/VMC/Ext/Root/Pos", "root", *map(float, root_pos + root_rot)))

    for name, _parent, pos in HUMANOID:
        rot = (0.0, 0.0, 0.0, 1.0)
        if name == "LeftUpperArm":
            rot = quat_axis_angle((0.0, 0.0, 1.0), 0.8 * math.sin(t * 2.0))  # wave
        elif name == "Head":
            rot = quat_axis_angle((1.0, 0.0, 0.0), 0.3 * math.sin(t * 1.3))  # nod
        msgs.append(osc_message("/VMC/Ext/Bone/Pos", name, *map(float, pos + rot)))

    names = BLEND_VRM1 if opts.vrm1_names else BLEND_VRM0
    for i, name in enumerate(names):
        if opts.partial_blend and (frame + i) % 2:
            continue
        value = 0.5 + 0.5 * math.sin(t * 1.7 + i)
        msgs.append(osc_message("/VMC/Ext/Blend/Val", name, float(value)))
    msgs.append(osc_message("/VMC/Ext/Blend/Apply"))
    return msgs


def frame_packets(frame, t, opts):
    msgs = frame_messages(frame, t, opts)
    return msgs if opts.no_bundle else [osc_bundle(msgs)]


# ---------------------------------------------------------------------------------------------
# Modes
# ---------------------------------------------------------------------------------------------


def cmd_send(opts):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dt = 1.0 / opts.fps
    start = time.perf_counter()
    frame = 0
    print(f"Sending VMC to {opts.host}:{opts.port} at {opts.fps} fps"
          + (f" for {opts.duration} s" if opts.duration else " (Ctrl+C to stop)"))
    try:
        while not opts.duration or frame * dt < opts.duration:
            for p in frame_packets(frame, frame * dt, opts):
                sock.sendto(p, (opts.host, opts.port))
            frame += 1
            delay = start + frame * dt - time.perf_counter()
            if delay > 0:
                time.sleep(delay)
    except KeyboardInterrupt:
        pass
    print(f"Sent {frame} frames")


def cmd_record(opts):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((opts.bind, opts.listen))
    sock.settimeout(0.5)
    print(f"Recording from {opts.bind}:{opts.listen} to {opts.out}"
          + (f" for {opts.duration} s" if opts.duration else " (Ctrl+C to stop)"))
    count, start = 0, None
    with open(opts.out, "wb") as f:
        try:
            while True:
                now = time.perf_counter()
                if start is not None and opts.duration and now - start > opts.duration:
                    break
                try:
                    data, _ = sock.recvfrom(65535)
                except socket.timeout:
                    continue
                if start is None:
                    start = now
                f.write(struct.pack("<dI", now - start, len(data)) + data)
                count += 1
        except KeyboardInterrupt:
            pass
    print(f"Recorded {count} packets")


MAX_PACKET = 65536  # larger than any UDP datagram; a bigger size means a corrupt file


def read_recording(path):
    with open(path, "rb") as f:
        while True:
            head = f.read(12)
            if len(head) < 12:
                return
            t, n = struct.unpack("<dI", head)
            if n > MAX_PACKET:
                raise ValueError(f"{path}: packet of {n} bytes at t={t:.3f}s; the recording is corrupt")
            data = f.read(n)
            if len(data) != n:
                raise ValueError(f"{path}: truncated packet at t={t:.3f}s ({len(data)} of {n} bytes)")
            yield t, data


def cmd_replay(opts):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    start = time.perf_counter()
    count = 0
    for t, data in read_recording(opts.file):
        delay = start + t / opts.speed - time.perf_counter()
        if delay > 0:
            time.sleep(delay)
        sock.sendto(data, (opts.host, opts.port))
        count += 1
    print(f"Replayed {count} packets to {opts.host}:{opts.port}")


def cmd_write(opts):
    count = 0
    with open(opts.out, "wb") as f:
        for frame in range(opts.frames):
            t = frame / opts.fps
            for data in frame_packets(frame, t, opts):
                f.write(struct.pack("<dI", t, len(data)) + data)
                count += 1
    print(f"Wrote {opts.frames} frames ({count} packets) to {opts.out}")


def cmd_dump(opts):
    if opts.generate:
        packets = ((f / opts.fps, p) for f in range(opts.limit) for p in frame_packets(f, f / opts.fps, opts))
    else:
        packets = read_recording(opts.file)
    shown = 0
    for t, data in packets:
        for address, args in osc_decode(data):
            print(f"{t:9.4f}  {address:20s} {args}")
        shown += 1
        if not opts.generate and shown >= opts.limit:
            break


def cmd_selftest(_opts):
    class O:
        legacy_root = v21 = vrm1_names = partial_blend = no_bundle = False
    opts = O()
    decoded = osc_decode(frame_packets(0, 0.0, opts)[0])
    addresses = [a for a, _ in decoded]
    assert addresses.count("/VMC/Ext/Bone/Pos") == 55, "expected 55 bones"
    root = next(args for a, args in decoded if a == "/VMC/Ext/Root/Pos")
    assert isinstance(root[0], str) and len(root) == 8, "Root/Pos must be a name plus 7 floats"
    assert addresses[-1] == "/VMC/Ext/Blend/Apply"
    opts.v21 = True
    root = next(args for a, args in osc_decode(frame_packets(0, 0.0, opts)[0]) if a == "/VMC/Ext/Root/Pos")
    assert len(root) == 14, "v2.1 Root/Pos has 14 arguments"
    opts.v21, opts.legacy_root = False, True
    root = next(args for a, args in osc_decode(frame_packets(0, 0.0, opts)[0]) if a == "/VMC/Ext/Root/Pos")
    assert len(root) == 7 and all(isinstance(v, float) for v in root)
    names = {n for n, _, _ in HUMANOID}
    for n, parent, _ in HUMANOID:
        assert parent is None or parent in names, f"{n} has unknown parent {parent}"
    print("selftest OK")


def positive_float(text):
    value = float(text)
    if value <= 0:
        raise argparse.ArgumentTypeError(f"must be greater than 0, not {text}")
    return value


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="mode")

    def stream_opts(p):
        p.add_argument("--fps", type=positive_float, default=60.0)
        p.add_argument("--v21", action="store_true", help="Root/Pos with v2.1 scale and offset (14 arguments)")
        p.add_argument("--legacy-root", action="store_true", help="Root/Pos as 7 floats, no name (non-conformant)")
        p.add_argument("--vrm1-names", action="store_true", help="VRM 1.0 expression names")
        p.add_argument("--partial-blend", action="store_true", help="send each blend shape every other frame")
        p.add_argument("--no-bundle", action="store_true", help="one packet per message")

    p = sub.add_parser("send")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=39539)
    p.add_argument("--duration", type=float, default=0.0, help="seconds (0 = until Ctrl+C)")
    stream_opts(p)

    p = sub.add_parser("record")
    p.add_argument("--bind", default="0.0.0.0")
    p.add_argument("--listen", type=int, default=39540)
    p.add_argument("--out", required=True)
    p.add_argument("--duration", type=float, default=0.0)

    p = sub.add_parser("replay")
    p.add_argument("file")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=39539)
    p.add_argument("--speed", type=positive_float, default=1.0)

    p = sub.add_parser("dump")
    p.add_argument("file", nargs="?")
    p.add_argument("--generate", action="store_true", help="dump generated frames instead of a file")
    p.add_argument("--limit", type=int, default=5, help="frames (generate) or packets (file) to show")
    stream_opts(p)

    p = sub.add_parser("write")
    p.add_argument("--out", required=True)
    p.add_argument("--frames", type=int, default=10)
    stream_opts(p)

    sub.add_parser("selftest")

    opts = ap.parse_args()
    mode = opts.mode or "send"
    if opts.mode is None:
        opts = ap.parse_args(["send"] + sys.argv[1:])
    if mode == "dump" and not opts.generate and not opts.file:
        ap.error("dump needs a file or --generate")
    {"send": cmd_send, "record": cmd_record, "replay": cmd_replay, "dump": cmd_dump, "write": cmd_write, "selftest": cmd_selftest}[mode](opts)


if __name__ == "__main__":
    main()
