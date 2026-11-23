import PatternOrganizationControls from "./PatternOrganizationControls.jsx";
import PatternSurfaceChannelControls from "./PatternSurfaceChannelControls.jsx";
import { PropagatePatternProperties, PatternEnclosureContains, CreatePatternEnclosure } from "./PatternOrganization.js";
import ArtworkArrayControls from "./ArtworkArrayControls.jsx";
import { ArtworkSelectionMask } from "./ArtworkSelectionMask.js";
import ArtworkDocumentMenu from "./ArtworkDocumentMenu.jsx";
import ArtworkMaskCanvas from "./ArtworkMaskCanvas.jsx";
import ArtworkMaskStackRow from "./ArtworkMaskStackRow.jsx";
import ArtworkMaskControls from "./ArtworkMaskControls.jsx";
import { PatternColourPreviewCss } from "./PatternColourSources.js";
import LayerStackPanel, { LayerStackFilters } from "./LayerStackPanel.jsx";
import PatternColourControls, { PatternPropertyCard } from "./PatternColourControls.jsx";
import PatternTextControls from "./PatternTextControls.jsx";
import SVGSourceEditor from "./SVGSourceEditor.jsx";
import { patternTextFont, loadPatternTextFonts } from "./patternTextFonts.js";
import {
  outlinePatternText,
  updatePatternTextLayer,
} from "./patternTextGeometry.js";
import { booleanPatternLayers, layerWorldContours } from "./vectorEditorGeometry.js";
import usePatternHistory from "./usePatternHistory.js";
import ShapeEditorCanvas from "./ShapeEditorCanvas.jsx";
import ShapeEditorToolbar, {
  ShapeSelectionActions,
  shapeTools,
} from "./ShapeEditorToolbar.jsx";
import {
  readPatternDraft,
  savePatternDraft,
  PATTERN_DRAFT_KEY,
} from "./patternDraft.js";
import {
  editableSelection,
  expandPatternSelection,
  translateSelection,
  snappedTranslation,
  alignSelection,
  distributeSelection,
  reorderSelection,
  clonePatternLayers,
  newPatternGroup,
  convertPatternLayerToPath,
  svgPathNodes,
  selectionBounds,
  resizeSelection,
  reframePatternPath,
} from "./shapeEditorGeometry.js";
import { replacePatternColor } from "./patternLibrary.js";
import {
  automaticPathNodes,
  worldPathNodes,
  withWorldPathNodes,
} from "./pathEditorGeometry.js";
import RugCompositionControls from "./RugCompositionControls.jsx";
import PatternTextileTools from "./PatternTextileTools.jsx";
import { textilePalettes } from "./patternLibrary.js";
import { rebuildCollectionFade } from "./patternCollections.js";
import PatternMaterialPreview from "./PatternMaterialPreview.jsx";
import { resolvePatternBase } from "./patternSurface.js";
import {
  reflectPatternLayer,
  radialPatternLayers,
} from "./patternTransforms.js";
import React, { useState, useMemo, useRef, useEffect, useLayoutEffect } from "react";
import {
  X,
  Plus,
  Undo2,
  Redo2,
  Upload,
  Download,
  Eye,
  EyeOff,
  Lock,
  LockKeyhole,
  UnlockKeyhole,
  GripVertical,
  Group,
  CircleHelp,
  FilePlus2,
  Check,
  Spline,
  ArrowUp,
  ArrowDown,
  Layers3,
} from "lucide-react";
import {
  patternStarter,
  resolvePatternStarterName,
  patternStarterCatalog,
  generatePatternLayout,
  patternLayer,
  patternSVG,
  patternDimensions,
  validatePattern,
  patternFinishes,
  normalizePatternText,
} from "./patternDocument.js";
import { sanitizePatternSVG } from "./patternImport.js";
import { rasterPatternSVG, patternImage } from "./patternRuntime.js";
import { leatherSourceSVG } from "./leatherSource.js";
import { StudioHeader, StudioDockTab } from "./StudioUI.jsx";
import PatternLayerThumbnail from "./PatternLayerThumbnail.jsx";
import "./patternEditor.css";
import "./shapeEditor.css";
// 🧩 tp-layer-row/tp-layer-thumbnail/tp-layer-eye/tp-layer-lock chrome is shared 1:1 with the
// Texture workspace's layer stack so both panels read identically; see textureStudio.css.
import "./textureStudio.css";

// Only mounted page cards generate SVG. Hundreds of presets must not block
// startup, pointer moves or background material-thumbnail updates.
const StarterThumb = React.memo(function StarterThumb({ name, palette }) {
  const src = useMemo(
    () =>
      `data:image/svg+xml,${encodeURIComponent(patternSVG(patternStarter(name, palette), "preview"))}`,
    [name, palette],
  );
  return <img src={src} alt="" loading="lazy" decoding="async" />;
});
const libraryGroups = [
  "All patterns",
  ...new Set(patternStarterCatalog.map((p) => p.group)),
];

