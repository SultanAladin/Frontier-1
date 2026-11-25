// Frontier Radiometric Lighting Architecture Lab — Core Photometry, Threaded Conty-Kulla Partition & Deferred WebGL2 Runtime
import {
  FullscreenVertexShader,
  PrimaryGbufferFragmentShader,
  UnifiedPartitionFragmentShader,
  ClusteredFroxelFragmentShader,
  RestirDiFragmentShader,
  PureSdfVoxelFragmentShader,
  DisplayToneFragmentShader,
} from "./LightingLabShaders.js";

export const LuminaireClassNames = [
  "PointSphere",
  "AnalyticSpot",
  "PhotometricIes",
  "AutomotiveEce",
  "PolygonalAreaLtc",
  "LinearTubeSegment",
  "EmissiveSurface",
  "EnvironmentPortal",
];

export const IesProfileCatalog = [
  { id: 0, name: "Barn-Door Studio Fresnel", peakCd: 18500, beamDeg: 46 },
  { id: 1, name: "ECE R112 Low-Beam (-1.0° Kink)", peakCd: 26400, beamDeg: 52 },
  { id: 2, name: "Automotive High-Beam Pencil", peakCd: 48000, beamDeg: 24 },
  { id: 3, name: "Architectural Batwing Downlight", peakCd: 6200, beamDeg: 96 },
  { id: 4, name: "Automotive Red Taillamp / Fog", peakCd: 3400, beamDeg: 78 },
  { id: 5, name: "Focused Studio Octa Strobe", peakCd: 14500, beamDeg: 42 },
];

export const PaintPresets = [
  { name: "Liquid Silver", hex: "#bfc6d0", rgb: [0.72, 0.75, 0.80], metallic: 0.90, roughness: 0.17 },
  { name: "Signal Orange", hex: "#ef5218", rgb: [0.88, 0.19, 0.04], metallic: 0.42, roughness: 0.19 },
  { name: "Forest Sage", hex: "#3d5e48", rgb: [0.16, 0.30, 0.20], metallic: 0.72, roughness: 0.20 },
  { name: "Midnight Cobalt", hex: "#1f4594", rgb: [0.08, 0.20, 0.56], metallic: 0.82, roughness: 0.18 },
  { name: "Obsidian Pearl", hex: "#1b1d22", rgb: [0.055, 0.06, 0.075], metallic: 0.85, roughness: 0.15 },
];

export function KelvinToLinearRgb(kelvin) {
  const t = Math.max(18, Math.min(120, kelvin / 100));
  let r, g, b;
  if (t <= 66) {
    r = 1.0;
    g = Math.max(0, Math.min(1, (99.4708 * Math.log(t) - 161.1196) / 255));
    b = t <= 19 ? 0.0 : Math.max(0, Math.min(1, (138.5177 * Math.log(t - 10) - 305.0448) / 255));
  } else {
    r = Math.max(0, Math.min(1, (329.6987 * Math.pow(t - 60, -0.1332)) / 255));
    g = Math.max(0, Math.min(1, (288.1222 * Math.pow(t - 60, -0.0755)) / 255));
    b = 1.0;
  }
  return [Math.pow(r, 2.2), Math.pow(g, 2.2), Math.pow(b, 2.2)];
}

export function EvaluateNormalizedIesCandela(profileId, polarRad, azimRad) {
  const deg = (polarRad * 180) / Math.PI;
  const sinA = Math.sin(azimRad);
  const cosA = Math.cos(azimRad);

  if (profileId === 0) {
    if (deg > 68) return 0;
    const core = Math.exp(-Math.pow(deg / 24, 2.6));
    const lens = 0.22 * Math.exp(-Math.pow((deg - 16) / 11, 2));
    return Math.min(1, (core + lens) / 1.08);
  }
  if (profileId === 1) {
    if (deg > 62) return 0;
    const horizSpread = 1.0 + 0.65 * Math.abs(sinA);
    const effDeg = deg / horizSpread;
    const hot = Math.exp(-Math.pow(effDeg / 14.5, 2.2));
    const kerbBoost = sinA > 0 ? 0.28 * Math.exp(-Math.pow((deg - 9) / 8, 2)) * sinA : 0;
    const cut = cosA > 0.15 && deg > 4 ? 0.35 : 1.0;
    return Math.min(1, (hot + kerbBoost) * cut);
  }
  if (profileId === 2) {
    if (deg > 48) return 0;
    return Math.min(1, 0.85 * Math.exp(-Math.pow(deg / 8.5, 2.0)) + 0.18 * Math.exp(-Math.pow(deg / 22.0, 2.0)));
  }
  if (profileId === 3) {
    if (deg > 84) return 0;
    const nadir = 0.52 * Math.cos((deg * Math.PI) / 170);
    const wing = 0.68 * Math.exp(-Math.pow((deg - 34) / 14, 2.0));
    return Math.min(1, Math.max(0, nadir + wing));
  }
  if (profileId === 4) {
    if (deg > 78) return 0;
    const hFactor = 0.55 + 0.65 * Math.abs(sinA);
    return Math.min(1, Math.exp(-Math.pow(deg / (26 * hFactor), 2.1)));
  }
  if (deg > 64) return 0;
  return Math.exp(-Math.pow(deg / 21.5, 2.4));
}

export function BuildPhotometricAtlasFloatData() {
  const W = 128;
  const H = 32 * 6;
  const data = new Float32Array(W * H * 4);
  for (let p = 0; p < 6; ++p) {
    for (let az = 0; az < 32; ++az) {
      const azRad = (az / 32) * Math.PI * 2.0;
      const row = p * 32 + az;
      for (let pol = 0; pol < 128; ++pol) {
        const polRad = (pol / 127) * Math.PI;
        const val = EvaluateNormalizedIesCandela(p, polRad, azRad);
        const idx = (row * W + pol) * 4;
        data[idx] = val;
        data[idx + 1] = val;
        data[idx + 2] = val;
        data[idx + 3] = 1.0;
      }
    }
  }
  return { width: W, height: H, data };
}

export function BuildHeitzLtcFloatData() {
  const N = 64;
  const data = new Float32Array(N * N * 4);
  for (let y = 0; y < N; ++y) {
    const sqrtOneMinusCos = y / (N - 1);
    const cosTheta = Math.max(0.02, 1.0 - sqrtOneMinusCos * sqrtOneMinusCos);
    const sinTheta = Math.sqrt(Math.max(0.0, 1.0 - cosTheta * cosTheta));
    for (let x = 0; x < N; ++x) {
      const rough = Math.max(0.02, x / (N - 1));
      const alpha = rough * rough;
      const invScale = 1.0 / Math.max(0.035, alpha * (0.82 + 0.18 * cosTheta) + 0.015);
      const offDiag = -sinTheta * (1.0 - alpha) * 0.72 * Math.min(6.0, invScale * 0.35);
      const skewZ = -sinTheta * (1.0 - alpha) * 0.38;
      const normAmp = 1.0 - 0.18 * rough * (1.0 - cosTheta);
      const o = (y * N + x) * 4;
      data[o + 0] = Math.min(28.0, invScale);
      data[o + 1] = offDiag;
      data[o + 2] = skewZ;
      data[o + 3] = normAmp;
    }
  }
  return { width: N, height: N, data };
}

function Vec3Sub(a, b) { return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]; }
function Vec3Normalize(v) {
  const l = Math.hypot(v[0], v[1], v[2]) || 1.0;
  return [v[0] / l, v[1] / l, v[2] / l];
}
function Vec3Cross(a, b) {
  return [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
  ];
}

function AimBasis(fromPos, targetPos) {
  const fwd = Vec3Normalize(Vec3Sub(targetPos, fromPos));
  const worldUp = Math.abs(fwd[1]) > 0.94 ? [0, 0, 1] : [0, 1, 0];
  const right = Vec3Normalize(Vec3Cross(worldUp, fwd));
  const up = Vec3Normalize(Vec3Cross(fwd, right));
  return { fwd, right, up };
}

