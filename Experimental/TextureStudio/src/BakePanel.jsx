import React, { useState, useRef, useEffect } from "react";
import {
  Layers3,
  ArrowDownToLine,
  LoaderCircle,
  Check,
  Palette,
  Sparkles,
  Mountain,
  Move3D,
  Contrast,
} from "lucide-react";
import { bakeMaterial, bakeChannels } from "./bakeMaterial.js";
import { SURFACE_BAKE_MAPS } from "./surfaceBakeMaps.js";
const icons = {
  color: Palette,
  mask: Contrast,
  normal: Move3D,
  height: Mountain,
  light: Sparkles,
};
export default function BakePanel({ params, onNotify, onPreviews = () => {} }) {
  const [resolution, setResolution] = useState(512),
    [width, setWidth] = useState(100),
    [channels, setChannels] = useState([...bakeChannels]);
  const [progress, setProgress] = useState(null),
    [error, setError] = useState(""),
    [busy, setBusy] = useState(false),
    [previews, setPreviews] = useState([]),
    [stamp, setStamp] = useState("");
  const controller = useRef(null),
    mounted = useRef(true),
    urls = useRef([]);
  useEffect(() => {
    mounted.current = true;
    return () => {
      mounted.current = false;
      controller.current?.abort();
      urls.current.forEach((p) => URL.revokeObjectURL(p.url));
      onPreviews([]);
    };
  }, []);
  const currentStamp = JSON.stringify({ params, resolution, width, channels });
  const stale = !!stamp && stamp !== currentStamp;
  async function bake() {
    setError("");
    setBusy(true);
    controller.current = new AbortController();
    try {
      const { bytes, maps } = await bakeMaterial(params, {
        resolution,
        widthMM: width,
        channels,
        signal: controller.current.signal,
        onProgress: (p) => {
          if (mounted.current) setProgress(p);
        },
      });
      if (!mounted.current) return;
      const next = Object.entries(maps).map(([key, pixels]) => ({
        key,
        set: params.name,
        resolution,
        url: URL.createObjectURL(new Blob([pixels], { type: "image/png" })),
      }));
      urls.current.forEach((p) => URL.revokeObjectURL(p.url));
      urls.current = next;
      setPreviews(next);
      setStamp(currentStamp);
      onPreviews(next);
      const url = URL.createObjectURL(
          new Blob([bytes], { type: "application/zip" }),
        ),
        a = document.createElement("a");
      a.href = url;
      a.download = params.id + "-maps.zip";
      a.click();
      setTimeout(() => URL.revokeObjectURL(url), 30000);
      onNotify(
        `${channels.length} procedural maps and the material recipe exported.`,
      );
    } catch (e) {
      if (mounted.current)
        setError(e.name === "AbortError" ? "Bake cancelled." : e.message);
    } finally {
      if (mounted.current) setBusy(false);
    }
  }
  return (
    <>
      <div className="modal-icon">
        <Layers3 size={24} />
      </div>
      <span className="eyebrow">FROM MATH TO MAPS</span>
      <h2 id="modal-title">Bake a surface patch</h2>
      <p>{params.name} · unlit channels from the same procedural shader.</p>
      <div className="bake-settings">
        <label>
          Resolution
          <select
            aria-label="Bake resolution"
            value={resolution}
            disabled={busy}
            onChange={(e) => setResolution(Number(e.target.value))}
          >
            {[256, 512, 1024, 2048].map((n) => (
              <option key={n} value={n}>
                {n} × {n}
              </option>
            ))}
          </select>
        </label>
        <label>
          Patch width · mm
          <input
            aria-label="Bake patch width"
            type="number"
            min="1"
            max="1000"
            value={width}
            disabled={busy}
            onChange={(e) => setWidth(Number(e.target.value))}
          />
        </label>
      </div>
      <div
        className="bk-map-presets"
        role="group"
        aria-label="Surface map presets"
      >
        <button disabled={busy} onClick={() => setChannels([...bakeChannels])}>
          All surface maps
        </button>
        <button
          disabled={busy}
          onClick={() => setChannels(["roughness", "metalness", "height"])}
        >
          Masks / data
        </button>
        <button disabled={busy} onClick={() => setChannels([])}>
          Clear surface maps
        </button>
      </div>
      <div
        className="bk-map-grid surface-bake-grid"
        role="group"
        aria-label="Surface bake maps"
      >
        {Object.entries(SURFACE_BAKE_MAPS).map(([key, map]) => {
          const Icon = icons[map.icon],
            selected = channels.includes(key),
            image = previews.find((p) => p.key === key);
          return (
            <button
              key={key}
              className="bk-map-tile"
              aria-label={`Bake surface ${map.label}`}
              aria-pressed={selected}
              disabled={busy}
              title={map.description}
              onClick={() =>
                setChannels((list) =>
                  selected ? list.filter((k) => k !== key) : [...list, key],
                )
              }
            >
              <span
                className={`bk-map-swatch bk-swatch-${map.swatch}`}
                aria-hidden="true"
              >
                {image ? <img src={image.url} alt="" /> : <Icon size={22} />}{" "}
                {selected && (
                  <i>
                    <Check size={11} />
                  </i>
                )}
              </span>
              <strong>{map.label}</strong>
              <small>
                {map.type === "scalar"
                  ? "Grayscale mask / data"
                  : map.colorSpace}
              </small>
            </button>
          );
        })}
      </div>
      {stale && (
        <p className="bk-volume-warning" role="status">
          Preview PNGs are from the previous bake; settings or map selection
          changed.
        </p>
      )}
      <p className="help-text">
        Click tiles to select. Green maps will be exported. Icons/swatches are
        illustrative until the first bake; thumbnails then show the actual PNGs.
      </p>
      <p className="bake-disclaimer">
        Flat XY patch, not a mesh UV bake. Seamless tiling is not guaranteed.
        Optical effects remain in the recipe; maps alone cannot capture
        scattering or angle-dependent specular. PNG channels are 8-bit.
      </p>
      {progress && (
        <div className="bake-progress" role="status">
          <span>
            {progress.label} · {progress.done}/{progress.total}
          </span>
          <progress
            aria-label="Bake progress"
            value={progress.done}
            max={progress.total}
          />
        </div>
      )}
      {error && (
        <p className="bake-error" role="alert">
          {error}
        </p>
      )}
      <button
        className="primary-button"
        disabled={
          busy ||
          !channels.length ||
          !Number.isFinite(width) ||
          width < 1 ||
          width > 1000
        }
        onClick={bake}
      >
        {busy ? (
          <LoaderCircle className="spin" size={16} />
        ) : (
          <ArrowDownToLine size={16} />
        )}{" "}
        {busy ? "Baking procedural maps…" : "Bake & download ZIP"}
      </button>
      {busy && (
        <button
          className="bake-cancel"
          onClick={() => controller.current?.abort()}
        >
          Cancel after current GPU pass
        </button>
      )}
      <p className="modal-footnote">
        {channels.length} selected PNG maps + scale/channel metadata + original
        recipe. No input textures are used.
      </p>
    </>
  );
}
