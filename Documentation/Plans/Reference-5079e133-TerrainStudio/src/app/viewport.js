/**
 * Three.js WebGL2 AAA Rock & Cliff Studio Viewport.
 *
 * Features:
 * - Honest Flat-Material Stone Rendering (zero bitmap noise, zero fake sedimentary stripe textures)
 * - Volumetric SDF Vertex AO + Geometric Curvature shader integration
 * - Dynamic tight-frustum 2048x2048 PCFSoftShadowMap directional sunlight rig
 * - 5 Inspection Modes: AAA Flat Stone, Pure Matte Clay, Lithology Tone, AO Grayscale, Curvature/Normal Diagnostics
 * - 1.80m Human Scale Reference Figure + Meter Grid Ground Shadow Catcher
 */

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { LITHOLOGY_PALETTES } from '../geology/presets.js';

export class RockStudioViewport {
  constructor(container) {
    this.container = container;
    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color('#171a1f');
    this.scene.fog = new THREE.FogExp2('#171a1f', 0.0045);

    const w = container.clientWidth || window.innerWidth;
    const h = container.clientHeight || window.innerHeight;

    this.camera = new THREE.PerspectiveCamera(40, w / h, 0.08, 600);
    this.camera.position.set(14, 9, 20);

    this.renderer = new THREE.WebGLRenderer({
      antialias: true,
      powerPreference: 'high-performance',
    });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    this.renderer.setSize(w, h);
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.04;
    container.appendChild(this.renderer.domElement);

    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.065;
    this.controls.maxPolarAngle = Math.PI * 0.495;
    this.controls.minDistance = 0.8;
    this.controls.maxDistance = 240;
    this.controls.target.set(0, 3.5, 0);
    // Reserve Right Mouse Button for Unreal Engine Free-Look Mouselook (MMB / Shift+LMB still pans)
    this.controls.mouseButtons = {
      LEFT: THREE.MOUSE.ROTATE,
      MIDDLE: THREE.MOUSE.PAN,
      RIGHT: null,
    };

    this._keysDown = new Set();
    this._rmbDown = false;
    this.flySpeed = 14.0;
    this._bindUnrealFlyControls();

    this.turntable = false;
    this.sunAzimuthDeg = 42;
    this.sunElevationDeg = 44;
    this.shadingMode = 'lithology'; // 'lithology' | 'flat-stone' | 'pure-clay' | 'ao' | 'curvature' | 'normals'
    this.currentLithology = 'navajoRed';
    this.boundRadius = 10;
    this.formationDims = [16, 10, 8];

    this.shaderUniforms = {
      uCurvatureStrength: { value: 0.85 },
      uAoStrength: { value: 0.92 },
      uUseVertexAlbedo: { value: 0.0 },
      uDebugMode: { value: 0 },
      uFabricType: { value: 0 },
      uChemStrength: { value: 1.0 },
    };
    this.hasCustomAlbedo = false;
    this.manualLithologyOverride = false;

    this._initEnvironmentAndLights();
    this._initGroundAndScaleRef();
    this._initRockMaterial();

    this.rockMesh = null;
    this.wireMesh = null;
    this.wireframeVisible = false;

    this._onResize = () => {
      if (this.active) this.resize();
    };
    window.addEventListener('resize', this._onResize);

    this.active = true;
    this._clock = new THREE.Clock();
    this._raf = 0;
    this._animate = this._animate.bind(this);
    this._animate();
  }

  setActive(flag) {
    this.active = Boolean(flag);
    this.controls.enabled = this.active;
    if (this.active) {
      this._clock.getDelta();
      this.resize();
    }
  }

