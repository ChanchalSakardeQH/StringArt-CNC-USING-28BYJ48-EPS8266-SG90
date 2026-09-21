# Printed parts — String Art CNC

Five parts, all watertight and all designed so nothing needs support.
`parts.py` regenerates every STL; every dimension is a named constant at the top.

```bash
pip install trimesh manifold3d shapely numpy matplotlib
python3 parts.py      # writes the five STLs into stl/
python3 fitcheck.py   # proves every bolt path is clear
python3 assembly.py   # renders stl/assembly.png
```

---

## Read this before printing

Three numbers in `parts.py` are **guesses**, because I don't know your machine:

```python
NAIL_RING_R = 200.0   # radius of the nail circle
NAIL_HEIGHT = 12.0    # how far the nails stand above the disc
DISC_THICK  = 6.0     # thickness of the nail disc
```

They don't change any part's geometry, but they drive the stack-up check the
script prints at the end — which tells you whether the servo column is tall
enough to put the arm at nail-shank height. Measure yours and re-run before
printing `servo_post`.

The 28BYJ-48 and SG90 are cloned by dozens of factories and vary by a few
tenths of a millimetre. The constants are nominal-plus-clearance. **Print the
disc hub first** as a fit test — it's 10 cm³ and takes about 40 minutes. If it
won't go on the shaft, raise `MOTOR_SHAFT_D`/`MOTOR_SHAFT_FLATS` by 0.1 and
reprint before committing to the rest.

---

## Parts

| File | Size | What it does |
|---|---|---|
| `motor_stand.stl` | 66 × 42 × 28 | Holds the 28BYJ-48 upright, shaft up, on the board |
| `disc_hub.stl` | 44 × 44 × 17 | D-shaft hub the nail disc bolts to |
| `servo_post.stl` | 58 × 26 × 100 | Base and column, slotted for radial and height adjustment |
| `servo_bracket.stl` | 54 × 30 × 46 | Slides on the column, holds the SG90 shaft-down |
| `servo_arm.stl` | 58 × 13 × 12 | Carries the thread tube, sweep radius adjustable |

### motor_stand

A continuous collar rather than two separate posts. The obvious design — a post
under each bracket ear — leaves only 1.4 mm of wall between each screw hole and
the motor body cutout, because the ear holes sit at ±17.5 mm and the body is
28 mm across. A self-tapper splits that. The collar braces both bosses all the
way round instead.

Motor drops in from the top, bracket rests on the collar, two M3 × 8 self-tappers
down into the bosses. The notch takes the wire connector. 5 mm of clearance under
the motor for the cable.

### disc_hub

Flange on the bed, boss upward — **in use it's inverted**: boss down over the
shaft, disc bolted to the flange on top. The blind bore gives the shaft a depth
stop. There's an M3 grub screw through the boss onto one of the shaft flats;
fit it, or the hub will creep under the wrap cycle's back-and-forth loading.

The four flange holes are on a 32 mm circle for M3.

### servo_post and servo_bracket

Both are L-shapes extruded sideways, so they print lying on their side with
zero overhang.

The column carries **two vertical through-slots**, 36 to 94 mm up from the
board, and the bracket has **four matching holes** — two rows of two, 18 mm
apart. Four bolts rather than two because the arm cantilevers well past the
nail ring and a single pair would let the bracket twist under the side load of
dragging thread around a nail.

**Shelf underside adjusts from 68 to 104 mm above the board**, which covers the
nail heads at 64.5 mm with room to spare. Bolt through with M3 × 25, nuts and
washers on the back of the column. Slacken all four to slide the height.

The base slots let you move the whole stand toward or away from the nail ring.

Run `fitcheck.py` after changing anything here. It pushes a virtual M3 through
every bolt position and reports how much material is in the way — zero means
clear. Two bugs it caught that were invisible in the render: a slot cut through
the wrong axis, so it was a blind groove rather than a through-slot, and a slot
whose lower end ran into the gusset.

Position matters more than anything else here: **the servo's pivot must sit
inside the nail ring**, with the arm long enough that the tube tip swings past
the ring and back. That's the geometry the wrap cycle depends on.

### servo_arm

The 4.9 mm bore is a press fit onto the SG90 spline — PLA takes the teeth if you
push it on warm. Bore it out and glue in the supplied horn if you'd rather.

Set the tube's sweep so the tip crosses the nail circle cleanly at shank height,
below the nail heads. `TUBE_D = 2.1` assumes a 2 mm brass tube; change it to
match whatever you use.

---

## Print settings

Nothing here is fussy. PLA is fine; PETG if the machine will sit in sun or heat.

| | |
|---|---|
| Layer height | 0.2 mm |
| Perimeters | 3 |
| Infill | 30% (40% for `servo_arm`) |
| Support | none needed on any part |
| Orientation | all parts as modelled, largest face down |

Total is about 115 cm³ — roughly 145 g, six to eight hours across all five.

`servo_arm` is the one part worth more perimeters. It's long and thin and takes
the whole side-load of dragging thread around a nail.

---

## What it looks like assembled

`assembly_guide.png` is rendered from the actual STLs at their fitted heights —
not an illustration, so what you see is what will print. `assembly.py`
regenerates it and re-checks the geometry after any change to `parts.py`.

The numbers it prints are the ones that matter:

```
tube sweeps R 141 to 231, ring at 200 -> crosses: YES
shelf underside 70.0 vs nail heads 64.5 -> 5.5 mm clearance
```

**"crosses: YES"** is the whole machine working or not. The tube's swept circle
has to straddle the nail circle, or no wrap can happen. It depends on three
numbers: pivot radius, arm length, nail ring radius. Change any of them and
re-run `assembly.py` before you print.

**Shelf clearance** is the tight one. The bracket shelf spans over the nails to
reach a pivot inside the ring, so its underside must clear the nail heads —
5.5 mm at my guessed 12 mm nail height. Taller nails eat it fast. If you end up
under about 3 mm, raise the bracket on the column slot and lengthen the tube to
suit, rather than trimming the nails.

---

## Assembly order

1. Motor into `motor_stand`, two M3 × 8 self-tappers through the bracket.
2. `disc_hub` onto the shaft, grub screw onto a flat. Check it runs true before
   the disc goes on — any wobble here is multiplied at the nail ring.
3. Disc onto the hub flange, four M3. **Home the machine and mark nail 0 now**,
   while you can still see the hub.
4. `servo_post` screwed to the board so the pivot falls inside the nail ring.
5. `servo_bracket` onto the column, height set so the arm sweeps at mid-shank.
6. `servo_arm` on the spline, tube clamped, then run **Test one wrap** from the
   web UI and adjust before tightening anything permanently.

Step 6 is where the real fitting happens. Expect to move the post radially a few
times and reprint the arm at least once with a different length — that's normal,
and it's why the arm is the cheapest part to print.

---

## What isn't here

The nail disc itself. That's the base template from the web UI (section 4),
printed at 100% and glued to ply or MDF, then drilled. It's too big to 3D print
and better in wood anyway — it stays flatter and holds nails far better.
