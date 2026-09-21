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
Assembly view of the String Art CNC.

Loads the real STLs and places them at their fitted heights, with stand-in
geometry for the bought parts (motor, servo, disc, nails, tube). Every position
comes from the same constants as parts.py, so if you change a dimension there
and re-run both, this view follows.
"""
import os, numpy as np, trimesh, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection, Line3DCollection

import parts as P
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# ---- where everything ends up, measured from the board surface -------------
BOARD_Z      = 0.0
PLATE_T      = 4.0
COLLAR_H     = P.MOTOR_BODY_H + 5.0
MOTOR_TOP    = PLATE_T + COLLAR_H            # bracket face
BOSS_TOP     = MOTOR_TOP + 1.5
HUB_BOTTOM   = BOSS_TOP
HUB_TOP      = HUB_BOTTOM + 17.0
DISC_TOP     = HUB_TOP + P.DISC_THICK
NAIL_TOP     = DISC_TOP + P.NAIL_HEIGHT

RING_R       = P.NAIL_RING_R
DISC_R       = RING_R + 12.0
COLUMN_X     = RING_R + 16.0                 # column stands clear of the disc
SHELF_Z      = NAIL_TOP + 5.5                # shelf underside clears nail heads
PIVOT_X      = COLUMN_X - 30.0               # servo axis, inside the nail ring
ARM_L        = 45.0                          # pivot to tube centre
TUBE_BOTTOM  = DISC_TOP + P.NAIL_HEIGHT * 0.45   # tip at mid shank

C_PRINT  = "#7f9fc4"
C_MOTOR  = "#4a4a4a"
C_DISC   = "#c8a878"
C_NAIL   = "#b9bcc0"
C_SERVO  = "#6f7479"
C_TUBE   = "#d08030"
C_THREAD = "#8a3a2a"

def load(name):
    return trimesh.load(f"stl/{name}.stl")

def rot(m, ang, axis, point=(0, 0, 0)):
    m = m.copy()
    m.apply_transform(trimesh.transformations.rotation_matrix(ang, axis, point))
    return m

def at(m, xyz):
    m = m.copy(); m.apply_translation(xyz); return m

def cyl(r, h, centre, sections=28):
    c = trimesh.creation.cylinder(radius=r, height=h, sections=sections)
    c.apply_translation(centre); return c

def box(x, y, z, centre):
    b = trimesh.creation.box(extents=(x, y, z)); b.apply_translation(centre); return b

# ---- assemble ---------------------------------------------------------------
def build():
    items = []

    items.append((at(load("motor_stand"), (0, 0, 0)), C_PRINT, "motor_stand"))

    # 28BYJ-48 stand-in: body, bracket plate, boss, D-shaft
    items.append((cyl(P.MOTOR_BODY_D / 2 - 0.3, P.MOTOR_BODY_H,
                      (0, 0, MOTOR_TOP - P.MOTOR_BODY_H / 2)), C_MOTOR, "28BYJ-48"))
    items.append((box(42, 7, 1.0, (0, 0, MOTOR_TOP + 0.5)), C_MOTOR, None))
    items.append((cyl(4.5, 1.5, (0, 0, MOTOR_TOP + 0.75)), C_MOTOR, None))
    items.append((cyl(2.5, 9.0, (0, 0, BOSS_TOP + 4.5)), C_MOTOR, None))

    # hub: modelled flange-down, so it is flipped in use -- boss down on the shaft
    hub = rot(load("disc_hub"), np.pi, [1, 0, 0])
    hub = at(hub, (0, 0, HUB_TOP))
    items.append((hub, C_PRINT, "disc_hub"))

    # nail disc -- kept as a half for the cutaway views so the motor shows
    disc = cyl(DISC_R, P.DISC_THICK, (0, 0, HUB_TOP + P.DISC_THICK / 2), 96)
    half = disc.intersection(box(DISC_R * 3, DISC_R, P.DISC_THICK * 3,
                                 (0, -DISC_R / 2, HUB_TOP + P.DISC_THICK / 2)))
    items.append((disc, C_DISC, "disc"))
    items.append((half, C_DISC, "disc_half"))

    # servo post and bracket
    items.append((at(load("servo_post"), (COLUMN_X, 0, 0)), C_PRINT, "servo_post"))
    br = rot(load("servo_bracket"), np.pi, [0, 0, 1])
    # the bracket's shelf underside sits 40 mm up its own face
    br = at(br, (COLUMN_X, 0, SHELF_Z - 40.0))
    items.append((br, C_PRINT, "servo_bracket"))

    # SG90 sitting in the shelf, shaft down
    items.append((box(P.SG90_BODY_L, P.SG90_BODY_W, 22.7,
                      (PIVOT_X + 5.5, 0, SHELF_Z + 6 + 11.3)), C_SERVO, "SG90"))
    # the two bolts, drawn so the joint is visible
    for sy in (-1, 1):
        for dz in (0.0, 18.0):
            items.append((cyl(1.6, 26, (COLUMN_X + 4, sy * 9.0,
                                        SHELF_Z - 30.0 + dz), 12), "#444", None))

    # arm hanging under the shelf, tube pointing down between the nails
    arm = rot(load("servo_arm"), np.pi, [1, 0, 0])
    arm = rot(arm, np.pi, [0, 0, 1])
    arm = at(arm, (PIVOT_X, 0, SHELF_Z - 1.0))
    items.append((arm, C_PRINT, "servo_arm"))
    tube_top = SHELF_Z - 1.0
    items.append((cyl(P.TUBE_D / 2 + 0.3, tube_top - TUBE_BOTTOM,
                      (PIVOT_X - ARM_L, 0, (tube_top + TUBE_BOTTOM) / 2), 20),
                  C_TUBE, "thread tube"))
    return items

def nails(n=120):
    segs = []
    for i in range(n):
        a = 2 * np.pi * i / n
        x, y = RING_R * np.cos(a), RING_R * np.sin(a)
        segs.append([(x, y, DISC_TOP), (x, y, NAIL_TOP)])
    return segs

def draw(ax, items, nail_segs=True, lw=0.1):
    for mesh, colour, _ in items:
        ax.add_collection3d(Poly3DCollection(
            mesh.vertices[mesh.faces], facecolor=colour,
            edgecolor="#2b3540", linewidths=lw, alpha=1.0))
    if nail_segs:
        ax.add_collection3d(Line3DCollection(nails(), colors=C_NAIL, linewidths=1.0))

def frame(ax, cx, cy, cz, r, elev=24, azim=-58):
    ax.set_xlim(cx - r, cx + r); ax.set_ylim(cy - r, cy + r); ax.set_zlim(cz - r, cz + r)
    ax.set_box_aspect((1, 1, 1)); ax.view_init(elev=elev, azim=azim)
    ax.set_xticks([]); ax.set_yticks([]); ax.set_zticks([])
    for a in (ax.xaxis, ax.yaxis, ax.zaxis):
        a.pane.fill = False; a.line.set_color((1, 1, 1, 0))

# ---- figure -----------------------------------------------------------------
items = build()

def pick(*names):
    return [i for i in items if i[2] in names]

def drop(*names):
    return [i for i in items if i[2] not in names]

PRINTED = ("motor_stand", "disc_hub", "servo_post", "servo_bracket", "servo_arm")

fig = plt.figure(figsize=(16, 13), facecolor="white")
gs = fig.add_gridspec(3, 3, height_ratios=[1.35, 1.35, 1.45], hspace=0.14, wspace=0.05)

# --- side elevation, the most useful single view ---------------------------
ax = fig.add_subplot(gs[0, :])
for mesh, colour, name in drop("disc_half"):
    tri = mesh.vertices[mesh.faces][:, :, [0, 2]]
    ax.add_collection(matplotlib.collections.PolyCollection(
        tri, facecolor=colour, edgecolor="none", alpha=0.97))
for i in range(0, 200):
    x = RING_R * np.cos(2 * np.pi * i / 200)
    ax.plot([x, x], [DISC_TOP, NAIL_TOP], color=C_NAIL, lw=0.7, zorder=0)
ax.plot([-DISC_R - 40, COLUMN_X + 80], [0, 0], color="#333", lw=2.5)
ax.text(-DISC_R - 36, -9, "board", fontsize=9, color="#333")

# Fan the callouts out vertically -- the real heights are only 42 mm apart
# over the whole stack, so stacked labels collide at any readable font size.
levels = [(MOTOR_TOP, "motor bracket"), (HUB_TOP, "hub flange"),
          (DISC_TOP, "disc face"), (NAIL_TOP, "nail heads"),
          (SHELF_Z, "servo shelf")]
x_line, x_text = COLUMN_X + 70, COLUMN_X + 120
spread = np.linspace(-34, NAIL_TOP + 62, len(levels))
for (z, label), ty in zip(levels, spread):
    ax.plot([-DISC_R - 20, x_line], [z, z], color="#b0b8c0", lw=0.6,
            ls=(0, (4, 4)), zorder=0)
    ax.plot([x_line, x_text - 6], [z, ty], color="#b0b8c0", lw=0.6, zorder=0)
    ax.text(x_text, ty, f"{label}   {z:.0f} mm", fontsize=10,
            va="center", color="#333")

notes = [((0, 14), (-168, -40), "motor_stand"),
         ((0, HUB_TOP - 8), (-252, 34), "disc_hub"),
         ((PIVOT_X - ARM_L, TUBE_BOTTOM + 4), (40, 126), "thread tube"),
         ((PIVOT_X, SHELF_Z + 16), (152, 136), "SG90, shaft down"),
         ((COLUMN_X + 5, 40), (COLUMN_X + 30, -40), "servo_post")]
for (x, y), (tx, ty), txt in notes:
    ax.annotate(txt, xy=(x, y), xytext=(tx, ty), fontsize=9.5, color="#1c3f6e",
                ha="center",
                arrowprops=dict(arrowstyle="-", color="#5f7fa8", lw=0.9,
                                shrinkA=0, shrinkB=3))
ax.set_xlim(-DISC_R - 60, COLUMN_X + 300)
ax.set_ylim(-52, NAIL_TOP + 80)
ax.set_aspect("equal"); ax.axis("off")
ax.set_title("Side elevation \u2014 heights in mm above the board", fontsize=12.5)

# --- three 3D views --------------------------------------------------------
def draw3d(ax, sel, nails_on, lw, cx, cy, cz, r, elev, azim, title):
    for mesh, colour, _ in sel:
        ax.add_collection3d(Poly3DCollection(
            mesh.vertices[mesh.faces], facecolor=colour,
            edgecolor="#2b3540", linewidths=lw))
    if nails_on:
        segs = [s for s in nails(200) if s[0][1] < 2]
        ax.add_collection3d(Line3DCollection(segs, colors=C_NAIL, linewidths=0.9))
    frame(ax, cx, cy, cz, r, elev, azim)
    ax.set_title(title, fontsize=11, pad=-2)

ax = fig.add_subplot(gs[1, 0], projection="3d")
draw3d(ax, drop("disc"), True, 0.05, 0, -30, DISC_TOP / 2 + 10, DISC_R * 1.02,
       26, -62, "Cutaway \u2014 half the disc removed")

ax = fig.add_subplot(gs[1, 1], projection="3d")
draw3d(ax, pick("motor_stand", "28BYJ-48", "disc_hub", None), False, 0.2,
       0, 0, 26, 40, 16, -64, "Motor and hub")

ax = fig.add_subplot(gs[1, 2], projection="3d")
draw3d(ax, pick("servo_post", "servo_bracket", "SG90", "servo_arm",
                "thread tube", "disc_half"), True, 0.2,
       PIVOT_X - 10, 0, SHELF_Z - 10, 68, 10, -78, "Wrap head")

# --- plan view of the wrap, showing the sweep ------------------------------
ax = fig.add_subplot(gs[2, :])
ring = plt.Circle((0, 0), RING_R, fill=False, color=C_NAIL, lw=1.2)
ax.add_patch(ring)
for i in range(360):
    a = 2 * np.pi * i / 360
    ax.plot(RING_R * np.cos(a), RING_R * np.sin(a), ".", color=C_NAIL, ms=0.8)
ax.add_patch(plt.Circle((0, 0), DISC_R, fill=True, color=C_DISC, alpha=0.18, lw=0))
sweep = plt.Circle((PIVOT_X, 0), ARM_L, fill=False, color="#1d9e75", lw=1.6, ls="--")
ax.add_patch(sweep)
ax.plot([PIVOT_X], [0], "s", color="#333", ms=6)
ax.annotate("servo pivot, inside the ring", xy=(PIVOT_X, 0), xytext=(PIVOT_X - 40, -86),
            fontsize=9.5, ha="center", color="#1c3f6e",
            arrowprops=dict(arrowstyle="-", color="#5f7fa8", lw=0.9))
for ang, lab in ((150, "tube inside"), (10, "tube outside")):
    a = np.radians(ang)
    tx, ty = PIVOT_X + ARM_L * np.cos(a), ARM_L * np.sin(a)
    ax.plot([PIVOT_X, tx], [0, ty], color="#1d9e75", lw=1.4)
    ax.plot(tx, ty, "o", color=C_TUBE, ms=7)
    ax.annotate(lab, xy=(tx, ty), xytext=(tx + 14, ty + 26), fontsize=9.5,
                color="#1c3f6e", ha="center",
                arrowprops=dict(arrowstyle="-", color="#5f7fa8", lw=0.9))
ax.set_xlim(-DISC_R - 20, PIVOT_X + ARM_L + 120); ax.set_ylim(-DISC_R * 0.78, DISC_R * 0.78)
ax.set_aspect("equal"); ax.axis("off")
ax.set_title("Plan \u2014 the arm sweeps the tube across the nail circle "
             f"(pivot R{PIVOT_X:.0f}, arm {ARM_L:.0f}, ring R{RING_R:.0f})", fontsize=12.5)

plt.savefig("stl/assembly.png", dpi=105, bbox_inches="tight", facecolor="white")
print("assembly.png written")
print(f"  tube sweeps R {abs(PIVOT_X-ARM_L):.0f} to {PIVOT_X+ARM_L:.0f}, ring at {RING_R:.0f} "
      f"-> crosses: {'YES' if abs(PIVOT_X-ARM_L) < RING_R < PIVOT_X+ARM_L else 'NO'}")
print(f"  shelf underside {SHELF_Z:.1f} vs nail heads {NAIL_TOP:.1f} "
      f"-> {SHELF_Z-NAIL_TOP:.1f} mm clearance")
