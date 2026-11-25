/*==========================================================================================================================================
                                                      TERRAINOCEANVIEWPORT.JS
==========================================================================================================================================*/
// Zero-Dependency WebGL2 Multi-Terrain Thin-Slab SDF Triangle Renderer:
// - Calibrated Dark-Studio Atmosphere & ACES Filmic Tonemapping (zero blinding glare!)
// - Animated Deep-Sapphire Fresnel/Gerstner Ocean (toggleable ON/OFF to inspect thin SDF slab underside)
// - Interactive 3D Spline Authoring + 3D XYZ (Up/Down Y-Axis + X/Z/XZ) Transform Gizmo Overlay
// - Real-Time 3D Terrain SDF Sculpting & Diagnostic Lenses

import {
    ExtractRegionTriangleMesh,
    ComputeHydrostaticWaterBodies,
    ApplyTerrainSculptDab,
    Clamp,
    SampleCatmullRomPolyline,
    CreateTerrainLayer
} from './TerrainLayerStack.js';

/*==========================================================================================================================================
                                                      MINIMAL 4x4 MATRIX HELPERS
==========================================================================================================================================*/
function Mat4Perspective(FovYRad, Aspect, Near, Far)
{
    const F = 1.0 / Math.tan(FovYRad * 0.5);
    const Nf = 1.0 / (Near - Far);
    return new Float32Array([
        F / Aspect, 0, 0, 0,
        0, F, 0, 0,
        0, 0, (Far + Near) * Nf, -1,
        0, 0, (2 * Far * Near) * Nf, 0
    ]);
}

function Mat4LookAt(Eye, Target, Up)
{
    let Zx = Eye[0] - Target[0], Zy = Eye[1] - Target[1], Zz = Eye[2] - Target[2];
    let Len = Math.hypot(Zx, Zy, Zz) || 1.0;
    Zx /= Len; Zy /= Len; Zz /= Len;

    let Xx = Up[1] * Zz - Up[2] * Zy;
    let Xy = Up[2] * Zx - Up[0] * Zz;
    let Xz = Up[0] * Zy - Up[1] * Zx;
    Len = Math.hypot(Xx, Xy, Xz) || 1.0;
    Xx /= Len; Xy /= Len; Xz /= Len;

    const Yx = Zy * Xz - Zz * Xy;
    const Yy = Zz * Xx - Zx * Xz;
    const Yz = Zx * Xy - Zy * Xx;

    return new Float32Array([
        Xx, Yx, Zx, 0,
        Xy, Yy, Zy, 0,
        Xz, Yz, Zz, 0,
        -(Xx * Eye[0] + Xy * Eye[1] + Xz * Eye[2]),
        -(Yx * Eye[0] + Yy * Eye[1] + Yz * Eye[2]),
        -(Zx * Eye[0] + Zy * Eye[1] + Zz * Eye[2]),
        1
    ]);
}

function Mat4Multiply(A, B)
{
    const Out = new Float32Array(16);
    for (let Col = 0; Col < 4; ++Col)
    {
        const B0 = B[Col * 4], B1 = B[Col * 4 + 1], B2 = B[Col * 4 + 2], B3 = B[Col * 4 + 3];
        Out[Col * 4]     = A[0] * B0 + A[4] * B1 + A[8]  * B2 + A[12] * B3;
        Out[Col * 4 + 1] = A[1] * B0 + A[5] * B1 + A[9]  * B2 + A[13] * B3;
        Out[Col * 4 + 2] = A[2] * B0 + A[6] * B1 + A[10] * B2 + A[14] * B3;
        Out[Col * 4 + 3] = A[3] * B0 + A[7] * B1 + A[11] * B2 + A[15] * B3;
    }
    return Out;
}

function ProjectWorldToScreen(ViewProj, WorldPt, Width, Height)
{
    const X = WorldPt[0], Y = WorldPt[1], Z = WorldPt[2];
    const Cx = ViewProj[0] * X + ViewProj[4] * Y + ViewProj[8]  * Z + ViewProj[12];
    const Cy = ViewProj[1] * X + ViewProj[5] * Y + ViewProj[9]  * Z + ViewProj[13];
    const Cw = ViewProj[3] * X + ViewProj[7] * Y + ViewProj[11] * Z + ViewProj[15];
    if (Cw <= 0.1) return null;
    const InvW = 1.0 / Cw;
    return [
        (Cx * InvW * 0.5 + 0.5) * Width,
        (0.5 - Cy * InvW * 0.5) * Height,
        Cw
    ];
}

function DistPointToSegment2D(Px, Py, Ax, Ay, Bx, By)
{
    const Ex = Bx - Ax, Ey = By - Ay;
    const LenSq = Math.max(1e-5, Ex * Ex + Ey * Ey);
    const T = Clamp(((Px - Ax) * Ex + (Py - Ay) * Ey) / LenSq, 0.0, 1.0);
    return Math.hypot(Px - (Ax + Ex * T), Py - (Ay + Ey * T));
}

/**
 * Closest parameter `s` along world line `AxisOrigin + s * AxisDir` to a 3D camera ray `RayOrigin + r * RayDir`.
 */
function ClosestAxisParamToRay(AxisOrigin, AxisDir, RayOrigin, RayDir)
{
    const Wx = AxisOrigin[0] - RayOrigin[0];
    const Wy = AxisOrigin[1] - RayOrigin[1];
    const Wz = AxisOrigin[2] - RayOrigin[2];

    const A = AxisDir[0] * AxisDir[0] + AxisDir[1] * AxisDir[1] + AxisDir[2] * AxisDir[2]; // 1
    const B = AxisDir[0] * RayDir[0]  + AxisDir[1] * RayDir[1]  + AxisDir[2] * RayDir[2];
    const C = RayDir[0]  * RayDir[0]  + RayDir[1]  * RayDir[1]  + RayDir[2]  * RayDir[2];  // 1
    const D = AxisDir[0] * Wx + AxisDir[1] * Wy + AxisDir[2] * Wz;
    const E = RayDir[0]  * Wx + RayDir[1]  * Wy + RayDir[2]  * Wz;

    const Denom = A * C - B * B;
    if (Math.abs(Denom) < 1e-5) return 0.0;
    return (B * E - C * D) / Denom;
}

/*==========================================================================================================================================
                              CALIBRATED DARK-STUDIO WEBGL2 SHADERS (ACES TONEMAPPED, NON-BLINDING)
==========================================================================================================================================*/
const SKY_VS = `#version 300 es
precision highp float;
out vec2 vNdc;
void main() {
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vNdc = pos * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 0.9999, 1.0);
}`;

const SKY_FS = `#version 300 es
precision highp float;
in vec2 vNdc;
out vec4 fragColor;
uniform vec3 uCamFwd;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform float uTanHalfFov;
uniform float uAspect;
uniform vec3 uSunDir;
uniform float uExposure;
uniform int uAtmosMode;

vec3 acesFilm(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 rd = normalize(uCamFwd + vNdc.x * uAspect * uTanHalfFov * uCamRight + vNdc.y * uTanHalfFov * uCamUp);

    // Calibrated Dark Studio palette matching 3D Clay Studio (#0b0b0b / #121212) — comfortable, high-contrast, zero glare!
    vec3 zenith  = vec3(0.012, 0.018, 0.028);
    vec3 horizon = vec3(0.038, 0.056, 0.078);
    vec3 nadir   = vec3(0.008, 0.011, 0.016);

    if (uAtmosMode == 1) {
        // Warm Coastal Dusk
        zenith  = vec3(0.018, 0.026, 0.048);
        horizon = vec3(0.095, 0.072, 0.068);
    } else if (uAtmosMode == 2) {
        // Crisp Twilight Daylight (still controlled, non-blinding)
        zenith  = vec3(0.032, 0.065, 0.125);
        horizon = vec3(0.115, 0.158, 0.210);
    }

    vec3 sky = rd.y >= 0.0
        ? mix(horizon, zenith, pow(clamp(rd.y, 0.0, 1.0), 0.55))
        : mix(horizon * 0.72, nadir, pow(clamp(-rd.y, 0.0, 1.0), 0.45));

    float sunDot = max(dot(rd, uSunDir), 0.0);
    sky += vec3(0.75, 0.56, 0.34) * pow(sunDot, 140.0) * 0.22;
    sky += vec3(0.32, 0.24, 0.15) * pow(sunDot, 12.0) * 0.06;

    vec3 mapped = acesFilm(sky * uExposure);
    fragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}`;

const TERRAIN_VS = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNorm;
layout(location = 2) in vec3 aCol;
layout(location = 3) in vec4 aAux;

uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vColor;
out vec4 vAux;

void main() {
    vWorldPos = aPos;
    vNormal = aNorm;
    vColor = aCol;
    vAux = aAux;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}`;

const TERRAIN_FS = `#version 300 es
precision highp float;
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vColor;
in vec4 vAux;

out vec4 fragColor;

uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform float uSeaLevel;
uniform float uExposure;
uniform int uAtmosMode;
uniform int uLensMode;
uniform int uShowContours;
uniform int uShowWireGrid;
uniform int uIsSelected;
uniform vec4 uBrushRing;

