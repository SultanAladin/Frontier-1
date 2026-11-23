//============================================================================================================================================
//                                                            RENDERSTUDIES.JSX
//============================================================================================================================================
// 🎛️ Small drawings for the render components: each is drawn from the camera's render settings and nothing animates.

import React, { useEffect, useRef } from "react";
import { RENDER_ENGINES } from "./RenderSettings.js";

// The studies read a small common shape ({ ShadowAlgorithm, ShadowSoftness, ShadowMapRes, … }); each renderer's cards map their own
// keys onto it.
const UsesShadowMap = (Settings) => ["contact_pcss", "pcss", "smrt", "pcf"].includes(Settings.ShadowAlgorithm);

//------------------------------------------------------------------------------------------------------------------------
//                                                   STUDIES
//------------------------------------------------------------------------------------------------------------------------

// Engine ladder: the six engines as rising steps, the active one lit.
export function EngineLadder({ Active }) {
  const Step = 300 / RENDER_ENGINES.length;
  return (
    <svg className="rs-study" viewBox="0 0 300 106" role="img" aria-label="Render engine ladder">
      {RENDER_ENGINES.map((Engine, Index) => {
        const Height = 22 + Index * 11, X = Index * Step + 3, On = Engine.id === Active;
        return (
          <g key={Engine.id}>
            <rect x={X} y={82 - Height} width={Step - 6} height={Height} rx="5" fill={On ? "#e6e6e6" : "#2a2a2a"} stroke={On ? "#ffffff" : "#ffffff14"} />
            <text x={X + (Step - 6) / 2} y={78} textAnchor="middle" style={{ fill: On ? "#151515" : "#8a8a8a", fontSize: 13 }}>{Engine.ordinal}</text>
            <text x={X + (Step - 6) / 2} y={98} textAnchor="middle" style={{ fill: On ? "#ececec" : "#7a7a7a", fontSize: 10.5 }}>{Engine.short}</text>
          </g>
        );
      })}
    </svg>
  );
}

// Area light over an occluder: umbra and penumbra on the ground follow the softness; ticks show shadow-map texels or traced rays.
export function PenumbraStudy({ Settings }) {
  const Ground = 110, Light = 30, Blocker = 66, Reach = (Ground - Light) / (Blocker - Light);
  const Width = 14 + 34 * Settings.ShadowSoftness, Centre = 150;
  const [Ll, Lr] = [Centre - Width / 2, Centre + Width / 2];
  const [O1, O2] = [118, 182];
  const At = (L, O) => L + (O - L) * Reach;
  const OuterLeft = At(Lr, O1), InnerLeft = At(Ll, O1), InnerRight = At(Lr, O2), OuterRight = At(Ll, O2);
  const UmbraLeft = Math.min(InnerLeft, (InnerLeft + InnerRight) / 2), UmbraRight = Math.max(UmbraLeft, InnerRight);
  const Map_ = UsesShadowMap(Settings);
  const Texel = (Settings.ShadowMapRes === 1024 ? 12 : 6) * (Settings.HalfResShadows ? 2 : 1);
  const Rays = Settings.ShadowAlgorithm === "bvh" || Settings.ShadowAlgorithm === "contact_pcss" ? Settings.ShadowRays : Settings.ShadowAlgorithm === "smrt" ? Settings.SmrtRays : 0;
  const Probe = (OuterLeft + InnerLeft) / 2;
  return (
    <svg className="rs-study" viewBox="0 0 300 132" role="img" aria-label="Shadow penumbra study">
      <defs>
        <linearGradient id="rs-pen-left" x1="0" x2="1">
          <stop offset="0" stopColor="#7ab8ff" stopOpacity="0" />
          <stop offset="1" stopColor="#7ab8ff" stopOpacity="0.42" />
        </linearGradient>
        <linearGradient id="rs-pen-right" x1="1" x2="0">
          <stop offset="0" stopColor="#7ab8ff" stopOpacity="0" />
          <stop offset="1" stopColor="#7ab8ff" stopOpacity="0.42" />
        </linearGradient>
      </defs>
      <rect x={Ll} y={Light - 3} width={Width} height="6" rx="3" fill="#f2e2b8" />
      <text x="8" y="14" style={{ fill: "#8a8a8a", fontSize: 12.5 }}>area light · softness {Settings.ShadowSoftness.toFixed(2)}</text>
      <g stroke="#ffffff22" strokeWidth="0.8" strokeDasharray="2 3">
        <path d={`M${Lr} ${Light}L${OuterLeft} ${Ground}M${Ll} ${Light}L${InnerLeft} ${Ground}M${Ll} ${Light}L${OuterRight} ${Ground}M${Lr} ${Light}L${InnerRight} ${Ground}`} />
      </g>
      <rect x={O1} y={Blocker - 3} width={O2 - O1} height="6" rx="2" fill="#5a5a5a" />
      <rect x={OuterLeft} y={Ground - 7} width={Math.max(0, InnerLeft - OuterLeft)} height="7" fill="url(#rs-pen-left)" />
      <rect x={UmbraLeft} y={Ground - 7} width={Math.max(0, UmbraRight - UmbraLeft)} height="7" fill="#7ab8ff" opacity="0.5" />
      <rect x={InnerRight} y={Ground - 7} width={Math.max(0, OuterRight - InnerRight)} height="7" fill="url(#rs-pen-right)" />
      <path d={`M8 ${Ground}H292`} stroke="#6a6a6a" />
      {Map_ &&
        Array.from({ length: Math.floor(284 / Texel) + 1 }, (_, Index) => (
          <path key={Index} d={`M${8 + Index * Texel} ${Ground}v4`} stroke="#ffffff26" />
        ))}
      {Array.from({ length: Math.min(16, Rays) }, (_, Index) => {
        const Target = Ll + ((Index + 0.5) / Math.min(16, Rays)) * Width;
        return <path key={"r" + Index} d={`M${Probe} ${Ground}L${Target} ${Light}`} stroke="#7ab8ff" strokeOpacity="0.55" strokeWidth="0.7" />;
      })}
      <text x="8" y={Ground + 18} style={{ fill: "#8a8a8a", fontSize: 12.5 }}>
        penumbra {(InnerLeft - OuterLeft).toFixed(0)} px · {Map_ ? `${Settings.ShadowMapRes}² map${Settings.HalfResShadows ? " · half res" : ""}` : "no shadow map"}
      </text>
    </svg>
  );
}

