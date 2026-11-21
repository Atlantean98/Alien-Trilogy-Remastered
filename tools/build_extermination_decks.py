"""Build the Extermination decks by remixing the campaign's own maps, one per section.

Each deck keeps its source level's geometry, textures, doors, lifts, switch panels, crates, barrels, lights and
logic (so its rooms, transitions, puzzles and exit elevator are the real game's), and replaces:

  enemies   record 0 a Warrior, record 1 the Queen in deck 4, level 3's finale (held until the squad arrives),
            the three-marine squad by the start, then mixed Xenomorph packs over the reachable floor - more and
            tougher packs deck by deck
  pickups   the level's own list stays in place (crates and switches reveal entries of it by index) with its
            weapons turned into pulse rifle ammunition and its medikits into 40-health ones; each pack adds
            pulse ammunition and every second pack a medikit
  logic     "spawn enemy" commands (they named the old enemy records) do nothing; the level's exit (op 8, its
            exit elevator or door) is Extermination's extraction: the game allows it once the deck is cleared,
            every ID tag recovered and, on deck 3, the Queen dead
  ceilings  a few rooms get a lower ceiling (cells and the ceiling faces over them, with a sloped seam where a
            room opens onto a neighbour)

The crew ID tags go in the places hardest to reach (far from the start, behind doors and lifts), inside the level's
own destructible lockers and crates (a hidden pickup the container drops when shot open); only if it runs out of
those, in a prop of our own against a wall. Crates that spawned an enemy hold a Facehugger. On deck 3 the security
exit leads to deck 4 - Section 3's Queen's Lair, the Queen alone - for the finale.

objectives.txt: title; terminal x z, 255 255; then a line per tag "x z code": code 9 in a crate, 10 in a locker,
11 on the floor (a stitched deck's second map),
else (prop kind 0 box, 1 locker, 2 badge) | (wall side + 1) << 4, side 0 -z, 1 +z, 2 -x, 3 +x.

Writes GameData/Isolation/<n>/L<code>LEV.MAP, <code>GFX.BIN and objectives.txt.

Usage: python tools/build_extermination_decks.py
"""
from collections import deque
import math
from pathlib import Path
import shutil
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "Xbox360" / "tools"))
import trilogy_level as tl
sys.path.insert(0, str(ROOT / "tools"))
import stitch_maps

# number, source level, deck title, packs, tags, (second map, mirrored, quarter turns) or None: the two maps share one
# grid, and the far end of the first's exit elevator room is the start of the second's (LevelExplorer)
DECKS = [   # harder deck by deck: more packs, more tags; deck 3 ends with the Queen
    (1, "131", "Medical Laboratory - Atmosphere Sub-level", 18, 6, ("154", False, 2)),   # 1.3.1 (Level 4), then 1.5.4 turned round
    (2, "231", "Mining and Smelting - Lead Mould", 22, 7, ("262", True, 0)),        # 2.3.1, then 2.6.2 mirrored (Dog strain)
    (3, "381", "Egg Chambers", 20, 7, None),                                # Section 3 Level 5
    (4, "391", "Queen's Lair", 0, 0, None),   # level 3's finale (Section 3's Queen's Lair): loaded from Egg Chambers' exit
]
# Kinds: 1 egg, 2 Facehugger, 3 Chestburster, 4 Bambi dog, 5 Dog, 6 Warrior.
PACK_KINDS = {"nest": [1, 1, 1, 2], "hunters": [6, 6, 5], "brood": [3, 3, 2, 1],
              "pack": [5, 5, 4], "mixed": [6, 4, 3], "warband": [6, 6, 6, 5]}
# Section flavour: the colony is egg-laid and brood-heavy, the prison runs with Dogs, the derelict is Warriors.
# The second map's own flavour: the Queen's Lair is egg-laid, Lead Mould is the Dog strain's ground.
PACK_ORDER_SECOND = {
    1: ["nest", "brood", "hunters", "nest", "mixed"],
    2: ["pack", "pack", "hunters", "pack", "mixed"],
}
PACK_ORDER = {
    1: ["nest", "brood", "hunters", "mixed", "nest", "pack", "brood", "hunters"],
    2: ["pack", "hunters", "pack", "brood", "mixed", "pack", "nest", "hunters"],
    3: ["warband", "hunters", "brood", "warband", "mixed", "nest", "hunters", "warband"],
}
START_CLEAR = 14      # cells (path length) from the start without packs
SPAWN_GAP = 2         # spawn cells at least this far (Chebyshev) from each other and from objects, doors and lifts
STEP = 8              # the player climbs 256 (8 units of 32)
HEADROOM = 32         # 1024 between floor and ceiling to walk through
LOWERED_ROOMS = 3     # rooms per deck that get a lower ceiling
LOWERED_CLEARANCE = 1280
LOWERED_MOST = 768    # the most a ceiling comes down (its walls' textures squeeze by as much)


