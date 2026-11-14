/**
 * Frontier Cloth Authoring — XPBD Top-3 demo (ProjectZero UI).
 * WebGL2 renderer + CPU XPBD (small-steps / energy-aware / AVBD-lite).
 * Mannequin via preserved Shared engine.
 */
import { MannequinEngine, BODY_PRESETS } from '../Shared/Mannequin/MannequinEngine.js';
import { ClothXPBD, buildGarment, TEXTILE_PRESETS, GARMENT_PRESETS, SOLVER_PICKS } from './ClothXPBD.js';

const MANNEQUIN_URL = './Assets/FrontierMannequin.bin';

function lookAtMat4(eye, target, up) {
  const zx = eye[0] - target[0], zy = eye[1] - target[1], zz = eye[2] - target[2];
  const zl = Math.hypot(zx, zy, zz) || 1;
  const z = [zx / zl, zy / zl, zz / zl];
  let x = [up[1] * z[2] - up[2] * z[1], up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0]];
  const xl = Math.hypot(x[0], x[1], x[2]) || 1;
  x = [x[0] / xl, x[1] / xl, x[2] / xl];
  const y = [z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]];
  const m = new Float32Array(16);
  m[0] = x[0]; m[1] = y[0]; m[2] = z[0]; m[3] = 0;
  m[4] = x[1]; m[5] = y[1]; m[6] = z[1]; m[7] = 0;
  m[8] = x[2]; m[9] = y[2]; m[10] = z[2]; m[11] = 0;
  m[12] = -(x[0] * eye[0] + x[1] * eye[1] + x[2] * eye[2]);
  m[13] = -(y[0] * eye[0] + y[1] * eye[1] + y[2] * eye[2]);
  m[14] = -(z[0] * eye[0] + z[1] * eye[1] + z[2] * eye[2]);
  m[15] = 1;
  return m;
}
function perspMat4(fov, aspect, near, far) {
  const f = 1 / Math.tan(fov / 2);
  const m = new Float32Array(16);
  m[0] = f / aspect; m[5] = f; m[10] = (far + near) / (near - far); m[11] = -1;
  m[14] = (2 * far * near) / (near - far);
  return m;
}
function mulMat4(a, b) {
  const o = new Float32Array(16);
  for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) {
    let s = 0;
    for (let k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
    o[c * 4 + r] = s;
  }
  return o;
}

