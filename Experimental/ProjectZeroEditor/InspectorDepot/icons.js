/* Icon set — single stroke language, 24×24 viewBox, matching Slate's reference sheet. */

export const svg = (path, { size = 14, color = 'currentColor', width = 1.9 } = {}) =>
  `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="${color}"
    stroke-width="${width}" stroke-linecap="round" stroke-linejoin="round">${path}</svg>`;

export const P = {
  folder:  '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>',
  cube:    '<path d="M12 2 3 7v10l9 5 9-5V7z"/><path d="M12 22V12"/><path d="m3 7 9 5 9-5"/>',
  sphere:  '<circle cx="12" cy="12" r="9"/><ellipse cx="12" cy="12" rx="9" ry="4"/><path d="M12 3v18"/>',
  torus:   '<circle cx="12" cy="12" r="9"/><ellipse cx="12" cy="12" rx="9" ry="3.4"/>',
  cylinder:'<ellipse cx="12" cy="6" rx="7" ry="3"/><path d="M5 6v12a7 3 0 0 0 14 0V6"/>',
  plane:   '<path d="m2 16 10-9 10 9-10 4z"/>',
  light:   '<circle cx="12" cy="12" r="4"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.2 2.2M16.9 16.9l2.2 2.2M19.1 4.9l-2.2 2.2M7.1 16.9 4.9 19.1"/>',
  spot:    '<path d="M12 3 5 17h14z"/><ellipse cx="12" cy="18" rx="7" ry="3"/>',
  camera:  '<rect x="2" y="6" width="13" height="12" rx="2"/><path d="m15 10 7-4v12l-7-4z"/>',
  sky:     '<path d="M4 16a5 5 0 1 1 3-9 6 6 0 1 1 5 9z"/><path d="M2 20h20"/>',
  sun:     '<circle cx="12" cy="12" r="5"/><path d="M12 1v3M12 20v3M1 12h3M20 12h3M4.2 4.2l2.1 2.1M17.7 17.7l2.1 2.1M19.8 4.2l-2.1 2.1M6.3 17.7l-2.1 2.1"/>',
  moon:    '<path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/>',
  stars:   '<path d="m12 2 1.9 5.1L19 9l-5.1 1.9L12 16l-1.9-5.1L5 9l5.1-1.9z"/><path d="M18.5 15.5 19 17l1.5.5L19 18l-.5 1.5L18 18l-1.5-.5L18 17z"/><path d="M5 15l.4 1.2L6.6 16.6 5.4 17 5 18.2 4.6 17 3.4 16.6 4.6 16.2z"/>',
  cloud:   '<path d="M6.5 18a4.5 4.5 0 0 1-.6-8.96A6 6 0 0 1 17.6 9.3 3.9 3.9 0 0 1 17.5 18z"/>',
  fog:     '<path d="M3 9h18M5 13h14M4 17h16"/><path d="M6.5 5.5a5 5 0 0 1 9.5 0"/>',
  wind:    '<path d="M3 8h9a3 3 0 1 0-3-3"/><path d="M3 13h13a3 3 0 1 1-3 3"/><path d="M3 18h7"/>',
  water:   '<path d="M2 8c2.5 0 2.5 2 5 2s2.5-2 5-2 2.5 2 5 2 2.5-2 5-2"/><path d="M2 14c2.5 0 2.5 2 5 2s2.5-2 5-2 2.5 2 5 2 2.5-2 5-2"/><path d="M2 20c2.5 0 2.5 1.6 5 1.6"/>',
  particles:'<circle cx="6" cy="7" r="1.6"/><circle cx="17" cy="5" r="1.2"/><circle cx="12" cy="12" r="2"/><circle cx="7" cy="17" r="1.3"/><circle cx="18" cy="16" r="1.7"/>',
  probe:   '<circle cx="12" cy="12" r="8"/><path d="M12 4a8 8 0 0 0 0 16"/><path d="M6.5 6.5A7.6 7.6 0 0 1 17 17"/>',
  audio:   '<path d="M4 9v6h4l5 4V5L8 9z"/><path d="M17 8.5a5 5 0 0 1 0 7"/><path d="M20 6a9 9 0 0 1 0 12"/>',
  post:    '<circle cx="12" cy="12" r="9"/><path d="M12 3v18"/><path d="M12 3a9 9 0 0 1 0 18" fill="currentColor" opacity=".35" stroke="none"/>',
  fx:      '<path d="M5 3v4M3 5h4M6 17v4M4 19h4M13 3l2.4 6.1L21.5 11.5 15.4 13.9 13 20l-2.4-6.1L4.5 11.5l6.1-2.4z"/>',
  eye:     '<path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7-10-7-10-7z"/><circle cx="12" cy="12" r="3"/>',
  eyeoff:  '<path d="M2 12s3.5-7 10-7c2 0 3.8.7 5.3 1.6M22 12s-3.5 7-10 7c-2 0-3.8-.7-5.3-1.6"/><path d="m3 3 18 18"/>',
  lock:    '<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/>',
  unlock:  '<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 7.5-2"/>',
  solo:    '<path d="M12 3 14.5 9 21 9.7l-4.9 4.3 1.5 6.4L12 17l-5.6 3.4L7.9 14 3 9.7 9.5 9z"/>',
  motion:  '<path d="M3 12h4l3-8 4 16 3-8h4"/>',
  chev:    '<path d="m9 6 6 6-6 6"/>',
  chevdown:'<path d="m6 9 6 6 6-6"/>',
  search:  '<circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/>',
  close:   '<path d="M18 6 6 18M6 6l12 12"/>',
  pin:     '<path d="M15 3 21 9l-4 1-3.5 3.5L14 18l-2 2-3.5-5L3 21l5.5-5.5L3.5 12l2-2 4.5.5L13.5 7z"/>',
  focus:   '<circle cx="12" cy="12" r="3"/><path d="M4 8V5a1 1 0 0 1 1-1h3M16 4h3a1 1 0 0 1 1 1v3M20 16v3a1 1 0 0 1-1 1h-3M8 20H5a1 1 0 0 1-1-1v-3"/>',
  settings:'<circle cx="12" cy="12" r="3"/><path d="M12 1v4M12 19v4M4.2 4.2 7 7M17 17l2.8 2.8M1 12h4M19 12h4M4.2 19.8 7 17M17 7l2.8-2.8"/>',
  layers:  '<path d="m12 2 9 5-9 5-9-5z"/><path d="m3 12 9 5 9-5"/><path d="m3 17 9 5 9-5"/>',
  panelL:  '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M9 4v16"/>',
  panelR:  '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M15 4v16"/>',
  play:    '<path d="M6 4l14 8-14 8z"/>',
  pause:   '<rect x="6" y="4" width="4" height="16" rx="1"/><rect x="14" y="4" width="4" height="16" rx="1"/>',
  stop:    '<rect x="6" y="6" width="12" height="12" rx="2"/>',
  tag:     '<path d="M3 12.5V4a1 1 0 0 1 1-1h8.5L21 11.5 12.5 20 3 12.5z"/><circle cx="7.5" cy="7.5" r="1.4"/>',
  step:    '<path d="M7 5v14l9-7z"/><path d="M18 5v14"/>',
  sim:     '<path d="M12 3a9 9 0 1 0 9 9"/><path d="M12 7v5l3.5 2"/><path d="M17 3l4 2-4 2z"/>',
  plus:    '<path d="M12 5v14M5 12h14"/>',
  trash:   '<path d="M4 7h16M9 7V5a1 1 0 0 1 1-1h4a1 1 0 0 1 1 1v2M6 7l1 13a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1l1-13"/>',
  copy:    '<rect x="9" y="9" width="12" height="12" rx="2"/><path d="M5 15V5a2 2 0 0 1 2-2h8"/>',
  reset:   '<path d="M3 12a9 9 0 1 0 3-6.7"/><path d="M3 4v5h5"/>',
  command: '<path d="M6 3a3 3 0 1 1-3 3v12a3 3 0 1 0 3-3h12a3 3 0 1 1 3 3V6a3 3 0 1 0-3 3H6z"/>',
  check:   '<path d="m4 12 6 6L20 6"/>',
  alert:   '<path d="M12 3 2 20h20z"/><path d="M12 10v4"/><path d="M12 17.2v.4"/>',
  arrowout:'<path d="M8.5 15.5 15.5 8.5"/><path d="M9.5 8.5h6v6"/>',
  grid:    '<path d="M3 9h18M3 15h18M9 3v18M15 3v18"/>',
  world:   '<circle cx="12" cy="12" r="9"/><path d="M3 12h18"/><path d="M12 3a14 14 0 0 1 0 18 14 14 0 0 1 0-18z"/>',
};

