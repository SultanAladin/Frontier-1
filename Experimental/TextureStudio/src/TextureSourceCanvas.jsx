import React, { useEffect, useRef, useState } from "react";
import { Brush, Expand, X } from "lucide-react";
import {
  paintTool,
  normalizePaintSettings,
  toolArtURL,
} from "./paintToolCatalogue.js";
import { normalizeTextureSource } from "./textureSource.js";
import {
  SOURCE_STROKE_POINT_LIMIT,
  SOURCE_STROKE_SCHEMA,
  sourceStrokeOptions,
  paintSourceStroke,
} from "./textureStrokes.js";
const canvasOf = (w, h) => {
  const canvas = document.createElement("canvas");
  canvas.width = w;
  canvas.height = h;
  return canvas;
};
export default function TextureSourceCanvas({
  layerId,
  layerName,
  channelId,
  channelLabel,
  texture,
  painting,
  disabled,
  onStroke,
  onClose,
  onOpenTools,
}) {
  const canvas = useRef(null),
    base = useRef(null),
    mark = useRef(null),
    gesture = useRef(null),
    frame = useRef(null),
    scope = useRef(0);
  const latest = useRef({ disabled, onStroke, texture });
  latest.current = { disabled, onStroke, texture };
  const [ready, setReady] = useState(false),
    [error, setError] = useState("");
  useEffect(() => {
    const token = ++scope.current;
    gesture.current = null;
    cancelAnimationFrame(frame.current);
    setReady(false);
    setError("");
    const c = canvas.current,
      w = texture?.width || 256,
      h = texture?.height || 256;
    c.width = w;
    c.height = h;
    base.current = canvasOf(w, h);
    mark.current = canvasOf(w, h);
    const done = () => {
      if (token !== scope.current) return;
      c.getContext("2d").clearRect(0, 0, w, h);
      c.getContext("2d").drawImage(base.current, 0, 0);
      setReady(true);
    };
    if (!texture) {
      done();
      return () => {
        ++scope.current;
      };
    }
    const image = new Image();
    image.onload = () => {
      if (token !== scope.current) return;
      base.current.getContext("2d").drawImage(image, 0, 0, w, h);
      done();
    };
    image.onerror = () => {
      if (token === scope.current)
        setError(
          "This stored source could not be decoded. Replace it with a valid raster.",
        );
    };
    image.src = texture.dataUrl;
    return () => {
      ++scope.current;
      image.onload = null;
      image.onerror = null;
      gesture.current = null;
      cancelAnimationFrame(frame.current);
    };
  }, [layerId, channelId, texture?.dataUrl, disabled]);
  const sample = (event) => {
    const bounds = canvas.current.getBoundingClientRect();
    return {
      x: Math.min(1, Math.max(0, (event.clientX - bounds.left) / bounds.width)),
      y: Math.min(1, Math.max(0, (event.clientY - bounds.top) / bounds.height)),
      pressure:
        event.pointerType === "pen"
          ? Math.min(1, Math.max(0, event.pressure))
          : 1,
    };
  };
  function draw() {
    frame.current = null;
    const g = gesture.current;
    if (g)
      paintSourceStroke(
        canvas.current,
        base.current,
        mark.current,
        g.points,
        g.settings,
      );
  }
  function add(event) {
    const g = gesture.current;
    if (!g || g.pointerId !== event.pointerId) return;
    const events = event.nativeEvent?.getCoalescedEvents?.() || [event];
    for (const e of events) {
      if (g.points.length >= SOURCE_STROKE_POINT_LIMIT) break;
      const point = sample(e),
        last = g.points.at(-1);
      if (
        Math.hypot(point.x - last.x, point.y - last.y) > 0.001 ||
        Math.abs(point.pressure - last.pressure) > 0.05
      )
        g.points.push(point);
    }
    if (frame.current === null) frame.current = requestAnimationFrame(draw);
  }
  function cancel() {
    cancelAnimationFrame(frame.current);
    frame.current = null;
    gesture.current = null;
    const c = canvas.current;
    if (c && base.current) {
      const ctx = c.getContext("2d");
      ctx.clearRect(0, 0, c.width, c.height);
      ctx.drawImage(base.current, 0, 0);
    }
  }
  function finish(e) {
    const g = gesture.current;
    if (!g || g.pointerId !== e.pointerId) return;
    // Pointer-up pressure is normally zero; retain the last contact sample.
    const end = sample(e);
    end.pressure = g.points.at(-1).pressure;
    if (
      g.points.length < SOURCE_STROKE_POINT_LIMIT &&
      Math.hypot(end.x - g.points.at(-1).x, end.y - g.points.at(-1).y) > 0.001
    )
      g.points.push(end);
    cancelAnimationFrame(frame.current);
    frame.current = null;
    draw();
    gesture.current = null;
    if (
      latest.current.disabled ||
      g.source !== latest.current.texture?.dataUrl ||
      g.scope !== scope.current
    ) {
      cancel();
      return;
    }
    const c = canvas.current;
    // Zero opacity/flow, erasing empty pixels and pressure-zero gestures are no-ops.
    const changed = c
        .getContext("2d")
        .getImageData(0, 0, c.width, c.height).data,
      before = base.current
        .getContext("2d")
        .getImageData(0, 0, c.width, c.height).data;
    if (!changed.some((v, i) => v !== before[i])) {
      cancel();
      return;
    }
    const opts = sourceStrokeOptions(g.settings),
      preview = canvasOf(c.width, c.height);
    preview.getContext("2d").globalAlpha = opts.alpha;
    preview.getContext("2d").drawImage(mark.current, 0, 0);
    const source = normalizeTextureSource({
        origin: "paint",
        name: `${layerName} · ${channelLabel} source`,
        width: c.width,
        height: c.height,
        dataUrl: c.toDataURL("image/png"),
      }),
      image = normalizeTextureSource({
        name:
          opts.operation === "erase"
            ? "Erase stroke footprint"
            : "Paint stroke",
        width: c.width,
        height: c.height,
        dataUrl: preview.toDataURL("image/png"),
      });
    if (!source || !image) {
      setError(
        "This raster exceeds the portable source limit. Choose a smaller source.",
      );
      cancel();
      return;
    }
    const stroke = {
      schema: SOURCE_STROKE_SCHEMA,
      id: crypto.randomUUID(),
      layerId,
      layerName,
      channelId,
      settings: g.settings,
      points: g.points,
      image,
    };
    const saved = latest.current.onStroke(stroke, source, g.source);
    if (!saved) {
      cancel();
      return;
    }
    base.current.getContext("2d").clearRect(0, 0, c.width, c.height);
    base.current.getContext("2d").drawImage(c, 0, 0);
  }
  const tool = paintTool(painting.toolId);
  return (
    <div
      className="tp-source-paint"
      aria-label={`${channelLabel} texture source editor`}
    >
      <header>
        <span>
          <Brush size={11} /> 2D texture source
        </span>
        <button
          aria-label={`Close ${channelLabel} texture canvas`}
          onClick={onClose}
        >
          <X size={12} />
        </button>
      </header>
      <button
        className="tp-source-active-tool"
        onClick={onOpenTools}
        title="Choose a painting instrument"
      >
        <img src={toolArtURL(tool.id, true)} alt="" />
        <span>
          {tool.label}
          <small>
            {sourceStrokeOptions(painting).operation === "erase"
              ? "Erase"
              : "Round tip"}{" "}
            · {sourceStrokeOptions(painting).size} px
          </small>
        </span>
        <Expand size={12} />
      </button>
      <canvas
        ref={canvas}
        tabIndex={0}
        aria-label={`${channelLabel} texture canvas`}
        aria-disabled={disabled || !ready}
        style={{
          aspectRatio: `${texture?.width || 256} / ${texture?.height || 256}`,
          cursor: disabled ? "not-allowed" : "crosshair",
        }}
        onPointerDown={(e) => {
          if (disabled || !ready || e.button !== 0 || gesture.current) return;
          e.preventDefault();
          canvas.current.focus();
          canvas.current.setPointerCapture(e.pointerId);
          gesture.current = {
            pointerId: e.pointerId,
            points: [sample(e)],
            settings: normalizePaintSettings(painting),
            source: texture?.dataUrl,
            scope: scope.current,
          };
          setError("");
          draw();
        }}
        onPointerMove={add}
        onPointerUp={finish}
        onPointerCancel={cancel}
        onLostPointerCapture={() => {
          if (gesture.current) cancel();
        }}
        onKeyDown={(e) => {
          if (e.key === "Escape" && gesture.current) {
            e.preventDefault();
            e.stopPropagation();
            cancel();
          }
        }}
      />
      <p>
        Draw directly on the source. One gesture = one undo step. Basic
        round-tip paint / erase; instrument-specific simulation and teapot UV
        compositing are not active.
      </p>
      {!ready && !error && <small>Preparing source…</small>}
      {error && (
        <p role="alert" className="tp-source-paint-error">
          {error}
        </p>
      )}
    </div>
  );
}
