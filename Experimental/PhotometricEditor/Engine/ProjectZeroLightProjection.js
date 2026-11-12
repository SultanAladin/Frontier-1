// ProjectZeroLightProjection.js — Exact verbatim architectural & photometric diagrams
// ported from Project Zero Editor (LightProjection.js & LightSpecification.js).
// Includes optional measured IES LM-63 sampling integration for active IES files.

export const LightIcons = {
  pointlight: "editor-point-light",
  spotlight: "editor-spotlight",
  ieslight: "editor-dome-light",
  arealight: "editor-area-light",
  tubelight: "light-area-2d",
  ledlight: "light-point-2d",
  ledstrip: "slate-ring-light",
};

export const EnvironmentIcons = {
  hdri: "editor-hdri-light",
  sun: "sun",
  moon: "moon",
  stars: "outliner-stars",
  flare: "lens-flare",
  atmosphere: "sky-scattering",
  fog: "fog",
  clouds: "clouds",
  post: "environment-exposure",
};

export const FormatNumber = (Value, Decimals = 0) =>
  Number(Value || 0).toLocaleString("en-US", {
    minimumFractionDigits: Decimals,
    maximumFractionDigits: Decimals,
  });

export const Clamp = (Value, Min, Max) => Math.max(Min, Math.min(Max, Value));

export const KelvinRgb = (Kelvin) => {
  const T = Clamp(Number(Kelvin) || 6500, 1000, 20000) / 100;
  const R =
    T <= 66
      ? 255
      : Clamp(329.698727446 * Math.pow(T - 60, -0.1332047592), 0, 255);
  const G =
    T <= 66
      ? Clamp(99.4708025861 * Math.log(T) - 161.1195681661, 0, 255)
      : Clamp(288.1221695283 * Math.pow(T - 60, -0.0755148492), 0, 255);
  const B =
    T >= 66
      ? 255
      : T <= 19
        ? 0
        : Clamp(138.5177312231 * Math.log(T - 10) - 305.0447927307, 0, 255);
  return { r: Math.round(R), g: Math.round(G), b: Math.round(B) };
};

export const MixTint = (Hex, Kelvin, Blend = 0.55) => {
  const SafeHex = String(Hex || "#ffffff").replace("#", "");
  const Clean = SafeHex.length === 6 ? SafeHex : "ffffff";
  const Base = {
    r: parseInt(Clean.slice(0, 2), 16) || 255,
    g: parseInt(Clean.slice(2, 4), 16) || 255,
    b: parseInt(Clean.slice(4, 6), 16) || 255,
  };
  if (Kelvin == null) return Base;
  const K = KelvinRgb(Kelvin);
  return {
    r: Math.round((Base.r * K.r) / 255 * (1 - Blend) + K.r * Blend),
    g: Math.round((Base.g * K.g) / 255 * (1 - Blend) + K.g * Blend),
    b: Math.round((Base.b * K.b) / 255 * (1 - Blend) + K.b * Blend),
  };
};

export const Rgba = (Color, Alpha) =>
  `rgba(${Color.r},${Color.g},${Color.b},${Clamp(Alpha, 0, 1).toFixed(3)})`;

export function BlendColour(Hex, Kelvin, UseTemp = true) {
  const Base = MixTint(Hex, UseTemp ? Kelvin : null, 0.55);
  const Peak = Math.max(1, Base.r, Base.g, Base.b);
  return {
    r: Math.round(255 * 0.25 + (Base.r / Peak) * 255 * 0.75),
    g: Math.round(255 * 0.25 + (Base.g / Peak) * 255 * 0.75),
    b: Math.round(255 * 0.25 + (Base.b / Peak) * 255 * 0.75),
  };
}

export const DefaultProfiles = [
  "Downlight",
  "Wall wash",
  "Batwing",
  "ECE Low Beam",
  "SAE Low Beam",
  "High Beam",
  "Fog Lamp",
  "Parking Lamp",
];

