/*==========================================================================================================================================
                                                       SDFRAYMARCHVIEWPORT.JS
==========================================================================================================================================*/
// Real-time WebGL2 Hybrid SDF Raymarching Viewport (RGBA16F 3D Sculpt Volume + 24 Analytic CSG Primitives)
// + 3D Transform Gizmo Overlay + 2D Distance Field Slice Minimap + Software Raycast Fallback.

import {
    Clamp,
    Vec3Sub,
    Vec3Add,
    Vec3Scale,
    Vec3Dot,
    Vec3Cross,
    Vec3Normalize,
    PackCsgUniformArray,
    MAX_CSG_NODES
} from './SdfMath.js';

const VERTEX_SHADER_GLSL = `#version 300 es
precision highp float;
out vec2 vUv;
void main() {
    vec2 pos = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
    vUv = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
`;

const FRAGMENT_SHADER_GLSL = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler3D;

in vec2 vUv;
out vec4 fragColor;

uniform sampler3D uVolumeTex;
uniform vec3 uVolMin;
uniform vec3 uVolMax;
uniform float uVoxelSize;
uniform int uBoundsMode; // 0=Auto-Expand, 1=Soft Cap, 2=Hard Clip
uniform vec3 uRayMin;
uniform vec3 uRayMax;
uniform int uVolumeVisible;

uniform int uCsgCount;
uniform vec4 uCsgData[120]; // 24 nodes * 5 vec4s

uniform vec3 uCamPos;
uniform vec3 uCamFwd;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform float uTanHalfFov;
uniform float uAspect;
uniform int uOrthoMode;
uniform float uOrthoScale;

uniform vec3 uSunDir;
uniform vec4 uMatParams; // roughness, metallic, subsurface, cavity
uniform int uLens;       // 0=PBR, 1=Wax, 2=Grey, 3=Jade, 4=Curvature, 5=Slice, 6=Normals
uniform int uShowGrid;
uniform float uGroundY;

uniform int uSliceEnabled;
uniform int uSliceAxis;    // 0=X, 1=Y, 2=Z
uniform float uSliceOffset;

uniform int uBrushActive;
uniform vec3 uBrushPos;
uniform vec3 uBrushNormal;
uniform float uBrushRadius;
uniform vec3 uBrushAccent;
uniform ivec4 uSymmetry; // x, y, z, radialCount

