import PointGradientEditor from "./PointGradientEditor.jsx";
import MaterialGeneratorEditor from "./MaterialGeneratorEditor.jsx";
import { defaultPointGradient } from "./pointGradient.js";
import {
  MATERIAL_GENERATOR_ID,
  createMaterialGenerator,
} from "./materialGeneratorModel.js";
import "./pointGradient.css";
import TextureSourceCanvas from "./TextureSourceCanvas.jsx";
import React, { useEffect, useRef, useState } from "react";
import {
  Plus,
  X,
  ChevronDown,
  Layers3,
  Circle,
  Image,
  Sparkles,
  Upload,
  Trash2,
} from "lucide-react";
import { Slider, ColorField } from "./MaterialControls.jsx";
import {
  PAINT_CHANNELS,
  CHANNEL_GROUPS,
  CHANNEL_GENERATORS,
  togglePaintChannel,
  normalizeChannelSettings,
  normalizeGenerator,
} from "./paintChannelModel.js";
import { importTextureSource } from "./textureSource.js";
import "./channelPropertyPanel.css";
const SOURCE_MODES = [
  ["value", "Value", Circle],
  ["texture", "Texture", Image],
  ["generator", "Generator", Sparkles],
  ["gradient", "Gradient", Circle],
];
export default function ChannelPropertyPanel({
  layer,
  onPatch,
  painting,
  onStroke,
  onOpenTools,
  materialWorkspace,
  gradientContext,
  onGradientActivate,
  disabled = layer.locked,
}) {
  const [picker, setPicker] = useState(false),
    [folded, setFolded] = useState(
      new Set([
        "height",
        "normal",
        "occlusion",
        "anisotropy",
        "anisotropyAngle",
        "clearcoat",
      ]),
    ),
    [importing, setImporting] = useState(null),
    [paintPad, setPaintPad] = useState(null),
    [error, setError] = useState("");
  const settings = normalizeChannelSettings(layer.channelSettings),
    file = useRef(null),
    channel = useRef(null),
    current = useRef({ layer, disabled, onPatch }),
    alive = useRef(true);
  current.current = { layer, disabled, onPatch };
  useEffect(() => {
    alive.current = true;
    return () => {
      alive.current = false;
    };
  }, []);
  const toggle = (id, on) =>
    onPatch({ channels: togglePaintChannel(layer.channels, id, on) });
  const change = (id, patch, field) =>
    onPatch(
      { channelSettings: { ...settings, [id]: { ...settings[id], ...patch } } },
      {
        discrete: !field,
        key: field ? `channel-${layer.id}-${id}-${field}` : undefined,
      },
    );
  async function readTexture(e) {
    const input = e.target.files[0],
      id = channel.current;
    e.target.value = "";
    if (!input || !id || disabled) return;
    setImporting(id);
    setError("");
    try {
      const texture = await importTextureSource(input);
      if (!alive.current || current.current.disabled) return;
      const latest = normalizeChannelSettings(
        current.current.layer.channelSettings,
      );
      if (
        latest[id].mode !== "texture" ||
        !current.current.layer.channels.includes(id)
      )
        return;
      current.current.onPatch(
        {
          channelSettings: {
            ...latest,
            [id]: { ...latest[id], mode: "texture", texture },
          },
        },
        { discrete: true },
      );
    } catch (err) {
      if (alive.current) setError(err.message);
    } finally {
      if (alive.current) setImporting(null);
    }
  }
  return (
    <section
      className="channel-property-panel"
      aria-label="Channel property card"
    >
      <input
        ref={file}
        type="file"
        hidden
        accept="image/png,image/jpeg,image/webp"
        aria-label="Import channel texture file"
        onChange={readTexture}
      />
      <div className="channel-target-card tp-property-card">
        <div className="channel-target-heading">
          <Layers3 size={14} />
          <h2>Channel targets</h2>
          <span className="channel-count">{layer.channels.length}</span>
          <button
            aria-label="Add painting channel"
            aria-expanded={picker}
            disabled={disabled}
            onClick={() => setPicker(!picker)}
          >
            <Plus size={14} />
          </button>
        </div>
        <div className="channel-chips">
          {PAINT_CHANNELS.filter((c) => layer.channels.includes(c.id)).map(
            (c) => (
              <span className="channel-chip" key={c.id}>
                <i style={{ background: c.color }} />
                {c.label}
                <button
                  aria-label={`Disable ${c.label} channel`}
                  disabled={disabled}
                  onClick={() => toggle(c.id, false)}
                >
                  <X size={10} />
                </button>
              </span>
            ),
          )}
        </div>
        {!!layer.channels.length && (
          <button
            className="channel-clear"
            disabled={disabled}
            onClick={() => onPatch({ channels: [] })}
          >
            Clear all
          </button>
        )}
        {picker && (
          <div
            className="channel-picker"
            role="group"
            aria-label="Available painting channels"
          >
            {CHANNEL_GROUPS.map((group) => (
              <section key={group}>
                <h3>{group}</h3>
                {PAINT_CHANNELS.filter((c) => c.group === group).map((c) => (
                  <label key={c.id}>
                    <i style={{ background: c.color }} />
                    <span>{c.label}</span>
                    <input
                      type="checkbox"
                      aria-label={`${c.label} channel`}
                      checked={layer.channels.includes(c.id)}
                      disabled={disabled}
                      onChange={(e) => toggle(c.id, e.target.checked)}
                    />
                  </label>
                ))}
              </section>
            ))}
          </div>
        )}
        {!layer.channels.length && (
          <p className="chan-note" role="status">
            No painting channels enabled. Existing channel values are retained.
          </p>
        )}
      </div>
      {error && (
        <p className="channel-import-error" role="alert">
          {error}
          <button
            aria-label="Dismiss texture import error"
            onClick={() => setError("")}
          >
            <X size={12} />
          </button>
        </p>
      )}
      {PAINT_CHANNELS.filter((c) => layer.channels.includes(c.id)).map((c) => {
        const value = settings[c.id],
          generator = CHANNEL_GENERATORS.find(
            (g) => g.id === value.generator?.id,
          ),
          isFolded = folded.has(c.id);
        return (
          <section
            key={c.id}
            className={`chan-panel tp-property-card ${isFolded ? "collapsed" : ""}`}
            style={{ "--channel-tint": c.color }}
          >
            <button
              className="chan-head"
              aria-label={`${c.label} channel properties`}
              aria-expanded={!isFolded}
              onClick={() =>
                setFolded((old) => {
                  const next = new Set(old);
                  next.has(c.id) ? next.delete(c.id) : next.add(c.id);
                  return next;
                })
              }
            >
              <i />
              <span>{c.label}</span>
              <small>
                {value.mode === "derived"
                  ? "Height-derived"
                  : SOURCE_MODES.find((m) => m[0] === value.mode)?.[1]}
              </small>
              <ChevronDown size={12} />
            </button>
            {!isFolded && (
              <div className="chan-body">
                {c.edit === "derived" ? (
                  <p className="chan-note">
                    Normal follows the Height target. Derivation will be
                    evaluated by the paint compositor.
                  </p>
                ) : (
                  <>
                    <div
                      className="channel-mode"
                      role="group"
                      aria-label={`${c.label} source`}
                    >
                      {SOURCE_MODES.filter(
                        ([mode]) => mode !== "gradient" || c.id === "baseColor",
                      ).map(([mode, label, Icon]) => (
                        <button
                          key={mode}
                          disabled={disabled}
                          aria-pressed={value.mode === mode}
                          onClick={() =>
                            change(c.id, {
                              mode,
                              ...(mode === "gradient" && !value.gradient
                                ? { gradient: defaultPointGradient() }
                                : {}),
                            })
                          }
                        >
                          <Icon size={11} />
                          {label}
                        </button>
                      ))}
                    </div>
                    {value.mode === "gradient" ? (
                      <PointGradientEditor
                        gradient={value.gradient}
                        label={`${c.label} gradient`}
                        disabled={disabled}
                        editor={
                          gradientContext?.layerId === layer.id &&
                          !gradientContext.mask &&
                          gradientContext.channelId === c.id
                            ? gradientContext
                            : null
                        }
                        onActivate={(flags) =>
                          onGradientActivate({
                            layerId: layer.id,
                            channelId: c.id,
                            mask: false,
                            ...flags,
                          })
                        }
                        onChange={(gradient, field) =>
                          change(
                            c.id,
                            { gradient },
                            field ? `gradient-${field}` : null,
                          )
                        }
                      />
                    ) : value.mode === "texture" ? (
                      <div className="channel-texture-source">
                        {value.texture ? (
                          <>
                            <div className="channel-source-preview">
                              <img
                                src={value.texture.dataUrl}
                                alt={`${c.label} ${value.texture.origin === "paint" ? "painted" : "imported"} texture`}
                              />
                              <div>
                                <strong>{value.texture.name}</strong>
                                <small>
                                  {value.texture.width} × {value.texture.height}{" "}
                                  · stored source
                                </small>
                              </div>
                            </div>
                            <button
                              className="channel-source-remove"
                              disabled={disabled}
                              onClick={() => change(c.id, { texture: null })}
                            >
                              <Trash2 size={12} />
                              Remove texture
                            </button>
                          </>
                        ) : (
                          <div className="channel-texture-empty">
                            <Image size={23} />
                            <span>Paint or import a texture source</span>
                          </div>
                        )}
                        <button
                          className="channel-import-button"
                          disabled={disabled || !!importing}
                          onClick={() => {
                            channel.current = c.id;
                            file.current.click();
                          }}
                        >
                          <Upload size={12} />
                          {importing === c.id
                            ? "Importing…"
                            : value.texture
                              ? "Replace texture"
                              : "Import texture"}
                        </button>
                        {onStroke && (
                          <button
                            className="channel-paint-button"
                            disabled={disabled || !!importing}
                            aria-label={`Paint ${c.label} texture`}
                            aria-expanded={paintPad === c.id}
                            onClick={() =>
                              setPaintPad((p) => (p === c.id ? null : c.id))
                            }
                          >
                            <Image size={12} />
                            {paintPad === c.id
                              ? "Close source canvas"
                              : "Paint texture"}
                          </button>
                        )}
                        {paintPad === c.id && onStroke && (
                          <TextureSourceCanvas
                            key={`${layer.id}-${c.id}`}
                            layerId={layer.id}
                            layerName={layer.name}
                            channelId={c.id}
                            channelLabel={c.label}
                            texture={value.texture}
                            painting={painting}
                            disabled={disabled || !!importing}
                            onStroke={onStroke}
                            onOpenTools={onOpenTools}
                            onClose={() => setPaintPad(null)}
                          />
                        )}
                        <p className="chan-note">
                          PNG, JPEG or WebP · portable source up to 256 px.
                          Round-tip source strokes are recorded in History;
                          layer / teapot compositing is not active.
                        </p>
                      </div>
                    ) : value.mode === "generator" ? (
                      <>
                        <label className="channel-generator-label">
                          <span>Generator</span>
                          <select
                            aria-label={`${c.label} generator`}
                            value={value.generator?.id || ""}
                            disabled={disabled}
                            onChange={(e) =>
                              change(c.id, {
                                generator:
                                  e.target.value === MATERIAL_GENERATOR_ID
                                    ? createMaterialGenerator()
                                    : normalizeGenerator({
                                        id: e.target.value,
                                      }),
                              })
                            }
                          >
                            <option value="">Choose a generator…</option>
                            <option value={MATERIAL_GENERATOR_ID}>
                              Material studio · custom source
                            </option>
                            {["Mask", "Wear", "Procedural"].map((group) => (
                              <optgroup label={group} key={group}>
                                {CHANNEL_GENERATORS.filter(
                                  (g) => g.group === group,
                                ).map((g) => (
                                  <option key={g.id} value={g.id}>
                                    {g.label}
                                  </option>
                                ))}
                              </optgroup>
                            ))}
                          </select>
                        </label>
                        {value.generator?.id === MATERIAL_GENERATOR_ID && (
                          <MaterialGeneratorEditor
                            value={value.generator}
                            workspace={materialWorkspace}
                            disabled={disabled}
                            onChange={(generator, field) =>
                              change(
                                c.id,
                                { generator },
                                field ? `material-generator-${field}` : null,
                              )
                            }
                            onRendered={(generator, expected) => {
                              if (!alive.current || current.current.disabled)
                                return;
                              const latest = normalizeChannelSettings(
                                current.current.layer.channelSettings,
                              );
                              if (
                                latest[c.id].mode !== "generator" ||
                                latest[c.id].generator?.stamp !== expected ||
                                !current.current.layer.channels.includes(c.id)
                              )
                                return;
                              current.current.onPatch(
                                {
                                  channelSettings: {
                                    ...latest,
                                    [c.id]: { ...latest[c.id], generator },
                                  },
                                },
                                { discrete: true },
                              );
                            }}
                          />
                        )}
                        {generator && (
                          <>
                            <p className="chan-note">
                              {generator.note} · source configuration.
                            </p>
                            {generator.parameters.map((p) => (
                              <Slider
                                key={p.key}
                                label={p.label}
                                ariaLabel={`${c.label} ${generator.label} ${p.label}`}
                                value={value.generator.parameters[p.key]}
                                disabled={disabled}
                                onChange={(v) =>
                                  change(
                                    c.id,
                                    {
                                      generator: {
                                        ...value.generator,
                                        parameters: {
                                          ...value.generator.parameters,
                                          [p.key]: v,
                                        },
                                      },
                                    },
                                    `generator-${generator.id}-${p.key}`,
                                  )
                                }
                              />
                            ))}
                          </>
                        )}
                      </>
                    ) : c.edit === "color" ? (
                      <ColorField
                        label="Colour"
                        ariaLabel={`${c.label} paint value`}
                        value={value.value}
                        disabled={disabled}
                        onChange={(v) => change(c.id, { value: v }, "value")}
                      />
                    ) : (
                      <Slider
                        label="Amount"
                        ariaLabel={`${c.label} paint amount`}
                        valueLabel={`${c.label} paint value`}
                        value={value.value}
                        min={c.min}
                        max={c.max}
                        step={c.unit ? 1 : 0.01}
                        unit={c.unit || ""}
                        disabled={disabled}
                        onChange={(v) => change(c.id, { value: v }, "value")}
                      />
                    )}
                  </>
                )}
                {value.mode === "value" && (
                  <div className="channel-value-preview">
                    <i
                      style={{
                        background:
                          c.edit === "color"
                            ? value.value
                            : `rgb(${Math.round(((value.value - c.min) / (c.max - c.min)) * 255)},${Math.round(((value.value - c.min) / (c.max - c.min)) * 255)},${Math.round(((value.value - c.min) / (c.max - c.min)) * 255)})`,
                      }}
                    />
                    <span>Value swatch</span>
                    <small>Not composited</small>
                  </div>
                )}
              </div>
            )}
          </section>
        );
      })}
    </section>
  );
}