// Normalized 0..1 distribution shapes for preset index / angle
export function SampleIesShape(Index, AngleDeg, FieldDeg, CutOffDeg) {
  const Rad = Math.abs(AngleDeg) * (Math.PI / 180);
  const HalfField = Math.max(6, (FieldDeg || 58) * 0.5);
  const U = Math.abs(AngleDeg) / HalfField;
  switch (Index) {
    case 1: {
      // Wall wash
      const Offset = ((AngleDeg - 18) / HalfField);
      return Clamp(Math.exp(-Offset * Offset * 1.45) * 0.94, 0, 1);
    }
    case 2: {
      // Batwing twin lobe
      const Wing = Math.abs(Math.abs(AngleDeg) - HalfField * 0.52) / (HalfField * 0.36);
      return Clamp(Math.exp(-Wing * Wing) * 0.98 + Math.exp(-U * U * 3) * 0.36, 0, 1);
    }
    case 3: // ECE Low Beam
    case 4: {
      // SAE Low Beam
      const Cutoff = CutOffDeg != null ? CutOffDeg : -1.0;
      const Edge = Clamp(1 - Math.max(0, AngleDeg - Cutoff) / 3.4, 0, 1);
      const Side = Math.exp(-Math.pow((AngleDeg + 14) / (HalfField * 0.65), 2));
      return Clamp(Edge * (0.28 + 0.72 * Side), 0, 1);
    }
    case 5: {
      // High Beam (tight pencil throw)
      const Beam = Math.exp(-Math.pow(AngleDeg / Math.max(5, HalfField * 0.34), 2));
      const Spill = Math.exp(-U * U * 1.6) * 0.28;
      return Clamp(Beam * 0.95 + Spill, 0, 1);
    }
    case 6: {
      // Fog Lamp (wide shallow horizontal blade)
      const Edge = Clamp(1 - Math.max(0, AngleDeg - (CutOffDeg ?? -2)) / 2.6, 0, 1);
      const Spread = Math.exp(-Math.pow(AngleDeg / Math.max(12, HalfField * 0.85), 2));
      return Clamp(Edge * Spread, 0, 1);
    }
    case 7: {
      // Parking Lamp (broad cosine)
      return Clamp(Math.pow(Math.max(0, Math.cos(Rad * 0.9)), 1.2) * 0.75, 0, 1);
    }
    case 0:
    default: {
      // Downlight
      return Clamp(Math.exp(-U * U * 2.1) * Math.max(0, Math.cos(Rad)), 0, 1);
    }
  }
}

export function IesWeight(Properties, AngleDeg, PhiDeg = 0) {
  if (Properties?.MeasuredSampleFn) {
    return Clamp(Properties.MeasuredSampleFn(Math.abs(AngleDeg), PhiDeg), 0, 1);
  }
  const Index = Number(Properties?.["Profile type"] ?? 0);
  const Field = Number(Properties?.["Field angle"] ?? 58);
  const CutOff = Number(Properties?.["Cut-off pitch"] ?? -1);
  return SampleIesShape(Index, AngleDeg, Field, CutOff);
}

const StrokeLine = (B, X1, Y1, X2, Y2, Width = 1) => {
  B.lineWidth = Width;
  B.beginPath();
  B.moveTo(X1, Y1);
  B.lineTo(X2, Y2);
  B.stroke();
};

const DrawLabel = (B, Text, X, Y, Color = "rgba(255,255,255,0.46)", Align = "left", Size = 8.5) => {
  B.fillStyle = Color;
  B.font = `${Size}px 'IBM Plex Mono', monospace`;
  B.textAlign = Align;
  B.fillText(Text, X, Y);
};

const DrawDimension = (B, X1, Y1, X2, Y2, Text, Offset = 12, Color = "rgba(255,255,255,0.42)") => {
  B.save();
  B.strokeStyle = Color;
  B.lineWidth = 0.85;
  const Dx = X2 - X1;
  const Dy = Y2 - Y1;
  const Length = Math.hypot(Dx, Dy) || 1;
  const Nx = (-Dy / Length) * Offset;
  const Ny = (Dx / Length) * Offset;
  const Ax = X1 + Nx;
  const Ay = Y1 + Ny;
  const Bx = X2 + Nx;
  const By = Y2 + Ny;
  StrokeLine(B, Ax, Ay, Bx, By, 0.85);
  const TickX = (Nx / (Math.abs(Offset) || 1)) * 3.5;
  const TickY = (Ny / (Math.abs(Offset) || 1)) * 3.5;
  StrokeLine(B, Ax - TickX, Ay - TickY, Ax + TickX, Ay + TickY, 0.85);
  StrokeLine(B, Bx - TickX, By - TickY, Bx + TickX, By + TickY, 0.85);
  DrawLabel(
    B,
    Text,
    (Ax + Bx) / 2,
    (Ay + By) / 2 + (Math.abs(Dx) >= Math.abs(Dy) ? (Offset > 0 ? 13 : -6) : 3),
    "rgba(255,255,255,0.52)",
    Math.abs(Dx) >= Math.abs(Dy) ? "center" : Offset > 0 ? "left" : "right",
    8,
  );
  B.restore();
};

const DrawCrosshair = (B, CX, CY, W, H) => {
  B.save();
  B.strokeStyle = "rgba(255,255,255,0.07)";
  B.setLineDash([2, 4]);
  StrokeLine(B, 14, CY, W - 14, CY, 0.8);
  StrokeLine(B, CX, 14, CX, H - 14, 0.8);
  B.setLineDash([]);
  B.restore();
};