/* Entity glyphs are compact two-colour flat marks. Utility controls stay monochrome so colour
   continues to mean “thing in the world”, never “button”. */
const MULTI = {
  folder: ['#f4c95d','#ff8a5b'], cube: ['#ff7b6b','#ffd166'], sphere: ['#75c7ff','#9b8cff'], torus: ['#b58cff','#ff87c8'], cylinder: ['#7bd6b2','#62a8ff'], plane: ['#62c7e8','#a7f3d0'],
  light: ['#ffd84d','#ff8b3d'], spot: ['#ffe36e','#ff7657'], camera: ['#63d3ff','#8b7cff'], sky: ['#69bfff','#b899ff'], sun: ['#ffd34e','#ff7b42'], moon: ['#d2dcff','#8d9cff'], stars: ['#e8e8ff','#9d8cff'], cloud: ['#eef5ff','#8eb9dd'], fog: ['#b7c8d8','#7d91aa'], wind: ['#6ee7c2','#6ba8ff'], water: ['#52d2ef','#557cff'],
  motion: ['#ff79c6','#62d8ff'], particles: ['#ff8bd5','#ffd15c'], probe: ['#61e2ff','#8b7cff'], audio: ['#b991ff','#ff82b8'], post: ['#ff9a63','#8d7dff'], fx: ['#ff81ca','#ffd35e'], world: ['#8fd070','#55c9e8'], layers: ['#d6a0ff','#68c9ff'],
};
export const folderIcon = name => ({ Environment:'sky', Water:'water', Terrain:'world', Assets:'layers', Objects:'cube', Lighting:'light', Cameras:'camera', Effects:'fx', Curves:'motion' })[name] || 'folder';
export const ic = (name, opts = {}) => {
  const pal = MULTI[name];
  if (!pal || opts.mono) return svg(P[name] || P.cube, opts);
  const size = opts.size || 14, primary = opts.color || pal[0], width = opts.width || 1.75;
  return `<svg class="multi-icon" width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" aria-hidden="true">
    <rect x="1" y="1" width="22" height="22" rx="6" fill="${primary}" opacity=".13"/>
    <g stroke="${primary}" stroke-width="${width}" stroke-linecap="round" stroke-linejoin="round">${P[name] || P.cube}</g>
    <circle cx="18.5" cy="5.5" r="2.25" fill="${pal[1]}" stroke="#0b0b0b" stroke-width="1"/>
  </svg>`;
};
