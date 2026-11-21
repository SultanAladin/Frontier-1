// Bespoke multi-part Lobby Showroom environments: Minimal Architecture (default), Glass Pavilion,
// Motorsport Garage, Photo Studio Set, and Open Horizon Terrace — with procedural stone/epoxy/plaster/timber
// textures, true structural glass, proper 3D suspended luminaires, and Fresnel planar floor reflections.
import * as THREE from '../Shared/Studio/Vendor/three.esm.min.js';
import { stoneSlabTextures, epoxyGarageTextures, plasterWallTexture, timberSlatTexture } from './ProceduralTextures.js';
import { buildPropObject, createArchitecturalGlass, createSurfaceMaterial } from './ShowroomProps.js';

export const ENVIRONMENT_PRESETS = {
  minimalGallery: {
    id: 'minimalGallery',
    name: 'Minimal Architecture',
    subtitle: 'Honed limestone floor · sculptural skylight cove · glass wing',
    swatch: '#d8e0e8',
    floorStyle: 'stone',
    sunEnabled: true,
    sunAzimuth: 128,
    sunElevation: 38,
    sunIntensity: 2.85,
    sunTemperature: 5750,
    skyZenith: '#7da5d0',
    skyHorizon: '#dfe6ee',
    wallColor: '#d8dee5',
    floorColor: '#b8c1cb',
    floorReflectivity: 0.42,
    floorRoughness: 0.16,
    exposure: 1.06,
    ambientIntensity: 1.32,
    fogDensity: 0.011,
    showArchitecture: true,
    showTurntable: true,
    turntableColor: '#ccd3dc',
    turntableRim: '#dceeff',
  },
  glassPavilion: {
    id: 'glassPavilion',
    name: 'Glass Pavilion',
    subtitle: 'Structural glass curtain wall · timber soffit · courtyard trees',
    swatch: '#9cb3c6',
    floorStyle: 'stone',
    sunEnabled: true,
    sunAzimuth: 52,
    sunElevation: 30,
    sunIntensity: 3.1,
    sunTemperature: 5300,
    skyZenith: '#6d99c8',
    skyHorizon: '#dce5ee',
    wallColor: '#c8ced6',
    floorColor: '#484f58',
    floorReflectivity: 0.46,
    floorRoughness: 0.14,
    exposure: 1.04,
    ambientIntensity: 1.24,
    fogDensity: 0.010,
    showArchitecture: true,
    showTurntable: false,
    turntableColor: '#484f58',
    turntableRim: '#d9ecff',
  },
  garageBay: {
    id: 'garageBay',
    name: 'Motorsport Garage',
    subtitle: 'Epoxy bay floor · glazed roll-up door · tool chest & LED truss',
    swatch: '#3a414c',
    floorStyle: 'epoxy',
    sunEnabled: true,
    sunAzimuth: 142,
    sunElevation: 48,
    sunIntensity: 1.85,
    sunTemperature: 5900,
    skyZenith: '#202630',
    skyHorizon: '#353d4a',
    wallColor: '#2c313a',
    floorColor: '#262a31',
    floorReflectivity: 0.44,
    floorRoughness: 0.18,
    exposure: 1.12,
    ambientIntensity: 1.10,
    fogDensity: 0.012,
    showArchitecture: true,
    showTurntable: false,
    turntableColor: '#2a2e36',
    turntableRim: '#99d4ff',
  },
  studioSet: {
    id: 'studioSet',
    name: 'Photo Studio Set',
    subtitle: 'Curved infinity cyclorama · truss rig · parabolic softboxes',
    swatch: '#262932',
    floorStyle: 'stone',
    sunEnabled: true,
    sunAzimuth: 135,
    sunElevation: 56,
    sunIntensity: 1.45,
    sunTemperature: 5600,
    skyZenith: '#14171d',
    skyHorizon: '#232833',
    wallColor: '#303540',
    floorColor: '#1f232b',
    floorReflectivity: 0.50,
    floorRoughness: 0.12,
    exposure: 1.10,
    ambientIntensity: 0.96,
    fogDensity: 0.014,
    showArchitecture: true,
    showTurntable: true,
    turntableColor: '#2b303a',
    turntableRim: '#c6e4ff',
  },
  horizonTerrace: {
    id: 'horizonTerrace',
    name: 'Open Horizon Terrace',
    subtitle: 'Architectural concrete terrace · glass windbreak · open sky',
    swatch: '#8eb7df',
    floorStyle: 'stone',
    sunEnabled: true,
    sunAzimuth: 118,
    sunElevation: 32,
    sunIntensity: 3.2,
    sunTemperature: 5600,
    skyZenith: '#4c84c4',
    skyHorizon: '#d9e6f3',
    wallColor: '#cfd6df',
    floorColor: '#98a3b0',
    floorReflectivity: 0.34,
    floorRoughness: 0.22,
    exposure: 1.04,
    ambientIntensity: 1.28,
    fogDensity: 0.007,
    showArchitecture: true,
    showTurntable: true,
    turntableColor: '#bcc5cf',
    turntableRim: '#dceeff',
  },
};

