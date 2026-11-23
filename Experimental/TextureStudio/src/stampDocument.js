import { NormalizeArtworkNames } from "./ArtworkNames.js";
import { NormalizeArtworkMask } from "./ArtworkMask.js";
import { StampVectorGeometry } from "./VectorArtwork.js";
import { PAINT_CHANNELS } from "./paintChannelModel.js";
import { normalizeTextureSource } from "./textureSource.js";

export const STAMP_SCHEMA = "alloy.stamp.v1";
export const STAMP_DRAFT_KEY = "alloy-stamp-project-v1";
export const STAMP_LIBRARY_KEY = "alloy-stamp-presets-v1";
export const STAMP_FILE_LIMIT = 600000;
export const STAMP_LAYER_LIMIT = 24;
export const STAMP_RUN_LIMIT = 128;
export const STAMP_TEXT_LIMIT = 1024;
export const STAMP_FONTS = ["DM Sans", "Space Grotesk"];
export const stampId = () =>
  globalThis.crypto?.randomUUID?.() ||
  `stamp-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 9)}`;
export const stampNumber = (v, fallback, min, max) =>
  Number.isFinite(Number(v))
    ? Math.min(max, Math.max(min, Number(v)))
    : fallback;
export const stampColour = (v, fallback = "#f3e6ce") =>
  typeof v === "string" && /^#[\da-f]{6}$/i.test(v)
    ? v.toLowerCase()
    : fallback;
export const stampName = (v, fallback = "Untitled stamp") =>
  (typeof v === "string"
    ? v
        .replace(/[\u0000-\u001f\u007f]/g, "")
        .trim()
        .slice(0, 80)
    : "") || fallback;
