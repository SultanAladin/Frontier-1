/*==========================================================================================================================================
                                                            ASSETBROWSER.JS
==========================================================================================================================================*/
/* Floating Pull-Up Asset Browser Overlay for Frontier Texture Studio:
   - Anchored as a bottom pull-up overlay above .status-bar so opening/resizing NEVER shrinks the workspace viewport
   - 3-Pane architecture:
       [Left: Hierarchical TreeView Categories / Folders / Attribute Filters]
       [Center: Grid / List Switchable Assets — 1:1 square cards with NO thumbnail background box, transparent baked icon + name below]
       [Right: Tile-Based Asset Inspector matching Material Inspector MetricTrio tiles]
   - Pre-baked transparent 3D PBR Material Sphere PNGs via MaterialIconBaker.js
   - Workspace-aware automatic category switching unless pinned by user */

import { Construct, ConstructGlyph, Toast } from "./StudioControls.js";
import { materials } from "./Engine/Materials/materials.js";
import { getBakedMaterialIcon } from "./MaterialIconBaker.js";
import {
  listPatternStarterCatalog,
  listStampStarterCatalog,
  getArtworkPreviewSvg,
} from "./Engine/Patterns/StarterCatalogBridge.js";

const WORKSPACE_TO_CATEGORY = {
  material: "materials",
  pattern: "patterns",
  stamp: "stamps",
  paint: "stamps",
  bake: "meshes",
};

const MESH_PRESETS = [
  { id: "teapot", name: "Studio Teapot", family: "Standard Study", desc: "Classic curved bevel & spout normals", topo: "High-Poly Tri", complexity: "6.4k tris", role: "Curvature & AO" },
  { id: "chamfer", name: "Hard-Surface Bevel", family: "Hard Surface", desc: "Machined chamfer plates & crisp edges", topo: "Bevel Quad/Tri", complexity: "3.2k tris", role: "Edge Wear" },
  { id: "helmet", name: "Visor Armor Study", family: "Hard Surface", desc: "Compound dome visor & neck flange", topo: "Sub-D Hull", complexity: "8.1k tris", role: "Anisotropic Spec" },
  { id: "torus", name: "Torus Knot Study", family: "Organic", desc: "Continuous braided curvature study", topo: "Procedural Loop", complexity: "5.8k tris", role: "Thin-Film & Sheen" },
  { id: "shaderball", name: "Frontier Shader Orb", family: "Standard Study", desc: "Nested cutaway studio orb", topo: "Cutaway Multi", complexity: "7.2k tris", role: "SSS & Clearcoat" },
];

const CATEGORY_LABELS = {
  materials: "Materials",
  patterns: "Patterns",
  stamps: "Decal Stamps",
  meshes: "3D Meshes",
};

const CATEGORY_FILTERS = {
  materials: [
    { id: "metallic", label: "Metallic", test: (it) => (it.raw.metalness ?? 0) >= 0.45 },
    { id: "clearcoat", label: "Clearcoat", test: (it) => (it.raw.coat ?? 0) >= 0.15 },
    { id: "emissive", label: "Emissive", test: (it) => (it.raw.emissionStrength ?? 0) > 0.05 },
    { id: "iridescent", label: "Iridescent", test: (it) => (it.raw.iridescence ?? 0) > 0.05 },
  ],
  patterns: [
    { id: "noise", label: "Tileable Noise", test: (it) => it.id.startsWith("noise-") },
    { id: "african", label: "African Heritage", test: (it) => it.family === "African Patterns" },
    { id: "multilayer", label: "Multi-Layer", test: (it) => (it.meta?.layers || 1) >= 2 },
  ],
  stamps: [
    { id: "hardsurface", label: "Hard Surface", test: (it) => it.family === "Hard Surface" },
    { id: "metallic-finish", label: "Metallic Foil", test: (it) => (it.meta?.finishes || []).includes("foil") },
    { id: "stencil-text", label: "Stencil Text", test: (it) => Boolean(it.meta?.hasText) },
  ],
  meshes: [
    { id: "hard-surface-mesh", label: "Hard Surface", test: (it) => it.family === "Hard Surface" },
    { id: "organic-mesh", label: "Organic & Study", test: (it) => it.family !== "Hard Surface" },
  ],
};

