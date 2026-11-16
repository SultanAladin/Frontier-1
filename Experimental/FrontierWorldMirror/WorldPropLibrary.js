//==========================================================================================================================================
// FILE: WorldPropLibrary.js
// PURPOSE: Procedural Multi-Part 3D Cinema Cameras, Studio Lights, Motorsport/Studio Fans, ControlVehicle Tyre Stack, and Bespoke
//          Proving-Ground Barriers & Kerbs in Frontier Z-Up meters (+X Right, +Y Forward, +Z Up), using clean flat OpenPBR materials.
//==========================================================================================================================================

import { ResolveMaterial } from "./MaterialSpecification.js";

export const FACE_KEYS = ["-X", "+X", "-Y", "+Y", "-Z", "+Z"];
export const FACE_LABELS = {
  "-Y": "Front (−Y)",
  "+Y": "Back (+Y)",
  "-X": "Left (−X)",
  "+X": "Right (+X)",
  "+Z": "Top (+Z)",
  "-Z": "Bottom (−Z)",
};

export function HasActiveFaceColors(FaceColors) {
  if (!FaceColors || typeof FaceColors !== "object") return false;
  if (FaceColors.Enabled === false) return false;
  for (const Key of FACE_KEYS) {
    if (FaceColors[Key] !== undefined && FaceColors[Key] !== null) return true;
  }
  if (FaceColors.Side !== undefined) return true;
  return false;
}

export function NormalizeFaceSpec(FaceColors, FaceIdx, FallbackHex = "#c8c6c2", FallbackRough = 0.45, FallbackMetal = 0.0) {
  const Key = FACE_KEYS[FaceIdx] || "-Y";
  const Raw =
    FaceColors?.[Key] ??
    (FaceIdx <= 3 && FaceColors?.Side !== undefined ? FaceColors.Side : undefined);
  if (!Raw) {
    return {
      Fill: FallbackHex,
      Roughness: FallbackRough,
      Metallic: FallbackMetal,
      Emission: 0,
      Pattern: null,
      Accent: "#ffffff",
    };
  }
  if (typeof Raw === "string") {
    return {
      Fill: Raw,
      Roughness: FallbackRough,
      Metallic: FallbackMetal,
      Emission: 0,
      Pattern: null,
      Accent: "#ffffff",
    };
  }
  return {
    Fill: Raw.Fill || FallbackHex,
    Roughness: Raw.Roughness ?? FallbackRough,
    Metallic: Raw.Metallic ?? FallbackMetal,
    Emission: Raw.Emission ?? 0,
    Pattern: Raw.Pattern || null,
    Accent: Raw.Accent || "#f5f7fa",
  };
}

// Helper material constructor matching Frontier MaterialSpecification (clean flat OpenPBR materials by default)
function Mat(Fill, Roughness = 0.42, Metallic = 0.0, Extra = {}) {
  const Type = Extra.Type || (Metallic > 0.65 ? "Metal" : "Standard");
  const Channels = {
    colour: { Source: "Fill", Fill },
    roughness: { Source: "Fill", Fill: Roughness },
    metallic: { Source: "Fill", Fill: Metallic },
  };
  if (Extra.Coat !== undefined) {
    Channels.coat = { Source: "Fill", Fill: Extra.Coat };
    Channels.coatRoughness = { Source: "Fill", Fill: Extra.CoatRoughness ?? 0.08 };
  }
  if (Extra.Emission !== undefined) {
    Channels.emission = { Source: "Fill", Fill: Extra.Emission };
  }
  if (Extra.Transmission !== undefined) {
    Channels.transmission = { Source: "Fill", Fill: Extra.Transmission };
    Channels.ior = { Source: "Fill", Fill: Extra.Ior ?? 1.52 };
  }
  if (Extra.GradientStops) {
    Channels.colour = {
      Source: "Gradient",
      Axis: Extra.GradientAxis || "V",
      Fill,
      Stops: Extra.GradientStops,
    };
  }
  return {
    Material: {
      Name: Extra.Name || "Prop Material",
      Type,
      Channels,
    },
  };
}

const PAL = {
  // Precision cinema & studio metals / polymers
  cinemaBody: Mat("#21252d", 0.44, 0.38),
  matteSlate: Mat("#1c2027", 0.54, 0.24),
  anodizedBlack: Mat("#14161b", 0.34, 0.78),
  gunmetal: Mat("#3b424e", 0.28, 0.86),
  titaniumIvory: Mat("#e5e7eb", 0.30, 0.18),
  machinedSilver: Mat("#cbd5e1", 0.20, 0.92),
  chrome: Mat("#dfe6ef", 0.09, 0.97),
  galvanized: Mat("#aeb6c0", 0.34, 0.82),
  steelPost: Mat("#3f4650", 0.42, 0.78),
  carbon: Mat("#1f232a", 0.26, 0.42, { Type: "Clear coat", Coat: 0.70, CoatRoughness: 0.09 }),
  rubberDark: Mat("#181a1e", 0.86, 0.02),
  unlitDiffuser: Mat("#2d333f", 0.52, 0.05),
  copper: Mat("#c87538", 0.24, 0.92),

  // Exact ControlVehicle (VehicleProvingGrounds CourseMaterial) wheel & tyre palette
  cvTyreRubber: Mat("#25262a", 0.761, 0.02), // CourseMaterial.Tyre: Dielectric(0.03741, 0.76087)
  cvRimGunmetal: Mat("#2d3036", 0.38, 0.76), // Forged dark gunmetal ControlVehicle rim
  cvHubCobalt: Mat("#3b78d8", 0.22, 1.0),    // CourseMaterial.Hub: anodized cobalt-blue center-lock hub
  cvBrakeBronze: Mat("#8c5230", 0.29, 0.82), // CourseMaterial.Brake: warm bronze-iron rotor disc

  // Clean flat motorsport & highway barrier colours (no noisy texture maps)
  fiaRed: Mat("#cf2222", 0.36, 0.05),
  kerbRed: Mat("#c82222", 0.58, 0.02),       // CourseMaterial.KerbRed
  kerbWhite: Mat("#edefe9", 0.54, 0.02),     // CourseMaterial.PaintWhite
  tecproWhite: Mat("#f1f3f5", 0.38, 0.02),
  apronAsphalt: Mat("#464b52", 0.86, 0.0),
  concreteBase: Mat("#a4a9a6", 0.86, 0.0),
  concreteWall: Mat("#bcbFBd", 0.82, 0.0),
  concreteCap: Mat("#cfd3d0", 0.78, 0.0),
  fitchYellow: Mat("#eab308", 0.42, 0.02),   // Flat highway Fitch sand-barrel yellow
  fitchRibYellow: Mat("#d99b06", 0.44, 0.02),

  fiaBlue: Mat("#1d4ed8", 0.32, 0.12, { Type: "Clear coat", Coat: 0.55, CoatRoughness: 0.1 }),
  safetyOrange: Mat("#ea580c", 0.38, 0.02),
  safetyYellow: Mat("#eab308", 0.36, 0.06),
  skyBlueArri: Mat("#2563eb", 0.34, 0.45),
  whiteCoat: Mat("#f3f4f6", 0.34, 0.04),

  // Multi-coated convex optical glass & clean emissive indicators
  sapphireOptic: Mat("#075985", 0.02, 0.18, { Type: "Clear coat", Coat: 1.0, CoatRoughness: 0.015 }),
  emeraldOptic: Mat("#0f766e", 0.03, 0.16, { Type: "Clear coat", Coat: 1.0, CoatRoughness: 0.02 }),
  monitorSlate: Mat("#0f172a", 0.12, 0.15, { Type: "Clear coat", Coat: 0.9, CoatRoughness: 0.04 }),
  warmGlow: Mat("#fff4d6", 0.18, 0.0, { Type: "Unlit", Emission: 4.8 }),
  coolGlow: Mat("#e0f2fe", 0.18, 0.0, { Type: "Unlit", Emission: 5.2 }),
  amberBeacon: Mat("#f59e0b", 0.16, 0.0, { Type: "Unlit", Emission: 4.5 }),
  tallyGreen: Mat("#22c55e", 0.15, 0.0, { Type: "Unlit", Emission: 4.2 }),
  tallyRed: Mat("#ef4444", 0.15, 0.0, { Type: "Unlit", Emission: 4.4 }),
};

// 3x3 rotation matrix helpers matching Shader Euler convention: R = Rz * Ry * Rx
export function EulerDegToMat3(RotDeg = [0, 0, 0]) {
  const Rx = ((Number(RotDeg[0]) || 0) * Math.PI) / 180;
  const Ry = ((Number(RotDeg[1]) || 0) * Math.PI) / 180;
  const Rz = ((Number(RotDeg[2]) || 0) * Math.PI) / 180;
  const Cx = Math.cos(Rx), Sx = Math.sin(Rx);
  const Cy = Math.cos(Ry), Sy = Math.sin(Ry);
  const Cz = Math.cos(Rz), Sz = Math.sin(Rz);
  return [
    [Cz * Cy, Sz * Cy, -Sy],
    [Cz * Sy * Sx - Sz * Cx, Sz * Sy * Sx + Cz * Cx, Cy * Sx],
    [Cz * Sy * Cx + Sz * Sx, Sz * Sy * Cx - Cz * Sx, Cy * Cx],
  ];
}

export function MulMat3(A, B) {
  const Out = [[0, 0, 0], [0, 0, 0], [0, 0, 0]];
  for (let Col = 0; Col < 3; ++Col) {
    const Bx = B[Col][0], By = B[Col][1], Bz = B[Col][2];
    Out[Col][0] = A[0][0] * Bx + A[1][0] * By + A[2][0] * Bz;
    Out[Col][1] = A[0][1] * Bx + A[1][1] * By + A[2][1] * Bz;
    Out[Col][2] = A[0][2] * Bx + A[1][2] * By + A[2][2] * Bz;
  }
  return Out;
}

export function Mat3ToEulerRad(M) {
  const Sy = Math.max(-1, Math.min(1, -M[0][2]));
  const Ry = Math.asin(Sy);
  const Cy = Math.cos(Ry);
  let Rx = 0, Rz = 0;
  if (Math.abs(Cy) > 1e-4) {
    Rx = Math.atan2(M[1][2], M[2][2]);
    Rz = Math.atan2(M[0][1], M[0][0]);
  } else {
    Rx = Math.atan2(-M[2][1], M[1][1]);
    Rz = 0;
  }
  return [Rx, Ry, Rz];
}

export function TransformVec3ByMat3(M, V) {
  return [
    M[0][0] * V[0] + M[1][0] * V[1] + M[2][0] * V[2],
    M[0][1] * V[0] + M[1][1] * V[1] + M[2][1] * V[2],
    M[0][2] * V[0] + M[1][2] * V[1] + M[2][2] * V[2],
  ];
}

function Part(Shape, Pos, HalfExtents, RotDeg, MaterialRec, FaceColors = null, Extra = {}) {
  return {
    Shape, // 0=box, 1=sphere, 2=cylinder, 3=cone, 4=torus, 5=cylinder-cone
    LocalOffset: Pos,
    HalfExtents,
    LocalRotDeg: RotDeg || [0, 0, 0],
    MaterialRec,
    FaceColors,
    CastShadows: Extra.CastShadows ?? true,
    GiContributor: Extra.GiContributor ?? true,
  };
}