// Hemisphere above a shading point: cones for SDF GI, cosine-distributed rays for BVH GI, a flat dome for ambient fill.
export function HemisphereStudy({ Settings }) {
  const Cx = 150, Cy = 104, R = 82;
  const Mode = Settings.GiAlgorithm;
  return (
    <svg className="rs-study" viewBox="0 0 300 130" role="img" aria-label="Indirect light hemisphere">
      <path d={`M${Cx - R} ${Cy}A${R} ${R} 0 0 1 ${Cx + R} ${Cy}`} fill={Mode === "off" ? "#7be0ad14" : "none"} stroke="#ffffff1c" />
      <path d={`M14 ${Cy}H286`} stroke="#6a6a6a" />
      {Mode === "sdf" &&
        Array.from({ length: Settings.GiCones }, (_, Index) => {
          const Angle = Math.PI * ((Index + 0.5) / Settings.GiCones), Half = (Math.PI / Settings.GiCones) * 0.36;
          const P = (A, L) => [Cx + Math.cos(Math.PI - A) * L, Cy - Math.sin(A) * L];
          const [X1, Y1] = P(Angle - Half, R * 0.94), [X2, Y2] = P(Angle + Half, R * 0.94);
          return <path key={Index} d={`M${Cx} ${Cy}L${X1} ${Y1}A${R * 0.94} ${R * 0.94} 0 0 1 ${X2} ${Y2}Z`} fill="#7be0ad" fillOpacity={0.12 + 0.22 * Math.sin(Angle)} stroke="#7be0ad" strokeOpacity="0.5" strokeWidth="0.7" />;
        })}
      {Mode === "bvh" &&
        Array.from({ length: Settings.GiSamples }, (_, Index) => {
          // Cosine-weighted directions on a Fibonacci spiral, projected onto the section plane.
          const U = (Index + 0.5) / Settings.GiSamples, Phi = Index * 2.39996;
          const Sin = Math.sqrt(U), X = Sin * Math.cos(Phi), Up = Math.sqrt(1 - U);
          const Length = R * (0.55 + 0.4 * Up);
          return <path key={Index} d={`M${Cx} ${Cy}L${Cx + X * Length} ${Cy - Up * Length}`} stroke="#7be0ad" strokeOpacity="0.75" strokeWidth="0.9" />;
        })}
      <circle cx={Cx} cy={Cy} r="3" fill="#e6e6e6" />
      <text x="14" y="14" style={{ fill: "#8a8a8a", fontSize: 12.5 }}>
        {Mode === "sdf" ? `${Settings.GiCones} cones through the distance field` : Mode === "bvh" ? `${Settings.GiSamples} samples / pixel · 2 bounces` : "no tracing · sky and ambient fill only"}
      </text>
      <text x="286" y="124" textAnchor="end" style={{ fill: "#8a8a8a", fontSize: 12.5 }}>fill × {Settings.FillBoost.toFixed(2)}</text>
    </svg>
  );
}

