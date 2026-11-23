import * as THREE from "three";
import drape from "./assets/draped-cloth.json";

export const clothSupport = drape.sphere;

export function createDrapedClothGeometry() {
  const n = drape.grid,
    positions = [...drape.positions],
    uvs = [],
    indices = [];
  for (let row = 0; row < n; row++)
    for (let col = 0; col < n; col++) {
      uvs.push(col / (n - 1), 1 - row / (n - 1));
      const i = row * n + col;
      if (row < n - 1 && col < n - 1)
        indices.push(i, i + n, i + 1, i + 1, i + n, i + n + 1);
    }
  const mesh = new THREE.BufferGeometry();
  mesh.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
  mesh.setIndex(indices);
  mesh.computeVertexNormals();
  const normals = mesh.getAttribute("normal");
  // A narrow physical edge gives the sheet a cloth-like thickness in macro view.
  const boundary = [];
  for (let col = 0; col < n; col++) boundary.push(col);
  for (let row = 1; row < n; row++) boundary.push(row * n + n - 1);
  for (let col = n - 2; col >= 0; col--) boundary.push((n - 1) * n + col);
  for (let row = n - 2; row > 0; row--) boundary.push(row * n);
  const start = positions.length / 3;
  for (const i of boundary) {
    positions.push(
      positions[i * 3],
      positions[i * 3 + 1],
      positions[i * 3 + 2],
    );
    positions.push(
      positions[i * 3] - normals.getX(i) * 0.003,
      positions[i * 3 + 1] - normals.getY(i) * 0.003,
      positions[i * 3 + 2] - normals.getZ(i) * 0.003,
    );
    uvs.push(uvs[i * 2], uvs[i * 2 + 1], uvs[i * 2], uvs[i * 2 + 1]);
  }
  for (let j = 0; j < boundary.length; j++) {
    const a = start + j * 2,
      b = start + ((j + 1) % boundary.length) * 2;
    indices.push(a, a + 1, b, b, a + 1, b + 1);
  }
  mesh.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
  mesh.setAttribute("uv", new THREE.Float32BufferAttribute(uvs, 2));
  mesh.setIndex(indices);
  mesh.deleteAttribute("normal");
  mesh.computeVertexNormals();
  mesh.computeBoundingBox();
  mesh.computeBoundingSphere();
  mesh.userData.frozen = true;
  mesh.userData.source = "Baked position-based cloth simulation";
  return mesh;
}
