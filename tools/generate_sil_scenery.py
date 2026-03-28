#!/usr/bin/env python3
"""Generate SIL testing scenery for CRRCSim.

Creates:
  - textures/grid.rgb: 100m grid texture (white lines on green)
  - objects/sil_terrain.ac: flat 6km x 6km terrain mesh
  - objects/sil_barn.ac, sil_silo.ac, sil_watertower.ac,
    sil_hangar.ac, sil_house.ac: simple reference objects
  - scenery/sil.xml: scenery definition
"""

import struct
import os
from pathlib import Path

REPO = Path(__file__).parent.parent
M_TO_FT = 3.28084
TERRAIN_HALF_M = 3000  # ±3km
TERRAIN_HALF_FT = TERRAIN_HALF_M * M_TO_FT
GRID_CELL_M = 100
GRID_CELLS = int(TERRAIN_HALF_M * 2 / GRID_CELL_M)  # 60 cells across


def write_sgi_rgb(filepath, width, height, pixels):
    """Write an SGI RGB image (uncompressed)."""
    with open(filepath, "wb") as f:
        # Header
        f.write(struct.pack(">H", 474))  # magic
        f.write(struct.pack("B", 0))  # storage: verbatim
        f.write(struct.pack("B", 1))  # bytes per channel
        f.write(struct.pack(">H", 3))  # dimension (3 = RGB)
        f.write(struct.pack(">H", width))
        f.write(struct.pack(">H", height))
        f.write(struct.pack(">H", 3))  # zsize (channels)
        f.write(struct.pack(">i", 0))  # pixmin
        f.write(struct.pack(">i", 255))  # pixmax
        f.write(b"\x00" * 4)  # dummy
        f.write(b"grid\x00" + b"\x00" * 75)  # name (80 bytes)
        f.write(struct.pack(">i", 0))  # colormap
        f.write(b"\x00" * 404)  # pad

        # Pixel data: all R rows, then all G rows, then all B rows
        # Rows are bottom-to-top in SGI format
        for channel in range(3):
            for y in range(height):
                row = bytes(pixels[y][x][channel] for x in range(width))
                f.write(row)


def generate_grid_texture():
    size = 256
    line_width = 4
    green = (60, 130, 50)
    white = (220, 220, 220)

    pixels = [[green for _ in range(size)] for _ in range(size)]

    for y in range(size):
        for x in range(size):
            if x < line_width or y < line_width:
                pixels[y][x] = white

    path = REPO / "textures" / "grid.rgb"
    write_sgi_rgb(str(path), size, size, pixels)
    print(f"  {path}")


def generate_runway_texture():
    width, height = 256, 256
    gray = (90, 90, 90)
    white = (220, 220, 220)

    pixels = [[gray for _ in range(width)] for _ in range(height)]

    # Center line dashes
    dash_len = 20
    gap_len = 15
    cx = width // 2
    for y in range(height):
        in_dash = (y % (dash_len + gap_len)) < dash_len
        if in_dash:
            for dx in range(-1, 2):
                pixels[y][cx + dx] = white

    # Edge lines
    for y in range(height):
        for dx in range(2):
            pixels[y][5 + dx] = white
            pixels[y][width - 7 + dx] = white

    path = REPO / "textures" / "runway.rgb"
    write_sgi_rgb(str(path), width, height, pixels)
    print(f"  {path}")