// A reflected ray between the floor and a wall, one segment per bounce; extra rays fan out at the first hit.
export function BounceStudy({ Settings }) {
  const Traced = Settings.ReflectionAlgorithm === "bvh";
  const Points = [[40, 18], [118, 98]];
  const Bounces = Traced ? Settings.ReflectionBounces : 0;
  for (let Index = 0; Index < Bounces; Index++) Points.push(Index % 2 === 0 ? [256, 52 + Index * 6] : [170 + Index * 6, 98]);
  const Spread = Traced ? Settings.ReflectionRays : 0;
  return (
    <svg className="rs-study" viewBox="0 0 300 118" role="img" aria-label="Reflection bounce study">
      <path d="M10 98H290M256 4V98" stroke="#6a6a6a" />
      <circle cx="40" cy="18" r="4" fill="#e6e6e6" />
      <polyline points={Points.map((Point) => Point.join(",")).join(" ")} fill="none" stroke="#c5a3ff" strokeWidth="1.3" />
      {Array.from({ length: Math.max(0, Spread - 1) }, (_, Index) => {
        const Tilt = ((Index + 1) / Spread - 0.5) * 0.6;
        return <path key={Index} d={`M118 98l${Math.cos(-0.62 + Tilt) * 70} ${Math.sin(-0.62 + Tilt) * 70}`} stroke="#c5a3ff" strokeOpacity="0.4" strokeWidth="0.8" />;
      })}
      {!Traced && <path d="M118 98L170 22" stroke="#c5a3ff" strokeDasharray="3 3" />}
      {Points.slice(1).map(([X, Y], Index) => (
        <circle key={Index} cx={X} cy={Y} r="2.6" fill="#c5a3ff" />
      ))}
      <text x="14" y="114" style={{ fill: "#8a8a8a", fontSize: 12.5 }}>
        {Traced ? `${Settings.ReflectionRays} rays · ${Settings.ReflectionBounces} bounce${Settings.ReflectionBounces > 1 ? "s" : ""}` : "sky, clouds, moon and stars only"}
      </text>
    </svg>
  );
}

// Output frame against the internal render frame for the resolution scale.
export function ScaleStudy({ Settings }) {
  const Fraction = Settings.RenderScale / 100;
  const Width = 176, Height = 99, Left = 62, Top = 8;
  const Inner = [Width * Math.min(Fraction, 1.25), Height * Math.min(Fraction, 1.25)];
  return (
    <svg className="rs-study" viewBox="0 0 300 126" role="img" aria-label="Resolution scale study">
      <rect x={Left} y={Top} width={Width} height={Height} rx="4" fill="#202020" stroke="#ffffff30" />
      <rect x={Left + (Width - Inner[0]) / 2} y={Top + (Height - Inner[1]) / 2} width={Inner[0]} height={Inner[1]} rx="3" fill="none" stroke="#e6e6e6" strokeDasharray={Fraction > 1 ? "4 3" : "none"} />
      <text x={Left + Width / 2} y={Top + Height / 2 + 3} textAnchor="middle" style={{ fill: "#cfcfcf", fontSize: 9 }}>
        {Math.round(1920 * Fraction)} × {Math.round(1080 * Fraction)}
      </text>
      <text x={Left + Width / 2} y={Top + Height + 14} textAnchor="middle" style={{ fill: "#8a8a8a", fontSize: 12.5 }}>
        for 1920 × 1080 output · {Fraction > 1 ? "supersampled" : Fraction < 1 ? "upscaled" : "native"}
      </text>
    </svg>
  );
}


