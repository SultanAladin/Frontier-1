import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { bakeWorld, visibleTileKeys } from '../world/world-map.js';

// World-map tiles share boundaries and scale; terrain bakes supply actual heights.
export const PREVIEW_TILE_SPAN = 240;
export const PREVIEW_HEIGHT_SCALE = .16;

export function createTerrainTileGeometry(data, tileX, tileY, centerX = 0, centerY = 0) {
  const n = data.size;
  const geometry = new THREE.PlaneGeometry(PREVIEW_TILE_SPAN, PREVIEW_TILE_SPAN, n - 1, n - 1);
  geometry.rotateX(-Math.PI / 2);
  geometry.translate((tileX + .5 - centerX) * PREVIEW_TILE_SPAN, 0, (tileY + .5 - centerY) * PREVIEW_TILE_SPAN);
  const pos = geometry.attributes.position;
  const colors = new Float32Array(n * n * 3);
  for (let i = 0; i < n * n; i++) {
    const land = data.coverage[i];
    // The unedited mask is flat; the baked Terrain Studio heightfield replaces it.
    const h = data.edited[i] ? data.heights[i] : 8 * land;
    pos.setY(i, h * PREVIEW_HEIGHT_SCALE - (1 - land) * 2.2);
    colors[i * 3] = .16 + .19 * land;
    colors[i * 3 + 1] = .21 + .21 * land;
    colors[i * 3 + 2] = .19 + .11 * land;
  }
  geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
  geometry.computeVertexNormals();
  geometry.computeBoundingSphere();
  return geometry;
}

// This is a visual ocean surface, not a shallow-water / erosion simulation.
export const WORLD_WATER_VERTEX = `
uniform float uTime;
uniform float uSeaLevel;
varying vec3 vWorldPosition;
varying vec3 vWaterNormal;
void main() {
  vec4 world = modelMatrix * vec4(position, 1.0);
  float a = world.x * 0.052 + world.z * 0.032 + uTime * 0.8;
  float b = world.x * -0.021 + world.z * 0.071 + uTime * 0.56;
  float c = world.x * 0.13 + world.z * -0.085 + uTime * 1.15;
  world.y = uSeaLevel + 0.38 * sin(a) + 0.20 * sin(b) + 0.06 * sin(c);
  float dx = 0.38 * 0.052 * cos(a) - 0.20 * 0.021 * cos(b) + 0.06 * 0.13 * cos(c);
  float dz = 0.38 * 0.032 * cos(a) + 0.20 * 0.071 * cos(b) - 0.06 * 0.085 * cos(c);
  vWaterNormal = normalize(vec3(-dx, 1.0, -dz));
  vWorldPosition = world.xyz;
  gl_Position = projectionMatrix * viewMatrix * world;
}`;
export const WORLD_WATER_FRAGMENT = `
precision highp float;
varying vec3 vWorldPosition;
varying vec3 vWaterNormal;
uniform float uTime;
void main() {
  // Fragment-scale crossing ripples retain detail even when a wide ocean
  // needs a comparatively coarse vertex mesh.
  float a = vWorldPosition.x * 0.18 + vWorldPosition.z * 0.11 + uTime * 0.85;
  float b = vWorldPosition.x * -0.09 + vWorldPosition.z * 0.24 + uTime * 0.61;
  vec3 N = normalize(vWaterNormal + vec3(0.10 * cos(a) - 0.045 * cos(b), 0.0, 0.06 * cos(a) + 0.10 * cos(b)));
  vec3 V = normalize(cameraPosition - vWorldPosition);
  vec3 L = normalize(vec3(0.42, 0.82, 0.38));
  float fresnel = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 4.0);
  float sparkle = pow(max(dot(reflect(-L, N), V), 0.0), 80.0);
  float diffuse = max(dot(N, L), 0.0);
  vec3 water = mix(vec3(0.025, 0.18, 0.23), vec3(0.24, 0.43, 0.47), fresnel * 0.7);
  water *= 0.86 + 0.14 * diffuse;
  water += vec3(sparkle * 0.55);
  gl_FragColor = vec4(water, 1.0);
}`;

