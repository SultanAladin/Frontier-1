// Viewport debug visuals without line helpers. Everything here is a lit or shaded surface, a soft volume or a glow:
//   field shells   collision and influence volumes: fresnel rim, soft face borders, rising scan bands (or voxel dust
//                  for SDF colliders), so a volume reads as a volume instead of a cage of edges
//   beams          spot-light cones and camera frusta as additive light volumes fading along their axis
//   glows          light cores and seam beads as camera-facing glow sprites
//   halo           selection: the selected surfaces glow amber toward their silhouette, inside their own material
//   dot floor      the ground as a field of dots (metres and quarters) fading into the dark, not a line grid
//   solids         arrows and anchors as small shaded solids
//   density        triangle density as a heat ramp on the surface (replaces wireframe)
import * as THREE from './Vendor/three.esm.min.js';

export const clock = { value: 0 }; // seconds, advanced by the Stage each frame and shared by every animated material

// ----------------------------------------------------------------------------------------------------- field shell
const SHELL_VERTEX = /* glsl */`
  varying vec3 vNormalView;
  varying vec3 vViewDir;
  varying vec3 vWorld;
  varying vec3 vLocal;
  varying vec2 vUv;
  varying vec3 vObjNormal;
  void main() {
    vec4 world = modelMatrix * vec4(position, 1.0);
    vWorld = world.xyz;
    vLocal = position;
    vObjNormal = normal;
    vUv = uv;
    vec4 view = viewMatrix * world;
    vViewDir = normalize(-view.xyz);
    vNormalView = normalize(normalMatrix * normal);
    gl_Position = projectionMatrix * view;
  }`;

const SHELL_FRAGMENT = /* glsl */`
  uniform vec3 uColor;
  uniform float uTime, uBase, uRim, uScan, uBorder, uDots, uPulse;
  varying vec3 vNormalView;
  varying vec3 vViewDir;
  varying vec3 vWorld;
  varying vec3 vLocal;
  varying vec2 vUv;
  varying vec3 vObjNormal;
  void main() {
    vec3 n = normalize(vNormalView);
    if (!gl_FrontFacing) n = -n;
    float facing = abs(dot(n, normalize(vViewDir)));
    float rim = pow(1.0 - facing, 2.2);
    float a = uBase + uRim * rim;
    // Soft face borders (box faces carry 0..1 UVs): a gradient that pools toward the edges, never a hard line.
    if (uBorder > 0.0) {
      vec2 e = min(vUv, 1.0 - vUv);
      float edge = min(e.x, e.y);
      a += uBorder * (1.0 - smoothstep(0.0, 0.14, edge)) * (1.0 - smoothstep(0.0, 0.14, edge));
    }
    // Rising scan bands in world height.
    if (uScan > 0.0) {
      float band = pow(0.5 + 0.5 * sin(vWorld.y * 14.0 - uTime * 2.4), 18.0);
      a += uScan * band;
    }
    // Voxel dust: one soft dot per voxel cell on the shell (SDF colliders).
    if (uDots > 0.0) {
      vec3 an = abs(vObjNormal);
      vec2 q = an.x > an.y && an.x > an.z ? vLocal.yz : (an.y > an.z ? vLocal.xz : vLocal.xy); // the face's own plane
      vec2 cell = fract(q * uDots) - 0.5;
      a += 0.55 * (1.0 - smoothstep(0.1, 0.22, length(cell)));
    }
    if (uPulse > 0.0) a *= 1.0 - uPulse * 0.5 + uPulse * 0.5 * sin(uTime * 4.0);
    gl_FragColor = vec4(uColor * (0.75 + 0.6 * rim), clamp(a, 0.0, 1.0));
  }`;

