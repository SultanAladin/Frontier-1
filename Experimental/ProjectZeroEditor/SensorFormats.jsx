// Camera sensor study: the authored sensor drawn to scale against common capture formats, centred like a lens sees them.
// Click a format to use its width. Crop factor and full-frame equivalent focal length are relative to 36 mm.
import React from "react";

export const Formats = [
  ["Micro Four Thirds", 17.3],
  ["APS-C", 23.6],
  ["Super 35", 24.89],
  ["Full frame", 36],
  ["Large format", 36.7],
  ["Medium format", 43.8],
  ["65 mm", 54.12],
  ["IMAX-class", 70],
];

export default function SensorFormats({ Width, Aspect, Focal, Change }) {
  const Scale = 268 / 70;
  const Cx = 150, Cy = 76;
  const Height = Width / Aspect;
  const Nearest = Formats.reduce((Best, Format) => (Math.abs(Format[1] - Width) < Math.abs(Best[1] - Width) ? Format : Best), Formats[0]);
  const Box = (W, H) => ({ x: Cx - (W * Scale) / 2, y: Cy - (H * Scale) / 2, width: W * Scale, height: H * Scale });
  // The image circle a full-frame lens must cover (43.3 mm diagonal), for reference.
  const Circle = (Math.hypot(Width, Height) / 2) * Scale;
  return (
    <svg className="sensor-formats" viewBox="0 0 300 168" role="img" aria-label="Sensor formats" data-sensor-width={Width}>
      <rect x="0" y="0" width="300" height="152" rx="12" fill="#151515" />
      <circle cx={Cx} cy={Cy} r={Circle} fill="none" stroke="#ffffff" strokeOpacity=".12" strokeDasharray="2 3" />
      {[...Formats].reverse().map(([Name, W]) => {
        const Rect = Box(W, W / Aspect);
        const Clipped = Rect.height > 146;
        return (
          <g
            key={Name}
            className="sensor-format"
            role="button"
            tabIndex={0}
            aria-label={`Use ${Name} sensor, ${W} mm`}
            onClick={() => Change(W)}
            onKeyDown={(Event) => (Event.key === "Enter" || Event.key === " ") && (Event.preventDefault(), Change(W))}
          >
            <rect {...Rect} y={Clipped ? 3 : Rect.y} height={Clipped ? 146 : Rect.height} rx="3" fill="transparent" stroke={Name === Nearest[0] ? "#9a9a9a" : "#ffffff"} strokeOpacity={Name === Nearest[0] ? 0.6 : 0.16} />
            <title>{`${Name} · ${W} mm`}</title>
          </g>
        );
      })}
      <rect {...Box(Width, Height)} rx="3" fill="#e6cf72" fillOpacity=".14" stroke="#e6cf72" strokeWidth="1.4" />
      <line x1={Cx - 4} x2={Cx + 4} y1={Cy} y2={Cy} stroke="#e6cf72" />
      <line x1={Cx} x2={Cx} y1={Cy - 4} y2={Cy + 4} stroke="#e6cf72" />
      <text x={Cx} y={Cy - (Height * Scale) / 2 + 11} textAnchor="middle" className="sensor-strong">
        {Nearest[0]}
      </text>
      <text x={Cx} y={Cy + (Height * Scale) / 2 - 5} textAnchor="middle" className="sensor-strong">
        {Width.toFixed(1)} × {Height.toFixed(1)} mm
      </text>
      <text x="0" y="165">click a frame to use that format · dashed: image circle</text>
      <text x="300" y="165" textAnchor="end">
        {(36 * Focal / Width).toFixed(0)} mm full-frame equiv.
      </text>
    </svg>
  );
}
