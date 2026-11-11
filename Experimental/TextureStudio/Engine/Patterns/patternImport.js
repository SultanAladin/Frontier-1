// Static, self-contained SVG artwork. Source is validated before preview,
// document mutation AND export. Never inject executable or network-capable SVG.
export function sanitizePatternSVG(text) {
  text = String(text);
  if (text.length > 500000) throw new Error("SVG is limited to 500 KB.");
  if (/<!DOCTYPE|<!ENTITY/i.test(text))
    throw new Error("SVG document entities are not allowed.");
  const xml = new DOMParser().parseFromString(text, "image/svg+xml");
  if (
    xml.querySelector("parsererror") ||
    xml.documentElement.localName !== "svg"
  )
    throw new Error("Invalid SVG document.");
  const tags = new Set([
    "svg",
    "g",
    "path",
    "rect",
    "circle",
    "ellipse",
    "polygon",
    "polyline",
    "line",
    "defs",
    "linearGradient",
    "radialGradient",
    "stop",
    "clipPath",
    "mask",
    "pattern",
    "use",
    "symbol",
    "text",
    "tspan",
    "image",
    "title",
    "desc",
  ]);
  const presentation = new Set([
    "fill",
    "stroke",
    "stroke-width",
    "stroke-linecap",
    "stroke-linejoin",
    "stroke-miterlimit",
    "stroke-dasharray",
    "stroke-dashoffset",
    "fill-rule",
    "clip-rule",
    "opacity",
    "fill-opacity",
    "stroke-opacity",
    "vector-effect",
    "clip-path",
    "mask",
    "color",
    "font-family",
    "font-size",
    "font-weight",
    "font-style",
    "text-anchor",
    "dominant-baseline",
    "alignment-baseline",
    "letter-spacing",
    "word-spacing",
    "text-decoration",
    "visibility",
    "display",
    "stop-color",
    "stop-opacity",
  ]);
  const attrs = new Set([
    ...presentation,
    "xmlns",
    "xmlns:xlink",
    "viewBox",
    "width",
    "height",
    "x",
    "y",
    "x1",
    "x2",
    "y1",
    "y2",
    "cx",
    "cy",
    "r",
    "rx",
    "ry",
    "d",
    "points",
    "transform",
    "id",
    "offset",
    "gradientUnits",
    "gradientTransform",
    "spreadMethod",
    "fx",
    "fy",
    "href",
    "xlink:href",
    "preserveAspectRatio",
    "maskUnits",
    "maskContentUnits",
    "patternUnits",
    "patternContentUnits",
    "patternTransform",
    "dx",
    "dy",
    "rotate",
    "textLength",
    "lengthAdjust",
    "xml:space",
  ]);
  const root = xml.documentElement,
    nodes = [root, ...root.querySelectorAll("*")];
  if (nodes.length > 5000) throw new Error("SVG is limited to 5,000 elements.");
  const ids = new Map();
  const safeReference = (value) => /^#[\w.:-]+$/.test(value);
  for (const node of nodes) {
    if (
      !tags.has(node.localName) ||
      (node.namespaceURI && node.namespaceURI !== "http://www.w3.org/2000/svg")
    )
      throw new Error(
        `Unsupported SVG element: ${node.localName}. Use static SVG artwork, not scripts, foreign HTML, animation or stylesheet blocks.`,
      );
    // Safe presentation-only inline styles are expanded, not retained as CSS
    // that could target the surrounding editor or load external resources.
    if (node.hasAttribute("style")) {
      for (const declaration of node.getAttribute("style").split(";")) {
        if (!declaration.trim()) continue;
        const colon = declaration.indexOf(":"),
          name = declaration.slice(0, colon).trim().toLowerCase(),
          value = declaration.slice(colon + 1).trim();
        if (
          colon < 0 ||
          !presentation.has(name) ||
          /[\\{}]|!important|\/\*|@/i.test(value)
        )
          throw new Error(
            "Unsupported SVG inline style. Use presentation attributes for static fills, strokes and text.",
          );
        node.setAttribute(name, value);
      }
      node.removeAttribute("style");
    }
    for (const a of [...node.attributes]) {
      if (
        (a.name === "xmlns" && a.value === "http://www.w3.org/2000/svg") ||
        (a.name === "xmlns:xlink" && a.value === "http://www.w3.org/1999/xlink")
      )
        continue;
      if (!attrs.has(a.name))
        throw new Error(
          `Unsupported SVG attribute: ${a.name}. Event handlers, scripts and external styles are not allowed.`,
        );
      if (a.name === "xmlns" || a.name === "xmlns:xlink")
        throw new Error("Invalid SVG namespace.");
      if (a.name === "href" || a.name === "xlink:href") {
        if (
          node.localName === "image" &&
          /^data:image\/(png|jpeg|webp);base64,[a-z\d+/=\s]+$/i.test(a.value)
        )
          continue;
        if (
          !safeReference(a.value) ||
          !["use", "linearGradient", "radialGradient", "pattern"].includes(
            node.localName,
          )
        )
          throw new Error(
            "SVG references must point to local IDs. Embedded images must be PNG, JPEG or WebP data URLs.",
          );
        continue;
      }
      if (
        /(?:javascript:|https?:|data:|@import|expression\s*\(|[\\]|\/\*)/i.test(
          a.value,
        ) ||
        (/url\s*\(/i.test(a.value) && !/^url\(#[\w.:-]+\)$/.test(a.value))
      )
        throw new Error("External SVG references are not allowed.");
      if (a.name === "id") {
        if (!/^[a-z_][\w.:-]*$/i.test(a.value) || ids.has(a.value))
          throw new Error(
            "SVG IDs must be unique and use letters, numbers, dashes or underscores.",
          );
        ids.set(a.value, node);
      }
      if (/(?:\bNaN\b|\bInfinity\b)/i.test(a.value))
        throw new Error("SVG coordinates must be finite.");
    }
  }
  // Reject cycles and exponential <use> expansion before a browser decodes it.
  const visiting = new Set(),
    counts = new Map();
  const count = (node, depth = 0) => {
    if (depth > 128) throw new Error("SVG nesting is limited to 128 levels.");
    if (visiting.has(node))
      throw new Error("Circular SVG references are not allowed.");
    if (counts.has(node)) return counts.get(node);
    visiting.add(node);
    let size = 1;
    const references = [];
    for (const a of [...node.attributes]) {
      const ref =
        (a.name === "href" || a.name === "xlink:href") && safeReference(a.value)
          ? a.value.slice(1)
          : a.value.match(/^url\(#([\w.:-]+)\)$/)?.[1];
      if (ref) {
        const target = ids.get(ref);
        if (!target) throw new Error(`SVG references an unknown ID: ${ref}.`);
        references.push(target);
      }
    }
    for (const child of [...node.children, ...references]) {
      size += count(child, depth + 1);
      if (size > 20000)
        throw new Error(
          "SVG references expand to too many elements. Simplify repeated definitions.",
        );
    }
    visiting.delete(node);
    counts.set(node, size);
    return size;
  };
  count(root);
  if (!root.getAttribute("viewBox"))
    root.setAttribute(
      "viewBox",
      `0 0 ${parseFloat(root.getAttribute("width")) || 100} ${parseFloat(root.getAttribute("height")) || 100}`,
    );
  const box = root
    .getAttribute("viewBox")
    .trim()
    .split(/[\s,]+/)
    .map(Number);
  if (
    box.length !== 4 ||
    box.some((n) => !Number.isFinite(n)) ||
    box[2] <= 0 ||
    box[3] <= 0
  )
    throw new Error(
      "SVG viewBox must have four finite numbers and a positive width and height.",
    );
  root.setAttribute("xmlns", "http://www.w3.org/2000/svg");
  root.setAttribute("width", "100");
  root.setAttribute("height", "100");
  return new XMLSerializer().serializeToString(xml);
}
