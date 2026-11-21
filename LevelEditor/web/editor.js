// Alien Trilogy level editor (3D). The level is the trilogy_level dict (Xbox360/tools/trilogy_level.py); the server
// (editor_server.py) turns it into the faces Export writes, which this page draws and lets you click on.
// World: a cell is 1 unit, heights are game units / 512. Cell (x, z) is grid[z][x].
"use strict";

// ---- Small helpers ----------------------------------------------------------------------------------------------
const $ = (id) => document.getElementById(id);
const api = async (path, body) => {
  const r = await fetch(path, body === undefined ? {} : { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
  const data = await r.json();
  if (!r.ok) throw new Error(data.error || r.statusText);
  return data;
};
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const key = (x, z) => `${x},${z}`;
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

const M4 = {
  persp(fov, aspect, near, far) {
    const f = 1 / Math.tan(fov / 2), nf = 1 / (near - far);
    return [f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (far + near) * nf, -1, 0, 0, 2 * far * near * nf, 0];
  },
  lookAt(e, t, u) {
    let zx = e[0] - t[0], zy = e[1] - t[1], zz = e[2] - t[2];
    let l = Math.hypot(zx, zy, zz); zx /= l; zy /= l; zz /= l;
    let xx = u[1] * zz - u[2] * zy, xy = u[2] * zx - u[0] * zz, xz = u[0] * zy - u[1] * zx;
    l = Math.hypot(xx, xy, xz); xx /= l; xy /= l; xz /= l;
    const yx = zy * xz - zz * xy, yy = zz * xx - zx * xz, yz = zx * xy - zy * xx;
    return [xx, yx, zx, 0, xy, yy, zy, 0, xz, yz, zz, 0,
      -(xx * e[0] + xy * e[1] + xz * e[2]), -(yx * e[0] + yy * e[1] + yz * e[2]), -(zx * e[0] + zy * e[1] + zz * e[2]), 1];
  },
  mul(a, b) {
    const o = new Array(16);
    for (let i = 0; i < 4; i++) for (let j = 0; j < 4; j++) {
      let s = 0; for (let k = 0; k < 4; k++) s += a[k * 4 + j] * b[i * 4 + k]; o[i * 4 + j] = s;
    }
    return o;
  },
  apply(m, p) {
    const x = p[0], y = p[1], z = p[2];
    const w = m[3] * x + m[7] * y + m[11] * z + m[15];
    return [(m[0] * x + m[4] * y + m[8] * z + m[12]) / w, (m[1] * x + m[5] * y + m[9] * z + m[13]) / w, w];
  },
};

// ---- State ------------------------------------------------------------------------------------------------------
const S = {
  info: null, set: null, level: null, file: null, dirty: false,
  undo: [], redo: [],
  tool: "dig", brush: 1, step: 128,
  texture: null, enemy: 6, pickup: 11,
  geo: null, faceOf: {},           // faceOf["x,z,part"] = first face index
  hover: null,                     // {face, x, z, part, solid}
  carry: null,                     // Select tool: {type, index}
  doorAxis: null,                  // null = from the camera
  showCeil: false, showGrid: true, fly: false,
  cam: { tx: 20, ty: 0, tz: 16, yaw: 0.6, pitch: 0.9, dist: 40, ex: 0, ey: 10, ez: 0 },
  keys: new Set(),
};
const TOOLS = [
  ["select", "Select / move", "V", "Click an enemy, pickup or the player start to pick it up, then click a floor to put it down. Delete removes it; R turns the start."],
  ["dig", "Dig floor", "1", "Click or drag over walls to open them into floor. Brush size applies."],
  ["fill", "Fill wall", "2", "Click or drag over floor to fill it in with solid wall."],
  ["raise", "Raise / lower", "3", "Click raises the floor by the step; Shift+click lowers it. Drag to paint. The player climbs at most 256 between cells - use ramps for more."],
  ["flatten", "Flatten", "4", "Click a floor, then drag: every cell you cross gets that floor's height."],
  ["ceiling", "Ceiling height", "5", "Click raises the ceiling by the step, Shift+click lowers it, Ctrl+click puts back the automatic dome."],
  ["ramp", "Ramp", "6", "Click a floor to make it a ramp rising away from the camera (128 per cell). Click again to turn it; Shift+click flattens it. Chain ramps for slopes."],
  ["paint", "Paint texture", "T", "Click any floor, wall or ceiling to paint the picked texture. Shift+click floods; Alt+click picks a texture up."],
  ["door", "Door", "7", "Click a wall to place a 4-cell door across it (floor on both sides). R turns it. Doors run along the axis facing the camera."],
  ["room", "Spawn room", "8", "Survival: paint the room behind a door that the waves pour out of."],
  ["hive", "Hive wall", "9", "Solid wall in the hive's organic textures."],
  ["start", "Player start", "P", "Click a floor to start there; click it again (or R) to turn."],
  ["enemy", "Enemy", "N", "Click a floor to place the enemy chosen in Objects."],
  ["pickup", "Pickup", "I", "Click a floor to place the item chosen in Objects."],
  ["erase", "Eraser", "X", "Removes doors, enemies, pickups, textures, heights and ramps from the cells you click."],
];
const SIDES = ["west", "east", "north", "south"];
const PART_NAMES = ["floor", "ceiling", "west wall", "east wall", "north wall", "south wall"];

// ---- WebGL ------------------------------------------------------------------------------------------------------
const canvas = $("gl");
const gl = canvas.getContext("webgl2", { antialias: true, alpha: false });
if (!gl) document.body.innerHTML = "<p style='padding:20px'>This editor needs WebGL 2 (any current Chrome, Edge or Firefox).</p>";

function shader(vs, fs) {
  const p = gl.createProgram();
  for (const [type, src] of [[gl.VERTEX_SHADER, vs], [gl.FRAGMENT_SHADER, fs]]) {
    const s = gl.createShader(type);
    gl.shaderSource(s, "#version 300 es\nprecision highp float;\n" + src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
    gl.attachShader(p, s);
  }
  gl.linkProgram(p);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
  const u = {};
  for (let i = 0, n = gl.getProgramParameter(p, gl.ACTIVE_UNIFORMS); i < n; i++) {
    const a = gl.getActiveUniform(p, i); u[a.name] = gl.getUniformLocation(p, a.name);
  }
  return { p, u };
}

// Level faces: position, local uv, atlas tile (-1 = flat colour), shade, normal, pick id.
const LEVEL = shader(`
layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUv; layout(location=2) in float aTile;
layout(location=3) in float aShade; layout(location=4) in vec3 aNormal; layout(location=5) in vec4 aId;
uniform mat4 uVP; uniform vec3 uEye; uniform float uCeil;
out vec2 vUv; flat out float vTile; out float vShade; out vec4 vId; out float vCull; out float vDist;
void main() {
  gl_Position = uVP * vec4(aPos, 1.0);
  vUv = aUv; vTile = aTile; vShade = aShade; vId = aId;
  vCull = dot(aNormal, uEye - aPos);
  if (aNormal.y < -0.5 && uCeil < 0.5) vCull = -1.0;
  vDist = length(uEye - aPos);
}`, `
in vec2 vUv; flat in float vTile; in float vShade; in vec4 vId; in float vCull; in float vDist;
uniform sampler2D uAtlas; uniform vec2 uAtlasTiles; uniform float uPick;
out vec4 o;
void main() {
  if (vCull < 0.0) discard;
  if (uPick > 0.5) { o = vId; return; }
  vec3 c;
  if (vTile < 0.0) { c = vec3(0.13, 0.15, 0.13) * vShade; }
  else {
    vec2 cell = vec2(mod(vTile, uAtlasTiles.x), floor(vTile / uAtlasTiles.x));
    vec2 uv = (cell + clamp(vUv, 0.004, 0.996)) / uAtlasTiles;
    vec4 t = texture(uAtlas, uv);
    if (t.a < 0.5) discard;
    // The PS1 texels are dark (the game's lighting lifts them): lift the darks, keep bright looks from washing out.
    c = (vec3(1.0) - pow(vec3(1.0) - t.rgb, vec3(1.8))) * vShade;
  }
  float fog = clamp((vDist - 30.0) / 120.0, 0.0, 0.6);
  o = vec4(mix(c, vec3(0.07, 0.08, 0.07), fog), 1.0);
}`);
// Flat coloured lines and shapes (markers, outlines, brush).
const FLAT = shader(`
layout(location=0) in vec3 aPos; layout(location=1) in vec4 aCol;
uniform mat4 uVP; out vec4 vCol;
void main() { gl_Position = uVP * vec4(aPos, 1.0); vCol = aCol; }`, `
in vec4 vCol; out vec4 o; void main() { o = vCol; }`);

const levelVao = gl.createVertexArray(), levelBuf = gl.createBuffer();
let levelCount = 0;
const flatVao = gl.createVertexArray(), flatBuf = gl.createBuffer();
gl.bindVertexArray(flatVao);
gl.bindBuffer(gl.ARRAY_BUFFER, flatBuf);
gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 28, 0);
gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 4, gl.FLOAT, false, 28, 12);
gl.bindVertexArray(null);

let atlasTex = null, atlasImg = null;
const pickFb = gl.createFramebuffer(), pickColor = gl.createTexture(), pickDepth = gl.createRenderbuffer();
let pickW = 0, pickH = 0;

function resize() {
  const r = canvas.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
  const w = Math.max(1, Math.round(r.width * dpr)), h = Math.max(1, Math.round(r.height * dpr));
  if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
  if (pickW !== w || pickH !== h) {
    pickW = w; pickH = h;
    gl.bindTexture(gl.TEXTURE_2D, pickColor);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, w, h, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
    gl.bindRenderbuffer(gl.RENDERBUFFER, pickDepth);
    gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT24, w, h);
    gl.bindFramebuffer(gl.FRAMEBUFFER, pickFb);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, pickColor, 0);
    gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, pickDepth);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }
}

