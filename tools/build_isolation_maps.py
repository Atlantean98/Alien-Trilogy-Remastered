"""Superseded (2026-11-15) by tools/build_extermination_decks.py, which remixes the campaign's own section maps.

Built the first, generated Extermination decks: editor projects and their native MAP exports."""
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "Xbox360" / "tools"))
import trilogy_level as editor

# Inclusive rooms, including the passages that connect them.
STAGES = [
    ("Signal Lost", [(2, 12, 8, 19), (8, 15, 17, 17), (14, 11, 20, 23),
                     (17, 5, 19, 11), (13, 2, 23, 6), (5, 3, 13, 5),
                     (4, 5, 6, 12), (20, 18, 29, 20), (26, 13, 34, 23)],
     (4, 17), (21, 4), (32, 20), (17, 20)),
    ("Emergency Power", [(3, 3, 10, 9), (7, 9, 9, 16), (3, 16, 14, 23),
                         (14, 19, 29, 21), (27, 13, 34, 23), (29, 6, 31, 13),
                         (18, 3, 34, 7), (18, 7, 20, 19), (10, 5, 18, 7)],
     (6, 5), (31, 20), (33, 5), (9, 21)),
    ("Last Shuttle", [(2, 9, 8, 16), (8, 11, 14, 13), (14, 5, 22, 19),
                      (18, 2, 20, 5), (13, 2, 20, 4), (22, 7, 30, 9),
                      (29, 5, 34, 15), (29, 15, 31, 21), (17, 21, 33, 24),
                      (17, 19, 19, 21), (22, 16, 29, 18)],
     (4, 12), (15, 3), (31, 23), (31, 9)),
]
PILLARS = (
    [(16, 13), (18, 19), (28, 16), (30, 18)],
    [(5, 19), (10, 19), (29, 16), (32, 18), (24, 5)],
    [(17, 9), (20, 12), (16, 16), (32, 12), (24, 22)],
)
TAGS = (
    [(5, 4), (18, 5), (16, 12), (29, 16), (33, 22)],
    [(4, 6), (5, 22), (17, 20), (31, 17), (21, 4), (33, 6)],
    [(5, 10), (16, 17), (16, 3), (27, 8), (33, 7), (19, 23), (32, 22)],
)
SQUAD = (
    [(5, 16), (6, 17), (5, 18)],
    [(7, 4), (8, 5), (7, 6)],
    [(5, 11), (6, 12), (5, 13)],
)
HUNTS = (
    [(17, 18), (19, 21), (20, 4), (29, 14), (32, 18), (24, 19)],
    [(5, 18), (11, 22), (21, 20), (31, 17), (24, 6), (32, 4), (30, 10)],
    [(7, 20), (17, 15), (21, 8), (26, 8), (31, 7), (32, 14), (22, 22), (29, 23)],
)
# Tile a consistent material across each deck. The generic editor's sampler
# projects the source map onto the new footprint and can split trim at cell edges.
DECK_MATERIALS = (
    (129, 21, [51, 35, 122, 21]),
    (26, 232, [51, 125, 122, 21]),
    (25, 198, [51, 35, 125, 21]),
)
ROOMS = (
    [((8, 7, 13, 11), (8, 10, 14, 10), "computer"),
     ((23, 7, 32, 12), (26, 12, 28, 13), "recreation"),
     ((10, 19, 13, 22), (12, 20, 14, 20), "lockers")],
    [((11, 9, 17, 14), (9, 11, 11, 11), "computer"),
     ((21, 8, 28, 15), (20, 10, 21, 11), "recreation"),
     ((21, 23, 25, 25), (22, 21, 23, 23), "lockers")],
    [((3, 17, 12, 23), (5, 16, 7, 17), "recreation"),
     ((24, 10, 28, 15), (26, 9, 27, 10), "computer"),
     ((23, 3, 26, 6), (24, 6, 25, 7), "lockers")],
)
ROOM_PALETTES = {
    "computer": (29, 4, [216, 140, 122, 21], (73, 122, 168)),
    "recreation": (46, 21, [51, 71, 73, 21], (160, 110, 72)),
    "lockers": (26, 115, [26, 157, 154, 21], (98, 147, 118)),
    "archives": (29, 4, [228, 140, 122, 21], (86, 128, 166)),
    "quarantine": (26, 115, [26, 157, 154, 21], (156, 106, 81)),
    "relay": (25, 4, [228, 140, 125, 21], (166, 124, 68)),
    "coolant": (29, 115, [51, 125, 122, 21], (76, 151, 151)),
    "departure": (46, 21, [51, 71, 73, 21], (148, 111, 76)),
    "security": (26, 4, [228, 140, 154, 21], (99, 123, 166)),
}
ANNEX_THEMES = (("archives", "quarantine"), ("relay", "coolant"),
                ("departure", "security"))
