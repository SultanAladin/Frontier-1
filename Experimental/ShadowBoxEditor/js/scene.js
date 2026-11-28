// SVG scene renderer + viewport interaction (pan/zoom, tools, handles).
import {
  state,
  emit,
  on,
  beginChange,
  select,
  addEntity,
  selectedEntity,
  uid,
  Materials,
  SplineKinds,
  LayerOrder,
  setTool,
} from "./state.js";
import { propArt, propShadow, Props } from "./props.js";

const viewport = document.getElementById("viewport");
const NS = "http://www.w3.org/2000/svg";

// ---------------------------------------------------------------- helpers
export function worldFromEvent(ev) {
  const rect = viewport.getBoundingClientRect();
  const sx = ev.clientX - rect.left;
  const sy = ev.clientY - rect.top;
  return {
    x: (sx - rect.width / 2) / state.cam.zoom + state.cam.x,
    y: (sy - rect.height / 2) / state.cam.zoom + state.cam.y,
  };
}
function snap(v) {
  const g = state.doc.settings.grid || 32;
  return state.doc.settings.snap ? Math.round(v / g) * g : Math.round(v);
}
function snapPoint(p) {
  return { x: snap(p.x), y: snap(p.y) };
}

// Catmull-Rom → cubic bezier path through points.
function splinePath(points, tension = 0.5, closed = false) {
  if (points.length < 2) return "";
  const p = points.map((q) => ({ x: q.x, y: q.y }));
  const n = p.length;
  const get = (i) => {
    if (closed) return p[(i + n) % n];
    return p[Math.max(0, Math.min(n - 1, i))];
  };
  let d = `M${p[0].x} ${p[0].y}`;
  const last = closed ? n : n - 1;
  for (let i = 0; i < last; i++) {
    const p0 = get(i - 1), p1 = get(i), p2 = get(i + 1), p3 = get(i + 2);
    const k = tension * 2;
    const c1x = p1.x + ((p2.x - p0.x) / 6) * k;
    const c1y = p1.y + ((p2.y - p0.y) / 6) * k;
    const c2x = p2.x - ((p3.x - p1.x) / 6) * k;
    const c2y = p2.y - ((p3.y - p1.y) / 6) * k;
    d += ` C${c1x} ${c1y} ${c2x} ${c2y} ${p2.x} ${p2.y}`;
  }
  if (closed) d += " Z";
  return d;
}

function el(tag, attrs = {}, parent = null) {
  const node = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === null || v === undefined) continue;
    node.setAttribute(k, v);
  }
  if (parent) parent.append(node);
  return node;
}

// ---------------------------------------------------------------- render
let svg = null;
let worldG = null;

export function buildScene() {
  viewport.innerHTML = "";
  svg = el("svg", { class: "scene" }, viewport);
  const defs = el("defs", {}, svg);
  // subtle ground pattern
  const pat = el(
    "pattern",
    { id: "grid-pattern", width: 32, height: 32, patternUnits: "userSpaceOnUse" },
    defs,
  );
  el("path", { d: "M32 0H0V32", fill: "none", stroke: "#ffffff08", "stroke-width": 1 }, pat);
  const pat2 = el(
    "pattern",
    { id: "grid-major", width: 160, height: 160, patternUnits: "userSpaceOnUse" },
    defs,
  );
  el("path", { d: "M160 0H0V160", fill: "none", stroke: "#ffffff14", "stroke-width": 1 }, pat2);

  worldG = el("g", { class: "world" }, svg);
  el("rect", { class: "bg", x: -20000, y: -20000, width: 40000, height: 40000, fill: state.doc.settings.background || "#0e120e" }, worldG);
  el("rect", { class: "grid-a", x: -20000, y: -20000, width: 40000, height: 40000, fill: "url(#grid-pattern)" }, worldG);
  el("rect", { class: "grid-b", x: -20000, y: -20000, width: 40000, height: 40000, fill: "url(#grid-major)" }, worldG);
  // axes
  el("path", { d: "M-20000 0H20000", stroke: "#ffffff18", "stroke-width": 1 }, worldG);
  el("path", { d: "M0 -20000V20000", stroke: "#ffffff18", "stroke-width": 1 }, worldG);

  for (const layer of LayerOrder) {
    el("g", { "data-layer": layer, class: `layer-${layer}` }, worldG);
  }
  el("g", { class: "layer-overlay" }, worldG);

  wireViewportEvents();
}

