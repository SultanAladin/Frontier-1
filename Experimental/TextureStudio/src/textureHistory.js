import { DIGITAL_BRUSH_PRESETS } from "./digitalBrush.js";
import { paintTool } from "./paintToolCatalogue.js";
import { PAINT_CHANNELS } from "./paintChannelModel.js";
export const TEXTURE_HISTORY_LIMIT = 80;
const equal = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const title = (key) =>
  key.replace(/([a-z])([A-Z])/g, "$1 $2").replace(/^./, (c) => c.toUpperCase());
const channelLabel = (id) =>
  PAINT_CHANNELS.find((c) => c.id === id)?.label || id;
// Descriptors are derived from validated snapshots, never supplied HTML/scripts.
export function describeTextureChange(before, after) {
  const digital = after.lastDigitalStroke;
  if (digital && digital.id !== before.lastDigitalStroke?.id)
    return {
      kind: "stroke",
      label: `${title(digital.operation)} ${digital.mask ? "mask" : "channels"}`,
      detail: `${digital.layerName} · ${digital.mask ? "Mask coverage" : digital.channels.map(channelLabel).join(", ")} · ${DIGITAL_BRUSH_PRESETS.find((p) => p.id === digital.brush.preset)?.name}`,
      layerId: digital.layerId,
    };
  if (!equal(before.digitalPainting, after.digitalPainting)) {
    const changed = Object.keys(after.digitalPainting).filter(
      (key) => before.digitalPainting?.[key] !== after.digitalPainting[key],
    );
    return {
      kind: "tool",
      label:
        changed.length === 1
          ? `Digital ${title(changed[0]).toLowerCase()}`
          : "Digital brush settings",
      detail:
        DIGITAL_BRUSH_PRESETS.find((p) => p.id === after.digitalPainting.preset)
          ?.name || "Digital brush",
    };
  }
  const stroke = after.lastStroke;
  if (stroke && stroke.id !== before.lastStroke?.id)
    return {
      kind: "stroke",
      label: `${stroke.operation === "erase" ? "Erase" : "Paint"} stroke`,
      detail: `${stroke.layerName} · ${channelLabel(stroke.channelId)} · ${paintTool(stroke.settings.toolId).label}`,
      layerId: stroke.layerId,
    };
  if (!equal(before.painting, after.painting)) {
    const a = before.painting,
      b = after.painting,
      tool = paintTool(b.toolId);
    if (a.toolId !== b.toolId)
      return { kind: "tool", label: "Choose instrument", detail: tool.label };
    if (a.color !== b.color)
      return {
        kind: "tool",
        label: "Tool colour",
        detail: b.color.toUpperCase(),
      };
    const keys = Object.keys(b.params).filter(
      (k) => a.params[k] !== b.params[k],
    );
    return {
      kind: "tool",
      label:
        keys.length === 1
          ? `Tool ${title(keys[0]).toLowerCase()}`
          : "Instrument settings",
      detail:
        keys.length === 1 ? `${tool.label} · ${b.params[keys[0]]}` : tool.label,
    };
  }
  const previous = new Map(before.layers.map((l) => [l.id, l])),
    next = new Map(after.layers.map((l) => [l.id, l]));
  const added = after.layers.filter((l) => !previous.has(l.id)),
    removed = before.layers.filter((l) => !next.has(l.id));
  if (added.length)
    return {
      kind: added[0].kind === "folder" ? "folder" : "layer",
      label: / copy$/.test(added[0].name)
        ? "Duplicate layer / folder"
        : added[0].kind === "folder"
          ? "Add folder"
          : "Add layer",
      detail:
        added.length > 1
          ? `${added[0].name} · ${added.length} records`
          : added[0].name,
      layerId: added[0].id,
    };
  if (removed.length)
    return {
      kind: removed[0].kind === "folder" ? "folder" : "layer",
      label:
        removed[0].kind === "folder"
          ? "Remove / ungroup folder"
          : "Delete layer",
      detail:
        removed.length > 1
          ? `${removed[0].name} · ${removed.length} records`
          : removed[0].name,
      layerId: removed[0].id,
    };
  for (const layer of after.layers) {
    const old = previous.get(layer.id);
    if (equal(old, layer)) continue;
    const info = { kind: "layer", detail: layer.name, layerId: layer.id };
    if (old.name !== layer.name)
      return {
        ...info,
        label: "Rename layer",
        detail: `${old.name} → ${layer.name}`,
      };
    if (old.parentId !== layer.parentId)
      return {
        ...info,
        kind: "folder",
        label: "Move to folder",
        detail: `${layer.name} → ${next.get(layer.parentId)?.name || "Root"}`,
      };
    if (old.locked !== layer.locked)
      return {
        ...info,
        label: layer.locked ? "Lock layer / folder" : "Unlock layer / folder",
      };
    if (old.visible !== layer.visible)
      return {
        ...info,
        label: layer.visible ? "Show layer / folder" : "Hide layer / folder",
      };
    if (
      !equal(old.surfaceTarget, layer.surfaceTarget) ||
      !equal(old.surfaceMaterial, layer.surfaceMaterial)
    )
      return {
        ...info,
        kind: "source",
        label: "Part material assignment",
        detail: `${layer.name} · ${layer.surfaceTarget?.partId || "removed"}`,
      };
    if (!equal(old.decals, layer.decals)) {
      const beforeDecals = old.decals || [],
        afterDecals = layer.decals || [];
      return {
        ...info,
        kind: "decal",
        label:
          afterDecals.length > beforeDecals.length
            ? "Stamp surface decal / mask"
            : afterDecals.length < beforeDecals.length
              ? "Remove decal component"
              : "Decal component settings",
        detail: `${layer.name} · ${afterDecals.length} components`,
      };
    }
    if (!equal(old.mask, layer.mask))
      return {
        ...info,
        kind: "mask",
        label: !old.mask
          ? "Add mask"
          : !layer.mask
            ? "Remove mask"
            : layer.mask.kind === "color"
              ? "Colour mask settings"
              : layer.mask.kind === "gradient"
                ? "Gradient mask settings"
                : "Fill mask settings",
      };
    if (old.opacity !== layer.opacity)
      return {
        ...info,
        label: "Layer opacity",
        detail: `${layer.name} · ${layer.opacity}%`,
      };
    if (old.blend !== layer.blend)
      return {
        ...info,
        label: "Layer blend",
        detail: `${layer.name} · ${layer.blend}`,
      };
    if (!equal(old.channels, layer.channels))
      return {
        ...info,
        kind: "channel",
        label: "Channel targets",
        detail: `${layer.name} · ${layer.channels.length} enabled`,
      };
    if (!equal(old.sourceAsset, layer.sourceAsset))
      return {
        ...info,
        kind: "source",
        label: "Assign source asset",
        detail: `${layer.name} · ${layer.sourceAsset?.name || "None"}`,
      };
    for (const id of Object.keys(layer.channelSettings)) {
      const a = old.channelSettings[id],
        b = layer.channelSettings[id];
      if (equal(a, b)) continue;
      if (
        b.generator?.id === "MaterialStudio" &&
        a.generator?.result?.dataUrl !== b.generator?.result?.dataUrl &&
        b.generator?.result
      )
        return {
          ...info,
          kind: "source",
          label: "Render Material generator",
          detail: `${layer.name} · ${channelLabel(id)}`,
        };
      if (a.gradient && b.gradient && !equal(a.gradient, b.gradient))
        return {
          ...info,
          kind: "source",
          label: "Gradient point settings",
          detail: `${layer.name} · ${channelLabel(id)}`,
        };
      if (a.texture?.dataUrl !== b.texture?.dataUrl)
        return {
          ...info,
          kind: "source",
          label: b.texture ? "Import / replace texture" : "Remove texture",
          detail: `${layer.name} · ${channelLabel(id)}`,
        };
      return {
        ...info,
        kind: "channel",
        label: `${channelLabel(id)} ${a.mode !== b.mode ? "source" : "settings"}`,
        detail: `${layer.name} · ${title(b.mode)}`,
      };
    }
    return { ...info, label: "Layer properties" };
  }
  if (
    !equal(
      before.layers.map((l) => l.id),
      after.layers.map((l) => l.id),
    )
  )
    return {
      kind: "layer",
      label: "Reorder layers",
      detail: "Top-to-bottom layer order",
    };
  return { kind: "project", label: "Project settings", detail: after.name };
}
export function createTextureHistory(
  state,
  label = "Opened project",
  now = Date.now(),
) {
  return {
    past: [],
    present: {
      ...state,
      revision: {
        id: "revision-0",
        label,
        detail: state.doc.name,
        kind: "project",
        time: now,
      },
    },
    future: [],
    key: null,
    time: 0,
    serial: 0,
  };
}
export function commitTextureHistory(
  history,
  state,
  { key, label } = {},
  now = Date.now(),
) {
  const merge = !!key && key === history.key && now - history.time <= 650;
  const serial = merge ? history.serial : history.serial + 1;
  const descriptor = describeTextureChange(history.present.doc, state.doc);
  return {
    past: merge
      ? history.past
      : [...history.past, history.present].slice(-TEXTURE_HISTORY_LIMIT),
    present: {
      ...state,
      revision: {
        ...descriptor,
        label: label || descriptor.label,
        id: merge ? history.present.revision.id : `revision-${serial}`,
        time: merge ? history.present.revision.time : now,
      },
    },
    future: [],
    key: key || null,
    time: now,
    serial,
  };
}
export function travelTextureHistory(history, direction) {
  if (direction === "undo") {
    if (!history.past.length) return history;
    return {
      ...history,
      past: history.past.slice(0, -1),
      present: history.past.at(-1),
      future: [...history.future, history.present],
      key: null,
    };
  }
  if (direction === "redo") {
    if (!history.future.length) return history;
    return {
      ...history,
      past: [...history.past, history.present],
      present: history.future.at(-1),
      future: history.future.slice(0, -1),
      key: null,
    };
  }
  return history;
}
export function textureHistoryTimeline(history) {
  const frames = [
    ...history.past,
    history.present,
    ...[...history.future].reverse(),
  ];
  return frames.map((frame, index) => ({
    ...frame.revision,
    index,
    current: index === history.past.length,
    undone: index > history.past.length,
    lastStroke: frame.doc.lastStroke || null,
    lastDigitalStroke: frame.doc.lastDigitalStroke || null,
  }));
}
export function restoreTextureHistory(history, id) {
  const entries = textureHistoryTimeline(history),
    target = entries.findIndex((e) => e.id === id);
  if (target < 0 || target === history.past.length) return history;
  let result = history;
  while (result.past.length !== target)
    result = travelTextureHistory(
      result,
      result.past.length > target ? "undo" : "redo",
    );
  return result;
}