  getCameraAdapter() {
    this.camera.updateMatrixWorld();
    this.camera.updateProjectionMatrix();
    const vp = new THREE.Matrix4().multiplyMatrices(
      this.camera.projectionMatrix,
      this.camera.matrixWorldInverse
    );
    const e = this.camera.matrixWorld.elements;
    const eye = [this.camera.position.x, this.camera.position.y, this.camera.position.z];
    // Camera world right = +X col, up = +Y col, forward = -Z col
    const right = [e[0], e[1], e[2]];
    const up = [e[4], e[5], e[6]];
    const forward = [-e[8], -e[9], -e[10]];
    const width = this.container.clientWidth || 1;
    const height = this.container.clientHeight || 1;
    const fovDeg = this.camera.fov;

    return {
      viewProj: vp.elements,
      width,
      height,
      eye,
      forward,
      right,
      up,
      fovDeg,
      minWorldY: 0.0,
      baseFloorY: () => 0.0,
      raycastGround: (clientX, clientY) => {
        const rect = this.container.getBoundingClientRect();
        const ndcX = ((clientX - rect.left) / Math.max(1, rect.width)) * 2 - 1;
        const ndcY = 1 - ((clientY - rect.top) / Math.max(1, rect.height)) * 2;
        const aspect = width / Math.max(1, height);
        const tanHalf = Math.tan((fovDeg * Math.PI) / 360);
        const dx = forward[0] + right[0] * ndcX * aspect * tanHalf + up[0] * ndcY * tanHalf;
        const dy = forward[1] + right[1] * ndcX * aspect * tanHalf + up[1] * ndcY * tanHalf;
        const dz = forward[2] + right[2] * ndcX * aspect * tanHalf + up[2] * ndcY * tanHalf;
        if (Math.abs(dy) < 1e-4) return null;
        const t = -eye[1] / dy;
        if (t < 0) return null;
        return [
          Math.max(-25, Math.min(25, eye[0] + dx * t)),
          0.0,
          Math.max(-25, Math.min(25, eye[2] + dz * t)),
        ];
      },
    };
  }

  _initEnvironmentAndLights() {
    // Subtle procedural studio sky/ground PMREM environment for realistic rough stone micro-specular
    const pmrem = new THREE.PMREMGenerator(this.renderer);
    const envScene = new THREE.Scene();
    const envGeo = new THREE.SphereGeometry(20, 24, 16);
    const envMat = new THREE.ShaderMaterial({
      side: THREE.BackSide,
      vertexShader: `
        varying vec3 vPos;
        void main() {
          vPos = position;
          gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
        }
      `,
      fragmentShader: `
        varying vec3 vPos;
        void main() {
          float h = normalize(vPos).y;
          vec3 sky = mix(vec3(0.24, 0.27, 0.32), vec3(0.42, 0.50, 0.62), clamp(h, 0.0, 1.0));
          vec3 gnd = vec3(0.14, 0.13, 0.12);
          vec3 col = h >= 0.0 ? sky : mix(vec3(0.20, 0.19, 0.18), gnd, clamp(-h * 2.0, 0.0, 1.0));
          gl_FragColor = vec4(col, 1.0);
        }
      `,
    });
    envScene.add(new THREE.Mesh(envGeo, envMat));
    this.envRT = pmrem.fromScene(envScene, 0.04);
    this.scene.environment = this.envRT.texture;
    this.scene.environmentIntensity = 0.55;
    envGeo.dispose();
    envMat.dispose();
    pmrem.dispose();

    // Key Sun Directional Light
    this.sunLight = new THREE.DirectionalLight('#fff6ea', 2.65);
    this.sunLight.castShadow = true;
    this.sunLight.shadow.mapSize.set(2048, 2048);
    this.sunLight.shadow.bias = -0.00035;
    this.sunLight.shadow.normalBias = 0.035;
    this.scene.add(this.sunLight);
    this.scene.add(this.sunLight.target);

    // Sky/Ground Hemisphere Fill
    this.hemiLight = new THREE.HemisphereLight('#a9c0db', '#4a433b', 0.62);
    this.scene.add(this.hemiLight);

    // Cool Rim Backlight to separate dark overhangs from background
    this.rimLight = new THREE.DirectionalLight('#8fb0d8', 0.42);
    this.scene.add(this.rimLight);

    this.updateSunPosition();
  }

