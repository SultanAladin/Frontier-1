import React, { useId } from "react";
import { Check, Layers3 } from "lucide-react";
import {
  MESH_MAPS,
  MESH_MAP_PRESETS,
  OTHER_BAKE_FAMILIES,
} from "./meshMaps.js";

export default function MeshMapGrid({
  channels,
  disabled,
  previews,
  onChange,
  onInspect,
}) {
  const id = useId();
  const groups = [...new Set(Object.values(MESH_MAPS).map((m) => m.group))];
  return (
    <section className="bk-map-picker" aria-label="Mesh maps">
      <div className="bk-map-picker-heading">
        <h2>
          <Layers3 size={14} /> Mesh maps
        </h2>
        <span aria-live="polite">
          {channels.length} / {Object.keys(MESH_MAPS).length} enabled
        </span>
      </div>
      <p>Click a tile to enable or disable it. Green tiles will be baked.</p>
      <div
        className="bk-map-presets"
        role="group"
        aria-label="Mesh map presets"
      >
        {Object.entries(MESH_MAP_PRESETS).map(([label, selection]) => (
          <button
            key={label}
            disabled={disabled}
            onClick={() => onChange(selection)}
          >
            {label}
          </button>
        ))}
        <button
          disabled={disabled}
          onClick={() => onChange(Object.keys(MESH_MAPS))}
        >
          All maps
        </button>
        <button
          disabled={disabled || !channels.length}
          onClick={() => onChange([])}
        >
          Clear maps
        </button>
      </div>
      {groups.map((group, index) => (
        <section
          key={group}
          className="bk-map-group"
          aria-labelledby={`${id}-${index}`}
        >
          <h3 id={`${id}-${index}`}>{group}</h3>
          <div className="bk-map-grid">
            {Object.entries(MESH_MAPS)
              .filter(([, m]) => m.group === group)
              .map(([key, m]) => {
                const enabled = channels.includes(key),
                  image = previews.find((p) => p.key === key);
                return (
                  <button
                    key={key}
                    className="bk-map-tile"
                    aria-label={`Bake ${m.label}`}
                    aria-pressed={enabled}
                    disabled={disabled}
                    title={m.description}
                    data-map={key}
                    onClick={() => {
                      onChange(
                        enabled
                          ? channels.filter((k) => k !== key)
                          : [...channels, key],
                      );
                      onInspect(key);
                    }}
                  >
                    <span
                      className={`bk-map-swatch bk-swatch-${m.swatch}`}
                      aria-hidden="true"
                    >
                      {image ? (
                        <img src={image.url} alt="" />
                      ) : (
                        <span>
                          {m.space
                            ? "XYZ"
                            : key === "uv-island"
                              ? "UV"
                              : key === "id"
                                ? "ID"
                                : ""}
                        </span>
                      )}
                      {enabled && (
                        <i>
                          <Check size={11} />
                        </i>
                      )}
                    </span>
                    <strong>{m.label}</strong>
                    <small>
                      {m.derived
                        ? "Derived mask / effect"
                        : m.space
                          ? `${m.space} space`
                          : m.type === "scalar"
                            ? "Grayscale"
                            : m.type === "color"
                              ? "RGB"
                              : "Vector data"}
                    </small>
                  </button>
                );
              })}
          </div>
        </section>
      ))}
      {!channels.length && (
        <p className="bk-selection-empty" role="status">
          No maps enabled. Click a tile to start a bake.
        </p>
      )}
      <details className="bk-map-research">
        <summary>Other bake families & requirements</summary>
        <p>
          Reviewed against Substance, Toolbag and Blender. These are not
          geometry-only outputs and are not substituted with fake maps.
        </p>
        {OTHER_BAKE_FAMILIES.map((family) => (
          <div key={family.label}>
            <strong>{family.label}</strong>
            <p>{family.types}</p>
            <small>{family.requirement}</small>
          </div>
        ))}
        <a
          href="https://docs.marmoset.co/docs/map-types/"
          target="_blank"
          rel="noreferrer"
        >
          Toolbag map reference ↗
        </a>
        <a
          href="https://docs.blender.org/manual/en/latest/render/cycles/baking.html"
          target="_blank"
          rel="noreferrer"
        >
          Blender bake reference ↗
        </a>
      </details>
    </section>
  );
}
