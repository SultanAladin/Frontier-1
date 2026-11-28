// Right dock: property inspector — level settings + per-entity cards.
import {
  state,
  on,
  emit,
  beginChange,
  pushSnapshot,
  snapshot,
  removeSelection,
  SplineKinds,
  Materials,
} from "./state.js";
import { renderScene } from "./scene.js";
import { Props, PropCategories, PropKinds } from "./props.js";

const root = document.getElementById("inspector");
const footer = document.getElementById("inspector-footer");

// Commit helper for gesture-style inputs (sliders, text): mutate silently while
// the gesture runs — full emit only on `change`, so focus/drag survive.
function gestureInput(input, apply) {
  let stashed = null;
  const stash = () => {
    if (stashed === null) stashed = snapshot();
  };
  input.addEventListener("focus", stash);
  input.addEventListener("pointerdown", stash);
  input.addEventListener("input", () => {
    stash();
    apply();
    state.dirty = true;
    renderScene();
  });
  input.addEventListener("change", () => {
    if (stashed !== null) {
      pushSnapshot(stashed);
      stashed = null;
    }
    apply();
    state.dirty = true;
    emit();
  });
}

function field(label, control, hint = "") {
  return `<div class="field-row"><label>${label}</label><div class="control">${control}</div>${
    hint ? `<small>${hint}</small>` : ""
  }</div>`;
}
function num(value, attrs = "") {
  return `<input type="number" value="${value}" ${attrs} />`;
}

function heading(eyebrow, title) {
  return `<div class="inspector-heading">
    <div class="breadcrumbs">${eyebrow}</div>
    <h1>${title}</h1>
  </div>`;
}

// ------------------------------------------------------------- level card
function levelCard() {
  const s = state.doc.settings;
  const wrap = document.createElement("div");
  wrap.innerHTML = `
    ${heading("World / Settings", escapeHtml(state.doc.name))}
    <div class="section-caption">Level <small>${state.doc.format} v${state.doc.version}</small></div>
    <div class="property-card">
      <h3>Identity</h3>
      ${field("Name", `<input type="text" id="lv-name" value="${escapeHtml(state.doc.name)}" />`)}
      ${field(
        "Grid size",
        num(s.grid, 'id="lv-grid" min="4" max="512" step="4"'),
        "world units",
      )}
      ${field(
        "Snap",
        `<label class="toggle"><input type="checkbox" id="lv-snap" ${s.snap ? "checked" : ""}/> Snap to grid</label>`,
      )}
    </div>
    <div class="property-card">
      <h3>Top-down light</h3>
      <p>Ground shadows stay flat on the map — they lean away from the sun, never billboards.</p>
      ${field(
        "Sun angle",
        `<input type="range" id="lv-sun" min="0" max="360" step="1" value="${s.sunAngle}" style="--fill:${(s.sunAngle / 360) * 100}%"/> <span class="readout" id="lv-sun-ro">${s.sunAngle}°</span>`,
      )}
      ${field(
        "Shadow length",
        `<input type="range" id="lv-shl" min="0" max="1.6" step="0.05" value="${s.shadowLength}" style="--fill:${(s.shadowLength / 1.6) * 100}%"/> <span class="readout" id="lv-shl-ro">${Number(s.shadowLength).toFixed(2)}</span>`,
      )}
      ${field("Backdrop", `<input type="color" id="lv-bg" value="${s.background}" />`)}
    </div>
    <div class="property-card">
      <h3>How to use this tool</h3>
      <p>Draw ground with <b>B</b>, lay roads/rivers/routes with the <b>spline tool (S)</b> — click points, press <b>Enter</b> to finish. Place props from the bottom <b>Prop Library</b> (<b>P</b>), drop labels (<b>L</b>) and design notes (<b>N</b>).</p>
      <p>Every level saves as JSON into <code>levels/</code> — commit &amp; push and the file shows up on GitHub for review.</p>
      <div class="card-actions">
        <button class="primary" id="lv-save">Save to repo</button>
        <button id="lv-download">Download .json</button>
      </div>
    </div>`;
  wire(wrap);
  return wrap;
}

