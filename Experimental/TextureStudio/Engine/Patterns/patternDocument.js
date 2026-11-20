import { NormalizePatternOrganization } from "./PatternOrganization.js";
import { NormalizePatternChannels, PatternChannelEnabled, PatternChannelStrength } from "./PatternSurfaceChannels.js";
import { NormalizeArtworkNames } from "./ArtworkNames.js";
import { NormalizeArtworkMask, ApplyArtworkMask } from "./ArtworkMask.js";
import { NormalizePatternColourSource, PatternColourReference, PatternColourDefinitions, PatternGroundLayer, PatternGroundRectangles } from "./PatternColourSources.js";
import {
  normalizePatternNodes,
  patternNodesPath,
  patternPolygonNodes,
  patternSizedPath,
  patternPrimitiveNodes,
} from "./patternGeometry.js";
import {
  geometricCatalog,
  geometricPattern,
} from "./geometricConstructions.js";
import { heritagePatternCatalog, heritagePattern } from "./heritagePatterns.js";
import { normalizeWeave } from "./patternWeaves.js";
import { ornamentalPattern } from "./ornamentalConstructions.js";
import {
  referencePattern,
  referenceDiamondPattern,
} from "./referencePatterns.js";
import { richRugPattern, normalizeRugComposition } from "./rugCompositions.js";
import {
  textileLibraryEntries,
  textilePalettes,
  textilePattern,
} from "./patternLibrary.js";
import { normalizeStitches } from "./patternStitches.js";
import {
  collectionPattern,
  normalizeCollectionFade,
} from "./patternCollections.js";
import { sanitizePatternSVG } from "./patternImport.js";
import { arrayInset, mirrorMotif, alignToEdges, gridArray, scatterPlacement } from "./patternProcedural.js";
// Portable vector pattern documents. No DOM dependency; shared by editor/export.
export const patternFinishes = {
  ink: {
    name: "Printed dye",
    roughness: 0.62,
    metalness: 0,
    height: 0,
    code: 0,
  },
  cotton: {
    name: "Woven cotton",
    roughness: 0.83,
    metalness: 0,
    height: 0.08,
    code: 1,
  },
  wool: {
    name: "Cut-pile wool",
    roughness: 0.94,
    metalness: 0,
    height: 0.65,
    code: 2,
  },
  ceramic: {
    name: "Glazed ceramic",
    roughness: 0.2,
    metalness: 0,
    height: 0.18,
    code: 3,
  },
  foil: {
    name: "Metal inlay",
    roughness: 0.28,
    metalness: 1,
    height: 0.04,
    code: 4,
  },
};
const patternNumber = (v, d, lo, hi) =>
  Number.isFinite(Number(v)) ? Math.min(hi, Math.max(lo, Number(v))) : d;
const patternColor = (v, d = "#dd765d") =>
  /^#[a-f\d]{6}$/i.test(v || "") ? v : d;