function makeMatIconSvg(item, size = 64) {
  const m = item.raw;
  const col = m.color || "#888";
  const sec = m.secondaryColor || m.colors?.[0] || "#ffffff";
  const rough = Math.max(0.06, m.roughness ?? 0.4);
  const metal = m.metalness ?? 0;
  const uid = `mat-${String(item.id || "x").replace(/[^a-zA-Z0-9_-]/g, "")}`;
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
  const accent = "#eff4cf";
  const stroke = "#b7c1cf";
  if (meshId === "torus") {
    return `<svg width="${size}" height="${size}" viewBox="0 0 64 64" fill="none"><ellipse cx="32" cy="32" rx="22" ry="12" stroke="${stroke}" stroke-width="4" transform="rotate(-25 32 32)"/><ellipse cx="32" cy="32" rx="22" ry="12" stroke="${accent}" stroke-width="2.5" transform="rotate(28 32 32)"/><circle cx="32" cy="32" r="5.5" stroke="${stroke}" stroke-width="2"/></svg>`;
  }
  if (meshId === "chamfer") {
    return `<svg width="${size}" height="${size}" viewBox="0 0 64 64" fill="none"><path d="M18 12h28l6 6v28l-6 6H18l-6-6V18z" fill="rgba(255,255,255,0.06)" stroke="${stroke}" stroke-width="2.6"/><path d="M22 22h20v20H22z" stroke="${accent}" stroke-width="1.8" opacity="0.85"/><path d="M12 18l10 4M52 18l-10 4M12 46l10-4M52 46l-10-4" stroke="${stroke}" stroke-width="1.4" opacity="0.5"/></svg>`;
  }
  if (meshId === "helmet") {
    return `<svg width="${size}" height="${size}" viewBox="0 0 64 64" fill="none"><path d="M16 33c0-10.5 7-18 16-18s16 7.5 16 18v12l-6 4H22l-6-4V33z" fill="rgba(255,255,255,0.06)" stroke="${stroke}" stroke-width="2.4"/><path d="M21 29h22l-3 9H24z" fill="rgba(239,244,207,0.22)" stroke="${accent}" stroke-width="1.8"/></svg>`;
  }
  if (meshId === "shaderball") {
    return `<svg width="${size}" height="${size}" viewBox="0 0 64 64" fill="none"><circle cx="32" cy="32" r="20" fill="rgba(255,255,255,0.06)" stroke="${stroke}" stroke-width="2.4"/><path d="M22 22a14 14 0 0 1 20 0v20a14 14 0 0 1-20 0z" stroke="${accent}" stroke-width="1.6" opacity="0.85"/><ellipse cx="32" cy="32" rx="20" ry="7" stroke="${stroke}" stroke-width="1.4" opacity="0.5"/></svg>`;
  }
  return `<svg width="${size}" height="${size}" viewBox="0 0 64 64" fill="none"><path d="M32 11l19 10.5v21L32 53 13 42.5v-21z" fill="rgba(255,255,255,0.06)" stroke="${stroke}" stroke-width="2.4"/><path d="M32 32l19-10.5M32 32v21M32 32L13 21.5" stroke="${accent}" stroke-width="1.8"/></svg>`;
}

