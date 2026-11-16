/*==========================================================================================================================================
                                                         VIEWPORTHEADERKIT.JS
==========================================================================================================================================*/
/* Shared two-tier AlloyStudio / Experimental viewport header builder unified across Pattern, Stamp & Texture Paint:
   Band 1 (.vp-doc-bar.se-header-bar):  [ Eyebrow · Status / Appropriately Scaled Canvas Name ] ... [ Editable Resolution + Preset Dropdown ]
   Band 2 (.vp-tools-bar.tp-authoring-bar): [ .tp-tool-group: Circular Tool Buttons ] [ More tools ▾ ] ... [ View | Undo/Redo | Save ▾ | CTAs ] */

import { Construct, ConstructGlyph } from "../StudioControls.js";

export const STUDIO_RESOLUTIONS = [512, 1024, 2048, 4096];

export function createViewportHeader(parentCol, opts = {}) {
  const {
    workspace = "pattern",
    badgeText = "PATTERN",
    eyebrowText = "",
    draftText = "",
    iconGlyph = "Pattern",
    canvasName = "Untitled",
    onRename = null,
    resolutions = STUDIO_RESOLUTIONS,
    resolution = 1024,
    onResolutionChange = null,
    viewOptions = [],
    viewValue = "",
    viewAriaLabel = "Canvas View",
    onViewChange = null,
    onUndo = null,
    onRedo = null,
    saveItems = [],
    primaryActions = [],
    tools = [],
    moreTools = [],
    moreControls = [],
    toolGroupClass = "tp-tool-group",
    onToolSelect = null,
    toolTrailing = [],
  } = opts;

  const docBar = Construct("div", "viewport-toolbar vp-doc-bar se-header-bar");
  docBar.dataset.workspaceHeader = workspace;

  const eyebrowEl = Construct("span", "vp-hero-eyebrow", eyebrowText || "");

  const nameInput = document.createElement("input");
  nameInput.type = "text";
  nameInput.className = "vp-name-input";
  nameInput.value = canvasName || "Untitled";
  nameInput.setAttribute("aria-label", "Canvas name");
  nameInput.title = "Click to rename canvas";

  const modeBadge = Construct("span", "vp-mode-badge", badgeText);
  const sceneIcon = Construct("div", "tp-scene-icon", ConstructGlyph(iconGlyph));
  const docPill = Construct("div", "se-doc-pill", [nameInput, modeBadge]);

  const statusText = Construct("span", "vp-draft-status", draftText || "");
  const statusSep = Construct("span", "vp-hero-meta-sep", "·");
  const metaLine = Construct("div", "vp-hero-meta-line", [eyebrowEl, statusSep, statusText]);
  const syncMetaVisibility = () => {
    const hasEye = !!(eyebrowEl.textContent || "").trim();
    const hasStatus = !!(statusText.textContent || "").trim();
    eyebrowEl.hidden = !hasEye;
    statusText.hidden = !hasStatus;
    statusSep.hidden = !(hasEye && hasStatus);
    metaLine.hidden = !(hasEye || hasStatus);
  };
  syncMetaVisibility();

  const heroBlock = Construct("div", "vp-hero-block", [
    metaLine,
    Construct("div", "vp-hero-title-row", [sceneIcon, docPill]),
  ]);

  nameInput.addEventListener("change", () => {
    const next = String(nameInput.value || "").trim() || "Untitled";
    nameInput.value = next;
    onRename?.(next);
  });

  // Editable Resolution Input + Preset Dropdown Combo ([ Resolution  [512] px | ▾ ])
  let committedResolution = Math.max(64, Math.min(4096, Math.round(Number(resolution) || 512)));
  const resInput = document.createElement("input");
  resInput.type = "number";
  resInput.className = "vp-res-input";
  resInput.min = "64";
  resInput.max = "4096";
  resInput.step = "64";
  resInput.value = String(committedResolution);
  resInput.setAttribute("aria-label", "Canvas Resolution (px)");
  resInput.title = "Type custom resolution (64–4096px) or pick from dropdown";

  const resSelect = Construct("select", "se-header-select se-res-select");
  resSelect.setAttribute("aria-label", "Canvas Resolution Presets");
  resSelect.title = "Preset Canvas Resolutions";
  for (const r of resolutions) {
    const opt = Construct("option", null, `${r} × ${r}`);
    opt.value = String(r);
    resSelect.append(opt);
  }
  resSelect.value = resolutions.includes(committedResolution) ? String(committedResolution) : "";

  const commitResolution = (nextRaw) => {
    const parsed = Math.round(Number(nextRaw));
    if (!Number.isFinite(parsed) || parsed <= 0) {
      resInput.value = String(committedResolution);
      return;
    }
    const clamped = Math.max(64, Math.min(4096, parsed));
    resInput.value = String(clamped);
    resSelect.value = resolutions.includes(clamped) ? String(clamped) : "";
    if (clamped !== committedResolution) {
      committedResolution = clamped;
      onResolutionChange?.(clamped);
    }
  };

  resInput.addEventListener("change", () => commitResolution(resInput.value));
  resInput.addEventListener("blur", () => commitResolution(resInput.value));
  resInput.addEventListener("keydown", (e) => {
    if (e.key === "Enter") {
      e.preventDefault();
      commitResolution(resInput.value);
      resInput.blur?.();
    }
  });
  resSelect.addEventListener("change", () => {
    const v = Number(resSelect.value);
    if (Number.isFinite(v) && v > 0) commitResolution(v);
  });

  const sizeWrap = Construct("div", "select-pill-wrap vp-res-dropdown", [
    resSelect,
    Construct("span", "pill-caret-divider", ConstructGlyph("ChevronDown", "glyph chev")),
  ]);
  const sizeField = Construct("div", "vp-field-chip vp-res-combo", [
    Construct("span", "vp-res-label", "Resolution"),
    resInput,
    Construct("span", "vp-res-unit", "px"),
    sizeWrap,
  ]);

  let viewSelect = null;
  let viewField = null;
  if (viewOptions.length) {
    viewSelect = Construct("select", "se-header-select vp-view-select");
    viewSelect.setAttribute("aria-label", viewAriaLabel);
    for (const [label, val] of viewOptions) {
      const opt = Construct("option", null, label);
      opt.value = val;
      viewSelect.append(opt);
    }
    if (viewValue) viewSelect.value = viewValue;
    viewSelect.addEventListener("change", () => onViewChange?.(viewSelect.value));
    const viewWrap = Construct("div", "select-pill-wrap vp-inline-select", [
      viewSelect,
      Construct("span", "pill-caret-divider", ConstructGlyph("ChevronDown", "glyph chev")),
    ]);
    viewField = Construct("label", "vp-field-chip vp-view-chip", [
      Construct("span", null, "View"),
      viewWrap,
    ]);
  }

  let undoBtn = null;
  let redoBtn = null;
  if (onUndo || onRedo) {
    undoBtn = Construct("button", "icon-btn vp-mini-icon vp-undo-btn", ConstructGlyph("Undo"));
    undoBtn.type = "button";
    undoBtn.setAttribute("data-header-action", "undo");
    undoBtn.title = "Undo (Ctrl+Z)";
    undoBtn.setAttribute("aria-label", "Undo");
    if (onUndo) undoBtn.addEventListener("click", onUndo);

    redoBtn = Construct("button", "icon-btn vp-mini-icon vp-redo-btn", ConstructGlyph("Redo"));
    redoBtn.type = "button";
    redoBtn.setAttribute("data-header-action", "redo");
    redoBtn.title = "Redo (Ctrl+Y)";
    redoBtn.setAttribute("aria-label", "Redo");
    if (onRedo) redoBtn.addEventListener("click", onRedo);
  }

  const saveWrap = Construct("div", "se-save-wrap");
  const saveBtn = Construct("button", "pill-btn se-save-btn", [
    ConstructGlyph("Save"),
    Construct("span", null, "Save"),
    Construct("span", "pill-caret-divider", ConstructGlyph("ChevronDown", "glyph chev")),
  ]);
  saveBtn.type = "button";
  saveBtn.title = "Save Project & Export Maps";

  const saveMenu = Construct("div", "se-save-menu");
  saveMenu.hidden = true;
  const saveItemNodes = new Map();

  for (const item of saveItems) {
    if (item.node) {
      saveMenu.append(item.node);
      continue;
    }
    const btn = Construct("button", "se-save-item", [
      Construct("span", null, item.label),
      item.meta ? Construct("small", null, item.meta) : null,
    ]);
    btn.type = "button";
    if (item.id) btn.dataset.saveId = item.id;
    btn.addEventListener("click", () => {
      closeSaveMenu();
      item.onClick?.();
    });
    if (item.id) saveItemNodes.set(item.id, btn);
    saveMenu.append(btn);
  }
  saveWrap.append(saveBtn, saveMenu);

  const closeSaveMenu = () => {
    saveMenu.hidden = true;
    saveBtn.classList.remove("active");
  };

  saveBtn.addEventListener("click", (e) => {
    e.stopPropagation();
    const nextOpen = saveMenu.hidden;
    saveMenu.hidden = !nextOpen;
    saveBtn.classList.toggle("active", nextOpen);
  });

  // Band 1 contains strictly: [ Document Title (appropriately scaled)      Editable Resolution + Dropdown ]
  const rightControls = Construct("div", "vp-doc-actions", [sizeField]);
  docBar.append(heroBlock, Construct("div", "toolbar-spacer"), rightControls);

  // Band 2: Image 1 Floating Circular Tool Dock ([ circles ] [ More tools ])
  const toolsBar = Construct("div", "vp-tools-bar tp-authoring-bar");
  const toolGroup = Construct("div", toolGroupClass);
  toolGroup.setAttribute("role", "group");
  toolGroup.setAttribute("aria-label", "Workspace tools");

  const toolButtons = new Map();
  const makeToolButton = (t, inMenu = false) => {
    const baseCls = inMenu ? "vp-more-tool-item" : t.className || t.buttonClass || "se-tool-btn";
    const circleCls = inMenu ? "" : " vp-tool-circle";
    const extraCls = !inMenu && t.extra ? " vp-tool-extra" : "";
    const children = [
      ConstructGlyph(t.glyph),
      Construct("span", inMenu ? "vp-tool-label-vis" : "vp-tool-label-sr", t.label),
    ];
    if (t.kbd && inMenu) children.push(Construct("kbd", "vp-tool-kbd", t.kbd));
    const btn = Construct("button", baseCls + circleCls + extraCls + (t.active ? " active" : ""), children);
    btn.type = "button";
    btn.title = t.title || (t.kbd ? `${t.label} (${t.kbd})` : t.label);
    if (!inMenu && t.datasetKey) btn.dataset[t.datasetKey] = t.id;
    btn.setAttribute("aria-label", t.label);
    btn.setAttribute("aria-pressed", t.active ? "true" : "false");
    btn.addEventListener("click", () => {
      onToolSelect?.(t.id);
      if (inMenu) closeMoreMenu();
    });
    if (!inMenu) {
      toolButtons.set(t.id, btn);
    } else {
      if (!menuToolButtons.has(t.id)) menuToolButtons.set(t.id, []);
      menuToolButtons.get(t.id).push(btn);
    }
    return btn;
  };

  const menuToolButtons = new Map();
  for (const t of tools) {
    toolGroup.append(makeToolButton(t, false));
  }
  toolsBar.append(toolGroup);

  let moreWrap = null;
  let moreBtn = null;
  let moreMenu = null;
  const closeMoreMenu = () => {
    if (!moreMenu || !moreBtn) return;
    moreMenu.hidden = true;
    moreBtn.classList.remove("active");
  };

  if (moreTools.length || moreControls.length) {
    moreWrap = Construct("div", "vp-more-tools-wrap");
    moreBtn = Construct("button", "vp-more-tools-btn", "More tools");
    moreBtn.type = "button";
    moreBtn.title = "Additional shapes, grid, snapping & stage tools";
    moreMenu = Construct("div", "vp-more-tools-menu");
    moreMenu.hidden = true;

    if (moreTools.length) {
      const mGrid = Construct("div", "vp-more-tools-grid");
      for (const mt of moreTools) mGrid.append(makeToolButton(mt, true));
      moreMenu.append(mGrid);
    }
    if (moreControls.length) {
      const mCtrls = Construct("div", "vp-more-tools-controls");
      for (const mc of moreControls) mCtrls.append(mc);
      moreMenu.append(mCtrls);
    }

    moreBtn.addEventListener("click", (e) => {
      e.stopPropagation();
      const next = moreMenu.hidden;
      moreMenu.hidden = !next;
      moreBtn.classList.toggle("active", next);
    });
    moreWrap.append(moreBtn, moreMenu);
    toolsBar.append(moreWrap);
  }

  const tailNodes = [
    viewField,
    undoBtn,
    redoBtn,
    saveWrap,
    ...primaryActions,
    ...toolTrailing,
  ].filter(Boolean);
  if (tailNodes.length) {
    const tail = Construct("div", "vp-tools-tail", [
      Construct("span", "vp-bar-divider"),
      ...tailNodes,
    ]);
    toolsBar.append(tail);
  }

  const onDocDown = (e) => {
    if (!saveMenu.hidden && !saveWrap.contains?.(e.target)) closeSaveMenu();
    if (moreMenu && !moreMenu.hidden && !moreWrap?.contains?.(e.target)) closeMoreMenu();
  };
  document.addEventListener("pointerdown", onDocDown);

  parentCol.append(docBar, toolsBar);

  return {
    docBar,
    toolsBar,
    toolGroup,
    nameInput,
    docPill,
    eyebrowEl,
    statusText,
    resInput,
    resSelect,
    viewSelect,
    saveBtn,
    saveMenu,
    undoBtn,
    redoBtn,
    moreBtn,
    moreMenu,
    toolButtons,
    setName(next) {
      if (document.activeElement !== nameInput) {
        nameInput.value = next || "Untitled";
      }
    },
    setEyebrow(txt) {
      eyebrowEl.textContent = txt || "";
      syncMetaVisibility();
    },
    setDraftStatus(txt) {
      statusText.textContent = txt || "";
      syncMetaVisibility();
    },
    setResolution(next) {
      const val = Math.max(64, Math.min(4096, Math.round(Number(next) || 512)));
      committedResolution = val;
      resInput.value = String(val);
      resSelect.value = resolutions.includes(val) ? String(val) : "";
    },
    setView(val) {
      if (viewSelect) viewSelect.value = val;
    },
    setUndoRedoState(canUndo, canRedo) {
      if (undoBtn) undoBtn.disabled = !canUndo;
      if (redoBtn) redoBtn.disabled = !canRedo;
    },
    setActiveTool(id) {
      for (const [tid, btn] of toolButtons) {
        const isActive = tid === id;
        btn.classList.toggle("active", isActive);
        btn.setAttribute("aria-pressed", isActive ? "true" : "false");
      }
      for (const [tid, list] of menuToolButtons) {
        const isActive = tid === id;
        for (const btn of list) {
          btn.classList.toggle("active", isActive);
          btn.setAttribute("aria-pressed", isActive ? "true" : "false");
        }
      }
    },
    setSaveItemMeta(id, txt) {
      const btn = saveItemNodes.get(id);
      const sm = btn?.querySelector?.("small");
      if (sm) sm.textContent = txt;
    },
    closeSaveMenu,
    dispose() {
      closeSaveMenu();
      closeMoreMenu();
      document.removeEventListener("pointerdown", onDocDown);
    },
  };
}
