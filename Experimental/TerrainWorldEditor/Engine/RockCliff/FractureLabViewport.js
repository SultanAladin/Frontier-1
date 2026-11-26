/*==========================================================================================================================================
                                                     FRACTURELABVIEWPORT.JS
==========================================================================================================================================*/
// Dual-Mode WebGL2 3D Viewport for the Frontier Pure-SDF Brittle Fracture & Thermal Cracking Lab.
//
// Engineered specifically for instant (< 5 ms) Windows Chrome ANGLE / D3D11 HLSL compilation & 60 FPS rendering:
//   1. `SDF_BAKE_FS`: Evaluates the Pure-SDF formation (`d`, `freshMask`, `thermalStress`, `strataMod`)
//      strictly ONCE per voxel into a single `112^3` `RGBA16F` 3D volume texture (`uSdfVol`, single attachment, no MRT).
//      Includes automatic core-WebGL2 CPU `RGBA16F` upload fallback if `EXT_color_buffer_float` is unavailable.
//   2. `RAYMARCH_FS`: Ultra-compact (~110-line, zero-noise, zero-nested-loop) 3D SDF raymarcher using `textureLod(..., 0.0)`
//      with analytical sub-pixel cross-section cutaway slicing, soft SDF shadows, volumetric 3D SDF AO, and 5 diagnostic lenses.
//   3. `MESH_VS` / `MESH_FS`: Sparse 4^3-Brick QEF Dual-Contoured Manifold Triangle Mesh + Sharp-Crease Wireframe Overlay.

import { clamp, DEG2RAD } from './Core/SpatialNoise3D.js';
import { evalFracturedFormationSdf, packFracturedFormationGpuBuffers } from './PureSdfFractureEngine.js';

/*==========================================================================================================================================
                          1. STAGE-1 SHADER: SINGLE-PASS GPU PURE-SDF 3D VOLUME BAKER (`uSdfVol`)
==========================================================================================================================================*/

const FULLSCREEN_VS = `#version 300 es
precision highp float;
out vec2 vNdc;
void main() {
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
    vNdc = p;
    gl_Position = vec4(p, 0.0, 1.0);
}
`;

const SDF_BAKE_FS = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

in vec2 vNdc;
out vec4 outSdf; // (d, freshMask, thermalStress, strataMod)

uniform sampler2D uBlockTex;
uniform sampler2D uPlaneTex;
uniform int uNumBlocks;
uniform int uCrackSalt;
uniform vec3 uVolMin;
uniform vec3 uVolMax;
uniform float uSliceZNorm;

uniform float uScale;
uniform vec3 uSunDir;
uniform float uJigsawRoughness;
uniform float uPlumoseStrength;
uniform float uStrataContrast;
uniform float uThermalNorm;
uniform float uDirectionalBias;
uniform float uSkinDepth;
uniform float uCrazing90Strength;
uniform float uCrazingSpacing;
uniform float uVoronoiStrength;
uniform float uExfoliationStrength;
uniform float uChemicalDissolution;
uniform float uTafoniHoneycomb;
uniform float uKarrenFlutes;

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
    return mix(mix(nx00, nx10, u.y), mix(nx01, nx11, u.y), u.z) * 2.0 - 1.0;
}

