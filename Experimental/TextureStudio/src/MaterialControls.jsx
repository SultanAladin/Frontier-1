import React, { useEffect, useState } from "react";
import {
  ChevronDown,
  Plus,
  X,
  Layers3,
  Sparkles,
  SlidersHorizontal,
  Palette,
  MoveUpRight,
} from "lucide-react";

export function Slider({
  label,
  ariaLabel,
  valueLabel,
  disabled = false,
  value = 0,
  onChange,
  min = 0,
  max = 1,
  step = 0.01,
  log = false,
  extend = false,
  unit = "",
  percentage = false,
  low,
  high,
}) {
  const multiplier = percentage ? 100 : 1;
  const [draft, setDraft] = useState(
    String(percentage ? Math.round(value * 100) : Number(value.toPrecision(6))),
  );
  useEffect(
    () =>
      setDraft(
        String(
          percentage ? Math.round(value * 100) : Number(value.toPrecision(6)),
        ),
      ),
    [value, multiplier, percentage],
  );
  const domainMax = extend ? Math.max(max, value) : max;
  const domainMin = extend && value > 0 ? Math.min(min, value) : min;
  const lo = log ? Math.log10(Math.max(0.000001, domainMin)) : domainMin;
  const hi = log ? Math.log10(domainMax) : domainMax;
  const position = log ? Math.log10(Math.max(0.000001, value)) : value;
  const commit = (text, final = false) => {
    if (text.trim() === "" || !Number.isFinite(Number(text))) {
      if (final) setDraft(String(percentage ? Math.round(value * 100) : value));
      return;
    }
    const n = Number(text) / multiplier;
    if (n < min && !final) return;
    const next = Math.min(
      extend ? 1000000 : max,
      Math.max(extend ? 0.000001 : min, n),
    );
    onChange(next);
    if (final)
      setDraft(
        String(
          percentage ? Math.round(next * 100) : Number(next.toPrecision(6)),
        ),
      );
  };
  return (
    <div className="slider-field">
      <div className="field-label">
        <label>{label}</label>
        <div className="slider-value">
          <input
            aria-label={valueLabel || (ariaLabel || label) + " value"}
            disabled={disabled}
            type="number"
            min={extend ? 0.000001 : min * multiplier}
            max={(extend ? 1000000 : max) * multiplier}
            step={step * multiplier}
            value={draft}
            onChange={(e) => {
              setDraft(e.target.value);
              commit(e.target.value);
            }}
            onBlur={() => commit(draft, true)}
            onKeyDown={(e) => {
              if (e.key === "Enter") e.target.blur();
            }}
          />
          {(unit || percentage) && <span>{percentage ? "%" : unit}</span>}
        </div>
      </div>
      <input
        aria-label={ariaLabel || label}
        disabled={disabled}
        className="range-input"
        type="range"
        min={lo}
        max={hi}
        step={log ? 0.001 : step}
        value={position}
        onChange={(e) =>
          onChange(
            log
              ? Number(Math.pow(10, Number(e.target.value)).toPrecision(5))
              : Number(e.target.value),
          )
        }
        style={{
          "--progress": `${Math.max(0, Math.min(100, ((position - lo) / (hi - lo)) * 100))}%`,
        }}
      />
      {(low || log) && (
        <div className="range-labels">
          <span>{low || `${domainMin.toLocaleString()} ${unit}`}</span>
          <span>
            {high || `${domainMax.toLocaleString()} ${unit}`}
            {log && (
              <b title="Logarithmic scale · type a value to extend the range">
                LOG ↗
              </b>
            )}
          </span>
        </div>
      )}
    </div>
  );
}

