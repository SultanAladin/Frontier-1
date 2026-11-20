/*============================================================================================================================================
                                                            FORCELAWREPOSITORY.JS
============================================================================================================================================*/
// Canonical force-law registry for the Frontier ForceField Editor HTML reference studio (+Z up, zero rigid-body physics).
// This studio is the HTML specification unit for the queued Fluid / ForceField / Particle ground-up redo (CLAUDE.md 1.2 + 2.1).
// Nineteen analytic force laws across three domains: VectorField, SlipstreamField, TurbulenceField.

"use strict";

const DEG_TO_RAD = Math.PI / 180.0;
const TWO_PI = Math.PI * 2.0;

//------------------------------------------------------------------------------------------------------------------------
//                                                    DOMAINS & OPTIONS
//------------------------------------------------------------------------------------------------------------------------

const ForceDomains = Object.freeze({
  VectorField: {
    Id: "VectorField",
    Label: "Vector Field",
    Subtitle: "Analytic directional, radial, orbital & electromagnetic force laws",
    Tint: "#4AA8FF",
  },
  SlipstreamField: {
    Id: "SlipstreamField",
    Label: "Slipstream Field",
    Subtitle: "Advected slipstream jets, spline corridors & boundary-layer currents",
    Tint: "#3AD69C",
  },
  TurbulenceField: {
    Id: "TurbulenceField",
    Label: "Turbulence Field",
    Subtitle: "Divergence-free 3D curl noise, atmospheric gusts & vorticity",
    Tint: "#E6A23C",
  },
});

const ReachEnvelopes = Object.freeze(["Sphere", "Column", "Box", "Infinite"]);
const AttenuationCurves = Object.freeze(["SmoothStep", "Linear", "InverseSquare", "Constant"]);
const DurationModes = Object.freeze(["Continuous", "Modulated", "Impulse"]);
const MotionPatterns = Object.freeze(["Static", "LinearAB", "SplineCurve", "CircularOrbit", "Figure8"]);
const MotionLoopModes = Object.freeze(["PingPong", "Loop", "Once"]);
const MeterQuantities = Object.freeze(["Speed", "Acceleration", "Curl", "Divergence", "Pressure"]);
const SlicePlanes = Object.freeze(["Top XY", "Front XZ", "Side YZ"]);

const ColourRampModes = Object.freeze(["Engineering", "Turbo", "Plasma", "Diverging", "Viridis", "DomainTint"]);

// CSS gradient previews for the ramp dropdown swatches.
const RampGradients = Object.freeze({
  Engineering: "linear-gradient(90deg,#102a56 0%,#1c7ed6 24%,#22b8cf 46%,#82c91e 66%,#fab005 84%,#f03e3e 100%)",
  Turbo: "linear-gradient(90deg,#30123b 0%,#4685fa 25%,#1ae4b6 50%,#faba39 76%,#7a0403 100%)",
  Plasma: "linear-gradient(90deg,#0d0887 0%,#7e03a8 32%,#cc4778 62%,#f89540 84%,#f0f921 100%)",
  Diverging: "linear-gradient(90deg,#2b6cb0 0%,#63b3ed 28%,#1e2430 50%,#fc8181 72%,#e53e3e 100%)",
  Viridis: "linear-gradient(90deg,#440154 0%,#31688e 35%,#35b779 70%,#fde725 100%)",
  DomainTint: "linear-gradient(90deg,#1e3a5f 0%,#4aa8ff 55%,#b8e1ff 100%)",
});

//------------------------------------------------------------------------------------------------------------------------
//                                                  FORCE LAW DEFINITIONS
//------------------------------------------------------------------------------------------------------------------------

