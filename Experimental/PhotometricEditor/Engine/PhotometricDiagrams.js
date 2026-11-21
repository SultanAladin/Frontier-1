// PhotometricDiagrams.js — Verbatim Project Zero Editor illustrations & diagrams
// for Luminaires (LightProjection.js), Sun (SunGizmo + IlluminanceCurve),
// Moon (LunarSphere + MoonDisc), Stars (300-star field + TwinkleSignal),
// Lens Flare, Atmosphere (ScatteringGraph), and Fog (FogBeamChamber).

import {
  BlendColour,
  Clamp,
  KelvinRgb,
  MixTint,
  ProjectLight,
  ProjectResponse,
  Rgba,
} from "./ProjectZeroLightProjection.js";
import { EvaluateSolarEphemeris } from "./CelestialAtmosphereModel.js";
import { PresetIesNames, SampleIes, SolveMoonMetrics } from "./PhotometryEngine.js";

function EnsureHiDpiCanvas(Canvas, LogicalW, LogicalH) {
  const Ratio = Math.min(2, (typeof window !== "undefined" ? window.devicePixelRatio : 1) || 1);
  const PixelW = Math.round(LogicalW * Ratio);
  const PixelH = Math.round(LogicalH * Ratio);
  if (Canvas.width !== PixelW || Canvas.height !== PixelH) {
    Canvas.width = PixelW;
    Canvas.height = PixelH;
  }
  const Ctx = Canvas.getContext("2d");
  Ctx.setTransform(Ratio, 0, 0, Ratio, 0, 0);
  return { Ctx, W: LogicalW, H: LogicalH };
}

function PropsToProjectZeroModel(Type, P = {}, LoadedIes = {}) {
  const ProfileName = P.profile || "ECE Low Beam";
  const ProfileIdx = Math.max(0, PresetIesNames.indexOf(ProfileName));
  const Properties = {
    Distance: P.distance ?? 26,
    Decay: P.decay ?? 2,
    Intensity: P.intensity ?? (Type === "spotlight" ? 62 : 14),
    "Outer cone": (P.angle ?? 13) * 2,
    Penumbra: P.penumbra ?? 0.42,
    "Profile type": ProfileIdx,
    "Field angle": P.cone ?? 58,
    "Cut-off pitch": P.cutoff ?? -1,
    "Luminous flux": P.lumens ?? 1650,
    "Profile scale": P.multiplier ?? 1,
    Width: P.width ?? 2,
    Height: P.height ?? 1,
    "Disk radius": (P.width ?? 1.8) * 0.5,
    Shape: P.aperture === "Disk" ? 1 : 0,
    Spread: P.spread ?? 120,
    "Two sided": !!P.twoSided,
    Length: P.length ?? 1.8,
    Radius: P.radius ?? 0.04,
    "Driver watts": P.watts ?? 18,
    Efficacy: P.efficacy ?? 125,
    Dimmer: P.dimmer ?? 1,
    "Package diameter": P.diameter ?? 40,
    "Emission angle": P.angle ?? 120,
    "Flux per metre": P.lumensPerMetre ?? 1000,
    "Watts per metre": P.wattsPerMetre ?? 14.4,
    "LED density": P.ledsPerMetre ?? 60,
    Voltage: P.voltage ?? 24,
    Opal: !!P.diffuser,
    "Colour temperature": P.temperature ?? 4300,
    Tint: P.color || "#ffffff",
  };

  const ActiveIesKey =
    P.customIesKey && LoadedIes?.[P.customIesKey]
      ? P.customIesKey
      : P.useMeasuredIes && P.measuredProfile && LoadedIes?.[P.measuredProfile]
        ? P.measuredProfile
        : null;

  if (ActiveIesKey) {
    const Parsed = LoadedIes[ActiveIesKey];
    Properties.MeasuredName = `${ActiveIesKey}.ies`;
    Properties.MeasuredSampleFn = (AngleDeg, PhiDeg) =>
      Clamp(SampleIes(Parsed, PhiDeg, AngleDeg), 0, 1);
  }

  const Read = (Key, Fallback) => {
    const V = Properties[Key];
    return V === undefined ? Fallback : V;
  };
  const HasTemp = ["ieslight", "tubelight", "ledlight", "ledstrip"].includes(Type);
  const Tint = BlendColour(Properties.Tint, Properties["Colour temperature"], HasTemp);
  const Flux =
    Type === "pointlight" || Type === "spotlight"
      ? Read("Intensity", 14)
      : Type === "ieslight"
        ? Read("Luminous flux", 1650) * Read("Profile scale", 1)
        : Type === "ledlight"
          ? Read("Driver watts", 10) * Read("Efficacy", 110) * Read("Dimmer", 1)
          : Type === "ledstrip"
            ? Read("Flux per metre", 1000) * Read("Length", 2.4) * Read("Dimmer", 1)
            : Read("Luminous flux", 2400);

  return { Style: Type, Read, Properties, Tint, Flux };
}

