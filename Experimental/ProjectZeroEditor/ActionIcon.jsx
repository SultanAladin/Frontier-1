import React from "react";

// Solid silhouettes for inspector actions; intentionally separate from the
// native outliner artwork and thin graph annotations.
export function QuickSymbol(Label, Context = "", Fallback = "visible") {
  const Named = {
    Visible: "visible",
    "Star field": "stars",
    Twinkle: "twinkle",
    Bake: "bake",
    "Use baked": "image",
    "Use baked image": "image",
    Sunlight: "sunlight",
    "Sun disc": "disc",
    "Day cycle": "cycle",
    Dynamic: "motion",
    Static: "pause",
    "Follow Sky": "orbit",
    "Follow Wind": "wind",
    "Spawn from Clouds": "cloud-spawn",
    "Ground Collision": "collision",
    "Air shear": "shear",
    "Alexander's Band": "band",
    Anamorphic: "anamorphic",
    Streaks: "streaks",
    Starburst: "starburst",
  };
  if (Label === "Enabled")
    return (
      {
        wind: "wind",
        clouds: "cloud",
        "local-cloud": "cloud",
        "height-fog": "fog",
        "aerial-fog": "fog",
        "local-fog": "fog",
        precipitation: "rain",
        rainbow: "rainbow",
      }[Context] || "visible"
    );
  return Named[Label] || Fallback;
}

