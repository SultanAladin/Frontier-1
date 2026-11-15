//==========================================================================================================================================
//                                                    SHOWCASEGALLERYSCENE.JS
//==========================================================================================================================================
// 1:1 HTML mirror of Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.{h,cpp}
// Defines the canonical Architectural Cornell-Studio Gallery scene, EditorCamera, OpenPBR materials, and live Outliner/Inspector compiler.

import { ResolveMaterial } from "./MaterialSpecification.js";
import {
  BuildDefaultDiagnosticForceRig,
  BuildForceOverlayGeometry,
  DEFAULT_FORCE_OVERLAY,
  FORCE_MODELS,
  GetForceModelMeta,
  PickForceBeacon,
  ResolveForceValues,
} from "./ForceFieldCatalog.js";
import {
  EulerDegToMat3,
  ExpandPropPrimitives,
  GetWorldPropById,
  HasActiveFaceColors,
  IsFanProp,
  TransformVec3ByMat3,
} from "./WorldPropLibrary.js";

export const ConstructForceTemplates = FORCE_MODELS.map((Law) => ({
  Id: `template-force-${Law.Id.toLowerCase()}`,
  Name: Law.Name,
  Panel: "force",
  ForceModel: Law.Id,
  Icon: `force-model:${Law.Id}`,
  Parent: "forces",
  Preview: true,
  Description: `${Law.Domain} · ${Law.Summary}`,
}));

function LinearChannelToSrgbByte(Value) {
  const X = Math.max(0, Math.min(1, Number(Value) || 0));
  const S = X <= 0.0031308 ? 12.92 * X : 1.055 * Math.pow(X, 1 / 2.4) - 0.055;
  return Math.max(0, Math.min(255, Math.round(S * 255)));
}

export function LinearRgbToHex([R, G, B]) {
  return (
    "#" +
    [R, G, B]
      .map((Channel) =>
        LinearChannelToSrgbByte(Channel).toString(16).padStart(2, "0"),
      )
      .join("")
  );
}

