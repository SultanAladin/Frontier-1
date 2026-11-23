import LayerStackPanel from "./LayerStackPanel.jsx";
import useDigitalPainting from "./useDigitalPainting.js";
import DigitalPaintMenu, { DigitalPaintToolbar } from "./DigitalPaintMenu.jsx";
import { DIGITAL_BRUSH_PRESETS } from "./digitalBrush.js";
import useMaskEditing from "./useMaskEditing.js";
import { evaluateTextureStack } from "./textureStackEvaluation.js";
import TextureMaskToolbar from "./TextureMaskToolbar.jsx";
import { maskHasPaint } from "./textureMaskPaint.js";
import TextureUVView from "./TextureUVView.jsx";
import {
  createTextureTeapotGeometry,
  TEXTURE_PARTS,
  texturePart,
  readAssetDrop,
} from "./textureSurfaceModel.js";
import useDecalEditing from "./useDecalEditing.js";
import {
  GRADIENT_POINT_LIMIT,
  normalizePointGradient,
} from "./pointGradient.js";
import "./pointGradient.css";
import TextureHistoryPanel from "./TextureHistoryPanel.jsx";
import { normalizeSourceStroke } from "./textureStrokes.js";
import AssetContentBrowser from "./AssetContentBrowser.jsx";
import {
  paintTool,
  normalizePaintSettings,
  selectPaintInstrument,
  PAINT_TOOLS,
} from "./paintToolCatalogue.js";
import {
  normalizeChannelSettings,
  normalizeGenerator,
} from "./paintChannelModel.js";
import React, { useEffect, useId, useRef, useState, useMemo } from "react";
import {
  Box,
  Brush,
  Check,
  FileJson,
  FolderOpen,
  Grid2X2,
  History,
  Layers3,
  Maximize2,
  Minus,
  Plus,
  Redo2,
  RotateCcw,
  Save,
  SlidersHorizontal,
  Stamp,
  Move3D,
  MousePointer2,
  Undo2,
  X,
} from "lucide-react";
import Viewport from "./Viewport.jsx";
import { materials, normalizeMaterial } from "./materials.js";
import useTextureDocument from "./useTextureDocument.js";
import {
  duplicateTextureLayer,
  insertTextureLayer,
  parseTextureDocument,
  patchTextureLayer,
  removeTextureLayer,
  reorderTextureLayer,
  moveTextureLayer,
  ungroupTextureFolder,
  textureSubtree,
  isTextureLayerLocked,
  isTextureLayerVisible,
  assignTexturePartMaterial,
  isTexturePartLocked,
  textureAncestors,
  TEXTURE_FILE_LIMIT,
} from "./textureDocument.js";
import TextureLayerStack, { DockTab } from "./TextureLayerStack.jsx";
import TextureLayerInspector from "./TextureLayerInspector.jsx";
import { StudioHeader } from "./StudioUI.jsx";
import "./textureStudio.css";

