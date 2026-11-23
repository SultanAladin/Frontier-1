import React, { useState } from "react";
import { Brush, MousePointer2 } from "lucide-react";
import { maskRegionLabel } from "./textureMaskPaint.js";
import "./textureMaskPaint.css";

// Mask editing rows only. Preview switching and the authoring status line
// live in the studio's consolidated authoring bar (TexturePaintStudio);
// this section appears when a mask is selected and carries the selection
// scopes, ink controls and region status that belong to mask editing.
export default function TextureMaskToolbar({
  editing,
  viewMode,
  source,
  layer,
}) {
  const [optionsOpen, setOptionsOpen] = useState(false);
  const { state, maskSelected, available, region } = editing;
  if (!maskSelected) return null;
  return (
    <section
      className="tp-mask-authoring"
      aria-label="Texture authoring"
      data-authoring-mode={editing.mode}
      data-options-open={optionsOpen}
    >
      <div
        className="tp-mask-selection-tools"
        role="group"
        aria-label="Mask selection and painting"
      >
        {[
          ["face", "Face"],
          ["tile", "UV Tile"],
          ["island", "UV Island"],
          ["object", "Object"],
        ].map(([scope, label]) => (
          <button
            key={scope}
            aria-label={`Select mask ${label}`}
            aria-pressed={state.tool === "select" && state.scope === scope}
            disabled={!available || (scope === "tile" && viewMode !== "uv")}
            title={
              scope === "tile"
                ? "UV tile selection is available in packed UV / Texture mode"
                : `Select a real ${label.toLowerCase()} on the teapot`
            }
            onClick={() => editing.selectScope(scope)}
          >
            <MousePointer2 size={12} />
            {label}
          </button>
        ))}
        <button
          aria-label="Paint mask"
          data-tooltip-body="Paint on the selected mask with the current brush and ink. White reveals the layer; black conceals it."
          aria-pressed={state.tool === "brush"}
          disabled={!available}
          onClick={() => editing.change({ tool: "brush" })}
        >
          <Brush size={13} />
          Paint mask
        </button>
        <button
          aria-label="Fill selected mask region"
          data-tooltip-body="Fill the selected face, UV tile, UV island or object region with the active mask ink."
          disabled={!available || !region}
          onClick={editing.fillRegion}
        >
          Fill selection
        </button>
        <button
          aria-label="Clear mask selection"
          disabled={!region}
          onClick={editing.clearRegion}
        >
          Clear
        </button>
        <button
          className="tp-mask-options-toggle"
          aria-label="Mask brush options"
          data-tooltip-body="Show or hide brush settings for the current mask. This does not change the selected layer."
          aria-expanded={optionsOpen}
          onClick={() => setOptionsOpen((open) => !open)}
        >
          Brush settings
        </button>
      </div>
      <div
        className="tp-mask-brush-options"
        role="group"
        aria-label="Mask brush settings"
      >
        <div className="tp-mask-ink">
          <button
            aria-label="White mask ink"
            aria-pressed={state.value === 1}
            onClick={() => editing.change({ value: 1 })}
          >
            <i className="white" />
            White
          </button>
          <button
            aria-label="Black mask ink"
            aria-pressed={state.value === 0}
            onClick={() => editing.change({ value: 0 })}
          >
            <i className="black" />
            Black
          </button>
        </div>
        <label>
          Size{" "}
          <input
            type="range"
            aria-label="Mask brush size"
            min="0.2"
            max="25"
            step="0.2"
            value={state.radius * 100}
            onChange={(e) =>
              editing.change({ radius: Number(e.target.value) / 100 })
            }
          />
          <output>{Math.round(state.radius * 1000) / 10}%</output>
        </label>
        <label>
          Opacity{" "}
          <input
            type="range"
            aria-label="Mask brush opacity"
            min="0"
            max="100"
            value={state.opacity * 100}
            onChange={(e) =>
              editing.change({ opacity: Number(e.target.value) / 100 })
            }
          />
          <output>{Math.round(state.opacity * 100)}%</output>
        </label>
        <label>
          Hardness{" "}
          <input
            type="range"
            aria-label="Mask brush hardness"
            min="0"
            max="100"
            value={state.hardness * 100}
            onChange={(e) =>
              editing.change({ hardness: Number(e.target.value) / 100 })
            }
          />
          <output>{Math.round(state.hardness * 100)}%</output>
        </label>
      </div>
      <div
        className="tp-mask-selection-status"
        aria-label="Selected mask region"
      >
        {maskRegionLabel(region)}
        {!region && state.tool === "brush"
          ? " · Brush is scoped to the hit component"
          : ""}
        {layer?.mask?.enabled === false
          ? " · Mask bypassed (enable to see coverage)"
          : ""}
        {source && viewMode === "uv" ? " · Source UVs are read-only" : ""}
        {layer?.mask?.kind === "color"
          ? " · Source RGB per UV tile, not shaded material"
          : ""}
      </div>
    </section>
  );
}