vec3 acesFilm(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 turboRamp(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c0 = vec3(0.05, 0.20, 0.42);
    vec3 c1 = vec3(0.08, 0.42, 0.28);
    vec3 c2 = vec3(0.58, 0.42, 0.10);
    vec3 c3 = vec3(0.62, 0.14, 0.09);
    if (t < 0.35) return mix(c0, c1, t / 0.35);
    if (t < 0.70) return mix(c1, c2, (t - 0.35) / 0.35);
    return mix(c2, c3, (t - 0.70) / 0.30);
}

void main() {
    vec3 fdx = dFdx(vWorldPos);
    vec3 fdy = dFdy(vWorldPos);
    vec3 faceN = normalize(cross(fdx, fdy));
    if (dot(faceN, vNormal) < 0.0) faceN = -faceN;
    vec3 N = normalize(mix(vNormal, faceN, 0.42));

    vec3 baseAlbedo = vColor;
    float relH = vWorldPos.y - uSeaLevel;

    if (uLensMode == 1) {
        baseAlbedo = turboRamp(vAux.x);
    } else if (uLensMode == 2) {
        baseAlbedo = turboRamp(clamp(relH / 260.0, 0.0, 1.0));
    } else if (uLensMode == 3) {
        float slopeDeg = acos(clamp(N.y, -1.0, 1.0)) * 57.29578;
        baseAlbedo = mix(vec3(0.06, 0.24, 0.16), vec3(0.58, 0.16, 0.08), smoothstep(18.0, 50.0, slopeDeg));
    }

    // Crisp Studio Key + Rim + Deep Valley Shadow Contrast
    float ndl = max(dot(N, uSunDir), 0.0);
    vec3 backDir = normalize(vec3(-uSunDir.x, 0.25, -uSunDir.z));
    float rim = max(dot(N, backDir), 0.0) * 0.16;

    vec3 skyAmb = mix(vec3(0.065, 0.075, 0.095), vec3(0.18, 0.22, 0.28), N.y * 0.5 + 0.5);
    vec3 sunCol = uAtmosMode == 1 ? vec3(1.42, 1.05, 0.76) : vec3(1.28, 1.18, 1.04);

    // Subtle cavity/slope darkening for rich sculptural depth
    float slopeAO = mix(0.76, 1.0, clamp(N.y * 0.65 + 0.35, 0.0, 1.0));
    vec3 lit = baseAlbedo * (skyAmb + sunCol * ndl + vec3(0.35, 0.45, 0.58) * rim) * slopeAO;

    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 H = normalize(uSunDir + V);
    float spec = pow(max(dot(N, H), 0.0), 36.0) * 0.035;
    lit += sunCol * spec;

    // ===================== NON-FLOODING SIMULATED WATER =====================
    // vAux.z = simulated water depth (m) · vAux.w = wetness film 0..1.
    // Thin rain films (< 0.08 m) render ONLY as darkened wet rock + specular —
    // they can NEVER flood hillslopes. True channeled rivers & pooled
    // depressions (depth over threshold) render as clear hydrostatic water.
    float wDepth = vAux.z;
    float wetFilm = clamp(vAux.w, 0.0, 1.0);
    float filmOnly = smoothstep(0.0, 0.08, wDepth) * (1.0 - smoothstep(0.08, 0.22, wDepth));
    float realWater = smoothstep(0.10, 0.55, wDepth);

    // Thin-film wet rock: darker, glossier — no water color at all.
    float wetDark = clamp(max(wetFilm * 0.85, filmOnly * 0.8), 0.0, 1.0);
    lit *= mix(1.0, 0.66, wetDark);
    lit += sunCol * pow(max(dot(N, H), 0.0), 64.0) * 0.09 * wetDark;

    // Real rivers / pools: clear turquoise water with depth-graded absorption.
    if (realWater > 0.002) {
        vec3 shallowW = vec3(0.055, 0.20, 0.21);
        vec3 deepW = vec3(0.035, 0.13, 0.17);
        vec3 wCol = mix(shallowW, deepW, clamp(wDepth / 1.8, 0.0, 1.0));
        float wLight = (skyAmb.y + sunCol.y * max(dot(vec3(0.0, 1.0, 0.0), uSunDir), 0.0) * 0.75);
        vec3 waterLit = wCol * (0.55 + wLight);
        float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0);
        waterLit += sunCol * pow(max(dot(N, H), 0.0), 90.0) * 0.32;
        waterLit += vec3(0.10, 0.13, 0.15) * fres * 0.35;
        lit = mix(lit, waterLit, clamp(realWater * 0.82, 0.0, 0.86));
    }

    // Damp shoreline band right at sea level
    if (uLensMode == 0 && relH > -2.5 && relH < 3.4 && N.y > -0.2) {
        float wet = 1.0 - smoothstep(-0.4, 3.0, relH);
        lit *= mix(1.0, 0.62, wet);
    }

    // Metric elevation contours
    if (uShowContours == 1 || uLensMode == 2) {
        float c25 = abs(fract(relH / 25.0 + 0.5) - 0.5) / max(fwidth(relH / 25.0), 1e-4);
        float c100 = abs(fract(relH / 100.0 + 0.5) - 0.5) / max(fwidth(relH / 100.0), 1e-4);
        float line = max((1.0 - smoothstep(0.0, 1.3, c25)) * 0.24, (1.0 - smoothstep(0.0, 1.45, c100)) * 0.52);
        lit = mix(lit, lit * 0.32 + vec3(0.025), line);
    }

    // Metric 25m grid overlay
    if (uShowWireGrid == 1 || uLensMode == 4) {
        vec2 g = abs(fract(vWorldPos.xz / 25.0 + 0.5) - 0.5) / max(fwidth(vWorldPos.xz / 25.0), vec2(1e-4));
        float gridLine = 1.0 - smoothstep(0.0, 1.25, min(g.x, g.y));
        lit = mix(lit, vec3(0.55, 0.58, 0.42), gridLine * 0.38);
    }

    if (uIsSelected == 0) {
        lit *= 0.80;
    }

    // 3D Sculpt Brush Ring Projection
    if (uBrushRing.w > 0.0) {
        float d = length(vWorldPos.xz - uBrushRing.xz);
        float ring = 1.0 - smoothstep(0.0, max(2.2, uBrushRing.w * 0.035), abs(d - uBrushRing.w));
        float centerDot = 1.0 - smoothstep(0.0, max(2.5, uBrushRing.w * 0.07), d);
        lit = mix(lit, vec3(0.78, 0.84, 0.52), clamp(ring * 0.9 + centerDot * 0.5, 0.0, 1.0));
    }

    // Subtle dark-studio horizon depth beyond 2.8 km (NEVER milky white!)
    float camDist = length(uCamPos - vWorldPos);
    float fog = 1.0 - exp(-max(0.0, camDist - 2600.0) * 0.000075);
    vec3 fogCol = uAtmosMode == 1 ? vec3(0.085, 0.068, 0.065) : (uAtmosMode == 2 ? vec3(0.10, 0.14, 0.19) : vec3(0.035, 0.052, 0.072));
    lit = mix(lit, fogCol, clamp(fog, 0.0, 0.42));

    vec3 mapped = acesFilm(lit * uExposure);
    fragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}`;

const OCEAN_VS = `#version 300 es
precision highp float;
layout(location = 0) in vec2 aGridUv;

uniform mat4 uViewProj;
uniform vec2 uOceanCenter;
uniform float uOceanSpan;
uniform float uSeaLevel;
uniform float uTime;
uniform float uWaveAmp;

out vec3 vWorldPos;
out float vWaveH;

float oceanWaveHeight(vec2 p, float t) {
    float w = 0.0;
    w += sin(p.x * 0.042 + t * 1.35) * 0.42;
    w += cos(p.y * 0.036 - t * 1.12) * 0.34;
    w += sin((p.x * 0.72 + p.y * 0.69) * 0.029 + t * 0.95) * 0.48;
    w += sin((p.x * -0.55 + p.y * 0.83) * 0.068 - t * 1.85) * 0.18;
    return w;
}

void main() {
    vec2 centered = aGridUv * 2.0 - 1.0;
    float r = length(centered);
    vec2 warped = centered * (1.0 + r * r * 2.6);
    vec2 worldXZ = uOceanCenter + warped * uOceanSpan;

    float distFade = 1.0 / (1.0 + r * r * 4.0);
    float h = oceanWaveHeight(worldXZ, uTime) * uWaveAmp * distFade;
    vWaveH = h;
    vWorldPos = vec3(worldXZ.x, uSeaLevel + h, worldXZ.y);
    gl_Position = uViewProj * vec4(vWorldPos, 1.0);
}`;

const OCEAN_FS = `#version 300 es
precision highp float;
in vec3 vWorldPos;
in float vWaveH;
out vec4 fragColor;

uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform float uTime;
uniform float uWaveAmp;
uniform float uExposure;
uniform int uAtmosMode;

vec3 acesFilm(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

float oceanWaveHeight(vec2 p, float t) {
    float w = 0.0;
    w += sin(p.x * 0.042 + t * 1.35) * 0.42;
    w += cos(p.y * 0.036 - t * 1.12) * 0.34;
    w += sin((p.x * 0.72 + p.y * 0.69) * 0.029 + t * 0.95) * 0.48;
    w += sin((p.x * -0.55 + p.y * 0.83) * 0.068 - t * 1.85) * 0.18;
    w += sin(p.x * 0.21 + p.y * 0.17 + t * 2.6) * 0.05;
    w += cos(p.x * 0.16 - p.y * 0.24 - t * 2.2) * 0.04;
    return w;
}

void main() {
    vec2 p = vWorldPos.xz;
    float camDist = length(uCamPos - vWorldPos);
    float atten = 1.0 / (1.0 + camDist * 0.00065);
    float eps = 1.25;

    float hL = oceanWaveHeight(p - vec2(eps, 0.0), uTime) * uWaveAmp * atten;
    float hR = oceanWaveHeight(p + vec2(eps, 0.0), uTime) * uWaveAmp * atten;
    float hD = oceanWaveHeight(p - vec2(0.0, eps), uTime) * uWaveAmp * atten;
    float hU = oceanWaveHeight(p + vec2(0.0, eps), uTime) * uWaveAmp * atten;

    vec3 N = normalize(vec3(hL - hR, 2.0 * eps * 2.6, hD - hU));
    vec3 V = normalize(uCamPos - vWorldPos);
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 4.5);

    // Rich dark abyssal sapphire & coastal emerald — zero milky washout!
    vec3 deepSea = vec3(0.006, 0.022, 0.038);
    vec3 midTeal = vec3(0.014, 0.052, 0.072);
    vec3 skyRefl = uAtmosMode == 1 ? vec3(0.095, 0.075, 0.072) : (uAtmosMode == 2 ? vec3(0.09, 0.13, 0.18) : vec3(0.042, 0.068, 0.098));

    float crest = clamp(vWaveH / max(0.2, uWaveAmp) * 0.5 + 0.5, 0.0, 1.0);
    vec3 waterBody = mix(deepSea, midTeal, crest * 0.75);
    vec3 col = mix(waterBody, skyRefl, clamp(0.08 + fresnel * 0.72, 0.0, 0.85));

    // Tight controlled sun glint
    vec3 H = normalize(uSunDir + V);
    float specTight = pow(max(dot(N, H), 0.0), 340.0);
    float specBroad = pow(max(dot(N, H), 0.0), 36.0);
    col += vec3(0.95, 0.82, 0.62) * (specTight * 0.85 + specBroad * 0.045);

    float whitecap = smoothstep(0.82, 0.99, crest) * atten * 0.10;
    col = mix(col, vec3(0.28, 0.34, 0.38), whitecap);

    float fog = 1.0 - exp(-max(0.0, camDist - 2200.0) * 0.00010);
    vec3 fogCol = uAtmosMode == 1 ? vec3(0.085, 0.068, 0.065) : (uAtmosMode == 2 ? vec3(0.10, 0.14, 0.19) : vec3(0.035, 0.052, 0.072));
    col = mix(col, fogCol, clamp(fog, 0.0, 0.75));

    vec3 mapped = acesFilm(col * uExposure);
    float alpha = mix(0.84, 0.96, clamp(fresnel + fog * 0.4, 0.0, 1.0));
    fragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), alpha);
}`;

const LINE_VS = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aCol;
uniform mat4 uViewProj;
out vec4 vCol;
void main() {
    vCol = aCol;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}`;

const LINE_FS = `#version 300 es
precision highp float;
in vec4 vCol;
out vec4 fragColor;
void main() {
    fragColor = vCol;
}`;

const WATERBODY_VS = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aWaterAux; // [depthM, isRiver]
uniform mat4 uViewProj;
uniform float uTime;
out vec3 vWorldPos;
out vec2 vWaterAux;
void main() {
    float ripple = sin(aPos.x * 0.14 + uTime * 1.9) * cos(aPos.z * 0.13 - uTime * 1.5) * 0.045;
    vec3 wp = vec3(aPos.x, aPos.y + ripple * clamp(aWaterAux.x, 0.0, 1.0), aPos.z);
    vWorldPos = wp;
    vWaterAux = aWaterAux;
    gl_Position = uViewProj * vec4(wp, 1.0);
}`;

const WATERBODY_FS = `#version 300 es
precision highp float;
in vec3 vWorldPos;
in vec2 vWaterAux;
uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform float uTime;
uniform float uExposure;
uniform int uAtmosMode;
out vec4 fragColor;