// ---- Level mesh from the server's geometry ----------------------------------------------------------------------
const SOLID_BASE = 1 << 20;   // pick ids: faces 1.., solid cells SOLID_BASE + cell
function buildMesh() {
  const g = S.geo, lv = S.level, W = lv.width, H = lv.height;
  const ox = g.offset[0], oz = g.offset[1];
  const solid = [];
  for (let z = 0; z < H; z++) for (let x = 0; x < W; x++) if (!"RD.".includes(cellType(x, z))) solid.push([x, z]);
  const verts = g.faces * 6 + solid.length * 6;
  const f32 = new Float32Array(verts * 11), u8 = new Uint8Array(f32.buffer);
  let o = 0;
  const put = (x, y, z, u, v, tile, shade, nx, ny, nz, id) => {
    f32[o] = x; f32[o + 1] = y; f32[o + 2] = z; f32[o + 3] = u; f32[o + 4] = v; f32[o + 5] = tile; f32[o + 6] = shade;
    f32[o + 7] = nx; f32[o + 8] = ny; f32[o + 9] = nz;
    const b = (o + 10) * 4; u8[b] = id & 255; u8[b + 1] = (id >> 8) & 255; u8[b + 2] = (id >> 16) & 255; u8[b + 3] = 255;
    o += 11;
  };
  const UV = [[0, 1], [1, 1], [1, 0], [0, 0]];
  const NORMALS = [[0, 1, 0], [0, -1, 0], [1, 0, 0], [-1, 0, 0], [0, 0, 1], [0, 0, -1]];
  const SHADE = [1.0, 0.72, 0.86, 0.86, 0.74, 0.74];
  S.faceOf = {};
  for (let f = 0; f < g.faces; f++) {
    const x = g.meta[f * 3], z = g.meta[f * 3 + 1], part = g.meta[f * 3 + 2];
    const k = `${x},${z},${part}`;
    if (!(k in S.faceOf)) S.faceOf[k] = f;
    const p = [];
    for (let i = 0; i < 4; i++) {
      const b = (f * 4 + i) * 3;
      p.push([(g.pos[b] + ox) / 512, g.pos[b + 1] / 512, (g.pos[b + 2] + oz) / 512]);
    }
    // Floors get lighter with height so platforms and pits read from above.
    const lift = part === 0 ? 1 + Math.min(0.6, (p[0][1] + p[2][1]) * 0.5 * 0.22) : 1;
    const n = NORMALS[part], sh = SHADE[part] * lift * (cellType(x, z) === "R" && part === 0 ? 0.8 : 1);
    for (const i of [0, 1, 2, 0, 2, 3]) put(p[i][0], p[i][1], p[i][2], UV[i][0], UV[i][1], g.tex[f], sh, n[0], n[1], n[2], f + 1);
  }
  for (const [x, z] of solid) {
    const sh = cellType(x, z) === "W" ? 1.6 : 1.0;
    const q = [[x, z], [x, z + 1], [x + 1, z + 1], [x + 1, z]];
    for (const i of [0, 1, 2, 0, 2, 3]) put(q[i][0], -0.02, q[i][1], 0, 0, -1, sh, 0, 1, 0, SOLID_BASE + z * W + x);
  }
  levelCount = verts;
  gl.bindVertexArray(levelVao);
  gl.bindBuffer(gl.ARRAY_BUFFER, levelBuf);
  gl.bufferData(gl.ARRAY_BUFFER, f32, gl.STATIC_DRAW);
  const st = 44;
  gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, st, 0);
  gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 2, gl.FLOAT, false, st, 12);
  gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 1, gl.FLOAT, false, st, 20);
  gl.enableVertexAttribArray(3); gl.vertexAttribPointer(3, 1, gl.FLOAT, false, st, 24);
  gl.enableVertexAttribArray(4); gl.vertexAttribPointer(4, 3, gl.FLOAT, false, st, 28);
  gl.enableVertexAttribArray(5); gl.vertexAttribPointer(5, 4, gl.UNSIGNED_BYTE, true, st, 40);
  gl.bindVertexArray(null);
}

// ---- Level model ------------------------------------------------------------------------------------------------
const cellType = (x, z) => {
  const lv = S.level;
  if (x < 0 || z < 0 || x >= lv.width || z >= lv.height) return "#";
  for (const d of lv.doors) if (doorCells(d).some(([a, b]) => a === x && b === z)) return "D";
  return lv.grid[z][x];
};
const isOpen = (x, z) => "R.".includes(S.level.grid[z]?.[x] ?? "#");
const doorCells = (d) => [0, 1, 2, 3].map((i) => d.axis === "x" ? [d.x + i, d.z] : [d.x, d.z + i]);
const floorOf = (x, z) => (S.level.floor || {})[key(x, z)] || 0;
const rampOf = (x, z) => (S.level.ramp || {})[key(x, z)];
const interior = (x, z) => x > 0 && z > 0 && x < S.level.width - 1 && z < S.level.height - 1;
function setCell(x, z, ch) {
  const row = S.level.grid[z];
  if (row[x] !== ch) S.level.grid[z] = row.slice(0, x) + ch + row.slice(x + 1);
}
function removeThings(x, z) {
  const lv = S.level;
  lv.enemies = lv.enemies.filter((e) => e.x !== x || e.z !== z);
  lv.pickups = lv.pickups.filter((p) => p.x !== x || p.z !== z);
}
function normalise(lv) {
  for (const k of ["doors", "enemies", "pickups"]) lv[k] = lv[k] || [];
  for (const k of ["ceil", "tex", "floor", "ramp"]) lv[k] = lv[k] || {};
  return lv;
}
// Ceiling at the cell (the writer's rule, without the dome's curve): shown in the status line only.
function ceilOf(x, z) {
  const lv = S.level, own = lv.ceil[key(x, z)];
  return own !== undefined ? own : null;
}

// ---- Undo -------------------------------------------------------------------------------------------------------
function snapshot() {
  S.undo.push(JSON.stringify(S.level));
  if (S.undo.length > 200) S.undo.shift();
  S.redo.length = 0;
}
function undo() { if (S.undo.length) { S.redo.push(JSON.stringify(S.level)); S.level = JSON.parse(S.undo.pop()); changed(); } }
function redo() { if (S.redo.length) { S.undo.push(JSON.stringify(S.level)); S.level = JSON.parse(S.redo.pop()); changed(); } }

// ---- Server round trips (rebuild after edits, one at a time) ----------------------------------------------------
let building = false, buildAgain = false, checkTimer = 0;
async function rebuild() {
  if (building) { buildAgain = true; return; }
  building = true; $("busy").style.display = "block";
  try {
    do {
      buildAgain = false;
      S.geo = await api("/api/geometry", S.level);
      buildMesh();
    } while (buildAgain);
  } catch (e) { status("Could not build the level: " + e.message); }
  building = false; $("busy").style.display = "none";
}
function changed(noDirty) {
  if (!noDirty) { S.dirty = true; }
  updateTitle();
  rebuild();
  clearTimeout(checkTimer);
  checkTimer = setTimeout(runChecks, 300);
}
async function runChecks() {
  const lv = S.level;
  let problems = [];
  try { problems = (await api("/api/check", lv)).problems; } catch (e) { problems = [e.message]; }
  const notes = [];
  if (lv.mode === "survival") {
    if (lv.enemies.some((e) => e.kind !== 7)) notes.push("Survival: placed enemies (other than a Queen) are not used - the waves come out of the spawn rooms.");
    if (lv.queen_last_wave && !lv.enemies.some((e) => e.kind === 7)) notes.push("Survival: place a Queen for her to appear in the last wave.");
  }
  const steep = steepSteps();
  if (steep) notes.push(`${steep} step${steep > 1 ? "s are" : " is"} higher than the player can climb (256): fine for ledges, use ramps for paths.`);
  let html = problems.length ? `<span class="bad">To fix before exporting:</span>\n` + problems.map((p) => `<span class="bad">• ${esc(p)}</span>`).join("\n")
    : `<span class="ok">Ready to export.</span>`;
  if (notes.length) html += "\n\n" + notes.map((n) => `<span class="note">• ${esc(n)}</span>`).join("\n");
  $("checks").innerHTML = html;
  $("checksTab").textContent = problems.length ? `Checks (${problems.length})` : "Checks";
  return problems;
}
function steepSteps() {
  const lv = S.level; let n = 0;
  for (let z = 1; z < lv.height - 1; z++) for (let x = 1; x < lv.width - 1; x++) {
    if (!isOpen(x, z)) continue;
    for (const [nx, nz] of [[x + 1, z], [x, z + 1]]) if (isOpen(nx, nz) && rampOf(x, z) === undefined && rampOf(nx, nz) === undefined
      && Math.abs(floorOf(x, z) - floorOf(nx, nz)) > S.info.limits.step) n++;
  }
  return n;
}

