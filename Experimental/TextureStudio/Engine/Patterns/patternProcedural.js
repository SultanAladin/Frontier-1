/*============================================================================================================================================
                                                          PATTERNPROCEDURAL.JS
============================================================================================================================================*/
// 🧩 Composable modifier-stack primitives for pattern motifs: Array/Inset, Mirror and Align-to-edge.
// A "motif" is just an array of layer-shaped objects (whatever `layerFactory(kind, extra)` returns,
// e.g. patternDocument.js's `patternLayer`); every operation below is a pure function over that
// array, so pattern starters compose a few reusable operations over one base shape instead of each
// one hand-listing every instance's absolute coordinates. Downstream rendering/export never changes:
// the output is still a flat layer array, exactly what patternStarter() already produced by hand.

// 📝 Array/Inset: repeats the base layer `count` times, each step the same shape inset by a fixed
// amount (`sizeStep` px off width/height, not a ratio) so repeated rings read as nested "inner and
// outer" copies of one motif. `colors` cycles across the steps (e.g. [accent, ground] alternates a
// woven look); an optional `role` tag records which colour slot produced each step, so a later
// Align-to-edge can recolour just the accent rings per edge without touching the ground rings.
// `extras` cycles a list of partial overrides by index (e.g. alternating relief/finish in lockstep
// with, or independently of, `colors`); `extrasAt(index)` computes a partial override directly, for a
// continuous per-ring gradient (e.g. relief fading ring to ring) that a fixed cycle can't express.
export function arrayInset(layerFactory, kind, base, options = {}) {
  const { count = 1, sizeStep = 0, stepX = 0, stepY = 0, colors, roles, extras, extrasAt } = options;
  if (!Number.isFinite(count) || count < 1) throw new Error("arrayInset needs a positive count.");
  const startWidth = base.width ?? 160;
  const startHeight = base.height ?? 160;
  const layers = [];
  for (let index = 0; index < count; index++) {
    const width = Math.max(0, startWidth - sizeStep * index);
    const height = Math.max(0, startHeight - sizeStep * index);
    const color = colors && colors.length ? colors[index % colors.length] : base.color;
    const role = roles && roles.length ? roles[index % roles.length] : undefined;
    const extra = extrasAt ? extrasAt(index) : extras && extras.length ? extras[index % extras.length] : undefined;
    layers.push(
      layerFactory(kind, {
        ...base,
        x: (base.x ?? 256) + stepX * index,
        y: (base.y ?? 256) + stepY * index,
        width,
        height,
        color,
        ...(role ? { proceduralRole: role } : {}),
        ...(extra || {}),
      }),
    );
  }
  return layers;
}

// 📝 Mirror: reflects every layer in `motif` across a horizontal ("y") or vertical ("x") line at
// `through`, returning the original motif plus its mirrored copy -- e.g. building one row and
// mirroring it top/bottom instead of hand-authoring both rows.
export function mirrorMotif(motif, options = {}) {
  const { axis = "y", through = 256 } = options;
  if (axis !== "x" && axis !== "y") throw new Error("mirrorMotif axis must be \"x\" or \"y\".");
  const reflected = motif.map((layer) => ({
    ...layer,
    x: axis === "x" ? 2 * through - layer.x : layer.x,
    y: axis === "y" ? 2 * through - layer.y : layer.y,
    // 📝 A single-axis reflection always flips handedness, so any non-zero rotation negates.
    rotation: layer.rotation ? -layer.rotation : layer.rotation,
  }));
  return [...motif, ...reflected];
}

