export const TEXTURE_MESH_MAP_SCHEMA = "alloy.texture-mesh-support.v1";
export const TEXTURE_BAKE_UV_SET = "teapot-bake-8x4-v1";
export const TEXTURE_MESH_MAP_LIMIT = 700000;
export const TEXTURE_MESH_MAP_ROLES = ["occlusion", "normal", "bevel-normal"];

// Baked support is non-color mesh data, not an editable white/color layer.
// Keep the existing 1 MB project limit; reject oversized bundles atomically.
export function normalizeTextureMeshMaps(raw) {
  if (raw == null) return null;
  if (
    !raw ||
    raw.schema !== TEXTURE_MESH_MAP_SCHEMA ||
    raw.target !== "teapot" ||
    raw.uvSet !== TEXTURE_BAKE_UV_SET ||
    !raw.maps ||
    typeof raw.maps !== "object" ||
    Array.isArray(raw.maps)
  )
    throw new Error(
      "Mesh support must use the built-in teapot's 32-chart bake layout.",
    );
  const maps = {};
  for (const [role, source] of Object.entries(raw.maps)) {
    if (!TEXTURE_MESH_MAP_ROLES.includes(role))
      throw new Error(
        "Only baked AO, tangent normal and bevel normal are mesh-support inputs.",
      );
    if (
      !source ||
      typeof source.dataUrl !== "string" ||
      source.dataUrl.length > TEXTURE_MESH_MAP_LIMIT ||
      !/^data:image\/png;base64,[A-Za-z0-9+/]+={0,2}$/.test(source.dataUrl)
    )
      throw new Error("Mesh support requires bounded, embedded PNG images.");
    const width = source.width,
      height = source.height;
    if (
      !Number.isInteger(width) ||
      width !== height ||
      width < 16 ||
      width > 1024 ||
      width & (width - 1)
    )
      throw new Error(
        "Mesh support maps use square power-of-two images from 16 to 1024 pixels.",
      );
    const bytes = Uint8Array.from(
      atob(source.dataUrl.split(",")[1].slice(0, 64)),
      (c) => c.charCodeAt(0),
    );
    if (
      bytes.length < 24 ||
      ![137, 80, 78, 71, 13, 10, 26, 10].every((v, i) => bytes[i] === v) ||
      String.fromCharCode(...bytes.slice(12, 16)) !== "IHDR"
    )
      throw new Error("Mesh support PNG header is invalid.");
    const view = new DataView(bytes.buffer);
    if (view.getUint32(16) !== width || view.getUint32(20) !== height)
      throw new Error("Mesh support dimensions do not match the embedded PNG.");
    const normalY = role === "occlusion" ? undefined : source.normalY || "+Y";
    if (normalY && !["+Y", "-Y"].includes(normalY))
      throw new Error("Choose OpenGL +Y or DirectX -Y tangent normals.");
    maps[role] = {
      dataUrl: source.dataUrl,
      width,
      height,
      colorSpace: "linear",
      ...(normalY ? { normalY } : {}),
    };
  }
  if (!Object.keys(maps).length)
    throw new Error("Mesh support needs at least one baked map.");
  const result = {
    schema: TEXTURE_MESH_MAP_SCHEMA,
    target: "teapot",
    uvSet: TEXTURE_BAKE_UV_SET,
    maps,
  };
  if (JSON.stringify(result).length > TEXTURE_MESH_MAP_LIMIT)
    throw new Error(
      "Mesh support exceeds its 700 KB portable budget. Bake at 512 or a smaller resolution.",
    );
  return result;
}
// Exact correspondence of authoring UVs to Baking's existing 8 x 4 patch atlas.
// No stretched UDIM guess and no changes to original UV0 or audited bake data.
export function textureBakeUV(authoringUV, partIndex) {
  const columns = partIndex <= 2 ? 4 : 2,
    rows = partIndex === 1 ? 4 : 2;
  const x = (authoringUV[0] - partIndex + 1) * columns,
    y = authoringUV[1] * rows;
  const local = Math.floor(y) * columns + Math.floor(x);
  const patch =
    partIndex === 1
      ? local < 12
        ? local
        : local + 16
      : partIndex === 2
        ? local + 20
        : partIndex === 3
          ? local + 12
          : local + 16;
  return [
    ((patch % 8) + x - Math.floor(x)) / 8,
    (Math.floor(patch / 8) + y - Math.floor(y)) / 4,
  ];
}
export function reorientTextureNormal(base, detail) {
  const t = [base[0], base[1], base[2] + 1],
    u = [-detail[0], -detail[1], detail[2]],
    dot = t.reduce((v, n, i) => v + n * u[i], 0);
  const value = t.map((n, i) => (n * dot) / Math.max(t[2], 1e-6) - u[i]);
  const length = Math.hypot(...value) || 1;
  return value.map((v) => v / length);
}
