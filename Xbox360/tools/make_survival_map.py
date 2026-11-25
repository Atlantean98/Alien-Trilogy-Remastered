"""(Shared with the PC version, where it builds Survival arena 1; it began in a side project, an Xbox 360 build - see
README.md here.) Builds the Xbox 360 Survival arena: a hive cavern in the style of the Section 1 queen level (L162), a little bigger,
with door-sealed spawn rooms around its edge and the queen's chamber at the north end. The level itself is written by
trilogy_level.py (the same writer the level editor uses), with L162's textures, sounds and music (code 162).

Writes assets/survival/ (L162LEV.MAP, 162GFX.BIN, level.txt; on the console they go in GameData/SURVIVAL/) and
assets/survival/preview_map.png (a top-down plan).

Usage: python tools/make_survival_map.py
       python tools/make_survival_map.py --view x,z facing out-dir   (a copy starting elsewhere, for screenshots)
"""
import math
import os
import sys

from PIL import Image, ImageDraw

import trilogy_level

HERE = os.path.dirname(os.path.abspath(__file__))
X360 = os.path.normpath(os.path.join(HERE, ".."))
OUT = os.path.join(X360, "assets", "survival")

W, H = 72, 56                  # L162 is 63 x 46
CX, CZ = 35.5, 27.5            # arena centre (cells)
RX, RZ = 29.0, 21.5            # superellipse radii (cells)
SOLID, ARENA, DOOR, ROOM, WOMB = 0, 1, 2, 3, 4
# L162's textures (162GFX.BIN numbering) in the order its cavern walls stack them, its queen's sac for the chamber,
# and plain panels for the spawn rooms.
WALL_BANDS = [[3, 4], [326, 327, 323], [296, 297, 290], [290, 291, 292, 293], [284, 285, 286, 287], [280, 281, 282, 283]]
WOMB_BANDS = [[232, 233], [234, 235], [236, 237], [264, 265], [266, 267], [268, 269]]
ROOM_WALL, ROOM_FLOOR, ROOM_CEILING = [355, 356], 351, 352
# L162's cavern walls as it lays them: each 512-unit band runs its own 2- or 4-panel pictures in order along the wall
# (trilogy_level.surfaces on L162: 3-4 or 338-343 at the foot, then 330-335, 322-327, 292-297, 286-291 up to the roof).
WALL_SEQS = [[[3, 4], [338, 339, 342, 343]], [[330, 331, 334, 335]], [[322, 323, 326, 327]], [[292, 293, 296, 297]],
             [[286, 287, 290, 291]]]


def noise(x, z, seed=0):
    """Deterministic smooth-ish value in [-1, 1]."""
    return (math.sin(x * 0.71 + seed * 1.3) * math.cos(z * 0.53 - seed * 0.7) + math.sin((x + z) * 0.37 + seed)) / 2


