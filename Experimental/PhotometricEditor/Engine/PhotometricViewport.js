// PhotometricViewport.js — Dark-Room Volumetric Fog Lighting Stage & Switchable Sky Chamber
// Custom GLSL Volumetric Light Scattering + Measured/Analytical IES DataTexture Lookup +
// 1,400 Illuminated Airborne Dust Motes + Machined Physical 3D Light Fixtures +
// Recessed Obsidian Distance Stage Floor (only revealed by real physical light).

import * as THREE from "../Vendor/three.esm.min.js";
import { OrbitControls } from "../Vendor/orbit-controls.esm.min.js";
import { EvaluateIesIntensity, KelvinToRgb, SampleIes } from "./PhotometryEngine.js";
import { PresetIesNames } from "./PhotometryEngine.js";
import { IesWeight } from "./ProjectZeroLightProjection.js";

const DEG2RAD = Math.PI / 180;
const Clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const MAX_DUST_LIGHTS = 6;

const GLSL_NOISE = `
  float hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
  }
  float noise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash31(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash31(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash31(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash31(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash31(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash31(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash31(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash31(i + vec3(1.0, 1.0, 1.0));
    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    return mix(mix(nx00, nx10, f.y), mix(nx01, nx11, f.y), f.z);
  }
  float fbm3(vec3 p) {
    float v = 0.0;
    float a = 0.52;
    for (int i = 0; i < 3; i++) {
      v += a * noise3(p);
      p = p * 2.07 + vec3(13.1, 7.7, 5.3);
      a *= 0.48;
    }
    return v;
  }
`;

function CreateVolumetricConeMaterial() {
  return new THREE.ShaderMaterial({
    transparent: true,
    depthWrite: false,
    side: THREE.DoubleSide,
    blending: THREE.AdditiveBlending,
    uniforms: {
      uColor: { value: new THREE.Color("#ffd9a0") },
      uIntensity: { value: 1.0 },
      uPenumbra: { value: 0.45 },
      uDecay: { value: 1.75 },
      uRangeM: { value: 26.0 },
      uFogDensity: { value: 1.0 },
      uShellWeight: { value: 1.0 },
      uTime: { value: 0.0 },
      uHasIes: { value: 0.0 },
      uIesTex: { value: null },
    },
    vertexShader: `
      varying vec3 vWorldPos;
      varying vec3 vNormalView;
      varying vec2 vUv;
      void main() {
        vUv = uv;
        vec4 wp = modelMatrix * vec4(position, 1.0);
        vWorldPos = wp.xyz;
        vNormalView = normalize(normalMatrix * normal);
        gl_Position = projectionMatrix * viewMatrix * wp;
      }
    `,
    fragmentShader: `
      uniform vec3 uColor;
      uniform float uIntensity;
      uniform float uPenumbra;
      uniform float uDecay;
      uniform float uRangeM;
      uniform float uFogDensity;
      uniform float uShellWeight;
      uniform float uTime;
      uniform float uHasIes;
      uniform sampler2D uIesTex;
      varying vec3 vWorldPos;
      varying vec3 vNormalView;
      varying vec2 vUv;
      ${GLSL_NOISE}
      void main() {
        // Cylinder rotated +PI/2 around X puts top (uv.y = 1) at fixture origin (+Z=0) and base (uv.y = 0) at Z=+ReachM
        float axial = clamp(1.0 - vUv.y, 0.0, 1.0);

        // Soft Fresnel silhouette vanish so polygon boundaries NEVER look like an SVG triangle
        float rim = abs(dot(normalize(vNormalView), vec3(0.0, 0.0, 1.0)));
        float edgePow = mix(3.4, 1.65, clamp(uPenumbra, 0.0, 1.0));
        float edgeFade = pow(smoothstep(0.025, 0.9, rim), edgePow);

        float lensClear = smoothstep(0.0, 0.045, axial);
        float distM = axial * max(1.0, uRangeM);
        float invSq = 1.0 / (1.0 + pow(distM * 0.22, max(0.35, uDecay * 0.85)));
        float farFade = pow(clamp(1.0 - axial, 0.0, 1.0), 1.3);

        vec3 flowPos = vWorldPos * vec3(0.34, 0.44, 0.34) + vec3(uTime * 0.11, -uTime * 0.035, uTime * 0.06);
        float mist = 0.64 + 0.56 * fbm3(flowPos);

        float iesGain = 1.0;
        if (uHasIes > 0.5) {
          float phiU = fract(vUv.x + 0.25);
          float shellThetaU = clamp(1.0 - uShellWeight * 0.65, 0.05, 0.98);
          float samp = texture2D(uIesTex, vec2(shellThetaU, phiU)).r;
          iesGain = 0.12 + samp * 1.48;
        }

        float scatter = lensClear * edgeFade * invSq * farFade * mist * iesGain;
        float alpha = clamp(scatter * uIntensity * uFogDensity * uShellWeight * 0.36, 0.0, 0.55);
        if (alpha < 0.0015) discard;
        gl_FragColor = vec4(uColor * (1.0 + (1.0 - axial) * 0.3), alpha);
      }
    `,
  });
}