function layerG(layer) {
  return worldG.querySelector(`.layer-${layer}`);
}
function overlayG() {
  return worldG.querySelector(".layer-overlay");
}

function drawTerrain() {
  const g = layerG("terrain");
  g.innerHTML = "";
  const strokes = state.doc.layers.terrain.slice();
  // live stroke while painting
  if (state.tool === "terrain" && state.draft?.stroke?.points?.length) {
    strokes.push({ ...state.draft.stroke, id: "__draft__" });
  }
  for (const t of strokes) {
    if (t.hidden || t.points.length < 2) continue;
    const mat = Materials[t.material] || Materials.grass;
    const d = t.points.map((p, i) => `${i ? "L" : "M"}${p[0]} ${p[1]}`).join(" ");
    const attrs = {
      d,
      class: "terrain-stroke entity",
      stroke: mat.colour,
      "stroke-width": t.width,
      "stroke-opacity": t.opacity ?? 0.9,
    };
    if (t.id !== "__draft__") {
      attrs["data-layer"] = "terrain";
      attrs["data-id"] = t.id;
    } else {
      attrs["pointer-events"] = "none";
    }
    el("path", attrs, g);
  }
}

function drawSplines() {
  const g = layerG("splines");
  g.innerHTML = "";
  const sel = state.selection?.layer === "splines" ? state.selection.id : null;
  for (const s of state.doc.layers.splines) {
    if (s.hidden) continue;
    const kind = SplineKinds[s.kind] || SplineKinds.path;
    const colour = s.colour || kind.colour;
    const d = splinePath(s.points, s.tension ?? 0.5, s.closed);
    if (!d) continue;
    const grp = el("g", { class: "entity", "data-layer": "splines", "data-id": s.id }, g);
    if (s.kind === "road") {
      el("path", { d, class: "spline-line", stroke: "#2c2c31", "stroke-width": s.width + 8 }, grp);
      el("path", { d, class: "spline-line", stroke: colour, "stroke-width": s.width }, grp);
      if (s.width > 18)
        el("path", {
          d,
          class: "spline-line",
          stroke: "#e8d84a",
          "stroke-width": 2,
          "stroke-dasharray": "10 12",
          "stroke-opacity": 0.75,
        }, grp);
    } else if (s.kind === "river") {
      el("path", { d, class: "spline-line", stroke: "#2b6f96", "stroke-width": s.width + 6, "stroke-opacity": 0.9 }, grp);
      el("path", { d, class: "spline-line", stroke: colour, "stroke-width": s.width }, grp);
      if (s.width > 16)
        el("path", {
          d,
          class: "spline-line water-flow",
          stroke: "#bfe6ff",
          "stroke-width": 2,
          "stroke-opacity": 0.7,
        }, grp);
    } else if (s.kind === "route") {
      el("path", {
        d,
        class: "spline-line",
        stroke: colour,
        "stroke-width": s.width,
        "stroke-dasharray": "14 10",
      }, grp);
      el("path", {
        d,
        class: "spline-line",
        stroke: "#ffffff55",
        "stroke-width": Math.max(1.5, s.width - 3),
        "stroke-dasharray": "4 20",
      }, grp);
    } else {
      el("path", { d, class: "spline-line", stroke: colour, "stroke-width": s.width, "stroke-opacity": 0.95 }, grp);
      if (s.width > 14)
        el("path", {
          d,
          class: "spline-line",
          stroke: "#ffffff22",
          "stroke-width": Math.max(2, s.width * 0.35),
          "stroke-dasharray": "2 8",
        }, grp);
    }
    // invisible wide hit area
    el(
      "path",
      { d, class: "hit-path", "data-layer": "splines", "data-id": s.id, "stroke-width": Math.max(22, s.width + 12) },
      grp,
    );
    if (s.id === sel) {
      el("path", { d, class: "selection-ring", "stroke-width": 1.5 / state.cam.zoom }, grp);
    }
  }
}

