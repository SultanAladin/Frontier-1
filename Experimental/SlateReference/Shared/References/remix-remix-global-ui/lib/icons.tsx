import React from 'react';

const C = {
  ink: "#e8e8f0", dim: "#8a8a99", blue: "#5b8cff", cyan: "#37d6d6", green: "#4fd18b",
  amber: "#ffb24d", red: "#ff6b6b", violet: "#b98bff", pink: "#ff7ab8", sky: "#7ec8ff",
  earth: "#c99b6a", mud: "#8a6a45", glass: "#2a2a33"
};

const CLASSIFICATION_HUE: Record<string, string> = {
  scene: C.sky, folder: C.violet, sketch: C.cyan, solid: C.amber,
  cylinder: C.green, sphere: C.pink, cone: C.red, revolve: C.earth, loft: C.blue
};

export const hueOf = (cls: string) => CLASSIFICATION_HUE[cls] || C.dim;

function shade(hex: string, amount: number) {
  const n = parseInt(hex.slice(1), 16);
  const r = Math.round(((n >> 16) & 255) * (1 - amount));
  const g = Math.round(((n >> 8) & 255) * (1 - amount));
  const b = Math.round((n & 255) * (1 - amount));
  return '#' + [r, g, b].map(v => v.toString(16).padStart(2, '0')).join('');
}

export function ClassificationIcon({ cls, size = 18, customHue }: { cls: string, size?: number, customHue?: string }) {
  const h = customHue || hueOf(cls);
  
  let content = null;
  switch (cls) {
    case 'scene':
      content = (
        <>
          <path d="M12 3l8 4-8 4-8-4z" fill={h} />
          <path d="M4 12l8 4 8-4" stroke={shade(h, .28)} strokeWidth="1.7" fill="none" />
          <path d="M4 17l8 4 8-4" stroke={shade(h, .52)} strokeWidth="1.7" fill="none" />
        </>
      );
      break;
    case 'folder':
      content = (
        <>
          <path d="M3 7.5a1.5 1.5 0 011.5-1.5h4l2 2.2h8A1.5 1.5 0 0120 9.7V18a1.5 1.5 0 01-1.5 1.5h-14A1.5 1.5 0 013 18z" fill={shade(h, .55)} />
          <path d="M3 10.6h18V18a1.5 1.5 0 01-1.5 1.5h-15A1.5 1.5 0 013 18z" fill={h} />
        </>
      );
      break;
    case 'solid':
      content = (
        <>
          <path d="M12 3l8 4.5-8 4.5-8-4.5z" fill={h} />
          <path d="M4 7.5v9L12 21v-9z" fill={shade(h, .34)} />
          <path d="M20 7.5v9L12 21v-9z" fill={shade(h, .58)} />
        </>
      );
      break;
    case 'cylinder':
      content = (
        <>
          <path d="M5 7.5h14v9a7 2.6 0 01-14 0z" fill={shade(h, .42)} />
          <ellipse cx="12" cy="7.5" rx="7" ry="2.6" fill={h} />
          <path d="M5 7.5v9M19 7.5v9" stroke={shade(h, .62)} strokeWidth="1.5" fill="none" />
        </>
      );
      break;
    case 'sphere':
      content = (
        <>
          <circle cx="12" cy="12" r="8.4" fill={shade(h, .38)} />
          <path d="M12 3.6a8.4 8.4 0 000 16.8 5 8.4 0 010-16.8z" fill={h} />
          <ellipse cx="12" cy="12" rx="8.4" ry="3.1" fill="none" stroke={shade(h, .66)} strokeWidth="1.3" />
        </>
      );
      break;
    case 'cone':
      content = (
        <>
          <path d="M12 3.4L19 18.2H5z" fill={h} />
          <path d="M12 3.4L19 18.2h-7z" fill={shade(h, .4)} />
          <ellipse cx="12" cy="18.2" rx="7" ry="2.5" fill={shade(h, .6)} />
        </>
      );
      break;
    case 'sketch':
      content = (
        <>
          <path d="M4 20V4h16" stroke={C.dim} strokeWidth="1.4" fill="none" strokeDasharray="2.6 2.4" />
          <path d="M5.5 16.5c3-7 7.5-8.5 13-9" stroke={h} strokeWidth="1.9" fill="none" />
          <circle cx="5.5" cy="16.5" r="1.9" fill={h} />
          <circle cx="18.5" cy="7.5" r="1.9" fill={shade(h, .3)} />
        </>
      );
      break;
    case 'revolve':
      content = (
        <>
          <path d="M12 2.5v19" stroke={C.dim} strokeWidth="1.4" fill="none" strokeDasharray="2.6 2.4" />
          <path d="M12 5.5c3.6 0 6.5 2.9 6.5 6.5S15.6 18.5 12 18.5" stroke={h} strokeWidth="1.9" fill="none" />
          <ellipse cx="12" cy="12" rx="3" ry="6.5" fill="none" stroke={shade(h, .34)} strokeWidth="1.6" />
        </>
      );
      break;
    case 'loft':
      content = (
        <>
          <ellipse cx="8" cy="6.5" rx="4" ry="1.9" fill="none" stroke={h} strokeWidth="1.7" />
          <ellipse cx="16" cy="17.5" rx="4" ry="1.9" fill="none" stroke={shade(h, .4)} strokeWidth="1.7" />
          <path d="M4 6.5L12 17.5M12 6.5L20 17.5" stroke={shade(h, .2)} strokeWidth="1.5" fill="none" />
        </>
      );
      break;
    default:
      content = (
        <>
          <path d="M12 3l8 4.5-8 4.5-8-4.5z" fill={h} />
          <path d="M4 7.5v9L12 21v-9z" fill={shade(h, .34)} />
          <path d="M20 7.5v9L12 21v-9z" fill={shade(h, .58)} />
        </>
      );
  }

  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="none" strokeLinecap="round" strokeLinejoin="round" xmlns="http://www.w3.org/2000/svg">
      {content}
    </svg>
  );
}
