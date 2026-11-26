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
import { evalRock2StackSdf, packRock2StackGpuBuffers } from './RockFormationStackEngine.js';

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
        float pRel = (h - 0.34) / 0.25;
        float sPinch = 1.0 - d0.w * exp(-pRel * pRel);
        float s = clamp(sTap * sPinch, 0.16, 2.40);
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
        for (int k = 0; k < 128; k++) {
            if (k >= bCount) break;
            int bi = bStart + k;
            vec4 sph = texelFetch(uBlockTex, ivec2(0, bi), 0);
            float lb = length(p - sph.xyz) - sph.w;
            if (lb > dLyr + intraBlend) continue;
            float dP = evalPlainPrimitive(bi, p);
            dLyr = (dLyr > 1e8) ? dP : (intraBlend > 1e-4 ? sminPoly(dLyr, dP, intraBlend) : min(dLyr, dP));
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
    float res = 1.0;
    float t = 0.06;
    for (int i = 0; i < 26; i++) {
        vec3 sp = ro + rd * t;
        if (any(lessThan(sp, uVolMin)) || any(greaterThan(sp, uVolMax))) break;
        float h = evalD(sp);
        if (h < 0.003) return 0.14;
        res = min(res, 8.0 * h / t);
        t += clamp(h * 0.85, 0.03, 0.45);
        if (t > 12.0) break;
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

        // Soft floor contact shadow from the 3D SDF volume!
        float fShadow = calcSoftShadow(fp + vec3(0.0, 0.04, 0.0), uSunDir);
        float contactAo = 1.0;
        if (all(greaterThan(fp, uVolMin - vec3(1.0))) && all(lessThan(fp, uVolMax + vec3(1.0)))) {
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
    constructor(canvas)
    {
        this.canvas = canvas;
        this.gl = canvas.getContext('webgl2', { antialias: true, alpha: false, depth: true });
        if (!this.gl)
        {
            console.warn('[RockFormation 2.0] WebGL2 context unavailable.');
            return;
        }

        this.compiledStack = null;
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
            (b.minY + b.maxY) * 0.42,
            (b.minZ + b.maxZ) * 0.5
        ];
        const span = Math.max(3.5, b.maxX - b.minX, b.maxY - b.minY, b.maxZ - b.minZ);
        this.distance = span * 1.38;
        this.yaw = 0.52;
        this.pitch = 0.28;
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
                this.pitch = clamp(this.pitch + dy * 0.0055, -0.25, 1.48);
            }
        });

        const endDrag = () => { dragging = false; };
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

        // right = normalize(cross(fwd, worldUp)) = (-fwd.z, 0, fwd.x)
        const rx = -fwd[2], rz = fwd[0];
        const rl = Math.hypot(rx, rz) || 1;
        const right = [rx / rl, 0, rz / rl];

        // up = cross(right, fwd)
        const up = [
            right[1] * fwd[2] - right[2] * fwd[1],
            right[2] * fwd[0] - right[0] * fwd[2],
            right[0] * fwd[1] - right[1] * fwd[0]
        ];
        return { camPos, fwd, right, up };
    }

    _buildViewProjMatrix(camPos, fwd, right, up, aspect)
    {
        const f = 1.0 / 0.44; // Matches screen * 0.44 FOV in raymarcher!
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

        const { camPos, fwd, right, up } = this._computeCameraBasis();
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
            return;
        }

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
}
