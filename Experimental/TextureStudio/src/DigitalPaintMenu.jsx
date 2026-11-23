import React, { useEffect, useRef, useState } from "react";
import {
  Brush,
  Eraser,
  PaintBucket,
  X,
  RotateCcw,
  SlidersHorizontal,
  Layers3,
  Palette,
  ChevronDown,
  Check,
  ArrowRight,
} from "lucide-react";
import useDialogFocus from "./useDialogFocus.js";
import { Slider } from "./MaterialControls.jsx";
import ColourPicker from "./ColourPicker.jsx";
import {
  DIGITAL_BRUSH_PRESETS,
  selectDigitalBrush,
  digitalBrushDabs,
  drawDigitalDabs,
  digitalPreviewPoints,
} from "./digitalBrush.js";
import {
  PAINT_CHANNELS,
  CHANNEL_GROUPS,
  togglePaintChannel,
} from "./paintChannelModel.js";
import PaintToolMenu from "./PaintToolMenu.jsx";
import "./digitalPainting.css";

const tools = [
  ["paint", "Paint", Brush],
  ["erase", "Erase", Eraser],
  ["fill", "Fill", PaintBucket],
];
export function DigitalStrokePreview({ brush, mask = false, ink = 1 }) {
  const ref = useRef(null),
    path = useRef(null),
    gesture = useRef(null),
    [revision, setRevision] = useState(0);
  function repaint(points = path.current || digitalPreviewPoints()) {
    const c = ref.current;
    if (!c) return;
    const ctx = c.getContext("2d");
    ctx.clearRect(0, 0, c.width, c.height);
    const mark = document.createElement("canvas");
    mark.width = c.width;
    mark.height = c.height;
    const color = mask ? (ink ? "#ffffff" : "#000000") : brush.color;
    const settings = { ...brush, color, tool: "paint" };
    if (brush.tool === "fill") {
      const m = mark.getContext("2d");
      m.fillStyle = color;
      m.fillRect(120, 45, 740, 120);
    } else
      drawDigitalDabs(
        mark.getContext("2d"),
        digitalBrushDabs(points, settings, 41, 256, color, !mask),
        settings,
      );
    if (brush.tool === "erase") {
      ctx.fillStyle = "#686b67";
      ctx.fillRect(0, 0, c.width, c.height);
      ctx.globalCompositeOperation = "destination-out";
    }
    ctx.globalAlpha = brush.opacity;
    ctx.drawImage(mark, 0, 0);
    ctx.globalAlpha = 1;
    ctx.globalCompositeOperation = "source-over";
    const pixels = ctx.getImageData(0, 0, c.width, c.height).data;
    let hash = 2166136261;
    for (let i = 0; i < pixels.length; i += 101)
      hash = Math.imul(hash ^ pixels[i], 16777619);
    c.dataset.previewHash = String(hash >>> 0);
  }
  useEffect(() => {
    repaint();
  }, [brush, mask, ink, revision]);
  function point(e) {
    const c = ref.current,
      r = c.getBoundingClientRect();
    return [
      ((e.clientX - r.left) * c.width) / r.width / 256,
      ((e.clientY - r.top) * c.height) / r.height / 256,
      e.pointerType === "mouse" ? 1 : Math.max(0.02, e.pressure || 1),
    ];
  }
  return (
    <section
      className="digital-stroke-preview"
      aria-label="Live digital stroke preview"
    >
      <header>
        <span>
          <Brush size={13} />{" "}
          {brush.tool === "fill" ? "Live fill preview" : "Stroke preview"}
        </span>
        <small>Same digital engine · draw here</small>
        <button
          aria-label="Reset digital stroke preview"
          onClick={() => {
            path.current = null;
            setRevision((n) => n + 1);
          }}
        >
          <RotateCcw size={12} />
        </button>
      </header>
      <canvas
        ref={ref}
        width="1024"
        height="210"
        tabIndex={0}
        aria-label="Digital brush stroke preview canvas"
        onPointerDown={(e) => {
          if (e.button !== 0 || gesture.current != null) return;
          e.preventDefault();
          gesture.current = e.pointerId;
          path.current = [point(e)];
          e.currentTarget.setPointerCapture(e.pointerId);
          repaint();
        }}
        onPointerMove={(e) => {
          if (gesture.current !== e.pointerId) return;
          if (path.current.length < 512) path.current.push(point(e));
          repaint();
        }}
        onPointerUp={(e) => {
          if (gesture.current !== e.pointerId) return;
          gesture.current = null;
          if (e.currentTarget.hasPointerCapture(e.pointerId))
            e.currentTarget.releasePointerCapture(e.pointerId);
        }}
        onPointerCancel={() => {
          gesture.current = null;
          path.current = null;
          setRevision((n) => n + 1);
        }}
        onLostPointerCapture={() => {
          gesture.current = null;
        }}
        onKeyDown={(e) => {
          if (e.key === "Enter" || e.key === " ") {
            e.preventDefault();
            path.current = null;
            setRevision((n) => n + 1);
          }
        }}
      />
      <p>
        {brush.tool === "erase"
          ? "Erase removes authored pixels and reveals the layer below; masks restore their original source."
          : brush.tool === "fill"
            ? "Fill uses channel values and opacity. Click a face, island or mesh component."
            : "Pressure, taper, shape, grain and colour dynamics are rendered, not illustrated."}
      </p>
    </section>
  );
}
function BrushCard({ title, icon: Icon = SlidersHorizontal, children, note }) {
  return (
    <section
      className="tp-property-card digital-brush-card"
      aria-label={`${title} properties`}
    >
      <h2>
        <Icon size={13} />
        {title}
      </h2>
      <div className="tp-shared-controls">{children}</div>
      {note && <p className="digital-card-note">{note}</p>}
    </section>
  );
}
export function DigitalBrushProperties({ brush, onChange, mask = false }) {
  const set = (key, value) => onChange({ ...brush, [key]: value });
  const slider = (key, label, max = 1, min = 0, unit = "") => (
    <Slider
      key={key}
      label={label}
      ariaLabel={`Digital brush ${label}`}
      value={brush[key]}
      onChange={(value) => set(key, value)}
      min={min}
      max={max}
      step={max > 2 ? 1 : 0.01}
      percentage={max === 1 && min === 0}
      unit={unit}
      disabled={
        brush.tool === "fill" && !["opacity", "heightStrength"].includes(key)
      }
    />
  );
  return (
    <div className="digital-brush-cards">
      <BrushCard
        title="Stroke path"
        note="Size is the diameter in screen (CSS) pixels. The tip stays circular at every zoom and projects across UV seams. Zero size or opacity paints nothing."
      >
        {slider("size", "Size", 128, 0, "px")}
        {slider("opacity", "Opacity")}
        {slider("flow", "Flow")}
        {slider("spacing", "Spacing", 2, 0.05)}
        {slider("stabilization", "Stabilization")}
        {slider("taperStart", "Start taper")}
        {slider("taperEnd", "End taper")}
      </BrushCard>
      <BrushCard title="Shape">
        <label className="digital-select">
          Tip shape
          <select
            aria-label="Digital brush tip shape"
            value={brush.shape}
            disabled={brush.tool === "fill"}
            onChange={(e) => set("shape", e.target.value)}
          >
            <option value="round">Round / ellipse</option>
            <option value="square">Square</option>
          </select>
          <ChevronDown size={11} />
        </label>
        {slider("hardness", "Hardness")}
        {slider("roundness", "Roundness")}
        {slider("rotation", "Rotation", 360, 0, "°")}
        {slider("rotationJitter", "Rotation jitter")}
        <label className="digital-switch">
          <input
            type="checkbox"
            aria-label="Digital brush follow stroke"
            checked={brush.follow}
            disabled={brush.tool === "fill"}
            onChange={(e) => set("follow", e.target.checked)}
          />
          Follow stroke direction
        </label>
      </BrushCard>
      <BrushCard
        title="Grain"
        note="Procedural digital tip texture. No paper or physical-media simulation."
      >
        <label className="digital-select">
          Texture
          <select
            aria-label="Digital brush grain texture"
            value={brush.grain}
            disabled={brush.tool === "fill"}
            onChange={(e) => set("grain", e.target.value)}
          >
            <option value="none">None</option>
            <option value="noise">Noise</option>
            <option value="lines">Lines</option>
            <option value="checker">Checker</option>
          </select>
          <ChevronDown size={11} />
        </label>
        {slider("grainAmount", "Grain amount")}
        {slider("grainScale", "Grain scale", 32, 1, "px")}
      </BrushCard>
      <BrushCard title="Stroke dynamics">
        {slider("sizeJitter", "Size jitter")}
        {slider("opacityJitter", "Opacity jitter")}
        {slider("scatter", "Scatter")}
      </BrushCard>
      <BrushCard
        title="Colour dynamics"
        icon={Palette}
        note={
          mask
            ? "Masks use grayscale ink. Colour dynamics apply to Base color painting only."
            : "Seeded hue, saturation and brightness variations are saved with each stroke."
        }
      >
        {[
          ["strokeHue", "Stroke hue"],
          ["strokeSaturation", "Stroke saturation"],
          ["strokeBrightness", "Stroke brightness"],
          ["dabHue", "Dab hue"],
          ["dabSaturation", "Dab saturation"],
          ["dabBrightness", "Dab brightness"],
          ["pressureBrightness", "Pressure brightness"],
        ].map(([key, label]) => slider(key, label))}
      </BrushCard>
      <BrushCard
        title="Pressure and surface"
        note="Mouse uses full pressure; pens and touch use PointerEvent pressure. Normal is derived from painted Height, composed with baked detail."
      >
        {slider("sizePressure", "Pressure size")}
        {slider("opacityPressure", "Pressure opacity")}
        {slider("heightStrength", "Height to normal", 2)}
      </BrushCard>
    </div>
  );
}
function DigitalChannelProperties({
  layer,
  brush,
  mask,
  ink,
  onInk,
  onChange,
  onPatch,
  disabled,
}) {
  const [opened, setOpened] = useState("baseColor");
  if (!layer)
    return (
      <div className="digital-target-empty">
        Select a layer or mask to paint.
      </div>
    );
  if (mask)
    return (
      <section
        className="digital-channel-panel"
        aria-label="Digital mask target"
      >
        <h2>
          <Layers3 size={13} /> Mask coverage
        </h2>
        <p>
          Paint changes grayscale coverage. Erase removes the painted overlay,
          preserving the original mask source.
        </p>
        <div className="digital-mask-inks">
          {[
            [1, "White", "#ffffff"],
            [0, "Black", "#000000"],
          ].map(([value, label, color]) => (
            <button
              key={value}
              aria-label={`${label} digital mask ink`}
              aria-pressed={ink === value}
              disabled={disabled}
              onClick={() => onInk(value)}
            >
              <i style={{ background: color }} />
              {label}
            </button>
          ))}
        </div>
        <p>
          Layer colour channels are not changed when painting a mask. Existing
          face / island / object selections constrain the stroke.
        </p>
      </section>
    );
  const enabled = layer.channels;
  const toggle = (id, on) =>
    onPatch({ channels: togglePaintChannel(enabled, id, on) });
  const setValue = (id, value) => {
    if (id === "baseColor") onChange({ ...brush, color: value });
    else
      onPatch({
        channelSettings: {
          ...layer.channelSettings,
          [id]: { ...layer.channelSettings[id], value },
        },
      });
  };
  return (
    <section
      className="digital-channel-panel"
      aria-label="Digital painting channels"
    >
      <h2>
        <Layers3 size={13} /> Channel targets{" "}
        <small>{enabled.length} / 14</small>
      </h2>
      <p>
        Enable the channels this brush writes. Erase only affects enabled
        channels. Disabled values and strokes are retained.
      </p>
      <button
        className="digital-clear-channels"
        disabled={disabled || !enabled.length}
        onClick={() => onPatch({ channels: [] })}
      >
        Disable all channels
      </button>
      {CHANNEL_GROUPS.map((group) => (
        <div key={group} className="digital-channel-group">
          <h3>{group}</h3>
          {PAINT_CHANNELS.filter((c) => c.group === group).map((c) => (
            <div
              key={c.id}
              className="digital-channel-target"
              data-enabled={enabled.includes(c.id)}
            >
              <div className="digital-channel-target-row">
                <label>
                  <input
                    type="checkbox"
                    aria-label={`Digital ${c.label} channel`}
                    checked={enabled.includes(c.id)}
                    disabled={disabled}
                    onChange={(e) => toggle(c.id, e.target.checked)}
                  />
                  <i style={{ background: c.color }} />
                  {c.label}
                </label>
                <button
                  aria-label={`Digital ${c.label} properties`}
                  aria-expanded={opened === c.id}
                  disabled={disabled || !enabled.includes(c.id)}
                  onClick={() => setOpened(opened === c.id ? null : c.id)}
                >
                  <ChevronDown size={12} />
                </button>
              </div>
              {enabled.includes(c.id) && opened === c.id && (
                <div className="digital-channel-value tp-shared-controls">
                  {c.edit === "derived" ? (
                    <p>
                      Derived from painted Height. Enabling Normal also enables
                      Height.
                    </p>
                  ) : c.edit === "color" ? (
                    <ColourPicker
                      value={
                        c.id === "baseColor"
                          ? brush.color
                          : layer.channelSettings[c.id].value
                      }
                      ariaLabel={`Digital ${c.label} colour`}
                      disabled={disabled}
                      onChange={(value) => setValue(c.id, value)}
                    />
                  ) : (
                    <Slider
                      label={c.label}
                      ariaLabel={`Digital ${c.label} target`}
                      value={layer.channelSettings[c.id].value}
                      disabled={disabled}
                      onChange={(value) => setValue(c.id, value)}
                      min={c.min}
                      max={c.max}
                      step={c.max > 3 ? 1 : 0.01}
                      unit={c.unit || ""}
                    />
                  )}
                </div>
              )}
            </div>
          ))}
        </div>
      ))}
      {!enabled.length && (
        <p role="status">No channels enabled — painting is a no-op.</p>
      )}
      <p className="digital-card-note">
        Base color uses the brush colour. Other targets use their saved channel
        values. Subsurface is a diffuse tint, not volumetric scattering.
      </p>
    </section>
  );
}
export default function DigitalPaintMenu({
  open,
  initialCatalogue = false,
  onClose,
  painting,
  layer,
  mask,
  ink,
  onInk,
  onPatch,
  disabled,
  legacySettings,
  onLegacyChange,
  onSave,
  onUndo,
  onRedo,
}) {
  const [catalogue, setCatalogue] = useState(false),
    [tab, setTab] = useState("brush"),
    ref = useRef(null);
  useDialogFocus(open && !catalogue, ref);
  useEffect(() => {
    if (open) {
      setCatalogue(initialCatalogue);
      setTab("brush");
    }
  }, [open, initialCatalogue]);
  useEffect(() => {
    if (!open || catalogue) return;
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
        onClose();
      }
    };
    window.addEventListener("keydown", key, true);
    return () => window.removeEventListener("keydown", key, true);
  }, [open, catalogue, onClose, onSave, onUndo, onRedo]);
  if (!open) return null;
  if (catalogue)
    return (
      <PaintToolMenu
        open
        settings={legacySettings}
        onChange={onLegacyChange}
        onClose={onClose}
        onSave={onSave}
        onUndo={onUndo}
        onRedo={onRedo}
      />
    );
  const brush = painting.brush,
    name = DIGITAL_BRUSH_PRESETS.find((p) => p.id === brush.preset)?.name;
  const useTool = () => {
    painting.arm(brush.tool);
    onClose();
  };
  return (
    <div
      className="paint-menu-backdrop"
      onPointerDown={(e) => {
        if (e.target === e.currentTarget) onClose();
      }}
    >
      <section
        className="alloy-digital-menu"
        role="dialog"
        aria-modal="true"
        aria-label="Digital brush studio"
        tabIndex={-1}
        ref={ref}
        data-properties-tab={tab}
      >
        <header className="digital-menu-heading">
          <Brush size={17} />
          <div>
            <strong>Digital brush studio</strong>
            <small>
              {mask ? "Mask coverage" : "Layer channels"} ·{" "}
              {layer?.name || "Select a layer"}
            </small>
          </div>
          <span>Digital only</span>
          <button aria-label="Close digital brush studio" onClick={onClose}>
            <X size={17} />
          </button>
        </header>
        <div className="digital-menu-toolbar">
          <div
            className="digital-tool-circles"
            role="group"
            aria-label="Digital painting tools"
          >
            {tools.map(([id, label, Icon]) => (
              <button
                key={id}
                aria-label={`Digital tool ${label}`}
                aria-pressed={brush.tool === id}
                onClick={() =>
                  painting.changeBrush(
                    { ...brush, tool: id },
                    { discrete: true },
                  )
                }
              >
                <i>
                  <Icon size={21} />
                </i>
                <span>{label}</span>
              </button>
            ))}
          </div>
          <div
            className="digital-preset-circles"
            role="group"
            aria-label="Digital brush presets"
          >
            {DIGITAL_BRUSH_PRESETS.map((p) => (
              <button
                key={p.id}
                title={p.note}
                aria-label={`Digital brush ${p.name}`}
                aria-pressed={brush.preset === p.id}
                onClick={() =>
                  painting.changeBrush(selectDigitalBrush(brush, p.id), {
                    discrete: true,
                  })
                }
              >
                <i>
                  <span className={`digital-tip-icon tip-${p.id}`} />
                  <Check size={10} />
                </i>
                <span>{p.name}</span>
              </button>
            ))}
          </div>
        </div>
        <DigitalStrokePreview brush={brush} mask={mask} ink={ink} />
        <nav
          className="digital-property-tabs"
          aria-label="Digital brush property tabs"
        >
          <button
            aria-pressed={tab === "brush"}
            onClick={() => setTab("brush")}
          >
            <SlidersHorizontal size={12} /> Brush properties
          </button>
          <button
            aria-pressed={tab === "channels"}
            onClick={() => setTab("channels")}
          >
            <Layers3 size={12} /> Channel properties
          </button>
        </nav>
        <div className="digital-menu-body">
          <div className="digital-brush-column">
            <div className="digital-brush-summary">
              <div>
                <small>ACTIVE DIGITAL TIP</small>
                <h1>{name}</h1>
              </div>
              <button
                aria-label="Reset digital brush properties"
                onClick={() =>
                  painting.changeBrush(
                    selectDigitalBrush(brush, brush.preset),
                    { discrete: true },
                  )
                }
              >
                <RotateCcw size={12} /> Reset
              </button>
            </div>
            {brush.tool === "fill" && (
              <label className="digital-fill-scope digital-select">
                Fill region
                <select
                  aria-label="Digital fill region"
                  value={brush.fillScope}
                  onChange={(e) =>
                    painting.changeBrush(
                      { ...brush, fillScope: e.target.value },
                      { discrete: true },
                    )
                  }
                >
                  <option value="object">Hit mesh component</option>
                  <option value="island">UV island</option>
                  <option value="face">Face</option>
                  <option value="tile">UV tile (UV view only)</option>
                </select>
                <ChevronDown size={12} />
              </label>
            )}
            <DigitalBrushProperties
              brush={brush}
              onChange={painting.changeBrush}
              mask={mask}
            />
          </div>
          <DigitalChannelProperties
            layer={layer}
            brush={brush}
            mask={mask}
            ink={ink}
            onInk={onInk}
            onChange={painting.changeBrush}
            onPatch={onPatch}
            disabled={disabled}
          />
        </div>
        <footer className="digital-menu-footer">
          <button
            className="digital-catalogue-link"
            onClick={() => setCatalogue(true)}
          >
            Instrument catalogue <small>configuration only</small>
          </button>
          <span>
            {painting.available
              ? mask
                ? "Painting the selected mask"
                : `${layer.channels.length} channel targets · undo per gesture`
              : "Select an unlocked, visible target in Lit or packed UV view"}
          </span>
          <button
            className="digital-use-tool"
            disabled={!painting.available}
            onClick={useTool}
          >
            {brush.tool === "erase" ? (
              <Eraser size={14} />
            ) : brush.tool === "fill" ? (
              <PaintBucket size={14} />
            ) : (
              <Brush size={14} />
            )}{" "}
            Use {brush.tool} <ArrowRight size={12} />
          </button>
        </footer>
      </section>
    </div>
  );
}
export function DigitalPaintToolbar({ painting, onOpen, mask = false }) {
  return (
    <div
      className="digital-viewport-tools"
      role="toolbar"
      aria-label="Digital surface painting"
    >
      {tools.map(([id, label, Icon]) => (
        <button
          key={id}
          aria-label={`${label} ${mask ? "mask digitally" : "layer channels"}`}
          aria-pressed={painting.active && painting.brush.tool === id}
          disabled={!painting.available}
          onClick={() => painting.arm(id)}
        >
          <Icon size={12} />
          {label}
        </button>
      ))}
      <button
        className="digital-brush-settings"
        aria-label="Digital brush settings"
        onClick={onOpen}
      >
        <SlidersHorizontal size={12} />
        {Math.round(painting.brush.size)} px
      </button>
    </div>
  );
}
