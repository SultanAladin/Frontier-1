// Sky colour card: the whole sky as an all-sky (fisheye) camera sees it, looking straight up — zenith in the middle,
// horizon on the rim, north up and east on the left as on a sky chart. Every pixel is an analytic single-scatter estimate
// from the authored atmosphere: Rayleigh and aerosol optical depth (scaled by their scale heights), Rayleigh and
// Henyey-Greenstein phase, Kasten-Young air mass for the view and the sun, ozone absorption on the sunlight path. Under
// it, a strip of the sky at every sun height from night to noon. Drag the dome to place the preview sun, or the strip
// to set its height. Illustrative only: no multiple scattering, no curved-planet march, not the native sky.
import React, { useMemo, useId } from "react";

const Rad = Math.PI / 180;
const Hex = (Text) =>
  /^#[\da-f]{6}$/i.test(Text || "") ? [1, 3, 5].map((At) => parseInt(Text.slice(At, At + 2), 16) / 255) : [1, 1, 1];
// Kasten-Young (1989) relative air mass for an elevation in degrees; held near its horizon limit below 0°.
export const AirMass = (Elevation) => {
  const Zenith = Math.min(90.5, 90 - Elevation);
  return 1 / (Math.cos(Zenith * Rad) + 0.50572 * Math.pow(96.07995 - Zenith, -1.6364));
};
export const Direction = (Elevation, Azimuth) => [
  Math.sin(Azimuth * Rad) * Math.cos(Elevation * Rad),
  Math.sin(Elevation * Rad),
  Math.cos(Azimuth * Rad) * Math.cos(Elevation * Rad),
];

// Optical depths at the zenith per channel (red, green, blue) from the authored settings.
export function SkyMedium(V) {
  const Read = (Name, Fallback) => V(Name) ?? Fallback;
  const Molecules = Read("Rayleigh", 1) * (Read("Rayleigh Scale H", 8000) / 8000);
  const Aerosols = Read("Mie", 1) * (Read("Mie Scale H", 1200) / 1200);
  return {
    Rayleigh: [0.041, 0.098, 0.236].map((Depth) => Depth * Molecules),
    Mie: 0.02 * Aerosols,
    Ozone: [0.024, 0.051, 0.003].map((Depth) => (Depth * Read("Ozone", 1.2)) / 1.2),
    Lean: Read("Mie Anisotropy", 0.78),
    Tint: Hex(Read("Sky Tint", "#ffffff")),
    Gain: Read("Sky Brightness", 1),
  };
}
// Linear radiance seen along a view direction with the sun at (elevation, azimuth).
export function SkyRadiance(Medium, View, ViewElevation, Sun, SunElevation) {
  const Cos = View[0] * Sun[0] + View[1] * Sun[1] + View[2] * Sun[2];
  const PhaseR = 0.0596831 * (1 + Cos * Cos);
  const G = Medium.Lean;
  const PhaseM = (1 - G * G) / (12.566371 * Math.pow(1 + G * G - 2 * G * Cos, 1.5));
  const ViewMass = AirMass(Math.max(0, ViewElevation));
  // Light reaching high, thin air has crossed less atmosphere than light reaching the ground.
  const SunMass = AirMass(SunElevation) * (0.35 + 0.65 * (1 - Math.sin(Math.max(0, ViewElevation) * Rad)));
  const Below = SunElevation >= 0 ? 1 : Math.exp(SunElevation * 0.55);
  return [0, 1, 2].map((Channel) => {
    const Scatter = Medium.Rayleigh[Channel] + Medium.Mie;
    const Extinct = Scatter + 0.11 * Medium.Mie + Medium.Ozone[Channel];
    const Sunlight = Math.exp(-Extinct * SunMass) * Below;
    const Single = ((Medium.Rayleigh[Channel] * PhaseR + Medium.Mie * PhaseM) / Extinct) * (1 - Math.exp(-Extinct * ViewMass)) * Sunlight;
    const Multiple = 0.002 * (1 - Math.exp(-Scatter * ViewMass)) * Math.exp(-Extinct * Math.min(SunMass, 12)) * Below;
    return (Single + Multiple) * 12 * Medium.Tint[Channel] * Medium.Gain;
  });
}
// Tone map on luminance, then scale the colour, so bright skies keep their hue instead of washing to white.
const Develop = (Rgb, Exposure = 2.2) => {
  const Luminance = 0.2126 * Rgb[0] + 0.7152 * Rgb[1] + 0.0722 * Rgb[2] + 1e-9;
  const Scale = (1 - Math.exp(-Luminance * Exposure)) / Luminance;
  return Rgb.map((C) => Math.round(255 * Math.pow(Math.min(1, C * Scale), 1 / 2.2)));
};
const Css = (Rgb) => "rgb(" + Rgb.join(",") + ")";

