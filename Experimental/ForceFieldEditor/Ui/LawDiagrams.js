/*============================================================================================================================================
                                                      LAW DIAGRAMS
============================================================================================================================================*/
// 🧩 Per-generator explanatory diagrams for the inspector. Each diagram is drawn from the source's own parameters on a 2-D canvas in the
// generator's local frame (u = the axis, radians − straight up v), so the picture changes as the user edits the law rather than serving as
// static decoration. Deterministic: the same parameters always produce the same diagram.

import { El, FitCanvas } from './Kit.js';
import * as M from '../Engine/FieldMath.js';

const INK = [206, 214, 226];
const DIM = [96, 104, 118];
const HALT = [255, 178, 56];
const AHEAD = [90, 196, 255];

// Local-frame axes of a diagram: distance along the source direction, and lateral offset.
function axes(radius) {
  return {
    U: (value) => 26 + ((value + radius) / (radius * 2)) * 1, // placeholder scale, replaced per draw
  };
}

function frameFor(width, height, radius) {
  const pad = 16;
  const span = Math.max(1e-3, radius * 2);
  const scale = Math.min((width - pad * 2) / span, (height - pad * 2) / span);
  const cx = width / 2;
  const cy = height / 2;
  return {
    Scale: scale,
    ToPixel: (u, v) => [cx + u * scale, cy - v * scale],
    Radius: radius,
  };
}

const line = (g, a, b, color, alpha = 0.8, width = 1) => {
  g.strokeStyle = `rgba(${color[0]},${color[1]},${color[2]},${alpha})`;
  g.lineWidth = width;
  g.beginPath();
  g.moveTo(a[0], a[1]);
  g.lineTo(b[0], b[1]);
  g.stroke();
};

const arrow = (g, a, b, color, alpha = 0.9, width = 1.2, head = 4) => {
  line(g, a, b, color, alpha, width);
  const angle = Math.atan2(b[1] - a[1], b[0] - a[0]);
  for (const sign of [-1, 1]) {
    const c = angle + Math.PI + sign * 0.42;
    g.strokeStyle = `rgba(${color[0]},${color[1]},${color[2]},${alpha})`;
    g.lineWidth = width;
    g.beginPath();
    g.moveTo(b[0], b[1]);
    g.lineTo(b[0] + Math.cos(c) * head, b[1] + Math.sin(c) * head);
    g.stroke();
  }
};

const ring = (g, cx, cy, radius, color, alpha = 0.5, width = 1) => {
  g.strokeStyle = `rgba(${color[0]},${color[1]},${color[2]},${alpha})`;
  g.lineWidth = width;
  g.beginPath();
  g.arc(cx, cy, Math.max(0.5, radius), 0, Math.PI * 2);
  g.stroke();
};

const label = (g, text, x, y, color = DIM, align = 'left') => {
  g.fillStyle = `rgba(${color[0]},${color[1]},${color[2]},0.85)`;
  g.font = '9px ui-monospace, SFMono-Regular, Menlo, monospace';
  g.textAlign = align;
  g.fillText(text, x, y);
};

// A faint axis cross in the local frame.
function axesCross(g, frame, width, height) {
  const [cx, cy] = frame.ToPixel(0, 0);
  line(g, [14, cy], [width - 14, cy], DIM, 0.22);
  line(g, [cx, 10], [cx, height - 10], DIM, 0.22);
  label(g, 'direction', width - 16, cy - 5, DIM, 'right');
}

