import React, { useEffect, useRef, useState } from "react";
import { Sparkles, RefreshCw, Link, ChevronDown } from "lucide-react";
import RecipeInspector from "./RecipeInspector.jsx";
import { Slider } from "./MaterialControls.jsx";
import { materials } from "./materials.js";
import {
  createMaterialGenerator,
  normalizeMaterialGenerator,
  MATERIAL_GENERATOR_OUTPUTS,
} from "./materialGeneratorModel.js";
import { renderMaterialGenerator } from "./renderMaterialGenerator.js";
export default function MaterialGeneratorEditor({
  value,
  onChange,
  onRendered,
  workspace,
  disabled,
}) {
  const [busy, setBusy] = useState(false),
    [error, setError] = useState(""),
    [progress, setProgress] = useState(""),
    [retry, setRetry] = useState(0),
    latest = useRef({ value, disabled, onRendered });
  latest.current = { value, disabled, onRendered };
  useEffect(() => {
    if (value.result || disabled) return;
    const controller = new AbortController();
    let live = true;
    const timer = setTimeout(async () => {
      setBusy(true);
      setError("");
      try {
        const next = await renderMaterialGenerator(value, {
          signal: controller.signal,
          onProgress: (p) => {
            if (live) setProgress(p.label);
          },
        });
        if (
          live &&
          !latest.current.disabled &&
          latest.current.value.stamp === next.stamp
        )
          latest.current.onRendered(next, value.stamp);
      } catch (e) {
        if (live && e.name !== "AbortError") setError(e.message);
      } finally {
        if (live) setBusy(false);
      }
    }, 350);
    return () => {
      live = false;
      clearTimeout(timer);
      controller.abort();
    };
  }, [value.stamp, disabled, retry]);
  const change = (patch, key) =>
      onChange(
        normalizeMaterialGenerator({ ...value, ...patch, result: null }),
        key,
      ),
    setMaterial = (action) => {
      try {
        const material =
          typeof action === "function" ? action(value.material) : action;
        change({ material }, "material-controls");
      } catch (e) {
        setError(e.message);
      }
    };
  return (
    <div
      className="tp-material-generator"
      aria-label="Material studio generator"
    >
      <div className="tp-generator-heading">
        <Sparkles size={12} />
        <strong>Material studio source</strong>
        <small>Custom</small>
      </div>
      <button
        className="tp-generator-current"
        disabled={disabled || !workspace?.params}
        onClick={() => {
          try {
            onChange(createMaterialGenerator(workspace.params));
          } catch (e) {
            setError(e.message);
          }
        }}
      >
        <Link size={12} /> Use current Material-studio material
      </button>
      <label className="channel-generator-label">
        <span>Recipe / saved material</span>
        <select
          aria-label="Material generator preset"
          disabled={disabled}
          value={value.material.id || ""}
          onChange={(e) => {
            const preset =
              workspace?.materials?.find((m) => m.id === e.target.value) ||
              materials.find((m) => m.id === e.target.value);
            if (preset) onChange(createMaterialGenerator(preset));
          }}
        >
          <option value={value.material.id}>
            {value.material.name || value.material.recipeId}
          </option>
          {(workspace?.materials || materials)
            .filter((m) => m.id !== value.material.id)
            .map((m) => (
              <option key={m.id} value={m.id}>
                {m.name}
              </option>
            ))}
        </select>
      </label>
      <label className="channel-generator-label">
        <span>Field output</span>
        <select
          aria-label="Material generator output"
          value={value.output}
          disabled={disabled}
          onChange={(e) => change({ output: e.target.value })}
        >
          {MATERIAL_GENERATOR_OUTPUTS.map((id) => (
            <option key={id} value={id}>
              {id === "scratch-mask"
                ? "Scratch cavities mask"
                : id.replaceAll("-", " ")}
            </option>
          ))}
        </select>
      </label>
      <div className="tp-material-source-preview">
        {value.result ? (
          <img
            src={value.result.dataUrl}
            alt="Generated Material-studio field"
          />
        ) : (
          <div>
            <Sparkles size={21} />
            <span>{busy ? progress : "Preparing procedural field"}</span>
          </div>
        )}
        <small>
          Actual procedural channel pixels · flat patch, not a shaded screenshot
          / mesh map.
        </small>
      </div>
      <div className="tp-generator-transfer">
        <Slider
          label="Patch width"
          ariaLabel="Material generator patch width"
          value={value.widthMM}
          min={1}
          max={1000}
          step={1}
          unit="mm"
          disabled={disabled}
          onChange={(widthMM) => change({ widthMM }, "width")}
        />
        <Slider
          label="Black point"
          ariaLabel="Material generator black point"
          value={value.low}
          disabled={disabled}
          onChange={(low) => change({ low }, "low")}
        />
        <Slider
          label="White point"
          ariaLabel="Material generator white point"
          value={value.high}
          disabled={disabled}
          onChange={(high) => change({ high }, "high")}
        />
        <Slider
          label="Gamma"
          ariaLabel="Material generator gamma"
          value={value.gamma}
          min={0.1}
          max={8}
          step={0.01}
          disabled={disabled}
          onChange={(gamma) => change({ gamma }, "gamma")}
        />
        <label className="tp-toggle-row">
          <span>Invert field</span>
          <input
            type="checkbox"
            aria-label="Invert Material generator"
            disabled={disabled}
            checked={value.inverted}
            onChange={(e) => change({ inverted: e.target.checked })}
          />
        </label>
      </div>
      <details className="tp-generator-recipe" open>
        <summary>
          <ChevronDown size={11} /> Shared Material controls
        </summary>
        <fieldset
          disabled={disabled}
          className="studio-material tp-generator-material-controls"
        >
          <RecipeInspector
            params={value.material}
            setParams={setMaterial}
            update={(key, v) => setMaterial({ ...value.material, [key]: v })}
          />
        </fieldset>
      </details>
      <button
        className="tp-generator-refresh"
        disabled={disabled || busy}
        onClick={() => {
          if (value.result) change({ result: null });
          setRetry((v) => v + 1);
        }}
      >
        <RefreshCw size={11} />
        {busy ? "Rendering…" : "Regenerate source"}
      </button>
      {error && (
        <p role="alert" className="channel-import-error">
          {error}
        </p>
      )}
    </div>
  );
}
