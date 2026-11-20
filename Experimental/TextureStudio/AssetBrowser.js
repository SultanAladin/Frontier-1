/*==========================================================================================================================================
                                                            ASSETBROWSER.JS
==========================================================================================================================================*/
/* Project Zero Notch ContentBrowser for Frontier Texture Studio (matched 1:1 to
   Experimental/ProjectZeroEditor arena/429d7dc7-frontier):
   - Bottom DrawerPanel shell with centered slanted SVG trapezoid notch ("ContentBrowser")
   - 3-stop drag/snap gesture (closed / half 0.46 split / full 1.0) + horizontal notch repositioning + page grip
   - Header: CONTENT / LIBRARY · ContentBrowser {N} items · Auto/Pinned sync · + Material · + Colour · Import · Split/Full · Close
   - Tools: Origin tabs (All | Project | Imported | Engine) · Search pill · View switch (Grid | List, or Grid | Vertical | Horizontal for Colours)
   - Dedicated filter bar: ColourCodex hue filters + sort on Colours, Family & Attribute filters on Materials/Patterns/Stamps/Meshes
   - 3-Pane Body: Left category rail (All, Materials, Patterns, Stamps, Meshes, Colours, Images) | Center catalogue | Right CONTENT DETAILS aside
   - Full ColourCodex TOML import/export + drag-to-slot + double-click apply, baked transparent 3D PBR Material spheres, vector SVG artwork previews */

import * as THREE from "three";
import { OBJLoader } from "./Vendor/addons/OBJLoader.js";
import { STLLoader } from "./Vendor/addons/STLLoader.js";
import { Construct, Toast } from "./StudioControls.js";
import { materials } from "./Engine/Materials/materials.js";
import { getBakedMaterialIcon } from "./MaterialIconBaker.js";
import { getBakedGeometryIcon } from "./GeometryIconBaker.js";
import { BAKE_STUDY_SHAPES } from "./Engine/BakingEngine.js";
import {
  listPatternStarterCatalog,
  listStampStarterCatalog,
  getArtworkPreviewSvg,
} from "./Engine/Patterns/StarterCatalogBridge.js";
import {
  ApplyToActiveColourSlot,
  BuiltInColourCodex,
  ChooseCardInk,
  ColourFilterCategories,
  ColourSortModes,
  GetActiveColourSlot,
  MatchesColourFilter,
  NormalizeHex,
  PackColourDragData,
  ParseColourCodexToml,
  ResolveColourEntry,
  SerializeColourCodexToml,
  SortColourAssets,
  SplitCardHeading,
} from "./ColourCodex.js";

const STORAGE_KEY = "Frontier.TextureStudio.ContentBrowser.v1";
const IDB_NAME = "Frontier.TextureStudio.Assets";
const SVG_NS = "http://www.w3.org/2000/svg";

const CATEGORY_ORDER = [
  "All",
  "Materials",
  "Patterns",
  "Stamps",
  "Meshes",
  "Colours",
  "Images",
];

const WORKSPACE_TO_KIND = {
  material: "Materials",
  pattern: "Patterns",
  stamp: "Stamps",
  paint: "Stamps",
  bake: "Meshes",
};

const CATEGORY_ALIASES = {
  all: "All",
  materials: "Materials",
  material: "Materials",
  patterns: "Patterns",
  pattern: "Patterns",
  stamps: "Stamps",
  stamp: "Stamps",
  meshes: "Meshes",
  mesh: "Meshes",
  models: "Meshes",
  colours: "Colours",
  colors: "Colours",
  colour: "Colours",
  images: "Images",
  image: "Images",
};

const MESH_PRESETS = BAKE_STUDY_SHAPES.map((s) => ({
  id: s.id,
  name: s.name || s.label,
  family: s.family || "Standard Study",
  desc: s.desc || s.label,
  topo: s.topo || "Sub-D Tri",
  complexity: s.complexity || "5k tris",
  role: s.role || "Surface Bake",
}));

const ATTRIBUTE_FILTERS = {
  Materials: [
    { id: "metallic", label: "Metallic", test: (it) => (it.raw?.metalness ?? 0) >= 0.45 },
    { id: "clearcoat", label: "Clearcoat", test: (it) => (it.raw?.coat ?? 0) >= 0.15 },
    { id: "emissive", label: "Emissive", test: (it) => (it.raw?.emissionStrength ?? 0) > 0.05 },
    { id: "iridescent", label: "Iridescent", test: (it) => (it.raw?.iridescence ?? 0) > 0.05 },
  ],
  Patterns: [
    { id: "noise", label: "Tileable Noise", test: (it) => String(it.id).startsWith("noise-") },
    { id: "african", label: "African Heritage", test: (it) => it.family === "African Patterns" },
    { id: "multilayer", label: "Multi-Layer", test: (it) => (it.meta?.layers || 1) >= 2 },
  ],
  Stamps: [
    { id: "hardsurface", label: "Hard Surface", test: (it) => it.family === "Hard Surface" },
    { id: "metallic-finish", label: "Metallic Foil", test: (it) => (it.meta?.finishes || []).includes("foil") },
    { id: "stencil-text", label: "Stencil Text", test: (it) => Boolean(it.meta?.hasText) },
  ],
  Meshes: [
    { id: "hard-surface-mesh", label: "Hard Surface", test: (it) => it.family === "Hard Surface" },
    { id: "organic-mesh", label: "Organic & Study", test: (it) => it.family !== "Hard Surface" },
  ],
};

function makeUuid() {
  if (typeof crypto !== "undefined" && typeof crypto.randomUUID === "function") {
    return crypto.randomUUID();
  }
  return `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 10)}`;
}

function normalizeKindName(raw) {
  if (!raw) return "Materials";
  if (CATEGORY_ORDER.includes(raw)) return raw;
  return CATEGORY_ALIASES[String(raw).trim().toLowerCase()] || "Materials";
}

function closestInteractive(target, selectors) {
  let cur = target;
  while (cur && typeof cur === "object") {
    const tag = String(cur.tagName || "").toLowerCase();
    for (const sel of selectors) {
      if (sel.startsWith(".")) {
        if (cur.classList?.contains?.(sel.slice(1))) return cur;
      } else if (sel.startsWith("[")) {
        const attrSpec = sel.slice(1, -1);
        const [attrName, attrVal] = attrSpec.split("=");
        const val = cur.getAttribute?.(attrName);
        if (attrVal !== undefined) {
          if (String(val) === attrVal) return cur;
        } else if (cur.hasAttribute?.(attrName) || val != null) {
          return cur;
        }
      } else if (tag === sel) {
        return cur;
      }
    }
    cur = cur.parentNode;
  }
  return null;
}

function createSvgElement(tag, attrs = {}) {
  const el =
    typeof document.createElementNS === "function"
      ? document.createElementNS(SVG_NS, tag)
      : document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    el.setAttribute?.(k, String(v));
  }
  return el;
}

function createBrowserIconSvg(name) {
  const svg = createSvgElement("svg", {
    viewBox: "0 0 24 24",
    width: "14",
    height: "14",
    fill: "none",
    stroke: "currentColor",
    "stroke-width": "2",
    "stroke-linecap": "round",
    "stroke-linejoin": "round",
    "aria-hidden": "true",
  });
  const addPath = (d) => svg.append(createSvgElement("path", { d }));
  const addRect = (x, y, w, h, rx) =>
    svg.append(createSvgElement("rect", { x, y, width: w, height: h, rx }));

  if (name === "search") {
    svg.append(createSvgElement("circle", { cx: 11, cy: 11, r: 7 }));
    addPath("m21 21-4.35-4.35");
  } else if (name === "grid") {
    addRect(3, 3, 7, 7, 1.4);
    addRect(14, 3, 7, 7, 1.4);
    addRect(3, 14, 7, 7, 1.4);
    addRect(14, 14, 7, 7, 1.4);
  } else if (name === "list") {
    addPath("M4 6h.01M9 6h11M4 12h.01M9 12h11M4 18h.01M9 18h11");
  } else if (name === "vertical") {
    addRect(3, 4, 18, 16, 2);
    addPath("M9.5 4v16M14.5 4v16");
  } else if (name === "horizontal") {
    addRect(3, 4, 18, 16, 2);
    addPath("M3 9.5h18M3 14.5h18");
  }
  return svg;
}