def ac3d_box(name, material_idx, w, h, d, texture=None, texrep_x=1, texrep_y=1):
    """Generate AC3D box: w=east/west, h=up, d=north/south, centered on ground."""
    hw, hd = w / 2, d / 2
    lines = []
    lines.append(f"OBJECT poly")
    lines.append(f'name "{name}"')
    if texture:
        lines.append(f'texture "{texture}"')
    lines.append("crease 45.0")
    lines.append(f"numvert 8")
    # Bottom 4 (y=0), then top 4 (y=h)
    lines.append(f"{-hw} 0 {-hd}")
    lines.append(f"{hw} 0 {-hd}")
    lines.append(f"{hw} 0 {hd}")
    lines.append(f"{-hw} 0 {hd}")
    lines.append(f"{hw} {h} {-hd}")
    lines.append(f"{-hw} {h} {-hd}")
    lines.append(f"{-hw} {h} {hd}")
    lines.append(f"{hw} {h} {hd}")

    faces = [
        (0, 1, 2, 3),  # bottom
        (4, 5, 6, 7),  # top
        (0, 1, 4, 5),  # south
        (2, 3, 6, 7),  # north
        (1, 2, 7, 4),  # east
        (3, 0, 5, 6),  # west
    ]
    lines.append(f"numsurf {len(faces)}")
    for face in faces:
        lines.append("SURF 0x20")
        lines.append(f"mat {material_idx}")
        lines.append("refs 4")
        for i, vi in enumerate(face):
            u = (i % 2) * texrep_x
            v = (i // 2) * texrep_y
            lines.append(f"{vi} {u} {v}")
    lines.append("kids 0")
    return "\n".join(lines)


def ac3d_cylinder(name, material_idx, radius, height, y_offset, segments=12):
    """Generate AC3D cylinder (vertical, centered on X/Z, base at y_offset)."""
    import math

    lines = []
    lines.append(f"OBJECT poly")
    lines.append(f'name "{name}"')
    lines.append("crease 45.0")

    # Vertices: bottom ring, then top ring
    verts = []
    for i in range(segments):
        angle = 2 * math.pi * i / segments
        x = radius * math.cos(angle)
        z = radius * math.sin(angle)
        verts.append((x, y_offset, z))
    for i in range(segments):
        angle = 2 * math.pi * i / segments
        x = radius * math.cos(angle)
        z = radius * math.sin(angle)
        verts.append((x, y_offset + height, z))

    lines.append(f"numvert {len(verts)}")
    for v in verts:
        lines.append(f"{v[0]:.4f} {v[1]:.4f} {v[2]:.4f}")

    # Side faces (quads)
    n = segments
    num_faces = n + 2  # sides + top + bottom
    lines.append(f"numsurf {num_faces}")

    for i in range(n):
        i2 = (i + 1) % n
        lines.append("SURF 0x20")
        lines.append(f"mat {material_idx}")
        lines.append("refs 4")
        lines.append(f"{i} 0 0")
        lines.append(f"{i2} 1 0")
        lines.append(f"{i2 + n} 1 1")
        lines.append(f"{i + n} 0 1")

    # Top face
    lines.append("SURF 0x20")
    lines.append(f"mat {material_idx}")
    lines.append(f"refs {n}")
    for i in range(n):
        lines.append(f"{i + n} 0 0")

    # Bottom face
    lines.append("SURF 0x20")
    lines.append(f"mat {material_idx}")
    lines.append(f"refs {n}")
    for i in reversed(range(n)):
        lines.append(f"{i} 0 0")

    lines.append("kids 0")
    return "\n".join(lines)


def write_ac3d(filepath, materials, objects_text, num_objects):
    with open(filepath, "w") as f:
        f.write("AC3Db\n")
        for mat in materials:
            r, g, b = mat["r"], mat["g"], mat["b"]
            ar, ag, ab = mat.get("ar", r), mat.get("ag", g), mat.get("ab", b)
            f.write(
                f'MATERIAL "{mat["name"]}" '
                f"rgb {r} {g} {b}  "
                f"amb {ar} {ag} {ab}  "
                f"emis 0 0 0  spec 0.5 0.5 0.5  shi 64  trans 0\n"
            )
        f.write("OBJECT world\n")
        f.write(f"kids {num_objects}\n")
        f.write(objects_text)
        f.write("\n")
    print(f"  {filepath}")


def generate_terrain():
    half = TERRAIN_HALF_FT
    tex_tiles = GRID_CELLS  # 60 tiles across

    materials = [{"name": "terrain", "r": 1, "g": 1, "b": 1}]

    terrain = f"""OBJECT poly
name "terrain"
texture "grid.rgb"
crease 45.0
numvert 4
{-half} 0 {-half}
{half} 0 {-half}
{half} 0 {half}
{-half} 0 {half}
numsurf 1
SURF 0x20
mat 0
refs 4
0 0 0
1 {tex_tiles} 0
2 {tex_tiles} {tex_tiles}
3 0 {tex_tiles}
kids 0"""

    write_ac3d(REPO / "objects" / "sil_terrain.ac", materials, terrain, 1)


def generate_runway():
    # 100ft wide x 1000ft long runway
    w, d = 100, 1000
    hw, hd = w / 2, d / 2

    materials = [{"name": "runway", "r": 1, "g": 1, "b": 1}]

    runway = f"""OBJECT poly
name "runway"
texture "runway.rgb"
crease 45.0
numvert 4
{-hw} 0.05 {-hd}
{hw} 0.05 {-hd}
{hw} 0.05 {hd}
{-hw} 0.05 {hd}
numsurf 1
SURF 0x20
mat 0
refs 4
0 0 0
1 1 0
2 1 10
3 0 10
kids 0"""

    write_ac3d(REPO / "objects" / "sil_runway.ac", materials, runway, 1)


def generate_barn():
    materials = [
        {"name": "barn_walls", "r": 0.6, "g": 0.2, "b": 0.1},
        {"name": "barn_roof", "r": 0.3, "g": 0.3, "b": 0.3},
    ]

    w, d, wall_h = 30, 50, 20  # ft
    peak_h = 30
    hw, hd = w / 2, d / 2

    walls = ac3d_box("walls", 0, w, wall_h, d)

    # Roof: triangular prism (2 slope quads + 2 gable triangles)
    roof = f"""OBJECT poly
name "roof"
crease 45.0
numvert 6
{-hw-2} {wall_h} {-hd}
{hw+2} {wall_h} {-hd}
0 {peak_h} {-hd}
{-hw-2} {wall_h} {hd}
{hw+2} {wall_h} {hd}
0 {peak_h} {hd}
numsurf 4
SURF 0x20
mat 1
refs 4
0 0 0
2 0.5 1
5 0.5 1
3 0 0
SURF 0x20
mat 1
refs 4
1 0 0
4 0 0
5 0.5 1
2 0.5 1
SURF 0x20
mat 1
refs 3
0 0 0
1 1 0
2 0.5 1
SURF 0x20
mat 1
refs 3
3 0 0
5 0.5 1
4 1 0
kids 0"""

    write_ac3d(REPO / "objects" / "sil_barn.ac", materials, walls + "\n" + roof, 2)


def generate_silo():
    materials = [
        {"name": "silo_body", "r": 0.8, "g": 0.8, "b": 0.75},
        {"name": "silo_cap", "r": 0.4, "g": 0.4, "b": 0.45},
    ]

    body = ac3d_cylinder("body", 0, radius=8, height=50, y_offset=0, segments=16)
    cap = ac3d_cylinder("cap", 1, radius=9, height=5, y_offset=50, segments=16)

    write_ac3d(REPO / "objects" / "sil_silo.ac", materials, body + "\n" + cap, 2)


def generate_watertower():
    materials = [
        {"name": "tower_legs", "r": 0.4, "g": 0.4, "b": 0.4},
        {"name": "tower_tank", "r": 0.6, "g": 0.6, "b": 0.7},
    ]

    # 4 legs as thin boxes
    leg_w = 2
    leg_spread = 10
    leg_h = 40
    legs = []
    for dx, dz in [(-1, -1), (1, -1), (1, 1), (-1, 1)]:
        x = dx * leg_spread
        z = dz * leg_spread
        legs.append(
            f"""OBJECT poly
name "leg"
loc {x} 0 {z}
crease 45.0
numvert 8
{-leg_w/2} 0 {-leg_w/2}
{leg_w/2} 0 {-leg_w/2}
{leg_w/2} 0 {leg_w/2}
{-leg_w/2} 0 {leg_w/2}
{leg_w/2} {leg_h} {-leg_w/2}
{-leg_w/2} {leg_h} {-leg_w/2}
{-leg_w/2} {leg_h} {leg_w/2}
{leg_w/2} {leg_h} {leg_w/2}
numsurf 4
SURF 0x20
mat 0
refs 4
0 0 0
1 1 0
4 1 1
5 0 1
SURF 0x20
mat 0
refs 4
1 0 0
2 1 0
7 1 1
4 0 1
SURF 0x20
mat 0
refs 4
2 0 0
3 1 0
6 1 1
7 0 1
SURF 0x20
mat 0
refs 4
3 0 0
0 1 0
5 1 1
6 0 1
kids 0"""
        )

    tank = ac3d_cylinder("tank", 1, radius=12, height=15, y_offset=leg_h, segments=16)

    all_objects = "\n".join(legs) + "\n" + tank
    write_ac3d(REPO / "objects" / "sil_watertower.ac", materials, all_objects, 5)  # 4 legs + 1 tank


def generate_hangar():
    materials = [
        {"name": "hangar_walls", "r": 0.7, "g": 0.7, "b": 0.65},
        {"name": "hangar_roof", "r": 0.25, "g": 0.3, "b": 0.35},
    ]

    walls = ac3d_box("walls", 0, 80, 25, 60)

    # Flat roof, slightly wider
    roof = ac3d_box("roof", 1, 84, 2, 64)
    roof = roof.replace('name "roof"\n', 'name "roof"\nloc 0 25 0\n')

    write_ac3d(REPO / "objects" / "sil_hangar.ac", materials, walls + "\n" + roof, 2)


def generate_house():
    materials = [
        {"name": "house_walls", "r": 0.85, "g": 0.8, "b": 0.7},
        {"name": "house_roof", "r": 0.5, "g": 0.15, "b": 0.1},
    ]

    w, d, wall_h = 25, 35, 15
    peak_h = 22
    hw, hd = w / 2, d / 2

    walls = ac3d_box("walls", 0, w, wall_h, d)

    roof = f"""OBJECT poly
name "roof"
crease 45.0
numvert 6
{-hw-1} {wall_h} {-hd}
{hw+1} {wall_h} {-hd}
0 {peak_h} {-hd}
{-hw-1} {wall_h} {hd}
{hw+1} {wall_h} {hd}
0 {peak_h} {hd}
numsurf 4
SURF 0x20
mat 1
refs 4
0 0 0
2 0.5 1
5 0.5 1
3 0 0
SURF 0x20
mat 1
refs 4
1 0 0
4 0 0
5 0.5 1
2 0.5 1
SURF 0x20
mat 1
refs 3
0 0 0
1 1 0
2 0.5 1
SURF 0x20
mat 1
refs 3
3 0 0
5 0.5 1
4 1 0
kids 0"""

    write_ac3d(REPO / "objects" / "sil_house.ac", materials, walls + "\n" + roof, 2)


def generate_scenery_xml():
    xml = """<?xml version="1.0"?>

<crrcsimSceneryFile version="3">

  <name>SIL Testing Field</name>
  <categories>
    <category>Field</category>
  </categories>
  <description>
    <en>
Large flat field with 100m grid and reference objects for SIL testing.
6km x 6km terrain.
    </en>
  </description>
  <changelog>
    <change>
      <date>2026-03-28</date>
      <author>copperpunk</author>
      <en>Created for smol SIL testing.</en>
    </change>
  </changelog>

  <views>
    <position name="default" north="-80" east="-40" height="6" />
  </views>

  <start>
    <position name="runway" north="0" east="0" height="0" />
  </start>

  <default>
    <wind velocity="0" direction="0" />
  </default>

  <sky type="original" texture="textures/clouds.rgb" radius="12000">
    <descr_short>
      <en>Sky dome</en>
    </descr_short>
  </sky>

  <scene type="model-based" getHeight_mode="2">

    <object filename="sil_terrain.ac" terrain="1">
      <instance north="0" east="0" height="0" />
    </object>

    <object filename="sil_runway.ac" terrain="0">
      <instance north="0" east="0" height="0" />
    </object>

    <!-- Reference objects scattered around the field -->

    <!-- Barn: 500m north, 300m east -->
    <object filename="sil_barn.ac" terrain="0">
      <instance north="1640" east="984" height="0" h="20" />
    </object>

    <!-- Water tower: 800m north, 500m west -->
    <object filename="sil_watertower.ac" terrain="0">
      <instance north="2625" east="-1640" height="0" />
    </object>

    <!-- Silo: 400m south, 600m east -->
    <object filename="sil_silo.ac" terrain="0">
      <instance north="-1312" east="1968" height="0" />
    </object>

    <!-- Hangar: near the runway, 100m south -->
    <object filename="sil_hangar.ac" terrain="0">
      <instance north="-328" east="150" height="0" h="0" />
    </object>

    <!-- House: 1km north, 200m west -->
    <object filename="sil_house.ac" terrain="0">
      <instance north="3280" east="-656" height="0" h="45" />
    </object>

    <!-- Second barn: 600m south, 800m west -->
    <object filename="sil_barn.ac" terrain="0">
      <instance north="-1968" east="-2625" height="0" h="70" />
    </object>

    <!-- Second silo: near the first barn -->
    <object filename="sil_silo.ac" terrain="0">
      <instance north="1750" east="1100" height="0" />
    </object>

  </scene>
</crrcsimSceneryFile>
"""
    path = REPO / "scenery" / "sil.xml"
    with open(path, "w") as f:
        f.write(xml)
    print(f"  {path}")


if __name__ == "__main__":
    print("Generating SIL scenery assets...")

    print("\nTextures:")
    generate_grid_texture()
    generate_runway_texture()

    print("\nObjects:")
    generate_terrain()
    generate_runway()
    generate_barn()
    generate_silo()
    generate_watertower()
    generate_hangar()
    generate_house()

    print("\nScenery:")
    generate_scenery_xml()

    print("\nDone. Set location in crrcsim.xml to: scenery/sil.xml")
