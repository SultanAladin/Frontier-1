import { DEFAULT_PARAMS, CONTROL_GROUPS, PRESETS } from "./presets.js";

export const SourceRevision = "33c5f2e13dc3d6912a8bb2d887a4e8061b401b6b";
export const InitialParameters = {
  ...DEFAULT_PARAMS,
  gridResolution: 48,
  raymarchSteps: 72,
  renderScale: 0.8,
  showAtlasMinimap: false,
  showVoxelGridLines: false,
  showBoundingBox: false,
  colliderAutoMove: false,
};
export const AdditionalControls = [
  {
    key: "emitterHeight",
    label: "Source height",
    type: "range",
    min: 0.01,
    max: 1.5,
    step: 0.01,
  },
  {
    key: "emitterTemperature",
    label: "Temperature injection",
    type: "range",
    min: 0,
    max: 9,
    step: 0.1,
  },
  {
    key: "emitterFuel",
    label: "Fuel injection",
    type: "range",
    min: 0,
    max: 9,
    step: 0.1,
  },
  {
    key: "emitterSmoke",
    label: "Smoke injection",
    type: "range",
    min: 0,
    max: 9,
    step: 0.1,
  },
  {
    key: "blastFuel",
    label: "Burst fuel",
    type: "range",
    min: 0,
    max: 9,
    step: 0.1,
  },
  {
    key: "blastSmoke",
    label: "Burst smoke",
    type: "range",
    min: 0,
    max: 9,
    step: 0.1,
  },
  {
    key: "windZ",
    label: "Crosswind Z",
    type: "range",
    min: -2.5,
    max: 2.5,
    step: 0.05,
  },
  {
    key: "velocityDamping",
    label: "Velocity damping",
    type: "range",
    min: 0,
    max: 2,
    step: 0.01,
  },
  {
    key: "sunIntensity",
    label: "Sun intensity",
    type: "range",
    min: 0,
    max: 8,
    step: 0.1,
  },
  {
    key: "ambientIntensity",
    label: "Ambient light",
    type: "range",
    min: 0,
    max: 3,
    step: 0.05,
  },
];
export const ControlSpecification = Object.fromEntries(
  [
    ...CONTROL_GROUPS.flatMap((Group) => Group.controls),
    ...AdditionalControls,
  ].map((Control) => [Control.key, Control]),
);
export const ControlLabels = {
  emitterEnabled: "Continuous emission",
  emitterRate: "Emission rate",
  emitterRadius: "Source radius",
  emitterUpwardVelocity: "Upward velocity",
  emitterSwirl: "Swirl",
  dynamicBounds: "Expand around bursts",
  boundsWidth: "Width / depth",
  boundsHeight: "Height",
  dynamicBoundsMax: "Expansion limit",
  gridResolution: "Voxel resolution",
  voxelQuantization: "Voxel quantization",
  autoGpuGovernor: "Adaptive raymarch quality",
  showActiveVoxelCells: "Active voxel cells",
  showAtlasMinimap: "Atlas minimap",
  atlasMinimapField: "Atlas field",
  pressureIterations: "Pressure iterations",
  timeScale: "Time scale",
  macCormackAdvection: "MacCormack advection",
  enclosedBox: "Closed domain walls",
  sliceAxis: "Slice axis",
  slicePosition: "Slice depth",
  vorticityConfinement: "Vorticity",
  buoyancy: "Buoyancy",
  smokeWeight: "Smoke weight",
  turbulenceStrength: "Turbulence strength",
  turbulenceScale: "Turbulence scale",
  burnRate: "Burn rate",
  burnHeat: "Heat release",
  sootGeneration: "Soot generation",
  combustionExpansion: "Combustion expansion",
  coolingRate: "Cooling",
  smokeDissipation: "Smoke dissipation",
  windX: "Crosswind X",
  renderChannel: "Render channel",
  colorPalette: "Colour palette",
  raymarchSteps: "Raymarch steps",
  shadowSteps: "Shadow steps",
  renderScale: "Viewport resolution scale",
  fireIntensity: "Fire intensity",
  bloomIntensity: "Bloom",
  godRaysIntensity: "Light shafts",
  shockwaveStrength: "Shockwave refraction",
  temperatureScale: "Temperature scale",
  internalScattering: "Internal scattering",
  densityExtinction: "Smoke extinction",
  smokeAlbedo: "Smoke albedo",
  shadowDensity: "Shadow density",
  phaseAnisotropy: "Phase anisotropy",
  sunElevation: "Sun elevation",
  sunAzimuth: "Sun azimuth",
  exposure: "Exposure",
  shrapnelEnabled: "Shrapnel streamers",
  blastStrength: "Shockwave velocity",
  blastRadius: "Burst radius",
  blastTemperature: "Burst temperature",
  blastLobes: "Lobe frequency",
  obstacleType: "Collider shape",
  obstacleX: "Position X",
  obstacleY: "Position Y",
  obstacleZ: "Position Z",
  colliderAutoMove: "Animate collider",
  colliderSpeed: "Animation speed",
  obstacleRadius: "Collider radius",
  showBoundingBox: "Domain bounds",
  showVoxelGridLines: "Voxel grid lines",
  showFloorGrid: "Studio floor",
  showEmbers: "Embers / ash",
  emberCount: "Particle count",
  emberSize: "Particle size",
  emberIntensity: "Particle brightness",
  emberLifetime: "Particle lifetime",
  emberAshiness: "Ash blend",
};
export const PresetPresentation = {
  ue5_pyro_default: [
    "Pyro plume",
    "fire",
    "Rolling flame · dark soot",
    "flame",
  ],
  oil_well_inferno: [
    "Oil well inferno",
    "fire",
    "Pressurised fuel column",
    "flame",
  ],
  shrapnel_airburst: [
    "Shrapnel airburst",
    "blast",
    "Ballistic smoke streamers",
    "burst",
  ],
  megaton_open_bounds: [
    "Wide-bounds blast",
    "blast",
    "Large expanding fireball",
    "burst",
  ],
  tactical_ordnance: [
    "Tactical detonation",
    "blast",
    "Flash core · dust cloud",
    "burst",
  ],
  obstacle_deflection: [
    "Smoke deflection",
    "smoke",
    "Flow around a collider",
    "smoke",
  ],
  wildfire_tornado: [
    "Wildfire tornado",
    "fire",
    "Spiralling emerald fire",
    "rotate",
  ],
  ashfall_motes: ["Ashfall", "smoke", "Drifting smoke & ash", "smoke"],
  voxelized_stylized: ["Voxel fire", "fire", "Discrete volume shading", "box"],
  dust_tornado: ["Dust tornado", "smoke", "Cold debris vortex", "rotate"],
  tyre_burnout: ["Tyre burnout", "smoke", "Rubber smoke off a ring", "cylinder"],
  ledge_sandfall: ["Ledge sandfall", "smoke", "Sand poured off a slab", "wall"],
  settling_dust: ["Settling dust", "smoke", "Slow pale aftermath", "smoke"],
  small_gust: ["Small gust", "smoke", "Thin haze on a crosswind", "orbit"],
  brick_fracture_dust: [
    "Brick fracture",
    "blast",
    "Cold masonry burst",
    "burst",
  ],
  lantern_flame: ["Lantern flame", "fire", "One small steady wick", "flame"],
  camp_fire: ["Camp fire", "fire", "Open wood fire with sparks", "flame"],
  low_gpu_performance: [
    "Lightweight plume",
    "fire",
    "24³ grid · lower GPU cost",
    "flame",
  ],
};

