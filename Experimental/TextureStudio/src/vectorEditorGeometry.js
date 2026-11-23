import paper from "paper";
import {
  patternPrimitiveNodes,
  patternPathContours,
  patternNodesPath,
} from "./patternGeometry.js";
import {
  layerWorldPoint,
  pathNodesBounds,
  clamp,
} from "./shapeEditorGeometry.js";

export function layerWorldContours(layer) {
  const contours = layer.nodes
    ? [{ nodes: layer.nodes, closed: !!layer.closed }]
    : ["path", "text"].includes(layer.kind)
      ? patternPathContours(layer.path)
      : [patternPrimitiveNodes(layer)].filter(Boolean);
  return (contours || []).map(({ nodes, closed }) => ({
    closed,
    nodes: nodes.map((n) => ({
      ...layerWorldPoint(layer, n),
      ...(n.in ? { in: layerWorldPoint(layer, n.in) } : {}),
      ...(n.out ? { out: layerWorldPoint(layer, n.out) } : {}),
    })),
  }));
}

// True Bézier booleans, not polygon/raster approximations. Embedded sources must
// be outlined explicitly; open strokes are never silently treated as fills.
export function booleanPatternLayers(layers, operation = "union") {
  const methods = {
    union: "unite",
    difference: "subtract",
    intersection: "intersect",
    exclude: "exclude",
  };
  if (!methods[operation] || layers.length < 2 || layers.length > 16)
    throw new Error("Select 2–16 filled vector shapes for a path operation.");
  const contours = layers.map((layer) => {
    const data = layerWorldContours(layer);
    if (
      layer.locked ||
      !layer.visible ||
      ["svg", "image"].includes(layer.kind) ||
      (layer.paint
        ? layer.fillEnabled === false
        : layer.kind === "path" && layer.strokeWidth) ||
      !data.length ||
      data.some((c) => !c.closed)
    )
      throw new Error(
        "Path operations require unlocked, closed, filled vectors. Outline text or use closed shapes; open strokes and embedded SVGs are not expanded automatically.",
      );
    return data;
  });
  if (contours.flat().reduce((n, c) => n + c.nodes.length, 0) > 4000)
    throw new Error(
      "This selection is too detailed to combine. Simplify it or combine fewer shapes.",
    );
  const scope = new paper.PaperScope();
  scope.setup(new scope.Size(512, 512));
  try {
    const items = contours.map((cs, i) => {
      // Keep the original double-precision Bézier coordinates. Serializing to
      // rounded SVG first destabilizes tangencies (rectangle minus inscribed circle).
      const Paths = cs.map(Contour => new scope.Path({
        segments: Contour.nodes.map(Point => new scope.Segment(
          new scope.Point(Point.x, Point.y),
          new scope.Point((Point.in?.x ?? Point.x) - Point.x, (Point.in?.y ?? Point.y) - Point.y),
          new scope.Point((Point.out?.x ?? Point.x) - Point.x, (Point.out?.y ?? Point.y) - Point.y),
        )),
        closed: true,
        insert: false,
      }));
      const item = Paths.length === 1 ? Paths[0] : new scope.CompoundPath({ children: Paths, insert: false });
      item.fillRule = layers[i].fillRule || "nonzero";
      return item;
    });
    let result = items[0];
    for (const item of items.slice(1))
      result = result[methods[operation]](item, { insert: false });
    result.reorient(true, true);
    const d = result.pathData;
    const cs = patternPathContours(d);
    if (!cs?.length || Math.abs(result.area) < 0.00001)
      throw new Error(
        "The operation has no filled result. The original shapes were left unchanged.",
      );
    if (d.length > 100000 || cs.reduce((n, c) => n + c.nodes.length, 0) > 4000)
      throw new Error(
        "The combined result is too detailed. The original shapes were left unchanged.",
      );
    const boxes = cs.map((c) => pathNodesBounds(c.nodes, c.closed));
    const minX = Math.min(...boxes.map((b) => b.minX)),
      maxX = Math.max(...boxes.map((b) => b.maxX)),
      minY = Math.min(...boxes.map((b) => b.minY)),
      maxY = Math.max(...boxes.map((b) => b.maxY));
    const x = clamp((minX + maxX) / 2, -512, 1024),
      y = clamp((minY + maxY) / 2, -512, 1024),
      width = clamp(maxX - minX, 1, 1024),
      height = clamp(maxY - minY, 1, 1024);
    const local = (p) => ({
      x: 50 + ((p.x - x) * 100) / width,
      y: 50 + ((p.y - y) * 100) / height,
    });
    const localContours = cs.map((c) => ({
      closed: c.closed,
      nodes: c.nodes.map((n) => ({
        ...local(n),
        ...(n.in ? { in: local(n.in) } : {}),
        ...(n.out ? { out: local(n.out) } : {}),
      })),
    }));
    const source = layers[0];
    return {
      ...source,
      kind: "path",
      name: {
        union: "Union",
        difference: "Difference",
        intersection: "Intersection",
        exclude: "Exclude",
      }[operation],
      x,
      y,
      width,
      height,
      rotation: 0,
      flipX: false,
      flipY: false,
      group: undefined,
      text: undefined,
      curveTension: undefined,
      fillRule: "nonzero",
      paint: true,
      fillEnabled: true,
      stroke: source.paint ? source.stroke : source.color,
      nodes:
        localContours.length === 1 && localContours[0].nodes.length <= 400
          ? localContours[0].nodes
          : undefined,
      closed: localContours.length === 1,
      path: localContours
        .map((c) => patternNodesPath(c.nodes, c.closed))
        .join(" "),
    };
  } finally {
    scope.project.remove();
  }
}
