import React, { useId, useRef } from "react";

export function SensorField({
  focal,
  aperture,
  focus,
  optics,
  sensor = 36,
  aspect = 1.5,
}) {
  const id = useId().replace(/:/g, ""),
    extent = 34 + optics.horizontalFov * 0.85,
    gap = 8 + Math.min(38, optics.pupil * 0.75);
  return (
    <div className="sensor-field">
      <svg
        viewBox="0 0 640 290"
        role="img"
        aria-label={`Sensor ${sensor} mm, ${optics.horizontalFov.toFixed(1)} degree horizontal field of view, ${optics.pupil.toFixed(1)} millimeter entrance pupil, focused at ${focus} meters`}
      >
        <defs>
          <linearGradient id={id}>
            <stop stopColor="#b59acc" stopOpacity=".02" />
            <stop offset="1" stopColor="#b59acc" stopOpacity=".1" />
          </linearGradient>
        </defs>
        <path d="M30 141 H610" stroke="#b3a5bf25" strokeDasharray="3 6" />
        <path
          className="optical-envelope"
          d={`M235 141 L553 ${141 - extent} V${141 + extent} Z`}
          fill={`url(#${id})`}
          stroke="#ae98c7"
          strokeWidth="1"
        />
        <path
          d={`M78 107 L235 141 L553 ${141 + extent} M78 175 L235 141 L553 ${141 - extent}`}
          fill="none"
          stroke="#b79dd0"
          strokeOpacity=".36"
          strokeWidth=".8"
        />
        <rect
          x="68"
          y="100"
          width="12"
          height="82"
          rx="3"
          fill="#a4b6c51f"
          stroke="#b9c7d6"
          strokeWidth="1.2"
        />
        {[0, 1, 2, 3, 4, 5].map((i) => (
          <path
            key={i}
            d={`M71 ${108 + i * 13} H77`}
            stroke="#d0dae2"
            strokeWidth=".6"
          />
        ))}
        <path
          d="M64 94 H57 V187 H64"
          stroke="#6d7b8a"
          fill="none"
          strokeWidth=".6"
        />
        <path
          className="pupil-gate"
          d={`M235 72 V${141 - gap} M235 ${141 + gap} V210`}
          stroke="#b9a5cd"
          strokeWidth="7"
          strokeLinecap="round"
        />
        <path
          d={`M225 ${141 - gap} H245 M225 ${141 + gap} H245`}
          stroke="#e2d0f4"
          strokeWidth="1.2"
        />
        <path
          d={`M553 ${141 - extent} V${141 + extent}`}
          stroke="#d6c6e8"
          strokeWidth="2"
        />
        <path
          d={`M545 ${141 - extent} H562 M545 ${141 + extent} H562`}
          stroke="#d6c6e8"
          strokeWidth="1"
        />
        <path
          d="M320 111 Q328 141 320 171"
          stroke="#a68abb"
          fill="none"
          strokeWidth=".7"
        />
        <text x="342" y="134" fill="#ddcbea" fontSize="21" fontWeight="300">
          {optics.horizontalFov.toFixed(1)}°
        </text>
        <text x="343" y="152" fill="#8f829d" fontSize="8" letterSpacing="1">
          HORIZONTAL FOV
        </text>
        <g textAnchor="middle">
          <text x="75" y="236" fill="#afbbc8" fontSize="10">
            Sensor
          </text>
          <text x="75" y="254" fill="#737b85" fontSize="9">
            {sensor.toFixed(1)} × {(sensor / aspect).toFixed(1)} mm
          </text>
          <text x="235" y="236" fill="#c5afd9" fontSize="10">
            ƒ/{aperture.toFixed(1)}
          </text>
          <text x="235" y="254" fill="#8a7c97" fontSize="9">
            Ø {optics.pupil.toFixed(1)} mm pupil
          </text>
          <text x="553" y="270" fill="#b5a5c4" fontSize="9">
            Focus plane · {focus} m
          </text>
        </g>
        <path d="M84 211 H225" stroke="#5d5267" strokeWidth=".6" />
        <text x="154" y="206" fill="#8f819b" fontSize="9" textAnchor="middle">
          {focal} mm
        </text>
      </svg>
      <div className="sensor-field-caption">
        <span>Optical schematic · not to scale</span>
        <span>
          {sensor.toFixed(1)} mm / {aspect.toFixed(3)}
        </span>
      </div>
    </div>
  );
}