# Mixed alien packs on top of the HUNTS: one in each panel-locked wing first, the rest spread over the deck.
# Kinds: 1 egg, 2 Facehugger, 3 Chestburster, 4 Bambi dog, 5 Dog, 6 Warrior.
PACK_KINDS = {"nest": [1, 1, 1, 2], "hunters": [6, 6, 5], "brood": [3, 3, 2, 1],
              "pack": [5, 5, 4], "mixed": [6, 4, 3]}
PACKS = (   # Extermination is a bloodbath: packs in every room
    ["nest", "hunters", "brood", "pack", "mixed", "nest", "hunters", "pack", "mixed", "brood", "hunters"],
    ["hunters", "nest", "brood", "pack", "mixed", "hunters", "brood", "nest", "pack", "mixed", "hunters", "brood"],
    ["hunters", "brood", "nest", "mixed", "pack", "hunters", "nest", "brood", "mixed", "hunters", "pack", "brood",
     "hunters"],
)
PACK_START_DISTANCE = 16   # cells (Manhattan) between the squad's start and any pack
PACK_RADIUS = 3            # members within this many cells of their pack's centre
PACK_GAP = 2               # spawn cells at least this far (Chebyshev) from props, supplies, panels and each other


def place_packs(grid, packs, start, blocked, annexes):
    """Mixed packs on free floor: (aliens, supplies). One pack goes in each locked annex, then farthest-point
    centres over the rest of the deck. Each pack brings pulse rifle ammunition and every third one a medikit."""
    width, height = len(grid[0]), len(grid)
    taken = set(blocked)

    gap = PACK_GAP

    def clear(x, z):
        return (1 <= x < width - 1 and 1 <= z < height - 1 and grid[z][x] == "."
                and all((x + i, z + j) not in taken for i in range(-gap + 1, gap)
                        for j in range(-gap + 1, gap)))

    def far(x, z):
        return abs(x - start[0]) + abs(z - start[1]) >= PACK_START_DISTANCE

    floor = [(x, z) for z in range(height) for x in range(width) if grid[z][x] == "."]
    aliens, supplies, centres = [], [], []
    for number, name in enumerate(packs):
        # The locked wings are small and furnished: there a spawn may stand beside a prop (never on one).
        gap = 1 if number < len(annexes) else PACK_GAP
        if number < len(annexes):
            pool = [c for c in annexes[number] if clear(*c)]
            pool.sort(key=lambda c: -(abs(c[0] - start[0]) + abs(c[1] - start[1])))
        else:
            pool = [c for c in floor if clear(*c) and far(*c)]
            pool.sort(key=lambda c: -min((abs(c[0] - a) + abs(c[1] - b) for a, b in centres),
                                         default=abs(c[0] - start[0]) + abs(c[1] - start[1])))
        for centre in pool:
            spots = sorted((c for c in ((centre[0] + i, centre[1] + j)
                                        for i in range(-PACK_RADIUS, PACK_RADIUS + 1)
                                        for j in range(-PACK_RADIUS, PACK_RADIUS + 1)) if clear(*c)),
                           key=lambda c: abs(c[0] - centre[0]) + abs(c[1] - centre[1]))
            members = []
            for spot in spots:
                if all(max(abs(spot[0] - a), abs(spot[1] - b)) >= PACK_GAP for a, b in members):
                    members.append(spot)
                if len(members) == len(PACK_KINDS[name]) + 1 + (number % 3 == 2):
                    break
            if len(members) < len(PACK_KINDS[name]) + 1 + (number % 3 == 2):
                continue
            centres.append(centre)
            for kind, (x, z) in zip(PACK_KINDS[name], members):
                aliens.append(dict(kind=kind, x=x, z=z))
            ammo = members[len(PACK_KINDS[name])]
            supplies.append(dict(kind=11, x=ammo[0], z=ammo[1], amount=99, reserve=1))
            if number % 3 == 2:
                kit = members[-1]
                supplies.append(dict(kind=20, x=kit[0], z=kit[1], amount=25))
            taken.update(members)
            break
        else:
            raise ValueError(f"No room for the {name} pack")
    return aliens, supplies


