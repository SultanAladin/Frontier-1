import React, { useRef, useState } from "react";
import { ArrowUp, ArrowDown, Plus, Trash2 } from "lucide-react";
import { Slider, ColorField } from "./MaterialControls.jsx";
import { defaultSandLayer, normalizeSandLayers } from "./materialProfiles.js";

export default function SandLayersEditor({ params, setParams }) {
  const beds = normalizeSandLayers(params.sandLayers);
  const [selected, setSelected] = useState(beds[0].id);
  const serial = useRef(0);
  const index = Math.max(
    0,
    beds.findIndex((b) => b.id === selected),
  );
  const bed = beds[index];
  const change = (transform) =>
    setParams((p) => ({
      ...p,
      sandLayers: normalizeSandLayers(
        transform(normalizeSandLayers(p.sandLayers)),
      ),
    }));
  const patch = (key, value) =>
    change((layers) =>
      layers.map((b) => (b.id === bed.id ? { ...b, [key]: value } : b)),
    );
  const slider = (key, label, min, max, unit = "", log = false) => (
    <Slider
      key={key}
      label={`Bed ${label}`}
      min={min}
      max={max}
      step={0.01}
      unit={unit}
      log={log}
      value={bed[key]}
      onChange={(value) => patch(key, value)}
    />
  );
  return (
    <div className="sand-bed-editor">
      <label className="weave-select">
        <span>Layer arrangement</span>
        <select
          aria-label="Sand layer arrangement"
          value={params.sandArrangement || "beds"}
          onChange={(e) =>
            setParams((p) => ({ ...p, sandArrangement: e.target.value }))
          }
        >
          <option value="beds">Stacked grain beds</option>
          <option value="bands">Sediment bands</option>
        </select>
      </label>
      <p className="help-text">
        Top bed occludes the lower grains where it covers them. Bands expose
        each bed's own properties in wavy strata. These are surface shaders, not
        loose particles or simulated erosion.
      </p>
      <div className="sand-bed-list" role="group" aria-label="Sand beds">
        {beds.map((b, i) => (
          <div className="sand-bed-row" key={b.id} data-sand-bed-id={b.id}>
            <button
              aria-label={`Select sand bed ${i + 1}`}
              aria-pressed={b.id === bed.id}
              onClick={() => setSelected(b.id)}
            >
              <i style={{ background: b.color }} />
              <span>
                {b.name}
                <small>
                  {b.size} mm · rough {b.roughness} · spec {b.specular}
                </small>
              </span>
            </button>
            <input
              type="checkbox"
              aria-label={`Enable sand bed ${i + 1}`}
              checked={b.enabled}
              onChange={(e) =>
                change((layers) =>
                  layers.map((v) =>
                    v.id === b.id ? { ...v, enabled: e.target.checked } : v,
                  ),
                )
              }
            />
          </div>
        ))}
      </div>
      <div className="sand-bed-actions">
        <button
          disabled={beds.length >= 4}
          aria-label="Add sand bed"
          onClick={() => {
            const next = {
              ...defaultSandLayer(beds.length),
              id: `sand-bed-${Date.now().toString(36)}-${serial.current++}`,
            };
            change((layers) => [...layers, next]);
            setSelected(next.id);
          }}
        >
          <Plus size={13} />
          Add bed
        </button>
        <button
          aria-label="Move sand bed up"
          disabled={index === 0}
          onClick={() =>
            change((layers) => {
              [layers[index - 1], layers[index]] = [
                layers[index],
                layers[index - 1],
              ];
              return layers;
            })
          }
        >
          <ArrowUp size={13} />
        </button>
        <button
          aria-label="Move sand bed down"
          disabled={index === beds.length - 1}
          onClick={() =>
            change((layers) => {
              [layers[index + 1], layers[index]] = [
                layers[index],
                layers[index + 1],
              ];
              return layers;
            })
          }
        >
          <ArrowDown size={13} />
        </button>
        <button
          aria-label="Remove sand bed"
          disabled={beds.length === 1}
          onClick={() => {
            change((layers) => layers.filter((b) => b.id !== bed.id));
            setSelected(beds[Math.max(0, index - 1)].id);
          }}
        >
          <Trash2 size={13} />
        </button>
      </div>
      <label className="sand-bed-name">
        <span>Bed name</span>
        <input
          aria-label="Sand bed name"
          maxLength={60}
          value={bed.name}
          onChange={(e) => patch("name", e.target.value)}
        />
      </label>
      <ColorField
        label="Bed mineral color"
        value={bed.color}
        onChange={(value) => patch("color", value)}
      />
      <ColorField
        label="Bed pale mineral color"
        value={bed.secondaryColor}
        onChange={(value) => patch("secondaryColor", value)}
      />
      {slider("size", "grain size", 0.0625, 2, "mm", true)}
      {slider("sizeVariation", "size variation", 0, 1)}
      {slider("roughness", "roughness", 0.04, 1)}
      {slider("roughnessVariation", "roughness variation", 0, 1)}
      {slider("specular", "specular", 0, 1)}
      {slider("specularVariation", "specular variation", 0, 1)}
      {slider("ior", "mineral IOR", 1.3, 2.2)}
      {slider("roundness", "grain roundness", 0, 1)}
      {slider("tilt", "facet tilt", 0, 1)}
      {slider("relief", "grain relief", 0, 1)}
      {slider("coverage", "coverage", 0, 1)}
      {params.sandArrangement === "bands" &&
        slider("thickness", "band thickness", 0.1, 4, "relative")}
    </div>
  );
}
