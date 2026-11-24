// Automotive Studio Lobby level definition, nested Outliner hierarchy,
// analytical rig aiming toward Hero Coupe, native Project-Zero Light/Camera
// property initialization, Car Configurator presets, and hierarchical transforms.

export const HeroCarAimTarget = [0.0, 0.88, -0.15];
export const Lobby2HeroAimTarget = [0.0, 1.38, -0.35];
export const Lobby3HeroAimTarget = [0.35, 0.88, 0.35];

export function IsStudioLobbyLevel(LevelId) {
  return LevelId === "studio-lobby" || LevelId === "studio-lobby-2" || LevelId === "studio-lobby-3";
}

export function GetHeroAimTargetForLevel(LevelId) {
  if (LevelId === "studio-lobby-2") return Lobby2HeroAimTarget;
  if (LevelId === "studio-lobby-3") return Lobby3HeroAimTarget;
  return HeroCarAimTarget;
}

export const StudioLevels = [
  {
    Id: "studio-lobby-3",
    Label: "Lobby 3 · Racing Garage",
    Subtitle: "Racing stadium pit garage · hydraulic car lift · tools, cages & sunlit grandstands",
  },
  {
    Id: "studio-lobby-2",
    Label: "Lobby 2 · Forest Stage",
    Subtitle: "Forest soundstage · rock incline · ferns, wood, wind fan & dust · 4 cars",
  },
  {
    Id: "studio-lobby",
    Label: "Lobby 1 · Cyclorama",
    Subtitle: "Photographic cyclorama · 3 vehicles · procedural rigs",
  },
  {
    Id: "showcase",
    Label: "Showcase",
    Subtitle: "Classic Project-Zero primitives & sky",
  },
];

export const VehiclePaintPresets = [
  { Name: "Signal Orange", Hex: "#e86512", Metallic: 0.62, Roughness: 0.14, Clearcoat: 1.0 },
  { Name: "Forest Sage", Hex: "#7ea689", Metallic: 0.42, Roughness: 0.16, Clearcoat: 0.98 },
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
    Eye: [5.75, 1.62, 6.45],
    Target: [0.05, 0.78, -0.15],
    FovDeg: 35,
  },
  {
    Id: "low-front",
    Key: "2",
    Label: "Low Front",
    Eye: [3.85, 0.85, 5.15],
    Target: [-0.1, 0.72, 0.0],
    FovDeg: 32,
  },
  {
    Id: "rear-quarter",
    Key: "3",
    Label: "Rear Quarter",
    Eye: [-5.45, 1.54, 5.45],
    Target: [0.15, 0.76, -0.25],
    FovDeg: 36,
  },
  {
    Id: "profile-65",
    Key: "4",
    Label: "Profile 65mm",
    Eye: [0.15, 1.22, 8.25],
    Target: [0.0, 0.78, -0.15],
    FovDeg: 25,
  },
  {
    Id: "overhead-crane",
    Key: "5",
    Label: "Overhead Crane",
    Eye: [5.4, 4.45, 8.1],
    Target: [0.0, 0.7, -0.4],
    FovDeg: 40,
  },
  {
    Id: "crew-bts",
    Key: "6",
    Label: "Crew BTS",
    Eye: [-7.4, 1.92, 8.85],
    Target: [0.25, 1.12, -0.4],
    FovDeg: 42,
  },
];

export const OpeningRevealDurationSec = 3.8;

// Keep ONLY the Game / Configurator Camera safely inside the studio walls & ceiling.
// Never apply this to the Editor Camera, which remains completely unrestricted.
export function ClampGameCameraInsideLobby(Eye, Values, LevelId) {
  if (!IsStudioLobbyLevel(LevelId)) return Eye;
  const Ceil = Values["studio-ceiling"] || {};
  const MaxY = Math.max(4.2, Number(Ceil.CeilingHeight ?? 7.25) - 0.78);
  if (LevelId === "studio-lobby-3") {
    const Sh = Values["l3-garage-shell"] || {};
    const HalfW = Math.max(8.0, Number(Sh.GarageWidth ?? 22.0) * 0.5 - 1.25);
    const MaxGarageY = Math.max(4.5, Number(Sh.GarageHeight ?? 7.15) - 1.15);
    return [
      Math.max(-HalfW, Math.min(HalfW, Eye[0])),
      Math.max(0.45, Math.min(MaxGarageY, Eye[1])),
      Math.max(-4.8, Math.min(16.5, Eye[2])),
    ];
  }
  if (LevelId === "studio-lobby-2") {
    const Bd = Values["l2-forest-backdrop"] || {};
    const BackZ = Number(Bd.Position?.[2] ?? -5.65) + 1.25;
    const HalfW = Math.max(7.2, Number(Bd.ProsceniumWidth ?? 20.5) * 0.5 - 0.95);
    const ClampedZ = Math.max(BackZ, Math.min(18.0, Eye[2]));
    const MaxHalfX = ClampedZ < 3.5 ? HalfW : 21.0;
    return [
      Math.max(-MaxHalfX, Math.min(MaxHalfX, Eye[0])),
      Math.max(0.45, Math.min(MaxY, Eye[1])),
      ClampedZ,
    ];
  }
  const Cyc = Values["studio-cyclorama"] || {};
  const CoveHalfW = Math.max(6.5, Number(Cyc.CoveWidth ?? 18.0) * 0.5 - 0.85);
  const CycZ = Number(Cyc.Position?.[2] ?? -0.8);
  const FloorD = Number(Cyc.FloorDepth ?? 13.5);
  const FrontZ = CycZ + FloorD * 0.42;
  const BackZ = CycZ - FloorD * 0.58 + 1.15;
  const ClampedZ = Math.max(BackZ, Math.min(18.0, Eye[2]));
  const MaxHalfX = ClampedZ < FrontZ + 0.45 ? CoveHalfW : 21.0;
  return [
    Math.max(-MaxHalfX, Math.min(MaxHalfX, Eye[0])),
    Math.max(0.32, Math.min(MaxY, Eye[1])),
    ClampedZ,
  ];
}

// Compute exact YawDeg & downward PitchDeg so local +Z (lens / light mouth) aims dead-on at TargetPos.
// Matches ModelTransformMat where local +Z maps to world [+sin(Yaw), 0, +cos(Yaw)],
// and positive PitchDeg tilts downward along Fwd = [0, -sin(Pitch), cos(Pitch)].
export function ComputeAimAngles(FromPos, TargetPos = HeroCarAimTarget) {
  const Dx = TargetPos[0] - FromPos[0];
  const Dy = TargetPos[1] - FromPos[1];
  const Dz = TargetPos[2] - FromPos[2];
  const Horiz = Math.hypot(Dx, Dz) || 1e-5;
  const YawDeg = Math.round(((Math.atan2(Dx, Dz) * 180) / Math.PI) * 10) / 10;
  const PitchDeg = Math.round(((Math.atan2(-Dy, Horiz) * 180) / Math.PI) * 10) / 10;
  return { YawDeg, PitchDeg };
}

// World-space unit forward direction for a rig oriented by (YawRad, PitchRad)
export function ComputeForwardVector(YawRad, PitchRad = 0) {
  const Cp = Math.cos(PitchRad);
  const Sp = Math.sin(PitchRad);
  return [Cp * Math.sin(YawRad), -Sp, Cp * Math.cos(YawRad)];
}