export function shellMaterial(color, { base = 0.05, rim = 0.5, scan = 0.22, border = 0, dots = 0, pulse = 0, additive = false } = {}) {
  return new THREE.ShaderMaterial({
    uniforms: {
      uColor: { value: new THREE.Color(color) }, uTime: clock, uBase: { value: base }, uRim: { value: rim },
      uScan: { value: scan }, uBorder: { value: border }, uDots: { value: dots }, uPulse: { value: pulse },
    },
    vertexShader: SHELL_VERTEX, fragmentShader: SHELL_FRAGMENT,
    transparent: true, depthWrite: false, side: THREE.DoubleSide,
    blending: additive ? THREE.AdditiveBlending : THREE.NormalBlending,
  });
}

// Collision or influence volume as a field shell. shape: { type: box|sphere|capsule|sdf, centre, half|radius|halfHeight, axis }
export function fieldShell(shape, color, options = {}) {
  const group = new THREE.Group();
  if (!shape || shape.type === 'none') return group;
  let geo, mat;
  if (shape.type === 'box' || shape.type === 'sdf') {
    geo = new THREE.BoxGeometry(shape.half[0] * 2, shape.half[1] * 2, shape.half[2] * 2);
    const longest = Math.max(...shape.half) * 2;
    mat = shape.type === 'sdf'
      ? shellMaterial(color, { base: 0.02, rim: 0.3, scan: 0, border: 0.25, dots: (shape.resolution || 32) / longest, ...options })
      : shellMaterial(color, { border: 0.4, ...options });
  } else if (shape.type === 'sphere') {
    geo = new THREE.SphereGeometry(shape.radius, 40, 20);
    mat = shellMaterial(color, options);
  } else if (shape.type === 'capsule') {
    geo = new THREE.CapsuleGeometry(shape.radius, shape.halfHeight * 2, 8, 24);
    mat = shellMaterial(color, options);
  }
  if (!geo) return group;
  const mesh = new THREE.Mesh(geo, mat);
  if (shape.type === 'capsule') {
    if (shape.axis === 0) mesh.rotation.z = Math.PI / 2;
    if (shape.axis === 2) mesh.rotation.x = Math.PI / 2;
  }
  mesh.position.set(...(shape.centre || [0, 0, 0]));
  mesh.renderOrder = 5;
  group.add(mesh);
  return markDebug(group);
}

// --------------------------------------------------------------------------------------------------------- beams
const BEAM_FRAGMENT = /* glsl */`
  uniform vec3 uColor;
  uniform float uStrength, uLength, uTime;
  varying vec3 vNormalView;
  varying vec3 vViewDir;
  varying vec3 vLocal;
  void main() {
    float along = clamp(vLocal.x / uLength, 0.0, 1.0);           // 0 at the source, 1 at the far end
    float facing = abs(dot(normalize(vNormalView), normalize(vViewDir)));
    float core = pow(facing, 1.4);                                 // brighter through the middle of the volume
    float fall = pow(1.0 - along, 1.6) * smoothstep(0.0, 0.06, along);
    float shimmer = 0.92 + 0.08 * sin(along * 40.0 - uTime * 3.0);
    gl_FragColor = vec4(uColor, uStrength * fall * (0.25 + 0.75 * core) * shimmer);
  }`;

// A light-volume beam along +X: spot cones (round) and camera frusta (sides = 4, aspect stretches it).
export function beam({ length, radius, color, sides = 32, aspect = 1, strength = 0.32 }) {
  const geo = new THREE.ConeGeometry(radius, length, sides, 1, true); // axis +Y, apex at +length/2
  geo.rotateZ(Math.PI / 2);          // apex to −X …
  geo.translate(length / 2, 0, 0);   // … then the apex sits at the origin and the open end at x = length
  if (sides === 4) geo.rotateX(Math.PI / 4); // pyramid faces square to Y and Z
  if (aspect !== 1) geo.scale(1, 1, aspect);
  geo.computeVertexNormals();
  const mat = new THREE.ShaderMaterial({
    uniforms: { uColor: { value: new THREE.Color(color) }, uStrength: { value: strength }, uLength: { value: length }, uTime: clock },
    vertexShader: SHELL_VERTEX, fragmentShader: BEAM_FRAGMENT,
    transparent: true, depthWrite: false, side: THREE.DoubleSide, blending: THREE.AdditiveBlending,
  });
  const mesh = new THREE.Mesh(geo, mat);
  mesh.renderOrder = 6;
  return markDebug(mesh);
}