// ----------------------------------------------------------- entity cards
function entityCard() {
  const { layer, id } = state.selection;
  const ent = state.doc.layers[layer].find((e) => e.id === id);
  if (!ent) return levelCard();
  const wrap = document.createElement("div");
  const titles = { terrain: "Terrain stroke", splines: "Spline", props: "Prop", labels: "Label", notes: "Note" };
  const sub =
    layer === "splines"
      ? SplineKinds[ent.kind]?.label || ent.kind
      : layer === "props"
        ? Props[ent.kind]?.label || ent.kind
        : layer === "terrain"
          ? Materials[ent.material]?.label || ent.material
          : titles[layer];
  wrap.innerHTML = `${heading(
    `${titles[layer]} / ${escapeHtml(String(sub))}`,
    escapeHtml(displayName(layer, ent)),
  )}`;

  const card = document.createElement("div");
  card.className = "property-card";
  card.innerHTML = `<h3>Transform</h3>`;

  if ("x" in ent) {
    card.insertAdjacentHTML(
      "beforeend",
      field(
        "Position",
        `<div class="pair">${num(Math.round(ent.x), 'data-px="x" min="-99999" max="99999"')}${num(
          Math.round(ent.y),
          'data-px="y" min="-99999" max="99999"',
        )}</div>`,
      ),
    );
  }
  if (layer === "props") {
    card.insertAdjacentHTML(
      "beforeend",
      field(
        "Rotation",
        `<input type="range" data-pr="rotation" min="-180" max="180" step="1" value="${ent.rotation || 0}" style="--fill:${(((ent.rotation || 0) + 180) / 360) * 100}%"/> <span class="readout">${Math.round(ent.rotation || 0)}°</span>`,
      ),
      );
    card.insertAdjacentHTML(
      "beforeend",
      field(
        "Scale",
        `<input type="range" data-pr="scale" min="0.3" max="3" step="0.05" value="${ent.scale || 1}" style="--fill:${(((ent.scale || 1) - 0.3) / 2.7) * 100}%"/> <span class="readout">${(ent.scale || 1).toFixed(2)}×</span>`,
      ),
    );
    card.insertAdjacentHTML(
      "beforeend",
      field("Kind", `<select data-pr="kind">${PropKinds.map(
        (k) =>
          `<option value="${k}" ${k === ent.kind ? "selected" : ""}>${Props[k].label} · ${Props[k].category}</option>`,
      ).join("")}</select>`),
    );
  }
  if (layer === "splines") {
    card.insertAdjacentHTML(
      "beforeend",
      field(
        "Kind",
        `<select data-pr="kind">${Object.entries(SplineKinds).map(
          ([k, v]) => `<option value="${k}" ${k === ent.kind ? "selected" : ""}>${v.label}</option>`,
        ).join("")}</select>`,
      ),
    );
    card.insertAdjacentHTML(
      "beforeend",
      field("Name", `<input type="text" data-pr="name" value="${escapeHtml(ent.name || "")}" />`),
    );
    card.insertAdjacentHTML(
      "beforeend",
      field(
        "Width",
        `<input type="range" data-pr="width" min="4" max="160" step="1" value="${ent.width}" style="--fill:${((ent.width - 4) / 156) * 100}%"/> <span class="readout">${ent.width}</span>`,
        "world units",
      ),
    );
    card.insertAdjacentHTML(
      "beforeend",
      field(
        "Smoothness",
        `<input type="range" data-pr="tension" min="0" max="1" step="0.05" value="${ent.tension ?? 0.5}" style="--fill:${(ent.tension ?? 0.5) * 100}%"/> <span class="readout">${(ent.tension ?? 0.5).toFixed(2)}</span>`,
      ),
    );
    card.insertAdjacentHTML(
      "beforeend",
      field(
        "Closed loop",
        `<label class="toggle"><input type="checkbox" data-pr="closed" ${ent.closed ? "checked" : ""}/> Loop back to start</label>`,
      ),
    );
    card.insertAdjacentHTML(
      "beforeend",
      `<div class="section-caption" style="margin-top:14px">Control points <small>${ent.points.length} · drag on canvas, Alt-click removes</small></div>`,
    );
  }
  if (layer === "terrain") {
    card.insertAdjacentHTML(
      "beforeend",
      field(
        "Material",
        `<select data-pr="material">${Object.entries(Materials).map(
          ([k, v]) => `<option value="${k}" ${k === ent.material ? "selected" : ""}>${v.label}</option>`,
        ).join("")}</select>`,
      ),
    );
    card.insertAdjacentHTML(
      "beforeend",
      field(
        "Width",
        `<input type="range" data-pr="width" min="6" max="300" step="2" value="${ent.width}" style="--fill:${((ent.width - 6) / 294) * 100}%"/> <span class="readout">${ent.width}</span>`,
      ),
    );
    card.insertAdjacentHTML(
      "beforeend",
      `<div class="section-caption" style="margin-top:14px">Stroke <small>${ent.points.length} points</small></div>`,
    );
  }
  if (layer === "labels") {
    card.insertAdjacentHTML(
      "beforeend",
      field("Text", `<input type="text" data-pr="text" value="${escapeHtml(ent.text)}" />`),
    );
    card.insertAdjacentHTML(
      "beforeend",
      field("Sub-line", `<input type="text" data-pr="sub" value="${escapeHtml(ent.sub || "")}" placeholder="optional" />`),
    );
    card.insertAdjacentHTML(
      "beforeend",
      field(
        "Size",
        `<input type="range" data-pr="size" min="8" max="72" step="1" value="${ent.size || 18}" style="--fill:${(((ent.size || 18) - 8) / 64) * 100}%"/> <span class="readout">${ent.size || 18}</span>`,
      ),
    );
    card.insertAdjacentHTML(
      "beforeend",
      field("Colour", `<input type="color" data-pr="colour" value="${ent.colour || "#f2e9d8"}" />`),
    );
    card.insertAdjacentHTML(
      "beforeend",
      field(
        "Bold",
        `<label class="toggle"><input type="checkbox" data-pr="bold" ${ent.bold ? "checked" : ""}/> Heavy weight</label>`,
      ),
    );
  }
  if (layer === "notes") {
    card.insertAdjacentHTML(
      "beforeend",
      field("Title", `<input type="text" data-pr="title" value="${escapeHtml(ent.title || "")}" />`),
    );
    card.insertAdjacentHTML(
      "beforeend",
      `<div class="field-row"><label>Design note</label></div><textarea data-pr="body" rows="8" placeholder="What does this do? How should it be built?">${escapeHtml(ent.body || "")}</textarea>`,
    );
  }

  card.insertAdjacentHTML(
    "beforeend",
    `<div class="card-actions">
      <button id="ent-dup">Duplicate</button>
      <button class="danger" id="ent-del">Delete</button>
    </div>`,
  );
  wrap.append(card);

  // notes card: how-to hint for this entity type
  const hint = document.createElement("div");
  hint.className = "property-card";
  hint.innerHTML = `<h3>${titles[layer]} tips</h3><p>${tipsFor(layer)}</p>`;
  wrap.append(hint);

  wire(wrap, ent, layer);
  return wrap;
}