export function EvaluateOpeningReveal(Progress, LevelId = "studio-lobby") {
  const Clamped = Math.max(0, Math.min(1, Progress));
  const Smooth =
    Clamped < 0.5
      ? 4 * Clamped * Clamped * Clamped
      : 1 - Math.pow(-2 * Clamped + 2, 3) / 2;

  const IsL2 = LevelId === "studio-lobby-2";
  const IsL3 = LevelId === "studio-lobby-3";
  const P0 = IsL3 ? [6.1, 1.35, 6.2] : IsL2 ? [-5.4, 1.35, 7.65] : [-5.8, 0.92, 6.95];
  const P1 = IsL3 ? [4.4, 1.48, 6.85] : IsL2 ? [-1.1, 1.52, 8.25] : [-1.4, 1.12, 7.65];
  const P2 = IsL3 ? [3.6, 1.56, 5.95] : IsL2 ? [3.8, 2.25, 7.95] : [3.9, 1.92, 7.35];
  const P3 = IsL3 ? [4.95, 1.52, 5.85] : IsL2 ? [5.45, 2.05, 7.15] : ConfiguratorPresets[0].Eye;

  const Inv = 1 - Smooth;
  const Eye = [0, 1, 2].map(
    (Axis) =>
      Inv * Inv * Inv * P0[Axis] +
      3 * Inv * Inv * Smooth * P1[Axis] +
      3 * Inv * Smooth * Smooth * P2[Axis] +
      Smooth * Smooth * Smooth * P3[Axis],
  );

  const T0 = IsL3 ? [-0.6, 1.12, -1.2] : IsL2 ? [-0.45, 1.05, 0.1] : [-0.65, 0.62, 0.25];
  const T1 = IsL3 ? [0.15, 0.95, 0.15] : IsL2 ? [0.05, 1.28, -0.35] : ConfiguratorPresets[0].Target;
  const Target = [0, 1, 2].map(
    (Axis) => T0[Axis] + (T1[Axis] - T0[Axis]) * Smooth,
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

// Compute exact initial aim angles from each tripod's mount head to Hero Coupe
const AimKey = ComputeAimAngles([-3.65, 2.28, 1.55], HeroCarAimTarget);
const AimUmbrella = ComputeAimAngles([3.45, 2.12, 1.85], HeroCarAimTarget);
const AimRim = ComputeAimAngles([-4.35, 2.52, -2.65], HeroCarAimTarget);
const AimAccent = ComputeAimAngles([4.55, 2.32, -2.4], HeroCarAimTarget);
const AimCamA = ComputeAimAngles([-3.25, 1.78, 5.75], HeroCarAimTarget);
const AimCamB = ComputeAimAngles([2.55, 1.6, 5.65], HeroCarAimTarget);

export const StudioLobbyRows = [
  // Vehicles collection (3 vehicles in level)
  ["studio-vehicles", "Vehicles", "group", "folder-scene", null, "Studio vehicle lineup · 3 vehicles"],
  [
    "vehicle-hero",
    "Hero Coupe · Cyclorama",
    "studio-vehicle",
    "studio-vehicle",
    "studio-vehicles",
    "ControlledIntegrator · Center hero vehicle under overhead softbox",
  ],
  [
    "vehicle-gt3",
    "Alpine GT · Outer Left Bay",
    "studio-vehicle",
    "studio-vehicle",
    "studio-vehicles",
    "ControlledIntegrator · Parked outside cyclorama on outer tiled floor",
  ],
  [
    "vehicle-tourer",
    "Liquid Tourer · Outer Right Bay",
    "studio-vehicle",
    "studio-vehicle",
    "studio-vehicles",
    "ControlledIntegrator · Parked outside cyclorama on outer tiled floor",
  ],

  // Studio architecture collection
  ["studio-architecture", "Studio Architecture", "group", "folder-world", null, "Cyclorama cove, glossy tiled floor, ceiling truss"],
  [
    "studio-cyclorama",
    "Cyclorama Infinity Cove",
    "studio-procedural",
    "studio-cyclorama",
    "studio-architecture",
    "Procedural studio.cyclorama · seamless matte cove",
  ],
  [
    "studio-floor",
    "Glossy Tiled Studio Floor",
    "studio-procedural",
    "studio-floor-grid",
    "studio-architecture",
    "Procedural studio.tiled_floor · dark reflective slate tiles",
  ],
  [
    "studio-ceiling",
    "Ribbed Ceiling & Truss",
    "studio-procedural",
    "studio-ceiling-truss",
    "studio-architecture",
    "Procedural studio.ceiling_truss · overhead grid & steel ribs",
  ],

  // Lighting rigs collection (Fixture/Stand parents -> Native Area/IES Light children)
  ["studio-lighting", "Lighting Rigs", "group", "folder-generic", null, "Hierarchical procedural stands, modifiers & native lights"],
  [
    "rig-softbox-fixture",
    "Overhead Softbox Bank",
    "studio-procedural",
    "studio-softbox",
    "studio-lighting",
    "Procedural studio.light_fixture · suspended silk overhead softbox",
  ],
  [
    "light-overhead-area",
    "Overhead Softbox Area Light",
    "light",
    "editor-area-light",
    "rig-softbox-fixture",
    "Rect Area Light · 6.4 × 3.7 m suspended silk diffuser",
    "arealight",
    "Softbox",
  ],
  [
    "rig-key-tripod",
    "Key Fresnel Tripod",
    "studio-procedural",
    "studio-tripod",
    "studio-lighting",
    "Shared procedural studio.tripod · wheeled studio light stand",
  ],
  [
    "rig-key-fixture",
    "Barn-Door Fresnel Fixture",
    "studio-procedural",
    "studio-fresnel",
    "rig-key-tripod",
    "Procedural studio.light_fixture · articulated 4-leaf barn doors",
  ],
  [
    "light-key-ies",
    "Key Fresnel IES Light",
    "light",
    "editor-dome-light",
    "rig-key-fixture",
    "IES Photometric · Barn-Door Fresnel lobe aimed at Hero Coupe",
    "ieslight",
    "IES Downlight",
  ],
  [
    "rig-umbrella-tripod",
    "Umbrella Fill Tripod",
    "studio-procedural",
    "studio-tripod",
    "studio-lighting",
    "Shared procedural studio.tripod · silver umbrella stand",
  ],
  [
    "rig-umbrella-fixture",
    "Parabolic Umbrella Modifier",
    "studio-procedural",
    "studio-umbrella",
    "rig-umbrella-tripod",
    "Procedural studio.light_fixture · 16-rib silver bounce umbrella",
  ],
  [
    "light-umbrella-area",
    "Umbrella Bounce Area Light",
    "light",
    "editor-area-light",
    "rig-umbrella-fixture",
    "Rect Area Light · soft silver parabolic bounce fill",
    "arealight",
    "Softbox",
  ],
  [
    "rig-rim-tripod",
    "Back-Left Rim Tripod",
    "studio-procedural",
    "studio-tripod",
    "studio-lighting",
    "Shared procedural studio.tripod · wheeled rim stand",
  ],
  [
    "rig-rim-fixture",
    "Rim LED Panel Fixture",
    "studio-procedural",
    "studio-led-panel",
    "rig-rim-tripod",
    "Procedural studio.light_fixture · cool edge-separation LED",
  ],
  [
    "light-rim-area",
    "Rim LED Area Light",
    "light",
    "editor-area-light",
    "rig-rim-fixture",
    "Rect Area Light · cool 6300 K contour rim separation",
    "arealight",
    "Softbox",
  ],
  [
    "rig-accent-tripod",
    "Back-Right Accent Tripod",
    "studio-procedural",
    "studio-tripod",
    "studio-lighting",
    "Shared procedural studio.tripod · wheeled accent strobe stand",
  ],
  [
    "rig-accent-fixture",
    "Octa Softbox Strobe",
    "studio-procedural",
    "studio-octa",
    "rig-accent-tripod",
    "Procedural studio.light_fixture · 8-sided studio octa modifier",
  ],
  [
    "light-accent-ies",
    "Accent Strobe IES Light",
    "light",
    "editor-dome-light",
    "rig-accent-fixture",
    "IES Photometric · focused cyclorama rim strobe",
    "ieslight",
    "IES Downlight",
  ],

  // Camera rigs collection (Tripod -> Camera Body -> Native Project-Zero Camera)
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
    "studio-tripod",
    "cameras",
    "Shared procedural studio.tripod · heavy matte tripod + floor mat",
  ],
  [
    "rig-cam-a-body",
    "Cinema Camera A",
    "studio-procedural",
    "studio-cinema-camera",
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
    "studio-tripod",
    "cameras",
    "Shared procedural studio.tripod · foreground tether camera tripod",
  ],
  [
    "rig-cam-b-body",
    "Medium Format Camera B",
    "studio-procedural",
    "studio-cinema-camera",
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
    "studio-workstation",
    "studio-props",
    "Procedural studio.workstation · hairpin desk, laptop & flight cases",
  ],
  [
    "prop-grip-cases",
    "Grip Cases & Floor Mats",
    "studio-procedural",
    "studio-workstation",
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
].map(([Id, Name, Panel, Icon, Parent, Description, ReferenceType, ReferencePreset]) => ({
  Id,
  Name,
  Panel,
  Icon,
  Parent,
  Description,
  ...(ReferenceType ? { ReferenceType } : {}),
  ...(ReferencePreset ? { ReferencePreset } : {}),
}));

export const StudioLobbyInitialValues = {
  // Vehicles: ONLY Hero Coupe is inside the cyclorama set; the 2 companion vehicles are parked OUTSIDE on the outer glossy tiled studio floor
  "vehicle-hero": {
    Position: [0.0, 0.0, -0.15],
    Rotation: [0, -16, 0],
    YawDeg: -16,
    Scale: 0.76,
    PaintHex: "#111319",
    Metallic: 0.84,
    Roughness: 0.13,
    Clearcoat: 1.0,
    RimHex: "#1b1d22",
    CaliperHex: "#b81d24",
  },
  "vehicle-gt3": {
    Position: [-12.2, 0.0, 4.8],
    Rotation: [0, -24, 0],
    YawDeg: -24,
    Scale: 0.74,
    PaintHex: "#eceff3",
    Metallic: 0.18,
    Roughness: 0.16,
    Clearcoat: 0.98,
    RimHex: "#1d2026",
    CaliperHex: "#c91e26",
  },
  "vehicle-tourer": {
    Position: [12.2, 0.0, 4.8],
    Rotation: [0, -156, 0],
    YawDeg: -156,
    Scale: 0.74,
    PaintHex: "#9ca5b4",
    Metallic: 0.92,
    Roughness: 0.18,
    Clearcoat: 0.96,
    RimHex: "#6f604e",
    CaliperHex: "#d49b24",
  },

  // Studio Architecture (Spacious wide photographic soundstage — prevents camera clipping)
  "studio-cyclorama": {
    ProceduralKind: "studio.cyclorama",
    Position: [0, 0, -0.8],
    Rotation: [0, 0, 0],
    YawDeg: 0,
    CoveWidth: 18.0,
    FloorDepth: 13.5,
    WallHeight: 7.0,
    CoveRadius: 2.2,
    SideWings: true,
    SurfaceTint: "#bfc5ce",
    PlasterGrain: 0.38,
  },
  "studio-floor": {
    ProceduralKind: "studio.tiled_floor",
    Position: [0, 0, 0],
    Rotation: [0, 0, 0],
    YawDeg: 0,
    FloorWidth: 48,
    FloorDepth: 40,
    TileSize: 1.2,
    FloorGloss: 0.68,
    SurfaceTint: "#1c2027",
  },
  "studio-ceiling": {
    ProceduralKind: "studio.ceiling_truss",
    Position: [0, 0, -0.5],
    Rotation: [0, 0, 0],
    YawDeg: 0,
    SpanWidth: 34,
    SpanDepth: 28,
    CeilingHeight: 7.25,
    RibCount: 16,
    TrussGrid: true,
  },

  // Overhead Softbox Bank -> Native Rect Area Light (strictly perpendicular vertical rigging cables)
  "rig-softbox-fixture": {
    ProceduralKind: "studio.light_fixture",
    Position: [0.0, 4.22, -0.25],
    Rotation: [0, 0, 0],
    YawDeg: 0,
    FixtureStyle: "Overhead Softbox",
    ApertureWidth: 7.2,
    ApertureHeight: 4.0,
    HousingDepth: 0.3,
    SuspensionDrop: 2.88,
    BarnDoorFlare: 28,
    YokeTiltDeg: 0,
    UmbrellaRibs: 16,
  },
  "light-overhead-area": {
    LightKind: "Area",
    LightType: 2,
    Intensity: 7.4,
    Kelvin: 5600,
    TintHex: "#f4f7ff",
    Width: 7.2,
    Height: 4.0,
    LocalOffset: [0, -0.16, 0],
    Enabled: true,
    ReferenceInspector: {
      Properties: {
        pos: [0.0, 4.06, -0.25],
        target: [0.0, 0.4, -0.25],
        width: 7.2,
        height: 4.0,
        lumens: 7400,
        spread: 125,
        color: "#f4f7ff",
        temperature: 5600,
        twoSided: false,
        shadows: true,
        showShape: true,
      },
    },
  },

  // Key Fresnel Tripod -> Barn-Door Fresnel -> Native IES Light (Inside set, aimed DEAD-ON at Hero Coupe)
  "rig-key-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [-3.65, 0.0, 1.55],
    Rotation: [0, AimKey.YawDeg, 0],
    YawDeg: AimKey.YawDeg,
    StandHeight: 1.58,
    ColumnLift: 0.34,
    LegSpreadDeg: 27,
    TubeRadius: 0.017,
    LegSegments: 3,
    HeadTiltDeg: AimKey.PitchDeg,
    Spreader: true,
    Casters: true,
    FloorMat: false,
  },
  "rig-key-fixture": {
    ProceduralKind: "studio.light_fixture",
    Position: [0, 0, 0],
    Rotation: [AimKey.PitchDeg, 0, 0],
    YawDeg: 0,
    FixtureStyle: "Barn-Door Fresnel",
    ApertureWidth: 0.48,
    ApertureHeight: 0.48,
    HousingDepth: 0.42,
    BarnDoorFlare: 34,
    YokeTiltDeg: AimKey.PitchDeg,
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
    LocalOffset: [0, 0.28, 0.2],
    Enabled: true,
    ReferenceInspector: {
      Properties: {
        pos: [-3.45, 2.28, 1.55],
        target: [...HeroCarAimTarget],
        profile: "ECE Low Beam",
        lumens: 4500,
        multiplier: 1.4,
        range: 24,
        cone: 46,
        cutoff: -1.0,
        color: "#fff5e8",
        temperature: 5400,
        shadows: true,
        showDistribution: true,
      },
    },
  },

  // Parabolic Umbrella Tripod -> Modifier -> Native Rect Area Light (Inside set, aimed DEAD-ON at Hero Coupe)
  "rig-umbrella-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [3.45, 0.0, 1.85],
    Rotation: [0, AimUmbrella.YawDeg, 0],
    YawDeg: AimUmbrella.YawDeg,
    StandHeight: 1.56,
    ColumnLift: 0.32,
    LegSpreadDeg: 28,
    TubeRadius: 0.016,
    LegSegments: 3,
    HeadTiltDeg: AimUmbrella.PitchDeg,
    Spreader: true,
    Casters: false,
    FloorMat: false,
  },
  "rig-umbrella-fixture": {
    ProceduralKind: "studio.light_fixture",
    Position: [0, 0, 0],
    Rotation: [AimUmbrella.PitchDeg, 0, 0],
    YawDeg: 0,
    FixtureStyle: "Parabolic Umbrella",
    ApertureWidth: 1.48,
    ApertureHeight: 1.48,
    HousingDepth: 0.48,
    BarnDoorFlare: 25,
    YokeTiltDeg: AimUmbrella.PitchDeg,
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
    LocalOffset: [0, 0.24, 0.14],
    Enabled: true,
    ReferenceInspector: {
      Properties: {
        pos: [3.25, 2.12, 1.85],
        target: [...HeroCarAimTarget],
        width: 1.55,
        height: 1.55,
        lumens: 3600,
        spread: 135,
        color: "#edf3ff",
        temperature: 5800,
        twoSided: false,
        shadows: false,
        showShape: true,
      },
    },
  },

  // Back-Left Rim Tripod -> LED Panel -> Native Rect Area Light (Inside set, aimed DEAD-ON at Hero Coupe)
  "rig-rim-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [-4.35, 0.0, -2.65],
    Rotation: [0, AimRim.YawDeg, 0],
    YawDeg: AimRim.YawDeg,
    StandHeight: 1.72,
    ColumnLift: 0.38,
    LegSpreadDeg: 26,
    TubeRadius: 0.017,
    LegSegments: 3,
    HeadTiltDeg: AimRim.PitchDeg,
    Spreader: true,
    Casters: true,
    FloorMat: false,
  },
  "rig-rim-fixture": {
    ProceduralKind: "studio.light_fixture",
    Position: [0, 0, 0],
    Rotation: [AimRim.PitchDeg, 0, 0],
    YawDeg: 0,
    FixtureStyle: "Studio LED Panel",
    ApertureWidth: 1.15,
    ApertureHeight: 0.68,
    HousingDepth: 0.14,
    BarnDoorFlare: 24,
    YokeTiltDeg: AimRim.PitchDeg,
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
    LocalOffset: [0, 0.34, 0.14],
    Enabled: true,
    ReferenceInspector: {
      Properties: {
        pos: [-4.15, 2.52, -2.55],
        target: [...HeroCarAimTarget],
        width: 1.15,
        height: 0.68,
        lumens: 3900,
        spread: 110,
        color: "#e1ecff",
        temperature: 6300,
        twoSided: false,
        shadows: false,
        showShape: true,
      },
    },
  },

  // Back-Right Accent Tripod -> Octa Strobe -> Native IES Light (Inside set, aimed DEAD-ON at Hero Coupe)
  "rig-accent-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [4.55, 0.0, -2.4],
    Rotation: [0, AimAccent.YawDeg, 0],
    YawDeg: AimAccent.YawDeg,
    StandHeight: 1.66,
    ColumnLift: 0.36,
    LegSpreadDeg: 26,
    TubeRadius: 0.016,
    LegSegments: 3,
    HeadTiltDeg: AimAccent.PitchDeg,
    Spreader: true,
    Casters: true,
    FloorMat: false,
  },
  "rig-accent-fixture": {
    ProceduralKind: "studio.light_fixture",
    Position: [0, 0, 0],
    Rotation: [AimAccent.PitchDeg, 0, 0],
    YawDeg: 0,
    FixtureStyle: "Octa Softbox",
    ApertureWidth: 0.96,
    ApertureHeight: 0.96,
    HousingDepth: 0.42,
    BarnDoorFlare: 25,
    YokeTiltDeg: AimAccent.PitchDeg,
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
    LocalOffset: [0, 0.22, 0.22],
    Enabled: true,
    ReferenceInspector: {
      Properties: {
        pos: [4.35, 2.32, -2.3],
        target: [...HeroCarAimTarget],
        profile: "High Beam",
        lumens: 3600,
        multiplier: 1.3,
        range: 22,
        cone: 42,
        cutoff: 0,
        color: "#f7f9ff",
        temperature: 5600,
        shadows: false,
        showDistribution: true,
      },
    },
  },

  // Cinema Tripod A -> Cinema Camera A (OUTSIDE cyclorama front lip on dark glossy tiled floor!)
  "rig-cam-a-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [-3.25, 0.0, 5.75],
    Rotation: [0, AimCamA.YawDeg, 0],
    YawDeg: AimCamA.YawDeg,
    StandHeight: 1.38,
    ColumnLift: 0.2,
    LegSpreadDeg: 24,
    TubeRadius: 0.02,
    LegSegments: 3,
    HeadTiltDeg: AimCamA.PitchDeg,
    Spreader: true,
    Casters: false,
    FloorMat: true,
  },
  "rig-cam-a-body": {
    ProceduralKind: "studio.cinema_camera",
    Position: [0, 0, 0],
    Rotation: [AimCamA.PitchDeg, 0, 0],
    YawDeg: 0,
    PitchDeg: AimCamA.PitchDeg,
    CameraStyle: "Cinema Matte Box",
    LensLength: 0.34,
    ReelRadius: 0.13,
    MatteBox: true,
    TopHandle: true,
  },
  "cam-configurator": {
    "Focal Length": 36,
    Aperture: 2.8,
    "Sensor Width": 36,
    "Subject Distance": 6.4,
    OrbitSpeedDegPerSec: 7.5,
    ShowroomOrbit: true,
    PresetIndex: 0,
  },

  // Cinema Tripod B -> Medium Format Camera B (OUTSIDE cyclorama front lip on dark glossy tiled floor!)
  "rig-cam-b-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [2.55, 0.0, 5.65],
    Rotation: [0, AimCamB.YawDeg, 0],
    YawDeg: AimCamB.YawDeg,
    StandHeight: 1.24,
    ColumnLift: 0.18,
    LegSpreadDeg: 25,
    TubeRadius: 0.018,
    LegSegments: 3,
    HeadTiltDeg: AimCamB.PitchDeg,
    Spreader: true,
    Casters: false,
    FloorMat: true,
  },
  "rig-cam-b-body": {
    ProceduralKind: "studio.cinema_camera",
    Position: [0, 0, 0],
    Rotation: [AimCamB.PitchDeg, 0, 0],
    YawDeg: 0,
    PitchDeg: AimCamB.PitchDeg,
    CameraStyle: "Medium Format Tether",
    LensLength: 0.24,
    ReelRadius: 0.08,
    MatteBox: true,
    TopHandle: true,
  },
  "cam-profile": {
    "Focal Length": 65,
    Aperture: 4.0,
    "Sensor Width": 36,
    "Subject Distance": 7.8,
  },

  // Studio Props & Crew (OUTSIDE the cyclorama set on the dark glossy tiled outer floor!)
  "prop-workstation": {
    ProceduralKind: "studio.workstation",
    Position: [5.9, 0.0, 6.45],
    Rotation: [0, -28, 0],
    YawDeg: -28,
    DeskWidth: 1.48,
    DeskDepth: 0.72,
    DeskHeight: 0.78,
    FlightCases: true,
    CoiledCables: true,
    HasDesk: true,
  },
  "prop-grip-cases": {
    ProceduralKind: "studio.workstation",
    Position: [-6.4, 0.0, 6.25],
    Rotation: [0, 22, 0],
    YawDeg: 22,
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

// ============================================================================
// STUDIO LOBBY 2 · FOREST SOUNDSTAGE (Real 3D Ferns, Fallen Wood, Rock Incline,
// Grass & Shrubs, Blowing Wind Fan, Sunbeam Dust Particles, Same Studio Lights
// + Hero Car Angled on Rock Incline + 3 Companion Cars Positioned Outside Set)
// ============================================================================
const L2AimKey = ComputeAimAngles([-4.75, 2.78, 1.45], Lobby2HeroAimTarget);
const L2AimUmbrella = ComputeAimAngles([4.85, 2.45, 2.15], Lobby2HeroAimTarget);
const L2AimRim = ComputeAimAngles([-4.95, 2.85, -2.55], Lobby2HeroAimTarget);
const L2AimAccent = ComputeAimAngles([5.05, 2.72, -2.25], Lobby2HeroAimTarget);
const L2AimCamA = ComputeAimAngles([-3.65, 1.85, 6.25], Lobby2HeroAimTarget);
const L2AimCamB = ComputeAimAngles([3.15, 1.68, 6.15], Lobby2HeroAimTarget);
const L2AimFan = ComputeAimAngles([-5.25, 1.28, 1.65], [0.4, 0.85, 0.35]);

export const StudioLobby2Rows = [
  // Vehicles collection: 1 Hero perched & angled on rock incline + 3 companion vehicles outside set
  ["studio-vehicles", "Vehicles", "group", "folder-scene", null, "Forest Stage lineup · 1 Hero on rock incline + 3 staging cars"],
  [
    "vehicle-hero",
    "Hero Vehicle · Rock Incline",
    "studio-vehicle",
    "studio-vehicle",
    "studio-vehicles",
    "ControlledIntegrator · Forest Sage perched & angled on granite rock incline",
  ],
  [
    "vehicle-gt3",
    "Obsidian Coupe · Left Bay",
    "studio-vehicle",
    "studio-vehicle",
    "studio-vehicles",
    "ControlledIntegrator · Companion vehicle #1 parked on left studio floor",
  ],
  [
    "vehicle-tourer",
    "Alpine GT · Right Bay",
    "studio-vehicle",
    "studio-vehicle",
    "studio-vehicles",
    "ControlledIntegrator · Companion vehicle #2 parked on right studio floor",
  ],
  [
    "vehicle-reserve",
    "Liquid Tourer · Outer Bay",
    "studio-vehicle",
    "studio-vehicle",
    "studio-vehicles",
    "ControlledIntegrator · Companion vehicle #3 parked in outer right staging bay",
  ],

  // Forest Soundstage Natural Set & Studio Architecture
  ["studio-architecture", "Forest Soundstage Set", "group", "folder-world", null, "LED forest backdrop, rock incline, fallen wood, ferns & grass"],
  [
    "l2-forest-backdrop",
    "Forest LED Backdrop & Flags",
    "studio-procedural",
    "studio-backdrop",
    "studio-architecture",
    "Backlit sunlit forest LED volume wall + proscenium + V-flat flags",
  ],
  [
    "l2-rock-outcrop",
    "Mossy Rock Incline & Stream",
    "studio-procedural",
    "studio-rock",
    "studio-architecture",
    "Granite rock incline outcrop, mossy boulders, river pebbles & forest stream",
  ],
  [
    "l2-fallen-wood",
    "Gnarled Fallen Log & Roots",
    "studio-procedural",
    "studio-wood",
    "studio-architecture",
    "Weathered sculpted fallen tree trunk & twisted roots on foreground moss",
  ],
  [
    "l2-fern-cluster",
    "Real 3D Forest Ferns",
    "studio-procedural",
    "studio-fern",
    "studio-architecture",
    "Multi-frond 3D pinnate ferns blowing dynamically in the studio fan wind",
  ],
  [
    "l2-grass-plants",
    "Grass Tufts & Woodland Shrubs",
    "studio-procedural",
    "studio-grass",
    "studio-architecture",
    "3D grass blades, leafy shrubs & bamboo shoots blowing in the fan wind",
  ],
  [
    "studio-floor",
    "Studio Concrete Floor",
    "studio-procedural",
    "studio-floor-grid",
    "studio-architecture",
    "Dark matte-satin concrete soundstage floor surrounding the forest island",
  ],
  [
    "studio-ceiling",
    "Ribbed Ceiling & Truss",
    "studio-procedural",
    "studio-ceiling-truss",
    "studio-architecture",
    "Dark overhead steel truss grid supporting softbox & lighting",
  ],

  // Wind Machine & Atmospheric Dust Effects
  ["studio-atmosphere", "Wind Fan & Dust FX", "group", "wind", null, "Live studio wind blower & sunlit floating dust particles"],
  [
    "l2-studio-fan",
    "Studio Wind Fan (Blower)",
    "studio-procedural",
    "studio-fan",
    "studio-atmosphere",
    "Wheeled spinning studio wind fan blowing ferns, grass & dust particles",
  ],
  [
    "l2-dust-particles",
    "Sunbeam Dust Particles",
    "studio-procedural",
    "studio-dust",
    "studio-atmosphere",
    "Floating sunlit dust motes & spores advected by the Studio Wind Fan",
  ],

  // Lighting Rigs (EXACT same studio lighting suite as Studio Lobby 1!)
  ["studio-lighting", "Lighting Rigs", "group", "folder-generic", null, "Same Studio Lobby hierarchical stands, softbox & native Area/IES lights"],
  [
    "rig-softbox-fixture",
    "Overhead Softbox Bank",
    "studio-procedural",
    "studio-softbox",
    "studio-lighting",
    "Suspended overhead silk diffuser with straight vertical hoist cables",
  ],
  [
    "light-overhead-area",
    "Overhead Softbox Area Light",
    "light",
    "editor-area-light",
    "rig-softbox-fixture",
    "Rect Area Light · wide overhead skylight softbox",
    "arealight",
    "Softbox",
  ],
  [
    "rig-key-tripod",
    "Key Fresnel Tripod",
    "studio-procedural",
    "studio-tripod",
    "studio-lighting",
    "Left stage Fresnel light stand casting warm golden woodland beam",
  ],
  [
    "rig-key-fixture",
    "Barn-Door Fresnel Fixture",
    "studio-procedural",
    "studio-fresnel",
    "rig-key-tripod",
    "Articulated 4-leaf barn-door studio Fresnel head",
  ],
  [
    "light-key-ies",
    "Key Fresnel IES Light",
    "light",
    "editor-dome-light",
    "rig-key-fixture",
    "IES Photometric · warm golden Fresnel sunbeam key",
    "ieslight",
    "IES Downlight",
  ],
  [
    "rig-umbrella-tripod",
    "Umbrella Fill Tripod",
    "studio-procedural",
    "studio-tripod",
    "studio-lighting",
    "Front-right silver bounce umbrella stand",
  ],
  [
    "rig-umbrella-fixture",
    "Parabolic Umbrella Modifier",
    "studio-procedural",
    "studio-umbrella",
    "rig-umbrella-tripod",
    "16-rib parabolic silver bounce modifier",
  ],
  [
    "light-umbrella-area",
    "Umbrella Bounce Area Light",
    "light",
    "editor-area-light",
    "rig-umbrella-fixture",
    "Rect Area Light · gentle woodland ambient fill",
    "arealight",
    "Softbox",
  ],
  [
    "rig-rim-tripod",
    "Back-Left Rim Tripod",
    "studio-procedural",
    "studio-tripod",
    "studio-lighting",
    "Back-left LED rim light stand",
  ],
  [
    "rig-rim-fixture",
    "Rim LED Panel Fixture",
    "studio-procedural",
    "studio-led-panel",
    "rig-rim-tripod",
    "Edge-separation daylight forest rim LED panel",
  ],
  [
    "light-rim-area",
    "Rim LED Area Light",
    "light",
    "editor-area-light",
    "rig-rim-fixture",
    "Rect Area Light · crisp canopy edge rim",
    "arealight",
    "Softbox",
  ],
  [
    "rig-accent-tripod",
    "Back-Right Accent Tripod",
    "studio-procedural",
    "studio-tripod",
    "studio-lighting",
    "Right stage high-stand backlight Fresnel/Octa",
  ],
  [
    "rig-accent-fixture",
    "Octa Softbox Strobe",
    "studio-procedural",
    "studio-octa",
    "rig-accent-tripod",
    "Right flank warm sun-rim studio head",
  ],
  [
    "light-accent-ies",
    "Accent Strobe IES Light",
    "light",
    "editor-dome-light",
    "rig-accent-fixture",
    "IES Photometric · warm sunbeam right backlight",
    "ieslight",
    "IES Downlight",
  ],

  // Camera Rigs (outside the forest diorama set, aimed at elevated Hero Vehicle)
  ["cameras", "Camera Rigs", "group", "camera", null, "Unrestricted Editor Camera & Game Camera rigs"],
  [
    "camera",
    "Editor Camera",
    "camera",
    "camera",
    "cameras",
    "Unrestricted free 3D editor navigation camera",
  ],
  [
    "rig-cam-a-tripod",
    "Cinema Tripod A",
    "studio-procedural",
    "studio-tripod",
    "cameras",
    "Foreground left cinema camera tripod on rubber mat",
  ],
  [
    "rig-cam-a-body",
    "Cinema Camera A",
    "studio-procedural",
    "studio-cinema-camera",
    "rig-cam-a-tripod",
    "Cinema matte-box camera mounted on Tripod A",
  ],
  [
    "cam-configurator",
    "Car Configurator Camera",
    "camera",
    "camera",
    "rig-cam-a-body",
    "Moving Game Camera · Opening Reveal + Showroom Orbit",
  ],
  [
    "rig-cam-b-tripod",
    "Cinema Tripod B",
    "studio-procedural",
    "studio-tripod",
    "cameras",
    "Foreground right secondary camera tripod",
  ],
  [
    "rig-cam-b-body",
    "Cinema Camera B",
    "studio-procedural",
    "studio-cinema-camera",
    "rig-cam-b-tripod",
    "Medium format tether camera mounted on Tripod B",
  ],
  [
    "cam-profile",
    "Profile 65mm Camera",
    "camera",
    "camera",
    "rig-cam-b-body",
    "65mm portrait camera aimed at Hero Vehicle on rock incline",
  ],

  // Outer Studio Crew Props & Ambient
  ["studio-props", "Studio Props & Crew", "group", "folder-generic", null, "Tether desk & grip cases outside forest island"],
  [
    "prop-workstation",
    "Tether Workstation & Desk",
    "studio-procedural",
    "studio-workstation",
    "studio-props",
    "Hairpin studio desk & tether display outside forest set",
  ],
  [
    "prop-grip-cases",
    "Grip Cases & Cables",
    "studio-procedural",
    "studio-workstation",
    "studio-props",
    "Rugged studio cases & coiled power cabling outside set",
  ],
  [
    "studio-ambient",
    "Studio Atmosphere & Exposure",
    "studio-procedural",
    "environment-exposure",
    "studio-props",
    "Exposure, forest haze ambient fill & vignette",
  ],
].map(([Id, Name, Panel, Icon, Parent, Description, ReferenceType, ReferencePreset]) => ({
  Id,
  Name,
  Panel,
  Icon,
  Parent,
  Description,
  ...(ReferenceType ? { ReferenceType } : {}),
  ...(ReferencePreset ? { ReferencePreset } : {}),
}));

export const StudioLobby2InitialValues = {
  ...JSON.parse(JSON.stringify(StudioLobbyInitialValues)),

  // 1) Hero Vehicle perched & angled heroically up the granite rock incline (Forest Sage like Ref 1 & 2!)
  "vehicle-hero": {
    Position: [0.0, 0.56, -0.35],
    Rotation: [8.5, -22, 3.8],
    YawDeg: -22,
    InclineDeg: 8.5,
    BankDeg: 3.8,
    Scale: 0.78,
    PaintHex: "#7ea689",
    Metallic: 0.42,
    Roughness: 0.16,
    Clearcoat: 0.98,
    RimHex: "#1e2126",
    CaliperHex: "#8c9c7a",
  },
  // 3 Companion Vehicles positioned cleanly outside the forest set on the surrounding studio floor
  "vehicle-gt3": {
    Position: [-11.8, 0.0, 4.2],
    Rotation: [0, -24, 0],
    YawDeg: -24,
    InclineDeg: 0,
    BankDeg: 0,
    Scale: 0.74,
    PaintHex: "#111319",
    Metallic: 0.84,
    Roughness: 0.14,
    Clearcoat: 1.0,
    RimHex: "#cfd5de",
    CaliperHex: "#b81d24",
  },
  "vehicle-tourer": {
    Position: [11.8, 0.0, 4.2],
    Rotation: [0, -156, 0],
    YawDeg: -156,
    InclineDeg: 0,
    BankDeg: 0,
    Scale: 0.74,
    PaintHex: "#eceff3",
    Metallic: 0.18,
    Roughness: 0.16,
    Clearcoat: 0.98,
    RimHex: "#1d1f24",
    CaliperHex: "#b81d24",
  },
  "vehicle-reserve": {
    Position: [10.8, 0.0, 9.4],
    Rotation: [0, -138, 0],
    YawDeg: -138,
    InclineDeg: 0,
    BankDeg: 0,
    Scale: 0.74,
    PaintHex: "#9ca5b4",
    Metallic: 0.92,
    Roughness: 0.18,
    Clearcoat: 0.95,
    RimHex: "#7d6850",
    CaliperHex: "#1e4d9b",
  },

  // Forest Soundstage Architecture & Natural Elements
  "l2-forest-backdrop": {
    ProceduralKind: "studio.forest_backdrop",
    Position: [0.0, 0.0, -5.65],
    Rotation: [0, 0, 0],
    YawDeg: 0,
    ScreenWidth: 12.4,
    ScreenHeight: 6.2,
    ProsceniumWidth: 20.5,
    WallHeight: 7.25,
    StudioFlags: true,
    BackdropBrightness: 1.25,
  },
  "l2-rock-outcrop": {
    ProceduralKind: "studio.rock_outcrop",
    Position: [0.0, 0.0, -0.35],
    Rotation: [0, 0, 0],
    YawDeg: 0,
    IslandWidth: 11.4,
    IslandDepth: 9.2,
    InclineHeight: 0.56,
    InclineDeg: 8.5,
    BankDeg: 3.8,
    VehicleYawDeg: -22,
    RockScale: 1.0,
    HasStream: true,
  },
  "l2-fallen-wood": {
    ProceduralKind: "studio.fallen_wood",
    Position: [2.35, 0.02, 2.25],
    Rotation: [0, -14, 0],
    YawDeg: -14,
    LogScale: 1.05,
  },
  "l2-fern-cluster": {
    ProceduralKind: "studio.fern_cluster",
    Position: [0.0, 0.0, -0.35],
    Rotation: [0, 0, 0],
    YawDeg: 0,
    FernCount: 15,
    FernScale: 1.05,
  },
  "l2-grass-plants": {
    ProceduralKind: "studio.grass_plants",
    Position: [0.0, 0.0, -0.35],
    Rotation: [0, 0, 0],
    YawDeg: 0,
    TuftCount: 56,
    GrassScale: 1.05,
  },
  "studio-floor": {
    ProceduralKind: "studio.tiled_floor",
    Position: [0.0, -0.004, 1.5],
    FloorWidth: 46.0,
    FloorDepth: 40.0,
    TileSize: 1.8,
    FloorGloss: 0.42,
    SurfaceTint: "#1c1f24",
  },
  "studio-ceiling": {
    ProceduralKind: "studio.ceiling_truss",
    Position: [0.0, 0.0, -0.8],
    CeilingWidth: 26.0,
    CeilingDepth: 22.0,
    CeilingHeight: 7.25,
    RibCount: 14,
  },

  // Studio Wind Fan & Floating Sunbeam Dust Particles
  "l2-studio-fan": {
    ProceduralKind: "studio.studio_fan",
    Position: [-5.25, 0.0, 1.65],
    Rotation: [0, L2AimFan.YawDeg, 0],
    YawDeg: L2AimFan.YawDeg,
    StandHeight: 1.28,
    FanRadius: 0.52,
    TiltDeg: -4,
    WindSpeed: 0.82,
    GustTurbulence: 0.55,
  },
  "l2-dust-particles": {
    ProceduralKind: "studio.dust_field",
    Position: [0.0, 2.1, -0.2],
    Rotation: [0, 0, 0],
    YawDeg: 0,
    EmitterRadius: 0.14,
    ParticleCount: 280,
    DriftSpeed: 0.72,
    GlowIntensity: 1.05,
    MoteSize: 4.8,
  },

  // Same Studio Lighting Rigs aimed cleanly at the elevated Hero Vehicle on the rock incline!
  "rig-softbox-fixture": {
    ProceduralKind: "studio.light_fixture",
    FixtureStyle: "Overhead Softbox",
    Position: [0.0, 4.42, -0.45],
    Rotation: [0, 0, 0],
    YawDeg: 0,
    ApertureWidth: 6.4,
    ApertureHeight: 3.8,
    HousingDepth: 0.3,
    SuspensionDrop: 2.68,
    BarnDoorSpread: 40,
    YokeTiltDeg: 0,
  },
  "rig-key-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [-4.75, 0.0, 1.45],
    Rotation: [0, L2AimKey.YawDeg, 0],
    YawDeg: L2AimKey.YawDeg,
    StandHeight: 2.35,
    ColumnLift: 0.35,
    LegSpread: 0.78,
    TubeRadius: 0.022,
    Casters: true,
    FloorMat: false,
    HeadTiltDeg: L2AimKey.PitchDeg,
  },
  "rig-key-fixture": {
    ProceduralKind: "studio.light_fixture",
    FixtureStyle: "Barn-Door Fresnel",
    Position: [0, 0, 0],
    ApertureWidth: 0.44,
    ApertureHeight: 0.44,
    HousingDepth: 0.42,
    BarnDoorSpread: 44,
    YokeTiltDeg: L2AimKey.PitchDeg,
  },
  "light-key-ies": {
    ...JSON.parse(JSON.stringify(StudioLobbyInitialValues["light-key-ies"])),
    ReferenceInspector: {
      Preset: "IES Downlight",
      Properties: {
        ...StudioLobbyInitialValues["light-key-ies"].ReferenceInspector.Properties,
        lumens: 15500,
        temperature: 3450,
        cone: 54,
        target: [...Lobby2HeroAimTarget],
      },
    },
  },
  "rig-umbrella-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [4.85, 0.0, 2.15],
    Rotation: [0, L2AimUmbrella.YawDeg, 0],
    YawDeg: L2AimUmbrella.YawDeg,
    StandHeight: 2.08,
    ColumnLift: 0.29,
    LegSpread: 0.72,
    TubeRadius: 0.02,
    Casters: true,
    FloorMat: false,
    HeadTiltDeg: L2AimUmbrella.PitchDeg,
  },
  "rig-umbrella-fixture": {
    ProceduralKind: "studio.light_fixture",
    FixtureStyle: "Parabolic Umbrella",
    Position: [0, 0, 0],
    ApertureWidth: 1.55,
    ApertureHeight: 1.55,
    HousingDepth: 0.52,
    BarnDoorSpread: 35,
    YokeTiltDeg: L2AimUmbrella.PitchDeg,
  },
  "rig-rim-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [-4.95, 0.0, -2.55],
    Rotation: [0, L2AimRim.YawDeg, 0],
    YawDeg: L2AimRim.YawDeg,
    StandHeight: 2.42,
    ColumnLift: 0.35,
    LegSpread: 0.76,
    TubeRadius: 0.021,
    Casters: true,
    FloorMat: false,
    HeadTiltDeg: L2AimRim.PitchDeg,
  },
  "rig-rim-fixture": {
    ProceduralKind: "studio.light_fixture",
    FixtureStyle: "Studio LED Panel",
    Position: [0, 0, 0],
    ApertureWidth: 1.05,
    ApertureHeight: 0.62,
    HousingDepth: 0.14,
    BarnDoorSpread: 30,
    YokeTiltDeg: L2AimRim.PitchDeg,
  },
  "rig-accent-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [5.05, 0.0, -2.25],
    Rotation: [0, L2AimAccent.YawDeg, 0],
    YawDeg: L2AimAccent.YawDeg,
    StandHeight: 2.32,
    ColumnLift: 0.32,
    LegSpread: 0.74,
    TubeRadius: 0.021,
    Casters: true,
    FloorMat: false,
    HeadTiltDeg: L2AimAccent.PitchDeg,
  },
  "rig-accent-fixture": {
    ProceduralKind: "studio.light_fixture",
    FixtureStyle: "Barn-Door Fresnel",
    Position: [0, 0, 0],
    ApertureWidth: 0.42,
    ApertureHeight: 0.42,
    HousingDepth: 0.4,
    BarnDoorSpread: 42,
    YokeTiltDeg: L2AimAccent.PitchDeg,
  },
  "light-accent-ies": {
    ...JSON.parse(JSON.stringify(StudioLobbyInitialValues["light-accent-ies"])),
    ReferenceInspector: {
      Preset: "IES Downlight",
      Properties: {
        ...StudioLobbyInitialValues["light-accent-ies"].ReferenceInspector.Properties,
        lumens: 14200,
        temperature: 3250,
        cone: 52,
        target: [...Lobby2HeroAimTarget],
      },
    },
  },

  // Foreground Camera Rigs (outside the forest moss island)
  "rig-cam-a-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [-3.65, 0.0, 6.25],
    Rotation: [0, L2AimCamA.YawDeg, 0],
    YawDeg: L2AimCamA.YawDeg,
    StandHeight: 1.56,
    ColumnLift: 0.26,
    LegSpread: 0.72,
    TubeRadius: 0.024,
    Casters: false,
    FloorMat: true,
    HeadTiltDeg: L2AimCamA.PitchDeg,
  },
  "rig-cam-a-body": {
    ProceduralKind: "studio.cinema_camera",
    CameraStyle: "Cinema Matte Box",
    Position: [0, 0, 0],
    BodyLength: 0.44,
    LensLength: 0.32,
    MatteBoxWidth: 0.28,
    PitchDeg: L2AimCamA.PitchDeg,
  },
  "rig-cam-b-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [3.15, 0.0, 6.15],
    Rotation: [0, L2AimCamB.YawDeg, 0],
    YawDeg: L2AimCamB.YawDeg,
    StandHeight: 1.42,
    ColumnLift: 0.23,
    LegSpread: 0.66,
    TubeRadius: 0.021,
    Casters: false,
    FloorMat: true,
    HeadTiltDeg: L2AimCamB.PitchDeg,
  },
  "rig-cam-b-body": {
    ProceduralKind: "studio.cinema_camera",
    CameraStyle: "Medium Format Tether",
    Position: [0, 0, 0],
    BodyLength: 0.32,
    LensLength: 0.22,
    MatteBoxWidth: 0.2,
    PitchDeg: L2AimCamB.PitchDeg,
  },

  "prop-workstation": {
    ProceduralKind: "studio.workstation",
    Position: [6.4, 0.0, 6.85],
    Rotation: [0, -28, 0],
    YawDeg: -28,
    DeskWidth: 1.48,
    DeskDepth: 0.72,
    DeskHeight: 0.78,
    FlightCases: true,
    CoiledCables: true,
    HasDesk: true,
  },
  "prop-grip-cases": {
    ProceduralKind: "studio.workstation",
    Position: [-6.8, 0.0, 6.55],
    Rotation: [0, 22, 0],
    YawDeg: 22,
    DeskWidth: 1.0,
    DeskDepth: 0.55,
    DeskHeight: 0.65,
    FlightCases: true,
    CoiledCables: true,
    HasDesk: false,
  },

  "studio-ambient": {
    ProceduralKind: "studio.ambient",
    Exposure: 1.18,
    AmbientFill: 0.16,
    ShadowDepth: 0.75,
    Vignette: 0.3,
    ShowLightGizmos: false,
  },
};