const CLOTH_VS = `#version 300 es
precision highp float;
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec2 aUv;
uniform mat4 uViewProj; out vec3 vPos; out vec3 vNrm; out vec2 vUv;
void main(){ gl_Position=uViewProj*vec4(aPos,1.0); vPos=aPos; vNrm=aNrm; vUv=aUv; }`;
const CLOTH_FS = `#version 300 es
precision highp float;
in vec3 vPos; in vec3 vNrm; in vec2 vUv;
uniform vec3 uCam; uniform vec4 uAlbedo; uniform vec3 uSheen;
out vec4 frag;
void main(){
  vec3 n=normalize(vNrm); if(!gl_FrontFacing) n=-n;
  vec3 v=normalize(uCam-vPos);
  vec3 L=normalize(vec3(0.55,0.85,0.58));
  float nd=max(dot(n,L),0.0);
  float wrap=max(0.0,(dot(n,L)+0.35)/1.35);
  vec3 H=normalize(L+v);
  float spec=pow(max(dot(n,H),0.0),24.0)*0.12;
  float sheen=pow(1.0-max(dot(n,v),0.0),3.2)*0.30;
  vec3 amb=mix(vec3(0.12,0.13,0.15),vec3(0.32,0.33,0.36),0.5+0.5*n.y);
  vec3 col=uAlbedo.rgb*(amb+vec3(1.04,0.99,0.92)*(0.72*nd+0.18*wrap))+uSheen*(sheen+spec);
  if(!gl_FrontFacing) col*=0.90;
  col=(col*(2.51*col+0.03))/(col*(2.43*col+0.59)+0.14);
  frag=vec4(pow(clamp(col,0.0,1.0),vec3(1.0/2.2)),1.0);
}`;
const MANN_VS = `#version 300 es
precision highp float;
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm;
uniform mat4 uViewProj; out vec3 vPos; out vec3 vNrm;
void main(){ gl_Position=uViewProj*vec4(aPos,1.0); vPos=aPos; vNrm=aNrm; }`;
const MANN_FS = `#version 300 es
precision highp float;
in vec3 vPos; in vec3 vNrm;
uniform vec3 uCam;
out vec4 frag;
void main(){
  vec3 n=normalize(vNrm); vec3 v=normalize(uCam-vPos);
  vec3 L=normalize(vec3(0.55,0.85,0.58));
  float nd=max(dot(n,L),0.0);
  vec3 H=normalize(L+v);
  float spec=pow(max(dot(n,H),0.0),32.0)*0.10;
  vec3 amb=mix(vec3(0.14,0.14,0.16),vec3(0.34,0.34,0.37),0.5+0.5*n.y);
  vec3 col=vec3(0.78,0.73,0.67)*(amb+vec3(1.04,0.99,0.92)*0.72*nd)+vec3(0.1)*spec;
  col=(col*(2.51*col+0.03))/(col*(2.43*col+0.59)+0.14);
  frag=vec4(pow(clamp(col,0.0,1.0),vec3(1.0/2.2)),1.0);
}`;
const FLOOR_VS = `#version 300 es
precision highp float;
layout(location=0) in vec3 aPos;
uniform mat4 uViewProj; out vec3 vPos;
void main(){ gl_Position=uViewProj*vec4(aPos,1.0); vPos=aPos; }`;
const FLOOR_FS = `#version 300 es
precision highp float;
in vec3 vPos; out vec4 frag;
void main(){
  vec2 g=abs(fract(vPos.xz*2.0)-0.5);
  float line=smoothstep(0.48,0.5,max(g.x,g.y));
  vec3 col=mix(vec3(0.06,0.06,0.07),vec3(0.10,0.10,0.11),line);
  frag=vec4(col,1.0);
}`;