// Fisheye image as a data URL (an <img>, so the AtmosphereLab dialog keeps a single <canvas>).
function DomeImage(Medium, SunElevation, SunAzimuth, Size) {
  const Canvas = document.createElement("canvas");
  Canvas.width = Canvas.height = Size;
  const Context2D = Canvas.getContext("2d");
  if (!Context2D) return "";
  const Pixels = Context2D.createImageData(Size, Size);
  const Sun = Direction(SunElevation, SunAzimuth);
  for (let Y = 0; Y < Size; Y++)
    for (let X = 0; X < Size; X++) {
      const U = ((X + 0.5) / Size) * 2 - 1, W = ((Y + 0.5) / Size) * 2 - 1;
      const Reach = Math.hypot(U, W);
      const At = (Y * Size + X) * 4;
      if (Reach > 1.02) continue;
      const Elevation = 90 - Math.min(1, Reach) * 90;
      const Azimuth = Math.atan2(-U, -W) / Rad;
      const Rgb = Develop(SkyRadiance(Medium, Direction(Elevation, Azimuth), Elevation, Sun, SunElevation));
      Pixels.data[At] = Rgb[0];
      Pixels.data[At + 1] = Rgb[1];
      Pixels.data[At + 2] = Rgb[2];
      Pixels.data[At + 3] = Reach > 1 ? Math.round(255 * (1.02 - Reach) * 50) : 255;
    }
  Context2D.putImageData(Pixels, 0, 0);
  return Canvas.toDataURL();
}

