/*==========================================================================================================================================
                                                     FRACTURELABVIEWPORT.JS
==========================================================================================================================================*/
// Dual-Mode WebGL2 3D Viewport for the Frontier Pure-SDF Brittle Fracture & Thermal Cracking Lab:
// - Mode 1 ('raymarch'): Real-time 60 FPS GPU Pure-SDF Raymarcher reading exact compiled polyhedral blocks &
//   inverse rigid pullback transforms from RGBA32F data textures (`uBlockTex`, `uPlaneTex`).
// - Mode 2 ('qef-mesh'): Ported Sparse 4^3-Brick QEF Dual-Contoured Manifold Triangle Mesh renderer with
//   Volumetric 3D SDF AO, Concave Cavity, Chipped Edge Wear, Crease-Split Normals & Wireframe Overlay.

import { clamp, DEG2RAD } from './Core/SpatialNoise3D.js';
import { packFracturedFormationGpuBuffers } from './PureSdfFractureEngine.js';

/*==========================================================================================================================================
                                      1. WEBGL2 SHADERS — 60 FPS GPU PURE-SDF RAYMARCHER
==========================================================================================================================================*/

const RAYMARCH_VS = `#version 300 es
precision highp float;
const vec2 POS[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
out vec2 vNdc;
void main() {
    vNdc = POS[gl_VertexID];
    gl_Position = vec4(vNdc, 0.0, 1.0);
}
`;

const RAYMARCH_FS = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

in vec2 vNdc;
out vec4 fragColor;

uniform sampler2D uBlockTex;
uniform sampler2D uPlaneTex;
uniform int uNumBlocks;
uniform int uCrackSalt;

uniform vec3 uCamPos;
uniform vec3 uCamFwd;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform float uTanHalfFov;
uniform float uAspect;

uniform vec3 uBoundsMin;
uniform vec3 uBoundsMax;
uniform vec3 uSunDir;
uniform float uExposure;
uniform int uLensMode;

// Fracture & Thermal SDF Parameters
uniform float uScale;
uniform float uJigsawRoughness;
uniform float uPlumoseStrength;
uniform float uThermalNorm;
uniform float uDirectionalBias;
uniform float uSkinDepth;
uniform float uCrazing90Strength;
uniform float uCrazingSpacing;
uniform float uVoronoiStrength;
uniform float uExfoliationStrength;
uniform int uCutawayEnabled;
uniform int uCutawayAxis;
uniform float uCutawayOffset;

// Lithology Theme Colors
uniform vec3 uColRind;
uniform vec3 uColFresh;
uniform vec3 uColIron;
uniform vec3 uColLichen;

float hash31(int ix, int iy, int iz, int salt) {
    uint n = uint(ix) * 1597334677u ^ uint(iy) * 3812015801u ^ uint(iz) * 2798796415u ^ uint(salt) * 198491317u;
    n = (n ^ (n >> 15u)) * 2246822519u;
    n = (n ^ (n >> 13u)) * 3266489917u;
    n = n ^ (n >> 16u);
    return float(n) * 2.3283064365386963e-10;
}

uint hash3u(int ix, int iy, int iz, int salt) {
    uint h = uint(ix) * 374761393u + uint(iy) * 668265263u + uint(iz) * 2147483647u + uint(salt) * 1597334677u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return h ^ (h >> 16u);
}

float scalarField3D(vec3 p, int salt) {
    vec3 fl = floor(p);
    ivec3 ip = ivec3(fl);
    vec3 f = p - fl;
    vec3 u = f * f * (3.0 - 2.0 * f);

    float n000 = hash31(ip.x,     ip.y,     ip.z,     salt);
    float n100 = hash31(ip.x + 1, ip.y,     ip.z,     salt);
    float n010 = hash31(ip.x,     ip.y + 1, ip.z,     salt);
    float n110 = hash31(ip.x + 1, ip.y + 1, ip.z,     salt);
    float n001 = hash31(ip.x,     ip.y,     ip.z + 1, salt);
    float n101 = hash31(ip.x + 1, ip.y,     ip.z + 1, salt);
    float n011 = hash31(ip.x,     ip.y + 1, ip.z + 1, salt);
    float n111 = hash31(ip.x + 1, ip.y + 1, ip.z + 1, salt);

    float nx00 = mix(n000, n100, u.x);
    float nx10 = mix(n010, n110, u.x);
    float nx01 = mix(n001, n101, u.x);
    float nx11 = mix(n011, n111, u.x);

    float nxy0 = mix(nx00, nx10, u.y);
    float nxy1 = mix(nx01, nx11, u.y);
    return mix(nxy0, nxy1, u.z) * 2.0 - 1.0;
}

float fbm3D(vec3 p, int salt) {
    float sum = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    float norm = 0.0;
    for (int i = 0; i < 3; i++) {
        sum += amp * scalarField3D(p * freq, salt + i * 101);
        norm += amp;
        freq *= 2.03;
        amp *= 0.5;
    }
    return sum / norm;
}

// Returns vec2(wallDist, cellRand)
vec2 evalPlanarCleavage3D(vec3 p, float jitter, int salt) {
    vec3 fl = floor(p);
    ivec3 ip = ivec3(fl);
    float min1 = 1e9;
    float min2 = 1e9;
    vec3 c1 = vec3(0.0);
    vec3 c2 = vec3(0.0);
    uint id1 = 0u;

    for (int dz = -1; dz <= 1; dz++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                ivec3 c = ip + ivec3(dx, dy, dz);
                vec3 j = (vec3(
                    hash31(c.x, c.y, c.z, salt + 11),
                    hash31(c.x, c.y, c.z, salt + 29),
                    hash31(c.x, c.y, c.z, salt + 47)
                ) - 0.5) * jitter;
                vec3 sp = vec3(c) + 0.5 + j;
                vec3 r = sp - p;
                float d2 = dot(r, r);
                if (d2 < min1) {
                    min2 = min1; c2 = c1;
                    min1 = d2;   c1 = sp;
                    id1 = hash3u(c.x, c.y, c.z, salt);
                } else if (d2 < min2) {
                    min2 = d2;   c2 = sp;
                }
            }
        }
    }
    float bLen = length(c2 - c1);
    float wallDist = bLen > 1e-5 ? (min2 - min1) / (2.0 * bLen) : 1.0;
    float cellRand = float((id1 >> 8u) & 0xffffu) / 32767.5 - 1.0;
    return vec2(wallDist, cellRand);
}

