/*============================================================================================================================================
                                                        FORCEFIELDCATALOG.JS
============================================================================================================================================*/
// Canonical 19-Law C++ ForceModel & VectorField Studio runtime for FrontierWorldMirror (+Z Up, zero rigid-body physics).
// Unifies Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h (Ordinals 0..18), SpaceContainerCodec (.ForceModel / .VectorFieldSpace),
// Experimental/VectorFieldVisualizer (Composite Iso-Contour Heatmap Slices, Bidirectional RK4 Streamline Ribbons, Animated Bokeh DOF Ribbons,
// Fresnel Volumetric Reach Shells, 3D Lattice Arrows, Beaufort Anemometer, Differential Curl/Divergence Probe, Waypoint Splines, and Cascade TOML).

import { CompileFieldProgram, FORMULA_PRESETS, DEFAULT_FORMULA_CODE } from "./VectorFieldLang.js";

export { FORMULA_PRESETS, DEFAULT_FORMULA_CODE };

const DEG_TO_RAD = Math.PI / 180.0;
const TWO_PI = Math.PI * 2.0;

export const ForceDomains = Object.freeze({
  VectorField: {
    Id: "VectorField",
    Label: "VectorField",
    Subtitle: "Analytical directional, radial, orbital & electromagnetic force laws",
    Color: "#4AA8FF",
  },
  FlowField: {
    Id: "FlowField",
    Label: "FlowField",
    Subtitle: "Advected slipstreams, spline corridors & boundary-layer currents",
    Color: "#3AD69C",
  },
  TurbulenceField: {
    Id: "TurbulenceField",
    Label: "TurbulenceField",
    Subtitle: "Divergence-free 3D curl noise, atmospheric gusts & vorticity",
    Color: "#E6A23C",
  },
});

export const ReachEnvelopes = ["Sphere", "Column", "Box", "Infinite"];
export const AttenuationCurves = ["SmoothStep", "Linear", "InverseSquare", "Constant"];
export const DurationModes = ["Continuous", "Modulated", "Impulse"];
export const MotionTopologies = ["Static", "LinearAB", "SplineCurve", "CircularOrbit", "Figure8"];
export const MotionLoopModes = ["PingPong", "Loop", "Once"];
export const EvaluatedQuantities = ["Speed", "Acceleration", "Curl", "Divergence", "Pressure"];
export const FieldColormaps = ["Engineering", "Turbo", "Plasma", "Diverging"];
export const ColorRampModes = ["Engineering", "Turbo", "Plasma", "Diverging", "Beaufort", "Viridis", "DomainTint"];
export const StreamColorModes = ["FieldPalette", "CyanSingle", "CustomSingle", "Elevation"];
export const BokehPaletteModes = ["BokehTeal", "FollowStreams"];

export const STREAM_COLOR_PRESETS = Object.freeze([
  { Color: "#68dcff", Label: "Classic Cyan" },
  { Color: "#84c7ff", Label: "Ice Blue" },
  { Color: "#54e6a4", Label: "Emerald" },
  { Color: "#ffc857", Label: "Amber Gold" },
  { Color: "#ff7461", Label: "Coral Glow" },
]);

export const COLORMAP_CSS_GRADIENTS = Object.freeze({
  Engineering: "linear-gradient(90deg, #102a56 0%, #1c7ed6 24%, #22b8cf 46%, #82c91e 66%, #fab005 84%, #f03e3e 100%)",
  Beaufort: "linear-gradient(90deg, #102a56 0%, #1c7ed6 24%, #22b8cf 46%, #82c91e 66%, #fab005 84%, #f03e3e 100%)",
  Turbo: "linear-gradient(90deg, #30123b 0%, #4685fa 25%, #1ae4b6 50%, #faba39 76%, #7a0403 100%)",
  Plasma: "linear-gradient(90deg, #0d0887 0%, #7e03a8 32%, #cc4778 62%, #f89540 84%, #f0f921 100%)",
  Viridis: "linear-gradient(90deg, #440154 0%, #31688e 35%, #35b779 70%, #fde725 100%)",
  Diverging: "linear-gradient(90deg, #2b6cb0 0%, #63b3ed 28%, #1e2430 50%, #fc8181 72%, #e53e3e 100%)",
  DomainTint: "linear-gradient(90deg, #1e3a5f 0%, #4aa8ff 55%, #b8e1ff 100%)",
});

export const DEFAULT_FORCE_OVERLAY = Object.freeze({
  Enabled: true,
  SimPlaying: true,
  ShowHeatmap: true,
  SliceEnabled: true,
  ShowEnvelopes: true,
  ShowVelocityArrows: true,
  ShowStreamlines: true,
  ShowBokehRibbons: true,
  ShowMotionPaths: true,
  ShowProbeHud: true,
  Quantity: "Speed",
  Colormap: "Engineering",
  ColorRamp: "Engineering",
  SliceAxis: "XY", // XY = horizontal ground-parallel in +Z-up world; XZ = front vertical; YZ = side vertical; ALL = 3 orthogonal planes
  SliceOffset: 0.0,
  FixedMax: 24.0,
  HeatmapOpacity: 0.62,
  ArrowDensity: 4,
  ArrowSpacing: 0.55,
  VolumeLayers: 1,
  VectorScale: 0.26,
  StreamColorMode: "CyanSingle",
  StreamSingleColor: "#68dcff",
  StreamThickness: 0.78,
  StreamEmissive: 1.05,
  StreamOpacity: 0.92,
  StreamResolution: 28,
  StreamLineCount: 96,
  BokehPalette: "BokehTeal",
  BokehCount: 120,
  BokehSpeed: 0.55,
  BokehThickness: 1.0,
  BokehEmissive: 1.05,
  ProbePosition: [0.35, 0.25, 0.75],
});

// Bespoke 16x16 SVG stroke paths for all 19 canonical C++ ForceModel laws
export const FORCE_MODEL_SVG_PATHS = Object.freeze({
  LinearWind:
    '<path d="M2 6h8.5a2 2 0 1 0-1.9-2.6M2 10h11a1.8 1.8 0 1 1-1.6 2.5M2 13h6.5a1.5 1.5 0 1 1-1.3 2.2"/>',
  Buoyancy:
    '<path d="M8 14V3M4.8 6.2L8 3l3.2 3.2"/><circle cx="8" cy="11.5" r="1.7"/>',
  RadialBurst:
    '<circle cx="8" cy="8" r="2.2"/><circle cx="8" cy="8" r="5.5" stroke-dasharray="2.2 2"/><path d="M8 1v2M8 13v2M1 8h2M13 8h2"/>',
  Attractor:
    '<circle cx="8" cy="8" r="1.8"/><path d="M2.5 2.5L5.8 5.8M13.5 2.5l-3.3 3.3M2.5 13.5l3.3-3.3M13.5 13.5l-3.3-3.3"/>',
  Repulsor:
    '<circle cx="8" cy="8" r="1.8"/><path d="M5.5 5.5L2.2 2.2M10.5 5.5l3.3-3.3M5.5 10.5l-3.3 3.3M10.5 10.5l3.3 3.3M2.2 4.5V2.2h2.3M13.8 4.5V2.2h-2.3"/>',
  AxisAttractor:
    '<path d="M3 13L13 3"/><path d="M4.5 5.5l2.5 2.5M11.5 10.5L9 8"/>',
  Containment:
    '<circle cx="8" cy="8" r="6"/><circle cx="8" cy="8" r="3" stroke-dasharray="1.8 1.8"/><path d="M8 2v2M8 12v2M2 8h2M12 8h2"/>',
  RotationalColumn:
    '<path d="M8 8a1.2 1.2 0 1 1-1.2-1.2 2.4 2.4 0 0 1 2.4 2.4A3.8 3.8 0 0 1 4 8a5.2 5.2 0 0 1 10.2-1"/>',
  Torque:
    '<path d="M12.8 6.2A5.2 5.2 0 1 0 11.5 12"/><path d="M10.2 6.5h3.2V3.3"/><circle cx="8" cy="8" r="1.4"/>',
  Drag:
    '<path d="M5.5 3v10M10.5 3v10M2 8h3.5M14 8h-3.5"/>',
  CurlNoise:
    '<path d="M2.5 7.5a2.8 2.8 0 1 1 5.2 1.4 2.8 2.8 0 1 0 5.6 1"/><path d="M11.8 5.2a2 2 0 0 0-3-.2"/>',
  AtmosphericGust:
    '<path d="M1.8 7h9.4a2.1 2.1 0 1 0-2-2.7M3.5 10.5h9.5a1.7 1.7 0 1 1-1.6 2.3"/><path d="M2 3.8l2.2 1.2L2 6.2"/>',
  VorticityConfinement:
    '<circle cx="5.5" cy="8" r="2.6"/><circle cx="10.8" cy="8" r="2.6"/><path d="M5.5 5.4l1.4-1.2M10.8 10.6l-1.4 1.2"/>',
  BrownianNoise:
    '<path d="M2.5 11.5L5 7l2.2 3.2L10 4.5l2 4 2-2.5"/><circle cx="2.5" cy="11.5" r="1"/><circle cx="14" cy="6" r="1"/>',
  LorentzField:
    '<path d="M2.5 9.5c1.5-4.5 3.5-4.5 4.5 0s3 4.5 4.5 0 1.8-4.5 2.5 0"/><path d="M2 4h12M11.8 2.2L14 4l-2.2 1.8"/>',
  MagneticDipole:
    '<path d="M6.5 4.5C3 4.5 2 8 2 8s1 3.5 4.5 3.5M9.5 4.5C13 4.5 14 8 14 8s-1 3.5-4.5 3.5"/><rect x="6.6" y="4.5" width="2.8" height="7" rx="1"/>',
  FlowStream:
    '<path d="M2 5c3-2 6 2 9 0l2-1M2 9c3-2 6 2 9 0l2-1M2 13c3-2 6 2 9 0l2-1"/><path d="M11.5 3.2L14 4l-.8 2.4"/>',
  SplineForce:
    '<path d="M2 12.5C5 12.5 5 3.5 8 3.5s3 9 6 9"/><path d="M12.2 10.5L14 12.5l-2 1.8"/>',
  BoundaryAdhesion:
    '<path d="M2 12.5h12"/><path d="M4 5c2.5 4.5 5.5 4.5 8 0"/><path d="M8 3.5v6.5M6 8l2 2 2-2"/>',
});

