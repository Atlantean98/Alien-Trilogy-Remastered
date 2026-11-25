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
SCENERY = {"263": ([25, 20], 8), "391": ([20], 5)}   # code: (object kinds, groups of 2-3)
DAIS = 128   # the Queen's dais height (one step; the player climbs 256)
# Floor and ceiling textures that make plain floor read as a lift: L391's pale lit floor tiles under a brown ceiling (a
# lift car's look, in a 3-deep pocket off the west corridor) and its lift-pad tiles (3x4 patches in the hall). In the
# arena they are ordinary floor, so they get the lair's common floor and ceiling. {code: (floors, floor, ceilings, ceiling)}
# Corridor mouths that turn a corner straight away look like small closed rooms from the hall (a lintel over a box with
# its back wall in view). Each becomes a bay of the hall instead: the solid inner corner is opened up and the bay takes
# the hall's ceiling, so the corridor is seen running away from its side. {code: [(x0, z0, x1, z1) inclusive]}
BAYS = {"391": [(2, 36, 10, 43), (5, 13, 12, 21)]}
LIFT_LOOK = {"391": ({278, 281, 282, 284, 285, 291, 292, 299, 300}, 283, {269, 270}, 54)}

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


def gfx_with_sac_page(code, extra_pages=()):
    """The lair's GFX.BIN plus L162's sac page as page 05 (then any extra L162 pages as 06, 07, ...), the index of the
    sac page's first texture rectangle, and {L162 page: first rectangle of its copy} for the extra pages."""
    own = open(os.path.join(os.path.dirname(tl.find_level(code)), f"{code}GFX.BIN"), "rb").read()
    src = dict(tl.chunks(open(os.path.join(os.path.dirname(tl.find_level("162")), "162GFX.BIN"), "rb").read()))
    count = lambda page: struct.unpack("<H", src[f"BX{page:02d}"][:2])[0]
    out = tl.chunks(own)
    first = sum(struct.unpack("<H", d[:2])[0] for t, d in out if t.startswith("BX"))
    out = [(t, bytearray(d)) for t, d in out]
    pages = [SAC_PAGE, *extra_pages]
    starts, at = {}, first
    for i, page in enumerate(pages):
        for kind in ("TP", "CL", "BX"):
            out.append((f"{kind}{NEW_PAGE + i:02d}", src[f"{kind}{page:02d}"]))
        starts[page] = at
        at += count(page)
    info = next(d for t, d in out if t == "INFO")
    struct.pack_into("<HH", info, 6, at, NEW_PAGE + len(pages))   # rectangle and page counts
    return form([(t, bytes(d)) for t, d in out], own[8:12]), first, {p: starts[p] for p in extra_pages}


def borrowed_door_models(code):
    """A lair with no door models of its own borrows L162's (TextureSet's fallback), but their faces number textures in
    L162's texture bank. Returns (the L162 pages they use, a function giving the models renumbered onto those pages'
    copies in the lair's bank), or ((), None) when the lair has its own."""
    ts = tl.TextureSet(code)
    if any(t[0] == "D" and t[1:].isdigit() for t, _ in ts.chunks):
        return (), None
    src = dict(tl.chunks(open(os.path.join(os.path.dirname(tl.find_level("162")), "162GFX.BIN"), "rb").read()))
    bounds, at = {}, 0
    for page in sorted(int(t[2:]) for t in src if t.startswith("BX")):
        n = struct.unpack("<H", src[f"BX{page:02d}"][:2])[0]
        bounds[page] = (at, at + n)
        at += n
    page_of = lambda tex: next(p for p, (a, b) in bounds.items() if a <= tex < b)
    used = sorted({page_of(struct.unpack_from("<H", d, 20 + i * 20 + 16)[0])
                   for _, d in ts.meshes for i in range(struct.unpack_from("<I", d, 12)[0])})

    def renumbered(starts):
        models = []
        for tag, data in ts.meshes:
            d = bytearray(data)
            for i in range(struct.unpack_from("<I", d, 12)[0]):
                tex = struct.unpack_from("<H", d, 20 + i * 20 + 16)[0]
                page = page_of(tex)
                struct.pack_into("<H", d, 20 + i * 20 + 16, starts[page] + tex - bounds[page][0])
            models.append((tag, bytes(d)))
        return models
    return tuple(used), renumbered


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


def fill_dead_ends(grid, ceil, region, hall=4000):
    """The lair's dead-end pockets: lower-ceilinged stubs off the main hall that lead nowhere read as lift cars (a lintel
    over a small box). A connected patch of lower ceiling is one when its cells meet the hall through a single opening
    (the corridors that loop round meet it at two or more). Returns the grid with them solid and the cells filled."""
    W, H = len(grid[0]), len(grid)
    low = {c for c in region if ceil.get(f"{c[0]},{c[1]}", 0) < hall}
    near = lambda x, z: ((x + 1, z), (x - 1, z), (x, z + 1), (x, z - 1))
    filled, seen = set(), set()
    for c in sorted(low):
        if c in seen:
            continue
        comp, todo = {c}, [c]
        while todo:
            for n in near(*todo.pop()):
                if n in low and n not in comp:
                    comp.add(n)
                    todo.append(n)
        seen |= comp
        mouth = {n for p in comp for n in near(*p) if n in region and n not in low}
        groups, left = 0, set(mouth)
        while left:
            groups += 1
            todo = [left.pop()]
            while todo:
                for n in [n for d in near(*todo.pop()) for n in [d] + list(near(*d))]:
                    if n in left:
                        left.discard(n)
                        todo.append(n)
        if groups == 1:
            filled |= comp
    rows = ["".join("#" if (x, z) in filled else grid[z][x] for x in range(W)) for z in range(H)]
    return rows, filled