// One diagram per law. `S` is the source, `P` its sanitised parameters, `law` the catalog entry.
const DRAW = {
  LinearWind(g, S, P, frame, width, height) {
    const radius = S.Radius || 8;
    const half = (S.Height || 8) / 2;
    // The column boundary in the plane is a rectangle in the (radial, axial) frame.
    const corners = [frame.ToPixel(-radius, -half), frame.ToPixel(radius, half)];
    g.strokeStyle = `rgba(${DIM[0]},${DIM[1]},${DIM[2]},0.35)`;
    g.setLineDash([4, 4]);
    g.strokeRect(corners[0][0], corners[1][1], corners[1][0] - corners[0][0], corners[0][1] - corners[1][1]);
    g.setLineDash([]);
    const veer = M.Radians(P.Veer ?? 0);
    for (let i = 0; i < 9; i++) {
      const v = -half + (i / 8) * half * 2;
      const speed = 1 + (P.Shear ?? 0) * v;
      const [x0, y0] = frame.ToPixel(-radius * 0.86, v);
      const [x1, y1] = frame.ToPixel(-radius * 0.86 + radius * 0.9 * Math.max(0.25, speed), v);
      const length = Math.hypot(x1 - x0, y1 - y0);
      const angle = Math.atan2(y1 - y0, x1 - x0) + veer * 0.35 * Math.sign(v);
      arrow(g, [x0, y0], [x0 + Math.cos(angle) * length, y0 + Math.sin(angle) * length], INK, 0.42 + 0.5 * Math.min(1, Math.max(0, speed)));
    }
    label(g, `v₀ ${(P.Strength ?? 0).toFixed(1)} m/s`, 16, height - 8, HALT);
    label(g, `shear ${(P.Shear ?? 0).toFixed(2)} /m`, width - 16, height - 8, DIM, 'right');
  },
  DirectedStream(g, S, P, frame, width, height) {
    const ext = S.HalfExtents;
    const [x0, y0] = frame.ToPixel(-ext[2], ext[0]);
    const [x1, y1] = frame.ToPixel(ext[2], -ext[0]);
    g.strokeStyle = `rgba(${DIM[0]},${DIM[1]},${DIM[2]},0.45)`;
    g.setLineDash([4, 4]);
    g.beginPath();
    g.moveTo(frame.ToPixel(-ext[2], 0)[0], y0);
    g.lineTo(frame.ToPixel(ext[2], 0)[0], y0);
    g.moveTo(frame.ToPixel(-ext[2], 0)[0], y1);
    g.lineTo(frame.ToPixel(ext[2], 0)[0], y1);
    g.stroke();
    g.setLineDash([]);
    for (let i = 0; i <= 12; i++) {
      const u = -ext[2] + (i / 12) * ext[2] * 2;
      const axial = (u + ext[2]) / (ext[2] * 2);
      const throat = 1 + (P.Throat ?? 0) * (1 - Math.abs(u) / Math.max(0.001, ext[2]));
      const speed = Math.max(0.15, M.SmoothStep(0, 0.28, axial) * throat);
      const spread = ext[0] * (0.85 - 0.3 * (P.Spread ?? 0));
      for (const v of [-spread * 0.6, 0, spread * 0.6]) {
        const [px, py] = frame.ToPixel(u, v);
        arrow(g, [px, py], [px + speed * 9, py], AHEAD, 0.25 + 0.65 * Math.min(1, speed), 1.1);
      }
    }
    label(g, `corridor ${ext[2].toFixed(1)} m`, 16, height - 8, HALT);
    label(g, `throat +${Math.round((P.Throat ?? 0) * 100)}%`, width - 16, height - 8, DIM, 'right');
  },
  Buoyancy(g, S, P, frame, width, height) {
    const radius = S.Radius || 6;
    const half = (S.Height || 9) / 2;
    for (let i = 0; i < 7; i++) {
      const v = -half + (i / 6) * half * 2;
      const gain = (P.DensityRatio ?? 1.6) * (1 - (P.HeightFalloff ?? 0.35) * M.Saturate((v + half) / (half * 2)));
      const [px, py] = frame.ToPixel(-radius * 0.6 + (i % 3) * radius * 0.6, v);
      arrow(g, [px, py], [px, py - Math.max(4, gain * 5)], INK, 0.3 + 0.6 * Math.min(1, gain / 2));
    }
    const [bx, by] = frame.ToPixel(0, -half * 0.4);
    ring(g, bx, by, 9, HALT, 0.8, 1.4);
    label(g, `ρ_amb/ρ_body ${(P.DensityRatio ?? 0).toFixed(2)}`, 16, height - 8, HALT);
  },
  RadialBurst(g, S, P, frame, width, height) {
    const radius = S.Radius || 10;
    const wave = Math.min(radius * 0.92, (P.WaveSpeed ?? 9) * 0.8);
    ring(g, ...frame.ToPixel(0, 0), wave * frame.Scale, AHEAD, 0.75, 1.6);
    ring(g, ...frame.ToPixel(0, 0), Math.max(2, (P.StartRadius ?? 0.6) * frame.Scale), HALT, 0.6, 1.2);
    for (let i = 0; i < 10; i++) {
      const a = (i / 10) * Math.PI * 2;
      const inner = wave * 0.94;
      const outer = Math.min(radius, wave * 1.2);
      const [px, py] = frame.ToPixel(Math.cos(a) * inner, Math.sin(a) * inner);
      const [qx, qy] = frame.ToPixel(Math.cos(a) * outer, Math.sin(a) * outer);
      arrow(g, [px, py], [qx, qy], INK, 0.72);
    }
    label(g, `front ${wave.toFixed(1)} m`, 16, height - 8, HALT);
    label(g, `σ ${(P.ShellWidth ?? 0).toFixed(2)} m`, width - 16, height - 8, DIM, 'right');
  },
  Attractor(g, S, P, frame, width, height) {
    const R = S.Radius || 9;
    const core = P.CoreRadius ?? 1.6;
    const steps = 7;
    for (let i = 0; i < steps; i++) {
      const a = (i / steps) * Math.PI * 2;
      const [px, py] = frame.ToPixel(Math.cos(a) * R * 0.82, Math.sin(a) * R * 0.82);
      const tangential = Math.min(0.55, (P.Swirl ?? 0) / 14);
      const tx = -Math.sin(a) * tangential - Math.cos(a) * 0.45;
      const ty = Math.cos(a) * tangential - Math.sin(a) * 0.45;
      const length = Math.hypot(tx, ty);
      arrow(g, [px, py], [px + (tx / length) * 15, py + (ty / length) * 15], INK, 0.8);
    }
    ring(g, ...frame.ToPixel(0, 0), Math.max(2, core * frame.Scale), HALT, 0.75, 1.3);
    ring(g, ...frame.ToPixel(0, 0), R * frame.Scale, DIM, 0.3, 1);
    label(g, `swirl ${(P.Swirl ?? 0).toFixed(1)}`, 16, height - 8, HALT);
  },
  Repulsor(g, S, P, frame, width, height) {
    const R = S.Radius || 8;
    ring(g, ...frame.ToPixel(0, 0), R * frame.Scale * 0.96, DIM, 0.3, 1);
    ring(g, ...frame.ToPixel(0, 0), Math.max(2, (P.CoreRadius ?? 1.2) * frame.Scale), HALT, 0.8, 1.3);
    for (let i = 0; i < 12; i++) {
      const a = (i / 12) * Math.PI * 2;
      const [px, py] = frame.ToPixel(Math.cos(a) * R * 0.22, Math.sin(a) * R * 0.22);
      const [qx, qy] = frame.ToPixel(Math.cos(a) * R * 0.88, Math.sin(a) * R * 0.88);
      arrow(g, [px, py], [qx, qy], INK, 0.8);
    }
    label(g, `power ${(P.Sharpness ?? 0).toFixed(1)}`, 16, height - 8, HALT);
  },
  AxisAttractor(g, S, P, frame, width, height) {
    const L = P.AxisLength ?? 12;
    const a = frame.ToPixel(-L * 0.5, 0);
    const b = frame.ToPixel(L * 0.5, 0);
    line(g, a, b, HALT, 0.9, 2);
    for (let i = 0; i < 6; i++) {
      const u = -L * 0.42 + (i / 5) * L * 0.84;
      for (const sign of [-1, 1]) {
        const [px, py] = frame.ToPixel(u, sign * Math.min(frame.Radius * 0.85, L * 0.32));
        const [qx, qy] = frame.ToPixel(u, sign * Math.min(frame.Radius * 0.12, L * 0.05));
        arrow(g, [px, py], [qx, qy], INK, 0.7);
      }
    }
    label(g, `axis ${L.toFixed(1)} m`, 16, height - 8, HALT);
  },
  SplineForce(g, S, P, frame, width, height) {
    const L = P.PathLength ?? 11;
    const bow = (P.Bow ?? 0.32) * L;
    const points = [];
    for (let i = 0; i <= 30; i++) {
      const u = (i / 30) * L;
      const t = i / 30;
      const v = Math.sin(Math.PI * t) * bow * 0.6;
      const [px, py] = frame.ToPixel(L * 0.5 - u, -v * 0.35);
      points.push([px, py]);
    }
    g.strokeStyle = `rgba(${HALT[0]},${HALT[1]},${HALT[2]},0.9)`;
    g.lineWidth = 1.8;
    g.beginPath();
    points.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y)));
    g.stroke();
    for (let i = 2; i < 29; i += 4) {
      const [px, py] = points[i];
      const [nx, ny] = points[Math.min(points.length - 1, i + 2)];
      arrow(g, [px, py], [px + (nx - px), py + (ny - py)], AHEAD, 0.7);
      for (const sign of [-1, 1]) line(g, [px, py], [px, py + sign * 7], INK, 0.35);
    }
    label(g, `capture ${(P.TubeRadius ?? 0).toFixed(1)} m`, 16, height - 8, HALT);
  },
  BoundaryAdhesion(g, S, P, frame, width, height) {
    const R = S.Radius || 7;
    ring(g, ...frame.ToPixel(0, 0), R * frame.Scale, HALT, 0.85, 1.6);
    const target = (P.TargetDistance ?? 0.35) * frame.Scale;
    ring(g, ...frame.ToPixel(0, 0), Math.max(2, R * frame.Scale - target), INK, 0.35, 1);
    for (let i = 0; i < 9; i++) {
      const a = (i / 9) * Math.PI * 2;
      const [px, py] = frame.ToPixel(Math.cos(a) * R * 0.55, Math.sin(a) * R * 0.55);
      const [qx, qy] = frame.ToPixel(Math.cos(a) * R * 0.9, Math.sin(a) * R * 0.9);
      arrow(g, [px, py], [qx, qy], INK, 0.75);
    }
    label(g, `cling ${(P.TargetDistance ?? 0).toFixed(2)} m`, 16, height - 8, HALT);
  },
  Containment(g, S, P, frame, width, height) {
    const R = S.Radius || 9;
    const inner = R * (P.InnerRatio ?? 0.55);
    ring(g, ...frame.ToPixel(0, 0), R * frame.Scale, HALT, 0.85, 1.6);
    ring(g, ...frame.ToPixel(0, 0), inner * frame.Scale, INK, 0.3, 1);
    for (let i = 0; i < 10; i++) {
      const a = (i / 10) * Math.PI * 2;
      const [px, py] = frame.ToPixel(Math.cos(a) * R * 0.94, Math.sin(a) * R * 0.94);
      const [qx, qy] = frame.ToPixel(Math.cos(a) * inner * 1.04, Math.sin(a) * inner * 1.04);
      arrow(g, [px, py], [qx, qy], INK, 0.75);
    }
    label(g, `free r < ${inner.toFixed(1)} m`, 16, height - 8, HALT);
  },
  RotationalColumn(g, S, P, frame, width, height) {
    const R = S.Radius || 6;
    const core = Math.max(0.2, P.CoreRadius ?? 1.5);
    ring(g, ...frame.ToPixel(0, 0), R * frame.Scale, DIM, 0.3, 1);
    ring(g, ...frame.ToPixel(0, 0), core * frame.Scale, HALT, 0.7, 1.2);
    for (let i = 0; i < 28; i++) {
      const r = core * 0.25 + (i % 4) * (R * 0.19);
      const a = (i / 28) * Math.PI * 2 * 3;
      const [px, py] = frame.ToPixel(Math.cos(a) * r, Math.sin(a) * r);
      const tangential = Math.atan2(py - height / 2, px - width / 2) + Math.PI / 2;
      arrow(g, [px, py], [px + Math.cos(tangential) * 5.5, py + Math.sin(tangential) * 5.5], INK, 0.42 + (r / R) * 0.5, 1, 3);
    }
    for (let i = 0; i < 5; i++) {
      const a = (i / 5) * Math.PI * 2 + 0.4;
      const [px, py] = frame.ToPixel(Math.cos(a) * R * 0.55, Math.sin(a) * R * 0.55);
      arrow(g, [px, py], [px, py - (P.Updraft ?? 0) * 1.4], AHEAD, 0.65);
    }
    label(g, `core ${core.toFixed(1)} m`, 16, height - 8, HALT);
    label(g, `up ${(P.Updraft ?? 0).toFixed(1)} m/s`, width - 16, height - 8, AHEAD, 'right');
  },
  Torque(g, S, P, frame, width, height) {
    const R = S.Radius || 6;
    const tilt = M.Radians(P.AxisTilt ?? 0);
    const [ax, ay] = frame.ToPixel(Math.sin(tilt) * R * 0.8, Math.cos(tilt) * R * 0.8);
    line(g, frame.ToPixel(0, 0), [ax, ay], HALT, 0.8, 1.4);
    for (let i = 0; i < 10; i++) {
      const a = (i / 10) * Math.PI * 2;
      const [px, py] = frame.ToPixel(Math.cos(a) * R * 0.6, Math.sin(a) * R * 0.6);
      arrow(g, [px, py], [px - Math.sin(a) * 8, py - Math.cos(a) * 8], INK, 0.75);
    }
    label(g, `axis tilt ${(P.AxisTilt ?? 0).toFixed(0)}°`, 16, height - 8, HALT);
  },
  MagnusForce(g, S, P, frame, width, height) {
    const R = S.Radius || 8;
    const [sx, sy] = frame.ToPixel(-R * 0.8, 0);
    const [ex, ey] = frame.ToPixel(R * 0.8, 0);
    arrow(g, [sx, sy], [ex, ey], INK, 0.55, 1.2);
    label(g, 'relative flow', width - 16, Math.round(height / 2) - 6, DIM, 'right');
    const lift = Math.min(0.9, 0.25 + Math.abs(P.SpinRate ?? 0) / 120);
    for (let i = 0; i < 5; i++) {
      const [px, py] = frame.ToPixel(-R * 0.4 + i * R * 0.2, 0);
      arrow(g, [px, py], [px, py - 16 * lift * Math.sign(P.SpinRate ?? 1)], AHEAD, 0.8);
    }
    ring(g, ...frame.ToPixel(0, 0), 5, HALT, 0.9, 1.4);
    label(g, `spin ${(P.SpinRate ?? 0).toFixed(0)} rad/s`, 16, height - 8, HALT);
  },
  Drag(g, S, P, frame, width, height) {
    const R = S.Radius || 9;
    ring(g, ...frame.ToPixel(0, 0), R * frame.Scale, DIM, 0.32, 1);
    const strength = Math.min(1, ((P.Linear ?? 0) + (P.Quadratic ?? 0) * 6) / 2.4);
    for (let i = 0; i < 7; i++) {
      const v = -R * 0.6 + (i / 6) * R * 1.2;
      const [px, py] = frame.ToPixel(-R * 0.75, v);
      const [qx, qy] = frame.ToPixel(R * 0.75 * (1 - strength * 0.3), v);
      arrow(g, [px, py], [qx, qy], INK, 0.35, 1);
      for (let k = 1; k < 4; k++) {
        const t = k / 4;
        line(g, [px + (qx - px) * t, py - 4], [px + (qx - px) * t, py + 4], HALT, 0.5 * strength, 1);
      }
    }
    label(g, `k₁ ${(P.Linear ?? 0).toFixed(2)} 1/s`, 16, height - 8, HALT);
    label(g, `k₂ ${(P.Quadratic ?? 0).toFixed(3)} s/m`, width - 16, height - 8, DIM, 'right');
  },
  Lift(g, S, P, frame, width, height) {
    const R = S.Radius || 7;
    const profile = [[-0.9, 0.05], [-0.5, 0.16], [0, 0.1], [0.5, 0.02], [0.9, 0.0]];
    g.strokeStyle = `rgba(${HALT[0]},${HALT[1]},${HALT[2]},0.9)`;
    g.lineWidth = 1.6;
    g.beginPath();
    profile.forEach(([u, v], i) => {
      const [px, py] = frame.ToPixel(u * R * 0.8, v * R * 1.2 - R * 0.25);
      i ? g.lineTo(px, py) : g.moveTo(px, py);
    });
    g.stroke();
    for (let i = 0; i < 5; i++) {
      const u = -R * 0.8 + (i / 4) * R * 1.6;
      const [px, py] = frame.ToPixel(u, -R * 0.62);
      const angle = M.Radians(P.AngleOfAttack ?? 0);
      const lift = (P.LiftCoefficient ?? 0.7) * 16;
      arrow(g, [px, py], [px - Math.sin(angle) * 9, py - lift * Math.cos(angle)], AHEAD, 0.75);
    }
    label(g, `C_L ${(P.LiftCoefficient ?? 0).toFixed(2)}`, 16, height - 8, HALT);
    label(g, `α ${(P.AngleOfAttack ?? 0).toFixed(0)}°`, width - 16, height - 8, DIM, 'right');
  },
  CurlNoise(g, S, P, frame, width, height) {
    const R = Math.max(S.HalfExtents[0], S.HalfExtents[1]) || 9;
    const scale = P.EddyScale ?? 4.6;
    for (let i = 0; i < 60; i++) {
      const u = (M.Fract(Math.sin(i * 12.9898) * 43758.5453) * 2 - 1) * R * 0.9;
      const v = (M.Fract(Math.sin(i * 78.233) * 12345.6789) * 2 - 1) * R * 0.72;
      const [px, py] = frame.ToPixel(u, v);
      const n = M.CurlNoise([u, v, 0.7], { Scale: scale, Seed: Math.round(P.Seed ?? 7), Octaves: Math.round(P.Octaves ?? 2) });
      const angle = Math.atan2(-n[1], n[0]);
      arrow(g, [px, py], [px + Math.cos(angle) * 7, py + Math.sin(angle) * 7], INK, 0.62, 1, 3);
    }
    label(g, `λ ${scale.toFixed(1)} m  ·  ${Math.round(P.Octaves ?? 1)} oct`, 16, height - 8, HALT);
  },
  BrownianNoise(g, S, P, frame, width, height) {
    const R = S.Radius || 7;
    for (let i = 0; i < 70; i++) {
      const u = (M.Fract(Math.sin(i * 3.3) * 9999) * 2 - 1) * R * 0.92;
      const v = (M.Fract(Math.cos(i * 7.7) * 7777) * 2 - 1) * R * 0.92;
      if (Math.hypot(u, v) > R * 0.95) continue;
      const n = M.GradientNoise(u / Math.max(0.2, P.CellScale ?? 1.8), v / Math.max(0.2, P.CellScale ?? 1.8), 2.7, Math.round(P.Seed ?? 23));
      const [px, py] = frame.ToPixel(u, v);
      const angle = n * Math.PI;
      line(g, [px, py], [px + Math.cos(angle) * 4.4, py + Math.sin(angle) * 4.4], INK, 0.6, 1);
    }
    ring(g, ...frame.ToPixel(0, 0), R * frame.Scale, DIM, 0.3, 1);
    label(g, `λ ${(P.CellScale ?? 0).toFixed(1)} m  ·  ${(P.Rate ?? 0).toFixed(1)} Hz`, 16, height - 8, HALT);
  },
  LorentzField(g, S, P, frame, width, height) {
    const ext = S.HalfExtents;
    for (let i = 0; i < 6; i++) {
      const u = -ext[2] * 0.85 + (i / 5) * ext[2] * 1.7;
      const [px, py] = frame.ToPixel(u, -ext[0] * 0.7);
      const charge = Math.sign(P.ChargeToMass ?? 1);
      const bend = Math.min(14, Math.abs(P.MagneticField ?? 0) * 3.2) * charge;
      const [qx, qy] = frame.ToPixel(u + Math.abs(bend) * 0.2, -ext[0] * 0.7);
      arrow(g, [px, py], [qx, qy], INK, 0.6);
      arrow(g, [px, py], [px + Math.cos(-Math.PI / 2 + bend * 0.06) * 14, py + Math.sin(-Math.PI / 2 + bend * 0.06) * 14], AHEAD, 0.7);
    }
    for (let i = 0; i < 4; i++) {
      const [px, py] = frame.ToPixel(-ext[2] * 0.7, -ext[0] * 0.2 + i * ext[0] * 0.3);
      line(g, [px, py], [px + 11, py], HALT, 0.7, 1.2);
    }
    label(g, 'E', 16, 14, HALT);
    label(g, `q/m ${(P.ChargeToMass ?? 0).toFixed(2)}`, 16, height - 8, DIM);
  },
  MagneticDipole(g, S, P, frame, width, height) {
    const R = S.Radius || 8;
    const aim = M.Radians(P.Aim ?? 0);
    const [mx, my] = frame.ToPixel(Math.sin(aim) * R * 0.55, Math.cos(aim) * R * 0.55);
    line(g, frame.ToPixel(0, 0), [mx, my], HALT, 0.9, 2);
    ring(g, mx, my, 3.4, HALT, 0.95, 1.2);
    for (let i = 0; i < 8; i++) {
      const a = (i / 8) * Math.PI * 2;
      const start = frame.ToPixel(Math.cos(a) * R * 0.25, Math.sin(a) * R * 0.25);
      const end = frame.ToPixel(Math.cos(a) * R * 0.85, Math.sin(a) * R * 0.85);
      const mid = frame.ToPixel(Math.cos(a + 0.5) * R * 0.62, Math.sin(a + 0.5) * R * 0.62);
      g.strokeStyle = `rgba(${INK[0]},${INK[1]},${INK[2]},0.7)`;
      g.lineWidth = 1.2;
      g.beginPath();
      g.moveTo(start[0], start[1]);
      g.quadraticCurveTo(mid[0], mid[1], end[0], end[1]);
      g.stroke();
    }
    label(g, `m ${(P.DipoleMoment ?? 1).toFixed(2)}`, 16, height - 8, HALT);
  },
  CustomFormula(g, S, P, frame, width, height) {
    const R = Math.max(S.HalfExtents[0], S.HalfExtents[1]) || 9;
    let compiled = null;
    try {
      // draw what the user's own expression produces, through the same compiler the field uses
      compiled = S.__Compiled || null;
    } catch {
      compiled = null;
    }
    for (let i = 0; i < 64; i++) {
      const u = (M.Fract(Math.sin(i * 9.17) * 4375.85) * 2 - 1) * R * 0.9;
      const v = (M.Fract(Math.sin(i * 4.73) * 1234.56) * 2 - 1) * R * 0.9;
      const [px, py] = frame.ToPixel(u, v);
      let angle = Math.atan2(v, u);
      if (compiled) {
        const sample = compiled.Sample(u, v, 0, 0.8);
        const length = Math.hypot(sample[0], sample[1]);
        if (length < 1e-4) continue;
        angle = Math.atan2(-sample[1], sample[0]);
      }
      arrow(g, [px, py], [px + Math.cos(angle) * 6.5, py + Math.sin(angle) * 6.5], INK, 0.6, 1, 3);
    }
    label(g, 'compiled expression', 16, height - 8, HALT);
  },
};

// A live diagram element for one source; call Paint after the source changes.
export function LawDiagram(source, law) {
  const wrap = El('div', { class: 'law-diagram' });
  const canvas = El('canvas', { 'aria-label': `${law.Name} diagram` });
  wrap.append(canvas);
  const paint = () => {
    const { Context: g, Width: width, Height: height } = FitCanvas(canvas, 104);
    g.clearRect(0, 0, width, height);
    g.fillStyle = '#0d0f13';
    g.fillRect(0, 0, width, height);
    const radius = Math.max(2, source.Radius || Math.max(source.HalfExtents?.[0] || 4, source.HalfExtents?.[1] || 4));
    const frame = frameFor(width, height, Math.min(radius, 14));
    axesCross(g, frame, width, height);
    const painter = DRAW[source.Law] || DRAW.LinearWind;
    try {
      painter(g, source, source.Params || {}, frame, width, height);
    } catch {
      label(g, 'diagram unavailable', 16, height - 8, DIM);
    }
  };
  return { Node: wrap, Paint: paint };
}