def add_locked_annex(grid, start, size):
    """Find a free rectangle with one four-tile entrance off the existing deck."""
    width, height = len(grid[0]), len(grid)
    candidates = []
    for z in range(2, height-2):
        for x in range(2, width-2):
            for dx, dz in ((1,0), (-1,0), (0,1), (0,-1)):
                u, v = abs(dz), abs(dx)
                point = lambda forward, across: (x+dx*forward+u*across, z+dz*forward+v*across)
                interior = {point(f,a) for f in range(1,size+1) for a in range(-1,size-1)}
                gate = [point(0,a) for a in range(4)]
                perimeter = {point(f,a) for f in range(0,size+2) for a in range(-2,size)}
                if any(not (1<=a<width-1 and 1<=b<height-1) or grid[b][a]!="#" for a,b in perimeter):
                    continue
                if not any(grid[b][a]=="." for a,b in (point(-1,i) for i in range(4))):
                    continue
                distance = abs(x-start[0])+abs(z-start[1])
                candidates.append((distance, interior, gate, point(size-2, size-3)))
    if not candidates:
        raise ValueError("No unused space for a locked Isolation wing")
    _, interior, gate, tag = max(candidates,key=lambda item:item[0])
    for x,z in interior:
        grid[z][x]="."
    x,z = gate[0]
    return dict(x=x,z=z,axis="x" if gate[1][0]!=x else "z"), interior, tag


