// Automotive Studio Lobby level definition, nested Outliner hierarchy,
// procedural studio component defaults, Car Configurator presets, and
// hierarchical parent-to-child transform resolution.

export const StudioLevels = [
  {
    Id: "studio-lobby",
    Label: "Studio Lobby",
    Subtitle: "Photographic cyclorama · 3 vehicles · procedural rigs",
  },
  {
    Id: "showcase",
    Label: "Showcase",
    Subtitle: "Classic Project-Zero primitives & sky",
  },
];

export const VehiclePaintPresets = [
  { Name: "Obsidian Gloss", Hex: "#111319", Metallic: 0.82, Roughness: 0.14, Clearcoat: 1.0 },
  { Name: "Alpine GT3 White", Hex: "#eceff3", Metallic: 0.16, Roughness: 0.16, Clearcoat: 0.98 },
  { Name: "Liquid Titanium", Hex: "#9ca5b4", Metallic: 0.92, Roughness: 0.18, Clearcoat: 0.95 },
  { Name: "Guards Carmine", Hex: "#a51520", Metallic: 0.58, Roughness: 0.16, Clearcoat: 1.0 },
  { Name: "Slate Graphite", Hex: "#2f3640", Metallic: 0.78, Roughness: 0.2, Clearcoat: 0.96 },
  { Name: "Midnight Cobalt", Hex: "#13294b", Metallic: 0.85, Roughness: 0.16, Clearcoat: 1.0 },
];

export const RimFinishPresets = [
  { Name: "Satin Black", Hex: "#1a1c20", Metallic: 0.85, Roughness: 0.28 },
  { Name: "Machined Silver", Hex: "#cfd5de", Metallic: 0.96, Roughness: 0.18 },
  { Name: "Satin Bronze", Hex: "#7d6850", Metallic: 0.88, Roughness: 0.26 },
  { Name: "Titanium Grey", Hex: "#676f7c", Metallic: 0.92, Roughness: 0.22 },
];

export const FixtureStyles = [
  "Overhead Softbox",
  "Barn-Door Fresnel",
  "Parabolic Umbrella",
  "Studio LED Panel",
  "Octa Softbox",
];

export const CinemaCameraStyles = [
  "Cinema Matte Box",
  "Medium Format Tether",
  "Compact Mirrorless",
];

export const IesProfiles = [
  "Barn-Door Fresnel",
  "Focused Spot",
  "Wall Wash",
  "Batwing Soft",
];

export const ConfiguratorPresets = [
  {
    Id: "hero-34",
    Key: "1",
    Label: "Hero 3/4",
    Eye: [5.45, 1.56, 6.25],
    Target: [0.05, 0.78, -0.1],
    FovDeg: 35,
  },
  {
    Id: "low-front",
    Key: "2",
    Label: "Low Front",
    Eye: [3.6, 0.82, 4.95],
    Target: [-0.15, 0.72, 0.05],
    FovDeg: 32,
  },
  {
    Id: "rear-quarter",
    Key: "3",
    Label: "Rear Quarter",
    Eye: [-5.1, 1.48, 5.2],
    Target: [0.15, 0.76, -0.25],
    FovDeg: 36,
  },
  {
    Id: "profile-65",
    Key: "4",
    Label: "Profile 65mm",
    Eye: [0.15, 1.18, 7.85],
    Target: [0.0, 0.78, -0.15],
    FovDeg: 25,
  },
  {
    Id: "overhead-crane",
    Key: "5",
    Label: "Overhead Crane",
    Eye: [4.8, 4.35, 7.4],
    Target: [0.0, 0.65, -0.4],
    FovDeg: 40,
  },
  {
    Id: "crew-bts",
    Key: "6",
    Label: "Crew BTS",
    Eye: [-6.6, 1.78, 7.9],
    Target: [0.3, 1.15, -0.5],
    FovDeg: 42,
  },
];

export const OpeningRevealDurationSec = 3.8;