export function kelvinToColor(kelvin) {
  const t = Math.max(1000, Math.min(12000, kelvin)) / 100;
  let r, g, b;
  if (t <= 66) {
    r = 255;
    g = 99.4708 * Math.log(t) - 161.1196;
    b = t <= 19 ? 0 : 138.5177 * Math.log(t - 10) - 305.0448;
  } else {
    r = 329.6987 * Math.pow(t - 60, -0.1332);
    g = 288.1222 * Math.pow(t - 60, -0.0755);
    b = 255;
  }
  return new THREE.Color(
    Math.max(0, Math.min(255, r)) / 255,
    Math.max(0, Math.min(255, g)) / 255,
    Math.max(0, Math.min(255, b)) / 255
  );
}

const SKY_VERT = /* glsl */`
  varying vec3 vDir;
  void main() {
    vDir = normalize(position);
    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
  }
`;

const SKY_FRAG = /* glsl */`
  uniform vec3 uZenith;
  uniform vec3 uHorizon;
  uniform vec3 uGround;
  uniform vec3 uSunDir;
  uniform vec3 uSunColor;
  uniform float uSunGlow;
  varying vec3 vDir;
  void main() {
    vec3 d = normalize(vDir);
    float h = clamp(d.y, 0.0, 1.0);
    vec3 col = mix(uHorizon, uZenith, pow(h, 0.56));
    if (d.y < 0.0) {
      col = mix(uHorizon, uGround, clamp(-d.y * 4.0, 0.0, 1.0));
    }
    float sunAmt = max(dot(d, normalize(uSunDir)), 0.0);
    col += uSunColor * (pow(sunAmt, 24.0) * 0.22 + pow(sunAmt, 420.0) * 1.4) * uSunGlow;
    gl_FragColor = vec4(col, 1.0);
  }
`;

const FLOOR_VERT = /* glsl */`
  varying vec4 vProj;
  varying vec3 vWorld;
  void main() {
    vec4 wp = modelMatrix * vec4(position, 1.0);
    vWorld = wp.xyz;
    vProj = projectionMatrix * viewMatrix * wp;
    gl_Position = vProj;
  }
`;

const FLOOR_FRAG = /* glsl */`
  uniform sampler2D uReflection;
  uniform sampler2D uFloorMap;
  uniform vec2 uTexelSize;
  uniform float uTileScale;
  uniform vec3 uBaseColor;
  uniform vec3 uHorizonColor;
  uniform float uReflectivity;
  uniform float uRoughness;
  uniform float uFogDensity;
  varying vec4 vProj;
  varying vec3 vWorld;

  void main() {
    vec2 uv = (vProj.xy / max(vProj.w, 0.0001)) * 0.5 + 0.5;
    uv = vec2(clamp(uv.x, 0.002, 0.998), clamp(1.0 - uv.y, 0.002, 0.998));

    float blur = mix(0.6, 4.4, uRoughness);
    vec2 off = uTexelSize * blur;
    vec3 refl = texture2D(uReflection, uv).rgb * 0.36
      + texture2D(uReflection, uv + vec2(off.x, 0.0)).rgb * 0.16
      + texture2D(uReflection, uv - vec2(off.x, 0.0)).rgb * 0.16
      + texture2D(uReflection, uv + vec2(0.0, off.y)).rgb * 0.16
      + texture2D(uReflection, uv - vec2(0.0, off.y)).rgb * 0.16;

    vec3 texSample = texture2D(uFloorMap, vWorld.xz * uTileScale).rgb;
    vec3 surfaceCol = mix(uBaseColor, texSample, 0.72);

    vec3 viewDir = normalize(cameraPosition - vWorld);
    float ndotv = clamp(viewDir.y, 0.0, 1.0);
    float fresnel = uReflectivity * (0.26 + 0.74 * pow(1.0 - ndotv, 2.5));

    float dist = length(vWorld.xz);
    float radial = smoothstep(4.0, 32.0, dist);
    vec3 base = mix(surfaceCol, uHorizonColor * 0.85, radial * 0.45);

    vec3 col = mix(base, refl, clamp(fresnel, 0.0, 0.78));
    float fog = 1.0 - exp(-pow(dist * uFogDensity * 1.3, 2.0));
    col = mix(col, uHorizonColor, clamp(fog * 0.82, 0.0, 0.88));

    gl_FragColor = vec4(col, 1.0);
  }
`;

