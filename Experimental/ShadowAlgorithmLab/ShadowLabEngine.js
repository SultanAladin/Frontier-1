import {
  FULLSCREEN_VERT,
  GBUFFER_VERT,
  GBUFFER_FRAG,
  LIGHT_DEPTH_VERT,
  LIGHT_DEPTH_FRAG,
  SHADOW_PCF_FRAG,
  SHADOW_PCSS_FRAG,
  SHADOW_CONTACT_PCSS_FRAG,
  SHADOW_SMRT_FRAG,
  SHADOW_SDF_FRAG,
  SHADOW_HYBRID_FRAG,
  SHADOW_BVH_FRAG,
  EMISSIVE_MAP_SHADOW_FRAG,
  EMISSIVE_BVH_SHADOW_FRAG,
  PLANE_FILTER_FRAG,
  SDF_STATIC_SHADE_FRAG,
  SDF_RADIOSITY_BOUNCE_FRAG,
  SDF_UNION_FRAG,
  GI_SDF_FRAG,
  GI_BVH_FRAG,
  GI_BILATERAL_FRAG,
  REFLECTION_SDF_FRAG,
  REFLECTION_BVH_FRAG,
  COMPOSITE_FRAG,
} from "./ShadowLabShaders.js";

//========================================================================================================================
// RIGHT-HANDED LINEAR ALGEBRA (+X Forward, +Y Left, +Z Up; X x Y = +Z)
//========================================================================================================================
function mat4Identity() {
  const m = new Float32Array(16);
  m[0] = 1; m[5] = 1; m[10] = 1; m[15] = 1;
  return m;
}

function mat4Multiply(a, b) {
  const out = new Float32Array(16);
  for (let col = 0; col < 4; col++) {
    for (let row = 0; row < 4; row++) {
      out[col * 4 + row] =
        a[0 * 4 + row] * b[col * 4 + 0] +
        a[1 * 4 + row] * b[col * 4 + 1] +
        a[2 * 4 + row] * b[col * 4 + 2] +
        a[3 * 4 + row] * b[col * 4 + 3];
    }
  }
  return out;
}

function mat4ComposeRigid(tx, ty, tz, yawZ, pitchY = 0, rollX = 0) {
  const cz = Math.cos(yawZ), sz = Math.sin(yawZ);
  const cy = Math.cos(pitchY), sy = Math.sin(pitchY);
  const cx = Math.cos(rollX), sx = Math.sin(rollX);
  const m = new Float32Array(16);
  m[0] = cz * cy;
  m[1] = sz * cy;
  m[2] = -sy;
  m[4] = cz * sy * sx - sz * cx;
  m[5] = sz * sy * sx + cz * cx;
  m[6] = cy * sx;
  m[8] = cz * sy * cx + sz * sx;
  m[9] = sz * sy * cx - cz * sx;
  m[10] = cy * cx;
  m[12] = tx;
  m[13] = ty;
  m[14] = tz;
  m[15] = 1;
  return m;
}

function mat4InvertRigid(m) {
  const inv = new Float32Array(16);
  inv[0] = m[0]; inv[1] = m[4]; inv[2]  = m[8];
  inv[4] = m[1]; inv[5] = m[5]; inv[6]  = m[9];
  inv[8] = m[2]; inv[9] = m[6]; inv[10] = m[10];
  const tx = m[12], ty = m[13], tz = m[14];
  inv[12] = -(inv[0] * tx + inv[4] * ty + inv[8]  * tz);
  inv[13] = -(inv[1] * tx + inv[5] * ty + inv[9]  * tz);
  inv[14] = -(inv[2] * tx + inv[6] * ty + inv[10] * tz);
  inv[15] = 1;
  return inv;
}

function mat4InvertGeneral(m) {
  const inv = new Float32Array(16);
  inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
  inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
  inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
  inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
  inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
  inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
  inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
  inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
  inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
  inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
  inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
  inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
  inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
  inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
  inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
  inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
  let det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
  det = Math.abs(det) > 1e-12 ? 1.0 / det : 0.0;
  for (let i = 0; i < 16; i++) inv[i] *= det;
  return inv;
}

function mat4LookAtRH(eye, target, up = [0, 0, 1]) {
  let fx = target[0] - eye[0], fy = target[1] - eye[1], fz = target[2] - eye[2];
  const fl = Math.hypot(fx, fy, fz) || 1;
  fx /= fl; fy /= fl; fz /= fl;

  let sx = fy * up[2] - fz * up[1];
  let sy = fz * up[0] - fx * up[2];
  let sz = fx * up[1] - fy * up[0];
  const sl = Math.hypot(sx, sy, sz) || 1;
  sx /= sl; sy /= sl; sz /= sl;

  const ux = sy * fz - sz * fy;
  const uy = sz * fx - sx * fz;
  const uz = sx * fy - sy * fx;

  const m = new Float32Array(16);
  m[0] = sx;  m[4] = sy;  m[8]  = sz;  m[12] = -(sx * eye[0] + sy * eye[1] + sz * eye[2]);
  m[1] = ux;  m[5] = uy;  m[9]  = uz;  m[13] = -(ux * eye[0] + uy * eye[1] + uz * eye[2]);
  m[2] = -fx; m[6] = -fy; m[10] = -fz; m[14] =  (fx * eye[0] + fy * eye[1] + fz * eye[2]);
  m[15] = 1;
  return m;
}

function mat4PerspectiveRH(fovYRad, aspect, near, far) {
  const f = 1.0 / Math.tan(fovYRad * 0.5);
  const nf = 1.0 / (near - far);
  const m = new Float32Array(16);
  m[0] = f / aspect;
  m[5] = f;
  m[10] = (far + near) * nf;
  m[11] = -1;
  m[14] = 2 * far * near * nf;
  return m;
}

function mat4OrthoRH(left, right, bottom, top, near, far) {
  const m = new Float32Array(16);
  m[0] = 2 / (right - left);
  m[5] = 2 / (top - bottom);
  m[10] = -2 / (far - near);
  m[12] = -(right + left) / (right - left);
  m[13] = -(top + bottom) / (top - bottom);
  m[14] = -(far + near) / (far - near);
  m[15] = 1;
  return m;
}

function decodeFloat16(h) {
  const s = (h & 0x8000) >> 15;
  const e = (h & 0x7C00) >> 10;
  const f = h & 0x03FF;
  if (e === 0) return (s ? -1 : 1) * Math.pow(2, -14) * (f / 1024);
  if (e === 0x1F) return f ? NaN : (s ? -Infinity : Infinity);
  return (s ? -1 : 1) * Math.pow(2, e - 15) * (1 + f / 1024);
}

const _f32View = new Float32Array(1);
const _u32View = new Uint32Array(_f32View.buffer);
function encodeFloat16(val) {
  _f32View[0] = val;
  const x = _u32View[0];
  const sign = (x >>> 16) & 0x8000;
  let exp = ((x >>> 23) & 0xff) - 127 + 15;
  let mant = x & 0x007fffff;
  if (exp <= 0) {
    if (exp < -10) return sign;
    mant = (mant | 0x00800000) >>> (1 - exp);
    return sign | ((mant + 0x00001000) >>> 13);
  }
  if (exp >= 31) return sign | 0x7bff;
  return sign | (exp << 10) | ((mant + 0x00001000) >>> 13);
}

function unpackOctahedralJs(packed20) {
  const p = Math.round(packed20);
  const u = ((p & 1023) / 1023.0) * 2.0 - 1.0;
  const v = (((p >>> 10) & 1023) / 1023.0) * 2.0 - 1.0;
  let nx = u, ny = v, nz = 1.0 - Math.abs(u) - Math.abs(v);
  if (nz < 0.0) {
    const ox = nx, oy = ny;
    nx = (1.0 - Math.abs(oy)) * (ox >= 0 ? 1 : -1);
    ny = (1.0 - Math.abs(ox)) * (oy >= 0 ? 1 : -1);
  }
  const len = Math.hypot(nx, ny, nz) || 1;
  return [nx / len, ny / len, nz / len];
}

function packOctahedralJs(nx, ny, nz) {
  const invL1 = 1.0 / (Math.abs(nx) + Math.abs(ny) + Math.abs(nz) + 1e-12);
  let x = nx * invL1, y = ny * invL1;
  if (nz < 0) {
    const ox = x, oy = y;
    x = (1 - Math.abs(oy)) * (ox >= 0 ? 1 : -1);
    y = (1 - Math.abs(ox)) * (oy >= 0 ? 1 : -1);
  }
  const ix = Math.max(0, Math.min(1023, Math.round((x * 0.5 + 0.5) * 1023)));
  const iy = Math.max(0, Math.min(1023, Math.round((y * 0.5 + 0.5) * 1023)));
  return ix | (iy << 10);
}

//========================================================================================================================
// CUSTOM TRIANGLE MESH: SHADOW CALIBRATION LOUVERS + ONCE-AT-STARTUP EXACT EDT SDF + SAH BLAS
//========================================================================================================================
function createCalibrationLouversGeometry() {
  const boxes = [];
  boxes.push({ cx: -5.20, cy: 2.85, cz: 1.38, sx: 0.14, sy: 0.14, sz: 2.76, tiltY: 0 });
  boxes.push({ cx: -1.40, cy: 2.85, cz: 1.38, sx: 0.14, sy: 0.14, sz: 2.76, tiltY: 0 });
  boxes.push({ cx: -5.20, cy: 4.55, cz: 1.38, sx: 0.14, sy: 0.14, sz: 2.76, tiltY: 0 });
  boxes.push({ cx: -1.40, cy: 4.55, cz: 1.38, sx: 0.14, sy: 0.14, sz: 2.76, tiltY: 0 });
  boxes.push({ cx: -3.30, cy: 2.85, cz: 2.68, sx: 4.10, sy: 0.10, sz: 0.12, tiltY: 0 });
  boxes.push({ cx: -3.30, cy: 4.55, cz: 2.68, sx: 4.10, sy: 0.10, sz: 0.12, tiltY: 0 });
  for (let i = 0; i < 11; i++) {
    const x = -5.05 + i * 0.35;
    boxes.push({ cx: x, cy: 3.70, cz: 2.66, sx: 0.045, sy: 1.85, sz: 0.13, tiltY: 0.32 });
  }

  const tris = [];
  const faceDefs = [
    [[ 1, -1, -1], [ 1,  1, -1], [ 1,  1,  1], [ 1, -1,  1], [ 1,  0,  0]],
    [[-1,  1, -1], [-1, -1, -1], [-1, -1,  1], [-1,  1,  1], [-1,  0,  0]],
    [[-1,  1,  1], [ 1,  1,  1], [ 1,  1, -1], [-1,  1, -1], [ 0,  1,  0]],
    [[-1, -1, -1], [ 1, -1, -1], [ 1, -1,  1], [-1, -1,  1], [ 0, -1,  0]],
    [[-1, -1,  1], [ 1, -1,  1], [ 1,  1,  1], [-1,  1,  1], [ 0,  0,  1]],
    [[-1,  1, -1], [ 1,  1, -1], [ 1, -1, -1], [-1, -1, -1], [ 0,  0, -1]],
  ];

  for (const b of boxes) {
    const hx = b.sx * 0.5, hy = b.sy * 0.5, hz = b.sz * 0.5;
    const c = Math.cos(b.tiltY), s = Math.sin(b.tiltY);
    const xf = (p) => {
      const lx = p[0] * hx, ly = p[1] * hy, lz = p[2] * hz;
      return [b.cx + lx * c + lz * s, b.cy + ly, b.cz - lx * s + lz * c];
    };
    const xfn = (n) => [n[0] * c + n[2] * s, n[1], -n[0] * s + n[2] * c];
    for (const [q0, q1, q2, q3, fn] of faceDefs) {
      const v0 = xf(q0), v1 = xf(q1), v2 = xf(q2), v3 = xf(q3), n = xfn(fn);
      tris.push({ p0: v0, p1: v1, p2: v2, n, matId: 21 });
      tris.push({ p0: v0, p1: v2, p2: v3, n, matId: 21 });
    }
  }
  return { boxes, tris };
}

function createEmissivePanelGeometry() {
  const center = [-0.25, -3.45, 1.32];
  const aimAt  = [ 0.00,  1.10, 0.45];
  let nx = aimAt[0] - center[0], ny = aimAt[1] - center[1], nz = aimAt[2] - center[2];
  const nl = Math.hypot(nx, ny, nz) || 1;
  nx /= nl; ny /= nl; nz /= nl;
  const normal = [nx, ny, nz];

  // right = normalize(cross(normal, [0, 0, 1])), up = cross(right, normal)
  let rx = ny * 1 - nz * 0, ry = nz * 0 - nx * 1, rz = nx * 0 - ny * 0;
  const rl = Math.hypot(rx, ry, rz) || 1;
  rx /= rl; ry /= rl; rz /= rl;
  const right = [rx, ry, rz];
  const up = [ry * nz - rz * ny, rz * nx - rx * nz, rx * ny - ry * nx];

  const halfW = 1.25, halfH = 0.30;
  const tris = [];
  const pushQuad = (a, b, c, d, n, matId) => {
    tris.push({ p0: a, p1: b, p2: c, n, matId });
    tris.push({ p0: a, p1: c, p2: d, n, matId });
  };

  // Glowing front face (matId = 22)
  const pt = (u, v, w) => [
    center[0] + right[0]*u + up[0]*v + normal[0]*w,
    center[1] + right[1]*u + up[1]*v + normal[1]*w,
    center[2] + right[2]*u + up[2]*v + normal[2]*w,
  ];
  pushQuad(pt(-halfW, -halfH, 0.03), pt(halfW, -halfH, 0.03), pt(halfW, halfH, 0.03), pt(-halfW, halfH, 0.03), normal, 22);

  // Dark anodized backing bezel & housing behind glowing surface (matId = 4)
  const hw = halfW + 0.07, hh = halfH + 0.07;
  const zF = 0.025, zB = -0.09;
  const negN = [-normal[0], -normal[1], -normal[2]];
  const negR = [-right[0], -right[1], -right[2]];
  const negU = [-up[0], -up[1], -up[2]];
  pushQuad(pt(hw, -hh, zB), pt(-hw, -hh, zB), pt(-hw, hh, zB), pt(hw, hh, zB), negN, 4);
  pushQuad(pt(-hw, hh, zB), pt(-hw, hh, zF), pt(hw, hh, zF), pt(hw, hh, zB), up, 4);
  pushQuad(pt(-hw, -hh, zF), pt(-hw, -hh, zB), pt(hw, -hh, zB), pt(hw, -hh, zF), negU, 4);
  pushQuad(pt(hw, -hh, zB), pt(hw, hh, zB), pt(hw, hh, zF), pt(hw, -hh, zF), right, 4);
  pushQuad(pt(-hw, hh, zB), pt(-hw, -hh, zB), pt(-hw, -hh, zF), pt(-hw, hh, zF), negR, 4);

  // Twin vertical steel stanchion legs down to ground z = 0
  const faceDefs = [
    [[ 1,-1,-1],[ 1, 1,-1],[ 1, 1, 1],[ 1,-1, 1],[ 1, 0, 0]],
    [[-1, 1,-1],[-1,-1,-1],[-1,-1, 1],[-1, 1, 1],[-1, 0, 0]],
    [[-1, 1, 1],[ 1, 1, 1],[ 1, 1,-1],[-1, 1,-1],[ 0, 1, 0]],
    [[-1,-1,-1],[ 1,-1,-1],[ 1,-1, 1],[-1,-1, 1],[ 0,-1, 0]],
    [[-1,-1, 1],[ 1,-1, 1],[ 1, 1, 1],[-1, 1, 1],[ 0, 0, 1]],
    [[-1, 1,-1],[ 1, 1,-1],[ 1,-1,-1],[-1,-1,-1],[ 0, 0,-1]],
  ];
  for (const sx of [-0.95, 0.95]) {
    const cx = center[0] + right[0] * sx - normal[0] * 0.06;
    const cy = center[1] + right[1] * sx - normal[1] * 0.06;
    const topZ = center[2] - 0.15;
    for (const [q0, q1, q2, q3, fn] of faceDefs) {
      const xf = (p) => [cx + p[0] * 0.045, cy + p[1] * 0.045, (p[2] > 0 ? topZ : 0.0)];
      pushQuad(xf(q0), xf(q1), xf(q2), xf(q3), fn, 4);
    }
  }

  // Perspective virtual shadow camera slightly behind glowing bar so whole 2.5m panel frustum projects cleanly
  const shadowOrigin = [center[0] - normal[0] * 0.72, center[1] - normal[1] * 0.72, Math.max(0.25, center[2] - normal[2] * 0.72)];
  const fovRad = (104.0 * Math.PI) / 180.0;
  const view = mat4LookAtRH(shadowOrigin, aimAt, [0, 0, 1]);
  const proj = mat4PerspectiveRH(fovRad, 1.0, 0.45, 42.0);
  const viewProj = mat4Multiply(proj, view);
  return {
    tris,
    rig: {
      center, right, up, normal, halfW, halfH,
      shadowOrigin,
      shadowForward: normal,
      viewProj,
      perspTan: Math.tan(fovRad * 0.5),
      res: 1024,
    },
  };
}