// ---- Camera -----------------------------------------------------------------------------------------------------
function camera() {
  const c = S.cam;
  const fx = -Math.cos(c.pitch) * Math.sin(c.yaw), fy = -Math.sin(c.pitch), fz = -Math.cos(c.pitch) * Math.cos(c.yaw);
  let eye, target;
  if (S.fly) { eye = [c.ex, c.ey, c.ez]; target = [c.ex + fx, c.ey + fy, c.ez + fz]; }
  else { target = [c.tx, c.ty, c.tz]; eye = [c.tx - fx * c.dist, c.ty - fy * c.dist, c.tz - fz * c.dist]; }
  const aspect = canvas.width / canvas.height;
  const proj = M4.persp(0.9, aspect, 0.05, 600);
  const view = M4.lookAt(eye, target, [0, 1, 0]);
  return { eye, vp: M4.mul(proj, view), fwd: [fx, fy, fz] };
}
function setFly(on) {
  const c = S.cam, cam = camera();
  if (on && !S.fly) { c.ex = cam.eye[0]; c.ey = cam.eye[1]; c.ez = cam.eye[2]; }
  if (!on && S.fly) { c.tx = c.ex + cam.fwd[0] * c.dist; c.ty = Math.max(0, c.ey + cam.fwd[1] * c.dist); c.tz = c.ez + cam.fwd[2] * c.dist; }
  S.fly = on;
  $("flyBtn").classList.toggle("on", on);
  status(on ? "Fly camera: hold the right mouse button to look, WASD to move, Space/Ctrl up and down, middle mouse to slide." : "Orbit camera: middle mouse drags the map, right mouse turns it, the wheel zooms.");
}
function frameLevel() {
  const lv = S.level, c = S.cam;
  c.tx = lv.width / 2; c.tz = lv.height / 2; c.ty = 0;
  c.dist = Math.max(lv.width, lv.height) * 1.05; c.pitch = 0.95; c.yaw = 0.5;
  if (S.fly) setFly(false);
}

// ---- Drawing ----------------------------------------------------------------------------------------------------
const flat = [];
function line(a, b, col) { flat.push(a[0], a[1], a[2], ...col, b[0], b[1], b[2], ...col); }
function box(x0, y0, z0, x1, y1, z1, col) {
  const p = [[x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1], [x0, y1, z0], [x1, y1, z0], [x1, y1, z1], [x0, y1, z1]];
  for (const [a, b] of [[0, 1], [1, 2], [2, 3], [3, 0], [4, 5], [5, 6], [6, 7], [7, 4], [0, 4], [1, 5], [2, 6], [3, 7]]) line(p[a], p[b], col);
}
const tris = [];
function quad(p, col) { for (const i of [0, 1, 2, 0, 2, 3]) tris.push(p[i][0], p[i][1], p[i][2], ...col); }
function solidBox(x0, y0, z0, x1, y1, z1, col) {
  const p = [[x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1], [x0, y1, z0], [x1, y1, z0], [x1, y1, z1], [x0, y1, z1]];
  for (const f of [[4, 5, 6, 7], [0, 1, 5, 4], [1, 2, 6, 5], [2, 3, 7, 6], [3, 0, 4, 7]]) quad(f.map((i) => p[i]), col);
}
function drawFlat(vp, data, mode) {
  if (!data.length) return;
  gl.useProgram(FLAT.p);
  gl.uniformMatrix4fv(FLAT.u.uVP, false, vp);
  gl.bindVertexArray(flatVao);
  gl.bindBuffer(gl.ARRAY_BUFFER, flatBuf);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(data), gl.DYNAMIC_DRAW);
  gl.drawArrays(mode, 0, data.length / 7);
}
const cellY = (x, z) => floorOf(x, z) / 512 + (rampOf(x, z) !== undefined ? 0.125 : 0);

function brushCells(x, z) {
  const r = S.brush >> 1, out = [];
  for (let dz = -r; dz <= r; dz++) for (let dx = -r; dx <= r; dx++) out.push([x + dx, z + dz]);
  return out;
}
function doorPreview(x, z) {
  const axis = S.doorAxis || (Math.abs(Math.sin(S.cam.yaw)) > Math.abs(Math.cos(S.cam.yaw)) ? "z" : "x");
  return axis === "x" ? { x: x - 1, z, axis } : { x, z: z - 1, axis };
}

