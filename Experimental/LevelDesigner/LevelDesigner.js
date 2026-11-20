// Level Designer workspace: Component Viewer styling & ergonomics combined with a Lobby architectural showroom,
// Basic vs. Premium object authoring, 3D Transform Gizmo (Select / Move / Rotate / Scale), interactive ControlVehicle
// configurator hotspots with smooth camera flights, clean UI-free mode, and Sun / Sky / Lights environment controls.
import { $, el, section, panel, rangeField, segmented, segmentedField, toggle, selectField, vec3Field, colorField, textField, rows, metrics, toast, download } from '../Shared/Studio/Ui.js';
import { icon, iconEl, hydrateLevelIcons } from './LevelIcons.js';
import { THREE, renderGeometry, lodMaterialGeometry, assetSlots } from '../Shared/Studio/Render.js';
import { CameraRig, createCameraHud } from '../Shared/Studio/Camera.js';
import { clock, dotFloor, densityColors, addHalo } from '../Shared/Studio/Visuals.js';
import { STEPS, fmt } from '../Shared/GeometryCompile/CompileSteps.js';
import { IMPORT_FORMATS } from '../Shared/GeometryCompile/Engine/ImportFormats.js';
import { ENVIRONMENT_PRESETS, ShowroomEnvironment, kelvinToColor } from './ShowroomEnvironment.js';
import { PROP_CATALOG, buildPropObject } from './ShowroomProps.js';
import { TransformGizmo } from './TransformGizmo.js';

const VEHICLE_SHEET = {
  axleMount: [[1.7274, 1.0475, 0.0914], [1.7274, -1.0475, 0.0914], [-1.6686, 1.0475, 0.0914], [-1.6686, -1.0475, 0.0914]],
  groundOffset: 0.50855 - 0.0914,
};

const STORAGE_KEY = 'Frontier.LevelDesigner.Lobby.v2';
const DEG = Math.PI / 180;

const CHANNELS = [
  { id: 'lit', label: 'Lit', key: '1' },
  { id: 'unlit', label: 'Unlit', key: '2' },
  { id: 'normals', label: 'Normals', key: '3' },
  { id: 'density', label: 'Density', key: '4' },
];

const BODY_SWATCHES = [
  { name: 'Glacier Blue', hex: '#7b97b4' },
  { name: 'Liquid Silver', hex: '#c8ced6' },
  { name: 'Racing Emerald', hex: '#2f5b4a' },
  { name: 'Speed Yellow', hex: '#d8b426' },
  { name: 'Crimson Pearl', hex: '#9e1f28' },
  { name: 'Obsidian Satin', hex: '#1c1f26' },
  { name: 'Alabaster White', hex: '#e6e9ee' },
  { name: 'Frontier Lime', hex: '#a9c718' },
];

const RIM_SWATCHES = [
  { name: 'Machined Silver', hex: '#b6bec9' },
  { name: 'Forged Gunmetal', hex: '#343942' },
  { name: 'Satin Black', hex: '#16181c' },
  { name: 'Satin Bronze', hex: '#a88d63' },
  { name: 'Anodized Cobalt', hex: '#2a62d4' },
];

const CALIPER_SWATCHES = [
  { name: 'Motorsport Red', hex: '#c82e26' },
  { name: 'Racing Yellow', hex: '#e0b422' },
  { name: 'Cobalt Blue', hex: '#2a62d4' },
  { name: 'Acid Green', hex: '#46d267' },
  { name: 'Titanium', hex: '#99a1ad' },
];

const HOTSPOTS = [
  {
    id: 'body',
    label: 'Body Paint',
    subtitle: 'Exterior coachwork & clearcoat',
    localPos: [0.95, 0.88, 0.72],
    camera: { targetOffset: [0.15, 0.65, 0.0], yawDeg: 42, pitchDeg: 14, distance: 6.2, focal: 42 },
  },
  {
    id: 'frontWheel',
    label: 'Wheels & Brakes',
    subtitle: 'Forged rims, tyres & calipers',
    localPos: [1.73, 0.44, 1.12],
    camera: { targetOffset: [1.65, 0.42, 0.45], yawDeg: 24, pitchDeg: 8, distance: 2.45, focal: 50 },
  },
  {
    id: 'wing',
    label: 'Rear Aero Wing',
    subtitle: 'Downforce wing & endplates',
    localPos: [-2.96, 1.32, 0.0],
    camera: { targetOffset: [-2.4, 1.05, 0.0], yawDeg: -130, pitchDeg: 18, distance: 3.1, focal: 45 },
  },
  {
    id: 'headlights',
    label: 'Headlamps & Glass',
    subtitle: 'DRL optics & cabin glazing',
    localPos: [2.85, 0.76, 0.48],
    camera: { targetOffset: [1.85, 0.68, 0.0], yawDeg: 74, pitchDeg: 12, distance: 3.6, focal: 45 },
  },
  {
    id: 'rearQuarter',
    label: 'Rear Wheels & Trim',
    subtitle: 'Rear axle, diffuser & tail lamps',
    localPos: [-1.67, 0.44, -1.12],
    camera: { targetOffset: [-1.55, 0.48, -0.45], yawDeg: -152, pitchDeg: 11, distance: 2.7, focal: 50 },
  },
];

let nextObjectId = 1;
const clone = (v) => JSON.parse(JSON.stringify(v));

function defaultVehicleConfig() {
  return {
    bodyColor: '#7b97b4',
    bodyFinish: 'metallic', // 'metallic' | 'gloss' | 'satin' | 'pearl'
    rimColor: '#b6bec9',
    rimFinish: 'machined', // 'machined' | 'satin' | 'gloss'
    caliperColor: '#c82e26',
    tyreRoughness: 0.76,
    wingEnabled: true,
    wingStyle: 'carbon', // 'carbon' | 'body' | 'forged'
    headlightsOn: true,
    headlightColor: '#ffffff',
    glassTint: 'smoke', // 'smoke' | 'limo' | 'clear'
  };
}

function createLobbyLevel(name = 'Lobby') {
  const preset = ENVIRONMENT_PRESETS.minimalGallery;
  const objects = [
    { id: 'grp-showroom', name: 'Showroom Stage', kind: 'Group', group: 'Showroom', parent: null, locked: true, props: {} },
    { id: 'cyclorama', name: 'Showroom Architecture', kind: 'ShowroomStage', group: 'Showroom', parent: 'grp-showroom', locked: true, props: { visible: true } },
    { id: 'turntable', name: 'Showroom Turntable', kind: 'Turntable', group: 'Showroom', parent: 'grp-showroom', locked: true, props: { visible: true } },

    { id: 'grp-vehicle', name: 'Hero Vehicle', kind: 'Group', group: 'Vehicle', parent: null, locked: true, props: {} },
    {
      id: 'hero-vehicle',
      name: 'ControlVehicle',
      kind: 'ControlVehicle',
      group: 'Vehicle',
      parent: 'grp-vehicle',
      locked: false,
      canMove: true,
      canRotate: true,
      canScale: true,
      props: { location: [0, 0, 0], rotation: [0, 0, 0], scale: [1, 1, 1], visible: true },
    },

    { id: 'grp-env', name: 'Environment', kind: 'Group', group: 'Environment', parent: null, locked: true, props: {} },
    { id: 'env-sun', name: 'Sun Light', kind: 'Sun', group: 'Environment', parent: 'grp-env', locked: true, props: {} },
    { id: 'env-sky', name: 'Sky & Architecture', kind: 'Sky', group: 'Environment', parent: 'grp-env', locked: true, props: {} },

    { id: 'grp-lights', name: 'Showroom Lights', kind: 'Group', group: 'Lighting', parent: null, locked: true, props: {} },
    {
      id: 'light-softbox',
      name: 'Overhead Parabolic Softbox',
      kind: 'StudioSoftbox',
      group: 'Lighting',
      parent: 'grp-lights',
      locked: false,
      canMove: true,
      canRotate: true,
      canScale: false,
      props: {
        location: [0, 4.35, 0],
        rotation: [0, 0, 0],
        scale: [1, 1, 1],
        visible: true,
        enabled: true,
        intensity: 1.9,
        lightColor: '#f8fbff',
        angle: 62,
        range: 20,
        castShadow: false,
      },
    },
    {
      id: 'light-fresnel',
      name: 'Key Fresnel Barn-Door Spot',
      kind: 'FresnelSpot',
      group: 'Lighting',
      parent: 'grp-lights',
      locked: false,
      canMove: true,
      canRotate: true,
      canScale: false,
      props: {
        location: [-4.6, 2.5, -3.4],
        rotation: [0, -35, 0],
        scale: [1, 1, 1],
        visible: true,
        enabled: true,
        intensity: 16,
        lightColor: '#fff8ef',
        angle: 36,
        range: 22,
        castShadow: false,
      },
    },

    { id: 'grp-cameras', name: 'Cameras', kind: 'Group', group: 'Cameras', parent: null, locked: true, props: {} },
    {
      id: 'cam-hero',
      name: 'Showroom Hero Camera',
      kind: 'Camera',
      group: 'Cameras',
      parent: 'grp-cameras',
      locked: false,
      canMove: true,
      canRotate: true,
      canScale: false,
      props: { location: [5.4, 2.05, 5.2], rotation: [0, -135, -14], scale: [1, 1, 1], visible: true, fov: 40 },
    },

    { id: 'grp-custom', name: 'Custom Objects', kind: 'Group', group: 'Custom', parent: null, locked: true, props: {} },
  ];

  return {
    id: `LVL_${Date.now()}_${Math.random().toString(36).slice(2, 6)}`,
    name,
    tier: 'basic', // 'basic' | 'premium'
    environment: {
      presetId: preset.id,
      floorStyle: preset.floorStyle,
      sunEnabled: preset.sunEnabled,
      sunAzimuth: preset.sunAzimuth,
      sunElevation: preset.sunElevation,
      sunIntensity: preset.sunIntensity,
      sunTemperature: preset.sunTemperature,
      skyZenith: preset.skyZenith,
      skyHorizon: preset.skyHorizon,
      wallColor: preset.wallColor,
      floorColor: preset.floorColor,
      floorReflectivity: preset.floorReflectivity,
      floorRoughness: preset.floorRoughness,
      exposure: preset.exposure,
      ambientIntensity: preset.ambientIntensity,
      fogDensity: preset.fogDensity,
      showArchitecture: preset.showArchitecture,
      showTurntable: preset.showTurntable,
      turntableColor: preset.turntableColor,
      turntableRim: preset.turntableRim,
    },
    vehicle: defaultVehicleConfig(),
    objects,
    dirty: false,
  };
}