// Source metadata is intentionally independent of font loading; stored outlines
// remain authoritative for offline and material exports.
export function normalizePatternText(input = {}) {
  return {
    value: String(input.value ?? "Text")
      .replace(
        /[^\S\n\r\t ]|[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/g,
        " ",
      )
      .slice(0, 2000),
    fontFamily:
      input.fontFamily === "Space Grotesk" ? "Space Grotesk" : "DM Sans",
    fontWeight: Number(input.fontWeight) === 600 ? 600 : 400,
    fontSize: patternNumber(input.fontSize, 48, 4, 256),
    lineHeight: patternNumber(input.lineHeight, 1.2, 0.5, 3),
    letterSpacing: patternNumber(input.letterSpacing, 0, -10, 40),
    align: ["center", "right"].includes(input.align) ? input.align : "left",
  };
}
export function validatePattern(input) {
  if (
    !input ||
    input.schema !== "alloy.pattern.v1" ||
    !Array.isArray(input.layers)
  )
    throw new Error("Not an Alloy pattern document.");
  if (JSON.stringify(input).length > 12000000)
    throw new Error("Pattern document exceeds 12 MB.");
  if (input.layers.length > 64)
    throw new Error("A pattern supports up to 64 layers.");
  if (Array.isArray(input.StoredGroups) && (input.StoredGroups.length > 24 || new Set(input.StoredGroups.map(Definition => Definition.Token)).size !== input.StoredGroups.length)) throw new Error("Stored group identities must be unique, with at most 24 definitions.");
  const tileAxes = ["x", "y", "none"].includes(input.tileAxes)
    ? input.tileAxes
    : "xy";
  return NormalizePatternOrganization({
    Enclosures: input.Enclosures,
    EditGroupProperties: input.EditGroupProperties,
    EditLinkedInstances: input.EditLinkedInstances,
    StoredGroups: (Array.isArray(input.StoredGroups) ? input.StoredGroups : []).slice(0, 24).map(Definition => {
      const Snapshot = validatePattern({ schema: "alloy.pattern.v1", layers: Definition.Layers, Enclosures: Definition.Enclosures });
      if (!Definition.Token || Snapshot.layers.some(Layer => !Layer.LinkedSlot) || new Set(Snapshot.layers.map(Layer => Layer.LinkedSlot)).size !== Snapshot.layers.length) throw new Error("Stored groups require unique feature-slot identities.");
      return { Token: String(Definition.Token).slice(0, 80), Caption: String(Definition.Caption || "Stored group").slice(0, 60), Layers: Snapshot.layers, Enclosures: Snapshot.Enclosures };
    }),
    schema: "alloy.pattern.v1",
    name: String(input.name || "Untitled pattern").slice(0, 80),
    ...(input.collection
      ? { collection: String(input.collection).slice(0, 60) }
      : {}),
    ...(input.presentation === "rug" ? { presentation: "rug" } : {}),
    ...(input.fade ? { fade: normalizeCollectionFade(input.fade) } : {}),
    tileAxes,
    ...(input.designAspect
      ? { designAspect: patternNumber(input.designAspect, 1, 0.5, 2) }
      : {}),
    ...(input.construction
      ? { construction: String(input.construction).slice(0, 80) }
      : {}),
    ...(input.referenceDesign
      ? { referenceDesign: String(input.referenceDesign).slice(0, 80) }
      : {}),
    ...(input.beadwork
      ? {
          beadwork: {
            columns: patternNumber(input.beadwork.columns, 96, 24, 180),
            rows: patternNumber(input.beadwork.rows, 72, 24, 180),
            height: patternNumber(input.beadwork.height, 0.004, 0.0001, 0.012),
          },
        }
      : {}),
    ...(input.ornament
      ? { ornament: normalizeRugComposition(input.ornament) }
      : {}),
    ...(input.library && typeof input.library === "object"
      ? {
          library: {
            family: String(input.library.family || "").slice(0, 60),
            palette: String(input.library.palette || "").slice(0, 30),
          },
        }
      : {}),
    ...(input.stitch ? { stitch: normalizeStitches(input.stitch) } : {}),
    ...(input.weave ? { weave: normalizeWeave(input.weave) } : {}),
    background: patternColor(input.background, "#eee8dc"),
    ...(NormalizePatternColourSource(input.backgroundSource) ? { backgroundSource: NormalizePatternColourSource(input.backgroundSource) } : {}),
    backgroundOpacity: patternNumber(input.backgroundOpacity, 1, 0, 1),
    repeat:
      tileAxes !== "xy"
        ? "straight"
        : ["straight", "half-drop", "mirror"].includes(input.repeat)
          ? input.repeat
          : "straight",
    repeats: patternNumber(input.repeats, 2, 0.25, 24),
    rotation: patternNumber(input.rotation, 0, -180, 180),
    mapping: ["object", "cylinder"].includes(input.mapping)
      ? input.mapping
      : "uv",
    layers: NormalizeArtworkNames(input.layers.map((l, i) => {
      if (!l || typeof l !== "object" || Array.isArray(l))
        throw new Error("Each pattern layer must be an object.");
      const finish = Object.hasOwn(patternFinishes, l.finish)
        ? l.finish
        : "ink";
      const defaults = patternFinishes[finish];
      const kind = [
        "rect",
        "ellipse",
        "diamond",
        "triangle",
        "flower",
        "polygon",
        "star",
        "path",
        "image",
        "svg",
        "text",
        "arc",
      ].includes(l.kind)
        ? l.kind
        : "rect";
      const nodes =
        kind === "path" && l.nodes ? normalizePatternNodes(l.nodes) : null;
      const rawPath = String(l.path || "");
      if (!nodes && rawPath.length > 100000)
        throw new Error(
          "SVG paths are limited to 100,000 characters. Split the artwork into more layers.",
        );
      const path = nodes ? patternNodesPath(nodes, l.closed === true) : rawPath;
      if (
        ["path", "text"].includes(kind) &&
        (path.match(/[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?/g) || []).some(
          (v) => !Number.isFinite(Number(v)),
        )
      )
        throw new Error("SVG path numbers must be finite.");
      if (
        ["path", "text"].includes(kind) &&
        !/^[MmLlHhVvCcSsQqTtAaZz\d\s.,+eE-]*$/.test(path)
      )
        throw new Error("Invalid SVG path.");
      const src = String(l.src || "");
      if (
        kind === "image" &&
        (!/^data:image\/(png|jpeg|webp);base64,[a-z\d+/=\s]+$/i.test(src) ||
          src.length > 6000000)
      )
        throw new Error(
          "Images must be embedded PNG, JPEG or WebP, at most 4 MB.",
        );
      return {
        id: "layer-" + i,
        ...Object.fromEntries(["EnclosureToken", "StructureToken", "LinkedDefinition", "LinkedSlot", "LinkedInstance"].filter(Key => typeof l[Key] === "string").map(Key => [Key, l[Key].slice(0, 80)])),
        ...(l.SurfaceChannels ? { SurfaceChannels: NormalizePatternChannels(l.SurfaceChannels) } : {}),
        ...(l.mask ? { mask: NormalizeArtworkMask(l.mask) } : {}),
        name: String(l.name || kind).slice(0, 60),
        kind,
        x: patternNumber(l.x, 256, -512, 1024),
        y: patternNumber(l.y, 256, -512, 1024),
        width: patternNumber(l.width, 100, 1, 1024),
        height: patternNumber(l.height, 100, 1, 1024),
        rotation: patternNumber(l.rotation, 0, -360, 360),
        ...(l.ornamentRole
          ? { ornamentRole: String(l.ornamentRole).slice(0, 60) }
          : {}),
        ...(l.fillRule === "evenodd" ? { fillRule: "evenodd" } : {}),
        ...(["holes", "thread", "highlight"].includes(l.stitchRole)
          ? { stitchRole: l.stitchRole }
          : {}),
        ...(l.weaveRole === "yarn" ? { weaveRole: "yarn" } : {}),
        flipX: l.flipX === true,
        flipY: l.flipY === true,
        opacity: patternNumber(l.opacity, 1, 0, 1),
        ...(l.locked === true ? { locked: true } : {}),
        ...(l.group
          ? {
              group: String(l.group)
                .replace(/[^\w-]/g, "")
                .slice(0, 80),
            }
          : {}),
        ...(l.paint === true || ["text", "arc"].includes(kind)
          ? {
              paint: true,
              fillEnabled:
                l.fillEnabled !== undefined
                  ? l.fillEnabled !== false
                  : kind !== "arc" || ["chord", "pie"].includes(l.arcClosure),
            }
          : {}),
        ...(kind === "rect" && l.cornerRadius !== undefined
          ? { cornerRadius: patternNumber(l.cornerRadius, 0, 0, 50) }
          : {}),
        ...(kind === "polygon"
          ? { sides: Math.round(patternNumber(l.sides, 6, 3, 24)) }
          : {}),
        ...(kind === "text" ? { text: normalizePatternText(l.text) } : {}),
        ...(kind === "arc"
          ? {
              arcStart: patternNumber(l.arcStart, -90, -360, 360),
              arcSweep: patternNumber(l.arcSweep, 180, -359.99, 359.99),
              arcClosure: ["chord", "pie"].includes(l.arcClosure)
                ? l.arcClosure
                : "open",
            }
          : {}),
        ...(kind === "star"
          ? {
              starPoints: Math.round(patternNumber(l.starPoints, 5, 3, 20)),
              innerRadius: patternNumber(l.innerRadius, 0.45, 0.05, 0.95),
            }
          : {}),
        ...(nodes ? { nodes, closed: l.closed === true } : {}),
        ...(nodes && l.curveTension !== undefined
          ? { curveTension: patternNumber(l.curveTension, 1, 0, 1) }
          : {}),
        color: patternColor(l.color),
        stroke: patternColor(l.stroke, "#222222"),
        ...(NormalizePatternColourSource(l.fillSource) ? { fillSource: NormalizePatternColourSource(l.fillSource) } : {}),
        ...(NormalizePatternColourSource(l.strokeSource) ? { strokeSource: NormalizePatternColourSource(l.strokeSource) } : {}),
        strokeWidth: patternNumber(l.strokeWidth, 0, 0, 25),
        ...(["butt", "round", "square"].includes(l.strokeLinecap)
          ? { strokeLinecap: l.strokeLinecap }
          : {}),
        ...(["miter", "round", "bevel"].includes(l.strokeLinejoin)
          ? { strokeLinejoin: l.strokeLinejoin }
          : {}),
        finish,
        roughness: patternNumber(l.roughness, defaults.roughness, 0.05, 1),
        metalness: patternNumber(l.metalness, defaults.metalness, 0, 1),
        relief: patternNumber(l.relief, defaults.height, -1, 1),
        visible: l.visible !== false,
        path,
        ...(kind === "image" ? { src } : {}),
        ...(kind === "svg"
          ? { svg: sanitizePatternSVG(String(l.svg || "")) }
          : {}),
      };
    })),
  });
}
export function patternLayer(kind = "diamond", extra = {}) {
  return {
    kind,
    name: kind,
    x: 256,
    y: 256,
    width: 160,
    height: 160,
    rotation: 0,
    opacity: 1,
    color: "#dd765d",
    finish: "cotton",
    ...extra,
  };
}
// Public starters only. Retired generators below are retained solely for old
// exported factories; saved documents load their embedded paths unchanged.
export const canonicalTextileEntries = textileLibraryEntries
  .filter(
    (p) =>
      (p.palette === "Indigo" || p.palette.startsWith("Single")) &&
      !["Micro dots", "Awning stripes", "Candy stripes"].includes(p.family),
  )
  .map((p) => ({
    ...p,
    name: p.family,
    colorways: p.palette.startsWith("Single")
      ? []
      : Object.keys(textilePalettes),
  }));
export const patternStarterCatalog = [
  ...geometricCatalog,
  {
    name: "Diamond Dissolve",
    group: "Geometric designs",
    description:
      "Retained geometric size fade: density changes motif size, not opacity.",
  },
  ...canonicalTextileEntries,
  ...heritagePatternCatalog,
  ...[
    "Diamond weave",
    "Painted blossoms",
    "Cube lattice",
    "Inlaid tile",
    "Banded geometry",
    "Graduated lattice",
    "Blank",
  ].map((name) => ({ name, group: "Originals" })),
];
export const patternStarterNames = patternStarterCatalog.map((p) => p.name);
export function resolvePatternStarterName(slug) {
  // Normalize diacritics so human-readable names still have portable ASCII URLs.
  // Search only the public catalog; retired rugs cannot return through old links.
  const normalize = (value) =>
    String(value || "")
      .normalize("NFD")
      .replace(/[\u0300-\u036f]/g, "")
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, "-")
      .replace(/^-|-$/g, "");
  const target = normalize(slug);
  return patternStarterNames.find((name) => normalize(name) === target);
}
export const patternInventory = {
  catalogEntries: patternStarterCatalog.length - 1,
  stitches: patternStarterCatalog.filter((p) => p.group === "Stitch patterns")
    .length,
  weaves: patternStarterCatalog.filter((p) => p.group === "Fabric weaves")
    .length,
};
// No quota, inferred artistic quality score, or "200 structures" claim.
export const patternStructureManifest = patternStarterCatalog
  .filter((p) => p.name !== "Blank")
  .map((p) => ({
    name: p.name,
    group: p.group,
    structure: p.id || p.family || p.name,
    basis:
      p.description ||
      "Editable pattern family; settings and colors do not add entries.",
  }));
export function patternStarter(name = "Diamond weave", colorway = "Indigo") {
  if (
    [
      "Chromatic Diamond Tapestry",
      "chromatic-diamond-tapestry",
      "African Diamond Carpet",
    ].includes(name)
  )
    return validatePattern({
      ...referenceDiamondPattern(),
      ...(name === "African Diamond Carpet"
        ? { name, collection: "African-inspired" }
        : {}),
    });
  const geometry = geometricPattern(name) || heritagePattern(name);
  if (geometry) return validatePattern(geometry);
  const family = canonicalTextileEntries.find((p) => p.name === name);
  if (family?.colorways.length)
    name =
      name +
      " - " +
      (family.colorways.includes(colorway) ? colorway : "Indigo");
  const collection =
    ornamentalPattern(name) ||
    referencePattern(name) ||
    richRugPattern(name) ||
    textilePattern(name) ||
    collectionPattern(name);
  if (collection) return validatePattern(collection);
  const d = {
    schema: "alloy.pattern.v1",
    name,
    background: "#263c48",
    repeat: "straight",
    repeats: 2,
    layers: [],
  };
  if (name === "Diamond weave") {
    d.layers.push(
      patternLayer("rect", {
        width: 512,
        height: 512,
        color: "#263c48",
        finish: "wool",
        relief: 0.3,
      }),
    );
    // 📝 Procedural reconstruction (array/inset + mirror + align-to-edge, per the pattern's own
    // worked example): one inset array builds the "inner and outer" weave rings from a single base
    // diamond, a vertical mirror produces the bottom row for free, and an edge alignment places two
    // independently-coloured copies at the tile's left/right quarters -- composable operations over
    // one base shape, not a hand-authored coordinate list.
    const WeaveRing = arrayInset(patternLayer, "diamond", { x: 0, y: 128, width: 242, height: 242, finish: "wool" }, {
      count: 4,
      sizeStep: 53,
      colors: ["accent", "#263c48"],
      roles: ["accent", "ground"],
    });
    const WeaveRows = mirrorMotif(WeaveRing, { axis: "y", through: 256 });
    d.layers.push(
      ...alignToEdges(WeaveRows, [
        { x: 128, y: 0, recolor: { accent: "#cc705b" } },
        { x: 384, y: 0, recolor: { accent: "#d8b16b" } },
      ]),
    );
  } else if (name === "Painted blossoms") {
    d.background = "#fff5e4";
    d.repeat = "half-drop";
    // 📝 Scatter: a deterministic pseudo-random placement tool, instead of inlined modulo math.
    for (let i = 0; i < 7; i++) {
      const { x, y } = scatterPlacement(i, { offsetX: 37, offsetY: 64 });
      d.layers.push(
        patternLayer("flower", {
          x,
          y,
          width: 100 + (i % 3) * 27,
          height: 100 + (i % 3) * 27,
          color: i % 2 ? "#fa9288" : "#e6505b",
          rotation: i * 21,
          finish: "cotton",
        }),
      );
      d.layers.push(
        patternLayer("ellipse", {
          x: x + 54,
          y: y + 56,
          width: 19,
          height: 45,
          rotation: 35,
          color: "#8c8545",
          finish: "cotton",
        }),
      );
      d.layers.push(
        patternLayer("ellipse", {
          x,
          y,
          width: 23,
          height: 23,
          color: "#9b633c",
          finish: "cotton",
        }),
      );
    }
  } else if (name === "Cube lattice") {
    d.background = "#207585";
    // 📝 Grid: one cube motif repeated on a brick-offset lattice (odd rows shift half a step).
    d.layers.push(
      ...gridArray(
        patternLayer,
        "path",
        {
          width: 128,
          height: 128,
          color: "#ecf0e3",
          path: "M 50 0 L 94 25 L 94 75 L 50 100 L 6 75 L 6 25 Z M 50 0 L 50 50 L 94 75 M 50 50 L 6 75",
          strokeWidth: 4,
          stroke: "#ecf0e3",
          finish: "ceramic",
        },
        { columns: 4, rows: 4, stepX: 128, stepY: 128, rowOffsetX: (row) => (row % 2) * 64 },
      ),
    );
  } else if (name === "Inlaid tile") {
    d.background = "#e4ded1";
    d.layers.push(
      patternLayer("rect", {
        width: 490,
        height: 490,
        color: "#194e60",
        finish: "ceramic",
      }),
    );
    // 📝 Array/Inset: one diamond shrinking into 5 concentric rings, alternating finish with colour.
    d.layers.push(
      ...arrayInset(
        patternLayer,
        "diamond",
        { width: 450, height: 450 },
        {
          count: 5,
          sizeStep: 85,
          extras: [
            { color: "#d3ae62", finish: "foil" },
            { color: "#194e60", finish: "ceramic" },
          ],
        },
      ),
    );
  } else if (name === "Banded geometry") {
    d.background = "#253940";
    d.repeats = 1;
    d.layers.push(
      patternLayer("rect", {
        width: 512,
        height: 512,
        color: d.background,
        finish: "wool",
        relief: 0.25,
      }),
    );
    const colors = ["#d6b77a", "#df7658", "#e7dfc9", "#87aaa0"];
    for (let band = 0; band < 4; band++) {
      const y = 64 + band * 128;
      // 📝 Mirror: the two thin rule lines bracketing a band are one line reflected about the band's
      // own centre line.
      d.layers.push(
        ...mirrorMotif(
          [
            patternLayer("rect", {
              x: 256,
              y: y - 54,
              width: 512,
              height: 7,
              color: colors[band],
              finish: "cotton",
              relief: 0.1,
            }),
          ],
          { axis: "y", through: y },
        ),
      );
      // 📝 Grid: one row of 6 alternating motifs along the band.
      d.layers.push(
        ...gridArray(
          patternLayer,
          band % 2 ? "path" : "triangle",
          {
            x: 42.667,
            y,
            width: 74,
            height: 78,
            color: colors[band],
            finish: "wool",
            relief: 0.7,
            path:
              band === 1
                ? "M50 0 L100 50 L50 100 L0 50 Z M50 23 L77 50 L50 77 L23 50 Z"
                : "M0 20 L25 0 L50 20 L75 0 L100 20 M0 60 L25 40 L50 60 L75 40 L100 60 M0 100 L25 80 L50 100 L75 80 L100 100",
            strokeWidth: band % 2 ? 4 : 0,
          },
          { columns: 6, rows: 1, stepX: 85.333, rotationAt: () => (band % 2 ? 0 : undefined) },
        ).map((layer, column) => (band % 2 ? layer : { ...layer, rotation: (column % 2) * 180 })),
      );
    }
  } else if (name === "Medallion rug") {
    d.background = "#253b40";
    d.repeats = 1;
    // 📝 Array/Inset: five concentric frames, alternating relief with colour.
    d.layers.push(
      ...arrayInset(
        patternLayer,
        "rect",
        { width: 512, height: 512, finish: "wool" },
        {
          count: 5,
          sizeStep: 22,
          colors: ["#d8be8c", "#784b43", "#d8be8c", "#784b43", "#253b40"],
          extras: [{ relief: 0.65 }, { relief: 0.4 }],
        },
      ),
    );
    const star =
      "M50 0L63 24L86 14L76 38L100 50L76 63L86 86L63 76L50 100L37 76L14 86L24 63L0 50L24 38L14 14L37 24Z";
    // 📝 Array/Inset: four concentric stars, relief fading ring to ring.
    d.layers.push(
      ...arrayInset(
        patternLayer,
        "path",
        { path: star, width: 310, height: 310, finish: "wool" },
        {
          count: 4,
          sizeStep: 53,
          colors: ["#d8be8c", "#b7664e", "#253b40", "#87aaa0"],
          extrasAt: (i) => ({ relief: 0.8 - i * 0.1 }),
        },
      ),
    );
    // 📝 Array/Inset + Align-to-edge: a two-ring diamond cluster repeated at the tile's four corners.
    const corner = arrayInset(
      patternLayer,
      "diamond",
      { x: 0, y: 0, width: 64, height: 64, finish: "wool" },
      { count: 2, sizeStep: 26, colors: ["#d8be8c", "#b7664e"] },
    );
    for (const x of [106, 406]) for (const y of [106, 406]) d.layers.push(...alignToEdges(corner, [{ x, y }]));
  } else if (name === "Graduated lattice") {
    d.background = "#236777";
    d.repeats = 1;
    // 📝 Grid with a per-row size gradient: one cube motif on an 8x8 lattice, scaled by a sine-wave
    // envelope across the rows -- the "gradient array that scales with distance/size" the library
    // needs a named tool for.
    d.layers.push(
      ...gridArray(
        patternLayer,
        "path",
        {
          x: 32,
          y: 32,
          color: "#f0efdf",
          finish: "ceramic",
          relief: 0.1,
          path: "M50 0L94 25L94 75L50 100L6 75L6 25Z M50 0L50 50L94 75 M50 50L6 75",
          strokeWidth: 4,
        },
        {
          columns: 8,
          rows: 8,
          stepX: 64,
          stepY: 64,
          sizeAt: (row) => 22 + 38 * Math.pow(Math.sin((Math.PI * (row + 0.5)) / 8), 2),
        },
      ),
    );
  } else {

    d.name = "Custom pattern";
    d.background = "#ece5d6";
  }
  return validatePattern(d);
}
export function patternDimensions(doc) {
  return [
    doc.repeat === "straight" ? 512 : 1024,
    doc.repeat === "mirror" ? 1024 : 512,
  ];
}
export function patternLayerTransform(
  l,
  { x = l.x, y = l.y, sx = 1, sy = 1 } = {},
) {
  const sized = l.paint && !["image", "svg"].includes(l.kind);
  return `translate(${x} ${y}) rotate(${l.rotation * sx * sy}) scale(${sx * (l.flipX ? -1 : 1) * (sized ? 1 : l.width / 100)} ${sy * (l.flipY ? -1 : 1) * (sized ? 1 : l.height / 100)})${sized ? "" : " translate(-50 -50)"}`;
}
export function patternShape(l, fill, channel = "color") { return ApplyArtworkMask(l, PatternUnmaskedShape(l, fill, channel)); }
function PatternUnmaskedShape(l, fill, channel = "color") {
  // Native editor paths are sized geometrically, not by an anisotropic parent
  // transform. Their stroke then scales normally with SVG viewBox/raster size.
  // This also avoids vector-effect's resolution-dependent behavior in SVG images.
  const filled = l.paint
      ? l.fillEnabled !== false
      : !(l.kind === "path" && l.strokeWidth),
    stroke = channel === "color" ? PatternColourReference(l, "stroke", l.paint ? l.stroke : fill) : fill,
    SourceFill = channel === "color" ? PatternColourReference(l, "fill", fill) : fill,
    style = `fill-rule="${l.fillRule === "evenodd" ? "evenodd" : "nonzero"}" fill="${filled ? SourceFill : "none"}" stroke="${l.strokeWidth ? stroke : "none"}" stroke-width="${l.strokeWidth || 0}" stroke-linecap="${l.strokeLinecap || "round"}" stroke-linejoin="${l.strokeLinejoin || "round"}"`;
  if (l.kind === "svg")
    return l.svg
      .replace(/id="([^"]+)"/g, `id="${l.id}-$1"`)
      .replace(/url\(#([^)]*)\)/g, `url(#${l.id}-$1)`)
      .replace(/((?:xlink:)?href)="#([^"]+)"/g, `$1="#${l.id}-$2"`);
  if (l.kind === "image")
    return `<image href="${l.src}" x="0" y="0" width="100" height="100" preserveAspectRatio="none"/>`;
  if (l.paint) {
    if (l.kind === "rect")
      return `<rect x="${-l.width / 2}" y="${-l.height / 2}" width="${l.width}" height="${l.height}" rx="${((l.cornerRadius || 0) * l.width) / 100}" ry="${((l.cornerRadius || 0) * l.height) / 100}" ${style}/>`;
    if (l.kind === "ellipse")
      return `<ellipse cx="0" cy="0" rx="${l.width / 2}" ry="${l.height / 2}" ${style}/>`;
    if (l.kind === "flower") {
      const circle = patternPrimitiveNodes({ kind: "ellipse" }).nodes;
      return [0, 72, 144, 216, 288]
        .map((angle) => {
          const a = (angle * Math.PI) / 180,
            petal = (p) => {
              const x = (p.x - 50) * 0.36,
                y = 27 + (p.y - 50) * 0.54 - 50;
              return {
                x: 50 + x * Math.cos(a) - y * Math.sin(a),
                y: 50 + x * Math.sin(a) + y * Math.cos(a),
              };
            },
            nodes = circle.map((n) => ({
              ...petal(n),
              in: petal(n.in),
              out: petal(n.out),
            }));
          return `<path d="${patternSizedPath({ ...l, nodes, closed: true })}" ${style}/>`;
        })
        .join("");
    }
    const d = patternSizedPath(l);
    if (d !== null) return `<path d="${d}" ${style}/>`;
    // Invalid/incomplete old source paths remain editable in the source panel.
    return `<path d="${l.path}" transform="scale(${l.width / 100} ${l.height / 100}) translate(-50 -50)" ${style}/>`;
  }
  if (l.kind === "rect")
    return `<rect width="100" height="100" rx="${l.cornerRadius || 0}" ${style}/>`;
  if (l.kind === "ellipse")
    return `<ellipse cx="50" cy="50" rx="50" ry="50" ${style}/>`;
  if (l.kind === "diamond")
    return `<path d="M50 0L100 50L50 100L0 50Z" ${style}/>`;
  if (l.kind === "triangle") return `<path d="M50 0L100 100L0 100Z" ${style}/>`;
  if (l.kind === "polygon" || l.kind === "star") {
    const nodes = patternPolygonNodes(
      l.kind === "polygon" ? l.sides : l.starPoints,
      l.kind === "star" ? l.innerRadius : null,
    );
    return `<path d="${patternNodesPath(nodes, true)}" ${style}/>`;
  }
  if (l.kind === "flower")
    return [0, 72, 144, 216, 288]
      .map(
        (a) =>
          `<ellipse cx="50" cy="27" rx="18" ry="27" transform="rotate(${a} 50 50)" ${style}/>`,
      )
      .join("");
  return `<path d="${l.path}" ${style}/>`;
}
export const textilePreviewDefs = `<defs><filter id="textile-relief" x="-10%" y="-10%" width="120%" height="120%" color-interpolation-filters="sRGB"><feDropShadow dx=".12" dy=".18" stdDeviation=".13" flood-color="#000000" flood-opacity=".55"/></filter></defs>`;
export function textilePreviewShape(l, fill, IncludeDefinitions = true) {
  const shadow =
    l.stitchRole === "thread" ||
    l.stitchRole === "highlight" ||
    (l.weaveRole && l.name === "Yarn silhouette");
  const shape = patternShape(l, fill, "color");
  return (IncludeDefinitions ? `<defs>${PatternColourDefinitions(l)}</defs>` : "") + (shadow ? `<g filter="url(#textile-relief)">${shape}</g>` : shape);
}
export function patternSVG(input, mode = "color") {
  const preview = mode === "preview";
  if (preview) mode = "color";
  const d = validatePattern(input),
    [w, h] = patternDimensions(d);
  let body = mode === "color" ? PatternGroundRectangles(d, w, h) : `<rect width="${w}" height="${h}" fill="${mode === "finish" ? "rgb(255,0,0)" : "rgb(158,0,128)"}" opacity="0"/>`;
  if (mode === "color") body = `<defs>${PatternColourDefinitions(PatternGroundLayer(d))}${d.layers.filter(l => l.visible).map(PatternColourDefinitions).join("")}</defs>` + body;
  if (preview) body = textilePreviewDefs + body;
  for (const l of d.layers.filter((l) => l.visible)) {
    if ((mode === "occlusion" && !l.SurfaceChannels?.Occlusion?.Enabled) || (mode === "normalHeight" && (!PatternChannelEnabled(l, "Height") || !PatternChannelEnabled(l, "Normal") || PatternChannelStrength(l, "Normal") === 0))) continue;
    if ((mode === "roughness" && !PatternChannelEnabled(l, "Roughness")) || (mode === "metalness" && !PatternChannelEnabled(l, "Metalness")) || (mode === "color" && !PatternChannelEnabled(l, "Color")) || (mode === "height" && !PatternChannelEnabled(l, "Height"))) continue;
    const f = patternFinishes[l.finish];
    const padding = l.paint
      ? l.strokeWidth *
        (l.strokeLinejoin === "miter"
          ? 2
          : l.strokeLinecap === "square"
            ? Math.SQRT1_2
            : 0.5)
      : ((l.strokeWidth || 0) * Math.max(l.width, l.height)) / 200;
    const HeightContribution = PatternChannelEnabled(l, "Height") ? l.relief * PatternChannelStrength(l, "Height") : 0;
    const NormalContribution = PatternChannelEnabled(l, "Normal") ? PatternChannelStrength(l, "Normal") : 0;
    const fill =
      mode === "normalHeight" ? `rgb(${Math.round(128 + HeightContribution * NormalContribution * 25)},${Math.round(128 + HeightContribution * NormalContribution * 25)},${Math.round(128 + HeightContribution * NormalContribution * 25)})`
      : mode === "height" ? `rgb(${Math.round(128 + HeightContribution * 100)},${Math.round(128 + HeightContribution * 100)},${Math.round(128 + HeightContribution * 100)})`
      : mode === "channelMask" ? `rgb(${PatternChannelEnabled(l, "Roughness") ? 255 : 0},${PatternChannelEnabled(l, "Metalness") ? 255 : 0},${Math.round((l.SurfaceChannels?.Occlusion?.Enabled ? PatternChannelStrength(l, "Occlusion") : 1) * 255)})`
      : mode === "roughness" ? `rgb(${Math.round(l.roughness * 255)},${Math.round(l.roughness * 255)},${Math.round(l.roughness * 255)})`
      : mode === "metalness" ? `rgb(${Math.round(l.metalness * 255)},${Math.round(l.metalness * 255)},${Math.round(l.metalness * 255)})`
      : mode === "occlusion" ? `rgb(${Math.round((l.SurfaceChannels?.Occlusion?.Enabled ? PatternChannelStrength(l, "Occlusion") : 1) * 255)},${Math.round((l.SurfaceChannels?.Occlusion?.Enabled ? PatternChannelStrength(l, "Occlusion") : 1) * 255)},${Math.round((l.SurfaceChannels?.Occlusion?.Enabled ? PatternChannelStrength(l, "Occlusion") : 1) * 255)})`
      : mode === "opacity" ? "white"
      : mode === "normal" ? "rgb(128,128,255)"
      : mode === "runtimeParams" ? `rgb(${Math.round(l.roughness * 255)},${Math.round(l.metalness * 255)},${Math.round(128 + HeightContribution * NormalContribution * 25)})`
      : mode === "color"
        ? l.color
        : `rgb(${Math.round(l.roughness * 255)},${Math.round(l.metalness * 255)},${Math.round(128 + HeightContribution * NormalContribution * 100)})`;
    // Each motif is wrapped, including motifs straddling the repeat boundary.
    for (let col = -4; col <= 4; col++)
      for (let row = -4; row <= 4; row++) {
        if (
          ((d.tileAxes === "x" || d.tileAxes === "none") && row !== 0) ||
          ((d.tileAxes === "y" || d.tileAxes === "none") && col !== 0)
        )
          continue;
        const mirror = d.repeat === "mirror",
          sx = mirror && Math.abs(col % 2) ? -1 : 1,
          sy = mirror && Math.abs(row % 2) ? -1 : 1;
        const x = col * 512 + (sx < 0 ? 512 - l.x : l.x),
          y =
            row * 512 +
            (sy < 0 ? 512 - l.y : l.y) +
            (d.repeat === "half-drop" && Math.abs(col % 2) ? 256 : 0);
        if (
          x + (l.width + l.height) / 2 + padding < 0 ||
          x - (l.width + l.height) / 2 - padding > w ||
          y + (l.width + l.height) / 2 + padding < 0 ||
          y - (l.width + l.height) / 2 - padding > h
        )
          continue;
        const tint =
          mode === "finish"
            ? `rgb(${f.code === 3 ? 255 : 0},${f.code === 2 ? 255 : 0},${f.code === 1 ? 255 : 0})`
            : fill;
        const id = `tint-${l.id}-${col + 2}-${row + 2}`;
        let shape =
          ["image", "svg"].includes(l.kind) && mode !== "color"
            ? `<defs><filter id="${id}" color-interpolation-filters="sRGB"><feFlood flood-color="${tint}"/><feComposite in2="SourceAlpha" operator="in"/></filter></defs><g filter="url(#${id})">${patternShape(l, tint, mode)}</g>`
            : preview
              ? textilePreviewShape(l, tint, false)
              : patternShape(l, tint, mode);
        if (mode === "normal") {
          if (!PatternChannelEnabled(l, "Normal") || !l.SurfaceChannels?.Normal?.Source) continue;
          const Contour = `normal-${id}`;
          const Strength = PatternChannelStrength(l, "Normal"), Angle = l.rotation * sx * sy * Math.PI / 180;
          const PlanarGain = Math.min(1, Strength), VerticalGain = Strength > 1 ? 1 / Strength : 1;
          const Cosine = Math.cos(Angle), Sine = Math.sin(Angle), FlipX = sx * (l.flipX ? -1 : 1), FlipY = sy * (l.flipY ? -1 : 1);
          const RedX = Cosine * FlipX * PlanarGain, RedY = Sine * FlipY * PlanarGain, GreenX = -Sine * FlipX * PlanarGain, GreenY = Cosine * FlipY * PlanarGain;
          const Sized = l.paint && !["image", "svg"].includes(l.kind), MapX = Sized ? -l.width / 2 : 0, MapY = Sized ? -l.height / 2 : 0, MapWidth = Sized ? l.width : 100, MapHeight = Sized ? l.height : 100;
          shape = `<defs><filter id="${Contour}-strength" color-interpolation-filters="sRGB"><feColorMatrix type="matrix" values="${RedX} ${RedY} 0 0 ${.5 - .5 * (RedX + RedY)} ${GreenX} ${GreenY} 0 0 ${.5 - .5 * (GreenX + GreenY)} 0 0 ${VerticalGain} 0 ${.5 - .5 * VerticalGain} 0 0 0 1 0"/></filter><mask id="${Contour}" mask-type="alpha" maskUnits="userSpaceOnUse" x="${Sized ? -l.width : -100}" y="${Sized ? -l.height : -100}" width="${Sized ? l.width * 2 : 300}" height="${Sized ? l.height * 2 : 300}">${patternShape(l, "white", "normal")}</mask></defs><image href="${l.SurfaceChannels.Normal.Source}" x="${MapX}" y="${MapY}" width="${MapWidth}" height="${MapHeight}" preserveAspectRatio="none" filter="url(#${Contour}-strength)" mask="url(#${Contour})"/>`;
        }
        body += `<g opacity="${PatternChannelEnabled(l, "Opacity") ? l.opacity * PatternChannelStrength(l, "Opacity") : 1}" transform="${patternLayerTransform(l, { x, y, sx, sy })}">${shape}</g>`;
      }
  }
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${w * (d.designAspect || 1)}" height="${h}" viewBox="0 0 ${w} ${h}" preserveAspectRatio="none">${body}</svg>`;
}

// Seeded layout generation is separate from sampling: exported SVG remains a
// stable seamless supertile and is editable like any hand-built document.
export function generatePatternLayout({
  seed = 17,
  style = "geometric",
  count = 12,
} = {}) {
  let state = (Number(seed) || 17) >>> 0;
  const random = () => {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
    return state / 4294967296;
  };
  const n = Math.max(2, Math.min(24, Math.round(Number(count) || 12))),
    side = Math.ceil(Math.sqrt(n));
  const doc = patternStarter("Blank");
  doc.name = (style === "floral" ? "Garden" : "Geometric") + " study " + seed;
  doc.background = style === "floral" ? "#fff4df" : "#253441";
  doc.layers = [];
  const palette =
    style === "floral"
      ? ["#ef5a64", "#f39384", "#dc454f", "#e6ad86"]
      : ["#d5b163", "#da735d", "#7fa4a0", "#ece2c7"];
  for (let i = 0; i < n; i++) {
    const x = (((i % side) + 0.5) * 512) / side + (random() - 0.5) * 38,
      y = ((Math.floor(i / side) + 0.5) * 512) / side + (random() - 0.5) * 38,
      w = ((0.55 + random() * 0.6) * 512) / side;
    const color = palette[Math.floor(random() * palette.length)];
    doc.layers.push(
      patternLayer(
        style === "floral"
          ? "flower"
          : ["diamond", "triangle", "rect"][Math.floor(random() * 3)],
        {
          x,
          y,
          width: w,
          height: w * (0.7 + random() * 0.5),
          rotation:
            style === "floral" ? random() * 360 : Math.floor(random() * 4) * 90,
          color,
          finish: style === "floral" ? "cotton" : "wool",
        },
      ),
    );
    if (style === "floral")
      doc.layers.push(
        patternLayer("ellipse", {
          x,
          y,
          width: w * 0.14,
          height: w * 0.14,
          color: "#93754c",
          finish: "cotton",
        }),
      );
  }
  return validatePattern(doc);
}