// Evaluate the cinematic opening camera reveal (t in [0, 1]) from a low foreground
// silhouette close-up sweeping up and around to the Hero 3/4 cyclorama framing.
export function EvaluateOpeningReveal(Progress) {
  const Clamped = Math.max(0, Math.min(1, Progress));
  const Smooth =
    Clamped < 0.5
      ? 4 * Clamped * Clamped * Clamped
      : 1 - Math.pow(-2 * Clamped + 2, 3) / 2;
  const EaseOut = 1 - Math.pow(1 - Clamped, 3);

  const P0 = [-5.8, 0.92, 6.95];
  const P1 = [-1.4, 1.12, 7.65];
  const P2 = [3.9, 1.92, 7.35];
  const P3 = ConfiguratorPresets[0].Eye;

  const Inv = 1 - Smooth;
  const Eye = [0, 1, 2].map(
    (Axis) =>
      Inv * Inv * Inv * P0[Axis] +
      3 * Inv * Inv * Smooth * P1[Axis] +
      3 * Inv * Smooth * Smooth * P2[Axis] +
      Smooth * Smooth * Smooth * P3[Axis],
  );

  const T0 = [-0.65, 0.62, 0.25];
  const T1 = ConfiguratorPresets[0].Target;
  const Target = [0, 1, 2].map(
    (Axis) => T0[Axis] + (T1[Axis] - T0[Axis]) * EaseOut,
  );

  const FovDeg = 46 + (ConfiguratorPresets[0].FovDeg - 46) * Smooth;
  return { Eye, Target, FovDeg };
}

export function ComputeTripodMountHeight(Params = {}) {
  const StandHeight = Number(Params.StandHeight ?? 1.55);
  const ColumnLift = Number(Params.ColumnLift ?? 0.28);
  const HasCasters = Boolean(Params.Casters);
  const HasFloorMat = Boolean(Params.FloorMat);
  return StandHeight + ColumnLift + (HasCasters ? 0.08 : 0) + (HasFloorMat ? 0.032 : 0);
}

