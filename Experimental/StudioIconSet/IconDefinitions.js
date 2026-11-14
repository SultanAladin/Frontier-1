// Frontier Studio icon set — 24 × 24 grid, 2 px safe margin, 1.5 px round strokes.
// Every icon is two layers: `main` (neutral, currentColor) and `accent` (family colour, carries the meaning).
// `dot(x, y, r)` is a filled accent/primary dot; everything else is raw SVG path data or elements.

const dot = (x, y, r = 1.5) => `<circle cx="${x}" cy="${y}" r="${r}" fill="currentColor" stroke="none"/>`;
const ring = (x, y, r) => `<circle cx="${x}" cy="${y}" r="${r}"/>`;
const p = (d, extra = '') => `<path d="${d}"${extra ? ' ' + extra : ''}/>`;
const dashed = (d, pattern = '2 2.6') => p(d, `stroke-dasharray="${pattern}"`);
const filled = (d, opacity = 1) => p(d, `fill="currentColor" stroke="none"${opacity < 1 ? ` fill-opacity="${opacity}"` : ''}`);

export const FAMILIES = [
  { id: 'studio', label: 'Studios', colour: '#b58cff', note: 'Launcher and tab icons for each editor' },
  { id: 'general', label: 'Basic tools', colour: '#ffb238', note: 'Shared by every studio: selection, transform, history, file' },
  { id: 'uv', label: 'UV Workspace', colour: '#4d9bff', note: 'Unwrap, seams, islands, layout and inspection' },
  { id: 'particle', label: 'Particle Editor', colour: '#ff8a4c', note: 'Emitter shapes, modules over life, playback' },
  { id: 'force', label: 'Force Field Editor', colour: '#46d267', note: 'Field types, falloff and field visualisation' },
];

const I = (family, id, name, tags, main, accent = '') => ({ family, id: `${family}-${id}`, name, tags, main, accent });

