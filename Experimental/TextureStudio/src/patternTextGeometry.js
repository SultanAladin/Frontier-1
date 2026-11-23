import { normalizePatternText } from "./patternDocument.js";

// The editor stores editable text AND its vector outline. All SVG, raster and
// exported material renderers use the outline, never a remote/system font.
export function outlinePatternText(font, input) {
  const text = normalizePatternText(input),
    size = text.fontSize;
  const lines = text.value.replace(/\r\n?/g, "\n").split("\n");
  const scale = size / font.unitsPerEm;
  const advance = (glyphs) =>
    glyphs.reduce(
      (x, glyph, i) =>
        x +
        (glyph.advanceWidth || 0) * scale +
        (i < glyphs.length - 1
          ? text.letterSpacing +
            font.getKerningValue(glyph, glyphs[i + 1]) * scale
          : 0),
      0,
    );
  const glyphLines = lines.map((line) =>
    font.stringToGlyphs(line.replace(/\t/g, "    ")),
  );
  const widths = glyphLines.map(advance),
    lineWidth = Math.max(1, ...widths);
  const ascender = font.ascender * scale,
    lineStep = size * text.lineHeight;
  const commands = [];
  glyphLines.forEach((glyphs, line) => {
    let x =
      text.align === "center"
        ? (lineWidth - widths[line]) / 2
        : text.align === "right"
          ? lineWidth - widths[line]
          : 0;
    glyphs.forEach((glyph, i) => {
      commands.push(
        ...glyph.getPath(x, ascender + line * lineStep, size).commands,
      );
      x +=
        (glyph.advanceWidth || 0) * scale +
        text.letterSpacing +
        (i < glyphs.length - 1
          ? font.getKerningValue(glyph, glyphs[i + 1]) * scale
          : 0);
    });
  });
  if (commands.length > 12000)
    throw new Error("Text is too detailed. Split it into shorter text layers.");
  // Include the line box, so empty/space-only lines remain editable and aligned.
  const xs = [0, lineWidth],
    ys = [0, Math.max(size, (lines.length - 1) * lineStep + size)];
  for (const c of commands)
    for (const [x, y] of [
      [c.x, c.y],
      [c.x1, c.y1],
      [c.x2, c.y2],
    ])
      if (Number.isFinite(x) && Number.isFinite(y)) {
        xs.push(x);
        ys.push(y);
      }
  const minX = Math.min(...xs),
    minY = Math.min(...ys),
    maxX = Math.max(...xs),
    maxY = Math.max(...ys);
  const width = Math.max(1, Math.min(1024, maxX - minX)),
    height = Math.max(1, Math.min(1024, maxY - minY));
  const cx = (minX + maxX) / 2,
    cy = (minY + maxY) / 2;
  const xy = (x, y) =>
    `${Number((50 + ((x - cx) * 100) / width).toFixed(4))} ${Number((50 + ((y - cy) * 100) / height).toFixed(4))}`;
  const path = commands
    .map((c) =>
      c.type === "Z"
        ? "Z"
        : c.type === "M" || c.type === "L"
          ? `${c.type}${xy(c.x, c.y)}`
          : c.type === "Q"
            ? `Q${xy(c.x1, c.y1)} ${xy(c.x, c.y)}`
            : `C${xy(c.x1, c.y1)} ${xy(c.x2, c.y2)} ${xy(c.x, c.y)}`,
    )
    .join(" ");
  if (path.length > 100000)
    throw new Error(
      "Text outline exceeds 100,000 characters. Split the text into more layers.",
    );
  const unsupported = [
    ...new Set(
      [...text.value].filter((c) => !/\s/u.test(c) && !font.charToGlyphIndex(c)),
    ),
  ];
  return { text, path, width, height, unsupported };
}

export function updatePatternTextLayer(layer, font, patch, oldFont = font) {
  const oldLayout = outlinePatternText(oldFont, layer.text);
  const next = outlinePatternText(font, { ...layer.text, ...patch });
  // Resizing a text object remains meaningful when its contents are changed.
  return {
    ...layer,
    text: next.text,
    path: next.path,
    width: Math.min(
      1024,
      Math.max(1, (next.width * layer.width) / oldLayout.width),
    ),
    height: Math.min(
      1024,
      Math.max(1, (next.height * layer.height) / oldLayout.height),
    ),
  };
}