// Hierarchical 90-Degree Orthogonal T-Junction Thermal Crazing!
float evalHierarchical90Crazing3D(float u, float v, float w, float spacing, int salt) {
    float L1 = max(0.08, spacing);
    float L2 = L1 * 0.92;
    float warpU = L1 * 0.14 * scalarField3D(vec3(u, v, w) / (L1 * 1.7), salt + 3);
    float warpV = L2 * 0.13 * scalarField3D(vec3(u, v, w) / (L2 * 1.6), salt + 19);

    float uCoord = (u + warpU) / L1;
    float k1f = floor(uCoord);
    int k1 = int(k1f);
    float fu = uCoord - k1f;
    float tier1Dist = min(fu, 1.0 - fu) * L1;

    float slabPhase = hash31(k1, 17, 53, salt + 41);
    float slabScale = 0.82 + 0.36 * hash31(k1, 89, 13, salt + 67);
    float effL2 = L2 * slabScale;

    float vCoord = (v + warpV) / effL2 + slabPhase;
    float k2f = floor(vCoord);
    int k2 = int(k2f);
    float fv = vCoord - k2f;
    float tier2Dist = min(fv, 1.0 - fv) * effL2;

    float tileRand = hash31(k1, k2, 7, salt + 97);
    if (tileRand > 0.42) {
        float splitPos = 0.36 + 0.28 * hash31(k1, k2, 31, salt + 131);
        float tier3Dist = abs(fu - splitPos) * L1 * 1.28;
        tier2Dist = min(tier2Dist, tier3Dist);
    }
    return min(tier1Dist, tier2Dist * 1.18);
}

float chamferSub(float base, float cut, float r) {
    float b = -cut;
    float m = max(base, b);
    if (r <= 1e-6) return m;
    return max(m, (base + b + r) * 0.70710678);
}

struct SdfHitInfo {
    float d;
    float dMacro;
    float freshMask;
    float thermalStress;
    float strataWeight;
    vec3 p0;
    bool isCutaway;
};