export function ComputeColliderPosition(Parameters, Time) {
  if (!Parameters.colliderAutoMove || !Parameters.obstacleType)
    return [Parameters.obstacleX, Parameters.obstacleY, Parameters.obstacleZ];
  const Phase = Time * Parameters.colliderSpeed;
  return [
    Math.max(
      0.08,
      Math.min(0.92, Parameters.obstacleX + Math.sin(Phase) * 0.18),
    ),
    Parameters.obstacleY,
    Math.max(
      0.08,
      Math.min(0.92, Parameters.obstacleZ + Math.sin(Phase * 0.7) * 0.12),
    ),
  ];
}

export function ValidateParameter(Key, Value) {
  if (!Object.hasOwn(DEFAULT_PARAMS, Key))
    throw new Error(`Unknown setting: ${Key}`);
  if (typeof Value !== typeof DEFAULT_PARAMS[Key])
    throw new Error(`Invalid type for ${Key}`);
  if (typeof Value === "boolean") return Value;
  if (Key === "interactionMode") {
    if (!["orbit", "flamethrower", "detonate"].includes(Value))
      throw new Error("Invalid interaction tool");
    return Value;
  }
  const Control = ControlSpecification[Key];
  if (!Number.isFinite(Value)) throw new Error(`Non-finite setting: ${Key}`);
  if (
    Control?.type === "select" &&
    !Control.options.some((Option) => (Option.id ?? Option.value) === Value)
  )
    throw new Error(`Invalid choice for ${Key}`);
  if (Control?.type === "range" && (Value < Control.min || Value > Control.max))
    throw new Error(`Setting outside limits: ${Key}`);
  return Value;
}

