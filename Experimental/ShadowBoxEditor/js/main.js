// ShadowBox entry point: status bar, keyboard, save/load, boot.
import {
  state,
  on,
  emit,
  setTool,
  undo,
  redo,
  canUndo,
  canRedo,
  serialize,
  loadDoc,
  blankDoc,
  counts,
  removeSelection,
  select,
} from "./state.js";
import {
  buildScene,
  renderScene,
  fitView,
  zoomBy,
  cancelDraft,
  finishDraft,
} from "./scene.js";
import { renderOutliner } from "./outliner.js";
import { renderInspector } from "./inspector.js";
import { renderToolbar } from "./toolbar.js";
import { renderNotch } from "./notch.js";
import { openDrawer } from "./drawer.js";

// ------------------------------------------------------------------ toast
let toastTimer = 0;
function toast(msg) {
  const node = document.getElementById("toast");
  node.textContent = msg;
  node.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => node.classList.remove("show"), 2600);
}

// ---------------------------------------------------------------- status
function renderStatus() {
  const c = counts();
  const sel = state.selection
    ? `${state.selection.layer} · ${state.selection.id}`
    : "—";
  document.getElementById("status").innerHTML = `
    <span><b>${state.dirty ? "● unsaved" : "○ saved"}</b></span>
    <span>TOOL <b>${state.tool}</b></span>
    <span>SELECTED <b>${sel}</b></span>
    <span>OBJECTS <b>${c.total}</b></span>
    <span>ZOOM <b>${Math.round(state.cam.zoom * 100)}%</b></span>
    <span>GRID <b>${state.gridOn ? state.doc.settings.grid + "u" : "off"}</b></span>`;
}
on(renderStatus);
window.addEventListener("shadowbox:view", renderStatus);

// ---------------------------------------------------------------- file io
async function save() {
  try {
    const res = await fetch("/api/save", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ name: state.doc.name, doc: JSON.parse(serialize()) }),
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || res.statusText);
    state.file = data.file;
    state.dirty = false;
    emit();
    toast(`Saved levels/${data.file} — commit & push to share`);
  } catch (err) {
    toast(`Save failed (${err.message}) — falling back to download`);
    download();
  }
}

function download() {
  const blob = new Blob([serialize()], { type: "application/json" });
  const a = document.createElement("a");
  const slug =
    state.doc.name.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") ||
    "level";
  a.href = URL.createObjectURL(blob);
  a.download = `${slug}.json`;
  a.click();
  URL.revokeObjectURL(a.href);
  toast(`Downloaded ${slug}.json`);
}

const fileInput = document.getElementById("file-input");
fileInput.addEventListener("change", async () => {
  const file = fileInput.files?.[0];
  if (!file) return;
  try {
    loadDoc(JSON.parse(await file.text()), file.name);
    fitView();
    toast(`Opened ${file.name}`);
  } catch (err) {
    toast(`Import failed: ${err.message}`);
  }
  fileInput.value = "";
});

window.shadowBox = { save, download, undo, redo, toast, openDrawer };

// ---------------------------------------------------------------- keys
const toolKeys = { v: "select", b: "terrain", s: "spline", p: "prop", l: "label", n: "note" };

function typing(e) {
  const t = e.target;
  return t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.tagName === "SELECT" || t.isContentEditable);
}

window.addEventListener("keydown", (e) => {
  if (typing(e)) {
    if (e.key === "Escape") e.target.blur();
    return;
  }
  const mod = e.ctrlKey || e.metaKey;
  if (mod && e.key.toLowerCase() === "z") {
    e.preventDefault();
    e.shiftKey ? redo() : undo();
    return;
  }
  if (mod && e.key.toLowerCase() === "y") {
    e.preventDefault();
    redo();
    return;
  }
  if (mod && e.key.toLowerCase() === "s") {
    e.preventDefault();
    save();
    return;
  }
  if (mod && e.key.toLowerCase() === "d") {
    e.preventDefault();
    document.getElementById("ent-dup")?.click();
    return;
  }
  if (mod) return;
  switch (e.key) {
    case "Escape":
      if (cancelDraft()) return;
      select(null, null);
      return;
    case "Enter":
      finishDraft();
      return;
    case "Backspace":
      if (state.tool === "spline" && state.draft?.points?.length) {
        state.draft.points.pop();
        renderScene();
        emit();
        e.preventDefault();
      } else if (state.tool === "spline" && state.draft) {
        state.draft = null;
        renderScene();
      } else if (removeSelection()) {
        e.preventDefault();
      }
      return;
    case "Delete":
      if (removeSelection()) e.preventDefault();
      return;
    case "f":
      fitView();
      return;
    case "g":
      state.gridOn = !state.gridOn;
      emit();
      return;
    case "Tab":
      e.preventDefault();
      openDrawer();
      return;
    default:
      break;
  }
  const k = e.key.toLowerCase();
  if (toolKeys[k]) setTool(toolKeys[k]);
});

// dbl-click empty canvas with spline tool finishes the spline (handled in scene).

// ---------------------------------------------------------------- dividers
document.querySelectorAll(".divider").forEach((div) => {
  div.addEventListener("pointerdown", (ev) => {
    const side = div.dataset.side;
    const workspace = document.getElementById("workspace");
    const startX = ev.clientX;
    const startW = parseFloat(
      getComputedStyle(workspace).getPropertyValue(side === "left" ? "--left" : "--right"),
    );
    const move = (e) => {
      const dx = e.clientX - startX;
      const w = side === "left" ? startW + dx : startW - dx;
      workspace.style.setProperty(
        side === "left" ? "--left" : "--right",
        `${Math.min(520, Math.max(200, w))}px`,
      );
      renderScene();
    };
    const up = () => {
      document.body.classList.remove("resizing");
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", up);
    };
    document.body.classList.add("resizing");
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", up);
    ev.preventDefault();
  });
});

// ---------------------------------------------------------------- boot
async function boot() {
  buildScene();
  renderOutliner();
  renderToolbar();
  renderInspector();
  renderStatus();
  renderNotch();

  // try the worked example on first run
  try {
    const res = await fetch("./levels/example-crossroads.json");
    if (res.ok) {
      loadDoc(await res.json());
      toast("Loaded example-crossroads.json — press S for splines");
    } else {
      loadDoc(blankDoc("River crossing"));
    }
  } catch {
    loadDoc(blankDoc("River crossing"));
  }

  renderOutliner();
  renderToolbar();
  renderInspector();
  renderScene();
  fitView();
  emit();

  // drag-and-drop a level .json anywhere
  window.addEventListener("dragover", (e) => e.preventDefault());
  window.addEventListener("drop", async (e) => {
    e.preventDefault();
    const file = e.dataTransfer?.files?.[0];
    if (!file || !file.name.endsWith(".json")) return;
    try {
      loadDoc(JSON.parse(await file.text()), file.name);
      fitView();
      toast(`Opened ${file.name}`);
    } catch (err) {
      toast(`Import failed: ${err.message}`);
    }
  });

  console.info("ShadowBox ready — tools: V B S P L N");
}

boot();