// ============================================================================
// STUDIO LOBBY 3 · MECHANICS GARAGE AT A RACING STADIUM
// Open pit-lane archway overlooking sunlit tiered stadium grandstands,
// galvanized steel bar shutters, 4-post hydraulic car lift holding a raised car,
// two-story mezzanine catwalks, overhead linear LED shop bars, rolling tool cart,
// cherry-red toolboxes, racing tire rack, snaking air hoses & golden sunbeams!
// ============================================================================
const L3AimKey = ComputeAimAngles([-4.2, 3.15, -3.65], Lobby3HeroAimTarget);
const L3AimUmbrella = ComputeAimAngles([4.85, 2.25, 2.65], Lobby3HeroAimTarget);
const L3AimRim = ComputeAimAngles([-4.6, 2.55, 3.25], Lobby3HeroAimTarget);
const L3AimAccent = ComputeAimAngles([4.95, 2.48, -2.55], Lobby3HeroAimTarget);
const L3AimCamA = ComputeAimAngles([-2.85, 1.75, 6.45], Lobby3HeroAimTarget);
const L3AimCamB = ComputeAimAngles([3.85, 1.58, 6.25], Lobby3HeroAimTarget);

export const StudioLobby3Rows = [
  // Vehicles collection: Hero Signal-Orange GT + Elevated Car on Hydraulic Lift + 2 Paddock Cars
  ["studio-vehicles", "Vehicles", "group", "folder-scene", null, "Racing Stadium Garage lineup · 4 cars (including 1 on hydraulic lift)"],
  [
    "vehicle-hero",
    "Hero GT · Pit Bay Center",
    "studio-vehicle",
    "studio-vehicle",
    "studio-vehicles",
    "ControlledIntegrator · Signal Orange GT bathed in stadium pit-lane sunbeam",
  ],
  [
    "vehicle-gt3",
    "Alpine GT · Right Pit Bay",
    "studio-vehicle",
    "studio-vehicle",
    "studio-vehicles",
    "ControlledIntegrator · Paddock GT car staged in right bay",
  ],
  [
    "vehicle-tourer",
    "Midnight Cobalt · Outer Paddock",
    "studio-vehicle",
    "studio-vehicle",
    "studio-vehicles",
    "ControlledIntegrator · Reserve track car on outer pit apron",
  ],

  // Mechanics Garage Architecture, Hydraulic Car Lift (with Elevated Car child!) & Workshop Equipment
  ["studio-architecture", "Racing Stadium Garage", "group", "folder-world", null, "Pit-lane archway, sunlit stadium grandstands, steel cages, mezzanine & hydraulic lift"],
  [
    "l3-garage-shell",
    "Stadium Pit Garage & Grandstands",
    "studio-procedural",
    "studio-stadium",
    "studio-architecture",
    "Concrete pit bay portal, sunlit racing stadium grandstands, steel bar cages, mezzanine & linear LEDs",
  ],
  [
    "l3-car-lift",
    "Hydraulic 4-Post Car Lift",
    "studio-procedural",
    "studio-lift",
    "studio-architecture",
    "Heavy hydraulic mechanic lift · LiftHeight raises & lowers the elevated car live",
  ],
  [
    "vehicle-lift",
    "Track Coupe · On Hydraulic Lift",
    "studio-vehicle",
    "studio-vehicle",
    "l3-car-lift",
    "ControlledIntegrator · Elevated race car mounted directly on the hydraulic lift deck",
  ],
  [
    "studio-floor",
    "Polished Concrete Pit Floor",
    "studio-procedural",
    "studio-floor-grid",
    "studio-architecture",
    "Tire-scuffed glossy dark concrete motorsport garage floor",
  ],

  // Mechanics Workshop Props, Tool Carts, Tire Rack & Snaking Air Hoses
  ["studio-props", "Mechanics Equipment & Tools", "group", "studio-toolbox", null, "Rolling cherry-red toolboxes, racing tire rack, hydraulic jack & floor hoses"],
  [
    "l3-tool-cart",
    "Rolling Cherry-Red Tool Cart",
    "studio-procedural",
    "studio-toolbox",
    "studio-props",
    "Stainless service cart with stacked cherry-red toolboxes, spare slicks & oil can",
  ],
  [
    "l3-tire-rack",
    "Racing Slick Tire Rack & Jack",
    "studio-procedural",
    "studio-garage",
    "studio-props",
    "Two-tier motorsport tire rack, hydraulic floor trolley jack & yellow jack stands",
  ],
  [
    "l3-diag-cabinet",
    "Diagnostic Telemetry Cabinet",
    "studio-procedural",
    "studio-workstation",
    "studio-props",
    "Rolling red diagnostic chest & live pit telemetry screen",
  ],
  [
    "l3-garage-cables",
    "Floor Air Hoses & Cable Ramp",
    "studio-procedural",
    "studio-cables",
    "studio-props",
    "Snaking pneumatic hoses, power cables & yellow rubber floor cable ramp",
  ],
  [
    "l3-dust-particles",
    "Sunbeam Garage Dust Motes",
    "studio-procedural",
    "studio-dust",
    "studio-props",
    "Warm atmospheric workshop dust floating through the pit-lane sunlight shaft",
  ],
  [
    "studio-ambient",
    "Garage Exposure & Atmosphere",
    "studio-procedural",
    "environment-exposure",
    "studio-props",
    "ACES exposure, dark concrete contrast & warm pit-lane bounce",
  ],

  // Lighting Rigs (Overhead LED Bank + Golden Stadium Portal Sunlight + Worklamps)
  ["studio-lighting", "Garage Lighting Rigs", "group", "folder-generic", null, "Linear overhead LED shop bank, stadium portal sunlight & warm mechanic lamps"],
  [
    "rig-softbox-fixture",
    "Overhead Linear Shop Bank",
    "studio-procedural",
    "studio-softbox",
    "studio-lighting",
    "Suspended overhead factory light bank with straight vertical rigging",
  ],
  [
    "light-overhead-area",
    "Overhead Shop LED Area Light",
    "light",
    "editor-area-light",
    "rig-softbox-fixture",
    "Rect Area Light · crisp 5800 K overhead factory LED bay illumination",
    "arealight",
    "Softbox",
  ],
  [
    "rig-key-tripod",
    "Stadium Portal Sun Stand",
    "studio-procedural",
    "studio-tripod",
    "studio-lighting",
    "Golden afternoon stadium pit-lane sunlight key stand",
  ],
  [
    "rig-key-fixture",
    "Barn-Door Fresnel Fixture",
    "studio-procedural",
    "studio-fresnel",
    "rig-key-tripod",
    "Warm golden stadium sunbeam directional head",
  ],
  [
    "light-key-ies",
    "Stadium Sunbeam IES Light",
    "light",
    "editor-dome-light",
    "rig-key-fixture",
    "IES Photometric · golden late-afternoon sunlight streaming through pit portal",
    "ieslight",
    "IES Downlight",
  ],
  [
    "rig-umbrella-tripod",
    "Pit Bay Fill Tripod",
    "studio-procedural",
    "studio-tripod",
    "studio-lighting",
    "Front-right silver bounce fill stand",
  ],
  [
    "rig-umbrella-fixture",
    "Parabolic Umbrella Modifier",
    "studio-procedural",
    "studio-umbrella",
    "rig-umbrella-tripod",
    "16-rib parabolic silver bounce modifier",
  ],
  [
    "light-umbrella-area",
    "Pit Bay Fill Area Light",
    "light",
    "editor-area-light",
    "rig-umbrella-fixture",
    "Rect Area Light · soft ambient garage bounce",
    "arealight",
    "Softbox",
  ],
  [
    "rig-rim-tripod",
    "Workshop Rim Tripod",
    "studio-procedural",
    "studio-tripod",
    "studio-lighting",
    "Left bay LED rim separation stand",
  ],
  [
    "rig-rim-fixture",
    "Rim LED Panel Fixture",
    "studio-procedural",
    "studio-led-panel",
    "rig-rim-tripod",
    "Cool industrial workshop contour LED panel",
  ],
  [
    "light-rim-area",
    "Workshop Rim Area Light",
    "light",
    "editor-area-light",
    "rig-rim-fixture",
    "Rect Area Light · cool metallic contour edge light",
    "arealight",
    "Softbox",
  ],
  [
    "rig-accent-tripod",
    "Mechanic Worklamp Tripod",
    "studio-procedural",
    "studio-tripod",
    "studio-lighting",
    "Warm amber mechanic's spotlight stand by right cage wall",
  ],
  [
    "rig-accent-fixture",
    "Barn-Door Worklamp Head",
    "studio-procedural",
    "studio-fresnel",
    "rig-accent-tripod",
    "Warm tungsten workshop halogen worklamp",
  ],
  [
    "light-accent-ies",
    "Mechanic Worklamp IES Light",
    "light",
    "editor-dome-light",
    "rig-accent-fixture",
    "IES Photometric · warm 3350 K workshop accent beam",
    "ieslight",
    "IES Downlight",
  ],

  // Camera Rigs
  ["cameras", "Camera Rigs", "group", "camera", null, "Unrestricted Editor Camera & Pit Garage Game Camera rigs"],
  [
    "camera",
    "Editor Camera",
    "camera",
    "camera",
    "cameras",
    "Unrestricted free 360° editor navigation camera",
  ],
  [
    "rig-cam-a-tripod",
    "Cinema Tripod A",
    "studio-procedural",
    "studio-tripod",
    "cameras",
    "Pit garage cinema camera tripod on rubber mat",
  ],
  [
    "rig-cam-a-body",
    "Cinema Camera A",
    "studio-procedural",
    "studio-cinema-camera",
    "rig-cam-a-tripod",
    "Cinema matte-box camera mounted on Tripod A",
  ],
  [
    "cam-configurator",
    "Car Configurator Camera",
    "camera",
    "camera",
    "rig-cam-a-body",
    "Moving Game Camera · Pit Bay Opening Reveal + Showroom Orbit",
  ],
  [
    "rig-cam-b-tripod",
    "Cinema Tripod B",
    "studio-procedural",
    "studio-tripod",
    "cameras",
    "Foreground right secondary pit camera tripod",
  ],
  [
    "rig-cam-b-body",
    "Cinema Camera B",
    "studio-procedural",
    "studio-cinema-camera",
    "rig-cam-b-tripod",
    "Medium format tether camera mounted on Tripod B",
  ],
  [
    "cam-profile",
    "Profile 65mm Camera",
    "camera",
    "camera",
    "rig-cam-b-body",
    "65mm pit-lane profile camera aimed at Signal Orange Hero GT",
  ],
].map(([Id, Name, Panel, Icon, Parent, Description, ReferenceType, ReferencePreset]) => ({
  Id,
  Name,
  Panel,
  Icon,
  Parent,
  Description,
  ...(ReferenceType ? { ReferenceType } : {}),
  ...(ReferencePreset ? { ReferencePreset } : {}),
}));