function buildTriangleBlas(tris, triGlobalOffset) {
  const items = tris.map((t, idx) => {
    const min = [
      Math.min(t.p0[0], t.p1[0], t.p2[0]),
      Math.min(t.p0[1], t.p1[1], t.p2[1]),
      Math.min(t.p0[2], t.p1[2], t.p2[2]),
    ];
    const max = [
      Math.max(t.p0[0], t.p1[0], t.p2[0]),
      Math.max(t.p0[1], t.p1[1], t.p2[1]),
      Math.max(t.p0[2], t.p1[2], t.p2[2]),
    ];
    const cen = [(min[0]+max[0])*0.5, (min[1]+max[1])*0.5, (min[2]+max[2])*0.5];
    return { t, idx, min, max, cen };
  });

  const reorderedTris = [];
  const nodes = [{ min: [0, 0, 0], max: [0, 0, 0], leftOrFirst: 0, count: 0 }];
  function populateNode(nodeIdx, list) {
    const bmin = [1e30, 1e30, 1e30], bmax = [-1e30, -1e30, -1e30];
    for (const it of list) {
      for (let k = 0; k < 3; k++) {
        if (it.min[k] < bmin[k]) bmin[k] = it.min[k];
        if (it.max[k] > bmax[k]) bmax[k] = it.max[k];
      }
    }
    if (list.length <= 4) {
      const first = triGlobalOffset + reorderedTris.length;
      for (const it of list) reorderedTris.push(it.t);
      nodes[nodeIdx] = { min: bmin, max: bmax, leftOrFirst: first, count: list.length };
      return;
    }
    const spans = [bmax[0]-bmin[0], bmax[1]-bmin[1], bmax[2]-bmin[2]];
    const axis = spans[0] >= spans[1] && spans[0] >= spans[2] ? 0 : (spans[1] >= spans[2] ? 1 : 2);
    list.sort((a, b) => a.cen[axis] - b.cen[axis]);
    const mid = list.length >> 1;
    const leftIdx = nodes.length;
    nodes.push(null, null);
    nodes[nodeIdx] = { min: bmin, max: bmax, leftOrFirst: leftIdx, count: 0 };
    populateNode(leftIdx, list.slice(0, mid));
    populateNode(leftIdx + 1, list.slice(mid));
  }
  populateNode(0, items);
  return { nodes, reorderedTris };
}

function edt1d(f, n, spacing, dOut, v, z) {
  const s2 = spacing * spacing;
  let k = 0;
  v[0] = 0; z[0] = -1e20; z[1] = 1e20;
  for (let q = 1; q < n; q++) {
    let s;
    while (true) {
      const r = v[k];
      s = ((f[q] + q * q * s2) - (f[r] + r * r * s2)) / (2 * (q - r) * s2);
      if (s <= z[k]) k--; else break;
    }
    k++; v[k] = q; z[k] = s; z[k + 1] = 1e20;
  }
  k = 0;
  for (let q = 0; q < n; q++) {
    while (z[k + 1] < q) k++;
    const dx = (q - v[k]) * spacing;
    dOut[q] = dx * dx + f[v[k]];
  }
}

function bakeLouversLocalSdfOnce(boxes) {
  const min = [-5.80, 2.35, -0.25];
  const max = [-0.90, 5.05, 3.10];
  const nx = 64, ny = 40, nz = 48;
  const dx = (max[0] - min[0]) / nx, dy = (max[1] - min[1]) / ny, dz = (max[2] - min[2]) / nz;
  const total = nx * ny * nz;
  const grid = new Float32Array(total);
  grid.fill(1e12);

  for (let iz = 0; iz < nz; iz++) {
    const wz = min[2] + (iz + 0.5) * dz;
    for (let iy = 0; iy < ny; iy++) {
      const wy = min[1] + (iy + 0.5) * dy;
      for (let ix = 0; ix < nx; ix++) {
        const wx = min[0] + (ix + 0.5) * dx;
        for (let b = 0; b < boxes.length; b++) {
          const box = boxes[b];
          const px = wx - box.cx, py = wy - box.cy, pz = wz - box.cz;
          const c = Math.cos(box.tiltY), s = Math.sin(box.tiltY);
          const lx = px * c - pz * s, lz = px * s + pz * c;
          if (Math.abs(lx) <= box.sx * 0.5 && Math.abs(py) <= box.sy * 0.5 && Math.abs(lz) <= box.sz * 0.5) {
            grid[(iz * ny + iy) * nx + ix] = 0.0;
            break;
          }
        }
      }
    }
  }

  const maxDim = Math.max(nx, ny, nz);
  const f = new Float32Array(maxDim), dOut = new Float32Array(maxDim);
  const v = new Int32Array(maxDim), z = new Float32Array(maxDim + 1);
  for (let iz = 0; iz < nz; iz++) for (let iy = 0; iy < ny; iy++) {
    const row = (iz * ny + iy) * nx;
    for (let ix = 0; ix < nx; ix++) f[ix] = grid[row + ix];
    edt1d(f, nx, dx, dOut, v, z);
    for (let ix = 0; ix < nx; ix++) grid[row + ix] = dOut[ix];
  }
  for (let iz = 0; iz < nz; iz++) for (let ix = 0; ix < nx; ix++) {
    for (let iy = 0; iy < ny; iy++) f[iy] = grid[(iz * ny + iy) * nx + ix];
    edt1d(f, ny, dy, dOut, v, z);
    for (let iy = 0; iy < ny; iy++) grid[(iz * ny + iy) * nx + ix] = dOut[iy];
  }
  for (let iy = 0; iy < ny; iy++) for (let ix = 0; ix < nx; ix++) {
    for (let iz = 0; iz < nz; iz++) f[iz] = grid[(iz * ny + iy) * nx + ix];
    edt1d(f, nz, dz, dOut, v, z);
    for (let iz = 0; iz < nz; iz++) grid[(iz * ny + iy) * nx + ix] = dOut[iz];
  }

  const payload16 = new Uint16Array(total);
  const voxelMin = Math.min(dx, dy, dz) * 0.35;
  for (let i = 0; i < total; i++) payload16[i] = encodeFloat16(Math.sqrt(Math.max(0, grid[i])) - voxelMin * 0.5);
  return { nx, ny, nz, min, max, payload16 };
}

//========================================================================================================================
// ALGORITHM METADATA & SHADER COMPILER
//========================================================================================================================
const ALGO_INFO = {
  contact_pcss: {
    badge: "CONTACT+PCSS",
    title: "Contact Clip + Screen-Space Contact + PCSS (No SDF!)",
    note: "<strong>Recommended #1 after BVH:</strong> 6 mm Contact Clip + 8-step screen-space contact ray locks tyres/trim cleanly, PCSS + Plane Filter softens distant penumbrae, zero SDF holes.",
  },
  pcss: {
    badge: "PCSS+FILTER",
    title: "Shadow Map · PCSS + Plane Filter",
    note: "<strong>#2 Fastest multi-light:</strong> Vogel blocker search + distance-hardening PCF + à-trous plane filter. Scales linearly across 3+ lights with zero SDF dependency.",
  },
  smrt: {
    badge: "SMRT",
    title: "SMRT · Depth Ray Trace (smrt-soft-shadows)",
    note: "<strong>#3 Without temporal:</strong> 14-step reverse quadratic march per ray; crisp close contact but ~2–3× heavier than PCSS across multiple lights.",
  },
  hybrid: {
    badge: "HYBRID+SDF",
    title: "Legacy Hybrid · Contact Clip + SDF Penumbra",
    note: "Blends Contact Map with Baked SDF cones at farW. Demonstrates how far SDF leaks/holes can creep into thin silhouettes.",
  },
  sdf: {
    badge: "SDF SH (HOLES)",
    title: "Baked Mesh SDF Cones (Artifact Comparison)",
    note: "8.6 cm Body / 9 cm Clip0 voxels punch holes through thin 3 cm panels & 4.5 cm louver slats -> why SDF belongs strictly to low-freq GI & matte reflections!",
  },
  pcf: {
    badge: "3×3 PCF",
    title: "Shadow Map · 3×3 PCF",
    note: "Constant-kernel 3×3 PCF baseline. Ultra-cheap, zero noise, no contact-hardening penumbra.",
  },
  bvh: {
    badge: "BVH SH",
    title: "Two-Level SAH BVH Raytraced Reference",
    note: "Exact TLAS+BLAS triangle intersection across all active lights. Physical ground truth.",
  },
};

const GI_LABELS = { off: "NO GI", sdf: "SDF GI", bvh: "BVH GI" };
const REFL_LABELS = { env: "ENV REFL", sdf: "SDF+SSR", bvh: "BVH REFL" };

function compileProgram(gl, vsSrc, fsSrc, label) {
  const vs = gl.createShader(gl.VERTEX_SHADER);
  gl.shaderSource(vs, vsSrc); gl.compileShader(vs);
  if (!gl.getShaderParameter(vs, gl.COMPILE_STATUS)) throw new Error(`[${label}] VS error: ` + gl.getShaderInfoLog(vs));
  const fs = gl.createShader(gl.FRAGMENT_SHADER);
  gl.shaderSource(fs, fsSrc); gl.compileShader(fs);
  if (!gl.getShaderParameter(fs, gl.COMPILE_STATUS)) throw new Error(`[${label}] FS error: ` + gl.getShaderInfoLog(fs));
  const prog = gl.createProgram();
  gl.attachShader(prog, vs); gl.attachShader(prog, fs); gl.linkProgram(prog);
  if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) throw new Error(`[${label}] Link error: ` + gl.getProgramInfoLog(prog));
  return prog;
}

const PAINT_PALETTE = [
  [0.92, 0.94, 0.98],
  [0.08, 0.37, 0.94],
  [0.86, 0.12, 0.14],
  [0.92, 0.68, 0.06],
  [0.05, 0.055, 0.07],
];

const KELVIN_COLORS = {
  warm: [1.00, 0.80, 0.56],
  day:  [1.00, 0.96, 0.90],
  cool: [0.84, 0.92, 1.00],
};

const QUALITY_PRESETS = [
  { bvhRays: 1,  smrtRays: 2,  pcssTaps: 8,  giSamples: 4,  giCones: 4  },
  { bvhRays: 2,  smrtRays: 4,  pcssTaps: 16, giSamples: 6,  giCones: 6  },
  { bvhRays: 4,  smrtRays: 6,  pcssTaps: 24, giSamples: 10, giCones: 8  },
  { bvhRays: 8,  smrtRays: 8,  pcssTaps: 32, giSamples: 14, giCones: 10 },
  { bvhRays: 16, smrtRays: 12, pcssTaps: 48, giSamples: 20, giCones: 14 },
];

const TLAS_RESERVED_NODES = 32;

