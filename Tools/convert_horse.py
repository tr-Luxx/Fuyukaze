"""
Turns a standing horse from a Sketchfab .glb into the files the game loads, and rigs it for a gallop:

    Content/Fuyukaze/Models/Horse.fuyumesh      the horse's geometry, facing +X with its hooves on Z = 0
    Saved/Horse/T_Horse_Color.jpg               its texture, ready for Tools/BuildContent.ps1 -Only horse to import
    Saved/Horse/rig.json                        where its joints are, which the horse material is built from
    Saved/Horse/preview.png                     the gallop drawn frame by frame, to check it by eye

The model this was written for is "Low poly Horse Brown Colour 3d Model Free" by iRahulRajput
(https://sketchfab.com/3d-models/low-poly-horse-brown-colour-3d-model-free-9073d1dfc345495a88ef84eed4d2a9e5), licensed CC-BY-4.0.
It has no skeleton and no animation, so the horse is animated in its material instead: every vertex is tagged here with
how much it belongs to the front legs, hind legs, neck and tail (stored as its vertex colour), and the material swings
those parts about the joints listed in rig.json. gallop() below is the same motion, used only to draw the preview.

Needs nothing but the Python standard library:

    "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\ThirdParty\\Python3\\Win64\\python.exe" Tools\\convert_horse.py path\\to\\horse.glb
"""

import json
import math
import os
import struct
import sys

from glb_tools import read_accessor, read_glb, write_png

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_MESH = os.path.join(ROOT, "Content", "Fuyukaze", "Models", "Horse.fuyumesh")
OUT_FOLDER = os.path.join(ROOT, "Saved", "Horse")

# centimetres per model unit. The model is one unit long, so this is the horse's length from muzzle to tail
SCALE = 235.0

# Joints, in the model's own units as (forward, up). The model faces +Z with +Y up
JOINTS = {
    "shoulder": (0.135, 0.42),
    "knee": (0.125, 0.20),
    "hip": (-0.25, 0.50),
    "hock": (-0.335, 0.25),
    "neck": (0.16, 0.55),
    "tail": (-0.38, 0.58),
    "body": (-0.05, 0.45),
}

# The gallop itself. Angles are radians; the material reads these same numbers from rig.json
GAIT = {
    "strides_per_second": 2.1,
    "front_swing": 0.62, "front_reach": 0.12, "front_fold": 1.25,
    "hind_swing": 0.55, "hind_reach": -0.04, "hind_fold": 0.95,
    "pair_lag": 0.75,          # how far behind its partner the second leg of each pair lands
    "bob": 7.0,                # cm the body rises and falls
    "rock": 0.07,              # how far the body pitches
    "neck_stretch": 0.12, "neck_nod": 0.10,
    "tail_lift": 0.3, "tail_wave": 0.07,
}


