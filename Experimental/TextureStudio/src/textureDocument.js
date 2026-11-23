import { normalizeDigitalBrush } from "./digitalBrush.js";
import {
  normalizeDigitalPaint,
  validateDigitalPaintGeometry,
  normalizeLastDigitalStroke,
  paintedChannelIds,
  PAINT_CHANNEL_SLICE_LIMIT,
} from "./textureDigitalPaint.js";
import { normalizeTextureMeshMaps } from "./textureMeshMaps.js";
import { validateMaskPaintGeometry } from "./textureMaskPaint.js";
import {
  normalizeSurfaceTarget,
  normalizeSurfaceMaterial,
  texturePart,
  createTextureTeapotGeometry,
} from "./textureSurfaceModel.js";
import {
  normalizeLayerDecals,
  normalizeDocumentStamps,
  createDecal,
  DECAL_LIMIT,
} from "./decalModel.js";
import { normalizeStampPreset, stampId } from "./stampDocument.js";
import { normalizeSourceStroke } from "./textureStrokes.js";
import { normalizeTextureMask } from "./textureMasks.js";
import {
  PAINT_CHANNELS,
  normalizeChannelSettings,
} from "./paintChannelModel.js";
import { normalizePaintSettings } from "./paintToolCatalogue.js";
// Portable Texture document: ordered live material/decal passes and bounded
// mask operations and replayable digital UV/channel strokes.
export const TEXTURE_SCHEMA = "alloy.texture.v1";
export const TEXTURE_DRAFT_KEY = "alloy-texture-project-v1";
export const TEXTURE_LAYER_LIMIT = 64;
export const TEXTURE_FOLDER_DEPTH_LIMIT = 8;
export const TEXTURE_FILE_LIMIT = 1024 * 1024;
export const TEXTURE_KINDS = {
  folder: {
    label: "Folder",
    description:
      "A layer group with its own blend, opacity and optional coverage mask.",
    color: "#bfac88",
  },
  decal: {
    label: "Decal",
    description:
      "A surface-projected stamp layer containing multiple decal components.",
    color: "#c4aa7a",
  },
  paint: {
    label: "Paint",
    description:
      "A sparse digital paint layer with editable UV strokes and per-channel values.",
    color: "#d79564",
  },
  fill: {
    label: "Fill",
    description:
      "Digital channel fills, scoped to real faces, islands or mesh components.",
    color: "#779cd1",
  },
  material: {
    label: "Material",
    description:
      "A native material snapshot covering the whole object or one part.",
    color: "#aa98d0",
  },
  generator: {
    label: "Generator",
    description: "A layer for a future procedural source.",
    color: "#78ab96",
  },
};
export const TEXTURE_BLENDS = [
  "Normal",
  "Multiply",
  "Screen",
  "Overlay",
  "Add",
  "Darken",
];
export const TEXTURE_CHANNELS = PAINT_CHANNELS;
const channelIds = new Set(TEXTURE_CHANNELS.map((c) => c.id));
let token = 0;
function nextId() {
  return (
    globalThis.crypto?.randomUUID?.() ||
    `texture-${Date.now().toString(36)}-${++token}`
  );
}
function text(value, fallback, limit = 80) {
  return (
    (typeof value === "string"
      ? value.replace(/[\u0000-\u001f\u007f]/g, "").trim()
      : ""
    ).slice(0, limit) || fallback
  );
}
function number(value, fallback, min, max) {
  const n = Number(value);
  return Number.isFinite(n) ? Math.min(max, Math.max(min, n)) : fallback;
}
function normalizeSourceAsset(raw) {
  if (
    !raw ||
    typeof raw !== "object" ||
    !["material", "brush", "generator"].includes(raw.type)
  )
    return null;
  if (typeof raw.id !== "string" || !/^[a-zA-Z0-9_-]{1,120}$/.test(raw.id))
    return null;
  return { type: raw.type, id: raw.id, name: text(raw.name, "Asset", 80) };
}
function normalizeEnabledChannels(ids) {
  // Imported normal targets obey the same Height dependency as the channel card.
  if (ids.includes("normal") && !ids.includes("height")) ids.push("height");
  return TEXTURE_CHANNELS.filter((c) => ids.includes(c.id)).map((c) => c.id);
}
function normalizeLayer(raw) {
  if (!raw || typeof raw !== "object" || Array.isArray(raw))
    throw new Error("Every layer must be a layer record.");
  if (typeof raw.id !== "string" || !/^[a-zA-Z0-9_-]{1,80}$/.test(raw.id))
    throw new Error("A layer has an invalid identity.");
  if (typeof raw.kind !== "string" || !Object.hasOwn(TEXTURE_KINDS, raw.kind))
    throw new Error("A layer has an unsupported type.");
  if (
    raw.parentId != null &&
    raw.parentId !== "" &&
    (typeof raw.parentId !== "string" ||
      !/^[a-zA-Z0-9_-]{1,80}$/.test(raw.parentId))
  )
    throw new Error("A layer has an invalid folder identity.");
  return {
    id: raw.id,
    kind: raw.kind,
    name: text(
      raw.name,
      raw.kind === "folder"
        ? "Folder"
        : `${TEXTURE_KINDS[raw.kind].label} layer`,
    ),
    parentId:
      typeof raw.parentId === "string" && raw.parentId ? raw.parentId : null,
    visible: raw.visible !== false,
    locked: raw.locked === true,
    opacity: number(raw.opacity ?? 100, 100, 0, 100),
    blend: TEXTURE_BLENDS.includes(raw.blend) ? raw.blend : "Normal",
    channelSettings: normalizeChannelSettings(raw.channelSettings),
    sourceAsset:
      raw.kind === "folder" ? null : normalizeSourceAsset(raw.sourceAsset),
    ...(raw.kind === "material" && (raw.surfaceTarget || raw.surfaceMaterial)
      ? {
          ...(raw.surfaceTarget
            ? { surfaceTarget: normalizeSurfaceTarget(raw.surfaceTarget) }
            : {}),
          surfaceMaterial: normalizeSurfaceMaterial(raw.surfaceMaterial),
        }
      : {}),
    channels: normalizeEnabledChannels([
      ...new Set(
        (Array.isArray(raw.channels)
          ? raw.channels
          : raw.kind === "folder"
            ? []
            : ["baseColor", "roughness"]
        )
          .map(
            (id) =>
              ({
                baseColour: "baseColor",
                metallic: "metalness",
                ambientOcclusion: "occlusion",
              })[id] || id,
          )
          .filter((id) => channelIds.has(id)),
      ),
    ]),
    ...(raw.paint ? { paint: normalizeDigitalPaint(raw.paint) } : {}),
    mask: normalizeTextureMask(raw.mask),
    decals: raw.kind === "folder" ? [] : normalizeLayerDecals(raw.decals),
  };
}
export function createTextureLayer(kind = "paint", options = {}) {
  return normalizeLayer({ id: nextId(), kind, ...options });
}
export function createTextureDocument() {
  return {
    schema: TEXTURE_SCHEMA,
    name: "Teapot study",
    stamps: [],
    painting: normalizePaintSettings(),
    digitalPainting: normalizeDigitalBrush(),
    lastDigitalStroke: null,
    lastStroke: null,
    scene: "teapot",
    // Topmost first, everywhere: the UI, history, files, and compositor.
    layers: [
      createTextureLayer("paint", {
        id: "surface-detail",
        name: "Surface detail",
      }),
      createTextureLayer("fill", {
        id: "surface-finish",
        name: "Surface finish",
      }),
      createTextureLayer("material", {
        id: "base-material",
        name: "Base material",
        channels: [
          "baseColor",
          "metalness",
          "roughness",
          "height",
          "normal",
          "occlusion",
        ],
      }),
    ],
  };
}
let maskAuthoringGeometry;
export function validateTextureDocument(input) {
  if (!input || typeof input !== "object" || input.schema !== TEXTURE_SCHEMA)
    throw new Error("Choose an Alloy texture-layer project (.texture.json).");
  if (!Array.isArray(input.layers) || input.layers.length > TEXTURE_LAYER_LIMIT)
    throw new Error(
      `A layer project supports up to ${TEXTURE_LAYER_LIMIT} layers.`,
    );
  const layers = orderTextureLayers(input.layers.map(normalizeLayer));
  for (const layer of layers)
    if (layer.mask?.paint) {
      maskAuthoringGeometry ||= createTextureTeapotGeometry(20);
      validateMaskPaintGeometry(layer.mask.paint, maskAuthoringGeometry);
    }
  for (const layer of layers)
    if (layer.paint) {
      if (layer.kind === "folder")
        throw new Error("Folders can paint their mask, not colour channels.");
      maskAuthoringGeometry ||= createTextureTeapotGeometry(20);
      validateDigitalPaintGeometry(layer.paint, maskAuthoringGeometry);
      if (
        layer.surfaceTarget &&
        layer.paint.operations.some(
          (op) => op.region.partId !== layer.surfaceTarget.partId,
        )
      )
        throw new Error(
          "Digital strokes must stay on their layer's target component.",
        );
    }
  const paintSlices = layers.reduce(
    (n, layer) =>
      n +
      (paintedChannelIds(layer).length
        ? paintedChannelIds(layer).length + 1
        : 0),
    0,
  );
  if (paintSlices > PAINT_CHANNEL_SLICE_LIMIT)
    throw new Error(
      "The project's 64 digital channel-slice budget is full. Disable unused channels or remove an unused paint layer.",
    );
  if (new Set(layers.map((l) => l.id)).size !== layers.length)
    throw new Error("Layer identities must be unique.");
  const stroke = input.lastStroke
    ? normalizeSourceStroke(input.lastStroke)
    : null;
  if (input.lastStroke && !stroke)
    throw new Error(
      "The recorded source stroke is invalid or exceeds its preview/point limit.",
    );
  const allStamps = normalizeDocumentStamps(input.stamps);
  for (const layer of layers) {
    if (
      layer.surfaceTarget &&
      layer.decals.some(
        (d) => !d.target || d.target.partId !== layer.surfaceTarget.partId,
      )
    )
      throw new Error(
        "Part-scoped decal components must stay on their layer's target.",
      );
  }
  const components = layers.flatMap((l) => l.decals);
  if (components.length > DECAL_LIMIT)
    throw new Error(
      `A project can have at most ${DECAL_LIMIT} decal components.`,
    );
  if (new Set(components.map((d) => d.id)).size !== components.length)
    throw new Error(
      "Decal component identities must be unique across the project.",
    );
  if (components.some((d) => !allStamps.some((s) => s.id === d.stampId)))
    throw new Error("A decal references a missing stamp snapshot.");
  const usedStamps = new Set(components.map((d) => d.stampId));
  const stamps = allStamps.filter((s) => usedStamps.has(s.id));
  const result = {
    schema: TEXTURE_SCHEMA,
    stamps,
    name: text(input.name, "Untitled surface"),
    scene: "teapot",
    ...(input.meshMaps
      ? { meshMaps: normalizeTextureMeshMaps(input.meshMaps) }
      : {}),
    painting: normalizePaintSettings(input.painting),
    digitalPainting: normalizeDigitalBrush(input.digitalPainting),
    lastDigitalStroke:
      input.lastDigitalStroke &&
      layers.some((l) => l.id === input.lastDigitalStroke.layerId)
        ? normalizeLastDigitalStroke(input.lastDigitalStroke)
        : null,
    lastStroke:
      stroke &&
      layers.some((l) => l.id === stroke.layerId && l.kind !== "folder")
        ? stroke
        : null,
    layers,
  };
  if (
    new TextEncoder().encode(JSON.stringify(result)).byteLength >
    TEXTURE_FILE_LIMIT
  )
    throw new Error("Layer project exceeds the 1 MB authoring limit.");
  return result;
}
export function parseTextureDocument(source) {
  if (
    typeof source !== "string" ||
    new TextEncoder().encode(source).byteLength > TEXTURE_FILE_LIMIT
  )
    throw new Error("Layer project files must be smaller than 1 MB.");
  let input;
  try {
    input = JSON.parse(source);
  } catch {
    throw new Error("The layer project is not valid JSON.");
  }
  return validateTextureDocument(input);
}