export default function ActionIcon({ Name, Size = 24 }) {
  let Drawing;
  switch (Name) {
    case "lock":
      Drawing = (
        <>
          <rect x="5" y="10" width="14" height="12" rx="3" />
          <path d="M7 11V7a5 5 0 0 1 10 0v4h-3V7a2 2 0 0 0-4 0v4Z" />
        </>
      );
      break;
    case "shadow":
      Drawing = (
        <>
          <path d="M3 5h10v10H3z" />
          <path d="m13 8 9 9-8 5-8-7h7Z" opacity=".4" />
        </>
      );
      break;
    case "gi":
      Drawing = (
        <>
          <circle cx="8" cy="8" r="5" />
          <path d="m3 19 8-7 3 3 7-7v6h-3v-1l-4 6-4-3-5 5Z" />
        </>
      );
      break;
    case "visible":
      Drawing = (
        <path
          fillRule="evenodd"
          d="M1 12C3.7 6.8 7.4 4 12 4s8.3 2.8 11 8c-2.7 5.2-6.4 8-11 8S3.7 17.2 1 12Zm11-5a5 5 0 1 0 0 10 5 5 0 0 0 0-10Zm0 2a3 3 0 1 1 0 6 3 3 0 0 1 0-6Z"
        />
      );
      break;
    case "sunlight":
      Drawing = (
        <>
          <circle cx="12" cy="12" r="5" />
          {Array.from({ length: 8 }, (_, Index) => (
            <rect
              key={Index}
              x="11"
              y="1"
              width="2"
              height="4"
              rx="1"
              transform={`rotate(${Index * 45} 12 12)`}
            />
          ))}
        </>
      );
      break;
    case "disc":
      Drawing = (
        <>
          <circle cx="12" cy="12" r="10" opacity=".18" />
          <circle cx="12" cy="12" r="7.5" />
          <path d="M5 19h14v2H5z" opacity=".45" />
        </>
      );
      break;
    case "stars":
      Drawing = (
        <path d="m9 2 2.6 6.4L18 11l-6.4 2.6L9 20l-2.6-6.4L0 11l6.4-2.6L9 2Zm10 0 1.1 2.9L23 6l-2.9 1.1L19 10l-1.1-2.9L15 6l2.9-1.1L19 2Zm0 13 1.1 2.9L23 19l-2.9 1.1L19 23l-1.1-2.9L15 19l2.9-1.1L19 15Z" />
      );
      break;
    case "twinkle":
      Drawing = (
        <>
          <path d="m12 2 3 7 7 3-7 3-3 7-3-7-7-3 7-3Z" />
          <path
            d="M2 2h3v3H2zm17 0h3v3h-3zM2 19h3v3H2zm17 0h3v3h-3z"
            opacity=".45"
          />
        </>
      );
      break;
    case "cycle":
      Drawing = (
        <>
          <path d="M19.3 4.7A10 10 0 0 0 2 11h3a7 7 0 0 1 12.2-4.6L14 10h9V1l-3.7 3.7ZM4.7 19.3A10 10 0 0 0 22 13h-3a7 7 0 0 1-12.2 4.6L10 14H1v9l3.7-3.7Z" />
          <circle cx="12" cy="12" r="3" />
        </>
      );
      break;
    case "motion":
      Drawing = (
        <>
          <path d="M10 4v16l12-8ZM1 6h6v3H1zm-1 5h7v2H0zm1 4h6v3H1z" />
        </>
      );
      break;
    case "pause":
      Drawing = (
        <>
          <rect x="5" y="4" width="5" height="16" rx="1.5" />
          <rect x="14" y="4" width="5" height="16" rx="1.5" />
        </>
      );
      break;
    case "orbit":
      Drawing = (
        <>
          <path d="M6.5 3.3A10 10 0 1 0 21 8l-2.7 1.2a7 7 0 1 1-10.3-3.3Z" />
          <circle cx="12" cy="12" r="4" />
          <circle cx="18" cy="4" r="3" />
        </>
      );
      break;
    case "wind":
      Drawing = (
        <path d="M2 6h11a2 2 0 1 0-2-2H8a5 5 0 1 1 5 5H2V6Zm0 5h16a5 5 0 1 1-5 5h3a2 2 0 1 0 2-2H2v-3Zm0 5h5a4 4 0 1 1-4 4h2a2 2 0 1 0 2-2H2v-2Z" />
      );
      break;
    case "shear":
      Drawing = (
        <>
          <path d="M1 5h14V2l7 5-7 5V9H1zM23 15H9v-3l-7 5 7 5v-3h14z" />
        </>
      );
      break;
    case "cloud":
      Drawing = (
        <path d="M6 20a6 6 0 0 1-.8-11.9A7.5 7.5 0 0 1 20 9a5.5 5.5 0 0 1-1.5 11Z" />
      );
      break;
    case "cloud-spawn":
      Drawing = (
        <>
          <path
            d="M6 14a5 5 0 0 1-.5-10 6.5 6.5 0 0 1 12 1A4.5 4.5 0 0 1 20 14h-6v-4h-4v4Z"
            opacity=".7"
          />
          <path d="M10 12h4v6h4l-6 6-6-6h4z" />
        </>
      );
      break;
    case "rain":
      Drawing = (
        <>
          <path d="M6 14a5 5 0 0 1-.5-10 6.5 6.5 0 0 1 12 1A4.5 4.5 0 0 1 20 14Z" />
          <path
            d="m6 16 2 1-3 6-2-1Zm7 0 2 1-3 6-2-1Zm7 0 2 1-3 6-2-1Z"
            opacity=".6"
          />
        </>
      );
      break;
    case "collision":
      Drawing = (
        <>
          <path d="M10 1h4v8h4l-6 6-6-6h4ZM2 20h20v3H2zM3 12l4 3-2 2-4-3Zm18 0 2 2-4 3-2-2Z" />
        </>
      );
      break;
    case "fog":
      Drawing = (
        <>
          <rect x="1" y="4" width="17" height="4" rx="2" />
          <rect x="6" y="10" width="17" height="4" rx="2" opacity=".7" />
          <rect x="2" y="16" width="17" height="4" rx="2" opacity=".4" />
        </>
      );
      break;
    case "rainbow":
      Drawing = (
        <>
          <path d="M1 21v-7a11 11 0 0 1 22 0v7h-3v-7a8 8 0 0 0-16 0v7Z" />
          <path
            d="M6 21v-7a6 6 0 0 1 12 0v7h-3v-7a3 3 0 0 0-6 0v7Z"
            opacity=".5"
          />
        </>
      );
      break;
    case "band":
      Drawing = (
        <>
          <path
            d="M1 21v-7a11 11 0 0 1 22 0v7h-2v-7a9 9 0 0 0-18 0v7Z"
            opacity=".4"
          />
          <path d="M6 21v-7a6 6 0 0 1 12 0v7h-4v-7a2 2 0 0 0-4 0v7Z" />
        </>
      );
      break;
    case "bake":
      Drawing = (
        <>
          <path d="M3 14h4v5h10v-5h4v8H3zM10 1h4v8h5l-7 7-7-7h5Z" />
        </>
      );
      break;
    case "image":
      Drawing = (
        <>
          <path
            fillRule="evenodd"
            d="M4 2h18v17H4V2Zm3 12h12l-4-6-3 4-2-2-3 4Zm2-9a2 2 0 1 0 0 4 2 2 0 0 0 0-4Z"
          />
          <path d="M0 6h2v15h16v2H0Z" opacity=".5" />
        </>
      );
      break;
    case "anamorphic":
      Drawing = (
        <>
          <ellipse cx="12" cy="12" rx="4" ry="8" />
          <path d="M0 11h24v2H0Z" />
          <path d="M2 7h20v1H2zm0 9h20v1H2z" opacity=".4" />
        </>
      );
      break;
    case "streaks":
      Drawing = <path d="m0 3 24 6-1 3L0 6Zm0 8 24 3-1 3L0 14Zm0 8h24v3H0Z" />;
      break;
    case "starburst":
      Drawing = (
        <path d="m12 0 2 8 7-5-5 7 8 2-8 2 5 7-7-5-2 8-2-8-7 5 5-7-8-2 8-2-5-7 7 5Z" />
      );
      break;
    case "construct":
      Drawing = (
        <>
          <path d="m8 2 8 4v7l-8 4-8-4V6Z" opacity=".65" />
          <path d="M17 11h3v4h4v3h-4v4h-3v-4h-4v-3h4Z" />
        </>
      );
      break;
    case "focus":
      Drawing = (
        <>
          <path d="M2 2h7v3H5v4H2zm13 0h7v7h-3V5h-4ZM2 15h3v4h4v3H2zm17 0h3v7h-7v-3h4Z" />
          <circle cx="12" cy="12" r="3" />
        </>
      );
      break;
    case "split":
      Drawing = (
        <path fillRule="evenodd" d="M2 4h20v16H2Zm3 3v10h6V7Zm9 0v10h5V7Z" />
      );
      break;
    case "diagnostics":
      Drawing = (
        <>
          <rect x="2" y="13" width="4" height="9" rx="1" />
          <rect x="10" y="7" width="4" height="15" rx="1" />
          <rect x="18" y="2" width="4" height="20" rx="1" />
        </>
      );
      break;
    case "edit":
      Drawing = <path d="m15 2 7 7-12 12-8 1 1-8Zm-9 13-1 4 4-1 10-10-3-3Z" />;
      break;
    case "simulate":
      Drawing = (
        <>
          <path d="M6 1h12v3h-3v4l7 11a3 3 0 0 1-2.5 4h-15A3 3 0 0 1 2 19L9 8V4H6Zm5 8-4 7h10l-4-7V4h-2Z" />
          <circle cx="10" cy="19" r="1" opacity=".3" />
        </>
      );
      break;
    case "play":
      Drawing = (
        <path d="M6 2a1 1 0 0 1 1.5-.8l15 10a1 1 0 0 1 0 1.6l-15 10A1 1 0 0 1 6 22Z" />
      );
      break;
    case "settings":
      Drawing = (
        <path
          fillRule="evenodd"
          d="m9 1 6 0 1 4 3-1 3 5-3 3 3 3-3 5-3-1-1 4H9l-1-4-3 1-3-5 3-3-3-3 3-5 3 1Zm3 6a5 5 0 1 0 0 10 5 5 0 0 0 0-10Z"
        />
      );
      break;
    default:
      Drawing = <path d="M3 3h18v18H3z" />;
  }
  return (
    <svg
      className="action-icon"
      data-action-icon={Name}
      width={Size}
      height={Size}
      viewBox="0 0 24 24"
      fill="currentColor"
      stroke="none"
      aria-hidden="true"
    >
      {Drawing}
    </svg>
  );
}