const ForceLawRepository = Object.freeze([
  {
    Ordinal: 0, Id: "LinearWind", Name: "Linear Wind", Domain: "VectorField", Channel: "Stream Velocity",
    Tint: "#4AA8FF", Unit: "m/s",
    Equation: "v(p,t) = S \u00B7 (1 + \u03C3\u00B7z) \u00B7 d\u0302(\u03C8) \u00B7 f(r)",
    Summary: "Directional prevailing airflow with altitude wind-shear and azimuth steering.",
    Defaults: { Strength: 12.5, Radius: 2.2, CoreRadius: 0.48, SwirlRatio: 0.12, Frequency: 0.22, Reach: "Column" },
  },
  {
    Ordinal: 1, Id: "Buoyancy", Name: "Buoyancy", Domain: "VectorField", Channel: "Acceleration",
    Tint: "#52C896", Unit: "m/s\u00B2",
    Equation: "a_z(p) = S \u00B7 ((\u03C1_f \u2212 \u03C1_b)/\u03C1_b) \u00B7 f(r) \u00B7 z\u0302",
    Summary: "Archimedean +Z vertical thermal lift or downdraft column with density lapse.",
    Defaults: { Strength: 11.0, Radius: 1.9, CoreRadius: 0.45, SwirlRatio: 0.15, Frequency: 0.18, Reach: "Column" },
  },
  {
    Ordinal: 2, Id: "RadialBurst", Name: "Radial Burst", Domain: "VectorField", Channel: "Impulse Accel",
    Tint: "#FF8A5B", Unit: "m/s\u00B2",
    Equation: "a(p,t) = S \u00B7 g_pulse(t) \u00B7 f(r) \u00B7 r\u0302",
    Summary: "Spherical outward pressure wave or pulsed shockwave envelope.",
    Defaults: { Strength: 18.0, Radius: 1.85, CoreRadius: 0.36, SwirlRatio: 0.0, Frequency: 0.65, Reach: "Sphere" },
  },
  {
    Ordinal: 3, Id: "Attractor", Name: "Attractor", Domain: "VectorField", Channel: "Acceleration",
    Tint: "#63B3FF", Unit: "m/s\u00B2",
    Equation: "a(p) = \u2212S \u00B7 f(r) \u00B7 r / max(r, r_inner)\u00B3",
    Summary: "Soft inner-radius centripetal gravity well pulling inward without a singularity.",
    Defaults: { Strength: 15.0, Radius: 2.0, CoreRadius: 0.42, SwirlRatio: 0.28, Frequency: 0.2, Reach: "Sphere", Attenuation: "InverseSquare" },
  },
  {
    Ordinal: 4, Id: "Repulsor", Name: "Repulsor", Domain: "VectorField", Channel: "Acceleration",
    Tint: "#F27979", Unit: "m/s\u00B2",
    Equation: "a(p) = +S \u00B7 f(r) \u00B7 r\u0302",
    Summary: "Continuous soft radial exclusion dome deflecting trajectories outward.",
    Defaults: { Strength: 16.0, Radius: 1.95, CoreRadius: 0.4, SwirlRatio: 0.0, Frequency: 0.2, Reach: "Sphere" },
  },
  {
    Ordinal: 5, Id: "AxisAttractor", Name: "Axis Attractor", Domain: "VectorField", Channel: "Acceleration",
    Tint: "#6EC8F5", Unit: "m/s\u00B2",
    Equation: "a(p) = \u2212S \u00B7 f(r_\u22A5) \u00B7 r\u0302_\u22A5 + \u03C9 \u00B7 (\u00E2 \u00D7 r\u0302_\u22A5)",
    Summary: "Cylindrical line-sink drawing stream radially onto the local +Z spine with optional swirl.",
    Defaults: { Strength: 13.5, Radius: 1.95, CoreRadius: 0.38, SwirlRatio: 0.55, Frequency: 0.25, Reach: "Column" },
  },
  {
    Ordinal: 6, Id: "Containment", Name: "Containment", Domain: "VectorField", Channel: "Restoring Accel",
    Tint: "#8C9EFF", Unit: "m/s\u00B2",
    Equation: "a(p) = \u2212S \u00B7 max(0, (r \u2212 r_inner)/(R \u2212 r_inner))\u00B2 \u00B7 r\u0302",
    Summary: "Calm interior dead zone surrounded by a restoring inward boundary shell.",
    Defaults: { Strength: 16.5, Radius: 2.15, CoreRadius: 0.95, SwirlRatio: 0.18, Frequency: 0.2, Reach: "Sphere" },
  },
  {
    Ordinal: 7, Id: "RotationalColumn", Name: "Cyclone Column", Domain: "VectorField", Channel: "Stream + Swirl",
    Tint: "#E8B85B", Unit: "m/s",
    Equation: "v_t = S\u00B7(2\u03C1)/(1+\u03C1\u00B2) + v_r + v_z",
    Summary: "Rankine cyclone funnel with solid-body inner core and 1/r outer decay plus helical updraft.",
    Defaults: { Strength: 18.5, Radius: 2.05, CoreRadius: 0.52, SwirlRatio: 1.2, Frequency: 0.3, Reach: "Sphere" },
  },
  {
    Ordinal: 8, Id: "Torque", Name: "Torque", Domain: "VectorField", Channel: "Angular Spin",
    Tint: "#C58AF9", Unit: "rad/s\u00B2",
    Equation: "\u03C4(p) = S \u00B7 f(r) \u00B7 (\u00E2 \u00D7 r\u0302)",
    Summary: "Pure rotational spin field imparting angular velocity around the local axis.",
    Defaults: { Strength: 14.0, Radius: 1.9, CoreRadius: 0.42, SwirlRatio: 1.35, Frequency: 0.25, Reach: "Sphere" },
  },
  {
    Ordinal: 9, Id: "DragBaffle", Name: "Drag Baffle", Domain: "VectorField", Channel: "Linear Damping",
    Tint: "#78B8C8", Unit: "1/s",
    Equation: "a_drag(p) = \u2212\u00BD \u00B7 C_d \u00B7 S \u00B7 f(r) \u00B7 v\u0302",
    Summary: "Viscous damping medium attenuating kinetic velocity inside the volume.",
    Defaults: { Strength: 10.5, Radius: 2.0, CoreRadius: 0.45, SwirlRatio: 0.0, Frequency: 0.15, Reach: "Box" },
  },
  {
    Ordinal: 10, Id: "CurlNoise", Name: "Curl Noise", Domain: "TurbulenceField", Channel: "\u2207\u00D7\u03A8 Velocity",
    Tint: "#E6A23C", Unit: "m/s",
    Equation: "v(p,t) = S \u00B7 f(r) \u00B7 (\u2207 \u00D7 \u03A8_fbm(p/\u03BB, t))   [\u2207\u00B7v \u2261 0]",
    Summary: "Incompressible divergence-free 3D multi-octave curl-noise eddies.",
    Defaults: { Strength: 14.5, Radius: 2.15, CoreRadius: 0.45, TurbulenceScale: 1.15, Octaves: 3, SwirlRatio: 0.75, Frequency: 0.32, Reach: "Sphere" },
  },
  {
    Ordinal: 11, Id: "AtmosphericGust", Name: "Atmospheric Gust", Domain: "TurbulenceField", Channel: "Gust Velocity",
    Tint: "#F0B45A", Unit: "m/s",
    Equation: "v(p,t) = S \u00B7 (0.6 + 0.4 sin(k\u00B7x \u2212 \u03C9t)) \u00B7 d\u0302 + \u03C3_curl(p,t)",
    Summary: "Travelling sinusoidal gust fronts layered over divergence-free boundary turbulence.",
    Defaults: { Strength: 15.5, Radius: 2.25, CoreRadius: 0.5, TurbulenceScale: 1.4, Octaves: 2, SwirlRatio: 0.35, Frequency: 0.45, Reach: "Box", Duration: "Modulated" },
  },
  {
    Ordinal: 12, Id: "VorticityConfinement", Name: "Vorticity Confinement", Domain: "TurbulenceField", Channel: "Eddy Boost",
    Tint: "#E8914F", Unit: "m/s\u00B2",
    Equation: "f_vc = \u03B5 \u00B7 S \u00B7 (N\u0302 \u00D7 \u03C9),   \u03C9 = \u2207 \u00D7 u",
    Summary: "Re-injects coherent fine-scale rolling eddy rings into sheared wakes.",
    Defaults: { Strength: 15.0, Radius: 2.0, CoreRadius: 0.45, TurbulenceScale: 1.0, Octaves: 3, SwirlRatio: 0.95, Frequency: 0.38, Reach: "Sphere" },
  },
  {
    Ordinal: 13, Id: "BrownianNoise", Name: "Brownian Noise", Domain: "TurbulenceField", Channel: "Stochastic Jitter",
    Tint: "#D9A85B", Unit: "m/s\u00B2",
    Equation: "a(p,t) = S \u00B7 f(r) \u00B7 \u03BE_3D(p/\u03BB + \u03BD\u00B7t)",
    Summary: "Stochastic 3D Langevin micro-jitter for dust motes, embers, and thermal shimmer.",
    Defaults: { Strength: 11.5, Radius: 1.95, CoreRadius: 0.38, TurbulenceScale: 0.8, Octaves: 3, SwirlRatio: 0.4, Frequency: 0.55, Reach: "Sphere" },
  },
  {
    Ordinal: 14, Id: "LorentzField", Name: "Lorentz Field", Domain: "VectorField", Channel: "Cyclotron q(E+v\u00D7B)",
    Tint: "#A78BFA", Unit: "m/s\u00B2",
    Equation: "a(p) = (q/m) \u00B7 (E(p) + u_drift \u00D7 B(p))",
    Summary: "Coupled electric drift and axial magnetic cyclotron gyration.",
    Defaults: { Strength: 16.0, Radius: 2.05, CoreRadius: 0.48, SwirlRatio: 1.05, Frequency: 0.35, Reach: "Column" },
  },
  {
    Ordinal: 15, Id: "MagneticDipole", Name: "Magnetic Dipole", Domain: "VectorField", Channel: "Dipole Flux B(p)",
    Tint: "#C084FC", Unit: "m/s",
    Equation: "B(p) = (3(m\u00B7r\u0302)r\u0302 \u2212 m) / max(r, r_inner)\u00B3",
    Summary: "Toroidal north-south dipole flux lobes curving smoothly from +Z pole to \u2212Z pole.",
    Defaults: { Strength: 15.5, Radius: 2.1, CoreRadius: 0.5, SwirlRatio: 0.35, Frequency: 0.2, Reach: "Sphere" },
  },
  {
    Ordinal: 16, Id: "SlipstreamJet", Name: "Slipstream Jet", Domain: "SlipstreamField", Channel: "Corridor Jet",
    Tint: "#3AD69C", Unit: "m/s",
    Equation: "v(p) = S \u00B7 f(r_\u22A5) \u00B7 t\u0302_corridor + \u03BA_inflow \u00B7 n\u0302_\u22A5",
    Summary: "Directed slipstream jet corridor with lateral entrainment toward the spine axis.",
    Defaults: { Strength: 17.5, Radius: 2.1, CoreRadius: 0.55, SwirlRatio: 0.25, Frequency: 0.25, Reach: "Sphere" },
  },
  {
    Ordinal: 17, Id: "SplineForce", Name: "Spline Force", Domain: "SlipstreamField", Channel: "Spline Guide",
    Tint: "#2DD4BF", Unit: "m/s",
    Equation: "v(p) = S \u00B7 T\u0302(s*) + k_tube \u00B7 (C(s*) \u2212 p)",
    Summary: "Curved S-bend streamline tube guiding transport tangentially with tube centering.",
    Defaults: { Strength: 15.0, Radius: 2.15, CoreRadius: 0.52, SwirlRatio: 0.45, Frequency: 0.28, Reach: "Box" },
  },
  {
    Ordinal: 18, Id: "BoundaryAdhesion", Name: "Boundary Adhesion", Domain: "SlipstreamField", Channel: "Coanda Layer",
    Tint: "#38BDF8", Unit: "m/s",
    Equation: "v(p) = S \u00B7 exp(\u2212d_surf/\u03B4) \u00B7 t\u0302_coanda \u2212 k_adhere \u00B7 n\u0302_surf",
    Summary: "Coanda surface-hugging boundary layer skimming along the floor/hull plane.",
    Defaults: { Strength: 13.0, Radius: 2.1, CoreRadius: 0.45, SwirlRatio: 0.2, Frequency: 0.22, Reach: "Box" },
  },
]);