function solidSVGColor(value) {
  if (!value || value === "none") return null;
  if (/^#[a-f\d]{6}$/i.test(value)) return value;
  if (/^#[a-f\d]{3}$/i.test(value))
    return "#" + [...value.slice(1)].map((c) => c + c).join("");
  if (
    /var\(|currentColor|inherit|initial|unset/i.test(value) ||
    !globalThis.CSS?.supports("color", value)
  )
    return null;
  const ctx = document.createElement("canvas").getContext("2d");
  ctx.fillStyle = value;
  return /^#[a-f\d]{6}$/i.test(ctx.fillStyle) ? ctx.fillStyle : null;
}
function patternDownload(text, name, type) {
  const a = document.createElement("a"),
    url = URL.createObjectURL(new Blob([text], { type }));
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
export default function PatternEditor({
  initial,
  currentMaterial,
  currentShape,
  onClose,
  onWorkspace,
  onApply,
}) {
  const [error, setError] = useState("");
  const history = usePatternHistory(
    () =>
      initial
        ? validatePattern(initial)
        : patternStarter(
            resolvePatternStarterName(
              new URLSearchParams(window.location.search).get("pattern"),
            ),
          ),
    setError,
  );
  const {
    doc,
    docRef,
    selected,
    setSelected,
    preview: previewDocument,
    cancel: cancelDocument,
    canUndo,
    canRedo,
  } = history;
  const canvasRef = useRef(null),
    clipboard = useRef([]),
    rangeAnchor = useRef(0),
    layerDrag = useRef(null);
  const [MaskIdentity, ConfigureMaskIdentity] = useState(null);
  const [LibraryCollapsed, ConfigureLibraryCollapsed] = useState(doc.layers.length > 0);
  const [EnclosureScope, ConfigureEnclosureScope] = useState("");
  const [LayerQuery, SetLayerQuery] = useState("");
  const [LayerFilter, SetLayerFilter] = useState("all");
  const [LayerAddOpen, SetLayerAddOpen] = useState(false);
  const LayerAddPanel = useRef(null);
  const LayerAddChoices = useRef(null);
  const [LayerAddPosition, SetLayerAddPosition] = useState(null);
  useLayoutEffect(() =>
  {
    if (!LayerAddOpen || !LayerAddChoices.current) { SetLayerAddPosition(null); return; }
    const Anchor = LayerAddPanel.current.getBoundingClientRect();
    const Bounds = LayerAddChoices.current.getBoundingClientRect();
    SetLayerAddPosition({ left: Math.max(12, Math.min(Anchor.right - Bounds.width, window.innerWidth - Bounds.width - 12)), top: Math.max(12, Anchor.bottom + Bounds.height + 8 <= window.innerHeight - 12 ? Anchor.bottom + 8 : Anchor.top - Bounds.height - 8) });
  }, [LayerAddOpen]);
  useEffect(() =>
  {
    if (!LayerAddOpen) return;
    const Dismiss = Event =>
    {
      if (Event.type === "keydown" && Event.key !== "Escape") return;
      if (Event.type === "pointerdown" && LayerAddPanel.current?.contains(Event.target)) return;
      SetLayerAddOpen(false);
      if (Event.type === "keydown") { Event.preventDefault(); Event.stopPropagation(); }
      if (Event.type === "keydown") LayerAddPanel.current?.querySelector("button")?.focus();
    };
    document.addEventListener("pointerdown", Dismiss, true);
    document.addEventListener("keydown", Dismiss, true);
    window.addEventListener("resize", Dismiss);
    return () =>
    {
      document.removeEventListener("pointerdown", Dismiss, true);
      document.removeEventListener("keydown", Dismiss, true);
      window.removeEventListener("resize", Dismiss);
    };
  }, [LayerAddOpen]);
  const LayerMatches = doc.layers.map((Layer, Index) => ({ Layer, Index })).filter(({ Layer }) =>
    (!EnclosureScope || PatternEnclosureContains(doc, EnclosureScope, Layer.EnclosureToken)) &&
    `${Layer.name} ${Layer.kind} ${Layer.group || ""}`.toLocaleLowerCase().includes(LayerQuery.trim().toLocaleLowerCase()) &&
    (LayerFilter === "all" || (LayerFilter === "locked" ? Layer.locked : Layer.visible === (LayerFilter === "visible"))));
  const SelectionRevision = useRef(selected); SelectionRevision.current = selected;
  const selection = selected.at(-1) ?? -1;
  const setSelection = (i) => setSelected(i >= 0 ? [i] : []);
  const l = selected.length === 1 ? doc.layers[selection] : null;
  const MaskActive = Boolean(l?.mask && l.id === MaskIdentity);
  useEffect(() => { if (MaskActive) ConfigureLibraryCollapsed(true); }, [MaskActive]);
  const editIds = editableSelection(doc, selected);
  const [tool, setTool] = useState("move"),
    [pathState, setPathState] = useState({
      active: false,
      points: 0,
      canUndo: false,
      canRedo: false,
    }),
    [pathPanelTarget, setPathPanelTarget] = useState(null),
    [showGrid, setShowGrid] = useState(false),
    [smartGuides, setSmartGuides] = useState(true),
    [showHelp, setShowHelp] = useState(false),
    [recoverable, setRecoverable] = useState(() => {
      const draft = readPatternDraft();
      return draft && JSON.stringify(draft.doc) !== JSON.stringify(doc)
        ? draft
        : null;
    }),
    [draftStatus, setDraftStatus] = useState("Ready to design"),
    [exportSize, setExportSize] = useState(2048),
    [drawingStyle, setDrawingStyle] = useState({
      paint: true,
      fillEnabled: true,
      color: "#dd765d",
      stroke: "#263c48",
      strokeWidth: 0,
      finish: "cotton",
      cornerRadius: 0,
      sides: 6,
      starPoints: 5,
      innerRadius: 0.45,
    });
  useEffect(() =>
  {
    if (selected.length < 2) return;
    const Reference = doc.layers[selected.find(Index => !doc.layers[Index].locked) ?? selected[0]];
    setDrawingStyle(Previous => ({ ...Previous, ...Object.fromEntries(["color", "finish", "roughness", "metalness", "relief"].filter(Key => Reference[Key] !== undefined).map(Key => [Key, Reference[Key]])), stroke: Reference.stroke || Reference.color, strokeWidth: Reference.strokeWidth || 0, fillSource: Reference.fillSource, strokeSource: Reference.strokeSource, fillEnabled: Reference.fillEnabled !== false }));
  }, [doc, selected]);
  const [snapping, setSnapping] = useState({
    points: true,
    curves: false,
    guides: true,
    angle: 0,
  });
  const [fontsReady, setFontsReady] = useState(false),
    [textDefaults, setTextDefaults] = useState(() => normalizePatternText()),
    [textFocus, setTextFocus] = useState(0),
    [textError, setTextError] = useState("");
  const [svgSource, setSVGSource] = useState(null);
  useEffect(() => {
    let mounted = true;
    loadPatternTextFonts()
      .then(() => {
        if (mounted) setFontsReady(true);
      })
      .catch((e) => {
        if (mounted) setTextError(e.message);
      });
    return () => {
      mounted = false;
    };
  }, []);
  function changeText(patch) {
    try {
      const current = docRef.current,
        target = current.layers[selection],
        options = normalizePatternText({
          ...(target?.kind === "text" && selected.length === 1
            ? target.text
            : textDefaults),
          ...patch,
        }),
        font = patternTextFont(options.fontFamily, options.fontWeight);
      if (!font)
        throw new Error(
          "The bundled fonts are still preparing. Please try again.",
        );
      const geometry = outlinePatternText(font, options);
      if (target?.kind === "text" && selected.length === 1) {
        if (target.locked) return;
        const next = updatePatternTextLayer(
          target,
          font,
          patch,
          patternTextFont(target.text.fontFamily, target.text.fontWeight),
        );
        commit(
          {
            ...current,
            layers: current.layers.map((l, i) => (i === selection ? next : l)),
          },
          { key: `text-${selection}-${Object.keys(patch).join("-")}` },
        );
      }
      setTextDefaults(options);
      setTextError(
        geometry.unsupported.length
          ? `The bundled Latin font has no glyph for: ${geometry.unsupported.join(" ")}. A missing-glyph outline is used.`
          : "",
      );
    } catch (e) {
      setTextError(e.message);
    }
  }
  function createText(position) {
    const current = docRef.current;
    if (current.layers.length >= 64) {
      setError("A pattern supports up to 64 layers.");
      return;
    }
    try {
      const font = patternTextFont(
        textDefaults.fontFamily,
        textDefaults.fontWeight,
      );
      if (!font)
        throw new Error(
          "The bundled fonts are still preparing. Please try again.",
        );
      const outlined = outlinePatternText(font, textDefaults);
      const added = patternLayer("text", {
        ...drawingStyle,
        ...outlined,
        name: `Text: ${textDefaults.value.slice(0, 25) || "empty"}`,
        x: Math.min(1024, Math.max(-512, position.x + outlined.width / 2)),
        y: Math.min(1024, Math.max(-512, position.y + outlined.height / 2)),
        paint: true,
      });
      if (
        commit(
          { ...current, layers: [...current.layers, added] },
          { selected: [current.layers.length] },
        )
      ) {
        setTextFocus((v) => v + 1);
        setTextError(
          outlined.unsupported.length
            ? `The bundled Latin font has no glyph for: ${outlined.unsupported.join(" ")}. A missing-glyph outline is used.`
            : "",
        );
      }
    } catch (e) {
      setError(e.message);
    }
  }
  function editText(index) {
    setSelected([index]);
    setTool("text");
    setTextFocus((v) => v + 1);
  }
  function openSVGSource(index = null) {
    if (canvasRef.current && !canvasRef.current.prepareExport()) return;
    const target = index === null ? null : docRef.current.layers[index];
    if (target?.locked) {
      setError("Unlock the SVG layer before editing its source.");
      return;
    }
    setSVGSource({ index, source: target?.svg || "", original: target?.svg });
  }
  function embedSVG(clean) {
    try {
      const current = docRef.current;
      if (svgSource.index !== null) {
        const target = current.layers[svgSource.index];
        if (
          !target ||
          target.kind !== "svg" ||
          target.locked ||
          target.svg !== svgSource.original
        )
          throw new Error(
            "The SVG layer changed. Reopen its source editor before applying.",
          );
        return commit({
          ...current,
          layers: current.layers.map((l, i) =>
            i === svgSource.index ? { ...l, svg: clean } : l,
          ),
        });
      }
      const root = new DOMParser().parseFromString(
          clean,
          "image/svg+xml",
        ).documentElement,
        box = root
          .getAttribute("viewBox")
          .split(/[\s,]+/)
          .map(Number),
        ratio = box[2] / box[3];
      return add("svg", {
        name: "Embedded SVG",
        svg: clean,
        width: ratio >= 1 ? 256 : Math.max(1, 256 * ratio),
        height: ratio >= 1 ? Math.max(1, 256 / ratio) : 256,
      });
    } catch (e) {
      setError(e.message);
      return false;
    }
  }
  function sampleAppearance(index, strokeOnly = false) {
    const current = docRef.current,
      source = current.layers[index];
    if (source && ["svg", "image"].includes(source.kind)) {
      setError(
        "The style eyedropper samples native vectors. Embedded images and SVG sources keep their own colors.",
      );
      return;
    }
    const paint = source
      ? {
          paint: true,
          color: source.color,
          fillEnabled: source.paint
            ? source.fillEnabled
            : !(source.kind === "path" && source.strokeWidth),
          stroke: source.paint ? source.stroke : source.color,
          strokeWidth: source.strokeWidth,
          strokeLinecap: source.strokeLinecap || "round",
          strokeLinejoin: source.strokeLinejoin || "round",
        }
      : { paint: true, color: current.background, fillEnabled: true };
    const patch = strokeOnly
      ? {
          paint: true,
          stroke: paint.stroke || paint.color,
          strokeWidth: paint.strokeWidth || 1,
        }
      : paint;
    setDrawingStyle((p) => ({ ...p, ...patch }));
    const chosen = editableSelection(current, selected).filter(
      (i) => !["svg", "image"].includes(current.layers[i].kind),
    );
    if (chosen.length)
      commit({
        ...current,
        layers: current.layers.map((l, i) =>
          chosen.includes(i) ? { ...l, ...patch } : l,
        ),
      });
    setDraftStatus(strokeOnly ? "Stroke sampled" : "Fill & stroke sampled");
  }
  useEffect(() => {
    if (!history.dirty || history.previewing || pathState.active) return;
    setDraftStatus("Saving draft…");
    const timer = setTimeout(() => {
      try {
        savePatternDraft(doc);
        setDraftStatus("Draft saved locally");
      } catch {
        setDraftStatus("Storage unavailable — save a document");
      }
    }, 700);
    return () => clearTimeout(timer);
  }, [doc, history.dirty, history.previewing, recoverable, pathState.active]);
  useEffect(() => {
    if (!l || ["image", "svg"].includes(l.kind)) return;
    setDrawingStyle((previous) => ({
      ...previous,
      paint: true,
      color: l.color,
      fillSource: l.fillSource,
      strokeSource: l.strokeSource,
      stroke: l.paint ? l.stroke : l.color,
      fillEnabled: l.paint
        ? l.fillEnabled
        : !(l.kind === "path" && l.strokeWidth),
      strokeWidth: l.strokeWidth,
      strokeLinecap: l.strokeLinecap || "round",
      strokeLinejoin: l.strokeLinejoin || "round",
      finish: l.finish,
      roughness: l.roughness,
      metalness: l.metalness,
      relief: l.relief,
      ...(l.cornerRadius !== undefined ? { cornerRadius: l.cornerRadius } : {}),
      ...(l.sides ? { sides: l.sides } : {}),
      ...(l.starPoints
        ? { starPoints: l.starPoints, innerRadius: l.innerRadius }
        : {}),
    }));
  }, [
    selection,
    l?.color,
    l?.fillSource,
    l?.strokeSource,
    l?.stroke,
    l?.fillEnabled,
    l?.strokeWidth,
    l?.strokeLinecap,
    l?.strokeLinejoin,
    l?.finish,
    l?.roughness,
    l?.metalness,
    l?.relief,
    l?.cornerRadius,
    l?.sides,
    l?.starPoints,
    l?.innerRadius,
  ]);
  const multiBounds =
    selected.length > 1 ? selectionBounds(doc, editIds) : null;
  function bulkGeometry(key, value) {
    if (!multiBounds || !Number.isFinite(value)) return;
    if (key === "x" || key === "y")
      commit(
        translateSelection(
          docRef.current,
          editIds,
          key === "x" ? value - multiBounds.x : 0,
          key === "y" ? value - multiBounds.y : 0,
        ),
        { key: `group-${key}` },
      );
    else
      commit(
        resizeSelection(
          docRef.current,
          editIds,
          key === "width" ? "e" : "s",
          {
            x: multiBounds.x + (key === "width" ? value / 2 : 0),
            y: multiBounds.y + (key === "height" ? value / 2 : 0),
          },
          { aspect, center: true },
        ),
        { key: `group-${key}` },
      );
  }
  function commit(next, options) {
    const Before = options?.before || docRef.current;
    const Scoped = !options?.IsolateStructure && EnclosureScope && next.Enclosures?.some(Entry => Entry.Token === EnclosureScope)
      ? { ...next, layers: next.layers.map((Layer, Index) => Index >= Before.layers.length && !Layer.EnclosureToken && !Layer.group ? { ...Layer, EnclosureToken: EnclosureScope } : Layer) } : next;
    const Finalized = history.commit(options?.IsolateStructure ? Scoped : PropagatePatternProperties(Before, Scoped), options);
    if (Finalized && (options?.IsolateStructure || (EnclosureScope && (options?.selected || SelectionRevision.current).some(Index => Scoped.layers[Index] && !PatternEnclosureContains(Scoped, EnclosureScope, Scoped.layers[Index].EnclosureToken))))) ConfigureEnclosureScope("");
    return Finalized;
  }
  const update = (v) =>
    commit(
      { ...docRef.current, ...v },
      { key: "document-" + Object.keys(v).join("-") },
    );
  const layer = (v) => {
    const current = docRef.current;
    if (!current.layers[selection] || current.layers[selection].locked) return;
    commit(
      {
        ...current,
        layers: current.layers.map((p, i) =>
          i === selection
            ? v.closed !== undefined && p.nodes?.some((n) => n.mode === "auto")
              ? withWorldPathNodes(
                  { ...p, ...v },
                  automaticPathNodes(
                    worldPathNodes(p),
                    v.closed,
                    p.curveTension ?? 1,
                  ),
                )
              : v.nodes || v.closed !== undefined
                ? reframePatternPath({ ...p, ...v })
                : { ...p, ...v }
            : p,
        ),
      },
      { key: `layer-${selection}-` + Object.keys(v).join("-") },
    );
  };
  function ChangeDrawingSource(Channel, Source, Colour)
  {
    const ColourKey = Channel === "fill" ? "color" : "stroke";
    const Patch = { paint: true, [`${Channel}Source`]: Source, [ColourKey]: Colour };
    setDrawingStyle(Previous => ({ ...Previous, ...Patch, ...(Channel === "stroke" ? { strokeWidth: Previous.strokeWidth || 1 } : {}) }));
    if (selected.length > 1)
    {
      const Current = docRef.current;
      commit({ ...Current, layers: Current.layers.map((Layer, Index) => editIds.includes(Index) && !["image", "svg"].includes(Layer.kind)
        ? { ...Layer, ...Patch, ...(Channel === "stroke" ? { strokeWidth: Layer.strokeWidth || 1 } : {}) } : Layer) });
    }
  }
  const undo = () => {
    if (canvasRef.current?.undoPath()) return;
    canvasRef.current?.cancelInteraction();
    history.undo();
  };
  const redo = () => {
    if (canvasRef.current?.redoPath()) return;
    canvasRef.current?.cancelInteraction();
    history.redo();
  };
  function saveDocument() {
    if (canvasRef.current && !canvasRef.current.prepareExport()) return;
    patternDownload(
      JSON.stringify(validatePattern(docRef.current), null, 2),
      "pattern.json",
      "application/json",
    );
  }
  function closeStudio() {
    if (
      pathState.points >= 2 &&
      canvasRef.current &&
      !canvasRef.current.prepareExport()
    )
      return;
    canvasRef.current?.cancelInteraction();
    try {
      if (history.dirty || pathState.active) savePatternDraft(docRef.current);
    } catch {}
    onClose();
  }
  function changeTool(next) {
    if (
      !["pen", "curve", "node", "hand"].includes(next) &&
      canvasRef.current?.hasPendingPath()
    ) {
      if (pathState.points < 2) canvasRef.current.cancelInteraction();
      else if (!canvasRef.current.prepareExport()) return;
    }
    setTool(next);
  }
  function persistWorkingDraft(working) {
    try {
      savePatternDraft(working);
      setDraftStatus("Path draft saved locally");
    } catch {
      setDraftStatus("Storage unavailable — save a document");
    }
  }
  function selectLayer(index, event) {
    ConfigureMaskIdentity(null);
    rangeAnchor.current = Math.min(rangeAnchor.current, doc.layers.length - 1);
    const ids =
      event.altKey || doc.layers[index].locked || !doc.layers[index].visible
        ? [index]
        : expandPatternSelection(doc, index);
    if (event.shiftKey) {
      const first = Math.min(rangeAnchor.current, index),
        last = Math.max(rangeAnchor.current, index);
      setSelected([
        ...new Set([
          ...selected,
          ...Array.from({ length: last - first + 1 }, (_, j) => first + j),
        ]),
      ]);
    } else if (event.ctrlKey || event.metaKey) {
      setSelected(
        ids.every((i) => selected.includes(i))
          ? selected.filter((i) => !ids.includes(i))
          : [...new Set([...selected, ...ids])],
      );
    } else setSelected(ids);
    rangeAnchor.current = index;
  }
  function action(kind) {
    const current = docRef.current,
      ids = editableSelection(current, selected),
      chosen = new Set(ids);
    if (kind.startsWith("boolean-")) {
      try {
        if (canvasRef.current && !canvasRef.current.prepareExport()) return;
        const doc = docRef.current,
          ids = editableSelection(doc, selected).sort((a, b) => a - b);
        if (ids.length !== selected.length)
          throw new Error(
            "Unlock and show all selected layers before combining them.",
          );
        const result = booleanPatternLayers(
            ids.map((i) => doc.layers[i]),
            kind.slice(8),
          ),
          target = Math.min(...ids),
          chosen = new Set(ids);
        commit(
          {
            ...doc,
            layers: doc.layers.flatMap((l, i) =>
              i === target ? [result] : chosen.has(i) ? [] : [l],
            ),
          },
          { selected: [target] },
        );
        setTool("move");
        setError("");
      } catch (e) {
        setError(e.message);
      }
      return;
    }
    if (kind === "copy") {
      clipboard.current = structuredClone(
        selected.filter((i) => current.layers[i]).map((i) => current.layers[i]),
      );
      setDraftStatus(`${clipboard.current.length} shapes copied`);
      return;
    }
    if (kind === "paste" || kind === "duplicate") {
      const source =
        kind === "paste"
          ? clipboard.current
          : ids.map((i) => current.layers[i]);
      if (!source.length) return;
      const copies = clonePatternLayers(source);
      if (
        commit(
          { ...current, layers: [...current.layers, ...copies] },
          { selected: copies.map((_, i) => current.layers.length + i) },
        )
      )
        setTool("move");
      return;
    }
    if (kind === "delete") {
      const removable = new Set(
        selected.filter((i) => current.layers[i] && !current.layers[i].locked),
      );
      if (removable.size)
        commit(
          {
            ...current,
            layers: current.layers.filter((_, i) => !removable.has(i)),
          },
          { selected: [] },
        );
      return;
    }
    if (!ids.length) return;
    if (kind.startsWith("align-")) {
      commit(alignSelection(current, ids, kind.slice(6)));
      return;
    }
    if (kind.startsWith("distribute-")) {
      commit(distributeSelection(current, ids, kind.endsWith("x") ? "x" : "y"));
      return;
    }
    if (["front", "back", "forward", "backward"].includes(kind)) {
      const result = reorderSelection(current, ids, kind);
      commit(result.doc, { selected: result.selected });
      return;
    }
    if (kind === "group" || kind === "ungroup" || kind === "lock") {
      if (kind === "group" && ids.length < 2) return;
      if (kind === "group") { commit(CreatePatternEnclosure(current, ids, "Group")); return; }
      commit({
        ...current,
        layers: current.layers.map((p, i) =>
          !chosen.has(i)
            ? p
            : kind === "lock"
              ? { ...p, locked: true }
              : { ...p, group: undefined, EnclosureToken: current.Enclosures?.find(Entry => Entry.Token === p.EnclosureToken)?.EnclosureToken || undefined },
        ),
      });
      return;
    }

  }
  function keyDown(e) {
    e.stopPropagation();
    if (e.defaultPrevented) return;
    const command = e.ctrlKey || e.metaKey,
      key = e.key.toLowerCase(),
      typing = e.target.closest(
        "input,textarea,select,[contenteditable='true']",
      );
    if (command && key === "s") {
      e.preventDefault();
      saveDocument();
      return;
    }
    if (typing) {
      if (e.key === "Escape") {
        e.preventDefault();
        e.target.blur();
        canvasRef.current?.focus();
      }
      return;
    }
    if (MaskActive)
    {
      if (command && key === "z") { e.preventDefault(); e.shiftKey ? redo() : undo(); }
      else if (key === "escape") { e.preventDefault(); ConfigureMaskIdentity(null); }
      else if (["delete", "backspace"].includes(key)) { e.preventDefault(); layer({ mask: undefined }); }
      return;
    }
    if (canvasRef.current?.consumeKey(e)) {
      e.preventDefault();
      return;
    }
    if (command && key === "z") {
      e.preventDefault();
      e.shiftKey ? redo() : undo();
      return;
    }
    if (command && key === "y") {
      e.preventDefault();
      redo();
      return;
    }
    if (command && key === "a") {
      e.preventDefault();
      setSelected(
        doc.layers.flatMap((p, i) => (p.visible && !p.locked ? [i] : [])),
      );
      return;
    }
    if (command && ["c", "v", "d", "g", "x"].includes(key)) {
      e.preventDefault();
      if (key === "x") {
        action("copy");
        action("delete");
      } else
        action(
          {
            c: "copy",
            v: "paste",
            d: "duplicate",
            g: e.shiftKey ? "ungroup" : "group",
          }[key],
        );
      return;
    }
    if (e.key === "Escape") {
      closeStudio();
      return;
    }
    if (e.key === "Delete" || e.key === "Backspace") {
      e.preventDefault();
      action("delete");
      return;
    }
    if (
      ["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"].includes(e.key) &&
      editIds.length
    ) {
      e.preventDefault();
      const step = (grid || 1) * (e.shiftKey ? 10 : 1),
        delta = {
          ArrowLeft: { x: -step, y: 0 },
          ArrowRight: { x: step, y: 0 },
          ArrowUp: { x: 0, y: -step },
          ArrowDown: { x: 0, y: step },
        }[e.key],
        snapped = snappedTranslation(docRef.current, editIds, delta, {
          grid,
          guides: false,
        });
      commit(
        translateSelection(
          docRef.current,
          editIds,
          snapped.delta.x,
          snapped.delta.y,
        ),
        { key: "nudge-" + editIds.join("-") },
      );
      return;
    }
    if (e.key === "[" || e.key === "]") {
      e.preventDefault();
      action(
        e.key === "]"
          ? e.shiftKey
            ? "front"
            : "forward"
          : e.shiftKey
            ? "back"
            : "backward",
      );
      return;
    }
    const match = shapeTools.find(
      ([, , shortcut]) => shortcut.toLowerCase() === key,
    );
    if (!command && match) {
      e.preventDefault();
      changeTool(match[0]);
      setMode("design");
    }
    if (e.key === "?") {
      e.preventDefault();
      setShowHelp((v) => !v);
    }
  }
  async function exportPNG() {
    if (canvasRef.current && !canvasRef.current.prepareExport()) return;
    setBusy(true);
    setError("");
    try {
      const current = validatePattern(docRef.current),
        [w, h] = patternDimensions(current),
        aspect = current.designAspect || 1,
        scale = exportSize / Math.max(w * aspect, h),
        canvas = await rasterPatternSVG(
          patternSVG(current),
          Math.round(w * aspect * scale),
          Math.round(h * scale),
        ),
        blob = await new Promise((resolve) =>
          canvas.toBlob(resolve, "image/png"),
        );
      if (!blob) throw new Error("Could not encode this pattern as PNG.");
      patternDownload(blob, "pattern.png", "image/png");
    } catch (e) {
      setError(e.message);
    } finally {
      setBusy(false);
    }
  }
  const [mode, setMode] = useState(() =>
      new URLSearchParams(window.location.search).get("view") === "3d"
        ? "material"
        : "design",
    ),
    [grid, setGrid] = useState(0),
    [aspect, setAspect] = useState(false),
    [ring, setRing] = useState({ count: 6, radius: 160 });
  const [collectionFilter, setCollectionFilter] = useState(
    () =>
      patternStarterCatalog.find((p) => p.name === doc.name)?.group ||
      "Geometric constructions",
  );
  const [librarySearch, setLibrarySearch] = useState(""),
    [libraryPalette, setLibraryPalette] = useState("Indigo"),
    [libraryPage, setLibraryPage] = useState(0);
  const libraryMatches = useMemo(
    () =>
      patternStarterCatalog.filter(
        (p) =>
          (collectionFilter === "All patterns" ||
            p.group === collectionFilter) &&
          `${p.name} ${p.group} ${p.description || ""} ${p.study || ""}`
            .toLowerCase()
            .includes(librarySearch.trim().toLowerCase()),
      ),
    [collectionFilter, libraryPalette, librarySearch],
  );
  const libraryPages = Math.max(1, Math.ceil(libraryMatches.length / 24));
  const pageIndex = Math.min(libraryPage, libraryPages - 1);
  useEffect(
    () => setLibraryPage(0),
    [collectionFilter, libraryPalette, librarySearch],
  );
  const [generator, setGenerator] = useState({
    seed: 17,
    style: "geometric",
    count: 12,
  });
  useEffect(() => {
    const root = document.querySelector(".pe-overlay"),
      previous = document.activeElement;
    root.querySelector("[data-editor-autofocus]")?.focus();
    const trap = (e) => {
      if (e.key !== "Tab") return;
      const nodes = [
        ...root.querySelectorAll("button,input,select,textarea,[tabindex='0']"),
      ].filter((n) => !n.matches(":disabled") && n.getClientRects().length > 0);
      const first = nodes[0],
        last = nodes.at(-1);
      if (e.shiftKey && document.activeElement === first) {
        e.preventDefault();
        last.focus();
      } else if (!e.shiftKey && document.activeElement === last) {
        e.preventDefault();
        first.focus();
      }
    };
    root.addEventListener("keydown", trap);
    return () => {
      root.removeEventListener("keydown", trap);
      previous?.focus();
    };
  }, []);
  const [busy, setBusy] = useState(false),
    [base, setBase] = useState(() =>
      !initial && (doc.presentation === "rug" || doc.library)
        ? "natural-cotton"
        : "current",
    );
  const previewTarget = useMemo(
    () => resolvePatternBase(currentMaterial, base),
    [currentMaterial, base],
  );
  const input = useRef(null),
    svgInput = useRef(null),
    jsonInput = useRef(null);
  function add(kind, extra = {}) {
    const current = docRef.current;
    if (current.layers.length >= 64) { setError("The pattern already has 64 layers."); return false; }
    const contour =
      kind === "path" && extra.path ? svgPathNodes(extra.path) : null;
    const added = commit(
      {
        ...current,
        layers: [
          ...current.layers,
          patternLayer(kind, {
            ...drawingStyle,
            name: kind,
            ...extra,
            ...(kind === "path"
              ? {
                  fillEnabled: extra.fillEnabled ?? !extra.strokeWidth,
                  stroke: extra.stroke || extra.color || drawingStyle.color,
                  ...(contour || {}),
                }
              : {}),
          }),
        ],
      },
      { selected: [current.layers.length] },
    );
    if (added) setTool("move");
    return added;
  }
  const [repeatDocument, setRepeatDocument] = useState(doc);
  useEffect(() => {
    if (!history.previewing) {
      setRepeatDocument(doc);
      return;
    }
    const timer = setTimeout(() => setRepeatDocument(doc), 100);
    return () => clearTimeout(timer);
  }, [doc, history.previewing]);
  const preview = useMemo(
    () =>
      `data:image/svg+xml;charset=utf-8,${encodeURIComponent(patternSVG(repeatDocument))}`,
    [repeatDocument],
  );
  async function importFile(file, kind) {
    if (!file) return;
    setBusy(true);
    setError("");
    try {
      const ImportLimit = kind === "json" ? 12000000 : 4 * 1024 * 1024;
      if (file.size > ImportLimit)
        throw new Error(kind === "json" ? "Pattern document import limit: 12 MB." : "Image/SVG import limit: 4 MB per file.");
      if (kind === "json") {
        const parsed = validatePattern(JSON.parse(await file.text()));
        canvasRef.current?.cancelInteraction();
        commit(parsed, { IsolateStructure: true });
        setTool("move");
        if (parsed.presentation === "rug" || parsed.library)
          setBase("natural-cotton");
        setSelection(0);
        return;
      }
      if (kind === "svg") {
        const clean = sanitizePatternSVG(await file.text());
        // Keep simple path-only imports directly editable; preserve grouped SVG sources.
        const xml = new DOMParser().parseFromString(clean, "image/svg+xml");
        const paths = xml.querySelectorAll("path"),
          root = xml.documentElement,
          path = paths[0];
        const inherited = (key, fallback) =>
          path?.getAttribute(key) ?? root.getAttribute(key) ?? fallback;
        const fill = inherited("fill", "#000000"),
          stroke = inherited("stroke", "none"),
          fillColor = solidSVGColor(fill),
          strokeColor = solidSVGColor(stroke);
        const strokeWidth =
            stroke !== "none" ? Number(inherited("stroke-width", "1")) : 0,
          pathOpacity = Number(path?.getAttribute("opacity") ?? "1"),
          rootOpacity = Number(root.getAttribute("opacity") ?? "1");
        const simplePaint =
          inherited("vector-effect", "none") === "none" &&
          Number.isFinite(strokeWidth) &&
          strokeWidth >= 0 &&
          strokeWidth <= 25 &&
          [pathOpacity, rootOpacity].every(
            (v) => Number.isFinite(v) && v >= 0 && v <= 1,
          ) &&
          (fill === "none" || fillColor) &&
          (stroke === "none" || strokeColor) &&
          ["fill-opacity", "stroke-opacity"].every(
            (key) => Number(inherited(key, "1")) === 1,
          );
        if (
          paths.length === 1 &&
          root.children.length === 1 &&
          !path.hasAttribute("transform") &&
          !root.hasAttribute("transform") &&
          (root.getAttribute("viewBox") || "") === "0 0 100 100" &&
          simplePaint
        ) {
          add("path", {
            name: file.name,
            path: path.getAttribute("d"),
            color: fillColor || strokeColor || drawingStyle.color,
            stroke: strokeColor || "#222222",
            fillEnabled: fill !== "none",
            paint: true,
            strokeWidth,
            strokeLinecap: inherited("stroke-linecap", "butt"),
            strokeLinejoin: inherited("stroke-linejoin", "miter"),
            fillRule: inherited("fill-rule", "nonzero"),
            opacity: pathOpacity * rootOpacity,
          });
        } else {
          // Preserve gradients, per-paint alpha and complete grouped sources.
          add("svg", { name: file.name, svg: clean, width: 256, height: 256 });
        }
      } else {
        if (!["image/png", "image/jpeg", "image/webp"].includes(file.type))
          throw new Error("Use PNG, JPEG, WebP or the SVG import button.");
        const url = URL.createObjectURL(file);
        try {
          const image = await patternImage(url),
            c = document.createElement("canvas");
          const scale = Math.min(1, 2048 / Math.max(image.width, image.height));
          c.width = Math.round(image.width * scale);
          c.height = Math.round(image.height * scale);
          c.getContext("2d").drawImage(image, 0, 0, c.width, c.height);
          add("image", {
            name: file.name,
            src: c.toDataURL("image/png"),
            width: 200,
            height: (200 * image.height) / image.width,
          });
        } finally {
          URL.revokeObjectURL(url);
        }
      }
    } catch (e) {
      setError(e.message);
    } finally {
      setBusy(false);
    }
  }
  const shift = (direction) => action(direction > 0 ? "forward" : "backward");
  const field = (label, key, min, max, step = 1) => (
    <label className="pe-field" key={key}>
      {label}
      <input
        aria-label={label}
        type="number"
        min={min}
        max={max}
        step={step}
        value={Number((l?.[key] ?? 0).toFixed(3))}
        onChange={(e) => layer({ [key]: Number(e.target.value) })}
      />
    </label>
  );
  return (
    <div
      className="pe-overlay studio-themed studio-pattern"
      role="dialog"
      aria-modal="true"
      aria-label="Pattern studio"
      onKeyDown={keyDown}
    >
      <div className="pe-header" inert={svgSource ? true : undefined}>
        <StudioHeader
          workspace="pattern"
          name={doc.name}
          extension=".pattern"
          onWorkspace={(next) => {
            if (
              next !== "pattern" &&
              canvasRef.current &&
              !canvasRef.current.prepareExport()
            )
              return;
            if (next !== "pattern") canvasRef.current?.cancelInteraction();
            if (next !== "pattern") onWorkspace?.(next);
          }}
          onClose={closeStudio}
          closeLabel="Close pattern studio"
        >
          <button
            onClick={undo}
            disabled={!canUndo && !pathState.canUndo}
            title="Undo"
          >
            <Undo2 size={16} />
          </button>
          <button
            onClick={redo}
            disabled={!canRedo && !pathState.canRedo}
            title="Redo"
          >
            <Redo2 size={16} />
          </button>
          <button data-editor-autofocus aria-label="Save document" title="Save document" onClick={saveDocument}>
            <Download size={14} />
          </button>
          <ArtworkDocumentMenu>
          <button
            title="New pattern"
            onClick={() => {
              canvasRef.current?.cancelInteraction();
              commit(patternStarter("Blank"), { selected: [] });
              ConfigureLibraryCollapsed(false);
              ConfigureMaskIdentity(null);
              setTool("move");
              setMode("design");
            }}
          >
            <FilePlus2 size={15} /> New
          </button>
          <button
            aria-label="Editor shortcuts"
            title="Editor shortcuts (?)"
            aria-expanded={showHelp}
            onClick={() => setShowHelp((v) => !v)}
          >
            <CircleHelp size={16} />
          </button>
          </ArtworkDocumentMenu>
          <button
            className="pe-primary"
            disabled={busy}
            onClick={() => {
              if (canvasRef.current && !canvasRef.current.prepareExport())
                return;
              canvasRef.current?.cancelInteraction();
              try {
                savePatternDraft(docRef.current);
              } catch {}
              onApply(validatePattern(docRef.current), base);
            }}
          >
            Apply to material ↗
          </button>
        </StudioHeader>
      </div>

      {recoverable && (
        <div className="se-recovery" role="status">
          <span>
            <strong>Unsaved draft available</strong> {recoverable.doc.name} ·{" "}
            {recoverable.doc.layers.length} shapes
          </span>
          <button
            onClick={() => {
              canvasRef.current?.cancelInteraction();
              const pathIndex = recoverable.doc.layers.findLastIndex(
                (p) => p.nodes && !p.closed && !p.locked && p.visible,
              );
              commit(recoverable.doc, {
                selected: pathIndex >= 0 ? [pathIndex] : [],
              });
              setRecoverable(null);
              setTool("move");
            }}
          >
            Restore draft
          </button>
          <button
            onClick={() => {
              setRecoverable(null);
              try {
                localStorage.removeItem(PATTERN_DRAFT_KEY);
              } catch {}
            }}
          >
            Dismiss
          </button>
        </div>
      )}
      {showHelp && (
        <div
          className="se-shortcuts"
          role="region"
          aria-label="Editor shortcuts"
        >
          <strong>Made for your keyboard</strong>
          {[
            ["Select / Points", "V / A"],
            ["Rectangle / Ellipse", "R / O"],
            ["Pen / Curve / Freehand", "P / U / B"],
            ["Break tangent / Reposition", "Alt / Space-drag"],
            ["Join open paths", "Ctrl/⌘ J"],
            ["Pan / Fit", "Space / 0"],
            ["Multi-select", "Shift-click"],
            ["Duplicate / Group", "Ctrl/⌘ D / G"],
            ["Copy / Paste", "Ctrl/⌘ C / V"],
            ["Undo / Redo", "Ctrl/⌘ Z / Shift Z"],
            ["Nudge / Fast nudge", "Arrow / Shift Arrow"],
            ["Finish / Cancel path", "Enter / Esc"],
          ].map(([label, key]) => (
            <span key={label}>
              {label}
              <kbd>{key}</kbd>
            </span>
          ))}
          <button
            onClick={() => setShowHelp(false)}
            aria-label="Hide editor shortcuts"
          >
            <X size={15} />
          </button>
        </div>
      )}
      <div className="pe-body" inert={svgSource ? true : undefined}>
        <aside className="pe-library" data-collapsed={LibraryCollapsed}>
          <StudioDockTab icon={Layers3}>Pattern library<button className="pattern-library-toggle" aria-label={LibraryCollapsed ? "Show pattern library" : "Hide pattern library"} aria-expanded={!LibraryCollapsed} title={LibraryCollapsed ? "Show pattern library" : "Hide pattern library"} onClick={() => ConfigureLibraryCollapsed(!LibraryCollapsed)}>{LibraryCollapsed ? "+" : "−"}</button></StudioDockTab>
          <div className="se-library-scroll">
            <span className="pe-kicker">START WITH A STRUCTURE</span>
            <button
              className="pe-wide pe-rich-library"
              onClick={() => {
                setCollectionFilter("Geometric constructions");
                setLibraryPalette("Indigo");
                setLibrarySearch("");
              }}
            >
              Geometric constructions ↗
            </button>
            <button
              className="pe-wide pe-rich-library"
              onClick={() => {
                setCollectionFilter("Stitch patterns");
                setLibrarySearch("");
              }}
            >
              Stitches · one thread color ↗
            </button>
            <select
              className="pe-collection-filter"
              aria-label="Pattern collection"
              value={collectionFilter}
              onChange={(e) => setCollectionFilter(e.target.value)}
            >
              {libraryGroups.map((g) => (
                <option key={g}>{g}</option>
              ))}
            </select>
            <input
              className="pe-library-search"
              type="search"
              aria-label="Search patterns"
              placeholder="Search weaves, stitches, checks…"
              value={librarySearch}
              onChange={(e) => setLibrarySearch(e.target.value)}
            />
            {![
              "Fabric weaves",
              "Stitch patterns",
              "Islamic geometry",
              "African traditional",
            ].includes(collectionFilter) && (
              <>
                <span className="pe-hint">
                  Print palette · not a separate pattern
                </span>
                <select
                  aria-label="Textile family colorway"
                  title="Color option for printed basic families only"
                  value={libraryPalette}
                  onChange={(e) => setLibraryPalette(e.target.value)}
                >
                  {Object.keys(textilePalettes).map((p) => (
                    <option key={p}>{p}</option>
                  ))}
                </select>
              </>
            )}
            {["Islamic geometry", "African traditional"].includes(
              collectionFilter,
            ) && (
              <p className="pe-hint pe-heritage-note">
                Original vector studies inspired by named traditions, not
                historical reproductions. Edit colours in Design palette.
              </p>
            )}
            <p className="pe-library-count" aria-live="polite">
              {libraryMatches.length} results · one card per family
            </p>
            <div
              className="pe-starters"
              key={`${collectionFilter}-${libraryPalette}-${librarySearch}-${pageIndex}`}
            >
              {libraryMatches
                .slice(pageIndex * 24, pageIndex * 24 + 24)
                .map(({ name, description }) => (
                  <button
                    key={name}
                    title={description || name}
                    onClick={() => {
                      canvasRef.current?.cancelInteraction();
                      const next = patternStarter(name, libraryPalette);
                      commit(next);
                      setTool("move");
                      if (next.presentation === "rug" || !!next.library)
                        setBase("natural-cotton");
                      setSelection(0);
                    }}
                  >
                    <StarterThumb name={name} palette={libraryPalette} />
                    <span>{name}</span>
                  </button>
                ))}
            </div>
            {!libraryMatches.length && (
              <p className="pe-hint">
                No matches. Try another collection or search.
              </p>
            )}
            <div className="pe-library-paging">
              <button
                aria-label="Previous pattern page"
                disabled={pageIndex === 0}
                onClick={() => setLibraryPage(pageIndex - 1)}
              >
                ←
              </button>
              <span>
                Page {pageIndex + 1} / {libraryPages}
              </span>
              <button
                aria-label="Next pattern page"
                disabled={pageIndex + 1 >= libraryPages}
                onClick={() => setLibraryPage(pageIndex + 1)}
              >
                →
              </button>
            </div>
            <p className="pe-hint">
              Stitches and weaves use one thread/yarn color. Backgrounds,
              colors, direction and draft variants do not add library cards. The
              rejected rug collections have been removed.
            </p>
            <div className="pe-section">
              <h3>Generate a layout</h3>
              <select
                aria-label="Generator style"
                value={generator.style}
                onChange={(e) =>
                  setGenerator({ ...generator, style: e.target.value })
                }
              >
                <option value="geometric">Geometric motifs</option>
                <option value="floral">Scattered flowers</option>
              </select>
              <div className="pe-grid">
                <label>
                  Seed
                  <input
                    aria-label="Layout seed"
                    type="number"
                    value={generator.seed}
                    onChange={(e) =>
                      setGenerator({ ...generator, seed: +e.target.value })
                    }
                  />
                </label>
                <label>
                  Motifs
                  <input
                    aria-label="Layout count"
                    type="number"
                    min="2"
                    max="24"
                    value={generator.count}
                    onChange={(e) =>
                      setGenerator({ ...generator, count: +e.target.value })
                    }
                  />
                </label>
              </div>
              <button
                className="pe-wide"
                onClick={() => {
                  canvasRef.current?.cancelInteraction();
                  commit(generatePatternLayout(generator));
                  setSelection(0);
                }}
              >
                Generate pattern
              </button>
              <h3 style={{ marginTop: 24 }}>Draw & import</h3>
              <div className="pe-shapes">
                {[
                  "rect",
                  "ellipse",
                  "diamond",
                  "triangle",
                  "flower",
                  "path",
                ].map((kind) => (
                  <button
                    key={kind}
                    onClick={() =>
                      add(
                        kind,
                        kind === "path"
                          ? { path: "M10 80 Q50 0 90 80", strokeWidth: 3 }
                          : {},
                      )
                    }
                  >
                    <Plus size={12} />
                    {kind}
                  </button>
                ))}
              </div>
              <button
                className="pe-wide"
                onClick={() => input.current.click()}
                disabled={busy}
              >
                <Upload size={14} /> Import image
              </button>
              <button
                className="pe-wide"
                onClick={() => svgInput.current.click()}
                disabled={busy}
              >
                <Upload size={14} /> Import SVG
              </button>
              <button className="pe-wide" onClick={() => openSVGSource()}>
                <Spline size={14} />
                Paste SVG source
              </button>
              <button
                className="pe-wide"
                onClick={() => jsonInput.current.click()}
                disabled={busy}
              >
                Open document
              </button>
              <input
                hidden
                type="file"
                ref={input}
                aria-label="Import pattern image"
                accept="image/png,image/jpeg,image/webp"
                onChange={(e) => {
                  importFile(e.target.files[0], "image");
                  e.target.value = "";
                }}
              />
              <input
                hidden
                type="file"
                ref={svgInput}
                accept=".svg"
                onChange={(e) => {
                  importFile(e.target.files[0], "svg");
                  e.target.value = "";
                }}
              />
              <input
                hidden
                type="file"
                ref={jsonInput}
                aria-label="Open pattern JSON"
                accept=".json"
                onChange={(e) => {
                  importFile(e.target.files[0], "json");
                  e.target.value = "";
                }}
              />
              <p className="pe-hint">
                Simple 100 × 100 SVG paths stay editable. Other supported SVG
                groups retain their vector source. Paste and edit self-contained
                SVG markup; no executable scripts or linked files.
              </p>
            </div>
          </div>

        </aside>
        <LayerStackPanel Caption="Layers" RegionLabel="Pattern layers" Detail={`${doc.layers.length} / 64`} className="pe-layers-dock">
          <div className="tp-pane-heading pattern-layer-heading">
            <h1>Motifs</h1>
            <div className="pattern-layer-add" ref={LayerAddPanel}>
              <button className="tp-round-icon" aria-label="Add pattern layer" aria-expanded={LayerAddOpen} aria-controls="pattern-layer-additions" disabled={doc.layers.length >= 64}
                title="Add a shape or editable preset without replacing this pattern."
                data-tooltip-title="Add a layer" data-tooltip-body="Choose a shape or an editable motif preset. Your existing layers stay in place."
                onClick={() => SetLayerAddOpen(!LayerAddOpen)}><Plus size={18}/></button>
              {LayerAddOpen && <div id="pattern-layer-additions" ref={LayerAddChoices} className="pattern-layer-additions" style={{ ...LayerAddPosition, visibility: LayerAddPosition ? "visible" : "hidden" }} role="group" aria-label="Add a pattern motif">
                <h4>Shapes</h4><div className="pattern-layer-shapes">{shapeTools.filter(([Kind]) => ["rect", "ellipse", "triangle", "diamond", "polygon", "star"].includes(Kind)).map(([Kind, Caption, , Icon]) =>
                  <button key={Kind} onClick={() => { if (add(Kind)) { SetLayerQuery(""); SetLayerFilter("all"); SetLayerAddOpen(false); } }}><Icon size={15}/>{Caption.replace(" tool", "")}</button>)}</div>
                <h4>Editable presets</h4>{[
                  ["Rosette", "star", { starPoints: 12, innerRadius: .65, width: 128, height: 128 }],
                  ["Ribbon", "rect", { width: 240, height: 48, cornerRadius: 20, rotation: -30 }],
                  ["Ring", "ellipse", { width: 140, height: 140, fillEnabled: false, strokeWidth: 8 }],
                ].map(([Caption, Kind, Preset]) => <button key={Caption} onClick={() => { if (add(Kind, { ...Preset, name: Caption })) { SetLayerQuery(""); SetLayerFilter("all"); SetLayerAddOpen(false); } }}>{Caption}</button>)}
              </div>}
            </div>
          </div>
          <PatternOrganizationControls Document={doc} Selection={selected} OnFinalize={commit} OnSelect={setSelected} OnError={setError} Scope={EnclosureScope} OnScope={ConfigureEnclosureScope}/>
          <LayerStackFilters Query={LayerQuery} OnQueryChange={SetLayerQuery} Filter={LayerFilter} OnFilterChange={SetLayerFilter}
            VisibleCount={doc.layers.filter(Layer => Layer.visible).length} HiddenCount={doc.layers.filter(Layer => !Layer.visible).length} LockedCount={doc.layers.filter(Layer => Layer.locked).length}/>
          <div className="pe-section se-layer-panel">
            <div className="se-layer-caption">
              Top layers render in front · drag to reorder
            </div>
            <div className="pe-layers" role="list" aria-label="Pattern layers">
              {LayerMatches
                .map(({ Layer: p, Index: i }) => (
                  <React.Fragment key={p.id}><div
                    key={p.id}
                    role="listitem"
                    className={
                      "tp-layer-row " +
                      (selected.includes(i) && !MaskActive ? "selected " : "") +
                      (!p.visible ? "hidden-row " : "") +
                      (p.locked ? "is-locked" : "")
                    }
                    draggable={!p.locked}
                    onDragStart={(e) => {
                      layerDrag.current = selected.includes(i)
                        ? selected.filter((j) => !doc.layers[j].locked)
                        : expandPatternSelection(doc, i);
                      e.dataTransfer.effectAllowed = "move";
                      e.dataTransfer.setData("text/plain", String(i));
                    }}
                    onDragOver={(e) => {
                      if (layerDrag.current?.length) {
                        e.preventDefault();
                        e.dataTransfer.dropEffect = "move";
                      }
                    }}
                    onDrop={(e) => {
                      e.preventDefault();
                      const ids = layerDrag.current;
                      layerDrag.current = null;
                      if (!ids?.length || ids.includes(i)) return;
                      const chosen = new Set(ids),
                        entries = doc.layers.map((p, index) => ({
                          layer: p,
                          index,
                        })),
                        retained = entries.filter((e) => !chosen.has(e.index)),
                        moved = entries.filter((e) => chosen.has(e.index)),
                        at = retained.findIndex((e) => e.index === i) + 1;
                      retained.splice(at, 0, ...moved);
                      commit(
                        { ...doc, layers: retained.map((e) => e.layer) },
                        {
                          selected: retained.flatMap((e, index) =>
                            chosen.has(e.index) ? [index] : [],
                          ),
                        },
                      );
                    }}
                    onDragEnd={() => {
                      layerDrag.current = null;
                    }}
                  >
                    <button
                      className="tp-layer-drag"
                      aria-label={`Drag ${p.name}`}
                      title="Drag to reorder"
                      disabled={!!p.locked}
                      draggable={!p.locked}
                      onClick={(e) => e.stopPropagation()}
                    >
                      <GripVertical size={12} />
                    </button>
                    <button
                      className="tp-layer-select"
                      aria-pressed={selected.includes(i) && !MaskActive}
                      aria-label={p.name}
                      title={p.name + (p.group ? " · grouped" : "")}
                      onClick={(e) => selectLayer(i, e)}
                    >
                      <PatternLayerThumbnail layer={p} size={34} FallbackIcon={shapeTools.find(([Category]) => Category === p.kind)?.[3] || Layers3} />
                      <span><strong>{p.name}</strong><small>{p.kind.toUpperCase()} · {Math.round((p.opacity ?? 1) * 100)}%{p.group ? " · grouped" : ""}</small></span>
                    </button>
                    <button
                      className="tp-layer-eye"
                      aria-label={`${p.visible ? "Hide" : "Show"} ${p.name}`}
                      title={p.visible ? "Hide layer" : "Show layer"}
                      onClick={() =>
                        commit({
                          ...doc,
                          layers: doc.layers.map((q, j) =>
                            j === i ? { ...q, visible: !q.visible } : q,
                          ),
                        })
                      }
                    >
                      {p.visible ? <Eye size={13} /> : <EyeOff size={13} />}
                    </button>
                    <button
                      className={`tp-layer-lock ${p.locked ? "locked" : ""}`}
                      aria-label={`${p.locked ? "Unlock" : "Lock"} ${p.name}`}
                      title={p.locked ? "Unlock layer" : "Lock layer"}
                      onClick={() =>
                        commit({
                          ...doc,
                          layers: doc.layers.map((q, j) =>
                            j === i ? { ...q, locked: !q.locked } : q,
                          ),
                        })
                      }
                    >
                      {p.locked ? <LockKeyhole size={12} /> : <UnlockKeyhole size={12} />}
                    </button>
                  </div><ArtworkMaskStackRow Mask={p.mask} Name={p.name} Selected={MaskActive && MaskIdentity === p.id} Locked={p.locked}
                    OnSelect={() => { if (canvasRef.current?.hasPendingPath() && !canvasRef.current.prepareExport()) return; setSelected([i]); ConfigureMaskIdentity(p.id); setMode("design"); }}
                    OnChange={Mask => commit({ ...docRef.current, layers: docRef.current.layers.map(Entry => Entry.id === p.id ? { ...Entry, mask: Mask } : Entry) })}/></React.Fragment>
                ))
                .reverse()}
            </div>
            {!!doc.layers.length && !LayerMatches.length && <p className="pe-hint">No matching layers. <button onClick={() => { SetLayerQuery(""); SetLayerFilter("all"); }}>Clear filters</button></p>}
            {!doc.layers.length && (
              <p className="pe-hint">
                Draw your first shape to start a pattern.
              </p>
            )}
          </div>
        </LayerStackPanel>
        <main className="pe-workspace" data-mask-editing={MaskActive || undefined}>
          <StudioDockTab icon={Spline}>{mode === "material" ? "3D viewport" : "2D viewport"}</StudioDockTab>
          <div className="pe-mode-switch" role="group" aria-label="Editor view">
            <button
              aria-pressed={mode === "design"}
              onClick={() => { ConfigureMaskIdentity(null); setMode("design"); }}
            >
              Design tile
            </button>
            <button
              aria-pressed={mode === "material"}
              onClick={() => {
                if (canvasRef.current && !canvasRef.current.prepareExport())
                  return;
                canvasRef.current?.cancelInteraction();
                ConfigureMaskIdentity(null);
                setMode("material");
              }}
            >
              3D material
            </button>
            <span>Compose in 2D. See it on a real surface.</span>
          </div>
          {patternStarterCatalog.find((p) => p.name === doc.name)
            ?.description && (
            <p className="pe-hint" style={{ margin: "12px 0 0" }}>
              Construction:{" "}
              {
                patternStarterCatalog.find((p) => p.name === doc.name)
                  .description
              }
            </p>
          )}
          <div className="pe-canvas-heading">
            <div>
              <span className="pe-kicker">
                {doc.tileAxes === "none"
                  ? "FINITE RUG / 512 UNITS"
                  : doc.tileAxes === "xy"
                    ? "REPEAT TILE / 512 UNITS"
                    : "ONE-WAY BORDER / 512 UNITS"}
              </span>
              <input
                aria-label="Pattern name"
                value={doc.name}
                onChange={(e) => update({ name: e.target.value })}
              />
            </div>
            <span className="se-draft-status" role="status">
              <Check size={12} />
              {draftStatus}
            </span>
          </div>
          {MaskActive ? <ArtworkMaskCanvas key={l.id} Mask={l.mask} Name={l.name} Disabled={l.locked} OnSave={saveDocument} OnClose={() => ConfigureMaskIdentity(null)} OnError={setError}
            OnHistory={Direction => Direction === "undo" ? undo() : redo()}
            OnChange={Mask => { if (!l.locked) commit({ ...docRef.current, layers: docRef.current.layers.map(Entry => Entry.id === l.id ? { ...Entry, mask: Mask } : Entry) }); }}/>
          : mode === "material" ? (
            <PatternMaterialPreview
              doc={doc}
              target={previewTarget}
              initialShape={base === "current" ? currentShape : undefined}
            />
          ) : (
            <>
              <ShapeEditorToolbar
                tool={tool}
                onToolChange={changeTool}
                grid={grid}
                onGridChange={(v) => {
                  setGrid(v);
                  if (v) setShowGrid(true);
                }}
                showGrid={showGrid}
                onGridToggle={() => setShowGrid((v) => !v)}
                smartGuides={smartGuides}
                onGuidesToggle={() => setSmartGuides((v) => !v)}
                aspect={aspect}
                onAspectChange={setAspect}
                snapping={snapping}
                onSnappingChange={setSnapping}
                onSVGSource={() => openSVGSource()}
              />
              <ShapeEditorCanvas
                ref={canvasRef}
                doc={doc}
                selected={selected}
                tool={tool}
                onToolChange={changeTool}
                grid={grid}
                aspect={aspect}
                showGrid={showGrid}
                smartGuides={smartGuides}
                snapping={snapping}
                style={drawingStyle}
                onDrawingStyle={(patch) =>
                  setDrawingStyle((p) => ({ ...p, ...patch }))
                }
                onCreateText={createText}
                onEditText={editText}
                onSampleAppearance={sampleAppearance}
                onSelection={setSelected}
                onPreview={previewDocument}
                onCommit={commit}
                onCancel={cancelDocument}
                onError={setError}
                onAction={action}
                onPathStateChange={setPathState}
                onWorkingDraft={persistWorkingDraft}
                onPathDirty={(points) =>
                  setDraftStatus(
                    points < 2 ? "Place another anchor" : "Saving path draft…",
                  )
                }
                pathPanelTarget={pathPanelTarget}
              />
            </>
          )}
          <div className="pe-repeat-heading">
            <span>
              <span className="se-live-dot" /> Seamless repeat preview
            </span>
            <small>
              {doc.tileAxes === "xy"
                ? "Edges wrap automatically • drag motifs across boundaries"
                : doc.tileAxes === "none"
                  ? "Finite rug composition • edges do not wrap"
                  : "One-way border • repeat across the fade only"}
            </small>
          </div>
          <div
            className="pe-repeat"
            aria-label="Repeated pattern preview"
            style={{
              backgroundImage: `url("${preview}")`,
              backgroundPosition: "center",
              backgroundRepeat:
                doc.tileAxes === "none"
                  ? "no-repeat"
                  : doc.tileAxes === "x"
                    ? "repeat-x"
                    : doc.tileAxes === "y"
                      ? "repeat-y"
                      : "repeat",
              backgroundSize:
                doc.tileAxes === "none"
                  ? "contain"
                  : doc.tileAxes !== "xy"
                    ? "160px 160px"
                    : doc.repeat === "mirror"
                      ? "240px 240px"
                      : doc.repeat === "half-drop"
                        ? "240px 120px"
                        : "120px 120px",
            }}
          />
          {error && (
            <p className="pe-error" role="alert">
              {error}
            </p>
          )}
          {busy && <p role="status">Preparing embedded source…</p>}
        </main>
        <aside className="pe-inspector" data-mask-editing={MaskActive || undefined}>
          <StudioDockTab icon={Layers3}>Inspector</StudioDockTab>
          {!MaskActive && (
              <ShapeSelectionActions
                count={selected.length}
                hasGroup={editIds.some((i) => doc.layers[i].group)}
                canBoolean={
                  editIds.length >= 2 &&
                  editIds.length <= 16 &&
                  editIds.length === selected.length &&
                  editIds.every(Index =>
                  {
                    const Geometry = doc.layers[Index];
                    const Contours = layerWorldContours(Geometry);
                    return !["image", "svg"].includes(Geometry.kind) &&
                      (Geometry.paint ? Geometry.fillEnabled !== false : !(Geometry.kind === "path" && Geometry.strokeWidth)) &&
                      Contours.length > 0 && Contours.every(Contour => Contour.closed);
                  })
                }
                MaskNames={selected.length === 2 && editIds.length === 2 ? selected.map(Index => doc.layers[Index].name) : null}
                OnUseMask={async SourceOffset => {
                  const Snapshot = docRef.current;
                  try {
                    const TargetIndex = selected[1 - SourceOffset];
                    const Next = await ArtworkSelectionMask(Snapshot, TargetIndex, selected[SourceOffset]);
                    if (docRef.current !== Snapshot || SelectionRevision.current !== selected) throw new Error("Selection changed; try again.");
                    commit(Next, { selected: [TargetIndex] }); ConfigureMaskIdentity(Snapshot.layers[TargetIndex].id);
                  } catch (Failure) { setError(Failure.message); }
                }}
                onAction={action}
              />
          )}
          {!MaskActive && <PatternSurfaceChannelControls Document={doc} Selection={selected} OnFinalize={commit} OnError={setError}/>}
          {!MaskActive && <ArtworkArrayControls Document={doc} Selection={selected} OnError={setError} OnFinalize={(Next, Indices) => { commit(Next, { selected: Indices }); setTool("move"); }}/>}
          <div className="se-path-panel" ref={setPathPanelTarget} />
          {(tool === "text" || l?.kind === "text") && (
            <PatternTextControls
              text={l?.kind === "text" ? l.text : textDefaults}
              onChange={changeText}
              ready={fontsReady}
              selected={l?.kind === "text"}
              disabled={!!l?.locked}
              error={textError}
              focusToken={textFocus}
              onOutline={() => {
                layer(convertPatternLayerToPath(l));
                setTool("move");
              }}
            />
          )}
          {(!l ||
            selected.length > 1 ||
            !["move", "node", "hand"].includes(tool)) && (
            <div className="pe-section se-drawing-panel">
              <span className="pe-kicker">
                {selected.length > 1
                  ? "MULTIPLE SELECTION"
                  : "DRAWING DEFAULTS"}
              </span>
              <h3>
                {selected.length > 1
                  ? `${selected.length} shapes selected`
                  : "Your next shape"}
              </h3>
              <p className="pe-hint">
                {selected.length > 1
                  ? "Shared edits apply to unlocked selected shapes. Mixed styles show the first unlocked shape as the editing reference."
                  : "Pick a tool and draw on the canvas. These are the style and material of new shapes."}
              </p>
              {selected.length > 1 && <label>Opacity<input aria-label="Selection opacity" type="number" min="0" max="1" step=".05" placeholder="Mixed"
                value={selected.every(Index => doc.layers[Index].opacity === doc.layers[selected[0]].opacity) ? doc.layers[selected[0]].opacity : ""}
                onChange={Event => { if (Event.target.value !== "") commit({ ...docRef.current, layers: docRef.current.layers.map((Layer, Index) => editIds.includes(Index) ? { ...Layer, opacity: Math.max(0, Math.min(1, Number(Event.target.value))) } : Layer) }); }}/></label>}
              {multiBounds && (
                <div className="pe-grid">
                  {[
                    ["x", "Selection X"],
                    ["y", "Selection Y"],
                    ["width", "Selection width"],
                    ["height", "Selection height"],
                  ].map(([key, label]) => (
                    <label key={key}>
                      {label.replace("Selection ", "")}
                      <input
                        aria-label={label}
                        type="number"
                        min={key === "x" || key === "y" ? -512 : 1}
                        max="1024"
                        value={Number(multiBounds[key].toFixed(3))}
                        onChange={(e) =>
                          bulkGeometry(key, Number(e.target.value))
                        }
                      />
                    </label>
                  ))}
                </div>
              )}
              <PatternPropertyCard Caption="Drawing fill">
              <PatternColourControls Caption="Drawing fill" ColourLabel="Drawing fill color" Source={drawingStyle.fillSource} Solid={drawingStyle.color}
                OnChange={(Source, Colour) => ChangeDrawingSource("fill", Source, Colour)}/>
              <label className="pe-check">
                <input
                  aria-label="Drawing fill enabled"
                  type="checkbox" role="switch"
                  checked={drawingStyle.fillEnabled}
                  onChange={(e) => {
                    setDrawingStyle({
                      ...drawingStyle,
                      fillEnabled: e.target.checked,
                    });
                    if (selected.length > 1)
                      commit({
                        ...doc,
                        layers: doc.layers.map((p, i) =>
                          editIds.includes(i)
                            ? {
                                ...p,
                                paint: true,
                                fillEnabled: e.target.checked,
                              }
                            : p,
                        ),
                      });
                  }}
                />{" "}
                Fill enabled
              </label>
              </PatternPropertyCard>
              <PatternPropertyCard Caption="Drawing stroke">
              <PatternColourControls Caption="Drawing stroke" ColourLabel="Drawing stroke color" Source={drawingStyle.strokeSource} Solid={drawingStyle.stroke}
                OnChange={(Source, Colour) => ChangeDrawingSource("stroke", Source, Colour)}/>
              <label>
                Stroke width
                <input
                  aria-label="Drawing stroke width"
                  type="number"
                  min="0"
                  max="25"
                  step="0.1"
                  value={drawingStyle.strokeWidth}
                  onChange={(e) => {
                    const value = Math.max(
                      0,
                      Math.min(25, Number(e.target.value)),
                    );
                    setDrawingStyle({ ...drawingStyle, strokeWidth: value });
                    if (selected.length > 1)
                      commit({
                        ...doc,
                        layers: doc.layers.map((p, i) =>
                          editIds.includes(i)
                            ? { ...p, paint: true, strokeWidth: value }
                            : p,
                        ),
                      });
                  }}
                />
              </label>
              </PatternPropertyCard>
              <label>
                Material finish
                <select
                  aria-label="Drawing material finish"
                  value={drawingStyle.finish}
                  onChange={(e) => {
                    const finish = e.target.value,
                      f = patternFinishes[finish];
                    setDrawingStyle({
                      ...drawingStyle,
                      finish,
                      roughness: f.roughness,
                      metalness: f.metalness,
                      relief: f.height,
                    });
                    if (selected.length > 1)
                      commit({
                        ...doc,
                        layers: doc.layers.map((p, i) =>
                          editIds.includes(i)
                            ? {
                                ...p,
                                finish,
                                roughness: f.roughness,
                                metalness: f.metalness,
                                relief: f.height,
                              }
                            : p,
                        ),
                      });
                  }}
                >
                  {Object.entries(patternFinishes).map(([key, f]) => (
                    <option key={key} value={key}>
                      {f.name}
                    </option>
                  ))}
                </select>
              </label>
              {tool === "polygon" && (
                <label>
                  Sides
                  <input
                    aria-label="Drawing polygon sides"
                    type="number"
                    min="3"
                    max="24"
                    value={drawingStyle.sides}
                    onChange={(e) =>
                      setDrawingStyle({
                        ...drawingStyle,
                        sides: Math.max(
                          3,
                          Math.min(24, Number(e.target.value)),
                        ),
                      })
                    }
                  />
                </label>
              )}
              {tool === "star" && (
                <div className="pe-grid">
                  <label>
                    Points
                    <input
                      aria-label="Drawing star points"
                      type="number"
                      min="3"
                      max="20"
                      value={drawingStyle.starPoints}
                      onChange={(e) =>
                        setDrawingStyle({
                          ...drawingStyle,
                          starPoints: Math.max(
                            3,
                            Math.min(20, Number(e.target.value)),
                          ),
                        })
                      }
                    />
                  </label>
                  <label>
                    Inner radius
                    <input
                      aria-label="Drawing star inner radius"
                      type="number"
                      min="0.05"
                      max="0.95"
                      step="0.05"
                      value={drawingStyle.innerRadius}
                      onChange={(e) =>
                        setDrawingStyle({
                          ...drawingStyle,
                          innerRadius: Math.max(
                            0.05,
                            Math.min(0.95, Number(e.target.value)),
                          ),
                        })
                      }
                    />
                  </label>
                </div>
              )}
            </div>
          )}
          <RugCompositionControls
            doc={doc}
            commit={commit}
            onError={setError}
          />
          <PatternTextileTools doc={doc} commit={commit} onError={setError} />

          {doc.fade && (
            <div className="pe-section pe-fade-controls">
              <span className="pe-kicker">GEOMETRIC FADING</span>
              <p className="pe-hint">
                Motifs grow from separated marks into a connected lattice. Color
                stays opaque.
              </p>
              <label>
                Fade direction
                <select
                  aria-label="Fade direction"
                  value={doc.fade.direction}
                  onChange={(e) =>
                    commit(
                      rebuildCollectionFade(doc, { direction: e.target.value }),
                    )
                  }
                >
                  <option value="down">Small → large, downward</option>
                  <option value="up">Small → large, upward</option>
                  <option value="right">Small → large, rightward</option>
                  <option value="left">Small → large, leftward</option>
                </select>
              </label>
              {[
                ["columns", "Motif density", 4, 22, 1],
                ["strength", "Fade strength", 0.3, 3, 0.1],
                ["minimum", "Smallest motif", 0, 0.6, 0.01],
                ["gap", "Lattice gap", 0.01, 0.35, 0.01],
                ["start", "Fade start", 0, 0.9, 0.01],
                ["end", "Fade end", 0.1, 1, 0.01],
              ].map(([key, label, min, max, step]) => (
                <label key={key}>
                  {label}
                  <div className="pe-fade-range">
                    <input
                      aria-label={label}
                      type="range"
                      min={min}
                      max={max}
                      step={step}
                      value={doc.fade[key]}
                      onChange={(e) =>
                        commit(
                          rebuildCollectionFade(doc, {
                            [key]: +e.target.value,
                          }),
                        )
                      }
                    />
                    <output>{doc.fade[key]}</output>
                  </div>
                </label>
              ))}
              <label className="pe-check">
                <input
                  type="checkbox" role="switch"
                  aria-label="Loop fade seamlessly"
                  checked={doc.fade.loop}
                  onChange={(e) =>
                    commit(
                      rebuildCollectionFade(doc, { loop: e.target.checked }),
                    )
                  }
                />{" "}
                Loop fade seamlessly
              </label>
              <p className="pe-hint">
                {doc.fade.loop
                  ? "Mirrored density envelope: repeats on both axes."
                  : "One-way border: repeats across the fade, not along it. The terminal edges are clamped in 3D."}{" "}
                Geometry controls regenerate the facet layers; existing colors
                and finishes are retained.
              </p>
            </div>
          )}

          <div className="pe-section se-surface-panel">
            <span className="pe-kicker">MATERIAL & REPEAT</span>
            <label>
              Apply on
              <select
                aria-label="Pattern base material"
                value={base}
                onChange={(e) => setBase(e.target.value)}
              >
                <option value="current">Current material</option>
                <option value="pottery">Glazed pottery / teapot</option>
                <option value="natural-cotton">Cotton / rug backing</option>
                <option value="plain-linen">Linen</option>
                <option value="porcelain-grid-tiles">Porcelain tiles</option>
              </select>
            </label>
            <label>
              Repeat layout
              <select
                aria-label="Repeat layout"
                disabled={doc.tileAxes !== "xy"}
                value={doc.repeat}
                onChange={(e) => update({ repeat: e.target.value })}
              >
                <option value="straight">Straight</option>
                <option value="half-drop">Half drop</option>
                <option value="mirror">Mirrored</option>
              </select>
            </label>
            <label>
              Mapping
              <select
                aria-label="Pattern mapping"
                value={doc.mapping}
                onChange={(e) => update({ mapping: e.target.value })}
              >
                <option value="uv">Mesh UV — fabrics / decoration</option>
                <option value="object">Object projection</option>
                <option value="cylinder">Cylindrical — pottery</option>
              </select>
            </label>
            <div className="pe-grid">
              <label>
                Repeats
                <input
                  aria-label="Pattern repeats"
                  type="number"
                  min=".25"
                  max="24"
                  step=".25"
                  value={doc.repeats}
                  onChange={(e) => update({ repeats: +e.target.value })}
                />
              </label>
              <label>
                Rotation
                <input
                  aria-label="Pattern rotation"
                  type="number"
                  min="-180"
                  max="180"
                  value={doc.rotation}
                  onChange={(e) => update({ rotation: +e.target.value })}
                />
              </label>
            </div>
            <PatternColourControls Caption="Ground" ColourLabel="Pattern ground color" Source={doc.backgroundSource} Solid={doc.background}
              OnChange={(Source, Colour) => update({ backgroundSource: Source, background: Colour })}/>
            <label className="pe-check">
              <input
                type="checkbox" role="switch"
                checked={!doc.backgroundOpacity}
                onChange={(e) =>
                  update({ backgroundOpacity: e.target.checked ? 0 : 1 })
                }
              />{" "}
              Transparent ground / decal
            </label>
          </div>
          {l && (
            <div className="pe-section se-selected-panel">
              <h3>
                {MaskActive ? "Layer mask" : "Selected motif"} <small>{MaskActive ? l.name : l.kind}</small>
              </h3>
              {l.locked && (
                <div className="se-lock-notice">
                  <Lock size={13} /> Locked layer{" "}
                  <button
                    onClick={() =>
                      commit({
                        ...doc,
                        layers: doc.layers.map((p, i) =>
                          i === selection ? { ...p, locked: false } : p,
                        ),
                      })
                    }
                  >
                    Unlock
                  </button>
                </div>
              )}
              <fieldset disabled={!!l.locked} className="se-property-fields">
                <ArtworkMaskControls Mask={l.mask} Editing={MaskActive} OnEdit={() => { if (canvasRef.current?.hasPendingPath() && !canvasRef.current.prepareExport()) return; ConfigureMaskIdentity(l.id); setMode("design"); }} OnChange={Mask => { if (!l.locked) commit({ ...docRef.current, layers: docRef.current.layers.map(Entry => Entry.id === l.id ? { ...Entry, mask: Mask } : Entry) }); }} OnError={setError}/>
                <PatternPropertyCard Caption="Transform">
                <input
                  aria-label="Layer name"
                  value={l.name}
                  onChange={(e) => layer({ name: e.target.value })}
                />
                <div className="pe-grid">
                  {field("X", "x", -512, 1024)}
                  {field("Y", "y", -512, 1024)}
                  {field("Width", "width", 1, 1024)}
                  {field("Height", "height", 1, 1024)}
                  {field("Angle", "rotation", -360, 360)}
                  {field("Opacity", "opacity", 0, 1, 0.05)}
                </div>
                </PatternPropertyCard>
                {["image", "svg"].includes(l.kind) && <p className="pe-hint">Original image/SVG colours retained. Edit SVG source to change its palette.</p>}

                {!["image", "svg"].includes(l.kind) && (
                  <>
                    <PatternPropertyCard Caption="Fill">
                    <PatternColourControls key={`fill-${l.id}`} Caption="Motif fill" ColourLabel="Motif color" Source={l.fillSource} Solid={l.color}
                    OnChange={(Source, Colour) => {
                      if (!Source && (doc.stitch || doc.weave || doc.library)) {
                        const Next = replacePatternColor(docRef.current, l.color, Colour);
                        Next.layers = Next.layers.map(Layer => Layer.id === l.id ? { ...Layer, fillSource: undefined } : Layer);
                        commit(Next);
                      } else layer({ fillSource: Source, color: Colour });
                    }}/>

                    <label className="pe-check">
                      <input
                        type="checkbox" role="switch"
                        aria-label="Motif fill enabled"
                        checked={
                          l.paint
                            ? l.fillEnabled
                            : !(l.kind === "path" && l.strokeWidth)
                        }
                        onChange={(e) =>
                          layer({
                            paint: true,
                            fillEnabled: e.target.checked,
                            stroke: l.paint ? l.stroke : l.color,
                          })
                        }
                      />{" "}
                      Fill enabled
                    </label>
                </PatternPropertyCard>
                <PatternPropertyCard Caption="Stroke & geometry">
                    <PatternColourControls key={`stroke-${l.id}`} Caption="Motif stroke" ColourLabel="Motif stroke color" Source={l.strokeSource} Solid={l.paint ? l.stroke : l.color}
                  OnChange={(Source, Colour) => layer({ paint: true, strokeSource: Source, stroke: Colour, fillEnabled: l.paint ? l.fillEnabled : !(l.kind === "path" && l.strokeWidth), strokeWidth: l.strokeWidth || 1 })}/>
                    {l.paint && (
                      <div className="pe-grid">
                        <label>
                          Line cap
                          <select
                            aria-label="Motif line cap"
                            value={l.strokeLinecap || "round"}
                            onChange={(e) =>
                              layer({ strokeLinecap: e.target.value })
                            }
                          >
                            {["round", "butt", "square"].map((v) => (
                              <option key={v}>{v}</option>
                            ))}
                          </select>
                        </label>
                        <label>
                          Line join
                          <select
                            aria-label="Motif line join"
                            value={l.strokeLinejoin || "round"}
                            onChange={(e) =>
                              layer({ strokeLinejoin: e.target.value })
                            }
                          >
                            {["round", "miter", "bevel"].map((v) => (
                              <option key={v}>{v}</option>
                            ))}
                          </select>
                        </label>
                      </div>
                    )}
                    {field("Stroke width", "strokeWidth", 0, 25, 0.1)}
                    {l.kind === "arc" && (
                      <>
                        <div className="pe-grid">
                          {field("Arc start angle", "arcStart", -360, 360)}
                          {field(
                            "Arc sweep angle",
                            "arcSweep",
                            -359.99,
                            359.99,
                            0.1,
                          )}
                        </div>
                        <label>
                          Arc closure
                          <select
                            aria-label="Arc closure"
                            value={l.arcClosure}
                            onChange={(e) =>
                              layer({
                                arcClosure: e.target.value,
                                fillEnabled: e.target.value !== "open",
                              })
                            }
                          >
                            <option value="open">Open arc</option>
                            <option value="chord">Closed chord</option>
                            <option value="pie">Pie / sector</option>
                          </select>
                        </label>
                      </>
                    )}
                    {l.kind === "rect" &&
                      field("Corner radius", "cornerRadius", 0, 50)}
                    {l.kind === "polygon" &&
                      field("Polygon sides", "sides", 3, 24)}
                    {l.kind === "star" && (
                      <div className="pe-grid">
                        {field("Star points", "starPoints", 3, 20)}
                        {field("Inner radius", "innerRadius", 0.05, 0.95, 0.05)}
                      </div>
                    )}
                    {l.nodes && (
                      <label className="pe-check">
                        <input
                          type="checkbox" role="switch"
                          aria-label="Close motif path"
                          checked={l.closed}
                          onChange={(e) => layer({ closed: e.target.checked })}
                        />{" "}
                        Closed path
                      </label>
                    )}
                    <button
                      className="pe-wide"
                      onClick={() => {
                        try {
                          layer(convertPatternLayerToPath(l));
                          setTool("node");
                          setMode("design");
                        } catch (error) {
                          setError(error.message);
                        }
                      }}
                    >
                      <Spline size={14} />{" "}
                      {l.nodes
                        ? "Edit path points"
                        : "Convert to editable path"}
                    </button>
                  </PatternPropertyCard>
                  </>
                )}

                <PatternPropertyCard Caption="Material surface">
                <label>
                  Material assignment
                  <select
                    aria-label="Motif material"
                    value={l.finish}
                    onChange={(e) => {
                      const f = patternFinishes[e.target.value];
                      layer({
                        finish: e.target.value,
                        roughness: f.roughness,
                        metalness: f.metalness,
                        relief: f.height,
                      });
                    }}
                  >
                    {Object.entries(patternFinishes).map(([k, f]) => (
                      <option key={k} value={k}>
                        {f.name}
                      </option>
                    ))}
                  </select>
                </label>
                <button
                  className="pe-wide"
                  onClick={() =>
                    commit({
                      ...doc,
                      layers: doc.layers.map((p) =>
                        p.locked
                          ? p
                          : {
                              ...p,
                              finish: l.finish,
                              roughness: l.roughness,
                              metalness: l.metalness,
                              relief: l.relief,
                            },
                      ),
                    })
                  }
                >
                  Assign this finish to all motifs
                </button>
                <div className="pe-grid">
                  {field("Roughness", "roughness", 0.05, 1, 0.01)}
                  {field("Metalness", "metalness", 0, 1, 0.05)}
                  {field("Relief (mm)", "relief", -1, 1, 0.01)}
                  
                </div>
                </PatternPropertyCard>
                {l.kind === "svg" && (
                  <button
                    className="pe-wide"
                    onClick={() => openSVGSource(selection)}
                  >
                    <Spline size={14} />
                    Edit SVG source
                  </button>
                )}
                {l.kind === "path" && (
                  <label>
                    SVG path · local 0–100
                    <textarea
                      aria-label="SVG path"
                      value={l.path}
                      onChange={(e) => {
                        const contour = svgPathNodes(e.target.value);
                        layer({
                          path: e.target.value,
                          nodes: contour?.nodes || undefined,
                          closed: contour?.closed || false,
                        });
                      }}
                    />
                  </label>
                )}
                <div className="pe-section">
                  <h3>Arrange & reflect</h3>
                  <div className="pe-shapes">
                    <button onClick={() => layer({ x: 256 })}>Center X</button>
                    <button onClick={() => layer({ y: 256 })}>Center Y</button>
                    <button
                      aria-pressed={l.flipX}
                      onClick={() => layer({ flipX: !l.flipX })}
                    >
                      Flip X
                    </button>
                    <button
                      aria-pressed={l.flipY}
                      onClick={() => layer({ flipY: !l.flipY })}
                    >
                      Flip Y
                    </button>
                    <button
                      onClick={() => add(l.kind, reflectPatternLayer(l, "x"))}
                    >
                      Reflect copy X
                    </button>
                    <button
                      onClick={() => add(l.kind, reflectPatternLayer(l, "y"))}
                    >
                      Reflect copy Y
                    </button>
                  </div>
                  <div className="pe-grid">
                    <label>
                      Copies
                      <input
                        aria-label="Radial copies"
                        type="number"
                        min="2"
                        max="12"
                        value={ring.count}
                        onChange={(e) =>
                          setRing({ ...ring, count: +e.target.value })
                        }
                      />
                    </label>
                    <label>
                      Radius
                      <input
                        aria-label="Radial radius"
                        type="number"
                        min="0"
                        max="256"
                        value={ring.radius}
                        onChange={(e) =>
                          setRing({ ...ring, radius: +e.target.value })
                        }
                      />
                    </label>
                  </div>
                  <button
                    className="pe-wide"
                    onClick={() =>
                      commit({
                        ...doc,
                        layers: doc.layers.flatMap((p, i) =>
                          i === selection
                            ? radialPatternLayers(p, ring.count, ring.radius)
                            : [p],
                        ),
                      })
                    }
                  >
                    Arrange in a ring
                  </button>
                </div>
                <label className="pe-check">
                  <input
                    type="checkbox" role="switch"
                    checked={l.visible}
                    onChange={(e) => layer({ visible: e.target.checked })}
                  />{" "}
                  Visible
                </label>
                <div className="pe-tools">
                  <button title="Bring forward" onClick={() => shift(1)}>
                    <ArrowUp size={15} /> Forward
                  </button>
                  <button title="Send backward" onClick={() => shift(-1)}>
                    <ArrowDown size={15} /> Backward
                  </button>
                </div>
              </fieldset>
            </div>
          )}
          <div className="pe-section se-export-panel">
            <h3>
              <Download size={14} /> Export pattern
            </h3>
            <button
              className="pe-wide"
              onClick={() => {
                if (canvasRef.current && !canvasRef.current.prepareExport())
                  return;
                patternDownload(
                  patternSVG(docRef.current),
                  "pattern.svg",
                  "image/svg+xml",
                );
              }}
            >
              <Download size={14} />{" "}
              {doc.tileAxes === "none"
                ? "Export rug SVG"
                : doc.tileAxes === "xy"
                  ? "Export seamless SVG"
                  : "Export border SVG"}
            </button>
            <div className="se-png-export">
              <select
                aria-label="Pattern PNG resolution"
                value={exportSize}
                onChange={(e) => setExportSize(Number(e.target.value))}
              >
                {[512, 1024, 2048, 4096].map((n) => (
                  <option key={n} value={n}>
                    {n}px
                  </option>
                ))}
              </select>
              <button disabled={busy} onClick={exportPNG}>
                <Download size={14} /> Export PNG
              </button>
            </div>
            <button
              className="pe-wide"
              onClick={() =>
                patternDownload(
                  leatherSourceSVG(),
                  "leather-source.svg",
                  "image/svg+xml",
                )
              }
            >
              Export leather source SVG
            </button>
            <p className="pe-hint">
              Vector shapes stay sharp in SVG. Live rendering samples 1024–2048
              px maps. Material slots affect color, relief, roughness, metalness
              and finish. Bake six channels from Export material.
            </p>
          </div>
        </aside>
      </div>
      {svgSource && (
        <SVGSourceEditor
          source={svgSource.source}
          editing={svgSource.index !== null}
          onApply={embedSVG}
          onClose={() => setSVGSource(null)}
        />
      )}
    </div>
  );
}