SdfHitInfo evalFormationFull(vec3 p, bool fineDetail) {
    SdfHitInfo res;
    res.dMacro = 1e9;
    res.freshMask = 0.0;
    res.thermalStress = uThermalNorm;
    res.strataWeight = 0.25;
    res.p0 = p;
    res.isCutaway = false;

    vec3 bestN = vec3(0.0, 1.0, 0.0);
    bool noCracks = false;

    for (int i = 0; i < 64; i++) {
        if (i >= uNumBlocks) break;
        vec4 sph = texelFetch(uBlockTex, ivec2(0, i), 0);
        float lowerB = length(p - sph.xyz) - sph.w - uScale * 0.04;
        if (lowerB > res.dMacro) continue;

        vec4 meta = texelFetch(uBlockTex, ivec2(1, i), 0);
        int pStart = int(meta.x + 0.5);
        int pCount = int(meta.y + 0.5);
        float bevel = meta.z;
        bool blkNoCracks = meta.w > 0.5;

        vec4 r0 = texelFetch(uBlockTex, ivec2(2, i), 0);
        vec4 r1 = texelFetch(uBlockTex, ivec2(3, i), 0);
        vec4 r2 = texelFetch(uBlockTex, ivec2(4, i), 0);
        vec4 m5 = texelFetch(uBlockTex, ivec2(5, i), 0);

        vec3 p0 = vec3(
            dot(r0.xyz, p) + r0.w,
            dot(r1.xyz, p) + r1.w,
            dot(r2.xyz, p) + r2.w
        );

        float jigsawDisp = 0.0;
        float extRelief = 0.0;
        if (fineDetail) {
            float jigAmp = uScale * 0.020 * uJigsawRoughness;
            float plumAmp = uScale * 0.0065 * uPlumoseStrength;
            if (jigAmp > 1e-5 || plumAmp > 1e-5) {
                float f1 = 1.65 / uScale;
                float w1 = fbm3D(vec3(p0.x * f1, p0.y * f1 * 0.9, p0.z * f1), uCrackSalt + 5);
                float rNuc = length(vec3(p0.x, p0.y - uScale * 0.08, p0.z));
                float plum = sin(rNuc * (26.0 / uScale) + w1 * 3.4) * 0.5 +
                             scalarField3D(vec3(p0.x * (9.5 / uScale), p0.y * (5.2 / uScale), p0.z * (9.5 / uScale)), uCrackSalt + 71) * 0.5;
                jigsawDisp = jigAmp * w1 + plumAmp * plum;
            }
            float cragF = 2.4 / uScale;
            float crag = uScale * 0.010 * scalarField3D(p0 * cragF, uCrackSalt + 113);
            float strataAmp = uScale * 0.0052 * m5.y;
            float bedY = p0.y * (5.4 / uScale) + 0.44 * scalarField3D(vec3(p0.x * (1.8 / uScale), p0.y * (2.6 / uScale), p0.z * (1.8 / uScale)), uCrackSalt + 149);
            float strataLedge = strataAmp * (sin(bedY * 6.2831853) * 0.72 + 0.28 * sin(bedY * 12.5663706 + 0.8));
            extRelief = crag + strataLedge;
        }

        float d1 = -1e9;
        float d2 = -1e9;
        vec3 n1 = vec3(0.0, 1.0, 0.0);
        vec3 n2 = vec3(0.0, 1.0, 0.0);
        float dMaxExt = -1e9;
        float dMaxFrac = -1e9;

        for (int j = 0; j < 32; j++) {
            if (j >= pCount) break;
            vec4 pl = texelFetch(uPlaneTex, ivec2(0, pStart + j), 0);
            float pType = texelFetch(uPlaneTex, ivec2(1, pStart + j), 0).x;
            float pd = dot(pl.xyz, p) - pl.w;

            if (fineDetail) {
                if (abs(pType) > 0.5) {
                    pd += pType * jigsawDisp;
                    dMaxFrac = max(dMaxFrac, pd);
                } else {
                    if (pl.y > -0.85) pd += extRelief;
                    dMaxExt = max(dMaxExt, pd);
                }
            } else {
                if (abs(pType) > 0.5) dMaxFrac = max(dMaxFrac, pd);
                else dMaxExt = max(dMaxExt, pd);
            }

            if (pd > d1) {
                d2 = d1; n2 = n1;
                d1 = pd; n1 = pl.xyz;
            } else if (pd > d2) {
                d2 = pd; n2 = pl.xyz;
            }
        }

        float dBlk = d1;
        vec3 bN = n1;
        if (bevel > 1e-5 && d2 > -bevel * 2.0) {
            float ch = (d1 + d2 + bevel) * 0.70710678;
            if (ch > dBlk) {
                dBlk = ch;
                bN = normalize(n1 + n2);
            }
        }

        if (dBlk < res.dMacro) {
            res.dMacro = dBlk;
            res.p0 = p0;
            bestN = bN;
            noCracks = blkNoCracks;
            res.strataWeight = m5.y;
            float tr = max(0.015, bevel * 1.35);
            res.freshMask = smoothstep(-tr, tr, dMaxFrac - dMaxExt);
        }
    }

    float d = res.dMacro;
    float crackFresh = 0.0;

    float maxFineDepth = max(uSkinDepth * 2.2, uExfoliationStrength > 0.02 ? uScale * 0.14 : 0.12);
    if (fineDetail && !noCracks && res.dMacro < uScale * 0.04 && res.dMacro > -maxFineDepth &&
        (uCrazing90Strength > 0.01 || uVoronoiStrength > 0.01 || uExfoliationStrength > 0.01)) {

        float ndots = max(0.0, dot(bestN, uSunDir));
        float sunGate = clamp(pow(ndots, 1.25) + 0.10, 0.0, 1.0);
        float insol = mix(1.0, sunGate, uDirectionalBias) * uThermalNorm;
        res.thermalStress = insol;

        vec3 p0 = res.p0;
        float curMacro = res.dMacro;

        // (A) Concentric Onion-Skin Exfoliation Sheeting (evaluated FIRST so peeled plates expose smooth fresh granite!)
        if (uExfoliationStrength > 0.04 && bestN.y > -0.18) {
            float sheetSpacing = uScale * 0.036;
            float plateThick = uScale * 0.015;
            float interiorDepth = -curMacro;
            if (interiorDepth > -sheetSpacing * 0.25 && interiorDepth < sheetSpacing * 3.1) {
                float warpS = sheetSpacing * 0.14 * scalarField3D(p0 * 0.9, uCrackSalt + 409);
                float sCoord = (interiorDepth + warpS) / sheetSpacing;
                float shellFl = floor(sCoord);
                int shellIdx = int(shellFl);

                if (uExfoliationStrength > 0.32 && shellIdx == 0 && interiorDepth > 0.0 && interiorDepth < plateThick * 1.15) {
                    vec2 relClv = evalPlanarCleavage3D(p0 * 0.62, 0.76, uCrackSalt + 503);
                    float relWall = relClv.x / 0.62;
                    if (relClv.y > (1.0 - (uExfoliationStrength - 0.22) * 0.85)) {
                        float dShellDepth = interiorDepth - plateThick;
                        float dPlateInner = 0.045 - relWall;
                        float windowCutter = max(dShellDepth, dPlateInner);
                        float peeled = max(d, -windowCutter);
                        if (peeled > d) {
                            d = peeled;
                            curMacro = peeled;
                            crackFresh = max(crackFresh, 0.82);
                        }
                    }
                }
                if (interiorDepth > sheetSpacing * 0.32) {
                    float fracS = sCoord - shellFl;
                    float seamDist = min(fracS, 1.0 - fracS) * sheetSpacing;
                    float seamHalfAp = sheetSpacing * 0.11 * uExfoliationStrength * smoothstep(-0.18, 0.35, bestN.y);
                    if (seamHalfAp > 0.0015) d = max(d, -(seamDist - seamHalfAp));
                }
            }
        }

        float tipDepth = max(0.06, uSkinDepth) * clamp(0.30 + 0.70 * insol, 0.15, 1.35);
        float vTaper = max(0.0, 1.0 + min(0.0, curMacro) / tipDepth);

        // (B) 90-Degree T-Junction Thermal Crazing
        if (uCrazing90Strength > 0.01 && insol > 0.02 && curMacro > -(tipDepth + uScale * 0.08)) {
            vec3 an = abs(bestN);
            vec3 uvw = (an.y >= an.x && an.y >= an.z) ? vec3(p0.x, p0.z, p0.y)
                     : (an.x >= an.z)                ? vec3(p0.z, p0.y, p0.x)
                                                     : vec3(p0.x, p0.y, p0.z);
            float cDist = evalHierarchical90Crazing3D(uvw.x, uvw.y, uvw.z, uCrazingSpacing, uCrackSalt + 211);
            float surfHalfAp90 = uCrazingSpacing * 0.072 * uCrazing90Strength * insol;
            if (surfHalfAp90 > 0.0015) {
                float halfAp90 = surfHalfAp90 * vTaper;
                float cut90 = max(cDist - halfAp90, (-curMacro) - tipDepth);
                float prevD = d;
                d = chamferSub(d, cut90, halfAp90 * 0.45);
                if (d > prevD + 1e-4 && vTaper > 0.0) crackFresh = max(crackFresh, smoothstep(surfHalfAp90 * 1.8, 0.0, cDist));
            }
        }

        // (C) 3D Polygonal Voronoi Stress Cleavage
        if (uVoronoiStrength > 0.01 && curMacro > -(tipDepth + uScale * 0.08)) {
            float vorScale = 1.55 / uScale * 4.2;
            vec2 clv = evalPlanarCleavage3D(vec3(p0.x * vorScale, p0.y * vorScale * 0.85, p0.z * vorScale), 0.74, uCrackSalt + 307);
            float worldWall = clv.x / vorScale;
            float surfHalfApVor = (0.009 + 0.024 * uVoronoiStrength) * (0.45 + 0.55 * insol) * (uScale / 7.0);
            if (surfHalfApVor > 0.0015) {
                float halfApVor = surfHalfApVor * vTaper;
                float cutVor = max(worldWall - halfApVor, (-curMacro) - tipDepth);
                float prevD = d;
                d = chamferSub(d, cutVor, halfApVor * 0.40);
                if (d > prevD + 1e-4 && vTaper > 0.0) crackFresh = max(crackFresh, smoothstep(surfHalfApVor * 1.8, 0.0, worldWall));
            }
        }
    }

    if (uCutawayEnabled != 0) {
        float axisVal = (uCutawayAxis == 0) ? p.x : p.z;
        float slicePlane = axisVal - uCutawayOffset;
        if (slicePlane > d) {
            d = slicePlane;
            res.isCutaway = true;
        }
    }

    res.d = d;
    res.freshMask = clamp(max(res.freshMask, crackFresh), 0.0, 1.0);
    return res;
}

float mapDist(vec3 p) {
    // Fast hybrid distance: skip fine procedural cracks when far from macro surface!
    SdfHitInfo macro = evalFormationFull(p, false);
    if (macro.d > 0.22) return macro.d - 0.14;
    return evalFormationFull(p, true).d;
}