def smooth(a, b, v):
    t = min(max((v - a) / (b - a), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def weights(p):
    """How much a vertex (in model units) belongs to a front leg, a hind leg, the neck and head, and the tail."""
    x, y, z = abs(p[0]), p[1], p[2]

    # where a leg joins the body the two sides share skin along the belly's midline, which must not be pulled apart
    seam = lambda low, high: 1.0 - (1.0 - smooth(0.0, 0.03, x)) * (1.0 - smooth(high, low, y))

    front = smooth(0.40, 0.30, y) * smooth(0.02, 0.07, z) * smooth(0.26, 0.21, z) * seam(0.28, 0.34)

    # the tail is fused to the back of the hind legs, so the nearer part of it follows them
    hind = smooth(0.47, 0.33, y) * smooth(-0.12, -0.22, z) * smooth(-0.47, -0.365, z) * seam(0.30, 0.38)

    neck = smooth(0.14, 0.32, z) * smooth(0.42, 0.58, y)
    tail = smooth(-0.375, -0.47, z) * min(max((0.62 - y) / 0.45, 0.0), 1.0)
    return front, hind, neck, tail


def swing(p, pivot, angle):
    """Turns a point (forward, side, up) about a sideways axis through pivot. A positive angle carries a hanging leg forward."""
    dx, dz = p[0] - pivot[0], p[2] - pivot[1]
    s, c = math.sin(angle), math.cos(angle)
    return (pivot[0] + dx * c - dz * s, p[1], pivot[1] + dx * s + dz * c)


def gallop(p, w, cycle, gait, rig):
    """The material's animation: p is a vertex in game space (cm, +X forward, +Z up), w its four weights."""
    front, hind, neck, tail = w
    g = GAIT
    lag = g["pair_lag"] if p[1] > 0.0 else 0.0

    # each leg folds at the knee as it comes forward through the air, then swings from the shoulder or hip.
    # The hind pair strike first and the front pair half a stride later
    u = cycle - math.pi - lag
    fold = smooth(0.0, 1.0, max(math.cos(u), 0.0))
    p = swing(p, rig["knee"], -g["front_fold"] * gait * fold * front * smooth(rig["knee"][1] + 7.0, rig["knee"][1] - 7.0, p[2]))
    p = swing(p, rig["shoulder"], gait * (g["front_swing"] * math.sin(u) + g["front_reach"]) * front)

    u = cycle - lag
    fold = smooth(0.0, 1.0, max(math.cos(u), 0.0))
    p = swing(p, rig["hock"], g["hind_fold"] * gait * fold * hind * smooth(rig["hock"][1] + 7.0, rig["hock"][1] - 7.0, p[2]))
    p = swing(p, rig["hip"], gait * (g["hind_swing"] * math.sin(u) + g["hind_reach"]) * hind)

    # the neck stretches out and nods, the tail streams behind
    p = swing(p, rig["neck"], -gait * (g["neck_stretch"] + g["neck_nod"] * math.sin(cycle + 1.0)) * neck)
    p = swing(p, rig["tail"], tail * (-g["tail_lift"] * gait + g["tail_wave"] * (0.3 + gait) * math.sin(cycle * 2.0 + p[2] * 0.03)))

    # and the whole horse rocks and bounds
    p = swing(p, rig["body"], gait * g["rock"] * math.sin(cycle + 2.2))
    return (p[0], p[1], p[2] + gait * g["bob"] * math.sin(cycle + 0.6))


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    document, binary = read_glb(sys.argv[1])
    primitive = document["meshes"][0]["primitives"][0]
    attributes = primitive["attributes"]
    positions = read_accessor(document, binary, attributes["POSITION"])
    normals = read_accessor(document, binary, attributes["NORMAL"])
    uvs = read_accessor(document, binary, attributes["TEXCOORD_0"])
    indices = [row[0] for row in read_accessor(document, binary, primitive["indices"])]

    # the game's horses face +X and stand on Z = 0. Forward Z -> X, side X -> -Y, up Y -> Z, which changes handedness
    # along with the engine, so the horse is not mirrored
    turn = lambda v: (v[2], -v[0], v[1])
    place = lambda p: tuple(c * SCALE for c in turn(p))
    rig = {name: (joint[0] * SCALE, joint[1] * SCALE) for name, joint in JOINTS.items()}

    os.makedirs(os.path.dirname(OUT_MESH), exist_ok=True)
    os.makedirs(OUT_FOLDER, exist_ok=True)
    placed = [place(p) for p in positions]
    tagged = [weights(p) for p in positions]

    with open(OUT_MESH, "wb") as handle:
        # "FUYC": like the katana's file, with a colour after each vertex's texture coordinate
        handle.write(struct.pack("<4sI", b"FUYC", 1))
        handle.write(struct.pack("<III", 0, len(positions), len(indices)))
        for p, normal, uv, w in zip(placed, normals, uvs, tagged):
            n = turn(normal)
            handle.write(struct.pack("<16f", p[0], p[1], p[2], n[0], n[1], n[2], 0.0, 0.0, 0.0, 0.0, uv[0], uv[1], w[0], w[1], w[2], w[3]))
        handle.write(struct.pack("<%dI" % len(indices), *indices))
    print("wrote %s: %d vertices, %d triangles" % (OUT_MESH, len(positions), len(indices) // 3))
    for axis, name in enumerate("XYZ"):
        print("  %s %.0f to %.0f cm" % (name, min(p[axis] for p in placed), max(p[axis] for p in placed)))

    image = document["images"][document["textures"][document["materials"][primitive["material"]]["pbrMetallicRoughness"]["baseColorTexture"]["index"]]["source"]]
    view = document["bufferViews"][image["bufferView"]]
    texture = os.path.join(OUT_FOLDER, "T_Horse_Color" + (".png" if image["mimeType"] == "image/png" else ".jpg"))
    with open(texture, "wb") as handle:
        handle.write(binary[view.get("byteOffset", 0):view.get("byteOffset", 0) + view["byteLength"]])
    print("wrote " + texture)

    with open(os.path.join(OUT_FOLDER, "rig.json"), "w") as handle:
        json.dump({"joints": rig, "gait": GAIT}, handle, indent=2)
    print("wrote " + os.path.join(OUT_FOLDER, "rig.json"))

    # the preview: the rig's tagging on top (red front legs, green hind legs, blue neck, white tail),
    # then eight moments of one stride seen from the side
    cell, zoom = 330, 1.15
    width, height = cell * 4, cell * 3
    pixels = bytearray([22] * (width * height * 3))

    def plot(column, row, p, colour):
        x = int(column * cell + cell * 0.5 + p[0] * zoom)
        y = int(row * cell + cell * 0.86 - p[2] * zoom)
        if 0 <= x < width and 0 <= y < height:
            pixels[(y * width + x) * 3:(y * width + x) * 3 + 3] = bytes(colour)

    for p, w in zip(placed, tagged):
        plot(0, 0, p, (int(60 + 195 * w[0]), int(60 + 195 * w[1]), int(60 + 195 * w[2])) if w[3] < 0.02 else (int(60 + 195 * w[3]),) * 3)
        plot(1, 0, (p[1] * 1.0 + 0.0, 0.0, p[2]), (150, 150, 150))
        plot(2, 0, gallop(p, w, 0.0, 0.0, rig), (150, 150, 150))
    for frame in range(8):
        cycle = frame / 8.0 * 2.0 * math.pi
        for p, w in zip(placed, tagged):
            colour = (235, 150, 110) if p[1] > 0.0 else (120, 170, 235)
            plot(frame % 4, 1 + frame // 4, gallop(p, w, cycle, 1.0, rig), colour)
    for row in range(3):
        y = int(row * cell + cell * 0.86)
        for x in range(width):
            if pixels[(y * width + x) * 3] == 22:
                pixels[(y * width + x) * 3:(y * width + x) * 3 + 3] = bytes((70, 70, 70))
    write_png(os.path.join(OUT_FOLDER, "preview.png"), width, height, pixels)
    print("wrote " + os.path.join(OUT_FOLDER, "preview.png"))


main()