// ── 1. VERBATIM PROJECT ZERO PREVIEW ILLUSTRATION ─────────────────────────────
export function DrawHeroLightCanvas(Canvas, Type, P, LoadedIes) {
  const { Ctx, W, H } = EnsureHiDpiCanvas(Canvas, 380, 260);
  const Model = PropsToProjectZeroModel(Type, P, LoadedIes);
  ProjectLight(Ctx, W, H, Model);
}

// ── 2. VERBATIM PROJECT ZERO ANALYTICAL CURVE & TABLE ─────────────────────────
export function DrawResponseCurveCanvas(Canvas, Type, P, LoadedIes) {
  const LogicalH = Type === "pointlight" ? 160 : 98;
  const { Ctx, W, H } = EnsureHiDpiCanvas(Canvas, 340, LogicalH);
  const Model = PropsToProjectZeroModel(Type, P, LoadedIes);
  ProjectResponse(Ctx, W, H, Model);

  const Int = Math.max(0.01, Number(P.intensity ?? 14));
  const Dec = Number(P.decay ?? 2);

  if (Type === "pointlight") {
    return {
      Kind: "Distance response",
      Lead: `${(Int / Math.pow(5, Dec)).toFixed(2)} lx`,
      Caption: "Authored decay at 5 m · free-space estimate",
      Rows: [1, 2, 5, 10].map((D) => {
        const E = Int / Math.pow(D, Dec);
        const Pct = (E / Int) * 100;
        return { Label: `${D} m`, Value: `${E.toFixed(2)} lx`, Rel: `${Pct.toFixed(1)}%` };
      }),
    };
  }
  if (Type === "spotlight") {
    const D5 = 2 * 5 * Math.tan((Clamp(Number(P.angle ?? 13), 1, 80) * Math.PI) / 180);
    return {
      Kind: "Beam section",
      Lead: `${D5.toFixed(2)} m`,
      Caption: "Beam diameter on a perpendicular plane at 5 m",
      Rows: [],
    };
  }
  if (Type === "ieslight") {
    return {
      Kind: "Angular response",
      Lead: `${Math.round(Number(P.cone ?? 58))}°`,
      Caption: P.useMeasuredIes || P.customIesKey
        ? "Measured LM-63 Type-C C0 plane · peak normalized"
        : "Normalized preset sections · not measured candela",
      Rows: [],
    };
  }
  if (Type === "arealight") {
    const Area = Math.max(
      0.01,
      P.aperture === "Disk"
        ? Math.PI * ((P.width ?? 1.8) * 0.5) ** 2
        : (P.width ?? 2) * (P.height ?? 1),
    );
    return {
      Kind: "Aperture balance",
      Lead: `${Math.round((P.lumens ?? 2400) / Area).toLocaleString()} lm/m²`,
      Caption: "Flux per aperture area · not surface luminance",
      Rows: [],
    };
  }
  if (Type === "tubelight") {
    const L = Math.max(0.1, Number(P.length ?? 1.8));
    return {
      Kind: "Linear output",
      Lead: `${Math.round((P.lumens ?? 1800) / L).toLocaleString()} lm/m`,
      Caption: "Authored source flux per metre",
      Rows: [],
    };
  }
  if (Type === "ledlight") {
    return {
      Kind: "Conversion budget",
      Lead: `${Math.round((P.dimmer ?? 1) * 100)} %`,
      Caption: "Authored dimmer · estimated output ignores thermal losses",
      Rows: [],
    };
  }
  const Amps = ((P.wattsPerMetre ?? 14.4) * (P.length ?? 2.4) * (P.dimmer ?? 1)) / Math.max(1, P.voltage ?? 24);
  return {
    Kind: "Electrical budget",
    Lead: `${Amps.toFixed(2)} A`,
    Caption: "Connected watts ÷ supply volts · ideal full-load current",
    Rows: [],
  };
}

// Used exclusively inside the full Goniometer Matrix modal
export function DrawIesPolarWebCanvas(Canvas, P, LoadedIes) {
  DrawHeroLightCanvas(Canvas, "ieslight", P, LoadedIes);
}

