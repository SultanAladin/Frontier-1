import React, { useRef, useEffect } from "react";
import { Type, Spline } from "lucide-react";
export default function PatternTextControls({
  text,
  onChange,
  selected,
  ready,
  error,
  onOutline,
  focusToken,
  disabled = false,
}) {
  const input = useRef(null);
  useEffect(() => {
    if (focusToken) {
      input.current?.focus();
      input.current?.select();
    }
  }, [focusToken]);
  return (
    <section className="se-text-workbench" aria-label="Text workbench">
      <div className="se-path-heading">
        <Type size={17} />
        <div>
          <strong>{selected ? "Edit text" : "Text tool"}</strong>
          <small>
            {selected
              ? "Editable type · portable vector outlines"
              : "Click on the canvas to place text"}
          </small>
        </div>
      </div>
      <fieldset disabled={disabled || !ready} className="se-property-fields">
        <label>
          Text content
          <textarea
            ref={input}
            aria-label="Text content"
            value={text.value}
            maxLength={2000}
            onChange={(e) => onChange({ value: e.target.value })}
            rows={3}
          />
        </label>
        <div className="se-text-fields">
          <label>
            Font family
            <select
              aria-label="Text font family"
              value={text.fontFamily}
              onChange={(e) => onChange({ fontFamily: e.target.value })}
            >
              <option>DM Sans</option>
              <option>Space Grotesk</option>
            </select>
          </label>
          <label>
            Weight
            <select
              aria-label="Text font weight"
              value={text.fontWeight}
              onChange={(e) => onChange({ fontWeight: Number(e.target.value) })}
            >
              <option value={400}>Regular</option>
              <option value={600}>Semibold</option>
            </select>
          </label>
        </div>
        <div className="se-text-fields">
          {[
            ["fontSize", "Font size", 4, 256, 1],
            ["letterSpacing", "Letter spacing", -10, 40, 0.1],
            ["lineHeight", "Line height", 0.5, 3, 0.05],
          ].map(([key, name, min, max, step]) => (
            <label key={key}>
              {name}
              <input
                aria-label={`Text ${name.toLowerCase()}`}
                type="number"
                min={min}
                max={max}
                step={step}
                value={text[key]}
                onChange={(e) => onChange({ [key]: Number(e.target.value) })}
              />
            </label>
          ))}
          <label>
            Alignment
            <select
              aria-label="Text alignment"
              value={text.align}
              onChange={(e) => onChange({ align: e.target.value })}
            >
              {["left", "center", "right"].map((v) => (
                <option key={v} value={v}>
                  {v[0].toUpperCase() + v.slice(1)}
                </option>
              ))}
            </select>
          </label>
        </div>
        {selected && (
          <button className="pe-wide" onClick={onOutline}>
            <Spline size={14} />
            Convert text to outlines
          </button>
        )}
      </fieldset>
      {!ready && (
        <p role="status" className="pe-hint">
          Preparing bundled fonts…
        </p>
      )}
      {error && (
        <p className="pe-error" role="alert">
          {error}
        </p>
      )}
      <p className="pe-hint">
        Multiline Latin text. SVG, PNG and material exports contain outlines—no
        font downloads. Converting removes editable text metadata; Undo restores
        it.
      </p>
    </section>
  );
}
