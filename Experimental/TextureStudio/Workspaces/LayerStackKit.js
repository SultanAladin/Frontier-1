/*==========================================================================================================================================
                                                         LAYERSTACKKIT.JS
==========================================================================================================================================*/
/* Unified Full-Design Layer Stack Panel shared across Pattern, Stamp, and Texture Paint workspaces:
   - Reference-faithful header ("Layers N / limit" + Folder+ button + Add Layer "+" dropdown menu)
   - Clickable Visible / Hidden stat filter cards + Search pill + TOP -> BOTTOM strip
   - Nested Folder tree with disclosure chevrons, 3px left selection spine, LIVE 36x36 layer preview thumbnails,
     inline double-click rename, scrubbable opacity pill, Up/Down + Drag-and-drop reorder, Eye & Lock toggles
   - Inline Mask sub-row (with live mask preview), Stamp/Decal sub-rows, Expandable channel/modifier detail drawer,
     Bottom action toolbar, System Layer 0 base bar, and 3-metric Pane Footer. */

import { Construct, ConstructGlyph, SmoothScroll } from "../StudioControls.js";

function pad2(n) {
  return String(Math.max(0, Math.round(n))).padStart(2, "0");
}

function getKindColor(kind) {
  switch (kind) {
    case "folder": return "#c8c2b4";
    default: return "#c2c5cd";
  }
}