export class ShowroomEnvironment {
  constructor(renderer, scene, camera) {
    this.renderer = renderer;
    this.scene = scene;
    this.camera = camera;
    this.pmrem = new THREE.PMREMGenerator(renderer);
    this.envRenderTarget = null;
    this.reflectionTarget = new THREE.WebGLRenderTarget(1024, 512, {
      minFilter: THREE.LinearFilter,
      magFilter: THREE.LinearFilter,
      format: THREE.RGBAFormat,
    });
    this.mirrorCamera = new THREE.PerspectiveCamera();

    this.group = new THREE.Group();
    this.group.name = 'ShowroomEnvironmentRoot';
    this.scene.add(this.group);

    // 1. Sky Dome
    this.skyMat = new THREE.ShaderMaterial({
      uniforms: {
        uZenith: { value: new THREE.Color('#7da5d0') },
        uHorizon: { value: new THREE.Color('#dfe6ee') },
        uGround: { value: new THREE.Color('#a4aeb9') },
        uSunDir: { value: new THREE.Vector3(0.4, 0.6, 0.5).normalize() },
        uSunColor: { value: new THREE.Color('#fff9ef') },
        uSunGlow: { value: 1.0 },
      },
      vertexShader: SKY_VERT,
      fragmentShader: SKY_FRAG,
      side: THREE.BackSide,
      depthWrite: false,
    });
    this.skyMesh = new THREE.Mesh(new THREE.SphereGeometry(140, 32, 20), this.skyMat);
    this.skyMesh.raycast = () => {};
    this.group.add(this.skyMesh);

    // 2. Textured Reflective Floor + Shadow Catcher
    const defaultFloorTex = stoneSlabTextures('#b8c1cb', [1, 1]).map;
    this.floorMat = new THREE.ShaderMaterial({
      uniforms: {
        uReflection: { value: this.reflectionTarget.texture },
        uFloorMap: { value: defaultFloorTex },
        uTexelSize: { value: new THREE.Vector2(1 / 1024, 1 / 512) },
        uTileScale: { value: 0.35 },
        uBaseColor: { value: new THREE.Color('#b8c1cb') },
        uHorizonColor: { value: new THREE.Color('#dfe6ee') },
        uReflectivity: { value: 0.42 },
        uRoughness: { value: 0.16 },
        uFogDensity: { value: 0.011 },
      },
      vertexShader: FLOOR_VERT,
      fragmentShader: FLOOR_FRAG,
    });
    this.floorMesh = new THREE.Mesh(new THREE.CircleGeometry(70, 72), this.floorMat);
    this.floorMesh.rotation.x = -Math.PI / 2;
    this.floorMesh.position.y = 0;
    this.floorMesh.raycast = () => {};
    this.group.add(this.floorMesh);

    this.shadowPlane = new THREE.Mesh(
      new THREE.PlaneGeometry(48, 48),
      new THREE.ShadowMaterial({ opacity: 0.36, color: 0x0b1220 })
    );
    this.shadowPlane.rotation.x = -Math.PI / 2;
    this.shadowPlane.position.y = 0.002;
    this.shadowPlane.receiveShadow = true;
    this.shadowPlane.raycast = () => {};
    this.group.add(this.shadowPlane);

    // 3. Architectural Bay Root (rebuilt cleanly per preset / wall tone)
    this.archGroup = new THREE.Group();
    this.group.add(this.archGroup);

    // 4. Flush Architectural Turntable Ring
    this.turntableGroup = new THREE.Group();
    this.buildTurntable();
    this.group.add(this.turntableGroup);

    // 5. Primary Daylight / Studio Lights
    this.hemi = new THREE.HemisphereLight('#eef4fc', '#666e7a', 1.32);
    this.sun = new THREE.DirectionalLight('#fff8ef', 2.85);
    this.sun.castShadow = true;
    this.sun.shadow.mapSize.set(2048, 2048);
    this.sun.shadow.bias = -0.00035;
    this.sun.shadow.normalBias = 0.015;
    const sc = this.sun.shadow.camera;
    sc.left = -15; sc.right = 15; sc.top = 15; sc.bottom = -15; sc.near = 0.5; sc.far = 55;
    this.fill = new THREE.DirectionalLight('#bfd6f6', 0.65);
    this.fill.position.set(-10, 6, -9);
    this.scene.add(this.hemi, this.sun, this.sun.target, this.fill);

    this.lastArchKey = '';
    this.lastEnvKey = '';
  }