// ── 3. VERBATIM PROJECT ZERO SUN ORBIT & ILLUMINANCE CURVE ────────────────────
export function DrawSolarArcCanvas(Canvas, Observer) {
  const { Ctx, W, H } = EnsureHiDpiCanvas(Canvas, 360, 260);
  Ctx.clearRect(0, 0, W, H);
  const Ephem = EvaluateSolarEphemeris(Observer);
  const Az = Ephem?.AzimuthDeg ?? 180;
  const El = Ephem?.ElevationDeg ?? 35;

  const R = 96;
  const CX = 180;
  const CY = 132;
  const Angle = ((Az - 135) * Math.PI) / 180;
  const Point = (T) => [
    CX - R * Math.cos(T) * Math.cos(Angle),
    CY - R * Math.sin(T) + R * 0.45 * Math.cos(T) * Math.sin(Angle),
  ];

  Ctx.save();
  Ctx.beginPath();
  Ctx.ellipse(CX, CY, R, 46, 0, 0, Math.PI * 2);
  Ctx.fillStyle = "rgba(21,21,21,0.62)";
  Ctx.fill();
  Ctx.clip();
  Ctx.strokeStyle = "rgba(255,255,255,0.05)";
  Ctx.lineWidth = 0.65;
  for (let i = 0; i < 16; i++) {
    Ctx.beginPath();
    Ctx.moveTo(84 + i * 12, 86);
    Ctx.lineTo(84 + i * 12, 178);
    Ctx.moveTo(84, 86 + i * 12);
    Ctx.lineTo(276, 86 + i * 12);
    Ctx.stroke();
  }
  Ctx.restore();

  Ctx.strokeStyle = "rgba(112,112,112,0.65)";
  Ctx.lineWidth = 0.9;
  Ctx.beginPath();
  Ctx.ellipse(CX, CY, R, 46, 0, 0, Math.PI * 2);
  Ctx.stroke();

  Ctx.save();
  Ctx.setLineDash([2, 3]);
  Ctx.strokeStyle = "rgba(119,115,107,0.5)";
  Ctx.lineWidth = 1;
  Ctx.beginPath();
  for (let i = 0; i <= 80; i++) {
    const [px, py] = Point(Math.PI + (i * Math.PI) / 80);
    if (i === 0) Ctx.moveTo(px, py);
    else Ctx.lineTo(px, py);
  }
  Ctx.stroke();
  Ctx.setLineDash([]);

  Ctx.strokeStyle = "#e2ac5b";
  Ctx.lineWidth = 1.8;
  Ctx.beginPath();
  for (let i = 0; i <= 80; i++) {
    const [px, py] = Point((i * Math.PI) / 80);
    if (i === 0) Ctx.moveTo(px, py);
    else Ctx.lineTo(px, py);
  }
  Ctx.stroke();
  Ctx.restore();

  const [X, Y] = Point((El * Math.PI) / 180);
  const [HX, HY] = Point(0);
  const Night = El < 0;

  Ctx.save();
  Ctx.setLineDash([2, 4]);
  Ctx.strokeStyle = "rgba(184,170,142,0.55)";
  Ctx.lineWidth = 0.8;
  Ctx.beginPath();
  Ctx.moveTo(X, Y);
  Ctx.lineTo(HX, HY);
  Ctx.stroke();
  Ctx.restore();

  Ctx.fillStyle = "rgba(197,192,182,0.32)";
  Ctx.beginPath();
  Ctx.ellipse(X, HY, 6, 2.3, 0, 0, Math.PI * 2);
  Ctx.fill();

  Ctx.fillStyle = "#181818";
  Ctx.strokeStyle = "#f1efe7";
  Ctx.lineWidth = 1.7;
  Ctx.beginPath();
  Ctx.arc(HX, HY, 5.5, 0, Math.PI * 2);
  Ctx.fill();
  Ctx.stroke();

  if (!Night) {
    const Glow = Ctx.createRadialGradient(X, Y, 2, X, Y, 22);
    Glow.addColorStop(0, "rgba(255,218,135,0.35)");
    Glow.addColorStop(1, "rgba(255,218,135,0)");
    Ctx.fillStyle = Glow;
    Ctx.beginPath();
    Ctx.arc(X, Y, 22, 0, Math.PI * 2);
    Ctx.fill();
  }

  Ctx.fillStyle = Night ? "#9b805c" : "#f4cd75";
  Ctx.strokeStyle = Night ? "#9b805c" : "#b89045";
  Ctx.lineWidth = 1;
  Ctx.beginPath();
  Ctx.arc(X, Y, 7.5, 0, Math.PI * 2);
  Ctx.fill();
  Ctx.stroke();

  Ctx.fillStyle = "rgba(190,196,184,0.6)";
  Ctx.font = "9px 'IBM Plex Mono', monospace";
  Ctx.textAlign = "center";
  Ctx.fillText("12h", CX, 20);
  Ctx.fillText("24h", CX, 250);
  Ctx.fillText("N", CX, 74);
  Ctx.fillText("S", CX, 194);
  Ctx.fillText("W", 56, 136);
  Ctx.fillText("E", 304, 136);
}

