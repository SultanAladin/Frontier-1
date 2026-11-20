// Frontier Geometry Compiler — Studio Icon Set bridge.
// Imports the canonical 24×24 two-tone SVG icon library directly from ../StudioIconSet/IconDefinitions.js
// and supplies matching 24×24 / 1.5px-stroke fallback glyphs only for Geometry / Cluster / SDF slots
// not yet present in StudioIconSet (tracked in MISSING_ICON_SLOTS).

import { FAMILIES, ICONS as STUDIO_ICONS } from '../StudioIconSet/IconDefinitions.js';

const STUDIO_BY_ID = new Map(STUDIO_ICONS.map((entry) => [entry.id, entry]));
const FAMILY_COLOUR = new Map(FAMILIES.map((family) => [family.id, family.colour]));

const p = (d, extra = '') => `<path d="${d}"${extra ? ' ' + extra : ''}/>`;
const ring = (x, y, r) => `<circle cx="${x}" cy="${y}" r="${r}"/>`;
const dot = (x, y, r = 1.5) => `<circle cx="${x}" cy="${y}" r="${r}" fill="currentColor" stroke="none"/>`;
const dashed = (d, pattern = '2 2.5') => p(d, `stroke-dasharray="${pattern}"`);
const filled = (d, opacity = 1) => p(d, `fill="currentColor" stroke="none"${opacity < 1 ? ` fill-opacity="${opacity}"` : ''}`);

// Direct 1:1 bindings to existing icons in Experimental/StudioIconSet/IconDefinitions.js
export const STUDIO_ICON_MAP = {
  // Cluster LOD & Workspace launchers
  cluster: 'studio-cluster-lod',
  uv: 'studio-uv-editor',
  // Basic tools
  grid: 'general-grid',
  lock: 'general-lock',
  fit: 'general-frame',
  upload: 'general-import',
  export: 'general-export',
  rebuild: 'general-rotate',
  visible: 'general-visible',
  settings: 'general-settings',
  scale: 'general-scale',
  delete: 'general-delete',
  copy: 'general-duplicate',
  search: 'general-search',
  // UV Workspace toolbar
  pack: 'uv-pack',
  rotate90: 'uv-rotate-90',
  mirror: 'uv-flip-horizontal',
  checker: 'uv-checker',
  seams: 'uv-seam-mark',
  island: 'uv-island',
  // SDF / Playback / Field indicators
  bounds: 'particle-emit-sphere',
  heatmap: 'force-heatmap',
  play: 'particle-play',
  pause: 'particle-pause',
  reset: 'particle-restart',
};

// Local 24×24 / 1.5px-stroke glyphs strictly following StudioIconSet rules for slots missing in IconDefinitions.js
const CLUSTER_TEAL = '#29c7c2';
const AMBER = '#ffb238';
const BLUE = '#4d9bff';

