#!/usr/bin/env python3
"""
String Art CNC -- printed parts generator.

Produces STLs for:
    motor_stand.stl    28BYJ-48 upright stand, shaft up
    disc_hub.stl       D-shaft hub the nail disc bolts to
    servo_post.stl     base + column for the SG90, height-adjustable
    servo_bracket.stl  slides on the column, holds the SG90 shaft-down
    servo_arm.stl      arm carrying the thread tube

EVERY DIMENSION IS A CONSTANT BELOW. Measure your own parts and edit -- the
28BYJ-48 and SG90 are cloned by dozens of factories and vary by a few tenths.
Run:  python3 parts.py
"""

import numpy as np
import trimesh
from shapely.geometry import Polygon, Point, box as sbox
from shapely.ops import unary_union

# ============================================================== parameters ===

# ---- 28BYJ-48 stepper (measure yours) ----
MOTOR_BODY_D      = 28.5   # body diameter + a little clearance
MOTOR_BODY_H      = 19.0   # body height, bracket face to bottom
MOTOR_EAR_SPACING = 35.0   # centre-to-centre of the two bracket holes
MOTOR_SHAFT_D     = 5.05   # shaft diameter (nominal 5.0)
MOTOR_SHAFT_FLATS = 3.10   # across the two flats (nominal 3.0)
MOTOR_SHAFT_USE   = 9.0    # usable shaft length above the boss

# ---- SG90 servo (measure yours) ----
SG90_BODY_L   = 23.2   # body length  (nominal 22.8 + clearance)
SG90_BODY_W   = 12.6   # body width   (nominal 12.2 + clearance)
SG90_TAB_HOLE = 27.8   # centre-to-centre of the mounting tabs
SG90_TAB_D    = 2.3    # tab screw clearance
SG90_SPLINE_D = 4.9    # output spline; press fit, PLA takes the teeth

# ---- fasteners ----
M3_CLEAR   = 3.4       # through-hole for an M3 screw
M3_TAP     = 2.5       # self-tapping pilot for M3 into plastic
M2_TAP     = 1.7

# ---- thread tube ----
TUBE_D     = 2.1       # outside diameter of your brass/steel guide tube

# ---- your machine (THESE ARE GUESSES -- measure and change) ----
NAIL_RING_R   = 200.0  # radius of the nail circle, mm
NAIL_HEIGHT   = 12.0   # how far the nails stand above the disc face
DISC_THICK    = 6.0    # thickness of the nail disc

SEG = 96               # cylinder facets

# ================================================================= helpers ===

def box(x, y, z, at=(0, 0, 0)):
    m = trimesh.creation.box(extents=(x, y, z))
    m.apply_translation(at)
    return m

def cyl(r, h, at=(0, 0, 0), axis="z"):
    m = trimesh.creation.cylinder(radius=r, height=h, sections=SEG)
    if axis == "x":
        m.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [0, 1, 0]))
    elif axis == "y":
        m.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [1, 0, 0]))
    m.apply_translation(at)
    return m

def slot2d(length, width, centre=(0, 0), vertical=False):
    """Rounded-end slot as a 2D polygon, for extruding."""
    r = width / 2.0
    L = max(0.0, length - width)
    cx, cy = centre
    if vertical:
        body = sbox(cx - r, cy - L / 2, cx + r, cy + L / 2)
        caps = [Point(cx, cy - L / 2).buffer(r, 64), Point(cx, cy + L / 2).buffer(r, 64)]
    else:
        body = sbox(cx - L / 2, cy - r, cx + L / 2, cy + r)
        caps = [Point(cx - L / 2, cy).buffer(r, 64), Point(cx + L / 2, cy).buffer(r, 64)]
    return unary_union([body] + caps)

def vslot_x(width, length, centre, depth):
    """Vertical slot: long axis up Z, bored through a wall along X.

    The obvious way to build this -- a 2D slot extruded and then rotated -- is
    easy to get wrong by one axis, which is exactly how the first version of the
    column ended up with a blind groove instead of a through-slot. Built from
    primitives on the axis it actually has to run along, there is nothing to
    mix up.
    """
    r = width / 2.0
    run = max(0.0, length - width)
    body = box(depth, width, run, centre)
    for sz in (-1, 1):
        body = body.union(cyl(r, depth,
                              (centre[0], centre[1], centre[2] + sz * run / 2),
                              axis="x"))
    return body