// =========================================================================
// VERBATIM PROJECT ZERO PREVIEW ILLUSTRATION (380x260 Blueprint Card)
// =========================================================================
export function ProjectLight(Brush, Width, Height, Model) {
  const { Style, Read, Properties, Tint, Flux } = Model;
  Brush.save();
  Brush.clearRect(0, 0, Width, Height);

  const W = 380;
  const H = 260;
  const S = Math.min(Width / W, Height / H);
  Brush.translate((Width - W * S) / 2, (Height - H * S) / 2);
  Brush.scale(S, S);

  // Blueprint drafting ground
  Brush.strokeStyle = "rgba(255,255,255,0.03)";
  Brush.lineWidth = 0.8;
  for (let X = 24; X < W; X += 24) StrokeLine(Brush, X, 12, X, H - 12, 0.7);
  for (let Y = 24; Y < H; Y += 24) StrokeLine(Brush, 12, Y, W - 12, Y, 0.7);

  if (Style === "pointlight") {
    const CX = W / 2;
    const CY = 128;
    const Reach = Read("Distance", 26);
    const Decay = Read("Decay", 2.0);
    const Energy = Clamp((Read("Intensity", 14) / 80) * 0.8 + 0.25, 0.25, 1);
    DrawCrosshair(Brush, CX, CY, W, H);

    const Radii = [30, 52, 74, 96];
    Radii.forEach((Radius, Index) => {
      const Norm = (Index + 1) / Radii.length;
      const Fall = Math.pow(1 - Norm * 0.72, Math.max(0.5, Decay * 0.55));
      Brush.strokeStyle =
        Index === Radii.length - 1
          ? Rgba(Tint, 0.26)
          : Rgba(Tint, Clamp(0.12 + Energy * Fall * 0.28, 0.08, 0.4));
      Brush.lineWidth = Index === 0 ? 1.1 : 0.85;
      if (Index === Radii.length - 1) Brush.setLineDash([3, 3]);
      Brush.beginPath();
      Brush.arc(CX, CY, Radius, 0, Math.PI * 2);
      Brush.stroke();
      Brush.setLineDash([]);
      const Dist = ((Index + 1) * Reach) / Radii.length;
      DrawLabel(
        Brush,
        `${Math.round(Dist)} m`,
        CX + Radius + 3,
        CY + 3,
        "rgba(255,255,255,0.42)",
        "left",
        7.5,
      );
    });

    for (let K = 0; K < 12; K++) {
      const A = (K * Math.PI * 2) / 12;
      Brush.strokeStyle = Rgba(Tint, 0.2 * Energy);
      Brush.setLineDash([2, 4]);
      StrokeLine(
        Brush,
        CX + Math.cos(A) * 14,
        CY + Math.sin(A) * 14,
        CX + Math.cos(A) * 88,
        CY + Math.sin(A) * 88,
        0.9,
      );
      Brush.setLineDash([]);
    }

    const Glow = Brush.createRadialGradient(CX, CY, 1, CX, CY, 34);
    Glow.addColorStop(0, Rgba(Tint, 0.42 * Energy));
    Glow.addColorStop(1, "rgba(0,0,0,0)");
    Brush.fillStyle = Glow;
    Brush.beginPath();
    Brush.arc(CX, CY, 34, 0, Math.PI * 2);
    Brush.fill();

    Brush.fillStyle = Rgba(Tint, 0.94);
    Brush.beginPath();
    Brush.arc(CX, CY, 6.2, 0, Math.PI * 2);
    Brush.fill();
    Brush.strokeStyle = "#ffffff";
    Brush.lineWidth = 1;
    Brush.stroke();

    DrawLabel(Brush, "OMNIDIRECTIONAL", 18, 22, "rgba(255,255,255,0.45)", "left", 8);
    DrawLabel(
      Brush,
      `DECAY ${Decay.toFixed(1)}`,
      W - 18,
      22,
      "rgba(255,255,255,0.45)",
      "right",
      8,
    );
    DrawLabel(
      Brush,
      "Reach guides · free space",
      CX,
      H - 18,
      "rgba(255,255,255,0.38)",
      "center",
      8,
    );
  } else if (Style === "spotlight") {
    const TopY = 48;
    const BottomY = 202;
    const OuterHalfDeg = Clamp(Read("Outer cone", 26) / 2, 3, 72);
    const Soft = Clamp(Read("Penumbra", 0.42), 0, 1);
    const InnerHalfDeg = OuterHalfDeg * (1 - Soft * 0.72);
    const CX = W / 2;
    const ReachPx = BottomY - TopY;
    const OuterW = Math.min(138, Math.tan((OuterHalfDeg * Math.PI) / 180) * ReachPx * 1.15);
    const InnerW = Math.tan((InnerHalfDeg * Math.PI) / 180) * ReachPx * 1.15;

    DrawCrosshair(Brush, CX, 130, W, H);

    // Fixture hood
    Brush.fillStyle = "#262825";
    Brush.strokeStyle = "rgba(255,255,255,0.55)";
    Brush.lineWidth = 1.1;
    Brush.beginPath();
    Brush.roundRect(CX - 18, TopY - 22, 36, 20, 4);
    Brush.fill();
    Brush.stroke();

    const ConeWash = Brush.createLinearGradient(CX, TopY, CX, BottomY);
    ConeWash.addColorStop(0, Rgba(Tint, 0.28));
    ConeWash.addColorStop(0.65, Rgba(Tint, 0.08));
    ConeWash.addColorStop(1, Rgba(Tint, 0.02));
    Brush.fillStyle = ConeWash;
    Brush.beginPath();
    Brush.moveTo(CX, TopY);
    Brush.lineTo(CX - OuterW, BottomY);
    Brush.lineTo(CX + OuterW, BottomY);
    Brush.closePath();
    Brush.fill();

    Brush.strokeStyle = Rgba(Tint, 0.8);
    Brush.lineWidth = 1.2;
    Brush.beginPath();
    Brush.moveTo(CX, TopY);
    Brush.lineTo(CX - OuterW, BottomY);
    Brush.lineTo(CX + OuterW, BottomY);
    Brush.closePath();
    Brush.stroke();

    Brush.strokeStyle = "rgba(255,255,255,0.65)";
    Brush.setLineDash([4, 4]);
    StrokeLine(Brush, CX, TopY, CX - InnerW, BottomY, 1);
    StrokeLine(Brush, CX, TopY, CX + InnerW, BottomY, 1);
    Brush.setLineDash([]);

    DrawDimension(
      Brush,
      CX - OuterW,
      BottomY,
      CX + OuterW,
      BottomY,
      `${Math.round(OuterHalfDeg * 2)}° FULL CONE`,
      16,
    );

    DrawLabel(Brush, "PENUMBRA", 18, 22, "rgba(255,255,255,0.45)", "left", 8);
    DrawLabel(
      Brush,
      `${Math.round(Soft * 100)} %`,
      W - 18,
      22,
      "rgba(255,255,255,0.45)",
      "right",
      8,
    );
  } else if (Style === "ieslight") {
    const CX = W / 2;
    const CY = 128;
    const MaxR = 92;
    const Profile = Read("Profile type", 3);
    const ProfileLabel = Properties?.MeasuredName
      ? Properties.MeasuredName.toUpperCase()
      : (DefaultProfiles[Profile] || "IES PRESET").toUpperCase();

    [0.33, 0.66, 1].forEach((Scale) => {
      Brush.strokeStyle = "rgba(255,255,255,0.09)";
      Brush.lineWidth = 0.85;
      Brush.beginPath();
      Brush.arc(CX, CY, MaxR * Scale, 0, Math.PI * 2);
      Brush.stroke();
    });

    Brush.strokeStyle = "rgba(255,255,255,0.08)";
    Brush.setLineDash([2, 4]);
    StrokeLine(Brush, CX - MaxR - 12, CY, CX + MaxR + 12, CY, 0.85);
    StrokeLine(Brush, CX, CY - MaxR - 10, CX, CY + MaxR + 10, 0.85);
    Brush.setLineDash([]);

    // C90 plane subtle curve
    Brush.beginPath();
    for (let A = -90; A <= 90; A += 3) {
      const Weight = IesWeight(Properties, A, 90);
      const R = 10 + Weight * (MaxR - 14);
      const Theta = (Math.PI / 2) - (A * Math.PI) / 180;
      const X = CX + Math.cos(Theta) * R;
      const Y = CY + Math.sin(Theta) * R * 0.92;
      if (A === -90) Brush.moveTo(X, Y);
      else Brush.lineTo(X, Y);
    }
    Brush.closePath();
    Brush.fillStyle = "rgba(180,210,235,0.08)";
    Brush.fill();
    Brush.strokeStyle = "rgba(180,210,235,0.45)";
    Brush.lineWidth = 1;
    Brush.stroke();

    // C0 primary plane
    Brush.beginPath();
    for (let A = -90; A <= 90; A += 3) {
      const Weight = IesWeight(Properties, A, 0);
      const R = 10 + Weight * (MaxR - 14);
      const Theta = (Math.PI / 2) + (A * Math.PI) / 180;
      const X = CX + Math.cos(Theta) * R;
      const Y = CY + Math.sin(Theta) * R * 0.92;
      if (A === -90) Brush.moveTo(X, Y);
      else Brush.lineTo(X, Y);
    }
    Brush.closePath();
    Brush.fillStyle = Rgba(Tint, 0.16);
    Brush.fill();
    Brush.strokeStyle = Rgba(Tint, 0.88);
    Brush.lineWidth = 1.35;
    Brush.stroke();

    Brush.fillStyle = Rgba(Tint, 0.95);
    Brush.beginPath();
    Brush.arc(CX, CY, 3.2, 0, Math.PI * 2);
    Brush.fill();

    DrawLabel(Brush, "C0", 18, 22, Rgba(Tint, 0.72), "left", 8);
    DrawLabel(Brush, "C90", W - 18, 22, "rgba(180,210,235,0.62)", "right", 8);
    DrawLabel(Brush, "0°", CX, CY + MaxR + 14, "rgba(255,255,255,0.4)", "center", 8);
    DrawLabel(Brush, "90°", CX - MaxR - 18, CY + 3, "rgba(255,255,255,0.34)", "right", 7.5);
    DrawLabel(Brush, "90°", CX + MaxR + 18, CY + 3, "rgba(255,255,255,0.34)", "left", 7.5);
    DrawLabel(
      Brush,
      Properties?.MeasuredName ? `MEASURED · ${ProfileLabel}` : "NORMALIZED PRESET",
      CX,
      H - 12,
      "rgba(255,255,255,0.38)",
      "center",
      7.5,
    );
  } else if (Style === "arealight") {
    const CX = W / 2;
    const CY = 122;
    const Shape = Read("Shape", 0) === 1;
    const WVal = Clamp(Read("Width", 2), 0.25, 8);
    const HVal = Clamp(Read("Height", 1), 0.25, 6);
    const RVal = Clamp(Read("Disk radius", 0.9), 0.15, 4);
    const Dual = !!Properties?.["Two sided"];
    const ScalePx = 86;

    DrawCrosshair(Brush, CX, CY, W, H);
    DrawLabel(Brush, "APERTURE / FRONT ELEVATION", 18, 22, "rgba(255,255,255,0.45)", "left", 8);

    if (Shape) {
      const Rad = Clamp(RVal * ScalePx * 0.5, 22, 74);
      Brush.fillStyle = Rgba(Tint, 0.14);
      Brush.strokeStyle = Rgba(Tint, 0.85);
      Brush.lineWidth = 1.4;
      Brush.beginPath();
      Brush.arc(CX, CY, Rad, 0, Math.PI * 2);
      Brush.fill();
      Brush.stroke();
      DrawDimension(
        Brush,
        CX - Rad,
        CY + Rad + 6,
        CX + Rad,
        CY + Rad + 6,
        `D = ${(RVal * 2).toFixed(2)} m`,
        16,
      );
    } else {
      const RW = Clamp((WVal / Math.max(WVal, HVal, 1.5)) * 188, 56, 210);
      const RH = Clamp((HVal / Math.max(WVal, HVal, 1.5)) * 130, 44, 134);
      Brush.fillStyle = Rgba(Tint, 0.12);
      Brush.strokeStyle = Rgba(Tint, 0.85);
      Brush.lineWidth = 1.4;
      Brush.strokeRect(CX - RW / 2, CY - RH / 2, RW, RH);
      Brush.fillRect(CX - RW / 2, CY - RH / 2, RW, RH);

      Brush.fillStyle = Rgba(Tint, 0.32);
      for (let X = CX - RW / 2 + 12; X < CX + RW / 2 - 4; X += 16) {
        for (let Y = CY - RH / 2 + 12; Y < CY + RH / 2 - 4; Y += 16) {
          Brush.fillRect(X, Y, 1.5, 1.5);
        }
      }

      DrawDimension(Brush, CX - RW / 2, CY + RH / 2, CX + RW / 2, CY + RH / 2, `${WVal.toFixed(1)} m`, 16);
      DrawDimension(Brush, CX + RW / 2, CY - RH / 2, CX + RW / 2, CY + RH / 2, `${HVal.toFixed(1)} m`, 16);
    }

    DrawLabel(
      Brush,
      Dual ? "TWO-SIDED EMISSION" : "FRONT FACE",
      18,
      H - 16,
      "rgba(255,255,255,0.42)",
      "left",
      8,
    );
    DrawLabel(
      Brush,
      `${Math.round(Read("Spread", 120))}° SPREAD`,
      W - 18,
      H - 16,
      "rgba(255,255,255,0.42)",
      "right",
      8,
    );
  } else if (Style === "tubelight") {
    const CX = W / 2;
    const CY = 114;
    const LengthM = Clamp(Read("Length", 1.5), 0.25, 6);
    const TubeR = Clamp(Read("Radius", 0.04), 0.01, 0.35);
    const PixelW = Clamp((LengthM / 4) * 236 + 88, 96, 260);
    const PixelH = Clamp(TubeR * 170 + 8, 9, 26);

    DrawCrosshair(Brush, CX, CY, W, H);
    const Glow = Brush.createLinearGradient(0, CY - PixelH * 1.9, 0, CY + PixelH * 1.9);
    Glow.addColorStop(0, "rgba(0,0,0,0)");
    Glow.addColorStop(0.5, Rgba(Tint, 0.22));
    Glow.addColorStop(1, "rgba(0,0,0,0)");
    Brush.fillStyle = Glow;
    Brush.fillRect(CX - PixelW / 2 - 14, CY - PixelH * 1.9, PixelW + 28, PixelH * 3.8);

    Brush.strokeStyle = Rgba(Tint, 0.22);
    Brush.setLineDash([2, 5]);
    for (let P = -3; P <= 3; P++) {
      const X = CX + (P * PixelW) / 7;
      StrokeLine(Brush, X, CY + PixelH / 2 + 8, X, CY + PixelH / 2 + 64, 1);
    }
    Brush.setLineDash([]);

    Brush.fillStyle = Rgba(Tint, 0.16);
    Brush.strokeStyle = Rgba(Tint, 0.88);
    Brush.lineWidth = 1.3;
    Brush.beginPath();
    Brush.roundRect(CX - PixelW / 2, CY - PixelH / 2, PixelW, PixelH, PixelH / 2);
    Brush.fill();
    Brush.stroke();

    DrawDimension(Brush, CX - PixelW / 2, CY + PixelH / 2 + 62, CX + PixelW / 2, CY + PixelH / 2 + 62, `${LengthM.toFixed(1)} m LENGTH`, 16);
    DrawLabel(Brush, `${Math.round(TubeR * 1000)} mm RADIUS`, 18, 22, "rgba(255,255,255,0.44)", "left", 8);
    DrawLabel(Brush, "RADIAL EMISSION", W - 18, 22, "rgba(255,255,255,0.44)", "right", 8);
  } else if (Style === "ledlight") {
    const CX = W / 2;
    const CY = 124;
    const SizeMm = Clamp(Read("Package diameter", 40), 5, 160);
    const BoxPx = Clamp(60 + SizeMm * 0.55, 62, 124);
    const Cone = Clamp(Read("Emission angle", 120), 15, 175);

    DrawCrosshair(Brush, CX, CY, W, H);
    Brush.fillStyle = "#222422";
    Brush.strokeStyle = "rgba(255,255,255,0.48)";
    Brush.lineWidth = 1.2;
    Brush.beginPath();
    Brush.roundRect(CX - BoxPx / 2, CY - BoxPx / 2, BoxPx, BoxPx, 10);
    Brush.fill();
    Brush.stroke();

    const Pads = 4;
    const Inner = BoxPx - 20;
    const Step = Inner / Pads;
    Brush.fillStyle = Rgba(Tint, Clamp(Read("Dimmer", 1) * 0.85 + 0.12, 0.15, 0.96));
    for (let Row = 0; Row < Pads; Row++) {
      for (let Col = 0; Col < Pads; Col++) {
        Brush.fillRect(
          CX - Inner / 2 + Col * Step + 2.5,
          CY - Inner / 2 + Row * Step + 2.5,
          Step - 5,
          Step - 5,
        );
      }
    }

    for (let F = -2; F <= 2; F++) {
      const Y = CY + F * (BoxPx / 5.5);
      StrokeLine(Brush, CX - BoxPx / 2 - 12, Y, CX - BoxPx / 2 - 4, Y, 1.4);
      StrokeLine(Brush, CX + BoxPx / 2 + 4, Y, CX + BoxPx / 2 + 12, Y, 1.4);
    }

    const BeamArc = Math.min(Math.PI * 0.92, (Cone * Math.PI) / 180);
    Brush.strokeStyle = Rgba(Tint, 0.55);
    Brush.lineWidth = 1;
    Brush.beginPath();
    Brush.arc(CX, CY, BoxPx * 0.72, -Math.PI / 2 - BeamArc / 2, -Math.PI / 2 + BeamArc / 2);
    Brush.stroke();

    DrawLabel(Brush, `${Math.round(Cone)}° OPTIC`, CX, CY - BoxPx * 0.78 - 6, "rgba(255,255,255,0.45)", "center", 8);
    DrawDimension(Brush, CX - BoxPx / 2, CY + BoxPx / 2, CX + BoxPx / 2, CY + BoxPx / 2, `${Math.round(SizeMm)} mm PACKAGE`, 16);
  } else {
    // LED Strip / Linear Run
    const CX = W / 2;
    const CY = 128;
    const Run = Clamp(Read("Length", 2.4), 0.25, 25);
    const Density = Clamp(Read("LED density", 60), 10, 240);
    const Opal = !!Properties?.["Opal"];
    const StripW = Clamp(180 + Math.min(Run, 8) * 14, 180, 294);
    const TotalNodes = Clamp(Math.round((Density * Run) / 12), 10, 28);

    DrawCrosshair(Brush, CX, CY, W, H);
    Brush.fillStyle = "#1f201e";
    Brush.strokeStyle = "rgba(255,255,255,0.45)";
    Brush.lineWidth = 1;
    Brush.fillRect(CX - StripW / 2, CY - 9, StripW, 18);
    Brush.strokeRect(CX - StripW / 2, CY - 9, StripW, 18);

    if (Opal) {
      const Beam = Brush.createLinearGradient(0, CY - 5, 0, CY + 5);
      Beam.addColorStop(0, Rgba(Tint, 0.85));
      Beam.addColorStop(1, Rgba(Tint, 0.45));
      Brush.fillStyle = Beam;
      Brush.fillRect(CX - StripW / 2 + 2, CY - 5, StripW - 4, 10);
    } else {
      for (let K = 0; K < TotalNodes; K++) {
        const X = CX - StripW / 2 + 8 + ((StripW - 16) * K) / Math.max(1, TotalNodes - 1);
        Brush.fillStyle = Rgba(Tint, 0.92);
        Brush.fillRect(X - 2, CY - 4, 4, 8);
      }
    }

    DrawDimension(
      Brush,
      CX - StripW / 2,
      CY + 24,
      CX + StripW / 2,
      CY + 24,
      `${Run.toFixed(1)} m RUN · ${Math.round(Density * Run)} EMITTERS`,
      14,
    );
    DrawLabel(Brush, `${Math.round(Read("Voltage", 24))} V DC`, 18, 22, "rgba(255,255,255,0.44)", "left", 8);
    DrawLabel(Brush, Opal ? "OPAL DIFFUSER" : "EXPOSED PACKAGES", W - 18, 22, "rgba(255,255,255,0.44)", "right", 8);
  }

  void Flux;
  Brush.restore();
}

