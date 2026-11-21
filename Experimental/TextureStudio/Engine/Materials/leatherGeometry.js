import * as THREE from "three";

// A softly flexed, finite-thickness sample. This is only a preview asset: the
// material is also tested on a flat panel and baked on an undeformed XY patch.
export function createLeatherSwatchGeometry({
  rug = false,
  rugAspect = 3.3 / 2.1,
} = {}) {
  const nx = 96,
    ny = 64,
    positions = [],
    uvs = [],
    indices = [];
  const point = (u, v, back) => {
    const x = (u - 0.5) * (rug ? 2.1 * rugAspect : 2.8),
      y = (v - 0.5) * 2.1;
    const z = rug
      ? 0.025 * Math.sin(x * 1.8) * Math.cos(y * 2) +
        0.075 * Math.pow(Math.abs(x) / (1.05 * rugAspect), 10)
      : 0.34 * Math.cos(x * 1.6) +
        0.095 * Math.sin(y * 2.7 + x * 0.7) +
        0.12 * x * y;
    return [x, y, z + (back ? -0.035 : 0)];
  };
  for (let side = 0; side < 2; side++)
    for (let j = 0; j <= ny; j++)
      for (let i = 0; i <= nx; i++) {
        const u = i / nx,
          v = j / ny;
        positions.push(...point(u, v, side));
        uvs.push(u, v);
      }
  const size = (nx + 1) * (ny + 1);
  for (let side = 0; side < 2; side++)
    for (let j = 0; j < ny; j++)
      for (let i = 0; i < nx; i++) {
        const a = side * size + j * (nx + 1) + i,
          b = a + 1,
          c = a + nx + 1,
          d = c + 1;
        if (side === 0) indices.push(a, b, c, b, d, c);
        else indices.push(a, c, b, b, c, d);
      }
  // Separate rim vertices avoid smoothing the skin normal over a cut edge.
  const edge = (a, b) => {
    const n = positions.length / 3;
    for (const id of [a, b, a + size, b + size]) {
      positions.push(...positions.slice(id * 3, id * 3 + 3));
      uvs.push(...uvs.slice(id * 2, id * 2 + 2));
    }
    indices.push(n, n + 1, n + 2, n + 1, n + 3, n + 2);
  };
  for (let i = 0; i < nx; i++) {
    edge(i + 1, i);
    edge(ny * (nx + 1) + i, ny * (nx + 1) + i + 1);
  }
  for (let j = 0; j < ny; j++) {
    edge(j * (nx + 1), (j + 1) * (nx + 1));
    edge((j + 1) * (nx + 1) + nx, j * (nx + 1) + nx);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
  g.setAttribute("uv", new THREE.Float32BufferAttribute(uvs, 2));
  g.setIndex(indices);
  g.computeVertexNormals();
  g.computeBoundingSphere();
  return g;
}