vec3 calcNormal(vec3 p) {
    const float e = 0.0032;
    vec2 k = vec2(1.0, -1.0);
    return normalize(
        k.xyy * mapDist(p + k.xyy * e) +
        k.yyx * mapDist(p + k.yyx * e) +
        k.yxy * mapDist(p + k.yxy * e) +
        k.xxx * mapDist(p + k.xxx * e)
    );
}

float calcSdfAO(vec3 p, vec3 n) {
    float occ = 0.0;
    float sca = 1.0;
    vec3 upBias = normalize(n * 0.72 + vec3(0.0, 0.36, 0.0));
    for (int i = 0; i < 4; i++) {
        float h = 0.045 + 0.16 * float(i * i);
        float d1 = mapDist(p + n * h);
        float d2 = mapDist(p + upBias * h);
        float d = 0.58 * d1 + 0.42 * d2;
        occ += max(0.0, h - d) * sca;
        sca *= 0.62;
    }
    return clamp(1.0 - 1.55 * occ, 0.08, 1.0);
}

float calcSoftShadow(vec3 ro, vec3 rd) {
    float res = 1.0;
    float t = 0.04;
    for (int i = 0; i < 28; i++) {
        vec3 p = ro + rd * t;
        if (p.y > uBoundsMax.y + 0.5) break;
        float h = mapDist(p);
        if (h < 0.0015) return 0.06;
        res = min(res, 10.0 * h / t);
        t += clamp(h, 0.03, 0.35);
        if (t > 12.0) break;
    }
    return clamp(res, 0.06, 1.0);
}

vec2 intersectAABB(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax) {
    vec3 invD = 1.0 / rd;
    vec3 t0 = (bmin - ro) * invD;
    vec3 t1 = (bmax - ro) * invD;
    vec3 tsmaller = min(t0, t1);
    vec3 tbigger = max(t0, t1);
    float tmin = max(max(tsmaller.x, tsmaller.y), max(tsmaller.z, 0.0));
    float tmax = min(min(tbigger.x, tbigger.y), tbigger.z);
    return vec2(tmin, tmax);
}

vec3 acesTonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 heatmapRamp(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c0 = vec3(0.10, 0.16, 0.34);
    vec3 c1 = vec3(0.18, 0.52, 0.62);
    vec3 c2 = vec3(0.92, 0.68, 0.24);
    vec3 c3 = vec3(0.95, 0.26, 0.16);
    if (t < 0.35) return mix(c0, c1, t / 0.35);
    if (t < 0.72) return mix(c1, c2, (t - 0.35) / 0.37);
    return mix(c2, c3, (t - 0.72) / 0.28);
}