// --------------------------------------------------------------------------------------------------------- glows
let glowTex = null;
export function glowTexture() {
  if (glowTex) return glowTex;
  const c = document.createElement('canvas');
  c.width = c.height = 128;
  const g = c.getContext('2d');
  const grad = g.createRadialGradient(64, 64, 0, 64, 64, 64);
  grad.addColorStop(0, 'rgba(255,255,255,1)');
  grad.addColorStop(0.18, 'rgba(255,255,255,0.75)');
  grad.addColorStop(0.45, 'rgba(255,255,255,0.18)');
  grad.addColorStop(1, 'rgba(255,255,255,0)');
  g.fillStyle = grad;
  g.fillRect(0, 0, 128, 128);
  glowTex = new THREE.CanvasTexture(c);
  glowTex.colorSpace = THREE.SRGBColorSpace;
  return glowTex;
}

export function glowSprite(color, size = 0.5, opacity = 0.9) {
  const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: glowTexture(), color, transparent: true, opacity, depthWrite: false, blending: THREE.AdditiveBlending }));
  sprite.scale.set(size, size, size);
  sprite.renderOrder = 7;
  return markDebug(sprite);
}

// Points along a set of segments as glowing beads (open edges, seams). segments: flat [ax,ay,az,bx,by,bz, ...]
export function beadTrail(segments, color, { spacing = 0.03, size = 0.035 } = {}) {
  const pts = [];
  for (let i = 0; i < segments.length; i += 6) {
    const ax = segments[i], ay = segments[i + 1], az = segments[i + 2], bx = segments[i + 3], by = segments[i + 4], bz = segments[i + 5];
    const len = Math.hypot(bx - ax, by - ay, bz - az);
    const n = Math.max(1, Math.round(len / spacing));
    for (let k = 0; k <= n; k++) { const t = k / n; pts.push(ax + (bx - ax) * t, ay + (by - ay) * t, az + (bz - az) * t); }
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(pts, 3));
  const points = new THREE.Points(geo, new THREE.PointsMaterial({ map: glowTexture(), color, size, sizeAttenuation: true, transparent: true, depthWrite: false, depthTest: false, blending: THREE.AdditiveBlending }));
  points.renderOrder = 20;
  return markDebug(points);
}

// ------------------------------------------------------------------------------------------------------- solids
const solid = (color, emissive = 0.35) => new THREE.MeshStandardMaterial({ color, emissive: color, emissiveIntensity: emissive, roughness: 0.35, metalness: 0.1 });

// Shaded arrow along +X.
export function solidArrow(length, color) {
  const g = new THREE.Group();
  const head = Math.min(length * 0.28, 0.25), r = Math.max(0.012, length * 0.025);
  const shaft = new THREE.Mesh(new THREE.CylinderGeometry(r, r, length - head, 14), solid(color));
  shaft.rotation.z = -Math.PI / 2;
  shaft.position.x = (length - head) / 2;
  const tip = new THREE.Mesh(new THREE.ConeGeometry(r * 3, head, 20), solid(color, 0.5));
  tip.rotation.z = -Math.PI / 2;
  tip.position.x = length - head / 2;
  g.add(shaft, tip);
  return markDebug(g);
}

