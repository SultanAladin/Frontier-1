import {
  isTextureBakeTarget,
  textureMeshMapsFromBake,
  publishTextureMeshMaps,
} from "./textureMeshMapBridge.js";
import React, { useEffect, useMemo, useRef, useState } from "react";
import {
  Box,
  Check,
  ChevronDown,
  Download,
  FileJson,
  FolderOpen,
  Layers3,
  LoaderCircle,
  Maximize2,
  Redo2,
  SlidersHorizontal,
  Square,
  Undo2,
  Upload,
  X,
} from "lucide-react";
import { StudioHeader, StudioDockTab } from "./StudioUI.jsx";
import BakePanel from "./BakePanel.jsx";
import Viewport from "./Viewport.jsx";
import { MESH_MAPS } from "./baking/meshMaps.js";
import {
  BAKE_PREFS_KEY,
  BAKE_RECIPE_SCHEMA,
  defaultBakeRecipe,
  restoreBakeRecipe,
  parseBakeRecipe,
  normalizeBakeSelection,
} from "./baking/bakeRecipe.js";
import MeshMapGrid from "./baking/MeshMapGrid.jsx";
import VolumeSdfPanel from "./baking/VolumeSdfPanel.jsx";
import { SURFACE_BAKE_MAPS } from "./surfaceBakeMaps.js";
import {
  createDemoBakeAssets,
  disposeBakeParts,
  importBakeMesh,
  meshSummary,
} from "./baking/bakeMeshes.js";
import { startMeshBake, packageMeshBake } from "./baking/bakeMeshJob.js";
import MeshBakeViewport from "./baking/MeshBakeViewport.jsx";

