import React, { useEffect, useRef, useState } from "react";
import { Box, Download, LoaderCircle } from "lucide-react";
import { startVolumeSdf, packageVolumeSdf } from "./volumeSdfJob.js";
export default function VolumeSdfPanel({
  parts,
  onBusy,
  onProgress,
  onPreview,
}) {
  const [resolution, setResolution] = useState(64),
    [signMode, setSignMode] = useState("strict"),
    [padding, setPadding] = useState(0.1);
  const [busy, setBusy] = useState(false),
    [error, setError] = useState(""),
    [result, setResult] = useState(null),
    [stale, setStale] = useState(false);
  const controller = useRef(null),
    mounted = useRef(true),
    urls = useRef([]),
    previousParts = useRef(parts);
  useEffect(
    () => () => {
      mounted.current = false;
      controller.current?.abort();
      urls.current.forEach((p) => URL.revokeObjectURL(p.url));
      onPreview([]);
    },
    [],
  );
  useEffect(() => {
    if (previousParts.current !== parts) {
      setStale(true);
      previousParts.current = parts;
    }
  }, [parts]);
  async function bake() {
    setBusy(true);
    setError("");
    controller.current = new AbortController();
    onBusy(true, controller.current);
    try {
      const raw = await startVolumeSdf(
        parts,
        { resolution, signMode, padding },
        { signal: controller.current.signal, onProgress },
      );
      const packed = await packageVolumeSdf(raw, {
        signal: controller.current.signal,
      });
      if (!mounted.current) {
        packed.previews.forEach((p) => URL.revokeObjectURL(p.url));
        return;
      }
      urls.current.forEach((p) => URL.revokeObjectURL(p.url));
      urls.current = packed.previews;
      setResult(packed);
      setStale(false);
      onPreview(packed.previews);
    } catch (e) {
      if (mounted.current)
        setError(e.name === "AbortError" ? "SDF bake cancelled." : e.message);
    } finally {
      if (mounted.current) setBusy(false);
      if (mounted.current) onBusy(false);
    }
  }
  function download() {
    const url = URL.createObjectURL(
        new Blob([result.bytes], { type: "application/zip" }),
      ),
      a = document.createElement("a");
    a.href = url;
    a.download = "Alloy-volume-SDF.zip";
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }
  return (
    <div className="bk-volume-panel">
      <div className="bk-inspector-title">
        <Box size={27} />
        <div>
          <span className="eyebrow">HIGH SOURCE → VOXEL VOLUME</span>
          <h2>3D distance field</h2>
        </div>
      </div>
      <p className="help-text">
        Full Float32 Euclidean distances, not a height texture. Negative inside,
        positive outside. Strict signing requires a closed solid.
      </p>
      <section className="bk-card">
        <h2>Volume settings</h2>
        <label className="bk-field">
          <span>Voxel resolution</span>
          <select
            aria-label="SDF voxel resolution"
            value={resolution}
            disabled={busy}
            onChange={(e) => {
              setResolution(Number(e.target.value));
              setStale(true);
            }}
          >
            {[16, 32, 64, 96, 128].map((n) => (
              <option key={n} value={n}>
                {n} × {n} × {n}
              </option>
            ))}
          </select>
        </label>
        <label className="bk-field">
          <span>Sign handling</span>
          <select
            aria-label="SDF sign handling"
            value={signMode}
            disabled={busy}
            onChange={(e) => {
              setSignMode(e.target.value);
              setStale(true);
            }}
          >
            <option value="strict">Signed · closed solid required</option>
            <option value="approximate">
              Approximate signed · open sources
            </option>
            <option value="unsigned">Unsigned distance only</option>
          </select>
        </label>
        <label className="bk-field">
          <span>Bounds padding</span>
          <input
            aria-label="SDF bounds padding"
            type="number"
            min={0.01}
            max={0.5}
            step={0.01}
            value={padding}
            disabled={busy}
            onChange={(e) => {
              setPadding(Number(e.target.value));
              setStale(true);
            }}
          />
        </label>
        <p className="help-text">
          Voxel resolution is separate from 512×512 map resolution. Range is
          stored unclamped, in source scene units. No automatic remesh or hole
          sealing.
        </p>
      </section>
      {signMode === "approximate" && (
        <p className="bk-volume-warning" role="status">
          Open/intersecting meshes do not define a guaranteed solid. Approximate
          parity signs and the agreement mask are exported explicitly; agreement
          is not proof of a correct sign.
        </p>
      )}
      {error && (
        <p className="bake-error" role="alert">
          {error}
        </p>
      )}
      <button
        className="bk-primary"
        disabled={busy || !parts.length}
        onClick={bake}
      >
        {busy ? <LoaderCircle className="spin" size={14} /> : <Box size={14} />}{" "}
        {busy ? "Baking volume…" : "Bake 3D SDF"}
      </button>
      {busy && (
        <button onClick={() => controller.current?.abort()}>
          Cancel volume bake
        </button>
      )}
      {result && (
        <>
          <section className="bk-card">
            <h2>
              {result.manifest.signed ? "Signed volume" : "Unsigned volume"}
              {stale ? " · previous settings" : ""}
            </h2>
            <p>
              {result.manifest.dimensions.join(" × ")} ·{" "}
              {result.manifest.stats.voxels.toLocaleString()} voxels
            </p>
            <p>
              {result.manifest.topology.boundaryEdges} boundary edges ·{" "}
              {result.manifest.stats.ambiguous.toLocaleString()} ray
              disagreements
            </p>
            {result.manifest.approximate && (
              <p className="bk-volume-warning">
                Approximate / non-closed source. Inspect the sign-agreement
                mask. Do not use these signs as certified collision geometry.
              </p>
            )}
          </section>
          <div className="bk-volume-slices">
            {result.previews.map((p) => (
              <button
                key={p.key}
                title="Open volume slice"
                onClick={() => onPreview(result.previews, p.key)}
              >
                <img src={p.url} alt={`Volume ${p.key} midpoint preview`} />
                <span>{p.key}</span>
              </button>
            ))}
          </div>
          <button className="bk-primary" onClick={download}>
            <Download size={14} />
            Download SDF volume ZIP
          </button>
          <p className="help-text">
            distance.f32 + sign-confidence.u8 + manifest + X/Y/Z slice previews.
            Preview PNGs are 8-bit; the full volume is not.
          </p>
        </>
      )}
    </div>
  );
}
