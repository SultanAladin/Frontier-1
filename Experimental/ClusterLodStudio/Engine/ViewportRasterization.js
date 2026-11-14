// WebGL2 viewport for the Cluster LOD Studio.
//
// Geometry layout on the GPU:
//   - positions / normals live in RGBA32F textures (one texel per source vertex), shared by every LOD level
//   - one static corner stream (vertex id, cluster id) holds every cluster of every level back to back
//   - each frame the CPU selection yields corner ranges per instance, submitted with WEBGL_multi_draw
//     (falls back to one call per merged range)
// The vertex shader pulls positions by id, so per-cluster / per-triangle debug colours need no extra buffers.

import { createSelectionState, selectClusters } from './ClusterSelection.js';

const VERTEX_TEX = 2048, CLUSTER_TEX = 1024;

export const VIEW_MODES = [
  { id: 'shaded', label: 'Shaded', hint: 'Lit surface' },
  { id: 'clusters', label: 'Clusters', hint: 'One colour per cluster' },
  { id: 'groups', label: 'Groups', hint: 'Clusters that simplify together' },
  { id: 'levels', label: 'LOD level', hint: 'Blue = full detail, red = coarsest' },
  { id: 'triangles', label: 'Triangles', hint: 'One colour per triangle' },
  { id: 'budget', label: 'Error', hint: 'Share of the pixel error threshold in use' },
];

const VS = `#version 300 es
precision highp float; precision highp int;
layout(location=0) in uint aVertex;
layout(location=1) in uint aCluster;
uniform highp sampler2D uPositions, uNormals, uClusterA, uClusterB;
uniform mat4 uViewProj, uModel;
uniform vec3 uEyeLocal;
uniform float uFocal, uNear, uThreshold;
uniform int uMode, uLevelCount, uInstance, uSelectedCluster, uSelectedInstance;
out vec3 vNormal; out vec3 vWorld; out vec3 vBary;
flat out vec3 vColor; flat out float vSelected; flat out vec4 vPick;

vec3 hue(float h) { return clamp(abs(fract(h + vec3(0.0, 2.0/3.0, 1.0/3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0); }
vec3 idColor(int id) {
  float h = fract(float(id) * 0.61803398875);
  float v = 0.62 + 0.38 * fract(float(id) * 0.3819660113 + 0.25);
  return mix(vec3(1.0), hue(h), 0.72) * v;
}
vec3 ramp(float t) {
  t = clamp(t, 0.0, 1.0);
  vec3 a = vec3(0.19, 0.42, 0.95), b = vec3(0.16, 0.78, 0.76), c = vec3(0.35, 0.82, 0.36), d = vec3(0.97, 0.80, 0.25), e = vec3(0.94, 0.30, 0.24);
  if (t < 0.25) return mix(a, b, t * 4.0);
  if (t < 0.5) return mix(b, c, (t - 0.25) * 4.0);
  if (t < 0.75) return mix(c, d, (t - 0.5) * 4.0);
  return mix(d, e, (t - 0.75) * 4.0);
}
void main() {
  ivec2 vt = ivec2(int(aVertex) % ${VERTEX_TEX}, int(aVertex) / ${VERTEX_TEX});
  vec3 p = texelFetch(uPositions, vt, 0).xyz;
  vec3 n = texelFetch(uNormals, vt, 0).xyz;
  vec4 world = uModel * vec4(p, 1.0);
  vWorld = world.xyz;
  vNormal = mat3(uModel) * n;
  int corner = gl_VertexID % 3;
  vBary = vec3(corner == 0, corner == 1, corner == 2);
  int cluster = int(aCluster);
  ivec2 ct = ivec2(cluster % ${CLUSTER_TEX}, cluster / ${CLUSTER_TEX});
  vec4 A = texelFetch(uClusterA, ct, 0);
  vec4 B = texelFetch(uClusterB, ct, 0);
  vec3 color = vec3(0.74, 0.73, 0.71);
  if (uMode == 1) color = idColor(cluster);
  else if (uMode == 2) color = B.w < 0.0 ? vec3(0.8) : idColor(int(B.w) * 7 + 3);
  else if (uMode == 3) color = ramp(B.y / max(1.0, float(uLevelCount - 1)));
  else if (uMode == 4) color = idColor(gl_VertexID / 3);
  else if (uMode == 5) {
    float d = max(distance(A.xyz, uEyeLocal) - A.w, uNear);
    color = ramp(B.x * uFocal / d / max(uThreshold, 1e-6));
  }
  vColor = color;
  vSelected = (cluster == uSelectedCluster && uInstance == uSelectedInstance) ? 1.0 : 0.0;
  int id = cluster + 1;
  vPick = vec4(float(id & 255), float((id >> 8) & 255), float((id >> 16) & 255), float(uInstance + 1)) / 255.0;
  gl_Position = uViewProj * world;
}`;