function drawProps() {
  const g = layerG("props");
  g.innerHTML = "";
  const sun = state.doc.settings.sunAngle ?? 48;
  const sl = state.doc.settings.shadowLength ?? 0.7;
  const sel = state.selection?.layer === "props" ? state.selection.id : null;
  // painter order: y so lower props overlap upper ones naturally
  const list = state.doc.layers.props
    .filter((p) => !p.hidden)
    .slice()
    .sort((a, b) => a.y - b.y);
  for (const p of list) {
    const meta = Props[p.kind] || Props["rock-s"];
    const grp = el(
      "g",
      {
        class: "entity",
        "data-layer": "props",
        "data-id": p.id,
        transform: `translate(${p.x} ${p.y})`,
      },
      g,
    );
    const sh = propShadow(p.kind, p.scale || 1, sun, sl);
    el(
      "ellipse",
      {
        cx: sh.cx,
        cy: sh.cy,
        rx: sh.rx,
        ry: sh.ry,
        fill: "#000000",
        "fill-opacity": 0.32,
        transform: `rotate(${sh.rotate} ${sh.cx} ${sh.cy})`,
      },
      grp,
    );
    const artG = el(
      "g",
      {
        class: `prop-art${meta.sway ? " sway" : ""}`,
        transform: `rotate(${p.rotation || 0}) scale(${p.scale || 1})`,
      },
      grp,
    );
    artG.innerHTML = meta.art;
    if (p.id === sel) {
      const r = (meta.radius * (p.scale || 1)) / state.cam.zoom;
      el(
        "ellipse",
        {
          class: "selection-ring",
          rx: r + 4 / state.cam.zoom,
          ry: r + 4 / state.cam.zoom,
        },
        grp,
      );
    }
  }
}

function wrapText(text, perLine, maxLines) {
  const words = String(text).split(/\s+/);
  const lines = [];
  let cur = "";
  for (const w of words) {
    if ((cur + " " + w).trim().length > perLine) {
      lines.push(cur.trim());
      cur = w;
      if (lines.length === maxLines) break;
    } else cur += " " + w;
  }
  if (lines.length < maxLines && cur.trim()) lines.push(cur.trim());
  if (lines.length === maxLines && words.join(" ").length > lines.join(" ").length) {
    lines[maxLines - 1] = lines[maxLines - 1].replace(/.,;:!?$/, "") + "…";
  }
  return lines;
}

function drawLabels() {
  const g = layerG("labels");
  g.innerHTML = "";
  const sel = state.selection?.layer === "labels" ? state.selection.id : null;
  for (const l of state.doc.layers.labels) {
    if (l.hidden) continue;
    const grp = el("g", { class: "entity", "data-layer": "labels", "data-id": l.id, transform: `translate(${l.x} ${l.y})` }, g);
    const size = l.size || 16;
    el(
      "text",
      {
        class: "label-text",
        "text-anchor": "middle",
        "dominant-baseline": "middle",
        "font-size": size,
        "font-weight": l.bold ? 600 : 400,
        fill: l.colour || "#f2e9d8",
        "letter-spacing": size * 0.06,
      },
      grp,
    ).textContent = l.text;
    if (l.sub) {
      el(
        "text",
        {
          class: "label-text",
          "text-anchor": "middle",
          y: size * 0.95,
          "font-size": size * 0.62,
          fill: "#ffffffaa",
        },
        grp,
      ).textContent = l.sub;
    }
    if (l.id === sel) {
      const w = Math.max(60, (l.text || "").length * size * 0.62);
      el(
        "rect",
        {
          class: "selection-ring",
          x: -w / 2 - 8,
          y: -size * 0.85,
          width: w + 16,
          height: size * (l.sub ? 2.1 : 1.7),
          rx: 8,
          "stroke-width": 1.5 / state.cam.zoom,
        },
        grp,
      );
    }
  }
}

