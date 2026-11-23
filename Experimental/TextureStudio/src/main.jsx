import { NormalizeMaterialAuthoring } from "./MaterialSpecification.js";
import StudioTooltips from "./StudioTooltips.jsx";
import {
  MATERIAL_PREVIEW_SHAPES,
  MATERIAL_ENVIRONMENTS,
} from "./materialViewportOptions.js";
import {
  resolvePatternBase,
  composePatternMaterial,
  patternPreviewShape,
} from "./patternSurface.js";
import PatternEditor from "./PatternEditor.jsx";
import StampStudio from "./StampStudio.jsx";
import TexturePaintStudio from "./TexturePaintStudio.jsx";
import BakingStudio from "./BakingStudio.jsx";
import {
  StudioHeader,
  StudioDockTab,
  StudioErrorBoundary,
  normalizeStudioWorkspace,
} from "./StudioUI.jsx";
import React, { useState, useEffect, useRef } from "react";
import { createRoot } from "react-dom/client";
import {
  Search,
  Plus,
  ChevronDown,
  ChevronRight,
  ArrowUpRight,
  ArrowDownToLine,
  SlidersHorizontal,
  Layers3,
  Box,
  X,
  Check,
  RotateCcw,
  Maximize,
  Minimize,
  Sun,
  MoveUpRight,
  CircleHelp,
  BookOpen,
  Sparkles,
  Circle,
  Grid2X2,
  Star,
  Copy,
  CheckCheck,
  Command,
  MousePointer2,
  PanelLeft,
  Scan,
  ZoomIn,
  ZoomOut,
  ExternalLink,
  Leaf,
  ArrowRight,
  Settings2,
  MoreHorizontal,
  Palette,
  Move3D,
  Camera,
  Code2,
  FileJson,
} from "lucide-react";
import Viewport, { renderThumbnails } from "./Viewport";
import { materials, normalizeMaterial } from "./materials";
import MaterialInspectorPanel from "./MaterialInspectorPanel.jsx";
import { exportMaterialJSON, exportMaterialShader } from "./materialExport.js";
import BakePanel from "./BakePanel";
import "@fontsource/dm-sans/latin-300.css";
import "@fontsource/dm-sans/latin-400.css";
import "@fontsource/dm-sans/latin-500.css";
import "@fontsource/dm-sans/latin-600.css";
import "@fontsource/dm-sans/latin-700.css";
import "@fontsource/space-grotesk/latin-400.css";
import "@fontsource/space-grotesk/latin-500.css";
import "@fontsource/space-grotesk/latin-600.css";
import "./styles.css";
import "./studioTheme.css";
import "./artworkAuthoring.css";

const categories = [
  "All materials",
  "Paint",
  "Metal",
  "Ceramic",
  "Fabric",
  "Rubber",
  "Glass",
  "Plastic",
  "Leather",
  "Clay",
  "Wax",
  "Skin",
  "Paper",
  "Technical",
  "Stone",
  "Wall",
  "Nature",
];

// A validated preset ID makes material studies shareable without importing code.
const initialMaterial =
  materials.find(
    (m) =>
      m.id ===
      (new URLSearchParams(window.location.search).get("material") ===
      "corrugated-tin"
        ? "corrugated-zinc"
        : new URLSearchParams(window.location.search).get("material")),
  ) || materials[0];
function initialPreview(p) {
  if (p.previewShape === "Panel" || p.type === 37 || p.type === 38)
    return "Panel";
  if (p.category === "Fabric" && p.type !== 7) return "Draped cloth";
  if (p.type === 31) return "Leaf";
  if (p.type === 32) return "Grass blade";
  if (p.type === 33) return "Petal";
  if (p.type === 34) return "Sphere";
  if (p.type === 35) return "Cactus";
  if (p.type === 36) return "Stem";
  if (p.type === 30 || p.type === 11) return "Leather swatch";
  if (p.type === 21) return "Pipe";
  if (p.type === 27 || p.type === 28) return "Foliage card";
  if ([15, 17, 20, 22, 23, 24, 25, 26, 29].includes(p.type)) return "Panel";
  if (p.type === 18) return "Sphere";
  if (p.id === "brake-disc") return "Brake rotor";
  return "Shader ball";
}

