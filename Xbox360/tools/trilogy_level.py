"""Reads and writes Alien Trilogy levels in the game's own format: for the Xbox 360 build's Survival arena
(make_survival_map.py) and the standalone level editor (LevelEditor/editor_server.py, 3D).

A level is described by a plain dict (the editor's project file is the same thing as JSON):
  name, code                 display name; the campaign level whose textures, sounds and music it uses ("162")
  width, height, grid        grid[z] is a string over x: '#' wall, '.' floor, 'R' spawn room, 'W' hive wall
                             (Survival's queen chamber: hive textures), 'D' door (from `doors`, not painted)
  doors                      [{"x", "z", "axis": "x"|"z"}]: 4 cells from (x, z) along the axis
  ceil                       {"x,z": height} per cell (world units, up to 8160, kept MIN_HEADROOM above the floor);
                             missing = the default dome above the cell's floor
  floor                      {"x,z": height} per cell (0..4096, steps of 32; missing = 0). The player climbs at most
                             STEP_LIMIT from one cell to the next.
  ramp                       {"x,z": direction} ramp cells rising RAMP_RISE across the cell from its floor height:
                             0 toward +z, 1 toward +x, 2 toward -x, 3 toward -z (the game's cell types 0x31-0x34)
  tex                        {"x,z": [floor, wall, ceiling, west, east, north(-z), south(+z)]} texture numbers of the
                             set, null = automatic; the side entries (optional) override "wall" for that side
  start, start_dir           player start cell [x, z] and facing 0-7
  enemies                    [{"kind", "x", "z"}] (kinds in ENEMIES; 7 = the queen)
  pickups                    [{"kind", "x", "z", "amount"?, "reserve"?}] (kinds in PICKUPS)
  mode                       "survival" or "exterminate"; lives, weapons (starting, 0-4), waves (count),
                             roster ("campaign", "section1", "section2", "section3"), queen_last_wave,
                             brightness ("dark", "normal", "bright")
build(level) checks it and returns the MAP0 bytes and a level.txt describing it; write(level, folder) writes
L<code>LEV.MAP, <code>GFX.BIN and level.txt into the folder (the 360's Survival arena: GameData/SURVIVAL/).
import_level(path) turns one of the campaign's levels into such a dict (its layout, doors, enemies, pickups, start
and floor/ceiling textures; floors come out flat, lifts and sloped cave walls are not kept).

The format (LevelLoading.cpp initializeLevelLayout, LevelExplorer.cpp load): header (36 bytes), vertices (x, y, z,
0), faces (4 vertex indices, texture, flags, light; one all -1 face last), cells (16 bytes: floor face, ..., type,
floor/32, occupant, ceiling/32, 10, trigger), waypoints, monsters (20), pickups (8), crates, doors (8), lifts,
trigger tables, lights (28), spare tables. World x = cell x * 512 - width * 256 (z likewise), y up.
"""
import collections
import math
import os
import shutil
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))          # the repository root
# The PS1 import (tools/import_pc_ps1.py) when present - the PC build reads only that now - else the DOS files.
# Both number their textures alike (same rectangle tables); the PS1 pages carry the PS1 colours.
GAME_CD = os.path.join(ROOT, "GameData", "PS1", "CD")
if not os.path.isdir(os.path.join(GAME_CD, "SECT12")):
    GAME_CD = os.path.join(ROOT, "GameData", "CD")

FLOOR = 0
DOOR_CEIL = 1376      # door opening (doors.load: travel for every level but 111/112)
ROOM_CEIL = 2048
MAX_FLOOR = 4096      # floor heights (cell byte 11 = height / 32)
MAX_CEIL = 8160       # cell byte 13 = ceiling / 32
MIN_HEADROOM = 1280   # a ceiling is kept at least this far above its floor
RAMP_RISE = 128       # the game's ramp cells rise 128 across the cell (WorldMovement.cpp floorHeight)
STEP_LIMIT = 256      # the player climbs at most this much from cell to cell (WorldMovement.cpp)
SPARE_RECORDS = 63    # Survival rewrites these to spawn its waves