export const ICONS = [
  // ---------------------------------------------------------------- Studios
  I('studio', 'uv-editor', 'UV Workspace', 'unwrap texture coordinates', p('M4 3.5V20h16.5') + p('M2.5 5.5L4 3.5l1.5 2M18.5 18.5l2 1.5-2 1.5'), p('M8 16l1.5-7 6.5-1.5 2.5 5-4.5 4.5z') + dot(8, 16, 1.3) + dot(9.5, 9, 1.3) + dot(16, 7.5, 1.3) + dot(18.5, 12.5, 1.3) + dot(14, 17, 1.3)),
  I('studio', 'particle-editor', 'Particle Editor', 'vfx sparks emitter', p('M11 3l1.9 5.6L18.5 10.5l-5.6 1.9L11 18l-1.9-5.6L3.5 10.5l5.6-1.9z'), p('M18.5 14.5l.8 2.2 2.2.8-2.2.8-.8 2.2-.8-2.2-2.2-.8 2.2-.8z') + dot(19, 5, 1.3) + dot(5, 19, 1.3)),
  I('studio', 'forcefield-editor', 'Force Field Editor', 'forces wind vortex physics', p('M5 4c4.5 3 4.5 13 0 16M19 4c-4.5 3-4.5 13 0 16') + dashed('M12 3v5M12 16v5'), ring(12, 12, 3.2) + dot(12, 12, 1.2)),
  I('studio', 'cluster-lod', 'Cluster LOD Studio', 'nanite lod meshlet hierarchy', p('M12 3.5l9 16H3z'), p('M7.5 11.5h9L12 19.5z')),
  I('studio', 'fluid-editor', 'Fluid Editor', 'liquid smoke simulation', p('M12 3c4 4.8 6.5 8 6.5 11.5a6.5 6.5 0 0 1-13 0C5.5 11 8 7.8 12 3z'), p('M8.5 15c1.2-1 2.3 1 3.5 0s2.3-1 3.5 0')),
  I('studio', 'fracture-editor', 'Fracture Editor', 'destruction shatter voronoi', p('M12 3l8 4.5v9L12 21l-8-4.5v-9z'), p('M12 3l-1.5 5 3 3-2.5 4.5 1 5.5M10.5 8L4 7.5M13.5 11l6.5 2')),
  I('studio', 'texture-studio', 'Texture Studio', 'image paint bitmap', '<rect x="3.5" y="3.5" width="17" height="17" rx="2.5"/>', p('M3.5 16l4.5-4.5 4 4 2.5-2.5 6 6') + ring(15.5, 8.5, 1.8)),
  I('studio', 'material-editor', 'Material Editor', 'shader surface pbr', ring(12, 12, 8.5), p('M7.5 9.5a5.5 5.5 0 0 1 4-3.5') + p('M4.2 15.3c4.3 2.6 11.3 2.6 15.6 0')),
  I('studio', 'light-editor', 'Photometric Editor', 'light lamp ies', p('M8.5 14.5a6 6 0 1 1 7 0c-.700.6-1 1.5-1 2.5h-5c0-1-.300-1.9-1-2.5zM10 20.5h4'), p('M10.3 10.5l1.7 1.7 1.7-1.7M12 12.2V17')),

  // ---------------------------------------------------------------- Basic tools
  I('general', 'select', 'Select', 'cursor pointer pick', p('M5.5 3.5l12.5 7.3-5.4 1.4-2.5 5.1z'), p('M14 15l4 5.5')),
  I('general', 'box-select', 'Box Select', 'marquee rectangle region', dashed('M4 4h16v16H4z', '2.4 2.6'), p('M11 11l8.5 3.5-3.5 1.5-1.5 3.5z')),
  I('general', 'move', 'Move', 'translate position grab', p('M12 3v18M3 12h18M9.5 5.5L12 3l2.5 2.5M9.5 18.5L12 21l2.5-2.5M5.5 9.5L3 12l2.5 2.5M18.5 9.5L21 12l-2.5 2.5'), dot(12, 12, 1.8)),
  I('general', 'rotate', 'Rotate', 'turn orbit angle', p('M19.5 12a7.5 7.5 0 1 1-2.4-5.5M17.5 2.5v4.2h-4.2'), dot(12, 12, 1.8)),
  I('general', 'scale', 'Scale', 'resize size', '<rect x="4" y="12.5" width="7.5" height="7.5" rx="1"/>', p('M13 11l7-7M14.5 4H20v5.5') + dashed('M4 9V4h6M20 13v7h-6', '2 2.6')),
  I('general', 'gizmo', 'Transform Gizmo', 'axes xyz manipulator', p('M12 13.5V4.5M12 13.5l-7.3 4.5M12 13.5l7.3 4.5'), dot(12, 4.5, 1.7) + dot(4.7, 18, 1.7) + dot(19.3, 18, 1.7)),
  I('general', 'pivot', 'Pivot', 'origin anchor centre', ring(12, 12, 5.5) + p('M12 2.5v4M12 17.5v4M2.5 12h4M17.5 12h4'), dot(12, 12, 2)),
  I('general', 'snap', 'Snap', 'magnet grid align', p('M6 4v8a6 6 0 0 0 12 0V4h-4v8a2 2 0 0 1-4 0V4z'), p('M6 8h4M14 8h4')),
  I('general', 'frame', 'Frame Selection', 'focus fit zoom', p('M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5'), ring(12, 12, 2.8)),
  I('general', 'grid', 'Grid', 'guides lattice', '<rect x="4" y="4" width="16" height="16" rx="1.5"/>', p('M4 9.3h16M4 14.7h16M9.3 4v16M14.7 4v16')),
  I('general', 'undo', 'Undo', 'back history revert', p('M4.5 9.5H14a5 5 0 0 1 0 10h-3.5'), p('M8.5 5L4 9.5 8.5 14')),
  I('general', 'redo', 'Redo', 'forward history repeat', p('M19.5 9.5H10a5 5 0 0 0 0 10h3.5'), p('M15.5 5L20 9.5 15.5 14')),
  I('general', 'duplicate', 'Duplicate', 'copy clone', '<rect x="9" y="9" width="11" height="11" rx="2"/>', p('M6 15H5.5A1.5 1.5 0 0 1 4 13.5v-8A1.5 1.5 0 0 1 5.5 4h8A1.5 1.5 0 0 1 15 5.5V6')),
  I('general', 'delete', 'Delete', 'remove trash bin', p('M4.5 7h15M10 7V4.5h4V7M6.5 7l.900 12a1.5 1.5 0 0 0 1.5 1.4h6.2a1.5 1.5 0 0 0 1.5-1.4L17.5 7'), p('M10.3 11v5.5M13.7 11v5.5')),
  I('general', 'visible', 'Visibility', 'eye show hide', p('M2.5 12s3.5-6.5 9.5-6.5S21.5 12 21.5 12s-3.5 6.5-9.5 6.5S2.5 12 2.5 12z'), ring(12, 12, 2.8)),
  I('general', 'lock', 'Lock', 'protect freeze', '<rect x="5" y="11" width="14" height="9.5" rx="2"/>' + p('M8 11V8a4 4 0 0 1 8 0v3'), p('M12 14.5v2.5')),
  I('general', 'settings', 'Settings', 'options sliders preferences', p('M4 7.5h8M18 7.5h2M4 16.5h2M12 16.5h8'), ring(15, 7.5, 2.5) + ring(9, 16.5, 2.5)),
  I('general', 'import', 'Import', 'load open file', p('M4.5 15.5V19a1 1 0 0 0 1 1h13a1 1 0 0 0 1-1v-3.5'), p('M12 4v11M7.5 10.5L12 15l4.5-4.5')),
  I('general', 'export', 'Export', 'save bake write', p('M4.5 15.5V19a1 1 0 0 0 1 1h13a1 1 0 0 0 1-1v-3.5'), p('M12 15V4M7.5 8.5L12 4l4.5 4.5')),
  I('general', 'search', 'Search', 'find filter', ring(10.5, 10.5, 6), p('M15 15l5.5 5.5')),

  // ---------------------------------------------------------------- UV Workspace
  I('uv', 'unwrap', 'Unwrap', 'unfold net flatten', p('M9 3h6v6h6v6h-6v6H9v-6H3V9h6z'), dashed('M9 9h6v6H9z')),
  I('uv', 'seam-mark', 'Mark Seam', 'cut edge split', p('M12 3L4 12l8 9M12 3l8 9-8 9'), p('M12 3v18', 'stroke-width="2.4"')),
  I('uv', 'seam-clear', 'Clear Seam', 'remove seam edge', p('M12 3L4 12l8 9M12 3l8 9-8 9'), dashed('M12 3v18', '1.6 3')),
  I('uv', 'island', 'Select Island', 'shell chart region', dashed('M5 8.5l6-4.5 8 3-2 8-7 5-5.5-5z', '2.4 2.4'), filled('M8 9.8l3.6-2.7 4.8 1.8-1.2 4.8-4.2 3-3.3-3z', 0.35) + p('M8 9.8l3.6-2.7 4.8 1.8-1.2 4.8-4.2 3-3.3-3z')),
  I('uv', 'pack', 'Pack Islands', 'layout arrange atlas', '<rect x="3.5" y="3.5" width="17" height="17" rx="1.5"/>', p('M6.5 6.5H12v4.5H6.5zM14.5 6.5h3v7.5h-3zM6.5 13.5H12v4H6.5zM14.5 16.5h3v1h-3z')),
  I('uv', 'stitch', 'Stitch', 'weld join sew merge', p('M3.5 6H9v12H3.5zM15 6h5.5v12H15z'), p('M9 9h6M9 12h6M9 15h6')),
  I('uv', 'cut', 'Cut', 'scissors split rip', p('M8.2 7.8L20 17.5M8.2 16.2L20 6.5'), ring(6, 6.5, 2.7) + ring(6, 17.5, 2.7)),
  I('uv', 'relax', 'Relax', 'smooth optimise unstretch', p('M3.5 7c2.8-3 5.7 3 8.5 0s5.7 3 8.5 0M3.5 17c2.8-3 5.7 3 8.5 0s5.7 3 8.5 0'), p('M3.5 12h17')),
  I('uv', 'pin', 'Pin', 'fix anchor vertex', p('M8.5 14h7l-1.2-4.5V6H15.5V3.5h-7V6H9.7v3.5z'), p('M12 14v7')),
  I('uv', 'align', 'Align', 'edge distribute', p('M4.5 3.5v17'), p('M8 6.5h11v4H8zM8 13.5h6.5v4H8z')),
  I('uv', 'straighten', 'Straighten', 'rectify line edges', p('M4 8l4 2.5 4-4.5 4 4.5 4-2.5'), p('M4 17h16') + dot(4, 17, 1.5) + dot(20, 17, 1.5) + dot(12, 17, 1.5)),
  I('uv', 'flip-horizontal', 'Flip Horizontal', 'mirror u', p('M9.5 6v12h-6z'), dashed('M12 3v18') + p('M14.5 6v12h6z')),
  I('uv', 'flip-vertical', 'Flip Vertical', 'mirror v', p('M6 9.5h12v-6z'), dashed('M3 12h18') + p('M6 14.5h12v6z')),
  I('uv', 'rotate-90', 'Rotate 90°', 'quarter turn', '<rect x="4" y="10" width="10" height="10" rx="1"/>', p('M12 4.5a7.5 7.5 0 0 1 7.5 7.5M17 9.5l2.5 2.5L22 9.5')),
  I('uv', 'checker', 'Checker', 'preview pattern texture', '<rect x="4" y="4" width="16" height="16" rx="1"/>', filled('M4.8 4.8h4.8v4.8H4.8zM14.4 4.8h4.8v4.8h-4.8zM9.6 9.6h4.8v4.8H9.6zM4.8 14.4h4.8v4.8H4.8zM14.4 14.4h4.8v4.8h-4.8z', 0.85)),
  I('uv', 'texel-density', 'Texel Density', 'resolution scale pixels', '<rect x="4" y="4" width="16" height="16" rx="1"/>', p('M4 12h8v8M8 12v8M4 16h8') + dot(16, 8, 1.6)),
  I('uv', 'udim', 'UDIM Tiles', 'tile grid multi', p('M3.5 3.5h7.5v7.5H3.5zM13 3.5h7.5v7.5H13zM3.5 13h7.5v7.5H3.5z'), filled('M13 13h7.5v7.5H13z', 0.3) + p('M13 13h7.5v7.5H13z')),
  I('uv', 'project-planar', 'Planar Projection', 'project flat', p('M3 19l4.5-5.5H21L16.5 19z'), p('M8 3.5v6M12 3.5v6M16 3.5v6M6.5 8L8 9.5 9.5 8M10.5 8L12 9.5 13.5 8M14.5 8L16 9.5 17.5 8')),
  I('uv', 'project-cylinder', 'Cylindrical Projection', 'project tube wrap', '<ellipse cx="12" cy="6" rx="7" ry="2.5"/>' + p('M5 6v12a7 2.5 0 0 0 14 0V6'), dashed('M12 8.5v12')),
  I('uv', 'project-sphere', 'Spherical Projection', 'project globe wrap', ring(12, 12, 8.5), p('M3.5 12a8.5 3.2 0 0 0 17 0') + dashed('M12 3.5a13 13 0 0 1 0 17')),
  I('uv', 'overlap', 'Overlap Check', 'intersect collision islands', p('M4 4h11v11H4zM9 9h11v11H9z'), filled('M9 9h6v6H9z', 0.4)),
  I('uv', 'distortion', 'Distortion', 'stretch heat angle area', p('M4 4c3 2 5 2 8 0s5-2 8 0v16c-3-2-5-2-8 0s-5 2-8 0z'), p('M4 12c3 2 5 2 8 0s5-2 8 0M12 4v16')),

  // ---------------------------------------------------------------- Particle Editor
  I('particle', 'emit-point', 'Point Emitter', 'source origin spawn', p('M12 3v3.5M12 17.5V21M3 12h3.5M17.5 12H21M5.6 5.6l2.5 2.5M15.9 15.9l2.5 2.5M5.6 18.4l2.5-2.5M15.9 8.1l2.5-2.5'), dot(12, 12, 2.4)),
  I('particle', 'emit-box', 'Box Emitter', 'volume cube spawn', p('M12 3l8 4.5v9L12 21l-8-4.5v-9zM4 7.5l8 4.5 8-4.5M12 12v9'), dot(8, 12.8, 1.2) + dot(15.5, 15, 1.2) + dot(12, 7.5, 1.2) + dot(16.5, 11, 1.2)),
  I('particle', 'emit-sphere', 'Sphere Emitter', 'volume ball spawn', ring(12, 12, 8.5), dot(12, 12, 1.4) + dot(8.5, 9, 1.2) + dot(15.5, 9.5, 1.2) + dot(9.5, 15.5, 1.2) + dot(15, 15, 1.2)),
  I('particle', 'emit-cone', 'Cone Emitter', 'spray nozzle spawn', p('M10 20.5h4M10 20.5L4.5 9.5M14 20.5l5.5-11') + '<ellipse cx="12" cy="9.5" rx="7.5" ry="2.3"/>', dot(8.5, 4.5, 1.2) + dot(12, 3.2, 1.2) + dot(15.5, 4.8, 1.2)),
  I('particle', 'emit-surface', 'Surface Emitter', 'geometry skin spawn', p('M3 17c3-4 6-4 9 0s6 4 9 0'), p('M6.5 13V7.5M12 16v-5.5M17.5 13V7.5M5 9l1.5-1.5L8 9M10.5 12l1.5-1.5 1.5 1.5M16 9l1.5-1.5L19 9')),
  I('particle', 'burst', 'Burst', 'explode instant spawn', dashed('M12 3.5a8.5 8.5 0 1 0 0 17 8.5 8.5 0 1 0 0-17z', '2 3.34'), dot(12, 12, 2.6) + dot(12, 7, 1.1) + dot(16.3, 9.5, 1.1) + dot(16.3, 14.5, 1.1) + dot(12, 17, 1.1) + dot(7.7, 14.5, 1.1) + dot(7.7, 9.5, 1.1)),
  I('particle', 'rate', 'Spawn Rate', 'emission per second timer', ring(12, 13.5, 7) + p('M10 3h4M12 3v3.5'), p('M12 13.5l3.3-3.3') + dot(12, 13.5, 1.3)),
  I('particle', 'lifetime', 'Lifetime', 'age duration hourglass', p('M6 3h12M6 21h12M7.5 3c0 5.5 4.5 6 4.5 9s-4.5 3.5-4.5 9M16.5 3c0 5.5-4.5 6-4.5 9s4.5 3.5 4.5 9'), p('M9.8 18.5h4.4M12 12v3.5M9.5 6.5h5')),
  I('particle', 'velocity', 'Velocity', 'speed direction initial', p('M4.5 19.5L19 5M11.5 5H19v7.5'), p('M4 12.5l3.5-3.5M11.5 20l3.5-3.5')),
  I('particle', 'size-over-life', 'Size over Life', 'scale curve grow', p('M4 4v16h16'), ring(7.8, 16, 1.2) + ring(12, 12.5, 2.2) + ring(17, 7.5, 3.2)),
  I('particle', 'colour-over-life', 'Colour over Life', 'gradient ramp tint', '<rect x="3" y="6.5" width="18" height="6.5" rx="3.25"/>', filled('M3.5 9.75a2.75 2.75 0 0 1 2.75-2.75H11v5.5H6.25A2.75 2.75 0 0 1 3.5 9.75z', 0.45) + filled('M6.5 19.5L8 16l1.5 3.5zM14.5 19.5L16 16l1.5 3.5z')),
  I('particle', 'gravity', 'Gravity', 'fall weight acceleration', p('M3.5 20.5h17M12 10v7.5M9 14.5l3 3 3-3'), dot(12, 5.5, 2.6)),
  I('particle', 'collision', 'Collision', 'bounce collide surface', p('M17.5 3v18M17.5 7.5l3-2.5M17.5 12.5l3-2.5M17.5 17.5l3-2.5'), p('M4.5 5l12 7-8.5 7M8 15v4h4') + dot(4.5, 5, 1.6)),
  I('particle', 'trail', 'Trail', 'ribbon streak tail', p('M3 17.5c4.5 0 5-10 9.5-10s3.5 5.5 7 5.5') + dashed('M3 20.5c5.5 0 6.5-7.5 9.5-8.5'), dot(20, 13, 2.2)),
  I('particle', 'sprite', 'Sprite', 'billboard quad flipbook', '<rect x="3.5" y="4.5" width="17" height="15" rx="2"/>', filled('M12 7.5l1.3 3.2 3.2 1.3-3.2 1.3L12 16.5l-1.3-3.2L7.5 12l3.2-1.3z')),
  I('particle', 'sub-emitter', 'Sub-emitter', 'child secondary spawn event', ring(8, 8, 4.5), p('M11.2 11.2l4 4M12.5 8h3.5') + ring(17.5, 17.5, 2.8) + ring(18.5, 8, 2.2)),
  I('particle', 'noise', 'Turbulence', 'noise curl jitter', p('M3 7c2-2.5 4 2.5 6 0s3.5-2.5 5.5 0 4 2.5 6.5-.500M3 17c2-2.5 4 2.5 6 0s3.5-2.5 5.5 0 4 2.5 6.5-.500'), p('M3 12c2.5 2.5 4-2.5 6.5 0s4 2.5 6 0 3-1.5 5.5 0')),
  I('particle', 'curve', 'Curve', 'graph spline editor over life', p('M4 4v16h16'), p('M5.5 18c5 0 5-11.5 13.5-12') + dot(5.5, 18, 1.5) + dot(19, 6, 1.5)),
  I('particle', 'play', 'Play', 'simulate run start', p('M8 5l11 7-11 7z')),
  I('particle', 'pause', 'Pause', 'hold stop', '<rect x="6.5" y="5" width="4" height="14" rx="1"/><rect x="13.5" y="5" width="4" height="14" rx="1"/>'),
  I('particle', 'restart', 'Restart', 'reset rewind replay', p('M4.5 12a7.5 7.5 0 1 0 2.4-5.5M4.5 3v4.2h4.2'), filled('M10.5 9.2l4.5 2.8-4.5 2.8z')),
  I('particle', 'loop', 'Loop', 'repeat cycle', p('M17 3l3 3-3 3M4 11V9.5A3.5 3.5 0 0 1 7.5 6H20'), p('M7 21l-3-3 3-3M20 13v1.5a3.5 3.5 0 0 1-3.5 3.5H4')),
  I('particle', 'step', 'Step Frame', 'next advance single', p('M6 5l9 7-9 7z'), p('M18.5 5v14')),

  // ---------------------------------------------------------------- Force Field Editor
  I('force', 'wind', 'Directional Wind', 'uniform push breeze', p('M3 8h10.5a2.8 2.8 0 1 0-2.8-2.8M3 16h13.5a2.8 2.8 0 1 1-2.8 2.8'), p('M3 12h17M17.5 9.5L20 12l-2.5 2.5')),
  I('force', 'attract', 'Attractor', 'pull radial inward point', p('M4 4l5 5M20 4l-5 5M4 20l5-5M20 20l-5-5M9 5.5V9H5.5M15 5.5V9h3.5M9 18.5V15H5.5M15 18.5V15h3.5'), dot(12, 12, 2.2)),
  I('force', 'repel', 'Repulsor', 'push radial outward explode', p('M9 9L4 4M15 9l5-5M9 15l-5 5M15 15l5 5M4 7.5V4h3.5M20 7.5V4h-3.5M4 16.5V20h3.5M20 16.5V20h-3.5'), ring(12, 12, 2.4)),
  I('force', 'vortex', 'Vortex', 'swirl spin tornado', p('M19.5 12a7.5 7.5 0 0 1-12.8 5.3M6 13.5v4.2h4.2M4.5 12a7.5 7.5 0 0 1 12.8-5.3M18 10.5V6.3h-4.2'), ring(12, 12, 2.6) + dot(12, 12, 1)),
  I('force', 'turbulence', 'Turbulence', 'noise chaos curl', p('M3 7h6.5A2.5 2.5 0 1 1 7 9.5M21 17h-6.5a2.5 2.5 0 1 1 2.5-2.5'), p('M3 14.5c2.5 0 2.5 3 5 3M13 7c2 0 2-2.5 4-2.5s2 2.5 4 2.5')),
  I('force', 'drag', 'Drag', 'damping resistance slow', ring(15.5, 12, 5), p('M3 7.5h6.5M3 12h4.5M3 16.5h6.5')),
  I('force', 'gravity', 'Gravity Field', 'uniform down weight', p('M6 3.5v11M12 3.5v11M18 3.5v11M4 12l2 2.5L8 12M10 12l2 2.5 2-2.5M16 12l2 2.5 2-2.5'), p('M3 20h18')),
  I('force', 'point', 'Point Force', 'radial source centre', dashed('M12 3.5a8.5 8.5 0 1 0 0 17 8.5 8.5 0 1 0 0-17z', '2.2 3.14') + ring(12, 12, 4.8), dot(12, 12, 1.8)),
  I('force', 'curve-guide', 'Curve Guide', 'spline path follow', p('M3.5 18C8 18 7 6 12 6s4 12 8.5 12'), dot(3.5, 18, 1.7) + dot(20.5, 18, 1.7) + p('M10 3.8L12.5 6 10 8.2')),
  I('force', 'magnetic', 'Magnetic', 'dipole field lines', '<rect x="7" y="9.5" width="10" height="5" rx="1"/>' + p('M12 9.5v5'), p('M16 9.5c0-6-8-6-8 0M16 14.5c0 6-8 6-8 0M19.5 6.5c2.5 3 2.5 8 0 11M4.5 6.5c-2.5 3-2.5 8 0 11')),
  I('force', 'falloff', 'Falloff', 'attenuation distance curve', p('M4 4v16h16'), p('M4.5 7c7.5 0 7.5 11.5 15 11.5')),
  I('force', 'range', 'Range', 'bounds radius extent', dashed('M4 4h16v16H4z', '2.4 2.9'), p('M7.5 12h9M9.5 9.5L7 12l2.5 2.5M14.5 9.5L17 12l-2.5 2.5')),
  I('force', 'strength', 'Strength', 'magnitude intensity gauge', p('M4.5 17a7.5 7.5 0 1 1 15 0M4.5 17h2M17.5 17h2M12 9.5v1.5'), p('M12 17l4.2-4.8') + dot(12, 17, 1.6)),
  I('force', 'streamlines', 'Streamlines', 'field lines trace visualise', p('M3 6c6.5 0 7 5 18 5M3 12c6.5 0 8.5 3 18 3M3 18c7.5 0 9.5 1 18 1'), dot(3, 6, 1.5) + dot(3, 12, 1.5) + dot(3, 18, 1.5)),
  I('force', 'vector-grid', 'Vector Grid', 'arrows samples quiver', p('M5 7.5l2.5-3M11.5 7.5l3-2.5M18 7.5l3.2-1.5M5 13.5l1.8-3.3M11.5 13.5l2.6-2.9M18 13.5l3-2.2M5 19.5l.800-3.5M11.5 19.5l2-3.2M18 19.5l2.8-2.8'), dot(5, 7.5, 1.2) + dot(11.5, 7.5, 1.2) + dot(18, 7.5, 1.2) + dot(5, 13.5, 1.2) + dot(11.5, 13.5, 1.2) + dot(18, 13.5, 1.2) + dot(5, 19.5, 1.2) + dot(11.5, 19.5, 1.2) + dot(18, 19.5, 1.2)),
  I('force', 'heatmap', 'Heatmap', 'magnitude slice colour', '<rect x="3.5" y="3.5" width="17" height="17" rx="1.5"/>', filled('M6 6h3.6v3.6H6z', 0.25) + filled('M10.2 6h3.6v3.6h-3.6z', 0.5) + filled('M14.4 6H18v3.6h-3.6z', 0.9) + filled('M6 10.2h3.6v3.6H6z', 0.5) + filled('M10.2 10.2h3.6v3.6h-3.6z', 0.9) + filled('M14.4 10.2H18v3.6h-3.6z', 0.5) + filled('M6 14.4h3.6V18H6z', 0.9) + filled('M10.2 14.4h3.6V18h-3.6z', 0.5) + filled('M14.4 14.4H18V18h-3.6z', 0.25)),
  I('force', 'obstacle', 'Obstacle', 'collider deflect block', p('M3 8.5c5 0 5-4 9-4s4 4 9 4M3 15.5c5 0 5 4 9 4s4-4 9-4'), ring(12, 12, 3.2)),
];