// Flat records stay portable; parent IDs define a validated, preorder folder tree.
export function textureAncestors(doc, id) {
  const records = new Map(doc.layers.map((l) => [l.id, l])),
    ancestors = [],
    seen = new Set([id]);
  let node = records.get(id);
  while (node?.parentId) {
    if (seen.has(node.parentId))
      throw new Error("Layer folders cannot form a cycle.");
    seen.add(node.parentId);
    node = records.get(node.parentId);
    if (!node) break;
    ancestors.push(node);
  }
  return ancestors;
}
export function isTextureLayerLocked(doc, id) {
  const layer = doc.layers.find((l) => l.id === id);
  return !!(layer?.locked || textureAncestors(doc, id).some((l) => l.locked));
}
export function isTextureLayerVisible(doc, id) {
  const layer = doc.layers.find((l) => l.id === id);
  return (
    !!layer &&
    layer.visible &&
    textureAncestors(doc, id).every((l) => l.visible)
  );
}
export function textureSubtree(doc, id) {
  return doc.layers.filter(
    (l) => l.id === id || textureAncestors(doc, l.id).some((a) => a.id === id),
  );
}
function orderTextureLayers(layers) {
  const records = new Map(layers.map((l) => [l.id, l]));
  if (records.size !== layers.length)
    throw new Error("Layer identities must be unique.");
  for (const layer of layers) {
    if (
      layer.parentId &&
      (!records.has(layer.parentId) ||
        records.get(layer.parentId).kind !== "folder")
    )
      throw new Error("A layer parent must be an existing folder.");
    const ancestors = textureAncestors({ layers }, layer.id);
    if (ancestors.length > TEXTURE_FOLDER_DEPTH_LIMIT)
      throw new Error(
        `Folders support up to ${TEXTURE_FOLDER_DEPTH_LIMIT} nested levels.`,
      );
  }
  const result = [];
  function walk(parent) {
    for (const l of layers.filter((l) => l.parentId === parent)) {
      result.push(l);
      if (l.kind === "folder") walk(l.id);
    }
  }
  walk(null);
  return result;
}
export function isTextureSubtreeProtected(doc, id) {
  return textureSubtree(doc, id).some(
    (l) =>
      isTextureLayerLocked(doc, l.id) || (l.decals || []).some((d) => d.locked),
  );
}
function immutableSubtree(doc, id) {
  return isTextureSubtreeProtected(doc, id);
}
export function patchTextureLayer(doc, id, patch) {
  const layer = doc.layers.find((l) => l.id === id);
  if (!layer) return doc;
  const allowed = isTextureLayerLocked(doc, id)
    ? Object.fromEntries(
        Object.entries(patch).filter(([k]) =>
          ["locked", "visible"].includes(k),
        ),
      )
    : patch;
  // Membership changes go through moveTextureLayer so a whole subtree is moved.
  const next = normalizeLayer({
    ...layer,
    ...allowed,
    id: layer.id,
    kind: layer.kind,
    parentId: layer.parentId,
  });
  return JSON.stringify(next) === JSON.stringify(layer)
    ? doc
    : { ...doc, layers: doc.layers.map((l) => (l.id === id ? next : l)) };
}
export function insertTextureLayer(doc, kind, aboveId) {
  if (doc.layers.length >= TEXTURE_LAYER_LIMIT)
    throw new Error(`The ${TEXTURE_LAYER_LIMIT}-layer limit has been reached.`);
  const target = doc.layers.find((l) => l.id === aboveId);
  if (target && isTextureLayerLocked(doc, target.id))
    throw new Error(
      "Unlock the selected layer or folder before adding content.",
    );
  const parentId =
    target?.kind === "folder" ? target.id : target?.parentId || null;
  const layer = createTextureLayer(kind, { parentId }),
    layers = [...doc.layers],
    index = target
      ? target.kind === "folder"
        ? layers.indexOf(target) + 1
        : layers.indexOf(target)
      : 0;
  layers.splice(index, 0, layer);
  return { doc: validateTextureDocument({ ...doc, layers }), id: layer.id };
}
export function duplicateTextureLayer(doc, id) {
  const root = doc.layers.find((l) => l.id === id);
  if (!root || immutableSubtree(doc, id)) return { doc, id };
  const subtree = textureSubtree(doc, id);
  if (doc.layers.length + subtree.length > TEXTURE_LAYER_LIMIT)
    throw new Error(`The ${TEXTURE_LAYER_LIMIT}-layer limit has been reached.`);
  const identities = new Map(subtree.map((l) => [l.id, nextId()]));
  const copies = subtree.map((l) =>
    createTextureLayer(l.kind, {
      ...structuredClone(l),
      id: identities.get(l.id),
      parentId: l.id === id ? l.parentId : identities.get(l.parentId),
      name: l.id === id ? `${l.name} copy`.slice(0, 80) : l.name,
      decals: (l.decals || []).map((d) => ({ ...d, id: stampId() })),
    }),
  );
  const layers = [...doc.layers];
  layers.splice(layers.indexOf(root), 0, ...copies);
  return {
    doc: validateTextureDocument({ ...doc, layers }),
    id: identities.get(id),
  };
}
export function removeTextureLayer(doc, id) {
  const index = doc.layers.findIndex((l) => l.id === id);
  if (index < 0 || immutableSubtree(doc, id)) return { doc, id };
  const ids = new Set(textureSubtree(doc, id).map((l) => l.id)),
    layers = doc.layers.filter((l) => !ids.has(l.id));
  return {
    doc: { ...doc, layers },
    id: layers[Math.min(index, layers.length - 1)]?.id || null,
  };
}
export function moveTextureLayer(doc, id, parentId = null, beforeId = null) {
  const root = doc.layers.find((l) => l.id === id),
    parent = doc.layers.find((l) => l.id === parentId),
    before = doc.layers.find((l) => l.id === beforeId);
  if (
    !root ||
    immutableSubtree(doc, id) ||
    (parentId &&
      (!parent ||
        parent.kind !== "folder" ||
        isTextureLayerLocked(doc, parentId))) ||
    (beforeId && (!before || before.parentId !== parentId))
  )
    return doc;
  const subtree = textureSubtree(doc, id),
    ids = new Set(subtree.map((l) => l.id));
  if (ids.has(parentId) || ids.has(beforeId)) return doc;
  const layers = doc.layers.filter((l) => !ids.has(l.id));
  let index = before
    ? layers.findIndex((l) => l.id === beforeId)
    : parentId
      ? layers.findIndex((l) => l.id === parentId) +
        textureSubtree({ layers }, parentId).length
      : layers.length;
  layers.splice(
    index,
    0,
    ...subtree.map((l) => (l.id === id ? { ...l, parentId } : l)),
  );
  const next = validateTextureDocument({ ...doc, layers });
  return JSON.stringify(next.layers) === JSON.stringify(doc.layers)
    ? doc
    : next;
}
// A drop describes sibling order, not flat preorder indices. Subtree removal,
// locks, cycle checks and depth validation stay in the one canonical move API.
export function textureLayerDropPosition(
  doc,
  sourceId,
  targetId,
  zone = "before",
) {
  const source = doc.layers.find((l) => l.id === sourceId);
  if (!source || immutableSubtree(doc, sourceId)) return null;
  if (!targetId) return { parentId: null, beforeId: null };
  const target = doc.layers.find((l) => l.id === targetId);
  const subtree = textureSubtree(doc, sourceId),
    ids = new Set(subtree.map((l) => l.id));
  if (
    !target ||
    ids.has(targetId) ||
    !["before", "after", "inside"].includes(zone)
  )
    return null;
  const withinDepth = (parentId) => {
    const depth = parentId ? textureAncestors(doc, parentId).length + 1 : 0;
    const oldDepth = textureAncestors(doc, sourceId).length;
    const relative = Math.max(
      0,
      ...subtree.map((l) => textureAncestors(doc, l.id).length - oldDepth),
    );
    return depth + relative <= TEXTURE_FOLDER_DEPTH_LIMIT;
  };
  if (zone === "inside") {
    return target.kind === "folder" &&
      !isTextureLayerLocked(doc, targetId) &&
      withinDepth(targetId)
      ? { parentId: targetId, beforeId: null }
      : null;
  }
  if (
    (target.parentId && isTextureLayerLocked(doc, target.parentId)) ||
    !withinDepth(target.parentId)
  )
    return null;
  const siblings = doc.layers.filter(
    (l) => l.parentId === target.parentId && !ids.has(l.id),
  );
  return {
    parentId: target.parentId,
    beforeId:
      zone === "after"
        ? siblings[siblings.findIndex((l) => l.id === targetId) + 1]?.id || null
        : target.id,
  };
}
export function reorderTextureLayer(doc, id, beforeId = null) {
  const before = doc.layers.find((l) => l.id === beforeId);
  if (beforeId && !before) return doc;
  return moveTextureLayer(doc, id, before?.parentId || null, beforeId);
}
export function ungroupTextureFolder(doc, id) {
  const folder = doc.layers.find((l) => l.id === id);
  if (folder?.kind !== "folder" || immutableSubtree(doc, id)) return doc;
  const layers = doc.layers
    .filter((l) => l.id !== id)
    .map((l) => (l.parentId === id ? { ...l, parentId: folder.parentId } : l));
  return validateTextureDocument({ ...doc, layers });
}