  buildTurntable() {
    this.plinthMat = new THREE.MeshPhysicalMaterial({ color: '#ccd3dc', roughness: 0.28, metalness: 0.12, clearcoat: 0.3 });
    this.plinthBezelMat = new THREE.MeshPhysicalMaterial({ color: '#8f98a6', roughness: 0.24, metalness: 0.85 });
    this.rimMat = new THREE.MeshBasicMaterial({ color: '#dceeff' });

    const disc = new THREE.Mesh(new THREE.CylinderGeometry(3.78, 3.92, 0.024, 72), this.plinthMat);
    disc.position.y = 0.001;
    disc.receiveShadow = true;
    disc.raycast = () => {};

    const bezel = new THREE.Mesh(new THREE.RingGeometry(3.75, 3.84, 72), this.plinthBezelMat);
    bezel.rotation.x = -Math.PI / 2;
    bezel.position.y = 0.0135;
    bezel.raycast = () => {};

    const rim = new THREE.Mesh(new THREE.RingGeometry(3.60, 3.64, 72), this.rimMat);
    rim.rotation.x = -Math.PI / 2;
    rim.position.y = 0.014;
    rim.raycast = () => {};

    this.turntableGroup.add(disc, bezel, rim);
  }

  addArchMesh(geo, mat, pos = [0, 0, 0], rot = [0, 0, 0], cast = true) {
    const m = new THREE.Mesh(geo, mat);
    m.position.set(...pos);
    m.rotation.set(...rot);
    m.castShadow = cast;
    m.receiveShadow = true;
    m.raycast = () => {};
    this.archGroup.add(m);
    return m;
  }

