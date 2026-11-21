"""Stitch two campaign maps into one: the second map placed beside the first (mirrored left-right if that is what
lets them fit the engine's 127 x 127 cell grid without their rooms touching), joined by a corridor carved through
the solid rock from the first map's exit room to the room the second map starts in. The corridor slopes down ramp
by ramp (the second map's entrance is lower), lit and textured like the rooms it joins.

Everything the second map's tables point at is renumbered: vertices, faces, texture rectangles and pages (its
GFX pages go after the first's), animated-texture slots and scripts, lights (both maps' light records are pooled,
duplicates shared - a face has 7 bits of light index), waypoints, pickups, map objects, doors and their models
(D chunks), lifts and their models (L chunks), trigger actions and their command chains. The first map's exit
(logic op 8) becomes a no-op; the second map's exit is the stitched map's exit. Doors the second map's Queen
opened on her death (her record's command) are left unlocked - the Extermination builder replaces the enemies.

Used by tools/build_extermination_decks.py: stitch(first, second) -> (MAP0 bytes, other LEV.MAP chunks, GFX.BIN
bytes, info). The engine reads 128 actions, commands and pickup records (LevelLoading.cpp caps).
"""
from collections import deque
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "Xbox360" / "tools"))
import trilogy_level as tl

HEAD = "<HHHHHHBBHHHHHHHBBHI"
ANIMATED = {8, 10, 26, 28, 30, 32}      # a face whose texture field is an animation slot (LevelExplorer draw)
CLOSED = struct.pack("<i", -1) + bytes([0, 0, 0, 0, 1, 0, 20, 0, 0, 0, 0, 0])
RAMP = 128


def meaningful(c):
    """A cell with something in it: a floor face, or (with none - a lift's cells, the lift model being their floor) a
    height, trigger, type or flag; not the plain solid filler between rooms."""
    return (struct.unpack_from("<i", c, 0)[0] >= 0 or any(c[4:8]) or c[8] not in (0, 1) or c[9]
            or c[10] not in (0, 20) or any(c[11:16]))


class Level:
    """A MAP0 split into its tables (LevelLoading.cpp initializeLevelLayout), plus its models and GFX."""

    def __init__(self, code):
        self.code = code
        path = tl.find_level(code)
        chunks = tl.chunks(open(path, "rb").read())
        d = dict(chunks)["MAP0"]
        self.models = [(t, bytearray(p)) for t, p in chunks if t != "MAP0"]
        self.gfx = tl.chunks(open(Path(path).parent / f"{code}GFX.BIN", "rb").read())
        h = list(struct.unpack_from(HEAD, d, 0))
        self.head = h
        vc, fc, self.W, self.H = h[0], h[1], h[2], h[3]
        self.start = [h[4], h[5]]
        p = 36

        def take(n, size):
            nonlocal p
            out = [bytearray(d[p + i * size:p + i * size + size]) for i in range(n)]
            p += n * size
            return out
        self.verts = [list(struct.unpack_from("<hhhH", v)) for v in take(vc, 8)]
        faces = take(fc, 20)
        self.faces = [list(struct.unpack_from("<iiiiHBB", f)) for f in faces]
        assert self.faces[-1][:4] == [-1, -1, -1, -1], code
        self.faces.pop()                           # the terminator; written back once at the end
        self.cells = take(self.W * self.H, 16)
        self.waypoints = take(h[6], 8)
        self.monsters = take(h[8], 20)
        self.pickups = take(h[9], 8)
        self.objects = take(h[10], 16)
        self.doors = take(h[11], 8)
        self.lifts = take(h[12], 16)
        self.actions = take(h[15], 4)
        self.commands = take(h[16], 4)
        self.lights = take(h[7], 28)
        self.slots = take(h[14], 4)
        self.scripts = take(h[17], 4)
        assert p == len(d), (code, p, len(d))
        self.rects = sum(struct.unpack_from("<H", c, 0)[0] for t, c in self.gfx if t.startswith("BX"))
        self.pages = sorted(int(t[2:]) for t, _ in self.gfx if t.startswith("TP"))

    def cell(self, x, z):
        return self.cells[z * self.W + x]

    def is_open(self, x, z):
        return 0 <= x < self.W and 0 <= z < self.H and struct.unpack_from("<i", self.cell(x, z), 0)[0] >= 0

    def exit_cells(self):
        """Cells whose trigger runs a chain with the level exit (op 8)."""
        exits = set()
        for i, a in enumerate(self.actions):
            pc, seen = a[1], set()
            while pc != 255 and pc < len(self.commands) and pc not in seen:
                seen.add(pc)
                if self.commands[pc][0] == 8:
                    exits.add(i)
                pc = self.commands[pc][1]
        return [(x, z) for z in range(self.H) for x in range(self.W)
                if self.is_open(x, z) and self.cell(x, z)[15] in exits]


def mirror_mesh(faces):
    """Reverse each face's winding (a mirrored face would otherwise face away)."""
    for f in faces:
        idx = [i for i in f[:4] if i >= 0]
        idx.reverse()
        f[:4] = idx + [-1] * (4 - len(idx))