const FS = `#version 300 es
precision highp float; precision highp int;
in vec3 vNormal; in vec3 vWorld; in vec3 vBary;
flat in vec3 vColor; flat in float vSelected;
uniform vec3 uEyeWorld;
uniform int uMode; uniform float uWire; uniform float uFlat;
out vec4 outColor;
void main() {
  vec3 faceN = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
  vec3 n = uFlat > 0.5 ? faceN : normalize(vNormal);
  vec3 v = normalize(uEyeWorld - vWorld);
  if (dot(n, v) < 0.0 && dot(faceN, v) > 0.0) n = faceN;
  vec3 key = normalize(vec3(0.45, 0.8, 0.35));
  float diffuse = max(dot(n, key), 0.0);
  float sky = n.y * 0.5 + 0.5;
  float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);
  vec3 light = mix(vec3(0.10, 0.10, 0.12), vec3(0.30, 0.33, 0.38), sky) + vec3(1.0, 0.96, 0.9) * diffuse * 0.85;
  vec3 color = vColor * light + rim * 0.06;
  if (uMode == 0) color += pow(max(dot(n, normalize(key + v)), 0.0), 40.0) * 0.12;
  else color = mix(color, vColor, 0.35);
  if (vSelected > 0.5) color = mix(color, vec3(1.0, 0.72, 0.2), 0.6);
  if (uWire > 0.0) {
    vec3 w = fwidth(vBary);
    vec3 a = smoothstep(vec3(0.0), w * 1.2, vBary);
    float edge = 1.0 - min(min(a.x, a.y), a.z);
    color = mix(color, uMode == 0 ? vec3(0.02, 0.03, 0.04) : color * 0.25, edge * uWire);
  }
  outColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}`;

const PICK_FS = `#version 300 es
precision highp float; precision highp int;
in vec3 vNormal; in vec3 vWorld; in vec3 vBary;
flat in vec3 vColor; flat in float vSelected; flat in vec4 vPick;
out vec4 outColor;
void main() { outColor = vPick; }`;

const LINE_VS = `#version 300 es
layout(location=0) in vec3 aPosition; layout(location=1) in vec4 aColor;
uniform mat4 uViewProj; out vec4 vColor;
void main() { vColor = aColor; gl_Position = uViewProj * vec4(aPosition, 1.0); }`;
const LINE_FS = `#version 300 es
precision highp float; in vec4 vColor; out vec4 outColor;
void main() { outColor = vColor; }`;