export function DrawSunDiscChromaticityCanvas(Canvas, Sun) {
  const { Ctx, W, H } = EnsureHiDpiCanvas(Canvas, 340, 100);
  Ctx.clearRect(0, 0, W, H);
  const T = Clamp((Sun?.Intensity ?? 18) / 60, 0, 1);
  const YFn = (x) => 76 - 56 * (1 - Math.cos((x * Math.PI) / 2));

  Ctx.save();
  Ctx.setLineDash([2, 5]);
  Ctx.strokeStyle = "rgba(255,255,255,0.06)";
  [24, 50, 77].forEach((y) => {
    Ctx.beginPath();
    Ctx.moveTo(14, y);
    Ctx.lineTo(W - 14, y);
    Ctx.stroke();
  });
  Ctx.restore();

  Ctx.strokeStyle = "#595246";
  Ctx.lineWidth = 1.2;
  Ctx.beginPath();
  for (let i = 0; i <= 60; i++) {
    const x = i / 60;
    const px = 14 + x * (W - 28);
    const py = YFn(x);
    if (i === 0) Ctx.moveTo(px, py);
    else Ctx.lineTo(px, py);
  }
  Ctx.stroke();

  const EndX = 14 + T * (W - 28);
  const EndY = YFn(T);
  const Fill = Ctx.createLinearGradient(0, 18, 0, 88);
  Fill.addColorStop(0, "rgba(216,189,130,0.22)");
  Fill.addColorStop(1, "rgba(216,189,130,0)");
  Ctx.beginPath();
  for (let i = 0; i <= 60; i++) {
    const x = (T * i) / 60;
    const px = 14 + x * (W - 28);
    const py = YFn(x);
    if (i === 0) Ctx.moveTo(px, py);
    else Ctx.lineTo(px, py);
  }
  Ctx.lineTo(EndX, 88);
  Ctx.lineTo(14, 88);
  Ctx.closePath();
  Ctx.fillStyle = Fill;
  Ctx.fill();

  Ctx.strokeStyle = "#dac18b";
  Ctx.lineWidth = 1.6;
  Ctx.beginPath();
  for (let i = 0; i <= 60; i++) {
    const x = (T * i) / 60;
    const px = 14 + x * (W - 28);
    const py = YFn(x);
    if (i === 0) Ctx.moveTo(px, py);
    else Ctx.lineTo(px, py);
  }
  Ctx.stroke();

  Ctx.fillStyle = "rgba(232,199,106,0.1)";
  Ctx.beginPath();
  Ctx.arc(EndX, EndY, 10, 0, Math.PI * 2);
  Ctx.fill();
  Ctx.fillStyle = "#efe2c2";
  Ctx.beginPath();
  Ctx.arc(EndX, EndY, 3.4, 0, Math.PI * 2);
  Ctx.fill();
}

// ── 4. VERBATIM PROJECT ZERO HDRI EQURECTANGULAR PROBE ────────────────────────
export function DrawHdrDomeCanvas(Canvas, Dome, SunDir = [0, 0.5, -0.8]) {
  const { Ctx, W, H } = EnsureHiDpiCanvas(Canvas, 340, 152);
  Ctx.clearRect(0, 0, W, H);
  const G = Ctx.createLinearGradient(0, 0, 0, H);
  G.addColorStop(0, Dome?.ZenithColor || "#3d6394");
  G.addColorStop(0.54, Dome?.HorizonColor || "#98adc4");
  G.addColorStop(0.56, Dome?.GroundColor || "#252624");
  G.addColorStop(1, "#121316");
  Ctx.fillStyle = G;
  Ctx.fillRect(0, 0, W, H);

  Ctx.strokeStyle = "rgba(255,255,255,0.16)";
  Ctx.setLineDash([3, 5]);
  Ctx.beginPath();
  Ctx.moveTo(0, H * 0.55);
  Ctx.lineTo(W, H * 0.55);
  Ctx.stroke();
  Ctx.setLineDash([]);

  const Rot = ((Dome?.RotationDeg ?? 0) % 360) / 360;
  const SX = ((Rot + 0.5 + (SunDir[0] || 0) * 0.25) % 1) * W;
  const SY = Clamp(H * 0.55 - (SunDir[1] || 0.4) * (H * 0.45), 14, H - 14);
  const Glow = Ctx.createRadialGradient(SX, SY, 2, SX, SY, 34);
  Glow.addColorStop(0, "rgba(255,244,205,0.92)");
  Glow.addColorStop(0.4, "rgba(255,214,135,0.35)");
  Glow.addColorStop(1, "rgba(255,214,135,0)");
  Ctx.fillStyle = Glow;
  Ctx.beginPath();
  Ctx.arc(SX, SY, 34, 0, Math.PI * 2);
  Ctx.fill();
}

