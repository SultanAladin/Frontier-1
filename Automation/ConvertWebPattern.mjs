#!/usr/bin/env node
// Convert the geometry subset of a validated Texture Studio Pattern JSON document
// into the strict native Vector Authoring TOML interchange format.
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { validatePattern } from "../Experimental/TextureStudio/src/patternDocument.js";
import {
  patternPathContours,
  patternPrimitiveNodes,
  patternSizedPath,
} from "../Experimental/TextureStudio/src/patternGeometry.js";

const Channels = ["Colour", "Height", "Normal", "Roughness", "Metalness", "Opacity", "Occlusion"];
// Native GPU/host segment budget (VectorCoverageRasterization allocates this many
// cubic slots; VectorCoverage.h enforces the same bound). Raised from 4,000 so the
// Alloy starters (weaves peak near 19k segments) convert without simplification.
const NativeSegmentBudget = 32768;
const fail = (message) => { throw new Error(message); };
const finite = (value, fallback = 0) => Number.isFinite(Number(value)) ? Number(value) : fallback;

function parseColour(value, label) {
  if (typeof value !== "string" || !/^#[\da-f]{6}$/i.test(value))
    fail(`${label} must be a literal #RRGGBB colour; linked palettes, alpha colours and gradients are not yet importable.`);
  const hex = value.slice(1);
  return [parseInt(hex.slice(0, 2), 16) / 255, parseInt(hex.slice(2, 4), 16) / 255,
    parseInt(hex.slice(4, 6), 16) / 255, 1];
}
function flowerNodes(layer) {
  const circle = patternPrimitiveNodes({ kind: "ellipse" }).nodes;
  const petal = (point) => ({
    x: (point.x - 50) * 0.36,
    y: 27 + (point.y - 50) * 0.54 - 50,
  });
  return Array.from({ length: 5 }, (_, index) => {
    const angle = index * 72 * Math.PI / 180;
    const rotate = (point) => {
      const p = petal(point);
      return { x: 50 + p.x * Math.cos(angle) - p.y * Math.sin(angle), y: 50 + p.x * Math.sin(angle) + p.y * Math.cos(angle) };
    };
    return { closed: true, nodes: circle.map((node) => ({
      ...rotate(node),
      ...(node.in ? { in: rotate(node.in) } : {}),
      ...(node.out ? { out: rotate(node.out) } : {}),
    })) };
  });
}
function contoursFor(layer) {
  let pathData;
  if (layer.kind === "flower") {
    return flowerNodes(layer).map(({ nodes }) => ({
      nodes: nodes.map((node) => ({
        ...node,
        x: ((node.x - 50) * layer.width) / 100,
        y: ((node.y - 50) * layer.height) / 100,
        ...(node.in ? { in: { x: ((node.in.x - 50) * layer.width) / 100, y: ((node.in.y - 50) * layer.height) / 100 } } : {}),
        ...(node.out ? { out: { x: ((node.out.x - 50) * layer.width) / 100, y: ((node.out.y - 50) * layer.height) / 100 } } : {}),
      })),
      closed: true,
    }));
  }
  try { pathData = patternSizedPath(layer); }
  catch (error) { fail(`Layer “${layer.name}”: ${error.message}`); }
  if (typeof pathData !== "string" || !pathData.trim())
    fail(`Layer “${layer.name}” (${layer.kind}) has no native-convertible vector path.`);
  const contours = patternPathContours(pathData, NativeSegmentBudget);
  if (!contours?.length) fail(`Layer “${layer.name}”: SVG path could not be parsed.`);
  return contours;
}
function cubicSegments(contours, layer) {
  const result = [];
  const local = (point) => [
    ((point.x / layer.width) * (layer.flipX ? -1 : 1)),
    ((point.y / layer.height) * (layer.flipY ? -1 : 1)),
  ];
  for (const contour of contours) {
    const nodes = contour.nodes;
    if (!Array.isArray(nodes) || nodes.length < 2) fail(`Layer “${layer.name}” contains an incomplete contour.`);
    const edgeCount = contour.closed ? nodes.length : nodes.length - 1;
    for (let index = 0; index < edgeCount; index++) {
      const start = nodes[index], end = nodes[(index + 1) % nodes.length];
      const a = local(start), b = local(end);
      const c1 = local(start.out || { x: start.x + (end.x - start.x) / 3, y: start.y + (end.y - start.y) / 3 });
      const c2 = local(end.in || { x: start.x + (end.x - start.x) * 2 / 3, y: start.y + (end.y - start.y) * 2 / 3 });
      result.push({ start_control: [...a, ...c1], control_end: [...c2, ...b] });
      if (result.length > NativeSegmentBudget) fail(`Layer “${layer.name}” exceeds the native ${NativeSegmentBudget.toLocaleString("en-US")}-segment limit.`);
    }
  }
  return result;
}
function patternCopies(layer, strokeWidth) {
  const padding = layer.paint
    ? strokeWidth * 0.5
    : (strokeWidth * Math.max(layer.width, layer.height)) / 200;
  const radius = (layer.width + layer.height) / 2 + padding;
  const copies = [];
  for (let column = -4; column <= 4; column++) {
    for (let row = -4; row <= 4; row++) {
      const x = layer.x + column * 512;
      const y = layer.y + row * 512;
      if (x + radius < 0 || x - radius > 512 || y + radius < 0 || y - radius > 512) continue;
      copies.push({ x, y });
    }
  }
  return copies.length ? copies : [{ x: layer.x, y: layer.y }];
}
function channelData(layer) {
  const source = layer.SurfaceChannels || {};
  const result = {};
  for (const name of Channels) {
    const item = source[name === "Colour" ? "Color" : name] || {};
    result[name] = { enabled: name === "Occlusion" ? item.Enabled === true : item.Enabled !== false,
      strength: Math.max(0, Math.min(name === "Normal" ? 4 : 1, finite(item.Strength, 1))) };
  }
  // Native scalar contributions carry their target map value. Web Pattern stores
  // the corresponding targets as per-layer material fields rather than channel strengths.
  result.Roughness.strength = Math.max(0, Math.min(1, finite(layer.roughness, 0.7)));
  result.Metalness.strength = Math.max(0, Math.min(1, finite(layer.metalness, 0)));
  result.Occlusion = { enabled: source.Occlusion?.Enabled === true,
    strength: 1 - Math.max(0, Math.min(1, finite(source.Occlusion?.Strength, 1))) };
  return result;
}
function layerStructure(layer) {
  if (layer.stitchRole) {
    const geometry = layer.stitchRole === "holes" ? "needle_entry" : layer.stitchRole === "thread" ? "stitch_thread" : "upper_thread_pass";
    return { structure: "stitch", geometry };
  }
  if (layer.weaveRole === "yarn") return { structure: "weave", geometry: "weave_yarn" };
  return { structure: "freeform", geometry: "unclassified" };
}
function nativeMask(layer) {
  const mask = layer.mask;
  if (!mask) return {};
  if (!["rect", "ellipse"].includes(layer.kind) || layer.strokeWidth > 0)
    fail(`Layer “${layer.name}” masks are currently supported only on unstroked rectangle and ellipse geometry.`);
  if (!["rect", "ellipse"].includes(mask.category) || mask.paint?.length)
    fail(`Layer “${layer.name}” uses an image or painted artwork mask that the native vector format cannot yet represent.`);
  const rotation = finite(layer.rotation) * Math.PI / 180;
  let localX = ((mask.x - 50) * layer.width) / 100;
  let localY = ((mask.y - 50) * layer.height) / 100;
  if (layer.flipX) localX = -localX;
  if (layer.flipY) localY = -localY;
  const centreX = layer.x + Math.cos(rotation) * localX - Math.sin(rotation) * localY;
  const centreY = layer.y + Math.sin(rotation) * localX + Math.cos(rotation) * localY;
  return {
    mask_enabled: mask.enabled,
    mask_inverted: mask.invert,
    mask_luminance: mask.mode === "luminance",
    mask: {
      topology: [mask.category === "ellipse" ? 1 : 0, 0, 0, 0],
      bounds: [centreX, centreY, layer.width * mask.width / 100, layer.height * mask.height / 100],
      transform: [rotation, 0, mask.opacity, 0],
      colour: [1, 1, 1, 1], colour_end: [1, 1, 1, 1], paint: [0, 0, 0, 1],
    },
  };
}
function instruction({ layer, root, segmentStart, segmentCount, colour, filled, strokeWidth, fillRule }) {
  return {
    topology: [6, segmentStart, segmentCount, fillRule === "evenodd" ? 1 : 0],
    bounds: [finite(layer.x), finite(layer.y), finite(layer.width), finite(layer.height)],
    transform: [finite(layer.rotation) * Math.PI / 180, strokeWidth, finite(layer.opacity, 1), 0],
    colour, colour_end: colour,
    paint: [0, 0, 0, filled ? 1 : 0],
    root,
  };
}
function appendOver(instructions, lower, upper) {
  const root = instructions.length;
  instructions.push({ topology: [28, lower, upper, 0], bounds: [0, 0, 0, 0],
    transform: [0, 1, 1, 0], colour: [1, 1, 1, 1], colour_end: [1, 1, 1, 1], paint: [0, 0, 0, 0], root });
  return root;
}
function nativeBackground(pattern, instructions, layers) {
  if (pattern.backgroundOpacity <= 0) return;
  const colour = parseColour(pattern.background, "Pattern background");
  const root = instructions.length;
  instructions.push({ topology: [0, 0, 0, 0], bounds: [256, 256, 512, 512],
    transform: [0, 0, pattern.backgroundOpacity, 0], colour, colour_end: colour, paint: [0, 0, 0, 1], root });
  const surface_channels = Object.fromEntries(Channels.map((name) => [name.toLowerCase(),
    { enabled: name === "Colour", strength: name === "Colour" ? 1 : 0 }]));
  layers.push({ token: 1, name: "Pattern background", visible: true, locked: false, root,
    surface_channels, surface_structure: "freeform", textile_geometry: "unclassified", surface_relief: 0 });
}
function nativeLayer(layer, token, root, structure, maskFields = {}, channels = channelData(layer)) {
  const surface_channels = Object.fromEntries(Channels.map((name) => [name.toLowerCase(), channels[name]]));
  return {
    token, name: String(layer.name || `Pattern Layer ${token}`).slice(0, 80),
    visible: layer.visible !== false, locked: layer.locked === true,
    root, surface_channels, surface_structure: structure.structure,
    textile_geometry: structure.geometry, surface_relief: Math.max(-1, Math.min(1, finite(layer.relief, 0))),
    ...maskFields,
  };
}

// Bake layer-normalized cubic segments into tile-space segments. Each contour point
// P resolves (per the native shader convention P = B + R(θ)·(seg×size)) to an
// absolute tile position, re-normalized to the tile frame (centre 256, extent 512)
// so one instruction with bounds [256, 256, 512, 512] and zero rotation covers it.
function absoluteTileSegments(entries, centre, size, rotationDegrees) {
  const theta = finite(rotationDegrees) * Math.PI / 180;
  const cos = Math.cos(theta), sin = Math.sin(theta);
  const place = (pair) => {
    const dx = pair[0] * size[0], dy = pair[1] * size[1];
    return [(centre[0] + cos * dx - sin * dy - 256) / 512,
      (centre[1] + sin * dx + cos * dy - 256) / 512];
  };
  return entries.map((entry) => {
    const [a, b, c, d] = [entry.start_control.slice(0, 2), entry.start_control.slice(2, 4),
      entry.control_end.slice(0, 2), entry.control_end.slice(2, 4)].map(place);
    return { start_control: [...a, ...b], control_end: [...c, ...d] };
  });
}
function wrapTileSegments(convertedSegments, copies, layer) {
  const size = [finite(layer.width), finite(layer.height)];
  return copies.flatMap((copy) => absoluteTileSegments(convertedSegments, [copy.x, copy.y], size, layer.rotation));
}
function sameArray(a, b) {
  return Array.isArray(a) && Array.isArray(b) && a.length === b.length && a.every((value, index) => value === b[index]);
}
// Merge one adjacent same-paint layer pair so over-budget documents (e.g. the 65
// instruction/layer Graduated lattice) fit the GPU caps. Only opaque, unmasked,
// single-instruction layers with identical paint, stroke, fill rule and surface
// channels merge; their union renders identically wherever they are adjacent.
function mergeAdjacentLayers(instructions, segments, layers) {
  for (let index = 0; index + 1 < layers.length; index++) {
    const lower = layers[index], upper = layers[index + 1];
    if (lower.mask || upper.mask || lower.visible !== upper.visible || lower.locked !== upper.locked) continue;
    if (lower.surface_structure !== upper.surface_structure || lower.textile_geometry !== upper.textile_geometry) continue;
    if (lower.surface_relief !== upper.surface_relief) continue;
    if (JSON.stringify(lower.surface_channels) !== JSON.stringify(upper.surface_channels)) continue;
    const first = instructions[lower.root], second = instructions[upper.root];
    if (!first || !second || first.topology[0] === 28 || second.topology[0] === 28) continue;
    if (!sameArray(first.colour, second.colour) || !sameArray(first.colour_end, second.colour_end)) continue;
    if (first.colour[3] !== 1 || first.transform[2] !== 1 || second.transform[2] !== 1) continue;
    if (first.transform[1] !== second.transform[1] || first.paint[3] !== second.paint[3]) continue;
    if ((first.topology[3] & 1) !== (second.topology[3] & 1)) continue;
    if (first.topology[0] !== 6 || second.topology[0] !== 6) {
      // A full-tile background rect already covers anything painted over it in the
      // same opaque paint, so the upper layer contributes nothing.
      const covers = (item) => item.topology[0] === 0 && sameArray(item.bounds, [256, 256, 512, 512]);
      if (!covers(first)) continue;
      removeInstruction(instructions, segments, layers, upper.root);
      layers.splice(index + 1, 1);
      renumberLayerTokens(layers);
      return true;
    }
    const lowerSegs = segments.slice(first.topology[1], first.topology[1] + first.topology[2]);
    const upperSegs = segments.slice(second.topology[1], second.topology[1] + second.topology[2]);
    const merged = [
      ...absoluteTileSegments(lowerSegs, first.bounds.slice(0, 2), first.bounds.slice(2, 4), first.transform[0] * 180 / Math.PI),
      ...absoluteTileSegments(upperSegs, second.bounds.slice(0, 2), second.bounds.slice(2, 4), second.transform[0] * 180 / Math.PI),
    ];
    if (merged.length > NativeSegmentBudget) continue;
    // Snapshot every surviving segment range before rebuilding storage: the merged
    // range moves to the front and all other ranges shift down past it.
    const kept = [];
    for (const item of instructions) {
      if (item === first || item === second) continue;
      kept.push({ item, range: segments.slice(item.topology[1], item.topology[1] + item.topology[2]) });
    }
    if (kept.reduce((total, entry) => total + entry.range.length, merged.length) > NativeSegmentBudget) continue;
    segments.length = 0;
    segments.push(...merged);
    first.topology = [6, 0, merged.length, first.topology[3]];
    first.bounds = [256, 256, 512, 512];
    first.transform[0] = 0;
    for (const { item, range } of kept) {
      item.topology[1] = segments.length;
      item.topology[2] = range.length;
      segments.push(...range);
    }
    removeInstructionReference(instructions, layers, upper.root);
    layers.splice(index + 1, 1);
    renumberLayerTokens(layers);
    return true;
  }
  return false;
}
function removeInstructionReference(instructions, layers, root) {
  instructions.splice(root, 1);
  for (const layer of layers) if (layer.root > root) layer.root -= 1;
  for (const item of instructions) {
    if (item.topology[0] === 28) {
      if (item.topology[1] > root) item.topology[1] -= 1;
      if (item.topology[2] > root) item.topology[2] -= 1;
    }
    if (item.root > root) item.root -= 1;
  }
}
function removeInstruction(instructions, segments, layers, root) {
  const victim = instructions[root];
  const ranges = [];
  for (const item of instructions) {
    if (item === victim) continue;
    ranges.push({ item, range: segments.slice(item.topology[1], item.topology[1] + item.topology[2]) });
  }
  segments.length = 0;
  for (const { item, range } of ranges) {
    item.topology[1] = segments.length;
    item.topology[2] = range.length;
    segments.push(...range);
  }
  removeInstructionReference(instructions, layers, root);
}
function renumberLayerTokens(layers) {
  layers.forEach((layer, index) => { layer.token = index + 1; });
}

/** Convert a validated Pattern JSON value to native vector-authoring TOML. */
export function convertPatternJson(input) {
  const pattern = validatePattern(input);
  if (!pattern || !Array.isArray(pattern.layers)) fail("Pattern JSON did not normalize to a layer document.");
  if (pattern.Enclosures?.length || pattern.StoredGroups?.length || pattern.layers.some((layer) => layer.group || layer.EnclosureToken || layer.LinkedInstance))
    fail("Pattern folders, linked instances, and group enclosures are not yet represented by the native vector document; import was stopped without dropping them.");
  if (pattern.backgroundSource) fail("Linked or gradient Pattern backgrounds are not yet supported by the native import.");
  if (pattern.mapping !== "uv" || pattern.rotation !== 0)
    fail("Native Pattern import currently requires UV mapping and zero document rotation.");
  // Documented single-tile deviations: the GPU repeat pipeline stamps straight XY
  // tiles, so half-drop offsets and single-axis repeats are accepted as straight-XY
  // equivalents. The baked tile itself is pixel-identical; only the neighbour
  // arrangement across tile boundaries differs from the web document.
  const tilingDeviations = [];
  if (pattern.repeat !== "straight")
    tilingDeviations.push(`Document repeat “${pattern.repeat}” baked as a straight-XY tile; neighbour offsets differ from the web canvas.`);
  if (pattern.tileAxes !== "xy")
    tilingDeviations.push(`Document tile axes “${pattern.tileAxes}” baked as a straight-XY tile; single-axis repetition differs from the web canvas.`);
  // Two-pass composition: shared-segment per-copy instructions keep segment totals
  // low (weaves peak near 19k), while copy-concatenation rescues instruction-heavy
  // documents (full-bleed bands) at the cost of duplicated segments. Rebuild with
  // concatenation only when the lean layout exceeds the instruction budget.
  let built = buildConvertedLayers(pattern, [...tilingDeviations], false);
  if (built.layers.length > 64 || composedInstructionCount(built) > 128)
    built = buildConvertedLayers(pattern, [...tilingDeviations], true);
  const { instructions, segments, layers, deviations } = built;
  let mergedPairs = 0;
  while ((layers.length > 64 || composedInstructionCount(built) > 128) &&
    mergeAdjacentLayers(instructions, segments, layers)) mergedPairs++;
  if (mergedPairs) deviations.push(`Merged ${mergedPairs} adjacent same-paint layer pair${mergedPairs === 1 ? "" : "s"} to fit the 64-layer / 128-instruction GPU budget; merged layers render identically.`);
  if (layers.length > 64)
    fail("Native Pattern import supports at most 64 layers, including its solid background.");
  if (instructions.length > 128 || composedInstructionCount(built) > 128)
    fail("Pattern exceeds the native 128-instruction limit after layer and mask composition.");
  return { format: "frontier.vector_artwork", version: 1,
    name: String(pattern.name || "Imported Pattern").slice(0, 80), stamp: false,
    token_clock: layers.length, instructions, segments, layers, enclosures: [],
    deviations };
}
function composedInstructionCount(built) {
  return built.instructions.length + Math.max(0, built.layers.length - 1) +
    built.layers.filter((layer) => layer.mask).length * 2;
}
function buildConvertedLayers(pattern, deviations, concatenateCopies) {
  const instructions = [], segments = [], layers = [];
  nativeBackground(pattern, instructions, layers);
  for (const layer of pattern.layers) {
    if (["image", "svg"].includes(layer.kind))
      fail(`Layer “${layer.name}” uses ${layer.kind} content, which is not yet supported by native geometry import; import was stopped without omitting it.`);
    if (layer.kind === "text" && !layer.path.trim())
      fail(`Layer “${layer.name}” has no stored text outline to import.`);
    if (layer.kind === "flower" && (!Number.isFinite(layer.width) || !Number.isFinite(layer.height)))
      fail(`Layer “${layer.name}” is a flower without dimensions; only dimensioned flower silhouettes are currently importable.`);
    if (layer.fillSource || layer.strokeSource || layer.fillGradient || layer.strokeGradient)
      fail(`Layer “${layer.name}” uses linked or gradient paint; only literal solid colours are supported in this import phase.`);
    const channels = channelData(layer);
    if (layer.SurfaceChannels?.Normal?.Source)
      fail(`Layer “${layer.name}” supplies an image normal map; native Pattern import currently supports height-derived normals only.`);
    if ((layer.stitchRole || layer.weaveRole) &&
        (!channels.Height.enabled || channels.Height.strength <= 0 || !channels.Normal.enabled || channels.Normal.strength <= 0))
      fail(`Layer “${layer.name}” is stitch/weave geometry and requires enabled, non-zero Height and Normal contributions.`);
    if (layer.strokeLinejoin && layer.strokeLinejoin !== "round")
      deviations.push(`Layer “${layer.name}” uses ${layer.strokeLinejoin} stroke joins; baked with round joins (native SDF strokes are round).`);
    if (layer.strokeLinecap && layer.strokeLinecap !== "round")
      deviations.push(`Layer “${layer.name}” uses ${layer.strokeLinecap} stroke caps; baked with round caps (native SDF strokes are round).`);
    const sourceStrokeWidth = Math.max(0, finite(layer.strokeWidth, 0));
    // Non-sized strokes are a percentage of the layer extent; anisotropic layers use
    // the larger axis so the baked width covers the web stroke.
    if (!layer.paint && sourceStrokeWidth > 0 && Math.abs(layer.width - layer.height) > 1e-5)
      deviations.push(`Layer “${layer.name}” has an anisotropically scaled stroke; baked with a single width from the larger axis.`);
    const strokeWidth = sourceStrokeWidth * (layer.paint ? 1 : Math.max(layer.width, layer.height) / 100);
    const contours = contoursFor(layer);
    const convertedSegments = cubicSegments(contours, layer);
    if (segments.length + convertedSegments.length > NativeSegmentBudget) fail(`Pattern exceeds the native ${NativeSegmentBudget.toLocaleString("en-US")}-segment limit.`);
    const segmentStart = segments.length;
    segments.push(...convertedSegments);
    const fill = layer.paint ? layer.fillEnabled !== false : !(layer.kind === "path" && sourceStrokeWidth);
    const fillColour = parseColour(layer.color || "#ffffff", `Layer “${layer.name}” fill`);
    const strokeColour = parseColour(layer.stroke || layer.color || "#ffffff", `Layer “${layer.name}” stroke`);
    const outputOpacity = channels.Opacity.enabled ? layer.opacity * finite(layer.SurfaceChannels?.Opacity?.Strength, 1) : 1;
    const sameColour = fillColour.every((value, channel) => Math.abs(value - strokeColour[channel]) < 1e-7);
    const copies = patternCopies(layer, sourceStrokeWidth);
    if (layer.mask && copies.length > 1)
      fail(`Layer “${layer.name}” uses a mask on a tile-wrapped shape; only single-copy masked layers are currently importable.`);
    const roots = [];
    const make = (coverageLayer, colour, filled, width, segmentStartOverride, segmentCountOverride) => {
      const root = instructions.length;
      instructions.push(instruction({ layer: coverageLayer, root,
        segmentStart: segmentStartOverride ?? segmentStart, segmentCount: segmentCountOverride ?? convertedSegments.length,
        colour, filled, strokeWidth: width, fillRule: layer.fillRule }));
      roots.push(root);
    };
    const emitPaints = (coverageLayer, segmentStartOverride, segmentCountOverride) => {
      const before = roots.length;
      if (fill && strokeWidth && !sameColour) {
        make(coverageLayer, fillColour, true, 0, segmentStartOverride, segmentCountOverride);
        make(coverageLayer, strokeColour, false, strokeWidth, segmentStartOverride, segmentCountOverride);
      } else if (fill || strokeWidth) make(coverageLayer, fill ? fillColour : strokeColour, fill, strokeWidth, segmentStartOverride, segmentCountOverride);
      else fail(`Layer “${layer.name}” has neither a fill nor a stroke.`);
      const painted = roots.slice(before);
      return painted.length === 2 ? appendOver(instructions, painted[0], painted[1]) : painted[0];
    };
    let root;
    if (concatenateCopies && copies.length > 1) {
      // Tile-wrapped copies would each cost contour instructions plus Over
      // composition (up to 81 copies for full-bleed bands). Concatenating the
      // copies into one tile-space contour range renders the identical union under
      // the same fill rule while costing a single instruction per paint.
      const placement = { ...layer, x: 256, y: 256, width: 512, height: 512, rotation: 0, opacity: outputOpacity };
      const wrapped = wrapTileSegments(convertedSegments, copies, layer);
      if (segments.length + wrapped.length - convertedSegments.length > NativeSegmentBudget)
        fail(`Pattern exceeds the native ${NativeSegmentBudget.toLocaleString("en-US")}-segment limit.`);
      segments.splice(segmentStart, convertedSegments.length, ...wrapped);
      root = emitPaints(placement, segmentStart, wrapped.length);
    } else if (copies.length > 1) {
      // Lean layout: every copy shares the one contour range, so segments stay low
      // while each copy keeps its own positioning instruction.
      const copyRoots = copies.map((copy) =>
        emitPaints({ ...layer, x: copy.x, y: copy.y, opacity: outputOpacity }));
      root = copyRoots[0];
      for (let copy = 1; copy < copyRoots.length; copy++)
        root = appendOver(instructions, root, copyRoots[copy]);
    } else {
      root = emitPaints({ ...layer, x: copies[0].x, y: copies[0].y, opacity: outputOpacity });
    }
    const shape = layerStructure(layer);
    const maskFields = layer.mask ? nativeMask({ ...layer, x: copies[0].x, y: copies[0].y }) : {};
    layers.push(nativeLayer(layer, layers.length + 1, root, shape, maskFields, channels));
  }
  return { instructions, segments, layers, deviations };
}

function tomlString(value) { return `"${String(value).replaceAll("\\", "\\\\").replaceAll('"', '\\"').replaceAll("\n", "\\n").replaceAll("\r", "\\r")}"`; }
// Native coordinates parse to float32, so seven decimals keep every shipped value
// within a unit in the last place of the full-precision conversion while roughly
// halving weave TOML sizes. Integers (topology, clocks) stay exact.
function tomlNumber(value) {
  if (!Number.isFinite(value)) return "0";
  if (Number.isInteger(value)) return String(value);
  return Number(value.toFixed(7)).toString();
}
function tomlValue(value) {
  if (typeof value === "string") return tomlString(value);
  if (typeof value === "boolean") return value ? "true" : "false";
  if (typeof value === "number") return tomlNumber(value);
  if (Array.isArray(value)) return `[${value.map(tomlValue).join(", ")}]`;
  if (value && typeof value === "object") return `{ ${Object.entries(value).map(([key, item]) => `${key} = ${tomlValue(item)}`).join(", ")} }`;
  return "0";
}
export function serializeNativeToml(document) {
  const lines = [
    `format = ${tomlString(document.format)}`,
    `version = ${document.version}`,
    `name = ${tomlString(document.name)}`,
    `stamp = ${document.stamp}`,
    `token_clock = ${document.token_clock}`,
    `instructions = ${tomlValue(document.instructions.map(({ root, ...item }) => item))}`,
    `segments = ${tomlValue(document.segments)}`,
    `layers = ${tomlValue(document.layers)}`,
    `enclosures = []`,
    "",
  ];
  return lines.join("\n");
}

async function main() {
  const [inputFile, outputFile] = process.argv.slice(2);
  if (!inputFile || !outputFile || process.argv.length !== 4) {
    console.error("Usage: node Automation/ConvertWebPattern.mjs <pattern.json> <native.vector.toml>");
    process.exitCode = 2;
    return;
  }
  const input = JSON.parse(fs.readFileSync(inputFile, "utf8"));
  const converted = convertPatternJson(input);
  const result = serializeNativeToml(converted);
  fs.mkdirSync(path.dirname(path.resolve(outputFile)), { recursive: true });
  fs.writeFileSync(outputFile, result, "utf8");
  console.log(`Converted ${input.layers?.length ?? 0} Pattern layers to ${outputFile}`);
  for (const note of converted.deviations || []) console.error(`NOTE: ${note}`);
}
if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  try { await main(); }
  catch (error) { console.error(`Pattern import failed: ${error.message}`); process.exitCode = 1; }
}