# ---- Layout ------------------------------------------------------------------------------------------------------
def layout():
    grid = [[SOLID] * W for _ in range(H)]
    for z in range(2, H - 2):
        for x in range(2, W - 2):
            dx, dz = (x + 0.5 - CX) / RX, (z + 0.5 - CZ) / RZ
            if dx ** 4 + dz ** 4 <= 1 + 0.10 * noise(x, z):
                grid[z][x] = ARENA
    # Hive pillars (cover), as L162's hive masses.
    for px, pz, r in [(-17, -8, 2.4), (17, -8, 2.4), (-10, 6, 1.9), (10, 6, 1.9), (-23, 6, 1.6), (23, 6, 1.6),
                      (0, -13, 1.7), (-7, -2, 1.2), (7, -2, 1.2)]:
        for z in range(H):
            for x in range(W):
                if math.hypot(x + 0.5 - (CX + px), z + 0.5 - (CZ + pz)) <= r + 0.25 * noise(x, z, 3):
                    grid[z][x] = SOLID
    # The queen's chamber at the north end: a ring of hive (womb) open to the south, the queen inside.
    qx, qz = CX, CZ + RZ - 6.5
    for z in range(H):
        for x in range(W):
            dx, dz = x + 0.5 - qx, z + 0.5 - qz
            r = math.hypot(dx, dz)
            south = dz < -1.0 and abs(dx) < 2.6
            if r <= 2.4:
                grid[z][x] = ARENA
            elif r <= 4.6 and not south:
                grid[z][x] = WOMB
            elif r <= 6.0 and dz > 2.5 and grid[z][x] == ARENA:
                grid[z][x] = WOMB
    queen = (int(qx), int(qz))
    # Doors in the arena's edge, each sealing a 3-deep spawn room; (side, position along the wall).
    doors = []
    specs = [("west", 27), ("east", 27), ("south", 23), ("south", 47), ("north", 20), ("north", 51)]
    for side, at in specs:
        cells, room = [], []
        # From the map's edge inwards to the first line where the 4 cells reach open floor: the door goes just
        # outside it, the room beyond, and the line inside is opened up so the doorway meets the cavern.
        if side in ("west", "east"):
            step = -1 if side == "west" else 1
            x = 1 if side == "west" else W - 2
            while all(grid[z][x] == SOLID for z in range(at - 1, at + 3)):
                x -= step
            x += step
            for z in range(at - 1, at + 3):
                inward = x - step
                if grid[z][inward] != ARENA:
                    grid[z][inward] = ARENA
                grid[z][x] = DOOR
                cells.append((x, z))
                for d in range(1, 4):
                    grid[z][x + step * d] = ROOM
                    room.append((x + step * d, z))
            outward = (step, 0)
        else:
            step = -1 if side == "south" else 1
            z = 1 if side == "south" else H - 2
            while all(grid[z][x] == SOLID for x in range(at - 1, at + 3)):
                z -= step
            z += step
            for x in range(at - 1, at + 3):
                inward = z - step
                if grid[inward][x] != ARENA:
                    grid[inward][x] = ARENA
                grid[z][x] = DOOR
                cells.append((x, z))
                for d in range(1, 4):
                    grid[z + step * d][x] = ROOM
                    room.append((x, z + step * d))
            outward = (0, step)
        # Door record: the cell at offset 0 of the 4 (offsets -1..2), rotation 2 along z, 4 along x.
        if side in ("west", "east"):
            record = (cells[0][0], cells[1][1], 2)
        else:
            record = (cells[1][0], cells[0][1], 4)
        doors.append(dict(side=side, cells=cells, room=room, record=record, outward=outward))
    # Keep the rooms inside the grid border.
    for d in doors:
        for x, z in d["room"]:
            assert 1 <= x < W - 1 and 1 <= z < H - 1, d
    return grid, doors, queen


def connected(grid, start):
    seen, todo = {start}, [start]
    while todo:
        x, z = todo.pop()
        for nx, nz in ((x + 1, z), (x - 1, z), (x, z + 1), (x, z - 1)):
            if 0 <= nx < W and 0 <= nz < H and (nx, nz) not in seen and grid[nz][nx] in (ARENA, DOOR, ROOM):
                seen.add((nx, nz))
                todo.append((nx, nz))
    return seen


# The PC Survival's supply balance (decompilation/reports/l331_goop_panels_xbox360_handoff.md): ammunition and
# medikit spots over the cavern (26 and 14, as its self-test expects) and one reserve spot per weapon by the start,
# none beside the queen or her egg sac (north of her).
QUEEN = (35, 32)
AMMO_SPOTS = [(39, 18), (43, 47), (48, 42), (26, 47), (56, 18), (17, 23), (43, 12), (26, 12), (61, 13), (22, 18), (12, 29),
              (52, 23), (11, 19), (12, 35), (17, 12), (20, 31), (19, 42), (25, 19), (27, 32), (30, 25), (30, 42), (35, 12),
              (39, 7), (39, 39), (46, 19), (47, 32)]
HEALTH_SPOTS = [(10, 25), (13, 38), (17, 16), (20, 26), (22, 40), (25, 10), (27, 26), (34, 21), (37, 17), (44, 39), (48, 14),
                (52, 29), (58, 22), (59, 32)]
RESERVE_SPOTS = [(33, 23), (37, 23), (33, 25), (37, 25), (35, 27)]


def spots(cells, reach):
    return [c for c in cells if c in reach and max(abs(c[0] - QUEEN[0]), abs(c[1] - QUEEN[1])) > 3]


