// Left dock: scene outliner with search, filter chips, groups, visibility.
import {
  state,
  on,
  emit,
  select,
  LayerOrder,
  LayerLabel,
  beginChange,
} from "./state.js";

const root = document.getElementById("outliner");

const filter = { text: "", layers: new Set() };
const collapsed = new Set();

const Icons = {
  terrain: '<svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4"><path d="M1.5 11.5 5 5l3 4 2.5-3 4 5.5z"/></svg>',
  splines: '<svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4"><path d="M2 13C5 13 5 3 8 3s3 10 6 10"/></svg>',
  props: '<svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4"><circle cx="8" cy="8" r="5"/><path d="M8 3v10M3 8h10"/></svg>',
  labels: '<svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4"><path d="M3 4h10M8 4v8M6 12h4"/></svg>',
  notes: '<svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4"><path d="M3 3h10v7l-3 3H3z"/><path d="M10 13v-3h3"/></svg>',
};

const Eye = (on_) =>
  on_
    ? '<svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.3"><path d="M1.5 8s2.5-4 6.5-4 6.5 4 6.5 4-2.5 4-6.5 4-6.5-4-6.5-4z"/><circle cx="8" cy="8" r="1.8"/></svg>'
    : '<svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.3"><path d="M2 8s2.5-4 6-4c1.2 0 2.2.4 3.1 1M14 8s-2.5 4-6 4c-1.2 0-2.3-.4-3.2-1M2 2l12 12"/></svg>';

function nameOf(layer, e) {
  if (layer === "splines") return e.name;
  if (layer === "labels") return e.text;
  if (layer === "notes") return e.title;
  if (layer === "terrain") return `${e.material} stroke`;
  if (layer === "props") return e.kind.replace(/-/g, " ");
  return e.id;
}
function subOf(layer, e) {
  if (layer === "splines") return e.kind;
  if (layer === "props") return "";
  if (layer === "terrain") return `${e.points.length}pts`;
  if (layer === "labels") return e.sub || "";
  if (layer === "notes") return "";
  return "";
}

function matches(layer, e) {
  if (filter.layers.size && !filter.layers.has(layer)) return false;
  if (!filter.text) return true;
  const q = filter.text.toLowerCase();
  return nameOf(layer, e).toLowerCase().includes(q) || subOf(layer, e).toLowerCase().includes(q);
}

function row(layer, e) {
  const sel = state.selection?.layer === layer && state.selection?.id === e.id;
  const div = document.createElement("div");
  div.className = `outliner-row${sel ? " selected" : ""}${e.hidden ? " hidden-row" : ""}`;
  div.tabIndex = 0;
  div.dataset.layer = layer;
  div.dataset.id = e.id;
  div.innerHTML = `
    <span class="row-icon">${Icons[layer]}</span>
    <span class="row-name"></span>
    <small></small>
    <button class="visibility" title="Show / hide">${Eye(!e.hidden)}</button>`;
  div.querySelector(".row-name").textContent = nameOf(layer, e);
  div.querySelector("small").textContent = subOf(layer, e);
  div.addEventListener("click", (ev) => {
    if (ev.target.closest(".visibility")) return;
    select(layer, e.id);
  });
  div.addEventListener("dblclick", (ev) => {
    if (ev.target.closest(".visibility")) return;
    select(layer, e.id);
    setTimeout(() => document.querySelector("#inspector input")?.select(), 30);
  });
  div.querySelector(".visibility").addEventListener("click", (ev) => {
    ev.stopPropagation();
    beginChange();
    e.hidden = !e.hidden;
    emit();
  });
  return div;
}

export function renderOutliner() {
  const counts = {};
  for (const l of LayerOrder) counts[l] = state.doc.layers[l].length;
  const total = LayerOrder.reduce((s, l) => s + counts[l], 0);
  const visible = LayerOrder.reduce(
    (s, l) => s + state.doc.layers[l].filter((e) => !e.hidden).length,
    0,
  );

  root.innerHTML = `
    <div class="outliner-heading">
      <h1>Outliner</h1>
      <small>${state.doc.name}</small>
    </div>
    <div class="stats">
      <button type="button" tabindex="-1">
        <span class="status-disc">✓</span>
        <small>Visible</small><strong>${visible}</strong>
      </button>
      <button type="button" tabindex="-1">
        <span class="muted-disc">○</span>
        <small>Hidden</small><strong>${total - visible}</strong>
      </button>
    </div>
    <div class="outliner-search">
      <label>
        <svg width="13" height="13" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4"><circle cx="7" cy="7" r="4.5"/><path d="m10.5 10.5 4 4"/></svg>
        <input type="text" placeholder="Filter objects…" value="${filter.text.replace(/"/g, "&quot;")}" />
      </label>
    </div>
    <div class="filter-chips">
      ${LayerOrder.map(
        (l) =>
          `<button data-chip="${l}" class="${filter.layers.has(l) ? "on" : ""}">${LayerLabel[l]} · ${counts[l]}</button>`,
      ).join("")}
    </div>
    <div class="outliner-rows" role="tree"></div>
    <div class="outliner-footer">
      <span><small>TOOL</small><b>${state.tool}</b></span>
      <span><small>SELECTION</small><b>${state.selection ? nameOf(state.selection.layer, state.doc.layers[state.selection.layer].find((e) => e.id === state.selection.id) || {}) : "—"}</b></span>
      <span><small>ZOOM</small><b>${Math.round(state.cam.zoom * 100)}%</b></span>
    </div>`;

  const input = root.querySelector(".outliner-search input");
  input.addEventListener("input", () => {
    filter.text = input.value;
    renderRows();
  });
  root.querySelectorAll("[data-chip]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const l = btn.dataset.chip;
      filter.layers.has(l) ? filter.layers.delete(l) : filter.layers.add(l);
      renderOutliner();
    });
  });
  renderRows();
}

function renderRows() {
  const box = root.querySelector(".outliner-rows");
  if (!box) return;
  box.innerHTML = "";
  let shown = 0;
  for (const layer of LayerOrder) {
    const all = state.doc.layers[layer];
    const list = all.filter((e) => matches(layer, e));
    if (filter.layers.size && !filter.layers.has(layer)) continue;
    const head = document.createElement("div");
    head.className = "group-header";
    head.innerHTML = `<span>${LayerLabel[layer]}</span><span class="count">${list.length}${list.length !== all.length ? ` / ${all.length}` : ""}</span>`;
    head.addEventListener("click", () => {
      collapsed.has(layer) ? collapsed.delete(layer) : collapsed.add(layer);
      renderRows();
    });
    box.append(head);
    if (collapsed.has(layer)) continue;
    for (const e of list) {
      box.append(row(layer, e));
      shown++;
    }
  }
  if (!shown) {
    box.insertAdjacentHTML("beforeend", `<p class="empty-results">No matching objects</p>`);
  }
}

on(renderOutliner);