function CreateVolumetricSphereMaterial() {
  return new THREE.ShaderMaterial({
    transparent: true,
    depthWrite: false,
    side: THREE.FrontSide,
    blending: THREE.AdditiveBlending,
    uniforms: {
      uColor: { value: new THREE.Color("#ffd9a0") },
      uIntensity: { value: 1.0 },
      uDecay: { value: 2.0 },
      uShellFrac: { value: 0.7 },
      uFogDensity: { value: 1.0 },
      uTime: { value: 0.0 },
    },
    vertexShader: `
      varying vec3 vWorldPos;
      varying vec3 vNormalView;
      void main() {
        vec4 wp = modelMatrix * vec4(position, 1.0);
        vWorldPos = wp.xyz;
        vNormalView = normalize(normalMatrix * normal);
        gl_Position = projectionMatrix * viewMatrix * wp;
      }
    `,
    fragmentShader: `
      uniform vec3 uColor;
      uniform float uIntensity;
      uniform float uDecay;
      uniform float uShellFrac;
      uniform float uFogDensity;
      uniform float uTime;
      varying vec3 vWorldPos;
      varying vec3 vNormalView;
      ${GLSL_NOISE}
      void main() {
        float facing = clamp(dot(normalize(vNormalView), vec3(0.0, 0.0, 1.0)), 0.0, 1.0);
        float glow = pow(facing, 2.3 + uDecay * 0.35);
        float mist = 0.65 + 0.5 * fbm3(vWorldPos * 0.4 + vec3(uTime * 0.08, uTime * 0.03, 0.0));
        float shellAtten = pow(1.0 - clamp(uShellFrac, 0.0, 0.94), max(0.7, uDecay * 0.8));
        float alpha = clamp(glow * mist * shellAtten * uIntensity * uFogDensity * 0.24, 0.0, 0.45);
        if (alpha < 0.0012) discard;
        gl_FragColor = vec4(uColor, alpha);
      }
    `,
  });
}

function CreateDustParticleSystem() {
  const Count = 1400;
  const Positions = new Float32Array(Count * 3);
  const Seeds = new Float32Array(Count * 3);
  for (let i = 0; i < Count; i++) {
    Positions[i * 3] = (Math.random() - 0.5) * 26;
    Positions[i * 3 + 1] = 0.15 + Math.random() * 6.5;
    Positions[i * 3 + 2] = -22 + Math.random() * 30;
    Seeds[i * 3] = Math.random() * 6.283;
    Seeds[i * 3 + 1] = 0.5 + Math.random() * 1.4;
    Seeds[i * 3 + 2] = Math.random();
  }
  const Geo = new THREE.BufferGeometry();
  Geo.setAttribute("position", new THREE.BufferAttribute(Positions, 3));
  Geo.setAttribute("aSeed", new THREE.BufferAttribute(Seeds, 3));

  const Mat = new THREE.ShaderMaterial({
    transparent: true,
    depthWrite: false,
    blending: THREE.AdditiveBlending,
    uniforms: {
      uTime: { value: 0 },
      uLightPos: { value: Array.from({ length: MAX_DUST_LIGHTS }, () => new THREE.Vector3()) },
      uLightDir: { value: Array.from({ length: MAX_DUST_LIGHTS }, () => new THREE.Vector3(0, 0, -1)) },
      uLightCol: { value: Array.from({ length: MAX_DUST_LIGHTS }, () => new THREE.Color("#000000")) },
      uLightParam: { value: Array.from({ length: MAX_DUST_LIGHTS }, () => new THREE.Vector4(0, 10, -1, 2)) },
      uFogDensity: { value: 1.0 },
    },
    vertexShader: `
      attribute vec3 aSeed;
      uniform float uTime;
      uniform vec3 uLightPos[${MAX_DUST_LIGHTS}];
      uniform vec3 uLightDir[${MAX_DUST_LIGHTS}];
      uniform vec3 uLightCol[${MAX_DUST_LIGHTS}];
      uniform vec4 uLightParam[${MAX_DUST_LIGHTS}];
      varying vec3 vColor;
      varying float vLit;
      void main() {
        vec3 p = position;
        p.x += sin(uTime * 0.24 * aSeed.y + aSeed.x) * 0.42;
        p.y += cos(uTime * 0.18 * aSeed.y + aSeed.x * 1.3) * 0.25;
        p.z += sin(uTime * 0.20 + aSeed.x * 0.7) * 0.42;

        vec3 accum = vec3(0.0);
        for (int i = 0; i < ${MAX_DUST_LIGHTS}; i++) {
          float pwr = uLightParam[i].x;
          if (pwr <= 0.001) continue;
          vec3 toP = p - uLightPos[i];
          float d = length(toP);
          float rangeM = max(1.0, uLightParam[i].y);
          if (d > rangeM) continue;
          vec3 dir = toP / max(0.001, d);
          float cosCone = uLightParam[i].z;
          float coneFactor = 1.0;
          if (cosCone > -0.9) {
            float align = dot(dir, normalize(uLightDir[i]));
            coneFactor = smoothstep(cosCone - 0.12, min(0.99, cosCone + 0.18), align);
          }
          float falloff = pow(clamp(1.0 - d / rangeM, 0.0, 1.0), 1.5) / (1.0 + 0.1 * d * d);
          accum += uLightCol[i] * pwr * coneFactor * falloff;
        }

        vColor = accum;
        vLit = clamp(max(accum.r, max(accum.g, accum.b)) * 1.3, 0.0, 1.0);
        vec4 mvPos = modelViewMatrix * vec4(p, 1.0);
        gl_PointSize = clamp((13.0 + aSeed.z * 16.0) / max(1.0, -mvPos.z), 1.4, 5.2) * step(0.015, vLit);
        gl_Position = projectionMatrix * mvPos;
      }
    `,
    fragmentShader: `
      uniform float uFogDensity;
      varying vec3 vColor;
      varying float vLit;
      void main() {
        vec2 c = gl_PointCoord - 0.5;
        float r2 = dot(c, c);
        if (r2 > 0.25) discard;
        float soft = pow(1.0 - r2 * 4.0, 2.0);
        float a = clamp(soft * vLit * (0.28 + uFogDensity * 0.5), 0.0, 0.72);
        if (a < 0.004) discard;
        gl_FragColor = vec4(vColor, a);
      }
    `,
  });

  const Pts = new THREE.Points(Geo, Mat);
  Pts.frustumCulled = false;
  return Pts;
}