// ── 5. VERBATIM PROJECT ZERO LUNAR SPHERE & MOON DISC DIAL ────────────────────
export function DrawMoonPreviewAndDialCanvas(Canvas, Moon) {
  const { Ctx, W, H } = EnsureHiDpiCanvas(Canvas, 360, 236);
  Ctx.clearRect(0, 0, W, H);
  const Metrics = SolveMoonMetrics(Moon);

  // Left: Lit lunar disc silhouette matching MoonDisc in moon-controls.jsx
  const DiscX = 94;
  const DiscY = 114;
  const DiscR = 54;
  Ctx.fillStyle = "#202834";
  Ctx.strokeStyle = "rgba(131,151,179,0.35)";
  Ctx.beginPath();
  Ctx.arc(DiscX, DiscY, DiscR, 0, Math.PI * 2);
  Ctx.fill();
  Ctx.stroke();

  const Frac = Clamp((Moon.PhaseDays ?? 8.9) / 29.53, 0, 1);
  const Wax = Frac < 0.5;
  const RX = Math.max(0.01, Math.abs(Math.cos(Frac * Math.PI * 2)) * DiscR);
  Ctx.save();
  Ctx.beginPath();
  Ctx.arc(DiscX, DiscY, DiscR, -Math.PI / 2, Math.PI / 2, !Wax);
  Ctx.ellipse(DiscX, DiscY, RX, DiscR, 0, Math.PI / 2, -Math.PI / 2, !(Wax ? Frac < 0.25 : Frac >= 0.75));
  Ctx.closePath();
  Ctx.clip();
  const MBody = Ctx.createRadialGradient(DiscX - 18, DiscY - 22, 4, DiscX, DiscY, DiscR * 1.2);
  MBody.addColorStop(0, "#dce2e4");
  MBody.addColorStop(0.65, "#a5b0bc");
  MBody.addColorStop(1, "#596b81");
  Ctx.fillStyle = MBody;
  Ctx.fillRect(DiscX - DiscR, DiscY - DiscR, DiscR * 2, DiscR * 2);
  Ctx.restore();

  Ctx.fillStyle = "#869bb6";
  Ctx.font = "9px 'IBM Plex Mono', monospace";
  Ctx.textAlign = "center";
  Ctx.fillText(`${Metrics.PhaseLabel} · ${(Moon.AngularDiameterDeg ?? 0.52).toFixed(2)}°`, DiscX, 198);

  // Right: Verbatim LunarSphere celestial orbital dial
  const CX = 258;
  const CY = 114;
  const R = 74;
  const Tilt = 0.34;
  const Project = (lon, lat) => {
    const a = (lon * Math.PI) / 180;
    const b = (lat * Math.PI) / 180;
    const x = Math.sin(a) * Math.cos(b);
    const z = Math.cos(a) * Math.cos(b);
    const y = Math.sin(b);
    return [
      CX + R * x,
      CY - R * (y * Math.cos(Tilt) - z * Math.sin(Tilt)),
      z * Math.cos(Tilt) + y * Math.sin(Tilt),
    ];
  };

  const Globe = Ctx.createRadialGradient(CX - R * 0.4, CY - R * 0.4, 4, CX, CY, R);
  Globe.addColorStop(0, "#455263");
  Globe.addColorStop(0.6, "#252f3c");
  Globe.addColorStop(1, "#151b25");
  Ctx.fillStyle = Globe;
  Ctx.strokeStyle = "rgba(120,151,186,0.34)";
  Ctx.beginPath();
  Ctx.arc(CX, CY, R, 0, Math.PI * 2);
  Ctx.fill();
  Ctx.stroke();

  [-60, -30, 0, 30, 60].forEach((lat) => {
    Ctx.strokeStyle = lat === 0 ? "rgba(181,213,240,0.55)" : "rgba(122,153,181,0.2)";
    Ctx.lineWidth = lat === 0 ? 1.1 : 0.6;
    Ctx.beginPath();
    for (let i = 0; i <= 60; i++) {
      const [px, py] = Project(i * 6, lat);
      if (i === 0) Ctx.moveTo(px, py);
      else Ctx.lineTo(px, py);
    }
    Ctx.stroke();
  });

  const [MX, MY, MZ] = Project(Moon.AzimuthDeg ?? 78, Moon.ElevationDeg ?? 28);
  Ctx.save();
  if (MZ < 0) Ctx.setLineDash([3, 3]);
  Ctx.strokeStyle = "#aacfeb";
  Ctx.lineWidth = 1.3;
  Ctx.beginPath();
  for (let i = 0; i <= 24; i++) {
    const [px, py] = Project(Moon.AzimuthDeg ?? 78, (i * (Moon.ElevationDeg ?? 28)) / 24);
    if (i === 0) Ctx.moveTo(px, py);
    else Ctx.lineTo(px, py);
  }
  Ctx.stroke();
  Ctx.restore();

  Ctx.fillStyle = "#e3edf5";
  Ctx.beginPath();
  Ctx.arc(MX, MY, 5.5, 0, Math.PI * 2);
  Ctx.fill();
  Ctx.fillText(`Az ${Math.round(Moon.AzimuthDeg ?? 78)}° · El ${Math.round(Moon.ElevationDeg ?? 28)}°`, CX, 198);
}