function drawNotes() {
  const g = layerG("notes");
  g.innerHTML = "";
  const sel = state.selection?.layer === "notes" ? state.selection.id : null;
  for (const n of state.doc.layers.notes) {
    if (n.hidden) continue;
    const grp = el("g", { class: "entity note-pin", "data-layer": "notes", "data-id": n.id, transform: `translate(${n.x} ${n.y})` }, g);
    const open = n.id === sel;
    // pin head
    el("circle", { r: 11, fill: open ? "#ffb454" : "#2c2c30", stroke: open ? "#ffd9a0" : "#4c4c55", "stroke-width": 2 }, grp);
    el(
      "text",
      { "text-anchor": "middle", "dominant-baseline": "central", "font-size": 12, fill: open ? "#241503" : "#ffb454", "font-weight": 600 },
      grp,
    ).textContent = "✎";
    // card
    const w = 210;
    const lines = wrapText(n.body || "", 34, open ? 8 : 3);
    const h = 34 + lines.length * 14;
    const card = el("g", { class: "note-card", transform: `translate(16 -${h / 2})` }, grp);
    el("rect", { x: 0, y: 0, width: w, height: h, rx: 10, fill: "#19191dcc", stroke: open ? "#ffb45488" : "#3a3a42", "stroke-width": 1.2 }, card);
    el("rect", { x: 0, y: 0, width: 4, height: h, rx: 2, fill: "#ffb454" }, card);
    el("text", { x: 14, y: 20, "font-size": 11.5, fill: "#ffb454", "font-weight": 600 }, card).textContent = n.title || "Note";
    lines.forEach((ln, i) => {
      el("text", { x: 14, y: 38 + i * 14, "font-size": 10.5, fill: "#b9b9c2" }, card).textContent = ln;
    });
  }
}

function drawOverlay() {
  const g = overlayG();
  g.innerHTML = "";
  // spline draft
  if (state.tool === "spline" && state.draft?.points?.length) {
    const pts = state.draft.points;
    const d = pts.map((p, i) => `${i ? "L" : "M"}${p.x} ${p.y}`).join(" ");
    el("path", { d, class: "draft-line", "stroke-width": 1.5 / state.cam.zoom }, g);
    pts.forEach((p, i) => {
      el(
        "circle",
        {
          cx: p.x,
          cy: p.y,
          r: (i === 0 ? 6 : 4.5) / state.cam.zoom,
          fill: i === 0 ? "#ffb454" : "#141414",
          stroke: "#ffb454",
          "stroke-width": 1.5 / state.cam.zoom,
        },
        g,
      );
    });
    if (state.draft.cursor) {
      el(
        "path",
        {
          d: `M${pts[pts.length - 1].x} ${pts[pts.length - 1].y} L${state.draft.cursor.x} ${state.draft.cursor.y}`,
          class: "draft-line",
          "stroke-width": 1 / state.cam.zoom,
          "stroke-opacity": 0.6,
        },
        g,
      );
    }
  }
  // terrain ghost brush
  if (state.tool === "terrain" && state.draft?.cursor) {
    el(
      "circle",
      {
        class: "brush-ghost",
        cx: state.draft.cursor.x,
        cy: state.draft.cursor.y,
        r: (state.draft.width || 46) / 2,
        "stroke-width": 1.5 / state.cam.zoom,
      },
      g,
    );
  }
  // selected spline control points
  if (state.selection?.layer === "splines" && state.tool === "select") {
    const s = selectedEntity();
    if (s) {
      s.points.forEach((p, i) => {
        const c = el(
          "circle",
          {
            class: "spline-handle",
            "data-handle": i,
            cx: p.x,
            cy: p.y,
            r: 5.5 / state.cam.zoom,
            "stroke-width": 1.5 / state.cam.zoom,
          },
          g,
        );
        c.style.cursor = "grab";
      });
    }
  }
}

export function renderScene() {
  if (!svg) buildScene();
  worldG.setAttribute(
    "transform",
    `translate(${viewport.clientWidth / 2} ${viewport.clientHeight / 2}) scale(${state.cam.zoom}) translate(${-state.cam.x} ${-state.cam.y})`,
  );
  svg.querySelector(".bg")?.setAttribute("fill", state.doc.settings.background || "#0e120e");
  const gridOn = state.gridOn;
  svg.querySelector(".grid-a").style.display = gridOn ? "" : "none";
  svg.querySelector(".grid-b").style.display = gridOn ? "" : "none";
  drawTerrain();
  drawSplines();
  drawProps();
  drawLabels();
  drawNotes();
  drawOverlay();
  viewport.className = `viewport tool-${state.tool}`;
}

// ---------------------------------------------------------------- picking
function pickEntity(target) {
  const node = target.closest?.("[data-layer][data-id]");
  if (!node) return null;
  return { layer: node.getAttribute("data-layer"), id: node.getAttribute("data-id") };
}

// ---------------------------------------------------------------- events
let panning = null;
let dragging = null; // {kind, ...}
let spaceDown = false;