function CreateObsidianFloorTexture() {
  const Canvas = document.createElement("canvas");
  Canvas.width = 1024;
  Canvas.height = 1024;
  const Ctx = Canvas.getContext("2d");
  Ctx.fillStyle = "#0b0c0f";
  Ctx.fillRect(0, 0, 1024, 1024);

  for (let i = 0; i < 6000; i++) {
    const v = 10 + Math.floor(Math.random() * 8);
    Ctx.fillStyle = `rgb(${v},${v + 1},${v + 2})`;
    Ctx.fillRect(Math.random() * 1024, Math.random() * 1024, 2 + Math.random() * 4, 2 + Math.random() * 4);
  }
  Ctx.strokeStyle = "#13161c";
  Ctx.lineWidth = 1.2;
  for (let k = 0; k <= 16; k++) {
    const p = (k / 16) * 1024;
    Ctx.beginPath();
    Ctx.moveTo(p, 0); Ctx.lineTo(p, 1024);
    Ctx.moveTo(0, p); Ctx.lineTo(1024, p);
    Ctx.stroke();
  }
  const Tex = new THREE.CanvasTexture(Canvas);
  Tex.wrapS = Tex.wrapT = THREE.RepeatWrapping;
  Tex.repeat.set(5, 5);
  Tex.colorSpace = THREE.SRGBColorSpace;
  return Tex;
}

function CreateDistanceOverlayTexture() {
  const Canvas = document.createElement("canvas");
  Canvas.width = 1024;
  Canvas.height = 2048;
  const Ctx = Canvas.getContext("2d");
  Ctx.clearRect(0, 0, 1024, 2048);

  const MtoY = (z) => ((z + 68) / 80) * 2048;
  Ctx.strokeStyle = "rgba(185,200,220,0.16)";
  Ctx.fillStyle = "rgba(195,208,224,0.24)";
  Ctx.font = "500 22px monospace";
  Ctx.textAlign = "center";

  const OriginY = MtoY(2);
  for (let d = 5; d <= 45; d += 5) {
    const RadiusPx = (d / 50) * 1024;
    Ctx.beginPath();
    Ctx.arc(512, OriginY, RadiusPx, 0, Math.PI * 2);
    Ctx.stroke();
    const LY = MtoY(2 - d);
    if (LY > 24 && LY < 2020) {
      Ctx.fillText(`${d} m`, 512, LY - 8);
    }
  }
  const Tex = new THREE.CanvasTexture(Canvas);
  Tex.anisotropy = 4;
  return Tex;
}

function BuildIesDataTexture(P, LoadedIes) {
  const W = 64;
  const H = 64;
  const Data = new Uint8Array(W * H * 4);
  const ProfileIndex = Math.max(0, PresetIesNames.indexOf(P.profile || "ECE Low Beam"));
  const ActiveKey = P.customIesKey && LoadedIes?.[P.customIesKey] ? P.customIesKey
    : P.useMeasuredIes && P.measuredProfile && LoadedIes?.[P.measuredProfile] ? P.measuredProfile : null;
  const Parsed = ActiveKey ? LoadedIes[ActiveKey] : null;

  for (let py = 0; py < H; py++) {
    const Phi = (py / H) * 360;
    for (let px = 0; px < W; px++) {
      const Theta = (px / (W - 1)) * 90;
      let val = 0;
      if (Parsed) {
        val = Clamp(SampleIes(Parsed, Phi, Theta), 0, 1);
      } else {
        const SignedTheta = Phi > 90 && Phi < 270 ? -Theta : Theta;
        val = IesWeight(
          { "Profile type": ProfileIndex, "Field angle": P.cone ?? 58, "Cut-off pitch": P.cutoff ?? -1 },
          SignedTheta,
          Phi,
        );
      }
      const B = Math.round(Clamp(val, 0, 1) * 255);
      const O = (py * W + px) * 4;
      Data[O] = B;
      Data[O + 1] = B;
      Data[O + 2] = B;
      Data[O + 3] = 255;
    }
  }
  const Tex = new THREE.DataTexture(Data, W, H, THREE.RGBAFormat);
  Tex.minFilter = THREE.LinearFilter;
  Tex.magFilter = THREE.LinearFilter;
  Tex.wrapS = THREE.ClampToEdgeWrapping;
  Tex.wrapT = THREE.RepeatWrapping;
  Tex.needsUpdate = true;
  return Tex;
}

