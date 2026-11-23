import {
  normalizeScreenProjection,
  screenOperationHasCoverage,
} from "./screenPaintProjection.js";
import { useEffect, useMemo, useRef, useState } from "react";
import {
  normalizeDigitalBrush,
  appendDigitalBrushSample,
} from "./digitalBrush.js";
import {
  digitalBrushCursor,
  digitalPointerPressure,
} from "./digitalBrushCursor.js";
import {
  DIGITAL_PAINT_SCHEMA,
  PAINT_POINT_LIMIT,
  digitalChannelValues,
  normalizeDigitalPaint,
} from "./textureDigitalPaint.js";
import {
  isTextureLayerLocked,
  isTextureLayerVisible,
  patchTextureLayer,
} from "./textureDocument.js";
import {
  maskRegionContains,
  maskRegionFromHit,
  normalizeMaskPaint,
} from "./textureMaskPaint.js";
import { AUTHORING_UV_SET } from "./textureSurfaceModel.js";
import { uvTopology, uvSurfaceHit } from "./textureUVGeometry.js";
import { digitalStrokeThumbnail } from "./digitalPaintRaster.js";
import {
  digitalStrokeThumbnailGpu,
  digitalScreenCoverageGpu,
} from "./digitalPaintGpu.js";

const identity = () => crypto.randomUUID();
function maskOperations(ops, ink) {
  return ops.map((op) =>
    op.kind === "fill"
      ? {
          kind: "region",
          region: op.region,
          value: ink,
          opacity: op.brush.opacity,
        }
      : {
          kind: "brush",
          region: op.region,
          value: ink,
          opacity: op.brush.opacity,
          radius: Math.max(0.002, op.brush.size / 512),
          hardness: op.brush.hardness,
          points: op.points.map((p) => p.slice(0, 2)),
          digital: {
            brush: op.brush,
            seed: op.seed,
            operation: op.kind,
            pressure: op.points.map((p) => p[2]),
            ...(op.projection ? { projection: op.projection } : {}),
          },
        },
  );
}
export default function useDigitalPainting(
  history,
  {
    geometry,
    viewMode,
    source,
    suspended,
    maskRegion,
    maskValue = 1,
    onActivate,
  },
) {
  const { doc, selection } = history;
  const layer = doc.layers.find((l) => l.id === selection.id),
    mask = !!(selection.mask && layer?.mask);
  const [armed, setArmed] = useState(false),
    [draft, setDraft] = useState(null);
  const gesture = useRef(null),
    frame = useRef(0),
    latest = useRef(null),
    cursorSeed = useRef(null);
  if (cursorSeed.current === null)
    cursorSeed.current = crypto.getRandomValues(new Uint32Array(1))[0];
  const brush = doc.digitalPainting;
  const available =
    !!layer &&
    (mask || layer.kind !== "folder") &&
    !isTextureLayerLocked(doc, layer.id) &&
    isTextureLayerVisible(doc, layer.id) &&
    viewMode !== "wireframe" &&
    !(viewMode === "uv" && source) &&
    (mask || layer.channels.some((c) => c !== "normal"));
  const active = armed && available && !suspended;
  latest.current = {
    doc,
    layer,
    selection,
    mask,
    active,
    brush,
    maskRegion,
    maskValue,
  };
  function cancel() {
    gesture.current = null;
    cancelAnimationFrame(frame.current);
    frame.current = 0;
    setDraft(null);
  }
  function deactivate() {
    cancel();
    setArmed(false);
  }
  function arm(tool = brush.tool) {
    cancel();
    onActivate();
    if (tool !== brush.tool)
      changeBrush({ ...brush, tool }, { discrete: true });
    setArmed(true);
  }
  function changeBrush(value, { discrete = false } = {}) {
    cancel();
    const current = history.docRef.current,
      next = normalizeDigitalBrush(value);
    const keys = Object.keys(next).filter(
      (k) => next[k] !== current.digitalPainting[k],
    );
    history.commit(
      { ...current, digitalPainting: next },
      {
        key: discrete ? undefined : `digital-brush-${keys.join("-")}`,
      },
    );
  }
  const targetKey = `${selection.id}:${mask}`;
  useEffect(() => {
    const g = gesture.current;
    if (
      g &&
      (g.ink !== maskValue ||
        JSON.stringify(g.selectedRegion) !== JSON.stringify(maskRegion) ||
        !active ||
        g.targetKey !== targetKey ||
        JSON.stringify(mask ? layer?.mask : layer?.paint) !== g.expected ||
        JSON.stringify(layer?.channels) !== g.expectedChannels ||
        JSON.stringify(layer?.channelSettings) !== g.expectedSettings ||
        JSON.stringify(brush) !== g.expectedBrush)
    )
      cancel();
  }, [
    active,
    targetKey,
    layer?.mask,
    layer?.paint,
    layer?.channels,
    layer?.channelSettings,
    brush,
    maskValue,
    maskRegion,
  ]);
  useEffect(() => {
    cancel();
  }, [viewMode, source]);
  useEffect(() => {
    if (suspended) deactivate();
  }, [suspended]);
  useEffect(() => {
    const escape = (e) => {
        if (e.key === "Escape") cancel();
      },
      hidden = () => {
        if (document.visibilityState === "hidden") cancel();
      };
    window.addEventListener("keydown", escape);
    window.addEventListener("blur", cancel);
    document.addEventListener("visibilitychange", hidden);
    return () => {
      window.removeEventListener("keydown", escape);
      window.removeEventListener("blur", cancel);
      document.removeEventListener("visibilitychange", hidden);
      cancelAnimationFrame(frame.current);
      gesture.current = null;
    };
  }, []);
  function updatedLayer(owner, ops, g) {
    if (g.mask)
      return {
        ...owner,
        mask: {
          ...owner.mask,
          paint: normalizeMaskPaint({
            uvSet: AUTHORING_UV_SET,
            operations: [
              ...(owner.mask.paint?.operations || []),
              ...maskOperations(ops, g.ink),
            ],
          }),
        },
      };
    return {
      ...owner,
      paint: normalizeDigitalPaint({
        schema: DIGITAL_PAINT_SCHEMA,
        uvSet: AUTHORING_UV_SET,
        operations: [...(owner.paint?.operations || []), ...ops],
      }),
    };
  }
  function publish() {
    const g = gesture.current;
    if (!g) return;
    try {
      const owner = updatedLayer(g.owner, g.operations, g);
      setDraft({
        layerId: owner.id,
        paint: owner.paint,
        mask: g.mask ? owner.mask : null,
      });
    } catch (error) {
      cancel();
      history.setError(error.message);
    }
  }
  function queue() {
    if (!frame.current)
      frame.current = requestAnimationFrame(() => {
        frame.current = 0;
        publish();
      });
  }
  function addSample(hit, event) {
    const g = gesture.current;
    if (!g) return;
    if (g.projection) {
      if (!event?.screenPosition) return;
      const sample = [
        ...event.screenPosition.map((n) => n / 256),
        digitalPointerPressure(event),
      ];
      let op = g.operations.at(-1);
      if (op?.points.at(-1)?.every((n, i) => Math.abs(n - sample[i]) < 1e-9))
        return;
      if (!op || op.points.length >= PAINT_POINT_LIMIT) {
        op = {
          id: identity(),
          kind: g.brush.tool,
          region: g.target,
          seed: (g.seed + g.operations.length) >>> 0,
          brush: g.brush,
          channels: g.channels,
          points: [],
          projection: g.projection,
        };
        g.operations.push(op);
      }
      appendDigitalBrushSample(op.points, sample, g.brush);
      return;
    }
    const triangle = hit && uvTopology(geometry).triangles[hit.faceIndex];
    if (!hit || !maskRegionContains(g.target, triangle)) {
      g.chartId = null;
      return;
    }
    const sample = [...hit.uvPosition, digitalPointerPressure(event)];
    let op = g.operations.at(-1);
    // In particular, do not split a full 512-point segment just because an
    // identical pointer-up sample arrived at its already exact endpoint.
    if (
      g.chartId === hit.chartId &&
      op?.points.at(-1)?.every((n, i) => Math.abs(n - sample[i]) < 1e-9)
    )
      return;
    if (
      !op ||
      g.chartId !== hit.chartId ||
      op.points.length >= PAINT_POINT_LIMIT
    ) {
      const region =
        g.target.kind === "face"
          ? g.target
          : { kind: "island", partId: g.target.partId, id: hit.chartId };
      op = {
        id: identity(),
        kind: g.brush.tool,
        region,
        seed: (g.seed + g.operations.length) >>> 0,
        brush: g.brush,
        channels: g.channels,
        points: [],
      };
      g.operations.push(op);
      g.chartId = hit.chartId;
    }
    appendDigitalBrushSample(op.points, sample, g.brush, (point) => {
      const candidate = uvSurfaceHit(geometry, point[0], point[1]);
      return (
        candidate &&
        maskRegionContains(
          op.region,
          uvTopology(geometry).triangles[candidate.faceIndex],
        )
      );
    });
  }
  // Preview and deposition share target validation and the seed of the next
  // gesture, so a stationary click gets the tip shown before pressing.
  function targetOf(l, hit, event) {
    if (l.active && l.brush.tool !== "fill" && event?.projection) {
      return (
        (l.mask && l.maskRegion) ||
        (l.layer.surfaceTarget
          ? { kind: "object", partId: l.layer.surfaceTarget.partId }
          : { kind: "surface" })
      );
    }
    if (
      !l.active ||
      !hit ||
      (l.brush.tool === "fill" &&
        l.brush.fillScope === "tile" &&
        viewMode !== "uv")
    )
      return null;
    const target =
      (l.mask && l.maskRegion) ||
      maskRegionFromHit(
        hit,
        l.brush.tool === "fill" ? l.brush.fillScope : "object",
      );
    if (
      !target ||
      !maskRegionContains(
        target,
        uvTopology(geometry).triangles[hit.faceIndex],
      ) ||
      (l.layer.surfaceTarget && target.partId !== l.layer.surfaceTarget.partId)
    )
      return null;
    return target;
  }
  function getCursor(hit, event) {
    const l = latest.current,
      g = gesture.current;
    const target = g?.target || targetOf(l, hit, event);
    if (!l.active || !target) return null;
    const channels =
      g?.channels ||
      (l.mask
        ? { baseColor: l.maskValue ? "#ffffff" : "#000000" }
        : digitalChannelValues(l.layer, l.brush));
    if (!Object.keys(channels).length) return null;
    const op =
      g?.projection || g?.chartId === hit?.chartId
        ? g?.operations.at(-1)
        : null;
    return digitalBrushCursor({
      hit,
      geometry,
      target,
      brush: g?.brush || l.brush,
      channels,
      seed: op?.seed ?? cursorSeed.current,
      points: op?.points,
      mask: l.mask,
      ink: l.maskValue,
      pressure: digitalPointerPressure(event, true),
      projection: g?.projection || event?.projection,
      screenPosition: event?.screenPosition,
    });
  }

  function commit(ops, g) {
    const current = history.docRef.current,
      owner = current.layers.find((l) => l.id === g.owner.id),
      l = latest.current;
    if (
      !owner ||
      !l.active ||
      l.selection.id !== owner.id ||
      l.mask !== g.mask ||
      isTextureLayerLocked(current, owner.id) ||
      !isTextureLayerVisible(current, owner.id) ||
      JSON.stringify(g.mask ? owner.mask : owner.paint) !== g.expected ||
      JSON.stringify(owner.channels) !== g.expectedChannels ||
      JSON.stringify(owner.channelSettings) !== g.expectedSettings
    )
      return false;
    try {
      const updated = updatedLayer(owner, ops, g);
      const thumbnailOps = ops.map((op) => ({
        ...op,
        brush: g.mask
          ? { ...op.brush, color: g.ink ? "#ffffff" : "#000000" }
          : op.brush,
      }));
      const record = {
        id: identity(),
        layerId: owner.id,
        layerName: owner.name,
        mask: g.mask,
        operation: g.brush.tool,
        channels: g.mask ? [] : Object.keys(g.channels),
        brush: g.brush,
        image:
          digitalStrokeThumbnailGpu(thumbnailOps, geometry, g.mask) ??
          digitalStrokeThumbnail(thumbnailOps, geometry, g.mask),
      };
      return history.commit(
        {
          ...patchTextureLayer(
            current,
            owner.id,
            g.mask ? { mask: updated.mask } : { paint: updated.paint },
          ),
          lastDigitalStroke: record,
          lastStroke: null,
        },
        {
          selection: { id: owner.id, mask: g.mask },
          label: `${g.brush.tool === "erase" ? "Erase" : g.brush.tool === "fill" ? "Fill" : "Paint"} ${g.mask ? "mask" : "channels"} · ${owner.name}`,
        },
      );
    } catch (error) {
      history.setError(error.message);
      return false;
    }
  }
  function onStart(hit, event) {
    const l = latest.current;
    if (
      l.brush.tool === "fill" &&
      l.brush.fillScope === "tile" &&
      viewMode !== "uv"
    )
      return false;
    if (
      !l.active ||
      (!hit && !event?.projection) ||
      gesture.current ||
      l.brush.opacity <= 0 ||
      (l.brush.tool !== "fill" && (l.brush.size <= 0 || l.brush.flow <= 0))
    )
      return false;
    const target = targetOf(l, hit, event);
    if (!target) return false;
    const channels = l.mask
      ? { baseColor: l.maskValue ? "#ffffff" : "#000000" }
      : digitalChannelValues(l.layer, l.brush);
    if (!Object.keys(channels).length) return false;
    if (
      l.brush.tool === "erase" &&
      !(l.mask
        ? l.layer.mask.paint?.operations.length
        : l.layer.paint?.operations.length)
    )
      return false;
    const g = {
      owner: l.layer,
      targetKey,
      mask: l.mask,
      ink: l.maskValue,
      channels,
      target,
      brush: { ...l.brush },
      seed: cursorSeed.current,
      selectedRegion: l.maskRegion,
      expected: JSON.stringify(l.mask ? l.layer.mask : l.layer.paint),
      expectedChannels: JSON.stringify(l.layer.channels),
      expectedSettings: JSON.stringify(l.layer.channelSettings),
      expectedBrush: JSON.stringify(l.brush),
      chartId: null,
      projection:
        l.brush.tool !== "fill" && event?.projection
          ? normalizeScreenProjection(event.projection)
          : null,
      operations: [],
    };
    if (l.brush.tool === "fill") {
      const op = {
        id: identity(),
        kind: "fill",
        region: target,
        seed: g.seed,
        brush: g.brush,
        channels,
        points: [],
      };
      commit([op], g);
      cursorSeed.current = crypto.getRandomValues(new Uint32Array(1))[0];
      return false;
    }
    gesture.current = g;
    addSample(hit, event);
    queue();
    return true;
  }
  function onMove(hit, event) {
    if (gesture.current) {
      addSample(hit, event);
      queue();
    }
  }
  function onEnd(cancelled, hit, event) {
    const g = gesture.current;
    if (!g) return;
    if (!cancelled && event && (hit || g.projection)) {
      // A pointer-up can arrive at a newer location than the final pointermove.
      // Keep the last contact pressure when a pen reports zero on lift-off.
      const pressure =
        event.pointerType !== "mouse" && event.pressure === 0 && !event.buttons
          ? (g.operations.at(-1)?.points.at(-1)?.[2] ?? 1)
          : event.pressure;
      addSample(hit, { ...event, pointerType: event.pointerType, pressure });
    }
    cancel();
    if (!cancelled) {
      const ops = g.projection
        ? g.operations.filter(
            (op) =>
              digitalScreenCoverageGpu(op, geometry) ??
              screenOperationHasCoverage(op, geometry),
          )
        : g.operations;
      if (ops.length) commit(ops, g);
      cursorSeed.current = crypto.getRandomValues(new Uint32Array(1))[0];
    }
  }
  return {
    brush,
    armed,
    available,
    active,
    draft,
    arm,
    deactivate,
    cancel,
    changeBrush,
    editor: {
      digital: true,
      brush,
      getCursor,
      active,
      tool: brush.tool,
      region: mask ? maskRegion : null,
      onStart,
      onMove,
      onEnd,
    },
    document: useMemo(
      () =>
        !draft || draft.mask
          ? doc
          : {
              ...doc,
              layers: doc.layers.map((l) =>
                l.id === draft.layerId ? { ...l, paint: draft.paint } : l,
              ),
            },
      [doc, draft],
    ),
  };
}