void main() {
    vec2 uv = vNdc;
    vec3 rd = normalize(uCamFwd + uv.x * uAspect * uTanHalfFov * uCamRight + uv.y * uTanHalfFov * uCamUp);
    vec3 ro = uCamPos;

    // Calibrated obsidian geological studio sky gradient
    float skyT = clamp(rd.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyCol = mix(vec3(0.055, 0.058, 0.068), vec3(0.115, 0.125, 0.148), pow(skyT, 0.75));
    float sunGlow = pow(max(0.0, dot(rd, uSunDir)), 24.0);
    skyCol += vec3(0.22, 0.18, 0.12) * sunGlow * 0.35;

    // Ray-box test against formation AABB
    vec3 padMin = uBoundsMin - vec3(0.35, 0.05, 0.35);
    vec3 padMax = uBoundsMax + vec3(0.35, 0.35, 0.35);
    vec2 boxHit = intersectAABB(ro, rd, padMin, padMax);

    float tRock = -1.0;
    if (boxHit.y >= boxHit.x) {
        float t = max(0.02, boxHit.x);
        for (int s = 0; s < 112; s++) {
            if (t > boxHit.y) break;
            vec3 p = ro + rd * t;
            float d = mapDist(p);
            if (d < 0.0022) {
                tRock = t;
                break;
            }
            t += d * 0.88;
        }
    }

    // Floor turntable intersection (y = 0)
    float tFloor = -1.0;
    if (rd.y < -1e-4) {
        float tf = -ro.y / rd.y;
        if (tf > 0.0 && (tRock < 0.0 || tf < tRock)) {
            vec3 fp = ro + rd * tf;
            if (length(fp.xz) < uScale * 1.65) {
                tFloor = tf;
            }
        }
    }

    vec3 col = skyCol;

    if (tFloor > 0.0) {
        vec3 fp = ro + rd * tFloor;
        float rDist = length(fp.xz);
        float discFade = 1.0 - smoothstep(uScale * 0.85, uScale * 1.62, rDist);

        // Metric 1-meter geological scale grid + concentric range rings
        vec2 g1 = abs(fract(fp.xz) - 0.5);
        float line1 = 1.0 - smoothstep(0.008, 0.024, min(g1.x, g1.y));
        float ring = 1.0 - smoothstep(0.010, 0.032, abs(fract(rDist / 2.0) - 0.5) * 2.0);

        float sh = calcSoftShadow(fp + vec3(0.0, 0.02, 0.0), uSunDir);
        float contactOcc = clamp(mapDist(fp + vec3(0.0, 0.18, 0.0)) / 0.32, 0.18, 1.0);

        vec3 floorBase = vec3(0.072, 0.075, 0.084) * (0.45 + 0.55 * sh) * contactOcc;
        floorBase += vec3(0.16, 0.15, 0.12) * (line1 * 0.22 + ring * 0.16) * discFade;
        col = mix(skyCol, floorBase, discFade);
    } else if (tRock > 0.0) {
        vec3 p = ro + rd * tRock;
        vec3 n = calcNormal(p);
        SdfHitInfo info = evalFormationFull(p, true);
        float ao = calcSdfAO(p, n);

        // Estimate local cavity/edge curvature from small normal neighborhood
        float dProbeOut = mapDist(p + n * 0.05);
        float cav = clamp((0.05 - dProbeOut) * 18.0, 0.0, 1.0);
        float edg = clamp((dProbeOut - 0.048) * 26.0, 0.0, 1.0);

        vec3 albedo = uColRind;
        if (info.isCutaway) {
            float depth = max(0.0, -info.dMacro);
            float skinZone = exp(-depth / max(0.06, uSkinDepth));
            float ring = 0.5 + 0.5 * cos((depth / 0.18) * 6.2831853);
            float isoLine = smoothstep(0.91, 0.99, ring);
            albedo = mix(uColFresh * 0.82, uColIron * 1.12, skinZone * 0.58);
            albedo = mix(albedo, vec3(0.96, 0.87, 0.62), isoLine * 0.52);
        } else {
            vec3 p0 = info.p0;
            float grain = scalarField3D(p0 * 6.5, uCrackSalt + 601);
            float bedBand = sin((p0.y * (5.4 / uScale) + 0.44 * scalarField3D(p0 * 0.6, uCrackSalt + 619)) * 6.2831853);
            float strataMod = 1.0 + bedBand * 0.14 * info.strataWeight + grain * 0.07;

            albedo = mix(uColRind, uColFresh, info.freshMask) * strataMod;
            float ironSeep = clamp(cav * 0.72 + info.freshMask * 0.28 * (0.5 + 0.5 * scalarField3D(vec3(p0.x * 2.1, p0.y * 0.8, p0.z * 2.1), uCrackSalt + 643)), 0.0, 1.0);
            albedo = mix(albedo, uColIron, ironSeep * 0.34);

            float upLichen = (1.0 - info.freshMask) * smoothstep(0.55, 0.92, n.y) * clamp(0.5 + 0.5 * grain, 0.0, 1.0);
            albedo = mix(albedo, uColLichen, upLichen * 0.30);
            albedo *= (1.0 + edg * 0.18 - cav * 0.18);
        }

        // Diagnostic Lens Overrides
        if (uLensMode == 1) {
            // Fresh Split Interior vs Weathered Rind
            albedo = mix(vec3(0.18, 0.24, 0.34), vec3(0.95, 0.64, 0.26), info.freshMask);
        } else if (uLensMode == 2) {
            // Diurnal ΔT Thermal Stress & Skin-Depth Heatmap
            albedo = heatmapRamp(info.thermalStress * 0.85 + info.freshMask * 0.15);
        } else if (uLensMode == 3) {
            // Volumetric 3D SDF AO & Cavity Clay
            albedo = vec3(0.84, 0.82, 0.78) * (1.0 - cav * 0.35);
        } else if (uLensMode == 4) {
            // Normal & 90° Dihedral Inspection
            vec3 nCol = n * 0.5 + 0.5;
            fragColor = vec4(pow(nCol * (0.55 + 0.45 * ao), vec3(1.0 / 2.2)), 1.0);
            return;
        }

        float sh = max(0.18, calcSoftShadow(p + n * 0.015, uSunDir));
        float ndotl = max(0.0, dot(n, uSunDir));
        vec3 fillDir = normalize(vec3(-uSunDir.x, 0.40, -uSunDir.z));
        float fill = max(0.0, dot(n, fillDir)) * 0.34;
        float hemi = 0.34 + 0.22 * (n.y * 0.5 + 0.5);

        vec3 halfV = normalize(uSunDir - rd);
        float spec = pow(max(0.0, dot(n, halfV)), 28.0) * (0.06 + 0.08 * info.freshMask) * sh;

        vec3 lit = albedo * (ndotl * sh * 1.48 + (hemi + fill) * ao) + vec3(1.0, 0.95, 0.86) * spec;
        col = lit;
    }

    col = acesTonemap(col * uExposure);
    col = pow(col, vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
`;

/*==========================================================================================================================================
                               2. WEBGL2 SHADERS — QEF DUAL-CONTOURED MESH + WIREFRAME OVERLAY
==========================================================================================================================================*/

const MESH_VS = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNorm;
layout(location = 2) in vec3 aAlbedo;
layout(location = 3) in vec4 aMasks;

uniform mat4 uViewProj;

out vec3 vPos;
out vec3 vNorm;
out vec3 vAlbedo;
out vec4 vMasks;

void main() {
    vPos = aPos;
    vNorm = aNorm;
    vAlbedo = aAlbedo;
    vMasks = aMasks;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
`;

const MESH_FS = `#version 300 es
precision highp float;

in vec3 vPos;
in vec3 vNorm;
in vec3 vAlbedo;
in vec4 vMasks; // x=AO, y=Cavity, z=Edge, w=FreshMask

uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform float uExposure;
uniform int uLensMode;
uniform int uIsWirePass;

out vec4 fragColor;

vec3 acesTonemap(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 heatmapRamp(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c0 = vec3(0.10, 0.16, 0.34);
    vec3 c1 = vec3(0.18, 0.52, 0.62);
    vec3 c2 = vec3(0.92, 0.68, 0.24);
    vec3 c3 = vec3(0.95, 0.26, 0.16);
    if (t < 0.35) return mix(c0, c1, t / 0.35);
    if (t < 0.72) return mix(c1, c2, (t - 0.35) / 0.37);
    return mix(c2, c3, (t - 0.72) / 0.28);
}

void main() {
    if (uIsWirePass != 0) {
        fragColor = vec4(0.94, 0.96, 0.81, 0.42);
        return;
    }

    vec3 n = normalize(vNorm);
    float ao = vMasks.x;
    float cav = vMasks.y;
    float fresh = vMasks.w;

    if (uLensMode == 4) {
        vec3 nCol = (n * 0.5 + 0.5) * (0.55 + 0.45 * ao);
        fragColor = vec4(pow(nCol, vec3(1.0 / 2.2)), 1.0);
        return;
    }

    vec3 albedo = vAlbedo;
    if (uLensMode == 1) {
        albedo = mix(vec3(0.18, 0.24, 0.34), vec3(0.95, 0.64, 0.26), fresh);
    } else if (uLensMode == 2) {
        float sunHit = max(0.0, dot(n, uSunDir));
        albedo = heatmapRamp(clamp(pow(sunHit, 1.2) * 0.8 + fresh * 0.2, 0.0, 1.0));
    } else if (uLensMode == 3) {
        albedo = vec3(0.84, 0.82, 0.78) * (1.0 - cav * 0.35);
    }

    float ndotl = max(0.0, dot(n, uSunDir));
    vec3 fillDir = normalize(vec3(-uSunDir.x, 0.40, -uSunDir.z));
    float fill = max(0.0, dot(n, fillDir)) * 0.34;
    float hemi = 0.34 + 0.22 * (n.y * 0.5 + 0.5);

    vec3 viewDir = normalize(uCamPos - vPos);
    vec3 halfV = normalize(uSunDir + viewDir);
    float spec = pow(max(0.0, dot(n, halfV)), 28.0) * (0.06 + 0.08 * fresh);

    vec3 lit = albedo * (ndotl * (0.35 + 0.65 * ao) * 1.48 + (hemi + fill) * ao) + vec3(1.0, 0.95, 0.86) * spec * ao;
    vec3 col = pow(acesTonemap(lit * uExposure), vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
`;

/*==========================================================================================================================================
                                          3. MATRIX HELPERS & VIEWPORT CONTROLLER
==========================================================================================================================================*/

function compileShader(gl, type, src)
{
    const s = gl.createShader(type);
    gl.shaderSource(s, src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS))
    {
        const err = gl.getShaderInfoLog(s);
        gl.deleteShader(s);
        throw new Error(`FractureLab Shader Error: ${err}`);
    }
    return s;
}

function linkProgram(gl, vsSrc, fsSrc)
{
    const vs = compileShader(gl, gl.VERTEX_SHADER, vsSrc);
    const fs = compileShader(gl, gl.FRAGMENT_SHADER, fsSrc);
    const p = gl.createProgram();
    gl.attachShader(p, vs);
    gl.attachShader(p, fs);
    gl.linkProgram(p);
    gl.deleteShader(vs);
    gl.deleteShader(fs);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS))
    {
        throw new Error(`FractureLab Link Error: ${gl.getProgramInfoLog(p)}`);
    }
    return p;
}