function BuildFixture(Light, HexTint, LoadedIes, FogDensityNorm, ShowVolumes) {
  const P = Light.Props;
  const Type = Light.Type;
  const Root = new THREE.Group();
  const Pos = P.pos || [0, 2.2, 0];
  const Rot = P.rot || [0, 0, 0];
  const Scale = P.scale || [1, 1, 1];
  Root.position.set(Pos[0], Pos[1], Pos[2]);
  Root.scale.set(Scale[0], Scale[1], Scale[2]);

  const MatteBlack = new THREE.MeshStandardMaterial({ color: "#0e1013", roughness: 0.42, metalness: 0.78 });
  const AnodizedGrey = new THREE.MeshStandardMaterial({ color: "#22262c", roughness: 0.34, metalness: 0.86 });
  const MachinedBrass = new THREE.MeshStandardMaterial({ color: "#7f6f4c", roughness: 0.32, metalness: 0.88 });
  const EmitterMat = new THREE.MeshStandardMaterial({
    color: HexTint,
    emissive: HexTint,
    emissiveIntensity: 3.4,
    roughness: 0.15,
  });
  const GlassShellMat = new THREE.MeshPhysicalMaterial({
    color: "#e6eff8",
    roughness: 0.08,
    transmission: 0.65,
    transparent: true,
    opacity: 0.38,
  });

  const VolumeShaders = [];
  let DustInfo = null;

  if (Type === "pointlight") {
    const Cord = new THREE.Mesh(new THREE.CylinderGeometry(0.012, 0.012, 1.4, 8), MatteBlack);
    Cord.position.y = 0.82;
    const Collar = new THREE.Mesh(new THREE.CylinderGeometry(0.055, 0.068, 0.11, 16), MachinedBrass);
    Collar.position.y = 0.11;
    const Bulb = new THREE.Mesh(new THREE.SphereGeometry(0.085, 20, 16), GlassShellMat);
    const Filament = new THREE.Mesh(new THREE.TorusKnotGeometry(0.028, 0.006, 28, 6, 2, 3), EmitterMat);
    Root.add(Cord, Collar, Bulb, Filament);
    for (let i = 0; i < 4; i++) {
      const Rib = new THREE.Mesh(new THREE.TorusGeometry(0.115, 0.005, 6, 20), MatteBlack);
      Rib.rotation.y = (i * Math.PI) / 4;
      Root.add(Rib);
    }

    const Reach = Clamp(Number(P.distance ?? 26), 2, 120);
    const Decay = Clamp(Number(P.decay ?? 2), 0.1, 4);
    const PowerScale = Clamp(Number(P.intensity ?? 14) / 14, 0.15, 8);
    const Pt = new THREE.PointLight(HexTint, PowerScale * 54, Reach, Clamp(Decay * 0.8, 0.5, 2.2));
    Pt.castShadow = !!P.shadows;
    Pt.shadow.mapSize.set(1024, 1024);
    Root.add(Pt);

    if (ShowVolumes && P.gizmoGlow !== false) {
      [0.24, 0.52, 0.85, 1.0].forEach((frac, idx) => {
        const Mat = CreateVolumetricSphereMaterial();
        Mat.uniforms.uColor.value.set(HexTint);
        Mat.uniforms.uIntensity.value = Clamp(PowerScale * 0.9, 0.2, 3.2);
        Mat.uniforms.uDecay.value = Decay;
        Mat.uniforms.uShellFrac.value = frac * 0.88;
        Mat.uniforms.uFogDensity.value = FogDensityNorm;
        const R = Math.max(0.9, Math.min(26, Reach * 0.42 * frac));
        const Sph = new THREE.Mesh(new THREE.SphereGeometry(R, 30, 22), Mat);
        Sph.renderOrder = 10 + idx;
        Root.add(Sph);
        VolumeShaders.push(Mat);
      });
    }
    DustInfo = { Pos: new THREE.Vector3(...Pos), Dir: new THREE.Vector3(0, -1, 0), Reach, CosCone: -1.0 };
  } else if (Type === "spotlight" || Type === "ieslight" || Type === "ledlight" || Type === "arealight") {
    const Aim = new THREE.Group();
    if (Array.isArray(P.target)) {
      const Dir = new THREE.Vector3(
        P.target[0] - Pos[0],
        P.target[1] - Pos[1],
        P.target[2] - Pos[2],
      );
      if (Dir.lengthSq() > 1e-6) {
        Dir.normalize();
        const RefUp = Math.abs(Dir.y) > 0.99 ? new THREE.Vector3(0, 0, -1) : new THREE.Vector3(0, 1, 0);
        const Right = new THREE.Vector3().crossVectors(RefUp, Dir).normalize();
        const TrueUp = new THREE.Vector3().crossVectors(Dir, Right).normalize();
        const Basis = new THREE.Matrix4().makeBasis(Right, TrueUp, Dir);
        Aim.quaternion.setFromRotationMatrix(Basis);
      }
    } else {
      Aim.rotation.set(Rot[0] * DEG2RAD, Rot[1] * DEG2RAD, Rot[2] * DEG2RAD);
    }

    if (Type === "spotlight") {
      // Barrel & lens pointing along +Z
      const Barrel = new THREE.Mesh(new THREE.CylinderGeometry(0.135, 0.148, 0.34, 24), MatteBlack);
      Barrel.rotation.x = Math.PI / 2;
      Barrel.position.z = -0.14;
      const Bezel = new THREE.Mesh(new THREE.CylinderGeometry(0.15, 0.15, 0.05, 24), AnodizedGrey);
      Bezel.rotation.x = Math.PI / 2;
      Bezel.position.z = 0.03;
      const Lens = new THREE.Mesh(new THREE.CylinderGeometry(0.13, 0.13, 0.02, 24), EmitterMat);
      Lens.rotation.x = Math.PI / 2;
      Lens.position.z = 0.05;
      Aim.add(Barrel, Bezel, Lens);
      for (let d = 0; d < 4; d++) {
        const Flap = new THREE.Mesh(new THREE.BoxGeometry(0.17, 0.14, 0.012), MatteBlack);
        const Ang = (d * Math.PI) / 2;
        Flap.position.set(Math.sin(Ang) * 0.16, Math.cos(Ang) * 0.16, 0.11);
        Flap.rotation.z = -Ang;
        Flap.rotation.x = 0.38;
        Aim.add(Flap);
      }
    } else if (Type === "ieslight") {
      const Pod = new THREE.Mesh(new THREE.BoxGeometry(0.3, 0.22, 0.28), MatteBlack);
      Pod.position.z = -0.12;
      const Bezel = new THREE.Mesh(new THREE.CylinderGeometry(0.12, 0.13, 0.1, 24), AnodizedGrey);
      Bezel.rotation.x = Math.PI / 2;
      Bezel.position.z = 0.03;
      const Eye = new THREE.Mesh(new THREE.SphereGeometry(0.105, 20, 14), EmitterMat);
      Eye.scale.z = 0.45;
      Eye.position.z = 0.06;
      Aim.add(Pod, Bezel, Eye);
    } else if (Type === "ledlight") {
      const Can = new THREE.Mesh(new THREE.CylinderGeometry(0.1, 0.11, 0.2, 20), AnodizedGrey);
      Can.rotation.x = Math.PI / 2;
      Can.position.z = -0.09;
      Aim.add(Can);
      for (let f = 0; f < 5; f++) {
        const Fin = new THREE.Mesh(new THREE.CylinderGeometry(0.122, 0.122, 0.008, 20), MatteBlack);
        Fin.rotation.x = Math.PI / 2;
        Fin.position.z = -0.14 + f * 0.032;
        Aim.add(Fin);
      }
      const Cob = new THREE.Mesh(new THREE.BoxGeometry(0.09, 0.09, 0.015), EmitterMat);
      Cob.position.z = 0.02;
      Aim.add(Cob);
    } else {
      const W = Clamp(Number(P.width ?? 2), 0.4, 6);
      const H = Clamp(Number(P.height ?? 1), 0.3, 4);
      const Frame = new THREE.Mesh(new THREE.BoxGeometry(W + 0.08, H + 0.08, 0.12), MatteBlack);
      Frame.position.z = -0.06;
      const Cloth = new THREE.Mesh(new THREE.PlaneGeometry(W, H), EmitterMat);
      Cloth.position.z = 0.01;
      Aim.add(Frame, Cloth);
    }

    const ReachM = Clamp(
      Type === "ieslight" ? Number(P.range ?? 42) : Type === "spotlight" ? Number(P.distance ?? 32) : 22,
      4,
      80,
    );
    const HalfConeRad =
      Type === "ieslight"
        ? Clamp(((Number(P.cone ?? 58) * 0.5) * DEG2RAD), 0.08, Math.PI * 0.44)
        : Type === "arealight"
          ? Clamp(((Number(P.spread ?? 120) * 0.45) * DEG2RAD), 0.22, Math.PI * 0.45)
          : Type === "ledlight"
            ? Clamp(((Number(P.angle ?? 90) * 0.5) * DEG2RAD), 0.08, Math.PI * 0.44)
            : Clamp(Number(P.angle ?? 28) * DEG2RAD, 0.05, Math.PI * 0.44);

    const Penumbra = Type === "spotlight" ? Clamp(Number(P.penumbra ?? 0.42), 0, 1) : 0.55;
    const Lumens =
      Type === "ieslight"
        ? Number(P.lumens ?? 1650) * Number(P.multiplier ?? 1)
        : Type === "ledlight"
          ? Number(P.watts ?? 18) * Number(P.efficacy ?? 125) * Number(P.dimmer ?? 1)
          : Type === "arealight"
            ? Number(P.lumens ?? 2400)
            : Number(P.intensity ?? 88) * 28;
    const PowerScale = Clamp(Lumens / 1600, 0.2, 6.5);

    const Sp = new THREE.SpotLight(HexTint, PowerScale * 95, ReachM, HalfConeRad, Clamp(Penumbra, 0.18, 1), 1.4);
    Sp.position.set(0, 0, 0.06);
    Sp.target.position.set(0, 0, 10);
    Sp.castShadow = !!P.shadows;
    Sp.shadow.mapSize.set(2048, 2048);
    Sp.shadow.bias = -0.00025;
    Aim.add(Sp, Sp.target);

    const ShowFlag =
      Type === "ieslight" ? P.showDistribution !== false
        : Type === "spotlight" ? P.showCone !== false
        : P.showShape !== false;

    if (ShowVolumes && ShowFlag) {
      const IesTex = Type === "ieslight" ? BuildIesDataTexture(P, LoadedIes) : null;
      const Shells = [
        { radMul: 0.48, weight: 1.0 },
        { radMul: 0.78, weight: 0.62 },
        { radMul: 1.05, weight: 0.34 },
      ];
      Shells.forEach((S, idx) => {
        const BottomR = Math.max(0.28, Math.tan(HalfConeRad) * ReachM * S.radMul);
        const TopR = Type === "arealight" ? Clamp(Number(P.width ?? 2) * 0.42, 0.2, 1.5) : 0.06;
        const ConeGeo = new THREE.CylinderGeometry(TopR, BottomR, ReachM, 42, 24, true);
        // Orient top (uv.y=1) at z=0, bottom (uv.y=0) at z=+ReachM
        ConeGeo.rotateX(Math.PI / 2);
        ConeGeo.translate(0, 0, ReachM * 0.5 + 0.04);

        const Mat = CreateVolumetricConeMaterial();
        Mat.uniforms.uColor.value.set(HexTint);
        Mat.uniforms.uIntensity.value = Clamp(PowerScale * 0.92, 0.22, 3.0);
        Mat.uniforms.uPenumbra.value = Penumbra;
        Mat.uniforms.uDecay.value = Type === "ieslight" ? 1.5 : 1.75;
        Mat.uniforms.uRangeM.value = ReachM;
        Mat.uniforms.uFogDensity.value = FogDensityNorm;
        Mat.uniforms.uShellWeight.value = S.weight;
        if (IesTex) {
          Mat.uniforms.uHasIes.value = 1.0;
          Mat.uniforms.uIesTex.value = IesTex;
        }
        const Mesh = new THREE.Mesh(ConeGeo, Mat);
        Mesh.renderOrder = 10 + idx;
        Aim.add(Mesh);
        VolumeShaders.push(Mat);
      });
    }

    Root.add(Aim);
    const DirWorld = new THREE.Vector3(0, 0, 1).applyQuaternion(Aim.quaternion).normalize();
    DustInfo = { Pos: new THREE.Vector3(...Pos), Dir: DirWorld, Reach: ReachM, CosCone: Math.cos(HalfConeRad) };
  } else {
    // TubeLight / LEDStrip
    const Len = Clamp(Number(P.length ?? 2.4), 0.4, 8);
    const Rad = Type === "tubelight" ? Clamp(Number(P.radius ?? 0.04), 0.015, 0.14) : 0.022;
    const Reach = Clamp(Number(P.distance ?? 18), 4, 48);
    const Rail = new THREE.Mesh(new THREE.BoxGeometry(Len + 0.06, 0.05, 0.06), AnodizedGrey);
    Rail.position.y = 0.03;
    Root.add(Rail);

    if (Type === "tubelight") {
      const Tube = new THREE.Mesh(new THREE.CylinderGeometry(Rad, Rad, Len, 18), EmitterMat);
      Tube.rotation.z = Math.PI / 2;
      Tube.position.y = -0.02;
      Root.add(Tube);
    } else if (P.diffuser) {
      const Opal = new THREE.Mesh(new THREE.BoxGeometry(Len, 0.022, 0.03), EmitterMat);
      Opal.position.y = -0.005;
      Root.add(Opal);
    } else {
      const Nodes = Math.min(36, Math.max(8, Math.round(Len * 12)));
      for (let i = 0; i < Nodes; i++) {
        const Dot = new THREE.Mesh(new THREE.BoxGeometry(0.018, 0.014, 0.018), EmitterMat);
        Dot.position.set(-Len / 2 + ((i + 0.5) / Nodes) * Len, -0.004, 0);
        Root.add(Dot);
      }
    }

    const Lumens = Type === "tubelight" ? Number(P.lumens ?? 1800) : Number(P.lumensPerMetre ?? 1000) * Len * Number(P.dimmer ?? 0.9);
    const PowerScale = Clamp(Lumens / 1800, 0.2, 5.5);
    const L1 = new THREE.PointLight(HexTint, PowerScale * 26, Reach, 1.5);
    L1.position.set(-Len * 0.28, -0.1, 0);
    const L2 = new THREE.PointLight(HexTint, PowerScale * 26, Reach, 1.5);
    L2.position.set(Len * 0.28, -0.1, 0);
    Root.add(L1, L2);

    if (ShowVolumes && P.showShape !== false) {
      const Mat = CreateVolumetricSphereMaterial();
      Mat.uniforms.uColor.value.set(HexTint);
      Mat.uniforms.uIntensity.value = Clamp(PowerScale * 0.72, 0.2, 2.5);
      Mat.uniforms.uDecay.value = 1.6;
      Mat.uniforms.uShellFrac.value = 0.55;
      Mat.uniforms.uFogDensity.value = FogDensityNorm;
      const Halo = new THREE.Mesh(new THREE.SphereGeometry(1.0, 24, 18), Mat);
      Halo.scale.set(Len * 1.4, 2.6, 2.6);
      Root.add(Halo);
      VolumeShaders.push(Mat);
    }
    DustInfo = { Pos: new THREE.Vector3(...Pos), Dir: new THREE.Vector3(0, -1, 0), Reach, CosCone: -1.0 };
  }

  return { Root, VolumeShaders, DustInfo };
}