  _initGroundAndScaleRef() {
    // Studio shadow-catching floor with subtle 1m & 5m scale grid
    const groundGeo = new THREE.CircleGeometry(120, 72);
    groundGeo.rotateX(-Math.PI / 2);

    const groundMat = new THREE.MeshStandardMaterial({
      color: '#1d2127',
      roughness: 0.96,
      metalness: 0.0,
    });
    groundMat.onBeforeCompile = (shader) => {
      shader.vertexShader = shader.vertexShader.replace(
        '#include <common>',
        '#include <common>\nvarying vec3 vGroundWorld;'
      ).replace(
        '#include <begin_vertex>',
        '#include <begin_vertex>\nvGroundWorld = (modelMatrix * vec4(transformed, 1.0)).xyz;'
      );
      shader.fragmentShader = shader.fragmentShader.replace(
        '#include <common>',
        '#include <common>\nvarying vec3 vGroundWorld;'
      ).replace(
        '#include <color_fragment>',
        `#include <color_fragment>
         vec2 c1 = abs(fract(vGroundWorld.xz - 0.5) - 0.5) / fwidth(vGroundWorld.xz);
         float line1 = 1.0 - min(min(c1.x, c1.y), 1.0);
         vec2 c5 = abs(fract(vGroundWorld.xz * 0.2 - 0.5) - 0.5) / fwidth(vGroundWorld.xz * 0.2);
         float line5 = 1.0 - min(min(c5.x, c5.y), 1.0);
         float distFade = exp(-length(vGroundWorld.xz) * 0.028);
         diffuseColor.rgb += vec3(0.035 * line1 + 0.075 * line5) * distFade;`
      );
    };

    this.groundMesh = new THREE.Mesh(groundGeo, groundMat);
    this.groundMesh.position.y = -0.005;
    this.groundMesh.receiveShadow = true;
    this.scene.add(this.groundMesh);

    // 1.80m Human Scale Reference Figure (essential in AAA environment production!)
    this.scaleHuman = this._buildHumanScaleFigure();
    this.scaleHuman.visible = true;
    this.scene.add(this.scaleHuman);
  }

  _buildHumanScaleFigure() {
    const group = new THREE.Group();
    const mat = new THREE.MeshStandardMaterial({
      color: '#d9822b',
      roughness: 0.55,
      metalness: 0.05,
    });

    // Legs (0 .. 0.88m)
    const legGeo = new THREE.CapsuleGeometry(0.075, 0.72, 6, 10);
    const legL = new THREE.Mesh(legGeo, mat);
    legL.position.set(-0.10, 0.44, 0);
    legL.castShadow = true;
    const legR = legL.clone();
    legR.position.x = 0.10;

    // Torso (0.86 .. 1.48m)
    const torsoGeo = new THREE.CapsuleGeometry(0.155, 0.40, 8, 12);
    const torso = new THREE.Mesh(torsoGeo, mat);
    torso.position.set(0, 1.17, 0);
    torso.castShadow = true;

    // Arms
    const armGeo = new THREE.CapsuleGeometry(0.055, 0.52, 6, 8);
    const armL = new THREE.Mesh(armGeo, mat);
    armL.position.set(-0.23, 1.12, 0);
    armL.rotation.z = 0.08;
    armL.castShadow = true;
    const armR = armL.clone();
    armR.position.x = 0.23;
    armR.rotation.z = -0.08;

    // Head (top at 1.80m)
    const headGeo = new THREE.SphereGeometry(0.115, 14, 12);
    const head = new THREE.Mesh(headGeo, mat);
    head.position.set(0, 1.685, 0);
    head.castShadow = true;

    // Base plinth ring (0.5m radius)
    const ringGeo = new THREE.RingGeometry(0.32, 0.36, 32);
    ringGeo.rotateX(-Math.PI / 2);
    const ringMat = new THREE.MeshBasicMaterial({ color: '#d9822b', side: THREE.DoubleSide });
    const ring = new THREE.Mesh(ringGeo, ringMat);
    ring.position.y = 0.004;

    group.add(legL, legR, torso, armL, armR, head, ring);
    return group;
  }

