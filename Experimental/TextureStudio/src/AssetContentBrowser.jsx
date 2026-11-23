import { createPortal } from "react-dom";
import {
  MATERIAL_PREVIEW_SHAPES,
  MATERIAL_ENVIRONMENTS,
} from "./materialViewportOptions.js";
import useDialogFocus from "./useDialogFocus.js";
import React, { useEffect, useRef, useState } from "react";
import {
  Layers3,
  Folder,
  Brush,
  Stamp,
  Sparkles,
  ChevronRight,
  Star,
  Search,
  Grid2X2,
  List,
  ChevronUp,
  ChevronDown,
  Maximize2,
  FolderOpen,
  ArrowDownToLine,
  ArrowRight,
  X,
} from "lucide-react";
import Viewport, { renderThumbnails } from "./Viewport.jsx";
import MaterialInspectorPanel from "./MaterialInspectorPanel.jsx";
import { exportMaterialJSON, exportMaterialShader } from "./materialExport.js";
import {
  PAINT_TOOLS,
  PAINT_FAMILIES,
  normalizePaintSettings,
  selectPaintInstrument,
  toolArtURL,
} from "./paintToolCatalogue.js";
import { PaintToolProperties } from "./PaintToolMenu.jsx";
import { CHANNEL_GENERATORS, normalizeGenerator } from "./paintChannelModel.js";
import "./assetContentBrowser.css";
const MAX_THUMBNAIL_BATCH = 32;
function restoredHeight() {
  try {
    return Math.max(
      38,
      Math.min(
        Math.min(900, window.innerHeight - 86),
        Number(localStorage.getItem("alloy-content-browser-height")) || 38,
      ),
    );
  } catch {
    return 38;
  }
}
function download(text, name, type) {
  const url = URL.createObjectURL(new Blob([text], { type })),
    a = document.createElement("a");
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
export default function AssetContentBrowser({
  workspace,
  painting,
  onPaintSettings,
  onAssign,
  canAssign,
  onExpandedChange,
  onSaveProject,
  onUndo,
  onRedo,
  stampPresets = [],
  stampPresetId,
  onStampPreset,
  onEditStamp,
}) {
  const [height, setHeight] = useState(restoredHeight),
    [kind, setKind] = useState("material"),
    [category, setCategory] = useState("All"),
    [query, setQuery] = useState(""),
    [view, setView] = useState("grid"),
    [thumbnailIds, setThumbnailIds] = useState([]),
    [assetDragging, setAssetDragging] = useState(false),
    [mobile, setMobile] = useState("assets");
  const [generator, setGenerator] = useState(
      normalizeGenerator({ id: "PerlinNoise" }),
    ),
    [modal, setModal] = useState(null),
    [customName, setCustomName] = useState(""),
    [status, setStatus] = useState("");
  const dialogRef = useRef(null);
  useDialogFocus(modal === "save", dialogRef);
  const drag = useRef(null),
    cache = useRef(workspace.thumbs),
    container = useRef(null),
    assetScroll = useRef(null),
    assetDragFrame = useRef(null),
    [progress, setProgress] = useState(null);
  function endAssetDrag() {
    cancelAnimationFrame(assetDragFrame.current);
    assetDragFrame.current = null;
    setAssetDragging(false);
  }
  useEffect(() => () => cancelAnimationFrame(assetDragFrame.current), []);
  cache.current = workspace.thumbs;
  const expanded = height > 80,
    maxHeight = () => Math.min(900, window.innerHeight - 86);
  useEffect(() => {
    onExpandedChange(expanded);
  }, [expanded, onExpandedChange]);
  const resize = (value) =>
    setHeight(Math.round(Math.max(38, Math.min(maxHeight(), value))));
  useEffect(() => {
    try {
      localStorage.setItem("alloy-content-browser-height", String(height));
    } catch {}
  }, [height]);
  useEffect(() => {
    const fit = () => resize(height);
    window.addEventListener("resize", fit);
    return () => window.removeEventListener("resize", fit);
  }, [height]);
  useEffect(() => {
    if (status) {
      const timer = setTimeout(() => setStatus(""), 3500);
      return () => clearTimeout(timer);
    }
  }, [status]);
  const materialAssets = workspace.materials.map((p) => ({
    id: p.id,
    name: p.name,
    type: "material",
    category: p.category,
    material: p,
  }));
  const brushAssets = PAINT_TOOLS.map((t) => ({
    id: t.id,
    name: t.label,
    type: "brush",
    category: PAINT_FAMILIES.find((f) => f.key === t.key).label,
    tool: t,
  }));
  const generatorAssets = CHANNEL_GENERATORS.map((g) => ({
    id: g.id,
    name: g.label,
    type: "generator",
    category: g.group,
    generator: g,
  }));
  const stampAssets = stampPresets.map((preset) => ({
    id: preset.id,
    name: preset.name,
    type: "stamp",
    category: "Stamps",
    preset,
  }));
  const pool =
    kind === "material"
      ? materialAssets
      : kind === "brush"
        ? brushAssets
        : kind === "stamp"
          ? stampAssets
          : generatorAssets;
  const filtered = pool.filter(
    (a) =>
      (category === "All" ||
        (category === "Saved" && a.id.startsWith("custom-")) ||
        a.category === category) &&
      `${a.name} ${a.category}`.toLowerCase().includes(query.toLowerCase()),
  );
  const visible = filtered,
    allIds = filtered.map((a) => a.id).join("|");
  useEffect(() => {
    if (!expanded || kind !== "material" || !assetScroll.current) {
      setThumbnailIds([]);
      return;
    }
    const intersecting = new Set(),
      observer = new IntersectionObserver(
        (entries) => {
          for (const entry of entries) {
            const id = entry.target.dataset.assetId;
            entry.isIntersecting
              ? intersecting.add(id)
              : intersecting.delete(id);
          }
          const ids = filtered
            .filter((a) => intersecting.has(a.id))
            .slice(0, MAX_THUMBNAIL_BATCH)
            .map((a) => a.id);
          setThumbnailIds((old) =>
            old.join("|") === ids.join("|") ? old : ids,
          );
        },
        { root: assetScroll.current, rootMargin: "140px 0px" },
      );
    assetScroll.current
      .querySelectorAll("[data-asset-id]")
      .forEach((el) => observer.observe(el));
    return () => observer.disconnect();
  }, [expanded, kind, allIds, view]);
  const ids = thumbnailIds.join("|");
  useEffect(() => {
    if (!expanded || kind !== "material" || !thumbnailIds.length) return;
    const controller = new AbortController(),
      catalog = filtered
        .filter((a) => thumbnailIds.includes(a.id))
        .map((a) => a.material);
    const timer = setTimeout(
      () =>
        renderThumbnails(
          (result) => workspace.setThumbs((old) => ({ ...old, ...result })),
          setProgress,
          controller.signal,
          cache.current,
          catalog,
        ).catch((e) => {
          if (!controller.signal.aborted)
            setStatus("Material thumbnails: " + e.message);
        }),
      140,
    );
    return () => {
      clearTimeout(timer);
      controller.abort();
    };
  }, [expanded, kind, ids]);
  const setType = (type) => {
    setKind(type);
    setCategory("All");
  };
  function select(asset) {
    if (asset.type === "material") workspace.selectMaterial(asset.material);
    else if (asset.type === "brush")
      onPaintSettings(selectPaintInstrument(painting, asset.id));
    else if (asset.type === "stamp") onStampPreset?.(asset.id);
    else setGenerator(normalizeGenerator({ id: asset.id }));
  }
  const selectedId =
    kind === "material"
      ? workspace.selected.id
      : kind === "brush"
        ? painting.toolId
        : kind === "stamp"
          ? stampPresetId
          : generator.id;
  const selected = pool.find((a) => a.id === selectedId) || pool[0];
  const assign = () => {
    if (!selected) return;
    onAssign({
      type: selected.type,
      id: selected.id,
      name: selected.name,
      parameters: kind === "generator" ? generator.parameters : null,
    });
    if (selected.type === "stamp") {
      resize(38);
      setStatus("Stamp selected · click the teapot surface to place it.");
    } else setStatus("Asset link stored on the layer · not paint compositing.");
  };
  const inspectorProps = {
    ...workspace,
    thumbs: workspace.thumbs,
    notify: setStatus,
    tab: workspace.tab,
    setTab: workspace.setTab,
    setCustomName,
    setModal,
    openWorkspace: workspace.openWorkspace,
  };
  return (
    <section
      className={`asset-content-browser ${expanded ? "is-open" : ""} ${assetDragging ? "is-asset-dragging" : ""}`}
      aria-label="Content browser"
      data-expanded={expanded}
      data-mobile-pane={mobile}
      style={{ height }}
      ref={container}
      onKeyDown={(e) => {
        const command = e.ctrlKey || e.metaKey,
          key = e.key.toLowerCase();
        if (command && key === "s") {
          e.preventDefault();
          if (kind === "material") {
            setCustomName(workspace.params.name + " Custom");
            setModal("save");
          } else onSaveProject();
        } else if (command && kind !== "material" && ["z", "y"].includes(key)) {
          e.preventDefault();
          key === "y" || e.shiftKey ? onRedo() : onUndo();
        }
        if (e.key === "Escape") setModal(null);
      }}
    >
      <div
        className="cb-resize"
        role="separator"
        aria-label="Resize content browser"
        aria-orientation="horizontal"
        aria-valuemin={38}
        aria-valuemax={maxHeight()}
        aria-valuenow={height}
        tabIndex={0}
        onPointerDown={(e) => {
          if (e.button !== 0) return;
          e.preventDefault();
          drag.current = { y: e.clientY, height };
          e.currentTarget.setPointerCapture(e.pointerId);
        }}
        onPointerMove={(e) => {
          if (drag.current)
            resize(drag.current.height + drag.current.y - e.clientY);
        }}
        onPointerUp={() => {
          drag.current = null;
          resize(height > 80 ? Math.max(300, height) : 38);
        }}
        onPointerCancel={() => {
          drag.current = null;
        }}
        onLostPointerCapture={() => {
          drag.current = null;
        }}
        onDoubleClick={() => resize(expanded ? 38 : maxHeight())}
        onKeyDown={(e) => {
          if (["ArrowUp", "ArrowDown", "Home", "End"].includes(e.key)) {
            e.preventDefault();
            resize(
              e.key === "Home"
                ? 38
                : e.key === "End"
                  ? maxHeight()
                  : e.key === "ArrowUp"
                    ? height <= 80
                      ? 320
                      : height + 70
                    : height - 70 <= 240
                      ? 38
                      : height - 70,
            );
          }
        }}
      >
        <i />
      </div>
      <header className="cb-bar">
        <button
          aria-label={
            expanded ? "Collapse content browser" : "Open content browser"
          }
          aria-expanded={expanded}
          onClick={() =>
            resize(expanded ? 38 : Math.max(360, window.innerHeight * 0.58))
          }
        >
          <FolderOpen size={14} />
          Content browser
          {expanded ? <ChevronDown size={13} /> : <ChevronUp size={13} />}
        </button>
        <span>
          {workspace.materials.length} materials · 102 instruments · 10
          generators · {stampPresets.length} stamps
        </span>
        <small>
          {status || "Drag the top edge upward to open the asset workspace"}
        </small>
        <button
          aria-label="Expand content browser"
          onClick={() => resize(maxHeight())}
        >
          <Maximize2 size={13} />
        </button>
      </header>
      {expanded && (
        <>
          <div className="cb-mobile-tabs">
            <select
              aria-label="Asset type"
              value={kind}
              onChange={(e) => setType(e.target.value)}
            >
              <option value="material">Materials</option>
              <option value="brush">Brushes / instruments</option>
              <option value="generator">Generators</option>
              <option value="stamp">Stamp presets</option>
            </select>
            <button
              aria-pressed={mobile === "assets"}
              onClick={() => setMobile("assets")}
            >
              Assets
            </button>
            <button
              aria-pressed={mobile === "preview"}
              onClick={() => setMobile("preview")}
            >
              Preview + inspector
            </button>
          </div>
          <div className="cb-layout">
            <aside className="cb-outliner" aria-label="Asset outliner">
              <h3>
                <Layers3 size={13} />
                Asset library
              </h3>
              {[
                ["material", "Materials", workspace.materials.length, Layers3],
                ["brush", "Brushes / instruments", 102, Brush],
                ["generator", "Generators", 10, Sparkles],
                ["stamp", "Stamp presets", stampPresets.length, Stamp],
              ].map(([type, label, count, Icon]) => (
                <button
                  key={type}
                  aria-pressed={kind === type}
                  onClick={() => setType(type)}
                >
                  <Icon size={13} />
                  <span>{label}</span>
                  <small>{count}</small>
                  <ChevronRight size={10} />
                </button>
              ))}
              <div className="cb-outliner-categories">
                <span>
                  {kind === "material"
                    ? "MATERIAL FAMILIES"
                    : kind === "brush"
                      ? "INSTRUMENT FAMILIES"
                      : kind === "stamp"
                        ? "STAMP LIBRARY"
                        : "GENERATOR FAMILIES"}
                </span>
                {[
                  "All",
                  ...(kind === "material" ? ["Saved"] : []),
                  ...new Set(pool.map((a) => a.category)),
                ].map((name) => (
                  <button
                    key={name}
                    aria-pressed={category === name}
                    onClick={() => {
                      setCategory(name);
                      assetScroll.current?.scrollTo({ top: 0 });
                    }}
                  >
                    <Folder size={12} />
                    <span>{name}</span>
                    <small>
                      {name === "All"
                        ? pool.length
                        : pool.filter((a) =>
                            name === "Saved"
                              ? a.id.startsWith("custom-")
                              : a.category === name,
                          ).length}
                    </small>
                  </button>
                ))}
              </div>
            </aside>
            <section className="cb-assets" aria-label="Asset grid">
              <div className="cb-grid-header">
                <div>
                  <strong>
                    {category === "All"
                      ? kind === "material"
                        ? "Materials"
                        : kind === "brush"
                          ? "Paint instruments"
                          : "Generators"
                      : category}
                  </strong>
                  <small>{filtered.length} assets</small>
                </div>
                <label>
                  <Search size={13} />
                  <input
                    aria-label="Search content assets"
                    placeholder="Search assets…"
                    value={query}
                    onChange={(e) => {
                      setQuery(e.target.value);
                      assetScroll.current?.scrollTo({ top: 0 });
                    }}
                  />
                </label>
                <button
                  aria-label="Asset grid view"
                  aria-pressed={view === "grid"}
                  onClick={() => setView("grid")}
                >
                  <Grid2X2 size={13} />
                </button>
                <button
                  aria-label="Asset list view"
                  aria-pressed={view === "list"}
                  onClick={() => setView("list")}
                >
                  <List size={13} />
                </button>
              </div>
              <div
                className={`cb-asset-items cb-${view}`}
                ref={assetScroll}
                tabIndex={0}
                aria-label="Scrollable asset previews"
              >
                {visible.map((asset) => (
                  <button
                    key={asset.id}
                    className="cb-asset-tile"
                    data-asset-id={asset.id}
                    aria-label={`Inspect asset ${asset.name}`}
                    aria-pressed={selectedId === asset.id}
                    onClick={() => select(asset)}
                    draggable
                    onDragStart={(e) => {
                      // Let the native drag image be captured first. Moving or
                      // hiding the source synchronously cancels Chromium drags.
                      assetDragFrame.current = requestAnimationFrame(() =>
                        setAssetDragging(true),
                      );
                      e.dataTransfer.effectAllowed = "copy";
                      e.dataTransfer.setData(
                        "application/x-alloy-asset",
                        JSON.stringify({
                          type: asset.type,
                          id: asset.id,
                          name: asset.name,
                          parameters:
                            asset.type === "generator"
                              ? asset.id === generator.id
                                ? generator.parameters
                                : normalizeGenerator({ id: asset.id })
                                    .parameters
                              : undefined,
                        }),
                      );
                    }}
                    onDragEnd={endAssetDrag}
                  >
                    {asset.type === "material" ? (
                      workspace.thumbs[asset.id] ? (
                        <img src={workspace.thumbs[asset.id]} alt="" />
                      ) : (
                        <div
                          className="cb-material-fallback"
                          style={{ "--asset-color": asset.material.color }}
                          aria-label="Material thumbnail pending"
                        >
                          <small>Rendering thumbnail…</small>
                        </div>
                      )
                    ) : asset.type === "stamp" ? (
                      <img src={asset.preset.raster.dataUrl} alt="" />
                    ) : asset.type === "brush" ? (
                      <img
                        className="cb-tool-art"
                        src={toolArtURL(asset.id)}
                        alt=""
                      />
                    ) : (
                      <div className="cb-generator-preview">
                        <Layers3 size={25} />
                        <small>Configuration</small>
                      </div>
                    )}
                    <span>
                      {asset.name}
                      <small>{asset.category}</small>
                    </span>
                  </button>
                ))}
                {!visible.length && (
                  <p>
                    No assets match. Clear the search or choose another family.
                  </p>
                )}
              </div>
              <footer>
                <span>{filtered.length} assets</span>
                <small>
                  {kind === "material" && progress && progress.phase !== "ready"
                    ? `Rendering ${progress.done}/${progress.total} visible thumbnails`
                    : "Scroll to browse · drag to assign"}
                </small>
              </footer>
            </section>
            <section
              className="cb-preview-inspector"
              aria-label="Asset preview and inspector"
            >
              <div className="cb-preview-toolbar">
                <span>{selected?.name}</span>
                <button disabled={!canAssign} onClick={assign}>
                  Assign to layer <ArrowRight size={12} />
                </button>
              </div>
              {kind === "material" ? (
                <div className="cb-material-workspace studio-material">
                  <div
                    className="cb-material-preview"
                    aria-label="Material asset preview"
                  >
                    <Viewport
                      params={workspace.params}
                      shape={workspace.shape}
                      environment={workspace.environment}
                      rotate={workspace.rotate}
                      wireframe={workspace.wireframe}
                      resetToken={workspace.resetToken}
                      zoom={workspace.zoom}
                      onZoomChange={workspace.setZoomLevel}
                      studioLayout
                    />
                    <div className="cb-preview-controls">
                      <select
                        aria-label="Asset preview object"
                        value={workspace.shape}
                        onChange={(e) => workspace.setShape(e.target.value)}
                      >
                        {MATERIAL_PREVIEW_SHAPES.map((shape) => (
                          <option key={shape}>{shape}</option>
                        ))}
                      </select>
                      <button
                        onClick={() =>
                          download(
                            exportMaterialJSON(workspace.params),
                            workspace.params.id + ".json",
                            "application/json",
                          )
                        }
                      >
                        <ArrowDownToLine size={11} />
                        JSON
                      </button>
                      <button
                        onClick={() =>
                          download(
                            exportMaterialShader({
                              ...workspace.params,
                              clothMapping: workspace.shape === "Draped cloth",
                            }),
                            workspace.params.id + ".js",
                            "text/javascript",
                          )
                        }
                      >
                        Shader
                      </button>
                    </div>
                  </div>
                  <div className="cb-view-options">
                    <select
                      aria-label="Asset preview lighting"
                      value={workspace.environment}
                      onChange={(e) => workspace.setEnvironment(e.target.value)}
                    >
                      {MATERIAL_ENVIRONMENTS.map((name) => (
                        <option key={name}>{name}</option>
                      ))}
                    </select>
                    <button
                      aria-label="Asset auto rotate"
                      aria-pressed={workspace.rotate}
                      onClick={() => workspace.setRotate(!workspace.rotate)}
                    >
                      Rotate
                    </button>
                    <button
                      aria-label="Asset wireframe"
                      aria-pressed={!!workspace.wireframe}
                      onClick={() =>
                        workspace.setWireframe(!workspace.wireframe)
                      }
                    >
                      Wireframe
                    </button>
                    <button
                      aria-label="Frame material asset"
                      onClick={() => workspace.setResetToken((n) => n + 1)}
                    >
                      Frame
                    </button>
                  </div>
                  <MaterialInspectorPanel {...inspectorProps} />
                </div>
              ) : kind === "stamp" ? (
                <div className="cb-stamp-inspector">
                  {selected && (
                    <>
                      <img
                        src={selected.preset.raster.dataUrl}
                        alt={`${selected.name} stamp artwork`}
                      />
                      <h2>{selected.name}</h2>
                      <p>
                        {selected.preset.project.layers.length} composited Text
                        / SVG / Image components
                      </p>
                      <small>
                        {selected.preset.project.channels.length} channel
                        targets ·{" "}
                        {selected.preset.project.maskMode === "alpha"
                          ? "alpha silhouette"
                          : "luminance × alpha"}{" "}
                        mask
                      </small>
                      <button
                        onClick={() => onEditStamp?.(selected.preset.project)}
                      >
                        Edit in Stamp workspace ↗
                      </button>
                      <button disabled={!canAssign} onClick={assign}>
                        Stamp on selected layer <Stamp size={14} />
                      </button>
                      <p>
                        Click the teapot surface to stamp. Each placement is its
                        own editable decal component; the preset is embedded
                        once per project.
                      </p>
                    </>
                  )}
                </div>
              ) : kind === "brush" ? (
                <PaintToolProperties
                  settings={painting}
                  onChange={onPaintSettings}
                />
              ) : (
                <div className="cb-generator-inspector">
                  <h2>{selected?.name}</h2>
                  <p>{selected?.generator.note}</p>
                  <small>
                    Stage source settings, then assign to a layer · not
                    evaluated paint
                  </small>
                  {selected?.generator.parameters.map((p) => (
                    <label key={p.key}>
                      {p.label}
                      <input
                        aria-label={`Asset generator ${p.label}`}
                        type="range"
                        min="0"
                        max="1"
                        step="0.01"
                        value={generator.parameters[p.key]}
                        onChange={(e) =>
                          setGenerator({
                            ...generator,
                            parameters: {
                              ...generator.parameters,
                              [p.key]: Number(e.target.value),
                            },
                          })
                        }
                      />
                      <output>{generator.parameters[p.key]}</output>
                    </label>
                  ))}
                </div>
              )}
            </section>
          </div>
        </>
      )}
      {modal === "save" &&
        createPortal(
          <div
            className="cb-save-overlay"
            onPointerDown={(e) => {
              if (e.target === e.currentTarget) setModal(null);
            }}
          >
            <div
              className="cb-save-dialog"
              role="dialog"
              aria-modal="true"
              aria-label="Save asset material preset"
              ref={dialogRef}
              tabIndex={-1}
            >
              <button
                aria-label="Close asset save dialog"
                onClick={() => setModal(null)}
              >
                <X size={14} />
              </button>
              <h2>Save a material preset</h2>
              <form
                onSubmit={(e) => {
                  e.preventDefault();
                  if (workspace.savePreset(customName)) {
                    setModal(null);
                    setStatus("Material saved to the shared library.");
                  }
                }}
              >
                <input
                  data-dialog-autofocus
                  aria-label="Asset material preset name"
                  maxLength={80}
                  value={customName}
                  onChange={(e) => setCustomName(e.target.value)}
                />
                <button type="submit">Save to shared library</button>
              </form>
            </div>
          </div>,
          document.body,
        )}
    </section>
  );
}