export function normalizeStampChannels(raw = ["baseColor"]) {
  const channels = Array.isArray(raw) ? raw : ["baseColor"];
  const selected = PAINT_CHANNELS.filter((c) => channels.includes(c.id)).map(
    (c) => c.id,
  );
  if (selected.includes("normal") && !selected.includes("height"))
    selected.push("height");
  return PAINT_CHANNELS.filter((c) => selected.includes(c.id)).map((c) => c.id);
}
export function normalizeStampValues(raw = {}) {
  raw = raw && typeof raw === "object" && !Array.isArray(raw) ? raw : {};
  return Object.fromEntries(
    PAINT_CHANNELS.map((c) => [
      c.id,
      c.edit === "color"
        ? stampColour(raw[c.id], c.value)
        : c.edit === "scalar"
          ? stampNumber(raw[c.id] ?? c.value, c.value, c.min, c.max)
          : 1,
    ]),
  );
}
export function normalizeStampRun(raw = {}) {
  raw = raw && typeof raw === "object" && !Array.isArray(raw) ? raw : {};
  return {
    text:
      typeof raw.text === "string"
        ? raw.text
            .replace(/\r\n?/g, "\n")
            .replace(/[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/g, "")
            .slice(0, STAMP_TEXT_LIMIT)
        : "",
    fontFamily: STAMP_FONTS.includes(raw.fontFamily)
      ? raw.fontFamily
      : STAMP_FONTS[0],
    fontSize: stampNumber(raw.fontSize ?? 48, 48, 8, 160),
    fontWeight: Number(raw.fontWeight) === 600 ? 600 : 400,
    color: stampColour(raw.color),
  };
}
const sameStyle = (a, b) =>
  a.fontFamily === b.fontFamily &&
  a.fontSize === b.fontSize &&
  a.fontWeight === b.fontWeight &&
  a.color === b.color;
export function normalizeStampRuns(raw = []) {
  if (!Array.isArray(raw) || raw.length > STAMP_RUN_LIMIT)
    throw new Error(`Text is limited to ${STAMP_RUN_LIMIT} style runs.`);
  const runs = [];
  let length = 0;
  for (const value of raw) {
    if (typeof value?.text === "string" && value.text.length > STAMP_TEXT_LIMIT)
      throw new Error(
        `Stamp text is limited to ${STAMP_TEXT_LIMIT} characters.`,
      );
    const run = normalizeStampRun(value);
    length += run.text.length;
    if (length > STAMP_TEXT_LIMIT)
      throw new Error(
        `Stamp text is limited to ${STAMP_TEXT_LIMIT} characters.`,
      );
    if (!run.text) continue;
    if (runs.length && sameStyle(runs.at(-1), run))
      runs.at(-1).text += run.text;
    else runs.push(run);
  }
  return runs.length ? runs : [normalizeStampRun({ ...raw[0], text: "" })];
}
// Character ranges, not HTML, are the canonical rich-text formatting operation.
export function formatStampRuns(raw, start, end, patch) {
  const runs = normalizeStampRuns(raw);
  const total = runs.reduce((n, r) => n + r.text.length, 0);
  if (!total) return [normalizeStampRun({ ...runs[0], ...patch })];
  start = Math.max(0, Math.min(total, start));
  end = Math.max(start, Math.min(total, end));
  if (end === start) {
    start = 0;
    end = total;
  }
  let offset = 0;
  const result = [];
  for (const run of runs) {
    const from = Math.max(0, start - offset),
      to = Math.min(run.text.length, end - offset);
    if (to > from) {
      if (from) result.push({ ...run, text: run.text.slice(0, from) });
      result.push({ ...run, ...patch, text: run.text.slice(from, to) });
      if (to < run.text.length)
        result.push({ ...run, text: run.text.slice(to) });
    } else result.push(run);
    offset += run.text.length;
  }
  return normalizeStampRuns(result);
}
export function replaceStampText(raw, start, end, value, style) {
  const runs = normalizeStampRuns(raw),
    result = [];
  let offset = 0,
    inserted = false;
  for (const run of runs) {
    const next = offset + run.text.length;
    if (offset < start)
      result.push({
        ...run,
        text: run.text.slice(0, Math.min(run.text.length, start - offset)),
      });
    if (!inserted && next >= start) {
      result.push({ ...run, ...style, text: value });
      inserted = true;
    }
    if (next > end)
      result.push({ ...run, text: run.text.slice(Math.max(0, end - offset)) });
    offset = next;
  }
  if (!inserted) result.push({ ...runs.at(-1), ...style, text: value });
  return normalizeStampRuns(result);
}
export function createStampLayer(kind = "text", options = {}) {
  return normalizeStampLayer({
    id: stampId(),
    kind,
    name: kind === "text" ? "Text" : kind === "svg" ? "SVG artwork" : "Image",
    x: 256,
    y: 256,
    width: kind === "text" ? 350 : 300,
    height:
      kind === "text"
        ? 140
        : options.image
          ? (300 * options.image.height) / options.image.width
          : 300,
    runs: [
      {
        text: "ALLOY",
        fontFamily: "Space Grotesk",
        fontSize: 64,
        fontWeight: 600,
        color: "#f3e6ce",
      },
    ],
    svg: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><circle cx="50" cy="50" r="42" fill="none" stroke="#f3e6ce" stroke-width="4"/></svg>',
    ...options,
  });
}
export function normalizeStampLayer(raw) {
  if (
    !raw ||
    typeof raw.id !== "string" ||
    !/^[\w-]{1,80}$/.test(raw.id) ||
    !["text", "svg", "image", "vector"].includes(raw.kind)
  )
    throw new Error("A stamp component has an invalid identity or type.");
  const result = {
    id: raw.id,
    ...(raw.mask ? { mask: NormalizeArtworkMask(raw.mask) } : {}),
    kind: raw.kind,
    name: stampName(raw.name, raw.kind === "text" ? "Text" : "Artwork"),
    visible: raw.visible !== false,
    locked: raw.locked === true,
    x: stampNumber(raw.x ?? 256, 256, -512, 1024),
    y: stampNumber(raw.y ?? 256, 256, -512, 1024),
    width: stampNumber(raw.width ?? 300, 300, 1, 1024),
    height: stampNumber(raw.height ?? 100, 100, 1, 1024),
    rotation: stampNumber(raw.rotation ?? 0, 0, -360, 360),
    opacity: stampNumber(raw.opacity ?? 1, 1, 0, 1),
  };
  if (raw.kind === "vector") result.vector = StampVectorGeometry({ ...result, vector: raw.vector });
  if (raw.kind === "text") result.runs = normalizeStampRuns(raw.runs);
  if (raw.kind === "image") {
    result.image = normalizeTextureSource(raw.image);
    if (!result.image)
      throw new Error("A stamp image must be a bounded embedded PNG.");
  }
  if (raw.kind === "svg") {
    if (
      typeof raw.svg !== "string" ||
      raw.svg.length > 200000 ||
      !/^\s*(?:<\?xml[^?]*\?>\s*)?<svg\b/i.test(raw.svg)
    )
      throw new Error("Embed SVG markup, limited to 200 KB per component.");
    // Full DOM/attribute/reference validation is performed by the shared SVG
    // sanitizer on import, composition and export. No arbitrary markup is injected.
    if (
      /<!DOCTYPE|<!ENTITY|<\s*(?:script|foreignObject|iframe|style|animate|set)\b|\bon\w+\s*=|javascript:|https?:\/\/(?!www\.w3\.org\/)/i.test(
        raw.svg,
      )
    )
      throw new Error("Only static, self-contained SVG markup is allowed.");
    result.svg = raw.svg;
  }
  return result;
}
export function normalizeStampDocument(input) {
  if (!input || input.schema !== STAMP_SCHEMA)
    throw new Error("This is not an Alloy stamp project.");
  if (!Array.isArray(input.layers) || input.layers.length > STAMP_LAYER_LIMIT)
    throw new Error(
      `A stamp can contain at most ${STAMP_LAYER_LIMIT} components.`,
    );
  const layers = NormalizeArtworkNames(input.layers.map(normalizeStampLayer));
  if (new Set(layers.map((l) => l.id)).size !== layers.length)
    throw new Error("Stamp component identities must be unique.");
  const doc = {
    schema: STAMP_SCHEMA,
    id:
      typeof input.id === "string" && /^[\w-]{1,80}$/.test(input.id)
        ? input.id
        : stampId(),
    name: stampName(input.name),
    channels: normalizeStampChannels(input.channels),
    channelValues: normalizeStampValues(input.channelValues),
    maskMode: input.maskMode === "luminance" ? "luminance" : "alpha",
    layers,
  };
  if (
    new TextEncoder().encode(JSON.stringify(doc)).byteLength > STAMP_FILE_LIMIT
  )
    throw new Error("Stamp projects are limited to 600 KB.");
  return doc;
}
export function parseStampDocument(text) {
  if (
    typeof text !== "string" ||
    new TextEncoder().encode(text).byteLength > STAMP_FILE_LIMIT
  )
    throw new Error("Stamp files must be smaller than 600 KB.");
  let input;
  try {
    input = JSON.parse(text);
  } catch {
    throw new Error("The stamp file is not valid JSON.");
  }
  return normalizeStampDocument(input);
}
export function createStampDocument() {
  return normalizeStampDocument({
    schema: STAMP_SCHEMA,
    id: stampId(),
    name: "Alloy maker mark",
    channels: ["baseColor", "roughness"],
    channelValues: { roughness: 0.42 },
    layers: [
      createStampLayer("text", {
        id: "maker-title",
        y: 232,
        width: 365,
        height: 144,
      }),
      createStampLayer("text", {
        id: "maker-subtitle",
        y: 308,
        width: 270,
        height: 34,
        name: "Edition",
        runs: [
          {
            text: "SURFACE STUDY / 01",
            fontFamily: "DM Sans",
            fontSize: 24,
            color: "#bf9362",
          },
        ],
      }),
      createStampLayer("svg", {
        id: "maker-frame",
        name: "Maker ring",
        width: 470,
        height: 470,
      }),
    ],
  });
}
export function normalizeStampPreset(raw) {
  const project = normalizeStampDocument(raw?.project);
  const raster = normalizeTextureSource(raw?.raster);
  if (!raster) throw new Error("A stamp preset needs a bounded rendered PNG.");
  return { id: project.id, name: project.name, project, raster };
}
export function readStampPresets() {
  try {
    const text = localStorage.getItem(STAMP_LIBRARY_KEY);
    if (!text || text.length > 4000000) return [];
    const raw = JSON.parse(text);
    return Array.isArray(raw)
      ? raw.slice(0, 20).flatMap((p) => {
          try {
            return [normalizeStampPreset(p)];
          } catch {
            return [];
          }
        })
      : [];
  } catch {
    return [];
  }
}
export function saveStampPreset(raw) {
  const preset = normalizeStampPreset(raw);
  const presets = [
    preset,
    ...readStampPresets().filter((p) => p.id !== preset.id),
  ];
  if (presets.length > 20)
    throw new Error(
      "The 20-preset library limit has been reached. Export or remove an older preset first.",
    );
  const text = JSON.stringify(presets);
  if (text.length > 4000000)
    throw new Error(
      "The stamp library is full. Export or remove older presets first.",
    );
  localStorage.setItem(STAMP_LIBRARY_KEY, text);
  window.dispatchEvent(new Event("alloy-stamp-library-change"));
  return preset;
}

export function removeStampPreset(id) {
  const presets = readStampPresets().filter((p) => p.id !== id);
  localStorage.setItem(STAMP_LIBRARY_KEY, JSON.stringify(presets));
  window.dispatchEvent(new Event("alloy-stamp-library-change"));
}