# Where the bracket bolts to the column. One place, used by both parts, so they
# cannot drift apart.
BOLT_Y      = 9.0    # two bolt lines either side of centre, resists twisting
BOLT_PITCH  = 18.0   # vertical spacing of the bracket's two bolt rows
# Slot ends, measured up the column from the board. The bottom has to clear the
# gusset, which fills the inside corner up to about z=32 -- a slot that starts
# lower looks open from the front but a bolt runs straight into the ramp.
SLOT_BOTTOM = 36.0
SLOT_TOP    = 94.0

def extrude(poly, height, at=(0, 0, 0)):
    m = trimesh.creation.extrude_polygon(poly, height)
    m.apply_translation(at)
    return m

def check(mesh, name):
    if not mesh.is_watertight:
        mesh.fill_holes()
    mesh.remove_unreferenced_vertices()
    e = mesh.bounds[1] - mesh.bounds[0]
    print(f"  {name:18s} {e[0]:6.1f} x {e[1]:6.1f} x {e[2]:6.1f} mm"
          f"   {mesh.volume/1000:6.1f} cm3   watertight={mesh.is_watertight}")
    return mesh

# ============================================================ 1 motor stand ==
# Two posts the motor's bracket bolts down onto, on a plate that screws to the
# board. The body hangs between the posts. Print as modelled: plate on the bed,
# posts upward. No overhangs, no support.

def motor_stand():
    BASE_L, BASE_W, BASE_T = 66.0, 42.0, 4.0
    COLLAR_H = MOTOR_BODY_H + 5.0        # 5 mm under the motor for the wires
    r_in  = MOTOR_BODY_D / 2 + 0.6       # slip fit on the body
    # Outer radius has to carry the bracket screws with meat on both sides.
    r_out = MOTOR_EAR_SPACING / 2 + M3_TAP / 2 + 2.0

    part = box(BASE_L, BASE_W, BASE_T, (0, 0, BASE_T / 2))
    # A continuous collar rather than two separate posts: the screw bosses are
    # then braced by the whole ring instead of standing on 1.4 mm of wall, which
    # is all the motor body leaves if you try to post them individually.
    collar = cyl(r_out, COLLAR_H, (0, 0, BASE_T + COLLAR_H / 2))
    collar = collar.difference(cyl(r_in, COLLAR_H + 2, (0, 0, BASE_T + COLLAR_H / 2)))
    part = part.union(collar)

    # bracket screws: self-tapping, blind, from the top of the collar
    depth = COLLAR_H - 3.0
    for sx in (-1, 1):
        part = part.difference(cyl(M3_TAP / 2, depth + 1,
                                   (sx * MOTOR_EAR_SPACING / 2, 0,
                                    BASE_T + COLLAR_H - depth / 2 + 0.5)))

    # wire exit: a full-height gap in the collar, aligned with the plate notch
    part = part.difference(box(14, r_out + 4, COLLAR_H + 2,
                               (0, (r_out + 4) / 2 + r_in - 2, BASE_T + COLLAR_H / 2)))
    part = part.difference(box(14, 10, BASE_T + 2, (0, BASE_W / 2, BASE_T / 2)))

    # centre opening, for wire routing and less plastic
    part = part.difference(cyl(r_in - 4, BASE_T + 2, (0, 0, BASE_T / 2)))

    # fixing the stand to the board
    for sx in (-1, 1):
        for sy in (-1, 1):
            part = part.difference(cyl(M3_CLEAR / 2, BASE_T + 2,
                                       (sx * (BASE_L / 2 - 6), sy * (BASE_W / 2 - 6),
                                        BASE_T / 2)))
    return check(part, "motor_stand")

# =============================================================== 2 disc hub ==
# Flange on the bed, boss upward, D-bore blind from the top of the print. In use
# it is inverted: boss down over the shaft, disc bolted to the flange on top.

