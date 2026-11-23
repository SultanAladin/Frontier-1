import { hasDigitalPaint, paintedChannelIds } from "./textureDigitalPaint.js";
import { materials } from "./materials.js";
import { TEXTURE_PARTS } from "./textureSurfaceModel.js";
import { textureAncestors, isTextureLayerVisible } from "./textureDocument.js";
import { maskHasPaint } from "./textureMaskPaint.js";

// Derived system data, not a movable/paintable user-layer record. Old projects
// retain every authoring row (including their formerly named Base material).
export const TEXTURE_SYSTEM_BASE = Object.freeze({
  id: "alloy-system-layer-0",
  name: "Layer 0 · Mesh base",
  level: 0,
  color: "#ffffff",
  locked: true,
  hidden: true,
  editable: false,
});
const STRIDE = 66; // one surface + up to 64 decal components + one spare slot
export function textureLayerRenderOrder(doc, layerId, component = -1) {
  const index = doc.layers.findIndex((l) => l.id === layerId);
  return index < 0
    ? 0
    : 100 + (doc.layers.length - index) * STRIDE + component + 1;
}
export function textureMaterialRecipe(layer, catalog = materials) {
  if (layer.kind !== "material") return null;
  return (
    layer.surfaceMaterial ||
    (layer.sourceAsset?.type === "material"
      ? catalog.find((m) => m.id === layer.sourceAsset.id) || null
      : null)
  );
}
function maskIsWhite(owner) {
  const m = owner.mask;
  if (!m || !m.enabled || m.strength === 0) return true;
  if (maskHasPaint(m) || !["fill", "paint"].includes(m.kind)) return false;
  return (m.fill === 1) !== m.inverted;
}
export function textureLayerFullyCovers(record) {
  return (
    !record.paintOnly &&
    !(
      record.layer.paint && paintedChannelIds(record.layer).includes("opacity")
    ) &&
    record.opacity === 1 &&
    record.layer.blend === "Normal" &&
    ![5, 27, 28].includes(record.parameters.type) &&
    !(record.parameters.translucency > 0) &&
    record.ancestors.every(
      (l) =>
        maskIsWhite(l) &&
        !l.decals.some((d) => d.asMask && d.visible && d.opacity > 0),
    )
  );
}
// Topmost-first portable records -> bottom-to-top surface passes. Neither the
// currently selected row nor inspector preview state changes this evaluation.
export function evaluateTextureStack(doc, catalog = materials) {
  const surfaces = [];
  const replaced = new Set();
  for (const layer of doc.layers) {
    const native = textureMaterialRecipe(layer, catalog);
    const paintOnly =
      !native && layer.kind !== "folder" && hasDigitalPaint(layer);
    const parameters =
      native ||
      (paintOnly
        ? {
            id: "digital-paint",
            name: "Digital channel paint",
            type: 1,
            color: "#ffffff",
            roughness: 0.75,
            metalness: 0,
            coat: 0,
          }
        : null);
    if (!parameters || !isTextureLayerVisible(doc, layer.id)) continue;
    const ancestors = [...textureAncestors(doc, layer.id), layer];
    const opacity = ancestors.reduce((v, l) => (v * l.opacity) / 100, 1);
    if (opacity <= 0) continue;
    const partIds = layer.surfaceTarget
      ? [layer.surfaceTarget.partId]
      : TEXTURE_PARTS.map((p) => p.id);
    const record = {
      layer,
      parameters,
      paintOnly,
      ancestors,
      opacity,
      partIds,
      renderOrder: textureLayerRenderOrder(doc, layer.id),
    };
    surfaces.push(record);
    if (textureLayerFullyCovers(record))
      partIds.forEach((id) => replaced.add(id));
  }
  return {
    surfaces: surfaces.reverse(),
    fallbackParts: TEXTURE_PARTS.filter((p) => !replaced.has(p.id)).map(
      (p) => p.id,
    ),
    replacedParts: [...replaced],
    base: TEXTURE_SYSTEM_BASE,
    // Legacy 2D sources remain source authoring. Authored digital rows now render.
    stateOnlyLayers: doc.layers
      .filter((l) => ["paint", "fill"].includes(l.kind) && !hasDigitalPaint(l))
      .map((l) => l.id),
  };
}