// Helper: build a strut cylinder from P0 to P1 with radius R
function StrutPart(P0, P1, Radius, MaterialRec) {
  const Mid = [(P0[0] + P1[0]) * 0.5, (P0[1] + P1[1]) * 0.5, (P0[2] + P1[2]) * 0.5];
  const Dx = P1[0] - P0[0], Dy = P1[1] - P0[1], Dz = P1[2] - P0[2];
  const Len = Math.max(1e-4, Math.hypot(Dx, Dy, Dz));
  const Uz = [Dx / Len, Dy / Len, Dz / Len];
  const Ref = Math.abs(Uz[2]) < 0.92 ? [0, 0, 1] : [1, 0, 0];
  let Ux = [
    Ref[1] * Uz[2] - Ref[2] * Uz[1],
    Ref[2] * Uz[0] - Ref[0] * Uz[2],
    Ref[0] * Uz[1] - Ref[1] * Uz[0],
  ];
  const Lx = Math.max(1e-5, Math.hypot(...Ux));
  Ux = [Ux[0] / Lx, Ux[1] / Lx, Ux[2] / Lx];
  const Uy = [
    Uz[1] * Ux[2] - Uz[2] * Ux[1],
    Uz[2] * Ux[0] - Uz[0] * Ux[2],
    Uz[0] * Ux[1] - Uz[1] * Ux[0],
  ];
  const Rad = Mat3ToEulerRad([Ux, Uy, Uz]);
  return Part(
    2,
    Mid,
    [Radius, Radius, Len * 0.5],
    Rad.map((R) => (R * 180) / Math.PI),
    MaterialRec,
  );
}

// Helper: 3-leg studio / cinema tripod spanning Z = [BottomZ .. TopZ]
function BuildTripodLegs(BottomZ, TopZ, SpreadRadius, LegMat = PAL.carbon, ClampMat = PAL.fiaRed) {
  const Parts = [];
  for (let I = 0; I < 3; ++I) {
    const Ang = (I * 2 * Math.PI) / 3 + Math.PI / 6;
    const Foot = [Math.cos(Ang) * SpreadRadius, Math.sin(Ang) * SpreadRadius, BottomZ + 0.02];
    const Crown = [Math.cos(Ang) * 0.058, Math.sin(Ang) * 0.058, TopZ];
    Parts.push(StrutPart(Foot, Crown, 0.018, LegMat));
    Parts.push(Part(2, [Foot[0], Foot[1], BottomZ + 0.014], [0.038, 0.038, 0.014], [0, 0, 0], PAL.rubberDark));
    const Mid = [(Foot[0] + Crown[0]) * 0.52, (Foot[1] + Crown[1]) * 0.52, (Foot[2] + Crown[2]) * 0.52];
    Parts.push(Part(2, Mid, [0.024, 0.024, 0.018], [0, 0, 0], ClampMat));
  }
  Parts.push(Part(2, [0, 0, (BottomZ + TopZ) * 0.56], [0.022, 0.022, (TopZ - BottomZ) * 0.35], [0, 0, 0], PAL.anodizedBlack));
  Parts.push(Part(2, [0, 0, TopZ + 0.024], [0.078, 0.078, 0.026], [0, 0, 0], PAL.gunmetal));
  return Parts;
}

// Helper: Bespoke Cinema Prime Lens + Machined Follow-Focus Torus Rings + Convex Sapphire Element + Open 4-Vane Matte Box
function BuildCinemaOptics({
  MountY = -0.14,
  CenterZ = 0.0,
  BarrelR = 0.068,
  BarrelHalfLen = 0.085,
  MatteBoxW = 0.138,
  MatteBoxH = 0.104,
  WithFrenchFlag = true,
  TallyMat = null,
}) {
  const BarrelMidY = MountY - BarrelHalfLen;
  const FrontGlassY = BarrelMidY - BarrelHalfLen + 0.012;
  const HoodY = FrontGlassY - 0.036;
  const Parts = [
    // Stainless steel PL-mount bayonet flange
    Part(2, [0, MountY + 0.008, CenterZ], [BarrelR + 0.008, BarrelR + 0.008, 0.014], [90, 0, 0], PAL.machinedSilver),
    // Stepped anodized prime lens barrel
    Part(2, [0, BarrelMidY, CenterZ], [BarrelR, BarrelR, BarrelHalfLen], [90, 0, 0], PAL.anodizedBlack),
    // Two 3D machined knurled follow-focus & iris gear rings (upright torus around Y optical axis)
    Part(4, [0, BarrelMidY + BarrelHalfLen * 0.30, CenterZ], [BarrelR + 0.012, BarrelR + 0.012, 0.010], [90, 0, 0], PAL.gunmetal),
    Part(4, [0, BarrelMidY - BarrelHalfLen * 0.24, CenterZ], [BarrelR + 0.011, BarrelR + 0.011, 0.009], [90, 0, 0], PAL.fiaRed),
    // Convex multi-coated front sapphire optical glass dome nestled inside front lens bezel
    Part(4, [0, FrontGlassY - 0.006, CenterZ], [BarrelR + 0.004, BarrelR + 0.004, 0.008], [90, 0, 0], PAL.machinedSilver),
    Part(1, [0, FrontGlassY + 0.022, CenterZ], [BarrelR * 0.88, BarrelR * 0.88, BarrelR * 0.88], [0, 0, 0], PAL.sapphireOptic),
    // Open 4-vane flared carbon-fiber matte box hood (top, bottom, left, right angled vanes — open optical center!)
    Part(0, [0, HoodY, CenterZ + MatteBoxH * 0.90], [MatteBoxW, 0.042, 0.009], [-14, 0, 0], PAL.carbon),
    Part(0, [0, HoodY, CenterZ - MatteBoxH * 0.90], [MatteBoxW, 0.042, 0.009], [14, 0, 0], PAL.carbon),
    Part(0, [-MatteBoxW * 0.92, HoodY, CenterZ], [0.009, 0.042, MatteBoxH * 0.88], [0, 0, 14], PAL.carbon),
    Part(0, [MatteBoxW * 0.92, HoodY, CenterZ], [0.009, 0.042, MatteBoxH * 0.88], [0, 0, -14], PAL.carbon),
  ];
  if (WithFrenchFlag) {
    // Hinged top carbon eyebrow flag tilted down over the front of the matte box
    Parts.push(
      Part(0, [0, HoodY - 0.048, CenterZ + MatteBoxH * 1.06], [MatteBoxW * 1.04, 0.046, 0.006], [24, 0, 0], PAL.anodizedBlack),
    );
  }
  if (TallyMat) {
    Parts.push(Part(0, [0, HoodY - 0.012, CenterZ + MatteBoxH * 0.98], [0.028, 0.010, 0.009], [0, 0, 0], TallyMat));
  }
  return Parts;
}

// Helper: Sculpted NATO Top Carry Handle + Dual 15mm LWS Carbon/Titanium Support Rods
function BuildRigHardware({ BodyTopZ, RodZ, FrontY, RearY }) {
  const Span = (RearY - FrontY) * 0.5;
  const MidY = (FrontY + RearY) * 0.5;
  return [
    // Front & rear NATO handle CNC risers + cylindrical grip bar + rear safety stop
    Part(0, [0, MidY - 0.07, BodyTopZ + 0.026], [0.016, 0.018, 0.026], [0, 0, 0], PAL.gunmetal),
    Part(0, [0, MidY + 0.07, BodyTopZ + 0.026], [0.016, 0.018, 0.026], [0, 0, 0], PAL.gunmetal),
    Part(2, [0, MidY - 0.005, BodyTopZ + 0.054], [0.015, 0.015, 0.115], [90, 0, 0], PAL.anodizedBlack),
    Part(2, [0, MidY + 0.108, BodyTopZ + 0.062], [0.018, 0.018, 0.012], [0, 0, 0], PAL.fiaRed),
    // Dual 15mm LWS support rods + baseplate bridge
    Part(0, [0, MidY + 0.01, RodZ - 0.012], [0.072, 0.095, 0.016], [0, 0, 0], PAL.gunmetal),
    Part(2, [-0.048, MidY - 0.05, RodZ], [0.009, 0.009, Span], [90, 0, 0], PAL.machinedSilver),
    Part(2, [0.048, MidY - 0.05, RodZ], [0.009, 0.009, Span], [90, 0, 0], PAL.machinedSilver),
  ];
}

// Helper: Pitched aerodynamic fan rotor blades arranged radially around the Y-axis (supports live SpinDeg rotation!)
function BuildRadialFanBlades({ CenterY, CenterZ, HubRadius, OuterRadius, BladeCount, BladeChord, PitchDeg = 28, SpinDeg = 0, BladeMat = PAL.machinedSilver }) {
  const Parts = [];
  const BladeHalfSpan = Math.max(0.02, (OuterRadius - HubRadius * 0.85) * 0.5);
  const MidR = HubRadius * 0.85 + BladeHalfSpan;
  for (let I = 0; I < BladeCount; ++I) {
    const ThetaDeg = (I * 360) / BladeCount + (Number(SpinDeg) || 0);
    const ThetaRad = (ThetaDeg * Math.PI) / 180;
    const Bx = Math.cos(ThetaRad) * MidR;
    const Bz = CenterZ + Math.sin(ThetaRad) * MidR;
    // Rotate blade radially around Y-axis (Ry = -ThetaDeg) with aerodynamic blade pitch around local X (Rx = PitchDeg)
    const MRadial = EulerDegToMat3([0, -ThetaDeg, 0]);
    const MPitch = EulerDegToMat3([PitchDeg, 0, 0]);
    const MCombined = MulMat3(MRadial, MPitch);
    const RotDeg = Mat3ToEulerRad(MCombined).map((R) => (R * 180) / Math.PI);
    Parts.push(Part(0, [Bx, CenterY, Bz], [BladeHalfSpan, 0.006, BladeChord], RotDeg, BladeMat));
  }
  return Parts;
}

// Rotate & translate an array of Parts rigidly around Origin into a new pose (e.g. leaning a ControlVehicle wheel against a stack!)
function TransformPartGroup(Parts, GroupRotDeg = [0, 0, 0], GroupTranslate = [0, 0, 0]) {
  const MGroup = EulerDegToMat3(GroupRotDeg);
  return Parts.map((P) => {
    const Off = TransformVec3ByMat3(MGroup, P.LocalOffset);
    const MLocal = EulerDegToMat3(P.LocalRotDeg);
    const MCombined = MulMat3(MGroup, MLocal);
    const RotDeg = Mat3ToEulerRad(MCombined).map((R) => (R * 180) / Math.PI);
    return {
      ...P,
      LocalOffset: [
        GroupTranslate[0] + Off[0],
        GroupTranslate[1] + Off[1],
        GroupTranslate[2] + Off[2],
      ],
      LocalRotDeg: RotDeg,
    };
  });
}
// Uses 3 overlapping torus rings so the central wheel bore is 100% hollow (never blocked by a solid cylinder endcap!)
function BuildControlVehicleTyre({ Pos = [0, 0, 0], ScaleFactor = 0.78, WithRimAndHub = false }) {
  const OuterR = 0.5086 * ScaleFactor;   // 0.397 m at 0.78x
  const HalfW = 0.1979 * ScaleFactor;    // 0.154 m at 0.78x
  const TubeR = 0.096 * ScaleFactor;     // Plump overlapping sidewall shoulder radius -> seamless smooth GT slick sidewall!
  const ShoulderZ = HalfW - TubeR;
  const RimR = 0.3446 * ScaleFactor;     // 0.269 m ControlVehicle rim flange radius
  const [Cx, Cy, Cz] = Pos;

  const Parts = [
    // Seamless plump GT racing slick carcass with true open inner wheel bore (zero cylinder endcap blocking the spokes!)
    Part(4, [Cx, Cy, Cz - ShoulderZ], [OuterR * 0.996, OuterR * 0.996, TubeR], [0, 0, 0], PAL.cvTyreRubber),
    Part(4, [Cx, Cy, Cz], [OuterR, OuterR, TubeR * 1.05], [0, 0, 0], PAL.cvTyreRubber),
    Part(4, [Cx, Cy, Cz + ShoulderZ], [OuterR * 0.996, OuterR * 0.996, TubeR], [0, 0, 0], PAL.cvTyreRubber),
  ];

  if (WithRimAndHub) {
    // Forged dark gunmetal outer rim lip + deep recessed brake rotor disc
    Parts.push(
      Part(4, [Cx, Cy, Cz + HalfW * 0.72], [RimR + 0.012, RimR + 0.012, 0.016], [0, 0, 0], PAL.cvRimGunmetal),
      Part(2, [Cx, Cy, Cz - HalfW * 0.18], [RimR * 0.96, RimR * 0.96, 0.022], [0, 0, 0], PAL.anodizedBlack),
      Part(2, [Cx, Cy, Cz + HalfW * 0.08], [RimR * 0.74, RimR * 0.74, 0.016], [0, 0, 0], PAL.cvBrakeBronze),
    );
    // 5 concave forged gunmetal spokes angling from outer rim flange down into the ControlVehicle center-lock hub
    for (let S = 0; S < 5; ++S) {
      const Ang = (S * 2 * Math.PI) / 5 + 0.25;
      const POuter = [Cx + Math.cos(Ang) * (RimR * 0.92), Cy + Math.sin(Ang) * (RimR * 0.92), Cz + HalfW * 0.68];
      const PInner = [Cx + Math.cos(Ang) * (RimR * 0.26), Cy + Math.sin(Ang) * (RimR * 0.26), Cz + HalfW * 0.36];
      Parts.push(StrutPart(PInner, POuter, 0.020 * ScaleFactor, PAL.cvRimGunmetal));
    }
    // Signature ControlVehicle anodized cobalt-blue metallic center-lock hub & machined center nut (CourseMaterial.Hub)
    Parts.push(
      Part(2, [Cx, Cy, Cz + HalfW * 0.42], [0.118 * ScaleFactor, 0.118 * ScaleFactor, 0.026], [0, 0, 0], PAL.cvHubCobalt),
      Part(2, [Cx, Cy, Cz + HalfW * 0.56], [0.054 * ScaleFactor, 0.054 * ScaleFactor, 0.022], [0, 0, 0], PAL.machinedSilver),
    );
  }
  return Parts;
}