export function HexToLinearRgb(Hex, Fallback = [0.78, 0.77, 0.75]) {
  if (typeof Hex !== "string" || !/^#[0-9a-f]{6}$/i.test(Hex)) return Fallback;
  const Bytes = [1, 3, 5].map((Idx) => parseInt(Hex.slice(Idx, Idx + 2), 16) / 255);
  return Bytes.map((S) =>
    S <= 0.04045 ? S / 12.92 : Math.pow((S + 0.055) / 1.055, 2.4),
  );
}

export function NormalizeVec3(V, Fallback = [0, 0, 1]) {
  const X = Number(V?.[0]) || 0;
  const Y = Number(V?.[1]) || 0;
  const Z = Number(V?.[2]) || 0;
  const Len = Math.hypot(X, Y, Z);
  return Len > 1e-6 ? [X / Len, Y / Len, Z / Len] : Fallback;
}

export const CanonicalEditorCamera = {
  Position: [0.0, -3.55, 0.42],
  Target: [0.0, 0.55, 0.15],
  WorldUp: [0.0, 0.0, 1.0],
  VerticalFovDeg: 43.0,
  NearClipMeters: 0.15,
  FarClipMeters: 60.0,
  ExposureGain: 1.35,
  FocalLengthMm: 25.7,
  SensorWidthMm: 36.0,
  ApertureFStop: 4.0,
  SubjectDistanceM: 4.11,
};

export function BuildMaterialRecord({
  Type = "Standard",
  BaseLinear = [0.78, 0.77, 0.75],
  Metallic = 0.0,
  Roughness = 0.5,
  DiffuseRoughness = 0.5,
  Anisotropy = 0.0,
  Coat = 0.0,
  CoatRoughness = 0.08,
  Transmission = 0.0,
  Ior = 1.5,
  Emission = 0.0,
  EdgeTintF82 = [1.0, 1.0, 1.0],
  AbbeNumber = 0.0,
  AttenuationLinear = [1.0, 1.0, 1.0],
}) {
  const Hex = LinearRgbToHex(BaseLinear);
  return {
    Material: {
      Type,
      Channels: {
        colour: { Source: "Fill", Fill: Hex },
        metallic: { Source: "Fill", Fill: Metallic },
        roughness: { Source: "Fill", Fill: Roughness },
        reflectance: { Source: "Fill", Fill: Ior },
        anisotropy: { Source: "Fill", Fill: Anisotropy },
        coat: { Source: "Fill", Fill: Coat },
        coatRoughness: { Source: "Fill", Fill: CoatRoughness },
        transmission: { Source: "Fill", Fill: Transmission },
        ior: { Source: "Fill", Fill: Ior },
        emission: { Source: "Fill", Fill: Emission },
      },
    },
    DiffuseRoughness,
    EdgeTintF82,
    AbbeDispersionNumber: AbbeNumber,
    AttenuationLinear,
  };
}

// Exact 17-entry Outliner hierarchy registered in C++ ShowcaseGalleryScene::RegisterShowcaseGalleryInOutliner
export const ShowcaseInitialRows = [
  {
    Id: "showcase",
    Name: "Showcase",
    Panel: "group",
    Icon: "folder-scene",
    Parent: null,
    Description: "Cornell-Studio Gallery · 10 specimens",
  },
  {
    Id: "gallery-floor",
    Name: "Gallery Floor",
    Panel: "geometry",
    Icon: "editor-cube",
    Parent: "showcase",
    Preview: true,
    PrimitiveShape: "box",
    Description: "Box · 5.60 × 4.80 × 0.16 m · GalleryNeutralPlaster",
  },
  {
    Id: "gallery-back-wall",
    Name: "Gallery Back Wall",
    Panel: "geometry",
    Icon: "editor-cube",
    Parent: "showcase",
    Preview: true,
    PrimitiveShape: "box",
    Description: "Box · 5.60 × 0.16 × 2.90 m · GalleryNeutralPlaster",
  },
  {
    Id: "crimson-left-wall",
    Name: "Crimson Left Wall",
    Panel: "geometry",
    Icon: "editor-cube",
    Parent: "showcase",
    Preview: true,
    PrimitiveShape: "box",
    Description: "Box · 0.16 × 4.40 × 2.90 m · GalleryCrimsonWall",
  },
  {
    Id: "emerald-right-wall",
    Name: "Emerald Right Wall",
    Panel: "geometry",
    Icon: "editor-cube",
    Parent: "showcase",
    Preview: true,
    PrimitiveShape: "box",
    Description: "Box · 0.16 × 4.40 × 2.90 m · GalleryEmeraldWall",
  },
  {
    Id: "clearcoat-roof-baffle",
    Name: "Clearcoat Roof Baffle",
    Panel: "geometry",
    Icon: "editor-cube",
    Parent: "showcase",
    Preview: true,
    PrimitiveShape: "box",
    Description: "Box · 2.70 × 2.30 × 0.14 m · ClearcoatCobaltArch",
  },
  {
    Id: "gold-pedestal",
    Name: "Anisotropic Gold Pedestal",
    Panel: "geometry",
    Icon: "editor-cube",
    Parent: "showcase",
    Preview: true,
    PrimitiveShape: "box",
    Description: "Box · 0.92 × 0.92 × 0.48 m · AnisotropicGoldPedestal",
  },
  {
    Id: "chrome-sphere",
    Name: "Mirror Chrome Sphere",
    Panel: "geometry",
    Icon: "editor-sphere",
    Parent: "showcase",
    Preview: true,
    PrimitiveShape: "sphere",
    Description: "Sphere · r = 0.52 m · PolishedMirrorChrome",
  },
  {
    Id: "plaster-west",
    Name: "Plaster Specimen West",
    Panel: "geometry",
    Icon: "editor-sphere",
    Parent: "showcase",
    Preview: true,
    PrimitiveShape: "sphere",
    Description: "Sphere · r = 0.44 m · GalleryNeutralPlaster",
  },
  {
    Id: "plaster-east",
    Name: "Plaster Specimen East",
    Panel: "geometry",
    Icon: "editor-sphere",
    Parent: "showcase",
    Preview: true,
    PrimitiveShape: "sphere",
    Description: "Sphere · r = 0.44 m · GalleryNeutralPlaster",
  },
  {
    Id: "abbe-glass-orb",
    Name: "Abbe Crown Glass Orb",
    Panel: "geometry",
    Icon: "editor-sphere",
    Parent: "showcase",
    Preview: true,
    PrimitiveShape: "sphere",
    Description: "Sphere · r = 0.40 m · AbbeCrownGlassOrb (IOR 1.52, Abbe 26)",
  },
  {
    Id: "cameras",
    Name: "Cameras",
    Panel: "group",
    Icon: "camera",
    Parent: null,
    Description: "Editor and scene cameras",
  },
  {
    Id: "camera",
    Name: "Editor Camera",
    Panel: "camera",
    Icon: "camera",
    Parent: "cameras",
    Description: "Permanent editor navigation camera · 43.0° vertical FOV",
  },
  {
    Id: "world",
    Name: "World",
    Panel: "group",
    Icon: "folder-world",
    Parent: null,
    Description: "Solar emitter & environment (next phase: Atmosphere)",
  },
  {
    Id: "sun",
    Name: "Sun",
    Panel: "sun",
    Icon: "sun",
    Parent: "world",
    Preview: true,
    Description: "Directional · 5.40, 5.10, 4.65 W/m² · 3.91 mm Contact-Clip",
  },
  {
    Id: "lighting",
    Name: "Lighting",
    Panel: "group",
    Icon: "folder-generic",
    Parent: null,
    Description: "Analytic LTC polygonal area lights",
  },
  {
    Id: "ceiling-softbox",
    Name: "Ceiling Softbox",
    Panel: "light",
    Icon: "editor-area-light",
    Parent: "lighting",
    Preview: true,
    Description: "LTC Quad · 1.70 × 1.30 m · 7.5, 6.6, 5.4 nit",
  },
];

const PlasterMat = () =>
  BuildMaterialRecord({
    Type: "Standard",
    BaseLinear: [0.78, 0.77, 0.75],
    Roughness: 0.65,
    DiffuseRoughness: 0.55,
  });

export const ShowcaseInitialValues = {
  showcase: {
    Notes: "Canonical C++ <-> HTML mirror geometry collection.",
  },
  "gallery-floor": {
    Position: [0.0, 0.2, -0.72],
    Rotation: [0, 0, 0],
    Scale: [5.6, 4.8, 0.16],
    "Cast shadows": true,
    GI: true,
    Notes:
      "Architectural floor slab receiving 3.91 mm contact-clip shadows, LTC softbox illumination, and Crimson/Emerald bounce.",
    ...PlasterMat(),
  },
  "gallery-back-wall": {
    Position: [0.0, 2.35, 0.7],
    Rotation: [0, 0, 0],
    Scale: [5.6, 0.16, 2.9],
    "Cast shadows": true,
    GI: true,
    Notes: "North neutral plaster backdrop closing the Cornell-Studio chamber.",
    ...PlasterMat(),
  },
  "crimson-left-wall": {
    Position: [-2.15, 0.25, 0.7],
    Rotation: [0, 0, 0],
    Scale: [0.16, 4.4, 2.9],
    "Cast shadows": true,
    GI: true,
    Notes: "West sunlit Crimson wall driving warm diffuse color bleed into the gallery.",
    ...BuildMaterialRecord({
      Type: "Standard",
      BaseLinear: [0.86, 0.08, 0.07],
      Roughness: 0.55,
      DiffuseRoughness: 0.5,
    }),
  },
  "emerald-right-wall": {
    Position: [2.15, 0.25, 0.7],
    Rotation: [0, 0, 0],
    Scale: [0.16, 4.4, 2.9],
    "Cast shadows": true,
    GI: true,
    Notes:
      "East Emerald wall reflecting saturated green indirect radiance onto the eastern specimen.",
    ...BuildMaterialRecord({
      Type: "Standard",
      BaseLinear: [0.09, 0.8, 0.17],
      Roughness: 0.55,
      DiffuseRoughness: 0.5,
    }),
  },
  "clearcoat-roof-baffle": {
    Position: [-0.45, 0.55, 1.92],
    Rotation: [0, 0, 0],
    Scale: [2.7, 2.3, 0.14],
    "Cast shadows": true,
    GI: true,
    Notes:
      "Overhead architectural baffle casting calibrated solar penumbra across the gallery floor.",
    ...BuildMaterialRecord({
      Type: "Clear coat",
      BaseLinear: [0.1, 0.25, 0.78],
      Roughness: 0.28,
      Coat: 1.0,
      CoatRoughness: 0.04,
    }),
  },
  "gold-pedestal": {
    Position: [-0.15, 0.55, -0.4],
    Rotation: [0, 0, 0],
    Scale: [0.92, 0.92, 0.48],
    "Cast shadows": true,
    GI: true,
    Notes:
      "Anisotropic F82 conductor pedestal supporting the central mirror chrome sphere.",
    ...BuildMaterialRecord({
      Type: "Metal",
      BaseLinear: [0.96, 0.76, 0.32],
      Metallic: 1.0,
      Roughness: 0.18,
      Anisotropy: 0.55,
      EdgeTintF82: [1.0, 0.9, 0.6],
    }),
  },
  "chrome-sphere": {
    Position: [-0.15, 0.55, 0.34],
    Rotation: [0, 0, 0],
    Scale: [1.04, 1.04, 1.04],
    "Cast shadows": true,
    GI: true,
    Notes:
      "Central hero conductor sphere showcasing Tier 1 GTX Hi-Z screen-space / scene specular reflections.",
    ...BuildMaterialRecord({
      Type: "SSR",
      BaseLinear: [0.94, 0.95, 0.97],
      Metallic: 1.0,
      Roughness: 0.035,
      EdgeTintF82: [0.98, 0.98, 0.99],
    }),
  },
  "plaster-west": {
    Position: [-1.25, 0.65, -0.2],
    Rotation: [0, 0, 0],
    Scale: [0.88, 0.88, 0.88],
    "Cast shadows": true,
    GI: true,
    Notes:
      "Western Oren-Nayar diffuse sphere capturing Crimson left-wall multi-bounce bleed.",
    ...PlasterMat(),
  },
  "plaster-east": {
    Position: [1.25, 0.65, -0.2],
    Rotation: [0, 0, 0],
    Scale: [0.88, 0.88, 0.88],
    "Cast shadows": true,
    GI: true,
    Notes:
      "Eastern Oren-Nayar diffuse sphere capturing Emerald right-wall multi-bounce bleed.",
    ...PlasterMat(),
  },
  "abbe-glass-orb": {
    Position: [0.62, -0.55, -0.16],
    Rotation: [0, 0, 0],
    Scale: [0.8, 0.8, 0.8],
    "Cast shadows": false,
    GI: false,
    Notes:
      "Foreground 2-layer dispersive dielectric orb (IOR 1.52, Abbe 26) refracting the gallery interior.",
    ...BuildMaterialRecord({
      Type: "Glass",
      BaseLinear: [0.95, 0.98, 1.0],
      Roughness: 0.02,
      Transmission: 1.0,
      Ior: 1.52,
      AbbeNumber: 26.0,
      AttenuationLinear: [0.95, 0.98, 1.0],
    }),
  },
  camera: {
    Position: [...CanonicalEditorCamera.Position],
    Target: [...CanonicalEditorCamera.Target],
    "Focal Length": CanonicalEditorCamera.FocalLengthMm,
    "Sensor Width": CanonicalEditorCamera.SensorWidthMm,
    Aperture: CanonicalEditorCamera.ApertureFStop,
    "Subject Distance": CanonicalEditorCamera.SubjectDistanceM,
    Notes:
      "Synchronized 1:1 with C++ ShowcaseWorldSceneBundle::EditorCamera · Origin (0.00, -3.55, 0.42) -> Target (0.00, 0.55, 0.15).",
  },
  sun: {
    Direction: [0.42, -0.55, 0.72],
    SolarIrradiance: [5.4, 5.1, 4.65],
    SkyRadiance: [0.16, 0.23, 0.36],
    Intensity: 5.4,
    "Angular Diameter": 2.06,
    Temperature: 5650,
    "Local Hours": 13.4,
    Notes:
      "Calibrated Tier 1 GTX Solar Directional Emitter (3.91 mm/tx Contact-Clip + SDF Cone Penumbra, zero VSM).",
  },
  "ceiling-softbox": {
    Position: [0.55, 0.35, 1.82],
    Rotation: [0, 0, 0],
    Scale: [1.7, 1.3, 1.0],
    Intensity: 7.5,
    EmittedRadiance: [7.5, 6.6, 5.4],
    "Cast shadows": false,
    GI: true,
    ReferenceInspector: {
      Type: "LuminousSurface",
      Properties: {
        lumens: 2400,
        dimmer: 1,
        color: "#fff3e2",
        temperature: 5400,
        useTemp: false,
        width: 1.7,
        height: 1.3,
        twoSided: false,
        spread: 165,
        pos: [0.55, 0.35, 1.82],
        rot: [0, 0, 0],
        castShadows: true,
        shadowSoftness: 1,
        affectSpecular: true,
      },
    },
    Notes:
      "Overhead clipped-horizon LTC polygonal area quad light (1.70 × 1.30 m, downward -Z normal).",
  },
};

export function RowEffectivelyVisible(Row, Rows, Hidden) {
  const Visited = new Set();
  let Cursor = Row;
  while (Cursor) {
    if (Hidden?.[Cursor.Id] || Visited.has(Cursor.Id)) return false;
    Visited.add(Cursor.Id);
    Cursor = Rows.find((Item) => Item.Id === Cursor.Parent);
  }
  return true;
}

function ShapeKindCode(Row) {
  const Explicit = Row.PrimitiveShape;
  if (Explicit === "sphere" || Row.Icon === "editor-sphere") return 1;
  if (Explicit === "cylinder-cone") return 5;
  if (Explicit === "cylinder" || Row.Icon === "editor-cylinder") return 2;
  if (Explicit === "cone" || Row.Icon === "editor-cone") return 3;
  if (Explicit === "torus" || Row.Icon === "editor-torus") return 4;
  return 0; // box
}

export function CompileLiveGtxScene(
  Rows,
  Values,
  Hidden,
  Projection = "PERSP",
  ExtraOptions = {},
) {
  const CamRec = Values?.camera || {};
  const RawPos = Array.isArray(CamRec.Position)
    ? CamRec.Position
    : CanonicalEditorCamera.Position;
  const RawTarget = Array.isArray(CamRec.Target)
    ? CamRec.Target
    : CanonicalEditorCamera.Target;
  const Focal = Math.max(8, Number(CamRec["Focal Length"]) || CanonicalEditorCamera.FocalLengthMm);
  const SensorW = Math.max(10, Number(CamRec["Sensor Width"]) || CanonicalEditorCamera.SensorWidthMm);
  const SensorH = SensorW / (16 / 9);
  const VerticalFovRad = 2 * Math.atan(SensorH / (2 * Focal));

  let CameraOrigin = [
    Number(RawPos[0]) || 0,
    Number(RawPos[1]) || 0,
    Number(RawPos[2]) || 0,
  ];
  let CameraTarget = [
    Number(RawTarget[0]) || 0,
    Number(RawTarget[1]) || 0,
    Number(RawTarget[2]) || 0,
  ];
  let WorldUp = [0, 0, 1];
  let OrthoHalfHeight = 0; // 0 => perspective

  if (Projection === "ORTHO") {
    OrthoHalfHeight = 2.05;
  } else if (Projection === "TOP") {
    CameraOrigin = [0.0, 0.45, 5.2];
    CameraTarget = [0.0, 0.45, 0.0];
    WorldUp = [0, 1, 0];
    OrthoHalfHeight = 2.65;
  } else if (Projection === "FRONT") {
    CameraOrigin = [0.0, -4.6, 0.52];
    CameraTarget = [0.0, 0.5, 0.52];
    WorldUp = [0, 0, 1];
    OrthoHalfHeight = 1.95;
  } else if (Projection === "RIGHT") {
    CameraOrigin = [4.8, 0.45, 0.52];
    CameraTarget = [0.0, 0.45, 0.52];
    WorldUp = [0, 0, 1];
    OrthoHalfHeight = 1.95;
  }

  // Sun emitter (supports both Visibility Eye and Outliner Power Toggle)
  const SunRow = Rows.find((Row) => Row.Panel === "sun");
  const SunRec = (SunRow && Values?.[SunRow.Id]) || Values?.sun || {};
  const SunVisible =
    (SunRow ? RowEffectivelyVisible(SunRow, Rows, Hidden) : true) &&
    SunRec.Powered !== false;
  const SunDir = NormalizeVec3(
    Array.isArray(SunRec.Direction) ? SunRec.Direction : [0.42, -0.55, 0.72],
    [0.42, -0.55, 0.72],
  );
  const SunIntensity = Math.max(0, Number(SunRec.Intensity ?? 5.4));
  const GainScale = SunIntensity / 5.4;
  const BaseSunIrr = Array.isArray(SunRec.SolarIrradiance)
    ? SunRec.SolarIrradiance
    : [5.4, 5.1, 4.65];
  const SunIrradiance = SunVisible
    ? BaseSunIrr.map((C) => C * GainScale)
    : [0, 0, 0];
  const AngularDiamDeg = Math.max(0.1, Number(SunRec["Angular Diameter"] ?? 2.06));
  const SunAngularRadiusRad = Math.max(0.002, (AngularDiamDeg * 0.5 * Math.PI) / 180);
  const SkyRadiance = Array.isArray(SunRec.SkyRadiance)
    ? SunRec.SkyRadiance
    : [0.16, 0.23, 0.36];

  // Post-process EV
  const PostRow = Rows.find((Row) => Row.Panel === "post");
  const PostEnabled = PostRow ? RowEffectivelyVisible(PostRow, Rows, Hidden) : true;
  const EvComp = PostEnabled
    ? Number(Values?.[PostRow?.Id]?.["EV Compensation"] ?? 0)
    : 0;
  const ExposureGain = CanonicalEditorCamera.ExposureGain * Math.pow(2, EvComp);

  // LTC Area Quad Lights (up to 12, including directional Studio Light rigs!)
  const AreaLights = [];
  const LightPickMap = new Map();
  let NextLightSlot = 0;
  for (const Row of Rows) {
    if (Row.Panel !== "light") continue;
    if (!RowEffectivelyVisible(Row, Rows, Hidden)) continue;
    const Rec = Values?.[Row.Id] || {};
    const PropDef = GetWorldPropById(Row.PropId || Rec.PropId);
    const PickCode = 400 + NextLightSlot++;
    LightPickMap.set(Row.Id, PickCode);

    // When toggled OFF in Outliner, keep the 3D fixture visible (in Primitives below) while turning emission OFF!
    if (Rec.Powered === false) continue;

    const RefProps = Rec.ReferenceInspector?.Properties || {};
    const RawPos = Array.isArray(Rec.Position)
      ? Rec.Position
      : Array.isArray(RefProps.pos)
        ? RefProps.pos
        : [0.55, 0.35, 1.82];
    const RotDeg = Array.isArray(Rec.Rotation)
      ? Rec.Rotation
      : Array.isArray(RefProps.rot)
        ? RefProps.rot
        : [0, 0, 0];
    const MRot = EulerDegToMat3(RotDeg);
    let EmitCenter = [Number(RawPos[0]) || 0, Number(RawPos[1]) || 0, Number(RawPos[2]) || 0];
    if (PropDef?.LightEmitter?.Offset) {
      const OffW = TransformVec3ByMat3(MRot, PropDef.LightEmitter.Offset);
      EmitCenter = [EmitCenter[0] + OffW[0], EmitCenter[1] + OffW[1], EmitCenter[2] + OffW[2]];
    }

    // Build orthonormal 3D light basis (Right, Up, Normal) so directional Studio Lights shine where aimed!
    const LocalAim = PropDef?.LightEmitter?.AimDir || [0, 0, -1];
    const WorldNormal = NormalizeVec3(TransformVec3ByMat3(MRot, LocalAim), [0, 0, -1]);
    let WorldRight = NormalizeVec3(TransformVec3ByMat3(MRot, [1, 0, 0]), [1, 0, 0]);
    const DotRN = WorldRight[0] * WorldNormal[0] + WorldRight[1] * WorldNormal[1] + WorldRight[2] * WorldNormal[2];
    WorldRight = NormalizeVec3(
      [
        WorldRight[0] - DotRN * WorldNormal[0],
        WorldRight[1] - DotRN * WorldNormal[1],
        WorldRight[2] - DotRN * WorldNormal[2],
      ],
      [1, 0, 0],
    );
    const WorldUpAxis = NormalizeVec3(
      [
        WorldNormal[1] * WorldRight[2] - WorldNormal[2] * WorldRight[1],
        WorldNormal[2] * WorldRight[0] - WorldNormal[0] * WorldRight[2],
        WorldNormal[0] * WorldRight[1] - WorldNormal[1] * WorldRight[0],
      ],
      [0, 1, 0],
    );

    const DefaultW = PropDef?.LightEmitter?.Width ?? 1.7;
    const DefaultH = PropDef?.LightEmitter?.Height ?? 1.3;
    const Width = PropDef
      ? Number(RefProps.width) || DefaultW
      : Array.isArray(Rec.Scale)
        ? Number(Rec.Scale[0]) || Number(RefProps.width) || DefaultW
        : Number(RefProps.width) || DefaultW;
    const Height = PropDef
      ? Number(RefProps.height) || DefaultH
      : Array.isArray(Rec.Scale)
        ? Number(Rec.Scale[1]) || Number(RefProps.height) || DefaultH
        : Number(RefProps.height) || DefaultH;
    const BaseLumens = PropDef?.LightEmitter?.Lumens ?? 2400;
    const LumensRatio =
      typeof RefProps.lumens === "number"
        ? (Math.max(0, RefProps.lumens) / BaseLumens) * Math.max(0, Number(RefProps.dimmer ?? 1))
        : Number(Rec.Intensity ?? 7.5) / 7.5;
    const TintRgb =
      typeof RefProps.color === "string" && /^#[0-9a-f]{6}$/i.test(RefProps.color)
        ? HexToLinearRgb(RefProps.color, [1.0, 0.88, 0.72])
        : null;
    const FallbackEmit = PropDef?.LightEmitter?.Radiance || [7.5, 6.6, 5.4];
    const BaseEmit = TintRgb
      ? [TintRgb[0] * 7.5, TintRgb[1] * 7.5, TintRgb[2] * 7.5]
      : Array.isArray(Rec.EmittedRadiance)
        ? Rec.EmittedRadiance
        : FallbackEmit;
    AreaLights.push({
      Id: Row.Id,
      PickCode,
      QuadVisible: !PropDef,
      Omni: Boolean(PropDef?.LightEmitter?.Omni),
      Center: EmitCenter,
      Right: WorldRight,
      Up: WorldUpAxis,
      Normal: WorldNormal,
      HalfWidth: Math.max(0.08, Math.abs(Width) * 0.5),
      HalfHeight: Math.max(0.08, Math.abs(Height) * 0.5),
      Radiance: BaseEmit.map((C) => Math.max(0, C * LumensRatio)),
    });
    if (AreaLights.length >= 12) break;
  }

  // Geometry, Multi-Part Props, Placed Camera Rigs, Fans & Studio Light Rigs (up to 384 primitives)
  const Primitives = [];
  let NextActorPickCode = 1;
  let HasSpinningFans = false;
  const FanSpinDeg = Number(ExtraOptions?.FanSpinDeg || 0);
  for (const Row of Rows) {
    const Rec = Values?.[Row.Id] || {};
    const PropId = Row.PropId || Rec.PropId || (Row.Panel === "camera" && Row.Id !== "camera" ? "engine_camera" : null);
    const IsRenderableProp =
      Boolean(PropId) && ["geometry", "camera", "light"].includes(Row.Panel) && Row.Id !== "camera";
    if (Row.Panel !== "geometry" && !IsRenderableProp) continue;
    if (!RowEffectivelyVisible(Row, Rows, Hidden)) continue;

    if (IsRenderableProp && GetWorldPropById(PropId)) {
      const SharedPickCode =
        Row.Panel === "light" && LightPickMap.has(Row.Id)
          ? LightPickMap.get(Row.Id)
          : NextActorPickCode++;
      const RowIsFan = IsFanProp({ ...Row, PropId }, Rec);
      if (RowIsFan && Rec.Powered !== false) {
        HasSpinningFans = true;
      }
      const RecForBuild =
        RowIsFan && Rec.Powered !== false ? { ...Rec, __FanSpinDeg: FanSpinDeg } : Rec;
      const Expanded = ExpandPropPrimitives({ ...Row, PropId }, RecForBuild, SharedPickCode);
      for (const SubPrim of Expanded) {
        if (Primitives.length >= 384) break;
        Primitives.push(SubPrim);
      }
      if (Primitives.length >= 384) break;
      continue;
    }

    if (Row.Panel !== "geometry") continue;
    const Pos = Array.isArray(Rec.Position) ? Rec.Position : [0, 0.5, 0];
    const Rot = Array.isArray(Rec.Rotation) ? Rec.Rotation : [0, 0, 0];
    const Scale = Array.isArray(Rec.Scale) ? Rec.Scale : [0.8, 0.8, 0.8];
    const Mat = ResolveMaterial(Rec);
    const Ch = Mat.Channels;

    const BaseColor = HexToLinearRgb(Ch.colour?.Fill, [0.78, 0.77, 0.75]);
    const Metallic = Math.max(0, Math.min(1, Number(Ch.metallic?.Fill ?? 0)));
    const Roughness = Math.max(0, Math.min(1, Number(Ch.roughness?.Fill ?? 0.35)));
    const ClampedRoughness = Math.max(0.02, Roughness);
    const Anisotropy = Math.max(-0.95, Math.min(0.95, Number(Ch.anisotropy?.Fill ?? 0)));
    const CoatWeight = Math.max(0, Math.min(1, Number(Ch.coat?.Fill ?? 0)));
    const CoatRoughness = Math.max(0.02, Math.min(1, Number(Ch.coatRoughness?.Fill ?? 0.08)));
    const Opacity = Math.max(0, Math.min(1, Number(Ch.opacity?.Fill ?? 1)));
    const Displacement = Math.max(-0.1, Math.min(0.1, Number(Ch.displacement?.Fill ?? 0)));
    const Transmission =
      Mat.Type === "Glass"
        ? Math.max(0.85, Number(Ch.transmission?.Fill ?? 1))
        : Math.max(0, Math.min(1, Number(Ch.transmission?.Fill ?? 0)));
    const Ior = Math.max(1.02, Math.min(2.5, Number(Ch.ior?.Fill ?? Ch.reflectance?.Fill ?? 1.5)));
    const Emission = Math.max(0, Number(Ch.emission?.Fill ?? 0));
    const DiffuseRoughness = Number(Rec.DiffuseRoughness ?? ClampedRoughness * 0.85);
    const EdgeTintF82 = Array.isArray(Rec.EdgeTintF82)
      ? Rec.EdgeTintF82
      : [1, 1, 1];
    const Abbe =
      Number(Rec.AbbeDispersionNumber) || (Transmission > 0.5 ? 26.0 : 0.0);
    const Attenuation = Array.isArray(Rec.AttenuationLinear)
      ? Rec.AttenuationLinear
      : BaseColor;

    const IsTransparentShell =
      (Transmission > 0.45 && Ch.transmission?.Source === "Fill") ||
      Mat.Type === "Glass" ||
      (Opacity < 0.92 && Ch.opacity?.Source === "Fill");
    const UseFaceAtlas = HasActiveFaceColors(Rec.FaceColors);
    const PickCode = NextActorPickCode++;

    Primitives.push({
      Id: Row.Id,
      OwnerId: Row.Id,
      PickCode,
      Name: Row.Name,
      Shape: ShapeKindCode(Row),
      Center: [Number(Pos[0]) || 0, Number(Pos[1]) || 0, Number(Pos[2]) || 0],
      RotationRad: Rot.map((Deg) => ((Number(Deg) || 0) * Math.PI) / 180),
      HalfExtents: [
        Math.max(0.015, Math.abs(Number(Scale[0]) || 0.8) * 0.5 + Displacement),
        Math.max(0.015, Math.abs(Number(Scale[1]) || 0.8) * 0.5 + Displacement),
        Math.max(0.015, Math.abs(Number(Scale[2]) || 0.8) * 0.5 + Displacement),
      ],
      CastShadows: (Rec["Cast shadows"] ?? !IsTransparentShell) ? 1 : 0,
      GiContributor: (Rec.GI ?? !IsTransparentShell) ? 1 : 0,
      GlassShell: IsTransparentShell,
      FaceAtlas: UseFaceAtlas,
      FaceColors: UseFaceAtlas ? Rec.FaceColors : null,
      ResolvedMaterial: Mat,
      MaterialType: Mat.Type,
      BaseColor,
      Metallic,
      Roughness: ClampedRoughness,
      DiffuseRoughness,
      Anisotropy,
      CoatWeight,
      CoatRoughness,
      Opacity,
      Displacement,
      Transmission,
      Ior,
      Emission,
      EdgeTintF82,
      Abbe,
      Attenuation,
    });
    if (Primitives.length >= 384) break;
  }

  // Analytical ForceFields & VectorFields (Pass 3A ForceOverlay — zero rigid-body physics!)
  const ForceNodes = [];
  for (const Row of Rows) {
    if (Row.Panel !== "force") continue;
    if (!RowEffectivelyVisible(Row, Rows, Hidden)) continue;
    const Rec = Values?.[Row.Id] || {};
    if (Rec.Powered === false) continue;
    const Pos = Array.isArray(Rec.Position) ? Rec.Position : [0, 0, 1.4];
    const Rot = Array.isArray(Rec.Rotation) ? Rec.Rotation : [0, 0, 0];
    const Force = ResolveForceValues(Rec.Force, Row.ForceModel || "LinearWind");
    ForceNodes.push({
      Id: Row.Id,
      Name: Row.Name,
      Position: [Number(Pos[0]) || 0, Number(Pos[1]) || 0, Number(Pos[2]) || 0],
      Rotation: [Number(Rot[0]) || 0, Number(Rot[1]) || 0, Number(Rot[2]) || 0],
      Force,
    });
  }

  const OverlayCfg = ExtraOptions?.ForceOverlaySettings || DEFAULT_FORCE_OVERLAY;
  const SelectedId = ExtraOptions?.SelectedId || null;
  const EffectiveForceNodes =
    ForceNodes.length > 0
      ? ForceNodes
      : OverlayCfg?.ForcePreviewRig
        ? BuildDefaultDiagnosticForceRig()
        : [];
  const ActiveCameraOrigin = Array.isArray(ExtraOptions?.CameraOrigin)
    ? ExtraOptions.CameraOrigin
    : CameraOrigin;
  const HasAnimatedForces =
    EffectiveForceNodes.length > 0 &&
    (OverlayCfg?.ShowStreamlines !== false ||
      Boolean(OverlayCfg?.ShowBokehRibbons) ||
      EffectiveForceNodes.some(
        (N) =>
          (N.Force?.MotionTopology && N.Force.MotionTopology !== "Static") ||
          (N.Force?.Duration && N.Force.Duration !== "Continuous"),
      ));
  const ForceOverlay =
    EffectiveForceNodes.length > 0 && OverlayCfg?.Enabled !== false
      ? BuildForceOverlayGeometry(
          EffectiveForceNodes,
          OverlayCfg,
          SelectedId,
          ExtraOptions?.TimeSec || 0,
          ActiveCameraOrigin,
        )
      : null;
  const ForceSignature =
    EffectiveForceNodes.length === 0
      ? ""
      : `${OverlayCfg?.Enabled !== false ? 1 : 0}:${OverlayCfg?.ShowHeatmap !== false ? 1 : 0}:${OverlayCfg?.ShowEnvelopes ? 1 : 0}:${OverlayCfg?.ShowVelocityArrows ? 1 : 0}:${OverlayCfg?.ShowStreamlines ? 1 : 0}:${OverlayCfg?.ShowBokehRibbons ? 1 : 0}:${OverlayCfg?.Quantity || "Speed"}:${OverlayCfg?.Colormap || OverlayCfg?.ColorRamp || "Engineering"}:${OverlayCfg?.SliceAxis || "XY"}:${OverlayCfg?.SliceOffset || 0}:${OverlayCfg?.FixedMax || 24}:${OverlayCfg?.ArrowDensity || 4}:${OverlayCfg?.HeatmapOpacity ?? 0.62}:${OverlayCfg?.VolumeLayers || 1}:${OverlayCfg?.StreamColorMode || "CyanSingle"}:${OverlayCfg?.StreamSingleColor || "#68dcff"}:${OverlayCfg?.StreamThickness ?? 0.78}:${OverlayCfg?.StreamEmissive ?? 1.05}:${OverlayCfg?.StreamOpacity ?? 0.92}:${OverlayCfg?.StreamResolution ?? 28}:${OverlayCfg?.StreamLineCount ?? 96}:${OverlayCfg?.BokehPalette || "BokehTeal"}:${OverlayCfg?.BokehSpeed ?? 0.55}:${OverlayCfg?.BokehCount ?? 120}:${OverlayCfg?.BokehThickness ?? 1.0}:${OverlayCfg?.BokehEmissive ?? 1.05}:${(OverlayCfg?.ProbePosition || []).join(",")}:${SelectedId || ""}|` +
        EffectiveForceNodes.map(
          (N) =>
            `${N.Id}:${N.Position.join(",")}:${N.Rotation.join(",")}:${N.Force.Model}:${N.Force.Enabled ? 1 : 0}:${N.Force.Strength}:${N.Force.Radius}:${N.Force.CoreRadius}:${N.Force.FalloffExponent}:${N.Force.Reach}:${N.Force.Attenuation}:${N.Force.Duration}:${N.Force.SwirlRatio}:${N.Force.TurbulenceScale}:${N.Force.Octaves}:${N.Force.Frequency}:${N.Force.YawDeg}:${N.Force.MotionTopology}:${N.Force.MotionLoopMode}:${N.Force.MotionSpeedHz}:${N.Force.MotionAmplitude}:${JSON.stringify(N.Force.MotionWaypoints || [])}:${N.Force.FormulaEnabled ? 1 : 0}:${N.Force.FormulaCode?.length || 0}`,
        ).join(";");

  return {
    HasSpinningFans,
    HasAnimatedForces,
    CameraOrigin,
    CameraTarget,
    WorldUp,
    VerticalFovRad,
    OrthoHalfHeight,
    ExposureGain,
    SunDir,
    SunIrradiance,
    SunAngularRadiusRad,
    SkyRadiance,
    AreaLights,
    Primitives,
    ForceNodes: EffectiveForceNodes,
    ForceOverlay,
    ForceSignature,
  };
}

function InvRotateEulerXYZ(V, R) {
  const Cx = Math.cos(R[0] || 0), Sx = Math.sin(R[0] || 0);
  const Cy = Math.cos(R[1] || 0), Sy = Math.sin(R[1] || 0);
  const Cz = Math.cos(R[2] || 0), Sz = Math.sin(R[2] || 0);
  const Z = [Cz * V[0] + Sz * V[1], -Sz * V[0] + Cz * V[1], V[2]];
  const Y = [Cy * Z[0] - Sy * Z[2], Z[1], Sy * Z[0] + Cy * Z[2]];
  return [Y[0], Cx * Y[1] + Sx * Y[2], -Sx * Y[1] + Cx * Y[2]];
}

export function ComputeCameraRayAtUv(CompiledScene, U, V, Aspect) {
  const { CameraOrigin, CameraTarget, WorldUp, VerticalFovRad, OrthoHalfHeight } =
    CompiledScene;
  const Fwd = NormalizeVec3([
    CameraTarget[0] - CameraOrigin[0],
    CameraTarget[1] - CameraOrigin[1],
    CameraTarget[2] - CameraOrigin[2],
  ]);
  const Cross = (A, B) => [
    A[1] * B[2] - A[2] * B[1],
    A[2] * B[0] - A[0] * B[2],
    A[0] * B[1] - A[1] * B[0],
  ];
  const Right = NormalizeVec3(Cross(Fwd, WorldUp), [1, 0, 0]);
  const Up = NormalizeVec3(Cross(Right, Fwd), [0, 0, 1]);

  const NdcX = 2 * U - 1;
  const NdcY = 1 - 2 * V;
  let RayOrigin, RayDir;
  if (OrthoHalfHeight > 0) {
    const HalfW = OrthoHalfHeight * Aspect;
    RayOrigin = [
      CameraOrigin[0] + Right[0] * NdcX * HalfW + Up[0] * NdcY * OrthoHalfHeight,
      CameraOrigin[1] + Right[1] * NdcX * HalfW + Up[1] * NdcY * OrthoHalfHeight,
      CameraOrigin[2] + Right[2] * NdcX * HalfW + Up[2] * NdcY * OrthoHalfHeight,
    ];
    RayDir = Fwd;
  } else {
    const TanHalf = Math.tan(VerticalFovRad * 0.5);
    RayOrigin = CameraOrigin;
    RayDir = NormalizeVec3([
      Fwd[0] + Right[0] * NdcX * Aspect * TanHalf + Up[0] * NdcY * TanHalf,
      Fwd[1] + Right[1] * NdcX * Aspect * TanHalf + Up[1] * NdcY * TanHalf,
      Fwd[2] + Right[2] * NdcX * Aspect * TanHalf + Up[2] * NdcY * TanHalf,
    ]);
  }
  return { RayOrigin, RayDir, Basis: { Right, Up, Forward: Fwd } };
}

function FwdRotateEulerXYZ(V, R) {
  const Cx = Math.cos(R[0] || 0), Sx = Math.sin(R[0] || 0);
  const Cy = Math.cos(R[1] || 0), Sy = Math.sin(R[1] || 0);
  const Cz = Math.cos(R[2] || 0), Sz = Math.sin(R[2] || 0);
  const X = [V[0], Cx * V[1] - Sx * V[2], Sx * V[1] + Cx * V[2]];
  const Y = [Cy * X[0] + Sy * X[2], X[1], -Sy * X[0] + Cy * X[2]];
  return [Cz * Y[0] - Sz * Y[1], Sz * Y[0] + Cz * Y[1], Y[2]];
}

function IntersectLocalFrustumZ(Lo, Ld, Z0, Z1, R0, R1, BestT) {
  let OutT = BestT;
  const Zc = 0.5 * (Z0 + Z1);
  const Hc = 0.5 * (Z1 - Z0);
  const Rm = 0.5 * (R0 + R1);
  const K = Hc > 1e-6 ? (R1 - R0) / (2.0 * Hc) : 0.0;
  const Oz = Lo[2] - Zc;
  const RAtOz = Rm + K * Oz;
  const A = Ld[0] * Ld[0] + Ld[1] * Ld[1] - K * K * Ld[2] * Ld[2];
  const B = Lo[0] * Ld[0] + Lo[1] * Ld[1] - K * RAtOz * Ld[2];
  const C = Lo[0] * Lo[0] + Lo[1] * Lo[1] - RAtOz * RAtOz;
  const Disc = B * B - A * C;
  if (Math.abs(A) > 1e-6 && Disc >= 0.0) {
    const Sq = Math.sqrt(Disc);
    const Ra = (-B - Sq) / A;
    const Rb = (-B + Sq) / A;
    const TMin = Math.min(Ra, Rb);
    const TMax = Math.max(Ra, Rb);
    const ZMin = Oz + Ld[2] * TMin;
    if (TMin > 0.04 && TMin < OutT && Math.abs(ZMin) <= Hc) OutT = TMin;
    const ZMax = Oz + Ld[2] * TMax;
    if (TMax > 0.04 && TMax < OutT && Math.abs(ZMax) <= Hc) OutT = TMax;
  }
  if (Math.abs(Ld[2]) > 1e-6) {
    const Tc0 = (Z0 - Lo[2]) / Ld[2];
    if (Tc0 > 0.04 && Tc0 < OutT) {
      const X0 = Lo[0] + Ld[0] * Tc0, Y0 = Lo[1] + Ld[1] * Tc0;
      if (X0 * X0 + Y0 * Y0 <= R0 * R0) OutT = Tc0;
    }
    const Tc1 = (Z1 - Lo[2]) / Ld[2];
    if (Tc1 > 0.04 && Tc1 < OutT) {
      const X1 = Lo[0] + Ld[0] * Tc1, Y1 = Lo[1] + Ld[1] * Tc1;
      if (X1 * X1 + Y1 * Y1 <= R1 * R1) OutT = Tc1;
    }
  }
  return OutT;
}

function IntersectLocalBox(Lo, Ld, BMin, BMax, BestT) {
  let TMin = 0.04;
  let TMax = BestT;
  let AxisHit = 2;
  let AxisSign = 1;
  for (let Axis = 0; Axis < 3; ++Axis) {
    const InvD = Math.abs(Ld[Axis]) > 1e-7 ? 1 / Ld[Axis] : 1e7;
    let T0 = (BMin[Axis] - Lo[Axis]) * InvD;
    let T1 = (BMax[Axis] - Lo[Axis]) * InvD;
    let Sign = -1;
    if (T0 > T1) {
      [T0, T1] = [T1, T0];
      Sign = 1;
    }
    if (T0 > TMin) {
      TMin = T0;
      AxisHit = Axis;
      AxisSign = Sign;
    }
    TMax = Math.min(TMax, T1);
    if (TMin > TMax) return { Hit: false, T: BestT, Normal: [0, 0, 1] };
  }
  const Normal = [0, 0, 0];
  Normal[AxisHit] = AxisSign;
  return { Hit: true, T: TMin, Normal };
}

function IntersectScenePrimLocal(Prim, Rel, LocalDir, CurrentBestT) {
  const Hx = Prim.HalfExtents[0];
  const Hy = Prim.HalfExtents[1];
  const Hz = Prim.HalfExtents[2];
  if (Prim.Shape === 1) {
    const O = [Rel[0] / Hx, Rel[1] / Hy, Rel[2] / Hz];
    const D = [LocalDir[0] / Hx, LocalDir[1] / Hy, LocalDir[2] / Hz];
    const A = D[0] * D[0] + D[1] * D[1] + D[2] * D[2];
    const B = O[0] * D[0] + O[1] * D[1] + O[2] * D[2];
    const C = O[0] * O[0] + O[1] * O[1] + O[2] * O[2] - 1;
    const Disc = B * B - A * C;
    if (Disc >= 0) {
      const Sq = Math.sqrt(Disc);
      const T0 = (-B - Sq) / A;
      const T1 = (-B + Sq) / A;
      const T = T0 > 0.04 ? T0 : T1 > 0.04 ? T1 : Infinity;
      if (T < CurrentBestT) {
        const Lp = [Rel[0] + LocalDir[0] * T, Rel[1] + LocalDir[1] * T, Rel[2] + LocalDir[2] * T];
        return { T, LocalNormal: NormalizeVec3([Lp[0] / (Hx * Hx), Lp[1] / (Hy * Hy), Lp[2] / (Hz * Hz)], [0, 0, 1]) };
      }
    }
    return null;
  }
  if (Prim.Shape === 2) {
    const Rad = Math.max(Hx, Hy);
    const T = IntersectLocalFrustumZ(Rel, LocalDir, -Hz, Hz, Rad, Rad, CurrentBestT);
    if (T < CurrentBestT) {
      const Lp = [Rel[0] + LocalDir[0] * T, Rel[1] + LocalDir[1] * T, Rel[2] + LocalDir[2] * T];
      const OnCap = Math.abs(Math.abs(Lp[2]) - Hz) < 0.004;
      const Ln = OnCap ? [0, 0, Lp[2] >= 0 ? 1 : -1] : NormalizeVec3([Lp[0], Lp[1], 0], [1, 0, 0]);
      return { T, LocalNormal: Ln };
    }
    return null;
  }
  if (Prim.Shape === 3) {
    // Traffic Cone: square weighted base + tapered car safety cone frustum
    const Rad = Math.max(Hx, Hy);
    const ZBase = -0.78 * Hz;
    let Best = CurrentBestT;
    let BestN = [0, 0, 1];
    const BaseHit = IntersectLocalBox(Rel, LocalDir, [-Rad, -Rad, -Hz], [Rad, Rad, ZBase], Best);
    if (BaseHit.Hit && BaseHit.T < Best) {
      Best = BaseHit.T;
      BestN = BaseHit.Normal;
    }
    const ConeT = IntersectLocalFrustumZ(Rel, LocalDir, ZBase, Hz, 0.72 * Rad, 0.13 * Rad, Best);
    if (ConeT < Best) {
      Best = ConeT;
      const Lp = [Rel[0] + LocalDir[0] * ConeT, Rel[1] + LocalDir[1] * ConeT, Rel[2] + LocalDir[2] * ConeT];
      BestN = Math.abs(Lp[2] - Hz) < 0.004
        ? [0, 0, 1]
        : NormalizeVec3([Lp[0], Lp[1], 0.33 * Math.hypot(Lp[0], Lp[1])], [0, 0, 1]);
    }
    return Best < CurrentBestT ? { T: Best, LocalNormal: BestN } : null;
  }
  if (Prim.Shape === 5) {
    // Cylinder Cone: round weighted base flange + steep cylinder-like car delineator cone post
    const Rad = Math.max(Hx, Hy);
    const ZBase = -0.84 * Hz;
    let Best = CurrentBestT;
    let BestN = [0, 0, 1];
    const BaseT = IntersectLocalFrustumZ(Rel, LocalDir, -Hz, ZBase, Rad, 0.94 * Rad, Best);
    if (BaseT < Best) {
      Best = BaseT;
      BestN = [0, 0, 1];
    }
    const PostT = IntersectLocalFrustumZ(Rel, LocalDir, ZBase, Hz, 0.50 * Rad, 0.37 * Rad, Best);
    if (PostT < Best) {
      Best = PostT;
      const Lp = [Rel[0] + LocalDir[0] * PostT, Rel[1] + LocalDir[1] * PostT, Rel[2] + LocalDir[2] * PostT];
      BestN = Math.abs(Lp[2] - Hz) < 0.004
        ? [0, 0, 1]
        : NormalizeVec3([Lp[0], Lp[1], 0.08 * Math.hypot(Lp[0], Lp[1])], [0, 0, 1]);
    }
    return Best < CurrentBestT ? { T: Best, LocalNormal: BestN } : null;
  }
  const BoxHit = IntersectLocalBox(Rel, LocalDir, [-Hx, -Hy, -Hz], [Hx, Hy, Hz], CurrentBestT);
  return BoxHit.Hit ? { T: BoxHit.T, LocalNormal: BoxHit.Normal } : null;
}

// Analytical 3D ray picking in Z-up world space so clicking any 3D object or ForceField beacon in the GTX viewport selects it in Outliner & Inspector!
export function PickSceneEntityAtUv(CompiledScene, U, V, Aspect) {
  const { RayOrigin, RayDir } = ComputeCameraRayAtUv(CompiledScene, U, V, Aspect);

  // ForceOverlay beacons render depth-cleared over scene surfaces in Pass 3A -> check beacon hit first
  if (CompiledScene.ForceOverlay) {
    const BeaconHit = PickForceBeacon(RayOrigin, RayDir, CompiledScene.ForceOverlay);
    if (BeaconHit) return BeaconHit.Id;
  }

  let BestT = Infinity;
  let BestId = null;

  for (const Prim of CompiledScene.Primitives) {
    if (Prim.Id === "__drag_preview__" || Prim.OwnerId === "__drag_preview__") continue;
    const WorldRel = [
      RayOrigin[0] - Prim.Center[0],
      RayOrigin[1] - Prim.Center[1],
      RayOrigin[2] - Prim.Center[2],
    ];
    const Rel = InvRotateEulerXYZ(WorldRel, Prim.RotationRad || [0, 0, 0]);
    const LocalDir = InvRotateEulerXYZ(RayDir, Prim.RotationRad || [0, 0, 0]);
    const Hit = IntersectScenePrimLocal(Prim, Rel, LocalDir, BestT);
    if (Hit && Hit.T < BestT) {
      BestT = Hit.T;
      BestId = Prim.OwnerId || Prim.Id;
    }
  }

  for (const Light of CompiledScene.AreaLights) {
    if (Light.QuadVisible === false) continue;
    if (Math.abs(RayDir[2]) > 1e-5) {
      const T = (Light.Center[2] - RayOrigin[2]) / RayDir[2];
      if (T > 0.05 && T < BestT) {
        const HitX = RayOrigin[0] + RayDir[0] * T - Light.Center[0];
        const HitY = RayOrigin[1] + RayDir[1] * T - Light.Center[1];
        if (
          Math.abs(HitX) <= Light.HalfWidth &&
          Math.abs(HitY) <= Light.HalfHeight
        ) {
          BestT = T;
          BestId = Light.Id;
        }
      }
    }
  }

  return BestId;
}

// Unreal Engine-style viewport surface placement raycast:
// Rays into upward-facing horizontal surfaces (floors, plinths, pedestals, cubes — ignoring ceiling baffles & vertical enclosure walls)
// and offsets Z by HalfHeightZ so Traffic Cones, Cylinder Cones, and meshes sit flush on the surface!
export function RaycastWorldPlacementAtUv(
  CompiledScene,
  U,
  V,
  Aspect = 16 / 9,
  HalfHeightZ = 0.39,
  IgnoreId = "__drag_preview__",
) {
  const { RayOrigin, RayDir } = ComputeCameraRayAtUv(CompiledScene, U, V, Aspect);
  let BestT = Infinity;
  let BestSurface = null;
  let BestHitId = null;

  for (const Prim of CompiledScene.Primitives) {
    if (Prim.Id === IgnoreId || Prim.OwnerId === IgnoreId || Prim.GlassShell) continue;
    // Skip overhead ceiling baffles so top-down or angled rays never snap actors onto the roof!
    if (Prim.Center[2] > 1.35 && Prim.HalfExtents[2] < 0.25) continue;

    const WorldRel = [
      RayOrigin[0] - Prim.Center[0],
      RayOrigin[1] - Prim.Center[1],
      RayOrigin[2] - Prim.Center[2],
    ];
    const Rot = Prim.RotationRad || [0, 0, 0];
    const Rel = InvRotateEulerXYZ(WorldRel, Rot);
    const LocalDir = InvRotateEulerXYZ(RayDir, Rot);
    const Hit = IntersectScenePrimLocal(Prim, Rel, LocalDir, BestT);
    if (!Hit) continue;
    const WorldNormal = FwdRotateEulerXYZ(Hit.LocalNormal, Rot);
    // Only snap onto upward-facing support surfaces (floors, pedestals, boxes) — ray passes through side/back walls to hit the floor in front!
    if (WorldNormal[2] >= 0.45 && Hit.T < BestT) {
      BestT = Hit.T;
      BestHitId = Prim.OwnerId || Prim.Id;
      BestSurface = [
        RayOrigin[0] + RayDir[0] * Hit.T,
        RayOrigin[1] + RayDir[1] * Hit.T,
        RayOrigin[2] + RayDir[2] * Hit.T,
      ];
    }
  }

  // Fallback to actual floor plane in the compiled scene (or -0.64 gallery default if no floor exists)
  if (!BestSurface) {
    let GroundZ = -0.64;
    for (const Prim of CompiledScene.Primitives) {
      if (Prim.Shape === 0 && Prim.HalfExtents[0] >= 2.5 && Prim.HalfExtents[1] >= 2.5 && Prim.HalfExtents[2] <= 0.4) {
        GroundZ = Prim.Center[2] + Prim.HalfExtents[2];
        break;
      }
    }
    if (RayDir[2] < -0.02) {
      const TGround = Math.min(36.0, Math.max(0.4, (GroundZ - RayOrigin[2]) / RayDir[2]));
      BestSurface = [
        RayOrigin[0] + RayDir[0] * TGround,
        RayOrigin[1] + RayDir[1] * TGround,
        GroundZ,
      ];
    } else {
      BestSurface = [
        RayOrigin[0] + RayDir[0] * 3.0,
        RayOrigin[1] + RayDir[1] * 3.0,
        GroundZ,
      ];
    }
  }

  const ClampedX = +Math.max(-40.0, Math.min(40.0, BestSurface[0])).toFixed(3);
  const ClampedY = +Math.max(-40.0, Math.min(40.0, BestSurface[1])).toFixed(3);
  const PlacedZ = +(BestSurface[2] + Math.max(0, Number(HalfHeightZ) || 0)).toFixed(3);
  return {
    Position: [ClampedX, ClampedY, PlacedZ],
    SurfacePoint: BestSurface.map((C) => +C.toFixed(3)),
    HitId: BestHitId,
  };
}