export function createLevelDesigner({ vehicleParts, assets, openCompile, importFiles, samples }) {
  const canvas = $('Viewport');
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, preserveDrawingBuffer: true });
  renderer.setPixelRatio(Math.min(devicePixelRatio || 1, 2));
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.06;
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFShadowMap;

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(40, 1, 0.02, 400);
  const raycaster = new THREE.Raycaster();

  const showroomEnv = new ShowroomEnvironment(renderer, scene, camera);
  const dotGrid = dotFloor(140);
  dotGrid.position.y = 0.004;
  dotGrid.visible = false; // clean showroom floor by default; toggleable via G / Grid button
  scene.add(dotGrid);

  const contentGroup = new THREE.Group();
  scene.add(contentGroup);

  let levels = [];
  let level = null;
  let selectedId = 'hero-vehicle';
  let channel = 'lit';
  let leftTab = 'outliner';
  let drawerTab = 'environment';
  let cleanMode = false;
  let hotspotsVisible = true;
  let activeHotspotId = null;
  let gizmoMode = 'translate';
  let gizmoEnabled = true;

  const objectNodes = new Map();
  let vehicleGroup = null;
  let selectionHalo = null;
  let isGizmoDragging = false;
  let fpsStats = { fps: 0, ms: 0, frames: 0, last: performance.now() };

  // Camera rig with auto-depth picking on scene meshes
  const pickSurface = (clientX, clientY) => {
    const rect = canvas.getBoundingClientRect();
    const ndc = new THREE.Vector2(
      ((clientX - rect.left) / Math.max(1, rect.width)) * 2 - 1,
      -((clientY - rect.top) / Math.max(1, rect.height)) * 2 + 1
    );
    raycaster.setFromCamera(ndc, camera);
    for (const hit of raycaster.intersectObject(contentGroup, true)) {
      const m = Array.isArray(hit.object.material) ? hit.object.material[0] : hit.object.material;
      if (hit.object.userData.debug || (m && m.visible === false)) continue;
      return hit.point;
    }
    return null;
  };
  const rig = new CameraRig(camera, canvas, { pick: pickSurface });

  const gizmo = new TransformGizmo(camera, canvas, {
    onDragChange(dragging) {
      isGizmoDragging = dragging;
      rig.cancelFlight();
    },
    onChange(nextTransform) {
      const obj = findObj(selectedId);
      if (!obj) return;
      obj.props.location = nextTransform.location;
      obj.props.rotation = nextTransform.rotation;
      obj.props.scale = nextTransform.scale;
      const node = objectNodes.get(obj.id);
      if (node) applyObjTransform(node, obj);
      level.dirty = true;
      syncDetailsTransformInputs(nextTransform);
      renderHeading();
    },
  });

  // Resize observer
  const resize = () => {
    const host = canvas.parentElement;
    const w = Math.max(1, host.clientWidth);
    const h = Math.max(1, host.clientHeight);
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
  };
  new ResizeObserver(resize).observe(canvas.parentElement);
  resize();

  // ------------------------------------------------------------------------------------------------ helpers
  const findObj = (id) => (level ? level.objects.find((o) => o.id === id) || null : null);
  const childrenOf = (id) => (level ? level.objects.filter((o) => o.parent === id) : []);
  const isPremium = () => level && level.tier === 'premium';

  function applyObjTransform(node, obj) {
    const p = obj.props;
    if (!p.location) return;
    node.position.set(...p.location);
    node.rotation.set((p.rotation?.[0] || 0) * DEG, (p.rotation?.[1] || 0) * DEG, (p.rotation?.[2] || 0) * DEG, 'YZX');
    node.scale.set(...(p.scale || [1, 1, 1]));
  }

  function syncDetailsTransformInputs(t) {
    const container = $('Details');
    const sets = container.querySelectorAll('[data-transform-group]');
    for (const grp of sets) {
      const key = grp.dataset.transformGroup;
      const arr = t[key];
      if (!arr) continue;
      const inputs = grp.querySelectorAll('input[type="number"]');
      inputs.forEach((inp, idx) => {
        if (document.activeElement !== inp && arr[idx] !== undefined) {
          inp.value = +Number(arr[idx]).toFixed(3);
        }
      });
    }
  }

  // ------------------------------------------------------------------------------------ vehicle construction
  function buildVehicleMaterials(asset, partKey) {
    const slots = assetSlots(asset);
    const vc = level.vehicle;

    if (channel === 'normals') return slots.map(() => new THREE.MeshNormalMaterial({ side: THREE.DoubleSide }));
    if (channel === 'density') return slots.map(() => new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.55, flatShading: true, side: THREE.DoubleSide }));

    const finishSpec = {
      metallic: { metalness: 0.72, roughness: 0.22, clearcoat: 1.0, clearcoatRoughness: 0.05 },
      gloss: { metalness: 0.10, roughness: 0.14, clearcoat: 1.0, clearcoatRoughness: 0.04 },
      satin: { metalness: 0.44, roughness: 0.44, clearcoat: 0.16, clearcoatRoughness: 0.30 },
      pearl: { metalness: 0.56, roughness: 0.17, clearcoat: 1.0, clearcoatRoughness: 0.03 },
    }[vc.bodyFinish] || { metalness: 0.72, roughness: 0.22, clearcoat: 1.0, clearcoatRoughness: 0.05 };

    const rimSpec = {
      machined: { metalness: 0.92, roughness: 0.18, clearcoat: 0.5 },
      satin: { metalness: 0.76, roughness: 0.38, clearcoat: 0.1 },
      gloss: { metalness: 0.45, roughness: 0.12, clearcoat: 1.0 },
    }[vc.rimFinish] || { metalness: 0.92, roughness: 0.18, clearcoat: 0.5 };

    const glassSpec = {
      smoke: { color: '#141922', opacity: 0.80 },
      limo: { color: '#090b0f', opacity: 0.92 },
      clear: { color: '#46586e', opacity: 0.46 },
    }[vc.glassTint] || { color: '#141922', opacity: 0.80 };

    return slots.map((s) => {
      const f = s.family;
      if (channel === 'unlit') {
        let col = s.color;
        if (f === 6) col = vc.bodyColor;
        if (f === 10) col = vc.rimColor;
        return new THREE.MeshBasicMaterial({ color: col, side: THREE.DoubleSide });
      }

      // Family 6: Body paint (or wing endplates)
      if (f === 6) {
        let col = vc.bodyColor;
        let spec = finishSpec;
        if (partKey === 'wing' && vc.wingStyle === 'carbon') {
          col = '#22262e';
          spec = { metalness: 0.25, roughness: 0.28, clearcoat: 0.85, clearcoatRoughness: 0.08 };
        } else if (partKey === 'wing' && vc.wingStyle === 'forged') {
          col = '#15171c';
          spec = { metalness: 0.55, roughness: 0.40, clearcoat: 0.15, clearcoatRoughness: 0.25 };
        }
        return new THREE.MeshPhysicalMaterial({ color: col, ...spec, envMapIntensity: 1.35, side: THREE.DoubleSide });
      }
      // Family 7: Cabin Glass
      if (f === 7) {
        return new THREE.MeshPhysicalMaterial({
          color: glassSpec.color,
          roughness: 0.04,
          metalness: 0.1,
          clearcoat: 1.0,
          clearcoatRoughness: 0.02,
          transparent: true,
          opacity: glassSpec.opacity,
          envMapIntensity: 1.6,
          side: THREE.DoubleSide,
        });
      }
      // Family 8: Aero trim / Carbon / Wing plane
      if (f === 8) {
        const col = partKey === 'wing' && vc.wingStyle === 'body' ? vc.bodyColor : '#1c2026';
        return new THREE.MeshPhysicalMaterial({
          color: col,
          roughness: 0.28,
          metalness: 0.25,
          clearcoat: 0.7,
          clearcoatRoughness: 0.08,
          envMapIntensity: 1.15,
          side: THREE.DoubleSide,
        });
      }
      // Family 9: Tyre Rubber (or dark inner barrel)
      if (f === 9) {
        const isTyre = partKey === 'tyre';
        return new THREE.MeshPhysicalMaterial({
          color: isTyre ? '#1b1d21' : '#262930',
          roughness: isTyre ? vc.tyreRoughness : 0.42,
          metalness: isTyre ? 0.02 : 0.72,
          envMapIntensity: 0.8,
          side: THREE.DoubleSide,
        });
      }
      // Family 10: Wheel Rim Spokes & Hub / Wing Pylons
      if (f === 10) {
        if (partKey === 'wing') {
          return new THREE.MeshPhysicalMaterial({ color: '#2b303a', roughness: 0.32, metalness: 0.85, side: THREE.DoubleSide });
        }
        return new THREE.MeshPhysicalMaterial({ color: vc.rimColor, ...rimSpec, envMapIntensity: 1.45, side: THREE.DoubleSide });
      }
      // Family 12: Metallic Trim
      if (f === 12) {
        return new THREE.MeshPhysicalMaterial({ color: '#c4ccd6', roughness: 0.24, metalness: 0.85, envMapIntensity: 1.3, side: THREE.DoubleSide });
      }
      // Family 13: Headlights
      if (f === 13) {
        return new THREE.MeshPhysicalMaterial({
          color: '#f5f8ff',
          emissive: vc.headlightColor,
          emissiveIntensity: vc.headlightsOn ? 2.1 : 0.1,
          roughness: 0.08,
          metalness: 0.05,
          side: THREE.DoubleSide,
        });
      }
      // Family 14: Taillights
      if (f === 14) {
        return new THREE.MeshPhysicalMaterial({
          color: '#ff282d',
          emissive: '#ff1f24',
          emissiveIntensity: vc.headlightsOn ? 1.9 : 0.15,
          roughness: 0.10,
          metalness: 0.05,
          side: THREE.DoubleSide,
        });
      }
      return new THREE.MeshPhysicalMaterial({ color: s.color, roughness: s.roughness, metalness: s.metalness, side: THREE.DoubleSide });
    });
  }

  function makeVehiclePartMesh(asset, partKey) {
    let geo = renderGeometry(asset);
    if (channel === 'density') {
      const cloned = geo.clone();
      cloned.clearGroups();
      cloned.setAttribute('color', new THREE.BufferAttribute(densityColors(cloned.attributes.position.array), 3));
      geo = cloned;
    }
    const mats = buildVehicleMaterials(asset, partKey);
    const mesh = new THREE.Mesh(geo, mats);
    mesh.castShadow = true;
    mesh.receiveShadow = true;
    return mesh;
  }

  function buildControlVehicleGroup() {
    const root = new THREE.Group();
    const vc = level.vehicle;

    // Body sitting at groundOffset so tyres contact y = 0 cleanly
    const bodyMount = new THREE.Group();
    bodyMount.position.set(0, VEHICLE_SHEET.groundOffset + 0.012, 0);
    bodyMount.add(makeVehiclePartMesh(vehicleParts.body, 'body'));

    // Rear aero wing
    if (vc.wingEnabled) {
      const wing = makeVehiclePartMesh(vehicleParts.wing, 'wing');
      wing.position.set(-2.9822, 0.8367, 0);
      bodyMount.add(wing);
    }

    // Headlamp subtle throw lights when enabled
    if (vc.headlightsOn && !cleanMode && false) {
      // Kept emissive + showroom lighting clean so floor shadow/reflection stays crisp like ref photos
    }

    root.add(bodyMount);

    // 4 wheels + rims + brake discs & customizable brake calipers
    const caliperMat = new THREE.MeshPhysicalMaterial({
      color: vc.caliperColor,
      roughness: 0.22,
      metalness: 0.35,
      clearcoat: 1.0,
      clearcoatRoughness: 0.05,
    });
    const rotorMat = new THREE.MeshPhysicalMaterial({
      color: '#6e737b',
      roughness: 0.28,
      metalness: 0.92,
    });

    for (const m of VEHICLE_SHEET.axleMount) {
      const wheelWrap = new THREE.Group();
      wheelWrap.position.set(m[0], m[2] + VEHICLE_SHEET.groundOffset + 0.012, -m[1]);
      const flipY = m[1] < 0 ? Math.PI : 0;

      const tyre = makeVehiclePartMesh(vehicleParts.tyre, 'tyre');
      tyre.rotation.y = flipY;
      const rim = makeVehiclePartMesh(vehicleParts.rim, 'rim');
      rim.rotation.y = flipY;
      wheelWrap.add(tyre, rim);

      // Subtle brake rotor + high-contrast caliper visible inside spokes
      const sideSign = m[1] < 0 ? 1 : -1;
      const rotor = new THREE.Mesh(new THREE.CylinderGeometry(0.24, 0.24, 0.032, 28), rotorMat);
      rotor.rotation.x = Math.PI / 2;
      rotor.position.set(0, 0, sideSign * 0.03);
      const caliper = new THREE.Mesh(new THREE.BoxGeometry(0.11, 0.17, 0.075), caliperMat);
      caliper.position.set(m[0] > 0 ? -0.17 : 0.17, 0.07, sideSign * 0.01);
      caliper.rotation.z = m[0] > 0 ? 0.35 : -0.35;
      wheelWrap.add(rotor, caliper);

      root.add(wheelWrap);
    }

    return root;
  }

  // ----------------------------------------------------------------------------------------------- scene build
  function rebuildScene() {
    if (!level) return;
    if (selectionHalo) { selectionHalo.restore(); selectionHalo = null; }
    for (const c of [...contentGroup.children]) {
      contentGroup.remove(c);
      c.traverse((o) => {
        if (o.material) {
          for (const m of Array.isArray(o.material) ? o.material : [o.material]) m.dispose();
        }
      });
    }
    objectNodes.clear();
    vehicleGroup = null;

    // Apply environment (Sun, Sky, Architecture, Reflective Floor, PMREM)
    const cyc = findObj('cyclorama');
    const turn = findObj('turntable');
    const envMerged = {
      ...level.environment,
      showArchitecture: level.environment.showArchitecture !== false && (cyc ? cyc.props.visible !== false : true),
      showTurntable: level.environment.showTurntable && (turn ? turn.props.visible !== false : true),
    };
    showroomEnv.apply(envMerged);

    // Update readout contrast for bright vs dark backdrop
    const isBrightBg = ['minimalGallery', 'glassPavilion', 'horizonTerrace'].includes(level.environment.presetId);
    $('Readout').classList.toggle('light-bg', isBrightBg);
    $('Legend').classList.toggle('light-bg', isBrightBg);

    // Build all scene objects
    for (const obj of level.objects) {
      if (['Group', 'ShowroomStage', 'Turntable', 'Sun', 'Sky'].includes(obj.kind)) continue;
      let node;
      if (obj.kind === 'ControlVehicle') {
        node = buildControlVehicleGroup();
        vehicleGroup = node;
      } else {
        node = buildPropObject(obj, {
          assets,
          editorHelpers: !cleanMode,
          selected: obj.id === selectedId,
        });
      }
      node.userData.objectId = obj.id;
      node.traverse((ch) => { ch.userData.objectId = obj.id; });
      applyObjTransform(node, obj);
      node.visible = obj.props.visible !== false;
      contentGroup.add(node);
      objectNodes.set(obj.id, node);
    }

    updateSelectionAndGizmo();
  }

  function updateSelectionAndGizmo() {
    if (selectionHalo) { selectionHalo.restore(); selectionHalo = null; }
    const obj = findObj(selectedId);
    const node = obj ? objectNodes.get(obj.id) : null;

    if (!cleanMode && node && obj.kind !== 'ControlVehicle') {
      selectionHalo = { restore: addHalo(node) };
    }

    const canTransform = Boolean(
      !cleanMode &&
      gizmoEnabled &&
      gizmoMode !== 'off' &&
      obj &&
      node &&
      obj.props.location &&
      (obj.canMove || obj.canRotate || obj.canScale)
    );

    if (canTransform) {
      gizmo.setMode(gizmoMode);
      gizmo.setTarget({
        canMove: obj.canMove !== false,
        canRotate: obj.canRotate !== false,
        canScale: obj.canScale !== false,
        getTransform: () => ({
          location: obj.props.location.slice(),
          rotation: (obj.props.rotation || [0, 0, 0]).slice(),
          scale: (obj.props.scale || [1, 1, 1]).slice(),
        }),
        setTransform: (next) => {
          obj.props.location = next.location;
          obj.props.rotation = next.rotation;
          obj.props.scale = next.scale;
        },
      });
    } else {
      gizmo.setTarget(null);
    }
  }

  // --------------------------------------------------------------------------------------- hotspot projection
  const hotspotEls = new Map();
  function buildHotspotDOM() {
    const layer = $('HotspotsLayer');
    layer.innerHTML = '';
    hotspotEls.clear();
    for (const hs of HOTSPOTS) {
      const tip = el('span', { class: 'hotspot-tip' }, hs.label);
      const btn = el('button', {
        type: 'button',
        class: 'hotspot-dot',
        'aria-label': `Configure ${hs.label}`,
        onClick: (e) => {
          e.stopPropagation();
          focusHotspot(hs.id);
        },
      }, tip);
      layer.append(btn);
      hotspotEls.set(hs.id, btn);
    }
  }
  buildHotspotDOM();

  function updateHotspotsProjection() {
    const layer = $('HotspotsLayer');
    const show = hotspotsVisible && vehicleGroup && vehicleGroup.visible;
    layer.hidden = !show;
    if (!show) return;

    const w = canvas.clientWidth || 1;
    const h = canvas.clientHeight || 1;
    const v = new THREE.Vector3();
    vehicleGroup.updateMatrixWorld(true);

    for (const hs of HOTSPOTS) {
      const btn = hotspotEls.get(hs.id);
      v.set(...hs.localPos).applyMatrix4(vehicleGroup.matrixWorld);
      v.project(camera);
      const visible = v.z >= -1 && v.z <= 1 && v.x >= -1.05 && v.x <= 1.05 && v.y >= -1.05 && v.y <= 1.05;
      btn.style.display = visible ? '' : 'none';
      if (visible) {
        const px = (v.x * 0.5 + 0.5) * w;
        const py = (-v.y * 0.5 + 0.5) * h;
        btn.style.left = `${px.toFixed(1)}px`;
        btn.style.top = `${py.toFixed(1)}px`;
        btn.classList.toggle('active', hs.id === activeHotspotId);
      }
    }
  }

  function focusHotspot(id) {
    const hs = HOTSPOTS.find((x) => x.id === id);
    if (!hs || !vehicleGroup) return;
    activeHotspotId = id;
    vehicleGroup.updateMatrixWorld(true);

    const target = new THREE.Vector3(...hs.camera.targetOffset).applyMatrix4(vehicleGroup.matrixWorld);
    const carYaw = (findObj('hero-vehicle')?.props.rotation?.[1] || 0) * DEG;
    rig.flyTo({
      target,
      yaw: hs.camera.yawDeg * DEG + carYaw,
      pitch: hs.camera.pitchDeg * DEG,
      distance: hs.camera.distance,
      focal: hs.camera.focal,
    }, { duration: 0.85 });

    if (!cleanMode) {
      selectedId = 'hero-vehicle';
      renderOutliner();
      renderDetails(id);
    }
    renderConfigPopover();
  }

  function renderConfigPopover() {
    const pop = $('ConfigPopover');
    if (!activeHotspotId) {
      pop.hidden = true;
      return;
    }
    const hs = HOTSPOTS.find((x) => x.id === activeHotspotId);
    const vc = level.vehicle;
    pop.hidden = false;
    pop.innerHTML = '';

    const closeBtn = el('button', {
      type: 'button',
      class: 'config-pop-close',
      title: 'Close',
      html: icon('x', 13),
      onClick: () => { activeHotspotId = null; pop.hidden = true; },
    });
    const overviewBtn = el('button', {
      type: 'button',
      class: 'tool',
      onClick: () => frameOverview(),
    }, iconEl('frame', 13), 'Overview');

    pop.append(el('div', { class: 'config-pop-head' },
      el('div', {}, el('b', {}, hs.label), el('span', {}, ' · '), el('small', {}, hs.subtitle)),
      el('span', { class: 'spacer' }),
      overviewBtn,
      closeBtn
    ));

    const refreshVehicle = () => {
      level.dirty = true;
      rebuildScene();
      renderConfigPopover();
      if (!cleanMode && selectedId === 'hero-vehicle') renderDetails(activeHotspotId);
      renderHeading();
    };

    if (hs.id === 'body') {
      const swatches = el('div', { class: 'config-swatches' },
        ...BODY_SWATCHES.map((sw) => el('button', {
          type: 'button',
          class: `swatch-btn ${vc.bodyColor.toLowerCase() === sw.hex.toLowerCase() ? 'active' : ''}`,
          style: { '--swatch': sw.hex },
          title: sw.name,
          onClick: () => { vc.bodyColor = sw.hex; refreshVehicle(); },
        })),
        el('input', {
          type: 'color',
          value: vc.bodyColor,
          title: 'Custom body colour',
          onInput: (e) => { vc.bodyColor = e.target.value; refreshVehicle(); },
        })
      );
      const finishes = segmented({
        cls: 'small',
        options: [
          { id: 'metallic', label: 'Metallic' },
          { id: 'gloss', label: 'Gloss' },
          { id: 'pearl', label: 'Pearl' },
          { id: 'satin', label: 'Satin' },
        ],
        value: vc.bodyFinish,
        onChange: (val) => { vc.bodyFinish = val; refreshVehicle(); },
      });
      pop.append(swatches, el('div', { class: 'config-controls-row' }, finishes));
    } else if (hs.id === 'frontWheel' || hs.id === 'rearQuarter') {
      const rimRow = el('div', { class: 'config-swatches' },
        el('small', { style: { minWidth: '58px', color: '#9a9a9a' } }, 'Rims'),
        ...RIM_SWATCHES.map((sw) => el('button', {
          type: 'button',
          class: `swatch-btn ${vc.rimColor.toLowerCase() === sw.hex.toLowerCase() ? 'active' : ''}`,
          style: { '--swatch': sw.hex },
          title: sw.name,
          onClick: () => { vc.rimColor = sw.hex; refreshVehicle(); },
        }))
      );
      const calRow = el('div', { class: 'config-swatches' },
        el('small', { style: { minWidth: '58px', color: '#9a9a9a' } }, 'Calipers'),
        ...CALIPER_SWATCHES.map((sw) => el('button', {
          type: 'button',
          class: `swatch-btn ${vc.caliperColor.toLowerCase() === sw.hex.toLowerCase() ? 'active' : ''}`,
          style: { '--swatch': sw.hex },
          title: sw.name,
          onClick: () => { vc.caliperColor = sw.hex; refreshVehicle(); },
        }))
      );
      const rimFin = segmented({
        cls: 'small',
        options: [{ id: 'machined', label: 'Machined' }, { id: 'gloss', label: 'Gloss' }, { id: 'satin', label: 'Satin' }],
        value: vc.rimFinish,
        onChange: (val) => { vc.rimFinish = val; refreshVehicle(); },
      });
      pop.append(rimRow, calRow, el('div', { class: 'config-controls-row' }, rimFin));
    } else if (hs.id === 'wing') {
      const wingStyle = segmented({
        cls: 'small',
        options: [{ id: 'carbon', label: 'Exposed Carbon' }, { id: 'body', label: 'Body Matched' }, { id: 'forged', label: 'Forged Matte' }],
        value: vc.wingStyle,
        onChange: (val) => { vc.wingStyle = val; refreshVehicle(); },
      });
      const toggleWing = el('button', {
        type: 'button',
        class: `tool ${vc.wingEnabled ? 'active' : ''}`,
        onClick: () => { vc.wingEnabled = !vc.wingEnabled; refreshVehicle(); },
      }, vc.wingEnabled ? 'Wing Mounted' : 'Wing Removed');
      pop.append(el('div', { class: 'config-controls-row' }, wingStyle, toggleWing));
    } else if (hs.id === 'headlights') {
      const lights = segmented({
        cls: 'small',
        options: [
          { id: '#ffffff', label: '6000K White' },
          { id: '#bfe0ff', label: '8000K Ice' },
          { id: '#ffd078', label: '3000K GT Amber' },
        ],
        value: vc.headlightColor,
        onChange: (val) => { vc.headlightColor = val; refreshVehicle(); },
      });
      const glass = segmented({
        cls: 'small',
        options: [{ id: 'smoke', label: 'Smoke' }, { id: 'limo', label: 'Limo Dark' }, { id: 'clear', label: 'Clear' }],
        value: vc.glassTint,
        onChange: (val) => { vc.glassTint = val; refreshVehicle(); },
      });
      const drlBtn = el('button', {
        type: 'button',
        class: `tool ${vc.headlightsOn ? 'active' : ''}`,
        onClick: () => { vc.headlightsOn = !vc.headlightsOn; refreshVehicle(); },
      }, vc.headlightsOn ? 'DRL On' : 'DRL Off');
      pop.append(el('div', { class: 'config-controls-row' }, drlBtn, lights), el('div', { class: 'config-controls-row' }, glass));
    }
  }

  function frameOverview(animate = true) {
    const carNode = objectNodes.get('hero-vehicle');
    const center = carNode ? carNode.position.clone().add(new THREE.Vector3(0, 0.72, 0)) : new THREE.Vector3(0, 0.72, 0);
    rig.flyTo({
      target: center,
      yaw: 44 * DEG,
      pitch: 14 * DEG,
      distance: 7.4,
      focal: 40,
    }, { duration: animate ? 0.85 : 0 });
  }

  function frameSelected() {
    const obj = findObj(selectedId);
    const node = obj ? objectNodes.get(obj.id) : null;
    if (!node || obj.id === 'hero-vehicle') {
      frameOverview(true);
      return;
    }
    node.updateMatrixWorld(true);
    const box = new THREE.Box3().setFromObject(node);
    if (box.isEmpty()) {
      rig.flyTo({ target: node.position.clone(), distance: 3.4 }, { duration: 0.75 });
    } else {
      rig.frame(box.expandByScalar(0.35), { keepAngle: true, animate: true });
    }
  }

  // ----------------------------------------------------------------------------------------------- UI renders
  function renderAll() {
    renderLevelTabs();
    renderHeading();
    renderOutliner();
    renderContentTray();
    renderDetails();
    renderDrawer();
    updateCleanHud();
  }

  function renderLevelTabs() {
    const strip = $('LevelTabs');
    strip.innerHTML = '';
    for (const lv of levels) {
      const close = levels.length > 1
        ? el('span', { class: 'tab-close', html: icon('x', 11), title: 'Close level', onClick: (e) => { e.stopPropagation(); closeLevel(lv); } })
        : null;
      const tab = el('button', {
        type: 'button',
        class: `document-tab ${lv === level ? 'active' : ''}`,
        onClick: () => openLevel(lv),
      }, iconEl('layers', 13), el('span', {}, lv.name + (lv.dirty ? ' •' : '')), close);
      strip.append(tab);
    }
    const newBtn = el('button', {
      type: 'button',
      class: 'document-tab',
      title: 'Create new blank Lobby variant (Premium)',
      style: { minWidth: '46px', padding: '0 16px' },
      onClick: () => {
        if (!isPremium()) {
          setTier('premium');
          toast('Premium User enabled · created Lobby copy');
        }
        const lv = createLobbyLevel(`Lobby ${levels.length + 1}`);
        lv.tier = 'premium';
        openLevel(lv);
      },
    }, iconEl('plus', 13));
    strip.append(newBtn);
  }

  function renderHeading() {
    if (!level) return;
    const preset = ENVIRONMENT_PRESETS[level.environment.presetId] || ENVIRONMENT_PRESETS.minimalGallery;
    $('LevelCaption').textContent = `${level.name} · ${isPremium() ? 'Premium' : 'Basic'}`;
    $('LevelTitle').textContent = level.name;
    $('LevelSubtitle').textContent = `Showroom · ${preset.name}${level.dirty ? ' · Unsaved changes' : ''}`;
    const badge = $('TierBadge');
    badge.textContent = isPremium() ? 'PREMIUM USER' : 'BASIC LOBBY';
    badge.className = `state-tag ${isPremium() ? 'premium' : 'basic'}`;
    $('CleanEnvLabel').textContent = preset.name;
    $('SaveLevelBtn').classList.toggle('stale', !!level.dirty);
  }

  function computeTriangles() {
    let tris = 0;
    contentGroup.traverse((o) => {
      if (o.isMesh && o.geometry && !o.userData.debug) {
        const idx = o.geometry.index;
        tris += idx ? idx.count / 3 : (o.geometry.attributes.position?.count || 0) / 3;
      }
    });
    return Math.round(tris);
  }

  function renderTierControls() {
    const host = $('TierToggle');
    host.innerHTML = '';
    host.append(segmented({
      cls: 'small',
      options: [
        { id: 'basic', label: 'Basic Lobby' },
        { id: 'premium', label: 'Premium User', icon: 'crown' },
      ],
      value: level.tier,
      onChange: (t) => setTier(t),
    }));
    $('TierNote').innerHTML = isPremium()
      ? '<b>Premium unlocked</b> · Add &amp; customize props, lights &amp; imports'
      : 'Default neutral showroom &amp; car configurator';
    $('BasicTierGate').hidden = true;
  }

  function setTier(tier) {
    if (!level || level.tier === tier) return;
    level.tier = tier;
    renderTierControls();
    renderHeading();
    renderOutliner();
    renderContentTray();
    renderDetails();
    toast(tier === 'premium' ? 'Premium User mode · Custom object creation unlocked' : 'Basic Lobby mode · Curated showroom view');
  }

  function renderOutliner() {
    if (!level) return;
    renderTierControls();
    const visibleObjs = level.objects.filter((o) => o.kind !== 'Group');
    $('StatObjects').textContent = String(visibleObjs.length);
    $('StatTriangles').textContent = fmt(computeTriangles());

    const tree = $('SceneTree');
    tree.innerHTML = '';
    const q = ($('OutlinerSearch').value || '').trim().toLowerCase();

    const groups = level.objects.filter((o) => o.kind === 'Group');
    for (const grp of groups) {
      const children = childrenOf(grp.id).filter((c) => !q || c.name.toLowerCase().includes(q) || c.kind.toLowerCase().includes(q));
      if (q && !children.length && !grp.name.toLowerCase().includes(q)) continue;

      const header = el('div', { class: 'collection' },
        iconEl('folder', 15),
        el('div', {}, el('b', {}, grp.name), el('small', {}, grp.id === 'grp-custom' && !isPremium() ? 'Premium User tier' : grp.group)),
        el('span', { class: 'count' }, String(children.length))
      );
      tree.append(header);

      if (!children.length && grp.id === 'grp-custom') {
        tree.append(el('p', { class: 'tree-empty' }, isPremium()
          ? 'No custom objects yet — click + Add or drag a 3D mesh into the viewport.'
          : 'Switch to Premium User to add custom showroom props, lights, or imported 3D meshes.'));
      }

      for (const c of children) {
        const iconName = c.kind === 'ControlVehicle' ? 'vehicle'
          : c.kind === 'Sun' ? 'sun'
          : c.kind === 'Sky' ? 'sky'
          : c.kind === 'ShowroomStage' ? 'arch'
          : c.kind === 'Turntable' ? 'plinth'
          : ['StudioSoftbox', 'LinearPendant', 'FresnelSpot', 'FloorUplight', 'LightArchRig'].includes(c.kind) ? 'spot'
          : ['CourtyardTree', 'PlanterHedge'].includes(c.kind) ? 'tree'
          : c.kind === 'Camera' ? 'camera'
          : 'cube';

        const canHide = c.props.visible !== undefined;
        const eyeBtn = canHide ? el('button', {
          type: 'button',
          class: `eye ${c.props.visible === false ? 'off' : ''}`,
          title: c.props.visible === false ? 'Show' : 'Hide',
          html: icon(c.props.visible === false ? 'eyeOff' : 'eye', 13),
          onClick: (e) => {
            e.stopPropagation();
            c.props.visible = c.props.visible === false;
            level.dirty = true;
            rebuildScene();
            renderOutliner();
            renderHeading();
          },
        }) : el('span');

        const row = el('button', {
          type: 'button',
          class: `entity comp-row ${c.id === selectedId ? 'active' : ''}`,
          style: { paddingLeft: '22px' },
          onClick: () => selectObject(c.id),
          onDblclick: () => { selectObject(c.id); frameSelected(); },
        },
          el('span', { class: 'tree-caret-sm' }, '·'),
          el('span', { class: 'entity-icon' }, iconEl(iconName, 16)),
          el('div', {},
            el('b', {}, c.name),
            el('small', {}, c.kind === 'ControlVehicle' ? `Config · ${level.vehicle.bodyFinish}` : c.kind)
          ),
          el('span'),
          eyeBtn
        );
        tree.append(row);
      }
    }
  }

  function selectObject(id) {
    selectedId = id;
    renderOutliner();
    renderDetails();
    updateSelectionAndGizmo();
  }

  function renderAddMenu() {
    const list = $('AddList');
    list.innerHTML = '';
    const q = ($('AddSearch').value || '').trim().toLowerCase();
    const groups = [...new Set(PROP_CATALOG.map((p) => p.group))];

    for (const grp of groups) {
      const items = PROP_CATALOG.filter((p) => p.group === grp && (!q || p.label.toLowerCase().includes(q) || p.note.toLowerCase().includes(q)));
      if (!items.length) continue;
      list.append(el('div', { class: 'menu-group' }, grp.toUpperCase()));
      for (const item of items) {
        const locked = !isPremium();
        const btn = el('button', {
          type: 'button',
          class: `menu-item ${locked ? 'locked' : ''}`,
          onClick: () => {
            if (!isPremium()) {
              setTier('premium');
            }
            $('AddMenu').hidden = true;
            spawnProp(item);
          },
        },
          iconEl(item.icon, 16),
          el('span', {}, item.label),
          locked ? el('span', { class: 'tier-pill' }, 'PREMIUM') : el('small', {}, item.group)
        );
        list.append(btn);
      }
    }
  }

  function spawnProp(catalogEntry, extraProps = {}) {
    const count = level.objects.filter((o) => o.kind === catalogEntry.id).length + 1;
    // Place nicely around the showroom perimeter rather than inside the car
    const angle = (level.objects.filter((o) => o.parent === 'grp-custom').length * 0.85) + 0.65;
    const radius = 5.0;
    const isCeilingLight = ['StudioSoftbox', 'LinearPendant'].includes(catalogEntry.id);
    const isSpot = catalogEntry.id === 'FresnelSpot';
    const defaultLoc = isCeilingLight
      ? [+(Math.cos(angle) * 2.8).toFixed(2), 4.05, +(Math.sin(angle) * 2.8).toFixed(2)]
      : isSpot
        ? [+(Math.cos(angle) * 4.2).toFixed(2), 2.15, +(Math.sin(angle) * 4.2).toFixed(2)]
        : catalogEntry.id === 'LightArchRig'
          ? [0, 0, -4.8]
          : catalogEntry.id === 'GarageDoorBay'
            ? [0, 0, -6.2]
            : [+(Math.cos(angle) * radius).toFixed(2), 0, +(Math.sin(angle) * radius).toFixed(2)];

    const obj = {
      id: `OBJ_${nextObjectId++}`,
      name: `${catalogEntry.label} ${count}`,
      kind: catalogEntry.id,
      group: 'Custom',
      parent: 'grp-custom',
      locked: false,
      canMove: true,
      canRotate: true,
      canScale: !['StudioSoftbox', 'LinearPendant', 'FresnelSpot', 'FloorUplight', 'Camera'].includes(catalogEntry.id),
      props: {
        location: defaultLoc,
        rotation: [0, 0, 0],
        scale: [1, 1, 1],
        visible: true,
        color: catalogEntry.id === 'GarageBayBench' ? '#a62626' : '#d8dee6',
        roughness: 0.34,
        metalness: 0.08,
        clearcoat: 0.0,
        accentColor: '#7cb8f0',
        enabled: true,
        intensity: catalogEntry.id === 'FresnelSpot' ? 18 : catalogEntry.id === 'FloorUplight' ? 8 : 1.9,
        lightColor: '#fff8ef',
        angle: catalogEntry.id === 'FresnelSpot' ? 34 : 58,
        range: 20,
        fov: 45,
        castShadow: false,
        ...extraProps,
      },
    };
    level.objects.push(obj);
    level.dirty = true;
    rebuildScene();
    selectObject(obj.id);
    renderHeading();
    toast(`Added ${obj.name} to Lobby`);
  }

  function spawnCompiledAsset(asset) {
    if (!isPremium()) setTier('premium');
    const obj = {
      id: `OBJ_${nextObjectId++}`,
      name: asset.name,
      kind: 'StaticMesh',
      group: 'Custom',
      parent: 'grp-custom',
      locked: false,
      canMove: true,
      canRotate: true,
      canScale: true,
      props: {
        assetId: asset.id,
        location: [4.2, 0.5, 1.4],
        rotation: [0, 0, 0],
        scale: [1, 1, 1],
        visible: true,
        color: '#cfd6df',
        roughness: 0.35,
        metalness: 0.1,
        clearcoat: 0.2,
      },
    };
    level.objects.push(obj);
    level.dirty = true;
    rebuildScene();
    selectObject(obj.id);
    renderHeading();
    toast(`Placed ${asset.name} in Lobby`);
  }

  function duplicateSelected() {
    const obj = findObj(selectedId);
    if (!obj || obj.locked || obj.kind === 'ControlVehicle') return;
    if (!isPremium()) { setTier('premium'); }
    const copy = clone(obj);
    copy.id = `OBJ_${nextObjectId++}`;
    copy.name = `${obj.name} Copy`;
    copy.props.location[0] = +(copy.props.location[0] + 0.9).toFixed(3);
    level.objects.push(copy);
    level.dirty = true;
    rebuildScene();
    selectObject(copy.id);
    renderHeading();
    toast(`Duplicated ${obj.name}`);
  }

  function deleteSelected() {
    const obj = findObj(selectedId);
    if (!obj || obj.locked || obj.kind === 'ControlVehicle') {
      toast('Built-in Lobby stage and Hero Vehicle cannot be deleted (hide via eye icon)');
      return;
    }
    level.objects = level.objects.filter((o) => o.id !== obj.id);
    level.dirty = true;
    selectedId = 'hero-vehicle';
    rebuildScene();
    renderOutliner();
    renderDetails();
    renderHeading();
    toast(`Removed ${obj.name}`);
  }

  // --------------------------------------------------------------------------------------- details inspector
  function renderDetails(focusZone = null) {
    const root = $('Details');
    root.innerHTML = '';
    const obj = findObj(selectedId);
    if (!obj) return;

    root.append(el('div', { class: 'crumbs' }, `Lobby / ${obj.group} / ${obj.name}`));

    // Header + name / duplicate / delete
    const titleNode = obj.locked || obj.kind === 'ControlVehicle'
      ? el('h1', {}, obj.name)
      : (() => {
          const inp = el('input', { type: 'text', class: 'name-input', value: obj.name });
          inp.addEventListener('change', () => {
            if (inp.value.trim()) {
              obj.name = inp.value.trim();
              level.dirty = true;
              renderOutliner();
              renderHeading();
            }
          });
          return inp;
        })();

    const actions = el('div', { class: 'detail-actions' });
    if (!obj.locked && obj.kind !== 'ControlVehicle') {
      actions.append(
        el('button', { type: 'button', class: 'mini', title: 'Duplicate (Ctrl+D)', html: icon('copy', 12), onClick: duplicateSelected }),
        el('button', { type: 'button', class: 'mini', title: 'Delete (Del)', html: icon('trash', 12), onClick: deleteSelected })
      );
    }

    root.append(
      el('div', { class: 'detail-head' },
        el('div', { class: 'detail-glyph' }, iconEl(obj.kind === 'ControlVehicle' ? 'vehicle' : obj.kind === 'Sun' ? 'sun' : obj.kind === 'Sky' ? 'sky' : 'cube', 20)),
        el('div', { class: 'detail-title' }, titleNode, el('div', { class: 'subtitle' }, obj.kind)),
        actions
      )
    );

    // Transform panel (for movable objects)
    if (obj.props.location) {
      const p = obj.props;
      const updateTx = (key, val) => {
        p[key] = val;
        level.dirty = true;
        const node = objectNodes.get(obj.id);
        if (node) applyObjTransform(node, obj);
        renderHeading();
      };
      const locField = vec3Field({ label: 'Location', value: p.location, step: 0.05, unit: 'm', onChange: (v) => updateTx('location', v) });
      locField.dataset.transformGroup = 'location';
      const rotField = vec3Field({ label: 'Rotation', value: p.rotation || [0, 0, 0], step: 1, unit: '°', onChange: (v) => updateTx('rotation', v) });
      rotField.dataset.transformGroup = 'rotation';
      const scaleField = obj.canScale !== false
        ? vec3Field({ label: 'Scale', value: p.scale || [1, 1, 1], step: 0.05, unit: '×', linkable: true, onChange: (v) => updateTx('scale', v) })
        : null;
      if (scaleField) scaleField.dataset.transformGroup = 'scale';

      root.append(section('TRANSFORM', el('button', {
        type: 'button',
        class: 'mini text',
        onClick: () => {
          updateTx('location', [0, 0, 0]);
          updateTx('rotation', [0, 0, 0]);
          if (obj.canScale !== false) updateTx('scale', [1, 1, 1]);
          renderDetails(focusZone);
        },
      }, 'Reset')));
      root.append(panel(locField, rotField, scaleField));
    }

    // Specific panels
    if (obj.kind === 'ControlVehicle') {
      renderVehicleDetails(root, focusZone);
    } else if (obj.kind === 'Sun') {
      renderSunDetails(root);
    } else if (obj.kind === 'Sky') {
      renderSkyDetails(root);
    } else if (obj.kind === 'ShowroomStage' || obj.kind === 'Turntable') {
      renderStageDetails(root, obj);
    } else if (obj.kind === 'Camera') {
      root.append(section('CAMERA'));
      root.append(panel(
        rangeField({
          label: 'Field of view', value: obj.props.fov ?? 40, min: 18, max: 95, step: 1, unit: '°',
          onInput: (val) => { obj.props.fov = val; level.dirty = true; rebuildScene(); },
        }),
        el('button', {
          type: 'button',
          class: 'primary wide',
          onClick: () => {
            const node = objectNodes.get(obj.id);
            if (!node) return;
            node.updateMatrixWorld(true);
            const pos = new THREE.Vector3().setFromMatrixPosition(node.matrixWorld);
            const fwd = new THREE.Vector3(1, 0, 0).transformDirection(node.matrixWorld);
            rig.lookThrough(pos, fwd, obj.props.fov ?? 40);
          },
        }, iconEl('camera', 14), 'Look Through Camera')
      ));
    } else {
      // Lights & Custom Props
      const isLightFixture = ['StudioSoftbox', 'LinearPendant', 'FresnelSpot', 'FloorUplight', 'LightArchRig'].includes(obj.kind);
      if (isLightFixture) {
        renderLightDetails(root, obj);
      }
      renderSurfaceMaterialDetails(root, obj);
    }
  }

  function renderVehicleDetails(root, focusZone) {
    const vc = level.vehicle;
    const refresh = () => {
      level.dirty = true;
      rebuildScene();
      renderDetails(focusZone);
      renderConfigPopover();
      renderHeading();
    };

    root.append(section('CAR CONFIGURATOR · QUICK FOCUS'));
    root.append(panel(
      el('div', { class: 'wheel-list' },
        ...HOTSPOTS.map((hs) => el('button', {
          type: 'button',
          class: `chip-toggle ${activeHotspotId === hs.id ? 'on' : ''}`,
          onClick: () => focusHotspot(hs.id),
        }, iconEl('hotspot', 12), hs.label)),
        el('button', {
          type: 'button',
          class: 'chip-toggle',
          onClick: () => { activeHotspotId = null; frameOverview(true); renderConfigPopover(); renderDetails(); },
        }, iconEl('frame', 12), 'Overview')
      )
    ));

    root.append(section('EXTERIOR PAINT'));
    root.append(panel(
      el('div', { class: 'inspector-swatches' },
        ...BODY_SWATCHES.map((sw) => el('button', {
          type: 'button',
          class: `swatch-btn ${vc.bodyColor.toLowerCase() === sw.hex.toLowerCase() ? 'active' : ''}`,
          style: { '--swatch': sw.hex },
          title: sw.name,
          onClick: () => { vc.bodyColor = sw.hex; refresh(); },
        }))
      ),
      colorField({ label: 'Custom paint hex', value: vc.bodyColor, onChange: (c) => { vc.bodyColor = c; refresh(); } }),
      segmentedField({
        label: 'Paint finish',
        options: [
          { id: 'metallic', label: 'Metallic' },
          { id: 'gloss', label: 'Gloss' },
          { id: 'pearl', label: 'Pearl' },
          { id: 'satin', label: 'Satin' },
        ],
        value: vc.bodyFinish,
        onChange: (val) => { vc.bodyFinish = val; refresh(); },
      })
    ));

    root.append(section('WHEELS, TYRES & CALIPERS'));
    root.append(panel(
      el('small', {}, 'Rim finish colour'),
      el('div', { class: 'inspector-swatches' },
        ...RIM_SWATCHES.map((sw) => el('button', {
          type: 'button',
          class: `swatch-btn ${vc.rimColor.toLowerCase() === sw.hex.toLowerCase() ? 'active' : ''}`,
          style: { '--swatch': sw.hex },
          title: sw.name,
          onClick: () => { vc.rimColor = sw.hex; refresh(); },
        }))
      ),
      segmentedField({
        label: 'Rim alloy finish',
        options: [{ id: 'machined', label: 'Machined' }, { id: 'gloss', label: 'Gloss' }, { id: 'satin', label: 'Satin' }],
        value: vc.rimFinish,
        onChange: (val) => { vc.rimFinish = val; refresh(); },
      }),
      el('small', {}, 'Brake caliper accent'),
      el('div', { class: 'inspector-swatches' },
        ...CALIPER_SWATCHES.map((sw) => el('button', {
          type: 'button',
          class: `swatch-btn ${vc.caliperColor.toLowerCase() === sw.hex.toLowerCase() ? 'active' : ''}`,
          style: { '--swatch': sw.hex },
          title: sw.name,
          onClick: () => { vc.caliperColor = sw.hex; refresh(); },
        }))
      ),
      rangeField({
        label: 'Tyre rubber roughness',
        value: vc.tyreRoughness,
        min: 0.45, max: 0.92, step: 0.02,
        onInput: (val) => { vc.tyreRoughness = val; level.dirty = true; rebuildScene(); },
      })
    ));

    root.append(section('AERO & LIGHTING'));
    root.append(panel(
      toggle({ label: 'Rear aero wing', value: vc.wingEnabled, onChange: (val) => { vc.wingEnabled = val; refresh(); } }),
      segmentedField({
        label: 'Wing finish',
        options: [{ id: 'carbon', label: 'Carbon' }, { id: 'body', label: 'Body Match' }, { id: 'forged', label: 'Forged' }],
        value: vc.wingStyle,
        onChange: (val) => { vc.wingStyle = val; refresh(); },
      }),
      toggle({ label: 'Headlamp & tail DRL', value: vc.headlightsOn, onChange: (val) => { vc.headlightsOn = val; refresh(); } }),
      segmentedField({
        label: 'Headlamp spectrum',
        options: [{ id: '#ffffff', label: '6000K' }, { id: '#bfe0ff', label: '8000K Ice' }, { id: '#ffd078', label: '3000K GT' }],
        value: vc.headlightColor,
        onChange: (val) => { vc.headlightColor = val; refresh(); },
      }),
      segmentedField({
        label: 'Cabin glazing tint',
        options: [{ id: 'smoke', label: 'Smoke' }, { id: 'limo', label: 'Limo' }, { id: 'clear', label: 'Clear' }],
        value: vc.glassTint,
        onChange: (val) => { vc.glassTint = val; refresh(); },
      })
    ));
  }

  function renderSunDetails(root) {
    const env = level.environment;
    const applyEnv = () => { level.dirty = true; rebuildScene(); renderHeading(); };
    root.append(section('DIRECTIONAL SUNLIGHT'));
    root.append(panel(
      toggle({ label: 'Sun enabled', value: env.sunEnabled, onChange: (v) => { env.sunEnabled = v; applyEnv(); } }),
      rangeField({ label: 'Azimuth', value: env.sunAzimuth, min: 0, max: 360, step: 1, unit: '°', onInput: (v) => { env.sunAzimuth = v; applyEnv(); } }),
      rangeField({ label: 'Elevation', value: env.sunElevation, min: 4, max: 88, step: 1, unit: '°', onInput: (v) => { env.sunElevation = v; applyEnv(); } }),
      rangeField({ label: 'Intensity', value: env.sunIntensity, min: 0, max: 6, step: 0.05, unit: 'W/m²', onInput: (v) => { env.sunIntensity = v; applyEnv(); } }),
      rangeField({ label: 'Colour temperature', value: env.sunTemperature, min: 2500, max: 9500, step: 50, unit: 'K', onInput: (v) => { env.sunTemperature = v; applyEnv(); } })
    ));
  }

  function renderSkyDetails(root) {
    const env = level.environment;
    const applyEnv = () => { level.dirty = true; rebuildScene(); renderHeading(); renderDrawer(); };

    root.append(section('SHOWROOM PRESET'));
    root.append(panel(
      selectField({
        label: 'Environment preset',
        options: Object.values(ENVIRONMENT_PRESETS).map((p) => ({ id: p.id, label: p.name })),
        value: env.presetId,
        onChange: (id) => applyEnvironmentPreset(id),
      }),
      el('p', {}, 'Defaults to the neutral architectural daylight cyclorama seen in the showroom references.')
    ));

    root.append(section('SKY & ATMOSPHERE'));
    root.append(panel(
      colorField({ label: 'Sky zenith', value: env.skyZenith, onChange: (c) => { env.skyZenith = c; applyEnv(); } }),
      colorField({ label: 'Sky horizon', value: env.skyHorizon, onChange: (c) => { env.skyHorizon = c; applyEnv(); } }),
      rangeField({ label: 'Ambient bounce', value: env.ambientIntensity, min: 0.2, max: 3.0, step: 0.05, onInput: (v) => { env.ambientIntensity = v; applyEnv(); } }),
      rangeField({ label: 'Exposure (ACES)', value: env.exposure, min: 0.5, max: 1.8, step: 0.02, onInput: (v) => { env.exposure = v; applyEnv(); } }),
      rangeField({ label: 'Distance haze', value: env.fogDensity, min: 0, max: 0.04, step: 0.001, onInput: (v) => { env.fogDensity = v; applyEnv(); } })
    ));

    root.append(section('ARCHITECTURE & FLOOR'));
    root.append(panel(
      toggle({ label: 'Showroom architecture', value: env.showArchitecture !== false, onChange: (v) => { env.showArchitecture = v; applyEnv(); } }),
      toggle({ label: 'Turntable plinth', value: env.showTurntable, onChange: (v) => { env.showTurntable = v; applyEnv(); } }),
      segmentedField({
        label: 'Floor material',
        options: [{ id: 'stone', label: 'Honed Stone' }, { id: 'epoxy', label: 'Garage Epoxy' }],
        value: env.floorStyle || 'stone',
        onChange: (v) => { env.floorStyle = v; applyEnv(); },
      }),
      colorField({ label: 'Architecture colour', value: env.wallColor, onChange: (c) => { env.wallColor = c; applyEnv(); } }),
      colorField({ label: 'Floor tone', value: env.floorColor, onChange: (c) => { env.floorColor = c; applyEnv(); } }),
      rangeField({ label: 'Floor reflectivity', value: env.floorReflectivity, min: 0, max: 0.85, step: 0.02, onInput: (v) => { env.floorReflectivity = v; applyEnv(); } }),
      rangeField({ label: 'Floor roughness', value: env.floorRoughness, min: 0.04, max: 0.65, step: 0.02, onInput: (v) => { env.floorRoughness = v; applyEnv(); } })
    ));
  }

  function renderStageDetails(root, obj) {
    const env = level.environment;
    const applyEnv = () => { level.dirty = true; rebuildScene(); renderHeading(); };
    root.append(section('SHOWROOM ARCHITECTURE'));
    root.append(panel(
      toggle({
        label: 'Visible in Lobby',
        value: obj.props.visible !== false,
        onChange: (v) => { obj.props.visible = v; applyEnv(); renderOutliner(); },
      }),
      obj.kind === 'Turntable'
        ? colorField({ label: 'Plinth colour', value: env.turntableColor, onChange: (c) => { env.turntableColor = c; applyEnv(); } })
        : colorField({ label: 'Wall colour', value: env.wallColor, onChange: (c) => { env.wallColor = c; applyEnv(); } }),
      obj.kind === 'Turntable'
        ? colorField({ label: 'Rim glow accent', value: env.turntableRim, onChange: (c) => { env.turntableRim = c; applyEnv(); } })
        : rangeField({ label: 'Floor reflectivity', value: env.floorReflectivity, min: 0, max: 0.85, step: 0.02, onInput: (v) => { env.floorReflectivity = v; applyEnv(); } })
    ));
  }

  function renderLightDetails(root, obj) {
    const p = obj.props;
    const apply = () => { level.dirty = true; rebuildScene(); renderHeading(); };
    const isSpot = obj.kind === 'FresnelSpot';
    const isSoft = ['StudioSoftbox', 'LinearPendant'].includes(obj.kind);
    root.append(section('LUMINAIRE'));
    root.append(panel(
      toggle({ label: 'Light enabled', value: p.enabled !== false, onChange: (v) => { p.enabled = v; apply(); } }),
      colorField({ label: 'Light colour', value: p.lightColor || '#fff8ef', onChange: (c) => { p.lightColor = c; apply(); } }),
      rangeField({ label: 'Intensity', value: p.intensity ?? 2.0, min: 0, max: isSpot ? 45 : 10, step: 0.1, onInput: (v) => { p.intensity = v; apply(); } }),
      (isSpot || isSoft) ? rangeField({ label: 'Beam angle', value: p.angle ?? 42, min: 12, max: 82, step: 1, unit: '°', onInput: (v) => { p.angle = v; apply(); } }) : null,
      rangeField({ label: 'Attenuation range', value: p.range ?? 20, min: 2, max: 50, step: 0.5, unit: 'm', onInput: (v) => { p.range = v; apply(); } }),
      isSpot ? toggle({ label: 'Cast shadows', value: !!p.castShadow, onChange: (v) => { p.castShadow = v; apply(); } }) : null
    ));
  }

  function renderSurfaceMaterialDetails(root, obj) {
    const p = obj.props;
    const apply = () => { level.dirty = true; rebuildScene(); renderHeading(); };
    root.append(section('SURFACE MATERIAL'));
    root.append(panel(
      colorField({ label: 'Base colour', value: p.color || '#d8dee6', onChange: (c) => { p.color = c; apply(); } }),
      rangeField({ label: 'Roughness', value: p.roughness ?? 0.34, min: 0.04, max: 0.98, step: 0.02, onInput: (v) => { p.roughness = v; apply(); } }),
      rangeField({ label: 'Metallic', value: p.metalness ?? 0.08, min: 0, max: 1, step: 0.02, onInput: (v) => { p.metalness = v; apply(); } }),
      rangeField({ label: 'Clearcoat', value: p.clearcoat ?? 0.0, min: 0, max: 1, step: 0.05, onInput: (v) => { p.clearcoat = v; apply(); } }),
      colorField({ label: 'Accent / Rim colour', value: p.accentColor || '#7cb8f0', onChange: (c) => { p.accentColor = c; apply(); } })
    ));
  }

  function applyEnvironmentPreset(presetId) {
    const p = ENVIRONMENT_PRESETS[presetId];
    if (!p || !level) return;
    level.environment = { ...clone(p), presetId: p.id };
    level.dirty = true;
    rebuildScene();
    renderDetails();
    renderDrawer();
    renderHeading();
    toast(`Environment · ${p.name}`);
  }

  // ------------------------------------------------------------------------------------------ bottom drawer
  function renderDrawer() {
    if (!level) return;
    const body = $('DrawerBody');
    body.innerHTML = '';
    for (const b of document.querySelectorAll('[data-drawer]')) {
      b.classList.toggle('active', b.dataset.drawer === drawerTab);
    }

    if (drawerTab === 'environment') {
      $('DrawerTitle').textContent = 'Showroom Environment Presets';
      const grid = el('div', { class: 'env-cards' });
      for (const preset of Object.values(ENVIRONMENT_PRESETS)) {
        const active = level.environment.presetId === preset.id;
        grid.append(el('button', {
          type: 'button',
          class: `env-card ${active ? 'active' : ''}`,
          onClick: () => applyEnvironmentPreset(preset.id),
        },
          el('div', { class: 'env-card-top' },
            el('span', { class: 'env-swatch', style: { background: preset.swatch } }),
            el('b', {}, preset.name),
            preset.id === 'minimalGallery' ? el('span', { class: 'default-tag' }, 'DEFAULT') : null
          ),
          el('small', {}, preset.subtitle)
        ));
      }
      body.append(grid, el('div', { class: 'drawer-actions' },
        el('button', { type: 'button', class: 'tool', onClick: () => selectObject('env-sun') }, iconEl('sun', 13), 'Edit Sun'),
        el('button', { type: 'button', class: 'tool', onClick: () => selectObject('env-sky') }, iconEl('sky', 13), 'Edit Sky & Floor'),
        el('button', { type: 'button', class: 'tool', onClick: () => { if (!isPremium()) setTier('premium'); spawnProp(PROP_CATALOG.find((x) => x.id === 'StudioSoftbox')); } }, iconEl('plus', 13), 'Add Softbox Fixture'),
        el('button', { type: 'button', class: 'tool', onClick: () => { if (!isPremium()) setTier('premium'); spawnProp(PROP_CATALOG.find((x) => x.id === 'FresnelSpot')); } }, iconEl('plus', 13), 'Add Fresnel Spot')
      ));
      return;
    }

    $('DrawerTitle').textContent = 'ControlVehicle Configurator';
    const vc = level.vehicle;
    const refresh = () => { level.dirty = true; rebuildScene(); renderDrawer(); renderConfigPopover(); renderHeading(); };
    body.append(
      el('div', { class: 'config-swatches', style: { marginBottom: '10px' } },
        el('small', { style: { minWidth: '80px', color: '#9a9a9a' } }, 'Body Paint'),
        ...BODY_SWATCHES.map((sw) => el('button', {
          type: 'button',
          class: `swatch-btn ${vc.bodyColor.toLowerCase() === sw.hex.toLowerCase() ? 'active' : ''}`,
          style: { '--swatch': sw.hex },
          title: sw.name,
          onClick: () => { vc.bodyColor = sw.hex; refresh(); },
        }))
      ),
      el('div', { class: 'config-controls-row' },
        ...HOTSPOTS.map((hs) => el('button', {
          type: 'button',
          class: `tool ${activeHotspotId === hs.id ? 'active' : ''}`,
          onClick: () => focusHotspot(hs.id),
        }, iconEl('hotspot', 12), hs.label)),
        el('button', { type: 'button', class: 'tool', onClick: () => setCleanMode(true) }, iconEl('eye', 12), 'Enter Clean View')
      )
    );
  }

  // ------------------------------------------------------------------------------------------- content tray
  function renderContentTray() {
    const compiled = [...assets.values()].filter((a) => a.out?.geometry);
    $('ContentCount').textContent = String(compiled.length);
    const list = $('ContentList');
    list.innerHTML = '';
    for (const a of compiled) {
      const g = a.out.geometry;
      list.append(el('div', { class: 'content-row' },
        el('div', { class: 'content-thumb' }, a.out.icon ? el('img', { src: a.out.icon, alt: '' }) : iconEl('cube', 20)),
        el('div', { class: 'content-meta' },
          el('b', {}, a.name),
          el('small', {}, `${fmt(g.indices.length / 3)} tris · ${g.bounds.size.map((x) => x.toFixed(2)).join(' × ')} m`),
          el('div', { class: 'chips' }, ...STEPS.filter((s) => s.optional).map((s) => el('span', { class: `chip ${a.steps[s.id].state === 'done' ? 'ok' : 'dim'}` }, s.label)))
        ),
        el('div', { class: 'content-actions' },
          el('button', { type: 'button', class: 'tool', onClick: () => spawnCompiledAsset(a) }, iconEl('plus', 12), 'Place in Lobby'),
          el('button', { type: 'button', class: 'tool', onClick: () => openCompile(a, { recompile: true }) }, iconEl('hammer', 12), 'Compile'),
          el('span')
        )
      ));
    }

    const sList = $('SampleList');
    sList.innerHTML = '';
    for (const s of samples) {
      sList.append(el('button', {
        type: 'button',
        class: 'entity import',
        style: { paddingLeft: '14px' },
        onClick: () => s.open(),
      },
        el('span', { class: 'entity-icon' }, iconEl(s.icon || 'sparkle', 16)),
        el('div', {}, el('b', {}, s.label), el('small', {}, s.note)),
        iconEl('chevron', 14)
      ));
    }
  }

  // --------------------------------------------------------------------------------------- clean view & modes
  function updateCleanHud() {
    $('Studio').classList.toggle('clean-mode', cleanMode);
    $('CleanHud').hidden = !cleanMode;
    $('CleanModeBtn').classList.toggle('active', cleanMode);
    $('CleanDotsBtn').classList.toggle('active', hotspotsVisible);
    $('CleanTurntableBtn').classList.toggle('active', rig.turntable);
  }

  function setCleanMode(enabled) {
    cleanMode = !!enabled;
    if (cleanMode) {
      hotspotsVisible = true;
      $('HotspotsBtn').classList.add('active');
    } else {
      activeHotspotId = null;
      $('ConfigPopover').hidden = true;
    }
    rebuildScene();
    updateCleanHud();
    resize();
  }

  function setGizmoMode(mode) {
    gizmoMode = mode;
    gizmoEnabled = mode !== 'off';
    for (const b of $('GizmoModes').querySelectorAll('button')) {
      b.classList.toggle('active', b.dataset.gizmo === mode);
    }
    $('GizmoToggleBtn').classList.toggle('active', gizmoEnabled);
    updateSelectionAndGizmo();
  }

  function setChannel(id) {
    channel = id;
    for (const b of $('ChannelBar').querySelectorAll('button')) {
      b.classList.toggle('active', b.dataset.value === id);
    }
    const notes = {
      lit: 'Lit · OpenPBR showroom & reflections',
      unlit: 'Unlit · Base colour slots',
      normals: 'Normals · Corner normals',
      density: 'Triangle density · Red dense, blue sparse',
    };
    $('Legend').textContent = notes[id] || '';
    if (level) rebuildScene();
  }

  function saveLevel() {
    try {
      const payload = {
        name: level.name,
        tier: level.tier,
        environment: level.environment,
        vehicle: level.vehicle,
        objects: level.objects,
      };
      localStorage.setItem(STORAGE_KEY, JSON.stringify(payload));
      level.dirty = false;
      renderHeading();
      renderLevelTabs();
      toast('Lobby level saved locally');
    } catch (err) {
      toast(`Could not save: ${err.message}`);
    }
  }

  function openLevel(lv) {
    if (!levels.includes(lv)) levels.push(lv);
    level = lv;
    selectedId = 'hero-vehicle';
    activeHotspotId = null;
    $('ConfigPopover').hidden = true;
    rebuildScene();
    frameOverview(false);
    renderAll();
  }

  function closeLevel(lv) {
    const idx = levels.indexOf(lv);
    if (idx < 0 || levels.length <= 1) return;
    levels.splice(idx, 1);
    if (level === lv) openLevel(levels[Math.max(0, idx - 1)]);
    else renderLevelTabs();
  }

  // ----------------------------------------------------------------------------------------- viewport events
  $('ChannelBar').append(segmented({
    options: CHANNELS.map((c) => ({ id: c.id, label: c.label, title: `${c.label} (${c.key})` })),
    value: channel,
    onChange: setChannel,
  }));

  for (const b of $('GizmoModes').querySelectorAll('button')) {
    b.addEventListener('click', () => setGizmoMode(b.dataset.gizmo));
  }
  $('GizmoToggleBtn').addEventListener('click', () => {
    gizmoEnabled = !gizmoEnabled;
    if (!gizmoEnabled) {
      for (const b of $('GizmoModes').querySelectorAll('button')) b.classList.toggle('active', b.dataset.gizmo === 'off');
    } else {
      if (gizmoMode === 'off') gizmoMode = 'translate';
      for (const b of $('GizmoModes').querySelectorAll('button')) b.classList.toggle('active', b.dataset.gizmo === gizmoMode);
    }
    $('GizmoToggleBtn').classList.toggle('active', gizmoEnabled);
    updateSelectionAndGizmo();
  });

  $('HotspotsBtn').addEventListener('click', () => {
    hotspotsVisible = !hotspotsVisible;
    $('HotspotsBtn').classList.toggle('active', hotspotsVisible);
    if (!hotspotsVisible) { activeHotspotId = null; $('ConfigPopover').hidden = true; }
    updateCleanHud();
  });
  $('FrameBtn').addEventListener('click', frameSelected);
  $('GridBtn').addEventListener('click', () => {
    dotGrid.visible = !dotGrid.visible;
    $('GridBtn').classList.toggle('active', dotGrid.visible);
  });

  $('CleanModeBtn').addEventListener('click', () => setCleanMode(!cleanMode));
  $('ExitCleanBtn').addEventListener('click', () => setCleanMode(false));
  $('CleanDotsBtn').addEventListener('click', () => $('HotspotsBtn').click());
  $('CleanTurntableBtn').addEventListener('click', () => {
    rig.turntable = !rig.turntable;
    rig.idle = 2;
    updateCleanHud();
  });
  $('CleanResetViewBtn').addEventListener('click', () => {
    activeHotspotId = null;
    $('ConfigPopover').hidden = true;
    frameOverview(true);
  });
  $('CleanEnvCycleBtn').addEventListener('click', () => {
    const keys = Object.keys(ENVIRONMENT_PRESETS);
    const next = keys[(keys.indexOf(level.environment.presetId) + 1) % keys.length];
    applyEnvironmentPreset(next);
    updateCleanHud();
  });

  $('SaveLevelBtn').addEventListener('click', saveLevel);
  $('OutlinerSearch').addEventListener('input', renderOutliner);
  $('AddObjectBtn').addEventListener('click', (e) => {
    e.stopPropagation();
    const m = $('AddMenu');
    m.hidden = !m.hidden;
    if (!m.hidden) {
      $('AddSearch').value = '';
      renderAddMenu();
      $('AddSearch').focus();
    }
  });
  $('AddSearch').addEventListener('input', renderAddMenu);
  $('AddSearch').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') $('AddList').querySelector('.menu-item')?.click();
    if (e.key === 'Escape') $('AddMenu').hidden = true;
  });
  document.addEventListener('pointerdown', (e) => {
    if (!e.target.closest('#AddMenu, #AddObjectBtn')) $('AddMenu').hidden = true;
  });
  $('UnlockPremiumBtn').addEventListener('click', () => setTier('premium'));

  for (const t of document.querySelectorAll('[data-left]')) {
    t.addEventListener('click', () => {
      leftTab = t.dataset.left;
      for (const b of document.querySelectorAll('[data-left]')) b.classList.toggle('active', b === t);
      $('LeftOutliner').hidden = leftTab !== 'outliner';
      $('LeftContent').hidden = leftTab !== 'content';
    });
  }

  $('ImportBtn').addEventListener('click', () => $('FileInput').click());
  $('ImportFormats').append(...IMPORT_FORMATS.map((f) => el('span', { class: `chip ${f.reads ? '' : 'dim'}` }, f.label)));

  for (const b of document.querySelectorAll('[data-drawer]')) {
    b.addEventListener('click', () => { drawerTab = b.dataset.drawer; renderDrawer(); });
  }
  $('DrawerHandle').addEventListener('click', () => {
    const d = $('Drawer');
    const openNow = !d.classList.contains('open');
    d.classList.toggle('open', openNow);
    $('DrawerHandle').setAttribute('aria-expanded', String(openNow));
    if (openNow) renderDrawer();
  });

  // Camera HUD (top-notch orientation dial + focal lens + turntable)
  createCameraHud(rig, $('ViewportWell'));

  // Viewport click picking (ignores drags and gizmo interactions)
  let pointerStart = null;
  canvas.addEventListener('pointerdown', (e) => {
    if (isGizmoDragging) return;
    pointerStart = [e.clientX, e.clientY];
  });
  canvas.addEventListener('pointerup', (e) => {
    if (isGizmoDragging || !pointerStart || e.button !== 0) { pointerStart = null; return; }
    const dist = Math.hypot(e.clientX - pointerStart[0], e.clientY - pointerStart[1]);
    pointerStart = null;
    if (dist > 4) return;

    const rect = canvas.getBoundingClientRect();
    const ndc = new THREE.Vector2(
      ((e.clientX - rect.left) / Math.max(1, rect.width)) * 2 - 1,
      -((e.clientY - rect.top) / Math.max(1, rect.height)) * 2 + 1
    );
    raycaster.setFromCamera(ndc, camera);
    const hits = raycaster.intersectObject(contentGroup, true);
    for (const hit of hits) {
      let o = hit.object;
      while (o && !o.userData.objectId) o = o.parent;
      if (o && o.visible !== false) {
        selectObject(o.userData.objectId);
        return;
      }
    }
  });
  canvas.addEventListener('dblclick', frameSelected);

  // Drag & drop files onto viewport
  const well = $('ViewportWell');
  well.addEventListener('dragover', (e) => { e.preventDefault(); well.classList.add('drop'); });
  well.addEventListener('dragleave', () => well.classList.remove('drop'));
  well.addEventListener('drop', (e) => {
    e.preventDefault();
    well.classList.remove('drop');
    if (e.dataTransfer.files.length) importFiles(e.dataTransfer.files);
  });

  // Keyboard shortcuts
  addEventListener('keydown', (e) => {
    if (!$('Compile').hidden || e.target.matches('input, textarea, select')) return;
    if (rig.flying) return;
    const ch = CHANNELS.find((c) => c.key === e.key);
    if (ch) { setChannel(ch.id); return; }
    const k = e.key.toLowerCase();
    if (k === 'q') setGizmoMode('off');
    else if (k === 'w') setGizmoMode('translate');
    else if (k === 'e') setGizmoMode('rotate');
    else if (k === 'r') setGizmoMode('scale');
    else if (k === 'x') $('GizmoToggleBtn').click();
    else if (k === 'h') setCleanMode(!cleanMode);
    else if (k === 'd' && !(e.ctrlKey || e.metaKey)) $('HotspotsBtn').click();
    else if (k === 'f') frameSelected();
    else if (k === 'g') $('GridBtn').click();
    else if (k === 't') { rig.turntable = !rig.turntable; rig.idle = 2; updateCleanHud(); }
    else if (e.key === 'Escape') {
      if (cleanMode) setCleanMode(false);
      else if (activeHotspotId) { activeHotspotId = null; $('ConfigPopover').hidden = true; }
    }
    else if (e.key === 'Delete' || e.key === 'Backspace') { e.preventDefault(); deleteSelected(); }
    else if ((e.ctrlKey || e.metaKey) && k === 'd') { e.preventDefault(); duplicateSelected(); }
    else if ((e.ctrlKey || e.metaKey) && k === 's') { e.preventDefault(); saveLevel(); }
  });

  // ----------------------------------------------------------------------------------------- render loop
  let lastT = performance.now();
  renderer.setAnimationLoop(() => {
    const now = performance.now();
    const dt = Math.min(0.05, (now - lastT) / 1000);
    lastT = now;
    clock.value += dt;

    rig.update(dt);
    if (dotGrid.visible) {
      dotGrid.material.uniforms.uFocus.value.copy(rig.target);
      dotGrid.material.uniforms.uRadius.value = Math.max(8, rig.distance * 2.6);
    }

    // Planar showroom reflection pass
    showroomEnv.renderReflection((mirrorCam) => {
      renderer.render(scene, mirrorCam);
    });

    // Main showroom render + 3D Transform Gizmo overlay pass
    renderer.render(scene, camera);
    if (!cleanMode) gizmo.render(renderer);

    updateHotspotsProjection();

    fpsStats.frames++;
    const elapsed = now - fpsStats.last;
    if (elapsed > 450) {
      fpsStats.fps = (fpsStats.frames * 1000) / elapsed;
      fpsStats.ms = dt * 1000;
      fpsStats.frames = 0;
      fpsStats.last = now;
      const obj = findObj(selectedId);
      const gizmoLabel = gizmoEnabled && gizmoMode !== 'off' ? gizmoMode : 'select';
      $('Readout').textContent =
        `${Math.round(fpsStats.fps)} fps  ${fpsStats.ms.toFixed(1)} ms\n` +
        `level  ${level ? level.name : '—'} · ${isPremium() ? 'premium' : 'basic'}\n` +
        `gizmo  ${gizmoLabel} · sel  ${obj ? obj.name : '—'}`;
    }
  });

  // Boot initial Lobby level (restoring from localStorage if valid, else clean default Neutral Daylight Lobby)
  const initial = createLobbyLevel('Lobby');
  try {
    const saved = localStorage.getItem(STORAGE_KEY);
    if (saved) {
      const parsed = JSON.parse(saved);
      if (parsed && parsed.environment && parsed.vehicle && Array.isArray(parsed.objects)) {
        initial.tier = parsed.tier === 'premium' ? 'premium' : 'basic';
        initial.environment = { ...initial.environment, ...parsed.environment };
        initial.vehicle = { ...initial.vehicle, ...parsed.vehicle };
        initial.objects = parsed.objects;
      }
    }
  } catch { /* ignore invalid saved state */ }

  setChannel('lit');
  hydrateLevelIcons(document);
  openLevel(initial);

  return {
    get level() { return level; },
    refresh() {
      if (!level) return;
      rebuildScene();
      renderAll();
    },
    placeAsset: spawnCompiledAsset,
  };
}
