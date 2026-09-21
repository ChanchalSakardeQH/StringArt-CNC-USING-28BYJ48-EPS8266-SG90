#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 YOUR NAME
#
# This file is part of ESP8266 String Art CNC.
#
# ESP8266 String Art CNC is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option) any
# later version.
#
# ESP8266 String Art CNC is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# ESP8266 String Art CNC. If not, see <https://www.gnu.org/licenses/>.
"""
String Art CNC -- electronics enclosure and OLED mount.

Produces STLs in stl/:
    enclosure_box.stl     6.3 x 3.3 x 5 in box (with lid), OLED window in front
    enclosure_lid.stl     screws onto the four corner bosses
    enclosure_tray.stl    universal perforated tray for the NodeMCU + ULN2003
    oled_frame.stl        bezel that holds the 0.96" OLED by its board edges
    oled_retainer.stl     clamps the frame to the wall and holds the OLED in
    oled_stand.stl        desk stand that takes the same frame + retainer

The OLED frame is held by its BOARD EDGES in a pocket, not by its mounting
holes. The supplied drawing dimensions the board, glass and flex notch, but not
the hole spacing or exactly where the glass sits vertically -- so nothing here
depends on either. If your module differs, only the small frame needs
reprinting, never the box.

Every dimension is a named constant below. Run:  python3 enclosure.py
"""

import os
import numpy as np
import trimesh
from shapely.geometry import Polygon

IN = 25.4
SEG = 64

# ============================================================== parameters ===

# ---- the box: 6.3 x 3.3 x 5 inches OUTSIDE, lid included ----
BOX_L = 6.3 * IN        # 160.02 mm, x
BOX_W = 3.3 * IN        #  83.82 mm, y  (front face at -y)
BOX_H = 5.0 * IN        # 127.00 mm, z  (closed height, lid included)
WALL  = 2.4
FLOOR = 3.0
LID_T = 3.0
BODY_H = BOX_H - LID_T  # the open box is 124 mm tall; the lid adds 3

OLED_Z = 92.0           # centre height of the OLED window on the front

# ---- 0.96" SSD1306 module, from the drawing ----
PCB_W, PCB_H, PCB_T = 25.0, 27.0, 1.2
GLASS_W, GLASS_H, GLASS_T = 22.0, 16.0, 1.4
# Where the glass sits, measured down from the board's top (header) edge.
# NOT dimensioned on the drawing; 5.5 = glass centred on the board. The glass
# pocket below has 1 mm of slack each way, so an error of up to 1 mm is fine.
GLASS_TOP = 5.5
HEADER_FROM_TOP = 1.7   # pin row centre, from the board's top edge
HEADER_W = 4 * 2.54
NOTCH_LEFT, NOTCH_W = 5.0, 19.0   # flex-cable notch along the bottom edge

# ---- OLED frame ----
FIT = 0.3               # clearance around the board, each side
LIP = 0.6               # plastic in front of the glass: 3 layers at 0.2
GLASS_SLACK = 1.0       # extra glass-pocket height each way (see GLASS_TOP)
FL_T = 2.0              # front bezel flange thickness
FB_D = WALL - 0.2       # body depth: a hair less than the wall, so the
                        # retainer clamps the WALL rather than the frame
FB_W, FB_H = 38.0, 33.0 # body, which sits in the wall opening
FL_W, FL_H = 46.0, 41.0 # visible bezel
OPEN_FIT = 0.2          # body-to-opening clearance, each side
SCREW_X = 16.0          # retainer screws, either side of the display

# ---- retainer ----
RT_W, RT_H, RT_T = 47.0, 43.0, 2.0
FINGER_GAP = 0.1        # leave the board this loose rather than squeeze the glass

# ---- fasteners ----
M3_CLEAR, M3_PILOT = 3.4, 2.5

# ---- tray ----
TRAY_L, TRAY_W, TRAY_T = 150.0, 72.0, 3.0
TRAY_HX, TRAY_HY = 70.0, 31.0     # corner screw positions
STANDOFF_H = 8.0
GRID_PITCH, GRID_D = 5.08, 2.7    # 0.1 in grid: M2.5 / M3 self-tapping, or cable ties

