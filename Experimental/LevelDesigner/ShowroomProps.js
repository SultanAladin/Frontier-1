// High-craft multi-part 3D Lobby props, organic trees/planters, garage bays, structural glass walls,
// architectural panels, and proper studio & architectural lighting fixtures (zero crude bare slab planes).
import * as THREE from '../Shared/Studio/Vendor/three.esm.min.js';
import { glowSprite, beam, cameraModel } from '../Shared/Studio/Visuals.js';
import { stoneSlabTextures, timberSlatTexture } from './ProceduralTextures.js';

export const PROP_CATALOG = [
  // Nature & Courtyard
  { id: 'CourtyardTree', label: 'Courtyard Olive Tree', group: 'Nature', icon: 'sparkle', tier: 'premium', note: 'Multi-branch tree in chamfered architectural stone planter' },
  { id: 'PlanterHedge', label: 'Architectural Planter', group: 'Nature', icon: 'layers', tier: 'premium', note: 'Limestone trough with layered ornamental foliage' },

  // Garage & Motorsport
  { id: 'GarageBayBench', label: 'Tool Chest & Bench', group: 'Garage', icon: 'cube', tier: 'premium', note: 'Multi-drawer cabinet, brushed steel top & pegboard rack' },
  { id: 'TwoPostLift', label: 'Two-Post Hydraulic Lift', group: 'Garage', icon: 'arch', tier: 'premium', note: 'Structural steel columns, crossbeam & articulated arms' },
  { id: 'TyreRack', label: 'Motorsport Tyre Rack', group: 'Garage', icon: 'wheel', tier: 'premium', note: 'Tubular rack holding 4 treaded tyres & alloy hubs' },
  { id: 'GarageDoorBay', label: 'Glazed Garage Door', group: 'Garage', icon: 'grid', tier: 'premium', note: 'Sectional aluminum frame with frosted architectural glass' },
  { id: 'PitKerb', label: 'Track Kerb Barrier', group: 'Garage', icon: 'tag', tier: 'premium', note: 'Chamfered painted motorsport pit kerb' },

  // Architecture & Glass
  { id: 'GlassPartition', label: 'Glass Curtain Wall', group: 'Architecture', icon: 'viewport', tier: 'premium', note: 'Structural low-iron glass panel with dark mullions' },
  { id: 'ArchWallPanel', label: 'Architectural Wall', group: 'Architecture', icon: 'layers', tier: 'premium', note: 'Board-formed stone/plaster bay with shadow reveals' },
  { id: 'DisplayPlinth', label: 'Showroom Plinth', group: 'Architecture', icon: 'plinth', tier: 'premium', note: 'Stepped podium with brushed rim & recessed light cove' },
  { id: 'LoungeSofa', label: 'Gallery Lounge Set', group: 'Architecture', icon: 'cube', tier: 'premium', note: 'Low leather sofa, steel base & glass coffee table' },
  { id: 'SpecKiosk', label: 'Vehicle Spec Kiosk', group: 'Architecture', icon: 'tag', tier: 'premium', note: 'Machined aluminum & glass telemetry lectern' },
  { id: 'BollardChain', label: 'Showroom Stanchions', group: 'Architecture', icon: 'capsule', tier: 'premium', note: 'Weighted anodized posts with tensioned cord' },

  // Proper Lighting Fixtures (no crude thin slabs)
  { id: 'StudioSoftbox', label: 'Studio Parabolic Softbox', group: 'Lighting', icon: 'spot', tier: 'premium', note: 'Octagonal hood, silver liner, louver grid, yoke & tripod' },
  { id: 'LinearPendant', label: 'Linear Light Pendant', group: 'Lighting', icon: 'bulb', tier: 'premium', note: 'Extruded aluminum channel, suspension cables & lens' },
  { id: 'FresnelSpot', label: 'Fresnel Barn-Door Spot', group: 'Lighting', icon: 'spot', tier: 'premium', note: 'Ribbed housing, 4-leaf barn doors, Fresnel lens & stand' },
  { id: 'FloorUplight', label: 'Architectural Uplight', group: 'Lighting', icon: 'bulb', tier: 'premium', note: 'Machined cylinder housing, bezel & grazing wall wash' },
  { id: 'LightArchRig', label: 'Truss Light Arch', group: 'Lighting', icon: 'arch', tier: 'premium', note: 'Aluminum box-truss arch with suspended studio lamps' },

  // Look-Dev & Cameras
  { id: 'LookDevOrb', label: 'Look-Dev Specimen', group: 'Look-Dev', icon: 'sphere', tier: 'premium', note: 'Brushed pedestal with clearcoat reference sphere' },
  { id: 'Camera', label: 'Showcase Camera', group: 'Cameras', icon: 'camera', tier: 'premium', note: 'Cinema camera rig with lens hood & viewfinder' },
];

