// Inline stroke icons (24 px grid, 1.6 stroke) shared by the Geometry Compile and Component Viewer panels.
const P = {
  file: '<path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><path d="M14 3v5h5"/>',
  cube: '<path d="M12 2.8 20 7.3v9.4L12 21.2 4 16.7V7.3z"/><path d="M4 7.3l8 4.5 8-4.5M12 11.8v9.4"/>',
  uv: '<rect x="3.5" y="3.5" width="17" height="17" rx="2"/><path d="M3.5 12h17M12 3.5v17"/><path d="M6.5 6.5l3 3M14.5 14.5l3 3"/>',
  sdf: '<circle cx="12" cy="12" r="3"/><circle cx="12" cy="12" r="6" stroke-dasharray="2 2"/><circle cx="12" cy="12" r="9.2" stroke-dasharray="1.5 2.5"/>',
  layers: '<path d="m12 3 9 5-9 5-9-5z"/><path d="m3 13 9 5 9-5"/><path d="m3 17.5 9 3.5 9-3.5" opacity=".6"/>',
  shield: '<path d="M12 3 4.5 6v5.5c0 4.6 3.2 8.2 7.5 9.5 4.3-1.3 7.5-4.9 7.5-9.5V6z"/>',
  camera: '<path d="M4 8h3l1.6-2.5h6.8L17 8h3a1 1 0 0 1 1 1v9a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V9a1 1 0 0 1 1-1z"/><circle cx="12" cy="13" r="3.4"/>',
  tag: '<path d="M3.5 12.3V4.5a1 1 0 0 1 1-1h7.8l8.2 8.2a1.4 1.4 0 0 1 0 2l-6.6 6.6a1.4 1.4 0 0 1-2 0z"/><circle cx="8" cy="8" r="1.4"/>',
  root: '<circle cx="12" cy="12" r="2.6"/><path d="M12 3v4.4M12 16.6V21M3 12h4.4M16.6 12H21"/>',
  pivot: '<circle cx="12" cy="12" r="7.5"/><circle cx="12" cy="12" r="1.6" fill="currentColor"/>',
  bulb: '<path d="M9 18h6M10 21h4"/><path d="M12 3a6 6 0 0 0-3.6 10.8c.7.6 1.1 1.4 1.1 2.2V16h5v-.1c0-.8.4-1.6 1.1-2.2A6 6 0 0 0 12 3z"/>',
  spot: '<path d="M8 3h8l-1.5 6h-5z"/><path d="M9.5 9 4 21h16L14.5 9" stroke-dasharray="2 2"/>',
  box: '<rect x="4" y="4" width="16" height="16" rx="2" stroke-dasharray="3 2"/>',
  sphere: '<circle cx="12" cy="12" r="8.5" stroke-dasharray="3 2"/><ellipse cx="12" cy="12" rx="8.5" ry="3"/>',
  capsule: '<rect x="7" y="2.8" width="10" height="18.4" rx="5" stroke-dasharray="3 2"/>',
  arrow: '<path d="M4 12h14M13 6.5 18.5 12 13 17.5"/>',
  vehicle: '<path d="M3.5 15.5V12l2-4.5h13l2 4.5v3.5"/><path d="M2.5 15.5h19"/><circle cx="7" cy="16.5" r="2"/><circle cx="17" cy="16.5" r="2"/><path d="M6.5 11.5h11"/>',
  sockFixed: '<path d="M12 3.5 19.4 7.75v8.5L12 20.5 4.6 16.25v-8.5z"/><circle cx="12" cy="12" r="2.4"/>',
  sockHinge: '<circle cx="6.5" cy="17.5" r="2.2"/><path d="M8.2 16 18.5 6.5"/><path d="M6.5 9a8.5 8.5 0 0 1 8.5 8.5" stroke-dasharray="2 2"/>',
  sockSlider: '<path d="M3 12h18M3 8.5v7M21 8.5v7"/><rect x="9" y="8.5" width="6" height="7" rx="1.6"/>',
  sockBall: '<circle cx="12" cy="16" r="3"/><path d="M12 13V4.5"/><path d="M12 13 6.5 5M12 13l5.5-8" stroke-dasharray="2 2"/>',
  sockSpring: '<path d="M12 2.5v3M12 18.5v3"/><path d="M8 5.5 16 7.5 8 9.5 16 11.5 8 13.5 16 15.5 8 17.5h8"/>',
  sockSpin: '<circle cx="12" cy="12" r="2.4"/><path d="M12 4a8 8 0 0 1 7.6 5.5M12 20a8 8 0 0 1-7.6-5.5"/><path d="m19.8 6.3-.2 3.2-3.1-.7M4.2 17.7l.2-3.2 3.1.7"/>',
  sockTether: '<circle cx="9" cy="4.5" r="1.6"/><path d="M9.6 6 15 16"/><circle cx="16.2" cy="18.2" r="2.5"/>',
  sockDock: '<path d="M3.5 8h6v8h-6z"/><path d="M9.5 10.5h3M9.5 13.5h3"/><path d="M15 8h5.5v8H15" stroke-dasharray="2 2"/>',
  kick: '<path d="M13 3 6 13h5l-1 8 7-10h-5z"/>',
  link: '<path d="M9.5 14.5 14.5 9.5"/><path d="M11 6.5 13 4.5a3.5 3.5 0 0 1 5 5L16 11.5M13 17.5 11 19.5a3.5 3.5 0 0 1-5-5L8 12.5"/>',
  unlink: '<path d="M11 6.5 13 4.5a3.5 3.5 0 0 1 5 5L16 11.5M13 17.5 11 19.5a3.5 3.5 0 0 1-5-5L8 12.5"/><path d="M4 4l3 3M20 20l-3-3"/>',
  rotate: '<path d="M20 12a8 8 0 1 1-2.4-5.7"/><path d="M20 3.5V8h-4.5"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  search: '<circle cx="11" cy="11" r="6.5"/><path d="m20 20-4.4-4.4"/>',
  trash: '<path d="M4 7h16M9.5 7V4.5h5V7M6.5 7l1 13h9l1-13"/>',
  copy: '<rect x="8" y="8" width="12" height="12" rx="2"/><path d="M16 8V5.5A1.5 1.5 0 0 0 14.5 4h-9A1.5 1.5 0 0 0 4 5.5v9A1.5 1.5 0 0 0 5.5 16H8"/>',
  eye: '<path d="M2.5 12S6 5.5 12 5.5 21.5 12 21.5 12 18 18.5 12 18.5 2.5 12 2.5 12z"/><circle cx="12" cy="12" r="2.8"/>',
  eyeOff: '<path d="M3 3l18 18M10.6 5.6A9.6 9.6 0 0 1 12 5.5c6 0 9.5 6.5 9.5 6.5a16.6 16.6 0 0 1-3 3.8M6.4 6.9C3.9 8.6 2.5 12 2.5 12S6 18.5 12 18.5c1.6 0 3-.4 4.2-1"/>',
  play: '<path d="M7 4.5v15l12-7.5z" fill="currentColor" stroke="none"/>',
  stop: '<rect x="6" y="6" width="12" height="12" rx="1.5" fill="currentColor" stroke="none"/>',
  frame: '<path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5"/>',
  grid: '<path d="M3.5 3.5h17v17h-17zM3.5 9.2h17M3.5 14.8h17M9.2 3.5v17M14.8 3.5v17"/>',
  upload: '<path d="M12 15V3.5M7 8.5l5-5 5 5"/><path d="M4 14.5v4a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-4"/>',
  chevron: '<path d="m9 6 6 6-6 6"/>',
  check: '<path d="m5 12.5 4.5 4.5L19 7.5"/>',
  x: '<path d="M6 6l12 12M18 6 6 18"/>',
  warn: '<path d="M12 4 2.8 19.5h18.4z"/><path d="M12 10v4.5M12 17.2v.3"/>',
  folder: '<path d="M3.5 7a1.5 1.5 0 0 1 1.5-1.5h4.4l2 2.2H19a1.5 1.5 0 0 1 1.5 1.5V17a1.5 1.5 0 0 1-1.5 1.5H5A1.5 1.5 0 0 1 3.5 17z"/>',
  hammer: '<path d="m14.5 6.5 3 3M4 20l8.5-8.5"/><path d="M11 5.5 15 2l7 7-3.5 3.5-2-2-2.5 2.5-3-3 2.5-2.5z"/>',
  graph: '<rect x="3" y="4" width="7" height="6" rx="1.5"/><rect x="14" y="14" width="7" height="6" rx="1.5"/><path d="M10 7h2.5a2 2 0 0 1 2 2v5"/>',
  viewport: '<rect x="3" y="4.5" width="18" height="15" rx="2"/><path d="m8 15 3-3.5 2 2 3-4 2.5 5.5"/>',
  more: '<circle cx="5.5" cy="12" r="1.3" fill="currentColor"/><circle cx="12" cy="12" r="1.3" fill="currentColor"/><circle cx="18.5" cy="12" r="1.3" fill="currentColor"/>',
  download: '<path d="M12 3.5V15M7 10l5 5 5-5"/><path d="M4 14.5v4a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-4"/>',
  lock: '<rect x="5" y="10.5" width="14" height="10" rx="2"/><path d="M8 10.5V7.5a4 4 0 0 1 8 0v3"/>',
  wheel: '<circle cx="12" cy="12" r="8.5"/><circle cx="12" cy="12" r="3"/><path d="M12 3.5V9M12 15v5.5M3.5 12H9M15 12h5.5"/>',
  link: '<path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1"/><path d="M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1"/>',
  help: '<circle cx="12" cy="12" r="8.5"/><path d="M9.8 9.5a2.3 2.3 0 1 1 3.2 2.1c-.6.3-1 .8-1 1.5v.4M12 16.5v.3"/>',
  sparkle: '<path d="M12 3v4M12 17v4M3 12h4M17 12h4M5.6 5.6l2.8 2.8M15.6 15.6l2.8 2.8M5.6 18.4l2.8-2.8M15.6 8.4l2.8-2.8"/>',
  reset: '<path d="M4 12a8 8 0 1 0 2.4-5.7"/><path d="M4 3.5V8h4.5"/>',
  dot: '<circle cx="12" cy="12" r="4" fill="currentColor" stroke="none"/>',
};

export function icon(name, size = 16) {
  const body = P[name] || P.dot;
  return `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${body}</svg>`;
}

export function hydrateIcons(root = document) {
  for (const node of root.querySelectorAll('[data-icon]')) {
    if (node.dataset.iconDone) continue;
    node.insertAdjacentHTML('afterbegin', icon(node.dataset.icon, +(node.dataset.iconSize || 16)));
    node.dataset.iconDone = '1';
  }
}
