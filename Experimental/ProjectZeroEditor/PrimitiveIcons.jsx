import React from "react";

// 🧩 Duotone glyphs for the primitive inspector: quick-control symbols drawn white on a vibrant seat, and the
//    category carousel's smaller line symbols. 24 × 24, round joins; the translucent fill gives the second tone.

const Strokes = {
  fill: "none",
  stroke: "currentColor",
  strokeWidth: 1.8,
  strokeLinecap: "round",
  strokeLinejoin: "round",
};

const Drawings = {
  visible: (
    <>
      <path d="M2.5 12S6 5.5 12 5.5 21.5 12 21.5 12 18 18.5 12 18.5 2.5 12 2.5 12Z" {...Strokes} />
      <circle cx="12" cy="12" r="3.4" fill="currentColor" />
      <circle cx="13.3" cy="10.8" r="1" fill="#000" opacity=".35" />
    </>
  ),
  dynamic: (
    <>
      <circle cx="15" cy="12" r="5.2" fill="currentColor" />
      <path d="M2.5 8.5h6M4 12h4.5M2.5 15.5h6" {...Strokes} />
      <path d="M13 9.6a3 3 0 0 1 3.6-.4" {...Strokes} stroke="#000" strokeOpacity=".3" />
    </>
  ),
  shadow: (
    <>
      <path d="M13.5 15.5 21 15.5 17.5 21 10 21Z" fill="currentColor" opacity=".45" />
      <path d="M8.5 3 14 6v6.5l-5.5 3L3 12.5V6Z" fill="currentColor" opacity=".28" />
      <path d="M8.5 3 14 6v6.5l-5.5 3L3 12.5V6ZM3 6l5.5 3L14 6M8.5 9v6.5" {...Strokes} />
    </>
  ),
  gi: (
    <>
      <circle cx="6" cy="6" r="2.6" fill="currentColor" />
      <path d="M6 1.5v1M1.5 6h1M2.8 2.8l.7.7M9.2 2.8l-.7.7" {...Strokes} strokeWidth="1.5" />
      <path d="M8.5 9 12 19l4.5-8" {...Strokes} />
      <path d="m14.6 11.4 2-.5.4 2" {...Strokes} />
      <path d="M3 19.5h18" {...Strokes} opacity=".55" />
      <circle cx="19" cy="7" r="1.6" fill="currentColor" opacity=".6" />
    </>
  ),
  physics: (
    <>
      <rect x="7" y="2.5" width="10" height="10" rx="2.4" fill="currentColor" />
      <path d="M12 15v5.5M9 17.8l3 2.9 3-2.9" {...Strokes} />
      <path d="M3.5 5v4M20.5 5v4" {...Strokes} opacity=".55" />
    </>
  ),
  lock: (
    <>
      <path d="M8 11V8a4 4 0 0 1 8 0v3" {...Strokes} />
      <rect x="5" y="10.5" width="14" height="10.5" rx="2.6" fill="currentColor" />
      <path d="M12 14.5v2.6" stroke="#000" strokeOpacity=".4" strokeWidth="2" strokeLinecap="round" />
    </>
  ),

  // Carousel categories (line symbols).
  overview: (
    <>
      <circle cx="12" cy="12" r="9" {...Strokes} />
      <path d="M12 11v5.5" {...Strokes} />
      <circle cx="12" cy="7.6" r="1.2" fill="currentColor" />
    </>
  ),
  "physics-tab": (
    <>
      <circle cx="12" cy="12" r="2" fill="currentColor" />
      <ellipse cx="12" cy="12" rx="9.5" ry="3.8" {...Strokes} />
      <ellipse cx="12" cy="12" rx="9.5" ry="3.8" {...Strokes} transform="rotate(60 12 12)" />
      <ellipse cx="12" cy="12" rx="9.5" ry="3.8" {...Strokes} transform="rotate(-60 12 12)" />
    </>
  ),
  geometry: (
    <>
      <path d="M12 2.5 20.5 7v10L12 21.5 3.5 17V7Z" {...Strokes} />
      <path d="M3.5 7 12 11.5 20.5 7M12 11.5v10" {...Strokes} />
    </>
  ),
  material: (
    <>
      <circle cx="12" cy="12" r="9" {...Strokes} />
      <path d="M12 3a9 9 0 0 1 0 18Z" fill="currentColor" opacity=".45" />
      <path d="M7.5 8.5a5 5 0 0 1 2.5-2" {...Strokes} />
    </>
  ),
  rendering: (
    <>
      <circle cx="12" cy="12" r="9" {...Strokes} />
      <path d="M12 3 14.5 10M21 12l-7 2.5M12 21l-2.5-7M3 12l7-2.5" {...Strokes} />
    </>
  ),
  fracture: (
    <>
      <path d="M4 5.5 11 3l2 5-3 3 4 3-2 7-8-3Z" {...Strokes} />
      <path d="m15.5 3.5 5 2.5-1 6-4-1.5 1.5-3Z" {...Strokes} />
      <path d="m16 14 4.5 1.5-1.5 5-3.5-1Z" {...Strokes} />
    </>
  ),

  // Card symbols.
  purpose: (
    <>
      <circle cx="12" cy="12" r="9" {...Strokes} />
      <circle cx="12" cy="12" r="5" {...Strokes} />
      <circle cx="12" cy="12" r="1.6" fill="currentColor" />
    </>
  ),
  check: <path d="m5 12.5 4.5 4.5L19 7.5" {...Strokes} strokeWidth="2.4" />,
  warning: (
    <>
      <path d="M12 7v6" {...Strokes} strokeWidth="2.4" />
      <circle cx="12" cy="17" r="1.4" fill="currentColor" />
    </>
  ),
  tag: (
    <>
      <path d="M3 12V4.5A1.5 1.5 0 0 1 4.5 3H12l9 9-9 9Z" {...Strokes} />
      <circle cx="7.8" cy="7.8" r="1.5" fill="currentColor" />
    </>
  ),
  weight: (
    <>
      <path d="M6.5 9h11l2.5 12H4Z" {...Strokes} />
      <circle cx="12" cy="5.5" r="2.5" {...Strokes} />
    </>
  ),
  pivot: (
    <>
      <circle cx="12" cy="12" r="2.4" fill="currentColor" />
      <path d="M12 2.5v5M12 16.5v5M2.5 12h5M16.5 12h5" {...Strokes} />
    </>
  ),
  ruler: (
    <>
      <rect x="2.5" y="8" width="19" height="8" rx="2" {...Strokes} />
      <path d="M6.5 8v3M10 8v4M13.5 8v3M17 8v4" {...Strokes} />
    </>
  ),
  notes: (
    <>
      <path d="M5 3.5h10l4 4v13H5Z" {...Strokes} />
      <path d="M8.5 11h7M8.5 14.5h7M8.5 18h4" {...Strokes} />
    </>
  ),
  chevron: <path d="m9 5 7 7-7 7" {...Strokes} strokeWidth="2" />,
  uv: (
    <>
      <rect x="4" y="4" width="16" height="16" rx="3.5" {...Strokes} />
      <path d="M7.5 8.5h4v4h-4Z" fill="currentColor" opacity=".45" />
      <path d="M13.5 12.5l3 3.5M13 16.5h4" {...Strokes} />
    </>
  ),
  sdf: (
    <>
      <circle cx="12" cy="12" r="3.2" fill="currentColor" opacity=".45" />
      <circle cx="12" cy="12" r="6.2" {...Strokes} />
      <circle cx="12" cy="12" r="9.2" {...Strokes} strokeOpacity=".4" />
    </>
  ),
  lod: (
    <>
      <rect x="9" y="3.5" width="6" height="4.5" rx="1.2" fill="currentColor" opacity=".45" />
      <rect x="4" y="11" width="6" height="4" rx="1.1" {...Strokes} />
      <rect x="14" y="11" width="6" height="4" rx="1.1" {...Strokes} />
      <path d="M12 8v1.6M7 11V9.6h10V11M4 19h16" {...Strokes} strokeOpacity=".6" />
    </>
  ),
  components: (
    <>
      <path d="M12 3.5 20.5 8 12 12.5 3.5 8Z" fill="currentColor" opacity=".45" />
      <path d="M12 3.5 20.5 8 12 12.5 3.5 8Z" {...Strokes} />
      <path d="m3.5 12 8.5 4.5 8.5-4.5M3.5 16l8.5 4.5 8.5-4.5" {...Strokes} />
    </>
  ),
  plus: <path d="M12 5v14M5 12h14" {...Strokes} strokeWidth="2" />,
  close: <path d="M7 7l10 10M17 7 7 17" {...Strokes} strokeWidth="2" />,
};

export default function PrimitiveGlyph({ Name, Size = 20, Tone }) {
  return (
    <svg
      className="primitive-glyph"
      width={Size}
      height={Size}
      viewBox="0 0 24 24"
      aria-hidden="true"
      style={Tone ? { color: Tone } : undefined}
    >
      {Drawings[Name] || Drawings.overview}
    </svg>
  );
}