function makeMatIconSvg(mat, size = 64) {
  const col = mat?.color || "#888";
  const sec = mat?.secondaryColor || mat?.colors?.[0] || "#ffffff";
  const rough = Math.max(0.06, mat?.roughness ?? 0.4);
  const metal = mat?.metalness ?? 0;
  const uid = `mat-${String(mat?.id || "x").replace(/[^a-zA-Z0-9_-]/g, "")}`;
  const specAlpha = (0.28 + (1 - rough) * 0.55).toFixed(2);
  const rimAlpha = (0.18 + metal * 0.35).toFixed(2);
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 64 64">
    <defs>
      <radialGradient id="${uid}-g" cx="35%" cy="29%" r="72%">
        <stop offset="0%" stop-color="${sec}" stop-opacity="${specAlpha}"/>
        <stop offset="28%" stop-color="${col}" stop-opacity="1"/>
        <stop offset="78%" stop-color="${col}" stop-opacity="0.72"/>
        <stop offset="100%" stop-color="#07080a" stop-opacity="0.96"/>
      </radialGradient>
      <radialGradient id="${uid}-rim" cx="72%" cy="76%" r="55%">
        <stop offset="0%" stop-color="#9cc6ff" stop-opacity="${rimAlpha}"/>
        <stop offset="65%" stop-color="#9cc6ff" stop-opacity="0"/>
      </radialGradient>
    </defs>
    <circle cx="32" cy="32" r="28" fill="url(#${uid}-g)"/>
    <circle cx="32" cy="32" r="28" fill="url(#${uid}-rim)"/>
    <ellipse cx="23.5" cy="20" rx="${(4 + (1 - rough) * 5).toFixed(1)}" ry="${(2.6 + (1 - rough) * 2.5).toFixed(1)}" transform="rotate(-24 23.5 20)" fill="#ffffff" fill-opacity="${((1 - rough) * 0.45).toFixed(2)}"/>
  </svg>`;
}

function makeMeshIconSvg(meshId, size = 64) {
  const accent = "#d3c5b2";
  const stroke = "#b7c1cf";
  if (meshId === "torus") {
    return `<svg width="${size}" height="${size}" viewBox="0 0 64 64" fill="none"><ellipse cx="32" cy="32" rx="22" ry="12" stroke="${stroke}" stroke-width="4" transform="rotate(-25 32 32)"/><ellipse cx="32" cy="32" rx="22" ry="12" stroke="${accent}" stroke-width="2.5" transform="rotate(28 32 32)"/><circle cx="32" cy="32" r="5.5" stroke="${stroke}" stroke-width="2"/></svg>`;
  }
  if (meshId === "chamfer") {
    return `<svg width="${size}" height="${size}" viewBox="0 0 64 64" fill="none"><path d="M18 12h28l6 6v28l-6 6H18l-6-6V18z" fill="rgba(255,255,255,0.06)" stroke="${stroke}" stroke-width="2.6"/><path d="M22 22h20v20H22z" stroke="${accent}" stroke-width="1.8" opacity="0.85"/><path d="M12 18l10 4M52 18l-10 4M12 46l10-4M52 46l-10-4" stroke="${stroke}" stroke-width="1.4" opacity="0.5"/></svg>`;
  }
  if (meshId === "helmet") {
    return `<svg width="${size}" height="${size}" viewBox="0 0 64 64" fill="none"><path d="M16 33c0-10.5 7-18 16-18s16 7.5 16 18v12l-6 4H22l-6-4V33z" fill="rgba(255,255,255,0.06)" stroke="${stroke}" stroke-width="2.4"/><path d="M21 29h22l-3 9H24z" fill="rgba(211,197,178,0.22)" stroke="${accent}" stroke-width="1.8"/></svg>`;
  }
  if (meshId === "shaderball") {
    return `<svg width="${size}" height="${size}" viewBox="0 0 64 64" fill="none"><circle cx="32" cy="32" r="20" fill="rgba(255,255,255,0.06)" stroke="${stroke}" stroke-width="2.4"/><path d="M22 22a14 14 0 0 1 20 0v20a14 14 0 0 1-20 0z" stroke="${accent}" stroke-width="1.6" opacity="0.85"/><ellipse cx="32" cy="32" rx="20" ry="7" stroke="${stroke}" stroke-width="1.4" opacity="0.5"/></svg>`;
  }
  return `<svg width="${size}" height="${size}" viewBox="0 0 64 64" fill="none"><path d="M32 11l19 10.5v21L32 53 13 42.5v-21z" fill="rgba(255,255,255,0.06)" stroke="${stroke}" stroke-width="2.4"/><path d="M32 32l19-10.5M32 32v21M32 32L13 21.5" stroke="${accent}" stroke-width="1.8"/></svg>`;
}

function summarizeArtworkEntry(entry) {
  let doc = null;
  try {
    doc = typeof entry?.build === "function" ? entry.build() : entry?.doc || null;
  } catch {}
  const layers = doc?.layers || [];
  const finishes = [...new Set(layers.map((l) => l.surface?.finish).filter(Boolean))];
  const modifiers = layers.reduce((sum, l) => sum + (l.modifiers?.length || 0), 0);
  const colors = [...new Set(layers.map((l) => l.shape?.color).filter(Boolean))].slice(0, 5);
  const hasText = layers.some((l) => l.kind === "text");
  return {
    layers: layers.length || 1,
    finishes,
    primaryFinish: finishes[0] || "ink",
    modifiers,
    colors,
    hasText,
    width: doc?.width || 512,
    height: doc?.height || 512,
  };
}

async function assetBlobStore(id, value, remove = false) {
  if (typeof indexedDB === "undefined") return undefined;
  return new Promise((resolve, reject) => {
    try {
      const req = indexedDB.open(IDB_NAME, 1);
      req.onupgradeneeded = () => {
        if (!req.result.objectStoreNames.contains("Files")) {
          req.result.createObjectStore("Files");
        }
      };
      req.onerror = () => reject(req.error);
      req.onsuccess = () => {
        const db = req.result;
        const tx = db.transaction("Files", value || remove ? "readwrite" : "readonly");
        const store = tx.objectStore("Files");
        const op = remove ? store.delete(id) : value ? store.put(value, id) : store.get(id);
        let result;
        op.onsuccess = () => { result = op.result; };
        tx.oncomplete = () => { db.close(); resolve(result); };
        tx.onerror = () => { db.close(); reject(tx.error); };
      };
    } catch {
      resolve(undefined);
    }
  });
}

function loadSavedRecords() {
  try {
    if (typeof localStorage === "undefined") return [];
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return [];
    const parsed = JSON.parse(raw);
    if (!Array.isArray(parsed)) return [];
    return parsed.slice(0, 256).filter((it) => it && typeof it.id === "string" && it.id.startsWith("asset-"));
  } catch {
    return [];
  }
}

function persistSavedRecords(records) {
  try {
    if (typeof localStorage === "undefined") return;
    const serializable = records.slice(0, 256).map((r) => {
      const copy = { ...r };
      if (copy.assetKind === "mesh" && copy.raw?.transfer) {
        copy.raw = { ...copy.raw, transfer: undefined };
      }
      return copy;
    });
    localStorage.setItem(STORAGE_KEY, JSON.stringify(serializable));
  } catch {}
}

function triggerBlobDownload(blob, filename) {
  if (typeof document === "undefined" || typeof URL === "undefined" || typeof URL.createObjectURL !== "function") return;
  const link = document.createElement("a");
  const url = URL.createObjectURL(blob);
  link.href = url;
  link.download = filename;
  link.click?.();
  setTimeout(() => URL.revokeObjectURL?.(url), 1000);
}

function buildGeometryTransferFromThreeGeometry(geo, name = "Imported Mesh") {
  if (!geo) return null;
  const working = geo.index ? geo.toNonIndexed() : geo.clone();
  if (!working.attributes.normal) working.computeVertexNormals();
  const posAttr = working.attributes.position;
  const normAttr = working.attributes.normal;
  const uvAttr = working.attributes.uv;
  if (!posAttr || posAttr.count < 3) return null;

  const triCount = Math.floor(posAttr.count / 3);
  const positions = new Float32Array(posAttr.array.slice(0, triCount * 9));
  const normals = normAttr
    ? new Float32Array(normAttr.array.slice(0, triCount * 9))
    : new Float32Array(triCount * 9);
  const uvs = new Float32Array(triCount * 6);
  const surfacePart = new Float32Array(triCount * 3);

  for (let i = 0; i < triCount * 3; i++) {
    if (uvAttr) {
      uvs[i * 2] = uvAttr.getX(i);
      uvs[i * 2 + 1] = uvAttr.getY(i);
    } else {
      const px = positions[i * 3];
      const py = positions[i * 3 + 1];
      const pz = positions[i * 3 + 2];
      uvs[i * 2] = 0.5 + 0.35 * px;
      uvs[i * 2 + 1] = 0.5 + 0.35 * (py + pz * 0.35);
    }
  }

  working.computeBoundingBox();
  const box = working.boundingBox || new THREE.Box3();
  const center = box.getCenter(new THREE.Vector3());
  const size = box.getSize(new THREE.Vector3());
  const radius = Math.max(0.25, 0.5 * size.length());
  working.dispose();

  return {
    version: 1,
    source: "content-browser",
    name,
    triCount,
    partNames: [name],
    positions,
    normals,
    uvs,
    surfacePart,
    bounds: {
      min: [box.min.x, box.min.y, box.min.z],
      max: [box.max.x, box.max.y, box.max.z],
      center: [center.x, center.y, center.z],
      radius,
    },
    udimTiles: [{ id: 1001, label: "Tile 1001", resolution: 2048 }],
    importanceGroups: [],
    timestamp: Date.now(),
  };
}

export function createAssetBrowser(hostOrOpts, maybeBridge) {
  const hostEl = hostOrOpts && typeof hostOrOpts.append === "function" ? hostOrOpts : null;
  const appBridge = maybeBridge || (!hostEl ? hostOrOpts?.appBridge : null);
  const onSelectAsset = !hostEl ? hostOrOpts?.onSelectAsset : null;
  const onWorkspaceRoute = !hostEl ? hostOrOpts?.onWorkspaceRoute : null;

  // ------------------------------------------------------------------ BUILT-IN ENGINE CATALOGS
  const engineMaterials = materials.map((m) => ({
    id: m.id,
    name: m.name,
    Kind: "Materials",
    Origin: "Engine",
    family: m.category || "Materials",
    assetKind: "material",
    raw: m,
    detail: m.description || "Built-in procedural PBR material preset.",
  }));

  const enginePatterns = listPatternStarterCatalog().map((p) => ({
    id: p.id,
    name: p.name,
    Kind: "Patterns",
    Origin: "Engine",
    family: p.group || "Patterns",
    assetKind: "pattern",
    raw: p,
    meta: summarizeArtworkEntry(p),
    detail: "Procedural / SVG tileable pattern preset.",
  }));

  const engineStamps = listStampStarterCatalog().map((s) => ({
    id: s.id,
    name: s.name,
    Kind: "Stamps",
    Origin: "Engine",
    family: s.group || "Technical Decals",
    assetKind: "stamp",
    raw: s,
    meta: summarizeArtworkEntry(s),
    detail: "Vector decal stamp preset ready for Stamp Studio or 3D Paint.",
  }));

  const engineMeshes = MESH_PRESETS.map((m) => ({
    id: m.id,
    name: m.name,
    Kind: "Meshes",
    Origin: "Engine",
    family: m.family,
    assetKind: "mesh",
    raw: m,
    detail: m.desc,
  }));

  const engineColours = BuiltInColourCodex.map((colour, index) => ({
    id: "engine-colour-" + index,
    name: colour.Name,
    Kind: "Colours",
    Origin: "Engine",
    family: colour.Family || colour.FilterGroup,
    assetKind: "colour",
    Colour: colour,
    detail: "ColourCodex alias · drag onto any colour swatch or double-click to apply.",
  }));

  const engineLibrary = [
    ...engineMaterials,
    ...enginePatterns,
    ...engineStamps,
    ...engineMeshes,
    ...engineColours,
  ];

  let userAssets = loadSavedRecords();
  const runtimeBlobUrls = new Map();
  const runtimeMeshTransfers = new Map();

  function getLibrary() {
    return [...userAssets, ...engineLibrary];
  }

  function findItemById(id, preferredKind = null) {
    const lib = getLibrary();
    if (preferredKind && preferredKind !== "All") {
      const exact = lib.find((it) => it.id === id && it.Kind === preferredKind);
      if (exact) return exact;
    }
    return lib.find((it) => it.id === id) || null;
  }

  function toPayload(item) {
    if (!item) return null;
    if (item.assetKind === "mesh" && runtimeMeshTransfers.has(item.id)) {
      return {
        assetKind: "mesh",
        id: item.id,
        name: item.name,
        family: item.family,
        raw: { ...item.raw, transfer: runtimeMeshTransfers.get(item.id) },
      };
    }
    return {
      assetKind: item.assetKind,
      id: item.id,
      name: item.name,
      family: item.family,
      raw: item.raw || item.Colour || item,
      thumbnail: item.Thumbnail || item.Data || null,
    };
  }

  function dispatchApplyItem(item, opts = {}) {
    if (!item) return false;
    if (item.Kind === "Colours" && item.Colour) {
      const label = ApplyToActiveColourSlot(item.Colour);
      if (label) {
        setStatus(`Applied ${item.Colour.Name} (${item.Colour.Hex}) -> ${label}`);
        Toast(`Applied ${item.Colour.Name} -> ${label}`);
        return true;
      }
      setStatus("Click or focus any colour slot to apply this ColourCodex alias.");
      Toast("No active colour slot in current workspace.");
      return false;
    }
    if (item.Kind === "Meshes" && runtimeMeshTransfers.has(item.id)) {
      const transfer = runtimeMeshTransfers.get(item.id);
      const targetWs = opts.targetWorkspace || (state.activeWorkspace === "bake" ? "bake" : state.activeWorkspace === "material" ? "material" : "paint");
      const applied = appBridge?.applyImportedMesh?.(transfer, targetWs);
      if (applied) {
        setStatus(`Loaded mesh "${item.name}" into ${targetWs} stage.`);
        Toast(`Loaded mesh "${item.name}"`);
        return true;
      }
    }
    if (opts.targetWorkspace === "paint") {
      if (item.assetKind === "stamp" && appBridge?.openStampAssetInPaint) {
        appBridge.openStampAssetInPaint(item.raw);
        return true;
      }
      if (item.assetKind === "pattern" && appBridge?.openPatternAssetInPaint) {
        appBridge.openPatternAssetInPaint(item.raw);
        return true;
      }
    }
    if (opts.targetWorkspace === "material" && item.assetKind === "pattern" && appBridge?.sendPatternToMaterial) {
      appBridge.sendPatternToMaterial(item.raw);
      return true;
    }
    if (onSelectAsset) {
      onSelectAsset(item, opts);
      return true;
    }
    const ok = Boolean(appBridge?.applyAssetToActiveWorkspace?.(toPayload(item)));
    if (ok) {
      setStatus(`Applied ${item.name} (${item.Kind}) to active stage.`);
    }
    return ok;
  }

  // ------------------------------------------------------------------ DRAWER + CONTENTBROWSER STATE
  const state = {
    open: false,
    progress: 0, // 0 = closed, 0.46 / 0.5 = half/split, 1 = full
    rest: 0,
    offsetPx: 0,
    dragging: false,
    origin: "All", // All | Project | Imported | Engine
    activeKind: "Materials", // All | Materials | Patterns | Stamps | Meshes | Colours | Images
    activeFamily: "All",
    activeFilter: null,
    search: "",
    viewMode: "grid", // grid | list
    colourView: "grid", // grid | list-vertical | list-horizontal
    colourFilter: "All",
    colourSort: "spectrum",
    userSpecifiedCategory: false,
    activeWorkspace: "material",
    inspectorItemId: engineMaterials[0]?.id || null,
    statusMessage: "",
    renameDraft: "",
    hexDraft: "#3B1F3A",
    busy: false,
  };

  // Compatibility accessors mapped onto state cleanly
  Object.defineProperties(state, {
    activeCategory: {
      enumerable: true,
      get: () => state.activeKind.toLowerCase(),
      set: (v) => { state.activeKind = normalizeKindName(v); },
    },
    collapsed: {
      enumerable: true,
      get: () => !state.open,
      set: (v) => {
        if (v) closeDrawer();
        else openDrawer(1);
      },
    },
    stage: {
      enumerable: true,
      get: () => (state.progress === 0 ? "closed" : state.progress >= 0.99 ? "full" : "half"),
    },
  });

  // ------------------------------------------------------------------ DRAWER SHELL + SVG NOTCH DOM
  const drawerShell = Construct("section", "drawer-shell bottom asset-drawer");
  drawerShell.setAttribute("data-drawer", "bottom");
  drawerShell.setAttribute("data-stage", "closed");

  const drawerSurface = Construct("div", "drawer-surface");
  drawerSurface.setAttribute("role", "dialog");
  drawerSurface.setAttribute("aria-label", "ContentBrowser drawer");
  drawerSurface.setAttribute("aria-hidden", "true");
  drawerSurface.setAttribute("inert", "");

  const browserRoot = Construct("aside", "asset-browser");
  browserRoot.setAttribute("aria-label", "ContentBrowser");

  const pageGrip = Construct("button", "drawer-page-grip");
  pageGrip.type = "button";
  pageGrip.setAttribute("aria-label", "Close ContentBrowser using page grip");
  pageGrip.append(Construct("i"), Construct("span", null, "Drag the page or handle to close"));

  const notchHandle = Construct("button", "drawer-handle asset-notch-handle");
  notchHandle.type = "button";
  notchHandle.setAttribute("aria-label", "ContentBrowser notch");
  notchHandle.setAttribute("aria-expanded", "false");
  notchHandle.title = "ContentBrowser · drag to open/close · drag sideways to reposition";

  const notchSvg = createSvgElement("svg", {
    viewBox: "0 0 400 36",
    preserveAspectRatio: "none",
    "aria-hidden": "true",
  });
  notchSvg.append(
    createSvgElement("path", {
      d: "M0 0C15 0 20 6 25 15L35 28C40 34 45 36 52 36H348C355 36 360 34 365 28L375 15C380 6 385 0 400 0Z",
    }),
  );
  const notchLabel = Construct("span", null, "ContentBrowser");
  notchHandle.append(notchSvg, notchLabel);

  drawerSurface.append(browserRoot, pageGrip);
  drawerShell.append(drawerSurface, notchHandle);
  if (hostEl) hostEl.append(drawerShell);

  // ------------------------------------------------------------------ CONTENTBROWSER INTERIOR DOM
  // 1. Header
  const headerEl = Construct("header", "asset-browser-header drawer-drag-zone");
  const headTitleBox = Construct("div");
  const eyebrowEl = Construct("span", "asset-eyebrow", "CONTENT / LIBRARY");
  const titleHeading = Construct("h1");
  const titleCountSmall = Construct("small", null, `${getLibrary().length} items`);
  titleHeading.append("ContentBrowser ", titleCountSmall);
  headTitleBox.append(eyebrowEl, titleHeading);

  const headActionsEl = Construct("div", "asset-header-actions");
  const autoFollowBtn = Construct("button", "asset-auto-pill");
  autoFollowBtn.type = "button";
  autoFollowBtn.title = "Follow active Texture Studio workspace automatically, or reset pin";

  const addMatBtn = Construct("button", null, "+ Material");
  addMatBtn.type = "button";
  const addColourBtn = Construct("button", null, "+ Colour");
  addColourBtn.type = "button";
  const exportTomlBtn = Construct("button", null, "Export .toml");
  exportTomlBtn.type = "button";

  const importBtn = Construct("button", "primary", "Import files");
  importBtn.type = "button";

  const fileInput = document.createElement("input");
  fileInput.type = "file";
  fileInput.multiple = true;
  fileInput.hidden = true;
  fileInput.accept = ".png,.jpg,.jpeg,.webp,.svg,.obj,.stl,.json,.material.json,.toml";
  fileInput.setAttribute("aria-label", "Import assets");

  const splitToggleBtn = Construct("button", "asset-split-toggle");
  splitToggleBtn.type = "button";
  splitToggleBtn.setAttribute("aria-label", "Toggle split content shelf");
  splitToggleBtn.setAttribute("aria-pressed", "false");
  splitToggleBtn.title = "Dock ContentBrowser to bottom half so the 3D/2D studio stage stays visible above";

  const closeBtn = Construct("button", null, "×");
  closeBtn.type = "button";
  closeBtn.setAttribute("aria-label", "Close ContentBrowser");

  headActionsEl.append(
    autoFollowBtn,
    addMatBtn,
    addColourBtn,
    exportTomlBtn,
    importBtn,
    fileInput,
    splitToggleBtn,
    closeBtn,
  );
  headerEl.append(headTitleBox, headActionsEl);

  // 2. Tools Bar (Origin Tabs | Search | View Switch)
  const toolsEl = Construct("div", "asset-browser-tools");
  const originTabsEl = Construct("div", "asset-origin-tabs");
  originTabsEl.setAttribute("role", "group");
  originTabsEl.setAttribute("aria-label", "Asset origin");
  const originBtns = new Map();
  for (const originName of ["All", "Project", "Imported", "Engine"]) {
    const btn = Construct("button", null, originName);
    btn.type = "button";
    btn.dataset.origin = originName;
    btn.setAttribute("aria-pressed", String(originName === state.origin));
    btn.addEventListener("click", () => {
      state.origin = originName;
      renderAll();
    });
    originBtns.set(originName, btn);
    originTabsEl.append(btn);
  }

  const searchWrap = Construct("div", "asset-search-wrap");
  searchWrap.append(createBrowserIconSvg("search"));
  const searchInput = document.createElement("input");
  searchInput.type = "search";
  searchInput.setAttribute("aria-label", "Search assets");
  searchInput.placeholder = "Search ContentBrowser…";
  const searchClearBtn = Construct("button", "asset-search-clear", "×");
  searchClearBtn.type = "button";
  searchClearBtn.setAttribute("aria-label", "Clear search");
  searchClearBtn.hidden = true;
  searchInput.addEventListener("input", () => {
    state.search = searchInput.value;
    searchClearBtn.hidden = !state.search;
    renderResultsAndPreview();
  });
  searchClearBtn.addEventListener("click", () => {
    state.search = "";
    searchInput.value = "";
    searchClearBtn.hidden = true;
    renderResultsAndPreview();
  });
  searchWrap.append(searchInput, searchClearBtn);

  const viewSwitchEl = Construct("div", "asset-view-switch");
  viewSwitchEl.setAttribute("role", "group");
  viewSwitchEl.setAttribute("aria-label", "Content layout");

  toolsEl.append(originTabsEl, searchWrap, viewSwitchEl);

  // 3. Dedicated Filter Bar (.colour-filters-bar)
  const filterBarEl = Construct("div", "colour-filters-bar");
  filterBarEl.setAttribute("role", "region");
  filterBarEl.setAttribute("aria-label", "Content Filters");

  // 4. 3-Column Body
  const bodyEl = Construct("div", "asset-browser-body");

  const railNav = Construct("nav", "asset-category-rail");
  railNav.setAttribute("aria-label", "Asset categories");
  railNav.setAttribute("data-drawer-scroll", "");

  const resultsSec = Construct("section", "asset-results");
  const resultsHeader = Construct("header", "drawer-drag-zone");
  const resultsHeading = Construct("h2", null, "Materials");
  const resultsCount = Construct("span", null, "0 results");
  resultsHeader.append(resultsHeading, resultsCount);
  const catalogueDiv = Construct("div", "asset-catalogue grid");
  catalogueDiv.setAttribute("data-drawer-scroll", "");
  resultsSec.append(resultsHeader, catalogueDiv);

  const previewAside = Construct("aside", "asset-preview");
  previewAside.setAttribute("data-drawer-scroll", "");

  bodyEl.append(railNav, resultsSec, previewAside);

  // 5. Footer
  const footerEl = Construct("footer", "asset-browser-footer");
  const statusSpan = Construct("span");
  statusSpan.setAttribute("role", "status");
  const brandFootSpan = Construct("span", null, "CONTENTBROWSER · TEXTURE STUDIO");
  footerEl.append(statusSpan, brandFootSpan);

  browserRoot.append(headerEl, toolsEl, filterBarEl, bodyEl, footerEl);

  // ------------------------------------------------------------------ DRAWER GEOMETRY & 3-STOP DRAG GESTURE
  function getHeaderBottom() { return 90; }
  function getFooterHeight() { return 25; }
  function getExtent() {
    const h = Number(typeof window !== "undefined" ? window.innerHeight : 900) || 900;
    return Math.max(320, h - getHeaderBottom() - getFooterHeight());
  }

  function applyDrawerGeometry() {
    const extent = getExtent();
    const prog = Math.max(0, Math.min(1, state.progress));
    const stage = prog === 0 ? "closed" : prog >= 0.99 ? "full" : "half";
    const isOpen = state.open || prog > 0;
    const visiblePx = Math.max(0, extent * prog);

    drawerShell.classList.toggle("open", isOpen);
    drawerShell.classList.toggle("dragging", state.dragging);
    drawerShell.setAttribute("data-stage", stage);
    drawerShell.style.setProperty("--drawer-visible", `${visiblePx.toFixed(1)}px`);
    drawerShell.style.transform = `translateY(${(extent * (1 - prog)).toFixed(1)}px)`;

    notchHandle.setAttribute("aria-expanded", String(state.open));
    notchHandle.style.left = `calc(50% - 110px + ${state.offsetPx}px)`;

    if (!state.open && prog === 0) {
      drawerSurface.setAttribute("aria-hidden", "true");
      drawerSurface.setAttribute("inert", "");
      drawerSurface.removeAttribute("aria-modal");
    } else {
      drawerSurface.removeAttribute("aria-hidden");
      drawerSurface.removeAttribute("inert");
      if (stage === "full") drawerSurface.setAttribute("aria-modal", "true");
      else drawerSurface.removeAttribute("aria-modal");
    }

    splitToggleBtn.setAttribute("aria-pressed", String(stage === "half"));
    splitToggleBtn.textContent = stage === "half" ? "⬜ Full" : "⬒ Split";
  }

  function settleDrawer(nextProgress) {
    const clamped = Math.max(0, Math.min(1, nextProgress));
    state.rest = clamped;
    state.progress = clamped;
    state.open = clamped > 0;
    state.dragging = false;
    applyDrawerGeometry();
  }

  function openDrawer(targetProgress = 1) {
    settleDrawer(targetProgress > 0 ? targetProgress : 1);
    notchHandle.focus?.({ preventScroll: true });
  }

  function closeDrawer() {
    settleDrawer(0);
  }

  // Notch + Drawer gesture state matching DrawerPanel.jsx
  let gesture = null;
  let suppressGripClick = false;
  let settledByKeydown = false;

  function beginGesture(e, fromHandle = false) {
    if (e.button !== undefined && e.button !== 0) return;
    if (gesture) return;
    settledByKeydown = false;
    if (
      !fromHandle &&
      (!state.open ||
        (closestInteractive(e.target, [
          "button",
          "input",
          "select",
          "textarea",
          "a",
          "[contenteditable=true]",
          "[data-drawer-scroll]",
          "[draggable=true]",
        ]) &&
          !closestInteractive(e.target, [".drawer-page-grip"])))
    ) {
      return;
    }
    gesture = {
      id: e.pointerId ?? 1,
      x: e.clientX ?? 0,
      y: e.clientY ?? 0,
      start: state.rest,
      offset: state.offsetPx,
      axis: null,
      fromHandle,
      samples: [[performance.now?.() || Date.now(), e.clientY ?? 0]],
      current: state.rest,
    };
    if (fromHandle && e.pointerId != null) {
      try { drawerShell.setPointerCapture?.(e.pointerId); } catch {}
    }
  }

  function moveGesture(e) {
    if (!gesture || (e.pointerId != null && gesture.id !== e.pointerId)) return;
    const cx = e.clientX ?? 0;
    const cy = e.clientY ?? 0;
    const dx = cx - gesture.x;
    const dy = cy - gesture.y;
    if (!gesture.axis && Math.hypot(dx, dy) > 7) {
      gesture.axis = Math.abs(dy) > Math.abs(dx) ? "Y" : "X";
      if (gesture.axis === "Y" && e.pointerId != null) {
        try { drawerShell.setPointerCapture?.(e.pointerId); } catch {}
      }
    }
    if (gesture.axis === "X" && gesture.fromHandle) {
      const w = Number(typeof window !== "undefined" ? window.innerWidth : 1440) || 1440;
      const maxShift = Math.max(0, (w - 220) / 2);
      state.offsetPx = Math.max(-maxShift, Math.min(maxShift, gesture.offset + dx));
      state.dragging = true;
      applyDrawerGeometry();
    }
    if (gesture.axis === "Y") {
      e.preventDefault?.();
      const extent = getExtent();
      const next = Math.max(0, Math.min(1, gesture.start + (-dy) / extent));
      gesture.current = next;
      state.progress = next;
      state.dragging = true;
      applyDrawerGeometry();
      const now = performance.now?.() || Date.now();
      gesture.samples.push([now, cy]);
      while (gesture.samples.length > 2 && now - gesture.samples[0][0] > 120) {
        gesture.samples.shift();
      }
    }
  }

  function endGesture(e, cancelled = false) {
    if (!gesture || (e?.pointerId != null && gesture.id !== e.pointerId)) return;
    let next = gesture.start;
    if (!cancelled && gesture.axis === "Y") {
      const first = gesture.samples[0];
      const last = gesture.samples[gesture.samples.length - 1];
      const now = performance.now?.() || Date.now();
      const idle = now - last[0];
      const vel =
        idle < 120
          ? ((-1 * (last[1] - first[1])) / Math.max(16, last[0] - first[0])) * 1000
          : 0;
      next = gesture.current < 0.25 ? 0 : gesture.current > 0.75 ? 1 : 0.5;
      if (Math.abs((e?.clientY ?? gesture.y) - gesture.y) > 12 && Math.abs(vel) > 300) {
        next =
          Math.abs(vel) > 1000
            ? vel > 0
              ? 1
              : 0
            : Math.max(0, Math.min(1, gesture.start + (vel > 0 ? 0.5 : -0.5)));
      }
    } else if (!cancelled && !gesture.axis && gesture.fromHandle) {
      next = state.open ? 0 : 1;
    }
    suppressGripClick = Boolean(gesture.axis) && closestInteractive(e?.target, [".drawer-page-grip"]) != null;
    gesture = null;
    settleDrawer(next);
  }

  drawerShell.addEventListener("pointerdown", (e) => beginGesture(e, false));
  drawerShell.addEventListener("pointermove", moveGesture);
  drawerShell.addEventListener("pointerup", (e) => endGesture(e, false));
  drawerShell.addEventListener("pointercancel", (e) => endGesture(e, true));
  drawerShell.addEventListener("lostpointercapture", (e) => {
    if (gesture && e.target === drawerShell) endGesture(e, true);
  });

  if (typeof window !== "undefined") {
    window.addEventListener("pointermove", moveGesture);
    window.addEventListener("pointerup", (e) => endGesture(e, false));
    window.addEventListener("resize", () => {
      gesture = null;
      const w = Number(window.innerWidth) || 1440;
      const maxShift = Math.max(0, (w - 220) / 2);
      state.offsetPx = Math.max(-maxShift, Math.min(maxShift, state.offsetPx));
      applyDrawerGeometry();
    });
    window.addEventListener("frontier:colour-applied", (ev) => {
      const detail = ev?.detail;
      if (detail?.Colour && detail?.TargetLabel) {
        setStatus(`Applied ${detail.Colour.Name} (${detail.Colour.Hex}) -> ${detail.TargetLabel}`);
      }
    });
  }

  notchHandle.addEventListener("pointerdown", (e) => {
    e.stopPropagation?.();
    beginGesture(e, true);
  });
  notchHandle.addEventListener("click", (e) => {
    if (settledByKeydown) {
      settledByKeydown = false;
      return;
    }
    if (!e?.detail) {
      settleDrawer(state.open ? 0 : 1);
    }
  });
  notchHandle.addEventListener("keydown", (e) => {
    if (e.key === "Enter" || e.key === " ") {
      e.preventDefault?.();
      settledByKeydown = true;
      settleDrawer(state.open ? 0 : 1);
    }
  });

  pageGrip.addEventListener("click", () => {
    if (!suppressGripClick) closeDrawer();
    suppressGripClick = false;
  });

  drawerShell.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && state.open) {
      e.stopPropagation?.();
      closeDrawer();
    }
  });

  // ------------------------------------------------------------------ FILTERING & DATA HELPERS
  function setStatus(msg) {
    state.statusMessage = msg;
    updateFooter();
  }

  function updateFooter() {
    if (state.statusMessage) {
      statusSpan.textContent = state.statusMessage;
    } else if (state.activeKind === "Colours") {
      statusSpan.textContent = "Drag any colour swatch/strip onto a colour slot or double-click to apply";
    } else {
      statusSpan.textContent = "Drag assets onto the 3D/2D studio stage · double-click to apply · drag header or notch to resize";
    }
  }

  function getItemsForKind(kind = state.activeKind) {
    const lib = getLibrary();
    return lib.filter(
      (it) =>
        (state.origin === "All" || it.Origin === state.origin) &&
        (kind === "All" || it.Kind === kind),
    );
  }

  function getFamiliesForKind(kind) {
    const items = getItemsForKind(kind);
    const counts = new Map();
    for (const it of items) {
      const fam = it.family || it.Kind;
      counts.set(fam, (counts.get(fam) || 0) + 1);
    }
    return [...counts.entries()].map(([name, count]) => ({ name, count }));
  }

  function getFilteredItems() {
    const lib = getLibrary();
    const q = state.search.trim().toLowerCase();
    if (state.activeKind === "Colours") {
      const base = lib.filter(
        (it) =>
          it.Kind === "Colours" &&
          (state.origin === "All" || it.Origin === state.origin) &&
          (`${it.name} ${it.Colour?.Hex || ""} ${it.family || ""}`).toLowerCase().includes(q) &&
          MatchesColourFilter(it.Colour, state.colourFilter),
      );
      return SortColourAssets(base, state.colourSort);
    }

    const activeAttrDef = (ATTRIBUTE_FILTERS[state.activeKind] || []).find((f) => f.id === state.activeFilter);
    return lib.filter((it) => {
      if (state.origin !== "All" && it.Origin !== state.origin) return false;
      if (state.activeKind !== "All" && it.Kind !== state.activeKind) return false;
      if (state.activeFamily !== "All" && it.family !== state.activeFamily) return false;
      if (activeAttrDef && !activeAttrDef.test(it)) return false;
      if (q) {
        const hay = `${it.name} ${it.family || ""} ${it.Kind} ${it.Colour?.Hex || ""}`.toLowerCase();
        if (!hay.includes(q)) return false;
      }
      return true;
    });
  }

  // ------------------------------------------------------------------ PROJECT ASSET AUTHORING & IMPORT
  function createProjectMaterial() {
    if (userAssets.length >= 256) {
      setStatus("Library limit reached (256 user assets). Remove an item first.");
      return null;
    }
    const baseRaw = appBridge?.getActiveMaterialParams?.() || materials[0];
    const names = new Set(getLibrary().map((a) => a.name));
    const baseName = baseRaw?.name ? `${baseRaw.name} Copy` : "Untitled Material";
    let unique = baseName;
    let idx = 2;
    while (names.has(unique)) unique = `${baseName} ${idx++}`;

    const matCopy = {
      ...structuredClone(baseRaw),
      id: `asset-mat-${makeUuid().slice(0, 8)}`,
      name: unique,
      category: "Project",
      description: "Browser-authored material snapshot from Texture Studio.",
    };
    const record = {
      id: "asset-" + makeUuid(),
      name: unique,
      Kind: "Materials",
      Origin: "Project",
      family: "Project",
      assetKind: "material",
      raw: matCopy,
      detail: "Browser-authored material preset stored in this browser.",
    };
    userAssets = [record, ...userAssets];
    persistSavedRecords(userAssets);
    state.origin = "Project";
    state.activeKind = "Materials";
    state.activeFamily = "All";
    state.activeFilter = null;
    state.search = "";
    searchInput.value = "";
    state.inspectorItemId = record.id;
    setStatus(`Saved "${unique}" to Project Materials`);
    renderAll();
    return record;
  }

  function createProjectColour(initialHex = null, baseName = "Untitled Colour") {
    if (userAssets.length >= 256) {
      setStatus("Library limit reached (256 user assets). Remove an item first.");
      return null;
    }
    const presets = ["#3B1F3A", "#F6B800", "#2454FF", "#E63956", "#10B981"];
    const names = new Set(getLibrary().map((a) => a.name));
    let unique = baseName;
    let idx = 2;
    while (names.has(unique)) unique = `${baseName} ${idx++}`;
    const existingCount = userAssets.filter((a) => a.Kind === "Colours").length;
    const colour = ResolveColourEntry({
      Name: unique,
      Hex: initialHex || presets[existingCount % presets.length],
      Family: "Project",
    });
    const record = {
      id: "asset-" + makeUuid(),
      name: unique,
      Kind: "Colours",
      Origin: "Project",
      family: "Project",
      assetKind: "colour",
      Colour: colour,
      detail: "Browser-authored ColourCodex alias · drag onto any colour slot or double-click to apply.",
    };
    userAssets = [record, ...userAssets];
    persistSavedRecords(userAssets);
    state.origin = "Project";
    state.activeKind = "Colours";
    state.colourFilter = "All";
    state.search = "";
    searchInput.value = "";
    state.inspectorItemId = record.id;
    setStatus(`Created ${unique} (${colour.Hex})`);
    renderAll();
    return record;
  }

  async function importFiles(filesList) {
    const files = [...(filesList || [])];
    if (!files.length || state.busy) return;
    state.busy = true;
    importBtn.disabled = true;
    importBtn.textContent = "Importing…";

    const added = [];
    const errors = [];
    const cap = Math.max(0, 256 - userAssets.length);

    for (const file of files.slice(0, Math.min(32, cap))) {
      try {
        if (file.size > 16 * 1024 * 1024) {
          throw new Error(`${file.name}: maximum file size is 16 MB.`);
        }
        const lower = file.name.toLowerCase();
        if (lower.endsWith(".toml")) {
          const parsed = ParseColourCodexToml(await file.text());
          if (!parsed.length) throw new Error(`${file.name}: no valid ColourCodex entries found.`);
          for (const col of parsed.slice(0, Math.max(1, cap - added.length))) {
            added.push({
              id: "asset-" + makeUuid(),
              name: col.Name,
              Kind: "Colours",
              Origin: "Imported",
              family: col.Family || "Imported",
              assetKind: "colour",
              Size: file.size,
              Colour: col,
              detail: `Imported from ${file.name} · drag onto any colour slot or double-click to apply.`,
            });
          }
          continue;
        }

        if (lower.endsWith(".material.json") || lower.endsWith(".json")) {
          const text = await file.text();
          const json = JSON.parse(text);
          const rawMat = json.material || json.Material || json;
          if (!rawMat || typeof rawMat !== "object") {
            throw new Error(`${file.name}: invalid material JSON.`);
          }
          const matId = `imp-mat-${makeUuid().slice(0, 8)}`;
          const rec = {
            id: "asset-" + makeUuid(),
            name: rawMat.name || rawMat.Name || file.name.replace(/(\.material)?\.json$/i, ""),
            Kind: "Materials",
            Origin: "Imported",
            family: rawMat.category || "Imported",
            assetKind: "material",
            Size: file.size,
            raw: { ...rawMat, id: matId },
            detail: `Imported material definition (${file.name}).`,
          };
          await assetBlobStore(rec.id, file);
          added.push(rec);
          continue;
        }

        const ext = lower.split(".").pop();
        if (ext === "obj" || ext === "stl") {
          let threeGeo = null;
          if (ext === "obj") {
            const grp = new OBJLoader().parse(await file.text());
            grp.traverse((ch) => { if (ch.isMesh && !threeGeo) threeGeo = ch.geometry.clone(); });
          } else {
            threeGeo = new STLLoader().parse(await file.arrayBuffer());
          }
          if (!threeGeo) throw new Error(`${file.name}: could not extract mesh geometry.`);
          threeGeo.center();
          threeGeo.computeBoundingSphere();
          const normScale = 1.45 / Math.max(0.01, threeGeo.boundingSphere?.radius || 1);
          threeGeo.scale(normScale, normScale, normScale);
          const meshName = file.name.replace(/\.(obj|stl)$/i, "");
          const transfer = buildGeometryTransferFromThreeGeometry(threeGeo, meshName);
          threeGeo.dispose();
          const meshRecId = "asset-" + makeUuid();
          if (transfer) runtimeMeshTransfers.set(meshRecId, transfer);
          const rec = {
            id: meshRecId,
            name: meshName,
            Kind: "Meshes",
            Origin: "Imported",
            family: "Imported",
            assetKind: "mesh",
            Size: file.size,
            raw: {
              id: meshRecId,
              name: meshName,
              family: "Imported",
              topo: ext.toUpperCase(),
              complexity: transfer ? `${(transfer.triCount / 1000).toFixed(1)}k tris` : "Mesh",
              role: "Imported Mesh",
              desc: `Imported ${ext.toUpperCase()} mesh (${transfer ? transfer.triCount.toLocaleString() : "?"} tris).`,
            },
            detail: `Imported ${ext.toUpperCase()} geometry (${transfer ? transfer.triCount.toLocaleString() : "?"} tris).`,
          };
          await assetBlobStore(rec.id, file);
          added.push(rec);
          continue;
        }

        if (["png", "jpg", "jpeg", "webp", "svg"].includes(ext)) {
          const recId = "asset-" + makeUuid();
          let dataUrl = "";
          let width = 0;
          let height = 0;
          if (typeof URL !== "undefined" && typeof URL.createObjectURL === "function" && typeof Image !== "undefined") {
            const objUrl = URL.createObjectURL(file);
            runtimeBlobUrls.set(recId, objUrl);
            try {
              const img = new Image();
              img.src = objUrl;
              await img.decode?.();
              width = img.naturalWidth || img.width || 0;
              height = img.naturalHeight || img.height || 0;
              if (width > 0 && height > 0) {
                const c = document.createElement("canvas");
                const ratio = Math.min(1, 128 / Math.max(width, height));
                c.width = Math.max(1, Math.round(width * ratio));
                c.height = Math.max(1, Math.round(height * ratio));
                c.getContext?.("2d")?.drawImage?.(img, 0, 0, c.width, c.height);
                dataUrl = c.toDataURL?.("image/png") || "";
              }
            } catch {}
          }
          const rec = {
            id: recId,
            name: file.name,
            Kind: "Images",
            Origin: "Imported",
            family: ext.toUpperCase(),
            assetKind: "image",
            Size: file.size,
            Width: width,
            Height: height,
            Thumbnail: dataUrl || undefined,
            Data: runtimeBlobUrls.get(recId) || dataUrl || undefined,
            raw: {
              id: recId,
              name: file.name,
              dataUrl: runtimeBlobUrls.get(recId) || dataUrl,
              width,
              height,
            },
            detail: `Imported ${ext.toUpperCase()} image${width ? ` (${width} × ${height})` : ""}.`,
          };
          await assetBlobStore(rec.id, file);
          added.push(rec);
          continue;
        }

        throw new Error(`${file.name}: unsupported extension.`);
      } catch (err) {
        errors.push(err?.message || String(err));
      }
    }

    if (added.length) {
      userAssets = [...added, ...userAssets];
      persistSavedRecords(userAssets);
      state.origin = "Imported";
      state.activeKind = added.every((a) => a.Kind === added[0].Kind) ? added[0].Kind : "All";
      state.activeFamily = "All";
      state.activeFilter = null;
      state.colourFilter = "All";
      state.search = "";
      searchInput.value = "";
      state.inspectorItemId = added[0].id;
    }

    setStatus(
      `${added.length} imported${errors.length ? " · " + errors.join(" · ") : " · ready in ContentBrowser"}`,
    );
    state.busy = false;
    importBtn.disabled = false;
    importBtn.textContent = "Import files";
    renderAll();
  }

  async function removeAsset(item) {
    if (!item || item.Origin === "Engine") return;
    await assetBlobStore(item.id, undefined, true);
    userAssets = userAssets.filter((a) => a.id !== item.id);
    runtimeMeshTransfers.delete(item.id);
    if (runtimeBlobUrls.has(item.id)) {
      URL.revokeObjectURL?.(runtimeBlobUrls.get(item.id));
      runtimeBlobUrls.delete(item.id);
    }
    persistSavedRecords(userAssets);
    if (state.inspectorItemId === item.id) state.inspectorItemId = null;
    setStatus(`Removed "${item.name}" from browser library.`);
    renderAll();
  }

  async function downloadAssetItem(item) {
    if (!item) return;
    if (item.Kind === "Colours" && item.Colour) {
      const toml = SerializeColourCodexToml([item.Colour]);
      triggerBlobDownload(
        new Blob([toml], { type: "application/toml;charset=utf-8" }),
        `${item.name.replace(/\s+/g, "")}.toml`,
      );
      setStatus(`Downloaded ${item.name}.toml`);
      return;
    }
    if (item.Kind === "Materials") {
      const json = JSON.stringify({ Format: "FrontierTextureMaterial.v1", Material: item.raw }, null, 2);
      triggerBlobDownload(
        new Blob([json], { type: "application/json" }),
        `${item.name.replace(/\s+/g, "_")}.material.json`,
      );
      setStatus(`Downloaded ${item.name}.material.json`);
      return;
    }
    if (item.Kind === "Patterns" || item.Kind === "Stamps") {
      const kindArg = item.Kind === "Patterns" ? "pattern" : "stamp";
      const svg = getArtworkPreviewSvg(item.raw, kindArg, 512);
      triggerBlobDownload(
        new Blob([svg], { type: "image/svg+xml;charset=utf-8" }),
        `${item.name.replace(/\s+/g, "_")}.svg`,
      );
      setStatus(`Downloaded ${item.name}.svg`);
      return;
    }
    const stored = await assetBlobStore(item.id);
    if (stored) {
      triggerBlobDownload(stored, item.name);
      setStatus(`Downloaded ${item.name}`);
      return;
    }
    setStatus("Download exported summary for engine preset.");
  }

  // ------------------------------------------------------------------ VISUAL ICON BUILDER (NO THUMB BOXES)
  function createAssetThumbnailNode(item, size = 70) {
    const thumb = Construct("span", "asset-thumbnail");
    if (item.Kind === "Colours" && item.Colour) {
      const circle = Construct("span", "asset-colour-circle");
      circle.style.backgroundColor = item.Colour.Hex;
      thumb.append(circle);
      return thumb;
    }
    if (item.Kind === "Materials") {
      const img = document.createElement("img");
      img.className = "cb-mat-baked-img";
      img.alt = item.name;
      img.draggable = false;
      const bakedUrl = getBakedMaterialIcon(item.raw, (upgradedUrl) => {
        if (img) img.src = upgradedUrl;
      });
      if (bakedUrl) {
        img.src = bakedUrl;
        thumb.append(img);
      } else {
        thumb.innerHTML = makeMatIconSvg(item.raw, size);
      }
      return thumb;
    }
    if (item.Kind === "Patterns") {
      thumb.classList.add("cb-icon-pattern");
      thumb.innerHTML = getArtworkPreviewSvg(item.raw, "pattern", size);
      return thumb;
    }
    if (item.Kind === "Stamps") {
      thumb.classList.add("cb-icon-stamp");
      thumb.innerHTML = getArtworkPreviewSvg(item.raw, "stamp", size);
      return thumb;
    }
    if (item.Kind === "Meshes") {
      thumb.classList.add("cb-icon-mesh");
      const bakedUrl = getBakedGeometryIcon(item.raw?.id || item.id, Math.max(88, Math.min(128, size * 2)));
      if (bakedUrl) {
        const img = document.createElement("img");
        img.className = "cb-mat-baked-img";
        img.alt = item.name;
        img.draggable = false;
        img.src = bakedUrl;
        thumb.append(img);
      } else {
        thumb.innerHTML = makeMeshIconSvg(item.raw?.id || item.id, size);
      }
      return thumb;
    }
    if (item.Kind === "Images") {
      const src = item.Data || item.Thumbnail;
      if (src) {
        const img = document.createElement("img");
        img.src = src;
        img.alt = item.name;
        img.loading = "lazy";
        thumb.append(img);
      } else {
        thumb.append(Construct("span", "asset-kind-icon", "▤"));
      }
      return thumb;
    }
    thumb.append(Construct("span", "asset-kind-icon", "◇"));
    return thumb;
  }

  // ------------------------------------------------------------------ RENDERING: HEADER, TOOLBAR, RAIL, CATALOGUE, DETAILS
  function renderHeaderControls() {
    const lib = getLibrary();
    titleCountSmall.textContent = `${lib.length} items`;

    autoFollowBtn.textContent = state.userSpecifiedCategory
      ? "Pinned · Reset"
      : `Auto · ${state.activeWorkspace}`;
    autoFollowBtn.classList.toggle("pinned", state.userSpecifiedCategory);

    exportTomlBtn.hidden = state.activeKind !== "Colours";

    for (const [originName, btn] of originBtns.entries()) {
      btn.setAttribute("aria-pressed", String(originName === state.origin));
    }

    // View switch (2-way Grid/List or 3-way Grid/Vertical/Horizontal for Colours)
    viewSwitchEl.innerHTML = "";
    if (state.activeKind === "Colours") {
      viewSwitchEl.className = "asset-view-switch colour-view-switch";
      viewSwitchEl.setAttribute("aria-label", "Colour layout");
      const modes = [
        { id: "grid", label: "Grid", icon: "grid", aria: "grid asset view" },
        { id: "list-vertical", label: "Vertical", icon: "vertical", aria: "vertical list colour view" },
        { id: "list-horizontal", label: "Horizontal", icon: "horizontal", aria: "horizontal list colour view" },
      ];
      for (const m of modes) {
        const btn = Construct("button");
        btn.type = "button";
        btn.dataset.view = m.id;
        btn.setAttribute("aria-label", m.aria);
        btn.setAttribute("aria-pressed", String(state.colourView === m.id));
        btn.append(createBrowserIconSvg(m.icon), Construct("span", null, m.label));
        btn.addEventListener("click", () => {
          state.colourView = m.id;
          renderHeaderControls();
          renderResults();
        });
        viewSwitchEl.append(btn);
      }
    } else {
      viewSwitchEl.className = "asset-view-switch";
      viewSwitchEl.setAttribute("aria-label", "Content layout");
      for (const m of [
        { id: "grid", label: "Grid", aria: "grid asset view" },
        { id: "list", label: "List", aria: "list asset view" },
      ]) {
        const btn = Construct("button");
        btn.type = "button";
        btn.dataset.view = m.id;
        btn.setAttribute("aria-label", m.aria);
        btn.setAttribute("aria-pressed", String(state.viewMode === m.id));
        btn.append(createBrowserIconSvg(m.id), Construct("span", null, m.label));
        btn.addEventListener("click", () => {
          state.viewMode = m.id;
          renderHeaderControls();
          renderResults();
        });
        viewSwitchEl.append(btn);
      }
    }
  }

  function renderFilterBar() {
    filterBarEl.innerHTML = "";
    const lib = getLibrary();

    if (state.activeKind === "Colours") {
      filterBarEl.hidden = false;
      const q = state.search.trim().toLowerCase();
      const pool = lib.filter(
        (it) =>
          it.Kind === "Colours" &&
          (state.origin === "All" || it.Origin === state.origin) &&
          (`${it.name} ${it.Colour?.Hex || ""} ${it.family || ""}`).toLowerCase().includes(q),
      );
      filterBarEl.append(Construct("span", "colour-filters-title", "Colour Filters"));
      const chipsWrap = Construct("div", "colour-filter-chips");
      chipsWrap.setAttribute("role", "group");
      chipsWrap.setAttribute("aria-label", "Colour filter family");
      for (const cat of ColourFilterCategories) {
        const count = pool.filter((a) => MatchesColourFilter(a.Colour, cat)).length;
        const btn = Construct("button");
        btn.type = "button";
        btn.setAttribute("aria-pressed", String(state.colourFilter === cat));
        btn.append(Construct("span", null, cat), Construct("small", null, String(count)));
        btn.addEventListener("click", () => {
          state.colourFilter = cat;
          renderFilterBar();
          renderResultsAndPreview();
        });
        chipsWrap.append(btn);
      }
      const sortLabel = Construct("label", "colour-sort-select");
      sortLabel.append("Sort ");
      const sortSel = document.createElement("select");
      sortSel.setAttribute("aria-label", "Sort colours");
      for (const m of ColourSortModes) {
        const opt = document.createElement("option");
        opt.value = m.Id;
        opt.textContent = m.Label;
        opt.selected = state.colourSort === m.Id;
        sortSel.append(opt);
      }
      sortSel.addEventListener("change", () => {
        state.colourSort = sortSel.value;
        renderResultsAndPreview();
      });
      sortLabel.append(sortSel);
      filterBarEl.append(chipsWrap, sortLabel);
      return;
    }

    const families = getFamiliesForKind(state.activeKind);
    const attrs = ATTRIBUTE_FILTERS[state.activeKind] || [];
    if (state.activeKind === "All" || (families.length <= 1 && !attrs.length)) {
      filterBarEl.hidden = true;
      return;
    }

    filterBarEl.hidden = false;
    filterBarEl.append(Construct("span", "colour-filters-title", "Collection"));
    const chipsWrap = Construct("div", "colour-filter-chips");
    chipsWrap.setAttribute("role", "group");
    chipsWrap.setAttribute("aria-label", `${state.activeKind} filters`);

    const totalInKind = getItemsForKind(state.activeKind).length;
    const allChip = Construct("button");
    allChip.type = "button";
    allChip.dataset.family = "All";
    allChip.setAttribute("aria-pressed", String(state.activeFamily === "All" && !state.activeFilter));
    allChip.append(Construct("span", null, "All"), Construct("small", null, String(totalInKind)));
    allChip.addEventListener("click", () => {
      state.activeFamily = "All";
      state.activeFilter = null;
      renderFilterBar();
      renderResultsAndPreview();
    });
    chipsWrap.append(allChip);

    for (const fam of families) {
      const btn = Construct("button");
      btn.type = "button";
      btn.dataset.family = fam.name;
      btn.setAttribute("aria-pressed", String(state.activeFamily === fam.name));
      btn.append(Construct("span", null, fam.name), Construct("small", null, String(fam.count)));
      btn.addEventListener("click", () => {
        state.activeFamily = state.activeFamily === fam.name ? "All" : fam.name;
        renderFilterBar();
        renderResultsAndPreview();
      });
      chipsWrap.append(btn);
    }

    if (attrs.length) {
      const basePool = getItemsForKind(state.activeKind).filter(
        (it) => state.activeFamily === "All" || it.family === state.activeFamily,
      );
      for (const f of attrs) {
        const count = basePool.filter(f.test).length;
        const btn = Construct("button", "attr-filter-chip");
        btn.type = "button";
        btn.dataset.filter = f.id;
        btn.setAttribute("aria-pressed", String(state.activeFilter === f.id));
        btn.append(Construct("span", null, f.label), Construct("small", null, String(count)));
        btn.addEventListener("click", () => {
          state.activeFilter = state.activeFilter === f.id ? null : f.id;
          renderFilterBar();
          renderResultsAndPreview();
        });
        chipsWrap.append(btn);
      }
    }

    filterBarEl.append(chipsWrap);
  }

  function renderCategoryRail() {
    railNav.innerHTML = "";
    const lib = getLibrary();
    for (const catName of CATEGORY_ORDER) {
      const count = lib.filter(
        (a) =>
          (catName === "All" || a.Kind === catName) &&
          (state.origin === "All" || a.Origin === state.origin),
      ).length;
      const btn = Construct("button");
      btn.type = "button";
      btn.dataset.category = catName.toLowerCase();
      btn.setAttribute("aria-pressed", String(catName === state.activeKind));
      btn.append(Construct("span", null, catName), Construct("small", null, String(count)));
      btn.addEventListener("click", () => {
        state.activeKind = catName;
        state.activeFamily = "All";
        state.activeFilter = null;
        state.userSpecifiedCategory = true;
        renderAll();
      });
      railNav.append(btn);
    }
    const note = Construct("p");
    note.innerHTML = "Engine files are read-only.<br>Project and imported items stay in this browser.";
    railNav.append(note);
  }

  function beginAssetDrag(e, item) {
    if (e.dataTransfer) {
      e.dataTransfer.effectAllowed = "copy";
      if (item.Kind === "Colours" && item.Colour) {
        PackColourDragData(e, item.Colour);
      } else {
        e.dataTransfer.setData(
          "application/x-frontier-asset",
          JSON.stringify({ assetKind: item.assetKind, id: item.id, name: item.name }),
        );
        e.dataTransfer.setData("text/plain", item.name);
      }
    }
    if (appBridge) appBridge.activeDragAsset = toPayload(item);
    if (item.Kind === "Colours") {
      document.body?.classList?.add("dragging-codex-colour");
    }
    // Dock from full -> split half shelf cleanly on the next frame so the drag ghost captures before sliding down
    if (state.progress > 0.75) {
      const snap = () => {
        if (state.progress > 0.75) settleDrawer(0.46);
      };
      if (typeof requestAnimationFrame === "function") requestAnimationFrame(snap);
      else snap();
    }
  }

  function endAssetDrag() {
    if (appBridge) appBridge.activeDragAsset = null;
    document.body?.classList?.remove("dragging-codex-colour");
  }

  function renderResults() {
    const visible = getFilteredItems();
    const headingText =
      state.activeKind === "All"
        ? "All content"
        : state.activeKind === "Colours" && state.colourFilter !== "All"
          ? `Colours · ${state.colourFilter}`
          : state.activeFamily !== "All"
            ? `${state.activeKind} · ${state.activeFamily}`
            : state.activeKind;
    resultsHeading.textContent = headingText;
    resultsCount.textContent = `${visible.length} ${visible.length === 1 ? "result" : "results"}`;

    catalogueDiv.innerHTML = "";
    const activeLayout = state.activeKind === "Colours" ? state.colourView : state.viewMode;

    // ColourCodex Vertical & Horizontal Spectrum Strips
    if (state.activeKind === "Colours" && (activeLayout === "list-vertical" || activeLayout === "list-horizontal")) {
      catalogueDiv.className = `asset-catalogue colour-strip-deck ${activeLayout}`;
      catalogueDiv.setAttribute(
        "aria-label",
        activeLayout === "list-vertical" ? "Vertical colour strip list" : "Horizontal colour strip list",
      );

      if (!visible.length) {
        catalogueDiv.append(buildEmptyState("No colours match filter", "Choose another Colour Filter or clear search."));
        return;
      }

      if (activeLayout === "list-vertical" && visible[0]?.Colour) {
        const ink = ChooseCardInk(visible[0].Colour.Hex);
        const banner = Construct(
          "span",
          "colour-strip-banner",
          state.colourFilter === "All" ? "ColourCodex Spectrum" : state.colourFilter,
        );
        banner.style.color = ink.text;
        catalogueDiv.append(banner);
      }

      for (const item of visible) {
        const ink = ChooseCardInk(item.Colour.Hex);
        const isSel = state.inspectorItemId === item.id;
        const band = Construct("button", `colour-strip-band${isSel ? " selected" : ""}`);
        band.type = "button";
        band.dataset.assetId = item.id;
        band.dataset.assetKind = item.assetKind;
        band.setAttribute("aria-label", `Select asset ${item.name}`);
        band.setAttribute("aria-pressed", String(isSel));
        band.draggable = true;
        band.style.backgroundColor = item.Colour.Hex;
        band.style.color = ink.text;
        band.title = `${item.name} · ${item.Colour.Hex} (Drag to colour slot or double-click)`;

        if (activeLayout === "list-vertical") {
          band.append(Construct("span", "colour-strip-vertical-hex", item.Colour.CompactHex));
        } else {
          const metaSpan = Construct("span", "colour-strip-horizontal-meta");
          const smallRgb = Construct("small", null, `RGB ${item.Colour.RGB.join(", ")}`);
          smallRgb.style.color = ink.muted;
          metaSpan.append(Construct("b", null, item.Colour.CompactHex), smallRgb);
          band.append(Construct("strong", "colour-strip-horizontal-name", item.name), metaSpan);
        }

        band.addEventListener("dragstart", (e) => beginAssetDrag(e, item));
        band.addEventListener("dragend", endAssetDrag);
        band.addEventListener("click", () => {
          state.inspectorItemId = item.id;
          state.renameDraft = "";
          renderResultsAndPreview();
        });
        band.addEventListener("dblclick", () => {
          state.inspectorItemId = item.id;
          dispatchApplyItem(item);
          renderResultsAndPreview();
        });
        catalogueDiv.append(band);
      }
      return;
    }

    // Standard Project Zero Grid / List Catalogue
    catalogueDiv.className = `asset-catalogue ${activeLayout === "list" ? "list" : "grid"}`;
    catalogueDiv.removeAttribute("aria-label");

    if (!visible.length) {
      catalogueDiv.append(
        buildEmptyState("No content found", "Change filters, switch origin tab, or import files into ContentBrowser."),
      );
      return;
    }

    const displayList = visible.slice(0, 180);
    const current = getSelectedItem(visible);

    for (const item of displayList) {
      const isSel = current?.id === item.id;
      const cell = Construct(
        "button",
        `asset-cell${isSel ? " selected" : ""}${item.Kind === "Colours" ? " colour-asset-cell" : ""}`,
      );
      cell.type = "button";
      cell.dataset.assetId = item.id;
      cell.dataset.assetKind = item.assetKind;
      cell.setAttribute("data-asset-id", item.id);
      cell.setAttribute("data-asset-kind", item.assetKind);
      cell.setAttribute("aria-label", `Select asset ${item.name}`);
      cell.setAttribute("aria-pressed", String(isSel));
      cell.draggable = true;
      cell.title = `${item.name} (${item.family || item.Kind}) · Click to inspect · Double-click or drag to Stage`;

      const thumb = createAssetThumbnailNode(item, activeLayout === "list" ? 37 : 70);
      const nameWrap = Construct("span", "asset-cell-name");
      nameWrap.append(item.name);
      const subText =
        item.Kind === "Colours"
          ? `${item.Colour.Hex} · ${item.Origin}`
          : `${item.family || item.Kind} · ${item.Origin}`;
      nameWrap.append(Construct("small", null, subText));

      cell.append(thumb, nameWrap);

      cell.addEventListener("dragstart", (e) => {
        cell.classList.add("dragging");
        beginAssetDrag(e, item);
      });
      cell.addEventListener("dragend", () => {
        cell.classList.remove("dragging");
        endAssetDrag();
      });
      cell.addEventListener("click", () => {
        state.inspectorItemId = item.id;
        state.renameDraft = "";
        for (const el of catalogueDiv.querySelectorAll(".asset-cell")) {
          if (el.classList.contains("selected")) {
            el.classList.remove("selected");
            el.setAttribute("aria-pressed", "false");
          }
        }
        cell.classList.add("selected");
        cell.setAttribute("aria-pressed", "true");
        renderPreviewAside();
      });
      cell.addEventListener("dblclick", () => {
        state.inspectorItemId = item.id;
        dispatchApplyItem(item);
        renderPreviewAside();
      });

      catalogueDiv.append(cell);
    }
  }

  function buildEmptyState(title, copy) {
    const empty = Construct("div", "asset-empty");
    const resetBtn = Construct("button", null, "Clear filters");
    resetBtn.type = "button";
    resetBtn.addEventListener("click", () => {
      state.search = "";
      searchInput.value = "";
      searchClearBtn.hidden = true;
      state.origin = "All";
      state.activeFamily = "All";
      state.activeFilter = null;
      state.colourFilter = "All";
      renderAll();
    });
    empty.append(Construct("h3", null, title), Construct("p", null, copy), resetBtn);
    return empty;
  }

  function getSelectedItem(visible = getFilteredItems()) {
    const lib = getLibrary();
    return (
      lib.find((it) => it.id === state.inspectorItemId) ||
      visible[0] ||
      getItemsForKind(state.activeKind)[0] ||
      null
    );
  }

  function buildMetadataPairs(item) {
    if (item.Kind === "Colours" && item.Colour) {
      const slot = GetActiveColourSlot();
      return [
        ["Type", "ColourCodex"],
        ["Filter", item.Colour.FilterGroup],
        ["Target slot", slot?.Label || "Primary colour"],
      ];
    }
    if (item.Kind === "Materials") {
      const m = item.raw || {};
      const rough = Number(m.roughness ?? 0.45);
      const metal = Number(m.metalness ?? 0);
      const coat = Number(m.coat ?? 0);
      return [
        ["Type", "PBR Material"],
        ["Collection", m.category || item.family],
        ["Roughness", rough.toFixed(2)],
        ["Metalness", `${Math.round(metal * 100)}%`],
        ["Clearcoat", coat > 0.01 ? coat.toFixed(2) : "None"],
        ["Kernel", m.shaderFamily || "Alloy PBR"],
        ["Owner", item.Origin === "Engine" ? "Engine library" : "Browser project"],
      ];
    }
    if (item.Kind === "Patterns" || item.Kind === "Stamps") {
      const meta = item.meta || summarizeArtworkEntry(item.raw);
      return [
        ["Type", item.Kind === "Patterns" ? "Vector Pattern" : "Decal Stamp"],
        ["Collection", item.family],
        ["Layers", String(meta.layers)],
        ["Canvas", `${meta.width} × ${meta.height}`],
        ["Finish", meta.primaryFinish.toUpperCase()],
        ["Modifiers", String(meta.modifiers)],
        ["Owner", item.Origin === "Engine" ? "Engine library" : "Browser project"],
      ];
    }
    if (item.Kind === "Meshes") {
      const mesh = item.raw || {};
      return [
        ["Type", "3D Study Mesh"],
        ["Collection", mesh.family || item.family],
        ["Topology", mesh.topo || "Tri/Quad"],
        ["Triangles", mesh.complexity || "—"],
        ["Bake Focus", mesh.role || "Tangent Space"],
        ["Owner", item.Origin === "Engine" ? "Engine library" : "Browser project"],
      ];
    }
    const pairs = [
      ["Type", item.Kind],
      ["Owner", item.Origin === "Engine" ? "Engine library" : "Browser project"],
    ];
    if (item.Width && item.Height) pairs.push(["Dimensions", `${item.Width} × ${item.Height}`]);
    if (item.Size !== undefined) pairs.push(["File size", `${(item.Size / 1024).toFixed(1)} KiB`]);
    return pairs;
  }

  function renderPreviewAside() {
    previewAside.innerHTML = "";
    const current = getSelectedItem();
    if (!current) {
      const empty = Construct("div", "asset-empty");
      empty.append(Construct("h3", null, "Select content"), Construct("p", null, "Inspect its source and preview here."));
      previewAside.append(empty);
      return;
    }
    state.inspectorItemId = current.id;

    const heading = Construct("div", "asset-preview-heading");
    heading.append(
      Construct("span", "asset-eyebrow", "CONTENT DETAILS"),
      Construct("span", "asset-origin-label", current.Origin),
    );
    previewAside.append(heading);

    if (current.Kind === "Colours" && current.Colour) {
      const ink = ChooseCardInk(current.Colour.Hex);
      state.hexDraft = current.Colour.Hex;
      const swatchCard = Construct("div", "codex-swatch-card");
      swatchCard.setAttribute("data-contrast", ink.contrastLabel);
      swatchCard.draggable = true;
      swatchCard.style.backgroundColor = current.Colour.Hex;
      swatchCard.style.color = ink.text;
      swatchCard.style.borderColor = ink.border;
      swatchCard.title = "Drag onto any colour slot or double-click to apply";

      const titleDiv = Construct("div", "codex-card-title");
      for (const line of SplitCardHeading(current.name)) {
        titleDiv.append(Construct("span", null, line));
      }
      const specsDiv = Construct("div", "codex-card-specs");
      specsDiv.style.color = ink.muted;
      const makeSpecRow = (k, v) => {
        const d = Construct("div");
        const b = Construct("b", null, k);
        b.style.color = ink.text;
        d.append(b, " ", v);
        return d;
      };
      specsDiv.append(
        makeSpecRow("HEX:", current.Colour.Hex),
        makeSpecRow("RGB:", current.Colour.RGB.join(", ")),
        makeSpecRow("CMYK:", current.Colour.CMYK.join(", ")),
      );
      swatchCard.append(titleDiv, specsDiv);

      swatchCard.addEventListener("dragstart", (e) => beginAssetDrag(e, current));
      swatchCard.addEventListener("dragend", endAssetDrag);
      swatchCard.addEventListener("dblclick", () => dispatchApplyItem(current));
      previewAside.append(swatchCard);
    } else {
      previewAside.append(Construct("h2", null, current.name));
      const previewBox = Construct("div", "asset-file-preview");
      const thumbNode = createAssetThumbnailNode(current, 128);
      thumbNode.classList.add("asset-preview-hero-thumb");
      previewBox.append(thumbNode);
      previewAside.append(previewBox);
    }

    if (current.detail) {
      previewAside.append(Construct("p", null, current.detail));
    }

    const dl = Construct("dl");
    for (const [dtText, ddText] of buildMetadataPairs(current)) {
      dl.append(Construct("dt", null, dtText), Construct("dd", null, ddText));
    }
    previewAside.append(dl);

    // Editable colour hex controls for Project/Imported colours
    if (current.Kind === "Colours" && current.Origin !== "Engine") {
      const hexEditWrap = Construct("div", "codex-hex-edit");
      const lbl = Construct("label", null, "Edit colour");
      const ctrlRow = Construct("div", "codex-hex-controls");

      const colorInp = document.createElement("input");
      colorInp.type = "color";
      colorInp.setAttribute("aria-label", "Edit codex colour picker");
      colorInp.value = current.Colour.Hex.toLowerCase();

      const hexInp = document.createElement("input");
      hexInp.type = "text";
      hexInp.maxLength = 7;
      hexInp.setAttribute("aria-label", "Edit codex colour hex");
      hexInp.value = current.Colour.Hex;

      const updateHex = (rawHex) => {
        const clean = NormalizeHex(rawHex, current.Colour.Hex);
        const nextColour = ResolveColourEntry({ ...current.Colour, Name: current.name, Hex: clean });
        userAssets = userAssets.map((a) => (a.id === current.id ? { ...a, Colour: nextColour } : a));
        persistSavedRecords(userAssets);
        renderAll();
      };
      colorInp.addEventListener("change", () => updateHex(colorInp.value));
      hexInp.addEventListener("blur", () => updateHex(hexInp.value));
      hexInp.addEventListener("keydown", (e) => { if (e.key === "Enter") hexInp.blur?.(); });

      ctrlRow.append(colorInp, hexInp);
      lbl.append(ctrlRow);
      hexEditWrap.append(lbl);
      previewAside.append(hexEditWrap);
    }

    // Primary & contextual action buttons
    const matActions = Construct("div", "asset-material-actions");
    if (current.Kind === "Colours") {
      const slot = GetActiveColourSlot();
      const applyColBtn = Construct(
        "button",
        "primary",
        `Apply to ${slot?.Label || "colour slot"}`,
      );
      applyColBtn.type = "button";
      applyColBtn.setAttribute("aria-label", "Apply colour to active slot");
      applyColBtn.addEventListener("click", () => dispatchApplyItem(current));

      const exportAllBtn = Construct("button", null, "Export ColourCodex.toml");
      exportAllBtn.type = "button";
      exportAllBtn.addEventListener("click", () => {
        const allCols = getLibrary().filter((a) => a.Kind === "Colours");
        triggerBlobDownload(
          new Blob([SerializeColourCodexToml(allCols.map((c) => c.Colour))], {
            type: "application/toml;charset=utf-8",
          }),
          "ColourCodex.toml",
        );
        setStatus(`Exported ${allCols.length} colours to ColourCodex.toml`);
      });
      matActions.append(applyColBtn, exportAllBtn);
    } else {
      const applyBtn = Construct("button", "primary", "Apply to Stage");
      applyBtn.type = "button";
      applyBtn.addEventListener("click", () => {
        dispatchApplyItem(current);
        if (onWorkspaceRoute) {
          if (current.assetKind === "material") onWorkspaceRoute("material");
          else if (current.assetKind === "pattern") onWorkspaceRoute("pattern");
          else if (current.assetKind === "stamp") onWorkspaceRoute("stamp");
          else if (current.assetKind === "mesh") onWorkspaceRoute("bake");
        }
      });
      matActions.append(applyBtn);

      if (current.Kind === "Patterns") {
        const tileBtn = Construct("button", null, "Tile on Material");
        tileBtn.type = "button";
        tileBtn.addEventListener("click", () => {
          onWorkspaceRoute?.("material");
          dispatchApplyItem(current, { targetWorkspace: "material" });
          Toast(`Tiled ${current.name} onto Material`);
        });
        matActions.append(tileBtn);
      }
      if (current.Kind === "Stamps" || current.Kind === "Patterns") {
        const paintBtn = Construct("button", null, "Send to Paint");
        paintBtn.type = "button";
        paintBtn.addEventListener("click", () => {
          onWorkspaceRoute?.("paint");
          dispatchApplyItem(current, { targetWorkspace: "paint" });
          Toast(`${current.name} sent to Texture Paint`);
        });
        matActions.append(paintBtn);
      }
      if (current.Kind === "Images") {
        const tileImgBtn = Construct("button", null, "Tile on Material");
        tileImgBtn.type = "button";
        tileImgBtn.addEventListener("click", () => {
          appBridge?.applyAssetToActiveWorkspace?.(toPayload(current), { role: "tile" });
        });
        matActions.append(tileImgBtn);
      }
    }
    previewAside.append(matActions);

    // File actions (Download + Copy to Project / Remove)
    const fileActions = Construct("div", "asset-file-actions");
    const dlBtn = Construct(
      "button",
      null,
      current.Kind === "Colours"
        ? "Download .toml"
        : current.Kind === "Materials"
          ? "Download material"
          : "Download file",
    );
    dlBtn.type = "button";
    dlBtn.addEventListener("click", () => downloadAssetItem(current));
    fileActions.append(dlBtn);

    if (current.Kind === "Colours" && current.Origin === "Engine") {
      const copyColBtn = Construct("button", null, "Copy to Project");
      copyColBtn.type = "button";
      copyColBtn.addEventListener("click", () =>
        createProjectColour(current.Colour.Hex, `${current.name} copy`),
      );
      fileActions.append(copyColBtn);
    } else if (current.Kind === "Materials" && current.Origin === "Engine") {
      const copyMatBtn = Construct("button", null, "Copy to Project");
      copyMatBtn.type = "button";
      copyMatBtn.addEventListener("click", () => {
        if (userAssets.length >= 256) return;
        const copyRec = {
          id: "asset-" + makeUuid(),
          name: `${current.name} copy`,
          Kind: "Materials",
          Origin: "Project",
          family: "Project",
          assetKind: "material",
          raw: { ...structuredClone(current.raw), id: `proj-${makeUuid().slice(0, 8)}`, name: `${current.name} copy`, category: "Project" },
          detail: "Project copy of engine material preset.",
        };
        userAssets = [copyRec, ...userAssets];
        persistSavedRecords(userAssets);
        state.origin = "Project";
        state.inspectorItemId = copyRec.id;
        setStatus(`Copied "${current.name}" to Project`);
        renderAll();
      });
      fileActions.append(copyMatBtn);
    } else if (current.Origin !== "Engine") {
      const remBtn = Construct("button", null, "Remove asset");
      remBtn.type = "button";
      remBtn.addEventListener("click", () => removeAsset(current));
      fileActions.append(remBtn);
    }
    previewAside.append(fileActions);

    // Rename form for Project/Imported assets
    if (current.Origin !== "Engine") {
      const form = document.createElement("form");
      form.className = "asset-rename";
      const rInp = document.createElement("input");
      rInp.type = "text";
      rInp.maxLength = 100;
      rInp.setAttribute("aria-label", "Asset name");
      rInp.placeholder = current.name;
      rInp.value = state.renameDraft;
      const rBtn = Construct("button", null, "Rename");
      rBtn.type = "submit";
      rBtn.disabled = !state.renameDraft.trim();
      rInp.addEventListener("input", () => {
        state.renameDraft = rInp.value;
        rBtn.disabled = !state.renameDraft.trim();
      });
      form.addEventListener("submit", (e) => {
        e.preventDefault?.();
        const nextName = state.renameDraft.trim();
        if (!nextName) return;
        userAssets = userAssets.map((a) => {
          if (a.id !== current.id) return a;
          return {
            ...a,
            name: nextName,
            ...(a.raw ? { raw: { ...a.raw, name: nextName } } : {}),
            ...(a.Colour ? { Colour: ResolveColourEntry({ ...a.Colour, Name: nextName }) } : {}),
          };
        });
        persistSavedRecords(userAssets);
        state.renameDraft = "";
        setStatus(`Renamed to "${nextName}"`);
        renderAll();
      });
      form.append(rInp, rBtn);
      previewAside.append(form);
    }
  }

  function renderResultsAndPreview() {
    renderResults();
    renderPreviewAside();
  }

  function renderAll() {
    renderHeaderControls();
    renderFilterBar();
    renderCategoryRail();
    renderResultsAndPreview();
    updateFooter();
  }

  // Wire header controls
  autoFollowBtn.addEventListener("click", () => {
    state.userSpecifiedCategory = false;
    state.activeKind = WORKSPACE_TO_KIND[state.activeWorkspace] || "Materials";
    state.activeFamily = "All";
    state.activeFilter = null;
    renderAll();
  });
  addMatBtn.addEventListener("click", () => createProjectMaterial());
  addColourBtn.addEventListener("click", () => createProjectColour());
  exportTomlBtn.addEventListener("click", () => {
    const allCols = getLibrary().filter((a) => a.Kind === "Colours");
    triggerBlobDownload(
      new Blob([SerializeColourCodexToml(allCols.map((c) => c.Colour))], {
        type: "application/toml;charset=utf-8",
      }),
      "ColourCodex.toml",
    );
    setStatus(`Exported ${allCols.length} colours to ColourCodex.toml`);
  });
  importBtn.addEventListener("click", () => fileInput.click?.());
  fileInput.addEventListener("change", () => {
    const chosen = [...(fileInput.files || [])];
    fileInput.value = "";
    importFiles(chosen);
  });
  splitToggleBtn.addEventListener("click", () => {
    const isHalf = state.progress > 0 && state.progress < 0.99;
    settleDrawer(isHalf ? 1 : 0.46);
  });
  closeBtn.addEventListener("click", () => closeDrawer());

  // Initial draw (starts cleanly closed with the centered ContentBrowser notch visible)
  applyDrawerGeometry();
  renderAll();

  function syncWorkspace(wsId) {
    state.activeWorkspace = wsId;
    if (!state.userSpecifiedCategory) {
      const nextKind = WORKSPACE_TO_KIND[wsId] || "Materials";
      if (nextKind !== state.activeKind) {
        state.activeKind = nextKind;
        state.activeFamily = "All";
        state.activeFilter = null;
      }
    }
    renderAll();
  }

  return {
    element: drawerShell,
    browserRoot,
    notchHandle,
    syncWorkspace,
    setActiveWorkspace: syncWorkspace,
    open(stageOrProgress = 1) {
      openDrawer(stageOrProgress === "half" ? 0.46 : Number(stageOrProgress) || 1);
    },
    close() {
      closeDrawer();
    },
    setStage(stageName) {
      if (stageName === "closed") closeDrawer();
      else if (stageName === "half") openDrawer(0.46);
      else openDrawer(1);
    },
    getAssetPayload(kind, id) {
      const mappedKind = kind ? normalizeKindName(kind) : state.activeKind;
      const item = findItemById(id, mappedKind);
      return toPayload(item);
    },
    setCategory(catId, opts = {}) {
      const userPinned = opts.userPinned !== undefined ? opts.userPinned : (opts.userSpecified ?? true);
      state.activeKind = normalizeKindName(catId);
      state.activeFamily = opts.family || "All";
      state.activeFilter = null;
      state.userSpecifiedCategory = Boolean(userPinned);
      if (opts.open !== false && !state.open) {
        openDrawer(0.46);
      }
      renderAll();
    },
    selectAsset(kind, id) {
      const mappedKind = kind ? normalizeKindName(kind) : state.activeKind;
      const item = findItemById(id, mappedKind);
      if (!item) return null;
      state.activeKind = item.Kind;
      state.activeFamily = "All";
      state.inspectorItemId = item.id;
      if (!state.open) openDrawer(0.46);
      renderAll();
      return item;
    },
    setViewMode(mode) {
      if (state.activeKind === "Colours" && ["grid", "list-vertical", "list-horizontal"].includes(mode)) {
        state.colourView = mode;
      } else if (mode === "grid" || mode === "list") {
        state.viewMode = mode;
      }
      renderHeaderControls();
      renderResults();
    },
    getState() {
      return {
        open: state.open,
        stage: state.progress === 0 ? "closed" : state.progress >= 0.99 ? "full" : "half",
        progress: state.progress,
        offsetPx: state.offsetPx,
        origin: state.origin,
        activeKind: state.activeKind,
        activeCategory: state.activeKind.toLowerCase(),
        activeFamily: state.activeFamily,
        activeFilter: state.activeFilter,
        search: state.search,
        viewMode: state.viewMode,
        colourView: state.colourView,
        colourFilter: state.colourFilter,
        userSpecifiedCategory: state.userSpecifiedCategory,
        activeWorkspace: state.activeWorkspace,
        collapsed: !state.open,
        inspectorItemId: state.inspectorItemId,
      };
    },
    setCollapsed(v) {
      if (v) closeDrawer();
      else openDrawer(1);
    },
    importFiles,
    createProjectColour,
    createProjectMaterial,
  };
}
