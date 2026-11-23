import {
  AUTHORING_UV_SET,
  TEXTURE_PARTS,
  normalizeSurfaceTarget,
} from "./textureSurfaceModel.js";
import {
  normalizeStampChannels,
  normalizeStampValues,
  normalizeStampPreset,
  stampId,
  stampName,
  stampNumber,
} from "./stampDocument.js";
export const DECAL_LIMIT = 64;
export const DECAL_STAMP_LIMIT = 16;
export const DECAL_SPACE = "object-sphere";
function vector(raw, fallback, min, max) {
  return fallback.map((v, i) =>
    stampNumber(Array.isArray(raw) ? raw[i] : v, v, min, max),
  );
}
export function normalizeDecal(raw) {
  if (
    !raw ||
    typeof raw.id !== "string" ||
    typeof raw.stampId !== "string" ||
    !/^[\w-]{1,80}$/.test(raw.id) ||
    !/^[\w-]{1,80}$/.test(raw.stampId)
  )
    throw new Error("A decal has an invalid component or stamp identity.");
  if (raw.space && ![DECAL_SPACE, "uv"].includes(raw.space))
    throw new Error("Decals use object-space or authoring UV coordinates.");
  const isUV = raw.space === "uv";
  if (
    isUV &&
    (raw.uvSet !== AUTHORING_UV_SET ||
      !Array.isArray(raw.uvPosition) ||
      raw.uvPosition.length !== 2 ||
      raw.uvPosition.some(
        (v, i) => !Number.isFinite(v) || v < 0 || v > (i === 0 ? 10 : 100),
      ))
  )
    throw new Error(
      "A UV decal must use the authoring UV set and finite UDIM coordinates.",
    );
  const target = raw.target
    ? normalizeSurfaceTarget(raw.target)
    : isUV
      ? normalizeSurfaceTarget({
          objectId: "teapot",
          partId: TEXTURE_PARTS.find(
            (p) => p.u === Math.floor(raw.uvPosition[0]),
          )?.id,
        })
      : null;
  if (
    isUV &&
    (raw.uvPosition[0] < TEXTURE_PARTS.find((p) => p.id === target.partId).u ||
      raw.uvPosition[0] >=
        TEXTURE_PARTS.find((p) => p.id === target.partId).u + 1 ||
      raw.uvPosition[1] > 1)
  )
    throw new Error(
      "A UV decal centre must stay within its part's authoring tile.",
    );
  let quaternion = vector(raw.quaternion, [0, 0, 0, 1], -1, 1);
  const length = Math.hypot(...quaternion);
  quaternion =
    length > 1e-8
      ? Math.abs(length - 1) < 1e-9
        ? quaternion
        : quaternion.map((v) => v / length)
      : [0, 0, 0, 1];
  return {
    id: raw.id,
    stampId: raw.stampId,
    name: stampName(raw.name, "Decal component"),
    space: isUV ? "uv" : DECAL_SPACE,
    ...(target ? { target } : {}),
    ...(isUV
      ? { uvSet: AUTHORING_UV_SET, uvPosition: [...raw.uvPosition] }
      : {}),
    visible: raw.visible !== false,
    locked: raw.locked === true,
    position: vector(raw.position, [0, 0, 1], -4, 4),
    quaternion,
    size: vector(
      raw.size,
      isUV ? [0.18, 0.18] : [0.65, 0.65],
      isUV ? 0.001 : 0.01,
      4,
    ),
    depth: stampNumber(raw.depth ?? 0.35, 0.35, 0.01, 2),
    roll: stampNumber(raw.roll ?? 0, 0, -180, 180),
    opacity: stampNumber(raw.opacity ?? 1, 1, 0, 1),
    asMask: raw.asMask === true,
    inverted: raw.inverted === true,
    channels: normalizeStampChannels(raw.channels),
    channelValues: normalizeStampValues(raw.channelValues),
  };
}
export function normalizeLayerDecals(raw) {
  if (raw == null) return [];
  if (!Array.isArray(raw) || raw.length > DECAL_LIMIT)
    throw new Error(
      `A project can have at most ${DECAL_LIMIT} decal components.`,
    );
  const decals = raw.map(normalizeDecal);
  if (new Set(decals.map((d) => d.id)).size !== decals.length)
    throw new Error("Decal component identities must be unique.");
  return decals;
}
export function normalizeDocumentStamps(raw) {
  if (raw == null) return [];
  if (!Array.isArray(raw) || raw.length > DECAL_STAMP_LIMIT)
    throw new Error(
      `A texture project can contain at most ${DECAL_STAMP_LIMIT} distinct stamp snapshots.`,
    );
  const stamps = raw.map(normalizeStampPreset);
  if (new Set(stamps.map((s) => s.id)).size !== stamps.length)
    throw new Error("Stamp snapshot identities must be unique.");
  return stamps;
}
export function createDecal(stamp, placement = {}) {
  return normalizeDecal({
    name: stamp.name,
    channels: stamp.project.channels,
    channelValues: stamp.project.channelValues,
    ...placement,
    id: stampId(),
    stampId: stamp.id,
  });
}
export function collectDecalRecords(doc, visible = () => true) {
  const stamps = new Map((doc.stamps || []).map((stamp) => [stamp.id, stamp]));
  return doc.layers.flatMap((layer, index) => {
    if (!visible(doc, layer.id)) return [];
    return (layer.decals || [])
      .filter(
        (d) => d.visible && d.opacity > 0 && (d.asMask || d.channels.length),
      )
      .map((decal, componentIndex) => ({
        layerId: layer.id,
        layerIndex: index,
        componentIndex,
        layer,
        decal,
        stamp: stamps.get(decal.stampId),
      }))
      .filter((r) => r.stamp);
  });
}