function render() {
  resize();
  const cam = camera();
  gl.viewport(0, 0, canvas.width, canvas.height);
  gl.clearColor(0.05, 0.06, 0.05, 1);
  gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
  gl.enable(gl.DEPTH_TEST);
  if (levelCount && atlasTex) {
    gl.useProgram(LEVEL.p);
    gl.uniformMatrix4fv(LEVEL.u.uVP, false, cam.vp);
    gl.uniform3fv(LEVEL.u.uEye, cam.eye);
    gl.uniform1f(LEVEL.u.uCeil, S.showCeil || S.fly ? 1 : 0);
    gl.uniform1f(LEVEL.u.uPick, 0);
    gl.uniform2f(LEVEL.u.uAtlasTiles, S.set.cols, Math.ceil(S.set.count / S.set.cols));
    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, atlasTex);
    gl.uniform1i(LEVEL.u.uAtlas, 0);
    gl.bindVertexArray(levelVao);
    gl.drawArrays(gl.TRIANGLES, 0, levelCount);
  }
  const lv = S.level;
  if (!lv) return;
  // Translucent overlays: doors, spawn-room tint, objects.
  tris.length = 0; flat.length = 0;
  for (const d of lv.doors) {
    const y = cellY(d.x, d.z);
    const c = doorCells(d);
    const x0 = Math.min(...c.map((p) => p[0])), z0 = Math.min(...c.map((p) => p[1]));
    const x1 = Math.max(...c.map((p) => p[0])) + 1, z1 = Math.max(...c.map((p) => p[1])) + 1;
    const thin = d.axis === "x" ? [x0, y, z0 + 0.42, x1, y + S.info.limits.door / 512, z1 - 0.42] : [x0 + 0.42, y, z0, x1 - 0.42, y + S.info.limits.door / 512, z1];
    solidBox(...thin, [1.0, 0.78, 0.2, S.fly ? 0.22 : 0.55]);
  }
  for (const e of lv.enemies) {
    const y = cellY(e.x, e.z), big = e.kind === 7;
    const s = big ? 0.9 : 0.28, h = big ? 4.5 : e.kind <= 3 ? 0.5 : 1.6;
    solidBox(e.x + 0.5 - s, y, e.z + 0.5 - s, e.x + 0.5 + s, y + h, e.z + 0.5 + s, big ? [1, 0.15, 0.15, 0.85] : [0.92, 0.32, 0.3, 0.85]);
  }
  for (const p of lv.pickups) {
    const y = cellY(p.x, p.z);
    solidBox(p.x + 0.33, y, p.z + 0.33, p.x + 0.67, y + 0.34, p.z + 0.67, [0.25, 0.82, 1, 0.9]);
  }
  if (!(S.fly && Math.hypot(S.cam.ex - lv.start[0] - 0.5, S.cam.ez - lv.start[1] - 0.5) < 1.2)) {
    const [sx, sz] = lv.start, y = cellY(sx, sz);
    solidBox(sx + 0.3, y, sz + 0.3, sx + 0.7, y + 1.5, sz + 0.7, [0.25, 0.5, 1, 0.9]);
    const a = startAngle(lv.start_dir ?? 4);
    const cx = sx + 0.5, cz = sz + 0.5, dx = Math.sin(a), dz = Math.cos(a);
    quad([[cx - dz * 0.25, y + 0.03, cz + dx * 0.25], [cx + dx * 0.9, y + 0.03, cz + dz * 0.9], [cx + dz * 0.25, y + 0.03, cz - dx * 0.25], [cx, y + 0.03, cz]], [0.4, 0.7, 1, 0.95]);
  }
  for (let z = 0; z < lv.height; z++) for (let x = 0; x < lv.width; x++) if (lv.grid[z][x] === "R") {
    const y = cellY(x, z) + 0.01;
    quad([[x, y, z], [x, y, z + 1], [x + 1, y, z + 1], [x + 1, y, z]], [0.35, 0.45, 1, 0.3]);
  }
  // Grid lines over the floors.
  if (S.showGrid && !S.fly) {
    const col = [0.55, 0.9, 0.55, 0.16];
    for (let z = 0; z < lv.height; z++) for (let x = 0; x < lv.width; x++) {
      if (!isOpen(x, z) && cellType(x, z) !== "D") continue;
      const y = cellY(x, z) + 0.005;
      line([x, y, z], [x + 1, y, z], col); line([x, y, z], [x, y, z + 1], col);
    }
  }
  // Hover: the face outline, the brush, the door preview.
  const h = S.hover;
  if (h) {
    const col = [1, 0.85, 0.25, 1];
    if (h.face !== undefined && ["paint", "ceiling"].includes(S.tool)) outlineFace(h.face, col);
    const cells = ["dig", "fill", "raise", "flatten", "ceiling", "room", "hive", "erase"].includes(S.tool) ? brushCells(h.x, h.z)
      : S.tool === "door" ? doorCells(doorPreview(h.x, h.z)) : [[h.x, h.z]];
    for (const [x, z] of cells) {
      if (x < 0 || z < 0 || x >= lv.width || z >= lv.height) continue;
      const y = (isOpen(x, z) || cellType(x, z) === "D" ? cellY(x, z) : 0) + 0.02;
      box(x + 0.03, y, z + 0.03, x + 0.97, y + (S.tool === "fill" || S.tool === "hive" ? 1.5 : 0.04), z + 0.97, col);
    }
    if (S.tool === "ramp" && isOpen(h.x, h.z)) {
      const d = rampAway(), y = floorOf(h.x, h.z) / 512 + 0.05, cx = h.x + 0.5, cz = h.z + 0.5;
      const v = [[0, 1], [1, 0], [-1, 0], [0, -1]][d];
      line([cx - v[0] * 0.4, y, cz - v[1] * 0.4], [cx + v[0] * 0.4, y + 0.25, cz + v[1] * 0.4], [1, 1, 1, 1]);
    }
  }
  if (S.carry) {
    const c = [0.9, 0.9, 1, 1];
    if (h) box(h.x + 0.1, cellY(h.x, h.z), h.z + 0.1, h.x + 0.9, cellY(h.x, h.z) + 1.2, h.z + 0.9, c);
  }
  gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
  gl.depthMask(false);
  drawFlat(cam.vp, tris, gl.TRIANGLES);
  gl.disable(gl.DEPTH_TEST);
  const hoverLines = flat.splice(0);
  drawFlat(cam.vp, hoverLines, gl.LINES);
  gl.enable(gl.DEPTH_TEST); gl.depthMask(true); gl.disable(gl.BLEND);
  drawLabels(cam.vp);
  const deg = Math.round(((S.cam.yaw * 180 / Math.PI) % 360 + 360) % 360);
  $("compass").textContent = `${S.fly ? "Fly" : "Orbit"} · facing ${["north", "north-west", "west", "south-west", "south", "south-east", "east", "north-east"][Math.round(deg / 45) % 8]}`;
}
function outlineFace(f, col) {
  const g = S.geo, ox = g.offset[0], oz = g.offset[1], p = [];
  for (let i = 0; i < 4; i++) { const b = (f * 4 + i) * 3; p.push([(g.pos[b] + ox) / 512, g.pos[b + 1] / 512, (g.pos[b + 2] + oz) / 512]); }
  for (let i = 0; i < 4; i++) line(p[i], p[(i + 1) % 4], col);
}
// start_dir 0-7; 4 faces -z ("up" on the old top-down map), turning a quarter per 2.
const startAngle = (d) => Math.PI - d * Math.PI / 4;
function rampAway() {
  const f = camera().fwd;
  return Math.abs(f[0]) > Math.abs(f[2]) ? (f[0] > 0 ? 1 : 2) : (f[2] > 0 ? 0 : 3);
}

const labelPool = [];
function drawLabels(vp) {
  const lv = S.level, el = $("labels"), w = canvas.clientWidth, h = canvas.clientHeight;
  const items = [];
  for (const e of lv.enemies) items.push([e.x + 0.5, cellY(e.x, e.z) + (e.kind === 7 ? 4.7 : 1.8), e.z + 0.5, S.enemyName[e.kind] || "Enemy", "#ff9a90"]);
  if (S.cam.dist < 70 || S.fly) for (const p of lv.pickups) items.push([p.x + 0.5, cellY(p.x, p.z) + 0.5, p.z + 0.5, S.pickupName[p.kind] || "Pickup", "#8fe0ff"]);
  items.push([lv.start[0] + 0.5, cellY(...lv.start) + 1.7, lv.start[1] + 0.5, "Player start", "#9fc0ff"]);
  let n = 0;
  for (const [x, y, z, text, col] of items) {
    const p = M4.apply(vp, [x, y, z]);
    if (p[2] <= 0.05 || Math.abs(p[0]) > 1.1 || Math.abs(p[1]) > 1.1) continue;
    let d = labelPool[n];
    if (!d) { d = document.createElement("div"); el.appendChild(d); labelPool.push(d); }
    d.style.display = "block";
    d.style.left = ((p[0] + 1) / 2 * w) + "px"; d.style.top = ((1 - p[1]) / 2 * h) + "px";
    if (d.textContent !== text) d.textContent = text;
    d.style.color = col;
    n++;
  }
  for (let i = n; i < labelPool.length; i++) labelPool[i].style.display = "none";
}

// ---- Picking ----------------------------------------------------------------------------------------------------
function pickAt(px, py) {
  if (!levelCount || !atlasTex) return null;
  const dpr = window.devicePixelRatio || 1, x = Math.round(px * dpr), y = canvas.height - 1 - Math.round(py * dpr);
  const cam = camera();
  gl.bindFramebuffer(gl.FRAMEBUFFER, pickFb);
  gl.viewport(0, 0, pickW, pickH);
  gl.enable(gl.SCISSOR_TEST); gl.scissor(x, y, 1, 1);
  gl.clearColor(0, 0, 0, 0); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
  gl.enable(gl.DEPTH_TEST);
  gl.useProgram(LEVEL.p);
  gl.uniformMatrix4fv(LEVEL.u.uVP, false, cam.vp);
  gl.uniform3fv(LEVEL.u.uEye, cam.eye);
  gl.uniform1f(LEVEL.u.uCeil, S.showCeil || S.fly ? 1 : 0);
  gl.uniform1f(LEVEL.u.uPick, 1);
  gl.bindVertexArray(levelVao);
  gl.drawArrays(gl.TRIANGLES, 0, levelCount);
  const px4 = new Uint8Array(4);
  gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px4);
  gl.disable(gl.SCISSOR_TEST);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  const id = px4[0] | (px4[1] << 8) | (px4[2] << 16);
  if (!id) return null;
  if (id >= SOLID_BASE) { const c = id - SOLID_BASE; return { x: c % S.level.width, z: Math.floor(c / S.level.width), solid: true }; }
  const f = id - 1, m = S.geo.meta;
  const hit = { face: f, x: m[f * 3], z: m[f * 3 + 1], part: m[f * 3 + 2] };
  // A wall face belongs to the open cell in front of it; for building, the cell behind it is the one you mean.
  if (hit.part >= 2) {
    const [dx, dz] = [[-1, 0], [1, 0], [0, -1], [0, 1]][hit.part - 2];
    hit.behind = [hit.x + dx, hit.z + dz];
  }
  return hit;
}