vec3 rotateQuat(vec3 v, vec4 q) {
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

float sdPrimitive(int shapeId, vec3 p, vec3 s) {
    vec3 ap = abs(p);
    if (shapeId == 0) {
        return length(p) - max(0.01, s.x);
    } else if (shapeId == 1) {
        vec3 q = ap - max(vec3(0.005), s);
        return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
    } else if (shapeId == 2) {
        float h = max(0.001, s.y);
        float cy = p.y - clamp(p.y, -h, h);
        return length(vec3(p.x, cy, p.z)) - max(0.005, s.x);
    } else if (shapeId == 3) {
        vec2 d = vec2(length(p.xz) - max(0.005, s.x), ap.y - max(0.005, s.y));
        return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
    } else if (shapeId == 4) {
        vec2 q = vec2(length(p.xz) - max(0.01, s.x), p.y);
        return length(q) - max(0.005, s.y);
    } else if (shapeId == 5) {
        float h = max(0.01, s.y);
        float r1 = max(0.005, s.x);
        float r2 = max(0.002, s.z);
        vec2 q = vec2(length(p.xz), p.y);
        vec2 k1 = vec2(r2, h);
        vec2 k2 = vec2(r2 - r1, 2.0 * h);
        vec2 ca = vec2(q.x - min(q.x, (q.y < 0.0) ? r1 : r2), abs(q.y) - h);
        vec2 cb = q - k1 + k2 * clamp(dot(k1 - q, k2) / max(1e-6, dot(k2, k2)), 0.0, 1.0);
        float sg = (cb.x < 0.0 && ca.y < 0.0) ? -1.0 : 1.0;
        return sg * sqrt(min(dot(ca, ca), dot(cb, cb)));
    } else if (shapeId == 6) {
        float sz = max(0.01, s.x);
        float m = ap.x + ap.y + ap.z - sz;
        vec3 q;
        if (3.0 * ap.x < m) q = ap.xyz;
        else if (3.0 * ap.y < m) q = ap.yzx;
        else if (3.0 * ap.z < m) q = ap.zxy;
        else return m * 0.57735027;
        float k = clamp(0.5 * (q.z - q.y + sz), 0.0, sz);
        return length(vec3(q.x, q.y - sz + k, q.z - k));
    } else if (shapeId == 7) {
        const vec3 k = vec3(-0.8660254, 0.5, 0.57735027);
        vec2 xz = ap.xz;
        xz -= 2.0 * min(dot(k.xy, xz), 0.0) * k.xy;
        float hx = max(0.01, s.x);
        float hy = max(0.005, s.y);
        vec2 d = vec2(
            length(xz - vec2(clamp(xz.x, -k.z * hx, k.z * hx), hx)) * sign(xz.y - hx),
            ap.y - hy
        );
        return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
    } else if (shapeId == 8) {
        vec3 r = max(vec3(0.01), s);
        float k0 = length(p / r);
        if (k0 < 1e-5) return -min(r.x, min(r.y, r.z));
        float k1 = length(p / (r * r));
        return k0 * (k0 - 1.0) / max(1e-5, k1);
    } else if (shapeId == 9) {
        float le = max(0.0, s.y);
        float r1 = max(0.01, s.x);
        float r2 = max(0.005, s.z);
        vec3 q = vec3(p.x, max(ap.y - le, 0.0), p.z);
        return length(vec2(length(q.xy) - r1, q.z)) - r2;
    } else if (shapeId == 10) {
        float r = max(0.02, s.x);
        float w = clamp(s.z, 0.005, r * 0.95);
        float d = r - w;
        vec2 v = ap.xz;
        float b = sqrt(max(1e-6, r * r - d * d));
        float dist2d = ((v.y - b) * d > v.x * b) ? length(v - vec2(0.0, b)) : length(v + vec2(d, 0.0)) - r;
        vec2 dm = vec2(dist2d, ap.y - max(0.005, s.y));
        return min(max(dm.x, dm.y), 0.0) + length(max(dm, 0.0));
    } else {
        vec3 q = ap - max(vec3(0.02), s);
        float boxD = length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
        float freq = 9.5;
        float g = dot(sin(p * freq), cos(p.yzx * freq));
        float gyrD = (abs(g) / freq - 0.042) * 0.75;
        return max(boxD, gyrD);
    }
}

float evalCsgNode(int base, vec3 wp) {
    vec4 d0 = uCsgData[base];
    vec4 d1 = uCsgData[base + 1];
    vec4 d2 = uCsgData[base + 2];
    vec4 d4 = uCsgData[base + 4];

    int packedFlags = int(floor(d4.w / 16.0 + 0.0001));
    float scale = max(0.05, d4.w - float(packedFlags) * 16.0);
    int mirX = packedFlags & 1;
    int mirZ = (packedFlags >> 1) & 1;
    int radialN = packedFlags >> 2;

    vec3 p = wp;
    if (mirX != 0) p.x = abs(p.x);
    if (mirZ != 0) p.z = abs(p.z);
    if (radialN >= 2) {
        float sector = 6.28318530718 / float(radialN);
        float ang = atan(p.z, p.x);
        ang = mod(ang + 0.5 * sector, sector) - 0.5 * sector;
        float rad = length(p.xz);
        p.x = cos(ang) * rad;
        p.z = sin(ang) * rad;
    }

    vec3 lp = rotateQuat(p - d0.xyz, d2) / scale;
    float twist = d4.z;
    if (abs(twist) > 1e-4) {
        float c = cos(twist * lp.y);
        float s = sin(twist * lp.y);
        lp.xz = mat2(c, s, -s, c) * lp.xz;
    }

    float dist = sdPrimitive(int(d0.w + 0.5), lp, d1.xyz);
    if (abs(twist) > 1e-4) dist *= 0.82;
    dist -= d4.x;
    if (d4.y > 1e-4) dist = abs(dist) - d4.y;
    return dist * scale;
}

vec4 combineOp(vec4 acc, float d2, vec3 col2, int opId, float k) {
    float sk = max(1e-4, k);
    float d1 = acc.w;
    vec3 c1 = acc.rgb;
    if (opId == 0) {
        float h = clamp(0.5 + 0.5 * (d2 - d1) / sk, 0.0, 1.0);
        float d = mix(d2, d1, h) - sk * h * (1.0 - h);
        return vec4(mix(col2, c1, h), d);
    } else if (opId == 1) {
        float h = clamp(0.5 - 0.5 * (d1 + d2) / sk, 0.0, 1.0);
        float d = mix(d1, -d2, h) + sk * h * (1.0 - h);
        return vec4(mix(c1, col2, h * 0.88), d);
    } else if (opId == 2) {
        float h = clamp(0.5 - 0.5 * (d2 - d1) / sk, 0.0, 1.0);
        float d = mix(d2, d1, h) + sk * h * (1.0 - h);
        return vec4(mix(col2, c1, h), d);
    } else if (opId == 3) {
        float ch = (d1 - sk + d2) * 0.70710678;
        float d = min(min(d1, d2), ch);
        float h = clamp(0.5 + 0.5 * (d2 - d1) / sk, 0.0, 1.0);
        return vec4(mix(col2, c1, h), d);
    } else {
        float inf = 1.0 - clamp((d2 + sk * 0.25) / sk, 0.0, 1.0);
        return vec4(mix(c1, col2, inf), d1);
    }
}

vec4 sampleVolume(vec3 p) {
    vec3 clamped = clamp(p, uVolMin, uVolMax);
    vec3 ext = p - clamped;
    float extLen = length(ext);
    vec3 uvw = (clamped - uVolMin) / max(vec3(1e-4), uVolMax - uVolMin);
    vec4 tex = texture(uVolumeTex, uvw);
    float d = (extLen > 0.0) ? (max(tex.a, 0.0) + extLen) : tex.a;

    // Smooth rounded boundary cushion prevents flat box-wall slicing when clay touches outer limits
    if (uBoundsMode != 2) {
        float inset = uVoxelSize * 1.85;
        float cornerR = uVoxelSize * 2.4;
        float sk = max(1e-4, uVoxelSize * 1.8);
        vec3 center = 0.5 * (uVolMin + uVolMax);
        vec3 halfExt = max(vec3(0.1), 0.5 * (uVolMax - uVolMin) - vec3(inset + cornerR));
        vec3 q = abs(p - center) - halfExt;
        float boxD = length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - cornerR;
        float h = clamp(0.5 - 0.5 * (boxD - d) / sk, 0.0, 1.0);
        d = mix(boxD, d, h) + sk * h * (1.0 - h);
    }
    return vec4(tex.rgb, d);
}

vec4 mapScene(vec3 p) {
    vec4 res = (uVolumeVisible != 0) ? sampleVolume(p) : vec4(0.68, 0.42, 0.31, 10.0);
    for (int i = 0; i < 24; ++i) {
        if (i >= uCsgCount) break;
        int base = i * 5;
        float nd = evalCsgNode(base, p);
        vec4 d1 = uCsgData[base + 1];
        vec4 d3 = uCsgData[base + 3];
        res = combineOp(res, nd, d3.rgb, int(d3.w + 0.5), d1.w);
    }
    if (uSliceEnabled != 0 && uLens != 5) {
        float coord = (uSliceAxis == 0) ? p.x : (uSliceAxis == 1) ? p.y : p.z;
        float cutPlane = coord - uSliceOffset;
        res.w = max(res.w, cutPlane);
    }
    return res;
}

float mapDist(vec3 p) {
    return mapScene(p).w;
}

vec3 calcNormal(vec3 p) {
    const float h = 0.0045;
    const vec2 k = vec2(1.0, -1.0);
    return normalize(
        k.xyy * mapDist(p + k.xyy * h) +
        k.yyx * mapDist(p + k.yyx * h) +
        k.yxy * mapDist(p + k.yxy * h) +
        k.xxx * mapDist(p + k.xxx * h)
    );
}

float calcCurvature(vec3 p) {
    float e = 0.018;
    float d0 = mapDist(p);
    float dx = mapDist(p + vec3(e, 0.0, 0.0)) + mapDist(p - vec3(e, 0.0, 0.0));
    float dy = mapDist(p + vec3(0.0, e, 0.0)) + mapDist(p - vec3(0.0, e, 0.0));
    float dz = mapDist(p + vec3(0.0, 0.0, e)) + mapDist(p - vec3(0.0, 0.0, e));
    return clamp((dx + dy + dz - 6.0 * d0) / (e * 1.4), -1.0, 1.0);
}

float calcAO(vec3 p, vec3 n) {
    float occ = 0.0;
    float sca = 1.0;
    for (int i = 0; i < 5; ++i) {
        float h = 0.02 + 0.065 * float(i);
        float d = mapDist(p + h * n);
        occ += (h - d) * sca;
        sca *= 0.72;
    }
    return clamp(1.0 - 2.4 * occ, 0.08, 1.0);
}

float calcSoftShadow(vec3 ro, vec3 rd) {
    float res = 1.0;
    float t = 0.03;
    for (int i = 0; i < 28; ++i) {
        float h = mapDist(ro + rd * t);
        if (h < 0.001) return 0.08;
        res = min(res, 10.0 * h / t);
        t += clamp(h, 0.02, 0.22);
        if (t > 4.5) break;
    }
    return clamp(res, 0.08, 1.0);
}

vec3 acesFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 shadeSurface(vec3 p, vec3 n, vec3 v, vec3 albedo) {
    float ao = calcAO(p, n);
    float curv = calcCurvature(p);

    if (uLens == 6) {
        // World Normals + Eikonal diagnostic
        return pow(clamp(n * 0.5 + 0.5, 0.0, 1.0) * (0.75 + 0.25 * ao), vec3(1.0 / 2.2));
    }
    if (uLens == 4) {
        // Mean Curvature & Cavity Lens
        vec3 ridge = vec3(0.95, 0.90, 0.76);
        vec3 flatCol = vec3(0.28, 0.30, 0.33);
        vec3 valley = vec3(0.06, 0.10, 0.16);
        vec3 c = (curv >= 0.0) ? mix(flatCol, ridge, curv) : mix(flatCol, valley, -curv);
        return pow(c * ao, vec3(1.0 / 2.2));
    }
    if (uLens == 1 || uLens == 2 || uLens == 3) {
        // Analytical Studio MatCaps in camera view space
        vec3 vn = normalize(vec3(dot(n, uCamRight), dot(n, uCamUp), dot(n, -uCamFwd)));
        float ndv = clamp(vn.z, 0.0, 1.0);
        float key = clamp(dot(vn, normalize(vec3(-0.42, 0.58, 0.70))), 0.0, 1.0);
        float rim = pow(1.0 - ndv, 3.2);
        vec3 base;
        if (uLens == 1) {
            base = mix(vec3(0.16, 0.03, 0.02), vec3(0.76, 0.24, 0.16), pow(key, 0.75));
            base += vec3(0.95, 0.62, 0.44) * pow(key, 9.0) * 0.42 + vec3(0.35, 0.12, 0.08) * rim;
        } else if (uLens == 2) {
            base = mix(vec3(0.10, 0.105, 0.115), vec3(0.68, 0.69, 0.71), pow(key, 0.85));
            base += vec3(0.85) * pow(key, 16.0) * 0.26 + vec3(0.20, 0.22, 0.25) * rim;
        } else {
            base = mix(vec3(0.03, 0.13, 0.10), vec3(0.26, 0.68, 0.52), pow(key, 0.7));
            base += vec3(0.75, 0.96, 0.86) * pow(key, 12.0) * 0.35 + vec3(0.35, 0.82, 0.66) * rim * 0.85;
        }
        base *= mix(1.0, 0.72 + 0.35 * curv, 0.45) * ao;
        return pow(clamp(base, 0.0, 1.0), vec3(1.0 / 2.2));
    }

    // Lens 0 / 5: Physically-Based GGX + Clay Subsurface Studio Lighting
    float rough = clamp(uMatParams.x, 0.06, 1.0);
    float metal = clamp(uMatParams.y, 0.0, 1.0);
    float subsurf = clamp(uMatParams.z, 0.0, 1.0);
    float cavityAmt = clamp(uMatParams.w, 0.0, 1.0);

    vec3 l = normalize(uSunDir);
    vec3 h = normalize(l + v);
    float ndl = max(dot(n, l), 0.0);
    float ndv = max(dot(n, v), 0.001);
    float ndh = max(dot(n, h), 0.0);
    float vdh = max(dot(v, h), 0.0);

    float shadow = (ndl > 0.005) ? calcSoftShadow(p + n * 0.012, l) : 0.08;

    // Wrap clay subsurface scattering
    float wrapDiff = clamp((dot(n, l) + subsurf * 0.55) / (1.0 + subsurf * 0.55), 0.0, 1.0);
    vec3 sssTint = pow(max(albedo, vec3(0.02)), vec3(1.35)) * vec3(1.25, 0.82, 0.68);

    // GGX microfacet specular
    float a = rough * rough;
    float a2 = a * a;
    float denom = ndh * ndh * (a2 - 1.0) + 1.0;
    float D = a2 / max(1e-5, 3.14159265 * denom * denom);
    vec3 f0 = mix(vec3(0.042), albedo, metal);
    vec3 F = f0 + (1.0 - f0) * pow(1.0 - vdh, 5.0);
    float kG = (rough + 1.0) * (rough + 1.0) * 0.125;
    float G = (ndv / (ndv * (1.0 - kG) + kG)) * (ndl / (ndl * (1.0 - kG) + kG));
    vec3 spec = (D * F * G) / max(1e-4, 4.0 * ndv * max(ndl, 0.001));

    vec3 kd = (1.0 - F) * (1.0 - metal);
    vec3 sunCol = vec3(2.45, 2.24, 1.96);
    vec3 direct = (kd * mix(albedo * ndl, sssTint * wrapDiff, subsurf * 0.55) / 3.14159265 + spec * ndl) * sunCol * shadow;

    // Studio sky hemisphere + ground bounce + rim
    vec3 skyCol = mix(vec3(0.09, 0.085, 0.08), vec3(0.24, 0.27, 0.33), n.y * 0.5 + 0.5);
    vec3 envSpec = mix(vec3(0.18, 0.20, 0.24), vec3(0.55, 0.52, 0.48), clamp(reflect(-v, n).y * 0.5 + 0.5, 0.0, 1.0));
    float fresnelRim = pow(1.0 - ndv, 4.0) * (1.0 - rough * 0.65);

    vec3 ambient = (kd * albedo * skyCol + f0 * envSpec * (1.0 - rough * 0.72) + vec3(0.22, 0.20, 0.18) * fresnelRim) * ao;
    float cavityFactor = mix(1.0, clamp(0.88 + curv * 0.42, 0.45, 1.24), cavityAmt);

    vec3 color = (direct + ambient) * cavityFactor;
    color = acesFilm(color);
    return pow(color, vec3(1.0 / 2.2));
}

float brushRingOverlay(vec3 hitP) {
    if (uBrushActive == 0) return 0.0;
    float acc = 0.0;
    float r = max(0.02, uBrushRadius);
    float band = max(0.006, r * 0.038);

    // Primary + X/Y/Z symmetry cursor rings on surface
    for (int sx = 0; sx < 2; ++sx) {
        if (sx == 1 && uSymmetry.x == 0) continue;
        for (int sy = 0; sy < 2; ++sy) {
            if (sy == 1 && uSymmetry.y == 0) continue;
            for (int sz = 0; sz < 2; ++sz) {
                if (sz == 1 && uSymmetry.z == 0) continue;
                vec3 bp = uBrushPos * vec3(sx == 1 ? -1.0 : 1.0, sy == 1 ? -1.0 : 1.0, sz == 1 ? -1.0 : 1.0);
                float d = length(hitP - bp);
                float outer = 1.0 - smoothstep(0.0, band, abs(d - r));
                float inner = (1.0 - smoothstep(0.0, band * 0.85, abs(d - r * 0.35))) * 0.45;
                float ghostWeight = (sx == 0 && sy == 0 && sz == 0) ? 1.0 : 0.48;
                acc = max(acc, (outer + inner) * ghostWeight);
            }
        }
    }
    return acc;
}

vec3 shadeFieldSlice(vec3 sp) {
    float d = mapScene(sp).w;
    vec3 col = (d > 0.0) ? vec3(0.78, 0.56, 0.28) : vec3(0.20, 0.68, 0.58);
    col *= 1.0 - exp(-4.5 * abs(d));
    col *= 0.78 + 0.22 * cos(95.0 * d);
    col = mix(col, vec3(0.98, 0.97, 0.92), 1.0 - smoothstep(0.0, 0.012, abs(d)));
    return col;
}

void main() {
    vec2 ndc = vUv * 2.0 - 1.0;
    vec3 ro, rd;
    if (uOrthoMode != 0) {
        ro = uCamPos + (ndc.x * uAspect * uOrthoScale) * uCamRight + (ndc.y * uOrthoScale) * uCamUp;
        rd = normalize(uCamFwd);
    } else {
        ro = uCamPos;
        rd = normalize(uCamFwd + (ndc.x * uAspect * uTanHalfFov) * uCamRight + (ndc.y * uTanHalfFov) * uCamUp);
    }

    // Background studio atmospheric charcoal gradient (no 2D flat grid!)
    float skyGrad = clamp(rd.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 bg = mix(vec3(0.048, 0.050, 0.055), vec3(0.095, 0.102, 0.114), smoothstep(0.0, 1.0, skyGrad));

    // Dynamic bounding-box ray-slab intersection covering expanded volume + active CSG nodes
    vec3 invRd = 1.0 / ( sign(rd) * max(abs(rd), vec3(1e-6)) );
    vec3 t0 = (uRayMin - ro) * invRd;
    vec3 t1 = (uRayMax - ro) * invRd;
    vec3 tsmaller = min(t0, t1);
    vec3 tbigger = max(t0, t1);
    float tmin = max(0.02, max(tsmaller.x, max(tsmaller.y, tsmaller.z)));
    float tmax = min(44.0, min(tbigger.x, min(tbigger.y, tbigger.z)));

    float hitT = 1e9;
    vec3 hitAlbedo = vec3(0.68, 0.42, 0.31);
    bool didHit = false;

    if (tmax >= tmin) {
        float t = tmin;
        for (int stepIdx = 0; stepIdx < 168; ++stepIdx) {
            vec3 p = ro + rd * t;
            vec4 sc = mapScene(p);
            if (sc.w < 0.0016) {
                hitT = t;
                hitAlbedo = sc.rgb;
                didHit = true;
                break;
            }
            t += max(0.004, sc.w * 0.88);
            if (t > tmax) break;
        }
    }

    vec3 finalCol = bg;

    // Real 3D Ground Grid at y = uGroundY (derivatives evaluated in uniform control flow for ANGLE D3D11 safety)
    float safeRdY = (abs(rd.y) > 1e-4) ? rd.y : (rd.y >= 0.0 ? 1e-4 : -1e-4);
    float tgRaw = (uGroundY - ro.y) / safeRdY;
    vec2 gpUncond = ro.xz + rd.xz * clamp(tgRaw, 0.0, 30.0);
    vec2 fwWorld = max(fwidth(gpUncond), vec2(1e-4));

    if (uShowGrid != 0 && abs(rd.y) > 1e-4 && tgRaw > 0.02 && tgRaw < hitT && tgRaw < 26.0) {
        vec3 gp = ro + rd * tgRaw;
        vec2 coordMinor = gp.xz / 0.25;
        vec2 gridMinor = abs(fract(coordMinor - 0.5) - 0.5) / (fwWorld / 0.25);
        float lineMinor = 1.0 - min(min(gridMinor.x, gridMinor.y), 1.0);

        vec2 coordMajor = gp.xz / 1.0;
        vec2 gridMajor = abs(fract(coordMajor - 0.5) - 0.5) / fwWorld;
        float lineMajor = 1.0 - min(min(gridMajor.x, gridMajor.y), 1.0);

        float fade = exp(-0.085 * dot(gp.xz, gp.xz));
        vec3 gridCol = bg;
        gridCol = mix(gridCol, vec3(0.16, 0.17, 0.19), lineMinor * 0.32 * fade);
        gridCol = mix(gridCol, vec3(0.26, 0.28, 0.31), lineMajor * 0.55 * fade);

        // World X (red) and Z (blue) origin axis hairlines
        float xAxis = (1.0 - smoothstep(0.0, fwWorld.y * 1.6, abs(gp.z))) * fade;
        float zAxis = (1.0 - smoothstep(0.0, fwWorld.x * 1.6, abs(gp.x))) * fade;
        gridCol = mix(gridCol, vec3(0.55, 0.22, 0.22), xAxis * 0.65);
        gridCol = mix(gridCol, vec3(0.20, 0.34, 0.58), zAxis * 0.65);

        // Soft SDF contact ambient shadow on studio floor
        float floorDist = mapDist(gp + vec3(0.0, 0.06, 0.0));
        float contactOcc = clamp(floorDist / 0.55, 0.28, 1.0);
        finalCol = gridCol * mix(1.0, contactOcc, fade);
    }

    if (didHit) {
        vec3 hp = ro + rd * hitT;
        vec3 hn = calcNormal(hp);
        finalCol = shadeSurface(hp, hn, -rd, hitAlbedo);

        float ring = brushRingOverlay(hp);
        if (ring > 0.001) {
            finalCol = mix(finalCol, uBrushAccent, ring * 0.85);
        }
    }

    // Interactive 3D SDF Field Slice Plane visualization when Lens == 5 ('slice')
    if (uLens == 5 || uSliceEnabled != 0) {
        float roA = (uSliceAxis == 0) ? ro.x : (uSliceAxis == 1) ? ro.y : ro.z;
        float rdA = (uSliceAxis == 0) ? rd.x : (uSliceAxis == 1) ? rd.y : rd.z;
        if (abs(rdA) > 1e-5) {
            float ts = (uSliceOffset - roA) / rdA;
            if (ts > 0.02 && (uLens == 5 ? (ts < 38.0) : (ts <= hitT + 0.004 && ts < 38.0))) {
                vec3 sp = ro + rd * ts;
                if (all(greaterThanEqual(sp, uVolMin)) && all(lessThanEqual(sp, uVolMax))) {
                    vec3 sliceVis = shadeFieldSlice(sp);
                    float alpha = (uLens == 5) ? (ts < hitT ? 0.86 : 0.52) : 0.0;
                    if (alpha > 0.0) {
                        finalCol = mix(finalCol, sliceVis, alpha);
                    }
                }
            }
        }
    }

    fragColor = vec4(finalCol, 1.0);
}
`;

const LENS_INDEX = {
    pbr: 0,
    matcap_wax: 1,
    matcap_grey: 2,
    matcap_jade: 3,
    curvature: 4,
    slice: 5,
    normals: 6
};

export class SdfRaymarchViewport
{
    constructor(GlCanvas, OverlayCanvas, SliceCanvas)
    {
        this.canvas = GlCanvas;
        this.overlay = OverlayCanvas;
        this.sliceCanvas = SliceCanvas;
        this.ctx2d = OverlayCanvas.getContext('2d');
        this.sliceCtx = SliceCanvas ? SliceCanvas.getContext('2d') : null;

        this.camera = {
            target: [0, 0.04, 0],
            yaw: 0.42,       // radians
            pitch: 0.24,     // radians
            distance: 3.15,
            fovDeg: 42,
            ortho: false,
            turntable: false
        };

        this.csgFloatBuffer = new Float32Array(MAX_CSG_NODES * 20);
        this.backend = 'initializing';
        this.gl = null;
        this.volumeTex = null;
        this.texAllocatedN = 0;

        this.initWebGL2();
    }

    initWebGL2()
    {
        const Gl = this.canvas.getContext('webgl2', {
            alpha: false,
            antialias: false,
            depth: false,
            stencil: false,
            powerPreference: 'high-performance',
            preserveDrawingBuffer: true
        });

        if (!Gl)
        {
            this.backend = 'software';
            this.fallbackCtx = this.canvas.getContext('2d');
            return;
        }

        const Vs = CompileShader(Gl, Gl.VERTEX_SHADER, VERTEX_SHADER_GLSL);
        const Fs = CompileShader(Gl, Gl.FRAGMENT_SHADER, FRAGMENT_SHADER_GLSL);
        if (!Vs || !Fs)
        {
            this.backend = 'software';
            this.fallbackCtx = this.canvas.getContext('2d');
            return;
        }

        const Prog = Gl.createProgram();
        Gl.attachShader(Prog, Vs);
        Gl.attachShader(Prog, Fs);
        Gl.linkProgram(Prog);
        if (!Gl.getProgramParameter(Prog, Gl.LINK_STATUS))
        {
            console.error('SDF Raymarch link error:', Gl.getProgramInfoLog(Prog));
            this.backend = 'software';
            this.fallbackCtx = this.canvas.getContext('2d');
            return;
        }

        this.gl = Gl;
        this.program = Prog;
        this.vao = Gl.createVertexArray();
        this.backend = 'webgl2';

        const U = Name => Gl.getUniformLocation(Prog, Name);
        this.uniforms = {
            uVolumeTex: U('uVolumeTex'),
            uVolMin: U('uVolMin'),
            uVolMax: U('uVolMax'),
            uVoxelSize: U('uVoxelSize'),
            uBoundsMode: U('uBoundsMode'),
            uRayMin: U('uRayMin'),
            uRayMax: U('uRayMax'),
            uVolumeVisible: U('uVolumeVisible'),
            uCsgCount: U('uCsgCount'),
            uCsgData: U('uCsgData[0]'),
            uCamPos: U('uCamPos'),
            uCamFwd: U('uCamFwd'),
            uCamRight: U('uCamRight'),
            uCamUp: U('uCamUp'),
            uTanHalfFov: U('uTanHalfFov'),
            uAspect: U('uAspect'),
            uOrthoMode: U('uOrthoMode'),
            uOrthoScale: U('uOrthoScale'),
            uSunDir: U('uSunDir'),
            uMatParams: U('uMatParams'),
            uLens: U('uLens'),
            uShowGrid: U('uShowGrid'),
            uGroundY: U('uGroundY'),
            uSliceEnabled: U('uSliceEnabled'),
            uSliceAxis: U('uSliceAxis'),
            uSliceOffset: U('uSliceOffset'),
            uBrushActive: U('uBrushActive'),
            uBrushPos: U('uBrushPos'),
            uBrushNormal: U('uBrushNormal'),
            uBrushRadius: U('uBrushRadius'),
            uBrushAccent: U('uBrushAccent'),
            uSymmetry: U('uSymmetry')
        };
    }

    syncVolumeTexture(Volume)
    {
        const Gl = this.gl;
        if (!Gl) return;

        const N = Volume.nx;
        Gl.activeTexture(Gl.TEXTURE0);

        if (!this.volumeTex || this.texAllocatedN !== N)
        {
            if (this.volumeTex) Gl.deleteTexture(this.volumeTex);
            this.volumeTex = Gl.createTexture();
            Gl.bindTexture(Gl.TEXTURE_3D, this.volumeTex);
            Gl.pixelStorei(Gl.UNPACK_ALIGNMENT, 1);
            Gl.texParameteri(Gl.TEXTURE_3D, Gl.TEXTURE_MIN_FILTER, Gl.LINEAR);
            Gl.texParameteri(Gl.TEXTURE_3D, Gl.TEXTURE_MAG_FILTER, Gl.LINEAR);
            Gl.texParameteri(Gl.TEXTURE_3D, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
            Gl.texParameteri(Gl.TEXTURE_3D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
            Gl.texParameteri(Gl.TEXTURE_3D, Gl.TEXTURE_WRAP_R, Gl.CLAMP_TO_EDGE);
            Gl.texImage3D(Gl.TEXTURE_3D, 0, Gl.RGBA16F, N, N, N, 0, Gl.RGBA, Gl.HALF_FLOAT, null);
            this.texAllocatedN = N;
            Volume.markAllDirty();
        }
        else
        {
            Gl.bindTexture(Gl.TEXTURE_3D, this.volumeTex);
        }

        const Sub = Volume.consumeDirtySubBrick();
        if (Sub)
        {
            Gl.pixelStorei(Gl.UNPACK_ALIGNMENT, 1);
            Gl.texSubImage3D(
                Gl.TEXTURE_3D,
                0,
                Sub.x0, Sub.y0, Sub.z0,
                Sub.width, Sub.height, Sub.depth,
                Gl.RGBA,
                Gl.HALF_FLOAT,
                Sub.data
            );
        }
    }

    computeCameraBasis()
    {
        const Cam = this.camera;
        const Cp = Math.cos(Cam.pitch), Sp = Math.sin(Cam.pitch);
        const Cy = Math.cos(Cam.yaw), Sy = Math.sin(Cam.yaw);
        const Offset = [Sy * Cp * Cam.distance, Sp * Cam.distance, Cy * Cp * Cam.distance];
        const Pos = Vec3Add(Cam.target, Offset);
        const Fwd = Vec3Normalize(Vec3Sub(Cam.target, Pos));
        let Right = Vec3Cross(Fwd, [0, 1, 0]);
        if ( Math.hypot(Right[0], Right[1], Right[2]) < 1e-4 ) Right = [1, 0, 0];
        else Right = Vec3Normalize(Right);
        const Up = Vec3Normalize(Vec3Cross(Right, Fwd));
        const TanHalfFov = Math.tan((Cam.fovDeg * Math.PI / 180) * 0.5);
        const OrthoScale = Cam.distance * TanHalfFov;
        return { Pos, Fwd, Right, Up, TanHalfFov, OrthoScale };
    }

    screenToRay(ClientX, ClientY)
    {
        const Rect = this.canvas.getBoundingClientRect();
        const NdcX = ((ClientX - Rect.left) / Math.max(1, Rect.width)) * 2.0 - 1.0;
        const NdcY = 1.0 - ((ClientY - Rect.top) / Math.max(1, Rect.height)) * 2.0;
        const Aspect = Math.max(1, Rect.width) / Math.max(1, Rect.height);
        const { Pos, Fwd, Right, Up, TanHalfFov, OrthoScale } = this.computeCameraBasis();

        if (this.camera.ortho)
        {
            const Origin = Vec3Add(
                Pos,
                Vec3Add(Vec3Scale(Right, NdcX * Aspect * OrthoScale), Vec3Scale(Up, NdcY * OrthoScale))
            );
            return { origin: Origin, dir: Fwd };
        }
        const Dir = Vec3Normalize(
            Vec3Add(Fwd, Vec3Add(Vec3Scale(Right, NdcX * Aspect * TanHalfFov), Vec3Scale(Up, NdcY * TanHalfFov)))
        );
        return { origin: Pos, dir: Dir };
    }

    projectWorldToScreen(WorldPos)
    {
        const Rect = this.canvas.getBoundingClientRect();
        const W = Math.max(1, Rect.width), H = Math.max(1, Rect.height);
        const Aspect = W / H;
        const { Pos, Fwd, Right, Up, TanHalfFov, OrthoScale } = this.computeCameraBasis();
        const Rel = Vec3Sub(WorldPos, Pos);
        const Z = Vec3Dot(Rel, Fwd);
        if (!this.camera.ortho && Z <= 0.05) return null;

        const XProj = Vec3Dot(Rel, Right);
        const YProj = Vec3Dot(Rel, Up);
        let NdcX, NdcY;
        if (this.camera.ortho)
        {
            NdcX = XProj / (Aspect * OrthoScale);
            NdcY = YProj / OrthoScale;
        }
        else
        {
            NdcX = XProj / (Z * Aspect * TanHalfFov);
            NdcY = YProj / (Z * TanHalfFov);
        }
        return {
            x: (NdcX * 0.5 + 0.5) * W,
            y: (0.5 - NdcY * 0.5) * H,
            depth: Z
        };
    }

    setViewPreset(ViewName)
    {
        this.camera.target = [0, 0.04, 0];
        if (ViewName === 'front') { this.camera.yaw = 0; this.camera.pitch = 0; this.camera.ortho = true; }
        else if (ViewName === 'side') { this.camera.yaw = Math.PI * 0.5; this.camera.pitch = 0; this.camera.ortho = true; }
        else if (ViewName === 'top') { this.camera.yaw = 0; this.camera.pitch = 1.48; this.camera.ortho = true; }
        else { this.camera.yaw = 0.42; this.camera.pitch = 0.24; this.camera.ortho = false; }
    }

    resizeIfNeeded()
    {
        const Rect = this.canvas.getBoundingClientRect();
        const Dpr = Math.min(window.devicePixelRatio || 1, 1.75);
        const TargetW = Math.max(2, Math.round(Rect.width * Dpr));
        const TargetH = Math.max(2, Math.round(Rect.height * Dpr));
        if (this.canvas.width !== TargetW || this.canvas.height !== TargetH)
        {
            this.canvas.width = TargetW;
            this.canvas.height = TargetH;
        }
        const OvDpr = window.devicePixelRatio || 1;
        const OvW = Math.max(2, Math.round(Rect.width * OvDpr));
        const OvH = Math.max(2, Math.round(Rect.height * OvDpr));
        if (this.overlay.width !== OvW || this.overlay.height !== OvH)
        {
            this.overlay.width = OvW;
            this.overlay.height = OvH;
        }
        return { width: Rect.width, height: Rect.height, dpr: OvDpr };
    }

    render(Volume, RenderState)
    {
        const { width: CssW, height: CssH, dpr: OvDpr } = this.resizeIfNeeded();
        if (CssW < 4 || CssH < 4) return;

        const Basis = this.computeCameraBasis();
        const Packed = PackCsgUniformArray(RenderState.csgNodes || [], this.csgFloatBuffer);
        const SceneBox = Volume.computeSceneBounds(RenderState.csgNodes || [], 0.85);
        const ModeIdx = Volume.boundsMode === 'soft' ? 1 : Volume.boundsMode === 'hard' ? 2 : 0;

        if (this.backend === 'webgl2' && this.gl)
        {
            const Gl = this.gl;
            this.syncVolumeTexture(Volume);

            Gl.viewport(0, 0, this.canvas.width, this.canvas.height);
            Gl.useProgram(this.program);
            Gl.bindVertexArray(this.vao);

            const U = this.uniforms;
            Gl.uniform1i(U.uVolumeTex, 0);
            Gl.uniform3fv(U.uVolMin, Volume.min);
            Gl.uniform3fv(U.uVolMax, Volume.max);
            Gl.uniform1f(U.uVoxelSize, Volume.voxelSize);
            Gl.uniform1i(U.uBoundsMode, ModeIdx);
            Gl.uniform3fv(U.uRayMin, SceneBox.min);
            Gl.uniform3fv(U.uRayMax, SceneBox.max);
            Gl.uniform1i(U.uVolumeVisible, RenderState.volumeVisible !== false ? 1 : 0);

            Gl.uniform1i(U.uCsgCount, Packed.Count);
            if (Packed.Count > 0)
            {
                Gl.uniform4fv(U.uCsgData, Packed.Buffer);
            }

            Gl.uniform3fv(U.uCamPos, Basis.Pos);
            Gl.uniform3fv(U.uCamFwd, Basis.Fwd);
            Gl.uniform3fv(U.uCamRight, Basis.Right);
            Gl.uniform3fv(U.uCamUp, Basis.Up);
            Gl.uniform1f(U.uTanHalfFov, Basis.TanHalfFov);
            Gl.uniform1f(U.uAspect, this.canvas.width / Math.max(1, this.canvas.height));
            Gl.uniform1i(U.uOrthoMode, this.camera.ortho ? 1 : 0);
            Gl.uniform1f(U.uOrthoScale, Basis.OrthoScale);

            const Az = (RenderState.sunAzimuth ?? 38) * Math.PI / 180;
            const El = (RenderState.sunElevation ?? 46) * Math.PI / 180;
            const SunDir = [Math.sin(Az) * Math.cos(El), Math.sin(El), Math.cos(Az) * Math.cos(El)];
            Gl.uniform3fv(U.uSunDir, SunDir);
            Gl.uniform4f(
                U.uMatParams,
                RenderState.roughness ?? 0.58,
                RenderState.metallic ?? 0.04,
                RenderState.subsurface ?? 0.35,
                RenderState.cavity ?? 0.55
            );
            Gl.uniform1i(U.uLens, LENS_INDEX[RenderState.lens] ?? 0);
            Gl.uniform1i(U.uShowGrid, RenderState.showGrid !== false ? 1 : 0);
            const EffectiveGroundY = Math.min(RenderState.groundY ?? -1.14, Volume.min[1] - 0.02);
            Gl.uniform1f(U.uGroundY, RenderState.groundY ?? -1.14);
            void EffectiveGroundY;

            Gl.uniform1i(U.uSliceEnabled, RenderState.sliceEnabled ? 1 : 0);
            Gl.uniform1i(U.uSliceAxis, RenderState.sliceAxis ?? 2);
            Gl.uniform1f(U.uSliceOffset, RenderState.sliceOffset ?? 0.0);

            const Hover = RenderState.brushHover;
            Gl.uniform1i(U.uBrushActive, Hover?.active ? 1 : 0);
            if (Hover?.active)
            {
                Gl.uniform3fv(U.uBrushPos, Hover.pos);
                Gl.uniform3fv(U.uBrushNormal, Hover.normal);
                Gl.uniform1f(U.uBrushRadius, Hover.radius);
                Gl.uniform3fv(U.uBrushAccent, Hover.accentLinear || [0.91, 0.76, 0.48]);
            }
            const Sym = RenderState.symmetry || {};
            Gl.uniform4i(U.uSymmetry, Sym.x ? 1 : 0, Sym.y ? 1 : 0, Sym.z ? 1 : 0, Sym.radial || 1);

            Gl.drawArrays(Gl.TRIANGLES, 0, 3);
        }
        else
        {
            this.renderSoftwareFallback(Volume, RenderState, Basis);
        }

        this.renderOverlay2D(CssW, CssH, OvDpr, RenderState, Volume);
    }

    /**
     * Draw 3D domain corner-bracket cage, symmetry guides, normal needle, and CSG transform gizmo on OverlayCanvas.
     */
    renderOverlay2D(CssW, CssH, Dpr, RenderState, Volume)
    {
        const Ctx = this.ctx2d;
        if (!Ctx) return;
        Ctx.save();
        Ctx.setTransform(Dpr, 0, 0, Dpr, 0, 0);
        Ctx.clearRect(0, 0, CssW, CssH);

        // 0. 3D Sculpt Domain Corner-Bracket Cage (shown when toggled ON or when brush approaches domain edge!)
        if (Volume && RenderState.volumeVisible !== false)
        {
            const Hover = RenderState.brushHover;
            let WallProximity = 0.0;
            if (Hover?.active && Hover.pos)
            {
                const R = Hover.radius || 0.24;
                const MinClearance = Math.min(
                    Hover.pos[0] - R - Volume.min[0], Volume.max[0] - (Hover.pos[0] + R),
                    Hover.pos[1] - R - Volume.min[1], Volume.max[1] - (Hover.pos[1] + R),
                    Hover.pos[2] - R - Volume.min[2], Volume.max[2] - (Hover.pos[2] + R)
                );
                if (MinClearance < 0.45)
                {
                    WallProximity = Clamp(1.0 - MinClearance / 0.45, 0.0, 1.0);
                }
            }

            if (RenderState.showBoundsCage || WallProximity > 0.02)
            {
                const Alpha = RenderState.showBoundsCage ? Math.max(0.38, WallProximity * 0.85) : WallProximity * 0.78;
                const IsAuto = Volume.boundsMode === 'expand' && (Volume.span < Volume.maxAutoSpan - 0.05);
                const StrokeRgb = IsAuto ? '232, 194, 122' : '255, 132, 108';
                const MinB = Volume.min, MaxB = Volume.max;
                const TickFrac = 0.14;
                const Tx = (MaxB[0] - MinB[0]) * TickFrac;
                const Ty = (MaxB[1] - MinB[1]) * TickFrac;
                const Tz = (MaxB[2] - MinB[2]) * TickFrac;

                Ctx.save();
                Ctx.strokeStyle = `rgba(${StrokeRgb}, ${Alpha.toFixed(3)})`;
                Ctx.lineWidth = WallProximity > 0.35 ? 1.5 : 1.1;
                let TopPt = null;

                for (let Cx = 0; Cx < 2; ++Cx)
                {
                    const X = Cx ? MaxB[0] : MinB[0];
                    const Sx = Cx ? -Tx : Tx;
                    for (let Cy = 0; Cy < 2; ++Cy)
                    {
                        const Y = Cy ? MaxB[1] : MinB[1];
                        const Sy = Cy ? -Ty : Ty;
                        for (let Cz = 0; Cz < 2; ++Cz)
                        {
                            const Z = Cz ? MaxB[2] : MinB[2];
                            const Sz = Cz ? -Tz : Tz;
                            const P0 = this.projectWorldToScreen([X, Y, Z]);
                            if (!P0) continue;
                            if (!TopPt || P0.y < TopPt.y) TopPt = P0;

                            const Px = this.projectWorldToScreen([X + Sx, Y, Z]);
                            const Py = this.projectWorldToScreen([X, Y + Sy, Z]);
                            const Pz = this.projectWorldToScreen([X, Y, Z + Sz]);
                            Ctx.beginPath();
                            if (Px) { Ctx.moveTo(P0.x, P0.y); Ctx.lineTo(Px.x, Px.y); }
                            if (Py) { Ctx.moveTo(P0.x, P0.y); Ctx.lineTo(Py.x, Py.y); }
                            if (Pz) { Ctx.moveTo(P0.x, P0.y); Ctx.lineTo(Pz.x, Pz.y); }
                            Ctx.stroke();
                        }
                    }
                }

                if (TopPt && TopPt.x > 40 && TopPt.x < CssW - 40 && TopPt.y > 24 && TopPt.y < CssH - 40)
                {
                    const ModeLabel = Volume.boundsMode === 'expand'
                        ? (Volume.span >= Volume.maxAutoSpan - 0.05 ? 'MAX 12m · SOFT CAP' : 'AUTO-EXPAND')
                        : Volume.boundsMode === 'soft' ? 'SOFT CUSHION CAP' : 'HARD CLIP';
                    const Label = `DOMAIN ${Volume.span.toFixed(1)}³ m · ${ModeLabel}`;
                    Ctx.font = '500 10px "DM Sans", sans-serif';
                    const Tw = Ctx.measureText(Label).width + 14;
                    const Bx = Clamp(TopPt.x - Tw * 0.5, 14, CssW - Tw - 14);
                    const By = Math.max(46, TopPt.y - 22);
                    Ctx.fillStyle = `rgba(14, 14, 15, ${(Alpha * 0.92).toFixed(3)})`;
                    Ctx.strokeStyle = `rgba(${StrokeRgb}, ${(Alpha * 0.65).toFixed(3)})`;
                    Ctx.beginPath();
                    if (Ctx.roundRect) Ctx.roundRect(Bx, By, Tw, 18, 9);
                    else Ctx.rect(Bx, By, Tw, 18);
                    Ctx.fill();
                    Ctx.stroke();
                    Ctx.fillStyle = `rgba(239, 239, 239, ${Math.min(1, Alpha + 0.25).toFixed(3)})`;
                    Ctx.fillText(Label, Bx + 7, By + 12.5);
                }
                Ctx.restore();
            }
        }

        // 1. Subtle 3D Symmetry Mirror Horizon Indicator Line (when X/Z symmetry active)
        const Sym = RenderState.symmetry || {};
        if (Sym.x)
        {
            const A = this.projectWorldToScreen([0, -1.14, -1.6]);
            const B = this.projectWorldToScreen([0, -1.14, 1.6]);
            if (A && B)
            {
                Ctx.save();
                Ctx.strokeStyle = 'rgba(232, 194, 122, 0.24)';
                Ctx.lineWidth = 1;
                Ctx.setLineDash([4, 5]);
                Ctx.beginPath();
                Ctx.moveTo(A.x, A.y);
                Ctx.lineTo(B.x, B.y);
                Ctx.stroke();
                Ctx.restore();
            }
        }

        // 2. Surface Normal Needle + Lazy-Mouse Tether at Brush Hit
        const Hover = RenderState.brushHover;
        if (Hover?.active)
        {
            const P0 = this.projectWorldToScreen(Hover.pos);
            const PTip = this.projectWorldToScreen(Vec3Add(Hover.pos, Vec3Scale(Hover.normal, Hover.radius * 0.45)));
            if (P0 && PTip)
            {
                Ctx.strokeStyle = Hover.accentCss || '#e8c27a';
                Ctx.lineWidth = 1.6;
                Ctx.beginPath();
                Ctx.moveTo(P0.x, P0.y);
                Ctx.lineTo(PTip.x, PTip.y);
                Ctx.stroke();

                Ctx.fillStyle = Hover.accentCss || '#e8c27a';
                Ctx.beginPath();
                Ctx.arc(P0.x, P0.y, 2.6, 0, Math.PI * 2);
                Ctx.fill();
            }

            if (Hover.lazyFrom)
            {
                const LazyScreen = this.projectWorldToScreen(Hover.lazyFrom);
                if (LazyScreen && P0)
                {
                    Ctx.strokeStyle = 'rgba(239, 244, 207, 0.78)';
                    Ctx.lineWidth = 1.3;
                    Ctx.setLineDash([3, 3]);
                    Ctx.beginPath();
                    Ctx.moveTo(LazyScreen.x, LazyScreen.y);
                    Ctx.lineTo(P0.x, P0.y);
                    Ctx.stroke();
                    Ctx.setLineDash([]);
                }
            }
        }

        // 3. Selected Parametric CSG Node Exterior Callout + Interactive 3D Transform Gizmo
        this.gizmoHandles = [];
        const SelectedNode = RenderState.selectedCsgNode;
        if (SelectedNode && SelectedNode.enabled !== false)
        {
            const Origin = SelectedNode.position || [0, 0, 0];
            const Center2D = this.projectWorldToScreen(Origin);
            if (Center2D)
            {
                const ArmLen = Math.max(0.32, this.camera.distance * 0.13);
                const Axes = [
                    { axis: 0, label: 'X', dir: [1, 0, 0], color: '#ef5350' },
                    { axis: 1, label: 'Y', dir: [0, 1, 0], color: '#69d06d' },
                    { axis: 2, label: 'Z', dir: [0, 0, 1], color: '#5b8cff' }
                ];

                // Exterior callout badge offset up-right so it doesn't obscure sculpted asset
                const CalloutX = Clamp(Center2D.x + 54, 18, CssW - 165);
                const CalloutY = Clamp(Center2D.y - 54, 24, CssH - 36);
                Ctx.strokeStyle = 'rgba(232, 194, 122, 0.42)';
                Ctx.lineWidth = 1;
                Ctx.beginPath();
                Ctx.moveTo(Center2D.x, Center2D.y);
                Ctx.lineTo(CalloutX, CalloutY);
                Ctx.stroke();

                const BadgeText = `${SelectedNode.name}${SelectedNode.op === 1 ? ' · CUT' : SelectedNode.op === 2 ? ' · INT' : ''}`;
                Ctx.font = '400 11px "DM Sans", sans-serif';
                const Tw = Ctx.measureText(BadgeText).width;
                const BoxW = Tw + 18, BoxH = 22;
                Ctx.fillStyle = 'rgba(18, 18, 18, 0.86)';
                Ctx.strokeStyle = SelectedNode.op === 1 ? 'rgba(255, 107, 107, 0.5)' : 'rgba(232, 194, 122, 0.45)';
                Ctx.beginPath();
                Ctx.roundRect(CalloutX, CalloutY - BoxH * 0.5, BoxW, BoxH, 11);
                Ctx.fill();
                Ctx.stroke();
                Ctx.fillStyle = '#efefef';
                Ctx.fillText(BadgeText, CalloutX + 9, CalloutY + 3.8);

                if (RenderState.showGizmo !== false && !SelectedNode.locked)
                {
                    for (const Ax of Axes)
                    {
                        const EndWorld = Vec3Add(Origin, Vec3Scale(Ax.dir, ArmLen));
                        const End2D = this.projectWorldToScreen(EndWorld);
                        if (!End2D) continue;

                        const Active = RenderState.activeGizmoAxis === Ax.axis;
                        Ctx.strokeStyle = Active ? '#fff6d6' : Ax.color;
                        Ctx.lineWidth = Active ? 3.0 : 2.1;
                        Ctx.beginPath();
                        Ctx.moveTo(Center2D.x, Center2D.y);
                        Ctx.lineTo(End2D.x, End2D.y);
                        Ctx.stroke();

                        // Arrowhead circle pill
                        Ctx.fillStyle = Active ? '#fff6d6' : Ax.color;
                        Ctx.beginPath();
                        Ctx.arc(End2D.x, End2D.y, Active ? 6.5 : 5.2, 0, Math.PI * 2);
                        Ctx.fill();

                        this.gizmoHandles.push({
                            type: 'translate-axis',
                            axis: Ax.axis,
                            dir: Ax.dir,
                            x: End2D.x,
                            y: End2D.y,
                            x0: Center2D.x,
                            y0: Center2D.y
                        });
                    }

                    // Center free screen-plane move ring
                    const FreeActive = RenderState.activeGizmoAxis === 'free';
                    Ctx.fillStyle = FreeActive ? '#eff4cf' : 'rgba(24, 24, 24, 0.92)';
                    Ctx.strokeStyle = '#e8c27a';
                    Ctx.lineWidth = 2;
                    Ctx.beginPath();
                    Ctx.arc(Center2D.x, Center2D.y, 7.5, 0, Math.PI * 2);
                    Ctx.fill();
                    Ctx.stroke();

                    this.gizmoHandles.push({
                        type: 'translate-free',
                        axis: 'free',
                        x: Center2D.x,
                        y: Center2D.y
                    });
                }
            }
        }

        Ctx.restore();
    }

    pickGizmoHandle(ClientX, ClientY)
    {
        if (!this.gizmoHandles?.length) return null;
        const Rect = this.canvas.getBoundingClientRect();
        const Mx = ClientX - Rect.left;
        const My = ClientY - Rect.top;

        // Check center and tip handles first
        for (const H of this.gizmoHandles)
        {
            if (Math.hypot(Mx - H.x, My - H.y) <= 13) return H;
        }
        // Check axis shaft proximity
        for (const H of this.gizmoHandles)
        {
            if (H.type !== 'translate-axis') continue;
            const Dx = H.x - H.x0, Dy = H.y - H.y0;
            const LenSq = Dx * Dx + Dy * Dy;
            if (LenSq < 4) continue;
            const T = Clamp(((Mx - H.x0) * Dx + (My - H.y0) * Dy) / LenSq, 0.15, 1.0);
            const Px = H.x0 + Dx * T, Py = H.y0 + Dy * T;
            if (Math.hypot(Mx - Px, My - Py) <= 8) return H;
        }
        return null;
    }

    /**
     * Renders the 2D Cross-Section Distance Field Slice Minimap inset (88x88 isoline contour view).
     */
    renderSliceMinimap(Volume, CsgNodes, Axis = 2, SliceOffset = 0.0, VolumeVisible = true)
    {
        if (!this.sliceCtx || !this.sliceCanvas) return;
        const Size = 96;
        if (this.sliceCanvas.width !== Size || this.sliceCanvas.height !== Size)
        {
            this.sliceCanvas.width = Size;
            this.sliceCanvas.height = Size;
        }
        const Img = this._sliceImg || (this._sliceImg = this.sliceCtx.createImageData(Size, Size));
        const Data = Img.data;
        const Tmp = [0, 0, 0, 0];
        const Span = 1.65;

        let Ptr = 0;
        for (let Py = 0; Py < Size; ++Py)
        {
            const V = (0.5 - Py / (Size - 1)) * 2.0 * Span;
            for (let Px = 0; Px < Size; ++Px)
            {
                const U = (Px / (Size - 1) - 0.5) * 2.0 * Span;
                let Wx, Wy, Wz;
                if (Axis === 0) { Wx = SliceOffset; Wy = V; Wz = U; }
                else if (Axis === 1) { Wx = U; Wy = SliceOffset; Wz = V; }
                else { Wx = U; Wy = V; Wz = SliceOffset; }

                const D = Volume.evaluateHybridPoint(Wx, Wy, Wz, CsgNodes, VolumeVisible, Tmp)[0];
                // Inigo Quilez signed-distance isoline palette tuned to Frontier studio dark theme
                let R, G, B;
                if (D > 0)
                {
                    R = 0.82; G = 0.60; B = 0.34;
                }
                else
                {
                    R = 0.22; G = 0.76; B = 0.60;
                }
                const Atten = (1.0 - Math.exp(-2.8 * Math.abs(D))) * (0.78 + 0.22 * Math.cos(65.0 * D));
                R = 0.07 + R * Atten * 0.78;
                G = 0.075 + G * Atten * 0.78;
                B = 0.085 + B * Atten * 0.78;

                // Bright zero-level isosurface contour band
                const IsoEdge = 1.0 - Clamp(Math.abs(D) / 0.032, 0.0, 1.0);
                R = R + (0.98 - R) * IsoEdge;
                G = G + (0.96 - G) * IsoEdge;
                B = B + (0.88 - B) * IsoEdge;

                Data[Ptr++] = Math.round(Clamp(R, 0, 1) * 255);
                Data[Ptr++] = Math.round(Clamp(G, 0, 1) * 255);
                Data[Ptr++] = Math.round(Clamp(B, 0, 1) * 255);
                Data[Ptr++] = 255;
            }
        }
        this.sliceCtx.putImageData(Img, 0, 0);
    }

    /**
     * Coarse 2D Canvas CPU sphere-trace fallback if WebGL2 context is unavailable.
     */
    renderSoftwareFallback(Volume, RenderState, Basis)
    {
        const Ctx = this.fallbackCtx;
        if (!Ctx) return;
        const W = 160, H = Math.max(90, Math.round(160 * (this.canvas.height / Math.max(1, this.canvas.width))));
        if (!this._swCanvas)
        {
            this._swCanvas = document.createElement('canvas');
            this._swCtx = this._swCanvas.getContext('2d');
        }
        if (this._swCanvas.width !== W || this._swCanvas.height !== H)
        {
            this._swCanvas.width = W;
            this._swCanvas.height = H;
            this._swImg = this._swCtx.createImageData(W, H);
        }
        const Data = this._swImg.data;
        const Aspect = W / H;
        const SunDir = Vec3Normalize([0.55, 0.72, 0.42]);
        const Tmp = [0, 0, 0, 0];
        let Ptr = 0;

        for (let Y = 0; Y < H; ++Y)
        {
            const NdcY = 1.0 - (Y / (H - 1)) * 2.0;
            for (let X = 0; X < W; ++X)
            {
                const NdcX = (X / (W - 1)) * 2.0 - 1.0;
                const Rd = Vec3Normalize(
                    Vec3Add(Basis.Fwd, Vec3Add(Vec3Scale(Basis.Right, NdcX * Aspect * Basis.TanHalfFov), Vec3Scale(Basis.Up, NdcY * Basis.TanHalfFov)))
                );
                const Hit = Volume.raycastHybrid(Basis.Pos, Rd, RenderState.csgNodes, RenderState.volumeVisible !== false, 12.0);
                if (Hit.hit)
                {
                    Volume.evaluateHybridPoint(Hit.pos[0], Hit.pos[1], Hit.pos[2], RenderState.csgNodes, RenderState.volumeVisible !== false, Tmp);
                    const Ndl = Math.max(0.15, Vec3Dot(Hit.normal, SunDir));
                    Data[Ptr++] = Math.min(255, Math.round(Math.pow(Tmp[1] * Ndl * 1.35, 1 / 2.2) * 255));
                    Data[Ptr++] = Math.min(255, Math.round(Math.pow(Tmp[2] * Ndl * 1.35, 1 / 2.2) * 255));
                    Data[Ptr++] = Math.min(255, Math.round(Math.pow(Tmp[3] * Ndl * 1.35, 1 / 2.2) * 255));
                    Data[Ptr++] = 255;
                }
                else
                {
                    Data[Ptr++] = 16; Data[Ptr++] = 17; Data[Ptr++] = 19; Data[Ptr++] = 255;
                }
            }
        }
        this._swCtx.putImageData(this._swImg, 0, 0);
        Ctx.imageSmoothingEnabled = true;
        Ctx.drawImage(this._swCanvas, 0, 0, this.canvas.width, this.canvas.height);
    }
}

function CompileShader(Gl, Type, Source)
{
    const Shader = Gl.createShader(Type);
    Gl.shaderSource(Shader, Source);
    Gl.compileShader(Shader);
    if (!Gl.getShaderParameter(Shader, Gl.COMPILE_STATUS))
    {
        console.error('SDF Shader compile error:', Gl.getShaderInfoLog(Shader));
        Gl.deleteShader(Shader);
        return null;
    }
    return Shader;
}
