import React, { useRef } from "react";
import "./PostEffects.css";

// 🧩 Camera / post-process effect components: Motion Blur (under Cameras) and the Post Process sub-effects. Each has a
//    property sheet in the NativePanels format and an infographic study (shutter disc, refraction profile, grid warp,
//    edge profile …) that redraws from the authored values, so every slider visibly changes the diagram.
// 📝 Browser study only. Nothing here reaches the native renderer.

//------------------------------------------------------------------------------------------------------------------------
//                                                  SHEETS AND ROWS
//------------------------------------------------------------------------------------------------------------------------

const S = (Label, Minimum, Maximum, Default, Unit, Decimals, Group) => ({
  Label, Control: "Slider", Minimum, Maximum, Default, Unit, Decimals, Group,
});
const T = (Label, Default, Group) => ({ Label, Control: "Switch", Default, Group });
const C = (Label, Options, Default, Group) => ({ Label, Control: "Select", Options, Default, Group });

export const EffectSheets = {
  "motion-blur": [
    S("Shutter angle", 0, 360, 180, "°", 0, "Shutter"),
    C("Reference rate", ["24 fps", "30 fps", "60 fps", "120 fps"], 0, "Shutter"),
    T("Object motion", true, "Sources"),
    T("Camera motion", true, "Sources"),
    S("Max blur", 0, 15, 6, "% width", 1, "Quality"),
    S("Samples", 2, 32, 12, "", 0, "Quality"),
    S("Preview speed", 0, 2, 1, "×", 2, "Preview"),
  ],
  "heat-shimmer": [
    S("Strength", 0, 1, 0.45, "", 2, "Distortion"),
    S("Scale", 0.25, 4, 1, "×", 2, "Distortion"),
    S("Speed", 0, 4, 1, "×", 2, "Distortion"),
    S("Start distance", 0, 400, 40, "m", 0, "Range"),
    S("Height falloff", 0, 1, 0.55, "", 2, "Range"),
    T("Show mask", false, "Preview"),
  ],
  glitch: [
    S("Intensity", 0, 1, 0.45, "", 2, "Glitch"),
    S("Frequency", 0, 10, 3, "Hz", 1, "Glitch"),
    S("Block size", 2, 64, 14, "px", 0, "Glitch"),
    S("RGB split", 0, 24, 5, "px", 0, "Signal"),
    S("Scanline jitter", 0, 1, 0.3, "", 2, "Signal"),
    T("Colour drift", true, "Signal"),
  ],
  "chromatic-aberration": [
    S("Strength", 0, 12, 4, "px", 1, "Fringe"),
    S("Falloff", 0.5, 4, 2, "", 2, "Fringe"),
    C("Mode", ["Radial", "Lateral"], 0, "Fringe"),
  ],
  "film-grain": [
    S("Amount", 0, 1, 0.3, "", 2, "Grain"),
    S("Size", 1, 4, 1.4, "px", 1, "Grain"),
    S("Luminance response", 0, 1, 0.7, "", 2, "Grain"),
    T("Coloured", false, "Grain"),
    T("Animated", true, "Grain"),
  ],
  "lens-distortion": [
    S("Distortion", -1, 1, -0.15, "", 2, "Lens"),
    S("Scale", 0.5, 1.5, 1, "×", 2, "Lens"),
    S("Centre X", -0.5, 0.5, 0, "", 2, "Centre"),
    S("Centre Y", -0.5, 0.5, 0, "", 2, "Centre"),
  ],
  scanlines: [
    S("Line count", 32, 144, 90, "lines", 0, "Lines"),
    S("Intensity", 0, 1, 0.45, "", 2, "Lines"),
    S("Curvature", 0, 0.4, 0.12, "", 2, "Tube"),
    S("Flicker", 0, 1, 0.15, "", 2, "Tube"),
    T("Phosphor mask", true, "Tube"),
  ],
  sharpen: [
    S("Amount", 0, 3, 0.9, "×", 2, "Sharpen"),
    S("Radius", 1, 3, 1, "px", 0, "Sharpen"),
    S("Threshold", 0, 0.2, 0.02, "", 3, "Sharpen"),
  ],
};

export const EffectRows = [
  ["motion-blur", "Motion Blur", "Shutter-based blur · camera and object motion", "cameras"],
  ["heat-shimmer", "Heat Shimmer", "Rising-air refraction near hot ground", "post"],
  ["glitch", "Glitch", "Digital signal break-up · blocks and RGB split", "post"],
  ["chromatic-aberration", "Chromatic Aberration", "Colour fringing towards the frame edge", "post"],
  ["film-grain", "Film Grain", "Luminance-weighted photographic grain", "post"],
  ["lens-distortion", "Lens Distortion", "Barrel / pincushion warp", "post"],
  ["scanlines", "Scanlines", "CRT lines, tube curvature and phosphor mask", "post"],
  ["sharpen", "Sharpen", "Unsharp-mask edge contrast", "post"],
].map(([Id, Name, Description, Owner]) => ({ Id, Name, Panel: Id, Icon: "effect-" + Id, Parent: Owner, Description }));

export const IsEffectPanel = (Panel) => Object.hasOwn(EffectSheets, Panel);

const Fixed = (Amount, Digits = 2) => {
  const Text = (+Amount || 0).toFixed(Digits);
  return Text.includes(".") ? Text.replace(/\.?0+$/, "") : Text;
};

