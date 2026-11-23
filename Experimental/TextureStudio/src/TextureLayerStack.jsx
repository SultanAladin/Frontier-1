import { LayerStackFilters } from "./LayerStackPanel.jsx";
import { evaluateTextureStack } from "./textureStackEvaluation.js";
import { texturePart } from "./textureSurfaceModel.js";
import GradientSliceImage from "./GradientSliceImage.jsx";
import { toolArtURL } from "./paintToolCatalogue.js";
import React, { useEffect, useRef, useState } from "react";
import {
  ArrowDown,
  GripVertical,
  Stamp,
  Folder,
  FolderOpen,
  FolderPlus,
  Brush,
  PaintBucket,
  Sparkles,
  Palette,
  ArrowUp,
  Check,
  ChevronDown,
  ChevronRight,
  Copy,
  Eye,
  EyeOff,
  Layers3,
  LockKeyhole,
  Plus,
  Search,
  Trash2,
  UnlockKeyhole,
  X,
} from "lucide-react";
import {
  TEXTURE_CHANNELS,
  TEXTURE_KINDS,
  TEXTURE_LAYER_LIMIT,
  textureAncestors,
  textureSubtree,
  isTextureLayerLocked,
  isTextureLayerVisible,
  textureLayerDropPosition,
  isTextureSubtreeProtected,
} from "./textureDocument.js";

