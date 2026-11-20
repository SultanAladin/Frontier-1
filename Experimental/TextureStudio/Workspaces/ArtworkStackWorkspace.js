/*==========================================================================================================================================
                                                      ARTWORKSTACKWORKSPACE.JS
==========================================================================================================================================*/
/* Unified Vector SVG + Layer-Stack + Non-Destructive Modifier-Stack Workspace powering both Pattern Studio
   (with 36+ African Patterns, Noise-Scaled Patterns & seamless 2x2 wrap) and Stamp Studio (transparent PBR decals & text).
   Features an interactive SVG vector editor (rulers, grid lines/axes, shape tools, direct move/resize/rotate handles)
   and drag-and-drop integration with the bottom Asset Browser. */

import {
  ARTWORK_SIZE,
  ARTWORK_FINISHES,
  ARTWORK_PBR_CHANNELS,
  ARTWORK_PBR_CHANNEL_ROWS,
  ARTWORK_PBR_CHANNEL_MAP,
  MOTIF_TYPES,
  MODIFIER_DEFINITIONS,
  artworkDocumentBackgroundSvg,
  artworkDocumentLayersSvg,
  artworkDocumentToSvgString,
  createArtworkDocument,
  createArtworkLayer,
  createDefaultModifier,
  createDefaultPointGradient2D,
  ensureArtworkLayerSurface,
  evaluateArtworkDocument,
  formatArtworkChannelValue,
  getArtworkChannelValue,
  getArtworkLayerPbrMask,
  normalizeGradientStops,
  normalizePointGradient2D,
  nodesToNormalizedCurveData,
  normalizedCurveToCanvasNodes,
  pathNodesToSvgD,
  setArtworkChannelValue,
  setArtworkLayerPbrMask,
} from "../Engine/Patterns/ArtworkStackEngine.js";
import {
  listPatternStarterCatalog,
  listStampStarterCatalog,
  loadStarterAsArtworkDocument,
} from "../Engine/Patterns/StarterCatalogBridge.js";
import { createLayerStackPanel } from "./LayerStackKit.js";
import { createViewportHeader, STUDIO_RESOLUTIONS } from "./ViewportHeaderKit.js";
import {
  Construct,
  ConstructGlyph,
  SectionCard,
  SliderRow,
  SwitchRow,
  SelectRow,
  SegmentedRow,
  ChannelMaskRow,
  ColorRow,
  TextRow,
  ActionRow,
  MetricTrio,
  SmoothScroll,
  OpenScope,
  CloseScope,
  ReleaseScope,
  Broadcast,
  Toast,
} from "../StudioControls.js";

const SVG_NS = "http://www.w3.org/2000/svg";

const VECTOR_TOOLS = [
  { id: "select", label: "Select", glyph: "Pointer", key: "V" },
  { id: "curve", label: "Curve", glyph: "Curve", key: "U" },
  { id: "pen", label: "Pen", glyph: "Pen", key: "P" },
  { id: "node", label: "Edit Points", glyph: "Focus", key: "A" },
  { id: "gradient", label: "Point Grad", glyph: "Gradient", key: "F" },
  { id: "square", label: "Rect", glyph: "Square", key: "R" },
  { id: "circle", label: "Circle", glyph: "Circle", key: "O" },
  { id: "diamond", label: "Diamond", glyph: "Diamond", key: "D" },
  { id: "triangle", label: "Tri", glyph: "Triangle", key: "T" },
  { id: "star", label: "Star", glyph: "Star", key: "S" },
  { id: "hexagon", label: "Hex", glyph: "Hexagon", key: "G" },
  { id: "stripe", label: "Stripe", glyph: "Stripe", key: "L" },
  { id: "text", label: "Text", glyph: "Type", key: "X" },
  { id: "pan", label: "Pan", glyph: "Hand", key: "H" },
];

function drawImageDataToCanvas(canvas, rgba, w, h, checkerboard = true, tileRepeat = 1) {
  canvas.width = w * tileRepeat;
  canvas.height = h * tileRepeat;
  const ctx = canvas.getContext("2d");
  if (checkerboard) {
    const sz = 16;
    for (let y = 0; y < canvas.height; y += sz) {
      for (let x = 0; x < canvas.width; x += sz) {
        ctx.fillStyle = ((x / sz + y / sz) & 1) === 0 ? "#18191d" : "#22242a";
        ctx.fillRect(x, y, sz, sz);
      }
    }
  } else {
    ctx.fillStyle = "#111215";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
  }

  const off = document.createElement("canvas");
  off.width = w;
  off.height = h;
  off.getContext("2d").putImageData(new ImageData(new Uint8ClampedArray(rgba), w, h), 0, 0);

  for (let ty = 0; ty < tileRepeat; ty++) {
    for (let tx = 0; tx < tileRepeat; tx++) {
      ctx.drawImage(off, tx * w, ty * h);
    }
  }
  if (tileRepeat > 1) {
    ctx.strokeStyle = "rgba(239, 244, 207, 0.35)";
    ctx.lineWidth = 1.5;
    ctx.setLineDash([6, 4]);
    for (let i = 1; i < tileRepeat; i++) {
      ctx.beginPath();
      ctx.moveTo(i * w, 0);
      ctx.lineTo(i * w, canvas.height);
      ctx.moveTo(0, i * h);
      ctx.lineTo(canvas.width, i * h);
      ctx.stroke();
    }
    ctx.setLineDash([]);
  }
}

function downloadBlobUrl(url, filename) {
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  a.click();
}

function snapCoord(val, step) {
  return step > 0 ? Math.round(val / step) * step : val;
}

const RESOLUTIONS = STUDIO_RESOLUTIONS;

function buildSingleLayerThumbSvg(doc, layer) {
  if (!layer) return "";
  const docSize = doc.size || ARTWORK_SIZE;
  const thumbLayer = { ...layer, visible: true, parentId: null };
  const singleDoc = {
    ...doc,
    mode: "stamp", // preview individual layer clearly without full-tile ghost crowding unless array-grid/wrap
    layers: [thumbLayer],
    background: { enabled: false },
  };
  const { defs, body } = artworkDocumentLayersSvg(singleDoc, {
    idPrefix: `lt-${String(layer.id || "x").replace(/[^a-z0-9]/gi, "")}`,
  });

  // Auto-frame compact single shapes so tiny motifs fill the 36x36 thumbnail cleanly
  const hasSpreadMod = (layer.modifiers || []).some(
    (m) => m.enabled !== false && (m.type === "array-grid" || m.type === "array-radial"),
  );
  let vb = `0 0 ${docSize} ${docSize}`;
  if (layer.kind !== "gradient" && !hasSpreadMod && layer.transform) {
    const bw = Math.max(24, Number(layer.transform.width || 64));
    const bh = Math.max(24, Number(layer.transform.height || 64));
    const span = Math.max(bw, bh);
    if (span < docSize * 0.56) {
      const box = Math.min(docSize, Math.max(120, span * 1.48));
      const cx = Number(layer.transform.x ?? docSize * 0.5);
      const cy = Number(layer.transform.y ?? docSize * 0.5);
      const vx = Math.max(0, Math.min(docSize - box, cx - box * 0.5));
      const vy = Math.max(0, Math.min(docSize - box, cy - box * 0.5));
      vb = `${vx.toFixed(1)} ${vy.toFixed(1)} ${box.toFixed(1)} ${box.toFixed(1)}`;
    }
  }
  return `<svg xmlns="${SVG_NS}" viewBox="${vb}"><defs>${defs}</defs>${body}</svg>`;
}

function buildMaskMiniSvg(mask) {
  if (!mask) return "";
  const inv = !!mask.invert;
  const fg = inv ? "#14161a" : "#eff4cf";
  const bg = inv ? "#eff4cf" : "#14161a";
  if (mask.mode === "radial") {
    return `<svg viewBox="0 0 24 24"><rect width="24" height="24" fill="${bg}"/><circle cx="12" cy="12" r="7.5" fill="${fg}" fill-opacity="0.9"/></svg>`;
  }
  if (mask.mode === "stripe") {
    return `<svg viewBox="0 0 24 24"><rect width="24" height="24" fill="${bg}"/><rect x="3" y="0" width="5" height="24" fill="${fg}"/><rect x="13" y="0" width="5" height="24" fill="${fg}"/></svg>`;
  }
  return `<svg viewBox="0 0 24 24"><rect width="24" height="24" fill="${bg}"/><circle cx="8" cy="9" r="5" fill="${fg}" fill-opacity="0.85"/><circle cx="16" cy="15" r="4.5" fill="${fg}" fill-opacity="0.7"/><circle cx="16" cy="7" r="2.8" fill="${fg}" fill-opacity="0.65"/></svg>`;
}