export const StudioLobby3InitialValues = {
  ...JSON.parse(JSON.stringify(StudioLobbyInitialValues)),

  // 1) Signal Orange Hero GT car parked heroically in the sunlit pit-bay shaft (Ref Image 1!)
  "vehicle-hero": {
    Position: [0.35, 0.0, 0.35],
    Rotation: [0, -146, 0],
    YawDeg: -146,
    InclineDeg: 0,
    BankDeg: 0,
    Scale: 0.78,
    PaintHex: "#e86512",
    Metallic: 0.62,
    Roughness: 0.14,
    Clearcoat: 1.0,
    RimHex: "#1b1d22",
    CaliperHex: "#e86512",
  },
  // 2) Elevated Track Coupe mounted directly atop the 4-post Hydraulic Car Lift (Ref Image 2!)
  "vehicle-lift": {
    Position: [0.0, 0.0, 0.0],
    Rotation: [0, 0, 0],
    YawDeg: 0,
    InclineDeg: 0,
    BankDeg: 0,
    Scale: 0.74,
    PaintHex: "#111319",
    Metallic: 0.86,
    Roughness: 0.14,
    Clearcoat: 1.0,
    RimHex: "#cfd5de",
    CaliperHex: "#b81d24",
  },
  // 3) Companion GT parked in right paddock bay
  "vehicle-gt3": {
    Position: [8.35, 0.0, 3.85],
    Rotation: [0, -156, 0],
    YawDeg: -156,
    InclineDeg: 0,
    BankDeg: 0,
    Scale: 0.75,
    PaintHex: "#eceff3",
    Metallic: 0.18,
    Roughness: 0.15,
    Clearcoat: 0.98,
    RimHex: "#1d1f24",
    CaliperHex: "#b81d24",
  },
  // 4) Reserve track car on outer pit apron
  "vehicle-tourer": {
    Position: [-8.65, 0.0, 7.65],
    Rotation: [0, -28, 0],
    YawDeg: -28,
    InclineDeg: 0,
    BankDeg: 0,
    Scale: 0.75,
    PaintHex: "#13294b",
    Metallic: 0.85,
    Roughness: 0.16,
    Clearcoat: 1.0,
    RimHex: "#7d6850",
    CaliperHex: "#e86512",
  },

  // Racing Stadium Pit Garage Shell & Sunlit Grandstand Vista
  "l3-garage-shell": {
    ProceduralKind: "studio.garage_shell",
    Position: [0.0, 0.0, 0.0],
    Rotation: [0, 0, 0],
    YawDeg: 0,
    GarageWidth: 22.0,
    GarageDepth: 18.0,
    GarageHeight: 7.15,
    ScreenWidth: 18.4,
    ScreenHeight: 8.2,
    BackdropCenterX: -3.8,
    BackdropBrightness: 1.32,
    ShowMezzanine: true,
    ShowLinearLeds: true,
  },
  // Heavy 4-Post Hydraulic Car Lift (parent of vehicle-lift!)
  "l3-car-lift": {
    ProceduralKind: "studio.car_lift",
    Position: [-6.4, 0.0, -1.35],
    Rotation: [0, 8, 0],
    YawDeg: 8,
    LiftHeight: 2.12,
    PostHeight: 3.85,
  },
  // Glossy dark concrete garage floor with subtle tire-scuff slabs & crisp reflections
  "studio-floor": {
    ProceduralKind: "studio.tiled_floor",
    Position: [0.0, -0.004, 1.5],
    FloorWidth: 44.0,
    FloorDepth: 36.0,
    TileSize: 2.8,
    FloorGloss: 0.76,
    SurfaceTint: "#202226",
  },

  // Mechanics Props: Rolling Tool Cart, Tire Rack, Diagnostic Chest, Snaking Floor Cables & Dust
  "l3-tool-cart": {
    ProceduralKind: "studio.garage_props",
    PropVariant: "tool-cart",
    Position: [5.25, 0.0, 3.15],
    Rotation: [0, -28, 0],
    YawDeg: -28,
  },
  "l3-tire-rack": {
    ProceduralKind: "studio.garage_props",
    PropVariant: "tire-rack",
    Position: [4.15, 0.0, -4.65],
    Rotation: [0, 0, 0],
    YawDeg: 0,
  },
  "l3-diag-cabinet": {
    ProceduralKind: "studio.garage_props",
    PropVariant: "diag-cabinet",
    Position: [-2.35, 0.0, -5.05],
    Rotation: [0, 14, 0],
    YawDeg: 14,
  },
  "l3-garage-cables": {
    ProceduralKind: "studio.garage_cables",
    Position: [0.0, 0.0, 0.0],
    Rotation: [0, 0, 0],
    YawDeg: 0,
    CableSpan: 11.5,
  },
  "l3-dust-particles": {
    ProceduralKind: "studio.dust_field",
    Position: [-0.8, 2.0, -0.4],
    Rotation: [0, 0, 0],
    YawDeg: 0,
    EmitterRadius: 0.14,
    ParticleCount: 165,
    DriftSpeed: 0.36,
    GlowIntensity: 0.92,
    MoteSize: 4.0,
  },

  // Garage Lighting Rigs (Overhead Linear LEDs + Golden Stadium Portal Sunlight + Worklamps)
  "rig-softbox-fixture": {
    ProceduralKind: "studio.light_fixture",
    FixtureStyle: "Overhead Softbox",
    Position: [0.0, 5.18, 0.25],
    Rotation: [0, 0, 0],
    YawDeg: 0,
    ApertureWidth: 7.4,
    ApertureHeight: 3.2,
    HousingDepth: 0.22,
    SuspensionDrop: 1.85,
    BarnDoorSpread: 35,
    YokeTiltDeg: 0,
  },
  "rig-key-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [-4.2, 0.0, -3.65],
    Rotation: [0, L3AimKey.YawDeg, 0],
    YawDeg: L3AimKey.YawDeg,
    StandHeight: 2.7,
    ColumnLift: 0.37,
    LegSpread: 0.82,
    TubeRadius: 0.022,
    Casters: true,
    FloorMat: false,
    HeadTiltDeg: L3AimKey.PitchDeg,
  },
  "rig-key-fixture": {
    ProceduralKind: "studio.light_fixture",
    FixtureStyle: "Barn-Door Fresnel",
    Position: [0, 0, 0],
    ApertureWidth: 0.48,
    ApertureHeight: 0.48,
    HousingDepth: 0.44,
    BarnDoorSpread: 48,
    YokeTiltDeg: L3AimKey.PitchDeg,
  },
  "light-key-ies": {
    ...JSON.parse(JSON.stringify(StudioLobbyInitialValues["light-key-ies"])),
    ReferenceInspector: {
      Preset: "IES Downlight",
      Properties: {
        ...StudioLobbyInitialValues["light-key-ies"].ReferenceInspector.Properties,
        lumens: 18500,
        temperature: 4450,
        cone: 62,
        target: [...Lobby3HeroAimTarget],
      },
    },
  },
  "rig-umbrella-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [4.85, 0.0, 2.65],
    Rotation: [0, L3AimUmbrella.YawDeg, 0],
    YawDeg: L3AimUmbrella.YawDeg,
    StandHeight: 1.92,
    ColumnLift: 0.25,
    LegSpread: 0.72,
    TubeRadius: 0.02,
    Casters: true,
    FloorMat: false,
    HeadTiltDeg: L3AimUmbrella.PitchDeg,
  },
  "rig-umbrella-fixture": {
    ProceduralKind: "studio.light_fixture",
    FixtureStyle: "Parabolic Umbrella",
    Position: [0, 0, 0],
    ApertureWidth: 1.45,
    ApertureHeight: 1.45,
    HousingDepth: 0.5,
    BarnDoorSpread: 35,
    YokeTiltDeg: L3AimUmbrella.PitchDeg,
  },
  "rig-rim-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [-4.6, 0.0, 3.25],
    Rotation: [0, L3AimRim.YawDeg, 0],
    YawDeg: L3AimRim.YawDeg,
    StandHeight: 2.18,
    ColumnLift: 0.29,
    LegSpread: 0.75,
    TubeRadius: 0.021,
    Casters: true,
    FloorMat: false,
    HeadTiltDeg: L3AimRim.PitchDeg,
  },
  "rig-rim-fixture": {
    ProceduralKind: "studio.light_fixture",
    FixtureStyle: "Studio LED Panel",
    Position: [0, 0, 0],
    ApertureWidth: 1.1,
    ApertureHeight: 0.58,
    HousingDepth: 0.14,
    BarnDoorSpread: 30,
    YokeTiltDeg: L3AimRim.PitchDeg,
  },
  "rig-accent-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [4.95, 0.0, -2.55],
    Rotation: [0, L3AimAccent.YawDeg, 0],
    YawDeg: L3AimAccent.YawDeg,
    StandHeight: 2.12,
    ColumnLift: 0.28,
    LegSpread: 0.74,
    TubeRadius: 0.021,
    Casters: true,
    FloorMat: false,
    HeadTiltDeg: L3AimAccent.PitchDeg,
  },
  "rig-accent-fixture": {
    ProceduralKind: "studio.light_fixture",
    FixtureStyle: "Barn-Door Fresnel",
    Position: [0, 0, 0],
    ApertureWidth: 0.42,
    ApertureHeight: 0.42,
    HousingDepth: 0.4,
    BarnDoorSpread: 44,
    YokeTiltDeg: L3AimAccent.PitchDeg,
  },
  "light-accent-ies": {
    ...JSON.parse(JSON.stringify(StudioLobbyInitialValues["light-accent-ies"])),
    ReferenceInspector: {
      Preset: "IES Downlight",
      Properties: {
        ...StudioLobbyInitialValues["light-accent-ies"].ReferenceInspector.Properties,
        lumens: 12800,
        temperature: 3350,
        cone: 54,
        target: [...Lobby3HeroAimTarget],
      },
    },
  },

  // Foreground Camera Rigs aimed at Hero GT in Pit Bay Center
  "rig-cam-a-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [-2.85, 0.0, 6.45],
    Rotation: [0, L3AimCamA.YawDeg, 0],
    YawDeg: L3AimCamA.YawDeg,
    StandHeight: 1.48,
    ColumnLift: 0.24,
    LegSpread: 0.72,
    TubeRadius: 0.024,
    Casters: false,
    FloorMat: true,
    HeadTiltDeg: L3AimCamA.PitchDeg,
  },
  "rig-cam-a-body": {
    ProceduralKind: "studio.cinema_camera",
    CameraStyle: "Cinema Matte Box",
    Position: [0, 0, 0],
    BodyLength: 0.44,
    LensLength: 0.32,
    MatteBoxWidth: 0.28,
    PitchDeg: L3AimCamA.PitchDeg,
  },
  "rig-cam-b-tripod": {
    ProceduralKind: "studio.tripod",
    Position: [3.85, 0.0, 6.25],
    Rotation: [0, L3AimCamB.YawDeg, 0],
    YawDeg: L3AimCamB.YawDeg,
    StandHeight: 1.34,
    ColumnLift: 0.21,
    LegSpread: 0.66,
    TubeRadius: 0.021,
    Casters: false,
    FloorMat: true,
    HeadTiltDeg: L3AimCamB.PitchDeg,
  },
  "rig-cam-b-body": {
    ProceduralKind: "studio.cinema_camera",
    CameraStyle: "Medium Format Tether",
    Position: [0, 0, 0],
    BodyLength: 0.32,
    LensLength: 0.22,
    MatteBoxWidth: 0.2,
    PitchDeg: L3AimCamB.PitchDeg,
  },

  "studio-ambient": {
    ProceduralKind: "studio.ambient",
    Exposure: 1.18,
    AmbientFill: 0.16,
    ShadowDepth: 0.76,
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

// Compute effective world transform { Position, YawRad, PitchRad, Forward, Visible } for every row
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

  function ReadYawDeg(Val) {
    if (Array.isArray(Val.Rotation) && Number.isFinite(Val.Rotation[1])) {
      return Number(Val.Rotation[1]);
    }
    return Number(Val.YawDeg) || 0;
  }

  function ReadPitchDeg(Val) {
    if (Val.YokeTiltDeg !== undefined) return Number(Val.YokeTiltDeg) || 0;
    if (Val.PitchDeg !== undefined) return Number(Val.PitchDeg) || 0;
    if (Val.HeadTiltDeg !== undefined) return Number(Val.HeadTiltDeg) || 0;
    if (Array.isArray(Val.Rotation) && Number.isFinite(Val.Rotation[0])) {
      return Number(Val.Rotation[0]);
    }
    return 0;
  }

  function Resolve(Id) {
    if (Id in Cache) return Cache[Id];
    const Row = ById[Id];
    const Val = Values[Id] || {};
    const LocalPos = Val.Position || Val.LocalOffset || [0, 0, 0];
    const LocalYaw = (ReadYawDeg(Val) * Math.PI) / 180;
    const LocalPitch = (ReadPitchDeg(Val) * Math.PI) / 180;
    const LocalIncline = ((Val.InclineDeg !== undefined ? Number(Val.InclineDeg) : Number(Val.Rotation?.[0] || 0)) * Math.PI) / 180;
    const LocalBank = ((Val.BankDeg !== undefined ? Number(Val.BankDeg) : Number(Val.Rotation?.[2] || 0)) * Math.PI) / 180;

    if (!Row || !Row.Parent || !ById[Row.Parent] || ById[Row.Parent].Panel === "group") {
      Cache[Id] = {
        Position: [LocalPos[0], LocalPos[1], LocalPos[2]],
        YawRad: LocalYaw,
        PitchRad: LocalPitch,
        InclineRad: LocalIncline,
        BankRad: LocalBank,
        Forward: ComputeForwardVector(LocalYaw, LocalPitch),
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
    } else if (ParentVal.ProceduralKind === "studio.car_lift") {
      MountY = Math.max(0.15, Math.min(2.6, Number(ParentVal.LiftHeight ?? 2.12)));
    }

    const CosY = Math.cos(ParentXf.YawRad);
    const SinY = Math.sin(ParentXf.YawRad);
    // Matches ModelTransformMat: worldX = x*cos + z*sin, worldZ = -x*sin + z*cos
    const Rx = LocalPos[0] * CosY + LocalPos[2] * SinY;
    const Rz = -LocalPos[0] * SinY + LocalPos[2] * CosY;

    const WorldYaw = ParentXf.YawRad + LocalYaw;
    // Fixture or camera inherits parent tripod head tilt unless overridden by its own YokeTiltDeg/PitchDeg
    let WorldPitch = LocalPitch;
    if (Row.Panel === "light" || Row.Panel === "camera") {
      WorldPitch = ParentXf.PitchRad;
    } else if (ParentRow.Panel === "studio-procedural" && Val.YokeTiltDeg === undefined && Val.PitchDeg === undefined) {
      WorldPitch = ParentXf.PitchRad;
    }

    Cache[Id] = {
      Position: [
        ParentXf.Position[0] + Rx,
        ParentXf.Position[1] + MountY + LocalPos[1],
        ParentXf.Position[2] + Rz,
      ],
      YawRad: WorldYaw,
      PitchRad: WorldPitch,
      Forward: ComputeForwardVector(WorldYaw, WorldPitch),
      Visible: IsVisible(Id),
    };
    return Cache[Id];
  }

  for (const Row of Rows) Resolve(Row.Id);
  return Cache;
}