export class PhotometricViewportController {
  constructor(MountEl, Options = {}) {
    this.MountEl = MountEl;
    this.OnBackendChange = Options.onBackendChange;
    this.ShadingMode = "pbr";
    this.ShowGizmos = true;
    this.DarkRoomOnly = true;
    this.SceneState = null;
    this.LoadedIes = {};
    this.VolumeShaders = [];
    this._MoonTexCache = {};
    this._TextureLoader = null;
    this._Disposed = false;
    this._ClockStart = performance.now();

    this.Canvas = document.createElement("canvas");
    this.Canvas.className = "viewport-webgl-canvas";
    MountEl.replaceChildren(this.Canvas);

    this.Scene = new THREE.Scene();
    this.Scene.background = new THREE.Color("#020203");
    this.Scene.fog = new THREE.FogExp2("#020305", 0.024);

    this.Camera = new THREE.PerspectiveCamera(44, 16 / 9, 0.1, 320);
    this.Camera.position.set(6.4, 3.2, 8.2);

    this.LuminaireGroup = new THREE.Group();
    this.EnvironmentGroup = new THREE.Group();
    this.DarkRoomGroup = new THREE.Group();

    try {
      this.Renderer = new THREE.WebGLRenderer({ canvas: this.Canvas, antialias: true, powerPreference: "high-performance" });
    } catch {
      try {
        this.Renderer = new THREE.WebGLRenderer({ canvas: this.Canvas, antialias: true });
      } catch {
        this.Renderer = null;
      }
    }

    if (this.Renderer) {
      this.Renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
      this.Renderer.shadowMap.enabled = true;
      this.Renderer.shadowMap.type = THREE.PCFSoftShadowMap;
      this.Renderer.toneMapping = THREE.ACESFilmicToneMapping;
      this.Renderer.toneMappingExposure = 1.15;
      this.Backend = "WebGL2 Volumetric";
    } else {
      this.Backend = "Software 2D Fallback";
    }
    this.OnBackendChange?.(this.Backend);

    if (this.Renderer) {
      this.Controls = new OrbitControls(this.Camera, this.Canvas);
      this.Controls.enableDamping = true;
      this.Controls.dampingFactor = 0.08;
      this.Controls.target.set(0, 1.0, -4.5);
      this.Controls.maxPolarAngle = Math.PI * 0.495;
      this.Controls.update();

      this._TextureLoader = new THREE.TextureLoader();
      this._EnsureMoonTexture("luna_2k.jpg");

      this._BuildDarkRoom();
      this.DustSystem = CreateDustParticleSystem();
      this.Scene.add(this.DarkRoomGroup, this.EnvironmentGroup, this.LuminaireGroup, this.DustSystem);
    }

    if (typeof ResizeObserver !== "undefined" && MountEl) {
      this._ResizeObserver = new ResizeObserver(() => this.Resize());
      this._ResizeObserver.observe(MountEl);
    }

    this.Resize();
    this._Animate();
  }

