/*==========================================================================================================================================
                                                   ROCKFORMATION2VIEWPORT.JS
==========================================================================================================================================*/
// Dual-Mode WebGL2 3D Viewport for RockFormation 2.0 (Composable Pure-SDF Plain Rock Layer Stack):
//   1. `ROCK2_BAKE_FS`: Evaluates the ordered Layer Stack (`d`, `r`, `g`, `b`) into a single `112^3` `RGBA16F`
//      3D texture (`uSdfVol`, single color attachment, zero noise loops -> < 1.5 ms GPU bake & instant HLSL compile).
//   2. `ROCK2_RAYMARCH_FS`: Sculptural Plain-Stone Studio Raymarcher with soft SDF shadows, volumetric 3D SDF AO,
//      studio reference grid, and 4 form-study lenses (Stone Tone Studio, Uniform Clay Maquette, 3D SDF AO, Normals).
//   3. `ROCK2_MESH_VS` / `ROCK2_MESH_FS`: 4^3 Sparse-Brick QEF Dual-Contour Triangle Mesh + Wireframe Overlay.

import { clamp, DEG2RAD } from './Core/SpatialNoise3D.js';
import {
    evalRock2StackSdf,
    pickRock2LayerAtRay,
    packRock2StackGpuBuffers,
    buildRock2EulerMatrix3,
    mulRock2Mat3,
    forwardRock2Point,
    inverseRock2Vector,
    buildRock2SplineArcTable,
    evaluateRock2GeneratorInstances,
    getOrCreateRock2InstanceOverride,
    normalizeSplineKnots
} from './RockFormationStackEngine.js';

const FULLSCREEN_VS = `#version 300 es
precision highp float;
out vec2 vNdc;
void main() {
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
    vNdc = p;
    gl_Position = vec4(p, 0.0, 1.0);
}
`;

const ROCK2_BAKE_FS = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

in vec2 vNdc;
out vec4 outSdf; // (d, colR, colG, colB)

uniform sampler2D uLayerTex;
uniform sampler2D uBlockTex;
uniform sampler2D uPlaneTex;
uniform int uNumLayers;
uniform vec3 uVolMin;
uniform vec3 uVolMax;
uniform float uSliceZNorm;

float sminPoly(float a, float b, float k) {
    if (k <= 1e-5) return min(a, b);
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * 0.25;
}

float smaxPoly(float a, float b, float k) {
    if (k <= 1e-5) return max(a, b);
    float h = max(k - abs(a - b), 0.0) / k;
    return max(a, b) + h * h * k * 0.25;
}

float chamferMin(float a, float b, float r) {
    float m = min(a, b);
    if (r <= 1e-5) return m;
    return min(m, (a + b - r) * 0.70710678);
}

float evalPlainPrimitive(int bi, vec3 p) {
    vec4 m1 = texelFetch(uBlockTex, ivec2(1, bi), 0);
    int pStart = int(m1.x + 0.5);
    int pCount = int(m1.y + 0.5);
    float kLse = m1.z;
    float baseBevel = m1.w;

    vec4 r0 = texelFetch(uBlockTex, ivec2(2, bi), 0);
    vec4 r1 = texelFetch(uBlockTex, ivec2(3, bi), 0);
    vec4 r2 = texelFetch(uBlockTex, ivec2(4, bi), 0);

    vec3 q = vec3(dot(r0.xyz, p) + r0.w, dot(r1.xyz, p) + r1.w, dot(r2.xyz, p) + r2.w);

    vec4 m9 = texelFetch(uBlockTex, ivec2(9, bi), 0);
    float sharpBase = m9.x;
    float flatBaseCut = m9.y;
    bool hasDeform = m9.z > 0.5;
    float deformPenalty = m9.w;

    float scaleFactor = 1.0;
    if (hasDeform) {
        vec4 d0 = texelFetch(uBlockTex, ivec2(7, bi), 0); // (baseY, refH, taper, pinch)
        vec4 d1 = texelFetch(uBlockTex, ivec2(8, bi), 0); // (twistRad, bendX, bendZ, skewX)
        vec4 d2 = texelFetch(uBlockTex, ivec2(10, bi), 0); // (ringFreq, ringAmp, ringPhase, pinchHeightNorm)

        float h = clamp((q.y - d0.x) / max(0.15, d0.y), -0.25, 1.35);
        float u = h - 0.5;
        float h2 = max(0.0, h);
        float x1 = q.x - d1.y * h2 * h2 * d0.y - d1.w * u * d0.y;
        float z1 = q.z - d1.z * h2 * h2 * d0.y;

        if (abs(d1.x) > 1e-5) {
            float th = d1.x * u;
            float ct = cos(th), st = sin(th);
            float rx = x1 * ct + z1 * st;
            float rz = -x1 * st + z1 * ct;
            x1 = rx;
            z1 = rz;
        }

        float sTap = max(0.16, 1.0 - d0.z * (h - 0.35));
        float pRel = (h - d2.w) / 0.22;
        float sPinch = 1.0 - d0.w * exp(-pRel * pRel);
        float sRing = 1.0;
        if (d2.y > 1e-4 && d2.x > 0.05) {
            float hc = clamp(h, 0.0, 1.0);
            sRing = 1.0 + d2.y * sin(hc * d2.x * 6.2831853 + d2.z);
        }
        float s = clamp(sTap * sPinch * sRing, 0.14, 2.50);
        q.x = x1 / s;
        q.z = z1 / s;
        scaleFactor = min(1.0, s) * deformPenalty;
    }

    float dPoly = 1e9;
    if (pCount > 0) {
        float invKLse = 1.0 / max(1e-4, kLse);
        float dFloor = -1e9;
        float bestPd = -1e9;
        float sumExp = 0.0;

        for (int j = 0; j < 40; j++) {
            if (j >= pCount) break;
            vec4 pl = texelFetch(uPlaneTex, ivec2(0, pStart + j), 0);
            float pd = dot(pl.xyz, q) - pl.w;

            if (sharpBase > 0.5 && pl.y <= -0.92) {
                dFloor = max(dFloor, pd);
            } else if (pd > bestPd) {
                sumExp = bestPd > -1e8 ? sumExp * exp(max(-24.0, (bestPd - pd) * invKLse)) + 1.0 : 1.0;
                bestPd = pd;
            } else {
                sumExp += exp(max(-24.0, (pd - bestPd) * invKLse));
            }
        }

        dPoly = bestPd > -1e8 ? (bestPd + kLse * log(max(1.0, sumExp))) : dFloor;
        if (dFloor > -1e8 && bestPd > -1e8) {
            dPoly = smaxPoly(dPoly, dFloor, baseBevel);
        }
    }

    vec4 m6 = texelFetch(uBlockTex, ivec2(6, bi), 0); // (sqWeight, sqExpH, sqExpV, primOp)
    float sqWeight = m6.x;

    float dSq = 1e9;
    if (sqWeight > 0.001 || pCount == 0) {
        vec4 sq = texelFetch(uBlockTex, ivec2(5, bi), 0); // (sqRx, sqRy, sqRz, sqCy)
        float p1 = 2.0 / max(0.22, m6.y);
        float p2 = 2.0 / max(0.22, m6.z);
        float m12 = p2 / p1;

        float ux = max(1e-5, abs(q.x) / max(0.04, sq.x) + 1e-5);
        float uy = max(1e-5, abs(q.y - sq.w) / max(0.04, sq.y) + 1e-5);
        float uz = max(1e-5, abs(q.z) / max(0.04, sq.z) + 1e-5);

        float A = max(1e-6, pow(ux, p1) + pow(uz, p1));
        float B = max(1e-6, pow(A, m12) + pow(uy, p2));

        if (B < 1e-5) {
            dSq = -min(sq.x, min(sq.y, sq.z)) * 0.85;
        } else {
            float F = pow(B, 1.0 / p2) - 1.0;
            float bTerm = pow(B, 1.0 / p2 - 1.0);
            float aTerm = pow(A, m12 - 1.0);
            float gx = (bTerm * aTerm * pow(ux, p1 - 1.0)) / sq.x;
            float gy = (bTerm * pow(uy, p2 - 1.0)) / sq.y;
            float gz = (bTerm * aTerm * pow(uz, p1 - 1.0)) / sq.z;
            float gradLen = max(1e-4, length(vec3(gx, gy, gz)));
            dSq = (F / gradLen) * 0.92;
        }

        if (flatBaseCut > 0.02) {
            dSq = smaxPoly(dSq, -q.y, baseBevel);
        }
    }

    float dLocal = (pCount == 0) ? dSq : ((sqWeight <= 0.001) ? dPoly : mix(dPoly, dSq, sqWeight));
    return dLocal * scaleFactor;
}

void main() {
    vec2 uv = vNdc * 0.5 + 0.5;
    vec3 p = mix(uVolMin, uVolMax, vec3(uv.x, uv.y, uSliceZNorm));

    if (uNumLayers <= 0) {
        outSdf = vec4(10.0, 0.58, 0.57, 0.55);
        return;
    }

    float dAcc = 1e9;
    vec3 colAcc = vec3(0.58, 0.57, 0.55);
    bool hasBase = false;

    for (int li = 0; li < 28; li++) {
        if (li >= uNumLayers) break;
        vec4 lm0 = texelFetch(uLayerTex, ivec2(0, li), 0); // (bStart, bCount, combineOp, blendRadius)
        vec4 lm1 = texelFetch(uLayerTex, ivec2(1, li), 0); // (colR, colG, colB, intraBlend)

        int bStart = int(lm0.x + 0.5);
        int bCount = int(lm0.y + 0.5);
        int combineOp = int(lm0.z + 0.5);
        float blendR = lm0.w;
        vec3 lyrCol = lm1.rgb;
        float intraBlend = lm1.w;

        float dLyr = 1e9;
        float dInst = 1e9;
        bool hasInst = false;
        for (int k = 0; k < 256; k++) {
            if (k >= bCount) break;
            int bi = bStart + k;
            vec4 sph = texelFetch(uBlockTex, ivec2(0, bi), 0);
            float pOp = texelFetch(uBlockTex, ivec2(6, bi), 0).w;
            float lb = length(p - sph.xyz) - sph.w;
            float lbCons = lb > 0.0 ? lb * 0.25 : lb;

            if (pOp > -1.5 && pOp < -0.5) {
                if (hasInst) {
                    dLyr = (dLyr > 1e8) ? dInst : (intraBlend > 1e-4 ? sminPoly(dLyr, dInst, intraBlend) : min(dLyr, dInst));
                }
                hasInst = true;
                if (lbCons > dLyr + intraBlend + 0.25) {
                    dInst = lbCons;
                } else {
                    dInst = evalPlainPrimitive(bi, p);
                }
            } else if (pOp >= 0.0) {
                float subB = pOp;
                if (lbCons > min(dInst, dLyr + intraBlend) + subB + 0.25) continue;
                float dP = evalPlainPrimitive(bi, p);
                dInst = (dInst > 1e8) ? dP : (subB > 1e-4 ? sminPoly(dInst, dP, subB) : min(dInst, dP));
                hasInst = true;
            } else {
                float cutB = max(0.0, -pOp - 2.0);
                if (!hasInst || dInst > dLyr + intraBlend + 0.25 || lbCons > max(0.0, -dInst) + cutB + 0.25) continue;
                float dP = evalPlainPrimitive(bi, p);
                dInst = smaxPoly(dInst, -dP, cutB);
            }
        }
        if (hasInst) {
            dLyr = (dLyr > 1e8) ? dInst : (intraBlend > 1e-4 ? sminPoly(dLyr, dInst, intraBlend) : min(dLyr, dInst));
        }

        if (!hasBase) {
            if (combineOp == 3) continue;
            dAcc = dLyr;
            colAcc = lyrCol;
            hasBase = true;
            continue;
        }

        if (combineOp == 0) {
            if (dLyr < dAcc) {
                dAcc = dLyr;
                colAcc = lyrCol;
            }
        } else if (combineOp == 1) {
            float w = blendR > 1e-4 ? clamp(0.5 + 0.5 * (dAcc - dLyr) / blendR, 0.0, 1.0) : (dLyr < dAcc ? 1.0 : 0.0);
            dAcc = sminPoly(dAcc, dLyr, blendR);
            colAcc = mix(colAcc, lyrCol, w);
        } else if (combineOp == 2) {
            float w = blendR > 1e-4 ? clamp(0.5 + 0.5 * (dAcc - dLyr) / blendR, 0.0, 1.0) : (dLyr < dAcc ? 1.0 : 0.0);
            dAcc = chamferMin(dAcc, dLyr, blendR);
            colAcc = mix(colAcc, lyrCol, w);
        } else if (combineOp == 3) {
            float prev = dAcc;
            dAcc = smaxPoly(dAcc, -dLyr, blendR);
            float cutW = smoothstep(0.0, max(0.02, blendR + 0.04), dAcc - prev);
            colAcc = mix(colAcc, lyrCol, cutW * 0.65);
        } else if (combineOp == 4) {
            float w = dLyr > dAcc ? 1.0 : 0.0;
            dAcc = smaxPoly(dAcc, dLyr, blendR);
            colAcc = mix(colAcc, lyrCol, w * 0.5);
        }
    }

    outSdf = vec4(dAcc, colAcc);
}
`;

const ROCK2_RAYMARCH_FS = `#version 300 es
precision highp float;
precision highp sampler3D;

in vec2 vNdc;
out vec4 outColor;

uniform sampler3D uSdfVol;
uniform vec3 uVolMin;
uniform vec3 uVolMax;

uniform vec3 uCamPos;
uniform vec3 uCamFwd;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform vec2 uResolution;
uniform vec3 uSunDir;
uniform float uExposure;
uniform int uLens;