export const StudioLobbyRows = [
  // Vehicles collection (3 vehicles in level)
  ["studio-vehicles", "Vehicles", "group", "folder-scene", null, "Studio vehicle lineup · 3 vehicles"],
  [
    "vehicle-hero",
    "Hero Coupe · Cyclorama",
    "studio-vehicle",
    "editor-cube",
    "studio-vehicles",
    "ControlledIntegrator · Center stage hero vehicle",
  ],
  [
    "vehicle-gt3",
    "Alpine GT · Left Bay",
    "studio-vehicle",
    "editor-cube",
    "studio-vehicles",
    "ControlledIntegrator · Stage-left wing showcase",
  ],
  [
    "vehicle-tourer",
    "Liquid Tourer · Right Bay",
    "studio-vehicle",
    "editor-cube",
    "studio-vehicles",
    "ControlledIntegrator · Stage-right wing showcase",
  ],

  // Studio architecture collection
  ["studio-architecture", "Studio Architecture", "group", "folder-world", null, "Cyclorama cove, glossy tiled floor, ceiling truss"],
  [
    "studio-cyclorama",
    "Cyclorama Infinity Cove",
    "studio-procedural",
    "editor-cylinder",
    "studio-architecture",
    "Procedural studio.cyclorama · seamless matte cove",
  ],
  [
    "studio-floor",
    "Glossy Tiled Studio Floor",
    "studio-procedural",
    "editor-cube",
    "studio-architecture",
    "Procedural studio.tiled_floor · dark reflective slate tiles",
  ],
  [
    "studio-ceiling",
    "Ribbed Ceiling & Truss",
    "studio-procedural",
    "editor-cube",
    "studio-architecture",
    "Procedural studio.ceiling_truss · overhead grid & steel ribs",
  ],

  // Lighting rigs collection (Fixture/Stand parents -> Area/IES Light children)
  ["studio-lighting", "Lighting Rigs", "group", "folder-generic", null, "Hierarchical procedural stands, modifiers & lights"],
  [
    "rig-softbox-fixture",
    "Overhead Softbox Bank",
    "studio-procedural",
    "editor-area-light",
    "studio-lighting",
    "Procedural studio.light_fixture · suspended silk overhead softbox",
  ],
  [
    "light-overhead-area",
    "Overhead Softbox Area Light",
    "light",
    "editor-area-light",
    "rig-softbox-fixture",
    "Area light · 6.2 × 3.8 m rectangular diffuser bank",
  ],
  [
    "rig-key-tripod",
    "Key Fresnel Tripod",
    "studio-procedural",
    "editor-cone",
    "studio-lighting",
    "Shared procedural studio.tripod · wheeled studio light stand",
  ],
  [
    "rig-key-fixture",
    "Barn-Door Fresnel Fixture",
    "studio-procedural",
    "editor-spot-light",
    "rig-key-tripod",
    "Procedural studio.light_fixture · articulated 4-leaf barn doors",
  ],
  [
    "light-key-ies",
    "Key Fresnel IES Light",
    "light",
    "editor-spot-light",
    "rig-key-fixture",
    "Photometric IES light · Barn-Door Fresnel lobe",
  ],
  [
    "rig-umbrella-tripod",
    "Umbrella Fill Tripod",
    "studio-procedural",
    "editor-cone",
    "studio-lighting",
    "Shared procedural studio.tripod · silver umbrella stand",
  ],
  [
    "rig-umbrella-fixture",
    "Parabolic Umbrella Modifier",
    "studio-procedural",
    "editor-sphere",
    "rig-umbrella-tripod",
    "Procedural studio.light_fixture · 16-rib silver bounce umbrella",
  ],
  [
    "light-umbrella-area",
    "Umbrella Bounce Area Light",
    "light",
    "editor-area-light",
    "rig-umbrella-fixture",
    "Area light · soft silver parabolic bounce fill",
  ],
  [
    "rig-rim-tripod",
    "Back-Left Rim Tripod",
    "studio-procedural",
    "editor-cone",
    "studio-lighting",
    "Shared procedural studio.tripod · wheeled rim stand",
  ],
  [
    "rig-rim-fixture",
    "Rim LED Panel Fixture",
    "studio-procedural",
    "editor-area-light",
    "rig-rim-tripod",
    "Procedural studio.light_fixture · cool edge-separation LED",
  ],
  [
    "light-rim-area",
    "Rim LED Area Light",
    "light",
    "editor-area-light",
    "rig-rim-fixture",
    "Area light · cool 6300 K contour rim separation",
  ],
  [
    "rig-accent-tripod",
    "Back-Right Accent Tripod",
    "studio-procedural",
    "editor-cone",
    "studio-lighting",
    "Shared procedural studio.tripod · wheeled accent strobe stand",
  ],
  [
    "rig-accent-fixture",
    "Octa Softbox Strobe",
    "studio-procedural",
    "editor-spot-light",
    "rig-accent-tripod",
    "Procedural studio.light_fixture · 8-sided studio octa modifier",
  ],
  [
    "light-accent-ies",
    "Accent Strobe IES Light",
    "light",
    "editor-spot-light",
    "rig-accent-fixture",
    "Photometric IES light · focused cyclorama rim strobe",
  ],

  // Camera rigs collection (Tripod -> Camera Body -> Render Camera)
  ["cameras", "Camera Rigs", "group", "camera", null, "Editor camera & mounted studio cinema camera rigs"],
  [
    "camera",
    "Editor Camera",
    "camera",
    "camera",
    "cameras",
    "Permanent editor navigation camera · Orbit/Pan/Zoom",
  ],
  [
    "rig-cam-a-tripod",
    "Cinema Tripod A",
    "studio-procedural",
    "editor-cone",
    "cameras",
    "Shared procedural studio.tripod · heavy matte tripod + floor mat",
  ],
  [
    "rig-cam-a-body",
    "Cinema Camera A",
    "studio-procedural",
    "camera",
    "rig-cam-a-tripod",
    "Procedural studio.cinema_camera · twin-magazine cine camera",
  ],
  [
    "cam-configurator",
    "Car Configurator Camera",
    "camera",
    "camera",
    "rig-cam-a-body",
    "Active Play mode configurator camera · opening reveal + 6 presets",
  ],
  [
    "rig-cam-b-tripod",
    "Cinema Tripod B",
    "studio-procedural",
    "editor-cone",
    "cameras",
    "Shared procedural studio.tripod · foreground tether camera tripod",
  ],
  [
    "rig-cam-b-body",
    "Medium Format Camera B",
    "studio-procedural",
    "camera",
    "rig-cam-b-tripod",
    "Procedural studio.cinema_camera · medium format studio camera",
  ],
  [
    "cam-profile",
    "Profile 65mm Camera",
    "camera",
    "camera",
    "rig-cam-b-body",
    "Side elevation 65 mm optical study camera",
  ],
  [
    "post",
    "Post Process",
    "post",
    "environment-exposure",
    "cameras",
    "ACES Filmic exposure & studio vignette",
  ],

  // Studio Props & Crew collection
  ["studio-props", "Studio Props & Crew", "group", "folder-generic", null, "Tether workstation, Pelican flight cases, cable runs & mats"],
  [
    "prop-workstation",
    "Tether Workstation & Desk",
    "studio-procedural",
    "editor-cube",
    "studio-props",
    "Procedural studio.workstation · hairpin desk, laptop & flight cases",
  ],
  [
    "prop-grip-cases",
    "Grip Cases & Floor Mats",
    "studio-procedural",
    "editor-cube",
    "studio-props",
    "Procedural studio.workstation · stage-left Pelican cases & cables",
  ],

  // World collection (studio exposure / subtle ambient control)
  ["world", "World", "group", "folder-world", null, "Interior studio ambient & tone balance"],
  [
    "studio-ambient",
    "Studio Ambient",
    "studio-procedural",
    "environment-exposure",
    "world",
    "Interior darkroom exposure, bounce fill & floor reflection strength",
  ],
].map(([Id, Name, Panel, Icon, Parent, Description]) => ({
  Id,
  Name,
  Panel,
  Icon,
  Parent,
  Description,
}));