const ForceLawIndex = new Map(ForceLawRepository.map((Law) => [Law.Id, Law]));

function GetForceLawMeta(LawIdOrOrdinal) {
  if (typeof LawIdOrOrdinal === "number") {
    return ForceLawRepository[Math.max(0, Math.min(ForceLawRepository.length - 1, LawIdOrOrdinal | 0))];
  }
  return ForceLawIndex.get(LawIdOrOrdinal) || ForceLawRepository[0];
}

//------------------------------------------------------------------------------------------------------------------------
//                                              CURATED ENGINE FORCE PRESETS
//------------------------------------------------------------------------------------------------------------------------

const ForcePresetRepository = Object.freeze([
  {
    Id: "force-preset-coastal-crosswind", Name: "Coastal Crosswind", LawId: "LinearWind",
    Summary: "Steady 14.5 m/s maritime crosswind with gentle altitude shear.",
    Force: { Model: "LinearWind", Strength: 14.5, Radius: 2.4, CoreRadius: 0.55, YawDeg: 38, SwirlRatio: 0.14, Reach: "Column" },
  },
  {
    Id: "force-preset-thermal-updraft", Name: "Thermal Updraft", LawId: "Buoyancy",
    Summary: "Rising +Z convective column with warm inner buoyancy.",
    Force: { Model: "Buoyancy", Strength: 13.0, Radius: 2.0, CoreRadius: 0.5, SwirlRatio: 0.22, Reach: "Column" },
  },
  {
    Id: "force-preset-cyclone-funnel", Name: "Cyclone Funnel", LawId: "RotationalColumn",
    Summary: "Rankine cyclone column with tight inner eyewall and helical updraft.",
    Force: { Model: "RotationalColumn", Strength: 19.5, Radius: 2.2, CoreRadius: 0.52, SwirlRatio: 1.35, Reach: "Sphere" },
  },
  {
    Id: "force-preset-magnetic-containment", Name: "Magnetic Containment", LawId: "Containment",
    Summary: "Calm inner chamber bounded by a smooth restoring shell.",
    Force: { Model: "Containment", Strength: 17.5, Radius: 2.3, CoreRadius: 0.95, SwirlRatio: 0.28, Reach: "Sphere" },
  },
  {
    Id: "force-preset-curlnoise-wake", Name: "CurlNoise Wake", LawId: "CurlNoise",
    Summary: "3-octave divergence-free turbulent wake field (div v = 0).",
    Force: { Model: "CurlNoise", Strength: 15.0, Radius: 2.3, CoreRadius: 0.48, TurbulenceScale: 1.15, Octaves: 3, Reach: "Sphere" },
  },
  {
    Id: "force-preset-orbital-attractor", Name: "Orbital Attractor", LawId: "Attractor",
    Summary: "Soft inner-radius inverse-square gravity sink with tangential swirl.",
    Force: { Model: "Attractor", Strength: 16.5, Radius: 2.1, CoreRadius: 0.45, SwirlRatio: 0.45, Attenuation: "InverseSquare", Reach: "Sphere" },
  },
  {
    Id: "force-preset-radial-shockwave", Name: "Radial Shockwave", LawId: "RadialBurst",
    Summary: "Periodic outward pressure pulse envelope.",
    Force: { Model: "RadialBurst", Strength: 21.0, Radius: 2.0, CoreRadius: 0.38, Duration: "Impulse", Frequency: 0.6, Reach: "Sphere" },
  },
  {
    Id: "force-preset-atmospheric-gust", Name: "Atmospheric Gust", LawId: "AtmosphericGust",
    Summary: "Modulated gust fronts with layered curl-noise shear.",
    Force: { Model: "AtmosphericGust", Strength: 16.0, Radius: 2.4, CoreRadius: 0.5, Duration: "Modulated", Frequency: 0.42, Reach: "Box" },
  },
  {
    Id: "force-preset-lorentz-plasma", Name: "Lorentz Plasma Field", LawId: "LorentzField",
    Summary: "Helical cyclotron gyration under crossed E and B fields.",
    Force: { Model: "LorentzField", Strength: 17.0, Radius: 2.1, CoreRadius: 0.48, SwirlRatio: 1.15, Reach: "Column" },
  },
  {
    Id: "force-preset-magnetic-dipole", Name: "Magnetic Dipole Torus", LawId: "MagneticDipole",
    Summary: "Closed dipolar flux loops arcing from +Z to \u2212Z pole.",
    Force: { Model: "MagneticDipole", Strength: 16.0, Radius: 2.2, CoreRadius: 0.5, Reach: "Sphere" },
  },
  {
    Id: "force-preset-slipstream-jet", Name: "Slipstream Jet", LawId: "SlipstreamJet",
    Summary: "High-speed directed jet tunnel with lateral entrainment.",
    Force: { Model: "SlipstreamJet", Strength: 18.0, Radius: 2.2, CoreRadius: 0.55, YawDeg: 180, Reach: "Sphere" },
  },
  {
    Id: "force-preset-spline-bend-guide", Name: "Spline Bend Guide", LawId: "SplineForce",
    Summary: "S-bend streamline tube guiding transport through a curved corridor.",
    Force: { Model: "SplineForce", Strength: 16.0, Radius: 2.2, CoreRadius: 0.52, SwirlRatio: 0.5, Reach: "Box" },
  },
]);

