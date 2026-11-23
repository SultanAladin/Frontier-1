import { ApplyArtworkMask } from "./ArtworkMask.js";
import { StampVectorGeometry } from "./VectorArtwork.js";
import { patternShape } from "./patternDocument.js";
import { PatternColourDefinitions } from "./PatternColourSources.js";
import { loadPatternTextFonts, patternTextFont } from "./patternTextFonts.js";
import { sanitizePatternSVG } from "./patternImport.js";
import { normalizeStampDocument } from "./stampDocument.js";
import { normalizeTextureSource } from "./textureSource.js";
const escape = (v) =>
  String(v).replace(
    /[&<>"']/g,
    (c) =>
      ({
        "&": "&amp;",
        "<": "&lt;",
        ">": "&gt;",
        '"': "&quot;",
        "'": "&apos;",
      })[c],
  );
const round = (v) => Number(v.toFixed(3));
const commandsToPath = (commands) =>
  commands
    .map((c) =>
      c.type === "Z"
        ? "Z"
        : c.type === "M" || c.type === "L"
          ? `${c.type}${round(c.x)} ${round(c.y)}`
          : c.type === "Q"
            ? `Q${round(c.x1)} ${round(c.y1)} ${round(c.x)} ${round(c.y)}`
            : `C${round(c.x1)} ${round(c.y1)} ${round(c.x2)} ${round(c.y2)} ${round(c.x)} ${round(c.y)}`,
    )
    .join(" ");

// Mixed-font runs share each line's baseline. These are actual bundled font
// outlines; size ratios, kerning, line breaks and colours survive SVG/PNG export.
export function outlineStampText(runs) {
  const lines = [[]];
  for (const run of runs) {
    const font = patternTextFont(run.fontFamily, run.fontWeight);
    if (!font) throw new Error("The bundled stamp fonts are still loading.");
    run.text.split("\n").forEach((text, index) => {
      if (index) lines.push([]);
      lines.at(-1).push({
        run,
        font,
        glyphs: font.stringToGlyphs(text.replace(/\t/g, "    ")),
      });
    });
  }
  const parts = [],
    unsupported = new Set();
  let y = 0,
    width = 1,
    commandCount = 0;
  for (const line of lines) {
    const ascender = Math.max(
      1,
      ...line.map(
        ({ font, run }) => (font.ascender / font.unitsPerEm) * run.fontSize,
      ),
    );
    const descender = Math.max(
      1,
      ...line.map(
        ({ font, run }) => (-font.descender / font.unitsPerEm) * run.fontSize,
      ),
    );
    let x = 0;
    for (const { run, font, glyphs } of line) {
      const commands = [];
      for (let i = 0; i < glyphs.length; i++) {
        const glyph = glyphs[i];
        commands.push(...glyph.getPath(x, y + ascender, run.fontSize).commands);
        x += ((glyph.advanceWidth || 0) / font.unitsPerEm) * run.fontSize;
        if (i < glyphs.length - 1)
          x +=
            (font.getKerningValue(glyph, glyphs[i + 1]) / font.unitsPerEm) *
            run.fontSize;
      }
      commandCount += commands.length;
      if (commandCount > 32000)
        throw new Error("Text outlines are too detailed. Use a shorter stamp.");
      for (const char of run.text)
        if (!/\s/u.test(char) && !font.charToGlyphIndex(char))
          unsupported.add(char);
      if (commands.length)
        parts.push({ path: commandsToPath(commands), color: run.color });
    }
    width = Math.max(width, x);
    y += Math.max(8, (ascender + descender) * 1.18);
  }
  return {
    parts,
    width,
    height: Math.max(1, y),
    unsupported: [...unsupported],
  };
}
function scopedSVG(value, prefix) {
  const xml = new DOMParser().parseFromString(
    sanitizePatternSVG(value),
    "image/svg+xml",
  );
  const root = xml.documentElement;
  // Each component owns its IDs, including copied gradients/clip paths.
  for (const node of [root, ...root.querySelectorAll("*")])
    for (const attribute of [...node.attributes]) {
      if (attribute.name === "id")
        node.setAttribute(attribute.name, `${prefix}-${attribute.value}`);
      else if (
        /^#[\w.:-]+$/.test(attribute.value) &&
        /^(?:xlink:)?href$/.test(attribute.name)
      )
        node.setAttribute(
          attribute.name,
          `#${prefix}-${attribute.value.slice(1)}`,
        );
      else if (/^url\(#[\w.:-]+\)$/.test(attribute.value))
        node.setAttribute(
          attribute.name,
          attribute.value.replace("url(#", `url(#${prefix}-`),
        );
    }
  root.setAttribute("width", "100");
  root.setAttribute("height", "100");
  return new XMLSerializer().serializeToString(root);
}
export async function composeStampSVG(input) {
  const doc = normalizeStampDocument(input);
  await loadPatternTextFonts();
  const components = [];
  const unsupported = new Set();
  for (const layer of [...doc.layers].reverse()) {
    if (!layer.visible || layer.opacity === 0) continue;
    if (layer.kind === "vector") {
      const Geometry = StampVectorGeometry(layer);
      components.push(`<g opacity="${layer.opacity}" transform="translate(${layer.x} ${layer.y}) rotate(${layer.rotation}) scale(${Geometry.flipX ? -1 : 1} ${Geometry.flipY ? -1 : 1})"><defs>${PatternColourDefinitions(Geometry)}</defs>${patternShape(Geometry, Geometry.color)}</g>`);
      continue;
    }
    let body,
      width = 100,
      height = 100;
    if (layer.kind === "text") {
      const outline = outlineStampText(layer.runs);
      width = outline.width;
      height = outline.height;
      for (const char of outline.unsupported) unsupported.add(char);
      body = outline.parts
        .map((p) => `<path d="${p.path}" fill="${p.color}"/>`)
        .join("");
    } else if (layer.kind === "svg")
      body = scopedSVG(layer.svg, `component-${layer.id}`);
    else
      body = `<image width="100" height="100" preserveAspectRatio="none" href="${escape(layer.image.dataUrl)}"/>`;
    components.push(
      `<g opacity="${layer.opacity}" transform="translate(${layer.x} ${layer.y}) rotate(${layer.rotation}) scale(${layer.width / width} ${layer.height / height}) translate(${-width / 2} ${-height / 2})">${ApplyArtworkMask(layer, body)}</g>`,
    );
  }
  return {
    svg: `<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="512" height="512" viewBox="0 0 512 512"><title>${escape(doc.name)}</title>${components.join("")}</svg>`,
    unsupported: [...unsupported],
  };
}
export async function renderStamp(input, edge = 256) {
  edge = Math.max(1, Math.min(256, Math.round(Number(edge) || 256)));
  const doc = normalizeStampDocument(input);
  const { svg, unsupported } = await composeStampSVG(doc);
  // Decode in an image sandbox, never as DOM in the application.
  const url = URL.createObjectURL(new Blob([svg], { type: "image/svg+xml" }));
  try {
    const image = new Image();
    image.src = url;
    await image.decode();
    const canvas = document.createElement("canvas");
    canvas.width = canvas.height = edge;
    canvas.getContext("2d").drawImage(image, 0, 0, edge, edge);
    let raster = normalizeTextureSource({
      name: doc.name,
      width: edge,
      height: edge,
      dataUrl: canvas.toDataURL("image/png"),
    });
    if (!raster && edge > 128) return renderStamp(doc, 128);
    if (!raster)
      throw new Error("The composed stamp exceeds the portable raster limit.");
    return { raster, svg, unsupported };
  } finally {
    URL.revokeObjectURL(url);
  }
}
export async function validatedStampDocument(doc) {
  const normalized = normalizeStampDocument(doc);
  return {
    ...normalized,
    layers: normalized.layers.map((l) =>
      l.kind === "svg" ? { ...l, svg: sanitizePatternSVG(l.svg) } : l,
    ),
  };
}