vec3 acesk(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec2 intersectAabb(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax) {
    vec3 invD = 1.0 / rd;
    vec3 t0s = (bmin - ro) * invD;
    vec3 t1s = (bmax - ro) * invD;
    vec3 tsmaller = min(t0s, t1s);
    vec3 tbigger  = max(t0s, t1s);
    float tmin = max(max(tsmaller.x, tsmaller.y), tsmaller.z);
    float tmax = min(min(tbigger.x, tbigger.y), tbigger.z);
    return vec2(max(0.0, tmin), tmax);
}

vec4 sampleVol(vec3 p) {
    vec3 uvw = (p - uVolMin) / (uVolMax - uVolMin);
    vec3 clamped = clamp(uvw, vec3(0.004), vec3(0.996));
    vec4 v = textureLod(uSdfVol, clamped, 0.0);
    vec3 over = max(vec3(0.0), max(uVolMin - p, p - uVolMax));
    return vec4(v.x + length(over), v.yzw);
}

float evalD(vec3 p) {
    return sampleVol(p).x;
}

vec3 calcNormal(vec3 p, float eps) {
    vec2 e = vec2(1.0, -1.0) * eps;
    return normalize(
        e.xyy * evalD(p + e.xyy) +
        e.yyx * evalD(p + e.yyx) +
        e.yxy * evalD(p + e.yxy) +
        e.xxx * evalD(p + e.xxx)
    );
}

float calcSoftShadow(vec3 ro, vec3 rd) {
    vec2 sb = intersectAabb(ro, rd, uVolMin, uVolMax);
    if (sb.y <= sb.x) return 1.0;
    float res = 1.0;
    float t = max(0.05, sb.x + 0.02);
    float tMax = min(sb.y, 28.0);
    for (int i = 0; i < 32; i++) {
        if (t > tMax) break;
        vec3 sp = ro + rd * t;
        float h = evalD(sp);
        if (h < 0.003) return 0.14;
        res = min(res, 8.0 * h / max(0.06, t));
        t += clamp(h * 0.85, 0.03, 0.45);
    }
    return clamp(res, 0.14, 1.0);
}

float calcVolAo(vec3 p, vec3 n) {
    float occ = 0.0;
    float w = 1.0;
    for (int i = 1; i <= 4; i++) {
        float dist = 0.08 * float(i) * float(i);
        float d = evalD(p + n * dist);
        occ += max(0.0, dist - d) * w;
        w *= 0.55;
    }
    return clamp(1.0 - 1.45 * occ, 0.16, 1.0);
}

vec3 renderSky(vec3 rd) {
    float h = clamp(rd.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 bot = vec3(0.055, 0.060, 0.072);
    vec3 top = vec3(0.115, 0.130, 0.155);
    float sunGlow = pow(max(0.0, dot(rd, uSunDir)), 18.0) * 0.08;
    return mix(bot, top, smoothstep(0.0, 1.0, h)) + vec3(1.0, 0.92, 0.80) * sunGlow;
}

void main() {
    vec2 aspect = vec2(uResolution.x / max(1.0, uResolution.y), 1.0);
    vec2 screen = vNdc * aspect * 0.44;
    vec3 ro = uCamPos;
    vec3 rd = normalize(uCamFwd + screen.x * uCamRight + screen.y * uCamUp);

    vec3 col = renderSky(rd);

    float tFloor = (abs(rd.y) > 1e-4) ? (-ro.y / rd.y) : -1.0;
    vec2 box = intersectAabb(ro, rd, uVolMin, uVolMax);
    bool hitRock = false;
    float tHit = 1e9;

    if (box.y > box.x) {
        float t = box.x;
        float voxelDiag = length(uVolMax - uVolMin) / 112.0;
        for (int i = 0; i < 120; i++) {
            vec3 p = ro + rd * t;
            float d = evalD(p);
            if (d < voxelDiag * 0.18) {
                hitRock = true;
                tHit = t;
                break;
            }
            t += max(voxelDiag * 0.25, d * 0.80);
            if (t > box.y) break;
        }
    }

    if (hitRock && (tFloor <= 0.0 || tHit < tFloor)) {
        vec3 p = ro + rd * tHit;
        float voxelSize = length(uVolMax - uVolMin) / 112.0;
        vec3 n = calcNormal(p, voxelSize * 0.45);
        vec4 s = sampleVol(p);
        float ao = calcVolAo(p, n);
        float shadow = calcSoftShadow(p + n * voxelSize * 0.65, uSunDir);

        // Subtle convexity bevel highlight from central difference Laplacian
        float dLap = evalD(p + n * voxelSize * 0.8) + evalD(p - n * voxelSize * 0.8) - 2.0 * s.x;
        float edgeWear = clamp(dLap * 14.0, 0.0, 1.0);

        if (uLens == 2) {
            col = vec3(ao);
        } else if (uLens == 3) {
            col = n * 0.5 + 0.5;
        } else {
            vec3 baseAlbedo = (uLens == 1) ? vec3(0.60, 0.58, 0.54) : s.yzw;
            baseAlbedo = mix(baseAlbedo, baseAlbedo * 1.14 + vec3(0.03), edgeWear * 0.45);

            float ndl = max(0.0, dot(n, uSunDir));
            vec3 halfV = normalize(uSunDir - rd);
            float spec = pow(max(0.0, dot(n, halfV)), 28.0) * 0.12;
            float rim = pow(1.0 - max(0.0, dot(n, -rd)), 3.2) * 0.12;
            vec3 hemi = mix(vec3(0.14, 0.15, 0.18), vec3(0.32, 0.34, 0.38), n.y * 0.5 + 0.5);

            vec3 lighting = hemi * ao + vec3(1.05, 0.99, 0.92) * ndl * shadow + vec3(0.75, 0.82, 0.92) * rim * ao;
            col = baseAlbedo * lighting + vec3(spec * shadow);
        }
    } else if (tFloor > 0.0 && tFloor < 90.0) {
        vec3 fp = ro + rd * tFloor;
        float fade = exp(-length(fp.xz) * 0.055);
        vec2 g1 = abs(fract(fp.xz - 0.5) - 0.5) / max(fwidth(fp.xz), vec2(1e-4));
        float line1 = 1.0 - min(min(g1.x, g1.y), 1.0);
        vec2 g5 = abs(fract(fp.xz * 0.2 - 0.5) - 0.5) / max(fwidth(fp.xz * 0.2), vec2(1e-4));
        float line5 = 1.0 - min(min(g5.x, g5.y), 1.0);

        vec3 floorCol = vec3(0.068, 0.074, 0.086);
        floorCol = mix(floorCol, vec3(0.12, 0.13, 0.15), line1 * 0.42 * fade);
        floorCol = mix(floorCol, vec3(0.20, 0.22, 0.25), line5 * 0.65 * fade);

        // Soft floor contact shadow from the 3D SDF volume (supports elevated floating sky islands cleanly)!
        float fShadow = calcSoftShadow(fp + vec3(0.0, 0.04, 0.0), uSunDir);
        float contactAo = 1.0;
        if (uVolMin.y <= 0.25 && all(greaterThan(fp.xz, uVolMin.xz - vec2(1.0))) && all(lessThan(fp.xz, uVolMax.xz + vec2(1.0)))) {
            float dGround = evalD(fp + vec3(0.0, 0.12, 0.0));
            contactAo = clamp(0.45 + 0.55 * smoothstep(0.0, 0.85, dGround), 0.35, 1.0);
        }
        floorCol *= (0.45 + 0.55 * fShadow) * contactAo;
        col = mix(col, floorCol, fade);
    }

    col = acesk(col * uExposure);
    col = pow(col, vec3(1.0 / 2.2));
    outColor = vec4(col, 1.0);
}
`;

const ROCK2_MESH_VS = `#version 300 es
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

const ROCK2_MESH_FS = `#version 300 es
precision highp float;

in vec3 vPos;
in vec3 vNorm;
in vec3 vAlbedo;
in vec4 vMasks;
out vec4 outColor;

uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform float uExposure;
uniform int uLens;
uniform int uWirePass;

vec3 acesk(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    if (uWirePass == 1) {
        outColor = vec4(0.94, 0.96, 0.82, 0.42);
        return;
    }

    vec3 n = normalize(vNorm);
    float ao = vMasks.x;

    vec3 col;
    if (uLens == 2) {
        col = vec3(ao);
    } else if (uLens == 3) {
        col = n * 0.5 + 0.5;
    } else {
        vec3 albedo = (uLens == 1) ? vec3(0.60, 0.58, 0.54) : vAlbedo;
        vec3 V = normalize(uCamPos - vPos);
        float ndl = max(0.0, dot(n, uSunDir));
        vec3 H = normalize(uSunDir + V);
        float spec = pow(max(0.0, dot(n, H)), 28.0) * 0.12;
        float rim = pow(1.0 - max(0.0, dot(n, V)), 3.2) * 0.12;
        vec3 hemi = mix(vec3(0.15, 0.16, 0.19), vec3(0.34, 0.36, 0.40), n.y * 0.5 + 0.5);
        col = albedo * (hemi * ao + vec3(1.02, 0.97, 0.90) * ndl + vec3(0.75, 0.82, 0.92) * rim * ao) + vec3(spec);
    }

    col = acesk(col * uExposure);
    col = pow(col, vec3(1.0 / 2.2));
    outColor = vec4(col, 1.0);
}
`;

/*==========================================================================================================================================
                                         ROCKFORMATION2VIEWPORT CONTROLLER CLASS
==========================================================================================================================================*/

export class RockFormation2Viewport
{
    constructor(canvas, overlayCanvas = null, callbacks = {})
    {
        this.canvas = canvas;
        this.overlayCanvas = overlayCanvas || (typeof document !== 'undefined' ? document.getElementById('OverlayRock2') : null);
        this.ctx2d = this.overlayCanvas ? this.overlayCanvas.getContext('2d') : null;
        this.callbacks = callbacks || {};

        this.gl = canvas.getContext('webgl2', { antialias: true, alpha: false, depth: true });
        if (!this.gl)
        {
            console.warn('[RockFormation 2.0] WebGL2 context unavailable.');
            return;
        }

        this.compiledStack = null;
        this.activeLayer = null;
        this.showGizmos = true;
        this.gizmoToolMode = 'path'; // 'path' | 'translate' | 'rotate' | 'scale'
        this.selectedInstanceIndex = -1; // -1 = Whole Layer; >= 0 = Individual Rock #k inside multi-rock layer
        this.instancePickLock = false;
        this.selectedKnotIndex = 0;
        this._screenHandles = [];
        this._screenSplineSegs = [];
        this._screenRotateSegs = [];
        this._hoveredHandle = null;
        this._hoveredCurveInsert = null;
        this._gizmoDrag = null;

        this.renderMode = 'raymarch'; // 'raymarch' | 'qef-mesh'
        this.showWireframe = false;
        this.lens = 0;
        this.exposure = 1.12;

        this.yaw = 0.52;
        this.pitch = 0.30;
        this.distance = 11.5;
        this.target = [0, 2.2, 0];

        this.volRes = 112;
        this.volMin = new Float32Array([-4, 0, -4]);
        this.volMax = new Float32Array([4, 6, 4]);
        this.numLayers = 0;

        this.meshIndexCount = 0;
        this.wireIndexCount = 0;
        this.meshStats = null;

        this._initGlResources();
        this._bindPointerEvents();
    }

    _compileShader(type, src)
    {
        const gl = this.gl;
        const sh = gl.createShader(type);
        gl.shaderSource(sh, src);
        gl.compileShader(sh);
        if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS))
        {
            console.error('[RockFormation 2.0 Shader Error]', gl.getShaderInfoLog(sh));
            gl.deleteShader(sh);
            return null;
        }
        return sh;
    }

    _linkProgram(vsSrc, fsSrc)
    {
        const gl = this.gl;
        const vs = this._compileShader(gl.VERTEX_SHADER, vsSrc);
        const fs = this._compileShader(gl.FRAGMENT_SHADER, fsSrc);
        if (!vs || !fs) return null;
        const prog = gl.createProgram();
        gl.attachShader(prog, vs);
        gl.attachShader(prog, fs);
        gl.linkProgram(prog);
        gl.deleteShader(vs);
        gl.deleteShader(fs);
        if (!gl.getProgramParameter(prog, gl.LINK_STATUS))
        {
            console.error('[RockFormation 2.0 Link Error]', gl.getProgramInfoLog(prog));
            return null;
        }
        return prog;
    }

    _create2DDataTex()
    {
        const gl = this.gl;
        const tex = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, tex);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        return tex;
    }

    _create3DVolumeTex(res)
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
        this.extColorBufferFloat = gl.getExtension('EXT_color_buffer_float');
        gl.getExtension('OES_texture_float_linear');

        this.bakeProg = this._linkProgram(FULLSCREEN_VS, ROCK2_BAKE_FS);
        this.rayProg = this._linkProgram(FULLSCREEN_VS, ROCK2_RAYMARCH_FS);
        this.meshProg = this._linkProgram(ROCK2_MESH_VS, ROCK2_MESH_FS);

        this.emptyVao = gl.createVertexArray();
        this.layerTex = this._create2DDataTex();
        this.blockTex = this._create2DDataTex();
        this.planeTex = this._create2DDataTex();

        this.sdfVolTex = this._create3DVolumeTex(this.volRes);
        this.bakeFbo = gl.createFramebuffer();
        this.gpuVolBakeSupported = false;

        if (this.extColorBufferFloat)
        {
            gl.bindFramebuffer(gl.FRAMEBUFFER, this.bakeFbo);
            gl.framebufferTextureLayer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, this.sdfVolTex, 0, 0);
            this.gpuVolBakeSupported = (gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE);
            gl.bindFramebuffer(gl.FRAMEBUFFER, null);
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

    setCompiledStack(compiledStack, options = {})
    {
        if (!this.gl || !compiledStack) return;
        const gl = this.gl;
        this.compiledStack = compiledStack;

        const b = compiledStack.bounds;
        this.volMin[0] = b.minX; this.volMin[1] = b.minY; this.volMin[2] = b.minZ;
        this.volMax[0] = b.maxX; this.volMax[1] = b.maxY; this.volMax[2] = b.maxZ;

        const packed = packRock2StackGpuBuffers(compiledStack);
        this.numLayers = packed.numLayers;

        gl.bindTexture(gl.TEXTURE_2D, this.layerTex);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, packed.layerTexWidth, packed.layerTexHeight, 0, gl.RGBA, gl.FLOAT, packed.layerTexData);

        gl.bindTexture(gl.TEXTURE_2D, this.blockTex);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, packed.blockTexWidth, packed.blockTexHeight, 0, gl.RGBA, gl.FLOAT, packed.blockTexData);

        gl.bindTexture(gl.TEXTURE_2D, this.planeTex);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, packed.planeTexWidth, packed.planeTexHeight, 0, gl.RGBA, gl.FLOAT, packed.planeTexData);

        if (this.gpuVolBakeSupported)
        {
            this._bakeVolumeGpu();
        }
        else
        {
            this._bakeVolumeCpuFallback();
        }

        if (options.reframe)
        {
            this.frameStack();
        }
    }

    _bakeVolumeGpu()
    {
        const gl = this.gl;
        const res = this.volRes;

        gl.bindFramebuffer(gl.FRAMEBUFFER, this.bakeFbo);
        gl.viewport(0, 0, res, res);
        gl.disable(gl.DEPTH_TEST);
        gl.disable(gl.BLEND);

        gl.useProgram(this.bakeProg);
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, this.layerTex);
        gl.uniform1i(gl.getUniformLocation(this.bakeProg, 'uLayerTex'), 0);

        gl.activeTexture(gl.TEXTURE1);
        gl.bindTexture(gl.TEXTURE_2D, this.blockTex);
        gl.uniform1i(gl.getUniformLocation(this.bakeProg, 'uBlockTex'), 1);

        gl.activeTexture(gl.TEXTURE2);
        gl.bindTexture(gl.TEXTURE_2D, this.planeTex);
        gl.uniform1i(gl.getUniformLocation(this.bakeProg, 'uPlaneTex'), 2);

        gl.uniform1i(gl.getUniformLocation(this.bakeProg, 'uNumLayers'), this.numLayers);
        gl.uniform3fv(gl.getUniformLocation(this.bakeProg, 'uVolMin'), this.volMin);
        gl.uniform3fv(gl.getUniformLocation(this.bakeProg, 'uVolMax'), this.volMax);

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

    _bakeVolumeCpuFallback()
    {
        const gl = this.gl;
        const res = 56;
        if (this.volRes !== res)
        {
            this.volRes = res;
            gl.deleteTexture(this.sdfVolTex);
            this.sdfVolTex = this._create3DVolumeTex(res);
        }

        const buf = new Float32Array(res * res * res * 4);
        const col = { r: 0.58, g: 0.57, b: 0.55 };
        let ptr = 0;
        for (let z = 0; z < res; z++)
        {
            const wz = this.volMin[2] + ((z + 0.5) / res) * (this.volMax[2] - this.volMin[2]);
            for (let y = 0; y < res; y++)
            {
                const wy = this.volMin[1] + ((y + 0.5) / res) * (this.volMax[1] - this.volMin[1]);
                for (let x = 0; x < res; x++)
                {
                    const wx = this.volMin[0] + ((x + 0.5) / res) * (this.volMax[0] - this.volMin[0]);
                    buf[ptr]     = evalRock2StackSdf(this.compiledStack, wx, wy, wz, col);
                    buf[ptr + 1] = col.r;
                    buf[ptr + 2] = col.g;
                    buf[ptr + 3] = col.b;
                    ptr += 4;
                }
            }
        }
        gl.bindTexture(gl.TEXTURE_3D, this.sdfVolTex);
        gl.texSubImage3D(gl.TEXTURE_3D, 0, 0, 0, 0, res, res, res, gl.RGBA, gl.FLOAT, buf);
    }

    uploadQefMesh(mesh, sdfStats)
    {
        const gl = this.gl;
        this.meshStats = sdfStats;
        if (!gl || !mesh || !mesh.indices || mesh.indices.length === 0)
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

    frameStack()
    {
        if (!this.compiledStack) return;
        const b = this.compiledStack.bounds;
        this.target = [
            (b.minX + b.maxX) * 0.5,
            b.minY > 0.2 ? (b.minY * 0.35 + b.maxY * 0.50) : (b.minY + b.maxY) * 0.42,
            (b.minZ + b.maxZ) * 0.5
        ];
        const span = Math.max(3.5, b.maxX - b.minX, Math.max(b.maxY, b.maxY - b.minY), b.maxZ - b.minZ);
        this.distance = span * 1.38;
        this.yaw = 0.52;
        this.pitch = 0.28;
    }

    setActiveLayer(layer)
    {
        this.activeLayer = layer || null;
        const knots = layer?.generator?.knots;
        if (Array.isArray(knots) && knots.length > 0)
        {
            this.selectedKnotIndex = clamp(this.selectedKnotIndex, 0, knots.length - 1);
        }
        else
        {
            this.selectedKnotIndex = 0;
        }
    }

    _getLayerTransform(layer)
    {
        const p = layer?.params || {};
        const R = buildRock2EulerMatrix3(p.yawDeg ?? 0, p.pitchDeg ?? 0, p.rollDeg ?? 0);
        const pivot = [p.posX ?? 0.0, p.posY ?? 0.0, p.posZ ?? 0.0];
        const upW = [R[1], R[4], R[7]];
        return { R, pivot, upW };
    }

    _projectWorldPoint(wx, wy, wz, cam, cssW, cssH)
    {
        const dx = wx - cam.camPos[0];
        const dy = wy - cam.camPos[1];
        const dz = wz - cam.camPos[2];
        const zCam = dx * cam.fwd[0] + dy * cam.fwd[1] + dz * cam.fwd[2];
        if (zCam <= 0.14) return null;

        const xCam = dx * cam.right[0] + dy * cam.right[1] + dz * cam.right[2];
        const yCam = dx * cam.up[0]    + dy * cam.up[1]    + dz * cam.up[2];
        const aspect = cssW / Math.max(1, cssH);
        const ndcX = xCam / (zCam * 0.44 * aspect);
        const ndcY = yCam / (zCam * 0.44);

        return {
            sx: (ndcX * 0.5 + 0.5) * cssW,
            sy: (0.5 - ndcY * 0.5) * cssH,
            depth: zCam
        };
    }

    _screenRay(clientX, clientY)
    {
        const rect = this.canvas.getBoundingClientRect();
        const cssW = Math.max(1, rect.width);
        const cssH = Math.max(1, rect.height);
        const sx = clientX - rect.left;
        const sy = clientY - rect.top;
        const ndcX = (sx / cssW) * 2.0 - 1.0;
        const ndcY = 1.0 - (sy / cssH) * 2.0;
        const aspect = cssW / cssH;

        const cam = this._computeCameraBasis();
        const u = ndcX * aspect * 0.44;
        const v = ndcY * 0.44;
        let rdx = cam.fwd[0] + u * cam.right[0] + v * cam.up[0];
        let rdy = cam.fwd[1] + u * cam.right[1] + v * cam.up[1];
        let rdz = cam.fwd[2] + u * cam.right[2] + v * cam.up[2];
        const rl = Math.hypot(rdx, rdy, rdz) || 1;
        rdx /= rl; rdy /= rl; rdz /= rl;

        return { ro: cam.camPos, rd: [rdx, rdy, rdz], sx, sy, cssW, cssH, cam };
    }

    _intersectHorizontalPlane(clientX, clientY, anchorW, upW)
    {
        const { ro, rd, cam } = this._screenRay(clientX, clientY);
        let nx = upW[0], ny = upW[1], nz = upW[2];
        let denom = rd[0] * nx + rd[1] * ny + rd[2] * nz;
        if (Math.abs(denom) < 0.06)
        {
            // Grazing-angle fallback onto view-aligned plane
            nx = -cam.fwd[0]; ny = -cam.fwd[1]; nz = -cam.fwd[2];
            denom = rd[0] * nx + rd[1] * ny + rd[2] * nz;
            if (Math.abs(denom) < 1e-5) return anchorW;
        }
        const t = ((anchorW[0] - ro[0]) * nx + (anchorW[1] - ro[1]) * ny + (anchorW[2] - ro[2]) * nz) / denom;
        const tClamped = clamp(t, 0.15, 160.0);
        return [ro[0] + rd[0] * tClamped, ro[1] + rd[1] * tClamped, ro[2] + rd[2] * tClamped];
    }

    _intersectVerticalAxis(clientX, clientY, anchorW, upW)
    {
        const { ro, rd } = this._screenRay(clientX, clientY);
        const vx = ro[0] - anchorW[0], vy = ro[1] - anchorW[1], vz = ro[2] - anchorW[2];
        const proj = vx * upW[0] + vy * upW[1] + vz * upW[2];
        let pnx = vx - upW[0] * proj;
        let pny = vy - upW[1] * proj;
        let pnz = vz - upW[2] * proj;
        const pl = Math.hypot(pnx, pny, pnz);
        if (pl < 1e-4) return 0.0;
        pnx /= pl; pny /= pl; pnz /= pl;

        const denom = rd[0] * pnx + rd[1] * pny + rd[2] * pnz;
        if (Math.abs(denom) < 1e-4) return 0.0;
        const t = ((anchorW[0] - ro[0]) * pnx + (anchorW[1] - ro[1]) * pny + (anchorW[2] - ro[2]) * pnz) / denom;
        const tClamped = clamp(t, 0.15, 160.0);
        const hx = ro[0] + rd[0] * tClamped - anchorW[0];
        const hy = ro[1] + rd[1] * tClamped - anchorW[1];
        const hz = ro[2] + rd[2] * tClamped - anchorW[2];
        return hx * upW[0] + hy * upW[1] + hz * upW[2];
    }

    setGizmoToolMode(mode = 'path')
    {
        const valid = (mode === 'translate' || mode === 'rotate' || mode === 'scale') ? mode : 'path';
        this.gizmoToolMode = valid;
    }

    _projectRayOntoWorldAxis(clientX, clientY, originW, axisW)
    {
        const { ro, rd } = this._screenRay(clientX, clientY);
        const w0x = ro[0] - originW[0];
        const w0y = ro[1] - originW[1];
        const w0z = ro[2] - originW[2];
        const b = axisW[0] * rd[0] + axisW[1] * rd[1] + axisW[2] * rd[2];
        const d = axisW[0] * w0x + axisW[1] * w0y + axisW[2] * w0z;
        const e = rd[0] * w0x + rd[1] * w0y + rd[2] * w0z;
        const denom = 1.0 - b * b;
        if (denom < 1e-4) return 0.0;
        return clamp((d - b * e) / denom, -80.0, 80.0);
    }

    _pickHandleAt(sx, sy)
    {
        if (!this.showGizmos) return null;
        let best = null;
        let bestDist = 1e9;
        for (let i = 0; i < this._screenHandles.length; i++)
        {
            const h = this._screenHandles[i];
            const d = Math.hypot(sx - h.sx, sy - h.sy);
            if (d <= h.hitRadius && d < bestDist)
            {
                bestDist = d;
                best = h;
            }
        }
        if (best) return best;

        // Also allow clicking anywhere along a 3D rotation gimbal ring
        for (let i = 0; i < this._screenRotateSegs.length; i++)
        {
            const seg = this._screenRotateSegs[i];
            const abx = seg.x1 - seg.x0, aby = seg.y1 - seg.y0;
            const len2 = abx * abx + aby * aby;
            const f = len2 > 1e-4 ? clamp(((sx - seg.x0) * abx + (sy - seg.y0) * aby) / len2, 0.0, 1.0) : 0.0;
            const px = seg.x0 + abx * f;
            const py = seg.y0 + aby * f;
            const d = Math.hypot(sx - px, sy - py);
            if (d <= 9.5 && d < bestDist)
            {
                bestDist = d;
                best = {
                    id: `rot_seg_${seg.rotKey}`,
                    kind: 'trs_rotate',
                    rotKey: seg.rotKey,
                    centerWorld: seg.centerWorld,
                    sx: px,
                    sy: py,
                    hitRadius: 10,
                    anchorWorld: [
                        seg.w0[0] + (seg.w1[0] - seg.w0[0]) * f,
                        seg.w0[1] + (seg.w1[1] - seg.w0[1]) * f,
                        seg.w0[2] + (seg.w1[2] - seg.w0[2]) * f
                    ]
                };
            }
        }
        return best;
    }

    _pickSplineCurveSegmentAt(sx, sy, maxDistPx = 13)
    {
        if (!this.showGizmos || !this._screenSplineSegs.length) return null;
        let best = null;
        let bestD = maxDistPx;
        for (let i = 0; i < this._screenSplineSegs.length; i++)
        {
            const seg = this._screenSplineSegs[i];
            const abx = seg.x1 - seg.x0, aby = seg.y1 - seg.y0;
            const len2 = abx * abx + aby * aby;
            const f = len2 > 1e-4 ? clamp(((sx - seg.x0) * abx + (sy - seg.y0) * aby) / len2, 0.0, 1.0) : 0.0;
            const px = seg.x0 + abx * f;
            const py = seg.y0 + aby * f;
            const d = Math.hypot(sx - px, sy - py);
            if (d < bestD)
            {
                bestD = d;
                best = {
                    px, py,
                    insertIndex: seg.segIndex + 1,
                    localPt: [
                        seg.l0[0] + (seg.l1[0] - seg.l0[0]) * f,
                        seg.l0[1] + (seg.l1[1] - seg.l0[1]) * f,
                        seg.l0[2] + (seg.l1[2] - seg.l0[2]) * f
                    ],
                    scale: seg.s0 + (seg.s1 - seg.s0) * f
                };
            }
        }
        return best;
    }

    _bindPointerEvents()
    {
        let camDragging = false;
        let panMode = false;
        let lastX = 0, lastY = 0;
        let downClientX = 0, downClientY = 0;
        let clickCandidate = false;

        this.canvas.addEventListener('contextmenu', e => e.preventDefault());

        this.canvas.addEventListener('dblclick', e =>
        {
            const effMode = (this.gizmoToolMode === 'path' && this.activeLayer?.generator?.type !== 'none') ? 'path' : this.gizmoToolMode;
            if (!this.showGizmos || effMode !== 'path' || !this.activeLayer || this.activeLayer.generator?.type !== 'spline') return;
            const rect = this.canvas.getBoundingClientRect();
            const sx = e.clientX - rect.left;
            const sy = e.clientY - rect.top;
            if (this._pickHandleAt(sx, sy)) return;

            const hit = this._pickSplineCurveSegmentAt(sx, sy, 16);
            if (!hit) return;

            const g = this.activeLayer.generator;
            g.knots = normalizeSplineKnots(g.knots);
            if (g.knots.length >= 20) return;

            this.callbacks.onBeforeGizmoEdit?.();
            const insIdx = clamp(hit.insertIndex, 1, g.knots.length);
            g.knots.splice(insIdx, 0, {
                x: Number(hit.localPt[0].toFixed(3)),
                y: Number(hit.localPt[1].toFixed(3)),
                z: Number(hit.localPt[2].toFixed(3)),
                scale: Number(hit.scale.toFixed(2))
            });
            this.selectedKnotIndex = insIdx;
            this.callbacks.onLiveGizmoEdit?.();
            this.callbacks.onCommitGizmoEdit?.();
        });

        this.canvas.addEventListener('pointerdown', e =>
        {
            const rect = this.canvas.getBoundingClientRect();
            const sx = e.clientX - rect.left;
            const sy = e.clientY - rect.top;
            downClientX = e.clientX;
            downClientY = e.clientY;
            clickCandidate = (e.button === 0 && !e.altKey && !e.shiftKey);

            if (e.button === 0 && !e.altKey && this.showGizmos && this.activeLayer && this.activeLayer.enabled)
            {
                const hit = this._pickHandleAt(sx, sy);
                if (hit)
                {
                    clickCandidate = false;
                    if (hit.kind === 'select_instance')
                    {
                        this.selectedInstanceIndex = hit.instanceIndex;
                        this.callbacks.onSelectInstance?.(this.activeLayer.id, hit.instanceIndex);
                        return;
                    }
                    if (hit.knotIndex !== undefined)
                    {
                        this.selectedKnotIndex = hit.knotIndex;
                    }
                    const { R, pivot, upW } = this._getLayerTransform(this.activeLayer);
                    const anchorW = hit.anchorWorld;
                    const p = this.activeLayer.params || {};
                    const g = this.activeLayer.generator;
                    const instIndex = (g && g.type !== 'none' && this.selectedInstanceIndex >= 0)
                        ? this.selectedInstanceIndex
                        : -1;
                    const compLayer = this.compiledStack?.compiledLayers?.find(c => c.id === this.activeLayer.id);
                    const instEntry = instIndex >= 0 ? compLayer?.instanceBounds?.[instIndex] : null;
                    const startOv = (instIndex >= 0 && g)
                        ? JSON.parse(JSON.stringify(getOrCreateRock2InstanceOverride(g, instIndex)))
                        : null;

                    let startAxisS = 0.0;
                    if (hit.axisVec)
                    {
                        startAxisS = this._projectRayOntoWorldAxis(e.clientX, e.clientY, hit.centerWorld || anchorW, hit.axisVec);
                    }

                    const startHitW = hit.axis === 'y'
                        ? anchorW
                        : this._intersectHorizontalPlane(e.clientX, e.clientY, anchorW, hit.planeUpW || upW);
                    const startAxisH = hit.axis === 'y'
                        ? this._intersectVerticalAxis(e.clientX, e.clientY, anchorW, upW)
                        : 0.0;

                    let rotMeta = null;
                    if (hit.kind === 'trs_rotate')
                    {
                        const cam = this._computeCameraBasis();
                        const cssW = Math.max(1, rect.width);
                        const cssH = Math.max(1, rect.height);
                        const cW = hit.centerWorld || pivot;
                        const cScr = this._projectWorldPoint(cW[0], cW[1], cW[2], cam, cssW, cssH) || { sx, sy };

                        if (instIndex >= 0 && instEntry && startOv)
                        {
                            const rawEuler = instEntry.rawEuler || [0, 0, 0];
                            const curWRot = instEntry.worldR || R;
                            const curWPivot = instEntry.worldPivot || cW;
                            const localInstCenter = inverseRock2Vector(
                                curWRot,
                                cW[0] - curWPivot[0],
                                cW[1] - curWPivot[1],
                                cW[2] - curWPivot[2]
                            );
                            const localGrab = inverseRock2Vector(
                                curWRot,
                                anchorW[0] - cW[0],
                                anchorW[1] - cW[1],
                                anchorW[2] - cW[2]
                            );

                            const deltaMap = { yawDeg: 'yawDelta', pitchDeg: 'pitchDelta', rollDeg: 'rollDelta' };
                            const dKey = deltaMap[hit.rotKey] || 'yawDelta';
                            const testDelta = {
                                yawDelta: startOv.yawDelta ?? 0,
                                pitchDelta: startOv.pitchDelta ?? 0,
                                rollDelta: startOv.rollDelta ?? 0
                            };
                            testDelta[dKey] += 1.0;

                            const testInstR = buildRock2EulerMatrix3(
                                rawEuler[0] + testDelta.yawDelta,
                                clamp(rawEuler[1] + testDelta.pitchDelta, -89, 89),
                                clamp(rawEuler[2] + testDelta.rollDelta, -89, 89)
                            );
                            const R1 = mulRock2Mat3(R, testInstR);
                            const p1W = forwardRock2Point(R1, cW, localGrab[0], localGrab[1], localGrab[2]);
                            const s0 = this._projectWorldPoint(anchorW[0], anchorW[1], anchorW[2], cam, cssW, cssH) || { sx, sy };
                            const s1 = this._projectWorldPoint(p1W[0], p1W[1], p1W[2], cam, cssW, cssH) || { sx: sx + 1, sy };

                            const vx = s1.sx - s0.sx, vy = s1.sy - s0.sy;
                            const vl = Math.hypot(vx, vy) || 1;
                            const crossVal = (s0.sx - cScr.sx) * vy - (s0.sy - cScr.sy) * vx;
                            const startAng = Math.atan2(sy - cScr.sy, sx - cScr.sx);

                            rotMeta = {
                                isInstance: true,
                                dKey,
                                rawEuler: [rawEuler[0], rawEuler[1], rawEuler[2]],
                                startWorldPivot: [curWPivot[0], curWPivot[1], curWPivot[2]],
                                localCenter: localInstCenter,
                                centerW: cW,
                                centerScr: cScr,
                                startSx: sx,
                                startSy: sy,
                                prevAng: startAng,
                                accumRad: 0.0,
                                crossVal,
                                crossSign: crossVal >= 0 ? 1.0 : -1.0,
                                tangentPx: [vx / vl, vy / vl]
                            };
                        }
                        else
                        {
                            const localCenter = inverseRock2Vector(R, cW[0] - pivot[0], cW[1] - pivot[1], cW[2] - pivot[2]);
                            const localGrab = inverseRock2Vector(R, anchorW[0] - cW[0], anchorW[1] - cW[1], anchorW[2] - cW[2]);

                            const testParams = {
                                yawDeg: p.yawDeg ?? 0,
                                pitchDeg: p.pitchDeg ?? 0,
                                rollDeg: p.rollDeg ?? 0
                            };
                            testParams[hit.rotKey] += 1.0;
                            const R1 = buildRock2EulerMatrix3(testParams.yawDeg, testParams.pitchDeg, testParams.rollDeg);
                            const p1W = forwardRock2Point(R1, cW, localGrab[0], localGrab[1], localGrab[2]);
                            const s0 = this._projectWorldPoint(anchorW[0], anchorW[1], anchorW[2], cam, cssW, cssH) || { sx, sy };
                            const s1 = this._projectWorldPoint(p1W[0], p1W[1], p1W[2], cam, cssW, cssH) || { sx: sx + 1, sy };

                            const vx = s1.sx - s0.sx, vy = s1.sy - s0.sy;
                            const vl = Math.hypot(vx, vy) || 1;
                            const crossVal = (s0.sx - cScr.sx) * vy - (s0.sy - cScr.sy) * vx;
                            const startAng = Math.atan2(sy - cScr.sy, sx - cScr.sx);

                            rotMeta = {
                                isInstance: false,
                                centerW: cW,
                                centerScr: cScr,
                                localCenter,
                                startSx: sx,
                                startSy: sy,
                                prevAng: startAng,
                                accumRad: 0.0,
                                crossVal,
                                crossSign: crossVal >= 0 ? 1.0 : -1.0,
                                tangentPx: [vx / vl, vy / vl]
                            };
                        }
                    }

                    this._gizmoDrag = {
                        handle: hit,
                        layer: this.activeLayer,
                        instIndex,
                        startOv,
                        R, pivot, upW,
                        anchorW,
                        startHitW,
                        startAxisH,
                        startAxisS,
                        startSx: sx,
                        startSy: sy,
                        rotMeta,
                        startSnapshotTaken: false,
                        moved: false,
                        startValues: JSON.parse(JSON.stringify(this.activeLayer.generator || {})),
                        startParams: JSON.parse(JSON.stringify(this.activeLayer.params || {}))
                    };
                    this.canvas.style.cursor = 'grabbing';
                    this.canvas.setPointerCapture(e.pointerId);
                    this.callbacks.onCommitGizmoEdit?.();
                    return;
                }
            }

            camDragging = true;
            panMode = (e.button === 1) || e.shiftKey;
            lastX = e.clientX;
            lastY = e.clientY;
            this.canvas.setPointerCapture(e.pointerId);
        });

        this.canvas.addEventListener('pointermove', e =>
        {
            if (this._gizmoDrag)
            {
                const dg = this._gizmoDrag;
                const g = dg.layer.generator;
                const p = dg.layer.params;
                if (!p) return;

                if (!dg.startSnapshotTaken)
                {
                    this.callbacks.onBeforeGizmoEdit?.();
                    dg.startSnapshotTaken = true;
                }
                dg.moved = true;

                const h = dg.handle;
                const sv = dg.startValues;
                const sp = dg.startParams;
                const rect = this.canvas.getBoundingClientRect();
                const sx = e.clientX - rect.left;
                const sy = e.clientY - rect.top;

                // --- WHOLE-LAYER OR PER-INSTANCE TRS TRANSFORM GIZMO HANDLERS ---
                const isInstEdit = dg.instIndex >= 0 && g && g.type !== 'none' && dg.startOv;
                const lSx = clamp(sp.scaleX ?? 1.0, 0.12, 8.0);
                const lSy = clamp(sp.scaleY ?? 1.0, 0.12, 8.0);
                const lSz = clamp(sp.scaleZ ?? 1.0, 0.12, 8.0);

                if (h.kind === 'trs_move_axis')
                {
                    const curS = this._projectRayOntoWorldAxis(e.clientX, e.clientY, h.centerWorld || dg.anchorW, h.axisVec);
                    const ds = curS - dg.startAxisS;
                    if (isInstEdit)
                    {
                        const ov = getOrCreateRock2InstanceOverride(g, dg.instIndex);
                        const dLoc = inverseRock2Vector(dg.R, h.axisVec[0] * ds, h.axisVec[1] * ds, h.axisVec[2] * ds);
                        ov.offsetX = Number(clamp((dg.startOv.offsetX ?? 0) + dLoc[0] / lSx, -28.0, 28.0).toFixed(3));
                        ov.offsetY = Number(clamp((dg.startOv.offsetY ?? 0) + dLoc[1] / lSy, -20.0, 28.0).toFixed(3));
                        ov.offsetZ = Number(clamp((dg.startOv.offsetZ ?? 0) + dLoc[2] / lSz, -28.0, 28.0).toFixed(3));
                    }
                    else
                    {
                        p.posX = Number(clamp((sp.posX ?? 0) + h.axisVec[0] * ds, -24.0, 24.0).toFixed(3));
                        p.posY = Number(clamp((sp.posY ?? 0) + h.axisVec[1] * ds, -12.0, 24.0).toFixed(3));
                        p.posZ = Number(clamp((sp.posZ ?? 0) + h.axisVec[2] * ds, -24.0, 24.0).toFixed(3));
                    }
                    this.callbacks.onLiveGizmoEdit?.();
                    return;
                }
                if (h.kind === 'trs_move_xz')
                {
                    const hitW = this._intersectHorizontalPlane(e.clientX, e.clientY, dg.anchorW, [0, 1, 0]);
                    const dx = hitW[0] - dg.startHitW[0];
                    const dz = hitW[2] - dg.startHitW[2];
                    if (isInstEdit)
                    {
                        const ov = getOrCreateRock2InstanceOverride(g, dg.instIndex);
                        const dLoc = inverseRock2Vector(dg.R, dx, 0.0, dz);
                        ov.offsetX = Number(clamp((dg.startOv.offsetX ?? 0) + dLoc[0] / lSx, -28.0, 28.0).toFixed(3));
                        ov.offsetY = Number(clamp((dg.startOv.offsetY ?? 0) + dLoc[1] / lSy, -20.0, 28.0).toFixed(3));
                        ov.offsetZ = Number(clamp((dg.startOv.offsetZ ?? 0) + dLoc[2] / lSz, -28.0, 28.0).toFixed(3));
                    }
                    else
                    {
                        p.posX = Number(clamp((sp.posX ?? 0) + dx, -24.0, 24.0).toFixed(3));
                        p.posZ = Number(clamp((sp.posZ ?? 0) + dz, -24.0, 24.0).toFixed(3));
                    }
                    this.callbacks.onLiveGizmoEdit?.();
                    return;
                }
                if (h.kind === 'trs_rotate' && dg.rotMeta)
                {
                    const rm = dg.rotMeta;
                    let deltaDeg = 0.0;
                    if (Math.abs(rm.crossVal) >= 1.5)
                    {
                        const curAng = Math.atan2(sy - rm.centerScr.sy, sx - rm.centerScr.sx);
                        const stepRad = Math.atan2(Math.sin(curAng - rm.prevAng), Math.cos(curAng - rm.prevAng));
                        rm.prevAng = curAng;
                        rm.accumRad += stepRad;
                        deltaDeg = (rm.accumRad / DEG2RAD) * rm.crossSign;
                    }
                    else
                    {
                        const dpx = (sx - rm.startSx) * rm.tangentPx[0] + (sy - rm.startSy) * rm.tangentPx[1];
                        deltaDeg = dpx * 0.75;
                    }

                    if (rm.isInstance && isInstEdit)
                    {
                        const ov = getOrCreateRock2InstanceOverride(g, dg.instIndex);
                        const dKey = rm.dKey;
                        let nextVal = (dg.startOv[dKey] ?? 0) + deltaDeg;
                        if (dKey === 'yawDelta')
                        {
                            while (nextVal > 180) nextVal -= 360;
                            while (nextVal < -180) nextVal += 360;
                        }
                        else
                        {
                            nextVal = clamp(nextVal, -89.0, 89.0);
                        }
                        ov[dKey] = Number(nextVal.toFixed(1));

                        // Keep individual rock #k anchored around its own 3D gizmo center!
                        const totYaw = rm.rawEuler[0] + (ov.yawDelta ?? 0);
                        const totPitch = clamp(rm.rawEuler[1] + (ov.pitchDelta ?? 0), -89, 89);
                        const totRoll = clamp(rm.rawEuler[2] + (ov.rollDelta ?? 0), -89, 89);
                        const newWorldR = mulRock2Mat3(dg.R, buildRock2EulerMatrix3(totYaw, totPitch, totRoll));
                        const rotOff = forwardRock2Point(newWorldR, [0, 0, 0], rm.localCenter[0], rm.localCenter[1], rm.localCenter[2]);
                        const targetWPivot = [
                            rm.centerW[0] - rotOff[0],
                            rm.centerW[1] - rotOff[1],
                            rm.centerW[2] - rotOff[2]
                        ];
                        const dPW = [
                            targetWPivot[0] - rm.startWorldPivot[0],
                            targetWPivot[1] - rm.startWorldPivot[1],
                            targetWPivot[2] - rm.startWorldPivot[2]
                        ];
                        const dLoc = inverseRock2Vector(dg.R, dPW[0], dPW[1], dPW[2]);
                        ov.offsetX = Number(clamp((dg.startOv.offsetX ?? 0) + dLoc[0] / lSx, -28.0, 28.0).toFixed(3));
                        ov.offsetY = Number(clamp((dg.startOv.offsetY ?? 0) + dLoc[1] / lSy, -20.0, 28.0).toFixed(3));
                        ov.offsetZ = Number(clamp((dg.startOv.offsetZ ?? 0) + dLoc[2] / lSz, -28.0, 28.0).toFixed(3));
                    }
                    else
                    {
                        let nextVal = (sp[h.rotKey] ?? 0) + deltaDeg;
                        if (h.rotKey === 'yawDeg')
                        {
                            while (nextVal > 180) nextVal -= 360;
                            while (nextVal < -180) nextVal += 360;
                        }
                        else
                        {
                            nextVal = clamp(nextVal, -89.0, 89.0);
                        }
                        p[h.rotKey] = Number(nextVal.toFixed(1));

                        // Keep rotation anchored cleanly around the 3D gizmo center
                        const newR = buildRock2EulerMatrix3(p.yawDeg ?? 0, p.pitchDeg ?? 0, p.rollDeg ?? 0);
                        const rotOff = forwardRock2Point(newR, [0, 0, 0], rm.localCenter[0], rm.localCenter[1], rm.localCenter[2]);
                        p.posX = Number(clamp(rm.centerW[0] - rotOff[0], -24.0, 24.0).toFixed(3));
                        p.posY = Number(clamp(rm.centerW[1] - rotOff[1], -12.0, 24.0).toFixed(3));
                        p.posZ = Number(clamp(rm.centerW[2] - rotOff[2], -24.0, 24.0).toFixed(3));
                    }
                    this.callbacks.onLiveGizmoEdit?.();
                    return;
                }
                if (h.kind === 'trs_scale_axis')
                {
                    const curS = this._projectRayOntoWorldAxis(e.clientX, e.clientY, h.centerWorld || dg.anchorW, h.axisVec);
                    const ds = curS - dg.startAxisS;
                    const refLen = Math.max(0.5, h.gizmoLen ?? 1.5);
                    const factor = Math.max(0.12, 1.0 + ds / refLen);
                    if (isInstEdit)
                    {
                        const ov = getOrCreateRock2InstanceOverride(g, dg.instIndex);
                        const baseS = dg.startOv[h.scaleKey] ?? 1.0;
                        ov[h.scaleKey] = Number(clamp(baseS * factor, 0.15, 6.0).toFixed(3));
                    }
                    else
                    {
                        const baseS = sp[h.scaleKey] ?? 1.0;
                        p[h.scaleKey] = Number(clamp(baseS * factor, 0.15, 6.0).toFixed(3));
                    }
                    this.callbacks.onLiveGizmoEdit?.();
                    return;
                }
                if (h.kind === 'trs_scale_uniform')
                {
                    const deltaPx = (sx - dg.startSx) - (sy - dg.startSy);
                    const factor = Math.exp(deltaPx * 0.0075);
                    if (isInstEdit)
                    {
                        const ov = getOrCreateRock2InstanceOverride(g, dg.instIndex);
                        ov.scaleX = Number(clamp((dg.startOv.scaleX ?? 1.0) * factor, 0.15, 6.0).toFixed(3));
                        ov.scaleY = Number(clamp((dg.startOv.scaleY ?? 1.0) * factor, 0.15, 6.0).toFixed(3));
                        ov.scaleZ = Number(clamp((dg.startOv.scaleZ ?? 1.0) * factor, 0.15, 6.0).toFixed(3));
                    }
                    else
                    {
                        p.scaleX = Number(clamp((sp.scaleX ?? 1.0) * factor, 0.15, 6.0).toFixed(3));
                        p.scaleY = Number(clamp((sp.scaleY ?? 1.0) * factor, 0.15, 6.0).toFixed(3));
                        p.scaleZ = Number(clamp((sp.scaleZ ?? 1.0) * factor, 0.15, 6.0).toFixed(3));
                    }
                    this.callbacks.onLiveGizmoEdit?.();
                    return;
                }

                // --- GENERATOR PATH / CLOUD / ARRAY / SPLATTER / SCATTER HANDLERS ---
                if (!g) return;

                if (h.axis === 'y')
                {
                    const curH = this._intersectVerticalAxis(e.clientX, e.clientY, dg.anchorW, dg.upW);
                    const dy = curH - dg.startAxisH;

                    if (h.kind === 'spline_knot_y' && Array.isArray(g.knots) && g.knots[h.knotIndex])
                    {
                        g.knots[h.knotIndex].y = clamp((sv.knots[h.knotIndex]?.y ?? 0) + dy, -6.0, 14.0);
                    }
                    else if (h.kind === 'array_start_y')
                    {
                        g.startY = clamp((sv.startY ?? 0) + dy, -6.0, 12.0);
                    }
                    else if (h.kind === 'array_end_y')
                    {
                        g.endY = clamp((sv.endY ?? 0) + dy, -6.0, 12.0);
                    }
                    else if (h.kind === 'cloud_center_y')
                    {
                        g.centerY = clamp((sv.centerY ?? 3.8) + dy, -2.0, 16.0);
                    }
                    else if (h.kind === 'cloud_spread_y')
                    {
                        g.spreadY = clamp((sv.spreadY ?? 2.1) + dy, 0.15, 10.0);
                    }
                }
                else
                {
                    const hitW = this._intersectHorizontalPlane(e.clientX, e.clientY, dg.anchorW, dg.upW);
                    const dLocal = inverseRock2Vector(
                        dg.R,
                        hitW[0] - dg.startHitW[0],
                        hitW[1] - dg.startHitW[1],
                        hitW[2] - dg.startHitW[2]
                    );
                    const dlx = dLocal[0];
                    const dlz = dLocal[2];

                    if (h.kind === 'spline_knot_xz' && Array.isArray(g.knots) && g.knots[h.knotIndex])
                    {
                        g.knots[h.knotIndex].x = clamp((sv.knots[h.knotIndex]?.x ?? 0) + dlx, -16.0, 16.0);
                        g.knots[h.knotIndex].z = clamp((sv.knots[h.knotIndex]?.z ?? 0) + dlz, -16.0, 16.0);
                    }
                    else if (h.kind === 'array_start_xz')
                    {
                        g.startX = clamp((sv.startX ?? -4.8) + dlx, -16.0, 16.0);
                        g.startZ = clamp((sv.startZ ?? 0.0) + dlz, -16.0, 16.0);
                    }
                    else if (h.kind === 'array_end_xz')
                    {
                        g.endX = clamp((sv.endX ?? 4.8) + dlx, -16.0, 16.0);
                        g.endZ = clamp((sv.endZ ?? 0.0) + dlz, -16.0, 16.0);
                    }
                    else if (h.kind === 'array_grid_x')
                    {
                        const nx = Math.max(2, Math.round(sv.countX ?? 3));
                        const baseHalf = ((nx - 1) * (sv.stepX ?? 2.4)) * 0.5;
                        const nextHalf = Math.max(0.25, baseHalf + dlx);
                        g.stepX = clamp((nextHalf * 2.0) / (nx - 1), 0.35, 8.0);
                    }
                    else if (h.kind === 'array_grid_z')
                    {
                        const nz = Math.max(2, Math.round(sv.countZ ?? 3));
                        const baseHalf = ((nz - 1) * (sv.stepZ ?? 2.4)) * 0.5;
                        const nextHalf = Math.max(0.25, baseHalf + dlz);
                        g.stepZ = clamp((nextHalf * 2.0) / (nz - 1), 0.35, 8.0);
                    }
                    else if (h.kind === 'array_hex_spacing')
                    {
                        const rings = Math.max(1, Math.round(sv.hexRings ?? 2));
                        const curLocal = inverseRock2Vector(dg.R, hitW[0] - dg.pivot[0], hitW[1] - dg.pivot[1], hitW[2] - dg.pivot[2]);
                        const rad = Math.max(0.35, Math.hypot(curLocal[0], curLocal[2]));
                        g.cellSpacing = clamp(rad / rings, 0.30, 4.5);
                    }
                    else if (h.kind === 'array_ring_radius')
                    {
                        const curLocal = inverseRock2Vector(dg.R, hitW[0] - dg.pivot[0], hitW[1] - dg.pivot[1], hitW[2] - dg.pivot[2]);
                        g.ringRadius = clamp(Math.hypot(curLocal[0], curLocal[2]), 0.5, 14.0);
                    }
                    else if (h.kind === 'splatter_radius')
                    {
                        const curLocal = inverseRock2Vector(dg.R, hitW[0] - dg.pivot[0], hitW[1] - dg.pivot[1], hitW[2] - dg.pivot[2]);
                        g.outerRadius = clamp(Math.hypot(curLocal[0], curLocal[2]), 0.5, 14.0);
                        if (g.splatterMode === 'talus_fan')
                        {
                            g.fanAzimuthDeg = Math.round(Math.atan2(curLocal[0], curLocal[2]) / DEG2RAD);
                        }
                    }
                    else if (h.kind === 'scatter_width')
                    {
                        const curLocal = inverseRock2Vector(dg.R, hitW[0] - dg.pivot[0], hitW[1] - dg.pivot[1], hitW[2] - dg.pivot[2]);
                        g.scatterWidth = clamp(Math.abs(curLocal[0]) * 2.0, 1.0, 22.0);
                    }
                    else if (h.kind === 'scatter_length')
                    {
                        const curLocal = inverseRock2Vector(dg.R, hitW[0] - dg.pivot[0], hitW[1] - dg.pivot[1], hitW[2] - dg.pivot[2]);
                        g.scatterLength = clamp(Math.abs(curLocal[2]) * 2.0, 1.0, 22.0);
                    }
                    else if (h.kind === 'cloud_center_xz')
                    {
                        g.centerX = clamp((sv.centerX ?? 0.0) + dlx, -16.0, 16.0);
                        g.centerZ = clamp((sv.centerZ ?? 0.0) + dlz, -16.0, 16.0);
                    }
                    else if (h.kind === 'cloud_radius_xz')
                    {
                        const curLocal = inverseRock2Vector(dg.R, hitW[0] - dg.pivot[0], hitW[1] - dg.pivot[1], hitW[2] - dg.pivot[2]);
                        const cx = sv.centerX ?? 0.0, cz = sv.centerZ ?? 0.0;
                        g.radiusXZ = clamp(Math.hypot(curLocal[0] - cx, curLocal[2] - cz), 0.4, 16.0);
                    }
                }

                this.callbacks.onLiveGizmoEdit?.();
                return;
            }

            if (!camDragging)
            {
                const rect = this.canvas.getBoundingClientRect();
                const sx = e.clientX - rect.left;
                const sy = e.clientY - rect.top;
                this._hoveredHandle = this._pickHandleAt(sx, sy);
                const effMode = (this.gizmoToolMode === 'path' && this.activeLayer?.generator?.type !== 'none') ? 'path' : this.gizmoToolMode;
                this._hoveredCurveInsert = (!this._hoveredHandle && effMode === 'path' && this.activeLayer?.generator?.type === 'spline')
                    ? this._pickSplineCurveSegmentAt(sx, sy, 13)
                    : null;

                if (this._hoveredHandle)
                {
                    this.canvas.style.cursor = this._hoveredHandle.axis === 'y' ? 'ns-resize' : 'grab';
                }
                else if (this._hoveredCurveInsert)
                {
                    this.canvas.style.cursor = 'copy';
                }
                else
                {
                    this.canvas.style.cursor = '';
                }
                return;
            }

            if (clickCandidate && Math.hypot(e.clientX - downClientX, e.clientY - downClientY) > 4.5)
            {
                clickCandidate = false;
            }

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
                this.pitch = clamp(this.pitch + dy * 0.0055, -0.25, 1.48);
            }
        });

        const endDrag = e =>
        {
            if (this._gizmoDrag)
            {
                const wasMoved = this._gizmoDrag.moved;
                this._gizmoDrag = null;
                this.canvas.style.cursor = this._hoveredHandle ? 'grab' : '';
                if (wasMoved) this.callbacks.onCommitGizmoEdit?.();
            }
            else if (clickCandidate && e && e.type === 'pointerup' && Math.hypot(e.clientX - downClientX, e.clientY - downClientY) <= 4.5)
            {
                // Click-on-rock in 3D Viewport -> Sphere-trace Pure-SDF stack!
                // First click on another layer selects that Layer; clicking a rock inside an already-selected
                // multi-rock layer (or with Instance Lock ON) drills directly into that individual rock #k!
                const { ro, rd } = this._screenRay(e.clientX, e.clientY);
                const picked = pickRock2LayerAtRay(this.compiledStack, ro, rd);
                if (picked && picked.layerId)
                {
                    const isSameLayer = (picked.layerId === this.activeLayer?.id);
                    const isMulti = (picked.rawInstanceCount ?? 1) > 1;
                    const nextInst = (isMulti && (this.instancePickLock || isSameLayer))
                        ? (picked.instanceIndex ?? 0)
                        : -1;
                    this.selectedInstanceIndex = nextInst;
                    if (nextInst >= 0 && this.callbacks.onSelectInstance)
                    {
                        this.callbacks.onSelectInstance(picked.layerId, nextInst, picked.hitPoint);
                    }
                    else
                    {
                        this.callbacks.onSelectLayer?.(picked.layerId, picked.hitPoint);
                    }
                }
                else if (this.selectedInstanceIndex >= 0 && this.activeLayer)
                {
                    // Clicking empty floor/sky exits sub-rock mode back to Whole Layer
                    this.selectedInstanceIndex = -1;
                    this.callbacks.onSelectInstance?.(this.activeLayer.id, -1);
                }
            }
            clickCandidate = false;
            camDragging = false;
        };
        this.canvas.addEventListener('pointerup', endDrag);
        this.canvas.addEventListener('pointercancel', endDrag);

        this.canvas.addEventListener('wheel', e =>
        {
            e.preventDefault();
            const factor = Math.exp(e.deltaY * 0.0011);
            this.distance = clamp(this.distance * factor, 1.5, 65.0);
        }, { passive: false });
    }

    _computeCameraBasis()
    {
        const cp = Math.cos(this.pitch), sp = Math.sin(this.pitch);
        const sy = Math.sin(this.yaw), cy = Math.cos(this.yaw);
        const camPos = [
            this.target[0] + this.distance * cp * sy,
            this.target[1] + this.distance * sp,
            this.target[2] + this.distance * cp * cy
        ];
        const fx = this.target[0] - camPos[0];
        const fy = this.target[1] - camPos[1];
        const fz = this.target[2] - camPos[2];
        const fl = Math.hypot(fx, fy, fz) || 1;
        const fwd = [fx / fl, fy / fl, fz / fl];

        const rx = -fwd[2], rz = fwd[0];
        const rl = Math.hypot(rx, rz) || 1;
        const right = [rx / rl, 0, rz / rl];

        const up = [
            right[1] * fwd[2] - right[2] * fwd[1],
            right[2] * fwd[0] - right[0] * fwd[2],
            right[0] * fwd[1] - right[1] * fwd[0]
        ];
        return { camPos, fwd, right, up };
    }

    _buildViewProjMatrix(camPos, fwd, right, up, aspect)
    {
        const f = 1.0 / 0.44;
        const near = 0.12, far = 160.0;
        const nf = 1.0 / (near - far);

        const p00 = f / aspect;
        const p11 = f;
        const p22 = (far + near) * nf;
        const p23 = -1.0;
        const p32 = (2.0 * far * near) * nf;

        const vx = -(right[0] * camPos[0] + right[1] * camPos[1] + right[2] * camPos[2]);
        const vy = -(up[0] * camPos[0] + up[1] * camPos[1] + up[2] * camPos[2]);
        const vz = (fwd[0] * camPos[0] + fwd[1] * camPos[1] + fwd[2] * camPos[2]);

        const V = [
            right[0], up[0], -fwd[0], 0,
            right[1], up[1], -fwd[1], 0,
            right[2], up[2], -fwd[2], 0,
            vx,       vy,     vz,     1
        ];
        const P = [
            p00, 0,   0,   0,
            0,   p11, 0,   0,
            0,   0,   p22, p23,
            0,   0,   p32, 0
        ];

        const out = new Float32Array(16);
        for (let col = 0; col < 4; col++)
        {
            for (let row = 0; row < 4; row++)
            {
                let s = 0;
                for (let k = 0; k < 4; k++) s += P[k * 4 + row] * V[col * 4 + k];
                out[col * 4 + row] = s;
            }
        }
        return out;
    }

    _drawHandleWithElevationArrow(ctx, cam, cssW, cssH, R, pivot, lx, ly, lz, opts)
    {
        const wBase = forwardRock2Point(R, pivot, lx, ly, lz);
        const wFloor = forwardRock2Point(R, pivot, lx, 0.0, lz);
        const arrowLenM = clamp(this.distance * 0.095, 0.55, 1.85);
        const wTip = forwardRock2Point(R, pivot, lx, ly + arrowLenM, lz);

        const pBase = this._projectWorldPoint(wBase[0], wBase[1], wBase[2], cam, cssW, cssH);
        if (!pBase) return;
        const pFloor = this._projectWorldPoint(wFloor[0], wFloor[1], wFloor[2], cam, cssW, cssH);
        const pTip = this._projectWorldPoint(wTip[0], wTip[1], wTip[2], cam, cssW, cssH);

        // Ground stem when elevated/lowered
        if (pFloor && Math.abs(ly) > 0.03)
        {
            ctx.save();
            ctx.setLineDash([3, 3]);
            ctx.strokeStyle = 'rgba(239, 244, 207, 0.38)';
            ctx.lineWidth = 1.25;
            ctx.beginPath();
            ctx.moveTo(pFloor.sx, pFloor.sy);
            ctx.lineTo(pBase.sx, pBase.sy);
            ctx.stroke();
            ctx.fillStyle = 'rgba(239, 244, 207, 0.55)';
            ctx.beginPath();
            ctx.arc(pFloor.sx, pFloor.sy, 2.6, 0, Math.PI * 2);
            ctx.fill();
            ctx.restore();
        }

        // Vertical Y Arrow Handle
        if (pTip && opts.kindY)
        {
            const isHoverY = (this._hoveredHandle?.id === opts.idY) || (this._gizmoDrag?.handle?.id === opts.idY);
            const arrowCol = isHoverY ? '#8ff7cb' : '#52d69c';

            ctx.save();
            ctx.strokeStyle = 'rgba(10, 12, 16, 0.78)';
            ctx.lineWidth = 4.0;
            ctx.beginPath();
            ctx.moveTo(pBase.sx, pBase.sy);
            ctx.lineTo(pTip.sx, pTip.sy);
            ctx.stroke();

            ctx.strokeStyle = arrowCol;
            ctx.lineWidth = isHoverY ? 2.6 : 2.0;
            ctx.beginPath();
            ctx.moveTo(pBase.sx, pBase.sy);
            ctx.lineTo(pTip.sx, pTip.sy);
            ctx.stroke();

            // Upward arrowhead triangle
            const ang = Math.atan2(pTip.sy - pBase.sy, pTip.sx - pBase.sx);
            const headLen = isHoverY ? 9.5 : 8.0;
            ctx.fillStyle = arrowCol;
            ctx.beginPath();
            ctx.moveTo(pTip.sx, pTip.sy);
            ctx.lineTo(pTip.sx - headLen * Math.cos(ang - 0.45), pTip.sy - headLen * Math.sin(ang - 0.45));
            ctx.lineTo(pTip.sx - headLen * Math.cos(ang + 0.45), pTip.sy - headLen * Math.sin(ang + 0.45));
            ctx.closePath();
            ctx.fill();
            ctx.restore();

            this._screenHandles.push({
                id: opts.idY,
                kind: opts.kindY,
                axis: 'y',
                knotIndex: opts.knotIndex,
                sx: pTip.sx,
                sy: pTip.sy,
                hitRadius: 12,
                anchorWorld: wBase
            });
        }

        // XZ Planar Disc Badge
        const isHoverXZ = (this._hoveredHandle?.id === opts.idXZ) || (this._gizmoDrag?.handle?.id === opts.idXZ);
        const rad = opts.selected ? 11.0 : 9.5;

        if (opts.selected || isHoverXZ)
        {
            ctx.save();
            ctx.strokeStyle = isHoverXZ ? '#ffffff' : opts.accent;
            ctx.lineWidth = 2.0;
            ctx.beginPath();
            ctx.arc(pBase.sx, pBase.sy, rad + 4.0, 0, Math.PI * 2);
            ctx.stroke();
            ctx.restore();
        }

        ctx.save();
        ctx.fillStyle = opts.selected ? opts.accent : 'rgba(20, 23, 29, 0.92)';
        ctx.strokeStyle = opts.accent;
        ctx.lineWidth = 2.2;
        ctx.beginPath();
        ctx.arc(pBase.sx, pBase.sy, rad, 0, Math.PI * 2);
        ctx.fill();
        ctx.stroke();

        ctx.fillStyle = opts.selected ? '#141610' : '#f4f6ef';
        ctx.font = '700 10px ui-sans-serif, system-ui, sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(opts.label, pBase.sx, pBase.sy + 0.5);
        ctx.restore();

        this._screenHandles.push({
            id: opts.idXZ,
            kind: opts.kindXZ,
            axis: 'xz',
            knotIndex: opts.knotIndex,
            sx: pBase.sx,
            sy: pBase.sy,
            hitRadius: 14,
            anchorWorld: wBase
        });
    }

    _drawSelectionBounds(ctx, cam, cssW, cssH, bounds, labelText, strokeColor = 'rgba(239, 244, 207, 0.44)', pillAccent = '#eff4cf')
    {
        if (!bounds) return;
        const { minX, minY, minZ, maxX, maxY, maxZ } = bounds;
        const corners = [
            [minX, minY, minZ], [maxX, minY, minZ], [maxX, minY, maxZ], [minX, minY, maxZ],
            [minX, maxY, minZ], [maxX, maxY, minZ], [maxX, maxY, maxZ], [minX, maxY, maxZ]
        ];
        const edges = [
            [0, 1], [1, 2], [2, 3], [3, 0],
            [4, 5], [5, 6], [6, 7], [7, 4],
            [0, 4], [1, 5], [2, 6], [3, 7]
        ];

        ctx.save();
        ctx.strokeStyle = strokeColor;
        ctx.lineWidth = 1.45;
        for (let i = 0; i < edges.length; i++)
        {
            const [ia, ib] = edges[i];
            const a = corners[ia], b = corners[ib];
            for (const [p0, p1] of [[a, b], [b, a]])
            {
                const q = [
                    p0[0] + (p1[0] - p0[0]) * 0.22,
                    p0[1] + (p1[1] - p0[1]) * 0.22,
                    p0[2] + (p1[2] - p0[2]) * 0.22
                ];
                const s0 = this._projectWorldPoint(p0[0], p0[1], p0[2], cam, cssW, cssH);
                const s1 = this._projectWorldPoint(q[0], q[1], q[2], cam, cssW, cssH);
                if (s0 && s1)
                {
                    ctx.beginPath();
                    ctx.moveTo(s0.sx, s0.sy);
                    ctx.lineTo(s1.sx, s1.sy);
                    ctx.stroke();
                }
            }
        }

        if (labelText)
        {
            const topP = this._projectWorldPoint(bounds.cx, maxY + 0.35, bounds.cz, cam, cssW, cssH);
            if (topP)
            {
                ctx.font = '600 10px ui-sans-serif, system-ui, sans-serif';
                const tw = ctx.measureText(labelText).width + 14;
                ctx.fillStyle = 'rgba(13, 16, 21, 0.88)';
                ctx.strokeStyle = pillAccent;
                ctx.lineWidth = 1.1;
                ctx.beginPath();
                ctx.roundRect(topP.sx - tw * 0.5, topP.sy - 20, tw, 18, 9);
                ctx.fill();
                ctx.stroke();
                ctx.fillStyle = pillAccent;
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                ctx.fillText(labelText, topP.sx, topP.sy - 11);
            }
        }
        ctx.restore();
    }

    _renderTrsGizmo(ctx, cam, cssW, cssH, layer, mode, centerW, R)
    {
        const pCenter = this._projectWorldPoint(centerW[0], centerW[1], centerW[2], cam, cssW, cssH);
        if (!pCenter) return;

        const camDist = Math.hypot(
            centerW[0] - cam.camPos[0],
            centerW[1] - cam.camPos[1],
            centerW[2] - cam.camPos[2]
        );
        const gizmoLen = clamp(camDist * 0.155, 0.85, 4.4);
        const p = layer.params || {};

        if (mode === 'translate')
        {
            // XZ ground-plane free-move quad
            const qLen = gizmoLen * 0.34;
            const qPts = [
                this._projectWorldPoint(centerW[0], centerW[1], centerW[2], cam, cssW, cssH),
                this._projectWorldPoint(centerW[0] + qLen, centerW[1], centerW[2], cam, cssW, cssH),
                this._projectWorldPoint(centerW[0] + qLen, centerW[1], centerW[2] + qLen, cam, cssW, cssH),
                this._projectWorldPoint(centerW[0], centerW[1], centerW[2] + qLen, cam, cssW, cssH)
            ];
            const isHovXZ = (this._hoveredHandle?.id === 'trs_mv_xz') || (this._gizmoDrag?.handle?.id === 'trs_mv_xz');
            if (qPts.every(Boolean))
            {
                ctx.save();
                ctx.fillStyle = isHovXZ ? 'rgba(239, 244, 207, 0.42)' : 'rgba(239, 244, 207, 0.20)';
                ctx.strokeStyle = isHovXZ ? '#ffffff' : '#eff4cf';
                ctx.lineWidth = 1.6;
                ctx.beginPath();
                ctx.moveTo(qPts[0].sx, qPts[0].sy);
                ctx.lineTo(qPts[1].sx, qPts[1].sy);
                ctx.lineTo(qPts[2].sx, qPts[2].sy);
                ctx.lineTo(qPts[3].sx, qPts[3].sy);
                ctx.closePath();
                ctx.fill();
                ctx.stroke();
                ctx.restore();

                const qMid = this._projectWorldPoint(centerW[0] + qLen * 0.55, centerW[1], centerW[2] + qLen * 0.55, cam, cssW, cssH);
                if (qMid)
                {
                    this._screenHandles.push({
                        id: 'trs_mv_xz',
                        kind: 'trs_move_xz',
                        axis: 'xz',
                        sx: qMid.sx,
                        sy: qMid.sy,
                        hitRadius: 15,
                        centerWorld: centerW,
                        anchorWorld: [centerW[0] + qLen * 0.55, centerW[1], centerW[2] + qLen * 0.55]
                    });
                }
            }

            const axes = [
                { id: 'trs_mv_x', label: 'X', vec: [1, 0, 0], col: '#ff6b6b', hov: '#ffa8a8' },
                { id: 'trs_mv_y', label: 'Y', vec: [0, 1, 0], col: '#7ee787', hov: '#b2f2bb' },
                { id: 'trs_mv_z', label: 'Z', vec: [0, 0, 1], col: '#6cb6ff', hov: '#a5d8ff' }
            ];

            for (let i = 0; i < axes.length; i++)
            {
                const ax = axes[i];
                const tipW = [
                    centerW[0] + ax.vec[0] * gizmoLen,
                    centerW[1] + ax.vec[1] * gizmoLen,
                    centerW[2] + ax.vec[2] * gizmoLen
                ];
                const pTip = this._projectWorldPoint(tipW[0], tipW[1], tipW[2], cam, cssW, cssH);
                if (!pTip) continue;

                const isHov = (this._hoveredHandle?.id === ax.id) || (this._gizmoDrag?.handle?.id === ax.id);
                const strokeCol = isHov ? ax.hov : ax.col;

                ctx.save();
                ctx.strokeStyle = 'rgba(10, 12, 16, 0.84)';
                ctx.lineWidth = 5.0;
                ctx.beginPath();
                ctx.moveTo(pCenter.sx, pCenter.sy);
                ctx.lineTo(pTip.sx, pTip.sy);
                ctx.stroke();

                ctx.strokeStyle = strokeCol;
                ctx.lineWidth = isHov ? 3.2 : 2.5;
                ctx.beginPath();
                ctx.moveTo(pCenter.sx, pCenter.sy);
                ctx.lineTo(pTip.sx, pTip.sy);
                ctx.stroke();

                const ang = Math.atan2(pTip.sy - pCenter.sy, pTip.sx - pCenter.sx);
                const head = isHov ? 12.0 : 10.2;
                ctx.fillStyle = strokeCol;
                ctx.beginPath();
                ctx.moveTo(pTip.sx, pTip.sy);
                ctx.lineTo(pTip.sx - head * Math.cos(ang - 0.44), pTip.sy - head * Math.sin(ang - 0.44));
                ctx.lineTo(pTip.sx - head * Math.cos(ang + 0.44), pTip.sy - head * Math.sin(ang + 0.44));
                ctx.closePath();
                ctx.fill();

                // Axis badge near tip
                const bx = pTip.sx + Math.cos(ang) * 11;
                const by = pTip.sy + Math.sin(ang) * 11;
                ctx.fillStyle = 'rgba(14, 17, 22, 0.90)';
                ctx.strokeStyle = strokeCol;
                ctx.lineWidth = 1.6;
                ctx.beginPath();
                ctx.arc(bx, by, 8.0, 0, Math.PI * 2);
                ctx.fill();
                ctx.stroke();
                ctx.fillStyle = strokeCol;
                ctx.font = '700 9.5px ui-sans-serif, system-ui, sans-serif';
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                ctx.fillText(ax.label, bx, by + 0.5);
                ctx.restore();

                this._screenHandles.push({
                    id: ax.id,
                    kind: 'trs_move_axis',
                    axis: ax.label === 'Y' ? 'y' : 'xz',
                    axisVec: ax.vec,
                    centerWorld: centerW,
                    anchorWorld: tipW,
                    sx: pTip.sx,
                    sy: pTip.sy,
                    hitRadius: 16
                });
            }

            if (this._gizmoDrag && (this._gizmoDrag.handle.kind === 'trs_move_axis' || this._gizmoDrag.handle.kind === 'trs_move_xz'))
            {
                const kIdx = this._gizmoDrag.instIndex ?? -1;
                const ov = kIdx >= 0 ? layer.generator?.instanceOverrides?.[kIdx] : null;
                const txt = ov
                    ? `Rock #${kIdx + 1} ΔMove  X ${(ov.offsetX ?? 0).toFixed(2)}m · Y ${(ov.offsetY ?? 0).toFixed(2)}m · Z ${(ov.offsetZ ?? 0).toFixed(2)}m`
                    : `Move  X ${(p.posX ?? 0).toFixed(2)}m · Y ${(p.posY ?? 0).toFixed(2)}m · Z ${(p.posZ ?? 0).toFixed(2)}m`;
                this._drawGizmoReadoutPill(ctx, pCenter.sx, pCenter.sy + 26, txt, ov ? '#7dd3fc' : '#eff4cf');
            }
            return;
        }

        if (mode === 'rotate')
        {
            const rings = [
                { id: 'trs_rot_pitch', rotKey: 'pitchDeg', label: 'P', col: '#ff6b6b', hov: '#ffa8a8', u: [0, 1, 0], v: [0, 0, 1] },
                { id: 'trs_rot_yaw',   rotKey: 'yawDeg',   label: 'Y', col: '#7ee787', hov: '#b2f2bb', u: [1, 0, 0], v: [0, 0, 1] },
                { id: 'trs_rot_roll',  rotKey: 'rollDeg',  label: 'R', col: '#6cb6ff', hov: '#a5d8ff', u: [1, 0, 0], v: [0, 1, 0] }
            ];

            const viewDir = [
                cam.camPos[0] - centerW[0],
                cam.camPos[1] - centerW[1],
                cam.camPos[2] - centerW[2]
            ];
            const vdLen = Math.hypot(viewDir[0], viewDir[1], viewDir[2]) || 1;
            viewDir[0] /= vdLen; viewDir[1] /= vdLen; viewDir[2] /= vdLen;

            const segs = 48;
            for (let ri = 0; ri < rings.length; ri++)
            {
                const rg = rings[ri];
                const isHov = (this._hoveredHandle?.rotKey === rg.rotKey) || (this._gizmoDrag?.handle?.rotKey === rg.rotKey);
                const strokeCol = isHov ? rg.hov : rg.col;

                // Use world-oriented basis for Yaw, layer-oriented basis for Pitch & Roll
                const uVec = rg.rotKey === 'yawDeg' ? rg.u : forwardRock2Point(R, [0, 0, 0], rg.u[0], rg.u[1], rg.u[2]);
                const vVec = rg.rotKey === 'yawDeg' ? rg.v : forwardRock2Point(R, [0, 0, 0], rg.v[0], rg.v[1], rg.v[2]);

                let bestFrontDot = -1e9;
                let badgePt = null;
                let badgeW = null;

                let prevP = null, prevW = null;
                for (let s = 0; s <= segs; s++)
                {
                    const a = (s / segs) * Math.PI * 2.0;
                    const ca = Math.cos(a), sa = Math.sin(a);
                    const rx = uVec[0] * ca + vVec[0] * sa;
                    const ry = uVec[1] * ca + vVec[1] * sa;
                    const rz = uVec[2] * ca + vVec[2] * sa;
                    const wPt = [
                        centerW[0] + rx * gizmoLen,
                        centerW[1] + ry * gizmoLen,
                        centerW[2] + rz * gizmoLen
                    ];
                    const p2 = this._projectWorldPoint(wPt[0], wPt[1], wPt[2], cam, cssW, cssH);
                    const fDot = rx * viewDir[0] + ry * viewDir[1] + rz * viewDir[2];

                    if (p2 && fDot > bestFrontDot)
                    {
                        bestFrontDot = fDot;
                        badgePt = p2;
                        badgeW = wPt;
                    }

                    if (prevP && p2)
                    {
                        const front = fDot >= -0.08;
                        ctx.save();
                        ctx.globalAlpha = front ? 0.96 : 0.26;
                        ctx.strokeStyle = strokeCol;
                        ctx.lineWidth = isHov ? (front ? 3.4 : 1.8) : (front ? 2.4 : 1.3);
                        ctx.beginPath();
                        ctx.moveTo(prevP.sx, prevP.sy);
                        ctx.lineTo(p2.sx, p2.sy);
                        ctx.stroke();
                        ctx.restore();

                        if (front)
                        {
                            this._screenRotateSegs.push({
                                rotKey: rg.rotKey,
                                centerWorld: centerW,
                                x0: prevP.sx, y0: prevP.sy,
                                x1: p2.sx,    y1: p2.sy,
                                w0: prevW,    w1: wPt
                            });
                        }
                    }
                    prevP = p2;
                    prevW = wPt;
                }

                if (badgePt && badgeW)
                {
                    ctx.save();
                    ctx.fillStyle = isHov ? strokeCol : 'rgba(14, 17, 22, 0.92)';
                    ctx.strokeStyle = strokeCol;
                    ctx.lineWidth = 2.0;
                    ctx.beginPath();
                    ctx.arc(badgePt.sx, badgePt.sy, 9.5, 0, Math.PI * 2);
                    ctx.fill();
                    ctx.stroke();
                    ctx.fillStyle = isHov ? '#101412' : strokeCol;
                    ctx.font = '700 9.5px ui-sans-serif, system-ui, sans-serif';
                    ctx.textAlign = 'center';
                    ctx.textBaseline = 'middle';
                    ctx.fillText(rg.label, badgePt.sx, badgePt.sy + 0.5);
                    ctx.restore();

                    this._screenHandles.push({
                        id: rg.id,
                        kind: 'trs_rotate',
                        rotKey: rg.rotKey,
                        centerWorld: centerW,
                        anchorWorld: badgeW,
                        sx: badgePt.sx,
                        sy: badgePt.sy,
                        hitRadius: 14
                    });
                }
            }

            if (this._gizmoDrag && this._gizmoDrag.handle.kind === 'trs_rotate')
            {
                const kIdx = this._gizmoDrag.instIndex ?? -1;
                const ov = kIdx >= 0 ? layer.generator?.instanceOverrides?.[kIdx] : null;
                const txt = ov
                    ? `Rock #${kIdx + 1} ΔYaw ${(ov.yawDelta ?? 0).toFixed(0)}° · ΔPitch ${(ov.pitchDelta ?? 0).toFixed(0)}° · ΔRoll ${(ov.rollDelta ?? 0).toFixed(0)}°`
                    : `Yaw ${(p.yawDeg ?? 0).toFixed(0)}° · Pitch ${(p.pitchDeg ?? 0).toFixed(0)}° · Roll ${(p.rollDeg ?? 0).toFixed(0)}°`;
                this._drawGizmoReadoutPill(ctx, pCenter.sx, pCenter.sy + 28, txt, ov ? '#7dd3fc' : '#eff4cf');
            }
            return;
        }

        if (mode === 'scale')
        {
            const axes = [
                { id: 'trs_sc_x', scaleKey: 'scaleX', label: 'X', vec: [R[0], R[3], R[6]], col: '#ff6b6b', hov: '#ffa8a8' },
                { id: 'trs_sc_y', scaleKey: 'scaleY', label: 'Y', vec: [R[1], R[4], R[7]], col: '#7ee787', hov: '#b2f2bb' },
                { id: 'trs_sc_z', scaleKey: 'scaleZ', label: 'Z', vec: [R[2], R[5], R[8]], col: '#6cb6ff', hov: '#a5d8ff' }
            ];

            for (let i = 0; i < axes.length; i++)
            {
                const ax = axes[i];
                const tipW = [
                    centerW[0] + ax.vec[0] * gizmoLen,
                    centerW[1] + ax.vec[1] * gizmoLen,
                    centerW[2] + ax.vec[2] * gizmoLen
                ];
                const pTip = this._projectWorldPoint(tipW[0], tipW[1], tipW[2], cam, cssW, cssH);
                if (!pTip) continue;

                const isHov = (this._hoveredHandle?.id === ax.id) || (this._gizmoDrag?.handle?.id === ax.id);
                const strokeCol = isHov ? ax.hov : ax.col;

                ctx.save();
                ctx.strokeStyle = 'rgba(10, 12, 16, 0.84)';
                ctx.lineWidth = 4.8;
                ctx.beginPath();
                ctx.moveTo(pCenter.sx, pCenter.sy);
                ctx.lineTo(pTip.sx, pTip.sy);
                ctx.stroke();

                ctx.strokeStyle = strokeCol;
                ctx.lineWidth = isHov ? 3.0 : 2.3;
                ctx.beginPath();
                ctx.moveTo(pCenter.sx, pCenter.sy);
                ctx.lineTo(pTip.sx, pTip.sy);
                ctx.stroke();

                // Box-tip scale handle
                const hs = isHov ? 7.5 : 6.5;
                ctx.fillStyle = isHov ? strokeCol : 'rgba(16, 19, 24, 0.94)';
                ctx.strokeStyle = strokeCol;
                ctx.lineWidth = 2.0;
                ctx.beginPath();
                ctx.rect(pTip.sx - hs, pTip.sy - hs, hs * 2, hs * 2);
                ctx.fill();
                ctx.stroke();

                ctx.fillStyle = isHov ? '#101412' : strokeCol;
                ctx.font = '700 9px ui-sans-serif, system-ui, sans-serif';
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                ctx.fillText(ax.label, pTip.sx, pTip.sy + 0.5);
                ctx.restore();

                this._screenHandles.push({
                    id: ax.id,
                    kind: 'trs_scale_axis',
                    scaleKey: ax.scaleKey,
                    axisVec: ax.vec,
                    gizmoLen,
                    centerWorld: centerW,
                    anchorWorld: tipW,
                    sx: pTip.sx,
                    sy: pTip.sy,
                    hitRadius: 15
                });
            }

            // Center Uniform Scale Cube
            const isHovUni = (this._hoveredHandle?.id === 'trs_sc_uni') || (this._gizmoDrag?.handle?.id === 'trs_sc_uni');
            const us = isHovUni ? 9.0 : 7.8;
            ctx.save();
            ctx.fillStyle = isHovUni ? '#ffffff' : '#eff4cf';
            ctx.strokeStyle = '#12151a';
            ctx.lineWidth = 2.0;
            ctx.beginPath();
            ctx.rect(pCenter.sx - us, pCenter.sy - us, us * 2, us * 2);
            ctx.fill();
            ctx.stroke();
            ctx.fillStyle = '#12151a';
            ctx.font = '700 9.5px ui-sans-serif, system-ui, sans-serif';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            ctx.fillText('S', pCenter.sx, pCenter.sy + 0.5);
            ctx.restore();

            this._screenHandles.push({
                id: 'trs_sc_uni',
                kind: 'trs_scale_uniform',
                centerWorld: centerW,
                anchorWorld: centerW,
                sx: pCenter.sx,
                sy: pCenter.sy,
                hitRadius: 14
            });

            if (this._gizmoDrag && (this._gizmoDrag.handle.kind === 'trs_scale_axis' || this._gizmoDrag.handle.kind === 'trs_scale_uniform'))
            {
                const kIdx = this._gizmoDrag.instIndex ?? -1;
                const ov = kIdx >= 0 ? layer.generator?.instanceOverrides?.[kIdx] : null;
                const txt = ov
                    ? `Rock #${kIdx + 1} Scale  X ${(ov.scaleX ?? 1).toFixed(2)}× · Y ${(ov.scaleY ?? 1).toFixed(2)}× · Z ${(ov.scaleZ ?? 1).toFixed(2)}×`
                    : `Scale  X ${(p.scaleX ?? 1).toFixed(2)}× · Y ${(p.scaleY ?? 1).toFixed(2)}× · Z ${(p.scaleZ ?? 1).toFixed(2)}×`;
                this._drawGizmoReadoutPill(ctx, pCenter.sx, pCenter.sy + 26, txt, ov ? '#7dd3fc' : '#eff4cf');
            }
        }
    }

    _drawGizmoReadoutPill(ctx, sx, sy, text, accent = '#eff4cf')
    {
        ctx.save();
        ctx.font = '600 10.5px ui-sans-serif, system-ui, sans-serif';
        const tw = ctx.measureText(text).width + 16;
        ctx.fillStyle = 'rgba(13, 16, 22, 0.90)';
        ctx.strokeStyle = accent;
        ctx.lineWidth = 1.2;
        ctx.beginPath();
        ctx.roundRect(sx - tw * 0.5, sy - 10, tw, 20, 10);
        ctx.fill();
        ctx.stroke();
        ctx.fillStyle = accent;
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(text, sx, sy);
        ctx.restore();
    }

    _renderGizmoOverlay(cam)
    {
        const ov = this.overlayCanvas;
        const ctx = this.ctx2d;
        if (!ov || !ctx) return;

        const dpr = Math.min(window.devicePixelRatio || 1, 2);
        const cssW = Math.max(1, ov.clientWidth || this.canvas.clientWidth);
        const cssH = Math.max(1, ov.clientHeight || this.canvas.clientHeight);
        const pw = Math.max(1, Math.floor(cssW * dpr));
        const ph = Math.max(1, Math.floor(cssH * dpr));
        if (ov.width !== pw || ov.height !== ph)
        {
            ov.width = pw;
            ov.height = ph;
        }

        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        ctx.clearRect(0, 0, cssW, cssH);

        this._screenHandles = [];
        this._screenSplineSegs = [];
        this._screenRotateSegs = [];

        const layer = this.activeLayer;
        if (!this.showGizmos || !layer || !layer.enabled) return;

        const { R, pivot } = this._getLayerTransform(layer);
        const lSx = clamp(layer.params?.scaleX ?? 1.0, 0.12, 8.0);
        const lSy = clamp(layer.params?.scaleY ?? 1.0, 0.12, 8.0);
        const lSz = clamp(layer.params?.scaleZ ?? 1.0, 0.12, 8.0);

        const compLayer = this.compiledStack?.compiledLayers?.find(c => c.id === layer.id);
        const lBounds = compLayer?.bounds || null;
        const g = layer.generator || { type: 'none' };

        // Clamp / validate per-instance selection against current generator count
        const totalGenCount = compLayer?.rawInstanceCount ?? 1;
        if (g.type === 'none' || totalGenCount <= 1 || this.selectedInstanceIndex >= totalGenCount)
        {
            this.selectedInstanceIndex = -1;
        }
        const selInstIdx = this.selectedInstanceIndex;
        const selInstEntry = (selInstIdx >= 0 && compLayer?.instanceBounds) ? compLayer.instanceBounds[selInstIdx] : null;

        // PER-INSTANCE SUB-ROCK EDIT MODE (Move / Rotate / Scale individual rock #k inside Array / Spline / Arch / Cloud / Scatter)
        if (selInstIdx >= 0)
        {
            this._drawSelectionBounds(ctx, cam, cssW, cssH, lBounds, null, 'rgba(239, 244, 207, 0.16)');

            // Draw sibling rock quick-pick markers so user can hop between individual rocks in 1 click
            if (compLayer?.instanceBounds)
            {
                ctx.save();
                for (const [kStr, ib] of Object.entries(compLayer.instanceBounds))
                {
                    const kNum = Number(kStr);
                    if (kNum === selInstIdx || !ib) continue;
                    const pSib = this._projectWorldPoint(ib.cx, ib.cy, ib.cz, cam, cssW, cssH);
                    if (!pSib) continue;
                    const hasOv = !!(g.instanceOverrides && g.instanceOverrides[kNum]);
                    const isHovSib = this._hoveredHandle?.id === `sel_inst_${kNum}`;
                    ctx.fillStyle = isHovSib ? '#7dd3fc' : (hasOv ? 'rgba(56, 189, 248, 0.88)' : 'rgba(239, 244, 207, 0.55)');
                    ctx.strokeStyle = 'rgba(10, 12, 16, 0.85)';
                    ctx.lineWidth = 1.5;
                    ctx.beginPath();
                    ctx.arc(pSib.sx, pSib.sy, isHovSib ? 6.5 : 4.5, 0, Math.PI * 2);
                    ctx.fill();
                    ctx.stroke();

                    this._screenHandles.push({
                        id: `sel_inst_${kNum}`,
                        kind: 'select_instance',
                        instanceIndex: kNum,
                        anchorWorld: [ib.cx, ib.cy, ib.cz],
                        sx: pSib.sx,
                        sy: pSib.sy,
                        hitRadius: 10
                    });
                }
                ctx.restore();
            }

            if (selInstEntry)
            {
                const instLabel = `Rock #${selInstIdx + 1} / ${totalGenCount} · Esc → Layer`;
                this._drawSelectionBounds(ctx, cam, cssW, cssH, selInstEntry, instLabel, 'rgba(56, 189, 248, 0.88)', '#7dd3fc');
                const instTool = this.gizmoToolMode === 'path' ? 'translate' : this.gizmoToolMode;
                this._renderTrsGizmo(
                    ctx, cam, cssW, cssH, layer, instTool,
                    [selInstEntry.cx, selInstEntry.cy, selInstEntry.cz],
                    selInstEntry.worldR || R
                );
            }
            return;
        }

        this._drawSelectionBounds(ctx, cam, cssW, cssH, lBounds, layer.name);

        const effectiveTool = (this.gizmoToolMode === 'path' && g.type === 'none')
            ? 'translate'
            : this.gizmoToolMode;

        if (effectiveTool !== 'path')
        {
            const centerW = lBounds
                ? [lBounds.cx, lBounds.cy, lBounds.cz]
                : [pivot[0], pivot[1] + Math.max(0.8, (layer.params?.height ?? 3.0) * 0.5), pivot[2]];
            this._renderTrsGizmo(ctx, cam, cssW, cssH, layer, effectiveTool, centerW, R);
            return;
        }

        const projLocal = (lx, ly, lz) =>
        {
            const w = forwardRock2Point(R, pivot, lx * lSx, ly * lSy, lz * lSz);
            return this._projectWorldPoint(w[0], w[1], w[2], cam, cssW, cssH);
        };

        // Draw generated rock instance markers + orientation ticks (customized rocks get a cyan ring!)
        const evalRes = evaluateRock2GeneratorInstances(g, layer.params, layer.type);
        const floatHover = layer.type === 'floating_rock' ? Math.max(0.0, layer.params?.hoverHeight ?? 2.4) : 0.0;
        ctx.save();
        for (let i = 0; i < evalRes.instances.length; i++)
        {
            const inst = evalRes.instances[i];
            const iy = inst.pos[1] + floatHover;
            const p0 = projLocal(inst.pos[0], iy, inst.pos[2]);
            if (!p0) continue;
            if (inst.hasOverride)
            {
                ctx.save();
                ctx.strokeStyle = '#38bdf8';
                ctx.lineWidth = 1.6;
                ctx.beginPath();
                ctx.arc(p0.sx, p0.sy, 6.2, 0, Math.PI * 2);
                ctx.stroke();
                ctx.restore();
            }
            if (floatHover > 0.1)
            {
                const pGround = projLocal(inst.pos[0], inst.pos[1], inst.pos[2]);
                if (pGround)
                {
                    ctx.save();
                    ctx.strokeStyle = 'rgba(124, 212, 248, 0.36)';
                    ctx.setLineDash([3, 3]);
                    ctx.lineWidth = 1.1;
                    ctx.beginPath();
                    ctx.moveTo(pGround.sx, pGround.sy);
                    ctx.lineTo(p0.sx, p0.sy);
                    ctx.stroke();
                    ctx.restore();
                }
            }
            const yawRad = inst.yawDeg * DEG2RAD;
            const tickLen = 0.42;
            const pDir = projLocal(
                inst.pos[0] + Math.sin(yawRad) * tickLen,
                iy,
                inst.pos[2] + Math.cos(yawRad) * tickLen
            );
            if (pDir)
            {
                ctx.strokeStyle = floatHover > 0.1 ? 'rgba(124, 212, 248, 0.58)' : 'rgba(239, 244, 207, 0.42)';
                ctx.lineWidth = 1.4;
                ctx.beginPath();
                ctx.moveTo(p0.sx, p0.sy);
                ctx.lineTo(pDir.sx, pDir.sy);
                ctx.stroke();
            }
            ctx.fillStyle = floatHover > 0.1 ? 'rgba(124, 212, 248, 0.86)' : 'rgba(239, 244, 207, 0.72)';
            ctx.beginPath();
            ctx.arc(p0.sx, p0.sy, 2.8, 0, Math.PI * 2);
            ctx.fill();
        }
        ctx.restore();

        // 1. INTERACTIVE 3D SPLINE OVERLAY & KNOT GIZMOS (WALL & 3D ARCH SPAN)
        if (g.type === 'spline')
        {
            const isArch = g.splineMode === 'arch_span';
            const arcTable = buildRock2SplineArcTable(isArch ? { ...g, closedLoop: 0 } : g, isArch ? 24 : 18);
            const samples = arcTable.samples;

            // Ground shadow projection at y = 0 + Upper Crest / Intrados Portal Guide
            ctx.save();
            ctx.setLineDash([4, 4]);
            ctx.strokeStyle = isArch ? 'rgba(125, 211, 252, 0.34)' : 'rgba(239, 244, 207, 0.24)';
            ctx.lineWidth = 1.3;
            ctx.beginPath();
            let startedFloor = false;
            for (let i = 0; i < samples.length; i++)
            {
                const pF = projLocal(samples[i].x, 0.0, samples[i].z);
                if (!pF) { startedFloor = false; continue; }
                if (!startedFloor) { ctx.moveTo(pF.sx, pF.sy); startedFloor = true; }
                else ctx.lineTo(pF.sx, pF.sy);
            }
            ctx.stroke();

            const cliffH = Math.max(0.5, layer.params?.height ?? 3.5);
            ctx.setLineDash([6, 5]);
            ctx.strokeStyle = isArch ? 'rgba(125, 211, 252, 0.52)' : 'rgba(240, 182, 127, 0.36)';
            ctx.lineWidth = 1.35;
            ctx.beginPath();
            let startedCrest = false;
            for (let i = 0; i < samples.length; i++)
            {
                const s = samples[i];
                const yGuide = isArch
                    ? Math.max(0.0, s.y - cliffH * 0.44 * (s.scale ?? 1.0) * (1.0 - (g.archCrownThin ?? 0.22) * Math.sin((i / Math.max(1, samples.length - 1)) * Math.PI)))
                    : (s.y + cliffH * (s.scale ?? 1.0));
                const pC = projLocal(s.x, yGuide, s.z);
                if (!pC) { startedCrest = false; continue; }
                if (!startedCrest) { ctx.moveTo(pC.sx, pC.sy); startedCrest = true; }
                else ctx.lineTo(pC.sx, pC.sy);
            }
            ctx.stroke();

            if (isArch && samples.length >= 3)
            {
                const sStart = samples[0];
                const sEnd = samples[samples.length - 1];
                let apexSample = samples[Math.floor(samples.length * 0.5)];
                for (let i = 0; i < samples.length; i++)
                {
                    if (samples[i].y > apexSample.y) apexSample = samples[i];
                }
                const spanM = (Math.hypot(sEnd.x - sStart.x, sEnd.z - sStart.z) * lSx).toFixed(1);
                const apexM = (apexSample.y * lSy).toFixed(1);
                const pApex = projLocal(apexSample.x, apexSample.y + cliffH * 0.65, apexSample.z);
                if (pApex)
                {
                    this._drawGizmoReadoutPill(ctx, pApex.sx, pApex.sy - 14, `Arch Span ${spanM}m · Apex ${apexM}m`, '#f0b67f');
                }
            }
            ctx.restore();

            // Outward cliff-face normal ticks along the spline
            if ((g.alignToTangent ?? 1) > 0.5)
            {
                ctx.save();
                ctx.strokeStyle = 'rgba(240, 182, 127, 0.55)';
                ctx.lineWidth = 1.4;
                const stride = Math.max(2, Math.floor(samples.length / 10));
                for (let i = stride; i < samples.length - 1; i += stride)
                {
                    const s = samples[i];
                    const nx = -s.tz, nz = s.tx;
                    const a = projLocal(s.x, s.y, s.z);
                    const b = projLocal(s.x + nx * 0.55, s.y, s.z + nz * 0.55);
                    if (a && b)
                    {
                        ctx.beginPath();
                        ctx.moveTo(a.sx, a.sy);
                        ctx.lineTo(b.sx, b.sy);
                        ctx.stroke();
                    }
                }
                ctx.restore();
            }

            // Main 3D Catmull-Rom Spline Curve (dark halo + bright studio lime-gold stroke)
            const pts2D = samples.map(s => projLocal(s.x, s.y, s.z));
            for (let i = 0; i < samples.length - 1; i++)
            {
                const a = pts2D[i], b = pts2D[i + 1];
                if (!a || !b) continue;
                this._screenSplineSegs.push({
                    x0: a.sx, y0: a.sy,
                    x1: b.sx, y1: b.sy,
                    segIndex: samples[i].segIndex,
                    l0: [samples[i].x, samples[i].y, samples[i].z],
                    l1: [samples[i + 1].x, samples[i + 1].y, samples[i + 1].z],
                    s0: samples[i].scale,
                    s1: samples[i + 1].scale
                });
            }

            const strokeSpline = (color, width) =>
            {
                ctx.strokeStyle = color;
                ctx.lineWidth = width;
                ctx.lineJoin = 'round';
                ctx.lineCap = 'round';
                ctx.beginPath();
                let open = false;
                for (let i = 0; i < pts2D.length; i++)
                {
                    const p = pts2D[i];
                    if (!p) { open = false; continue; }
                    if (!open) { ctx.moveTo(p.sx, p.sy); open = true; }
                    else ctx.lineTo(p.sx, p.sy);
                }
                ctx.stroke();
            };

            ctx.save();
            strokeSpline('rgba(10, 12, 16, 0.85)', 5.2);
            strokeSpline('#eff4cf', 2.4);
            ctx.restore();

            // Double-click curve insert preview badge
            if (this._hoveredCurveInsert && !this._gizmoDrag)
            {
                const hi = this._hoveredCurveInsert;
                ctx.save();
                ctx.fillStyle = '#52d69c';
                ctx.strokeStyle = '#101412';
                ctx.lineWidth = 2;
                ctx.beginPath();
                ctx.arc(hi.px, hi.py, 7.5, 0, Math.PI * 2);
                ctx.fill();
                ctx.stroke();
                ctx.fillStyle = '#0d120f';
                ctx.font = '700 11px ui-sans-serif, system-ui, sans-serif';
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                ctx.fillText('+', hi.px, hi.py + 0.5);
                ctx.restore();
            }

            // Knot Gizmos (XZ planar move + Y vertical elevation arrow)
            const knots = arcTable.knots;
            for (let k = 0; k < knots.length; k++)
            {
                const kn = knots[k];
                this._drawHandleWithElevationArrow(ctx, cam, cssW, cssH, R, pivot, kn.x, kn.y, kn.z, {
                    idXZ: `sp_xz_${k}`,
                    idY: `sp_y_${k}`,
                    kindXZ: 'spline_knot_xz',
                    kindY: 'spline_knot_y',
                    knotIndex: k,
                    selected: k === this.selectedKnotIndex,
                    label: String(k + 1),
                    accent: '#eff4cf'
                });
            }
            return;
        }

        // 2. INTERACTIVE 3D ARRAY OVERLAY & ENDPOINT / EXTENT GIZMOS
        if (g.type === 'array')
        {
            const mode = (g.arrayMode === 'linear' || !g.arrayMode) ? 'line' : g.arrayMode;

            if (mode === 'line')
            {
                const ax = g.startX ?? -4.8, ay = g.startY ?? 0.0, az = g.startZ ?? 0.0;
                const bx = g.endX ?? 4.8,    by = g.endY ?? 0.0,   bz = g.endZ ?? 0.0;
                const pA = projLocal(ax, ay, az);
                const pB = projLocal(bx, by, bz);

                // Lateral jitter corridor preview lines
                const lat = g.lateralJitter ?? 0.24;
                if (lat > 0.02)
                {
                    const dx = bx - ax, dz = bz - az;
                    const hl = Math.hypot(dx, dz) || 1;
                    const nx = -dz / hl, nz = dx / hl;
                    ctx.save();
                    ctx.setLineDash([4, 5]);
                    ctx.strokeStyle = 'rgba(125, 211, 252, 0.28)';
                    ctx.lineWidth = 1.2;
                    for (const sgn of [-1, 1])
                    {
                        const c0 = projLocal(ax + nx * lat * sgn, ay, az + nz * lat * sgn);
                        const c1 = projLocal(bx + nx * lat * sgn, by, bz + nz * lat * sgn);
                        if (c0 && c1)
                        {
                            ctx.beginPath();
                            ctx.moveTo(c0.sx, c0.sy);
                            ctx.lineTo(c1.sx, c1.sy);
                            ctx.stroke();
                        }
                    }
                    ctx.restore();
                }

                if (pA && pB)
                {
                    ctx.save();
                    ctx.strokeStyle = 'rgba(10, 12, 16, 0.82)';
                    ctx.lineWidth = 4.6;
                    ctx.beginPath();
                    ctx.moveTo(pA.sx, pA.sy);
                    ctx.lineTo(pB.sx, pB.sy);
                    ctx.stroke();

                    ctx.setLineDash([7, 5]);
                    ctx.strokeStyle = '#7dd3fc';
                    ctx.lineWidth = 2.3;
                    ctx.beginPath();
                    ctx.moveTo(pA.sx, pA.sy);
                    ctx.lineTo(pB.sx, pB.sy);
                    ctx.stroke();
                    ctx.setLineDash([]);

                    // Midpoint meter & rock count readout pill
                    const mx = (pA.sx + pB.sx) * 0.5;
                    const my = (pA.sy + pB.sy) * 0.5 - 14;
                    const distM = Math.hypot(bx - ax, by - ay, bz - az).toFixed(1);
                    const tag = `${distM}m · ×${evalRes.instances.length} varied`;
                    ctx.font = '600 10px ui-sans-serif, system-ui, sans-serif';
                    const tw = ctx.measureText(tag).width + 12;
                    ctx.fillStyle = 'rgba(14, 18, 24, 0.86)';
                    ctx.strokeStyle = 'rgba(125, 211, 252, 0.45)';
                    ctx.lineWidth = 1;
                    ctx.beginPath();
                    ctx.roundRect(mx - tw * 0.5, my - 9, tw, 18, 9);
                    ctx.fill();
                    ctx.stroke();
                    ctx.fillStyle = '#bae6fd';
                    ctx.textAlign = 'center';
                    ctx.textBaseline = 'middle';
                    ctx.fillText(tag, mx, my);
                    ctx.restore();
                }

                this._drawHandleWithElevationArrow(ctx, cam, cssW, cssH, R, pivot, ax, ay, az, {
                    idXZ: 'arr_a_xz', idY: 'arr_a_y',
                    kindXZ: 'array_start_xz', kindY: 'array_start_y',
                    selected: true, label: 'A', accent: '#7dd3fc'
                });
                this._drawHandleWithElevationArrow(ctx, cam, cssW, cssH, R, pivot, bx, by, bz, {
                    idXZ: 'arr_b_xz', idY: 'arr_b_y',
                    kindXZ: 'array_end_xz', kindY: 'array_end_y',
                    selected: true, label: 'B', accent: '#7dd3fc'
                });
            }
            else if (mode === 'grid2d' || mode === 'grid3d')
            {
                const halfX = Math.max(0.4, ((Math.round(g.countX ?? 3) - 1) * (g.stepX ?? 2.4)) * 0.5);
                const halfZ = Math.max(0.4, ((Math.round(g.countZ ?? 3) - 1) * (g.stepZ ?? 2.4)) * 0.5);
                const corners = [
                    projLocal(-halfX, 0, -halfZ),
                    projLocal( halfX, 0, -halfZ),
                    projLocal( halfX, 0,  halfZ),
                    projLocal(-halfX, 0,  halfZ)
                ];
                if (corners.every(Boolean))
                {
                    ctx.save();
                    ctx.setLineDash([6, 4]);
                    ctx.strokeStyle = '#7dd3fc';
                    ctx.lineWidth = 1.8;
                    ctx.beginPath();
                    ctx.moveTo(corners[0].sx, corners[0].sy);
                    for (let i = 1; i < 4; i++) ctx.lineTo(corners[i].sx, corners[i].sy);
                    ctx.closePath();
                    ctx.stroke();
                    ctx.restore();
                }
                this._drawHandleWithElevationArrow(ctx, cam, cssW, cssH, R, pivot, halfX, 0, 0, {
                    idXZ: 'arr_gx', kindXZ: 'array_grid_x', selected: true, label: 'X', accent: '#7dd3fc'
                });
                this._drawHandleWithElevationArrow(ctx, cam, cssW, cssH, R, pivot, 0, 0, halfZ, {
                    idXZ: 'arr_gz', kindXZ: 'array_grid_z', selected: true, label: 'Z', accent: '#7dd3fc'
                });
            }
            else if (mode === 'hex' || mode === 'radial')
            {
                const rad = mode === 'hex'
                    ? Math.max(0.5, Math.round(g.hexRings ?? 2) * (g.cellSpacing ?? 1.02))
                    : Math.max(0.5, g.ringRadius ?? 3.8);
                ctx.save();
                ctx.setLineDash([5, 4]);
                ctx.strokeStyle = '#7dd3fc';
                ctx.lineWidth = 1.8;
                ctx.beginPath();
                let started = false;
                const steps = mode === 'hex' ? 6 : 40;
                for (let s = 0; s <= steps; s++)
                {
                    const a = (s / steps) * Math.PI * 2;
                    const p = projLocal(Math.sin(a) * rad, 0, Math.cos(a) * rad);
                    if (!p) { started = false; continue; }
                    if (!started) { ctx.moveTo(p.sx, p.sy); started = true; }
                    else ctx.lineTo(p.sx, p.sy);
                }
                ctx.stroke();
                ctx.restore();

                this._drawHandleWithElevationArrow(ctx, cam, cssW, cssH, R, pivot, rad, 0, 0, {
                    idXZ: 'arr_rad',
                    kindXZ: mode === 'hex' ? 'array_hex_spacing' : 'array_ring_radius',
                    selected: true, label: 'R', accent: '#7dd3fc'
                });
            }
            return;
        }

        // 3. INTERACTIVE 3D SPLATTER & SCATTER FIELD HANDLES
        if (g.type === 'splatter')
        {
            const rad = Math.max(0.5, g.outerRadius ?? 3.8);
            const isFan = g.splatterMode === 'talus_fan';
            const az = (g.fanAzimuthDeg ?? 0) * DEG2RAD;

            ctx.save();
            ctx.setLineDash([5, 4]);
            ctx.strokeStyle = '#f0b67f';
            ctx.lineWidth = 1.8;
            ctx.beginPath();
            if (isFan)
            {
                const halfSp = ((g.fanSpreadDeg ?? 110) * 0.5) * DEG2RAD;
                const c0 = projLocal(0, 0, 0);
                if (c0) ctx.moveTo(c0.sx, c0.sy);
                for (let s = 0; s <= 24; s++)
                {
                    const a = az - halfSp + (s / 24) * (halfSp * 2);
                    const p = projLocal(Math.sin(a) * rad, 0, Math.cos(a) * rad);
                    if (p) ctx.lineTo(p.sx, p.sy);
                }
                if (c0) ctx.lineTo(c0.sx, c0.sy);
            }
            else
            {
                let started = false;
                for (let s = 0; s <= 40; s++)
                {
                    const a = (s / 40) * Math.PI * 2;
                    const p = projLocal(Math.sin(a) * rad, 0, Math.cos(a) * rad);
                    if (!p) { started = false; continue; }
                    if (!started) { ctx.moveTo(p.sx, p.sy); started = true; }
                    else ctx.lineTo(p.sx, p.sy);
                }
            }
            ctx.stroke();
            ctx.restore();

            const hx = isFan ? Math.sin(az) * rad : rad;
            const hz = isFan ? Math.cos(az) * rad : 0;
            this._drawHandleWithElevationArrow(ctx, cam, cssW, cssH, R, pivot, hx, 0, hz, {
                idXZ: 'splat_r', kindXZ: 'splatter_radius', selected: true, label: 'R', accent: '#f0b67f'
            });
            return;
        }

        if (g.type === 'scatter')
        {
            const hw = Math.max(0.5, (g.scatterWidth ?? 6.8) * 0.5);
            const hl = Math.max(0.5, (g.scatterLength ?? 6.2) * 0.5);
            const corners = [
                projLocal(-hw, 0, -hl),
                projLocal( hw, 0, -hl),
                projLocal( hw, 0,  hl),
                projLocal(-hw, 0,  hl)
            ];
            if (corners.every(Boolean))
            {
                ctx.save();
                ctx.setLineDash([5, 4]);
                ctx.strokeStyle = '#c8d99b';
                ctx.lineWidth = 1.8;
                ctx.beginPath();
                ctx.moveTo(corners[0].sx, corners[0].sy);
                for (let i = 1; i < 4; i++) ctx.lineTo(corners[i].sx, corners[i].sy);
                ctx.closePath();
                ctx.stroke();
                ctx.restore();
            }
            this._drawHandleWithElevationArrow(ctx, cam, cssW, cssH, R, pivot, hw * lSx, 0, 0, {
                idXZ: 'scat_w', kindXZ: 'scatter_width', selected: true, label: 'W', accent: '#c8d99b'
            });
            this._drawHandleWithElevationArrow(ctx, cam, cssW, cssH, R, pivot, 0, 0, hl * lSz, {
                idXZ: 'scat_l', kindXZ: 'scatter_length', selected: true, label: 'L', accent: '#c8d99b'
            });
            return;
        }

        // 4. INTERACTIVE 3D CLOUD / FLOATING SATELLITES VOLUMETRIC GIZMO
        if (g.type === 'cloud')
        {
            const cx = g.centerX ?? 0.0;
            const cy = g.centerY ?? 3.8;
            const cz = g.centerZ ?? 0.0;
            const rad = Math.max(0.4, g.radiusXZ ?? 4.8);
            const sprY = Math.max(0.15, g.spreadY ?? 2.1);
            const innerClear = clamp(g.innerClearance ?? 0.42, 0.0, 0.92);
            const tiltRad = (g.orbitTiltDeg ?? 14) * DEG2RAD;
            const cosT = Math.cos(tiltRad), sinT = Math.sin(tiltRad);

            const drawTiltedRing = (ringR, strokeCol, dash) =>
            {
                ctx.save();
                ctx.setLineDash(dash);
                ctx.strokeStyle = strokeCol;
                ctx.lineWidth = 1.75;
                ctx.beginPath();
                let started = false;
                for (let s = 0; s <= 48; s++)
                {
                    const a = (s / 48) * Math.PI * 2.0;
                    const xOrb = Math.cos(a) * ringR;
                    const zOrb = Math.sin(a) * ringR;
                    const ox = xOrb * cosT;
                    const oy = xOrb * sinT;
                    const p = projLocal(cx + ox, cy + oy, cz + zOrb);
                    if (!p) { started = false; continue; }
                    if (!started) { ctx.moveTo(p.sx, p.sy); started = true; }
                    else ctx.lineTo(p.sx, p.sy);
                }
                ctx.stroke();
                ctx.restore();
            };

            drawTiltedRing(rad, '#7dd3fc', [6, 4]);
            if (innerClear > 0.08)
            {
                drawTiltedRing(rad * innerClear, 'rgba(125, 211, 252, 0.34)', [3, 4]);
            }

            // Vertical spread envelope line (cy - sprY -> cy + sprY)
            const pBot = projLocal(cx, cy - sprY, cz);
            const pTop = projLocal(cx, cy + sprY, cz);
            if (pBot && pTop)
            {
                ctx.save();
                ctx.setLineDash([4, 4]);
                ctx.strokeStyle = 'rgba(139, 233, 253, 0.45)';
                ctx.lineWidth = 1.4;
                ctx.beginPath();
                ctx.moveTo(pBot.sx, pBot.sy);
                ctx.lineTo(pTop.sx, pTop.sy);
                ctx.stroke();
                ctx.restore();
            }

            // Center XZ + Y altitude handle ('C')
            this._drawHandleWithElevationArrow(ctx, cam, cssW, cssH, R, pivot, cx * lSx, cy * lSy, cz * lSz, {
                idXZ: 'cld_c_xz', idY: 'cld_c_y',
                kindXZ: 'cloud_center_xz', kindY: 'cloud_center_y',
                selected: true, label: 'C', accent: '#7dd3fc'
            });

            // Outer Orbital Radius handle ('R')
            this._drawHandleWithElevationArrow(ctx, cam, cssW, cssH, R, pivot, (cx + rad * cosT) * lSx, (cy + rad * sinT) * lSy, cz * lSz, {
                idXZ: 'cld_r_xz',
                kindXZ: 'cloud_radius_xz',
                selected: true, label: 'R', accent: '#38bdf8'
            });

            // Upper Spread Y handle ('H')
            this._drawHandleWithElevationArrow(ctx, cam, cssW, cssH, R, pivot, cx * lSx, (cy + sprY) * lSy, cz * lSz, {
                idXZ: 'cld_h_base', idY: 'cld_h_y',
                kindXZ: 'cloud_center_xz', kindY: 'cloud_spread_y',
                selected: false, label: 'H', accent: '#a5f3fc'
            });
        }
    }

    render()
    {
        const gl = this.gl;
        if (!gl || !this.compiledStack) return;

        const dpr = Math.min(window.devicePixelRatio || 1, 2);
        const w = Math.max(1, Math.floor(this.canvas.clientWidth * dpr));
        const h = Math.max(1, Math.floor(this.canvas.clientHeight * dpr));
        if (this.canvas.width !== w || this.canvas.height !== h)
        {
            this.canvas.width = w;
            this.canvas.height = h;
        }

        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(0, 0, w, h);

        const cam = this._computeCameraBasis();
        const { camPos, fwd, right, up } = cam;
        const sunDir = this.compiledStack.sunDir || [0.5, 0.7, 0.5];

        if (this.renderMode === 'qef-mesh' && this.meshIndexCount > 0)
        {
            gl.clearColor(0.068, 0.074, 0.086, 1.0);
            gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
            gl.enable(gl.DEPTH_TEST);

            const vp = this._buildViewProjMatrix(camPos, fwd, right, up, w / h);
            gl.useProgram(this.meshProg);
            gl.uniformMatrix4fv(gl.getUniformLocation(this.meshProg, 'uViewProj'), false, vp);
            gl.uniform3fv(gl.getUniformLocation(this.meshProg, 'uCamPos'), camPos);
            gl.uniform3fv(gl.getUniformLocation(this.meshProg, 'uSunDir'), sunDir);
            gl.uniform1f(gl.getUniformLocation(this.meshProg, 'uExposure'), this.exposure);
            gl.uniform1i(gl.getUniformLocation(this.meshProg, 'uLens'), this.lens);
            gl.uniform1i(gl.getUniformLocation(this.meshProg, 'uWirePass'), 0);

            gl.bindVertexArray(this.meshVao);
            gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.iboTris);
            gl.drawElements(gl.TRIANGLES, this.meshIndexCount, gl.UNSIGNED_INT, 0);

            if (this.showWireframe && this.wireIndexCount > 0)
            {
                gl.enable(gl.BLEND);
                gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
                gl.uniform1i(gl.getUniformLocation(this.meshProg, 'uWirePass'), 1);
                gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.iboWire);
                gl.drawElements(gl.LINES, this.wireIndexCount, gl.UNSIGNED_INT, 0);
                gl.disable(gl.BLEND);
            }
            gl.bindVertexArray(null);
        }
        else
        {
            gl.disable(gl.DEPTH_TEST);
            gl.useProgram(this.rayProg);
            gl.activeTexture(gl.TEXTURE0);
            gl.bindTexture(gl.TEXTURE_3D, this.sdfVolTex);
            gl.uniform1i(gl.getUniformLocation(this.rayProg, 'uSdfVol'), 0);
            gl.uniform3fv(gl.getUniformLocation(this.rayProg, 'uVolMin'), this.volMin);
            gl.uniform3fv(gl.getUniformLocation(this.rayProg, 'uVolMax'), this.volMax);
            gl.uniform3fv(gl.getUniformLocation(this.rayProg, 'uCamPos'), camPos);
            gl.uniform3fv(gl.getUniformLocation(this.rayProg, 'uCamFwd'), fwd);
            gl.uniform3fv(gl.getUniformLocation(this.rayProg, 'uCamRight'), right);
            gl.uniform3fv(gl.getUniformLocation(this.rayProg, 'uCamUp'), up);
            gl.uniform2f(gl.getUniformLocation(this.rayProg, 'uResolution'), w, h);
            gl.uniform3fv(gl.getUniformLocation(this.rayProg, 'uSunDir'), sunDir);
            gl.uniform1f(gl.getUniformLocation(this.rayProg, 'uExposure'), this.exposure);
            gl.uniform1i(gl.getUniformLocation(this.rayProg, 'uLens'), this.lens);

            gl.bindVertexArray(this.emptyVao);
            gl.drawArrays(gl.TRIANGLES, 0, 3);
            gl.bindVertexArray(null);
        }

        this._renderGizmoOverlay(cam);
    }
}

