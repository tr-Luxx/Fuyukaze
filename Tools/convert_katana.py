"""
Turns a katana from a Sketchfab .glb into the files the game loads:

    Content/Fuyukaze/Models/Katana.fuyumesh     the sword's geometry, re-posed the way the game holds weapons
    Saved/Katana/*.png, *.jpg                   its textures, ready for Tools/BuildContent.ps1 -Only katana to import

The model this was written for is "Katana" by nejenabinupa (https://sketchfab.com/3d-models/katana-61e49a14ec9043a7bd3ba22fb7878508),
licensed CC-BY-NC-4.0. Its scene holds a drawn sword, its scabbard, and a second sword sheathed; this takes the drawn one.

Needs nothing but the Python standard library. Run it with any Python 3, for instance the one that ships with Unreal:

    "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\ThirdParty\\Python3\\Win64\\python.exe" Tools\\convert_katana.py path\\to\\katana.glb
"""

import math
import os
import struct
import sys

from glb_tools import read_accessor, read_glb, write_png

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_MESH = os.path.join(ROOT, "Content", "Fuyukaze", "Models", "Katana.fuyumesh")
OUT_TEXTURES = os.path.join(ROOT, "Saved", "Katana")

# the node that groups the drawn sword, and one stray part under it that belongs to the other sword
SWORD_NODE = "Circle.004"
SKIP_NODES = ("Cube.008",)

# centimetres per model unit. Chosen so the grip comes out a little over 25 cm and the blade about 77 cm
SCALE = 17.5

# where the middle of the grip sits in the model, which becomes the origin: the point the hand closes on
GRIP_MIDDLE = -0.70