// Canonical 19 ForceModel Laws matching ForceFieldDescriptors.h (Ordinals 0..18)
export const FORCE_MODELS = Object.freeze([
  {
    Ordinal: 0,
    Id: "LinearWind",
    Name: "Linear Wind",
    Domain: "VectorField",
    Channel: "Flow Velocity",
    Color: "#4AA8FF",
    Unit: "m/s",
    Equation: "v(p,t) = S · (1 + σ·z) · d̂(ψ) · f(r)",
    Summary: "Directional prevailing airflow with altitude wind-shear and directional azimuth.",
    Defaults: { Strength: 12.5, Radius: 2.2, CoreRadius: 0.48, SwirlRatio: 0.12, Frequency: 0.22, Reach: "Column" },
  },
  {
    Ordinal: 1,
    Id: "Buoyancy",
    Name: "Buoyancy",
    Domain: "VectorField",
    Channel: "Acceleration",
    Color: "#52C896",
    Unit: "m/s²",
    Equation: "a_z(p) = S · ((ρ_f - ρ_b)/ρ_b) · f(r) · ẑ",
    Summary: "Archimedean +Z vertical thermal lift or downdraft column with density lapse.",
    Defaults: { Strength: 11.0, Radius: 1.9, CoreRadius: 0.45, SwirlRatio: 0.15, Frequency: 0.18, Reach: "Column" },
  },
  {
    Ordinal: 2,
    Id: "RadialBurst",
    Name: "Radial Burst",
    Domain: "VectorField",
    Channel: "Impulse Accel",
    Color: "#FF8A5B",
    Unit: "m/s²",
    Equation: "a(p,t) = S · g_imp(t) · f(r) · r̂",
    Summary: "Spherical outward pressure wave or pulsed shockwave envelope.",
    Defaults: { Strength: 18.0, Radius: 1.85, CoreRadius: 0.36, SwirlRatio: 0.0, Frequency: 0.65, Reach: "Sphere", Duration: "Continuous" },
  },
  {
    Ordinal: 3,
    Id: "Attractor",
    Name: "Attractor",
    Domain: "VectorField",
    Channel: "Acceleration",
    Color: "#63B3FF",
    Unit: "m/s²",
    Equation: "a(p) = -S · f(r) · r / max(r, r_core)³",
    Summary: "Soft-core centripetal gravity well pulling inward without central singularity.",
    Defaults: { Strength: 15.0, Radius: 2.0, CoreRadius: 0.42, SwirlRatio: 0.28, Frequency: 0.2, Reach: "Sphere", Attenuation: "InverseSquare" },
  },
  {
    Ordinal: 4,
    Id: "Repulsor",
    Name: "Repulsor",
    Domain: "VectorField",
    Channel: "Acceleration",
    Color: "#F27979",
    Unit: "m/s²",
    Equation: "a(p) = +S · f(r) · r̂",
    Summary: "Continuous soft-core radial exclusion dome deflecting trajectories outward.",
    Defaults: { Strength: 16.0, Radius: 1.95, CoreRadius: 0.4, SwirlRatio: 0.0, Frequency: 0.2, Reach: "Sphere" },
  },
  {
    Ordinal: 5,
    Id: "AxisAttractor",
    Name: "Axis Attractor",
    Domain: "VectorField",
    Channel: "Acceleration",
    Color: "#6EC8F5",
    Unit: "m/s²",
    Equation: "a(p) = -S · f(r_⊥) · r̂_⊥ + ω · (â × r̂_⊥)",
    Summary: "Cylindrical line-sink drawing flow radially onto the local +Z spine with optional swirl.",
    Defaults: { Strength: 13.5, Radius: 1.95, CoreRadius: 0.38, SwirlRatio: 0.55, Frequency: 0.25, Reach: "Column" },
  },
  {
    Ordinal: 6,
    Id: "Containment",
    Name: "Containment",
    Domain: "VectorField",
    Channel: "Restoring Accel",
    Color: "#8C9EFF",
    Unit: "m/s²",
    Equation: "a(p) = -S · max(0, (r - r_inner)/(R - r_inner))² · r̂",
    Summary: "Calm interior dead-zone surrounded by a restoring inward boundary shell.",
    Defaults: { Strength: 16.5, Radius: 2.15, CoreRadius: 0.95, SwirlRatio: 0.18, Frequency: 0.2, Reach: "Sphere" },
  },
  {
    Ordinal: 7,
    Id: "RotationalColumn",
    Name: "Vortex Column",
    Domain: "VectorField",
    Channel: "Flow + Swirl",
    Color: "#E8B85B",
    Unit: "m/s",
    Equation: "v_t = S·(2ρ)/(1+ρ²) + v_r + v_z",
    Summary: "Rankine cyclone / vortex funnel with solid-body inner core and 1/r outer decay.",
    Defaults: { Strength: 18.5, Radius: 2.05, CoreRadius: 0.52, SwirlRatio: 1.2, Frequency: 0.3, Reach: "Sphere" },
  },
  {
    Ordinal: 8,
    Id: "Torque",
    Name: "Torque",
    Domain: "VectorField",
    Channel: "Angular Spin",
    Color: "#C58AF9",
    Unit: "rad/s²",
    Equation: "τ(p) = S · f(r) · (â × r̂)",
    Summary: "Pure rotational spin field imparting angular velocity around the local axis.",
    Defaults: { Strength: 14.0, Radius: 1.9, CoreRadius: 0.42, SwirlRatio: 1.35, Frequency: 0.25, Reach: "Sphere" },
  },
  {
    Ordinal: 9,
    Id: "Drag",
    Name: "Drag Baffle",
    Domain: "VectorField",
    Channel: "Linear Damping",
    Color: "#78B8C8",
    Unit: "1/s",
    Equation: "a_drag(p) = -½ · C_d · S · f(r) · v̂",
    Summary: "Viscous damping medium attenuating kinetic velocity inside the volume.",
    Defaults: { Strength: 10.5, Radius: 2.0, CoreRadius: 0.45, SwirlRatio: 0.0, Frequency: 0.15, Reach: "Box" },
  },
  {
    Ordinal: 10,
    Id: "CurlNoise",
    Name: "Curl Noise",
    Domain: "TurbulenceField",
    Channel: "∇×Ψ Velocity",
    Color: "#E6A23C",
    Unit: "m/s",
    Equation: "v(p,t) = S · f(r) · (∇ × Ψ_fbm(p/λ, t))   [∇·v ≡ 0]",
    Summary: "Incompressible divergence-free 3D multi-octave curl-noise eddies.",
    Defaults: { Strength: 14.5, Radius: 2.15, CoreRadius: 0.45, TurbulenceScale: 1.15, Octaves: 3, SwirlRatio: 0.75, Frequency: 0.32, Reach: "Sphere" },
  },
  {
    Ordinal: 11,
    Id: "AtmosphericGust",
    Name: "Atmospheric Gust",
    Domain: "TurbulenceField",
    Channel: "Gust Velocity",
    Color: "#F0B45A",
    Unit: "m/s",
    Equation: "v(p,t) = S · (0.6 + 0.4 sin(k·x - ωt)) · d̂ + σ_curl(p,t)",
    Summary: "Traveling sinusoidal gust fronts layered over divergence-free boundary turbulence.",
    Defaults: { Strength: 15.5, Radius: 2.25, CoreRadius: 0.5, TurbulenceScale: 1.4, Octaves: 2, SwirlRatio: 0.35, Frequency: 0.45, Reach: "Box", Duration: "Modulated" },
  },
  {
    Ordinal: 12,
    Id: "VorticityConfinement",
    Name: "Vorticity Confinement",
    Domain: "TurbulenceField",
    Channel: "Eddy Boost",
    Color: "#E8914F",
    Unit: "m/s²",
    Equation: "f_vc = ε · S · (N̂ × ω),   ω = ∇ × u",
    Summary: "Re-injects coherent fine-scale rolling vortex rings into sheared flow wakes.",
    Defaults: { Strength: 15.0, Radius: 2.0, CoreRadius: 0.45, TurbulenceScale: 1.0, Octaves: 3, SwirlRatio: 0.95, Frequency: 0.38, Reach: "Sphere" },
  },
  {
    Ordinal: 13,
    Id: "BrownianNoise",
    Name: "Brownian Noise",
    Domain: "TurbulenceField",
    Channel: "Stochastic Jitter",
    Color: "#D9A85B",
    Unit: "m/s²",
    Equation: "a(p,t) = S · f(r) · ξ_3D(p/λ + ν·t)",
    Summary: "Stochastic 3D Langevin micro-jitter for dust motes, embers, and thermal shimmer.",
    Defaults: { Strength: 11.5, Radius: 1.95, CoreRadius: 0.38, TurbulenceScale: 0.8, Octaves: 3, SwirlRatio: 0.4, Frequency: 0.55, Reach: "Sphere" },
  },
  {
    Ordinal: 14,
    Id: "LorentzField",
    Name: "Lorentz Field",
    Domain: "VectorField",
    Channel: "Cyclotron q(E+v×B)",
    Color: "#A78BFA",
    Unit: "m/s²",
    Equation: "a(p) = (q/m) · (E(p) + u_drift × B(p))",
    Summary: "Coupled electric drift and axial magnetic cyclotron gyration.",
    Defaults: { Strength: 16.0, Radius: 2.05, CoreRadius: 0.48, SwirlRatio: 1.05, Frequency: 0.35, Reach: "Column" },
  },
  {
    Ordinal: 15,
    Id: "MagneticDipole",
    Name: "Magnetic Dipole",
    Domain: "VectorField",
    Channel: "Dipole Flux B(p)",
    Color: "#C084FC",
    Unit: "m/s",
    Equation: "B(p) = (3(m·r̂)r̂ - m) / max(r, r_core)³",
    Summary: "Toroidal north-south dipole flux lobes curving smoothly from +Z pole to -Z pole.",
    Defaults: { Strength: 15.5, Radius: 2.1, CoreRadius: 0.5, SwirlRatio: 0.35, Frequency: 0.2, Reach: "Sphere" },
  },
  {
    Ordinal: 16,
    Id: "FlowStream",
    Name: "Flow Stream",
    Domain: "FlowField",
    Channel: "Corridor Jet",
    Color: "#3AD69C",
    Unit: "m/s",
    Equation: "v(p) = S · f(r_⊥) · t̂_corridor + κ_inflow · n̂_⊥",
    Summary: "Directed slipstream jet corridor with lateral entrainment toward the core axis.",
    Defaults: { Strength: 17.5, Radius: 2.1, CoreRadius: 0.55, SwirlRatio: 0.25, Frequency: 0.25, Reach: "Sphere" },
  },
  {
    Ordinal: 17,
    Id: "SplineForce",
    Name: "Spline Force",
    Domain: "FlowField",
    Channel: "Spline Guide",
    Color: "#2DD4BF",
    Unit: "m/s",
    Equation: "v(p) = S · T̂(s*) + k_tube · (C(s*) - p)",
    Summary: "Curved S-bend streamline tube guiding flow tangentially with tube centering.",
    Defaults: { Strength: 15.0, Radius: 2.15, CoreRadius: 0.52, SwirlRatio: 0.45, Frequency: 0.28, Reach: "Box" },
  },
  {
    Ordinal: 18,
    Id: "BoundaryAdhesion",
    Name: "Boundary Adhesion",
    Domain: "FlowField",
    Channel: "Coanda Layer",
    Color: "#38BDF8",
    Unit: "m/s",
    Equation: "v(p) = S · exp(-d_surf/δ) · t̂_coanda - k_adhere · n̂_surf",
    Summary: "Coanda surface-hugging boundary layer skimming along the floor/hull plane.",
    Defaults: { Strength: 13.0, Radius: 2.1, CoreRadius: 0.45, SwirlRatio: 0.2, Frequency: 0.22, Reach: "Box" },
  },
]);

const FORCE_MODEL_MAP = new Map(FORCE_MODELS.map((M) => [M.Id, M]));

export function GetForceModelMeta(ModelIdOrOrdinal) {
  if (typeof ModelIdOrOrdinal === "number") {
    return FORCE_MODELS[Math.max(0, Math.min(FORCE_MODELS.length - 1, ModelIdOrOrdinal | 0))];
  }
  if (ModelIdOrOrdinal === "Stream") return FORCE_MODEL_MAP.get("FlowStream");
  if (ModelIdOrOrdinal === "Vortex") return FORCE_MODEL_MAP.get("RotationalColumn");
  if (ModelIdOrOrdinal === "SurfaceAdhesion") return FORCE_MODEL_MAP.get("BoundaryAdhesion");
  return FORCE_MODEL_MAP.get(ModelIdOrOrdinal) || FORCE_MODELS[0];
}

// Curated .ForceModel & .VectorFieldSpace presets exposed in AssetDepot / ContentBrowser & Construct
export const ENGINE_FORCE_PRESETS = Object.freeze([
  {
    Id: "force-preset-coastal-crosswind",
    Name: "Coastal Crosswind",
    Format: ".ForceModel",
    Model: "LinearWind",
    Summary: "Steady 14.5 m/s maritime crosswind with gentle altitude shear.",
    Force: { Model: "LinearWind", Strength: 14.5, Radius: 2.4, CoreRadius: 0.55, YawDeg: 38, SwirlRatio: 0.14, Reach: "Column" },
  },
  {
    Id: "force-preset-thermal-updraft",
    Name: "Thermal Updraft",
    Format: ".ForceModel",
    Model: "Buoyancy",
    Summary: "Rising +Z convective column with warm core buoyancy.",
    Force: { Model: "Buoyancy", Strength: 13.0, Radius: 2.0, CoreRadius: 0.5, SwirlRatio: 0.22, Reach: "Column" },
  },
  {
    Id: "force-preset-cyclone-funnel",
    Name: "Cyclone Funnel",
    Format: ".ForceModel",
    Model: "RotationalColumn",
    Summary: "Rankine vortex column with tight inner eyewall and helical updraft.",
    Force: { Model: "RotationalColumn", Strength: 19.5, Radius: 2.2, CoreRadius: 0.52, SwirlRatio: 1.35, Reach: "Sphere" },
  },
  {
    Id: "force-preset-magnetic-containment",
    Name: "Magnetic Containment",
    Format: ".ForceModel",
    Model: "Containment",
    Summary: "Calm inner chamber bounded by a smooth restoring shell.",
    Force: { Model: "Containment", Strength: 17.5, Radius: 2.3, CoreRadius: 0.95, SwirlRatio: 0.28, Reach: "Sphere" },
  },
  {
    Id: "force-preset-curlnoise-wake",
    Name: "CurlNoise Wake",
    Format: ".VectorFieldSpace",
    Model: "CurlNoise",
    Summary: "3-octave divergence-free turbulent wake field (div v = 0).",
    Force: { Model: "CurlNoise", Strength: 15.0, Radius: 2.3, CoreRadius: 0.48, TurbulenceScale: 1.15, Octaves: 3, Reach: "Sphere" },
  },
  {
    Id: "force-preset-orbital-attractor",
    Name: "Orbital Attractor",
    Format: ".ForceModel",
    Model: "Attractor",
    Summary: "Soft-core inverse-square gravity sink with tangential swirl.",
    Force: { Model: "Attractor", Strength: 16.5, Radius: 2.1, CoreRadius: 0.45, SwirlRatio: 0.45, Attenuation: "InverseSquare", Reach: "Sphere" },
  },
  {
    Id: "force-preset-radial-shockwave",
    Name: "Radial Shockwave",
    Format: ".ForceModel",
    Model: "RadialBurst",
    Summary: "Periodic outward pressure pulse envelope.",
    Force: { Model: "RadialBurst", Strength: 21.0, Radius: 2.0, CoreRadius: 0.38, Duration: "Impulse", Frequency: 0.6, Reach: "Sphere" },
  },
  {
    Id: "force-preset-atmospheric-gust",
    Name: "Atmospheric Gust",
    Format: ".VectorFieldSpace",
    Model: "AtmosphericGust",
    Summary: "Modulated gust fronts with layered curl-noise shear.",
    Force: { Model: "AtmosphericGust", Strength: 16.0, Radius: 2.4, CoreRadius: 0.5, Duration: "Modulated", Frequency: 0.42, Reach: "Box" },
  },
  {
    Id: "force-preset-lorentz-plasma",
    Name: "Lorentz Plasma Field",
    Format: ".VectorFieldSpace",
    Model: "LorentzField",
    Summary: "Helical cyclotron gyration under crossed E and B fields.",
    Force: { Model: "LorentzField", Strength: 17.0, Radius: 2.1, CoreRadius: 0.48, SwirlRatio: 1.15, Reach: "Column" },
  },
  {
    Id: "force-preset-magnetic-dipole",
    Name: "Magnetic Dipole Torus",
    Format: ".ForceModel",
    Model: "MagneticDipole",
    Summary: "Closed dipolar field loops arcing from +Z to -Z pole.",
    Force: { Model: "MagneticDipole", Strength: 16.0, Radius: 2.2, CoreRadius: 0.5, Reach: "Sphere" },
  },
  {
    Id: "force-preset-slipstream-jet",
    Name: "Slipstream Jet",
    Format: ".ForceModel",
    Model: "FlowStream",
    Summary: "High-speed directed jet tunnel with lateral entrainment.",
    Force: { Model: "FlowStream", Strength: 18.0, Radius: 2.2, CoreRadius: 0.55, YawDeg: 180, Reach: "Sphere" },
  },
  {
    Id: "force-preset-helmholtz-formula",
    Name: "Helmholtz Formula Field",
    Format: ".VectorFieldSpace",
    Model: "FlowStream",
    Summary: "Custom VectorFieldLang symbolic Helmholtz vortex + updraft.",
    Force: {
      Model: "FlowStream",
      Strength: 16.0,
      Radius: 2.2,
      CoreRadius: 0.5,
      Reach: "Sphere",
      FormulaEnabled: true,
      FormulaCode: FORMULA_PRESETS[1]?.Code || DEFAULT_FORMULA_CODE,
    },
  },
]);

// Canonical 3-source Wind Tunnel ForceField Rig matching C++ 01-ForceFieldLitPbrAndOverlay.png
export function BuildDefaultDiagnosticForceRig() {
  return [
    {
      Id: "force-helical-vortex",
      Name: "Helical Vortex Path",
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
        MotionTopology: "SplineCurve",
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
        Model: "FlowStream",
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
      Position: [0.10, 0.08, 0.12],
      Rotation: [0, 0, 0],
      Force: ResolveForceValues({
        Model: "RadialBurst",
        Strength: 17.5,
        Radius: 1.12,
        CoreRadius: 0.30,
        Frequency: 0.45,
        Reach: "Sphere",
        Attenuation: "SmoothStep",
        Duration: "Continuous",
      }),
    },
  ];
}

const ClampNum = (Val, Min, Max, Fallback) => {
  const N = Number(Val);
  return Number.isFinite(N) ? Math.max(Min, Math.min(Max, N)) : Fallback;
};

export function ResolveForceValues(Raw = {}, FallbackModel = "LinearWind") {
  const Src = Raw?.Force && typeof Raw.Force === "object" ? Raw.Force : Raw;
  const Meta = GetForceModelMeta(Src?.Model || FallbackModel);
  const Def = Meta.Defaults;
  const Waypoints = Array.isArray(Src?.MotionWaypoints)
    ? Src.MotionWaypoints
        .filter((W) => Array.isArray(W) && W.length >= 3)
        .map((W) => [Number(W[0]) || 0, Number(W[1]) || 0, Number(W[2]) || 0])
    : [];
  return {
    Model: Meta.Id,
    Domain: Meta.Domain,
    Enabled: Src?.Enabled !== false,
    Strength: ClampNum(Src?.Strength, -80, 80, Def.Strength ?? 14.0),
    Radius: ClampNum(Src?.Radius, 0.35, 250.0, Def.Radius ?? 2.1),
    CoreRadius: ClampNum(Src?.CoreRadius, 0.08, 120.0, Def.CoreRadius ?? 0.48),
    FalloffExponent: ClampNum(Src?.FalloffExponent, 0.2, 6.0, 1.35),
    Attenuation: AttenuationCurves.includes(Src?.Attenuation)
      ? Src.Attenuation
      : Def.Attenuation || "SmoothStep",
    Reach: ReachEnvelopes.includes(Src?.Reach) ? Src.Reach : Def.Reach || "Sphere",
    Duration: DurationModes.includes(Src?.Duration) ? Src.Duration : Def.Duration || "Continuous",
    SwirlRatio: ClampNum(Src?.SwirlRatio, -3.0, 3.0, Def.SwirlRatio ?? 0.25),
    TurbulenceScale: ClampNum(Src?.TurbulenceScale, 0.15, 60.0, Def.TurbulenceScale ?? 1.2),
    Octaves: Math.round(ClampNum(Src?.Octaves, 1, 5, Def.Octaves ?? 3)),
    Frequency: ClampNum(Src?.Frequency, 0.02, 5.0, Def.Frequency ?? 0.28),
    YawDeg: ClampNum(Src?.YawDeg, -360, 360, 25),
    PitchDeg: ClampNum(Src?.PitchDeg, -90, 90, 0),
    MotionTopology: MotionTopologies.includes(Src?.MotionTopology) ? Src.MotionTopology : "Static",
    MotionLoopMode: MotionLoopModes.includes(Src?.MotionLoopMode) ? Src.MotionLoopMode : "PingPong",
    MotionAmplitude: ClampNum(Src?.MotionAmplitude, 0.0, 25.0, 0.85),
    MotionSpeedHz: ClampNum(Src?.MotionSpeedHz, 0.02, 4.0, 0.22),
    MotionPhase: ClampNum(Src?.MotionPhase, 0.0, 1.0, 0.0),
    MotionScrubU: Src?.MotionScrubU !== undefined && Src?.MotionScrubU !== null ? ClampNum(Src.MotionScrubU, 0.0, 1.0, 0.0) : null,
    MotionWaypoints: Waypoints,
    FormulaEnabled: Boolean(Src?.FormulaEnabled),
    FormulaCode:
      typeof Src?.FormulaCode === "string" && Src.FormulaCode.trim()
        ? Src.FormulaCode
        : DEFAULT_FORMULA_CODE,
  };
}