// Shadow-map texels near a shadow edge with the filter footprint on top: 3 × 3 cells for PCF, a Vogel disk of taps for PCSS (with
// the dashed blocker search), short contact rays for Contact + PCSS and quadratic march steps for SMRT.
export function KernelStudy({ Settings }) {
  const Cell = (Settings.ShadowMapRes === 1024 ? 18 : 9) * (Settings.HalfResShadows ? 2 : 1);
  const Left = 10, Top = 8, Right = 290, Bottom = 118, Cx = 150, Cy = 63;
  const Columns = Math.ceil((Right - Left) / Cell), Rows = Math.ceil((Bottom - Top) / Cell);
  const Mode = Settings.ShadowAlgorithm;
  const Radius = 10 + 16 * Settings.ShadowSoftness;
  const Taps = Mode === "pcss" || Mode === "contact_pcss" ? Settings.PcssTaps : 0;
  // Texels whose centre is left of the slanted blocker edge read as shadowed.
  const Shadowed = (X, Y) => X < 150 + (Y - Cy) * 0.55;
  return (
    <svg className="rs-study" viewBox="0 0 300 140" role="img" aria-label="Shadow map filter footprint" data-study="kernel">
      <defs>
        <clipPath id="rs-kernel-clip">
          <rect x={Left} y={Top} width={Right - Left} height={Bottom - Top} rx="8" />
        </clipPath>
      </defs>
      <g clipPath="url(#rs-kernel-clip)">
        {Array.from({ length: Rows }, (_, Row) =>
          Array.from({ length: Columns }, (_, Column) => {
            const X = Left + Column * Cell, Y = Top + Row * Cell;
            return <rect key={Row + "-" + Column} x={X} y={Y} width={Cell} height={Cell} fill={Shadowed(X + Cell / 2, Y + Cell / 2) ? "#262c33" : "#1b1b1b"} stroke="#ffffff0d" strokeWidth="0.6" />;
          }),
        )}
        <path d={`M${150 + (Top - Cy) * 0.55} ${Top}L${150 + (Bottom - Cy) * 0.55} ${Bottom}`} stroke="#7ab8ff55" strokeDasharray="3 3" />
      </g>
      {Mode === "pcf" && <rect x={Cx - Cell * 1.5} y={Cy - Cell * 1.5} width={Cell * 3} height={Cell * 3} fill="#7ab8ff22" stroke="#7ab8ff" strokeWidth="1.2" rx="1.5" />}
      {Taps > 0 && (
        <>
          <circle cx={Cx} cy={Cy} r={Radius * 1.35} fill="none" stroke="#7ab8ff66" strokeDasharray="2 3" />
          {Array.from({ length: Taps }, (_, Index) => {
            const R = Radius * Math.sqrt((Index + 0.5) / Taps), A = Index * 2.39996;
            return <circle key={Index} cx={Cx + Math.cos(A) * R} cy={Cy + Math.sin(A) * R} r="1.7" fill="#7ab8ff" />;
          })}
        </>
      )}
      {Mode === "contact_pcss" &&
        Array.from({ length: Math.min(16, Settings.ShadowRays) }, (_, Index) => {
          const A = -Math.PI / 2 - 0.5 + (Index / Math.max(1, Settings.ShadowRays - 1)) * 1;
          return <path key={"c" + Index} d={`M${Cx} ${Cy}l${Math.cos(A) * 30} ${Math.sin(A) * 30}`} stroke="#f2e2b8" strokeOpacity="0.7" strokeWidth="0.8" />;
        })}
      {Mode === "smrt" && (
        <>
          <path d={`M${Cx} ${Cy}L${Cx + 120} ${Top + 6}`} stroke="#7ab8ff88" />
          {Array.from({ length: Settings.SmrtRays }, (_, Index) => {
            const T = ((Index + 1) / Settings.SmrtRays) ** 2;
            return <circle key={Index} cx={Cx + 120 * T} cy={Cy + (Top + 6 - Cy) * T} r="2.4" fill="#7ab8ff" />;
          })}
        </>
      )}
      <circle cx={Cx} cy={Cy} r="2.6" fill="#ececec" />
      <text x="10" y="134">
        {Settings.ShadowMapRes}² map{Settings.HalfResShadows ? " · half res" : ""} · {Mode === "pcf" ? "9 taps" : Mode === "smrt" ? `${Settings.SmrtRays} march steps` : `${Taps} taps`}
        {Mode === "contact_pcss" ? ` + ${Settings.ShadowRays} contact rays` : ""}
      </text>
    </svg>
  );
}

// Sphere tracing towards an area light: each step jumps by the distance to the nearest surface (drawn as its circle). Penumbra
// modes add the cone to the light's edges, voxel cells, cap samples or reference rays.
export function SdfTraceStudy({ Settings, Ordinal }) {
  const P = [44, 118], Light = [252, 22], Half = 10 + 14 * Settings.ShadowSoftness;
  const Blocker = [156, 74, 20];
  const Distance = (X, Y) => Math.min(Math.hypot(X - Blocker[0], Y - Blocker[1]) - Blocker[2], 118 - Y + 40);
  const Dx = Light[0] - P[0], Dy = Light[1] - P[1], Length = Math.hypot(Dx, Dy);
  const Steps = [];
  for (let T = 4, Guard = 0; T < Length && Guard < 14; Guard++) {
    const X = P[0] + (Dx / Length) * T, Y = P[1] + (Dy / Length) * T, D = Distance(X, Y);
    Steps.push([X, Y, Math.max(0, D)]);
    if (D < 1) break;
    T += Math.max(D, 3);
  }
  const Cone = Ordinal >= 1;
  return (
    <svg className="rs-study" viewBox="0 0 300 140" role="img" aria-label="Sphere tracing study" data-study="sdf-trace">
      <path d="M8 118H292" stroke="#6a6a6a" />
      {Cone && <path d={`M${P[0]} ${P[1]}L${Light[0] - Half} ${Light[1]}L${Light[0] + Half} ${Light[1]}Z`} fill="#7be0ad14" stroke="#7be0ad44" strokeDasharray="3 3" />}
      {(Ordinal === 2 || Ordinal === 3) &&
        Array.from({ length: 9 }, (_, Index) => {
          const T = (Index + 1) / 10, Size = 6 + T * 14;
          return <rect key={Index} x={P[0] + Dx * T - Size / 2} y={P[1] + Dy * T - Size / 2} width={Size} height={Size} fill="none" stroke="#7be0ad55" />;
        })}
      {Ordinal === 3 &&
        [-0.9, -0.45, 0.45, 0.9].map((Tilt) => (
          <path key={Tilt} d={`M${P[0]} ${P[1]}l${Math.sin(Tilt) * 40} ${-Math.cos(Tilt) * 40}`} stroke="#7be0ad88" strokeWidth="2.4" strokeLinecap="round" />
        ))}
      {Ordinal === 4 &&
        Array.from({ length: 16 }, (_, Index) => {
          const R = Half * Math.sqrt((Index + 0.5) / 16), A = Index * 2.39996;
          return <circle key={Index} cx={Light[0] + Math.cos(A) * R} cy={Light[1] + Math.sin(A) * R * 0.45} r="1.4" fill="#7be0ad" />;
        })}
      {Ordinal === 5 &&
        Array.from({ length: 14 }, (_, Index) => {
          const X = Light[0] - Half + ((Index + 0.5) / 14) * Half * 2;
          return <path key={Index} d={`M${P[0]} ${P[1]}L${X} ${Light[1]}`} stroke="#7be0ad" strokeOpacity="0.28" strokeWidth="0.7" />;
        })}
      <circle cx={Blocker[0]} cy={Blocker[1]} r={Blocker[2]} fill="#3a3a3a" stroke="#8a8a8a" />
      {Steps.map(([X, Y, D], Index) => (
        <g key={Index}>
          <circle cx={X} cy={Y} r={Math.min(D, 70)} fill="none" stroke="#ffffff" strokeOpacity={0.14} />
          <circle cx={X} cy={Y} r="2" fill="#ececec" />
        </g>
      ))}
      <path d={`M${P[0]} ${P[1]}L${Light[0]} ${Light[1]}`} stroke="#ececec" strokeOpacity="0.5" strokeDasharray="2 3" />
      <rect x={Light[0] - Half} y={Light[1] - 3} width={Half * 2} height="6" rx="3" fill="#f2e2b8" />
      <circle cx={P[0]} cy={P[1]} r="3" fill="#7be0ad" />
      <text x="10" y="134">{Steps.length} sphere-trace steps · {Steps[Steps.length - 1]?.[2] < 1 ? "hits the blocker" : "reaches the light"}</text>
    </svg>
  );
}