# Enemy kinds (ExplorerEnemies names) with a monster record from the campaign's levels.
ENEMIES = {
    1: ("Egg", [1, 0, 0, 255, 0, 5, 255, 0, 0, 2, 0, 16, 0, 8, 0, 0, 0, 8, 0, 4]),
    2: ("Facehugger", [2, 0, 0, 255, 0, 2, 255, 0, 0, 0, 0, 4, 252, 9, 100, 0, 0, 6, 0, 3]),
    3: ("Chestburster", [3, 0, 0, 255, 0, 10, 255, 0, 0, 0, 182, 4, 46, 22, 100, 0, 130, 7, 0, 3]),
    4: ("Bambi", [4, 0, 0, 255, 0, 20, 255, 0, 0, 0, 0, 8, 170, 17, 60, 0, 228, 3, 220, 2]),
    5: ("Dog alien", [5, 0, 0, 255, 0, 30, 255, 0, 0, 0, 130, 9, 0, 20, 100, 0, 244, 3, 0, 3]),
    6: ("Warrior", [6, 0, 0, 255, 0, 30, 255, 0, 0, 0, 11, 6, 184, 14, 100, 0, 18, 4, 0, 3]),
    7: ("Queen", [7, 0, 0, 255, 4, 190, 2, 0, 0, 0, 164, 7, 177, 37, 150, 0, 60, 14, 24, 11]),
    8: ("Ceiling warrior", [8, 0, 0, 255, 0, 50, 255, 0, 0, 0, 0, 16, 199, 32, 100, 0, 241, 3, 231, 2]),
    9: ("Ceiling dog alien", [9, 0, 0, 255, 0, 30, 255, 0, 0, 0, 51, 7, 0, 20, 40, 0, 192, 3, 0, 3]),
    11: ("Guard", [11, 0, 0, 255, 0, 10, 255, 0, 0, 0, 79, 6, 137, 25, 50, 0, 120, 15, 187, 14]),
    12: ("Soldier", [12, 0, 0, 255, 0, 20, 255, 0, 0, 0, 0, 8, 210, 18, 60, 0, 0, 33, 137, 14]),
    13: ("Synthetic", [13, 0, 0, 255, 0, 80, 255, 0, 0, 0, 136, 4, 146, 24, 30, 0, 114, 24, 0, 6]),
    14: ("Handler", [14, 0, 0, 255, 0, 30, 255, 0, 0, 0, 0, 8, 26, 35, 80, 0, 0, 42, 86, 34]),
}
# Pickup kinds (PickupRuntime.cpp awardPickup) with their usual amount and reserve.
PICKUPS = {
    1: ("Shotgun", 24, 0), 2: ("Pulse rifle", 99, 2), 3: ("Flamethrower", 250, 0), 4: ("Smartgun", 100, 2),
    9: ("Pistol clips", 15, 2), 10: ("Shotgun shells", 16, 0), 11: ("Pulse rifle ammo", 99, 1), 12: ("Grenades", 5, 0),
    13: ("Flamethrower fuel", 100, 0), 14: ("Smartgun ammo", 100, 1), 20: ("Medikit", 40, 0), 18: ("Armour", 50, 0),
    19: ("Heavy armour", 100, 0), 17: ("Hypo", 0, 0), 16: ("Auto mapper", 0, 0),
}
WEAPONS = ["Pistol", "Shotgun", "Pulse rifle", "Flamethrower", "Smartgun"]
SETS = {   # texture sets by level code, named for the editor
    "111": "Colony entrance", "112": "Colony", "113": "Colony (lit)", "122": "Recreation rooms", "131": "Garage",
    "141": "Operations", "154": "Atmosphere plant", "161": "Hive", "162": "Queen's hive",
    "211": "Prison", "212": "Prison block", "213": "Prison tunnels", "222": "Foundry", "231": "Refinery",
    "232": "Lead works", "242": "Mine", "243": "Mine shafts", "262": "Furnace", "263": "Queen's furnace",
    "311": "Derelict ship", "321": "Ship tunnels", "322": "Ship decks", "323": "Ship core", "324": "Ship hold",
    "325": "Ship engines", "331": "Hive ship", "351": "Bone ship", "352": "Bone tunnels", "353": "Bone halls",
    "361": "Egg chamber", "371": "Hive core", "381": "Hive depths", "391": "Queen's lair",
}
BRIGHTNESS = {"dark": 0.6, "normal": 1.0, "bright": 1.5}
SKIP_FLAGS = {8, 10, 12, 13, 14, 26, 28, 30, 32, 34}   # animated or translucent faces


def chunks(data):
    out, p = [], 12
    while p + 8 <= len(data):
        tag = data[p:p + 4].decode("latin1")
        n = struct.unpack(">I", data[p + 4:p + 8])[0]
        out.append((tag, data[p + 8:p + 8 + n]))
        p += 8 + n + (n & 1)
    return out


def find_level(code):
    for section in sorted(os.listdir(GAME_CD)):
        if not os.path.isdir(os.path.join(GAME_CD, section)):
            continue
        path = os.path.join(GAME_CD, section, f"L{code}LEV.MAP")
        if os.path.exists(path):
            return path
    return None


def available_sets():
    return [code for code in SETS if find_level(code) and os.path.exists(os.path.join(os.path.dirname(find_level(code)), f"{code}GFX.BIN"))]


