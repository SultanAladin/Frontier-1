// Component Viewer stroke-icon vocabulary extended with Level Designer icons (24 px grid, 1.6 stroke).
import { icon as baseIcon } from '../Shared/Studio/Icons.js';

const EXTRA = {
  pointer: '<path d="M5 4l6.2 15.5 2.4-6 6-2.4z"/><path d="M13.5 13.5 19 19"/>',
  move: '<path d="M12 3v18M3 12h18"/><path d="m9.5 5.5 2.5-2.5 2.5 2.5M9.5 18.5 12 21l2.5-2.5M5.5 9.5 3 12l2.5 2.5M18.5 9.5 21 12l-2.5 2.5"/>',
  scale: '<path d="M4 14v6h6M20 10V4h-6"/><path d="M14 10 20 4M10 14 4 20"/><rect x="8.5" y="8.5" width="7" height="7" rx="1"/>',
  gizmo: '<path d="M12 12V3.5M12 12l7.2 4.2M12 12 4.8 16.2"/><path d="m9.8 5.6 2.2-2.2 2.2 2.2M17 16.9l2.4-.6-.6-2.4M7 16.9l-2.4-.6.6-2.4"/><circle cx="12" cy="12" r="2.1"/>',
  sun: '<circle cx="12" cy="12" r="4.2"/><path d="M12 2.5v2.3M12 19.2v2.3M2.5 12h2.3M19.2 12h2.3M5.3 5.3l1.6 1.6M17.1 17.1l1.6 1.6M18.7 5.3l-1.6 1.6M6.9 17.1l-1.6 1.6"/>',
  sky: '<path d="M6.8 17.5h10.6a3.6 3.6 0 0 0 .4-7.18A5.2 5.2 0 0 0 7.7 8.9 4.2 4.2 0 0 0 6.8 17.5z"/>',
  hotspot: '<circle cx="12" cy="12" r="2.6" fill="currentColor" stroke="none"/><circle cx="12" cy="12" r="6.2"/><circle cx="12" cy="12" r="9.5" stroke-dasharray="2.5 2.5"/>',
  palette: '<path d="M12 3a9 9 0 1 0 0 18 2.2 2.2 0 0 0 2.2-2.2c0-.6-.2-1.1-.6-1.5-.4-.4-.6-.9-.6-1.5a2 2 0 0 1 2-2h2.2A3.8 3.8 0 0 0 21 10a9 9 0 0 0-9-7z"/><circle cx="7.5" cy="10.5" r="1" fill="currentColor"/><circle cx="10.5" cy="7.5" r="1" fill="currentColor"/><circle cx="14.5" cy="7.5" r="1" fill="currentColor"/>',
  crown: '<path d="M4 18h16l1-9-5.2 3.8L12 6l-3.8 6.8L3 9z"/><path d="M5 20.5h14"/>',
  plinth: '<ellipse cx="12" cy="15.5" rx="8.5" ry="3.2"/><path d="M3.5 15.5V18c0 1.8 3.8 3.2 8.5 3.2s8.5-1.4 8.5-3.2v-2.5"/><path d="M7.5 14V7.5L12 5l4.5 2.5V14"/>',
  arch: '<path d="M4.5 20V11a7.5 7.5 0 0 1 15 0v9"/><path d="M8 20v-9a4 4 0 0 1 8 0v9"/>',
  sliders: '<path d="M4 7h16M4 17h16"/><circle cx="9" cy="7" r="2" fill="currentColor"/><circle cx="15" cy="17" r="2" fill="currentColor"/>',
  save: '<path d="M5 4h11l3 3v13H5z"/><path d="M8 4v5h7V4"/><rect x="8" y="13" width="8" height="7"/>',
  globe: '<circle cx="12" cy="12" r="8.5"/><ellipse cx="12" cy="12" rx="3.8" ry="8.5"/><path d="M3.8 9.5h16.4M3.8 14.5h16.4"/>',
  tree: '<path d="M12 20v-7"/><path d="M12 13c-3.8 0-6-2.5-6-5.5C6 4.7 8.5 3 12 3s6 1.7 6 4.5c0 3-2.2 5.5-6 5.5z"/><path d="M7 20h10"/>',
  garage: '<path d="M3 10l9-6 9 6v10H3z"/><path d="M7 20v-7h10v7"/><path d="M7 16h10"/>',
  glass: '<rect x="4" y="4" width="16" height="16" rx="1.5"/><path d="M8 9l4-4M8 14l7-7"/>',
  sofa: '<path d="M5 12V8.5A2.5 2.5 0 0 1 7.5 6h9A2.5 2.5 0 0 1 19 8.5V12"/><path d="M3 12h18v4.5a1.5 1.5 0 0 1-1.5 1.5h-15A1.5 1.5 0 0 1 3 16.5z"/><path d="M6 18v2M18 18v2"/>',
  kiosk: '<rect x="7" y="4" width="10" height="7" rx="1"/><path d="M12 11v9M8 20h8"/>',
  bollard: '<path d="M7 20V8a2 2 0 0 1 4 0v12M13 20V8a2 2 0 0 1 4 0v12"/><path d="M11 11c.7.9 1.3.9 2 0M5 20h14"/>',
  panel: '<rect x="4" y="4" width="16" height="16" rx="1"/><path d="M9 4v16M15 4v16"/>',
};

export function icon(name, size = 16) {
  if (EXTRA[name]) {
    return `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${EXTRA[name]}</svg>`;
  }
  return baseIcon(name, size);
}

export function iconEl(name, size = 16, cls = '') {
  const span = document.createElement('span');
  span.className = `ico ${cls}`.trim();
  span.innerHTML = icon(name, size);
  return span;
}

export function hydrateLevelIcons(root = document) {
  for (const node of root.querySelectorAll('[data-icon]')) {
    if (node.dataset.iconDone) continue;
    node.insertAdjacentHTML('afterbegin', icon(node.dataset.icon, +(node.dataset.iconSize || 16)));
    node.dataset.iconDone = '1';
  }
}