// MatCap sphere shaded in the browser from the MatCap settings. Dragging around it sets the studio light angle.
const Hex = (Text) => [1, 3, 5].map((At) => parseInt(String(Text).slice(At, At + 2), 16) / 255 || 0.8);
export function MatcapBall({ Settings, SetLight }) {
  const Ref = useRef(null);
  const { MatcapChannel: Channel, MatcapLight: Degrees, MatcapRim: Rim, MatcapCavity: Cavity, MatcapWire: Wire, MatcapTint: Tint } = Settings;
  useEffect(() => {
    const Canvas = Ref.current;
    if (!Canvas) return;
    const Pixels = 360, Brush = Canvas.getContext("2d");
    Canvas.width = Canvas.height = Pixels;
    const Picture = Brush.createImageData(Pixels, Pixels), Data = Picture.data;
    const Angle = (Degrees * Math.PI) / 180;
    let [Lx, Ly, Lz] = [Math.cos(Angle) * 0.8, Math.sin(Angle) * 0.8, 0.6];
    const Hx = Lx, Hy = Ly, Hz = Lz + 1, Hl = Math.hypot(Hx, Hy, Hz);
    const [Tr, Tg, Tb] = Hex(Tint);
    for (let Y = 0; Y < Pixels; Y++)
      for (let X = 0; X < Pixels; X++) {
        const Nx = ((X + 0.5) / Pixels) * 2.2 - 1.1, Ny = -(((Y + 0.5) / Pixels) * 2.2 - 1.1), R2 = Nx * Nx + Ny * Ny;
        const At = (Y * Pixels + X) * 4;
        if (R2 > 1) continue;
        const Nz = Math.sqrt(1 - R2);
        const Groove = Math.max(0, 1 - Math.abs(Ny + 0.28) / 0.05, 1 - Math.abs(Nx + 0.36 * Nz) / 0.045);
        const Occlusion = 1 - Cavity * Math.min(1, Groove) * 0.85;
        const Diffuse = Math.max(0, Nx * Lx + Ny * Ly + Nz * Lz);
        const Facing = Math.max(0, (Nx * Hx + Ny * Hy + Nz * Hz) / Hl);
        const Specular = Facing ** 48 * 0.6;
        const Edge = (1 - Nz) ** 2.4 * Rim;
        let C;
        if (Channel === "normal") C = [Nx * 0.5 + 0.5, Ny * 0.5 + 0.5, Nz * 0.5 + 0.5];
        else if (Channel === "roughness") { const G = 0.15 + 0.85 * Facing ** 6; C = [G, G, G]; }
        else if (Channel === "cavity") { const G = 0.9 * Occlusion; C = [G, G, G]; }
        else C = [Tr, Tg, Tb].map((Base) => Base * (0.16 + 0.84 * Diffuse) * Occlusion + Specular + Edge * 0.8);
        Data[At] = Math.min(255, C[0] * 255);
        Data[At + 1] = Math.min(255, C[1] * 255);
        Data[At + 2] = Math.min(255, C[2] * 255);
        Data[At + 3] = Math.min(255, (1 - Math.sqrt(R2)) * Pixels * 1.2 * 255);
      }
    Brush.putImageData(Picture, 0, 0);
    const C = Pixels / 2, Radius = Pixels / 2.2;
    if (Wire) {
      Brush.strokeStyle = "rgba(255,255,255,0.28)";
      Brush.lineWidth = 1;
      for (let Index = 1; Index < 6; Index++) {
        const Lat = (Index / 6) * Math.PI - Math.PI / 2;
        Brush.beginPath();
        Brush.ellipse(C, C - Math.sin(Lat) * Radius, Math.cos(Lat) * Radius, Math.cos(Lat) * Radius * 0.18, 0, 0, Math.PI * 2);
        Brush.stroke();
        Brush.beginPath();
        Brush.ellipse(C, C, Math.abs(Math.cos((Index / 6) * Math.PI)) * Radius, Radius, 0, 0, Math.PI * 2);
        Brush.stroke();
      }
    }
    Brush.fillStyle = "#edc181";
    Brush.beginPath();
    Brush.arc(C + Math.cos(Angle) * Radius * 1.08, C - Math.sin(Angle) * Radius * 1.08, 7, 0, Math.PI * 2);
    Brush.fill();
  }, [Channel, Degrees, Rim, Cavity, Wire, Tint]);
  const Aim = (Event) => {
    const Box = Event.currentTarget.getBoundingClientRect();
    const Next = (Math.atan2(-(Event.clientY - Box.top - Box.height / 2), Event.clientX - Box.left - Box.width / 2) * 180) / Math.PI;
    SetLight(Math.round((Next + 360) % 360));
  };
  return (
    <canvas
      ref={Ref}
      className="rc-matcap"
      role="img"
      aria-label={`MatCap sphere · ${Channel} · light ${Degrees}°`}
      onPointerDown={(Event) => {
        Event.currentTarget.setPointerCapture(Event.pointerId);
        Aim(Event);
      }}
      onPointerMove={(Event) => Event.buttons && Aim(Event)}
    />
  );
}