vec3 acesFilmWB(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    float depthM = max(0.0, vWaterAux.x);
    float isRiver = vWaterAux.y;
    vec2 p = vWorldPos.xz;
    float flow = isRiver > 0.5 ? uTime * 2.8 : uTime * 1.1;
    float nx = cos(p.x * 0.22 + flow) * 0.035 + sin((p.x + p.y) * 0.11 - flow * 0.7) * 0.025;
    float nz = sin(p.y * 0.21 - flow * 0.85) * 0.035 + cos((p.x - p.y) * 0.13 + flow * 0.6) * 0.025;
    vec3 N = normalize(vec3(nx, 1.0, nz));
    vec3 V = normalize(uCamPos - vWorldPos);
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 4.0);

    vec3 shallowTurquoise = vec3(0.026, 0.105, 0.118);
    vec3 deepTarn = vec3(0.008, 0.038, 0.065);
    vec3 bodyCol = mix(shallowTurquoise, deepTarn, clamp(depthM / 6.5, 0.0, 1.0));
    vec3 skyRefl = uAtmosMode == 1 ? vec3(0.11, 0.085, 0.08) : (uAtmosMode == 2 ? vec3(0.11, 0.16, 0.22) : vec3(0.055, 0.088, 0.125));
    vec3 col = mix(bodyCol, skyRefl, clamp(0.14 + fresnel * 0.68, 0.0, 0.86));

    vec3 H = normalize(uSunDir + V);
    float spec = pow(max(dot(N, H), 0.0), 220.0) * 0.72 + pow(max(dot(N, H), 0.0), 28.0) * 0.05;
    col += vec3(0.95, 0.86, 0.68) * spec;

    // Gentle foam glint on brisk river riffles
    if (isRiver > 0.5) {
        float riffle = smoothstep(0.55, 0.95, sin(p.x * 0.35 + flow * 1.4) * cos(p.y * 0.33 - flow * 1.2) * 0.5 + 0.5);
        col = mix(col, vec3(0.24, 0.30, 0.34), riffle * 0.16);
    }

    vec3 mapped = acesFilmWB(col * uExposure);
    float alpha = clamp(0.52 + smoothstep(0.0, 2.2, depthM) * 0.36 + fresnel * 0.14, 0.45, 0.93);
    fragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), alpha);
}`;

function CreateGlProgram(Gl, VsSrc, FsSrc)
{
    const Compile = (Type, Src) =>
    {
        const S = Gl.createShader(Type);
        Gl.shaderSource(S, Src);
        Gl.compileShader(S);
        if (!Gl.getShaderParameter(S, Gl.COMPILE_STATUS))
        {
            const Info = Gl.getShaderInfoLog(S);
            Gl.deleteShader(S);
            throw new Error(`GLSL Compile Error: ${Info}`);
        }
        return S;
    };
    const Vs = Compile(Gl.VERTEX_SHADER, VsSrc);
    const Fs = Compile(Gl.FRAGMENT_SHADER, FsSrc);
    const Prog = Gl.createProgram();
    Gl.attachShader(Prog, Vs);
    Gl.attachShader(Prog, Fs);
    Gl.linkProgram(Prog);
    Gl.deleteShader(Vs);
    Gl.deleteShader(Fs);
    if (!Gl.getProgramParameter(Prog, Gl.LINK_STATUS))
    {
        throw new Error(`GLSL Link Error: ${Gl.getProgramInfoLog(Prog)}`);
    }
    return Prog;
}

/*==========================================================================================================================================
                                                  TERRAIN OCEAN VIEWPORT CLASS
==========================================================================================================================================*/
export class TerrainOceanViewport
{
    constructor(Canvas, OverlayCanvas, World, Callbacks = {})
    {
        this.canvas = Canvas;
        this.overlayCanvas = OverlayCanvas;
        this.overlayCtx = OverlayCanvas ? OverlayCanvas.getContext('2d') : null;
        this.world = World;
        this.cb = Callbacks;

        const Gl = Canvas.getContext('webgl2', { antialias: true, alpha: false, depth: true, preserveDrawingBuffer: true });
        if (!Gl) throw new Error('WebGL2 is required for the 3D SDF Terrain + Ocean Viewport.');
        this.gl = Gl;

        // Camera spherical orbit
        this.target = [-120.0, 35.0, -80.0];
        this.yaw = 0.68;
        this.pitch = 0.50;
        this.distance = 2450.0;
        this.fovDeg = 45.0;

        // Calibrated Dark-Studio Lighting & Ocean parameters
        this.sunAzimuthDeg = 134.0;
        this.sunElevationDeg = 34.0;
        this.exposure = 0.94;
        this.atmosphereMode = 0; // 0 = Dark Obsidian Studio, 1 = Coastal Dusk, 2 = Soft Daylight
        this.waveAmplitude = 1.25;
        this.animateWaves = true;

        // Multi-Terrain Visibility Scope: 'all' | 'selected' | 'solo'
        this.viewScope = 'all';
        this.activeRegionId = World.regions[0]?.id || null;
        this.selectedRegionIds = new Set(World.regions[0] ? [World.regions[0].id] : []);

        // Diagnostic & Overlay toggles
        this.lensMode = 0;
        this.showContours = false;
        this.showWireGrid = false;
        this.showBounds3D = true;
        this.showGizmo = true;
        this.showInlandWater = true;

        // 3D Interaction mode: 'navigate' | 'spline' | 'spline-add' | 'raise' | 'lower' | 'smooth' | 'flatten' | 'pinch' | 'overhang' | 'cave' | 'restore'
        this.tool3D = 'spline'; // Start ready to click/drag 3D Spline Gizmos AND orbit cleanly!
        this.sculptRadiusM = 110.0;
        this.sculptStrength = 0.62;
        this.hoverHit = null;

        // Active 3D Spline + Gizmo State
        this.activeSplineLayerId = null;
        this.selectedKnotIndex = 0;
        this.hoverGizmoAxis = null; // 'x' | 'y' | 'z' | 'xz' | null
        this.hoverKnotIndex = -1;
        this._lastViewProj = null;

        // GPU Programs & Mesh Caches
        this.skyProg = CreateGlProgram(Gl, SKY_VS, SKY_FS);
        this.terrainProg = CreateGlProgram(Gl, TERRAIN_VS, TERRAIN_FS);
        this.oceanProg = CreateGlProgram(Gl, OCEAN_VS, OCEAN_FS);
        this.waterBodyProg = CreateGlProgram(Gl, WATERBODY_VS, WATERBODY_FS);
        this.lineProg = CreateGlProgram(Gl, LINE_VS, LINE_FS);

        this.emptyVao = Gl.createVertexArray();
        this.regionGpu = new Map();
        this._initOceanMesh(168);
        this._initDynamicLineBuffer();

        this.totalVisibleTriangles = 0;
        this.totalVisibleVertices = 0;
        this.visibleTerrainCount = 0;

        this.dragState = null;
        this.startTime = performance.now();
        this._bindEvents();
    }

    _initOceanMesh(GridDivs = 168)
    {
        const Gl = this.gl;
        const N = GridDivs + 1;
        const Uvs = new Float32Array(N * N * 2);
        let P = 0;
        for (let Z = 0; Z < N; ++Z)
        {
            for (let X = 0; X < N; ++X)
            {
                Uvs[P++] = X / GridDivs;
                Uvs[P++] = Z / GridDivs;
            }
        }
        const Indices = new Uint32Array(GridDivs * GridDivs * 6);
        let K = 0;
        for (let Z = 0; Z < GridDivs; ++Z)
        {
            for (let X = 0; X < GridDivs; ++X)
            {
                const I00 = Z * N + X, I10 = I00 + 1;
                const I01 = (Z + 1) * N + X, I11 = I01 + 1;
                Indices[K++] = I00; Indices[K++] = I01; Indices[K++] = I11;
                Indices[K++] = I00; Indices[K++] = I11; Indices[K++] = I10;
            }
        }

        const Vao = Gl.createVertexArray();
        Gl.bindVertexArray(Vao);
        const Vbo = Gl.createBuffer();
        Gl.bindBuffer(Gl.ARRAY_BUFFER, Vbo);
        Gl.bufferData(Gl.ARRAY_BUFFER, Uvs, Gl.STATIC_DRAW);
        Gl.enableVertexAttribArray(0);
        Gl.vertexAttribPointer(0, 2, Gl.FLOAT, false, 0, 0);

        const Ibo = Gl.createBuffer();
        Gl.bindBuffer(Gl.ELEMENT_ARRAY_BUFFER, Ibo);
        Gl.bufferData(Gl.ELEMENT_ARRAY_BUFFER, Indices, Gl.STATIC_DRAW);
        Gl.bindVertexArray(null);

        this.oceanVao = Vao;
        this.oceanIndexCount = Indices.length;
    }

    _initDynamicLineBuffer()
    {
        const Gl = this.gl;
        this.lineVao = Gl.createVertexArray();
        this.lineVbo = Gl.createBuffer();
        Gl.bindVertexArray(this.lineVao);
        Gl.bindBuffer(Gl.ARRAY_BUFFER, this.lineVbo);
        Gl.enableVertexAttribArray(0);
        Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, 28, 0);
        Gl.enableVertexAttribArray(1);
        Gl.vertexAttribPointer(1, 4, Gl.FLOAT, false, 28, 12);
        Gl.bindVertexArray(null);
    }

    /*======================================================================================================================================
                                            CAMERA, SPLINE HELPERS & RAYCASTING
    ======================================================================================================================================*/
    getCameraBasis()
    {
        const CosP = Math.cos(this.pitch), SinP = Math.sin(this.pitch);
        const CosY = Math.cos(this.yaw), SinY = Math.sin(this.yaw);
        const Eye = [
            this.target[0] + this.distance * CosP * SinY,
            this.target[1] + this.distance * SinP,
            this.target[2] + this.distance * CosP * CosY
        ];
        let Fx = this.target[0] - Eye[0], Fy = this.target[1] - Eye[1], Fz = this.target[2] - Eye[2];
        const Fl = Math.hypot(Fx, Fy, Fz) || 1.0;
        Fx /= Fl; Fy /= Fl; Fz /= Fl;

        let Rx = -Fz, Ry = 0.0, Rz = Fx;
        const Rl = Math.hypot(Rx, Ry, Rz) || 1.0;
        Rx /= Rl; Rz /= Rl;

        const Ux = Ry * Fz - Rz * Fy;
        const Uy = Rz * Fx - Rx * Fz;
        const Uz = Rx * Fy - Ry * Fx;

        return { eye: Eye, fwd: [Fx, Fy, Fz], right: [Rx, Ry, Rz], up: [Ux, Uy, Uz] };
    }

    getSunDirection()
    {
        const Az = (this.sunAzimuthDeg * Math.PI) / 180.0;
        const El = (this.sunElevationDeg * Math.PI) / 180.0;
        return [Math.cos(El) * Math.cos(Az), Math.sin(El), Math.cos(El) * Math.sin(Az)];
    }

    getVisibleRegions()
    {
        return this.world.regions.filter(R =>
        {
            if (!R.enabled) return false;
            if (this.viewScope === 'solo') return R.id === this.activeRegionId;
            if (this.viewScope === 'selected') return this.selectedRegionIds.has(R.id);
            return true;
        });
    }

    getActiveSplineContext()
    {
        const Reg = this.world.getRegion(this.activeRegionId);
        if (!Reg || !Reg.enabled) return null;
        let Lyr = Reg.stack.find(L => L.id === this.activeSplineLayerId && L.type === 'spline');
        if (!Lyr) Lyr = Reg.stack.find(L => L.type === 'spline' && L.enabled !== false);
        if (!Lyr || !Lyr.points || Lyr.points.length === 0) return null;
        this.selectedKnotIndex = Clamp(this.selectedKnotIndex | 0, 0, Lyr.points.length - 1);
        return { region: Reg, layer: Lyr };
    }

    sampleTerrainSurfaceY(Region, Wx, Wz)
    {
        const Rt = Region?.runtime;
        if (!Rt || !Rt.hasLand) return (this.world.seaLevel ?? 0) + 12.0;
        const Ix = Clamp(Math.round((Wx - Region.bounds.x) / Rt.stepX), 0, Rt.nx - 1);
        const Iz = Clamp(Math.round((Wz - Region.bounds.z) / Rt.stepZ), 0, Rt.nz - 1);
        return Rt.heights[Iz * Rt.nx + Ix];
    }

    getKnotWorldPos(Region, Knot)
    {
        const Y = Number.isFinite(Knot[2]) ? Knot[2] : this.sampleTerrainSurfaceY(Region, Knot[0], Knot[1]);
        return [Knot[0], Y, Knot[1]];
    }

    frameRegion(Region)
    {
        if (!Region) return;
        const B = Region.bounds;
        this.target[0] = B.x + B.width * 0.5;
        this.target[1] = Math.max(15.0, (Region.runtime?.maxHeight || 120.0) * 0.25);
        this.target[2] = B.z + B.depth * 0.5;
        this.distance = Math.max(340.0, Math.hypot(B.width, B.depth) * 0.96);
    }

    frameVisibleWorld()
    {
        const List = this.getVisibleRegions();
        const TargetList = List.length ? List : this.world.regions;
        if (!TargetList.length) return;
        let MinX = Infinity, MinZ = Infinity, MaxX = -Infinity, MaxZ = -Infinity, MaxH = 60.0;
        for (const R of TargetList)
        {
            MinX = Math.min(MinX, R.bounds.x);
            MinZ = Math.min(MinZ, R.bounds.z);
            MaxX = Math.max(MaxX, R.bounds.x + R.bounds.width);
            MaxZ = Math.max(MaxZ, R.bounds.z + R.bounds.depth);
            if (R.runtime?.maxHeight > MaxH) MaxH = R.runtime.maxHeight;
        }
        this.target[0] = 0.5 * (MinX + MaxX);
        this.target[1] = MaxH * 0.18;
        this.target[2] = 0.5 * (MinZ + MaxZ);
        this.distance = Math.max(520.0, Math.hypot(MaxX - MinX, MaxZ - MinZ) * 0.85);
    }

    screenRay(ClientX, ClientY)
    {
        const Rect = this.canvas.getBoundingClientRect();
        const NdcX = ((ClientX - Rect.left) / Math.max(1, Rect.width)) * 2.0 - 1.0;
        const NdcY = 1.0 - ((ClientY - Rect.top) / Math.max(1, Rect.height)) * 2.0;
        const Aspect = Math.max(1, Rect.width) / Math.max(1, Rect.height);
        const TanHalf = Math.tan((this.fovDeg * Math.PI) / 360.0);
        const { eye: Eye, fwd: Fwd, right: Right, up: Up } = this.getCameraBasis();

        let Dx = Fwd[0] + NdcX * Aspect * TanHalf * Right[0] + NdcY * TanHalf * Up[0];
        let Dy = Fwd[1] + NdcX * Aspect * TanHalf * Right[1] + NdcY * TanHalf * Up[1];
        let Dz = Fwd[2] + NdcX * Aspect * TanHalf * Right[2] + NdcY * TanHalf * Up[2];
        const L = Math.hypot(Dx, Dy, Dz) || 1.0;
        return { origin: Eye, dir: [Dx / L, Dy / L, Dz / L] };
    }

    raycastVisibleTerrains(ClientX, ClientY)
    {
        const { origin: Ro, dir: Rd } = this.screenRay(ClientX, ClientY);
        const Regions = this.getVisibleRegions();
        let BestHit = null;
        let BestT = 1e18;

        for (const R of Regions)
        {
            const Rt = R.runtime;
            if (!Rt || !Rt.hasLand) continue;

            const B = R.bounds;
            const MinY = Rt.slabBottom - 5.0;
            const MaxY = Math.max(Rt.maxHeight + 60.0, 80.0);

            const InvX = 1.0 / (Math.abs(Rd[0]) < 1e-7 ? 1e-7 : Rd[0]);
            const InvY = 1.0 / (Math.abs(Rd[1]) < 1e-7 ? 1e-7 : Rd[1]);
            const InvZ = 1.0 / (Math.abs(Rd[2]) < 1e-7 ? 1e-7 : Rd[2]);

            const Tx1 = (B.x - Ro[0]) * InvX, Tx2 = (B.x + B.width - Ro[0]) * InvX;
            const Ty1 = (MinY - Ro[1]) * InvY, Ty2 = (MaxY - Ro[1]) * InvY;
            const Tz1 = (B.z - Ro[2]) * InvZ, Tz2 = (B.z + B.depth - Ro[2]) * InvZ;

            const TEnter = Math.max(0.0, Math.min(Tx1, Tx2), Math.min(Ty1, Ty2), Math.min(Tz1, Tz2));
            const TExit = Math.min(BestT, Math.max(Tx1, Tx2), Math.max(Ty1, Ty2), Math.max(Tz1, Tz2));
            if (TExit < TEnter) continue;

            const Step = Math.max(3.5, Math.min(Rt.stepX, Rt.stepZ) * 0.65);
            let PrevT = TEnter;
            for (let T = TEnter; T <= TExit; T += Step)
            {
                const Px = Ro[0] + Rd[0] * T;
                const Py = Ro[1] + Rd[1] * T;
                const Pz = Ro[2] + Rd[2] * T;

                const Gx = (Px - B.x) / Rt.stepX;
                const Gz = (Pz - B.z) / Rt.stepZ;
                if (Gx < 0 || Gx >= Rt.nx - 1 || Gz < 0 || Gz >= Rt.nz - 1) { PrevT = T; continue; }

                const Ix = Math.floor(Gx), Iz = Math.floor(Gz);
                const D2 = Rt.sdf2D[Iz * Rt.nx + Ix];
                if (D2 > Rt.stepX) { PrevT = T; continue; }

                const Fx = Gx - Ix, Fz = Gz - Iz;
                const H00 = Rt.heights[Iz * Rt.nx + Ix], H10 = Rt.heights[Iz * Rt.nx + Ix + 1];
                const H01 = Rt.heights[(Iz + 1) * Rt.nx + Ix], H11 = Rt.heights[(Iz + 1) * Rt.nx + Ix + 1];
                const SurfH = (H00 * (1 - Fx) + H10 * Fx) * (1 - Fz) + (H01 * (1 - Fx) + H11 * Fx) * Fz;

                if (Py <= SurfH && Py >= Rt.slabBottom - 10.0)
                {
                    let Lo = PrevT, Hi = T;
                    for (let BStep = 0; BStep < 4; ++BStep)
                    {
                        const Mid = 0.5 * (Lo + Hi);
                        const Mx = Ro[0] + Rd[0] * Mid, My = Ro[1] + Rd[1] * Mid, Mz = Ro[2] + Rd[2] * Mid;
                        const Ux = Clamp((Mx - B.x) / Rt.stepX, 0, Rt.nx - 1.001);
                        const Uz = Clamp((Mz - B.z) / Rt.stepZ, 0, Rt.nz - 1.001);
                        const Jx = Math.floor(Ux), Jz = Math.floor(Uz);
                        const Mh = Rt.heights[Jz * Rt.nx + Jx];
                        if (My <= Mh) Hi = Mid; else Lo = Mid;
                    }
                    BestT = Hi;
                    BestHit = {
                        region: R,
                        point: [Ro[0] + Rd[0] * Hi, Ro[1] + Rd[1] * Hi, Ro[2] + Rd[2] * Hi]
                    };
                    break;
                }
                PrevT = T;
            }
        }
        return BestHit;
    }

    /*======================================================================================================================================
                                      3D SPLINE KNOT & XYZ (UP/DOWN) GIZMO HIT-TESTING
    ======================================================================================================================================*/
    _computeGizmoGeometry(Ctx3D, Width, Height)
    {
        if (!this.showGizmo || !this._lastViewProj || !Ctx3D) return null;
        const { region: Reg, layer: Lyr } = Ctx3D;
        const Knot = Lyr.points[this.selectedKnotIndex];
        if (!Knot) return null;

        const Origin = this.getKnotWorldPos(Reg, Knot);
        const { eye: Eye } = this.getCameraBasis();
        const CamDist = Math.hypot(Eye[0] - Origin[0], Eye[1] - Origin[1], Eye[2] - Origin[2]);
        const ScaleM = Clamp(CamDist * 0.115, 18.0, 520.0);

        const P0 = ProjectWorldToScreen(this._lastViewProj, Origin, Width, Height);
        if (!P0) return null;

        const Px = ProjectWorldToScreen(this._lastViewProj, [Origin[0] + ScaleM, Origin[1], Origin[2]], Width, Height);
        const PyUp = ProjectWorldToScreen(this._lastViewProj, [Origin[0], Origin[1] + ScaleM * 1.15, Origin[2]], Width, Height);
        const PyDn = ProjectWorldToScreen(this._lastViewProj, [Origin[0], Origin[1] - ScaleM * 0.52, Origin[2]], Width, Height);
        const Pz = ProjectWorldToScreen(this._lastViewProj, [Origin[0], Origin[1], Origin[2] + ScaleM], Width, Height);
        const Pxz = ProjectWorldToScreen(this._lastViewProj, [Origin[0] + ScaleM * 0.30, Origin[1], Origin[2] + ScaleM * 0.30], Width, Height);

        return { origin: Origin, scaleM: ScaleM, p0: P0, px: Px, pyUp: PyUp, pyDn: PyDn, pz: Pz, pxz: Pxz };
    }

    _hitTestSplineGizmoAndKnots(Sx, Sy, Width, Height)
    {
        const Ctx = this.getActiveSplineContext();
        if (!Ctx || !this._lastViewProj) return null;

        // 1. First test the 3D XYZ Translation Gizmo on the selected knot!
        const Giz = this._computeGizmoGeometry(Ctx, Width, Height);
        if (Giz)
        {
            // XZ planar square handle
            if (Giz.pxz && Math.hypot(Sx - Giz.pxz[0], Sy - Giz.pxz[1]) <= 12.0)
            {
                return { kind: 'gizmo', axis: 'xz', ...Ctx, origin: Giz.origin };
            }
            // Y Axis (Green UP / DOWN vertical shaft + both arrowheads) — highest priority for vertical elevation!
            if (Giz.pyUp)
            {
                const EndDn = Giz.pyDn || Giz.p0;
                const Dy = DistPointToSegment2D(Sx, Sy, EndDn[0], EndDn[1], Giz.pyUp[0], Giz.pyUp[1]);
                if (Dy <= 12.0) return { kind: 'gizmo', axis: 'y', ...Ctx, origin: Giz.origin };
            }
            if (Giz.px && DistPointToSegment2D(Sx, Sy, Giz.p0[0], Giz.p0[1], Giz.px[0], Giz.px[1]) <= 11.0)
            {
                return { kind: 'gizmo', axis: 'x', ...Ctx, origin: Giz.origin };
            }
            if (Giz.pz && DistPointToSegment2D(Sx, Sy, Giz.p0[0], Giz.p0[1], Giz.pz[0], Giz.pz[1]) <= 11.0)
            {
                return { kind: 'gizmo', axis: 'z', ...Ctx, origin: Giz.origin };
            }
        }

        // 2. Test clicking any Spline Knot sphere along any enabled spline in the active region!
        const Reg = Ctx.region;
        for (const Layer of Reg.stack)
        {
            if (Layer.type !== 'spline' || !Layer.enabled || !Layer.points) continue;
            for (let I = 0; I < Layer.points.length; ++I)
            {
                const WPos = this.getKnotWorldPos(Reg, Layer.points[I]);
                const Scr = ProjectWorldToScreen(this._lastViewProj, WPos, Width, Height);
                if (Scr && Math.hypot(Sx - Scr[0], Sy - Scr[1]) <= 13.5)
                {
                    return { kind: 'knot', region: Reg, layer: Layer, knotIndex: I, origin: WPos };
                }
            }
        }
        return null;
    }

    /*======================================================================================================================================
                                                3D VIEWPORT POINTER INTERACTION
    ======================================================================================================================================*/
    _bindEvents()
    {
        const El = this.canvas;
        El.addEventListener('contextmenu', E => E.preventDefault());

        El.addEventListener('pointerdown', E =>
        {
            const Rect = El.getBoundingClientRect();
            const Sx = E.clientX - Rect.left, Sy = E.clientY - Rect.top;

            // Pan: Middle Mouse or Shift+Right
            if (E.button === 1 || (E.button === 2 && E.shiftKey))
            {
                El.setPointerCapture(E.pointerId);
                this.dragState = { kind: 'pan', pointerId: E.pointerId, lastX: E.clientX, lastY: E.clientY };
                return;
            }

            // 1. Check 3D Spline Gizmo & Knot Handles on Left-Click (whenever Gizmo is visible and Alt is not held)
            if (E.button === 0 && !E.altKey && this.showGizmo)
            {
                const GizHit = this._hitTestSplineGizmoAndKnots(Sx, Sy, Rect.width, Rect.height);
                if (GizHit)
                {
                    El.setPointerCapture(E.pointerId);
                    this.activeSplineLayerId = GizHit.layer.id;
                    if (GizHit.kind === 'knot')
                    {
                        this.selectedKnotIndex = GizHit.knotIndex;
                        this.cb.onSplineKnotSelected?.(GizHit.region, GizHit.layer, GizHit.knotIndex);
                        // Also start Y-drag immediately if user drags the knot vertically with Ctrl, or XZ drag? Let's allow selecting cleanly, or dragging XZ!
                        return;
                    }

                    // Gizmo axis drag ('x' | 'y' | 'z' | 'xz')!
                    this.cb.onBeforeSplineGizmoEdit?.(GizHit.region, GizHit.layer);
                    const Knot = GizHit.layer.points[this.selectedKnotIndex];
                    const OrigKnot = [Knot[0], Knot[1], GizHit.origin[1]];
                    const Ray = this.screenRay(E.clientX, E.clientY);

                    let AxisVec = [0, 1, 0];
                    if (GizHit.axis === 'x') AxisVec = [1, 0, 0];
                    else if (GizHit.axis === 'z') AxisVec = [0, 0, 1];

                    const StartParam = GizHit.axis === 'xz'
                        ? 0
                        : ClosestAxisParamToRay(GizHit.origin, AxisVec, Ray.origin, Ray.dir);

                    // For XZ plane drag, intersect ray with horizontal plane y = origin[1]
                    let StartPlaneXZ = [OrigKnot[0], OrigKnot[1]];
                    if (GizHit.axis === 'xz' && Math.abs(Ray.dir[1]) > 1e-4)
                    {
                        const TPlane = (GizHit.origin[1] - Ray.origin[1]) / Ray.dir[1];
                        if (TPlane > 0)
                        {
                            StartPlaneXZ = [Ray.origin[0] + Ray.dir[0] * TPlane, Ray.origin[2] + Ray.dir[2] * TPlane];
                        }
                    }

                    this.dragState = {
                        kind: 'gizmo-drag',
                        pointerId: E.pointerId,
                        axis: GizHit.axis,
                        axisVec: AxisVec,
                        region: GizHit.region,
                        layer: GizHit.layer,
                        knotIndex: this.selectedKnotIndex,
                        gizmoOrigin: GizHit.origin,
                        origKnot: OrigKnot,
                        startParam: StartParam,
                        startPlaneXZ: StartPlaneXZ
                    };
                    return;
                }
            }

            // 2. Add 3D Spline Knot by clicking directly on the terrain ('spline-add' tool OR Shift+Click in 'spline' tool!)
            if (E.button === 0 && !E.altKey && (this.tool3D === 'spline-add' || (this.tool3D === 'spline' && E.shiftKey)))
            {
                const Hit = this.raycastVisibleTerrains(E.clientX, E.clientY);
                if (Hit)
                {
                    this._append3DSplineKnotAtHit(Hit);
                    return;
                }
            }

            const IsSculptBrush = ['raise', 'lower', 'smooth', 'flatten', 'pinch', 'overhang', 'cave', 'restore'].includes(this.tool3D);

            // 3. Orbit: Alt+Left, Right Mouse, or Left Mouse in 'navigate' / 'spline' mode (when not hitting gizmo)
            if (E.altKey || E.button === 2 || (!IsSculptBrush && E.button === 0))
            {
                El.setPointerCapture(E.pointerId);
                this.dragState = {
                    kind: 'orbit',
                    pointerId: E.pointerId,
                    startX: E.clientX, startY: E.clientY,
                    lastX: E.clientX, lastY: E.clientY,
                    moved: false,
                    shiftSelect: E.shiftKey || E.ctrlKey || E.metaKey
                };
                return;
            }

            // 4. 3D Terrain SDF Sculpting!
            if (IsSculptBrush && E.button === 0)
            {
                const Hit = this.raycastVisibleTerrains(E.clientX, E.clientY);
                if (!Hit)
                {
                    El.setPointerCapture(E.pointerId);
                    this.dragState = { kind: 'orbit', pointerId: E.pointerId, startX: E.clientX, startY: E.clientY, lastX: E.clientX, lastY: E.clientY, moved: true };
                    return;
                }

                El.setPointerCapture(E.pointerId);
                this.cb.onBeforeSculpt3D?.(Hit.region);
                ApplyTerrainSculptDab(this.world, Hit.region, Hit.point, this.tool3D, {
                    radius: this.sculptRadiusM,
                    strength: this.sculptStrength,
                    invert: E.ctrlKey,
                    flattenHeight: Hit.point[1]
                });
                Hit.region.meshDirty = true;
                this.cb.onSculptLive3D?.(Hit.region);

                this.dragState = {
                    kind: 'sculpt',
                    pointerId: E.pointerId,
                    region: Hit.region,
                    lastHitPos: Hit.point,
                    flattenHeight: Hit.point[1]
                };
            }
        });

        El.addEventListener('pointermove', E =>
        {
            const Rect = El.getBoundingClientRect();
            const Sx = E.clientX - Rect.left, Sy = E.clientY - Rect.top;
            const IsSculptBrush = ['raise', 'lower', 'smooth', 'flatten', 'pinch', 'overhang', 'cave', 'restore'].includes(this.tool3D);

            if (!this.dragState)
            {
                // Hover feedback for 3D Gizmo axes & knots
                const GizHit = this.showGizmo ? this._hitTestSplineGizmoAndKnots(Sx, Sy, Rect.width, Rect.height) : null;
                this.hoverGizmoAxis = GizHit?.kind === 'gizmo' ? GizHit.axis : null;
                this.hoverKnotIndex = GizHit?.kind === 'knot' ? GizHit.knotIndex : -1;
                El.style.cursor = GizHit ? 'grab' : (this.tool3D === 'spline-add' ? 'crosshair' : 'default');

                if (IsSculptBrush || this.tool3D === 'spline-add')
                {
                    this.hoverHit = this.raycastVisibleTerrains(E.clientX, E.clientY);
                }
                else
                {
                    this.hoverHit = null;
                }
                return;
            }

            const D = this.dragState;
            if (D.kind === 'gizmo-drag')
            {
                El.style.cursor = 'grabbing';
                const Ray = this.screenRay(E.clientX, E.clientY);
                const Knot = D.layer.points[D.knotIndex];
                if (!Knot) return;

                if (D.axis === 'xz')
                {
                    if (Math.abs(Ray.dir[1]) > 1e-4)
                    {
                        const TPlane = (D.gizmoOrigin[1] - Ray.origin[1]) / Ray.dir[1];
                        if (TPlane > 0 && TPlane < 40000)
                        {
                            const CurX = Ray.origin[0] + Ray.dir[0] * TPlane;
                            const CurZ = Ray.origin[2] + Ray.dir[2] * TPlane;
                            Knot[0] = Math.round(D.origKnot[0] + (CurX - D.startPlaneXZ[0]));
                            Knot[1] = Math.round(D.origKnot[1] + (CurZ - D.startPlaneXZ[1]));
                        }
                    }
                }
                else
                {
                    const CurParam = ClosestAxisParamToRay(D.gizmoOrigin, D.axisVec, Ray.origin, Ray.dir);
                    const Delta = CurParam - D.startParam;
                    if (D.axis === 'y')
                    {
                        // Move Spline Knot UP or DOWN smoothly in meters!
                        Knot[2] = Math.round(Clamp(D.origKnot[2] + Delta, -65.0, 950.0) * 10.0) / 10.0;
                    }
                    else if (D.axis === 'x')
                    {
                        Knot[0] = Math.round(D.origKnot[0] + Delta);
                    }
                    else if (D.axis === 'z')
                    {
                        Knot[1] = Math.round(D.origKnot[1] + Delta);
                    }
                }

                D.region.dirty = true;
                D.region.meshDirty = true;
                this.cb.onSplineGizmoLive?.(D.region, D.layer, D.knotIndex);
                return;
            }

            if (D.kind === 'orbit')
            {
                const Dx = E.clientX - D.lastX, Dy = E.clientY - D.lastY;
                if (Math.hypot(E.clientX - D.startX, E.clientY - D.startY) > 5) D.moved = true;
                this.yaw -= Dx * 0.0055;
                this.pitch = Clamp(this.pitch + Dy * 0.0052, 0.05, 1.48);
                D.lastX = E.clientX;
                D.lastY = E.clientY;
            }
            else if (D.kind === 'pan')
            {
                const Dx = E.clientX - D.lastX, Dy = E.clientY - D.lastY;
                const { right: Right, fwd: Fwd } = this.getCameraBasis();
                const HLen = Math.hypot(Fwd[0], Fwd[2]) || 1.0;
                const Hx = Fwd[0] / HLen, Hz = Fwd[2] / HLen;
                const PanScale = this.distance * 0.0012;
                this.target[0] += (-Dx * Right[0] + Dy * Hx) * PanScale;
                this.target[2] += (-Dx * Right[2] + Dy * Hz) * PanScale;
                D.lastX = E.clientX;
                D.lastY = E.clientY;
            }
            else if (D.kind === 'sculpt')
            {
                this.hoverHit = this.raycastVisibleTerrains(E.clientX, E.clientY);
                if (this.hoverHit)
                {
                    const Hit = this.hoverHit;
                    const StepDist = Math.hypot(Hit.point[0] - D.lastHitPos[0], Hit.point[2] - D.lastHitPos[2]);
                    if (StepDist >= Math.max(6.0, this.sculptRadiusM * 0.18))
                    {
                        ApplyTerrainSculptDab(this.world, Hit.region, Hit.point, this.tool3D, {
                            radius: this.sculptRadiusM,
                            strength: this.sculptStrength,
                            invert: E.ctrlKey,
                            prevPos: D.lastHitPos,
                            flattenHeight: D.flattenHeight
                        });
                        Hit.region.meshDirty = true;
                        D.lastHitPos = Hit.point;
                        this.cb.onSculptLive3D?.(Hit.region);
                    }
                }
            }
        });

        const EndDrag = E =>
        {
            if (!this.dragState) return;
            const D = this.dragState;
            this.dragState = null;
            El.style.cursor = 'default';
            if (El.hasPointerCapture?.(E.pointerId)) El.releasePointerCapture(E.pointerId);

            if (D.kind === 'gizmo-drag')
            {
                this.cb.onSplineGizmoFinished?.(D.region, D.layer, D.knotIndex);
            }
            else if (D.kind === 'orbit' && !D.moved && E.button === 0)
            {
                const Hit = this.raycastVisibleTerrains(E.clientX, E.clientY);
                if (Hit) this.cb.onPickRegionIn3D?.(Hit.region, D.shiftSelect);
            }
            else if (D.kind === 'sculpt')
            {
                this.cb.onSculptStrokeFinished3D?.(D.region);
            }
        };

        El.addEventListener('pointerup', EndDrag);
        El.addEventListener('pointercancel', EndDrag);
        El.addEventListener('pointerleave', () => { this.hoverHit = null; this.hoverGizmoAxis = null; });

        El.addEventListener('wheel', E =>
        {
            E.preventDefault();
            const Factor = Math.exp(E.deltaY * 0.0012);
            this.distance = Clamp(this.distance * Factor, 60.0, 15000.0);
        }, { passive: false });
    }

    _append3DSplineKnotAtHit(Hit)
    {
        const Reg = Hit.region;
        this.cb.onBeforeSplineGizmoEdit?.(Reg, null);
        if (Reg.id !== this.activeRegionId)
        {
            this.cb.onPickRegionIn3D?.(Reg, false);
        }

        let Lyr = Reg.stack.find(L => L.id === this.activeSplineLayerId && L.type === 'spline')
               || Reg.stack.find(L => L.type === 'spline');
        if (!Lyr)
        {
            Lyr = CreateTerrainLayer('spline', {
                name: '3D Graded Spline Landform',
                splineMode: 'grade',
                width: 105.0,
                amount: 32.0,
                falloff: { mode: 'none', distance: 100, edgeFloor: 1.0 },
                points: []
            });
            Reg.stack.push(Lyr);
        }

        const NewPt = [Math.round(Hit.point[0]), Math.round(Hit.point[2]), Math.round(Hit.point[1] * 10) / 10];
        Lyr.points.push(NewPt);
        this.activeSplineLayerId = Lyr.id;
        this.selectedKnotIndex = Lyr.points.length - 1;
        Reg.dirty = true;
        Reg.meshDirty = true;
        this.cb.onSplineKnotAdded3D?.(Reg, Lyr, this.selectedKnotIndex);
    }

    /*======================================================================================================================================
                                                GPU MESH SYNCHRONIZATION & RENDER LOOP
    ======================================================================================================================================*/
    markRegionMeshDirty(RegionId = null)
    {
        if (RegionId)
        {
            const R = this.world.getRegion(RegionId);
            if (R) R.meshDirty = true;
        }
        else
        {
            for (const R of this.world.regions) R.meshDirty = true;
        }
    }

    _syncRegionGpuBuffer(Region, PreviewLayerId = null)
    {
        const Gl = this.gl;
        let Entry = this.regionGpu.get(Region.id);
        if (!Entry)
        {
            Entry = {
                vao: Gl.createVertexArray(),
                vboPos: Gl.createBuffer(),
                vboNorm: Gl.createBuffer(),
                vboCol: Gl.createBuffer(),
                vboAux: Gl.createBuffer(),
                ibo: Gl.createBuffer(),
                watVao: Gl.createVertexArray(),
                watVboPos: Gl.createBuffer(),
                watVboAux: Gl.createBuffer(),
                watIbo: Gl.createBuffer(),
                watIndexCount: 0,
                indexCount: 0,
                triCount: 0,
                vertCount: 0
            };
            Gl.bindVertexArray(Entry.vao);

            Gl.bindBuffer(Gl.ARRAY_BUFFER, Entry.vboPos);
            Gl.enableVertexAttribArray(0);
            Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, 0, 0);

            Gl.bindBuffer(Gl.ARRAY_BUFFER, Entry.vboNorm);
            Gl.enableVertexAttribArray(1);
            Gl.vertexAttribPointer(1, 3, Gl.FLOAT, false, 0, 0);

            Gl.bindBuffer(Gl.ARRAY_BUFFER, Entry.vboCol);
            Gl.enableVertexAttribArray(2);
            Gl.vertexAttribPointer(2, 3, Gl.FLOAT, false, 0, 0);

            Gl.bindBuffer(Gl.ARRAY_BUFFER, Entry.vboAux);
            Gl.enableVertexAttribArray(3);
            Gl.vertexAttribPointer(3, 4, Gl.FLOAT, false, 0, 0);

            Gl.bindBuffer(Gl.ELEMENT_ARRAY_BUFFER, Entry.ibo);

            Gl.bindVertexArray(Entry.watVao);
            Gl.bindBuffer(Gl.ARRAY_BUFFER, Entry.watVboPos);
            Gl.enableVertexAttribArray(0);
            Gl.vertexAttribPointer(0, 3, Gl.FLOAT, false, 0, 0);
            Gl.bindBuffer(Gl.ARRAY_BUFFER, Entry.watVboAux);
            Gl.enableVertexAttribArray(1);
            Gl.vertexAttribPointer(1, 2, Gl.FLOAT, false, 0, 0);
            Gl.bindBuffer(Gl.ELEMENT_ARRAY_BUFFER, Entry.watIbo);

            Gl.bindVertexArray(null);

            this.regionGpu.set(Region.id, Entry);
            Region.meshDirty = true;
        }

        if (Region.dirty || Region.meshDirty)
        {
            const Mesh = ExtractRegionTriangleMesh(this.world, Region, {
                includeUnderside: true,
                previewFalloffLayerId: PreviewLayerId
            });

            Gl.bindBuffer(Gl.ARRAY_BUFFER, Entry.vboPos);
            Gl.bufferData(Gl.ARRAY_BUFFER, Mesh.positions, Gl.DYNAMIC_DRAW);

            Gl.bindBuffer(Gl.ARRAY_BUFFER, Entry.vboNorm);
            Gl.bufferData(Gl.ARRAY_BUFFER, Mesh.normals, Gl.DYNAMIC_DRAW);

            Gl.bindBuffer(Gl.ARRAY_BUFFER, Entry.vboCol);
            Gl.bufferData(Gl.ARRAY_BUFFER, Mesh.colors, Gl.DYNAMIC_DRAW);

            Gl.bindBuffer(Gl.ARRAY_BUFFER, Entry.vboAux);
            Gl.bufferData(Gl.ARRAY_BUFFER, Mesh.aux, Gl.DYNAMIC_DRAW);

            Gl.bindBuffer(Gl.ELEMENT_ARRAY_BUFFER, Entry.ibo);
            Gl.bufferData(Gl.ELEMENT_ARRAY_BUFFER, Mesh.indices, Gl.DYNAMIC_DRAW);

            const WatMesh = ComputeHydrostaticWaterBodies(this.world, Region);
            Gl.bindBuffer(Gl.ARRAY_BUFFER, Entry.watVboPos);
            Gl.bufferData(Gl.ARRAY_BUFFER, WatMesh.positions, Gl.DYNAMIC_DRAW);
            Gl.bindBuffer(Gl.ARRAY_BUFFER, Entry.watVboAux);
            Gl.bufferData(Gl.ARRAY_BUFFER, WatMesh.aux, Gl.DYNAMIC_DRAW);
            Gl.bindBuffer(Gl.ELEMENT_ARRAY_BUFFER, Entry.watIbo);
            Gl.bufferData(Gl.ELEMENT_ARRAY_BUFFER, WatMesh.indices, Gl.DYNAMIC_DRAW);
            Entry.watIndexCount = WatMesh.indices.length;

            Entry.indexCount = Mesh.indices.length;
            Entry.triCount = Mesh.triangleCount;
            Entry.vertCount = Mesh.vertexCount;
            Region.meshDirty = false;
        }

        return Entry;
    }

    render(PreviewFalloffLayerId = null)
    {
        const El = this.canvas;
        const Rect = El.getBoundingClientRect();
        if (Rect.width < 4 || Rect.height < 4) return;

        const Dpr = Math.min(window.devicePixelRatio || 1, 2);
        const W = Math.round(Rect.width * Dpr);
        const H = Math.round(Rect.height * Dpr);
        if (El.width !== W || El.height !== H)
        {
            El.width = W;
            El.height = H;
        }

        const Gl = this.gl;
        Gl.viewport(0, 0, W, H);
        Gl.clearDepth(1.0);
        Gl.clear(Gl.COLOR_BUFFER_BIT | Gl.DEPTH_BUFFER_BIT);

        const Aspect = W / H;
        const FovRad = (this.fovDeg * Math.PI) / 180.0;
        const { eye: Eye, fwd: Fwd, right: Right, up: Up } = this.getCameraBasis();
        const SunDir = this.getSunDirection();

        const Proj = Mat4Perspective(FovRad, Aspect, 4.0, 32000.0);
        const View = Mat4LookAt(Eye, this.target, [0, 1, 0]);
        const ViewProj = Mat4Multiply(Proj, View);
        this._lastViewProj = ViewProj;

        // 1. Calibrated Dark-Studio Sky Pass
        Gl.disable(Gl.DEPTH_TEST);
        Gl.disable(Gl.BLEND);
        Gl.useProgram(this.skyProg);
        Gl.uniform3fv(Gl.getUniformLocation(this.skyProg, 'uCamFwd'), Fwd);
        Gl.uniform3fv(Gl.getUniformLocation(this.skyProg, 'uCamRight'), Right);
        Gl.uniform3fv(Gl.getUniformLocation(this.skyProg, 'uCamUp'), Up);
        Gl.uniform1f(Gl.getUniformLocation(this.skyProg, 'uTanHalfFov'), Math.tan(FovRad * 0.5));
        Gl.uniform1f(Gl.getUniformLocation(this.skyProg, 'uAspect'), Aspect);
        Gl.uniform3fv(Gl.getUniformLocation(this.skyProg, 'uSunDir'), SunDir);
        Gl.uniform1f(Gl.getUniformLocation(this.skyProg, 'uExposure'), this.exposure);
        Gl.uniform1i(Gl.getUniformLocation(this.skyProg, 'uAtmosMode'), this.atmosphereMode);
        Gl.bindVertexArray(this.emptyVao);
        Gl.drawArrays(Gl.TRIANGLES, 0, 3);

        // 2. Thin-Slab Multi-Terrain Triangle Mesh Pass
        Gl.enable(Gl.DEPTH_TEST);
        Gl.depthFunc(Gl.LEQUAL);
        Gl.enable(Gl.CULL_FACE);
        Gl.cullFace(Gl.BACK);

        const VisibleRegions = this.getVisibleRegions();
        this.visibleTerrainCount = VisibleRegions.length;
        let TotalTris = 0, TotalVerts = 0;

        Gl.useProgram(this.terrainProg);
        Gl.uniformMatrix4fv(Gl.getUniformLocation(this.terrainProg, 'uViewProj'), false, ViewProj);
        Gl.uniform3fv(Gl.getUniformLocation(this.terrainProg, 'uCamPos'), Eye);
        Gl.uniform3fv(Gl.getUniformLocation(this.terrainProg, 'uSunDir'), SunDir);
        Gl.uniform1f(Gl.getUniformLocation(this.terrainProg, 'uSeaLevel'), this.world.seaLevel ?? 0.0);
        Gl.uniform1f(Gl.getUniformLocation(this.terrainProg, 'uExposure'), this.exposure);
        Gl.uniform1i(Gl.getUniformLocation(this.terrainProg, 'uAtmosMode'), this.atmosphereMode);
        Gl.uniform1i(Gl.getUniformLocation(this.terrainProg, 'uLensMode'), this.lensMode);
        Gl.uniform1i(Gl.getUniformLocation(this.terrainProg, 'uShowContours'), this.showContours ? 1 : 0);
        Gl.uniform1i(Gl.getUniformLocation(this.terrainProg, 'uShowWireGrid'), this.showWireGrid ? 1 : 0);

        const BrushRadius = this.tool3D === 'spline-add' ? 18.0 : this.sculptRadiusM;
        const BrushUniform = this.hoverHit
            ? [this.hoverHit.point[0], this.hoverHit.point[1], this.hoverHit.point[2], BrushRadius]
            : [0, 0, 0, -1];
        Gl.uniform4fv(Gl.getUniformLocation(this.terrainProg, 'uBrushRing'), BrushUniform);

        const LocSelected = Gl.getUniformLocation(this.terrainProg, 'uIsSelected');
        for (const Region of VisibleRegions)
        {
            const Gpu = this._syncRegionGpuBuffer(Region, Region.id === this.activeRegionId ? PreviewFalloffLayerId : null);
            if (Gpu.indexCount === 0) continue;

            const IsSel = this.selectedRegionIds.has(Region.id) || this.viewScope === 'all';
            Gl.uniform1i(LocSelected, IsSel ? 1 : 0);
            Gl.bindVertexArray(Gpu.vao);
            Gl.drawElements(Gl.TRIANGLES, Gpu.indexCount, Gl.UNSIGNED_INT, 0);

            TotalTris += Gpu.triCount;
            TotalVerts += Gpu.vertCount;
        }
        this.totalVisibleTriangles = TotalTris;
        this.totalVisibleVertices = TotalVerts;

        // 3. 3D Terrain Bounds Cages
        if (this.showBounds3D && VisibleRegions.length > 0)
        {
            this._renderBounds3D(ViewProj, VisibleRegions);
        }

        // 3B. Hydrostatic Closed-Basin Inland Lakes & Trunk Rivers Pass (`WATERBODY_VS/FS`)
        if (this.showInlandWater && VisibleRegions.length > 0)
        {
            const ElapsedW = this.animateWaves ? (performance.now() - this.startTime) * 0.001 : 1.25;
            Gl.disable(Gl.CULL_FACE);
            Gl.enable(Gl.BLEND);
            Gl.blendFunc(Gl.SRC_ALPHA, Gl.ONE_MINUS_SRC_ALPHA);
            Gl.depthMask(false);

            Gl.useProgram(this.waterBodyProg);
            Gl.uniformMatrix4fv(Gl.getUniformLocation(this.waterBodyProg, 'uViewProj'), false, ViewProj);
            Gl.uniform3fv(Gl.getUniformLocation(this.waterBodyProg, 'uCamPos'), Eye);
            Gl.uniform3fv(Gl.getUniformLocation(this.waterBodyProg, 'uSunDir'), SunDir);
            Gl.uniform1f(Gl.getUniformLocation(this.waterBodyProg, 'uTime'), ElapsedW);
            Gl.uniform1f(Gl.getUniformLocation(this.waterBodyProg, 'uExposure'), this.exposure);
            Gl.uniform1i(Gl.getUniformLocation(this.waterBodyProg, 'uAtmosMode'), this.atmosphereMode);

            for (const Region of VisibleRegions)
            {
                const Gpu = this.regionGpu.get(Region.id);
                if (!Gpu || Gpu.watIndexCount === 0) continue;
                Gl.bindVertexArray(Gpu.watVao);
                Gl.drawElements(Gl.TRIANGLES, Gpu.watIndexCount, Gl.UNSIGNED_INT, 0);
            }

            Gl.depthMask(true);
            Gl.disable(Gl.BLEND);
        }

        // 4. Deep-Sapphire Animated Ocean Pass
        if (this.world.oceanVisible !== false)
        {
            Gl.disable(Gl.CULL_FACE);
            Gl.enable(Gl.BLEND);
            Gl.blendFunc(Gl.SRC_ALPHA, Gl.ONE_MINUS_SRC_ALPHA);
            Gl.depthMask(false);

            const Elapsed = this.animateWaves ? (performance.now() - this.startTime) * 0.001 : 1.25;
            Gl.useProgram(this.oceanProg);
            Gl.uniformMatrix4fv(Gl.getUniformLocation(this.oceanProg, 'uViewProj'), false, ViewProj);
            Gl.uniform2f(Gl.getUniformLocation(this.oceanProg, 'uOceanCenter'), this.target[0], this.target[2]);
            Gl.uniform1f(Gl.getUniformLocation(this.oceanProg, 'uOceanSpan'), 5400.0);
            Gl.uniform1f(Gl.getUniformLocation(this.oceanProg, 'uSeaLevel'), this.world.seaLevel ?? 0.0);
            Gl.uniform1f(Gl.getUniformLocation(this.oceanProg, 'uTime'), Elapsed);
            Gl.uniform1f(Gl.getUniformLocation(this.oceanProg, 'uWaveAmp'), this.waveAmplitude);
            Gl.uniform1f(Gl.getUniformLocation(this.oceanProg, 'uExposure'), this.exposure);
            Gl.uniform1i(Gl.getUniformLocation(this.oceanProg, 'uAtmosMode'), this.atmosphereMode);
            Gl.uniform3fv(Gl.getUniformLocation(this.oceanProg, 'uCamPos'), Eye);
            Gl.uniform3fv(Gl.getUniformLocation(this.oceanProg, 'uSunDir'), SunDir);

            Gl.bindVertexArray(this.oceanVao);
            Gl.drawElements(Gl.TRIANGLES, this.oceanIndexCount, Gl.UNSIGNED_INT, 0);

            Gl.depthMask(true);
            Gl.disable(Gl.BLEND);
        }

        Gl.bindVertexArray(null);

        // 5. Crisp 2D Projected Overlay: 3D Spline Curves, Knot Handles, Elevation Stalks & XYZ Up/Down Gizmo!
        this._renderSplineAndGizmoOverlay(Rect.width, Rect.height, Dpr, ViewProj);
    }

    _renderBounds3D(ViewProj, VisibleRegions)
    {
        const Gl = this.gl;
        const Data = [];
        const PushSeg = (Ax, Ay, Az, Bx, By, Bz, R, G, B, A) =>
        {
            Data.push(Ax, Ay, Az, R, G, B, A, Bx, By, Bz, R, G, B, A);
        };

        const Sea = this.world.seaLevel ?? 0.0;
        for (const Region of VisibleRegions)
        {
            const B = Region.bounds;
            const IsPrimary = Region.id === this.activeRegionId;
            const IsSelected = this.selectedRegionIds.has(Region.id);
            const Col = IsPrimary ? [0.93, 0.96, 0.81, 0.62] : (IsSelected ? [0.49, 0.78, 1.0, 0.48] : [0.65, 0.70, 0.76, 0.20]);

            const X0 = B.x, X1 = B.x + B.width;
            const Z0 = B.z, Z1 = B.z + B.depth;
            const Y0 = (Region.runtime?.slabBottom ?? (Sea - 6.5));
            const Y1 = Math.max(Sea + 75.0, (Region.runtime?.maxHeight ?? 140.0) + 18.0);

            PushSeg(X0, Sea + 1.2, Z0, X1, Sea + 1.2, Z0, ...Col);
            PushSeg(X1, Sea + 1.2, Z0, X1, Sea + 1.2, Z1, ...Col);
            PushSeg(X1, Sea + 1.2, Z1, X0, Sea + 1.2, Z1, ...Col);
            PushSeg(X0, Sea + 1.2, Z1, X0, Sea + 1.2, Z0, ...Col);

            const Lx = Math.min(110.0, B.width * 0.11);
            const Lz = Math.min(110.0, B.depth * 0.11);
            const Ly = Math.min(50.0, (Y1 - Y0) * 0.24);
            const Corners = [
                [X0, Y1, Z0, 1, -1, 1],
                [X1, Y1, Z0, -1, -1, 1],
                [X1, Y1, Z1, -1, -1, -1],
                [X0, Y1, Z1, 1, -1, -1]
            ];
            for (const [Cx, Cy, Cz, Sx, Sy, Sz] of Corners)
            {
                PushSeg(Cx, Cy, Cz, Cx + Sx * Lx, Cy, Cz, ...Col);
                PushSeg(Cx, Cy, Cz, Cx, Cy + Sy * Ly, Cz, ...Col);
                PushSeg(Cx, Cy, Cz, Cx, Cy, Cz + Sz * Lz, ...Col);
            }
        }

        if (Data.length === 0) return;
        Gl.enable(Gl.BLEND);
        Gl.blendFunc(Gl.SRC_ALPHA, Gl.ONE_MINUS_SRC_ALPHA);
        Gl.useProgram(this.lineProg);
        Gl.uniformMatrix4fv(Gl.getUniformLocation(this.lineProg, 'uViewProj'), false, ViewProj);
        Gl.bindVertexArray(this.lineVao);
        Gl.bindBuffer(Gl.ARRAY_BUFFER, this.lineVbo);
        Gl.bufferData(Gl.ARRAY_BUFFER, new Float32Array(Data), Gl.STREAM_DRAW);
        Gl.drawArrays(Gl.LINES, 0, Data.length / 7);
        Gl.disable(Gl.BLEND);
    }

    /*======================================================================================================================================
                           2D OVERLAY CANVAS: 3D SPLINE CURVES, ELEVATION STALKS & XYZ UP/DOWN GIZMO
    ======================================================================================================================================*/
    _renderSplineAndGizmoOverlay(Width, Height, Dpr, ViewProj)
    {
        const Ov = this.overlayCanvas;
        const Ctx = this.overlayCtx;
        if (!Ov || !Ctx) return;

        const TargetW = Math.round(Width * Dpr);
        const TargetH = Math.round(Height * Dpr);
        if (Ov.width !== TargetW || Ov.height !== TargetH)
        {
            Ov.width = TargetW;
            Ov.height = TargetH;
        }

        Ctx.save();
        Ctx.setTransform(Dpr, 0, 0, Dpr, 0, 0);
        Ctx.clearRect(0, 0, Width, Height);

        if (!this.showGizmo)
        {
            Ctx.restore();
            return;
        }

        const SplineCtx = this.getActiveSplineContext();
        if (!SplineCtx)
        {
            Ctx.restore();
            return;
        }

        const { region: Reg, layer: ActiveSpline } = SplineCtx;

        // Draw all enabled splines on the active region; highlight ActiveSpline with knots + XYZ Gizmo
        for (const Layer of Reg.stack)
        {
            if (Layer.type !== 'spline' || !Layer.enabled || !Layer.points || Layer.points.length === 0) continue;
            const IsActiveLyr = Layer.id === ActiveSpline.id;

            // 1. Draw smooth 3D Catmull-Rom curve
            if (Layer.points.length >= 2)
            {
                const Poly = SampleCatmullRomPolyline(Layer.points, 8);
                Ctx.beginPath();
                let Started = false;
                for (const Pt of Poly)
                {
                    const Wy = Number.isFinite(Pt[2]) ? Pt[2] : this.sampleTerrainSurfaceY(Reg, Pt[0], Pt[1]);
                    const Scr = ProjectWorldToScreen(ViewProj, [Pt[0], Wy + 2.2, Pt[1]], Width, Height);
                    if (!Scr) { Started = false; continue; }
                    if (!Started) { Ctx.moveTo(Scr[0], Scr[1]); Started = true; }
                    else Ctx.lineTo(Scr[0], Scr[1]);
                }
                Ctx.strokeStyle = IsActiveLyr ? '#eff4cf' : 'rgba(126, 200, 255, 0.55)';
                Ctx.lineWidth = IsActiveLyr ? 2.6 : 1.5;
                Ctx.stroke();
            }

            if (!IsActiveLyr) continue;

            // 2. Draw vertical elevation drop-stalks & numbered 3D Knot handles
            for (let I = 0; I < Layer.points.length; ++I)
            {
                const Knot = Layer.points[I];
                const WPos = this.getKnotWorldPos(Reg, Knot);
                const GroundY = this.sampleTerrainSurfaceY(Reg, Knot[0], Knot[1]);
                const ScrKnot = ProjectWorldToScreen(ViewProj, WPos, Width, Height);
                const ScrGround = ProjectWorldToScreen(ViewProj, [Knot[0], GroundY, Knot[1]], Width, Height);
                if (!ScrKnot) continue;

                // Elevation stalk showing exact vertical separation from ground!
                if (ScrGround && Math.hypot(ScrKnot[0] - ScrGround[0], ScrKnot[1] - ScrGround[1]) > 4)
                {
                    Ctx.save();
                    Ctx.setLineDash([3, 3]);
                    Ctx.strokeStyle = 'rgba(239, 244, 207, 0.45)';
                    Ctx.lineWidth = 1.25;
                    Ctx.beginPath();
                    Ctx.moveTo(ScrGround[0], ScrGround[1]);
                    Ctx.lineTo(ScrKnot[0], ScrKnot[1]);
                    Ctx.stroke();
                    Ctx.beginPath();
                    Ctx.arc(ScrGround[0], ScrGround[1], 3.0, 0, Math.PI * 2);
                    Ctx.fillStyle = 'rgba(239, 244, 207, 0.55)';
                    Ctx.fill();
                    Ctx.restore();
                }

                const IsSelKnot = I === this.selectedKnotIndex;
                const IsHoverKnot = I === this.hoverKnotIndex;
                const R = IsSelKnot ? 7.5 : (IsHoverKnot ? 6.8 : 5.6);

                Ctx.beginPath();
                Ctx.arc(ScrKnot[0], ScrKnot[1], R, 0, Math.PI * 2);
                Ctx.fillStyle = IsSelKnot ? '#eff4cf' : '#16181c';
                Ctx.fill();
                Ctx.lineWidth = 2.0;
                Ctx.strokeStyle = IsSelKnot ? '#14160c' : '#eff4cf';
                Ctx.stroke();

                // Knot index label
                Ctx.font = '600 9.5px "DM Sans", sans-serif';
                Ctx.fillStyle = IsSelKnot ? '#14160c' : '#eff4cf';
                Ctx.textAlign = 'center';
                Ctx.textBaseline = 'middle';
                Ctx.fillText(String(I + 1), ScrKnot[0], ScrKnot[1] + 0.5);
            }
        }

        // 3. Draw 3D XYZ Translation Gizmo (with prominent Green Up/Down Y Arrow!) on Selected Knot
        const Giz = this._computeGizmoGeometry(SplineCtx, Width, Height);
        if (Giz)
        {
            const ActiveAxis = this.dragState?.kind === 'gizmo-drag' ? this.dragState.axis : this.hoverGizmoAxis;

            const DrawArrow = (From, To, Color, Highlighted, Label) =>
            {
                if (!From || !To) return;
                const Dx = To[0] - From[0], Dy = To[1] - From[1];
                const Len = Math.hypot(Dx, Dy);
                if (Len < 4) return;
                const Ux = Dx / Len, Uy = Dy / Len;
                const Px = -Uy, Py = Ux;

                const Col = Highlighted ? '#fff7b2' : Color;
                Ctx.strokeStyle = Col;
                Ctx.fillStyle = Col;
                Ctx.lineWidth = Highlighted ? 3.6 : 2.6;

                Ctx.beginPath();
                Ctx.moveTo(From[0], From[1]);
                Ctx.lineTo(To[0] - Ux * 10, To[1] - Uy * 10);
                Ctx.stroke();

                // Cone arrowhead
                Ctx.beginPath();
                Ctx.moveTo(To[0], To[1]);
                Ctx.lineTo(To[0] - Ux * 12 + Px * 5.2, To[1] - Uy * 12 + Py * 5.2);
                Ctx.lineTo(To[0] - Ux * 12 - Px * 5.2, To[1] - Uy * 12 - Py * 5.2);
                Ctx.closePath();
                Ctx.fill();

                if (Label)
                {
                    Ctx.font = '700 10.5px "DM Sans", sans-serif';
                    Ctx.fillStyle = Col;
                    Ctx.textAlign = 'center';
                    Ctx.fillText(Label, To[0] + Ux * 11, To[1] + Uy * 11);
                }
            };

            // XZ Plane Handle
            if (Giz.pxz && Giz.px && Giz.pz)
            {
                const Hx = Giz.p0[0] + (Giz.px[0] - Giz.p0[0]) * 0.28;
                const Hy = Giz.p0[1] + (Giz.px[1] - Giz.p0[1]) * 0.28;
                const Zx = Giz.p0[0] + (Giz.pz[0] - Giz.p0[0]) * 0.28;
                const Zy = Giz.p0[1] + (Giz.pz[1] - Giz.p0[1]) * 0.28;
                Ctx.beginPath();
                Ctx.moveTo(Giz.p0[0], Giz.p0[1]);
                Ctx.lineTo(Hx, Hy);
                Ctx.lineTo(Giz.pxz[0], Giz.pxz[1]);
                Ctx.lineTo(Zx, Zy);
                Ctx.closePath();
                Ctx.fillStyle = ActiveAxis === 'xz' ? 'rgba(255, 247, 178, 0.45)' : 'rgba(232, 194, 122, 0.22)';
                Ctx.strokeStyle = ActiveAxis === 'xz' ? '#fff7b2' : 'rgba(232, 194, 122, 0.78)';
                Ctx.lineWidth = 1.4;
                Ctx.fill();
                Ctx.stroke();
            }

            // Red X Axis, Blue Z Axis, and Double-Ended Green Y (UP / DOWN) Axis!
            DrawArrow(Giz.p0, Giz.px, '#ff6464', ActiveAxis === 'x', 'X');
            DrawArrow(Giz.p0, Giz.pz, '#5cb4ff', ActiveAxis === 'z', 'Z');
            if (Giz.pyDn) DrawArrow(Giz.p0, Giz.pyDn, '#3ec46d', ActiveAxis === 'y', '');
            DrawArrow(Giz.p0, Giz.pyUp, '#49e67c', ActiveAxis === 'y', 'Y ↕');

            // Live Elevation Tag next to Selected Knot
            const KnotElev = Giz.origin[1];
            const ElevText = `Knot #${this.selectedKnotIndex + 1} · Y ${(KnotElev >= 0 ? '+' : '') + KnotElev.toFixed(1)} m`;
            Ctx.font = '500 11px "DM Sans", sans-serif';
            const Tw = Ctx.measureText(ElevText).width;
            const Bx = Giz.p0[0] + 14, By = Giz.p0[1] - 24;
            Ctx.fillStyle = 'rgba(14, 15, 17, 0.88)';
            Ctx.strokeStyle = ActiveAxis === 'y' ? '#49e67c' : 'rgba(239, 244, 207, 0.35)';
            Ctx.lineWidth = 1;
            Ctx.beginPath();
            Ctx.roundRect(Bx, By, Tw + 14, 21, 999);
            Ctx.fill();
            Ctx.stroke();
            Ctx.fillStyle = '#eff4cf';
            Ctx.textAlign = 'left';
            Ctx.textBaseline = 'middle';
            Ctx.fillText(ElevText, Bx + 7, By + 10.5);
        }

        Ctx.restore();
    }
}
