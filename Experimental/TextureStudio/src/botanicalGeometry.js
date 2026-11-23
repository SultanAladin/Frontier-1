import * as THREE from "three";

// True silhouette meshes, not alpha cards. U crosses the blade; V runs base -> tip.
export function createBotanicalGeometry(kind = "Leaf", ribCount = 10) {
  if (kind === "Stem")
    return new THREE.CylinderGeometry(0.3, 0.34, 2.7, 96, 48);
  const nx = kind === "Cactus" ? 160 : 40,
    ny = 100,
    positions = [],
    uvs = [],
    indices = [];
  for (let j = 0; j <= ny; j++)
    for (let i = 0; i <= nx; i++) {
      const u = i / nx,
        v = j / ny,
        x = u * 2 - 1,
        s = Math.max(0, Math.sin(Math.PI * v));
      let width = 0.78 * Math.pow(s, 0.8) + 0.002,
        y = (v - 0.5) * 2.7;
      let z =
        0.12 * Math.sin(v * Math.PI) -
        0.13 * Math.abs(x) * s +
        0.05 * x * x * Math.sin(v * 8);
      let lean = 0.06 * Math.sin(v * 5);
      if (kind === "Petal") {
        width = 0.94 * Math.sqrt(s) * (0.4 + 0.6 * v) + 0.002;
        z =
          0.19 * x * x * s -
          0.12 * Math.sin(v * Math.PI) +
          0.025 * Math.sin(v * 35) * Math.pow(x, 4) * s;
        lean = 0;
      }
      if (kind === "Grass blade") {
        width = 0.17 * Math.pow(1 - v, 0.65) + 0.001;
        y = (v - 0.5) * 3;
        z = 0.18 * v * v + 0.055 * x * x;
        lean = 0.23 * v * v;
      }
      if (kind === "Cactus") {
        const a = u * Math.PI * 2;
        const radius =
          (0.68 * Math.pow(s, 0.32) + 0.002) *
          (0.87 + 0.13 * Math.cos(a * ribCount - Math.PI));
        positions.push(Math.sin(a) * radius, y, Math.cos(a) * radius);
      } else positions.push(x * width + lean, y, z);
      uvs.push(u, v);
      if (i < nx && j < ny) {
        const a = j * (nx + 1) + i,
          b = a + 1,
          c = a + nx + 1,
          d = c + 1;
        indices.push(a, b, c, b, d, c);
      }
    }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute(
    "position",
    new THREE.Float32BufferAttribute(positions, 3),
  );
  geometry.setAttribute("uv", new THREE.Float32BufferAttribute(uvs, 2));
  geometry.setIndex(indices);
  geometry.computeVertexNormals();
  return geometry;
}
