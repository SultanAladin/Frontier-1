// Centre viewport toolbar: identity, tools, context controls, view toggles.
import {
  state,
  on,
  emit,
  setTool,
  Materials,
  SplineKinds,
} from "./state.js";
import { fitView, zoomBy, finishDraft, cancelDraft } from "./scene.js";

const root = document.getElementById("toolbar");

const Tools = [
  { id: "select", label: "Select", key: "V", icon: "M4 2l9 8-4 .6L11 15l-2 1-2.6-4.4L4 14z" },
  { id: "terrain", label: "Ground", key: "B", icon: "M2 12c2-5 4-7 6-7s4 2 6 7M2 12h12" },
  { id: "spline", label: "Spline", key: "S", icon: "M2 13C5 13 5 3 8 3s3 10 6 10" },
  { id: "prop", label: "Prop", key: "P", icon: "M8 2a6 6 0 100 12A6 6 0 008 2zM8 5v6M5 8h6" },
  { id: "label", label: "Label", key: "L", icon: "M3 4h10M8 4v8M6 12h4" },
  { id: "note", label: "Note", key: "N", icon: "M3 3h10v7l-3 3H3zM10 13v-3h3" },
];

function contextHtml() {
  if (state.tool === "terrain") {
    const mat = state.draft?.material || "grass";
    const width = state.draft?.width || 46;
    return `
      ${Object.entries(Materials)
        .map(
          ([k, v]) =>
            `<button class="swatch ${k === mat ? "on" : ""}" data-mat="${k}" title="${v.label}"><i style="background:${v.colour}"></i></button>`,
        )
        .join("")}
      <span class="viewport-tool-divider"></span>
      <input type="range" id="brush-size" min="8" max="240" step="4" value="${width}" style="--fill:${((width - 8) / 232) * 100}%" title="Brush width" />
      <span class="hint">Drag to paint ground · under everything</span>`;
  }
  if (state.tool === "spline") {
    const kind = state.draft?.kind || "path";
    const n = state.draft?.points?.length || 0;
    return `
      ${Object.entries(SplineKinds)
        .map(
          ([k, v]) =>
            `<button class="kind-chip ${k === kind ? "on" : ""}" data-kind="${k}">${v.label}</button>`,
        )
        .join("")}
      <span class="viewport-tool-divider"></span>
      <span class="hint">${
        n
          ? `${n} point${n > 1 ? "s" : ""} — click to add, Enter/double-click to finish, Esc cancels`
          : "Click to lay points for road / river / trail / vehicle route"
      }</span>`;
  }
  if (state.tool === "prop") {
    return `<span class="hint">Click the map to place — Shift skips scatter jitter · pick more in the Prop Library below</span>`;
  }
  if (state.tool === "label") return `<span class="hint">Click the map to drop a label</span>`;
  if (state.tool === "note") return `<span class="hint">Click the map to pin a design note</span>`;
  return `<span class="hint">Drag to move · spline handles to sculpt · Space or middle-drag pans · wheel zooms</span>`;
}

export function renderToolbar() {
  const counts = Object.values(state.doc.layers).reduce((s, a) => s + a.length, 0);
  root.innerHTML = `
    <div class="viewport-heading-row">
      <div class="viewport-identity">
        <div>
          <strong>${escapeHtml(state.doc.name)}</strong>
          <span>TOP-DOWN · 2D LEVEL BLOCKOUT</span>
        </div>
        <span class="viewport-preview-badge">${counts} OBJECTS</span>
      </div>
      <div class="viewport-modes" role="group" aria-label="Tools">
        ${Tools.map(
          (t) =>
            `<button data-tool="${t.id}" class="${state.tool === t.id ? "active" : ""}" title="${t.label} (${t.key})">
              <svg width="13" height="13" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4"><path d="${t.icon}"/></svg>
              <span>${t.label}</span><kbd>${t.key}</kbd>
            </button>`,
        ).join("")}
      </div>
    </div>
    <div class="viewport-action-row">
      <div class="viewport-context">${contextHtml()}</div>
      <div class="viewport-secondary-tools">
        <button class="viewport-icon-button" id="tb-undo" title="Undo (Ctrl+Z)">
          <svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4"><path d="M6 3 2.5 6.5 6 10M2.5 6.5H10a3.5 3.5 0 010 7H7"/></svg>
        </button>
        <button class="viewport-icon-button" id="tb-redo" title="Redo (Ctrl+Shift+Z)">
          <svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4"><path d="M10 3l3.5 3.5L10 10M13.5 6.5H6a3.5 3.5 0 000 7h3"/></svg>
        </button>
        <button class="viewport-icon-button" id="tb-grid" aria-pressed="${state.gridOn}" title="Toggle grid (G)">
          <svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2"><path d="M2 6h12M2 10h12M6 2v12M10 2v12"/></svg>
        </button>
        <button class="viewport-icon-button" id="tb-fit" title="Fit view (F)">
          <svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4"><path d="M2 5V2h3M14 5V2h-3M2 11v3h3M14 11v3h-3"/></svg>
        </button>
        <button class="viewport-icon-button" id="tb-zin" title="Zoom in">+</button>
        <button class="viewport-icon-button" id="tb-zout" title="Zoom out">−</button>
      </div>
    </div>`;

  root.querySelectorAll("[data-tool]").forEach((b) =>
    b.addEventListener("click", () => setTool(b.dataset.tool)),
  );
  root.querySelectorAll("[data-mat]").forEach((b) =>
    b.addEventListener("click", () => {
      state.draft = { ...(state.draft || {}), material: b.dataset.mat };
      emit();
    }),
  );
  root.querySelectorAll("[data-kind]").forEach((b) =>
    b.addEventListener("click", () => {
      state.draft = { ...(state.draft || {}), kind: b.dataset.kind };
      emit();
    }),
  );
  const brush = root.querySelector("#brush-size");
  brush?.addEventListener("input", () => {
    state.draft = { ...(state.draft || {}), width: +brush.value };
    brush.style.setProperty("--fill", `${((+brush.value - 8) / 232) * 100}%`);
  });
  root.querySelector("#tb-undo").addEventListener("click", () => window.shadowBox?.undo());
  root.querySelector("#tb-redo").addEventListener("click", () => window.shadowBox?.redo());
  root.querySelector("#tb-grid").addEventListener("click", () => {
    state.gridOn = !state.gridOn;
    emit();
  });
  root.querySelector("#tb-fit").addEventListener("click", () => fitView());
  root.querySelector("#tb-zin").addEventListener("click", () => zoomBy(1.25));
  root.querySelector("#tb-zout").addEventListener("click", () => zoomBy(0.8));
}

function escapeHtml(s) {
  return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/"/g, "&quot;");
}

on(renderToolbar);