// The anchor of a class (and pivots inside it): a flat ring with a bright core and three axis beads on short stems.
export function anchorMark(size = 0.12, color = '#e8e8e8') {
  const g = new THREE.Group();
  const ring = new THREE.Mesh(new THREE.TorusGeometry(size, size * 0.09, 10, 48), solid(color, 0.25));
  ring.rotation.x = Math.PI / 2;
  const core = new THREE.Mesh(new THREE.SphereGeometry(size * 0.22, 16, 12), solid(color, 0.6));
  g.add(ring, core);
  const axes = [['#e0605a', [1, 0, 0]], ['#6fcf6a', [0, 1, 0]], ['#5a8de0', [0, 0, 1]]];
  for (const [c, d] of axes) {
    const stem = new THREE.Mesh(new THREE.CylinderGeometry(size * 0.035, size * 0.035, size * 1.4, 8), solid(c, 0.3));
    const bead = new THREE.Mesh(new THREE.SphereGeometry(size * 0.13, 12, 10), solid(c, 0.55));
    const dir = new THREE.Vector3(...d);
    stem.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir);
    stem.position.copy(dir).multiplyScalar(size * 0.7);
    bead.position.copy(dir).multiplyScalar(size * 1.45);
    g.add(stem, bead);
  }
  g.add(glowSprite(color, size * 3, 0.35));
  return markDebug(g);
}

// ------------------------------------------------------------------------------------------------------ selection
// The selected surfaces themselves glow amber toward their silhouette: the fresnel term is injected into the mesh's
// own material (a per-selection clone), so there is no second mesh, no fill and no z-fighting, just the surface lit
// from the edges, breathing slowly.
const HALO_COLOR = new THREE.Color('#ffb238');

export function haloCompile(shader) {
  shader.uniforms.uHaloTime = clock;
  shader.uniforms.uHaloColor = { value: HALO_COLOR };
  shader.vertexShader = shader.vertexShader
    .replace('void main() {', 'varying vec3 vHaloN;\nvarying vec3 vHaloP;\nvoid main() {')
    .replace('#include <project_vertex>', '#include <project_vertex>\n  vHaloN = normalMatrix * normal;\n  vHaloP = mvPosition.xyz;');
  shader.fragmentShader = shader.fragmentShader
    .replace('void main() {', 'uniform float uHaloTime;\nuniform vec3 uHaloColor;\nvarying vec3 vHaloN;\nvarying vec3 vHaloP;\nvoid main() {')
    .replace(/\}\s*$/, `
  vec3 haloN = length(vHaloN) > 0.01 ? normalize(vHaloN) : normalize(cross(dFdx(vHaloP), dFdy(vHaloP)));
  float haloRim = pow(1.0 - abs(dot(haloN, normalize(-vHaloP))), 2.4);
  float haloBreathe = 0.8 + 0.2 * sin(uHaloTime * 3.2);
  gl_FragColor.rgb = mix(gl_FragColor.rgb, uHaloColor, clamp(haloRim * 0.85 * haloBreathe, 0.0, 1.0)) + uHaloColor * 0.035;
}`);
}

function haloOf(material) {
  const m = material.clone();
  m.onBeforeCompile = haloCompile;
  m.customProgramCacheKey = () => `halo:${material.type}`;
  return m;
}

// Tints every surface of `node` (child components excluded). Returns a function that restores the materials.
export function addHalo(node) {
  const swapped = [];
  const visit = (o) => {
    if (o !== node && o.userData.componentId) return;
    if (o.isMesh && o.material && !o.userData.haloOriginal) {
      const list = Array.isArray(o.material) ? o.material : [o.material];
      const first = list[0];
      if (first && first.visible !== false && !first.isShaderMaterial) {
        o.userData.haloOriginal = o.material;
        o.material = Array.isArray(o.material) ? list.map(haloOf) : haloOf(o.material);
        swapped.push(o);
      }
    }
    for (const c of o.children) visit(c);
  };
  visit(node);
  return () => {
    for (const o of swapped) {
      const tinted = Array.isArray(o.material) ? o.material : [o.material];
      for (const m of tinted) m.dispose();
      o.material = o.userData.haloOriginal;
      delete o.userData.haloOriginal;
    }
  };
}