  _initRockMaterial() {
    this.rockMaterial = new THREE.MeshStandardMaterial({
      color: '#7b7873',
      roughness: 0.85,
      metalness: 0.0,
      dithering: true,
    });

    this.rockMaterial.onBeforeCompile = (shader) => {
      shader.uniforms.uCurvatureStrength = this.shaderUniforms.uCurvatureStrength;
      shader.uniforms.uAoStrength = this.shaderUniforms.uAoStrength;
      shader.uniforms.uUseVertexAlbedo = this.shaderUniforms.uUseVertexAlbedo;
      shader.uniforms.uDebugMode = this.shaderUniforms.uDebugMode;
      shader.uniforms.uFabricType = this.shaderUniforms.uFabricType;
      shader.uniforms.uChemStrength = this.shaderUniforms.uChemStrength;

      shader.vertexShader = shader.vertexShader
        .replace(
          '#include <common>',
          `#include <common>
           attribute vec4 aRockMask;
           attribute vec3 aRockAlbedo;
           attribute vec2 aRockStain;
           varying vec4 vRockMask;
           varying vec3 vRockAlbedo;
           varying vec2 vRockStain;
           varying vec3 vWorldNormalViz;
           varying vec3 vRockWorldPos;`
        )
        .replace(
          '#include <begin_vertex>',
          `#include <begin_vertex>
           vRockMask = aRockMask;
           vRockAlbedo = aRockAlbedo;
           vRockStain = aRockStain;
           vWorldNormalViz = normalize(mat3(modelMatrix) * objectNormal);
           vRockWorldPos = (modelMatrix * vec4(transformed, 1.0)).xyz;`
        );

      shader.fragmentShader = shader.fragmentShader
        .replace(
          '#include <common>',
          `#include <common>
           varying vec4 vRockMask;
           varying vec3 vRockAlbedo;
           varying vec2 vRockStain;
           varying vec3 vWorldNormalViz;
           varying vec3 vRockWorldPos;
           uniform float uChemStrength;
           uniform float uCurvatureStrength;
           uniform float uAoStrength;
           uniform float uUseVertexAlbedo;
           uniform int uDebugMode;
           uniform int uFabricType;

           float rsHash(vec3 p) {
             p = fract(p * 0.3183099 + vec3(0.11, 0.17, 0.23));
             p *= 17.0;
             return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
           }
           float rsNoise(vec3 x) {
             vec3 i = floor(x);
             vec3 f = fract(x);
             f = f * f * (3.0 - 2.0 * f);
             return mix(
               mix(mix(rsHash(i), rsHash(i + vec3(1,0,0)), f.x),
                   mix(rsHash(i + vec3(0,1,0)), rsHash(i + vec3(1,1,0)), f.x), f.y),
               mix(mix(rsHash(i + vec3(0,0,1)), rsHash(i + vec3(1,0,1)), f.x),
                   mix(rsHash(i + vec3(0,1,1)), rsHash(i + vec3(1,1,1)), f.x), f.y), f.z);
           }`
        )
        .replace(
          '#include <color_fragment>',
          `#include <color_fragment>
           if (uUseVertexAlbedo > 0.5) {
             diffuseColor.rgb = vRockAlbedo;
           }
           if (uFabricType > 0) {
             vec3 wp = vRockWorldPos;
             vec3 wn = normalize(vWorldNormalViz);
             if (uFabricType == 1) {
               // Hexagonal columnar basalt colonnade seams + chisel cross-joints
               vec2 q = wp.xz * 1.35;
               vec2 r = vec2(1.0, 1.7320508);
               vec2 h = r * 0.5;
               vec2 a = mod(q, r) - h;
               vec2 b = mod(q - h, r) - h;
               vec2 gv = dot(a, a) < dot(b, b) ? a : b;
               float hexEdge = 0.5 - max(abs(gv.x), abs(gv.x) * 0.5 + abs(gv.y) * 0.8660254);
               float seam = 1.0 - smoothstep(0.015, 0.085, hexEdge);
               float crossJ = 1.0 - smoothstep(0.02, 0.09, abs(fract(wp.y * 0.85 + rsNoise(wp * 0.4) * 0.35) - 0.5));
               diffuseColor.rgb *= (1.0 - 0.34 * seam - 0.16 * crossJ * (1.0 - abs(wn.y)));
               diffuseColor.rgb = mix(diffuseColor.rgb, diffuseColor.rgb * vec3(1.18, 0.94, 0.76), seam * 0.35);
             } else if (uFabricType == 2) {
               // Aeolian trough cross-bedding laminations + desert varnish sheen
               float setId = floor((wp.y + rsNoise(wp * 0.22) * 1.4) * 0.45);
               float foreset = sin(wp.y * 11.0 + wp.x * (2.6 + mod(setId, 3.0) * 1.2) + rsNoise(wp * 0.65) * 2.4);
               float lam = smoothstep(-0.6, 0.8, foreset);
               diffuseColor.rgb *= mix(0.84, 1.12, lam);
               float varnish = smoothstep(0.2, 0.8, 1.0 - abs(wn.y)) * rsNoise(wp * 0.35);
               diffuseColor.rgb = mix(diffuseColor.rgb, diffuseColor.rgb * vec3(0.68, 0.56, 0.52), varnish * 0.32);
             } else if (uFabricType == 3) {
               // Tafoni salt-weathering honeycomb alveoli on steep cliff faces
               float steep = smoothstep(0.25, 0.78, 1.0 - abs(wn.y));
               float cell1 = abs(rsNoise(wp * 2.3) - 0.5) * 2.0;
               float cell2 = abs(rsNoise(wp * 5.1 + 4.3) - 0.5) * 2.0;
               float pit = (1.0 - smoothstep(0.08, 0.42, cell1 * 0.7 + cell2 * 0.3)) * steep;
               diffuseColor.rgb *= (1.0 - 0.42 * pit);
               diffuseColor.rgb += vec3(0.05, 0.04, 0.03) * smoothstep(0.55, 0.88, cell1) * steep;
             } else if (uFabricType == 4) {
               // Folded metamorphic gneissic leucosome/melanosome banding + quartz veins
               float fold = wp.y * 2.2 + sin(wp.x * 0.9 + rsNoise(wp * 0.45) * 3.8) * 1.3;
               float band = sin(fold * 3.4);
               diffuseColor.rgb = mix(diffuseColor.rgb * 0.76, diffuseColor.rgb * 1.24 + vec3(0.05), smoothstep(-0.5, 0.6, band));
               float vein = 1.0 - smoothstep(0.015, 0.055, abs(rsNoise(wp * 1.15 + 9.2) - 0.52));
               diffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.72, 0.70, 0.66), vein * 0.45);
             }
           }
           float edgeCatch = vRockMask.z * 0.065 * uCurvatureStrength;
           float cavityDark = 1.0 - vRockMask.y * 0.16 * uCurvatureStrength;
           diffuseColor.rgb = (diffuseColor.rgb + vec3(edgeCatch)) * cavityDark;`
        )
        .replace(
          '#include <map_fragment>',
          `#include <map_fragment>
           // Chemical weathering: baked iron-rust & copper-patina stain flow
           if (uChemStrength > 0.001) {
             float feS = clamp(vRockStain.x, 0.0, 1.0) * uChemStrength;
             float cuS = clamp(vRockStain.y, 0.0, 1.0) * uChemStrength;
             if (feS > 0.004) {
               vec3 rust = mix(vec3(0.46, 0.21, 0.06), vec3(0.15, 0.06, 0.03), smoothstep(0.35, 0.9, feS));
               diffuseColor.rgb = mix(diffuseColor.rgb, rust, smoothstep(0.05, 0.7, feS) * 0.88);
             }
             if (cuS > 0.004) {
               vec3 patina = mix(vec3(0.14, 0.48, 0.38), vec3(0.08, 0.26, 0.52), smoothstep(0.4, 0.95, cuS));
               diffuseColor.rgb = mix(diffuseColor.rgb, patina, smoothstep(0.05, 0.7, cuS) * 0.85);
             }
           }`
        )
        .replace(
          '#include <aomap_fragment>',
          `#include <aomap_fragment>
           float vtxAO = mix(1.0, vRockMask.x, uAoStrength);
           reflectedLight.indirectDiffuse *= vtxAO;
           reflectedLight.indirectSpecular *= mix(1.0, vtxAO, 0.8);`
        )
        .replace(
          '#include <dithering_fragment>',
          `#include <dithering_fragment>
           if (uDebugMode == 1) {
             gl_FragColor = vec4(vec3(vRockMask.x), 1.0);
           } else if (uDebugMode == 2) {
             gl_FragColor = vec4(vRockMask.y, vRockMask.z, vRockMask.x, 1.0);
           } else if (uDebugMode == 3) {
             gl_FragColor = vec4(normalize(vWorldNormalViz) * 0.5 + 0.5, 1.0);
           }`
        );
    };

    this.wireMaterial = new THREE.MeshBasicMaterial({
      color: '#11161d',
      wireframe: true,
      transparent: true,
      opacity: 0.26,
      depthTest: true,
    });
  }

