// Top notch: level file actions + saved level list.
import { state, on, serialize, loadDoc, blankDoc, emit, beginChange } from "./state.js";

const shell = document.getElementById("notch-shell");
const handle = document.getElementById("notch-handle");
const dash = document.getElementById("notch-dashboard");

let levelsCache = [];

async function fetchLevels() {
  try {
    const res = await fetch("/api/levels");
    const data = await res.json();
    levelsCache = data.levels || [];
  } catch {
    levelsCache = [];
  }
  return levelsCache;
}

function icon(path) {
  return `<span class="glyph"><svg width="18" height="18" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4"><path d="${path}"/></svg></span>`;
}

export async function renderNotch() {
  const levels = await fetchLevels();
  dash.innerHTML = `
    <header>
      <span>Level file</span>
      <button id="notch-close" title="Close">×</button>
    </header>
    <div class="dashboard-grid">
      <button id="nb-new">${icon("M8 2v8M5 7l3 3 3-3M2 13h12")}New</button>
      <button id="nb-save" class="primary-wrap">${icon("M3 3h8l2 2v8H3zM5 3v4h6V3M5 13v-4h6v4")}Save</button>
      <button id="nb-dl">${icon("M8 2v8M5 7l3 3 3-3M3 13h10")}Export</button>
      <button id="nb-open">${icon("M2 5h5l1.5 1.5H14v7H2zM2 5V3.5h4L7.5 5")}Import</button>
    </div>
    <div class="saved-levels">
      <h4>SAVED IN LEVELS/</h4>
      ${
        levels.length
          ? levels
              .map(
                (n) =>
                  `<button class="level-file" data-level="${n}">▸ ${n}</button>`,
              )
              .join("")
          : `<p>No saved levels yet — hit Save to write the first one.</p>`
      }
    </div>`;

  dash.querySelector("#notch-close").addEventListener("click", () => toggleNotch(false));
  dash.querySelector("#nb-new").addEventListener("click", () => {
    if (state.dirty && !confirm("Discard unsaved changes and start a new level?")) return;
    loadDoc(blankDoc("New level"));
    toggleNotch(false);
  });
  dash.querySelector("#nb-save").addEventListener("click", () => window.shadowBox?.save());
  dash.querySelector("#nb-dl").addEventListener("click", () => window.shadowBox?.download());
  dash.querySelector("#nb-open").addEventListener("click", () => document.getElementById("file-input").click());
  dash.querySelectorAll("[data-level]").forEach((b) =>
    b.addEventListener("click", async () => {
      try {
        const res = await fetch(`/api/levels/${b.dataset.level}`);
        if (!res.ok) throw new Error(await res.text());
        loadDoc(await res.json(), b.dataset.level);
        toggleNotch(false);
        window.shadowBox?.toast(`Loaded ${b.dataset.level}`);
      } catch (err) {
        window.shadowBox?.toast(`Load failed: ${err.message}`);
      }
    }),
  );
}

function toggleNotch(open) {
  shell.classList.toggle("open", open);
  handle.setAttribute("aria-expanded", String(open));
  if (open) renderNotch();
}

handle.addEventListener("click", () => toggleNotch(!shell.classList.contains("open")));
document.getElementById("notch-shade").addEventListener("pointerdown", (e) => {
  if (e.target.id === "notch-shade") toggleNotch(false);
});
window.addEventListener("keydown", (e) => {
  if (e.key === "Escape" && shell.classList.contains("open")) toggleNotch(false);
});

on(() => {
  // keep the header name fresh while open
  if (shell.classList.contains("open")) renderNotch();
});
