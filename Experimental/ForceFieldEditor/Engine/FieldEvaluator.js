/*============================================================================================================================================
                                                      FIELD EVALUATOR
============================================================================================================================================*/
// 🧩 Accumulates every bounded force source into one vector field. Sources declare a channel — velocity, acceleration or damping — and the
// evaluator keeps those channels separate: advection velocities drive streamlines and motes, accelerations drive body dynamics, damping
// removes energy. Every contribution is multiplied by the attenuation of its finite envelope, so nothing leaks past its authored bound.

import * as M from './FieldMath.js';
import { LawById, ParamSchema, SanitizeParams, EnvelopeKinds, AttenuationKinds, DurationKinds, MotionKinds, LoopPolicies, FORCE_LAWS } from './ForceFieldCatalog.js';
import { CompileFieldProgram, DEFAULT_FORMULA_CODE } from './VectorFieldLang.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                  DOCUMENT SHAPE
//------------------------------------------------------------------------------------------------------------------------

let IdCounter = 0;
export function NewId(prefix = 'source') {
  IdCounter += 1;
  const stamp = Math.floor(Math.random() * 0x100000).toString(36);
  return `${prefix}-${stamp}${IdCounter}`;
}

export const DEFAULT_DOMAIN = Object.freeze({ Min: [-12, -12, -5], Max: [12, 12, 5] });

export function NewSource(lawId = 'LinearWind', overrides = {}) {
  const law = LawById[lawId] || FORCE_LAWS[0];
  const source = {
    Id: NewId(law.Id.toLowerCase()),
    Name: overrides.Name || law.Name,
    Law: law.Id,
    Enabled: true,
    Muted: false,
    Position: [0, 0, 0],
    Direction: [1, 0, 0],
    Envelope: law.Envelope || 'Sphere',
    Radius: law.Radius ?? 6,
    Height: law.Height ?? 8,
    HalfExtents: (law.HalfExtents || [6, 6, 4]).slice(),
    Attenuation: law.Attenuation || 'SmoothStep',
    Duration: law.Duration || 'Continuous',
    Motion: 'Static',
    LoopPolicy: 'PingPong',
    SwirlRadius: 4,
    OrbitSpeed: 0.25,
    PathLength: 8,
    PathBow: 0.35,
    PathPhase: 0,
    ModulationPeriod: 8,
    ModulationAttack: 1.4,
    ModulationRelease: 1.6,
    ModulationPhase: 0,
    ImpulseInterval: 4,
    ImpulseSpeed: 11,
    ImpulseWidth: 1.1,
    ImpulseStartRadius: 0.6,
    Params: SanitizeParams(law.Id, {}),
    Formula: DEFAULT_FORMULA_CODE,
  };
  if (law.Formula) source.Formula = law.Formula.Default;
  for (const [key, value] of Object.entries(overrides)) {
    if (key === 'Params') source.Params = SanitizeParams(law.Id, value);
    else source[key] = value;
  }
  return source;
}

export function SanitizeSource(raw) {
  const law = LawById[raw && raw.Law] ? raw.Law : 'LinearWind';
  const source = NewSource(law);
  source.Id = typeof raw.Id === 'string' && raw.Id ? raw.Id.slice(0, 64) : source.Id;
  source.Name = typeof raw.Name === 'string' && raw.Name.trim() ? raw.Name.slice(0, 60) : LawById[law].Name;
  source.Enabled = raw.Enabled !== false;
  source.Muted = raw.Muted === true;
  const num = (v, fallback, lo, hi) => (Number.isFinite(Number(v)) ? M.Clamp(Number(v), lo, hi) : fallback);
  // Tolerant: a single unusable component keeps its default instead of discarding the whole vector.
  const vec = (v, fallback) => (Array.isArray(v) && v.length === 3 ? v.map((c, i) => (Number.isFinite(Number(c)) ? Number(c) : fallback[i])) : fallback.slice());
  source.Position = vec(raw.Position, source.Position).map((c) => M.Clamp(c, -500, 500));
  source.Direction = M.Normalize(vec(raw.Direction, source.Direction), [0, 0, 1]);
  source.Envelope = EnvelopeKinds.includes(raw.Envelope) ? raw.Envelope : source.Envelope;
  source.Attenuation = AttenuationKinds.includes(raw.Attenuation) ? raw.Attenuation : source.Attenuation;
  source.Duration = DurationKinds.includes(raw.Duration) ? raw.Duration : source.Duration;
  source.Motion = MotionKinds.includes(raw.Motion) ? raw.Motion : 'Static';
  source.LoopPolicy = LoopPolicies.includes(raw.LoopPolicy) ? raw.LoopPolicy : 'PingPong';
  source.Radius = num(raw.Radius, source.Radius, 0.2, 200);
  source.Height = num(raw.Height, source.Height, 0.2, 400);
  source.HalfExtents = vec(raw.HalfExtents, source.HalfExtents).map((c) => M.Clamp(c, 0.2, 200));
  source.SwirlRadius = num(raw.SwirlRadius, source.SwirlRadius, 0, 100);
  source.OrbitSpeed = num(raw.OrbitSpeed, source.OrbitSpeed, 0, 20);
  source.PathLength = num(raw.PathLength, source.PathLength, 0, 200);
  source.PathBow = num(raw.PathBow, source.PathBow, -4, 4);
  source.PathPhase = num(raw.PathPhase, source.PathPhase, -10, 10);
  source.ModulationPeriod = num(raw.ModulationPeriod, source.ModulationPeriod, 0.05, 120);
  source.ModulationAttack = num(raw.ModulationAttack, source.ModulationAttack, 0.01, 120);
  source.ModulationRelease = num(raw.ModulationRelease, source.ModulationRelease, 0.01, 120);
  source.ModulationPhase = num(raw.ModulationPhase, source.ModulationPhase, -10, 10);
  source.ImpulseInterval = num(raw.ImpulseInterval, source.ImpulseInterval, 0.1, 120);
  source.ImpulseSpeed = num(raw.ImpulseSpeed, source.ImpulseSpeed, 0.01, 200);
  source.ImpulseWidth = num(raw.ImpulseWidth, source.ImpulseWidth, 0.05, 40);
  source.ImpulseStartRadius = num(raw.ImpulseStartRadius, source.ImpulseStartRadius, 0.05, 40);
  source.Params = SanitizeParams(law, raw.Params);
  source.Formula = typeof raw.Formula === 'string' && raw.Formula.trim() ? raw.Formula.slice(0, 2000) : source.Formula;
  return source;
}