//------------------------------------------------------------------------------------------------------------------------
//                                                  VALUE RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

function ClampNumber(Scalar, Min, Max, Fallback) {
  const N = Number(Scalar);
  if (!Number.isFinite(N)) return Fallback;
  return Math.max(Min, Math.min(Max, N));
}

// Resolve a raw serialized generator payload onto the complete canonical parameter set for its law.
function ResolveForceValues(Raw = {}, FallbackLawId = "LinearWind") {
  const Src = Raw && typeof Raw === "object" ? Raw : {};
  const Meta = GetForceLawMeta(Src.Model || FallbackLawId);
  const Def = Meta.Defaults;
  const Waypoints = Array.isArray(Src.MotionWaypoints)
    ? Src.MotionWaypoints
        .filter((W) => Array.isArray(W) && W.length >= 3)
        .map((W) => [Number(W[0]) || 0, Number(W[1]) || 0, Number(W[2]) || 0])
    : [];
  return {
    Model: Meta.Id,
    Domain: Meta.Domain,
    Enabled: Src.Enabled !== false,
    Muted: Src.Muted === true,
    Tint: typeof Src.Tint === "string" && /^#[0-9a-fA-F]{6}$/.test(Src.Tint) ? Src.Tint : null,
    Strength: ClampNumber(Src.Strength, -80, 80, Def.Strength ?? 14.0),
    Radius: ClampNumber(Src.Radius, 0.35, 250.0, Def.Radius ?? 2.1),
    CoreRadius: ClampNumber(Src.CoreRadius, 0.08, 120.0, Def.CoreRadius ?? 0.48),
    FalloffExponent: ClampNumber(Src.FalloffExponent, 0.2, 6.0, 1.35),
    Attenuation: AttenuationCurves.includes(Src.Attenuation) ? Src.Attenuation : Def.Attenuation || "SmoothStep",
    Reach: ReachEnvelopes.includes(Src.Reach) ? Src.Reach : Def.Reach || "Sphere",
    Duration: DurationModes.includes(Src.Duration) ? Src.Duration : Def.Duration || "Continuous",
    SwirlRatio: ClampNumber(Src.SwirlRatio, -3.0, 3.0, Def.SwirlRatio ?? 0.25),
    TurbulenceScale: ClampNumber(Src.TurbulenceScale, 0.15, 60.0, Def.TurbulenceScale ?? 1.2),
    Octaves: Math.round(ClampNumber(Src.Octaves, 1, 5, Def.Octaves ?? 3)),
    Frequency: ClampNumber(Src.Frequency, 0.02, 5.0, Def.Frequency ?? 0.28),
    YawDeg: ClampNumber(Src.YawDeg, -360, 360, 25),
    PitchDeg: ClampNumber(Src.PitchDeg, -90, 90, 0),
    MotionPattern: MotionPatterns.includes(Src.MotionPattern) ? Src.MotionPattern : "Static",
    MotionLoopMode: MotionLoopModes.includes(Src.MotionLoopMode) ? Src.MotionLoopMode : "PingPong",
    MotionAmplitude: ClampNumber(Src.MotionAmplitude, 0.0, 25.0, 0.85),
    MotionSpeedHz: ClampNumber(Src.MotionSpeedHz, 0.02, 4.0, 0.22),
    MotionPhase: ClampNumber(Src.MotionPhase, 0.0, 1.0, 0.0),
    MotionWaypoints: Waypoints,
  };
}