  rebuildArchitecture(env) {
    const key = `${env.presetId}|${env.wallColor}|${env.showArchitecture}`;
    if (key === this.lastArchKey) return;
    this.lastArchKey = key;

    while (this.archGroup.children.length) {
      const ch = this.archGroup.children.pop();
      ch.traverse((o) => {
        if (o.geometry) o.geometry.dispose();
        if (o.material) {
          for (const m of Array.isArray(o.material) ? o.material : [o.material]) m.dispose();
        }
      });
    }

    if (env.showArchitecture === false) return;

    const wallTex = plasterWallTexture(env.wallColor, [6, 2]);
    const wallMat = createSurfaceMaterial({ color: env.wallColor, roughness: 0.84, metalness: 0.02, map: wallTex });
    const steelMat = createSurfaceMaterial({ color: '#1d2128', roughness: 0.34, metalness: 0.86 });
    const brushedMat = createSurfaceMaterial({ color: '#adb6c2', roughness: 0.24, metalness: 0.90 });
    const timberMat = createSurfaceMaterial({ color: '#78583e', roughness: 0.56, metalness: 0.04, map: timberSlatTexture([6, 3]) });
    const glassMat = createArchitecturalGlass('#d8e8f2', 0.22);

    // --------------------------------------------------------- PRESET 1: MINIMAL ARCHITECTURAL GALLERY
    if (env.presetId === 'minimalGallery') {
      // Smooth curved sculptural backdrop wall with radiused base cove + top coping
      const wallArc = new THREE.CylinderGeometry(14.6, 14.6, 5.1, 72, 1, true, Math.PI * 0.62, Math.PI * 1.15);
      wallMat.side = THREE.DoubleSide;
      this.addArchMesh(wallArc, wallMat, [0, 2.55, 0]);

      // Radiused floor-to-wall cove sweep
      const coveProfile = [];
      for (let i = 0; i <= 10; i++) {
        const t = (i / 10) * (Math.PI / 2);
        coveProfile.push(new THREE.Vector2(14.6 - 1.1 * (1 - Math.cos(t)), 1.1 * (1 - Math.sin(t))));
      }
      const coveGeo = new THREE.LatheGeometry(coveProfile, 64, Math.PI * 0.62, Math.PI * 1.15);
      this.addArchMesh(coveGeo, wallMat, [0, 0, 0], [0, 0, 0], false);

      // Architectural concrete overhang canopy + recessed skylight beams on the back-left
      this.addArchMesh(new THREE.BoxGeometry(11.5, 0.32, 4.4), wallMat, [-2.2, 5.15, -8.4], [0, 0.26, 0]);
      for (let i = -2; i <= 2; i++) {
        this.addArchMesh(new THREE.BoxGeometry(0.22, 0.36, 4.5), steelMat, [-2.2 + i * 2.2, 4.95, -8.4], [0, 0.26, 0]);
      }

      // Architectural wing fin casting crisp diagonal shadow across the cove
      this.addArchMesh(new THREE.BoxGeometry(0.42, 6.2, 10.5), wallMat, [-10.8, 3.1, -4.6], [0, 0.28, 0]);
      this.addArchMesh(new THREE.BoxGeometry(0.48, 0.14, 10.6), brushedMat, [-10.8, 6.22, -4.6], [0, 0.28, 0]);

      // Frameless low-iron structural glass balustrade partition along the right side
      const glassWing = buildPropObject({ id: 'arch_glass', kind: 'GlassPartition', props: { color: '#1c2026', accentColor: '#dcebf5' } }, { editorHelpers: false });
      glassWing.position.set(8.2, 0, -4.2);
      glassWing.rotation.y = -0.58;
      this.archGroup.add(glassWing);

      // Sculptural architectural planter tree flanking the wing
      const tree = buildPropObject({ id: 'arch_tree', kind: 'CourtyardTree', props: {} }, { editorHelpers: false });
      tree.position.set(6.4, 0, -7.2);
      this.archGroup.add(tree);
    }

    // --------------------------------------------------------- PRESET 2: GLASS PAVILION SHOWROOM
    else if (env.presetId === 'glassPavilion') {
      // Structural steel H-columns & roof beams around perimeter
      const colPositions = [
        [-8.2, -7.2], [-2.8, -7.2], [2.8, -7.2], [8.2, -7.2],
        [-8.2, -1.2], [8.2, -1.2], [-8.2, 4.8], [8.2, 4.8],
      ];
      for (const [cx, cz] of colPositions) {
        this.addArchMesh(new THREE.BoxGeometry(0.24, 4.6, 0.24), steelMat, [cx, 2.3, cz]);
        this.addArchMesh(new THREE.BoxGeometry(0.36, 0.06, 0.36), brushedMat, [cx, 0.03, cz]);
      }
      // Roof perimeter fascia & slatted timber soffit rafters
      this.addArchMesh(new THREE.BoxGeometry(17.2, 0.26, 0.30), steelMat, [0, 4.65, -7.2]);
      this.addArchMesh(new THREE.BoxGeometry(0.30, 0.26, 12.6), steelMat, [-8.2, 4.65, -1.2]);
      this.addArchMesh(new THREE.BoxGeometry(0.30, 0.26, 12.6), steelMat, [8.2, 4.65, -1.2]);
      for (let z = -6.4; z <= 4.2; z += 1.35) {
        this.addArchMesh(new THREE.BoxGeometry(16.2, 0.14, 0.12), timberMat, [0, 4.52, z]);
      }

      // Floor-to-ceiling structural glass curtain walls (back + left + right)
      this.addArchMesh(new THREE.BoxGeometry(16.2, 4.35, 0.03), glassMat, [0, 2.25, -7.2], [0, 0, 0], false);
      this.addArchMesh(new THREE.BoxGeometry(0.03, 4.35, 11.8), glassMat, [-8.2, 2.25, -1.2], [0, 0, 0], false);
      this.addArchMesh(new THREE.BoxGeometry(0.03, 4.35, 11.8), glassMat, [8.2, 2.25, -1.2], [0, 0, 0], false);
      // Transom bars
      this.addArchMesh(new THREE.BoxGeometry(16.3, 0.06, 0.08), steelMat, [0, 3.1, -7.2]);
      this.addArchMesh(new THREE.BoxGeometry(0.08, 0.06, 12.0), steelMat, [-8.2, 3.1, -1.2]);
      this.addArchMesh(new THREE.BoxGeometry(0.08, 0.06, 12.0), steelMat, [8.2, 3.1, -1.2]);

      // Suspended architectural linear pendants inside the pavilion
      for (const lx of [-3.2, 3.2]) {
        const pend = buildPropObject({ id: `pav_pend_${lx}`, kind: 'LinearPendant', props: { width: 4.4, intensity: 1.8 } }, { editorHelpers: false });
        pend.position.set(lx, 4.25, -1.0);
        pend.rotation.y = Math.PI / 2;
        this.archGroup.add(pend);
      }

      // Courtyard trees visible outside the glass curtain wall
      for (const [tx, tz] of [[-5.5, -10.2], [5.5, -10.2]]) {
        const tree = buildPropObject({ id: `pav_tree_${tx}`, kind: 'CourtyardTree', props: {} }, { editorHelpers: false });
        tree.position.set(tx, 0, tz);
        this.archGroup.add(tree);
      }
    }

    // --------------------------------------------------------- PRESET 3: MOTORSPORT GARAGE BAY
    else if (env.presetId === 'garageBay') {
      // Back wall + side walls with recessed structural steel bays
      this.addArchMesh(new THREE.BoxGeometry(16.4, 4.6, 0.32), wallMat, [0, 2.3, -6.8]);
      this.addArchMesh(new THREE.BoxGeometry(0.32, 4.6, 12.6), wallMat, [-8.0, 2.3, -0.6]);
      this.addArchMesh(new THREE.BoxGeometry(0.32, 4.6, 12.6), wallMat, [8.0, 2.3, -0.6]);
      for (const rx of [-7.8, -3.9, 0, 3.9, 7.8]) {
        this.addArchMesh(new THREE.BoxGeometry(0.18, 4.6, 0.24), steelMat, [rx, 2.3, -6.6]);
        this.addArchMesh(new THREE.BoxGeometry(0.18, 0.22, 12.2), steelMat, [rx, 4.45, -0.6]);
      }

      // Glazed sectional garage door on the back bay
      const door = buildPropObject({ id: 'gar_door', kind: 'GarageDoorBay', props: {} }, { editorHelpers: false });
      door.position.set(-2.4, 0, -6.55);
      this.archGroup.add(door);

      // Tool cabinet & workbench + Motorsport tyre rack
      const bench = buildPropObject({ id: 'gar_bench', kind: 'GarageBayBench', props: {} }, { editorHelpers: false });
      bench.position.set(3.2, 0, -6.1);
      this.archGroup.add(bench);

      const rack = buildPropObject({ id: 'gar_rack', kind: 'TyreRack', props: {} }, { editorHelpers: false });
      rack.position.set(-6.9, 0, -2.5);
      rack.rotation.y = Math.PI / 2;
      this.archGroup.add(rack);

      // Overhead linear LED bay light rig (real multi-part aluminum pendants)
      for (const lz of [-2.2, 1.4]) {
        for (const lx of [-2.4, 2.4]) {
          const pend = buildPropObject({ id: `gar_led_${lx}_${lz}`, kind: 'LinearPendant', props: { width: 3.4, intensity: 1.9 } }, { editorHelpers: false });
          pend.position.set(lx, 4.05, lz);
          this.archGroup.add(pend);
        }
      }
    }

    // --------------------------------------------------------- PRESET 4: PRODUCTION STUDIO SET
    else if (env.presetId === 'studioSet') {
      // Seamless U-shaped curved infinity cyclorama wall + floor cove
      wallMat.side = THREE.DoubleSide;
      const cycWall = new THREE.CylinderGeometry(12.2, 12.2, 5.4, 64, 1, true, Math.PI * 0.65, Math.PI * 1.10);
      this.addArchMesh(cycWall, wallMat, [0, 2.7, 0]);
      const covePts = [];
      for (let i = 0; i <= 12; i++) {
        const t = (i / 12) * (Math.PI / 2);
        covePts.push(new THREE.Vector2(12.2 - 1.4 * (1 - Math.cos(t)), 1.4 * (1 - Math.sin(t))));
      }
      this.addArchMesh(new THREE.LatheGeometry(covePts, 64, Math.PI * 0.65, Math.PI * 1.10), wallMat, [0, 0, 0], [0, 0, 0], false);

      // Suspended aluminum box-truss gantry overhead
      for (const tz of [-1.8, 1.8]) {
        this.addArchMesh(new THREE.CylinderGeometry(0.035, 0.035, 10.5, 8), brushedMat, [0, 4.85, tz], [0, 0, Math.PI / 2]);
        this.addArchMesh(new THREE.CylinderGeometry(0.035, 0.035, 10.5, 8), brushedMat, [0, 5.15, tz], [0, 0, Math.PI / 2]);
      }
      for (const tx of [-4.6, 0, 4.6]) {
        this.addArchMesh(new THREE.CylinderGeometry(0.03, 0.03, 3.6, 8), brushedMat, [tx, 4.85, 0], [Math.PI / 2, 0, 0]);
      }

      // 3 Proper Parabolic Studio Softboxes rigged overhead + 2 flanking Fresnel barn-door spots
      for (const sx of [-2.8, 0, 2.8]) {
        const sb = buildPropObject({ id: `stu_sb_${sx}`, kind: 'StudioSoftbox', props: { intensity: 1.6 } }, { editorHelpers: false });
        sb.position.set(sx, 3.95, 0);
        this.archGroup.add(sb);
      }
      const spotL = buildPropObject({ id: 'stu_fres_l', kind: 'FresnelSpot', props: { intensity: 16 } }, { editorHelpers: false });
      spotL.position.set(-4.8, 2.2, 3.4);
      spotL.rotation.y = 0.65;
      this.archGroup.add(spotL);
    }

    // --------------------------------------------------------- PRESET 5: OPEN HORIZON TERRACE
    else if (env.presetId === 'horizonTerrace') {
      // Raised architectural terrace deck curb + low parapet wall + glass windbreak
      this.addArchMesh(new THREE.BoxGeometry(18, 0.14, 14), wallMat, [0, -0.05, 0]);
      this.addArchMesh(new THREE.BoxGeometry(16.4, 0.85, 0.36), wallMat, [0, 0.425, -6.8]);
      this.addArchMesh(new THREE.BoxGeometry(16.5, 0.06, 0.42), brushedMat, [0, 0.88, -6.8]);
      this.addArchMesh(new THREE.BoxGeometry(16.0, 1.05, 0.03), glassMat, [0, 1.40, -6.8], [0, 0, 0], false);

      // Architectural louvered pergola wing on the left
      for (const px of [-6.8, -6.8]) {
        this.addArchMesh(new THREE.BoxGeometry(0.24, 3.8, 0.24), steelMat, [px, 1.9, -6.4]);
        this.addArchMesh(new THREE.BoxGeometry(0.24, 3.8, 0.24), steelMat, [px, 1.9, -1.8]);
      }
      for (let lz = -6.4; lz <= -1.8; lz += 0.55) {
        this.addArchMesh(new THREE.BoxGeometry(3.6, 0.08, 0.16), timberMat, [-5.6, 3.78, lz]);
      }

      // Two courtyard olive trees framing the terrace
      const t1 = buildPropObject({ id: 'ter_t1', kind: 'CourtyardTree', props: {} }, { editorHelpers: false });
      t1.position.set(-5.8, 0, -5.2);
      const t2 = buildPropObject({ id: 'ter_t2', kind: 'CourtyardTree', props: {} }, { editorHelpers: false });
      t2.position.set(6.2, 0, -5.2);
      this.archGroup.add(t1, t2);
    }
  }