// ---- Tools ------------------------------------------------------------------------------------------------------
let stroke = null;   // {touched:Set, base}
function targetCell(h, wantSolid) {
  // Floor tools act on the floor cell; wall tools (dig, door) on the solid cell behind a clicked wall.
  if (wantSolid && h.behind) return h.behind;
  return [h.x, h.z];
}
function act(h, first, e) {
  const lv = S.level, tool = S.tool;
  if (!h) return false;
  const shift = e.shiftKey, alt = e.altKey, ctrl = e.ctrlKey;
  let [x, z] = targetCell(h, ["dig", "door", "hive"].includes(tool) || (tool === "erase" && h.behind && cellType(...h.behind) === "D"));
  let did = false;
  const each = (fn) => {
    for (const [cx, cz] of brushCells(x, z)) {
      if (!interior(cx, cz)) continue;
      const k = key(cx, cz);
      if (stroke.touched.has(k)) continue;
      stroke.touched.add(k);
      if (fn(cx, cz, k) !== false) did = true;
    }
  };
  switch (tool) {
    case "dig":
      each((cx, cz) => {
        if (lv.grid[cz][cx] === ".") return false;
        lv.doors = lv.doors.filter((d) => !doorCells(d).some(([a, b]) => a === cx && b === cz));
        setCell(cx, cz, ".");
        if (h.part === 0 || h.part === undefined) { /* keep */ } else if (!(key(cx, cz) in lv.floor)) {
          const f = floorOf(h.x, h.z); if (f) lv.floor[key(cx, cz)] = f;   // dig sideways: continue the floor you stand on
        }
      });
      break;
    case "fill": case "hive":
      each((cx, cz, k) => {
        const ch = tool === "fill" ? "#" : "W";
        if (lv.grid[cz][cx] === ch) return false;
        setCell(cx, cz, ch); removeThings(cx, cz);
        lv.doors = lv.doors.filter((d) => !doorCells(d).some(([a, b]) => a === cx && b === cz));
        delete lv.ramp[k];
      });
      break;
    case "room":
      each((cx, cz) => { if (lv.grid[cz][cx] === "R") return false; setCell(cx, cz, "R"); });
      break;
    case "raise":
      each((cx, cz, k) => {
        if (!isOpen(cx, cz)) return false;
        const v = clamp(floorOf(cx, cz) + (shift ? -S.step : S.step), 0, S.info.limits.max_floor);
        if (v === floorOf(cx, cz)) return false;
        if (v) lv.floor[k] = v; else delete lv.floor[k];
        if (lv.ceil[k] !== undefined && lv.ceil[k] < v + S.info.limits.headroom) lv.ceil[k] = v + S.info.limits.headroom;
      });
      break;
    case "flatten":
      if (first) stroke.base = floorOf(x, z);
      each((cx, cz, k) => {
        if (!isOpen(cx, cz)) return false;
        const changedRamp = k in lv.ramp; delete lv.ramp[k];
        if (floorOf(cx, cz) === stroke.base && !changedRamp) return false;
        if (stroke.base) lv.floor[k] = stroke.base; else delete lv.floor[k];
      });
      break;
    case "ceiling":
      each((cx, cz, k) => {
        if (!isOpen(cx, cz)) return false;
        if (ctrl) { if (!(k in lv.ceil)) return false; delete lv.ceil[k]; return; }
        const geoCeil = ceilingFromGeo(cx, cz);
        const now = lv.ceil[k] ?? geoCeil;
        lv.ceil[k] = clamp(Math.round((now + (shift ? -S.step : S.step)) / 32) * 32, floorOf(cx, cz) + S.info.limits.headroom, S.info.limits.max_ceil);
      });
      break;
    case "ramp":
      if (!first || !isOpen(x, z) || cellType(x, z) === "D") return false;
      { const k = key(x, z);
        if (shift) { if (!(k in lv.ramp)) return false; delete lv.ramp[k]; }
        else if (k in lv.ramp) lv.ramp[k] = [1, 3, 0, 2][lv.ramp[k]];   // turn a quarter
        else lv.ramp[k] = rampAway();
        did = true; }
      break;
    case "paint":
      if (h.face === undefined) return false;
      if (alt) { S.texture = S.geo.tex[h.face]; drawPalette(); status(`Picked up texture ${S.texture}.`); return false; }
      if (S.texture === null) { status("Pick a texture in the Textures tab first."); return false; }
      did = paint(h, shift && first);
      break;
    case "door":
      if (!first) return false;
      { const d = doorPreview(x, z), cells = doorCells(d);
        if (cells.some(([a, b]) => !interior(a, b))) { status("A door is 4 cells long: that runs off the map."); return false; }
        lv.doors = lv.doors.filter((o) => !doorCells(o).some(([a, b]) => cells.some(([c, e]) => a === c && b === e)));
        lv.doors.push(d);
        for (const [a, b] of cells) { setCell(a, b, "#"); removeThings(a, b); delete lv.ramp[key(a, b)]; }
        did = true; }
      break;
    case "start":
      if (!first) return false;
      if (lv.start[0] === x && lv.start[1] === z) lv.start_dir = ((lv.start_dir ?? 4) + 2) % 8;
      else { lv.start = [x, z]; if (!isOpen(x, z)) setCell(x, z, "."); }
      did = true;
      break;
    case "enemy": case "pickup":
      if (!first) return false;
      if (!isOpen(x, z)) { status("Place things on a floor."); return false; }
      if (tool === "enemy") {
        if (S.enemy === 7) lv.enemies = lv.enemies.filter((o) => o.kind !== 7);
        removeThings(x, z); lv.enemies.push({ kind: S.enemy, x, z });
      } else { removeThings(x, z); lv.pickups.push({ kind: S.pickup, x, z }); }
      did = true;
      break;
    case "erase":
      each((cx, cz, k) => {
        const before = JSON.stringify([lv.enemies.length, lv.pickups.length, lv.doors.length, k in lv.tex, k in lv.ceil, k in lv.floor, k in lv.ramp]);
        removeThings(cx, cz);
        lv.doors = lv.doors.filter((d) => !doorCells(d).some(([a, b]) => a === cx && b === cz));
        delete lv.tex[k]; delete lv.ceil[k]; delete lv.floor[k]; delete lv.ramp[k];
        if (lv.grid[cz][cx] === "R") setCell(cx, cz, ".");
        return before !== JSON.stringify([lv.enemies.length, lv.pickups.length, lv.doors.length, k in lv.tex, k in lv.ceil, k in lv.floor, k in lv.ramp]);
      });
      break;
    case "select":
      if (!first) return false;
      return selectClick(x, z, h);
  }
  return did;
}
function ceilingFromGeo(x, z) {
  const f = S.faceOf[`${x},${z},1`];
  if (f === undefined) return floorOf(x, z) + 2048;
  let lo = Infinity;
  for (let i = 0; i < 4; i++) lo = Math.min(lo, S.geo.pos[(f * 4 + i) * 3 + 1]);
  return lo;
}
function paint(h, flood) {
  const lv = S.level, t = S.texture, part = h.part;
  const slotFor = (p) => p === 0 ? 0 : p === 1 ? 2 : 3 + (p - 2);
  const setTex = (x, z, p) => {
    const k = key(x, z), own = (lv.tex[k] || [null, null, null]).slice();
    while (own.length < 7) own.push(null);
    if (p >= 2 && $("allWalls").checked) { own[1] = t; own[3] = own[4] = own[5] = own[6] = null; }
    else own[slotFor(p)] = t;
    while (own.length > 3 && own[own.length - 1] === null) own.pop();
    lv.tex[k] = own;
  };
  if (flood && part <= 1) {
    const from = S.geo.tex[h.face], seen = new Set([key(h.x, h.z)]), todo = [[h.x, h.z]];
    let n = 0;
    while (todo.length && n < 4000) {
      const [x, z] = todo.pop(); setTex(x, z, part); n++;
      for (const [nx, nz] of [[x + 1, z], [x - 1, z], [x, z + 1], [x, z - 1]]) {
        const k = key(nx, nz), f = S.faceOf[`${nx},${nz},${part}`];
        if (seen.has(k) || f === undefined || S.geo.tex[f] !== from) continue;
        seen.add(k); todo.push([nx, nz]);
      }
    }
    status(`Painted ${n} ${PART_NAMES[part]}s.`);
    return true;
  }
  setTex(h.x, h.z, part);
  return true;
}
function selectClick(x, z, h) {
  const lv = S.level;
  if (S.carry) {
    if (!isOpen(x, z)) { status("Put it down on a floor."); return false; }
    const c = S.carry; S.carry = null;
    if (c.type === "start") lv.start = [x, z];
    else { const list = c.type === "enemy" ? lv.enemies : lv.pickups; removeThings(x, z); list.push({ ...c.item, x, z }); }
    status("Put down.");
    return true;
  }
  const ei = lv.enemies.findIndex((e) => e.x === x && e.z === z), pi = lv.pickups.findIndex((p) => p.x === x && p.z === z);
  if (ei >= 0) { S.carry = { type: "enemy", item: lv.enemies.splice(ei, 1)[0] }; status(`Carrying ${S.enemyName[S.carry.item.kind]}: click a floor to put it down (Esc puts it back).`); S.carryFrom = [x, z]; return true; }
  if (pi >= 0) { S.carry = { type: "pickup", item: lv.pickups.splice(pi, 1)[0] }; status(`Carrying ${S.pickupName[S.carry.item.kind]}: click a floor to put it down.`); S.carryFrom = [x, z]; return true; }
  if (lv.start[0] === x && lv.start[1] === z) { S.carry = { type: "start" }; status("Carrying the player start: click a floor to put it down."); return false; }
  status(describe(x, z, h));
  return false;
}
function describe(x, z, h) {
  const lv = S.level, k = key(x, z), t = cellType(x, z);
  const parts = [{ "#": "Wall", ".": "Floor", R: "Spawn room", W: "Hive wall", D: "Door" }[t] || t];
  if (isOpen(x, z) || t === "D") {
    parts.push(`floor ${floorOf(x, z)}`);
    if (rampOf(x, z) !== undefined) parts.push(`ramp rising ${["south", "east", "west", "north"][rampOf(x, z)]}`);
    parts.push(lv.ceil[k] !== undefined ? `ceiling ${lv.ceil[k]}` : `ceiling auto (${ceilingFromGeo(x, z)})`);
  }
  for (const e of lv.enemies) if (e.x === x && e.z === z) parts.push(S.enemyName[e.kind]);
  for (const p of lv.pickups) if (p.x === x && p.z === z) parts.push(S.pickupName[p.kind]);
  if (lv.start[0] === x && lv.start[1] === z) parts.push("player start");
  if (h && h.face !== undefined) parts.push(`${PART_NAMES[h.part]}: texture ${S.geo.tex[h.face]}`);
  return `Cell ${x}, ${z}: ${parts.join(", ")}`;
}