//==========================================================================================================================================
// CATALOG OF BESPOKE 3D CAMERAS, STUDIO LIGHTS, FANS, CONTROLVEHICLE TYRES & PROVING-GROUND BARRIERS (28 ITEMS)
//   Local coordinates are centered so Z ∈ [-HalfHeightZ, +HalfHeightZ], placing bottom flush at Z_world - HalfHeightZ!
//==========================================================================================================================================
export const WORLD_PROP_CATALOG = [
  // ===================================================== 1. CAMERAS (6) =====================================================
  {
    PropId: "editor_camera",
    Name: "Editor Camera Rig",
    Category: "Cameras",
    Panel: "camera",
    Icon: "camera",
    HalfHeightZ: 0.20,
    NominalScale: [1, 1, 1],
    DefaultFocalMm: 35,
    Description: "Digital cinema camera package (ARRI/RED style) · PL prime lens, knurled focus rings, open carbon matte box, EVF & V-mount",
    BuildParts: () => [
      // Sculpted chamfered sensor body + vertical side heatsink ribcage + crimson sensor-plane badge + rosette boss
      Part(0, [0, 0.04, -0.02], [0.082, 0.125, 0.088], [0, 0, 0], PAL.cinemaBody),
      Part(0, [0, 0.04, -0.02], [0.088, 0.085, 0.072], [0, 0, 0], PAL.anodizedBlack),
      Part(0, [0.089, 0.01, -0.01], [0.005, 0.036, 0.036], [0, 0, 0], PAL.fiaRed),
      Part(2, [0.090, -0.05, -0.05], [0.022, 0.022, 0.008], [0, 90, 0], PAL.machinedSilver),
      // Side OLED Electronic Viewfinder (EVF) barrel + flared rubber eyecup + ocular optic on -X
      Part(0, [-0.105, -0.04, 0.04], [0.026, 0.014, 0.014], [0, 0, 0], PAL.gunmetal),
      Part(2, [-0.135, 0.02, 0.05], [0.026, 0.026, 0.075], [90, 0, 0], PAL.anodizedBlack),
      Part(4, [-0.135, 0.098, 0.05], [0.032, 0.032, 0.009], [90, 0, 0], PAL.rubberDark),
      // Tilted rear V-Mount Li-Ion battery + emerald power tally bar
      Part(0, [0, 0.195, -0.01], [0.064, 0.034, 0.078], [6, 0, 0], PAL.gunmetal),
      Part(0, [-0.048, 0.232, 0.035], [0.008, 0.005, 0.024], [6, 0, 0], PAL.tallyGreen),
      // Multi-stage prime lens + focus gear rings + sapphire glass dome + open 4-vane carbon matte box + French flag
      ...BuildCinemaOptics({
        MountY: -0.09,
        CenterZ: -0.02,
        BarrelR: 0.062,
        BarrelHalfLen: 0.082,
        MatteBoxW: 0.122,
        MatteBoxH: 0.092,
        WithFrenchFlag: true,
        TallyMat: PAL.tallyGreen,
      }),
      // NATO top carry handle + 15mm LWS dual support rods
      ...BuildRigHardware({ BodyTopZ: 0.068, RodZ: -0.132, FrontY: -0.24, RearY: 0.16 }),
    ],
  },
  {
    PropId: "engine_camera",
    Name: "Engine Camera",
    Category: "Cameras",
    Panel: "camera",
    Icon: "camera",
    HalfHeightZ: 0.28,
    NominalScale: [1, 1, 1],
    DefaultFocalMm: 28,
    Description: "35mm Panavision/Arriflex-style Director Engine Camera · twin coaxial film magazines, reflex viewfinder & anamorphic matte box",
    BuildParts: () => [
      // Sculpted 35mm movement body + machined silver side gear housing
      Part(0, [0, 0.03, -0.07], [0.085, 0.135, 0.092], [0, 0, 0], PAL.cinemaBody),
      Part(0, [-0.086, 0.02, -0.07], [0.008, 0.095, 0.068], [0, 0, 0], PAL.machinedSilver),
      // Iconic Twin Coaxial 400ft 35mm Film Magazines perched on top with brushed aluminum hub bosses
      Part(2, [0, -0.035, 0.125], [0.096, 0.096, 0.042], [0, 90, 0], PAL.anodizedBlack),
      Part(2, [0, -0.035, 0.125], [0.036, 0.036, 0.048], [0, 90, 0], PAL.machinedSilver),
      Part(2, [0, 0.155, 0.145], [0.096, 0.096, 0.042], [0, 90, 0], PAL.anodizedBlack),
      Part(2, [0, 0.155, 0.145], [0.036, 0.036, 0.048], [0, 90, 0], PAL.machinedSilver),
      Part(0, [0, 0.06, 0.055], [0.048, 0.095, 0.038], [10, 0, 0], PAL.gunmetal),
      // Long articulated optical reflex viewfinder along operator left (-X)
      Part(2, [-0.125, 0.06, -0.04], [0.022, 0.022, 0.135], [90, 0, 0], PAL.anodizedBlack),
      Part(4, [-0.125, 0.20, -0.04], [0.030, 0.030, 0.010], [90, 0, 0], PAL.rubberDark),
      // Anamorphic prime lens + focus gear rings + sapphire front element + open 4-vane matte box + red on-air tally
      ...BuildCinemaOptics({
        MountY: -0.11,
        CenterZ: -0.07,
        BarrelR: 0.066,
        BarrelHalfLen: 0.088,
        MatteBoxW: 0.132,
        MatteBoxH: 0.098,
        WithFrenchFlag: true,
        TallyMat: PAL.tallyRed,
      }),
      // Studio dovetail sled + dual 15mm support rods
      Part(0, [0, 0.02, -0.185], [0.076, 0.165, 0.018], [0, 0, 0], PAL.gunmetal),
      Part(2, [-0.052, -0.04, -0.168], [0.009, 0.009, 0.22], [90, 0, 0], PAL.machinedSilver),
      Part(2, [0.052, -0.04, -0.168], [0.009, 0.009, 0.22], [90, 0, 0], PAL.machinedSilver),
    ],
  },
  {
    PropId: "cinema_camera",
    Name: "Tripod Broadcast Camera",
    Category: "Cameras",
    Panel: "camera",
    Icon: "camera",
    HalfHeightZ: 0.84,
    NominalScale: [1, 1, 1],
    DefaultFocalMm: 50,
    Description: "Studio cinema camera on 3-leg carbon tripod with OConnor fluid head, dual pan bars, sun-hooded monitor & open matte box",
    BuildParts: () => [
      ...BuildTripodLegs(-0.84, 0.34, 0.44, PAL.carbon, PAL.fiaRed),
      // OConnor-style cylindrical fluid head + side drag knob + twin rear telescopic pan bars
      Part(2, [0, 0, 0.41], [0.074, 0.074, 0.048], [0, 0, 0], PAL.anodizedBlack),
      Part(4, [-0.082, 0, 0.41], [0.032, 0.032, 0.009], [0, 90, 0], PAL.fiaRed),
      StrutPart([0.065, 0.04, 0.42], [0.20, 0.32, 0.24], 0.011, PAL.machinedSilver),
      StrutPart([-0.065, 0.04, 0.42], [-0.18, 0.30, 0.25], 0.011, PAL.machinedSilver),
      Part(2, [0.20, 0.32, 0.24], [0.017, 0.017, 0.045], [52, -22, 0], PAL.rubberDark),
      // Cinema camera body + V-Mount battery + NATO top handle
      Part(0, [0, 0.02, 0.56], [0.084, 0.135, 0.086], [0, 0, 0], PAL.cinemaBody),
      Part(0, [0, 0.19, 0.57], [0.066, 0.034, 0.076], [5, 0, 0], PAL.gunmetal),
      Part(2, [0, 0.01, 0.68], [0.014, 0.014, 0.095], [90, 0, 0], PAL.anodizedBlack),
      // Tilted 7-inch director monitor with 3D sun-hood visor
      Part(0, [0, -0.02, 0.755], [0.082, 0.010, 0.048], [-16, 0, 0], PAL.monitorSlate),
      Part(0, [0, -0.045, 0.802], [0.085, 0.026, 0.005], [-16, 0, 0], PAL.anodizedBlack),
      // Open matte box + multi-ring prime lens + convex sapphire glass
      ...BuildCinemaOptics({
        MountY: -0.12,
        CenterZ: 0.555,
        BarrelR: 0.060,
        BarrelHalfLen: 0.080,
        MatteBoxW: 0.120,
        MatteBoxH: 0.088,
        WithFrenchFlag: true,
        TallyMat: PAL.tallyRed,
      }),
    ],
  },
  {
    PropId: "gimbal_camera",
    Name: "3-Axis Ring Gimbal Camera",
    Category: "Cameras",
    Panel: "camera",
    Icon: "camera",
    HalfHeightZ: 0.36,
    NominalScale: [1, 1, 1],
    DefaultFocalMm: 24,
    Description: "Ronin-style carbon-fiber 3-axis motorized ring stabilizer with brushless gimbal yoke & cinema prime payload",
    BuildParts: () => [
      // Fold-out dual kickstand feet at base
      StrutPart([-0.18, 0.11, -0.35], [-0.07, 0.0, -0.28], 0.013, PAL.rubberDark),
      StrutPart([0.18, 0.11, -0.35], [0.07, 0.0, -0.28], 0.013, PAL.rubberDark),
      StrutPart([-0.18, -0.11, -0.35], [-0.07, 0.0, -0.28], 0.013, PAL.rubberDark),
      StrutPart([0.18, -0.11, -0.35], [0.07, 0.0, -0.28], 0.013, PAL.rubberDark),
      // Upright circular carbon-fiber ring + sculpted side rubberized grips + top brushless pan motor
      Part(4, [0, 0, 0.01], [0.295, 0.295, 0.020], [90, 0, 0], PAL.carbon),
      Part(2, [-0.282, 0, 0.01], [0.026, 0.026, 0.085], [0, 0, 0], PAL.fiaRed),
      Part(2, [0.282, 0, 0.01], [0.026, 0.026, 0.085], [0, 0, 0], PAL.fiaRed),
      Part(2, [0, 0, 0.285], [0.036, 0.036, 0.022], [0, 0, 0], PAL.anodizedBlack),
      Part(4, [0, 0, 0.285], [0.038, 0.038, 0.007], [0, 0, 0], PAL.copper),
      // Inner 3-axis CNC cradle yoke arms + roll motor
      Part(0, [0, 0.125, 0.02], [0.115, 0.014, 0.014], [0, 0, 0], PAL.anodizedBlack),
      Part(2, [0, 0.138, 0.02], [0.030, 0.030, 0.016], [90, 0, 0], PAL.copper),
      Part(0, [-0.105, 0.04, -0.02], [0.012, 0.095, 0.014], [0, 0, 0], PAL.anodizedBlack),
      Part(0, [0.105, 0.04, -0.02], [0.012, 0.095, 0.014], [0, 0, 0], PAL.anodizedBlack),
      // Suspended compact cinema body + prime lens + focus gear torus + sapphire front optic + top wireless monitor
      Part(0, [0, 0.02, -0.02], [0.068, 0.082, 0.064], [0, 0, 0], PAL.cinemaBody),
      ...BuildCinemaOptics({
        MountY: -0.065,
        CenterZ: -0.02,
        BarrelR: 0.048,
        BarrelHalfLen: 0.058,
        MatteBoxW: 0.092,
        MatteBoxH: 0.068,
        WithFrenchFlag: false,
      }),
      Part(0, [0, -0.02, 0.335], [0.078, 0.010, 0.044], [-18, 0, 0], PAL.monitorSlate),
    ],
  },
  {
    PropId: "highspeed_phantom_cam",
    Name: "1000 FPS High-Speed Camera",
    Category: "Cameras",
    Panel: "camera",
    Icon: "camera",
    HalfHeightZ: 0.27,
    NominalScale: [1, 1, 1],
    DefaultFocalMm: 85,
    Description: "Phantom-style 1000 fps proving-ground high-speed camera · titanium-white armored body, white telephoto lens & CNC spider",
    BuildParts: () => [
      // Heavy CNC 3-leg billet leveling spider base
      Part(2, [0, 0.02, -0.205], [0.092, 0.092, 0.028], [0, 0, 0], PAL.anodizedBlack),
      Part(0, [-0.135, -0.085, -0.245], [0.092, 0.028, 0.016], [0, 0, 32], PAL.gunmetal),
      Part(0, [0.135, -0.085, -0.245], [0.092, 0.028, 0.016], [0, 0, -32], PAL.gunmetal),
      Part(0, [0, 0.165, -0.245], [0.028, 0.092, 0.016], [0, 0, 0], PAL.gunmetal),
      // Armored titanium-ivory high-speed chassis + dark recessed heatsink core + crimson CineMag SSD module + top handle
      Part(0, [0, 0.05, -0.03], [0.092, 0.145, 0.098], [0, 0, 0], PAL.titaniumIvory),
      Part(0, [0, 0.05, -0.03], [0.098, 0.095, 0.068], [0, 0, 0], PAL.anodizedBlack),
      Part(0, [0, 0.08, 0.088], [0.072, 0.095, 0.024], [0, 0, 0], PAL.fiaRed),
      Part(2, [0, 0.04, 0.145], [0.014, 0.014, 0.115], [90, 0, 0], PAL.anodizedBlack),
      // Long white-barrel f/2.8 proving-ground telephoto lens + black focus rings + deep hood + sapphire optic
      Part(2, [0, -0.18, -0.03], [0.060, 0.060, 0.095], [90, 0, 0], PAL.titaniumIvory),
      Part(4, [0, -0.15, -0.03], [0.068, 0.068, 0.011], [90, 0, 0], PAL.anodizedBlack),
      Part(4, [0, -0.22, -0.03], [0.068, 0.068, 0.010], [90, 0, 0], PAL.anodizedBlack),
      Part(2, [0, -0.31, -0.03], [0.066, 0.066, 0.045], [90, 0, 0], PAL.anodizedBlack),
      Part(1, [0, -0.325, -0.03], [0.054, 0.054, 0.054], [0, 0, 0], PAL.emeraldOptic),
      // Twin side high-speed LED tracking strobe pods
      Part(2, [-0.125, -0.12, 0.03], [0.030, 0.030, 0.022], [90, 0, 0], PAL.anodizedBlack),
      Part(1, [-0.125, -0.142, 0.03], [0.022, 0.022, 0.022], [0, 0, 0], PAL.coolGlow),
      Part(2, [0.125, -0.12, 0.03], [0.030, 0.030, 0.022], [90, 0, 0], PAL.anodizedBlack),
      Part(1, [0.125, -0.142, 0.03], [0.022, 0.022, 0.022], [0, 0, 0], PAL.coolGlow),
    ],
  },
  {
    PropId: "eng_camera_case",
    Name: "ENG Shoulder Camera & Case",
    Category: "Cameras",
    Panel: "camera",
    Icon: "camera",
    HalfHeightZ: 0.34,
    NominalScale: [1, 1, 1],
    DefaultFocalMm: 32,
    Description: "Broadcast ENG shoulder camera with B4 servo zoom lens & shotgun mic perched on a molded Pelican flight case",
    BuildParts: () => [
      // Clean flat molded flight case + black corner bumpers + twin silver butterfly latches + front handle (zero bitmap stripes!)
      Part(0, [0, 0, -0.18], [0.285, 0.205, 0.155], [0, 0, 0], PAL.safetyOrange),
      Part(0, [0, 0, -0.065], [0.294, 0.214, 0.016], [0, 0, 0], PAL.rubberDark),
      Part(0, [-0.14, -0.212, -0.09], [0.026, 0.010, 0.036], [0, 0, 0], PAL.machinedSilver),
      Part(0, [0.14, -0.212, -0.09], [0.026, 0.010, 0.036], [0, 0, 0], PAL.machinedSilver),
      Part(2, [0, -0.224, -0.16], [0.012, 0.012, 0.075], [0, 90, 0], PAL.rubberDark),
      // Broadcast ENG shoulder camera angled at 10 deg on top of case
      Part(0, [0.01, 0.04, 0.105], [0.072, 0.155, 0.092], [0, 0, 10], PAL.cinemaBody),
      Part(0, [0.01, 0.04, 0.005], [0.068, 0.095, 0.018], [0, 0, 10], PAL.rubberDark),
      Part(0, [-0.02, 0.215, 0.11], [0.062, 0.032, 0.078], [0, 0, 10], PAL.gunmetal),
      // B4 servo zoom lens + side ergonomic servo rocker pod + sapphire front glass + top shotgun mic
      Part(2, [0.042, -0.175, 0.095], [0.050, 0.050, 0.088], [90, 0, 10], PAL.anodizedBlack),
      Part(4, [0.040, -0.165, 0.095], [0.058, 0.058, 0.009], [90, 0, 10], PAL.gunmetal),
      Part(0, [0.098, -0.155, 0.082], [0.020, 0.048, 0.026], [0, 0, 10], PAL.rubberDark),
      Part(1, [0.055, -0.252, 0.095], [0.042, 0.042, 0.042], [0, 0, 0], PAL.sapphireOptic),
      Part(2, [0.065, -0.135, 0.228], [0.014, 0.014, 0.115], [90, 0, 10], PAL.rubberDark),
      Part(2, [0.005, 0.02, 0.222], [0.013, 0.013, 0.095], [90, 0, 10], PAL.anodizedBlack),
    ],
  },

  // ================================================= 2. STUDIO & STAGE LIGHTS (6) =================================================
  {
    PropId: "led_panel_stand",
    Name: "Bi-Color Studio LED Panel",
    Category: "Studio Lights",
    Panel: "light",
    Icon: "editor-area-light",
    HalfHeightZ: 0.90,
    NominalScale: [1, 1, 1],
    LightEmitter: {
      Offset: [0, -0.18, 0.58],
      AimDir: [0, -0.94, -0.34],
      Width: 0.68,
      Height: 0.46,
      Radiance: [8.8, 7.8, 6.2],
      ColorHex: "#fff3d6",
      Lumens: 3200,
    },
    Description: "1x1 Bi-Color LED studio panel on chrome C-stand with 4 barndoors & live LTC area light emission",
    BuildParts: (Rec = {}) => {
      const IsOn = Rec?.Powered !== false;
      return [
        ...BuildTripodLegs(-0.90, 0.22, 0.40, PAL.chrome, PAL.anodizedBlack),
        Part(2, [0, 0, 0.38], [0.018, 0.018, 0.20], [0, 0, 0], PAL.chrome),
        Part(0, [0, 0, 0.58], [0.34, 0.042, 0.23], [0, 0, 0], PAL.anodizedBlack),
        Part(0, [0, -0.044, 0.58], [0.31, 0.006, 0.20], [0, 0, 0], IsOn ? PAL.warmGlow : PAL.unlitDiffuser),
        Part(0, [0, -0.11, 0.83], [0.34, 0.08, 0.010], [32, 0, 0], PAL.matteSlate),
        Part(0, [0, -0.11, 0.33], [0.34, 0.08, 0.010], [-32, 0, 0], PAL.matteSlate),
        Part(0, [-0.38, -0.10, 0.58], [0.010, 0.08, 0.22], [0, 0, -28], PAL.matteSlate),
        Part(0, [0.38, -0.10, 0.58], [0.010, 0.08, 0.22], [0, 0, 28], PAL.matteSlate),
        Part(0, [0.10, 0.075, 0.55], [0.06, 0.032, 0.085], [0, 0, 0], PAL.gunmetal),
      ];
    },
  },
  {
    PropId: "softbox_cinema_light",
    Name: "Octagon Softbox Studio Light",
    Category: "Studio Lights",
    Panel: "light",
    Icon: "editor-area-light",
    HalfHeightZ: 0.95,
    NominalScale: [1, 1, 1],
    LightEmitter: {
      Offset: [0, -0.28, 0.56],
      AimDir: [0, -0.95, -0.31],
      Width: 0.85,
      Height: 0.85,
      Radiance: [9.4, 8.9, 8.2],
      ColorHex: "#fff9ed",
      Lumens: 3800,
    },
    Description: "Parabolic key-light softbox dome on studio stand with COB lamp head & glowing front silk diffuser",
    BuildParts: (Rec = {}) => {
      const IsOn = Rec?.Powered !== false;
      return [
        ...BuildTripodLegs(-0.95, 0.25, 0.44, PAL.anodizedBlack, PAL.safetyYellow),
        Part(2, [0, 0, 0.42], [0.020, 0.020, 0.20], [0, 0, 0], PAL.chrome),
        Part(2, [0, 0.14, 0.58], [0.095, 0.095, 0.12], [90, 0, 0], PAL.gunmetal),
        Part(5, [0, -0.08, 0.58], [0.42, 0.42, 0.18], [-90, 0, 0], PAL.rubberDark, {
          Side: { Fill: "#18191e", Roughness: 0.82, Metallic: 0.05 },
          "+Z": IsOn
            ? { Fill: "#fffaf0", Roughness: 0.18, Metallic: 0.0, Emission: 5.8 }
            : { Fill: "#2e3440", Roughness: 0.55, Metallic: 0.04, Emission: 0.0 },
          "-Z": { Fill: "#27272a", Roughness: 0.6, Metallic: 0.4 },
        }),
      ];
    },
  },
  {
    PropId: "floodlight_tower",
    Name: "4-Bank Stadium Floodlight Tower",
    Category: "Studio Lights",
    Panel: "light",
    Icon: "editor-area-light",
    HalfHeightZ: 1.25,
    NominalScale: [1, 1, 1],
    LightEmitter: {
      Offset: [0, -0.18, 1.00],
      AimDir: [0, -0.90, -0.44],
      Width: 1.15,
      Height: 0.55,
      Radiance: [11.5, 11.2, 10.6],
      ColorHex: "#f8fafc",
      Lumens: 5200,
    },
    Description: "Mobile safety-yellow paddock generator mast with 4 high-output stadium LED flood pods",
    BuildParts: (Rec = {}) => {
      const IsOn = Rec?.Powered !== false;
      return [
        Part(0, [0, 0, -1.04], [0.34, 0.44, 0.17], [0, 0, 0], PAL.safetyYellow),
        Part(0, [0, 0, -1.19], [0.36, 0.46, 0.025], [0, 0, 0], PAL.anodizedBlack),
        Part(2, [-0.36, 0.22, -1.15], [0.09, 0.09, 0.04], [0, 90, 0], PAL.cvTyreRubber),
        Part(2, [0.36, 0.22, -1.15], [0.09, 0.09, 0.04], [0, 90, 0], PAL.cvTyreRubber),
        Part(2, [0, 0, -0.02], [0.042, 0.042, 0.92], [0, 0, 0], PAL.galvanized),
        Part(0, [0, 0, 0.94], [0.56, 0.04, 0.035], [0, 0, 0], PAL.gunmetal),
        Part(2, [0, 0, 1.20], [0.042, 0.042, 0.05], [0, 0, 0], IsOn ? PAL.amberBeacon : PAL.unlitDiffuser),
        ...[-0.40, -0.14, 0.14, 0.40].flatMap((X) => [
          Part(0, [X, -0.05, 1.05], [0.11, 0.050, 0.085], [16, 0, 0], PAL.anodizedBlack),
          Part(0, [X, -0.102, 1.035], [0.095, 0.006, 0.070], [16, 0, 0], IsOn ? PAL.coolGlow : PAL.unlitDiffuser),
        ]),
      ];
    },
  },
  {
    PropId: "sky_panel_truss_bar",
    Name: "Twin SkyPanel Truss Light",
    Category: "Studio Lights",
    Panel: "light",
    Icon: "editor-area-light",
    HalfHeightZ: 1.02,
    NominalScale: [1, 1, 1],
    LightEmitter: {
      Offset: [0, -0.16, 0.72],
      AimDir: [0, -0.92, -0.39],
      Width: 1.25,
      Height: 0.42,
      Radiance: [8.5, 9.6, 11.2],
      ColorHex: "#e0f2fe",
      Lumens: 4400,
    },
    Description: "Heavy T-bar truss stand carrying twin wide ARRI-style RGBWW SkyPanel cinema soft lights",
    BuildParts: (Rec = {}) => {
      const IsOn = Rec?.Powered !== false;
      return [
        Part(0, [0, 0, -0.98], [0.42, 0.42, 0.035], [0, 0, 0], PAL.anodizedBlack),
        Part(2, [0, 0, -0.06], [0.032, 0.032, 0.88], [0, 0, 0], PAL.chrome),
        Part(0, [0, 0, 0.86], [0.68, 0.035, 0.035], [0, 0, 0], PAL.chrome),
        ...[-0.36, 0.36].flatMap((X) => [
          Part(0, [X, -0.04, 0.72], [0.27, 0.058, 0.14], [14, 0, 0], PAL.skyBlueArri),
          Part(0, [X, -0.10, 0.705], [0.245, 0.006, 0.118], [14, 0, 0], IsOn ? PAL.coolGlow : PAL.unlitDiffuser),
        ]),
      ];
    },
  },
  {
    PropId: "pit_worklight_tripod",
    Name: "Twin Paddock LED Worklight",
    Category: "Studio Lights",
    Panel: "light",
    Icon: "editor-area-light",
    HalfHeightZ: 0.86,
    NominalScale: [1, 1, 1],
    LightEmitter: {
      Offset: [0, -0.14, 0.70],
      AimDir: [0, -0.94, -0.34],
      Width: 0.62,
      Height: 0.30,
      Radiance: [8.4, 7.6, 5.8],
      ColorHex: "#fef3c7",
      Lumens: 2800,
    },
    Description: "Safety-yellow mechanic tripod with twin IP65 die-cast COB floodlight heads",
    BuildParts: (Rec = {}) => {
      const IsOn = Rec?.Powered !== false;
      return [
        ...BuildTripodLegs(-0.86, 0.20, 0.38, PAL.safetyYellow, PAL.anodizedBlack),
        Part(2, [0, 0, 0.38], [0.02, 0.02, 0.24], [0, 0, 0], PAL.safetyYellow),
        Part(0, [0, 0, 0.62], [0.30, 0.025, 0.022], [0, 0, 0], PAL.safetyYellow),
        ...[-0.18, 0.18].flatMap((X) => [
          Part(0, [X, -0.03, 0.72], [0.12, 0.042, 0.09], [10, 0, 0], PAL.safetyYellow),
          Part(0, [X, -0.074, 0.712], [0.102, 0.006, 0.072], [10, 0, 0], IsOn ? PAL.warmGlow : PAL.unlitDiffuser),
        ]),
      ];
    },
  },
  {
    PropId: "cine_tube_stand",
    Name: "Quad RGB Pixel Tube Stand",
    Category: "Studio Lights",
    Panel: "light",
    Icon: "editor-area-light",
    HalfHeightZ: 0.88,
    NominalScale: [1, 1, 1],
    LightEmitter: {
      Offset: [0, -0.10, 0.25],
      AimDir: [0, -1.0, 0.0],
      Omni: true,
      Width: 0.48,
      Height: 0.95,
      Radiance: [6.5, 8.8, 10.5],
      ColorHex: "#67e8f9",
      Lumens: 2600,
    },
    Description: "4-bank vertical RGBWW pixel LED cinema tubes on low-profile C-stand",
    BuildParts: (Rec = {}) => {
      const IsOn = Rec?.Powered !== false;
      return [
        ...BuildTripodLegs(-0.88, -0.38, 0.34, PAL.anodizedBlack, PAL.chrome),
        Part(2, [0, 0, -0.05], [0.018, 0.018, 0.36], [0, 0, 0], PAL.chrome),
        Part(0, [0, 0.02, 0.24], [0.22, 0.022, 0.028], [0, 0, 0], PAL.gunmetal),
        ...[
          [-0.15, "#38bdf8"],
          [-0.05, "#f8fafc"],
          [0.05, "#fbbf24"],
          [0.15, "#f43f5e"],
        ].map(([X, Hex]) =>
          Part(
            2,
            [X, -0.02, 0.26],
            [0.022, 0.022, 0.56],
            [0, 0, 0],
            IsOn ? Mat(Hex, 0.14, 0.0, { Type: "Unlit", Emission: 5.5 }) : PAL.unlitDiffuser,
          ),
        ),
      ];
    },
  },

  // ============================================ 3. MOTORSPORT & STUDIO FANS (3) ============================================
  {
    PropId: "wind_drum_fan",
    Name: "Brake-Cooling Drum Fan",
    Category: "Fans & Wind",
    IsFan: true,
    DefaultRpm: 1850,
    Panel: "geometry",
    Icon: "editor-cylinder",
    HalfHeightZ: 0.45,
    NominalScale: [1, 1, 1],
    Description: "88cm wheeled paddock steel drum fan · rolled yellow Venturi shroud, 7 pitched aero blades, chrome guard & tilt cradle",
    BuildParts: (Rec = {}) => {
      const IsOn = Rec?.Powered !== false;
      const SpinDeg = IsOn ? Number(Rec?.__FanSpinDeg || 0) : 0;
      return [
        // Tubular steel A-frame floor cradle + rear transport wheels + front rubber feet
        Part(0, [0, 0, -0.425], [0.36, 0.22, 0.018], [0, 0, 0], PAL.anodizedBlack),
        Part(0, [-0.385, 0, -0.18], [0.016, 0.028, 0.25], [0, 0, 0], PAL.anodizedBlack),
        Part(0, [0.385, 0, -0.18], [0.016, 0.028, 0.25], [0, 0, 0], PAL.anodizedBlack),
        Part(2, [-0.39, 0.18, -0.39], [0.058, 0.058, 0.024], [0, 90, 0], PAL.cvTyreRubber),
        Part(2, [0.39, 0.18, -0.39], [0.058, 0.058, 0.024], [0, 90, 0], PAL.cvTyreRubber),
        // Heavy rolled-steel yellow cylindrical drum shroud (overlapping Venturi rings -> 100% solid seamless drum wall!)
        Part(4, [0, -0.132, 0.03], [0.366, 0.366, 0.068], [90, 0, 0], PAL.safetyYellow),
        Part(4, [0, 0.132, 0.03], [0.366, 0.366, 0.068], [90, 0, 0], PAL.safetyYellow),
        Part(4, [0, 0.0, 0.03], [0.368, 0.368, 0.092], [90, 0, 0], PAL.safetyYellow),
        Part(4, [0, -0.066, 0.03], [0.367, 0.367, 0.090], [90, 0, 0], PAL.safetyYellow),
        Part(4, [0, 0.066, 0.03], [0.367, 0.367, 0.090], [90, 0, 0], PAL.safetyYellow),
        Part(2, [0, 0, 0.425], [0.013, 0.013, 0.10], [0, 90, 0], PAL.anodizedBlack),
        Part(1, [0.16, -0.145, 0.355], [0.014, 0.014, 0.014], [0, 0, 0], IsOn ? PAL.tallyGreen : PAL.rubberDark),
        // Central electric drive motor pod + machined aluminum nosecone spinner + 7 pitched impeller blades
        Part(2, [0, 0.04, 0.03], [0.072, 0.072, 0.085], [90, 0, 0], PAL.anodizedBlack),
        Part(1, [0, -0.048, 0.03], [0.068, 0.068, 0.068], [0, 0, 0], PAL.machinedSilver),
        ...BuildRadialFanBlades({
          CenterY: -0.02,
          CenterZ: 0.03,
          HubRadius: 0.065,
          OuterRadius: 0.325,
          BladeCount: 7,
          BladeChord: 0.046,
          PitchDeg: 28,
          SpinDeg,
          BladeMat: PAL.machinedSilver,
        }),
        // Front concentric chrome wire guard ring + cross-stator bars
        Part(4, [0, -0.145, 0.03], [0.22, 0.22, 0.006], [90, 0, 0], PAL.chrome),
        Part(0, [0, -0.148, 0.03], [0.34, 0.005, 0.005], [0, 0, 0], PAL.chrome),
        Part(0, [0, -0.148, 0.03], [0.005, 0.005, 0.34], [0, 0, 0], PAL.chrome),
      ];
    },
  },
  {
    PropId: "pit_turbine_fan",
    Name: "Pit-Lane Pedestal Turbine Fan",
    Category: "Fans & Wind",
    IsFan: true,
    DefaultRpm: 2400,
    Panel: "geometry",
    Icon: "editor-cylinder",
    HalfHeightZ: 0.62,
    NominalScale: [1, 1, 1],
    Description: "Directional motorsport radiator & studio wind-machine turbine on telescoping pedestal with cobalt Venturi lip",
    BuildParts: (Rec = {}) => {
      const IsOn = Rec?.Powered !== false;
      const SpinDeg = IsOn ? Number(Rec?.__FanSpinDeg || 0) * 1.25 : 0;
      return [
        // Heavy weighted circular studio base + telescoping column + anodized lock collar
        Part(2, [0, 0, -0.59], [0.28, 0.28, 0.028], [0, 0, 0], PAL.anodizedBlack),
        Part(2, [0, 0, -0.54], [0.09, 0.09, 0.028], [0, 0, 0], PAL.gunmetal),
        Part(2, [0, 0, -0.22], [0.026, 0.026, 0.32], [0, 0, 0], PAL.chrome),
        Part(2, [0, 0, -0.02], [0.036, 0.036, 0.024], [0, 0, 0], PAL.fiaRed),
        // Yoke cradle + deep jet-cowling Venturi duct with anodized cobalt-blue intake lip + power tally
        Part(0, [0, 0, 0.06], [0.285, 0.022, 0.018], [0, 0, 0], PAL.gunmetal),
        Part(0, [-0.28, 0, 0.20], [0.014, 0.024, 0.15], [0, 0, 0], PAL.gunmetal),
        Part(0, [0.28, 0, 0.20], [0.014, 0.024, 0.15], [0, 0, 0], PAL.gunmetal),
        Part(4, [0, -0.125, 0.28], [0.268, 0.268, 0.065], [90, 0, 0], PAL.cvHubCobalt),
        Part(4, [0, -0.055, 0.28], [0.262, 0.262, 0.088], [90, 0, 0], PAL.anodizedBlack),
        Part(4, [0, 0.020, 0.28], [0.262, 0.262, 0.088], [90, 0, 0], PAL.anodizedBlack),
        Part(4, [0, 0.095, 0.28], [0.264, 0.264, 0.075], [90, 0, 0], PAL.anodizedBlack),
        Part(1, [0, -0.145, 0.545], [0.013, 0.013, 0.013], [0, 0, 0], IsOn ? PAL.tallyGreen : PAL.rubberDark),
        // Rear finned motor pod + pointed aluminum nosecone + 6 high-pitch aero turbine blades
        Part(2, [0, 0.08, 0.28], [0.064, 0.064, 0.095], [90, 0, 0], PAL.gunmetal),
        Part(1, [0, -0.035, 0.28], [0.058, 0.058, 0.058], [0, 0, 0], PAL.machinedSilver),
        ...BuildRadialFanBlades({
          CenterY: -0.015,
          CenterZ: 0.28,
          HubRadius: 0.055,
          OuterRadius: 0.232,
          BladeCount: 6,
          BladeChord: 0.040,
          PitchDeg: 32,
          SpinDeg,
          BladeMat: PAL.machinedSilver,
        }),
      ];
    },
  },
  {
    PropId: "exhaust_blower_fan",
    Name: "Dyno Floor Air-Mover Blower",
    Category: "Fans & Wind",
    IsFan: true,
    DefaultRpm: 3200,
    Panel: "geometry",
    Icon: "editor-cylinder",
    HalfHeightZ: 0.35,
    NominalScale: [1, 1, 1],
    Description: "Proving-ground chassis-dyno axial floor wind-tunnel blower · 20° pitched crimson Venturi bellmouth, copper spinner & 7 aero blades",
    BuildParts: (Rec = {}) => {
      const IsOn = Rec?.Powered !== false;
      const SpinDeg = IsOn ? Number(Rec?.__FanSpinDeg || 0) * 1.5 : 0;
      // Heavy tubular steel skid frame + anti-vibration rubber pads + rear control box
      const Parts = [
        Part(0, [-0.24, 0.0, -0.325], [0.020, 0.24, 0.018], [0, 0, 0], PAL.anodizedBlack),
        Part(0, [0.24, 0.0, -0.325], [0.020, 0.24, 0.018], [0, 0, 0], PAL.anodizedBlack),
        Part(0, [0, -0.20, -0.325], [0.25, 0.020, 0.018], [0, 0, 0], PAL.anodizedBlack),
        Part(0, [0, 0.20, -0.325], [0.25, 0.020, 0.018], [0, 0, 0], PAL.anodizedBlack),
        Part(0, [-0.24, 0.02, -0.14], [0.016, 0.026, 0.18], [0, 0, 0], PAL.gunmetal),
        Part(0, [0.24, 0.02, -0.14], [0.016, 0.026, 0.18], [0, 0, 0], PAL.gunmetal),
        // Crimson axial Venturi bellmouth duct pitched 18° upward for underbody/radiator airflow
        Part(4, [0, -0.120, 0.044], [0.248, 0.248, 0.068], [108, 0, 0], PAL.fiaRed),
        Part(4, [0, -0.052, 0.022], [0.240, 0.240, 0.086], [108, 0, 0], PAL.fiaRed),
        Part(4, [0, 0.018, -0.002], [0.240, 0.240, 0.086], [108, 0, 0], PAL.anodizedBlack),
        Part(4, [0, 0.088, -0.024], [0.244, 0.244, 0.072], [108, 0, 0], PAL.fiaRed),
        Part(2, [0, 0.02, 0.305], [0.014, 0.014, 0.11], [0, 90, 0], PAL.anodizedBlack),
        Part(1, [0.13, -0.125, 0.265], [0.013, 0.013, 0.013], [0, 0, 0], IsOn ? PAL.tallyGreen : PAL.rubberDark),
        // Central copper/gunmetal high-RPM motor hub + spinner cone + concentric chrome safety grille
        Part(2, [0, 0.03, 0.00], [0.062, 0.062, 0.085], [108, 0, 0], PAL.anodizedBlack),
        Part(1, [0, -0.052, 0.026], [0.056, 0.056, 0.056], [0, 0, 0], PAL.copper),
        Part(4, [0, -0.135, 0.048], [0.155, 0.155, 0.005], [108, 0, 0], PAL.chrome),
        ...BuildRadialFanBlades({
          CenterY: -0.035,
          CenterZ: 0.020,
          HubRadius: 0.052,
          OuterRadius: 0.208,
          BladeCount: 7,
          BladeChord: 0.038,
          PitchDeg: 30,
          SpinDeg,
          BladeMat: PAL.machinedSilver,
        }),
      ];
      return Parts;
    },
  },

  // ============================================ 4. CONES, BARRIERS, KERB & PADDOCK (13) ============================================
  {
    PropId: "apex_cone",
    Name: "FIA Apex Marker Cone",
    Category: "Track & Cones",
    Panel: "geometry",
    Icon: "editor-cone",
    HalfHeightZ: 0.45,
    NominalScale: [1, 1, 1],
    Description: "Tall 90cm FIA fluorescent orange apex marker pylon with twin retro-reflective collars & rubber base",
    BuildParts: () => [
      Part(
        3,
        [0, 0, 0],
        [0.24, 0.24, 0.45],
        [0, 0, 0],
        Mat("#ff5500", 0.38, 0.0, {
          GradientAxis: "V",
          GradientStops: [
            { Id: "b0", Position: 0.0, Value: "#1d1f24" },
            { Id: "b1", Position: 0.13, Value: "#1d1f24" },
            { Id: "o1", Position: 0.15, Value: "#ff5500" },
            { Id: "o2", Position: 0.44, Value: "#ff5500" },
            { Id: "w1", Position: 0.46, Value: "#f8fafc" },
            { Id: "w2", Position: 0.58, Value: "#f8fafc" },
            { Id: "o3", Position: 0.60, Value: "#ff5500" },
            { Id: "o4", Position: 0.72, Value: "#ff5500" },
            { Id: "w3", Position: 0.74, Value: "#f8fafc" },
            { Id: "w4", Position: 0.86, Value: "#f8fafc" },
            { Id: "o5", Position: 0.88, Value: "#ff5500" },
          ],
        }),
      ),
    ],
  },
  {
    PropId: "slalom_cone",
    Name: "Autocross Slalom Cone",
    Category: "Track & Cones",
    Panel: "geometry",
    Icon: "editor-cone",
    HalfHeightZ: 0.24,
    NominalScale: [1, 1, 1],
    Description: "45cm high-visibility chartreuse-lime autocross slalom cone with weighted black plinth",
    BuildParts: () => [
      Part(
        3,
        [0, 0, 0],
        [0.18, 0.18, 0.24],
        [0, 0, 0],
        Mat("#84cc16", 0.36, 0.0, {
          GradientAxis: "V",
          GradientStops: [
            { Id: "b0", Position: 0.0, Value: "#18181b" },
            { Id: "b1", Position: 0.16, Value: "#18181b" },
            { Id: "g1", Position: 0.18, Value: "#84cc16" },
            { Id: "g2", Position: 0.62, Value: "#84cc16" },
            { Id: "w1", Position: 0.64, Value: "#f8fafc" },
            { Id: "w2", Position: 0.80, Value: "#f8fafc" },
            { Id: "g3", Position: 0.82, Value: "#84cc16" },
          ],
        }),
      ),
    ],
  },
  {
    PropId: "bollard_cone",
    Name: "FIA Chicane Bollard Post",
    Category: "Track & Cones",
    Panel: "geometry",
    Icon: "editor-cylinder",
    HalfHeightZ: 0.50,
    NominalScale: [1, 1, 1],
    Description: "100cm flexible FIA red/white striped pit-entry delineator bollard with bolted rubber flange",
    BuildParts: () => [
      Part(
        5,
        [0, 0, 0],
        [0.19, 0.19, 0.50],
        [0, 0, 0],
        Mat("#dc2626", 0.34, 0.0, {
          GradientAxis: "V",
          GradientStops: [
            { Id: "b0", Position: 0.0, Value: "#18191e" },
            { Id: "b1", Position: 0.11, Value: "#18191e" },
            { Id: "r1", Position: 0.13, Value: "#dc2626" },
            { Id: "r2", Position: 0.36, Value: "#dc2626" },
            { Id: "w1", Position: 0.38, Value: "#f8fafc" },
            { Id: "w2", Position: 0.56, Value: "#f8fafc" },
            { Id: "r3", Position: 0.58, Value: "#dc2626" },
            { Id: "r4", Position: 0.74, Value: "#dc2626" },
            { Id: "w3", Position: 0.76, Value: "#f8fafc" },
            { Id: "w4", Position: 0.90, Value: "#f8fafc" },
            { Id: "r5", Position: 0.92, Value: "#dc2626" },
          ],
        }),
      ),
    ],
  },
  {
    PropId: "foldable_cone",
    Name: "LED Beacon Safety Cone",
    Category: "Track & Cones",
    Panel: "geometry",
    Icon: "editor-cone",
    HalfHeightZ: 0.36,
    NominalScale: [1, 1, 1],
    Description: "Emergency paddock safety cone topped with a glowing amber LED strobe beacon",
    BuildParts: () => [
      Part(
        3,
        [0, 0, -0.04],
        [0.21, 0.21, 0.32],
        [0, 0, 0],
        Mat("#ff5500", 0.40, 0.0, {
          GradientAxis: "V",
          GradientStops: [
            { Id: "b0", Position: 0.0, Value: "#1f232a" },
            { Id: "b1", Position: 0.15, Value: "#1f232a" },
            { Id: "o1", Position: 0.17, Value: "#ff5500" },
            { Id: "o2", Position: 0.55, Value: "#ff5500" },
            { Id: "w1", Position: 0.57, Value: "#f8fafc" },
            { Id: "w2", Position: 0.78, Value: "#f8fafc" },
            { Id: "o3", Position: 0.80, Value: "#ff5500" },
          ],
        }),
      ),
      Part(2, [0, 0, 0.315], [0.038, 0.038, 0.042], [0, 0, 0], PAL.amberBeacon),
    ],
  },
  {
    PropId: "tire_stack",
    Name: "ControlVehicle GT Tyre Stack",
    Category: "Barriers & Paddock",
    Panel: "geometry",
    Icon: "editor-cylinder",
    HalfHeightZ: 0.44,
    NominalScale: [1, 1, 1],
    Description: "4-corner ControlVehicle 396mm GT slick wheel set · 3 stacked flat + 1 leaning upright with 5-spoke rim & cobalt center-lock hub",
    BuildParts: () => [
      // 3 stacked ControlVehicle 396mm GT slicks (ScaleFactor = 0.72 -> OuterR = 0.366, HalfW = 0.142, total stack Z ∈ [-0.43 .. +0.43])
      ...BuildControlVehicleTyre({ Pos: [-0.08, 0.08, -0.290], ScaleFactor: 0.72, WithRimAndHub: false }),
      ...BuildControlVehicleTyre({ Pos: [-0.06, 0.09, -0.005], ScaleFactor: 0.72, WithRimAndHub: false }),
      ...BuildControlVehicleTyre({ Pos: [-0.09, 0.07, 0.280], ScaleFactor: 0.72, WithRimAndHub: true }),
      // 4th ControlVehicle wheel leaning upright (74° pitch, 18° yaw) against the stack, showcasing the 5-spoke rim & cobalt-blue hub!
      ...TransformPartGroup(
        BuildControlVehicleTyre({ Pos: [0, 0, 0], ScaleFactor: 0.72, WithRimAndHub: true }),
        [74, 0, -18],
        [0.24, -0.26, -0.075],
      ),
    ],
  },
  {
    PropId: "jersey_barrier",
    Name: "New Jersey Concrete Barrier",
    Category: "Barriers & Paddock",
    Panel: "geometry",
    Icon: "editor-cube",
    HalfHeightZ: 0.44,
    NominalScale: [1, 1, 1],
    Description: "Authentic sloped F-profile New Jersey precast concrete barrier with scupper slots, steel interlock loops & top reflector",
    BuildParts: () => [
      // Planted 62cm-wide lower concrete base foot + 2 dark recessed forklift/drainage scupper channels
      Part(0, [0, 0, -0.385], [0.94, 0.30, 0.055], [0, 0, 0], PAL.concreteBase),
      Part(0, [-0.46, 0, -0.422], [0.11, 0.308, 0.018], [0, 0, 0], PAL.rubberDark),
      Part(0, [0.46, 0, -0.422], [0.11, 0.308, 0.018], [0, 0, 0], PAL.rubberDark),
      // Authentic ±28° sloped lower vehicle-redirection flare skirts transitioning smoothly from 60cm foot to 22cm upper stem!
      Part(0, [0, -0.155, -0.225], [0.94, 0.095, 0.145], [-28, 0, 0], PAL.concreteWall),
      Part(0, [0, 0.155, -0.225], [0.94, 0.095, 0.145], [28, 0, 0], PAL.concreteWall),
      Part(0, [0, 0, -0.215], [0.938, 0.135, 0.135], [0, 0, 0], PAL.concreteWall),
      // Tapered upper concrete stem wall + chamfered top cap ridge
      Part(0, [0, 0, 0.11], [0.94, 0.108, 0.265], [0, 0, 0], PAL.concreteWall),
      Part(0, [0, 0, 0.395], [0.94, 0.088, 0.030], [0, 0, 0], PAL.concreteCap),
      // Galvanized steel interlocking male/female end loops + top safety retro-reflector tab
      Part(4, [-0.955, 0, 0.04], [0.052, 0.052, 0.012], [90, 0, 0], PAL.galvanized),
      Part(2, [0.965, 0, 0.04], [0.022, 0.022, 0.11], [0, 0, 0], PAL.galvanized),
      Part(0, [0, 0, 0.432], [0.055, 0.014, 0.018], [0, 0, 0], PAL.safetyOrange),
    ],
  },
  {
    PropId: "tecpro_block",
    Name: "Tecpro Impact Barrier Pair",
    Category: "Barriers & Paddock",
    Panel: "geometry",
    Icon: "editor-cube",
    HalfHeightZ: 0.46,
    NominalScale: [1, 1, 1],
    Description: "Interlocking FIA crimson & white polyethylene high-speed Tecpro crash barrier cells with cylindrical hinge knuckles & straps",
    BuildParts: () => [
      // Left Cell (-X): FIA Crimson molded energy-absorbing polyethylene module + rounded vertical end hinge knuckle
      Part(0, [-0.41, 0, 0.0], [0.38, 0.26, 0.44], [0, 0, 0], PAL.fiaRed),
      Part(0, [-0.41, 0, -0.38], [0.395, 0.28, 0.06], [0, 0, 0], PAL.fiaRed),
      Part(0, [-0.41, 0, 0.38], [0.395, 0.28, 0.06], [0, 0, 0], PAL.fiaRed),
      Part(2, [-0.79, 0, 0.0], [0.24, 0.24, 0.435], [0, 0, 0], PAL.fiaRed),
      // Right Cell (+X): Pure White interlocking polyethylene module + rounded vertical end hinge knuckle
      Part(0, [0.41, 0, 0.0], [0.38, 0.26, 0.44], [0, 0, 0], PAL.tecproWhite),
      Part(0, [0.41, 0, -0.38], [0.395, 0.28, 0.06], [0, 0, 0], PAL.tecproWhite),
      Part(0, [0.41, 0, 0.38], [0.395, 0.28, 0.06], [0, 0, 0], PAL.tecproWhite),
      Part(2, [0.79, 0, 0.0], [0.24, 0.24, 0.435], [0, 0, 0], PAL.tecproWhite),
      // Central interlocking cylindrical knuckle joint + steel drop pin
      Part(2, [0.0, 0, 0.0], [0.16, 0.16, 0.445], [0, 0, 0], PAL.fiaRed),
      Part(2, [0.0, 0, 0.0], [0.032, 0.032, 0.46], [0, 0, 0], PAL.galvanized),
      // Twin recessed matte-black heavy nylon retention straps wrapping horizontally across both cells
      Part(0, [0, 0, 0.14], [0.84, 0.272, 0.036], [0, 0, 0], PAL.rubberDark),
      Part(0, [0, 0, -0.14], [0.84, 0.272, 0.036], [0, 0, 0], PAL.rubberDark),
    ],
  },
  {
    PropId: "armco_barrier",
    Name: "Armco W-Beam Guardrail",
    Category: "Barriers & Paddock",
    Panel: "geometry",
    Icon: "editor-cube",
    HalfHeightZ: 0.44,
    NominalScale: [1, 1, 1],
    Description: "Proving-ground double-corrugated galvanized steel W-Beam Armco circuit guardrail on 3 heavy steel I-posts",
    BuildParts: () => [
      // 3 heavy steel I-posts + bolted floor baseplates + stand-off blockouts
      ...[-0.82, 0.0, 0.82].flatMap((X) => [
        Part(0, [X, 0.09, -0.422], [0.095, 0.095, 0.018], [0, 0, 0], PAL.steelPost),
        Part(0, [X, 0.09, 0.0], [0.038, 0.052, 0.43], [0, 0, 0], PAL.steelPost),
        Part(0, [X, -0.005, 0.12], [0.042, 0.050, 0.22], [0, 0, 0], PAL.steelPost),
      ]),
      // Upper corrugated W-Beam rail (upper wave crest + recessed bolt valley + lower wave crest)
      Part(0, [0, -0.072, 0.305], [1.02, 0.022, 0.042], [-10, 0, 0], PAL.galvanized),
      Part(0, [0, -0.052, 0.240], [1.02, 0.016, 0.034], [0, 0, 0], PAL.machinedSilver),
      Part(0, [0, -0.072, 0.175], [1.02, 0.022, 0.042], [10, 0, 0], PAL.galvanized),
      // Lower corrugated W-Beam motorsport anti-underride rail
      Part(0, [0, -0.072, 0.045], [1.02, 0.022, 0.040], [-10, 0, 0], PAL.galvanized),
      Part(0, [0, -0.052, -0.018], [1.02, 0.016, 0.032], [0, 0, 0], PAL.machinedSilver),
      Part(0, [0, -0.072, -0.080], [1.02, 0.022, 0.040], [10, 0, 0], PAL.galvanized),
    ],
  },
  {
    PropId: "rumble_curb",
    Name: "FIA Sawtooth Apex Kerb",
    Category: "Track & Cones",
    Panel: "geometry",
    Icon: "editor-cube",
    HalfHeightZ: 0.095,
    NominalScale: [1, 1, 1],
    Description: "6-stone physical 3D bevelled FIA red & white sawtooth cornering rumble kerb with rear asphalt run-off apron",
    BuildParts: () => [
      // Rear dark asphalt run-off apron behind the kerb stones
      Part(0, [0, 0.25, -0.045], [1.02, 0.13, 0.048], [0, 0, 0], PAL.apronAsphalt),
      // 6 alternating FIA Red & White physical 3D sawtooth kerb stones (flat nose plinth + pitched sawtooth ramp in matching colour!)
      ...[0, 1, 2, 3, 4, 5].flatMap((Idx) => {
        const X = -0.845 + Idx * 0.338;
        const MatCol = Idx % 2 === 0 ? PAL.kerbRed : PAL.kerbWhite;
        return [
          Part(0, [X, -0.05, -0.064], [0.168, 0.27, 0.028], [0, 0, 0], MatCol),
          Part(0, [X, -0.04, 0.002], [0.166, 0.265, 0.046], [-13, 6.2, 0], MatCol),
        ];
      }),
    ],
  },
  {
    PropId: "pit_board",
    Name: "Brake Distance 100m Board",
    Category: "Track & Cones",
    Panel: "geometry",
    Icon: "editor-cube",
    HalfHeightZ: 0.56,
    NominalScale: [1, 1, 1],
    Description: "Trackside A-frame 100m brake marker board with high-contrast face graphic & sandbag ballast",
    BuildParts: () => [
      StrutPart([-0.38, -0.22, -0.55], [-0.36, -0.02, 0.48], 0.018, PAL.galvanized),
      StrutPart([0.38, -0.22, -0.55], [0.36, -0.02, 0.48], 0.018, PAL.galvanized),
      StrutPart([-0.38, 0.24, -0.55], [-0.36, -0.02, 0.48], 0.018, PAL.galvanized),
      StrutPart([0.38, 0.24, -0.55], [0.36, -0.02, 0.48], 0.018, PAL.galvanized),
      Part(0, [0, 0.16, -0.50], [0.34, 0.09, 0.055], [0, 0, 0], PAL.safetyOrange),
      Part(0, [0, -0.08, 0.12], [0.42, 0.022, 0.32], [-10, 0, 0], PAL.whiteCoat, {
        "-Y": { Fill: "#f8fafc", Roughness: 0.28, Pattern: "brake_100m", Accent: "#dc2626" },
        "+Y": { Fill: "#334155", Roughness: 0.48, Metallic: 0.65 },
        "-X": { Fill: "#dc2626", Roughness: 0.35 },
        "+X": { Fill: "#dc2626", Roughness: 0.35 },
        "+Z": { Fill: "#dc2626", Roughness: 0.35 },
        "-Z": { Fill: "#1e293b", Roughness: 0.5 },
      }),
    ],
  },
  {
    PropId: "sand_barrel",
    Name: "Fitch Inertial Sand Barrel",
    Category: "Barriers & Paddock",
    Panel: "geometry",
    Icon: "editor-cylinder",
    HalfHeightZ: 0.46,
    NominalScale: [1, 1, 1],
    Description: "Highway & pit-entry Fitch crash attenuator sand barrel · flat safety-yellow molded poly drum, 4 hoop ribs & black snap lid",
    BuildParts: () => [
      // Molded black recycled-rubber base plinth
      Part(2, [0, 0, -0.425], [0.355, 0.355, 0.035], [0, 0, 0], PAL.rubberDark),
      // Clean flat safety-yellow HDPE barrel body (stepped lower + upper sections, zero noisy texture maps!)
      Part(2, [0, 0, -0.12], [0.335, 0.335, 0.275], [0, 0, 0], PAL.fitchYellow),
      Part(2, [0, 0, 0.24], [0.318, 0.318, 0.145], [0, 0, 0], PAL.fitchYellow),
      // 4 real 3D molded circumferential stiffening hoop ribs around the drum
      Part(4, [0, 0, -0.27], [0.352, 0.352, 0.018], [0, 0, 0], PAL.fitchRibYellow),
      Part(4, [0, 0, -0.09], [0.352, 0.352, 0.018], [0, 0, 0], PAL.fitchRibYellow),
      Part(4, [0, 0, 0.09], [0.348, 0.348, 0.018], [0, 0, 0], PAL.fitchRibYellow),
      Part(4, [0, 0, 0.26], [0.335, 0.335, 0.016], [0, 0, 0], PAL.fitchRibYellow),
      // Recessed flat matte-black snap-on weather lid + central fill bung + side lift handles
      Part(2, [0, 0, 0.405], [0.336, 0.336, 0.032], [0, 0, 0], PAL.rubberDark),
      Part(2, [0, 0, 0.444], [0.075, 0.075, 0.015], [0, 0, 0], PAL.anodizedBlack),
      Part(0, [-0.345, 0, 0.20], [0.018, 0.055, 0.016], [0, 0, 0], PAL.rubberDark),
      Part(0, [0.345, 0, 0.20], [0.018, 0.055, 0.016], [0, 0, 0], PAL.rubberDark),
    ],
  },
  {
    PropId: "toolbox_chest",
    Name: "Paddock Rolling Tool Chest",
    Category: "Barriers & Paddock",
    Panel: "geometry",
    Icon: "editor-cube",
    HalfHeightZ: 0.48,
    NominalScale: [1, 1, 1],
    Description: "FIA red mechanic rolling cabinet with 3D chrome drawer pulls, rubber top workmat & swivel casters",
    BuildParts: () => [
      Part(2, [-0.36, -0.18, -0.42], [0.055, 0.055, 0.03], [0, 90, 0], PAL.rubberDark),
      Part(2, [0.36, -0.18, -0.42], [0.055, 0.055, 0.03], [0, 90, 0], PAL.rubberDark),
      Part(2, [-0.36, 0.18, -0.42], [0.055, 0.055, 0.03], [0, 90, 0], PAL.rubberDark),
      Part(2, [0.36, 0.18, -0.42], [0.055, 0.055, 0.03], [0, 90, 0], PAL.rubberDark),
      Part(0, [0, 0, 0.03], [0.44, 0.25, 0.40], [0, 0, 0], PAL.fiaRed),
      Part(0, [0, 0, 0.442], [0.445, 0.255, 0.014], [0, 0, 0], PAL.rubberDark),
      // 5 real 3D brushed-chrome horizontal drawer pull bars on -Y front
      ...[-0.26, -0.12, 0.02, 0.16, 0.30].map((Z) =>
        Part(0, [0, -0.258, Z], [0.36, 0.010, 0.012], [0, 0, 0], PAL.chrome),
      ),
      Part(2, [0.48, 0, 0.34], [0.014, 0.014, 0.19], [90, 0, 0], PAL.chrome),
    ],
  },
  {
    PropId: "fuel_drum",
    Name: "200L Racing Fuel Drum",
    Category: "Barriers & Paddock",
    Panel: "geometry",
    Icon: "editor-cylinder",
    HalfHeightZ: 0.45,
    NominalScale: [1, 1, 1],
    Description: "200-liter steel motorsport race fuel drum in blue/white/red livery with rolling hoops & bung caps",
    BuildParts: () => [
      Part(
        2,
        [0, 0, 0],
        [0.29, 0.29, 0.44],
        [0, 0, 0],
        Mat("#1d4ed8", 0.28, 0.65, {
          GradientAxis: "V",
          GradientStops: [
            { Id: "b0", Position: 0.0, Value: "#1e3a8a" },
            { Id: "b1", Position: 0.30, Value: "#1d4ed8" },
            { Id: "w1", Position: 0.32, Value: "#f8fafc" },
            { Id: "w2", Position: 0.66, Value: "#f8fafc" },
            { Id: "r1", Position: 0.68, Value: "#dc2626" },
            { Id: "r2", Position: 1.0, Value: "#b91c1c" },
          ],
        }),
      ),
      Part(4, [0, 0, -0.14], [0.298, 0.298, 0.016], [0, 0, 0], PAL.chrome),
      Part(4, [0, 0, 0.14], [0.298, 0.298, 0.016], [0, 0, 0], PAL.chrome),
      Part(2, [0.14, 0.06, 0.45], [0.032, 0.032, 0.015], [0, 0, 0], PAL.chrome),
    ],
  },
];