class TextureSet:
    """A campaign level's textures and the look of its floors, ceilings and walls."""

    def __init__(self, code):
        self.code = code
        self.map_path = find_level(code)
        if not self.map_path:
            raise ValueError(f"no level L{code}LEV.MAP in {GAME_CD}")
        self.gfx_path = os.path.join(os.path.dirname(self.map_path), f"{code}GFX.BIN")
        raw = open(self.map_path, "rb").read()
        self.form = raw[8:12]
        self.chunks = chunks(raw)
        m = dict(self.chunks)["MAP0"]
        w = lambda o: struct.unpack("<H", m[o:o + 2])[0]
        vc, fc, self.W, self.H = w(0), w(2), w(4), w(6)
        fb = 36 + vc * 8
        cb = fb + fc * 20
        wb = cb + self.W * self.H * 16
        after = wb + m[12] * 8 + w(14) * 20 + w(16) * 8 + w(18) * 16
        lb = after + w(20) * 8 + w(22) * 16 + m[28] * 4 + m[29] * 4
        self.lights = bytes(m[lb:lb + m[13] * 28])
        self.light_count = m[13]
        self.meshes = [(t, d) for t, d in self.chunks if t[0] in "D" and t[1:].isdigit()]
        if not self.meshes and code != "162":
            self.meshes = TextureSet("162").meshes
        self._load_gfx()
        vert = lambda i: struct.unpack("<hhh", m[36 + i * 8:42 + i * 8])
        floors, ceilings, bands = {}, {}, collections.defaultdict(collections.Counter)
        floor_count, ceil_count = collections.Counter(), collections.Counter()
        for i in range(fc - 1):
            o = fb + i * 20
            idx = [j for j in struct.unpack("<iiii", m[o:o + 16]) if j >= 0]
            tex, flags = w(o + 16), m[o + 18]
            if flags in SKIP_FLAGS or tex >= len(self.rects) or tex in self.transparent:
                continue
            vs = [vert(j) for j in idx]
            ys = {v[1] for v in vs}
            if len(ys) == 1:
                continue
            band = min(v[1] for v in vs) // 512
            bands[band][tex] += 1
        for z in range(self.H):
            for x in range(self.W):
                c = m[cb + (z * self.W + x) * 16:cb + (z * self.W + x) * 16 + 16]
                ptr = struct.unpack("<i", c[0:4])[0]
                if ptr < 0:
                    continue
                for fi in (ptr, ptr + 1):
                    if fi >= fc - 1:
                        continue
                    o = fb + fi * 20
                    idx = [j for j in struct.unpack("<iiii", m[o:o + 16]) if j >= 0]
                    tex, flags = w(o + 16), m[o + 18]
                    ys = {vert(j)[1] for j in idx}
                    if len(ys) != 1 or flags in SKIP_FLAGS or tex >= len(self.rects) or tex in self.transparent:
                        continue
                    if ys.pop() > c[11] * 32 + 64:
                        ceilings[(x, z)] = (tex, flags)
                        ceil_count[tex] += 1
                    else:
                        floors[(x, z)] = (tex, flags)
                        floor_count[tex] += 1
        self.floor_cells, self.ceil_cells = floors, ceilings
        self.floor_textures = [t for t, _ in floor_count.most_common()] or [0]
        self.ceil_textures = [t for t, _ in ceil_count.most_common()] or self.floor_textures
        all_walls = collections.Counter()
        for c in bands.values():
            all_walls.update(c)
        self.wall_textures = [t for t, _ in all_walls.most_common()] or self.floor_textures
        self.bands = []
        for b in range(8):
            top = [t for t, _ in bands.get(b, collections.Counter()).most_common(3)]
            self.bands.append(top or self.wall_textures[:3])
        if floors:
            xs = [p[0] for p in floors]
            zs = [p[1] for p in floors]
            self.bbox = (min(xs), max(xs), min(zs), max(zs))
        else:
            self.bbox = (0, self.W - 1, 0, self.H - 1)
        self._cache = {}

    def _load_gfx(self):
        data = open(self.gfx_path, "rb").read()
        cs = chunks(data)
        cd = dict(cs)
        self.pages, self.rects, self.transparent = {}, [], set()
        for tag, page in cs:
            if tag.startswith("TP"):
                self.pages[int(tag[2:])] = (page, cd["CL" + tag[2:]])
        for tag, box in cs:
            if tag.startswith("BX"):
                count = struct.unpack("<H", box[:2])[0]
                pg = int(tag[2:])
                for i in range(count):
                    q = 2 + i * 6
                    self.rects.append((pg, box[q], box[q + 1], box[q + 2] + 1, box[q + 3] + 1))
        for i, (pg, x, y, w_, h_) in enumerate(self.rects):
            page, pal = self.pages[pg]
            for yy in range(y, y + h_, 2):
                hit = False
                for xx in range(x, x + w_, 2):
                    k = yy * 256 + xx
                    idx = page[k] if len(page) == 65536 else (page[k // 2] >> ((k & 1) * 4)) & 15
                    c = struct.unpack("<H", pal[4 + idx * 2:6 + idx * 2])[0]
                    if (c & 0x7FFF) == 0x7C1F:
                        hit = True
                        break
                if hit:
                    self.transparent.add(i)
                    break

    def image(self, tex, size=32):
        """The texture as a PIL image (for the editor)."""
        from PIL import Image
        key = (tex, size)
        if key in self._cache:
            return self._cache[key]
        pg, x, y, w_, h_ = self.rects[tex]
        page, pal = self.pages[pg]
        img = Image.new("RGB", (w_, h_))
        px = img.load()
        for yy in range(h_):
            for xx in range(w_):
                k = (y + yy) * 256 + x + xx
                idx = page[k] if len(page) == 65536 else (page[k // 2] >> ((k & 1) * 4)) & 15
                c = struct.unpack("<H", pal[4 + idx * 2:6 + idx * 2])[0]
                px[xx, yy] = ((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, ((c >> 10) & 31) * 255 // 31)
        img = img.resize((size, size))
        self._cache[key] = img
        return img

    def usable(self):
        return [i for i in range(len(self.rects)) if i not in self.transparent]

    def sample(self, x, z, box, ceiling):
        """The source level's floor/ceiling texture at the place matching (x, z) of a level spanning `box`."""
        cells = self.ceil_cells if ceiling else self.floor_cells
        if not cells:
            return ((self.ceil_textures if ceiling else self.floor_textures)[0], 0)
        lx0, lx1, lz0, lz1 = self.bbox
        ax0, ax1, az0, az1 = box
        sx = round(lx0 + (x - ax0) * (lx1 - lx0) / max(1, ax1 - ax0))
        sz = round(lz0 + (z - az0) * (lz1 - lz0) / max(1, az1 - az0))
        for r in range(0, 16):
            for dz in range(-r, r + 1):
                for dx in range(-r, r + 1):
                    if max(abs(dx), abs(dz)) == r and (sx + dx, sz + dz) in cells:
                        return cells[(sx + dx, sz + dz)]
        return ((self.ceil_textures if ceiling else self.floor_textures)[0], 0)


def pick(options, *key):
    h = 2166136261
    for k in key:
        h = ((h ^ (int(k) & 0xFFFF)) * 16777619) & 0xFFFFFFFF
    return options[h % len(options)]


def noise(x, z, seed=0):
    return (math.sin(x * 0.71 + seed * 1.3) * math.cos(z * 0.53 - seed * 0.7) + math.sin((x + z) * 0.37 + seed)) / 2


def door_cells(door):
    x, z = door["x"], door["z"]
    return [(x + i, z) if door["axis"] == "x" else (x, z + i) for i in range(4)]


class LevelError(Exception):
    pass


def check(level):
    """Problems that stop a level working, as plain sentences (empty: fine)."""
    problems = []
    W, H = level["width"], level["height"]
    grid = [list(row) for row in level["grid"]]
    if not (16 <= W <= 127 and 16 <= H <= 127):
        problems.append("The map must be between 16 and 127 cells each way.")
        return problems
    open_cells = sum(row.count(".") for row in grid)
    if open_cells < 20:
        problems.append("Paint some floor first.")
    sx, sz = level.get("start") or (None, None)
    if sx is None or grid[sz][sx] != ".":
        problems.append("Put the player start on a floor cell.")
    for d in level.get("doors", []):
        cells = door_cells(d)
        if any(not (1 <= x < W - 1 and 1 <= z < H - 1) for x, z in cells):
            problems.append(f"The door at {d['x']},{d['z']} runs off the map.")
            continue
        sides = []
        for x, z in cells:
            a = (x, z - 1) if d["axis"] == "x" else (x - 1, z)
            b = (x, z + 1) if d["axis"] == "x" else (x + 1, z)
            sides.append((grid[a[1]][a[0]], grid[b[1]][b[0]]))
        if not any(s[0] in ".R" and s[1] in ".R" for s in sides):
            problems.append(f"The door at {d['x']},{d['z']} must sit in a wall with open floor (or a spawn room) on "
                            "both sides of it.")
    queens = [e for e in level.get("enemies", []) if e["kind"] == 7]
    if len(queens) > 1:
        problems.append("Only one Queen per level.")
    for e in level.get("enemies", []) + level.get("pickups", []):
        if grid[e["z"]][e["x"]] not in ".R":
            problems.append(f"Something is placed in a wall at {e['x']},{e['z']}.")
    if level.get("mode") == "survival":
        if not spawn_doors(level):
            problems.append("Survival needs at least one door with a spawn room (R) behind it.")
        if len([e for e in level.get("enemies", []) if e["kind"] != 7]) + 64 > 128:
            problems.append("Too many enemies placed.")
    elif level.get("mode") == "exterminate" and not level.get("enemies"):
        problems.append("Exterminate needs some enemies placed.")
    if len(level.get("enemies", [])) > (64 if level.get("mode") == "survival" else 128):
        problems.append("Too many enemies (128 at most).")
    if len(level.get("pickups", [])) > 100:
        problems.append("Too many pickups (100 at most).")
    return problems


def spawn_doors(level):
    """Doors with spawn-room cells on one side: (door, room cells, outward direction)."""
    grid = level["grid"]
    W, H = level["width"], level["height"]
    out = []
    for d in level.get("doors", []):
        cells = door_cells(d)
        for sign in (-1, 1):
            step = (0, sign) if d["axis"] == "x" else (sign, 0)
            first = [(x + step[0], z + step[1]) for x, z in cells]
            if all(0 <= x < W and 0 <= z < H and grid[z][x] == "R" for x, z in first):
                room, todo = set(first), list(first)
                while todo:
                    x, z = todo.pop()
                    for nx, nz in ((x + 1, z), (x - 1, z), (x, z + 1), (x, z - 1)):
                        if 0 <= nx < W and 0 <= nz < H and grid[nz][nx] == "R" and (nx, nz) not in room:
                            room.add((nx, nz))
                            todo.append((nx, nz))
                # nearest the door last: enemies fill a room from the back
                ordered = sorted(room, key=lambda c: -abs(c[0] - cells[1][0]) - abs(c[1] - cells[1][1]))
                out.append((d, ordered[:24], step))
                break
    return out


def build(level, texset=None):
    problems = check(level)
    if problems:
        raise LevelError("\n".join(problems))
    ts = texset or TextureSet(level["code"])
    g = geometry(level, ts)
    if len(g["verts"]) > 32767 or len(g["faces"]) + 1 > 32767:
        raise LevelError("The level is too big to build (too many faces): make it smaller or simpler.")
    return _tables(level, ts, g)


LIGHT_FLOOR, LIGHT_WALL, LIGHT_HIGH = 8, 9, 10   # light records the level's faces use


def geometry(level, ts):
    """The level's faces as the game gets them: verts [(x, y, z)], faces [((4 vertex indices), texture, flags,
    light)] and, per face, meta [(x, z, part)]: part 0 floor, 1 ceiling, 2-5 the west/east/north/south wall."""
    W, H = level["width"], level["height"]
    grid = [list(row) for row in level["grid"]]
    for x in range(W):
        grid[0][x] = grid[H - 1][x] = "#"
    for z in range(H):
        grid[z][0] = grid[z][W - 1] = "#"
    doors = level.get("doors", [])
    for d in doors:
        for x, z in door_cells(d):
            grid[z][x] = "D"
    is_open = lambda x, z: 0 <= x < W and 0 <= z < H and grid[z][x] in ".RD"
    ceil_set = {tuple(map(int, k.split(","))): int(v) for k, v in level.get("ceil", {}).items()}
    tex_set = {tuple(map(int, k.split(","))): v for k, v in level.get("tex", {}).items()}
    floor_set = {tuple(map(int, k.split(","))): max(0, min(MAX_FLOOR, int(v) // 32 * 32)) for k, v in level.get("floor", {}).items()}
    ramp_set = {tuple(map(int, k.split(","))): int(v) % 4 for k, v in level.get("ramp", {}).items()}
    for d in doors:   # a door's cells share one floor (its record has one)
        base = floor_set.get(door_cells(d)[1], 0)
        for c in door_cells(d):
            floor_set[c] = base
            ramp_set.pop(c, None)
    opens = [(x, z) for z in range(H) for x in range(W) if grid[z][x] == "."] or [(W // 2, H // 2)]
    box = (min(p[0] for p in opens), max(p[0] for p in opens), min(p[1] for p in opens), max(p[1] for p in opens))
    cx, cz = (box[0] + box[1] + 1) / 2, (box[2] + box[3] + 1) / 2
    rx, rz = max(4, (box[1] - box[0]) / 2 + 2), max(4, (box[3] - box[2]) / 2 + 2)

    def dome(px, pz):
        dx, dz = (px - cx) / rx, (pz - cz) / rz
        h = 2368 + 1088 * max(0.0, 1 - (dx * dx + dz * dz)) + 160 * noise(px * 1.7, pz * 1.7, 5)
        return int(round(min(3456, max(2240, h)) / 32)) * 32

    def floor_at(x, z, px, pz):
        """The cell's floor at corner (px, pz): its height, raised along a ramp (WorldMovement.cpp floorHeight)."""
        base = floor_set.get((x, z), FLOOR)
        r = ramp_set.get((x, z))
        if r is None:
            return base
        rise = (pz - z, px - x, x + 1 - px, z + 1 - pz)[r]   # 0 up toward +z, 1 +x, 2 -x, 3 -z
        return base + RAMP_RISE * rise

    def ceil(x, z, px, pz):
        t = grid[z][x]
        base = floor_set.get((x, z), FLOOR)
        top = base + (RAMP_RISE if (x, z) in ramp_set else 0)
        if t == "D":
            return base + DOOR_CEIL
        if (x, z) in ceil_set:
            value = ceil_set[(x, z)]
        else:
            value = base + (ROOM_CEIL if t == "R" else dome(px, pz))
        return min(MAX_CEIL, max(value, top + MIN_HEADROOM))

    verts, vindex, faces, cell_face = [], {}, [], {}

    def v(px, y, pz):
        key = (px * 512 - W * 256, y, pz * 512 - H * 256)
        if key not in vindex:
            vindex[key] = len(verts)
            verts.append(key)
        return vindex[key]

    L_FLOOR, L_WALL, L_HIGH = LIGHT_FLOOR, LIGHT_WALL, LIGHT_HIGH
    meta = []
    hive_bands = level.get("hive_bands") or ts.bands
    wall_bands = level.get("wall_bands") or ts.bands
    room_wall = level.get("room_wall") or ts.wall_textures[:2]
    for z in range(H):
        for x in range(W):
            t = grid[z][x]
            if t not in ".RD":
                continue
            cell_face[(x, z)] = len(faces)
            own = tex_set.get((x, z), [None, None, None])
            c00, c01, c11, c10 = (x, z), (x, z + 1), (x + 1, z + 1), (x + 1, z)
            if own[0] is not None:
                ftex, fflags = own[0], 0
            elif t == ".":
                ftex, fflags = ts.sample(x, z, box, False)
            else:
                ftex, fflags = level.get("room_floor", ts.floor_textures[0]), 0
            faces.append(((v(x, floor_at(x, z, *c00), z), v(x, floor_at(x, z, *c01), z + 1), v(x + 1, floor_at(x, z, *c11), z + 1),
                           v(x + 1, floor_at(x, z, *c10), z)), ftex, fflags, L_FLOOR))
            meta.append((x, z, 0))
            if own[2] is not None:
                ctex, cflags = own[2], 0
            elif t == ".":
                ctex, cflags = ts.sample(x, z, box, True)
            else:
                ctex, cflags = level.get("room_ceiling", ts.ceil_textures[0]), 0
            faces.append(((v(x, ceil(x, z, *c01), z + 1), v(x, ceil(x, z, *c00), z), v(x + 1, ceil(x, z, *c10), z),
                           v(x + 1, ceil(x, z, *c11), z + 1)), ctex, cflags, 128 | L_HIGH))
            meta.append((x, z, 1))
            for side, (nx, nz, a, b) in enumerate(((x - 1, z, c00, c01), (x + 1, z, c11, c10), (x, z - 1, c10, c00), (x, z + 1, c01, c11))):
                top_a, top_b = ceil(x, z, *a), ceil(x, z, *b)
                floor_a, floor_b = floor_at(x, z, *a), floor_at(x, z, *b)
                spans = []   # (bottom_a, bottom_b, top_a, top_b): wall this cell sees on that side
                if is_open(nx, nz):
                    # Below the neighbour's floor (a step up) and above its ceiling (a lower ceiling).
                    nf_a, nf_b = floor_at(nx, nz, *a), floor_at(nx, nz, *b)
                    if nf_a > floor_a or nf_b > floor_b:
                        spans.append((floor_a, floor_b, max(floor_a, min(nf_a, top_a)), max(floor_b, min(nf_b, top_b))))
                    low_a, low_b = ceil(nx, nz, *a), ceil(nx, nz, *b)
                    if low_a < top_a or low_b < top_b:
                        spans.append((max(floor_a, min(low_a, top_a)), max(floor_b, min(low_b, top_b)), top_a, top_b))
                else:
                    spans.append((floor_a, floor_b, top_a, top_b))
                hive = 0 <= nx < W and 0 <= nz < H and grid[nz][nx] == "W"
                side_tex = own[3 + side] if len(own) > 3 + side else None
                if side_tex is not None:
                    bands = [[side_tex]] * 8
                elif own[1] is not None:
                    bands = [[own[1]]] * 8
                elif hive:
                    bands = hive_bands
                elif t == "R":
                    bands = [room_wall] * 8
                else:
                    bands = wall_bands
                for bottom_a, bottom_b, top_a_, top_b_ in spans:
                    band = min(bottom_a, bottom_b) // 512
                    while True:
                        y0, y1 = band * 512, band * 512 + 512
                        ya0, yb0 = max(bottom_a, min(y0, top_a_)), max(bottom_b, min(y0, top_b_))
                        ya1, yb1 = max(bottom_a, min(y1, top_a_)), max(bottom_b, min(y1, top_b_))
                        if ya1 > ya0 or yb1 > yb0:
                            tex = pick(bands[min(band, len(bands) - 1)], x, z, nx, nz, band)
                            faces.append(((v(a[0], ya0, a[1]), v(b[0], yb0, b[1]), v(b[0], yb1, b[1]), v(a[0], ya1, a[1])),
                                          tex, 0, L_WALL if band < 3 else 128 | L_HIGH))
                            meta.append((x, z, 2 + side))
                        if y1 >= max(top_a_, top_b_):
                            break
                        band += 1
    return {"W": W, "H": H, "grid": grid, "doors": doors, "verts": verts, "faces": faces, "meta": meta,
            "cell_face": cell_face, "ceil": ceil, "floor_at": floor_at, "floor_set": floor_set, "ramp_set": ramp_set,
            "box": box, "cx": cx, "cz": cz}


def _tables(level, ts, g):
    """build(): the MAP0 bytes and level.txt around geometry()'s faces."""
    W, H, grid, doors, verts, faces = g["W"], g["H"], g["grid"], g["doors"], g["verts"], g["faces"]
    cell_face, ceil, floor_set, ramp_set, box, cx, cz = (g["cell_face"], g["ceil"], g["floor_set"], g["ramp_set"],
                                                         g["box"], g["cx"], g["cz"])
    bright = BRIGHTNESS.get(level.get("brightness", "normal"), 1.0)
    L_FLOOR, L_WALL, L_HIGH = LIGHT_FLOOR, LIGHT_WALL, LIGHT_HIGH

    # Monster records. Survival: the queen (if placed) first, then the spare records the waves rewrite - all held
    # back by the game. Exterminate: every placed enemy, there from the start.
    mode = level.get("mode", "survival")
    records = []

    def record(kind, x, z, facing=None, hold=False):
        r = list(ENEMIES[kind][1])
        r[1], r[2] = x, z
        if facing is not None:
            r[4] = facing
        r[6] = 255 if kind != 7 else r[6]
        r[7], r[8] = 0, 1 if hold else 0
        return bytes(r)

    queen = next((e for e in level.get("enemies", []) if e["kind"] == 7), None)
    if mode == "survival":
        if queen:
            records.append(record(7, queen["x"], queen["z"], 4, hold=True))
        while len(records) < 1 + SPARE_RECORDS:
            records.append(record(6, level["start"][0], level["start"][1], hold=True))
    else:
        for e in level.get("enemies", []):
            records.append(record(e["kind"], e["x"], e["z"], (e["x"] * 7 + e["z"] * 3) % 8))
    pickups = []
    for p in level.get("pickups", []):
        name, amount, reserve = PICKUPS[p["kind"]]
        pickups.append(bytes([p["x"], p["z"], p["kind"], min(255, p.get("amount", amount)), min(255, p.get("reserve", reserve)), 0, 0, 0]))
    door_records = []
    spawn = {id(d) for d, _, _ in spawn_doors(level)}
    for d in doors:
        cells = door_cells(d)
        rx_, rz_ = cells[1]
        rotation = 4 if d["axis"] == "x" else 2
        lock = 1 if id(d) in spawn else 0   # spawn doors: the game opens them; others: automatic
        door_records.append(bytes([rx_, rz_, 0, 150, lock, floor_set.get(cells[1], FLOOR) // 32, rotation, 1 if any(t == "D001" for t, _ in ts.meshes) else 0]))

    start = level["start"]
    out = bytearray(struct.pack("<HHHHHHBBHHHHHHHBBHI", len(verts), len(faces) + 1, W, H, start[0], start[1], 1,
                                ts.light_count, len(records), len(pickups), 0, len(door_records), 0,
                                level.get("start_dir", 4), 0, 0, 0, 0, 67))
    for x, y, z in verts:
        out += struct.pack("<hhhH", x, y, z, 0)
    for idx, tex, flags, light in faces:
        out += struct.pack("<iiiiHBB", *idx, tex, flags, light)
    out += struct.pack("<iiiiHBB", -1, -1, -1, -1, 0xFFFF, 255, 255)
    for z in range(H):
        for x in range(W):
            t = grid[z][x]
            if t in ".RD":
                corners = [(x, z), (x, z + 1), (x + 1, z), (x + 1, z + 1)]
                top = min(ceil(x, z, px, pz) for px, pz in corners)
                base, kind = floor_set.get((x, z), FLOOR), 6 if t == "D" else 0
                if (x, z) in ramp_set:
                    # Ramp cell types 0x31-0x34; 0x32 and 0x34 descend from the cell's height, so it is the top.
                    r = ramp_set[(x, z)]
                    kind = (0x31, 0x33, 0x32, 0x34)[r]
                    if r >= 2:
                        base += RAMP_RISE
                out += struct.pack("<i", cell_face[(x, z)]) + bytes([0, 0, 0, 0, 1, 0, kind, base // 32,
                                                                      0, min(255, top // 32), 10, 0])
            else:
                out += struct.pack("<i", -1) + bytes([0, 0, 0, 0, 1, 0, 20, 0, 0, 0, 0, 0])
    out += bytes([int(cx), int(cz), 0, 0, 255, 255, 255, 255])
    for r in records:
        out += r
    for p in pickups:
        out += p
    for r in door_records:
        out += r
    lights = bytearray(ts.lights)
    for index, rgb in ((L_FLOOR, (34, 40, 30)), (L_WALL, (40, 35, 28)), (L_HIGH, (22, 22, 20))):
        if index < ts.light_count:
            lights[index * 28:index * 28 + 3] = bytes(min(255, int(c * bright + (bright - 1) * 40)) for c in rgb)
    out += lights
    data = bytes(out)
    validate(data)
    return data, config(level, grid, box), {"vertices": len(verts), "faces": len(faces)}


def validate(data):
    """The checks LevelExplorer.cpp's load() makes."""
    w = lambda o: struct.unpack("<H", data[o:o + 2])[0]
    vc, fc, width, height = w(0), w(2), w(4), w(6)
    assert fc and vc <= 32767 and fc <= 32767 and 0 < width <= 127 and 0 < height <= 127
    end = (36 + vc * 8 + fc * 20 + width * height * 16 + data[12] * 8 + w(14) * 20 + w(16) * 8 + w(18) * 16 +
           w(20) * 8 + w(22) * 16 + data[28] * 4 + data[29] * 4 + data[13] * 28 + w(26) * 4 + w(30) * 4)
    assert end == len(data) and len(data) <= 0x130000, (end, len(data))


def config(level, grid, box):
    """level.txt: what the game needs beyond the map (src/xenon_survival.h reads it)."""
    W, H = level["width"], level["height"]
    lines = [f"name={level.get('name', 'Custom level')}", f"mode={level.get('mode', 'survival')}",
             f"lives={int(level.get('lives', 5))}", "weapons=" + ",".join(str(w) for w in level.get("weapons", [0])),
             f"waves={int(level.get('waves', 10))}", f"roster={level.get('roster', 'campaign')}",
             f"queen={1 if any(e['kind'] == 7 for e in level.get('enemies', [])) and level.get('queen_last_wave', True) else 0}",
             f"start={level['start'][0]},{level['start'][1]}", f"size={W},{H}"]
    for d, room, step in spawn_doors(level):
        cells = door_cells(d)
        lines.append(f"door={cells[1][0]},{cells[1][1]},{step[0]},{step[1]}," + ";".join(f"{x}:{z}" for x, z in room))
    # Ammunition spots: open cells with open neighbours, spread over the floor; the supply point beside the start.
    spots, taken = [], []
    cells = [(x, z) for z in range(2, H - 2) for x in range(2, W - 2)
             if all(grid[z + j][x + i] == "." for i in (-1, 0, 1) for j in (-1, 0, 1))]
    sx, sz = level["start"]
    cells.sort(key=lambda c: (noise(c[0], c[1], 9), c))
    for c in cells:
        if all(abs(c[0] - t[0]) + abs(c[1] - t[1]) >= 7 for t in taken) and abs(c[0] - sx) + abs(c[1] - sz) >= 5:
            taken.append(c)
        if len(taken) >= 12:
            break
    if level.get("ammo_spots"):
        taken = [tuple(c) for c in level["ammo_spots"]]
    lines.append("ammo=" + ";".join(f"{x}:{z}" for x, z in taken))
    supply = [(x, z) for x, z in sorted(((sx + dx, sz + dz) for dz in range(-3, 4) for dx in range(-3, 4)),
              key=lambda c: abs(c[0] - sx) + abs(c[1] - sz) + (0 if c[1] > sz else 0.5))
              if (x, z) != (sx, sz) and 0 < x < W - 1 and 0 < z < H - 1 and grid[z][x] == "."][:8]
    lines.append("supply=" + ";".join(f"{x}:{z}" for x, z in supply))
    # Fixed medikit spots and per-weapon reserve spots (the PC Survival's balance; optional).
    for key, field in (("health_spots", "health"), ("reserve_spots", "reserve_ammo")):
        if level.get(key):
            lines.append(f"{field}=" + ";".join(f"{x}:{z}" for x, z in level[key]))
    return "\n".join(lines) + "\n"


def write(level, folder, texset=None):
    ts = texset or TextureSet(level["code"])
    data, cfg, stats = build(level, ts)
    os.makedirs(folder, exist_ok=True)
    body = ts.form
    for tag, payload in [("MAP0", data)] + ts.meshes:
        body += tag.encode("latin1") + struct.pack(">I", len(payload)) + payload + (b"\0" if len(payload) & 1 else b"")
    for name in os.listdir(folder):   # one level per folder
        if name.upper().endswith("LEV.MAP") or name.upper().endswith("GFX.BIN"):
            os.remove(os.path.join(folder, name))
    open(os.path.join(folder, f"L{level['code']}LEV.MAP"), "wb").write(b"FORM" + struct.pack(">I", len(body)) + body)
    shutil.copyfile(ts.gfx_path, os.path.join(folder, f"{level['code']}GFX.BIN"))
    open(os.path.join(folder, "level.txt"), "w", newline="\n").write(cfg)
    return stats


def import_level(path):
    """A campaign level as an editable level dict."""
    raw = open(path, "rb").read()
    m = dict(chunks(raw))["MAP0"]
    w = lambda o: struct.unpack("<H", m[o:o + 2])[0]
    vc, fc, W, H = w(0), w(2), w(4), w(6)
    code = os.path.basename(path)[1:4]
    fb = 36 + vc * 8
    cb = fb + fc * 20
    wb = cb + W * H * 16
    mb = wb + m[12] * 8
    pb = mb + w(14) * 20
    db = pb + w(16) * 8 + w(18) * 16
    vert_y = lambda i: struct.unpack("<h", m[38 + i * 8:40 + i * 8])[0]
    grid, ceil, tex, floor, ramp = [], {}, {}, {}, {}
    for z in range(H):
        row = ""
        for x in range(W):
            c = m[cb + (z * W + x) * 16:cb + (z * W + x) * 16 + 16]
            ptr = struct.unpack("<i", c[0:4])[0]
            solid = 20 <= c[10] < 45 or c[13] - c[11] < 32 or ptr < 0 or x in (0, W - 1) or z in (0, H - 1)
            row += "#" if solid else "."
            if solid:
                continue
            base = c[11] * 32
            if 0x31 <= c[10] <= 0x38:
                r = {0x31: 0, 0x35: 0, 0x33: 1, 0x37: 1, 0x32: 2, 0x36: 2, 0x34: 3, 0x38: 3}[c[10]]
                ramp[f"{x},{z}"] = r
                if r >= 2:
                    base = max(0, base - RAMP_RISE)
            if base:
                floor[f"{x},{z}"] = min(MAX_FLOOR, base)
            ceil[f"{x},{z}"] = max(base + MIN_HEADROOM, min(MAX_CEIL, c[13] * 32))
            textures = [None, None, None]
            for fi in (ptr, ptr + 1):
                if fi >= fc - 1:
                    continue
                o = fb + fi * 20
                idx = [j for j in struct.unpack("<iiii", m[o:o + 16]) if j >= 0]
                ys = {vert_y(j) for j in idx}
                if len(ys) != 1 or m[o + 18] in SKIP_FLAGS:
                    continue
                textures[2 if ys.pop() > c[11] * 32 + 64 else 0] = w(o + 16)
            if textures != [None, None, None]:
                tex[f"{x},{z}"] = textures
        grid.append(row)
    rows = [list(r) for r in grid]
    doors = []
    for i in range(w(20)):
        r = m[db + i * 8:db + i * 8 + 8]
        axis = "z" if r[6] & 2 else "x"
        d = {"x": r[0] if axis == "z" else r[0] - 1, "z": r[1] - 1 if axis == "z" else r[1], "axis": axis}
        cells = door_cells(d)
        if all(1 <= cx < W - 1 and 1 <= cz < H - 1 for cx, cz in cells):
            for cx, cz in cells:
                rows[cz][cx] = "#"
            doors.append(d)
    enemies = []
    for i in range(w(14)):
        r = m[mb + i * 20:mb + i * 20 + 20]
        if r[0] in ENEMIES and rows[r[2]][r[1]] == ".":
            enemies.append({"kind": r[0], "x": r[1], "z": r[2]})
    pickups = []
    for i in range(w(16)):
        r = m[pb + i * 8:pb + i * 8 + 8]
        if r[2] in PICKUPS and r[6] == 0 and rows[r[1]][r[0]] == ".":
            pickups.append({"kind": r[2], "x": r[0], "z": r[1], "amount": r[3], "reserve": r[4]})
    start = [w(8), w(10)]
    if rows[start[1]][start[0]] != ".":
        start = next(([x, z] for z in range(H) for x in range(W) if rows[z][x] == "."), [1, 1])
    return {"name": f"{SETS.get(code, code)} (edited)", "code": code, "width": W, "height": H,
            "grid": ["".join(r) for r in rows], "doors": doors, "ceil": ceil, "tex": tex, "floor": floor, "ramp": ramp,
            "start": start,
            "start_dir": w(24) if w(24) < 8 else 4, "enemies": enemies, "pickups": pickups, "mode": "exterminate",
            "lives": 5, "weapons": [0, 1, 2], "waves": 10, "roster": "campaign", "queen_last_wave": True,
            "brightness": "normal"}