// --- Minimal column-major mat4 helpers ---------------------------------------------------------------------
function perspective(fovY, aspect, near, far) {
  const f = 1 / Math.tan(fovY / 2), m = new Float32Array(16);
  m[0] = f / aspect; m[5] = f; m[10] = (far + near) / (near - far); m[11] = -1; m[14] = (2 * far * near) / (near - far);
  return m;
}
function lookAt(eye, target) {
  let zx = eye[0] - target[0], zy = eye[1] - target[1], zz = eye[2] - target[2];
  let l = Math.hypot(zx, zy, zz) || 1; zx /= l; zy /= l; zz /= l;
  let xx = zz, xy = 0, xz = -zx; // cross(up=(0,1,0), z)
  l = Math.hypot(xx, xy, xz) || 1; xx /= l; xz /= l;
  const yx = zy * xz - zz * xy, yy = zz * xx - zx * xz, yz = zx * xy - zy * xx;
  return new Float32Array([xx, yx, zx, 0, xy, yy, zy, 0, xz, yz, zz, 0,
    -(xx * eye[0] + xy * eye[1] + xz * eye[2]), -(yx * eye[0] + yy * eye[1] + yz * eye[2]), -(zx * eye[0] + zy * eye[1] + zz * eye[2]), 1]);
}
function multiply(a, b, out = new Float32Array(16)) {
  for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) {
    out[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
  }
  return out;
}
// Frustum planes of a column-major clip matrix, normalised, inside = positive.
function frustumPlanes(m, out) {
  const row = (r) => [m[r], m[4 + r], m[8 + r], m[12 + r]];
  const w = row(3), sets = [row(0), row(1), row(2)];
  let o = 0;
  for (let i = 0; i < 3; i++) for (const sign of [1, -1]) {
    const a = w[0] + sign * sets[i][0], b = w[1] + sign * sets[i][1], c = w[2] + sign * sets[i][2], d = w[3] + sign * sets[i][3];
    const l = Math.hypot(a, b, c) || 1;
    out[o++] = a / l; out[o++] = b / l; out[o++] = c / l; out[o++] = d / l;
  }
  return out;
}

function compile(gl, vs, fs) {
  const program = gl.createProgram();
  for (const [type, source] of [[gl.VERTEX_SHADER, vs], [gl.FRAGMENT_SHADER, fs]]) {
    const shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) throw new Error('Shader: ' + gl.getShaderInfoLog(shader));
    gl.attachShader(program, shader);
  }
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error('Program: ' + gl.getProgramInfoLog(program));
  const uniforms = {};
  const count = gl.getProgramParameter(program, gl.ACTIVE_UNIFORMS);
  for (let i = 0; i < count; i++) { const info = gl.getActiveUniform(program, i); uniforms[info.name] = gl.getUniformLocation(program, info.name); }
  return { program, uniforms };
}