class Map0:
    """MAP0 split into its sections (offsets from the header counts, as TextureSet and the game read them)."""

    def __init__(self, data):
        self.d = bytearray(data)
        w = lambda o: struct.unpack_from("<H", self.d, o)[0]
        self.vc, self.fc, self.W, self.H = w(0), w(2), w(4), w(6)
        self.start = (w(8), w(10))
        self.cells_at = 36 + self.vc * 8 + self.fc * 20
        p = self.cells_at + self.W * self.H * 16
        self.path_at = p
        p += self.d[12] * 8
        self.monsters_at, self.monsters = p, w(14)
        p += self.monsters * 20
        self.pickups_at, self.pickups = p, w(16)
        p += self.pickups * 8
        self.objects_at, self.objects = p, w(18)
        p += self.objects * 16
        self.doors_at, self.doors = p, w(20)
        p += self.doors * 8
        self.lifts_at, self.lifts = p, w(22)
        p += self.lifts * 16
        self.actions_at = p
        p += self.d[28] * 4
        self.commands_at, self.commands = p, self.d[29]
        p += self.commands * 4
        self.tail_at = p

    def cell(self, x, z):
        o = self.cells_at + (z * self.W + x) * 16
        return self.d[o:o + 16]

    def is_open(self, x, z):
        return 0 <= x < self.W and 0 <= z < self.H and struct.unpack_from("<i", self.cell(x, z), 0)[0] >= 0

    def vertex(self, i):
        return list(struct.unpack_from("<hhh", self.d, 36 + i * 8))

    def set_vertex_y(self, i, y):
        struct.pack_into("<h", self.d, 36 + i * 8 + 2, y)

    def face_vertices(self, f):
        o = 36 + self.vc * 8 + f * 20
        return [v for v in struct.unpack_from("<iiii", self.d, o) if v >= 0]


def door_cells(m):
    cells = set()
    for i in range(m.doors):
        r = m.d[m.doors_at + i * 8:m.doors_at + i * 8 + 8]
        x, z, along_z = r[0], r[1], bool(r[6] & 2)
        for k in range(-1, 3):
            cells.add((x, z + k) if along_z else (x + k, z))
    return cells


def lift_cells(m):
    lifts = {}
    for i in range(m.lifts):
        r = m.d[m.lifts_at + i * 16:m.lifts_at + i * 16 + 16]
        for dz in range(r[3]):
            for dx in range(r[2]):
                lifts[(r[0] + dx, r[1] + dz)] = (r[4], r[5])
    return lifts


def object_cells(m):
    """Every cell a map object stands on: a blastable wall (21, 28, 29) covers byte 8 x byte 9 cells from its corner
    (an alien spawned in one would take over its cell and leave the wall walk-through)."""
    cells = set()
    for i in range(m.objects):
        r = m.d[m.objects_at + i * 16:m.objects_at + i * 16 + 16]
        size = (max(1, r[8]), max(1, r[9])) if r[2] in (21, 28, 29) else (1, 1)
        cells.update((r[0] + dx, r[1] + dz) for dz in range(size[1]) for dx in range(size[0]))
    return cells


def reachable(m, doors, lifts, start=None):
    """Cells the player can reach from the start: steps up to 256, any drop, 1024 headroom, through doors (the
    level's switches open them) and onto lifts at either height. Returns {cell: path length}."""
    def floors(c):
        return set(lifts[c]) if c in lifts else {m.cell(*c)[11]}

    def passable(c):   # as WorldMovement's collide: byte 7 bit 0x80 is solid (doors set it while closed)
        if not m.is_open(*c):
            return False
        cell = m.cell(*c)
        # Solid as the game makes it at load (LevelLoading.cpp resetLevelCollisionState): byte 7 bit 0x80, set for
        # cell types 1 and 20-24 (wall blocks, pillars) - the map file leaves it clear. Doors set 20 while closed.
        if (cell[7] & 0x80 or cell[10] == 1 or 20 <= cell[10] <= 24) and c not in doors:
            return False
        return c in lifts or c in doors or cell[13] - cell[11] >= HEADROOM   # a closed door's ceiling is at its floor

    def rise(c):   # ramps and half steps reach up to 128 above their base (floorHeight)
        return 4 if 0x2d <= m.cell(*c)[10] <= 0x38 else 0

    start = tuple(start or m.start)
    dist = {start: 0}
    todo = deque([(start, m.cell(*start)[11])])
    seen = {(start, m.cell(*start)[11])}
    while todo:
        c, f = todo.popleft()
        for n in ((c[0] + 1, c[1]), (c[0] - 1, c[1]), (c[0], c[1] + 1), (c[0], c[1] - 1)):
            if not passable(n):
                continue
            for nf in floors(n):
                if nf - (f + rise(c)) > STEP or (n, nf) in seen:
                    continue
                seen.add((n, nf))
                dist.setdefault(n, dist[c] + 1)
                todo.append((n, nf))
    return dist