def export_deck(level, textures, zones):
    problems = editor.check(level)
    if problems:
        raise ValueError("; ".join(problems))
    geometry = editor.geometry(level, textures)
    faces = geometry["faces"]
    for index, (x, z, part) in enumerate(geometry["meta"]):
        if (x,z) in zones and part >= 2:
            indices, _, flags, light = faces[index]
            band = min(3, min(geometry["verts"][v][1] for v in indices)//512)
            faces[index] = (indices, ROOM_PALETTES[zones[x,z]][2][band], flags, light)
    data, _, stats = editor._tables(level, textures, geometry)
    data = bytearray(data)
    v,f,w,h = struct.unpack_from("<4H",data)
    records = 36+v*8+f*20+w*h*16+data[12]*8
    records += struct.unpack_from("<H",data,14)[0]*20 + struct.unpack_from("<H",data,16)[0]*8
    for index in range(len(level["doors"])):
        data[records+index*8+4] = 1
    light_count = data[13]
    banks = {}
    for name, (_, _, _, rgb) in ROOM_PALETTES.items():
        banks[name] = light_count + len(banks)
        data.extend(bytes([*rgb, 0]) + bytes(24))
    data[13] = light_count + len(banks)
    faces_at = 36 + 8*len(geometry["verts"])
    for index, (x, z, _) in enumerate(geometry["meta"]):
        if (x,z) in zones:
            at = faces_at+20*index+19
            data[at] = (data[at] & 128) | banks[zones[x,z]]
    editor.validate(data)
    return data, stats


def main():
    textures = editor.TextureSet("122")
    for index, (name, rooms, start, terminal, exit_cell, stalker) in enumerate(STAGES, 1):
        width, height = 74, 54
        grid = [["#"] * width for _ in range(height)]
        for left, bottom, right, top in rooms:
            for z in range(2*bottom, 2*(top + 1)):
                for x in range(2*left, 2*(right + 1)):
                    grid[z][x] = "."
        for x, z in PILLARS[index-1]:
            for dz in range(2):
                for dx in range(2):
                    grid[2*z+dz][2*x+dx] = "#"
        zones, fixtures = {}, []
        for room, passage, theme in ROOMS[index-1]:
            for left, bottom, right, top in (room, passage):
                for z in range(2*bottom, 2*(top+1)):
                    for x in range(2*left, 2*(right+1)):
                        grid[z][x] = "."
                        zones[x,z] = theme
            left, bottom, right, top = room
            fixture = {"lockers": 4, "computer": 5, "recreation": 6}[theme]
            fixtures.extend((x,z,fixture)
                            for z in (2*bottom+1, 2*top)
                            for x in range(2*left+2, 2*right, 3))

        def cell(point):
            return [2*value+1 for value in point]

        tags = [cell(point) for point in TAGS[index-1]]
        entry = cell(start)
        occupied = set(tuple(tag) for tag in tags) | {tuple(cell(start)),tuple(cell(terminal)),tuple(cell(exit_cell))}
        occupied.update((x,z) for x,z,_ in fixtures)
        panel = max((point for point, theme in zones.items()
                     if theme=="computer" and point not in occupied
                     and all(abs(point[0]-a)+abs(point[1]-b)>2 for a,b in occupied)),
                    key=lambda point:abs(point[0]-entry[0])+abs(point[1]-entry[1]))
        doors, panels, annexes = [], [], []
        for number, (size, theme) in enumerate(zip((6,7), ANNEX_THEMES[index-1])):
            door, interior, tag = add_locked_annex(grid, panel, size)
            doors.append(door)
            annexes.append(sorted(interior))
            panels.append((*panel, 7+number))
            tags.append(tag)
            zones.update((point,theme) for point in interior)
            panel=min((point for point in interior if point!=tag and
                       abs(point[0]-tag[0])+abs(point[1]-tag[1])>2),
                      key=lambda point:abs(point[0]-entry[0])+abs(point[1]-entry[1]))
            # Furniture hugs the perimeter, leaving a broad central route for
            # both the player and hunter. Keep consoles and search props clear.
            blocked = {tag, panel} | {(x,z) for x,z,_ in panels}
            fixtures_here = []
            for x,z in sorted(interior):
                neighbours = ((x-1,z),(x+1,z),(x,z-1),(x,z+1))
                if not any(grid[b][a]=="#" for a,b in neighbours):
                    continue
                if any(abs(x-a)+abs(z-b)<3 for a,b in blocked):
                    continue
                if abs(x-door["x"])+abs(z-door["z"])<5:
                    continue
                if any(abs(x-a)+abs(z-b)<3 for a,b in fixtures_here):
                    continue
                fixtures_here.append((x,z))
                kind = 6 if theme=="departure" else 4 if theme=="quarantine" else 5
                fixtures.append((x,z,kind))
        for x, z in tags:
            assert grid[z][x] == ".", (name, x, z)
        supplies = [dict(kind=11, x=x-1, z=z, amount=99, reserve=2) for x,z in tags]
        supplies.extend(dict(kind=20, x=x, z=z-1, amount=25) for x,z in tags[::2])
        start, terminal, exit_cell, stalker = map(cell, (start, terminal, exit_cell, stalker))
        level = dict(name=name, code="122", width=width, height=height, start=start,
                     mode="exterminate", lives=1, weapons=[2], doors=doors, pickups=supplies,
                     enemies=[dict(kind=6, x=stalker[0], z=stalker[1])],
                     grid=["".join(row) for row in grid],
                     ceil={f"{x},{z}": 1920 for z, row in enumerate(grid)
                           for x, cell in enumerate(row) if cell == "."})
        if index == 3:
            # The security terminal and Smartgun are in the room immediately
            # north of this chamber, connected by the short central passage.
            level["enemies"].append(dict(kind=7, x=37, z=21))
        for point in SQUAD[index-1]:
            x, z = cell(point)
            level["enemies"].append(dict(kind=13, x=x, z=z))
        for i, point in enumerate(HUNTS[index-1]):
            x,z = cell(point)
            assert grid[z][x] == ".", (name, point)
            level["enemies"].append(dict(kind=5 if i%2==0 else 6, x=x, z=z))
        # Mixed packs (after the Queen, which deck 3's terminal spawns as record 1, and the squad).
        blocked = {tuple(t) for t in tags} | {(x, z) for x, z, _ in fixtures + panels}
        blocked |= {(p["x"], p["z"]) for p in supplies} | {tuple(start), tuple(terminal), tuple(exit_cell)}
        blocked |= {(e["x"], e["z"]) for e in level["enemies"]}
        for door in doors:
            blocked.update(editor.door_cells(door))
        aliens, pack_supplies = place_packs(grid, PACKS[index-1], start, blocked, annexes)
        level["enemies"].extend(aliens)
        level["pickups"].extend(pack_supplies)
        floor, ceiling, walls = DECK_MATERIALS[index-1]
        level["tex"] = {f"{x},{z}": [floor, None, ceiling]
                        for z, row in enumerate(grid) for x, tile in enumerate(row) if tile == "."}
        level["wall_bands"] = [[wall] for wall in walls] + [[walls[-1]]] * 4
        for (x,z), theme in zones.items():
            floor, ceiling, _, _ = ROOM_PALETTES[theme]
            level["tex"][f"{x},{z}"] = [floor, None, ceiling]
        folder = ROOT / "GameData" / "Isolation" / str(index)
        folder.mkdir(parents=True, exist_ok=True)
        (folder / f"{name}.atlevel").write_text(json.dumps(level, indent=2), encoding="utf-8")
        data, stats = export_deck(level, textures, zones)
        body = textures.form
        for tag, payload in [("MAP0", data)] + textures.meshes:
            body += tag.encode("latin1") + struct.pack(">I", len(payload)) + payload
            if len(payload) % 2:
                body += b"\0"
        (folder / "L111LEV.MAP").write_bytes(b"FORM" + struct.pack(">I", len(body)) + body)
        (folder / "objectives.txt").write_text(
            f"{name}\n{terminal[0]} {terminal[1]} {exit_cell[0]} {exit_cell[1]}\n"
            + "".join(f"{x} {z} {i%3}\n" for i,(x,z) in enumerate(tags))
            + "".join(f"{x} {z} {kind}\n" for x,z,kind in fixtures+panels), encoding="ascii")
        print(name, stats)


if __name__ == "__main__":
    main()

