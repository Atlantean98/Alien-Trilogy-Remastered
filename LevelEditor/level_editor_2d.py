"""Alien Trilogy level editor: a standalone Windows program for building new levels, or reworking the campaign's,
in the game's own level format (tools/trilogy_level.py writes them).

Paint the map from above with the tools on the left, set the level up on the right, then Export: a folder with
L<code>LEV.MAP, <code>GFX.BIN and level.txt. 3D preview draws the level from the player start with the PC build's
renderer (decompilation/cpp/build/recovered_world_explorer.exe), if it is there.

Run: double-click "Level Editor (2D).bat" (or: python level_editor_2d.py). Needs Python 3 with Pillow and the game's data
(GameData/CD beside this folder). Project files (.atlevel) are the level as JSON.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog, ttk

from PIL import Image, ImageTk

# The level format code is shared with the Xbox 360 build's Survival arena (Xbox360/tools).
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Xbox360", "tools"))
import trilogy_level as tl  # noqa: E402

ROOT = tl.ROOT
PC_EXE = os.path.join(ROOT, "decompilation", "cpp", "build", "recovered_world_explorer.exe")
REPORTS = os.path.join(ROOT, "decompilation", "reports")
EXPORT_HOME = os.path.join(ROOT, "Custom Levels")

COLOURS = {"#": "#1c1a18", ".": "#3d5a3c", "R": "#55557a", "W": "#7a3d28", "D": "#c8a028"}
TOOLS = [   # (key, label, hint)
    ("floor", "Floor", "Paint open floor."),
    ("wall", "Wall", "Paint solid wall."),
    ("room", "Spawn room", "Survival: paint a room behind a door; the waves wait in it and pour out when it opens."),
    ("hive", "Hive wall", "A wall dressed in the hive's organic textures (good around a queen)."),
    ("door_x", "Door  ↔", "Click to place a 4-cell door running left-right. It must sit in a wall with floor on both sides."),
    ("door_z", "Door  ↕", "Click to place a 4-cell door running up-down."),
    ("start", "Player start", "Click to put the player here; click again to turn them."),
    ("enemy", "Enemy", "Click to place the enemy chosen below."),
    ("pickup", "Pickup", "Click to place the item chosen below."),
    ("ceiling", "Ceiling height", "Paint the ceiling height chosen below."),
    ("texture", "Texture", "Paint the texture picked on the right onto floor, walls or ceiling."),
    ("erase", "Eraser", "Removes doors, enemies, pickups, textures and heights (leaves floor and wall)."),
]
CEILINGS = [("Automatic (dome)", None), ("Low (1.5 m)", 1536), ("Normal (2 m)", 2048), ("High (2.6 m)", 2560),
            ("Very high (3.3 m)", 3328), ("Huge (4 m)", 4064)]
ROSTERS = [("Campaign ramp (Section 1 to 3)", "campaign"), ("Section 1 breeds", "section1"),
           ("Section 2 breeds", "section2"), ("Section 3 breeds", "section3")]
FACING = ["↓", "↘", "→", "↗", "↑", "↖", "←", "↙"]   # start_dir 0-7 drawn north-up


def blank_level(width=40, height=32, code="162"):
    grid = []
    for z in range(height):
        grid.append("".join("." if 3 <= x < width - 3 and 3 <= z < height - 3 else "#" for x in range(width)))
    return {"name": "My level", "code": code, "width": width, "height": height, "grid": grid, "doors": [],
            "ceil": {}, "tex": {}, "start": [width // 2, height // 2], "start_dir": 4, "enemies": [], "pickups": [],
            "mode": "survival", "lives": 5, "weapons": [0], "waves": 10, "roster": "campaign",
            "queen_last_wave": True, "brightness": "normal"}


class Editor(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Alien Trilogy Level Editor")
        self.geometry("1400x860")
        self.minsize(1100, 700)
        self.configure(bg="#202020")
        self.level = blank_level()
        self.path = None
        self.dirty = False
        self.undo_stack, self.redo_stack = [], []
        self.sets = {}
        self.cell = 16
        self.tool = tk.StringVar(value="floor")
        self.brush = tk.IntVar(value=1)
        self.enemy_kind = tk.StringVar(value=tl.ENEMIES[6][0])
        self.pickup_kind = tk.StringVar(value=tl.PICKUPS[11][0])
        self.ceiling_choice = tk.StringVar(value=CEILINGS[3][0])
        self.texture_target = tk.StringVar(value="floor")
        self.texture = None
        self.show_textures = tk.BooleanVar(value=False)
        self._thumbs = {}
        self._build_ui()
        self.load_level(self.level, None)
        self.protocol("WM_DELETE_WINDOW", self.quit_editor)

    # ---- UI ------------------------------------------------------------------------------------------------------
    def _build_ui(self):
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure(".", background="#2a2a2a", foreground="#e6e6e6", fieldbackground="#3a3a3a")
        style.configure("TButton", padding=6)
        style.configure("Tool.TRadiobutton", padding=6, font=("Segoe UI", 10))
        style.configure("Head.TLabel", font=("Segoe UI", 11, "bold"), foreground="#9be59b")
        style.map("TButton", background=[("active", "#3f5f3f")])
        style.map("TCombobox", fieldbackground=[("readonly", "#3a3a3a")], foreground=[("readonly", "#e6e6e6")],
                  selectbackground=[("readonly", "#3a3a3a")], selectforeground=[("readonly", "#e6e6e6")])
        style.configure("TSpinbox", arrowcolor="#e6e6e6")
        self.option_add("*TCombobox*Listbox.background", "#2a2a2a")
        self.option_add("*TCombobox*Listbox.foreground", "#e6e6e6")
        bar = ttk.Frame(self)
        bar.pack(side="top", fill="x")
        for text, cmd in [("New", self.new_level), ("Open", self.open_level), ("Edit a campaign level", self.import_campaign),
                          ("Save", self.save_level), ("Export level", self.export_level), ("3D preview", self.preview_3d),
                          ("Undo", self.undo), ("Redo", self.redo), ("Help", self.show_help)]:
            ttk.Button(bar, text=text, command=cmd).pack(side="left", padx=2, pady=4)
        ttk.Label(bar, text="  Zoom").pack(side="left")
        ttk.Button(bar, text="-", width=3, command=lambda: self.zoom(-4)).pack(side="left")
        ttk.Button(bar, text="+", width=3, command=lambda: self.zoom(4)).pack(side="left")
        ttk.Checkbutton(bar, text="Show textures", variable=self.show_textures, command=self.redraw).pack(side="left", padx=8)

        body = ttk.Frame(self)
        body.pack(fill="both", expand=True)
        left = ttk.Frame(body, width=230)
        left.pack(side="left", fill="y", padx=6, pady=6)
        ttk.Label(left, text="Tools", style="Head.TLabel").pack(anchor="w")
        for key, label, hint in TOOLS:
            ttk.Radiobutton(left, text=label, value=key, variable=self.tool, style="Tool.TRadiobutton",
                            command=self.tool_changed).pack(anchor="w", fill="x")
        ttk.Label(left, text="Brush size").pack(anchor="w", pady=(8, 0))
        sizes = ttk.Frame(left)
        sizes.pack(anchor="w")
        for n in (1, 3, 5):
            ttk.Radiobutton(sizes, text=str(n), value=n, variable=self.brush).pack(side="left")
        ttk.Label(left, text="Enemy").pack(anchor="w", pady=(8, 0))
        ttk.Combobox(left, textvariable=self.enemy_kind, state="readonly",
                     values=[v[0] for v in tl.ENEMIES.values()]).pack(fill="x")
        ttk.Label(left, text="Pickup").pack(anchor="w", pady=(6, 0))
        ttk.Combobox(left, textvariable=self.pickup_kind, state="readonly",
                     values=[v[0] for v in tl.PICKUPS.values()]).pack(fill="x")
        ttk.Label(left, text="Ceiling").pack(anchor="w", pady=(6, 0))
        ttk.Combobox(left, textvariable=self.ceiling_choice, state="readonly", values=[c[0] for c in CEILINGS]).pack(fill="x")
        self.hint = ttk.Label(left, text="", wraplength=215, foreground="#b0b0b0")
        self.hint.pack(anchor="w", pady=10)

        right = ttk.Frame(body, width=330)
        right.pack(side="right", fill="y", padx=6, pady=6)
        self._build_settings(right)

        centre = ttk.Frame(body)
        centre.pack(side="left", fill="both", expand=True)
        self.canvas = tk.Canvas(centre, bg="#101010", highlightthickness=0)
        xs = ttk.Scrollbar(centre, orient="horizontal", command=self.canvas.xview)
        ys = ttk.Scrollbar(centre, orient="vertical", command=self.canvas.yview)
        self.canvas.configure(xscrollcommand=xs.set, yscrollcommand=ys.set)
        ys.pack(side="right", fill="y")
        xs.pack(side="bottom", fill="x")
        self.canvas.pack(fill="both", expand=True)
        self.canvas.bind("<Button-1>", self.press)
        self.canvas.bind("<B1-Motion>", self.drag)
        self.canvas.bind("<ButtonRelease-1>", self.release)
        self.canvas.bind("<Button-3>", self.right_click)
        self.canvas.bind("<Motion>", self.hover)
        self.canvas.bind("<MouseWheel>", lambda e: self.canvas.yview_scroll(-1 if e.delta > 0 else 1, "units"))
        self.canvas.bind("<Control-MouseWheel>", lambda e: self.zoom(2 if e.delta > 0 else -2))
        self.bind_all("<Control-z>", lambda e: self.undo())
        self.bind_all("<Control-y>", lambda e: self.redo())
        self.bind_all("<Control-s>", lambda e: self.save_level())
        self.status = ttk.Label(self, text="", anchor="w")
        self.status.pack(side="bottom", fill="x")
        self.tool_changed()

    def _build_settings(self, panel):
        ttk.Label(panel, text="Level", style="Head.TLabel").pack(anchor="w")
        form = ttk.Frame(panel)
        form.pack(fill="x")
        self.v_name = tk.StringVar()
        self.v_set = tk.StringVar()
        self.v_mode = tk.StringVar()
        self.v_lives = tk.IntVar()
        self.v_waves = tk.IntVar()
        self.v_roster = tk.StringVar()
        self.v_queen = tk.BooleanVar()
        self.v_light = tk.StringVar()
        self.v_weapons = [tk.BooleanVar() for _ in tl.WEAPONS]
        rows = [("Name", ttk.Entry(form, textvariable=self.v_name)),
                ("Look", ttk.Combobox(form, textvariable=self.v_set, state="readonly",
                                      values=[f"{c}  {tl.SETS[c]}" for c in tl.available_sets()])),
                ("Mode", ttk.Combobox(form, textvariable=self.v_mode, state="readonly", values=["Survival", "Exterminate"])),
                ("Lives", ttk.Spinbox(form, from_=1, to=99, textvariable=self.v_lives, width=6)),
                ("Waves", ttk.Spinbox(form, from_=1, to=30, textvariable=self.v_waves, width=6)),
                ("Enemy mix", ttk.Combobox(form, textvariable=self.v_roster, state="readonly", values=[r[0] for r in ROSTERS])),
                ("Lighting", ttk.Combobox(form, textvariable=self.v_light, state="readonly", values=["Dark", "Normal", "Bright"]))]
        for i, (label, widget) in enumerate(rows):
            ttk.Label(form, text=label).grid(row=i, column=0, sticky="w", pady=2)
            widget.grid(row=i, column=1, sticky="ew", pady=2)
        form.columnconfigure(1, weight=1)
        ttk.Checkbutton(panel, text="Queen fights in the last wave (place her)", variable=self.v_queen,
                        command=self.settings_changed).pack(anchor="w", pady=(4, 0))
        ttk.Label(panel, text="Starting weapons").pack(anchor="w", pady=(6, 0))
        wf = ttk.Frame(panel)
        wf.pack(anchor="w")
        for i, name in enumerate(tl.WEAPONS):
            ttk.Checkbutton(wf, text=name, variable=self.v_weapons[i], command=self.settings_changed).grid(
                row=i // 2, column=i % 2, sticky="w")
        size = ttk.Frame(panel)
        size.pack(fill="x", pady=(6, 0))
        ttk.Label(size, text="Map size").pack(side="left")
        ttk.Button(size, text="Resize...", command=self.resize).pack(side="right")
        for var in (self.v_name, self.v_set, self.v_mode, self.v_lives, self.v_waves, self.v_roster, self.v_light):
            var.trace_add("write", lambda *_: self.settings_changed())

        ttk.Label(panel, text="Textures", style="Head.TLabel").pack(anchor="w", pady=(10, 0))
        tf = ttk.Frame(panel)
        tf.pack(anchor="w")
        for value, text in (("floor", "Floor"), ("wall", "Walls"), ("ceiling", "Ceiling")):
            ttk.Radiobutton(tf, text=text, value=value, variable=self.texture_target).pack(side="left")
        self.texture_label = ttk.Label(panel, text="Pick a texture below, then use the Texture tool.")
        self.texture_label.pack(anchor="w")
        gal = ttk.Frame(panel)
        gal.pack(fill="both", expand=False)
        self.gallery = tk.Canvas(gal, width=310, height=210, bg="#151515", highlightthickness=0)
        gs = ttk.Scrollbar(gal, orient="vertical", command=self.gallery.yview)
        self.gallery.configure(yscrollcommand=gs.set)
        gs.pack(side="right", fill="y")
        self.gallery.pack(side="left", fill="both")
        self.gallery.bind("<Button-1>", self.pick_texture)
        self.gallery.bind("<MouseWheel>", lambda e: self.gallery.yview_scroll(-1 if e.delta > 0 else 1, "units"))

        ttk.Label(panel, text="Checks", style="Head.TLabel").pack(anchor="w", pady=(10, 0))
        self.problems = tk.Text(panel, height=9, width=40, wrap="word", bg="#151515", fg="#e6e6e6", relief="flat")
        self.problems.pack(fill="both", expand=True)

    # ---- Level state ---------------------------------------------------------------------------------------------
    def texset(self, code=None):
        code = code or self.level["code"]
        if code not in self.sets:
            self.config(cursor="watch")
            self.update()
            self.sets[code] = tl.TextureSet(code)
            self.config(cursor="")
        return self.sets[code]

    def load_level(self, level, path):
        self.level = level
        self.path = path
        self.undo_stack.clear()
        self.redo_stack.clear()
        self._loading = True
        lv = self.level
        self.v_name.set(lv.get("name", "My level"))
        self.v_set.set(f"{lv['code']}  {tl.SETS.get(lv['code'], lv['code'])}")
        self.v_mode.set("Exterminate" if lv.get("mode") == "exterminate" else "Survival")
        self.v_lives.set(lv.get("lives", 5))
        self.v_waves.set(lv.get("waves", 10))
        self.v_roster.set(next(r[0] for r in ROSTERS if r[1] == lv.get("roster", "campaign")))
        self.v_queen.set(lv.get("queen_last_wave", True))
        self.v_light.set(lv.get("brightness", "normal").capitalize())
        for i, var in enumerate(self.v_weapons):
            var.set(i in lv.get("weapons", [0]))
        self._loading = False
        self.dirty = False
        self.fill_gallery()
        self.redraw()
        self.check()
        self.update_title()

    def settings_changed(self):
        if getattr(self, "_loading", False):
            return
        lv = self.level
        lv["name"] = self.v_name.get().strip() or "My level"
        code = self.v_set.get().split()[0] if self.v_set.get() else lv["code"]
        if code != lv["code"]:
            lv["code"] = code
            lv["tex"] = {}   # texture numbers belong to a set
            self.fill_gallery()
            self.redraw()
        lv["mode"] = "exterminate" if self.v_mode.get() == "Exterminate" else "survival"
        try:
            lv["lives"] = max(1, min(99, int(self.v_lives.get())))
            lv["waves"] = max(1, min(30, int(self.v_waves.get())))
        except (tk.TclError, ValueError):
            pass
        lv["roster"] = next((r[1] for r in ROSTERS if r[0] == self.v_roster.get()), "campaign")
        lv["queen_last_wave"] = bool(self.v_queen.get())
        lv["brightness"] = self.v_light.get().lower() or "normal"
        lv["weapons"] = [i for i, var in enumerate(self.v_weapons) if var.get()] or [0]
        self.dirty = True
        self.check()
        self.update_title()

    def snapshot(self):
        self.undo_stack.append(json.dumps(self.level))
        if len(self.undo_stack) > 100:
            self.undo_stack.pop(0)
        self.redo_stack.clear()

    def undo(self):
        if self.undo_stack:
            self.redo_stack.append(json.dumps(self.level))
            self.level = json.loads(self.undo_stack.pop())
            self.after_edit()

    def redo(self):
        if self.redo_stack:
            self.undo_stack.append(json.dumps(self.level))
            self.level = json.loads(self.redo_stack.pop())
            self.after_edit()

    def after_edit(self):
        self.dirty = True
        self.redraw()
        self.check()
        self.update_title()

    def update_title(self):
        name = os.path.basename(self.path) if self.path else "unsaved"
        self.title(f"Alien Trilogy Level Editor - {self.level.get('name', '')} ({name}){' *' if self.dirty else ''}")

    def check(self):
        problems = tl.check(self.level)
        notes = []
        lv = self.level
        if lv.get("mode") == "survival":
            if any(e["kind"] != 7 for e in lv.get("enemies", [])):
                notes.append("Survival: placed enemies (other than a Queen) are not used - the waves come out of the spawn rooms.")
            if lv.get("queen_last_wave") and not any(e["kind"] == 7 for e in lv.get("enemies", [])):
                notes.append("Survival: place a Queen (Enemy tool) for her to appear in the last wave.")
        self.problems.delete("1.0", "end")
        if problems:
            self.problems.insert("end", "To fix before exporting:\n", ())
            for p in problems:
                self.problems.insert("end", "• " + p + "\n")
        else:
            self.problems.insert("end", "Ready to export.\n")
        for n in notes:
            self.problems.insert("end", "• " + n + "\n")
        return problems

    # ---- Drawing -------------------------------------------------------------------------------------------------
    def to_canvas(self, x, z):
        return x * self.cell, (self.level["height"] - 1 - z) * self.cell

    def from_canvas(self, px, py):
        x = int(self.canvas.canvasx(px) // self.cell)
        z = self.level["height"] - 1 - int(self.canvas.canvasy(py) // self.cell)
        if 0 <= x < self.level["width"] and 0 <= z < self.level["height"]:
            return x, z
        return None

    def thumb(self, tex, size):
        key = (self.level["code"], tex, size)
        if key not in self._thumbs:
            self._thumbs[key] = ImageTk.PhotoImage(self.texset().image(tex, size))
        return self._thumbs[key]

    def redraw(self):
        c, lv, s = self.canvas, self.level, self.cell
        c.delete("all")
        W, H = lv["width"], lv["height"]
        c.configure(scrollregion=(0, 0, W * s, H * s))
        doors = {cell for d in lv.get("doors", []) for cell in tl.door_cells(d)}
        tex = lv.get("tex", {})
        ceil = lv.get("ceil", {})
        show = self.show_textures.get() and s >= 12
        ts = self.texset() if show else None
        opens = [(x, z) for z in range(H) for x in range(W) if lv["grid"][z][x] == "."] or [(0, 0)]
        box = (min(p[0] for p in opens), max(p[0] for p in opens), min(p[1] for p in opens), max(p[1] for p in opens))
        for z in range(H):
            row = lv["grid"][z]
            for x in range(W):
                t = "D" if (x, z) in doors else row[x]
                px, py = self.to_canvas(x, z)
                key = f"{x},{z}"
                if show and t in ".R":
                    own = tex.get(key, [None, None, None])
                    ftex = own[0] if own[0] is not None else ts.sample(x, z, box, False)[0]
                    c.create_image(px, py, image=self.thumb(ftex, s), anchor="nw")
                    if t == "R":
                        c.create_rectangle(px, py, px + s, py + s, outline="", fill="#55557a", stipple="gray50")
                else:
                    c.create_rectangle(px, py, px + s, py + s, fill=COLOURS[t], outline="#151515")
                if key in ceil and t in ".R" and s >= 10:
                    shade = {1536: "L", 2048: "N", 2560: "H", 3328: "V", 4064: "X"}.get(ceil[key], "?")
                    c.create_text(px + s - 3, py + 3, text=shade, fill="#9ec7ff", font=("Segoe UI", max(6, s // 3)), anchor="ne")
                if key in tex and s >= 10:
                    c.create_rectangle(px + 1, py + s - 4, px + 4, py + s - 1, fill="#ffd040", outline="")
        for e in lv.get("enemies", []):
            px, py = self.to_canvas(e["x"], e["z"])
            colour = "#ff3030" if e["kind"] == 7 else "#e06060"
            c.create_oval(px + 2, py + 2, px + s - 2, py + s - 2, fill=colour, outline="#300")
            c.create_text(px + s / 2, py + s / 2, text="Q" if e["kind"] == 7 else tl.ENEMIES[e["kind"]][0][0],
                          fill="white", font=("Segoe UI", max(6, s // 2), "bold"))
        for p in lv.get("pickups", []):
            px, py = self.to_canvas(p["x"], p["z"])
            m = s / 2
            c.create_polygon(px + m, py + 2, px + s - 2, py + m, px + m, py + s - 2, px + 2, py + m, fill="#40d0ff", outline="#024")
        sx, sz = lv["start"]
        px, py = self.to_canvas(sx, sz)
        c.create_oval(px + 1, py + 1, px + s - 1, py + s - 1, fill="#3080ff", outline="white")
        c.create_text(px + s / 2, py + s / 2, text=FACING[lv.get("start_dir", 4) % 8], fill="white",
                      font=("Segoe UI", max(6, s // 2), "bold"))

    def fill_gallery(self):
        g = self.gallery
        g.delete("all")
        ts = self.texset()
        self._gallery_items = {}
        size, cols = 36, 8
        for n, tex in enumerate(ts.usable()):
            x, y = (n % cols) * (size + 2) + 2, (n // cols) * (size + 2) + 2
            item = g.create_image(x, y, image=self.thumb(tex, size), anchor="nw")
            self._gallery_items[item] = tex
            if tex == self.texture:
                g.create_rectangle(x - 1, y - 1, x + size, y + size, outline="#ffd040", width=2)
        g.configure(scrollregion=(0, 0, cols * (size + 2), (len(ts.usable()) // cols + 1) * (size + 2)))

    def pick_texture(self, event):
        items = self.gallery.find_overlapping(self.gallery.canvasx(event.x), self.gallery.canvasy(event.y),
                                              self.gallery.canvasx(event.x), self.gallery.canvasy(event.y))
        for item in items:
            if item in self._gallery_items:
                self.texture = self._gallery_items[item]
                self.texture_label.configure(text=f"Texture {self.texture} picked: use the Texture tool on the map.")
                self.tool.set("texture")
                self.tool_changed()
                self.fill_gallery()
                return

    def zoom(self, step):
        self.cell = max(6, min(40, self.cell + step))
        self.redraw()

    # ---- Editing -------------------------------------------------------------------------------------------------
    def tool_changed(self):
        hint = next(h for k, _, h in TOOLS if k == self.tool.get())
        self.hint.configure(text=hint + "\n\nRight-click a cell to see what is there. Ctrl+Z undoes.")

    def set_cell(self, x, z, ch):
        lv = self.level
        if not (0 <= x < lv["width"] and 0 <= z < lv["height"]):
            return
        row = lv["grid"][z]
        if row[x] != ch:
            lv["grid"][z] = row[:x] + ch + row[x + 1:]

    def brush_cells(self, x, z):
        r = self.brush.get() // 2
        return [(x + dx, z + dz) for dz in range(-r, r + 1) for dx in range(-r, r + 1)]

    def press(self, event):
        cell = self.from_canvas(event.x, event.y)
        if not cell:
            return
        self.snapshot()
        self._stroke = True
        self.apply(cell, first=True)

    def drag(self, event):
        cell = self.from_canvas(event.x, event.y)
        if cell and getattr(self, "_stroke", False) and self.tool.get() in ("floor", "wall", "room", "hive", "ceiling", "texture", "erase"):
            self.apply(cell, first=False)

    def release(self, _event):
        if getattr(self, "_stroke", False):
            self._stroke = False
            self.after_edit()

    def remove_things(self, x, z):
        lv = self.level
        lv["enemies"] = [e for e in lv["enemies"] if (e["x"], e["z"]) != (x, z)]
        lv["pickups"] = [p for p in lv["pickups"] if (p["x"], p["z"]) != (x, z)]

    def apply(self, cell, first):
        lv, tool = self.level, self.tool.get()
        x, z = cell
        key = f"{x},{z}"
        if tool in ("floor", "wall", "room", "hive"):
            ch = {"floor": ".", "wall": "#", "room": "R", "hive": "W"}[tool]
            for cx, cz in self.brush_cells(x, z):
                if 0 < cx < lv["width"] - 1 and 0 < cz < lv["height"] - 1:
                    self.set_cell(cx, cz, ch)
                    if ch in "#W":
                        self.remove_things(cx, cz)
                        lv["doors"] = [d for d in lv["doors"] if (cx, cz) not in tl.door_cells(d)]
        elif tool in ("door_x", "door_z") and first:
            axis = "x" if tool == "door_x" else "z"
            d = {"x": x, "z": z, "axis": axis}
            cells = tl.door_cells(d)
            if any(not (0 < cx < lv["width"] - 1 and 0 < cz < lv["height"] - 1) for cx, cz in cells):
                self.status.configure(text="A door is 4 cells long: that runs off the map.")
                return
            lv["doors"] = [o for o in lv["doors"] if not set(tl.door_cells(o)) & set(cells)]
            lv["doors"].append(d)
            for cx, cz in cells:
                self.set_cell(cx, cz, "#")
                self.remove_things(cx, cz)
        elif tool == "start" and first:
            if lv["start"] == [x, z]:
                lv["start_dir"] = (lv.get("start_dir", 4) + 2) % 8
            else:
                lv["start"] = [x, z]
                if lv["grid"][z][x] != ".":
                    self.set_cell(x, z, ".")
        elif tool == "enemy" and first:
            kind = next(k for k, v in tl.ENEMIES.items() if v[0] == self.enemy_kind.get())
            if kind == 7:
                lv["enemies"] = [e for e in lv["enemies"] if e["kind"] != 7]
            self.remove_things(x, z)
            lv["enemies"].append({"kind": kind, "x": x, "z": z})
            if lv["grid"][z][x] not in ".R":
                self.set_cell(x, z, ".")
        elif tool == "pickup" and first:
            kind = next(k for k, v in tl.PICKUPS.items() if v[0] == self.pickup_kind.get())
            self.remove_things(x, z)
            lv["pickups"].append({"kind": kind, "x": x, "z": z})
            if lv["grid"][z][x] not in ".R":
                self.set_cell(x, z, ".")
        elif tool == "ceiling":
            value = next(v for name, v in CEILINGS if name == self.ceiling_choice.get())
            for cx, cz in self.brush_cells(x, z):
                k = f"{cx},{cz}"
                if value is None:
                    lv["ceil"].pop(k, None)
                else:
                    lv["ceil"][k] = value
        elif tool == "texture":
            if self.texture is None:
                self.status.configure(text="Pick a texture on the right first.")
                return
            slot = {"floor": 0, "wall": 1, "ceiling": 2}[self.texture_target.get()]
            for cx, cz in self.brush_cells(x, z):
                k = f"{cx},{cz}"
                own = list(lv["tex"].get(k, [None, None, None]))
                own[slot] = self.texture
                lv["tex"][k] = own
        elif tool == "erase":
            for cx, cz in self.brush_cells(x, z):
                k = f"{cx},{cz}"
                self.remove_things(cx, cz)
                lv["tex"].pop(k, None)
                lv["ceil"].pop(k, None)
                lv["doors"] = [d for d in lv["doors"] if (cx, cz) not in tl.door_cells(d)]
        self.redraw()

    def hover(self, event):
        cell = self.from_canvas(event.x, event.y)
        if cell:
            self.status.configure(text=f"Cell {cell[0]}, {cell[1]}    {self.describe(cell)}")

    def describe(self, cell):
        lv = self.level
        x, z = cell
        names = {"#": "Wall", ".": "Floor", "R": "Spawn room", "W": "Hive wall"}
        parts = [names.get(lv["grid"][z][x], "?")]
        if any(cell in tl.door_cells(d) for d in lv["doors"]):
            parts = ["Door"]
        parts += [tl.ENEMIES[e["kind"]][0] for e in lv["enemies"] if (e["x"], e["z"]) == cell]
        parts += [tl.PICKUPS[p["kind"]][0] for p in lv["pickups"] if (p["x"], p["z"]) == cell]
        k = f"{x},{z}"
        if k in lv["ceil"]:
            parts.append(f"ceiling {lv['ceil'][k]}")
        if k in lv["tex"]:
            parts.append("textures " + "/".join("auto" if t is None else str(t) for t in lv["tex"][k]))
        if lv["start"] == [x, z]:
            parts.append("player start")
        return ", ".join(parts)

    def right_click(self, event):
        cell = self.from_canvas(event.x, event.y)
        if cell:
            messagebox.showinfo("This cell", f"Cell {cell[0]}, {cell[1]}: {self.describe(cell)}")

    # ---- Files ---------------------------------------------------------------------------------------------------
    def ask_save_changes(self):
        if not self.dirty:
            return True
        answer = messagebox.askyesnocancel("Unsaved changes", "Save the current level first?")
        if answer is None:
            return False
        if answer:
            return self.save_level()
        return True

    def new_level(self):
        if not self.ask_save_changes():
            return
        choice = NewDialog(self).result
        if not choice:
            return
        if choice == "arena":
            import make_survival_map
            level = make_survival_map.level()
            level["name"] = "My arena"
        else:
            w, h = choice
            level = blank_level(w, h, self.level.get("code", "162"))
        self.load_level(level, None)
        self.dirty = True
        self.update_title()

    def open_level(self):
        if not self.ask_save_changes():
            return
        path = filedialog.askopenfilename(title="Open a level", filetypes=[("Level projects", "*.atlevel"), ("All files", "*.*")],
                                          initialdir=EXPORT_HOME if os.path.isdir(EXPORT_HOME) else ROOT)
        if not path:
            return
        try:
            level = json.load(open(path, encoding="utf-8"))
            self.load_level(level, path)
        except Exception as error:   # noqa: BLE001 - shown to the user
            messagebox.showerror("Open", f"That file could not be opened:\n{error}")

    def import_campaign(self):
        if not self.ask_save_changes():
            return
        codes = [c for c in tl.SETS if tl.find_level(c) and c != "113"]
        pick_ = ListDialog(self, "Edit a campaign level", "Pick a level to start from. Its layout, doors, enemies, "
                           "pickups and textures come across; floors come out flat.", [f"{c}  {tl.SETS[c]}" for c in codes]).result
        if not pick_:
            return
        code = pick_.split()[0]
        try:
            level = tl.import_level(tl.find_level(code))
        except Exception as error:   # noqa: BLE001
            messagebox.showerror("Import", f"That level could not be read:\n{error}")
            return
        self.load_level(level, None)
        self.dirty = True
        self.update_title()

    def save_level(self):
        path = self.path
        if not path:
            os.makedirs(EXPORT_HOME, exist_ok=True)
            path = filedialog.asksaveasfilename(title="Save level project", defaultextension=".atlevel",
                                                initialdir=EXPORT_HOME, initialfile=safe_name(self.level["name"]) + ".atlevel",
                                                filetypes=[("Level projects", "*.atlevel")])
            if not path:
                return False
        with open(path, "w", encoding="utf-8") as f:
            json.dump(self.level, f, indent=1)
        self.path = path
        self.dirty = False
        self.update_title()
        self.status.configure(text=f"Saved {path}")
        return True

    def export_level(self):
        problems = self.check()
        if problems:
            messagebox.showwarning("Export", "Fix these first:\n\n" + "\n".join("• " + p for p in problems))
            return
        os.makedirs(EXPORT_HOME, exist_ok=True)
        base = filedialog.askdirectory(title="Export into which folder? (a folder named after the level goes in it)",
                                       initialdir=EXPORT_HOME)
        if not base:
            return
        folder = os.path.join(base, safe_name(self.level["name"]))
        try:
            stats = tl.write(self.level, folder, self.texset())
        except Exception as error:   # noqa: BLE001
            messagebox.showerror("Export", f"The level could not be built:\n{error}")
            return
        files = ", ".join(sorted(os.listdir(folder)))
        messagebox.showinfo("Exported", f"{self.level['name']} is in\n{folder}\n\n{files}\n\n"
                            f"{stats['faces']} faces, {stats['vertices']} vertices.")

    def preview_3d(self):
        if not os.path.exists(PC_EXE):
            messagebox.showinfo("3D preview", "The 3D preview uses the PC build of the game, which was not found at\n" + PC_EXE)
            return
        problems = self.check()
        if problems:
            messagebox.showwarning("3D preview", "Fix these first:\n\n" + "\n".join("• " + p for p in problems))
            return
        work = tempfile.mkdtemp(prefix="at_preview_")
        try:
            tl.write(self.level, work, self.texset())
        except Exception as error:   # noqa: BLE001
            messagebox.showerror("3D preview", f"The level could not be built:\n{error}")
            return
        self.status.configure(text="Drawing the 3D preview with the PC renderer...")
        self.config(cursor="watch")
        self._render_result = None
        threading.Thread(target=self._render, args=(work,), daemon=True).start()
        self.after(250, self._poll_render)

    def _poll_render(self):
        if self._render_result is None:
            self.after(250, self._poll_render)
            return
        image, error = self._render_result
        self._render_result = None
        self._show_render(image, error)

    def _render(self, work):
        image, error = None, None
        snapshot = tempfile.mkdtemp(prefix="at_reports_")
        before = {}
        try:
            for name in os.listdir(REPORTS):
                p = os.path.join(REPORTS, name)
                if os.path.isfile(p):
                    before[name] = os.path.getmtime(p)
                    shutil.copy2(p, snapshot)
            level = os.path.join(work, f"L{self.level['code']}LEV.MAP")
            subprocess.run([PC_EXE, "--level", level, "--gpu-self-test"], cwd=os.path.dirname(PC_EXE), timeout=180,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            shot = os.path.join(REPORTS, "gpu-dx11-preview.bmp")
            if os.path.exists(shot) and os.path.getmtime(shot) != before.get("gpu-dx11-preview.bmp"):
                image = Image.open(shot).convert("RGB")
            else:
                error = "The renderer did not produce a picture."
        except Exception as e:   # noqa: BLE001
            error = str(e)
        finally:
            # Put the PC build's reports back as they were (the self-test overwrites them).
            for name in os.listdir(REPORTS):
                p = os.path.join(REPORTS, name)
                if not os.path.isfile(p):
                    continue
                if name in before:
                    if os.path.getmtime(p) != before[name]:
                        shutil.copy2(os.path.join(snapshot, name), p)
                else:
                    os.remove(p)
            shutil.rmtree(snapshot, ignore_errors=True)
            shutil.rmtree(work, ignore_errors=True)
        self._render_result = (image, error)   # picked up by _poll_render on the window's thread

    def _show_render(self, image, error):
        self.config(cursor="")
        if error:
            self.status.configure(text="3D preview failed.")
            messagebox.showerror("3D preview", error)
            return
        self.status.configure(text="3D preview: from the player start (click the start again to turn it, then preview).")
        win = tk.Toplevel(self)
        win.title("3D preview - from the player start")
        photo = ImageTk.PhotoImage(image)
        label = tk.Label(win, image=photo, bg="black")
        label.image = photo
        label.pack()

    def resize(self):
        lv = self.level
        answer = simpledialog.askstring("Map size", "New size as width x height (16 to 127 each):",
                                        initialvalue=f"{lv['width']}x{lv['height']}", parent=self)
        if not answer:
            return
        try:
            w, h = [int(v) for v in answer.lower().replace(" ", "").split("x")]
        except ValueError:
            return
        w, h = max(16, min(127, w)), max(16, min(127, h))
        self.snapshot()
        grid = []
        for z in range(h):
            row = lv["grid"][z] if z < lv["height"] else ""
            grid.append("".join(row[x] if x < len(row) and 0 < x < w - 1 and 0 < z < h - 1 else "#" for x in range(w)))
        lv["grid"], lv["width"], lv["height"] = grid, w, h
        inside = lambda x, z: 0 < x < w - 1 and 0 < z < h - 1
        lv["enemies"] = [e for e in lv["enemies"] if inside(e["x"], e["z"])]
        lv["pickups"] = [p for p in lv["pickups"] if inside(p["x"], p["z"])]
        lv["doors"] = [d for d in lv["doors"] if all(inside(x, z) for x, z in tl.door_cells(d))]
        if not inside(*lv["start"]):
            lv["start"] = [w // 2, h // 2]
        self.after_edit()

    def show_help(self):
        messagebox.showinfo("How it works", (
            "1. New starts a blank room or a copy of the Survival hive arena; 'Edit a campaign level' starts from one "
            "of the game's levels.\n\n"
            "2. Paint with the tools on the left: Floor and Wall shape the level; Door places a 4-cell door into a "
            "wall (floor on both sides); Player start sets where you begin (click it again to turn).\n\n"
            "3. Survival: paint Spawn rooms (blue) behind doors - the waves come out of them. Place a Queen for the "
            "last wave. Exterminate: place the enemies you want, killing them all wins.\n\n"
            "4. Look chooses which campaign level's textures, sounds and music the level uses. Texture: pick one on "
            "the right, choose Floor/Walls/Ceiling, and paint it on. 'Show textures' shows the floor textures.\n\n"
            "5. Checks (bottom right) says what still needs fixing. 3D preview draws the level from the player start. "
            "Export writes the level's files into a folder; Save keeps your project to come back to."))

    def quit_editor(self):
        if self.ask_save_changes():
            self.destroy()


def safe_name(name):
    keep = "".join(ch if ch.isalnum() or ch in " -_" else "_" for ch in name).strip()
    return keep or "Level"


class NewDialog(simpledialog.Dialog):
    def body(self, master):
        self.title("New level")
        self.choice = tk.StringVar(value="room")
        ttk.Radiobutton(master, text="Blank room", value="room", variable=self.choice).grid(row=0, column=0, sticky="w")
        ttk.Radiobutton(master, text="Copy of the Survival hive arena", value="arena", variable=self.choice).grid(row=1, column=0, sticky="w")
        ttk.Label(master, text="Size (blank room):").grid(row=2, column=0, sticky="w", pady=(8, 0))
        self.size = tk.StringVar(value="40x32")
        ttk.Combobox(master, textvariable=self.size, values=["24x24", "40x32", "64x48", "96x80"]).grid(row=3, column=0, sticky="w")
        return master

    def apply(self):
        if self.choice.get() == "arena":
            self.result = "arena"
            return
        try:
            w, h = [int(v) for v in self.size.get().lower().split("x")]
        except ValueError:
            w, h = 40, 32
        self.result = (max(16, min(127, w)), max(16, min(127, h)))


class ListDialog(simpledialog.Dialog):
    def __init__(self, parent, title, text, items):
        self.text, self.items, self.result = text, items, None
        self._title = title
        super().__init__(parent)

    def body(self, master):
        self.title(self._title)
        ttk.Label(master, text=self.text, wraplength=380).pack(anchor="w")
        self.box = tk.Listbox(master, height=16, width=40)
        for item in self.items:
            self.box.insert("end", item)
        self.box.pack(fill="both", expand=True, pady=6)
        self.box.bind("<Double-Button-1>", lambda e: self.ok())
        return self.box

    def apply(self):
        sel = self.box.curselection()
        self.result = self.items[sel[0]] if sel else None


if __name__ == "__main__":
    Editor().mainloop()
