import React, { useId } from "react";

const Clamp = (Value, Minimum, Maximum) =>
  Math.max(Minimum, Math.min(Maximum, Value));

export default function HeightFogVisual({ V, Change }) {
  const Id = useId().replaceAll(":", ""),
    Enabled = !!V("Enabled"),
    Density = Math.max(0, Number(V("Density")) || 0),
    Falloff = Math.max(10, Number(V("Falloff Height")) || 400),
    Colour = V("Colour") || "#ffffff",
    At = (Altitude) => Density * Math.exp(-Altitude / Falloff),
    Left = 34,
    Right = 286,
    Top = 20,
    Bottom = 174,
    X = (Altitude) => Left + (Altitude / 3000) * (Right - Left),
    Y = (Value) => Bottom - (Value / 0.2) * (Bottom - Top),
    Profile = Array.from({ length: 61 }, (_, Index) => {
      const Altitude = Index * 50;
      return [X(Altitude), Y(At(Altitude))];
    }),
    Path = Profile.map(([Px, Py], Index) =>
      `${Index ? "L" : "M"}${Px.toFixed(2)} ${Py.toFixed(2)}`,
    ).join(" "),
    FillPath = `${Path} L${Right} ${Bottom} L${Left} ${Bottom} Z`,
    PointX = X(Falloff),
    PointY = Y(At(Falloff));

  const Edit = (Event) => {
    const Bounds = Event.currentTarget.getBoundingClientRect(),
      Px = ((Event.clientX - Bounds.left) / Bounds.width) * 300,
      Py = ((Event.clientY - Bounds.top) / Bounds.height) * 210,
      NextFalloff = Math.round(
        10 + (Clamp(Px, Left, Right) - Left) / (Right - Left) * 2990,
      ),
      NextDensity = +(
        ((Bottom - Clamp(Py, Top, Bottom)) / (Bottom - Top)) *
        0.2
      ).toFixed(4);
    Change("Falloff Height", NextFalloff);
    Change("Density", NextDensity);
  };

  return (
    <div className="height-density-visual" data-enabled={Enabled}>
      <div className="height-density-status">
        <span>
          <i /> {Enabled ? "Live density profile" : "Authored preview"}
        </span>
        <strong>{Falloff.toLocaleString("en-US")} m falloff</strong>
      </div>
      <div className="height-density-metric">
        <strong>{Density.toFixed(4)}</strong>
        <span>m⁻¹ datum density</span>
      </div>
      <svg
        viewBox="0 0 300 210"
        role="img"
        aria-label="Interactive Height Fog density profile"
        preserveAspectRatio="none"
        tabIndex="0"
        onPointerDown={(Event) => {
          Event.currentTarget.setPointerCapture(Event.pointerId);
          Edit(Event);
        }}
        onPointerMove={(Event) => {
          if (Event.currentTarget.hasPointerCapture(Event.pointerId)) Edit(Event);
        }}
        onPointerUp={(Event) =>
          Event.currentTarget.releasePointerCapture(Event.pointerId)
        }
      >
        <defs>
          <linearGradient id={Id + "area"} x2="0" y2="1">
            <stop stopColor={Colour} stopOpacity={Enabled ? ".42" : ".28"} />
            <stop offset="1" stopColor={Colour} stopOpacity=".025" />
          </linearGradient>
        </defs>
        <rect width="300" height="210" rx="12" fill="#191919" />
        {[0, 0.05, 0.1, 0.15, 0.2].map((Value) => (
          <g key={Value}>
            <path
              d={`M${Left} ${Y(Value)}H${Right}`}
              stroke="#ffffff12"
              strokeDasharray={Value ? "2 5" : undefined}
            />
            <text x={Left - 6} y={Y(Value) + 3} textAnchor="end">
              {Value.toFixed(Value ? 2 : 0)}
            </text>
          </g>
        ))}
        {[0, 1000, 2000, 3000].map((Altitude) => (
          <g key={Altitude}>
            <path
              d={`M${X(Altitude)} ${Top}V${Bottom}`}
              stroke="#ffffff0b"
            />
            <text x={X(Altitude)} y="193" textAnchor={Altitude === 0 ? "start" : Altitude === 3000 ? "end" : "middle"}>
              {Altitude ? `${Altitude / 1000} km` : "0 m"}
            </text>
          </g>
        ))}
        <path d={FillPath} fill={`url(#${Id}area)`} />
        <path d={Path} fill="none" stroke={Colour} strokeWidth="1.7" />
        <path
          d={`M${PointX} ${Top}V${Bottom}`}
          stroke="#ffffff6b"
          strokeDasharray="4 4"
        />
        <circle cx={PointX} cy={PointY} r="5" fill={Colour} stroke="#111315" strokeWidth="2" />
        <text x={Math.min(Right - 4, PointX + 8)} y={Math.max(14, PointY - 9)} textAnchor={PointX > 230 ? "end" : "start"}>
          37% · {Falloff.toFixed(0)} m
        </text>
        <text x={Left} y="207">DRAG POINT · DENSITY / FALLOFF</text>
      </svg>
      <div className="height-density-readings">
        <span>
          <small>At falloff height</small>
          <b>{At(Falloff).toFixed(4)} m⁻¹</b>
        </span>
        <span>
          <small>At 2× falloff</small>
          <b>{At(Falloff * 2).toFixed(4)} m⁻¹</b>
        </span>
      </div>
      <p>
        Drag the profile point to author the same Density and Falloff Height values
        used by Medium, Visibility and Beam Chamber.
      </p>
    </div>
  );
}