export const StudioLobbyInitialValues = {
  // Vehicles (3 vehicles in the level)
  "vehicle-hero": {
    Position: [0.0, 0.0, -0.15],
    YawDeg: -16,
    Scale: 0.76,
    PaintHex: "#111319",
    Metallic: 0.84,
    Roughness: 0.13,
    Clearcoat: 1.0,
    RimHex: "#1b1d22",
    CaliperHex: "#b81d24",
    ShowWheelGlow: false,
  },
  "vehicle-gt3": {
    Position: [-6.6, 0.0, -1.8],
    YawDeg: -32,
    Scale: 0.74,
    PaintHex: "#eceff3",
    Metallic: 0.18,
    Roughness: 0.16,
    Clearcoat: 0.98,
    RimHex: "#1d2026",
    CaliperHex: "#c91e26",
  },
  "vehicle-tourer": {
    Position: [6.7, 0.0, -1.8],
    YawDeg: -148,
    Scale: 0.74,
    PaintHex: "#9ca5b4",
    Metallic: 0.92,
    Roughness: 0.18,
    Clearcoat: 0.96,
    RimHex: "#6f604e",
    CaliperHex: "#d49b24",
  },

  // Studio Architecture
  "studio-cyclorama": {
    ProceduralKind: "studio.cyclorama",
    Position: [0, 0, -0.3],
    YawDeg: 0,
    CoveWidth: 11.4,
    FloorDepth: 8.2,
    WallHeight: 5.6,
    CoveRadius: 1.85,
    SideWings: true,
    SurfaceTint: "#bfc5ce",
    PlasterGrain: 0.38,
  },
  "studio-floor": {
    ProceduralKind: "studio.tiled_floor",
    Position: [0, 0, 0],
    YawDeg: 0,
    FloorWidth: 34,
    FloorDepth: 28,
    TileSize: 1.2,
    FloorGloss: 0.68,
    SurfaceTint: "#1c2027",
  },
  "studio-ceiling": {
    ProceduralKind: "studio.ceiling_truss",
    Position: [0, 0, -0.5],
    YawDeg: 0,
    SpanWidth: 26,
    SpanDepth: 20,
    CeilingHeight: 5.95,
    RibCount: 12,
    TrussGrid: true,
  },

  // Overhead Softbox Bank -> Child Area Light
  "rig-softbox-fixture": {
    ProceduralKind: "studio.light_fixture",
    Position: [0.0, 3.62, -0.25],
    YawDeg: 0,
    FixtureStyle: "Overhead Softbox",
    ApertureWidth: 6.4,
    ApertureHeight: 3.7,
    HousingDepth: 0.28,
    SuspensionDrop: 2.32,
    BarnDoorFlare: 28,
    YokeTiltDeg: 0,
    UmbrellaRibs: 16,
  },
  "light-overhead-area": {
    LightKind: "Area",
    LightType: 2,
    Intensity: 6.8,
    Kelvin: 5600,
    TintHex: "#f4f7ff",
    Width: 6.4,
    Height: 3.7,
    LocalOffset: [0, -0.15, 0],
    AimTarget: [0, 0.4, -0.2],
    Enabled: true,
  },

  // Key Fresnel Tripod -> Fixture -> IES Light
  "rig-key-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [-3.45, 0.0, 1.55],
    YawDeg: 38,
    StandHeight: 1.58,
    ColumnLift: 0.34,
    LegSpreadDeg: 27,
    TubeRadius: 0.017,
    LegSegments: 3,
    HeadTiltDeg: -14,
    Spreader: true,
    Casters: true,
    FloorMat: false,
  },
  "rig-key-fixture": {
    ProceduralKind: "studio.light_fixture",
    Position: [0, 0, 0],
    YawDeg: 0,
    FixtureStyle: "Barn-Door Fresnel",
    ApertureWidth: 0.48,
    ApertureHeight: 0.48,
    HousingDepth: 0.42,
    BarnDoorFlare: 34,
    YokeTiltDeg: -14,
    UmbrellaRibs: 12,
    SuspensionDrop: 0,
  },
  "light-key-ies": {
    LightKind: "IES",
    LightType: 1,
    IesProfile: "Barn-Door Fresnel",
    Intensity: 18.0,
    Kelvin: 5400,
    TintHex: "#fff5e8",
    BeamDeg: 46,
    Softness: 0.35,
    LocalOffset: [0, 0.14, 0.2],
    AimTarget: [-0.25, 0.85, -0.15],
    Enabled: true,
  },

  // Parabolic Umbrella Tripod -> Modifier -> Area Light
  "rig-umbrella-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [3.25, 0.0, 1.85],
    YawDeg: -48,
    StandHeight: 1.56,
    ColumnLift: 0.32,
    LegSpreadDeg: 28,
    TubeRadius: 0.016,
    LegSegments: 3,
    HeadTiltDeg: -10,
    Spreader: true,
    Casters: false,
    FloorMat: false,
  },
  "rig-umbrella-fixture": {
    ProceduralKind: "studio.light_fixture",
    Position: [0, 0, 0],
    YawDeg: 0,
    FixtureStyle: "Parabolic Umbrella",
    ApertureWidth: 1.48,
    ApertureHeight: 1.48,
    HousingDepth: 0.48,
    BarnDoorFlare: 25,
    YokeTiltDeg: -10,
    UmbrellaRibs: 16,
    SuspensionDrop: 0,
  },
  "light-umbrella-area": {
    LightKind: "Area",
    LightType: 2,
    Intensity: 3.6,
    Kelvin: 5800,
    TintHex: "#edf3ff",
    Width: 1.55,
    Height: 1.55,
    LocalOffset: [0, 0.06, -0.15],
    AimTarget: [0.35, 0.85, -0.15],
    Enabled: true,
  },

  // Back-Left Rim Tripod -> LED Panel -> Area Light
  "rig-rim-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [-4.15, 0.0, -2.55],
    YawDeg: 132,
    StandHeight: 1.72,
    ColumnLift: 0.38,
    LegSpreadDeg: 26,
    TubeRadius: 0.017,
    LegSegments: 3,
    HeadTiltDeg: -18,
    Spreader: true,
    Casters: true,
    FloorMat: false,
  },
  "rig-rim-fixture": {
    ProceduralKind: "studio.light_fixture",
    Position: [0, 0, 0],
    YawDeg: 0,
    FixtureStyle: "Studio LED Panel",
    ApertureWidth: 1.15,
    ApertureHeight: 0.68,
    HousingDepth: 0.14,
    BarnDoorFlare: 24,
    YokeTiltDeg: -18,
    UmbrellaRibs: 12,
    SuspensionDrop: 0,
  },
  "light-rim-area": {
    LightKind: "Area",
    LightType: 2,
    Intensity: 3.9,
    Kelvin: 6300,
    TintHex: "#e1ecff",
    Width: 1.15,
    Height: 0.68,
    LocalOffset: [0, 0.1, 0.12],
    AimTarget: [-0.3, 0.9, -0.25],
    Enabled: true,
  },

  // Back-Right Accent Tripod -> Octa Strobe -> IES Light
  "rig-accent-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [4.35, 0.0, -2.3],
    YawDeg: -136,
    StandHeight: 1.66,
    ColumnLift: 0.36,
    LegSpreadDeg: 26,
    TubeRadius: 0.016,
    LegSegments: 3,
    HeadTiltDeg: -16,
    Spreader: true,
    Casters: true,
    FloorMat: false,
  },
  "rig-accent-fixture": {
    ProceduralKind: "studio.light_fixture",
    Position: [0, 0, 0],
    YawDeg: 0,
    FixtureStyle: "Octa Softbox",
    ApertureWidth: 0.96,
    ApertureHeight: 0.96,
    HousingDepth: 0.42,
    BarnDoorFlare: 25,
    YokeTiltDeg: -16,
    UmbrellaRibs: 8,
    SuspensionDrop: 0,
  },
  "light-accent-ies": {
    LightKind: "IES",
    LightType: 1,
    IesProfile: "Focused Spot",
    Intensity: 14.5,
    Kelvin: 5600,
    TintHex: "#f7f9ff",
    BeamDeg: 42,
    Softness: 0.42,
    LocalOffset: [0, 0.12, 0.22],
    AimTarget: [0.35, 0.8, -0.15],
    Enabled: true,
  },

  // Cinema Tripod A -> Cinema Camera A -> Car Configurator Camera
  "rig-cam-a-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [-2.85, 0.0, 4.18],
    YawDeg: 29,
    StandHeight: 1.38,
    ColumnLift: 0.2,
    LegSpreadDeg: 24,
    TubeRadius: 0.02,
    LegSegments: 3,
    HeadTiltDeg: -4,
    Spreader: true,
    Casters: false,
    FloorMat: true,
  },
  "rig-cam-a-body": {
    ProceduralKind: "studio.cinema_camera",
    Position: [0, 0, 0],
    YawDeg: 0,
    CameraStyle: "Cinema Matte Box",
    LensLength: 0.34,
    ReelRadius: 0.13,
    MatteBox: true,
    TopHandle: true,
  },
  "cam-configurator": {
    FocalLengthMm: 38,
    ApertureFStop: 2.8,
    OrbitSpeedDegPerSec: 7.5,
    ShowroomOrbit: true,
    PresetIndex: 0,
  },

  // Cinema Tripod B -> Medium Format Camera B -> Profile 65mm Camera
  "rig-cam-b-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [2.15, 0.0, 4.02],
    YawDeg: -24,
    StandHeight: 1.24,
    ColumnLift: 0.18,
    LegSpreadDeg: 25,
    TubeRadius: 0.018,
    LegSegments: 3,
    HeadTiltDeg: -3,
    Spreader: true,
    Casters: false,
    FloorMat: true,
  },
  "rig-cam-b-body": {
    ProceduralKind: "studio.cinema_camera",
    Position: [0, 0, 0],
    YawDeg: 0,
    CameraStyle: "Medium Format Tether",
    LensLength: 0.24,
    ReelRadius: 0.08,
    MatteBox: true,
    TopHandle: true,
  },
  "cam-profile": {
    FocalLengthMm: 65,
    ApertureFStop: 4.0,
  },

  // Studio Props & Crew
  "prop-workstation": {
    ProceduralKind: "studio.workstation",
    Position: [4.75, 0.0, 3.65],
    YawDeg: -26,
    DeskWidth: 1.48,
    DeskDepth: 0.72,
    DeskHeight: 0.78,
    FlightCases: true,
    CoiledCables: true,
    HasDesk: true,
  },
  "prop-grip-cases": {
    ProceduralKind: "studio.workstation",
    Position: [-4.85, 0.0, 3.25],
    YawDeg: 18,
    DeskWidth: 1.0,
    DeskDepth: 0.55,
    DeskHeight: 0.65,
    FlightCases: true,
    CoiledCables: true,
    HasDesk: false,
  },

  // Studio interior exposure / ambient
  "studio-ambient": {
    ProceduralKind: "studio.ambient",
    Exposure: 1.16,
    AmbientFill: 0.14,
    ShadowDepth: 0.72,
    Vignette: 0.28,
    ShowLightGizmos: false,
  },
};