export const SUPPLEMENTAL_ICONS = {
  // UI Chrome / Status (missing from general family)
  check: {
    colour: '#46d267',
    main: p('M5 12.5l4.5 4.5L19 7'),
    accent: '',
  },
  close: {
    colour: AMBER,
    main: p('M6 6l12 12M18 6L6 18'),
    accent: '',
  },
  help: {
    colour: AMBER,
    main: ring(12, 12, 8.5),
    accent: p('M9.4 9.2a2.7 2.7 0 1 1 4.4 2.1c-1 .7-1.8 1.2-1.8 2.4') + dot(12, 16.8, 1.1),
  },
  folder: {
    colour: AMBER,
    main: p('M3.5 6.5h5.5l2 2h9.5v10a1.5 1.5 0 0 1-1.5 1.5H5a1.5 1.5 0 0 1-1.5-1.5z'),
    accent: p('M3.5 10.5h17'),
  },
  layers: {
    colour: CLUSTER_TEAL,
    main: p('M12 4l8.5 4.5L12 13 3.5 8.5z'),
    accent: p('M3.5 12.5L12 17l8.5-4.5M3.5 16.5L12 21l8.5-4.5'),
  },
  cube: {
    colour: BLUE,
    main: p('M12 3l8 4.5v9L12 21l-8-4.5v-9z'),
    accent: p('M4 7.5L12 12l8-4.5M12 12v9'),
  },
  camera: {
    colour: AMBER,
    main: p('M3.5 8.2h3L8.2 5.5h7.6l1.7 2.7h3v10.3a1.5 1.5 0 0 1-1.5 1.5H5a1.5 1.5 0 0 1-1.5-1.5z'),
    accent: ring(12, 13.4, 3.3),
  },
  // Cluster LOD / Geometry Compiler specific (missing family)
  wire: {
    colour: CLUSTER_TEAL,
    main: p('M12 3.5l8.5 16H3.5z'),
    accent: p('M12 3.5v16M7.75 11.5h8.5'),
  },
  cull: {
    colour: CLUSTER_TEAL,
    main: p('M2.5 12s3.5-6.5 9.5-6.5S21.5 12 21.5 12s-3.5 6.5-9.5 6.5S2.5 12 2.5 12z'),
    accent: p('M4 20L20 4'),
  },
  frustum: {
    colour: CLUSTER_TEAL,
    main: p('M3.5 12l16-7v14z'),
    accent: p('M13 7.8v8.4') + dot(3.5, 12, 1.4),
  },
  cone: {
    colour: CLUSTER_TEAL,
    main: p('M12 3.5L4.5 18.5c2.2 2 12.8 2 15 0z'),
    accent: p('M12 3.5v11.5') + dot(12, 15, 1.3),
  },
  flat: {
    colour: CLUSTER_TEAL,
    main: p('M3.5 18.5l5-12 5 7.5 7-9v13.5z'),
    accent: p('M8.5 6.5v12M13.5 14v4.5'),
  },
  flip: {
    colour: CLUSTER_TEAL,
    main: p('M4 17h16'),
    accent: p('M12 17V5M8.5 8.5L12 5l3.5 3.5'),
  },
  // SDF / Volumetric Baker specific (missing family)
  sdfVolume: {
    colour: CLUSTER_TEAL,
    main: p('M12 3l8 4.5v9L12 21l-8-4.5v-9z'),
    accent: ring(12, 12, 4.2) + dot(12, 12, 1.3),
  },
  sdfSlice: {
    colour: CLUSTER_TEAL,
    main: '<rect x="3.5" y="3.5" width="17" height="17" rx="2"/>',
    accent: ring(12, 12, 5.2) + ring(12, 12, 2.3) + dashed('M3.5 12h17', '1.8 2.4'),
  },
  sdfCutaway: {
    colour: CLUSTER_TEAL,
    main: p('M12 3.5a8.5 8.5 0 1 0 8.5 8.5H12z'),
    accent: p('M12 3.5V12h8.5') + dashed('M14.5 9.5l5-5', '1.6 2.2'),
  },
  sdfBricks: {
    colour: CLUSTER_TEAL,
    main: p('M4 4h16v16H4zM12 4v16M4 12h16'),
    accent: filled('M5 5h6v6H5z', 0.4) + filled('M13 13h6v6h-6z', 0.4) + p('M5 5h6v6H5zM13 13h6v6h-6z'),
  },
};

export const MISSING_ICON_SLOTS = Object.keys(SUPPLEMENTAL_ICONS);

export function renderIcon(name, { size = 16, weight = 1.5, mono = false } = {}) {
  const studioId = STUDIO_ICON_MAP[name] || (STUDIO_BY_ID.has(name) ? name : null);
  let main = '';
  let accent = '';
  let accentColour = 'currentColor';

  if (studioId && STUDIO_BY_ID.has(studioId)) {
    const entry = STUDIO_BY_ID.get(studioId);
    main = entry.main;
    accent = entry.accent;
    accentColour = FAMILY_COLOUR.get(entry.family) || 'currentColor';
  } else if (SUPPLEMENTAL_ICONS[name]) {
    const entry = SUPPLEMENTAL_ICONS[name];
    main = entry.main;
    accent = entry.accent;
    accentColour = entry.colour || 'currentColor';
  }

  const head = `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="${weight}" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">`;
  const mainLayer = main ? `<g>${main}</g>` : '';
  const accentLayer = accent
    ? mono
      ? `<g>${accent}</g>`
      : `<g style="color:var(--icon-accent,${accentColour})">${accent}</g>`
    : '';
  return head + mainLayer + accentLayer + '</svg>';
}

export function hydrateIcons(root = document) {
  root.querySelectorAll('[data-icon]').forEach((node) => {
    if (node.firstChild) return;
    const size = Number(node.dataset.iconSize) || (node.classList.contains('scene-icon') ? 20 : 16);
    const mono = node.hasAttribute('data-icon-mono');
    node.innerHTML = renderIcon(node.dataset.icon, { size, mono });
  });
}
