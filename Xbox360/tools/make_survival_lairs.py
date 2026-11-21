"""Builds Survival arenas 2 and 3 from the campaign's own Queen's Lairs, resampled to a new size:

  2  Section 2's Queen's Lair (L263), much larger (x1.45): GameData/SURVIVAL2
  3  Section 3's Queen's Lair (L391), a little smaller (x0.9), pistol and shotgun from the start: GameData/SURVIVAL3

The Queen gets L162's egg sac and cord as in the first arena: its texture page is added to the arena's GFX.BIN and the
sac's map (QUEENSAC.MAP, L162's with the sac retextured) goes beside it; she stands where the sac fits the floor.

The lair's open cells, per-cell floor and ceiling textures and ceiling clearances are resampled (nearest cell); floors
are flattened (the lairs' ramps don't survive resampling) except for a low dais under the Queen. Six door-sealed spawn
rooms are cut into the walls round the edge, spread out and away from the start and the Queen; ammunition, medikit
and reserve spots are spread over the reachable floor (26, 14 and 5, as the first arena). The level is written by
trilogy_level.py in the lair's own code, so its textures, sounds and music apply.

Writes assets/survival2|3 (map, GFX, level.txt, preview_map.png) and copies them to GameData/SURVIVAL2|3.

Usage: python tools/make_survival_lairs.py
"""
import math
import os
import shutil
import struct

from PIL import Image, ImageDraw

import trilogy_level as tl

HERE = os.path.dirname(os.path.abspath(__file__))
X360 = os.path.normpath(os.path.join(HERE, ".."))
ROOT = os.path.normpath(os.path.join(X360, ".."))

ARENAS = [   # number, source lair code, scale, name, enemy roster, starting weapons (0 pistol, 1 shotgun)
    (2, "263", 1.45, "Foundry Lair", "section2", [0]),
    (3, "391", 0.90, "Derelict Lair", "section3", [0]),   # the shotgun comes at wave 2 (the shared weapon schedule, ExplorerSurvival.h)
]
DAIS = 128   # the Queen's dais height (one step; the player climbs 256)

# The Queen's egg sac and cord (the game draws it behind her in Survival, LevelExplorer loadSurvivalQueenSac) is a
# block of L162's map, textured from L162's texture page 3. The other arenas get that page as an extra page 05 of their
# own GFX.BIN and a copy of L162's map (QUEENSAC.MAP) whose sac faces point at it.
SAC_VERTICES = (4773, 5425)   # LevelExplorer.cpp: sacVertexStart, sacVertexEnd
SAC_PAGE, NEW_PAGE = 3, 5
SAC_ANGLE, SAC_SCALE = 3 * math.pi / 4, 0.85   # LevelExplorer.cpp: SurvivalSacAngle, the sac's scale


def form(chunks_, kind):
    body = kind
    for tag, data in chunks_:
        body += tag.encode("latin1") + struct.pack(">I", len(data)) + data + (b"\0" if len(data) & 1 else b"")
    return b"FORM" + struct.pack(">I", len(body)) + body


def gfx_with_sac_page(code):
    """The lair's GFX.BIN plus L162's sac page as page 05, and the index of its first texture rectangle."""
    own = open(os.path.join(os.path.dirname(tl.find_level(code)), f"{code}GFX.BIN"), "rb").read()
    src = dict(tl.chunks(open(os.path.join(os.path.dirname(tl.find_level("162")), "162GFX.BIN"), "rb").read()))
    out = tl.chunks(own)
    first = sum(struct.unpack("<H", d[:2])[0] for t, d in out if t.startswith("BX"))
    added = struct.unpack("<H", src[f"BX{SAC_PAGE:02d}"][:2])[0]
    out = [(t, bytearray(d)) for t, d in out]
    info = next(d for t, d in out if t == "INFO")
    struct.pack_into("<HH", info, 6, first + added, NEW_PAGE + 1)   # rectangle and page counts
    for kind in ("TP", "CL", "BX"):
        out.append((f"{kind}{NEW_PAGE:02d}", src[f"{kind}{SAC_PAGE:02d}"]))
    return form([(t, bytes(d)) for t, d in out], own[8:12]), first