// =========================================================================
// VERBATIM PROJECT ZERO ANALYTICAL CURVE (ProjectResponse)
// =========================================================================
export function ProjectResponse(Brush, Width, Height, Model) {
  const { Style, Read, Properties, Tint, Flux } = Model;
  Brush.save();
  Brush.clearRect(0, 0, Width, Height);

  const P = { l: 38, r: 14, t: 16, b: 24 };
  const PW = Width - P.l - P.r;
  const PH = Height - P.t - P.b;

  Brush.strokeStyle = "rgba(255,255,255,0.06)";
  Brush.lineWidth = 0.8;
  Brush.setLineDash([2, 4]);
  for (let R = 0; R <= 2; R++) {
    const Y = P.t + (R * PH) / 2;
    StrokeLine(Brush, P.l, Y, Width - P.r, Y, 0.8);
  }
  Brush.setLineDash([]);

  if (Style === "pointlight") {
    const Intensity = Math.max(0.01, Read("Intensity", 14));
    const Decay = Read("Decay", 2);
    const Distances = [1, 2, 5, 10];
    const Values = Distances.map((D) => Intensity / Math.pow(Math.max(1, D), Decay));
    const MaxVal = Math.max(Values[0], 0.1);
    const Plot = (D) => {
      const X = P.l + ((D - 1) / 9) * PW;
      const Val = Intensity / Math.pow(Math.max(1, D), Decay);
      const Y = P.t + PH - (Clamp(Val / MaxVal, 0, 1) * PH);
      return [X, Y];
    };

    DrawLabel(Brush, "ILLUMINANCE · lx", P.l, P.t - 4, "rgba(255,255,255,0.38)", "left", 7.5);
    [MaxVal, MaxVal * 0.75, MaxVal * 0.5, MaxVal * 0.25, 0].forEach((Mark, Index) => {
      const Y = P.t + (Index * PH) / 4;
      DrawLabel(
        Brush,
        Mark >= 10 ? Mark.toFixed(1) : Mark.toFixed(2),
        P.l - 5,
        Y + 3,
        "rgba(255,255,255,0.34)",
        "right",
        7.5,
      );
    });

    Brush.strokeStyle = "rgba(255,255,255,0.07)";
    Brush.setLineDash([2, 4]);
    [2, 5].forEach((D) => {
      const [X] = Plot(D);
      StrokeLine(Brush, X, P.t, X, P.t + PH, 0.8);
    });
    Brush.setLineDash([]);

    const Area = Brush.createLinearGradient(0, P.t, 0, P.t + PH);
    Area.addColorStop(0, Rgba(Tint, 0.16));
    Area.addColorStop(1, Rgba(Tint, 0));
    Brush.beginPath();
    for (let K = 0; K <= 90; K++) {
      const D = 1 + (9 * K) / 90;
      const [X, Y] = Plot(D);
      if (K === 0) Brush.moveTo(X, Y);
      else Brush.lineTo(X, Y);
    }
    Brush.lineTo(P.l + PW, P.t + PH);
    Brush.lineTo(P.l, P.t + PH);
    Brush.closePath();
    Brush.fillStyle = Area;
    Brush.fill();

    Brush.strokeStyle = Rgba(Tint, 0.9);
    Brush.lineWidth = 1.6;
    Brush.beginPath();
    for (let K = 0; K <= 90; K++) {
      const D = 1 + (9 * K) / 90;
      const [X, Y] = Plot(D);
      if (K === 0) Brush.moveTo(X, Y);
      else Brush.lineTo(X, Y);
    }
    Brush.stroke();

    Distances.forEach((D, Index) => {
      const [X, Y] = Plot(D);
      Brush.fillStyle = Index === 2 ? "#ffffff" : Rgba(Tint, 0.92);
      Brush.beginPath();
      Brush.arc(X, Y, Index === 2 ? 3.5 : 2.8, 0, Math.PI * 2);
      Brush.fill();
      DrawLabel(
        Brush,
        `${D} m`,
        X,
        Height - 6,
        "rgba(255,255,255,0.38)",
        Index === 3 ? "right" : Index === 0 ? "left" : "center",
        7.5,
      );
    });
  } else if (Style === "spotlight") {
    const HalfDeg = Clamp(Read("Outer cone", 26) / 2, 2, 78);
    const Penumbra = Clamp(Read("Penumbra", 0.42), 0, 1);
    const Inner = HalfDeg * (1 - Penumbra * 0.72);
    Brush.strokeStyle = Rgba(Tint, 0.9);
    Brush.lineWidth = 1.5;
    Brush.beginPath();
    for (let K = 0; K <= 120; K++) {
      const A = -90 + (180 * K) / 120;
      const Mag = Math.abs(A);
      const V =
        Mag <= Inner
          ? 1
          : Mag >= HalfDeg
            ? 0
            : Math.cos(((Mag - Inner) / Math.max(0.1, HalfDeg - Inner)) * (Math.PI / 2));
      const X = P.l + (K / 120) * PW;
      const Y = P.t + PH - Math.pow(Clamp(V, 0, 1), 1.4) * (PH - 4);
      if (K === 0) Brush.moveTo(X, Y);
      else Brush.lineTo(X, Y);
    }
    Brush.stroke();
    DrawLabel(Brush, "-90°", P.l, Height - 6, "rgba(255,255,255,0.36)", "left", 7.5);
    DrawLabel(Brush, "90°", Width - P.r, Height - 6, "rgba(255,255,255,0.36)", "right", 7.5);
  } else if (Style === "ieslight") {
    Brush.strokeStyle = Rgba(Tint, 0.88);
    Brush.lineWidth = 1.4;
    Brush.beginPath();
    for (let K = 0; K <= 120; K++) {
      const A = -90 + (180 * K) / 120;
      const V = IesWeight(Properties, A, 0);
      const X = P.l + (K / 120) * PW;
      const Y = P.t + PH - V * (PH - 4);
      if (K === 0) Brush.moveTo(X, Y);
      else Brush.lineTo(X, Y);
    }
    Brush.stroke();
    DrawLabel(Brush, "-90°", P.l, Height - 6, "rgba(255,255,255,0.36)", "left", 7.5);
    DrawLabel(Brush, "90°", Width - P.r, Height - 6, "rgba(255,255,255,0.36)", "right", 7.5);
  } else if (Style === "arealight") {
    const Spread = Clamp(Read("Spread", 120), 20, 180) * (Math.PI / 180);
    Brush.strokeStyle = Rgba(Tint, 0.88);
    Brush.lineWidth = 1.4;
    Brush.beginPath();
    for (let K = 0; K <= 90; K++) {
      const Theta = -Math.PI / 2 + (Math.PI * K) / 90;
      const Factor = Math.max(0, Math.cos((Theta * Math.PI) / Math.max(0.4, Spread)));
      const X = P.l + (K / 90) * PW;
      const Y = P.t + PH - Factor * (PH - 4);
      if (K === 0) Brush.moveTo(X, Y);
      else Brush.lineTo(X, Y);
    }
    Brush.stroke();
    DrawLabel(Brush, "-90°", P.l, Height - 6, "rgba(255,255,255,0.36)", "left", 7.5);
    DrawLabel(Brush, "90°", Width - P.r, Height - 6, "rgba(255,255,255,0.36)", "right", 7.5);
  } else {
    const Factor =
      Style === "ledlight"
        ? Clamp(Read("Dimmer", 1), 0, 1)
        : Style === "ledstrip"
          ? Clamp(Read("Dimmer", 1), 0, 1)
          : 1;
    Brush.strokeStyle = Rgba(Tint, 0.85);
    Brush.lineWidth = 1.4;
    Brush.beginPath();
    const Slope = Style === "tubelight" ? 0 : 1;
    Brush.moveTo(P.l, P.t + PH - (Slope ? 0 : PH * 0.56));
    Brush.lineTo(P.l + PW, P.t + PH - (Slope ? Factor * (PH - 4) : PH * 0.56));
    Brush.stroke();
    DrawLabel(Brush, "0", P.l, Height - 6, "rgba(255,255,255,0.36)", "left", 7.5);
    DrawLabel(
      Brush,
      Style === "ledstrip" ? `${Read("Length", 2.4).toFixed(1)} m` : "100%",
      Width - P.r,
      Height - 6,
      "rgba(255,255,255,0.36)",
      "right",
      7.5,
    );
  }

  void Flux;
  Brush.restore();
}