function mat4Perspective(fovYRad, aspect, near, far)
{
    const f = 1.0 / Math.tan(fovYRad * 0.5);
    const nf = 1.0 / (near - far);
    return new Float32Array([
        f / aspect, 0, 0, 0,
        0, f, 0, 0,
        0, 0, (far + near) * nf, -1,
        0, 0, (2 * far * near) * nf, 0
    ]);
}

function mat4LookAt(eye, target, up)
{
    let zx = eye[0] - target[0], zy = eye[1] - target[1], zz = eye[2] - target[2];
    let zl = Math.hypot(zx, zy, zz) || 1;
    zx /= zl; zy /= zl; zz /= zl;

    let xx = up[1] * zz - up[2] * zy;
    let xy = up[2] * zx - up[0] * zz;
    let xz = up[0] * zy - up[1] * zx;
    let xl = Math.hypot(xx, xy, xz) || 1;
    xx /= xl; xy /= xl; xz /= xl;

    const yx = zy * xz - zz * xy;
    const yy = zz * xx - zx * xz;
    const yz = zx * xy - zy * xx;

    return new Float32Array([
        xx, yx, zx, 0,
        xy, yy, zy, 0,
        xz, yz, zz, 0,
        -(xx * eye[0] + xy * eye[1] + xz * eye[2]),
        -(yx * eye[0] + yy * eye[1] + yz * eye[2]),
        -(zx * eye[0] + zy * eye[1] + zz * eye[2]),
        1
    ]);
}

function mat4Mul(A, B)
{
    const out = new Float32Array(16);
    for (let c = 0; c < 4; c++)
    {
        for (let r = 0; r < 4; r++)
        {
            out[c * 4 + r] =
                A[0 * 4 + r] * B[c * 4 + 0] +
                A[1 * 4 + r] * B[c * 4 + 1] +
                A[2 * 4 + r] * B[c * 4 + 2] +
                A[3 * 4 + r] * B[c * 4 + 3];
        }
    }
    return out;
}

export class FractureLabViewport
{
    constructor(canvas, callbacks = {})
    {
        this.canvas = canvas;
        this.callbacks = callbacks;
        this.gl = canvas.getContext('webgl2', { antialias: true, alpha: false, preserveDrawingBuffer: true });
        if (!this.gl) throw new Error('WebGL2 is required for Pure-SDF Fracture Lab.');

        this.renderMode = 'raymarch'; // 'raymarch' | 'qef-mesh'
        this.showWireframe = false;
        this.lensMode = 0;
        this.exposure = 1.02;
        this.active = false;

        // Dramatic 3/4 hero camera looking straight down the cleaved jigsaw chasm!
        this.yaw = 0.44;
        this.pitch = 0.24;
        this.distance = 10.2;
        this.target = [0.0, 2.55, 0.0];

        this.formation = null;
        this.meshStats = null;
        this.meshIndexCount = 0;
        this.wireIndexCount = 0;

        this._initGlResources();
        this._bindPointerEvents();
    }

    _initGlResources()
    {
        const gl = this.gl;
        this.rayProg = linkProgram(gl, RAYMARCH_VS, RAYMARCH_FS);
        this.meshProg = linkProgram(gl, MESH_VS, MESH_FS);

        this.emptyVao = gl.createVertexArray();

        // RGBA32F data textures for exact polyhedral blocks & half-space planes
        this.blockTex = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, this.blockTex);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