export const DEFAULT_OVERLAY = Object.freeze({
  ShowVectors: true,
  ShowStreamlines: true,
  ShowMotes: true,
  ShowEnvelopes: true,
  ShowMotionPaths: true,
  ShowGrid: true,
  ShowProbe: true,
  Quantity: 'Speed',
  Colormap: 'Engineering',
  SliceAxis: 'XY',
  SliceOffset: 0,
  MaxValue: 18,
  AutoRange: true,
  HeatmapOpacity: 0.72,
  ArrowDensity: 4,
  ArrowScale: 0.9,
  StreamSeedCount: 90,
  StreamSteps: 70,
  StreamStepSize: 0.35,
  StreamDirection: 'Both',
  StreamColorMode: 'Elevation',
  MoteCount: 420,
  MoteCoupling: 2.6,
});

export const DEFAULT_SIM = Object.freeze({ TimeScale: 1, MotesRun: true, ImpulseAt: 0 });

export function DefaultDocument() {
  // Six generators chosen so the field reads on every slice plane: two velocity laws, three force laws (whose flow is
  // derived, see FlowAt) and one turbulence bed. Velocity scales are balanced so no single source saturates the ramp.
  const sources = [
    NewSource('RotationalColumn', { Name: 'Intake swirl', Position: [0, 0, 0], Radius: 5.2, Height: 9, Direction: [0, 0, 1], Attenuation: 'SmoothStep', Params: { Strength: 7.5, CoreRadius: 1.5, Inflow: 0.14, Updraft: 2.4 } }),
    NewSource('LinearWind', { Name: 'Prevailing wind', Position: [-5.6, -3.6, 0], Radius: 6.0, Height: 10, Direction: [0.94, 0.34, 0], Attenuation: 'SmoothStep', Params: { Strength: 6.4, Shear: 0.1, Veer: 1.4, GustAmplitude: 0.24, GustFrequency: 0.2, Turbulence: 0.2 } }),
    NewSource('Attractor', { Name: 'Gravity well', Position: [-8, 0, 0], Radius: 4.6, Attenuation: 'InverseSquare', Params: { Strength: 9, CoreRadius: 1.4, Swirl: 3.4, SpinBias: 0 } }),
    NewSource('RadialBurst', { Name: 'Pressure pulse', Position: [8, 0, 0], Radius: 4, Attenuation: 'SmoothStep', Duration: 'Impulse', ImpulseInterval: 4.5, ImpulseSpeed: 6, ImpulseWidth: 0.55, ImpulseScope: 3.6, Params: { Strength: 20, WaveSpeed: 6, ShellWidth: 1.0, StartRadius: 0.5, Decay: 0.6 } }),
    NewSource('CurlNoise', { Name: 'Eddy bed', Position: [0, 0, -3.8], HalfExtents: [10.5, 10.5, 0.9], Direction: [0, 0, 1], Params: { Strength: 4.2, EddyScale: 4.4, Octaves: 2, Drift: 0.3, Seed: 7 } }),
    NewSource('DirectedStream', { Name: 'Cross draft', Position: [-8.5, 0, 0], HalfExtents: [2.4, 8, 3.2], Direction: [1, 0, 0], Params: { Strength: 5.4, Spread: 0.4, Throat: 0.5, Turbulence: 0.12 } }),
  ];
  return { Format: 'FrontierForceField', Version: 1, Domain: { Min: [-12, -12, -5], Max: [12, 12, 5] }, Sources: sources, Overlay: { ...DEFAULT_OVERLAY }, Sim: { ...DEFAULT_SIM }, View: { Yaw: 0.72, Pitch: 0.55, Distance: 30, Target: [0, 0, 0] } };
}