// ── 6. VERBATIM PROJECT ZERO 300-STAR FIELD & TWINKLE SIGNAL ──────────────────
export function DrawStarsAndTwinkleCanvas(Canvas, Stars, TimeSec = 0) {
  const { Ctx, W, H } = EnsureHiDpiCanvas(Canvas, 360, 224);
  Ctx.clearRect(0, 0, W, H);

  // Top half: 300-star equatorial field
  Ctx.fillStyle = "#16171a";
  Ctx.beginPath();
  Ctx.roundRect(8, 8, W - 16, 114, 8);
  Ctx.fill();

  const Limit = Clamp(Stars?.LimitingMagnitude ?? 5.5, 0.5, 9);
  const Rot = ((Stars?.CelestialRotationDeg ?? 45) * Math.PI) / 180;
  const CX = W / 2;
  const CY = 65;

  for (let Index = 0; Index < 300; Index++) {
    const H1 = (Index * 0.61803398875) % 1;
    const H2 = (Index * 0.7548776662) % 1;
    const H3 = (Index * 0.5698402909) % 1;
    const Mag = 0.5 + H3 * 8.2;
    if (Mag > Limit) continue;
    const Dx = (H1 - 0.5) * (W - 44);
    const Dy = (H2 - 0.5) * 94;
    const X = CX + Dx * Math.cos(Rot) - Dy * Math.sin(Rot);
    const Y = CY + Dx * Math.sin(Rot) + Dy * Math.cos(Rot);
    if (Y < 12 || Y > 118) continue;
    Ctx.globalAlpha = Clamp(1 - Mag / (Limit + 0.5), 0.15, 1);
    Ctx.fillStyle = Index % 7 === 0 ? "#ffd9a8" : Index % 5 === 0 ? "#b8d6ff" : "#ebe6f6";
    Ctx.beginPath();
    Ctx.arc(X, Y, Mag < 2.4 ? 1.8 : 0.9, 0, Math.PI * 2);
    Ctx.fill();
  }
  Ctx.globalAlpha = 1;

  // Bottom half: Verbatim TwinkleSignal trace
  const Depth = Stars?.TwinkleEnabled ? Stars?.TwinkleDepthPct ?? 30 : 0;
  const Rate = Stars?.TwinkleRateHz ?? 1.2;
  const FluxAt = (t) => 1 - (Depth / 100) * (0.5 + 0.3 * Math.sin(t * Math.PI * 2 * Rate) + 0.2 * Math.sin(t * Math.PI * 2 * Rate * 0.47 + 0.8));

  Ctx.fillStyle = "#19171d";
  Ctx.strokeStyle = "rgba(255,255,255,0.06)";
  Ctx.beginPath();
  Ctx.roundRect(8, 130, W - 16, 72, 8);
  Ctx.fill();
  Ctx.stroke();

  Ctx.strokeStyle = "#c8a4df";
  Ctx.lineWidth = 1.4;
  Ctx.beginPath();
  for (let i = 0; i <= 100; i++) {
    const V = FluxAt(TimeSec - 4 + i / 25);
    const X = 18 + (i / 100) * (W - 36);
    const Y = 142 + (1 - V) * 46;
    if (i === 0) Ctx.moveTo(X, Y);
    else Ctx.lineTo(X, Y);
  }
  Ctx.stroke();

  const NowF = FluxAt(TimeSec);
  Ctx.fillStyle = "#e0d1ea";
  Ctx.font = "9px 'IBM Plex Mono', monospace";
  Ctx.fillText(`TWINKLE FLUX · ${Math.round(NowF * 100)}%`, 16, 218);
}