export function createLayerStackPanel(leftCol, config) {
  const uiState = {
    addMenuOpen: false,
    filter: "all", // "all" | "visible" | "hidden"
    search: "",
    expandedIds: new Set(),
    collapsedFolderIds: new Set(),
  };

  leftCol.innerHTML = "";

  // 1. Heading ("Layers N / 32" + New Folder + Add Layer "+")
  const countEl = Construct("small", "tp-pane-count", "0 / 32");
  const btnNewFolder = Construct("button", "tp-round-icon tp-new-folder", ConstructGlyph("FolderPlus"));
  btnNewFolder.type = "button";
  btnNewFolder.title = "New Group Folder";

  const btnAddToggle = Construct("button", "tp-round-icon tp-add-toggle", ConstructGlyph("Plus"));
  btnAddToggle.type = "button";
  btnAddToggle.title = "Add Layer";

  const heading = Construct("div", "tp-pane-heading", [
    Construct("h2", null, config.title || "Layers"),
    countEl,
    btnNewFolder,
    btnAddToggle,
  ]);

  // 2. Floating Add Layer Menu (.tp-add-menu) — keeps exact .chip-btn labels for both users & automated validation!
  const addMenu = Construct("div", "tp-add-menu");
  addMenu.hidden = true;
  const closeAddBtn = Construct("button", "stack-mini-btn", ConstructGlyph("Close"));
  closeAddBtn.type = "button";
  closeAddBtn.title = "Close Add Menu";
  addMenu.append(
    Construct("div", "tp-add-menu-head", [
      Construct("span", null, "Add Layer"),
      closeAddBtn,
    ]),
  );
  const addMenuList = Construct("div", "tp-add-menu-list");
  addMenu.append(addMenuList);

  const setAddMenuOpen = (next) => {
    uiState.addMenuOpen = !!next;
    addMenu.hidden = !uiState.addMenuOpen;
    addMenu.classList.toggle("open", uiState.addMenuOpen);
    btnAddToggle.classList.toggle("is-open", uiState.addMenuOpen);
  };
  btnAddToggle.addEventListener("click", (e) => {
    e.stopPropagation();
    setAddMenuOpen(!uiState.addMenuOpen);
  });
  closeAddBtn.addEventListener("click", (e) => {
    e.stopPropagation();
    setAddMenuOpen(false);
  });

  // 3. Visible / Hidden Stat Filter Cards
  const visCountEl = Construct("strong", null, "00");
  const visStatBtn = Construct("button", "tp-stat-card", [
    Construct("span", "tp-status-disc is-visible", ConstructGlyph("Check")),
    Construct("small", null, "Visible"),
    visCountEl,
  ]);
  visStatBtn.type = "button";
  visStatBtn.title = "Filter visible layers";

  const hidCountEl = Construct("strong", null, "00");
  const hidStatBtn = Construct("button", "tp-stat-card", [
    Construct("span", "tp-status-disc is-hidden", ConstructGlyph("EyeOff")),
    Construct("small", null, "Hidden"),
    hidCountEl,
  ]);
  hidStatBtn.type = "button";
  hidStatBtn.title = "Filter hidden layers";

  const statsRow = Construct("div", "tp-layer-stats", [visStatBtn, hidStatBtn]);

  const filterNoteText = Construct("span", null, "");
  const btnClearFilter = Construct("button", "tp-clear-filter", "Show all");
  btnClearFilter.type = "button";
  const filterNote = Construct("div", "tp-filter-note", [filterNoteText, btnClearFilter]);
  filterNote.hidden = true;

  // 4. Layer Search Pill
  const searchInp = document.createElement("input");
  searchInp.type = "search";
  searchInp.className = "tp-search-input";
  searchInp.placeholder = "Find a layer…";
  searchInp.setAttribute("aria-label", "Find a layer");
  const btnClearSearch = Construct("button", "tp-search-clear", ConstructGlyph("Close"));
  btnClearSearch.type = "button";
  btnClearSearch.title = "Clear search";
  btnClearSearch.hidden = true;
  const searchBar = Construct("div", "tp-layer-search", [
    Construct("label", "tp-search-pill", [
      ConstructGlyph("Search"),
      searchInp,
      btnClearSearch,
    ]),
  ]);

  // 5. TOP -> BOTTOM Order Strip
  const shownEl = Construct("span", "tp-order-right", "0 shown");
  const orderStrip = Construct("div", "tp-layer-order", [
    Construct("span", null, "TOP → BOTTOM"),
    shownEl,
  ]);

  // 6. Scrollable Stack Container
  const scrollWrap = Construct("div", "panel-scroll tp-layer-scroll");
  const scrollInner = Construct("div", "panel-scroll-inner tp-layer-list");
  scrollWrap.append(scrollInner);
  SmoothScroll(scrollWrap, scrollInner);

  // 7. Bottom Action Toolbar
  const actUp = Construct("button", "tp-action-btn", ConstructGlyph("ArrowUp"));
  actUp.type = "button"; actUp.title = "Move selected layer up";
  const actDown = Construct("button", "tp-action-btn", ConstructGlyph("ArrowDown"));
  actDown.type = "button"; actDown.title = "Move selected layer down";
  const actDup = Construct("button", "tp-action-btn", ConstructGlyph("Copy"));
  actDup.type = "button"; actDup.title = "Duplicate selected layer";
  const actMask = Construct("button", "tp-action-btn", ConstructGlyph("Mask"));
  actMask.type = "button"; actMask.title = "Add or toggle layer mask";
  const actDel = Construct("button", "tp-action-btn danger", ConstructGlyph("Trash"));
  actDel.type = "button"; actDel.title = "Delete selected layer";

  const actionsBar = Construct("div", "tp-layer-actions", [
    actUp,
    actDown,
    Construct("span", "tp-action-spacer"),
    actDup,
    actMask,
    actDel,
  ]);

  // 8. System Base Row (Layer 0 / Canvas Tile)
  const sysBadge = Construct("small", "tp-sys-badge", "Base");
  const sysMeta = Construct("span", "tp-sys-meta", "Surface base");
  const sysTitle = Construct("strong", "tp-sys-title", "Layer 0 · Mesh base");
  const sysRow = Construct("div", "tp-system-base", [
    Construct("span", "tp-sys-icon", ConstructGlyph("Layers")),
    Construct("span", "tp-sys-copy", [sysTitle, sysMeta]),
    sysBadge,
  ]);

  // 9. 3-Metric Pane Footer
  const fTotalEl = Construct("strong", null, "0");
  const fVisEl = Construct("strong", null, "0");
  const fExtraLabel = Construct("span", null, config.extraMetricLabel || "ITEMS");
  const fExtraEl = Construct("strong", null, "0");
  const paneFooter = Construct("div", "tp-pane-footer", [
    Construct("span", null, [Construct("span", null, "LAYERS"), fTotalEl]),
    Construct("span", null, [Construct("span", null, "VISIBLE"), fVisEl]),
    Construct("span", null, [fExtraLabel, fExtraEl]),
  ]);

  leftCol.append(
    heading,
    addMenu,
    statsRow,
    filterNote,
    searchBar,
    orderStrip,
    scrollWrap,
    actionsBar,
    sysRow,
    paneFooter,
  );

  let latestArgs = null;
  let dragLayerId = null;

  const rebuildAddMenu = (addOptions) => {
    addMenuList.innerHTML = "";
    for (const opt of addOptions || []) {
      const iconBox = Construct("span", `tp-kind-icon tp-kind-${opt.kind}`, ConstructGlyph(opt.glyph || "Layers"));
      iconBox.style.setProperty("--kind-color", getKindColor(opt.kind));

      // Exact opt.label on .chip-btn keeps automated validation selectors intact!
      const btn = Construct("button", "chip-btn tp-add-item", opt.label);
      btn.type = "button";
      btn.dataset.layerKind = opt.kind;
      const onPick = (e) => {
        e.stopPropagation();
        setAddMenuOpen(false);
        latestArgs?.onAddLayer?.(opt.kind);
      };
      btn.addEventListener("click", onPick);

      const copy = Construct("div", "tp-add-copy", [
        btn,
        opt.hint ? Construct("small", "tp-add-hint", opt.hint) : null,
      ]);
      const row = Construct("div", "tp-add-menu-row", [iconBox, copy]);
      row.addEventListener("click", onPick);
      addMenuList.append(row);
    }
  };

  visStatBtn.addEventListener("click", () => {
    uiState.filter = uiState.filter === "visible" ? "all" : "visible";
    if (latestArgs) render(latestArgs);
  });
  hidStatBtn.addEventListener("click", () => {
    uiState.filter = uiState.filter === "hidden" ? "all" : "hidden";
    if (latestArgs) render(latestArgs);
  });
  btnClearFilter.addEventListener("click", () => {
    uiState.filter = "all";
    uiState.search = "";
    searchInp.value = "";
    btnClearSearch.hidden = true;
    if (latestArgs) render(latestArgs);
  });
  searchInp.addEventListener("input", () => {
    uiState.search = searchInp.value || "";
    btnClearSearch.hidden = !uiState.search.trim();
    if (latestArgs) render(latestArgs);
  });
  btnClearSearch.addEventListener("click", () => {
    uiState.search = "";
    searchInp.value = "";
    btnClearSearch.hidden = true;
    if (latestArgs) render(latestArgs);
  });

  btnNewFolder.addEventListener("click", () => {
    setAddMenuOpen(false);
    latestArgs?.onAddLayer?.("folder");
  });

  const onDocPointerDown = (e) => {
    if (!uiState.addMenuOpen) return;
    if (!addMenu.contains?.(e.target) && !btnAddToggle.contains?.(e.target)) {
      setAddMenuOpen(false);
    }
  };
  document.addEventListener?.("pointerdown", onDocPointerDown);

  actUp.addEventListener("click", () => {
    if (latestArgs?.selectedId) latestArgs.onMoveLayer?.(latestArgs.selectedId, +1);
  });
  actDown.addEventListener("click", () => {
    if (latestArgs?.selectedId) latestArgs.onMoveLayer?.(latestArgs.selectedId, -1);
  });
  actDup.addEventListener("click", () => {
    if (latestArgs?.selectedId) latestArgs.onDuplicateLayer?.(latestArgs.selectedId);
  });
  actMask.addEventListener("click", () => {
    if (latestArgs?.selectedId) latestArgs.onToggleLayerMask?.(latestArgs.selectedId);
  });
  actDel.addEventListener("click", () => {
    if (latestArgs?.selectedId) latestArgs.onDeleteLayer?.(latestArgs.selectedId);
  });

  function render(args) {
    latestArgs = args;
    const layers = args.layers || [];
    const limit = args.limit || 32;
    const selectedId = args.selectedId;
    const layerById = new Map(layers.map((l) => [l.id, l]));

    if (args.addOptions && addMenuList.children.length !== args.addOptions.length) {
      rebuildAddMenu(args.addOptions);
    }

    const isEffectiveVisible = (lyr) => {
      if (!lyr || lyr.visible === false) return false;
      if (lyr.parentId && layerById.has(lyr.parentId)) return isEffectiveVisible(layerById.get(lyr.parentId));
      return true;
    };

    const getDepth = (lyr) => {
      let d = 0;
      let cur = lyr;
      const seen = new Set([lyr.id]);
      while (cur?.parentId && layerById.has(cur.parentId) && !seen.has(cur.parentId)) {
        d++;
        cur = layerById.get(cur.parentId);
        seen.add(cur.id);
      }
      return Math.min(4, d);
    };

    const isUnderCollapsedFolder = (lyr) => {
      let cur = lyr;
      const seen = new Set([lyr.id]);
      while (cur?.parentId && layerById.has(cur.parentId) && !seen.has(cur.parentId)) {
        const parent = layerById.get(cur.parentId);
        if (uiState.collapsedFolderIds.has(parent.id)) return true;
        cur = parent;
        seen.add(parent.id);
      }
      return false;
    };

    const visibleCount = layers.filter((l) => isEffectiveVisible(l)).length;
    const hiddenCount = layers.length - visibleCount;

    countEl.textContent = `${layers.length} / ${limit}`;
    visCountEl.textContent = pad2(visibleCount);
    hidCountEl.textContent = pad2(hiddenCount);
    visStatBtn.classList.toggle("is-active", uiState.filter === "visible");
    hidStatBtn.classList.toggle("is-active", uiState.filter === "hidden");

    const q = uiState.search.trim().toLowerCase();
    const filtering = uiState.filter !== "all" || q.length > 0;
    filterNote.hidden = !filtering;
    if (filtering) {
      const parts = [];
      if (uiState.filter === "visible") parts.push("Visible layers");
      else if (uiState.filter === "hidden") parts.push("Hidden layers");
      if (q) parts.push(`“${uiState.search.trim()}”`);
      filterNoteText.textContent = `Filtered: ${parts.join(" · ")}`;
    }

    const layersTopDown = [...layers].reverse();
    const childCountMap = new Map();
    for (const l of layers) {
      if (l.parentId) childCountMap.set(l.parentId, (childCountMap.get(l.parentId) || 0) + 1);
    }

    const displayed = [];
    for (let vIdx = 0; vIdx < layersTopDown.length; vIdx++) {
      const layer = layersTopDown[vIdx];
      if (!filtering && isUnderCollapsedFolder(layer)) continue;
      const effVis = isEffectiveVisible(layer);
      if (uiState.filter === "visible" && !effVis) continue;
      if (uiState.filter === "hidden" && effVis) continue;
      if (q) {
        const hay = `${layer.name || ""} ${layer.kind || ""} ${args.getSubtitle?.(layer) || ""}`.toLowerCase();
        if (!hay.includes(q)) continue;
      }
      displayed.push({ layer, vIdx, effVis, depth: filtering ? 0 : getDepth(layer) });
    }

    shownEl.textContent = args.orderRightText || `${displayed.length} shown`;
    fTotalEl.textContent = String(layers.length);
    fVisEl.textContent = String(visibleCount);
    fExtraEl.textContent = String(args.extraMetricValue ?? 0);

    if (args.systemBase) {
      sysRow.hidden = false;
      sysTitle.textContent = args.systemBase.title || "Layer 0 · Mesh base";
      sysMeta.textContent = args.systemBase.meta || "";
      sysBadge.textContent = args.systemBase.badge || "Base";
    } else {
      sysRow.hidden = true;
    }

    const selIdx = layersTopDown.findIndex((l) => l.id === selectedId);
    actUp.disabled = selIdx <= 0;
    actDown.disabled = selIdx < 0 || selIdx >= layersTopDown.length - 1;
    actDup.disabled = !selectedId;
    actMask.disabled = !selectedId;
    actDel.disabled = !selectedId || layers.length <= 1;

    scrollInner.innerHTML = "";

    if (!displayed.length) {
      scrollInner.append(
        Construct("div", "tp-empty-state", [
          Construct("strong", null, filtering ? "No matching layers" : "No layers yet"),
          Construct("span", null, filtering ? "Clear filter or search query to view all layers." : "Use + above to add a layer."),
        ]),
      );
      return;
    }

    for (const entry of displayed) {
      const { layer, vIdx, effVis, depth } = entry;
      const isSelected = layer.id === selectedId;
      const isFolder = layer.kind === "folder";
      const isCollapsedFolder = isFolder && uiState.collapsedFolderIds.has(layer.id);
      const isDetailsOpen = !isFolder && uiState.expandedIds.has(layer.id);

      const group = Construct("div", "tp-layer-group" + (isSelected ? " is-selected" : ""));

      const row = Construct(
        "div",
        "stack-row tp-layer-row"
          + (isSelected ? " active is-selected" : "")
          + (!effVis ? " is-hidden" : "")
          + (layer.locked ? " is-locked" : "")
          + (isFolder ? " is-folder" : ""),
      );
      row.draggable = true;
      row.dataset.layerId = layer.id;
      row.style.setProperty("--layer-depth", String(depth));

      const handle = Construct("span", "stack-drag-handle", ConstructGlyph("Grip"));
      handle.title = "Drag to reorder layer";

      const expandBtn = Construct(
        "button",
        "tp-expand-btn" + (isFolder ? (isCollapsedFolder ? "" : " is-open") : (isDetailsOpen ? " is-open" : "")),
        ConstructGlyph(isFolder ? (isCollapsedFolder ? "ChevronRight" : "ChevronDown") : (isDetailsOpen ? "ChevronDown" : "ChevronRight")),
      );
      expandBtn.type = "button";
      expandBtn.title = isFolder ? "Collapse / Expand Folder" : "Toggle Layer Details";
      expandBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        if (isFolder) {
          if (uiState.collapsedFolderIds.has(layer.id)) uiState.collapsedFolderIds.delete(layer.id);
          else uiState.collapsedFolderIds.add(layer.id);
        } else {
          if (uiState.expandedIds.has(layer.id)) uiState.expandedIds.delete(layer.id);
          else uiState.expandedIds.add(layer.id);
        }
        render(latestArgs);
      });

      const thumb = Construct("div", "stack-row-thumb tp-layer-thumbnail");
      args.renderThumb?.(layer, thumb, { isFolder, isCollapsedFolder, isSelected });

      const titleEl = Construct("span", "stack-row-title", layer.name || "Untitled Layer");
      if (args.onRenameLayer) {
        titleEl.title = "Double-click to rename layer";
        titleEl.addEventListener("dblclick", (e) => {
          e.stopPropagation();
          const inp = document.createElement("input");
          inp.type = "text";
          inp.className = "tp-inline-rename";
          inp.value = layer.name || "";
          titleEl.replaceWith(inp);
          inp.focus?.();
          inp.select?.();
          let committed = false;
          const finish = (save) => {
            if (committed) return;
            committed = true;
            if (save) {
              const trimmed = inp.value.trim();
              if (trimmed && trimmed !== layer.name) args.onRenameLayer(layer.id, trimmed);
            }
            render(latestArgs);
          };
          inp.addEventListener("keydown", (ke) => {
            if (ke.key === "Enter") { ke.preventDefault(); finish(true); }
            else if (ke.key === "Escape") { ke.preventDefault(); finish(false); }
          });
          inp.addEventListener("blur", () => finish(true));
        });
      }

      const subText = isFolder
        ? `Folder · ${childCountMap.get(layer.id) || 0} item${(childCountMap.get(layer.id) || 0) === 1 ? "" : "s"}`
        : (args.getSubtitle?.(layer) || layer.kind.toUpperCase());

      const info = Construct("div", "stack-row-info", [
        titleEl,
        Construct("span", "stack-row-sub", subText),
      ]);

      // Opacity scrub capsule
      const opPct = Math.round((layer.opacity ?? 1) * 100);
      const opPill = Construct("span", "tp-opacity-pill", `${opPct}%`);
      opPill.title = args.onOpacityChange ? "Drag left/right to scrub layer opacity" : "Layer opacity";
      if (args.onOpacityChange) {
        opPill.addEventListener("pointerdown", (e) => {
          if (e.button !== 0 || layer.locked) return;
          e.stopPropagation();
          const startX = e.clientX;
          const startOp = layer.opacity ?? 1;
          opPill.setPointerCapture?.(e.pointerId);
          const onMove = (me) => {
            const next = Math.max(0, Math.min(1, startOp + (me.clientX - startX) / 110));
            opPill.textContent = `${Math.round(next * 100)}%`;
            args.onOpacityChange(layer.id, Math.round(next * 100) / 100, false);
          };
          const onUp = (ue) => {
            window.removeEventListener("pointermove", onMove);
            window.removeEventListener("pointerup", onUp);
            opPill.releasePointerCapture?.(ue.pointerId);
            const finalOp = Math.max(0, Math.min(1, startOp + (ue.clientX - startX) / 110));
            args.onOpacityChange(layer.id, Math.round(finalOp * 100) / 100, true);
          };
          window.addEventListener("pointermove", onMove);
          window.addEventListener("pointerup", onUp);
        });
      }

      // Up / Down reorder buttons (preserving exact .stack-order-btn selector!)
      const orderBox = Construct("div", "stack-order-btns");
      const upBtn = Construct("button", "stack-order-btn", ConstructGlyph("ArrowUp"));
      upBtn.type = "button";
      upBtn.title = "Move layer up (Alt+↑)";
      upBtn.disabled = vIdx === 0;
      upBtn.addEventListener("click", (e) => { e.stopPropagation(); args.onMoveLayer?.(layer.id, +1); });

      const dnBtn = Construct("button", "stack-order-btn", ConstructGlyph("ArrowDown"));
      dnBtn.type = "button";
      dnBtn.title = "Move layer down (Alt+↓)";
      dnBtn.disabled = vIdx === layersTopDown.length - 1;
      dnBtn.addEventListener("click", (e) => { e.stopPropagation(); args.onMoveLayer?.(layer.id, -1); });
      orderBox.append(upBtn, dnBtn);

      const visBtn = Construct("button", "stack-mini-btn tp-eye-btn" + (layer.visible !== false ? " on" : ""), ConstructGlyph(layer.visible !== false ? "Eye" : "EyeOff"));
      visBtn.type = "button";
      visBtn.title = layer.visible !== false ? "Hide layer" : "Show layer";
      visBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        args.onToggleVisible?.(layer.id);
      });

      const lockBtn = Construct("button", "stack-mini-btn tp-lock-btn" + (layer.locked ? " is-locked" : ""), ConstructGlyph(layer.locked ? "Lock" : "LockOpen"));
      lockBtn.type = "button";
      lockBtn.title = layer.locked ? "Unlock layer" : "Lock layer";
      lockBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        args.onToggleLock?.(layer.id);
      });

      const controls = Construct("div", "tp-row-controls", [
        opPill,
        orderBox,
        visBtn,
        lockBtn,
      ]);

      row.addEventListener("dragstart", (e) => {
        dragLayerId = layer.id;
        row.classList.add("dragging");
        if (e.dataTransfer) {
          e.dataTransfer.effectAllowed = "move";
          e.dataTransfer.setData("text/plain", layer.id);
        }
      });
      row.addEventListener("dragend", () => {
        dragLayerId = null;
        row.classList.remove("dragging", "drop-above", "drop-below");
      });
      row.addEventListener("dragover", (e) => {
        if (!dragLayerId || dragLayerId === layer.id) return;
        e.preventDefault();
        const r = row.getBoundingClientRect();
        const above = e.clientY < r.top + r.height * 0.5;
        row.classList.toggle("drop-above", above);
        row.classList.toggle("drop-below", !above);
      });
      row.addEventListener("dragleave", () => row.classList.remove("drop-above", "drop-below"));
      row.addEventListener("drop", (e) => {
        e.preventDefault();
        const src = dragLayerId || e.dataTransfer?.getData("text/plain");
        const r = row.getBoundingClientRect();
        const above = e.clientY < r.top + r.height * 0.5;
        row.classList.remove("drop-above", "drop-below");
        if (src && src !== layer.id) args.onDropReorder?.(src, layer.id, above);
      });

      row.addEventListener("click", () => {
        args.onSelectLayer?.(layer.id);
      });

      row.append(handle, expandBtn, thumb, info, controls);
      group.append(row);

      // Mask sub-row directly below layer (with live mask thumbnail!)
      const maskDesc = args.getMaskSubrow?.(layer);
      if (maskDesc) {
        const maskRow = Construct("div", "tp-mask-row" + (maskDesc.active ? " is-active" : "") + (maskDesc.enabled ? "" : " is-disabled"));
        maskRow.style.setProperty("--layer-depth", String(depth));
        const mThumb = Construct("div", "tp-mask-thumb");
        if (maskDesc.thumbEl) mThumb.append(maskDesc.thumbEl);
        else mThumb.append(ConstructGlyph("Mask"));
        const mInfo = Construct("div", "tp-subrow-info", [
          Construct("strong", null, maskDesc.label || "Layer mask"),
          Construct("small", null, maskDesc.meta || ""),
        ]);
        const mEye = Construct("button", "stack-mini-btn tp-eye-btn" + (maskDesc.enabled ? " on" : ""), ConstructGlyph(maskDesc.enabled ? "Eye" : "EyeOff"));
        mEye.type = "button";
        mEye.title = maskDesc.enabled ? "Disable mask" : "Enable mask";
        mEye.addEventListener("click", (e) => { e.stopPropagation(); maskDesc.onToggle?.(); });
        maskRow.addEventListener("click", (e) => { e.stopPropagation(); maskDesc.onSelect?.(); });
        maskRow.append(mThumb, mInfo, mEye);
        group.append(maskRow);
      }

      // Placed stamp / child sub-rows (e.g. movable stamps in Texture Paint!)
      const childSubrows = args.getChildSubrows?.(layer) || [];
      for (const child of childSubrows) {
        const cRow = Construct("div", "tp-decal-tree-row" + ((child.selected || child.active) ? " selected" : "") + (child.visible === false ? " is-hidden" : ""));
        cRow.style.setProperty("--layer-depth", String(depth));
        const cThumb = Construct("div", "tp-decal-tree-thumb");
        if (child.thumbEl) cThumb.append(child.thumbEl);
        else cThumb.append(ConstructGlyph("Stamp"));
        const cInfo = Construct("div", "tp-subrow-info", [
          Construct("strong", null, child.label || "Stamp"),
          Construct("small", null, child.meta || "Decal"),
        ]);
        const cEye = Construct("button", "stack-mini-btn tp-eye-btn" + (child.visible !== false ? " on" : ""), ConstructGlyph(child.visible !== false ? "Eye" : "EyeOff"));
        cEye.type = "button";
        cEye.addEventListener("click", (e) => { e.stopPropagation(); child.onToggle?.(); });
        cRow.addEventListener("click", (e) => { e.stopPropagation(); child.onSelect?.(); });
        cRow.append(cThumb, cInfo, cEye);
        group.append(cRow);
      }

      // Expandable layer details (channels / modifiers / quick actions)
      if (isDetailsOpen && args.renderExpanded) {
        const expEl = Construct("div", "tp-layer-expanded");
        expEl.style.setProperty("--layer-depth", String(depth));
        args.renderExpanded(layer, expEl);
        group.append(expEl);
      }

      scrollInner.append(group);
    }
  }

  return {
    render,
    expandLayerDetails: (id) => { if (id) uiState.expandedIds.add(id); },
    closeAddMenu: () => setAddMenuOpen(false),
    dispose: () => document.removeEventListener?.("pointerdown", onDocPointerDown),
    uiState,
  };
}