export function KelvinToRgb(Kelvin) {
  const Temp = Math.max(1800, Math.min(12000, Number(Kelvin || 5600))) / 100;
  let R, G, B;
  if (Temp <= 66) {
    R = 255;
    G = 99.4708025861 * Math.log(Temp) - 161.1195681661;
    B = Temp <= 19 ? 0 : 138.5177312231 * Math.log(Temp - 10) - 305.0447927307;
  } else {
    R = 329.698727446 * Math.pow(Temp - 60, -0.1332047592);
    G = 288.1221695283 * Math.pow(Temp - 60, -0.0755148492);
    B = 255;
  }
  return [
    Math.max(0, Math.min(255, R)) / 255,
    Math.max(0, Math.min(255, G)) / 255,
    Math.max(0, Math.min(255, B)) / 255,
  ];
}

export function HexToLinearRgb(Hex) {
  const Clean = String(Hex || "#ffffff").replace("#", "");
  const Full =
    Clean.length === 3
      ? Clean.split("").map((C) => C + C).join("")
      : Clean.padEnd(6, "f").slice(0, 6);
  const Int = parseInt(Full, 16);
  const Srgb = [((Int >> 16) & 255) / 255, ((Int >> 8) & 255) / 255, (Int & 255) / 255];
  return Srgb.map((C) => Math.pow(C, 2.2));
}