export function EnsureMotionWaypoints(BaseCenter = [0, 0, 0.6], Force = {}, TargetTopology = "SplineCurve") {
  const Cx = Number(BaseCenter[0]) || 0;
  const Cy = Number(BaseCenter[1]) || 0;
  const Cz = Number(BaseCenter[2]) || 0.6;
  const Span = Math.max(0.45, Math.min(1.6, (Force.Radius || 1.8) * 0.55));
  if (TargetTopology === "LinearAB") {
    return [
      [Number((Cx - Span * 0.85).toFixed(2)), Number((Cy - Span * 0.25).toFixed(2)), Number(Cz.toFixed(2))],
      [Number((Cx + Span * 0.85).toFixed(2)), Number((Cy + Span * 0.25).toFixed(2)), Number(Cz.toFixed(2))],
    ];
  }
  return [
    [Number((Cx - Span * 0.9).toFixed(2)), Number((Cy - Span * 0.35).toFixed(2)), Number(Math.max(0.2, Cz - 0.15).toFixed(2))],
    [Number(Cx.toFixed(2)), Number((Cy + Span * 0.45).toFixed(2)), Number((Cz + 0.22).toFixed(2))],
    [Number((Cx + Span * 0.9).toFixed(2)), Number((Cy - Span * 0.2).toFixed(2)), Number(Cz.toFixed(2))],
  ];
}

function EvalCatmullRom3(Pts, U) {
  const N = Pts.length;
  if (N === 0) return [0, 0, 0];
  if (N === 1) return [...Pts[0]];
  if (N === 2) {
    return [
      Pts[0][0] + (Pts[1][0] - Pts[0][0]) * U,
      Pts[0][1] + (Pts[1][1] - Pts[0][1]) * U,
      Pts[0][2] + (Pts[1][2] - Pts[0][2]) * U,
    ];
  }
  const Scaled = Math.max(0, Math.min(1, U)) * (N - 1);
  const Seg = Math.min(N - 2, Math.floor(Scaled));
  const T = Scaled - Seg;
  const T2 = T * T;
  const T3 = T2 * T;
  const P0 = Pts[Math.max(0, Seg - 1)];
  const P1 = Pts[Seg];
  const P2 = Pts[Seg + 1];
  const P3 = Pts[Math.min(N - 1, Seg + 2)];
  const Interp = (K) =>
    0.5 *
    (2 * P1[K] +
      (-P0[K] + P2[K]) * T +
      (2 * P0[K] - 5 * P1[K] + 4 * P2[K] - P3[K]) * T2 +
      (-P0[K] + 3 * P1[K] - 3 * P2[K] + P3[K]) * T3);
  return [Interp(0), Interp(1), Interp(2)];
}

export function EvaluateMotionPositionAtU(BaseCenter, Force, U01) {
  const Cx = BaseCenter?.[0] || 0;
  const Cy = BaseCenter?.[1] || 0;
  const Cz = BaseCenter?.[2] || 0;
  const Mode = Force.MotionTopology || "Static";
  const U = Math.max(0, Math.min(1, Number(U01) || 0));
  const Amp = Force.MotionAmplitude || 0.85;

  if (Mode === "Static") return [Cx, Cy, Cz];
  if ((Mode === "SplineCurve" || Mode === "LinearAB") && Array.isArray(Force.MotionWaypoints) && Force.MotionWaypoints.length >= 2) {
    return EvalCatmullRom3(Force.MotionWaypoints, U);
  }
  if (Mode === "LinearAB") {
    return [Cx + (U * 2 - 1) * Amp, Cy, Cz];
  }
  if (Mode === "CircularOrbit") {
    const Ang = U * TWO_PI;
    return [Cx + Math.cos(Ang) * Amp, Cy + Math.sin(Ang) * Amp, Cz];
  }
  if (Mode === "Figure8") {
    const Ang = U * TWO_PI;
    return [Cx + Math.sin(Ang) * Amp, Cy + Math.sin(Ang * 2.0) * Amp * 0.55, Cz];
  }
  return [Cx, Cy, Cz];
}

export function ComputeMotionPathMetrics(BaseCenter, Force) {
  if (!Force || Force.MotionTopology === "Static") return { LengthM: 0, DurationSec: 0 };
  let Len = 0;
  let Prev = EvaluateMotionPositionAtU(BaseCenter, Force, 0);
  for (let I = 1; I <= 24; ++I) {
    const Cur = EvaluateMotionPositionAtU(BaseCenter, Force, I / 24);
    Len += Math.hypot(Cur[0] - Prev[0], Cur[1] - Prev[1], Cur[2] - Prev[2]);
    Prev = Cur;
  }
  const DurationSec = 1.0 / Math.max(0.02, Force.MotionSpeedHz || 0.22);
  return { LengthM: Len, DurationSec };
}

//------------------------------------------------------------------------------------------------------------------------
//                                  ANALYTICAL +Z UP VECTOR FIELD EVALUATOR (NO PHYSICS)
//------------------------------------------------------------------------------------------------------------------------

function Hash3(X, Y, Z) {
  const S = Math.sin(X * 127.1 + Y * 311.7 + Z * 74.7) * 43758.5453123;
  return S - Math.floor(S);
}

function ValueNoise3(X, Y, Z) {
  const Ix = Math.floor(X), Iy = Math.floor(Y), Iz = Math.floor(Z);
  const Fx = X - Ix, Fy = Y - Iy, Fz = Z - Iz;
  const Ux = Fx * Fx * Fx * (Fx * (Fx * 6 - 15) + 10);
  const Uy = Fy * Fy * Fy * (Fy * (Fy * 6 - 15) + 10);
  const Uz = Fz * Fz * Fz * (Fz * (Fz * 6 - 15) + 10);
  const Lerp = (A, B, T) => A + (B - A) * T;
  const N000 = Hash3(Ix, Iy, Iz),       N100 = Hash3(Ix + 1, Iy, Iz);
  const N010 = Hash3(Ix, Iy + 1, Iz),   N110 = Hash3(Ix + 1, Iy + 1, Iz);
  const N001 = Hash3(Ix, Iy, Iz + 1),   N101 = Hash3(Ix + 1, Iy, Iz + 1);
  const N011 = Hash3(Ix, Iy + 1, Iz + 1), N111 = Hash3(Ix + 1, Iy + 1, Iz + 1);
  return (
    Lerp(
      Lerp(Lerp(N000, N100, Ux), Lerp(N010, N110, Ux), Uy),
      Lerp(Lerp(N001, N101, Ux), Lerp(N011, N111, Ux), Uy),
      Uz,
    ) * 2.0 - 1.0
  );
}

function PotentialFbmVec3(X, Y, Z, Octaves = 3) {
  let Ax = 0, Ay = 0, Az = 0, Amp = 1.0, Freq = 1.0, Norm = 0;
  for (let O = 0; O < Octaves; ++O) {
    Ax += Amp * ValueNoise3(X * Freq + 13.7, Y * Freq - 5.3, Z * Freq + 2.1);
    Ay += Amp * ValueNoise3(X * Freq - 9.2, Y * Freq + 17.4, Z * Freq - 6.8);
    Az += Amp * ValueNoise3(X * Freq + 4.9, Y * Freq - 11.1, Z * Freq + 21.3);
    Norm += Amp;
    Amp *= 0.5;
    Freq *= 2.02;
  }
  const Inv = Norm > 1e-6 ? 1.0 / Norm : 1.0;
  return [Ax * Inv, Ay * Inv, Az * Inv];
}

// Divergence-free 3D Curl(Psi) via central finite differences (div(curl Psi) = 0)
export function SampleCurlNoise3D(Px, Py, Pz, ScaleMeters = 1.3, Octaves = 3, TimeSec = 0) {
  const InvS = 1.0 / Math.max(0.15, ScaleMeters);
  const Qx = Px * InvS + TimeSec * 0.24;
  const Qy = Py * InvS - TimeSec * 0.19;
  const Qz = Pz * InvS + TimeSec * 0.15;
  const Eps = 1e-3;
  const Pxp = PotentialFbmVec3(Qx + Eps, Qy, Qz, Octaves);
  const Pxm = PotentialFbmVec3(Qx - Eps, Qy, Qz, Octaves);
  const Pyp = PotentialFbmVec3(Qx, Qy + Eps, Qz, Octaves);
  const Pym = PotentialFbmVec3(Qx, Qy - Eps, Qz, Octaves);
  const Pzp = PotentialFbmVec3(Qx, Qy, Qz + Eps, Octaves);
  const Pzm = PotentialFbmVec3(Qx, Qy, Qz - Eps, Octaves);
  const Inv2E = 0.5 / Eps;
  const dPz_dy = (Pyp[2] - Pym[2]) * Inv2E;
  const dPy_dz = (Pzp[1] - Pzm[1]) * Inv2E;
  const dPx_dz = (Pzp[0] - Pzm[0]) * Inv2E;
  const dPz_dx = (Pxp[2] - Pxm[2]) * Inv2E;
  const dPy_dx = (Pxp[1] - Pxm[1]) * Inv2E;
  const dPx_dy = (Pyp[0] - Pym[0]) * Inv2E;
  return [dPz_dy - dPy_dz, dPx_dz - dPz_dx, dPy_dx - dPx_dy];
}

export function EvaluateReachNormalizedDistance(Dx, Dy, Dz, Radius, Reach) {
  const SafeR = Math.max(0.1, Radius);
  if (Reach === "Infinite") return 0.0;
  if (Reach === "Column") {
    const RadialXY = Math.hypot(Dx, Dy) / SafeR;
    const AxialZ = Math.abs(Dz) / (SafeR * 1.15);
    return Math.max(RadialXY, AxialZ);
  }
  if (Reach === "Box") {
    return Math.max(Math.abs(Dx), Math.abs(Dy), Math.abs(Dz)) / SafeR;
  }
  return Math.hypot(Dx, Dy, Dz) / SafeR;
}

export function EvaluateRadialEnvelope(NormU, Curve = "SmoothStep", Exponent = 1.35) {
  if (NormU >= 1.0) return 0.0;
  if (Curve === "Constant") return 1.0;
  const U = Math.max(0.0, Math.min(1.0, NormU));
  if (Curve === "Linear") return Math.pow(1.0 - U, Exponent);
  if (Curve === "InverseSquare") {
    const Tail = 1.0 - U * U * (3.0 - 2.0 * U);
    return (1.0 / (1.0 + 6.0 * U * U)) * Tail;
  }
  const S = 1.0 - U * U;
  return Math.pow(Math.max(0.0, S * S), Exponent * 0.75);
}

export function EvaluateMotionCenter(BaseCenter, Force, TimeSec = 0) {
  const Cx = BaseCenter?.[0] || 0;
  const Cy = BaseCenter?.[1] || 0;
  const Cz = BaseCenter?.[2] || 0;
  const Mode = Force.MotionTopology || "Static";
  if (Mode === "Static") return [Cx, Cy, Cz];

  if (Force.MotionScrubU !== undefined && Force.MotionScrubU !== null && TimeSec === 0) {
    return EvaluateMotionPositionAtU(BaseCenter, Force, Force.MotionScrubU);
  }

  const RawPhase = (TimeSec * (Force.MotionSpeedHz || 0.22)) + (Force.MotionPhase || 0);
  const Loop = Force.MotionLoopMode || "PingPong";
  let U = 0;
  if (Loop === "Once") {
    U = Math.max(0, Math.min(1, RawPhase));
  } else if (Loop === "Loop" || Mode === "CircularOrbit" || Mode === "Figure8") {
    U = ((RawPhase % 1) + 1) % 1;
  } else {
    const Cyc = ((RawPhase % 2) + 2) % 2;
    U = Cyc <= 1 ? Cyc : 2 - Cyc;
  }

  if ((Mode === "SplineCurve" || Mode === "LinearAB") && Array.isArray(Force.MotionWaypoints) && Force.MotionWaypoints.length >= 2) {
    return EvalCatmullRom3(Force.MotionWaypoints, U);
  }
  const Amp = Force.MotionAmplitude || 0;
  if (Amp <= 1e-4) return [Cx, Cy, Cz];
  const PhaseRad = TimeSec * TWO_PI * (Force.MotionSpeedHz || 0.22);
  if (Mode === "LinearAB") return [Cx + Math.sin(PhaseRad) * Amp, Cy, Cz];
  if (Mode === "CircularOrbit") return [Cx + Math.cos(PhaseRad) * Amp, Cy + Math.sin(PhaseRad) * Amp, Cz];
  if (Mode === "Figure8") return [Cx + Math.sin(PhaseRad) * Amp, Cy + Math.sin(PhaseRad * 2.0) * Amp * 0.55, Cz];
  return [Cx, Cy, Cz];
}

const FORMULA_CACHE = new Map();
export function GetCompiledForceFormula(Code) {
  const Src = typeof Code === "string" ? Code : DEFAULT_FORMULA_CODE;
  if (FORMULA_CACHE.has(Src)) return FORMULA_CACHE.get(Src);
  const Compiled = CompileFieldProgram(Src);
  if (FORMULA_CACHE.size > 48) FORMULA_CACHE.clear();
  FORMULA_CACHE.set(Src, Compiled);
  return Compiled;
}