        this.planeTex = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, this.planeTex);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

        // QEF Dual-Contour Mesh VAO & buffers
        this.meshVao = gl.createVertexArray();
        this.vboPos = gl.createBuffer();
        this.vboNorm = gl.createBuffer();
        this.vboAlbedo = gl.createBuffer();
        this.vboMasks = gl.createBuffer();
        this.iboTris = gl.createBuffer();
        this.iboWire = gl.createBuffer();

        gl.bindVertexArray(this.meshVao);
        gl.bindBuffer(gl.ARRAY_BUFFER, this.vboPos);
        gl.enableVertexAttribArray(0);
        gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);

        gl.bindBuffer(gl.ARRAY_BUFFER, this.vboNorm);
        gl.enableVertexAttribArray(1);
        gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);

        gl.bindBuffer(gl.ARRAY_BUFFER, this.vboAlbedo);
        gl.enableVertexAttribArray(2);
        gl.vertexAttribPointer(2, 3, gl.FLOAT, false, 0, 0);

        gl.bindBuffer(gl.ARRAY_BUFFER, this.vboMasks);
        gl.enableVertexAttribArray(3);
        gl.vertexAttribPointer(3, 4, gl.FLOAT, false, 0, 0);
        gl.bindVertexArray(null);
    }

    setFormation(formation)
    {
        this.formation = formation;
        if (!formation) return;

        const gl = this.gl;
        const packed = packFracturedFormationGpuBuffers(formation);
        this.numBlocks = packed.numBlocks;

        gl.bindTexture(gl.TEXTURE_2D, this.blockTex);
        gl.texImage2D(
            gl.TEXTURE_2D, 0, gl.RGBA32F,
            packed.blockTexWidth, packed.blockTexHeight, 0,
            gl.RGBA, gl.FLOAT, packed.blockTexData
        );

        gl.bindTexture(gl.TEXTURE_2D, this.planeTex);
        gl.texImage2D(
            gl.TEXTURE_2D, 0, gl.RGBA32F,
            packed.planeTexWidth, packed.planeTexHeight, 0,
            gl.RGBA, gl.FLOAT, packed.planeTexData
        );
    }

    uploadQefMesh(mesh, sdfStats = null)
    {
        const gl = this.gl;
        this.meshStats = sdfStats;
        if (!mesh || !mesh.indices || mesh.indices.length === 0)
        {
            this.meshIndexCount = 0;
            this.wireIndexCount = 0;
            return;
        }

        gl.bindBuffer(gl.ARRAY_BUFFER, this.vboPos);
        gl.bufferData(gl.ARRAY_BUFFER, mesh.positions, gl.STATIC_DRAW);

        gl.bindBuffer(gl.ARRAY_BUFFER, this.vboNorm);
        gl.bufferData(gl.ARRAY_BUFFER, mesh.normals, gl.STATIC_DRAW);

        gl.bindBuffer(gl.ARRAY_BUFFER, this.vboAlbedo);
        gl.bufferData(gl.ARRAY_BUFFER, mesh.albedos, gl.STATIC_DRAW);

        gl.bindBuffer(gl.ARRAY_BUFFER, this.vboMasks);
        gl.bufferData(gl.ARRAY_BUFFER, mesh.colors, gl.STATIC_DRAW);

        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.iboTris);
        gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, mesh.indices, gl.STATIC_DRAW);
        this.meshIndexCount = mesh.indices.length;

        // Build wireframe edge index buffer
        const triCount = (mesh.indices.length / 3) | 0;
        const wire = new Uint32Array(triCount * 6);
        for (let t = 0; t < triCount; t++)
        {
            const a = mesh.indices[t * 3], b = mesh.indices[t * 3 + 1], c = mesh.indices[t * 3 + 2];
            const w = t * 6;
            wire[w] = a; wire[w + 1] = b;
            wire[w + 2] = b; wire[w + 3] = c;
            wire[w + 4] = c; wire[w + 5] = a;
        }
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.iboWire);
        gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, wire, gl.STATIC_DRAW);
        this.wireIndexCount = wire.length;
    }

    frameFormation()
    {
        if (!this.formation) return;
        const b = this.formation.bounds;
        this.target = [
            (b.minX + b.maxX) * 0.5,
            (b.minY + b.maxY) * 0.44,
            (b.minZ + b.maxZ) * 0.5
        ];
        const span = Math.max(b.maxX - b.minX, b.maxY - b.minY, b.maxZ - b.minZ);
        this.distance = span * 1.36;
        this.yaw = 0.44;
        this.pitch = 0.24;
    }

    _bindPointerEvents()
    {
        let dragging = false;
        let panMode = false;
        let lastX = 0, lastY = 0;

        this.canvas.addEventListener('contextmenu', e => e.preventDefault());

        this.canvas.addEventListener('pointerdown', e =>
        {
            dragging = true;
            panMode = (e.button === 1) || e.shiftKey;
            lastX = e.clientX;
            lastY = e.clientY;
            this.canvas.setPointerCapture(e.pointerId);
        });

        this.canvas.addEventListener('pointermove', e =>
        {
            if (!dragging) return;
            const dx = e.clientX - lastX;
            const dy = e.clientY - lastY;
            lastX = e.clientX;
            lastY = e.clientY;

            if (panMode)
            {
                const scale = this.distance * 0.0014;
                const cy = Math.cos(this.yaw), sy = Math.sin(this.yaw);
                const right = [cy, 0, -sy];
                this.target[0] -= right[0] * dx * scale;
                this.target[2] -= right[2] * dx * scale;
                this.target[1] += dy * scale;
            }
            else
            {
                this.yaw -= dx * 0.0065;
                this.pitch = clamp(this.pitch + dy * 0.0055, -0.18, 1.38);
            }
        });

        const endDrag = e =>
        {
            if (!dragging) return;
            dragging = false;
            try { this.canvas.releasePointerCapture(e.pointerId); } catch (_) { /* ignore */ }
        };
        this.canvas.addEventListener('pointerup', endDrag);
        this.canvas.addEventListener('pointercancel', endDrag);

        this.canvas.addEventListener('wheel', e =>
        {
            e.preventDefault();
            const factor = Math.exp(e.deltaY * 0.0011);
            this.distance = clamp(this.distance * factor, 2.2, 34.0);
        }, { passive: false });
    }

    _computeCameraBasis()
    {
        const cp = Math.cos(this.pitch), sp = Math.sin(this.pitch);
        const cy = Math.cos(this.yaw), sy = Math.sin(this.yaw);
        const eye = [
            this.target[0] + this.distance * cp * sy,
            this.target[1] + this.distance * sp,
            this.target[2] + this.distance * cp * cy
        ];
        let fx = this.target[0] - eye[0], fy = this.target[1] - eye[1], fz = this.target[2] - eye[2];
        const fl = Math.hypot(fx, fy, fz) || 1;
        fx /= fl; fy /= fl; fz /= fl;

        // Right = normalize(cross(fwd, worldUp))
        let rx = -fz, ry = 0, rz = fx;
        const rl = Math.hypot(rx, ry, rz) || 1;
        rx /= rl; rz /= rl;

        // Up = cross(right, fwd)
        const ux = ry * fz - rz * fy;
        const uy = rz * fx - rx * fz;
        const uz = rx * fy - ry * fx;

        return { eye, fwd: [fx, fy, fz], right: [rx, ry, rz], up: [ux, uy, uz] };
    }

    render()
    {
        if (!this.formation) return;
        const gl = this.gl;

        // Cap DPR at 1.35 in raymarch mode for butter-smooth 60 FPS across all screens
        const maxDpr = this.renderMode === 'raymarch' ? 1.35 : 1.75;
        const dpr = Math.min(window.devicePixelRatio || 1, maxDpr);
        const cw = Math.max(1, Math.floor(this.canvas.clientWidth * dpr));
        const ch = Math.max(1, Math.floor(this.canvas.clientHeight * dpr));
        if (this.canvas.width !== cw || this.canvas.height !== ch)
        {
            this.canvas.width = cw;
            this.canvas.height = ch;
        }
        gl.viewport(0, 0, cw, ch);

        const aspect = cw / ch;
        const fovRad = 42 * DEG2RAD;
        const cam = this._computeCameraBasis();
        const { params, bounds, sunDir, theme, crackSalt } = this.formation;

        if (this.renderMode === 'raymarch' || this.meshIndexCount === 0)
        {
            gl.disable(gl.DEPTH_TEST);
            gl.disable(gl.BLEND);
            gl.useProgram(this.rayProg);

            gl.activeTexture(gl.TEXTURE0);
            gl.bindTexture(gl.TEXTURE_2D, this.blockTex);
            gl.uniform1i(gl.getUniformLocation(this.rayProg, 'uBlockTex'), 0);

            gl.activeTexture(gl.TEXTURE1);
            gl.bindTexture(gl.TEXTURE_2D, this.planeTex);
            gl.uniform1i(gl.getUniformLocation(this.rayProg, 'uPlaneTex'), 1);

            gl.uniform1i(gl.getUniformLocation(this.rayProg, 'uNumBlocks'), this.numBlocks || 0);
            gl.uniform1i(gl.getUniformLocation(this.rayProg, 'uCrackSalt'), crackSalt | 0);

            gl.uniform3fv(gl.getUniformLocation(this.rayProg, 'uCamPos'), cam.eye);
            gl.uniform3fv(gl.getUniformLocation(this.rayProg, 'uCamFwd'), cam.fwd);
            gl.uniform3fv(gl.getUniformLocation(this.rayProg, 'uCamRight'), cam.right);
            gl.uniform3fv(gl.getUniformLocation(this.rayProg, 'uCamUp'), cam.up);
            gl.uniform1f(gl.getUniformLocation(this.rayProg, 'uTanHalfFov'), Math.tan(fovRad * 0.5));
            gl.uniform1f(gl.getUniformLocation(this.rayProg, 'uAspect'), aspect);

            gl.uniform3f(gl.getUniformLocation(this.rayProg, 'uBoundsMin'), bounds.minX, bounds.minY, bounds.minZ);
            gl.uniform3f(gl.getUniformLocation(this.rayProg, 'uBoundsMax'), bounds.maxX, bounds.maxY, bounds.maxZ);
            gl.uniform3fv(gl.getUniformLocation(this.rayProg, 'uSunDir'), sunDir);
            gl.uniform1f(gl.getUniformLocation(this.rayProg, 'uExposure'), this.exposure);
            gl.uniform1i(gl.getUniformLocation(this.rayProg, 'uLensMode'), this.lensMode);

            gl.uniform1f(gl.getUniformLocation(this.rayProg, 'uScale'), params.scale);
            gl.uniform1f(gl.getUniformLocation(this.rayProg, 'uJigsawRoughness'), params.jigsawRoughness ?? 0.55);
            gl.uniform1f(gl.getUniformLocation(this.rayProg, 'uPlumoseStrength'), params.plumoseStrength ?? 0.60);
            gl.uniform1f(gl.getUniformLocation(this.rayProg, 'uThermalNorm'), clamp((params.thermalDeltaT ?? 38) / 65.0, 0, 1.5));
            gl.uniform1f(gl.getUniformLocation(this.rayProg, 'uDirectionalBias'), params.directionalBias ?? 0.55);
            gl.uniform1f(gl.getUniformLocation(this.rayProg, 'uSkinDepth'), Math.max(0.06, params.thermalSkinDepth ?? 0.44));
            gl.uniform1f(gl.getUniformLocation(this.rayProg, 'uCrazing90Strength'), params.crazing90Strength ?? 0.30);
            gl.uniform1f(gl.getUniformLocation(this.rayProg, 'uCrazingSpacing'), Math.max(0.18, params.crazingSpacing ?? 0.62));
            gl.uniform1f(gl.getUniformLocation(this.rayProg, 'uVoronoiStrength'), params.voronoiJointStrength ?? 0.36);
            gl.uniform1f(gl.getUniformLocation(this.rayProg, 'uExfoliationStrength'), params.exfoliationStrength ?? 0.14);
            gl.uniform1i(gl.getUniformLocation(this.rayProg, 'uCutawayEnabled'), params.cutawayEnabled ? 1 : 0);
            gl.uniform1i(gl.getUniformLocation(this.rayProg, 'uCutawayAxis'), params.cutawayAxis ?? 0);
            gl.uniform1f(gl.getUniformLocation(this.rayProg, 'uCutawayOffset'), params.cutawayOffset ?? 0.25);

            gl.uniform3fv(gl.getUniformLocation(this.rayProg, 'uColRind'), theme.rind);
            gl.uniform3fv(gl.getUniformLocation(this.rayProg, 'uColFresh'), theme.fresh);
            gl.uniform3fv(gl.getUniformLocation(this.rayProg, 'uColIron'), theme.iron);
            gl.uniform3fv(gl.getUniformLocation(this.rayProg, 'uColLichen'), theme.lichen);

            gl.bindVertexArray(this.emptyVao);
            gl.drawArrays(gl.TRIANGLES, 0, 3);
            gl.bindVertexArray(null);
            return;
        }

        // Mode 2: QEF Dual-Contoured Triangle Mesh + Sharp-Crease Wireframe Overlay
        gl.clearColor(0.072, 0.076, 0.088, 1.0);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.enable(gl.DEPTH_TEST);
        gl.depthFunc(gl.LEQUAL);

        const proj = mat4Perspective(fovRad, aspect, 0.1, 120.0);
        const view = mat4LookAt(cam.eye, this.target, [0, 1, 0]);
        const vp = mat4Mul(proj, view);

        gl.useProgram(this.meshProg);
        gl.uniformMatrix4fv(gl.getUniformLocation(this.meshProg, 'uViewProj'), false, vp);
        gl.uniform3fv(gl.getUniformLocation(this.meshProg, 'uCamPos'), cam.eye);
        gl.uniform3fv(gl.getUniformLocation(this.meshProg, 'uSunDir'), sunDir);
        gl.uniform1f(gl.getUniformLocation(this.meshProg, 'uExposure'), this.exposure);
        gl.uniform1i(gl.getUniformLocation(this.meshProg, 'uLensMode'), this.lensMode);
        gl.uniform1i(gl.getUniformLocation(this.meshProg, 'uIsWirePass'), 0);

        gl.bindVertexArray(this.meshVao);
        gl.enable(gl.POLYGON_OFFSET_FILL);
        gl.polygonOffset(1.0, 1.0);
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.iboTris);
        gl.drawElements(gl.TRIANGLES, this.meshIndexCount, gl.UNSIGNED_INT, 0);
        gl.disable(gl.POLYGON_OFFSET_FILL);

        if (this.showWireframe && this.wireIndexCount > 0)
        {
            gl.enable(gl.BLEND);
            gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
            gl.uniform1i(gl.getUniformLocation(this.meshProg, 'uIsWirePass'), 1);
            gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.iboWire);
            gl.drawElements(gl.LINES, this.wireIndexCount, gl.UNSIGNED_INT, 0);
            gl.disable(gl.BLEND);
        }

        gl.bindVertexArray(null);
    }
}