# ================================================================= helpers ===

def box(x, y, z, at=(0, 0, 0)):
    m = trimesh.creation.box(extents=(x, y, z))
    m.apply_translation(at)
    return m

def cyl(r, h, at=(0, 0, 0), axis="z", sections=SEG):
    m = trimesh.creation.cylinder(radius=r, height=h, sections=sections)
    if axis == "x":
        m.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [0, 1, 0]))
    elif axis == "y":
        m.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [1, 0, 0]))
    m.apply_translation(at)
    return m

def hull(points):
    return trimesh.convex.convex_hull(np.asarray(points, dtype=float))

def countersink_z(r_top, r_bot, h, at):
    """Cone for a countersunk head; r_top at z=at.z, narrowing downward."""
    a = np.linspace(0, 2 * np.pi, SEG, endpoint=False)
    x, y, z = at
    top = [(x + r_top * np.cos(t), y + r_top * np.sin(t), z) for t in a]
    bot = [(x + r_bot * np.cos(t), y + r_bot * np.sin(t), z - h) for t in a]
    return hull(top + bot)

def stadium_y(width, height, depth, at):
    """Upright slot bored along y. The rounded top prints without support."""
    x, y, z = at
    r = width / 2
    run = max(0.0, height - width)
    m = box(width, depth, run, at)
    for s in (-1, 1):
        m = m.union(cyl(r, depth, (x, y, z + s * run / 2), axis="y"))
    return m

def stadium_x(width, height, depth, at):
    x, y, z = at
    r = width / 2
    run = max(0.0, height - width)
    m = box(depth, width, run, at)
    for s in (-1, 1):
        m = m.union(cyl(r, depth, (x, y, z + s * run / 2), axis="x"))
    return m

def union_all(meshes):
    return trimesh.boolean.union(meshes) if len(meshes) > 1 else meshes[0]

def report(mesh, name):
    e = mesh.bounds[1] - mesh.bounds[0]
    print(f"  {name:20s} {e[0]:7.1f} x {e[1]:6.1f} x {e[2]:6.1f} mm   "
          f"{mesh.volume / 1000:6.1f} cm3   watertight={mesh.is_watertight}")
    return mesh

# ===================================================== OLED frame (bezel) ===
# Frame coordinates, which are also its print orientation:
#   x across, y up the display, z = depth from the FRONT face (z=0, on the
#   bed) backward. Front bezel flange first, the narrower body on top of it,
#   every pocket open upward -- so it prints face-down with no support at all.

def glass_centre_y():
    return PCB_H / 2 - GLASS_TOP - GLASS_H / 2

def pcb_front_z():
    return LIP + GLASS_T                       # 2.0: board front face

def pcb_back_z():
    return pcb_front_z() + PCB_T               # 3.2

def frame_back_z():
    return FL_T + FB_D                         # 4.2