def sac_map(first_rect):
    """L162's map with the sac's faces textured from the new page."""
    raw = open(tl.find_level("162"), "rb").read()
    chunks_ = tl.chunks(raw)
    m = bytearray(dict(chunks_)["MAP0"])
    w = lambda o: struct.unpack("<H", m[o:o + 2])[0]
    vc, fc = w(0), w(2)
    fb = 36 + vc * 8
    page_start = sum(struct.unpack("<H", d[:2])[0] for t, d in
                     tl.chunks(open(os.path.join(os.path.dirname(tl.find_level("162")), "162GFX.BIN"), "rb").read())
                     if t.startswith("BX") and int(t[2:]) < SAC_PAGE)
    page_count = struct.unpack("<H", dict(tl.chunks(open(os.path.join(os.path.dirname(tl.find_level("162")), "162GFX.BIN"), "rb").read()))[f"BX{SAC_PAGE:02d}"][:2])[0]
    faces = 0
    for i in range(fc - 1):
        o = fb + i * 20
        idx = struct.unpack("<IIII", m[o:o + 16])
        n = 3 if idx[3] & 0x80000000 else 4
        if not all(SAC_VERTICES[0] <= j < SAC_VERTICES[1] for j in idx[:n]):
            continue
        tex = w(o + 16)
        if not page_start <= tex < page_start + page_count:
            raise SystemExit(f"sac face {i} uses texture {tex}, not on L162's page {SAC_PAGE}")
        struct.pack_into("<H", m, o + 16, first_rect + tex - page_start)
        faces += 1
    assert faces == 632, faces   # LevelExplorer's check
    return form([(t, bytes(m) if t == "MAP0" else d) for t, d in chunks_], raw[8:12])


def sac_footprint():
    """The sac's floor footprint around the Queen, as drawn (offsets in world units)."""
    m = dict(tl.chunks(open(tl.find_level("162"), "rb").read()))["MAP0"]
    w = lambda o: struct.unpack("<H", m[o:o + 2])[0]
    s = lambda o: struct.unpack("<h", m[o:o + 2])[0]
    vc, fc, W, H = w(0), w(2), w(4), w(6)
    enemies = 36 + vc * 8 + fc * 20 + W * H * 16 + m[12] * 8
    qx, qz = m[enemies + 1] * 512 + 256 - W * 256, m[enemies + 2] * 512 + 256 - H * 256
    cs, sn = math.cos(SAC_ANGLE), math.sin(SAC_ANGLE)
    out = []
    for i in range(*SAC_VERTICES):
        x, z = (s(36 + i * 8) - qx) * SAC_SCALE, (s(36 + i * 8 + 4) - qz) * SAC_SCALE
        out.append((x * cs + z * sn, -x * sn + z * cs))
    return out


