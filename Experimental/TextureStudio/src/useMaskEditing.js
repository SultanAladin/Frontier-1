import { useEffect, useRef, useState, useMemo } from "react";
import {
  isTextureLayerLocked,
  isTextureLayerVisible,
  patchTextureLayer,
} from "./textureDocument.js";
import {
  normalizeMaskPaint,
  maskRegionFromHit,
  maskRegionContains,
  maskRegionLabel,
  MASK_POINT_LIMIT,
} from "./textureMaskPaint.js";
import { AUTHORING_UV_SET } from "./textureSurfaceModel.js";
import { uvTopology } from "./textureUVGeometry.js";

export const TEXTURE_AUTHORING_KEY = "alloy-texture-authoring-v1";
const defaults = {
  preview: "material",
  tool: "brush",
  scope: "object",
  value: 1,
  radius: 0.04,
  opacity: 1,
  hardness: 0.8,
};
function readState() {
  try {
    const raw = JSON.parse(localStorage.getItem(TEXTURE_AUTHORING_KEY));
    return {
      ...defaults,
      ...Object.fromEntries(
        Object.keys(defaults).map((k) => [k, raw?.[k] ?? defaults[k]]),
      ),
      preview: raw?.preview === "mask" ? "mask" : "material",
      tool: ["brush", "select", "navigate"].includes(raw?.tool)
        ? raw.tool
        : "brush",
      scope: ["face", "tile", "island", "object"].includes(raw?.scope)
        ? raw.scope
        : "object",
      value: raw?.value === 0 ? 0 : 1,
      radius: Number.isFinite(raw?.radius)
        ? Math.max(0.002, Math.min(0.25, raw.radius))
        : 0.04,
      opacity: Number.isFinite(raw?.opacity)
        ? Math.max(0, Math.min(1, raw.opacity))
        : 1,
      hardness: Number.isFinite(raw?.hardness)
        ? Math.max(0, Math.min(1, raw.hardness))
        : 0.8,
    };
  } catch {
    return defaults;
  }
}
export default function useMaskEditing(
  history,
  { geometry, viewMode, source, suspended, onActivate, decalId },
) {
  const { doc, selection } = history,
    layer = doc.layers.find((l) => l.id === selection.id),
    maskSelected = !!(selection.mask && layer?.mask),
    [state, setState] = useState(readState),
    [region, setRegion] = useState(null),
    [draft, setDraft] = useState(null),
    gesture = useRef(null),
    lastSelection = useRef(`${selection.id}:${maskSelected}`),
    latest = useRef(null),
    frame = useRef(0);
  const mode = maskSelected
    ? "mask"
    : suspended === "decal"
      ? "decal"
      : "paint";
  const available =
      maskSelected &&
      !isTextureLayerLocked(doc, layer.id) &&
      isTextureLayerVisible(doc, layer.id) &&
      !(viewMode === "uv" && source),
    active = available && !suspended && state.tool !== "navigate";
  latest.current = { doc, selection, layer, active, state, region };
  useEffect(() => {
    const identity = `${selection.id}:${maskSelected}`;
    if (lastSelection.current !== identity) {
      lastSelection.current = identity;
      cancel();
      setRegion(null);
      setState((s) => ({
        ...s,
        preview: maskSelected ? "mask" : "material",
        tool: "brush",
      }));
    }
    if (
      gesture.current &&
      (!active || JSON.stringify(layer?.mask) !== gesture.current.expected)
    )
      cancel();
  }, [selection.id, maskSelected, active, layer?.mask]);
  useEffect(() => {
    if (viewMode !== "uv" && state.scope === "tile") {
      cancel();
      setRegion(null);
      setState((s) => ({ ...s, scope: "object" }));
    }
  }, [viewMode, state.scope]);
  useEffect(() => {
    try {
      localStorage.setItem(
        TEXTURE_AUTHORING_KEY,
        JSON.stringify({ ...state, selection, mode }),
      );
    } catch {
      /* Optional UI state; project data has its own save path. */
    }
  }, [state, selection.id, selection.mask, mode]);
  useEffect(() => {
    const escape = (e) => {
      if (e.key === "Escape") cancel();
    };
    window.addEventListener("keydown", escape);
    return () => {
      window.removeEventListener("keydown", escape);
      cancelAnimationFrame(frame.current);
      gesture.current = null;
    };
  }, []);
  function cancel() {
    gesture.current = null;
    cancelAnimationFrame(frame.current);
    frame.current = 0;
    setDraft(null);
  }
  function change(patch) {
    cancel();
    if (patch.tool && patch.tool !== "navigate") onActivate();
    setState((s) => ({ ...s, ...patch }));
  }
  function selectScope(scope) {
    cancel();
    onActivate();
    setRegion(null);
    setState((s) => ({ ...s, scope, tool: "select", preview: "mask" }));
  }
  function publish() {
    const g = gesture.current;
    if (!g) return;
    try {
      const paint = normalizeMaskPaint({
        uvSet: AUTHORING_UV_SET,
        operations: [...(g.mask.paint?.operations || []), ...g.operations],
      });
      setDraft({ layerId: g.layerId, mask: { ...g.mask, paint } });
    } catch (e) {
      cancel();
      history.setError(e.message);
    }
  }
  function queue() {
    if (!frame.current)
      frame.current = requestAnimationFrame(() => {
        frame.current = 0;
        publish();
      });
  }
  function onStart(hit) {
    const l = latest.current;
    if (!l.active || !hit || gesture.current) return false;
    const picked = maskRegionFromHit(hit, l.state.scope);
    if (!picked) return false;
    if (l.state.tool === "select") {
      setRegion(picked);
      return true;
    }
    if (l.state.opacity <= 0) return false;
    const target = l.region || picked,
      triangle = uvTopology(geometry).triangles[hit.faceIndex];
    if (!maskRegionContains(target, triangle)) return false;
    const g = {
      layerId: l.layer.id,
      mask: l.layer.mask,
      expected: JSON.stringify(l.layer.mask),
      operations: [],
      region: target,
      settings: { ...l.state },
      chartId: null,
    };
    gesture.current = g;
    setState((s) => ({ ...s, preview: "mask" }));
    addSample(hit);
    publish();
    return !!gesture.current;
  }
  function addSample(hit) {
    const g = gesture.current;
    if (!g) return;
    const triangle = hit && uvTopology(geometry).triangles[hit.faceIndex];
    if (!hit || !maskRegionContains(g.region, triangle)) {
      g.chartId = null;
      return;
    }
    let op = g.operations.at(-1);
    if (
      !op ||
      g.chartId !== hit.chartId ||
      op.points.length >= MASK_POINT_LIMIT
    ) {
      op = {
        kind: "brush",
        region: g.region,
        value: g.settings.value,
        opacity: g.settings.opacity,
        radius: g.settings.radius,
        hardness: g.settings.hardness,
        points: [],
      };
      g.operations.push(op);
      g.chartId = hit.chartId;
    }
    const previous = op.points.at(-1),
      p = hit.uvPosition;
    if (
      !previous ||
      Math.hypot(p[0] - previous[0], p[1] - previous[1]) >=
        g.settings.radius * 0.12
    )
      op.points.push([...p]);
  }
  function onMove(hit) {
    if (gesture.current) {
      addSample(hit);
      queue();
    }
  }
  function onEnd(cancelled) {
    const g = gesture.current;
    if (!g) return;
    gesture.current = null;
    cancelAnimationFrame(frame.current);
    frame.current = 0;
    setDraft(null);
    if (cancelled) return;
    const current = history.docRef.current,
      owner = current.layers.find((l) => l.id === g.layerId),
      l = latest.current;
    if (
      !owner ||
      isTextureLayerLocked(current, owner.id) ||
      !isTextureLayerVisible(current, owner.id) ||
      !l.selection.mask ||
      l.selection.id !== owner.id ||
      JSON.stringify(owner.mask) !== g.expected
    )
      return;
    try {
      const paint = normalizeMaskPaint({
        uvSet: AUTHORING_UV_SET,
        operations: [...(owner.mask.paint?.operations || []), ...g.operations],
      });
      history.commit(
        patchTextureLayer(current, owner.id, {
          mask: { ...owner.mask, paint },
        }),
        {
          selection: { id: owner.id, mask: true },
          label: `${g.settings.value ? "Paint" : "Erase"} mask · ${maskRegionLabel(g.region)}`,
        },
      );
    } catch (e) {
      history.setError(e.message);
    }
  }
  function fillRegion() {
    const l = latest.current;
    if (!available || !l.region || l.state.opacity <= 0) return;
    cancel();
    try {
      const paint = normalizeMaskPaint({
        uvSet: AUTHORING_UV_SET,
        operations: [
          ...(l.layer.mask.paint?.operations || []),
          {
            kind: "region",
            region: l.region,
            value: l.state.value,
            opacity: l.state.opacity,
          },
        ],
      });
      history.commit(
        patchTextureLayer(history.docRef.current, l.layer.id, {
          mask: { ...l.layer.mask, paint },
        }),
        {
          selection: { id: l.layer.id, mask: true },
          label: `Fill mask · ${maskRegionLabel(l.region)}`,
        },
      );
    } catch (e) {
      history.setError(e.message);
    }
  }
  return {
    mode,
    state,
    region,
    draft,
    maskSelected,
    available,
    change,
    selectScope,
    fillRegion,
    cancel,
    clearRegion: () => {
      cancel();
      setRegion(null);
    },
    editor: { active, region, tool: state.tool, onStart, onMove, onEnd },
    preview: useMemo(
      () => ({
        doc,
        layerId: layer?.id,
        mask: draft && draft.layerId === layer?.id ? draft.mask : layer?.mask,
        decalId,
        region,
        preview: state.preview,
      }),
      [doc, layer?.id, layer?.mask, draft, decalId, region, state.preview],
    ),
  };
}
