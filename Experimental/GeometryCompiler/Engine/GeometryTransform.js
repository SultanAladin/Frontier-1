// Rigid-plus-scale transform for source geometry, applied before the cluster hierarchy is built.
// Order: scale, mirror, rotate (X, then Y, then Z), centre, then flip normals. A mirror reverses winding because it
// reflects the surface, so the stored triangle order is corrected to keep outward normals.

export const CENTRE_MODES = ['none', 'origin', 'floor'];

export function identityTransform() {
  return { scale: [1, 1, 1], mirror: [false, false, false], rotate: [0, 0, 0], centre: 'none', flipNormals: false };
}

export function isIdentityTransform(t) {
  return t.scale.every((s) => s === 1) && !t.mirror.some(Boolean) && t.rotate.every((r) => r === 0) &&
    t.centre === 'none' && !t.flipNormals;
}

// Clamped so a zero or negative scale cannot collapse the mesh.
const MIN_SCALE = 1e-3;

function rotationMatrix(degrees) {
  const [ax, ay, az] = degrees.map((d) => (d * Math.PI) / 180);
  const [cx, sx] = [Math.cos(ax), Math.sin(ax)];
  const [cy, sy] = [Math.cos(ay), Math.sin(ay)];
  const [cz, sz] = [Math.cos(az), Math.sin(az)];
  // R = Rz * Ry * Rx, row-major 3x3.
  const rx = [1, 0, 0, 0, cx, -sx, 0, sx, cx];
  const ry = [cy, 0, sy, 0, 1, 0, -sy, 0, cy];
  const rz = [cz, -sz, 0, sz, cz, 0, 0, 0, 1];
  return multiply3(rz, multiply3(ry, rx));
}

function multiply3(a, b) {
  const out = new Array(9).fill(0);
  for (let r = 0; r < 3; r++) for (let c = 0; c < 3; c++) {
    for (let k = 0; k < 3; k++) out[r * 3 + c] += a[r * 3 + k] * b[k * 3 + c];
  }
  return out;
}

// Returns new arrays; the inputs are never modified.
export function transformGeometry(positions, indices, transform) {
  const t = { ...identityTransform(), ...transform };
  const n = positions.length / 3;
  const scale = t.scale.map((s) => Math.max(MIN_SCALE, Math.abs(s)));
  const sign = t.mirror.map((m) => (m ? -1 : 1));
  const R = rotationMatrix(t.rotate);
  const out = new Float32Array(positions.length);
  for (let i = 0; i < n; i++) {
    const x = positions[i * 3] * scale[0] * sign[0];
    const y = positions[i * 3 + 1] * scale[1] * sign[1];
    const z = positions[i * 3 + 2] * scale[2] * sign[2];
    out[i * 3] = R[0] * x + R[1] * y + R[2] * z;
    out[i * 3 + 1] = R[3] * x + R[4] * y + R[5] * z;
    out[i * 3 + 2] = R[6] * x + R[7] * y + R[8] * z;
  }
  if (t.centre !== 'none' && n > 0) {
    const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
    for (let i = 0; i < n; i++) for (let k = 0; k < 3; k++) {
      lo[k] = Math.min(lo[k], out[i * 3 + k]);
      hi[k] = Math.max(hi[k], out[i * 3 + k]);
    }
    const offset = t.centre === 'origin'
      ? [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2]
      : [(lo[0] + hi[0]) / 2, lo[1], (lo[2] + hi[2]) / 2];
    for (let i = 0; i < n; i++) for (let k = 0; k < 3; k++) out[i * 3 + k] -= offset[k];
  }
  // A mirror (odd count) reverses winding; flipNormals reverses it again.
  const reversed = (sign.filter((s) => s < 0).length % 2 === 1) !== t.flipNormals;
  const idx = new Uint32Array(indices.length);
  for (let f = 0; f + 2 < indices.length; f += 3) {
    if (reversed) { idx[f] = indices[f]; idx[f + 1] = indices[f + 2]; idx[f + 2] = indices[f + 1]; }
    else { idx[f] = indices[f]; idx[f + 1] = indices[f + 1]; idx[f + 2] = indices[f + 2]; }
  }
  return { positions: out, indices: idx };
}