export function GetWorldPropById(PropId) {
  if (!PropId) return null;
  return WORLD_PROP_CATALOG.find((Item) => Item.PropId === PropId) || null;
}

export function IsFanProp(Row, Rec = {}) {
  const Def = GetWorldPropById(Row?.PropId || Rec?.PropId);
  return Boolean(Def?.IsFan || Def?.Category === "Fans & Wind");
}

// Expand a World Prop into world-space analytical primitives sharing OwnerId = Row.Id
export function ExpandPropPrimitives(Row, Rec = {}, PickCode = 1) {
  const Def = GetWorldPropById(Row?.PropId || Rec?.PropId);
  if (!Def) return [];

  const ActorPos = Array.isArray(Rec.Position)
    ? [Number(Rec.Position[0]) || 0, Number(Rec.Position[1]) || 0, Number(Rec.Position[2]) || 0]
    : [0, 0.4, 0];
  const ActorRotDeg = Array.isArray(Rec.Rotation)
    ? [Number(Rec.Rotation[0]) || 0, Number(Rec.Rotation[1]) || 0, Number(Rec.Rotation[2]) || 0]
    : [0, 0, 0];
  const RawScale = Array.isArray(Rec.Scale) ? Rec.Scale : Def.NominalScale || [1, 1, 1];
  const BaseNom = Def.NominalScale || [1, 1, 1];
  const Sx = Math.max(0.08, Math.abs(Number(RawScale[0]) || 1) / Math.max(1e-4, BaseNom[0]));
  const Sy = Math.max(0.08, Math.abs(Number(RawScale[1]) || 1) / Math.max(1e-4, BaseNom[1]));
  const Sz = Math.max(0.08, Math.abs(Number(RawScale[2]) || 1) / Math.max(1e-4, BaseNom[2]));
  const UniformS = (Sx + Sy + Sz) / 3;

  const MActor = EulerDegToMat3(ActorRotDeg);
  const RawParts = Def.BuildParts(Rec);
  const UserFaceOverride = HasActiveFaceColors(Rec.FaceColors) ? Rec.FaceColors : null;
  const UserCustomMat = Rec.Material ? ResolveMaterial(Rec) : null;

  const Out = [];
  for (let I = 0; I < RawParts.length; ++I) {
    const P = RawParts[I];
    const ScaledOffset = [
      P.LocalOffset[0] * Sx,
      P.LocalOffset[1] * Sy,
      P.LocalOffset[2] * Sz,
    ];
    const WorldOff = TransformVec3ByMat3(MActor, ScaledOffset);
    const Center = [
      ActorPos[0] + WorldOff[0],
      ActorPos[1] + WorldOff[1],
      ActorPos[2] + WorldOff[2],
    ];
    const MLocal = EulerDegToMat3(P.LocalRotDeg);
    const MWorld = MulMat3(MActor, MLocal);
    const RotationRad = Mat3ToEulerRad(MWorld);

    const HalfExtents =
      P.Shape === 1 || P.Shape === 4
        ? [
            Math.max(0.005, P.HalfExtents[0] * UniformS),
            Math.max(0.005, P.HalfExtents[1] * UniformS),
            Math.max(0.005, P.HalfExtents[2] * UniformS),
          ]
        : [
            Math.max(0.005, P.HalfExtents[0] * Sx),
            Math.max(0.005, P.HalfExtents[1] * Sy),
            Math.max(0.005, P.HalfExtents[2] * Sz),
          ];

    const ResolvedMat =
      I === 0 && UserCustomMat ? UserCustomMat : ResolveMaterial(P.MaterialRec);
    const Ch = ResolvedMat.Channels;
    const FaceColors = I === 0 && UserFaceOverride ? UserFaceOverride : P.FaceColors;
    const UseFaceAtlas = HasActiveFaceColors(FaceColors);
    const Metallic = Math.max(0, Math.min(1, Number(Ch.metallic?.Fill ?? 0)));
    const Roughness = Math.max(0.02, Math.min(1, Number(Ch.roughness?.Fill ?? 0.38)));
    const Emission = Math.max(0, Number(Ch.emission?.Fill ?? 0));

    Out.push({
      Id: `${Row.Id}__part_${I}`,
      OwnerId: Row.Id,
      PickCode,
      Name: `${Row.Name} (${I + 1})`,
      Shape: P.Shape,
      Center,
      RotationRad,
      HalfExtents,
      CastShadows: (Rec["Cast shadows"] ?? P.CastShadows) ? 1 : 0,
      GiContributor: (Rec.GI ?? P.GiContributor) ? 1 : 0,
      GlassShell: false,
      FaceAtlas: UseFaceAtlas,
      FaceColors: UseFaceAtlas ? FaceColors : null,
      ResolvedMaterial: ResolvedMat,
      MaterialType: ResolvedMat.Type,
      BaseColor: [0.65, 0.65, 0.65],
      Metallic,
      Roughness,
      DiffuseRoughness: Roughness * 0.85,
      Anisotropy: 0,
      CoatWeight: Number(Ch.coat?.Fill ?? 0),
      CoatRoughness: Number(Ch.coatRoughness?.Fill ?? 0.08),
      Opacity: 1,
      Displacement: 0,
      Transmission: 0,
      Ior: 1.5,
      Emission,
      EdgeTintF82: [1, 1, 1],
      Abbe: 0,
      Attenuation: [1, 1, 1],
    });
  }
  return Out;
}