function wireViewportEvents() {
  svg.addEventListener("pointerdown", onPointerDown);
  svg.addEventListener("pointermove", onPointerMove);
  svg.addEventListener("pointerup", onPointerUp);
  svg.addEventListener("pointercancel", onPointerUp);
  svg.addEventListener("dblclick", onDblClick);
  svg.addEventListener("wheel", onWheel, { passive: false });
  svg.addEventListener("contextmenu", (e) => e.preventDefault());
}

function onWheel(ev) {
  ev.preventDefault();
  const before = worldFromEvent(ev);
  const factor = Math.exp(-ev.deltaY * 0.0016);
  state.cam.zoom = Math.min(6, Math.max(0.08, state.cam.zoom * factor));
  const after = worldFromEvent(ev);
  state.cam.x += before.x - after.x;
  state.cam.y += before.y - after.y;
  renderScene();
  viewChanged();
}

// let the status bar track zoom without a full emit
export function viewChanged() {
  window.dispatchEvent(new CustomEvent("shadowbox:view"));
}

function onPointerDown(ev) {
  const world = worldFromEvent(ev);
  const hit = pickEntity(ev.target);
  const handle = ev.target.getAttribute?.("data-handle");

  // Alt-click a spline control point removes it (README contract).
  if (
    ev.button === 0 &&
    ev.altKey &&
    handle !== null &&
    handle !== undefined &&
    state.selection?.layer === "splines"
  ) {
    const s = state.doc.layers.splines.find((e) => e.id === state.selection.id);
    if (s && s.points.length > 2) {
      beginChange();
      s.points.splice(+handle, 1);
      state.dirty = true;
      emit();
      ev.preventDefault();
    }
    return;
  }

  if (ev.button === 1 || spaceDown) {
    panning = { x: ev.clientX, y: ev.clientY, camX: state.cam.x, camY: state.cam.y };
    viewport.classList.add("panning");
    svg.setPointerCapture(ev.pointerId);
    ev.preventDefault();
    return;
  }
  if (ev.button !== 0) return;

  if (state.tool === "select") {
    if (handle !== null && handle !== undefined && state.selection?.layer === "splines") {
      beginChange();
      dragging = { kind: "handle", index: +handle, id: state.selection.id };
      svg.setPointerCapture(ev.pointerId);
      return;
    }
    if (hit) {
      select(hit.layer, hit.id);
      const ent = state.doc.layers[hit.layer].find((e) => e.id === hit.id);
      dragging = { kind: "entity", layer: hit.layer, id: hit.id, start: world, orig: JSON.parse(JSON.stringify(ent)) };
      svg.setPointerCapture(ev.pointerId);
      return;
    }
    select(null, null);
    return;
  }

  if (state.tool === "spline") {
    const p = snapPoint(world);
    if (!Array.isArray(state.draft?.points)) {
      state.draft = { ...(state.draft || {}), points: [], cursor: p };
    }
    // click near first point closes
    const first = state.draft.points[0];
    if (first && state.draft.points.length > 2 && Math.hypot(first.x - p.x, first.y - p.y) < 14 / state.cam.zoom) {
      finishSpline(true);
      return;
    }
    state.draft.points.push(p);
    state.draft.cursor = p;
    emit();
    return;
  }

  if (state.tool === "terrain") {
    beginChange();
    const width = state.draft?.width || 46;
    const material = state.draft?.material || "grass";
    const stroke = { id: uid("ter"), material, width, points: [[Math.round(world.x), Math.round(world.y)]], hidden: false };
    state.draft = { ...state.draft, stroke, width, material, cursor: world };
    dragging = { kind: "terrain" };
    svg.setPointerCapture(ev.pointerId);
    renderScene();
    return;
  }

  if (state.tool === "prop") {
    const kind = state.placing || "rock-m";
    beginChange();
    const p = snapPoint(world);
    const jitter = ev.shiftKey ? 0 : 1;
    addEntity("props", {
      id: uid("prop"),
      kind,
      x: p.x,
      y: p.y,
      rotation: jitter ? Math.round((Math.random() * 60 - 30) * 10) / 10 : 0,
      scale: jitter ? Math.round((0.85 + Math.random() * 0.35) * 100) / 100 : 1,
      hidden: false,
    });
    return;
  }

  if (state.tool === "label") {
    beginChange();
    const p = snapPoint(world);
    addEntity("labels", {
      id: uid("lbl"),
      text: "Label",
      sub: "",
      x: p.x,
      y: p.y,
      size: 18,
      colour: "#f2e9d8",
      bold: false,
      hidden: false,
    });
    setTool("select");
    return;
  }

  if (state.tool === "note") {
    beginChange();
    const p = snapPoint(world);
    addEntity("notes", {
      id: uid("note"),
      title: "Design note",
      body: "Describe what this does and how it should be built…",
      x: p.x,
      y: p.y,
      hidden: false,
    });
    setTool("select");
    return;
  }
}