function makeProg(gl, vsSrc, fsSrc) {
  const vs = gl.createShader(gl.VERTEX_SHADER); gl.shaderSource(vs, vsSrc); gl.compileShader(vs);
  if (!gl.getShaderParameter(vs, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(vs));
  const fs = gl.createShader(gl.FRAGMENT_SHADER); gl.shaderSource(fs, fsSrc); gl.compileShader(fs);
  if (!gl.getShaderParameter(fs, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(fs));
  const p = gl.createProgram(); gl.attachShader(p, vs); gl.attachShader(p, fs); gl.linkProgram(p);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
  return p;
}
function makeVBO(gl, data, size, dynamic) {
  const b = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, b);
  gl.bufferData(gl.ARRAY_BUFFER, data, dynamic ? gl.DYNAMIC_DRAW : gl.STATIC_DRAW);
  gl.enableVertexAttribArray(size);
  gl.vertexAttribPointer(size, 3, gl.FLOAT, false, 0, 0);
  return b;
}

class AtelierApp {
  constructor() {
    this.canvas = document.getElementById('cloth-canvas');
    this.gl = this.canvas.getContext('webgl2', { antialias: true });
    if (!this.gl) throw new Error('WebGL2 unavailable');
    this.state = {
      garmentId: 'atelier_dress', materialKey: 'silk', solverMode: 'xpbd_small',
      substeps: 8, iterations: 1, wind: 0, selfCollision: false, playing: true, frame: 0,
    };
    this.camera = { yaw: 0.44, pitch: 0.14, distance: 2.35, target: [0, 1.04, 0.02], eye: [1, 1.35, 2.1], viewProj: new Float32Array(16), aspect: 1.4 };
    this.pointer = { mode: null, lastX: 0, lastY: 0 };
    this.solver = null; this.patch = null; this.mannequin = null;
    this.initGL();
  }
  initGL() {
    const gl = this.gl;
    this.clothProg = makeProg(gl, CLOTH_VS, CLOTH_FS);
    this.mannProg = makeProg(gl, MANN_VS, MANN_FS);
    this.floorProg = makeProg(gl, FLOOR_VS, FLOOR_FS);
    this.clothLoc = { uViewProj: gl.getUniformLocation(this.clothProg, 'uViewProj'), uCam: gl.getUniformLocation(this.clothProg, 'uCam'), uAlbedo: gl.getUniformLocation(this.clothProg, 'uAlbedo'), uSheen: gl.getUniformLocation(this.clothProg, 'uSheen') };
    this.mannLoc = { uViewProj: gl.getUniformLocation(this.mannProg, 'uViewProj'), uCam: gl.getUniformLocation(this.mannProg, 'uCam') };
    this.floorLoc = { uViewProj: gl.getUniformLocation(this.floorProg, 'uViewProj') };
    const floorVerts = new Float32Array([-5, 0, -5, 5, 0, -5, -5, 0, 5, -5, 0, 5, 5, 0, -5, 5, 0, 5]);
    this.floorVAO = gl.createVertexArray();
    gl.bindVertexArray(this.floorVAO);
    const fb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, fb); gl.bufferData(gl.ARRAY_BUFFER, floorVerts, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);
  }
  async init() {
    this.buildGarmentList(); this.buildBodyPresets(); this.buildFabricRow(); this.bindUI(); this.handleResize();
    window.addEventListener('resize', () => this.handleResize());
    this.mannequin = await MannequinEngine.load(MANNEQUIN_URL);
    this.updateMeasurements();
    this.setupMannequinBuffers();
    this.rebuildGarment();
    const loop = () => { this.tick(); requestAnimationFrame(loop); };
    requestAnimationFrame(loop);
  }
  setupMannequinBuffers() {
    const gl = this.gl, m = this.mannequin;
    this.mannVAO = gl.createVertexArray();
    gl.bindVertexArray(this.mannVAO);
    this.mannPosB = makeVBO(gl, m.positions, 0, true);
    this.mannNrmB = makeVBO(gl, m.normals, 1, true);
    this.mannIBO = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.mannIBO);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, m.triIndices, gl.STATIC_DRAW);
    gl.bindVertexArray(null);
  }
  buildGarmentList() {
    const el = document.getElementById('garment-list');
    el.innerHTML = '';
    for (const [id, g] of Object.entries(GARMENT_PRESETS)) {
      const div = document.createElement('div');
      div.className = 'outliner-item' + (id === this.state.garmentId ? ' active' : '');
      div.innerHTML = `<span class="dot"></span><span><b style="font-weight:400;">${g.name}</b><small>${g.nx}×${g.ny} grid</small></span>`;
      div.addEventListener('click', () => {
        this.state.garmentId = id; this.state.materialKey = g.material;
        el.querySelectorAll('.outliner-item').forEach((b) => b.classList.remove('active'));
        div.classList.add('active');
        this.rebuildGarment();
      });
      el.appendChild(div);
    }
  }
  buildBodyPresets() {
    const el = document.getElementById('body-presets');
    el.innerHTML = '';
    for (const [id, p] of Object.entries(BODY_PRESETS)) {
      const btn = document.createElement('button');
      btn.textContent = p.name; btn.title = p.subtitle;
      btn.addEventListener('click', () => {
        this.mannequin.applyParams(p.params, { resetVelocity: true });
        this.updateMeasurements(); this.rebuildGarment();
      });
      el.appendChild(btn);
    }
    document.getElementById('btn-reset-body').addEventListener('click', () => {
      this.mannequin.applyParams(BODY_PRESETS.atelier36.params, { resetVelocity: true });
      this.updateMeasurements(); this.rebuildGarment();
    });
  }
  buildFabricRow() {
    const el = document.getElementById('fabric-row');
    el.innerHTML = '';
    for (const [id, m] of Object.entries(TEXTILE_PRESETS)) {
      const btn = document.createElement('button');
      btn.className = 'swatch' + (id === this.state.materialKey ? ' active' : '');
      btn.title = m.name;
      btn.style.background = `rgb(${Math.round(m.color[0] * 255)},${Math.round(m.color[1] * 255)},${Math.round(m.color[2] * 255)})`;
      btn.addEventListener('click', () => {
        this.state.materialKey = id;
        el.querySelectorAll('.swatch').forEach((b) => b.classList.remove('active'));
        btn.classList.add('active');
        this.rebuildGarment();
      });
      el.appendChild(btn);
    }
  }
  bindUI() {
    const syncRange = (id, valId, fmt) => {
      const el = document.getElementById(id);
      const fill = () => { const pct = ((el.value - el.min) / (el.max - el.min)) * 100; el.style.setProperty('--fill', pct + '%'); document.getElementById(valId).textContent = fmt(Number(el.value)); };
      el.addEventListener('input', fill); fill();
      return el;
    };
    const subEl = syncRange('rng-substeps', 'val-substeps', (v) => String(v));
    const itEl = syncRange('rng-iters', 'val-iters', (v) => String(v));
    const windEl = syncRange('rng-wind', 'val-wind', (v) => v.toFixed(2) + '×');
    subEl.addEventListener('input', () => this.state.substeps = Number(subEl.value));
    itEl.addEventListener('input', () => this.state.iterations = Number(itEl.value));
    windEl.addEventListener('input', () => this.state.wind = Number(windEl.value));
    const tgl = document.getElementById('tgl-selfcol');
    tgl.addEventListener('click', () => {
      this.state.selfCollision = !this.state.selfCollision;
      tgl.classList.toggle('on', this.state.selfCollision);
      tgl.setAttribute('aria-pressed', String(this.state.selfCollision));
    });
    document.getElementById('btn-play').addEventListener('click', (e) => {
      this.state.playing = !this.state.playing;
      e.target.textContent = this.state.playing ? 'Pause' : 'Play';
    });
    document.getElementById('btn-recut').addEventListener('click', () => this.rebuildGarment());
    const setSolver = (mode) => {
      this.state.solverMode = mode;
      document.querySelectorAll('[data-solver]').forEach((b) => b.classList.toggle('active', b.dataset.solver === mode));
      const spec = SOLVER_PICKS[mode];
      document.getElementById('solver-tier-badge').textContent = spec.tier.split(' · ')[0];
      document.getElementById('solver-note').textContent = spec.desc;
      document.getElementById('tel-solver-name').textContent = spec.title;
      if (this.solver) this.solver.setSolverMode(mode);
    };
    document.querySelectorAll('[data-solver]').forEach((btn) => btn.addEventListener('click', () => setSolver(btn.dataset.solver)));
    document.querySelectorAll('[data-cam]').forEach((btn) => btn.addEventListener('click', () => {
      document.querySelectorAll('[data-cam]').forEach((b) => b.classList.remove('active'));
      btn.classList.add('active');
      this.setCameraPreset(btn.dataset.cam);
    }));
    const notch = document.getElementById('notch-shell');
    document.getElementById('notch-handle').addEventListener('click', () => notch.classList.toggle('open'));
    document.getElementById('notch-close').addEventListener('click', () => notch.classList.remove('open'));
    const drawer = document.getElementById('drawer');
    document.getElementById('drawer-handle').addEventListener('click', () => drawer.classList.toggle('open'));
    this.canvas.addEventListener('pointerdown', (e) => { this.pointer.mode = e.button === 2 ? 'pan' : 'orbit'; this.pointer.lastX = e.clientX; this.pointer.lastY = e.clientY; this.canvas.setPointerCapture(e.pointerId); });
    this.canvas.addEventListener('pointermove', (e) => {
      if (!this.pointer.mode) return;
      const dx = e.clientX - this.pointer.lastX, dy = e.clientY - this.pointer.lastY;
      this.pointer.lastX = e.clientX; this.pointer.lastY = e.clientY;
      if (this.pointer.mode === 'orbit') { this.camera.yaw -= dx * 0.005; this.camera.pitch = Math.max(-0.4, Math.min(1.2, this.camera.pitch + dy * 0.005)); }
      else { this.camera.target[0] -= dx * 0.002; this.camera.target[1] += dy * 0.002; }
      this.updateCameraMatrices();
    });
    this.canvas.addEventListener('pointerup', () => this.pointer.mode = null);
    this.canvas.addEventListener('wheel', (e) => { e.preventDefault(); this.camera.distance = Math.max(0.8, Math.min(6, this.camera.distance + e.deltaY * 0.003)); this.updateCameraMatrices(); }, { passive: false });
    this.canvas.addEventListener('contextmenu', (e) => e.preventDefault());
  }
  setCameraPreset(name) {
    const presets = { front: [0, 0.1, 2.35], threeQuarter: [0.44, 0.14, 2.35], side: [Math.PI / 2, 0.1, 2.35], back: [Math.PI, 0.1, 2.35] };
    const p = presets[name] || presets.threeQuarter;
    this.camera.yaw = p[0]; this.camera.pitch = p[1]; this.camera.distance = p[2];
    this.updateCameraMatrices();
  }
  handleResize() {
    const w = this.canvas.clientWidth || this.canvas.parentElement.clientWidth;
    const h = this.canvas.clientHeight || this.canvas.parentElement.clientHeight;
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    this.canvas.width = Math.max(1, Math.floor(w * dpr));
    this.canvas.height = Math.max(1, Math.floor(h * dpr));
    this.camera.aspect = this.canvas.width / this.canvas.height;
    this.updateCameraMatrices();
  }
  updateCameraMatrices() {
    const cosP = Math.cos(this.camera.pitch), sinP = Math.sin(this.camera.pitch);
    const cosY = Math.cos(this.camera.yaw), sinY = Math.sin(this.camera.yaw);
    this.camera.eye = [this.camera.target[0] + this.camera.distance * cosP * sinY, this.camera.target[1] + this.camera.distance * sinP, this.camera.target[2] + this.camera.distance * cosP * cosY];
    const view = lookAtMat4(this.camera.eye, this.camera.target, [0, 1, 0]);
    const proj = perspMat4(0.62, this.camera.aspect, 0.05, 35);
    this.camera.viewProj = mulMat4(proj, view);
  }
  updateMeasurements() {
    const m = this.mannequin.measurements;
    document.getElementById('measure-badge').textContent = `${(this.mannequin.vCount / 1000).toFixed(1)}K verts`;
    document.getElementById('hud-bwh').textContent = `B ${m.bust.toFixed(0)} · W ${m.waist.toFixed(0)} · H ${m.hips.toFixed(0)} cm`;
    const grid = document.getElementById('measure-grid');
    grid.innerHTML = '';
    for (const [k, v] of [['Bust', m.bust], ['Waist', m.waist], ['Hips', m.hips], ['Shoulder', m.shoulderWidth]]) {
      const cell = document.createElement('div');
      cell.className = 'tel-cell';
      cell.innerHTML = `<span class="tel-k">${k}</span><span class="tel-v">${v.toFixed(1)} cm</span>`;
      grid.appendChild(cell);
    }
  }
  rebuildGarment() {
    this.patch = buildGarment(this.state.garmentId, this.mannequin, this.state.materialKey, 1.0);
    const mat = TEXTILE_PRESETS[this.state.materialKey];
    this.solver = new ClothXPBD(this.patch, mat, this.mannequin, this.state.solverMode);
    this.state.frame = 0;
    const gl = this.gl;
    if (this.clothVAO) { gl.deleteVertexArray(this.clothVAO); }
    this.clothVAO = gl.createVertexArray();
    gl.bindVertexArray(this.clothVAO);
    this.clothPosB = makeVBO(gl, this.solver.pos, 0, true);
    this.clothNrmB = makeVBO(gl, this.solver.normals, 1, true);
    const uvB = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, uvB); gl.bufferData(gl.ARRAY_BUFFER, this.patch.uv, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 2, gl.FLOAT, false, 0, 0);
    this.clothIBO = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.clothIBO);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, this.patch.triIndices, gl.STATIC_DRAW);
    gl.bindVertexArray(null);
    document.getElementById('hud-garment-info').textContent = `${GARMENT_PRESETS[this.state.garmentId].name} · ${this.patch.pointCount} pts · ${mat.name}`;
  }
  tick() {
    if (!this.solver) return;
    this.updateCameraMatrices();
    if (this.state.playing) {
      this.solver.stepFrame(1 / 60, this.state.substeps, this.state.iterations, this.state.selfCollision, this.state.wind);
      this.mannequin.clearMotionDelta();
      this.state.frame++;
    }
    this.render();
    this.updateTelemetry();
  }
  render() {
    const gl = this.gl, m = this.mannequin;
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.enable(gl.DEPTH_TEST);
    gl.clearColor(0.043, 0.045, 0.052, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.useProgram(this.floorProg);
    gl.uniformMatrix4fv(this.floorLoc.uViewProj, false, this.camera.viewProj);
    gl.bindVertexArray(this.floorVAO); gl.drawArrays(gl.TRIANGLES, 0, 6);
    // Sync mannequin buffers (body morphs)
    gl.bindBuffer(gl.ARRAY_BUFFER, this.mannPosB); gl.bufferSubData(gl.ARRAY_BUFFER, 0, m.positions);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.mannNrmB); gl.bufferSubData(gl.ARRAY_BUFFER, 0, m.normals);
    gl.useProgram(this.mannProg);
    gl.enable(gl.CULL_FACE); gl.cullFace(gl.BACK);
    gl.uniformMatrix4fv(this.mannLoc.uViewProj, false, this.camera.viewProj);
    gl.uniform3fv(this.mannLoc.uCam, this.camera.eye);
    gl.bindVertexArray(this.mannVAO); gl.drawElements(gl.TRIANGLES, m.tCount * 3, gl.UNSIGNED_INT, 0);
    // Cloth
    const mat = TEXTILE_PRESETS[this.state.materialKey];
    gl.bindBuffer(gl.ARRAY_BUFFER, this.clothPosB); gl.bufferSubData(gl.ARRAY_BUFFER, 0, this.solver.pos);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.clothNrmB); gl.bufferSubData(gl.ARRAY_BUFFER, 0, this.solver.normals);
    gl.useProgram(this.clothProg);
    gl.disable(gl.CULL_FACE);
    gl.uniformMatrix4fv(this.clothLoc.uViewProj, false, this.camera.viewProj);
    gl.uniform3fv(this.clothLoc.uCam, this.camera.eye);
    gl.uniform4f(this.clothLoc.uAlbedo, mat.color[0], mat.color[1], mat.color[2], 0.5);
    gl.uniform3f(this.clothLoc.uSheen, mat.color[0] * 0.5, mat.color[1] * 0.5, mat.color[2] * 0.5);
    gl.bindVertexArray(this.clothVAO); gl.drawElements(gl.TRIANGLES, this.patch.triCount * 3, gl.UNSIGNED_INT, 0);
    gl.bindVertexArray(null);
  }
  updateTelemetry() {
    document.getElementById('frame-counter').textContent = `Frame ${this.state.frame}`;
    const tel = this.solver.evaluateTelemetry();
    document.getElementById('tel-ms').textContent = `${tel.stepMs.toFixed(2)} ms`;
    document.getElementById('tel-maxstr').textContent = `${Math.max(0, (tel.maxStretch - 1) * 100).toFixed(1)}%`;
    document.getElementById('tel-meanstr').textContent = `${Math.max(0, (tel.meanStretch - 1) * 100).toFixed(1)}%`;
    document.getElementById('tel-pen').textContent = `${tel.maxPenMm.toFixed(2)} mm`;
    document.getElementById('tel-invalid').textContent = String(tel.invalidCount);
    const pill = document.getElementById('tel-status');
    if (tel.invalidCount > 0 || tel.maxPenMm > 2.5) { pill.className = 'tel-pill crit'; pill.textContent = 'UNSTABLE'; }
    else if ((tel.maxStretch - 1) * 100 > 35) { pill.className = 'tel-pill warn'; pill.textContent = 'STRETCHED'; }
    else { pill.className = 'tel-pill'; pill.textContent = 'STABLE'; }
  }
}

window.addEventListener('DOMContentLoaded', () => {
  const app = new AtelierApp();
  app.init().catch((err) => {
    console.error(err);
    document.getElementById('hud-garment-info').textContent = `Error: ${err.message}`;
  });
});