  _EnsureMoonTexture(File) {
    if (!File || !this._TextureLoader) return null;
    if (this._MoonTexCache[File]) return this._MoonTexCache[File];
    this._MoonTexCache[File] = "loading";
    this._TextureLoader.load(`./Textures/Moons/${File}`, (Tex) => {
      Tex.colorSpace = THREE.SRGBColorSpace;
      this._MoonTexCache[File] = Tex;
      if (this.SceneState) this.SyncScene(this.SceneState, this.LoadedIes);
    });
    return null;
  }

  _BuildDarkRoom() {
    this.DarkRoomGroup.clear();
    this.RoomAmbient = new THREE.AmbientLight("#0a0c10", 0.045);
    this.DarkRoomGroup.add(this.RoomAmbient);

    const FloorMat = new THREE.MeshStandardMaterial({
      color: "#ffffff",
      map: CreateObsidianFloorTexture(),
      roughness: 0.34,
      metalness: 0.12,
    });
    const Floor = new THREE.Mesh(new THREE.PlaneGeometry(54, 80), FloorMat);
    Floor.rotation.x = -Math.PI / 2;
    Floor.position.set(0, 0, -28);
    Floor.receiveShadow = true;
    this.DarkRoomGroup.add(Floor);

    const Overlay = new THREE.Mesh(
      new THREE.PlaneGeometry(50, 80),
      new THREE.MeshStandardMaterial({
        map: CreateDistanceOverlayTexture(),
        transparent: true,
        roughness: 0.7,
        depthWrite: false,
      }),
    );
    Overlay.rotation.x = -Math.PI / 2;
    Overlay.position.set(0, 0.004, -28);
    Overlay.receiveShadow = true;
    this.DarkRoomGroup.add(Overlay);

    const Stone = new THREE.MeshStandardMaterial({ color: "#15181e", roughness: 0.78, metalness: 0.08 });
    const PillarL = new THREE.Mesh(new THREE.BoxGeometry(0.65, 4.6, 0.65), Stone);
    PillarL.position.set(-2.5, 2.3, -6.5);
    PillarL.castShadow = PillarL.receiveShadow = true;
    const PillarR = new THREE.Mesh(new THREE.BoxGeometry(0.75, 5.2, 0.75), Stone);
    PillarR.position.set(2.9, 2.6, -12.5);
    PillarR.castShadow = PillarR.receiveShadow = true;
    const CalibrationOrb = new THREE.Mesh(
      new THREE.SphereGeometry(0.45, 36, 28),
      new THREE.MeshStandardMaterial({ color: "#1f232b", roughness: 0.36, metalness: 0.22 }),
    );
    CalibrationOrb.position.set(0, 0.45, -2.5);
    CalibrationOrb.castShadow = CalibrationOrb.receiveShadow = true;
    const Wall = new THREE.Mesh(new THREE.PlaneGeometry(54, 18), Stone);
    Wall.position.set(0, 9, -28);
    Wall.receiveShadow = true;
    this.DarkRoomGroup.add(PillarL, PillarR, CalibrationOrb, Wall);
  }