export function ColorField({
  label,
  ariaLabel,
  value,
  onChange,
  disabled = false,
}) {
  const [text, setText] = useState(value);
  useEffect(() => setText(value), [value]);
  return (
    <div className="color-control">
      <div className="field-label">
        <label>{label}</label>
        <span className="muted-label">sRGB</span>
      </div>
      <div className="color-field">
        <label className="base-color-swatch" style={{ background: value }}>
          <input
            type="color"
            aria-label={ariaLabel || label}
            disabled={disabled}
            value={value}
            onChange={(e) => onChange(e.target.value)}
          />
        </label>
        <input
          aria-label={(ariaLabel || label) + " hex"}
          disabled={disabled}
          value={text.toUpperCase()}
          maxLength={7}
          onChange={(e) => {
            setText(e.target.value);
            if (/^#[0-9a-f]{6}$/i.test(e.target.value))
              onChange(e.target.value);
          }}
          onBlur={() => {
            if (!/^#[0-9a-f]{6}$/i.test(text)) setText(value);
          }}
        />
        <Palette size={13} />
      </div>
    </div>
  );
}
export const palettes = {
  Spectrum: ["#c6db89", "#59bea3", "#ab8bc9", "#d17b83", "#82a3d5"],
  "Vivid spectrum": ["#ec5667", "#50c38c", "#ae67e5", "#5999ee", "#edc25c"],
  "Warm metallic": ["#f0cb8a", "#d99a5d", "#b97046", "#e6bf83", "#e7d6a9"],
  "Cool metallic": ["#93bbdc", "#a49cdc", "#6ed0c5", "#c0cedd", "#789cab"],
};
function RampHex({ value, onChange }) {
  const [text, setText] = useState(value);
  useEffect(() => setText(value), [value]);
  return (
    <input
      aria-label="Selected flake color hex"
      value={text.toUpperCase()}
      maxLength={7}
      onChange={(e) => {
        setText(e.target.value);
        if (/^#[0-9a-f]{6}$/i.test(e.target.value)) onChange(e.target.value);
      }}
      onBlur={() => {
        if (!/^#[0-9a-f]{6}$/i.test(text)) setText(value);
      }}
    />
  );
}
export function ColorRamp({ params: p, update, setParams }) {
  const [active, setActive] = useState(0),
    [palette, setPalette] = useState("Spectrum");
  const index = Math.min(active, p.colors.length - 1);
  const sorted = p.colors
    .map((c, i) => ({ c, pos: p.colorStops[i] }))
    .sort((a, b) => a.pos - b.pos);
  const gradient =
    "linear-gradient(90deg, " +
    sorted.map((s) => `${s.c} ${s.pos * 100}%`).join(", ") +
    ")";
  const setColor = (c) =>
    update(
      "colors",
      p.colors.map((v, i) => (i === index ? c : v)),
    );
  const add = () => {
    if (p.colors.length >= 12) return;
    const positions = [0, ...p.colorStops, 1].sort((a, b) => a - b);
    let gap = 0,
      pos = 1;
    for (let i = 1; i < positions.length; i++) {
      if (positions[i] - positions[i - 1] > gap) {
        gap = positions[i] - positions[i - 1];
        pos = (positions[i] + positions[i - 1]) / 2;
      }
    }
    const count = p.colors.length + 1;
    setParams((v) => ({
      ...v,
      colors: [...v.colors, "#8ca6e0"],
      colorStops: [...v.colorStops, pos],
    }));
    setActive(count - 1);
  };
  const remove = () => {
    if (p.colors.length <= 1) return;
    setParams((v) => ({
      ...v,
      colors: v.colors.filter((_, i) => i !== index),
      colorStops: v.colorStops.filter((_, i) => i !== index),
    }));
    setActive(Math.max(0, index - 1));
  };
  return (
    <div className="ramp-editor">
      <div className="field-label">
        <label>Flake color distribution</label>
        <span>{p.colors.length}/12</span>
      </div>
      <div className="segmented-control">
        {[
          ["palette", "Palette"],
          ["ramp", "Color ramp"],
          ["single", "Single"],
        ].map(([id, label]) => (
          <button
            key={id}
            className={p.colorMode === id ? "active" : ""}
            onClick={() => update("colorMode", id)}
          >
            {label}
          </button>
        ))}
      </div>
      {p.colorMode === "ramp" && (
        <div
          className="ramp-preview"
          style={{ background: gradient }}
          aria-label="Color ramp preview"
        >
          {p.colors.map((c, i) => (
            <button
              key={i}
              aria-label={"Select color stop " + (i + 1)}
              className={index === i ? "active" : ""}
              style={{ left: `${p.colorStops[i] * 100}%`, background: c }}
              onClick={() => setActive(i)}
            />
          ))}
        </div>
      )}
      <div className="flake-colors">
        {p.colors.map((c, i) => (
          <label
            key={i}
            className={index === i ? "active" : ""}
            style={{ background: c }}
            title={`Flake color ${i + 1} · click to choose`}
            onClick={() => setActive(i)}
          >
            <input
              type="color"
              aria-label={"Flake color " + (i + 1)}
              value={c}
              onChange={(e) => {
                setActive(i);
                update(
                  "colors",
                  p.colors.map((v, j) => (j === i ? e.target.value : v)),
                );
              }}
            />
          </label>
        ))}
        <button
          title="Add flake color"
          disabled={p.colors.length >= 12}
          onClick={add}
        >
          <Plus size={15} />
        </button>
      </div>
      <div className="selected-stop">
        <label>Color {index + 1}</label>
        <RampHex value={p.colors[index]} onChange={setColor} />
        <button
          title="Remove selected color"
          disabled={p.colors.length === 1}
          onClick={remove}
        >
          <X size={13} />
        </button>
      </div>
      {p.colorMode === "ramp" && (
        <Slider
          label="Stop position"
          value={p.colorStops[index]}
          onChange={(v) =>
            update(
              "colorStops",
              p.colorStops.map((s, i) => (i === index ? v : s)),
            )
          }
          percentage
        />
      )}
      <label className="palette-select">
        <span>
          <span className="spectrum-dot" />
          {palette}
        </span>
        <select
          aria-label="Flake color palette"
          value={palette}
          onChange={(e) => {
            const colors = palettes[e.target.value];
            setPalette(e.target.value);
            setParams((v) => ({
              ...v,
              colors: [...colors],
              colorStops: colors.map((_, i) => i / (colors.length - 1)),
            }));
            setActive(0);
          }}
        >
          {Object.keys(palettes).map((k) => (
            <option key={k}>{k}</option>
          ))}
        </select>
        <ChevronDown size={13} />
      </label>
      <p className="help-text">
        {p.colorMode === "ramp"
          ? "Each flake samples the ramp. Select a stop to edit its position."
          : p.colorMode === "single"
            ? "The first color is used for every flake."
            : "Random, individual flake colors. Click a swatch to open the color picker."}
      </p>
    </div>
  );
}