// ------------------------------------------------------------------------------------------------- camera model
// A compact cinema camera facing +X: rounded graphite body, PL-style mount, lens with a knurled focus ring and a
// coated front element, flared hood, top handle, flip-out monitor with a lit screen, rear battery and a tally lamp.
// Returns { group, lensFront } (x of the front element, where the view beam starts).
export function cameraModel({ active = false } = {}) {
  const g = new THREE.Group();
  const graphite = new THREE.MeshStandardMaterial({ color: '#25282f', roughness: 0.42, metalness: 0.35 });
  const satin = new THREE.MeshStandardMaterial({ color: '#3a3e47', roughness: 0.3, metalness: 0.7 });
  const rubber = new THREE.MeshStandardMaterial({ color: '#141518', roughness: 0.85, metalness: 0 });
  const metal = new THREE.MeshStandardMaterial({ color: '#9aa0aa', roughness: 0.22, metalness: 1 });
  const glass = new THREE.MeshStandardMaterial({ color: '#0d1626', roughness: 0.05, metalness: 0.9, emissive: '#2a3f7a', emissiveIntensity: 0.35 });
  const add = (geo, mat, x = 0, y = 0, z = 0) => { const m = new THREE.Mesh(geo, mat); m.position.set(x, y, z); m.castShadow = true; g.add(m); return m; };
  const alongX = (geo) => geo.rotateZ(-Math.PI / 2);

  // Body: rounded profile in X/Y, extruded across Z, centred.
  const L = 0.24, H = 0.15, W = 0.12, r = 0.028;
  const shape = new THREE.Shape();
  shape.moveTo(-L / 2 + r, -H / 2);
  shape.lineTo(L / 2 - r, -H / 2); shape.quadraticCurveTo(L / 2, -H / 2, L / 2, -H / 2 + r);
  shape.lineTo(L / 2, H / 2 - r); shape.quadraticCurveTo(L / 2, H / 2, L / 2 - r, H / 2);
  shape.lineTo(-L / 2 + r, H / 2); shape.quadraticCurveTo(-L / 2, H / 2, -L / 2, H / 2 - r);
  shape.lineTo(-L / 2, -H / 2 + r); shape.quadraticCurveTo(-L / 2, -H / 2, -L / 2 + r, -H / 2);
  const bevel = 0.008;
  const body = new THREE.ExtrudeGeometry(shape, { depth: W - bevel * 2, bevelEnabled: true, bevelThickness: bevel, bevelSize: bevel, bevelSegments: 3, curveSegments: 6 });
  body.translate(0, 0, -(W - bevel * 2) / 2);
  add(body, graphite);
  // Side vent strip and a recessed panel line on the operator side.
  for (let i = 0; i < 6; i++) add(new THREE.BoxGeometry(0.006, 0.07, 0.004), rubber, -0.07 + i * 0.014, 0.005, -W / 2 - 0.001);

  // Mount and lens (lathe profile along +X).
  const mountX = L / 2 + bevel;
  add(alongX(new THREE.CylinderGeometry(0.058, 0.058, 0.014, 40)), satin, mountX + 0.007);
  add(alongX(new THREE.CylinderGeometry(0.05, 0.05, 0.02, 40)), graphite, mountX + 0.024);
  const ring = alongX(new THREE.CylinderGeometry(0.056, 0.056, 0.04, 96, 1));
  const pos = ring.attributes.position;
  for (let i = 0; i < pos.count; i++) { // knurl: alternate the radius around the ring
    const y = pos.getY(i), z = pos.getZ(i), a = Math.atan2(z, y), k = Math.sin(a * 48) > 0 ? 1 : 0.95;
    if (Math.hypot(y, z) > 0.01) { pos.setY(i, y * k); pos.setZ(i, z * k); }
  }
  ring.computeVertexNormals();
  add(ring, rubber, mountX + 0.054);
  add(alongX(new THREE.CylinderGeometry(0.052, 0.052, 0.006, 40)), metal, mountX + 0.077); // witness ring
  const hoodProfile = [new THREE.Vector2(0.05, 0), new THREE.Vector2(0.053, 0.02), new THREE.Vector2(0.07, 0.06), new THREE.Vector2(0.072, 0.064), new THREE.Vector2(0.066, 0.064), new THREE.Vector2(0.049, 0.02)];
  const hood = alongX(new THREE.LatheGeometry(hoodProfile, 48));
  add(hood, rubber, mountX + 0.08).material = new THREE.MeshStandardMaterial({ color: '#141518', roughness: 0.85, side: THREE.DoubleSide });
  const lensFront = mountX + 0.095;
  const element = alongX(new THREE.SphereGeometry(0.06, 40, 12, 0, Math.PI * 2, 0, 0.75));
  element.scale(0.45, 1, 1);
  add(element, glass, lensFront - 0.03);

  // Top handle: two posts and a grip.
  add(new THREE.CylinderGeometry(0.008, 0.008, 0.035, 12), satin, -0.07, H / 2 + 0.017);
  add(new THREE.CylinderGeometry(0.008, 0.008, 0.035, 12), satin, 0.07, H / 2 + 0.017);
  add(alongX(new THREE.CapsuleGeometry(0.012, 0.16, 4, 16)), rubber, 0, H / 2 + 0.04);

  // Flip-out monitor on the operator side (−Z), screen facing back.
  const arm = add(new THREE.BoxGeometry(0.02, 0.012, 0.03), satin, 0.06, 0.03, -W / 2 - 0.015);
  arm.castShadow = false;
  add(new THREE.BoxGeometry(0.11, 0.07, 0.008), graphite, 0.03, 0.03, -W / 2 - 0.034);
  const screen = add(new THREE.PlaneGeometry(0.096, 0.056), new THREE.MeshBasicMaterial({ color: active ? '#bcd3ff' : '#4a5a78' }), 0.03, 0.03, -W / 2 - 0.0385);
  screen.rotation.y = Math.PI;

  // Rear battery and tally lamp.
  add(new THREE.BoxGeometry(0.03, 0.11, 0.09), rubber, -L / 2 - 0.02, -0.005);
  add(new THREE.BoxGeometry(0.004, 0.02, 0.05), metal, -L / 2 - 0.037, 0.02);
  add(new THREE.SphereGeometry(0.008, 12, 8), new THREE.MeshBasicMaterial({ color: active ? '#ff4d3d' : '#5a1f1b' }), L / 2 - 0.02, H / 2 + 0.004, 0.035);
  if (active) { const tally = glowSprite('#ff4d3d', 0.08, 0.9); tally.position.set(L / 2 - 0.02, H / 2 + 0.004, 0.035); g.add(tally); }
  return { group: g, lensFront };
}