  _ClearGroup(G) {
    while (G.children.length > 0) {
      const C = G.children[0];
      G.remove(C);
      C.traverse?.((O) => {
        O.geometry?.dispose?.();
        if (O.material) {
          O.material.uniforms?.uIesTex?.value?.dispose?.();
          O.material.dispose?.();
        }
      });
    }
  }

  SetShadingMode(Mode) {
    this.ShadingMode = Mode;
    if (this.SceneState) this.SyncScene(this.SceneState, this.LoadedIes);
  }

  SetDarkRoomOnly(Only) {
    this.DarkRoomOnly = !!Only;
    if (this.SceneState) this.SyncScene(this.SceneState, this.LoadedIes);
  }

  SetCameraView(View) {
    if (!this.Controls) return;
    if (View === "top") {
      this.Camera.position.set(0, 20, -6);
      this.Controls.target.set(0, 0, -6);
    } else if (View === "wall") {
      this.Camera.position.set(0, 2.2, 4.0);
      this.Controls.target.set(0, 1.4, -18);
    } else if (View === "roadway") {
      this.Camera.position.set(6.4, 3.2, 8.4);
      this.Controls.target.set(0, 1.0, -6.0);
    } else {
      this.Camera.position.set(6.2, 3.3, 7.6);
      this.Controls.target.set(0, 1.1, -3.5);
    }
    this.Controls.update();
  }

  SampleStagePointLux(X, Y, Z, SceneState, LoadedIes) {
    let Sum = 0;
    for (const L of SceneState.Lights || []) {
      if (!L.Visible) continue;
      const P = L.Props;
      const dx = X - (P.pos?.[0] ?? 0);
      const dy = Y - (P.pos?.[1] ?? 2);
      const dz = Z - (P.pos?.[2] ?? 0);
      const R2 = Math.max(0.25, dx * dx + dy * dy + dz * dz);
      const Int =
        L.Type === "ieslight"
          ? Number(P.lumens ?? 1650) * Number(P.multiplier ?? 1) * 0.25 * (EvaluateIesIntensity(P, LoadedIes, 0, 15) + 0.05)
          : L.Type === "spotlight"
            ? Number(P.intensity ?? 80) * 1.4
            : Number(P.intensity ?? P.lumens ?? 500) * 0.1;
      Sum += Int / R2;
    }
    return Sum;
  }