def oled_frame():
    part = box(FL_W, FL_H, FL_T, (0, 0, FL_T / 2))
    part = part.union(box(FB_W, FB_H, FB_D, (0, 0, FL_T + FB_D / 2)))

    gy = glass_centre_y()
    zf, zb = pcb_front_z(), frame_back_z()
    cuts = []

    # Window: slightly smaller than the glass, so a thin lip hides the glass
    # edge and absorbs a little placement error. 45-degree chamfer at the face.
    ww, wh = GLASS_W, GLASS_H - 1.0
    ch = 0.5
    cuts.append(box(ww, wh, LIP + 0.2, (0, gy, LIP / 2)))
    cuts.append(hull([(sx * (ww / 2 + ch), gy + sy * (wh / 2 + ch), -0.01)
                      for sx in (-1, 1) for sy in (-1, 1)] +
                     [(sx * ww / 2, gy + sy * wh / 2, ch)
                      for sx in (-1, 1) for sy in (-1, 1)]))

    # Glass pocket, with vertical slack for the undimensioned glass position.
    cuts.append(box(GLASS_W + 2 * FIT, GLASS_H + 2 * GLASS_SLACK, GLASS_T + 0.1,
                    (0, gy, LIP + (GLASS_T + 0.1) / 2)))

    # Board pocket, open at the back. The board rests on the ledge at z=2.0.
    depth = zb - zf + 1.0
    cuts.append(box(PCB_W + 2 * FIT, PCB_H + 2 * FIT, depth, (0, 0, zf + depth / 2)))

    # The flex cable wraps round the bottom edge through the notch; give it
    # room below the board outline.
    nx = -PCB_W / 2 + NOTCH_LEFT + NOTCH_W / 2
    cuts.append(box(NOTCH_W + 1.0, 1.4, depth,
                    (nx, -PCB_H / 2 - FIT - 0.5, zf + depth / 2)))

    # ...and where it lies across the board front, between the glass and the
    # bottom edge, so the ledge doesn't press on it.
    yt, yb = gy - GLASS_H / 2 - GLASS_SLACK, -PCB_H / 2 - FIT - 1.0
    cuts.append(box(NOTCH_W + 1.0, yt - yb, zf - 1.2 + 0.01,
                    (nx, (yt + yb) / 2, 1.2 + (zf - 1.2) / 2)))

    # Header solder joints stand proud of the board front; relieve the ledge.
    hy = PCB_H / 2 - HEADER_FROM_TOP
    cuts.append(box(HEADER_W + 4.0, 3.6, zf - 0.8 + 0.01, (0, hy, 0.8 + (zf - 0.8) / 2)))

    # Pilots for the retainer's two screws.
    for sx in (-1, 1):
        cuts.append(cyl(M3_PILOT / 2, zb - 0.8 + 0.01,
                        (sx * SCREW_X, 0, 0.8 + (zb - 0.8) / 2)))

    return report(part.difference(union_all(cuts)), "oled_frame")

# ================================================================ retainer ===
# Printed flat, fingers up. In use it is flipped: fingers point at the board,
# the plate bears on the INSIDE of the wall, and two screws into the frame
# clamp the wall between plate and bezel. One pair of screws does both jobs.

def finger_len():
    # Plate face sits on the wall's inner face: FL_T + WALL from the front.
    return (FL_T + WALL) - pcb_back_z() - FINGER_GAP

def oled_retainer():
    part = box(RT_W, RT_H, RT_T, (0, 0, RT_T / 2))
    # Open centre: clears the parts on the back of the board, the header pins
    # at the top, and the folded flex at the bottom.
    part = part.difference(box(21.6, 30.0, RT_T + 2, (0, -0.5, RT_T / 2)))
    fl = finger_len()
    for sx in (-1, 1):
        # Fingers land on the board's side margins, mid-height -- clear of
        # the corner mounting holes, the header and the flex.
        part = part.union(box(1.5, 12.0, fl, (sx * 11.75, 0, RT_T + fl / 2)))
        part = part.difference(cyl(M3_CLEAR / 2, RT_T + 2, (sx * SCREW_X, 0, RT_T / 2)))
    return report(part, "oled_retainer")

# =================================================================== box ====

def wall_opening_for_frame():
    return FB_W + 2 * OPEN_FIT, FB_H + 2 * OPEN_FIT

def corner_boss(sx, sy):
    """Lid screw boss in a top inner corner, tapered 45 degrees underneath so
    the box prints upright with no support."""
    cx, cy = sx * (BOX_L / 2 - WALL), sy * (BOX_W / 2 - WALL)
    size, top_h = 9.0, 20.0
    taper = size * np.sqrt(2)
    h = top_h + taper
    z0 = BODY_H - h
    # overlap the walls by 1 mm so the boss fuses with them
    bx = cx - sx * (size - 1.0) / 2
    by = cy - sy * (size - 1.0) / 2
    m = box(size + 1.0, size + 1.0, h, (bx, by, z0 + h / 2))
    n = np.array([sx / np.sqrt(2), sy / np.sqrt(2), 1.0])
    m = m.slice_plane(plane_origin=[cx, cy, z0], plane_normal=n / np.linalg.norm(n), cap=True)
    return m

def boss_centre(sx, sy):
    return (sx * (BOX_L / 2 - WALL - 4.5), sy * (BOX_W / 2 - WALL - 4.5))