void vorCorner(ivec3 base, int ox, int oy, int oz, vec3 p, float jitter, int salt,
               inout float min1, inout float min2, inout vec3 c1, inout vec3 c2, inout uint id1) {
    ivec3 c = base + ivec3(ox, oy, oz);
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

vec3 evalFastCleavage3D(vec3 p, float jitter, int salt) {
    ivec3 base = ivec3(floor(p - 0.5));
    float min1 = 1e9;
    float min2 = 1e9;
    vec3 c1 = vec3(0.0);
    vec3 c2 = vec3(0.0);
    uint id1 = 0u;

    vorCorner(base, 0, 0, 0, p, jitter, salt, min1, min2, c1, c2, id1);
    vorCorner(base, 1, 0, 0, p, jitter, salt, min1, min2, c1, c2, id1);
    vorCorner(base, 0, 1, 0, p, jitter, salt, min1, min2, c1, c2, id1);
    vorCorner(base, 1, 1, 0, p, jitter, salt, min1, min2, c1, c2, id1);
    vorCorner(base, 0, 0, 1, p, jitter, salt, min1, min2, c1, c2, id1);
    vorCorner(base, 1, 0, 1, p, jitter, salt, min1, min2, c1, c2, id1);
    vorCorner(base, 0, 1, 1, p, jitter, salt, min1, min2, c1, c2, id1);
    vorCorner(base, 1, 1, 1, p, jitter, salt, min1, min2, c1, c2, id1);

    float bLen = length(c2 - c1);
    float wallDist = bLen > 1e-5 ? (min2 - min1) / (2.0 * bLen) : 1.0;
    float cellRand = float((id1 >> 8u) & 0xffffu) / 32767.5 - 1.0;
    return vec3(wallDist, cellRand, sqrt(max(0.0, min1)));
}

vec4 evalOrganic90Crazing3D(float u, float v, float w, float spacing, int salt) {
    float L1 = max(0.08, spacing);
    float L2 = L1 * 0.94;

    // 1. Rotate joint frame ~22 deg off rigid horizontal/vertical Cartesian axes!
    float ur0 = u * 0.92718385 - v * 0.37460659;
    float vr0 = u * 0.37460659 + v * 0.92718385;

    // 2. Strike-anisotropic 3-scale organic warp: meanders & kinks boldly along strike with ZERO bullseye loops!
    float invMacro = 1.0 / (L1 * 2.15);
    float invMeso = 1.0 / (L1 * 0.68);
    float invJag = 1.0 / (L1 * 0.24);

    float macroU = L1 * 0.38 * scalarField3D(vec3(ur0 * 0.35, vr0, w * 0.8) * invMacro, salt + 3);
    float macroV = L2 * 0.36 * scalarField3D(vec3(ur0 * invMacro + 7.3, vr0 * invMacro * 0.35 - 3.1, w * invMacro * 0.8), salt + 19);

    float mesoU = L1 * 0.14 * scalarField3D(vec3(ur0 * 0.26, vr0, w * 0.8) * invMeso, salt + 29);
    float mesoV = L2 * 0.13 * scalarField3D(vec3(ur0 * invMeso - 4.7, vr0 * invMeso * 0.26 + 5.9, w * invMeso * 0.8), salt + 37);

    float jagU = L1 * 0.048 * (abs(scalarField3D(vec3(ur0 * 0.20, vr0, w * 0.7) * invJag, salt + 43)) * 2.0 - 1.0);
    float jagV = L2 * 0.046 * (abs(scalarField3D(vec3(ur0 * invJag + 2.1, vr0 * invJag * 0.20, w * invJag * 0.7), salt + 59)) * 2.0 - 1.0);

    float uCoord = (ur0 + macroU + mesoU + jagU) / L1;
    float k1f = floor(uCoord);
    int k1 = int(k1f);
    float fu = uCoord - k1f;
    float tier1Dist = min(fu, 1.0 - fu) * L1;

    float slabPhase = hash31(k1, 17, 53, salt + 41);
    float slabScale = 0.68 + 0.64 * hash31(k1, 89, 13, salt + 67);
    float effL2 = L2 * slabScale;

    float vCoord = (vr0 + macroV + mesoV + jagV) / effL2 + slabPhase;
    float k2f = floor(vCoord);
    int k2 = int(k2f);
    float fv = vCoord - k2f;
    float tier2Dist = min(fv, 1.0 - fv) * effL2;

    float tileRand = hash31(k1, k2, 7, salt + 97);
    if (tileRand > 0.56) {
        float splitPos = 0.28 + 0.44 * hash31(k1, k2, 31, salt + 131);
        float splitTilt = (hash31(k1, k2, 59, salt + 137) - 0.5) * 0.22;
        float tier3Dist = abs(fu - (splitPos + splitTilt * (fv - 0.5))) * L1 * 1.45;
        tier2Dist = min(tier2Dist, tier3Dist);
    }

    // 3. True Hierarchical C1 Pinch-and-Swell Aperture (bold Tier-1 master fissures -> finer tapering Tier-2 hairlines!)
    float vit1 = scalarField3D(vec3(ur0 * (0.48 / L1), vr0 * (0.62 / L1), w * (0.52 / L1)), salt + 151);
    float vit2 = scalarField3D(vec3(ur0 * (0.68 / L1) + 11.3, vr0 * (0.48 / L1) - 3.8, w * (0.58 / L1)), salt + 173);
    float lipChip = 0.80 + 0.42 * abs(scalarField3D(vec3(ur0, vr0, w) * (invJag * 1.1), salt + 199));

    float open1 = smoothstep(-0.18, 0.42, vit1);
    float open2 = smoothstep(-0.10, 0.48, vit2);
    float mod1 = open1 * open1 * 1.38 * lipChip;
    float mod2 = open2 * sqrt(max(0.0, open2)) * 0.66 * lipChip;

    if (mod1 > 0.06 && mod2 > 0.04) {
        float spall = 0.35 * smoothstep(L1 * 0.14, 0.0, max(tier1Dist, tier2Dist));
        mod1 += spall * open1;
        mod2 += spall * open2;
    }
    return vec4(tier1Dist, tier2Dist, mod1, mod2);
}

float sminPoly(float a, float b, float k) {
    if (k <= 1e-6) return min(a, b);
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * 0.25;
}

float smaxPoly(float a, float b, float k) {
    if (k <= 1e-6) return max(a, b);
    float h = max(k - abs(a - b), 0.0) / k;
    return max(a, b) + h * h * k * 0.25;
}

float chamferMax(float a, float b, float r) {
    float m = max(a, b);
    if (r <= 1e-6) return m;
    return max(m, (a + b + r) * 0.70710678);
}

float chamferSub(float base, float cut, float r) {
    return chamferMax(base, -cut, r);
}

void main() {
    vec2 uv = vNdc * 0.5 + 0.5;
    vec3 p = mix(uVolMin, uVolMax, vec3(uv.x, uv.y, uSliceZNorm));

    float bestD = 1e9;
    float bestDExt = 1e9;
    float bestDFrac = -1e9;
    float bestBevel = 0.04;
    vec3 bestP0 = p;
    vec3 bestN = vec3(0.0, 1.0, 0.0);
    float bestFresh = 0.0;
    float bestStrata = 0.25;
    bool noCracks = false;

    float exfRound = uScale * 0.088 * smoothstep(0.18, 0.95, uExfoliationStrength);
    float chemRound = uScale * 0.056 * uChemicalDissolution;

    for (int i = 0; i < 32; i++) {
        if (i >= uNumBlocks) break;
        vec4 sph = texelFetch(uBlockTex, ivec2(0, i), 0);
        float lowerB = length(p - sph.xyz) - sph.w - uScale * 0.085;
        if (lowerB > bestD) continue;

        vec4 meta = texelFetch(uBlockTex, ivec2(1, i), 0);
        int pStart = int(meta.x + 0.5);
        int pCount = int(meta.y + 0.5);
        float bevel = meta.z;
        bool blkNoCracks = meta.w > 0.5;

        vec4 r0 = texelFetch(uBlockTex, ivec2(2, i), 0);
        vec4 r1 = texelFetch(uBlockTex, ivec2(3, i), 0);
        vec4 r2 = texelFetch(uBlockTex, ivec2(4, i), 0);
        vec4 m5 = texelFetch(uBlockTex, ivec2(5, i), 0);

        vec3 p0 = vec3(dot(r0.xyz, p) + r0.w, dot(r1.xyz, p) + r1.w, dot(r2.xyz, p) + r2.w);
        float effStrata = m5.y * uStrataContrast;

        float f0 = 0.58 / uScale;
        float f1 = 1.65 / uScale;
        float f2 = 4.40 / uScale;
        float macroBend = scalarField3D(vec3(p0.x * f0, p0.y * f0 * 0.75, p0.z * f0), uCrackSalt + 2);
        float w1 = scalarField3D(vec3(p0.x * f1, p0.y * f1 * 0.9, p0.z * f1), uCrackSalt + 5) * 0.67 +
                   scalarField3D(vec3(p0.x * f1, p0.y * f1 * 0.9, p0.z * f1) * 2.05, uCrackSalt + 106) * 0.33;
        float jagTooth = abs(scalarField3D(p0 * f2, uCrackSalt + 47)) * 2.0 - 1.0;

        float rNuc = length(vec3(p0.x, p0.y - uScale * 0.08, p0.z));
        float plum = sin(rNuc * (22.0 / uScale) + w1 * 4.2) * 0.45 +
                     scalarField3D(vec3(p0.x * (8.5 / uScale), p0.y * (4.8 / uScale), p0.z * (8.5 / uScale)), uCrackSalt + 71) * 0.55;
        float plumAuto = clamp(0.20 + 0.72 * uJigsawRoughness, 0.0, 1.0) * mix(0.65, uPlumoseStrength, 0.5) * smoothstep(0.0, 0.20, uJigsawRoughness);
        float jigsawDisp = uScale * uJigsawRoughness * (0.022 * macroBend + 0.017 * w1 + 0.007 * jagTooth) +
                           uScale * 0.0080 * plumAuto * plum;

        float chemSoften = 1.0 - 0.42 * uChemicalDissolution;
        float macroLobe = uScale * 0.024 * scalarField3D(vec3(p0.x, p0.y * 0.85, p0.z) * (0.85 / uScale), uCrackSalt + 109);
        float angularCrag = uScale * 0.015 * chemSoften * (abs(scalarField3D(p0 * (2.15 / uScale), uCrackSalt + 113)) * 2.0 - 1.0);
        float fineGrain = uScale * 0.0050 * chemSoften * scalarField3D(p0 * (5.10 / uScale), uCrackSalt + 127);
        float strataAmp = uScale * 0.0065 * effStrata * (1.0 - 0.55 * uKarrenFlutes);
        float bedY = p0.y * (5.4 / uScale) + 0.52 * scalarField3D(vec3(p0.x * (1.6 / uScale), p0.y * (2.4 / uScale), p0.z * (1.6 / uScale)), uCrackSalt + 149);
        float strataLedge = strataAmp * (sin(bedY * 6.2831853) * 0.72 + 0.28 * sin(bedY * 12.5663706 + 0.8));
        float extRelief = macroLobe + angularCrag + fineGrain + strataLedge;

        float wf = 1.25 / uScale;
        float wa = uScale * 0.024;
        vec3 extWarp = vec3(
            wa * scalarField3D(vec3(p0.x * wf, p0.y * wf * 0.9, p0.z * wf), uCrackSalt + 131),
            wa * 0.75 * scalarField3D(vec3(p0.x * wf + 5.7, p0.y * wf * 0.9 - 3.1, p0.z * wf), uCrackSalt + 137),
            wa * scalarField3D(vec3(p0.x * wf - 4.3, p0.y * wf * 0.9 + 6.2, p0.z * wf), uCrackSalt + 139)
        );

        float basalNotch = 0.0;
        if (uChemicalDissolution > 0.01 && !blkNoCracks && p.y < uScale * 0.32) {
            float yRel = (p.y - uScale * 0.095) / (uScale * 0.076);
            float notchBell = exp(-yRel * yRel) * smoothstep(0.0, uScale * 0.028, p.y);
            float bedMod = 0.74 + 0.26 * sin(p0.y * (12.0 / uScale) + 1.1 * scalarField3D(p0 * (1.5 / uScale), uCrackSalt + 701));
            basalNotch = uScale * 0.058 * uChemicalDissolution * notchBell * bedMod;
        }

        float kLse = max(0.012, (bevel * (0.95 + 0.85 * abs(scalarField3D(p0 * (1.6 / uScale), uCrackSalt + 163))) + exfRound + chemRound) * 0.42);
        float invKLse = 1.0 / kLse;
        float invNormSoft = 1.0 / max(0.035, kLse * 1.35);

        float dFloor = -1e9;
        float bestExtPd = -1e9;
        float sumExtExp = 0.0;
        float dFrac = -1e9;
        float bestTopPd = -1e9;
        vec3 sumN = vec3(0.0);

        for (int j = 0; j < 28; j++) {
            if (j >= pCount) break;
            vec4 pl = texelFetch(uPlaneTex, ivec2(0, pStart + j), 0);
            float pType = texelFetch(uPlaneTex, ivec2(1, pStart + j), 0).x;
            float pd = dot(pl.xyz, p) - pl.w;

            if (abs(pType) > 0.5) {
                pd += pType * jigsawDisp;
                dFrac = dFrac < -1e8 ? pd : chamferMax(dFrac, pd, bevel * 0.65);
            } else if (pl.y <= -0.85) {
                dFloor = max(dFloor, pd);
            } else {
                float sideMask = max(0.0, 1.0 - abs(pl.y) * 0.80);
                pd += extRelief + basalNotch * sideMask + dot(pl.xyz, extWarp);
                if (pd > bestExtPd) {
                    sumExtExp = bestExtPd > -1e8 ? sumExtExp * exp(max(-24.0, (bestExtPd - pd) * invKLse)) + 1.0 : 1.0;
                    bestExtPd = pd;
                } else {
                    sumExtExp += exp(max(-24.0, (pd - bestExtPd) * invKLse));
                }
            }

            if (pd > bestTopPd) {
                float rescale = bestTopPd > -1e8 ? exp(max(-20.0, (bestTopPd - pd) * invNormSoft)) : 0.0;
                sumN = sumN * rescale + pl.xyz;
                bestTopPd = pd;
            } else {
                float w = exp(max(-20.0, (pd - bestTopPd) * invNormSoft));
                sumN += pl.xyz * w;
            }
        }

        float dExt = bestExtPd > -1e8 ? (bestExtPd + kLse * log(max(1.0, sumExtExp))) : -1e9;
        float dMaxExt = dExt;
        if (dFloor > -1e8) dMaxExt = dMaxExt < -1e8 ? dFloor : max(dMaxExt, dFloor);

        float dBlk = dMaxExt;
        if (dFrac > -1e8) dBlk = dBlk < -1e8 ? dFrac : chamferMax(dBlk, dFrac, bevel);

        vec3 bN = normalize(length(sumN) > 1e-5 ? sumN : vec3(0.0, 1.0, 0.0));

        if (dBlk < bestD) {
            bestD = dBlk;
            bestDExt = dMaxExt;
            bestDFrac = dFrac;
            bestBevel = bevel;
            bestP0 = p0;
            bestN = bN;
            noCracks = blkNoCracks;
            bestStrata = effStrata;
            float tr = max(0.015, bevel * 1.35);
            bestFresh = smoothstep(-tr, tr, dFrac - dMaxExt);
        }
    }

    float d = bestD;
    float curMacro = bestD;
    float crackFresh = 0.0;
    float chemStain = 0.0;
    float thermalStress = uThermalNorm;
    float shellAgeFactor = 1.0;

    float maxFineDepth = max(
        max(uSkinDepth * 1.65, uExfoliationStrength > 0.14 ? uScale * 0.13 : 0.12),
        (uChemicalDissolution > 0.01 || uTafoniHoneycomb > 0.01 || uKarrenFlutes > 0.01) ? uScale * 0.11 : 0.12
    );
    if (!noCracks && curMacro < uScale * 0.05 && curMacro > -maxFineDepth &&
        (uCrazing90Strength > 0.01 || uVoronoiStrength > 0.01 || uExfoliationStrength > 0.14 ||
         uChemicalDissolution > 0.01 || uTafoniHoneycomb > 0.01 || uKarrenFlutes > 0.01)) {

        float ndots = max(1e-4, dot(bestN, uSunDir));
        float sunGate = clamp(pow(ndots, 1.25) + 0.10, 0.0, 1.0);
        float insol = mix(1.0, sunGate, uDirectionalBias) * uThermalNorm;
        thermalStress = insol;
        vec3 p0 = bestP0;

        if (uExfoliationStrength > 0.14) {
            float exfAct = smoothstep(0.14, 0.92, uExfoliationStrength);
            float hShell = uScale * (0.0130 + 0.0070 * exfAct);
            if (bestDExt > -(4.8 * hShell) && bestDExt < uScale * 0.04) {
                float r1 = length(vec3((p0.x - uScale * 0.30) * 0.88, (p0.y - uScale * 0.94) * 0.96, (p0.z - uScale * 0.32) * 0.90));
                float r2 = length(vec3((p0.x + uScale * 0.48) * 0.92, (p0.y - uScale * 0.68) * 1.02, (p0.z + uScale * 0.44) * 0.92)) + uScale * 0.16;
                float rBase = sminPoly(r1, r2, uScale * 0.14);

                float wf1 = 1.0 / (uScale * 0.30);
                float wf2 = 1.0 / (uScale * 0.105);
                float wShared = uScale * (
                    0.085 * scalarField3D(p0 * wf1, uCrackSalt + 491) +
                    0.032 * (abs(scalarField3D(p0 * wf2, uCrackSalt + 499)) * 2.0 - 1.0)
                );
                float basalHold = max(0.0, uScale * 0.14 - p0.y) * 1.65;
                float rSpall = rBase + wShared + basalHold;

                float fanF = 1.0 / (uScale * 0.42);
                float kf = 1.0 / (uScale * 0.15);
                float fan3 = uScale * 0.095 * scalarField3D(p0 * fanF, uCrackSalt + 503);
                float fan2 = uScale * (0.045 + 0.145 * (0.5 + 0.5 * scalarField3D(p0 * fanF + vec3(4.2, 0.0, -2.9), uCrackSalt + 509)));
                float fan1 = uScale * (0.045 + 0.140 * (0.5 + 0.5 * scalarField3D(p0 * fanF + vec3(-3.6, 5.1, 0.0), uCrackSalt + 517)));
                float fan0 = uScale * (0.045 + 0.135 * (0.5 + 0.5 * scalarField3D(p0 * fanF + vec3(1.9, -4.4, 3.3), uCrackSalt + 521)));

                float kink3 = uScale * 0.028 * (abs(scalarField3D(p0 * kf, uCrackSalt + 511)) * 2.0 - 1.0);
                float kink2 = uScale * 0.028 * (abs(scalarField3D(p0 * kf + vec3(3.7, 0.0, 0.0), uCrackSalt + 523)) * 2.0 - 1.0);
                float kink1 = uScale * 0.026 * (abs(scalarField3D(p0 * kf + vec3(-4.3, 0.0, 0.0), uCrackSalt + 541)) * 2.0 - 1.0);
                float kink0 = uScale * 0.024 * (abs(scalarField3D(p0 * kf + vec3(7.1, 0.0, 0.0), uCrackSalt + 557)) * 2.0 - 1.0);

                float maxR = uScale * 0.78 * exfAct;
                float dw3 = (maxR + fan3) - (rSpall + kink3);
                float dw2 = min(dw3 - uScale * 0.024, (maxR + fan3 - fan2 * exfAct) - (rSpall + kink2));
                float dw1 = min(dw2 - uScale * 0.024, (maxR + fan3 - (fan2 + fan1) * exfAct) - (rSpall + kink1));
                float dw0 = min(dw1 - uScale * 0.024, (maxR + fan3 - (fan2 + fan1 + fan0) * exfAct) - (rSpall + kink0));

                float pScale = (0.95 / uScale) * 3.6;
                float pwx = 0.36 * scalarField3D(p0 * (pScale * 0.5), uCrackSalt + 563);
                float pwz = 0.36 * scalarField3D(p0 * (pScale * 0.5) + vec3(4.8, 0.0, -3.1), uCrackSalt + 569);
                vec3 plateClv = evalFastCleavage3D(vec3(p0.x * pScale + pwx, p0.y * pScale * 0.9, p0.z * pScale + pwz), 0.62, uCrackSalt + 577);
                float plateWallWorld = plateClv.x / pScale;
                float plateOpen = smoothstep(-0.18, 0.34, scalarField3D(p0 * (pScale * 0.45), uCrackSalt + 587));

                float dOnionExt = bestDExt + 4.0 * hShell;
                float onionFresh = 0.96 * exfAct;
                float onionAge = 0.04;
                float wSlot = hShell * 2.25;

                float dws[4] = float[4](dw0, dw1, dw2, dw3);
                float freshTiers[4] = float[4](0.64, 0.36, 0.14, 0.00);
                float ageTiers[4] = float[4](0.15, 0.38, 0.72, 1.00);

                for (int k = 0; k < 4; k++) {
                    float surfOff = float(3 - k) * hShell;
                    float dSurf = bestDExt + surfOff;
                    float dw = dws[k];
                    float dSlab = chamferMax(dSurf, dw, hShell * 0.13);

                    float rimStress = smoothstep(-uScale * 0.42, 0.0, dw) * plateOpen * (0.58 + 0.15 * float(k));
                    float plateAp = hShell * 0.24 * rimStress;
                    if (plateAp > 0.0012) {
                        float cutPlate = max(plateWallWorld - plateAp, (-dSurf) - hShell * 0.95);
                        dSlab = chamferSub(dSlab, cutPlate, plateAp * 0.42);
                    }

                    float tSlot = clamp(1.0 + dw / wSlot, 0.0, 1.0);
                    if (tSlot > 0.001) {
                        float aSlot = hShell * 0.24 * tSlot;
                        float dSeamPlane = abs(dSurf + hShell * 0.90);
                        float cutSlot = max(max(dSeamPlane - aSlot, (-dw) - wSlot), dw - 0.025);
                        dSlab = max(dSlab, -cutSlot);
                    }

                    float blend = smoothstep(0.016, -0.016, dSlab - dOnionExt);
                    dOnionExt = min(dOnionExt, dSlab);
                    onionFresh = mix(onionFresh, freshTiers[k] * exfAct, blend);
                    onionAge = mix(onionAge, ageTiers[k], blend);
                }

                float dCombined = bestDFrac > -1e8 ? chamferMax(dOnionExt, bestDFrac, bestBevel) : dOnionExt;
                d = dCombined;
                curMacro = dCombined;

                float extWeight = 1.0 - bestFresh;
                crackFresh = max(crackFresh, onionFresh * extWeight);
                shellAgeFactor = mix(1.0, onionAge, exfAct * extWeight);
            }
        }

        float tipDepth = max(0.06, uSkinDepth) * clamp(0.30 + 0.70 * insol, 0.15, 1.35) * (0.35 + 0.65 * shellAgeFactor);
        float vTaper = max(0.0, 1.0 + min(0.0, curMacro) / tipDepth) * shellAgeFactor;
        float grikeFlare = 1.0 + 1.35 * uChemicalDissolution * smoothstep(-uScale * 0.050, 0.0, curMacro);
        float grikeLipSmooth = 1.0 + 1.80 * uChemicalDissolution;

        if (uCrazing90Strength > 0.01 && insol > 0.02 && curMacro > -(tipDepth + uSkinDepth * 0.65)) {
            vec3 an = abs(bestN);
            vec3 uvwT = (an.y >= an.x && an.y >= an.z) ? vec3(p0.x, p0.z, p0.y)
                      : (an.x >= an.z)                ? vec3(p0.z, p0.y, p0.x)
                                                      : vec3(p0.x, p0.y, p0.z);
            vec4 c90 = evalOrganic90Crazing3D(uvwT.x, uvwT.y, uvwT.z, uCrazingSpacing, uCrackSalt + 211);
            float surfHalfAp90 = uCrazingSpacing * 0.068 * uCrazing90Strength * insol * grikeFlare;
            if (surfHalfAp90 > 0.0015) {
                float ap1 = surfHalfAp90 * c90.z * vTaper;
                float ap2 = surfHalfAp90 * c90.w * vTaper;
                float cutOrganic90 = min(c90.x - ap1, c90.y * 1.12 - ap2);
                float effAp = max(ap1, ap2);
                float cut90 = max(cutOrganic90, (-curMacro) - tipDepth);
                d = uChemicalDissolution > 0.15
                    ? smaxPoly(d, -cut90, effAp * 0.42 * grikeLipSmooth)
                    : chamferSub(d, cut90, effAp * 0.35);
            }
        }

        if (uVoronoiStrength > 0.01 && curMacro > -(tipDepth + uSkinDepth * 0.65)) {
            float vorScale = (1.35 / uScale) * 4.2;
            vec3 vp = vec3(p0.x * vorScale, p0.y * vorScale * 0.86, p0.z * vorScale);
            vec3 w0 = vec3(
                0.38 * scalarField3D(vp * 0.55, uCrackSalt + 311) + 0.12 * (abs(scalarField3D(vp * 2.15, uCrackSalt + 317)) * 2.0 - 1.0),
                0.36 * scalarField3D(vp * 0.55 + vec3(5.2, -2.8, 0.0), uCrackSalt + 331) + 0.12 * (abs(scalarField3D(vp * 2.15 + vec3(-3.4, 0.0, 0.0), uCrackSalt + 337)) * 2.0 - 1.0),
                0.38 * scalarField3D(vp * 0.55 + vec3(-4.1, 6.3, 0.0), uCrackSalt + 347) + 0.12 * (abs(scalarField3D(vp * 2.15 + vec3(0.0, 4.1, 0.0), uCrackSalt + 353)) * 2.0 - 1.0)
            );
            vec3 clv = evalFastCleavage3D(vp + w0, 0.62, uCrackSalt + 307);
            float worldWall = clv.x / vorScale;

            float vorVit = scalarField3D(vp * 0.48 + vec3(-7.1, 3.9, 0.0), uCrackSalt + 367);
            float openVor = smoothstep(-0.12, 0.46, vorVit);
            float vorBridge = openVor * sqrt(max(0.0, openVor)) * (0.82 + 0.46 * abs(scalarField3D(vp * 2.7, uCrackSalt + 379)));

            float surfHalfApVor = (0.010 + 0.030 * uVoronoiStrength) * (0.45 + 0.55 * insol) * (uScale / 7.0) * vorBridge * grikeFlare;
            if (surfHalfApVor > 0.0012) {
                float halfApVor = surfHalfApVor * vTaper;
                float cutVor = max(worldWall - halfApVor, (-curMacro) - tipDepth);
                d = uChemicalDissolution > 0.15
                    ? smaxPoly(d, -cutVor, halfApVor * 0.45 * grikeLipSmooth)
                    : chamferSub(d, cutVor, halfApVor * 0.36);
            }

            float nearPrimary = smoothstep(0.42, 0.02, clv.x) * smoothstep(0.12, 0.60, vorBridge);
            if (nearPrimary > 0.04 && vTaper > 0.0) {
                float subScale = 2.45;
                vec3 subClv = evalFastCleavage3D((vp + w0 * 1.3) * subScale, 0.62, uCrackSalt + 389);
                float subWallWorld = subClv.x / (vorScale * subScale);
                float subHalfAp = (0.0045 + 0.012 * uVoronoiStrength) * nearPrimary * vTaper * (uScale / 7.0);
                if (subHalfAp > 0.0010) {
                    float cutSub = max(subWallWorld - subHalfAp, (-curMacro) - tipDepth * 0.55);
                    d = chamferSub(d, cutSub, subHalfAp * 0.32);
                }
            }
        }

        if (uChemicalDissolution > 0.01 && p.y < uScale * 0.28) {
            float yRel = (p.y - uScale * 0.095) / (uScale * 0.082);
            chemStain = max(chemStain, clamp(exp(-yRel * yRel) * uChemicalDissolution * 0.95, 0.0, 1.0));
        }

        float extRindGate = smoothstep(0.60, 0.10, bestFresh);

        if (uTafoniHoneycomb > 0.01 && extRindGate > 0.02 && curMacro > -uScale * 0.075) {
            float shelterGate = smoothstep(0.62, -0.02, bestN.y);
            float heightZone = smoothstep(uScale * 0.03, uScale * 0.11, p.y) * smoothstep(uScale * 0.88, uScale * 0.54, p.y + uScale * 0.20 * uKarrenFlutes);
            float patchN = scalarField3D(vec3(p0.x * (1.15 / uScale), p0.y * (1.55 / uScale), p0.z * (1.15 / uScale)), uCrackSalt + 719);
            float colonyGate = smoothstep(-0.20 + (1.0 - uTafoniHoneycomb) * 0.36, 0.32, patchN) * shelterGate * heightZone * extRindGate;

            if (colonyGate > 0.02) {
                float ts1 = 4.8 / uScale;
                float twx = 0.20 * scalarField3D(p0 * (ts1 * 0.5), uCrackSalt + 727);
                float twz = 0.20 * scalarField3D(p0 * (ts1 * 0.5) + vec3(3.9, -2.4, 0.0), uCrackSalt + 731);
                vec3 tc1 = evalFastCleavage3D(vec3(p0.x * ts1 + twx, p0.y * ts1 * 1.38, p0.z * ts1 + twz), 0.58, uCrackSalt + 733);

                float cellAct1 = 0.72 + 0.28 * (0.5 + 0.5 * tc1.y);
                float rPit1 = (0.43 * (0.62 + 0.38 * uTafoniHoneycomb) * cellAct1 * (0.55 + 0.45 * colonyGate)) / ts1;
                float tRib1 = 0.105 / ts1;
                float maxDepth1 = uScale * (0.020 + 0.028 * uTafoniHoneycomb) * colonyGate * cellAct1;
                float dBowl1 = smaxPoly(smaxPoly((tc1.z / ts1) - rPit1, tRib1 - (tc1.x / ts1), 0.055), (-curMacro) - maxDepth1, 0.055);
                float prevD = d;
                d = smaxPoly(d, -dBowl1, 0.048 + 0.025 * uTafoniHoneycomb);
                if (d > prevD + 0.002) {
                    chemStain = max(chemStain, clamp((d - prevD) * 14.0 * uTafoniHoneycomb, 0.0, 0.92));
                    crackFresh = max(crackFresh, 0.32 * uTafoniHoneycomb * colonyGate);
                }

                float ts2 = 9.2 / uScale;
                vec3 tc2 = evalFastCleavage3D(vec3(p0.x * ts2 - twz, p0.y * ts2 * 1.32, p0.z * ts2 + twx), 0.58, uCrackSalt + 751);
                float cellAct2 = smoothstep(-0.30, 0.25, tc2.y);
                if (cellAct2 > 0.02) {
                    float rPit2 = (0.40 * (0.55 + 0.45 * uTafoniHoneycomb) * cellAct2 * colonyGate) / ts2;
                    float tRib2 = 0.105 / ts2;
                    float maxDepth2 = uScale * (0.010 + 0.015 * uTafoniHoneycomb) * colonyGate * cellAct2;
                    float dBowl2 = smaxPoly(smaxPoly((tc2.z / ts2) - rPit2, tRib2 - (tc2.x / ts2), 0.032), (-curMacro) - maxDepth2, 0.032);
                    float prevD2 = d;
                    d = smaxPoly(d, -dBowl2, 0.028 + 0.015 * uTafoniHoneycomb);
                    if (d > prevD2 + 0.001) chemStain = max(chemStain, clamp((d - prevD2) * 18.0 * uTafoniHoneycomb, 0.0, 0.85));
                }
            }
        }

        if (uKarrenFlutes > 0.01 && extRindGate > 0.02 && curMacro > -uScale * 0.075) {
            float rainGate = smoothstep(-0.10, 0.36, bestN.y) * smoothstep(uScale * 0.16, uScale * 0.40, p.y) * extRindGate;
            if (rainGate > 0.01) {
                float kw = uScale * 0.018;
                float kx = p0.x + kw * scalarField3D(vec3(p0.x * (1.6 / uScale), p0.y * (0.40 / uScale), p0.z * (1.6 / uScale)), uCrackSalt + 769);
                float kz = p0.z + kw * scalarField3D(vec3(p0.x * (1.6 / uScale) + 4.7, p0.y * (0.40 / uScale) - 2.3, p0.z * (1.6 / uScale)), uCrackSalt + 773);

                float fk1 = 10.5 / uScale;
                float fk2 = 22.0 / uScale;
                float u1 = smoothstep(0.02, 0.66, abs(scalarField3D(vec3(kx * fk1, p0.y * fk1 * 0.05, kz * fk1), uCrackSalt + 787)));
                float u2 = smoothstep(0.02, 0.66, abs(scalarField3D(vec3(kx * fk2 + 2.9, p0.y * fk2 * 0.05, kz * fk2 - 1.7), uCrackSalt + 797)));

                float depthFade = smoothstep(-uScale * 0.065, 0.0, curMacro);
                float fluteRecess = uScale * (0.032 * u1 + 0.013 * u2) * uKarrenFlutes * rainGate * depthFade;
                d += fluteRecess;
                crackFresh = max(crackFresh, (1.0 - u1) * rainGate * uKarrenFlutes * 0.42);
                chemStain = max(chemStain, u1 * rainGate * uKarrenFlutes * 0.48);
            }

            float benchGate = smoothstep(0.75, 0.94, bestN.y) * smoothstep(uScale * 0.28, uScale * 0.48, p.y) * extRindGate;
            if (benchGate > 0.01) {
                float gs = 3.8 / uScale;
                vec3 gc = evalFastCleavage3D(vec3(p0.x * gs, p0.y * gs * 0.35, p0.z * gs), 0.58, uCrackSalt + 809);
                float rPan = (0.38 * (0.60 + 0.40 * uKarrenFlutes) * benchGate) / gs;
                float panMaxD = uScale * 0.022 * uKarrenFlutes * benchGate;
                float dPan = smaxPoly(smaxPoly((gc.z / gs) - rPan, (0.11 / gs) - (gc.x / gs), 0.04), (-curMacro) - panMaxD, 0.035);
                d = smaxPoly(d, -dPan, 0.032);
                if (-dPan > -0.01) chemStain = max(chemStain, 0.62 * uKarrenFlutes * benchGate);
            }
        }
    }

    float grain = scalarField3D(bestP0 * 6.5, uCrackSalt + 601);
    float bedBand = sin((bestP0.y * (5.4 / uScale) + 0.44 * scalarField3D(bestP0 * 0.6, uCrackSalt + 619)) * 6.2831853);
    float strataMod = 1.0 + bedBand * 0.14 * bestStrata + grain * 0.07;

    float chemAny = step(0.015, uChemicalDissolution + uTafoniHoneycomb + uKarrenFlutes);
    float weatherChan = mix(clamp(thermalStress, 0.0, 1.2), clamp(max(chemStain, thermalStress * 0.35), 0.0, 1.0), chemAny);

    outSdf = vec4(d, clamp(max(bestFresh, crackFresh), 0.0, 1.0), weatherChan, strataMod);
}
`;

/*==========================================================================================================================================
                     2. STAGE-2 SHADER: ULTRA-COMPACT 60 FPS 3D-SDF VOLUME RAYMARCHER (~120 LINES!)
==========================================================================================================================================*/

const RAYMARCH_FS = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler3D;

in vec2 vNdc;
out vec4 fragColor;

uniform sampler3D uSdfVol;
uniform vec3 uVolMin;
uniform vec3 uVolMax;

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

uniform float uScale;
uniform float uSkinDepth;
uniform float uChemActive;
uniform int uCutawayEnabled;
uniform int uCutawayAxis;
uniform float uCutawayOffset;

uniform vec3 uColRind;
uniform vec3 uColFresh;
uniform vec3 uColIron;
uniform vec3 uColLichen;

vec4 fetchVol(vec3 p) {
    vec3 uvw = clamp((p - uVolMin) / (uVolMax - uVolMin), 0.002, 0.998);
    return textureLod(uSdfVol, uvw, 0.0);
}

float mapDist(vec3 p) {
    float d = fetchVol(p).x;
    if (uCutawayEnabled != 0) {
        float axisVal = (uCutawayAxis == 0) ? p.x : p.z;
        d = max(d, axisVal - uCutawayOffset);
    }
    return d;
}

vec3 calcNormal(vec3 p) {
    const float e = 0.025;
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
    for (int i = 0; i < 4; i++) {
        float h = 0.06 + 0.16 * float(i * i);
        float d = mapDist(p + n * h);
        occ += max(0.0, h - d) * sca;
        sca *= 0.62;
    }
    return clamp(1.0 - 1.55 * occ, 0.12, 1.0);
}

float calcSoftShadow(vec3 ro, vec3 rd) {
    float res = 1.0;
    float t = 0.08;
    for (int i = 0; i < 24; i++) {
        vec3 p = ro + rd * t;
        if (p.y > uBoundsMax.y + 0.4) break;
        float h = mapDist(p);
        if (h < 0.004) return 0.18;
        res = min(res, 9.5 * h / t);
        t += clamp(h, 0.05, 0.40);
        if (t > 12.0) break;
    }
    return clamp(res, 0.18, 1.0);
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
    vec3 rd = normalize(uCamFwd + vNdc.x * uAspect * uTanHalfFov * uCamRight + vNdc.y * uTanHalfFov * uCamUp);
    vec3 ro = uCamPos;

    float skyT = clamp(rd.y * 0.5 + 0.5, 1e-4, 1.0);
    vec3 skyCol = mix(vec3(0.052, 0.056, 0.066), vec3(0.112, 0.122, 0.145), pow(skyT, 0.75));
    float sunGlow = pow(max(1e-4, dot(rd, uSunDir)), 24.0);
    skyCol += vec3(0.22, 0.18, 0.12) * sunGlow * 0.35;

    vec3 padMin = uBoundsMin - vec3(0.32, 0.04, 0.32);
    vec3 padMax = uBoundsMax + vec3(0.32, 0.32, 0.32);
    vec2 boxHit = intersectAABB(ro, rd, padMin, padMax);

    float tRock = -1.0;
    if (boxHit.y >= boxHit.x) {
        float t = max(0.02, boxHit.x);
        for (int s = 0; s < 96; s++) {
            if (t > boxHit.y) break;
            float d = mapDist(ro + rd * t);
            if (d < 0.004) {
                tRock = t;
                break;
            }
            t += max(0.018, d * 0.85);
        }
    }

    float tFloor = -1.0;
    if (rd.y < -1e-4) {
        float tf = -ro.y / rd.y;
        if (tf > 0.0 && (tRock < 0.0 || tf < tRock)) {
            vec3 fp = ro + rd * tf;
            if (length(fp.xz) < uScale * 1.65) tFloor = tf;
        }
    }

    vec3 col = skyCol;

    if (tFloor > 0.0) {
        vec3 fp = ro + rd * tFloor;
        float rDist = length(fp.xz);
        float discFade = 1.0 - smoothstep(uScale * 0.85, uScale * 1.62, rDist);

        vec2 g1 = abs(fract(fp.xz) - 0.5);
        float line1 = 1.0 - smoothstep(0.008, 0.024, min(g1.x, g1.y));
        float ring = 1.0 - smoothstep(0.010, 0.032, abs(fract(rDist / 2.0) - 0.5) * 2.0);

        float sh = calcSoftShadow(fp + vec3(0.0, 0.03, 0.0), uSunDir);
        float contactOcc = clamp(mapDist(fp + vec3(0.0, 0.20, 0.0)) / 0.34, 0.20, 1.0);

        vec3 floorBase = vec3(0.072, 0.075, 0.084) * (0.45 + 0.55 * sh) * contactOcc;
        floorBase += vec3(0.16, 0.15, 0.12) * (line1 * 0.22 + ring * 0.16) * discFade;
        col = mix(skyCol, floorBase, discFade);
    } else if (tRock > 0.0) {
        vec3 p = ro + rd * tRock;
        vec3 n = calcNormal(p);
        vec4 vSample = fetchVol(p);
        float dOrig = vSample.x;
        float freshMask = vSample.y;
        float thermalStress = vSample.z;
        float strataMod = vSample.w;

        bool isCutaway = false;
        if (uCutawayEnabled != 0) {
            float axisVal = (uCutawayAxis == 0) ? p.x : p.z;
            if ((axisVal - uCutawayOffset) > dOrig - 0.02) isCutaway = true;
        }

        float ao = calcSdfAO(p, n);
        float dProbeOut = mapDist(p + n * 0.08);
        float cav = clamp((0.08 - dProbeOut) * 11.0, 0.0, 1.0);
        float edg = clamp((dProbeOut - 0.076) * 16.0, 0.0, 1.0);

        vec3 albedo = uColRind;
        if (isCutaway) {
            float depth = max(0.0, -dOrig);
            float skinZone = exp(-depth / max(0.06, uSkinDepth));
            float ring = 0.5 + 0.5 * cos((depth / 0.15) * 6.2831853);
            float isoLine = smoothstep(0.90, 0.99, ring);
            albedo = mix(uColFresh * 0.85, uColIron * 1.15, skinZone * 0.58);
            albedo = mix(albedo, vec3(0.96, 0.87, 0.62), isoLine * 0.52);
            float seamBand = step(depth, 0.62) * smoothstep(0.92, 0.995, ring);
            albedo = mix(albedo, uColRind * 0.42, seamBand * 0.78);
        } else {
            albedo = mix(uColRind, uColFresh, freshMask) * strataMod;
            float liesegang = sin(clamp(freshMask, 0.0, 1.0) * 3.14159265);
            float chemStain = thermalStress * uChemActive;
            albedo = mix(albedo, uColIron, clamp(cav * 0.68 + chemStain * 0.65 + liesegang * 0.50, 0.0, 1.0) * 0.44);
            float upLichen = (1.0 - freshMask) * smoothstep(0.55, 0.92, n.y);
            albedo = mix(albedo, uColLichen, upLichen * 0.28);
            albedo *= (1.0 + edg * 0.18 - cav * 0.18);
        }

        if (uLensMode == 1) {
            albedo = mix(vec3(0.18, 0.24, 0.34), vec3(0.95, 0.64, 0.26), freshMask);
        } else if (uLensMode == 2) {
            albedo = heatmapRamp(thermalStress * 0.85 + freshMask * 0.15);
        } else if (uLensMode == 3) {
            albedo = vec3(0.84, 0.82, 0.78) * (1.0 - cav * 0.35);
        } else if (uLensMode == 4) {
            vec3 nCol = max(vec3(1e-4), n * 0.5 + 0.5);
            fragColor = vec4(pow(nCol * (0.55 + 0.45 * ao), vec3(1.0 / 2.2)), 1.0);
            return;
        }

        float sh = calcSoftShadow(p + n * 0.04, uSunDir);
        float ndotl = max(0.0, dot(n, uSunDir));
        vec3 fillDir = normalize(vec3(-uSunDir.x, 0.40, -uSunDir.z));
        float fill = max(0.0, dot(n, fillDir)) * 0.34;
        float hemi = 0.34 + 0.22 * (n.y * 0.5 + 0.5);

        vec3 halfV = normalize(uSunDir - rd);
        float spec = pow(max(1e-4, dot(n, halfV)), 28.0) * (0.06 + 0.08 * freshMask) * sh;

        col = albedo * (ndotl * sh * 1.48 + (hemi + fill) * ao) + vec3(1.0, 0.95, 0.86) * spec;
    }

    col = max(vec3(1e-5), acesTonemap(col * uExposure));
    col = pow(col, vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
`;

/*==========================================================================================================================================
                               3. WEBGL2 SHADERS: QEF DUAL-CONTOURED MESH + WIREFRAME OVERLAY
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
in vec4 vMasks;

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
        vec3 nCol = max(vec3(1e-4), (n * 0.5 + 0.5) * (0.55 + 0.45 * ao));
        fragColor = vec4(pow(nCol, vec3(1.0 / 2.2)), 1.0);
        return;
    }

    vec3 albedo = vAlbedo;
    if (uLensMode == 1) {
        albedo = mix(vec3(0.18, 0.24, 0.34), vec3(0.95, 0.64, 0.26), fresh);
    } else if (uLensMode == 2) {
        float sunHit = max(1e-4, dot(n, uSunDir));
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
    float spec = pow(max(1e-4, dot(n, halfV)), 28.0) * (0.06 + 0.08 * fresh);

    vec3 lit = albedo * (ndotl * (0.35 + 0.65 * ao) * 1.48 + (hemi + fill) * ao) + vec3(1.0, 0.95, 0.86) * spec * ao;
    vec3 col = pow(max(vec3(1e-5), acesTonemap(lit * uExposure)), vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
`;

/*==========================================================================================================================================
                                          4. MATRIX HELPERS & VIEWPORT CONTROLLER
==========================================================================================================================================*/

function compileShader(gl, type, src, label)
{
    const s = gl.createShader(type);
    gl.shaderSource(s, src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS))
    {
        const err = gl.getShaderInfoLog(s) || (gl.isContextLost() ? 'WebGL context lost' : 'Unknown shader error');
        gl.deleteShader(s);
        throw new Error(`FractureLab Shader Error (${label}): ${err}`);
    }
    return s;
}