def mirror(L):
    """Mirror a level left-right (x -> W-1-x): geometry, cells, and every table with a position or facing."""
    W = L.W
    lift_floor = [L.cell(r[0], r[1])[11] for r in L.lifts]   # the game rests a lift at its corner cell's floor
    for v in L.verts:
        v[0] = -v[0]
    mirror_mesh(L.faces)
    # Cell attributes (WorldMovement.cpp): half steps 0x2e/0x2f and diagonal walls 0x19/0x1a, 0x1b/0x1c swap;
    # x ramps 0x32/0x33 (0x36/0x37) swap and, as 0x32 falls from its base where 0x33 rises, move base 128.
    swap = {0x2e: 0x2f, 0x2f: 0x2e, 0x19: 0x1a, 0x1a: 0x19, 0x1b: 0x1c, 0x1c: 0x1b}
    cells = []
    for z in range(L.H):
        row = [L.cell(W - 1 - x, z) for x in range(W)]
        for c in row:
            a = c[10]
            if a in swap:
                c[10] = swap[a]
            elif a in (0x32, 0x36):
                c[10], c[11] = a + 1, max(0, c[11] - 4)
            elif a in (0x33, 0x37):
                c[10], c[11] = a - 1, min(255, c[11] + 4)
        cells += row
    L.cells = cells
    for w in L.waypoints:   # its x is a cell edge (flag 1: the cell's middle)
        w[0] = W - 1 - w[0] if w[3] & 1 else W - w[0]
    for r in L.monsters:
        r[1] = W - 1 - r[1]
        if r[4] < 8:
            r[4] = (8 - r[4]) % 8
    for r in L.pickups:
        r[0] = W - 1 - r[0]
    for r in L.objects:   # a wall (21, 28, 29) covers byte 8 x byte 9 cells from its corner
        r[0] = W - r[0] - max(1, r[8])
        r[14] = (8 - r[14]) % 8
    for r in L.doors:   # four cells from x-1 (along x) or a column (along z)
        r[0] = W - 1 - r[0] if r[6] & 2 else W - 2 - r[0]
        r[6] = (8 - r[6]) % 8 if r[6] < 8 else r[6]
    for r in L.lifts:
        r[0] = W - r[0] - r[2]
    keep_lift_floors(L, lift_floor)
    L.start[0] = W - 1 - L.start[0]
    L.head[13] = (8 - L.head[13]) % 8 if L.head[13] < 8 else L.head[13]
    lift_width = {r[12]: r[2] for r in reversed(L.lifts)}   # a lift model sits on its first cell, reaching +x
    for i, (t, data) in enumerate(L.models):
        if not t[1:].isdigit() or data[:4] != b"OBJ1":
            continue
        shift = (lift_width.get(int(t[1:]), 1) - 1) * 512 if t[0] == "L" else 0
        fc, vc = struct.unpack_from("<II", data, 12)
        faces = [list(struct.unpack_from("<iiiiHBB", data, 20 + j * 20)) for j in range(fc)]
        mirror_mesh(faces)
        for j, f in enumerate(faces):
            struct.pack_into("<iiiiHBB", data, 20 + j * 20, *f)
        for j in range(vc):
            o = 20 + fc * 20 + j * 8
            struct.pack_into("<h", data, o, -struct.unpack_from("<h", data, o)[0] + shift)


def rotate(L):
    """Turn a level a quarter (cell (x, z) -> (H-1-z, x); world (X, Z) -> (-Z, X), so windings keep their side):
    geometry, cells and every table with a position, axis or facing. Facings and door/object rotations step by 2
    (an eighth each); lift models turn with the map (they are drawn unturned), door models turn by their rotation."""
    W, H = L.W, L.H
    lift_floor = [L.cell(r[0], r[1])[11] for r in L.lifts]   # the game rests a lift at its corner cell's floor
    for v in L.verts:
        v[0], v[2] = -v[2], v[0]
    # Cell attributes (WorldMovement.cpp): local lx' = 512 - lz, lz' = lx.
    turn = {0x2d: 0x2e, 0x2e: 0x30, 0x2f: 0x2d, 0x30: 0x2f, 0x19: 0x1a, 0x1a: 0x1c, 0x1b: 0x19, 0x1c: 0x1b}
    ramps = {0x31: (0x32, 4), 0x32: (0x34, 0), 0x33: (0x31, 0), 0x34: (0x33, -4),
             0x35: (0x36, 4), 0x36: (0x38, 0), 0x37: (0x35, 0), 0x38: (0x37, -4)}
    cells = []
    for z in range(W):            # new grid: W' = H, H' = W
        for x in range(H):
            c = L.cell(z, H - 1 - x)
            a = c[10]
            if a in turn:
                c[10] = turn[a]
            elif a in ramps:
                c[10], c[11] = ramps[a][0], min(255, max(0, c[11] + ramps[a][1]))   # (a ramp already at 0 stays)
            cells.append(c)
    L.cells = cells
    for w in L.waypoints:   # x at a cell edge, or its middle with flag 1 (z: flag 2)
        f1, f2 = w[3] & 1, w[3] & 2
        w[0], w[1] = (H - 1 - w[1]) if f2 else (H - w[1]), w[0]
        w[3] = (w[3] & ~3) | (1 if f2 else 0) | (2 if f1 else 0)
    for r in L.monsters:
        r[1], r[2] = H - 1 - r[2], r[1]
        if r[4] < 8:
            r[4] = (r[4] + 2) % 8
    for r in L.pickups:
        r[0], r[1] = H - 1 - r[1], r[0]
    for r in L.objects:   # walls: their rectangle turns too
        r[0], r[1] = H - r[1] - max(1, r[9]), r[0]
        r[8], r[9] = r[9], r[8]
        r[14] = (r[14] + 2) % 8
    for r in L.doors:   # along x (four cells from x-1) becomes along z, and the other way
        r[0], r[1] = (H - 2 - r[1]) if r[6] & 2 else (H - 1 - r[1]), r[0]
        r[6] = (r[6] + 2) % 8 if r[6] < 8 else r[6]
    lift_depth = {r[12]: r[3] for r in reversed(L.lifts)}   # its model sits on its first cell, reaching +x and +z
    for r in L.lifts:
        r[0], r[1], r[2], r[3] = H - r[1] - r[3], r[0], r[3], r[2]
    L.start = [H - 1 - L.start[1], L.start[0]]
    L.head[13] = (L.head[13] + 2) % 8 if L.head[13] < 8 else L.head[13]
    for i, (t, data) in enumerate(L.models):
        if t[0] != "L" or not t[1:].isdigit() or data[:4] != b"OBJ1":
            continue
        fc, vc = struct.unpack_from("<II", data, 12)
        shift = (lift_depth.get(int(t[1:]), 1) - 1) * 512
        for j in range(vc):
            o = 20 + fc * 20 + j * 8
            x, y, z = struct.unpack_from("<hhh", data, o)
            struct.pack_into("<hhh", data, o, -z + shift, y, x)
    L.W, L.H = H, W
    keep_lift_floors(L, lift_floor)