def enclosure_box():
    part = box(BOX_L, BOX_W, BODY_H, (0, 0, BODY_H / 2))
    part = part.difference(box(BOX_L - 2 * WALL, BOX_W - 2 * WALL, BODY_H,
                               (0, 0, FLOOR + BODY_H / 2)))
    adds = [corner_boss(sx, sy) for sx in (-1, 1) for sy in (-1, 1)]
    for sx in (-1, 1):
        for sy in (-1, 1):
            adds.append(cyl(4.0, STANDOFF_H, (sx * TRAY_HX, sy * TRAY_HY,
                                              FLOOR + STANDOFF_H / 2)))
    part = union_all([part] + adds)

    cuts = []
    # lid screw pilots
    for sx in (-1, 1):
        for sy in (-1, 1):
            x, y = boss_centre(sx, sy)
            cuts.append(cyl(M3_PILOT / 2, 14.0, (x, y, BODY_H - 7.0 + 0.01)))
            # tray standoff pilots
            cuts.append(cyl(M3_PILOT / 2, STANDOFF_H, (sx * TRAY_HX, sy * TRAY_HY,
                                                       FLOOR + STANDOFF_H / 2 + 1.0)))
    # floor holes for fixing the box down
    for sx in (-1, 1):
        for sy in (-1, 1):
            cuts.append(cyl(M3_CLEAR / 2, FLOOR + 2, (sx * 50.0, sy * 20.0, FLOOR / 2)))

    # front: the OLED opening
    ow, oh = wall_opening_for_frame()
    cuts.append(box(ow, WALL + 2, oh, (0, -BOX_W / 2 + WALL / 2, OLED_Z)))

    # back: cable port (upright, round-topped) and a DC jack hole
    cuts.append(stadium_y(16.0, 36.0, WALL + 2, (45.0, BOX_W / 2 - WALL / 2, 34.0)))
    cuts.append(cyl(8.2 / 2, WALL + 2, (-45.0, BOX_W / 2 - WALL / 2, 30.0), axis="y"))

    # ends: ventilation
    for sx in (-1, 1):
        for y in (-20, -12, -4, 4, 12, 20):
            cuts.append(stadium_x(3.0, 36.0, WALL + 2, (sx * (BOX_L / 2 - WALL / 2), y, 70.0)))

    return report(part.difference(union_all(cuts)), "enclosure_box")

# =================================================================== lid ====
# Printed outer face down, lip up. The countersinks are on the bed side, which
# is the outside once the lid is turned over.

def enclosure_lid():
    lip_h, lip_w, gap = 5.0, 2.0, 0.3
    part = box(BOX_L, BOX_W, LID_T, (0, 0, LID_T / 2))
    ow, oy = BOX_L - 2 * WALL - 2 * gap, BOX_W - 2 * WALL - 2 * gap
    ring = box(ow, oy, lip_h, (0, 0, LID_T + lip_h / 2)).difference(
        box(ow - 2 * lip_w, oy - 2 * lip_w, lip_h + 2, (0, 0, LID_T + lip_h / 2)))
    # notch the lip round the corner bosses
    for sx in (-1, 1):
        for sy in (-1, 1):
            cx, cy = sx * (BOX_L / 2 - WALL), sy * (BOX_W / 2 - WALL)
            ring = ring.difference(box(22.0, 22.0, lip_h + 2, (cx, cy, LID_T + lip_h / 2)))
    part = part.union(ring)

    cuts = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            x, y = boss_centre(sx, sy)
            cuts.append(cyl(M3_CLEAR / 2, LID_T + 2, (x, y, LID_T / 2)))
            cuts.append(countersink_z(3.2, 1.7, 1.6, (x, y, 1.6)).apply_transform(
                trimesh.transformations.reflection_matrix([0, 0, 0.8], [0, 0, 1])))
    for i in range(5):
        x = -48 + i * 24
        cuts.append(box(3.0, 40.0, LID_T + 2, (x, 0, LID_T / 2)))
    return report(part.difference(union_all(cuts)), "enclosure_lid")

# ================================================================== tray ====