// 📝 Grid array: a 2D repeat of one base layer across `columns` × `rows`, generalizing arrayInset's
// single-axis step to two axes, with per-cell callbacks for every property that can legitimately vary
// cell to cell -- row offset (brick/herringbone courses), size (distance/gradient scaling, e.g. a
// sine-wave band), colour and rotation/kind (checkerboards, alternating motifs). Each callback
// receives (row, column) and may return `undefined` to keep the base layer's value.
export function gridArray(layerFactory, kind, base, options = {}) {
  const {
    columns = 1,
    rows = 1,
    stepX = 0,
    stepY = 0,
    rowOffsetX,
    sizeAt,
    colorAt,
    rotationAt,
    kindAt,
  } = options;
  if (!Number.isFinite(columns) || !Number.isFinite(rows) || columns < 1 || rows < 1)
    throw new Error("gridArray needs positive columns/rows.");
  const layers = [];
  for (let row = 0; row < rows; row++) {
    const offsetX = rowOffsetX ? rowOffsetX(row) : 0;
    for (let column = 0; column < columns; column++) {
      const size = sizeAt ? sizeAt(row, column) : undefined;
      layers.push(
        layerFactory(kindAt ? kindAt(row, column) ?? kind : kind, {
          ...base,
          x: (base.x ?? 0) + offsetX + stepX * column,
          y: (base.y ?? 0) + stepY * row,
          width: size ?? base.width,
          height: size ?? base.height,
          color: (colorAt && colorAt(row, column)) ?? base.color,
          rotation: (rotationAt && rotationAt(row, column)) ?? base.rotation,
        }),
      );
    }
  }
  return layers;
}

// 📝 Fibonacci square spiral: the classic construction -- each successive square (side = the next
// Fibonacci number) is attached to the growing bounding rectangle's edge, rotating through
// right/up/left/down -- computed from the recurrence instead of a hand-transcribed list of
// [x, y, side] literals. Returns `count` squares as [x, y, side] in local, un-scaled spiral units
// (square 0 occupies [0, 0, 1]); the caller projects/scales them into tile space.
export function fibonacciSquareSpiral(count) {
  if (!Number.isInteger(count) || count < 1) throw new Error("fibonacciSquareSpiral needs a positive integer count.");
  const fibonacci = [1, 1];
  while (fibonacci.length < count) fibonacci.push(fibonacci[fibonacci.length - 1] + fibonacci[fibonacci.length - 2]);
  const directions = ["right", "up", "left", "down"];
  const bounds = { xmin: 0, ymin: 0, xmax: 0, ymax: 0 };
  const squares = [];
  for (let index = 0; index < count; index++) {
    const side = fibonacci[index];
    let x, y;
    if (index === 0) {
      x = 0;
      y = 0;
      bounds.xmax = side;
      bounds.ymax = side;
    } else {
      switch (directions[(index - 1) % 4]) {
        case "right":
          x = bounds.xmax;
          y = bounds.ymin;
          bounds.xmax += side;
          break;
        case "up":
          x = bounds.xmin;
          y = bounds.ymin - side;
          bounds.ymin -= side;
          break;
        case "left":
          x = bounds.xmin - side;
          y = bounds.ymin;
          bounds.xmin -= side;
          break;
        default: // "down"
          x = bounds.xmin;
          y = bounds.ymax;
          bounds.ymax += side;
          break;
      }
    }
    squares.push([x, y, side]);
  }
  return squares;
}

// 📝 Scatter placement: a deterministic pseudo-random position for motif copy `index`, built from two
// coprime-ish strides modulo the tile size -- the reusable technique behind "scattered" motifs (e.g.
// a tossed-flower print) instead of inlined magic-number arithmetic at each call site.
export function scatterPlacement(index, options = {}) {
  const { strideX = 197, strideY = 113, offsetX = 0, offsetY = 0, width = 512, height = 512 } = options;
  return {
    x: (index * strideX + offsetX) % width,
    y: (index * strideY + offsetY) % height,
  };
}
// relative to the tile's edges/quarters (anchor.x/anchor.y are the translation applied to the
// motif's own local coordinates, which are authored as offsets from the anchor, not the tile
// centre) instead of a hand-picked absolute coordinate. `anchor.recolor` maps a `proceduralRole`
// (as tagged by arrayInset) to a replacement colour, so each edge can carry its own accent while
// sharing the same ring structure.
export function alignToEdges(motif, anchors) {
  if (!Array.isArray(anchors) || !anchors.length) throw new Error("alignToEdges needs at least one anchor.");
  const layers = [];
  for (const anchor of anchors) {
    for (const layer of motif) {
      const { proceduralRole, ...rest } = layer;
      const recolored = proceduralRole && anchor.recolor && anchor.recolor[proceduralRole];
      layers.push({
        ...rest,
        x: layer.x + (anchor.x ?? 0),
        y: layer.y + (anchor.y ?? 0),
        color: recolored || layer.color,
      });
    }
  }
  return layers;
}