def gated(m, dist, doors, lifts):
    """Cells whose shortest route from the start passes a door or a lift (a puzzle on the way)."""
    gate = {}
    order = sorted(dist, key=dist.get)
    for c in order:
        if dist[c] == 0:
            gate[c] = False
            continue
        prev = [n for n in ((c[0] + 1, c[1]), (c[0] - 1, c[1]), (c[0], c[1] + 1), (c[0], c[1] - 1))
                if dist.get(n) == dist[c] - 1]
        gate[c] = any(gate.get(p, False) for p in prev) or c in doors or c in lifts
    return gate


def remix(number, code, title, packs, tag_count, second=None):
    src = tl.find_level(code)
    raw = open(src, "rb").read()
    chunks = tl.chunks(raw)
    stitched = None
    if second:   # two maps in one grid (tools/stitch_maps.py); the first's exit elevator takes the squad to the second
        map0, models, gfx_bytes, stitched = stitch_maps.stitch(code, second[0], second[1], turns=second[2])
        chunks = [("MAP0", map0)] + models
    m = Map0(dict(chunks)["MAP0"])
    doors, lifts, objects = door_cells(m), lift_cells(m), object_cells(m)
    dist = reachable(m, doors, lifts)
    part2 = stitched["second"] if stitched else set()
    corridor = set(stitched["corridor"]) if stitched else set()
    if stitched:   # the second map's cells count on from the end of the first (the ride in between)
        first_end = max(dist.values())
        for c, d in reachable(m, doors, lifts, stitched["second_start"]).items():
            dist.setdefault(c, first_end + 1 + d)
        if sum(c in dist for c in part2) * 2 < len(part2):
            raise SystemExit(f"deck {number}: the second map's start cannot reach most of it")
    gate = gated(m, dist, doors, lifts)
    far_end = max(dist.values())

    def neighbours8(c, r=1):
        return [(c[0] + i, c[1] + j) for i in range(-r, r + 1) for j in range(-r, r + 1)]

    def roomy(c):   # a cell with open, reachable floor all round (not a doorway or corridor edge)
        return all(n in dist and n not in doors and n not in lifts for n in neighbours8(c))

    taken = set(doors) | set(lifts) | set(objects) | corridor
    for i in range(m.pickups):
        r = m.d[m.pickups_at + i * 8:m.pickups_at + i * 8 + 8]
        taken.add((r[0], r[1]))
    def free(c, gap=SPAWN_GAP):   # byte 15: the cell's floor trigger (0: none) - no spawns on switch cells
        return (c in dist and roomy(c) and m.cell(*c)[15] == 0
                and all(n not in taken for n in neighbours8(c, gap - 1)))

    # Squad by the start; record 0 a Warrior (the old stalker's slot); deck 3: the Queen's room at the far end.
    start = tuple(m.start)
    squad = sorted((c for c in dist if 1 <= dist[c] <= 10 and c not in taken and m.cell(*c)[15] == 0),
                   key=lambda c: (dist[c], c))[:3]
    if len(squad) < 3:
        raise SystemExit(f"deck {number}: no room for the squad by the start")
    taken.update(squad)
    taken.update(neighbours8(start))
    queen = terminal = None
    if number == 4:   # the finale: the Queen where the lair keeps her (released as the squad arrives), no terminal
        own = [(m.d[m.monsters_at + i * 20 + 1], m.d[m.monsters_at + i * 20 + 2]) for i in range(m.monsters)
               if m.d[m.monsters_at + i * 20] == 7]
        queen = next((c for c in own if c in dist), None) or max((c for c in dist if free(c, 3)), key=lambda c: (dist[c], c))
        taken.update(neighbours8(queen, 3))
    # Tags: the hardest places - far along, behind doors and lifts, spread out - each in a prop (crate, locker,
    # cabinet) standing flush against a plain stretch of wall, facing the room (the game draws it there,
    # LevelExplorer). The cell: plain level floor (no diagonal wall or stair in it) with room for a locker; a straight
    # wall behind it three cells wide (not a corner or a pillar's end); level floor in front and to both sides.
    def plain(c):
        return m.is_open(*c) and not (0x19 <= m.cell(*c)[10] <= 0x1c or 0x2d <= m.cell(*c)[10] <= 0x38)

    def against_wall(c):
        """The side of the wall behind c (0 -z, 1 +z, 2 -x, 3 +x), or None."""
        if c not in dist or c in taken or m.cell(*c)[15] or any(n in taken for n in neighbours8(c)):
            return None
        if not (2 <= c[0] < m.W - 2 and 2 <= c[1] < m.H - 2):   # the wall behind is the map's own, not its edge
            return None
        cell = m.cell(*c)
        if not plain(c) or cell[13] - cell[11] < 48:   # 1536 of headroom: a 1220 locker and some
            return None
        for side, (dx, dz) in enumerate(((0, -1), (0, 1), (-1, 0), (1, 0))):
            back, front = (c[0] + dx, c[1] + dz), (c[0] - dx, c[1] - dz)
            sides = [(c[0] + dz, c[1] + dx), (c[0] - dz, c[1] - dx)]
            behind = [back] + [(n[0] + dx, n[1] + dz) for n in sides]
            if any(m.is_open(*w) for w in behind):
                continue   # not a straight solid wall three cells wide
            if not all(n in dist and plain(n) and n not in doors and n not in lifts and m.cell(*n)[11] == cell[11]
                       for n in [front] + sides):
                continue
            if sum(n in dist for n in ((front[0] + 1, front[1]), (front[0] - 1, front[1]),
                                       (front[0], front[1] + 1), (front[0], front[1] - 1))) < 3:
                continue
            return side
        return None
    # First choice: the level's own destructible containers - its lockers (kind 25) and box crates (kind 20). The tag
    # is a hidden pickup record the container's free contents slot names (object record byte 7, always a pickup;
    # byte 6 when that is taken, unless byte 3 bit 2 makes byte 6 an enemy): shoot the container open and the tag
    # drops out, as the campaign's crates drop theirs (ExplorerObjects -> ExplorerPickups::crateDrop).
    objs = [bytearray(m.d[m.objects_at + i * 16:m.objects_at + i * 16 + 16]) for i in range(m.objects)]

    def slot(r):
        return 7 if r[7] == 255 else 6 if r[6] == 255 and not r[3] & 2 else None

    def score(c, chosen):
        return (dist[c] / far_end + (0.6 if gate[c] else 0) +
                min((math.dist(c, t) for t in chosen), default=40) / 40 + (0.15 if cand[c][1] == 25 else 0), c)
    cand = {}
    for i, r in enumerate(objs):
        c = (r[0], r[1])
        near = [dist[n] for n in ((c[0] + 1, c[1]), (c[0] - 1, c[1]), (c[0], c[1] + 1), (c[0], c[1] - 1)) if n in dist]
        # (not on a ramp or step: the game sets a crate at its cell's base height, half sunk into a slope)
        if (r[2] in (20, 25) and slot(r) is not None and near and min(near) + 1 >= START_CLEAR and c not in cand
                and not 0x2d <= m.cell(*c)[10] <= 0x38):
            dist.setdefault(c, min(near) + 1)
            gate.setdefault(c, any(gate.get(n, False) for n in ((c[0] + 1, c[1]), (c[0] - 1, c[1]),
                                                                 (c[0], c[1] + 1), (c[0], c[1] - 1))))
            cand[c] = (i, r[2])
    tags, hidden = [], []   # tags: (cell, container code); hidden: (object index, slot, cell)
    # Stitched: the first map's share first (from its own containers), then the rest of the deck.
    shares = ([((lambda c: c not in part2), tag_count // 2), ((lambda c: c in part2), tag_count)] if part2
              else [((lambda c: True), tag_count)])
    for spread, (inside, upto) in [(sp, sh) for sh in shares for sp in (4, 2)]:   # spread out; then any not beside one
        pool = [c for c in cand if inside(c) and c not in [t for t, _ in tags] and all(math.dist(c, t) >= spread for t, _ in tags)]
        while pool and len(tags) < upto:
            best = max(pool, key=lambda c: score(c, [t for t, _ in tags]))
            i, kind = cand[best]
            tags.append((best, 10 if kind == 25 else 9))
            hidden.append((i, slot(objs[i]), best))
            pool = [c for c in pool if math.dist(c, best) >= spread]
    # A stitched deck's second map short of containers (the Queen's Lair has none): tags dropped on the floor of
    # its far rooms - a visible ID tag pickup (code 11), picked up as the campaign's are.
    floor_tags = []
    if part2:   # each map its share (half each), what its containers could not hold lying on its floor
        second_quota = tag_count - tag_count // 2
        while sum(t in part2 for t, _ in tags) > second_quota:   # the second map's containers took more than its half
            k = max(i for i, (t, _) in enumerate(tags) if t in part2)
            hidden = [h for h in hidden if h[2] != tags[k][0]]
            del tags[k]
        for inside, quota in (((lambda c: c not in part2), tag_count // 2), ((lambda c: c in part2), tag_count - tag_count // 2)):
            have = sum(inside(t) for t, _ in tags)
            pool = [c for c in dist if inside(c) and free(c) and dist[c] >= START_CLEAR
                    and all(math.dist(c, t) >= 10 for t, _ in tags)]
            while pool and have < quota:
                best = max(pool, key=lambda c: (dist[c] / far_end + min((math.dist(c, t) for t, _ in tags), default=40) / 20, c))
                tags.append((best, 11))
                floor_tags.append(best)
                taken.update(neighbours8(best, 1))
                have += 1
                pool = [c for c in pool if math.dist(c, best) >= 10 and c not in taken]
    else:   # a single map short of usable containers: the rest on the floor of its far rooms too
        pool = [c for c in dist if free(c) and dist[c] >= START_CLEAR and all(math.dist(c, t) >= 10 for t, _ in tags)]
        while pool and len(tags) < tag_count:
            best = max(pool, key=lambda c: (dist[c] / far_end + min((math.dist(c, t) for t, _ in tags), default=40) / 20, c))
            tags.append((best, 11))
            floor_tags.append(best)
            taken.update(neighbours8(best, 1))
            pool = [c for c in pool if math.dist(c, best) >= 10 and c not in taken]
    # Only when the level runs out of containers and floor: a prop of our own against a plain stretch of wall.
    pool = [c for c in dist if against_wall(c) is not None and dist[c] >= START_CLEAR
            and all(math.dist(c, t) >= 8 for t, _ in tags)]
    while pool and len(tags) < tag_count:
        best = max(pool, key=lambda c: (dist[c] / far_end + (0.6 if gate[c] else 0) +
                                        min((math.dist(c, t) for t, _ in tags), default=40) / 40, c))
        tags.append((best, (len(tags) % 3) | ((against_wall(best) + 1) << 4)))
        taken.update(neighbours8(best, 1))
        pool = [c for c in pool if math.dist(c, best) >= 8]
    if len(tags) < tag_count:
        raise SystemExit(f"deck {number}: room for only {len(tags)} of {tag_count} ID tags")
    # Packs: farthest-point centres over the reachable floor, away from the start.
    order = PACK_ORDER.get(number, PACK_ORDER[3])
    aliens, supplies, centres = [], [], []
    for p in range(packs):
        name = order[p % len(order)]
        need = max(len(PACK_KINDS[n]) for n in PACK_KINDS) + 2 + (p % 2)
        members = []
        for gap in (SPAWN_GAP, 1):   # tight maps: then beside an object or another pack (never on one)
            pool = [c for c in dist if free(c, gap) and dist[c] >= START_CLEAR]
            pool.sort(key=lambda c: (-min((math.dist(c, k) for k in centres), default=dist[c]), c))
            for centre in pool:
                spots = sorted((c for c in neighbours8(centre, 3) if free(c, gap)), key=lambda c: (math.dist(c, centre), c))
                members = []
                for s in spots:
                    if all(max(abs(s[0] - a), abs(s[1] - b)) >= 2 for a, b in members):
                        members.append(s)
                    if len(members) == need:
                        break
                if len(members) == need:
                    break
            if len(members) == need:
                break
        else:
            raise SystemExit(f"deck {number}: no room for pack {p + 1} ({name})")
        if centre in part2:
            name = PACK_ORDER_SECOND[number][p % len(PACK_ORDER_SECOND[number])]
        members = members[:len(PACK_KINDS[name]) + 2 + (p % 2)]
        centres.append(centre)
        aliens += [(k, c) for k, c in zip(PACK_KINDS[name], members)]
        for spot in members[len(PACK_KINDS[name]):len(PACK_KINDS[name]) + 2]:
            supplies.append((11, spot, 99, 1))   # pulse rifle ammunition by every pack
        if p % 2:
            supplies.append((20, members[-1], 40, 0))
        taken.update(members)
    warrior = max((c for c in dist if free(c)), key=lambda c: (dist[c] if number != 3 else -abs(dist[c] - far_end // 2), c))
    taken.add(warrior)

    # Enemy records: 0 the Warrior, 1 the Queen (deck 3, held), the squad (kind 13), the packs.
    def record(kind, c, hold=False):
        r = list(tl.ENEMIES[kind][1])
        r[1], r[2] = c
        r[4] = (c[0] * 7 + c[1] * 3) % 8
        r[6] = 255 if kind != 7 else r[6]
        r[7], r[8] = 0, 1 if hold else 0
        return bytes(r)
    records = [record(6, warrior, hold=number == 4)]   # (the finale is the Queen alone)
    if queen:
        records.append(record(7, queen, hold=True))
    records += [record(13, c) for c in squad]
    records += [record(k, c) for k, c in aliens]
    for r in objs:   # a crate that spawned an enemy: now a Facehugger waiting inside (held until it breaks)
        if r[2] in (20, 25) and r[3] & 2 and r[6] != 255:
            if len(records) < 128 - 24:
                r[6] = len(records)
                records.append(record(2, (r[0], r[1]), hold=True))
            else:
                r[3] &= ~2 & 255
                r[6] = 255
    if len(records) > 128 - 24:   # room for eggs hatching under the 128-entity cap
        raise SystemExit(f"deck {number}: {len(records)} enemies leave no room for hatching")
    # Pickups: the level's own (weapons -> pulse ammo, medikits 40), then the packs' supplies.
    picks = bytearray(m.d[m.pickups_at:m.pickups_at + m.pickups * 8])
    dermpatches = 0
    for i in range(m.pickups):
        o = i * 8
        if picks[o + 2] in (0, 1, 2, 3, 4, 9, 10, 13, 14, 26, 27):   # weapons and the other guns' ammunition
            picks[o + 2], picks[o + 3], picks[o + 4] = 11, 99, 1
        elif picks[o + 2] == 15:   # the campaign's own ID tags: Extermination's tags are its own (they would count)
            picks[o + 2], picks[o + 3], picks[o + 4] = 11, 99, 1
        elif picks[o + 2] in (21, 24):   # every other DermPatch: a pulse rifle clip (there is plenty of both)
            dermpatches += 1
            if dermpatches % 2:
                picks[o + 2], picks[o + 3], picks[o + 4] = 11, 99, 1
        elif picks[o + 2] == 20:
            picks[o + 3] = 40
    room = 128 - len(picks) // 8 - len(hidden) - len(floor_tags)   # the engine reads 128 pickup records
    if len(supplies) > room:   # a big stitched deck: keep the medikits, drop some of the extra ammunition
        supplies.sort(key=lambda s: s[0] != 20)
        supplies = supplies[:max(0, room)]
    for kind, c, amount, reserve in supplies:
        picks += bytes([c[0], c[1], kind, amount, reserve, 0, 0, 0])
    for i, at, c in hidden:   # the tags: hidden until their container is broken (count 0 of 1, as crate contents)
        objs[i][at] = len(picks) // 8
        picks += bytes([c[0], c[1], 15, 1, 0, 0, 1, 0])
    for c in floor_tags:      # and lying out in the open
        picks += bytes([c[0], c[1], 15, 1, 0, 0, 0, 0])
    # Logic: spawn-enemy commands do nothing now (op 3 -> 255, the chain carries on).
    cmds = bytearray(m.d[m.commands_at:m.commands_at + m.commands * 4])
    for i in range(m.commands):
        if cmds[i * 4] == 3:
            cmds[i * 4] = 255
    # A switch that moves a lift can be used again (count 255; the game re-arms its panel): one use only, as in the
    # campaign, could leave a lift - and the squad's way back - stuck where the first press sent it.
    acts = bytearray(m.d[m.actions_at:m.commands_at])
    for i in range(len(acts) // 4):
        pc, seen, ops = acts[i * 4 + 1], set(), []
        while pc != 255 and pc < m.commands and pc not in seen:
            seen.add(pc)
            ops.append(cmds[pc * 4])
            pc = cmds[pc * 4 + 1]
        if 4 in ops and (5 in ops or 7 in ops):
            acts[i * 4 + 2] = 255
    # Lower ceilings: the largest high rooms away from doors, lifts and objects.
    lowered = lower_ceilings(m, dist, set(doors) | set(lifts) | corridor | (set(neighbours8(queen, 3)) if queen else set()))
    out = bytearray(m.d[:m.monsters_at]) + b"".join(records) + picks + b"".join(objs)
    out += m.d[m.doors_at:m.actions_at] + acts + cmds + m.d[m.tail_at:]
    struct.pack_into("<H", out, 14, len(records))
    struct.pack_into("<H", out, 16, len(picks) // 8)
    tl.validate(bytes(out)) if hasattr(tl, "validate") else None
    body = raw[8:12]
    for tag, payload in [("MAP0", bytes(out))] + [(t, d) for t, d in chunks if t != "MAP0"]:
        body += tag.encode("latin1") + struct.pack(">I", len(payload)) + payload + (b"\0" if len(payload) & 1 else b"")
    folder = ROOT / "GameData" / "Isolation" / str(number)
    folder.mkdir(parents=True, exist_ok=True)
    for old in folder.iterdir():
        if old.is_file():
            old.unlink()
    (folder / f"L{code}LEV.MAP").write_bytes(b"FORM" + struct.pack(">I", len(body)) + body)
    gfx, locker = deck_textures(code, gfx_bytes if stitched else open(Path(src).parent / f"{code}GFX.BIN", "rb").read())
    (folder / f"{code}GFX.BIN").write_bytes(gfx)
    (folder / "props.txt").write_text(" ".join(map(str, locker)) + "\n", encoding="ascii")
    if stitched:   # the game: which map each cell is in (parts.bin), and where the elevator ride arrives
        parts = bytearray(m.W * m.H)
        for c, p in stitched["parts"].items():
            parts[c[1] * m.W + c[0]] = p
        (folder / "parts.bin").write_bytes(bytes(parts))
        x, z = stitched["second_start"]
        # The way back: the floor beside the first map's exit room (off its trigger, so arriving there is not leaving
        # again), facing away from it.
        exit_room = set(stitched["first_exit"])
        cx = sum(c[0] for c in exit_room) / len(exit_room)
        cz = sum(c[1] for c in exit_room) / len(exit_room)
        around = [n for c in exit_room for n in ((c[0] + 1, c[1]), (c[0] - 1, c[1]), (c[0], c[1] + 1), (c[0], c[1] - 1))
                  if n not in exit_room and n in dist and n not in doors and n not in lifts and m.cell(*n)[15] == 0]
        if not around:
            raise SystemExit(f"deck {number}: no floor beside the first map's exit to come back to")
        back = min(around, key=lambda n: (math.dist(n, (cx, cz)), n))
        dx, dz = back[0] - cx, back[1] - cz
        away = (2 if dx > 0 else 6) if abs(dx) >= abs(dz) else (4 if dz > 0 else 0)

        def view(facing):   # open floor straight ahead (facing 0 -z, 2 +x, 4 +z, 6 -x), preferring away from the exit
            step = {0: (0, -1), 2: (1, 0), 4: (0, 1), 6: (-1, 0)}[facing]
            n, c = 0, back
            while n < 12:
                c = (c[0] + step[0], c[1] + step[1])
                if c not in dist or c in exit_room:
                    break
                n += 1
            return n, facing == away
        facing = max((0, 2, 4, 6), key=view)
        (folder / "stitch.txt").write_text(f"{x} {z} {stitched['second_dir']}\n{title.split(' - ')[-1]}\n"
                                           f"{back[0]} {back[1]} {facing}\n{title.split(' - ')[0]}\n", encoding="ascii")
    t = terminal or (255, 255)
    (folder / "objectives.txt").write_text(
        f"{title}\n{t[0]} {t[1]} 255 255\n" + "".join(f"{x} {z} {code}\n" for (x, z), code in tags),
        encoding="ascii")
    kinds = {}
    for k, _ in aliens:
        kinds[k] = kinds.get(k, 0) + 1
    print(f"deck {number} {title} (L{code}): {len(aliens)} aliens {dict(sorted(kinds.items()))}, {len(supplies)} supplies,"
          f" {len(tags)} tags ({len(hidden)} in containers, {sum(gate[t] for t, _ in tags)} behind doors/lifts), lowered {lowered} cells,"
          f" queen {queen}, terminal {terminal}, reachable {len(dist)} cells"
          + (f", stitched {second[0]} by a {len(corridor)}-cell corridor ({stitched['ramps']} ramps down),"
             f" {sum(t in part2 for t, _ in tags)} tags and {sum(c in part2 for c in centres)} packs in it" if stitched else ""))


# The tag lockers are textured from Recreation Rooms' (L122) locker panels: front 157, side 154, top 155, on its
# texture page 2 (rectangles 114-175). Another deck gets that page added to its own GFX.BIN (the game reads every
# TPnn/CLnn/BXnn page, rectangles in page order) and props.txt says where those three textures are in it.
LOCKER = (157, 154, 155)
LOCKER_PAGE, LOCKER_FIRST = 2, 114


def deck_textures(code, own):
    if code == "122":
        return own, LOCKER
    chunks = tl.chunks(own)
    pages = sorted(int(t[2:]) for t, _ in chunks if t.startswith("TP"))
    first = sum(struct.unpack("<H", d[:2])[0] for t, d in chunks if t.startswith("BX"))
    page = pages[-1] + 1
    src = dict(tl.chunks(open(Path(tl.find_level("122")).parent / "122GFX.BIN", "rb").read()))
    chunks = [(t, bytearray(d)) for t, d in chunks]
    for kind in ("TP", "CL", "BX"):
        chunks.append((f"{kind}{page:02d}", bytearray(src[f"{kind}{LOCKER_PAGE:02d}"])))
    info = next((d for t, d in chunks if t == "INFO"), None)
    if info is not None:
        added = struct.unpack("<H", src[f"BX{LOCKER_PAGE:02d}"][:2])[0]
        struct.pack_into("<HH", info, 6, first + added, page + 1)
    body = own[8:12]
    for t, d in chunks:
        body += t.encode("latin1") + struct.pack(">I", len(d)) + bytes(d) + (b"\0" if len(d) & 1 else b"")
    return b"FORM" + struct.pack(">I", len(body)) + body, tuple(first + i - LOCKER_FIRST for i in LOCKER)


def lower_ceilings(m, dist, avoid):
    """Give a few high rooms a low ceiling. A room: a connected patch of open cells of one ceiling height, each with
    one flat ceiling face at exactly that height. Its ceiling vertices come down, so only rooms where that moves
    nothing else are taken: no other flat face (a neighbour's ceiling, a water surface) shares a vertex with them,
    and every wall that does reaches from below the new ceiling (shortened) or from above the old one (lengthened) -
    no seams, spikes or dragged water. No doors, lifts, stairs or animated/translucent faces in the room."""
    faces, users = [], {}
    for f in range(m.fc):
        o = 36 + m.vc * 8 + f * 20
        vs = [v for v in struct.unpack_from("<iiii", m.d, o) if v >= 0]
        faces.append((vs, m.d[o + 18]))
        for v in vs:
            users.setdefault(v, []).append(f)

    def cell_of(vs):
        x = sum(m.vertex(v)[0] for v in vs) / len(vs) + m.W * 256
        z = sum(m.vertex(v)[2] for v in vs) / len(vs) + m.H * 256
        return int(x // 512), int(z // 512)
    ceiling_face = {}
    for f, (vs, flags) in enumerate(faces):
        if len(vs) < 3 or len({m.vertex(v)[1] for v in vs}) != 1:
            continue
        c = cell_of(vs)
        if m.is_open(*c) and m.vertex(vs[0])[1] == m.cell(*c)[13] * 32:
            ceiling_face.setdefault(c, []).append(f)

    def usable(c):
        a = m.cell(*c)
        return (c not in avoid and len(ceiling_face.get(c, ())) == 1 and not 0x2d <= a[10] <= 0x38
                and faces[ceiling_face[c][0]][1] not in tl.SKIP_FLAGS and a[13] - a[11] >= 48)
    left = {c for c in ceiling_face if usable(c)}
    rooms = []
    while left:
        seed = left.pop()
        room, todo = {seed}, [seed]
        while todo:
            c = todo.pop()
            for n in ((c[0] + 1, c[1]), (c[0] - 1, c[1]), (c[0], c[1] + 1), (c[0], c[1] - 1)):
                if n in left and m.cell(*n)[13] == m.cell(*seed)[13]:
                    left.remove(n)
                    room.add(n)
                    todo.append(n)
        if len(room) < 12 or sum(c in dist for c in room) * 2 < len(room):
            continue
        old = m.cell(*seed)[13] * 32
        new = max(m.cell(*c)[11] for c in room) * 32 + LOWERED_CLEARANCE
        own = {ceiling_face[c][0] for c in room}
        # The ceiling plane carries on over cells outside the room (one flat face sharing a vertex at the same
        # height): take those faces too, so the whole plane comes down together - unless one lies over a door, lift
        # or stair, or is animated or translucent.
        ok, grow = True, list(own)
        while grow and ok:
            for g in {g for v in faces[grow.pop()][0] for g in users[v]} - own:
                vs, flags = faces[g]
                ys = {m.vertex(v)[1] for v in vs}
                if ys != {old}:
                    continue
                c = cell_of(vs)
                if flags in tl.SKIP_FLAGS or c in avoid or not m.is_open(*c):
                    ok = False
                    break
                own.add(g)
                grow.append(g)
                rise = 128 if 0x2d <= m.cell(*c)[10] <= 0x38 else 0   # over a stair: clear its top step
                new = max(new, m.cell(*c)[11] * 32 + rise + LOWERED_CLEARANCE)
        verts = {v for f in own for v in faces[f][0]}
        for f in {g for v in verts for g in users[v]} - own if ok else ():
            vs, flags = faces[f]
            ys = [m.vertex(v)[1] for v in vs]
            if len(set(ys)) == 1 or flags in tl.SKIP_FLAGS:
                ok = False
                break
            # a wall (a window frame, a lower opening) reaching partway up: the new ceiling stays above its top
            new = max([new] + [y for v, y in zip(vs, ys) if v not in verts and y < old])
        room = room | {cell_of(faces[f][0]) for f in own}
        new = max(new, old - LOWERED_MOST)   # the walls up to it are squeezed: keep that modest
        if ok and new <= old - 256 and len(room) <= 320:
            rooms.append((len(room), seed, room, new, verts))
    rooms.sort(key=lambda r: (-r[0], r[1]))
    changed, used, picked = 0, set(), 0
    for _, _, room, new, verts in rooms:
        if picked == LOWERED_ROOMS or verts & used:
            continue   # another seed of a plane already lowered
        picked += 1
        used |= verts
        for v in verts:
            m.set_vertex_y(v, new)
        for c in room:
            m.d[m.cells_at + (c[1] * m.W + c[0]) * 16 + 13] = new // 32
        changed += len(room)
    return changed


if __name__ == "__main__":
    for deck in DECKS:
        remix(*deck)
