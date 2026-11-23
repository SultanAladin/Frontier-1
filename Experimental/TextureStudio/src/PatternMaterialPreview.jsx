import React, { useEffect, useMemo, useState } from "react";
import Viewport from "./Viewport.jsx";
import {
  composePatternMaterial,
  patternPreviewShape,
} from "./patternSurface.js";

const previewShapes = [
  "Shader ball",
  "Panel",
  "Rug",
  "Draped cloth",
  "Leather swatch",
  "Rounded cube",
  "Sphere",
  "Teapot",
];
export default function PatternMaterialPreview({ doc, target, initialShape }) {
  const preferredShape =
    doc.presentation === "rug" && target.category === "Fabric"
      ? "Rug"
      : previewShapes.includes(initialShape)
        ? initialShape
        : patternPreviewShape(target);
  const [renderDoc, setRenderDoc] = useState(doc),
    [shape, setShape] = useState(preferredShape),
    [environment, setEnvironment] = useState("Studio softbox");
  const [status, setStatus] = useState({ phase: "compiling" }),
    [zoom, setZoom] = useState(null),
    [fit, setFit] = useState(0),
    [zoomPercent, setZoomPercent] = useState(100);
  useEffect(() => {
    const timer = setTimeout(() => setRenderDoc(doc), 450);
    return () => clearTimeout(timer);
  }, [doc]);
  useEffect(() => {
    setShape(preferredShape);
  }, [target.id, preferredShape]);
  const params = useMemo(
    () => composePatternMaterial(target, renderDoc),
    [target, renderDoc],
  );
  const pending = doc !== renderDoc || status.phase === "compiling";
  return (
    <section
      className="pe-material-preview"
      aria-label="Pattern material preview"
    >
      <div className="pe-preview-toolbar">
        <label>
          Object
          <select
            aria-label="Pattern preview object"
            value={shape}
            onChange={(e) => setShape(e.target.value)}
          >
            {previewShapes.map((s) => (
              <option key={s}>{s}</option>
            ))}
          </select>
        </label>
        <label>
          Lighting
          <select
            aria-label="Pattern preview lighting"
            value={environment}
            onChange={(e) => setEnvironment(e.target.value)}
          >
            {[
              "Studio softbox",
              "Daylight",
              "Warm atelier",
              "Low-key studio",
            ].map((s) => (
              <option key={s}>{s}</option>
            ))}
          </select>
        </label>
        <button
          onClick={() => {
            setFit((n) => n + 1);
            setZoom(null);
          }}
        >
          Fit preview
        </button>
        <button onClick={() => setZoom({ macro: true, token: Date.now() })}>
          Inspect detail
        </button>
      </div>
      <div className="pe-preview-stage">
        <Viewport
          params={params}
          shape={shape}
          environment={environment}
          rotate={false}
          wireframe={false}
          resetToken={fit}
          zoom={zoom}
          onCompile={setStatus}
          onZoomChange={setZoomPercent}
        />
      </div>
      <label className="pe-preview-zoom">
        Preview zoom
        <input
          aria-label="Pattern preview zoom"
          type="range"
          min="60"
          max="800"
          step="5"
          value={Math.max(60, Math.min(800, zoomPercent))}
          onChange={(e) =>
            setZoom({ percent: +e.target.value, token: Date.now() })
          }
        />
        <output>{Math.round(zoomPercent)}%</output>
      </label>
      <div
        className="pe-preview-status"
        role="status"
        aria-label="Material preview status"
      >
        <span className={pending ? "pending" : ""} />
        {status.phase === "error"
          ? "Preview failed — change the source or return to the workspace."
          : pending
            ? "Updating material…"
            : "Live material · ready"}
        <small>MMB orbit · Shift+MMB pan · Wheel / Ctrl+MMB zoom · Camera lag</small>
      </div>
    </section>
  );
}
