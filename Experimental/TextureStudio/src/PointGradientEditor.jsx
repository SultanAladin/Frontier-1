import React, { useEffect, useMemo, useState } from "react";
import { Move, MousePointer2, Trash2, Plus, RotateCcw } from "lucide-react";
import ColourPicker from "./ColourPicker.jsx";
import { Slider } from "./MaterialControls.jsx";
import {
  GRADIENT_POINT_LIMIT,
  defaultPointGradient,
  rasterizePointGradient,
} from "./pointGradient.js";
export default function PointGradientEditor({
  gradient,
  onChange,
  disabled = false,
  mask = null,
  editor,
  onActivate,
  label = "Gradient",
}) {
  const [selected, setSelected] = useState(gradient.points[0]?.id),
    point =
      gradient.points.find(
        (p) => p.id === (editor?.active ? editor.pointId : selected),
      ) || gradient.points[0];
  useEffect(() => {
    if (!gradient.points.some((p) => p.id === selected))
      setSelected(gradient.points[0]?.id);
  }, [gradient.points, selected]);
  const preview = useMemo(() => {
    const c = document.createElement("canvas");
    c.width = c.height = 128;
    const ctx = c.getContext("2d"),
      image = ctx.createImageData(128, 128);
    image.data.set(rasterizePointGradient(gradient, { mask }));
    ctx.putImageData(image, 0, 0);
    return c.toDataURL("image/png");
  }, [gradient, mask]);
  const patch = (changes, field) =>
    onChange(
      {
        ...gradient,
        points: gradient.points.map((p) =>
          p.id === point.id ? { ...p, ...changes } : p,
        ),
      },
      field ? `${point.id}-${field}` : null,
    );
  return (
    <section
      className="tp-point-gradient tp-shared-controls"
      aria-label={`${label} editor`}
    >
      <div className="tp-gradient-preview">
        <img
          src={preview}
          alt={
            mask ? "Gradient mask field slice" : "Gradient colour field slice"
          }
        />
        <small>Object-space XY slice · not a UV atlas</small>
      </div>
      <div className="tp-gradient-actions">
        <button
          disabled={disabled}
          aria-pressed={!!editor?.active}
          onClick={() =>
            onActivate({
              active: !editor?.active,
              pointId: point?.id,
              placing: false,
            })
          }
        >
          <Move size={12} />
          {editor?.active ? "Finish editing" : "Edit in viewport"}
        </button>
        <button
          disabled={disabled || gradient.points.length >= GRADIENT_POINT_LIMIT}
          aria-pressed={!!editor?.placing}
          onClick={() =>
            onActivate({
              active: true,
              pointId: point?.id,
              placing: !editor?.placing,
            })
          }
        >
          <Plus size={12} /> Place point
        </button>
      </div>
      <p className="chan-note">
        <MousePointer2 size={10} />
        {editor?.placing
          ? "Click the teapot to place a new gradient ball."
          : "Drag the small surface balls to move points. Orbit normally outside the balls."}{" "}
        Points stay anchored to the object, not the screen.
      </p>
      <div
        className="tp-gradient-points"
        role="group"
        aria-label={`${label} points`}
      >
        {gradient.points.map((p, i) => (
          <button
            key={p.id}
            disabled={disabled}
            title={`Point ${i + 1}`}
            aria-label={`${label} point ${i + 1}`}
            aria-pressed={p.id === point?.id}
            onClick={() => {
              setSelected(p.id);
              if (editor?.active)
                onActivate({ active: true, placing: false, pointId: p.id });
            }}
          >
            <i style={{ background: p.color }} />
            {i + 1}
          </button>
        ))}
        <small>
          {gradient.points.length}/{GRADIENT_POINT_LIMIT}
        </small>
      </div>
      {point ? (
        <>
          <ColourPicker
            ariaLabel={`${label} point colour`}
            value={point.color}
            disabled={disabled}
            onChange={(color) => patch({ color }, "colour")}
            swatches={
              mask
                ? ["#000000", "#555555", "#aaaaaa", "#ffffff"]
                : [
                    "#e5ac6b",
                    "#6489cc",
                    "#83af77",
                    "#dd5d5d",
                    "#ffffff",
                    "#000000",
                  ]
            }
          />
          <Slider
            label="Influence radius"
            ariaLabel={`${label} point radius`}
            value={point.radius}
            min={0}
            max={3}
            step={0.01}
            unit="r"
            disabled={disabled}
            onChange={(radius) => patch({ radius }, "radius")}
          />
          <Slider
            label="Influence weight"
            ariaLabel={`${label} point weight`}
            value={point.weight}
            percentage
            disabled={disabled}
            onChange={(weight) => patch({ weight }, "weight")}
          />
          <div className="tp-gradient-coordinate">
            <span>Object point</span>
            <code>{point.position.map((v) => v.toFixed(2)).join(" · ")}</code>
            <button
              disabled={disabled}
              aria-label={`Delete ${label.toLowerCase()} point`}
              onClick={() =>
                onChange({
                  ...gradient,
                  points: gradient.points.filter((p) => p.id !== point.id),
                })
              }
            >
              <Trash2 size={12} />
            </button>
          </div>
        </>
      ) : (
        <p className="chan-note">
          No points. Place one on the teapot; the background colour is the
          current field.
        </p>
      )}
      <details className="tp-gradient-background">
        <summary>Fallback colour</summary>
        <ColourPicker
          ariaLabel={`${label} background colour`}
          value={gradient.background}
          disabled={disabled}
          onChange={(background) =>
            onChange({ ...gradient, background }, "background")
          }
        />
      </details>
      <button
        className="tp-gradient-reset"
        disabled={disabled}
        onClick={() => onChange(defaultPointGradient(!!mask))}
      >
        <RotateCcw size={11} /> Reset gradient
      </button>
      {mask && (
        <p className="chan-note">
          Point colour luminance defines coverage: black hides, white reveals.
          Mask strength and invert are evaluated on the selected surface
          preview.
        </p>
      )}
    </section>
  );
}