// Per-pixel work split into shadows, GI and reflections.
export function BudgetBar({ Budget, Tones }) {
  const Parts = [["Shadows", Budget.Shadow, Tones.Shadow], ["GI", Budget.Gi, Tones.Gi], ["Reflections", Budget.Reflection, Tones.Reflection]];
  const Sum = Math.max(1, Parts.reduce((Total, [, Amount]) => Total + Amount, 0));
  let X = 8;
  return (
    <svg className="rs-study" viewBox="0 0 300 52" role="img" aria-label="Samples per pixel by pass" data-study="budget">
      <rect x="8" y="8" width="284" height="14" rx="7" fill="#232323" />
      {Parts.map(([Label, Amount, Tone]) => {
        const Width = (Amount / Sum) * 284, Start = X;
        X += Width;
        return Amount > 0 ? <rect key={Label} x={Start + 1} y="8" width={Math.max(0, Width - 2)} height="14" rx="6" fill={Tone} opacity="0.75" /> : null;
      })}
      {Parts.map(([Label, Amount, Tone], Index) => (
        <g key={Label} transform={`translate(${8 + Index * 98} 40)`}>
          <circle cx="4" cy="-4" r="3.5" fill={Tone} opacity={Amount ? 1 : 0.3} />
          <text x="12" y="0">{Label} {Amount}</text>
        </g>
      ))}
    </svg>
  );
}