def disc_hub():
    FLANGE_D, FLANGE_T = 44.0, 4.0
    BOSS_D,   BOSS_H   = 22.0, 13.0
    BOLT_CIRCLE        = 32.0
    bore_depth = MOTOR_SHAFT_USE + 0.5

    part = cyl(FLANGE_D / 2, FLANGE_T, (0, 0, FLANGE_T / 2))
    part = part.union(cyl(BOSS_D / 2, BOSS_H, (0, 0, FLANGE_T + BOSS_H / 2)))

    top = FLANGE_T + BOSS_H
    # D-bore: a cylinder clipped to the shaft's across-flats width
    bore = cyl(MOTOR_SHAFT_D / 2, bore_depth + 1, (0, 0, top - bore_depth / 2 + 0.5))
    bore = bore.intersection(box(MOTOR_SHAFT_D + 2, MOTOR_SHAFT_FLATS, bore_depth + 2,
                                 (0, 0, top - bore_depth / 2 + 0.5)))
    part = part.difference(bore)

    # grub screw through the boss onto a flat, to stop the hub creeping
    part = part.difference(cyl(M3_TAP / 2, BOSS_D + 2, (0, 0, FLANGE_T + BOSS_H / 2), axis="y"))

    # disc bolts
    for i in range(4):
        a = np.pi / 4 + i * np.pi / 2
        part = part.difference(cyl(M3_CLEAR / 2, FLANGE_T + 2,
                                   (BOLT_CIRCLE / 2 * np.cos(a),
                                    BOLT_CIRCLE / 2 * np.sin(a), FLANGE_T / 2)))
    return check(part, "disc_hub")

# ============================================================= 3 servo post ==
# L of base + column, modelled as one 2D profile extruded sideways, so it prints
# flat on its side with no overhang anywhere. Slots in the base let you slide the
# whole stand radially; the slot up the column sets the height.

def servo_post():
    BASE_L, BASE_T = 58.0, 6.0
    COL_H, COL_T   = 100.0, 10.0
    WIDTH          = 26.0
    GUSSET         = 26.0

    profile = Polygon([
        (0, 0), (BASE_L, 0), (BASE_L, BASE_T),
        (COL_T + GUSSET, BASE_T),          # gusset ramp up to the column
        (COL_T, BASE_T + GUSSET),
        (COL_T, COL_H), (0, COL_H),
    ])
    part = extrude(profile, WIDTH)         # x=along base, y=up, z=width
    part.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [1, 0, 0]))
    part.apply_translation((0, WIDTH / 2, 0))   # now x=base run, y=width, z=up

    # radial adjustment slots in the base foot
    for sy in (-1, 1):
        s = slot2d(20.0, M3_CLEAR, centre=(BASE_L - 18.0, sy * 8.0))
        part = part.difference(extrude(s, BASE_T + 2, (0, 0, -1)))

    # Height adjustment: two through-slots up the column, bored along X so an
    # M3 passes clean through the 10 mm wall. Two of them rather than one keeps
    # the bracket from twisting under the arm's side load.
    mid = (SLOT_BOTTOM + SLOT_TOP) / 2
    length = SLOT_TOP - SLOT_BOTTOM
    for sy in (-1, 1):
        part = part.difference(
            vslot_x(M3_CLEAR, length, (COL_T / 2, sy * BOLT_Y, mid), COL_T + 6))
    return check(part, "servo_post")

# ========================================================== 4 servo bracket ==
# Another extruded L: a face that bolts to the column slot, and a shelf the
# servo drops into shaft-down. Print on its side, flat face to the bed.

def servo_bracket():
    FACE_H, FACE_T   = 46.0, 8.0
    SHELF_L, SHELF_T = 46.0, 6.0
    GUSSET           = 10.0
    WIDTH            = 30.0
    POCKET_X         = FACE_T + 12.0     # clear of the gusset ramp

    # Shelf at the TOP of the face, not the bottom: the face then hangs down
    # the column instead of sticking up past the top of it, and the bolts land
    # well inside the slot at any working height.
    profile = Polygon([
        (0, 0), (FACE_T, 0),
        (FACE_T, FACE_H - SHELF_T - GUSSET),
        (FACE_T + GUSSET, FACE_H - SHELF_T),
        (FACE_T + SHELF_L, FACE_H - SHELF_T),
        (FACE_T + SHELF_L, FACE_H),
        (0, FACE_H),
    ])
    part = extrude(profile, WIDTH)
    part.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [1, 0, 0]))
    part.apply_translation((0, WIDTH / 2, 0))

    # four bolts into the column slots
    for sy in (-1, 1):
        for z in (10.0, 10.0 + BOLT_PITCH):
            part = part.difference(cyl(M3_CLEAR / 2, FACE_T + 4,
                                       (FACE_T / 2, sy * BOLT_Y, z), axis="x"))

    # servo pocket, shaft pointing down through the shelf
    cx = POCKET_X + SG90_BODY_L / 2
    zc = FACE_H - SHELF_T / 2
    part = part.difference(box(SG90_BODY_L, SG90_BODY_W, SHELF_T + 4, (cx, 0, zc)))
    for sx in (-1, 1):
        part = part.difference(cyl(SG90_TAB_D / 2, SHELF_T + 4,
                                   (cx + sx * SG90_TAB_HOLE / 2, 0, zc)))
    return check(part, "servo_bracket")