  updateEnvironmentMap(env) {
    const key = `${env.presetId}|${env.skyZenith}|${env.skyHorizon}|${env.sunTemperature}|${env.wallColor}|${env.floorColor}`;
    if (key === this.lastEnvKey) return;
    this.lastEnvKey = key;

    const envScene = new THREE.Scene();
    envScene.background = new THREE.Color(env.skyHorizon);
    envScene.add(new THREE.HemisphereLight(env.skyZenith, env.floorColor, 1.4));

    // Studio softbox reflection cards positioned realistically for crisp automotive body & glass highlights
    const cardMat = new THREE.MeshBasicMaterial({ color: '#ffffff', side: THREE.DoubleSide });
    for (const [x, y, z, w, h] of [[-2.8, 5.2, 0, 3.2, 2.2], [2.8, 5.2, 0, 3.2, 2.2], [0, 5.5, -2.5, 8.0, 1.4]]) {
      const card = new THREE.Mesh(new THREE.PlaneGeometry(w, h), cardMat);
      card.position.set(x, y, z);
      card.rotation.x = Math.PI / 2;
      envScene.add(card);
    }
    const horizonWall = new THREE.Mesh(
      new THREE.CylinderGeometry(12, 12, 5, 32, 1, true),
      new THREE.MeshBasicMaterial({ color: env.wallColor, side: THREE.BackSide })
    );
    horizonWall.position.y = 2.0;
    envScene.add(horizonWall);

    const nextRT = this.pmrem.fromScene(envScene, 0.035);
    if (this.envRenderTarget) this.envRenderTarget.dispose();
    this.envRenderTarget = nextRT;
    this.scene.environment = nextRT.texture;
  }