function onPointerMove(ev) {
  const world = worldFromEvent(ev);

  if (panning) {
    state.cam.x = panning.camX - (ev.clientX - panning.x) / state.cam.zoom;
    state.cam.y = panning.camY - (ev.clientY - panning.y) / state.cam.zoom;
    renderScene();
    return;
  }

  if (dragging) {
    if (dragging.kind === "handle") {
      const s = state.doc.layers.splines.find((e) => e.id === dragging.id);
      if (s) {
        const p = snapPoint(world);
        s.points[dragging.index] = p;
        state.dirty = true;
        renderScene();
      }
      return;
    }
    if (dragging.kind === "entity") {
      const list = state.doc.layers[dragging.layer];
      const ent = list.find((e) => e.id === dragging.id);
      if (ent) {
        const dx = Math.round(world.x - dragging.start.x);
        const dy = Math.round(world.y - dragging.start.y);
        if (dragging.layer === "splines") {
          ent.points = dragging.orig.points.map((pt) => ({ x: pt.x + dx, y: pt.y + dy }));
        } else if (Array.isArray(ent.points)) {
          // terrain stroke: points are [x, y] pairs
          ent.points = dragging.orig.points.map((pt) => [pt[0] + dx, pt[1] + dy]);
        } else if ("x" in ent) {
          ent.x = dragging.orig.x + dx;
          ent.y = dragging.orig.y + dy;
        }
        state.dirty = true;
        renderScene();
      }
      return;
    }
    if (dragging.kind === "terrain" && state.draft?.stroke) {
      const st = state.draft.stroke;
      const last = st.points[st.points.length - 1];
      const p = [Math.round(world.x), Math.round(world.y)];
      if (Math.hypot(p[0] - last[0], p[1] - last[1]) > 6) {
        st.points.push(p);
      }
      state.draft.cursor = world;
      renderScene();
      return;
    }
  }

  // hover + ghost
  if (state.tool === "terrain") {
    if (!state.draft) state.draft = {};
    state.draft.cursor = world;
    renderScene();
    return;
  }
  if (state.tool === "spline" && state.draft) {
    state.draft.cursor = snapPoint(world);
    renderScene();
    return;
  }
  // hover is tracked for the status bar only — no full re-render while moving
  state.hover = pickEntity(ev.target);
}

function onPointerUp(ev) {
  if (panning) {
    panning = null;
    viewport.classList.remove("panning");
    return;
  }
  if (dragging?.kind === "terrain") {
    const st = state.draft?.stroke;
    if (st && st.points.length >= 2) {
      state.doc.layers.terrain.push(st);
      state.selection = { layer: "terrain", id: st.id };
      state.dirty = true;
    }
    state.draft = { width: state.draft?.width || 46, material: state.draft?.material || "grass" };
    dragging = null;
    emit();
    return;
  }
  if (dragging) {
    dragging = null;
    state.dirty = true;
    emit();
    return;
  }
}

function onDblClick(ev) {
  if (state.tool === "spline" && state.draft && state.draft.points.length >= 2) {
    finishSpline(false);
    ev.preventDefault();
    return;
  }
  const hit = pickEntity(ev.target);
  if (hit?.layer === "splines" && state.tool === "select") {
    // insert a control point at the nearest spot on the polyline
    const s = state.doc.layers.splines.find((e) => e.id === hit.id);
    if (s) {
      const world = worldFromEvent(ev);
      let best = null;
      const segs = s.closed ? s.points.length : s.points.length - 1;
      for (let i = 0; i < segs; i++) {
        const a = s.points[i];
        const b = s.points[(i + 1) % s.points.length];
        const abx = b.x - a.x,
          aby = b.y - a.y;
        const len2 = abx * abx + aby * aby || 1;
        let t = ((world.x - a.x) * abx + (world.y - a.y) * aby) / len2;
        t = Math.max(0, Math.min(1, t));
        const px = a.x + abx * t,
          py = a.y + aby * t;
        const d = Math.hypot(world.x - px, world.y - py);
        if (!best || d < best.d) best = { d, index: i + 1, px, py };
      }
      if (best && best.d < 40 / state.cam.zoom) {
        beginChange();
        s.points.splice(best.index, 0, {
          x: Math.round(best.px),
          y: Math.round(best.py),
        });
        state.dirty = true;
        select("splines", s.id);
        ev.preventDefault();
        return;
      }
    }
  }
  if (hit) {
    select(hit.layer, hit.id);
    document.getElementById("inspector")?.querySelector("input, textarea")?.focus();
  }
}

