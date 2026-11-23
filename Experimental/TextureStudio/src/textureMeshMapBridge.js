import {
  TEXTURE_DRAFT_KEY,
  createTextureDocument,
  parseTextureDocument,
  validateTextureDocument,
} from "./textureDocument.js";
import {
  TEXTURE_BAKE_UV_SET,
  TEXTURE_MESH_MAP_SCHEMA,
  TEXTURE_MESH_MAP_ROLES,
  normalizeTextureMeshMaps,
} from "./textureMeshMaps.js";

// Only the built-in teapot's chart-tagged low geometry is compatible. Imported
// meshes with coincidentally similar names are never auto-attached to Texture.
export function isTextureBakeTarget(target) {
  return (
    target?.name === "Teapot" &&
    target.geometry?.userData?.textureBakeUVSet === TEXTURE_BAKE_UV_SET &&
    target.geometry?.userData?.teapotSegments === 20
  );
}
export async function textureMeshMapsFromBake(result) {
  const maps = {};
  for (const role of TEXTURE_MESH_MAP_ROLES) {
    const pixels = result.maps[role];
    if (!pixels) continue;
    const resolution = result.settings.resolution;
    if (resolution > 1024)
      throw new Error(
        "Texture mesh support is bounded to 1024 pixels; bake at 512 for portable projects.",
      );
    const canvas = document.createElement("canvas");
    canvas.width = canvas.height = resolution;
    canvas
      .getContext("2d")
      .putImageData(new ImageData(pixels, resolution, resolution), 0, 0);
    maps[role] = {
      dataUrl: canvas.toDataURL("image/png"),
      width: resolution,
      height: resolution,
      ...(role !== "occlusion" ? { normalY: result.settings.normalY } : {}),
    };
  }
  if (!Object.keys(maps).length) return null;
  return normalizeTextureMeshMaps({
    schema: TEXTURE_MESH_MAP_SCHEMA,
    target: "teapot",
    uvSet: TEXTURE_BAKE_UV_SET,
    maps,
  });
}
export function attachTextureMeshMaps(doc, snapshot) {
  const input = normalizeTextureMeshMaps(snapshot);
  if (!input) return doc;
  return validateTextureDocument({
    ...doc,
    meshMaps: normalizeTextureMeshMaps({
      ...input,
      maps: { ...doc.meshMaps?.maps, ...input.maps },
    }),
  });
}
export function publishTextureMeshMaps(snapshot, storage = localStorage) {
  const source = storage.getItem(TEXTURE_DRAFT_KEY);
  const doc = source ? parseTextureDocument(source) : createTextureDocument();
  const next = attachTextureMeshMaps(doc, snapshot);
  storage.setItem(TEXTURE_DRAFT_KEY, JSON.stringify(next));
  return next;
}