// A preset is frozen once embedded, shared by references rather than repeated
// PNG/project payloads for each dab. Re-saving a library preset never mutates
// already-placed components; changed artwork gets a fresh project identity.
function embedTextureStamp(doc, rawPreset) {
  let preset = normalizeStampPreset(rawPreset);
  const used = new Set(
    doc.layers.flatMap((l) => l.decals || []).map((d) => d.stampId),
  );
  const stamps = (doc.stamps || []).filter((s) => used.has(s.id));
  const equivalent = stamps.find(
    (s) =>
      s.name === preset.name &&
      JSON.stringify(s.raster) === JSON.stringify(preset.raster) &&
      JSON.stringify({ ...s.project, id: null }) ===
        JSON.stringify({ ...preset.project, id: null }),
  );
  if (equivalent) preset = equivalent;
  const existing = stamps.find((s) => s.id === preset.id);
  if (existing && JSON.stringify(existing) !== JSON.stringify(preset)) {
    const id = stampId();
    preset = normalizeStampPreset({
      ...preset,
      project: { ...preset.project, id },
    });
  }
  if (!stamps.some((s) => s.id === preset.id)) stamps.push(preset);
  return { preset, stamps };
}
export function addTextureDecal(doc, layerId, rawPreset, placement = {}) {
  const layer = doc.layers.find((l) => l.id === layerId);
  if (!layer || layer.kind === "folder" || isTextureLayerLocked(doc, layerId))
    return { doc, id: null };
  if (doc.layers.flatMap((l) => l.decals || []).length >= DECAL_LIMIT)
    throw new Error(`The ${DECAL_LIMIT}-decal project limit has been reached.`);
  if (placement.target && isTexturePartLocked(doc, placement.target.partId))
    throw new Error(
      "Unlock this part's material layer before dropping a stamp onto it.",
    );
  const { preset, stamps } = embedTextureStamp(doc, rawPreset);
  if (
    layer.surfaceTarget &&
    placement.target &&
    layer.surfaceTarget.partId !== placement.target.partId
  )
    throw new Error(
      `This layer targets ${texturePart(layer.surfaceTarget.partId).name}. Select a layer for that part first.`,
    );
  const component = createDecal(preset, {
    ...placement,
    target: placement.target || layer.surfaceTarget,
  });
  const next = validateTextureDocument({
    ...doc,
    stamps,
    layers: doc.layers.map((l) =>
      l.id === layerId ? { ...l, decals: [...(l.decals || []), component] } : l,
    ),
  });
  return { doc: next, id: component.id };
}
export function patchTextureDecal(doc, layerId, componentId, patch) {
  const layer = doc.layers.find((l) => l.id === layerId);
  const component = layer?.decals?.find((d) => d.id === componentId);
  if (
    !component ||
    isTextureLayerLocked(doc, layerId) ||
    (((component.target && isTexturePartLocked(doc, component.target.partId)) ||
      (patch.target && isTexturePartLocked(doc, patch.target.partId))) &&
      !Object.keys(patch).every((k) => ["locked", "visible"].includes(k))) ||
    (component.locked &&
      !Object.keys(patch).every((k) => ["locked", "visible"].includes(k)))
  )
    return doc;
  return validateTextureDocument({
    ...doc,
    layers: doc.layers.map((l) =>
      l.id === layerId
        ? {
            ...l,
            decals: l.decals.map((d) =>
              d.id === componentId
                ? {
                    ...d,
                    ...patch,
                    id: d.id,
                    stampId: d.stampId,
                    space: d.space,
                  }
                : d,
            ),
          }
        : l,
    ),
  });
}
export function removeTextureDecal(doc, layerId, componentId) {
  const layer = doc.layers.find((l) => l.id === layerId);
  if (
    !layer ||
    isTextureLayerLocked(doc, layerId) ||
    layer.decals?.find((d) => d.id === componentId)?.locked ||
    (layer.decals?.find((d) => d.id === componentId)?.target &&
      isTexturePartLocked(
        doc,
        layer.decals.find((d) => d.id === componentId).target.partId,
      ))
  )
    return doc;
  const layers = doc.layers.map((l) =>
    l.id === layerId
      ? { ...l, decals: l.decals.filter((d) => d.id !== componentId) }
      : l,
  );
  const used = new Set(
    layers.flatMap((l) => l.decals || []).map((d) => d.stampId),
  );
  return validateTextureDocument({
    ...doc,
    layers,
    stamps: (doc.stamps || []).filter((s) => used.has(s.id)),
  });
}

