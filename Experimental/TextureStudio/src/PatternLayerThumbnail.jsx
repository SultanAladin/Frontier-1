/*==========================================================================================================================================
                                                        PATTERNLAYERTHUMBNAIL.JSX
==========================================================================================================================================*/
// 🧩 Small per-layer preview swatch for the pattern layer stack, matching the Texture workspace's
// tp-layer-thumbnail box (same classNames/sizing) so both stacks read identically at a glance.
// Texture thumbnails show a rendered image; a pattern layer has no baked raster, so this renders
// the layer's own vector contour locally (sized to just that shape's own bounds, not the full tile).

import React from "react";
import { Layers3 } from "lucide-react";
import { patternSizedPath } from "./patternGeometry.js";

export default function PatternLayerThumbnail({ layer, size = 34, FallbackIcon = Layers3 }) {
  if (layer.kind === "image" && layer.src)
    return (
      <span className="tp-layer-thumbnail" style={{ width: size, height: size }}>
        <img src={layer.src} alt={`${layer.name} preview`} />
      </span>
    );
  let d = null;
  try {
    d = patternSizedPath(layer);
  } catch {
    d = null;
  }
  if (!d)
    return (
      <span className="tp-kind-icon" style={{ width: size, height: size }}>
        <FallbackIcon size={size * 0.5} strokeWidth={1.5} />
      </span>
    );
  const width = Math.max(1, layer.width || 160),
    height = Math.max(1, layer.height || 160);
  return (
    <span className="tp-layer-thumbnail" style={{ width: size, height: size }}>
      <svg
        viewBox={`${-width / 2} ${-height / 2} ${width} ${height}`}
        width={size}
        height={size}
        preserveAspectRatio="xMidYMid meet"
        aria-hidden="true"
      >
        <path
          d={d}
          fill={layer.color || "none"}
          stroke={layer.strokeWidth ? layer.stroke || "#222222" : "none"}
          strokeWidth={layer.strokeWidth || 0}
          transform={layer.rotation ? `rotate(${layer.rotation})` : undefined}
        />
      </svg>
    </span>
  );
}
