import { uvScreenProjection, screenPointer } from "./screenPaintProjection.js";
import {
  drawUVBrushCursor,
  digitalCursorLabel,
  uvCanvasPoint,
  uvAtCanvasPoint,
} from "./digitalBrushCursor.js";
import { maskGeometryCache } from "./maskPaintRaster.js";
import React, { useEffect, useMemo, useRef, useState } from "react";
import {
  uvTopology,
  uvSurfaceHit,
  projectArtworkGeometry,
  barycentricUV,
  uvTile,
  uvBoundaryLoops,
} from "./textureUVGeometry.js";
import {
  TEXTURE_PARTS,
  texturePart,
  readAssetDrop,
} from "./textureSurfaceModel.js";
import { collectDecalRecords } from "./decalModel.js";
import { isTextureLayerVisible, textureAncestors } from "./textureDocument.js";
import { decalLocalProjector } from "./decalProjection.js";

const clamp = (n, a, b) => Math.max(a, Math.min(b, n));
function drawTriangleImage(ctx, image, destination, artwork) {
  const source = artwork.map(([u, v]) => [
      u * image.naturalWidth,
      (1 - v) * image.naturalHeight,
    ]),
    [a, b, c] = source,
    [p, q, r] = destination,
    determinant = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]);
  if (Math.abs(determinant) < 1e-10) return;
  const ax =
      ((q[0] - p[0]) * (c[1] - a[1]) - (r[0] - p[0]) * (b[1] - a[1])) /
      determinant,
    cx =
      ((r[0] - p[0]) * (b[0] - a[0]) - (q[0] - p[0]) * (c[0] - a[0])) /
      determinant,
    by =
      ((q[1] - p[1]) * (c[1] - a[1]) - (r[1] - p[1]) * (b[1] - a[1])) /
      determinant,
    dy =
      ((r[1] - p[1]) * (b[0] - a[0]) - (q[1] - p[1]) * (c[0] - a[0])) /
      determinant;
  ctx.save();
  ctx.beginPath();
  ctx.moveTo(...p);
  ctx.lineTo(...q);
  ctx.lineTo(...r);
  ctx.closePath();
  ctx.clip();
  ctx.transform(
    ax,
    by,
    cx,
    dy,
    p[0] - ax * a[0] - cx * a[1],
    p[1] - by * a[0] - dy * a[1],
  );
  ctx.drawImage(image, 0, 0);
  ctx.restore();
}
function footprintAt(footprint, point) {
  const uv = footprint.attributes.uvAuthoring;
  for (let i = 0; i < uv.count; i += 3)
    if (
      barycentricUV(point, {
        points: [0, 1, 2].map((j) => [uv.getX(i + j), uv.getY(i + j)]),
      })
    )
      return true;
  return false;
}
export default function TextureUVView({
  geometry,
  doc,
  editor,
  maskEditor,
  maskAtlas,
  appearanceAtlas,
  selectedPart,
  onPartSelect,
  onAssetDrop,
  source,
  resetToken,
  zoom,
  grid,
  onZoomChange,
  onError,
  onOpenPaintTools,
}) {
  const canvasRef = useRef(null),
    hostRef = useRef(null),
    brushCursorCanvas = useRef(null),
    brushCursorReadout = useRef(null),
    cursorPointer = useRef(null),
    displayed = useRef({
      size: { width: 1, height: 1 },
      camera: { scale: 100, x: 0, y: 0 },
    }),
    cameraRef = useRef({ scale: 100, x: 0, y: 0 }),
    drag = useRef(null),
    latest = useRef(null),
    images = useRef(new Map()),
    frameScale = useRef(100),
    spaceHeld = useRef(false);
  const [size, setSize] = useState({ width: 1, height: 1 }),
    [camera, setCamera] = useState(cameraRef.current),
    [hover, setHover] = useState(null),
    [imageVersion, setImageVersion] = useState(0);
  cameraRef.current = camera;
  const topology = useMemo(
    () => uvTopology(geometry, source),
    [geometry, source],
  );
  const paths = useMemo(() => {
    const parts = new Map();
    for (const triangle of topology.triangles) {
      const key = triangle.part?.id || "mesh",
        record = parts.get(key) || {
          fill: new Path2D(),
          wire: new Path2D(),
          part: triangle.part,
        };
      record.wire.moveTo(...triangle.points[0]);
      record.wire.lineTo(...triangle.points[1]);
      record.wire.lineTo(...triangle.points[2]);
      record.wire.closePath();
      parts.set(key, record);
    }
    for (const loop of uvBoundaryLoops(geometry, source)) {
      const path = parts.get(loop.part?.id || "mesh")?.fill;
      if (!path) continue;
      path.moveTo(...loop.points[0]);
      for (const point of loop.points.slice(1)) path.lineTo(...point);
      path.closePath();
    }
    return parts;
  }, [topology, geometry, source]);
  const footprints = useMemo(() => {
    if (source) return [];
    return collectDecalRecords(doc, isTextureLayerVisible)
      .map((record) => ({
        ...record,
        opacity: [
          ...textureAncestors(doc, record.layerId),
          record.layer,
        ].reduce((v, l) => (v * l.opacity) / 100, record.decal.opacity),
      }))
      .filter((record) => record.opacity > 0)
      .map((record) => ({
        ...record,
        footprint: projectArtworkGeometry(
          geometry,
          record.decal,
          record.decal.space === "uv"
            ? null
            : decalLocalProjector({ geometry }, record.decal),
        ),
      }));
  }, [geometry, doc, source]);
  useEffect(
    () => () => footprints.forEach((r) => r.footprint.dispose()),
    [footprints],
  );
  latest.current = {
    maskEditor,
    editor,
    selectedPart,
    source,
    footprints,
    onPartSelect,
    onAssetDrop,
    onError,
  };
  function changeCamera(value) {
    cameraRef.current = value;
    setCamera(value);
    onZoomChange?.(Math.round((value.scale / frameScale.current) * 100));
  }
  function cancel(e) {
    const d = drag.current;
    // A second finger's cancellation/lost capture cannot end the first gesture.
    if (e?.pointerId !== undefined && d?.pointerId !== e.pointerId) return;
    drag.current = null;
    if (d?.kind === "mask") latest.current.maskEditor?.onEnd(true);
    if (d?.kind === "transform") latest.current.editor?.onTransformEnd?.(true);
    if (d && canvasRef.current?.hasPointerCapture(d.pointerId))
      canvasRef.current.releasePointerCapture(d.pointerId);
    spaceHeld.current = false;
  }
  function frame(partId = null) {
    const rect = size;
    if (rect.width <= 1 || rect.height <= 1) return;
    const part = !source && texturePart(partId),
      width = part ? 1 : source ? 1 : 4,
      top = rect.height < 260 ? 64 : 120,
      bottom = 62,
      availableHeight = Math.max(24, rect.height - top - bottom),
      scale = Math.max(
        12,
        Math.min((rect.width - 52) / width, availableHeight),
      );
    frameScale.current = scale;
    changeCamera({
      scale,
      x: rect.width / 2 - (part ? part.u + 0.5 : width / 2) * scale,
      y: top + availableHeight / 2 + scale / 2,
    });
  }
  useEffect(() => {
    const observer = new ResizeObserver((entries) => {
      const { width, height } = entries[0].contentRect;
      cancel();
      setSize({
        width: Math.max(1, Math.round(width)),
        height: Math.max(1, Math.round(height)),
      });
    });
    observer.observe(hostRef.current);
    return () => {
      observer.disconnect();
      cancel();
    };
  }, []);
  useEffect(() => {
    frame();
  }, [size.width, size.height, source]);
  useEffect(() => {
    cancel();
    frame(selectedPart);
  }, [resetToken]);
  useEffect(() => {
    if (!zoom) return;
    const old = cameraRef.current,
      scale = clamp(old.scale * (zoom.direction > 0 ? 1.25 : 0.8), 12, 4096),
      rect = size;
    changeCamera({
      scale,
      x: rect.width / 2 + ((old.x - rect.width / 2) * scale) / old.scale,
      y: rect.height / 2 + ((old.y - rect.height / 2) * scale) / old.scale,
    });
  }, [zoom]);
  useEffect(() => {
    const canvas = canvasRef.current;
    function wheel(e) {
      e.preventDefault();
      cancel();
      const [x, y] = logicalPoint(e),
        old = cameraRef.current,
        scale = clamp(
          old.scale * Math.exp(-clamp(e.deltaY, -120, 120) * 0.0025),
          12,
          4096,
        );
      changeCamera({
        scale,
        x: x + ((old.x - x) * scale) / old.scale,
        y: y + ((old.y - y) * scale) / old.scale,
      });
    }
    const blur = () => {
      cancel();
      clearCursor();
    };
    const visibility = () => {
      if (document.visibilityState === "hidden") {
        cancel();
        clearCursor();
      }
    };
    const escape = (e) => {
      if (e.key === "Escape") {
        cancel();
        clearCursor();
      }
    };
    window.addEventListener("keydown", escape);
    document.addEventListener("visibilitychange", visibility);
    canvas.addEventListener("wheel", wheel, { passive: false });
    window.addEventListener("blur", blur);
    return () => {
      canvas.removeEventListener("wheel", wheel);
      window.removeEventListener("keydown", escape);
      window.removeEventListener("blur", blur);
      document.removeEventListener("visibilitychange", visibility);
      cancel();
    };
  }, []);
  useEffect(() => {
    let alive = true;
    const ids = new Set(doc.stamps.map((s) => s.id));
    for (const id of images.current.keys())
      if (!ids.has(id)) images.current.delete(id);
    for (const stamp of doc.stamps) {
      let record = images.current.get(stamp.id);
      if (record?.source !== stamp.raster.dataUrl) {
        const image = new Image();
        image.src = stamp.raster.dataUrl;
        record = { image, source: stamp.raster.dataUrl, ready: false };
        record.promise = image.decode().then(() => {
          record.ready = true;
        });
        images.current.set(stamp.id, record);
      }
      if (!record.ready)
        record.promise
          .then(() => {
            if (alive) setImageVersion((v) => v + 1);
          })
          .catch(() => {
            if (alive) onError?.("A UV stamp preview could not be decoded.");
          });
    }
    return () => {
      alive = false;
    };
  }, [doc.stamps]);
  const background = useMemo(() => {
    const canvas = document.createElement("canvas"),
      dpr = Math.min(window.devicePixelRatio || 1, 2),
      { width, height } = size;
    canvas.width = Math.round(width * dpr);
    canvas.height = Math.round(height * dpr);
    const ctx = canvas.getContext("2d");
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.fillStyle = "#141719";
    ctx.fillRect(0, 0, width, height);
    const { scale, x, y } = camera;
    ctx.setTransform(dpr * scale, 0, 0, -dpr * scale, dpr * x, dpr * y);
    for (const tile of topology.tiles) {
      ctx.fillStyle = "#1d2226";
      ctx.fillRect(tile.u, tile.v, 1, 1);
      if (grid) {
        ctx.strokeStyle = "#ffffff08";
        ctx.lineWidth = 0.5 / scale;
        ctx.beginPath();
        for (let i = 1; i < 10; i++) {
          ctx.moveTo(tile.u + i / 10, tile.v);
          ctx.lineTo(tile.u + i / 10, tile.v + 1);
          ctx.moveTo(tile.u, tile.v + i / 10);
          ctx.lineTo(tile.u + 1, tile.v + i / 10);
        }
        ctx.stroke();
      }
      ctx.strokeStyle = "#5b626b";
      ctx.lineWidth = 1 / scale;
      ctx.strokeRect(tile.u, tile.v, 1, 1);
    }
    for (const [id, record] of paths) {
      ctx.fillStyle = `${record.part?.color || "#aaaaaa"}12`;
      ctx.fill(record.fill);
      ctx.strokeStyle =
        selectedPart === id
          ? `${record.part.color}aa`
          : `${record.part?.color || "#aaaaaa"}55`;
      ctx.lineWidth = (selectedPart === id ? 0.65 : 0.4) / scale;
      ctx.stroke(record.wire);
    }
    return canvas;
  }, [size, camera, topology, paths, selectedPart, grid]);
  useEffect(() => {
    const canvas = canvasRef.current,
      ctx = canvas.getContext("2d"),
      dpr = Math.min(window.devicePixelRatio || 1, 2),
      { width, height } = size;
    if (canvas.width !== Math.round(width * dpr))
      canvas.width = Math.round(width * dpr);
    if (canvas.height !== Math.round(height * dpr))
      canvas.height = Math.round(height * dpr);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.globalAlpha = 1;
    ctx.drawImage(background, 0, 0, width, height);
    const { scale, x, y } = camera;
    ctx.setTransform(dpr * scale, 0, 0, -dpr * scale, dpr * x, dpr * y);
    if ((maskAtlas || appearanceAtlas) && !source) {
      ctx.save();
      ctx.scale(1, -1);
      ctx.drawImage((maskAtlas || appearanceAtlas).canvas, 0, -1, 4, 1);
      ctx.restore();
    }
    if (maskEditor?.region && !source) {
      ctx.save();
      ctx.strokeStyle = "#ffd08a";
      ctx.lineWidth = 1.8 / scale;
      ctx.stroke(maskGeometryCache(geometry).path(maskEditor.region));
      ctx.restore();
    }
    let artTriangles = 0;
    for (const record of maskAtlas && !source ? [] : footprints) {
      const image = images.current.get(record.stamp.id),
        uv = record.footprint.attributes.uvAuthoring,
        art = record.footprint.attributes.uv;
      if (!image?.ready) continue;
      ctx.globalAlpha = appearanceAtlas
        ? 0
        : record.opacity * (record.decal.asMask ? 0.38 : 1);
      if (
        record.decal.space === "uv" &&
        paths.has(record.decal.target?.partId)
      ) {
        // One image draw clipped by the actual UV triangle union is equivalent
        // to per-triangle affine mapping for a UV projector, without hundreds
        // of tiny GPU clip/draw operations on every pointer event.
        const d = record.decal;
        ctx.save();
        ctx.clip(paths.get(d.target.partId).fill);
        ctx.translate(...d.uvPosition);
        ctx.rotate((d.roll * Math.PI) / 180);
        ctx.scale(d.size[0], -d.size[1]);
        ctx.drawImage(image.image, -0.5, -0.5, 1, 1);
        ctx.restore();
        artTriangles += uv.count / 3;
      } else
        for (let i = 0; i < uv.count; i += 3) {
          const points = [0, 1, 2].map((j) => [uv.getX(i + j), uv.getY(i + j)]),
            artwork = [0, 1, 2].map((j) => [art.getX(i + j), art.getY(i + j)]);
          drawTriangleImage(ctx, image.image, points, artwork);
          artTriangles++;
        }
      ctx.globalAlpha = 1;
      if (record.decal.id === editor?.id && record.decal.space === "uv") {
        const d = record.decal;
        ctx.save();
        ctx.translate(...d.uvPosition);
        ctx.rotate((d.roll * Math.PI) / 180);
        ctx.strokeStyle = d.locked ? "#aaa" : "#efc38b";
        ctx.lineWidth = 1.5 / scale;
        ctx.setLineDash([5 / scale, 4 / scale]);
        ctx.strokeRect(-d.size[0] / 2, -d.size[1] / 2, d.size[0], d.size[1]);
        ctx.setLineDash([]);
        const radius = 3.5 / scale;
        ctx.fillStyle = "#efc38b";
        for (const a of [-1, 1])
          for (const b of [-1, 1])
            ctx.fillRect(
              (a * d.size[0]) / 2 - radius,
              (b * d.size[1]) / 2 - radius,
              radius * 2,
              radius * 2,
            );
        ctx.restore();
      }
    }
    if (
      hover?.target &&
      (!editor?.surfaceTarget ||
        editor.surfaceTarget.partId === hover.target.partId) &&
      editor?.active &&
      ["brush", "place"].includes(editor.mode)
    ) {
      const b = editor.brush;
      ctx.save();
      ctx.translate(...hover.uvPosition);
      ctx.rotate(((b.roll || 0) * Math.PI) / 180);
      ctx.strokeStyle = "#ebcf9b";
      ctx.lineWidth = 1 / scale;
      ctx.setLineDash([4 / scale, 3 / scale]);
      ctx.strokeRect(-b.size[0] / 2, -b.size[1] / 2, ...b.size);
      ctx.restore();
    }
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.font = "11px 'DM Sans',sans-serif";
    for (const tile of topology.tiles) {
      const tx = x + tile.u * scale,
        ty = y - tile.v * scale;
      ctx.fillStyle = "#bac0c4";
      ctx.fillText(String(tile.id), tx + 7, ty + 20);
      const part = TEXTURE_PARTS.find((p) => p.tile === tile.id);
      if (!source && part && scale > 105) {
        ctx.fillStyle = part.color;
        ctx.fillText(part.name, tx + 7, ty + 35);
      }
    }
    displayed.current = { size, camera };
    canvas.dataset.maskPreview = maskAtlas ? "mask" : "material";
    canvas.dataset.maskRevision = String(maskAtlas?.revision || 0);
    canvas.dataset.layerAtlasRevision = String(appearanceAtlas?.revision || 0);
    canvas.dataset.layerAtlas = appearanceAtlas
      ? "ordered-lit-appearance"
      : "pending";
    canvas.dataset.uvCamera = JSON.stringify(camera);
    canvas.dataset.uvTiles = topology.tiles.map((t) => t.id).join(",");
    canvas.dataset.uvTriangles = String(topology.triangles.length);
    canvas.dataset.uvCharts = String(topology.charts.length);
    canvas.dataset.uvArtworkTriangles = String(artTriangles);
    canvas.dataset.uvProbes = JSON.stringify(
      topology.charts.map((chart) => {
        const uv = [
          (chart.min[0] + chart.max[0]) / 2,
          (chart.min[1] + chart.max[1]) / 2,
        ];
        return {
          chartId: chart.id,
          partId: chart.part?.id,
          uv,
          x: x + uv[0] * scale,
          y: y - uv[1] * scale,
        };
      }),
    );
  }, [
    size,
    camera,
    topology,
    paths,
    background,
    footprints,
    imageVersion,
    hover,
    selectedPart,
    editor,
    grid,
    maskAtlas,
    appearanceAtlas,
    maskEditor,
    source,
    geometry,
  ]);
  function logicalPoint(e) {
    return uvCanvasPoint(
      e.clientX,
      e.clientY,
      canvasRef.current.getBoundingClientRect(),
      displayed.current.size,
    );
  }
  function coordinates(e) {
    return uvAtCanvasPoint(logicalPoint(e), displayed.current.camera);
  }
  function hit(e) {
    const rect = canvasRef.current.getBoundingClientRect();
    if (
      e.clientX < rect.left ||
      e.clientX > rect.right ||
      e.clientY < rect.top ||
      e.clientY > rect.bottom
    )
      return null;
    const p = coordinates(e);
    return uvSurfaceHit(geometry, ...p, null, source);
  }
  function paintingPointer(e) {
    if (!latest.current.maskEditor?.digital) return e;
    const rect = canvasRef.current.getBoundingClientRect();
    return screenPointer(
      e,
      uvScreenProjection(
        displayed.current.camera,
        displayed.current.size,
        rect,
      ),
      rect,
    );
  }
  function rememberCursor(e) {
    cursorPointer.current = {
      ...paintingPointer(e),
      clientX: e.clientX,
      clientY: e.clientY,
      pointerType: e.pointerType,
      pressure: e.pressure,
      buttons: e.buttons,
      altKey: e.altKey,
      ctrlKey: e.ctrlKey,
      metaKey: e.metaKey,
    };
  }
  function clearCursor() {
    cursorPointer.current = null;
    setHover(null);
  }
  useEffect(() => {
    const pointer = cursorPointer.current;
    const cursor =
      !source &&
      pointer &&
      !spaceHeld.current &&
      !pointer.altKey &&
      !pointer.ctrlKey &&
      !pointer.metaKey &&
      drag.current?.kind !== "pan" &&
      maskEditor?.digital &&
      maskEditor.active
        ? maskEditor.getCursor(hit(pointer), paintingPointer(pointer))
        : null;
    drawUVBrushCursor(
      brushCursorCanvas.current,
      {
        ...displayed.current.size,
        screenWidth: canvasRef.current.getBoundingClientRect().width,
        screenHeight: canvasRef.current.getBoundingClientRect().height,
      },
      displayed.current.camera,
      cursor,
      geometry,
    );
    hostRef.current?.classList.toggle("digital-cursor-visible", !!cursor);
    const readout = brushCursorReadout.current;
    if (readout) {
      readout.hidden = !cursor;
      if (cursor) {
        const [x, y] = logicalPoint(pointer);
        readout.textContent = digitalCursorLabel(cursor);
        readout.style.left = `${Math.min(x, displayed.current.size.width * 0.7)}px`;
        readout.style.top = `${Math.min(y, displayed.current.size.height * 0.9)}px`;
      }
    }
  }, [hover, camera, size, maskEditor, source, geometry]);
  function pointerDown(e) {
    if (drag.current || e.isPrimary === false) {
      e.preventDefault();
      return;
    }
    rememberCursor(e);
    setHover(hit(e));
    const p = coordinates(e),
      current = latest.current;
    if (
      e.button === 1 ||
      (e.button === 0 &&
        (e.altKey ||
          spaceHeld.current ||
          (!current.editor?.active &&
            !current.maskEditor?.active &&
            e.pointerType === "touch")))
    ) {
      e.preventDefault();
      canvasRef.current.focus();
      e.currentTarget.setPointerCapture(e.pointerId);
      drag.current = {
        kind: "pan",
        pointerId: e.pointerId,
        x: logicalPoint(e)[0],
        y: logicalPoint(e)[1],
        before: { ...cameraRef.current },
      };
      return;
    }
    if (e.button !== 0) return;
    canvasRef.current.focus();
    const surface = hit(e);
    if (current.maskEditor?.active && !source) {
      e.preventDefault();
      e.stopPropagation();
      if (current.maskEditor.onStart(surface, paintingPointer(e))) {
        drag.current = { kind: "mask", pointerId: e.pointerId };
        e.currentTarget.setPointerCapture(e.pointerId);
      }
      return;
    }
    if (source) {
      if (surface?.target) onPartSelect(surface.target.partId);
      return;
    }
    if (
      current.editor?.active &&
      ["brush", "place"].includes(current.editor.mode)
    ) {
      if (surface) {
        e.preventDefault();
        current.editor.onStamp(surface);
        onPartSelect(surface.target?.partId);
      }
      return;
    }
    const candidate = [...current.footprints]
      .reverse()
      .find((r) => r.decal.visible && footprintAt(r.footprint, p));
    if (candidate?.decal.space === "uv") {
      current.editor?.onSelect?.(candidate.decal.id, candidate.layerId);
      if (
        current.editor.active &&
        current.editor.id === candidate.decal.id &&
        !candidate.decal.locked &&
        current.editor.mode === "transform"
      ) {
        e.preventDefault();
        e.currentTarget.setPointerCapture(e.pointerId);
        const decal = candidate.decal,
          centre = decal.uvPosition;
        drag.current = {
          kind: "transform",
          pointerId: e.pointerId,
          before: decal,
          expected: JSON.stringify(
            doc.layers
              .find((l) => l.id === candidate.layerId)
              ?.decals.find((d) => d.id === decal.id),
          ),
          point: p,
          mode: current.editor.gizmo,
          angle: Math.atan2(p[1] - centre[1], p[0] - centre[0]),
          distance: Math.max(
            0.001,
            Math.hypot(p[0] - centre[0], p[1] - centre[1]),
          ),
        };
      }
    } else if (surface?.target) onPartSelect(surface.target.partId);
  }
  function pointerMove(e) {
    const d = drag.current;
    if (d && d.pointerId !== e.pointerId) return;
    if (!d && e.pointerType === "touch") {
      clearCursor();
      return;
    }
    rememberCursor(e);
    const surface = hit(e);
    setHover(surface);
    if (d?.kind === "mask") {
      latest.current.maskEditor?.onMove(surface, paintingPointer(e));
      return;
    }
    if (d?.kind === "pan") {
      changeCamera({
        ...d.before,
        x: d.before.x + logicalPoint(e)[0] - d.x,
        y: d.before.y + logicalPoint(e)[1] - d.y,
      });
      return;
    }
    if (d?.kind === "transform") {
      const p = coordinates(e),
        b = d.before,
        part = texturePart(b.target?.partId);
      let patch;
      if (d.mode === "rotate") {
        let roll =
          b.roll +
          ((Math.atan2(p[1] - b.uvPosition[1], p[0] - b.uvPosition[0]) -
            d.angle) *
            180) /
            Math.PI;
        if (e.shiftKey) roll = Math.round(roll / 15) * 15;
        patch = { roll: ((((roll + 180) % 360) + 360) % 360) - 180 };
      } else if (d.mode === "scale") {
        let ratio =
          Math.hypot(p[0] - b.uvPosition[0], p[1] - b.uvPosition[1]) /
          d.distance;
        if (e.shiftKey) ratio = Math.round(ratio * 10) / 10;
        ratio = clamp(
          ratio,
          Math.max(0.001 / b.size[0], 0.001 / b.size[1]),
          Math.min(4 / b.size[0], 4 / b.size[1]),
        );
        patch = { size: b.size.map((n) => n * ratio) };
      } else {
        const value = b.uvPosition.map((n, i) => {
          const coordinate = n + p[i] - d.point[i];
          return e.shiftKey ? Math.round(coordinate * 100) / 100 : coordinate;
        });
        patch = {
          uvPosition: [
            clamp(value[0], part?.u ?? 0, part ? part.u + 0.99999 : 9.99999),
            clamp(value[1], 0, 0.99999),
          ],
        };
      }
      latest.current.editor.onPreviewTransform?.(patch, d.expected);
      return;
    }
  }
  function pointerUp(e) {
    const d = drag.current;
    if (!d || d.pointerId !== e.pointerId) {
      if (!d && e.pointerType === "touch") clearCursor();
      return;
    }
    if (e.pointerType === "touch") clearCursor();
    else {
      rememberCursor(e);
      setHover(hit(e));
    }
    drag.current = null;
    if (d?.kind === "mask")
      latest.current.maskEditor?.onEnd(false, hit(e), paintingPointer(e));
    if (d?.kind === "transform") latest.current.editor.onTransformEnd?.(false);
    if (e.currentTarget.hasPointerCapture(e.pointerId))
      e.currentTarget.releasePointerCapture(e.pointerId);
  }
  function dragOver(e) {
    if (!Array.from(e.dataTransfer.types).includes("application/x-alloy-asset"))
      return;
    e.preventDefault();
    e.stopPropagation();
    e.dataTransfer.dropEffect = source ? "none" : "copy";
    setHover(source ? null : hit(e));
  }
  const tile = hover && uvTile(...hover.uvPosition);
  return (
    <div
      ref={hostRef}
      className="tp-uv-view"
      data-compact={size.height < 260}
      aria-label="UV and UDIM viewer"
    >
      <canvas
        ref={canvasRef}
        className="tp-uv-canvas"
        aria-label="UV texture-space canvas"
        role="application"
        tabIndex={0}
        onPointerDown={pointerDown}
        onPointerMove={pointerMove}
        onPointerUp={pointerUp}
        onPointerCancel={(e) => {
          if (drag.current && drag.current.pointerId !== e.pointerId) return;
          cancel(e);
          clearCursor();
        }}
        onLostPointerCapture={(e) => {
          if (drag.current?.pointerId === e.pointerId) {
            cancel(e);
            clearCursor();
          }
        }}
        onPointerLeave={() => {
          if (!drag.current) clearCursor();
        }}
        onDragOver={dragOver}
        onDragLeave={() => setHover(null)}
        onDrop={(e) => {
          e.preventDefault();
          e.stopPropagation();
          const asset = readAssetDrop(e.dataTransfer),
            surface = !source && hit(e);
          setHover(null);
          if (source)
            onError(
              "Source UVs overlap. Choose Authoring UDIMs to place decals or materials.",
            );
          else if (asset && surface) onAssetDrop(asset, surface);
        }}
        onContextMenu={(e) => {
          e.preventDefault();
          e.stopPropagation();
          cancel();
          clearCursor();
          onOpenPaintTools?.();
        }}
        onKeyDown={(e) => {
          if (e.code === "Space") {
            e.preventDefault();
            spaceHeld.current = true;
            clearCursor();
          }
          if (e.key === "Escape") {
            e.preventDefault();
            e.stopPropagation();
            cancel();
            clearCursor();
          }
        }}
        onKeyUp={(e) => {
          if (e.code === "Space") spaceHeld.current = false;
        }}
      />
      <canvas
        ref={brushCursorCanvas}
        className="digital-uv-cursor"
        aria-label="Brush footprint preview"
        aria-hidden="true"
      />
      <span
        ref={brushCursorReadout}
        className="digital-cursor-readout"
        aria-hidden="true"
        hidden
      />
      <div className="tp-uv-info" aria-live="polite">
        <strong>
          {source ? "SOURCE UV0 · OVERLAPPING" : "AUTHORING UDIMS"}
        </strong>
        <span>
          {topology.charts.length} charts · {topology.tiles.length}{" "}
          {topology.tiles.length === 1 ? "tile" : "tiles"} ·{" "}
          {topology.triangles.length.toLocaleString()} triangles
        </span>
      </div>
      {source && (
        <p className="tp-uv-source-notice">
          The original 32 patch UVs overlap in 1001. Inspect only; use Authoring
          UDIMs for unambiguous placement.
        </p>
      )}
      <div className="tp-uv-readout">
        {hover?.target ? (
          <span>
            {texturePart(hover.target.partId)?.name} · {tile?.id} · U{" "}
            {hover.uvPosition[0].toFixed(3)} / V{" "}
            {hover.uvPosition[1].toFixed(3)}
          </span>
        ) : (
          <span>Pan: middle drag / Alt-drag · Zoom: wheel · F: frame</span>
        )}
        <small>
          UV topology + artwork footprints · mask guides · no baked atlas
        </small>
      </div>
    </div>
  );
}
