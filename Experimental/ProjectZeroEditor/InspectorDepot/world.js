/* ════════════════════════════════════════════════════════════════════════════════════════════
   WORLD MODEL
   One authored description of every entity kind the editor understands: its icon, its accent,
   and the property groups that both the docked inspector and the floating billboard popup are
   generated from. The UI never hard-codes a control — it reads this table.
   ════════════════════════════════════════════════════════════════════════════════════════════ */

const S = (k, label, min, max, o = {}) => ({ k, label, kind: 'slider', min, max, ...o });
const T = (k, label, o = {}) => ({ k, label, kind: 'switch', ...o });
const V = (k, label, o = {}) => ({ k, label, kind: 'vec3', ...o });
const C = (k, label, o = {}) => ({ k, label, kind: 'color', ...o });
const D = (k, label, options, o = {}) => ({ k, label, kind: 'select', options, ...o });
const R = (k, label, o = {}) => ({ k, label, kind: 'readout', ...o });

const TRANSFORM = (o = {}) => ({
  title: 'Transform',
  props: [
    V('pos', 'Position', { def: [0, 0, 0], step: 0.05 }),
    V('rot', 'Rotation', { def: [0, 0, 0], step: 1, unit: '°' }),
    V('scale', 'Scale', { def: [1, 1, 1], step: 0.02, min: 0.01 }),
    ...(o.extra || []),
  ],
});

const MATERIAL = {
  title: 'Surface',
  props: [
    C('color', 'Albedo', { def: '#c9ccd1' }),
    S('metalness', 'Metallic', 0, 1, { def: 0.1, dec: 2 }),
    S('roughness', 'Roughness', 0, 1, { def: 0.45, dec: 2 }),
    C('emissive', 'Emissive', { def: '#000000' }),
    S('emissiveStrength', 'Emission', 0, 12, { def: 0, dec: 2, unit: '×' }),
    T('castShadow', 'Cast shadow', { def: true }),
  ],
};