export function ValidateScene(Input) {
  if (
    !Input ||
    Input.format !== "frontier-fluid-scene" ||
    Input.version !== 1 ||
    !Input.params ||
    Array.isArray(Input.params) ||
    typeof Input.params !== "object"
  )
    throw new Error("Not a supported Fluid scene (version 1).");
  const Parameters = { ...InitialParameters };
  for (const [Key, Value] of Object.entries(Input.params))
    Parameters[Key] = ValidateParameter(Key, Value);
  const Names = {
    domain: "Gas domain",
    emitter: "Fire emitter",
    collider: "Sphere collider",
    sun: "Directional light",
  };
  for (const Key of Object.keys(Names)) {
    if (Input.names?.[Key] !== undefined) {
      if (
        typeof Input.names[Key] !== "string" ||
        !Input.names[Key].trim() ||
        Input.names[Key].length > 64
      )
        throw new Error("Object names must be 1–64 characters.");
      Names[Key] = Input.names[Key].trim();
    }
  }
  if (
    typeof Input.name !== "string" ||
    !Input.name.trim() ||
    Input.name.length > 64
  )
    throw new Error("Scene name must be 1–64 characters.");
  let Camera = null;
  if (Input.camera !== undefined) {
    Camera = Input.camera;
    if (
      !Camera ||
      !["theta", "phi", "distance"].every((Key) =>
        Number.isFinite(Camera[Key]),
      ) ||
      Camera.phi < 0.01 ||
      Camera.phi > 3.13 ||
      Camera.distance < 0.85 ||
      Camera.distance > 9.5 ||
      Math.abs(Camera.theta) > 1e6 ||
      !Array.isArray(Camera.center) ||
      Camera.center.length !== 3 ||
      !Camera.center.every(
        (Value) => Number.isFinite(Value) && Math.abs(Value) <= 100,
      )
    )
      throw new Error("Invalid camera position.");
    Camera = {
      theta: Camera.theta,
      phi: Camera.phi,
      distance: Camera.distance,
      center: [...Camera.center],
    };
  }
  return { Parameters, Names, Camera, Name: Input.name.trim() };
}

export function ConstructPresetParameters(Key) {
  if (!Object.hasOwn(PRESETS, Key)) throw new Error("Unknown preset");
  return { ...InitialParameters, ...PRESETS[Key].params };
}