  apply(env) {
    const az = THREE.MathUtils.degToRad(env.sunAzimuth);
    const el = THREE.MathUtils.degToRad(Math.max(2, env.sunElevation));
    const sunDir = new THREE.Vector3(
      Math.cos(el) * Math.cos(az),
      Math.sin(el),
      Math.cos(el) * Math.sin(az)
    ).normalize();

    const sunColor = kelvinToColor(env.sunTemperature);
    this.sun.visible = env.sunEnabled !== false;
    this.sun.position.copy(sunDir).multiplyScalar(22);
    this.sun.target.position.set(0, 0.5, 0);
    this.sun.color.copy(sunColor);
    this.sun.intensity = env.sunEnabled !== false ? env.sunIntensity : 0;

    this.hemi.color.set(env.skyZenith);
    this.hemi.groundColor.set(env.floorColor);
    this.hemi.intensity = env.ambientIntensity;
    this.fill.intensity = env.sunEnabled !== false ? 0.55 : 0.9;

    this.skyMat.uniforms.uZenith.value.set(env.skyZenith);
    this.skyMat.uniforms.uHorizon.value.set(env.skyHorizon);
    this.skyMat.uniforms.uGround.value.set(env.floorColor);
    this.skyMat.uniforms.uSunDir.value.copy(sunDir);
    this.skyMat.uniforms.uSunColor.value.copy(sunColor);
    this.skyMat.uniforms.uSunGlow.value = env.sunEnabled !== false ? 1.0 : 0.0;

    this.rebuildArchitecture(env);

    this.turntableGroup.visible = env.showTurntable !== false;
    this.plinthMat.color.set(env.turntableColor || '#ccd3dc');
    this.rimMat.color.set(env.turntableRim || '#dceeff');

    const floorTex = env.floorStyle === 'epoxy'
      ? epoxyGarageTextures([1, 1]).map
      : stoneSlabTextures(env.floorColor, [1, 1]).map;
    this.floorMat.uniforms.uFloorMap.value = floorTex;
    this.floorMat.uniforms.uBaseColor.value.set(env.floorColor);
    this.floorMat.uniforms.uHorizonColor.value.set(env.skyHorizon);
    this.floorMat.uniforms.uReflectivity.value = env.floorReflectivity;
    this.floorMat.uniforms.uRoughness.value = env.floorRoughness;
    this.floorMat.uniforms.uFogDensity.value = env.fogDensity;

    this.renderer.toneMappingExposure = env.exposure;
    this.updateEnvironmentMap(env);
  }

