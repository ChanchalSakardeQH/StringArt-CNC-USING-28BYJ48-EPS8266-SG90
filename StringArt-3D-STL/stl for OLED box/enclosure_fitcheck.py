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
Assembly fit check for enclosure.py.

Puts every part where it goes, adds a model of the OLED module built from the
drawing, and measures collisions and clearances. Zero volume means clear.
Run after changing anything in enclosure.py.
"""
import os
import numpy as np
import trimesh
import enclosure as E

os.chdir(os.path.dirname(os.path.abspath(__file__)))
load = lambda n: trimesh.load(f"stl/{n}.stl")
bad = 0

def clash(a, b):
    i = a.intersection(b)
    return 0.0 if i.is_empty else i.volume

def check(label, ok, detail=""):
    global bad
    bad += not ok
    print(f"  [{'ok' if ok else 'FAIL'}] {label}" + (f"   {detail}" if detail else ""))

def xform(mesh, M):
    m = mesh.copy(); m.apply_transform(M); return m

# ---- the OLED module, in frame coordinates (z = depth from the bezel face) --
def oled_model():
    gy = E.glass_centre_y()
    glass = E.box(E.GLASS_W, E.GLASS_H, E.GLASS_T, (0, gy, E.LIP + E.GLASS_T / 2))
    pcb = E.box(E.PCB_W, E.PCB_H, E.PCB_T, (0, 0, E.pcb_front_z() + E.PCB_T / 2))
    return glass, pcb

# ---- frame coordinates -> box coordinates (front wall, bezel outside) ------
def frame_to_box():
    M = np.eye(4)
    M[:3, 0] = [1, 0, 0]          # x stays x
    M[:3, 1] = [0, 0, 1]          # frame y (up the display) -> box z
    M[:3, 2] = [0, 1, 0]          # frame z (depth, backward) -> box +y (inward)
    # bezel flange sits on the OUTSIDE of the front wall
    M[:3, 3] = [0, -E.BOX_W / 2 - E.FL_T, E.OLED_Z]
    return M

def retainer_to_frame():
    # Printed fingers-up. In use it is turned over (a half-turn about y), so
    # the fingered face -- printed z = RT_T -- lands on the wall's inner face
    # at depth FL_T + WALL, the plate runs back from there, and the fingers
    # reach forward to the board.
    M = trimesh.transformations.rotation_matrix(np.pi, [0, 1, 0])
    M = trimesh.transformations.translation_matrix([0, 0, E.FL_T + E.WALL + E.RT_T]) @ M
    return M

print("OLED module in the frame")
frame = load("oled_frame")
glass, pcb = oled_model()
check("glass clears the frame", clash(frame, glass) < 0.01, f"{clash(frame, glass):.3f} mm3")
check("board clears the frame", clash(frame, pcb) < 0.01, f"{clash(frame, pcb):.3f} mm3")
# board front must actually rest on the ledge: nudge it 0.05 forward -> contact
nudged = pcb.copy(); nudged.apply_translation((0, 0, -0.05))
check("board rests on the ledge (touches when nudged forward)", clash(frame, nudged) > 0.01)
check("glass sits behind the front lip", E.LIP > 0, f"recessed {E.LIP} mm")

print("\nRetainer")
ret = xform(load("oled_retainer"), retainer_to_frame())
check("retainer clears the frame", clash(frame, ret) < 0.01, f"{clash(frame, ret):.3f} mm3")
check("fingers clear the board (no squeeze on the glass)", clash(ret, pcb) < 0.001)
touch = pcb.copy(); touch.apply_translation((0, 0, E.FINGER_GAP + 0.05))
check("fingers hold the board to within the set gap", clash(ret, touch) > 0.001,
      f"gap {E.FINGER_GAP} mm")
fx = 11.75 - 0.75
check("fingers land inside the board outline", fx + 1.5 <= E.PCB_W / 2 + 0.01,
      f"x {fx:.2f}..{fx+1.5:.2f}, board edge {E.PCB_W/2}")

print("\nIn the box")
body = load("enclosure_box")
fr_b = xform(frame, frame_to_box())
rt_b = xform(ret, frame_to_box())
check("frame body fits the wall opening", clash(body, fr_b) < 0.01, f"{clash(body, fr_b):.3f} mm3")
check("retainer clears the box", clash(body, rt_b) < 0.01, f"{clash(body, rt_b):.3f} mm3")
ow, oh = E.wall_opening_for_frame()
check("bezel covers the opening", E.FL_W > ow + 4 and E.FL_H > oh + 4,
      f"overlap {(E.FL_W-ow)/2:.1f} / {(E.FL_H-oh)/2:.1f} mm each side")
check("retainer covers the opening", E.RT_W > ow + 4 and E.RT_H > oh + 4,
      f"overlap {(E.RT_W-ow)/2:.1f} / {(E.RT_H-oh)/2:.1f} mm each side")
# "clears" alone can't tell clamped from floating: nudge each part toward the
# wall and it must touch.
r_in = rt_b.copy(); r_in.apply_translation((0, -0.05, 0))
f_in = fr_b.copy(); f_in.apply_translation((0, 0.05, 0))
check("retainer bears on the inside of the wall", clash(body, r_in) > 0.01)
check("bezel bears on the outside of the wall", clash(body, f_in) > 0.01)
check("wall is clamped, not the frame body", E.FB_D < E.WALL,
      f"body {E.FB_D} < wall {E.WALL}")

# retainer screws: an M3 through retainer and into the frame pilot
for sx in (-1, 1):
    bolt = E.cyl(1.4, 10, (sx * E.SCREW_X, 0, E.FL_T + E.WALL - 3), sections=24)
    check(f"retainer screw {'L' if sx<0 else 'R'} passes the retainer",
          clash(ret, bolt) < 0.01)
    pilot_hit = clash(frame, E.cyl(E.M3_PILOT / 2 - 0.05, 3.0,
                                   (sx * E.SCREW_X, 0, E.frame_back_z() - 1.5), sections=24))
    check(f"retainer screw {'L' if sx<0 else 'R'} meets its pilot hole", pilot_hit < 0.01)

print("\nLid")
lid = load("enclosure_lid")
lid_use = xform(lid, trimesh.transformations.reflection_matrix([0, 0, 0], [0, 0, 1]))
lid_use.apply_translation((0, 0, E.BODY_H + E.LID_T))
check("lid lip clears walls and bosses", clash(body, lid_use) < 0.01,
      f"{clash(body, lid_use):.3f} mm3")
check("closed height is 5.00 in", abs(lid_use.bounds[1][2] - E.BOX_H) < 0.01,
      f"{lid_use.bounds[1][2]:.2f} mm")
check("lid lip clears the OLED retainer", lid_use.bounds[0][2] > rt_b.bounds[1][2],
      f"lip bottom {lid_use.bounds[0][2]:.1f}, retainer top {rt_b.bounds[1][2]:.1f}")
for sx in (-1, 1):
    for sy in (-1, 1):
        x, y = E.boss_centre(sx, sy)
        hole = E.cyl(1.4, E.LID_T + 1, (x, y, E.BODY_H + E.LID_T / 2), sections=24)
        pil = E.cyl(E.M3_PILOT / 2 - 0.05, 10, (x, y, E.BODY_H - 5), sections=24)
        if not (clash(lid_use, hole) < 0.01 and clash(body, pil) < 0.01):
            check(f"lid screw at ({x:.0f},{y:.0f}) lines up", False)
check("all four lid screws line up with boss pilots", True)

print("\nTray")
tray = load("enclosure_tray")
tray.apply_translation((0, 0, E.FLOOR + E.STANDOFF_H))
check("tray clears the box", clash(body, tray) < 0.01, f"{clash(body, tray):.3f} mm3")
for sx in (-1, 1):
    for sy in (-1, 1):
        pil = E.cyl(E.M3_PILOT / 2 - 0.05, 6, (sx * E.TRAY_HX, sy * E.TRAY_HY,
                                              E.FLOOR + E.STANDOFF_H - 3), sections=24)
        hole = E.cyl(1.5, E.TRAY_T, (sx * E.TRAY_HX, sy * E.TRAY_HY,
                                     E.FLOOR + E.STANDOFF_H + E.TRAY_T / 2), sections=24)
        if clash(body, pil) > 0.01 or clash(tray, hole) > 0.01:
            check("tray screw lines up", False)
check("all four tray screws line up with standoffs", True)
inner = (E.BOX_L - 2 * E.WALL, E.BOX_W - 2 * E.WALL, E.BODY_H - E.FLOOR)
print(f"        inside the box: {inner[0]:.1f} x {inner[1]:.1f} x {inner[2]:.1f} mm")

print("\nDesk stand")
stand = load("oled_stand")
Ms = E.stand_panel_matrix()
# frame on the stand: bezel on the panel front, t axis = out of the front face
Mf = np.eye(4)
Mf[:3, 0] = Ms[:3, 0]
Mf[:3, 1] = Ms[:3, 1]
Mf[:3, 2] = -Ms[:3, 2]                       # frame depth runs INTO the panel
Mf[:3, 3] = Ms[:3, 3] + Ms[:3, 1] * E.PL_OPEN_V + Ms[:3, 2] * E.FL_T
fr_s = xform(frame, Mf)
rt_s = xform(ret, Mf)
check("frame fits the stand opening", clash(stand, fr_s) < 0.01, f"{clash(stand, fr_s):.3f} mm3")
check("retainer clears the stand", clash(stand, rt_s) < 0.01, f"{clash(stand, rt_s):.3f} mm3")

print("\nPrint bed")
biggest = max((load(n).bounds[1] - load(n).bounds[0]) for n in
              ["enclosure_box"]).tolist()
print(f"        largest part {biggest[0]:.0f} x {biggest[1]:.0f} x {biggest[2]:.0f} mm "
      f"-- needs a bed of at least 165 x 90 mm and 130 mm of height")

print("\n" + ("ALL CLEAR" if bad == 0 else f"{bad} PROBLEM(S) -- fix before printing"))