def sac_fits(region, cell, footprint):
    ax, az = cell[0] * 512 + 256, cell[1] * 512 + 256
    return all((int((ax + fx) // 512), int((az + fz) // 512)) in region for fx, fz in footprint)


def resample(src, scale):
    """The lair's open shape, textures and ceiling clearances at the new size (nearest source cell)."""
    sw, sh = src["width"], src["height"]
    W, H = min(127, round(sw * scale)), min(127, round(sh * scale))
    grid, tex, ceil = [], {}, {}
    sfloor = src["floor"]
    for z in range(H):
        row = ""
        for x in range(W):
            sx, sz = min(sw - 1, int((x + 0.5) / scale)), min(sh - 1, int((z + 0.5) / scale))
            open_ = 0 < x < W - 1 and 0 < z < H - 1 and src["grid"][sz][sx] == "."
            row += "." if open_ else "#"
            if open_:
                k = f"{sx},{sz}"
                if k in src["tex"]:
                    own = [t for t in src["tex"][k][:3]]
                    tex[f"{x},{z}"] = own
                if k in src["ceil"]:   # clearance above the source floor, now above floor 0
                    clearance = src["ceil"][k] - sfloor.get(k, 0)
                    ceil[f"{x},{z}"] = max(tl.MIN_HEADROOM + 256, min(4064, clearance))
        grid.append(row)
    return W, H, grid, tex, ceil


def reachable(grid, start):
    W, H = len(grid[0]), len(grid)
    seen, todo = {start}, [start]
    while todo:
        x, z = todo.pop()
        for nx, nz in ((x + 1, z), (x - 1, z), (x, z + 1), (x, z - 1)):
            if 0 <= nx < W and 0 <= nz < H and (nx, nz) not in seen and grid[nz][nx] in ".R":
                seen.add((nx, nz))
                todo.append((nx, nz))
    return seen


def largest_region(grid):
    W, H = len(grid[0]), len(grid)
    best, done = set(), set()
    for z in range(H):
        for x in range(W):
            if grid[z][x] == "." and (x, z) not in done:
                region = reachable(grid, (x, z))
                done |= region
                if len(region) > len(best):
                    best = region
    return best


def door_candidates(grid, region):
    """Places for a 4-cell door: the line of 4 solid wall cells touches open floor on its inner side along its whole
    length, and the 3 lines beyond it are solid and inside the map (for the spawn room)."""
    W, H = len(grid[0]), len(grid)
    out = []
    solid = lambda x, z: 0 <= x < W and 0 <= z < H and grid[z][x] == "#"
    for side, (ox, oz) in (("west", (-1, 0)), ("east", (1, 0)), ("south", (0, -1)), ("north", (0, 1))):
        along = (0, 1) if ox else (1, 0)
        for z in range(1, H - 1):
            for x in range(1, W - 1):
                cells = [(x + along[0] * i, z + along[1] * i) for i in range(4)]
                if not all(solid(cx, cz) for cx, cz in cells):
                    continue
                if not all((cx - ox, cz - oz) in region for cx, cz in cells):
                    continue
                room = [(cx + ox * dd, cz + oz * dd) for cx, cz in cells for dd in range(1, 4)]
                if not all(1 <= rx < W - 1 and 1 <= rz < H - 1 and solid(rx, rz) for rx, rz in room):
                    continue
                # the room's own walls stay solid: one more line round it inside the map
                ring = [(cx + ox * 4, cz + oz * 4) for cx, cz in cells]
                ring += [(cells[0][0] - along[0] + ox * dd, cells[0][1] - along[1] + oz * dd) for dd in range(1, 4)]
                ring += [(cells[3][0] + along[0] + ox * dd, cells[3][1] + along[1] + oz * dd) for dd in range(1, 4)]
                if not all(0 <= rx < W and 0 <= rz < H and grid[rz][rx] == "#" for rx, rz in ring):
                    continue
                out.append(dict(side=side, cells=cells, room=room, axis="z" if ox else "x"))
    return out


def spread(candidates, count, avoid, min_avoid):
    """Farthest-point picks from candidates (dicts with cells, or cells), away from the points in avoid."""
    centre = lambda c: (sum(p[0] for p in c["cells"]) / 4, sum(p[1] for p in c["cells"]) / 4) if isinstance(c, dict) else c
    pool = [c for c in candidates if all(math.dist(centre(c), a) >= min_avoid for a in avoid)]
    picked = []
    while pool and len(picked) < count:
        if not picked:
            best = max(pool, key=lambda c: min(math.dist(centre(c), a) for a in avoid) if avoid else 0)
        else:
            best = max(pool, key=lambda c: min(math.dist(centre(c), centre(p)) for p in picked))
        picked.append(best)
        pool = [c for c in pool if math.dist(centre(c), centre(best)) >= 6 and
                not (isinstance(c, dict) and set(c["cells"] + c["room"]) & set(best["cells"] + best["room"]))]
    return picked


START_DISTANCE = 8   # cells from the Queen: ~4100 units, where she shows dimly through the black fog (2040-5100)


def line_of_sight(grid, a, b):
    ax, az, bx, bz = a[0] + .5, a[1] + .5, b[0] + .5, b[1] + .5
    n = int(max(abs(bx - ax), abs(bz - az)) * 4) + 1
    return all(grid[int(az + (bz - az) * i / n)][int(ax + (bx - ax) * i / n)] != "#" for i in range(n + 1))


def start_cell(grid, region, queen, footprint):
    """Where the player starts (facing the Queen, LevelExplorer/ExplorerSurvival): START_DISTANCE cells from her with
    a clear line to her, in open floor, in front of her rather than on her egg sac's side."""
    sx = sum(f[0] for f in footprint) / len(footprint)
    sz = sum(f[1] for f in footprint) / len(footprint)
    open_ = lambda c: all((c[0] + i, c[1] + j) in region for i in (-1, 0, 1) for j in (-1, 0, 1))
    pool = [c for c in region if open_(c) and max(abs(c[0] - queen[0]), abs(c[1] - queen[1])) > 3
            and (c[0] - queen[0]) * sx + (c[1] - queen[1]) * sz < 0 and line_of_sight(grid, c, queen)]
    if not pool:
        raise SystemExit("no start cell in front of the Queen with a clear view of her")
    return min(pool, key=lambda c: (abs(math.dist(c, queen) - START_DISTANCE), c))


def build(number, code, scale, name, roster, weapons):
    src = tl.import_level(tl.find_level(code))
    W, H, grid, tex, ceil = resample(src, scale)
    region = largest_region(grid)
    grid = ["".join("." if (x, z) in region else "#" for x in range(W)) for z in range(H)]
    # The Queen where the lair has her, on a dais; the start in front of her at the edge of the fog (start_cell).
    sq = next(e for e in src["enemies"] if e["kind"] == 7)
    qx, qz = min(W - 2, int((sq["x"] + 0.5) * scale)), min(H - 2, int((sq["z"] + 0.5) * scale))
    # The nearest cell to her place in the lair where her egg sac stays inside the floor.
    footprint = sac_footprint()
    queen = min((c for c in region if sac_fits(region, c, footprint)), key=lambda c: math.dist(c, (qx, qz)))
    start = start_cell(grid, region, queen, footprint)
    floor = {}
    for x, z in region:
        if max(abs(x - queen[0]), abs(z - queen[1])) <= 2:
            floor[f"{x},{z}"] = DAIS
            k = f"{x},{z}"
            if k in ceil:
                ceil[k] = max(ceil[k], DAIS + tl.MIN_HEADROOM + 512)
    # Spawn rooms.
    doors = spread(door_candidates(grid, region), 6, [start, queen], 10)
    if len(doors) < 6:
        raise SystemExit(f"arena {number}: only {len(doors)} spawn rooms fit")
    rows = [list(r) for r in grid]
    door_list = []
    for d in doors:
        for x, z in d["room"]:
            rows[z][x] = "R"
        cells = sorted(d["cells"])
        door_list.append({"x": cells[0][0], "z": cells[0][1], "axis": d["axis"]})
    rows = ["".join(r) for r in rows]
    # Supplies over the floor, none on the dais or by the doors.
    near_door = {(x + i, z + j) for d in doors for x, z in d["cells"] for i in range(-2, 3) for j in range(-2, 3)}
    floor_cells = sorted(c for c in region if c not in near_door and max(abs(c[0] - queen[0]), abs(c[1] - queen[1])) > 4
                         and all((c[0] + i, c[1] + j) in region for i in (-1, 0, 1) for j in (-1, 0, 1)))
    ammo = spread(floor_cells, 26, [start], 4)
    health = spread([c for c in floor_cells if c not in ammo], 14, [start] + ammo, 2)
    reserve = sorted((c for c in region if 1 <= math.dist(c, start) <= 3 and c not in near_door),
                     key=lambda c: math.dist(c, start))[:5]
    facing = int(round((math.atan2(queen[0] - start[0], queen[1] - start[1]) / (2 * math.pi)) * 8)) % 8
    lv = {
        "name": name, "code": code, "width": W, "height": H, "grid": rows, "doors": door_list,
        "ceil": ceil, "tex": tex, "floor": floor, "ramp": {},
        "start": list(start), "start_dir": (4 - facing) % 8, "enemies": [{"kind": 7, "x": queen[0], "z": queen[1]}],
        "pickups": [], "mode": "survival", "lives": 5, "weapons": weapons, "waves": 10, "roster": roster,
        "queen_last_wave": True, "brightness": "normal",
        "ammo_spots": [list(c) for c in ammo], "health_spots": [list(c) for c in health],
        "reserve_spots": [list(c) for c in reserve],
    }
    out = os.path.join(X360, "assets", f"survival{number}")
    stats = tl.write(lv, out)
    gfx, first_rect = gfx_with_sac_page(code)
    open(os.path.join(out, f"{code}GFX.BIN"), "wb").write(gfx)
    open(os.path.join(out, "QUEENSAC.MAP"), "wb").write(sac_map(first_rect))
    preview(lv, os.path.join(out, "preview_map.png"))
    game = os.path.join(ROOT, "GameData", f"SURVIVAL{number}")
    os.makedirs(game, exist_ok=True)
    for f in os.listdir(game):
        os.remove(os.path.join(game, f))
    for f in os.listdir(out):
        if f != "preview_map.png":
            shutil.copy2(os.path.join(out, f), game)
    print(f"arena {number} ({name}, L{code} x{scale}): {W}x{H}, {len(region)} floor cells, {stats['faces']} faces, "
          f"{len(door_list)} doors, queen {queen}, start {start} -> {game}")


def preview(lv, path):
    s = 6
    W, H = lv["width"], lv["height"]
    img = Image.new("RGB", (W * s, H * s), (10, 10, 10))
    d = ImageDraw.Draw(img)
    grid = [list(r) for r in lv["grid"]]
    for dd in lv["doors"]:
        for x, z in tl.door_cells(dd):
            grid[z][x] = "D"
    colours = {"#": (25, 22, 20), ".": (60, 90, 60), "R": (90, 90, 140), "D": (220, 180, 40)}
    for z in range(H):
        for x in range(W):
            y = (H - 1 - z) * s
            c = colours[grid[z][x]]
            if f"{x},{z}" in lv["floor"]:
                c = (110, 150, 90)
            d.rectangle((x * s, y, x * s + s - 1, y + s - 1), fill=c)

    def mark(c, col, r=0):
        y = (H - 1 - c[1]) * s
        d.ellipse((c[0] * s - r, y - r, c[0] * s + s + r, y + s + r), fill=col)
    for c in lv["ammo_spots"]:
        mark(c, (230, 200, 60))
    for c in lv["health_spots"]:
        mark(c, (230, 230, 230))
    for c in lv["reserve_spots"]:
        mark(c, (60, 220, 230))
    mark(lv["start"], (80, 160, 255), 2)
    q = lv["enemies"][0]
    mark((q["x"], q["z"]), (255, 40, 40), 2)
    img.save(path)


if __name__ == "__main__":
    for arena in ARENAS:
        build(*arena)