// ------------------------------------------------------------------------------------------------------ dot floor
const FLOOR_VERTEX = /* glsl */`
  varying vec3 vLocal;
  varying vec3 vWorld;
  void main() {
    vLocal = position;
    vec4 world = modelMatrix * vec4(position, 1.0);
    vWorld = world.xyz;
    gl_Position = projectionMatrix * viewMatrix * world;
  }`;

const FLOOR_FRAGMENT = /* glsl */`
  uniform vec3 uFocus;
  uniform float uRadius;
  varying vec3 vLocal;
  varying vec3 vWorld;
  float dots(vec2 p, float spacing, float radius, float px) {
    vec2 g = (fract(p / spacing + 0.5) - 0.5) * spacing;
    return 1.0 - smoothstep(radius - px, radius + px, length(g));
  }
  void main() {
    vec2 p = vLocal.xy;
    float px = max(fwidth(p.x), fwidth(p.y));
    float major = dots(p, 1.0, max(0.018, px * 1.1), px) * (1.0 - smoothstep(0.06, 0.3, px));
    float minor = dots(p, 0.25, max(0.007, px * 0.8), px) * (1.0 - smoothstep(0.012, 0.045, px));
    // Dots on the axes are tinted: red along X, blue along Z (plane local y is world -Z).
    vec2 cell = floor(p + 0.5);
    vec3 col = vec3(0.42, 0.44, 0.5);
    if (abs(cell.y) < 0.5) col = vec3(0.85, 0.36, 0.33);
    if (abs(cell.x) < 0.5) col = vec3(0.33, 0.52, 0.88);
    float fade = 1.0 - smoothstep(uRadius * 0.25, uRadius, distance(vWorld.xz, uFocus.xz));
    float pool = (1.0 - smoothstep(0.0, uRadius * 0.6, distance(vWorld.xz, uFocus.xz))) * 0.05;
    float a = max(major * 0.8, minor * 0.4) * fade;
    gl_FragColor = vec4(mix(vec3(0.09, 0.09, 0.11), col, step(0.001, a)), a + pool);
  }`;

