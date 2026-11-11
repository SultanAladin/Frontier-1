import React from "react";
import { FogDensity } from "./LiveGraph.jsx";
import "./FogPanel.css";

// One Medium-owned chamber shared by Height, Atmospheric and Local Fog.
export function FogBeamChamber({ Kind, V }) {
  const Local = Kind === "local-fog",
    Height = Kind === "height-fog",
    Enabled = !!V("Enabled"),
    ProbeHeight = Height ? 25 : 2,
    Extinction = Math.max(0, FogDensity(Kind, V, ProbeHeight, true)),
    Start = Local || Height ? 0 : Math.max(0, Number(V("Start")) || 0),
    SpreadName = Local ? "Anisotropy" : Height ? "Sun Scatter" : "Mie Blend",
    Spread = Math.max(0, Number(V(SpreadName)) || 0),
    DistanceMax = Height ? 120 : 400,
    Tint = Height ? V("Colour") || "#d4e3ec" : "#d4e3ec",
    Transmission = (Distance) => Math.exp(-Extinction * Math.max(0, Distance)),
    Range = Extinction > 0 ? -Math.log(0.02) / Extinction : Infinity,
    Marker = 12 + Math.max(0, Math.min(1, Range / DistanceMax)) * 276,
    Preview = Enabled ? 1 : 0.82,
    Percent = (Value) => `${(Value * 100).toFixed(0)}%`;
  return (
    <section className="fog-shared-beam" data-enabled={Enabled}>
      <header>
        <span>Beam chamber</span>
        <strong>{Enabled ? "LIVE" : "PREVIEW"}</strong>
      </header>
      <small>
        {Local
          ? "Interior 2 m layer"
          : Height
            ? "Height medium · 25 m layer"
            : `Distance haze · start ${Start} m`}
      </small>
      <svg viewBox="0 0 300 102" role="img" aria-label="Fog beam chamber">
        {[26, 51, 76].map((Y) => (
          <path
            key={Y}
            d={`M12 ${Y}H288`}
            stroke="#ffffff10"
            strokeDasharray="2 5"
          />
        ))}
        {Array.from({ length: 70 }, (_, Index) => {
          const Fraction = Index / 69,
            Distance = Fraction * DistanceMax,
            Physical = Transmission(Distance),
            Exposure = 0.32 + 0.68 * Math.pow(Physical, 0.15),
            Width = 3 + Fraction * Fraction * (11 + Spread * 24);
          return (
            <rect
              key={Index}
              x={12 + Fraction * 272}
              y={51 - Width}
              width="4.2"
              height={Width * 2}
              fill={Tint}
              opacity={Preview * (0.1 + Spread * 0.2) * Exposure}
            />
          );
        })}
        <path
          d={`M12 51L288 ${40 - Spread * 10}M12 51L288 ${62 + Spread * 10}`}
          stroke={Tint}
          strokeOpacity={Preview * 0.42}
        />
        <path
          d="M12 51H288"
          stroke={Tint}
          strokeOpacity={Preview * 0.82}
          strokeWidth="1.6"
        />
        {Number.isFinite(Range) && (
          <path
            d={`M${Marker} 18V84`}
            stroke="#ed8f8f"
            strokeOpacity=".78"
            strokeDasharray="3 3"
          />
        )}
        <circle cx="12" cy="51" r="4" fill="#fff1ca" opacity={Preview} />
        <text x="10" y="13">
          {SpreadName.toUpperCase()} · {Spread.toFixed(2)}
        </text>
        <text x="290" y="13" textAnchor="end">
          2% ·{" "}
          {Number.isFinite(Range)
            ? Range >= 1000
              ? `${(Range / 1000).toFixed(1)} km`
              : `${Range.toFixed(0)} m`
            : "CLEAR"}
        </text>
        <text x="290" y="96" textAnchor="end">
          {Percent(Transmission(DistanceMax))} PHYSICAL AT {DistanceMax} m
        </text>
      </svg>
    </section>
  );
}
