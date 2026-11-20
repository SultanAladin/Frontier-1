/*============================================================================================================================================
                                                    FIELD RASTERISER
============================================================================================================================================*/
// 🧩 Dependency-free software rasteriser for the force field. Built from the same evaluator, colormaps and RK4 integrator the live viewports use,
// it runs in the browser and in Node (where it produces the reference PNG sheets) so the field visualisation can be verified without a GPU.

import * as M from '../Engine/FieldMath.js';
import { EvaluateAt, FlowAt, QuantityAt, SuggestMaximum, DEFAULT_OVERLAY } from '../Engine/FieldEvaluator.js';

const clamp255 = (v) => (v < 0 ? 0 : v > 255 ? 255 : v | 0);

//------------------------------------------------------------------------------------------------------------------------
//                                                  COLOR BUFFER
//------------------------------------------------------------------------------------------------------------------------

export function CreateSurface(width, height) {
  const data = new Uint8ClampedArray(width * height * 4);
  const surface = {
    Width: width,
    Height: height,
    Data: data,
    Fill(r, g, b, a = 255) {
      for (let i = 0; i < data.length; i += 4) {
        data[i] = r;
        data[i + 1] = g;
        data[i + 2] = b;
        data[i + 3] = a;
      }
    },
    Blend(x, y, r, g, b, alpha) {
      if (x < 0 || y < 0 || x >= width || y >= height) return;
      const i = (y * width + x) * 4;
      const a = alpha < 0 ? 0 : alpha > 1 ? 1 : alpha;
      data[i] += (r - data[i]) * a;
      data[i + 1] += (g - data[i + 1]) * a;
      data[i + 2] += (b - data[i + 2]) * a;
      data[i + 3] = 255;
    },
    Node() {
      return { width, height, data };
    },
  };
  return surface;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

// Liang–Barsky clip of a segment against the section plate, so overlays can never escape the plot rectangle.
export function ClipToPlot(x0, y0, x1, y1, box) {
  let t0 = 0;
  let t1 = 1;
  const dx = x1 - x0;
  const dy = y1 - y0;
  const tests = [
    [-dx, x0 - box.X],
    [dx, box.X + box.W - x0],
    [-dy, y0 - box.Y],
    [dy, box.Y + box.H - y0],
  ];
  for (const [p, q] of tests) {
    if (Math.abs(p) < 1e-9) {
      if (q < 0) return null;
      continue;
    }
    const r = q / p;
    // Liang–Barsky: a negative direction shrinks from the start of the span, a positive one from its end. (The bounds are
    // exclusive, so a segment lying exactly on an edge is kept rather than discarded.)
    if (p < 0) {
      if (r >= t1) return null;
      if (r > t0) t0 = r;
    } else {
      if (r <= t0) return null;
      if (r < t1) t1 = r;
    }
  }
  return [x0 + dx * t0, y0 + dy * t0, x0 + dx * t1, y0 + dy * t1];
}

// Every section overlay draws through this scissor, so no envelope ring, box edge or marker can escape the plate.
export function ClipSurface(surface, box, pad = 2) {
  const view = {
    Width: surface.Width,
    Height: surface.Height,
    Data: surface.Data,
    Node: () => surface.Node(),
    Fill: (...args) => surface.Fill(...args),
    Blend(x, y, r, g, b, a) {
      if (x < box.X - pad || y < box.Y - pad || x > box.X + box.W + pad || y > box.Y + box.H + pad) return;
      surface.Blend(x, y, r, g, b, a);
    },
  };
  return view;
}

// Segments are clipped before rasterising, then drawn as plain lines.
export function PlotLine(surface, box, x0, y0, x1, y1, colour, alpha = 1, thickness = 1) {
  const clipped = ClipToPlot(x0, y0, x1, y1, box);
  if (!clipped) return;
  Line(surface, clipped[0], clipped[1], clipped[2], clipped[3], colour, alpha, thickness);
}

export function Line(surface, x0, y0, x1, y1, [r, g, b], alpha = 1, thickness = 1) {
  const dx = x1 - x0;
  const dy = y1 - y0;
  const steps = Math.max(1, Math.ceil(Math.max(Math.abs(dx), Math.abs(dy))));
  const half = Math.max(0.5, thickness / 2);
  for (let i = 0; i <= steps; i++) {
    const t = i / steps;
    const x = x0 + dx * t;
    const y = y0 + dy * t;
    if (thickness <= 1.05) {
      surface.Blend(x | 0, y | 0, r, g, b, alpha);
      continue;
    }
    const r0 = Math.ceil(half);
    for (let oy = -r0; oy <= r0; oy++) {
      for (let ox = -r0; ox <= r0; ox++) {
        const d = Math.hypot(ox, oy);
        if (d <= half) surface.Blend((x | 0) + ox, (y | 0) + oy, r, g, b, alpha * (1 - Math.pow(d / half, 3) * 0.55));
      }
    }
  }
}

export function Polyline(surface, points, color, alpha = 1, thickness = 1) {
  for (let i = 1; i < points.length; i++) Line(surface, points[i - 1][0], points[i - 1][1], points[i][0], points[i][1], color, alpha, thickness);
}

export function Disc(surface, cx, cy, radius, [r, g, b], alpha = 1) {
  const r0 = Math.ceil(radius);
  for (let oy = -r0; oy <= r0; oy++) {
    for (let ox = -r0; ox <= r0; ox++) {
      const d = Math.hypot(ox, oy);
      if (d <= radius) surface.Blend(cx + ox, cy + oy, r, g, b, alpha * (1 - Math.pow(d / Math.max(0.001, radius), 2) * 0.35));
    }
  }
}

export function Ring(surface, cx, cy, radius, color, alpha = 1, thickness = 1, dash = 0) {
  const steps = Math.max(18, Math.ceil(radius * 4));
  for (let i = 0; i <= steps; i++) {
    if (dash > 0 && Math.floor(i / 3) % 2 === 1) continue;
    const a = (i / steps) * Math.PI * 2;
    const x = cx + Math.cos(a) * radius;
    const y = cy + Math.sin(a) * radius;
    if (thickness <= 1.05) surface.Blend(x | 0, y | 0, color[0], color[1], color[2], alpha);
    else {
      const r0 = Math.ceil(thickness / 2);
      for (let oy = -r0; oy <= r0; oy++) for (let ox = -r0; ox <= r0; ox++) if (Math.hypot(ox, oy) <= thickness / 2) surface.Blend((x | 0) + ox, (y | 0) + oy, color[0], color[1], color[2], alpha);
    }
  }
}

export function Arrow(surface, x0, y0, x1, y1, color, alpha = 1, thickness = 1, head = 4) {
  Line(surface, x0, y0, x1, y1, color, alpha, thickness);
  const angle = Math.atan2(y1 - y0, x1 - x0);
  const wing = Math.max(2, head);
  for (const sign of [-1, 1]) {
    const a = angle + Math.PI + sign * 0.42;
    Line(surface, x1, y1, x1 + Math.cos(a) * wing, y1 + Math.sin(a) * wing, color, alpha, thickness);
  }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PROJECTION (ORTHOGRAPHIC)
//------------------------------------------------------------------------------------------------------------------------

const AXIS_FRAMES = {
  XY: { Right: [1, 0, 0], Up: [0, 1, 0], Edge: [0, 0, 1], U: 0, V: 1, W: 2, LabelU: 'X [m]', LabelV: 'Y [m]', LabelW: 'Z [m]' },
  XZ: { Right: [1, 0, 0], Up: [0, 0, 1], Edge: [0, 1, 0], U: 0, V: 2, W: 1, LabelU: 'X [m]', LabelV: 'Z [m]', LabelW: 'Y [m]' },
  YZ: { Right: [0, 1, 0], Up: [0, 0, 1], Edge: [1, 0, 0], U: 1, V: 2, W: 0, LabelU: 'Y [m]', LabelV: 'Z [m]', LabelW: 'X [m]' },
};

export function SectionFrames() {
  return AXIS_FRAMES;
}

// A rectangle mapping the plane's two axes into pixel space, preserving aspect ratio.
export function SectionProjection(surface, axis, domain, padding = 34) {
  const frame = AXIS_FRAMES[axis] || AXIS_FRAMES.XY;
  const spanU = domain.Max[frame.U] - domain.Min[frame.U];
  const spanV = domain.Max[frame.V] - domain.Min[frame.V];
  const usableW = surface.Width - padding * 2;
  const usableH = surface.Height - padding * 2 - 16;
  const scale = Math.min(usableW / Math.max(1e-6, spanU), usableH / Math.max(1e-6, spanV));
  const originX = padding + (usableW - spanU * scale) / 2;
  const originY = padding + (usableH - spanV * scale) / 2;
  return {
    Frame: frame,
    Scale: scale,
    Plot: { X: originX, Y: originY, W: spanU * scale, H: spanV * scale },
    ToPixels(point) {
      const u = point[frame.U];
      const v = point[frame.V];
      return [originX + (u - domain.Min[frame.U]) * scale, originY + (domain.Max[frame.V] - v) * scale];
    },
    ToWorld(px, py) {
      const point = [0, 0, 0];
      point[frame.U] = domain.Min[frame.U] + (px - originX) / scale;
      point[frame.V] = domain.Max[frame.V] - (py - originY) / scale;
      point[frame.W] = 0;
      return point;
    },
    PlanePoint(u, v, offset) {
      const point = [0, 0, 0];
      point[frame.U] = u;
      point[frame.V] = v;
      point[frame.W] = offset;
      return point;
    },
  };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  HEATMAP FIELD
//------------------------------------------------------------------------------------------------------------------------

// Rasterises one slice of the field into the surface. Rows are sampled once and reused by the isoline pass.
export function SectionHeatmap(surface, projection, runtime, time, options = {}) {
  const quantity = options.Quantity || 'Speed';
  const colormap = options.Colormap || 'Engineering';
  const maximum = Math.max(1e-6, options.Maximum || 1);
  const offset = options.Offset || 0;
  const opacity = options.Opacity ?? 0.8;
  const samples = options.Samples || 100;
  const { Plot, Frame } = projection;
  const wide = Math.max(2, Math.round((Plot.W / Math.max(1, Plot.H)) * samples));
  const rows = samples;
  const cols = wide;
  const values = new Float32Array(rows * cols);
  const domain = options.Domain;

  for (let r = 0; r < rows; r++) {
    const v = domain.Max[Frame.V] - ((r + 0.5) / rows) * (domain.Max[Frame.V] - domain.Min[Frame.V]);
    for (let c = 0; c < cols; c++) {
      const u = domain.Min[Frame.U] + ((c + 0.5) / cols) * (domain.Max[Frame.U] - domain.Min[Frame.U]);
      const point = [0, 0, 0];
      point[Frame.U] = u;
      point[Frame.V] = v;
      point[Frame.W] = offset;
      values[r * cols + c] = quantity === 'Speed' ? M.Length(FlowAt(runtime, point, time, { Drift: 0.4 })) : QuantityAt(runtime, point, time, quantity);
    }
  }

  // Colour is calibrated on the slice's own distribution: the 92nd percentile maps to the hot end of the ramp and the
  // median to the cool end, so the authored flow reads as a gradient instead of a saturated plate.
  const sorted = Float32Array.from(values).sort();
  const percentile = (q) => sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * q))] || 0;
  const hot = Math.max(0.4, percentile(0.92));
  const cold = Math.max(0, Math.min(percentile(0.5), hot * 0.55));
  const scale = Math.max(1e-6, hot - cold + hot * 0.16);
  const bias = cold / Math.max(1e-6, hot + hot * 0.16);
  const isDiverging = quantity === 'Divergence';
  for (let y = 0; y < surface.Height; y++) {
    const t = (y - Plot.Y) / Math.max(1e-6, Plot.H);
    const r = Math.min(rows - 1, Math.max(0, Math.floor(t * rows)));
    for (let x = 0; x < surface.Width; x++) {
      const s = (x - Plot.X) / Math.max(1e-6, Plot.W);
      if (s < 0 || s > 1 || t < 0 || t > 1) continue;
      const c = Math.min(cols - 1, Math.max(0, Math.floor(s * cols)));
      const raw = values[r * cols + c];
      const normalised = Math.min(1, Math.max(0, Math.abs(raw) / scale - bias * 0.55));
      const shade = isDiverging ? 0.5 + Math.sign(raw) * 0.5 * Math.sqrt(normalised) : Math.pow(normalised, 0.62);
      const color = M.SampleColormap(colormap, shade);
      // Alpha is driven by the same normalised value so quiet regions stay dark and strong regions read hot.
      const alpha = opacity * (0.3 + 0.7 * Math.pow(normalised, 0.45));
      surface.Blend(x, y, color[0], color[1], color[2], alpha);
    }
  }

  // SpeedScale is the value that lands at the hot end of the ramp, so the legend and the arrows can share it.
  return { Values: values, Rows: rows, Cols: cols, SpeedScale: scale + bias * 0.55 * scale, ContinuousScale: maximum };
}