  renderReflection(onRenderScene) {
    const w = Math.max(256, Math.floor(this.renderer.domElement.width * 0.5));
    const h = Math.max(256, Math.floor(this.renderer.domElement.height * 0.5));
    if (this.reflectionTarget.width !== w || this.reflectionTarget.height !== h) {
      this.reflectionTarget.setSize(w, h);
      this.floorMat.uniforms.uTexelSize.value.set(1 / w, 1 / h);
    }

    this.mirrorCamera.copy(this.camera);
    this.mirrorCamera.position.y = -this.camera.position.y;
    const target = new THREE.Vector3();
    this.camera.getWorldDirection(target);
    target.add(this.camera.position);
    target.y = -target.y;
    this.mirrorCamera.up.set(0, 1, 0);
    this.mirrorCamera.lookAt(target);
    this.mirrorCamera.updateMatrixWorld();

    const prevFloorVis = this.floorMesh.visible;
    const prevShadowVis = this.shadowPlane.visible;
    const prevShadows = this.renderer.shadowMap.autoUpdate;
    this.floorMesh.visible = false;
    this.shadowPlane.visible = false;
    this.renderer.shadowMap.autoUpdate = false;

    const prevTarget = this.renderer.getRenderTarget();
    this.renderer.setRenderTarget(this.reflectionTarget);
    this.renderer.clear();
    onRenderScene(this.mirrorCamera);
    this.renderer.setRenderTarget(prevTarget);

    this.floorMesh.visible = prevFloorVis;
    this.shadowPlane.visible = prevShadowVis;
    this.renderer.shadowMap.autoUpdate = prevShadows;
  }
}