  SyncScene(SceneState, LoadedIes = {}) {
    this.SceneState = SceneState;
    this.LoadedIes = LoadedIes;
    if (!this.Renderer) return;

    this._ClearGroup(this.LuminaireGroup);
    this._ClearGroup(this.EnvironmentGroup);
    this.VolumeShaders = [];

    const Post = SceneState.Env?.Post || {};
    this.Renderer.toneMappingExposure = Clamp(Math.pow(2, (Post.ExposureEv ?? 0) * 0.25) * 1.15, 0.35, 3.6);
    const Fog = SceneState.Env?.Fog || {};
    const FogDensityNorm = Clamp((Fog.Density ?? 0.024) / 0.024, 0.2, 2.5);

    const SelId = SceneState.SelectedId || "";
    const IsSkySelection = ["env:sun", "env:moon", "env:stars", "env:envdome", "env:lensflare", "env:atmosphere"].includes(SelId);
    const ShowSky = !this.DarkRoomOnly || IsSkySelection;

    if (ShowSky) {
      const Sun = SceneState.SunState || { ElevationDeg: 25, AzimuthDeg: 180, Direction: [0, 0.4, -0.9], Intensity: 8, TintRgb: [1, 0.95, 0.88] };
      const Day = Clamp((Sun.ElevationDeg + 4) / 38, 0, 1);
      const Night = Clamp(-Sun.ElevationDeg / 18, 0, 1);
      const SkyCol = new THREE.Color("#04060d").lerp(new THREE.Color("#1f3a5c"), Day * 0.72);
      this.Scene.background = SkyCol;
      this.Scene.fog = new THREE.FogExp2(SkyCol.getHex(), Clamp((Fog.Density ?? 0.014) * 0.8, 0.002, 0.08));
      this.RoomAmbient.intensity = 0.06 + Day * 0.55;

      if (Sun.Enabled !== false && Sun.ElevationDeg > -10) {
        const SCol = new THREE.Color(...(Sun.TintRgb || [1, 0.95, 0.85]));
        const Dir = new THREE.DirectionalLight(SCol, Clamp(Sun.Intensity * 0.14, 0.1, 4) * Day);
        const Az = (Sun.AzimuthDeg || 180) * DEG2RAD;
        const El = (Sun.ElevationDeg || 25) * DEG2RAD;
        const V = new THREE.Vector3(Math.sin(Az) * Math.cos(El), Math.sin(El), -Math.cos(Az) * Math.cos(El));
        Dir.position.copy(V).multiplyScalar(36);
        Dir.castShadow = true;
        this.EnvironmentGroup.add(Dir);
        const Disc = new THREE.Mesh(new THREE.SphereGeometry(2.4, 24, 24), new THREE.MeshBasicMaterial({ color: SCol }));
        Disc.position.copy(V).multiplyScalar(115);
        this.EnvironmentGroup.add(Disc);
      }

      const Moon = SceneState.Env?.Moon || {};
      if (Moon.Visible !== false) {
        const MAz = (Moon.AzimuthDeg ?? 78) * DEG2RAD;
        const MEl = (Moon.ElevationDeg ?? 28) * DEG2RAD;
        const MV = new THREE.Vector3(Math.sin(MAz) * Math.cos(MEl), Math.sin(MEl), -Math.cos(MAz) * Math.cos(MEl));
        const TexOrNull = this._EnsureMoonTexture(Moon.TextureFile || "luna_2k.jpg");
        const LoadedMoonTex = TexOrNull && TexOrNull !== "loading" ? TexOrNull : null;
        const MMat = new THREE.MeshStandardMaterial({
          map: LoadedMoonTex,
          color: LoadedMoonTex ? "#eef3fb" : "#c6d3e3",
          emissive: "#8ca4c4",
          emissiveIntensity: 0.25 + Night * 0.55,
          roughness: 0.9,
        });
        const MMesh = new THREE.Mesh(new THREE.SphereGeometry(3.8, 32, 32), MMat);
        MMesh.position.copy(MV).multiplyScalar(92);
        this.EnvironmentGroup.add(MMesh);
      }
    } else {
      // Dark Volumetric Fog Bay — zero distracting sky ambient; purely real light through fog!
      this.Scene.background = new THREE.Color("#020203");
      this.Scene.fog = new THREE.FogExp2("#020305", Clamp((Fog.Density ?? 0.024) * 0.9, 0.004, 0.07));
      this.RoomAmbient.intensity = 0.035;
    }

    const DustUniforms = this.DustSystem?.material?.uniforms;
    let DustIdx = 0;

    for (const Light of SceneState.Lights || []) {
      if (!Light.Visible) continue;
      const HasTemp = ["ieslight", "tubelight", "ledlight", "ledstrip"].includes(Light.Type);
      const K = KelvinToRgb(Light.Props.temperature ?? 4300);
      const KCol = new THREE.Color(K[0] / 255, K[1] / 255, K[2] / 255);
      const TCol = new THREE.Color(Light.Props.color || "#ffffff");
      const FinalCol = HasTemp ? KCol.clone().lerp(TCol, 0.35) : TCol;
      const HexTint = "#" + FinalCol.getHexString();

      const { Root, VolumeShaders, DustInfo } = BuildFixture(
        Light,
        HexTint,
        LoadedIes,
        FogDensityNorm,
        this.ShowGizmos,
      );
      this.LuminaireGroup.add(Root);
      this.VolumeShaders.push(...VolumeShaders);

      if (DustUniforms && DustInfo && DustIdx < MAX_DUST_LIGHTS) {
        DustUniforms.uLightPos.value[DustIdx].copy(DustInfo.Pos);
        DustUniforms.uLightDir.value[DustIdx].copy(DustInfo.Dir);
        DustUniforms.uLightCol.value[DustIdx].copy(FinalCol);
        DustUniforms.uLightParam.value[DustIdx].set(1.3, DustInfo.Reach, DustInfo.CosCone, Light.Props.decay ?? 2.0);
        DustIdx++;
      }
    }

    if (DustUniforms) {
      for (let i = DustIdx; i < MAX_DUST_LIGHTS; i++) {
        DustUniforms.uLightParam.value[i].set(0, 10, -1, 2);
      }
      DustUniforms.uFogDensity.value = FogDensityNorm;
    }
  }

  Resize() {
    const W = Math.max(320, this.MountEl?.clientWidth || 800);
    const H = Math.max(240, this.MountEl?.clientHeight || 520);
    if (this.Renderer) {
      this.Renderer.setSize(W, H, false);
      this.Camera.aspect = W / H;
      this.Camera.updateProjectionMatrix();
    } else {
      this.Canvas.width = W;
      this.Canvas.height = H;
    }
  }

  _Animate() {
    if (this._Disposed) return;
    requestAnimationFrame(() => this._Animate());
    const TimeSec = (performance.now() - this._ClockStart) * 0.001;
    if (this.Renderer) {
      for (const M of this.VolumeShaders) {
        if (M.uniforms?.uTime) M.uniforms.uTime.value = TimeSec;
      }
      if (this.DustSystem?.material?.uniforms?.uTime) {
        this.DustSystem.material.uniforms.uTime.value = TimeSec;
      }
      this.Controls?.update();
      this.Renderer.render(this.Scene, this.Camera);
    }
  }
}