export function mountArtworkStackWorkspace(root, appBridge, { mode = "pattern" } = {}) {
  const isPattern = mode === "pattern";
  const catalog = isPattern ? listPatternStarterCatalog() : listStampStarterCatalog();
  const initialDoc = catalog.length ? loadStarterAsArtworkDocument(catalog[0]) : createArtworkDocument(mode);
  if (!initialDoc.selectedLayerId && initialDoc.layers?.length) {
    initialDoc.selectedLayerId = initialDoc.layers[initialDoc.layers.length - 1].id;
  }

  let state = {
    mode,
    doc: initialDoc,
    activeStarterId: catalog[0]?.id || "",
    resolution: 1024,
    viewChannel: "vector", // "vector" | "tile2x2" | "composite" | "albedo" | "roughness" | "metalness" | "height" | "normal"
    vectorTool: "select",
    showGrid: true,
    showAxes: true,
    showTileRepeat: isPattern,
    snapStep: 16, // 0, 8, 16, 32, 64
    zoom: 1,
    panX: 0,
    panY: 0,
    activePointGradSlot: "fill",
    selectedPointGradIdx: 0,
    lastStats: { instances: 1, coveragePct: 0, minScale: 1, maxScale: 1 },
    lastEval: null,
  };

  let inspectorScope = null;
  let disposed = false;
  let spacePanning = false;
  let pointerInteraction = null; // { kind: "move" | "resize" | "rotate" | "draw" | "pan" | "pen-drag" | "node-drag" | "handle-drag", ... }
  let activePathSession = null; // { nodes, closed, tension, tool, hoverPt, nearStart, editingLayerId, activeNodeIndex }
  const undoStack = [];
  const redoStack = [];

  const grid = Construct("div", "studio-grid");

  // ----------------------------------------------------------------- LEFT: FULL DESIGN LAYER STACK + LIVE THUMBNAILS
  const leftCol = Construct("aside", "studio-column tp-layer-dock");
  const layerStackUI = createLayerStackPanel(leftCol, {
    title: "Layers",
    extraMetricLabel: "MODS",
  });

  // ----------------------------------------------------------------- CENTER: UNIFIED 2-TIER ALLOYSTUDIO HEADER + STAGE
  const centerCol = Construct("main", "studio-column viewport-column");

  const viewOpts = [
    ["Vector SVG", "vector"],
    ...(isPattern ? [["Seamless 2×2", "tile2x2"]] : []),
    ["Single PBR Tile", "composite"],
    ["Albedo", "albedo"],
    ["Roughness", "roughness"],
    ["Metalness", "metalness"],
    ["Height", "height"],
    ["Normal", "normal"],
    ...ARTWORK_PBR_CHANNELS.filter((c) => c.extended && c.key !== "normal").map((c) => [c.label, c.key]),
  ];

  const loadJsonInp = document.createElement("input");
  loadJsonInp.type = "file";
  loadJsonInp.accept = ".json,.pattern.json,.stamp.json";
  loadJsonInp.hidden = true;

  const btnSendToPaint = Construct("button", "pill-btn accent", [
    ConstructGlyph("Brush"),
    Construct("span", null, isPattern ? "Use in Paint" : "Use Stamp"),
  ]);
  btnSendToPaint.type = "button";
  btnSendToPaint.title = isPattern ? "Use Pattern in Texture Paint" : "Use Stamp in Texture Paint";

  const btnSendToMaterial = isPattern
    ? Construct("button", "pill-btn warm", [
        ConstructGlyph("Material"),
        Construct("span", null, "Tile on Material"),
      ])
    : null;
  if (btnSendToMaterial) {
    btnSendToMaterial.type = "button";
    btnSendToMaterial.title = "Apply this pattern onto the active Material as a tileable PBR texture";
  }

  const btnGrid = Construct("button", "se-tool-btn" + (state.showGrid ? " active" : ""), [
    ConstructGlyph("Grid"),
    Construct("span", null, "Grid"),
  ]);
  btnGrid.type = "button";
  btnGrid.title = "Toggle Snap Grid";

  const btnAxes = Construct("button", "se-tool-btn" + (state.showAxes ? " active" : ""), [
    ConstructGlyph("Focus"),
    Construct("span", null, "Axes"),
  ]);
  btnAxes.type = "button";
  btnAxes.title = "Toggle Center Symmetry Axes";

  const snapSelect = Construct("select", "se-header-select");
  snapSelect.setAttribute("aria-label", "Snap Grid Step");
  for (const [label, val] of [["Snap: Off", 0], ["Snap: 8px", 8], ["Snap: 16px", 16], ["Snap: 32px", 32], ["Snap: 64px", 64]]) {
    const o = Construct("option", null, label);
    o.value = String(val);
    snapSelect.append(o);
  }
  snapSelect.value = String(state.snapStep);
  const snapWrap = Construct("div", "select-pill-wrap vp-compact-select", [
    snapSelect,
    Construct("span", "pill-caret-divider", ConstructGlyph("ChevronDown", "glyph chev")),
  ]);

  const exportRgbaBufferPng = (rgba, w, h, filename) => {
    const off = document.createElement("canvas");
    off.width = w;
    off.height = h;
    off.getContext("2d").putImageData(new ImageData(new Uint8ClampedArray(rgba), w, h), 0, 0);
    downloadBlobUrl(off.toDataURL("image/png"), filename);
  };

  const performSaveProject = () => {
    const payload = {
      schema: `frontier.${mode}.v1`,
      resolution: state.resolution,
      savedAt: new Date().toISOString(),
      document: state.doc,
    };
    const json = JSON.stringify(payload, null, 2);
    try {
      localStorage?.setItem(`frontier-last-${mode}-doc`, json);
    } catch {}
    const blob = new Blob([json], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const slug = (state.doc.name || mode).toLowerCase().replace(/[^a-z0-9]+/g, "-");
    downloadBlobUrl(url, `${slug}.${mode}.json`);
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    Toast(`Saved ${state.doc.name || mode} (${state.resolution}px) project`);
  };

  function syncUndoState() {
    headerUI?.setUndoRedoState(undoStack.length > 0, redoStack.length > 0);
  }

  function pushUndo(snap = null) {
    undoStack.push(snap || structuredClone(state.doc));
    if (undoStack.length > 40) undoStack.shift();
    redoStack.length = 0;
    syncUndoState();
  }

  function performUndo() {
    if (!undoStack.length) return;
    redoStack.push(structuredClone(state.doc));
    state.doc = undoStack.pop();
    syncUndoState();
    evaluateAndRender(true);
    rebuildInspector();
    Toast("Undone");
  }

  function performRedo() {
    if (!redoStack.length) return;
    undoStack.push(structuredClone(state.doc));
    state.doc = redoStack.pop();
    syncUndoState();
    evaluateAndRender(true);
    rebuildInspector();
    Toast("Redone");
  }

  const primaryToolIds = new Set(["select", "curve", "gradient", "square", "circle", "diamond", "triangle", "text", "pan"]);
  const primaryToolList = VECTOR_TOOLS.filter((t) => primaryToolIds.has(t.id));
  const extraToolList = VECTOR_TOOLS.filter((t) => !primaryToolIds.has(t.id));

  const headerUI = createViewportHeader(centerCol, {
    workspace: mode,
    badgeText: isPattern ? "PATTERN" : "STAMP",
    iconGlyph: isPattern ? "Pattern" : "Stamp",
    canvasName: state.doc.name || (isPattern ? "Untitled pattern" : "Untitled stamp"),
    onRename(next) {
      if (state.doc.name === next) return;
      pushUndo();
      state.doc.name = next;
      evaluateAndRender(false, { skipPbr: true });
      rebuildInspector();
    },
    resolutions: RESOLUTIONS,
    resolution: state.resolution,
    onResolutionChange(val) {
      const next = Math.max(64, Math.min(4096, Math.round(Number(val) || 0)));
      if (!Number.isFinite(next) || next <= 0) return;
      state.resolution = next;
      headerUI.setResolution(next);
      headerUI.setSaveItemMeta("png", `${next}px`);
      headerUI.setSaveItemMeta("pbr", `${Math.min(2048, next)}px`);
      evaluateAndRender(true);
      Toast(`Resolution set to ${next} × ${next}px`);
    },
    viewOptions: viewOpts,
    viewValue: state.viewChannel,
    viewAriaLabel: "Canvas View",
    onViewChange(val) {
      state.viewChannel = val;
      syncViewDisplay();
    },
    onUndo: performUndo,
    onRedo: performRedo,
    saveItems: [
      { id: "project", label: "Save Project File", meta: `.${mode}.json`, onClick: performSaveProject },
      {
        id: "png",
        label: "Export PNG",
        meta: `${state.resolution}px`,
        onClick() {
          const sz = state.resolution;
          const ev = evaluateArtworkDocument(state.doc, sz);
          const slug = (state.doc.name || mode).toLowerCase().replace(/[^a-z0-9]+/g, "-");
          exportRgbaBufferPng(ev.albedo, ev.width, ev.heightPx || ev.width, `${slug}-${mode}-${sz}.png`);
          Toast(`Exported ${sz}×${sz} ${mode.toUpperCase()} PNG`);
        },
      },
      {
        id: "svg",
        label: "Export Vector SVG",
        meta: ".svg",
        onClick() {
          const sz = state.resolution;
          const svgStr = artworkDocumentToSvgString(state.doc, { width: sz, height: sz, showBg: true });
          const blob = new Blob([svgStr], { type: "image/svg+xml" });
          const url = URL.createObjectURL(blob);
          const slug = (state.doc.name || mode).toLowerCase().replace(/[^a-z0-9]+/g, "-");
          downloadBlobUrl(url, `${slug}-${mode}-${sz}.svg`);
          setTimeout(() => URL.revokeObjectURL(url), 1000);
          Toast(`Exported ${sz}×${sz} Vector SVG`);
        },
      },
      {
        id: "pbr",
        label: "Export PBR Maps",
        meta: `${Math.min(2048, state.resolution)}px`,
        onClick() {
          const sz = Math.min(2048, state.resolution);
          const ev = evaluateArtworkDocument(state.doc, sz);
          const slug = (state.doc.name || mode).toLowerCase().replace(/[^a-z0-9]+/g, "-");
          const w = ev.width, h = ev.heightPx || ev.width;
          exportRgbaBufferPng(ev.albedo, w, h, `${slug}-albedo-${sz}.png`);
          exportRgbaBufferPng(ev.roughness, w, h, `${slug}-roughness-${sz}.png`);
          exportRgbaBufferPng(ev.metalness, w, h, `${slug}-metalness-${sz}.png`);
          exportRgbaBufferPng(ev.normal, w, h, `${slug}-normal-${sz}.png`);
          let count = 4;
          for (const [extKey, extBuf] of Object.entries(ev.extraChannels || {})) {
            if (!extBuf || extKey === "normal") continue;
            exportRgbaBufferPng(extBuf, w, h, `${slug}-${extKey}-${sz}.png`);
            count++;
          }
          Toast(`Exported ${count} PBR maps @ ${sz}×${sz}px`);
        },
      },
      { id: "open", label: "Open Project File…", meta: ".json", onClick: () => loadJsonInp.click() },
      { node: loadJsonInp },
    ],
    primaryActions: [btnSendToMaterial, btnSendToPaint].filter(Boolean),
    tools: VECTOR_TOOLS.map((t) => ({
      id: t.id,
      label: t.label,
      glyph: t.glyph,
      kbd: t.key,
      datasetKey: "vectorTool",
      extra: !primaryToolIds.has(t.id),
      active: state.vectorTool === t.id,
    })),
    moreTools: extraToolList.map((t) => ({
      id: t.id,
      label: t.label,
      glyph: t.glyph,
      kbd: t.key,
      active: state.vectorTool === t.id,
    })),
    moreControls: [btnGrid, btnAxes, snapWrap],
    onToolSelect: (id) => selectVectorTool(id),
  });
  syncUndoState();

  // Interactive SVG Stage Shell
  const stage = Construct("div", "se-stage-shell");

  const viewControls = Construct("div", "se-view-controls");
  viewControls.setAttribute("role", "toolbar");
  viewControls.setAttribute("aria-label", "Viewport zoom controls");
  const btnZoomOut = Construct("button", null, "−");
  btnZoomOut.type = "button";
  const zoomReadout = Construct("span", "tabular", "100%");
  const btnZoomIn = Construct("button", null, "+");
  btnZoomIn.type = "button";
  const btnZoomFit = Construct("button", "se-fit-btn", "Fit");
  btnZoomFit.type = "button";
  viewControls.append(btnZoomOut, zoomReadout, btnZoomIn, btnZoomFit);

  const seViewport = Construct("div", "se-viewport");

  const rulerCorner = Construct("div", "se-ruler-corner", "");
  const rulerXWrap = Construct("div", "se-ruler-x");
  const rulerXCanvas = document.createElement("canvas");
  rulerXWrap.append(rulerXCanvas);

  const rulerYWrap = Construct("div", "se-ruler-y");
  const rulerYCanvas = document.createElement("canvas");
  rulerYWrap.append(rulerYCanvas);

  // Primary Interactive SVG Canvas
  const svgEl = document.createElementNS(SVG_NS, "svg");
  svgEl.setAttribute("class", "se-canvas");
  svgEl.setAttribute("tabindex", "0");
  svgEl.setAttribute("aria-label", isPattern ? "Interactive Pattern SVG Vector Canvas" : "Interactive Stamp SVG Vector Canvas");

  // PBR raster overlay (hidden while editing in vector mode!)
  const pbrWrap = Construct("div", "se-pbr-raster");
  pbrWrap.hidden = true;
  const mainCanvas = document.createElement("canvas");
  pbrWrap.append(mainCanvas);

  seViewport.append(rulerCorner, rulerXWrap, rulerYWrap, svgEl, pbrWrap, viewControls);

  const stageFooter = Construct("div", "se-stage-footer");
  const hintText = Construct("span", "se-stage-hints", "Marquee to select · Shift constrain · Alt centred resize · Space pan");
  const selCountEl = Construct("span", "se-sel-count", state.doc.selectedLayerId ? "1 selected" : "0 selected");
  stageFooter.append(hintText, selCountEl);

  stage.append(seViewport, stageFooter);
  centerCol.append(stage);

  // ----------------------------------------------------------------- RIGHT: LAYER, MASK & MODIFIER STACK
  const rightCol = Construct("aside", "studio-column");
  rightCol.append(
    Construct("div", "panel-heading", [
      Construct("h2", null, "Layer & Modifiers"),
      Construct("span", "panel-heading-meta", isPattern ? "VECTOR + PBR" : "DECAL + PBR"),
    ]),
  );
  const rightScroll = Construct("div", "panel-scroll");
  const rightInner = Construct("div", "panel-scroll-inner");
  rightScroll.append(rightInner);
  rightCol.append(
    rightScroll,
    Construct("div", "panel-footer", [
      Construct("span", null, "Non-destructive Modifier Stack"),
      Construct("span", null, "Live SVG + PBR"),
    ]),
  );

  grid.append(leftCol, centerCol, rightCol);
  root.append(grid);
  SmoothScroll(rightScroll, rightInner);

  // ----------------------------------------------------------------- HELPERS
  function getSelectedLayer() {
    return state.doc.layers.find((l) => l.id === state.doc.selectedLayerId) || state.doc.layers[state.doc.layers.length - 1] || state.doc.layers[0];
  }

  function getPointGradientTarget(autoCreate = false) {
    const slot = state.activePointGradSlot || "fill";
    const bg = state.doc.background;
    const size = state.doc.size || ARTWORK_SIZE;
    if (slot === "ground" && bg?.enabled) {
      if (autoCreate && bg.fillMode !== "point") {
        bg.fillMode = "point";
        bg.fillPointGradient = normalizePointGradient2D(bg.fillPointGradient, bg.color || "#171a1f", bg.colorB || "#2b3340", size);
      }
      if (bg.fillMode === "point") {
        return {
          slot: "ground",
          label: "Document Ground",
          getGrad: () => {
            bg.fillPointGradient = normalizePointGradient2D(bg.fillPointGradient, bg.color || "#171a1f", bg.colorB || "#2b3340", size);
            return bg.fillPointGradient;
          },
          setGrad: (g) => { bg.fillPointGradient = g; },
        };
      }
    }

    const sel = getSelectedLayer();
    if (sel && sel.kind !== "folder" && !sel.locked) {
      if (slot === "stroke" && sel.shape?.strokeMode === "point") {
        return {
          slot: "stroke",
          layer: sel,
          label: `${sel.name} Stroke`,
          getGrad: () => {
            sel.shape.strokePointGradient = normalizePointGradient2D(sel.shape.strokePointGradient, sel.shape.strokeColor || "#16181c", sel.shape.strokeColorB || "#eff4cf", size);
            return sel.shape.strokePointGradient;
          },
          setGrad: (g) => { sel.shape.strokePointGradient = g; },
        };
      }
      if (sel.shape?.fillMode === "point" || autoCreate) {
        if (autoCreate && sel.shape.fillMode !== "point") {
          sel.shape.fillMode = "point";
          sel.shape.fillEnabled = true;
          sel.shape.fillPointGradient = normalizePointGradient2D(sel.shape.fillPointGradient, sel.shape.color || "#eff4cf", sel.shape.colorB || "#5a7692", size);
        }
        if (sel.shape?.fillMode === "point") {
          return {
            slot: "fill",
            layer: sel,
            label: sel.name,
            getGrad: () => {
              sel.shape.fillPointGradient = normalizePointGradient2D(sel.shape.fillPointGradient, sel.shape.color || "#eff4cf", sel.shape.colorB || "#5a7692", size);
              return sel.shape.fillPointGradient;
            },
            setGrad: (g) => { sel.shape.fillPointGradient = g; },
          };
        }
      }
    }

    if (bg?.enabled && bg.fillMode === "point") {
      return {
        slot: "ground",
        label: "Document Ground",
        getGrad: () => {
          bg.fillPointGradient = normalizePointGradient2D(bg.fillPointGradient, bg.color || "#171a1f", bg.colorB || "#2b3340", size);
          return bg.fillPointGradient;
        },
        setGrad: (g) => { bg.fillPointGradient = g; },
      };
    }

    if (autoCreate) {
      let existingGrad = state.doc.layers.find((l) => l.kind === "gradient" && l.visible && !l.locked);
      if (!existingGrad) {
        existingGrad = createArtworkLayer("gradient", {
          name: "Point Gradient Field",
          shape: {
            fillMode: "point",
            color: isPattern ? "#eff4cf" : "#e8c27a",
            colorB: "#5a7692",
            fillPointGradient: createDefaultPointGradient2D(isPattern ? "#eff4cf" : "#e8c27a", "#5a7692", size),
          },
        });
        state.doc.layers.unshift(existingGrad);
      } else {
        existingGrad.shape.fillMode = "point";
        existingGrad.shape.fillPointGradient = normalizePointGradient2D(existingGrad.shape.fillPointGradient, existingGrad.shape.color || "#eff4cf", existingGrad.shape.colorB || "#5a7692", size);
      }
      state.doc.selectedLayerId = existingGrad.id;
      state.activePointGradSlot = "fill";
      renderLeftColumn();
      return {
        slot: "fill",
        layer: existingGrad,
        label: existingGrad.name,
        getGrad: () => existingGrad.shape.fillPointGradient,
        setGrad: (g) => { existingGrad.shape.fillPointGradient = g; },
      };
    }
    return null;
  }

  function syncPathSessionHints() {
    if (activePathSession) {
      const n = activePathSession.nodes.length;
      hintText.textContent = activePathSession.editingLayerId
        ? "Editing curve nodes · Drag points or handles · Dbl-click node to toggle smooth/corner · Enter finish"
        : `Curve path (${n} pt${n === 1 ? "" : "s"}) · Click add point · Click start or Dbl-click / Enter finish · Backspace undo pt`;
    } else if (state.vectorTool === "gradient") {
      const tgt = getPointGradientTarget(false);
      const count = tgt ? tgt.getGrad().points.length : 0;
      hintText.textContent = `Point Gradient (${count}/12 pts${tgt ? ` · ${tgt.label}` : ""}) · Click canvas to add colour points · Drag handles to move gradient in scene · Del removes`;
    } else if (state.vectorTool === "curve" || state.vectorTool === "pen") {
      hintText.textContent = `${state.vectorTool === "curve" ? "Curve Tool (U)" : "Pen Tool (P)"} · Click canvas to trace smooth Bézier curve · Enter or Dbl-click to commit`;
    } else if (state.vectorTool === "node") {
      hintText.textContent = "Point Editor (A) · Select a Curve layer to adjust nodes & Bézier handles";
    } else {
      hintText.textContent = "Marquee to select · Shift constrain · Alt centred resize · Space pan · U Curve · F Point Grad";
    }
  }

  function startEditLayerCurveSession(layer) {
    if (!layer?.shape?.pathNodes?.length) return false;
    activePathSession = {
      nodes: normalizedCurveToCanvasNodes(layer.shape.pathNodes, layer.transform),
      closed: layer.shape.closedPath !== false,
      tension: layer.shape.curveTension ?? 0.5,
      tool: "node",
      hoverPt: null,
      nearStart: false,
      editingLayerId: layer.id,
      activeNodeIndex: -1,
    };
    syncPathSessionHints();
    renderVectorSvgCanvas();
    return true;
  }

  function commitActivePathSession({ closeLoop = null } = {}) {
    if (!activePathSession) return false;
    const sess = activePathSession;
    const isClosed = closeLoop !== null ? closeLoop : sess.closed;
    if (sess.nodes.length < 2) {
      activePathSession = null;
      syncPathSessionHints();
      renderVectorSvgCanvas();
      return false;
    }
    const curveData = nodesToNormalizedCurveData(sess.nodes, isClosed, sess.tension);
    if (!curveData) {
      activePathSession = null;
      syncPathSessionHints();
      renderVectorSvgCanvas();
      return false;
    }

    pushUndo();
    if (sess.editingLayerId) {
      const target = state.doc.layers.find((l) => l.id === sess.editingLayerId);
      if (target) {
        target.transform = { ...target.transform, ...curveData.transform, rotation: 0 };
        target.shape.motifType = "svg-path";
        target.shape.pathNodes = curveData.pathNodes;
        target.shape.closedPath = curveData.closedPath;
        target.shape.curveTension = curveData.curveTension;
        target.shape.svgPath = curveData.svgPath;
        state.doc.selectedLayerId = target.id;
      }
      activePathSession = null;
      selectVectorTool("select");
      evaluateAndRender(true);
      rebuildInspector();
      Toast("Updated curve geometry");
      return true;
    }

    const baseCol = isPattern ? "#eff4cf" : "#e8c27a";
    const created = createArtworkLayer("motif", {
      name: sess.tool === "pen" ? "Pen Path" : "Curve Path",
      transform: curveData.transform,
      shape: {
        motifType: "svg-path",
        color: baseCol,
        colorB: "#5a7692",
        fillMode: "solid",
        fillStops: [{ color: baseCol, offset: 0 }, { color: "#5a7692", offset: 1 }],
        fillEnabled: isClosed,
        strokeWidth: isClosed ? 0 : 6,
        strokeColor: baseCol,
        pathNodes: curveData.pathNodes,
        closedPath: curveData.closedPath,
        curveTension: curveData.curveTension,
        svgPath: curveData.svgPath,
      },
    });
    state.doc.layers.push(created);
    state.doc.selectedLayerId = created.id;
    activePathSession = null;
    selectVectorTool("select");
    evaluateAndRender(true);
    rebuildInspector();
    Toast(`Created ${created.name} (${sess.nodes.length} points)`);
    return true;
  }

  function selectVectorTool(id) {
    if (activePathSession && id !== activePathSession.tool && id !== "node") {
      commitActivePathSession();
    }
    state.vectorTool = id;
    headerUI.setActiveTool(id);
    if (id === "node") {
      const sel = getSelectedLayer();
      if (sel?.shape?.motifType === "svg-path" && sel.shape.pathNodes?.length) {
        startEditLayerCurveSession(sel);
      }
    }
    svgEl.style.cursor = id === "pan" ? "grab" : id === "select" ? "default" : "crosshair";
    syncPathSessionHints();
    if (state.viewChannel === "vector") renderVectorSvgCanvas();
  }

  function getSvgViewMetrics() {
    const rect = svgEl.getBoundingClientRect();
    const w = Math.max(120, rect.width || seViewport.clientWidth - 24 || 640);
    const h = Math.max(120, rect.height || seViewport.clientHeight - 22 || 480);
    const docSize = state.doc.size || ARTWORK_SIZE;
    const baseSpan = docSize * 1.18;
    const spanY = baseSpan / state.zoom;
    const spanX = spanY * (w / h);
    const cx = docSize * 0.5 + state.panX;
    const cy = docSize * 0.5 + state.panY;
    return {
      rect,
      w,
      h,
      docSize,
      vx: cx - spanX * 0.5,
      vy: cy - spanY * 0.5,
      vw: spanX,
      vh: spanY,
    };
  }

  function clientToSvg(clientX, clientY) {
    const m = getSvgViewMetrics();
    const rx = (clientX - m.rect.left) / Math.max(1, m.rect.width);
    const ry = (clientY - m.rect.top) / Math.max(1, m.rect.height);
    return {
      x: m.vx + rx * m.vw,
      y: m.vy + ry * m.vh,
      unitsPerPx: m.vw / Math.max(1, m.rect.width),
    };
  }

  function drawRulers(m) {
    // Top X ruler (Image 1 muted olive ruler styling)
    const rw = Math.max(1, Math.round(rulerXWrap.clientWidth || m.w));
    const rh = Math.max(1, Math.round(rulerXWrap.clientHeight || 24));
    if (rulerXCanvas.width !== rw || rulerXCanvas.height !== rh) {
      rulerXCanvas.width = rw;
      rulerXCanvas.height = rh;
    }
    const rcx = rulerXCanvas.getContext("2d");
    rcx.fillStyle = "#161b12";
    rcx.fillRect(0, 0, rw, rh);
    rcx.strokeStyle = "#36402d";
    rcx.fillStyle = "#7a866b";
    rcx.font = "9px monospace";

    const step = m.vw > 1100 ? 128 : m.vw > 380 ? 64 : 32;
    const firstX = Math.floor(m.vx / step) * step;
    for (let v = firstX; v <= m.vx + m.vw; v += step) {
      const px = ((v - m.vx) / m.vw) * rw;
      const major = v % 64 === 0;
      rcx.beginPath();
      rcx.moveTo(px, major ? 2 : 13);
      rcx.lineTo(px, rh);
      rcx.stroke();
      if (major) {
        rcx.fillText(String(Math.round(v)), px + 4, 12);
      }
    }

    // Left Y ruler (upright horizontal labels like Image 1)
    const yw = Math.max(1, Math.round(rulerYWrap.clientWidth || 30));
    const yh = Math.max(1, Math.round(rulerYWrap.clientHeight || m.h));
    if (rulerYCanvas.width !== yw || rulerYCanvas.height !== yh) {
      rulerYCanvas.width = yw;
      rulerYCanvas.height = yh;
    }
    const rcy = rulerYCanvas.getContext("2d");
    rcy.fillStyle = "#161b12";
    rcy.fillRect(0, 0, yw, yh);
    rcy.strokeStyle = "#36402d";
    rcy.fillStyle = "#7a866b";
    rcy.font = "9px monospace";
    const firstY = Math.floor(m.vy / step) * step;
    for (let v = firstY; v <= m.vy + m.vh; v += step) {
      const py = ((v - m.vy) / m.vh) * yh;
      const major = v % 64 === 0;
      rcy.beginPath();
      rcy.moveTo(major ? 2 : 18, py);
      rcy.lineTo(yw, py);
      rcy.stroke();
      if (major && v >= 0 && v <= (m.docSize || 512) + step) {
        rcy.fillText(String(Math.round(v)), 4, py + 11);
      }
    }
  }

  function renderVectorSvgCanvas() {
    if (disposed) return;
    const m = getSvgViewMetrics();
    const docSize = m.docSize;
    svgEl.setAttribute("viewBox", `${m.vx.toFixed(2)} ${m.vy.toFixed(2)} ${m.vw.toFixed(2)} ${m.vh.toFixed(2)}`);
    zoomReadout.textContent = `${Math.round(state.zoom * 100)}%`;
    const activeSel = getSelectedLayer();
    selCountEl.textContent = activeSel && activeSel.kind !== "folder" ? "1 selected" : "0 selected";

    const { defs: artDefs, body: artBody } = artworkDocumentLayersSvg(state.doc, { idPrefix: "se-live", pointRes: pointerInteraction?.kind === "ptgrad-move" ? 64 : 128 });
    const bgSvg = artworkDocumentBackgroundSvg(state.doc, { id: "se-bg-grad", pointRes: pointerInteraction?.kind === "ptgrad-move" ? 64 : 128 });
    const units = m.vw / Math.max(1, m.w);
    const gridStep = Math.max(8, state.snapStep || 32);

    const tileBg = bgSvg.rect;
    const extraDefs = bgSvg.defs;

    // Adjacent subtle 2x2 ghost repeat preview in Pattern mode
    let ghostTiles = "";
    if (isPattern && state.showTileRepeat) {
      const offsets = [[-docSize, 0], [docSize, 0], [0, -docSize], [0, docSize]];
      ghostTiles = `<g opacity="0.28" pointer-events="none">${offsets
        .map(([ox, oy]) => `<g transform="translate(${ox} ${oy})">${tileBg}${artBody}</g>`)
        .join("")}</g>`;
    }

    // Grid lines + center axes
    let gridMarkup = "";
    if (state.showGrid) {
      gridMarkup += `<rect x="0" y="0" width="${docSize}" height="${docSize}" fill="url(#se-grid)" pointer-events="none"/>`;
      gridMarkup += `<path d="M128 0V${docSize} M256 0V${docSize} M384 0V${docSize} M0 128H${docSize} M0 256H${docSize} M0 384H${docSize}" stroke="rgba(128,166,144,0.22)" stroke-width="${units * 1}" fill="none" pointer-events="none"/>`;
    }
    if (state.showAxes) {
      gridMarkup += `<line x1="${docSize * 0.5}" y1="0" x2="${docSize * 0.5}" y2="${docSize}" stroke="rgba(232,194,122,0.34)" stroke-width="${units * 1.1}" stroke-dasharray="${units * 5} ${units * 4}" pointer-events="none"/>`;
      gridMarkup += `<line x1="0" y1="${docSize * 0.5}" x2="${docSize}" y2="${docSize * 0.5}" stroke="rgba(232,194,122,0.34)" stroke-width="${units * 1.1}" stroke-dasharray="${units * 5} ${units * 4}" pointer-events="none"/>`;
    }

    // Active selection transform box + 8 resize handles + rotation circle
    let selectionMarkup = "";
    const sel = getSelectedLayer();
    if (sel && sel.kind !== "folder" && sel.visible && state.viewChannel === "vector") {
      const { x, y, width, height, rotation } = sel.transform;
      const bw = Math.max(8, width || 32);
      const bh = Math.max(8, height || 32);
      const hw = bw * 0.5, hh = bh * 0.5;
      const hs = Math.max(4, units * 8);
      const hHalf = hs * 0.5;
      const rotY = -hh - units * 28;
      const handlePositions = [
        ["nw", -hw, -hh, "nwse-resize"],
        ["n", 0, -hh, "ns-resize"],
        ["ne", hw, -hh, "nesw-resize"],
        ["e", hw, 0, "ew-resize"],
        ["se", hw, hh, "nwse-resize"],
        ["s", 0, hh, "ns-resize"],
        ["sw", -hw, hh, "nesw-resize"],
        ["w", -hw, 0, "ew-resize"],
      ];
      selectionMarkup = `
        <g class="se-selection-gizmo" transform="translate(${x} ${y}) rotate(${rotation || 0})">
          <rect x="${-hw}" y="${-hh}" width="${bw}" height="${bh}" fill="transparent" stroke="#c1dcab" stroke-width="${units * 1.6}" data-layer-id="${String(sel.id).replace(/"/g, "&quot;")}" style="cursor:move;"/>
          <line x1="0" y1="${-hh}" x2="0" y2="${rotY}" stroke="#c1dcab" stroke-width="${units * 1.3}" pointer-events="none"/>
          <circle cx="0" cy="${rotY}" r="${units * 6}" fill="#17190e" stroke="#c1dcab" stroke-width="${units * 1.8}" data-handle="rotate" style="cursor:grab;"/>
          ${handlePositions
            .map(
              ([id, hx, hy, cur]) =>
                `<rect x="${hx - hHalf}" y="${hy - hHalf}" width="${hs}" height="${hs}" rx="${units * 1.2}" fill="#eff4cf" stroke="#17190e" stroke-width="${units * 1.4}" data-handle="${id}" style="cursor:${cur};"/>`,
            )
            .join("")}
        </g>`;
    }

    // Live draw preview rect or live interactive Curve/Pen/Node overlay
    let drawDraftMarkup = "";
    if (pointerInteraction?.kind === "draw" && pointerInteraction.draftRect) {
      const dr = pointerInteraction.draftRect;
      drawDraftMarkup = `<rect x="${dr.x}" y="${dr.y}" width="${dr.w}" height="${dr.h}" fill="rgba(239,244,207,0.12)" stroke="#eff4cf" stroke-width="${units * 1.6}" stroke-dasharray="${units * 5} ${units * 4}" pointer-events="none"/>`;
    }
    if (activePathSession && state.viewChannel === "vector") {
      const sess = activePathSession;
      const previewNodes = sess.hoverPt && !sess.editingLayerId && !sess.nearStart
        ? [...sess.nodes, { x: sess.hoverPt.x, y: sess.hoverPt.y, mode: sess.tool === "curve" ? "auto" : "corner" }]
        : sess.nodes;
      const previewD = previewNodes.length >= 2 ? pathNodesToSvgD(previewNodes, sess.closed, sess.tension) : "";
      const committedD = sess.nodes.length >= 2 ? pathNodesToSvgD(sess.nodes, sess.closed, sess.tension) : "";
      const nr = units * 5.2;
      const hr = units * 3.8;
      let nodesSvg = "";
      sess.nodes.forEach((n, idx) => {
        const isFirst = idx === 0;
        const isActive = idx === sess.activeNodeIndex;
        if (n.in) {
          nodesSvg += `<line x1="${n.x}" y1="${n.y}" x2="${n.x + n.in.x}" y2="${n.y + n.in.y}" stroke="#7dd3fc" stroke-width="${units * 1.1}" opacity="0.8"/>`;
          nodesSvg += `<circle cx="${n.x + n.in.x}" cy="${n.y + n.in.y}" r="${hr}" fill="#7dd3fc" stroke="#141619" stroke-width="${units * 1.1}" data-path-handle="in:${idx}" style="cursor:grab;"/>`;
        }
        if (n.out) {
          nodesSvg += `<line x1="${n.x}" y1="${n.y}" x2="${n.x + n.out.x}" y2="${n.y + n.out.y}" stroke="#7dd3fc" stroke-width="${units * 1.1}" opacity="0.8"/>`;
          nodesSvg += `<circle cx="${n.x + n.out.x}" cy="${n.y + n.out.y}" r="${hr}" fill="#7dd3fc" stroke="#141619" stroke-width="${units * 1.1}" data-path-handle="out:${idx}" style="cursor:grab;"/>`;
        }
        const strokeCol = isFirst && sess.nearStart ? "#4ade80" : isActive ? "#eff4cf" : "#e8c27a";
        const fillCol = isFirst && sess.nearStart ? "#4ade80" : isActive ? "#e8c27a" : "#161915";
        nodesSvg += `<circle cx="${n.x}" cy="${n.y}" r="${isFirst && sess.nearStart ? nr * 1.45 : nr}" fill="${fillCol}" stroke="${strokeCol}" stroke-width="${units * 1.8}" data-path-node="${idx}" style="cursor:move;"/>`;
      });
      drawDraftMarkup += `
        <g class="se-curve-overlay">
          ${previewD && previewD !== committedD ? `<path d="${previewD}" fill="none" stroke="rgba(239,244,207,0.45)" stroke-width="${units * 1.6}" stroke-dasharray="${units * 4} ${units * 4}" pointer-events="none"/>` : ""}
          ${committedD ? `<path d="${committedD}" fill="${sess.closed ? "rgba(232,194,122,0.14)" : "none"}" stroke="#eff4cf" stroke-width="${units * 2.1}" stroke-linecap="round" pointer-events="none"/>` : ""}
          ${nodesSvg}
        </g>`;
    }

    // Scene-space interactive Point Gradient nodes overlay
    let pointGradMarkup = "";
    const ptGradTarget = getPointGradientTarget(false);
    if (ptGradTarget && (state.vectorTool === "gradient" || state.vectorTool === "select") && state.viewChannel === "vector" && !activePathSession) {
      const grad = ptGradTarget.getGrad();
      const selPtIdx = Math.min(Math.max(0, state.selectedPointGradIdx || 0), Math.max(0, grad.points.length - 1));
      const nr = units * 8.5;
      const selPt = grad.points[selPtIdx];
      let rings = "";
      if (selPt) {
        rings += `<circle cx="${selPt.x}" cy="${selPt.y}" r="${Math.max(12, selPt.radius * (grad.falloff || 1))}" fill="none" stroke="rgba(239,244,207,0.34)" stroke-width="${units * 1.1}" stroke-dasharray="${units * 4} ${units * 4}" pointer-events="none"/>`;
      }
      const handles = grad.points.map((p, idx) => {
        const isSel = idx === selPtIdx;
        const r = isSel ? nr * 1.2 : nr;
        return `
          <g data-ptgrad-id="${p.id}" data-ptgrad-idx="${idx}" style="cursor:move;">
            <circle cx="${p.x}" cy="${p.y}" r="${r + units * 2.2}" fill="none" stroke="${isSel ? "#eff4cf" : "rgba(0,0,0,0.45)"}" stroke-width="${units * (isSel ? 2.1 : 1.2)}"/>
            <circle cx="${p.x}" cy="${p.y}" r="${r}" fill="${p.color}" stroke="#141619" stroke-width="${units * 1.6}"/>
            <text x="${p.x}" y="${p.y}" text-anchor="middle" dominant-baseline="central" font-family="monospace" font-size="${units * 9}" font-weight="700" fill="#141619" stroke="rgba(255,255,255,0.65)" stroke-width="${units * 0.45}" paint-order="stroke" pointer-events="none">${idx + 1}</text>
          </g>`;
      }).join("");
      pointGradMarkup = `<g class="se-ptgrad-overlay">${rings}${handles}</g>`;
    }

    svgEl.innerHTML = `
      <defs>
        <pattern id="se-checker" width="24" height="24" patternUnits="userSpaceOnUse">
          <rect width="24" height="24" fill="#171b19"/>
          <rect width="12" height="12" fill="#1e2321"/>
          <rect x="12" y="12" width="12" height="12" fill="#1e2321"/>
        </pattern>
        <pattern id="se-grid" width="${gridStep}" height="${gridStep}" patternUnits="userSpaceOnUse">
          <path d="M ${gridStep} 0 L 0 0 0 ${gridStep}" fill="none" stroke="rgba(103,135,118,0.28)" stroke-width="${units * 0.9}"/>
        </pattern>
        <clipPath id="se-tile-clip">
          <rect x="0" y="0" width="${docSize}" height="${docSize}"/>
        </clipPath>
        ${extraDefs}
        ${artDefs}
      </defs>
      ${ghostTiles}
      <rect x="0" y="0" width="${docSize}" height="${docSize}" fill="url(#se-checker)" data-canvas-bg="1"/>
      ${tileBg}
      <g clip-path="url(#se-tile-clip)">
        ${artBody}
      </g>
      ${gridMarkup}
      <rect x="0" y="0" width="${docSize}" height="${docSize}" fill="none" stroke="#c1dcab" stroke-opacity="0.52" stroke-width="${units * 1.4}" pointer-events="none"/>
      ${selectionMarkup}
      ${pointGradMarkup}
      ${drawDraftMarkup}
    `;
    drawRulers(m);
  }

  function updatePbrPreviews() {
    if (!state.lastEval || disposed) return;
    const ev = state.lastEval;
    const w = ev.width;
    const h = ev.heightPx || w;

    if (state.viewChannel !== "vector") {
      const chanData =
        state.viewChannel === "albedo" || state.viewChannel === "composite" || state.viewChannel === "tile2x2"
          ? ev.albedo
          : state.viewChannel === "roughness"
            ? ev.roughness
            : state.viewChannel === "metalness"
              ? ev.metalness
              : state.viewChannel === "height"
                ? ev.height
                : state.viewChannel === "normal"
                  ? ev.normal
                  : (ev.extraChannels?.[state.viewChannel] || ev.albedo);
      const repeat = state.viewChannel === "tile2x2" ? 2 : 1;
      const checker = state.viewChannel === "composite" || state.viewChannel === "tile2x2" || state.viewChannel === "albedo" || state.viewChannel === "opacity";
      drawImageDataToCanvas(mainCanvas, chanData, w, h, checker, repeat);
    }
  }

  function syncViewDisplay() {
    const isVector = state.viewChannel === "vector";
    pbrWrap.hidden = isVector;
    svgEl.hidden = !isVector;
    viewControls.hidden = !isVector;
    rulerCorner.hidden = !isVector;
    rulerXWrap.hidden = !isVector;
    rulerYWrap.hidden = !isVector;
    if (isVector) {
      renderVectorSvgCanvas();
    } else {
      const isExtKey = !!ARTWORK_PBR_CHANNEL_MAP[state.viewChannel]?.extended && state.viewChannel !== "normal";
      if (isExtKey && !state.lastEval?.extraChannels?.[state.viewChannel]) {
        evaluateAndRender(false);
      } else {
        updatePbrPreviews();
      }
    }
  }

  function evaluateAndRender(reRenderLeft = false, { skipPbr = false } = {}) {
    if (disposed) return;
    if (state.viewChannel === "vector") renderVectorSvgCanvas();
    if (!skipPbr) {
      const isExtKey = !!ARTWORK_PBR_CHANNEL_MAP[state.viewChannel]?.extended && state.viewChannel !== "normal";
      const ev = evaluateArtworkDocument(state.doc, 384, isExtKey ? { extraChannels: [state.viewChannel] } : {});
      state.lastEval = ev;
      state.lastStats = ev.stats;
      updatePbrPreviews();
    }
    const sel = getSelectedLayer();
    if (sel && sel.kind !== "folder") {
      for (const card of rightInner.querySelectorAll(".pbr-channel-card")) {
        const k = card.dataset?.pbrChannel;
        if (!k) continue;
        const val = getArtworkChannelValue(sel, k);
        const pill = card.querySelector(".pbr-card-val");
        if (pill) pill.textContent = formatArtworkChannelValue(k, val);
        const dot = card.querySelector(".pbr-card-dot");
        if (dot && ARTWORK_PBR_CHANNEL_MAP[k]?.isColor) dot.style.background = String(val);
      }
    }
    headerUI.setName(state.doc.name || (isPattern ? "Pattern" : "Stamp"));
    headerUI.setResolution(state.resolution);
    if (reRenderLeft) renderLeftColumn();
    Broadcast();
  }

  // ----------------------------------------------------------------- LEFT LAYER STACK (FULL DESIGN + LIVE PREVIEWS)
  const ARTWORK_ADD_OPTIONS = [
    { kind: "motif", label: "+ Motif", glyph: "Star", hint: "Vector shape / procedural motif" },
    { kind: "text", label: "+ Text", glyph: "Type", hint: "Typography & engraved label" },
    { kind: "gradient", label: "+ Grad", glyph: "Sliders", hint: "Linear / radial surface gradient" },
    { kind: "folder", label: "+ Folder", glyph: "Folder", hint: "Group & nest child layers" },
  ];

  function moveLayerByOffset(layerId, deltaUp) {
    const idx = state.doc.layers.findIndex((l) => l.id === layerId);
    if (idx < 0) return false;
    const nextIdx = idx + (deltaUp > 0 ? 1 : -1);
    if (nextIdx < 0 || nextIdx >= state.doc.layers.length) return false;
    pushUndo();
    const [item] = state.doc.layers.splice(idx, 1);
    state.doc.layers.splice(nextIdx, 0, item);
    evaluateAndRender(true);
    rebuildInspector();
    return true;
  }

  function moveLayerRelativeTo(sourceId, targetId, dropAboveInVisual) {
    if (!sourceId || sourceId === targetId) return false;
    const srcIdx = state.doc.layers.findIndex((l) => l.id === sourceId);
    const tgtIdx = state.doc.layers.findIndex((l) => l.id === targetId);
    if (srcIdx < 0 || tgtIdx < 0) return false;
    pushUndo();
    const [item] = state.doc.layers.splice(srcIdx, 1);
    const newTgtIdx = state.doc.layers.findIndex((l) => l.id === targetId);
    const insertAt = dropAboveInVisual ? newTgtIdx + 1 : newTgtIdx;
    state.doc.layers.splice(insertAt, 0, item);
    evaluateAndRender(true);
    rebuildInspector();
    return true;
  }

  function addArtworkLayer(kind) {
    pushUndo();
    const layer = createArtworkLayer(kind, {
      name: kind === "text" ? "Decal Typography" : kind === "gradient" ? "Gradient Wash" : kind === "folder" ? "Group Folder" : "Motif Shape",
      shape: { color: isPattern ? "#eff4cf" : "#e8c27a" },
    });
    state.doc.layers.push(layer);
    state.doc.selectedLayerId = layer.id;
    evaluateAndRender(true);
    rebuildInspector();
  }

  function renderLeftColumn() {
    const sel = getSelectedLayer();
    const totalMods = state.doc.layers.reduce((acc, l) => acc + ((l.modifiers || []).filter((m) => m.enabled !== false).length), 0);
    const bg = state.doc.background || {};

    layerStackUI.render({
      layers: state.doc.layers,
      limit: 32,
      selectedId: sel?.id || null,
      addOptions: ARTWORK_ADD_OPTIONS,
      extraMetricValue: totalMods,
      systemBase: {
        title: `Canvas Tile · ${state.resolution}×${state.resolution}`,
        meta: bg.enabled ? `Background ${bg.color || "#171a1f"}` : "Transparent alpha cutout",
        badge: isPattern ? "Seamless" : "Cutout",
      },
      renderThumb(layer, thumbEl, { isFolder }) {
        if (isFolder) {
          thumbEl.classList.add("tp-kind-folder");
          thumbEl.append(ConstructGlyph("Folder"));
          return;
        }
        thumbEl.innerHTML = buildSingleLayerThumbSvg(state.doc, layer);
      },
      getSubtitle(layer) {
        const modCount = (layer.modifiers || []).filter((m) => m.enabled !== false).length;
        const kindTag = layer.kind === "motif" ? (layer.shape?.motifType || "motif") : layer.kind;
        const blendTag = layer.blend && layer.blend !== "Normal" ? ` · ${layer.blend}` : "";
        const modTag = modCount > 0 ? ` · ${modCount} mod${modCount > 1 ? "s" : ""}` : "";
        return `${kindTag}${blendTag}${modTag}`;
      },
      getMaskSubrow(layer) {
        if (!layer.mask) return null;
        const thumbWrap = Construct("div", "");
        thumbWrap.innerHTML = buildMaskMiniSvg(layer.mask);
        return {
          enabled: !!layer.mask.enabled,
          active: false,
          label: "Layer mask",
          meta: `${layer.mask.mode || "noise"} · T:${Number(layer.mask.threshold ?? 0.45).toFixed(2)}`,
          thumbEl: thumbWrap.firstElementChild || null,
          onSelect: () => {
            state.doc.selectedLayerId = layer.id;
            renderLeftColumn();
            rebuildInspector();
          },
          onToggle: () => {
            layer.mask.enabled = !layer.mask.enabled;
            evaluateAndRender(true);
            rebuildInspector();
          },
        };
      },
      renderExpanded(layer, expEl) {
        const pills = Construct("div", "tp-exp-pills");
        const pbrMask = getArtworkLayerPbrMask(layer);
        for (const chDef of ARTWORK_PBR_CHANNELS) {
          if (!pbrMask[chDef.key]) continue;
          const dot = document.createElement("i");
          dot.style.setProperty("--pill-color", chDef.isColor ? String(getArtworkChannelValue(layer, chDef.key)) : "#9ea2aa");
          pills.append(Construct("span", "tp-exp-pill", [dot, Construct("span", null, chDef.short.toUpperCase())]));
        }
        for (const mod of (layer.modifiers || []).filter((m) => m.enabled !== false)) {
          const dot = document.createElement("i");
          dot.style.setProperty("--pill-color", "#9ea2aa");
          pills.append(Construct("span", "tp-exp-pill", [dot, Construct("span", null, mod.type)]));
        }
        expEl.append(
          pills,
          Construct("div", "tp-exp-meta", `Pos (${Math.round(layer.transform?.x ?? 256)}, ${Math.round(layer.transform?.y ?? 256)}) · ${Math.round(layer.transform?.width ?? 64)}×${Math.round(layer.transform?.height ?? 64)}`),
        );
      },
      onSelectLayer: (id) => {
        state.doc.selectedLayerId = id;
        renderLeftColumn();
        if (state.viewChannel === "vector") renderVectorSvgCanvas();
        rebuildInspector();
      },
      onAddLayer: addArtworkLayer,
      onMoveLayer: moveLayerByOffset,
      onDropReorder: moveLayerRelativeTo,
      onToggleVisible: (id) => {
        const l = state.doc.layers.find((x) => x.id === id);
        if (!l) return;
        l.visible = l.visible === false;
        evaluateAndRender(true);
      },
      onToggleLock: (id) => {
        const l = state.doc.layers.find((x) => x.id === id);
        if (!l) return;
        l.locked = !l.locked;
        renderLeftColumn();
        rebuildInspector();
      },
      onRenameLayer: (id, nextName) => {
        const l = state.doc.layers.find((x) => x.id === id);
        if (l) { l.name = nextName; rebuildInspector(); }
      },
      onOpacityChange: (id, val, final) => {
        const l = state.doc.layers.find((x) => x.id === id);
        if (!l) return;
        l.opacity = val;
        evaluateAndRender(final, { skipPbr: !final });
      },
      onDuplicateLayer: (id) => {
        const src = state.doc.layers.find((x) => x.id === id);
        if (!src) return;
        pushUndo();
        const copy = structuredClone(src);
        copy.id = `lyr_${Math.random().toString(36).slice(2, 9)}`;
        copy.name = `${src.name} Copy`;
        if (copy.transform && copy.kind !== "folder") {
          copy.transform.x = Math.min(512, (copy.transform.x ?? 256) + 18);
          copy.transform.y = Math.min(512, (copy.transform.y ?? 256) + 18);
        }
        const idx = state.doc.layers.findIndex((x) => x.id === id);
        state.doc.layers.splice(idx + 1, 0, copy);
        state.doc.selectedLayerId = copy.id;
        evaluateAndRender(true);
        rebuildInspector();
        Toast(`Duplicated "${src.name}"`);
      },
      onToggleLayerMask: (id) => {
        const l = state.doc.layers.find((x) => x.id === id);
        if (!l || l.kind === "folder") return;
        pushUndo();
        if (!l.mask) {
          l.mask = { enabled: true, invert: false, mode: "noise", frequency: 4, threshold: 0.45, softness: 0.25, seed: 211 };
        } else {
          l.mask.enabled = !l.mask.enabled;
        }
        state.doc.selectedLayerId = l.id;
        evaluateAndRender(true);
        rebuildInspector();
      },
      onDeleteLayer: (id) => {
        if (state.doc.layers.length <= 1) {
          Toast("Keep at least one layer in stack");
          return;
        }
        pushUndo();
        state.doc.layers = state.doc.layers.filter((l) => l.id !== id);
        if (state.doc.selectedLayerId === id) state.doc.selectedLayerId = state.doc.layers[state.doc.layers.length - 1].id;
        evaluateAndRender(true);
        rebuildInspector();
      },
    });
  }

  // ----------------------------------------------------------------- RIGHT INSPECTOR
  function rebuildInspector() {
    if (inspectorScope) ReleaseScope(inspectorScope);
    inspectorScope = OpenScope();
    rightInner.innerHTML = "";

    const layer = getSelectedLayer();
    if (!layer) return;

    rightInner.append(
      Construct("div", "object-heading", [
        Construct("div", "object-symbol", ConstructGlyph(isPattern ? "Pattern" : "Stamp")),
        Construct("div", "object-heading-copy", [
          Construct("strong", null, state.doc.name || (isPattern ? "Pattern Document" : "Stamp Decal")),
          Construct("span", null, `ACTIVE LAYER: ${layer.name}`),
        ]),
      ]),
    );

    MetricTrio(rightInner, [
      { Label: "Instances", Highlight: true, Read: () => ({ value: String(state.lastStats.instances || 1), sub: "Evaluated" }) },
      { Label: "Scale Span", Read: () => ({ value: `${Number(state.lastStats.minScale ?? 1).toFixed(2)}–${Number(state.lastStats.maxScale ?? 1).toFixed(2)}`, sub: "Noise Mod" }) },
      { Label: "Coverage", Read: () => ({ value: `${Math.round(state.lastStats.coveragePct ?? 0)}%`, sub: `${state.doc.layers.length} Layers` }) },
    ]);

    const lSec = SectionCard(rightInner, { Title: "Layer Properties", Glyph: "Layers", Badge: layer.kind.toUpperCase() });
    TextRow(lSec.Inner, {
      Label: "Layer Name",
      Read: () => layer.name,
      Write: (v) => { layer.name = v; renderLeftColumn(); },
    });

    const appendColourSourceControls = (parentEl, {
      label = "Fill",
      slot = "fill",
      allowSolid = true,
      getMode,
      setMode,
      getStops,
      setStops,
      getAngle,
      setAngle,
      getPointGrad,
      setPointGrad,
      fallbackA = "#e8c27a",
      fallbackB = "#5a7692",
    }) => {
      const modeOpts = allowSolid
        ? [["Solid", "solid"], ["Linear Grad", "linear"], ["Radial Grad", "radial"], ["Point Field", "point"]]
        : [["Linear Grad", "linear"], ["Radial Grad", "radial"], ["Point Field", "point"]];
      SegmentedRow(parentEl, {
        Label: `${label} Source`,
        Options: modeOpts,
        Read: () => getMode() || (allowSolid ? "solid" : "linear"),
        Write: (v) => {
          setMode(v);
          if (v === "point" && getPointGrad && setPointGrad) {
            state.activePointGradSlot = slot;
            setPointGrad(normalizePointGradient2D(getPointGrad(), fallbackA, fallbackB, state.doc.size || ARTWORK_SIZE));
          } else if (v !== "solid" && (!getStops() || getStops().length < 2)) {
            setStops(normalizeGradientStops(getStops(), fallbackA, fallbackB));
          }
          evaluateAndRender(true);
          rebuildInspector();
        },
      });
      const curMode = getMode() || (allowSolid ? "solid" : "linear");
      if (curMode === "solid") {
        ColorRow(parentEl, {
          Label: `${label} Colour`,
          Read: () => (getStops()?.[0]?.color) || fallbackA,
          Write: (v) => {
            const cur = normalizeGradientStops(getStops(), v, fallbackB);
            cur[0] = { ...cur[0], color: v };
            setStops(cur, v);
            evaluateAndRender(true);
          },
        });
        return;
      }
      if (curMode === "point" && getPointGrad && setPointGrad) {
        const docSize = state.doc.size || ARTWORK_SIZE;
        const grad = normalizePointGradient2D(getPointGrad(), fallbackA, fallbackB, docSize);
        setPointGrad(grad);
        state.selectedPointGradIdx = Math.min(Math.max(0, state.selectedPointGradIdx || 0), Math.max(0, grad.points.length - 1));
        const wrap = Construct("div", "se-gradient-editor");
        wrap.style.cssText = "display:flex;flex-direction:column;gap:6px;padding:6px 0;";

        const isEditingScene = state.vectorTool === "gradient" && state.activePointGradSlot === slot;
        const sceneBtn = Construct("button", "chip-btn" + (isEditingScene ? " active" : ""), isEditingScene ? "Done Placing Points" : "Edit Points in Scene (F)");
        sceneBtn.type = "button";
        sceneBtn.addEventListener("click", () => {
          state.activePointGradSlot = slot;
          selectVectorTool(isEditingScene ? "select" : "gradient");
          rebuildInspector();
        });
        wrap.append(sceneBtn);

        const ptStrip = Construct("div", "chip-strip");
        grad.points.forEach((pt, idx) => {
          const b = Construct("button", "chip-btn" + (idx === state.selectedPointGradIdx ? " active" : ""), `${idx + 1}`);
          b.type = "button";
          b.style.borderLeft = `6px solid ${pt.color}`;
          b.addEventListener("click", () => {
            state.activePointGradSlot = slot;
            state.selectedPointGradIdx = idx;
            renderVectorSvgCanvas();
            rebuildInspector();
          });
          ptStrip.append(b);
        });
        const addPtBtn = Construct("button", "chip-btn", "+ Point");
        addPtBtn.type = "button";
        addPtBtn.disabled = grad.points.length >= 12;
        addPtBtn.addEventListener("click", () => {
          if (grad.points.length >= 12) return;
          pushUndo();
          const idx = grad.points.length;
          const palette = ["#eff4cf", "#e8c27a", "#5a7692", "#c87849", "#699c8c", "#d890a8", "#8fb8de"];
          grad.points.push({
            id: `pg_${Date.now().toString(36)}_${idx}`,
            x: Math.round(docSize * (0.25 + ((idx * 0.23) % 0.55))),
            y: Math.round(docSize * (0.28 + ((idx * 0.31) % 0.52))),
            color: palette[idx % palette.length],
            radius: Math.round(docSize * 0.42),
            weight: 1.0,
          });
          state.activePointGradSlot = slot;
          state.selectedPointGradIdx = grad.points.length - 1;
          setPointGrad(grad);
          evaluateAndRender(true);
          rebuildInspector();
        });
        ptStrip.append(addPtBtn);
        wrap.append(ptStrip);

        const selPt = grad.points[state.selectedPointGradIdx];
        if (selPt) {
          ColorRow(wrap, {
            Label: `Point ${state.selectedPointGradIdx + 1} Colour`,
            Read: () => selPt.color,
            Write: (v) => {
              selPt.color = v;
              setPointGrad(grad);
              evaluateAndRender(true);
              rebuildInspector();
            },
          });
          SliderRow(wrap, {
            Label: `Point ${state.selectedPointGradIdx + 1} Radius`,
            Minimum: 24, Maximum: Math.round(docSize * 1.5), Step: 4, Unit: "px",
            Read: () => selPt.radius,
            Write: (v) => {
              selPt.radius = v;
              setPointGrad(grad);
              evaluateAndRender(true);
            },
          });
          SliderRow(wrap, {
            Label: `Point ${state.selectedPointGradIdx + 1} Weight`,
            Minimum: 0.05, Maximum: 1.0, Step: 0.05,
            Read: () => selPt.weight,
            Write: (v) => {
              selPt.weight = v;
              setPointGrad(grad);
              evaluateAndRender(true);
            },
          });
        }
        SliderRow(wrap, {
          Label: "Global Spread",
          Minimum: 0.3, Maximum: 2.5, Step: 0.05,
          Read: () => grad.falloff ?? 1.0,
          Write: (v) => {
            grad.falloff = v;
            setPointGrad(grad);
            evaluateAndRender(true);
          },
        });
        ColorRow(wrap, {
          Label: "Field Base Colour",
          Read: () => grad.background || fallbackB,
          Write: (v) => {
            grad.background = v;
            setPointGrad(grad);
            evaluateAndRender(true);
          },
        });
        if (grad.points.length > 1) {
          const rmBtn = Construct("button", "chip-btn", `Remove Point ${state.selectedPointGradIdx + 1}`);
          rmBtn.type = "button";
          rmBtn.addEventListener("click", () => {
            if (grad.points.length <= 1) return;
            pushUndo();
            grad.points.splice(state.selectedPointGradIdx, 1);
            state.selectedPointGradIdx = Math.max(0, state.selectedPointGradIdx - 1);
            setPointGrad(grad);
            evaluateAndRender(true);
            rebuildInspector();
          });
          wrap.append(rmBtn);
        }
        parentEl.append(wrap);
        return;
      }

      const stops = normalizeGradientStops(getStops(), fallbackA, fallbackB);
      const wrap = Construct("div", "se-gradient-editor");
      wrap.style.cssText = "display:flex;flex-direction:column;gap:6px;padding:6px 0;";

      const bar = Construct("div", "se-gradient-bar");
      const cssGrad = stops.map((s) => `${s.color} ${(s.offset * 100).toFixed(1)}%`).join(", ");
      bar.style.cssText = `height:14px;width:100%;border:1px solid rgba(255,255,255,0.14);background:${
        curMode === "radial"
          ? `radial-gradient(circle, ${cssGrad})`
          : `linear-gradient(${Math.round(getAngle?.() ?? 45)}deg, ${cssGrad})`
      };`;
      wrap.append(bar);

      if (curMode === "linear" && getAngle && setAngle) {
        SliderRow(wrap, {
          Label: "Gradient Angle",
          Minimum: -180,
          Maximum: 180,
          Step: 1,
          Unit: "°",
          Read: () => getAngle() ?? 45,
          Write: (v) => {
            setAngle(v);
            evaluateAndRender(true);
          },
        });
      }

      stops.forEach((stop, sIdx) => {
        const sRow = Construct("div", "se-gradient-stop-row");
        sRow.style.cssText = "display:grid;grid-template-columns:22px 1fr 44px 22px;align-items:center;gap:6px;";
        const colInp = document.createElement("input");
        colInp.type = "color";
        colInp.className = "colour-stop-native";
        colInp.value = stop.color;
        colInp.addEventListener("input", () => {
          const next = stops.map((st, idx) => (idx === sIdx ? { ...st, color: colInp.value } : st));
          setStops(next);
          const nextCss = normalizeGradientStops(next).map((s) => `${s.color} ${(s.offset * 100).toFixed(1)}%`).join(", ");
          bar.style.background = curMode === "radial" ? `radial-gradient(circle, ${nextCss})` : `linear-gradient(${Math.round(getAngle?.() ?? 45)}deg, ${nextCss})`;
          evaluateAndRender(false, { skipPbr: true });
        });
        colInp.addEventListener("change", () => evaluateAndRender(true));

        const posRange = document.createElement("input");
        posRange.type = "range";
        posRange.min = "0";
        posRange.max = "100";
        posRange.step = "1";
        posRange.value = String(Math.round(stop.offset * 100));
        const pctTag = Construct("span", "property-hint", `${Math.round(stop.offset * 100)}%`);
        pctTag.style.cssText = "font-size:10px;text-align:right;margin:0;";
        posRange.addEventListener("input", () => {
          const pct = Math.max(0, Math.min(100, Number(posRange.value)));
          pctTag.textContent = `${pct}%`;
          const next = stops.map((st, idx) => (idx === sIdx ? { ...st, offset: pct / 100 } : st));
          setStops(next);
          evaluateAndRender(false, { skipPbr: true });
        });
        posRange.addEventListener("change", () => {
          evaluateAndRender(true);
          rebuildInspector();
        });

        const delBtn = Construct("button", "stack-mini-btn", "×");
        delBtn.type = "button";
        delBtn.disabled = stops.length <= 2;
        delBtn.title = stops.length <= 2 ? "Minimum 2 gradient stops" : "Remove stop";
        delBtn.addEventListener("click", () => {
          if (stops.length <= 2) return;
          const next = stops.filter((_, idx) => idx !== sIdx);
          setStops(next);
          evaluateAndRender(true);
          rebuildInspector();
        });

        sRow.append(colInp, posRange, pctTag, delBtn);
        wrap.append(sRow);
      });

      const addStopBtn = Construct("button", "chip-btn", `+ Add ${label} Stop`);
      addStopBtn.type = "button";
      addStopBtn.disabled = stops.length >= 12;
      addStopBtn.addEventListener("click", () => {
        if (stops.length >= 12) return;
        const last = stops[stops.length - 1];
        const prev = stops[stops.length - 2] || last;
        const mid = Math.min(0.95, Math.max(0.05, (prev.offset + last.offset) * 0.5));
        const next = [...stops.slice(0, -1), { color: "#eff4cf", offset: mid }, last];
        setStops(next);
        evaluateAndRender(true);
        rebuildInspector();
      });
      wrap.append(addStopBtn);
      parentEl.append(wrap);
    };

    if (layer.kind === "motif") {
      SelectRow(lSec.Inner, {
        Label: "Motif Shape",
        Options: [...MOTIF_TYPES.map((m) => [m.label, m.id]), ...(layer.shape.motifType === "svg-path" ? [["Curve / Vector Path", "svg-path"]] : [])],
        Read: () => layer.shape.motifType,
        Write: (v) => { layer.shape.motifType = v; evaluateAndRender(true); rebuildInspector(); },
      });
      if (layer.shape.motifType === "svg-path" && layer.shape.pathNodes?.length) {
        SwitchRow(lSec.Inner, {
          Label: "Closed Curve Loop",
          Read: () => layer.shape.closedPath !== false,
          Write: (v) => {
            layer.shape.closedPath = !!v;
            layer.shape.svgPath = pathNodesToSvgD(layer.shape.pathNodes, layer.shape.closedPath, layer.shape.curveTension ?? 0.5);
            evaluateAndRender(true);
          },
        });
        SliderRow(lSec.Inner, {
          Label: "Curve Smoothness",
          Minimum: 0.1,
          Maximum: 1.0,
          Step: 0.05,
          Read: () => layer.shape.curveTension ?? 0.5,
          Write: (v) => {
            layer.shape.curveTension = v;
            layer.shape.svgPath = pathNodesToSvgD(layer.shape.pathNodes, layer.shape.closedPath !== false, v);
            evaluateAndRender(true);
          },
        });
        ActionRow(lSec.Inner, [
          {
            Label: "Edit Curve Points (A)",
            Glyph: "Curve",
            Tone: "accent",
            Act: () => {
              state.vectorTool = "node";
              headerUI.setActiveTool("node");
              startEditLayerCurveSession(layer);
            },
          },
        ]);
      }
    } else if (layer.kind === "text") {
      TextRow(lSec.Inner, {
        Label: "Text Content",
        Read: () => layer.text?.value || "FRONTIER",
        Write: (v) => { if (!layer.text) layer.text = {}; layer.text.value = v; evaluateAndRender(true); },
      });
      SliderRow(lSec.Inner, {
        Label: "Font Size",
        Minimum: 14, Maximum: 160, Step: 2, Unit: "px",
        Read: () => layer.text?.fontSize ?? 54,
        Write: (v) => { if (!layer.text) layer.text = {}; layer.text.fontSize = v; evaluateAndRender(true); },
      });
    }

    if (layer.kind !== "folder") {
      if (layer.kind !== "gradient") {
        SwitchRow(lSec.Inner, {
          Label: "Fill Enabled",
          Read: () => layer.shape.fillEnabled !== false,
          Write: (v) => { layer.shape.fillEnabled = !!v; evaluateAndRender(true); rebuildInspector(); },
        });
      }
      if (layer.shape.fillEnabled !== false || layer.kind === "gradient") {
        appendColourSourceControls(lSec.Inner, {
          label: "Fill",
          slot: "fill",
          allowSolid: layer.kind !== "gradient",
          getMode: () => layer.shape.fillMode || (layer.kind === "gradient" ? "linear" : "solid"),
          setMode: (m) => { layer.shape.fillMode = m; },
          getStops: () => layer.shape.fillStops,
          setStops: (stops, primaryOverride) => {
            const norm = normalizeGradientStops(stops, layer.shape.color, layer.shape.colorB);
            layer.shape.fillStops = norm;
            layer.shape.color = primaryOverride || norm[0].color;
            layer.shape.colorB = norm[norm.length - 1].color;
          },
          getAngle: () => layer.shape.gradientAngle ?? 45,
          setAngle: (a) => { layer.shape.gradientAngle = a; },
          getPointGrad: () => layer.shape.fillPointGradient,
          setPointGrad: (g) => { layer.shape.fillPointGradient = g; },
          fallbackA: layer.shape.color || "#e8c27a",
          fallbackB: layer.shape.colorB || "#5a7692",
        });
      }
      SliderRow(lSec.Inner, {
        Label: "Stroke Outline",
        Minimum: 0, Maximum: 18, Step: 0.5, Unit: "px",
        Read: () => layer.shape.strokeWidth ?? 0,
        Write: (v) => {
          const hadStroke = (layer.shape.strokeWidth || 0) > 0;
          layer.shape.strokeWidth = v;
          evaluateAndRender(true);
          if ((v > 0) !== hadStroke) rebuildInspector();
        },
      });
      if ((layer.shape.strokeWidth || 0) > 0 || layer.shape.motifType === "line" || layer.shape.fillEnabled === false) {
        appendColourSourceControls(lSec.Inner, {
          label: "Stroke",
          slot: "stroke",
          allowSolid: true,
          getMode: () => layer.shape.strokeMode || "solid",
          setMode: (m) => { layer.shape.strokeMode = m; },
          getStops: () => layer.shape.strokeStops,
          setStops: (stops, primaryOverride) => {
            const norm = normalizeGradientStops(stops, layer.shape.strokeColor, layer.shape.strokeColorB);
            layer.shape.strokeStops = norm;
            layer.shape.strokeColor = primaryOverride || norm[0].color;
            layer.shape.strokeColorB = norm[norm.length - 1].color;
          },
          getAngle: () => layer.shape.strokeGradientAngle ?? 45,
          setAngle: (a) => { layer.shape.strokeGradientAngle = a; },
          getPointGrad: () => layer.shape.strokePointGradient,
          setPointGrad: (g) => { layer.shape.strokePointGradient = g; },
          fallbackA: layer.shape.strokeColor || "#16181c",
          fallbackB: layer.shape.strokeColorB || "#eff4cf",
        });
      }
      SelectRow(lSec.Inner, {
        Label: "Blend Mode",
        Options: ["Normal", "Multiply", "Screen", "Overlay", "Add", "Darken"].map((b) => [b, b]),
        Read: () => layer.blend || "Normal",
        Write: (v) => { layer.blend = v; evaluateAndRender(true); },
      });
      SliderRow(lSec.Inner, {
        Label: "Layer Opacity",
        Minimum: 0, Maximum: 1, Step: 0.02,
        Read: () => layer.opacity ?? 1,
        Write: (v) => { layer.opacity = v; evaluateAndRender(true); },
      });
      SliderRow(lSec.Inner, {
        Label: "Center X",
        Minimum: 0, Maximum: 512, Step: 1, Unit: "px",
        Read: () => layer.transform.x,
        Write: (v) => { layer.transform.x = v; evaluateAndRender(true); },
      });
      SliderRow(lSec.Inner, {
        Label: "Center Y",
        Minimum: 0, Maximum: 512, Step: 1, Unit: "px",
        Read: () => layer.transform.y,
        Write: (v) => { layer.transform.y = v; evaluateAndRender(true); },
      });
      SliderRow(lSec.Inner, {
        Label: "Width",
        Minimum: 6, Maximum: 512, Step: 2, Unit: "px",
        Read: () => layer.transform.width,
        Write: (v) => { layer.transform.width = v; evaluateAndRender(true); },
      });
      SliderRow(lSec.Inner, {
        Label: "Height",
        Minimum: 6, Maximum: 512, Step: 2, Unit: "px",
        Read: () => layer.transform.height,
        Write: (v) => { layer.transform.height = v; evaluateAndRender(true); },
      });
      SliderRow(lSec.Inner, {
        Label: "Rotation",
        Minimum: -180, Maximum: 180, Step: 1, Unit: "°",
        Read: () => layer.transform.rotation,
        Write: (v) => { layer.transform.rotation = v; evaluateAndRender(true); },
      });
    }

    // Full PBR Surface Channels (+ Channel dropdown & per-added-channel cards)
    if (layer.kind !== "folder") {
      ensureArtworkLayerSurface(layer);
      const pbrMask = getArtworkLayerPbrMask(layer);
      const activePbrDefs = ARTWORK_PBR_CHANNELS.filter((c) => pbrMask[c.key] === true);
      const sSec = SectionCard(rightInner, {
        Title: "PBR Surface Channels",
        Glyph: "Material",
        Badge: `${activePbrDefs.length} ACTIVE`,
      });
      SelectRow(sSec.Inner, {
        Label: "Surface Finish",
        Options: Object.entries(ARTWORK_FINISHES).map(([k, v]) => [v.label || v.name, k]),
        Read: () => layer.surface.finish,
        Write: (v) => {
          const preset = ARTWORK_FINISHES[v];
          pushUndo();
          layer.surface.finish = v;
          if (preset) {
            layer.surface.roughness = preset.roughness;
            layer.surface.metalness = preset.metalness;
            layer.surface.height = preset.height;
          }
          evaluateAndRender(true);
          rebuildInspector();
        },
      });

      ChannelMaskRow(sSec.Inner, {
        Label: "Active PBR Channels",
        AddLabel: "Channel",
        Channels: ARTWORK_PBR_CHANNEL_ROWS,
        StrictPresence: true,
        Read: () => getArtworkLayerPbrMask(layer),
        Write: (nextMap) => {
          pushUndo();
          setArtworkLayerPbrMask(layer, nextMap);
          evaluateAndRender(true);
          rebuildInspector();
        },
      });

      const cardsWrap = Construct("div", "pbr-channel-cards");
      if (!activePbrDefs.length) {
        cardsWrap.append(
          Construct("div", "property-hint pbr-cards-empty", "No PBR channels active — add channels from the + Channel dropdown above."),
        );
      }
      for (const chDef of activePbrDefs) {
        const card = Construct("div", "pbr-channel-card");
        card.dataset.pbrChannel = chDef.key;

        const curVal = getArtworkChannelValue(layer, chDef.key);
        const valPill = Construct("span", "pbr-card-val tabular", formatArtworkChannelValue(chDef.key, curVal));
        const dot = Construct("i", "pbr-card-dot");
        dot.style.background = chDef.isColor ? String(curVal || chDef.defaultVal || "#e8c27a") : "#9ea2aa";

        const eyeBtn = Construct(
          "button",
          "stack-mini-btn" + (state.viewChannel === chDef.key ? " on" : ""),
          ConstructGlyph("Eye"),
        );
        eyeBtn.type = "button";
        eyeBtn.title = `Solo ${chDef.label} map in viewport`;
        eyeBtn.addEventListener("click", () => {
          state.viewChannel = state.viewChannel === chDef.key ? "vector" : chDef.key;
          headerUI.setView(state.viewChannel);
          syncViewDisplay();
          rebuildInspector();
        });

        const rmBtn = Construct("button", "stack-mini-btn pbr-card-remove", ConstructGlyph("Close"));
        rmBtn.type = "button";
        rmBtn.title = `Remove ${chDef.label} channel`;
        rmBtn.addEventListener("click", () => {
          pushUndo();
          const nextMask = { ...getArtworkLayerPbrMask(layer), [chDef.key]: false };
          setArtworkLayerPbrMask(layer, nextMask);
          evaluateAndRender(true);
          rebuildInspector();
        });

        const head = Construct("div", "pbr-channel-card-head", [
          dot,
          Construct("strong", null, chDef.label),
          Construct("span", "modifier-type-pill", chDef.sub || (chDef.isColor ? "Colour" : "Scalar")),
          valPill,
          eyeBtn,
          rmBtn,
        ]);
        card.append(head);

        if (chDef.isColor) {
          ColorRow(card, {
            Label: `${chDef.label} Value`,
            SlotLabel: `${layer.name} · ${chDef.label}`,
            Read: () => String(getArtworkChannelValue(layer, chDef.key) || chDef.defaultVal || "#ffffff"),
            Write: (v) => {
              const saved = setArtworkChannelValue(layer, chDef.key, v);
              valPill.textContent = formatArtworkChannelValue(chDef.key, saved);
              dot.style.background = String(saved);
              evaluateAndRender(chDef.key === "albedo");
            },
          });
        } else {
          SliderRow(card, {
            Label: `${chDef.label} Value`,
            Minimum: chDef.min ?? 0,
            Maximum: chDef.max ?? 1,
            Step: chDef.step ?? 0.01,
            Read: () => Number(getArtworkChannelValue(layer, chDef.key)),
            Write: (v) => {
              const saved = setArtworkChannelValue(layer, chDef.key, v);
              valPill.textContent = formatArtworkChannelValue(chDef.key, saved);
              evaluateAndRender(false);
            },
          });
        }

        cardsWrap.append(card);
      }
      sSec.Inner.append(cardsWrap);
    }

    // Per-Layer Mask
    if (layer.kind !== "folder") {
      const kSec = SectionCard(rightInner, { Title: "Layer Mask", Glyph: "Focus", Badge: layer.mask?.enabled ? "ON" : "OFF", Open: !!layer.mask?.enabled });
      SwitchRow(kSec.Inner, {
        Label: "Enable Layer Mask",
        Read: () => !!layer.mask?.enabled,
        Write: (v) => {
          if (!layer.mask) layer.mask = { enabled: v, invert: false, mode: "noise", frequency: 4, threshold: 0.45, softness: 0.25, seed: 211 };
          else layer.mask.enabled = v;
          evaluateAndRender(true);
          rebuildInspector();
        },
      });
      if (layer.mask?.enabled) {
        SegmentedRow(kSec.Inner, {
          Label: "Mask Mode",
          Options: [["Noise", "noise"], ["Radial", "radial"], ["Stripe", "stripe"]],
          Read: () => layer.mask.mode,
          Write: (v) => { layer.mask.mode = v; evaluateAndRender(); },
        });
        SliderRow(kSec.Inner, { Label: "Threshold", Minimum: 0, Maximum: 1, Step: 0.02, Read: () => layer.mask.threshold, Write: (v) => { layer.mask.threshold = v; evaluateAndRender(); } });
        SliderRow(kSec.Inner, { Label: "Softness", Minimum: 0.02, Maximum: 0.8, Step: 0.02, Read: () => layer.mask.softness, Write: (v) => { layer.mask.softness = v; evaluateAndRender(); } });
        SliderRow(kSec.Inner, { Label: "Scale / Freq", Minimum: 1, Maximum: 16, Step: 1, Read: () => layer.mask.frequency, Write: (v) => { layer.mask.frequency = v; evaluateAndRender(); } });
        SwitchRow(kSec.Inner, { Label: "Invert Mask", Read: () => !!layer.mask.invert, Write: (v) => { layer.mask.invert = v; evaluateAndRender(); } });
      }
    }

    // Modifier Stack
    const mSec = SectionCard(rightInner, {
      Title: "Non-Destructive Modifiers",
      Glyph: "Modifier",
      Badge: `${(layer.modifiers || []).length} ACTIVE`,
    });

    const addModRow = Construct("div", "chip-strip");
    for (const def of MODIFIER_DEFINITIONS) {
      const ab = Construct("button", "chip-btn", `+ ${def.label}`);
      ab.type = "button";
      ab.addEventListener("click", () => {
        layer.modifiers = layer.modifiers || [];
        layer.modifiers.push(createDefaultModifier(def.type));
        evaluateAndRender(true);
        rebuildInspector();
      });
      addModRow.append(ab);
    }
    mSec.Inner.append(addModRow);

    for (let mi = 0; mi < (layer.modifiers || []).length; mi++) {
      const mod = layer.modifiers[mi];
      const def = MODIFIER_DEFINITIONS.find((d) => d.type === mod.type) || { label: mod.type };
      const card = Construct("div", "modifier-card");
      const head = Construct("div", "modifier-card-head", [
        ConstructGlyph(mod.type === "noise-scale" ? "Noise" : "Modifier"),
        Construct("strong", null, def.label),
      ]);
      const tog = Construct("button", "stack-mini-btn" + (mod.enabled !== false ? " on" : ""), ConstructGlyph(mod.enabled !== false ? "Eye" : "EyeOff"));
      tog.type = "button";
      tog.addEventListener("click", () => { mod.enabled = mod.enabled === false; evaluateAndRender(true); rebuildInspector(); });
      const rm = Construct("button", "stack-mini-btn", ConstructGlyph("Close"));
      rm.type = "button";
      rm.addEventListener("click", () => { layer.modifiers.splice(mi, 1); evaluateAndRender(true); rebuildInspector(); });
      head.append(tog, rm);
      card.append(head);

      if (mod.type === "noise-scale") {
        SelectRow(card, {
          Label: "Noise Algorithm",
          Options: [["FBM (Multi-Octave)", "fbm"], ["Perlin Gradient", "perlin"], ["Worley Cellular", "worley"], ["Ridged Folds", "ridged"], ["Radial Falloff", "radial"]],
          Read: () => mod.noiseType,
          Write: (v) => { mod.noiseType = v; evaluateAndRender(); },
        });
        SelectRow(card, {
          Label: "Modulate Target",
          Options: [["Scale + Color", "scale-color"], ["Scale Only", "scale"], ["Scale + Opacity", "scale-opacity"], ["Scale + Relief", "scale-height"]],
          Read: () => mod.target,
          Write: (v) => { mod.target = v; evaluateAndRender(); },
        });
        SliderRow(card, { Label: "Min Scale", Minimum: 0.05, Maximum: 2.0, Step: 0.02, Read: () => mod.minScale, Write: (v) => { mod.minScale = v; evaluateAndRender(); } });
        SliderRow(card, { Label: "Max Scale", Minimum: 0.2, Maximum: 3.0, Step: 0.02, Read: () => mod.maxScale, Write: (v) => { mod.maxScale = v; evaluateAndRender(); } });
        SliderRow(card, { Label: "Frequency", Minimum: 0.5, Maximum: 14, Step: 0.25, Read: () => mod.frequency, Write: (v) => { mod.frequency = v; evaluateAndRender(); } });
        ColorRow(card, { Label: "Target Color B", Read: () => mod.colorB || "#76b2d8", Write: (v) => { mod.colorB = v; evaluateAndRender(); } });
      } else if (mod.type === "array-grid") {
        SegmentedRow(card, {
          Label: "Grid Layout",
          Options: [["Grid", "grid"], ["Brick", "brick"], ["Hex", "hex"], ["Drop", "half-drop"]],
          Read: () => mod.layout || "grid",
          Write: (v) => { mod.layout = v; evaluateAndRender(); },
        });
        SliderRow(card, { Label: "Columns", Minimum: 1, Maximum: 24, Step: 1, Read: () => mod.columns, Write: (v) => { mod.columns = v; evaluateAndRender(); } });
        SliderRow(card, { Label: "Rows", Minimum: 1, Maximum: 24, Step: 1, Read: () => mod.rows, Write: (v) => { mod.rows = v; evaluateAndRender(); } });
      } else if (mod.type === "array-radial") {
        SliderRow(card, { Label: "Count", Minimum: 3, Maximum: 32, Step: 1, Read: () => mod.count, Write: (v) => { mod.count = v; evaluateAndRender(); } });
        SliderRow(card, { Label: "Radius", Minimum: 20, Maximum: 240, Step: 4, Unit: "px", Read: () => mod.radius, Write: (v) => { mod.radius = v; evaluateAndRender(); } });
        SliderRow(card, { Label: "Rings", Minimum: 1, Maximum: 5, Step: 1, Read: () => mod.rings, Write: (v) => { mod.rings = v; evaluateAndRender(); } });
      } else if (mod.type === "inset-echo") {
        SliderRow(card, { Label: "Echo Copies", Minimum: 1, Maximum: 8, Step: 1, Read: () => mod.copies, Write: (v) => { mod.copies = v; evaluateAndRender(); } });
        SliderRow(card, { Label: "Inset Step", Minimum: 8, Maximum: 90, Step: 2, Unit: "px", Read: () => mod.stepPx, Write: (v) => { mod.stepPx = v; evaluateAndRender(); } });
        ColorRow(card, { Label: "Alt Color", Read: () => mod.colorB || "#181b1f", Write: (v) => { mod.colorB = v; evaluateAndRender(); } });
      } else if (mod.type === "noise-jitter") {
        SliderRow(card, { Label: "Position Jitter", Minimum: 0, Maximum: 80, Step: 1, Unit: "px", Read: () => mod.posJitter, Write: (v) => { mod.posJitter = v; evaluateAndRender(); } });
        SliderRow(card, { Label: "Rotation Jitter", Minimum: 0, Maximum: 180, Step: 2, Unit: "°", Read: () => mod.rotJitter, Write: (v) => { mod.rotJitter = v; evaluateAndRender(); } });
        SliderRow(card, { Label: "Dropout", Minimum: 0, Maximum: 0.85, Step: 0.02, Read: () => mod.dropout, Write: (v) => { mod.dropout = v; evaluateAndRender(); } });
      } else if (mod.type === "warp") {
        SegmentedRow(card, {
          Label: "Warp Mode",
          Options: [["Wave", "wave"], ["Swirl", "swirl"], ["Curl", "curl"], ["Pinch", "pinch"]],
          Read: () => mod.warpType || "wave",
          Write: (v) => { mod.warpType = v; evaluateAndRender(); },
        });
        SliderRow(card, { Label: "Amplitude", Minimum: 0, Maximum: 120, Step: 2, Unit: "px", Read: () => mod.amplitude, Write: (v) => { mod.amplitude = v; evaluateAndRender(); } });
      } else if (mod.type === "edge-wear") {
        SliderRow(card, { Label: "Chip Wear", Minimum: 0, Maximum: 0.9, Step: 0.02, Read: () => mod.chipAmount, Write: (v) => { mod.chipAmount = v; evaluateAndRender(); } });
      }
      mSec.Inner.append(card);
    }

    const bgSec = SectionCard(rightInner, { Title: "Document Ground & Tile", Glyph: "Pattern", Open: false });
    SwitchRow(bgSec.Inner, {
      Label: "Ground Fill Enabled",
      Read: () => !!state.doc.background?.enabled,
      Write: (v) => { state.doc.background.enabled = v; evaluateAndRender(); rebuildInspector(); },
    });
    if (state.doc.background?.enabled) {
      appendColourSourceControls(bgSec.Inner, {
        label: "Ground",
        slot: "ground",
        allowSolid: true,
        getMode: () => state.doc.background.fillMode || "solid",
        setMode: (m) => { state.doc.background.fillMode = m; },
        getStops: () => state.doc.background.fillStops,
        setStops: (stops, primaryOverride) => {
          const norm = normalizeGradientStops(stops, state.doc.background.color, state.doc.background.colorB);
          state.doc.background.fillStops = norm;
          state.doc.background.color = primaryOverride || norm[0].color;
          state.doc.background.colorB = norm[norm.length - 1].color;
        },
        getAngle: () => state.doc.background.gradientAngle ?? 45,
        setAngle: (a) => { state.doc.background.gradientAngle = a; },
        getPointGrad: () => state.doc.background.fillPointGradient,
        setPointGrad: (g) => { state.doc.background.fillPointGradient = g; },
        fallbackA: state.doc.background.color || "#171a1f",
        fallbackB: state.doc.background.colorB || "#0b0b0b",
      });
    }
    if (isPattern) {
      SwitchRow(bgSec.Inner, {
        Label: "Show Ghost Repeat Borders",
        Read: () => state.showTileRepeat,
        Write: (v) => { state.showTileRepeat = v; if (state.viewChannel === "vector") renderVectorSvgCanvas(); },
      });
    }

    CloseScope(inspectorScope);
    Broadcast();
  }

  // ----------------------------------------------------------------- SVG DIRECT VECTOR INTERACTION
  svgEl.addEventListener("pointerdown", (e) => {
    if (e.button === 1 || (e.button === 0 && (state.vectorTool === "pan" || spacePanning))) {
      e.preventDefault();
      const m = getSvgViewMetrics();
      pointerInteraction = {
        kind: "pan",
        startClientX: e.clientX,
        startClientY: e.clientY,
        startPanX: state.panX,
        startPanY: state.panY,
        units: m.vw / Math.max(1, m.w),
      };
      svgEl.style.cursor = "grabbing";
      svgEl.setPointerCapture?.(e.pointerId);
      return;
    }
    if (e.button !== 0 || state.viewChannel !== "vector") return;

    const pt = clientToSvg(e.clientX, e.clientY);

    // Interactive scene-space Point Gradient handle hit-test (drag or select)
    const ptGradHandleEl = e.target.closest?.("[data-ptgrad-id]");
    if (ptGradHandleEl && (state.vectorTool === "gradient" || state.vectorTool === "select")) {
      const tgt = getPointGradientTarget(false);
      if (tgt) {
        const grad = tgt.getGrad();
        const idx = Number(ptGradHandleEl.dataset.ptgradIdx);
        if (Number.isInteger(idx) && grad.points[idx]) {
          state.selectedPointGradIdx = idx;
          pointerInteraction = {
            kind: "ptgrad-move",
            tgt,
            idx,
            undoSnap: structuredClone(state.doc),
          };
          svgEl.setPointerCapture?.(e.pointerId);
          renderVectorSvgCanvas();
          rebuildInspector();
          return;
        }
      }
    }

    // Scene-space Point Gradient click-to-place point directly in scene
    if (state.vectorTool === "gradient") {
      const snap = structuredClone(state.doc);
      const sx = snapCoord(pt.x, e.shiftKey ? 0 : state.snapStep);
      const sy = snapCoord(pt.y, e.shiftKey ? 0 : state.snapStep);
      const tgt = getPointGradientTarget(true);
      if (tgt) {
        const grad = tgt.getGrad();
        const docSize = state.doc.size || ARTWORK_SIZE;
        // Hit-check near existing handle in case user clicked close without SVG hit-test
        const hitIdx = grad.points.findIndex((p) => Math.hypot(p.x - pt.x, p.y - pt.y) <= pt.unitsPerPx * 14);
        if (hitIdx >= 0) {
          state.selectedPointGradIdx = hitIdx;
          pointerInteraction = { kind: "ptgrad-move", tgt, idx: hitIdx, undoSnap: snap };
        } else if (grad.points.length < 12) {
          const palette = ["#eff4cf", "#e8c27a", "#5a7692", "#c87849", "#699c8c", "#d890a8", "#8fb8de"];
          const nextIdx = grad.points.length;
          grad.points.push({
            id: `pg_${Date.now().toString(36)}_${nextIdx}`,
            x: Math.max(-docSize * 0.25, Math.min(docSize * 1.25, sx)),
            y: Math.max(-docSize * 0.25, Math.min(docSize * 1.25, sy)),
            color: palette[nextIdx % palette.length],
            radius: Math.round(docSize * 0.42),
            weight: 1.0,
          });
          state.selectedPointGradIdx = grad.points.length - 1;
          tgt.setGrad(grad);
          pointerInteraction = { kind: "ptgrad-move", tgt, idx: state.selectedPointGradIdx, undoSnap: snap };
          evaluateAndRender(false, { skipPbr: true });
        }
        syncPathSessionHints();
        renderVectorSvgCanvas();
        rebuildInspector();
        svgEl.setPointerCapture?.(e.pointerId);
        return;
      }
    }

    // Interactive Curve / Pen / Point-Edit node & Bézier handle dragging
    const pathHandleEl = e.target.closest?.("[data-path-handle]");
    if (pathHandleEl && activePathSession) {
      const [side, idxStr] = String(pathHandleEl.dataset.pathHandle).split(":");
      const idx = Number(idxStr);
      if (Number.isInteger(idx) && activePathSession.nodes[idx]) {
        activePathSession.activeNodeIndex = idx;
        pointerInteraction = { kind: "path-handle", side, idx };
        svgEl.setPointerCapture?.(e.pointerId);
        renderVectorSvgCanvas();
        return;
      }
    }
    const pathNodeEl = e.target.closest?.("[data-path-node]");
    if (pathNodeEl && activePathSession) {
      const idx = Number(pathNodeEl.dataset.pathNode);
      if (idx === 0 && !activePathSession.editingLayerId && activePathSession.nodes.length >= 3 && (state.vectorTool === "curve" || state.vectorTool === "pen")) {
        commitActivePathSession({ closeLoop: true });
        return;
      }
      if (Number.isInteger(idx) && activePathSession.nodes[idx]) {
        activePathSession.activeNodeIndex = idx;
        pointerInteraction = { kind: "path-node", idx };
        svgEl.setPointerCapture?.(e.pointerId);
        renderVectorSvgCanvas();
        return;
      }
    }

    if (state.vectorTool === "curve" || state.vectorTool === "pen") {
      let sx = snapCoord(pt.x, e.shiftKey ? 0 : state.snapStep);
      let sy = snapCoord(pt.y, e.shiftKey ? 0 : state.snapStep);
      if (!activePathSession) {
        activePathSession = {
          nodes: [],
          closed: false,
          tension: 0.5,
          tool: state.vectorTool,
          hoverPt: null,
          nearStart: false,
          editingLayerId: null,
          activeNodeIndex: -1,
        };
      }
      const prev = activePathSession.nodes[activePathSession.nodes.length - 1];
      if (e.shiftKey && prev) {
        const dx = sx - prev.x, dy = sy - prev.y;
        const dist = Math.hypot(dx, dy);
        if (dist > 1) {
          const ang = Math.round(Math.atan2(dy, dx) / (Math.PI / 4)) * (Math.PI / 4);
          sx = prev.x + Math.cos(ang) * dist;
          sy = prev.y + Math.sin(ang) * dist;
        }
      }
      const first = activePathSession.nodes[0];
      if (first && activePathSession.nodes.length >= 3 && Math.hypot(sx - first.x, sy - first.y) <= pt.unitsPerPx * 14) {
        commitActivePathSession({ closeLoop: true });
        return;
      }
      activePathSession.nodes.push({
        x: sx,
        y: sy,
        mode: state.vectorTool === "curve" ? "auto" : "corner",
        in: null,
        out: null,
      });
      activePathSession.activeNodeIndex = activePathSession.nodes.length - 1;
      if (state.vectorTool === "pen") {
        pointerInteraction = { kind: "pen-pull", idx: activePathSession.nodes.length - 1 };
        svgEl.setPointerCapture?.(e.pointerId);
      }
      syncPathSessionHints();
      renderVectorSvgCanvas();
      return;
    }

    if (state.vectorTool === "node") {
      const hitNodeGrp = e.target.closest?.("[data-layer-id]");
      if (hitNodeGrp) {
        const target = state.doc.layers.find((l) => l.id === hitNodeGrp.dataset.layerId);
        if (target?.shape?.motifType === "svg-path" && target.shape.pathNodes?.length) {
          state.doc.selectedLayerId = target.id;
          startEditLayerCurveSession(target);
          renderLeftColumn();
          rebuildInspector();
          return;
        }
      }
    }

    const handleEl = e.target.closest?.("[data-handle]");
    if (handleEl) {
      const sel = getSelectedLayer();
      if (!sel || sel.locked) return;
      const handleId = handleEl.dataset.handle;
      const snap = structuredClone(state.doc);
      if (handleId === "rotate") {
        pointerInteraction = {
          kind: "rotate",
          layer: sel,
          undoSnap: snap,
          startAngle: Math.atan2(pt.y - sel.transform.y, pt.x - sel.transform.x) * (180 / Math.PI),
          origRot: sel.transform.rotation || 0,
        };
      } else {
        pointerInteraction = {
          kind: "resize",
          layer: sel,
          undoSnap: snap,
          handle: handleId,
          startX: pt.x,
          startY: pt.y,
          orig: { ...sel.transform },
        };
      }
      svgEl.setPointerCapture?.(e.pointerId);
      return;
    }

    // Shape drawing tool
    if (state.vectorTool !== "select") {
      const sx = snapCoord(pt.x, state.snapStep);
      const sy = snapCoord(pt.y, state.snapStep);
      pointerInteraction = {
        kind: "draw",
        tool: state.vectorTool,
        startX: sx,
        startY: sy,
        draftRect: { x: sx, y: sy, w: 1, h: 1 },
      };
      svgEl.setPointerCapture?.(e.pointerId);
      renderVectorSvgCanvas();
      return;
    }

    // Select & move shape layer directly on SVG canvas
    const hitGroup = e.target.closest?.("[data-layer-id]");
    if (hitGroup) {
      const targetLayer = state.doc.layers.find((l) => l.id === hitGroup.dataset.layerId);
      if (targetLayer) {
        if (state.doc.selectedLayerId !== targetLayer.id) {
          state.doc.selectedLayerId = targetLayer.id;
          renderLeftColumn();
          rebuildInspector();
          renderVectorSvgCanvas();
        }
        if (!targetLayer.locked) {
          pointerInteraction = {
            kind: "move",
            layer: targetLayer,
            undoSnap: structuredClone(state.doc),
            startX: pt.x,
            startY: pt.y,
            origX: targetLayer.transform.x,
            origY: targetLayer.transform.y,
          };
          svgEl.setPointerCapture?.(e.pointerId);
        }
      }
    }
  });

  svgEl.addEventListener("pointermove", (e) => {
    if (!pointerInteraction && activePathSession && state.viewChannel === "vector" && (state.vectorTool === "curve" || state.vectorTool === "pen")) {
      const pt = clientToSvg(e.clientX, e.clientY);
      let hx = snapCoord(pt.x, e.shiftKey ? 0 : state.snapStep);
      let hy = snapCoord(pt.y, e.shiftKey ? 0 : state.snapStep);
      const prev = activePathSession.nodes[activePathSession.nodes.length - 1];
      if (e.shiftKey && prev) {
        const dx = hx - prev.x, dy = hy - prev.y;
        const dist = Math.hypot(dx, dy);
        if (dist > 1) {
          const ang = Math.round(Math.atan2(dy, dx) / (Math.PI / 4)) * (Math.PI / 4);
          hx = prev.x + Math.cos(ang) * dist;
          hy = prev.y + Math.sin(ang) * dist;
        }
      }
      const first = activePathSession.nodes[0];
      activePathSession.nearStart = !!(first && activePathSession.nodes.length >= 3 && Math.hypot(hx - first.x, hy - first.y) <= pt.unitsPerPx * 14);
      activePathSession.hoverPt = { x: hx, y: hy };
      renderVectorSvgCanvas();
      return;
    }
    if (!pointerInteraction) return;
    if (pointerInteraction.kind === "pan") {
      state.panX = pointerInteraction.startPanX - (e.clientX - pointerInteraction.startClientX) * pointerInteraction.units;
      state.panY = pointerInteraction.startPanY - (e.clientY - pointerInteraction.startClientY) * pointerInteraction.units;
      renderVectorSvgCanvas();
      return;
    }

    const pt = clientToSvg(e.clientX, e.clientY);
    if (pointerInteraction.kind === "ptgrad-move" && pointerInteraction.tgt) {
      const grad = pointerInteraction.tgt.getGrad();
      const gp = grad.points[pointerInteraction.idx];
      if (gp) {
        const docSize = state.doc.size || ARTWORK_SIZE;
        gp.x = Math.max(-docSize * 0.25, Math.min(docSize * 1.25, snapCoord(pt.x, e.shiftKey ? 0 : state.snapStep)));
        gp.y = Math.max(-docSize * 0.25, Math.min(docSize * 1.25, snapCoord(pt.y, e.shiftKey ? 0 : state.snapStep)));
        pointerInteraction.tgt.setGrad(grad);
        selCountEl.textContent = `Point ${pointerInteraction.idx + 1} (${Math.round(gp.x)}, ${Math.round(gp.y)})`;
        evaluateAndRender(false, { skipPbr: true });
      }
      return;
    }
    if (pointerInteraction.kind === "pen-pull" && activePathSession) {
      const node = activePathSession.nodes[pointerInteraction.idx];
      if (node) {
        const dx = pt.x - node.x, dy = pt.y - node.y;
        if (Math.hypot(dx, dy) > 4) {
          node.mode = "symmetric";
          node.out = { x: dx, y: dy };
          node.in = { x: -dx, y: -dy };
          renderVectorSvgCanvas();
        }
      }
      return;
    }
    if (pointerInteraction.kind === "path-node" && activePathSession) {
      const node = activePathSession.nodes[pointerInteraction.idx];
      if (node) {
        node.x = snapCoord(pt.x, e.shiftKey ? 0 : state.snapStep);
        node.y = snapCoord(pt.y, e.shiftKey ? 0 : state.snapStep);
        renderVectorSvgCanvas();
      }
      return;
    }
    if (pointerInteraction.kind === "path-handle" && activePathSession) {
      const node = activePathSession.nodes[pointerInteraction.idx];
      if (node) {
        const dx = pt.x - node.x, dy = pt.y - node.y;
        node.mode = "smooth";
        if (pointerInteraction.side === "out") {
          node.out = { x: dx, y: dy };
          node.in = { x: -dx, y: -dy };
        } else {
          node.in = { x: dx, y: dy };
          node.out = { x: -dx, y: -dy };
        }
        renderVectorSvgCanvas();
      }
      return;
    }
    const l = pointerInteraction.layer;

    if (pointerInteraction.kind === "move" && l) {
      const dx = pt.x - pointerInteraction.startX;
      const dy = pt.y - pointerInteraction.startY;
      l.transform.x = snapCoord(pointerInteraction.origX + dx, e.shiftKey ? 0 : state.snapStep);
      l.transform.y = snapCoord(pointerInteraction.origY + dy, e.shiftKey ? 0 : state.snapStep);
      selCountEl.textContent = `${l.name} (${Math.round(l.transform.x)}, ${Math.round(l.transform.y)})`;
      evaluateAndRender(false, { skipPbr: true });
      Broadcast();
    } else if (pointerInteraction.kind === "resize" && l) {
      const rad = -((pointerInteraction.orig.rotation || 0) * Math.PI) / 180;
      const dxWorld = pt.x - pointerInteraction.startX;
      const dyWorld = pt.y - pointerInteraction.startY;
      const dx = dxWorld * Math.cos(rad) - dyWorld * Math.sin(rad);
      const dy = dxWorld * Math.sin(rad) + dyWorld * Math.cos(rad);
      const h = pointerInteraction.handle;
      const minSz = 10;
      let nextW = pointerInteraction.orig.width;
      let nextH = pointerInteraction.orig.height;
      let shiftX = 0, shiftY = 0;

      if (h.includes("e")) {
        nextW = Math.max(minSz, snapCoord(pointerInteraction.orig.width + dx, state.snapStep));
        shiftX = (nextW - pointerInteraction.orig.width) * 0.5;
      } else if (h.includes("w")) {
        nextW = Math.max(minSz, snapCoord(pointerInteraction.orig.width - dx, state.snapStep));
        shiftX = -(nextW - pointerInteraction.orig.width) * 0.5;
      }
      if (h.includes("s")) {
        nextH = Math.max(minSz, snapCoord(pointerInteraction.orig.height + dy, state.snapStep));
        shiftY = (nextH - pointerInteraction.orig.height) * 0.5;
      } else if (h.includes("n")) {
        nextH = Math.max(minSz, snapCoord(pointerInteraction.orig.height - dy, state.snapStep));
        shiftY = -(nextH - pointerInteraction.orig.height) * 0.5;
      }
      if (e.shiftKey) {
        const uniform = Math.max(nextW, nextH);
        nextW = nextH = uniform;
      }
      const rotBack = ((pointerInteraction.orig.rotation || 0) * Math.PI) / 180;
      l.transform.width = nextW;
      l.transform.height = nextH;
      l.transform.x = pointerInteraction.orig.x + shiftX * Math.cos(rotBack) - shiftY * Math.sin(rotBack);
      l.transform.y = pointerInteraction.orig.y + shiftX * Math.sin(rotBack) + shiftY * Math.cos(rotBack);
      selCountEl.textContent = `${l.name} ${Math.round(nextW)}×${Math.round(nextH)}`;
      evaluateAndRender(false, { skipPbr: true });
      Broadcast();
    } else if (pointerInteraction.kind === "rotate" && l) {
      const curAngle = Math.atan2(pt.y - l.transform.y, pt.x - l.transform.x) * (180 / Math.PI);
      let nextRot = pointerInteraction.origRot + (curAngle - pointerInteraction.startAngle);
      if (e.shiftKey) nextRot = Math.round(nextRot / 15) * 15;
      while (nextRot > 180) nextRot -= 360;
      while (nextRot < -180) nextRot += 360;
      l.transform.rotation = Math.round(nextRot);
      selCountEl.textContent = `${l.name} ${l.transform.rotation}°`;
      evaluateAndRender(false, { skipPbr: true });
      Broadcast();
    } else if (pointerInteraction.kind === "draw") {
      const ex = snapCoord(pt.x, state.snapStep);
      const ey = snapCoord(pt.y, state.snapStep);
      const x = Math.min(pointerInteraction.startX, ex);
      const y = Math.min(pointerInteraction.startY, ey);
      const w = Math.max(8, Math.abs(ex - pointerInteraction.startX));
      const h = Math.max(8, Math.abs(ey - pointerInteraction.startY));
      pointerInteraction.draftRect = { x, y, w, h };
      renderVectorSvgCanvas();
    }
  });

  const endSvgPointer = (e) => {
    if (!pointerInteraction) return;
    const act = pointerInteraction;
    pointerInteraction = null;
    svgEl.style.cursor = state.vectorTool === "pan" ? "grab" : state.vectorTool === "select" ? "default" : "crosshair";

    if (act.kind === "ptgrad-move") {
      if (act.undoSnap) pushUndo(act.undoSnap);
      syncPathSessionHints();
      evaluateAndRender(true);
      rebuildInspector();
      return;
    }

    if (act.kind === "pen-pull" || act.kind === "path-node" || act.kind === "path-handle") {
      if (activePathSession?.editingLayerId && (act.kind === "path-node" || act.kind === "path-handle")) {
        const l = state.doc.layers.find((x) => x.id === activePathSession.editingLayerId);
        const curveData = nodesToNormalizedCurveData(activePathSession.nodes, activePathSession.closed, activePathSession.tension);
        if (l && curveData) {
          pushUndo();
          l.transform = { ...l.transform, ...curveData.transform, rotation: 0 };
          l.shape.pathNodes = curveData.pathNodes;
          l.shape.svgPath = curveData.svgPath;
          evaluateAndRender(true);
        }
      }
      return;
    }

    if (act.kind === "draw") {
      pushUndo();
      const dr = act.draftRect;
      const w = Math.max(48, dr.w < 12 ? 96 : dr.w);
      const h = Math.max(48, dr.h < 12 ? (act.tool === "stripe" ? 36 : 96) : dr.h);
      const cx = dr.w < 12 ? act.startX : dr.x + w * 0.5;
      const cy = dr.h < 12 ? act.startY : dr.y + h * 0.5;
      const isText = act.tool === "text";
      const created = createArtworkLayer(isText ? "text" : "motif", {
        name: isText ? "Vector Label" : `${act.tool.charAt(0).toUpperCase() + act.tool.slice(1)} Shape`,
        transform: { x: Math.round(cx), y: Math.round(cy), width: Math.round(w), height: Math.round(h), rotation: 0 },
        shape: { motifType: isText ? "square" : act.tool, color: isPattern ? "#eff4cf" : "#e8c27a" },
      });
      state.doc.layers.push(created);
      state.doc.selectedLayerId = created.id;
      if (!e.shiftKey) selectVectorTool("select");
      evaluateAndRender(true);
      rebuildInspector();
      Toast(`Created ${created.name} at (${Math.round(cx)}, ${Math.round(cy)})`);
      return;
    }

    if (act.kind === "move" || act.kind === "resize" || act.kind === "rotate") {
      if (act.undoSnap) pushUndo(act.undoSnap);
      evaluateAndRender(true);
    }
  };
  svgEl.addEventListener("pointerup", endSvgPointer);
  svgEl.addEventListener("pointercancel", endSvgPointer);
  window.addEventListener("pointerup", endSvgPointer);
  svgEl.addEventListener("dblclick", (e) => {
    if (activePathSession) {
      e.preventDefault();
      commitActivePathSession({ closeLoop: activePathSession.closed });
    }
  });

  svgEl.addEventListener("wheel", (e) => {
    e.preventDefault();
    const factor = e.deltaY < 0 ? 1.12 : 1 / 1.12;
    state.zoom = Math.max(0.25, Math.min(8, state.zoom * factor));
    renderVectorSvgCanvas();
  }, { passive: false });

  btnZoomOut.addEventListener("click", () => { state.zoom = Math.max(0.25, state.zoom / 1.25); renderVectorSvgCanvas(); });
  btnZoomIn.addEventListener("click", () => { state.zoom = Math.min(8, state.zoom * 1.25); renderVectorSvgCanvas(); });
  btnZoomFit.addEventListener("click", () => { state.zoom = 1; state.panX = 0; state.panY = 0; renderVectorSvgCanvas(); });

  // ----------------------------------------------------------------- ASSET APPLICATION & DRAG-DROP ONTO CANVAS
  function applyAsset(payload, dropInfo = {}) {
    if (!payload) return false;
    let svgPt = null;
    if (Number.isFinite(dropInfo.clientX) && Number.isFinite(dropInfo.clientY)) {
      svgPt = clientToSvg(dropInfo.clientX, dropInfo.clientY);
      svgPt.x = Math.max(0, Math.min(ARTWORK_SIZE, snapCoord(svgPt.x, state.snapStep)));
      svgPt.y = Math.max(0, Math.min(ARTWORK_SIZE, snapCoord(svgPt.y, state.snapStep)));
    }

    if (payload.assetKind === "pattern" || (payload.assetKind === "stamp" && !isPattern && !dropInfo.merge)) {
      pushUndo();
      const entry = payload.raw;
      state.activeStarterId = entry.id;
      state.doc = loadStarterAsArtworkDocument(entry);
      if (state.doc.layers?.length) state.doc.selectedLayerId = state.doc.layers[state.doc.layers.length - 1].id;
      evaluateAndRender(true);
      rebuildInspector();
      Toast(`Loaded "${entry.name}" onto ${isPattern ? "Pattern" : "Stamp"} SVG Canvas`);
      return true;
    }

    if (payload.assetKind === "stamp") {
      // Drop stamp into Pattern (or Shift-drop into Stamp) as placed layer(s) at drop coordinates!
      pushUndo();
      const stampDoc = loadStarterAsArtworkDocument(payload.raw);
      const targetX = svgPt ? svgPt.x : 256;
      const targetY = svgPt ? svgPt.y : 256;
      for (const srcLyr of stampDoc.layers || []) {
        const cloned = structuredClone(srcLyr);
        cloned.id = `lyr_${Math.random().toString(36).slice(2, 9)}`;
        cloned.transform.x = targetX + (cloned.transform.x - 256) * 0.5;
        cloned.transform.y = targetY + (cloned.transform.y - 256) * 0.5;
        cloned.transform.width *= 0.5;
        cloned.transform.height *= 0.5;
        state.doc.layers.push(cloned);
        state.doc.selectedLayerId = cloned.id;
      }
      evaluateAndRender(true);
      rebuildInspector();
      Toast(`Placed stamp "${payload.name}" at (${Math.round(targetX)}, ${Math.round(targetY)})`);
      return true;
    }

    if (payload.assetKind === "material") {
      const mat = payload.raw;
      const sel = getSelectedLayer();
      if (sel && sel.kind !== "folder") {
        pushUndo();
        ensureArtworkLayerSurface(sel);
        if (mat.color) setArtworkChannelValue(sel, "albedo", mat.color);
        if (mat.roughness != null) setArtworkChannelValue(sel, "roughness", mat.roughness);
        if (mat.metalness != null) setArtworkChannelValue(sel, "metalness", mat.metalness);
        const mask = getArtworkLayerPbrMask(sel);
        const applyExtMat = (key, val) => {
          mask[key] = true;
          setArtworkChannelValue(sel, key, val);
        };
        if ((mat.coat ?? 0) > 0.01) {
          applyExtMat("clearcoat", mat.coat);
          if (mat.coatRoughness != null) applyExtMat("coatRoughness", mat.coatRoughness);
        }
        if ((mat.sheen ?? 0) > 0.01) {
          applyExtMat("sheen", mat.sheen);
          if (mat.sheenRoughness != null) applyExtMat("sheenRoughness", mat.sheenRoughness);
          if (mat.sheenColor) applyExtMat("sheenColor", mat.sheenColor);
        }
        if ((mat.iridescence ?? 0) > 0.01) {
          applyExtMat("iridescence", mat.iridescence);
          if (mat.filmThickness != null) applyExtMat("iridescenceThickness", Math.min(1, Number(mat.filmThickness) / 1200));
        }
        if ((mat.anisotropy ?? 0) > 0.01) applyExtMat("anisotropy", mat.anisotropy);
        if ((mat.translucency ?? 0) > 0.01) applyExtMat("transmission", mat.translucency);
        if ((mat.emissionStrength ?? 0) > 0.01 && mat.emissionColor) {
          applyExtMat("emissive", mat.emissionColor);
          applyExtMat("emissiveIntensity", mat.emissionStrength);
        }
        setArtworkLayerPbrMask(sel, mask);
        evaluateAndRender(true);
        rebuildInspector();
        Toast(`Applied "${mat.name}" PBR look to layer "${sel.name}"`);
        return true;
      }
    }
    return false;
  }

  seViewport.addEventListener("dragover", (e) => {
    e.preventDefault();
    if (e.dataTransfer) e.dataTransfer.dropEffect = "copy";
    seViewport.classList.add("canvas-drop-active");
  });
  seViewport.addEventListener("dragleave", () => seViewport.classList.remove("canvas-drop-active"));
  seViewport.addEventListener("drop", (e) => {
    e.preventDefault();
    seViewport.classList.remove("canvas-drop-active");
    let payload = null;
    try {
      const rawJson = e.dataTransfer?.getData("application/x-frontier-asset") || e.dataTransfer?.getData("text/plain");
      if (rawJson) {
        const parsed = JSON.parse(rawJson);
        payload = parsed.raw ? parsed : appBridge.resolveAssetPayload?.(parsed.assetKind, parsed.id);
      }
    } catch {}
    if (!payload) payload = appBridge.activeDragAsset;
    if (payload) applyAsset(payload, { source: "drop", clientX: e.clientX, clientY: e.clientY, merge: e.shiftKey });
  });

  // ----------------------------------------------------------------- TOOLBAR & POPUP ACTIONS
  loadJsonInp.addEventListener("change", async () => {
    const file = loadJsonInp.files?.[0];
    if (!file) return;
    try {
      const parsed = JSON.parse(await file.text());
      const docData = parsed?.document || parsed;
      if (Array.isArray(docData?.layers) && docData.layers.length) {
        pushUndo();
        state.doc = docData;
        const loadedRes = Math.round(Number(parsed.resolution));
        if (Number.isFinite(loadedRes) && loadedRes >= 64 && loadedRes <= 4096) {
          state.resolution = loadedRes;
          headerUI.setResolution(state.resolution);
          headerUI.setSaveItemMeta("png", `${state.resolution}px`);
          headerUI.setSaveItemMeta("pbr", `${Math.min(2048, state.resolution)}px`);
        }
        evaluateAndRender(true);
        rebuildInspector();
        headerUI.closeSaveMenu();
        Toast(`Opened "${state.doc.name || file.name}"`);
      } else {
        Toast("Invalid project JSON");
      }
    } catch (err) {
      Toast(`Open error: ${err.message}`);
    }
    loadJsonInp.value = "";
  });

  btnGrid.addEventListener("click", () => {
    state.showGrid = !state.showGrid;
    btnGrid.classList.toggle("active", state.showGrid);
    if (state.viewChannel === "vector") renderVectorSvgCanvas();
  });
  btnAxes.addEventListener("click", () => {
    state.showAxes = !state.showAxes;
    btnAxes.classList.toggle("active", state.showAxes);
    if (state.viewChannel === "vector") renderVectorSvgCanvas();
  });
  snapSelect.addEventListener("change", () => {
    state.snapStep = Number(snapSelect.value) || 0;
    if (state.viewChannel === "vector") renderVectorSvgCanvas();
  });

  btnSendToPaint.addEventListener("click", () => {
    const sz = Math.min(1024, state.resolution);
    const ev = evaluateArtworkDocument(state.doc, sz);
    const payload = { ...ev, name: state.doc.name };
    if (isPattern) appBridge.sendPatternToPaint(payload);
    else appBridge.sendStampToPaint(payload);
  });
  if (btnSendToMaterial) {
    btnSendToMaterial.addEventListener("click", () => {
      const sz = Math.min(1024, state.resolution);
      const ev = evaluateArtworkDocument(state.doc, sz);
      appBridge.sendPatternToMaterial?.({
        ...ev,
        name: state.doc.name,
        repeats: state.doc.tiling?.repeats || 3,
      });
    });
  }

  // Right-click (<5px) / Q / Tab quick modifier & tool popup + shortcuts
  let activeArtPopup = null;
  function closeArtPopup() {
    if (activeArtPopup) {
      activeArtPopup.remove?.();
      activeArtPopup = null;
    }
  }
  function openArtPopup(clientX = null, clientY = null) {
    if (activeArtPopup) { closeArtPopup(); return; }
    const box = stage.getBoundingClientRect();
    const px = clientX ?? (box.left + box.width * 0.5 - 165);
    const py = clientY ?? (box.top + 70);
    const pop = Construct("div", "tool-popup-menu");
    pop.style.left = `${Math.max(12, Math.min(px, (window.innerWidth || 1280) - 356))}px`;
    pop.style.top = `${Math.max(12, Math.min(py, (window.innerHeight || 800) - 360))}px`;

    const closeBtn = Construct("button", "stack-mini-btn", ConstructGlyph("Close"));
    closeBtn.type = "button";
    closeBtn.addEventListener("click", closeArtPopup);
    const selLayer = getSelectedLayer();
    pop.append(
      Construct("div", "tool-popup-head", [
        ConstructGlyph(isPattern ? "Pattern" : "Stamp"),
        Construct("strong", null, selLayer ? `${selLayer.name} Quick Menu` : "Vector Quick Menu"),
        Construct("small", null, "Q / Tab / Esc"),
        closeBtn,
      ]),
    );

    const chanRow = Construct("div", "chip-strip");
    for (const [label, val] of [["Vector SVG", "vector"], ["2×2 Tile", "tile2x2"], ["Albedo", "albedo"], ["Rough", "roughness"], ["Normal", "normal"]]) {
      const cb = Construct("button", "chip-btn" + (state.viewChannel === val ? " active" : ""), label);
      cb.type = "button";
      cb.addEventListener("click", () => {
        state.viewChannel = val;
        headerUI.setView(val);
        syncViewDisplay();
        closeArtPopup();
      });
      chanRow.append(cb);
    }
    pop.append(Construct("div", "property-label", "Canvas View"), chanRow);

    const modRow = Construct("div", "chip-strip");
    for (const [label, kind] of [["+ Noise Scale", "noise-scale"], ["+ Array Grid", "array-grid"], ["+ Radial", "array-radial"], ["+ Warp", "warp"]]) {
      const mb = Construct("button", "chip-btn", label);
      mb.type = "button";
      mb.addEventListener("click", () => {
        const l = getSelectedLayer();
        if (l) {
          pushUndo();
          l.modifiers = l.modifiers || [];
          l.modifiers.push(createDefaultModifier(kind));
          evaluateAndRender(true);
          rebuildInspector();
        }
        closeArtPopup();
      });
      modRow.append(mb);
    }
    pop.append(Construct("div", "property-label", "Quick Add Modifier"), modRow);

    const actBtn = Construct("button", "action-button accent", [
      ConstructGlyph("Brush"),
      Construct("span", null, isPattern ? "Send Pattern to Paint" : "Send Stamp to Paint"),
    ]);
    actBtn.type = "button";
    actBtn.style.width = "100%";
    actBtn.addEventListener("click", () => { closeArtPopup(); btnSendToPaint.click(); });
    pop.append(actBtn);

    document.body.append(pop);
    activeArtPopup = pop;
  }

  let artRDown = null;
  stage.addEventListener("pointerdown", (e) => {
    if (e.button === 2) artRDown = { x: e.clientX, y: e.clientY };
    else if (activeArtPopup && !activeArtPopup.contains(e.target)) closeArtPopup();
  });
  stage.addEventListener("contextmenu", (e) => {
    e.preventDefault();
    if (!artRDown || Math.hypot(e.clientX - artRDown.x, e.clientY - artRDown.y) < 5) {
      openArtPopup(e.clientX, e.clientY);
    }
    artRDown = null;
  });

  const onArtKeyDown = (e) => {
    if (["INPUT", "TEXTAREA", "SELECT"].includes(document.activeElement?.tagName)) return;
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s") {
      e.preventDefault();
      performSaveProject();
      return;
    }
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "z") {
      e.preventDefault();
      if (e.shiftKey) performRedo();
      else performUndo();
      return;
    }
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "y") {
      e.preventDefault();
      performRedo();
      return;
    }
    if (e.code === "Space") { spacePanning = true; return; }
    if (e.key === "Enter" && activePathSession) {
      e.preventDefault();
      commitActivePathSession();
      return;
    }
    if ((e.key === "Backspace" || e.key === "Delete") && state.vectorTool === "gradient" && !activePathSession) {
      const tgt = getPointGradientTarget(false);
      if (tgt) {
        const grad = tgt.getGrad();
        if (grad.points.length > 1) {
          e.preventDefault();
          pushUndo();
          grad.points.splice(state.selectedPointGradIdx, 1);
          state.selectedPointGradIdx = Math.max(0, state.selectedPointGradIdx - 1);
          tgt.setGrad(grad);
          syncPathSessionHints();
          evaluateAndRender(true);
          rebuildInspector();
          return;
        }
      }
    }
    if ((e.key === "Backspace" || e.key === "Delete") && activePathSession && !activePathSession.editingLayerId) {
      e.preventDefault();
      if (activePathSession.nodes.length > 1) {
        activePathSession.nodes.pop();
        syncPathSessionHints();
        renderVectorSvgCanvas();
      } else {
        activePathSession = null;
        syncPathSessionHints();
        renderVectorSvgCanvas();
      }
      return;
    }
    if (e.key === "Escape") {
      if (activePathSession) {
        activePathSession = null;
        syncPathSessionHints();
        renderVectorSvgCanvas();
        return;
      }
      closeArtPopup(); headerUI.closeSaveMenu(); return;
    }
    if (e.key.toLowerCase() === "q" || e.key === "Tab") { e.preventDefault(); openArtPopup(); return; }
    if (e.altKey && (e.key === "ArrowUp" || e.key === "ArrowDown")) {
      e.preventDefault();
      const sel = getSelectedLayer();
      if (sel) moveLayerByOffset(sel.id, e.key === "ArrowUp" ? +1 : -1);
      return;
    }
    const toolMatch = VECTOR_TOOLS.find((t) => t.key.toLowerCase() === e.key.toLowerCase());
    if (toolMatch && !e.ctrlKey && !e.metaKey) selectVectorTool(toolMatch.id);
  };
  const onArtKeyUp = (e) => {
    if (e.code === "Space") spacePanning = false;
  };
  window.addEventListener("keydown", onArtKeyDown);
  window.addEventListener("keyup", onArtKeyUp);

  const ro = new ResizeObserver(() => {
    if (state.viewChannel === "vector") renderVectorSvgCanvas();
  });
  ro.observe(seViewport);

  evaluateAndRender(true);
  rebuildInspector();
  syncViewDisplay();

  return {
    name: isPattern ? "Pattern" : "Stamp",
    applyAsset,
    getSnapshot: () => {
      const sel = getSelectedLayer();
      return {
        mode,
        name: state.doc.name,
        resolution: state.resolution,
        layers: state.doc.layers.length,
        viewChannel: state.viewChannel,
        vectorTool: state.vectorTool,
        showGrid: state.showGrid,
        snapStep: state.snapStep,
        undoDepth: undoStack.length,
        redoDepth: redoStack.length,
        selectedLayer: sel ? { id: sel.id, name: sel.name, ...sel.transform } : null,
        stats: state.lastStats,
      };
    },
    dispose() {
      disposed = true;
      ro.disconnect();
      headerUI.dispose();
      layerStackUI.dispose?.();
      closeArtPopup();
      window.removeEventListener("keydown", onArtKeyDown);
      window.removeEventListener("keyup", onArtKeyUp);
      window.removeEventListener("pointerup", endSvgPointer);
      if (inspectorScope) ReleaseScope(inspectorScope);
      root.innerHTML = "";
    },
  };
}