function initialRecipe() {
  try {
    return restoreBakeRecipe(localStorage);
  } catch {
    return defaultBakeRecipe();
  }
}
function downloadBlob(blob, name) {
  const url = URL.createObjectURL(blob),
    a = document.createElement("a");
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1500);
}
export default function BakingStudio({
  params,
  currentShape,
  initialMode = "mesh",
  textureTarget = false,
  onWorkspace,
  onClose,
}) {
  const [initial] = useState(initialRecipe),
    [settings, setSettings] = useState(
      textureTarget
        ? {
            ...initial.settings,
            resolution: 512,
            channels: ["occlusion", "normal", "bevel-normal"],
          }
        : initial.settings,
    );
  const [assets, setAssets] = useState(() =>
      createDemoBakeAssets("Teapot", { textureTarget }),
    ),
    [demo, setDemo] = useState("Teapot"),
    [mode, setMode] = useState(initialMode);
  const [view, setView] = useState("mesh"),
    [inspectMap, setInspectMap] = useState(
      initial.settings.channels[0] || "normal",
    ),
    [mobilePanel, setMobilePanel] = useState("assets");
  const [selectedTargets, setSelectedTargets] = useState(["Teapot"]);
  const [busy, setBusy] = useState(false),
    [importing, setImporting] = useState(""),
    [progress, setProgress] = useState(null),
    [error, setError] = useState(""),
    [info, setInfo] = useState(
      initial.legacy
        ? "Legacy recipe settings recovered. Node effects are no longer applied; choose map tiles instead."
        : textureTarget
          ? "Texture support: matching 20-segment teapot low mesh, genuine high/low AO + normal + bevel bake at 512. Compatible results attach to locked Layer 0 without replacing user layers."
          : "Source meshes stay in this session. Recipes contain settings, not geometry.",
    );
  const [results, setResults] = useState([]),
    [previews, setPreviews] = useState([]),
    [zip, setZip] = useState(null),
    [stamp, setStamp] = useState(""),
    [previewSet, setPreviewSet] = useState(""),
    [previewMap, setPreviewMap] = useState("normal");
  const [volumePreviews, setVolumePreviews] = useState([]),
    [materialPreviews, setMaterialPreviews] = useState([]);
  const [overlay, setOverlay] = useState(false),
    [wireframe, setWireframe] = useState(false),
    [reset, setReset] = useState(0);
  const [, setRevision] = useState(0);
  const mounted = useRef(true),
    controller = useRef(null),
    lowInput = useRef(null),
    highInput = useRef(null),
    recipeInput = useRef(null),
    importEpoch = useRef(0),
    assetsRef = useRef(assets),
    previewsRef = useRef(previews),
    settingsRef = useRef(settings),
    history = useRef({ past: [], future: [], key: null, time: 0 });
  assetsRef.current = assets;
  previewsRef.current = previews;
  settingsRef.current = settings;
  useEffect(
    () => () => {
      mounted.current = false;
      importEpoch.current++;
      controller.current?.abort();
      disposeBakeParts(assetsRef.current.low);
      disposeBakeParts(assetsRef.current.high);
      previewsRef.current.forEach((p) => URL.revokeObjectURL(p.url));
    },
    [],
  );
  useEffect(() => {
    if (initial.legacyBackupFailed) {
      setInfo(
        "The old node recipe was kept unchanged because its backup could not be saved. Save a new recipe file to retain this map-only setup.",
      );
      return;
    }
    const timer = setTimeout(() => {
      try {
        localStorage.setItem(
          BAKE_PREFS_KEY,
          JSON.stringify({ schema: BAKE_RECIPE_SCHEMA, settings }),
        );
      } catch {
        setInfo(
          "Local recipe storage is unavailable. Save a recipe JSON file to keep your setup.",
        );
      }
    }, 400);
    return () => clearTimeout(timer);
  }, [settings, initial]);
  const lowSummary = useMemo(() => meshSummary(assets.low), [assets.low]),
    highSummary = useMemo(() => meshSummary(assets.high), [assets.high]);
  const targets = assets.low.filter((p) => selectedTargets.includes(p.name));
  const selectedPart =
    assets.low.find((p) => p.name === (previewSet || selectedTargets[0])) ||
    assets.low[0];
  const selectedMap = MESH_MAPS[inspectMap] || MESH_MAPS.normal;
  const currentStamp = JSON.stringify({
    settings,
    sources: assets.label,
    low: lowSummary,
    high: highSummary,
  });
  const stale = !!stamp && stamp !== currentStamp;
  const normalPreview =
    previews.find(
      (p) => p.set === selectedPart?.name && p.key === "bevel-normal",
    ) ||
    previews.find((p) => p.set === selectedPart?.name && p.key === "normal");
  const visiblePreviews =
    mode === "volume"
      ? volumePreviews
      : mode === "material"
        ? materialPreviews
        : previews;
  const activePreviewSet = visiblePreviews.some((p) => p.set === previewSet)
    ? previewSet
    : visiblePreviews[0]?.set;
  const image =
    visiblePreviews.find(
      (p) => p.set === activePreviewSet && p.key === previewMap,
    ) || visiblePreviews.find((p) => p.set === activePreviewSet);
  const mapMeta = (key) =>
    mode === "volume"
      ? { label: key.replace("-", " "), colorSpace: "linear" }
      : mode === "material"
        ? SURFACE_BAKE_MAPS[key]
        : MESH_MAPS[key];
  const receivePreviews = (list, key, kind) => {
    (kind === "volume" ? setVolumePreviews : setMaterialPreviews)(list);
    if (list.length) {
      setPreviewSet(list[0].set);
      setPreviewMap(key || list[0].key);
      setView("maps");
    }
  };
  function clearResults() {
    previewsRef.current.forEach((p) => URL.revokeObjectURL(p.url));
    previewsRef.current = [];
    setPreviews([]);
    setResults([]);
    setZip(null);
    setStamp("");
  }
  function setSettingsDocument(input, key) {
    try {
      const candidate = normalizeBakeSelection(input),
        before = settingsRef.current;
      if (JSON.stringify(candidate) === JSON.stringify(before)) return;
      const h = history.current,
        now = Date.now();
      if (!key || key !== h.key || now - h.time > 650) {
        h.past.push(before);
        if (h.past.length > 60) h.past.shift();
      }
      h.future = [];
      h.key = key;
      h.time = now;
      settingsRef.current = candidate;
      setSettings(candidate);
      setError("");
    } catch (e) {
      setError(e.message);
    }
  }
  function travel(direction) {
    const h = history.current,
      from = direction === "undo" ? h.past : h.future,
      to = direction === "undo" ? h.future : h.past;
    if (!from.length) return;
    to.push(settingsRef.current);
    const next = from.pop();
    settingsRef.current = next;
    setSettings(next);
    h.key = null;
    setRevision((n) => n + 1);
  }
  function recipe() {
    return {
      schema: BAKE_RECIPE_SCHEMA,
      settings,
      sources: {
        label: assets.label,
        low: assets.low.map((p) => p.name),
        high: assets.high.map((p) => p.name),
        embedded: false,
      },
    };
  }
  function saveRecipe() {
    downloadBlob(
      new Blob([JSON.stringify(recipe(), null, 2)], {
        type: "application/json",
      }),
      "Alloy-baking-recipe.json",
    );
    setInfo(
      "Recipe exported. Source meshes are not embedded; reimport them in another session.",
    );
  }
  async function loadRecipe(e) {
    const file = e.target.files[0];
    e.target.value = "";
    if (!file) return;
    try {
      if (file.size > 200000)
        throw new Error("Baking recipes must be smaller than 200 KB.");
      const input = JSON.parse(await file.text());
      const parsed = parseBakeRecipe(input);
      if (!mounted.current) return;
      setSettingsDocument(parsed.settings);
      setInfo(
        parsed.legacy
          ? "Legacy settings loaded. Node effects are not applied in map-only baking; select the equivalent map tiles. Source meshes were kept."
          : "Recipe opened. Reimport its source meshes if needed; current session meshes were not replaced.",
      );
    } catch (e) {
      if (mounted.current) setError(e.message);
    }
  }
  async function importAsset(e, role) {
    const file = e.target.files[0];
    e.target.value = "";
    if (!file || busy) return;
    const ticket = ++importEpoch.current;
    setImporting(role);
    setError("");
    try {
      const parts = await importBakeMesh(file, role);
      if (!mounted.current || ticket !== importEpoch.current) {
        disposeBakeParts(parts);
        return;
      }
      const used = new Set();
      parts.forEach((p) => {
        const base = p.name;
        let suffix = 1;
        while (used.has(p.name)) p.name = `${base}-${suffix++}`;
        used.add(p.name);
      });
      disposeBakeParts(assetsRef.current[role]);
      const next = {
        ...assetsRef.current,
        [role]: parts,
        label: file.name,
        source: "Imported static geometry",
      };
      assetsRef.current = next;
      setAssets(next);
      if (role === "low") {
        setSelectedTargets(parts.map((p) => p.name));
        setPreviewSet(parts[0].name);
      }
      clearResults();
      setInfo(
        `${parts.length} ${role} mesh object${parts.length === 1 ? "" : "s"} imported. Transforms are preserved; models are not independently rescaled.`,
      );
    } catch (e) {
      if (mounted.current && ticket === importEpoch.current)
        setError(e.message);
    } finally {
      if (mounted.current && ticket === importEpoch.current) setImporting("");
    }
  }
  function selectDemo(name) {
    if (busy) return;
    importEpoch.current++;
    setImporting("");
    const next = createDemoBakeAssets(name, { textureTarget });
    disposeBakeParts(assetsRef.current.low);
    disposeBakeParts(assetsRef.current.high);
    assetsRef.current = next;
    setAssets(next);
    setDemo(name);
    setSelectedTargets([name]);
    setPreviewSet(name);
    clearResults();
    setInfo(
      "Built-in low meshes use non-overlapping UVs. Imported low meshes require their own UV layouts.",
    );
  }
  async function bake() {
    setError("");
    setBusy(true);
    controller.current = new AbortController();
    const signal = controller.current.signal,
      bakedStamp = currentStamp;
    try {
      const raw = await startMeshBake(targets, assets.high, settings, null, {
        signal,
        onProgress: (p) => {
          if (mounted.current) setProgress(p);
        },
      });
      const packaged = await packageMeshBake(
        raw,
        null,
        {
          label: assets.label,
          low: targets.map((p) => ({ name: p.name, ...meshSummary([p]) })),
          high: assets.high.map((p) => ({ name: p.name, ...meshSummary([p]) })),
        },
        {
          signal,
          onProgress: (p) => {
            if (mounted.current) setProgress(p);
          },
        },
      );
      if (!mounted.current) {
        packaged.previews.forEach((p) => URL.revokeObjectURL(p.url));
        return;
      }
      clearResults();
      previewsRef.current = packaged.previews;
      setPreviews(packaged.previews);
      setResults(raw.map(({ maps, ...r }) => r));
      setZip(packaged.bytes);
      setStamp(bakedStamp);
      setPreviewSet(raw[0]?.name || "");
      setPreviewMap(settings.channels[0]);
      setView("maps");
      let supportInfo = "";
      if (targets.length === 1 && isTextureBakeTarget(targets[0])) {
        try {
          const support = await textureMeshMapsFromBake(raw[0]);
          if (support) {
            publishTextureMeshMaps(support);
            supportInfo =
              " AO / normal support attached to Texture's locked Layer 0 (AO multiplies indirect light; normals use reoriented blending).";
          }
        } catch (e) {
          // A portable-support budget/storage failure never discards the real
          // completed bake ZIP or replaces the user's previous Texture project.
          supportInfo = ` Texture mesh support was not attached: ${e.message}`;
        }
      }
      const misses = raw.reduce((n, r) => n + r.stats.misses, 0);
      setInfo(
        `${raw.length} texture set${raw.length === 1 ? "" : "s"} baked. ${misses.toLocaleString()} missed projections${misses ? " — check ray distance/alignment" : ""}. Download the ZIP when ready.${supportInfo}`,
      );
    } catch (e) {
      if (mounted.current)
        setError(
          e.name === "AbortError"
            ? "Bake cancelled. Previous results were kept."
            : e.message,
        );
    } finally {
      if (mounted.current) setBusy(false);
    }
  }
  const patch = (key, value) =>
    setSettingsDocument({ ...settingsRef.current, [key]: value }, key);
  const locked = busy || !!importing;
  return (
    <section
      className="bk-studio studio-themed"
      role="dialog"
      aria-modal="true"
      aria-label="Baking studio"
      data-mobile-panel={mobilePanel}
    >
      <StudioHeader
        workspace="baking"
        name={assets.label}
        extension=".bake"
        onWorkspace={onWorkspace}
        onClose={onClose}
        closeLabel="Close baking studio"
      >
        <button
          title="Open bake recipe"
          aria-label="Open bake recipe"
          onClick={() => recipeInput.current.click()}
          disabled={locked}
        >
          <FolderOpen size={14} />
        </button>
        <button onClick={saveRecipe} disabled={busy}>
          <FileJson size={13} />
          <span>Save recipe</span>
        </button>
      </StudioHeader>
      <input
        type="file"
        ref={lowInput}
        accept=".obj,.glb,.gltf,.ply"
        hidden
        aria-label="Low mesh file"
        onChange={(e) => importAsset(e, "low")}
      />
      <input
        type="file"
        ref={highInput}
        accept=".obj,.glb,.gltf,.ply,.stl"
        hidden
        aria-label="High mesh file"
        onChange={(e) => importAsset(e, "high")}
      />
      <input
        type="file"
        ref={recipeInput}
        accept=".json,application/json"
        hidden
        aria-label="Bake recipe file"
        onChange={loadRecipe}
      />
      <div
        className="bk-mobile-tabs"
        role="tablist"
        aria-label="Baking dock panels"
      >
        <button
          role="tab"
          aria-selected={mobilePanel === "assets"}
          onClick={() => setMobilePanel("assets")}
        >
          Meshes / maps
        </button>
        <button
          role="tab"
          aria-selected={mobilePanel === "inspector"}
          onClick={() => setMobilePanel("inspector")}
        >
          Inspector
        </button>
      </div>
      <main className="bk-layout">
        <aside className="bk-dock bk-assets" aria-label="Bake source meshes">
          <StudioDockTab icon={Layers3}>Bake project</StudioDockTab>
          <div className="bk-pane-heading">
            <h1>Baking</h1>
            <small>Map-only workflow</small>
          </div>
          <div className="bk-mode-switch">
            <button
              aria-pressed={mode === "mesh"}
              disabled={busy}
              onClick={() => setMode("mesh")}
            >
              Mesh maps
            </button>
            <button
              aria-pressed={mode === "material"}
              disabled={busy}
              onClick={() => {
                setMode("material");
                setView("mesh");
              }}
            >
              Material patch
            </button>
            <button
              aria-pressed={mode === "volume"}
              disabled={busy}
              onClick={() => {
                setMode("volume");
                setView("mesh");
              }}
            >
              Volume SDF
            </button>
          </div>
          <div className="bk-asset-scroll">
            <div className="bk-section-caption">
              SOURCE GEOMETRY{" "}
              <small>
                {importing ? `Importing ${importing}…` : assets.source}
              </small>
            </div>
            <section className="bk-card">
              <h2>
                <i />
                Low UV targets <small>{lowSummary.objects} objects</small>
              </h2>
              <div className="bk-mesh-stat">
                <Box size={22} />
                <div>
                  <strong>{lowSummary.triangles.toLocaleString()}</strong>
                  <small>
                    triangles · UV0 {lowSummary.uv ? "present" : "missing"}
                  </small>
                </div>
              </div>
              <button
                className="bk-import-button"
                disabled={locked}
                onClick={() => lowInput.current.click()}
              >
                <Upload size={13} /> Import low mesh
              </button>
              <div className="bk-target-list">
                {assets.low.map((p) => (
                  <label key={p.name}>
                    <input
                      aria-label={`Bake target ${p.name}`}
                      type="checkbox"
                      checked={selectedTargets.includes(p.name)}
                      disabled={locked}
                      onChange={(e) =>
                        setSelectedTargets((t) =>
                          e.target.checked
                            ? [...t, p.name]
                            : t.filter((n) => n !== p.name),
                        )
                      }
                    />
                    <span>{p.name}</span>
                    <small>
                      {(
                        (p.geometry.index?.count ||
                          p.geometry.attributes.position.count) / 3
                      ).toLocaleString()}
                    </small>
                  </label>
                ))}
              </div>
            </section>
            <section className="bk-card">
              <h2>
                <i className="warm" />
                High source <small>{highSummary.objects} objects</small>
              </h2>
              <div className="bk-mesh-stat">
                <Box size={22} />
                <div>
                  <strong>{highSummary.triangles.toLocaleString()}</strong>
                  <small>triangles · world transforms retained</small>
                </div>
              </div>
              <button
                className="bk-import-button"
                disabled={locked}
                onClick={() => highInput.current.click()}
              >
                <Upload size={13} /> Import high mesh
              </button>
              <p className="bk-note">
                OBJ · GLB / embedded glTF · PLY · high-only STL. No external
                asset requests.
              </p>
            </section>
            <label className="bk-field bk-demo-field">
              <span>Built-in study</span>
              <select
                aria-label="Bake demo mesh"
                value={demo}
                disabled={locked}
                onChange={(e) => selectDemo(e.target.value)}
              >
                {["Teapot", "Cube", "Sphere"].map((n) => (
                  <option key={n}>{n}</option>
                ))}
              </select>
            </label>
            {mode === "mesh" && (
              <MeshMapGrid
                channels={settings.channels}
                disabled={locked}
                previews={previews.filter(
                  (p) => p.set === (previewSet || previews[0]?.set),
                )}
                onChange={(channels) =>
                  setSettingsDocument({ ...settingsRef.current, channels })
                }
                onInspect={setInspectMap}
              />
            )}
          </div>
          <footer className="bk-pane-footer">
            <span>
              <i className="bk-status-dot" /> CPU BVH / WebGL preview
            </span>
            <small>{assets.low.length} texture sets</small>
          </footer>
        </aside>
        <section className="bk-dock bk-preview" aria-label="Baking viewport">
          <StudioDockTab icon={Box}>Viewport / maps</StudioDockTab>
          <div className="bk-preview-toolbar">
            <div>
              <strong>{selectedPart?.name || "Mesh study"}</strong>
              <small>
                {mode === "volume"
                  ? "High mesh · 3D distance volume"
                  : mode === "material"
                    ? "Procedural flat-patch export"
                    : stale
                      ? "Previous bake · settings changed"
                      : "Mesh-map authoring"}
              </small>
            </div>
            <div className="bk-view-tabs" role="group" aria-label="Baking view">
              <button
                aria-pressed={view === "mesh"}
                onClick={() => setView("mesh")}
              >
                3D preview
              </button>
              <button
                aria-pressed={view === "maps"}
                onClick={() => setView("maps")}
              >
                2D maps
              </button>
            </div>
          </div>
          {view === "maps" ? (
            <div className="bk-map-stage">
              <div className="bk-map-toolbar">
                <select
                  aria-label="Preview texture set"
                  value={activePreviewSet || ""}
                  onChange={(e) => setPreviewSet(e.target.value)}
                  disabled={!visiblePreviews.length}
                >
                  {[...new Set(visiblePreviews.map((p) => p.set))].map((n) => (
                    <option key={n}>{n}</option>
                  ))}
                </select>
                <select
                  aria-label="Preview mesh map"
                  value={image?.key || previewMap}
                  onChange={(e) => setPreviewMap(e.target.value)}
                  disabled={!visiblePreviews.length}
                >
                  {[...new Set(visiblePreviews.map((p) => p.key))]
                    .filter((k) =>
                      visiblePreviews.some(
                        (p) => p.set === activePreviewSet && p.key === k,
                      ),
                    )
                    .map((k) => (
                      <option key={k} value={k}>
                        {mapMeta(k)?.label || k}
                      </option>
                    ))}
                </select>
                {image && (
                  <button
                    onClick={() => {
                      const a = document.createElement("a");
                      a.href = image.url;
                      a.download = `${image.key}.png`;
                      a.click();
                    }}
                  >
                    <Download size={12} /> PNG
                  </button>
                )}
              </div>
              {image ? (
                <div className="bk-map-image">
                  <img
                    src={image.url}
                    alt={`${mapMeta(image.key)?.label || image.key} mesh map`}
                  />
                  <span>
                    {image.resolution} × {image.resolution} ·{" "}
                    {mapMeta(image.key)?.colorSpace || "linear"} ·{" "}
                    {stale ? "previous recipe" : "current bake"}
                  </span>
                </div>
              ) : (
                <div className="bk-empty">
                  <Layers3 size={30} />
                  <h2>No baked maps yet</h2>
                  <p>
                    Choose your low UV targets and high source, then bake the
                    selected maps. Material-patch mode downloads its own six-map
                    ZIP.
                  </p>
                </div>
              )}
            </div>
          ) : (
            <>
              <div className="bk-mesh-toolbar">
                <button
                  aria-label="Frame bake mesh"
                  onClick={() => setReset((n) => n + 1)}
                >
                  <Maximize2 size={13} /> Frame
                </button>
                <button
                  aria-pressed={overlay}
                  onClick={() => setOverlay(!overlay)}
                >
                  High overlay
                </button>
                <button
                  aria-pressed={wireframe}
                  onClick={() => setWireframe(!wireframe)}
                >
                  Wireframe
                </button>
                <span>Orbit · scroll to zoom</span>
              </div>
              <div className="bk-scene">
                {mode === "material" ? (
                  <Viewport
                    params={params}
                    shape={currentShape || "Teapot"}
                    environment="Studio softbox"
                    rotate={false}
                    wireframe={wireframe}
                    resetToken={reset}
                    studioLayout
                  />
                ) : (
                  <MeshBakeViewport
                    geometry={
                      mode === "volume"
                        ? assets.high[0]?.geometry
                        : selectedPart?.geometry
                    }
                    highGeometry={assets.high[0]?.geometry}
                    label={selectedPart?.name}
                    normalURL={
                      mode === "volume" ? undefined : normalPreview?.url
                    }
                    normalY={results[0]?.settings.normalY || settings.normalY}
                    overlay={overlay}
                    wireframe={wireframe}
                    resetToken={reset}
                  />
                )}
              </div>
            </>
          )}
          <div className="bk-run-panel">
            {progress && (
              <div className="bk-progress" role="status">
                <span>
                  {progress.name ? `${progress.name} · ` : ""}
                  {progress.phase}
                  {progress.set
                    ? ` · set ${progress.set}/${progress.sets}`
                    : ""}
                </span>
                <progress
                  aria-label="Mesh bake progress"
                  max={progress.total || 1}
                  value={progress.done || 0}
                />
                {progress.hits !== undefined && (
                  <small>
                    {progress.hits.toLocaleString()} hits ·{" "}
                    {progress.misses.toLocaleString()} misses
                  </small>
                )}
              </div>
            )}
            <div className="bk-run-actions">
              {mode === "mesh" && (
                <button
                  className="bk-primary"
                  disabled={
                    locked || !targets.length || !settings.channels.length
                  }
                  onClick={bake}
                >
                  {busy ? (
                    <LoaderCircle className="spin" size={15} />
                  ) : (
                    <Layers3 size={15} />
                  )}{" "}
                  {busy
                    ? "Baking mesh maps…"
                    : `Bake ${targets.length > 1 ? "all selected meshes" : "mesh maps"}`}
                </button>
              )}
              {busy && (
                <button onClick={() => controller.current?.abort()}>
                  <Square size={12} /> Cancel bake
                </button>
              )}
              {zip && mode === "mesh" && (
                <button
                  onClick={() =>
                    downloadBlob(
                      new Blob([zip], { type: "application/zip" }),
                      "Alloy-mesh-maps.zip",
                    )
                  }
                >
                  <Download size={14} /> Download maps ZIP
                </button>
              )}
              <div className="bk-settings-history">
                <button
                  aria-label="Undo bake settings"
                  disabled={locked || !history.current.past.length}
                  onClick={() => travel("undo")}
                >
                  <Undo2 size={13} />
                </button>
                <button
                  aria-label="Redo bake settings"
                  disabled={locked || !history.current.future.length}
                  onClick={() => travel("redo")}
                >
                  <Redo2 size={13} />
                </button>
              </div>
            </div>
            {results.length > 0 && (
              <div className="bk-result-summary">
                {results.map((r) => (
                  <span key={r.name}>
                    <Check size={11} />
                    {r.name} · {r.stats.charts} charts ·{" "}
                    {r.stats.hits.toLocaleString()} hits ·{" "}
                    {r.stats.misses.toLocaleString()} misses ·{" "}
                    {r.stats.nearestFallbacks || 0} nearest fallbacks
                  </span>
                ))}
              </div>
            )}
          </div>
        </section>
        <aside className="bk-dock bk-inspector" aria-label="Baking inspector">
          <StudioDockTab icon={SlidersHorizontal}>Inspector</StudioDockTab>
          <div className="bk-inspector-scroll">
            {mode === "material" ? (
              <div className="bk-material-patch">
                <BakePanel
                  params={params}
                  onNotify={setInfo}
                  onPreviews={(list, key) =>
                    receivePreviews(list, key, "material")
                  }
                />
              </div>
            ) : mode === "volume" ? (
              <VolumeSdfPanel
                parts={assets.high}
                onBusy={(value, ctrl) => {
                  setBusy(value);
                  if (ctrl) controller.current = ctrl;
                  if (!value) setProgress(null);
                }}
                onProgress={setProgress}
                onPreview={(list, key) => receivePreviews(list, key, "volume")}
              />
            ) : (
              <>
                <div className="bk-inspector-identity">
                  <div>
                    <Box size={24} />
                  </div>
                  <section>
                    <small>STATIC HIGH → LOW</small>
                    <h1>Mesh map bake</h1>
                    <p>
                      {targets.length} selected texture set
                      {targets.length === 1 ? "" : "s"}
                    </p>
                  </section>
                </div>
                <div className="bk-section-caption">
                  OUTPUT <small>8-bit PNG + manifest</small>
                </div>
                <section className="bk-card">
                  <h2>
                    <i />
                    Texture settings
                  </h2>
                  <label className="bk-field">
                    <span>Resolution</span>
                    <select
                      aria-label="Mesh bake resolution"
                      disabled={locked}
                      value={settings.resolution}
                      onChange={(e) =>
                        patch("resolution", Number(e.target.value))
                      }
                    >
                      {[64, 128, 256, 512, 1024].map((n) => (
                        <option key={n} value={n}>
                          {n} × {n}
                        </option>
                      ))}
                    </select>
                  </label>
                  <label className="bk-field">
                    <span>Padding</span>
                    <input
                      aria-label="Mesh bake padding"
                      type="number"
                      min={0}
                      max={32}
                      value={settings.padding}
                      disabled={locked}
                      onChange={(e) => patch("padding", Number(e.target.value))}
                    />
                  </label>
                  <label className="bk-field">
                    <span>Normal format</span>
                    <select
                      aria-label="Mesh normal convention"
                      disabled={locked}
                      value={settings.normalY}
                      onChange={(e) => patch("normalY", e.target.value)}
                    >
                      <option value="+Y">OpenGL +Y</option>
                      <option value="-Y">DirectX −Y</option>
                    </select>
                  </label>
                </section>
                <section className="bk-card bk-selected-map">
                  <h2>
                    <i className="cool" />
                    Map inspector
                  </h2>
                  <strong>{selectedMap.label}</strong>
                  <p>{selectedMap.description}</p>
                  <small>
                    {settings.channels.includes(inspectMap)
                      ? "Enabled · will be baked"
                      : "Disabled · click its tile on the left to enable"}
                  </small>
                  {["bevel-normal", "bevel-mask"].includes(inspectMap) && (
                    <>
                      <label className="bk-field">
                        <span>Bevel radius</span>
                        <input
                          aria-label="Bevel radius"
                          type="number"
                          min="0"
                          max="10"
                          step="0.005"
                          value={settings.bevelRadius}
                          disabled={locked}
                          onChange={(e) =>
                            patch("bevelRadius", Number(e.target.value))
                          }
                        />
                      </label>
                      <label className="bk-field">
                        <span>Bevel strength</span>
                        <input
                          aria-label="Bevel strength"
                          type="number"
                          min="0"
                          max="1"
                          step="0.1"
                          value={settings.bevelStrength}
                          disabled={locked}
                          onChange={(e) =>
                            patch("bevelStrength", Number(e.target.value))
                          }
                        />
                      </label>
                      <label className="bk-field">
                        <span>Bevel samples</span>
                        <select
                          aria-label="Bevel samples"
                          value={settings.bevelSamples}
                          disabled={locked}
                          onChange={(e) =>
                            patch("bevelSamples", Number(e.target.value))
                          }
                        >
                          {[4, 8, 12, 16].map((n) => (
                            <option key={n}>{n}</option>
                          ))}
                        </select>
                      </label>
                    </>
                  )}
                  {inspectMap === "dust" && (
                    <>
                      <label className="bk-field">
                        <span>Dust up axis</span>
                        <select
                          aria-label="Dust up axis"
                          value={settings.dustAxis}
                          disabled={locked}
                          onChange={(e) => patch("dustAxis", e.target.value)}
                        >
                          {["+X", "-X", "+Y", "-Y", "+Z", "-Z"].map((n) => (
                            <option key={n}>{n}</option>
                          ))}
                        </select>
                      </label>
                      <label className="bk-field">
                        <span>Slope falloff</span>
                        <input
                          aria-label="Dust slope falloff"
                          type="number"
                          min="0.1"
                          max="16"
                          step="0.1"
                          value={settings.dustFalloff}
                          disabled={locked}
                          onChange={(e) =>
                            patch("dustFalloff", Number(e.target.value))
                          }
                        />
                      </label>
                    </>
                  )}
                  {inspectMap === "wireframe" && (
                    <label className="bk-field">
                      <span>Line width (px)</span>
                      <input
                        aria-label="UV wireframe width"
                        type="number"
                        min="0.25"
                        max="8"
                        step="0.25"
                        value={settings.wireWidth}
                        disabled={locked}
                        onChange={(e) =>
                          patch("wireWidth", Number(e.target.value))
                        }
                      />
                    </label>
                  )}
                  {["convexity", "cavity", "dirt", "edge-wear"].includes(
                    inspectMap,
                  ) && (
                    <label className="bk-field">
                      <span>Mask strength</span>
                      <input
                        aria-label="Mesh mask strength"
                        type="number"
                        min="0"
                        max="16"
                        step="0.5"
                        value={settings.maskStrength}
                        disabled={locked}
                        onChange={(e) =>
                          patch("maskStrength", Number(e.target.value))
                        }
                      />
                    </label>
                  )}
                </section>
                <section className="bk-card">
                  <h2>
                    <i className="warm" />
                    High-to-low projection
                  </h2>
                  <label className="bk-field">
                    <span>Method</span>
                    <select
                      aria-label="Mesh projection method"
                      value={settings.projection}
                      disabled={locked}
                      onChange={(e) => patch("projection", e.target.value)}
                    >
                      <option value="ray">Normal ray</option>
                      <option value="nearest">Nearest surface</option>
                    </select>
                  </label>
                  {[
                    ["front", "Front distance"],
                    ["back", "Back distance"],
                    ["aoDistance", "AO distance"],
                    ["secondaryAoDistance", "Secondary AO distance"],
                    ["thicknessRange", "Thickness range"],
                    ["curvatureRadius", "Curvature radius"],
                  ].map(([key, label]) => (
                    <label className="bk-field" key={key}>
                      <span>{label}</span>
                      <input
                        type="number"
                        aria-label={label}
                        min={0.0001}
                        step={0.01}
                        value={settings[key]}
                        disabled={locked}
                        onChange={(e) => patch(key, Number(e.target.value))}
                      />
                    </label>
                  ))}
                  <label className="bk-field">
                    <span>AO samples</span>
                    <select
                      aria-label="AO samples"
                      value={settings.samples}
                      disabled={locked}
                      onChange={(e) => patch("samples", Number(e.target.value))}
                    >
                      {[4, 8, 16, 32, 64].map((n) => (
                        <option key={n}>{n}</option>
                      ))}
                    </select>
                  </label>
                  <label className="bk-check-field">
                    <input
                      aria-label="Match high meshes by name"
                      type="checkbox"
                      checked={settings.matchByName}
                      disabled={locked}
                      onChange={(e) => patch("matchByName", e.target.checked)}
                    />
                    <span>Match objects by name</span>
                  </label>
                  <p className="bk-note">
                    Distances use source scene units. Ray front/back limits act
                    as a uniform projection envelope, not a custom cage mesh.
                  </p>
                </section>
                <section className="bk-card bk-notes-card">
                  <h2>
                    <i />
                    Bake contract
                  </h2>
                  <p>
                    Each low object is its own UV texture set. Imported UVs must
                    be non-overlapping in 0–1. Misses, chart count and coverage
                    are reported rather than hidden.
                  </p>
                  <p>
                    Bevel softens normals only; curvature is an estimate. No
                    full Substance/Blender parity, MikkTSpace certification,
                    UDIM, animated geometry or material-texture transfer is
                    claimed.
                  </p>
                </section>
              </>
            )}
          </div>
          <footer className="bk-pane-footer">
            <span>Settings + selected maps</span>
            <small>Source meshes: session only</small>
          </footer>
        </aside>
      </main>
      <footer className="bk-status-bar">
        <span>
          <i className="bk-status-dot" />
          {info}
        </span>
        <small>BOUNDED STATIC MESH BAKER</small>
      </footer>
      {error && (
        <div className="bk-error" role="alert">
          <span>{error}</span>
          <button
            aria-label="Dismiss baking error"
            onClick={() => setError("")}
          >
            <X size={13} />
          </button>
        </div>
      )}
    </section>
  );
}
