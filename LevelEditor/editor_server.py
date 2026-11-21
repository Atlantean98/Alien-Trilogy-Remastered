"""Alien Trilogy level editor (3D): a small local server and a WebGL page in the browser.

Run "Level Editor.bat" (or: python editor_server.py). It serves the editor from web/ on 127.0.0.1 and opens it in
the default browser; close this window to quit. Needs Python 3 with Pillow and the game's data (GameData/PS1/CD).

The level itself is the same dict as before (Xbox360/tools/trilogy_level.py; project files .atlevel are it as
JSON). The page edits it; the geometry it draws comes from trilogy_level.geometry(), so what you see is what Export
writes.
"""
import json
import os
import socket
import struct
import sys
import threading
import traceback
import urllib.parse
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "Xbox360", "tools"))
import trilogy_level as tl  # noqa: E402

WEB = os.path.join(HERE, "web")
CACHE = os.path.join(HERE, "cache")
PROJECTS = os.path.join(tl.ROOT, "Custom Levels")
TILE = 64          # atlas tile size (each texture scaled to it)
ATLAS_COLS = 32

_sets = {}
_lock = threading.Lock()


def texset(code):
    with _lock:
        if code not in _sets:
            _sets[code] = tl.TextureSet(code)
        return _sets[code]


def atlas(code):
    """All the set's textures in one PNG (TILE x TILE each, ATLAS_COLS across), transparent texels alpha 0."""
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, f"atlas_{code}.png")
    ts = texset(code)
    if not os.path.exists(path) or os.path.getmtime(path) < os.path.getmtime(ts.gfx_path):
        pages = {}
        for number, (page, pal) in ts.pages.items():
            colours = []
            for i in range(len(pal[4:]) // 2):
                c = struct.unpack("<H", pal[4 + i * 2:6 + i * 2])[0]
                key = (c & 0x7FFF) == 0x7C1F
                colours.append((0, 0, 0, 0) if key else ((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31,
                                                        ((c >> 10) & 31) * 255 // 31, 255))
            img = Image.new("RGBA", (256, 256))
            if len(page) == 65536:
                img.putdata([colours[b] if b < len(colours) else (0, 0, 0, 0) for b in page])
            else:
                data = []
                for b in page[:32768]:
                    data.append(colours[b & 15] if (b & 15) < len(colours) else (0, 0, 0, 0))
                    data.append(colours[b >> 4] if (b >> 4) < len(colours) else (0, 0, 0, 0))
                img.putdata(data[:65536])
            pages[number] = img
        count = len(ts.rects)
        rows = (count + ATLAS_COLS - 1) // ATLAS_COLS
        out = Image.new("RGBA", (ATLAS_COLS * TILE, max(1, rows) * TILE))
        for i, (pg, x, y, w, h) in enumerate(ts.rects):
            tile = pages[pg].crop((x, y, x + w, y + h)).resize((TILE, TILE), Image.NEAREST)
            out.paste(tile, ((i % ATLAS_COLS) * TILE, (i // ATLAS_COLS) * TILE))
        out.save(path)
    return path


def set_info(code):
    ts = texset(code)
    return {"code": code, "count": len(ts.rects), "cols": ATLAS_COLS, "tile": TILE, "usable": ts.usable(),
            "floors": ts.floor_textures[:12], "walls": ts.wall_textures[:12], "ceilings": ts.ceil_textures[:12]}


def geometry(level):
    """The faces Export would write, flattened for the page: positions, texture, part, cell."""
    ts = texset(level["code"])
    g = tl.geometry(level, ts)
    verts = g["verts"]
    pos, tex, meta = [], [], []
    for (idx, t, flags, light), (x, z, part) in zip(g["faces"], g["meta"]):
        for i in idx:
            pos.extend(verts[i])
        tex.append(t)
        meta.extend((x, z, part))
    return {"pos": pos, "tex": tex, "meta": meta, "faces": len(g["faces"]), "vertices": len(verts),
            "offset": [level["width"] * 256, level["height"] * 256]}


def safe_name(name):
    keep = "".join(ch if ch.isalnum() or ch in " -_" else "_" for ch in name).strip()
    return keep or "Level"


def projects():
    os.makedirs(PROJECTS, exist_ok=True)
    out = []
    for name in sorted(os.listdir(PROJECTS)):
        if name.lower().endswith(".atlevel"):
            p = os.path.join(PROJECTS, name)
            out.append({"file": name, "modified": os.path.getmtime(p)})
    return out


def info():
    sets = [{"code": c, "name": tl.SETS[c]} for c in tl.available_sets()]
    campaign = [{"code": c, "name": tl.SETS[c]} for c in tl.SETS if tl.find_level(c) and c != "113"]
    return {"sets": sets, "campaign": campaign,
            "enemies": [{"kind": k, "name": v[0]} for k, v in tl.ENEMIES.items()],
            "pickups": [{"kind": k, "name": v[0]} for k, v in tl.PICKUPS.items()],
            "weapons": tl.WEAPONS, "projects": PROJECTS,
            "limits": {"step": tl.STEP_LIMIT, "ramp": tl.RAMP_RISE, "max_floor": tl.MAX_FLOOR, "max_ceil": tl.MAX_CEIL,
                       "headroom": tl.MIN_HEADROOM, "door": tl.DOOR_CEIL}}


def blank_level(width=40, height=32, code="162"):
    grid = ["".join("." if 3 <= x < width - 3 and 3 <= z < height - 3 else "#" for x in range(width)) for z in range(height)]
    return {"name": "My level", "code": code, "width": width, "height": height, "grid": grid, "doors": [],
            "ceil": {}, "tex": {}, "floor": {}, "ramp": {}, "start": [width // 2, height // 2], "start_dir": 4,
            "enemies": [], "pickups": [], "mode": "survival", "lives": 5, "weapons": [0], "waves": 10,
            "roster": "campaign", "queen_last_wave": True, "brightness": "normal"}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):   # quiet console
        pass

    def send(self, status, body, kind="application/json"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body, separators=(",", ":")).encode()
        elif isinstance(body, str):
            body = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def body(self):
        n = int(self.headers.get("Content-Length", 0))
        return json.loads(self.rfile.read(n) or b"{}")

    def do_GET(self):
        try:
            url = urllib.parse.urlparse(self.path)
            q = urllib.parse.parse_qs(url.query)
            p = url.path
            if p == "/" or p.startswith("/web/"):
                name = "index.html" if p == "/" else p[5:]
                full = os.path.normpath(os.path.join(WEB, name))
                if not full.startswith(WEB) or not os.path.isfile(full):
                    return self.send(404, {"error": "not found"})
                kind = {".html": "text/html", ".js": "text/javascript", ".css": "text/css"}.get(os.path.splitext(full)[1], "application/octet-stream")
                return self.send(200, open(full, "rb").read(), kind + "; charset=utf-8" if kind.startswith("text") else kind)
            if p == "/api/info":
                return self.send(200, info())
            if p == "/api/set":
                return self.send(200, set_info(q["code"][0]))
            if p == "/api/atlas":
                return self.send(200, open(atlas(q["code"][0]), "rb").read(), "image/png")
            if p == "/api/projects":
                return self.send(200, projects())
            if p == "/api/open":
                name = os.path.basename(q["file"][0])
                return self.send(200, json.load(open(os.path.join(PROJECTS, name), encoding="utf-8")))
            if p == "/api/import":
                return self.send(200, tl.import_level(tl.find_level(q["code"][0])))
            if p == "/api/new":
                if q.get("kind", [""])[0] == "arena":
                    import make_survival_map
                    level = make_survival_map.level()
                    level["name"] = "My arena"
                    return self.send(200, level)
                w, h = int(q.get("w", [40])[0]), int(q.get("h", [32])[0])
                return self.send(200, blank_level(max(16, min(127, w)), max(16, min(127, h)), q.get("code", ["162"])[0]))
            return self.send(404, {"error": "not found"})
        except Exception as error:   # noqa: BLE001 - reported to the page
            traceback.print_exc()
            return self.send(500, {"error": str(error)})

    def do_POST(self):
        try:
            p = urllib.parse.urlparse(self.path).path
            data = self.body()
            if p == "/api/geometry":
                return self.send(200, geometry(data))
            if p == "/api/shot":   # development: a frame the page rendered, saved to cache/shot.png
                import base64
                os.makedirs(CACHE, exist_ok=True)
                open(os.path.join(CACHE, "shot.png"), "wb").write(base64.b64decode(data["png"].split(",", 1)[1]))
                return self.send(200, {"ok": True})
            if p == "/api/check":
                return self.send(200, {"problems": tl.check(data)})
            if p == "/api/save":
                level, name = data["level"], safe_name(data.get("file") or data["level"].get("name", "Level"))
                if not name.lower().endswith(".atlevel"):
                    name += ".atlevel"
                os.makedirs(PROJECTS, exist_ok=True)
                path = os.path.join(PROJECTS, name)
                with open(path, "w", encoding="utf-8") as f:
                    json.dump(level, f, indent=1)
                return self.send(200, {"file": name, "path": path})
            if p == "/api/export":
                level = data["level"]
                problems = tl.check(level)
                if problems:
                    return self.send(200, {"problems": problems})
                folder = os.path.join(PROJECTS, safe_name(level.get("name", "Level")))
                stats = tl.write(level, folder, texset(level["code"]))
                return self.send(200, {"folder": folder, "files": sorted(os.listdir(folder)), **stats})
            return self.send(404, {"error": "not found"})
        except Exception as error:   # noqa: BLE001
            traceback.print_exc()
            return self.send(500, {"error": str(error)})


def free_port(preferred=8765):
    for port in [preferred] + list(range(preferred + 1, preferred + 40)):
        with socket.socket() as s:
            try:
                s.bind(("127.0.0.1", port))
                return port
            except OSError:
                continue
    return 0


def main():
    port = free_port()
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    url = f"http://127.0.0.1:{server.server_address[1]}/"
    print("Alien Trilogy Level Editor")
    print(f"  editor:   {url}")
    print(f"  projects: {PROJECTS}")
    print("Close this window to quit the editor.")
    if "--no-browser" not in sys.argv:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
