// ShadowBox document state, history, camera and events.

export const LayerOrder = ["terrain", "splines", "props", "labels", "notes"];
export const LayerLabel = {
  terrain: "Terrain",
  splines: "Splines",
  props: "Props",
  labels: "Labels",
  notes: "Notes",
};

export const SplineKinds = {
  road: { label: "Road", width: 46, colour: "#4b4b52" },
  river: { label: "River", width: 40, colour: "#3f8fbf" },
  path: { label: "Trail", width: 22, colour: "#a3773f" },
  route: { label: "Vehicle route", width: 6, colour: "#ffb454" },
};

export const Materials = {
  grass: { label: "Grass", colour: "#5d8f38" },
  dirt: { label: "Dirt", colour: "#8a6a3e" },
  sand: { label: "Sand", colour: "#c9b276" },
  stone: { label: "Stone", colour: "#7d7d82" },
  mud: { label: "Mud", colour: "#5a4630" },
  asphalt: { label: "Asphalt", colour: "#3c3f44" },
};

let seq = 0;
export function uid(prefix) {
  seq += 1;
  return `${prefix}-${Date.now().toString(36)}-${seq.toString(36)}`;
}

export function blankDoc(name = "Untitled level") {
  return {
    format: "shadowbox.level",
    version: 1,
    name,
    settings: {
      grid: 32,
      snap: true,
      sunAngle: 48,
      shadowLength: 0.7,
      background: "#0e120e",
    },
    layers: { terrain: [], splines: [], props: [], labels: [], notes: [] },
  };
}

export const state = {
  doc: blankDoc(),
  cam: { x: 0, y: 0, zoom: 1 },
  selection: null, // { layer, id }
  tool: "select",
  draft: null, // in-progress tool data
  placing: null, // prop kind being placed
  hover: null, // { layer, id }
  gridOn: true,
  dirty: false,
  file: null, // saved file name once persisted
};

const past = [];
const future = [];
const listeners = new Set();

export function on(fn) {
  listeners.add(fn);
  return () => listeners.delete(fn);
}
export function emit() {
  for (const fn of listeners) fn();
}

export function snapshot() {
  return JSON.stringify(state.doc);
}
export function beginChange() {
  past.push(snapshot());
  if (past.length > 80) past.shift();
  future.length = 0;
}
// Push a stashed pre-edit snapshot (used for slider/typing gestures).
export function pushSnapshot(snap) {
  if (typeof snap !== "string") return;
  past.push(snap);
  if (past.length > 80) past.shift();
  future.length = 0;
}
export function undo() {
  if (!past.length) return false;
  future.push(snapshot());
  state.doc = JSON.parse(past.pop());
  state.selection = null;
  state.draft = null;
  state.dirty = true;
  emit();
  return true;
}
export function redo() {
  if (!future.length) return false;
  past.push(snapshot());
  state.doc = JSON.parse(future.pop());
  state.selection = null;
  state.draft = null;
  state.dirty = true;
  emit();
  return true;
}
export function canUndo() {
  return past.length > 0;
}
export function canRedo() {
  return future.length > 0;
}

export function setTool(tool) {
  if (state.tool === tool) return;
  state.tool = tool;
  state.draft = null;
  emit();
}
export function select(layer, id) {
  state.selection = layer && id ? { layer, id } : null;
  emit();
}
export function selectedEntity() {
  if (!state.selection) return null;
  const { layer, id } = state.selection;
  return state.doc.layers[layer]?.find((e) => e.id === id) || null;
}

export function findEntity(id) {
  for (const layer of LayerOrder) {
    const ent = state.doc.layers[layer].find((e) => e.id === id);
    if (ent) return { layer, ent };
  }
  return null;
}

export function addEntity(layer, ent) {
  state.doc.layers[layer].push(ent);
  state.selection = { layer, id: ent.id };
  state.dirty = true;
  emit();
  return ent;
}
export function removeSelection() {
  if (!state.selection) return false;
  const { layer, id } = state.selection;
  const list = state.doc.layers[layer];
  const idx = list.findIndex((e) => e.id === id);
  if (idx < 0) return false;
  beginChange();
  list.splice(idx, 1);
  state.selection = null;
  state.dirty = true;
  emit();
  return true;
}
export function updateSelected(patch, { history = false } = {}) {
  if (!state.selection) return;
  const ent = selectedEntity();
  if (!ent) return;
  if (history) beginChange();
  Object.assign(ent, patch);
  state.dirty = true;
  emit();
}

export function counts() {
  const d = state.doc;
  return {
    terrain: d.layers.terrain.length,
    splines: d.layers.splines.length,
    props: d.layers.props.length,
    labels: d.layers.labels.length,
    notes: d.layers.notes.length,
    total:
      d.layers.terrain.length +
      d.layers.splines.length +
      d.layers.props.length +
      d.layers.labels.length +
      d.layers.notes.length,
  };
}

export function serialize() {
  return JSON.stringify(state.doc, null, 2);
}
export function loadDoc(doc, file = null) {
  if (!doc || doc.format !== "shadowbox.level") throw new Error("Not a ShadowBox level");
  state.doc = doc;
  state.doc.settings = { ...blankDoc().settings, ...(doc.settings || {}) };
  state.selection = null;
  state.draft = null;
  state.file = file;
  state.dirty = false;
  past.length = 0;
  future.length = 0;
  emit();
}
