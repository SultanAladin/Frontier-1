import React from "react";

// 🧩 Outliner and inspector icons for the camera / post-process effects. Small filled drawings with soft gradients,
//    sized like the native outliner artwork (24 × 24). Ids in gradients are prefixed per icon so several can share a page.

const Drawings = {
  "effect-motion-blur": (Id) => (
    <>
      <defs>
        <radialGradient id={Id + "b"} cx="40%" cy="35%" r="70%">
          <stop offset="0" stopColor="#ffd28a" />
          <stop offset="1" stopColor="#f07a2e" />
        </radialGradient>
        <linearGradient id={Id + "s"} x1="0" x2="1">
          <stop offset="0" stopColor="#f07a2e" stopOpacity="0" />
          <stop offset="1" stopColor="#f5a04f" stopOpacity=".9" />
        </linearGradient>
      </defs>
      <rect x="2" y="7.2" width="12" height="2.2" rx="1.1" fill={`url(#${Id}s)`} />
      <rect x="4" y="11" width="10" height="2.2" rx="1.1" fill={`url(#${Id}s)`} />
      <rect x="1" y="14.8" width="13" height="2.2" rx="1.1" fill={`url(#${Id}s)`} />
      <circle cx="16.5" cy="12" r="5.5" fill={`url(#${Id}b)`} />
      <circle cx="15" cy="10.3" r="1.5" fill="#fff" opacity=".55" />
    </>
  ),
  "effect-heat-shimmer": (Id) => (
    <>
      <defs>
        <linearGradient id={Id + "h"} x1="0" y1="1" x2="0" y2="0">
          <stop offset="0" stopColor="#ff5a3c" />
          <stop offset="1" stopColor="#ffc35a" />
        </linearGradient>
      </defs>
      <rect x="2" y="19" width="20" height="2.4" rx="1.2" fill="#6b4b3b" />
      {[6, 12, 18].map((X) => (
        <path
          key={X}
          d={`M${X} 17c-2.2-2.2 2.2-3.8 0-6s2.2-3.8 0-6`}
          fill="none"
          stroke={`url(#${Id}h)`}
          strokeWidth="2"
          strokeLinecap="round"
        />
      ))}
    </>
  ),
  "effect-glitch": () => (
    <>
      <rect x="3" y="4" width="15" height="5" rx="1.2" fill="#2fd3e6" />
      <rect x="6" y="9.5" width="15" height="5" rx="1.2" fill="#ff3d8b" />
      <rect x="2" y="15" width="12" height="5" rx="1.2" fill="#eaeaea" />
      <rect x="16" y="16.3" width="5" height="2.4" rx="1" fill="#2fd3e6" />
      <rect x="6" y="9.5" width="6" height="5" fill="#8b6cff" opacity=".85" />
    </>
  ),
  "effect-chromatic-aberration": () => (
    <g style={{ mixBlendMode: "screen" }}>
      <circle cx="9.6" cy="10" r="5.6" fill="#ff3b3b" opacity=".95" />
      <circle cx="14.4" cy="10" r="5.6" fill="#36d26a" opacity=".95" />
      <circle cx="12" cy="14.2" r="5.6" fill="#3b7bff" opacity=".95" />
    </g>
  ),
  "effect-film-grain": (Id) => (
    <>
      <defs>
        <linearGradient id={Id + "f"} x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#8d8d8d" />
          <stop offset="1" stopColor="#4a4a4a" />
        </linearGradient>
      </defs>
      <rect x="3" y="3" width="18" height="18" rx="4" fill={`url(#${Id}f)`} />
      {[
        [7, 7, 1.1], [12, 6, 0.8], [17, 8, 1.2], [9, 11, 0.9], [14.5, 11.5, 1.3], [6.5, 15, 1], [11, 16, 1.2],
        [16.5, 16, 0.9], [18, 12, 0.7], [8.5, 18.5, 0.7], [13.5, 18.5, 0.8], [12, 9, 0.6],
      ].map(([X, Y, R]) => (
        <circle key={X + "-" + Y} cx={X} cy={Y} r={R} fill="#f2f2f2" opacity=".8" />
      ))}
    </>
  ),
  "effect-lens-distortion": (Id) => (
    <>
      <defs>
        <radialGradient id={Id + "l"} cx="40%" cy="35%" r="75%">
          <stop offset="0" stopColor="#7fc4ff" />
          <stop offset="1" stopColor="#2d68d8" />
        </radialGradient>
      </defs>
      <circle cx="12" cy="12" r="9.5" fill={`url(#${Id}l)`} />
      <g fill="none" stroke="#fff" strokeOpacity=".85" strokeWidth="1.2">
        <path d="M5.5 8.5Q12 6 18.5 8.5M4.5 12h15M5.5 15.5Q12 18 18.5 15.5" />
        <path d="M8.5 5.5Q6 12 8.5 18.5M12 3.5v17M15.5 5.5Q18 12 15.5 18.5" />
      </g>
    </>
  ),
  "effect-scanlines": (Id) => (
    <>
      <defs>
        <linearGradient id={Id + "c"} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="#5ff0a8" />
          <stop offset="1" stopColor="#1f9f63" />
        </linearGradient>
      </defs>
      <rect x="2.5" y="4" width="19" height="14" rx="3.5" fill="#2b2b2b" />
      <rect x="4.2" y="5.7" width="15.6" height="10.6" rx="2.2" fill={`url(#${Id}c)`} />
      {[7.4, 9.6, 11.8, 14].map((Y) => (
        <rect key={Y} x="4.2" y={Y} width="15.6" height=".9" fill="#0c3a24" opacity=".7" />
      ))}
      <rect x="9" y="19" width="6" height="1.8" rx=".9" fill="#5a5a5a" />
    </>
  ),
  "effect-sharpen": (Id) => (
    <>
      <defs>
        <linearGradient id={Id + "d"} x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#c9f3ff" />
          <stop offset="1" stopColor="#2fb6d6" />
        </linearGradient>
      </defs>
      <path d="M12 2.5 20 10l-8 11.5L4 10Z" fill={`url(#${Id}d)`} />
      <path d="M4 10h16M12 2.5 9 10l3 11.5L15 10Z" fill="none" stroke="#fff" strokeOpacity=".7" strokeWidth="1" />
      <path d="M19.5 3v3M18 4.5h3" stroke="#fff" strokeWidth="1.3" strokeLinecap="round" />
    </>
  ),
};

export const IsEffectIcon = (Name) => Object.hasOwn(Drawings, Name);

let Counter = 0;
export default function PostEffectIcon({ Name, Size = 24 }) {
  const Id = React.useMemo(() => "pe" + ++Counter + "-", []);
  const Draw = Drawings[Name] || Drawings["effect-motion-blur"];
  return (
    <svg
      className="native-icon effect-icon"
      data-native-icon={Name}
      width={Size}
      height={Size}
      viewBox="0 0 24 24"
      aria-hidden="true"
    >
      {Draw(Id)}
    </svg>
  );
}
