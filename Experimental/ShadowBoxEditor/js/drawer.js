// Bottom drawer: prop library with categories + search; click picks the kind.
import { state, on, emit, setTool } from "./state.js";
import { Props, PropCategories, PropKinds, propArt } from "./props.js";

const shell = document.getElementById("drawer-shell");
const handle = document.getElementById("drawer-handle");
const surface = document.getElementById("drawer-surface");

const ui = { category: "All", query: "" };

function thumbSvg(kind) {
  const meta = Props[kind];
  const r = meta.radius + 4;
  return `<svg viewBox="${-r} ${-r} ${r * 2} ${r * 2}" aria-hidden="true">
    <ellipse cx="${meta.radius * 0.4}" cy="${meta.radius * 0.45}" rx="${meta.radius * 0.9}" ry="${meta.radius * 0.7}" fill="#000" opacity=".3"/>
    <g>${meta.art}</g>
  </svg>`;
}

function render() {
  const kinds = PropKinds.filter((k) => {
    const m = Props[k];
    if (ui.category !== "All" && m.category !== ui.category) return false;
    if (ui.query && !m.label.toLowerCase().includes(ui.query.toLowerCase())) return false;
    return true;
  });
  surface.innerHTML = `
    <div class="asset-browser">
      <header class="asset-browser-header">
        <div>
          <span class="asset-eyebrow">SHADOWBOX / CONTENT</span>
          <h1>Prop Library <small>top-down SVG · animated · shadowed</small></h1>
        </div>
        <button id="drawer-close" title="Close">×</button>
      </header>
      <div class="asset-browser-tools">
        <div class="asset-category-rail">
          ${["All", ...PropCategories]
            .map(
              (c) =>
                `<button data-cat="${c}" class="${ui.category === c ? "on" : ""}">${c}</button>`,
            )
            .join("")}
        </div>
        <label class="asset-search-wrap">
          <svg width="12" height="12" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4"><circle cx="7" cy="7" r="4.5"/><path d="m10.5 10.5 4 4"/></svg>
          <input type="text" placeholder="Search props…" value="${ui.query.replace(/"/g, "&quot;")}" />
        </label>
      </div>
      <div class="asset-results">
        ${kinds
          .map(
            (k) => `
          <div class="asset-thumb ${state.placing === k ? "on" : ""}" data-kind="${k}" title="${Props[k].label}">
            <span class="thumb-art">${thumbSvg(k)}</span>
            <span>${Props[k].label}</span>
          </div>`,
          )
          .join("") || `<p class="empty-results">No props match</p>`}
      </div>
    </div>`;

  surface.querySelector("#drawer-close").addEventListener("click", () => toggle(false));
  surface.querySelectorAll("[data-cat]").forEach((b) =>
    b.addEventListener("click", () => {
      ui.category = b.dataset.cat;
      render();
    }),
  );
  const search = surface.querySelector(".asset-search-wrap input");
  search.addEventListener("input", () => {
    ui.query = search.value;
    render();
    surface.querySelector(".asset-search-wrap input")?.focus();
  });
  surface.querySelectorAll("[data-kind]").forEach((b) =>
    b.addEventListener("click", () => {
      state.placing = b.dataset.kind;
      setTool("prop");
      render();
      window.shadowBox?.toast(`Placing ${Props[b.dataset.kind].label} — click the map (Shift = precise)`);
    }),
  );
}

function toggle(open) {
  shell.classList.toggle("open", open);
  handle.setAttribute("aria-expanded", String(open));
  if (open) render();
}

handle.addEventListener("click", () => toggle(!shell.classList.contains("open")));

export function openDrawer() {
  toggle(true);
}

on(() => {
  if (shell.classList.contains("open")) {
    surface.querySelectorAll(".asset-thumb").forEach((t) =>
      t.classList.toggle("on", t.dataset.kind === state.placing),
    );
  }
});