// Compute effective world transform { Position, YawRad, PitchRad } for every row
// respecting parent tripod mount heights and hierarchical parenting.
export function ResolveStudioHierarchy(Rows, Values, Hidden) {
  const ById = {};
  for (const Row of Rows) ById[Row.Id] = Row;
  const Cache = {};
  const VisibleCache = {};

  function IsVisible(Id) {
    if (Id in VisibleCache) return VisibleCache[Id];
    const Row = ById[Id];
    if (!Row) return false;
    if (Hidden[Id]) {
      VisibleCache[Id] = false;
      return false;
    }
    VisibleCache[Id] = Row.Parent ? IsVisible(Row.Parent) : true;
    return VisibleCache[Id];
  }

  function Resolve(Id) {
    if (Id in Cache) return Cache[Id];
    const Row = ById[Id];
    const Val = Values[Id] || {};
    const LocalPos = Val.Position || Val.LocalOffset || [0, 0, 0];
    const LocalYaw = ((Number(Val.YawDeg) || 0) * Math.PI) / 180;
    const LocalPitch = ((Number(Val.YokeTiltDeg ?? Val.HeadTiltDeg) || 0) * Math.PI) / 180;

    if (!Row || !Row.Parent || !ById[Row.Parent] || ById[Row.Parent].Panel === "group") {
      Cache[Id] = {
        Position: [LocalPos[0], LocalPos[1], LocalPos[2]],
        YawRad: LocalYaw,
        PitchRad: LocalPitch,
        Visible: IsVisible(Id),
      };
      return Cache[Id];
    }

    const ParentRow = ById[Row.Parent];
    const ParentVal = Values[Row.Parent] || {};
    const ParentXf = Resolve(Row.Parent);
    let MountY = 0;
    if (ParentVal.ProceduralKind === "studio.tripod") {
      MountY = ComputeTripodMountHeight(ParentVal);
    }

    const CosY = Math.cos(ParentXf.YawRad);
    const SinY = Math.sin(ParentXf.YawRad);
    const Rx = LocalPos[0] * CosY + LocalPos[2] * SinY;
    const Rz = -LocalPos[0] * SinY + LocalPos[2] * CosY;

    Cache[Id] = {
      Position: [
        ParentXf.Position[0] + Rx,
        ParentXf.Position[1] + MountY + LocalPos[1],
        ParentXf.Position[2] + Rz,
      ],
      YawRad: ParentXf.YawRad + LocalYaw,
      PitchRad: ParentRow.Panel === "studio-procedural" ? ParentXf.PitchRad + LocalPitch : LocalPitch,
      Visible: IsVisible(Id),
    };
    return Cache[Id];
  }

  for (const Row of Rows) Resolve(Row.Id);
  return Cache;
}