// Deterministic 4-waypoint closed loop around a base centre for SplineCurve / LinearAB motion patterns.
function EnsureMotionWaypoints(BaseCenter = [0, 0, 0.6], Force = {}, TargetPattern = "SplineCurve") {
  const Cx = Number(BaseCenter[0]) || 0;
  const Cy = Number(BaseCenter[1]) || 0;
  const Cz = Number(BaseCenter[2]) || 0.6;
  const Span = Math.max(0.45, Math.min(1.6, (Force.Radius || 1.8) * 0.55));
  const Seeds = [
    [-1.0, 0.35, 0.45],
    [0.15, 1.0, -0.25],
    [1.0, -0.3, 0.3],
    [-0.25, -1.0, -0.4],
  ];
  const Waypoints = Seeds.map((Seed) => [Cx + Seed[0] * Span, Cy + Seed[1] * Span, Cz + Seed[2] * Span * 0.6]);
  return TargetPattern === "LinearAB" ? [Waypoints[0], Waypoints[2]] : Waypoints;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   LAW ICON PATHS
//------------------------------------------------------------------------------------------------------------------------

// Bespoke 16x16 stroke icon paths, one per canonical force law (rendered as inline SVG in the registry & drawer).
const ForceLawIconPaths = Object.freeze({
  LinearWind:
    '<path d="M2 6h8.5a2 2 0 1 0-1.9-2.6M2 10h11a1.8 1.8 0 1 1-1.6 2.5M2 13h6.5a1.5 1.5 0 1 1-1.3 2.2"/>',
  Buoyancy:
    '<path d="M8 14V3M4.8 6.2L8 3l3.2 3.2"/><circle cx="8" cy="11.5" r="1.7"/>',
  RadialBurst:
    '<circle cx="8" cy="8" r="2.2"/><circle cx="8" cy="8" r="5.5" stroke-dasharray="2.2 2"/><path d="M8 1v2M8 13v2M1 8h2M13 8h2"/>',
  Attractor:
    '<circle cx="8" cy="8" r="1.8"/><path d="M2.5 2.5L5.8 5.8M13.5 2.5l-3.3 3.3M2.5 13.5l3.3-3.3M13.5 13.5l-3.3-3.3"/>',
  Repulsor:
    '<circle cx="8" cy="8" r="1.8"/><path d="M5.5 5.5L2.2 2.2M10.5 5.5l3.3-3.3M5.5 10.5l-3.3-3.3M10.5 10.5l3.3 3.3M2.2 4.5V2.2h2.3M13.8 4.5V2.2h-2.3"/>',
  AxisAttractor:
    '<path d="M3 13L13 3"/><path d="M4.5 5.5l2.5 2.5M11.5 10.5L9 8"/>',
  Containment:
    '<circle cx="8" cy="8" r="6"/><circle cx="8" cy="8" r="3" stroke-dasharray="1.8 1.8"/><path d="M8 2v2M8 12v2M2 8h2M12 8h2"/>',
  RotationalColumn:
    '<path d="M8 8a1.2 1.2 0 1 1-1.2-1.2 2.4 2.4 0 0 1 2.4 2.4A3.8 3.8 0 0 1 4 8a5.2 5.2 0 0 1 10.2-1"/>',
  Torque:
    '<path d="M13.5 8a5.5 5.5 0 1 1-2.2-4.4"/><path d="M11 1.5l.6 2.3-2.3.6"/>',
  DragBaffle:
    '<path d="M2 5h9M2 8h11M2 11h9"/><path d="M13 5l2 1.6M13 11l2-1.6"/>',
  CurlNoise:
    '<path d="M3 11c1.5-4 3.5-4 5-1s3.5 3 5-1"/><path d="M3 6c1.5-4 3.5-4 5-1s3.5 3 5-1" opacity="0.55"/>',
  AtmosphericGust:
    '<path d="M2 7h10a1.6 1.6 0 1 0-1.5-2.1M2 11h7a1.3 1.3 0 1 1-1.2 1.7"/><path d="M12.5 10.5l2 1.2-2 1.2"/>',
  VorticityConfinement:
    '<circle cx="8" cy="8" r="2"/><circle cx="8" cy="8" r="4.6" stroke-dasharray="1.6 1.9"/><path d="M12.5 3.5l2 2M12.5 12.5l2-2"/>',
  BrownianNoise:
    '<path d="M3 12l2-3 2 2 2-4 2 3 2-5"/><circle cx="3" cy="12" r="0.9"/><circle cx="13" cy="5" r="0.9"/>',
  LorentzField:
    '<circle cx="8" cy="8" r="5.6"/><path d="M5 10.5V6a3 3 0 0 1 6 0v4.5"/>',
  MagneticDipole:
    '<path d="M5.5 13V6a2.5 2.5 0 0 1 5 0v7"/><path d="M5.5 13c0 1.4 5 1.4 5 0"/><path d="M4 4l1.5 1.5M12 4l-1.5 1.5"/>',
  SlipstreamJet:
    '<path d="M2 8h7"/><path d="M9 5.5L13 8l-4 2.5"/><path d="M2 4.5h4M2 11.5h4" opacity="0.6"/>',
  SplineForce:
    '<path d="M2 12c3.5 0 3-8 6.5-8S13 11 14 6"/><circle cx="2" cy="12" r="1"/><circle cx="14" cy="6" r="1"/>',
  BoundaryAdhesion:
    '<path d="M2 13h12"/><path d="M3 13c1.5-4.5 4-6 7-5.5"/><path d="M10 4.5l2.4 1.6-2.6 1.4"/>',
});

//------------------------------------------------------------------------------------------------------------------------
//                                                  DEFAULT DIAGNOSTIC RIG
//------------------------------------------------------------------------------------------------------------------------

// Canonical 3-source wind-tunnel diagnostic rig restored as the studio baseline scene.
function ConstructDefaultForceRig() {
  return [
    {
      Id: "force-helical-cyclone",
      Name: "Helical Cyclone Path",
      Position: [-0.68, 0.22, 0.18],
      Rotation: [0, 0, 0],
      Force: ResolveForceValues({
        Model: "RotationalColumn",
        Strength: 22.0,
        Radius: 1.28,
        CoreRadius: 0.36,
        SwirlRatio: 1.28,
        Reach: "Sphere",
        Attenuation: "SmoothStep",
        MotionPattern: "SplineCurve",
        MotionLoopMode: "PingPong",
        MotionSpeedHz: 0.16,
        MotionAmplitude: 0.85,
        MotionWaypoints: [
          [-1.18, 0.04, 0.16],
          [-0.55, 0.28, 0.34],
          [0.14, 0.44, 0.42],
          [0.44, -0.18, 0.14],
        ],
      }),
    },
    {
      Id: "force-cantilever-jet",
      Name: "Cantilever Jet Plume",
      Position: [0.82, 0.36, 0.16],
      Rotation: [0, 0, 0],
      Force: ResolveForceValues({
        Model: "SlipstreamJet",
        Strength: 21.0,
        Radius: 1.22,
        CoreRadius: 0.38,
        YawDeg: 192,
        SwirlRatio: 0.24,
        Reach: "Sphere",
        Attenuation: "SmoothStep",
      }),
    },
    {
      Id: "force-orbital-burst",
      Name: "Orbital Radial Burst",
      Position: [0.1, 0.08, 0.12],
      Rotation: [0, 0, 0],
      Force: ResolveForceValues({
        Model: "RadialBurst",
        Strength: 17.5,
        Radius: 1.12,
        CoreRadius: 0.3,
        Frequency: 0.45,
        Reach: "Sphere",
        Attenuation: "SmoothStep",
        Duration: "Continuous",
      }),
    },
  ];
}