// MatCap studio shadow: the sphere over an implied floor with the chosen shadow (drop ellipse, contact rays, cavity grooves).
export function StudioShadowStudy({ Mode, Strength, Softness, Light }) {
  const Cx = 150, R = 32, Floor = 104, Cy = Floor - R;
  const Angle = (Light * Math.PI) / 180;
  const Shift = -Math.cos(Angle) * 26;
  const Blur = 1.5 + Softness * 2.5;
  return (
    <svg className="rs-study" viewBox="0 0 300 140" role="img" aria-label="Studio shadow study" data-study="studio-shadow">
      <defs>
        <radialGradient id="rs-studio-ball" cx="0.38" cy="0.32" r="0.8">
          <stop offset="0" stopColor="#efe6d9" />
          <stop offset="0.5" stopColor="#a99d8e" />
          <stop offset="1" stopColor="#3a3530" />
        </radialGradient>
        <filter id="rs-studio-blur" x="-50%" y="-200%" width="200%" height="500%">
          <feGaussianBlur stdDeviation={Blur} />
        </filter>
      </defs>
      <path d={`M60 ${Floor - 12}H240L282 ${Floor + 14}H18Z`} fill="#3c3a37" />
      {Mode === "drop" && <ellipse cx={Cx + Shift} cy={Floor} rx={R * (0.85 + Softness * 0.25)} ry={5 + Softness * 1.5} fill="#000" opacity={0.2 + Strength * 0.8} filter="url(#rs-studio-blur)" />}
      <circle cx={Cx} cy={Cy} r={R} fill="url(#rs-studio-ball)" />
      {Mode === "cavity" && (
        <g stroke="#151515" strokeOpacity={0.3 + Strength * 0.6} strokeWidth={1.2 + Strength * 1.6} fill="none">
          <path d={`M${Cx - R + 3} ${Cy + 9}Q${Cx} ${Cy + 20} ${Cx + R - 3} ${Cy + 9}`} />
          <path d={`M${Cx - 10} ${Cy - R + 2}Q${Cx - 18} ${Cy} ${Cx - 10} ${Cy + R - 2}`} />
        </g>
      )}
      {Mode === "contact" &&
        Array.from({ length: 6 }, (_, Index) => {
          const X = Cx - R * 0.8 + Index * ((R * 1.6) / 5);
          return <path key={Index} d={`M${X} ${Floor}l${Math.cos(Angle) * 18} ${-Math.abs(Math.sin(Angle)) * 18 - 4}`} stroke="#f2e2b8" strokeOpacity={0.35 + Strength * 0.5} strokeWidth="0.8" />;
        })}
      <circle cx={Cx + Math.cos(Angle) * (R + 18)} cy={Cy - Math.sin(Angle) * (R + 18)} r="4" fill="#edc181" />
      <text x="14" y="16">{Mode === "none" ? "no shadow" : `strength ${Math.round(Strength * 100)} % · softness ${Softness.toFixed(2)}`}</text>
    </svg>
  );
}