export function replaceTextureDecalStamp(doc, layerId, componentId, rawPreset) {
  const layer = doc.layers.find((l) => l.id === layerId),
    component = layer?.decals.find((d) => d.id === componentId);
  if (
    !component ||
    component.locked ||
    isTextureLayerLocked(doc, layerId) ||
    (component.target && isTexturePartLocked(doc, component.target.partId))
  )
    return doc;
  const without = {
    ...doc,
    layers: doc.layers.map((l) =>
      l.id === layerId
        ? { ...l, decals: l.decals.filter((d) => d.id !== componentId) }
        : l,
    ),
  };
  const { preset, stamps } = embedTextureStamp(without, rawPreset);
  return validateTextureDocument({
    ...doc,
    stamps,
    layers: doc.layers.map((l) =>
      l.id === layerId
        ? {
            ...l,
            decals: l.decals.map((d) =>
              d.id === componentId
                ? {
                    ...d,
                    stampId: preset.id,
                    channels: preset.project.channels,
                    channelValues: preset.project.channelValues,
                  }
                : d,
            ),
          }
        : l,
    ),
  });
}

// A viewport drop changes one real material region, never the whole specimen.
// Preset data is embedded so editing/removing a library entry cannot rewrite it.
export function assignTexturePartMaterial(
  doc,
  target,
  material,
  asset = material,
) {
  const scope = normalizeSurfaceTarget(target);
  if (!scope)
    throw new Error(
      "Choose an existing teapot part before assigning a material.",
    );
  const part = texturePart(scope.partId),
    previous = activeTexturePartMaterials(doc).get(scope.partId)?.layer;
  if (
    doc.layers.some(
      (l) =>
        l.surfaceTarget?.partId === scope.partId &&
        l.surfaceMaterial &&
        isTextureLayerLocked(doc, l.id),
    )
  )
    throw new Error(
      `${part.name} has a locked material assignment. Unlock its layer before replacing it.`,
    );
  const snapshot = normalizeSurfaceMaterial(material),
    sourceAsset = {
      type: "material",
      id: asset.id,
      name: asset.name || snapshot.name || "Material",
    };
  if (previous) {
    const next = patchTextureLayer(doc, previous.id, {
      surfaceTarget: scope,
      surfaceMaterial: snapshot,
      sourceAsset,
      name: previous.name.startsWith(`${part.name} · `)
        ? `${part.name} · ${sourceAsset.name}`
        : previous.name,
    });
    return { doc: validateTextureDocument(next), id: previous.id };
  }
  if (doc.layers.length >= TEXTURE_LAYER_LIMIT)
    throw new Error("The 64-layer limit has been reached.");
  const layer = createTextureLayer("material", {
    name: `${part.name} · ${sourceAsset.name}`,
    surfaceTarget: scope,
    surfaceMaterial: snapshot,
    sourceAsset,
  });
  return {
    doc: validateTextureDocument({ ...doc, layers: [layer, ...doc.layers] }),
    id: layer.id,
  };
}
export function activeTexturePartMaterials(doc) {
  const result = new Map();
  for (const layer of doc.layers) {
    const partId = layer.surfaceTarget?.partId;
    if (
      !partId ||
      !layer.surfaceMaterial ||
      result.has(partId) ||
      !isTextureLayerVisible(doc, layer.id)
    )
      continue;
    const ancestors = [...textureAncestors(doc, layer.id), layer],
      opacity = ancestors.reduce(
        (value, owner) => (value * owner.opacity) / 100,
        1,
      );
    if (opacity > 0) result.set(partId, { layer, ancestors, opacity });
  }
  return result;
}

export function isTexturePartLocked(doc, partId) {
  return doc.layers.some(
    (l) =>
      l.surfaceMaterial &&
      l.surfaceTarget?.partId === partId &&
      isTextureLayerLocked(doc, l.id),
  );
}