export const TYPES = {
  /* ── organisation ──────────────────────────────────────────────────────────────────────── */
  folder: {
    label: 'Folder', icon: 'folder', color: '#c9a24b', cat: 'Scene', noBillboard: true,
    groups: [{
      title: 'Group', props: [
        R('children', 'Contents'),
        C('tint', 'Row tint', { def: '#c9a24b', swatches: true }),
      ],
    }],
  },

  /* ── environment ───────────────────────────────────────────────────────────────────────── */
  sky: {
    label: 'Sky', icon: 'sky', color: '#8fd3ff', cat: 'Environment',
    groups: [
      { title: 'Atmosphere', props: [
        S('rayleigh', 'Rayleigh', 0, 4, { def: 1.35, dec: 2 }),
        S('mie', 'Mie haze', 0, 1, { def: 0.22, dec: 3 }),
        S('mieG', 'Mie forward', 0, 0.98, { def: 0.78, dec: 2 }),
        S('turbidity', 'Turbidity', 1, 20, { def: 3.4, dec: 1 }),
        S('ozone', 'Ozone', 0, 3, { def: 1.0, dec: 2 }),
      ]},
      { title: 'Look', props: [
        C('zenith', 'Zenith tint', { def: '#2f6dd0' }),
        C('horizon', 'Horizon tint', { def: '#9fc4e8' }),
        C('ground', 'Ground bounce', { def: '#14181d' }),
        S('intensity', 'Sky light', 0, 4, { def: 1.0, dec: 2, unit: '×' }),
      ]},
      { title: 'Rendering', props: [
        T('aerial', 'Aerial perspective', { def: true }),
        T('lightsScene', 'Sky lights scene', { def: true }),
      ]},
    ],
  },

  sun: {
    label: 'Sun', icon: 'sun', color: '#ffb14b', cat: 'Environment',
    groups: [
      { title: 'Orbit', props: [
        S('elevation', 'Elevation', -20, 90, { def: 14, dec: 1, unit: '°' }),
        S('azimuth', 'Azimuth', 0, 360, { def: 118, dec: 0, unit: '°' }),
        T('animate', 'Animate', { def: false }),
        S('rate', 'Rate', 1, 600, { def: 120, dec: 0, unit: '×' }),
      ]},
      { title: 'Disc & light', props: [
        S('angular', 'Angular size', 0.1, 4, { def: 0.6, dec: 2, unit: '°' }),
        S('intensity', 'Illuminance', 0, 160, { def: 88, dec: 0, unit: 'klx' }),
        C('tint', 'Tint', { def: '#fff0d4' }),
        S('temperature', 'Temperature', 1600, 12000, { def: 5400, dec: 0, unit: 'K' }),
        T('shadows', 'Cast shadows', { def: true }),
        S('softness', 'Shadow softness', 0, 10, { def: 2.4, dec: 1 }),
      ]},
    ],
  },

  moon: {
    label: 'Moon', icon: 'moon', color: '#b8c4d6', cat: 'Environment',
    groups: [
      { title: 'Orbit', props: [
        S('elevation', 'Elevation', -20, 90, { def: 46, dec: 1, unit: '°' }),
        S('azimuth', 'Azimuth', 0, 360, { def: 292, dec: 0, unit: '°' }),
        S('phase', 'Phase', 0, 1, { def: 0.68, dec: 2 }),
        S('period', 'Period', 1, 60, { def: 27.3, dec: 1, unit: 'd' }),
      ]},
      { title: 'Appearance', props: [
        S('angular', 'Angular size', 0.2, 6, { def: 1.6, dec: 2, unit: '°' }),
        S('brightness', 'Brightness', 0, 4, { def: 1.1, dec: 2, unit: '×' }),
        S('earthshine', 'Earthshine', 0, 1, { def: 0.16, dec: 2 }),
        C('tint', 'Tint', { def: '#d8e2f2' }),
        S('moonlight', 'Moonlight', 0, 2, { def: 0.35, dec: 2, unit: 'lx' }),
      ]},
    ],
  },

  stars: {
    label: 'Stars', icon: 'stars', color: '#cfd8ff', cat: 'Environment',
    groups: [
      { title: 'Field', props: [
        S('density', 'Density', 0, 1, { def: 0.55, dec: 2 }),
        S('brightness', 'Brightness', 0, 3, { def: 1.15, dec: 2, unit: '×' }),
        S('size', 'Point size', 0.4, 4, { def: 1.5, dec: 2, unit: 'px' }),
        S('twinkle', 'Twinkle', 0, 1, { def: 0.45, dec: 2 }),
      ]},
      { title: 'Sphere', props: [
        S('rotation', 'Rotation', 0, 360, { def: 24, dec: 0, unit: '°' }),
        S('drift', 'Sidereal drift', 0, 4, { def: 0.6, dec: 2, unit: '×' }),
        C('warm', 'Warm class', { def: '#ffd7ae' }),
        C('cool', 'Cool class', { def: '#bcd4ff' }),
        T('milkyway', 'Galactic band', { def: true }),
      ]},
    ],
  },

  clouds: {
    label: 'Clouds', icon: 'cloud', color: '#dfe6ee', cat: 'Environment',
    groups: [
      { title: 'Layer', props: [
        S('coverage', 'Coverage', 0, 1, { def: 0.46, dec: 2 }),
        S('density', 'Density', 0, 1, { def: 0.62, dec: 2 }),
        S('altitude', 'Altitude', 20, 400, { def: 130, dec: 0, unit: 'm' }),
        S('scale', 'Feature size', 0.2, 4, { def: 1.0, dec: 2, unit: '×' }),
        S('detail', 'Detail', 0, 1, { def: 0.55, dec: 2 }),
      ]},
      { title: 'Motion & tint', props: [
        S('speed', 'Drift speed', 0, 6, { def: 1.0, dec: 2, unit: '×' }),
        C('tint', 'Base tint', { def: '#eef3f8' }),
        C('shade', 'Shadowed', { def: '#5c6a7c' }),
        T('windLinked', 'Follow wind', { def: true }),
      ]},
    ],
  },

  fog: {
    label: 'Fog', icon: 'fog', color: '#9fb0c0', cat: 'Environment',
    groups: [{
      title: 'Volumetrics', props: [
        T('enabled', 'Enabled', { def: true }),
        S('density', 'Density', 0, 0.06, { def: 0.011, dec: 4 }),
        S('height', 'Height falloff', 1, 200, { def: 42, dec: 0, unit: 'm' }),
        C('color', 'Colour', { def: '#8fa4bb' }),
        S('sunScatter', 'Sun scatter', 0, 2, { def: 0.7, dec: 2 }),
      ],
    }],
  },

  wind: {
    label: 'Wind', icon: 'wind', color: '#89e0c4', cat: 'Environment', noBillboard: true,
    groups: [{
      title: 'Field', props: [
        S('speed', 'Speed', 0, 30, { def: 4.2, dec: 1, unit: 'm/s' }),
        S('direction', 'Direction', 0, 360, { def: 214, dec: 0, unit: '°' }),
        S('gust', 'Gustiness', 0, 1, { def: 0.3, dec: 2 }),
        S('turbulence', 'Turbulence', 0, 1, { def: 0.24, dec: 2 }),
      ],
    }],
  },

  /* ── water ─────────────────────────────────────────────────────────────────────────────── */
  water: {
    label: 'Water', icon: 'water', color: '#4fb6d8', cat: 'Water',
    groups: [
      { title: 'Body', props: [
        S('level', 'Sea level', -8, 6, { def: -0.6, dec: 2, unit: 'm' }),
        S('extent', 'Extent', 50, 4000, { def: 1400, dec: 0, unit: 'm' }),
        C('deep', 'Deep colour', { def: '#06222e' }),
        C('shallow', 'Shallow', { def: '#1d7b8c' }),
        S('clarity', 'Clarity', 0, 1, { def: 0.55, dec: 2 }),
      ]},
      { title: 'Waves', props: [
        S('amplitude', 'Amplitude', 0, 1.4, { def: 0.19, dec: 3, unit: 'm' }),
        S('wavelength', 'Wavelength', 0.5, 30, { def: 7.5, dec: 1, unit: 'm' }),
        S('choppiness', 'Choppiness', 0, 2, { def: 0.85, dec: 2 }),
        S('speed', 'Speed', 0, 4, { def: 1.0, dec: 2, unit: '×' }),
        T('windLinked', 'Follow wind', { def: true }),
      ]},
      { title: 'Surface', props: [
        S('reflectivity', 'Reflectivity', 0, 1, { def: 0.82, dec: 2 }),
        S('roughness', 'Roughness', 0, 1, { def: 0.07, dec: 3 }),
        S('foam', 'Foam', 0, 1, { def: 0.28, dec: 2 }),
        S('specular', 'Sun glint', 0, 4, { def: 1.6, dec: 2, unit: '×' }),
      ]},
    ],
  },

  /* ── terrain & imported assets ─────────────────────────────────────────────────────────── */
  terrain: {
    label: 'Terrain', icon: 'world', color: '#8fb36b', cat: 'Terrain',
    groups: [
      { title: 'Transform', props: [V('pos', 'Position', { def: [0, -2, 0], step: 0.1 })] },
      { title: 'Height Field', props: [
        D('terrainMode', 'Authoring mode', ['Sculpt', 'Erode', 'Smooth', 'Paint', 'Stamp'], { def: 'Sculpt' }),
        S('size', 'World size', 10, 2000, { def: 180, dec: 0, unit: 'm' }),
        S('height', 'Relief', 0, 80, { def: 12, dec: 1, unit: 'm' }),
        S('frequency', 'Feature scale', 0.1, 8, { def: 1.2, dec: 2 }),
        S('seed', 'Seed', 0, 9999, { def: 417, dec: 0 }),
        D('resolution', 'Resolution', ['64', '128', '256', '512', '1024'], { def: '256' }),
        C('lowColor', 'Valley colour', { def: '#263628' }), C('highColor', 'Peak colour', { def: '#88917b' }),
        S('roughness', 'Surface roughness', 0, 1, { def: 0.88, dec: 2 }),
        T('wireframe', 'Wire overlay', { def: false }), T('castShadow', 'Cast shadows', { def: true }),
      ]},
    ],
  },

  asset: {
    label: 'Asset Slot', icon: 'layers', color: '#d4a5ff', cat: 'Assets',
    groups: [
      { title: 'Transform', props: [V('pos', 'Position', { def: [0, 1, 0], step: 0.05 }), V('rot', 'Rotation', { def: [0, 0, 0], step: 1, unit: '°' }), V('scale', 'Scale', { def: [1, 1, 1], step: 0.02 })] },
      { title: 'Asset Source', props: [
        D('assetKind', 'Asset type', ['Auto Detect', '3D Model', 'Image / Texture', 'Audio', 'IES Light', 'HDRI', 'Video', 'Data'], { def: 'Auto Detect' }),
        D('importMode', 'Import as', ['Reference', 'Embed', 'Copy into project'], { def: 'Reference' }),
        T('autoReload', 'Watch source', { def: true }), T('generatePreview', 'Generate preview', { def: true }),
      ]},
    ],
  },

  curve: {
    label: 'Curve', icon: 'motion', color: '#ff79c6', cat: 'Curves',
    groups: [
      TRANSFORM(),
      { title: 'Curve', props: [
        D('curveKind', 'Curve type', ['Bezier', 'Catmull-Rom', 'NURBS', 'Polyline', 'Arc', 'Helix'], { def: 'Bezier' }),
        S('pointCount', 'Control points', 2, 32, { def: 4, dec: 0 }), S('degree', 'Degree', 1, 5, { def: 3, dec: 0 }),
        S('tension', 'Tension', 0, 1, { def: 0.5, dec: 2 }), S('thickness', 'Viewport width', 0.5, 8, { def: 2, dec: 1, unit: 'px' }),
        C('color', 'Curve colour', { def: '#ff79c6' }), T('closed', 'Closed loop', { def: false }), T('showControls', 'Show control polygon', { def: true }), T('adaptive', 'Adaptive sampling', { def: true }),
      ]},
    ],
  },

  /* ── geometry ──────────────────────────────────────────────────────────────────────────── */
  cube:     { label: 'Cube',     icon: 'cube',     color: '#9aa0a6', cat: 'Geometry', mesh: 'box',      groups: [TRANSFORM(), MATERIAL] },
  sphere:   { label: 'Sphere',   icon: 'sphere',   color: '#9aa0a6', cat: 'Geometry', mesh: 'sphere',   groups: [TRANSFORM(), MATERIAL] },
  torus:    { label: 'Torus',    icon: 'torus',    color: '#9aa0a6', cat: 'Geometry', mesh: 'torus',    groups: [TRANSFORM(), MATERIAL] },
  cylinder: { label: 'Cylinder', icon: 'cylinder', color: '#9aa0a6', cat: 'Geometry', mesh: 'cylinder', groups: [TRANSFORM(), MATERIAL] },
  plane:    { label: 'Plane',    icon: 'plane',    color: '#9aa0a6', cat: 'Geometry', mesh: 'plane',    groups: [TRANSFORM(), MATERIAL] },

  /* ── lighting ──────────────────────────────────────────────────────────────────────────── */
  pointlight: {
    label: 'Point Light', icon: 'light', color: '#f5d34b', cat: 'Lighting',
    groups: [
      { title: 'Transform', props: [V('pos', 'Position', { def: [0, 2, 0], step: 0.05 })] },
      { title: 'Emission', props: [
        C('color', 'Colour', { def: '#ffd9a0' }),
        S('intensity', 'Intensity', 0, 60, { def: 14, dec: 1, unit: 'cd' }),
        S('distance', 'Reach', 1, 120, { def: 26, dec: 0, unit: 'm' }),
        S('decay', 'Decay', 0, 4, { def: 2, dec: 2 }),
        T('shadows', 'Cast shadows', { def: false }),
        T('gizmoGlow', 'Show glow', { def: true }),
      ]},
    ],
  },

  spotlight: {
    label: 'Spot Light', icon: 'spot', color: '#f5d34b', cat: 'Lighting',
    groups: [
      { title: 'Transform', props: [
        V('pos', 'Position', { def: [0, 6, 0], step: 0.05 }),
        V('target', 'Aim at', { def: [0, 0, 0], step: 0.05 }),
      ]},
      { title: 'Cone', props: [
        S('angle', 'Cone angle', 2, 80, { def: 26, dec: 1, unit: '°' }),
        S('penumbra', 'Softness', 0, 1, { def: 0.42, dec: 2 }),
        S('intensity', 'Intensity', 0, 200, { def: 62, dec: 0, unit: 'cd' }),
        C('color', 'Colour', { def: '#e8f0ff' }),
        T('shadows', 'Cast shadows', { def: true }),
        T('showCone', 'Draw cone', { def: true }),
      ]},
    ],
  },

  ieslight: {
    label: 'IES / Automotive', icon: 'spot', color: '#f6dc72', cat: 'Lighting',
    groups: [
      { title: 'Transform', props: [
        V('pos', 'Position', { def: [0, 1, 0], step: 0.05 }),
        V('target', 'Aim at', { def: [0, 0.6, -12], step: 0.05 }),
      ]},
      { title: 'Photometry', props: [
        D('profile', 'Distribution', ['ECE Low Beam', 'SAE Low Beam', 'High Beam', 'Fog Lamp', 'Parking Lamp', 'Custom .IES'], { def: 'ECE Low Beam' }),
        S('lumens', 'Luminous flux', 0, 8000, { def: 1650, dec: 0, unit: 'lm' }),
        S('multiplier', 'Profile multiplier', 0, 4, { def: 1, dec: 2, unit: '×' }),
        S('range', 'Photometric range', 1, 250, { def: 120, dec: 0, unit: 'm' }),
        S('cone', 'Field angle', 5, 100, { def: 58, dec: 1, unit: '°' }),
        S('cutoff', 'Cut-off pitch', -5, 5, { def: -1.0, dec: 1, unit: '°' }),
        C('color', 'Colour', { def: '#fff2cf' }),
        S('temperature', 'Temperature', 1800, 12000, { def: 4300, dec: 0, unit: 'K' }),
        T('shadows', 'Cast shadows', { def: true }),
        T('showDistribution', 'Draw distribution', { def: true }),
      ]},
    ],
  },

  arealight: {
    label: 'Rect Area Light', icon: 'plane', color: '#f5d34b', cat: 'Lighting',
    groups: [
      { title: 'Transform', props: [V('pos', 'Position', { def: [0, 3, 0], step: 0.05 }), V('target', 'Aim at', { def: [0, 0, 0], step: 0.05 })] },
      { title: 'Emitter', props: [
        S('width', 'Width', 0.1, 20, { def: 2, dec: 2, unit: 'm' }),
        S('height', 'Height', 0.1, 20, { def: 1, dec: 2, unit: 'm' }),
        S('lumens', 'Luminous flux', 0, 20000, { def: 2400, dec: 0, unit: 'lm' }),
        S('spread', 'Spread', 1, 180, { def: 120, dec: 0, unit: '°' }),
        C('color', 'Colour', { def: '#fff1d6' }),
        T('twoSided', 'Two sided', { def: false }), T('shadows', 'Cast shadows', { def: false }), T('showShape', 'Draw emitter', { def: true }),
      ]},
    ],
  },

  tubelight: {
    label: 'Tube Light', icon: 'light', color: '#f5d34b', cat: 'Lighting',
    groups: [
      { title: 'Transform', props: [V('pos', 'Position', { def: [0, 2, 0], step: 0.05 }), V('rot', 'Rotation', { def: [0, 0, 0], step: 1, unit: '°' })] },
      { title: 'Emitter', props: [
        S('length', 'Length', 0.1, 20, { def: 1.5, dec: 2, unit: 'm' }), S('radius', 'Radius', 0.01, 1, { def: 0.04, dec: 2, unit: 'm' }),
        S('lumens', 'Luminous flux', 0, 12000, { def: 1800, dec: 0, unit: 'lm' }), S('distance', 'Reach', 1, 120, { def: 24, dec: 0, unit: 'm' }),
        C('color', 'Colour', { def: '#e8f2ff' }), S('temperature', 'Temperature', 1800, 12000, { def: 5600, dec: 0, unit: 'K' }),
        T('shadows', 'Cast shadows', { def: false }), T('showShape', 'Draw emitter', { def: true }),
      ]},
    ],
  },

  /* ── cameras ───────────────────────────────────────────────────────────────────────────── */
  camera: {
    label: 'Camera', icon: 'camera', color: '#69c3ff', cat: 'Cameras',
    groups: [
      { title: 'Transform', props: [
        V('pos', 'Position', { def: [8, 3, 10], step: 0.05 }),
        V('lookAt', 'Look at', { def: [0, 1, 0], step: 0.05 }),
      ]},
      { title: 'Lens', props: [
        S('fov', 'Field of view', 12, 120, { def: 46, dec: 0, unit: '°' }),
        S('focus', 'Focus distance', 0.2, 80, { def: 12, dec: 1, unit: 'm' }),
        S('aperture', 'Aperture', 0.7, 22, { def: 2.8, dec: 1, unit: 'f' }),
        S('shutter', 'Shutter', 1, 500, { def: 60, dec: 0, unit: '1/s' }),
      ]},
      { title: 'Framing', props: [
        D('gate', 'Aspect', ['16:9', '2.39:1', '4:3', '1:1'], { def: '16:9' }),
        T('showFrustum', 'Draw frustum', { def: true }),
      ]},
    ],
  },

  cinecamera: {
    label: 'Cinematic Camera', icon: 'camera', color: '#64d2ff', cat: 'Cameras',
    groups: [
      { title: 'Transform', props: [V('pos', 'Position', { def: [10, 3, 12], step: 0.05 }), V('lookAt', 'Look at', { def: [0, 1, 0], step: 0.05 })] },
      { title: 'Cinema Lens', props: [
        S('focal', 'Focal length', 12, 200, { def: 50, dec: 0, unit: 'mm' }), S('sensorWidth', 'Sensor width', 12, 70, { def: 36, dec: 1, unit: 'mm' }),
        S('fov', 'Field of view', 6, 140, { def: 39.6, dec: 1, unit: '°' }), S('focus', 'Focus distance', 0.2, 500, { def: 8, dec: 1, unit: 'm' }),
        S('aperture', 'Aperture', 0.7, 22, { def: 2.8, dec: 1, unit: 'f' }), S('shutterAngle', 'Shutter angle', 1, 360, { def: 180, dec: 0, unit: '°' }),
        S('iso', 'ISO', 25, 12800, { def: 800, dec: 0 }), S('squeeze', 'Anamorphic squeeze', 1, 2, { def: 1, dec: 2, unit: '×' }),
        D('gate', 'Aspect', ['16:9', '2.39:1', '4:3', '1:1'], { def: '2.39:1' }), T('showFrustum', 'Draw frustum', { def: true }),
      ]},
    ],
  },

  playercamera: {
    label: 'Player Camera', icon: 'camera', color: '#69c3ff', cat: 'Cameras',
    groups: [
      { title: 'Transform', props: [V('pos', 'Position', { def: [0, 2.2, 6], step: 0.05 }), V('lookAt', 'Look at', { def: [0, 1.4, 0], step: 0.05 })] },
      { title: 'Player Rig', props: [
        S('fov', 'Field of view', 35, 120, { def: 72, dec: 0, unit: '°' }), S('boom', 'Boom length', 0, 12, { def: 4.2, dec: 1, unit: 'm' }),
        S('shoulder', 'Shoulder offset', -2, 2, { def: 0.45, dec: 2, unit: 'm' }), S('damping', 'Follow damping', 0, 1, { def: 0.72, dec: 2 }),
        S('near', 'Near clip', 0.01, 2, { def: 0.1, dec: 2, unit: 'm' }), S('far', 'Far clip', 10, 10000, { def: 2000, dec: 0, unit: 'm' }),
        T('collision', 'Camera collision', { def: true }), T('input', 'Player input', { def: true }),
        D('gate', 'Aspect', ['16:9', '2.39:1', '4:3', '1:1'], { def: '16:9' }), S('aperture', 'Preview aperture', 0.7, 22, { def: 5.6, dec: 1, unit: 'f' }), T('showFrustum', 'Draw frustum', { def: true }),
      ]},
    ],
  },

  vehiclecamera: {
    label: 'Vehicle Camera', icon: 'camera', color: '#5eead4', cat: 'Cameras',
    groups: [
      { title: 'Transform', props: [V('pos', 'Position', { def: [0, 1.8, 6.5], step: 0.05 }), V('lookAt', 'Look at', { def: [0, 1, 0], step: 0.05 })] },
      { title: 'Vehicle Rig', props: [
        D('mode', 'Mount', ['Chase', 'Bumper', 'Cockpit', 'Hood', 'Orbit Rig'], { def: 'Chase' }), S('fov', 'Field of view', 35, 130, { def: 68, dec: 0, unit: '°' }),
        S('distance', 'Follow distance', 0, 20, { def: 6.5, dec: 1, unit: 'm' }), S('height', 'Mount height', 0.2, 6, { def: 1.8, dec: 1, unit: 'm' }),
        S('lag', 'Velocity lag', 0, 1, { def: 0.35, dec: 2 }), S('lookAhead', 'Look ahead', 0, 50, { def: 12, dec: 0, unit: 'm' }),
        S('shake', 'Road vibration', 0, 1, { def: 0.12, dec: 2 }), T('horizonLock', 'Horizon lock', { def: true }), T('collision', 'Collision sweep', { def: true }),
        D('gate', 'Aspect', ['16:9', '2.39:1', '4:3', '1:1'], { def: '16:9' }), S('aperture', 'Preview aperture', 0.7, 22, { def: 4, dec: 1, unit: 'f' }), T('showFrustum', 'Draw frustum', { def: true }),
      ]},
    ],
  },

  /* ── effects ───────────────────────────────────────────────────────────────────────────── */
  particles: {
    label: 'Particles', icon: 'particles', color: '#ffa8e0', cat: 'Effects',
    groups: [
      { title: 'Emitter', props: [
        V('pos', 'Position', { def: [0, 1.4, 0], step: 0.05 }),
        S('count', 'Count', 0, 4000, { def: 420, dec: 0 }),
        S('radius', 'Spread', 0.5, 40, { def: 7, dec: 1, unit: 'm' }),
        S('height', 'Column', 0.2, 30, { def: 4.2, dec: 1, unit: 'm' }),
      ]},
      { title: 'Look', props: [
        C('color', 'Colour', { def: '#ffd88a' }),
        S('size', 'Size', 0.2, 8, { def: 1.5, dec: 2, unit: 'px' }),
        S('speed', 'Rise speed', 0, 3, { def: 0.5, dec: 2, unit: '×' }),
        S('flicker', 'Flicker', 0, 1, { def: 0.7, dec: 2 }),
      ]},
    ],
  },

  probe: {
    label: 'Reflection Probe', icon: 'probe', color: '#7de0ff', cat: 'Effects',
    groups: [{
      title: 'Capture', props: [
        V('pos', 'Position', { def: [0, 2, 0], step: 0.05 }),
        S('radius', 'Radius', 1, 80, { def: 14, dec: 1, unit: 'm' }),
        S('intensity', 'Intensity', 0, 3, { def: 1, dec: 2, unit: '×' }),
        D('resolution', 'Resolution', ['128', '256', '512', '1024'], { def: '256' }),
        T('realtime', 'Realtime', { def: false }),
        T('showBounds', 'Draw bounds', { def: true }),
      ],
    }],
  },

  audio: {
    label: 'Audio Emitter', icon: 'audio', color: '#b78dff', cat: 'Effects',
    groups: [{
      title: 'Source', props: [
        V('pos', 'Position', { def: [0, 1, 0], step: 0.05 }),
        D('clip', 'Clip', ['Shoreline', 'Wind in pines', 'Night insects', 'Distant thunder'], { def: 'Shoreline' }),
        S('gain', 'Gain', 0, 2, { def: 0.8, dec: 2, unit: '×' }),
        S('radius', 'Falloff', 1, 200, { def: 32, dec: 0, unit: 'm' }),
        T('loop', 'Loop', { def: true }),
        T('spatial', '3D spatialised', { def: true }),
      ],
    }],
  },

  post: {
    label: 'Post Stack', icon: 'post', color: '#ff9d6c', cat: 'Effects', noBillboard: true,
    groups: [
      { title: 'Tone', props: [
        S('exposure', 'Exposure', 0.05, 4, { def: 1.0, dec: 2, unit: 'EV' }),
        D('tonemap', 'Tonemap', ['ACES', 'AgX', 'Filmic', 'Reinhard', 'Linear'], { def: 'ACES' }),
        S('contrast', 'Contrast', 0.5, 2, { def: 1.02, dec: 2 }),
        S('saturation', 'Saturation', 0, 2, { def: 1.0, dec: 2 }),
      ]},
      { title: 'Image', props: [
        T('bloom', 'Bloom', { def: true }),
        S('bloomStrength', 'Bloom strength', 0, 2, { def: 0.42, dec: 2 }),
        S('bloomThreshold', 'Threshold', 0, 2, { def: 0.85, dec: 2 }),
        S('vignette', 'Vignette', 0, 1, { def: 0.32, dec: 2 }),
        S('grain', 'Grain', 0, 1, { def: 0.12, dec: 2 }),
      ]},
    ],
  },
};