// Compute tight world-space AABB for any compiled primitive array
export function ComputePrimitivesTightBounds(Primitives) {
  let MinX = Infinity, MinY = Infinity, MinZ = Infinity;
  let MaxX = -Infinity, MaxY = -Infinity, MaxZ = -Infinity;
  for (const P of Primitives) {
    const [Rx, Ry, Rz] = P.RotationRad || [0, 0, 0];
    const Cx = Math.cos(Rx), Sx = Math.sin(Rx);
    const Cy = Math.cos(Ry), Sy = Math.sin(Ry);
    const Cz = Math.cos(Rz), Sz = Math.sin(Rz);
    const M00 = Cz * Cy, M01 = Cz * Sy * Sx - Sz * Cx, M02 = Cz * Sy * Cx + Sz * Sx;
    const M10 = Sz * Cy, M11 = Sz * Sy * Sx + Cz * Cx, M12 = Sz * Sy * Cx - Cz * Sx;
    const M20 = -Sy,     M21 = Cy * Sx,                M22 = Cy * Cx;
    const Hx = P.HalfExtents[0], Hy = P.HalfExtents[1], Hz = P.HalfExtents[2];
    const Ex = Math.abs(M00) * Hx + Math.abs(M01) * Hy + Math.abs(M02) * Hz;
    const Ey = Math.abs(M10) * Hx + Math.abs(M11) * Hy + Math.abs(M12) * Hz;
    const Ez = Math.abs(M20) * Hx + Math.abs(M21) * Hy + Math.abs(M22) * Hz;
    MinX = Math.min(MinX, P.Center[0] - Ex);
    MaxX = Math.max(MaxX, P.Center[0] + Ex);
    MinY = Math.min(MinY, P.Center[1] - Ey);
    MaxY = Math.max(MaxY, P.Center[1] + Ey);
    MinZ = Math.min(MinZ, P.Center[2] - Ez);
    MaxZ = Math.max(MaxZ, P.Center[2] + Ez);
  }
  if (!Number.isFinite(MinX)) {
    return {
      Min: [-0.5, -0.5, -0.5],
      Max: [0.5, 0.5, 0.5],
      Center: [0, 0, 0],
      Size: [1, 1, 1],
      Radius: 0.866,
    };
  }
  const Center = [(MinX + MaxX) * 0.5, (MinY + MaxY) * 0.5, (MinZ + MaxZ) * 0.5];
  const Size = [Math.max(0.02, MaxX - MinX), Math.max(0.02, MaxY - MinY), Math.max(0.02, MaxZ - MinZ)];
  const Radius = Math.max(0.03, 0.5 * Math.hypot(Size[0], Size[1], Size[2]));
  return { Min: [MinX, MinY, MinZ], Max: [MaxX, MaxY, MaxZ], Center, Size, Radius };
}
