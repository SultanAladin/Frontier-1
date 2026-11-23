import React from "react";
import { patternNodesPath } from "./patternGeometry.js";
import { pathNodeMode, pathSegment } from "./pathEditorGeometry.js";

export default function PathEditorOverlay({
  nodes,
  closed,
  pending,
  selected,
  units,
  showHandles,
  tool,
  hover,
  activeSegment,
}) {
  const selectedSet = new Set(selected),
    handleSize = units * 7;
  return (
    <g data-path-origin={pending ? "draft" : "document"}>
      {nodes.slice(0, closed ? nodes.length : -1).map((_, i) => {
        const s = pathSegment(nodes, i, closed);
        if (!s) return null;
        const d = patternNodesPath([s.a, s.b]);
        return (
          <g key={`segment-${i}`}>
            {activeSegment?.index === i && (
              <path
                d={d}
                fill="none"
                stroke="#cfe5bb"
                strokeWidth="2"
                vectorEffect="non-scaling-stroke"
                pointerEvents="none"
              />
            )}
            <path
              d={d}
              data-segment={i}
              aria-label={`Path segment ${i + 1}`}
              fill="none"
              stroke="transparent"
              strokeWidth="14"
              vectorEffect="non-scaling-stroke"
              style={{ cursor: tool === "node" ? "grab" : "copy" }}
            />
          </g>
        );
      })}
      {hover && (
        <circle
          cx={hover.point.x}
          cy={hover.point.y}
          r={units * 4}
          fill="#e0efca"
          stroke="#334437"
          strokeWidth="1"
          vectorEffect="non-scaling-stroke"
          pointerEvents="none"
        />
      )}
      {nodes.map((n, i) => {
        const active = selectedSet.has(i),
          mode = pathNodeMode(n),
          endpoint = !closed && (i === 0 || i === nodes.length - 1),
          label = pending ? `Draft point ${i + 1}` : `Path point ${i + 1}`;
        return (
          <g key={`anchor-${i}`}>
            {showHandles &&
              (active ||
                (pending && i === nodes.length - 1) ||
                selectedSet.has(i - 1) ||
                selectedSet.has(i + 1)) &&
              ["in", "out"]
                .filter((side) => n[side])
                .map((side) => {
                  const q = n[side];
                  return (
                    <g key={side}>
                      <path
                        d={`M${n.x} ${n.y}L${q.x} ${q.y}`}
                        stroke="#b9d6a8"
                        strokeWidth="1"
                        vectorEffect="non-scaling-stroke"
                        pointerEvents="none"
                      />
                      <circle
                        data-node={i}
                        data-control={side}
                        aria-label={
                          pending
                            ? `Draft point ${i + 1} ${side} handle`
                            : `Point ${i + 1} ${side} handle`
                        }
                        cx={q.x}
                        cy={q.y}
                        r={units * 4}
                        fill="#222f28"
                        stroke="#d4e9bd"
                        strokeWidth="1.2"
                        vectorEffect="non-scaling-stroke"
                        style={{ cursor: "move" }}
                      />
                    </g>
                  );
                })}
            {endpoint && (
              <circle
                cx={n.x}
                cy={n.y}
                r={handleSize * 1.25}
                fill="none"
                stroke="#c7ddaf"
                strokeDasharray={pending && i === 0 ? "2 2" : undefined}
                strokeOpacity=".45"
                strokeWidth="1"
                vectorEffect="non-scaling-stroke"
                pointerEvents="none"
              />
            )}
            <circle
              data-node={i}
              data-control="anchor"
              cx={n.x}
              cy={n.y}
              r={units * 9}
              fill="transparent"
              style={{ cursor: "move" }}
            />
            {mode === "auto" ? (
              <circle
                data-node={i}
                data-control="anchor"
                data-point-mode={mode}
                aria-label={label}
                cx={n.x}
                cy={n.y}
                r={handleSize * 0.58}
                fill={active ? "#edffd6" : "#25372c"}
                stroke="#d8ebc3"
                strokeWidth="1.3"
                vectorEffect="non-scaling-stroke"
                style={{ cursor: "move" }}
              />
            ) : (
              <rect
                data-node={i}
                data-control="anchor"
                data-point-mode={mode}
                aria-label={label}
                x={n.x - handleSize / 2}
                y={n.y - handleSize / 2}
                width={handleSize}
                height={handleSize}
                rx={mode === "corner" ? 0 : units * 2}
                fill={active ? "#edffd6" : "#25372c"}
                stroke="#d8ebc3"
                strokeWidth="1.3"
                vectorEffect="non-scaling-stroke"
                style={{ cursor: "move" }}
              />
            )}
          </g>
        );
      })}
    </g>
  );
}