export function ConstructSceneLuminaires(sceneId, lightDensity = 0) {
  const list = [];
  const add = (spec) => list.push({ enabled: true, castShadow: true, barnFlareDeg: 34, ...spec });
  const heroAim = sceneId === 1 ? [0.0, 0.78, 0.0] : [0.0, 0.56, 0.0];

  if (sceneId === 0 || sceneId === 1) {
    const isForest = sceneId === 1;
    add({
      id: "light-overhead-softbox",
      name: "Overhead Silk Softbox",
      group: "Studio Rigs",
      kind: 4,
      pos: [0.0, 4.06, -0.25],
      rightAxis: [1, 0, 0],
      upAxis: [0, 0, 1],
      halfW: 3.6,
      halfH: 2.0,
      kelvin: isForest ? 5200 : 5600,
      intensity: isForest ? 1.85 : 2.35,
      range: 22.0,
      aux: 0.0,
    });

    const keyPos = isForest ? [-4.45, 2.68, 1.55] : [-3.55, 2.28, 1.55];
    const keyBasis = AimBasis(keyPos, heroAim);
    add({
      id: "light-key-fresnel",
      name: isForest ? "Woodland Sun Fresnel (IES)" : "Barn-Door Key Fresnel (IES)",
      group: "Studio Rigs",
      kind: 2,
      pos: keyPos,
      rightAxis: keyBasis.fwd,
      upAxis: keyBasis.up,
      halfW: ((isForest ? 42 : 34) * Math.PI) / 180,
      halfH: 0.22,
      kelvin: isForest ? 3450 : 5400,
      intensity: isForest ? 26.0 : 21.0,
      range: 24.0,
      aux: 0,
    });

    const umbPos = isForest ? [4.45, 2.32, 2.05] : [3.35, 2.12, 1.85];
    const umbBasis = AimBasis(umbPos, heroAim);
    add({
      id: "light-umbrella-fill",
      name: "Parabolic Bounce Umbrella",
      group: "Studio Rigs",
      kind: 4,
      pos: umbPos,
      rightAxis: umbBasis.right,
      upAxis: Vec3Normalize(Vec3Cross(umbBasis.right, umbBasis.fwd)),
      halfW: 0.82,
      halfH: 0.82,
      kelvin: 5800,
      intensity: 2.8,
      range: 18.0,
      castShadow: false,
      aux: 0.0,
    });

    const rimPos = [-4.15, 2.52, -2.55];
    const rimBasis = AimBasis(rimPos, heroAim);
    add({
      id: "light-rim-panel",
      name: "Rim Contour LED Panel",
      group: "Studio Rigs",
      kind: 4,
      pos: rimPos,
      rightAxis: rimBasis.right,
      upAxis: Vec3Normalize(Vec3Cross(rimBasis.right, rimBasis.fwd)),
      halfW: 0.62,
      halfH: 0.36,
      kelvin: 6400,
      intensity: 4.8,
      range: 18.0,
      castShadow: false,
      aux: 0.0,
    });

    const accPos = [4.35, 2.38, -2.35];
    const accBasis = AimBasis(accPos, heroAim);
    add({
      id: "light-octa-accent",
      name: "Octa Rim Strobe (IES)",
      group: "Studio Rigs",
      kind: 2,
      pos: accPos,
      rightAxis: accBasis.fwd,
      upAxis: accBasis.up,
      halfW: (40 * Math.PI) / 180,
      halfH: 0.24,
      kelvin: isForest ? 3300 : 5600,
      intensity: 16.5,
      range: 20.0,
      castShadow: false,
      aux: 5,
    });

    if (isForest) {
      for (let seg = -1; seg <= 1; ++seg) {
        const ang = seg * 0.44;
        const px = Math.sin(ang) * 8.45;
        const pz = 3.6 - Math.cos(ang) * 8.45;
        const panelBasis = AimBasis([px, 2.3, pz], [0, 1.2, 0]);
        add({
          id: `led-volume-seg-${seg + 2}`,
          name: `Forest LED Volume Bay ${seg + 2}`,
          group: "Emissive Surfaces",
          kind: 6,
          pos: [px, 2.35, pz],
          rightAxis: panelBasis.right,
          upAxis: Vec3Normalize(Vec3Cross(panelBasis.right, panelBasis.fwd)),
          halfW: 1.85,
          halfH: 2.15,
          kelvin: 5300,
          tintRgb: [0.62, 0.92, 0.46],
          intensity: 1.55,
          range: 22.0,
          castShadow: false,
          aux: 0.0,
        });
      }
    }
  } else {
    add({
      id: "portal-pit-archway",
      name: "Open Pit Archway Sky Portal",
      group: "Environment & Portals",
      kind: 7,
      pos: [-0.6, 1.85, -5.15],
      rightAxis: [1, 0, 0],
      upAxis: [0, 1, 0],
      halfW: 2.75,
      halfH: 1.75,
      kelvin: 6500,
      tintRgb: [0.68, 0.82, 1.0],
      intensity: 1.65,
      range: 24.0,
      castShadow: true,
      aux: 0.0,
    });

    [-2.6, 0.4, 3.6].forEach((tx, idx) => {
      add({
        id: `garage-tube-${idx + 1}`,
        name: `Ceiling Linear LED Bank ${idx + 1}`,
        group: "Linear Tube Luminaires",
        kind: 5,
        pos: [tx, 4.92, 0.15],
        rightAxis: [0, 0, 1],
        upAxis: [0, -1, 0],
        halfW: 2.35,
        halfH: 0.045,
        kelvin: 5800,
        intensity: 3.2,
        range: 18.0,
        castShadow: idx === 1,
        aux: 0.0,
      });
    });

    const bayBasis = AimBasis([5.35, 4.45, 0.85], [5.8, 0.9, 0.85]);
    add({
      id: "garage-bay-ies",
      name: "Mechanic Bay Worklamp (IES)",
      group: "Studio Rigs",
      kind: 2,
      pos: [5.35, 4.45, 0.85],
      rightAxis: bayBasis.fwd,
      upAxis: bayBasis.up,
      halfW: 0.0,
      halfH: 0.18,
      kelvin: 3400,
      intensity: 15.0,
      range: 16.0,
      castShadow: false,
      aux: 3,
    });

    add({
      id: "emissive-telemetry",
      name: "Pit Telemetry Wall Screen",
      group: "Emissive Surfaces",
      kind: 6,
      pos: [6.48, 1.72, -0.45],
      rightAxis: [0, 0, 1],
      upAxis: [0, 1, 0],
      halfW: 0.66,
      halfH: 0.40,
      kelvin: 7500,
      tintRgb: [0.22, 0.78, 1.0],
      intensity: 3.2,
      range: 11.0,
      castShadow: false,
      aux: 0.0,
    });
  }

  const carYOff = sceneId === 1 ? 0.30 : 0.0;
  for (const side of [-1, 1]) {
    const label = side < 0 ? "Left" : "Right";
    const hlPos = [side * 0.68, 0.48 + carYOff, 2.14];
    const hlFwd = Vec3Normalize([side * 0.03, -0.055, 1.0]);
    add({
      id: `car-headlamp-${label.toLowerCase()}`,
      name: `Hero ECE Low-Beam (${label})`,
      group: "Automotive Lamps",
      kind: 3,
      pos: hlPos,
      rightAxis: hlFwd,
      upAxis: [0, 1, 0],
      halfW: 0.52,
      halfH: 0.06,
      kelvin: 6000,
      intensity: 24.0,
      range: 26.0,
      castShadow: true,
      aux: 1,
    });

    add({
      id: `car-drl-${label.toLowerCase()}`,
      name: `Hero DRL Lightguide (${label})`,
      group: "Automotive Lamps",
      kind: 5,
      pos: [side * 0.68, 0.47 + carYOff, 2.08],
      rightAxis: Vec3Normalize([side * 0.85, 0.14, -0.45]),
      upAxis: [0, 1, 0],
      halfW: 0.18,
      halfH: 0.022,
      kelvin: 6500,
      intensity: 3.4,
      range: 7.5,
      castShadow: false,
      aux: 0.0,
    });
  }

  add({
    id: "car-taillamp-bar",
    name: "Hero Rear Red Taillamp Bar",
    group: "Automotive Lamps",
    kind: 5,
    pos: [0.0, 0.63 + carYOff, -2.20],
    rightAxis: [1, 0, 0],
    upAxis: [0, 1, 0],
    halfW: 0.78,
    halfH: 0.028,
    kelvin: 2200,
    tintRgb: [1.0, 0.06, 0.04],
    intensity: 4.8,
    range: 11.0,
    castShadow: false,
    aux: 0.0,
  });

  add({
    id: "car-taillamp-ies",
    name: "Hero Rear Brake IES Wash",
    group: "Automotive Lamps",
    kind: 3,
    pos: [0.0, 0.62 + carYOff, -2.24],
    rightAxis: [0, -0.14, -0.99],
    upAxis: [0, 1, 0],
    halfW: 0.8,
    halfH: 0.09,
    kelvin: 2200,
    tintRgb: [1.0, 0.05, 0.03],
    intensity: 7.5,
    range: 12.0,
    castShadow: false,
    aux: 4,
  });

  add({
    id: "point-inspection-orb",
    name: "Floor Inspection Point Lamp",
    group: "Point & Spot",
    kind: 0,
    pos: [2.25, 0.34, 2.45],
    rightAxis: [0, 1, 0],
    upAxis: [0, 0, 1],
    halfW: 0.12,
    halfH: 0.09,
    kelvin: 4100,
    intensity: 2.2,
    range: 9.0,
    castShadow: false,
    aux: 0.0,
  });

  const targetTotal = lightDensity === 2 ? 96 : lightDensity === 1 ? 40 : list.length;
  let extraIdx = 0;
  while (list.length < targetTotal) {
    const ringIdx = extraIdx++;
    const cols = 8;
    const gx = (ringIdx % cols) - (cols - 1) * 0.5;
    const gz = Math.floor(ringIdx / cols);
    const px = gx * 2.15;
    const pz = -4.2 + (gz % 6) * 2.15;
    const kindCycle = ringIdx % 4;
    if (kindCycle === 0) {
      add({
        id: `many-ies-${ringIdx}`,
        name: `Bay Downlight #${ringIdx + 1} (IES)`,
        group: "Many-Light Array",
        kind: 2,
        pos: [px, 4.75, pz],
        rightAxis: [0, -1, 0],
        upAxis: [0, 0, 1],
        halfW: 0.0,
        halfH: 0.12,
        kelvin: 4200 + (ringIdx % 5) * 450,
        intensity: 3.2,
        range: 13.5,
        castShadow: false,
        aux: 3,
      });
    } else if (kindCycle === 1) {
      const hueT = (ringIdx * 0.6180339) % 1.0;
      const tint = hueT < 0.33 ? [1.0, 0.52, 0.18] : hueT < 0.66 ? [0.25, 0.68, 1.0] : [0.95, 0.92, 0.85];
      add({
        id: `many-point-${ringIdx}`,
        name: `Perimeter Point #${ringIdx + 1}`,
        group: "Many-Light Array",
        kind: 0,
        pos: [px * 1.15, 0.55 + (ringIdx % 3) * 0.65, pz],
        rightAxis: [0, 1, 0],
        upAxis: [0, 0, 1],
        halfW: 0.08,
        halfH: 0.08,
        kelvin: 5000,
        tintRgb: tint,
        intensity: 1.35,
        range: 9.5,
        castShadow: false,
        aux: 0,
      });
    } else if (kindCycle === 2) {
      add({
        id: `many-tube-${ringIdx}`,
        name: `Truss LED Tube #${ringIdx + 1}`,
        group: "Many-Light Array",
        kind: 5,
        pos: [px, 3.85, pz],
        rightAxis: [1, 0, 0],
        upAxis: [0, -1, 0],
        halfW: 0.55,
        halfH: 0.025,
        kelvin: 5600,
        intensity: 1.25,
        range: 10.5,
        castShadow: false,
        aux: 0,
      });
    } else {
      const spPos = [px * 1.1, 3.4, pz];
      const spBasis = AimBasis(spPos, [px * 0.25, 0.3, pz * 0.35]);
      add({
        id: `many-spot-${ringIdx}`,
        name: `Track Pin-Spot #${ringIdx + 1}`,
        group: "Many-Light Array",
        kind: 1,
        pos: spPos,
        rightAxis: spBasis.fwd,
        upAxis: spBasis.up,
        halfW: (28 * Math.PI) / 180,
        halfH: 0.09,
        kelvin: 3600 + (ringIdx % 4) * 600,
        intensity: 4.2,
        range: 14.0,
        castShadow: false,
        aux: 0,
      });
    }
  }

  return list;
}