export function dotFloor(size = 240) {
  const mat = new THREE.ShaderMaterial({
    uniforms: { uFocus: { value: new THREE.Vector3() }, uRadius: { value: 12 } },
    vertexShader: FLOOR_VERTEX, fragmentShader: FLOOR_FRAGMENT, transparent: true, depthWrite: false,
    polygonOffset: true, polygonOffsetFactor: 1, polygonOffsetUnits: 1,
  });
  const mesh = new THREE.Mesh(new THREE.PlaneGeometry(size, size), mat);
  mesh.rotation.x = -Math.PI / 2;
  mesh.renderOrder = -1;
  mesh.raycast = () => {};
  return markDebug(mesh);
}

// -------------------------------------------------------------------------------------------------------- density
const RAMP = ['#306bf2', '#29c7c2', '#59d15c', '#f7cc40', '#f04d3d'].map((c) => new THREE.Color(c));

// Heat colour per triangle of a non-indexed geometry: red where triangles are small (dense), blue where large.
export function densityColors(positions) {
  const tris = positions.length / 9;
  const areas = new Float32Array(tris);
  for (let t = 0; t < tris; t++) {
    const o = t * 9;
    const ux = positions[o + 3] - positions[o], uy = positions[o + 4] - positions[o + 1], uz = positions[o + 5] - positions[o + 2];
    const vx = positions[o + 6] - positions[o], vy = positions[o + 7] - positions[o + 1], vz = positions[o + 8] - positions[o + 2];
    areas[t] = 0.5 * Math.hypot(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx) + 1e-12;
  }
  const sorted = Float32Array.from(areas).sort();
  const median = sorted[Math.floor(tris / 2)] || 1;
  const colors = new Float32Array(tris * 9);
  const c = new THREE.Color();
  for (let t = 0; t < tris; t++) {
    // log2(area / median) in [-3, 3] → dense (red) … sparse (blue)
    const x = Math.max(0, Math.min(1, 0.5 - Math.log2(areas[t] / median) / 6));
    const f = x * (RAMP.length - 1), i = Math.min(RAMP.length - 2, Math.floor(f));
    c.copy(RAMP[i]).lerp(RAMP[i + 1], f - i);
    for (let k = 0; k < 3; k++) { colors[t * 9 + k * 3] = c.r; colors[t * 9 + k * 3 + 1] = c.g; colors[t * 9 + k * 3 + 2] = c.b; }
  }
  return colors;
}

// ---------------------------------------------------------------------------------------------------------- utils
// Debug visuals: not pickable, not part of framing bounds.
export function markDebug(object) {
  object.traverse((o) => { o.userData.debug = true; o.raycast = () => {}; });
  return object;
}