export function EvaluateForceSourceAt(ForceConfig, BaseCenter, RotationDeg, Px, Py, Pz, TimeSec = 0) {
  const F = ResolveForceValues(ForceConfig);
  if (!F.Enabled) return [0, 0, 0];

  const Center = EvaluateMotionCenter(BaseCenter || [0, 0, 1.4], F, TimeSec);
  const Dx = Px - Center[0];
  const Dy = Py - Center[1];
  const Dz = Pz - Center[2];

  const NormU = EvaluateReachNormalizedDistance(Dx, Dy, Dz, F.Radius, F.Reach);
  if (NormU >= 1.0 && F.Reach !== "Infinite") return [0, 0, 0];

  let Gate = 1.0;
  if (F.Duration === "Modulated") {
    Gate = 0.62 + 0.38 * Math.sin(TimeSec * TWO_PI * F.Frequency);
  } else if (F.Duration === "Impulse") {
    const Cycle = (TimeSec * F.Frequency) % 1.0;
    const T = Cycle < 0 ? Cycle + 1.0 : Cycle;
    Gate = Math.exp(-T * 4.5) * Math.sin(Math.min(1.0, T * 6.0) * Math.PI * 0.5);
  }

  const Env = EvaluateRadialEnvelope(NormU, F.Attenuation, F.FalloffExponent) * Gate;
  if (Math.abs(Env) <= 1e-6) return [0, 0, 0];

  if (F.FormulaEnabled && F.FormulaCode) {
    const Prog = GetCompiledForceFormula(F.FormulaCode);
    if (Prog && Prog.Ok && typeof Prog.Evaluate === "function") {
      try {
        const Res = Prog.Evaluate(
          { X: Dx, Y: Dz, Z: Dy },
          { X: 0, Y: 0, Z: 0 },
          {
            Strength: F.Strength,
            Radius: F.Radius,
            Swirl: F.SwirlRatio * F.Strength,
            Gate,
            AttenuateFn: (Dist, Rad) =>
              EvaluateRadialEnvelope(Dist / Math.max(0.1, Rad || F.Radius), F.Attenuation, F.FalloffExponent),
          },
          TimeSec,
        );
        if (Res && Number.isFinite(Res.X)) {
          return [Res.X, Res.Z || 0, Res.Y || 0];
        }
      } catch {}
    }
  }

  const RotYaw = (((RotationDeg?.[2] || 0) + F.YawDeg) * DEG_TO_RAD);
  const RotPitch = (((RotationDeg?.[1] || 0) + F.PitchDeg) * DEG_TO_RAD);
  const DirX = Math.cos(RotPitch) * Math.cos(RotYaw);
  const DirY = Math.cos(RotPitch) * Math.sin(RotYaw);
  const DirZ = Math.sin(RotPitch);

  const R3 = Math.hypot(Dx, Dy, Dz);
  const SafeR3 = Math.max(F.CoreRadius, R3, 0.08);
  const Rx = R3 > 1e-5 ? Dx / R3 : 1.0;
  const Ry = R3 > 1e-5 ? Dy / R3 : 0.0;
  const Rz = R3 > 1e-5 ? Dz / R3 : 0.0;

  const Rxy = Math.hypot(Dx, Dy);
  const Tx = Rxy > 1e-5 ? -Dy / Rxy : 0.0;
  const Ty = Rxy > 1e-5 ? Dx / Rxy : 1.0;

  const S = F.Strength * Env;

  switch (F.Model) {
    case "LinearWind": {
      const Shear = 1.0 + 0.14 * Math.max(-1.0, Dz / Math.max(0.5, F.Radius));
      return [DirX * S * Shear, DirY * S * Shear, (DirZ + 0.05 * F.SwirlRatio) * S];
    }
    case "Buoyancy":
      return [-Dx * 0.12 * S, -Dy * 0.12 * S, S];
    case "RadialBurst":
    case "Repulsor":
      return [Rx * S, Ry * S, Rz * S];
    case "Attractor": {
      const CoreGain = Math.min(1.8, (F.Radius * 0.55) / SafeR3);
      return [
        (-Rx * CoreGain + Tx * F.SwirlRatio * 0.45) * S,
        (-Ry * CoreGain + Ty * F.SwirlRatio * 0.45) * S,
        -Rz * CoreGain * S,
      ];
    }
    case "AxisAttractor": {
      const RadIn = Rxy > 1e-5 ? -1.0 / Rxy : 0.0;
      return [
        (Dx * RadIn * 0.75 + Tx * F.SwirlRatio) * S,
        (Dy * RadIn * 0.75 + Ty * F.SwirlRatio) * S,
        0.18 * S,
      ];
    }
    case "Containment": {
      if (R3 <= F.CoreRadius) return [Tx * S * 0.12 * F.SwirlRatio, Ty * S * 0.12 * F.SwirlRatio, 0];
      const Wall = Math.min(1.5, (R3 - F.CoreRadius) / Math.max(0.15, F.Radius - F.CoreRadius));
      return [-Rx * S * Wall, -Ry * S * Wall, -Rz * S * Wall];
    }
    case "RotationalColumn": {
      const Rankine =
        Rxy < F.CoreRadius
          ? Rxy / Math.max(0.08, F.CoreRadius)
          : Math.max(0.08, F.CoreRadius) / Math.max(F.CoreRadius, Rxy);
      const Inflow = -0.22 * (Rxy > 1e-5 ? 1.0 : 0.0);
      return [
        (Tx * Rankine * F.SwirlRatio + (Rxy > 1e-5 ? (Dx / Rxy) * Inflow : 0)) * S,
        (Ty * Rankine * F.SwirlRatio + (Rxy > 1e-5 ? (Dy / Rxy) * Inflow : 0)) * S,
        0.28 * Rankine * S,
      ];
    }
    case "Torque":
      return [Tx * S * F.SwirlRatio, Ty * S * F.SwirlRatio, 0.0];
    case "Drag":
      return [-DirX * S * 0.55, -DirY * S * 0.55, -Rz * S * 0.35];
    case "CurlNoise": {
      const C = SampleCurlNoise3D(Dx, Dy, Dz, F.TurbulenceScale, F.Octaves, TimeSec);
      return [C[0] * S * 1.4, C[1] * S * 1.4, C[2] * S * 1.4];
    }
    case "AtmosphericGust": {
      const Wave = 0.65 + 0.35 * Math.sin((Dx * DirX + Dy * DirY) * 2.2 - TimeSec * TWO_PI * F.Frequency);
      const C = SampleCurlNoise3D(Dx, Dy, Dz, F.TurbulenceScale, F.Octaves, TimeSec);
      return [
        (DirX * Wave + C[0] * 0.42) * S,
        (DirY * Wave + C[1] * 0.42) * S,
        (DirZ * Wave + C[2] * 0.35) * S,
      ];
    }
    case "VorticityConfinement": {
      const C = SampleCurlNoise3D(Dx, Dy, Dz, F.TurbulenceScale, F.Octaves, TimeSec);
      return [(Tx * 0.65 + C[0]) * S, (Ty * 0.65 + C[1]) * S, C[2] * S];
    }
    case "BrownianNoise": {
      const J = PotentialFbmVec3(Dx / F.TurbulenceScale + TimeSec * 1.7, Dy / F.TurbulenceScale, Dz / F.TurbulenceScale - TimeSec * 1.3, F.Octaves);
      return [J[0] * S, J[1] * S, J[2] * S];
    }
    case "LorentzField":
      return [
        (DirX * 0.35 + Tx * F.SwirlRatio) * S,
        (DirY * 0.35 + Ty * F.SwirlRatio) * S,
        0.42 * S,
      ];
    case "MagneticDipole": {
      const MdotR = Rz;
      const Bx = 3.0 * MdotR * Rx;
      const By = 3.0 * MdotR * Ry;
      const Bz = 3.0 * MdotR * Rz - 1.0;
      const BNorm = Math.hypot(Bx, By, Bz) || 1.0;
      return [(Bx / BNorm) * S, (By / BNorm) * S, (Bz / BNorm) * S];
    }
    case "FlowStream": {
      const Proj = Dx * DirX + Dy * DirY + Dz * DirZ;
      const LatX = Dx - Proj * DirX;
      const LatY = Dy - Proj * DirY;
      const LatZ = Dz - Proj * DirZ;
      return [
        (DirX - LatX * 0.32) * S,
        (DirY - LatY * 0.32) * S,
        (DirZ - LatZ * 0.32) * S,
      ];
    }
    case "SplineForce": {
      const CurveY = 0.55 * Math.sin((Dx / Math.max(0.5, F.Radius)) * Math.PI);
      const Slope = 0.55 * (Math.PI / Math.max(0.5, F.Radius)) * Math.cos((Dx / Math.max(0.5, F.Radius)) * Math.PI);
      const NormS = Math.hypot(1.0, Slope);
      return [
        (1.0 / NormS) * S,
        ((Slope / NormS) - (Dy - CurveY) * 0.45) * S,
        -Dz * 0.35 * S,
      ];
    }
    case "BoundaryAdhesion": {
      const HeightAboveFloor = Math.max(0.0, Pz);
      const Skin = Math.exp(-HeightAboveFloor / Math.max(0.25, F.CoreRadius * 1.4));
      return [DirX * S * (0.45 + 0.75 * Skin), DirY * S * (0.45 + 0.75 * Skin), -0.45 * S * Skin];
    }
    default:
      return [DirX * S, DirY * S, DirZ * S];
  }
}

export function EvaluateSceneForceFieldAt(ForceNodes, Px, Py, Pz, TimeSec = 0) {
  let Vx = 0, Vy = 0, Vz = 0;
  if (!Array.isArray(ForceNodes)) return [0, 0, 0];
  for (const Node of ForceNodes) {
    if (!Node || Node.Hidden) continue;
    const V = EvaluateForceSourceAt(Node.Force, Node.Position, Node.Rotation, Px, Py, Pz, TimeSec);
    Vx += V[0];
    Vy += V[1];
    Vz += V[2];
  }
  return [Vx, Vy, Vz];
}

export function ClassifyBeaufortScale(SpeedMs) {
  const S = Math.abs(SpeedMs);
  if (S < 0.5) return { Number: 0, Label: "Calm", Land: "smoke rises vertically; mirror-still air" };
  if (S < 1.6) return { Number: 1, Label: "Light Air", Land: "wind motion visible in smoke drift" };
  if (S < 3.4) return { Number: 2, Label: "Light Breeze", Land: "leaves rustle gently; wind felt on face" };
  if (S < 5.5) return { Number: 3, Label: "Gentle Breeze", Land: "leaves and small twigs in constant motion" };
  if (S < 8.0) return { Number: 4, Label: "Moderate Breeze", Land: "raises dust and loose paper; small branches move" };
  if (S < 10.8) return { Number: 5, Label: "Fresh Breeze", Land: "small trees in leaf begin to sway" };
  if (S < 13.9) return { Number: 6, Label: "Strong Breeze", Land: "large branches in motion; whistling in wires" };
  if (S < 17.2) return { Number: 7, Label: "Near Gale", Land: "whole trees in motion; effort walking upwind" };
  if (S < 20.8) return { Number: 8, Label: "Gale", Land: "twigs broken off trees; strong structural load" };
  if (S < 24.5) return { Number: 9, Label: "Strong Gale", Land: "high-velocity gusts; vortex shedding active" };
  if (S < 28.5) return { Number: 10, Label: "Storm", Land: "trees uprooted; severe aerodynamic pressure" };
  if (S < 32.7) return { Number: 11, Label: "Violent Storm", Land: "widespread turbulent shear; extreme dynamic q" };
  return { Number: 12, Label: "Hurricane", Land: "extreme cyclonic core; supersonic-class gust load" };
}

function CompassFromBearingDeg(Deg) {
  const Dirs = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"];
  const Norm = ((Deg % 360) + 360) % 360;
  return Dirs[Math.round(Norm / 45) % 8];
}

// Central-difference 3D differential tensor (Curl |∇×F|, Divergence ∇·F) + Anemometer telemetry
export function EvaluateSceneCompositeTelemetry(ForceNodes, Px, Py, Pz, TimeSec = 0, StepH = 0.08) {
  let FlowX = 0, FlowY = 0, FlowZ = 0;
  let AccX = 0, AccY = 0, AccZ = 0;
  let ContributingCount = 0;

  if (Array.isArray(ForceNodes)) {
    for (const Node of ForceNodes) {
      if (!Node || Node.Hidden) continue;
      const V = EvaluateForceSourceAt(Node.Force, Node.Position, Node.Rotation, Px, Py, Pz, TimeSec);
      const Mag = Math.hypot(V[0], V[1], V[2]);
      if (Mag > 1e-3) ContributingCount += 1;
      const Meta = GetForceModelMeta(Node.Force?.Model);
      if (Meta.Unit === "m/s") {
        FlowX += V[0]; FlowY += V[1]; FlowZ += V[2];
      } else {
        AccX += V[0]; AccY += V[1]; AccZ += V[2];
      }
    }
  }

  const TotalVec = [FlowX + AccX, FlowY + AccY, FlowZ + AccZ];
  const TotalMag = Math.hypot(TotalVec[0], TotalVec[1], TotalVec[2]);
  const FlowSpeed = Math.hypot(FlowX, FlowY, FlowZ);
  const AccelMag = Math.hypot(AccX, AccY, AccZ);
  const EffectiveSpeed = FlowSpeed > 0.05 ? FlowSpeed : TotalMag;

  const H = Math.max(0.02, StepH);
  const Inv2H = 0.5 / H;
  const Xp = EvaluateSceneForceFieldAt(ForceNodes, Px + H, Py, Pz, TimeSec);
  const Xm = EvaluateSceneForceFieldAt(ForceNodes, Px - H, Py, Pz, TimeSec);
  const Yp = EvaluateSceneForceFieldAt(ForceNodes, Px, Py + H, Pz, TimeSec);
  const Ym = EvaluateSceneForceFieldAt(ForceNodes, Px, Py - H, Pz, TimeSec);
  const Zp = EvaluateSceneForceFieldAt(ForceNodes, Px, Py, Pz + H, TimeSec);
  const Zm = EvaluateSceneForceFieldAt(ForceNodes, Px, Py, Pz - H, TimeSec);

  const Divergence = ((Xp[0] - Xm[0]) + (Yp[1] - Ym[1]) + (Zp[2] - Zm[2])) * Inv2H;
  const CurlX = (Yp[2] - Ym[2] - (Zp[1] - Zm[1])) * Inv2H;
  const CurlY = (Zp[0] - Zm[0] - (Xp[2] - Xm[2])) * Inv2H;
  const CurlZ = (Xp[1] - Xm[1] - (Yp[0] - Ym[0])) * Inv2H;
  const CurlMag = Math.hypot(CurlX, CurlY, CurlZ);

  const Horiz = Math.hypot(TotalVec[0], TotalVec[1]);
  const CompassDeg = Horiz > 0.02 ? ((Math.atan2(TotalVec[0], TotalVec[1]) * 180) / Math.PI + 360) % 360 : 0;
  const DynamicPressurePa = 0.5 * 1.225 * EffectiveSpeed * EffectiveSpeed;

  return {
    TotalVec,
    TotalMag,
    Speed: EffectiveSpeed,
    FlowSpeed,
    AccelMag,
    CurlVec: [CurlX, CurlY, CurlZ],
    CurlMag,
    Divergence,
    DynamicPressurePa,
    CompassDeg,
    CompassLabel: CompassFromBearingDeg(CompassDeg),
    Beaufort: ClassifyBeaufortScale(EffectiveSpeed),
    BeaufortShare: Math.max(0, Math.min(1, EffectiveSpeed / 34.0)),
    ContributingCount,
  };
}

//------------------------------------------------------------------------------------------------------------------------
//                  SCIENTIFIC COLORMAPS (EXACT LUT MATCH WITH VECTORFIELDSHADERS.JS & C++ EXHIBITS)
//------------------------------------------------------------------------------------------------------------------------

function HexToRgb01(Hex) {
  const Clean = String(Hex || "#4AA8FF").replace("#", "");
  const Num = parseInt(Clean.length === 3 ? Clean.split("").map((C) => C + C).join("") : Clean, 16) || 0x4aa8ff;
  return [((Num >> 16) & 255) / 255, ((Num >> 8) & 255) / 255, (Num & 255) / 255];
}

function LerpRgb(Stops, Share01) {
  const T = Math.max(0, Math.min(1, Share01));
  const Scaled = T * (Stops.length - 1);
  const I = Math.min(Stops.length - 2, Math.floor(Scaled));
  const F = Scaled - I;
  const A = Stops[I], B = Stops[I + 1];
  return [A[0] + (B[0] - A[0]) * F, A[1] + (B[1] - A[1]) * F, A[2] + (B[2] - A[2]) * F];
}

