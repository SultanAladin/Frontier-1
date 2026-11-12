/*==========================================================================================================================================
                                                     VIEWPORTRASTERIZATION.JS
==========================================================================================================================================*/
// 🧩 Three.js WebGL2 3D viewport with physical sky/cloud dome shader, 5 diagnostic lenses, raycast impact, and screen-space HUD projection.

import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import { EvaluateFaceNormal, Sub3, Add3, Mul3, Norm3, EvaluatePieceSdfSlice } from "./ConvexPartitioning.js";
import { StreamFragmentsIntoBatchedBuffers, RaycastFlatConvexFragments } from "./FastRuntimeFracture.js";

//------------------------------------------------------------------------------------------------------------------------
//                                           SKY + ATMOSPHERE + CLOUD DOME SHADERS
//------------------------------------------------------------------------------------------------------------------------

const SkyDomeVertexShader = /* glsl */ `
varying vec3 vWorldPos;
void main() {
    vec4 wp = modelMatrix * vec4(position, 1.0);
    vWorldPos = wp.xyz;
    gl_Position = projectionMatrix * viewMatrix * wp;
}
`;

const SkyDomeFragmentShader = /* glsl */ `
precision highp float;
varying vec3 vWorldPos;

uniform vec3 uSunDir;
uniform vec3 uSunTint;
uniform float uSunAngularCos;
uniform float uSkyGain;
uniform float uDirectGain;
uniform float uRayleighMul;
uniform float uRayleighScaleKm;
uniform float uMieMul;
uniform float uMieG;
uniform float uOzoneMul;
uniform float uTwilightGlow;
uniform float uHorizonLine;
uniform float uSkyBrightness;
uniform vec3 uSkyTint;
uniform vec3 uGroundTint;

uniform float uCloudEnabled;
uniform float uCloudCoverage;
uniform float uCloudDensity;
uniform float uCloudBaseKm;
uniform float uCloudTopKm;
uniform float uCloudScale;
uniform float uCloudPhaseG;
uniform vec2 uCloudWindOffset;

float hash31(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
}

float noise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash31(i + vec3(0,0,0));
    float n100 = hash31(i + vec3(1,0,0));
    float n010 = hash31(i + vec3(0,1,0));
    float n110 = hash31(i + vec3(1,1,0));
    float n001 = hash31(i + vec3(0,0,1));
    float n101 = hash31(i + vec3(1,0,1));
    float n011 = hash31(i + vec3(0,1,1));
    float n111 = hash31(i + vec3(1,1,1));
    return mix(
        mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
        mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y),
        f.z
    );
}

float fbm4(vec3 p) {
    float s = 0.0, a = 0.52;
    for (int i = 0; i < 4; i++) {
        s += a * noise3(p);
        p *= 2.04;
        a *= 0.5;
    }
    return s;
}

void main() {
    vec3 dir = normalize(vWorldPos - cameraPosition);
    vec3 sunD = normalize(uSunDir);
    float mu = dir.y;
    float cosTheta = dot(dir, sunD);

    // Subtle deep editorial studio vignette below horizon blended with atmospheric sky above
    if (mu < -0.04) {
        float nadirMix = clamp((-mu - 0.04) * 3.5, 0.0, 1.0);
        vec3 horizonFloor = vec3(0.045, 0.050, 0.048) + uGroundTint * 0.22 * max(0.05, sunD.y + 0.15);
        vec3 deepVoid = vec3(0.025, 0.027, 0.026);
        gl_FragColor = vec4(mix(horizonFloor, deepVoid, nadirMix), 1.0);
        return;
    }

    float zenith = max(0.015, mu + 0.045);
    float opticalMass = 1.0 / zenith;
    vec3 betaR = vec3(0.058, 0.1356, 0.331) * uRayleighMul * (8.0 / max(1.0, uRayleighScaleKm));
    float betaM = 0.042 * uMieMul;
    vec3 betaO = vec3(0.0065, 0.0188, 0.00085) * uOzoneMul;

    vec3 ext = exp(-(betaR + betaO + vec3(betaM)) * min(14.0, opticalMass * 0.55));
    float sunZen = max(0.02, sunD.y + 0.12);
    vec3 sunTrans = exp(-(betaR * 1.4 + betaO * 1.8 + vec3(betaM)) * (0.48 / sunZen));

    float phaseR = 0.059683 * (1.0 + cosTheta * cosTheta);
    float g = clamp(uMieG, -0.85, 0.96);
    float g2 = g * g;
    float phaseM = 0.119366 * ((1.0 - g2) * (1.0 + cosTheta * cosTheta)) / ((2.0 + g2) * pow(max(0.002, 1.0 + g2 - 2.0 * g * cosTheta), 1.5));

    vec3 sky = (betaR * phaseR * 16.0 + vec3(betaM * phaseM * 5.2)) * sunTrans * (1.0 - ext) * 4.2 * uSkyGain * uSunTint;
    // Multiple scattering ambient dome
    sky += vec3(0.032, 0.052, 0.082) * uRayleighMul * max(0.06, sunD.y + 0.24) * uSkyGain;

    // Twilight & horizon line
    float hBand = exp(-abs(mu) * 16.0);
    float twi = clamp(1.0 - abs(sunD.y - 0.02) * 3.0, 0.0, 1.0) * uTwilightGlow;
    sky += hBand * (vec3(0.46, 0.20, 0.07) * twi + vec3(0.12, 0.14, 0.15) * uHorizonLine);
    sky *= uSkyBrightness * uSkyTint;

    // Analytic crisp solar disc + circumsolar corona
    float sunEdge = smoothstep(uSunAngularCos - 0.00045, uSunAngularCos + 0.00015, cosTheta);
    if (mu > -0.01) {
        sky += sunTrans * uSunTint * uDirectGain * (sunEdge * 14.0 + pow(max(0.0, cosTheta), 380.0) * 1.4);
    }

    // Volumetric cloud slab single-scatter raymarch
    if (uCloudEnabled > 0.5 && mu > 0.012) {
        float tBase = uCloudBaseKm / mu;
        float tTop = max(tBase + 0.05, uCloudTopKm / mu);
        float dt = (tTop - tBase) / 8.0;
        float transC = 1.0;
        vec3 cloudLum = vec3(0.0);
        float hgC = 0.079577 * (1.0 - uCloudPhaseG * uCloudPhaseG) / pow(max(0.02, 1.0 + uCloudPhaseG * uCloudPhaseG - 2.0 * uCloudPhaseG * cosTheta), 1.5);

        for (int s = 0; s < 8; s++) {
            float t = tBase + (float(s) + 0.5) * dt;
            vec3 sampleP = dir * t;
            float hFrac = float(s) / 8.0;
            float prof = smoothstep(0.0, 0.2, hFrac) * smoothstep(1.0, 0.65, hFrac);
            vec3 uvw = vec3(sampleP.xz * (0.42 / max(0.25, uCloudScale)) + uCloudWindOffset, hFrac * 2.1);
            float n = fbm4(uvw);
            float thresh = 1.0 - clamp(uCloudCoverage, 0.0, 1.0);
            float dens = max(0.0, (n - thresh * 0.80) / max(0.15, 1.0 - thresh * 0.7)) * prof * uCloudDensity;
            if (dens > 0.002) {
                float stepExt = exp(-dens * dt * 2.6);
                // Self-shadow sample toward sun
                float nSun = fbm4(uvw + sunD.xzy * 0.35);
                float sunOcc = exp(-max(0.0, nSun - thresh * 0.8) * 1.9 * uCloudDensity);
                vec3 directC = sunTrans * uSunTint * uDirectGain * (0.55 + 2.8 * hgC) * sunOcc;
                vec3 ambientC = mix(vec3(0.08, 0.10, 0.13), vec3(0.26, 0.31, 0.38), hFrac) * max(0.12, sunD.y + 0.25);
                cloudLum += transC * (1.0 - stepExt) * (directC + ambientC);
                transC *= stepExt;
                if (transC < 0.03) break;
            }
        }
        float horizonFade = smoothstep(0.012, 0.11, mu);
        sky = mix(sky, sky * transC + cloudLum, horizonFade);
    }

    // Filmic Reinhard + gamma
    vec3 mapped = sky / (vec3(1.0) + sky * 0.85);
    mapped = pow(max(vec3(0.0), mapped), vec3(1.0 / 2.2));
    // Blend very gently with dark editorial base so UI cards pop with crisp contrast
    gl_FragColor = vec4(mapped * 0.92 + vec3(0.018, 0.020, 0.019), 1.0);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                           CLOUD-SHADOW TELEMETRY STAGE SHADER
//------------------------------------------------------------------------------------------------------------------------

const GroundFloorVertexShader = /* glsl */ `
varying vec3 vWorldPos;
varying vec2 vUv;
void main() {
    vUv = uv;
    vec4 wp = modelMatrix * vec4(position, 1.0);
    vWorldPos = wp.xyz;
    gl_Position = projectionMatrix * viewMatrix * wp;
}
`;

const GroundFloorFragmentShader = /* glsl */ `
precision highp float;
varying vec3 vWorldPos;
uniform vec2 uCloudWindOffset;
uniform float uCloudCoverage;
uniform float uCloudShadowStrength;
uniform vec3 uAccentColor;
uniform vec3 uSunDir;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float noise2(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1,0)), f.x), mix(hash21(i + vec2(0,1)), hash21(i + vec2(1,1)), f.x), f.y);
}