// Iso-contour overlay: draws marching-squares segments of the same scalar field.
export function SectionIsolines(surface, projection, field, options = {}) {
  const { Plot, Scale } = projection;
  const { Values, Rows, Cols } = field;
  const levels = options.Levels || 5;
  const maximum = Math.max(1e-6, options.Maximum || 1);
  const domain = options.Domain;
  const { Frame } = projection;
  for (let i = 1; i <= levels; i++) {
    const iso = (maximum * i) / (levels + 1);
    const segments = M.MarchingSquares(Values, Rows, Cols, iso);
    const alpha = 0.16 + 0.16 * (1 - i / (levels + 1));
    for (const [a, b] of segments) {
      const toPixel = ([gx, gy]) => {
        const u = domain.Min[Frame.U] + (gx / Cols) * (domain.Max[Frame.U] - domain.Min[Frame.U]);
        const v = domain.Max[Frame.V] - (gy / Rows) * (domain.Max[Frame.V] - domain.Min[Frame.V]);
        return [Plot.X + (u - domain.Min[Frame.U]) * Scale, Plot.Y + (domain.Max[Frame.V] - v) * Scale];
      };
      const p0 = toPixel(a);
      const p1 = toPixel(b);
      Line(surface, p0[0], p0[1], p1[0], p1[1], [235, 240, 248], alpha, 1);
    }
  }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  ARROW GRID
//------------------------------------------------------------------------------------------------------------------------

export function SectionArrows(surface, projection, runtime, time, options = {}) {
  const { Plot, Frame } = projection;
  const domain = options.Domain;
  const density = Math.max(1, options.Density || 4);
  const scale = options.Scale ?? 0.9;
  const offset = options.Offset || 0;
  const maximum = Math.max(1e-6, options.Maximum || 1);
  const stride = 1 / density;
  for (let fy = stride / 2; fy < 1; fy += stride) {
    for (let fx = stride / 2; fx < 1; fx += stride) {
      const u = domain.Min[Frame.U] + fx * (domain.Max[Frame.U] - domain.Min[Frame.U]);
      const v = domain.Min[Frame.V] + fy * (domain.Max[Frame.V] - domain.Min[Frame.V]);
      const point = [0, 0, 0];
      point[Frame.U] = u;
      point[Frame.V] = v;
      point[Frame.W] = offset;
      // The in-plane component of the drawn flow: force laws have no medium velocity, so sampling Velocity directly
      // would leave their regions blank.
      const flow = FlowAt(runtime, point, time, { Drift: 0.4 });
      const vec = [flow[Frame.U], flow[Frame.V]];
      const magnitude = Math.hypot(vec[0], vec[1]);
      if (magnitude < 1e-5) continue;
      const normalised = Math.min(1, magnitude / Math.max(1e-6, maximum));
      const length = (Plot.W / (density * 2.2)) * (0.32 + 0.86 * Math.sqrt(normalised)) * scale;
      const angle = Math.atan2(-vec[1], vec[0]);
      const [px, py] = projection.ToPixels(point);
      const dx = Math.cos(angle) * length;
      const dy = Math.sin(angle) * length;
      // Arrows never sink into the plate: the ramp is floored and lifted toward white so slow regions stay readable.
      const base = M.SampleColormap(options.Colormap || 'Engineering', 0.22 + 0.78 * Math.pow(normalised, 0.75));
      const color = [base[0] * 0.8 + 52, base[1] * 0.8 + 52, base[2] * 0.8 + 52];
      Arrow(surface, px - dx / 2, py - dy / 2, px + dx / 2, py + dy / 2, color, 0.95, Math.max(1.2, Plot.W / 300), Math.max(3.5, length * 0.38));
    }
  }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  STREAMLINES
//------------------------------------------------------------------------------------------------------------------------

function seedLattice(domain, frame, count, offset, seed) {
  const seeds = [];
  const golden = 2.39996323;
  const spanU = domain.Max[frame.U] - domain.Min[frame.U];
  const spanV = domain.Max[frame.V] - domain.Min[frame.V];
  for (let i = 0; i < count; i++) {
    const r = Math.sqrt((i + 0.5) / count);
    const a = i * golden + seed;
    const point = [0, 0, 0];
    point[frame.U] = domain.Min[frame.U] + spanU * (0.5 + 0.46 * r * Math.cos(a));
    point[frame.V] = domain.Min[frame.V] + spanV * (0.5 + 0.46 * r * Math.sin(a));
    point[frame.W] = offset;
    seeds.push(point);
  }
  return seeds;
}

export function SectionStreamlines(surface, projection, runtime, time, options = {}) {
  const { Frame, Plot } = projection;
  const domain = options.Domain;
  const country = options.Count || 90;
  const steps = options.Steps || 70;
  const stepSize = options.StepSize || 0.35;
  const offset = options.Offset || 0;
  const seed = options.Seed || 0.7;
  const width = surface.Width;
  const height = surface.Height;

  // Evaluate a slice-projected velocity, refusing to leave the plane so streamlines never tunnel out of the section.
  const onSliceVelocity = (p) => {
    const flow = FlowAt(runtime, p, time, { Drift: 0.4 });
    const v = [0, 0, 0];
    v[Frame.U] = flow[Frame.U];
    v[Frame.V] = flow[Frame.V];
    v[Frame.W] = 0;
    return v;
  };

  for (const origin of seedLattice(domain, Frame, country, offset, seed)) {
    const streamline = M.IntegrateFlowLine(onSliceVelocity, origin, {
      Steps: steps,
      StepSize: stepSize * 1.2,
      Bounds: { Min: domain.Min, Max: domain.Max },
      Stop: (p) => {
        const [px, py] = projection.ToPixels(p);
        return px < 4 || py < 4 || px > width - 4 || py > height - 4;
      },
    });
    if (streamline.length < 3) continue;
    for (let i = 1; i < streamline.length; i++) {
      const speed = M.Length(FlowAt(runtime, streamline[i], time, { Drift: 0.4 }));
      const t = Math.min(1, speed / Math.max(1e-6, options.Maximum || 1));
      const color = options.Monochrome ? options.Monochrome : M.SampleColormap(options.Colormap || 'Engineering', Math.sqrt(t));
      const a = projection.ToPixels(streamline[i - 1]);
      const b = projection.ToPixels(streamline[i]);
      Line(surface, a[0], a[1], b[0], b[1], color, 0.15 + 0.75 * (i / streamline.length), options.Thickness || 1.2);
    }
    // A travelling bead makes the direction of travel legible on a still image.
    const beadIndex = Math.floor((streamline.length - 1) * (0.35 + 0.4 * Math.sin(seed + origin[Frame.U])));
    const bead = projection.ToPixels(streamline[Math.max(1, Math.min(streamline.length - 1, beadIndex))]);
    Disc(surface, bead[0], bead[1], Math.max(1.3, Plot.W / 360), [255, 255, 255], 0.85);
  }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SOURCE ANCHORS & INFLUENCE
//------------------------------------------------------------------------------------------------------------------------

export function SectionSources(rawSurface, projection, document, time, options = {}) {
  const { Frame, Scale, Plot } = projection;
  const surface = ClipSurface(rawSurface, Plot);
  const offset = options.Offset || 0;
  for (const source of document.Sources) {
    if (!source.Enabled || source.Muted) continue;
    const anchor = M.MotionAnchorAt(source, time);
    const distance = Math.abs(anchor[Frame.W] - offset);
    const [px, py] = projection.ToPixels(anchor);
    // An envelope is drawn only when this slice actually cuts it; a sphere seen edge-on is its widest circle, a column
    // or a box is only meaningful within its own half-extent on the normal axis.
    const crosses = source.Envelope === 'Infinite' || (source.Envelope === 'Sphere' ? distance < source.Radius : distance < (source.Height || 0.0001) / 2 || distance < (source.HalfExtents ? source.HalfExtents[2] : 0.0001));
    const colour = source.Id === options.Selected ? [255, 178, 56] : [232, 238, 248];
    if (options.ShowEnvelopes && crosses && source.Envelope !== 'Infinite') {
      const alpha = 0.2;
      if (source.Envelope === 'Sphere') {
        Ring(surface, px, py, source.Radius * Scale, colour, alpha, 1, 1);
      } else if (source.Envelope === 'Column') {
        const radius = source.Radius * Scale;
        Ring(surface, px, py, radius, colour, alpha, 1, 1);
        Ring(surface, px, py, radius * 0.34, colour, alpha * 0.55, 1, 1);
        // The column's lips, drawn only where they lie inside the plate.
        const half = (source.Height / 2) * Scale;
        PlotLine(surface, Plot, px - radius, py - half, px + radius, py - half, colour, alpha * 0.7, 1);
        PlotLine(surface, Plot, px - radius, py + half, px + radius, py + half, colour, alpha * 0.7, 1);
      } else if (source.Envelope === 'Box') {
        // The box's cross-section on this plane: project the eight corners, keep the hull rectangle.
        const frame = M.DirectionFrame(source.Direction);
        const e = source.HalfExtents;
        const corners = [];
        for (const sx of [-1, 1]) for (const sy of [-1, 1]) for (const sz of [-1, 1]) corners.push(M.Add(anchor, frame.ToWorld([sx * e[0], sy * e[1], sz * e[2]])));
        const projected = corners.map((c) => projection.ToPixels(c));
        const x0 = Math.min(...projected.map((p) => p[0]));
        const x1 = Math.max(...projected.map((p) => p[0]));
        const y0 = Math.min(...projected.map((p) => p[1]));
        const y1 = Math.max(...projected.map((p) => p[1]));
        // Only the corners that fall inside the plate are traced, so an oversized bed does not read as a frame.
        PlotLine(surface, Plot, x0, y0, x1, y0, colour, alpha * 0.7, 1);
        PlotLine(surface, Plot, x1, y0, x1, y1, colour, alpha * 0.7, 1);
        PlotLine(surface, Plot, x1, y1, x0, y1, colour, alpha * 0.7, 1);
        PlotLine(surface, Plot, x0, y1, x0, y0, colour, alpha * 0.7, 1);
      }
      if (source.Duration === 'Impulse') Ring(surface, px, py, M.ImpulseFrontRadius(source, time) * Scale, [255, 120, 100], 0.45, 1.4);
    }
    if (px >= Plot.X - 6 && px <= Plot.X + Plot.W + 6 && py >= Plot.Y - 6 && py <= Plot.Y + Plot.H + 6) {
      Disc(surface, px, py, 3.4, colour, crosses ? 1 : 0.45);
      if (source.Id === options.Selected) Ring(surface, px, py, 7.5, [255, 178, 56], 0.9, 1.4);
    }
  }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  AXES & GRID
//------------------------------------------------------------------------------------------------------------------------

export function SectionGrid(surface, projection, domain, options = {}) {
  const { Plot, Frame, Scale } = projection;
  const step = options.Step || 4;
  const color = options.Color || [96, 103, 118];
  for (let u = Math.ceil(domain.Min[Frame.U] / step) * step; u <= domain.Max[Frame.U]; u += step) {
    const x = Plot.X + (u - domain.Min[Frame.U]) * Scale;
    Line(surface, x, Plot.Y, x, Plot.Y + Plot.H, color, 0.5, 1);
  }
  for (let v = Math.ceil(domain.Min[Frame.V] / step) * step; v <= domain.Max[Frame.V]; v += step) {
    const y = Plot.Y + (domain.Max[Frame.V] - v) * Scale;
    Line(surface, Plot.X, y, Plot.X + Plot.W, y, color, 0.5, 1);
  }
  Line(surface, Plot.X, Plot.Y + Plot.H, Plot.X + Plot.W, Plot.Y + Plot.H, [120, 126, 138], 0.8, 1);
  Line(surface, Plot.X, Plot.Y, Plot.X, Plot.Y + Plot.H, [120, 126, 138], 0.8, 1);
}

export function SectionProbe(rawSurface, projection, point, options = {}) {
  const surface = ClipSurface(rawSurface, projection.Plot, 8);
  const [px, py] = projection.ToPixels(point);
  const color = options.Color || [255, 178, 56];
  Ring(surface, px, py, 6, color, 0.95, 1.2);
  Ring(surface, px, py, 11, color, 0.4, 1);
  for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) Line(surface, px + dx * 14, py + dy * 14, px + dx * 20, py + dy * 20, color, 0.75, 1);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  FULL SECTION COMPOSITE
//------------------------------------------------------------------------------------------------------------------------

export function RenderSectionSurface(document, runtime, time, axis = 'XY', options = {}) {
  const overlay = { ...DEFAULT_OVERLAY, ...document.Overlay, ...options.Overlay };
  const width = options.Width || 640;
  const height = options.Height || 420;
  const surface = CreateSurface(width, height);
  surface.Fill(11, 12, 15, 255);
  const projection = SectionProjection(surface, axis, document.Domain);
  const { Plot } = projection;
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      if (x < Plot.X || y < Plot.Y || x > Plot.X + Plot.W || y > Plot.Y + Plot.H) surface.Blend(x, y, 16, 17, 21, 1);
    }
  }
  const maximum = overlay.AutoRange ? SuggestMaximum(runtime, document.Domain, time, overlay.Quantity) : overlay.MaxValue;
  if (overlay.ShowHeatmap !== false) {
    const field = SectionHeatmap(surface, projection, runtime, time, {
      Quantity: overlay.Quantity,
      Colormap: overlay.Colormap,
      Maximum: maximum,
      Offset: overlay.SliceOffset,
      Opacity: overlay.HeatmapOpacity,
      Domain: document.Domain,
      Samples: Math.max(60, Math.round(height / 4)),
    });
    SectionIsolines(surface, projection, field, { Maximum: maximum, Domain: document.Domain, Levels: 4 });
  }
  if (overlay.ShowStreamlines !== false) {
    SectionStreamlines(surface, projection, runtime, time, {
      Domain: document.Domain,
      Colormap: overlay.Colormap,
      Count: overlay.StreamSeedCount,
      Steps: overlay.StreamSteps,
      StepSize: overlay.StreamStepSize,
      Offset: overlay.SliceOffset,
      Maximum: maximum,
      Thickness: Math.max(1, width / 420),
    });
  }
  if (overlay.ShowVectors !== false) {
    SectionArrows(surface, projection, runtime, time, {
      Domain: document.Domain,
      Colormap: overlay.Colormap,
      Density: overlay.ArrowDensity,
      Scale: overlay.ArrowScale,
      Offset: overlay.SliceOffset,
      Maximum: maximum,
    });
  }
  SectionSources(surface, projection, document, time, {
    Offset: overlay.SliceOffset,
    ShowEnvelopes: overlay.ShowEnvelopes,
    Selected: options.Selected,
  });
  if (overlay.ShowProbe !== false && options.Probe) SectionProbe(surface, projection, options.Probe);
  return { Surface: surface, Projection: projection, Maximum: maximum };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PPM (deposit-only, for Node proof sheets)
//------------------------------------------------------------------------------------------------------------------------

export function SurfaceToPpm(surface) {
  const header = Buffer.from(`P6\n${surface.Width} ${surface.Height}\n255\n`, 'ascii');
  const body = Buffer.alloc(surface.Width * surface.Height * 3);
  for (let i = 0, j = 0; i < surface.Data.length; i += 4, j += 3) {
    body[j] = clamp255(surface.Data[i]);
    body[j + 1] = clamp255(surface.Data[i + 1]);
    body[j + 2] = clamp255(surface.Data[i + 2]);
  }
  return Buffer.concat([header, body]);
}