export function createSurfaceMaterial({ color = '#d6dce4', roughness = 0.38, metalness = 0.06, clearcoat = 0.0, emissive = '#000000', emissiveIntensity = 0, map = null } = {}) {
  return new THREE.MeshPhysicalMaterial({
    color,
    roughness,
    metalness,
    clearcoat,
    clearcoatRoughness: 0.08,
    emissive,
    emissiveIntensity,
    map,
    envMapIntensity: 1.2,
  });
}

export function createArchitecturalGlass(tint = '#dbe8f0', opacity = 0.24) {
  return new THREE.MeshPhysicalMaterial({
    color: tint,
    roughness: 0.04,
    metalness: 0.05,
    clearcoat: 1.0,
    clearcoatRoughness: 0.03,
    reflectivity: 0.95,
    transparent: true,
    opacity,
    depthWrite: false,
    envMapIntensity: 1.8,
    side: THREE.DoubleSide,
  });
}

function addPart(parent, geo, mat, pos = [0, 0, 0], rot = [0, 0, 0], cast = true) {
  const m = new THREE.Mesh(geo, mat);
  m.position.set(...pos);
  m.rotation.set(...rot);
  m.castShadow = cast;
  m.receiveShadow = true;
  parent.add(m);
  return m;
}

export function buildPropObject(record, { assets = null, editorHelpers = true, selected = false } = {}) {
  const g = new THREE.Group();
  const p = record.props || {};
  const primaryMat = createSurfaceMaterial({
    color: p.color || '#d5dce3',
    roughness: p.roughness ?? 0.36,
    metalness: p.metalness ?? 0.08,
    clearcoat: p.clearcoat ?? 0.05,
  });
  const darkAnodized = createSurfaceMaterial({ color: '#1b1e24', roughness: 0.34, metalness: 0.85 });
  const brushedSteel = createSurfaceMaterial({ color: '#aeb6c0', roughness: 0.24, metalness: 0.92 });
  const rubberMat = createSurfaceMaterial({ color: '#181a1e', roughness: 0.82, metalness: 0.02 });
  const accentMat = new THREE.MeshBasicMaterial({ color: p.accentColor || '#8ec4f8' });

  switch (record.kind) {
    // ------------------------------------------------------------------ NATURE & COURTYARD
    case 'CourtyardTree': {
      const stoneTex = stoneSlabTextures('#cfd3d8', [2, 2]).map;
      const planterMat = createSurfaceMaterial({ color: p.color || '#cfd3d8', roughness: 0.72, metalness: 0.02, map: stoneTex });
      const soilMat = createSurfaceMaterial({ color: '#2b241d', roughness: 0.95, metalness: 0 });
      const barkMat = createSurfaceMaterial({ color: '#5c4b3b', roughness: 0.88, metalness: 0 });
      const leafMat1 = createSurfaceMaterial({ color: p.accentColor || '#5e7652', roughness: 0.74, metalness: 0 });
      const leafMat2 = createSurfaceMaterial({ color: '#758e67', roughness: 0.70, metalness: 0 });

      // Chamfered architectural planter box + rim + soil bed
      addPart(g, new THREE.BoxGeometry(1.16, 0.44, 1.16), planterMat, [0, 0.22, 0]);
      addPart(g, new THREE.BoxGeometry(1.22, 0.05, 1.22), planterMat, [0, 0.45, 0]);
      addPart(g, new THREE.BoxGeometry(1.04, 0.04, 1.04), darkAnodized, [0, 0.46, 0], [0, 0, 0], false);
      addPart(g, new THREE.BoxGeometry(0.98, 0.03, 0.98), soilMat, [0, 0.47, 0], [0, 0, 0], false);

      // Tapered trunk + 4 sculpted main limbs
      const trunkCurve = new THREE.CatmullRomCurve3([
        new THREE.Vector3(0, 0.45, 0),
        new THREE.Vector3(0.03, 1.05, -0.02),
        new THREE.Vector3(-0.02, 1.65, 0.04),
      ]);
      addPart(g, new THREE.TubeGeometry(trunkCurve, 12, 0.075, 10, false), barkMat);

      const limbs = [
        [[0.0, 1.35, 0.0], [0.35, 1.95, 0.28], 0.038],
        [[0.0, 1.45, 0.0], [-0.38, 2.05, 0.22], 0.036],
        [[0.0, 1.50, 0.0], [0.18, 2.15, -0.36], 0.035],
        [[0.0, 1.58, 0.0], [-0.22, 2.25, -0.24], 0.032],
      ];
      for (const [a, b, r] of limbs) {
        const c = new THREE.CatmullRomCurve3([new THREE.Vector3(...a), new THREE.Vector3(...b)]);
        addPart(g, new THREE.TubeGeometry(c, 8, r, 8, false), barkMat);
      }

      // Layered organic foliage clusters
      const clusters = [
        [0.0, 2.38, 0.0, 0.56, 0.42, 0.56, leafMat1],
        [0.42, 2.05, 0.30, 0.44, 0.34, 0.42, leafMat2],
        [-0.44, 2.12, 0.24, 0.42, 0.32, 0.42, leafMat1],
        [0.24, 2.24, -0.38, 0.40, 0.32, 0.40, leafMat2],
        [-0.25, 2.32, -0.26, 0.38, 0.30, 0.38, leafMat1],
        [0.12, 2.62, 0.08, 0.36, 0.28, 0.36, leafMat2],
      ];
      for (const [x, y, z, sx, sy, sz, mat] of clusters) {
        const canopy = addPart(g, new THREE.IcosahedronGeometry(1, 2), mat, [x, y, z]);
        canopy.scale.set(sx, sy, sz);
      }
      break;
    }

    case 'PlanterHedge': {
      const troughMat = createSurfaceMaterial({ color: p.color || '#ced4dc', roughness: 0.68, metalness: 0.04 });
      const foliageMat = createSurfaceMaterial({ color: p.accentColor || '#4f6d48', roughness: 0.78, metalness: 0 });
      addPart(g, new THREE.BoxGeometry(2.2, 0.46, 0.62), troughMat, [0, 0.23, 0]);
      addPart(g, new THREE.BoxGeometry(2.26, 0.05, 0.68), troughMat, [0, 0.47, 0]);
      for (let i = -3; i <= 3; i++) {
        const bush = addPart(g, new THREE.IcosahedronGeometry(0.28 + ((i * 7) & 1) * 0.05, 2), foliageMat, [i * 0.28, 0.68, (i % 2) * 0.05]);
        bush.scale.set(1.1, 0.88, 0.9);
      }
      break;
    }

    // ------------------------------------------------------------------ GARAGE & MOTORSPORT
    case 'GarageBayBench': {
      const cabMat = createSurfaceMaterial({ color: p.color || '#2b323d', roughness: 0.34, metalness: 0.65 });
      const topMat = createSurfaceMaterial({ color: '#9ca3ad', roughness: 0.25, metalness: 0.85 });
      const accentCab = createSurfaceMaterial({ color: p.accentColor || '#c83428', roughness: 0.32, metalness: 0.45 });

      // Main cabinet frame & heavy steel countertop
      addPart(g, new THREE.BoxGeometry(2.4, 0.92, 0.68), darkAnodized, [0, 0.46, 0]);
      addPart(g, new THREE.BoxGeometry(2.46, 0.05, 0.72), topMat, [0, 0.945, 0]);

      // 2 banks of drawers with brushed pulls
      for (const bx of [-0.6, 0.6]) {
        for (let row = 0; row < 4; row++) {
          const y = 0.18 + row * 0.185;
          addPart(g, new THREE.BoxGeometry(1.06, 0.155, 0.04), row === 0 ? accentCab : cabMat, [bx, y, 0.345]);
          addPart(g, new THREE.BoxGeometry(0.68, 0.018, 0.025), brushedSteel, [bx, y + 0.04, 0.37]);
        }
      }
      // Pegboard backsplash + overhead wall cabinet
      addPart(g, new THREE.BoxGeometry(2.4, 0.82, 0.04), darkAnodized, [0, 1.38, -0.32]);
      addPart(g, new THREE.BoxGeometry(2.4, 0.48, 0.38), cabMat, [0, 2.02, -0.15]);
      for (const ox of [-0.78, 0, 0.78]) {
        addPart(g, new THREE.BoxGeometry(0.74, 0.42, 0.03), accentCab, [ox, 2.02, 0.045]);
      }
      break;
    }

    case 'TwoPostLift': {
      const postMat = createSurfaceMaterial({ color: p.color || '#2d3440', roughness: 0.36, metalness: 0.75 });
      const armMat = createSurfaceMaterial({ color: p.accentColor || '#d69e2e', roughness: 0.32, metalness: 0.6 });
      for (const sx of [-1.62, 1.62]) {
        // Base plate + H-column + hydraulic carriage
        addPart(g, new THREE.BoxGeometry(0.42, 0.04, 0.56), darkAnodized, [sx, 0.02, 0]);
        addPart(g, new THREE.BoxGeometry(0.18, 3.65, 0.24), postMat, [sx, 1.84, 0]);
        addPart(g, new THREE.BoxGeometry(0.24, 0.72, 0.30), armMat, [sx, 0.85, 0]);
        const dir = sx < 0 ? 1 : -1;
        for (const az of [-0.28, 0.28]) {
          addPart(g, new THREE.BoxGeometry(0.75, 0.08, 0.09), armMat, [sx + dir * 0.42, 0.54, az]);
          addPart(g, new THREE.CylinderGeometry(0.08, 0.08, 0.04, 16), rubberMat, [sx + dir * 0.74, 0.60, az]);
        }
      }
      // Overhead crossbeam
      addPart(g, new THREE.BoxGeometry(3.52, 0.16, 0.22), postMat, [0, 3.62, 0]);
      break;
    }

    case 'TyreRack': {
      const rackMat = createSurfaceMaterial({ color: p.color || '#252930', roughness: 0.38, metalness: 0.82 });
      const hubMat = createSurfaceMaterial({ color: p.accentColor || '#b8c2ce', roughness: 0.22, metalness: 0.92 });
      // Side A-frames + horizontal rails
      for (const sx of [-0.95, 0.95]) {
        addPart(g, new THREE.BoxGeometry(0.06, 1.45, 0.06), rackMat, [sx, 0.725, -0.32]);
        addPart(g, new THREE.BoxGeometry(0.06, 1.45, 0.06), rackMat, [sx, 0.725, 0.32]);
        addPart(g, new THREE.BoxGeometry(0.06, 0.06, 0.70), rackMat, [sx, 0.04, 0]);
        addPart(g, new THREE.BoxGeometry(0.06, 0.06, 0.70), rackMat, [sx, 1.42, 0]);
      }
      for (const rz of [-0.24, 0.24]) {
        addPart(g, new THREE.CylinderGeometry(0.025, 0.025, 1.96, 12), rackMat, [0, 0.46, rz], [0, 0, Math.PI / 2]);
      }
      // 4 sculpted motorsport tyres on the rack
      for (let i = 0; i < 4; i++) {
        const tx = -0.58 + i * 0.385;
        const t = addPart(g, new THREE.TorusGeometry(0.28, 0.11, 16, 32), rubberMat, [tx, 0.76, 0], [0, Math.PI / 2, 0]);
        t.castShadow = true;
        addPart(g, new THREE.CylinderGeometry(0.19, 0.19, 0.18, 20), hubMat, [tx, 0.76, 0], [0, 0, Math.PI / 2]);
      }
      break;
    }

    case 'GarageDoorBay': {
      const frameMat = createSurfaceMaterial({ color: p.color || '#262a32', roughness: 0.36, metalness: 0.82 });
      const frostedGlass = createArchitecturalGlass('#dde6ee', 0.42);
      frostedGlass.roughness = 0.28;
      // Outer portal + vertical tracks
      addPart(g, new THREE.BoxGeometry(0.14, 3.2, 0.18), frameMat, [-2.2, 1.6, 0]);
      addPart(g, new THREE.BoxGeometry(0.14, 3.2, 0.18), frameMat, [2.2, 1.6, 0]);
      addPart(g, new THREE.BoxGeometry(4.54, 0.16, 0.22), frameMat, [0, 3.22, 0]);
      // 4 glazed sectional horizontal panels
      for (let row = 0; row < 4; row++) {
        const y = 0.42 + row * 0.76;
        addPart(g, new THREE.BoxGeometry(4.24, 0.07, 0.08), frameMat, [0, y - 0.34, 0]);
        addPart(g, new THREE.BoxGeometry(4.24, 0.07, 0.08), frameMat, [0, y + 0.34, 0]);
        for (let col = -1; col <= 1; col++) {
          addPart(g, new THREE.BoxGeometry(1.32, 0.62, 0.024), frostedGlass, [col * 1.40, y, 0], [0, 0, 0], false);
        }
        for (const vx of [-2.08, -0.70, 0.70, 2.08]) {
          addPart(g, new THREE.BoxGeometry(0.07, 0.74, 0.08), frameMat, [vx, y, 0]);
        }
      }
      break;
    }

    case 'PitKerb': {
      const whiteMat = createSurfaceMaterial({ color: '#e3e6ea', roughness: 0.65, metalness: 0.02 });
      const redMat = createSurfaceMaterial({ color: p.accentColor || '#c83428', roughness: 0.65, metalness: 0.02 });
      for (let i = -2; i <= 2; i++) {
        const seg = addPart(g, new THREE.BoxGeometry(0.76, 0.14, 0.62), (i & 1) ? whiteMat : redMat, [i * 0.78, 0.07, 0]);
        seg.rotation.x = -0.14;
      }
      break;
    }

    // ------------------------------------------------------------------ ARCHITECTURE & GLASS
    case 'GlassPartition': {
      const mullionMat = createSurfaceMaterial({ color: p.color || '#1c2026', roughness: 0.30, metalness: 0.88 });
      const glassMat = createArchitecturalGlass(p.accentColor || '#d8e8f2', 0.24);
      // Top & bottom U-channels + 3 vertical mullions + 3 clear low-iron glass bays
      addPart(g, new THREE.BoxGeometry(4.6, 0.08, 0.12), mullionMat, [0, 0.04, 0]);
      addPart(g, new THREE.BoxGeometry(4.6, 0.08, 0.12), mullionMat, [0, 3.06, 0]);
      for (const mx of [-2.26, -0.75, 0.75, 2.26]) {
        addPart(g, new THREE.BoxGeometry(0.06, 3.10, 0.10), mullionMat, [mx, 1.55, 0]);
      }
      addPart(g, new THREE.BoxGeometry(4.46, 2.94, 0.025), glassMat, [0, 1.55, 0], [0, 0, 0], false);
      break;
    }

    case 'ArchWallPanel': {
      const wallMat = createSurfaceMaterial({ color: p.color || '#d5dce3', roughness: 0.82, metalness: 0.02 });
      // Base plinth shadow reveal + main structural monolith + top coping
      addPart(g, new THREE.BoxGeometry(4.2, 0.10, 0.36), darkAnodized, [0, 0.05, 0]);
      addPart(g, new THREE.BoxGeometry(4.2, 3.25, 0.38), wallMat, [0, 1.74, 0]);
      addPart(g, new THREE.BoxGeometry(4.26, 0.06, 0.42), brushedSteel, [0, 3.40, 0]);
      // Recessed vertical architectural shadow seams
      for (const sx of [-1.4, 0, 1.4]) {
        addPart(g, new THREE.BoxGeometry(0.03, 3.22, 0.40), darkAnodized, [sx, 1.74, 0], [0, 0, 0], false);
      }
      break;
    }

    case 'DisplayPlinth': {
      const tier1 = addPart(g, new THREE.CylinderGeometry(1.18, 1.24, 0.12, 56), primaryMat, [0, 0.06, 0]);
      tier1.receiveShadow = true;
      addPart(g, new THREE.CylinderGeometry(1.06, 1.12, 0.10, 56), darkAnodized, [0, 0.17, 0]);
      addPart(g, new THREE.TorusGeometry(1.07, 0.012, 10, 56), accentMat, [0, 0.122, 0], [Math.PI / 2, 0, 0], false);
      addPart(g, new THREE.CylinderGeometry(1.02, 1.02, 0.015, 56), brushedSteel, [0, 0.228, 0]);
      break;
    }

    case 'LoungeSofa': {
      const fabricMat = createSurfaceMaterial({ color: p.color || '#3c4048', roughness: 0.78, metalness: 0.02 });
      const woodMat = createSurfaceMaterial({ color: '#6b4d34', roughness: 0.55, metalness: 0.04, map: timberSlatTexture([2, 2]) });
      const glassMat = createArchitecturalGlass('#cfe0eb', 0.32);
      // Timber & dark steel plinth base + 3 seat cushions + backrest + bolsters
      addPart(g, new THREE.BoxGeometry(2.35, 0.07, 0.92), woodMat, [0, 0.18, 0]);
      for (const lx of [-1.05, 1.05]) for (const lz of [-0.36, 0.36]) {
        addPart(g, new THREE.CylinderGeometry(0.022, 0.018, 0.15, 12), darkAnodized, [lx, 0.075, lz]);
      }
      for (const cx of [-0.72, 0, 0.72]) {
        addPart(g, new THREE.BoxGeometry(0.70, 0.18, 0.80), fabricMat, [cx, 0.30, 0.04]);
        addPart(g, new THREE.BoxGeometry(0.70, 0.36, 0.18), fabricMat, [cx, 0.52, -0.34]);
      }
      addPart(g, new THREE.BoxGeometry(0.14, 0.30, 0.86), fabricMat, [-1.10, 0.36, 0]);
      addPart(g, new THREE.BoxGeometry(0.14, 0.30, 0.86), fabricMat, [1.10, 0.36, 0]);
      // Accompanying glass coffee table in front
      addPart(g, new THREE.CylinderGeometry(0.46, 0.46, 0.02, 36), glassMat, [0, 0.36, 1.18], [0, 0, 0], false);
      addPart(g, new THREE.TorusGeometry(0.46, 0.016, 10, 36), darkAnodized, [0, 0.36, 1.18], [Math.PI / 2, 0, 0]);
      addPart(g, new THREE.CylinderGeometry(0.18, 0.24, 0.34, 24), darkAnodized, [0, 0.17, 1.18]);
      break;
    }

    case 'SpecKiosk': {
      addPart(g, new THREE.CylinderGeometry(0.28, 0.30, 0.04, 32), darkAnodized, [0, 0.02, 0]);
      addPart(g, new THREE.BoxGeometry(0.14, 1.04, 0.09), brushedSteel, [0, 0.54, 0]);
      const head = new THREE.Group();
      head.position.set(0, 1.08, 0.04);
      head.rotation.x = -0.58;
      addPart(head, new THREE.BoxGeometry(0.56, 0.38, 0.04), darkAnodized, [0, 0, 0]);
      const screenMat = createSurfaceMaterial({ color: '#0d1522', roughness: 0.10, metalness: 0.1, emissive: p.accentColor || '#4d9bff', emissiveIntensity: 0.45 });
      addPart(head, new THREE.BoxGeometry(0.50, 0.32, 0.01), screenMat, [0, 0, 0.022], [0, 0, 0], false);
      g.add(head);
      break;
    }

    case 'BollardChain': {
      for (const bx of [-0.9, 0.9]) {
        addPart(g, new THREE.CylinderGeometry(0.15, 0.16, 0.035, 24), darkAnodized, [bx, 0.018, 0]);
        addPart(g, new THREE.CylinderGeometry(0.03, 0.03, 0.88, 18), brushedSteel, [bx, 0.47, 0]);
        addPart(g, new THREE.CylinderGeometry(0.04, 0.04, 0.05, 18), darkAnodized, [bx, 0.90, 0]);
      }
      const sag = new THREE.CatmullRomCurve3([
        new THREE.Vector3(-0.9, 0.85, 0),
        new THREE.Vector3(0, 0.72, 0),
        new THREE.Vector3(0.9, 0.85, 0),
      ]);
      addPart(g, new THREE.TubeGeometry(sag, 20, 0.015, 8, false), primaryMat);
      break;
    }

    // ------------------------------------------------------------------ PROPER 3D LIGHTING FIXTURES
    case 'StudioSoftbox': {
      const enabled = p.enabled !== false;
      const col = p.lightColor || '#fff9f2';
      const intensity = enabled ? (p.intensity ?? 2.4) : 0;
      const angle = p.angle ?? 62;
      const range = p.range ?? 20;

      // Real multi-component illumination: primary wide softbox SpotLight + soft bounce fill
      const spot = new THREE.SpotLight(col, intensity * 14, range, THREE.MathUtils.degToRad(angle), 0.85, 1.4);
      spot.position.set(0, 0.0, 0);
      spot.target.position.set(0, -4, 0);
      spot.castShadow = !!p.castShadow;
      g.add(spot, spot.target);

      const bounce = new THREE.PointLight(col, intensity * 1.4, range * 0.65, 1.6);
      bounce.position.set(0, -0.35, 0);
      g.add(bounce);

      // 3D Octagonal/Parabolic Softbox Housing:
      // 1) Tapered 8-sided black ripstop fabric cone + silver reflector interior
      const hoodOuter = createSurfaceMaterial({ color: '#17191f', roughness: 0.75, metalness: 0.1 });
      const silverLiner = createSurfaceMaterial({ color: '#cfd6df', roughness: 0.22, metalness: 0.92 });
      const diffuserMat = new THREE.MeshBasicMaterial({ color: enabled ? col : '#262930', side: THREE.DoubleSide });

      const cone = addPart(g, new THREE.CylinderGeometry(0.32, 1.05, 0.62, 8, 1, true), hoodOuter, [0, 0.31, 0]);
      cone.material.side = THREE.DoubleSide;
      const innerCone = addPart(g, new THREE.CylinderGeometry(0.28, 0.98, 0.56, 8, 1, true), silverLiner, [0, 0.29, 0], [0, 0, 0], false);
      innerCone.material.side = THREE.BackSide;

      // 2) Glowing octagonal silk diffuser + egg-crate louver grid baffles
      addPart(g, new THREE.CircleGeometry(0.98, 8), diffuserMat, [0, 0.03, 0], [Math.PI / 2, 0, 0], false);
      for (let r = -2; r <= 2; r++) {
        addPart(g, new THREE.BoxGeometry(1.75, 0.07, 0.012), darkAnodized, [0, 0.0, r * 0.34], [0, 0, 0], false);
        addPart(g, new THREE.BoxGeometry(0.012, 0.07, 1.75), darkAnodized, [r * 0.34, 0.0, 0], [0, 0, 0], false);
      }

      // 3) Rear speedring, lamp ballast cylinder, articulated U-yoke & twin steel rigging cables
      addPart(g, new THREE.CylinderGeometry(0.33, 0.33, 0.12, 16), darkAnodized, [0, 0.64, 0]);
      addPart(g, new THREE.CylinderGeometry(0.14, 0.14, 0.32, 16), brushedSteel, [0, 0.84, 0]);
      addPart(g, new THREE.BoxGeometry(0.84, 0.04, 0.06), darkAnodized, [0, 0.98, 0]);
      for (const rx of [-0.40, 0.40]) {
        addPart(g, new THREE.BoxGeometry(0.04, 0.38, 0.06), darkAnodized, [rx, 0.80, 0]);
        addPart(g, new THREE.CylinderGeometry(0.008, 0.008, 2.2, 6), brushedSteel, [rx, 2.08, 0], [0, 0, 0], false);
      }

      if (editorHelpers && selected) {
        const b = beam({ length: 2.6, radius: 1.5, color: col, strength: 0.16 });
        b.rotation.z = -Math.PI / 2;
        g.add(b);
      }
      break;
    }

    case 'LinearPendant': {
      const enabled = p.enabled !== false;
      const col = p.lightColor || '#f7fbff';
      const intensity = enabled ? (p.intensity ?? 2.2) : 0;
      const len = p.width ?? 3.6;
      const range = p.range ?? 16;

      // Twin spaced downlights for smooth linear light spread
      for (const lx of [-len * 0.3, len * 0.3]) {
        const s = new THREE.SpotLight(col, intensity * 6.5, range, THREE.MathUtils.degToRad(p.angle ?? 66), 0.88, 1.5);
        s.position.set(lx, -0.04, 0);
        s.target.position.set(lx, -4, 0);
        g.add(s, s.target);
      }

      // Extruded anodized aluminum channel + end caps + recessed frosted lens + aircraft cables
      addPart(g, new THREE.BoxGeometry(len, 0.09, 0.11), darkAnodized, [0, 0.045, 0]);
      addPart(g, new THREE.BoxGeometry(0.03, 0.10, 0.12), brushedSteel, [-len / 2, 0.045, 0]);
      addPart(g, new THREE.BoxGeometry(0.03, 0.10, 0.12), brushedSteel, [len / 2, 0.045, 0]);
      const lensMat = new THREE.MeshBasicMaterial({ color: enabled ? col : '#262a32' });
      addPart(g, new THREE.BoxGeometry(len - 0.06, 0.02, 0.075), lensMat, [0, -0.004, 0], [0, 0, 0], false);
      for (const sx of [-len * 0.38, len * 0.38]) {
        addPart(g, new THREE.CylinderGeometry(0.005, 0.005, 1.8, 6), brushedSteel, [sx, 0.98, 0], [0, 0, 0], false);
      }
      break;
    }

    case 'FresnelSpot': {
      const enabled = p.enabled !== false;
      const col = p.lightColor || '#fff7ec';
      const intensity = enabled ? (p.intensity ?? 24) : 0;
      const angle = p.angle ?? 34;
      const range = p.range ?? 24;

      const light = new THREE.SpotLight(col, intensity, range, THREE.MathUtils.degToRad(angle), 0.55, 1.6);
      light.position.set(0.24, 0, 0);
      light.target.position.set(2.5, -1.2, 0);
      light.castShadow = !!p.castShadow;
      g.add(light, light.target);

      // Lamp head aimed toward (2.5, -1.2, 0)
      const head = new THREE.Group();
      const aimDir = new THREE.Vector3(2.5, -1.2, 0).normalize();
      head.quaternion.setFromUnitVectors(new THREE.Vector3(1, 0, 0), aimDir);

      // Ribbed cylindrical barrel + Fresnel lens + 4 articulated barn doors
      addPart(head, new THREE.CylinderGeometry(0.14, 0.15, 0.34, 18), darkAnodized, [0, 0, 0], [0, 0, Math.PI / 2]);
      addPart(head, new THREE.CylinderGeometry(0.155, 0.155, 0.06, 18), brushedSteel, [0.16, 0, 0], [0, 0, Math.PI / 2]);
      const lens = new THREE.MeshBasicMaterial({ color: enabled ? col : '#2a2e36' });
      addPart(head, new THREE.CylinderGeometry(0.125, 0.125, 0.02, 18), lens, [0.185, 0, 0], [0, 0, Math.PI / 2], false);

      // 4 Barn-door flaps
      const flaps = [
        [[0.26, 0.16, 0], [0.18, 0.015, 0.24], [0, 0, 0.42]],
        [[0.26, -0.16, 0], [0.18, 0.015, 0.24], [0, 0, -0.42]],
        [[0.26, 0, 0.16], [0.18, 0.24, 0.015], [0, -0.42, 0]],
        [[0.26, 0, -0.16], [0.18, 0.24, 0.015], [0, 0.42, 0]],
      ];
      for (const [pos, sz, rot] of flaps) {
        addPart(head, new THREE.BoxGeometry(...sz), darkAnodized, pos, rot);
      }
      g.add(head);

      // U-yoke & vertical spigot stand / hanging rod
      addPart(g, new THREE.BoxGeometry(0.03, 0.36, 0.04), brushedSteel, [0, -0.18, 0.17]);
      addPart(g, new THREE.BoxGeometry(0.03, 0.36, 0.04), brushedSteel, [0, -0.18, -0.17]);
      addPart(g, new THREE.BoxGeometry(0.04, 0.04, 0.38), brushedSteel, [0, -0.36, 0]);

      if (editorHelpers) {
        const len = Math.min(range, 2.8);
        const r = Math.tan(THREE.MathUtils.degToRad(angle)) * len;
        const cone = beam({ length: len, radius: r, color: col, strength: selected ? 0.32 : 0.13 });
        cone.quaternion.copy(head.quaternion);
        cone.position.copy(aimDir).multiplyScalar(0.2);
        g.add(cone);
      }
      break;
    }

    case 'FloorUplight': {
      const enabled = p.enabled !== false;
      const col = p.lightColor || '#fff2dc';
      const intensity = enabled ? (p.intensity ?? 18) : 0;
      const angle = p.angle ?? 42;
      const range = p.range ?? 14;

      const light = new THREE.SpotLight(col, intensity, range, THREE.MathUtils.degToRad(angle), 0.65, 1.5);
      light.position.set(0, 0.20, 0);
      light.target.position.set(0, 4.0, 0);
      g.add(light, light.target);

      addPart(g, new THREE.CylinderGeometry(0.14, 0.16, 0.18, 24), darkAnodized, [0, 0.09, 0]);
      addPart(g, new THREE.CylinderGeometry(0.15, 0.15, 0.025, 24), brushedSteel, [0, 0.185, 0]);
      addPart(g, new THREE.CircleGeometry(0.125, 24), new THREE.MeshBasicMaterial({ color: enabled ? col : '#262a32' }), [0, 0.20, 0], [-Math.PI / 2, 0, 0], false);
      if (editorHelpers && selected) {
        const b = beam({ length: 2.2, radius: 1.1, color: col, strength: 0.2 });
        b.rotation.z = Math.PI / 2;
        b.position.y = 0.20;
        g.add(b);
      }
      break;
    }

    case 'LightArchRig': {
      const enabled = p.enabled !== false;
      const col = p.lightColor || '#f2f7ff';
      const curve = new THREE.CatmullRomCurve3([
        new THREE.Vector3(-4.4, 0, 0),
        new THREE.Vector3(-4.2, 3.3, 0),
        new THREE.Vector3(-2.4, 4.45, 0),
        new THREE.Vector3(0, 4.65, 0),
        new THREE.Vector3(2.4, 4.45, 0),
        new THREE.Vector3(4.2, 3.3, 0),
        new THREE.Vector3(4.4, 0, 0),
      ]);
      // Dual parallel brushed-aluminum truss chords + glowing diffuser core
      addPart(g, new THREE.TubeGeometry(curve, 48, 0.045, 8, false), brushedSteel, [0, 0, -0.10]);
      addPart(g, new THREE.TubeGeometry(curve, 48, 0.045, 8, false), brushedSteel, [0, 0, 0.10]);
      addPart(g, new THREE.TubeGeometry(curve, 48, 0.036, 8, false), new THREE.MeshBasicMaterial({ color: enabled ? col : '#282c34' }), [0, -0.02, 0], [0, 0, 0], false);
      if (enabled) {
        const pt = new THREE.PointLight(col, (p.intensity ?? 2.0) * 5, p.range ?? 18, 1.5);
        pt.position.set(0, 4.3, 0);
        g.add(pt);
      }
      break;
    }

    // ------------------------------------------------------------------ LOOK-DEV & CAMERAS
    case 'LookDevOrb': {
      addPart(g, new THREE.CylinderGeometry(0.36, 0.40, 0.92, 32), darkAnodized, [0, 0.46, 0]);
      addPart(g, new THREE.CylinderGeometry(0.37, 0.37, 0.03, 32), brushedSteel, [0, 0.935, 0]);
      const orbMat = createSurfaceMaterial({
        color: p.color || '#2c68b8',
        roughness: p.roughness ?? 0.16,
        metalness: p.metalness ?? 0.45,
        clearcoat: p.clearcoat ?? 1.0,
      });
      addPart(g, new THREE.SphereGeometry(0.28, 36, 24), orbMat, [0, 1.24, 0]);
      break;
    }

    case 'Camera': {
      if (editorHelpers) {
        const model = cameraModel({ active: selected });
        g.add(model.group);
        const len = selected ? 2.4 : 1.1;
        const h = Math.tan(THREE.MathUtils.degToRad(p.fov ?? 42) / 2) * len;
        const view = beam({ length: len, radius: h * Math.SQRT2, sides: 4, aspect: 16 / 9, color: '#bcd3ff', strength: selected ? 0.28 : 0.14 });
        view.position.x = model.lensFront;
        g.add(view);
      }
      break;
    }

    case 'StaticMesh': {
      if (assets && p.assetId && assets.get(p.assetId)) {
        const asset = assets.get(p.assetId);
        if (asset.out?.geometry) {
          const geoSrc = asset.out.geometry;
          const pos = new Float32Array(geoSrc.indices.length * 3);
          const nor = new Float32Array(geoSrc.indices.length * 3);
          for (let t = 0; t < geoSrc.indices.length / 3; t++) {
            for (let k = 0; k < 3; k++) {
              const v = geoSrc.indices[t * 3 + k] * 3;
              pos.set([geoSrc.positions[v], geoSrc.positions[v + 1], geoSrc.positions[v + 2]], (t * 3 + k) * 3);
              nor.set(geoSrc.normals.subarray(t * 9 + k * 3, t * 9 + k * 3 + 3), (t * 3 + k) * 3);
            }
          }
          const geo = new THREE.BufferGeometry();
          geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
          geo.setAttribute('normal', new THREE.BufferAttribute(nor, 3));
          geo.computeBoundingBox();
          addPart(g, geo, primaryMat);
        }
      } else {
        addPart(g, new THREE.BoxGeometry(0.8, 0.8, 0.8), primaryMat, [0, 0.4, 0]);
      }
      break;
    }
  }

  return g;
}