function tipsFor(layer) {
  return (
    {
      terrain: "Ground painted under everything — roads, rivers and props sit on top. Keep strokes wide and loose like the reference maps.",
      splines: "Splines drive roads, rivers, trails and vehicle routes. Double-click the path to insert a point, drag handles to sculpt, Alt-click a handle to remove it.",
      props: "Top-down SVG props — foliage sways gently. Shadows follow the level's sun angle, so keep the light consistent across the map.",
      labels: "Callouts for zones and landmarks. Double-click on the canvas to edit the text here.",
      notes: "Pin design intent next to the geometry: what it does, how it should be built, gameplay beats, constraints.",
    }[layer] || ""
  );
}

function displayName(layer, ent) {
  if (layer === "splines") return ent.name || "Spline";
  if (layer === "labels") return ent.text || "Label";
  if (layer === "notes") return ent.title || "Note";
  if (layer === "terrain") return `${Materials[ent.material]?.label || ent.material} stroke`;
  if (layer === "props") return Props[ent.kind]?.label || ent.kind;
  return ent.id;
}

function wire(wrap, ent, layer) {
  // level bindings
  wrap.querySelector("#lv-name")?.addEventListener("change", (e) => {
    beginChange();
    state.doc.name = e.target.value || "Untitled level";
    state.dirty = true;
    emit();
  });
  wrap.querySelector("#lv-grid")?.addEventListener("change", (e) => {
    beginChange();
    state.doc.settings.grid = Math.max(4, +e.target.value || 32);
    emit();
  });
  wrap.querySelector("#lv-snap")?.addEventListener("change", (e) => {
    state.doc.settings.snap = e.target.checked;
    emit();
  });
  const sun = wrap.querySelector("#lv-sun");
  if (sun)
    gestureInput(sun, () => {
      state.doc.settings.sunAngle = +sun.value;
      sun.style.setProperty("--fill", `${(+sun.value / 360) * 100}%`);
      const ro = wrap.querySelector("#lv-sun-ro");
      if (ro) ro.textContent = `${sun.value}°`;
    });
  const shl = wrap.querySelector("#lv-shl");
  if (shl)
    gestureInput(shl, () => {
      state.doc.settings.shadowLength = +shl.value;
      shl.style.setProperty("--fill", `${((+shl.value / 1.6) * 100)}%`);
      const ro = wrap.querySelector("#lv-shl-ro");
      if (ro) ro.textContent = (+shl.value).toFixed(2);
    });
  const bg = wrap.querySelector("#lv-bg");
  if (bg)
    gestureInput(bg, () => {
      state.doc.settings.background = bg.value;
    });
  wrap.querySelector("#lv-save")?.addEventListener("click", () => window.shadowBox?.save());
  wrap.querySelector("#lv-download")?.addEventListener("click", () => window.shadowBox?.download());

  if (!ent) return;

  wrap.querySelectorAll("[data-px]").forEach((input) => {
    input.addEventListener("change", () => {
      beginChange();
      const axis = input.dataset.px;
      ent[axis] = +input.value || 0;
      state.dirty = true;
      emit();
    });
  });

  const readoutFor = (key, v) => {
    if (key === "scale") return `${(+v).toFixed(2)}×`;
    if (key === "tension") return (+v).toFixed(2);
    if (key === "rotation") return `${Math.round(+v)}°`;
    if (key === "width" || key === "size") return `${Math.round(+v)}`;
    return String(v);
  };

  wrap.querySelectorAll("[data-pr]").forEach((input) => {
    const key = input.dataset.pr;
    const type = input.type;
    const tag = input.tagName;
    const discrete =
      type === "checkbox" || tag === "SELECT" || key === "name" || key === "text" || key === "sub" || key === "title" || key === "body" || key === "colour";

    const apply = () => {
      let v;
      if (type === "checkbox") v = input.checked;
      else if (type === "range" || type === "number") v = +input.value;
      else v = input.value;
      ent[key] = v;
      if (type === "range") {
        const min = +input.min,
          max = +input.max;
        input.style.setProperty("--fill", `${((v - min) / (max - min)) * 100}%`);
        const ro = input.nextElementSibling;
        if (ro?.classList.contains("readout")) ro.textContent = readoutFor(key, v);
      }
    };

    if (discrete) {
      if (type === "text" || tag === "TEXTAREA") {
        // typing: mutate silently so the caret survives; one undo step on commit
        let stashed = null;
        input.addEventListener("focus", () => {
          stashed = snapshot();
        });
        input.addEventListener("input", () => {
          apply();
          state.dirty = true;
          renderScene();
        });
        input.addEventListener("change", () => {
          if (stashed !== null) {
            pushSnapshot(stashed);
            stashed = null;
          }
          apply();
          state.dirty = true;
          emit();
        });
      } else {
        input.addEventListener("change", () => {
          beginChange();
          apply();
          state.dirty = true;
          emit();
        });
      }
    } else {
      gestureInput(input, apply);
    }
  });
  wrap.querySelector("#ent-del")?.addEventListener("click", () => removeSelection());
  wrap.querySelector("#ent-dup")?.addEventListener("click", () => {
    beginChange();
    const copy = JSON.parse(JSON.stringify(ent));
    copy.id = `${ent.id}-copy-${Math.random().toString(36).slice(2, 6)}`;
    if ("x" in copy) {
      copy.x += 24;
      copy.y += 24;
    } else if (copy.points) {
      copy.points = copy.points.map((p) =>
        Array.isArray(p) ? [p[0] + 24, p[1] + 24] : { x: p.x + 24, y: p.y + 24 },
      );
    }
    state.doc.layers[layer].push(copy);
    state.selection = { layer, id: copy.id };
    state.dirty = true;
    emit();
  });
}

export function renderInspector() {
  const has = !!state.selection;
  root.innerHTML = "";
  root.append(has ? entityCard() : levelCard());
  footer.innerHTML = has
    ? `<span>${state.selection.layer} · ${state.selection.id}</span>
       <button class="danger" id="foot-del">Delete</button>`
    : `<span>${state.doc.layers.terrain.length + state.doc.layers.splines.length + state.doc.layers.props.length + state.doc.layers.labels.length + state.doc.layers.notes.length} objects · ${state.dirty ? "unsaved changes" : state.file ? state.file : "no file"}</span>
       <button id="foot-save">Save</button>`;
  footer.querySelector("#foot-del")?.addEventListener("click", () => removeSelection());
  footer.querySelector("#foot-save")?.addEventListener("click", () => window.shadowBox?.save());
}

function escapeHtml(s) {
  return String(s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

on(renderInspector);