// ---- Mouse and keyboard -----------------------------------------------------------------------------------------
let drag = null;   // {button, x, y}
canvas.addEventListener("contextmenu", (e) => e.preventDefault());
canvas.addEventListener("mousedown", (e) => {
  e.preventDefault();
  canvas.focus?.();
  drag = { button: e.button, x: e.clientX, y: e.clientY, moved: 0 };
  if (e.button === 0) {
    const h = pickAt(e.offsetX, e.offsetY);
    S.hover = h;
    snapshot();
    stroke = { touched: new Set(), did: false };
    if (act(h, true, e)) { stroke.did = true; changed(); }
  }
});
window.addEventListener("mouseup", (e) => {
  if (drag && drag.button === 0 && stroke) {
    if (!stroke.did) S.undo.pop();   // nothing changed: no undo step
    stroke = null;
  }
  drag = null;
});
canvas.addEventListener("mousemove", (e) => {
  const c = S.cam;
  if (drag) {
    const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
    drag.x = e.clientX; drag.y = e.clientY; drag.moved += Math.abs(dx) + Math.abs(dy);
    if (drag.button === 1 || (drag.button === 2 && e.shiftKey)) {
      // Middle mouse: grab the map and slide it (orbit), or slide the camera (fly).
      const k = (S.fly ? 0.03 : c.dist * 0.0016);
      const rx = Math.cos(c.yaw), rz = -Math.sin(c.yaw);
      if (S.fly) { c.ex -= rx * dx * k; c.ez -= rz * dx * k; c.ey += dy * k; }
      else {
        const fx = -Math.sin(c.yaw), fz = -Math.cos(c.yaw);
        c.tx -= rx * dx * k - fx * dy * k; c.tz -= rz * dx * k - fz * dy * k;
      }
    } else if (drag.button === 2) {
      c.yaw -= dx * 0.006;
      c.pitch = clamp(c.pitch + dy * 0.005 * (S.fly ? -1 : 1), S.fly ? -1.5 : 0.08, 1.55);
    } else if (drag.button === 0 && stroke) {
      // Sample along the movement so a quick stroke still covers every cell it crosses.
      const steps = Math.max(1, Math.ceil(Math.hypot(dx, dy) / 6));
      let any = false, h = null;
      for (let i = steps - 1; i >= 0; i--) {
        h = pickAt(e.offsetX - dx * i / steps, e.offsetY - dy * i / steps);
        if (h && act(h, false, e)) any = true;
      }
      S.hover = h;
      if (any) { stroke.did = true; changed(); }
    }
    return;
  }
  S.hover = pickAt(e.offsetX, e.offsetY);
  if (S.hover) status(describe(S.hover.x, S.hover.z, S.hover) + (S.hover.behind && cellType(...S.hover.behind) !== "." ? `  ·  behind it: cell ${S.hover.behind[0]}, ${S.hover.behind[1]}` : ""));
});
canvas.addEventListener("mouseleave", () => { S.hover = null; });
canvas.addEventListener("wheel", (e) => {
  e.preventDefault();
  const c = S.cam, s = Math.sign(e.deltaY);
  if (S.fly) { const f = camera().fwd, k = -s * 1.5; c.ex += f[0] * k; c.ey += f[1] * k; c.ez += f[2] * k; }
  else {
    // Zoom toward the point under the cursor.
    const h = pickAt(e.offsetX, e.offsetY);
    const old = c.dist; c.dist = clamp(c.dist * (s > 0 ? 1.12 : 1 / 1.12), 1.5, 260);
    if (h && s < 0) { const t = 1 - c.dist / old, y = isOpen(h.x, h.z) ? cellY(h.x, h.z) : 0; c.tx += (h.x + 0.5 - c.tx) * t; c.tz += (h.z + 0.5 - c.tz) * t; c.ty += (y - c.ty) * t; }
  }
}, { passive: false });

const TOOL_KEYS = {};
for (const [id, , k] of TOOLS) TOOL_KEYS[k.toLowerCase()] = id;
window.addEventListener("keydown", (e) => {
  if (e.target.matches("input, select, textarea") || $("modal").classList.contains("on")) {
    if (e.key === "Escape") closeModal();
    return;
  }
  const k = e.key.toLowerCase();
  if (e.ctrlKey && k === "z") { e.preventDefault(); undo(); return; }
  if (e.ctrlKey && k === "y") { e.preventDefault(); redo(); return; }
  if (e.ctrlKey && k === "s") { e.preventDefault(); save(false); return; }
  if (e.ctrlKey) return;
  if (k === "tab") { e.preventDefault(); setFly(!S.fly); return; }
  if (["w", "a", "s", "d", "q", "e", " ", "shift", "arrowup", "arrowdown", "arrowleft", "arrowright"].includes(k) && !e.repeat) S.keys.add(k);
  if (["w", "a", "s", "d", "q", "e", " "].includes(k)) { e.preventDefault(); return; }
  if (k === "f") frameLevel();
  else if (k === "c") toggleCeil();
  else if (k === "g") toggleGrid();
  else if (k === "r") rotateThing();
  else if (k === "escape") { if (S.carry) { putBack(); } }
  else if (k === "delete" || k === "backspace") { if (S.carry) { S.carry = null; S.dirty = true; changed(); status("Removed."); } }
  else if (k === "[" || k === "]") setBrush(clamp(S.brush + (k === "]" ? 2 : -2), 1, 7));
  else if (TOOL_KEYS[k]) setTool(TOOL_KEYS[k]);
});
window.addEventListener("keyup", (e) => S.keys.delete(e.key.toLowerCase()));
window.addEventListener("blur", () => S.keys.clear());
function putBack() {
  const c = S.carry; S.carry = null;
  if (c && c.type !== "start" && S.carryFrom) { const list = c.type === "enemy" ? S.level.enemies : S.level.pickups; list.push({ ...c.item, x: S.carryFrom[0], z: S.carryFrom[1] }); }
  S.undo.pop(); status("Put back.");
}
function rotateThing() {
  const lv = S.level;
  if (S.tool === "door") { S.doorAxis = (S.doorAxis || (Math.abs(Math.sin(S.cam.yaw)) > Math.abs(Math.cos(S.cam.yaw)) ? "z" : "x")) === "x" ? "z" : "x"; status(`Doors now run ${S.doorAxis === "x" ? "east-west" : "north-south"}.`); return; }
  const h = S.hover;
  if (S.tool === "ramp" && h && rampOf(h.x, h.z) !== undefined) { snapshot(); lv.ramp[key(h.x, h.z)] = [1, 3, 0, 2][rampOf(h.x, h.z)]; changed(); return; }
  snapshot(); lv.start_dir = ((lv.start_dir ?? 4) + 2) % 8; changed();
}
let last = performance.now();
function tick(now) {
  const dt = Math.min(0.05, (now - last) / 1000); last = now;
  const c = S.cam, K = S.keys;
  if (K.size) {
    const fwd = (K.has("w") || K.has("arrowup") ? 1 : 0) - (K.has("s") || K.has("arrowdown") ? 1 : 0);
    const side = (K.has("d") || K.has("arrowright") ? 1 : 0) - (K.has("a") || K.has("arrowleft") ? 1 : 0);
    if (S.fly) {
      const f = camera().fwd, sp = (K.has("shift") ? 30 : 10) * dt, rx = Math.cos(c.yaw), rz = -Math.sin(c.yaw);
      c.ex += (f[0] * fwd + rx * side) * sp; c.ey += f[1] * fwd * sp; c.ez += (f[2] * fwd + rz * side) * sp;
      if (K.has(" ")) c.ey += sp; if (K.has("q")) c.ey -= sp; if (K.has("e")) c.ey += sp;
    } else {
      const sp = c.dist * (K.has("shift") ? 2.2 : 0.9) * dt, fx = -Math.sin(c.yaw), fz = -Math.cos(c.yaw), rx = Math.cos(c.yaw), rz = -Math.sin(c.yaw);
      c.tx += (fx * fwd + rx * side) * sp; c.tz += (fz * fwd + rz * side) * sp;
      if (K.has("q")) c.yaw += 1.8 * dt; if (K.has("e")) c.yaw -= 1.8 * dt;
    }
  }
  render();
  requestAnimationFrame(tick);
}