function summarizeArtworkEntry(entry) {
  let doc = null;
  try {
    doc = typeof entry.build === "function" ? entry.build() : null;
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

export function createAssetBrowser(hostOrOpts, maybeBridge) {
  const hostEl = hostOrOpts && typeof hostOrOpts.append === "function" ? hostOrOpts : null;
  const appBridge = maybeBridge || (!hostEl ? hostOrOpts?.appBridge : null);
  const onSelectAsset = !hostEl ? hostOrOpts?.onSelectAsset : null;
  const onWorkspaceRoute = !hostEl ? hostOrOpts?.onWorkspaceRoute : null;

  const root = Construct("aside", "asset-browser");
  root.setAttribute("role", "region");
  root.setAttribute("aria-label", "Asset Browser");
  if (hostEl) hostEl.append(root);

  function toPayload(item) {
    if (!item) return null;
    return {
      assetKind: item.kind,
      id: item.id,
      name: item.name,
      family: item.family,
      raw: item.raw,
    };
  }

  function dispatchApplyItem(item, opts = {}) {
    if (!item) return false;
    if (opts.targetWorkspace === "paint") {
      if (item.kind === "stamp" && appBridge?.openStampAssetInPaint) {
        appBridge.openStampAssetInPaint(item.raw);
        return true;
      }
      if (item.kind === "pattern" && appBridge?.openPatternAssetInPaint) {
        appBridge.openPatternAssetInPaint(item.raw);
        return true;
      }
    }
    if (opts.targetWorkspace === "material" && item.kind === "pattern" && appBridge?.sendPatternToMaterial) {
      appBridge.sendPatternToMaterial(item.raw);
      return true;
    }
    if (onSelectAsset) {
      onSelectAsset(item, opts);
      return true;
    }
    return Boolean(appBridge?.applyAssetToActiveWorkspace?.(toPayload(item)));
  }

  const patternCatalog = listPatternStarterCatalog().map((p) => ({
    id: p.id,
    name: p.name,
    family: p.group || "Patterns",
    kind: "pattern",
    raw: p,
    meta: summarizeArtworkEntry(p),
  }));
  const stampCatalog = listStampStarterCatalog().map((s) => ({
    id: s.id,
    name: s.name,
    family: s.group || "Technical Decals",
    kind: "stamp",
    raw: s,
    meta: summarizeArtworkEntry(s),
  }));
  const materialCatalog = materials.map((m) => ({
    id: m.id,
    name: m.name,
    family: m.category || "Materials",
    kind: "material",
    raw: m,
  }));
  const meshCatalog = MESH_PRESETS.map((m) => ({
    id: m.id,
    name: m.name,
    family: m.family,
    kind: "mesh",
    raw: m,
  }));

  const state = {
    activeCategory: "materials",
    activeFamily: "All",
    activeFilter: null,
    search: "",
    viewMode: "grid",
    userSpecifiedCategory: false,
    activeWorkspace: "material",
    collapsed: false,
    heightPx: 356,
    inspectorItemId: materialCatalog[0]?.id || null,
    expandedCats: new Set(["materials"]),
  };

  function getItems(cat = state.activeCategory) {
    if (cat === "materials") return materialCatalog;
    if (cat === "patterns") return patternCatalog;
    if (cat === "stamps") return stampCatalog;
    if (cat === "meshes") return meshCatalog;
    return materialCatalog;
  }

  function getFamiliesFor(cat) {
    const items = getItems(cat);
    const counts = new Map();
    for (const it of items) {
      counts.set(it.family, (counts.get(it.family) || 0) + 1);
    }
    return [...counts.entries()].map(([name, count]) => ({ name, count }));
  }

  function getFilteredItems() {
    const items = getItems();
    const q = state.search.trim().toLowerCase();
    const activeFilterDef = (CATEGORY_FILTERS[state.activeCategory] || []).find((f) => f.id === state.activeFilter);
    return items.filter((it) => {
      if (state.activeFamily !== "All" && it.family !== state.activeFamily) return false;
      if (activeFilterDef && !activeFilterDef.test(it)) return false;
      if (q && !it.name.toLowerCase().includes(q) && !it.family.toLowerCase().includes(q)) return false;
      return true;
    });
  }

  function findItemById(id, cat = state.activeCategory) {
    return getItems(cat).find((it) => it.id === id)
      || [...materialCatalog, ...patternCatalog, ...stampCatalog, ...meshCatalog].find((it) => it.id === id)
      || null;
  }

  // ---- Top Resize Drag Handle ----
  const resizeNotch = Construct("div", "cb-resize");
  resizeNotch.title = "Drag to resize Asset Browser overlay · Double-click to collapse/expand";
  const notchPill = Construct("span", "cb-resize-pill");
  resizeNotch.append(notchPill);

  // ---- Header Bar ----
  const header = Construct("div", "cb-head");
  const headLeft = Construct("div", "cb-head-left");
  const titleWrap = Construct("div", "cb-title-wrap");
  titleWrap.append(ConstructGlyph("Folder", "glyph cb-head-glyph"), Construct("span", "cb-title", "Asset Browser"));
  const autoBadge = Construct("button", "cb-auto-pill");
  autoBadge.type = "button";
  const countBadge = Construct("span", "cb-count-badge", "0");
  headLeft.append(titleWrap, autoBadge);

  const catTabsWrap = Construct("div", "cb-cat-tabs");
  const catDefs = [
    { id: "materials", label: "Materials", glyph: "Material" },
    { id: "patterns", label: "Patterns", glyph: "Pattern" },
    { id: "stamps", label: "Decal Stamps", glyph: "Stamp" },
    { id: "meshes", label: "3D Meshes", glyph: "Cube" },
  ];
  const catBtnMap = new Map();
  for (const c of catDefs) {
    const btn = Construct("button", "cb-cat-tab");
    btn.type = "button";
    btn.dataset.category = c.id;
    btn.setAttribute("data-category", c.id);
    btn.append(ConstructGlyph(c.glyph, "glyph"), Construct("span", null, c.label));
    btn.addEventListener("click", () => {
      state.activeCategory = c.id;
      state.activeFamily = "All";
      state.activeFilter = null;
      state.userSpecifiedCategory = true;
      state.expandedCats = new Set([c.id]);
      if (state.collapsed) {
        state.collapsed = false;
        applyHeight();
      }
      render();
    });
    catBtnMap.set(c.id, btn);
    catTabsWrap.append(btn);
  }

  const headRight = Construct("div", "cb-head-right");
  const searchWrap = Construct("div", "cb-search-wrap");
  searchWrap.append(ConstructGlyph("Search", "glyph cb-search-icon"));
  const searchInp = Construct("input", "cb-search");
  searchInp.type = "search";
  searchInp.placeholder = "Search materials...";
  searchInp.addEventListener("input", () => {
    state.search = searchInp.value;
    renderCenterAndInspector();
  });
  searchWrap.append(searchInp);

  // Grid / List View Switcher
  const viewToggle = Construct("div", "cb-view-toggle");
  viewToggle.setAttribute("role", "group");
  viewToggle.setAttribute("aria-label", "Asset View Mode");
  const gridViewBtn = Construct("button", "cb-view-btn");
  gridViewBtn.type = "button";
  gridViewBtn.dataset.view = "grid";
  gridViewBtn.setAttribute("data-view", "grid");
  gridViewBtn.title = "Square Grid View";
  gridViewBtn.append(ConstructGlyph("Grid", "glyph"), Construct("span", null, "Grid"));
  const listViewBtn = Construct("button", "cb-view-btn");
  listViewBtn.type = "button";
  listViewBtn.dataset.view = "list";
  listViewBtn.setAttribute("data-view", "list");
  listViewBtn.title = "Compact List View";
  listViewBtn.append(ConstructGlyph("List", "glyph"), Construct("span", null, "List"));

  gridViewBtn.addEventListener("click", () => {
    state.viewMode = "grid";
    renderViewButtons();
    renderGrid();
  });
  listViewBtn.addEventListener("click", () => {
    state.viewMode = "list";
    renderViewButtons();
    renderGrid();
  });
  viewToggle.append(gridViewBtn, listViewBtn);

  const collapseBtn = Construct("button", "cb-collapse-btn");
  collapseBtn.type = "button";
  collapseBtn.title = "Collapse / Expand Asset Browser Overlay";
  collapseBtn.append(ConstructGlyph("ChevronDown", "glyph"));
  collapseBtn.addEventListener("click", () => {
    state.collapsed = !state.collapsed;
    applyHeight();
  });

  headRight.append(searchWrap, viewToggle, collapseBtn);
  header.append(headLeft, catTabsWrap, headRight);

  autoBadge.addEventListener("click", () => {
    state.userSpecifiedCategory = false;
    state.activeCategory = WORKSPACE_TO_CATEGORY[state.activeWorkspace] || "materials";
    state.activeFamily = "All";
    state.activeFilter = null;
    state.expandedCats = new Set([state.activeCategory]);
    render();
  });

  // ---- Body: 3 Panes [Left TreeView/Folders/Filters | Center Square Grid/List | Right Tile Inspector] ----
  const body = Construct("div", "cb-body");
  const treePane = Construct("aside", "cb-tree-pane");
  const centerPane = Construct("div", "cb-center-pane");
  const familyPillsStrip = Construct("div", "cb-family-pills");
  const collectionBar = Construct("div", "cb-collection-bar");
  const collectionTitle = Construct("span", "cb-collection-title", "THE ESSENTIAL COLLECTION");
  const collectionCount = Construct("span", "cb-collection-count", "0");
  collectionBar.append(collectionTitle, collectionCount);
  const gridScroll = Construct("div", "cb-grid-scroll");
  const grid = Construct("div", "cb-grid");
  gridScroll.append(grid);
  centerPane.append(familyPillsStrip, collectionBar, gridScroll);
  const inspectorPane = Construct("aside", "cb-inspector");
  body.append(treePane, centerPane, inspectorPane);
  root.append(resizeNotch, header, body);

  const clearNode = (el, ...nodes) => {
    el.innerHTML = "";
    if (nodes.length) el.append(...nodes);
  };

  function applyHeight() {
    const h = state.collapsed ? 38 : state.heightPx;
    root.style.height = `${h}px`;
    root.classList.toggle("collapsed", state.collapsed);
    clearNode(collapseBtn, ConstructGlyph(state.collapsed ? "ChevronUp" : "ChevronDown", "glyph"));
    // Intentionally does NOT call window.resize or touch .studio-main so the viewport never resizes.
  }

  let dragStartY = 0;
  let dragStartH = 326;
  let dragging = false;
  const onPointerMove = (e) => {
    if (!dragging) return;
    const dy = dragStartY - e.clientY;
    const maxH = typeof window !== "undefined" && window.innerHeight ? Math.min(680, Math.floor(window.innerHeight * 0.72)) : 560;
    state.heightPx = Math.max(196, Math.min(maxH, Math.round(dragStartH + dy)));
    if (state.collapsed && state.heightPx > 120) state.collapsed = false;
    applyHeight();
  };
  const onPointerUp = () => {
    if (!dragging) return;
    dragging = false;
    root.classList.remove("resizing");
    window.removeEventListener("pointermove", onPointerMove);
    window.removeEventListener("pointerup", onPointerUp);
  };
  resizeNotch.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    dragging = true;
    dragStartY = e.clientY;
    dragStartH = state.collapsed ? 38 : state.heightPx;
    root.classList.add("resizing");
    window.addEventListener("pointermove", onPointerMove);
    window.addEventListener("pointerup", onPointerUp);
  });
  resizeNotch.addEventListener("dblclick", () => {
    state.collapsed = !state.collapsed;
    applyHeight();
  });

  function renderViewButtons() {
    gridViewBtn.classList.toggle("active", state.viewMode === "grid");
    gridViewBtn.setAttribute("aria-pressed", state.viewMode === "grid" ? "true" : "false");
    listViewBtn.classList.toggle("active", state.viewMode === "list");
    listViewBtn.setAttribute("aria-pressed", state.viewMode === "list" ? "true" : "false");
  }

  function renderTreePane() {
    clearNode(treePane);

    const treeSectionTitle = Construct("div", "cb-tree-section-head");
    treeSectionTitle.append(ConstructGlyph("Layers", "glyph"), Construct("span", null, "Library Tree"));
    treePane.append(treeSectionTitle);

    const treeList = Construct("div", "cb-tree-list");
    for (const cat of catDefs) {
      const catItems = getItems(cat.id);
      const isActiveCat = state.activeCategory === cat.id;
      const isExpanded = state.expandedCats.has(cat.id);

      const branch = Construct("div", `cb-tree-branch${isActiveCat ? " active-branch" : ""}`);
      const branchHead = Construct("div", `cb-tree-cat-row${isActiveCat ? " active" : ""}`);

      const caretBtn = Construct("button", "cb-tree-caret");
      caretBtn.type = "button";
      caretBtn.title = isExpanded ? "Collapse folder group" : "Expand folder group";
      caretBtn.append(ConstructGlyph(isExpanded ? "ChevronDown" : "ChevronRight", "glyph"));
      caretBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        if (state.expandedCats.has(cat.id)) state.expandedCats.delete(cat.id);
        else state.expandedCats.add(cat.id);
        renderTreePane();
      });

      const catSelectBtn = Construct("button", "cb-tree-cat-btn");
      catSelectBtn.type = "button";
      catSelectBtn.append(
        ConstructGlyph(cat.glyph, "glyph cb-tree-cat-glyph"),
        Construct("span", "cb-tree-label", cat.label),
        Construct("span", "cb-tree-count", String(catItems.length)),
      );
      catSelectBtn.addEventListener("click", () => {
        state.activeCategory = cat.id;
        state.activeFamily = "All";
        state.activeFilter = null;
        state.userSpecifiedCategory = true;
        state.expandedCats.add(cat.id);
        render();
      });

      branchHead.append(caretBtn, catSelectBtn);
      branch.append(branchHead);

      if (isExpanded) {
        const subTree = Construct("div", "cb-tree-children");
        const families = getFamiliesFor(cat.id);
        const allFolders = [{ name: "All", count: catItems.length }, ...families];
        for (const fam of allFolders) {
          const isFamActive = isActiveCat && state.activeFamily === fam.name;
          const famBtn = Construct("button", `cb-family-btn cb-tree-node${isFamActive ? " active" : ""}`);
          famBtn.type = "button";
          famBtn.dataset.family = fam.name;
          famBtn.setAttribute("data-family", fam.name);
          famBtn.append(
            ConstructGlyph(fam.name === "All" ? "Layers" : "Folder", "glyph cb-tree-folder-glyph"),
            Construct("span", "cb-tree-label", fam.name === "All" ? `All ${cat.label}` : fam.name),
            Construct("span", "cb-tree-count", String(fam.count)),
          );
          famBtn.addEventListener("click", () => {
            state.activeCategory = cat.id;
            state.activeFamily = fam.name;
            state.userSpecifiedCategory = cat.id !== (WORKSPACE_TO_CATEGORY[state.activeWorkspace] || "materials")
              ? true
              : state.userSpecifiedCategory;
            render();
          });
          subTree.append(famBtn);
        }
        branch.append(subTree);
      }
      treeList.append(branch);
    }
    treePane.append(treeList);

    // Quick Attribute Filters for active category
    const filterDefs = CATEGORY_FILTERS[state.activeCategory] || [];
    if (filterDefs.length) {
      const filterHead = Construct("div", "cb-tree-section-head cb-tree-filter-head");
      filterHead.append(ConstructGlyph("Filter", "glyph"), Construct("span", null, "Quick Filters"));
      if (state.activeFilter) {
        const clearBtn = Construct("button", "cb-filter-clear", "Reset");
        clearBtn.type = "button";
        clearBtn.addEventListener("click", () => {
          state.activeFilter = null;
          render();
        });
        filterHead.append(clearBtn);
      }
      treePane.append(filterHead);

      const filterList = Construct("div", "cb-tree-filters");
      const basePool = getItems().filter((it) => state.activeFamily === "All" || it.family === state.activeFamily);
      for (const f of filterDefs) {
        const matchCount = basePool.filter(f.test).length;
        const active = state.activeFilter === f.id;
        const pill = Construct("button", `cb-filter-pill${active ? " active" : ""}`);
        pill.type = "button";
        pill.dataset.filter = f.id;
        pill.append(
          Construct("span", "cb-filter-dot"),
          Construct("span", "cb-filter-name", f.label),
          Construct("span", "cb-tree-count", String(matchCount)),
        );
        pill.addEventListener("click", () => {
          state.activeFilter = active ? null : f.id;
          renderTreePane();
          renderCenterAndInspector();
        });
        filterList.append(pill);
      }
      treePane.append(filterList);
    }
  }

  function createAssetIconElement(item, size = 64) {
    const wrap = Construct("div", "cb-asset-icon");
    if (item.kind === "material") {
      const img = document.createElement("img");
      img.className = "cb-mat-baked-img";
      img.alt = item.name;
      img.draggable = false;
      const bakedUrl = getBakedMaterialIcon(item.raw, (upgradedUrl) => {
        if (img) img.src = upgradedUrl;
      });
      if (bakedUrl) {
        img.src = bakedUrl;
        wrap.append(img);
      } else {
        // Headless fallback (no 2D canvas pixel API): transparent circular inline SVG PBR orb (no box)
        wrap.innerHTML = makeMatIconSvg(item, size);
      }
    } else if (item.kind === "pattern") {
      wrap.classList.add("cb-icon-pattern");
      wrap.innerHTML = getArtworkPreviewSvg(item.raw, "pattern", size);
    } else if (item.kind === "stamp") {
      wrap.classList.add("cb-icon-stamp");
      wrap.innerHTML = getArtworkPreviewSvg(item.raw, "stamp", size);
    } else {
      wrap.classList.add("cb-icon-mesh");
      wrap.innerHTML = makeMeshIconSvg(item.id, size);
    }
    return wrap;
  }

  function buildInspectorTile(label, value, sub = "", highlight = false) {
    const tile = Construct("div", `kpi-card cb-insp-prop${highlight ? " highlight" : ""}`);
    tile.append(
      Construct("div", "kpi-label", label),
      Construct("div", "kpi-val", value),
      Construct("div", "kpi-sub", sub),
    );
    return tile;
  }

  function getInspectorTileSpecs(item) {
    if (item.kind === "material") {
      const m = item.raw;
      const rough = Number(m.roughness ?? 0.45);
      const metal = Number(m.metalness ?? 0);
      const coat = Number(m.coat ?? 0);
      const tiles = [
        { label: "Roughness", val: rough.toFixed(2), sub: rough < 0.25 ? "Glossy Mirror" : rough > 0.7 ? "Matte Diffuse" : "Satin Sheen", highlight: true },
        { label: "Metalness", val: `${Math.round(metal * 100)}%`, sub: metal >= 0.7 ? "Conductor" : metal > 0.1 ? "Composite" : "Dielectric" },
        { label: "Clearcoat", val: coat > 0.01 ? coat.toFixed(2) : "None", sub: coat > 0.01 ? `Rough ${(m.coatRoughness ?? 0.1).toFixed(2)}` : `IOR ${(m.ior ?? 1.5).toFixed(2)}` },
        { label: "Scale", val: `${Number(m.detailScale || 1).toFixed(1)}x`, sub: m.shaderFamily || m.category || "PBR Kernel" },
      ];
      if ((m.emissionStrength ?? 0) > 0.01) {
        tiles.push({ label: "Emission", val: `${Number(m.emissionStrength).toFixed(1)}x`, sub: m.emissionColor || "Self-Lit" });
      }
      if ((m.iridescence ?? 0) > 0.01) {
        tiles.push({ label: "Thin Film", val: Number(m.iridescence).toFixed(2), sub: "Iridescent" });
      }
      return tiles;
    }
    if (item.kind === "pattern" || item.kind === "stamp") {
      const meta = item.meta || summarizeArtworkEntry(item.raw);
      return [
        { label: "Layers", val: String(meta.layers), sub: meta.layers > 1 ? "Vector Stack" : "Single Motif", highlight: true },
        { label: "Canvas", val: `${meta.width}px`, sub: "Vector Alpha" },
        { label: "Finish", val: meta.primaryFinish.toUpperCase(), sub: meta.finishes.length > 1 ? `${meta.finishes.length} Finishes` : "Surface Tag" },
        { label: "Modifiers", val: String(meta.modifiers), sub: meta.hasText ? "Includes Text" : "Procedural" },
      ];
    }
    const mesh = item.raw;
    return [
      { label: "Geometry", val: mesh.name.split(" ")[0], sub: mesh.family, highlight: true },
      { label: "Topology", val: mesh.topo, sub: mesh.complexity },
      { label: "Bake Focus", val: mesh.role, sub: "Tangent Space" },
      { label: "UV Set", val: "0–1", sub: "Packed Studio" },
    ];
  }

  function renderInspector() {
    clearNode(inspectorPane);
    const inspHead = Construct("div", "cb-insp-head");
    inspHead.append(
      Construct("span", "cb-insp-heading", "Asset Inspector"),
      Construct("span", "cb-insp-cat-pill", CATEGORY_LABELS[state.activeCategory] || "Assets"),
    );
    inspectorPane.append(inspHead);

    const item = findItemById(state.inspectorItemId, state.activeCategory) || getFilteredItems()[0] || getItems()[0];
    if (!item) {
      inspectorPane.append(Construct("div", "cb-empty", "Select any asset card to inspect parameters & PBR tiles."));
      return;
    }
    state.inspectorItemId = item.id;

    // Hero Preview Card: large centerpiece showcase icon + title/family/desc above property tiles
    const heroCard = Construct("div", "cb-insp-hero");
    const heroStage = Construct("div", "cb-insp-hero-stage");
    heroStage.append(createAssetIconElement(item, 136));

    const metaCol = Construct("div", "cb-insp-meta");
    metaCol.append(
      Construct("div", "cb-insp-title", item.name),
      Construct("div", "cb-insp-sub", `${item.family} · ${item.kind.toUpperCase()}`),
    );
    const desc = item.raw?.description || item.raw?.desc || "";
    if (desc) metaCol.append(Construct("div", "cb-insp-desc", desc));
    heroCard.append(heroStage, metaCol);
    inspectorPane.append(heroCard);

    // MetricTrio-style Tile Grid (.kpi-trio.cb-insp-tiles -> .kpi-card.cb-insp-prop) below the hero card
    const tileSpecs = getInspectorTileSpecs(item);
    const tilesGrid = Construct("div", "kpi-trio cb-insp-tiles");
    for (const t of tileSpecs) {
      tilesGrid.append(buildInspectorTile(t.label, t.val, t.sub, Boolean(t.highlight)));
    }
    inspectorPane.append(tilesGrid);

    // Action Buttons
    const actions = Construct("div", "cb-insp-actions");
    const applyBtn = Construct("button", "cb-insp-btn primary", "Apply to Stage");
    applyBtn.type = "button";
    applyBtn.addEventListener("click", () => {
      dispatchApplyItem(item);
      if (onWorkspaceRoute) {
        if (item.kind === "material") onWorkspaceRoute("material");
        else if (item.kind === "pattern") onWorkspaceRoute("pattern");
        else if (item.kind === "stamp") onWorkspaceRoute("stamp");
        else if (item.kind === "mesh") onWorkspaceRoute("bake");
      }
    });
    actions.append(applyBtn);

    if (item.kind === "stamp" || item.kind === "pattern") {
      const paintBtn = Construct("button", "cb-insp-btn", "Send to Paint");
      paintBtn.type = "button";
      paintBtn.addEventListener("click", () => {
        if (onWorkspaceRoute) onWorkspaceRoute("paint");
        dispatchApplyItem(item, { targetWorkspace: "paint" });
        Toast(`${item.name} sent to Texture Paint`);
      });
      actions.append(paintBtn);
    }
    if (item.kind === "pattern") {
      const tileMatBtn = Construct("button", "cb-insp-btn", "Tile on Material");
      tileMatBtn.type = "button";
      tileMatBtn.addEventListener("click", () => {
        if (onWorkspaceRoute) onWorkspaceRoute("material");
        dispatchApplyItem(item, { targetWorkspace: "material" });
        Toast(`Tiled ${item.name} onto active Material`);
      });
      actions.append(tileMatBtn);
    }
    inspectorPane.append(actions);
  }

  function renderGrid() {
    clearNode(grid);
    clearNode(familyPillsStrip);
    grid.className = state.viewMode === "list" ? "cb-list" : "cb-grid";

    const searchPlaceholders = {
      materials: "Search materials...",
      patterns: "Search patterns...",
      stamps: "Search stamps...",
      meshes: "Search meshes...",
    };
    searchInp.placeholder = searchPlaceholders[state.activeCategory] || "Search assets...";

    const allPillsLabel = {
      materials: "All materials",
      patterns: "All patterns",
      stamps: "All stamps",
      meshes: "All meshes",
    };
    const fams = ["All", ...getFamiliesFor(state.activeCategory).map((f) => f.name)];
    for (const fam of fams) {
      const chip = Construct(
        "button",
        `cb-family-chip${state.activeFamily === fam ? " active" : ""}`,
        fam === "All" ? (allPillsLabel[state.activeCategory] || "All") : fam,
      );
      chip.type = "button";
      chip.dataset.family = fam;
      chip.addEventListener("click", () => {
        state.activeFamily = fam;
        renderTreePane();
        renderCenterAndInspector();
      });
      familyPillsStrip.append(chip);
    }

    const filtered = getFilteredItems();
    const total = getItems().length;
    countBadge.textContent = filtered.length === total ? `${total}` : `${filtered.length}/${total}`;

    const collectionLabels = {
      materials: state.activeFamily === "All" ? "THE ESSENTIAL COLLECTION" : `${state.activeFamily.toUpperCase()} COLLECTION`,
      patterns: state.activeFamily === "All" ? "PATTERN LIBRARY" : `${state.activeFamily.toUpperCase()} PATTERNS`,
      stamps: state.activeFamily === "All" ? "DECAL STAMPS COLLECTION" : `${state.activeFamily.toUpperCase()} STAMPS`,
      meshes: state.activeFamily === "All" ? "LOOK-DEV STUDY MESHES" : `${state.activeFamily.toUpperCase()} MESHES`,
    };
    collectionTitle.textContent = collectionLabels[state.activeCategory] || "ASSET COLLECTION";
    collectionCount.textContent = String(filtered.length);

    const visible = filtered.slice(0, 140);
    if (!visible.length) {
      grid.append(Construct("div", "cb-empty", "No assets match your current folder, filter, or search."));
      return;
    }

    for (const item of visible) {
      const isSelected = state.inspectorItemId === item.id;
      const isList = state.viewMode === "list";
      const card = Construct("div", `cb-card${isList ? " cb-card-list" : ""}${isSelected ? " selected active" : ""}`);
      card.draggable = true;
      card.dataset.assetKind = item.kind;
      card.dataset.assetId = item.id;
      card.setAttribute("data-asset-kind", item.kind);
      card.setAttribute("data-asset-id", item.id);
      card.title = `${item.name} (${item.family}) — Click to inspect · Double-click or drag to Stage`;

      const iconEl = createAssetIconElement(item, isList ? 34 : 76);
      const labelEl = Construct("div", "cb-card-name", item.name);

      if (isList) {
        const textCol = Construct("div", "cb-list-text");
        textCol.append(labelEl, Construct("div", "cb-list-sub", item.family));
        const metricText = item.kind === "material"
          ? `R ${(item.raw.roughness ?? 0.45).toFixed(2)} · M ${Math.round((item.raw.metalness ?? 0) * 100)}%`
          : item.kind === "mesh"
            ? (item.raw.complexity || "Study")
            : `${item.meta?.layers || 1} ${(item.meta?.layers || 1) === 1 ? "layer" : "layers"}`;
        const metricEl = Construct("span", "cb-list-metric", metricText);
        card.append(iconEl, textCol, metricEl);
      } else {
        const checkBadge = Construct("span", "cb-card-check", ConstructGlyph("Check", "glyph"));
        const cardFooter = Construct("div", "cb-card-footer", [
          labelEl,
          Construct("div", "cb-card-sub", item.family),
        ]);
        card.append(checkBadge, iconEl, cardFooter);
      }

      card.addEventListener("dragstart", (e) => {
        if (e.dataTransfer) {
          e.dataTransfer.effectAllowed = "copy";
          e.dataTransfer.setData(
            "application/x-frontier-asset",
            JSON.stringify({ assetKind: item.kind, id: item.id, name: item.name }),
          );
          e.dataTransfer.setData("text/plain", item.name);
        }
        card.classList.add("dragging");
      });
      card.addEventListener("dragend", () => card.classList.remove("dragging"));

      card.addEventListener("click", () => {
        state.inspectorItemId = item.id;
        for (const el of grid.querySelectorAll(".cb-card.selected")) {
          el.classList.remove("selected");
          el.classList.remove("active");
        }
        card.classList.add("selected");
        card.classList.add("active");
        renderInspector();
      });

      card.addEventListener("dblclick", () => {
        state.inspectorItemId = item.id;
        dispatchApplyItem(item);
        renderInspector();
        Toast(`Applied ${item.name}`);
      });

      grid.append(card);
    }
  }

  function renderCenterAndInspector() {
    renderGrid();
    renderInspector();
  }

  function render() {
    for (const [id, btn] of catBtnMap.entries()) {
      btn.classList.toggle("active", state.activeCategory === id);
    }
    autoBadge.textContent = state.userSpecifiedCategory
      ? "Pinned · Reset"
      : `Auto · ${state.activeWorkspace}`;
    autoBadge.classList.toggle("pinned", state.userSpecifiedCategory);
    renderViewButtons();
    renderTreePane();
    renderCenterAndInspector();
  }

  function syncWorkspace(wsId) {
    state.activeWorkspace = wsId;
    if (!state.userSpecifiedCategory) {
      const nextCat = WORKSPACE_TO_CATEGORY[wsId] || "materials";
      if (nextCat !== state.activeCategory) {
        state.activeCategory = nextCat;
        state.activeFamily = "All";
        state.activeFilter = null;
        state.expandedCats = new Set([nextCat]);
      }
    }
    render();
  }

  applyHeight();
  render();

  return {
    element: root,
    syncWorkspace,
    setActiveWorkspace: syncWorkspace,
    getAssetPayload(kind, id) {
      const catMap = { material: "materials", pattern: "patterns", stamp: "stamps", mesh: "meshes" };
      const item = findItemById(id, catMap[kind] || state.activeCategory);
      return toPayload(item);
    },
    setCategory(catId, { userPinned = true } = {}) {
      if (!WORKSPACE_TO_CATEGORY[catId] && !catBtnMap.has(catId)) return;
      state.activeCategory = catId;
      state.activeFamily = "All";
      state.activeFilter = null;
      state.userSpecifiedCategory = userPinned;
      state.expandedCats = new Set([catId]);
      render();
    },
    setViewMode(mode) {
      if (mode !== "grid" && mode !== "list") return;
      state.viewMode = mode;
      renderViewButtons();
      renderGrid();
    },
    getState() {
      return { ...state, expandedCats: [...state.expandedCats] };
    },
    setCollapsed(v) {
      state.collapsed = Boolean(v);
      applyHeight();
    },
  };
}