export function SanitizeDocument(raw) {
  const base = DefaultDocument();
  if (!raw || typeof raw !== 'object') return base;
  const domain = raw.Domain && typeof raw.Domain === 'object' ? raw.Domain : base.Domain;
  const vec = (v, fallback) => (Array.isArray(v) && v.length === 3 && v.every((c) => Number.isFinite(Number(c))) ? v.map(Number) : fallback.slice());
  const out = {
    Format: 'FrontierForceField',
    Version: 1,
    Domain: { Min: vec(domain.Min, DEFAULT_DOMAIN.Min), Max: vec(domain.Max, DEFAULT_DOMAIN.Max) },
    Sources: Array.isArray(raw.Sources) ? raw.Sources.slice(0, 64).map(SanitizeSource) : base.Sources,
    Overlay: { ...DEFAULT_OVERLAY },
    Sim: { ...DEFAULT_SIM },
    View: { ...base.View },
  };
  const asBool = (v, fallback) => (typeof v === 'boolean' ? v : v === 'true' || v === 1 || v === '1' ? true : v === 'false' || v === 0 || v === '0' ? false : fallback);
  for (const key of Object.keys(DEFAULT_OVERLAY)) {
    const value = raw.Overlay ? raw.Overlay[key] : undefined;
    if (value === undefined) continue;
    if (typeof DEFAULT_OVERLAY[key] === 'boolean') out.Overlay[key] = asBool(value, DEFAULT_OVERLAY[key]);
    else if (typeof DEFAULT_OVERLAY[key] === 'number' && Number.isFinite(Number(value))) out.Overlay[key] = Number(value);
    else if (typeof DEFAULT_OVERLAY[key] === 'string' && typeof value === 'string') out.Overlay[key] = value;
  }
  for (const key of Object.keys(DEFAULT_SIM)) {
    const value = raw.Sim ? raw.Sim[key] : undefined;
    if (value === undefined) continue;
    if (typeof DEFAULT_SIM[key] === 'boolean') out.Sim[key] = value === true;
    else if (Number.isFinite(Number(value))) out.Sim[key] = Number(value);
  }
  if (raw.View && typeof raw.View === 'object') {
    const v = raw.View;
    if (Number.isFinite(Number(v.Yaw))) out.View.Yaw = Number(v.Yaw);
    if (Number.isFinite(Number(v.Pitch))) out.View.Pitch = M.Clamp(Number(v.Pitch), -1.45, 1.45);
    if (Number.isFinite(Number(v.Distance))) out.View.Distance = M.Clamp(Number(v.Distance), 4, 400);
    out.View.Target = vec(v.Target, [0, 0, 0]);
  }
  return out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  FRAME PREPARATION
//------------------------------------------------------------------------------------------------------------------------

// One runtime record per live source: its resolved anchor, oriented frame, law and compiled formula.
export function PrepareFrame(sources, time, domain) {
  const runtime = [];
  for (const source of sources || []) {
    if (!source.Enabled || source.Muted) continue;
    const law = LawById[source.Law];
    if (!law) continue;
    const anchor = M.MotionAnchorAt(source, time);
    const frame = M.DirectionFrame(source.Direction);
    const derived = { ...source, Anchor: anchor, Frame: frame };
    const record = { Source: source, Law: law, Anchor: anchor, Frame: frame, Derived: derived, Compiled: null };
    if (source.Law === 'CustomFormula') record.Compiled = CompileFieldProgram(source.Formula);
    runtime.push(record);
  }
  return runtime;
}

const randomVectorNoise = (p, scale, time, seed) => [
  M.GradientNoise(p[0] / scale + time * 0.31, p[1] / scale - time * 0.17, p[2] / scale + 7.1, seed),
  M.GradientNoise(p[0] / scale - 3.7, p[1] / scale + time * 0.23, p[2] / scale + 1.3, seed + 11),
  M.GradientNoise(p[0] / scale + 5.9, p[1] / scale + 2.3, p[2] / scale - time * 0.27, seed + 23),
];

const velocityOnly = (runtime, point, time) => {
  let v = [0, 0, 0];
  for (const record of runtime) {
    if (record.Law.Channel !== 'velocity') continue;
    const env = M.EnvelopeCoordinate(record.Derived, point);
    if (!env.Inside) continue;
    const a = M.AttenuationOf(record.Source, env.U);
    if (a <= 1e-6) continue;
    const gate = M.DurationGate(record.Source, time, M.Length(env.Local));
    if (gate <= 1e-6) continue;
    const r = LawContribution(record, point, time, env, null);
    v = M.Add(v, M.Scale(r.Velocity, a * gate));
  }
  return v;
};

//------------------------------------------------------------------------------------------------------------------------
//                                              FLOW DERIVATION
//------------------------------------------------------------------------------------------------------------------------

// Force laws (acceleration channel) carry no velocity of their own, so a streamline through one would stall. The studio
// therefore derives a flow field for them with a cheap frozen map: the acceleration at a single fixed reference point is
// integrated once, in the frame of that reference, giving every generator a streamable drift. The result is continuous in
// space (so lines do not jump between adjacent samples) and costs one EvaluateAt per probe.
export function PrepareFlow(runtime, time, options = {}) {
  const drift = options.Drift ?? 0.42;
  return { Runtime: runtime, Time: time, Drift: drift, Brushes: new Map(), DriftMaps: new Map() };
}

// Cached drift brush. A force law's induced motion is the arc its own acceleration sweeps over the drift window, measured
// once at a fixed reference offset from the anchor. The brush is then painted along that arc, so a swirl genuinely curls
// and a sink genuinely converges — the shape comes from the physics, the placement from the envelope.
function DriftBrush(flow, record) {
  const key = record.Source.Id;
  const cached = flow.DriftMaps.get(key);
  if (cached) return cached;
  const source = record.Source;
  const anchor = M.MotionAnchorAt(source, flow.Time);
  const core = Math.max(0.35, Number(source.Params?.CoreRadius ?? 1.4));
  // Sample at a characteristic offset inside the envelope: the core radius, clamped well inside the wall.
  const reach = Math.max(0.5, Number(source.Radius ?? 4));
  const offset = Math.min(core, reach * 0.55);
  const e1 = (record.Frame?.E1) || M.DirectionFrame(source.Direction).E1;
  const start = M.Add(anchor, M.Scale(e1, offset));
  const single = [record];
  const h = flow.Drift / 4;
  let p = start.slice();
  let v = [0, 0, 0];
  for (let i = 0; i < 4; i++) {
    const sample = EvaluateAt(single, p, flow.Time);
    v = M.Scale(M.Add(v, M.Scale(sample.Acceleration, h)), Math.exp(-Math.max(0, sample.Damping) * h));
    p = M.Add(p, M.Scale(v, h));
  }
  // The swept path, in anchor-local coordinates: this is the shape every point inside the envelope inherits.
  const path = [];
  p = start.slice();
  v = [0, 0, 0];
  let travelled = 0;
  for (let i = 0; i < 8; i++) {
    const sample = EvaluateAt(single, p, flow.Time);
    const hh = flow.Drift / 8;
    v = M.Scale(M.Add(v, M.Scale(sample.Acceleration, hh)), Math.exp(-Math.max(0, sample.Damping) * hh));
    p = M.Add(p, M.Scale(v, hh));
    path.push(M.Sub(p, anchor));
    travelled += M.Length(M.Scale(v, hh));
  }
  const chord = M.Sub(p, start);
  const brush = {
    Path: path,
    Chord: chord,
    Length: M.Length(chord),
    Core: core,
    Anchor: anchor,
    Falloff: Math.max(0.6, reach * 0.5),
    // a is the inward/outward pull along the local radius, b the circulation about the anchor's axis.
    Radial: M.Length(chord) * Math.sign(M.Dot(chord, M.Sub(start, anchor))),
    Axis: record.Frame?.Axis || M.DirectionFrame(source.Direction).Axis,
    Swirl: Number(source.Params?.Swirl ?? 0) * 0.1,
  };
  flow.DriftMaps.set(key, brush);
  return brush;
}

// The flow at a point: medium velocity, plus each force law's drift painted from its brush.
export function FlowSample(flow, point) {
  const { Runtime: runtime, Time: time } = flow;
  let v = [0, 0, 0];
  for (const record of runtime) {
    const env = M.EnvelopeCoordinate(record.Derived, point);
    if (!env.Inside) continue;
    let attenuation = M.AttenuationOf(record.Source, env.U);
    if (record.Source.Duration === 'Impulse') attenuation = M.ImpulseScope(record.Source, M.Length(env.Local), time);
    const gate = M.DurationGate(record.Source, time, M.Length(env.Local));
    const weight = attenuation * gate;
    if (weight <= 1e-6) continue;
    const sample = EvaluateAt(runtime, point, time);
    if (record.Law.Channel === 'acceleration') {
      // The brush: a pull toward (or push from) the anchor plus a circulation about its axis, both fading at the
      // envelope wall. This reproduces the shape a probe would trace, without another field evaluation.
      const brush = DriftBrush(flow, record);
      const local = M.Sub(point, brush.Anchor);
      const radial = M.Length(local);
      const fade = Math.exp(-Math.pow(radial / Math.max(0.6, brush.Falloff), 2));
      const dir = radial > 1e-5 ? M.Scale(local, 1 / radial) : [0, 0, 0];
      const axis = brush.Axis || [0, 0, 1];
      const tangent = M.Normalize(M.Cross(axis, dir), [0, 0, 0]);
      const pull = M.Scale(dir, -brush.Radial * 2.2);
      const spin = M.Scale(tangent, Math.abs(brush.Radial) * brush.Swirl * 6);
      v = M.Add(v, M.Scale(M.Add(pull, spin), fade * weight));
    } else {
      v = M.Add(v, M.Scale(sample.Velocity, weight));
    }
  }
  return v;
}

export function FlowAt(runtime, point, time, options = {}) {
  const flow = options.Flow || PrepareFlow(runtime, time, options);
  return FlowSample(flow, point);
}

export function FlowSpeedAt(runtime, point, time, options = {}) {
  return M.Length(FlowAt(runtime, point, time, options));
}

// The read-out field for a viewport: `Speed` is the drawn flow, everything else is the sampled quantity. One extra
// EvaluateAt per mode per step, and only when a law actually drives the drawing.
export function VisualFieldAt(runtime, point, time, options = {}) {
  const flow = options.Flow || PrepareFlow(runtime, time, options);
  return { Flow: FlowSample(flow, point), Sample: EvaluateAt(runtime, point, time) };
}

// The vector a single law contributes at `point`. Returns { Velocity, Acceleration, Damping } before attenuation scaling.
function LawContribution(record, point, time, env, ambient) {
  const source = record.Source;
  const P = source.Params || {};
  const S = Number(P.Strength ?? 0);
  const local = env.Local;
  const frame = record.Frame;
  const axis = frame.Axis;
  const e1 = frame.E1;
  const e2 = frame.E2;
  const r = M.Length(local);
  const radial = M.Hypot2 ? M.Hypot2(local[0], local[1]) : Math.hypot(local[0], local[1]);
  const dir = r > 1e-6 ? local.map((c) => c / r) : axis.slice();
  const needMedium = source.Law === 'MagnusForce' || source.Law === 'Lift' || source.Law === 'LorentzField';
  const medium = needMedium ? (ambient || [0, 0, 0]) : null;

  switch (source.Law) {
    case 'LinearWind': {
      const h = local[2];
      const speed = S * Math.max(0.05, 1 + (P.Shear ?? 0) * h);
      const bearing = M.RotateBearing(axis, (P.Veer ?? 0) * h);
      const gust = 1 + (P.GustAmplitude ?? 0) * Math.sin(2 * Math.PI * (P.GustFrequency ?? 0.2) * time);
      const rough = M.CurlNoise(point, { Scale: 4.5, Seed: 3, Drift: time * 0.12, Octaves: 1 });
      const turbulence = (P.Turbulence ?? 0) * S * 0.35;
      return { Velocity: M.Add(M.Scale(bearing, speed * gust), M.Scale(rough, turbulence)), Acceleration: [0, 0, 0], Damping: 0 };
    }
    case 'DirectedStream': {
      const ext = source.HalfExtents;
      const rho = Math.min(1, Math.hypot(local[0] / Math.max(0.01, ext[0]), local[1] / Math.max(0.01, ext[1])));
      const axial = (local[2] + ext[2]) / Math.max(0.01, ext[2] * 2);
      const inlet = M.SmoothStep(0, 0.28, axial);
      const profile = (1 - (P.Spread ?? 0) * rho * rho) * (1 + (P.Throat ?? 0) * (1 - rho)) * inlet;
      const wander = M.CurlNoise(point, { Scale: 3.2, Seed: 9, Drift: time * 0.2, Octaves: 1 });
      return {
        Velocity: M.Add(M.Scale(axis, S * profile), M.Scale(wander, (P.Turbulence ?? 0) * S * 0.25)),
        Acceleration: [0, 0, 0],
        Damping: 0,
      };
    }
    case 'Buoyancy': {
      const g = (P.Gravity ?? 9.81) / 9.81;
      const lift = S * (P.DensityRatio ?? 1.6) * g * (1 - (P.HeightFalloff ?? 0.35) * M.Saturate(env.U));
      return { Velocity: [0, 0, 0], Acceleration: M.Scale([0, 0, 1], lift), Damping: 0 };
    }
    case 'RadialBurst': {
      return { Velocity: [0, 0, 0], Acceleration: M.Scale(dir, S), Damping: 0 };
    }
    case 'Attractor': {
      const core = Math.max(0.05, P.CoreRadius ?? 1.6);
      const pull = S / (1 + Math.pow(r / core, 2));
      const inward = M.Scale(dir, -pull);
      const swirl = M.Scale(M.Normalize(M.Cross(axis, dir), e1), P.Swirl ?? 0);
      const bias = M.Scale(axis, P.SpinBias ?? 0);
      return { Velocity: [0, 0, 0], Acceleration: M.Add(M.Add(inward, swirl), bias), Damping: 0 };
    }
    case 'Repulsor': {
      const core = Math.max(0.05, P.CoreRadius ?? 1.2);
      const power = Math.max(1, P.Sharpness ?? 2.4);
      const push = S / (1 + Math.pow(r / core, power));
      return { Velocity: [0, 0, 0], Acceleration: M.Add(M.Scale(dir, push), M.Scale(axis, P.Lift ?? 0)), Damping: 0 };
    }
    case 'AxisAttractor': {
      const length = Math.max(0.1, P.AxisLength ?? 12);
      const along = M.Clamp(M.Dot(local, axis), 0, length);
      const closest = M.Scale(axis, along);
      const delta = M.Sub(local, closest);
      const dist = M.Length(delta);
      const core = Math.max(0.05, P.CoreRadius ?? 1.4);
      const pull = S / (1 + Math.pow(dist / core, 2));
      return {
        Velocity: [0, 0, 0],
        Acceleration: M.Add(M.Scale(M.Normalize(delta, e1), -pull), M.Scale(axis, P.AxialPull ?? 0)),
        Damping: 0,
      };
    }
    case 'SplineForce': {
      const length = Math.max(1, P.PathLength ?? 11);
      const bow = (P.Bow ?? 0.32) * length;
      const p0 = [0, 0, 0];
      const p1 = M.Add(M.Scale(axis, length / 3), M.Scale(e1, bow));
      const p2 = M.Add(M.Scale(axis, (2 * length) / 3), M.Scale(e1, -bow));
      const p3 = M.Scale(axis, length);
      let best = Infinity;
      let bestPoint = p0;
      let bestTangent = axis;
      let prev = p0;
      for (let i = 0; i <= 14; i++) {
        const u = i / 14;
        const q = BezierPoint(p0, p1, p2, p3, u);
        const d = M.Distance(q, local);
        if (d < best) {
          best = d;
          bestPoint = q;
          bestTangent = i > 0 ? M.Normalize(M.Sub(q, prev), axis) : M.Normalize(M.Sub(BezierPoint(p0, p1, p2, p3, 0.02), p0), axis);
        }
        prev = q;
      }
      const capture = Math.max(0.1, P.TubeRadius ?? 2.6);
      const weight = 1 - M.SmoothStep(0, capture, best);
      const pull = M.Scale(M.Sub(bestPoint, local), S);
      const push = M.Scale(bestTangent, P.TangentialPush ?? 0);
      return { Velocity: [0, 0, 0], Acceleration: M.Scale(M.Add(pull, push), weight), Damping: 0 };
    }
    case 'BoundaryAdhesion': {
      const wall = M.EnvelopeWall(record.Derived, point);
      const target = P.TargetDistance ?? 0.35;
      const sigma = Math.max(0.05, P.Softness ?? 0.7);
      const offset = wall.Distance - target;
      const cling = -Math.sign(offset) * Math.exp(-Math.pow(offset / sigma, 2)) * S;
      return { Velocity: [0, 0, 0], Acceleration: M.Add(M.Scale(wall.Normal, cling), M.Scale(wall.Normal, P.NormalBias ?? 0)), Damping: 0 };
    }
    case 'Containment': {
      const outer = Math.max(0.2, source.Radius);
      const inner = outer * M.Clamp(P.InnerRatio ?? 0.55, 0.02, 0.98);
      if (r <= inner) return { Velocity: [0, 0, 0], Acceleration: [0, 0, 0], Damping: 0 };
      const t = (r - inner) / Math.max(0.01, outer - inner);
      return {
        Velocity: [0, 0, 0],
        Acceleration: M.Scale(dir, -S * t),
        Damping: (P.WallDamping ?? 0.8) * t,
      };
    }
    case 'RotationalColumn': {
      const core = Math.max(0.1, P.CoreRadius ?? 1.5);
      const rho = radial / core;
      const tangential = (S * (1 - Math.exp(-rho * rho))) / Math.max(rho, 0.08);
      const radialDir = radial > 1e-5 ? [local[0] / radial, local[1] / radial, 0] : [1, 0, 0];
      const tangentDir = M.Normalize(M.Cross(axis, radialDir), e1);
      const inflow = -((P.Inflow ?? 0.22) * rho * S * 0.12);
      const updraft = (P.Updraft ?? 3.2) * Math.exp(-0.42 * rho * rho);
      return {
        Velocity: M.Add(M.Add(M.Scale(tangentDir, tangential), M.Scale(radialDir, inflow)), M.Scale(axis, updraft)),
        Acceleration: [0, 0, 0],
        Damping: 0,
      };
    }
    case 'Torque': {
      const tilt = M.Radians(P.AxisTilt ?? 0);
      const spinAxis = M.Normalize(M.Add(M.Scale(axis, Math.cos(tilt)), M.Scale(e1, Math.sin(tilt))), axis);
      const core = Math.max(0.1, P.CoreRadius ?? 1.1);
      const gain = S / (1 + Math.pow(r / core, 2));
      return { Velocity: [0, 0, 0], Acceleration: M.Scale(M.Normalize(M.Cross(spinAxis, dir), e2), gain), Damping: 0 };
    }
    case 'MagnusForce': {
      const speed = M.Length(medium);
      if (speed < 1e-3) return { Velocity: [0, 0, 0], Acceleration: [0, 0, 0], Damping: 0 };
      const spinAxis = axis;
      const curve = M.Cross(spinAxis, M.Scale(medium, 1 / speed));
      const core = Math.max(0.1, P.CoreRadius ?? 1.8);
      const gain = S / (1 + Math.pow(r / core, 2));
      return { Velocity: [0, 0, 0], Acceleration: M.Scale(curve, gain), Damping: 0 };
    }
    case 'Drag': {
      const speed = M.Length(medium || [0, 0, 0]);
      const k = ((P.Linear ?? 0.9) + (P.Quadratic ?? 0.06) * speed) * (P.Strength ?? 1);
      return { Velocity: [0, 0, 0], Acceleration: [0, 0, 0], Damping: k };
    }
    case 'Lift': {
      const speed = M.Length(medium);
      if (speed < 1e-3) return { Velocity: [0, 0, 0], Acceleration: [0, 0, 0], Damping: 0 };
      const flow = M.Scale(medium, 1 / speed);
      const normal = M.Normalize(M.Cross(axis, flow), [0, 0, 1]);
      const aoa = M.Radians(P.AngleOfAttack ?? 0);
      const tilted = M.Normalize(M.Add(M.Scale(normal, Math.cos(aoa)), M.Scale(flow, Math.sin(aoa))), normal);
      const density = (P.AirDensity ?? 1.225) / 1.225;
      const gain = S * (P.LiftCoefficient ?? 0.7) * density * Math.pow(speed / 8, 2);
      return { Velocity: [0, 0, 0], Acceleration: M.Scale(tilted, gain), Damping: 0 };
    }
    case 'CurlNoise': {
      const v = M.CurlNoise(point, {
        Scale: Math.max(0.2, P.EddyScale ?? 4.6),
        Seed: Math.round(P.Seed ?? 7),
        Drift: (P.Drift ?? 0.35) * time,
        Octaves: Math.round(P.Octaves ?? 2),
      });
      return { Velocity: M.Scale(v, S), Acceleration: [0, 0, 0], Damping: 0 };
    }
    case 'BrownianNoise': {
      const jitter = randomVectorNoise(point, Math.max(0.05, P.CellScale ?? 1.8), (P.Rate ?? 0.9) * time, Math.round(P.Seed ?? 23));
      return { Velocity: [0, 0, 0], Acceleration: M.Scale(jitter, S), Damping: 0 };
    }
    case 'LorentzField': {
      const qm = P.ChargeToMass ?? 1.15;
      const electric = M.Scale(axis, (P.ElectricField ?? 1.4) * qm);
      const magnetic = M.Cross(medium, M.Scale(axis, (P.MagneticField ?? 2.1) * qm));
      return { Velocity: [0, 0, 0], Acceleration: M.Scale(M.Add(electric, magnetic), S), Damping: 0 };
    }
    case 'MagneticDipole': {
      const aim = M.Radians(P.Aim ?? 0);
      const moment = M.Normalize(M.Add(M.Scale(axis, Math.cos(aim)), M.Scale(e1, Math.sin(aim))), axis);
      const momentGain = P.DipoleMoment ?? 1;
      const soft = Math.max(0.1, P.SoftRadius ?? 0.9);
      const rr = Math.max(soft, r);
      const projection = M.Dot(moment, dir);
      const field = M.Sub(M.Scale(dir, 3 * projection), M.Scale(moment, 1));
      const gain = (S * momentGain * Math.pow(soft / rr, 3)) / Math.max(0.08, soft);
      return { Velocity: [0, 0, 0], Acceleration: M.Scale(field, gain), Damping: 0 };
    }
    case 'CustomFormula': {
      if (!record.Compiled) record.Compiled = CompileFieldProgram(source.Formula);
      const t = time * (P.FormulaSpeed ?? 1);
      const v = record.Compiled.Sample(point[0], point[1], point[2], t);
      return { Velocity: M.Scale(v, S), Acceleration: [0, 0, 0], Damping: 0 };
    }
    default:
      return { Velocity: [0, 0, 0], Acceleration: [0, 0, 0], Damping: 0 };
  }
}

function BezierPoint(p0, p1, p2, p3, u) {
  const i = 1 - u;
  return [
    i * i * i * p0[0] + 3 * i * i * u * p1[0] + 3 * i * u * u * p2[0] + u * u * u * p3[0],
    i * i * i * p0[1] + 3 * i * i * u * p1[1] + 3 * i * u * u * p2[1] + u * u * u * p3[1],
    i * i * i * p0[2] + 3 * i * i * u * p1[2] + 3 * i * u * u * p2[2] + u * u * u * p3[2],
  ];
}

export const HasMediumSources = (sources) =>
  (sources || []).some((s) => s.Enabled && !s.Muted && ['MagnusForce', 'Lift', 'LorentzField', 'Drag'].includes(s.Law));

//------------------------------------------------------------------------------------------------------------------------
//                                                  EVALUATION
//------------------------------------------------------------------------------------------------------------------------

// Field sample at `point`: separate velocity / acceleration / damping channels plus the bounded influence sum.
export function EvaluateAt(runtime, point, time, options = {}) {
  const trace = options.Trace === true;
  const ambient = options.Ambient || null;
  const out = { Velocity: [0, 0, 0], Acceleration: [0, 0, 0], Damping: 0, Influence: 0, PerSource: trace ? [] : null };
  const needsMedium = options.NeedsMedium === true && ambient === null;
  const medium = needsMedium ? velocityOnly(runtime, point, time) : ambient || [0, 0, 0];
  for (const record of runtime) {
    const env = M.EnvelopeCoordinate(record.Derived, point);
    if (!env.Inside) {
      if (trace) out.PerSource.push({ Id: record.Source.Id, Name: record.Source.Name, Law: record.Law.Id, Weight: 0, U: env.U, Inside: false });
      continue;
    }
    let attenuation = M.AttenuationOf(record.Source, env.U);
    // An impulse shell carries its own radial profile, so it is bounded by reach instead of the static curve.
    if (record.Source.Duration === 'Impulse') attenuation = M.ImpulseScope(record.Source, M.Length(env.Local), time);
    if (attenuation <= 1e-6) {
      if (trace) out.PerSource.push({ Id: record.Source.Id, Name: record.Source.Name, Law: record.Law.Id, Weight: 0, U: env.U, Inside: true });
      continue;
    }
    const gate = M.DurationGate(record.Source, time, M.Length(env.Local));
    const weight = attenuation * gate;
    if (weight <= 1e-6) continue;
    const contribution = LawContribution(record, point, time, env, medium);
    out.Velocity = M.Add(out.Velocity, M.Scale(contribution.Velocity, weight));
    out.Acceleration = M.Add(out.Acceleration, M.Scale(contribution.Acceleration, weight));
    out.Damping += contribution.Damping * weight;
    out.Influence += attenuation;
    if (trace) {
      out.PerSource.push({
        Id: record.Source.Id,
        Name: record.Source.Name,
        Law: record.Law.Id,
        Weight: weight,
        U: env.U,
        Inside: true,
        Velocity: M.Scale(contribution.Velocity, weight),
        Acceleration: M.Scale(contribution.Acceleration, weight),
      });
    }
  }
  return out;
}

export const VelocityAt = (runtime, point, time, options) => EvaluateAt(runtime, point, time, options).Velocity;
export const SpeedAt = (runtime, point, time, options) => M.Length(EvaluateAt(runtime, point, time, options).Velocity);

// Curl and divergence of the velocity channel by central differences.
export function VelocityDerivatives(runtime, point, time, options = {}) {
  const h = options.Step ?? 0.12;
  const xp = EvaluateAt(runtime, [point[0] + h, point[1], point[2]], time, options).Velocity;
  const xm = EvaluateAt(runtime, [point[0] - h, point[1], point[2]], time, options).Velocity;
  const yp = EvaluateAt(runtime, [point[0], point[1] + h, point[2]], time, options).Velocity;
  const ym = EvaluateAt(runtime, [point[0], point[1] - h, point[2]], time, options).Velocity;
  const zp = EvaluateAt(runtime, [point[0], point[1], point[2] + h], time, options).Velocity;
  const zm = EvaluateAt(runtime, [point[0], point[1], point[2] - h], time, options).Velocity;
  const dVx = [(xp[0] - xm[0]) / (2 * h), (yp[0] - ym[0]) / (2 * h), (zp[0] - zm[0]) / (2 * h)];
  const dVy = [(xp[1] - xm[1]) / (2 * h), (yp[1] - ym[1]) / (2 * h), (zp[1] - zm[1]) / (2 * h)];
  const dVz = [(xp[2] - xm[2]) / (2 * h), (yp[2] - ym[2]) / (2 * h), (zp[2] - zm[2]) / (2 * h)];
  return {
    Divergence: dVx[0] + dVy[1] + dVz[2],
    Curl: [dVy[2] - dVz[1], dVz[0] - dVx[2], dVx[1] - dVy[0]],
  };
}

// Scalar channel the viewport heatmaps and the 2-D section draw.
export function QuantityAt(runtime, point, time, quantity, options = {}) {
  if (quantity === 'Acceleration') return M.Length(EvaluateAt(runtime, point, time, options).Acceleration);
  if (quantity === 'Curl') return M.Length(VelocityDerivatives(runtime, point, time, options).Curl);
  if (quantity === 'Divergence') return VelocityDerivatives(runtime, point, time, options).Divergence;
  if (quantity === 'Influence') return EvaluateAt(runtime, point, time, options).Influence;
  return M.Length(EvaluateAt(runtime, point, time, options).Velocity);
}

// Adaptive maximum for the auto range: samples the domain corners, centre and face centres.
export function SuggestMaximum(runtime, domain, time, quantity) {
  const [min, max] = [domain.Min, domain.Max];
  const centre = [(min[0] + max[0]) / 2, (min[1] + max[1]) / 2, (min[2] + max[2]) / 2];
  const points = [centre];
  const steps = 4;
  for (let i = 0; i < steps; i++) {
    for (let j = 0; j < steps; j++) {
      for (let k = 0; k < steps; k++) {
        points.push([
          min[0] + ((max[0] - min[0]) * (i + 0.5)) / steps,
          min[1] + ((max[1] - min[1]) * (j + 0.5)) / steps,
          min[2] + ((max[2] - min[2]) * (k + 0.5)) / steps,
        ]);
      }
    }
  }
  // Speed is the drawn flow (see FlowAt), so the auto range must cover the flow field the views actually render.
  const samples = points
    .map((p) => (quantity === 'Speed' ? M.Length(FlowAt(runtime, p, time)) : Math.abs(QuantityAt(runtime, p, time, quantity))))
    .sort((a, b) => a - b);
  // Auto range tracks a high percentile rather than the single hottest probe, so one hotspot cannot flatten the
  // rest of the slice, while never dropping below 35 % of the true peak.
  const peak = samples[samples.length - 1] || 0;
  const percentile = samples[Math.min(samples.length - 1, Math.floor(samples.length * 0.92))] || 0;
  return Math.max(1e-3, Math.max(percentile, peak * 0.35));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PROBE STEP (motes)
//------------------------------------------------------------------------------------------------------------------------

// Advances one advected mote: couples to the medium velocity, integrates acceleration and damping, returns the new state.
export function AdvectMote(runtime, mote, dt, time, options = {}) {
  const coupling = options.Coupling ?? 2.6;
  const sample = EvaluateAt(runtime, mote.P, time, options);
  const medium = sample.Velocity;
  const k = 1 - Math.exp(-Math.max(0, coupling) * dt);
  const vx = mote.V[0] + (medium[0] - mote.V[0]) * k + sample.Acceleration[0] * dt;
  const vy = mote.V[1] + (medium[1] - mote.V[1]) * k + sample.Acceleration[1] * dt;
  const vz = mote.V[2] + (medium[2] - mote.V[2]) * k + sample.Acceleration[2] * dt;
  const damp = Math.exp(-Math.max(0, sample.Damping) * dt);
  mote.V = [vx * damp, vy * damp, vz * damp];
  mote.P = [mote.P[0] + mote.V[0] * dt, mote.P[1] + mote.V[1] * dt, mote.P[2] + mote.V[2] * dt];
  mote.Age += dt;
  return mote;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SERIALISATION
//------------------------------------------------------------------------------------------------------------------------

export function DocumentToJson(document) {
  return JSON.stringify(SanitizeDocument(document), null, 2);
}

export function DocumentFromJson(text) {
  const raw = JSON.parse(text);
  if (!raw || typeof raw !== 'object') throw new Error('Not a force field document');
  if (raw.Format && raw.Format !== 'FrontierForceField') throw new Error(`Unknown format "${raw.Format}"`);
  if (!Array.isArray(raw.Sources)) throw new Error('Document has no Sources array');
  return SanitizeDocument(raw);
}

// Plain-text companion format for pipeline hand-off; mirrors the shape of the retired cascade document.
export function DocumentToCascadeText(document) {
  const doc = SanitizeDocument(document);
  const lines = [
    '# Frontier ForceField cascade — exported from the ForceField Editor',
    `[Domain]`,
    `Min = ${doc.Domain.Min.map((v) => v.toFixed(3)).join(', ')}`,
    `Max = ${doc.Domain.Max.map((v) => v.toFixed(3)).join(', ')}`,
    '',
  ];
  for (const source of doc.Sources) {
    lines.push('[[Source]]');
    lines.push(`Id = "${source.Id}"`);
    lines.push(`Name = "${source.Name.replace(/"/g, '')}"`);
    lines.push(`Law = "${source.Law}"`);
    lines.push(`Enabled = ${source.Enabled}`);
    lines.push(`Position = ${source.Position.map((v) => v.toFixed(3)).join(', ')}`);
    lines.push(`Direction = ${source.Direction.map((v) => v.toFixed(4)).join(', ')}`);
    lines.push(`Envelope = "${source.Envelope}"`);
    lines.push(`Radius = ${source.Radius.toFixed(3)}`);
    lines.push(`Height = ${source.Height.toFixed(3)}`);
    lines.push(`HalfExtents = ${source.HalfExtents.map((v) => v.toFixed(3)).join(', ')}`);
    lines.push(`Attenuation = "${source.Attenuation}"`);
    lines.push(`Duration = "${source.Duration}"`);
    lines.push(`Motion = "${source.Motion}"`);
    const params = ParamSchema(source.Law).map((p) => `${p.Key} = ${Number(source.Params[p.Key]).toFixed(4)}`);
    lines.push(`[Source.Params]`);
    lines.push(...params);
    lines.push('');
  }
  return lines.join('\n');
}

// A spreadsheet-friendly source table: one row per generator with its bound, duration, motion and every parameter.
export function DocumentToCsv(document) {
  const doc = SanitizeDocument(document);
  const paramKeys = [...new Set(doc.Sources.flatMap((source) => ParamSchema(source.Law).map((p) => p.Key)))];
  const header = ['Id', 'Name', 'Law', 'Domain', 'Channel', 'Enabled', 'Muted', 'Anchor X', 'Anchor Y', 'Anchor Z', 'Dir X', 'Dir Y', 'Dir Z', 'Envelope', 'Radius', 'Height', 'Half X', 'Half Y', 'Half Z', 'Attenuation', 'Duration', 'Motion', ...paramKeys];
  const escape = (v) => {
    const text = String(v ?? '');
    return /[",\n]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
  };
  const rows = doc.Sources.map((source) => {
    const law = LawById[source.Law];
    return [
      source.Id, source.Name, source.Law, law ? law.Domain : '', law ? law.Channel : '',
      source.Enabled, source.Muted,
      ...source.Position, ...source.Direction,
      source.Envelope, source.Radius, source.Height, ...source.HalfExtents,
      source.Attenuation, source.Duration, source.Motion,
      ...paramKeys.map((key) => (source.Params[key] !== undefined ? source.Params[key] : '')),
    ].map(escape).join(',');
  });
  return [header.join(','), ...rows].join('\n');
}