export function ComputeLuminaireLinearRadiance(L) {
  const kRgb = L.tintRgb || KelvinToLinearRgb(L.kelvin || 5600);
  return [kRgb[0] * L.intensity, kRgb[1] * L.intensity, kRgb[2] * L.intensity];
}

function LuminaireDirectionAndSpread(L) {
  if (L.kind === 0 || L.kind === 5) {
    return { axis: [0, -1, 0], cosHalfSpread: -1.0 };
  }
  if (L.kind === 4 || L.kind === 6 || L.kind === 7) {
    const fwd = Vec3Normalize(Vec3Cross(L.rightAxis, L.upAxis));
    return { axis: fwd, cosHalfSpread: L.aux < -0.5 ? -1.0 : 0.0 };
  }
  return { axis: Vec3Normalize(L.rightAxis), cosHalfSpread: Math.cos(Math.min(Math.PI, Math.max(0.35, L.halfW || 1.05))) };
}

// Pre-order Threaded Spatial-Orientation Binary Importance Partition Builder
// Emits exact missSkip = subtreeEndIndex so GPU traverses with ZERO stack arrays!
export function BuildPhotometricLightPartition(activeLuminaires) {
  const count = activeLuminaires.length;
  if (count === 0) {
    return { nodes: [], rootIndex: -1, totalPower: 0 };
  }

  const leaves = activeLuminaires.map((L, gpuIndex) => {
    const rad = ComputeLuminaireLinearRadiance(L);
    const lumWeight = (rad[0] + rad[1] + rad[2]) / 3.0;
    const areaFactor = L.kind === 4 || L.kind === 6 || L.kind === 7
      ? Math.max(0.5, 4.0 * L.halfW * L.halfH * 0.35)
      : L.kind === 5 ? Math.max(0.35, 2.0 * L.halfW * 0.5) : 0.45;
    const power = Math.max(0.01, lumWeight * areaFactor);
    const pad = L.kind === 4 || L.kind === 6 || L.kind === 7
      ? Math.max(L.halfW, L.halfH)
      : L.kind === 5 ? L.halfW : 0.18;
    const { axis, cosHalfSpread } = LuminaireDirectionAndSpread(L);
    return {
      gpuIndex,
      pos: L.pos,
      min: [L.pos[0] - pad, L.pos[1] - pad, L.pos[2] - pad],
      max: [L.pos[0] + pad, L.pos[1] + pad, L.pos[2] + pad],
      power,
      axis,
      cosHalfSpread,
    };
  });

  const nodes = [];

  function buildPreOrder(items) {
    const nodeIndex = nodes.length;
    nodes.push(null);

    let minX = Infinity, minY = Infinity, minZ = Infinity;
    let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
    let totalPower = 0;
    let axX = 0, axY = 0, axZ = 0;
    let bestRep = items[0].gpuIndex;
    let bestRepPow = -1;
    let anyOmni = false;

    for (const it of items) {
      minX = Math.min(minX, it.min[0]); minY = Math.min(minY, it.min[1]); minZ = Math.min(minZ, it.min[2]);
      maxX = Math.max(maxX, it.max[0]); maxY = Math.max(maxY, it.max[1]); maxZ = Math.max(maxZ, it.max[2]);
      totalPower += it.power;
      axX += it.axis[0] * it.power;
      axY += it.axis[1] * it.power;
      axZ += it.axis[2] * it.power;
      if (it.cosHalfSpread < -0.9) anyOmni = true;
      if (it.power > bestRepPow) {
        bestRepPow = it.power;
        bestRep = it.gpuIndex;
      }
    }

    const avgAxis = Vec3Normalize([axX, axY, axZ]);
    let minCos = anyOmni ? -1.0 : 1.0;
    if (!anyOmni) {
      for (const it of items) {
        const dotA = Math.max(-1, Math.min(1, avgAxis[0] * it.axis[0] + avgAxis[1] * it.axis[1] + avgAxis[2] * it.axis[2]));
        const combinedAngle = Math.acos(dotA) + Math.acos(Math.max(-1, Math.min(1, it.cosHalfSpread)));
        minCos = Math.min(minCos, combinedAngle >= Math.PI ? -1.0 : Math.cos(combinedAngle));
      }
    }

    if (items.length === 1) {
      const missSkip = nodes.length; // Next sibling/uncle in pre-order
      nodes[nodeIndex] = {
        min: [minX, minY, minZ],
        max: [maxX, maxY, maxZ],
        totalPower,
        axis: items[0].axis,
        cosHalfSpread: items[0].cosHalfSpread,
        repIndex: items[0].gpuIndex,
        isLeaf: 1,
        missSkip,
        left: -1,
        right: -1,
        leafIndex: items[0].gpuIndex,
        count: 1,
      };
      return nodeIndex;
    }

    const ext = [maxX - minX, maxY - minY, maxZ - minZ];
    let splitAxis = 0;
    if (ext[1] > ext[0] && ext[1] >= ext[2]) splitAxis = 1;
    else if (ext[2] > ext[0] && ext[2] > ext[1]) splitAxis = 2;

    const sorted = items.slice().sort((a, b) => (a.pos[splitAxis] + 0.35 * a.axis[splitAxis]) - (b.pos[splitAxis] + 0.35 * b.axis[splitAxis]));
    const mid = sorted.length >> 1;
    const leftIdx = buildPreOrder(sorted.slice(0, mid)); // Guaranteed == nodeIndex + 1!
    const rightIdx = buildPreOrder(sorted.slice(mid));
    const missSkip = nodes.length; // Index immediately after entire subtree!

    nodes[nodeIndex] = {
      min: [minX, minY, minZ],
      max: [maxX, maxY, maxZ],
      totalPower,
      axis: avgAxis,
      cosHalfSpread: minCos,
      repIndex: bestRep,
      isLeaf: 0,
      missSkip,
      left: leftIdx,
      right: rightIdx,
      leafIndex: -1,
      count: items.length,
    };
    return nodeIndex;
  }

  const rootIndex = buildPreOrder(leaves);
  return {
    nodes,
    rootIndex,
    totalPower: nodes[rootIndex].totalPower,
  };
}