const LUT_ENGINEERING = [
  [0.063, 0.165, 0.337],
  [0.110, 0.494, 0.839],
  [0.133, 0.722, 0.812],
  [0.510, 0.788, 0.118],
  [0.980, 0.690, 0.020],
  [0.941, 0.243, 0.243],
];
const LUT_TURBO = [
  [0.188, 0.071, 0.231],
  [0.275, 0.522, 0.980],
  [0.102, 0.894, 0.714],
  [0.980, 0.729, 0.224],
  [0.478, 0.016, 0.012],
];
const LUT_PLASMA = [
  [0.051, 0.031, 0.529],
  [0.494, 0.012, 0.659],
  [0.800, 0.278, 0.471],
  [0.973, 0.584, 0.251],
  [0.941, 0.976, 0.129],
];
const LUT_DIVERGING = [
  [0.169, 0.424, 0.690],
  [0.388, 0.702, 0.929],
  [0.145, 0.175, 0.225],
  [0.988, 0.506, 0.506],
  [0.898, 0.243, 0.243],
];

export function SampleFieldColormap(RampMode = "Engineering", Share01 = 0.0, DomainRgb = [0.29, 0.66, 1.0]) {
  const T = Math.max(0, Math.min(1, Share01));
  if (RampMode === "DomainTint") {
    return [
      DomainRgb[0] * (0.45 + 0.55 * T),
      DomainRgb[1] * (0.45 + 0.55 * T),
      DomainRgb[2] * (0.45 + 0.55 * T),
    ];
  }
  if (RampMode === "Turbo") return LerpRgb(LUT_TURBO, T);
  if (RampMode === "Plasma" || RampMode === "Viridis") return LerpRgb(LUT_PLASMA, T);
  if (RampMode === "Diverging") return LerpRgb(LUT_DIVERGING, T);
  return LerpRgb(LUT_ENGINEERING, T);
}

//------------------------------------------------------------------------------------------------------------------------
//                      GPU FORCEOVERLAY 12-FLOAT VERTEX BUILDER (WebGPU + WebGL2 + CpuGtx)
//------------------------------------------------------------------------------------------------------------------------

function PushTri12(Buf, A, B, C, Normal, Rgba, ShadingWeight = 0.0) {
  const [R, G, Bl, Al] = Rgba;
  const [Nx, Ny, Nz] = Normal;
  Buf.push(
    A[0], A[1], A[2], Nx, Ny, Nz, R, G, Bl, Al, 0, ShadingWeight,
    B[0], B[1], B[2], Nx, Ny, Nz, R, G, Bl, Al, 0, ShadingWeight,
    C[0], C[1], C[2], Nx, Ny, Nz, R, G, Bl, Al, 0, ShadingWeight,
  );
}

function PushSmoothTri12(Buf, A, Ca, B, Cb, C, Cc, Normal = [0, 0, 1]) {
  const [Nx, Ny, Nz] = Normal;
  Buf.push(
    A[0], A[1], A[2], Nx, Ny, Nz, Ca[0], Ca[1], Ca[2], Ca[3], 0, 0.0,
    B[0], B[1], B[2], Nx, Ny, Nz, Cb[0], Cb[1], Cb[2], Cb[3], 0, 0.0,
    C[0], C[1], C[2], Nx, Ny, Nz, Cc[0], Cc[1], Cc[2], Cc[3], 0, 0.0,
  );
}

function PushRibbonQuad(Buf, P0, P1, HalfW0, HalfW1, Col0, Col1, CameraOrigin) {
  const Tx = P1[0] - P0[0], Ty = P1[1] - P0[1], Tz = P1[2] - P0[2];
  const TLen = Math.hypot(Tx, Ty, Tz);
  if (TLen < 1e-5) return;
  const Dx = Tx / TLen, Dy = Ty / TLen, Dz = Tz / TLen;

  const Mx = 0.5 * (P0[0] + P1[0]) - CameraOrigin[0];
  const My = 0.5 * (P0[1] + P1[1]) - CameraOrigin[1];
  const Mz = 0.5 * (P0[2] + P1[2]) - CameraOrigin[2];

  let Lx = Dy * Mz - Dz * My;
  let Ly = Dz * Mx - Dx * Mz;
  let Lz = Dx * My - Dy * Mx;
  let LLen = Math.hypot(Lx, Ly, Lz);
  if (LLen < 1e-5) {
    Lx = -Dy; Ly = Dx; Lz = 0;
    LLen = Math.hypot(Lx, Ly, Lz) || 1;
  }
  Lx /= LLen; Ly /= LLen; Lz /= LLen;

  const A0 = [P0[0] - Lx * HalfW0, P0[1] - Ly * HalfW0, P0[2] - Lz * HalfW0];
  const B0 = [P0[0] + Lx * HalfW0, P0[1] + Ly * HalfW0, P0[2] + Lz * HalfW0];
  const A1 = [P1[0] - Lx * HalfW1, P1[1] - Ly * HalfW1, P1[2] - Lz * HalfW1];
  const B1 = [P1[0] + Lx * HalfW1, P1[1] + Ly * HalfW1, P1[2] + Lz * HalfW1];
  PushSmoothTri12(Buf, A0, Col0, B0, Col0, B1, Col1);
  PushSmoothTri12(Buf, A0, Col0, B1, Col1, A1, Col1);
}

function PushBillboardDisc(Buf, Center, Radius, CenterRgba, EdgeRgba, CameraOrigin, Segments = 10) {
  let Vx = CameraOrigin[0] - Center[0];
  let Vy = CameraOrigin[1] - Center[1];
  let Vz = CameraOrigin[2] - Center[2];
  const VLen = Math.hypot(Vx, Vy, Vz) || 1;
  Vx /= VLen; Vy /= VLen; Vz /= VLen;

  const Hx = Math.abs(Vz) < 0.92 ? 0 : 1;
  const Hz = Math.abs(Vz) < 0.92 ? 1 : 0;
  let Ux = -Hz * Vy, Uy = Hz * Vx - Hx * Vz, Uz = Hx * Vy;
  const ULen = Math.hypot(Ux, Uy, Uz) || 1;
  Ux /= ULen; Uy /= ULen; Uz /= ULen;
  const Wx = Vy * Uz - Vz * Uy;
  const Wy = Vz * Ux - Vx * Uz;
  const Wz = Vx * Uy - Vy * Ux;

  for (let I = 0; I < Segments; ++I) {
    const A0 = (I / Segments) * TWO_PI;
    const A1 = ((I + 1) / Segments) * TWO_PI;
    const P0 = [
      Center[0] + (Ux * Math.cos(A0) + Wx * Math.sin(A0)) * Radius,
      Center[1] + (Uy * Math.cos(A0) + Wy * Math.sin(A0)) * Radius,
      Center[2] + (Uz * Math.cos(A0) + Wz * Math.sin(A0)) * Radius,
    ];
    const P1 = [
      Center[0] + (Ux * Math.cos(A1) + Wx * Math.sin(A1)) * Radius,
      Center[1] + (Uy * Math.cos(A1) + Wy * Math.sin(A1)) * Radius,
      Center[2] + (Uz * Math.cos(A1) + Wz * Math.sin(A1)) * Radius,
    ];
    PushSmoothTri12(Buf, Center, CenterRgba, P0, EdgeRgba, P1, EdgeRgba, [Vx, Vy, Vz]);
  }
}

function PushTubeSegment(Buf, P0, P1, Radius, Rgba, Sides = 4, ShadingWeight = 0.14) {
  const Dx = P1[0] - P0[0], Dy = P1[1] - P0[1], Dz = P1[2] - P0[2];
  const L = Math.hypot(Dx, Dy, Dz);
  if (L < 1e-4) return;
  const Ax = Dx / L, Ay = Dy / L, Az = Dz / L;
  const Hx = Math.abs(Az) < 0.9 ? 0 : 1;
  const Hz = Math.abs(Az) < 0.9 ? 1 : 0;
  let Ux = -Hz * Ay;
  let Uy = Hz * Ax - Hx * Az;
  let Uz = Hx * Ay;
  const UL = Math.hypot(Ux, Uy, Uz) || 1;
  Ux /= UL; Uy /= UL; Uz /= UL;
  const Vx = Ay * Uz - Az * Uy;
  const Vy = Az * Ux - Ax * Uz;
  const Vz = Ax * Uy - Ay * Ux;

  for (let I = 0; I < Sides; ++I) {
    const A0 = (I / Sides) * TWO_PI;
    const A1 = ((I + 1) / Sides) * TWO_PI;
    const C0 = Math.cos(A0), S0 = Math.sin(A0);
    const C1 = Math.cos(A1), S1 = Math.sin(A1);
    const N0x = Ux * C0 + Vx * S0, N0y = Uy * C0 + Vy * S0, N0z = Uz * C0 + Vz * S0;
    const N1x = Ux * C1 + Vx * S1, N1y = Uy * C1 + Vy * S1, N1z = Uz * C1 + Vz * S1;
    const V00 = [P0[0] + N0x * Radius, P0[1] + N0y * Radius, P0[2] + N0z * Radius];
    const V01 = [P0[0] + N1x * Radius, P0[1] + N1y * Radius, P0[2] + N1z * Radius];
    const V10 = [P1[0] + N0x * Radius, P1[1] + N0y * Radius, P1[2] + N0z * Radius];
    const V11 = [P1[0] + N1x * Radius, P1[1] + N1y * Radius, P1[2] + N1z * Radius];
    PushTri12(Buf, V00, V10, V11, [N0x, N0y, N0z], Rgba, ShadingWeight);
    PushTri12(Buf, V00, V11, V01, [N1x, N1y, N1z], Rgba, ShadingWeight);
  }
}

function PushArrow3D(Buf, Origin, Dir, Length, ShaftRadius, Rgba) {
  const TipLen = Math.min(Length * 0.34, ShaftRadius * 5.2);
  const ShaftLen = Math.max(0.02, Length - TipLen);
  const Mid = [
    Origin[0] + Dir[0] * ShaftLen,
    Origin[1] + Dir[1] * ShaftLen,
    Origin[2] + Dir[2] * ShaftLen,
  ];
  const Tip = [
    Origin[0] + Dir[0] * Length,
    Origin[1] + Dir[1] * Length,
    Origin[2] + Dir[2] * Length,
  ];
  PushTubeSegment(Buf, Origin, Mid, ShaftRadius, Rgba, 4, 0.16);

  const Hx = Math.abs(Dir[2]) < 0.9 ? 0 : 1;
  const Hz = Math.abs(Dir[2]) < 0.9 ? 1 : 0;
  let Ux = -Hz * Dir[1], Uy = Hz * Dir[0] - Hx * Dir[2], Uz = Hx * Dir[1];
  const UL = Math.hypot(Ux, Uy, Uz) || 1;
  Ux /= UL; Uy /= UL; Uz /= UL;
  const Vx = Dir[1] * Uz - Dir[2] * Uy;
  const Vy = Dir[2] * Ux - Dir[0] * Uz;
  const Vz = Dir[0] * Uy - Dir[1] * Ux;
  const ConeR = ShaftRadius * 2.45;

  for (let I = 0; I < 4; ++I) {
    const A0 = (I / 4) * TWO_PI;
    const A1 = ((I + 1) / 4) * TWO_PI;
    const B0 = [
      Mid[0] + (Ux * Math.cos(A0) + Vx * Math.sin(A0)) * ConeR,
      Mid[1] + (Uy * Math.cos(A0) + Vy * Math.sin(A0)) * ConeR,
      Mid[2] + (Uz * Math.cos(A0) + Vz * Math.sin(A0)) * ConeR,
    ];
    const B1 = [
      Mid[0] + (Ux * Math.cos(A1) + Vx * Math.sin(A1)) * ConeR,
      Mid[1] + (Uy * Math.cos(A1) + Vy * Math.sin(A1)) * ConeR,
      Mid[2] + (Uz * Math.cos(A1) + Vz * Math.sin(A1)) * ConeR,
    ];
    PushTri12(Buf, B0, B1, Tip, Dir, Rgba, 0.22);
  }
}

function PushCircleRing(Buf, Center, UAxis, VAxis, Radius, TubeR, Rgba, Segments = 32, Dashed = false) {
  for (let I = 0; I < Segments; ++I) {
    if (Dashed && (I & 1) === 1) continue;
    const A0 = (I / Segments) * TWO_PI;
    const A1 = ((I + 1) / Segments) * TWO_PI;
    const P0 = [
      Center[0] + (UAxis[0] * Math.cos(A0) + VAxis[0] * Math.sin(A0)) * Radius,
      Center[1] + (UAxis[1] * Math.cos(A0) + VAxis[1] * Math.sin(A0)) * Radius,
      Center[2] + (UAxis[2] * Math.cos(A0) + VAxis[2] * Math.sin(A0)) * Radius,
    ];
    const P1 = [
      Center[0] + (UAxis[0] * Math.cos(A1) + VAxis[0] * Math.sin(A1)) * Radius,
      Center[1] + (UAxis[1] * Math.cos(A1) + VAxis[1] * Math.sin(A1)) * Radius,
      Center[2] + (UAxis[2] * Math.cos(A1) + VAxis[2] * Math.sin(A1)) * Radius,
    ];
    PushTubeSegment(Buf, P0, P1, TubeR, Rgba, 3, 0.0);
  }
}

function PushSolidSphere(Buf, Center, Radius, Rgba, Rings = 8, Sectors = 12, ShadingWeight = 0.32) {
  for (let R = 0; R < Rings; ++R) {
    const Phi0 = (R / Rings) * Math.PI;
    const Phi1 = ((R + 1) / Rings) * Math.PI;
    const SinP0 = Math.sin(Phi0), CosP0 = Math.cos(Phi0);
    const SinP1 = Math.sin(Phi1), CosP1 = Math.cos(Phi1);
    for (let S = 0; S < Sectors; ++S) {
      const Th0 = (S / Sectors) * TWO_PI;
      const Th1 = ((S + 1) / Sectors) * TWO_PI;
      const N00 = [SinP0 * Math.cos(Th0), SinP0 * Math.sin(Th0), CosP0];
      const N01 = [SinP0 * Math.cos(Th1), SinP0 * Math.sin(Th1), CosP0];
      const N10 = [SinP1 * Math.cos(Th0), SinP1 * Math.sin(Th0), CosP1];
      const N11 = [SinP1 * Math.cos(Th1), SinP1 * Math.sin(Th1), CosP1];
      const P00 = [Center[0] + N00[0] * Radius, Center[1] + N00[1] * Radius, Center[2] + N00[2] * Radius];
      const P01 = [Center[0] + N01[0] * Radius, Center[1] + N01[1] * Radius, Center[2] + N01[2] * Radius];
      const P10 = [Center[0] + N10[0] * Radius, Center[1] + N10[1] * Radius, Center[2] + N10[2] * Radius];
      const P11 = [Center[0] + N11[0] * Radius, Center[1] + N11[1] * Radius, Center[2] + N11[2] * Radius];
      PushTri12(Buf, P00, P10, P11, N10, Rgba, ShadingWeight);
      PushTri12(Buf, P00, P11, P01, N01, Rgba, ShadingWeight);
    }
  }
}