// ── 7. VERBATIM PROJECT ZERO LENS FLARE COMPOSITE DIAGRAM ─────────────────────
export function DrawLensFlareCanvas(Canvas, Flare) {
  const { Ctx, W, H } = EnsureHiDpiCanvas(Canvas, 340, 158);
  Ctx.clearRect(0, 0, W, H);
  Ctx.fillStyle = "#16171a";
  Ctx.fillRect(0, 0, W, H);
  if (Flare?.Enabled === false) return;

  const SX = (Flare?.PreviewX ?? 0.28) * W;
  const SY = (Flare?.PreviewY ?? 0.43) * H;
  const CX = W * 0.5;
  const CY = H * 0.5;
  const DX = CX - SX;
  const DY = CY - SY;
  const Bright = Clamp((Flare?.Intensity ?? 1) * 0.7, 0.15, 1);

  const SunG = Ctx.createRadialGradient(SX, SY, 1, SX, SY, 52 * Bright);
  SunG.addColorStop(0, `rgba(255,244,210,${0.9 * Bright})`);
  SunG.addColorStop(0.28, `rgba(255,210,145,${0.42 * Bright})`);
  SunG.addColorStop(1, "rgba(183,69,40,0)");
  Ctx.fillStyle = SunG;
  Ctx.beginPath();
  Ctx.arc(SX, SY, 52 * Bright, 0, Math.PI * 2);
  Ctx.fill();

  if (Flare?.Anamorphic) {
    const Str = Ctx.createLinearGradient(0, SY, W, SY);
    Str.addColorStop(0, "rgba(130,167,203,0)");
    Str.addColorStop(Clamp(SX / W, 0.1, 0.9), `rgba(255,248,229,${0.88 * Bright})`);
    Str.addColorStop(1, "rgba(113,141,187,0)");
    Ctx.strokeStyle = Str;
    Ctx.lineWidth = 1.5;
    Ctx.beginPath();
    Ctx.moveTo(8, SY);
    Ctx.lineTo(W - 8, SY);
    Ctx.stroke();
  }

  const Ghosts = Flare?.GhostCount ?? 6;
  for (let i = 1; i <= Ghosts; i++) {
    const T = (i / Math.max(1, Ghosts)) * 1.9;
    const GX = SX + DX * T;
    const GY = SY + DY * T;
    const R = 6 + ((i * 5) % 18) * (Flare?.HaloRadius ?? 0.28) * 2.5;
    Ctx.fillStyle = i % 2 === 0 ? `rgba(121,184,220,${0.18 * (Flare?.GhostBrightness ?? 0.35) * 2})` : `rgba(182,204,131,${0.16 * (Flare?.GhostBrightness ?? 0.35) * 2})`;
    Ctx.beginPath();
    Ctx.arc(GX, GY, R, 0, Math.PI * 2);
    Ctx.fill();
  }
}

// ── 8. VERBATIM PROJECT ZERO SCATTERING GRAPH & FOG BEAM CHAMBER ──────────────
export function DrawAtmosphereScatteringCanvas(Canvas, Atmo) {
  const { Ctx, W, H } = EnsureHiDpiCanvas(Canvas, 340, 146);
  Ctx.clearRect(0, 0, W, H);
  const Rayleigh = Clamp(Atmo?.Rayleigh ?? 1.0, 0, 3);
  const Mie = Clamp(Atmo?.Mie ?? 1.0, 0, 3);

  Ctx.save();
  Ctx.setLineDash([2, 6]);
  Ctx.strokeStyle = "rgba(255,255,255,0.08)";
  [24, 52, 80, 108].forEach((y) => {
    Ctx.beginPath();
    Ctx.moveTo(16, y);
    Ctx.lineTo(W - 16, y);
    Ctx.stroke();
  });
  Ctx.restore();

  const Fill = Ctx.createLinearGradient(0, 18, 0, 114);
  Fill.addColorStop(0, "rgba(145,175,210,0.22)");
  Fill.addColorStop(1, "rgba(145,175,210,0)");
  const StartY = 104 - Rayleigh * 28;
  Ctx.beginPath();
  Ctx.moveTo(16, StartY);
  Ctx.bezierCurveTo(82, StartY + 6, 94, 92, 186, 100);
  Ctx.bezierCurveTo(250, 106, 290, 108, W - 16, 109);
  Ctx.lineTo(W - 16, 114);
  Ctx.lineTo(16, 114);
  Ctx.closePath();
  Ctx.fillStyle = Fill;
  Ctx.fill();

  Ctx.strokeStyle = "#a1b9d8";
  Ctx.lineWidth = 1.8;
  Ctx.beginPath();
  Ctx.moveTo(16, StartY);
  Ctx.bezierCurveTo(82, StartY + 6, 94, 92, 186, 100);
  Ctx.bezierCurveTo(250, 106, 290, 108, W - 16, 109);
  Ctx.stroke();

  Ctx.save();
  Ctx.setLineDash([5, 5]);
  Ctx.strokeStyle = "#ccba95";
  Ctx.lineWidth = 1.2;
  Ctx.beginPath();
  Ctx.moveTo(16, 108 - Mie * 20);
  Ctx.quadraticCurveTo(W / 2, 110 - Mie * 16, W - 16, 112 - Mie * 12);
  Ctx.stroke();
  Ctx.restore();

  const Spec = Ctx.createLinearGradient(16, 0, W - 16, 0);
  Spec.addColorStop(0, "#927fb0");
  Spec.addColorStop(0.25, "#7f9dc9");
  Spec.addColorStop(0.5, "#94b5a9");
  Spec.addColorStop(0.75, "#c5b27e");
  Spec.addColorStop(1, "#c28b7c");
  Ctx.fillStyle = Spec;
  Ctx.fillRect(16, 122, W - 32, 3);

  Ctx.fillStyle = "#858585";
  Ctx.font = "9px 'IBM Plex Mono', monospace";
  Ctx.fillText("380 nm", 16, 138);
  Ctx.textAlign = "center";
  Ctx.fillText("Visible spectrum", W / 2, 138);
  Ctx.textAlign = "right";
  Ctx.fillText("780 nm", W - 16, 138);
}