/* ── the authored scene ────────────────────────────────────────────────────────────────────── */
let UID = 0;
export const makeNode = (name, type, kids = [], over = {}) => {
  const t = TYPES[type];
  const props = {};
  (t?.groups || []).forEach(g => g.props.forEach(p => {
    if (p.kind === 'readout') return;
    props[p.k] = Array.isArray(p.def) ? p.def.slice() : p.def;
  }));
  const { props: overProps, ...rest } = over;
  return {
    id: ++UID, name, type, kids,
    open: true, vis: true, locked: false, solo: false, dynamic: false, notes: '',
    props: { ...props, ...(overProps || {}) },
    ...rest,
  };
};

const N = makeNode;

export const scene = [
  N('Environment', 'folder', [
    N('Sky Atmosphere', 'sky', [], { locked: true }),
    N('Sun', 'sun', [], { dynamic: true, props: { elevation: 12, azimuth: 108 } }),
    N('Moon', 'moon', [], { dynamic: true }),
    N('Star Field', 'stars', []),
    N('Cloud Layer', 'clouds', []),
    N('Height Fog', 'fog', []),
    N('Wind Field', 'wind', []),
  ], { props: { tint: '#8fd3ff' } }),

  N('Water', 'folder', [
    N('Ocean', 'water', []),
  ], { props: { tint: '#4fb6d8' } }),

  N('Terrain', 'folder', [
    N('Landscape', 'terrain', []),
  ], { props: { tint: '#8fb36b' } }),

  N('Assets', 'folder', [
    N('New Asset', 'asset', []),
  ], { props: { tint: '#d4a5ff' } }),

  N('Curves', 'folder', [
    N('Motion Path', 'curve', [], { props: { curveKind: 'Bezier', color: '#ff79c6' } }),
    N('Vehicle Rail', 'curve', [], { props: { curveKind: 'Catmull-Rom', color: '#5eead4', tension: 0.35 } }),
  ], { props: { tint: '#ff79c6' } }),

  N('Objects', 'folder', [
    N('Platform', 'cylinder', [], {
      locked: true,
      props: { pos: [0, -0.35, 0], scale: [7.4, 0.35, 7.4], color: '#20242a', roughness: 0.85, metalness: 0.05 },
    }),
    N('Anchor Cube', 'cube', [], {
      dynamic: true,
      props: { pos: [-2.4, 0.9, 0.6], rot: [0, 24, 0], scale: [1.6, 1.6, 1.6], color: '#d64f45', roughness: 0.35 },
    }),
    N('Chrome Sphere', 'sphere', [], {
      props: { pos: [1.6, 1.0, -1.4], scale: [1, 1, 1], color: '#f2f4f7', metalness: 1, roughness: 0.08 },
    }),
    N('Signal Torus', 'torus', [], {
      dynamic: true,
      props: { pos: [2.6, 1.5, 1.9], rot: [64, 0, 0], scale: [0.85, 0.85, 0.85], color: '#2f3a44', metalness: 0.7, roughness: 0.25, emissive: '#6c77ff', emissiveStrength: 2.4 },
    }),
    N('Marker Post', 'cylinder', [], {
      props: { pos: [-4.4, 1.2, -2.8], scale: [0.16, 2.4, 0.16], color: '#e8e2d6', roughness: 0.6 },
    }),
    N('Glass Slab', 'cube', [], {
      props: { pos: [4.6, 0.85, -2.2], rot: [0, -18, 0], scale: [0.25, 1.7, 2.6], color: '#8fd3ff', metalness: 0.2, roughness: 0.05, emissive: '#123044', emissiveStrength: 0.4 },
    }),
  ], { props: { tint: '#9aa0a6' } }),

  N('Lighting', 'folder', [
    N('Key Spot', 'spotlight', [], {
      props: { pos: [-6, 8.5, 5], target: [-2.4, 0.9, 0.6], angle: 22, intensity: 90, color: '#fff2dd' },
    }),
    N('Rim Point', 'pointlight', [], {
      props: { pos: [5.2, 2.4, -4.2], color: '#6c77ff', intensity: 22, distance: 30 },
    }),
    N('Fill Point', 'pointlight', [], {
      props: { pos: [-3.5, 1.6, 4.6], color: '#ffb47a', intensity: 10, distance: 22 },
    }),
    N('ECE Low Beam', 'ieslight', [], {
      props: { pos: [-1.1, 0.72, 4.2], target: [-1.1, 0.45, -24], profile: 'ECE Low Beam' },
    }),
    N('Softbox', 'arealight', [], { props: { pos: [3.5, 4.5, 2], target: [0, 1, 0] } }),
    N('Studio Tube', 'tubelight', [], { props: { pos: [-3, 3.2, -2], rot: [0, 0, 20] } }),
  ], { props: { tint: '#f5d34b' } }),

  N('Cameras', 'folder', [
    N('Hero Camera', 'camera', [], { props: { pos: [9.5, 3.4, 9.5], lookAt: [0, 1.1, 0], fov: 42 } }),
    N('Wide Camera', 'camera', [], { props: { pos: [-11, 6, -8], lookAt: [0, 1, 0], fov: 74, gate: '2.39:1' } }),
    N('Picture Car Cinema', 'cinecamera', [], { props: { pos: [8, 2.6, 11], lookAt: [0, 1, 0], focal: 65, fov: 31, focus: 9.5 } }),
    N('Player Follow', 'playercamera', [], { dynamic: true }),
    N('Vehicle Chase', 'vehiclecamera', [], { dynamic: true }),
  ], { props: { tint: '#69c3ff' } }),

  N('Effects', 'folder', [
    N('Fireflies', 'particles', []),
    N('Shore Ambience', 'audio', [], { props: { pos: [-7, 1.2, 6.5], clip: 'Shoreline' } }),
    N('Centre Probe', 'probe', [], { props: { pos: [0, 2.2, 0], radius: 12 } }),
    N('Post Stack', 'post', []),
  ], { props: { tint: '#ff9d6c' } }),
];