//========================================================================================================================
// MAIN ENGINE
//========================================================================================================================
async function startShadowLab() {
  const canvas = document.getElementById("viewport-canvas");
  const gl = canvas.getContext("webgl2", { antialias: false, powerPreference: "high-performance" });
  if (!gl) {
    document.getElementById("loading-banner").textContent = "WebGL2 is required to run ShadowAlgorithmLab.";
    return;
  }

  const extFloat = gl.getExtension("EXT_color_buffer_float");
  gl.getExtension("OES_texture_float_linear");
  const extTimer = gl.getExtension("EXT_disjoint_timer_query_webgl2");
  const dbgRenderInfo = gl.getExtension("WEBGL_debug_renderer_info");
  const gpuString = dbgRenderInfo
    ? gl.getParameter(dbgRenderInfo.UNMASKED_RENDERER_WEBGL)
    : gl.getParameter(gl.RENDERER);
  document.getElementById("stat-gpu").textContent =
    (gpuString || "WebGL2 GPU").replace(/ANGLE \(([^,]+), ([^,(]+).*\)/, "$2").slice(0, 22);

  if (!extFloat) {
    document.getElementById("loading-banner").textContent = "EXT_color_buffer_float is required on this GPU.";
    return;
  }

  const resp = await fetch("./Assets/VehicleBenchmarkAssets.bin");
  if (!resp.ok) throw new Error("Failed to fetch Assets/VehicleBenchmarkAssets.bin");
  const arrayBuf = await resp.arrayBuffer();
  const dv = new DataView(arrayBuf);
  if (String.fromCharCode(dv.getUint8(0), dv.getUint8(1), dv.getUint8(2), dv.getUint8(3)) !== "FVB1") {
    throw new Error("Invalid FVB1 asset header");
  }

  const fileMeshCount = dv.getUint32(4, true);
  const fileBlasNodes = dv.getUint32(8, true);
  const fileTris      = dv.getUint32(12, true);
  const fileSdfCount  = dv.getUint32(16, true);

  let offset = 20;
  const meshMeta = [];
  for (let i = 0; i < fileMeshCount; i++) {
    meshMeta.push({
      nodeOffset: dv.getUint32(offset, true),
      nodeCount: dv.getUint32(offset + 4, true),
      triOffset: dv.getUint32(offset + 8, true),
      triCount: dv.getUint32(offset + 12, true),
    });
    offset += 16;
  }

  const blasFileByteStart = offset;
  offset += fileBlasNodes * 32;
  const triFileByteStart = offset;
  offset += fileTris * 64;

  const sdfVolumes = [];
  for (let s = 0; s < fileSdfCount; s++) {
    const nx = dv.getUint32(offset + 0, true);
    const ny = dv.getUint32(offset + 4, true);
    const nz = dv.getUint32(offset + 8, true);
    const byteLen = dv.getUint32(offset + 12, true);
    const min = [dv.getFloat32(offset + 16, true), dv.getFloat32(offset + 20, true), dv.getFloat32(offset + 24, true)];
    const max = [dv.getFloat32(offset + 28, true), dv.getFloat32(offset + 32, true), dv.getFloat32(offset + 36, true)];
    offset += 40;
    const payload = new Uint8Array(arrayBuf, offset, byteLen);
    offset += byteLen;
    while (offset % 4 !== 0) offset++;
    sdfVolumes.push({ nx, ny, nz, min, max, payload });
  }

  const louversGeo = createCalibrationLouversGeometry();
  const louversBlas = buildTriangleBlas(louversGeo.tris, fileTris);
  const louversSdfVol = bakeLouversLocalSdfOnce(louversGeo.boxes);

  const emissiveGeo = createEmissivePanelGeometry();
  const emissiveNodeOffset = fileBlasNodes + louversBlas.nodes.length;
  const emissiveTriOffset = fileTris + louversBlas.reorderedTris.length;
  const emissiveBlas = buildTriangleBlas(emissiveGeo.tris, emissiveTriOffset);
  const EMISSIVE_RIG = emissiveGeo.rig;

  meshMeta.push({
    nodeOffset: fileBlasNodes,
    nodeCount: louversBlas.nodes.length,
    triOffset: fileTris,
    triCount: louversBlas.reorderedTris.length,
  });
  meshMeta.push({
    nodeOffset: emissiveNodeOffset,
    nodeCount: emissiveBlas.nodes.length,
    triOffset: emissiveTriOffset,
    triCount: emissiveBlas.reorderedTris.length,
  });

  const totalBlasNodes = fileBlasNodes + louversBlas.nodes.length + emissiveBlas.nodes.length;
  const totalTris = fileTris + louversBlas.reorderedTris.length + emissiveBlas.reorderedTris.length;
  const totalCombinedNodes = TLAS_RESERVED_NODES + totalBlasNodes;

  const nodeTexWidth = 2048;
  const nodeTexHeight = Math.ceil((totalCombinedNodes * 2) / nodeTexWidth);
  const nodeTexData = new Float32Array(nodeTexWidth * nodeTexHeight * 4);
  const meshLocalBounds = meshMeta.map(() => ({ min: [0, 0, 0], max: [0, 0, 0] }));

  let nOff = blasFileByteStart;
  for (let n = 0; n < fileBlasNodes; n++) {
    const minX = dv.getFloat32(nOff + 0, true), minY = dv.getFloat32(nOff + 4, true), minZ = dv.getFloat32(nOff + 8, true);
    let leftOrFirst = Math.round(dv.getFloat32(nOff + 12, true));
    const maxX = dv.getFloat32(nOff + 16, true), maxY = dv.getFloat32(nOff + 20, true), maxZ = dv.getFloat32(nOff + 24, true);
    const count = Math.round(dv.getFloat32(nOff + 28, true));
    nOff += 32;
    if (count === 0) leftOrFirst += TLAS_RESERVED_NODES;
    const dst = (TLAS_RESERVED_NODES + n) * 2;
    nodeTexData[dst * 4 + 0] = minX; nodeTexData[dst * 4 + 1] = minY; nodeTexData[dst * 4 + 2] = minZ; nodeTexData[dst * 4 + 3] = leftOrFirst;
    nodeTexData[(dst + 1) * 4 + 0] = maxX; nodeTexData[(dst + 1) * 4 + 1] = maxY; nodeTexData[(dst + 1) * 4 + 2] = maxZ; nodeTexData[(dst + 1) * 4 + 3] = count;
    for (let m = 0; m < fileMeshCount; m++) {
      if (n === meshMeta[m].nodeOffset) {
        meshLocalBounds[m].min = [minX, minY, minZ];
        meshLocalBounds[m].max = [maxX, maxY, maxZ];
      }
    }
  }

  for (let n = 0; n < louversBlas.nodes.length; n++) {
    const nd = louversBlas.nodes[n];
    const lof = (nd.count === 0) ? (TLAS_RESERVED_NODES + fileBlasNodes + nd.leftOrFirst) : nd.leftOrFirst;
    const dst = (TLAS_RESERVED_NODES + fileBlasNodes + n) * 2;
    nodeTexData[dst * 4 + 0] = nd.min[0]; nodeTexData[dst * 4 + 1] = nd.min[1]; nodeTexData[dst * 4 + 2] = nd.min[2]; nodeTexData[dst * 4 + 3] = lof;
    nodeTexData[(dst + 1) * 4 + 0] = nd.max[0]; nodeTexData[(dst + 1) * 4 + 1] = nd.max[1]; nodeTexData[(dst + 1) * 4 + 2] = nd.max[2]; nodeTexData[(dst + 1) * 4 + 3] = nd.count;
    if (n === 0) {
      meshLocalBounds[fileMeshCount].min = nd.min;
      meshLocalBounds[fileMeshCount].max = nd.max;
    }
  }

  for (let n = 0; n < emissiveBlas.nodes.length; n++) {
    const nd = emissiveBlas.nodes[n];
    const lof = (nd.count === 0) ? (TLAS_RESERVED_NODES + emissiveNodeOffset + nd.leftOrFirst) : nd.leftOrFirst;
    const dst = (TLAS_RESERVED_NODES + emissiveNodeOffset + n) * 2;
    nodeTexData[dst * 4 + 0] = nd.min[0]; nodeTexData[dst * 4 + 1] = nd.min[1]; nodeTexData[dst * 4 + 2] = nd.min[2]; nodeTexData[dst * 4 + 3] = lof;
    nodeTexData[(dst + 1) * 4 + 0] = nd.max[0]; nodeTexData[(dst + 1) * 4 + 1] = nd.max[1]; nodeTexData[(dst + 1) * 4 + 2] = nd.max[2]; nodeTexData[(dst + 1) * 4 + 3] = nd.count;
    if (n === 0) {
      meshLocalBounds[fileMeshCount + 1].min = nd.min;
      meshLocalBounds[fileMeshCount + 1].max = nd.max;
    }
  }

  const triTexWidth = 2048;
  const triTexHeight = Math.ceil((totalTris * 4) / triTexWidth);
  const triTexData = new Float32Array(triTexWidth * triTexHeight * 4);
  new Uint8Array(triTexData.buffer, 0, fileTris * 64).set(new Uint8Array(arrayBuf, triFileByteStart, fileTris * 64));

  const appendBlasTris = (reordered, startTri) => {
    for (let t = 0; t < reordered.length; t++) {
      const tr = reordered[t];
      const fBase = (startTri + t) * 16;
      const oct = packOctahedralJs(tr.n[0], tr.n[1], tr.n[2]);
      triTexData[fBase + 0] = tr.p0[0]; triTexData[fBase + 1] = tr.p0[1]; triTexData[fBase + 2] = tr.p0[2]; triTexData[fBase + 3] = tr.matId;
      triTexData[fBase + 4] = tr.p1[0] - tr.p0[0]; triTexData[fBase + 5] = tr.p1[1] - tr.p0[1]; triTexData[fBase + 6] = tr.p1[2] - tr.p0[2]; triTexData[fBase + 7] = oct;
      triTexData[fBase + 8] = tr.p2[0] - tr.p0[0]; triTexData[fBase + 9] = tr.p2[1] - tr.p0[1]; triTexData[fBase + 10] = tr.p2[2] - tr.p0[2]; triTexData[fBase + 11] = oct;
      triTexData[fBase + 15] = oct;
    }
  };
  appendBlasTris(louversBlas.reorderedTris, fileTris);
  appendBlasTris(emissiveBlas.reorderedTris, emissiveTriOffset);

  const meshVaos = [];
  for (let m = 0; m < meshMeta.length; m++) {
    const { triOffset, triCount } = meshMeta[m];
    const vboFloats = new Float32Array(triCount * 3 * 7);
    let ptr = 0;
    for (let t = 0; t < triCount; t++) {
      const fBase = (triOffset + t) * 16;
      const p0x = triTexData[fBase + 0], p0y = triTexData[fBase + 1], p0z = triTexData[fBase + 2];
      const matId = Math.round(triTexData[fBase + 3]);
      const e1x = triTexData[fBase + 4], e1y = triTexData[fBase + 5], e1z = triTexData[fBase + 6];
      const n0 = unpackOctahedralJs(triTexData[fBase + 7]);
      const e2x = triTexData[fBase + 8], e2y = triTexData[fBase + 9], e2z = triTexData[fBase + 10];
      const n1 = unpackOctahedralJs(triTexData[fBase + 11]);
      const n2 = unpackOctahedralJs(triTexData[fBase + 15]);
      const verts = [
        [p0x, p0y, p0z, n0[0], n0[1], n0[2], matId],
        [p0x + e1x, p0y + e1y, p0z + e1z, n1[0], n1[1], n1[2], matId],
        [p0x + e2x, p0y + e2y, p0z + e2z, n2[0], n2[1], n2[2], matId],
      ];
      for (const v of verts) for (let k = 0; k < 7; k++) vboFloats[ptr++] = v[k];
    }
    const vao = gl.createVertexArray();
    const vbo = gl.createBuffer();
    gl.bindVertexArray(vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.bufferData(gl.ARRAY_BUFFER, vboFloats, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 28, 0);
    gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 28, 12);
    gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 1, gl.FLOAT, false, 28, 24);
    gl.bindVertexArray(null);
    meshVaos.push({ vao, vertexCount: triCount * 3 });
  }

  // Upload Pre-Baked 3D SDF Textures + Arena Radiosity Cascade Buffers (0 per-frame geometry re-bakes!)
  function createR16fSdf3D(nx, ny, nz, uint16Arr) {
    const tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_3D, tex);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    gl.texImage3D(gl.TEXTURE_3D, 0, gl.R16F, nx, ny, nz, 0, gl.RED, gl.HALF_FLOAT, uint16Arr);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_R, gl.CLAMP_TO_EDGE);
    return tex;
  }

  function makeEmpty3D(nx, ny, nz, internalFmt, fmt) {
    const tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_3D, tex);
    gl.texImage3D(gl.TEXTURE_3D, 0, internalFmt, nx, ny, nz, 0, fmt, gl.FLOAT, null);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_R, gl.CLAMP_TO_EDGE);
    return tex;
  }

  function createArenaCascadeVolumes(vol, needDynamicCopy) {
    const count = vol.nx * vol.ny * vol.nz;
    const rawRg = new Float32Array(count * 2);
    const dist16 = new Uint16Array(count);
    const view = new DataView(vol.payload.buffer, vol.payload.byteOffset, vol.payload.byteLength);
    for (let i = 0; i < count; i++) {
      const hDist = view.getUint16(i * 4, true);
      dist16[i] = hDist;
      rawRg[i * 2 + 0] = decodeFloat16(hDist);
      rawRg[i * 2 + 1] = view.getUint16(i * 4 + 2, true);
    }

    const rawTex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_3D, rawTex);
    gl.texImage3D(gl.TEXTURE_3D, 0, gl.RG16F, vol.nx, vol.ny, vol.nz, 0, gl.RG, gl.FLOAT, rawRg);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_R, gl.CLAMP_TO_EDGE);

    const distTex = createR16fSdf3D(vol.nx, vol.ny, vol.nz, dist16);
    const res = {
      rawTex, distTex,
      directTex: makeEmpty3D(vol.nx, vol.ny, vol.nz, gl.RGBA16F, gl.RGBA),
      tmpTex:    makeEmpty3D(vol.nx, vol.ny, vol.nz, gl.RGBA16F, gl.RGBA),
      texA:      makeEmpty3D(vol.nx, vol.ny, vol.nz, gl.RGBA16F, gl.RGBA),
      reflA:     makeEmpty3D(vol.nx, vol.ny, vol.nz, gl.R16F, gl.RED),
    };
    if (needDynamicCopy) {
      res.texB  = makeEmpty3D(vol.nx, vol.ny, vol.nz, gl.RGBA16F, gl.RGBA);
      res.reflB = makeEmpty3D(vol.nx, vol.ny, vol.nz, gl.R16F, gl.RED);
    }
    return res;
  }

  const bodySdfTex = createR16fSdf3D(sdfVolumes[0].nx, sdfVolumes[0].ny, sdfVolumes[0].nz,
    new Uint16Array(sdfVolumes[0].payload.buffer, sdfVolumes[0].payload.byteOffset, sdfVolumes[0].nx * sdfVolumes[0].ny * sdfVolumes[0].nz));
  const rimSdfTex = createR16fSdf3D(sdfVolumes[1].nx, sdfVolumes[1].ny, sdfVolumes[1].nz,
    new Uint16Array(sdfVolumes[1].payload.buffer, sdfVolumes[1].payload.byteOffset, sdfVolumes[1].nx * sdfVolumes[1].ny * sdfVolumes[1].nz));
  const tyreSdfTex = createR16fSdf3D(sdfVolumes[2].nx, sdfVolumes[2].ny, sdfVolumes[2].nz,
    new Uint16Array(sdfVolumes[2].payload.buffer, sdfVolumes[2].payload.byteOffset, sdfVolumes[2].nx * sdfVolumes[2].ny * sdfVolumes[2].nz));
  const louverSdfTex = createR16fSdf3D(louversSdfVol.nx, louversSdfVol.ny, louversSdfVol.nz, louversSdfVol.payload16);

  const clip0Vol = sdfVolumes[3];
  const clip1Vol = sdfVolumes[4];
  const clip0Pair = createArenaCascadeVolumes(clip0Vol, true);
  const clip1Pair = createArenaCascadeVolumes(clip1Vol, false);

  const clip0Span = [clip0Vol.max[0]-clip0Vol.min[0], clip0Vol.max[1]-clip0Vol.min[1], clip0Vol.max[2]-clip0Vol.min[2]];
  const clip1Span = [clip1Vol.max[0]-clip1Vol.min[0], clip1Vol.max[1]-clip1Vol.min[1], clip1Vol.max[2]-clip1Vol.min[2]];
  const clip0Voxel = clip0Span[2] / clip0Vol.nz;
  const clip1Voxel = clip1Span[2] / clip1Vol.nz;

  const bvhNodesTex = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, bvhNodesTex);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, nodeTexWidth, nodeTexHeight, 0, gl.RGBA, gl.FLOAT, nodeTexData);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);

  const bvhTrisTex = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, bvhTrisTex);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, triTexWidth, triTexHeight, 0, gl.RGBA, gl.FLOAT, triTexData);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);

  const instancesData = new Float32Array(128 * 4);
  const instancesTex = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, instancesTex);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, 128, 1, 0, gl.RGBA, gl.FLOAT, instancesData);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);

  // Compile All Programs
  const progGBuffer       = compileProgram(gl, GBUFFER_VERT, GBUFFER_FRAG, "GBuffer");
  const progLightDepth    = compileProgram(gl, LIGHT_DEPTH_VERT, LIGHT_DEPTH_FRAG, "LightDepth");
  const progPlaneFilter   = compileProgram(gl, FULLSCREEN_VERT, PLANE_FILTER_FRAG, "PlaneFilter");
  const progStaticShade   = compileProgram(gl, FULLSCREEN_VERT, SDF_STATIC_SHADE_FRAG, "SdfStaticShade");
  const progSdfBounce     = compileProgram(gl, FULLSCREEN_VERT, SDF_RADIOSITY_BOUNCE_FRAG, "SdfRadiosityBounce");
  const progSdfUnion      = compileProgram(gl, FULLSCREEN_VERT, SDF_UNION_FRAG, "SdfUnion");
  const progGiSdf         = compileProgram(gl, FULLSCREEN_VERT, GI_SDF_FRAG, "GiSdf");
  const progGiBvh         = compileProgram(gl, FULLSCREEN_VERT, GI_BVH_FRAG, "GiBvh");
  const progGiBilateral   = compileProgram(gl, FULLSCREEN_VERT, GI_BILATERAL_FRAG, "GiBilateral");
  const progReflSdf       = compileProgram(gl, FULLSCREEN_VERT, REFLECTION_SDF_FRAG, "ReflSdf");
  const progReflBvh       = compileProgram(gl, FULLSCREEN_VERT, REFLECTION_BVH_FRAG, "ReflBvh");
  const progEmissiveMapSh = compileProgram(gl, FULLSCREEN_VERT, EMISSIVE_MAP_SHADOW_FRAG, "EmissiveMapShadow");
  const progEmissiveBvhSh = compileProgram(gl, FULLSCREEN_VERT, EMISSIVE_BVH_SHADOW_FRAG, "EmissiveBvhShadow");
  const progComposite     = compileProgram(gl, FULLSCREEN_VERT, COMPOSITE_FRAG, "Composite");

  const shadowProgs = {
    pcf:          compileProgram(gl, FULLSCREEN_VERT, SHADOW_PCF_FRAG, "ShadowPcf"),
    pcss:         compileProgram(gl, FULLSCREEN_VERT, SHADOW_PCSS_FRAG, "ShadowPcss"),
    contact_pcss: compileProgram(gl, FULLSCREEN_VERT, SHADOW_CONTACT_PCSS_FRAG, "ShadowContactPcss"),
    smrt:         compileProgram(gl, FULLSCREEN_VERT, SHADOW_SMRT_FRAG, "ShadowSmrt"),
    sdf:          compileProgram(gl, FULLSCREEN_VERT, SHADOW_SDF_FRAG, "ShadowSdf"),
    hybrid:       compileProgram(gl, FULLSCREEN_VERT, SHADOW_HYBRID_FRAG, "ShadowHybrid"),
    bvh:          compileProgram(gl, FULLSCREEN_VERT, SHADOW_BVH_FRAG, "ShadowBvh"),
  };

  const emptyVao = gl.createVertexArray();
  const sdfSliceFbo = gl.createFramebuffer();

  // Render Targets
  let screenW = 0, screenH = 0, shadowTraceW = 0, shadowTraceH = 0, giTraceW = 0, giTraceH = 0;
  let keyShadowRes = 2048;
  const secShadowRes = 1024, contactRes = 1024;

  let gBufferFbo = null, gPosTex = null, gNormTex = null, gDirectTex = null, gDepthRb = null;
  const shadowMaps = [null, null, null];
  let contactMap = null;
  let emissiveDepthMap = null;

  let shadowRawFbo = null, shadowRawTex = null;
  let shadowPingAFbo = null, shadowPingATex = null;
  let shadowPingBFbo = null, shadowPingBTex = null;
  let shadowFinalAFbo = null, shadowFinalATex = null;
  let shadowFinalBFbo = null, shadowFinalBTex = null;
  let emissiveFinalAFbo = null, emissiveFinalATex = null;
  let emissiveFinalBFbo = null, emissiveFinalBTex = null;

  let giRawFbo = null, giRawTex = null;
  let giPingAFbo = null, giPingATex = null;
  let giPingBFbo = null, giPingBTex = null;
  let giSdfFinalFbo = null, giSdfFinalTex = null;
  let giBvhFinalFbo = null, giBvhFinalTex = null;
  let reflSdfFbo = null, reflSdfTex = null;
  let reflBvhFbo = null, reflBvhTex = null;

  function makeTex2D(w, h, internalFmt, fmt, type, filter) {
    const t = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, t);
    gl.texImage2D(gl.TEXTURE_2D, 0, internalFmt, w, h, 0, fmt, type, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    return t;
  }

  function makeSingleFbo(tex) {
    const fbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
    gl.drawBuffers([gl.COLOR_ATTACHMENT0]);
    return fbo;
  }

  function makeDepthRasterFbo(res) {
    const tex = makeTex2D(res, res, gl.R32F, gl.RED, gl.FLOAT, gl.NEAREST);
    const rb = gl.createRenderbuffer();
    gl.bindRenderbuffer(gl.RENDERBUFFER, rb);
    gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT24, res, res);
    const fbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
    gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, rb);
    gl.drawBuffers([gl.COLOR_ATTACHMENT0]);
    return { fbo, tex, rb, res };
  }

  function rebuildShadowMaps(newKeyRes) {
    keyShadowRes = newKeyRes;
    if (shadowMaps[0]) {
      gl.deleteFramebuffer(shadowMaps[0].fbo); gl.deleteTexture(shadowMaps[0].tex); gl.deleteRenderbuffer(shadowMaps[0].rb);
    }
    shadowMaps[0] = makeDepthRasterFbo(keyShadowRes);
    if (!shadowMaps[1]) shadowMaps[1] = makeDepthRasterFbo(secShadowRes);
    if (!shadowMaps[2]) shadowMaps[2] = makeDepthRasterFbo(secShadowRes);
    if (!contactMap) contactMap = makeDepthRasterFbo(contactRes);
    if (!emissiveDepthMap) emissiveDepthMap = makeDepthRasterFbo(EMISSIVE_RIG.res);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }

  const state = {
    algoA: "contact_pcss",
    algoB: "bvh",
    giA: "sdf",
    giB: "bvh",
    reflA: "sdf",
    reflB: "bvh",

    viewLayout: "split",
    surfaceMode: "pbr",
    splitX: 0.5,

    lightCount: 2,
    lightType: "sun",
    lightSweep: false,
    sunRadiusDeg: 2.40,
    areaRadiusM: 0.42,
    lightAzimuthDeg: 46,
    lightElevationDeg: 34,
    lightDistanceM: 7.5,
    lightIntensity: 1.25,
    fillBoost: 1.15,
    giIntensity: 1.25,
    reflBounces: 2,
    lightKelvin: "day",

    qualityIdx: 1,
    planeFilter: true,
    sdfMode: "local",
    halfRes: false,
    showLouvers: true,
    emissiveOn: true,
    emissiveShadowMode: "shadowed",
    emissiveIntensity: 1.35,

    driveMode: "auto",
    camPreset: "hero",
    paintIdx: 0,
    hudHidden: false,

    carX: -1.10,
    carY: 0.35,
    carYaw: 0.28,
    carSpeed: 0.0,
    steerAngle: 0.14,
    wheelSpin: 0.0,
    autoTime: 0.0,

    camYaw: -1.92,
    camPitch: 0.28,
    camDist: 7.60,

    timings: {
      depthPrep: 0, union: 0,
      shA: 0, shB: 0,
      giSdf: 0, giBvh: 0,
      reflSdf: 0, reflBvh: 0,
    },
  };

  function resizeTargets(force = false) {
    const dpr = Math.min(window.devicePixelRatio || 1, 1.35);
    const w = Math.max(640, Math.floor(window.innerWidth * dpr));
    const h = Math.max(360, Math.floor(window.innerHeight * dpr));
    const shScale = state.halfRes ? 0.5 : 1.0;
    const sw = Math.max(320, Math.floor(w * shScale));
    const sh = Math.max(180, Math.floor(h * shScale));
    const gw = Math.max(320, Math.floor(w * 0.5));
    const gh = Math.max(180, Math.floor(h * 0.5));
    if (!force && w === screenW && h === screenH && sw === shadowTraceW && sh === shadowTraceH) return;

    screenW = w; screenH = h;
    shadowTraceW = sw; shadowTraceH = sh;
    giTraceW = gw; giTraceH = gh;
    canvas.width = w; canvas.height = h;

    if (gBufferFbo) {
      [gBufferFbo, shadowRawFbo, shadowPingAFbo, shadowPingBFbo, shadowFinalAFbo, shadowFinalBFbo,
       emissiveFinalAFbo, emissiveFinalBFbo,
       giRawFbo, giPingAFbo, giPingBFbo, giSdfFinalFbo, giBvhFinalFbo, reflSdfFbo, reflBvhFbo].forEach((f) => gl.deleteFramebuffer(f));
      [gPosTex, gNormTex, gDirectTex, shadowRawTex, shadowPingATex, shadowPingBTex, shadowFinalATex, shadowFinalBTex,
       emissiveFinalATex, emissiveFinalBTex,
       giRawTex, giPingATex, giPingBTex, giSdfFinalTex, giBvhFinalTex, reflSdfTex, reflBvhTex].forEach((t) => gl.deleteTexture(t));
      gl.deleteRenderbuffer(gDepthRb);
    }

    gPosTex    = makeTex2D(screenW, screenH, gl.RGBA16F, gl.RGBA, gl.FLOAT, gl.NEAREST);
    gNormTex   = makeTex2D(screenW, screenH, gl.RGBA16F, gl.RGBA, gl.FLOAT, gl.NEAREST);
    gDirectTex = makeTex2D(screenW, screenH, gl.RGBA16F, gl.RGBA, gl.FLOAT, gl.LINEAR);
    gDepthRb   = gl.createRenderbuffer();
    gl.bindRenderbuffer(gl.RENDERBUFFER, gDepthRb);
    gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT24, screenW, screenH);
    gBufferFbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, gBufferFbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, gPosTex, 0);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT1, gl.TEXTURE_2D, gNormTex, 0);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT2, gl.TEXTURE_2D, gDirectTex, 0);
    gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, gDepthRb);
    gl.drawBuffers([gl.COLOR_ATTACHMENT0, gl.COLOR_ATTACHMENT1, gl.COLOR_ATTACHMENT2]);

    shadowRawTex    = makeTex2D(shadowTraceW, shadowTraceH, gl.RGBA16F, gl.RGBA, gl.FLOAT, gl.NEAREST);
    shadowRawFbo    = makeSingleFbo(shadowRawTex);
    shadowPingATex  = makeTex2D(shadowTraceW, shadowTraceH, gl.RGBA16F, gl.RGBA, gl.FLOAT, gl.NEAREST);
    shadowPingAFbo  = makeSingleFbo(shadowPingATex);
    shadowPingBTex  = makeTex2D(shadowTraceW, shadowTraceH, gl.RGBA16F, gl.RGBA, gl.FLOAT, gl.NEAREST);
    shadowPingBFbo  = makeSingleFbo(shadowPingBTex);
    shadowFinalATex = makeTex2D(shadowTraceW, shadowTraceH, gl.RGBA16F, gl.RGBA, gl.FLOAT, gl.LINEAR);
    shadowFinalAFbo = makeSingleFbo(shadowFinalATex);
    shadowFinalBTex = makeTex2D(shadowTraceW, shadowTraceH, gl.RGBA16F, gl.RGBA, gl.FLOAT, gl.LINEAR);
    shadowFinalBFbo = makeSingleFbo(shadowFinalBTex);
    emissiveFinalATex = makeTex2D(shadowTraceW, shadowTraceH, gl.RGBA16F, gl.RGBA, gl.FLOAT, gl.LINEAR);
    emissiveFinalAFbo = makeSingleFbo(emissiveFinalATex);
    emissiveFinalBTex = makeTex2D(shadowTraceW, shadowTraceH, gl.RGBA16F, gl.RGBA, gl.FLOAT, gl.LINEAR);
    emissiveFinalBFbo = makeSingleFbo(emissiveFinalBTex);

    giRawTex      = makeTex2D(giTraceW, giTraceH, gl.RGBA16F, gl.RGBA, gl.FLOAT, gl.NEAREST);
    giRawFbo      = makeSingleFbo(giRawTex);
    giPingATex    = makeTex2D(giTraceW, giTraceH, gl.RGBA16F, gl.RGBA, gl.FLOAT, gl.NEAREST);
    giPingAFbo    = makeSingleFbo(giPingATex);
    giPingBTex    = makeTex2D(giTraceW, giTraceH, gl.RGBA16F, gl.RGBA, gl.FLOAT, gl.NEAREST);
    giPingBFbo    = makeSingleFbo(giPingBTex);
    giSdfFinalTex = makeTex2D(giTraceW, giTraceH, gl.RGBA16F, gl.RGBA, gl.FLOAT, gl.LINEAR);
    giSdfFinalFbo = makeSingleFbo(giSdfFinalTex);
    giBvhFinalTex = makeTex2D(giTraceW, giTraceH, gl.RGBA16F, gl.RGBA, gl.FLOAT, gl.LINEAR);
    giBvhFinalFbo = makeSingleFbo(giBvhFinalTex);

    reflSdfTex = makeTex2D(giTraceW, giTraceH, gl.RGBA16F, gl.RGBA, gl.FLOAT, gl.LINEAR);
    reflSdfFbo = makeSingleFbo(reflSdfTex);
    reflBvhTex = makeTex2D(giTraceW, giTraceH, gl.RGBA16F, gl.RGBA, gl.FLOAT, gl.LINEAR);
    reflBvhFbo = makeSingleFbo(reflBvhTex);

    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }

  rebuildShadowMaps(2048);
  resizeTargets(true);
  window.addEventListener("resize", () => resizeTargets(false));

  //======================================================================================================================
  // INPUT & VEHICLE CRUISE / WASD
  //======================================================================================================================
  const keys = new Set();
  window.addEventListener("keydown", (e) => {
    const k = e.key.toLowerCase();
    keys.add(k);
    if (["w", "a", "s", "d", "arrowup", "arrowdown", "arrowleft", "arrowright"].includes(k)) {
      if (state.driveMode !== "manual") { state.driveMode = "manual"; syncDriveButtons(); }
    }
    if (k === "h") toggleHud();
    if (k === "r") { state.carX = -1.10; state.carY = 0.35; state.carYaw = 0.28; state.carSpeed = 0; state.steerAngle = 0; }
  });
  window.addEventListener("keyup", (e) => keys.delete(e.key.toLowerCase()));

  let draggingCam = false, draggingSplit = false, lastMx = 0, lastMy = 0;
  canvas.addEventListener("pointerdown", (e) => {
    draggingCam = true; lastMx = e.clientX; lastMy = e.clientY;
    canvas.setPointerCapture(e.pointerId);
  });
  window.addEventListener("pointermove", (e) => {
    if (draggingSplit) {
      state.splitX = Math.min(0.92, Math.max(0.08, e.clientX / window.innerWidth));
      document.getElementById("split-divider").style.left = (state.splitX * 100).toFixed(1) + "%";
      return;
    }
    if (!draggingCam) return;
    const dx = e.clientX - lastMx, dy = e.clientY - lastMy;
    lastMx = e.clientX; lastMy = e.clientY;
    state.camYaw -= dx * 0.0065;
    state.camPitch = Math.min(1.25, Math.max(0.06, state.camPitch + dy * 0.0055));
  });
  window.addEventListener("pointerup", () => { draggingCam = false; draggingSplit = false; });
  canvas.addEventListener("wheel", (e) => {
    e.preventDefault();
    state.camDist = Math.min(40.0, Math.max(2.6, state.camDist + e.deltaY * 0.008));
  }, { passive: false });
  document.getElementById("split-divider").addEventListener("pointerdown", (e) => {
    e.stopPropagation(); draggingSplit = true;
  });

  const WHEEL_MOUNTS = [
    { x:  1.665, y:  1.045, front: true,  right: false },
    { x:  1.665, y: -1.045, front: true,  right: true  },
    { x: -1.705, y:  1.045, front: false, right: false },
    { x: -1.705, y: -1.045, front: false, right: true  },
  ];
  const TYRE_RADIUS = 0.50855;

  let activeInstances = [];
  function updateVehicleAndTlas(dt) {
    if (state.driveMode === "pose") {
      state.carX = -1.65; state.carY = 0.45; state.carYaw = 0.25; state.steerAngle = 0.22;
    } else if (state.driveMode === "auto") {
      state.autoTime += dt;
      const t = state.autoTime * 0.52;
      const nextX = -1.8 + Math.sin(t) * 3.4;
      const nextY = 0.25 + Math.sin(t * 2.0) * 1.15;
      const vx = nextX - state.carX, vy = nextY - state.carY;
      const dist = Math.hypot(vx, vy);
      if (dist > 1e-5) state.carYaw = Math.atan2(vy, vx);
      state.steerAngle = Math.cos(t * 2.0) * 0.24;
      state.carX = nextX; state.carY = nextY;
      state.wheelSpin += dist / TYRE_RADIUS;
    } else {
      const throttle = (keys.has("w") || keys.has("arrowup") ? 1 : 0) - (keys.has("s") || keys.has("arrowdown") ? 1 : 0);
      const steerIn  = (keys.has("a") || keys.has("arrowleft") ? 1 : 0) - (keys.has("d") || keys.has("arrowright") ? 1 : 0);
      state.steerAngle += (steerIn * 0.48 - state.steerAngle) * Math.min(1, dt * 8.5);
      if (throttle !== 0) state.carSpeed += throttle * 8.5 * dt; else state.carSpeed *= Math.pow(0.24, dt);
      state.carSpeed = Math.max(-7.5, Math.min(12.0, state.carSpeed));
      state.carYaw += (state.carSpeed / 3.37) * Math.tan(state.steerAngle) * dt;
      state.carX = Math.max(-18.5, Math.min(17.5, state.carX + Math.cos(state.carYaw) * state.carSpeed * dt));
      state.carY = Math.max(-12.0, Math.min(11.0, state.carY + Math.sin(state.carYaw) * state.carSpeed * dt));
      state.wheelSpin += (state.carSpeed * dt) / TYRE_RADIUS;
    }

    const cy = Math.cos(state.carYaw), sy = Math.sin(state.carYaw);
    const bodyModel = mat4ComposeRigid(state.carX, state.carY, TYRE_RADIUS, state.carYaw, 0, 0);
    const bodyInv   = mat4InvertRigid(bodyModel);

    const wheelModels = [], wheelInvs = [];
    for (const wm of WHEEL_MOUNTS) {
      const wx = state.carX + wm.x * cy - wm.y * sy;
      const wy = state.carY + wm.x * sy + wm.y * cy;
      const wYaw = state.carYaw + (wm.front ? state.steerAngle : 0) + (wm.right ? Math.PI : 0);
      const spin = wm.right ? -state.wheelSpin : state.wheelSpin;
      const m = mat4Multiply(mat4ComposeRigid(wx, wy, TYRE_RADIUS, wYaw, 0, 0), mat4ComposeRigid(0, 0, 0, 0, spin, 0));
      wheelModels.push(m); wheelInvs.push(mat4InvertRigid(m));
    }

    const idM = mat4Identity();
    activeInstances = [
      { meshIdx: 0, l2w: bodyModel, w2l: bodyInv, matOverride: -1 },
      { meshIdx: 1, l2w: wheelModels[0], w2l: wheelInvs[0], matOverride: -1 },
      { meshIdx: 1, l2w: wheelModels[1], w2l: wheelInvs[1], matOverride: -1 },
      { meshIdx: 1, l2w: wheelModels[2], w2l: wheelInvs[2], matOverride: -1 },
      { meshIdx: 1, l2w: wheelModels[3], w2l: wheelInvs[3], matOverride: -1 },
      { meshIdx: 2, l2w: wheelModels[0], w2l: wheelInvs[0], matOverride: 11 },
      { meshIdx: 2, l2w: wheelModels[1], w2l: wheelInvs[1], matOverride: 11 },
      { meshIdx: 2, l2w: wheelModels[2], w2l: wheelInvs[2], matOverride: 11 },
      { meshIdx: 2, l2w: wheelModels[3], w2l: wheelInvs[3], matOverride: 11 },
      { meshIdx: 3, l2w: idM, w2l: idM, matOverride: -1 },
    ];
    if (state.showLouvers) activeInstances.push({ meshIdx: 4, l2w: idM, w2l: idM, matOverride: 21 });
    if (state.emissiveOn) activeInstances.push({ meshIdx: 5, l2w: idM, w2l: idM, matOverride: -1 });

    for (let i = 0; i < activeInstances.length; i++) {
      const base = i * 9 * 4;
      instancesData.set(activeInstances[i].l2w, base);
      instancesData.set(activeInstances[i].w2l, base + 16);
      instancesData[base + 32] = activeInstances[i].matOverride + 1;
    }
    gl.bindTexture(gl.TEXTURE_2D, instancesTex);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, 128, 1, gl.RGBA, gl.FLOAT, instancesData);

    const leaves = activeInstances.map((inst, idx) => {
      const lb = meshLocalBounds[inst.meshIdx];
      const wmin = [1e30, 1e30, 1e30], wmax = [-1e30, -1e30, -1e30];
      const m = inst.l2w;
      for (let c = 0; c < 8; c++) {
        const lx = (c & 1) ? lb.max[0] : lb.min[0];
        const ly = (c & 2) ? lb.max[1] : lb.min[1];
        const lz = (c & 4) ? lb.max[2] : lb.min[2];
        const wx = m[0]*lx + m[4]*ly + m[8]*lz + m[12];
        const wy = m[1]*lx + m[5]*ly + m[9]*lz + m[13];
        const wz = m[2]*lx + m[6]*ly + m[10]*lz + m[14];
        if (wx < wmin[0]) wmin[0] = wx; if (wy < wmin[1]) wmin[1] = wy; if (wz < wmin[2]) wmin[2] = wz;
        if (wx > wmax[0]) wmax[0] = wx; if (wy > wmax[1]) wmax[1] = wy; if (wz > wmax[2]) wmax[2] = wz;
      }
      return { min: wmin, max: wmax, leftOrFirst: TLAS_RESERVED_NODES + meshMeta[inst.meshIdx].nodeOffset, count: -(idx + 1) };
    });

    let nextTlasSlot = 1;
    function writeTlasNode(slot, min, max, lof, cnt) {
      const t = slot * 2;
      nodeTexData[t * 4 + 0] = min[0]; nodeTexData[t * 4 + 1] = min[1]; nodeTexData[t * 4 + 2] = min[2]; nodeTexData[t * 4 + 3] = lof;
      nodeTexData[(t + 1) * 4 + 0] = max[0]; nodeTexData[(t + 1) * 4 + 1] = max[1]; nodeTexData[(t + 1) * 4 + 2] = max[2]; nodeTexData[(t + 1) * 4 + 3] = cnt;
    }
    function buildTlasSlot(slot, list) {
      if (list.length === 1) { writeTlasNode(slot, list[0].min, list[0].max, list[0].leftOrFirst, list[0].count); return; }
      const bmin = [1e30, 1e30, 1e30], bmax = [-1e30, -1e30, -1e30];
      for (const it of list) for (let k = 0; k < 3; k++) {
        if (it.min[k] < bmin[k]) bmin[k] = it.min[k];
        if (it.max[k] > bmax[k]) bmax[k] = it.max[k];
      }
      const spans = [bmax[0]-bmin[0], bmax[1]-bmin[1], bmax[2]-bmin[2]];
      const axis = spans[0] >= spans[1] && spans[0] >= spans[2] ? 0 : (spans[1] >= spans[2] ? 1 : 2);
      list.sort((a, b) => (a.min[axis] + a.max[axis]) - (b.min[axis] + b.max[axis]));
      const mid = list.length >> 1;
      const leftSlot = nextTlasSlot; nextTlasSlot += 2;
      writeTlasNode(slot, bmin, bmax, leftSlot, 0);
      buildTlasSlot(leftSlot, list.slice(0, mid));
      buildTlasSlot(leftSlot + 1, list.slice(mid));
    }
    buildTlasSlot(0, leaves);

    gl.bindTexture(gl.TEXTURE_2D, bvhNodesTex);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, TLAS_RESERVED_NODES * 2, 1, gl.RGBA, gl.FLOAT,
      nodeTexData.subarray(0, TLAS_RESERVED_NODES * 2 * 4));
    return { bodyInv, wheelInvs, bodyModel };
  }

  //======================================================================================================================
  // MULTI-LIGHT RIG BUILDER (Up to 3 Real Shadow-Casting Lights + Tight Contact Clip on Light 0)
  //======================================================================================================================
  const lightU = {
    viewProj: new Float32Array(48),
    type: new Int32Array(3),
    origin: new Float32Array(9),
    forward: new Float32Array(9),
    dir: new Float32Array(9),
    color: new Float32Array(9),
    intensity: new Float32Array(3),
    radius: new Float32Array(3),
    res: new Float32Array(3),
    orthoHalf: new Float32Array(3),
    perspTan: new Float32Array(3),
  };

  function makePerspectiveSpotRig(origin, target, fovDeg, radiusM, color, intensity, res) {
    const upRef = [0, 0, 1];
    let fx = target[0] - origin[0], fy = target[1] - origin[1], fz = target[2] - origin[2];
    const fl = Math.hypot(fx, fy, fz) || 1;
    fx /= fl; fy /= fl; fz /= fl;
    const fovRad = (fovDeg * Math.PI) / 180.0;
    const view = mat4LookAtRH(origin, target, upRef);
    const proj = mat4PerspectiveRH(fovRad, 1.0, 0.4, 55.0);
    return {
      typeInt: 1,
      lightOrigin: origin,
      lightForward: [fx, fy, fz],
      lightDir: [-fx, -fy, -fz],
      lightViewProj: mat4Multiply(proj, view),
      lightRadius: radiusM,
      color,
      intensity,
      res,
      orthoHalfSpan: 0.0,
      perspTanHalfFov: Math.tan(fovRad * 0.5),
    };
  }

  function computeMultiLightRig() {
    const az = (state.lightAzimuthDeg * Math.PI) / 180.0;
    const el = (state.lightElevationDeg * Math.PI) / 180.0;
    const dir = [Math.cos(el) * Math.cos(az), Math.cos(el) * Math.sin(az), Math.sin(el)];
    const l = Math.hypot(dir[0], dir[1], dir[2]) || 1;
    dir[0] /= l; dir[1] /= l; dir[2] /= l;
    const upRef = Math.abs(dir[2]) < 0.96 ? [0, 0, 1] : [1, 0, 0];

    let keyLight;
    let contactRig;
    if (state.lightType === "sun") {
      const halfSpan = 16.0;
      const texelWorld = (2.0 * halfSpan) / keyShadowRes;
      let rx = upRef[1]*dir[2] - upRef[2]*dir[1];
      let ry = upRef[2]*dir[0] - upRef[0]*dir[2];
      let rz = upRef[0]*dir[1] - upRef[1]*dir[0];
      const rl = Math.hypot(rx, ry, rz) || 1;
      rx /= rl; ry /= rl; rz /= rl;
      const ux = dir[1]*rz - dir[2]*ry, uy = dir[2]*rx - dir[0]*rz, uz = dir[0]*ry - dir[1]*rx;

      const cx = state.carX * 0.4, cy = state.carY * 0.4, cz = 0.5;
      const projU = Math.round((cx*rx + cy*ry + cz*rz) / texelWorld) * texelWorld;
      const projV = Math.round((cx*ux + cy*uy + cz*uz) / texelWorld) * texelWorld;
      const projN = cx*dir[0] + cy*dir[1] + cz*dir[2];
      const snappedTarget = [rx*projU + ux*projV + dir[0]*projN, ry*projU + uy*projV + dir[1]*projN, rz*projU + uz*projV + dir[2]*projN];
      const lightOrigin = [snappedTarget[0] + dir[0] * 42.0, snappedTarget[1] + dir[1] * 42.0, snappedTarget[2] + dir[2] * 42.0];
      const lightForward = [-dir[0], -dir[1], -dir[2]];
      const view = mat4LookAtRH(lightOrigin, snappedTarget, upRef);
      const proj = mat4OrthoRH(-halfSpan, halfSpan, -halfSpan, halfSpan, 1.0, 92.0);
      keyLight = {
        typeInt: 0, lightOrigin, lightForward, lightDir: dir,
        lightViewProj: mat4Multiply(proj, view),
        lightRadius: Math.tan((state.sunRadiusDeg * Math.PI) / 180.0),
        color: KELVIN_COLORS[state.lightKelvin], intensity: state.lightIntensity,
        res: keyShadowRes, orthoHalfSpan: halfSpan, perspTanHalfFov: 0.0,
      };

      const cHalf = 3.2;
      const cTex = (2.0 * cHalf) / contactRes;
      const cU = Math.round((state.carX*rx + state.carY*ry + 0.5*rz) / cTex) * cTex;
      const cV = Math.round((state.carX*ux + state.carY*uy + 0.5*uz) / cTex) * cTex;
      const cN = state.carX*dir[0] + state.carY*dir[1] + 0.5*dir[2];
      const cTarget = [rx*cU + ux*cV + dir[0]*cN, ry*cU + uy*cV + dir[1]*cN, rz*cU + uz*cV + dir[2]*cN];
      const cOrigin = [cTarget[0] + dir[0]*24.0, cTarget[1] + dir[1]*24.0, cTarget[2] + dir[2]*24.0];
      contactRig = {
        origin: cOrigin, forward: lightForward,
        viewProj: mat4Multiply(mat4OrthoRH(-cHalf, cHalf, -cHalf, cHalf, 1.0, 52.0), mat4LookAtRH(cOrigin, cTarget, upRef)),
        halfSpan: cHalf, perspTan: 0.0,
      };
    } else {
      const target = [state.carX * 0.55, state.carY * 0.55, 0.45];
      const lightOrigin = [
        target[0] + dir[0] * state.lightDistanceM,
        target[1] + dir[1] * state.lightDistanceM,
        Math.max(1.0, target[2] + dir[2] * state.lightDistanceM),
      ];
      keyLight = makePerspectiveSpotRig(lightOrigin, target, 74.0, state.areaRadiusM, KELVIN_COLORS[state.lightKelvin], state.lightIntensity, keyShadowRes);

      const cTarget = [state.carX, state.carY, 0.55];
      const cDist = Math.hypot(cTarget[0]-lightOrigin[0], cTarget[1]-lightOrigin[1], cTarget[2]-lightOrigin[2]) || 1;
      const cFov = Math.min((68.0 * Math.PI) / 180.0, 2.0 * Math.atan(3.5 / Math.max(1.2, cDist)));
      contactRig = {
        origin: lightOrigin,
        forward: [(cTarget[0]-lightOrigin[0])/cDist, (cTarget[1]-lightOrigin[1])/cDist, (cTarget[2]-lightOrigin[2])/cDist],
        viewProj: mat4Multiply(mat4PerspectiveRH(cFov, 1.0, 0.35, 50.0), mat4LookAtRH(lightOrigin, cTarget, upRef)),
        halfSpan: 0.0, perspTan: Math.tan(cFov * 0.5),
      };
    }

    // Light 1 (Warm Overhead Gantry Bay Spot) & Light 2 (Cool Opposite Rim Bay Spot)
    const sweepRad = state.lightSweep ? (state.lightAzimuthDeg * Math.PI) / 180.0 : 0.0;
    const bay1Origin = [2.6 * Math.cos(sweepRad * 0.4) - 0.5, -5.1, 5.7];
    const bay2Origin = [-5.3, 4.9, 5.2];
    const light1 = makePerspectiveSpotRig(bay1Origin, [state.carX * 0.25, 0.15, 0.55], 76.0, 0.34, [1.00, 0.90, 0.76], 1.10, secShadowRes);
    const light2 = makePerspectiveSpotRig(bay2Origin, [-0.8 + state.carX * 0.20, 0.35, 0.65], 76.0, 0.28, [0.76, 0.90, 1.12], 1.05, secShadowRes);

    const lights = [keyLight, light1, light2];
    for (let i = 0; i < 3; i++) {
      lightU.viewProj.set(lights[i].lightViewProj, i * 16);
      lightU.type[i] = lights[i].typeInt;
      lightU.origin.set(lights[i].lightOrigin, i * 3);
      lightU.forward.set(lights[i].lightForward, i * 3);
      lightU.dir.set(lights[i].lightDir, i * 3);
      lightU.color.set(lights[i].color, i * 3);
      lightU.intensity[i] = lights[i].intensity;
      lightU.radius[i] = lights[i].lightRadius;
      lightU.res[i] = lights[i].res;
      lightU.orthoHalf[i] = lights[i].orthoHalfSpan;
      lightU.perspTan[i] = lights[i].perspTanHalfFov;
    }

    return { lights, contactRig };
  }

  function bindMultiLightArrays(prog, includeProj = false) {
    const loc = (n) => gl.getUniformLocation(prog, n);
    const locArr = (n) => gl.getUniformLocation(prog, n + "[0]") || gl.getUniformLocation(prog, n);
    gl.uniform1i(loc("uLightCount"), state.lightCount);
    gl.uniform1iv(locArr("uLightType"), lightU.type);
    gl.uniform3fv(locArr("uLightOrigin"), lightU.origin);
    gl.uniform3fv(locArr("uLightForward"), lightU.forward);
    gl.uniform3fv(locArr("uLightDir"), lightU.dir);
    gl.uniform3fv(locArr("uLightColor"), lightU.color);
    gl.uniform1fv(locArr("uLightIntensity"), lightU.intensity);
    const rLoc = locArr("uLightRadius");
    if (rLoc !== null) gl.uniform1fv(rLoc, lightU.radius);
    if (includeProj) {
      gl.uniformMatrix4fv(locArr("uLightViewProj"), false, lightU.viewProj);
      gl.uniform1fv(locArr("uShadowMapRes"), lightU.res);
      gl.uniform1fv(locArr("uOrthoHalfSpan"), lightU.orthoHalf);
      gl.uniform1fv(locArr("uPerspTanHalfFov"), lightU.perspTan);
    }
  }

  //======================================================================================================================
  // STATIC SDF RADIOSITY CACHE (Bakes ONLY at boot or when Light/Paint changes — NEVER on vehicle movement!)
  //======================================================================================================================
  let lastRadiositySig = "";
  function bakeStaticRadiosityVolume(prog, targetPair, vol, srcTexForBounce) {
    gl.viewport(0, 0, vol.nx, vol.ny);
    gl.disable(gl.DEPTH_TEST);
    gl.bindVertexArray(emptyVao);
    gl.useProgram(prog);

    const loc = (n) => gl.getUniformLocation(prog, n);
    gl.uniform3fv(loc("uTargetMin"), vol.min);
    gl.uniform3fv(loc("uTargetMax"), vol.max);
    gl.uniform1f(loc("uTargetVoxel"), (vol.max[2] - vol.min[2]) / vol.nz);
    gl.uniform1i(loc("uTargetDepth"), vol.nz);
    gl.uniform3fv(loc("uClip0Min"), clip0Vol.min);
    gl.uniform3f(loc("uClip0InvSpan"), 1/clip0Span[0], 1/clip0Span[1], 1/clip0Span[2]);
    gl.uniform3fv(loc("uClip1Min"), clip1Vol.min);
    gl.uniform3f(loc("uClip1InvSpan"), 1/clip1Span[0], 1/clip1Span[1], 1/clip1Span[2]);
    gl.uniform3fv(loc("uPaintColor"), PAINT_PALETTE[state.paintIdx]);

    const sliceLoc = loc("uSliceIndex");
    if (prog === progStaticShade) {
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_3D, targetPair.rawTex);
      gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_3D, clip0Pair.distTex);
      gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_3D, clip1Pair.distTex);
      gl.uniform1i(loc("uTargetRaw"), 0);
      gl.uniform1i(loc("uClip0DistLinear"), 1);
      gl.uniform1i(loc("uClip1DistLinear"), 2);
      bindMultiLightArrays(prog, false);
      gl.drawBuffers([gl.COLOR_ATTACHMENT0, gl.COLOR_ATTACHMENT1]);
      for (let iz = 0; iz < vol.nz; iz++) {
        gl.framebufferTextureLayer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, targetPair.directTex, 0, iz);
        gl.framebufferTextureLayer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT1, targetPair.reflA, 0, iz);
        gl.uniform1i(sliceLoc, iz);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
      }
    } else {
      const { prev0, prev1, dstTex } = srcTexForBounce;
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_3D, targetPair.rawTex);
      gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_3D, targetPair.directTex);
      gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_3D, prev0);
      gl.activeTexture(gl.TEXTURE3); gl.bindTexture(gl.TEXTURE_3D, prev1);
      gl.uniform1i(loc("uTargetRaw"), 0);
      gl.uniform1i(loc("uDirectVoxel"), 1);
      gl.uniform1i(loc("uPrevClip0Linear"), 2);
      gl.uniform1i(loc("uPrevClip1Linear"), 3);
      gl.uniform1f(loc("uClip0Voxel"), clip0Voxel);
      gl.uniform1f(loc("uClip1Voxel"), clip1Voxel);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT1, gl.TEXTURE_2D, null, 0);
      gl.drawBuffers([gl.COLOR_ATTACHMENT0]);
      for (let iz = 0; iz < vol.nz; iz++) {
        gl.framebufferTextureLayer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, dstTex, 0, iz);
        gl.uniform1i(sliceLoc, iz);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
      }
    }
  }

  function ensureStaticRadiosityUpdated() {
    const azKey = state.lightSweep ? Math.round(state.lightAzimuthDeg / 3.0) : Math.round(state.lightAzimuthDeg * 2);
    const sig = `${state.lightCount}|${state.lightType}|${azKey}|${Math.round(state.lightElevationDeg * 2)}|${state.lightIntensity.toFixed(2)}|${state.lightKelvin}|${state.paintIdx}`;
    if (sig === lastRadiositySig) return;
    lastRadiositySig = sig;
    gl.bindFramebuffer(gl.FRAMEBUFFER, sdfSliceFbo);
    bakeStaticRadiosityVolume(progStaticShade, clip0Pair, clip0Vol, null);
    bakeStaticRadiosityVolume(progStaticShade, clip1Pair, clip1Vol, null);
    bakeStaticRadiosityVolume(progSdfBounce, clip0Pair, clip0Vol, { prev0: clip0Pair.directTex, prev1: clip1Pair.directTex, dstTex: clip0Pair.tmpTex });
    bakeStaticRadiosityVolume(progSdfBounce, clip1Pair, clip1Vol, { prev0: clip0Pair.directTex, prev1: clip1Pair.directTex, dstTex: clip1Pair.tmpTex });
    bakeStaticRadiosityVolume(progSdfBounce, clip0Pair, clip0Vol, { prev0: clip0Pair.tmpTex, prev1: clip1Pair.tmpTex, dstTex: clip0Pair.texA });
    bakeStaticRadiosityVolume(progSdfBounce, clip1Pair, clip1Vol, { prev0: clip0Pair.tmpTex, prev1: clip1Pair.tmpTex, dstTex: clip1Pair.texA });
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }

  function runDynamicSdfUnion(bodyModel, bodyInv, wheelInvs) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, sdfSliceFbo);
    gl.viewport(0, 0, clip0Vol.nx, clip0Vol.ny);
    gl.disable(gl.DEPTH_TEST);
    gl.drawBuffers([gl.COLOR_ATTACHMENT0, gl.COLOR_ATTACHMENT1]);
    gl.useProgram(progSdfUnion);

    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_3D, clip0Pair.texA);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_3D, clip0Pair.reflA);
    gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_3D, clip0Pair.rawTex);
    gl.activeTexture(gl.TEXTURE3); gl.bindTexture(gl.TEXTURE_3D, bodySdfTex);
    gl.activeTexture(gl.TEXTURE4); gl.bindTexture(gl.TEXTURE_3D, rimSdfTex);
    gl.activeTexture(gl.TEXTURE5); gl.bindTexture(gl.TEXTURE_3D, tyreSdfTex);

    const loc = (n) => gl.getUniformLocation(progSdfUnion, n);
    gl.uniform1i(loc("uStaticClip0"), 0);
    gl.uniform1i(loc("uStaticClip0Refl"), 1);
    gl.uniform1i(loc("uClip0Raw"), 2);
    gl.uniform1i(loc("uBodySdf"), 3);
    gl.uniform1i(loc("uRimSdf"), 4);
    gl.uniform1i(loc("uTyreSdf"), 5);

    gl.uniform3fv(loc("uClip0Min"), clip0Vol.min);
    gl.uniform3fv(loc("uClip0Max"), clip0Vol.max);
    const bVol = sdfVolumes[0], rVol = sdfVolumes[1], tVol = sdfVolumes[2];
    gl.uniform3fv(loc("uBodySdfMin"), bVol.min);
    gl.uniform3f(loc("uBodySdfInvSpan"), 1/(bVol.max[0]-bVol.min[0]), 1/(bVol.max[1]-bVol.min[1]), 1/(bVol.max[2]-bVol.min[2]));
    gl.uniform3fv(loc("uRimSdfMin"), rVol.min);
    gl.uniform3f(loc("uRimSdfInvSpan"), 1/(rVol.max[0]-rVol.min[0]), 1/(rVol.max[1]-rVol.min[1]), 1/(rVol.max[2]-rVol.min[2]));
    gl.uniform3fv(loc("uTyreSdfMin"), tVol.min);
    gl.uniform3f(loc("uTyreSdfInvSpan"), 1/(tVol.max[0]-tVol.min[0]), 1/(tVol.max[1]-tVol.min[1]), 1/(tVol.max[2]-tVol.min[2]));

    gl.uniformMatrix4fv(loc("uBodyLocalToWorld"), false, bodyModel);
    gl.uniformMatrix4fv(loc("uBodyWorldToLocal"), false, bodyInv);
    const flatWheels = new Float32Array(64);
    for (let w = 0; w < 4; w++) flatWheels.set(wheelInvs[w], w * 16);
    gl.uniformMatrix4fv(loc("uWheelWorldToLocal[0]") || loc("uWheelWorldToLocal"), false, flatWheels);
    gl.uniform3f(loc("uCarCenter"), state.carX, state.carY, TYRE_RADIUS);
    gl.uniform3fv(loc("uPaintColor"), PAINT_PALETTE[state.paintIdx]);
    bindMultiLightArrays(progSdfUnion, false);

    const zNormLoc = loc("uSliceZNorm"), idxLoc = loc("uSliceIndex");
    gl.bindVertexArray(emptyVao);
    for (let iz = 0; iz < clip0Vol.nz; iz++) {
      gl.framebufferTextureLayer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, clip0Pair.texB, 0, iz);
      gl.framebufferTextureLayer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT1, clip0Pair.reflB, 0, iz);
      gl.uniform1f(zNormLoc, (iz + 0.5) / clip0Vol.nz);
      gl.uniform1i(idxLoc, iz);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }

  //======================================================================================================================
  // RENDER PASSES
  //======================================================================================================================
  function drawSceneGeometry(prog, modelLoc, overLoc, skipEmissivePanel = false) {
    for (const inst of activeInstances) {
      if (skipEmissivePanel && inst.meshIdx === 5) continue;
      const m = meshVaos[inst.meshIdx];
      gl.bindVertexArray(m.vao);
      gl.uniformMatrix4fv(modelLoc, false, inst.l2w);
      gl.uniform1i(overLoc, inst.matOverride);
      gl.drawArrays(gl.TRIANGLES, 0, m.vertexCount);
    }
  }

  function runGBuffer(viewProj) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, gBufferFbo);
    gl.viewport(0, 0, screenW, screenH);
    gl.enable(gl.DEPTH_TEST);
    gl.drawBuffers([gl.COLOR_ATTACHMENT0, gl.COLOR_ATTACHMENT1, gl.COLOR_ATTACHMENT2]);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.useProgram(progGBuffer);
    gl.uniformMatrix4fv(gl.getUniformLocation(progGBuffer, "uViewProj"), false, viewProj);
    gl.uniform3fv(gl.getUniformLocation(progGBuffer, "uPaintColor"), PAINT_PALETTE[state.paintIdx]);
    gl.uniform1f(gl.getUniformLocation(progGBuffer, "uEmissiveIntensity"), state.emissiveOn ? state.emissiveIntensity : 0.0);
    bindMultiLightArrays(progGBuffer, false);
    drawSceneGeometry(progGBuffer, gl.getUniformLocation(progGBuffer, "uModel"), gl.getUniformLocation(progGBuffer, "uMatOverride"));
  }

  const CLEAR_DEPTH_FLOAT = new Float32Array([1e5, 0, 0, 1]);
  function runLightDepthPass(target, viewProj, origin, forward, typeInt, skipEmissivePanel = false) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, target.fbo);
    gl.viewport(0, 0, target.res, target.res);
    gl.enable(gl.DEPTH_TEST);
    gl.drawBuffers([gl.COLOR_ATTACHMENT0]);
    gl.clearBufferfv(gl.COLOR, 0, CLEAR_DEPTH_FLOAT);
    gl.clear(gl.DEPTH_BUFFER_BIT);
    gl.useProgram(progLightDepth);
    gl.uniformMatrix4fv(gl.getUniformLocation(progLightDepth, "uLightViewProj"), false, viewProj);
    gl.uniform1i(gl.getUniformLocation(progLightDepth, "uLightType"), typeInt);
    gl.uniform3fv(gl.getUniformLocation(progLightDepth, "uLightOrigin"), origin);
    gl.uniform3fv(gl.getUniformLocation(progLightDepth, "uLightForward"), forward);
    drawSceneGeometry(progLightDepth, gl.getUniformLocation(progLightDepth, "uModel"), gl.getUniformLocation(progLightDepth, "uMatOverride"), skipEmissivePanel);
  }

  function bindGlobalSdfUniforms(prog, startUnit) {
    const texs = [clip0Pair.texB, clip0Pair.reflB, clip1Pair.texA, clip1Pair.reflA];
    const names = ["uClip0Linear", "uClip0Refl", "uClip1Linear", "uClip1Refl"];
    for (let i = 0; i < texs.length; i++) {
      gl.activeTexture(gl.TEXTURE0 + startUnit + i);
      gl.bindTexture(gl.TEXTURE_3D, texs[i]);
      gl.uniform1i(gl.getUniformLocation(prog, names[i]), startUnit + i);
    }
    gl.uniform3fv(gl.getUniformLocation(prog, "uClip0Min"), clip0Vol.min);
    gl.uniform3f(gl.getUniformLocation(prog, "uClip0InvSpan"), 1/clip0Span[0], 1/clip0Span[1], 1/clip0Span[2]);
    gl.uniform1f(gl.getUniformLocation(prog, "uClip0Voxel"), clip0Voxel);
    gl.uniform3fv(gl.getUniformLocation(prog, "uClip1Min"), clip1Vol.min);
    gl.uniform3f(gl.getUniformLocation(prog, "uClip1InvSpan"), 1/clip1Span[0], 1/clip1Span[1], 1/clip1Span[2]);
    gl.uniform1f(gl.getUniformLocation(prog, "uClip1Voxel"), clip1Voxel);
  }

  function bindShadowSdfUniforms(prog, startUnit, bodyInv, wheelInvs) {
    const texs = [clip0Pair.distTex, clip1Pair.distTex, clip0Pair.texB, bodySdfTex, rimSdfTex, tyreSdfTex, louverSdfTex];
    const names = ["uClip0Dist", "uClip1Dist", "uClip0Linear", "uBodySdf", "uRimSdf", "uTyreSdf", "uLouverSdf"];
    for (let i = 0; i < texs.length; i++) {
      gl.activeTexture(gl.TEXTURE0 + startUnit + i);
      gl.bindTexture(gl.TEXTURE_3D, texs[i]);
      gl.uniform1i(gl.getUniformLocation(prog, names[i]), startUnit + i);
    }
    gl.uniform3fv(gl.getUniformLocation(prog, "uClip0Min"), clip0Vol.min);
    gl.uniform3f(gl.getUniformLocation(prog, "uClip0InvSpan"), 1/clip0Span[0], 1/clip0Span[1], 1/clip0Span[2]);
    gl.uniform1f(gl.getUniformLocation(prog, "uClip0Voxel"), clip0Voxel);
    gl.uniform3fv(gl.getUniformLocation(prog, "uClip1Min"), clip1Vol.min);
    gl.uniform3f(gl.getUniformLocation(prog, "uClip1InvSpan"), 1/clip1Span[0], 1/clip1Span[1], 1/clip1Span[2]);
    gl.uniform1f(gl.getUniformLocation(prog, "uClip1Voxel"), clip1Voxel);

    const bindBox = (v, prefix) => {
      gl.uniform3fv(gl.getUniformLocation(prog, prefix + "Min"), v.min);
      gl.uniform3fv(gl.getUniformLocation(prog, prefix + "Max"), v.max);
      gl.uniform3f(gl.getUniformLocation(prog, prefix + "InvSpan"), 1/(v.max[0]-v.min[0]), 1/(v.max[1]-v.min[1]), 1/(v.max[2]-v.min[2]));
    };
    bindBox(sdfVolumes[0], "uBodySdf");
    bindBox(sdfVolumes[1], "uRimSdf");
    bindBox(sdfVolumes[2], "uTyreSdf");
    bindBox(louversSdfVol, "uLouverSdf");

    gl.uniformMatrix4fv(gl.getUniformLocation(prog, "uBodyWorldToLocal"), false, bodyInv);
    const flatWheels = new Float32Array(64);
    for (let w = 0; w < 4; w++) flatWheels.set(wheelInvs[w], w * 16);
    gl.uniformMatrix4fv(gl.getUniformLocation(prog, "uWheelWorldToLocal[0]") || gl.getUniformLocation(prog, "uWheelWorldToLocal"), false, flatWheels);
    gl.uniform1i(gl.getUniformLocation(prog, "uSdfMode"), state.sdfMode === "clip" ? 1 : 0);
    gl.uniform1i(gl.getUniformLocation(prog, "uShowLouvers"), state.showLouvers ? 1 : 0);
  }

  function runShadowAlgorithm(algo, outFbo, lightRig, viewProj, bodyInv, wheelInvs, camPos, scissorRect) {
    const prog = shadowProgs[algo];
    const q = QUALITY_PRESETS[state.qualityIdx];
    const shouldFilter = state.planeFilter && (algo === "pcss" || algo === "contact_pcss" || algo === "smrt" || algo === "hybrid" || algo === "bvh");

    gl.disable(gl.DEPTH_TEST);
    if (scissorRect) {
      gl.enable(gl.SCISSOR_TEST);
      gl.scissor(scissorRect[0], scissorRect[1], scissorRect[2], scissorRect[3]);
    }

    gl.bindFramebuffer(gl.FRAMEBUFFER, shouldFilter ? shadowRawFbo : outFbo);
    gl.viewport(0, 0, shadowTraceW, shadowTraceH);
    gl.useProgram(prog);

    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, gPosTex);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, gNormTex);
    gl.uniform1i(gl.getUniformLocation(prog, "uGPosRough"), 0);
    gl.uniform1i(gl.getUniformLocation(prog, "uGNormMat"), 1);

    const needsMaps = (algo === "pcf" || algo === "pcss" || algo === "contact_pcss" || algo === "smrt");
    bindMultiLightArrays(prog, needsMaps);

    if (needsMaps) {
      gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, shadowMaps[0].tex);
      gl.activeTexture(gl.TEXTURE3); gl.bindTexture(gl.TEXTURE_2D, shadowMaps[1].tex);
      gl.activeTexture(gl.TEXTURE4); gl.bindTexture(gl.TEXTURE_2D, shadowMaps[2].tex);
      gl.uniform1i(gl.getUniformLocation(prog, "uShadowMap0"), 2);
      gl.uniform1i(gl.getUniformLocation(prog, "uShadowMap1"), 3);
      gl.uniform1i(gl.getUniformLocation(prog, "uShadowMap2"), 4);
      if (algo === "pcss" || algo === "contact_pcss") gl.uniform1i(gl.getUniformLocation(prog, "uPcssTaps"), q.pcssTaps);
      if (algo === "smrt") gl.uniform1i(gl.getUniformLocation(prog, "uSmrtRays"), q.smrtRays);
    }

    if (algo === "contact_pcss") {
      gl.activeTexture(gl.TEXTURE5); gl.bindTexture(gl.TEXTURE_2D, contactMap.tex);
      gl.uniform1i(gl.getUniformLocation(prog, "uContactMap"), 5);
      gl.uniformMatrix4fv(gl.getUniformLocation(prog, "uContactViewProj"), false, lightRig.contactRig.viewProj);
      gl.uniform3fv(gl.getUniformLocation(prog, "uContactOrigin"), lightRig.contactRig.origin);
      gl.uniform3fv(gl.getUniformLocation(prog, "uContactForward"), lightRig.contactRig.forward);
      gl.uniform1f(gl.getUniformLocation(prog, "uContactHalfSpan"), lightRig.contactRig.halfSpan);
      gl.uniform1f(gl.getUniformLocation(prog, "uContactRes"), contactRes);
      gl.uniform1f(gl.getUniformLocation(prog, "uContactPerspTan"), lightRig.contactRig.perspTan);
      gl.uniformMatrix4fv(gl.getUniformLocation(prog, "uCamViewProj"), false, viewProj);
      gl.uniform3fv(gl.getUniformLocation(prog, "uCamPos"), camPos);
    } else if (algo === "sdf") {
      bindShadowSdfUniforms(prog, 2, bodyInv, wheelInvs);
    } else if (algo === "hybrid") {
      gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, contactMap.tex);
      gl.uniform1i(gl.getUniformLocation(prog, "uContactMap"), 2);
      gl.uniformMatrix4fv(gl.getUniformLocation(prog, "uContactViewProj"), false, lightRig.contactRig.viewProj);
      gl.uniform3fv(gl.getUniformLocation(prog, "uContactOrigin"), lightRig.contactRig.origin);
      gl.uniform3fv(gl.getUniformLocation(prog, "uContactForward"), lightRig.contactRig.forward);
      gl.uniform1f(gl.getUniformLocation(prog, "uContactHalfSpan"), lightRig.contactRig.halfSpan);
      gl.uniform1f(gl.getUniformLocation(prog, "uContactRes"), contactRes);
      gl.uniform1f(gl.getUniformLocation(prog, "uContactPerspTan"), lightRig.contactRig.perspTan);
      bindShadowSdfUniforms(prog, 3, bodyInv, wheelInvs);
    } else if (algo === "bvh") {
      gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, bvhNodesTex);
      gl.activeTexture(gl.TEXTURE3); gl.bindTexture(gl.TEXTURE_2D, bvhTrisTex);
      gl.activeTexture(gl.TEXTURE4); gl.bindTexture(gl.TEXTURE_2D, instancesTex);
      gl.uniform1i(gl.getUniformLocation(prog, "uBvhNodes"), 2);
      gl.uniform1i(gl.getUniformLocation(prog, "uBvhTris"), 3);
      gl.uniform1i(gl.getUniformLocation(prog, "uInstances"), 4);
      gl.uniform1i(gl.getUniformLocation(prog, "uMaxBvhInstances"), activeInstances.length);
      gl.uniform1i(gl.getUniformLocation(prog, "uBvhRays"), q.bvhRays);
    }

    gl.bindVertexArray(emptyVao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);

    if (shouldFilter) {
      gl.useProgram(progPlaneFilter);
      gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, gPosTex);
      gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, gNormTex);
      gl.uniform1i(gl.getUniformLocation(progPlaneFilter, "uShadowIn"), 0);
      gl.uniform1i(gl.getUniformLocation(progPlaneFilter, "uGPosRough"), 1);
      gl.uniform1i(gl.getUniformLocation(progPlaneFilter, "uGNormMat"), 2);
      gl.uniform2f(gl.getUniformLocation(progPlaneFilter, "uTexelSize"), 1.0 / shadowTraceW, 1.0 / shadowTraceH);
      gl.uniform1i(gl.getUniformLocation(progPlaneFilter, "uLightType0"), lightRig.lights[0].typeInt);
      gl.uniform3fv(gl.getUniformLocation(progPlaneFilter, "uLightOrigin0"), lightRig.lights[0].lightOrigin);
      gl.uniform3fv(gl.getUniformLocation(progPlaneFilter, "uCamPos"), camPos);
      gl.uniform1f(gl.getUniformLocation(progPlaneFilter, "uLightRadius0"), lightRig.lights[0].lightRadius);
      gl.uniform1f(gl.getUniformLocation(progPlaneFilter, "uViewportHeight"), shadowTraceH);
      const stepLoc = gl.getUniformLocation(progPlaneFilter, "uStepPx");

      gl.bindFramebuffer(gl.FRAMEBUFFER, shadowPingAFbo);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, shadowRawTex);
      gl.uniform1i(stepLoc, 1); gl.drawArrays(gl.TRIANGLES, 0, 3);

      gl.bindFramebuffer(gl.FRAMEBUFFER, shadowPingBFbo);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, shadowPingATex);
      gl.uniform1i(stepLoc, 2); gl.drawArrays(gl.TRIANGLES, 0, 3);

      gl.bindFramebuffer(gl.FRAMEBUFFER, outFbo);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, shadowPingBTex);
      gl.uniform1i(stepLoc, 4); gl.drawArrays(gl.TRIANGLES, 0, 3);
    }

    if (scissorRect) gl.disable(gl.SCISSOR_TEST);
  }

  function runEmissiveShadowPass(algo, outFbo, viewProj, bodyInv, wheelInvs, camPos, scissorRect) {
    const q = QUALITY_PRESETS[state.qualityIdx];
    const shouldFilter = state.planeFilter && (algo !== "pcf" && algo !== "sdf");
    const algoMap = { pcf: 0, pcss: 1, contact_pcss: 2, smrt: 3, sdf: 4, hybrid: 5 };

    gl.disable(gl.DEPTH_TEST);
    if (scissorRect) {
      gl.enable(gl.SCISSOR_TEST);
      gl.scissor(scissorRect[0], scissorRect[1], scissorRect[2], scissorRect[3]);
    }

    gl.bindFramebuffer(gl.FRAMEBUFFER, shouldFilter ? shadowRawFbo : outFbo);
    gl.viewport(0, 0, shadowTraceW, shadowTraceH);
    gl.bindVertexArray(emptyVao);

    if (algo === "bvh") {
      const prog = progEmissiveBvhSh;
      gl.useProgram(prog);
      const loc = (n) => gl.getUniformLocation(prog, n);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, gPosTex);
      gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, gNormTex);
      gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, bvhNodesTex);
      gl.activeTexture(gl.TEXTURE3); gl.bindTexture(gl.TEXTURE_2D, bvhTrisTex);
      gl.activeTexture(gl.TEXTURE4); gl.bindTexture(gl.TEXTURE_2D, instancesTex);
      gl.uniform1i(loc("uGPosRough"), 0);
      gl.uniform1i(loc("uGNormMat"), 1);
      gl.uniform1i(loc("uBvhNodes"), 2);
      gl.uniform1i(loc("uBvhTris"), 3);
      gl.uniform1i(loc("uInstances"), 4);
      gl.uniform1i(loc("uMaxBvhInstances"), activeInstances.length);
      gl.uniform3fv(loc("uEmissiveCenter"), EMISSIVE_RIG.center);
      gl.uniform3fv(loc("uEmissiveRight"), EMISSIVE_RIG.right);
      gl.uniform3fv(loc("uEmissiveUp"), EMISSIVE_RIG.up);
      gl.uniform3fv(loc("uEmissiveNormal"), EMISSIVE_RIG.normal);
      gl.uniform1f(loc("uEmissiveHalfW"), EMISSIVE_RIG.halfW);
      gl.uniform1f(loc("uEmissiveHalfH"), EMISSIVE_RIG.halfH);
      gl.uniform1i(loc("uBvhRays"), Math.max(2, q.bvhRays));
    } else {
      const prog = progEmissiveMapSh;
      gl.useProgram(prog);
      const loc = (n) => gl.getUniformLocation(prog, n);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, gPosTex);
      gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, gNormTex);
      gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, emissiveDepthMap.tex);
      gl.uniform1i(loc("uGPosRough"), 0);
      gl.uniform1i(loc("uGNormMat"), 1);
      gl.uniform1i(loc("uEmissiveDepthMap"), 2);
      bindShadowSdfUniforms(prog, 3, bodyInv, wheelInvs);
      gl.uniform1i(loc("uEmissiveAlgo"), algoMap[algo] ?? 2);
      gl.uniformMatrix4fv(loc("uEmissiveViewProj"), false, EMISSIVE_RIG.viewProj);
      gl.uniform3fv(loc("uEmissiveDepthOrigin"), EMISSIVE_RIG.shadowOrigin);
      gl.uniform3fv(loc("uEmissiveCenter"), EMISSIVE_RIG.center);
      gl.uniform3fv(loc("uEmissiveNormal"), EMISSIVE_RIG.normal);
      gl.uniform1f(loc("uEmissiveHalfW"), EMISSIVE_RIG.halfW);
      gl.uniform1f(loc("uEmissiveHalfH"), EMISSIVE_RIG.halfH);
      gl.uniform1f(loc("uEmissiveDepthRes"), EMISSIVE_RIG.res);
      gl.uniform1f(loc("uEmissivePerspTan"), EMISSIVE_RIG.perspTan);
      gl.uniformMatrix4fv(loc("uCamViewProj"), false, viewProj);
      gl.uniform3fv(loc("uCamPos"), camPos);
      gl.uniform1i(loc("uPcssTaps"), q.pcssTaps);
      gl.uniform1i(loc("uSmrtRays"), q.smrtRays);
    }

    gl.drawArrays(gl.TRIANGLES, 0, 3);

    if (shouldFilter) {
      gl.useProgram(progPlaneFilter);
      gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, gPosTex);
      gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, gNormTex);
      gl.uniform1i(gl.getUniformLocation(progPlaneFilter, "uShadowIn"), 0);
      gl.uniform1i(gl.getUniformLocation(progPlaneFilter, "uGPosRough"), 1);
      gl.uniform1i(gl.getUniformLocation(progPlaneFilter, "uGNormMat"), 2);
      gl.uniform2f(gl.getUniformLocation(progPlaneFilter, "uTexelSize"), 1.0 / shadowTraceW, 1.0 / shadowTraceH);
      gl.uniform1i(gl.getUniformLocation(progPlaneFilter, "uLightType0"), 1);
      gl.uniform3fv(gl.getUniformLocation(progPlaneFilter, "uLightOrigin0"), EMISSIVE_RIG.center);
      gl.uniform3fv(gl.getUniformLocation(progPlaneFilter, "uCamPos"), camPos);
      gl.uniform1f(gl.getUniformLocation(progPlaneFilter, "uLightRadius0"), EMISSIVE_RIG.halfW * 0.6);
      gl.uniform1f(gl.getUniformLocation(progPlaneFilter, "uViewportHeight"), shadowTraceH);
      const stepLoc = gl.getUniformLocation(progPlaneFilter, "uStepPx");

      gl.bindFramebuffer(gl.FRAMEBUFFER, shadowPingAFbo);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, shadowRawTex);
      gl.uniform1i(stepLoc, 1); gl.drawArrays(gl.TRIANGLES, 0, 3);

      gl.bindFramebuffer(gl.FRAMEBUFFER, outFbo);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, shadowPingATex);
      gl.uniform1i(stepLoc, 2); gl.drawArrays(gl.TRIANGLES, 0, 3);
    }

    if (scissorRect) gl.disable(gl.SCISSOR_TEST);
  }

  function runGiPass(mode, outFbo) {
    const q = QUALITY_PRESETS[state.qualityIdx];
    gl.disable(gl.DEPTH_TEST);
    gl.bindFramebuffer(gl.FRAMEBUFFER, giRawFbo);
    gl.viewport(0, 0, giTraceW, giTraceH);
    gl.bindVertexArray(emptyVao);

    if (mode === "sdf") {
      gl.useProgram(progGiSdf);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, gPosTex);
      gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, gNormTex);
      gl.uniform1i(gl.getUniformLocation(progGiSdf, "uGPosRough"), 0);
      gl.uniform1i(gl.getUniformLocation(progGiSdf, "uGNormMat"), 1);
      gl.uniform1i(gl.getUniformLocation(progGiSdf, "uGiCones"), q.giCones);
      bindGlobalSdfUniforms(progGiSdf, 2);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    } else {
      gl.useProgram(progGiBvh);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, gPosTex);
      gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, gNormTex);
      gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, bvhNodesTex);
      gl.activeTexture(gl.TEXTURE3); gl.bindTexture(gl.TEXTURE_2D, bvhTrisTex);
      gl.activeTexture(gl.TEXTURE4); gl.bindTexture(gl.TEXTURE_2D, instancesTex);
      gl.uniform1i(gl.getUniformLocation(progGiBvh, "uGPosRough"), 0);
      gl.uniform1i(gl.getUniformLocation(progGiBvh, "uGNormMat"), 1);
      gl.uniform1i(gl.getUniformLocation(progGiBvh, "uBvhNodes"), 2);
      gl.uniform1i(gl.getUniformLocation(progGiBvh, "uBvhTris"), 3);
      gl.uniform1i(gl.getUniformLocation(progGiBvh, "uInstances"), 4);
      gl.uniform1i(gl.getUniformLocation(progGiBvh, "uMaxBvhInstances"), activeInstances.length);
      gl.uniform3fv(gl.getUniformLocation(progGiBvh, "uPaintColor"), PAINT_PALETTE[state.paintIdx]);
      gl.uniform1i(gl.getUniformLocation(progGiBvh, "uGiSamples"), q.giSamples);
      bindMultiLightArrays(progGiBvh, false);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }

    // 3-Pass Plane-Aware Variance-Clamped GI Denoiser
    gl.useProgram(progGiBilateral);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, gPosTex);
    gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, gNormTex);
    gl.uniform1i(gl.getUniformLocation(progGiBilateral, "uGiIn"), 0);
    gl.uniform1i(gl.getUniformLocation(progGiBilateral, "uGPosRough"), 1);
    gl.uniform1i(gl.getUniformLocation(progGiBilateral, "uGNormMat"), 2);
    gl.uniform2f(gl.getUniformLocation(progGiBilateral, "uTexelSize"), 1.0 / giTraceW, 1.0 / giTraceH);
    const stepLoc = gl.getUniformLocation(progGiBilateral, "uStepPx");

    gl.bindFramebuffer(gl.FRAMEBUFFER, giPingAFbo);
    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, giRawTex);
    gl.uniform1i(stepLoc, 1); gl.drawArrays(gl.TRIANGLES, 0, 3);

    gl.bindFramebuffer(gl.FRAMEBUFFER, giPingBFbo);
    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, giPingATex);
    gl.uniform1i(stepLoc, 2); gl.drawArrays(gl.TRIANGLES, 0, 3);

    gl.bindFramebuffer(gl.FRAMEBUFFER, outFbo);
    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, giPingBTex);
    gl.uniform1i(stepLoc, 4); gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  function runReflectionPass(mode, outFbo, viewProj, camPos, lightRig) {
    gl.disable(gl.DEPTH_TEST);
    gl.bindFramebuffer(gl.FRAMEBUFFER, outFbo);
    gl.viewport(0, 0, giTraceW, giTraceH);
    gl.bindVertexArray(emptyVao);

    if (mode === "sdf") {
      gl.useProgram(progReflSdf);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, gPosTex);
      gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, gNormTex);
      gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, gDirectTex);
      gl.uniform1i(gl.getUniformLocation(progReflSdf, "uGPosRough"), 0);
      gl.uniform1i(gl.getUniformLocation(progReflSdf, "uGNormMat"), 1);
      gl.uniform1i(gl.getUniformLocation(progReflSdf, "uGDirect"), 2);
      gl.uniformMatrix4fv(gl.getUniformLocation(progReflSdf, "uViewProj"), false, viewProj);
      gl.uniform3fv(gl.getUniformLocation(progReflSdf, "uCamPos"), camPos);
      gl.uniform3fv(gl.getUniformLocation(progReflSdf, "uSunDir"), lightRig.lights[0].lightDir);
      gl.uniform1i(gl.getUniformLocation(progReflSdf, "uLightType0"), lightRig.lights[0].typeInt);
      gl.uniform3fv(gl.getUniformLocation(progReflSdf, "uLightColor0"), lightRig.lights[0].color);
      gl.uniform1i(gl.getUniformLocation(progReflSdf, "uMaxBounces"), state.reflBounces);
      gl.uniform1f(gl.getUniformLocation(progReflSdf, "uRoughnessCutoff"), 0.56);
      bindGlobalSdfUniforms(progReflSdf, 3);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    } else {
      gl.useProgram(progReflBvh);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, gPosTex);
      gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, gNormTex);
      gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, bvhNodesTex);
      gl.activeTexture(gl.TEXTURE3); gl.bindTexture(gl.TEXTURE_2D, bvhTrisTex);
      gl.activeTexture(gl.TEXTURE4); gl.bindTexture(gl.TEXTURE_2D, instancesTex);
      gl.uniform1i(gl.getUniformLocation(progReflBvh, "uGPosRough"), 0);
      gl.uniform1i(gl.getUniformLocation(progReflBvh, "uGNormMat"), 1);
      gl.uniform1i(gl.getUniformLocation(progReflBvh, "uBvhNodes"), 2);
      gl.uniform1i(gl.getUniformLocation(progReflBvh, "uBvhTris"), 3);
      gl.uniform1i(gl.getUniformLocation(progReflBvh, "uInstances"), 4);
      gl.uniform1i(gl.getUniformLocation(progReflBvh, "uMaxBvhInstances"), activeInstances.length);
      gl.uniform3fv(gl.getUniformLocation(progReflBvh, "uCamPos"), camPos);
      gl.uniform3fv(gl.getUniformLocation(progReflBvh, "uPaintColor"), PAINT_PALETTE[state.paintIdx]);
      gl.uniform1i(gl.getUniformLocation(progReflBvh, "uMaxBounces"), state.reflBounces);
      gl.uniform1f(gl.getUniformLocation(progReflBvh, "uRoughnessCutoff"), 0.56);
      bindMultiLightArrays(progReflBvh, false);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }
  }

  function runComposite(invViewProj, camPos) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, screenW, screenH);
    gl.disable(gl.DEPTH_TEST);
    gl.useProgram(progComposite);

    const giTexA = state.giA === "sdf" ? giSdfFinalTex : giBvhFinalTex;
    const giTexB = state.giB === "sdf" ? giSdfFinalTex : giBvhFinalTex;
    const rTexA  = state.reflA === "sdf" ? reflSdfTex : reflBvhTex;
    const rTexB  = state.reflB === "sdf" ? reflSdfTex : reflBvhTex;

    const texs = [gPosTex, gNormTex, shadowFinalATex, shadowFinalBTex, giTexA, giTexB, rTexA, rTexB, emissiveFinalATex, emissiveFinalBTex];
    const names = ["uGPosRough", "uGNormMat", "uShadowA", "uShadowB", "uGiA", "uGiB", "uReflA", "uReflB", "uEmissiveVisA", "uEmissiveVisB"];
    for (let i = 0; i < texs.length; i++) {
      gl.activeTexture(gl.TEXTURE0 + i);
      gl.bindTexture(gl.TEXTURE_2D, texs[i]);
      gl.uniform1i(gl.getUniformLocation(progComposite, names[i]), i);
    }

    const loc = (n) => gl.getUniformLocation(progComposite, n);
    gl.uniformMatrix4fv(loc("uInvViewProj"), false, invViewProj);
    gl.uniform3fv(loc("uCamPos"), camPos);
    gl.uniform3fv(loc("uPaintColor"), PAINT_PALETTE[state.paintIdx]);
    bindMultiLightArrays(progComposite, false);
    gl.uniform1f(loc("uFillBoost"), state.fillBoost);
    gl.uniform1f(loc("uGiIntensity"), state.giIntensity);

    gl.uniform1i(loc("uEmissiveOn"), state.emissiveOn ? 1 : 0);
    gl.uniform1i(loc("uEmissiveShadowMode"), state.emissiveShadowMode === "shadowed" ? 1 : 0);
    gl.uniform3fv(loc("uEmissiveCenter"), EMISSIVE_RIG.center);
    gl.uniform3fv(loc("uEmissiveRight"), EMISSIVE_RIG.right);
    gl.uniform3fv(loc("uEmissiveUp"), EMISSIVE_RIG.up);
    gl.uniform3fv(loc("uEmissiveNormal"), EMISSIVE_RIG.normal);
    gl.uniform1f(loc("uEmissiveHalfW"), EMISSIVE_RIG.halfW);
    gl.uniform1f(loc("uEmissiveHalfH"), EMISSIVE_RIG.halfH);
    gl.uniform1f(loc("uEmissiveIntensity"), state.emissiveOn ? state.emissiveIntensity : 0.0);

    const giInt = (m) => (m === "off" ? 0 : (m === "sdf" ? 1 : 2));
    const reflInt = (m) => (m === "env" ? 0 : (m === "sdf" ? 1 : 2));
    gl.uniform1i(loc("uGiModeA"), giInt(state.giA));
    gl.uniform1i(loc("uGiModeB"), giInt(state.giB));
    gl.uniform1i(loc("uReflModeA"), reflInt(state.reflA));
    gl.uniform1i(loc("uReflModeB"), reflInt(state.reflB));

    const layoutInt = state.viewLayout === "split" ? 0 : (state.viewLayout === "single" ? 1 : 2);
    const surfInt = state.surfaceMode === "pbr" ? 0 : (state.surfaceMode === "clay" ? 1 : (state.surfaceMode === "mask" ? 2 : 3));
    gl.uniform1i(loc("uViewLayout"), layoutInt);
    gl.uniform1i(loc("uSurfaceMode"), surfInt);
    gl.uniform1f(loc("uSplitX"), state.splitX);
    gl.uniform2f(loc("uFullTexelSize"), 1.0 / screenW, 1.0 / screenH);
    gl.uniform2f(loc("uTraceTexelSize"), 1.0 / giTraceW, 1.0 / giTraceH);

    gl.bindVertexArray(emptyVao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  //======================================================================================================================
  // UI BINDING & EXPLANATION SYNC
  //======================================================================================================================
  function syncAlgoMetaUi() {
    document.getElementById("select-algo-a").value = state.algoA;
    document.getElementById("select-algo-b").value = state.algoB;
    document.getElementById("select-gi-a").value = state.giA;
    document.getElementById("select-gi-b").value = state.giB;
    document.getElementById("select-refl-a").value = state.reflA;
    document.getElementById("select-refl-b").value = state.reflB;

    document.getElementById("badge-left").textContent =
      `A · ${ALGO_INFO[state.algoA].badge} · ${GI_LABELS[state.giA]} · ${REFL_LABELS[state.reflA]}`;
    document.getElementById("badge-right").textContent =
      `B · ${ALGO_INFO[state.algoB].badge} · ${GI_LABELS[state.giB]} · ${REFL_LABELS[state.reflB]}`;

    document.getElementById("split-divider").style.display = (state.viewLayout === "split") ? "block" : "none";

    const noteEl = document.getElementById("algo-note");
    if (state.viewLayout === "single") {
      noteEl.innerHTML = ALGO_INFO[state.algoA].note;
    } else {
      noteEl.innerHTML =
        `<strong>A (${ALGO_INFO[state.algoA].badge}):</strong> ${ALGO_INFO[state.algoA].note}<br/>` +
        `<strong>B (${ALGO_INFO[state.algoB].badge}):</strong> ${ALGO_INFO[state.algoB].note}`;
    }
  }

  function syncLightUi() {
    const isSun = state.lightType === "sun";
    const sizeInput = document.getElementById("slider-light-size");
    document.getElementById("wrap-light-dist").style.display = isSun ? "none" : "flex";
    if (isSun) {
      document.getElementById("lbl-light-size").textContent = "Sun Radius";
      sizeInput.min = "0.00"; sizeInput.max = "8.00"; sizeInput.step = "0.05";
      sizeInput.value = state.sunRadiusDeg.toFixed(2);
      document.getElementById("val-light-size").textContent = state.sunRadiusDeg.toFixed(2) + "°";
      document.getElementById("stat-light").textContent = `${state.lightCount} Light${state.lightCount > 1 ? "s" : ""} · ${state.sunRadiusDeg.toFixed(2)}°`;
    } else {
      document.getElementById("lbl-light-size").textContent = "Disc Radius";
      sizeInput.min = "0.02"; sizeInput.max = "1.50"; sizeInput.step = "0.02";
      sizeInput.value = state.areaRadiusM.toFixed(2);
      document.getElementById("val-light-size").textContent = state.areaRadiusM.toFixed(2) + " m";
      document.getElementById("stat-light").textContent = `${state.lightCount} Light${state.lightCount > 1 ? "s" : ""} · ${state.areaRadiusM.toFixed(2)}m`;
    }
    document.getElementById("val-azimuth").textContent = Math.round(state.lightAzimuthDeg) + "°";
    document.getElementById("slider-azimuth").value = Math.round(state.lightAzimuthDeg);
    document.getElementById("val-elevation").textContent = Math.round(state.lightElevationDeg) + "°";
    document.getElementById("val-light-dist").textContent = state.lightDistanceM.toFixed(1) + " m";
    document.getElementById("val-light-int").textContent = state.lightIntensity.toFixed(2) + "×";
    document.getElementById("val-fill-boost").textContent = state.fillBoost.toFixed(2) + "×";
    document.getElementById("val-gi-int").textContent = state.giIntensity.toFixed(2) + "×";
    document.getElementById("val-emissive-int").textContent = state.emissiveIntensity.toFixed(2) + "×";
  }

  function syncDriveButtons() {
    document.querySelectorAll("[data-drive]").forEach((b) =>
      b.setAttribute("aria-pressed", b.dataset.drive === state.driveMode ? "true" : "false"));
  }

  function toggleHud() {
    state.hudHidden = !state.hudHidden;
    document.getElementById("compare-panel").classList.toggle("collapsed", state.hudHidden);
    document.getElementById("controls-dock").classList.toggle("collapsed", state.hudHidden);
    const btn = document.getElementById("btn-toggle-hud");
    btn.setAttribute("aria-pressed", state.hudHidden ? "true" : "false");
    btn.textContent = state.hudHidden ? "Show HUD [H]" : "Hide HUD";
  }

  document.getElementById("select-algo-a").addEventListener("change", (e) => { state.algoA = e.target.value; syncAlgoMetaUi(); });
  document.getElementById("select-algo-b").addEventListener("change", (e) => { state.algoB = e.target.value; syncAlgoMetaUi(); });
  document.getElementById("select-gi-a").addEventListener("change", (e) => { state.giA = e.target.value; syncAlgoMetaUi(); });
  document.getElementById("select-gi-b").addEventListener("change", (e) => { state.giB = e.target.value; syncAlgoMetaUi(); });
  document.getElementById("select-refl-a").addEventListener("change", (e) => { state.reflA = e.target.value; syncAlgoMetaUi(); });
  document.getElementById("select-refl-b").addEventListener("change", (e) => { state.reflB = e.target.value; syncAlgoMetaUi(); });

  document.getElementById("btn-swap-algos").addEventListener("click", () => {
    [state.algoA, state.algoB] = [state.algoB, state.algoA];
    [state.giA, state.giB]     = [state.giB, state.giA];
    [state.reflA, state.reflB] = [state.reflB, state.reflA];
    syncAlgoMetaUi();
  });

  document.querySelectorAll("[data-layout]").forEach((btn) => btn.addEventListener("click", () => {
    state.viewLayout = btn.dataset.layout;
    document.querySelectorAll("[data-layout]").forEach((b) => b.setAttribute("aria-pressed", b === btn ? "true" : "false"));
    syncAlgoMetaUi();
  }));
  document.querySelectorAll("[data-surface]").forEach((btn) => btn.addEventListener("click", () => {
    state.surfaceMode = btn.dataset.surface;
    document.querySelectorAll("[data-surface]").forEach((b) => b.setAttribute("aria-pressed", b === btn ? "true" : "false"));
  }));
  document.getElementById("btn-toggle-hud").addEventListener("click", toggleHud);

  document.querySelectorAll("[data-light-count]").forEach((btn) => btn.addEventListener("click", () => {
    state.lightCount = parseInt(btn.dataset.lightCount, 10);
    document.querySelectorAll("[data-light-count]").forEach((b) => b.setAttribute("aria-pressed", b === btn ? "true" : "false"));
    syncLightUi();
  }));
  document.querySelectorAll("[data-light-type]").forEach((btn) => btn.addEventListener("click", () => {
    state.lightType = btn.dataset.lightType;
    document.querySelectorAll("[data-light-type]").forEach((b) => b.setAttribute("aria-pressed", b === btn ? "true" : "false"));
    syncLightUi();
  }));
  document.getElementById("btn-light-sweep").addEventListener("click", (e) => {
    state.lightSweep = !state.lightSweep;
    e.currentTarget.setAttribute("aria-pressed", state.lightSweep ? "true" : "false");
  });
  document.getElementById("slider-light-size").addEventListener("input", (e) => {
    const v = parseFloat(e.target.value);
    if (state.lightType === "sun") state.sunRadiusDeg = v; else state.areaRadiusM = v;
    syncLightUi();
  });
  document.getElementById("slider-azimuth").addEventListener("input", (e) => { state.lightAzimuthDeg = parseFloat(e.target.value); syncLightUi(); });
  document.getElementById("slider-elevation").addEventListener("input", (e) => { state.lightElevationDeg = parseFloat(e.target.value); syncLightUi(); });
  document.getElementById("slider-light-dist").addEventListener("input", (e) => { state.lightDistanceM = parseFloat(e.target.value); syncLightUi(); });
  document.getElementById("slider-light-int").addEventListener("input", (e) => { state.lightIntensity = parseFloat(e.target.value); syncLightUi(); });
  document.getElementById("slider-fill-boost").addEventListener("input", (e) => { state.fillBoost = parseFloat(e.target.value); syncLightUi(); });
  document.getElementById("slider-gi-int").addEventListener("input", (e) => { state.giIntensity = parseFloat(e.target.value); syncLightUi(); });
  document.getElementById("btn-emissive-on").addEventListener("click", (e) => {
    state.emissiveOn = !state.emissiveOn;
    e.currentTarget.setAttribute("aria-pressed", state.emissiveOn ? "true" : "false");
  });
  document.querySelectorAll("[data-emissive-shadow]").forEach((btn) => btn.addEventListener("click", () => {
    state.emissiveShadowMode = btn.dataset.emissiveShadow;
    document.querySelectorAll("[data-emissive-shadow]").forEach((b) => b.setAttribute("aria-pressed", b === btn ? "true" : "false"));
  }));
  document.getElementById("slider-emissive-int").addEventListener("input", (e) => { state.emissiveIntensity = parseFloat(e.target.value); syncLightUi(); });

  document.querySelectorAll("[data-kelvin]").forEach((btn) => btn.addEventListener("click", () => {
    state.lightKelvin = btn.dataset.kelvin;
    document.querySelectorAll("[data-kelvin]").forEach((b) => b.setAttribute("aria-pressed", b === btn ? "true" : "false"));
  }));
  document.querySelectorAll("[data-quality]").forEach((btn) => btn.addEventListener("click", () => {
    state.qualityIdx = parseInt(btn.dataset.quality, 10);
    document.querySelectorAll("[data-quality]").forEach((b) => b.setAttribute("aria-pressed", b === btn ? "true" : "false"));
  }));
  document.querySelectorAll("[data-refl-bounces]").forEach((btn) => btn.addEventListener("click", () => {
    state.reflBounces = parseInt(btn.dataset.reflBounces, 10);
    document.querySelectorAll("[data-refl-bounces]").forEach((b) => b.setAttribute("aria-pressed", b === btn ? "true" : "false"));
  }));
  document.getElementById("btn-plane-filter").addEventListener("click", (e) => {
    state.planeFilter = !state.planeFilter;
    e.currentTarget.setAttribute("aria-pressed", state.planeFilter ? "true" : "false");
  });
  document.querySelectorAll("[data-sdf-mode]").forEach((btn) => btn.addEventListener("click", () => {
    state.sdfMode = btn.dataset.sdfMode;
    document.querySelectorAll("[data-sdf-mode]").forEach((b) => b.setAttribute("aria-pressed", b === btn ? "true" : "false"));
  }));
  document.querySelectorAll("[data-smap-res]").forEach((btn) => btn.addEventListener("click", () => {
    const r = parseInt(btn.dataset.smapRes, 10);
    document.querySelectorAll("[data-smap-res]").forEach((b) => b.setAttribute("aria-pressed", b === btn ? "true" : "false"));
    rebuildShadowMaps(r);
  }));
  document.getElementById("btn-half-res").addEventListener("click", (e) => {
    state.halfRes = !state.halfRes;
    e.currentTarget.setAttribute("aria-pressed", state.halfRes ? "true" : "false");
    resizeTargets(true);
  });
  document.querySelectorAll("[data-drive]").forEach((btn) => btn.addEventListener("click", () => {
    state.driveMode = btn.dataset.drive; syncDriveButtons();
  }));
  document.getElementById("btn-louvers").addEventListener("click", (e) => {
    state.showLouvers = !state.showLouvers;
    e.currentTarget.setAttribute("aria-pressed", state.showLouvers ? "true" : "false");
  });
  document.querySelectorAll("[data-cam]").forEach((btn) => btn.addEventListener("click", () => {
    state.camPreset = btn.dataset.cam;
    document.querySelectorAll("[data-cam]").forEach((b) => b.setAttribute("aria-pressed", b === btn ? "true" : "false"));
    if (state.camPreset === "hero")     { state.camYaw = -1.92; state.camPitch = 0.28; state.camDist = 7.60; }
    if (state.camPreset === "tyre")     { state.camYaw = -2.35; state.camPitch = 0.14; state.camDist = 3.70; }
    if (state.camPreset === "emissive") { state.camYaw = -1.02; state.camPitch = 0.25; state.camDist = 8.40; }
    if (state.camPreset === "penumbra") { state.camYaw = -1.55; state.camPitch = 0.62; state.camDist = 11.4; }
  }));
  document.querySelectorAll(".paint-swatch").forEach((btn) => btn.addEventListener("click", () => {
    state.paintIdx = parseInt(btn.dataset.paint, 10);
    document.querySelectorAll(".paint-swatch").forEach((b) => b.setAttribute("aria-pressed", b === btn ? "true" : "false"));
  }));

  syncAlgoMetaUi();
  syncLightUi();

  //======================================================================================================================
  // GPU TIMER QUERY RING (Measures depth prep, shadow A/B, SDF union, GI, and Reflections in ms!)
  //======================================================================================================================
  const pendingQueries = [];
  function beginTimer(tag) {
    if (!extTimer) return null;
    const q = gl.createQuery();
    gl.beginQuery(extTimer.TIME_ELAPSED_EXT, q);
    return { q, tag };
  }
  function endTimer(tok) {
    if (!extTimer || !tok) return;
    gl.endQuery(extTimer.TIME_ELAPSED_EXT);
    pendingQueries.push(tok);
  }
  function pollTimers() {
    if (!extTimer) return;
    const disjoint = gl.getParameter(extTimer.GPU_DISJOINT_EXT);
    while (pendingQueries.length > 0) {
      const top = pendingQueries[0];
      if (!gl.getQueryParameter(top.q, gl.QUERY_RESULT_AVAILABLE)) break;
      pendingQueries.shift();
      const ns = gl.getQueryParameter(top.q, gl.QUERY_RESULT);
      gl.deleteQuery(top.q);
      if (!disjoint) {
        const ms = ns * 1e-6;
        const prev = state.timings[top.tag] || 0;
        state.timings[top.tag] = prev === 0 ? ms : prev * 0.85 + ms * 0.15;
      }
    }
  }

  document.getElementById("loading-banner").style.display = "none";

  //======================================================================================================================
  // MAIN RENDER LOOP
  //======================================================================================================================
  let prevTime = performance.now(), fpsFrames = 0, fpsTimer = 0;

  function renderFrame(now) {
    const dt = Math.min(0.05, Math.max(0.0005, (now - prevTime) * 0.001));
    prevTime = now;

    if (state.lightSweep) {
      state.lightAzimuthDeg += dt * 32.0;
      if (state.lightAzimuthDeg > 180.0) state.lightAzimuthDeg -= 360.0;
      syncLightUi();
    }

    const { bodyInv, wheelInvs, bodyModel } = updateVehicleAndTlas(dt);
    const lightRig = computeMultiLightRig();

    // Camera
    const target = [state.carX, state.carY, 0.72];
    let camPos;
    if (state.camPreset === "chase") {
      const cy = Math.cos(state.carYaw), sy = Math.sin(state.carYaw);
      camPos = [state.carX - cy * 6.8, state.carY - sy * 6.8, 2.35];
    } else {
      const ce = Math.cos(state.camPitch);
      camPos = [
        target[0] + state.camDist * ce * Math.cos(state.camYaw),
        target[1] + state.camDist * ce * Math.sin(state.camYaw),
        Math.max(0.25, target[2] + state.camDist * Math.sin(state.camPitch)),
      ];
    }
    const view = mat4LookAtRH(camPos, target, [0, 0, 1]);
    const proj = mat4PerspectiveRH((44.0 * Math.PI) / 180.0, screenW / screenH, 0.15, 160.0);
    const viewProj = mat4Multiply(proj, view);
    const invViewProj = mat4InvertGeneral(viewProj);

    // 1. G-Buffer
    runGBuffer(viewProj);

    // Determine active features across Left A and Right B
    const single = state.viewLayout === "single";
    const activeShadows = single ? [state.algoA] : [state.algoA, state.algoB];
    const activeGi      = single ? [state.giA]   : [state.giA, state.giB];
    const activeRefl    = single ? [state.reflA] : [state.reflA, state.reflB];

    const needsMap = (a) => (a === "pcf" || a === "pcss" || a === "contact_pcss" || a === "smrt");
    const needsContact = (a) => (a === "contact_pcss" || a === "hybrid");
    const runMapsAny = activeShadows.some(needsMap);
    const runContactAny = activeShadows.some(needsContact);
    const runEmissiveSh = state.emissiveOn && state.emissiveShadowMode === "shadowed";
    const runEmissiveMapAny = runEmissiveSh && activeShadows.some((a) => a !== "bvh");
    const runUnionAny = activeGi.includes("sdf") || activeRefl.includes("sdf") ||
      (state.sdfMode === "clip" && activeShadows.some((a) => a === "sdf" || a === "hybrid"));

    if (runMapsAny || runContactAny || runEmissiveMapAny) {
      const tDepth = beginTimer("depthPrep");
      if (runMapsAny) {
        for (let i = 0; i < state.lightCount; i++) {
          const L = lightRig.lights[i];
          runLightDepthPass(shadowMaps[i], L.lightViewProj, L.lightOrigin, L.lightForward, L.typeInt);
        }
      }
      if (runContactAny) {
        const C = lightRig.contactRig;
        runLightDepthPass(contactMap, C.viewProj, C.origin, C.forward, lightRig.lights[0].typeInt);
      }
      if (runEmissiveMapAny) {
        runLightDepthPass(emissiveDepthMap, EMISSIVE_RIG.viewProj, EMISSIVE_RIG.shadowOrigin, EMISSIVE_RIG.shadowForward, 1, true);
      }
      endTimer(tDepth);
    } else {
      state.timings.depthPrep = 0;
    }

    if (runUnionAny) {
      ensureStaticRadiosityUpdated();
      const tUn = beginTimer("union");
      runDynamicSdfUnion(bodyModel, bodyInv, wheelInvs);
      endTimer(tUn);
    } else {
      state.timings.union = 0;
    }

    // 2. Shadow Passes (Scissor-clipped in Split view!)
    if (state.viewLayout === "split") {
      const splitPx = Math.round(state.splitX * shadowTraceW);
      const pad = state.planeFilter ? 16 : 0;
      const wA = Math.min(shadowTraceW, splitPx + pad);
      const xB = Math.max(0, splitPx - pad);
      const wB = shadowTraceW - xB;

      const tA = beginTimer("shA");
      runShadowAlgorithm(state.algoA, shadowFinalAFbo, lightRig, viewProj, bodyInv, wheelInvs, camPos, [0, 0, wA, shadowTraceH]);
      if (runEmissiveSh) runEmissiveShadowPass(state.algoA, emissiveFinalAFbo, viewProj, bodyInv, wheelInvs, camPos, [0, 0, wA, shadowTraceH]);
      endTimer(tA);

      const tB = beginTimer("shB");
      runShadowAlgorithm(state.algoB, shadowFinalBFbo, lightRig, viewProj, bodyInv, wheelInvs, camPos, [xB, 0, wB, shadowTraceH]);
      if (runEmissiveSh) runEmissiveShadowPass(state.algoB, emissiveFinalBFbo, viewProj, bodyInv, wheelInvs, camPos, [xB, 0, wB, shadowTraceH]);
      endTimer(tB);
    } else if (single) {
      const tA = beginTimer("shA");
      runShadowAlgorithm(state.algoA, shadowFinalAFbo, lightRig, viewProj, bodyInv, wheelInvs, camPos, null);
      if (runEmissiveSh) runEmissiveShadowPass(state.algoA, emissiveFinalAFbo, viewProj, bodyInv, wheelInvs, camPos, null);
      endTimer(tA);
      state.timings.shB = 0;
    } else {
      const tA = beginTimer("shA");
      runShadowAlgorithm(state.algoA, shadowFinalAFbo, lightRig, viewProj, bodyInv, wheelInvs, camPos, null);
      if (runEmissiveSh) runEmissiveShadowPass(state.algoA, emissiveFinalAFbo, viewProj, bodyInv, wheelInvs, camPos, null);
      endTimer(tA);
      const tB = beginTimer("shB");
      runShadowAlgorithm(state.algoB, shadowFinalBFbo, lightRig, viewProj, bodyInv, wheelInvs, camPos, null);
      if (runEmissiveSh) runEmissiveShadowPass(state.algoB, emissiveFinalBFbo, viewProj, bodyInv, wheelInvs, camPos, null);
      endTimer(tB);
    }

    // 3. GI Passes (Deduplicated cleanly if A and B use same GI mode)
    if (activeGi.includes("sdf")) {
      const t = beginTimer("giSdf"); runGiPass("sdf", giSdfFinalFbo); endTimer(t);
    } else state.timings.giSdf = 0;
    if (activeGi.includes("bvh")) {
      const t = beginTimer("giBvh"); runGiPass("bvh", giBvhFinalFbo); endTimer(t);
    } else state.timings.giBvh = 0;

    // 4. Reflection Passes (Deduplicated cleanly if A and B use same Reflection mode)
    if (activeRefl.includes("sdf")) {
      const t = beginTimer("reflSdf"); runReflectionPass("sdf", reflSdfFbo, viewProj, camPos, lightRig); endTimer(t);
    } else state.timings.reflSdf = 0;
    if (activeRefl.includes("bvh")) {
      const t = beginTimer("reflBvh"); runReflectionPass("bvh", reflBvhFbo, viewProj, camPos, lightRig); endTimer(t);
    } else state.timings.reflBvh = 0;

    // 5. Composite
    runComposite(invViewProj, camPos);
    pollTimers();

    fpsFrames++;
    fpsTimer += dt;
    if (fpsTimer >= 0.45) {
      const fps = fpsFrames / fpsTimer;
      document.getElementById("hud-fps-pill").textContent = `${Math.round(fps)} FPS`;

      const T = state.timings;
      const giCostOf = (m) => (m === "off" ? 0 : (m === "sdf" ? T.giSdf : T.giBvh));
      const reflCostOf = (m) => (m === "env" ? 0 : (m === "sdf" ? T.reflSdf : T.reflBvh));
      const shPrepA = (needsMap(state.algoA) || needsContact(state.algoA) || (runEmissiveSh && state.algoA !== "bvh")) ? T.depthPrep : 0;
      const shPrepB = (needsMap(state.algoB) || needsContact(state.algoB) || (runEmissiveSh && state.algoB !== "bvh")) ? T.depthPrep : 0;
      const unA = (state.giA === "sdf" || state.reflA === "sdf" || (state.sdfMode === "clip" && (state.algoA === "sdf" || state.algoA === "hybrid"))) ? T.union : 0;
      const unB = (state.giB === "sdf" || state.reflB === "sdf" || (state.sdfMode === "clip" && (state.algoB === "sdf" || state.algoB === "hybrid"))) ? T.union : 0;

      const scaleSplit = state.viewLayout === "split" ? (1.0 / Math.max(0.15, state.splitX)) : 1.0;
      const scaleSplitB = state.viewLayout === "split" ? (1.0 / Math.max(0.15, 1.0 - state.splitX)) : 1.0;

      const shFullA = T.shA * scaleSplit + shPrepA;
      const shFullB = single ? 0 : (T.shB * scaleSplitB + shPrepB);
      const giReflA = giCostOf(state.giA) + reflCostOf(state.reflA) + unA;
      const giReflB = single ? 0 : (giCostOf(state.giB) + reflCostOf(state.reflB) + unB);
      const totA = shFullA + giReflA;
      const totB = shFullB + giReflB;

      if (extTimer) {
        document.getElementById("ms-left").textContent = `${totA.toFixed(2)} ms`;
        document.getElementById("ms-right").textContent = single ? "—" : `${totB.toFixed(2)} ms`;
        document.getElementById("stat-shadow-ms").textContent = single
          ? `${shFullA.toFixed(2)} ms`
          : `${shFullA.toFixed(2)} | ${shFullB.toFixed(2)} ms`;
        document.getElementById("stat-girefl-ms").textContent = single
          ? `${giReflA.toFixed(2)} ms`
          : `${giReflA.toFixed(2)} | ${giReflB.toFixed(2)} ms`;
        document.getElementById("stat-total-ms").textContent = single
          ? `${totA.toFixed(2)} ms`
          : `${totA.toFixed(2)} | ${totB.toFixed(2)} ms`;
      } else {
        document.getElementById("ms-left").textContent = "n/a";
        document.getElementById("ms-right").textContent = "n/a";
      }

      document.getElementById("stat-rebakes").textContent =
        runUnionAny ? "0 Geometry (GI Union Only)" : "0 (Once-Baked)";
      fpsFrames = 0;
      fpsTimer = 0;
    }

    requestAnimationFrame(renderFrame);
  }

  requestAnimationFrame(renderFrame);
}

startShadowLab().catch((err) => {
  console.error(err);
  const el = document.getElementById("loading-banner");
  if (el) el.textContent = "ShadowAlgorithmLab Error: " + err.message;
});