// ---------------------------------------------------------------------------
// Interactive Browser Studio Initialization
// ---------------------------------------------------------------------------
export function StartRadiometricLightingStudio() {
  const canvas = document.getElementById("glCanvas");
  canvas.addEventListener("webglcontextlost", (e) => {
    e.preventDefault();
    document.getElementById("diagTitle").textContent = "WebGL2 Context Paused";
    document.getElementById("diagBody").textContent = "Waiting for browser GPU context restoration...";
  });

  const gl = canvas.getContext("webgl2", {
    antialias: false,
    depth: false,
    stencil: false,
  });
  if (!gl) {
    document.getElementById("diagBody").textContent = "WebGL2 context unavailable in this browser.";
    return;
  }

  let hasColorFloat = false;

  function compileShader(type, src, label) {
    if (gl.isContextLost()) return null;
    const sh = gl.createShader(type);
    if (!sh) return null;
    gl.shaderSource(sh, src);
    gl.compileShader(sh);
    if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
      if (gl.isContextLost()) return null;
      const log = gl.getShaderInfoLog(sh) || "Shader compile error";
      console.error(`[${label}] Shader compile error:`, log);
      throw new Error(`[${label}] Compile failed: ${log}`);
    }
    return sh;
  }

  function createProgram(vsSrc, fsSrc, label) {
    if (gl.isContextLost()) return null;
    const prog = gl.createProgram();
    const vs = compileShader(gl.VERTEX_SHADER, vsSrc, `${label}:VS`);
    const fs = compileShader(gl.FRAGMENT_SHADER, fsSrc, `${label}:FS`);
    if (!prog || !vs || !fs) return null;
    gl.attachShader(prog, vs);
    gl.attachShader(prog, fs);
    gl.linkProgram(prog);
    gl.deleteShader(vs);
    gl.deleteShader(fs);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
      if (gl.isContextLost()) return null;
      const log = gl.getProgramInfoLog(prog) || "Program link error";
      console.error(`[${label}] Program link error:`, log);
      throw new Error(`[${label}] Link failed: ${log}`);
    }
    return prog;
  }

  function makeLutTex(width, height, floatData, linearFilter) {
    if (gl.isContextLost()) return null;
    const tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_BASE_LEVEL, 0);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAX_LEVEL, 0);
    gl.texImage2D(gl.TEXTURE_2D, 0, linearFilter ? gl.RGBA16F : gl.RGBA32F, width, height, 0, gl.RGBA, gl.FLOAT, floatData);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, linearFilter ? gl.LINEAR : gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, linearFilter ? gl.LINEAR : gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    return tex;
  }

  const solverSpecs = [
    ["UnifiedPartition", UnifiedPartitionFragmentShader],
    ["ClusteredFroxels", ClusteredFroxelFragmentShader],
    ["RestirDi1Spp", RestirDiFragmentShader],
    ["PureSdfVoxel", PureSdfVoxelFragmentShader],
  ];
  const solverProgs = [null, null, null, null];
  let gbufferProg = null;
  let displayProg = null;
  let emptyVao = null;

  const iesAtlas = BuildPhotometricAtlasFloatData();
  const ltcAtlas = BuildHeitzLtcFloatData();
  const MAX_LUM = 128;
  const MAX_NODES = 256;
  const lumBuffer = new Float32Array(4 * MAX_LUM * 4);
  const partBuffer = new Float32Array(4 * MAX_NODES * 4);
  let iesTex = null, ltcTex = null, lumTex = null, partTex = null;

  // Deferred G-Buffer + Ping-Pong HDR Targets
  let fboW = 0, fboH = 0;
  let gbufFbo = null, gPosMatTex = null, gNormDepthTex = null;
  let refSplitFbo = null, refSplitTex = null;
  const pingPong = [null, null];
  let ppIdx = 0;
  let useLdrShadeTarget = false;

  function getSolverProg(idx) {
    if (!solverProgs[idx] && !gl.isContextLost()) {
      const [label, fsSrc] = solverSpecs[idx];
      solverProgs[idx] = createProgram(FullscreenVertexShader, fsSrc, label);
    }
    return solverProgs[idx];
  }

  function rebuildGpuResources() {
    if (gl.isContextLost()) return false;
    hasColorFloat = Boolean(
      gl.getExtension("EXT_color_buffer_float") || gl.getExtension("EXT_color_buffer_half_float")
    );
    gl.getExtension("OES_texture_float_linear");
    useLdrShadeTarget = !hasColorFloat;

    solverProgs.fill(null);
    fboW = 0;
    fboH = 0;
    gbufFbo = null;
    refSplitFbo = null;
    pingPong[0] = null;
    pingPong[1] = null;

    gbufferProg = createProgram(FullscreenVertexShader, PrimaryGbufferFragmentShader, "PrimaryGbuffer");
    solverProgs[0] = getSolverProg(0);
    displayProg = createProgram(FullscreenVertexShader, DisplayToneFragmentShader, "DisplayTone");
    emptyVao = gl.createVertexArray();
    iesTex = makeLutTex(iesAtlas.width, iesAtlas.height, iesAtlas.data, true);
    ltcTex = makeLutTex(ltcAtlas.width, ltcAtlas.height, ltcAtlas.data, true);
    lumTex = makeLutTex(4, MAX_LUM, lumBuffer, false);
    partTex = makeLutTex(4, MAX_NODES, partBuffer, false);
    return Boolean(gbufferProg && solverProgs[0] && displayProg);
  }

  try {
    rebuildGpuResources();
  } catch (err) {
    document.getElementById("diagTitle").textContent = "Shader Pipeline Diagnostic";
    document.getElementById("diagBody").textContent = String(err.message || err);
    return;
  }

  canvas.addEventListener("webglcontextrestored", () => {
    try {
      if (rebuildGpuResources()) {
        uploadRadiometricRegister();
        updateTelemetryHud();
        requestAnimationFrame(renderTick);
      }
    } catch (err) {
      console.error("Context restore failed:", err);
    }
  });

  function allocRenderTex(w, h, internalFmt, fmt, type, linear) {
    const tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_BASE_LEVEL, 0);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAX_LEVEL, 0);
    gl.texImage2D(gl.TEXTURE_2D, 0, internalFmt, w, h, 0, fmt, type, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, linear ? gl.LINEAR : gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, linear ? gl.LINEAR : gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    return tex;
  }

  function ensureRenderTargets(w, h) {
    if (w === fboW && h === fboH && gbufFbo && pingPong[0]) return;
    fboW = w;
    fboH = h;

    if (gbufFbo) {
      gl.deleteFramebuffer(gbufFbo);
      gl.deleteTexture(gPosMatTex);
      gl.deleteTexture(gNormDepthTex);
    }
    if (refSplitFbo) {
      gl.deleteFramebuffer(refSplitFbo);
      gl.deleteTexture(refSplitTex);
    }

    // G-Buffer MRT (RGBA16F world pos/normal; RGBA8 fallback when float color buffers are unavailable)
    const gIntFmt = hasColorFloat ? gl.RGBA16F : gl.RGBA8;
    const gType = hasColorFloat ? gl.HALF_FLOAT : gl.UNSIGNED_BYTE;
    gPosMatTex = allocRenderTex(w, h, gIntFmt, gl.RGBA, gType, false);
    gNormDepthTex = allocRenderTex(w, h, gIntFmt, gl.RGBA, gType, false);
    gbufFbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, gbufFbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, gPosMatTex, 0);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT1, gl.TEXTURE_2D, gNormDepthTex, 0);
    gl.drawBuffers([gl.COLOR_ATTACHMENT0, gl.COLOR_ATTACHMENT1]);
    if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) {
      console.error("[RadiometricLab] G-Buffer MRT incomplete; falling back to single-attachment RGBA8.");
      gl.deleteTexture(gNormDepthTex);
      gNormDepthTex = null;
      gl.drawBuffers([gl.COLOR_ATTACHMENT0]);
      if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) {
        document.getElementById("diagTitle").textContent = "Framebuffer Diagnostic";
        document.getElementById("diagBody").textContent = "G-Buffer framebuffer incomplete on this GPU. Try a different browser or enable hardware acceleration.";
        gbufFbo = null;
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        return;
      }
    }

    const sIntFmt = useLdrShadeTarget ? gl.RGBA8 : gl.RGBA16F;
    const sType = useLdrShadeTarget ? gl.UNSIGNED_BYTE : gl.HALF_FLOAT;

    refSplitTex = allocRenderTex(w, h, sIntFmt, gl.RGBA, sType, true);
    refSplitFbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, refSplitFbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, refSplitTex, 0);

    for (let i = 0; i < 2; ++i) {
      if (pingPong[i]) {
        gl.deleteFramebuffer(pingPong[i].fbo);
        gl.deleteTexture(pingPong[i].tex);
      }
      const tex = allocRenderTex(w, h, sIntFmt, gl.RGBA, sType, true);
      const fbo = gl.createFramebuffer();
      gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
      pingPong[i] = { fbo, tex };
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }

  const state = {
    sceneId: 0,
    engineMode: 2,
    solverMode: 0,
    lightDensity: 0,
    cutBudget: 8,
    compareSplit: 0,
    splitX: 0.50,
    froxelHeatmap: 1,
    falseColorLux: 0,
    matCapKeyLock: 1,
    showGizmos: 0,
    wheelSpin: false,
    wheelAngle: 0.0,
    animateLift: false,
    liftHeight: 1.48,
    sunElevation: 24.0,
    exposure: 1.16,
    paintIdx: 0,
    coatRoughness: 0.025,
    luminaires: [],
    selectedId: "light-key-fresnel",
    activeGpuCount: 0,
    partitionNodeCount: 0,
    totalFluxPower: 0,
    frameIndex: 0,
    resetHistory: 1,
    camYaw: 0.66,
    camPitch: 0.24,
    camDist: 6.45,
    camTarget: [0.0, 0.58, 0.0],
  };

  function uploadRadiometricRegister() {
    const active = state.luminaires.filter((l) => l.enabled);
    lumBuffer.fill(0);
    active.forEach((L, idx) => {
      if (idx >= MAX_LUM) return;
      const rad = ComputeLuminaireLinearRadiance(L);
      const qRange = Math.max(4, Math.min(255, Math.round(L.range * 4.0)));
      const packedKindFlags = (L.kind & 7) | ((L.castShadow ? 1 : 0) << 3) | (qRange << 4);
      const o = idx * 16;
      lumBuffer[o + 0] = L.pos[0];
      lumBuffer[o + 1] = L.pos[1];
      lumBuffer[o + 2] = L.pos[2];
      lumBuffer[o + 3] = packedKindFlags;

      lumBuffer[o + 4] = L.rightAxis[0];
      lumBuffer[o + 5] = L.rightAxis[1];
      lumBuffer[o + 6] = L.rightAxis[2];
      lumBuffer[o + 7] = L.halfW;

      lumBuffer[o + 8] = L.upAxis[0];
      lumBuffer[o + 9] = L.upAxis[1];
      lumBuffer[o + 10] = L.upAxis[2];
      lumBuffer[o + 11] = L.halfH;

      lumBuffer[o + 12] = rad[0];
      lumBuffer[o + 13] = rad[1];
      lumBuffer[o + 14] = rad[2];
      lumBuffer[o + 15] = L.aux;
    });

    if (!gl.isContextLost() && lumTex) {
      gl.bindTexture(gl.TEXTURE_2D, lumTex);
      gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, 4, MAX_LUM, gl.RGBA, gl.FLOAT, lumBuffer);
    }

    const tree = BuildPhotometricLightPartition(active.slice(0, MAX_LUM));
    partBuffer.fill(0);
    tree.nodes.forEach((nd, idx) => {
      if (idx >= MAX_NODES) return;
      const o = idx * 16;
      partBuffer[o + 0] = nd.min[0];
      partBuffer[o + 1] = nd.min[1];
      partBuffer[o + 2] = nd.min[2];
      partBuffer[o + 3] = nd.totalPower;

      partBuffer[o + 4] = nd.max[0];
      partBuffer[o + 5] = nd.max[1];
      partBuffer[o + 6] = nd.max[2];
      partBuffer[o + 7] = nd.cosHalfSpread;

      partBuffer[o + 8] = nd.axis[0];
      partBuffer[o + 9] = nd.axis[1];
      partBuffer[o + 10] = nd.axis[2];
      partBuffer[o + 11] = nd.repIndex;

      // Threaded pre-order skip-links: x=isLeaf, y=missSkip, z=repIndex, w=count
      partBuffer[o + 12] = nd.isLeaf;
      partBuffer[o + 13] = nd.missSkip;
      partBuffer[o + 14] = nd.repIndex;
      partBuffer[o + 15] = nd.count;
    });

    if (!gl.isContextLost() && partTex) {
      gl.bindTexture(gl.TEXTURE_2D, partTex);
      gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, 4, MAX_NODES, gl.RGBA, gl.FLOAT, partBuffer);
    }

    state.activeGpuCount = Math.min(active.length, MAX_LUM);
    state.partitionNodeCount = tree.nodes.length;
    state.totalFluxPower = tree.totalPower;
    state.frameIndex = 0;
    state.resetHistory = 1;
    updateTelemetryHud();
    renderInspectorDiagram();
  }

  function loadScenePreset(sceneId) {
    state.sceneId = sceneId;
    state.paintIdx = sceneId === 2 ? 1 : sceneId === 1 ? 2 : 0;
    state.camYaw = sceneId === 2 ? 0.56 : sceneId === 1 ? 0.62 : 0.66;
    state.camPitch = sceneId === 2 ? 0.20 : 0.24;
    state.camDist = sceneId === 2 ? 6.85 : 6.45;
    state.luminaires = ConstructSceneLuminaires(sceneId, state.lightDensity);
    if (!state.luminaires.find((l) => l.id === state.selectedId)) {
      state.selectedId = state.luminaires[0]?.id || "";
    }
    rebuildOutlinerDom();
    syncInspectorDom();
    uploadRadiometricRegister();
  }

  function swatchColorForLuminaire(L) {
    const rgb = L.tintRgb || KelvinToLinearRgb(L.kelvin || 5600);
    const m = Math.max(0.01, rgb[0], rgb[1], rgb[2]);
    const r = Math.round(Math.pow(rgb[0] / m, 1 / 2.2) * 255);
    const g = Math.round(Math.pow(rgb[1] / m, 1 / 2.2) * 255);
    const b = Math.round(Math.pow(rgb[2] / m, 1 / 2.2) * 255);
    return `rgb(${r},${g},${b})`;
  }

  const shortKindLabels = ["Point", "Spot", "IES", "ECE", "LTC", "Tube", "Stokes", "Portal"];

  function rebuildOutlinerDom() {
    const container = document.getElementById("outlinerScroll");
    container.innerHTML = "";

    const groups = new Map();
    for (const L of state.luminaires) {
      if (!groups.has(L.group)) groups.set(L.group, []);
      groups.get(L.group).push(L);
    }

    for (const [groupName, items] of groups.entries()) {
      const hdr = document.createElement("div");
      hdr.className = "outliner-section-label";
      hdr.textContent = `${groupName} (${items.length})`;
      container.appendChild(hdr);

      const listEl = document.createElement("div");
      listEl.className = "outliner-list";
      for (const L of items) {
        const row = document.createElement("div");
        row.className = "outliner-row" + (L.id === state.selectedId ? " selected" : "");
        row.dataset.id = L.id;

        const sw = document.createElement("span");
        sw.className = "outliner-swatch";
        sw.style.background = swatchColorForLuminaire(L);

        const nm = document.createElement("span");
        nm.className = "outliner-name";
        nm.textContent = L.name;

        const kd = document.createElement("span");
        kd.className = "outliner-kind";
        kd.textContent = shortKindLabels[L.kind] || "Light";

        const eye = document.createElement("button");
        eye.className = "eye-btn" + (L.enabled ? "" : " off");
        eye.title = L.enabled ? "Disable luminaire" : "Enable luminaire";
        eye.textContent = L.enabled ? "●" : "○";
        eye.addEventListener("click", (ev) => {
          ev.stopPropagation();
          L.enabled = !L.enabled;
          rebuildOutlinerDom();
          uploadRadiometricRegister();
        });

        row.addEventListener("click", () => {
          state.selectedId = L.id;
          rebuildOutlinerDom();
          syncInspectorDom();
          renderInspectorDiagram();
        });

        row.append(sw, nm, kd, eye);
        listEl.appendChild(row);
      }
      container.appendChild(listEl);
    }
    document.getElementById("outlinerCount").textContent = `${state.luminaires.filter((l) => l.enabled).length} / ${state.luminaires.length}`;
  }

  function getSelectedLuminaire() {
    return state.luminaires.find((l) => l.id === state.selectedId) || state.luminaires[0];
  }

  function syncInspectorDom() {
    const L = getSelectedLuminaire();
    if (!L) return;

    document.getElementById("inspName").textContent = L.name;
    document.getElementById("inspClassBadge").textContent = LuminaireClassNames[L.kind];

    const displayVal = L.kind === 2 || L.kind === 3
      ? L.intensity * 850
      : L.kind === 4 || L.kind === 6 || L.kind === 7
      ? L.intensity * (4.0 * L.halfW * L.halfH) * 320
      : L.intensity * 620;
    const unitStr = (L.kind === 2 || L.kind === 3) ? "cd" : "lm";
    const whole = Math.floor(displayVal);
    const dec = Math.floor((displayVal - whole) * 10);
    document.getElementById("giantWhole").textContent = whole.toLocaleString("en-US");
    document.getElementById("giantDec").textContent = `.${dec}`;
    document.getElementById("giantUnit").textContent = unitStr;
    document.getElementById("giantCaption").textContent =
      L.kind === 4 ? `Dual-Lobe Clipped Heitz LTC · ${(L.halfW * 2).toFixed(1)} × ${(L.halfH * 2).toFixed(1)} m` :
      L.kind === 5 ? `Picott Diffuse + Karis Segment GGX · ${(L.halfW * 2).toFixed(2)} m tube` :
      L.kind === 6 ? `Zero-Noise Stokes Spherical Contour · Emissive Surface` :
      L.kind === 7 ? `Portal Rect LTC Window · Guides Exterior Sky Indoors` :
      L.kind === 3 ? `Automotive ECE R112 · -1.0° Asymmetric Horizon Kink` :
      L.kind === 2 ? `LM-63 Type-C 128×32 Grid · Analytic 4-Leaf Barn-Door Clip` :
      `Non-Singular Karis Sphere Radiator`;

    document.getElementById("ctlIntensity").value = L.intensity;
    document.getElementById("valIntensity").textContent = L.intensity.toFixed(1);
    document.getElementById("ctlKelvin").value = L.kelvin || 5600;
    document.getElementById("valKelvin").textContent = `${Math.round(L.kelvin || 5600)} K`;
    document.getElementById("ctlRange").value = L.range;
    document.getElementById("valRange").textContent = `${L.range.toFixed(1)} m`;

    const sizeLabel = document.getElementById("lblAperture");
    const sizeSlider = document.getElementById("ctlAperture");
    const sizeVal = document.getElementById("valAperture");
    if (L.kind === 2) {
      sizeLabel.textContent = "Barn-Door Flare";
      sizeSlider.min = "10";
      sizeSlider.max = "75";
      sizeSlider.step = "1";
      const deg = Math.round(((L.halfW || 0.6) * 180) / Math.PI);
      sizeSlider.value = deg || 65;
      sizeVal.textContent = L.halfW > 0.02 ? `${deg}°` : "Open";
    } else {
      sizeLabel.textContent = L.kind === 5 ? "Tube Length" : "Aperture Span";
      sizeSlider.min = "0.15";
      sizeSlider.max = "4.5";
      sizeSlider.step = "0.05";
      sizeSlider.value = L.halfW;
      sizeVal.textContent = `${(L.halfW * 2).toFixed(2)} m`;
    }

    const profWrap = document.getElementById("rowIesProfile");
    const profSelect = document.getElementById("selIesProfile");
    if (L.kind === 2 || L.kind === 3) {
      profWrap.style.display = "grid";
      profSelect.value = String(Math.round(L.aux));
    } else {
      profWrap.style.display = "none";
    }

    document.getElementById("tileNodes").textContent = `${state.partitionNodeCount}`;
    document.getElementById("tileCut").textContent = state.cutBudget >= 96 ? "All" : `K=${state.cutBudget}`;
    document.getElementById("tileHeight").textContent = `${Math.max(1, Math.ceil(Math.log2(Math.max(1, state.activeGpuCount))) + 1)}`;
  }

  function renderInspectorDiagram() {
    const cvs = document.getElementById("polarCanvas");
    const rect = cvs.parentElement.getBoundingClientRect();
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    const w = Math.max(240, Math.floor(rect.width));
    const h = Math.max(150, Math.floor(rect.height));
    if (cvs.width !== w * dpr || cvs.height !== h * dpr) {
      cvs.width = w * dpr;
      cvs.height = h * dpr;
    }
    const ctx = cvs.getContext("2d");
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);

    const L = getSelectedLuminaire();
    if (!L) return;

    const cx = w * 0.5;
    const cy = (L.kind === 2 || L.kind === 3 || L.kind === 1) ? 30 : h * 0.52;
    const maxR = (L.kind === 2 || L.kind === 3 || L.kind === 1) ? h - 44 : Math.min(w, h) * 0.36;

    ctx.strokeStyle = "rgba(255,255,255,0.08)";
    ctx.lineWidth = 1;
    for (const rFrac of [0.33, 0.66, 1.0]) {
      ctx.beginPath();
      ctx.arc(cx, cy, maxR * rFrac, 0, Math.PI * 2);
      ctx.stroke();
    }
    ctx.beginPath();
    ctx.moveTo(14, cy);
    ctx.lineTo(w - 14, cy);
    ctx.moveTo(cx, 10);
    ctx.lineTo(cx, h - 10);
    ctx.stroke();

    if (L.kind === 2 || L.kind === 3 || L.kind === 1) {
      const profId = (L.kind === 1) ? 5 : Math.max(0, Math.min(5, Math.round(L.aux)));
      const planes = [
        { az: Math.PI * 0.5, stroke: "#4c8dff", fill: "rgba(76,141,255,0.16)" },
        { az: 0.0, stroke: "#f5a524", fill: "rgba(245,165,36,0.16)" },
      ];
      for (const pl of planes) {
        ctx.beginPath();
        for (let s = -90; s <= 90; s += 2) {
          const pol = (Math.abs(s) * Math.PI) / 180;
          const az = s < 0 ? pl.az + Math.PI : pl.az;
          let cand = L.kind === 1 ? Math.pow(Math.max(0, Math.cos(pol)), 8) : EvaluateNormalizedIesCandela(profId, pol, az);
          if (L.kind === 2 && L.halfW > 0.02 && pol > L.halfW) cand *= 0.05;
          const rr = cand * maxR;
          const px = cx + Math.sin((s * Math.PI) / 180) * rr;
          const py = cy + Math.cos((s * Math.PI) / 180) * rr;
          if (s === -90) ctx.moveTo(px, py);
          else ctx.lineTo(px, py);
        }
        ctx.closePath();
        ctx.fillStyle = pl.fill;
        ctx.fill();
        ctx.strokeStyle = pl.stroke;
        ctx.lineWidth = 1.6;
        ctx.stroke();
      }
      ctx.fillStyle = "#a8a8b2";
      ctx.font = "10.5px Inter, sans-serif";
      ctx.fillText("C90° Lateral", 10, h - 10);
      ctx.fillStyle = "#f5a524";
      ctx.fillText(L.kind === 3 ? "ECE -1.0° Kink" : "C0° Axial", w - 92, h - 10);
    } else {
      const aspect = Math.max(0.2, Math.min(3.2, L.halfW / Math.max(0.03, L.halfH)));
      const rw = Math.min(w * 0.62, 48 * aspect);
      const rh = L.kind === 5 ? 8 : Math.min(h * 0.54, 48 / Math.max(0.5, aspect));
      ctx.fillStyle = swatchColorForLuminaire(L);
      ctx.globalAlpha = 0.24;
      ctx.fillRect(cx - rw * 0.5, cy - rh * 0.5, rw, rh);
      ctx.globalAlpha = 1.0;
      ctx.strokeStyle = swatchColorForLuminaire(L);
      ctx.lineWidth = 2;
      ctx.strokeRect(cx - rw * 0.5, cy - rh * 0.5, rw, rh);
      ctx.fillStyle = "#a8a8b2";
      ctx.font = "10.5px Inter, sans-serif";
      ctx.fillText(
        L.kind === 5 ? "Karis Representative Segment + Picott Line Integral" :
        L.kind === 6 ? "Stokes Contour ∮ θ (e×n)/|e×n| / 2π" :
        "Clipped Spherical-Polygon Dual-Lobe Heitz LTC",
        12, h - 10
      );
    }
  }

  const solverDescriptions = [
    {
      title: "Rank #1 · Unified Analytic Radiometry + Spatial-Orientation Partition Cut",
      body: "100% analytical BRDFs (Dual-Lobe Clipped LTC + 128×32 LM-63 IES + Picott/Karis Tube + Stokes Emissive Contour) indexed by one threaded world-space cone BVH. Zero temporal ghosting on spinning rims/lifts, crisp clearcoat reflections, and O(log N + K) scaling across all 6 render engines.",
      warn: false,
    },
    {
      title: "Rejected Primary · Clustered 3D Camera Froxels",
      body: "Camera-frustum Z-slices fail off-screen BVH clearcoat reflections & world-space GI surfels, force tight range-sphere clamps that clip floor reflections, and still stall on O(K) overlapping showroom lights inside hot chassis froxels (heatmap shown).",
      warn: true,
    },
    {
      title: "Rejected Real-Time · Spatiotemporal 1-spp ReSTIR DI",
      body: "Replaces smooth LTC softboxes & sharp ECE -1.0° cutoffs with 1-spp point grain, breaks clearcoat (α=0.025) neighbor Jacobians, smears highlights across spinning wheels/lifts via temporal history, and suffers indoor garage starvation from 90 klx outdoor sun PDF dominance.",
      warn: true,
    },
    {
      title: "Forbidden Anti-Pattern · Pure SDF Voxel Lighting",
      body: "Evaluating radiance/normals from quantized SDF voxels obliterates ECE headlamp cutoffs & barn doors, leaks light through thin car fenders, rounds tire shadows into floating blobs, and loses view-dependent rectangular clearcoat reflections.",
      warn: true,
    },
  ];

  function updateTelemetryHud() {
    const desc = solverDescriptions[state.solverMode];
    document.getElementById("diagTitle").textContent = desc.title;
    document.getElementById("diagBody").textContent = desc.body;
    document.getElementById("solverDot").className = "hud-dot" + (desc.warn ? " warn" : "");

    const engineNames = ["0 · MatCap", "1 · OpenPBR Direct", "2 · Hybrid GI (SDF Vis + Analytic)", "3 · Progressive Sobol PT"];
    document.getElementById("hudEngine").textContent = engineNames[state.engineMode];
    document.getElementById("hudLights").textContent = `${state.activeGpuCount} Active Luminaires · Cut ${state.cutBudget >= 96 ? "Brute" : state.cutBudget}`;
    document.getElementById("splitRightTag").textContent =
      state.solverMode === 0 ? engineNames[state.engineMode] :
      state.solverMode === 1 ? "Clustered 3D Froxels" :
      state.solverMode === 2 ? "1-spp ReSTIR DI" : "Pure SDF Voxel Lighting";
  }

  function bindSegmentedGroup(containerId, stateKey, onChange) {
    const wrap = document.getElementById(containerId);
    wrap.querySelectorAll(".seg-btn").forEach((btn) => {
      btn.addEventListener("click", () => {
        wrap.querySelectorAll(".seg-btn").forEach((b) => b.classList.remove("active"));
        btn.classList.add("active");
        state[stateKey] = Number(btn.dataset.val);
        onChange();
      });
    });
  }

  bindSegmentedGroup("segScenes", "sceneId", () => loadScenePreset(state.sceneId));
  bindSegmentedGroup("segEngines", "engineMode", () => {
    state.frameIndex = 0;
    state.resetHistory = 1;
    updateTelemetryHud();
  });
  bindSegmentedGroup("segSolvers", "solverMode", () => {
    state.frameIndex = 0;
    state.resetHistory = 1;
    updateTelemetryHud();
  });
  bindSegmentedGroup("segDensity", "lightDensity", () => {
    state.luminaires = ConstructSceneLuminaires(state.sceneId, state.lightDensity);
    rebuildOutlinerDom();
    syncInspectorDom();
    uploadRadiometricRegister();
  });
  bindSegmentedGroup("segCut", "cutBudget", () => {
    state.frameIndex = 0;
    state.resetHistory = 1;
    syncInspectorDom();
    updateTelemetryHud();
  });

  const swatchWrap = document.getElementById("paintSwatches");
  PaintPresets.forEach((p, idx) => {
    const b = document.createElement("button");
    b.className = "paint-btn" + (idx === state.paintIdx ? " active" : "");
    b.style.background = p.hex;
    b.title = p.name;
    b.addEventListener("click", () => {
      state.paintIdx = idx;
      swatchWrap.querySelectorAll(".paint-btn").forEach((el, j) => el.classList.toggle("active", j === idx));
      state.frameIndex = 0;
      state.resetHistory = 1;
    });
    swatchWrap.appendChild(b);
  });

  document.getElementById("ctlIntensity").addEventListener("input", (e) => {
    const L = getSelectedLuminaire();
    if (!L) return;
    L.intensity = parseFloat(e.target.value);
    syncInspectorDom();
    uploadRadiometricRegister();
  });

  document.getElementById("ctlKelvin").addEventListener("input", (e) => {
    const L = getSelectedLuminaire();
    if (!L) return;
    L.kelvin = parseFloat(e.target.value);
    delete L.tintRgb;
    rebuildOutlinerDom();
    syncInspectorDom();
    uploadRadiometricRegister();
  });

  document.getElementById("ctlRange").addEventListener("input", (e) => {
    const L = getSelectedLuminaire();
    if (!L) return;
    L.range = parseFloat(e.target.value);
    syncInspectorDom();
    uploadRadiometricRegister();
  });

  document.getElementById("ctlAperture").addEventListener("input", (e) => {
    const L = getSelectedLuminaire();
    if (!L) return;
    const v = parseFloat(e.target.value);
    if (L.kind === 2) {
      L.halfW = v >= 74 ? 0.0 : (v * Math.PI) / 180;
    } else {
      const ratio = L.halfH / Math.max(0.02, L.halfW);
      L.halfW = v;
      if (L.kind === 4 || L.kind === 6 || L.kind === 7) {
        L.halfH = Math.max(0.15, v * Math.min(1.5, Math.max(0.45, ratio)));
      }
    }
    syncInspectorDom();
    uploadRadiometricRegister();
  });

  document.getElementById("selIesProfile").addEventListener("change", (e) => {
    const L = getSelectedLuminaire();
    if (!L) return;
    L.aux = parseInt(e.target.value, 10);
    syncInspectorDom();
    uploadRadiometricRegister();
  });

  document.getElementById("ctlCoatRough").addEventListener("input", (e) => {
    state.coatRoughness = parseFloat(e.target.value);
    document.getElementById("valCoatRough").textContent = state.coatRoughness.toFixed(3);
    state.frameIndex = 0;
    state.resetHistory = 1;
  });

  document.getElementById("ctlSunEl").addEventListener("input", (e) => {
    state.sunElevation = parseFloat(e.target.value);
    document.getElementById("valSunEl").textContent = `${state.sunElevation.toFixed(0)}°`;
    state.frameIndex = 0;
    state.resetHistory = 1;
  });

  document.getElementById("ctlExposure").addEventListener("input", (e) => {
    state.exposure = parseFloat(e.target.value);
    document.getElementById("valExposure").textContent = state.exposure.toFixed(2);
  });

  function bindPillToggle(btnId, getter, toggler) {
    const btn = document.getElementById(btnId);
    btn.addEventListener("click", () => {
      toggler();
      btn.classList.toggle("on", getter());
      btn.querySelector(".st").textContent = getter() ? "ON" : "OFF";
    });
  }

  bindPillToggle("btnSplit", () => state.compareSplit === 1, () => {
    state.compareSplit = state.compareSplit ? 0 : 1;
    document.getElementById("splitOverlay").classList.toggle("hidden", state.compareSplit === 0);
    state.frameIndex = 0;
    state.resetHistory = 1;
  });

  bindPillToggle("btnWheelSpin", () => state.wheelSpin, () => { state.wheelSpin = !state.wheelSpin; });
  bindPillToggle("btnLiftAnim", () => state.animateLift, () => { state.animateLift = !state.animateLift; });
  bindPillToggle("btnHeatmap", () => state.froxelHeatmap === 1, () => {
    state.froxelHeatmap = state.froxelHeatmap ? 0 : 1;
    state.resetHistory = 1;
  });
  bindPillToggle("btnFalseLux", () => state.falseColorLux === 1, () => {
    state.falseColorLux = state.falseColorLux ? 0 : 1;
  });
  bindPillToggle("btnGizmos", () => state.showGizmos === 1, () => {
    state.showGizmos = state.showGizmos ? 0 : 1;
    state.resetHistory = 1;
  });

  const splitLine = document.getElementById("splitLine");
  const splitKnob = document.getElementById("splitKnob");
  let draggingSplit = false;

  function syncSplitDom() {
    const pct = `${(state.splitX * 100).toFixed(1)}%`;
    splitLine.style.left = pct;
    splitKnob.style.left = pct;
  }
  syncSplitDom();

  splitKnob.addEventListener("pointerdown", (e) => {
    e.stopPropagation();
    draggingSplit = true;
    splitKnob.setPointerCapture(e.pointerId);
  });
  window.addEventListener("pointermove", (e) => {
    if (!draggingSplit) return;
    const r = canvas.getBoundingClientRect();
    state.splitX = Math.max(0.08, Math.min(0.92, (e.clientX - r.left) / Math.max(1, r.width)));
    syncSplitDom();
  });
  window.addEventListener("pointerup", () => { draggingSplit = false; });

  let camDrag = null;
  canvas.addEventListener("pointerdown", (e) => {
    camDrag = { x: e.clientX, y: e.clientY, pan: e.button === 2 || e.shiftKey };
    canvas.setPointerCapture(e.pointerId);
  });
  canvas.addEventListener("contextmenu", (e) => e.preventDefault());
  canvas.addEventListener("pointermove", (e) => {
    if (!camDrag) return;
    const dx = e.clientX - camDrag.x;
    const dy = e.clientY - camDrag.y;
    camDrag.x = e.clientX;
    camDrag.y = e.clientY;
    if (camDrag.pan) {
      const cy = Math.cos(state.camYaw), sy = Math.sin(state.camYaw);
      state.camTarget[0] -= cy * dx * 0.008;
      state.camTarget[2] += sy * dx * 0.008;
      state.camTarget[1] = Math.max(0.15, Math.min(2.6, state.camTarget[1] + dy * 0.008));
    } else {
      state.camYaw -= dx * 0.0065;
      state.camPitch = Math.max(0.04, Math.min(1.22, state.camPitch + dy * 0.0065));
    }
    state.frameIndex = 0;
    state.resetHistory = 1;
  });
  window.addEventListener("pointerup", () => { camDrag = null; });
  canvas.addEventListener("wheel", (e) => {
    e.preventDefault();
    state.camDist = Math.max(2.8, Math.min(15.5, state.camDist * (1.0 + Math.sign(e.deltaY) * 0.08)));
    state.frameIndex = 0;
    state.resetHistory = 1;
  }, { passive: false });

  window.addEventListener("resize", () => renderInspectorDiagram());

  loadScenePreset(0);

  let prevCam = { pos: [0, 2, 6], fwd: [0, 0, -1], right: [1, 0, 0], up: [0, 1, 0] };

  function computeCameraBasis() {
    const cp = Math.cos(state.camPitch), sp = Math.sin(state.camPitch);
    const cy = Math.cos(state.camYaw), sy = Math.sin(state.camYaw);
    const pos = [
      state.camTarget[0] + state.camDist * cp * sy,
      state.camTarget[1] + state.camDist * sp,
      state.camTarget[2] + state.camDist * cp * cy,
    ];
    const fwd = Vec3Normalize(Vec3Sub(state.camTarget, pos));
    const right = Vec3Normalize(Vec3Cross(fwd, [0, 1, 0]));
    const up = Vec3Normalize(Vec3Cross(right, fwd));
    return { pos, fwd, right, up };
  }

  const progCache = new WeakMap();
  function uLoc(prog, name) {
    let map = progCache.get(prog);
    if (!map) {
      map = new Map();
      progCache.set(prog, map);
    }
    if (!map.has(name)) map.set(name, gl.getUniformLocation(prog, name));
    return map.get(name);
  }

  function dispatchShadePass(prog, targetFbo, w, h, now, cam, paint, prevTex, overrideEngineMode) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, targetFbo);
    gl.viewport(0, 0, w, h);
    gl.useProgram(prog);

    gl.uniform2f(uLoc(prog, "uResolution"), w, h);
    gl.uniform1f(uLoc(prog, "uTime"), now * 0.001);
    gl.uniform1i(uLoc(prog, "uFrameIndex"), state.frameIndex);
    gl.uniform3fv(uLoc(prog, "uCamPos"), cam.pos);
    gl.uniform3fv(uLoc(prog, "uCamFwd"), cam.fwd);
    gl.uniform3fv(uLoc(prog, "uCamRight"), cam.right);
    gl.uniform3fv(uLoc(prog, "uCamUp"), cam.up);
    gl.uniform1f(uLoc(prog, "uTanHalfFov"), Math.tan((40 * Math.PI) / 360));

    gl.uniform1i(uLoc(prog, "uSceneId"), state.sceneId);
    gl.uniform1i(uLoc(prog, "uEngineMode"), overrideEngineMode);
    gl.uniform1i(uLoc(prog, "uFroxelHeatmap"), state.froxelHeatmap);
    gl.uniform1i(uLoc(prog, "uCutBudget"), state.cutBudget);
    gl.uniform1i(uLoc(prog, "uLuminaireCount"), state.activeGpuCount);
    gl.uniform1i(uLoc(prog, "uPartitionNodeCount"), state.partitionNodeCount);

    gl.uniform3fv(uLoc(prog, "uCarAlbedo"), paint.rgb);
    gl.uniform1f(uLoc(prog, "uCarMetallic"), paint.metallic);
    gl.uniform1f(uLoc(prog, "uCarRoughness"), paint.roughness);
    gl.uniform1f(uLoc(prog, "uCoatRoughness"), state.coatRoughness);
    gl.uniform1f(uLoc(prog, "uLiftHeight"), state.liftHeight);
    gl.uniform1f(uLoc(prog, "uSunElevation"), state.sunElevation);
    gl.uniform1i(uLoc(prog, "uMatCapKeyLock"), state.matCapKeyLock);
    gl.uniform1i(uLoc(prog, "uShowGizmos"), state.showGizmos);

    gl.uniform3fv(uLoc(prog, "uPrevCamPos"), prevCam.pos);
    gl.uniform3fv(uLoc(prog, "uPrevCamFwd"), prevCam.fwd);
    gl.uniform3fv(uLoc(prog, "uPrevCamRight"), prevCam.right);
    gl.uniform3fv(uLoc(prog, "uPrevCamUp"), prevCam.up);
    gl.uniform1i(uLoc(prog, "uResetHistory"), state.resetHistory);
    gl.uniform1i(uLoc(prog, "uLdrFallback"), useLdrShadeTarget ? 1 : 0);

    const bindTex = (unit, tex, name) => {
      gl.activeTexture(gl.TEXTURE0 + unit);
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.uniform1i(uLoc(prog, name), unit);
    };
    bindTex(0, gPosMatTex, "uGPosMat");
    bindTex(1, gNormDepthTex, "uGNormDepth");
    bindTex(2, lumTex, "uLuminaireTex");
    bindTex(3, partTex, "uPartitionTex");
    bindTex(4, iesTex, "uIesAtlasTex");
    bindTex(5, ltcTex, "uLtcTex");
    bindTex(6, prevTex, "uPrevAccumTex");

    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  let lastTime = performance.now();
  let fpsAccum = 0, fpsCount = 0;

  function renderTick(now) {
    if (gl.isContextLost()) return;
    const dt = Math.min(0.1, (now - lastTime) * 0.001);
    lastTime = now;
    fpsAccum += dt;
    fpsCount++;
    if (fpsAccum >= 0.45) {
      const ms = (fpsAccum / fpsCount) * 1000;
      document.getElementById("hudPerf").textContent = `${ms.toFixed(1)} ms · ${Math.round(1000 / Math.max(1, ms))} FPS`;
      fpsAccum = 0;
      fpsCount = 0;
    }

    if (state.wheelSpin) {
      state.wheelAngle += dt * 7.5;
      if (state.engineMode === 3) state.frameIndex = 0;
    }
    if (state.animateLift) {
      state.liftHeight = 0.95 + 0.65 * (0.5 + 0.5 * Math.sin(now * 0.0018));
      if (state.engineMode === 3) state.frameIndex = 0;
    }

    const rect = canvas.getBoundingClientRect();
    const scale = Math.min(1.25, window.devicePixelRatio || 1) * 0.92;
    const w = Math.max(320, Math.floor(rect.width * scale));
    const h = Math.max(200, Math.floor(rect.height * scale));
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
      state.resetHistory = 1;
      state.frameIndex = 0;
    }
    ensureRenderTargets(w, h);
    if (!gbufFbo || !pingPong[0] || !pingPong[1] || !gbufferProg || !displayProg) {
      requestAnimationFrame(renderTick);
      return;
    }

    const cam = computeCameraBasis();
    const paint = PaintPresets[state.paintIdx];
    const srcIdx = ppIdx;
    const dstIdx = 1 - ppIdx;

    const activeProgIdx = state.engineMode === 0 ? 0 : state.solverMode;
    const activeProg = getSolverProg(activeProgIdx);
    if (!activeProg) {
      requestAnimationFrame(renderTick);
      return;
    }

    // Pass 1: Deferred Primary Raymarch into G-Buffer (Zero light loops!)
    gl.bindFramebuffer(gl.FRAMEBUFFER, gbufFbo);
    gl.viewport(0, 0, w, h);
    gl.useProgram(gbufferProg);
    gl.bindVertexArray(emptyVao);
    gl.uniform2f(uLoc(gbufferProg, "uResolution"), w, h);
    gl.uniform3fv(uLoc(gbufferProg, "uCamPos"), cam.pos);
    gl.uniform3fv(uLoc(gbufferProg, "uCamFwd"), cam.fwd);
    gl.uniform3fv(uLoc(gbufferProg, "uCamRight"), cam.right);
    gl.uniform3fv(uLoc(gbufferProg, "uCamUp"), cam.up);
    gl.uniform1f(uLoc(gbufferProg, "uTanHalfFov"), Math.tan((40 * Math.PI) / 360));
    gl.uniform1i(uLoc(gbufferProg, "uSceneId"), state.sceneId);
    gl.uniform1f(uLoc(gbufferProg, "uWheelAngle"), state.wheelAngle);
    gl.uniform1f(uLoc(gbufferProg, "uLiftHeight"), state.liftHeight);
    const jx = state.engineMode === 3 ? (((state.frameIndex * 0.75487766) % 1) - 0.5) / w : 0;
    const jy = state.engineMode === 3 ? (((state.frameIndex * 0.56984029) % 1) - 0.5) / h : 0;
    gl.uniform2f(uLoc(gbufferProg, "uJitter"), jx, jy);
    gl.drawArrays(gl.TRIANGLES, 0, 3);

    // Pass 2: Active Solver Shade Pass (and Reference Partition Pass when A/B Split Wipe is enabled)
    dispatchShadePass(activeProg, pingPong[dstIdx].fbo, w, h, now, cam, paint, pingPong[srcIdx].tex, state.engineMode);

    let leftSplitTex = pingPong[dstIdx].tex;
    if (state.compareSplit === 1) {
      const refProg = getSolverProg(0);
      if (refProg) {
        const refEngine = state.engineMode === 0 ? 2 : state.engineMode;
        dispatchShadePass(refProg, refSplitFbo, w, h, now, cam, paint, pingPong[srcIdx].tex, refEngine);
        leftSplitTex = refSplitTex;
      }
    }

    // Pass 3: ACES Filmic / False-Colour Isolux + Split-Wipe Display Composite
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, w, h);
    gl.useProgram(displayProg);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, leftSplitTex);
    gl.uniform1i(uLoc(displayProg, "uHdrTexA"), 0);
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, pingPong[dstIdx].tex);
    gl.uniform1i(uLoc(displayProg, "uHdrTexB"), 1);
    gl.uniform1i(uLoc(displayProg, "uCompareSplit"), state.compareSplit);
    gl.uniform1f(uLoc(displayProg, "uSplitX"), state.splitX);
    gl.uniform1f(uLoc(displayProg, "uExposure"), state.exposure);
    gl.uniform1i(uLoc(displayProg, "uFalseColorLux"), state.falseColorLux);
    gl.uniform1i(uLoc(displayProg, "uLdrFallback"), useLdrShadeTarget ? 1 : 0);
    gl.drawArrays(gl.TRIANGLES, 0, 3);

    ppIdx = dstIdx;
    prevCam = cam;
    state.resetHistory = 0;
    state.frameIndex++;

    requestAnimationFrame(renderTick);
  }

  window.__RadiometricLab = {
    state,
    setScene: (id) => document.querySelectorAll("#segScenes .seg-btn")[id]?.click(),
    setSolver: (id) => document.querySelectorAll("#segSolvers .seg-btn")[id]?.click(),
    setEngine: (id) => document.querySelectorAll("#segEngines .seg-btn")[id]?.click(),
    setDensity: (id) => document.querySelectorAll("#segDensity .seg-btn")[id]?.click(),
    toggleSplit: () => document.getElementById("btnSplit").click(),
  };

  requestAnimationFrame(renderTick);
}