  setFormationMesh(meshData, lithology, dims, autoFrame = false) {
    if (this.rockMesh) {
      this.scene.remove(this.rockMesh);
      this.rockMesh.geometry.dispose();
      this.rockMesh = null;
    }
    if (this.wireMesh) {
      this.scene.remove(this.wireMesh);
      this.wireMesh = null;
    }

    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.BufferAttribute(meshData.positions, 3));
    geometry.setAttribute('normal', new THREE.BufferAttribute(meshData.normals, 3));
    geometry.setAttribute('uv', new THREE.BufferAttribute(meshData.uvs, 2));
    geometry.setAttribute('aRockMask', new THREE.BufferAttribute(meshData.colors, 4));
    const vCount = meshData.positions.length / 3;
    const albedoArr = (meshData.hasCustomAlbedo && meshData.albedos)
      ? meshData.albedos
      : new Float32Array(vCount * 3).fill(0.5);
    geometry.setAttribute('aRockAlbedo', new THREE.BufferAttribute(albedoArr, 3));
    const stainArr = meshData.stains ? meshData.stains : new Float32Array(vCount * 2);
    geometry.setAttribute('aRockStain', new THREE.BufferAttribute(stainArr, 2));
    geometry.setIndex(new THREE.BufferAttribute(meshData.indices, 1));
    geometry.computeBoundingBox();
    geometry.computeBoundingSphere();

