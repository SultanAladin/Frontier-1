import ColourPicker from "./ColourPicker.jsx";
import { Slider } from "./MaterialControls.jsx";
import "./channelPropertyPanel.css";
import useDialogFocus from "./useDialogFocus.js";
import React, { useEffect, useRef, useState } from "react";
import { Brush, X, ArrowLeft, SlidersHorizontal, Search } from "lucide-react";
import {
  PAINT_FAMILIES,
  PAINT_TOOLS,
  PAINT_SWATCHES,
  paintTool,
  normalizePaintSettings,
  selectPaintInstrument,
  visibleToolSchema,
  toolArtURL,
} from "./paintToolCatalogue.js";
import "./paintTools.css";
export function PaintToolProperties({ settings, onChange }) {
  const tool = paintTool(settings.toolId),
    value = normalizePaintSettings(settings),
    patch = (key, data) =>
      onChange({ ...value, params: { ...value.params, [key]: data } });
  return (
    <div className="paint-tool-properties tp-shared-controls">
      <section className="paint-tool-hero tp-property-card">
        <img src={toolArtURL(tool.id)} alt={tool.label} />
        <h3>{tool.label}</h3>
        <span>
          {PAINT_FAMILIES.find((f) => f.key === tool.key)?.label} · tool
          configuration
        </span>
      </section>
      <div
        className="paint-mark-preview"
        aria-label="Representative stroke preview"
        style={{
          "--ink": value.color,
          "--opacity":
            (value.params.opacity ?? value.params.strength ?? 100) / 100,
          "--size": `${Math.min(30, Math.max(2, value.params.size * 0.3))}px`,
        }}
      >
        <i />
        <b />
      </div>
      <p className="paint-preview-note">
        Representative mark only · not a paint-engine stroke
      </p>
      <ColourPicker
        value={value.color}
        onChange={(color) => onChange({ ...value, color })}
        ariaLabel="Paint tool color"
        swatches={PAINT_SWATCHES[tool.key] || []}
      />
      {visibleToolSchema(value).map((group) => (
        <section
          className="paint-control-group tp-property-card"
          key={group.title}
        >
          <h2>
            <SlidersHorizontal size={11} />
            {group.title}
          </h2>
          {group.controls.map((c) =>
            c.type === "slider" ? (
              <Slider
                key={c.k}
                label={c.label}
                ariaLabel={`Paint ${c.label}`}
                value={value.params[c.k]}
                min={c.min}
                max={c.max}
                step={c.step}
                unit={c.unit || ""}
                onChange={(v) => patch(c.k, v)}
              />
            ) : c.type === "switch" ? (
              <label key={c.k} className="tp-toggle-row">
                <span>{c.label}</span>
                <input
                  type="checkbox"
                  role="switch"
                  aria-label={`Paint ${c.label}`}
                  checked={value.params[c.k]}
                  onChange={(e) => patch(c.k, e.target.checked)}
                />
              </label>
            ) : c.type === "seg" ? (
              <div className="paint-shared-segment" key={c.k}>
                <label>{c.label}</label>
                <div
                  className="tp-source-pills"
                  role="group"
                  aria-label={`Paint ${c.label}`}
                >
                  {c.opts.map((o) => (
                    <button
                      key={o}
                      aria-pressed={value.params[c.k] === o}
                      onClick={() => patch(c.k, o)}
                    >
                      {o}
                    </button>
                  ))}
                </div>
              </div>
            ) : (
              <label key={c.k} className="channel-generator-label">
                <span>{c.label}</span>
                <select
                  aria-label={`Paint ${c.label}`}
                  value={value.params[c.k]}
                  onChange={(e) => patch(c.k, e.target.value)}
                >
                  {c.opts.map((o) => (
                    <option key={o}>{o}</option>
                  ))}
                </select>
              </label>
            ),
          )}
        </section>
      ))}
    </div>
  );
}
export default function PaintToolMenu({
  settings,
  onChange,
  open,
  onClose,
  onSave,
  onUndo,
  onRedo,
}) {
  const [family, setFamily] = useState(paintTool(settings.toolId).key),
    [page, setPage] = useState("tools"),
    [search, setSearch] = useState("");
  const ref = useRef(null);
  useDialogFocus(open, ref);
  useEffect(() => {
    if (open) {
      setPage("tools");
      setFamily(paintTool(settings.toolId).key);
      setSearch("");
    }
  }, [open]);
  useEffect(() => {
    if (!open) return;
    const key = (e) => {
      const command = e.ctrlKey || e.metaKey,
        letter = e.key.toLowerCase();
      if (command && ["s", "z", "y"].includes(letter)) {
        e.preventDefault();
        e.stopPropagation();
        if (letter === "s") onSave();
        else if (letter === "y" || e.shiftKey) onRedo();
        else onUndo();
      } else if (e.key === "Escape") {
        e.preventDefault();
        e.stopPropagation();
        page === "properties" ? setPage("tools") : onClose();
      }
    };
    window.addEventListener("keydown", key, true);
    return () => window.removeEventListener("keydown", key, true);
  }, [open, page, onClose, onSave, onUndo, onRedo]);
  if (!open) return null;
  const list = PAINT_TOOLS.filter(
    (t) =>
      t.key === family && t.label.toLowerCase().includes(search.toLowerCase()),
  );
  return (
    <div
      className="paint-menu-backdrop"
      onPointerDown={(e) => {
        if (e.target === e.currentTarget) onClose();
      }}
    >
      <section
        className="alloy-paint-menu"
        role="dialog"
        aria-modal="true"
        aria-label="Paint tool menu"
        tabIndex={-1}
        ref={ref}
      >
        <header>
          {page === "properties" && (
            <button
              aria-label="Back to paint instruments"
              onClick={() => setPage("tools")}
            >
              <ArrowLeft size={15} />
            </button>
          )}
          <Brush size={16} />
          <span>
            {page === "properties"
              ? "Instrument properties"
              : "Paint instruments"}
          </span>
          <small>102 tools · 10 families</small>
          <button aria-label="Close paint tool menu" onClick={onClose}>
            <X size={16} />
          </button>
        </header>
        {page === "properties" ? (
          <PaintToolProperties settings={settings} onChange={onChange} />
        ) : (
          <div className="paint-menu-body">
            <nav aria-label="Paint tool families">
              {PAINT_FAMILIES.map((f) => (
                <button
                  key={f.key}
                  aria-pressed={family === f.key}
                  onClick={() => setFamily(f.key)}
                >
                  <i style={{ background: f.dot }} />
                  <span>{f.label}</span>
                  <small>{f.tally}</small>
                </button>
              ))}
            </nav>
            <div className="paint-instruments">
              <label className="paint-search">
                <Search size={12} />
                <input
                  aria-label="Search paint instruments"
                  placeholder="Find an instrument…"
                  value={search}
                  onChange={(e) => setSearch(e.target.value)}
                />
              </label>
              <div className="paint-instrument-grid">
                {list.map((tool) => (
                  <button
                    key={tool.id}
                    className="paint-instrument-card"
                    title={tool.label}
                    aria-label={`Select paint tool ${tool.label}`}
                    aria-pressed={settings.toolId === tool.id}
                    onClick={() => {
                      onChange(selectPaintInstrument(settings, tool.id));
                      setPage("properties");
                    }}
                  >
                    <span
                      className="paint-instrument-portrait"
                      aria-hidden="true"
                    >
                      <img src={toolArtURL(tool.id, true)} alt="" />
                    </span>
                    <span className="paint-instrument-label">{tool.label}</span>
                  </button>
                ))}
              </div>
              {!list.length && <p>No instruments match.</p>}
            </div>
          </div>
        )}
        <footer>
          Configuration is saved with the layer project. Strokes are not yet
          composited onto the teapot.
        </footer>
      </section>
    </div>
  );
}