// Smooth Fresnel Translucent Volumetric Shell (matches C++ 01-ForceFieldLitPbrAndOverlay.png!)
function PushFresnelSphereShell(Buf, Center, Radius, TintRgb, IsSelected, CameraOrigin, Rings = 14, Sectors = 24) {
  const BaseAlpha = IsSelected ? 0.030 : 0.015;
  const RimAlpha = IsSelected ? 0.17 : 0.105;

  const VertCol = (Pos, Norm) => {
    let Vx = CameraOrigin[0] - Pos[0];
    let Vy = CameraOrigin[1] - Pos[1];
    let Vz = CameraOrigin[2] - Pos[2];
    const VL = Math.hypot(Vx, Vy, Vz) || 1;
    Vx /= VL; Vy /= VL; Vz /= VL;
    const NdotV = Math.abs(Norm[0] * Vx + Norm[1] * Vy + Norm[2] * Vz);
    const Rim = Math.pow(Math.max(0, 1.0 - NdotV), 1.75);
    const TopLift = 0.88 + 0.20 * Math.max(0, Norm[2]);
    return [
      Math.min(1, TintRgb[0] * TopLift + Rim * 0.14),
      Math.min(1, TintRgb[1] * TopLift + Rim * 0.14),
      Math.min(1, TintRgb[2] * TopLift + Rim * 0.14),
      BaseAlpha + (RimAlpha - BaseAlpha) * Rim,
    ];
  };

  for (let R = 0; R < Rings; ++R) {
    const Phi0 = (R / Rings) * Math.PI;
    const Phi1 = ((R + 1) / Rings) * Math.PI;
    const SinP0 = Math.sin(Phi0), CosP0 = Math.cos(Phi0);
    const SinP1 = Math.sin(Phi1), CosP1 = Math.cos(Phi1);
    for (let S = 0; S < Sectors; ++S) {
      const Th0 = (S / Sectors) * TWO_PI;
      const Th1 = ((S + 1) / Sectors) * TWO_PI;
      const N00 = [SinP0 * Math.cos(Th0), SinP0 * Math.sin(Th0), CosP0];
      const N01 = [SinP0 * Math.cos(Th1), SinP0 * Math.sin(Th1), CosP0];
      const N10 = [SinP1 * Math.cos(Th0), SinP1 * Math.sin(Th0), CosP1];
      const N11 = [SinP1 * Math.cos(Th1), SinP1 * Math.sin(Th1), CosP1];
      const P00 = [Center[0] + N00[0] * Radius, Center[1] + N00[1] * Radius, Center[2] + N00[2] * Radius];
      const P01 = [Center[0] + N01[0] * Radius, Center[1] + N01[1] * Radius, Center[2] + N01[2] * Radius];
      const P10 = [Center[0] + N10[0] * Radius, Center[1] + N10[1] * Radius, Center[2] + N10[2] * Radius];
      const P11 = [Center[0] + N11[0] * Radius, Center[1] + N11[1] * Radius, Center[2] + N11[2] * Radius];
      const C00 = VertCol(P00, N00), C01 = VertCol(P01, N01);
      const C10 = VertCol(P10, N10), C11 = VertCol(P11, N11);
      PushSmoothTri12(Buf, P00, C00, P10, C10, P11, C11, N10);
      PushSmoothTri12(Buf, P00, C00, P11, C11, P01, C01, N01);
    }
  }
}

function PushFresnelColumnShell(Buf, Center, Radius, HalfH, TintRgb, IsSelected, CameraOrigin, Sectors = 24) {
  const BaseAlpha = IsSelected ? 0.05 : 0.03;
  const RimAlpha = IsSelected ? 0.25 : 0.17;
  for (let S = 0; S < Sectors; ++S) {
    const A0 = (S / Sectors) * TWO_PI;
    const A1 = ((S + 1) / Sectors) * TWO_PI;
    const N0 = [Math.cos(A0), Math.sin(A0), 0];
    const N1 = [Math.cos(A1), Math.sin(A1), 0];
    const P0B = [Center[0] + N0[0] * Radius, Center[1] + N0[1] * Radius, Center[2] - HalfH];
    const P0T = [Center[0] + N0[0] * Radius, Center[1] + N0[1] * Radius, Center[2] + HalfH];
    const P1B = [Center[0] + N1[0] * Radius, Center[1] + N1[1] * Radius, Center[2] - HalfH];
    const P1T = [Center[0] + N1[0] * Radius, Center[1] + N1[1] * Radius, Center[2] + HalfH];
    const ColAt = (P, N) => {
      const Vx = CameraOrigin[0] - P[0], Vy = CameraOrigin[1] - P[1], Vz = CameraOrigin[2] - P[2];
      const VL = Math.hypot(Vx, Vy, Vz) || 1;
      const Rim = Math.pow(Math.max(0, 1.0 - Math.abs((N[0] * Vx + N[1] * Vy) / VL)), 1.65);
      return [TintRgb[0], TintRgb[1], TintRgb[2], BaseAlpha + (RimAlpha - BaseAlpha) * Rim];
    };
    const C0B = ColAt(P0B, N0), C0T = ColAt(P0T, N0), C1B = ColAt(P1B, N1), C1T = ColAt(P1T, N1);
    PushSmoothTri12(Buf, P0B, C0B, P1B, C1B, P1T, C1T, N0);
    PushSmoothTri12(Buf, P0B, C0B, P1T, C1T, P0T, C0T, N1);
  }
}

function EvaluateSliceQuantityAt(ForceNodes, Quantity, Px, Py, Pz, TimeSec, StepH = 0.09) {
  if (Quantity === "Curl" || Quantity === "Divergence") {
    const Inv2H = 0.5 / StepH;
    const Xp = EvaluateSceneForceFieldAt(ForceNodes, Px + StepH, Py, Pz, TimeSec);
    const Xm = EvaluateSceneForceFieldAt(ForceNodes, Px - StepH, Py, Pz, TimeSec);
    const Yp = EvaluateSceneForceFieldAt(ForceNodes, Px, Py + StepH, Pz, TimeSec);
    const Ym = EvaluateSceneForceFieldAt(ForceNodes, Px, Py - StepH, Pz, TimeSec);
    const Zp = EvaluateSceneForceFieldAt(ForceNodes, Px, Py, Pz + StepH, TimeSec);
    const Zm = EvaluateSceneForceFieldAt(ForceNodes, Px, Py, Pz - StepH, TimeSec);
    if (Quantity === "Divergence") {
      return ((Xp[0] - Xm[0]) + (Yp[1] - Ym[1]) + (Zp[2] - Zm[2])) * Inv2H * 2.5;
    }
    const Cx = (Yp[2] - Ym[2] - (Zp[1] - Zm[1])) * Inv2H;
    const Cy = (Zp[0] - Zm[0] - (Xp[2] - Xm[2])) * Inv2H;
    const Cz = (Xp[1] - Xm[1] - (Yp[0] - Ym[0])) * Inv2H;
    return Math.hypot(Cx, Cy, Cz) * 0.82;
  }
  const V = EvaluateSceneForceFieldAt(ForceNodes, Px, Py, Pz, TimeSec);
  const Spd = Math.hypot(V[0], V[1], V[2]);
  if (Quantity === "Pressure") return 0.5 * 1.225 * Spd * Spd * 0.12;
  return Spd;
}

function BuildHeatmapSliceMesh(Buf, ForceNodes, Bounds, AxisMode, SliceOffset, Settings, TimeSec) {
  const Res = 40;
  const Quantity = Settings.Quantity || "Speed";
  const Colormap = Settings.Colormap || Settings.ColorRamp || "Engineering";
  const FixedMax = Math.max(2.0, Number(Settings.FixedMax) || 24.0);
  const MaxOpacity = Math.max(0.08, Math.min(0.95, Number(Settings.HeatmapOpacity) || 0.62));
  const IsSigned = Quantity === "Divergence";

  const SpanX = Bounds.MaxX - Bounds.MinX;
  const SpanY = Bounds.MaxY - Bounds.MinY;
  const SpanZ = Bounds.MaxZ - Bounds.MinZ;

  const PointAt = (Iu, Iv) => {
    const U = Iu / Res;
    const V = Iv / Res;
    if (AxisMode === "XZ") {
      return [Bounds.MinX + U * SpanX, Bounds.CenterY + SliceOffset, Bounds.MinZ + V * SpanZ];
    }
    if (AxisMode === "YZ") {
      return [Bounds.CenterX + SliceOffset, Bounds.MinY + U * SpanY, Bounds.MinZ + V * SpanZ];
    }
    // XY horizontal ground-parallel plane in +Z-up world (anchored just above Gallery floor -0.58 so concentric iso-contours read clearly from camera!)
    const ZPlane = Math.max(-0.61, -0.56 + SliceOffset);
    return [Bounds.MinX + U * SpanX, Bounds.MinY + V * SpanY, ZPlane];
  };

  const Stride = Res + 1;
  const Pts = new Array(Stride * Stride);
  const Shares = new Float32Array(Stride * Stride);

  for (let Iv = 0; Iv <= Res; ++Iv) {
    for (let Iu = 0; Iu <= Res; ++Iu) {
      const Idx = Iv * Stride + Iu;
      const P = PointAt(Iu, Iv);
      Pts[Idx] = P;
      // Sample XY slice through the active mid-plane height (BaseSliceZ + SliceOffset) when projecting onto the floor carpet!
      const EvalZ = AxisMode === "XY" ? Bounds.BaseSliceZ + SliceOffset : P[2];
      const Val = EvaluateSliceQuantityAt(ForceNodes, Quantity, P[0], P[1], EvalZ, TimeSec);
      Shares[Idx] = IsSigned
        ? Math.max(0, Math.min(1, 0.5 + Val / (FixedMax * 1.4)))
        : Math.max(0, Math.min(1, Val / FixedMax));
    }
  }

  const Colors = new Array(Stride * Stride);
  for (let Iv = 0; Iv <= Res; ++Iv) {
    for (let Iu = 0; Iu <= Res; ++Iu) {
      const Idx = Iv * Stride + Iu;
      const Share = Shares[Idx];
      let Rgb = SampleFieldColormap(IsSigned ? "Diverging" : Colormap, Share);

      // Sharp iso-contour rings every 10% share (matching SLICE_DISPLAY_FRAG & C++ 01-ForceFieldLitPbrAndOverlay.png!)
      const SLeft = Shares[Iv * Stride + Math.max(0, Iu - 1)];
      const SRight = Shares[Iv * Stride + Math.min(Res, Iu + 1)];
      const SDown = Shares[Math.max(0, Iv - 1) * Stride + Iu];
      const SUp = Shares[Math.min(Res, Iv + 1) * Stride + Iu];
      const GradIso = Math.hypot((SRight - SLeft) * 5.0, (SUp - SDown) * 5.0);
      const IsoCoord = Share * 10.0;
      const Frac = IsoCoord - Math.floor(IsoCoord);
      const DistToBand = Math.min(Frac, 1.0 - Frac) / Math.max(0.035, GradIso * 1.25);
      const IsoLine = GradIso > 0.008 ? Math.max(0, 1.0 - DistToBand * 0.95) : 0.0;
      Rgb = [
        Rgb[0] + (0.96 - Rgb[0]) * IsoLine * 0.28,
        Rgb[1] + (0.97 - Rgb[1]) * IsoLine * 0.28,
        Rgb[2] + (0.99 - Rgb[2]) * IsoLine * 0.28,
      ];

      const Activity = IsSigned ? Math.min(1, Math.abs(Share - 0.5) * 3.4) : Math.min(1, Math.max(0, (Share - 0.025) / 0.16));
      const SmoothAct = Activity * Activity * (3 - 2 * Activity);
      const Alpha = SmoothAct * MaxOpacity + IsoLine * SmoothAct * 0.14;
      Colors[Idx] = [Rgb[0], Rgb[1], Rgb[2], Alpha];
    }
  }

  for (let Iv = 0; Iv < Res; ++Iv) {
    for (let Iu = 0; Iu < Res; ++Iu) {
      const I00 = Iv * Stride + Iu;
      const I10 = I00 + 1;
      const I01 = I00 + Stride;
      const I11 = I01 + 1;
      if (Colors[I00][3] < 0.01 && Colors[I10][3] < 0.01 && Colors[I01][3] < 0.01 && Colors[I11][3] < 0.01) {
        continue;
      }
      PushSmoothTri12(Buf, Pts[I00], Colors[I00], Pts[I10], Colors[I10], Pts[I11], Colors[I11]);
      PushSmoothTri12(Buf, Pts[I00], Colors[I00], Pts[I11], Colors[I11], Pts[I01], Colors[I01]);
    }
  }
}

// Bidirectional 4th-order Runge-Kutta (RK4) streamline integrator through the composite multi-source field
function TraceBidirectionalStreamline(ForceNodes, SeedPos, Bounds, StepsPerDir, StepLen, TimeSec) {
  const TraceHalf = (Sign) => {
    const Out = [];
    let X = SeedPos[0], Y = SeedPos[1], Z = SeedPos[2];
    for (let S = 0; S < StepsPerDir; ++S) {
      const K1 = EvaluateSceneForceFieldAt(ForceNodes, X, Y, Z, TimeSec);
      const M1 = Math.hypot(K1[0], K1[1], K1[2]);
      if (M1 < 0.18) break;
      const D1x = (K1[0] / M1) * Sign, D1y = (K1[1] / M1) * Sign, D1z = (K1[2] / M1) * Sign;

      const K2 = EvaluateSceneForceFieldAt(ForceNodes, X + D1x * StepLen * 0.5, Y + D1y * StepLen * 0.5, Z + D1z * StepLen * 0.5, TimeSec);
      const M2 = Math.hypot(K2[0], K2[1], K2[2]);
      if (M2 < 0.18) break;
      const D2x = (K2[0] / M2) * Sign, D2y = (K2[1] / M2) * Sign, D2z = (K2[2] / M2) * Sign;

      const K3 = EvaluateSceneForceFieldAt(ForceNodes, X + D2x * StepLen * 0.5, Y + D2y * StepLen * 0.5, Z + D2z * StepLen * 0.5, TimeSec);
      const M3 = Math.hypot(K3[0], K3[1], K3[2]);
      if (M3 < 0.18) break;
      const D3x = (K3[0] / M3) * Sign, D3y = (K3[1] / M3) * Sign, D3z = (K3[2] / M3) * Sign;

      const K4 = EvaluateSceneForceFieldAt(ForceNodes, X + D3x * StepLen, Y + D3y * StepLen, Z + D3z * StepLen, TimeSec);
      const M4 = Math.hypot(K4[0], K4[1], K4[2]);
      if (M4 < 0.18) break;
      const D4x = (K4[0] / M4) * Sign, D4y = (K4[1] / M4) * Sign, D4z = (K4[2] / M4) * Sign;

      const Nx = X + ((D1x + 2 * D2x + 2 * D3x + D4x) / 6.0) * StepLen;
      const Ny = Y + ((D1y + 2 * D2y + 2 * D3y + D4y) / 6.0) * StepLen;
      const Nz = Z + ((D1z + 2 * D2z + 2 * D3z + D4z) / 6.0) * StepLen;
      if (
        Nx < Bounds.MinX - 0.3 || Nx > Bounds.MaxX + 0.3 ||
        Ny < Bounds.MinY - 0.3 || Ny > Bounds.MaxY + 0.3 ||
        Nz < Bounds.MinZ - 0.2 || Nz > Bounds.MaxZ + 0.4
      ) {
        break;
      }
      X = Nx; Y = Ny; Z = Nz;
      Out.push([X, Y, Z, 0.5 * (M1 + M4)]);
    }
    return Out;
  };

  const Back = TraceHalf(-1);
  const V0 = EvaluateSceneForceFieldAt(ForceNodes, SeedPos[0], SeedPos[1], SeedPos[2], TimeSec);
  const M0 = Math.hypot(V0[0], V0[1], V0[2]);
  if (M0 < 0.18) return [];
  const Fwd = TraceHalf(+1);

  const Curve = [];
  for (let I = Back.length - 1; I >= 0; --I) Curve.push(Back[I]);
  Curve.push([SeedPos[0], SeedPos[1], SeedPos[2], M0]);
  for (let I = 0; I < Fwd.length; ++I) Curve.push(Fwd[I]);
  return Curve;
}