export function LayerKindIcon({ kind, size = 30, open = false }) {
  const Icon =
    kind === "folder"
      ? open
        ? FolderOpen
        : Folder
      : kind === "decal"
        ? Stamp
        : kind === "paint"
          ? Brush
          : kind === "fill"
            ? PaintBucket
            : kind === "generator"
              ? Sparkles
              : Layers3;
  return (
    <span
      className={`tp-kind-icon tp-kind-${kind}`}
      style={{
        width: size,
        height: size,
        "--kind-color": TEXTURE_KINDS[kind]?.color,
      }}
      aria-hidden="true"
    >
      <Icon size={size * 0.5} strokeWidth={1.5} />
    </span>
  );
}
export function LayerThumbnail({
  layer,
  thumbs = {},
  size = 30,
  open = false,
}) {
  const texture = layer.channels
    .map((id) => layer.channelSettings?.[id])
    .find(
      (s) =>
        (s?.mode === "texture" && s.texture) ||
        (s?.mode === "generator" && s.generator?.result),
    );
  const sourceTexture = texture?.texture || texture?.generator?.result;
  const gradient = layer.channels
    .map((id) => layer.channelSettings[id])
    .find((s) => s?.mode === "gradient")?.gradient;
  const image =
    sourceTexture?.dataUrl ||
    (layer.sourceAsset?.type === "material"
      ? thumbs[layer.sourceAsset.id]
      : layer.sourceAsset?.type === "brush" && layer.kind !== "paint"
        ? toolArtURL(layer.sourceAsset.id, true)
        : null);
  if (image)
    return (
      <span
        className="tp-layer-thumbnail"
        style={{ width: size, height: size }}
      >
        <img
          src={image}
          alt={
            sourceTexture
              ? `${layer.name} texture preview`
              : `${layer.name} source preview`
          }
        />
      </span>
    );
  if (gradient)
    return (
      <span
        className="tp-layer-thumbnail"
        style={{ width: size, height: size }}
      >
        <GradientSliceImage
          gradient={gradient}
          alt={`${layer.name} gradient field preview`}
        />
      </span>
    );
  if (layer.kind === "paint")
    return (
      <span
        className="tp-layer-thumbnail tp-empty-texture"
        style={{ width: size, height: size }}
        role="img"
        aria-label={
          layer.paint?.operations.length
            ? `${layer.name} digital paint target`
            : `${layer.name} empty texture`
        }
        title={
          layer.paint?.operations.length
            ? `${layer.paint.operations.length} digital operations · evaluated in Lit / UV`
            : "Empty digital paint target"
        }
      />
    );
  return <LayerKindIcon kind={layer.kind} size={size} open={open} />;
}
export function MaskIcon({ mask, size = 26 }) {
  if (mask.kind === "gradient")
    return (
      <span
        className="tp-mask-icon gradient-mask"
        style={{ width: size, height: size }}
      >
        <GradientSliceImage
          gradient={mask.gradient}
          mask={mask}
          alt="Gradient mask field preview"
        />
      </span>
    );
  const white = (mask.fill === 1) !== mask.inverted;
  return (
    <span
      className={`tp-mask-icon ${white ? "white" : "black"} ${mask.kind === "color" ? "color-mask" : ""}`}
      style={{ width: size, height: size, "--mask-color": mask.color }}
      aria-hidden="true"
    >
      <i />
      {mask.kind === "color" && <b />}
      {!!mask.paint?.operations.length && (
        <span className="tp-mask-painted-mark">✎</span>
      )}
    </span>
  );
}
export function DockTab({ icon: Icon, children, detail }) {
  return (
    <div className="tp-tab-strip">
      <div className="tp-document-tab active">
        <Icon size={12} />
        <span>{children}</span>
        {detail && <small>{detail}</small>}
      </div>
    </div>
  );
}
export default function TextureLayerStack({
  doc,
  selection,
  select,
  onAction,
  onBake,
  onDrop,
  onVisibility,
  onLock,
  onMask,
  onAssetDrop,
  onFolderDrop,
  onMove,
  decalContext,
  thumbs = {},
  disabled,
}) {
  const [systemOpen, setSystemOpen] = useState(false);
  const stackPlan = evaluateTextureStack(doc);
  const [query, setQuery] = useState("");
  const [filter, setFilter] = useState("all");
  const [addOpen, setAddOpen] = useState(false);
  const [expanded, setExpanded] = useState({});
  const [dropTarget, setDropTarget] = useState(null);
  const dragged = useRef(null),
    treeRef = useRef(null),
    menuRef = useRef(null),
    addRef = useRef(null);
  const selected = doc.layers.find((l) => l.id === selection.id);
  const siblings = doc.layers.filter(
    (l) => l.parentId === (selected?.parentId || null),
  );
  const index = siblings.findIndex((l) => l.id === selection.id);
  const visible = doc.layers.filter((l) =>
    isTextureLayerVisible(doc, l.id),
  ).length;
  const groupBlocked = selected && isTextureSubtreeProtected(doc, selected.id);
  const isOpen = (layer) =>
    expanded[layer.id] ??
    (layer.kind === "folder" ||
      layer.kind === "decal" ||
      layer.decals.length > 0);
  useEffect(() => {
    if (!selection.id) return;
    const ids = textureAncestors(doc, selection.id).map((l) => l.id);
    setExpanded((old) => {
      if (!ids.some((id) => old[id] === false)) return old;
      return { ...old, ...Object.fromEntries(ids.map((id) => [id, true])) };
    });
  }, [selection.id, doc.layers.map((l) => `${l.id}:${l.parentId}`).join("|")]);
  useEffect(() => {
    const row = treeRef.current?.querySelector('[aria-selected="true"]');
    row?.scrollIntoView({ block: "nearest" });
  }, [selection.id, selection.mask, index, decalContext?.id]);
  useEffect(() => {
    if (addOpen) menuRef.current?.querySelector('[role="menuitem"]')?.focus();
  }, [addOpen]);
  const matches = doc.layers.filter(
    (l) =>
      (!query ||
        `${l.name} ${TEXTURE_KINDS[l.kind].label}`
          .toLowerCase()
          .includes(query.toLowerCase())) &&
      (filter === "all" ||
        isTextureLayerVisible(doc, l.id) === (filter === "visible")),
  );
  const matched = new Set(matches.map((l) => l.id));
  if (query || filter !== "all")
    for (const l of matches)
      for (const a of textureAncestors(doc, l.id)) matched.add(a.id);
  const layers = doc.layers.filter(
    (l) =>
      matched.has(l.id) &&
      (query ||
        filter !== "all" ||
        textureAncestors(doc, l.id).every((a) => isOpen(a))),
  );
  const choose = (id, mask = false) => {
    select(id, mask);
    setAddOpen(false);
  };
  function zone(e, layer) {
    if (!dragged.current) return "inside"; // Asset assignment remains unchanged.
    const rect = e.currentTarget.getBoundingClientRect();
    const y = (e.clientY - rect.top) / rect.height;
    return layer.kind === "folder"
      ? y < 0.25
        ? "before"
        : y > 0.75
          ? "after"
          : "inside"
      : y <= 0.5
        ? "before"
        : "after";
  }
  function drop(e, id, position = "before") {
    e.preventDefault();
    const asset = e.dataTransfer.getData("application/x-alloy-asset");
    if (asset) {
      setDropTarget(null);
      dragged.current = null;
      try {
        if (asset.length <= 1024)
          onAssetDrop(JSON.parse(asset), id || selection.id);
      } catch {
        /* Ignore foreign drag payloads. */
      }
      return;
    }
    const source = dragged.current;
    dragged.current = null;
    setDropTarget(null);
    if (source && source !== id) {
      const destination = textureLayerDropPosition(doc, source, id, position);
      if (destination && onMove) {
        onMove(source, destination.parentId, destination.beforeId);
        if (position === "inside" && id)
          setExpanded((old) => ({ ...old, [id]: true }));
      } else if (!onMove) {
        const target = doc.layers.find((l) => l.id === id);
        target?.kind === "folder"
          ? onFolderDrop(source, id)
          : onDrop(source, id);
      }
    }
  }
  function navigate(e, layer) {
    if (e.target.tagName === "INPUT") return;
    const index = layers.findIndex((l) => l.id === layer.id);
    if (["ArrowDown", "ArrowUp", "Home", "End"].includes(e.key)) {
      e.preventDefault();
      const target =
        layers[
          e.key === "Home"
            ? 0
            : e.key === "End"
              ? layers.length - 1
              : Math.min(
                  layers.length - 1,
                  Math.max(0, index + (e.key === "ArrowDown" ? 1 : -1)),
                )
        ];
      choose(target.id);
      e.currentTarget
        .closest('[role="tree"]')
        .querySelector(`[data-layer-select="${target.id}"]`)
        ?.focus();
    }
    if (e.key === "ArrowRight" || e.key === "ArrowLeft") {
      e.preventDefault();
      setExpanded((p) => ({ ...p, [layer.id]: e.key === "ArrowRight" }));
    }
  }
  return (
    <>
      <div className="tp-pane-heading">
        <h1>Layers</h1>
        <small>
          {doc.layers.length} / {TEXTURE_LAYER_LIMIT}
        </small>
        <button
          className="tp-round-icon tp-new-folder"
          aria-label="Add layer folder"
          title="New folder"
          disabled={doc.layers.length >= TEXTURE_LAYER_LIMIT}
          onClick={() => onAction("add", "folder")}
        >
          <FolderPlus size={16} />
        </button>
        <button
          className="tp-round-icon"
          aria-label="Add layer"
          ref={addRef}
          aria-expanded={addOpen}
          disabled={doc.layers.length >= TEXTURE_LAYER_LIMIT}
          onClick={() => setAddOpen(!addOpen)}
        >
          <Plus size={18} />
        </button>
      </div>
      <LayerStackFilters Query={query} OnQueryChange={setQuery} Filter={filter} OnFilterChange={setFilter} VisibleCount={visible} HiddenCount={doc.layers.length - visible}/>
      {addOpen && (
        <>
          <div
            className="tp-menu-dismiss"
            onPointerDown={() => setAddOpen(false)}
          />
          <div
            className="tp-add-menu"
            role="menu"
            ref={menuRef}
            aria-label="Add texture layer"
            onKeyDown={(e) => {
              if (e.key === "Escape") {
                e.stopPropagation();
                setAddOpen(false);
                addRef.current?.focus();
              } else if (
                ["ArrowUp", "ArrowDown", "Home", "End"].includes(e.key)
              ) {
                e.preventDefault();
                const entries = [
                  ...menuRef.current.querySelectorAll('[role="menuitem"]'),
                ];
                const at = entries.indexOf(document.activeElement);
                entries[
                  e.key === "Home"
                    ? 0
                    : e.key === "End"
                      ? entries.length - 1
                      : (at +
                          entries.length +
                          (e.key === "ArrowDown" ? 1 : -1)) %
                        entries.length
                ]?.focus();
              }
            }}
          >
            <small>ADD A LAYER</small>
            {Object.entries(TEXTURE_KINDS)
              .sort(([a], [b]) => {
                const order = [
                  "paint",
                  "fill",
                  "material",
                  "generator",
                  "decal",
                  "folder",
                ];
                return order.indexOf(a) - order.indexOf(b);
              })
              .map(([kind, info]) => (
                <button
                  key={kind}
                  role="menuitem"
                  onClick={() => {
                    onAction("add", kind);
                    setAddOpen(false);
                  }}
                >
                  <LayerKindIcon kind={kind} size={30} />
                  <span>
                    <strong>
                      {info.label}
                      {kind === "folder" ? "" : " layer"}
                    </strong>
                    <small>
                      {kind === "folder"
                        ? "Group layers · folder mask"
                        : kind === "decal"
                          ? "Surface stamps · multiple components"
                          : kind === "paint"
                            ? "Digital strokes · Paint / Erase"
                            : kind === "fill"
                              ? "Digital channel region fills"
                              : "Source and material setup"}
                    </small>
                  </span>
                  <Plus size={13} />
                </button>
              ))}
          </div>
        </>
      )}
      <div className="tp-layer-order">
        <span>TOP → BOTTOM</span>
        <span>{layers.length} shown</span>
      </div>
      <div
        className={`tp-layer-list ${dragged.current ? "is-layer-dragging" : ""}`}
        ref={treeRef}
        role="tree"
        aria-label="Texture layers"
        onDragOver={(e) => {
          if (
            dragged.current ||
            e.dataTransfer.types.includes("application/x-alloy-asset")
          )
            e.preventDefault();
        }}
        onDrop={(e) => drop(e, null)}
      >
        {layers.map((layer) => (
          <div
            key={layer.id}
            className="tp-layer-group"
            role="none"
            style={{ "--layer-depth": textureAncestors(doc, layer.id).length }}
          >
            <div
              role="treeitem"
              aria-selected={
                selection.id === layer.id &&
                !selection.mask &&
                !layer.decals.some((d) => d.id === decalContext?.id)
              }
              aria-expanded={isOpen(layer)}
              aria-level={textureAncestors(doc, layer.id).length + 1}
              className={`tp-layer-row ${selection.id === layer.id && !selection.mask ? "selected" : ""} ${!isTextureLayerVisible(doc, layer.id) ? "hidden-row" : ""} ${dropTarget?.id === layer.id ? `drop-target drop-${dropTarget.zone}` : ""}`}
              data-layer-id={layer.id}
              style={{
                "--layer-depth": textureAncestors(doc, layer.id).length,
              }}
              draggable={
                !isTextureSubtreeProtected(doc, layer.id) &&
                !query &&
                filter === "all"
              }
              onDragStart={(e) => {
                dragged.current = layer.id;
                setDropTarget({ id: layer.id, zone: "dragging" });
                e.dataTransfer.effectAllowed = "move";
                e.dataTransfer.setData(
                  "application/alloy-texture-layer",
                  layer.id,
                );
              }}
              onDragEnd={() => {
                dragged.current = null;
                setDropTarget(null);
              }}
              onDragOver={(e) => {
                if (
                  !dragged.current &&
                  !e.dataTransfer.types.includes("application/x-alloy-asset")
                )
                  return;
                e.preventDefault();
                e.stopPropagation();
                const position = zone(e, layer);
                const allowed = !dragged.current
                  ? !isTextureLayerLocked(doc, layer.id)
                  : !!textureLayerDropPosition(
                      doc,
                      dragged.current,
                      layer.id,
                      position,
                    );
                e.dataTransfer.dropEffect = allowed
                  ? dragged.current
                    ? "move"
                    : "copy"
                  : "none";
                setDropTarget(
                  allowed ? { id: layer.id, zone: position } : null,
                );
              }}
              onDragLeave={(e) => {
                if (!e.currentTarget.contains(e.relatedTarget))
                  setDropTarget(null);
              }}
              onDrop={(e) => {
                e.stopPropagation();
                drop(e, layer.id, zone(e, layer));
              }}
              onKeyDown={(e) => navigate(e, layer)}
            >
              <button
                className="tp-layer-drag"
                aria-label={`Drag ${layer.name}`}
                title="Drag to reorder. Folder: edges reorder, centre nests."
                disabled={
                  !!query ||
                  filter !== "all" ||
                  isTextureSubtreeProtected(doc, layer.id)
                }
                draggable={
                  !query &&
                  filter === "all" &&
                  !isTextureSubtreeProtected(doc, layer.id)
                }
                onClick={(e) => e.stopPropagation()}
              >
                <GripVertical size={12} />
              </button>
              <button
                className="tp-disclosure"
                aria-label={`${isOpen(layer) ? "Collapse" : "Expand"} ${layer.name}`}
                onClick={() =>
                  setExpanded((p) => ({ ...p, [layer.id]: !isOpen(layer) }))
                }
              >
                {isOpen(layer) ? (
                  <ChevronDown size={12} />
                ) : (
                  <ChevronRight size={12} />
                )}
              </button>
              <button
                className="tp-layer-select"
                data-layer-select={layer.id}
                aria-label={`Select ${layer.name}`}
                onClick={() => choose(layer.id)}
              >
                <LayerThumbnail
                  layer={layer}
                  thumbs={thumbs}
                  size={34}
                  open={isOpen(layer)}
                />
                <span>
                  <strong>{layer.name}</strong>
                  <small>
                    {layer.kind === "folder"
                      ? `${doc.layers.filter((l) => l.parentId === layer.id).length} ${doc.layers.filter((l) => l.parentId === layer.id).length === 1 ? "item" : "items"}`
                      : TEXTURE_KINDS[layer.kind].label}{" "}
                    · {layer.blend} · {layer.opacity}%
                  </small>
                </span>
              </button>
              <button
                className="tp-layer-eye"
                aria-label={`${layer.visible ? "Hide" : "Show"} ${layer.name}`}
                title={layer.visible ? "Hide layer" : "Show layer"}
                onClick={() => onVisibility(layer.id)}
              >
                {layer.visible ? <Eye size={13} /> : <EyeOff size={13} />}
              </button>
              <button
                className={`tp-layer-lock ${isTextureLayerLocked(doc, layer.id) ? "locked" : ""}`}
                title={
                  textureAncestors(doc, layer.id).some((l) => l.locked)
                    ? "Locked by parent folder"
                    : undefined
                }
                disabled={textureAncestors(doc, layer.id).some((l) => l.locked)}
                aria-label={`${layer.locked ? "Unlock" : "Lock"} ${layer.name}`}
                onClick={() => onLock(layer.id)}
              >
                {isTextureLayerLocked(doc, layer.id) ? (
                  <LockKeyhole size={12} />
                ) : (
                  <UnlockKeyhole size={12} />
                )}
              </button>
            </div>
            {isOpen(layer) &&
              (layer.decals || []).map((decal) => (
                <div
                  key={decal.id}
                  className={`tp-decal-tree-row ${selection.id === layer.id && decalContext?.id === decal.id ? "selected" : ""}`}
                  role="treeitem"
                  aria-level={textureAncestors(doc, layer.id).length + 2}
                  aria-selected={
                    selection.id === layer.id && decalContext?.id === decal.id
                  }
                >
                  <button
                    aria-label={`Select decal ${decal.name}`}
                    onClick={() => decalContext?.onSelect(decal.id, layer.id)}
                  >
                    <img
                      src={
                        doc.stamps.find((s) => s.id === decal.stampId)?.raster
                          .dataUrl
                      }
                      alt=""
                    />
                    <span>
                      {decal.name}
                      <small>
                        {decal.asMask ? "Stamp mask" : "Decal component"} ·{" "}
                        {Math.round(decal.opacity * 100)}%
                      </small>
                    </span>
                  </button>
                  <button
                    aria-label={`${decal.visible ? "Hide" : "Show"} surface decal ${decal.name}`}
                    disabled={isTextureLayerLocked(doc, layer.id)}
                    onClick={() => {
                      decalContext?.onSelect(decal.id, layer.id);
                      decalContext?.onPatchId(
                        decal.id,
                        { visible: !decal.visible },
                        null,
                        null,
                        "Toggle decal visibility",
                        layer.id,
                      );
                    }}
                  >
                    {decal.visible ? <Eye size={11} /> : <EyeOff size={11} />}
                  </button>
                </div>
              ))}
            {layer.mask && (
              <div
                className={`tp-mask-row ${selection.id === layer.id && selection.mask ? "selected" : ""}`}
                role="treeitem"
                aria-selected={selection.id === layer.id && selection.mask}
              >
                <button
                  onClick={() => choose(layer.id, true)}
                  aria-label={`Select mask of ${layer.name}`}
                >
                  <MaskIcon mask={layer.mask} size={20} />
                  <span>
                    {layer.mask.kind === "color"
                      ? "Colour mask"
                      : layer.mask.kind === "gradient"
                        ? "Gradient mask"
                        : "Layer mask"}{" "}
                    <small>
                      {layer.mask.kind === "gradient"
                        ? "Surface field"
                        : layer.mask.kind === "color"
                          ? layer.mask.color
                          : layer.mask.inverted
                            ? "Inverted"
                            : layer.mask.fill === 1
                              ? "White"
                              : "Black"}{" "}
                      · {layer.mask.strength}%
                    </small>
                  </span>
                </button>
                <button
                  className="tp-layer-eye"
                  disabled={isTextureLayerLocked(doc, layer.id)}
                  aria-label={`${layer.mask.enabled ? "Disable" : "Enable"} mask of ${layer.name}`}
                  onClick={() =>
                    onMask(layer.id, { enabled: !layer.mask.enabled })
                  }
                >
                  {layer.mask.enabled ? (
                    <Eye size={12} />
                  ) : (
                    <EyeOff size={12} />
                  )}
                </button>
              </div>
            )}
            {isOpen(layer) && layer.kind !== "folder" && (
              <div className="tp-layer-expanded" role="group">
                <small>
                  {layer.surfaceMaterial
                    ? layer.surfaceTarget
                      ? "PART MATERIAL"
                      : "MATERIAL"
                    : "CHANNEL TARGETS"}
                </small>
                <div>
                  {layer.surfaceMaterial && (
                    <em>
                      {layer.surfaceTarget
                        ? `${texturePart(layer.surfaceTarget.partId).name} · UDIM ${texturePart(layer.surfaceTarget.partId).tile}`
                        : "All teapot parts · live stack material"}
                    </em>
                  )}
                  {!layer.surfaceMaterial &&
                    TEXTURE_CHANNELS.filter((c) =>
                      layer.channels.includes(c.id),
                    ).map((c) => (
                      <span
                        key={c.id}
                        title={c.label}
                        style={{ "--channel-color": c.color }}
                      >
                        {c.short}
                      </span>
                    ))}
                  {!layer.surfaceMaterial && !layer.channels.length && (
                    <em>No channels enabled</em>
                  )}
                </div>
                <p>
                  {layer.surfaceMaterial ? (
                    layer.surfaceTarget ? (
                      "Native material · live on this part only"
                    ) : (
                      "Native material · covers lower layers"
                    )
                  ) : (
                    <>
                      {layer.kind === "paint"
                        ? layer.channels.some(
                            (id) => layer.channelSettings[id]?.texture,
                          )
                          ? "Stored texture source"
                          : "Empty texture source"
                        : "Unevaluated source"}{" "}
                      · setup only
                    </>
                  )}
                </p>
              </div>
            )}
          </div>
        ))}
        {!layers.length && (
          <div className="tp-empty">
            <Layers3 size={26} />
            <strong>
              {doc.layers.length ? "No matching layers" : "Start with a layer"}
            </strong>
            <p>
              {doc.layers.length
                ? "Clear the search or visibility filter."
                : "Add a layer to organize your surface."}
            </p>
            {doc.layers.length ? (
              <button
                onClick={() => {
                  setQuery("");
                  setFilter("all");
                }}
              >
                Clear filters
              </button>
            ) : (
              <button onClick={() => setAddOpen(true)}>
                <Plus size={13} /> Add layer
              </button>
            )}
          </div>
        )}
        {!query && filter === "all" && layers.length > 0 && (
          <div
            className={`tp-bottom-drop ${dropTarget === "bottom" ? "active" : ""}`}
            onDragOver={(e) => {
              if (dragged.current) {
                e.preventDefault();
                setDropTarget("bottom");
              }
            }}
            onDrop={(e) => {
              e.stopPropagation();
              drop(e, null);
            }}
          >
            Drop here to move to the root
          </div>
        )}
      </div>
      <div
        className="tp-layer-actions"
        role="toolbar"
        aria-label="Layer actions"
      >
        <button
          aria-label="Move layer up"
          title="Move up"
          disabled={disabled || groupBlocked || index <= 0}
          onClick={() => onAction("up")}
        >
          <ArrowUp size={15} />
        </button>
        <button
          aria-label="Move layer down"
          title="Move down"
          disabled={disabled || groupBlocked || index >= siblings.length - 1}
          onClick={() => onAction("down")}
        >
          <ArrowDown size={15} />
        </button>
        <span />
        <button
          aria-label="Duplicate layer"
          title="Duplicate · Ctrl/⌘ D"
          disabled={
            disabled || groupBlocked || doc.layers.length >= TEXTURE_LAYER_LIMIT
          }
          onClick={() => onAction("duplicate")}
        >
          <Copy size={14} />
        </button>
        <button
          aria-label="Add layer mask"
          title="Add white mask"
          disabled={disabled || !!selected?.mask}
          onClick={() => onAction("mask")}
        >
          <span className="tp-mask-tool" />
        </button>
        <button
          aria-label="Add colour mask"
          title="Colour selection mask"
          disabled={disabled || !!selected?.mask}
          onClick={() => onAction("color-mask")}
        >
          <Palette size={14} />
        </button>
        <button
          aria-label="Delete layer"
          title={
            selected?.kind === "folder"
              ? "Delete folder and its contents"
              : "Delete layer"
          }
          disabled={disabled || groupBlocked}
          onClick={() => onAction("remove")}
        >
          <Trash2 size={14} />
        </button>
      </div>
      <div
        className="tp-system-base"
        aria-label="System Layer 0"
        data-base-active={stackPlan.fallbackParts.length > 0}
      >
        <button
          type="button"
          aria-label="Inspect locked system Layer 0"
          aria-expanded={systemOpen}
          onClick={() => setSystemOpen(!systemOpen)}
        >
          <i className="tp-system-white" />
          <LockKeyhole size={12} />
          <span>Layer 0 · Mesh base</span>
          <small>{stackPlan.fallbackParts.length ? "Auto" : "Replaced"}</small>
          <ChevronDown size={12} />
        </button>
        {systemOpen && (
          <div className="tp-system-base-info">
            <strong>Locked · hidden from editable stack</strong>
            <p>
              White fallback
              {stackPlan.fallbackParts.length
                ? ` for ${stackPlan.fallbackParts.map((id) => texturePart(id).name).join(", ")}`
                : " automatically replaced by opaque materials"}
              . Masks, hidden layers or zero opacity restore it where needed.
            </p>
            <p>
              {doc.meshMaps
                ? Object.keys(doc.meshMaps.maps)
                    .map((key) =>
                      key === "occlusion"
                        ? "AO · indirect-light multiply"
                        : key === "normal"
                          ? "Baked normal · RNM"
                          : "Bevel normal · RNM",
                    )
                    .join(" · ")
                : "No baked mesh maps attached. Bake the built-in teapot to attach AO / normal support."}
            </p>
            {onBake && (
              <button type="button" className="tp-system-bake" onClick={onBake}>
                Bake matching teapot support · 512
              </button>
            )}
            <p>
              Mesh support survives material replacement. This system row cannot
              be painted, moved, deleted or unlocked.
            </p>
          </div>
        )}
      </div>
      <footer className="tp-pane-footer">
        <span>
          <small>LAYERS</small>
          <b>{doc.layers.length}</b>
        </span>
        <span>
          <small>VISIBLE</small>
          <b>{visible}</b>
        </span>
        <span>
          <small>CONTENT</small>
          <b>{doc.layers.reduce((n, l) => n + l.decals.length, 0)} decals</b>
        </span>
      </footer>
    </>
  );
}