    this.hasCustomAlbedo = !!(meshData.hasCustomAlbedo && meshData.albedos);
    this.manualLithologyOverride = false;

    this.rockMesh = new THREE.Mesh(geometry, this.rockMaterial);
    this.rockMesh.castShadow = true;
    this.rockMesh.receiveShadow = true;
    this.scene.add(this.rockMesh);

    this.wireMesh = new THREE.Mesh(geometry, this.wireMaterial);
    this.wireMesh.visible = this.wireframeVisible;
    this.scene.add(this.wireMesh);

    this.currentLithology = lithology || 'dolomite';
    this.formationDims = dims || [12, 8, 8];
    const bb = geometry.boundingBox;
    if (bb) {
      this.formationCenter = [
        0.5 * (bb.min.x + bb.max.x),
        0.5 * (bb.min.y + bb.max.y),
        0.5 * (bb.min.z + bb.max.z),
      ];
      this.boundRadius = Math.max(
        2,
        0.5 * Math.hypot(bb.max.x - bb.min.x, bb.max.y - bb.min.y, bb.max.z - bb.min.z)
      );
      this.scaleHuman.position.set(bb.max.x + 0.9, 0, bb.min.z + (bb.max.z - bb.min.z) * 0.65);
    } else {
      this.formationCenter = [0, this.formationDims[1] * 0.42, 0];
      this.boundRadius = Math.max(2, geometry.boundingSphere ? geometry.boundingSphere.radius : 8);
      this.scaleHuman.position.set(
        this.formationDims[0] * 0.54 + 0.9,
        0,
        this.formationDims[2] * 0.28
      );
    }

    this._applyShadingUniforms();
    this.updateSunPosition();