export default function SkyDome({ V, Change }) {
  const Medium = SkyMedium(V);
  const SunElevation = V("Sky preview elevation") ?? 32;
  const SunAzimuth = V("Sky preview azimuth") ?? 285;
  const Key = JSON.stringify([Medium, SunElevation, SunAzimuth]);
  const Image = useMemo(() => DomeImage(Medium, SunElevation, SunAzimuth, 120), [Key]);
  const Clip = "sky-dome-" + useId().replace(/:/g, "");
  const Cx = 78, Cy = 80, R = 62;
  const Place = (Elevation, Azimuth) => {
    const Reach = (R * (90 - Elevation)) / 90;
    return [Cx - Reach * Math.sin(Azimuth * Rad), Cy - Reach * Math.cos(Azimuth * Rad)];
  };
  const Sun = Direction(SunElevation, SunAzimuth);
  const Sample = (Elevation, Azimuth) => Css(Develop(SkyRadiance(Medium, Direction(Elevation, Azimuth), Elevation, Sun, SunElevation)));
  const Readings = [
    ["Overhead", Sample(90, 0)],
    ["Horizon · sun side", Sample(3, SunAzimuth)],
    ["Horizon · far side", Sample(3, SunAzimuth + 180)],
    [
      "Sunlight",
      Css(
        Develop(
          [0, 1, 2].map(
            (Channel) =>
              0.9 *
              Math.exp(-(Medium.Rayleigh[Channel] + 1.11 * Medium.Mie + Medium.Ozone[Channel]) * AirMass(SunElevation)) *
              (SunElevation >= 0 ? 1 : Math.exp(SunElevation * 0.55)),
          ),
          1.4,
        ),
      ),
    ],
  ];
  // Sky at every sun height: each column runs from the overhead colour (top) to the sun-side horizon (bottom).
  const Heights = Array.from({ length: 52 }, (_, Step) => -12 + Step * 2);
  const StripX = (Elevation) => 8 + ((Elevation + 12) / 102) * 284;
  const Columns = Heights.map((Elevation) => {
    const SunHere = Direction(Elevation, SunAzimuth);
    return [84, 50, 20, 4].map((Look) => Css(Develop(SkyRadiance(Medium, Direction(Look, SunAzimuth + 40), Look, SunHere, Elevation))));
  });
  const SunPoint = Place(Math.max(0, SunElevation), SunAzimuth);
  const SetSun = (Elevation, Azimuth) => {
    Change("Sky preview elevation", Math.round(Math.max(-12, Math.min(90, Elevation)) * 10) / 10);
    if (Azimuth != null) Change("Sky preview azimuth", Math.round((((Azimuth % 360) + 360) % 360) * 10) / 10);
  };
  const Pointer = (Event) => {
    const Svg = Event.currentTarget.ownerSVGElement || Event.currentTarget;
    const Point = Svg.createSVGPoint();
    Point.x = Event.clientX;
    Point.y = Event.clientY;
    return Point.matrixTransform(Svg.getScreenCTM().inverse());
  };
  const DomeDrag = (Event) => {
    const P = Pointer(Event), Dx = P.x - Cx, Dy = P.y - Cy;
    const Reach = Math.min(1, Math.hypot(Dx, Dy) / R);
    SetSun(90 - Reach * 90, Math.atan2(-Dx, -Dy) / Rad);
  };
  const StripDrag = (Event) => SetSun(-12 + ((Pointer(Event).x - 8) / 284) * 102);
  const Drag = (Apply) => ({
    onPointerDown: (Event) => {
      Event.currentTarget.setPointerCapture(Event.pointerId);
      Apply(Event);
    },
    onPointerMove: (Event) => Event.buttons & 1 && Apply(Event),
  });
  const Compass = [
    ["N", 0],
    ["E", 90],
    ["S", 180],
    ["W", 270],
  ];
  return (
    <section className="property-card atmosphere-presets" data-card="Sky colour">
      <h3>Sky colour</h3>
      <p>
        Looking up · sun {SunElevation.toFixed(0)}° high, bearing {SunAzimuth.toFixed(0)}°
      </p>
      <svg className="sky-dome" viewBox="0 0 300 230" aria-label="All-sky view of the current atmosphere" data-sky-dome="">
        <defs>
          <clipPath id={Clip}>
            <circle cx={Cx} cy={Cy} r={R} />
          </clipPath>
        </defs>
        <g
          role="slider"
          tabIndex={0}
          aria-label="Preview sun position"
          aria-valuemin={-12}
          aria-valuemax={90}
          aria-valuenow={SunElevation}
          onKeyDown={(Event) => {
            const Moves = { ArrowUp: [2, 0], ArrowDown: [-2, 0], ArrowLeft: [0, -5], ArrowRight: [0, 5] };
            if (!(Event.key in Moves)) return;
            Event.preventDefault();
            SetSun(SunElevation + Moves[Event.key][0], SunAzimuth + Moves[Event.key][1]);
          }}
          {...Drag(DomeDrag)}
          className="sky-dome-grab"
        >
          <circle cx={Cx} cy={Cy} r={R + 1} fill="#050608" />
          {Image && <image href={Image} x={Cx - R - 1.3} y={Cy - R - 1.3} width={2 * R + 2.6} height={2 * R + 2.6} clipPath={`url(#${Clip})`} preserveAspectRatio="none" data-sky-image="" />}
          {[30, 60].map((Ring) => (
            <circle key={Ring} cx={Cx} cy={Cy} r={(R * (90 - Ring)) / 90} fill="none" stroke="#ffffff" strokeOpacity=".14" strokeDasharray="1.5 3" />
          ))}
          <circle cx={Cx} cy={Cy} r={R} fill="none" stroke="#ffffff" strokeOpacity=".22" />
          {SunElevation > -1 && (
            <g data-sky-sun="">
              <circle cx={SunPoint[0]} cy={SunPoint[1]} r="9" fill="#fff6dc" fillOpacity=".35" />
              <circle cx={SunPoint[0]} cy={SunPoint[1]} r="3.6" fill="#fffdf6" />
            </g>
          )}
          {Compass.map(([Name, Azimuth]) => {
            const [X, Y] = Place(-11, Azimuth);
            return (
              <text key={Name} x={X} y={Y + 3} textAnchor="middle" className={Name === "N" ? "sky-label" : ""}>
                {Name}
              </text>
            );
          })}
        </g>
        {Readings.map(([Label, Colour], Index) => (
          <g key={Label} transform={`translate(164 ${10 + Index * 32})`} data-sky-reading={Label}>
            <rect width="22" height="22" rx="6" fill={Colour} stroke="#ffffff" strokeOpacity=".12" />
            <text x="30" y="9" className="sky-reading-label">{Label}</text>
            <text x="30" y="20" className="sky-reading-rgb">{Colour.replace("rgb(", "").replace(")", "")}</text>
          </g>
        ))}
        <g {...Drag(StripDrag)} className="sky-dome-grab" data-sky-strip="">
          <rect x="0" y="160" width="300" height="70" fill="transparent" />
          {Columns.map((Stops, Index) => (
            <g key={Index}>
              {Stops.map((Colour, Row) => (
                <rect key={Row} x={StripX(Heights[Index]) - 0.2} y={168 + Row * 8} width={284 / 51 + 0.6} height="8.4" fill={Colour} />
              ))}
            </g>
          ))}
          <rect x={StripX(SunElevation) - 1.5} y="163" width="3" height="42" rx="1.5" fill="#f4f4f4" />
          {[
            [-12, "−12°", "start"],
            [0, "sunset"],
            [30, "30°"],
            [60, "60°"],
            [90, "overhead", "end"],
          ].map(([Elevation, Label, Anchor]) => (
            <text key={Label} x={StripX(Elevation)} y="216" textAnchor={Anchor || "middle"}>
              {Label}
            </text>
          ))}
          <text x="0" y="228" className="sky-faint">Sky at every sun height · top overhead, bottom horizon · drag to set</text>
        </g>
      </svg>
      <p className="graph-note">Single-scatter estimate from the settings below (Kasten-Young air mass, Rayleigh + Henyey-Greenstein phase). Not the native sky.</p>
    </section>
  );
}