function ResolveStreamlineRgb(Settings, Share01, ZHeight, Bounds) {
  const Mode = Settings.StreamColorMode || "CyanSingle";
  if (Mode === "CyanSingle") return [0.408, 0.863, 1.0]; // #68dcff Classic Cyan
  if (Mode === "CustomSingle") return HexToRgb01(Settings.StreamSingleColor || "#68dcff");
  if (Mode === "Elevation") {
    const HShare = Math.max(0, Math.min(1, (ZHeight - Bounds.MinZ) / Math.max(0.4, Bounds.MaxZ - Bounds.MinZ)));
    return LerpRgb([[0.18, 0.62, 0.98], [0.33, 0.90, 0.68], [1.0, 0.80, 0.34]], HShare);
  }
  return SampleFieldColormap(Settings.Colormap || Settings.ColorRamp || "Engineering", Share01);
}

export function BuildForceOverlayGeometry(
  ForceNodes,
  OverlaySettings = DEFAULT_FORCE_OVERLAY,
  SelectedId = null,
  TimeSec = 0,
  CameraOrigin = [0.0, -3.55, 0.42],
) {
  const Settings = { ...DEFAULT_FORCE_OVERLAY, ...(OverlaySettings || {}) };
  const OpaqueVerts = [];
  const TranslucentVerts = [];
  const PickBeacons = [];

  if (!Settings.Enabled || !Array.isArray(ForceNodes) || ForceNodes.length === 0) {
    return {
      OpaqueBuffer: new Float32Array(0),
      OpaqueVertexCount: 0,
      TranslucentBuffer: new Float32Array(0),
      TranslucentVertexCount: 0,
      PickBeacons,
    };
  }

  const ActiveNodes = [];
  let MinX = Infinity, MinY = Infinity, MinZ = Infinity;
  let MaxX = -Infinity, MaxY = -Infinity, MaxZ = -Infinity;
  let WeightedZ = 0, WeightSum = 0;

  for (const Node of ForceNodes) {
    if (!Node || Node.Hidden) continue;
    const F = ResolveForceValues(Node.Force);
    const Meta = GetForceModelMeta(F.Model);
    const BaseCenter = Node.Position || [0, 0, 0.65];
    const Center = EvaluateMotionCenter(BaseCenter, F, TimeSec);
    const IsSelected = Node.Id === SelectedId;
    const DomainRgb = HexToRgb01(Meta.Color);

    const BeaconR = Math.max(0.065, Math.min(0.14, F.Radius * 0.058));
    PickBeacons.push({
      Id: Node.Id,
      Center,
      Radius: BeaconR * 2.2,
    });

    // Central glowing 3D sphere beacon (amber-gold when selected, domain color otherwise)
    const BeaconRgba = IsSelected
      ? [1.0, 0.76, 0.28, 1.0]
      : [
          Math.min(1, DomainRgb[0] * 1.15 + 0.08),
          Math.min(1, DomainRgb[1] * 1.15 + 0.08),
          Math.min(1, DomainRgb[2] * 1.15 + 0.08),
          1.0,
        ];
    PushSolidSphere(OpaqueVerts, Center, BeaconR, BeaconRgba, 7, 12, 0.28);

    if (!F.Enabled) continue;

    ActiveNodes.push({ Node, F, Meta, BaseCenter, Center, IsSelected, DomainRgb });
    const BoundR = Math.min(F.Radius * 1.08, 6.5);
    MinX = Math.min(MinX, Center[0] - BoundR);
    MaxX = Math.max(MaxX, Center[0] + BoundR);
    MinY = Math.min(MinY, Center[1] - BoundR);
    MaxY = Math.max(MaxY, Center[1] + BoundR);
    MinZ = Math.min(MinZ, Math.max(-0.48, Center[2] - BoundR * 0.85));
    MaxZ = Math.max(MaxZ, Center[2] + BoundR * 0.95);
    WeightedZ += Math.max(-0.42, Center[2] - Math.min(0.45, F.Radius * 0.24));
    WeightSum += 1;
  }

  if (ActiveNodes.length === 0) {
    return {
      OpaqueBuffer: new Float32Array(OpaqueVerts),
      OpaqueVertexCount: OpaqueVerts.length / 12,
      TranslucentBuffer: new Float32Array(TranslucentVerts),
      TranslucentVertexCount: TranslucentVerts.length / 12,
      PickBeacons,
    };
  }

  const Bounds = {
    MinX, MaxX, MinY, MaxY, MinZ, MaxZ,
    CenterX: 0.5 * (MinX + MaxX),
    CenterY: 0.5 * (MinY + MaxY),
    CenterZ: 0.5 * (MinZ + MaxZ),
    BaseSliceZ: WeightSum > 0 ? WeightedZ / WeightSum : 0.2,
  };
  const FixedMax = Math.max(2.0, Number(Settings.FixedMax) || 24.0);

  // 1. GPU Iso-Contour Heatmap Cross-Section Slice(s) (drawn first into TranslucentBuffer so shells & ribbons layer cleanly over it!)
  const ShowHeatmap = Settings.ShowHeatmap !== false && Settings.SliceEnabled !== false;
  if (ShowHeatmap) {
    const AxisMode = Settings.SliceAxis || "XY";
    const Offset = Number(Settings.SliceOffset) || 0.0;
    if (AxisMode === "ALL") {
      BuildHeatmapSliceMesh(TranslucentVerts, ForceNodes, Bounds, "XY", Offset, Settings, TimeSec);
      BuildHeatmapSliceMesh(TranslucentVerts, ForceNodes, Bounds, "XZ", Offset, Settings, TimeSec);
      BuildHeatmapSliceMesh(TranslucentVerts, ForceNodes, Bounds, "YZ", Offset, Settings, TimeSec);
    } else {
      BuildHeatmapSliceMesh(TranslucentVerts, ForceNodes, Bounds, AxisMode, Offset, Settings, TimeSec);
    }
  }

  // 2. Motion Trajectory Curves + Waypoint Spheres (A, P1, ..., B)
  if (Settings.ShowMotionPaths) {
    for (const Item of ActiveNodes) {
      const { F, BaseCenter, IsSelected } = Item;
      if (F.MotionTopology === "Static") continue;
      const PathColor = IsSelected ? [1.0, 0.68, 0.24, 0.92] : [0.95, 0.62, 0.28, 0.58];
      const Segs = 32;
      for (let S = 0; S < Segs; ++S) {
        const P0 = EvaluateMotionPositionAtU(BaseCenter, F, S / Segs);
        const P1 = EvaluateMotionPositionAtU(BaseCenter, F, (S + 1) / Segs);
        PushTubeSegment(OpaqueVerts, P0, P1, IsSelected ? 0.014 : 0.010, PathColor, 4, 0.1);
      }
      if (Array.isArray(F.MotionWaypoints) && F.MotionWaypoints.length >= 2) {
        for (let W = 0; W < F.MotionWaypoints.length; ++W) {
          const Wp = F.MotionWaypoints[W];
          PushSolidSphere(OpaqueVerts, Wp, IsSelected ? 0.042 : 0.032, [1.0, 0.54, 0.16, 1.0], 6, 10, 0.25);
        }
      }
    }
  }

  // 3. 3D Composite Lattice Velocity Vector Arrows
  if (Settings.ShowVelocityArrows) {
    const GridN = Math.max(3, Math.min(8, (Number(Settings.ArrowDensity) || 4) + 2));
    const Layers = Math.max(1, Math.min(3, Number(Settings.VolumeLayers) || 1));
    const SpanX = Bounds.MaxX - Bounds.MinX;
    const SpanY = Bounds.MaxY - Bounds.MinY;
    const CellStep = Math.max(SpanX, SpanY) / GridN;
    const Colormap = Settings.Colormap || Settings.ColorRamp || "Engineering";

    for (let L = 0; L < Layers; ++L) {
      const ZSample =
        Layers === 1
          ? Math.max(-0.38, Bounds.BaseSliceZ + (Number(Settings.SliceOffset) || 0) + 0.04)
          : Bounds.MinZ + ((L + 0.5) / Layers) * (Bounds.MaxZ - Bounds.MinZ);
      for (let Iy = 0; Iy < GridN; ++Iy) {
        for (let Ix = 0; Ix < GridN; ++Ix) {
          const Sx = Bounds.MinX + ((Ix + 0.5) / GridN) * SpanX;
          const Sy = Bounds.MinY + ((Iy + 0.5) / GridN) * SpanY;
          const Vec = EvaluateSceneForceFieldAt(ForceNodes, Sx, Sy, ZSample, TimeSec);
          const Spd = Math.hypot(Vec[0], Vec[1], Vec[2]);
          if (Spd < 0.28) continue;
          const Share = Math.min(1, Spd / FixedMax);
          const Dir = [Vec[0] / Spd, Vec[1] / Spd, Vec[2] / Spd];
          const ArrowLen = Math.min(CellStep * 0.82, Math.max(0.11, (0.35 + 0.95 * Share) * (Settings.VectorScale || 0.26) * 1.35));
          const Rgb = SampleFieldColormap(Colormap, Share);
          PushArrow3D(OpaqueVerts, [Sx, Sy, ZSample], Dir, ArrowLen, 0.0085 + 0.004 * Share, [Rgb[0], Rgb[1], Rgb[2], 1.0]);
        }
      }
    }
  }

  // 4. Shared Composite RK4 Curves for Streamline Ribbons & Animated Bokeh DOF Ribbons
  const WantStreams = Settings.ShowStreamlines !== false;
  const WantBokeh = Boolean(Settings.ShowBokehRibbons);
  if (WantStreams || WantBokeh) {
    const TotalSeeds = Math.max(24, Math.min(240, Number(Settings.StreamLineCount) || 96));
    const StepsPerDir = Math.max(10, Math.min(52, Math.round((Number(Settings.StreamResolution) || 28) * 0.65)));
    const AvgRadius = ActiveNodes.reduce((Acc, It) => Acc + It.F.Radius, 0) / ActiveNodes.length;
    const StepLen = Math.max(0.045, Math.min(0.16, AvgRadius * 0.068));
    const GoldenAngle = Math.PI * (3 - Math.sqrt(5));
    const Curves = [];

    const SeedsPerNode = Math.max(8, Math.ceil(TotalSeeds / ActiveNodes.length));
    for (let NIdx = 0; NIdx < ActiveNodes.length; ++NIdx) {
      const { Center, F } = ActiveNodes[NIdx];
      const RSeedMax = Math.min(F.Radius * 0.86, 4.5);
      for (let S = 0; S < SeedsPerNode && Curves.length < TotalSeeds; ++S) {
        const Frac = (S + 0.5) / SeedsPerNode;
        const ZNorm = 1 - 2 * Frac;
        const RadXY = Math.sqrt(Math.max(0, 1 - ZNorm * ZNorm));
        const Theta = S * GoldenAngle + NIdx * 1.17;
        const ShellFrac = 0.24 + 0.68 * (((S * 0.6180339887) % 1));
        const Seed = [
          Center[0] + Math.cos(Theta) * RadXY * RSeedMax * ShellFrac,
          Center[1] + Math.sin(Theta) * RadXY * RSeedMax * ShellFrac,
          Math.max(-0.40, Center[2] + ZNorm * RSeedMax * ShellFrac * 0.78),
        ];
        const Curve = TraceBidirectionalStreamline(ForceNodes, Seed, Bounds, StepsPerDir, StepLen, TimeSec);
        if (Curve.length >= 4) Curves.push({ Curve, SeedIdx: Curves.length });
      }
    }

    // 4A. Bidirectional RK4 Streamline Ribbons with Travelling Comet Spark
    if (WantStreams) {
      const BaseHalfW = 0.0085 * Math.max(0.25, Number(Settings.StreamThickness) || 0.78);
      const Emissive = Math.max(0.25, Number(Settings.StreamEmissive) || 1.05);
      const Opacity = Math.max(0.15, Math.min(1.0, Number(Settings.StreamOpacity) || 0.92));

      for (const { Curve, SeedIdx } of Curves) {
        const Len = Curve.length;
        const StrandSeed = ((SeedIdx * 0.3719) % 1);
        const SpeedRate = 0.22 + 0.14 * ((SeedIdx * 0.713) % 1);
        const HeadPos = (((StrandSeed + TimeSec * SpeedRate) % 1) + 1) % 1 * 1.28;

        for (let I = 0; I < Len - 1; ++I) {
          const P0 = Curve[I], P1 = Curve[I + 1];
          const U0 = I / (Len - 1), U1 = (I + 1) / (Len - 1);
          const Taper0 = Math.pow(Math.sin(Math.PI * U0), 0.38);
          const Taper1 = Math.pow(Math.sin(Math.PI * U1), 0.38);
          const Share0 = Math.min(1, P0[3] / FixedMax);
          const Share1 = Math.min(1, P1[3] / FixedMax);

          const BaseRgb0 = ResolveStreamlineRgb(Settings, Share0, P0[2], Bounds);
          const BaseRgb1 = ResolveStreamlineRgb(Settings, Share1, P1[2], Bounds);

          const EvalLit = (U, BaseRgb) => {
            const Dist = HeadPos - U;
            const Streak = Dist >= 0 && Dist <= 0.28 ? Math.pow(1 - Dist / 0.28, 1.6) * Math.min(1, Dist / 0.04) : 0;
            const Gain = (0.68 + 0.72 * Streak) * Emissive;
            return [
              Math.min(1, BaseRgb[0] * Gain + Streak * 0.22),
              Math.min(1, BaseRgb[1] * Gain + Streak * 0.24),
              Math.min(1, BaseRgb[2] * Gain + Streak * 0.26),
              Math.min(0.96, (0.55 + 0.42 * Streak) * Opacity),
            ];
          };

          const Col0 = EvalLit(U0, BaseRgb0);
          const Col1 = EvalLit(U1, BaseRgb1);
          Col0[3] *= Taper0;
          Col1[3] *= Taper1;
          PushRibbonQuad(
            TranslucentVerts,
            P0,
            P1,
            BaseHalfW * (0.45 + 0.75 * Taper0),
            BaseHalfW * (0.45 + 0.75 * Taper1),
            Col0,
            Col1,
            CameraOrigin,
          );
        }
      }
    }

    // 4B. Live Advected Bokeh DOF Stream Ribbons + Glowing Head Beads (from VectorFieldVisualizer)
    if (WantBokeh && Curves.length > 0) {
      const BokehCount = Math.max(24, Math.min(260, Math.round((Number(Settings.BokehCount) || 120) * 0.65)));
      const BokehSpeed = Math.max(0.1, Number(Settings.BokehSpeed) || 0.55);
      const BokehThick = Math.max(0.35, Number(Settings.BokehThickness) || 1.0);
      const BokehGlow = Math.max(0.35, Number(Settings.BokehEmissive) || 1.05);
      const FollowStreams = Settings.BokehPalette === "FollowStreams";

      for (let B = 0; B < BokehCount; ++B) {
        const CurveEntry = Curves[B % Curves.length];
        const Pts = CurveEntry.Curve;
        const N = Pts.length;
        if (N < 5) continue;

        const Phase = (((B * 0.2937 + TimeSec * BokehSpeed * (0.32 + 0.18 * ((B * 0.618) % 1))) % 1) + 1) % 1;
        const HeadFloat = Phase * (N - 1);
        const HeadIdx = Math.min(N - 1, Math.max(1, Math.floor(HeadFloat)));
        const WakeLen = Math.min(HeadIdx, Math.max(3, Math.round(N * 0.28)));
        const StartIdx = HeadIdx - WakeLen;
        const HeadPt = Pts[HeadIdx];
        const HeadShare = Math.min(1, HeadPt[3] / FixedMax);
        const EndFade = Math.sin(Math.PI * Phase);
        if (EndFade < 0.06) continue;

        const MidRgb = FollowStreams
          ? ResolveStreamlineRgb(Settings, HeadShare, HeadPt[2], Bounds)
          : [0.16, 0.88, 0.98]; // Luminous Cyan-Teal Bokeh
        const TailRgb = FollowStreams
          ? [MidRgb[0] * 0.45, MidRgb[1] * 0.45, MidRgb[2] * 0.45]
          : [0.06, 0.54, 0.82]; // Deep Cerulean Wake
        const HeadRgb = [
          Math.min(1, (FollowStreams ? MidRgb[0] * 0.45 + 0.55 : 0.82) * BokehGlow),
          Math.min(1, (FollowStreams ? MidRgb[1] * 0.45 + 0.55 : 0.99) * BokehGlow),
          Math.min(1, (FollowStreams ? MidRgb[2] * 0.45 + 0.55 : 1.0) * BokehGlow),
        ];

        for (let K = StartIdx; K < HeadIdx; ++K) {
          const W0 = (K - StartIdx) / WakeLen;
          const W1 = (K + 1 - StartIdx) / WakeLen;
          const C0 = [
            TailRgb[0] + (MidRgb[0] - TailRgb[0]) * W0,
            TailRgb[1] + (MidRgb[1] - TailRgb[1]) * W0,
            TailRgb[2] + (MidRgb[2] - TailRgb[2]) * W0,
            0.68 * W0 * W0 * EndFade,
          ];
          const C1 = [
            TailRgb[0] + (HeadRgb[0] - TailRgb[0]) * W1,
            TailRgb[1] + (HeadRgb[1] - TailRgb[1]) * W1,
            TailRgb[2] + (HeadRgb[2] - TailRgb[2]) * W1,
            0.84 * W1 * W1 * EndFade,
          ];
          PushRibbonQuad(
            TranslucentVerts,
            Pts[K],
            Pts[K + 1],
            0.003 * BokehThick + 0.012 * BokehThick * W0,
            0.003 * BokehThick + 0.015 * BokehThick * W1,
            C0,
            C1,
            CameraOrigin,
          );
        }

        // Creamy DOF Bokeh Halo Disc + Sharp Luminous Core Bead
        const DofRadius = (0.024 + 0.026 * ((B * 0.417) % 1)) * BokehThick;
        PushBillboardDisc(
          TranslucentVerts,
          HeadPt,
          DofRadius,
          [HeadRgb[0], HeadRgb[1], HeadRgb[2], 0.42 * EndFade],
          [MidRgb[0], MidRgb[1], MidRgb[2], 0.0],
          CameraOrigin,
          8,
        );
        PushBillboardDisc(
          TranslucentVerts,
          HeadPt,
          DofRadius * 0.36,
          [1.0, 1.0, 1.0, 0.92 * EndFade],
          [HeadRgb[0], HeadRgb[1], HeadRgb[2], 0.05],
          CameraOrigin,
          6,
        );
      }
    }
  }

  // 5. Outer Translucent Fresnel Volumetric Reach Shells & Subtle Meridian Rings (drawn last so Fresnel rim wraps the volume!)
  if (Settings.ShowEnvelopes) {
    for (const Item of ActiveNodes) {
      const { F, Center, IsSelected, DomainRgb } = Item;
      if (F.Reach === "Infinite") continue;
      const R = F.Radius;
      const ShellTint = IsSelected
        ? [0.96, 0.78, 0.36]
        : [
            0.35 * DomainRgb[0] + 0.25,
            0.35 * DomainRgb[1] + 0.62,
            0.35 * DomainRgb[2] + 0.68,
          ];
      const RingRgba = [ShellTint[0], ShellTint[1], ShellTint[2], IsSelected ? 0.52 : 0.26];
      const WireR = IsSelected ? 0.0075 : 0.005;

      if (F.Reach === "Column") {
        const HalfH = R * 0.95;
        PushFresnelColumnShell(TranslucentVerts, Center, R, HalfH, ShellTint, IsSelected, CameraOrigin, 24);
        PushCircleRing(TranslucentVerts, [Center[0], Center[1], Center[2] - HalfH], [1, 0, 0], [0, 1, 0], R, WireR, RingRgba, 32);
        PushCircleRing(TranslucentVerts, [Center[0], Center[1], Center[2] + HalfH], [1, 0, 0], [0, 1, 0], R, WireR, RingRgba, 32);
      } else if (F.Reach === "Box") {
        const Corners = [
          [-R, -R, -R], [R, -R, -R], [R, R, -R], [-R, R, -R],
          [-R, -R, R],  [R, -R, R],  [R, R, R],  [-R, R, R],
        ].map((O) => [Center[0] + O[0], Center[1] + O[1], Center[2] + O[2]]);
        const Edges = [
          [0, 1], [1, 2], [2, 3], [3, 0],
          [4, 5], [5, 6], [6, 7], [7, 4],
          [0, 4], [1, 5], [2, 6], [3, 7],
        ];
        for (const [I0, I1] of Edges) {
          PushTubeSegment(TranslucentVerts, Corners[I0], Corners[I1], WireR * 1.2, RingRgba, 3, 0.0);
        }
      } else {
        PushFresnelSphereShell(TranslucentVerts, Center, R, ShellTint, IsSelected, CameraOrigin, 14, 24);
        PushCircleRing(TranslucentVerts, Center, [1, 0, 0], [0, 1, 0], R, WireR, RingRgba, 36);
        if (IsSelected && F.CoreRadius > 0.12 && F.CoreRadius < R * 0.95) {
          PushCircleRing(TranslucentVerts, Center, [1, 0, 0], [0, 1, 0], F.CoreRadius, WireR * 0.85, [1.0, 0.85, 0.45, 0.38], 24, true);
        }
      }
    }
  }

  // 6. 3D Probe Sample Marker (subtle emerald-cyan crosshair sphere at ProbePosition)
  if (Settings.ShowProbeHud !== false && Array.isArray(Settings.ProbePosition)) {
    const P = Settings.ProbePosition;
    PushSolidSphere(OpaqueVerts, P, 0.036, [0.45, 0.96, 0.82, 1.0], 6, 10, 0.2);
    PushCircleRing(TranslucentVerts, P, [1, 0, 0], [0, 1, 0], 0.095, 0.0045, [0.45, 0.96, 0.82, 0.65], 20);
  }

  return {
    OpaqueBuffer: new Float32Array(OpaqueVerts),
    OpaqueVertexCount: OpaqueVerts.length / 12,
    TranslucentBuffer: new Float32Array(TranslucentVerts),
    TranslucentVertexCount: TranslucentVerts.length / 12,
    PickBeacons,
  };
}