export class ViewportRasterization {
  constructor(canvas) {
    this.canvas = canvas;
    const gl = canvas.getContext('webgl2', { antialias: true, alpha: true, premultipliedAlpha: true, powerPreference: 'high-performance' });
    if (!gl) throw new Error('WebGL2 is not available in this browser');
    this.gl = gl;
    this.multiDraw = gl.getExtension('WEBGL_multi_draw');
    this.surface = compile(gl, VS, FS);
    this.picker = compile(gl, VS, PICK_FS);
    this.lines = compile(gl, LINE_VS, LINE_FS);
    this.lineBuffer = gl.createBuffer();
    this.lineArray = gl.createVertexArray();
    gl.bindVertexArray(this.lineArray);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.lineBuffer);
    gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 28, 0);
    gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 4, gl.FLOAT, false, 28, 12);
    gl.bindVertexArray(null);
    this.lineData = [];
    this.geometry = null;
    this.instances = [];
    this.frozen = null;
    this.planes = new Float32Array(24);
    this.model = new Float32Array(16);
    this.modelViewProj = new Float32Array(16);
    this.statistics = { triangles: 0, clusters: 0, tests: 0, culledFrustum: 0, culledBackface: 0, ranges: 0, drawCalls: 0, instancesVisible: 0, selectMs: 0, levelTriangles: new Uint32Array(32), levelClusters: new Uint32Array(32) };
    this.pickTarget = null;
  }

  dispose() {
    const gl = this.gl, a = this.geometry;
    if (!a) return;
    for (const t of [a.positions, a.normals, a.clusterA, a.clusterB]) gl.deleteTexture(t);
    gl.deleteBuffer(a.corners); gl.deleteVertexArray(a.array);
    this.geometry = null;
  }

  floatTexture(width, data, count) {
    const gl = this.gl;
    const height = Math.max(1, Math.ceil(count / width));
    const padded = new Float32Array(width * height * 4);
    padded.set(data);
    const texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, width, height, 0, gl.RGBA, gl.FLOAT, padded);
    for (const p of [gl.TEXTURE_MIN_FILTER, gl.TEXTURE_MAG_FILTER]) gl.texParameteri(gl.TEXTURE_2D, p, gl.NEAREST);
    for (const p of [gl.TEXTURE_WRAP_S, gl.TEXTURE_WRAP_T]) gl.texParameteri(gl.TEXTURE_2D, p, gl.CLAMP_TO_EDGE);
    return texture;
  }

  setGeometry(result) {
    this.dispose();
    const gl = this.gl, h = result.hierarchy, vertexCount = result.vertexCount;
    const expand = (source) => {
      const out = new Float32Array(vertexCount * 4);
      for (let i = 0; i < vertexCount; i++) { out[i * 4] = source[i * 3]; out[i * 4 + 1] = source[i * 3 + 1]; out[i * 4 + 2] = source[i * 3 + 2]; }
      return out;
    };
    const A = new Float32Array(h.clusterCount * 4), B = new Float32Array(h.clusterCount * 4);
    for (let c = 0; c < h.clusterCount; c++) {
      const self = h.clusterGroupSelf[c];
      const sphere = self < 0 ? h.clusterSphere.subarray(c * 4, c * 4 + 4) : h.groupSphere.subarray(self * 4, self * 4 + 4);
      A.set(sphere, c * 4);
      B[c * 4] = self < 0 ? 0 : h.groupError[self];
      B[c * 4 + 1] = h.clusterLevel[c];
      B[c * 4 + 2] = self;
      B[c * 4 + 3] = h.clusterGroupParent[c];
    }
    // Interleaved (vertex id, cluster id) corner stream.
    const corners = new Uint32Array(h.cornerVertex.length * 2);
    for (let i = 0; i < h.cornerVertex.length; i++) { corners[i * 2] = h.cornerVertex[i]; corners[i * 2 + 1] = h.cornerCluster[i]; }
    const buffer = gl.createBuffer();
    const array = gl.createVertexArray();
    gl.bindVertexArray(array);
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.bufferData(gl.ARRAY_BUFFER, corners, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0); gl.vertexAttribIPointer(0, 1, gl.UNSIGNED_INT, 8, 0);
    gl.enableVertexAttribArray(1); gl.vertexAttribIPointer(1, 1, gl.UNSIGNED_INT, 8, 4);
    gl.bindVertexArray(null);
    let radius = 0;
    const low = [Infinity, Infinity, Infinity], high = [-Infinity, -Infinity, -Infinity];
    for (let i = 0; i < result.positions.length; i += 3) {
      radius = Math.max(radius, Math.hypot(result.positions[i], result.positions[i + 1], result.positions[i + 2]));
      for (let k = 0; k < 3; k++) { low[k] = Math.min(low[k], result.positions[i + k]); high[k] = Math.max(high[k], result.positions[i + k]); }
    }
    // Centre of the bounding box and the radius around it: the icon camera frames this, not the origin.
    const centre = [(low[0] + high[0]) / 2, (low[1] + high[1]) / 2, (low[2] + high[2]) / 2];
    let fitRadius = 0;
    for (let i = 0; i < result.positions.length; i += 3) {
      fitRadius = Math.max(fitRadius, Math.hypot(result.positions[i] - centre[0], result.positions[i + 1] - centre[1], result.positions[i + 2] - centre[2]));
    }
    this.geometry = {
      hierarchy: h, radius, centre, fitRadius: Math.max(fitRadius, 1e-6), lowest: result.lowest,
      positions: this.floatTexture(VERTEX_TEX, expand(result.positions), vertexCount),
      normals: this.floatTexture(VERTEX_TEX, expand(result.normals), vertexCount),
      clusterA: this.floatTexture(CLUSTER_TEX, A, h.clusterCount),
      clusterB: this.floatTexture(CLUSTER_TEX, B, h.clusterCount),
      corners: buffer, array,
      selection: createSelectionState(h),
      gpuBytes: corners.byteLength + vertexCount * 32 + h.clusterCount * 32,
    };
  }

  // Square grid of instances; each gets a deterministic yaw so silhouettes differ.
  setInstanceGrid(side, spacing) {
    this.instances = [];
    for (let j = 0; j < side; j++) for (let i = 0; i < side; i++) {
      const index = j * side + i;
      const yaw = side === 1 ? 0 : ((Math.imul(index + 1, 2654435761) >>> 0) / 4294967296) * Math.PI * 2;
      this.instances.push({ x: (i - (side - 1) / 2) * spacing, z: (j - (side - 1) / 2) * spacing, yaw, cos: Math.cos(yaw), sin: Math.sin(yaw) });
    }
    this.extent = ((side - 1) / 2) * spacing * Math.SQRT2 + (this.geometry ? this.geometry.radius : 1);
  }

  instanceMatrix(instance, out) {
    out.fill(0);
    out[0] = instance.cos; out[2] = -instance.sin; out[5] = 1; out[8] = instance.sin; out[10] = instance.cos;
    out[12] = instance.x; out[14] = instance.z; out[15] = 1;
    return out;
  }
  toWorld(instance, p) {
    return [p[0] * instance.cos + p[2] * instance.sin + instance.x, p[1], -p[0] * instance.sin + p[2] * instance.cos + instance.z];
  }
  // The instance nearest the origin: the one the icon camera frames when the grid holds several copies.
  centreInstance() {
    let best = 0, distance = Infinity;
    this.instances.forEach((instance, index) => {
      const d = instance.x * instance.x + instance.z * instance.z;
      if (d < distance) { distance = d; best = index; }
    });
    return best;
  }
  toLocal(instance, p) {
    const x = p[0] - instance.x, z = p[2] - instance.z;
    return [x * instance.cos - z * instance.sin, p[1], x * instance.sin + z * instance.cos];
  }

  cameraMatrices(camera, width, height) {
    const distance = Math.hypot(camera.eye[0] - camera.target[0], camera.eye[1] - camera.target[1], camera.eye[2] - camera.target[2]);
    const reach = Math.hypot(camera.eye[0], camera.eye[1], camera.eye[2]) + (this.extent || 2);
    const near = Math.min(1, Math.max(0.002, distance * 0.02));
    const far = Math.max(50, reach * 1.5);
    const viewProj = multiply(perspective(camera.fov, width / height, near, far), lookAt(camera.eye, camera.target));
    return { viewProj, near, focal: height / (2 * Math.tan(camera.fov / 2)), eye: camera.eye.slice() };
  }

  freeze(camera, width, height) { this.frozen = { ...this.cameraMatrices(camera, width, height), aspect: width / height, fov: camera.fov, target: camera.target.slice() }; }
  unfreeze() { this.frozen = null; }

  // Runs selection per instance and submits the ranges with `program` already bound.
  submitInstances(program, lod, settings, collect, onlyIndex = -1) {
    const gl = this.gl, a = this.geometry, h = a.hierarchy, state = a.selection, stats = this.statistics;
    const view = { eye: null, focal: lod.focal, near: lod.near, threshold: settings.threshold, planes: settings.frustumCull ? this.planes : null, backface: settings.backfaceCull, forcedLevel: settings.forcedLevel };
    let firstInstance = true;
    for (let index = 0; index < this.instances.length; index++) {
      if (onlyIndex >= 0 && index !== onlyIndex) continue;
      const instance = this.instances[index];
      this.instanceMatrix(instance, this.model);
      view.eye = this.toLocal(instance, lod.eye);
      if (settings.frustumCull) {
        frustumPlanes(multiply(lod.viewProj, this.model, this.modelViewProj), this.planes);
        // Whole-instance reject before touching the DAG.
        let outside = false;
        for (let p = 0; p < 24; p += 4) if (this.planes[p + 3] < -a.radius) { outside = true; break; }
        if (outside) continue;
      }
      selectClusters(h, state, view, collect && firstInstance);
      firstInstance = false;
      if (!state.rangeCount) continue;
      gl.uniformMatrix4fv(program.uniforms.uModel, false, this.model);
      gl.uniform3fv(program.uniforms.uEyeLocal, view.eye);
      gl.uniform1i(program.uniforms.uInstance, index);
      if (this.multiDraw) {
        this.multiDraw.multiDrawArraysWEBGL(gl.TRIANGLES, state.firsts, 0, state.counts, 0, state.rangeCount);
      } else {
        for (let r = 0; r < state.rangeCount; r++) gl.drawArrays(gl.TRIANGLES, state.firsts[r], state.counts[r]);
      }
      if (collect) { stats.ranges += state.rangeCount; stats.drawCalls += this.multiDraw ? 1 : state.rangeCount; stats.instancesVisible++; }
    }
    if (collect) {
      stats.triangles = firstInstance ? 0 : state.triangles; stats.clusters = firstInstance ? 0 : state.clusters; stats.tests = firstInstance ? 0 : state.tests;
      stats.culledFrustum = firstInstance ? 0 : state.culledFrustum; stats.culledBackface = firstInstance ? 0 : state.culledBackface;
      if (firstInstance) { stats.levelTriangles.fill(0); stats.levelClusters.fill(0); } else { stats.levelTriangles.set(state.levelTriangles); stats.levelClusters.set(state.levelClusters); }
    }
  }

  bindSurface(program, camera, lod, settings) {
    const gl = this.gl, a = this.geometry, u = program.uniforms;
    gl.useProgram(program.program);
    gl.bindVertexArray(a.array);
    [a.positions, a.normals, a.clusterA, a.clusterB].forEach((texture, unit) => { gl.activeTexture(gl.TEXTURE0 + unit); gl.bindTexture(gl.TEXTURE_2D, texture); });
    gl.uniform1i(u.uPositions, 0); gl.uniform1i(u.uNormals, 1); gl.uniform1i(u.uClusterA, 2); gl.uniform1i(u.uClusterB, 3);
    gl.uniformMatrix4fv(u.uViewProj, false, camera.viewProj);
    gl.uniform3fv(u.uEyeWorld, camera.eye);
    gl.uniform1f(u.uFocal, lod.focal); gl.uniform1f(u.uNear, lod.near); gl.uniform1f(u.uThreshold, settings.threshold);
    gl.uniform1i(u.uMode, Math.max(0, VIEW_MODES.findIndex((m) => m.id === settings.viewMode)));
    gl.uniform1i(u.uLevelCount, a.hierarchy.levels.length);
    gl.uniform1f(u.uWire, settings.wireframe ? 0.85 : 0);
    gl.uniform1f(u.uFlat, settings.flatShading ? 1 : 0);
    gl.uniform1i(u.uSelectedCluster, settings.selected ? settings.selected.cluster : -1);
    gl.uniform1i(u.uSelectedInstance, settings.selected ? settings.selected.instance : -1);
  }

  resize() {
    const ratio = Math.min(window.devicePixelRatio || 1, 2);
    const width = Math.max(2, Math.round(this.canvas.clientWidth * ratio)), height = Math.max(2, Math.round(this.canvas.clientHeight * ratio));
    if (this.canvas.width !== width || this.canvas.height !== height) { this.canvas.width = width; this.canvas.height = height; }
  }

  render(cameraInput, settings) {
    const gl = this.gl, stats = this.statistics;
    this.resize();
    const width = this.canvas.width, height = this.canvas.height;
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, width, height);
    gl.clearColor(0.043, 0.045, 0.05, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    if (!this.geometry) return stats;
    const camera = this.cameraMatrices(cameraInput, width, height);
    // LOD + culling use the frozen camera when present, so the cut can be inspected from outside.
    const lod = this.frozen ? { ...this.frozen, focal: height / (2 * Math.tan(this.frozen.fov / 2)) } : camera;
    this.lodView = lod;
    gl.enable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    if (settings.cullBackTriangles) { gl.enable(gl.CULL_FACE); gl.cullFace(gl.BACK); } else gl.disable(gl.CULL_FACE);
    stats.ranges = 0; stats.drawCalls = 0; stats.instancesVisible = 0;
    const started = performance.now();
    this.bindSurface(this.surface, camera, lod, settings);
    this.submitInstances(this.surface, lod, settings, true);
    stats.selectMs = performance.now() - started;
    gl.bindVertexArray(null);
    this.renderLines(camera, settings);
    return stats;
  }

  // --- Overlay lines: ground grid, frozen frustum, selected cluster bounds ---------------------------------
  pushLine(a, b, color) { this.lineData.push(a[0], a[1], a[2], ...color, b[0], b[1], b[2], ...color); }
  pushSphere(instance, sphere, color) {
    const segments = 48, c = [sphere[0], sphere[1], sphere[2]], r = sphere[3];
    const world = (p) => [p[0] * instance.cos + p[2] * instance.sin + instance.x, p[1], -p[0] * instance.sin + p[2] * instance.cos + instance.z];
    for (let axis = 0; axis < 3; axis++) {
      let previous = null;
      for (let i = 0; i <= segments; i++) {
        const t = (i / segments) * Math.PI * 2, p = [c[0], c[1], c[2]];
        p[(axis + 1) % 3] += Math.cos(t) * r; p[(axis + 2) % 3] += Math.sin(t) * r;
        const w = world(p);
        if (previous) this.pushLine(previous, w, color);
        previous = w;
      }
    }
  }
  renderLines(camera, settings) {
    const gl = this.gl, a = this.geometry, h = a.hierarchy;
    this.lineData.length = 0;
    if (settings.grid) {
      const y = a.lowest - 0.02, step = Math.max(0.5, Math.pow(2, Math.round(Math.log2((this.extent || 2) / 12))));
      const half = Math.ceil(((this.extent || 2) * 1.6) / step) * step;
      for (let v = -half; v <= half + 1e-6; v += step) {
        const strong = Math.abs(v) < 1e-6 ? 0.34 : 0.13;
        this.pushLine([v, y, -half], [v, y, half], [1, 1, 1, strong]);
        this.pushLine([-half, y, v], [half, y, v], [1, 1, 1, strong]);
      }
    }
    if (this.frozen) {
      const f = this.frozen, t = Math.tan(f.fov / 2), depth = Math.min(6, Math.max(1.5, (this.extent || 2) * 0.5));
      let zx = f.eye[0] - f.target[0], zy = f.eye[1] - f.target[1], zz = f.eye[2] - f.target[2];
      let l = Math.hypot(zx, zy, zz) || 1; zx /= l; zy /= l; zz /= l;
      let xx = zz, xz = -zx; l = Math.hypot(xx, xz) || 1; xx /= l; xz /= l;
      const yx = zy * xz, yy = zz * xx - zx * xz, yz = -zy * xx;
      const corner = (sx, sy) => [
        f.eye[0] + (-zx + xx * sx * t * f.aspect + yx * sy * t) * depth,
        f.eye[1] + (-zy + yy * sy * t) * depth,
        f.eye[2] + (-zz + xz * sx * t * f.aspect + yz * sy * t) * depth];
      const corners = [corner(-1, -1), corner(1, -1), corner(1, 1), corner(-1, 1)], color = [1, 0.72, 0.2, 0.9];
      for (let i = 0; i < 4; i++) { this.pushLine(f.eye, corners[i], color); this.pushLine(corners[i], corners[(i + 1) % 4], color); }
    }
    const selected = settings.selected;
    if (selected && settings.bounds && this.instances[selected.instance] && selected.cluster < h.clusterCount) {
      const instance = this.instances[selected.instance], c = selected.cluster;
      this.pushSphere(instance, h.clusterSphere.subarray(c * 4, c * 4 + 4), [1, 1, 1, 0.85]);
      const self = h.clusterGroupSelf[c], parent = h.clusterGroupParent[c];
      if (self >= 0) this.pushSphere(instance, h.groupSphere.subarray(self * 4, self * 4 + 4), [1, 0.72, 0.2, 0.75]);
      if (parent >= 0) this.pushSphere(instance, h.groupSphere.subarray(parent * 4, parent * 4 + 4), [0.35, 0.65, 1, 0.75]);
    }
    if (!this.lineData.length) return;
    gl.useProgram(this.lines.program);
    gl.uniformMatrix4fv(this.lines.uniforms.uViewProj, false, camera.viewProj);
    gl.bindVertexArray(this.lineArray);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.lineBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(this.lineData), gl.DYNAMIC_DRAW);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    gl.depthMask(false);
    gl.drawArrays(gl.LINES, 0, this.lineData.length / 7);
    gl.depthMask(true);
    gl.disable(gl.BLEND);
    gl.bindVertexArray(null);
  }

  // Renders the centre instance alone into a size x size buffer with a transparent background. `camera` must
  // already carry the icon crop as its fov. Returns top-down, unpremultiplied RGBA bytes.
  captureIcon(camera, settings, size) {
    const gl = this.gl;
    const previous = [this.canvas.width, this.canvas.height];
    this.canvas.width = size; this.canvas.height = size;
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, size, size);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.DEPTH_TEST); gl.disable(gl.BLEND);
    if (settings.cullBackTriangles) { gl.enable(gl.CULL_FACE); gl.cullFace(gl.BACK); } else gl.disable(gl.CULL_FACE);
    const view = this.cameraMatrices(camera, size, size);
    const quiet = { ...settings, selected: null };
    this.bindSurface(this.surface, view, view, quiet);
    this.submitInstances(this.surface, view, quiet, false, this.centreInstance());
    gl.bindVertexArray(null);
    const raw = new Uint8Array(size * size * 4);
    gl.readPixels(0, 0, size, size, gl.RGBA, gl.UNSIGNED_BYTE, raw);
    this.canvas.width = previous[0]; this.canvas.height = previous[1];
    // The framebuffer holds premultiplied colour; PNG wants straight alpha. Rows are flipped to top-down.
    const out = new Uint8ClampedArray(size * size * 4);
    for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
      const s = (y * size + x) * 4, d = ((size - 1 - y) * size + x) * 4, alpha = raw[s + 3];
      if (!alpha) continue;
      const k = 255 / alpha;
      out[d] = Math.min(255, raw[s] * k); out[d + 1] = Math.min(255, raw[s + 1] * k); out[d + 2] = Math.min(255, raw[s + 2] * k); out[d + 3] = alpha;
    }
    return out;
  }

  // Returns { cluster, instance } under the canvas-space pixel, or null.
  pick(cameraInput, settings, clientX, clientY) {
    if (!this.geometry) return null;
    const gl = this.gl, width = this.canvas.width, height = this.canvas.height;
    if (!this.pickTarget || this.pickTarget.width !== width || this.pickTarget.height !== height) {
      if (this.pickTarget) { gl.deleteFramebuffer(this.pickTarget.framebuffer); gl.deleteTexture(this.pickTarget.color); gl.deleteRenderbuffer(this.pickTarget.depth); }
      const framebuffer = gl.createFramebuffer(), color = gl.createTexture(), depth = gl.createRenderbuffer();
      gl.bindTexture(gl.TEXTURE_2D, color);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.bindRenderbuffer(gl.RENDERBUFFER, depth);
      gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT24, width, height);
      gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, color, 0);
      gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, depth);
      this.pickTarget = { framebuffer, color, depth, width, height };
    }
    const camera = this.cameraMatrices(cameraInput, width, height);
    const lod = this.frozen ? { ...this.frozen, focal: height / (2 * Math.tan(this.frozen.fov / 2)) } : camera;
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.pickTarget.framebuffer);
    gl.viewport(0, 0, width, height);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.DEPTH_TEST); gl.disable(gl.BLEND);
    if (settings.cullBackTriangles) { gl.enable(gl.CULL_FACE); gl.cullFace(gl.BACK); } else gl.disable(gl.CULL_FACE);
    this.bindSurface(this.picker, camera, lod, settings);
    this.submitInstances(this.picker, lod, settings, false);
    gl.bindVertexArray(null);
    const bounds = this.canvas.getBoundingClientRect();
    const x = Math.floor(((clientX - bounds.left) / bounds.width) * width), y = Math.floor((1 - (clientY - bounds.top) / bounds.height) * height);
    const pixel = new Uint8Array(4);
    gl.readPixels(Math.min(width - 1, Math.max(0, x)), Math.min(height - 1, Math.max(0, y)), 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    const id = pixel[0] | (pixel[1] << 8) | (pixel[2] << 16);
    return id ? { cluster: id - 1, instance: pixel[3] - 1 } : null;
  }
}