export function DrawHeightFogBeamCanvas(Canvas, Fog) {
  const { Ctx, W, H } = EnsureHiDpiCanvas(Canvas, 320, 104);
  Ctx.clearRect(0, 0, W, H);
  const Extinction = Math.max(0.0005, Fog?.Density ?? 0.024);
  const Spread = Clamp(Fog?.SunScatter ?? 0.6, 0, 1);
  const DistMax = 120;
  const Tint = MixTint(Fog?.Tint || "#d4e3ec", null, 0);
  const Range = -Math.log(0.02) / Extinction;
  const Marker = 14 + Clamp(Range / DistMax, 0, 1) * (W - 28);

  Ctx.save();
  Ctx.setLineDash([2, 5]);
  Ctx.strokeStyle = "rgba(255,255,255,0.06)";
  [26, 52, 78].forEach((y) => {
    Ctx.beginPath();
    Ctx.moveTo(14, y);
    Ctx.lineTo(W - 14, y);
    Ctx.stroke();
  });
  Ctx.restore();

  for (let i = 0; i < 68; i++) {
    const Frac = i / 67;
    const D = Frac * DistMax;
    const T = Math.exp(-Extinction * D);
    const Exp = 0.32 + 0.68 * Math.pow(T, 0.15);
    const Half = 3 + Frac * Frac * (11 + Spread * 24);
    Ctx.fillStyle = Rgba(Tint, (0.12 + Spread * 0.22) * Exp);
    Ctx.fillRect(14 + Frac * (W - 32), 52 - Half, 4.5, Half * 2);
  }

  Ctx.strokeStyle = Rgba(Tint, 0.85);
  Ctx.lineWidth = 1.5;
  Ctx.beginPath();
  Ctx.moveTo(14, 52);
  Ctx.lineTo(W - 14, 52);
  Ctx.stroke();

  Ctx.save();
  Ctx.setLineDash([3, 3]);
  Ctx.strokeStyle = "rgba(237,143,143,0.78)";
  Ctx.beginPath();
  Ctx.moveTo(Marker, 18);
  Ctx.lineTo(Marker, 86);
  Ctx.stroke();
  Ctx.restore();

  Ctx.fillStyle = "#fff1ca";
  Ctx.beginPath();
  Ctx.arc(14, 52, 4, 0, Math.PI * 2);
  Ctx.fill();

  Ctx.fillStyle = "rgba(255,255,255,0.5)";
  Ctx.font = "8.5px 'IBM Plex Mono', monospace";
  Ctx.fillText(`SCATTER · ${Spread.toFixed(2)}`, 12, 13);
  Ctx.textAlign = "right";
  Ctx.fillText(`2% · ${Range >= 1000 ? (Range / 1000).toFixed(1) + " km" : Range.toFixed(0) + " m"}`, W - 12, 13);
  Ctx.fillText(`${Math.round(Math.exp(-Extinction * DistMax) * 100)}% AT ${DistMax} m`, W - 12, 98);
}

// Keep alias exports cleanly
export const DrawLightPreviewDiagram = DrawHeroLightCanvas;
export const DrawLightResponseCurve = DrawResponseCurveCanvas;
export const DrawPolarPhotometry = DrawIesPolarWebCanvas;
export const DrawSunOrbitGizmo = DrawSolarArcCanvas;
export const DrawAtmosphereScatterChart = DrawAtmosphereScatteringCanvas;
export const DrawFogChamberCanvas = DrawHeightFogBeamCanvas;