def level():
    """The arena as a trilogy_level description."""
    grid, doors, queen = layout()
    # 8 cells south of the queen, facing her: she shows dimly through the black fog (2040-5100 units) at the start.
    start = (QUEEN[0], QUEEN[1] - 8)
    reach = connected(grid, start)
    # The PC Survival draws the queen's original egg sac (from L162) behind her instead of the hive enclosure, so the
    # enclosure's cells are open floor and she stands further south, at (35, 32), to leave room for the membrane.
    chars = {SOLID: "#", ARENA: ".", DOOR: "#", ROOM: "R", WOMB: "."}
    queen = QUEEN
    rows = []
    for z in range(H):
        row = ""
        for x in range(W):
            t = grid[z][x]
            if t == ARENA and (x, z) not in reach:
                t = SOLID      # pockets the player could never reach
            row += chars[t]
        rows.append(row)
    door_list = []
    for d in doors:
        cells = sorted(d["cells"])
        axis = "z" if d["side"] in ("west", "east") else "x"
        door_list.append({"x": cells[0][0], "z": cells[0][1], "axis": axis})
    lv = {
        "name": "Survival", "code": "162", "width": W, "height": H, "grid": rows, "doors": door_list,
        "start": list(start), "start_dir": 4, "enemies": [{"kind": 7, "x": queen[0], "z": queen[1]}], "pickups": [],
        "mode": "survival", "lives": 5, "weapons": [0], "waves": 10, "roster": "campaign", "queen_last_wave": True,
        "brightness": "normal", "ammo_spots": spots(AMMO_SPOTS, reach), "health_spots": spots(HEALTH_SPOTS, reach),
        "reserve_spots": RESERVE_SPOTS, "wall_bands": WALL_BANDS, "hive_bands": WOMB_BANDS, "room_wall": ROOM_WALL,
        "room_floor": ROOM_FLOOR, "room_ceiling": ROOM_CEILING, "wall_seqs": WALL_SEQS,
    }
    # Explosive barrels in three small groups - off the supply spots and clear of the egg sac behind the Queen
    # (its footprint runs about x 33-41, z 30-47). Survival respawns destroyed ones every other wave.
    sac = [(x, z) for x in range(32, 43) for z in range(29, 49)]
    supplies = [tuple(c) for key in ("ammo_spots", "health_spots", "reserve_spots") for c in lv[key]]
    lv["objects"] = trilogy_level.place_barrels(lv, avoid=sac + supplies)
    # Scenery as Section 1 dresses its rooms: stacks of crates and lockers against the cavern wall.
    lv["objects"] += trilogy_level.place_scenery(lv, [25, 20], groups=5, avoid=sac + supplies)
    return lv


def preview(lv, path):
    s = 10
    img = Image.new("RGB", (W * s, H * s), (10, 10, 10))
    dr = ImageDraw.Draw(img)
    colours = {"#": (25, 22, 20), ".": (60, 90, 60), "R": (90, 90, 110), "W": (120, 60, 40), "D": (200, 170, 40)}
    grid = [list(r) for r in lv["grid"]]
    for d in lv["doors"]:
        for x, z in trilogy_level.door_cells(d):
            grid[z][x] = "D"
    for z in range(H):
        for x in range(W):
            y = (H - 1 - z) * s
            dr.rectangle((x * s, y, x * s + s - 1, y + s - 1), fill=colours[grid[z][x]])

    def mark(x, z, col):
        y = (H - 1 - z) * s
        dr.ellipse((x * s + 1, y + 1, x * s + s - 2, y + s - 2), fill=col)
    for e in lv["enemies"]:
        mark(e["x"], e["z"], (255, 40, 40))
    mark(*lv["start"], (80, 160, 255))
    img.save(path)


def main():
    lv = level()
    if len(sys.argv) == 5 and sys.argv[1] == "--view":
        lv["start"] = [int(v) for v in sys.argv[2].split(",")]
        lv["start_dir"] = int(sys.argv[3])
        lv["grid"][lv["start"][1]] = lv["grid"][lv["start"][1]]   # must be floor (checked by the writer)
        trilogy_level.write(lv, sys.argv[4])
        return
    stats = trilogy_level.write(lv, OUT)
    preview(lv, os.path.join(OUT, "preview_map.png"))
    print(f"arena {W}x{H}: {stats['vertices']} vertices, {stats['faces']} faces, {len(lv['doors'])} doors -> {OUT}")
    print(open(os.path.join(OUT, "level.txt")).read())


if __name__ == "__main__":
    main()