void main() {
    float dist = length(vWorldPos.xz);
    float fade = 1.0 - smoothstep(2.5, 13.5, dist);
    if (fade <= 0.002) discard;

    // Dot-matrix precision grid (inspired by 146a70d9e465a1a9def640bca5aa68f8.jpg)
    vec2 cell = fract(vWorldPos.xz * 2.5) - 0.5;
    float dotMark = 1.0 - smoothstep(0.028, 0.052, length(cell));

    // Major 1-metre technical rings around plinth
    float ring1 = 1.0 - smoothstep(0.008, 0.022, abs(dist - 1.35));
    float ring2 = 1.0 - smoothstep(0.008, 0.024, abs(dist - 2.70));

    // Moving cloud shadow attenuation
    vec2 cUv = vWorldPos.xz * 0.16 + uCloudWindOffset * 0.55 - uSunDir.xz * 0.4;
    float cn = 0.6 * noise2(cUv) + 0.4 * noise2(cUv * 2.1);
    float cMask = smoothstep(1.0 - uCloudCoverage, 1.02 - uCloudCoverage * 0.5, cn);
    float shadowMul = 1.0 - cMask * uCloudShadowStrength * 0.55;

    vec3 baseCol = vec3(0.055, 0.059, 0.057) * shadowMul;
    baseCol += vec3(0.18, 0.20, 0.19) * dotMark * 0.55 * fade;
    baseCol += uAccentColor * (ring1 * 0.26 + ring2 * 0.11) * fade;

    gl_FragColor = vec4(baseCol, fade * 0.94);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                             VIEWPORT RASTERIZATION HOST
//------------------------------------------------------------------------------------------------------------------------

export class ViewportRasterizationHost
{
    constructor(ContainerElement, Callbacks = {})
    {
        this.Container = ContainerElement;
        this.Callbacks = Callbacks;
        this.ShadingLens = "pbr"; // pbr | xray | telemetry | plexus | sdf
        this.ActiveTool = "strike"; // strike | orbit | gizmo
        this.SelectedFragmentIndex = -1;
        this.HoveredFragmentIndex = -1;
        this.ObjectLiftY = 0.98;
        this.ScanClock = 0.0;
        this.CloudClock = 0.0;
        this.SdfSliceY = 0.0;
        this.AccentHex = "#EFE838";

        this.FragmentSolids = [];
        this.FragmentEdges = [];
        this.BondLineVisual = null;
        this.DustPoints = null;
        this.SdfPlaneVisual = null;
        this.ImpactGizmoGroup = null;
        this.LocalCloudCage = null;

        this.BatchedModeActive = false;
        this.BatchSolidVisual = null;
        this.LastGpuStreamMs = 0.0;
        this.ActiveDrawCalls = 1;
        this.SdfDirty = true;

        this.Scene = new THREE.Scene();
        this.Camera = new THREE.PerspectiveCamera(42, 16 / 9, 0.08, 240);
        this.Camera.position.set(2.85, 2.18, 3.35);

        this.Raycaster = new THREE.Raycaster();
        this.PointerNdc = new THREE.Vector2();
        this.WebGlReady = false;

        this.InitializeRenderer();
        this.InitializeArenaFloor();
        this.InitializeBatchedRealtimeSurface();
        this.BindPointerEvents();
    }

    InitializeRenderer()
    {
        try
        {
            this.Renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false, powerPreference: "high-performance" });
            this.Renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
            this.Renderer.shadowMap.enabled = true;
            this.Renderer.shadowMap.type = THREE.PCFSoftShadowMap;
            this.Renderer.toneMapping = THREE.ACESFilmicToneMapping;
            this.Renderer.toneMappingExposure = 1.08;
            this.Container.appendChild(this.Renderer.domElement);

            this.Controls = new OrbitControls(this.Camera, this.Renderer.domElement);
            this.Controls.enableDamping = true;
            this.Controls.dampingFactor = 0.065;
            this.Controls.target.set(0, 0.92, 0);
            this.Controls.minDistance = 1.1;
            this.Controls.maxDistance = 14.0;
            this.Controls.maxPolarAngle = Math.PI * 0.495;
            this.WebGlReady = true;
        }
        catch
        {
            // Headless test fallback canvas
            this.FallbackCanvas = document.createElement("canvas");
            this.FallbackCanvas.className = "viewport-fallback-canvas";
            this.Container.appendChild(this.FallbackCanvas);
            this.WebGlReady = false;
        }
    }

    InitializeArenaFloor()
    {
        // Sky + Atmosphere + Volumetric Cloud Dome
        this.SkyUniforms = {
            uSunDir: { value: new THREE.Vector3(0.45, 0.56, -0.69) },
            uSunTint: { value: new THREE.Vector3(1.0, 0.96, 0.90) },
            uSunAngularCos: { value: Math.cos((0.533 * Math.PI) / 360.0) },
            uSkyGain: { value: 1.0 },
            uDirectGain: { value: 1.15 },
            uRayleighMul: { value: 1.0 },
            uRayleighScaleKm: { value: 8.0 },
            uMieMul: { value: 1.0 },
            uMieG: { value: 0.80 },
            uOzoneMul: { value: 1.0 },
            uTwilightGlow: { value: 0.62 },
            uHorizonLine: { value: 0.24 },
            uSkyBrightness: { value: 1.05 },
            uSkyTint: { value: new THREE.Vector3(0.96, 0.99, 1.0) },
            uGroundTint: { value: new THREE.Vector3(0.16, 0.17, 0.15) },
            uCloudEnabled: { value: 1.0 },
            uCloudCoverage: { value: 0.56 },
            uCloudDensity: { value: 1.05 },
            uCloudBaseKm: { value: 0.9 },
            uCloudTopKm: { value: 2.05 },
            uCloudScale: { value: 1.0 },
            uCloudPhaseG: { value: 0.36 },
            uCloudWindOffset: { value: new THREE.Vector2(0, 0) }
        };

        const SkyGeo = new THREE.SphereGeometry(95, 40, 28);
        const SkyMat = new THREE.ShaderMaterial({
            vertexShader: SkyDomeVertexShader,
            fragmentShader: SkyDomeFragmentShader,
            uniforms: this.SkyUniforms,
            side: THREE.BackSide,
            depthWrite: false
        });
        this.SkyDome = new THREE.Mesh(SkyGeo, SkyMat);
        this.Scene.add(this.SkyDome);

        // Lighting
        this.HemisphereLight = new THREE.HemisphereLight(0x9ec4eb, 0x1b1d1b, 0.82);
        this.Scene.add(this.HemisphereLight);

        this.SunLight = new THREE.DirectionalLight(0xfff4e2, 2.45);
        this.SunLight.position.set(5.5, 7.2, -6.5);
        this.SunLight.castShadow = true;
        this.SunLight.shadow.mapSize.set(2048, 2048);
        this.SunLight.shadow.camera.near = 0.5;
        this.SunLight.shadow.camera.far = 26.0;
        const sc = 4.5;
        this.SunLight.shadow.camera.left = -sc;
        this.SunLight.shadow.camera.right = sc;
        this.SunLight.shadow.camera.top = sc;
        this.SunLight.shadow.camera.bottom = -sc;
        this.SunLight.shadow.bias = -0.0005;
        this.Scene.add(this.SunLight);

        this.RimLight = new THREE.DirectionalLight(0x5eead4, 0.38);
        this.RimLight.position.set(-4.5, 2.8, 4.2);
        this.Scene.add(this.RimLight);

        // Dot-Matrix Telemetry Arena Floor + Shadow Receiver
        this.GroundUniforms = {
            uCloudWindOffset: { value: new THREE.Vector2(0, 0) },
            uCloudCoverage: { value: 0.56 },
            uCloudShadowStrength: { value: 0.68 },
            uAccentColor: { value: new THREE.Color(0xefe838) },
            uSunDir: { value: new THREE.Vector3(0.45, 0.56, -0.69) }
        };
        const FloorGeo = new THREE.PlaneGeometry(30, 30);
        FloorGeo.rotateX(-Math.PI * 0.5);
        const FloorMat = new THREE.ShaderMaterial({
            vertexShader: GroundFloorVertexShader,
            fragmentShader: GroundFloorFragmentShader,
            uniforms: this.GroundUniforms,
            transparent: true,
            depthWrite: false
        });
        this.FloorSurface = new THREE.Mesh(FloorGeo, FloorMat);
        this.Scene.add(this.FloorSurface);

        const ShadowCatcherGeo = new THREE.PlaneGeometry(18, 18);
        ShadowCatcherGeo.rotateX(-Math.PI * 0.5);
        const ShadowCatcherMat = new THREE.ShadowMaterial({ opacity: 0.48 });
        this.ShadowCatcher = new THREE.Mesh(ShadowCatcherGeo, ShadowCatcherMat);
        this.ShadowCatcher.position.y = 0.002;
        this.ShadowCatcher.receiveShadow = true;
        this.Scene.add(this.ShadowCatcher);

        // Fragments root group
        this.FragmentsGroup = new THREE.Group();
        this.Scene.add(this.FragmentsGroup);

        // Impact locus 3D reticle + Hertzian cone indicator
        this.ImpactGizmoGroup = new THREE.Group();
        const RingGeo = new THREE.RingGeometry(0.055, 0.075, 32);
        const RingMat = new THREE.MeshBasicMaterial({ color: 0xefe838, side: THREE.DoubleSide, transparent: true, opacity: 0.95, depthTest: false });
        this.ImpactRingVisual = new THREE.Mesh(RingGeo, RingMat);
        const OuterRingGeo = new THREE.RingGeometry(0.13, 0.138, 40);
        const OuterRingMat = new THREE.MeshBasicMaterial({ color: 0xffffff, side: THREE.DoubleSide, transparent: true, opacity: 0.45, depthTest: false });
        this.ImpactOuterRing = new THREE.Mesh(OuterRingGeo, OuterRingMat);
        const ArrowGeo = new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(0, 0, 0.28), new THREE.Vector3(0, 0, 0)]);
        const ArrowLine = new THREE.Line(ArrowGeo, new THREE.LineBasicMaterial({ color: 0xefe838, transparent: true, opacity: 0.88 }));
        this.ImpactGizmoGroup.add(this.ImpactRingVisual, this.ImpactOuterRing, ArrowLine);
        this.Scene.add(this.ImpactGizmoGroup);

        // Local Volumetric Cloud Box wireframe cage (visible when Clouds -> Local is active or toggled)
        const BoxEdges = new THREE.EdgesGeometry(new THREE.BoxGeometry(2, 2, 2));
        this.LocalCloudCage = new THREE.LineSegments(
            BoxEdges,
            new THREE.LineBasicMaterial({ color: 0x38b28b, transparent: true, opacity: 0.65 })
        );
        this.LocalCloudCage.visible = false;
        this.Scene.add(this.LocalCloudCage);
    }

    InitializeBatchedRealtimeSurface()
    {
        const MaxVerts = 49152;
        this.BatchMaxVerts = MaxVerts;
        this.BatchPosArray = new Float32Array(MaxVerts * 3);
        this.BatchNormArray = new Float32Array(MaxVerts * 3);
        this.BatchMetaArray = new Float32Array(MaxVerts * 2);

        this.BatchTransformData = new Float32Array(2 * 256 * 4);
        for (let i = 0; i < 256; ++i)
        {
            this.BatchTransformData[i * 8 + 3] = 1.0; // visible scale
            this.BatchTransformData[i * 8 + 7] = 1.0; // quat.w
        }

        this.BatchTransformTex = new THREE.DataTexture(
            this.BatchTransformData,
            2,
            256,
            THREE.RGBAFormat,
            THREE.FloatType
        );
        this.BatchTransformTex.magFilter = THREE.NearestFilter;
        this.BatchTransformTex.minFilter = THREE.NearestFilter;
        this.BatchTransformTex.generateMipmaps = false;
        this.BatchTransformTex.needsUpdate = true;

        this.BatchUniforms = {
            uPieceTransforms: { value: this.BatchTransformTex },
            uExteriorColor: { value: new THREE.Color("#8d908b") },
            uInteriorColor: { value: new THREE.Color("#6f716b") }
        };

        const BatchGeo = new THREE.BufferGeometry();
        BatchGeo.setAttribute("position", new THREE.BufferAttribute(this.BatchPosArray, 3));
        BatchGeo.setAttribute("normal", new THREE.BufferAttribute(this.BatchNormArray, 3));
        BatchGeo.setAttribute("aPieceMeta", new THREE.BufferAttribute(this.BatchMetaArray, 2));
        BatchGeo.setDrawRange(0, 0);

        const BatchMat = new THREE.MeshStandardMaterial({
            color: 0xffffff,
            roughness: 0.86,
            metalness: 0.04
        });
        BatchMat.onBeforeCompile = (shader) =>
        {
            shader.uniforms.uPieceTransforms = this.BatchUniforms.uPieceTransforms;
            shader.uniforms.uExteriorColor = this.BatchUniforms.uExteriorColor;
            shader.uniforms.uInteriorColor = this.BatchUniforms.uInteriorColor;

            shader.vertexShader = `
                attribute vec2 aPieceMeta;
                uniform sampler2D uPieceTransforms;
                varying float vIsInterior;
                vec3 rotateQuatBatch(vec4 q, vec3 v) {
                    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
                }
            \n` + shader.vertexShader
                .replace(
                    "#include <beginnormal_vertex>",
                    `#include <beginnormal_vertex>
                    int pIdxN = int(aPieceMeta.x + 0.5);
                    vec4 qRotN = texelFetch(uPieceTransforms, ivec2(1, pIdxN), 0);
                    objectNormal = rotateQuatBatch(qRotN, objectNormal);
                    vIsInterior = aPieceMeta.y;`
                )
                .replace(
                    "#include <begin_vertex>",
                    `#include <begin_vertex>
                    int pIdxP = int(aPieceMeta.x + 0.5);
                    vec4 pTr = texelFetch(uPieceTransforms, ivec2(0, pIdxP), 0);
                    vec4 qRotP = texelFetch(uPieceTransforms, ivec2(1, pIdxP), 0);
                    transformed = rotateQuatBatch(qRotP, transformed) * pTr.w + pTr.xyz;`
                );

            shader.fragmentShader = `
                uniform vec3 uExteriorColor;
                uniform vec3 uInteriorColor;
                varying float vIsInterior;
            \n` + shader.fragmentShader.replace(
                "#include <color_fragment>",
                `#include <color_fragment>
                diffuseColor.rgb *= mix(uExteriorColor, uInteriorColor, step(0.5, vIsInterior));`
            );
        };

        this.BatchSolidVisual = new THREE.Mesh(BatchGeo, BatchMat);
        this.BatchSolidVisual.castShadow = true;
        this.BatchSolidVisual.receiveShadow = true;
        this.BatchSolidVisual.frustumCulled = false;
        this.BatchSolidVisual.visible = false;
        this.Scene.add(this.BatchSolidVisual);
    }

    BindPointerEvents()
    {
        const TargetEl = this.WebGlReady ? this.Renderer.domElement : this.FallbackCanvas;
        if (!TargetEl) return;

        let DownPos = null;
        TargetEl.addEventListener("pointerdown", (ev) =>
        {
            DownPos = { x: ev.clientX, y: ev.clientY };
        });

        TargetEl.addEventListener("pointermove", (ev) =>
        {
            const Rect = TargetEl.getBoundingClientRect();
            if (Rect.width <= 0 || Rect.height <= 0) return;
            this.PointerNdc.x = ((ev.clientX - Rect.left) / Rect.width) * 2.0 - 1.0;
            this.PointerNdc.y = -((ev.clientY - Rect.top) / Rect.height) * 2.0 + 1.0;

            if (!this.WebGlReady || !this.CurrentPartition) return;
            this.Raycaster.setFromCamera(this.PointerNdc, this.Camera);

            let HitIdx = -1;
            if (this.BatchedModeActive)
            {
                const ro = this.Raycaster.ray.origin, rd = this.Raycaster.ray.direction;
                const FastHit = RaycastFlatConvexFragments(this.CurrentPartition.Fragments, ro.x, ro.y, ro.z, rd.x, rd.y, rd.z, this.ObjectLiftY);
                HitIdx = FastHit ? FastHit.FragmentIndex : -1;
            }
            else if (this.FragmentSolids.length)
            {
                const Hits = this.Raycaster.intersectObjects(this.FragmentSolids.filter((m) => m.visible), false);
                HitIdx = Hits.length ? Hits[0].object.userData.FragmentIndex : -1;
            }

            if (HitIdx !== this.HoveredFragmentIndex)
            {
                this.HoveredFragmentIndex = HitIdx;
                this.UpdateFragmentHighlighting();
                if (this.Callbacks.OnHoverFragment) this.Callbacks.OnHoverFragment(HitIdx);
            }
        });

        TargetEl.addEventListener("pointerup", (ev) =>
        {
            FallbackOrbit = null;
            if (!DownPos) return;
            const Moved = Math.hypot(ev.clientX - DownPos.x, ev.clientY - DownPos.y);
            DownPos = null;
            if (Moved > 5 || !this.WebGlReady || !this.CurrentPartition) return;

            const Rect = TargetEl.getBoundingClientRect();
            this.PointerNdc.x = ((ev.clientX - Rect.left) / Rect.width) * 2.0 - 1.0;
            this.PointerNdc.y = -((ev.clientY - Rect.top) / Rect.height) * 2.0 + 1.0;
            this.Raycaster.setFromCamera(this.PointerNdc, this.Camera);

            if (this.BatchedModeActive)
            {
                const ro = this.Raycaster.ray.origin, rd = this.Raycaster.ray.direction;
                const FastHit = RaycastFlatConvexFragments(this.CurrentPartition.Fragments, ro.x, ro.y, ro.z, rd.x, rd.y, rd.z, this.ObjectLiftY);
                if (FastHit)
                {
                    if (this.ActiveTool === "strike" || this.ActiveTool === "gizmo")
                    {
                        const ObjPoint = [FastHit.WorldPoint[0], FastHit.WorldPoint[1] - this.ObjectLiftY, FastHit.WorldPoint[2]];
                        const ImpactNormal = Norm3([-FastHit.Normal[0], -FastHit.Normal[1], -FastHit.Normal[2]]);
                        if (this.Callbacks.OnViewportStrike) this.Callbacks.OnViewportStrike(ObjPoint, ImpactNormal, this.ActiveTool === "strike");
                    }
                    else
                    {
                        this.SelectedFragmentIndex = FastHit.FragmentIndex;
                        if (this.Callbacks.OnSelectFragment) this.Callbacks.OnSelectFragment(FastHit.FragmentIndex);
                    }
                }
                return;
            }

            const Hits = this.Raycaster.intersectObjects(this.FragmentSolids.filter((m) => m.visible), false);

            if (Hits.length)
            {
                const Hit = Hits[0];
                const FragIdx = Hit.object.userData.FragmentIndex;
                if (this.ActiveTool === "strike" || this.ActiveTool === "gizmo")
                {
                    // Convert world hit point back into base-solid object coordinates
                    const ObjPoint = [
                        Hit.point.x,
                        Hit.point.y - this.ObjectLiftY,
                        Hit.point.z
                    ];
                    const WorldNorm = Hit.face
                        ? Hit.face.normal.clone().transformDirection(Hit.object.matrixWorld).negate()
                        : this.Camera.getWorldDirection(new THREE.Vector3());
                    const ImpactNormal = Norm3([WorldNorm.x, WorldNorm.y, WorldNorm.z]);
                    if (this.Callbacks.OnViewportStrike)
                    {
                        this.Callbacks.OnViewportStrike(ObjPoint, ImpactNormal, this.ActiveTool === "strike");
                    }
                }
                else
                {
                    this.SelectFragment(FragIdx);
                    if (this.Callbacks.OnSelectFragment) this.Callbacks.OnSelectFragment(FragIdx);
                }
            }
        });
    }

    Resize()
    {
        const w = Math.max(320, this.Container.clientWidth || 800);
        const h = Math.max(240, this.Container.clientHeight || 600);
        this.Camera.aspect = w / h;
        this.Camera.updateProjectionMatrix();
        if (this.WebGlReady)
        {
            this.Renderer.setSize(w, h, false);
        }
        else if (this.FallbackCanvas)
        {
            this.FallbackCanvas.width = w;
            this.FallbackCanvas.height = h;
        }
    }

    ZoomBy(Factor)
    {
        const Offset = this.Camera.position.clone().sub(this.Controls ? this.Controls.target : new THREE.Vector3(0, 0.9, 0));
        Offset.multiplyScalar(Factor);
        const Len = Offset.length();
        if (Len >= 1.15 && Len <= 13.5)
        {
            this.Camera.position.copy((this.Controls ? this.Controls.target : new THREE.Vector3(0, 0.9, 0)).clone().add(Offset));
        }
    }

    ResetCamera()
    {
        this.SetCameraView("orbit");
    }

    SetCameraView(Mode = "orbit")
    {
        const tgt = new THREE.Vector3(0, this.ObjectLiftY * 0.92, 0);
        if (Mode === "top") this.Camera.position.set(0.02, 5.1, 0.45);
        else if (Mode === "front") this.Camera.position.set(0.0, this.ObjectLiftY * 0.95, 4.4);
        else if (Mode === "side") this.Camera.position.set(4.4, this.ObjectLiftY * 0.95, 0.0);
        else this.Camera.position.set(2.85, 2.18, 3.35);

        if (this.Controls)
        {
            this.Controls.target.copy(tgt);
            this.Controls.update();
        }
        else
        {
            this.Camera.lookAt(tgt);
        }
    }

    SetAccentColor(HexString)
    {
        this.AccentHex = HexString;
        this.GroundUniforms.uAccentColor.value.set(HexString);
        if (this.ImpactRingVisual) this.ImpactRingVisual.material.color.set(HexString);
        this.ApplyShadingLens();
    }

    DisposeIndividualSolids()
    {
        for (const m of this.FragmentSolids)
        {
            m.geometry.dispose();
            if (Array.isArray(m.material)) m.material.forEach((mt) => mt.dispose());
            else m.material.dispose();
        }
        for (const e of this.FragmentEdges)
        {
            e.geometry.dispose();
            e.material.dispose();
        }
        this.FragmentsGroup.clear();
        this.FragmentSolids = [];
        this.FragmentEdges = [];
    }

    MaterializeIndividualSolids()
    {
        if (!this.CurrentPartition || this.FragmentSolids.length > 0) return;
        const { Fragments, MaterialSpec } = this.CurrentPartition;

        for (let i = 0; i < Fragments.length; ++i)
        {
            const Frag = Fragments[i];
            const Geo = Frag.EngineeringSurface
                ? BuildEngineeringBufferGeometry(Frag.EngineeringSurface, Frag.Centroid)
                : BuildFragmentBufferGeometry(Frag.Complex, Frag.Centroid);

            const ExtMat = new THREE.MeshPhysicalMaterial({
                color: new THREE.Color(MaterialSpec.ExteriorHex),
                roughness: MaterialSpec.Roughness,
                metalness: MaterialSpec.Metalness,
                transparent: MaterialSpec.Transmission > 0.1,
                opacity: MaterialSpec.Transmission > 0.1 ? 0.68 : 1.0,
                clearcoat: MaterialSpec.Transmission > 0.1 ? 0.9 : 0.08
            });
            const IntMat = new THREE.MeshStandardMaterial({
                color: new THREE.Color(MaterialSpec.InteriorHex),
                roughness: Math.min(1.0, MaterialSpec.Roughness + 0.08),
                metalness: MaterialSpec.Metalness * 0.5
            });

            const SolidVisual = new THREE.Mesh(Geo, [ExtMat, IntMat]);
            SolidVisual.position.set(Frag.Centroid[0], Frag.Centroid[1] + this.ObjectLiftY, Frag.Centroid[2]);
            SolidVisual.castShadow = true;
            SolidVisual.receiveShadow = true;
            SolidVisual.visible = Frag.Visible !== false;
            SolidVisual.userData = {
                FragmentIndex: i,
                Proximity: Frag.Proximity,
                StressRatio: Frag.StressRatio,
                PbrMaterials: [ExtMat, IntMat]
            };

            const HullSourceGeo = Frag.EngineeringSurface
                ? BuildFragmentBufferGeometry(Frag.ConvexHull || Frag.Complex, Frag.Centroid)
                : Geo;
            const EdgeGeo = new THREE.EdgesGeometry(HullSourceGeo, 18);
            if (HullSourceGeo !== Geo) HullSourceGeo.dispose();

            const EdgeMat = new THREE.LineBasicMaterial({
                color: 0xffffff,
                transparent: true,
                opacity: 0.16
            });
            const Edges = new THREE.LineSegments(EdgeGeo, EdgeMat);
            SolidVisual.add(Edges);

            this.FragmentsGroup.add(SolidVisual);
            this.FragmentSolids.push(SolidVisual);
            this.FragmentEdges.push(Edges);
        }
    }

    RebuildPartitionVisuals(PartitionResult, ImpactPoint, ImpactNormal)
    {
        this.DisposeIndividualSolids();

        const { Fragments, MaterialSpec, BaseMetrics } = PartitionResult;
        this.CurrentPartition = PartitionResult;
        this.ObjectLiftY = Math.max(0.02, -BaseMetrics.BoundsMin[1]);
        this.SdfDirty = true;

        const GpuStart = performance.now();
        const UseSingleDrawBatch = PartitionResult.Solver === "runtime_boussinesq" && this.ShadingLens === "pbr";

        if (UseSingleDrawBatch && this.BatchSolidVisual)
        {
            this.BatchedModeActive = true;
            const VertCount = StreamFragmentsIntoBatchedBuffers(
                Fragments,
                this.BatchPosArray,
                this.BatchNormArray,
                this.BatchMetaArray,
                this.BatchMaxVerts
            );
            const Geo = this.BatchSolidVisual.geometry;
            Geo.getAttribute("position").needsUpdate = true;
            Geo.getAttribute("normal").needsUpdate = true;
            Geo.getAttribute("aPieceMeta").needsUpdate = true;
            Geo.setDrawRange(0, VertCount);

            this.BatchUniforms.uExteriorColor.value.set(MaterialSpec.ExteriorHex);
            this.BatchUniforms.uInteriorColor.value.set(MaterialSpec.InteriorHex);
            this.BatchSolidVisual.material.roughness = MaterialSpec.Roughness;
            this.BatchSolidVisual.material.metalness = MaterialSpec.Metalness;
            this.BatchSolidVisual.material.transparent = MaterialSpec.Transmission > 0.1;
            this.BatchSolidVisual.material.opacity = MaterialSpec.Transmission > 0.1 ? 0.68 : 1.0;

            for (let i = 0; i < Fragments.length && i < 256; ++i)
            {
                const Frag = Fragments[i];
                const b = i * 8;
                this.BatchTransformData[b + 0] = Frag.Centroid[0];
                this.BatchTransformData[b + 1] = Frag.Centroid[1] + this.ObjectLiftY;
                this.BatchTransformData[b + 2] = Frag.Centroid[2];
                this.BatchTransformData[b + 3] = Frag.Visible !== false ? 1.0 : 0.0;
                this.BatchTransformData[b + 4] = 0.0;
                this.BatchTransformData[b + 5] = 0.0;
                this.BatchTransformData[b + 6] = 0.0;
                this.BatchTransformData[b + 7] = 1.0;
            }
            this.BatchTransformTex.needsUpdate = true;
            this.BatchSolidVisual.visible = true;
            this.FragmentsGroup.visible = false;
            this.ActiveDrawCalls = 1;
        }
        else
        {
            this.BatchedModeActive = false;
            if (this.BatchSolidVisual) this.BatchSolidVisual.visible = false;
            this.FragmentsGroup.visible = true;
            this.MaterializeIndividualSolids();
            this.ActiveDrawCalls = Fragments.length;
        }
        this.LastGpuStreamMs = performance.now() - GpuStart;

        // Adjacency Bond Plexus LineSegments
        if (this.BondLineVisual)
        {
            this.Scene.remove(this.BondLineVisual);
            this.BondLineVisual.geometry.dispose();
            this.BondLineVisual.material.dispose();
        }
        const BondCount = Math.max(1, PartitionResult.Bonds.length);
        const BondPositions = new Float32Array(BondCount * 6);
        const BondColors = new Float32Array(BondCount * 6);
        const BondGeo = new THREE.BufferGeometry();
        BondGeo.setAttribute("position", new THREE.BufferAttribute(BondPositions, 3));
        BondGeo.setAttribute("color", new THREE.BufferAttribute(BondColors, 3));
        const BondMat = new THREE.LineBasicMaterial({
            vertexColors: true,
            transparent: true,
            opacity: 0.88,
            depthTest: false
        });
        this.BondLineVisual = new THREE.LineSegments(BondGeo, BondMat);
        this.Scene.add(this.BondLineVisual);

        // Lazy SDF Slice Plane: only evaluate 64x64 SDF when SDF lens is actually active!
        if (this.ShadingLens === "sdf")
        {
            this.RebuildSdfSliceVisual();
            this.SdfDirty = false;
        }
        else if (this.SdfPlaneVisual)
        {
            this.SdfPlaneVisual.visible = false;
        }

        // Position Impact Gizmo
        this.UpdateImpactGizmo(ImpactPoint, ImpactNormal);
        this.ApplyShadingLens();
    }

    RebuildSdfSliceVisual(Resolution = 64)
    {
        if (!this.CurrentPartition) return;
        const SliceData = EvaluatePieceSdfSlice(this.CurrentPartition.Fragments, this.SdfSliceY, Resolution, 1.35);
        const Rgba = new Uint8Array(Resolution * Resolution * 4);

        for (let i = 0; i < SliceData.Field.length; ++i)
        {
            const sd = SliceData.Field[i];
            const bands = 0.5 + 0.5 * Math.cos(sd * 56.0);
            let r, g, b, a;
            if (sd <= 0)
            {
                // Interior iso-contours in glowing gold/emerald
                const depth = Math.min(1.0, -sd * 4.5);
                r = Math.round(56 + 185 * depth + 30 * bands);
                g = Math.round(178 + 60 * depth + 20 * bands);
                b = Math.round(139 - 60 * depth);
                a = 225;
            }
            else
            {
                const falloff = Math.exp(-sd * 3.8);
                r = Math.round(20 + 38 * bands * falloff);
                g = Math.round(32 + 95 * bands * falloff);
                b = Math.round(36 + 85 * bands * falloff);
                a = Math.round(185 * falloff);
            }
            const base = i * 4;
            Rgba[base] = r;
            Rgba[base + 1] = g;
            Rgba[base + 2] = b;
            Rgba[base + 3] = a;
        }

        if (this.SdfPlaneVisual)
        {
            this.Scene.remove(this.SdfPlaneVisual);
            this.SdfPlaneVisual.geometry.dispose();
            this.SdfPlaneVisual.material.map?.dispose();
            this.SdfPlaneVisual.material.dispose();
        }

        const Tex = new THREE.DataTexture(Rgba, Resolution, Resolution, THREE.RGBAFormat);
        Tex.needsUpdate = true;
        Tex.magFilter = THREE.LinearFilter;
        Tex.minFilter = THREE.LinearFilter;

        const PlaneGeo = new THREE.PlaneGeometry(2.7, 2.7);
        PlaneGeo.rotateX(-Math.PI * 0.5);
        const PlaneMat = new THREE.MeshBasicMaterial({
            map: Tex,
            transparent: true,
            side: THREE.DoubleSide,
            depthWrite: false
        });
        this.SdfPlaneVisual = new THREE.Mesh(PlaneGeo, PlaneMat);
        this.SdfPlaneVisual.position.set(0, this.ObjectLiftY + this.SdfSliceY, 0);
        this.SdfPlaneVisual.visible = this.ShadingLens === "sdf";
        this.Scene.add(this.SdfPlaneVisual);
    }

    UpdateImpactGizmo(ImpactPoint = [0, 0.2, 0.5], ImpactNormal = [0, 0, -1])
    {
        this.LastImpactWorld = new THREE.Vector3(
            ImpactPoint[0],
            ImpactPoint[1] + this.ObjectLiftY,
            ImpactPoint[2]
        );
        this.ImpactGizmoGroup.position.copy(this.LastImpactWorld);
        const LookTarget = this.LastImpactWorld.clone().sub(new THREE.Vector3(ImpactNormal[0], ImpactNormal[1], ImpactNormal[2]));
        this.ImpactGizmoGroup.lookAt(LookTarget);
    }

    SetShadingLens(LensName)
    {
        this.ShadingLens = LensName;
        this.ApplyShadingLens();
    }

    ApplyShadingLens()
    {
        if (!this.CurrentPartition) return;
        const Accent = new THREE.Color(this.AccentHex);

        const WantBatched = this.CurrentPartition.Solver === "runtime_boussinesq" && this.ShadingLens === "pbr";
        if (WantBatched && this.BatchSolidVisual)
        {
            this.BatchedModeActive = true;
            this.BatchSolidVisual.visible = true;
            this.FragmentsGroup.visible = false;
            this.ActiveDrawCalls = 1;
        }
        else
        {
            this.BatchedModeActive = false;
            if (this.BatchSolidVisual) this.BatchSolidVisual.visible = false;
            this.FragmentsGroup.visible = true;
            if (this.FragmentSolids.length === 0) this.MaterializeIndividualSolids();
            this.ActiveDrawCalls = this.FragmentSolids.length;
        }

        if (this.ShadingLens === "sdf" && this.SdfDirty)
        {
            this.RebuildSdfSliceVisual();
            this.SdfDirty = false;
        }

        for (let i = 0; i < this.FragmentSolids.length; ++i)
        {
            const SolidVisual = this.FragmentSolids[i];
            const Edges = this.FragmentEdges[i];
            const Prox = SolidVisual.userData.Proximity;

            if (this.ShadingLens === "pbr")
            {
                SolidVisual.material = SolidVisual.userData.PbrMaterials;
                Edges.material.color.setHex(0xffffff);
                Edges.material.opacity = 0.12;
            }
            else if (this.ShadingLens === "xray")
            {
                // Golden X-Ray core scan (inspired by bef5178e3f075257202d8236789c9a73.jpg)
                const CoreGlow = new THREE.Color().setHSL(0.13 - Prox * 0.03, 0.92, 0.16 + Prox * 0.42);
                const CortexMat = new THREE.MeshPhysicalMaterial({
                    color: 0x161816,
                    emissive: CoreGlow.clone().multiplyScalar(0.28),
                    roughness: 0.25,
                    transparent: true,
                    opacity: 0.32,
                    depthWrite: false
                });
                const SeamMat = new THREE.MeshBasicMaterial({
                    color: CoreGlow,
                    transparent: true,
                    opacity: 0.72,
                    blending: THREE.AdditiveBlending,
                    depthWrite: false
                });
                SolidVisual.material = [CortexMat, SeamMat];
                Edges.material.color.copy(Prox > 0.45 ? Accent : new THREE.Color(0xd4cf9a));
                Edges.material.opacity = 0.28 + Prox * 0.55;
            }
            else if (this.ShadingLens === "telemetry")
            {
                // Bioluminescent Emerald-Teal Telemetry Scan (inspired by 13edfbc6a1110a5abd8ec9099429cf07.jpg)
                const StrainCol = new THREE.Color().lerpColors(new THREE.Color(0x0d2b26), new THREE.Color(0x4ef2be), Math.pow(Prox, 0.85));
                const TeleMat = new THREE.MeshStandardMaterial({
                    color: StrainCol,
                    emissive: StrainCol.clone().multiplyScalar(0.52),
                    roughness: 0.35,
                    metalness: 0.1,
                    transparent: true,
                    opacity: 0.86
                });
                SolidVisual.material = [TeleMat, TeleMat];
                Edges.material.color.setHex(0x6fffe0);
                Edges.material.opacity = 0.55;
            }
            else if (this.ShadingLens === "plexus")
            {
                // Wireframe + Structural Bond Plexus (inspired by 146a70d9e465a1a9def640bca5aa68f8.jpg)
                const GhostMat = new THREE.MeshBasicMaterial({
                    color: Prox > 0.52 ? 0xd9cd3b : 0x232726,
                    transparent: true,
                    opacity: Prox > 0.52 ? 0.28 : 0.14,
                    depthWrite: false
                });
                SolidVisual.material = [GhostMat, GhostMat];
                Edges.material.color.setHex(Prox > 0.52 ? 0xefe838 : 0x9ca3af);
                Edges.material.opacity = 0.44;
            }
            else if (this.ShadingLens === "sdf")
            {
                const WireMat = new THREE.MeshBasicMaterial({
                    color: 0x1b2422,
                    transparent: true,
                    opacity: 0.20,
                    depthWrite: false
                });
                SolidVisual.material = [WireMat, WireMat];
                Edges.material.color.setHex(0x38b28b);
                Edges.material.opacity = 0.36;
            }
        }

        if (this.BondLineVisual)
        {
            this.BondLineVisual.visible = this.ShadingLens === "plexus" || this.CurrentPipeline === "hybrid";
        }
        if (this.SdfPlaneVisual)
        {
            this.SdfPlaneVisual.visible = this.ShadingLens === "sdf";
        }
        this.UpdateFragmentHighlighting();
    }

    SelectFragment(FragIndex)
    {
        this.SelectedFragmentIndex = FragIndex;
        this.UpdateFragmentHighlighting();
    }

    SetFragmentVisibility(FragIndex, VisibleFlag)
    {
        if (this.FragmentSolids[FragIndex])
        {
            this.FragmentSolids[FragIndex].visible = VisibleFlag;
        }
        if (this.BatchTransformData && FragIndex >= 0 && FragIndex < 256)
        {
            this.BatchTransformData[FragIndex * 8 + 3] = VisibleFlag ? 1.0 : 0.0;
            if (this.BatchTransformTex) this.BatchTransformTex.needsUpdate = true;
        }
    }

    UpdateFragmentHighlighting()
    {
        for (let i = 0; i < this.FragmentEdges.length; ++i)
        {
            const Edges = this.FragmentEdges[i];
            if (i === this.SelectedFragmentIndex)
            {
                Edges.material.color.set(this.AccentHex);
                Edges.material.opacity = 1.0;
            }
            else if (i === this.HoveredFragmentIndex)
            {
                Edges.material.color.setHex(0xffffff);
                Edges.material.opacity = 0.88;
            }
        }
    }

    ApplyTimelineSample(Sample, Fragments, PipelineMode)
    {
        this.CurrentPipeline = PipelineMode;
        if (!Sample || !Fragments || !Fragments.length) return;

        if (this.BatchedModeActive && this.BatchTransformTex)
        {
            const Count = Math.min(256, Fragments.length);
            for (let i = 0; i < Count; ++i)
            {
                const Frag = Fragments[i];
                const b = i * 8;
                this.BatchTransformData[b + 0] = Frag.Centroid[0] + Sample.Offsets[i * 3 + 0];
                this.BatchTransformData[b + 1] = Frag.Centroid[1] + this.ObjectLiftY + Sample.Offsets[i * 3 + 1];
                this.BatchTransformData[b + 2] = Frag.Centroid[2] + Sample.Offsets[i * 3 + 2];
                this.BatchTransformData[b + 3] = Frag.Visible !== false ? 1.0 : 0.0;
                this.BatchTransformData[b + 4] = Sample.Rotations[i * 4 + 0];
                this.BatchTransformData[b + 5] = Sample.Rotations[i * 4 + 1];
                this.BatchTransformData[b + 6] = Sample.Rotations[i * 4 + 2];
                this.BatchTransformData[b + 7] = Sample.Rotations[i * 4 + 3];
            }
            this.BatchTransformTex.needsUpdate = true;
        }

        for (let i = 0; i < this.FragmentSolids.length; ++i)
        {
            const SolidVisual = this.FragmentSolids[i];
            const Frag = Fragments[i];
            SolidVisual.position.set(
                Frag.Centroid[0] + Sample.Offsets[i * 3 + 0],
                Frag.Centroid[1] + this.ObjectLiftY + Sample.Offsets[i * 3 + 1],
                Frag.Centroid[2] + Sample.Offsets[i * 3 + 2]
            );
            SolidVisual.quaternion.set(
                Sample.Rotations[i * 4 + 0],
                Sample.Rotations[i * 4 + 1],
                Sample.Rotations[i * 4 + 2],
                Sample.Rotations[i * 4 + 3]
            );
        }

        // Update Bond Plexus world positions and strain colors
        if (this.BondLineVisual && this.CurrentPartition)
        {
            this.BondLineVisual.visible = this.ShadingLens === "plexus" || PipelineMode === "hybrid";
            const Bonds = Sample.ActiveBonds || this.CurrentPartition.Bonds;
            const PosAttr = this.BondLineVisual.geometry.getAttribute("position");
            const ColAttr = this.BondLineVisual.geometry.getAttribute("color");

            for (let b = 0; b < Bonds.length; ++b)
            {
                const Bond = Bonds[b];
                const ma = this.FragmentSolids[Bond.IndexA];
                const mb = this.FragmentSolids[Bond.IndexB];
                if (!ma || !mb || !ma.visible || !mb.visible) continue;

                const dist = ma.position.distanceTo(mb.position);
                const origDist = Math.hypot(
                    Bond.PointA[0] - Bond.PointB[0],
                    Bond.PointA[1] - Bond.PointB[1],
                    Bond.PointA[2] - Bond.PointB[2]
                );
                const stretchedOut = dist > origDist * 1.45 + 0.18;

                if (stretchedOut && this.ShadingLens !== "plexus")
                {
                    PosAttr.setXYZ(b * 2, 0, -50, 0);
                    PosAttr.setXYZ(b * 2 + 1, 0, -50, 0);
                }
                else
                {
                    PosAttr.setXYZ(b * 2, ma.position.x, ma.position.y, ma.position.z);
                    PosAttr.setXYZ(b * 2 + 1, mb.position.x, mb.position.y, mb.position.z);
                }

                // Intact bonds = crisp white; high-strain / broken bonds = glowing lime-gold (#efe838)
                const HighStrain = Bond.StrainRatio > 0.72 || Bond.BrokenRuntime;
                const r = HighStrain ? 0.94 : 0.88;
                const g = HighStrain ? 0.91 : 0.92;
                const bl = HighStrain ? 0.22 : 0.95;
                ColAttr.setXYZ(b * 2, r, g, bl);
                ColAttr.setXYZ(b * 2 + 1, r, g, bl);
            }
            PosAttr.needsUpdate = true;
            ColAttr.needsUpdate = true;
        }
    }

    UpdateEnvironmentUniforms(SunParams, SolarSolved, AtmosParams, CloudsParams, DeltaSeconds = 0.016)
    {
        const Dir = SolarSolved.Direction;
        this.SkyUniforms.uSunDir.value.set(Dir[0], Dir[1], Dir[2]);
        this.GroundUniforms.uSunDir.value.set(Dir[0], Dir[1], Dir[2]);

        this.SkyUniforms.uSunTint.value.set(SunParams.TintRgb[0], SunParams.TintRgb[1], SunParams.TintRgb[2]);
        this.SkyUniforms.uSunAngularCos.value = Math.cos((SunParams.AngularDiameterDeg * Math.PI) / 360.0);
        this.SkyUniforms.uSkyGain.value = SunParams.SkyGainMultiplier;
        this.SkyUniforms.uDirectGain.value = SunParams.DirectBeamMultiplier;

        this.SkyUniforms.uRayleighMul.value = AtmosParams.RayleighMultiplier;
        this.SkyUniforms.uRayleighScaleKm.value = AtmosParams.RayleighScaleHeightKm;
        this.SkyUniforms.uMieMul.value = AtmosParams.MieMultiplier;
        this.SkyUniforms.uMieG.value = AtmosParams.MiePhaseG;
        this.SkyUniforms.uOzoneMul.value = AtmosParams.OzoneMultiplier;
        this.SkyUniforms.uTwilightGlow.value = AtmosParams.TwilightGlow;
        this.SkyUniforms.uHorizonLine.value = AtmosParams.HorizonWhiteLine;
        this.SkyUniforms.uSkyBrightness.value = AtmosParams.SkyBrightness;
        this.SkyUniforms.uSkyTint.value.set(AtmosParams.SkyTintRgb[0], AtmosParams.SkyTintRgb[1], AtmosParams.SkyTintRgb[2]);
        this.SkyUniforms.uGroundTint.value.set(AtmosParams.GroundTintRgb[0], AtmosParams.GroundTintRgb[1], AtmosParams.GroundTintRgb[2]);

        const Glob = CloudsParams.Global;
        const EffectiveTopM = Math.min(Glob.CeilingHeightM, Glob.BaseHeightM + Glob.ThicknessM);
        this.SkyUniforms.uCloudEnabled.value = Glob.Enabled ? 1.0 : 0.0;
        this.SkyUniforms.uCloudCoverage.value = Glob.Coverage;
        this.SkyUniforms.uCloudDensity.value = Glob.DensityScale;
        this.SkyUniforms.uCloudBaseKm.value = Glob.BaseHeightM / 1000.0;
        this.SkyUniforms.uCloudTopKm.value = Math.max(Glob.BaseHeightM + 80.0, EffectiveTopM) / 1000.0;
        this.SkyUniforms.uCloudScale.value = Glob.FeatureScale;
        this.SkyUniforms.uCloudPhaseG.value = Glob.PhaseG;

        if (Glob.FollowWind)
        {
            this.CloudClock += DeltaSeconds;
        }
        const wx = this.CloudClock * Glob.WindXMps * 0.014;
        const wy = this.CloudClock * Glob.WindYMps * 0.014;
        this.SkyUniforms.uCloudWindOffset.value.set(wx, wy);
        this.GroundUniforms.uCloudWindOffset.value.set(wx, wy);
        this.GroundUniforms.uCloudCoverage.value = Glob.Enabled ? Glob.Coverage : 0.0;
        this.GroundUniforms.uCloudShadowStrength.value = Glob.ShadowStrength;

        // Update 3D Directional Sun Light color, elevation warmth, and shadow vector
        const SunEl = SolarSolved.ElevationDeg;
        const AboveHorizon = Math.max(0.0, Math.min(1.0, (SunEl + 4.0) / 22.0));
        const Warmth = Math.max(0.0, 1.0 - Math.max(0, SunEl) / 28.0);
        this.SunLight.position.set(Dir[0] * 9.0, Math.max(0.4, Dir[1] * 9.0), Dir[2] * 9.0);
        this.SunLight.intensity = AboveHorizon * 2.55 * SunParams.DirectBeamMultiplier * (1.0 - (Glob.Enabled ? Glob.Coverage * 0.35 : 0));
        this.SunLight.color.setRGB(
            SunParams.TintRgb[0],
            SunParams.TintRgb[1] * (1.0 - Warmth * 0.24),
            SunParams.TintRgb[2] * (1.0 - Warmth * 0.48)
        );
        this.HemisphereLight.intensity = (0.28 + 0.68 * AboveHorizon) * SunParams.SkyGainMultiplier * AtmosParams.SkyBrightness;

        // Local Volumetric Cloud Box 3D Cage
        const Loc = CloudsParams.Local;
        this.LocalCloudCage.position.set(Loc.CentreX, Loc.CentreY, Loc.CentreZ);
        this.LocalCloudCage.scale.set(Loc.HalfExtentX, Loc.HalfExtentY, Loc.HalfExtentZ);
    }

    ProjectImpactScreenCoordinates()
    {
        const w = this.Container.clientWidth || 800;
        const h = this.Container.clientHeight || 600;
        if (!this.LastImpactWorld) return { x: w * 0.5, y: h * 0.48, visible: true };

        const Projected = this.LastImpactWorld.clone().project(this.Camera);
        return {
            x: (Projected.x * 0.5 + 0.5) * w,
            y: (-Projected.y * 0.5 + 0.5) * h,
            visible: Projected.z < 1.0
        };
    }

    RenderFrame()
    {
        if (this.WebGlReady)
        {
            this.Controls.update();
            this.Renderer.render(this.Scene, this.Camera);
        }
        else if (this.FallbackCanvas)
        {
            this.RenderSoftwareFallback();
        }
    }

    RenderSoftwareFallback()
    {
        const ctx = this.FallbackCanvas.getContext("2d");
        if (!ctx || !this.CurrentPartition) return;
        const w = this.FallbackCanvas.width || 800;
        const h = this.FallbackCanvas.height || 600;
        const skyY = Math.max(0.15, Math.min(0.85, 0.56));
        const sunDir = this.SkyUniforms?.uSunDir?.value || { x: 0.4, y: 0.5, z: -0.6 };
        const sunUp = Math.max(0, Math.min(1, sunDir.y * 1.4 + 0.35));
        const grad = ctx.createLinearGradient(0, 0, 0, h);
        grad.addColorStop(0, `rgb(${Math.round(12 + 22 * sunUp)}, ${Math.round(18 + 34 * sunUp)}, ${Math.round(28 + 52 * sunUp)})`);
        grad.addColorStop(skyY, `rgb(${Math.round(18 + 26 * sunUp)}, ${Math.round(22 + 24 * sunUp)}, ${Math.round(24 + 28 * sunUp)})`);
        grad.addColorStop(1, "#090a0b");
        ctx.fillStyle = grad;
        ctx.fillRect(0, 0, w, h);

        this.Camera.lookAt(0, this.ObjectLiftY * 0.92, 0);
        this.Camera.updateMatrixWorld();
        this.Camera.updateProjectionMatrix();
        const Proj = (x, y, z) =>
        {
            const v = new THREE.Vector3(x, y, z).project(this.Camera);
            return [(v.x * 0.5 + 0.5) * w, (-v.y * 0.5 + 0.5) * h, v.z];
        };

        // Ground telemetry grid
        ctx.strokeStyle = "rgba(255,255,255,0.06)";
        ctx.lineWidth = 1;
        for (let g = -4; g <= 4; g += 1)
        {
            const [ax, ay, az] = Proj(g, 0, -4), [bx, by, bz] = Proj(g, 0, 4);
            if (az < 1 && bz < 1) { ctx.beginPath(); ctx.moveTo(ax, ay); ctx.lineTo(bx, by); ctx.stroke(); }
            const [cx, cy, cz] = Proj(-4, 0, g), [dx, dy, dz] = Proj(4, 0, g);
            if (cz < 1 && dz < 1) { ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(dx, dy); ctx.stroke(); }
        }

        // Collect all faces across visible fragments for painter sorting
        const MatSpec = this.CurrentPartition.MaterialSpec;
        const Faces = [];
        for (let i = 0; i < this.FragmentSolids.length; ++i)
        {
            const m = this.FragmentSolids[i];
            if (!m.visible) continue;
            const Frag = this.CurrentPartition.Fragments[i];
            const Selected = i === this.SelectedFragmentIndex;
            for (const Face of Frag.Complex.Faces)
            {
                const Pts = [];
                let depth = 0, valid = true;
                for (const vi of Face.Indices)
                {
                    const lv = Frag.Complex.Vertices[vi];
                    const wp = new THREE.Vector3(
                        lv[0] - Frag.Centroid[0],
                        lv[1] - Frag.Centroid[1],
                        lv[2] - Frag.Centroid[2]
                    ).applyQuaternion(m.quaternion).add(m.position);
                    const p = Proj(wp.x, wp.y, wp.z);
                    if (p[2] >= 1) valid = false;
                    Pts.push(p);
                    depth += wp.distanceToSquared(this.Camera.position);
                }
                if (valid && Pts.length >= 3)
                {
                    Faces.push({ Pts, Depth: depth / Pts.length, Interior: Face.Interior, Selected, Stress: Frag.StressRatio });
                }
            }
        }
        Faces.sort((a, b) => b.Depth - a.Depth);

        for (const f of Faces)
        {
            ctx.beginPath();
            ctx.moveTo(f.Pts[0][0], f.Pts[0][1]);
            for (let k = 1; k < f.Pts.length; ++k) ctx.lineTo(f.Pts[k][0], f.Pts[k][1]);
            ctx.closePath();
            if (this.ShadingLens === "xray")
            {
                ctx.fillStyle = f.Interior ? "rgba(232, 194, 122, 0.24)" : "rgba(62, 196, 109, 0.08)";
                ctx.strokeStyle = f.Selected ? "#ffffff" : "rgba(232, 194, 122, 0.72)";
            }
            else if (this.ShadingLens === "telemetry")
            {
                ctx.fillStyle = f.Stress > 0.55 ? "rgba(62, 196, 109, 0.34)" : "rgba(255, 255, 255, 0.14)";
                ctx.strokeStyle = "rgba(78, 242, 190, 0.55)";
            }
            else if (this.ShadingLens === "plexus")
            {
                ctx.fillStyle = "rgba(30, 35, 34, 0.22)";
                ctx.strokeStyle = f.Stress > 0.5 ? "#e8c27a" : "rgba(220, 220, 220, 0.5)";
            }
            else
            {
                ctx.fillStyle = f.Interior ? (MatSpec?.InteriorHex || "#8a7a62") + "cc" : (MatSpec?.ExteriorHex || "#8e9491") + "bb";
                ctx.strokeStyle = f.Selected ? this.AccentHex : "rgba(255,255,255,0.22)";
            }
            ctx.fill();
            ctx.lineWidth = f.Selected ? 1.8 : 0.9;
            ctx.stroke();
        }

        // Bond lines in plexus/hybrid modes
        if ((this.ShadingLens === "plexus" || this.CurrentPipeline === "hybrid") && this.CurrentPartition.Bonds)
        {
            ctx.lineWidth = 1.1;
            for (const Bond of this.CurrentPartition.Bonds)
            {
                const ma = this.FragmentSolids[Bond.A], mb = this.FragmentSolids[Bond.B];
                if (!ma || !mb) continue;
                const a = Proj(ma.position.x, ma.position.y, ma.position.z);
                const b = Proj(mb.position.x, mb.position.y, mb.position.z);
                ctx.strokeStyle = (Bond.StrainRatio > 0.72 || Bond.BrokenRuntime) ? "rgba(232, 194, 122, 0.85)" : "rgba(255, 255, 255, 0.45)";
                ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.stroke();
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                        FRAGMENT BUFFERGEOMETRY BUILDER
//------------------------------------------------------------------------------------------------------------------------

function BuildFragmentBufferGeometry(Complex, Centroid)
{
    const ExtPositions = [], ExtNormals = [];
    const IntPositions = [], IntNormals = [];

    for (const Face of Complex.Faces)
    {
        const n = EvaluateFaceNormal(Complex, Face);
        const PosTarget = Face.Interior ? IntPositions : ExtPositions;
        const NormTarget = Face.Interior ? IntNormals : ExtNormals;

        const v0 = Sub3(Complex.Vertices[Face.Indices[0]], Centroid);
        for (let i = 1; i + 1 < Face.Indices.length; ++i)
        {
            const v1 = Sub3(Complex.Vertices[Face.Indices[i]], Centroid);
            const v2 = Sub3(Complex.Vertices[Face.Indices[i + 1]], Centroid);
            PosTarget.push(v0[0], v0[1], v0[2], v1[0], v1[1], v1[2], v2[0], v2[1], v2[2]);
            NormTarget.push(n[0], n[1], n[2], n[0], n[1], n[2], n[0], n[1], n[2]);
        }
    }

    const AllPos = new Float32Array(ExtPositions.length + IntPositions.length);
    AllPos.set(ExtPositions, 0);
    AllPos.set(IntPositions, ExtPositions.length);

    const AllNorm = new Float32Array(ExtNormals.length + IntNormals.length);
    AllNorm.set(ExtNormals, 0);
    AllNorm.set(IntNormals, ExtNormals.length);

    const Geo = new THREE.BufferGeometry();
    Geo.setAttribute("position", new THREE.BufferAttribute(AllPos, 3));
    Geo.setAttribute("normal", new THREE.BufferAttribute(AllNorm, 3));
    Geo.clearGroups();
    if (ExtPositions.length > 0) Geo.addGroup(0, ExtPositions.length / 3, 0);
    if (IntPositions.length > 0) Geo.addGroup(ExtPositions.length / 3, IntPositions.length / 3, 1);
    Geo.computeBoundingSphere();
    return Geo;
}

function BuildEngineeringBufferGeometry(EngSurf, Centroid)
{
    const SrcPos = EngSurf.Positions;
    const SrcNorm = EngSurf.Normals;
    const Flags = EngSurf.InteriorFlags;
    const VertCount = Flags.length;

    let extVerts = 0, intVerts = 0;
    for (let i = 0; i < VertCount; ++i)
    {
        if (Flags[i]) intVerts++; else extVerts++;
    }

    const AllPos = new Float32Array(VertCount * 3);
    const AllNorm = new Float32Array(VertCount * 3);
    const cx = Centroid[0], cy = Centroid[1], cz = Centroid[2];

    let eCur = 0;
    let iCur = extVerts * 3;
    for (let v = 0; v < VertCount; ++v)
    {
        const s = v * 3;
        if (Flags[v] === 0)
        {
            AllPos[eCur] = SrcPos[s] - cx;
            AllPos[eCur + 1] = SrcPos[s + 1] - cy;
            AllPos[eCur + 2] = SrcPos[s + 2] - cz;
            AllNorm[eCur] = SrcNorm[s];
            AllNorm[eCur + 1] = SrcNorm[s + 1];
            AllNorm[eCur + 2] = SrcNorm[s + 2];
            eCur += 3;
        }
        else
        {
            AllPos[iCur] = SrcPos[s] - cx;
            AllPos[iCur + 1] = SrcPos[s + 1] - cy;
            AllPos[iCur + 2] = SrcPos[s + 2] - cz;
            AllNorm[iCur] = SrcNorm[s];
            AllNorm[iCur + 1] = SrcNorm[s + 1];
            AllNorm[iCur + 2] = SrcNorm[s + 2];
            iCur += 3;
        }
    }

    const Geo = new THREE.BufferGeometry();
    Geo.setAttribute("position", new THREE.BufferAttribute(AllPos, 3));
    Geo.setAttribute("normal", new THREE.BufferAttribute(AllNorm, 3));
    Geo.clearGroups();
    if (extVerts > 0) Geo.addGroup(0, extVerts, 0);
    if (intVerts > 0) Geo.addGroup(extVerts, intVerts, 1);
    Geo.computeBoundingSphere();
    return Geo;
}
