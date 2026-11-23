import { TEXTURE_BAKE_UV_SET } from "../textureMeshMaps.js";
import * as THREE from "three";
import { TeapotGeometry } from "three/addons/geometries/TeapotGeometry.js";
import { OBJLoader } from "three/addons/loaders/OBJLoader.js";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { PLYLoader } from "three/addons/loaders/PLYLoader.js";
import { STLLoader } from "three/addons/loaders/STLLoader.js";
export const BAKE_FILE_LIMIT = 30 * 1024 * 1024;
export const BAKE_TRIANGLE_LIMIT = 1000000;
const safeName = (name) =>
  String(name || "Mesh")
    .replace(/[\u0000-\u001f\u007f]/g, "")
    .slice(0, 90);

export function packTeapotUVs(geometry, segments) {
  const uv = geometry.attributes.uv,
    count = (segments + 1) ** 2;
  const chart = new Float32Array(uv.count),
    part = new Float32Array(uv.count);
  for (let i = 0; i < uv.count; i++) {
    const patch = Math.floor(i / count),
      x = patch % 8,
      y = Math.floor(patch / 8),
      g = 0.045;
    uv.setXY(
      i,
      (x + g + uv.getX(i) * (1 - 2 * g)) / 8,
      (y + g + uv.getY(i) * (1 - 2 * g)) / 4,
    );
    chart[i] = patch + 1;
    part[i] = patch;
  }
  uv.needsUpdate = true;
  geometry.setAttribute("bakeChartId", new THREE.BufferAttribute(chart, 1));
  geometry.setAttribute("bakePartId", new THREE.BufferAttribute(part, 1));
  geometry.userData.textureBakeUVSet = TEXTURE_BAKE_UV_SET;
  geometry.userData.teapotSegments = segments;
  return geometry;
}
function packedBox(segments = 1) {
  const g = new THREE.BoxGeometry(2, 2, 2, segments, segments, segments),
    uv = g.attributes.uv,
    count = (segments + 1) ** 2,
    chart = new Float32Array(uv.count),
    parts = new Float32Array(uv.count);
  for (let i = 0; i < uv.count; i++) {
    const face = Math.floor(i / count),
      x = face % 3,
      y = Math.floor(face / 3);
    uv.setXY(
      i,
      (x + 0.06 + uv.getX(i) * 0.88) / 3,
      (y + 0.06 + uv.getY(i) * 0.88) / 2,
    );
    chart[i] = face + 1;
    parts[i] = face;
  }
  g.setAttribute("bakeChartId", new THREE.BufferAttribute(chart, 1));
  g.setAttribute("bakePartId", new THREE.BufferAttribute(parts, 1));
  return g;
}
export function createDemoBakeAssets(
  shape = "Teapot",
  { textureTarget = false } = {},
) {
  const segments = textureTarget ? 20 : 8;
  const low =
    shape === "Cube"
      ? packedBox(1)
      : shape === "Sphere"
        ? new THREE.SphereGeometry(1.05, 16, 12)
        : packTeapotUVs(new TeapotGeometry(1.05, segments), segments);
  const high =
    shape === "Cube"
      ? packedBox(1)
      : shape === "Sphere"
        ? new THREE.SphereGeometry(1.05, 64, 48)
        : packTeapotUVs(new TeapotGeometry(1.05, 24), 24);
  if (shape === "Sphere") {
    // Three's seam-duplicated pole UVs can extend by half a segment beyond
    // 0–1. Keep the built-in target in one tile; imported UVs remain untouched.
    const uv = low.attributes.uv;
    for (let i = 0; i < uv.count; i++)
      uv.setX(i, Math.max(0, Math.min(1, uv.getX(i))));
    uv.needsUpdate = true;
  }
  return {
    low: [{ name: shape, geometry: low }],
    high: [{ name: shape, geometry: high }],
    label: `${shape} study`,
    source: "Built-in geometry",
  };
}
export function disposeBakeParts(parts) {
  parts?.forEach((p) => p.geometry.dispose());
}
export function bakeMeshData(geometry, partId = 0) {
  const position = geometry.attributes.position;
  if (!position || position.count < 3)
    throw new Error("The mesh has no triangle positions.");
  const indices = geometry.index
    ? new Uint32Array(geometry.index.array)
    : Uint32Array.from({ length: position.count }, (_, i) => i);
  if (indices.length % 3 || indices.length / 3 > BAKE_TRIANGLE_LIMIT)
    throw new Error(
      `Meshes must be triangulated and stay below ${BAKE_TRIANGLE_LIMIT.toLocaleString()} triangles.`,
    );
  if (
    !geometry.attributes.normal ||
    geometry.attributes.normal.count !== position.count
  )
    geometry.computeVertexNormals();
  const attribute = (name, size, fallback = 0) => {
    const a = geometry.attributes[name],
      out = new Float32Array(position.count * size);
    for (let i = 0; i < position.count; i++)
      for (let c = 0; c < size; c++) {
        const v = a ? a.getComponent(i, c) : fallback;
        if (!Number.isFinite(v))
          throw new Error(`Mesh ${name} data contains a non-finite value.`);
        out[i * size + c] = v;
      }
    return out;
  };
  for (const i of indices)
    if (i >= position.count)
      throw new Error("Mesh indices reference a missing vertex.");
  return {
    objectToWorld:
      geometry.userData.bakeObjectToWorld || new THREE.Matrix4().toArray(),
    positions: attribute("position", 3),
    normals: attribute("normal", 3),
    indices,
    uvs: geometry.attributes.uv ? attribute("uv", 2) : null,
    colors: geometry.attributes.color ? attribute("color", 3, 1) : null,
    // Object identity is not a face/UV-chart identity. A built-in teapot or
    // cube is one source object even though it has many UV islands.
    partIds: new Float32Array(position.count).fill(partId),
    chartIds: geometry.attributes.bakeChartId
      ? attribute("bakeChartId", 1)
      : null,
  };
}
export function mergeBakeMeshData(parts, objectIds = []) {
  const data = parts.map((p, i) => bakeMeshData(p.geometry, objectIds[i] ?? i));
  let vertices = 0,
    indexCount = 0;
  data.forEach((d) => {
    vertices += d.positions.length / 3;
    indexCount += d.indices.length;
  });
  if (indexCount / 3 > BAKE_TRIANGLE_LIMIT)
    throw new Error(
      "The combined high meshes exceed the one-million-triangle limit.",
    );
  const positions = new Float32Array(vertices * 3),
    normals = new Float32Array(vertices * 3),
    colors = new Float32Array(vertices * 3).fill(1),
    partIds = new Float32Array(vertices),
    indices = new Uint32Array(indexCount);
  let v = 0,
    t = 0;
  for (const d of data) {
    positions.set(d.positions, v * 3);
    normals.set(d.normals, v * 3);
    if (d.colors) colors.set(d.colors, v * 3);
    partIds.set(d.partIds, v);
    for (let i = 0; i < d.indices.length; i++)
      indices[t + i] = d.indices[i] + v;
    v += d.positions.length / 3;
    t += d.indices.length;
  }
  return { positions, normals, colors, partIds, indices };
}
function stripGltfResources(json) {
  const extensions = json.extensionsRequired || [];
  if (
    extensions.some((e) =>
      ["KHR_draco_mesh_compression", "EXT_meshopt_compression"].includes(e),
    )
  )
    throw new Error(
      "Compressed glTF meshes need a decoder not included here. Export uncompressed GLB/OBJ instead.",
    );
  for (const buffer of json.buffers || [])
    if (
      buffer.uri &&
      !/^data:application\/(?:octet-stream|gltf-buffer);base64,/i.test(
        buffer.uri,
      )
    )
      throw new Error(
        "glTF must embed its buffers. Use a self-contained GLB; external URLs are not fetched.",
      );
  // Geometry-only import: no HTTP requests, image decoding, or material texture allocations.
  delete json.images;
  delete json.textures;
  delete json.materials;
  delete json.samplers;
  for (const mesh of json.meshes || [])
    for (const p of mesh.primitives || []) delete p.material;
  return json;
}
function geometryOnlyGLB(buffer) {
  const data = new DataView(buffer);
  if (
    buffer.byteLength < 20 ||
    data.getUint32(0, true) !== 0x46546c67 ||
    data.getUint32(4, true) !== 2
  )
    throw new Error("Choose a valid glTF 2.0 binary file.");
  let offset = 12,
    json = null,
    binary = null;
  while (offset + 8 <= buffer.byteLength) {
    const length = data.getUint32(offset, true),
      type = data.getUint32(offset + 4, true);
    offset += 8;
    if (offset + length > buffer.byteLength)
      throw new Error("The GLB has an invalid chunk size.");
    if (type === 0x4e4f534a)
      json = JSON.parse(
        new TextDecoder().decode(new Uint8Array(buffer, offset, length)).trim(),
      );
    if (type === 0x004e4942) binary = new Uint8Array(buffer, offset, length);
    offset += length;
  }
  if (!json) throw new Error("The GLB has no scene description.");
  const bytes = new TextEncoder().encode(
      JSON.stringify(stripGltfResources(json)),
    ),
    length = Math.ceil(bytes.length / 4) * 4,
    total = 12 + 8 + length + (binary ? 8 + binary.length : 0),
    out = new ArrayBuffer(total),
    view = new DataView(out),
    u8 = new Uint8Array(out);
  view.setUint32(0, 0x46546c67, true);
  view.setUint32(4, 2, true);
  view.setUint32(8, total, true);
  view.setUint32(12, length, true);
  view.setUint32(16, 0x4e4f534a, true);
  u8.fill(32, 20, 20 + length);
  u8.set(bytes, 20);
  if (binary) {
    view.setUint32(20 + length, binary.length, true);
    view.setUint32(24 + length, 0x004e4942, true);
    u8.set(binary, 28 + length);
  }
  return out;
}
export async function importBakeMesh(file, role) {
  if (file.size > BAKE_FILE_LIMIT)
    throw new Error("Mesh files must be smaller than 30 MB.");
  const extension = file.name.split(".").at(-1).toLowerCase();
  let root;
  if (extension === "obj") root = new OBJLoader().parse(await file.text());
  else if (extension === "glb" || extension === "gltf") {
    const source =
      extension === "glb"
        ? geometryOnlyGLB(await file.arrayBuffer())
        : JSON.stringify(stripGltfResources(JSON.parse(await file.text())));
    root = (await new GLTFLoader().parseAsync(source, "")).scene;
  } else if (extension === "ply")
    root = new THREE.Mesh(new PLYLoader().parse(await file.arrayBuffer()));
  else if (extension === "stl")
    root = new THREE.Mesh(new STLLoader().parse(await file.arrayBuffer()));
  else
    throw new Error(
      "Supported mesh files: OBJ, self-contained GLB/glTF, PLY and STL (high mesh only).",
    );
  const parts = [];
  try {
    root.updateMatrixWorld(true);
    root.traverse((object) => {
      if (!object.isMesh) return;
      if (object.isInstancedMesh)
        throw new Error(
          "Realize mesh instances before export. Instanced geometry is not silently reduced to its first instance.",
        );
      if (object.isSkinnedMesh)
        throw new Error(
          "Skinned meshes must be exported as static posed geometry before baking.",
        );
      const geometry = object.geometry.clone();
      geometry.applyMatrix4(object.matrixWorld);
      geometry.userData.bakeObjectToWorld = object.matrixWorld.toArray();
      if (!geometry.attributes.normal) geometry.computeVertexNormals();
      const name = safeName(object.name || file.name.replace(/\.[^.]+$/, ""));
      try {
        bakeMeshData(geometry, parts.length);
        if (role === "low" && !geometry.attributes.uv)
          throw new Error(
            `${name} has no UV0 coordinates. Low meshes require a non-overlapping 0–1 UV layout.`,
          );
        parts.push({ name, geometry });
      } catch (e) {
        geometry.dispose();
        throw e;
      }
    });
    if (!parts.length) throw new Error("The file contains no triangle meshes.");
    const total = parts.reduce(
      (n, p) =>
        n +
        (p.geometry.index?.count || p.geometry.attributes.position.count) / 3,
      0,
    );
    if (total > BAKE_TRIANGLE_LIMIT)
      throw new Error("The imported geometry exceeds one million triangles.");
    if (parts.length > 16)
      throw new Error("Choose an asset with up to 16 mesh objects per import.");
    return parts;
  } catch (e) {
    disposeBakeParts(parts);
    throw e;
  } finally {
    root.traverse((object) => {
      object.geometry?.dispose();
      const materials = Array.isArray(object.material)
        ? object.material
        : [object.material];
      for (const m of materials) {
        if (!m) continue;
        Object.values(m).forEach((v) => {
          if (v?.isTexture) v.dispose();
        });
        m.dispose();
      }
    });
  }
}
export function meshSummary(parts) {
  return {
    objects: parts.length,
    triangles: parts.reduce(
      (n, p) =>
        n +
        (p.geometry.index?.count || p.geometry.attributes.position.count) / 3,
      0,
    ),
    vertices: parts.reduce(
      (n, p) => n + p.geometry.attributes.position.count,
      0,
    ),
    uv: parts.every((p) => !!p.geometry.attributes.uv),
  };
}