function linkProgram(gl, vsSrc, fsSrc, label)
{
    const vs = compileShader(gl, gl.VERTEX_SHADER, vsSrc, `${label}:VS`);
    const fs = compileShader(gl, gl.FRAGMENT_SHADER, fsSrc, `${label}:FS`);
    const p = gl.createProgram();
    gl.attachShader(p, vs);
    gl.attachShader(p, fs);
    gl.linkProgram(p);
    gl.deleteShader(vs);
    gl.deleteShader(fs);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS))
    {
        const err = gl.getProgramInfoLog(p) || (gl.isContextLost() ? 'WebGL context lost during link' : `GL err ${gl.getError()}`);
        gl.deleteProgram(p);
        throw new Error(`FractureLab Link Error (${label}): ${err}`);
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

function mat4Mul(a, b)
{
    const out = new Float32Array(16);
    for (let c = 0; c < 4; c++)
    {
        for (let r = 0; r < 4; r++)
        {
            out[c * 4 + r] =
                a[0 * 4 + r] * b[c * 4 + 0] +
                a[1 * 4 + r] * b[c * 4 + 1] +
                a[2 * 4 + r] * b[c * 4 + 2] +
                a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
    return out;
}

const SDF_VOL_RES = 128; // 128^3 RGBA16F single 3D texture (~5.5 cm voxels, < 3 ms GPU bake)

export class FractureLabViewport
{
    constructor(canvas, callbacks = {})
    {
        this.canvas = canvas;
        this.callbacks = callbacks;
        const gl = canvas.getContext('webgl2', {
            antialias: false,
            alpha: false,
            depth: true,
            preserveDrawingBuffer: false,
            powerPreference: 'high-performance'
        });
        if (!gl) throw new Error('WebGL2 is required for the Pure-SDF Fracture Lab.');
        this.gl = gl;

        this.renderMode = 'raymarch';
        this.showWireframe = false;
        this.lensMode = 0;
        this.exposure = 1.15;

        this.yaw = 0.44;
        this.pitch = 0.24;
        this.distance = 11.2;
        this.target = [0.0, 2.75, 0.0];

        this.formation = null;
        this.volMin = [-5, -0.5, -5];
        this.volMax = [5, 8, 5];
        this.meshIndexCount = 0;
        this.wireIndexCount = 0;
        this.meshStats = null;
        this.gpuVolBakeSupported = false;

        this._initGlResources();
        this._bindPointerEvents();
    }

    _create3DVolumeTexture(res)
    {
        const gl = this.gl;
        const tex = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_3D, tex);
        gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_R, gl.CLAMP_TO_EDGE);
        gl.texImage3D(gl.TEXTURE_3D, 0, gl.RGBA16F, res, res, res, 0, gl.RGBA, gl.FLOAT, null);
        return tex;
    }

    _initGlResources()
    {
        const gl = this.gl;

        this.meshProg = linkProgram(gl, MESH_VS, MESH_FS, 'QefMesh');
        this.rayProg = linkProgram(gl, FULLSCREEN_VS, RAYMARCH_FS, 'VolumeRaymarch');
        this.emptyVao = gl.createVertexArray();

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

        this.volRes = SDF_VOL_RES;
        this.sdfVolTex = this._create3DVolumeTexture(this.volRes);

        try
        {
            this.bakeProg = linkProgram(gl, FULLSCREEN_VS, SDF_BAKE_FS, 'SdfSliceBake');
            const hasFloatFbo = !!gl.getExtension('EXT_color_buffer_float');
            this.bakeFbo = gl.createFramebuffer();
            if (hasFloatFbo)
            {
                gl.bindFramebuffer(gl.FRAMEBUFFER, this.bakeFbo);
                gl.framebufferTextureLayer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, this.sdfVolTex, 0, 0);
                this.gpuVolBakeSupported = (gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE);
                gl.bindFramebuffer(gl.FRAMEBUFFER, null);
            }
        }
        catch (err)
        {
            console.warn('[FractureLab] GPU slice baker unavailable, using CPU RGBA16F volume fallback:', err);
            this.gpuVolBakeSupported = false;
        }

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

    _bakeSdfVolumeGpu()
    {
        const gl = this.gl;
        const res = this.volRes;
        const { params, sunDir, crackSalt } = this.formation;

        gl.bindFramebuffer(gl.FRAMEBUFFER, this.bakeFbo);
        gl.viewport(0, 0, res, res);
        gl.disable(gl.DEPTH_TEST);
        gl.disable(gl.BLEND);

        gl.useProgram(this.bakeProg);
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, this.blockTex);
        gl.uniform1i(gl.getUniformLocation(this.bakeProg, 'uBlockTex'), 0);

        gl.activeTexture(gl.TEXTURE1);
        gl.bindTexture(gl.TEXTURE_2D, this.planeTex);
        gl.uniform1i(gl.getUniformLocation(this.bakeProg, 'uPlaneTex'), 1);

        gl.uniform1i(gl.getUniformLocation(this.bakeProg, 'uNumBlocks'), this.numBlocks || 0);
        gl.uniform1i(gl.getUniformLocation(this.bakeProg, 'uCrackSalt'), crackSalt | 0);
        gl.uniform3fv(gl.getUniformLocation(this.bakeProg, 'uVolMin'), this.volMin);
        gl.uniform3fv(gl.getUniformLocation(this.bakeProg, 'uVolMax'), this.volMax);

        gl.uniform1f(gl.getUniformLocation(this.bakeProg, 'uScale'), params.scale);
        gl.uniform3fv(gl.getUniformLocation(this.bakeProg, 'uSunDir'), sunDir);
        gl.uniform1f(gl.getUniformLocation(this.bakeProg, 'uJigsawRoughness'), params.jigsawRoughness ?? 0.55);
        gl.uniform1f(gl.getUniformLocation(this.bakeProg, 'uPlumoseStrength'), params.plumoseStrength ?? 0.60);
        gl.uniform1f(gl.getUniformLocation(this.bakeProg, 'uStrataContrast'), params.strataContrast ?? 1.0);
        gl.uniform1f(gl.getUniformLocation(this.bakeProg, 'uThermalNorm'), clamp((params.thermalDeltaT ?? 38) / 65.0, 0, 1.5));
        gl.uniform1f(gl.getUniformLocation(this.bakeProg, 'uDirectionalBias'), params.directionalBias ?? 0.55);
        gl.uniform1f(gl.getUniformLocation(this.bakeProg, 'uSkinDepth'), Math.max(0.06, params.thermalSkinDepth ?? 0.44));
        gl.uniform1f(gl.getUniformLocation(this.bakeProg, 'uCrazing90Strength'), params.crazing90Strength ?? 0.30);
        gl.uniform1f(gl.getUniformLocation(this.bakeProg, 'uCrazingSpacing'), Math.max(0.18, params.crazingSpacing ?? 0.62));
        gl.uniform1f(gl.getUniformLocation(this.bakeProg, 'uVoronoiStrength'), params.voronoiJointStrength ?? 0.36);
        gl.uniform1f(gl.getUniformLocation(this.bakeProg, 'uExfoliationStrength'), params.exfoliationStrength ?? 0.0);
        gl.uniform1f(gl.getUniformLocation(this.bakeProg, 'uChemicalDissolution'), params.chemicalDissolution ?? 0.0);
        gl.uniform1f(gl.getUniformLocation(this.bakeProg, 'uTafoniHoneycomb'), params.tafoniHoneycomb ?? 0.0);
        gl.uniform1f(gl.getUniformLocation(this.bakeProg, 'uKarrenFlutes'), params.karrenFlutes ?? 0.0);

        const uSliceLoc = gl.getUniformLocation(this.bakeProg, 'uSliceZNorm');
        gl.bindVertexArray(this.emptyVao);

        for (let z = 0; z < res; z++)
        {
            gl.framebufferTextureLayer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, this.sdfVolTex, 0, z);
            gl.uniform1f(uSliceLoc, (z + 0.5) / res);
            gl.drawArrays(gl.TRIANGLES, 0, 3);
        }

        gl.bindVertexArray(null);
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    }

    _bakeSdfVolumeCpuFallback()
    {
        const gl = this.gl;
        const res = 56;
        if (this.volRes !== res)
        {
            this.volRes = res;
            gl.deleteTexture(this.sdfVolTex);
            this.sdfVolTex = this._create3DVolumeTexture(res);
        }

        const buf = new Float32Array(res * res * res * 4);
        const info = {};
        const prevCut = this.formation.params.cutawayEnabled;
        this.formation.params.cutawayEnabled = false;

        let ptr = 0;
        for (let z = 0; z < res; z++)
        {
            const wz = this.volMin[2] + (this.volMax[2] - this.volMin[2]) * ((z + 0.5) / res);
            for (let y = 0; y < res; y++)
            {
                const wy = this.volMin[1] + (this.volMax[1] - this.volMin[1]) * ((y + 0.5) / res);
                for (let x = 0; x < res; x++)
                {
                    const wx = this.volMin[0] + (this.volMax[0] - this.volMin[0]) * ((x + 0.5) / res);
                    const d = evalFracturedFormationSdf(this.formation, wx, wy, wz, null, info);
                    buf[ptr] = d;
                    buf[ptr + 1] = info.freshMask ?? 0;
                    buf[ptr + 2] = Math.max(info.chemStain ?? 0, (info.thermalStress ?? 0.5) * 0.35);
                    buf[ptr + 3] = 1.0;
                    ptr += 4;
                }
            }
        }
        this.formation.params.cutawayEnabled = prevCut;

        gl.bindTexture(gl.TEXTURE_3D, this.sdfVolTex);
        gl.texImage3D(gl.TEXTURE_3D, 0, gl.RGBA16F, res, res, res, 0, gl.RGBA, gl.FLOAT, buf);
    }

    setFormation(formation, structuralChanged = true, cutawayOnly = false)
    {
        this.formation = formation;
        if (!formation || cutawayOnly) return;

        const gl = this.gl;
        if (structuralChanged)
        {
            const b = formation.bounds;
            const pad = 0.50;
            this.volMin = [b.minX - pad, b.minY - 0.18, b.minZ - pad];
            this.volMax = [b.maxX + pad, b.maxY + pad, b.maxZ + pad];

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

        if (this.gpuVolBakeSupported)
        {
            this._bakeSdfVolumeGpu();
        }
        else
        {
            this._bakeSdfVolumeCpuFallback();
        }
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
        this.distance = span * 1.38;
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
                this.target[0] -= cy * dx * scale;
                this.target[2] += sy * dx * scale;
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

        let rx = -fz, ry = 0, rz = fx;
        const rl = Math.hypot(rx, ry, rz) || 1;
        rx /= rl; rz /= rl;

        const ux = ry * fz - rz * fy;
        const uy = rz * fx - rx * fz;
        const uz = rx * fy - ry * fx;

        return { eye, fwd: [fx, fy, fz], right: [rx, ry, rz], up: [ux, uy, uz] };
    }

    render()
    {
        if (!this.formation) return;
        const gl = this.gl;
        if (gl.isContextLost()) return;

        const dpr = Math.min(window.devicePixelRatio || 1, 1.65);
        const cw = Math.max(1, Math.floor(this.canvas.clientWidth * dpr));
        const ch = Math.max(1, Math.floor(this.canvas.clientHeight * dpr));
        if (this.canvas.width !== cw || this.canvas.height !== ch)
        {
            this.canvas.width = cw;
            this.canvas.height = ch;
        }
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(0, 0, cw, ch);

        const aspect = cw / ch;
        const fovRad = 42 * DEG2RAD;
        const cam = this._computeCameraBasis();
        const { params, bounds, sunDir, theme } = this.formation;

        if (this.renderMode === 'raymarch' || this.meshIndexCount === 0)
        {
            gl.disable(gl.DEPTH_TEST);
            gl.disable(gl.BLEND);
            gl.useProgram(this.rayProg);

            gl.activeTexture(gl.TEXTURE0);
            gl.bindTexture(gl.TEXTURE_3D, this.sdfVolTex);
            gl.uniform1i(gl.getUniformLocation(this.rayProg, 'uSdfVol'), 0);

            gl.uniform3fv(gl.getUniformLocation(this.rayProg, 'uVolMin'), this.volMin);
            gl.uniform3fv(gl.getUniformLocation(this.rayProg, 'uVolMax'), this.volMax);

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

            const chemSum = (params.chemicalDissolution ?? 0) + (params.tafoniHoneycomb ?? 0) + (params.karrenFlutes ?? 0);
            gl.uniform1f(gl.getUniformLocation(this.rayProg, 'uScale'), params.scale);
            gl.uniform1f(gl.getUniformLocation(this.rayProg, 'uSkinDepth'), Math.max(0.06, params.thermalSkinDepth ?? 0.44));
            gl.uniform1f(gl.getUniformLocation(this.rayProg, 'uChemActive'), chemSum > 0.015 ? 1.0 : 0.0);
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