export function EffectSummary(Panel, V) {
  switch (Panel) {
    case "motion-blur": return Fixed(V("Shutter angle"), 0) + "° shutter · " + Fixed(V("Samples"), 0) + " samples";
    case "heat-shimmer": return Fixed(V("Strength")) + " · from " + Fixed(V("Start distance"), 0) + " m";
    case "glitch": return Fixed(V("Intensity")) + " · " + Fixed(V("Frequency"), 1) + " Hz";
    case "chromatic-aberration": return Fixed(V("Strength"), 1) + " px fringe";
    case "film-grain": return Math.round(V("Amount") * 100) + "% · " + Fixed(V("Size"), 1) + " px";
    case "lens-distortion": {
      const K = +V("Distortion");
      return (Math.abs(K) < 0.005 ? "None" : K < 0 ? "Barrel " : "Pincushion ") + (Math.abs(K) < 0.005 ? "" : Fixed(Math.abs(K)));
    }
    case "scanlines": return Fixed(V("Line count"), 0) + " lines · " + Math.round(V("Intensity") * 100) + "%";
    case "sharpen": return Fixed(V("Amount")) + "× · " + Fixed(V("Radius"), 0) + " px";
    default: return "";
  }
}

// Scene migration: Lens Flare moves from Sun to Cameras; Motion Blur joins Cameras; the effects join Post Process.
// Runs once per saved scene (CameraEffectsRevision), so later deletions are respected.
export function ArrangeCameraEffects(Rows = [], Revision) {
  if (Revision === 1 || !Array.isArray(Rows)) return Rows;
  let Result = Rows.filter((Row) => Row && Row.Id !== "flare");
  const Flare = Rows.find((Row) => Row?.Id === "flare");
  const HasCameras = Result.some((Row) => Row.Id === "cameras");
  const Insert = (Row) => {
    const Post = Result.findIndex((Each) => Each.Id === "post");
    if (Row.Parent === "cameras") {
      const At = Post >= 0 ? Post : Result.length;
      Result.splice(At, 0, Row);
      return;
    }
    let At = Post;
    for (let Index = 0; Index < Result.length; Index++) if (Result[Index].Parent === "post") At = Index;
    Result.splice(At < 0 ? Result.length : At + 1, 0, Row);
  };
  if (Flare)
    Insert(
      Flare.Parent === "sun" && HasCameras
        ? { ...Flare, Parent: "cameras", Description: "Camera optical effect · sun-driven" }
        : Flare,
    );
  const Present = new Set(Result.map((Row) => Row.Id));
  for (const Row of EffectRows)
    if (!Present.has(Row.Id) && (Row.Parent !== "post" || Present.has("post"))) Insert({ ...Row });
  return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   INFOGRAPHICS
//------------------------------------------------------------------------------------------------------------------------
// One diagram per effect in the camera-card style: a big reading, a schematic that redraws from the authored values,
// and a caption. Dragging across a diagram sets that effect's main value. Nothing here renders the scene itself.

// PCG-style integer hash → [0, 1): fixed pseudo-random layout, identical on every redraw.
function Random(A, B = 0, C = 0) {
  let N = (Math.imul(A | 0, 73856093) ^ Math.imul(B | 0, 19349663) ^ Math.imul(C | 0, 83492791)) >>> 0;
  N = (Math.imul(N, 747796405) + 2891336453) >>> 0;
  const W = Math.imul((N >>> ((N >>> 28) + 4)) ^ N, 277803737) >>> 0;
  return (((W >>> 22) ^ W) >>> 0) / 4294967296;
}
const Clamp = (Amount, Low, High) => Math.max(Low, Math.min(High, Amount));
const Path = (Points) => Points.map(([X, Y], Index) => (Index ? "L" : "M") + X.toFixed(1) + " " + Y.toFixed(1)).join("");
const Rates = [24, 30, 60, 120];
const Screen = { mixBlendMode: "screen" };

export function MotionBlurSpan(P) {
  const Rate = Rates[P("Reference rate")] || 24;
  const Exposure = P("Shutter angle") / 360 / Rate;
  // Reference: a fast object that crosses the whole frame in a quarter second at preview speed 1×.
  const Travel = P("Preview speed") * Exposure * 400;
  const Limit = P("Max blur");
  return { Rate, Exposure, Streak: Math.min(Travel, Limit), Clamped: Travel > Limit + 0.01 };
}
const GrainWeight = (R, L) => 1 - R + R * 4 * L * (1 - L) * 1.15;
const SharpenProfile = (P) => {
  const Amount = P("Amount"), Radius = P("Radius"), Threshold = P("Threshold");
  const Edge = (X) => 0.2 + 0.6 / (1 + Math.exp(-X * 2.6));
  const Blur = (X) => {
    let Sum = 0, Weight = 0;
    for (let Offset = -3 * Radius; Offset <= 3 * Radius; Offset += 0.25) {
      const W = Math.exp(-(Offset * Offset) / (2 * Radius * Radius));
      Sum += Edge(X + Offset) * W;
      Weight += W;
    }
    return Sum / Weight;
  };
  return Array.from({ length: 121 }, (_, Index) => {
    const X = -6 + Index * 0.1, Original = Edge(X), Detail = Original - Blur(X);
    // Soft threshold: only the part of the detail above the threshold is boosted, so there is no jump.
    const Kept = Math.sign(Detail) * Math.max(0, Math.abs(Detail) - Threshold);
    return [X, Original, Original + Amount * Kept];
  });
};
const Bend = (K, Scale, Cx, Cy) => ([U, V]) => {
  const Dx = U - Cx, Dy = (V - Cy) * 0.5625, R2 = Dx * Dx + Dy * Dy;
  const F = (1 + K * R2 * 0.6) / Scale;
  return [Cx + Dx * F, Cy + (Dy * F) / 0.5625];
};

function Readouts(Panel, P) {
  const Percent = (Amount) => Math.round(Amount * 100) + "%";
  switch (Panel) {
    case "motion-blur": {
      const Span = MotionBlurSpan(P);
      return [
        ["Exposure", Span.Exposure > 0 ? "1/" + Math.round(1 / Span.Exposure) + " s" : "Closed"],
        [Span.Clamped ? "Streak · capped" : "Streak width", Span.Streak.toFixed(1) + "%"],
        ["Samples", Fixed(P("Samples"), 0)],
      ];
    }
    case "heat-shimmer":
      return [["Starts at", Fixed(P("Start distance"), 0) + " m"], ["Peak shift", (P("Strength") * 7).toFixed(1) + " px"], ["Rise", Percent(1 - P("Height falloff"))]];
    case "glitch":
      return [["Bursts", Fixed(P("Frequency"), 1) + " / s"], ["Block", Fixed(P("Block size"), 0) + " px"], ["RGB split", Fixed(P("RGB split"), 0) + " px"]];
    case "chromatic-aberration":
      return [["Corner fringe", Fixed(P("Strength"), 1) + " px"], ["Half-way", Fixed(P("Strength") * 0.5 ** P("Falloff"), 1) + " px"], ["Mode", ["Radial", "Lateral"][P("Mode")] || "Radial"]];
    case "film-grain": {
      const R = P("Luminance response");
      return [["Amount", Percent(P("Amount"))], ["Grain size", Fixed(P("Size"), 1) + " px"], ["Shadows vs mids", Percent(GrainWeight(R, 0.08) / GrainWeight(R, 0.5))]];
    }
    case "lens-distortion": {
      const K = P("Distortion"), Corner = Bend(K, P("Scale"), 0, 0)([1, 1]);
      return [["Type", Math.abs(K) < 0.005 ? "None" : K < 0 ? "Barrel" : "Pincushion"], ["Corner shift", ((Corner[0] - 1) * 100).toFixed(0) + "%"], ["Scale", Fixed(P("Scale")) + "×"]];
    }
    case "scanlines":
      return [["Lines", Fixed(P("Line count"), 0)], ["Darkening", Percent(P("Intensity"))], ["Flicker", Percent(P("Flicker"))]];
    case "sharpen": {
      const Peak = Math.max(...SharpenProfile(P).map(([, Original, Sharp]) => Sharp - Original));
      return [["Amount", Fixed(P("Amount")) + "×"], ["Overshoot", "+" + Math.round(Peak * 255) + " / 255"], ["Threshold", Math.round(P("Threshold") * 255) + " / 255"]];
    }
    default:
      return [];
  }
}

// Main reading and caption shown large above the diagram; also which value a horizontal drag edits.
function Headline(Panel, P) {
  switch (Panel) {
    case "motion-blur": {
      const Span = MotionBlurSpan(P);
      return { Title: "Shutter study", Whole: Span.Exposure > 0 ? "1/" + Math.round(1 / Span.Exposure) : "0", Unit: "s exposure", Caption: `${Fixed(P("Shutter angle"), 0)}° shutter at ${Span.Rate} fps · drag to change the shutter angle`, Drag: ["Shutter angle", 0, 360] };
    }
    case "heat-shimmer":
      return { Title: "Refraction profile", Whole: Fixed(P("Start distance"), 0), Unit: "m to first shimmer", Caption: "Side view from the lens · drag to move where the shimmer starts", Drag: ["Start distance", 0, 400] };
    case "glitch":
      return { Title: "Signal break-up", Whole: String(Math.round(P("Intensity") * 100)), Unit: "% intensity", Caption: "The signal trace and a test frame during a burst · drag to change the intensity", Drag: ["Intensity", 0, 1] };
    case "chromatic-aberration":
      return { Title: "Fringe across the frame", Whole: Fixed(P("Strength"), 1), Unit: "px at the corners", Caption: "Red and blue separation from centre to corner · drag to change the strength", Drag: ["Strength", 0, 12] };
    case "film-grain":
      return { Title: "Grain response", Whole: String(Math.round(P("Amount") * 100)), Unit: "% grain", Caption: "Grain strength across the brightness range · drag to change the amount", Drag: ["Amount", 0, 1] };
    case "lens-distortion": {
      const K = P("Distortion");
      return { Title: "Grid warp", Whole: Fixed(Math.abs(K), 2), Unit: Math.abs(K) < 0.005 ? "no distortion" : K < 0 ? "barrel" : "pincushion", Caption: "Dashed: straight grid · solid: where it lands · drag left for barrel, right for pincushion", Drag: ["Distortion", -1, 1] };
    }
    case "scanlines":
      return { Title: "Tube profile", Whole: Fixed(P("Line count"), 0), Unit: "lines", Caption: "Screen shape and a magnified slice of the lines · drag to change the line count", Drag: ["Line count", 32, 144] };
    case "sharpen":
      return { Title: "Edge profile", Whole: Fixed(P("Amount"), 2), Unit: "× edge contrast", Caption: "Brightness across one soft edge, before and after · drag to change the amount", Drag: ["Amount", 0, 3] };
    default:
      return { Title: "Effect", Whole: "", Unit: "", Caption: "" };
  }
}

const Axis = ({ X1, X2, Y, Labels }) => (
  <g>
    <line x1={X1} x2={X2} y1={Y} y2={Y} stroke="#4a4a4a" />
    {Labels.map(([Text, X], Index) => (
      <text key={Index} x={X} y={Y + 11} textAnchor="middle">{Text}</text>
    ))}
  </g>
);

function MotionBlurChart({ P }) {
  const Angle = Clamp(P("Shutter angle"), 0, 360), Span = MotionBlurSpan(P);
  const Cx = 58, Cy = 80, R = 40, Wedge = (Angle / 360) * Math.PI * 2;
  const End = [Cx + R * Math.sin(Wedge), Cy - R * Math.cos(Wedge)];
  const Open = Angle >= 359.5 ? `M${Cx} ${Cy - R}A${R} ${R} 0 1 1 ${Cx - 0.01} ${Cy - R}Z` : `M${Cx} ${Cy}L${Cx} ${Cy - R}A${R} ${R} 0 ${Angle > 180 ? 1 : 0} 1 ${End[0].toFixed(2)} ${End[1].toFixed(2)}Z`;
  const Left = 128, Width = 180, FrameWidth = Width / 3;
  // The track shows a quarter of the frame width, magnified 4×, so a few percent of blur is readable.
  const Streak = (Span.Streak / 25) * Width, Count = Math.max(2, Math.round(P("Samples")));
  const Ball = Left + Width * 0.62, Track = 128;
  return (
    <>
      <circle cx={Cx} cy={Cy} r={R + 6} fill="none" stroke="#3a3a3a" strokeDasharray="1.5 3" />
      <circle cx={Cx} cy={Cy} r={R} fill="#101010" stroke="#4a4a4a" />
      {Angle > 0.5 && <path d={Open} fill="#f5b04f" opacity=".85" />}
      <line x1={Cx} y1={Cy} x2={Cx} y2={Cy - R - 6} stroke="#f2f2f2" strokeDasharray="1.5 2" />
      <circle cx={Cx} cy={Cy} r="3" fill="#f2f2f2" />
      <text x={Cx} y={Cy + R + 18} textAnchor="middle" className="fx-accent">{Fixed(Angle, 0)}° open</text>
      <text x={Left} y="22">FRAMES · {Span.Rate} FPS</text>
      {[0, 1, 2].map((Index) => (
        <g key={Index}>
          <rect x={Left + Index * FrameWidth + 1} y="30" width={FrameWidth - 2} height="22" rx="5" fill="#151515" stroke="#ffffff12" />
          <rect x={Left + Index * FrameWidth + 4} y="35" width={Math.max(0, (FrameWidth - 8) * (Angle / 360))} height="12" rx="3" fill="#f5b04f" opacity=".8" />
        </g>
      ))}
      <text x={Left} y="64">shutter open {Span.Exposure > 0 ? "1/" + Math.round(1 / Span.Exposure) + " s" : "—"} of each 1/{Span.Rate} s frame</text>
      <text x={Left} y="96">OBJECT CROSSING THE FRAME IN {Fixed(0.25 / Math.max(0.01, P("Preview speed")), 2)} s · 4× ZOOM</text>
      <rect x={Left} y="104" width={Width} height="46" rx="8" fill="#121212" stroke="#ffffff10" />
      {P("Camera motion") &&
        Array.from({ length: 8 }, (_, Index) => (
          <line key={Index} x1={Left + 12 + Index * 21} x2={Left + 12 + Index * 21 + Clamp(Streak * 0.35, 1, 15)} y1="144" y2="144" stroke="#5a5a5a" strokeWidth="2" strokeLinecap="round" />
        ))}
      {P("Object motion") ? (
        <>
          <defs>
            <linearGradient id="fx-streak" x1="0" x2="1">
              <stop offset="0" stopColor="#f5b04f" stopOpacity="0" />
              <stop offset="1" stopColor="#f5b04f" stopOpacity=".9" />
            </linearGradient>
          </defs>
          <rect x={Ball - Streak} y={Track - 7} width={Math.max(0.5, Streak)} height="14" rx="7" fill="url(#fx-streak)" />
          {Array.from({ length: Count }, (_, Index) => (
            <circle key={Index} cx={Ball - Streak + (Streak * Index) / (Count - 1)} cy={Track} r="1.3" fill="#fff" opacity=".7" />
          ))}
          <circle cx={Ball} cy={Track} r="7" fill="#f2f2f2" />
        </>
      ) : (
        <>
          <circle cx={Ball} cy={Track} r="7" fill="#9a9a9a" />
          <text x={Left + 8} y={Track + 3}>object motion off</text>
        </>
      )}
      {Span.Clamped && <line x1={Ball - Streak} x2={Ball - Streak} y1="108" y2="148" stroke="#ff8c7a" strokeDasharray="2 2" />}
      <text x={Left + Width} y="164" textAnchor="end">{Count} samples along the streak{Span.Clamped ? " · capped by Max blur" : ""}</text>
    </>
  );
}

function HeatShimmerChart({ P }) {
  const Left = 36, Right = 308, Ground = 142, Top = 22;
  const Px = (Metres) => Left + (Metres / 400) * (Right - Left);
  const Start = P("Start distance"), Strength = P("Strength"), Scale = P("Scale"), Falloff = P("Height falloff");
  const Ramp = (Metres) => Strength * Clamp((Metres - Start) / 60, 0, 1);
  const Curve = Array.from({ length: 81 }, (_, Index) => {
    const Metres = Index * 5;
    return [Px(Metres), 70 - Ramp(Metres) * 40];
  });
  const Layers = [8, 18, 30, 44].map((Height) => {
    const Decay = Math.exp(-Height * Falloff * 0.06);
    const Points = [];
    for (let X = Px(Start); X <= Right; X += 2) {
      const Metres = ((X - Left) / (Right - Left)) * 400;
      Points.push([X, Ground - Height - Math.sin((X / (22 / Scale)) * Math.PI * 2 + Height) * Ramp(Metres) * 6 * Decay]);
    }
    return { Height, Decay, Points };
  });
  return (
    <>
      <text x={Left} y={Top}>DISPLACEMENT</text>
      {[0, 0.5, 1].map((Level) => (
        <g key={Level}>
          <line x1={Left} x2={Right} y1={70 - Level * 40} y2={70 - Level * 40} stroke="#ffffff10" strokeDasharray="2 4" />
          <text x={Left - 6} y={73 - Level * 40} textAnchor="end">{(Level * 7).toFixed(1)}</text>
        </g>
      ))}
      <path d={Path(Curve)} fill="none" stroke="#ff8a4c" strokeWidth="1.6" />
      <rect x={Left} y={Ground} width={Right - Left} height="8" fill="#2a211c" />
      {Layers.map(({ Height, Decay, Points }) =>
        Points.length > 1 ? <path key={Height} d={Path(Points)} fill="none" stroke="#ffb36a" strokeWidth="1.1" opacity={0.15 + 0.75 * Decay} /> : null,
      )}
      <line x1={Px(Start)} x2={Px(Start)} y1={Top + 4} y2={Ground} stroke="#ffb36a" strokeDasharray="3 3" />
      <text x={Clamp(Px(Start) + 4, Left, Right - 70)} y={Ground - 52} className="fx-accent">starts {Fixed(Start, 0)} m</text>
      <path d={`M${Left - 22} ${Ground - 10}h12v8h-12z M${Left - 10} ${Ground - 8}l6 -3v10l-6 -3z`} fill="#9a9a9a" />
      <Axis X1={Left} X2={Right} Y={Ground + 8} Labels={[0, 100, 200, 300, 400].map((Metres) => [Metres + " m", Px(Metres)])} />
    </>
  );
}

// SMPTE-style colour bars (75 %) and the lower calibration strip: a familiar test frame, so displaced blocks and the
// red / green / blue channel separation read at a glance.
const TestBars = ["#bfbfbf", "#bfbf00", "#00bfbf", "#00bf00", "#bf00bf", "#bf0000", "#0000bf"];
const TestStrip = ["#0000bf", "#131313", "#bf00bf", "#131313", "#00bfbf", "#131313", "#bfbfbf"];
const Channel = (Hex, Index) => {
  const Level = parseInt(Hex.slice(1 + Index * 2, 3 + Index * 2), 16);
  return "rgb(" + [0, 1, 2].map((Slot) => (Slot === Index ? Level : 0)).join(",") + ")";
};

function GlitchChart({ P }) {
  const Left = 14, Right = 306, Width = Right - Left;
  const Intensity = P("Intensity"), Frequency = P("Frequency"), Block = P("Block size");
  const Split = P("RGB split") * 0.8, Jitter = P("Scanline jitter"), Drift = !!P("Colour drift");
  // Oscilloscope trace: one second of signal. Each burst is a short jagged stretch whose height follows Intensity.
  const Count = Math.round(Frequency);
  const Bursts = Array.from({ length: Count }, (_, Index) => ({
    At: (Index + 0.25 + Random(Index, 7) * 0.5) / Math.max(1, Count),
    Span: 0.025 + 0.05 * Intensity * (0.6 + 0.8 * Random(Index, 3)),
  }));
  const Base = 30;
  const Trace = Path(
    Array.from({ length: 293 }, (_, Step) => {
      const T = Step / 292;
      const Hit = Bursts.find((Burst) => Math.abs(T - Burst.At) < Burst.Span / 2);
      const Noise = (Random(Step, 17) - 0.5) * 1.4;
      return [Left + T * Width, Base + Noise + (Hit ? (Random(Step, 29) - 0.5) * 34 * Intensity : 0)];
    }),
  );
  // Test frame: rows one block tall; a hit row is pushed sideways and its channels pull apart.
  const Top = 58, Height = 92, Band = Clamp(Block * 0.6, 3, 34), Rows = Math.ceil(Height / Band);
  const Bar = Width / 7, Split70 = Top + Height * 0.72;
  const Paint = (Colours, Y, H, Fill) =>
    Colours.map((Colour, Index) => <rect key={Index} x={Left + Index * Bar} y={Y} width={Bar + 0.5} height={H} fill={Fill ? Fill(Colour) : Colour} />);
  const TestFrame = (Fill) => (
    <>
      {Paint(TestBars, Top, Split70 - Top, Fill)}
      {Paint(TestStrip, Split70, Top + Height - Split70, Fill)}
    </>
  );
  let Displaced = 0;
  return (
    <>
      <text x={Left} y="10">1 SECOND OF SIGNAL · {Count} BURST{Count === 1 ? "" : "S"}</text>
      {Bursts.map((Burst, Index) => (
        <rect key={Index} x={Left + (Burst.At - Burst.Span / 2) * Width} y="14" width={Burst.Span * Width} height="32" rx="3" fill="#b06bff" opacity=".14" />
      ))}
      <path d={Trace} fill="none" stroke="#b9a2ff" strokeWidth="1.1" strokeLinejoin="round" />
      <defs>
        <clipPath id="fx-glitch-frame">
          <rect x={Left} y={Top} width={Width} height={Height} rx="6" />
        </clipPath>
        {Array.from({ length: Rows }, (_, Row) => (
          <clipPath key={Row} id={"fx-glitch-row-" + Row}>
            <rect x={Left} y={Top + Row * Band} width={Width} height={Band + 0.6} />
          </clipPath>
        ))}
      </defs>
      <g clipPath="url(#fx-glitch-frame)">
        <rect x={Left} y={Top} width={Width} height={Height} fill="#070707" />
        {Array.from({ length: Rows }, (_, Row) => {
          const Hit = Random(Row, 5) < Intensity * 0.75;
          if (Hit) Displaced++;
          const Shift = Hit ? (Random(Row, 9) - 0.5) * 2 * 46 * Intensity : (Random(Row, 13) - 0.5) * 5 * Jitter;
          return (
            <g key={Row} clipPath={`url(#fx-glitch-row-${Row})`}>
              {Hit ? (
                <>
                  {[0, 1, 2].map((Index) => (
                    <g key={Index} transform={`translate(${(Shift + (Index - 1) * Split).toFixed(1)} 0)`} style={Screen}>
                      {TestFrame((Colour) => Channel(Colour, Index))}
                    </g>
                  ))}
                  {Drift && <rect x={Left} y={Top + Row * Band} width={Width} height={Band} fill="#8b6cff" opacity=".2" />}
                </>
              ) : (
                <g transform={`translate(${Shift.toFixed(1)} 0)`}>{TestFrame()}</g>
              )}
            </g>
          );
        })}
      </g>
      <rect x={Left} y={Top} width={Width} height={Height} rx="6" fill="none" stroke="#ffffff14" />
      <line x1={Left - 6} x2={Left - 6} y1={Top} y2={Top + Band} stroke="#d6d6d6" />
      <line x1={Left - 9} x2={Left - 3} y1={Top} y2={Top} stroke="#d6d6d6" />
      <line x1={Left - 9} x2={Left - 3} y1={Top + Band} y2={Top + Band} stroke="#d6d6d6" />
      <text x={Left} y="166">one block = {Fixed(Block, 0)} px · channels {Fixed(P("RGB split"), 0)} px apart</text>
      <text x={Right} y="166" textAnchor="end" className="fx-accent">{Displaced} of {Rows} rows hit</text>
    </>
  );
}

function ChromaticChart({ P }) {
  const Left = 34, Right = 196, Top = 24, Bottom = 136, Strength = P("Strength"), Falloff = P("Falloff");
  const Lateral = P("Mode") === 1;
  const Px = (R) => Left + R * (Right - Left);
  const Py = (Shift) => (Top + Bottom) / 2 - (Shift / 12) * ((Bottom - Top) / 2);
  const Curve = (Sign) => Path(Array.from({ length: 41 }, (_, Index) => [Px(Index / 40), Py(Sign * Strength * (Index / 40) ** Falloff)]));
  const Fx = 214, Fy = 34, Fw = 92, Fh = 92 * 0.5625 * 1.4;
  return (
    <>
      <text x={Left} y="16">SHIFT · px</text>
      {[-12, -6, 0, 6, 12].map((Shift) => (
        <g key={Shift}>
          <line x1={Left} x2={Right} y1={Py(Shift)} y2={Py(Shift)} stroke={Shift ? "#ffffff10" : "#4a4a4a"} strokeDasharray={Shift ? "2 4" : ""} />
          <text x={Left - 6} y={Py(Shift) + 3} textAnchor="end">{Shift}</text>
        </g>
      ))}
      <path d={Curve(1)} fill="none" stroke="#ff5a5a" strokeWidth="1.7" />
      <path d={Curve(-1)} fill="none" stroke="#5a8cff" strokeWidth="1.7" />
      <text x={Right} y={Py(Strength) - 5} textAnchor="end" className="fx-red">red out</text>
      <text x={Right} y={Py(-Strength) + 11} textAnchor="end" className="fx-blue">blue in</text>
      <Axis X1={Left} X2={Right} Y={Bottom + 4} Labels={[["centre", Left], ["half-way", Px(0.5)], ["corner", Right]]} />
      <rect x={Fx} y={Fy} width={Fw} height={Fh} rx="6" fill="#121212" stroke="#ffffff14" />
      {[
        [0.5, 0.5],
        [0.78, 0.5],
        [0.93, 0.12],
      ].map(([U, V], Index) => {
        const Dx = U - 0.5, Dy = V - 0.5, R = Math.min(1, Math.hypot(Dx, Dy) / 0.6);
        const Shift = Strength * R ** Falloff * 1.6, Angle = Lateral ? 0 : Math.atan2(Dy, Dx);
        const X = Fx + U * Fw, Y = Fy + V * Fh, Ox = Math.cos(Angle) * Shift, Oy = Math.sin(Angle) * Shift;
        return (
          <g key={Index}>
            <circle cx={X + Ox} cy={Y + Oy} r="6" fill="#ff2a2a" style={Screen} />
            <circle cx={X} cy={Y} r="6" fill="#2aff5a" style={Screen} />
            <circle cx={X - Ox} cy={Y - Oy} r="6" fill="#2a5aff" style={Screen} />
          </g>
        );
      })}
      <text x={Fx + Fw / 2} y={Fy + Fh + 13} textAnchor="middle">{Lateral ? "lateral · horizontal only" : "radial · away from centre"}</text>
      <text x={Fx + Fw / 2} y={Fy + Fh + 24} textAnchor="middle">fringes magnified</text>
    </>
  );
}

function FilmGrainChart({ P }) {
  const Left = 30, Right = 306, Top = 22, Bottom = 92, R = P("Luminance response"), Amount = P("Amount"), Size = P("Size");
  const Px = (L) => Left + L * (Right - Left);
  const Py = (W) => Bottom - (W / 1.2) * (Bottom - Top);
  const Curve = Array.from({ length: 51 }, (_, Index) => [Px(Index / 50), Py(GrainWeight(R, Index / 50) * Amount * 1.6)]);
  const Strip = 104, StripHeight = 40, Coloured = P("Coloured");
  const Dots = [];
  for (let Index = 0; Index < 900; Index++) {
    const L = Random(Index, 1), Y = Random(Index, 2);
    const Strength = GrainWeight(R, L) * Amount;
    if (Random(Index, 3) > Strength * 1.4) continue;
    const Tint = Coloured ? ["#ff9a9a", "#9aff9a", "#9ab8ff"][Index % 3] : L > 0.5 ? "#000" : "#fff";
    Dots.push(<circle key={Index} cx={Px(L)} cy={Strip + 2 + Y * (StripHeight - 4)} r={0.35 + Size * 0.35} fill={Tint} opacity={0.25 + 0.5 * Random(Index, 4)} />);
  }
  return (
    <>
      <defs>
        <linearGradient id="fx-luma" x1="0" x2="1">
          <stop offset="0" stopColor="#050505" />
          <stop offset="1" stopColor="#f0f0f0" />
        </linearGradient>
      </defs>
      <text x={Left} y="14">GRAIN STRENGTH</text>
      {[0, 0.4, 0.8, 1.2].map((Level) => (
        <line key={Level} x1={Left} x2={Right} y1={Py(Level)} y2={Py(Level)} stroke="#ffffff10" strokeDasharray="2 4" />
      ))}
      <path d={Path([...Curve, [Px(1), Bottom], [Px(0), Bottom]])} fill="#e6cf7222" />
      <path d={Path(Curve)} fill="none" stroke="#e6cf72" strokeWidth="1.6" />
      <rect x={Left} y={Strip} width={Right - Left} height={StripHeight} rx="6" fill="url(#fx-luma)" />
      {Dots}
      <Axis X1={Left} X2={Right} Y={Strip + StripHeight + 4} Labels={[["shadows", Px(0.08)], ["mid-tones", Px(0.5)], ["highlights", Px(0.92)]]} />
    </>
  );
}

function LensDistortionChart({ P }) {
  const Fx = 40, Fy = 14, Fw = 240, Fh = 135;
  const Warp = Bend(P("Distortion"), P("Scale"), P("Centre X") * 2, P("Centre Y") * 2);
  const At = ([U, V]) => [Fx + ((U + 1) / 2) * Fw, Fy + ((V + 1) / 2) * Fh];
  const Lines = [];
  for (let Index = 0; Index <= 8; Index++) {
    const U = -1 + Index / 4;
    Lines.push(Array.from({ length: 25 }, (_, Step) => [U, -1 + Step / 12]));
  }
  for (let Index = 0; Index <= 6; Index++) {
    const V = -1 + Index / 3;
    Lines.push(Array.from({ length: 33 }, (_, Step) => [-1 + Step / 16, V]));
  }
  return (
    <>
      <clipPath id="fx-lens-frame">
        <rect x={Fx} y={Fy} width={Fw} height={Fh} rx="8" />
      </clipPath>
      <rect x={Fx} y={Fy} width={Fw} height={Fh} rx="8" fill="#121212" stroke="#ffffff14" />
      <g clipPath="url(#fx-lens-frame)">
        {Lines.map((Line, Index) => (
          <path key={"s" + Index} d={Path(Line.map(At))} fill="none" stroke="#ffffff16" strokeDasharray="2 3" />
        ))}
        {Lines.map((Line, Index) => (
          <path key={"w" + Index} d={Path(Line.map(Warp).map(At))} fill="none" stroke="#5fc8ff" strokeWidth="1.1" opacity=".85" />
        ))}
      </g>
      {[[-1, -1], [1, -1], [1, 1], [-1, 1]].map((Corner, Index) => {
        const [X1, Y1] = At(Corner), [X2, Y2] = At(Warp(Corner));
        return <line key={Index} x1={X1} y1={Y1} x2={X2} y2={Y2} stroke="#f2f2f2" strokeWidth="1.2" markerEnd="url(#fx-arrow)" />;
      })}
      <defs>
        <marker id="fx-arrow" viewBox="0 0 6 6" refX="5" refY="3" markerWidth="5" markerHeight="5" orient="auto">
          <path d="M0 0L6 3L0 6Z" fill="#f2f2f2" />
        </marker>
      </defs>
      <circle cx={At([P("Centre X") * 2, P("Centre Y") * 2])[0]} cy={At([P("Centre X") * 2, P("Centre Y") * 2])[1]} r="3" fill="#ffc93d" />
      <text x="160" y="164" textAnchor="middle">arrows: where each corner moves · amber: distortion centre</text>
    </>
  );
}

function ScanlinesChart({ P }) {
  const Count = Math.round(P("Line count")), Intensity = P("Intensity"), Curvature = P("Curvature");
  const Sx = 16, Sy = 18, Sw = 170, Sh = 128, Bulge = Curvature * 40;
  const Screen = `M${Sx} ${Sy}Q${Sx + Sw / 2} ${Sy - Bulge} ${Sx + Sw} ${Sy}Q${Sx + Sw + Bulge} ${Sy + Sh / 2} ${Sx + Sw} ${Sy + Sh}Q${Sx + Sw / 2} ${Sy + Sh + Bulge} ${Sx} ${Sy + Sh}Q${Sx - Bulge} ${Sy + Sh / 2} ${Sx} ${Sy}Z`;
  const Lines = Array.from({ length: Count }, (_, Index) => {
    const T = (Index + 0.5) / Count, Y = Sy + T * Sh, Edge = Bulge * (1 - (2 * T - 1) ** 2);
    return `M${Sx - Edge} ${Y}Q${Sx + Sw / 2} ${Y + (T - 0.5) * Bulge * 0.6} ${Sx + Sw + Edge} ${Y}`;
  });
  const Zx = 206, Zy = 22, Zw = 100, Zh = 120;
  const Profile = Array.from({ length: 61 }, (_, Index) => {
    const T = Index / 60, Brightness = 1 - Intensity * (0.5 - 0.5 * Math.cos(T * Math.PI * 2 * 4));
    return [Zx + 10 + Brightness * (Zw - 20), Zy + T * Zh];
  });
  return (
    <>
      <clipPath id="fx-tube">
        <path d={Screen} />
      </clipPath>
      <path d={Screen} fill="#1d2a24" stroke="#5a5a5a" />
      <g clipPath="url(#fx-tube)">
        <rect x={Sx - 20} y={Sy - 20} width={Sw + 40} height={Sh + 40} fill="url(#fx-tube-glow)" />
        {Lines.map((Line, Index) => (
          <path key={Index} d={Line} stroke="#000" strokeWidth={Math.max(0.4, (Sh / Count) * 0.5)} opacity={Intensity} fill="none" />
        ))}
      </g>
      <defs>
        <radialGradient id="fx-tube-glow" cx="50%" cy="45%" r="65%">
          <stop offset="0" stopColor="#6fe0a8" stopOpacity=".55" />
          <stop offset="1" stopColor="#6fe0a8" stopOpacity="0" />
        </radialGradient>
      </defs>
      <rect x={Sx + Sw * 0.62} y={Sy + Sh * 0.3} width="22" height="26" fill="none" stroke="#f2f2f2" strokeDasharray="2 2" />
      <line x1={Sx + Sw * 0.62 + 22} y1={Sy + Sh * 0.3} x2={Zx} y2={Zy} stroke="#ffffff30" />
      <rect x={Zx} y={Zy} width={Zw} height={Zh} rx="8" fill="#121212" stroke="#ffffff14" />
      {P("Phosphor mask") &&
        Array.from({ length: 12 }, (_, Index) => (
          <rect key={Index} x={Zx + 6 + Index * 7.4} y={Zy + 4} width="6" height={Zh - 8} fill={["#ff4d4d", "#4dff88", "#4d8cff"][Index % 3]} opacity=".12" />
        ))}
      <path d={Path(Profile)} fill="none" stroke="#6fe0a8" strokeWidth="1.6" />
      <text x={Zx + Zw / 2} y={Zy + Zh + 13} textAnchor="middle">brightness across 4 lines</text>
      <text x={Sx + Sw / 2} y={Sy + Sh + 22} textAnchor="middle">{Count} lines · curvature {Fixed(Curvature)}</text>
    </>
  );
}

function SharpenChart({ P }) {
  const Left = 30, Right = 306, Top = 20, Bottom = 116;
  const Rows = SharpenProfile(P);
  const Px = (X) => Left + ((X + 6) / 12) * (Right - Left);
  const Py = (L) => Bottom - L * (Bottom - Top);
  const Before = Rows.map(([X, Original]) => [Px(X), Py(Original)]);
  const After = Rows.map(([X, , Sharp]) => [Px(X), Py(Clamp(Sharp, -0.1, 1.1))]);
  const Threshold = P("Threshold");
  return (
    <>
      <defs>
        <linearGradient id="fx-edge-before" x1="0" x2="1">
          {Rows.filter((_, Index) => Index % 10 === 0).map(([X, Original]) => {
            const Level = Math.round(Clamp(Original, 0, 1) * 255);
            return <stop key={X} offset={(X + 6) / 12} stopColor={`rgb(${Level},${Level},${Level})`} />;
          })}
        </linearGradient>
        <linearGradient id="fx-edge-after" x1="0" x2="1">
          {Rows.filter((_, Index) => Index % 5 === 0).map(([X, , Sharp]) => {
            const Level = Math.round(Clamp(Sharp, 0, 1) * 255);
            return <stop key={X} offset={(X + 6) / 12} stopColor={`rgb(${Level},${Level},${Level})`} />;
          })}
        </linearGradient>
      </defs>
      <text x={Left} y="14">BRIGHTNESS</text>
      {[0, 0.5, 1].map((Level) => (
        <line key={Level} x1={Left} x2={Right} y1={Py(Level)} y2={Py(Level)} stroke="#ffffff10" strokeDasharray="2 4" />
      ))}
      <path d={Path([...After, ...Before.slice().reverse()])} fill="#5fe0ff22" />
      <path d={Path(Before)} fill="none" stroke="#8a8a8a" strokeWidth="1.2" strokeDasharray="3 3" />
      <path d={Path(After)} fill="none" stroke="#5fe0ff" strokeWidth="1.7" />
      <text x={Px(-4.5)} y={Py(0.2) + 12}>dark edge</text>
      <text x={Px(4.5)} y={Py(0.8) - 6} textAnchor="end" className="fx-accent">bright edge</text>
      <rect x={Left} y="126" width={Right - Left} height="12" rx="3" fill="url(#fx-edge-before)" />
      <rect x={Left} y="142" width={Right - Left} height="12" rx="3" fill="url(#fx-edge-after)" />
      <text x={Left - 4} y="135" textAnchor="end">before</text>
      <text x={Left - 4} y="151" textAnchor="end">after</text>
      <text x={Right} y="166" textAnchor="end">details under {Math.round(Threshold * 255)} / 255 are left alone</text>
    </>
  );
}

const Charts = {
  "motion-blur": MotionBlurChart,
  "heat-shimmer": HeatShimmerChart,
  glitch: GlitchChart,
  "chromatic-aberration": ChromaticChart,
  "film-grain": FilmGrainChart,
  "lens-distortion": LensDistortionChart,
  scanlines: ScanlinesChart,
  sharpen: SharpenChart,
};

export function EffectStudy({ Panel, Read, Assign, Active }) {
  const Drag = useRef(false);
  const Lead = Headline(Panel, Read);
  const Chart = Charts[Panel];
  const [Integer, Fraction] = String(Lead.Whole).split(".");
  const Pick = (Event) => {
    if (!Lead.Drag || !Assign) return;
    const [Key, Low, High] = Lead.Drag;
    const Box = Event.currentTarget.getBoundingClientRect();
    const Ratio = Clamp((Event.clientX - Box.left) / Box.width, 0, 1);
    const Step = High - Low > 20 ? 1 : 0.01;
    Assign(Key, +(Math.round((Low + Ratio * (High - Low)) / Step) * Step).toFixed(2));
  };
  return (
    <section className="generic-card effect-study" data-card="Effect study" data-effect-study={Panel}>
      <header className="effect-study-head">
        <h3>{Lead.Title}</h3>
        <span>{Active ? "Authored values" : "Effect disabled"}</span>
      </header>
      <div className="effect-figure">
        <span className="effect-figure-number">
          {Integer}
          {Fraction !== undefined && <span className="effect-figure-fraction">.{Fraction}</span>}
        </span>
        <small>{Lead.Unit}</small>
      </div>
      <div className={"effect-chart" + (Active ? "" : " is-off")}>
        <svg
          viewBox="0 0 320 172"
          role="img"
          aria-label={Lead.Title}
          data-effect-chart={Panel}
          onPointerDown={(Event) => {
            Drag.current = true;
            Event.currentTarget.setPointerCapture(Event.pointerId);
            Pick(Event);
          }}
          onPointerMove={(Event) => Drag.current && Pick(Event)}
          onPointerUp={() => (Drag.current = false)}
          onPointerCancel={() => (Drag.current = false)}
        >
          {Chart && <Chart P={Read} />}
        </svg>
      </div>
      <div className="effect-readouts">
        {Readouts(Panel, Read).map(([Label, Text]) => (
          <div key={Label}>
            <span>{Label}</span>
            <b>{Text}</b>
          </div>
        ))}
      </div>
      <p className="graph-note">{Lead.Caption}. Diagram of the authored values; not the native renderer.</p>
    </section>
  );
}