export function PickForceBeacon(RayOrigin, RayDir, ForceOverlay) {
  if (!ForceOverlay || !Array.isArray(ForceOverlay.PickBeacons)) return null;
  let BestT = Infinity;
  let BestId = null;
  for (const B of ForceOverlay.PickBeacons) {
    const Ox = RayOrigin[0] - B.Center[0];
    const Oy = RayOrigin[1] - B.Center[1];
    const Oz = RayOrigin[2] - B.Center[2];
    const Proj = Ox * RayDir[0] + Oy * RayDir[1] + Oz * RayDir[2];
    const C = Ox * Ox + Oy * Oy + Oz * Oz - B.Radius * B.Radius;
    const Disc = Proj * Proj - C;
    if (Disc < 0) continue;
    const T = -Proj - Math.sqrt(Disc);
    if (T > 0.02 && T < BestT) {
      BestT = T;
      BestId = B.Id;
    }
  }
  return BestId ? { Id: BestId, Distance: BestT } : null;
}

//------------------------------------------------------------------------------------------------------------------------
//                      SPACECONTAINER (.FORCEMODEL / .VECTORFIELDSPACE) TOML CODEC
//------------------------------------------------------------------------------------------------------------------------

export function SerializeForceModelToml(SubjectName, ForceValues, Transform = {}) {
  const F = ResolveForceValues(ForceValues);
  const Meta = GetForceModelMeta(F.Model);
  const Pos = Transform.Position || [0, 0, 1.4];
  const Rot = Transform.Rotation || [0, 0, 0];
  const Esc = (S) => String(S || "").replace(/\\/g, "\\\\").replace(/"/g, '\\"');
  const Lines = [
    "# Frontier SpaceContainer (.ForceModel / .VectorFieldSpace)",
    "[SpaceContainer]",
    'MagicSignature = "FRSP1"',
    `ContainerExtension = "${F.FormulaEnabled ? ".VectorFieldSpace" : ".ForceModel"}"`,
    `SubFormat = "${F.FormulaEnabled ? "VectorField" : "ForceModel"}"`,
    "SchemaVersion = 1",
    "",
    "[ForceSource]",
    `Title = "${Esc(SubjectName || Meta.Name)}"`,
    `Model = "${F.Model}"`,
    `ModelOrdinal = ${Meta.Ordinal}`,
    `Domain = "${Meta.Domain}"`,
    `Enabled = ${F.Enabled ? "true" : "false"}`,
    `Strength = ${F.Strength.toFixed(3)}`,
    `RadiusMeters = ${F.Radius.toFixed(3)}`,
    `CoreRadiusMeters = ${F.CoreRadius.toFixed(3)}`,
    `FalloffExponent = ${F.FalloffExponent.toFixed(3)}`,
    `Attenuation = "${F.Attenuation}"`,
    `ReachEnvelope = "${F.Reach}"`,
    `DurationMode = "${F.Duration}"`,
    `SwirlRatio = ${F.SwirlRatio.toFixed(3)}`,
    `TurbulenceScaleMeters = ${F.TurbulenceScale.toFixed(3)}`,
    `OctaveCount = ${F.Octaves}`,
    `FrequencyHertz = ${F.Frequency.toFixed(3)}`,
    `YawDegrees = ${F.YawDeg.toFixed(1)}`,
    `PitchDegrees = ${F.PitchDeg.toFixed(1)}`,
    `CentreMeters = [${Pos[0].toFixed(3)}, ${Pos[1].toFixed(3)}, ${Pos[2].toFixed(3)}]`,
    `EulerDegrees = [${Rot[0].toFixed(1)}, ${Rot[1].toFixed(1)}, ${Rot[2].toFixed(1)}]`,
    `MotionTopology = "${F.MotionTopology}"`,
    `MotionLoopMode = "${F.MotionLoopMode}"`,
    `MotionAmplitudeMeters = ${F.MotionAmplitude.toFixed(3)}`,
    `MotionSpeedHertz = ${F.MotionSpeedHz.toFixed(3)}`,
    `FormulaEnabled = ${F.FormulaEnabled ? "true" : "false"}`,
    `FormulaSource = "${Esc(F.FormulaCode).replace(/\n/g, "\\n")}"`,
    "",
  ];
  return Lines.join("\n");
}

export function SerializeSimulationSpaceToml(ForceNodes = [], DocumentTitle = "VectorFieldWorkspace") {
  const Esc = (S) => String(S || "").replace(/\\/g, "\\\\").replace(/"/g, '\\"');
  const Lines = [
    "[VectorFieldSpace]",
    `DocumentTitle = "${Esc(DocumentTitle)}"`,
    'CoordinateSystem = "RightHandZUp"',
    "CascadeResolution = 16",
    "",
  ];
  for (const Node of ForceNodes) {
    if (!Node) continue;
    const F = ResolveForceValues(Node.Force);
    const Meta = GetForceModelMeta(F.Model);
    const Pos = Node.Position || [0, 0, 0.6];
    Lines.push(
      "[[ForceSources]]",
      `Label = "${Esc(Node.Name || Meta.Name)}"`,
      `Model = "${F.Model}"`,
      `Domain = "${Meta.Domain}"`,
      `Enabled = ${F.Enabled && !Node.Hidden ? "true" : "false"}`,
      `RadiusMetres = ${F.Radius.toFixed(2)}`,
      `CoreRadiusMetres = ${F.CoreRadius.toFixed(2)}`,
      `Strength = ${F.Strength.toFixed(2)}`,
      `ReachShape = "${F.Reach}"`,
      `Attenuation = "${F.Attenuation}"`,
      `Duration = "${F.Duration}"`,
      `CentreMetres = [${Pos[0].toFixed(2)}, ${Pos[1].toFixed(2)}, ${Pos[2].toFixed(2)}]`,
      `MotionTopology = "${F.MotionTopology}"`,
      `FormulaEnabled = ${F.FormulaEnabled ? "true" : "false"}`,
      "",
    );
  }
  return Lines.join("\n");
}

export function ParseForceModelToml(TomlText, FallbackName = "Imported Force") {
  const Text = String(TomlText || "");
  const ReadStr = (Key, Def = "") => {
    const M = Text.match(new RegExp(`${Key}\\s*=\\s*"([^"]*)"`, "i"));
    return M ? M[1].replace(/\\n/g, "\n").replace(/\\"/g, '"') : Def;
  };
  const ReadNum = (Key, Def) => {
    const M = Text.match(new RegExp(`${Key}\\s*=\\s*([-+]?[0-9]*\\.?[0-9]+)`, "i"));
    return M ? Number(M[1]) : Def;
  };
  const ReadBool = (Key, Def) => {
    const M = Text.match(new RegExp(`${Key}\\s*=\\s*(true|false)`, "i"));
    return M ? M[1].toLowerCase() === "true" : Def;
  };
  const ReadVec3 = (Key, Def) => {
    const M = Text.match(new RegExp(`${Key}\\s*=\\s*\\[([^\\]]+)\\]`, "i"));
    if (!M) return Def;
    const Parts = M[1].split(",").map((S) => Number(S.trim()));
    return Parts.length >= 3 && Parts.every(Number.isFinite) ? Parts.slice(0, 3) : Def;
  };

  const Title = ReadStr("Title", ReadStr("Label", FallbackName));
  const Model = GetForceModelMeta(ReadStr("Model", "LinearWind")).Id;
  const Force = ResolveForceValues({
    Model,
    Enabled: ReadBool("Enabled", true),
    Strength: ReadNum("Strength", undefined),
    Radius: ReadNum("RadiusMeters", ReadNum("RadiusMetres", undefined)),
    CoreRadius: ReadNum("CoreRadiusMeters", ReadNum("CoreRadiusMetres", undefined)),
    FalloffExponent: ReadNum("FalloffExponent", 1.35),
    Attenuation: ReadStr("Attenuation", "SmoothStep"),
    Reach: ReadStr("ReachEnvelope", ReadStr("ReachShape", undefined)),
    Duration: ReadStr("DurationMode", ReadStr("Duration", "Continuous")),
    SwirlRatio: ReadNum("SwirlRatio", undefined),
    TurbulenceScale: ReadNum("TurbulenceScaleMeters", undefined),
    Octaves: ReadNum("OctaveCount", undefined),
    Frequency: ReadNum("FrequencyHertz", undefined),
    YawDeg: ReadNum("YawDegrees", 25),
    PitchDeg: ReadNum("PitchDegrees", 0),
    MotionTopology: ReadStr("MotionTopology", "Static"),
    MotionLoopMode: ReadStr("MotionLoopMode", "PingPong"),
    MotionAmplitude: ReadNum("MotionAmplitudeMeters", 0.85),
    MotionSpeedHz: ReadNum("MotionSpeedHertz", 0.22),
    FormulaEnabled: ReadBool("FormulaEnabled", false),
    FormulaCode: ReadStr("FormulaSource", DEFAULT_FORMULA_CODE),
  });
  const Position = ReadVec3("CentreMeters", ReadVec3("CentreMetres", [0, 0, 1.4]));
  const Rotation = ReadVec3("EulerDegrees", [0, 0, 0]);
  return { Name: Title, Force, Position, Rotation };
}