def enclosure_tray():
    part = box(TRAY_L, TRAY_W, TRAY_T, (0, 0, TRAY_T / 2))
    cuts = []
    corners = [(sx * TRAY_HX, sy * TRAY_HY) for sx in (-1, 1) for sy in (-1, 1)]
    for x, y in corners:
        cuts.append(cyl(M3_CLEAR / 2, TRAY_T + 2, (x, y, TRAY_T / 2)))
    nx = int((TRAY_L - 8) // GRID_PITCH)
    ny = int((TRAY_W - 8) // GRID_PITCH)
    for i in range(nx + 1):
        for j in range(ny + 1):
            x = -nx * GRID_PITCH / 2 + i * GRID_PITCH
            y = -ny * GRID_PITCH / 2 + j * GRID_PITCH
            if any(np.hypot(x - cx, y - cy) < 5.5 for cx, cy in corners):
                continue
            cuts.append(cyl(GRID_D / 2, TRAY_T + 2, (x, y, TRAY_T / 2), sections=12))
    # two slots so wires can drop underneath
    for x in (-40.0, 40.0):
        cuts.append(box(30.0, 8.0, TRAY_T + 2, (x, 0, TRAY_T / 2)))
    return report(part.difference(union_all(cuts)), "enclosure_tray")

# ================================================================= stand ====
# Stands on its base. The panel leans back 20 degrees, so its back face is a
# 20-degree overhang and the opening a short bridge -- both print unsupported.

STAND_TILT = np.radians(20)
PL_W, PL_H = 56.0, 56.0
PL_OPEN_V = 30.0        # opening centre, measured up the panel

def stand_panel_matrix():
    s, c = np.sin(STAND_TILT), np.cos(STAND_TILT)
    M = np.eye(4)
    M[:3, 0] = [1, 0, 0]           # x: across
    M[:3, 1] = [0, s, c]           # v: up the panel
    M[:3, 2] = [0, -c, s]          # t: out of the front face
    M[:3, 3] = [0, 2.0, 4.0]       # front-bottom edge sits on the base
    return M

def oled_stand():
    base = box(60.0, 50.0, 4.0, (0, 25.0, 2.0))
    # panel in its own coordinates: t from -WALL (back) to 0 (front face)
    panel = box(PL_W, PL_H, WALL, (0, PL_H / 2, -WALL / 2))
    ow, oh = wall_opening_for_frame()
    panel = panel.difference(box(ow, oh, WALL + 2, (0, PL_OPEN_V, -WALL / 2)))
    panel.apply_transform(stand_panel_matrix())

    s, c = np.sin(STAND_TILT), np.cos(STAND_TILT)
    ya, za = 2.0 + WALL * c, 4.0 - WALL * s
    yb, zb = 2.0 + PL_H * s + WALL * c, 4.0 + PL_H * c - WALL * s
    prof = Polygon([(ya, 4.0), (yb, zb), (yb, 4.0)])
    P = np.eye(4)
    P[:3, :3] = [[0, 0, 1], [1, 0, 0], [0, 1, 0]]   # (a,b,e) -> (e,a,b)
    cheeks = []
    for sx in (-1, 1):
        ch = trimesh.creation.extrude_polygon(prof, 3.0)
        ch.apply_transform(P)
        ch.apply_translation((sx * (PL_W / 2 - 1.5) - 1.5, 0, 0))
        cheeks.append(ch)
    part = union_all([base, panel] + cheeks)
    for sx in (-1, 1):
        part = part.difference(cyl(M3_CLEAR / 2, 6.0, (sx * 20.0, 40.0, 2.0)))
    return report(part, "oled_stand")

# ================================================================== main ====

if __name__ == "__main__":
    out = os.environ.get("OUT", os.path.join(os.path.dirname(os.path.abspath(__file__)), "stl"))
    os.makedirs(out, exist_ok=True)
    print(f"Box: {BOX_L:.2f} x {BOX_W:.2f} x {BOX_H:.2f} mm outside "
          f"({BOX_L/IN:.1f} x {BOX_W/IN:.1f} x {BOX_H/IN:.1f} in), lid included\n")
    for fn in (enclosure_box, enclosure_lid, enclosure_tray,
               oled_frame, oled_retainer, oled_stand):
        fn().export(os.path.join(out, fn.__name__ + ".stl"))
    print(f"\nWritten to {out}")
