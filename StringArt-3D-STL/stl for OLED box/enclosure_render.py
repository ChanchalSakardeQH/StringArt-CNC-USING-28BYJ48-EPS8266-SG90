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
"""Renders enclosure_guide.png from the real STLs in their fitted positions."""
import os, numpy as np, trimesh, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import enclosure as E
import enclosure_fitcheck as F   # reuses its placement transforms (and re-checks)

os.chdir(os.path.dirname(os.path.abspath(__file__)))
load = lambda n: trimesh.load(f"stl/{n}.stl")
C_BOX, C_OLED, C_GLASS, C_PCB, C_RET = "#8fa6c4", "#e8a33c", "#1c1c22", "#2d6fb8", "#6fbf8f"

def draw(ax, items, lw=0.08):
    # One collection for everything, so faces are depth-sorted against each
    # other rather than part-by-part -- otherwise a big box is painted over the
    # small bezel sitting in front of it.
    tris, cols = [], []
    lo, hi = np.full(3, np.inf), np.full(3, -np.inf)
    for m, c in items:
        tris.append(m.vertices[m.faces])
        cols += [c] * len(m.faces)
        lo, hi = np.minimum(lo, m.bounds[0]), np.maximum(hi, m.bounds[1])
    ax.add_collection3d(Poly3DCollection(np.concatenate(tris), facecolors=cols,
                                         edgecolor="#2b3540", linewidths=lw))
    c, r = (lo + hi) / 2, (hi - lo).max() / 2 * 1.02
    ax.set_xlim(c[0]-r, c[0]+r); ax.set_ylim(c[1]-r, c[1]+r); ax.set_zlim(c[2]-r, c[2]+r)
    ax.set_box_aspect((1, 1, 1))
    ax.set_xticks([]); ax.set_yticks([]); ax.set_zticks([])
    for a in (ax.xaxis, ax.yaxis, ax.zaxis): a.pane.fill = False; a.line.set_color((1,1,1,0))

def moved(m, M=None, t=(0, 0, 0)):
    m = m.copy()
    if M is not None: m.apply_transform(M)
    m.apply_translation(t); return m

body, lid, tray = load("enclosure_box"), load("enclosure_lid"), load("enclosure_tray")
frame, ret, stand = load("oled_frame"), load("oled_retainer"), load("oled_stand")
glass, pcb = F.oled_model()
Fb = F.frame_to_box()
Rf = F.retainer_to_frame()
lid_use = moved(lid, trimesh.transformations.reflection_matrix([0,0,0],[0,0,1]),
                (0, 0, E.BODY_H + E.LID_T))

fig = plt.figure(figsize=(15, 11.5), facecolor="white")

ax = fig.add_subplot(2, 2, 1, projection="3d")
draw(ax, [(body, C_BOX), (lid_use, C_BOX), (moved(frame, Fb), C_OLED),
          (moved(glass, Fb), C_GLASS)])
ax.view_init(elev=18, azim=-62)
ax.set_title(f"Closed: {E.BOX_L/E.IN:.1f} x {E.BOX_W/E.IN:.1f} x {E.BOX_H/E.IN:.1f} in",
             fontsize=12)

ax = fig.add_subplot(2, 2, 2, projection="3d")
draw(ax, [(body, C_BOX), (moved(lid_use, t=(0, 0, 95)), C_BOX),
          (moved(tray, t=(0, 0, E.BODY_H + 22)), C_RET),
          (moved(frame, Fb), C_OLED), (moved(ret, Fb @ Rf), C_OLED)], lw=0.04)
ax.view_init(elev=24, azim=-128)
ax.set_title("Exploded: lid and component tray lifted out", fontsize=12)

ax = fig.add_subplot(2, 2, 3, projection="3d")
ex = [(frame, C_OLED),
      (moved(glass, t=(0, 0, 9)), C_GLASS), (moved(pcb, t=(0, 0, 9)), C_PCB),
      (moved(ret, Rf, (0, 0, 20)), C_RET)]
draw(ax, ex, lw=0.15)
ax.view_init(elev=22, azim=-58)
ax.set_title("OLED stack: bezel frame, module, retainer\n(held by the board edges, not its holes)",
             fontsize=12)

ax = fig.add_subplot(2, 2, 4, projection="3d")
Ms = E.stand_panel_matrix()
Mf = np.eye(4)
Mf[:3, 0], Mf[:3, 1], Mf[:3, 2] = Ms[:3, 0], Ms[:3, 1], -Ms[:3, 2]
Mf[:3, 3] = Ms[:3, 3] + Ms[:3, 1] * E.PL_OPEN_V + Ms[:3, 2] * E.FL_T
draw(ax, [(stand, C_BOX), (moved(frame, Mf), C_OLED), (moved(glass, Mf), C_GLASS)],
     lw=0.12)
ax.view_init(elev=16, azim=-58)
ax.set_title("Optional desk stand: same frame and retainer", fontsize=12)

plt.tight_layout()
plt.savefig("enclosure_guide.png", dpi=95, bbox_inches="tight", facecolor="white")
print("enclosure_guide.png written")