const PREVIEW_MATERIAL = normalizeMaterial({
  id: "texture-system-white",
  name: "Layer 0 · white mesh base",
  type: 1,
  color: "#ffffff",
  metalness: 0,
  roughness: 0.55,
  flakes: 0,
  coat: 0,
  depth: 0,
  wear: 0,
});
function readLayout() {
  try {
    const layout = JSON.parse(localStorage.getItem("alloy-texture-layout-v1"));
    return {
      layers: Math.min(360, Math.max(240, Number(layout.layers) || 278)),
      inspector: Math.min(440, Math.max(290, Number(layout.inspector) || 338)),
    };
  } catch {
    return { layers: 278, inspector: 338 };
  }
}
function ResizeDock({ name, width, min, max, onChange, edge = "right" }) {
  const drag = useRef(null);
  return (
    <div
      className="tp-dock-divider"
      role="separator"
      aria-label={`Resize ${name}`}
      aria-orientation="vertical"
      aria-valuemin={min}
      aria-valuemax={max}
      aria-valuenow={width}
      tabIndex={0}
      onPointerDown={(e) => {
        if (e.button !== 0) return;
        e.preventDefault();
        drag.current = { x: e.clientX, width };
        e.currentTarget.setPointerCapture(e.pointerId);
      }}
      onPointerMove={(e) => {
        if (drag.current)
          onChange(
            drag.current.width +
              (e.clientX - drag.current.x) * (edge === "left" ? -1 : 1),
          );
      }}
      onPointerUp={() => {
        drag.current = null;
      }}
      onPointerCancel={() => {
        drag.current = null;
      }}
      onLostPointerCapture={() => {
        drag.current = null;
      }}
      onKeyDown={(e) => {
        if (["ArrowLeft", "ArrowRight", "Home", "End"].includes(e.key)) {
          e.preventDefault();
          onChange(
            e.key === "Home"
              ? min
              : e.key === "End"
                ? max
                : width +
                  (e.key === "ArrowRight" ? 10 : -10) *
                    (edge === "left" ? -1 : 1),
          );
        }
      }}
    />
  );
}
export default function TexturePaintStudio({
  onClose,
  onWorkspace,
  materialWorkspace,
  initialStamp,
  onStampConsumed,
  onEditStamp,
}) {
  const history = useTextureDocument();
  const digitalPaintingRef = useRef(null);
  const { doc, selection, select } = history;
  const layer = doc.layers.find((l) => l.id === selection.id);
  const disabled = !layer || isTextureLayerLocked(doc, layer.id);
  const index = doc.layers.findIndex((l) => l.id === selection.id);
  const [layout, setLayout] = useState(readLayout);
  const [mobilePanel, setMobilePanel] = useState("layers");
  const [rightPanel, setRightPanel] = useState("inspector"),
    panelId = useId();
  const [viewMode, setViewMode] = useState("shaded"),
    [uvSource, setUVSource] = useState(false),
    [selectedPart, setSelectedPart] = useState(null),
    [dropPart, setDropPart] = useState(null);
  const [surfaceGeometry] = useState(() => createTextureTeapotGeometry(20)),
    surfaceAPI = useRef(null);
  const wireframe = viewMode === "wireframe";
  useEffect(() => () => surfaceGeometry.dispose(), [surfaceGeometry]);
  const [grid, setGrid] = useState(true);
  const [reset3D, setReset3D] = useState(0),
    [resetUV, setResetUV] = useState(0),
    [zoom3D, setZoom3D] = useState(null),
    [zoomUV, setZoomUV] = useState(null),
    [zoomLevel3D, setZoomLevel3D] = useState(100),
    [zoomLevelUV, setZoomLevelUV] = useState(100);
  const zoomLevel = viewMode === "uv" ? zoomLevelUV : zoomLevel3D;
  const setResetToken = viewMode === "uv" ? setResetUV : setReset3D;
  const setZoom = viewMode === "uv" ? setZoomUV : setZoom3D;
  const [ready, setReady] = useState(false);
  const [toast, setToast] = useState("");
  const [help, setHelp] = useState(false);
  const [gradientTarget, setGradientTarget] = useState(null),
    [gradientDraft, setGradientDraft] = useState(null),
    gradientGesture = useRef(null),
    gradientDraftRef = useRef(null);
  gradientDraftRef.current = gradientDraft;
  function readGradient(current, target) {
    const owner = current.layers.find((l) => l.id === target?.layerId);
    if (!owner) return null;
    return target.mask
      ? owner.mask?.kind === "gradient"
        ? owner.mask.gradient
        : null
      : owner.channels.includes(target.channelId) &&
          owner.channelSettings[target.channelId]?.mode === "gradient"
        ? owner.channelSettings[target.channelId].gradient
        : null;
  }
  function writeGradient(target, value, { key, expected, label } = {}) {
    const current = history.docRef.current,
      owner = current.layers.find((l) => l.id === target?.layerId),
      before = readGradient(current, target);
    if (
      !owner ||
      !before ||
      isTextureLayerLocked(current, owner.id) ||
      owner.id !== selection.id ||
      target.mask !== !!selection.mask
    )
      return false;
    if (expected && JSON.stringify(before) !== expected) return false;
    const gradient = normalizePointGradient(value, !!target.mask),
      changes = target.mask
        ? { mask: { ...owner.mask, gradient } }
        : {
            channelSettings: {
              ...owner.channelSettings,
              [target.channelId]: {
                ...owner.channelSettings[target.channelId],
                gradient,
              },
            },
          };
    return history.commit(patchTextureLayer(current, owner.id, changes), {
      key,
      label,
    });
  }
  function activateGradient(target) {
    digitalPaintingRef.current?.deactivate();
    maskEditing.change({ tool: "navigate" });
    decalEditing.deactivate();
    gradientGesture.current = null;
    setGradientDraft(null);
    setGradientTarget((old) => ({ ...old, ...target }));
  }
  useEffect(() => {
    setGradientTarget(null);
    setGradientDraft(null);
    gradientGesture.current = null;
  }, [selection.id, selection.mask]);
  const storedGradient = readGradient(doc, gradientTarget),
    activeGradient =
      gradientTarget?.active &&
      storedGradient &&
      gradientTarget.layerId === selection.id &&
      !!gradientTarget.mask === !!selection.mask &&
      !disabled;
  useEffect(() => {
    if (!activeGradient) {
      gradientGesture.current = null;
      setGradientDraft(null);
    }
  }, [!!activeGradient]);
  const liveGradient =
    activeGradient &&
    gradientDraft?.key ===
      `${gradientTarget.layerId}-${gradientTarget.mask ? "mask" : gradientTarget.channelId}`
      ? gradientDraft.value
      : storedGradient;
  const gradientContext = gradientTarget
    ? { ...gradientTarget, active: !!activeGradient }
    : null;
  const pointEditor = activeGradient
    ? {
        ...gradientTarget,
        active: true,
        gradient: liveGradient,
        onSelect: (id) => setGradientTarget((old) => ({ ...old, pointId: id })),
        onPlace: (position) => {
          const base = readGradient(history.docRef.current, gradientTarget);
          if (!base || base.points.length >= GRADIENT_POINT_LIMIT) return;
          const id = crypto.randomUUID(),
            active =
              base.points.find((p) => p.id === gradientTarget.pointId) ||
              base.points[0];
          if (
            writeGradient(
              gradientTarget,
              {
                ...base,
                points: [
                  ...base.points,
                  {
                    id,
                    position,
                    color: active?.color || doc.painting.color,
                    radius: active?.radius ?? 0.8,
                    weight: 1,
                  },
                ],
              },
              { label: "Place gradient point" },
            )
          )
            setGradientTarget((old) => ({
              ...old,
              placing: false,
              pointId: id,
            }));
        },
        onStart: (id) => {
          const before = readGradient(history.docRef.current, gradientTarget);
          if (!before) return;
          gradientGesture.current = {
            target: gradientTarget,
            before,
            expected: JSON.stringify(before),
            key: `${gradientTarget.layerId}-${gradientTarget.mask ? "mask" : gradientTarget.channelId}`,
          };
          setGradientDraft({ key: gradientGesture.current.key, value: before });
        },
        onMove: (id, position) => {
          const gesture = gradientGesture.current;
          if (!gesture) return;
          setGradientDraft((old) => ({
            key: gesture.key,
            value: {
              ...(old?.value || gesture.before),
              points: (old?.value || gesture.before).points.map((p) =>
                p.id === id ? { ...p, position } : p,
              ),
            },
          }));
        },
        onEnd: (cancelled) => {
          const gesture = gradientGesture.current,
            value = gradientDraftRef.current;
          gradientGesture.current = null;
          setGradientDraft(null);
          if (!cancelled && gesture && value)
            writeGradient(gesture.target, value.value, {
              expected: gesture.expected,
              label: "Move gradient point",
            });
        },
        onKeyMove: (id, key, shift) => {
          const base = readGradient(history.docRef.current, gradientTarget),
            step = shift ? 0.1 : 0.02;
          if (!base) return;
          writeGradient(
            gradientTarget,
            {
              ...base,
              points: base.points.map((p) =>
                p.id === id
                  ? {
                      ...p,
                      position: p.position.map((v, i) =>
                        Math.min(
                          2,
                          Math.max(
                            -2,
                            v +
                              (i === 0
                                ? key === "ArrowRight"
                                  ? step
                                  : key === "ArrowLeft"
                                    ? -step
                                    : 0
                                : i === 1
                                  ? key === "ArrowUp"
                                    ? step
                                    : key === "ArrowDown"
                                      ? -step
                                      : 0
                                  : 0),
                          ),
                        ),
                      ),
                    }
                  : p,
              ),
            },
            { key: `gradient-key-${id}`, label: "Move gradient point" },
          );
        },
        onDelete: (id) => {
          const base = readGradient(history.docRef.current, gradientTarget);
          if (base)
            writeGradient(
              gradientTarget,
              { ...base, points: base.points.filter((p) => p.id !== id) },
              { label: "Remove gradient point" },
            );
        },
      }
    : null;
  // This is a narrow selected-field preview, not a compositor for the whole stack.
  const sourceLayer =
    layer?.kind === "folder"
      ? textureSubtree(doc, layer.id).find(
          (l) => l.kind !== "folder" && isTextureLayerVisible(doc, l.id),
        ) || layer
      : layer;
  const decalEditing = useDecalEditing(history, {
    initialStamp,
    onStampConsumed,
    onEditStamp,
    previewLayerId: sourceLayer?.surfaceMaterial ? null : sourceLayer?.id,
    space: viewMode === "uv" ? "uv" : "object-sphere",
    placementAvailable: !(viewMode === "uv" && uvSource),
    onRequestSpace: (space) => {
      if (space === "uv") setUVSource(false);
      chooseView(
        space === "uv" ? "uv" : viewMode === "uv" ? "shaded" : viewMode,
      );
    },
    onDeactivateGradient: () => {
      gradientGesture.current = null;
      setGradientDraft(null);
      setGradientTarget((old) => (old ? { ...old, active: false } : null));
    },
  });
  const selectedDecal = layer?.decals.find(
      (d) => d.id === decalEditing.context.id,
    ),
    [maskAtlas, setMaskAtlas] = useState(null);
  const [surfaceAtlas, setSurfaceAtlas] = useState(null);
  const maskEditing = useMaskEditing(history, {
    geometry: surfaceGeometry,
    viewMode,
    source: uvSource,
    suspended: decalEditing.editor.active
      ? "decal"
      : activeGradient
        ? "gradient"
        : false,
    decalId: selectedDecal?.asMask ? selectedDecal.id : null,
    onActivate: () => {
      digitalPaintingRef.current?.deactivate();
      decalEditing.deactivate();
      gradientGesture.current = null;
      setGradientDraft(null);
      setGradientTarget((old) => (old ? { ...old, active: false } : null));
    },
  });
  const digitalPainting = useDigitalPainting(history, {
    geometry: surfaceGeometry,
    viewMode,
    source: uvSource,
    suspended: decalEditing.editor.active || !!activeGradient,
    maskRegion: maskEditing.region,
    maskValue: maskEditing.state.value,
    onActivate: () => {
      maskEditing.cancel();
      decalEditing.deactivate();
      gradientGesture.current = null;
      setGradientDraft(null);
      setGradientTarget((old) => (old ? { ...old, active: false } : null));
    },
  });
  digitalPaintingRef.current = digitalPainting;
  const surfaceEditing = digitalPainting.active
    ? digitalPainting.editor
    : maskEditing.editor;
  const maskPreview = useMemo(
    () => ({
      ...maskEditing.preview,
      ...(digitalPainting.draft?.mask
        ? { mask: digitalPainting.draft.mask }
        : {}),
      ...(activeGradient && gradientTarget.mask && selection.mask
        ? { mask: { ...layer.mask, gradient: liveGradient } }
        : {}),
      doc: decalEditing.document,
    }),
    [
      maskEditing.preview,
      digitalPainting.draft,
      activeGradient,
      gradientTarget,
      liveGradient,
      selection.mask,
      layer?.mask,
      decalEditing.document,
    ],
  );
  const selectLayer = (id, mask = false) => {
    decalEditing.clearSelection();
    select(id, mask);
  };
  const gradientChannel =
    sourceLayer?.channels?.includes("baseColor") &&
    sourceLayer.channelSettings.baseColor.mode === "gradient"
      ? "baseColor"
      : null;
  const fillGradient = gradientChannel
    ? sourceLayer.channelSettings[gradientChannel].gradient
    : null;
  const maskOwners = sourceLayer
      ? [...textureAncestors(doc, sourceLayer.id), sourceLayer]
      : [],
    previewMasks = maskOwners
      .map((l) => l.mask)
      .filter(
        (m) => m && !maskHasPaint(m) && ["fill", "gradient"].includes(m.kind),
      );
  let surfaceGradient =
    sourceLayer &&
    !sourceLayer.surfaceMaterial &&
    (fillGradient ||
      maskOwners.some((l) => l.mask?.kind === "gradient") ||
      (sourceLayer.kind !== "decal" &&
        sourceLayer.decals.some((d) => d.asMask && d.visible && d.opacity > 0)))
      ? {
          fill: fillGradient,
          value: sourceLayer.channelSettings?.baseColor?.value || "#b87333",
          masks: previewMasks,
          opacity: maskOwners.reduce((a, l) => (a * l.opacity) / 100, 1),
        }
      : null;
  // The legacy selected-source preview cannot override authored stack materials.
  if (evaluateTextureStack(doc).surfaces.length) surfaceGradient = null;
  if (surfaceGradient && !isTextureLayerVisible(doc, sourceLayer.id))
    surfaceGradient = null;
  if (surfaceGradient && activeGradient && gradientDraft) {
    if (gradientTarget.mask)
      surfaceGradient = {
        ...surfaceGradient,
        masks: maskOwners
          .filter(
            (l) =>
              l.mask &&
              !maskHasPaint(l.mask) &&
              ["fill", "gradient"].includes(l.mask.kind),
          )
          .map((l) =>
            l.id === gradientTarget.layerId
              ? { ...l.mask, gradient: liveGradient }
              : l.mask,
          ),
      };
    else surfaceGradient = { ...surfaceGradient, fill: liveGradient };
  }

  const [paintMenuCatalogue, setPaintMenuCatalogue] = useState(false);
  const [paintMenuOpen, setPaintMenuOpen] = useState(false),
    [browserOpen, setBrowserOpen] = useState(false);
  const toolButton = useRef(null);
  const viewportRef = useRef(null),
    layoutRef = useRef(null),
    fileRef = useRef(null),
    mounted = useRef(true);
  useEffect(
    () => () => {
      mounted.current = false;
    },
    [],
  );
  useEffect(() => {
    if (toast) {
      const timer = setTimeout(() => setToast(""), 2800);
      return () => clearTimeout(timer);
    }
  }, [toast]);
  useEffect(() => {
    try {
      localStorage.setItem("alloy-texture-layout-v1", JSON.stringify(layout));
    } catch {
      /* Dock sizing is optional. */
    }
  }, [layout]);
  function resize(side, width) {
    const other = side === "layers" ? layout.inspector : layout.layers;
    const min = side === "layers" ? 240 : 290;
    const max = Math.max(
      min,
      Math.min(
        side === "layers" ? 360 : 440,
        (layoutRef.current?.clientWidth || 1440) - other - 320,
      ),
    );
    setLayout((p) => ({
      ...p,
      [side]: Math.round(Math.max(min, Math.min(max, width))),
    }));
  }
  function patch(id, changes, { key, discrete = false } = {}) {
    const fields = Object.keys(changes);
    // Coalesce continuous controls, never discrete target toggles / clear-all.
    const continuous = fields.every((field) =>
      ["name", "opacity", "channelSettings", "surfaceMaterial"].includes(field),
    );
    history.commit(patchTextureLayer(history.docRef.current, id, changes), {
      key: discrete
        ? undefined
        : key || (continuous ? `layer-${id}-${fields.join("-")}` : undefined),
    });
  }
  function painting(next) {
    const current = history.docRef.current,
      value = normalizePaintSettings(next);
    const keys = Object.keys(value.params).filter(
      (k) => value.params[k] !== current.painting.params[k],
    );
    if (value.color !== current.painting.color) keys.push("color");
    history.commit(
      { ...current, painting: value },
      {
        key:
          value.toolId === current.painting.toolId
            ? `paint-${value.toolId}-${keys.join("-")}`
            : undefined,
      },
    );
  }
  function sourceStroke(stroke, texture, expectedSource) {
    const current = history.docRef.current,
      owner = current.layers.find((l) => l.id === stroke.layerId),
      record = normalizeSourceStroke(stroke);
    if (
      !record ||
      !owner ||
      owner.kind === "folder" ||
      isTextureLayerLocked(current, owner.id) ||
      selection.id !== owner.id ||
      selection.mask ||
      !owner.channels.includes(stroke.channelId)
    )
      return false;
    const settings = owner.channelSettings[stroke.channelId];
    if (
      settings.mode !== "texture" ||
      settings.texture?.dataUrl !== expectedSource
    )
      return false;
    const next = patchTextureLayer(current, owner.id, {
      channelSettings: {
        ...owner.channelSettings,
        [stroke.channelId]: { ...settings, texture },
      },
    });
    return history.commit(
      { ...next, lastStroke: record, lastDigitalStroke: null },
      { selection: { id: owner.id } },
    );
  }
  function openDigitalMenu() {
    surfaceAPI.current?.navigate();
    digitalPainting.cancel();
    maskEditing.cancel();
    setPaintMenuCatalogue(false);
    setPaintMenuOpen(true);
  }
  function openSourceTools() {
    surfaceAPI.current?.navigate();
    digitalPainting.cancel();
    maskEditing.cancel();
    setPaintMenuCatalogue(true);
    setPaintMenuOpen(true);
  }
  function closePaintMenu() {
    setPaintMenuOpen(false);
    toolButton.current?.focus();
  }
  function chooseView(mode) {
    if (mode !== viewMode) decalEditing.deactivate();
    gradientGesture.current = null;
    setGradientDraft(null);
    setGradientTarget((old) => (old ? { ...old, active: false } : null));
    setDropPart(null);
    surfaceAPI.current?.hover(selectedPart);
    maskEditing.cancel();
    digitalPainting.cancel();
    surfaceAPI.current?.navigate();
    setViewMode(mode);
  }
  function selectPart(id) {
    setSelectedPart(id);
    surfaceAPI.current?.hover(id);
  }
  function dropOnSurface(asset, hit) {
    setDropPart(null);
    surfaceAPI.current?.hover(hit?.target?.partId || selectedPart);
    if (!hit?.target || !asset || !["material", "stamp"].includes(asset.type))
      return false;
    const current = history.docRef.current,
      part = texturePart(hit.target.partId);
    if (isTexturePartLocked(current, part.id)) {
      history.setError(
        `${part.name} has a locked material layer. Unlock it before dropping assets onto this part.`,
      );
      return false;
    }
    if (asset.type === "stamp") {
      const added = decalEditing.dropStamp(asset.id, hit);
      if (added) {
        selectPart(part.id);
        setToast(
          `Decal placed on ${part.name} · ${hit.space === "uv" ? "UV" : "surface"} coordinates`,
        );
      }
      return added;
    }
    const definition = materialWorkspace.materials.find(
      (m) => m.id === asset.id,
    );
    if (!definition) {
      history.setError("That material is not in the shared library.");
      return false;
    }
    const snapshot =
      materialWorkspace.params?.id === asset.id
        ? materialWorkspace.params
        : definition;
    try {
      const result = assignTexturePartMaterial(
        current,
        hit.target,
        snapshot,
        definition,
      );
      if (
        !history.commit(result.doc, {
          selection: { id: result.id },
          label: `Assign ${definition.name} to ${part.name}`,
        })
      )
        return false;
      decalEditing.deactivate();
      selectPart(part.id);
      setRightPanel("inspector");
      setToast(
        `${definition.name} → ${part.name} only · other parts unchanged`,
      );
      return true;
    } catch (error) {
      history.setError(error.message);
      return false;
    }
  }
  function sceneDragOver(e) {
    if (
      viewMode === "uv" ||
      !Array.from(e.dataTransfer.types).includes("application/x-alloy-asset")
    )
      return;
    e.preventDefault();
    e.dataTransfer.dropEffect = "copy";
    const hit = surfaceAPI.current?.pick(e.clientX, e.clientY),
      partId = hit?.target?.partId || null;
    setDropPart(partId);
    surfaceAPI.current?.hover(partId);
  }
  function assignAsset(asset, targetId = selection.id) {
    let current = history.docRef.current,
      target = current.layers.find((l) => l.id === targetId);
    if (!target || isTextureLayerLocked(current, target.id)) {
      history.setError("Select an unlocked layer before assigning an asset.");
      return;
    }
    if (asset.type === "stamp") {
      const source = decalEditing.context.presets.find(
        (p) => p.id === asset.id,
      );
      if (!source) {
        history.setError("That stamp is not in the shared library.");
        return;
      }
      if (target.kind === "folder") {
        try {
          const added = insertTextureLayer(current, "decal", target.id);
          history.commit(added.doc, {
            selection: { id: added.id },
            label: "Add decal layer to folder",
          });
          targetId = added.id;
        } catch (e) {
          history.setError(e.message);
          return;
        }
      }
      decalEditing.context.onPresetDrop(source.id, targetId);
      setToast(
        "Stamp preset selected. Click the teapot surface to create its decal component.",
      );
      return;
    }
    const definition =
      asset.type === "material"
        ? materialWorkspace.materials.find((a) => a.id === asset.id)
        : asset.type === "brush"
          ? PAINT_TOOLS.find((a) => a.id === asset.id)
          : asset.type === "generator"
            ? normalizeGenerator(asset)
            : null;
    if (!definition) {
      history.setError("That asset is not in the shared library.");
      return;
    }
    // Dropping on a folder creates a real child record, never an asset on
    // the group itself. One history commit covers insertion and assignment.
    if (target.kind === "folder") {
      try {
        const added = insertTextureLayer(
          current,
          asset.type === "brush" ? "paint" : asset.type,
          target.id,
        );
        current = added.doc;
        targetId = added.id;
        target = current.layers.find((l) => l.id === targetId);
      } catch (e) {
        history.setError(e.message);
        return;
      }
    }
    const changes = {
      sourceAsset: {
        type: asset.type,
        id: definition.id,
        name:
          asset.type === "brush"
            ? definition.label
            : asset.type === "material"
              ? definition.name
              : asset.name,
      },
    };
    if (asset.type === "material" && target.kind === "material")
      changes.surfaceMaterial =
        materialWorkspace.params?.id === asset.id
          ? materialWorkspace.params
          : definition;
    if (asset.type === "generator") {
      const channel =
          target.channels.find((id) => id !== "normal") || "baseColor",
        settings = normalizeChannelSettings(target.channelSettings);
      changes.channels = target.channels.includes(channel)
        ? target.channels
        : [...target.channels, channel];
      changes.channelSettings = {
        ...settings,
        [channel]: {
          ...settings[channel],
          mode: "generator",
          generator: definition,
        },
      };
    }
    const next = patchTextureLayer(current, targetId, changes);
    if (asset.type === "brush")
      next.painting = selectPaintInstrument(current.painting, asset.id);
    history.commit(next, { selection: { id: targetId } });
  }
  function mask(id, changes, options = {}) {
    const current = history.docRef.current,
      owner = current.layers.find((l) => l.id === id);
    if (!owner) return;
    if (changes === null) {
      selectLayer(id, true);
      setMobilePanel("inspector");
      setRightPanel("inspector");
      return;
    }
    history.commit(
      patchTextureLayer(current, id, {
        mask: {
          enabled: true,
          fill: 1,
          inverted: false,
          strength: 100,
          ...owner.mask,
          ...changes,
        },
      }),
      {
        key:
          options.key ||
          (owner.mask &&
          Object.keys(changes).length === 1 &&
          ["strength", "tolerance", "softness", "color"].includes(
            Object.keys(changes)[0],
          )
            ? `mask-${id}-${Object.keys(changes)[0]}`
            : undefined),
        selection: { id, mask: true },
      },
    );
  }
  function moveLayer(id, parentId, beforeId = null) {
    try {
      history.commit(
        moveTextureLayer(history.docRef.current, id, parentId, beforeId),
      );
    } catch (e) {
      history.setError(e.message);
    }
  }
  function dropLayer(id, before) {
    try {
      history.commit(reorderTextureLayer(history.docRef.current, id, before));
    } catch (e) {
      history.setError(e.message);
    }
  }
  function action(verb, kind) {
    const current = history.docRef.current,
      selected = current.layers.find((l) => l.id === selection.id);
    try {
      if (verb === "add") {
        const result = insertTextureLayer(current, kind, selection.id);
        history.commit(result.doc, { selection: { id: result.id } });
      } else if (selected && !isTextureLayerLocked(current, selected.id)) {
        if (verb === "duplicate" || verb === "remove") {
          const children = textureSubtree(current, selected.id).length - 1;
          if (
            verb === "remove" &&
            selected.kind === "folder" &&
            children &&
            !window.confirm(
              `Delete “${selected.name}” and its ${children} descendant layers? This can be undone.`,
            )
          )
            return;
          const result =
            verb === "duplicate"
              ? duplicateTextureLayer(current, selected.id)
              : removeTextureLayer(current, selected.id);
          history.commit(result.doc, { selection: { id: result.id } });
        } else if (verb === "up" || verb === "down") {
          const siblings = current.layers.filter(
              (l) => l.parentId === selected.parentId,
            ),
            at = siblings.indexOf(selected);
          if (verb === "up" && at > 0)
            moveLayer(selected.id, selected.parentId, siblings[at - 1].id);
          if (verb === "down" && at < siblings.length - 1)
            moveLayer(
              selected.id,
              selected.parentId,
              siblings[at + 2]?.id || null,
            );
        } else if ((verb === "mask" || verb === "color-mask") && !selected.mask)
          mask(selected.id, { kind: verb === "color-mask" ? "color" : "fill" });
        else if (verb === "ungroup" && selected.kind === "folder") {
          if (
            (selected.mask ||
              selected.opacity !== 100 ||
              selected.blend !== "Normal") &&
            !window.confirm(
              "Ungroup this folder? Its blend, opacity and mask are removed; child layers are kept. This can be undone.",
            )
          )
            return;
          const next = ungroupTextureFolder(current, selected.id),
            child = current.layers.find((l) => l.parentId === selected.id);
          history.commit(next, {
            selection: {
              id:
                next !== current
                  ? child?.id || selected.parentId || next.layers[0]?.id
                  : selected.id,
            },
          });
        }
      }
    } catch (e) {
      history.setError(e.message);
    }
  }
  function saveProject() {
    if (document.activeElement?.matches('input[aria-label="Layer name"]'))
      document.activeElement.blur();
    history.save();
    const current = history.docRef.current;
    const url = URL.createObjectURL(
      new Blob([JSON.stringify(current, null, 2)], {
        type: "application/json",
      }),
    );
    const link = document.createElement("a");
    link.href = url;
    link.download = `${current.name.replace(/[^a-zA-Z0-9_-]+/g, "-") || "Surface"}.texture.json`;
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    setToast("Layer project saved · sources included, no composited maps");
  }
  async function openProject(e) {
    const file = e.target.files[0];
    e.target.value = "";
    if (!file) return;
    try {
      if (file.size > TEXTURE_FILE_LIMIT)
        throw new Error("Layer project files must be smaller than 1 MB.");
      const imported = parseTextureDocument(await file.text());
      if (!mounted.current) return;
      history.commit(imported, {
        selection: { id: imported.layers.at(-1)?.id },
        label: "Open project file",
      });
      setToast("Layer project opened");
    } catch (e) {
      if (mounted.current) history.setError(e.message);
    }
  }
  useEffect(() => {
    function key(e) {
      if (
        e.defaultPrevented ||
        e.target.closest?.(".asset-content-browser,.alloy-paint-menu")
      )
        return;
      const command = e.ctrlKey || e.metaKey;
      const typing =
        e.target.isContentEditable ||
        ["TEXTAREA", "SELECT"].includes(e.target.tagName) ||
        (e.target.tagName === "INPUT" &&
          !["checkbox", "range", "button"].includes(e.target.type));
      if (command && e.key.toLowerCase() === "s") {
        e.preventDefault();
        saveProject();
        return;
      }
      if (typing) return;
      if (command && e.key.toLowerCase() === "z") {
        e.preventDefault();
        e.shiftKey ? history.redo() : history.undo();
      } else if (command && e.key.toLowerCase() === "y") {
        e.preventDefault();
        history.redo();
      } else if (command && e.key.toLowerCase() === "d") {
        e.preventDefault();
        action("duplicate");
      } else if (e.altKey && ["ArrowUp", "ArrowDown"].includes(e.key)) {
        e.preventDefault();
        action(e.key === "ArrowUp" ? "up" : "down");
      } else if (
        ["Delete", "Backspace"].includes(e.key) &&
        !e.target.closest("button")
      ) {
        e.preventDefault();
        action("remove");
      } else if (!command && ["f", "r"].includes(e.key.toLowerCase())) {
        e.preventDefault();
        setResetToken((p) => p + 1);
      } else if (e.key === "Escape") {
        decalEditing.deactivate();
        if (help) setHelp(false);
      }
    }
    window.addEventListener("keydown", key);
    return () => window.removeEventListener("keydown", key);
  });
  const editedDocument = digitalPainting.draft
    ? {
        ...decalEditing.document,
        layers: decalEditing.document.layers.map((l) =>
          l.id === digitalPainting.draft.layerId
            ? {
                ...l,
                ...(digitalPainting.draft.mask
                  ? { mask: digitalPainting.draft.mask }
                  : { paint: digitalPainting.draft.paint }),
              }
            : l,
        ),
      }
    : decalEditing.document;
  const surfaceDocument =
    sourceLayer?.surfaceMaterial &&
    activeGradient &&
    gradientDraft &&
    gradientTarget.mask
      ? {
          ...editedDocument,
          layers: editedDocument.layers.map((l) =>
            l.id === gradientTarget.layerId
              ? { ...l, mask: { ...l.mask, gradient: liveGradient } }
              : l,
          ),
        }
      : editedDocument;
  const viewport = (
    <>
      <DockTab icon={Box}>
        Viewport <small>Teapot</small>
      </DockTab>
      <div className="tp-viewport-toolbar">
        <div className="tp-scene-icon">
          <Box size={17} />
        </div>
        <strong className="tp-viewport-title">Teapot</strong>
        <span className="tp-preview-badge">PREVIEW</span>
        <label className="tp-header-field">
          <span>Shading</span>
          <select
            aria-label="Viewport shading"
            value={viewMode}
            onChange={(e) => chooseView(e.target.value)}
          >
            <option value="shaded">Lit</option>
            <option value="wireframe">Wireframe</option>
            <option value="uv">UV / Texture</option>
          </select>
        </label>
        <label className="tp-header-field">
          <span>Target</span>
          <select
            aria-label="Texture mesh parts"
            title="Drop materials onto the part you want"
            value={selectedPart ?? ""}
            onChange={(e) => selectPart(e.target.value || null)}
          >
            <option value="">All parts</option>
            {TEXTURE_PARTS.map((part) => (
              <option key={part.id} value={part.id}>
                {part.name}
              </option>
            ))}
          </select>
        </label>
        {viewMode === "uv" && (
          <label className="tp-header-field">
            <span>UV set</span>
            <select
              aria-label="UV coordinate set"
              value={uvSource ? "source" : "authoring"}
              onChange={(e) => {
                decalEditing.deactivate();
                setUVSource(e.target.value === "source");
              }}
            >
              <option value="authoring">Authoring UDIMs · 1001–1004</option>
              <option value="source">Source UV0 · overlapping</option>
            </select>
          </label>
        )}
        <div className="tp-viewport-end">
          <button
            className="tp-frame-button"
            aria-label={viewMode === "uv" ? "Frame UVs" : "Frame teapot"}
            title={
              viewMode === "uv"
                ? "Frame selected UV tile or all tiles · F"
                : "Frame teapot · F"
            }
            onClick={() => setResetToken((p) => p + 1)}
          >
            <Maximize2 size={14} /> Frame <kbd>F</kbd>
          </button>
          <button
              aria-label="Toggle viewport grid"
              aria-pressed={grid}
              title="Ground grid"
              onClick={() => setGrid(!grid)}
            >
              <Grid2X2 size={15} />
            </button>
            <button
              aria-label="Reset teapot view"
              title="Reset view · R"
              onClick={() => setResetToken((p) => p + 1)}
            >
              <RotateCcw size={14} />
            </button>
        </div>
      </div>
      <div className="tp-authoring-bar">
        <div
          className="tp-tool-group"
          role="group"
          aria-label="Surface decal placement tools"
        >
          <button
            aria-label="Navigate teapot"
            aria-pressed={
              !decalEditing.editor.active &&
              !maskEditing.editor.active &&
              !digitalPainting.active &&
              !pointEditor?.active
            }
            onClick={() => {
              digitalPainting.deactivate();
              maskEditing.change({ tool: "navigate" });
              decalEditing.context.onNavigate();
            }}
          >
            <MousePointer2 size={12} /> Navigate
          </button>
          <button
            aria-label="Viewport stamp brush"
            aria-pressed={
              decalEditing.editor.active && decalEditing.editor.mode === "brush"
            }
            disabled={
              disabled ||
              layer?.kind === "folder" ||
              !decalEditing.context.preset ||
              !decalEditing.context.placementAvailable
            }
            onClick={() => decalEditing.context.onArm("brush")}
          >
            <Stamp size={12} /> Stamp
          </button>
          <button
            aria-label="Viewport decal transform"
            aria-pressed={
              decalEditing.editor.active &&
              ["transform", "place"].includes(decalEditing.editor.mode)
            }
            disabled={
              disabled ||
              layer?.kind === "folder" ||
              !decalEditing.context.preset ||
              !decalEditing.context.placementAvailable
            }
            onClick={() => decalEditing.context.onArm("transform")}
          >
            <Move3D size={12} /> Transform
          </button>
        </div>
        <span className="tp-action-divider" />
        <DigitalPaintToolbar
          painting={digitalPainting}
          onOpen={() => openDigitalMenu()}
          mask={selection.mask}
        />
        <span className="tp-action-divider" />
        <div
          className="tp-mask-preview-tabs"
          role="group"
          aria-label="Texture previews"
        >
          <button
            aria-label="Material preview"
            aria-pressed={maskEditing.state.preview === "material"}
            onClick={() => maskEditing.change({ preview: "material" })}
          >
            Material
          </button>
          <button
            aria-label="Mask preview"
            aria-pressed={maskEditing.state.preview === "mask"}
            disabled={
              !(maskEditing.maskSelected || selectedDecal?.asMask) ||
              (viewMode === "uv" && uvSource)
            }
            onClick={() => maskEditing.change({ preview: "mask" })}
          >
            Mask
          </button>
        </div>
        <span
          className="tp-authoring-state"
          role="status"
          aria-label="Texture editing mode"
        >
          {maskEditing.mode === "mask"
            ? `Mask mode · ${layer?.name}`
            : maskEditing.mode === "decal"
              ? "Decal mode"
              : digitalPainting?.active
                ? `Paint mode · Digital ${digitalPainting.brush.tool}`
                : "Paint mode · Digital tools available"}
        </span>
        <button
          className="tp-stamp-studio-link"
          aria-label="Open Stamp workspace"
          onClick={() => onWorkspace("stamp")}
        >
          Stamp studio ↗
        </button>
      </div>
      <TextureMaskToolbar
        editing={maskEditing}
        viewMode={viewMode}
        source={uvSource}
        layer={layer}
      />
      <div
        data-view-mode={viewMode}
        data-selected-part={selectedPart || ""}
        className={`tp-scene-viewport ${digitalPainting.active ? "tp-digital-editing" : ""} ${surfaceEditing.active ? "tp-mask-editing" : ""} ${pointEditor?.placing ? "tp-gradient-placing" : ""} ${decalEditing.editor.active && ["brush", "place"].includes(decalEditing.editor.mode) ? "tp-decal-placing" : ""}`}
        ref={viewportRef}
        aria-label="Teapot viewport"
        onDragOver={sceneDragOver}
        onDragLeave={(e) => {
          if (e.currentTarget.contains(e.relatedTarget)) return;
          setDropPart(null);
          surfaceAPI.current?.hover(selectedPart);
        }}
        onDrop={(e) => {
          if (viewMode === "uv") return;
          e.preventDefault();
          const asset = readAssetDrop(e.dataTransfer),
            hit = surfaceAPI.current?.pick(e.clientX, e.clientY);
          dropOnSurface(asset, hit);
        }}
        onContextMenu={(e) => {
          e.preventDefault();
          openDigitalMenu();
        }}
      >
        <Viewport
          pointGradient={surfaceGradient}
          gradientEditor={viewMode === "uv" ? null : pointEditor}
          decalDocument={surfaceDocument}
          decalEditor={decalEditing.editor}
          params={PREVIEW_MATERIAL}
          systemBase
          paused={browserOpen || paintMenuOpen || viewMode === "uv"}
          renderEnabled={viewMode !== "uv"}
          shape="Teapot"
          environment="Studio softbox"
          rotate={false}
          wireframe={wireframe}
          surfaceGeometry={surfaceGeometry}
          surfaceAPI={surfaceAPI}
          maskPreview={maskPreview}
          maskEditor={viewMode === "uv" ? null : surfaceEditing}
          onSurfaceAtlas={setSurfaceAtlas}
          onMaskAtlas={(atlas) =>
            setMaskAtlas((old) =>
              old?.canvas === atlas?.canvas && old?.revision === atlas?.revision
                ? old
                : atlas,
            )
          }
          selectedPart={selectedPart}
          onPartSelect={selectPart}
          resetToken={reset3D}
          zoom={zoom3D}
          onZoomChange={setZoomLevel3D}
          onReady={() => setReady(true)}
          gridVisible={grid}
          studioLayout
        />
        {viewMode === "uv" && (
          <TextureUVView
            geometry={surfaceGeometry}
            doc={surfaceDocument}
            editor={decalEditing.editor}
            maskEditor={surfaceEditing}
            appearanceAtlas={surfaceAtlas}
            maskAtlas={maskPreview.preview === "mask" ? maskAtlas : null}
            selectedPart={selectedPart}
            onPartSelect={selectPart}
            onAssetDrop={dropOnSurface}
            source={uvSource}
            resetToken={resetUV}
            zoom={zoomUV}
            grid={grid}
            onZoomChange={setZoomLevelUV}
            onError={history.setError}
            onOpenPaintTools={openDigitalMenu}
          />
        )}
        {dropPart && (
          <span className="tp-part-drop-note">
            Drop → {texturePart(dropPart).name} only
          </span>
        )}
        {decalEditing.editor.active && (
          <span className="tp-decal-mode-note">
            {viewMode === "uv"
              ? decalEditing.editor.mode === "brush"
                ? "Click a UV island to stamp · gutters do not accept placement"
                : decalEditing.editor.mode === "place"
                  ? "Click a UV island to re-anchor this UV decal"
                  : "Drag artwork to move / rotate / scale · Shift snaps · Escape cancels"
              : decalEditing.editor.mode === "brush"
                ? "Aim at the surface · click to stamp · Escape / Navigate to orbit"
                : decalEditing.editor.mode === "place"
                  ? "Click to anchor the decal to the surface"
                  : "Drag 3D handles · move / rotate / scale · Re-anchor to snap to the surface"}
          </span>
        )}
        {!ready && <span className="tp-scene-loading">Preparing teapot…</span>}
        {surfaceGradient && (
          <span className="tp-gradient-scene-note">
            {pointEditor?.placing
              ? "Click the surface to place a point"
              : pointEditor
                ? "Drag gradient balls · surface anchored"
                : "Selected surface gradient / mask preview"}
          </span>
        )}
        <div className="tp-scene-caption">
          <span className="tp-little-dot" /> TEAPOT STUDY{" "}
          <small>01 / DEFAULT MESH</small>
        </div>
        <div className="tp-scene-nav">
          <button
            aria-label={viewMode === "uv" ? "Zoom out UVs" : "Zoom out teapot"}
            onClick={() => setZoom({ direction: -1, token: Date.now() })}
          >
            <Minus size={13} />
          </button>
          <output aria-label={viewMode === "uv" ? "UV zoom" : "Teapot zoom"}>
            {zoomLevel}%
          </output>
          <button
            aria-label={viewMode === "uv" ? "Zoom in UVs" : "Zoom in teapot"}
            onClick={() => setZoom({ direction: 1, token: Date.now() })}
          >
            <Plus size={13} />
          </button>
        </div>
        <div className="tp-view-axis" aria-hidden="true">
          <svg width="62" height="68" viewBox="0 0 62 68">
            <path
              d="M31 39V12M31 39l23 11M31 39 9 52"
              stroke="#666"
              strokeWidth="1"
            />
            <circle cx="31" cy="39" r="3" fill="#bcbcb9" />
            <circle cx="31" cy="12" r="8" fill="#253b2d" />
            <text x="31" y="15" fill="#95bca1">
              Y
            </text>
            <circle cx="54" cy="50" r="8" fill="#44302e" />
            <text x="54" y="53" fill="#c18e85">
              X
            </text>
            <circle cx="9" cy="52" r="8" fill="#2b3346" />
            <text x="9" y="55" fill="#97a7c8">
              Z
            </text>
          </svg>
        </div>
        <span className="tp-viewport-hint">Drag to orbit · scroll to zoom</span>
      </div>
      <footer className="tp-viewport-footer">
        <span>
          <span className={`tp-little-dot ${ready ? "ready" : ""}`} />
          {ready ? "Three.js · WebGL" : "Preparing scene"}
        </span>
        <span className="tp-scope-indicator">
          {viewMode === "uv"
            ? "Ordered lit UV appearance · not exported PBR maps"
            : doc.layers.some(
                  (l) => l.surfaceMaterial || l.paint?.operations.length,
                )
              ? "Layer stack live · top layers cover lower decals"
              : surfaceGradient
                ? "Selected fill / mask + decals · not a full UV stack"
                : doc.layers.some((l) => l.decals.length)
                  ? "Layer stack live · decals follow layer order"
                  : "Layer 0 · white fallback · digital tools available"}
        </span>
      </footer>
    </>
  );
  return (
    <section
      className="tp-studio"
      aria-label="Texture studio"
      data-mobile-panel={mobilePanel}
      data-mask-authoring={maskEditing.maskSelected ? "mask" : "paint"}
      data-digital-painting={digitalPainting.active}
    >
      <StudioHeader
        workspace="texture"
        name={doc.name}
        extension=".texture"
        onWorkspace={onWorkspace}
        onClose={onClose}
        closeLabel="Close texture studio"
      >
        <button
          className="tp-paint-tool-button"
          ref={toolButton}
          aria-label="Open paint tool menu"
          title="Digital Paint / Erase / Fill · also right-click the viewport"
          aria-expanded={paintMenuOpen}
          onClick={() => openDigitalMenu()}
        >
          <Brush size={14} />
          <span>
            {
              DIGITAL_BRUSH_PRESETS.find(
                (p) => p.id === doc.digitalPainting.preset,
              )?.name
            }
          </span>
        </button>
        <button
          aria-label="Open layer project"
          title="Open layer project JSON"
          onClick={() => fileRef.current.click()}
        >
          <FolderOpen size={14} />
        </button>
        <button
          className="tp-save-project"
          aria-label="Save project"
          onClick={saveProject}
        >
          <Save size={13} />
          <span>Save project</span>
        </button>
      </StudioHeader>
      <input
        ref={fileRef}
        type="file"
        accept=".json,application/json"
        hidden
        aria-label="Open texture layer JSON"
        onChange={openProject}
      />
      <div
        className="tp-mobile-tabs"
        role="tablist"
        aria-label="Left dock panels"
      >
        <button
          role="tab"
          aria-selected={mobilePanel === "layers"}
          onClick={() => setMobilePanel("layers")}
        >
          <Layers3 size={13} /> Layers
        </button>
        <button
          role="tab"
          aria-selected={mobilePanel === "inspector"}
          onClick={() => {
            setMobilePanel("inspector");
            setRightPanel("inspector");
          }}
        >
          <SlidersHorizontal size={13} /> Inspector
        </button>
        <button
          role="tab"
          aria-selected={mobilePanel === "history"}
          onClick={() => {
            setMobilePanel("history");
            setRightPanel("history");
          }}
        >
          <History size={13} /> History
        </button>
      </div>
      <main
        className="tp-layout"
        ref={layoutRef}
        style={{
          "--layers-width": `${layout.layers}px`,
          "--inspector-width": `${layout.inspector}px`,
        }}
      >
        <LayerStackPanel Caption="Layer stack" className="tp-dock tp-layers-dock">
          <TextureLayerStack
            doc={doc}
            selection={selection}
            select={selectLayer}
            decalContext={decalEditing.context}
            disabled={disabled}
            onAction={action}
            onBake={() => onWorkspace?.("baking", "mesh")}
            onDrop={dropLayer}
            onMove={moveLayer}
            onFolderDrop={(id, parentId) => moveLayer(id, parentId)}
            thumbs={materialWorkspace.thumbs}
            onVisibility={(id) =>
              patch(id, {
                visible: !history.docRef.current.layers.find((l) => l.id === id)
                  .visible,
              })
            }
            onLock={(id) =>
              patch(id, {
                locked: !history.docRef.current.layers.find((l) => l.id === id)
                  .locked,
              })
            }
            onMask={mask}
            onAssetDrop={assignAsset}
          />
          <ResizeDock
            name="layer stack"
            width={layout.layers}
            min={240}
            max={360}
            onChange={(w) => resize("layers", w)}
          />
        </LayerStackPanel>
        <section className="tp-dock tp-viewport-dock" aria-label="3D scene">
          {viewport}
        </section>
        <aside
          className="tp-dock tp-inspector-dock"
          aria-label="Layer inspector"
        >
          <div
            className="tp-tab-strip tp-inspector-tabs"
            role="tablist"
            aria-label="Inspector panels"
            onKeyDown={(e) => {
              if (!["ArrowLeft", "ArrowRight", "Home", "End"].includes(e.key))
                return;
              e.preventDefault();
              const tab =
                e.key === "Home"
                  ? "inspector"
                  : e.key === "End"
                    ? "history"
                    : rightPanel === "inspector"
                      ? "history"
                      : "inspector";
              setRightPanel(tab);
              e.currentTarget
                .querySelector(`[data-panel-tab="${tab}"]`)
                ?.focus();
            }}
          >
            {[
              ["inspector", "Inspector", SlidersHorizontal],
              ["history", "History", History],
            ].map(([id, label, Icon]) => (
              <button
                key={id}
                className={`tp-document-tab ${rightPanel === id ? "active" : ""}`}
                role="tab"
                id={`${panelId}-${id}-tab`}
                data-panel-tab={id}
                aria-selected={rightPanel === id}
                aria-controls={`${panelId}-${id}`}
                tabIndex={rightPanel === id ? 0 : -1}
                onClick={() => setRightPanel(id)}
              >
                <Icon size={12} />
                <span>{label}</span>
              </button>
            ))}
          </div>
          <div
            className="tp-inspector-content"
            role="tabpanel"
            id={`${panelId}-inspector`}
            aria-label="Inspector"
            hidden={rightPanel !== "inspector"}
          >
            <TextureLayerInspector
              showTab={false}
              decalContext={decalEditing.context}
              painting={doc.painting}
              materialWorkspace={materialWorkspace}
              gradientContext={gradientContext}
              onGradientActivate={activateGradient}
              onStroke={sourceStroke}
              onOpenTools={openSourceTools}
              onOpenDigitalTools={openDigitalMenu}
              layer={layer}
              doc={doc}
              thumbs={materialWorkspace.thumbs}
              onFolderChange={(parentId) => moveLayer(selection.id, parentId)}
              onUngroup={() => action("ungroup")}
              maskSelected={selection.mask}
              index={index}
              count={doc.layers.length}
              onPatch={(changes, options) =>
                patch(selection.id, changes, options)
              }
              onMask={(changes, options) =>
                mask(selection.id, changes, options)
              }
              onSelectLayer={() => selectLayer(selection.id)}
              onRemoveMask={() =>
                history.commit(
                  patchTextureLayer(history.docRef.current, selection.id, {
                    mask: null,
                  }),
                  { selection: { id: selection.id, mask: false } },
                )
              }
            />
          </div>
          <div
            className="tp-inspector-content"
            role="tabpanel"
            id={`${panelId}-history`}
            aria-label="History"
            hidden={rightPanel !== "history"}
          >
            <TextureHistoryPanel history={history} />
          </div>
          <ResizeDock
            name="layer inspector"
            edge="left"
            width={layout.inspector}
            min={290}
            max={440}
            onChange={(w) => resize("inspector", w)}
          />
        </aside>
      </main>
      <div className="tp-content-browser-slot" aria-hidden="true" />
      <AssetContentBrowser
        workspace={materialWorkspace}
        painting={doc.painting}
        onPaintSettings={painting}
        onAssign={assignAsset}
        canAssign={!disabled}
        onExpandedChange={setBrowserOpen}
        onSaveProject={saveProject}
        onUndo={history.undo}
        onRedo={history.redo}
        stampPresets={decalEditing.context.presets}
        stampPresetId={decalEditing.context.preset?.id}
        onStampPreset={decalEditing.context.onPreset}
        onEditStamp={onEditStamp}
      />
      <DigitalPaintMenu
        painting={digitalPainting}
        initialCatalogue={paintMenuCatalogue}
        layer={layer}
        mask={!!selection.mask}
        ink={maskEditing.state.value}
        onInk={(value) => {
          digitalPainting.cancel();
          maskEditing.change({ value });
        }}
        onPatch={(changes) => {
          digitalPainting.cancel();
          patch(layer.id, changes, { discrete: true });
        }}
        disabled={disabled || !isTextureLayerVisible(doc, layer?.id)}
        legacySettings={doc.painting}
        onLegacyChange={painting}
        open={paintMenuOpen}
        onClose={closePaintMenu}
        onSave={saveProject}
        onUndo={history.undo}
        onRedo={history.redo}
      />
      <footer className="tp-status-bar">
        <span>
          <span className="tp-little-dot" />
          {history.status}
        </span>
        <div>
          <button
            aria-label="Undo layer edit"
            title="Undo · Ctrl/⌘ Z"
            disabled={!history.canUndo}
            onClick={history.undo}
          >
            <Undo2 size={13} />
          </button>
          <button
            aria-label="Redo layer edit"
            title="Redo · Ctrl/⌘ Shift Z"
            disabled={!history.canRedo}
            onClick={history.redo}
          >
            <Redo2 size={13} />
          </button>
          <span className="tp-action-divider" />
          <button
            aria-label="Layer workspace help"
            aria-expanded={help}
            onClick={() => setHelp(!help)}
          >
            <FileJson size={12} /> Layer document
          </button>
        </div>
        <span className="tp-stage-label">DIGITAL PAINT / LAYERS</span>
      </footer>
      {help && (
        <div
          className="tp-help-card"
          role="region"
          aria-label="Layer workspace guide"
        >
          <button
            aria-label="Close layer workspace guide"
            onClick={() => setHelp(false)}
          >
            <X size={14} />
          </button>
          <h2>Part materials & UV authoring</h2>
          <p>
            Drag a material from the shared browser directly onto Body, Lid,
            Handle or Spout. Only the hit part receives its native material; its
            editable snapshot is saved in a material layer.
          </p>
          <p>
            Choose UV / Texture to see the actual authoring charts in UDIMs
            1001–1004. Click a UV island with Stamp or drop a stamp there; it
            appears on the correct 3D part. Source UV0 is available for
            read-only inspection because its patches overlap.
          </p>
          <p>
            <kbd>Ctrl/⌘ Z</kbd> Undo · <kbd>Ctrl/⌘ D</kbd> Duplicate ·{" "}
            <kbd>Ctrl/⌘ S</kbd> Save project JSON · <kbd>F</kbd> Frame teapot.
          </p>
          <small>
            Digital Paint / Erase / Fill works on the selected layer or mask in
            Lit and packed UV views. Brush settings and channel targets live in
            the popup brush studio; History shows actual operation footprints.
            Imported 2D source textures remain a separate authoring workflow.
            Physical-media instruments are configuration only, not simulated.
          </small>
        </div>
      )}
      {history.error && (
        <div className="tp-error" role="alert">
          <span>{history.error}</span>
          <button
            aria-label="Dismiss layer error"
            onClick={() => history.setError("")}
          >
            <X size={13} />
          </button>
        </div>
      )}
      {toast && (
        <div className="tp-toast" role="status">
          <Check size={13} />
          {toast}
        </div>
      )}
    </section>
  );
}
