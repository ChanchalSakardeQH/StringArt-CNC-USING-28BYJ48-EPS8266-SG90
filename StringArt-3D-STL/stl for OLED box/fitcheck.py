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
Fit check: can an M3 actually pass through the column slot and the bracket?

Probes the bolt path with a cylinder and measures how much material is in the
way. Anything above zero means the bolt does not fit. Run after any change to
parts.py -- this is the check that would have caught the blind groove.
"""
import os, numpy as np, trimesh
import parts as P
os.chdir(os.path.dirname(os.path.abspath(__file__)))

M3_PROBE_R = P.M3_CLEAR / 2

def bolt(x, y, z, length=40):
    c = trimesh.creation.cylinder(radius=M3_PROBE_R, height=length, sections=24)
    c.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [0, 1, 0]))
    c.apply_translation((x, y, z))
    return c

def blocked(mesh, probe):
    inter = mesh.intersection(probe)
    return 0.0 if inter.is_empty else inter.volume

post = trimesh.load("stl/servo_post.stl")
brac = trimesh.load("stl/servo_bracket.stl")

print("Column slots -- material in an M3 bolt path (mm3, 0 = clear)\n")
# A bolt sits on the slot's centreline, so its usable travel stops half a
# diameter short of each rounded end -- probing the geometric tips would
# "fail" on a perfectly good slot.
LO = P.SLOT_BOTTOM + P.M3_CLEAR / 2
HI = P.SLOT_TOP - P.M3_CLEAR / 2
zs = [LO, LO + 8, 45, 60, 75, HI]
bad = 0
for z in zs:
    row = []
    for sy in (-1, 1):
        v = blocked(post, bolt(5, sy * P.BOLT_Y, z))
        row.append(v)
        if v > 1.0:   # tolerance for facet noise on the probe
            bad += 1
    print(f"   z={z:5.1f}   y=-{P.BOLT_Y:<4.1f} {row[0]:7.2f}      y=+{P.BOLT_Y:<4.1f} {row[1]:7.2f}")

print("\n   just outside the slot ends (should be blocked -- the bolt stops there):")
for z in (P.SLOT_BOTTOM - 4, P.SLOT_TOP + 4):
    print(f"   z={z:5.1f}   {blocked(post, bolt(5, 0, z)):7.2f}")

print("\nBracket bolt holes\n")
for z in (10.0, 10.0 + P.BOLT_PITCH):
    for sy in (-1, 1):
        v = blocked(brac, bolt(4, sy * P.BOLT_Y, z))
        if v > 1.0:
            bad += 1
        print(f"   z={z:5.1f}   y={sy*P.BOLT_Y:+5.1f}   {v:7.2f}")

# Do the bracket holes line up with the slot over the whole travel?
print("\nAdjustment range\n")
OFFSET   = 46.0 - 6.0 - 10.0        # shelf underside above the lower bolt
lo_shelf = LO + OFFSET
hi_shelf = HI - P.BOLT_PITCH + OFFSET
print(f"   shelf underside can sit from {lo_shelf:.0f} to {hi_shelf:.0f} mm above the board")
print(f"   ({hi_shelf - lo_shelf:.0f} mm of travel, bolts {P.BOLT_PITCH:.0f} mm apart "
      f"in a {P.SLOT_TOP - P.SLOT_BOTTOM:.0f} mm slot)")

nail_top = 4.0 + P.MOTOR_BODY_H + 5.0 + 1.5 + 17.0 + P.DISC_THICK + P.NAIL_HEIGHT
print(f"   nail heads at {nail_top:.1f} mm, so the shelf must sit above that")
print(f"   usable band: {max(lo_shelf, nail_top + 3):.0f} to {hi_shelf:.0f} mm")

print("\n" + ("ALL CLEAR" if bad == 0 else f"{bad} BLOCKED BOLT PATHS -- fix before printing"))