    if (autoFrame) {
      this.frameCamera('hero');
    }
  }

  setShadingMode(mode) {
    this.shadingMode = mode;
    this._applyShadingUniforms();
  }

  setFabricMode(code) {
    this.shaderUniforms.uFabricType.value = Math.max(0, Math.min(4, Number(code) || 0));
  }

  setLithologyOverride(lithKey) {
    if (lithKey) {
      this.currentLithology = lithKey;
      this.manualLithologyOverride = true;
      this._applyShadingUniforms();
    }
  }

  _applyShadingUniforms() {
    const mode = this.shadingMode;
    this.shaderUniforms.uUseVertexAlbedo.value = 0.0;
    this.shaderUniforms.uChemStrength.value = 1.0;
    if (mode === 'pure-clay') {
      this.rockMaterial.color.set(LITHOLOGY_PALETTES.pureClay.color);
      this.rockMaterial.roughness = LITHOLOGY_PALETTES.pureClay.roughness;
      this.shaderUniforms.uCurvatureStrength.value = 0.0;
      this.shaderUniforms.uChemStrength.value = 0.0; // pure clay stays unstained
      this.shaderUniforms.uAoStrength.value = 0.85;
      this.shaderUniforms.uDebugMode.value = 0;
    } else if (mode === 'lithology') {
      const pal = LITHOLOGY_PALETTES[this.currentLithology] || LITHOLOGY_PALETTES.neutralStone;
      this.rockMaterial.color.set(pal.color);
      this.rockMaterial.roughness = pal.roughness;
      this.shaderUniforms.uCurvatureStrength.value = 0.88;
      this.shaderUniforms.uAoStrength.value = 0.94;
      this.shaderUniforms.uUseVertexAlbedo.value = (this.hasCustomAlbedo && !this.manualLithologyOverride) ? 1.0 : 0.0;
      this.shaderUniforms.uDebugMode.value = 0;
    } else if (mode === 'ao') {
      this.shaderUniforms.uDebugMode.value = 1;
    } else if (mode === 'curvature') {
      this.shaderUniforms.uDebugMode.value = 2;
    } else if (mode === 'normals') {
      this.shaderUniforms.uDebugMode.value = 3;
    } else {
      // 'flat-stone' default AAA studio stone
      this.rockMaterial.color.set(LITHOLOGY_PALETTES.neutralStone.color);
      this.rockMaterial.roughness = LITHOLOGY_PALETTES.neutralStone.roughness;
      this.shaderUniforms.uCurvatureStrength.value = 0.85;
      this.shaderUniforms.uAoStrength.value = 0.92;
      this.shaderUniforms.uDebugMode.value = 0;
    }
  }

  setSunAngles(azimuthDeg, elevationDeg) {
    this.sunAzimuthDeg = azimuthDeg;
    this.sunElevationDeg = elevationDeg;
    this.updateSunPosition();
  }

  updateSunPosition() {
    const az = (this.sunAzimuthDeg * Math.PI) / 180;
    const el = (Math.max(6, this.sunElevationDeg) * Math.PI) / 180;
    const r = Math.max(18, this.boundRadius * 3.2);
    const [cx, , cz] = this.formationCenter || [0, 0, 0];
    const targetY = this.formationDims[1] * 0.42;

    const sx = cx + Math.sin(az) * Math.cos(el) * r;
    const sy = targetY + Math.sin(el) * r;
    const sz = cz + Math.cos(az) * Math.cos(el) * r;

    this.sunLight.position.set(sx, sy, sz);
    this.sunLight.target.position.set(cx, targetY, cz);
    this.sunLight.target.updateMatrixWorld();

    // Tight shadow frustum scaled to current asset
    const ext = Math.max(4, this.boundRadius * 1.45);
    const cam = this.sunLight.shadow.camera;
    cam.left = -ext;
    cam.right = ext;
    cam.top = ext;
    cam.bottom = -ext;
    cam.near = r * 0.15;
    cam.far = r * 2.4;
    cam.updateProjectionMatrix();
    this.sunLight.shadow.normalBias = this.boundRadius * 0.0032;

    this.rimLight.position.set(-sx * 0.8, targetY + r * 0.45, -sz * 0.8);
  }

  setWireframe(enabled) {
    this.wireframeVisible = !!enabled;
    if (this.wireMesh) this.wireMesh.visible = this.wireframeVisible;
  }

  setScaleHumanVisible(enabled) {
    this.scaleHuman.visible = !!enabled;
  }

  setTurntable(enabled) {
    this.turntable = !!enabled;
  }

  frameCamera(preset = 'hero') {
    const [, dy] = this.formationDims;
    const [cx, , cz] = this.formationCenter || [0, 0, 0];
    const targetY = dy * 0.42;
    this.controls.target.set(cx, targetY, cz);

    const dist = Math.max(3.0, this.boundRadius * 2.25);
    let yaw = 0.48, pitch = 0.33;
    if (preset === 'front') { yaw = 0.0; pitch = 0.22; }
    else if (preset === 'side') { yaw = 1.38; pitch = 0.24; }
    else if (preset === 'top') { yaw = 0.25; pitch = 1.28; }
    else if (preset === 'low') { yaw = -0.38; pitch = 0.07; }

    this.camera.position.set(
      cx + Math.sin(yaw) * Math.cos(pitch) * dist,
      targetY + Math.sin(pitch) * dist,
      cz + Math.cos(yaw) * Math.cos(pitch) * dist
    );
    this.controls.update();
  }

  resize() {
    const w = this.container.clientWidth || window.innerWidth;
    const h = this.container.clientHeight || window.innerHeight;
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
    this.renderer.setSize(w, h);
  }

  _bindUnrealFlyControls() {
    const dom = this.renderer.domElement;
    let rmbDragging = false;
    let lastX = 0;
    let lastY = 0;

    dom.addEventListener('contextmenu', (e) => e.preventDefault());
    dom.addEventListener('pointerdown', (e) => {
      if (!this.active || e.button !== 2) return;
      rmbDragging = true;
      this._rmbDown = true;
      lastX = e.clientX;
      lastY = e.clientY;
      dom.setPointerCapture(e.pointerId);
    });
    dom.addEventListener('pointermove', (e) => {
      if (!this.active || !rmbDragging) return;
      const dx = e.clientX - lastX;
      const dy = e.clientY - lastY;
      lastX = e.clientX;
      lastY = e.clientY;

      // Rotate look target around camera eye (Unreal RMB Free-Look!)
      const lookVec = this.controls.target.clone().sub(this.camera.position);
      const dist = Math.max(0.5, lookVec.length());
      const sph = new THREE.Spherical().setFromVector3(lookVec);
      sph.theta -= dx * 0.0046;
      sph.phi = Math.max(0.08, Math.min(Math.PI - 0.08, sph.phi + dy * 0.0044));
      lookVec.setFromSpherical(sph).setLength(dist);
      this.controls.target.copy(this.camera.position).add(lookVec);
    });
    const endRmb = (e) => {
      if (!e || e.button === 2) {
        rmbDragging = false;
        this._rmbDown = false;
      }
    };
    dom.addEventListener('pointerup', endRmb);
    dom.addEventListener('pointercancel', endRmb);
    dom.addEventListener(
      'wheel',
      (e) => {
        if (!this.active || !this._rmbDown) return;
        e.preventDefault();
        e.stopImmediatePropagation();
        this.flySpeed = Math.max(2.5, Math.min(90.0, this.flySpeed * Math.exp(-e.deltaY * 0.0012)));
      },
      { passive: false, capture: true }
    );

    const flyCodes = new Set([
      'KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyQ', 'KeyE',
      'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight',
      'ShiftLeft', 'ShiftRight',
    ]);
    window.addEventListener('keydown', (e) => {
      if (!this.active || e.metaKey || e.ctrlKey) return;
      const tag = document.activeElement ? document.activeElement.tagName : '';
      const type = document.activeElement ? (document.activeElement.type || '') : '';
      if (tag === 'TEXTAREA' || (tag === 'INPUT' && type !== 'range' && type !== 'checkbox')) return;
      if (tag === 'SELECT' && e.code.startsWith('Arrow')) return;
      if (flyCodes.has(e.code)) {
        if (tag === 'INPUT' && type === 'range' && e.code.startsWith('Key')) {
          document.activeElement.blur();
        }
        this._keysDown.add(e.code);
        if (e.code.startsWith('Arrow')) e.preventDefault();
      }
    });
    window.addEventListener('keyup', (e) => this._keysDown.delete(e.code));
    window.addEventListener('blur', () => {
      this._keysDown.clear();
      this._rmbDown = false;
    });
  }

  _stepWasdCamera(dt) {
    if (this._keysDown.size === 0) return;
    const k = this._keysDown;
    const moveFwd = (k.has('KeyW') || k.has('ArrowUp') ? 1 : 0) - (k.has('KeyS') || k.has('ArrowDown') ? 1 : 0);
    const moveRight = (k.has('KeyD') || k.has('ArrowRight') ? 1 : 0) - (k.has('KeyA') || k.has('ArrowLeft') ? 1 : 0);
    const moveUp = (k.has('KeyE') ? 1 : 0) - (k.has('KeyQ') ? 1 : 0);
    if (moveFwd === 0 && moveRight === 0 && moveUp === 0) return;

    const turbo = (k.has('ShiftLeft') || k.has('ShiftRight')) ? 3.0 : 1.0;
    const step = this.flySpeed * turbo * Math.min(0.08, Math.max(0.001, dt));

    const fwd = new THREE.Vector3();
    this.camera.getWorldDirection(fwd);
    const right = new THREE.Vector3().crossVectors(fwd, new THREE.Vector3(0, 1, 0)).normalize();
    const delta = new THREE.Vector3()
      .addScaledVector(fwd, moveFwd)
      .addScaledVector(right, moveRight);
    delta.y += moveUp;
    if (delta.lengthSq() > 1.0) delta.normalize();
    delta.multiplyScalar(step);

    this.camera.position.add(delta);
    this.controls.target.add(delta);
    if (this.camera.position.y < 0.25) {
      const lift = 0.25 - this.camera.position.y;
      this.camera.position.y += lift;
      this.controls.target.y += lift;
    }
  }

  _animate() {
    this._raf = requestAnimationFrame(this._animate);
    if (!this.active) return;
    const dt = this._clock.getDelta();
    this._stepWasdCamera(dt);
    if (this.turntable && this._keysDown.size === 0) {
      const offset = this.camera.position.clone().sub(this.controls.target);
      offset.applyAxisAngle(new THREE.Vector3(0, 1, 0), dt * 0.35);
      this.camera.position.copy(this.controls.target).add(offset);
    }
    this.controls.update();
    this.renderer.render(this.scene, this.camera);
  }
}