// ---- Panels -----------------------------------------------------------------------------------------------------
function status(t) { $("status").textContent = t; }
function setTool(id) {
  if (S.carry && id !== "select") putBack();
  S.tool = id;
  for (const b of document.querySelectorAll("#tools button")) b.classList.toggle("on", b.dataset.tool === id);
  $("hint").textContent = TOOLS.find((t) => t[0] === id)[3];
  if (id === "paint") showTab("textures");
  if (id === "enemy" || id === "pickup") showTab("objects");
}
function setBrush(n) {
  S.brush = n;
  for (const b of document.querySelectorAll("#brush button")) b.classList.toggle("on", +b.dataset.v === n);
}
function setStep(n) {
  S.step = n;
  for (const b of document.querySelectorAll("#step button")) b.classList.toggle("on", +b.dataset.v === n);
}
function toggleCeil() { S.showCeil = !S.showCeil; $("ceilBtn").classList.toggle("on", S.showCeil); }
function toggleGrid() { S.showGrid = !S.showGrid; $("gridBtn").classList.toggle("on", S.showGrid); }
function showTab(name) {
  for (const b of document.querySelectorAll("#tabs button")) b.classList.toggle("on", b.dataset.tab === name);
  for (const t of document.querySelectorAll(".tab")) t.classList.toggle("on", t.id === "tab-" + name);
}
function updateTitle() {
  document.title = `${S.level?.name || "Level"}${S.dirty ? " *" : ""} - Alien Trilogy Level Editor`;
}

// Texture palette: the set's usable textures from the atlas.
const PAL_COLS = 6;
function drawPalette() {
  const cv = $("palette"), set = S.set;
  if (!set || !atlasImg) return;
  const size = Math.floor((cv.parentElement.clientWidth - 22) / PAL_COLS);
  const list = set.usable;
  cv.width = size * PAL_COLS; cv.height = Math.ceil(list.length / PAL_COLS) * size;
  cv.style.height = cv.height + "px";
  const g = cv.getContext("2d");
  g.imageSmoothingEnabled = false;
  list.forEach((t, i) => {
    const sx = (t % set.cols) * set.tile, sy = Math.floor(t / set.cols) * set.tile;
    const dx = (i % PAL_COLS) * size, dy = Math.floor(i / PAL_COLS) * size;
    g.drawImage(atlasImg, sx, sy, set.tile, set.tile, dx + 1, dy + 1, size - 2, size - 2);
    if (t === S.texture) { g.strokeStyle = "#ffd040"; g.lineWidth = 3; g.strokeRect(dx + 1.5, dy + 1.5, size - 3, size - 3); }
  });
  cv.onclick = (e) => {
    const r = cv.getBoundingClientRect(), i = Math.floor((e.clientY - r.top) / size) * PAL_COLS + Math.floor((e.clientX - r.left) / size);
    if (i >= 0 && i < list.length) { S.texture = list[i]; drawPalette(); setTool("paint"); }
  };
  const ct = $("currentTex").getContext("2d");
  ct.imageSmoothingEnabled = false; ct.clearRect(0, 0, 64, 64);
  if (S.texture !== null) {
    ct.drawImage(atlasImg, (S.texture % set.cols) * set.tile, Math.floor(S.texture / set.cols) * set.tile, set.tile, set.tile, 0, 0, 64, 64);
    const where = set.floors.includes(S.texture) ? " (a floor of this look)" : set.walls.includes(S.texture) ? " (a wall of this look)" : set.ceilings.includes(S.texture) ? " (a ceiling of this look)" : "";
    $("currentName").textContent = `Texture ${S.texture}${where}. Paint tool (T): click any floor, wall or ceiling.`;
  }
}
async function loadSet(code) {
  S.set = await api("/api/set?code=" + code);
  const img = new Image();
  await new Promise((ok, fail) => {
    img.onload = ok; img.onerror = () => fail(new Error("the textures could not be loaded"));
    img.src = "/api/atlas?code=" + code + "&t=" + Date.now();
  });
  atlasImg = img;
  if (!atlasTex) atlasTex = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, atlasTex);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  if (S.texture !== null && !S.set.usable.includes(S.texture)) S.texture = null;
  drawPalette();
}
function fillLevelForm() {
  const lv = S.level;
  $("f-name").value = lv.name || "";
  $("f-code").value = lv.code;
  $("f-mode").value = lv.mode || "survival";
  $("f-lives").value = lv.lives ?? 5;
  $("f-waves").value = lv.waves ?? 10;
  $("f-roster").value = lv.roster || "campaign";
  $("f-brightness").value = lv.brightness || "normal";
  $("f-queen").checked = lv.queen_last_wave ?? true;
  $("f-size").textContent = `${lv.width} x ${lv.height}`;
  document.querySelectorAll("#f-weapons input").forEach((b, i) => { b.checked = (lv.weapons || [0]).includes(i); });
}
function formChanged(e) {
  const lv = S.level, id = e.target.id;
  snapshot();
  if (id === "f-code") {
    if (Object.keys(lv.tex).length && !confirm("Texture numbers belong to a look: changing it clears the textures you painted. Go ahead?")) { e.target.value = lv.code; S.undo.pop(); return; }
    lv.code = e.target.value; lv.tex = {};
    loadSet(lv.code).then(() => changed());
    return;
  }
  if (id === "f-name") lv.name = e.target.value.trim() || "My level";
  else if (id === "f-mode") lv.mode = e.target.value;
  else if (id === "f-lives") lv.lives = clamp(+e.target.value || 5, 1, 99);
  else if (id === "f-waves") lv.waves = clamp(+e.target.value || 10, 1, 30);
  else if (id === "f-roster") lv.roster = e.target.value;
  else if (id === "f-brightness") lv.brightness = e.target.value;
  else if (id === "f-queen") lv.queen_last_wave = e.target.checked;
  else if (id.startsWith("w-")) lv.weapons = [...document.querySelectorAll("#f-weapons input")].map((b, i) => b.checked ? i : -1).filter((i) => i >= 0);
  if (!lv.weapons?.length) lv.weapons = [0];
  changed();
}