def campaign_surfaces(code, rows, scale, src_size):
    """Walls and lights as the lair has them (trilogy_level.surfaces), for the arena's open cells: each cell takes its
    source cell's floor and ceiling lights, and each side the textures and lights, band by band, of the nearest source
    cell with a wall on that side (its own first, then along the wall, then further out). Sides with none keep the
    generator's own choice."""
    flats, walls = tl.surfaces(code)
    sw, sh = src_size
    out_walls, out_lights = {}, {}
    for z, row in enumerate(rows):
        for x, t in enumerate(row):
            if t != ".":
                continue
            sx, sz = min(sw - 1, int((x + 0.5) / scale)), min(sh - 1, int((z + 0.5) / scale))
            if (sx, sz) in flats:
                out_lights[f"{x},{z}"] = list(flats[(sx, sz)])
            for side in range(4):
                along = (0, 1) if side < 2 else (1, 0)
                found = None
                for r in range(3):
                    ring = [(i, j) for i in range(-r, r + 1) for j in range(-r, r + 1) if max(abs(i), abs(j)) == r]
                    # along the wall first (same distance into the room), then off it
                    ring.sort(key=lambda o: (abs(o[0] * along[1] + o[1] * along[0]), abs(o[0]) + abs(o[1])))
                    for i, j in ring:
                        bands = walls.get((sx + i, sz + j, side))
                        if bands:
                            found = bands
                            break
                    if found:
                        break
                if found:
                    out_walls[f"{x},{z},{side}"] = [list(found.get(b, found[min(found, key=lambda k: abs(k - b))]))
                                                    for b in range(8)]
    return out_walls, out_lights


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
    if code in LIFT_LOOK:
        floors, floor_tex, ceilings, ceil_tex = LIFT_LOOK[code]
        changed = 0
        for k, t in tex.items():
            new = [floor_tex if t[0] in floors else t[0], t[1], ceil_tex if t[2] in ceilings else t[2]]
            changed += new != t
            tex[k] = new
        print(f"arena {number}: {changed} cells that looked like lifts retextured as plain floor")
    region = largest_region(grid)
    if code in LIFT_LOOK:
        grid, filled = fill_dead_ends(grid, ceil, region)
        region = largest_region(grid)
        print(f"arena {number}: {len(filled)} cells of dead-end pockets filled in")
    for x0, z0, x1, z1 in BAYS.get(code, []):
        rows = [list(r) for r in grid]
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                rows[z][x] = "."
                ceil[f"{x},{z}"] = 4064
                tex[f"{x},{z}"] = [LIFT_LOOK[code][1], None, None]
        grid = ["".join(r) for r in rows]
        region = largest_region(grid)
        print(f"arena {number}: corridor mouth at ({x0}-{x1}, {z0}-{z1}) opened into a bay of the hall")
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
    # The lair's own walls and lighting (its tinted lights, and its wall textures in the order it lays them).
    lv["walls"], lv["lights"] = campaign_surfaces(code, rows, scale, (src["width"], src["height"]))
    print(f"arena {number}: {len(lv['walls'])} wall sides and {len(lv['lights'])} cells lit as in L{code}")
    # Explosive barrels in three small groups, off the supply spots and the Queen's dais (Survival respawns destroyed
    # ones every other wave).
    dais = [(queen[0] + i, queen[1] + j) for i in range(-5, 6) for j in range(-5, 6)]
    supplies = [tuple(c) for key in ("ammo_spots", "health_spots", "reserve_spots") for c in lv[key]]
    lv["objects"] = tl.place_barrels(lv, avoid=dais + supplies)
    # Scenery as the lair's section dresses its rooms: crates and lockers in Section 2's foundry, the alien pods
    # (kind 20, drawn as pods in Section 3) aboard the derelict.
    lv["objects"] += tl.place_scenery(lv, SCENERY[code][0], groups=SCENERY[code][1], avoid=dais + supplies)
    out = os.path.join(X360, "assets", f"survival{number}")
    # Doors: a lair without door models of its own (L391) borrows L162's - their texture pages come along too, and the
    # models are renumbered onto them (else its doors showed whatever the lair's own bank held there, or nothing).
    door_pages, renumber = borrowed_door_models(code)
    gfx, first_rect, door_starts = gfx_with_sac_page(code, door_pages)
    texset = None
    if renumber:
        texset = tl.TextureSet(code)
        texset.meshes = renumber(door_starts)
    stats = tl.write(lv, out, texset)
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