/* ── tree helpers ──────────────────────────────────────────────────────────────────────────── */
export const flat = [];
export function reflatten() {
  flat.length = 0;
  const walk = (list, depth, parent) => list.forEach(n => {
    n.depth = depth; n.parent = parent; flat.push(n); walk(n.kids, depth + 1, n);
  });
  walk(scene, 0, null);
  return flat;
}
export const byId = id => flat.find(n => n.id === id) || null;
export const isFolder = n => n.type === 'folder';
export const typeOf = n => TYPES[n.type] || TYPES.cube;
export const leaves = () => flat.filter(n => !isFolder(n));
/* ── isolation (Unreal's "isolate selection", Blender's local view) ────────────────────────────
   A set, not a single id: any number of entities can be isolated together. A node survives
   isolation if it IS isolated, or is an ancestor of one (so its folder row still reads), or a
   descendant of one (so isolating a group keeps the group whole). */
let _isolated = new Set();
const related = (a, b) => {           /* a is b, or an ancestor / descendant of b */
  let c = b; while (c) { if (c === a) return true; c = c.parent; }
  c = a; while (c) { if (c === b) return true; c = c.parent; }
  return false;
};
export const setIsolation = ids => { _isolated = new Set(ids || []); };
export const isolatedIds = () => _isolated;
export const isIsolating = () => _isolated.size > 0;
export const isIsolated = n => _isolated.has(n.id);

/* is this node inside the current isolation set (itself, an ancestor or a descendant of a member) */
export const inIsolation = n => {
  if (!_isolated.size) return true;
  for (const id of _isolated) {
    const s = flat.find(x => x.id === id);
    if (s && related(s, n)) return true;
  }
  return false;
};

export const effectiveVis = n => {
  let cur = n;
  while (cur) { if (!cur.vis) return false; cur = cur.parent; }
  return inIsolation(n);
};
export const CATEGORIES = ['Environment', 'Water', 'Terrain', 'Assets', 'Curves', 'Geometry', 'Lighting', 'Cameras', 'Effects'];
export const catOf = n => typeOf(n).cat || 'Scene';
reflatten();