// Follow-up 46: ambient occlusion per renderer. A point on the floor beside a wall, with the method's samples: XeGTAO horizon
// slices, SDF taps / cones / clipmap voxels, or traced rays (with a bent normal, or GI paths). Radius is 0–1 of the drawing's reach.
export function OcclusionStudy({ Method = "off", Count = 4, Radius = 0.3 }) {
  const Floor = 104, Wall = 214, Px = 182, Reach = 24 + 70 * Math.max(0, Math.min(1, Radius));
  const Tone = "#e0b07b";
  const Hit = (Dx, Dy) => {
    // Distance along (Dx, Dy) from the point to the wall, or Infinity when the ray leaves past it.
    if (Dx <= 0) return Infinity;
    const T = (Wall - Px) / Dx;
    return Floor - Dy * T >= 20 ? T : Infinity;
  };
  const Rays = (Total) =>
    Array.from({ length: Total }, (_, Index) => {
      const U = (Index + 0.5) / Total, Angle = Math.acos(1 - 2 * U);
      const Dx = Math.cos(Angle), Dy = Math.sin(Angle) || 0.01;
      const Length = Math.min(Reach, Hit(Dx, Dy));
      return { Dx, Dy, Length, Blocked: Length < Reach };
    });
  let Body = null, Caption = "no ambient occlusion";
  if (Method === "xegtao") {
    const Slices = Math.max(1, Count);
    const Right = Math.atan2(Math.min(84, Reach * 0.9) * (Reach > Wall - Px ? 1 : 0.15), Wall - Px);
    Body = (
      <>
        <path d={`M${Px} ${Floor}L${Px - Reach} ${Floor}A${Reach} ${Reach} 0 0 1 ${Px + Math.cos(Right) * Reach} ${Floor - Math.sin(Right) * Reach}Z`} fill={Tone} fillOpacity="0.12" />
        {Array.from({ length: Slices }, (_, Index) => {
          const Tilt = (Index + 0.5) / Slices - 0.5;
          return <ellipse key={Index} cx={Px} cy={Floor} rx={Reach} ry={Math.abs(Tilt) * 22 + 2} fill="none" stroke={Tone} strokeOpacity="0.35" strokeWidth="0.8" />;
        })}
        <path d={`M${Px} ${Floor}L${Px + Math.cos(Right) * Reach} ${Floor - Math.sin(Right) * Reach}`} stroke={Tone} strokeWidth="1.4" />
        <path d={`M${Px} ${Floor}L${Px - Reach} ${Floor}`} stroke={Tone} strokeWidth="1.4" />
      </>
    );
    Caption = `${Slices} slice${Slices === 1 ? "" : "s"} · horizon search in screen space`;
  } else if (Method === "sdf_multitap") {
    const Taps = Math.max(1, Count);
    Body = Array.from({ length: Taps }, (_, Index) => {
      const Y = Floor - ((Index + 1) / Taps) * Reach * 0.9, Distance = Math.min(Wall - Px, Floor - Y);
      return (
        <g key={Index}>
          <circle cx={Px} cy={Y} r={Distance} fill="none" stroke={Tone} strokeOpacity="0.28" strokeWidth="0.8" />
          <circle cx={Px} cy={Y} r="2.4" fill={Tone} />
        </g>
      );
    });
    Caption = `${Taps} taps along the normal · each reads the field`;
  } else if (Method === "sdf_cone") {
    const Cones = Math.max(1, Count);
    Body = Array.from({ length: Cones }, (_, Index) => {
      const Angle = Math.PI * ((Index + 0.5) / Cones), Half = (Math.PI / Cones) * 0.38;
      const Length = Math.min(Reach, Hit(Math.cos(Angle), Math.sin(Angle)));
      const P = (A) => [Px + Math.cos(A) * Length, Floor - Math.sin(A) * Length];
      const [X1, Y1] = P(Angle - Half), [X2, Y2] = P(Angle + Half);
      return <path key={Index} d={`M${Px} ${Floor}L${X1} ${Y1}L${X2} ${Y2}Z`} fill={Tone} fillOpacity={Length < Reach ? 0.42 : 0.14} stroke={Tone} strokeOpacity="0.55" strokeWidth="0.7" />;
    });
    Caption = `${Cones} cone${Cones === 1 ? "" : "s"} · darker ones meet the wall`;
  } else if (Method === "sdf_clipmap") {
    const Cell = 12, Cells = [];
    for (let X = Px - Reach; X < Px + Reach; X += Cell)
      for (let Y = Floor - Reach; Y < Floor; Y += Cell) {
        if (Math.hypot(X + Cell / 2 - Px, Y + Cell / 2 - Floor) > Reach) continue;
        const Near = Math.max(0, 1 - Math.min(Wall - (X + Cell), Floor - (Y + Cell)) / 36);
        Cells.push(<rect key={X + "," + Y} x={X + 0.5} y={Y + 0.5} width={Cell - 1} height={Cell - 1} fill={Tone} fillOpacity={0.06 + 0.4 * Near} />);
      }
    Body = <>{Cells}</>;
    Caption = "voxel clipmap · occupancy near the point";
  } else if (Method === "rtao" || Method === "rtao_bent") {
    const List = Rays(Math.min(24, Math.max(1, Count) * 4));
    let Bx = 0, By = 0;
    for (const Each of List) if (!Each.Blocked) { Bx += Each.Dx; By += Each.Dy; }
    const Bent = Math.hypot(Bx, By) || 1;
    Body = (
      <>
        <circle cx={Px} cy={Floor} r={Reach} fill="none" stroke="#ffffff1a" strokeDasharray="3 3" />
        {List.map((Each, Index) => (
          <path key={Index} d={`M${Px} ${Floor}L${Px + Each.Dx * Each.Length} ${Floor - Each.Dy * Each.Length}`} stroke={Each.Blocked ? Tone : "#ffffff"} strokeOpacity={Each.Blocked ? 0.9 : 0.25} strokeWidth="0.9" />
        ))}
        {Method === "rtao_bent" && <path d={`M${Px} ${Floor}L${Px + (Bx / Bent) * 60} ${Floor - (By / Bent) * 60}`} stroke="#7ab8ff" strokeWidth="2" />}
      </>
    );
    Caption = `${Math.max(1, Count)} ray${Count === 1 ? "" : "s"} / px · ${List.filter((Each) => Each.Blocked).length} of ${List.length} drawn hit${Method === "rtao_bent" ? " · blue: bent normal" : ""}`;
  } else if (Method === "traced_gi") {
    Body = (
      <>
        <path d={`M${Px} ${Floor}L${Wall} 52L118 20`} stroke={Tone} strokeOpacity="0.8" strokeWidth="1" fill="none" />
        <path d={`M${Px} ${Floor}L96 30`} stroke="#ffffff" strokeOpacity="0.3" strokeWidth="1" fill="none" />
        <path d={`M${Px} ${Floor}L${Wall} 86L150 ${Floor}L120 40`} stroke={Tone} strokeOpacity="0.6" strokeWidth="1" fill="none" />
      </>
    );
    Caption = "occlusion comes from the traced GI paths";
  }
  return (
    <svg className="rs-study" viewBox="0 0 300 130" role="img" aria-label="Ambient occlusion" data-ao={Method}>
      <path d={`M14 ${Floor}H${Wall}V20`} stroke="#8a8a8a" strokeWidth="1.4" fill="none" />
      <path d={`M${Wall} ${Floor}H286`} stroke="#4a4a4a" />
      <rect x={Wall} y="20" width={286 - Wall} height={Floor - 20} fill="#ffffff08" />
      <clipPath id="rs-ao-clip">
        <rect x="0" y="0" width="300" height={Floor + 1} />
      </clipPath>
      <g clipPath="url(#rs-ao-clip)">{Body}</g>
      <circle cx={Px} cy={Floor} r="3" fill="#e6e6e6" />
      <text x="14" y="122" style={{ fill: "#8a8a8a", fontSize: 11 }}>
        {Caption}
      </text>
    </svg>
  );
}