SHELF_UNDERSIDE_IN_PART = 46.0 - 6.0   # FACE_H - SHELF_T, for the assembly view

# ============================================================== 5 servo arm ==
# Press-fits onto the SG90 spline. The tube clamp runs on a slot so the sweep
# radius can be trimmed without reprinting.

def servo_arm():
    HUB_D, HUB_H = 13.0, 7.0
    ARM_L, ARM_W, ARM_T = 52.0, 11.0, 5.0
    CLAMP_D, CLAMP_H = 9.0, 12.0

    part = cyl(HUB_D / 2, HUB_H, (0, 0, HUB_H / 2))
    part = part.union(box(ARM_L, ARM_W, ARM_T, (ARM_L / 2, 0, ARM_T / 2)))
    part = part.union(cyl(CLAMP_D / 2, CLAMP_H, (ARM_L - 7.0, 0, CLAMP_H / 2)))

    # spline bore + retaining screw
    part = part.difference(cyl(SG90_SPLINE_D / 2, HUB_H + 2, (0, 0, HUB_H / 2 + 1)))
    # tube bore, vertical, with a grub screw to pinch it
    part = part.difference(cyl(TUBE_D / 2, CLAMP_H + 2, (ARM_L - 7.0, 0, CLAMP_H / 2)))
    part = part.difference(cyl(M2_TAP / 2, CLAMP_D + 2,
                               (ARM_L - 7.0, 0, CLAMP_H - 3.0), axis="y"))
    # lightening slot, also lets you slide a second tube clamp along
    part = part.difference(extrude(slot2d(22.0, 4.0, centre=(ARM_L / 2 - 2, 0)),
                                   ARM_T + 2, (0, 0, -1)))
    return check(part, "servo_arm")

# ===================================================================== main ==

if __name__ == "__main__":
    import os
    out = os.environ.get("OUT", ".")
    print("Generating parts (all dimensions mm)\n")
    for fn in (motor_stand, disc_hub, servo_post, servo_bracket, servo_arm):
        mesh = fn()
        mesh.export(os.path.join(out, fn.__name__ + ".stl"))
    print(f"\nWritten to {os.path.abspath(out)}")
    # ---- stack-up, so you can check the servo can actually reach ----
    plate, collar = 4.0, MOTOR_BODY_H + 5.0
    motor_top = plate + collar                 # bracket face = top of collar
    hub_top   = motor_top + 1.5 + 13.0 + 4.0   # motor boss + hub boss + flange
    disc_top  = hub_top + DISC_THICK
    nail_top  = disc_top + NAIL_HEIGHT
    # bracket shelf can sit anywhere along the column slot
    lo = plate + 8.0
    hi = 100.0 - 20.0
    print("\nStack-up from the board surface (mm):")
    for label, z in (("motor bracket face", motor_top), ("hub flange top", hub_top),
                     ("disc top face", disc_top), ("nail heads", nail_top)):
        print(f"    {label:22s} {z:6.1f}")
    print(f"    servo shelf range      {lo:6.1f} - {hi:.1f}")
    want = disc_top + NAIL_HEIGHT * 0.5
    ok = lo <= want <= hi
    print(f"\n  arm wants to sweep at about {want:.1f} mm (mid-shank): "
          f"{'IN RANGE' if ok else 'OUT OF RANGE -- raise COL_H'}")
    print(f"\nThese use NAIL_RING_R={NAIL_RING_R}, NAIL_HEIGHT={NAIL_HEIGHT}, "
          f"DISC_THICK={DISC_THICK} -- all guesses. Measure and re-run.")