export class WorldPreview {
  constructor(element) {
    this.element = element;
    this.renderer = new THREE.WebGLRenderer({ antialias: true });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    element.append(this.renderer.domElement);
    this.scene = new THREE.Scene(); this.scene.background = new THREE.Color('#1b262d');
    this.camera = new THREE.PerspectiveCamera(45, 1, .1, 10000000);
    this.camera.position.set(220, 260, 320);
    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.target.set(0, 0, 0); this.controls.update();
    this.scene.add(new THREE.HemisphereLight(0xd8ebff, 0x525548, 2.0));
    const sun = new THREE.DirectionalLight(0xffffff, 2); sun.position.set(100, 200, 70); this.scene.add(sun);
    this.terrainGroup = new THREE.Group(); this.scene.add(this.terrainGroup);
    this.terrainMaterial = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 1, side: THREE.DoubleSide });
    this.waterMaterial = new THREE.ShaderMaterial({
      uniforms: { uTime: { value: 0 }, uSeaLevel: { value: 0 } },
      vertexShader: WORLD_WATER_VERTEX, fragmentShader: WORLD_WATER_FRAGMENT,
      transparent: false, depthWrite: true, side: THREE.DoubleSide,
    });
    this.clock = new THREE.Clock(); this.active = false;
    const tick = () => {
      requestAnimationFrame(tick);
      if (!this.active || !this.element.clientWidth || !this.element.clientHeight) return;
      const width = this.element.clientWidth, height = this.element.clientHeight;
      this.renderer.setSize(width, height, false); this.camera.aspect = width / height; this.camera.updateProjectionMatrix();
      this.waterMaterial.uniforms.uTime.value = this.clock.getElapsedTime();
      this.controls.update(); this.renderer.render(this.scene, this.camera);
    };
    tick();
  }
  rebuild(world, resolution, { water = true, seaLevel = 0, fit = false } = {}) {
    for (const child of [...this.terrainGroup.children]) { this.terrainGroup.remove(child); child.geometry.dispose(); }
    if (this.water) { this.scene.remove(this.water); this.water.geometry.dispose(); }
    const tiles = visibleTileKeys(world).map(key => key.split(',').map(Number));
    let minX = 0, maxX = 1, minY = 0, maxY = 1;
    if (tiles.length) {
      minX = Infinity; maxX = -Infinity; minY = Infinity; maxY = -Infinity;
      for (const [x, y] of tiles) {
        minX = Math.min(minX, x); maxX = Math.max(maxX, x + 1);
        minY = Math.min(minY, y); maxY = Math.max(maxY, y + 1);
      }
    }
    const cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
    const frameChanged = this.centerX !== cx || this.centerY !== cy;
    this.centerX = cx; this.centerY = cy;
    for (const [x, y] of tiles) {
      const data = bakeWorld(world, resolution, { x, y, width: 1, height: 1 });
      const mesh = new THREE.Mesh(createTerrainTileGeometry(data, x, y, cx, cy), this.terrainMaterial);
      mesh.name = `Terrain tile ${x}, ${y}`; this.terrainGroup.add(mesh);
    }
    const spanX = (maxX - minX + 4) * PREVIEW_TILE_SPAN, spanY = (maxY - minY + 4) * PREVIEW_TILE_SPAN;
    const geometry = new THREE.PlaneGeometry(spanX, spanY, 96, 96); geometry.rotateX(-Math.PI / 2);
    this.water = new THREE.Mesh(geometry, this.waterMaterial); this.water.visible = water;
    this.waterMaterial.uniforms.uSeaLevel.value = seaLevel * PREVIEW_HEIGHT_SCALE;
    this.scene.add(this.water);
    if (fit || !this.fitted || frameChanged) {
      // Frame the painted terrain, not the padded ocean plane.
      const extent = Math.max(maxX - minX, maxY - minY, 1) * PREVIEW_TILE_SPAN * 1.1;
      this.controls.target.set(0, 0, 0); this.camera.position.set(extent * .7, extent * .8, extent); this.camera.far = Math.max(20000, extent * 20); this.camera.updateProjectionMatrix(); this.controls.update(); this.fitted = true;
    }
    return { tileCount: tiles.length, resolution };
  }
  setWater(visible, level) {
    if (this.water) this.water.visible = visible;
    this.waterMaterial.uniforms.uSeaLevel.value = level * PREVIEW_HEIGHT_SCALE;
  }
}