def keep_lift_floors(L, floors):
    """A lift's resting floor is its corner cell's (LevelLoading.cpp initializeLiftFloorHeights: low = that cell's
    floor, high = it plus the travel). Turned or mirrored, another of its cells is the corner: give all its cells the
    floor its old corner had, so it rests where the level meant (raised until its switch, say), not at the shaft's
    bottom."""
    for r, floor in zip(L.lifts, floors):
        for dz in range(r[3]):
            for dx in range(r[2]):
                L.cell(r[0] + dx, r[1] + dz)[11] = floor


def place(A, B, flip):
    """Where B goes (dx, dz from A's corner) and the stitched grid: no B room within a cell of an A room, the grid
    at most 127 a side, B's start as near A's exit as that allows. Returns (ax, az, bx, bz, W, H)."""
    oa = {(x, z) for z in range(A.H) for x in range(A.W) if A.is_open(x, z)}
    ob = [(B.W - 1 - x if flip else x, z) for z in range(B.H) for x in range(B.W) if B.is_open(x, z)]
    near = {(x + i, z + j) for x, z in oa for i in (-1, 0, 1) for j in (-1, 0, 1)}
    exit_ = A.exit_cells()
    ex = (sum(c[0] for c in exit_) / len(exit_), sum(c[1] for c in exit_) / len(exit_))
    sx = B.W - 1 - B.start[0] if flip else B.start[0]
    found = []
    for dx in range(-B.W - 2, A.W + 3):
        for dz in range(-B.H - 2, A.H + 3):
            W = max(A.W, dx + B.W) - min(0, dx)
            H = max(A.H, dz + B.H) - min(0, dz)
            if W > 127 or H > 127:
                continue
            if any((x + dx, z + dz) in near for x, z in ob):
                continue
            found.append((abs(sx + dx - ex[0]) + abs(B.start[1] + dz - ex[1]), dx, dz, W, H))
    found.sort()
    return [(-min(0, dx), -min(0, dz), -min(0, dx) + dx, -min(0, dz) + dz, W, H) for _, dx, dz, W, H in found]


def route(A, B, where, reach=3):
    """The corridor for a placement: (A room cell, path through rock, B room cell), or None. A path cell is rock
    with no room beside it (but, at its ends, the room it opens from); its ends open from a plain cell of A's exit
    room and of B's start room (no door, lift, stair or diagonal wall, room to stand)."""
    ax, az, bx, bz, W, H = where
    part = {}
    for z in range(A.H):
        for x in range(A.W):
            if A.is_open(x, z):
                part[(x + ax, z + az)] = A.cell(x, z)
    for z in range(B.H):
        for x in range(B.W):
            if B.is_open(x, z):
                part[(x + bx, z + bz)] = B.cell(x, z)
    avoid = ({(r[0] + k + ax, r[1] + az) if not r[6] & 2 else (r[0] + ax, r[1] + k + az) for r in A.doors for k in range(-1, 3)}
             | {(r[0] + k + bx, r[1] + bz) if not r[6] & 2 else (r[0] + bx, r[1] + k + bz) for r in B.doors for k in range(-1, 3)}
             | {(r[0] + i + ax, r[1] + j + az) for r in A.lifts for i in range(r[2]) for j in range(r[3])}
             | {(r[0] + i + bx, r[1] + j + bz) for r in B.lifts for i in range(r[2]) for j in range(r[3])})

    def flat_room(c):
        cell = part.get(c)
        return (cell is not None and c not in avoid and cell[10] == 0   # plain floor (20: a door's or a sealed cell)
                and cell[13] - cell[11] >= 36 and not cell[7] & 0x80)
    sides = ((1, 0), (-1, 0), (0, 1), (0, -1))

    def rock(c):   # rock, or a solid block of a map (attribute 20: walls all round, nothing walks there)
        return (1 <= c[0] < W - 1 and 1 <= c[1] < H - 1 and c not in avoid
                and (c not in part or (part[c][10] == 20 and not part[c][15])))

    def lonely(c, allowed):   # no room beside it (a cave wall can bulge into the next cell), blocks are fine
        return rock(c) and all(n not in part or n in allowed or part[n][10] == 20
                               for n in ((c[0] + 1, c[1]), (c[0] - 1, c[1]), (c[0], c[1] + 1), (c[0], c[1] - 1)))
    exit_room = {(x + ax, z + az) for x, z in A.exit_cells()}
    starts = {(x + bx, z + bz) for x in range(B.W) for z in range(B.H)
              if B.is_open(x, z) and abs(x - B.start[0]) + abs(z - B.start[1]) <= reach}
    ends_b = {(c[0] + dx, c[1] + dz): c for c in starts if flat_room(c) for dx, dz in sides if rock((c[0] + dx, c[1] + dz))}
    best = None
    for room_a in sorted(exit_room):
        if not flat_room(room_a):
            continue
        for dx, dz in sides:
            e = (room_a[0] + dx, room_a[1] + dz)
            if not lonely(e, {room_a}):
                continue
            prev = {e: None}
            todo = deque([e])
            while todo:
                c = todo.popleft()
                if c in ends_b and lonely(c, {ends_b[c]} | ({room_a} if c == e else set())):
                    path = [c]
                    while prev[path[-1]]:
                        path.append(prev[path[-1]])
                    path.reverse()
                    if not best or len(path) < len(best[1]):
                        best = (room_a, path, ends_b[c])
                    break
                for ddx, ddz in sides:
                    n = (c[0] + ddx, c[1] + ddz)
                    if n not in prev and (lonely(n, set()) or (n in ends_b and lonely(n, {ends_b[n]}))):
                        prev[n] = c
                        todo.append(n)
    return best