// ---- Files ------------------------------------------------------------------------------------------------------
function modal(html) { $("modalBox").innerHTML = html; $("modal").classList.add("on"); }
function closeModal() { $("modal").classList.remove("on"); }
async function setLevel(level, file) {
  S.level = normalise(level); S.file = file || null; S.dirty = false; S.undo = []; S.redo = []; S.carry = null;
  if (!S.set || S.set.code !== S.level.code) await loadSet(S.level.code);
  fillLevelForm(); frameLevel(); updateTitle();
  await rebuild(); runChecks();
}
function guard() { return !S.dirty || confirm("The level has unsaved changes. Carry on without saving?"); }
async function cmdNew() {
  if (!guard()) return;
  modal(`<h2>New level</h2>
    <div class="row"><label><input type="radio" name="nk" value="room" checked> Blank room</label></div>
    <div class="row" style="margin-left:22px"><label>Size</label><select id="nsize"><option>24x24</option><option selected>40x32</option><option>64x48</option><option>96x80</option></select></div>
    <div class="row"><label><input type="radio" name="nk" value="arena"> A copy of the Survival hive arena</label></div>
    <div class="buttons"><button onclick="closeModal()">Cancel</button><button class="on" id="nok">Create</button></div>`);
  $("nok").onclick = async () => {
    const kind = document.querySelector("input[name=nk]:checked").value, [w, h] = $("nsize").value.split("x");
    closeModal();
    await setLevel(await api(`/api/new?kind=${kind}&w=${w}&h=${h}&code=${S.level?.code || "162"}`));
    S.dirty = true; updateTitle();
  };
}
async function cmdOpen() {
  if (!guard()) return;
  const list = await api("/api/projects");
  modal(`<h2>Open a level</h2><p style="color:var(--dim)">Projects in ${esc(S.info.projects)}</p>
    <div class="list" id="olist">${list.length ? list.sort((a, b) => b.modified - a.modified).map((p) => `<div data-f="${esc(p.file)}">${esc(p.file.replace(/\.atlevel$/i, ""))} <span style="color:var(--dim);float:right">${new Date(p.modified * 1000).toLocaleString()}</span></div>`).join("") : "<div>No saved levels yet.</div>"}</div>
    <div class="buttons"><button onclick="closeModal()">Cancel</button></div>`);
  for (const d of document.querySelectorAll("#olist div[data-f]")) d.onclick = async () => {
    closeModal();
    try { await setLevel(await api("/api/open?file=" + encodeURIComponent(d.dataset.f)), d.dataset.f); status("Opened " + d.dataset.f); }
    catch (e) { alert("That file could not be opened: " + e.message); }
  };
}
async function cmdImport() {
  if (!guard()) return;
  modal(`<h2>Edit a campaign level</h2><p style="color:var(--dim)">Start from one of the game's levels: its layout, floor heights, ramps, ceilings, doors, enemies, pickups and textures come across. (Lifts and sloped cave walls are not kept.)</p>
    <div class="list" id="ilist">${S.info.campaign.map((c) => `<div data-c="${c.code}">${c.code}  ${esc(c.name)}</div>`).join("")}</div>
    <div class="buttons"><button onclick="closeModal()">Cancel</button></div>`);
  for (const d of document.querySelectorAll("#ilist div")) d.onclick = async () => {
    closeModal(); status("Reading level " + d.dataset.c + "...");
    try { await setLevel(await api("/api/import?code=" + d.dataset.c)); S.dirty = true; updateTitle(); status("Level " + d.dataset.c + " loaded."); }
    catch (e) { alert("That level could not be read: " + e.message); }
  };
}
async function save(asNew) {
  let file = S.file;
  if (!file || asNew) {
    file = prompt("Save the level as:", (S.level.name || "My level").replace(/[^\w \-]/g, "_"));
    if (!file) return false;
  }
  try {
    const r = await api("/api/save", { level: S.level, file });
    S.file = r.file; S.dirty = false; updateTitle(); status("Saved " + r.path);
    return true;
  } catch (e) { alert("Could not save: " + e.message); return false; }
}
async function cmdExport() {
  const problems = await runChecks();
  if (problems.length) { showTab("checks"); alert("Fix these first:\n\n" + problems.map((p) => "• " + p).join("\n")); return; }
  status("Exporting...");
  try {
    const r = await api("/api/export", { level: S.level });
    if (r.problems) { alert("Fix these first:\n\n" + r.problems.join("\n")); return; }
    modal(`<h2>Exported</h2><p>${esc(S.level.name)} is in</p><p><code>${esc(r.folder)}</code></p><p style="color:var(--dim)">${esc(r.files.join(", "))}<br>${r.faces} faces, ${r.vertices} vertices.</p>
      <div class="buttons"><button class="on" onclick="closeModal()">OK</button></div>`);
    status("Exported to " + r.folder);
  } catch (e) { alert("The level could not be built: " + e.message); }
}
function cmdResize() {
  const lv = S.level, answer = prompt("New size as width x height (16 to 127 each):", `${lv.width}x${lv.height}`);
  if (!answer) return;
  const m = answer.toLowerCase().replace(/\s/g, "").split("x").map(Number);
  if (m.length !== 2 || m.some(isNaN)) return;
  const w = clamp(m[0], 16, 127), h = clamp(m[1], 16, 127);
  snapshot();
  const grid = [];
  for (let z = 0; z < h; z++) {
    const row = z < lv.height ? lv.grid[z] : "";
    let s = "";
    for (let x = 0; x < w; x++) s += x < row.length && x > 0 && x < w - 1 && z > 0 && z < h - 1 ? row[x] : "#";
    grid.push(s);
  }
  const inside = (x, z) => x > 0 && z > 0 && x < w - 1 && z < h - 1;
  lv.grid = grid; lv.width = w; lv.height = h;
  lv.enemies = lv.enemies.filter((e) => inside(e.x, e.z));
  lv.pickups = lv.pickups.filter((p) => inside(p.x, p.z));
  lv.doors = lv.doors.filter((d) => doorCells(d).every(([x, z]) => inside(x, z)));
  for (const t of ["ceil", "tex", "floor", "ramp"]) for (const k of Object.keys(lv[t])) { const [x, z] = k.split(",").map(Number); if (!inside(x, z)) delete lv[t][k]; }
  if (!inside(...lv.start)) lv.start = [w >> 1, h >> 1];
  fillLevelForm(); changed();
}
function cmdHelp() {
  const rows = [
    ["Middle mouse drag", "Grab the map and slide it around"], ["Right mouse drag", "Turn the view (orbit) / look around (fly)"],
    ["Mouse wheel", "Zoom (toward the cursor)"], ["W A S D / arrows", "Move; Shift to go faster"], ["Q / E", "Turn (orbit) / down and up (fly)"],
    ["Tab", "Fly camera on/off (walk the level from inside)"], ["F", "Frame the whole level"], ["C", "Show ceilings (hidden from above by default)"],
    ["G", "Grid lines"], ["[ and ]", "Brush size"], ["R", "Turn: door direction, ramp under the cursor, player start"],
    ["Ctrl+Z / Ctrl+Y", "Undo / redo"], ["Ctrl+S", "Save"], ["Esc", "Put back what you are carrying"],
  ];
  modal(`<h2>How it works</h2>
    <p>Build the level in 3D: dig rooms out of solid rock, raise and lower floors, add ramps between levels, set ceiling heights, paint textures straight onto floors, walls and ceilings, then place doors, the player, enemies and pickups. What you see is what <b>Export</b> writes (the game's own level files).</p>
    <table class="help">${TOOLS.map((t) => `<tr><td>${t[2]}  ${t[1]}</td><td>${t[3]}</td></tr>`).join("")}</table>
    <h3>Camera and keys</h3>
    <table class="help">${rows.map((r) => `<tr><td>${r[0]}</td><td>${r[1]}</td></tr>`).join("")}</table>
    <p style="color:var(--dim)">The player climbs 256 units at most from one cell to the next (the height step buttons are in game units: a cell is 512 wide). Ramps rise 128 per cell, so a ramp of two cells climbs one 256 step. Doors keep one floor height along their length. Survival levels need a door with a spawn room behind it.</p>
    <div class="buttons"><button class="on" onclick="closeModal()">OK</button></div>`);
}
window.closeModal = closeModal;

// ---- Start ------------------------------------------------------------------------------------------------------
async function main() {
  S.info = await api("/api/info");
  S.enemyName = Object.fromEntries(S.info.enemies.map((e) => [e.kind, e.name]));
  S.pickupName = Object.fromEntries(S.info.pickups.map((p) => [p.kind, p.name]));
  // Panels
  const tools = $("tools");
  for (const [id, label, k] of TOOLS) {
    const b = document.createElement("button");
    b.dataset.tool = id; b.innerHTML = `<span>${label}</span><kbd>${k}</kbd>`;
    b.onclick = () => setTool(id); tools.appendChild(b);
  }
  for (const n of [1, 3, 5, 7]) { const b = document.createElement("button"); b.textContent = n; b.dataset.v = n; b.onclick = () => setBrush(n); $("brush").appendChild(b); }
  for (const n of [32, 64, 128, 256, 512]) { const b = document.createElement("button"); b.textContent = n; b.dataset.v = n; b.onclick = () => setStep(n); $("step").appendChild(b); }
  const fill = (el, items, get, set) => {
    el.innerHTML = items.map((i) => `<div data-k="${i.kind}">${esc(i.name)}</div>`).join("");
    const mark = () => el.querySelectorAll("div").forEach((d) => d.classList.toggle("on", +d.dataset.k === get()));
    el.onclick = (e) => { const d = e.target.closest("div[data-k]"); if (d) { set(+d.dataset.k); mark(); } };
    mark();
  };
  fill($("enemyList"), S.info.enemies, () => S.enemy, (k) => { S.enemy = k; setTool("enemy"); });
  fill($("pickupList"), S.info.pickups, () => S.pickup, (k) => { S.pickup = k; setTool("pickup"); });
  $("f-code").innerHTML = S.info.sets.map((s) => `<option value="${s.code}">${s.code}  ${esc(s.name)}</option>`).join("");
  $("f-weapons").innerHTML = S.info.weapons.map((w, i) => `<label style="display:inline-block;width:48%"><input type="checkbox" id="w-${i}"> ${esc(w)}</label>`).join("");
  for (const id of ["f-name", "f-code", "f-mode", "f-lives", "f-waves", "f-roster", "f-brightness", "f-queen"]) $(id).addEventListener("change", formChanged);
  document.querySelectorAll("#f-weapons input").forEach((b) => b.addEventListener("change", formChanged));
  $("resizeBtn").onclick = cmdResize;
  for (const b of document.querySelectorAll("#tabs button")) b.onclick = () => showTab(b.dataset.tab);
  const cmds = { new: cmdNew, open: cmdOpen, import: cmdImport, save: () => save(false), saveas: () => save(true), export: cmdExport,
    undo, redo, frame: frameLevel, fly: () => setFly(!S.fly), ceilings: toggleCeil, grid: toggleGrid, help: cmdHelp };
  for (const b of document.querySelectorAll("#top button[data-cmd]")) b.onclick = () => cmds[b.dataset.cmd]();
  window.addEventListener("resize", drawPalette);
  window.addEventListener("beforeunload", (e) => { if (S.dirty) { e.preventDefault(); e.returnValue = ""; } });
  setTool("dig"); setBrush(1); setStep(128);
  await setLevel(await api("/api/new?w=40&h=32&code=162"));
  status("Middle mouse drags the map, right mouse turns it, the wheel zooms. Dig rooms with 1, raise floors with 3, paint with T. Help explains the rest.");
  requestAnimationFrame(tick);
}
main().catch((e) => { status("Could not start: " + e.message); console.error(e); });