def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    document, binary = read_glb(sys.argv[1])
    nodes = document["nodes"]

    sword = next(i for i, n in enumerate(nodes) if n.get("name") == SWORD_NODE)
    parts = []
    stack = list(nodes[sword].get("children", []))
    while stack:
        index = stack.pop()
        node = nodes[index]
        if node.get("name") in SKIP_NODES:
            continue
        if "mesh" in node:
            parts.append(node["mesh"])
        stack.extend(node.get("children", []))

    # gather every part by material, still in the model's own space
    sections = {}
    for mesh_index in sorted(parts):
        for primitive in document["meshes"][mesh_index]["primitives"]:
            attributes = primitive["attributes"]
            positions = read_accessor(document, binary, attributes["POSITION"])
            normals = read_accessor(document, binary, attributes["NORMAL"])
            uvs = read_accessor(document, binary, attributes["TEXCOORD_0"]) if "TEXCOORD_0" in attributes else [(0.0, 0.0)] * len(positions)
            tangents = read_accessor(document, binary, attributes["TANGENT"]) if "TANGENT" in attributes else [(0.0, 0.0, 0.0, 0.0)] * len(positions)
            indices = [row[0] for row in read_accessor(document, binary, primitive["indices"])]

            section = sections.setdefault(primitive["material"], {"vertices": [], "indices": []})
            base = len(section["vertices"])
            section["vertices"].extend(zip(positions, normals, tangents, uvs))
            section["indices"].extend(base + i for i in indices)

    # the grip's axis runs through the middle of the wrapped handle
    wrap = next(m for m, material in enumerate(document["materials"]) if material.get("normalTexture"))
    grip = [v[0] for v in sections[wrap]["vertices"]]
    centre_x = (min(p[0] for p in grip) + max(p[0] for p in grip)) / 2
    centre_z = (min(p[2] for p in grip) + max(p[2] for p in grip)) / 2

    # the game holds weapons by the origin with the blade along +X. The model lies along +Y and curves towards +X,
    # so: length Y -> X, the curve X -> -Z (the tip dips, as the game's own katana did), thickness Z -> Y.
    # That is a pure rotation, so nothing is mirrored and the triangles keep their winding
    def turn(v):
        return (v[1], v[2], -v[0])

    def rough(p):
        return turn(((p[0] - centre_x) * SCALE, (p[1] - GRIP_MIDDLE) * SCALE, (p[2] - centre_z) * SCALE))

    # the model rests on its guard, which leaves the blade pointing a few degrees off to one side.
    # Fit a line through the blade as seen from above and swing the whole sword about the grip until it lies along X
    blade = [rough(v[0]) for m in sections if m != wrap and document["materials"][m].get("pbrMetallicRoughness", {}).get("baseColorTexture") for v in sections[m]["vertices"]]
    blade = [p for p in blade if p[0] > 20.0]
    mean_x = sum(p[0] for p in blade) / len(blade)
    mean_y = sum(p[1] for p in blade) / len(blade)
    slope = sum((p[0] - mean_x) * (p[1] - mean_y) for p in blade) / sum((p[0] - mean_x) ** 2 for p in blade)
    lean = math.atan(slope)
    cos_lean, sin_lean = math.cos(lean), math.sin(lean)
    print("blade leaned %.2f degrees, now straightened" % math.degrees(lean))

    def level(v):
        return (v[0] * cos_lean + v[1] * sin_lean, -v[0] * sin_lean + v[1] * cos_lean, v[2])

    def place(p):
        return level(rough(p))

    os.makedirs(os.path.dirname(OUT_MESH), exist_ok=True)
    low = [1e9] * 3
    high = [-1e9] * 3
    with open(OUT_MESH, "wb") as handle:
        handle.write(struct.pack("<4sI", b"FUYM", len(sections)))
        for material in sorted(sections):
            section = sections[material]
            handle.write(struct.pack("<III", material, len(section["vertices"]), len(section["indices"])))
            for position, normal, tangent, uv in section["vertices"]:
                p = place(position)
                n = level(turn(normal))
                t = level(turn(tangent[:3]))
                for axis in range(3):
                    low[axis] = min(low[axis], p[axis])
                    high[axis] = max(high[axis], p[axis])
                handle.write(struct.pack("<12f", p[0], p[1], p[2], n[0], n[1], n[2], t[0], t[1], t[2], tangent[3], uv[0], uv[1]))
            handle.write(struct.pack("<%dI" % len(section["indices"]), *section["indices"]))
            name = document["materials"][material].get("name")
            print("section %d (%s): %d vertices, %d triangles" % (material, name, len(section["vertices"]), len(section["indices"]) // 3))
    print("wrote %s (%.0f KB)" % (OUT_MESH, os.path.getsize(OUT_MESH) / 1024))
    print("extent in cm: X %.1f to %.1f, Y %.1f to %.1f, Z %.1f to %.1f" % (low[0], high[0], low[1], high[1], low[2], high[2]))

    # the textures of the materials the sword uses
    os.makedirs(OUT_TEXTURES, exist_ok=True)
    wanted = {}
    for material in sections:
        m = document["materials"][material]
        pbr = m.get("pbrMetallicRoughness", {})
        label = "Wrap" if material == wrap else "Blade"
        for slot, suffix in ((pbr.get("baseColorTexture"), "Color"), (pbr.get("metallicRoughnessTexture"), "Mask"), (m.get("normalTexture"), "Normal")):
            if slot:
                wanted[document["textures"][slot["index"]]["source"]] = "T_Katana%s_%s" % (label, suffix)
    for image_index, name in sorted(wanted.items()):
        image = document["images"][image_index]
        view = document["bufferViews"][image["bufferView"]]
        start = view.get("byteOffset", 0)
        path = os.path.join(OUT_TEXTURES, name + (".png" if image["mimeType"] == "image/png" else ".jpg"))
        with open(path, "wb") as handle:
            handle.write(binary[start:start + view["byteLength"]])
        print("wrote " + path)

    # two silhouettes of the result, to check the pose by eye: from the side (X across, Z up) and from above (X across, Y up)
    width, height, zoom = 1200, 520, 10.0
    pixels = bytearray([24] * (width * height * 3))
    colours = [(200, 60, 60), (200, 210, 225), (90, 90, 110), (230, 180, 70), (120, 220, 120), (200, 120, 220), (220, 220, 120)]
    for material in sorted(sections):
        colour = colours[material % len(colours)]
        for position, _, _, _ in sections[material]["vertices"]:
            p = place(position)
            for row, up in ((130, p[2]), (390, p[1])):
                x = int(180 + p[0] * zoom)
                y = int(row - up * zoom)
                if 0 <= x < width and 0 <= y < height:
                    pixels[(y * width + x) * 3:(y * width + x) * 3 + 3] = bytes(colour)
    for row in (130, 390):
        for x in range(width):
            if pixels[(row * width + x) * 3] == 24:
                pixels[(row * width + x) * 3:(row * width + x) * 3 + 3] = bytes((60, 60, 60))
    for y in range(height):
        if pixels[(y * width + 180) * 3] == 24:
            pixels[(y * width + 180) * 3:(y * width + 180) * 3 + 3] = bytes((60, 60, 60))
    preview = os.path.join(OUT_TEXTURES, "preview.png")
    write_png(preview, width, height, pixels)
    print("wrote " + preview)


main()