def stitch(first, second, flip=False, corridor=False, turns=0):
    """corridor False (Extermination's way): the maps share the grid but nothing joins them; the game moves the
    squad from the first map's exit elevator to the second map's start (both exits stay op 8). corridor True:
    a tunnel carved from the first map's exit room to the second map."""
    A, B = Level(first), Level(second)
    if flip:
        mirror(B)
    for _ in range(turns):
        rotate(B)
    placements = place(A, B, False)[:400]   # B is already mirrored: place it as it is
    if not placements:
        raise SystemExit(f"{first}+{second}: the maps do not fit one 127 x 127 grid")
    where, joined = placements[0], None
    # B's start room if a corridor can reach it, else whichever of B's rooms is nearest A's exit.
    for reach, w, j in ((r, w, route(A, B, w, r)) for r in (3, 10_000) for w in placements) if corridor else ():
        if j and (len(j[1]) <= 40 or reach > 3):   # a short walk, not a trek round the whole map
            where, joined = w, j
            break
    else:
        if corridor:
            raise SystemExit(f"{first}+{second}: no way between the maps")
    ax, az, bx, bz, W, H = where
    nA_v, nA_f = len(A.verts), len(A.faces)

    # B's tables renumbered after A's.
    a_groups = len([i for i, s in enumerate(A.scripts) if i == 0 or s[:2] != A.scripts[i - 1][:2]])
    a_dmodels = sum(1 for t, _ in A.models if t[0] == "D" and t[1:].isdigit())
    a_lmodels = sum(1 for t, _ in A.models if t[0] == "L" and t[1:].isdigit())

    # Lights: one pool, duplicates shared; a face keeps bit 7 of its light byte.
    pool, index = [], {}

    def light_of(rec):
        key = bytes(rec)
        if key not in index:
            index[key] = len(pool)
            pool.append(bytearray(rec))
        return index[key]
    a_light = [light_of(r) for r in A.lights]
    b_light = [light_of(r) for r in B.lights]
    if len(pool) > 128:
        raise SystemExit(f"{first}+{second}: {len(pool)} different lights (a face can name 128)")

    # Heights: B is raised or lowered so its entrance sits a whole number of ramps below A's exit; worked out with
    # the corridor below (shift in units of 32).
    verts = [[v[0] + (A.W * 256) + ax * 512 - W * 256, v[1], v[2] + (A.H * 256) + az * 512 - H * 256, v[3]] for v in A.verts]
    faces = [f[:4] + [f[4], f[5], (f[6] & 128) | a_light[f[6] & 127]] for f in A.faces]
    cells = [bytearray(CLOSED) for _ in range(W * H)]
    for z in range(A.H):
        for x in range(A.W):
            cells[(z + az) * W + x + ax] = bytearray(A.cell(x, z))
    part = {}   # cell -> 1 (first map), 2 (second map), 3 (corridor); lift cells (no floor face) count too
    for z in range(A.H):
        for x in range(A.W):
            if A.is_open(x, z) or meaningful(A.cell(x, z)):
                part[(x + ax, z + az)] = 1
    for z in range(B.H):
        for x in range(B.W):
            if B.is_open(x, z) or (meaningful(B.cell(x, z)) and (x + bx, z + bz) not in part):
                part[(x + bx, z + bz)] = 2

    # --- the corridor: from a cell of A's exit room through rock to a cell of B's start room ---
    def g_open(c):
        return c in part

    def plain_cell(c):
        cell = cells_of(c)
        return cell[10] in (0, 6, 10, 20) or cell[10] < 0x19

    def cells_of(c):
        x, z = c
        if part.get(c) == 2:
            return B.cell(x - bx, z - bz)
        return cells[z * W + x]
    a_doors = {(r[0] + k + ax, r[1] + az) if not r[6] & 2 else (r[0] + ax, r[1] + k + az) for r in A.doors for k in range(-1, 3)}
    b_doors = {(r[0] + k + bx, r[1] + bz) if not r[6] & 2 else (r[0] + bx, r[1] + k + bz) for r in B.doors for k in range(-1, 3)}
    a_lifts = {(r[0] + i + ax, r[1] + j + az) for r in A.lifts for i in range(r[2]) for j in range(r[3])}
    b_lifts = {(r[0] + i + bx, r[1] + j + bz) for r in B.lifts for i in range(r[2]) for j in range(r[3])}
    avoid = a_doors | b_doors | a_lifts | b_lifts

    def flat_room(c, which):
        cell = cells_of(c)
        return (part.get(c) == which and c not in avoid and cell[10] not in range(0x19, 0x1d)
                and cell[10] not in range(0x2d, 0x39) and cell[13] - cell[11] >= 36 and not cell[7] & 0x80)
    exit_room = {(x + ax, z + az) for x, z in A.exit_cells()}
    starts = {(x + bx, z + bz) for x in range(B.W) for z in range(B.H)
              if abs(x - B.start[0]) + abs(z - B.start[1]) <= 3}
    sides = ((1, 0), (-1, 0), (0, 1), (0, -1))

    def rock(c):
        return 1 <= c[0] < W - 1 and 1 <= c[1] < H - 1 and not g_open(c)

    room_a, path, room_b = joined or (None, [], None)

    # Straight runs take the ramps; the drop is a whole number of ramps (B moved up or down to make it so).
    def heading(i):
        return (path[i + 1][0] - path[i][0], path[i + 1][1] - path[i][1]) if i + 1 < len(path) else None
    straight = [i for i in range(1, len(path) - 1)
                if (path[i][0] - path[i - 1][0], path[i][1] - path[i - 1][1]) == heading(i)]
    fa = cells_of(room_a)[11] * 32 if corridor else 0
    fb = cells_of(room_b)[11] * 32 if corridor else 0
    b_floor = [B.cell(x, z)[11] for x in range(B.W) for z in range(B.H) if B.is_open(x, z)]
    b_ceil = [B.cell(x, z)[13] for x in range(B.W) for z in range(B.H) if B.is_open(x, z)]
    lo, hi = -min(b_floor) * 32, (255 - max(b_ceil)) * 32   # how far B can move and keep its heights in a byte
    ramps = min(len(straight), max(0, (fa - fb) // RAMP))
    shift = fa - RAMP * ramps - fb
    if not lo <= shift <= hi or shift % 32:
        raise SystemExit(f"{first}+{second}: cannot bring the second map to the corridor's height ({shift})")
    sy = shift // 32
    # Spread the ramps evenly over the straight cells.
    use = set(straight[round(k * len(straight) / ramps)] for k in range(ramps)) if ramps else set()
    while len(use) < ramps:
        use.add(next(i for i in straight if i not in use))

    # --- B joins A's tables ---
    for v in B.verts:
        verts.append([v[0] + (B.W * 256) + bx * 512 - W * 256, v[1] + shift, v[2] + (B.H * 256) + bz * 512 - H * 256, v[3]])
    for f in B.faces:
        idx = [i + nA_v if i >= 0 else -1 for i in f[:4]]
        tex = f[4]
        if f[5] in ANIMATED:
            tex += len(A.slots)
        elif tex != 0xFFFF:
            tex += A.rects
        faces.append(idx + [tex, f[5], (f[6] & 128) | b_light[f[6] & 127]])
    for z in range(B.H):
        for x in range(B.W):
            # B's rooms, and its cells with no floor face but data in them (its lifts' cells: heights, triggers, type),
            # never over a cell of A's
            if not B.is_open(x, z) and (not meaningful(B.cell(x, z)) or meaningful(cells[(z + bz) * W + x + bx])):
                continue
            c = bytearray(B.cell(x, z))
            if struct.unpack_from("<i", c, 0)[0] >= 0:
                struct.pack_into("<i", c, 0, struct.unpack_from("<i", c, 0)[0] + nA_f)
            c[6] = c[6] + len(A.waypoints)
            c[11] += sy
            c[13] += sy
            if c[15]:
                c[15] += len(A.actions)
            cells[(z + bz) * W + x + bx] = c
    for c in cells:
        assert 0 <= c[6] < 256
    waypoints = [bytearray(w) for w in A.waypoints]
    for wp in waypoints:
        wp[0] += ax
        wp[1] += az
    for w in B.waypoints:
        w = bytearray(w)
        w[0] += bx
        w[1] += bz
        for k in range(4, 8):
            if w[k] != 255:
                w[k] += len(A.waypoints)
        waypoints.append(w)
    monsters = []
    for r, (ox, oz) in [(r, (ax, az)) for r in A.monsters] + [(r, (bx, bz)) for r in B.monsters]:
        r = bytearray(r)
        r[1] += ox
        r[2] += oz
        monsters.append(r)
    pickups = []
    for r, (ox, oz) in [(r, (ax, az)) for r in A.pickups] + [(r, (bx, bz)) for r in B.pickups]:
        r = bytearray(r)
        r[0] += ox
        r[1] += oz
        pickups.append(r)
    objects = []
    for r in A.objects:
        r = bytearray(r)
        r[0] += ax
        r[1] += az
        objects.append(r)
    for r in B.objects:
        r = bytearray(r)
        r[0] += bx
        r[1] += bz
        if r[6] != 255 and not r[3] & 2:
            r[6] += len(A.pickups)
        elif r[6] != 255:
            r[6] += len(A.monsters)
        if r[7] != 255:
            r[7] += len(A.pickups)
        if r[2] in (21, 28, 29) and struct.unpack_from("<H", r, 12)[0]:   # the texture slots a blown wall changes
            struct.pack_into("<H", r, 10, struct.unpack_from("<H", r, 10)[0] + len(A.slots))
        objects.append(r)
    doors = []
    for r in A.doors:
        r = bytearray(r)
        r[0] += ax
        r[1] += az
        doors.append(r)
    # B's Queen opened doors on her death (her record's byte 6 is a command): with her gone they stand unlocked.
    queen_doors = set()
    for r in B.monsters:
        if r[0] == 7 and r[6] != 255:
            pc, seen = r[6], set()
            while pc != 255 and pc < len(B.commands) and pc not in seen:
                seen.add(pc)
                if B.commands[pc][0] in (1, 6):
                    queen_doors.add(B.commands[pc][3])
                pc = B.commands[pc][1]
    for i, r in enumerate(B.doors):
        r = bytearray(r)
        r[0] += bx
        r[1] += bz
        r[5] += sy
        if i in queen_doors:
            r[4] = 0
        doors.append(r)
    lifts = []
    for r in A.lifts:
        r = bytearray(r)
        r[0] += ax
        r[1] += az
        lifts.append(r)
    for r in B.lifts:
        r = bytearray(r)
        r[0] += bx
        r[1] += bz
        r[4] += sy
        r[5] += sy
        r[12] += a_lmodels
        lifts.append(r)
    # Door models: a door record's byte 7 names its D chunk.
    for r in doors[len(A.doors):]:
        r[7] += a_dmodels
    actions = [bytearray(a) for a in A.actions]
    for a in B.actions:
        a = bytearray(a)
        if a[1] != 255:
            a[1] += len(A.commands)
        actions.append(a)
    commands = []
    for c in A.commands:
        c = bytearray(c)
        if c[0] == 8 and corridor:
            c[0] = 255   # the first map's exit: the way on is the corridor now
        elif c[0] == 0 and c[3] < len(a_light):
            c[3] = a_light[c[3]]
        commands.append(c)
    remap = {0: None, 1: len(A.doors), 6: len(A.doors), 2: len(A.pickups), 3: len(A.monsters),
             4: len(A.objects), 5: len(A.lifts), 7: len(A.lifts), 9: len(A.slots)}
    for c in B.commands:
        c = bytearray(c)
        if c[1] != 255:
            c[1] += len(A.commands)
        if c[0] == 0 and c[3] < len(b_light):
            c[3] = b_light[c[3]]
        elif c[0] in remap and remap[c[0]] is not None and c[3] != 255:
            c[3] += remap[c[0]]
        commands.append(c)
    if max(len(actions), len(commands)) > 128 or len(pickups) > 128 or len(objects) > 128 or len(doors) > 64 or len(lifts) > 32:
        raise SystemExit(f"{first}+{second}: too many records for the engine")
    slots = [bytearray(s) for s in A.slots]
    for s in B.slots:
        s = bytearray(s)
        g = struct.unpack_from("<H", s, 0)[0]
        if g != 0xFFFF:
            struct.pack_into("<H", s, 0, g + a_groups)
        slots.append(s)
    scripts = [bytearray(s) for s in A.scripts]
    last = struct.unpack_from("<H", A.scripts[-1], 0)[0] if A.scripts else 0
    for s in B.scripts:   # a new group starts where the group word changes: keep B's apart from A's last
        s = bytearray(s)
        g, value = struct.unpack_from("<HH", s, 0)
        struct.pack_into("<H", s, 0, (g + last + 1) & 0xFFFF)
        if not value & 0x8000:
            struct.pack_into("<H", s, 2, value + A.rects)
        scripts.append(s)

    if corridor:
        # --- corridor geometry (trilogy_level.geometry's windings) ---
        def corners(x, z):
            return (x, z), (x, z + 1), (x + 1, z + 1), (x + 1, z)   # c00 c01 c11 c10

        heights = {}   # cell -> (floor base, attribute, {corner: floor})
        h = fa
        for i, c in enumerate(path):
            x, z = c
            c00, c01, c11, c10 = corners(x, z)
            if i in use:
                d = heading(i)
                if d == (0, 1):
                    attr, base, fl = 0x34, h, {c00: h, c10: h, c01: h - RAMP, c11: h - RAMP}
                elif d == (0, -1):
                    attr, base, fl = 0x31, h - RAMP, {c01: h, c11: h, c00: h - RAMP, c10: h - RAMP}
                elif d == (1, 0):
                    attr, base, fl = 0x32, h, {c00: h, c01: h, c10: h - RAMP, c11: h - RAMP}
                else:
                    attr, base, fl = 0x33, h - RAMP, {c10: h, c11: h, c00: h - RAMP, c01: h - RAMP}
                h -= RAMP
            else:
                attr, base, fl = 0, h, {k: h for k in (c00, c01, c11, c10)}
            heights[c] = (base, attr, fl)
        assert h == fb + shift, (h, fb, shift)
        ra, rb = cells_of(room_a), cells[room_b[1] * W + room_b[0]]
        room = {room_a: (ra[11] * 32, ra[13] * 32), room_b: (rb[11] * 32, rb[13] * 32)}
        clear = min(room[room_a][1] - room[room_a][0], room[room_b][1] - room[room_b][0], 1536)

        # The textures and lights of the walls the corridor breaks through, to dress it in.
        vx = lambda gx: gx * 512 - W * 256
        vz = lambda gz: gz * 512 - H * 256

        def edge_faces(c, n):
            """The walls between room cell c and corridor cell n: standing faces on that edge or bulging from it into n
            (a cave wall), within n's width."""
            x0, x1, z0, z1 = vx(n[0]) - 8, vx(n[0] + 1) + 8, vz(n[1]) - 8, vz(n[1] + 1) + 8
            out = []
            for i, f in enumerate(faces):
                idx = [k for k in f[:4] if k >= 0]
                if len({verts[k][1] for k in idx}) == 1:
                    continue   # floors and ceilings stay
                if all(x0 <= verts[k][0] <= x1 and z0 <= verts[k][2] <= z1 for k in idx):
                    out.append(i)
            return out
        holes = {}
        style = {}
        for end, rm in ((path[0], room_a), (path[-1], room_b)):
            found = edge_faces(rm, end)
            if not found:
                raise SystemExit(f"{first}+{second}: no wall to break at {rm}->{end}")
            f0 = faces[found[0]]
            holes[end] = found
            # floor and ceiling of the room cell it opens from
            floor_face = struct.unpack_from("<i", cells[rm[1] * W + rm[0]], 0)[0]
            fl_tex, fl_light = faces[floor_face][4], faces[floor_face][6]
            ce = [f for f in faces if len([k for k in f[:4] if k >= 0]) >= 3
                  and all(verts[k][1] == room[rm][1] for k in f[:4] if k >= 0)
                  and vx(rm[0]) <= min(verts[k][0] for k in f[:4] if k >= 0) and max(verts[k][0] for k in f[:4] if k >= 0) <= vx(rm[0] + 1)
                  and vz(rm[1]) <= min(verts[k][2] for k in f[:4] if k >= 0) and max(verts[k][2] for k in f[:4] if k >= 0) <= vz(rm[1] + 1)]
            ce_tex, ce_light = (ce[0][4], ce[0][6]) if ce else (fl_tex, fl_light)
            if faces[floor_face][5] in ANIMATED:
                fl_tex = f0[4]
            if ce and ce[0][5] in ANIMATED:
                ce_tex = f0[4]
            style[end] = (f0[4] if f0[5] not in ANIMATED else fl_tex, f0[6], fl_tex, fl_light, ce_tex, ce_light)
        for end, found in holes.items():   # the wall comes down (degenerate faces draw nothing)
            for i in found:
                faces[i][:4] = [faces[i][0], faces[i][0], faces[i][0], -1]

        vindex = {}

        def v(px, y, pz):
            key = (vx(px), y, vz(pz))
            if key not in vindex:
                vindex[key] = len(verts)
                verts.append([key[0], key[1], key[2], 0])
            return vindex[key]

        def floor_at(c, k):
            return heights[c][2][k] if c in heights else room[c][0]

        def ceil_at(c, k):
            return heights[c][2][k] + clear if c in heights else room[c][1]

        def wall(a, b, ya0, yb0, ya1, yb1, tex, light):
            y0 = min(ya0, yb0)
            band = y0 // 512
            while True:
                lo_, hi_ = band * 512, band * 512 + 512
                a0, b0 = max(ya0, min(lo_, ya1)), max(yb0, min(lo_, yb1))
                a1, b1 = max(ya0, min(hi_, ya1)), max(yb0, min(hi_, yb1))
                if a1 > a0 or b1 > b0:
                    faces.append([v(a[0], a0, a[1]), v(b[0], b0, b[1]), v(b[0], b1, b[1]), v(a[0], a1, a[1]), tex, 0, light])
                if hi_ >= max(ya1, yb1):
                    break
                band += 1
        first_face = len(faces)
        corridor_cells = {}
        half = len(path) / 2
        for i, c in enumerate(path):
            x, z = c
            wall_tex, wall_light, fl_tex, fl_light, ce_tex, ce_light = style[path[0] if i < half else path[-1]]
            c00, c01, c11, c10 = corners(x, z)
            floor_face = len(faces)
            faces.append([v(*c00[:1], floor_at(c, c00), c00[1]), v(c01[0], floor_at(c, c01), c01[1]),
                          v(c11[0], floor_at(c, c11), c11[1]), v(c10[0], floor_at(c, c10), c10[1]), fl_tex, 0, fl_light])
            faces.append([v(c01[0], ceil_at(c, c01), c01[1]), v(c00[0], ceil_at(c, c00), c00[1]),
                          v(c10[0], ceil_at(c, c10), c10[1]), v(c11[0], ceil_at(c, c11), c11[1]), ce_tex, 0, ce_light])
            for (nx, nz, a, b) in ((x - 1, z, c00, c01), (x + 1, z, c11, c10), (x, z - 1, c10, c00), (x, z + 1, c01, c11)):
                n = (nx, nz)
                fa_, fb_, ta, tb = floor_at(c, a), floor_at(c, b), ceil_at(c, a), ceil_at(c, b)
                if n in heights or n in room:
                    if (n == room_a and i == 0) or (n == room_b and i == len(path) - 1) or n in heights:
                        na, nb = floor_at(n, a), floor_at(n, b)
                        if na > fa_ or nb > fb_:
                            wall(a, b, fa_, fb_, max(fa_, min(na, ta)), max(fb_, min(nb, tb)), wall_tex, wall_light)
                        la, lb = ceil_at(n, a), ceil_at(n, b)
                        if la < ta or lb < tb:
                            wall(a, b, max(fa_, min(la, ta)), max(fb_, min(lb, tb)), ta, tb, wall_tex, wall_light)
                        if n in room and (la > ta or lb > tb):   # the room's higher wall above the corridor, seen from it
                            wall(b, a, tb, ta, lb, la, wall_tex, wall_light)
                        continue
                wall(a, b, fa_, fb_, ta, tb, wall_tex, wall_light)
            base, attr, fl = heights[c]
            rm = room_a if i < half else room_b
            cell = bytearray(cells[rm[1] * W + rm[0]])
            struct.pack_into("<i", cell, 0, floor_face)
            cell[5] = 0
            cell[7] &= 0x7f
            cell[10] = attr
            cell[11] = base // 32
            cell[12] = 0
            cell[13] = (min(fl.values()) + clear) // 32
            cell[15] = 0
            cells[z * W + x] = cell
            part[c] = 3
            corridor_cells[c] = i
    if len(verts) > 32767 or len(faces) + 1 > 32767:
        raise SystemExit(f"{first}+{second}: too many vertices or faces")

    # --- write ---
    head = list(A.head)
    start = (A.start[0] + ax, A.start[1] + az)
    head[0:6] = [len(verts), len(faces) + 1, W, H, start[0], start[1]]
    head[6], head[7] = len(waypoints), len(pool) if len(pool) < 256 else 0
    head[8:13] = [len(monsters), len(pickups), len(objects), len(doors), len(lifts)]
    head[14], head[15], head[16], head[17] = len(slots), len(actions), len(commands), len(scripts)
    out = bytearray(struct.pack(HEAD, *head))
    for x, y, z, w in verts:
        out += struct.pack("<hhhH", x, y, z, w)
    for f in faces:
        out += struct.pack("<iiiiHBB", *f)
    out += struct.pack("<iiiiHBB", -1, -1, -1, -1, 0xFFFF, 255, 255)
    for table in (cells, waypoints, monsters, pickups, objects, doors, lifts, actions, commands, pool, slots, scripts):
        for r in table:
            out += r
    # Models: A's, then B's renumbered after them, textures after A's rectangles.
    models = [(t, bytes(d)) for t, d in A.models]
    for t, d in B.models:
        d = bytearray(d)
        if t[1:].isdigit() and d[:4] == b"OBJ1":
            n = int(t[1:]) + (a_dmodels if t[0] == "D" else a_lmodels if t[0] == "L" else 0)
            fc = struct.unpack_from("<I", d, 12)[0]
            for j in range(fc):
                o = 20 + j * 20 + 16
                tex = struct.unpack_from("<H", d, o)[0]
                if tex != 0xFFFF and d[o + 2] not in ANIMATED:
                    struct.pack_into("<H", d, o, tex + A.rects)
            t = f"{t[0]}{n:03d}"
        models.append((t, bytes(d)))
    # GFX: A's pages, then B's renumbered after them.
    gfx = [(t, bytearray(d)) for t, d in A.gfx]
    page0 = A.pages[-1] + 1
    for t, d in B.gfx:
        if t[:2] in ("TP", "CL", "BX"):
            gfx.append((f"{t[:2]}{int(t[2:]) + page0:02d}", bytearray(d)))
    info = next(d for t, d in gfx if t == "INFO")
    struct.pack_into("<HH", info, 6, A.rects + B.rects, page0 + len(B.pages))
    raw = open(Path(tl.find_level(first)).parent / f"{first}GFX.BIN", "rb").read()
    body = raw[8:12]
    for t, d in gfx:
        body += t.encode("latin1") + struct.pack(">I", len(d)) + bytes(d) + (b"\0" if len(d) & 1 else b"")
    gfx_bytes = b"FORM" + struct.pack(">I", len(body)) + body
    info = {"first": {c for c, p in part.items() if p == 1}, "second": {c for c, p in part.items() if p == 2},
            "corridor": list(path), "second_start": (B.start[0] + bx, B.start[1] + bz), "grid": (W, H),
            "offsets": (ax, az, bx, bz), "shift": shift, "ramps": ramps, "lights": len(pool),
            "join": (room_a, room_b), "parts": part, "second_dir": B.head[13],
            "first_exit": [(x + ax, z + az) for x, z in A.exit_cells()]}
    return bytes(out), models, gfx_bytes, info


if __name__ == "__main__":
    for a, b, flip, turns in (("131", "154", False, 2), ("231", "262", True, 0)):
        data, models, gfx, info = stitch(a, b, flip, turns=turns)
        print(a, b, "grid", info["grid"], "corridor", len(info["corridor"]), "ramps", info["ramps"], "shift", info["shift"],
              "lights", info["lights"], "size", len(data), "join", info["join"])