function App() {
  const [selected, setSelected] = useState(initialMaterial);
  const [params, setParams] = useState(() => {
    try { const Saved = JSON.parse(localStorage.getItem(`alloy-authoring-v2:${initialMaterial.id}`) || "null"); return Saved?.id === initialMaterial.id && Saved?.Authoring?.Version === 2 ? { ...Saved, Authoring: NormalizeMaterialAuthoring(Saved.Authoring) } : { ...initialMaterial }; }
    catch { return { ...initialMaterial }; }
  });
  useEffect(() => { if (params.Authoring) { try { localStorage.setItem(`alloy-authoring-v2:${params.id}`, JSON.stringify(params)); } catch {} } }, [params]);
  const [category, setCategory] = useState("All materials");
  const [search, setSearch] = useState("");
  const [thumbs, setThumbs] = useState({});
  const [shape, setShape] = useState(initialPreview(initialMaterial));
  const [environment, setEnvironment] = useState("Studio softbox");
  const [rotate, setRotate] = useState(false);
  const [wireframe, setWireframe] = useState(false);
  const [resetToken, setResetToken] = useState(0);
  const [zoom, setZoom] = useState(null);
  const [zoomLevel, setZoomLevel] = useState(100);
  const [tab, setTab] = useState("Layers");
  const [workspace, setWorkspace] = useState(() =>
    normalizeStudioWorkspace(
      new URLSearchParams(window.location.search).get("studio"),
    ),
  );
  const [modal, setModal] = useState(null);
  const [stampEdit, setStampEdit] = useState(null);
  const [stampRequest, setStampRequest] = useState(null);
  const [bakeMode, setBakeMode] = useState("mesh");
  const [textureBakeTarget, setTextureBakeTarget] = useState(false);
  const [workspaceEpoch, setWorkspaceEpoch] = useState(0);
  function openWorkspace(value, mode = "mesh") {
    const next = normalizeStudioWorkspace(value);
    if (next !== "stamp" && workspace === "stamp") setStampEdit(null);
    setModal(null);
    setWorkspace(next);
    if (next === "baking") {
      setBakeMode(mode);
      setTextureBakeTarget(workspace === "texture");
    }
    try {
      const url = new URL(window.location.href);
      if (next === "material") url.searchParams.delete("studio");
      else url.searchParams.set("studio", next);
      window.history.replaceState(null, "", url);
    } catch {
      /* Workspace routing remains usable on restricted/file origins. */
    }
  }
  useEffect(() => {
    const change = () => {
      setWorkspace(
        normalizeStudioWorkspace(
          new URLSearchParams(window.location.search).get("studio"),
        ),
      );
      setModal(null);
    };
    window.addEventListener("popstate", change);
    return () => window.removeEventListener("popstate", change);
  }, []);
  const [toast, setToast] = useState("");
  const [full, setFull] = useState(false);
  const [mobileLibrary, setMobileLibrary] = useState(false);
  const [customName, setCustomName] = useState("");
  const [saved, setSaved] = useState(() => {
    try {
      const list = JSON.parse(localStorage.getItem("alloy-saved") || "[]");
      return Array.isArray(list) ? list.map(normalizeMaterial) : [];
    } catch {
      return [];
    }
  });
  const [favorites, setFavorites] = useState(() => {
    try {
      return JSON.parse(localStorage.getItem("alloy-favorites") || "[]");
    } catch {
      return [];
    }
  });
  const [onlySaved, setOnlySaved] = useState(false);
  const [ready, setReady] = useState(false);
  const [preparation, setPreparation] = useState({
    done: 0,
    total: materials.length,
    phase: "starting",
    name: "Library",
  });
  const [compileStatus, setCompileStatus] = useState({
    phase: "compiling",
    name: "Racing Green",
  });
  const thumbnailRef = useRef(thumbs);
  thumbnailRef.current = thumbs;
  const searchRef = useRef(null);
  const viewerRef = useRef(null);
  const notify = (text) => setToast(text);
  const authoringOpen = workspace !== "material";
  useEffect(() => {
    // Do not compete with vector input or the live pattern preview for GPU work.
    if (authoringOpen) return;
    const controller = new AbortController();
    const timer = setTimeout(() => {
      renderThumbnails(
        setThumbs,
        setPreparation,
        controller.signal,
        thumbnailRef.current,
      ).catch((error) => {
        if (!controller.signal.aborted) {
          setPreparation((p) => ({ ...p, phase: "error" }));
          setToast(
            "Library previews could not finish. Materials remain selectable.",
          );
          console.error(error);
        }
      });
    }, 180);
    return () => {
      clearTimeout(timer);
      controller.abort();
    };
  }, [authoringOpen]);
  useEffect(() => {
    if (toast) {
      const t = setTimeout(() => setToast(""), 3200);
      return () => clearTimeout(t);
    }
  }, [toast]);
  useEffect(() => {
    try {
      localStorage.setItem("alloy-saved", JSON.stringify(saved));
    } catch {
      setToast(
        "Browser storage is unavailable. Export your preset to keep it.",
      );
    }
  }, [saved]);
  useEffect(() => {
    try {
      localStorage.setItem("alloy-favorites", JSON.stringify(favorites));
    } catch {}
  }, [favorites]);
  useEffect(() => {
    function key(e) {
      if (
        e.target.closest?.(".pe-overlay, .tp-studio, .bk-studio") ||
        document.querySelector(".tp-studio, .bk-studio, .pe-overlay")
      )
        return;
      if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === "s") {
        e.preventDefault();
        setModal("save");
        return;
      }
      if (e.key === "?") {
        setModal("shortcuts");
        return;
      }
      if (["INPUT", "TEXTAREA", "SELECT"].includes(e.target.tagName)) return;
      if (e.key === "/") {
        e.preventDefault();
        searchRef.current?.focus();
      }
      if (e.key === "Escape") {
        setModal(null);
        setFull(false);
      }
      if (e.key.toLowerCase() === "r") setResetToken((n) => n + 1);
      if (e.key === " ") {
        e.preventDefault();
        setRotate((r) => !r);
      }
      if (e.key.toLowerCase() === "f") setFull((f) => !f);
    }
    window.addEventListener("keydown", key);
    return () => window.removeEventListener("keydown", key);
  }, []);
  const update = (key, value) => setParams((p) => ({ ...p, [key]: value }));
  function selectMaterial(mat) {
    setSelected(mat);
    try {
      const Saved = JSON.parse(localStorage.getItem(`alloy-authoring-v2:${mat.id}`) || "null");
      setParams(Saved?.id === mat.id && Saved.Authoring?.Version === 2 ? { ...Saved, Authoring: NormalizeMaterialAuthoring(Saved.Authoring) } : { ...mat, colors: [...mat.colors] });
    } catch { setParams({ ...mat, colors: [...mat.colors] }); }
    setMobileLibrary(false);
    if (mat.category === "Fabric" && mat.type !== 7 && shape !== "Draped cloth")
      setShape("Draped cloth");
    else if (mat.type === 11) setShape("Leather swatch");
    else if (mat.type >= 30 && mat.type <= 36) setShape(initialPreview(mat));
    else if (mat.type === 21) setShape(initialPreview(mat));
    else if (mat.type === 37 || mat.type === 38) setShape("Panel");
    else if (mat.type === 27 || mat.type === 28) setShape("Foliage card");
    else if (mat.type >= 22 && mat.type <= 29) setShape("Panel");
    else if (mat.type === 18) setShape("Sphere");
    else if ([15, 17, 20].includes(mat.type)) setShape("Panel");
    else if (mat.id === "brake-disc") setShape("Brake rotor");
    else if (shape === "Brake rotor") setShape("Shader ball");
  }
  const allMaterials = [...materials, ...saved];
  const filtered = allMaterials.filter(
    (m) =>
      (!onlySaved ||
        saved.some((s) => s.id === m.id) ||
        favorites.includes(m.id)) &&
      (category === "All materials" || m.category === category) &&
      (m.name + " " + m.category + " " + m.label)
        .toLowerCase()
        .includes(search.toLowerCase()),
  );
  const changed = JSON.stringify(params) !== JSON.stringify(selected);
  const layerCount = params.Authoring?.Layers.length || (params.type === 0 ? 3 : params.coat > 0 ? 2 : 1);
  const toggleFavorite = (id) =>
    setFavorites((f) =>
      f.includes(id) ? f.filter((v) => v !== id) : [...f, id],
    );
  function saveMaterialPreset(name) {
    if (!name.trim()) return null;
    const preset = normalizeMaterial({
      ...params,
      id: "custom-" + Date.now(),
      name: name.trim().slice(0, 80),
    });
    setSaved((list) => [...list, preset]);
    setSelected(preset);
    setParams(preset);
    return preset;
  }
  function savePreset(e) {
    e.preventDefault();
    const name = customName.trim();
    if (!name) return;
    saveMaterialPreset(name);
    setCustomName("");
    setModal(null);
    notify("Your material has been saved to the library.");
  }
  function download(data, name, type) {
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([data], { type }));
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
    notify("Material exported. Ready for your next project.");
    setModal(null);
  }
  function screenshot() {
    const canvas = viewerRef.current?.querySelector("canvas");
    if (!canvas) {
      notify("The viewport is still loading.");
      return;
    }
    const a = document.createElement("a");
    a.href = canvas.toDataURL("image/png");
    a.download = params.id + "-preview.png";
    a.click();
    setModal(null);
    notify("Preview image exported.");
  }
  return (
    <div
      className={
        "app studio-themed studio-material " + (full ? "is-focused" : "")
      }
    >
      {authoringOpen && (
        <StudioErrorBoundary
          key={workspace + workspaceEpoch}
          name={workspace}
          onReturn={() => openWorkspace("material")}
          onRetry={() => setWorkspaceEpoch((n) => n + 1)}
        >
          {workspace === "pattern" && (
            <PatternEditor
              initial={params.pattern}
              currentMaterial={params}
              currentShape={shape}
              onClose={() => openWorkspace("material")}
              onWorkspace={openWorkspace}
              onApply={(pattern, base) => {
                const target = resolvePatternBase(params, base);
                setParams(composePatternMaterial(target, pattern));
                if (base !== "current") {
                  setSelected(target);
                  setShape(patternPreviewShape({ ...target, pattern }));
                }
                openWorkspace("material");
                notify(
                  "Pattern applied. Save as a preset to keep this material.",
                );
              }}
            />
          )}
          {workspace === "texture" && (
            <TexturePaintStudio
              initialStamp={stampRequest}
              onStampConsumed={() => setStampRequest(null)}
              onEditStamp={(project) => {
                setStampEdit(project);
                openWorkspace("stamp");
              }}
              onClose={() => openWorkspace("material")}
              onWorkspace={openWorkspace}
              materialWorkspace={{
                params,
                selected,
                setParams,
                update,
                selectMaterial,
                materials: allMaterials,
                thumbs,
                setThumbs,
                favorites,
                toggleFavorite,
                shape,
                setShape,
                environment,
                setEnvironment,
                rotate,
                setRotate,
                wireframe,
                setWireframe,
                resetToken,
                setResetToken,
                zoom,
                setZoom,
                setZoomLevel,
                layerCount,
                tab,
                setTab,
                savePreset: saveMaterialPreset,
                openWorkspace,
              }}
            />
          )}
          {workspace === "stamp" && (
            <StampStudio
              initial={stampEdit}
              onWorkspace={openWorkspace}
              onClose={() => openWorkspace("texture")}
              onUse={(preset) => {
                setStampRequest(preset);
                openWorkspace("texture");
              }}
            />
          )}
          {workspace === "baking" && (
            <BakingStudio
              params={params}
              currentShape={shape}
              initialMode={bakeMode}
              textureTarget={textureBakeTarget}
              onClose={() => openWorkspace("material")}
              onWorkspace={openWorkspace}
            />
          )}
        </StudioErrorBoundary>
      )}
      <div
        style={{ display: "contents" }}
        inert={authoringOpen ? true : undefined}
        aria-hidden={authoringOpen ? true : undefined}
      >
        <StudioHeader
          workspace="material"
          name={params.name}
          extension=".material"
          onWorkspace={openWorkspace}
          legacyNames
        >
          <button
            className="studio-header-guide"
            title="Quick guide"
            aria-label="Quick guide"
            onClick={() => setModal("guide")}
          >
            <BookOpen size={14} />
          </button>
          <button
            title="Studio settings"
            aria-label="Studio settings"
            onClick={() => setModal("settings")}
          >
            <Settings2 size={15} />
          </button>
        </StudioHeader>

        <div className="project-bar">
          <div className="breadcrumb">
            <button
              className="icon-button mobile-toggle"
              aria-label="Toggle material library"
              onClick={() => setMobileLibrary(!mobileLibrary)}
            >
              <PanelLeft size={18} />
            </button>
            <Box size={15} />
            <span>Automotive essentials</span>
            <ChevronRight size={13} />
            <strong>Material explorer</strong>
            <span className="version-badge">v7.10</span>
          </div>
          <div className="project-actions">
            <span className="saved-state">
              <span className="status-dot" />
              {changed ? "Unsaved preset changes" : "Workspace up to date"}
            </span>
            <button
              className="export-button"
              onClick={() => setModal("export")}
            >
              <ArrowUpRight size={15} /> Export material
            </button>
          </div>
        </div>
        <main className="workspace">
          <aside
            className={"library-panel " + (mobileLibrary ? "mobile-open" : "")}
          >
            <StudioDockTab icon={Layers3}>Material library</StudioDockTab>
            <div className="material-collection-tabs">
              <button
                aria-pressed={!onlySaved}
                onClick={() => setOnlySaved(false)}
              >
                Workspace
              </button>
              <button
                aria-pressed={onlySaved}
                onClick={() => {
                  setOnlySaved(true);
                  setCategory("All materials");
                  setSearch("");
                }}
              >
                My collection{" "}
                <span>
                  {saved.length +
                    favorites.filter((id) => !saved.some((p) => p.id === id))
                      .length}
                </span>
              </button>
            </div>
            <div className="panel-title">
              <h2>
                {onlySaved ? "My collection" : "Material library"}{" "}
                <span>{onlySaved ? filtered.length : allMaterials.length}</span>
              </h2>
              <button
                className="icon-button add-button"
                title="Save a new material preset"
                onClick={() => {
                  setCustomName(params.name + " Custom");
                  setModal("save");
                }}
              >
                <Plus size={16} />
              </button>
            </div>
            <label className="search-box">
              <Search size={15} />
              <input
                ref={searchRef}
                placeholder="Search materials..."
                value={search}
                onChange={(e) => setSearch(e.target.value)}
              />
              {search ? (
                <button onClick={() => setSearch("")} aria-label="Clear search">
                  <X size={13} />
                </button>
              ) : (
                <kbd>/</kbd>
              )}
            </label>
            <div className="category-list">
              {categories.map((c) => (
                <button
                  key={c}
                  className={category === c ? "selected" : ""}
                  onClick={() => setCategory(c)}
                >
                  {c}
                </button>
              ))}
            </div>
            <div className="library-section-label">
              <span>
                {onlySaved
                  ? "YOUR MATERIALS"
                  : category === "All materials"
                    ? "THE ESSENTIAL COLLECTION"
                    : category.toUpperCase() + " MATERIALS"}
              </span>
              <span>{String(filtered.length).padStart(2, "0")}</span>
            </div>
            <div className="material-scroll">
              <div className="material-grid">
                {filtered.map((m) => (
                  <div
                    key={m.id}
                    className={
                      "material-card " +
                      (selected.id === m.id ? "is-selected" : "")
                    }
                  >
                    <button
                      className="material-select"
                      onClick={() => selectMaterial(m)}
                      aria-label={"Apply " + m.name}
                    >
                      <div
                        className={"material-preview material-type-" + m.type}
                      >
                        {thumbs[m.id] ? (
                          <img src={thumbs[m.id]} alt="" />
                        ) : (
                          <div
                            className="fallback-sphere"
                            style={{ "--sphere-color": m.color }}
                          />
                        )}
                        <span className="preview-shadow" />
                        {selected.id === m.id && (
                          <span className="selected-check">
                            <Check size={10} strokeWidth={3} />
                          </span>
                        )}
                      </div>
                      <span className="material-name">{m.name}</span>
                      <span className="material-category">
                        {m.category === "Paint"
                          ? "Automotive paint"
                          : m.category}
                      </span>
                    </button>
                    <button
                      className={
                        "favorite-button " +
                        (favorites.includes(m.id) ? "is-favorite" : "")
                      }
                      aria-label={
                        (favorites.includes(m.id) ? "Unsave " : "Save ") +
                        m.name
                      }
                      onClick={() => toggleFavorite(m.id)}
                    >
                      <Star
                        size={12}
                        fill={
                          favorites.includes(m.id) ? "currentColor" : "none"
                        }
                      />
                    </button>
                  </div>
                ))}
              </div>
              {!filtered.length && (
                <div className="empty-state">
                  <Layers3 size={28} />
                  <h3>
                    {onlySaved ? "Make it your own" : "No materials found"}
                  </h3>
                  <p>
                    {onlySaved
                      ? "Star a material or save a custom preset to build your collection."
                      : "Try another search or material category."}
                  </p>
                  <button
                    onClick={() => {
                      setOnlySaved(false);
                      setSearch("");
                      setCategory("All materials");
                    }}
                  >
                    Explore all materials <ArrowRight size={14} />
                  </button>
                </div>
              )}
            </div>
            <div className="library-footer">
              <span className="procedural-icon">
                <Sparkles size={16} />
              </span>
              <div>
                <strong>Beautiful by calculation.</strong>
                <p>{materials.length} presets. Vector + procedural surfaces.</p>
              </div>
              <CircleHelp size={14} onClick={() => setModal("guide")} />
            </div>
          </aside>
          <section className="viewport-panel" ref={viewerRef}>
            <StudioDockTab icon={Box}>
              Viewport <small>{shape}</small>
            </StudioDockTab>
            {!authoringOpen && (
              <Viewport
                params={params}
                paused={authoringOpen}
                shape={shape}
                environment={environment}
                rotate={!authoringOpen && rotate}
                wireframe={wireframe}
                resetToken={resetToken}
                zoom={zoom}
                onReady={() => setReady(true)}
                onZoomChange={setZoomLevel}
                onCompile={setCompileStatus}
              />
            )}
            <div className="viewport-topbar">
              <div className="live-label">
                <span className="status-dot" /> LIVE PREVIEW{" "}
                <span className="render-tag">
                  {shape === "Draped cloth" ? "FROZEN CLOTH" : "PBR"}
                </span>
              </div>
              <div className="viewport-top-actions">
                <button
                  className={"rotate-button " + (rotate ? "enabled" : "")}
                  onClick={() => setRotate(!rotate)}
                >
                  <span className="small-toggle">
                    <i />
                  </span>{" "}
                  Auto rotate
                </button>
                <span className="tool-divider" />
                <button
                  className="icon-button"
                  title={full ? "Exit focus mode (F)" : "Focus viewport (F)"}
                  onClick={() => setFull(!full)}
                >
                  {full ? <Minimize size={16} /> : <Maximize size={16} />}
                </button>
              </div>
            </div>
            <div
              className={
                "viewport-heading " + (zoomLevel > 220 ? "is-macro" : "")
              }
            >
              <div className="eyebrow">
                <span>
                  {String(
                    materials.findIndex((m) => m.id === selected.id) + 1 ||
                      allMaterials.indexOf(selected) + 1,
                  ).padStart(2, "0")}
                </span>{" "}
                / {params.category.toUpperCase()} COLLECTION
              </div>
              <h1>{params.name}</h1>
              <p>{params.description}</p>
            </div>
            {(preparation.phase !== "ready" ||
              compileStatus.phase !== "ready") && (
              <div className="compile-notice" role="status" aria-live="polite">
                <div>
                  <span className="compile-pulse" />
                  <strong>
                    {preparation.phase === "error" ||
                    compileStatus.phase === "error"
                      ? "Preview preparation interrupted"
                      : compileStatus.phase === "compiling"
                        ? "Compiling material shader"
                        : "Preparing procedural library"}
                  </strong>
                  <span>
                    {preparation.done}/{preparation.total}
                  </span>
                </div>
                <progress
                  aria-label="Material preparation progress"
                  max={preparation.total}
                  value={preparation.done}
                />
                <small>
                  {compileStatus.phase === "compiling"
                    ? compileStatus.name
                    : preparation.name}{" "}
                  · completed material previews, not driver-percent
                </small>
              </div>
            )}
            {!ready && (
              <div className="loading-state">
                <span />
                Preparing your studio…
              </div>
            )}
            <div className="side-tools">
              <button
                className="icon-button"
                title="Zoom in"
                onClick={() => setZoom({ direction: 1, time: Date.now() })}
              >
                <ZoomIn size={17} />
              </button>
              <button
                className="icon-button"
                title="Zoom out"
                onClick={() => setZoom({ direction: -1, time: Date.now() })}
              >
                <ZoomOut size={17} />
              </button>
              <span />
              <button
                className={"icon-button " + (wireframe ? "on" : "")}
                title="Toggle wireframe"
                onClick={() => setWireframe(!wireframe)}
              >
                <Box size={17} />
              </button>
              <button
                className="icon-button"
                title="Reset camera (R)"
                onClick={() => setResetToken((n) => n + 1)}
              >
                <RotateCcw size={16} />
              </button>
            </div>
            <div className="axis-gizmo">
              <span className="axis-y">Y</span>
              <span className="axis-z">Z</span>
              <span className="axis-x">X</span>
              <i />
              <b />
              <em />
            </div>
            <div className="viewport-caption">
              <span className="caption-line" />
              <span>EVERY DETAIL. BY DESIGN.</span>
            </div>
            <div className="viewport-bottom">
              <div className="zoom-dock">
                <button
                  title="Fit entire asset"
                  onClick={() => setResetToken((n) => n + 1)}
                >
                  Fit
                </button>
                <span className="zoom-readout">
                  {zoomLevel.toLocaleString()}
                  <small>%</small>
                </span>
                <input
                  aria-label="Viewport zoom"
                  type="range"
                  min="1"
                  max="4"
                  step=".005"
                  value={Math.log10(zoomLevel)}
                  onChange={(e) => {
                    const percent = Math.round(10 ** Number(e.target.value));
                    setZoomLevel(percent);
                    setZoom({ percent, time: Date.now() });
                  }}
                />
                <button
                  onClick={() => setZoom({ macro: true, time: Date.now() })}
                >
                  <Scan size={12} /> Macro
                </button>
              </div>
              <div className="view-controls">
                <label>
                  <Box size={15} />
                  <select
                    aria-label="Preview object"
                    value={shape}
                    onChange={(e) => setShape(e.target.value)}
                  >
                    {MATERIAL_PREVIEW_SHAPES.map((x) => (
                      <option key={x}>{x}</option>
                    ))}
                  </select>
                  <ChevronDown size={13} />
                </label>
                <span className="view-control-divider" />
                <label>
                  <Sun size={16} />
                  <select
                    aria-label="Studio lighting"
                    value={environment}
                    onChange={(e) => setEnvironment(e.target.value)}
                  >
                    {MATERIAL_ENVIRONMENTS.map((x) => (
                      <option key={x}>{x}</option>
                    ))}
                  </select>
                  <ChevronDown size={13} />
                </label>
                <span className="view-control-divider" />
                <button
                  className="icon-button"
                  title="Save viewport image"
                  onClick={screenshot}
                >
                  <Camera size={16} />
                </button>
              </div>
              <div className="orbit-hint">
                <MousePointer2 size={12} />
                <span>MMB orbit · Shift+MMB pan</span>
                <i />
                <span>
                  Wheel / Ctrl+MMB zoom · Camera lag · Numpad . frame
                </span>
              </div>
            </div>
            <div className="viewport-status">
              <span>
                <span className="tiny-square" />{" "}
                {wireframe ? "WIREFRAME" : "PERSPECTIVE"}
                <i /> {environment === "Daylight" ? "DAYLIGHT" : "STUDIO"}{" "}
                LIGHTING
              </span>
              <span>
                WEBGL 2.0 <span className="status-dot" />
              </span>
            </div>
          </section>
          <MaterialInspectorPanel
            params={params}
            selected={selected}
            setParams={setParams}
            notify={notify}
            thumbs={thumbs}
            favorites={favorites}
            toggleFavorite={toggleFavorite}
            tab={tab}
            setTab={setTab}
            layerCount={layerCount}
            update={update}
            setCustomName={setCustomName}
            setModal={setModal}
            openWorkspace={openWorkspace}
          />
        </main>
        {toast && (
          <div className="toast">
            <span>
              <Check size={14} />
            </span>
            {toast}
            <button onClick={() => setToast("")}>
              <X size={13} />
            </button>
          </div>
        )}
        {[
          "save",
          "export",
          "bake",
          "guide",
          "shortcuts",
          "settings",
          "workspace",
        ].includes(modal) &&
          !authoringOpen && (
            <div className="modal-backdrop" onClick={() => setModal(null)}>
              <div
                className={"modal " + (modal === "guide" ? "guide-modal" : "")}
                role="dialog"
                aria-modal="true"
                aria-labelledby="modal-title"
                onClick={(e) => e.stopPropagation()}
              >
                <button
                  className="modal-close icon-button"
                  onClick={() => setModal(null)}
                  aria-label="Close dialog"
                >
                  <X size={18} />
                </button>
                {modal === "save" && (
                  <>
                    <div className="modal-icon">
                      <Layers3 size={23} />
                    </div>
                    <span className="eyebrow">YOUR OWN FINISH</span>
                    <h2 id="modal-title">Save a material preset</h2>
                    <p>
                      Keep your current colors and surface settings in your
                      personal material collection.
                    </p>
                    <form onSubmit={savePreset}>
                      <label className="form-label">Material name</label>
                      <input
                        className="modal-input"
                        autoFocus
                        maxLength={35}
                        value={customName}
                        onChange={(e) => setCustomName(e.target.value)}
                        placeholder="Give your material a name"
                        required
                      />
                      <div className="save-preview">
                        <span style={{ background: params.color }} />
                        <div>
                          <strong>{params.category} material</strong>
                          <p>
                            {Math.round(params.roughness * 100)}% roughness ·{" "}
                            {Math.round(params.coat * 100)}% clearcoat
                          </p>
                        </div>
                        <Check size={17} />
                      </div>
                      <button type="submit" className="primary-button">
                        Save to my library <ArrowRight size={16} />
                      </button>
                    </form>
                  </>
                )}
                {modal === "export" && (
                  <>
                    <div className="modal-icon">
                      <ArrowUpRight size={24} />
                    </div>
                    <span className="eyebrow">TAKE IT WITH YOU</span>
                    <h2 id="modal-title">Export your material</h2>
                    <p>
                      {params.name}. Every parameter, ready for your next
                      project.
                    </p>
                    <button
                      className="export-option"
                      onClick={() =>
                        download(
                          exportMaterialJSON(params),
                          params.id + ".json",
                          "application/json",
                        )
                      }
                    >
                      <FileJson size={23} />
                      <span>
                        <strong>Material preset</strong>
                        <small>
                          All surface parameters and flake colors · JSON
                        </small>
                      </span>
                      <ArrowDownToLine size={17} />
                    </button>
                    <button
                      className="export-option"
                      onClick={() =>
                        download(
                          exportMaterialShader({
                            ...params,
                            clothMapping: shape === "Draped cloth",
                          }),
                          params.id + ".js",
                          "text/javascript",
                        )
                      }
                    >
                      <Code2 size={23} />
                      <span>
                        <strong>Three.js procedural shader</strong>
                        <small>
                          Ready-to-use physical material · JavaScript
                        </small>
                      </span>
                      <ArrowDownToLine size={17} />
                    </button>
                    <button
                      className="export-option"
                      onClick={() => openWorkspace("baking", "material")}
                    >
                      <Layers3 size={23} />
                      <span>
                        <strong>Bake procedural maps</strong>
                        <small>Six surface channels + recipe · PNG / ZIP</small>
                      </span>
                      <ArrowRight size={17} />
                    </button>
                    <button className="export-option" onClick={screenshot}>
                      <Camera size={23} />
                      <span>
                        <strong>Viewport snapshot</strong>
                        <small>
                          Your material in the current studio lighting · PNG
                        </small>
                      </span>
                      <ArrowDownToLine size={17} />
                    </button>
                    <p className="modal-footnote">
                      Shaders target Three.js. Direct Unreal Engine import is
                      not supported.
                    </p>
                  </>
                )}
                {modal === "bake" && (
                  <BakePanel params={params} onNotify={notify} />
                )}
                {modal === "guide" && (
                  <>
                    <div className="modal-icon">
                      <BookOpen size={23} />
                    </div>
                    <span className="eyebrow">WELCOME TO ALLOY</span>
                    <h2 id="modal-title">A studio for the surface.</h2>
                    <p>
                      Explore procedural materials, vector-backed leather and
                      custom SVG/image surface patterns. Sources are embedded;
                      no runtime CDN is needed.
                    </p>
                    <div className="guide-steps">
                      {[
                        {
                          icon: <Grid2X2 size={19} />,
                          title: "01 — Find your finish",
                          text: "Choose from paints, metals, ceramic, rubber, plastics, leather, textiles and glass in the material library.",
                        },
                        {
                          icon: <Move3D size={19} />,
                          title: "02 — Look a little closer",
                          text: "Middle-drag to orbit, Shift+middle-drag to pan and scroll to zoom. Camera lag smooths navigation. Switch between seventeen assets, including a frozen cloth drape over a ball. Textile presets select the drape automatically.",
                        },
                        {
                          icon: <SlidersHorizontal size={19} />,
                          title: "03 — Make it yours",
                          text: "Use finish controls tuned to each material. Explore nine fabric weaves with independent yarn colors, cellular flake ramps and angle-shifting iridescent paint.",
                        },
                        {
                          icon: <ArrowUpRight size={19} />,
                          title: "04 — Take it further",
                          text: "Save a local preset, export the Three.js shader, bake six procedural surface maps, or capture the viewport as a PNG.",
                        },
                      ].map((s) => (
                        <div key={s.title}>
                          {s.icon}
                          <span>
                            <h4>{s.title}</h4>
                            <p>{s.text}</p>
                          </span>
                        </div>
                      ))}
                    </div>
                    <button
                      className="primary-button"
                      onClick={() => setModal(null)}
                    >
                      Let’s make something beautiful <ArrowRight size={15} />
                    </button>
                  </>
                )}
                {modal === "shortcuts" && (
                  <>
                    <div className="modal-icon">
                      <Command size={23} />
                    </div>
                    <h2 id="modal-title">Less clicking. More creating.</h2>
                    <div className="shortcut-list">
                      {[
                        ["Search materials", "/"],
                        ["Reset camera", "R"],
                        ["Focus viewport", "F"],
                        ["Toggle auto rotation", "Space"],
                        ["Save custom preset", "⌘ / Ctrl S"],
                        ["Close dialog / exit focus", "Esc"],
                      ].map(([l, k]) => (
                        <div key={k}>
                          <span>{l}</span>
                          <kbd>{k}</kbd>
                        </div>
                      ))}
                    </div>
                  </>
                )}
                {modal === "settings" && (
                  <>
                    <div className="modal-icon">
                      <Settings2 size={23} />
                    </div>
                    <h2 id="modal-title">Studio settings</h2>
                    <p>Set up your workspace for a closer look.</p>
                    <div className="settings-row">
                      <span>Auto rotate preview</span>
                      <button
                        className={"toggle " + (rotate ? "on" : "")}
                        onClick={() => setRotate(!rotate)}
                        aria-label="Toggle rotation"
                      >
                        <i />
                      </button>
                    </div>
                    <div className="settings-row">
                      <span>Wireframe overlay</span>
                      <button
                        className={"toggle " + (wireframe ? "on" : "")}
                        onClick={() => setWireframe(!wireframe)}
                        aria-label="Toggle wireframe"
                      >
                        <i />
                      </button>
                    </div>
                    <div className="settings-row">
                      <span>Lighting environment</span>
                      <select
                        value={environment}
                        onChange={(e) => setEnvironment(e.target.value)}
                      >
                        {[
                          "Studio softbox",
                          "Daylight",
                          "Warm atelier",
                          "Low-key studio",
                        ].map((e) => (
                          <option key={e}>{e}</option>
                        ))}
                      </select>
                    </div>
                    <div className="settings-info">
                      <Check size={15} /> ACES tone mapping · Physical lighting
                      · 2× display resolution
                    </div>
                  </>
                )}
                {modal === "workspace" && (
                  <>
                    <div className="avatar large">JD</div>
                    <h2 id="modal-title">Your personal studio.</h2>
                    <p>
                      This workspace runs locally in your browser. Saved presets
                      and favorites stay on this device—no account required.
                    </p>
                    <div className="workspace-numbers">
                      <div>
                        <strong>{saved.length}</strong>
                        <span>Custom presets</span>
                      </div>
                      <div>
                        <strong>{favorites.length}</strong>
                        <span>Favorites</span>
                      </div>
                    </div>
                    <button
                      className="primary-button"
                      onClick={() => {
                        setOnlySaved(true);
                        setCategory("All materials");
                        setSearch("");
                        setModal(null);
                      }}
                    >
                      Open my collection <ArrowRight size={15} />
                    </button>
                  </>
                )}
              </div>
            </div>
          )}
      </div>
    </div>
  );
}
createRoot(document.getElementById("root")).render(<><App /><StudioTooltips /></>);