function finishSpline(closed) {
  const raw = state.draft?.points || [];
  // drop consecutive duplicates (double-click adds two)
  const pts = raw.filter(
    (p, i) => i === 0 || p.x !== raw[i - 1].x || p.y !== raw[i - 1].y,
  );
  if (pts.length < 2) {
    state.draft = null;
    renderScene();
    return;
  }
  beginChange();
  const kind = state.draft.kind || "path";
  const meta = SplineKinds[kind];
  const ent = {
    id: uid("spl"),
    name: `${meta.label} ${state.doc.layers.splines.length + 1}`,
    kind,
    points: pts.map((p) => ({ x: p.x, y: p.y })),
    width: state.draft.width || meta.width,
    tension: 0.5,
    closed: !!closed,
    colour: null,
    hidden: false,
  };
  state.doc.layers.splines.push(ent);
  state.selection = { layer: "splines", id: ent.id };
  state.draft = null;
  state.dirty = true;
  setTool("select");
}

export function cancelDraft() {
  if (state.draft) {
    if (state.draft.stroke) {
      const i = state.doc.layers.terrain.findIndex((t) => t.id === state.draft.stroke.id);
      if (i >= 0) {
        state.doc.layers.terrain.splice(i, 1);
      }
    }
    state.draft = null;
    renderScene();
    return true;
  }
  return false;
}

export function finishDraft() {
  if (state.tool === "spline" && state.draft?.points?.length >= 2) {
    finishSpline(false);
    return true;
  }
  return false;
}

export function fitView() {
  const all = [];
  for (const layer of LayerOrder) {
    for (const e of state.doc.layers[layer]) {
      if (e.hidden) continue;
      if (e.points) {
        for (const p of e.points) {
          if (Array.isArray(p)) all.push({ x: p[0], y: p[1] });
          else all.push(p);
        }
      } else if ("x" in e) {
        all.push({ x: e.x, y: e.y });
        const r = (Props[e.kind]?.radius || 16) * (e.scale || 1);
        all.push({ x: e.x - r, y: e.y - r }, { x: e.x + r, y: e.y + r });
      }
    }
  }
  if (!all.length) {
    state.cam = { x: 0, y: 0, zoom: 1 };
    renderScene();
    viewChanged();
    return;
  }
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  for (const p of all) {
    minX = Math.min(minX, p.x);
    maxX = Math.max(maxX, p.x);
    minY = Math.min(minY, p.y);
    maxY = Math.max(maxY, p.y);
  }
  const pad = 90;
  const w = viewport.clientWidth - pad * 2;
  const h = viewport.clientHeight - pad * 2;
  state.cam.x = (minX + maxX) / 2;
  state.cam.y = (minY + maxY) / 2;
  state.cam.zoom = Math.min(6, Math.max(0.08, Math.min(w / Math.max(1, maxX - minX), h / Math.max(1, maxY - minY))));
  renderScene();
  viewChanged();
}

export function zoomBy(factor) {
  state.cam.zoom = Math.min(6, Math.max(0.08, state.cam.zoom * factor));
  renderScene();
  viewChanged();
}

export function screenToWorldGrid() {
  return state.doc.settings.grid;
}

// keyboard bridge for space-pan
window.addEventListener("keydown", (e) => {
  if (e.code === "Space" && !e.repeat && !isTyping(e)) {
    spaceDown = true;
    e.preventDefault();
  }
});
window.addEventListener("keyup", (e) => {
  if (e.code === "Space") spaceDown = false;
});
function isTyping(e) {
  const t = e.target;
  return t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.isContentEditable);
}

on(() => renderScene());
window.addEventListener("resize", () => renderScene());
