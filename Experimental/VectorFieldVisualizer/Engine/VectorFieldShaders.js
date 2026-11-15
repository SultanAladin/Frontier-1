/*============================================================================================================================================
                                                      VECTORFIELDSHADERS.JS
============================================================================================================================================*/
// 🧩 Single-Kernel Ultra-Light WebGL2 (GLSL ES 3.00) Field Architecture:
//    • ONLY SLICE_BAKE_FRAG runs the 19-ForceModel + Custom Symbolic Formula evaluator (on a zoom-adaptive 128×128 FBO)
//    • SLICE_DISPLAY_FRAG and GPU_ARROWS_VERT sample uFieldBakeTex in O(1); STREAMLINE_* renders anti-aliased RK4 ribbons
//    • Zero dynamic uniform indexing, zero uint bit-hacks, zero unguarded pow/tanh — compiles in milliseconds

const GLSL_FIELD_HEADER = /* glsl */ `
#define MAX_FORCES 20
#define PI 3.14159265359

uniform int   uForceCount;
uniform float uTime;
uniform float uWorldScale;
uniform vec4  uForceA[MAX_FORCES];
uniform vec4  uForceB[MAX_FORCES];
uniform vec4  uForceC[MAX_FORCES];
uniform vec4  uForceD[MAX_FORCES];
uniform vec4  uForceE[MAX_FORCES];

float vfSmooth01(float t)
{
    float c = clamp(t, 0.0, 1.0);
    return c * c * (3.0 - 2.0 * c);
}

float vfTanh(float x)
{
    float c = clamp(x, -10.0, 10.0);
    float e = exp(2.0 * c);
    return (e - 1.0) / (e + 1.0);
}

vec3 vfSafeNorm(vec3 v)
{
    float l = length(v);
    return l > 1e-4 ? v / l : vec3(0.0);
}

float vfAttenuate(int curve, float dist, float radius)
{
    if (radius <= 1e-5 || dist >= radius) return 0.0;
    if (curve == 3) return 1.0;
    float u = clamp(dist / radius, 0.0, 1.0);
    if (curve == 1) return 1.0 - u;
    if (curve == 2)
    {
        float tail = 1.0 - vfSmooth01((u - 0.82) / 0.18);
        return (1.0 / (1.0 + 9.0 * u * u)) * tail;
    }
    float s = 1.0 - u * u;
    return s * s;
}

vec3 vfTrigPotential(vec3 p, float seed)
{
    vec3 s = vec3(seed * 0.013, seed * 0.029, seed * 0.047);
    vec3 a = p + s;
    vec3 b = p * 1.93 + s.yzx * 1.7;
    return vec3(
        sin(a.y * 2.1 + cos(a.z * 1.7)) + 0.45 * cos(b.z * 2.3 + sin(b.y * 1.9)),
        sin(a.z * 2.2 + cos(a.x * 1.8)) + 0.45 * cos(b.x * 2.1 + sin(b.z * 1.7)),
        sin(a.x * 1.9 + cos(a.y * 2.1)) + 0.45 * cos(b.y * 2.2 + sin(b.x * 1.8))
    );
}

vec3 vfCurlNoise(vec3 w, float scale, float seed)
{
    vec3 p = (w / max(2.0, scale)) * 1.65;
    float eps = 0.02;
    float inv2 = 25.0;
    vec3 px = vfTrigPotential(p + vec3(eps, 0.0, 0.0), seed) - vfTrigPotential(p - vec3(eps, 0.0, 0.0), seed);
    vec3 py = vfTrigPotential(p + vec3(0.0, eps, 0.0), seed) - vfTrigPotential(p - vec3(0.0, eps, 0.0), seed);
    vec3 pz = vfTrigPotential(p + vec3(0.0, 0.0, eps), seed) - vfTrigPotential(p - vec3(0.0, 0.0, eps), seed);
    return vec3(py.z - pz.y, pz.x - px.z, px.y - py.x) * (inv2 * 0.42);
}

vec3 vfFastNoise3(vec3 p, float tVal, float seed)
{
    vec3 q = p + vec3(seed * 0.011, tVal * 1.73, seed * 0.023);
    return vec3(
        sin(q.x * 2.3 + cos(q.y * 1.9 + q.z * 1.7)),
        sin(q.y * 2.1 + cos(q.z * 2.3 - q.x * 1.6)),
        sin(q.z * 2.4 + cos(q.x * 1.8 + q.y * 2.0))
    ) * 0.85;
}

vec3 vfAxisDir(float azRad, float elRad)
{
    float ce = cos(elRad);
    return vec3(sin(azRad) * ce, sin(elRad), -cos(azRad) * ce);
}
`;

const GLSL_COMPOSITE_HEAD = /* glsl */ `
void vfEvalComposite(vec3 p, float time, out vec3 outVel, out vec3 outAcc, out float outDamp, out vec3 outTotal, out float outWaveGlow)
{
    outVel = vec3(0.0);
    outAcc = vec3(0.0);
    outDamp = 0.0;
    outWaveGlow = 0.0;

    for (int i = 0; i < MAX_FORCES; ++i)
    {
        if (i >= uForceCount) break;
        vec4 A = uForceA[i];
        vec4 B = uForceB[i];
        vec4 C = uForceC[i];
        vec4 D = uForceD[i];
        vec4 E = uForceE[i];

        int model = int(B.x + 0.5);
        float strength = B.y;
        float bearing = B.z;
        float height = max(10.0, B.w);
        int reach = int(C.x + 0.5);
        int atten = int(C.y + 0.5);
        int durMode = int(C.z + 0.5);
        int chan = int(C.w + 0.5);

        float gate = 1.0;
        float age = time;
        if (durMode == 1)
        {
            float freq = max(0.02, D.w > 0.001 ? D.w : 0.25);
            float wave = 0.5 + 0.5 * sin(2.0 * PI * freq * time);
            gate = vfSmooth01((wave - 0.175) / 0.65);
        }
        else if (durMode == 2)
        {
            float localT = time - E.x;
            if (localT < 0.0) gate = 0.0;
            else
            {
                if (E.z > 0.0 && E.y > 0.0) localT = mod(localT, E.z);
                float win = max(0.5, E.y);
                if (localT > win) gate = 0.0;
                else
                {
                    float atk = vfSmooth01(localT / 0.2);
                    float dec = 1.0 - vfSmooth01((localT - (win - 1.5)) / 1.5);
                    gate = clamp(atk * dec, 0.0, 1.0);
                    age = localT;
                }
            }
        }
        if (gate <= 1e-4) continue;

        vec3 d = p - A.xyz;
        float radius = max(1.0, A.w);
        float share = 0.0;
        if (reach == 0)
        {
            share = vfAttenuate(atten, length(d), radius);
        }
        else if (reach == 1)
        {
            if (d.y >= -20.0 && d.y <= height * 1.25)
            {
                float r = length(d.xz);
                if (r < radius)
                {
                    float vt = d.y <= height ? 1.0 : 1.0 - vfSmooth01((d.y - height) / (0.25 * height));
                    float bt = d.y >= 0.0 ? 1.0 : 1.0 - vfSmooth01(-d.y / 20.0);
                    share = vfAttenuate(atten, r, radius) * vt * bt;
                }
            }
        }
        else
        {
            float cs = cos(bearing), sn = sin(bearing);
            float lx = d.x * cs + d.z * sn;
            float lz = -d.x * sn + d.z * cs;
            float hx = max(5.0, D.x), hz = max(5.0, D.y);
            float nrm = max(max(abs(lx) / hx, abs(lz) / hz), d.y < -12.0 ? -d.y / 12.0 : d.y / height);
            share = nrm >= 1.0 ? 0.0 : vfAttenuate(atten, nrm, 1.0);
        }
        if (share <= 1e-5) continue;

        float gain = gate * share;
        vec3 f = vec3(0.0);

        if (model == 0) // LinearWind
        {
            float blow = bearing + PI;
            float altKm = max(0.0, p.y) * 0.001;
            float veerDir = blow + D.y * altKm;
            float shearGain = 1.0 + D.x * min(altKm, 12.0);
            vec3 dir = vec3(sin(veerDir), 0.0, -cos(veerDir));
            float along = dot(d.xz, dir.xz);
            float phase = 2.0 * PI * D.w * time - 0.015 * along;
            float gust = 0.62 * sin(phase) + 0.38 * sin(2.13 * phase + 1.07);
            float spd = strength * shearGain * (1.0 + D.z * gust);
            f = dir * spd;
            if (E.w > 0.01)
            {
                vec3 cn = vfCurlNoise(p - dir * (0.35 * strength * time), 46.0, 31.0);
                f += cn * vec3(1.0, 0.68, 1.0) * (E.w * strength * 0.42);
            }
        }
        else if (model == 1) // FlowStream
        {
            vec2 fwd = vec2(sin(bearing), -cos(bearing));
            vec2 right = vec2(-fwd.y, fwd.x);
            int prof = int(E.w + 0.5);
            if (prof == 1)
            {
                float r = length(d.xz);
                float rho = r / max(6.0, D.z);
                float plume = exp(-0.5 * rho * rho);
                float rad = -D.w * (2.0 * rho / (1.0 + rho * rho)) / max(1e-3, r);
                f = vec3(d.x * rad, strength * plume, d.z * rad);
            }
            else if (prof == 4)
            {
                float along = -dot(d.xz, fwd);
                float lat = dot(d.xz, right);
                float wlen = max(20.0, D.y);
                float hw = max(6.0, D.z);
                float spread = 1.0 + 0.7 * clamp((along + 30.0) / wlen, 0.0, 1.5);
                float ln = lat / (hw * spread);
                float w = exp(-0.5 * ln * ln) * vfSmooth01((along + hw * 2.0) / (hw * 2.0));
                float roll = -ln * E.x * w;
                f.xz = w * fwd * strength + right * roll;
                f.y = max(0.0, 1.0 - 0.7 * ln * ln) * E.x * 0.65 * w;
            }
            else if (prof == 3)
            {
                float lat = dot(d.xz, right);
                float steer = -vfTanh(lat / max(12.0, D.z)) * 0.55;
                f.xz = (fwd + right * steer) * strength;
            }
            else if (prof == 2)
            {
                float perp = length(vec2(dot(d.xz, right), d.y)) / max(6.0, D.z);
                float jet = exp(-0.5 * perp * perp);
                f.xz = fwd * strength * jet;
            }
            else
            {
                float meander = 0.16 * sin(dot(d.xz, fwd) * 0.015 + time * 0.8);
                f.xz = (fwd + right * meander) * strength;
            }
        }
        else if (model == 2) // Buoyancy
        {
            float strat = 1.0 - D.w * clamp(d.y / height, 0.0, 0.85);
            float r = length(d.xz);
            float circ = r > 1e-3 ? -0.15 * strength * (d.y > 40.0 ? 1.0 : -1.0) * exp(-r / max(20.0, radius * 0.4)) / r : 0.0;
            f = vec3(d.x * circ, strength * clamp(D.z, -2.0, 3.0) * strat, d.z * circ);
        }
        else if (model == 3) // RadialBurst
        {
            float r0 = max(1.0, D.z);
            float frontR = r0 + max(5.0, D.x) * age;
            float thick = max(2.0, D.y);
            float dist = length(d);
            float su = (dist - frontR) / thick;
            if (abs(su) <= 3.2 && dist > 1e-3)
            {
                float shell = exp(-0.5 * su * su);
                float dec = pow(max(1e-4, r0 / max(r0, frontR)), 0.65);
                f = d * (strength * dec * shell / dist);
                outWaveGlow = max(outWaveGlow, shell * gate);
            }
        }
        else if (model == 4) // Attractor
        {
            float dist = length(d);
            float soft = max(1.0, D.z);
            vec3 u = dist > 1e-4 ? d / dist : vec3(0.0);
            float tl = length(vec2(-d.z, d.x));
            vec2 tang = tl > 1e-4 ? vec2(-d.z, d.x) / tl : vec2(1.0, 0.0);
            float pull = dist < 1e-4 ? 0.0 : strength * (dist / max(dist, soft));
            f = vec3(-u.x * pull + tang.x * D.w, -u.y * pull, -u.z * pull + tang.y * D.w);
        }
        else if (model == 5) // Repulsor
        {
            float dist = max(1e-3, length(d));
            vec3 u = d / dist;
            float tl = length(vec2(-d.z, d.x));
            vec2 tang = tl > 1e-4 ? vec2(-d.z, d.x) / tl : vec2(1.0, 0.0);
            f = vec3(u.x * strength + tang.x * D.w, u.y * strength, u.z * strength + tang.y * D.w);
        }
        else if (model == 6) // AxisAttractor
        {
            vec3 ax = vfAxisDir(bearing, D.x);
            float proj = clamp(dot(d, ax), -D.y * 0.5, D.y * 0.5);
            vec3 perp = d - ax * proj;
            float pd = max(1e-3, length(perp));
            vec3 n = perp / pd;
            vec3 sw = cross(ax, n);
            f = -n * strength + ax * D.z + sw * D.w;
        }
        else if (model == 7) // SplineForce
        {
            vec2 fwd = vec2(sin(bearing), -cos(bearing));
            vec2 right = vec2(-fwd.y, fwd.x);
            float span = max(40.0, radius * 1.6);
            float along = clamp(dot(d.xz, fwd), -span * 0.5, span * 0.5);
            float s = along / (span * 0.5);
            float latOff = D.z * 65.0 * sin(s * PI * 0.85);
            float vOff = D.w * (1.0 - s * s);
            vec3 cPt = A.xyz + vec3(fwd.x * along + right.x * latOff, vOff, fwd.y * along + right.y * latOff);
            float dLat = D.z * 65.0 * PI * 0.85 * cos(s * PI * 0.85) / (span * 0.5);
            float dVert = D.w * (-2.0 * s) / (span * 0.5);
            vec3 tanV = normalize(vec3(fwd.x + right.x * dLat, dVert, fwd.y + right.y * dLat));
            f = tanV * strength + (cPt - p) * E.x;
        }
        else if (model == 8) // SurfaceAdhesion
        {
            float dh = p.y - D.x;
            float sig = max(4.0, D.y);
            float cling = exp(-(dh * dh) / (sig * sig));
            vec2 dir = vec2(sin(bearing), -cos(bearing));
            f = vec3(dir.x * D.z * cling, -strength * vfTanh(dh / sig) * (0.4 + 0.6 * cling), dir.y * D.z * cling);
        }
        else if (model == 9) // Containment
        {
            float dist = length(d);
            float rin = clamp(D.z, 0.05, 0.92) * radius;
            if (dist > rin && dist > 1e-4)
            {
                float wall = clamp((dist - rin) / max(1.0, radius - rin), 0.0, 1.0);
                vec3 u = d / dist;
                float tl = length(vec2(-d.z, d.x));
                vec2 tang = tl > 1e-4 ? vec2(-d.z, d.x) / tl : vec2(1.0, 0.0);
                f = vec3(-u.x * strength * wall + tang.x * D.w * wall, -u.y * strength * wall, -u.z * strength * wall + tang.y * D.w * wall);
            }
        }
        else if (model == 10) // Vortex
        {
            float r = length(d.xz);
            float core = max(3.0, D.x);
            float rho = r / core;
            float prof = (2.0 * rho) / (1.0 + rho * rho);
            float vt = strength * prof * D.w;
            float hs = clamp(d.y / height, 0.0, 1.0);
            float vr = -D.y * (exp(-max(0.0, d.y) / (0.18 * height)) - 0.3 * vfSmooth01((hs - 0.72) / 0.28)) * prof;
            float vy = D.z * exp(-0.42 * rho * rho);
            vec2 ur = r > 1e-4 ? d.xz / r : vec2(1.0, 0.0);
            f = vec3(ur.y * vt + ur.x * vr, vy, -ur.x * vt + ur.y * vr);
        }
        else if (model == 11) // Torque
        {
            vec3 ax = vfAxisDir(bearing, PI * 0.5 - D.x);
            vec3 cp = cross(ax, d);
            float cpLen = length(cp);
            float lev = clamp(cpLen / max(10.0, radius * 0.65), 0.0, 1.5);
            f = (cpLen > 1e-4 ? cp / cpLen : vec3(0.0)) * (strength * lev * D.y);
        }
        else if (model == 12) // MagnusForce
        {
            vec3 sa = vfAxisDir(bearing, PI * 0.5 - D.x);
            vec3 fd = vfAxisDir(bearing, 0.0);
            f = cross(sa, fd) * (strength * (D.z * 0.2)) + fd * 2.5;
        }
        else if (model == 13) // Drag
        {
            outDamp += (strength + D.z * 10.0) * gain;
            continue;
        }
        else if (model == 14) // Lift
        {
            vec2 ch = vec2(sin(bearing), -cos(bearing));
            f = vec3(ch.x * strength * 0.35 * sin(D.x), strength * D.z * cos(D.x), ch.y * strength * 0.35 * sin(D.x));
        }
        else if (model == 15) // CurlNoise
        {
            float scale = max(6.0, D.x);
            vec2 dr = vec2(sin(bearing), -cos(bearing)) * (D.y * time);
            int mode = int(E.w + 0.5);
            vec3 cn = vfCurlNoise(p - vec3(dr.x, 0.0, dr.y), scale, D.z * 0.01);
            if (mode == 1)
            {
                float along = d.x * sin(bearing) - d.z * cos(bearing);
                float burst = max(0.0, sin(2.0 * PI * 0.25 * time - 0.025 * along));
                f = vec3(sin(bearing) * strength * burst + cn.x * strength * 0.5, cn.y * strength * 0.3, -cos(bearing) * strength * burst + cn.z * strength * 0.5);
            }
            else if (mode == 2)
            {
                vec3 cn2 = vfCurlNoise(p + vec3(9.2 - dr.x, 4.1, -7.5 - dr.y), scale * 0.55, D.z * 0.01 + 7.7);
                float r = max(1.0, length(d.xz));
                f = vec3(cn2.x * strength * 1.15 + (-d.z / r) * strength * 0.45, cn2.y * strength * 0.75, cn2.z * strength * 1.15 + (d.x / r) * strength * 0.45);
            }
            else
            {
                f = cn * strength;
            }
        }
        else if (model == 16) // BrownianNoise
        {
            float cell = max(3.0, D.x);
            float tRate = max(0.5, D.y);
            float qT = floor(time * tRate);
            float fT = vfSmooth01(time * tRate - qT);
            vec3 b0 = vfFastNoise3(p / cell, qT, D.z);
            vec3 b1 = vfFastNoise3(p / cell, qT + 1.0, D.z);
            f = mix(b0, b1, fT) * strength;
        }
        else if (model == 17) // LorentzField
        {
            vec3 b = vfAxisDir(bearing, D.x);
            vec3 v = vec3(-d.z * 0.08 + 10.0, d.y * 0.04, d.x * 0.08);
            vec3 cr = cross(v, b);
            float clen = length(cr);
            f = D.z * strength * ((clen > 1e-4 ? cr / clen : vec3(0.0)) + b * D.w);
        }
        else if (model == 18) // MagneticDipole
        {
            vec3 m = vfAxisDir(bearing, D.x);
            float dist = length(d);
            float soft = max(4.0, D.z);
            float r = max(soft, dist);
            vec3 rHat = dist > 1e-4 ? d / dist : vec3(0.0, 1.0, 0.0);
            vec3 b = 3.0 * dot(m, rHat) * rHat - m;
            float bl = length(b);
            f = (bl > 1e-4 ? b / bl : vec3(0.0)) * (strength * pow(max(1e-4, soft / r), 1.85));
        }
`;

const GLSL_COMPOSITE_TAIL = /* glsl */ `
        if (chan == 0) outVel += f * gain;
        else outAcc += f * gain;
    }

    float dampAtten = exp(-outDamp * 0.15);
    outTotal = outVel * dampAtten + outAcc * 0.65;
}
`;

export function BuildGlslFieldCommon(CustomSlots = [])
{
    const CustomDecls = (CustomSlots || []).map((S) => S.GlslCode).join('\n\n');
    const CustomDispatch = (CustomSlots || []).map((S, Idx) => {
        const SlotId = S.Slot ?? Idx;
        return `
        else if (model == ${19 + SlotId})
        {
            f = vfEvalCustomFormula${SlotId}(p, A.xyz, d, radius, strength, D.z, time, atten, gate);
            gain = gate;
        }
    `;
    }).join('');
    return [GLSL_FIELD_HEADER, CustomDecls, GLSL_COMPOSITE_HEAD, CustomDispatch, GLSL_COMPOSITE_TAIL].join('\n');
}

export const GLSL_FIELD_COMMON = BuildGlslFieldCommon([]);

export const GLSL_COLORMAPS = /* glsl */ `
uniform int uColormap;

vec3 vfPalette5(vec3 c0, vec3 c1, vec3 c2, vec3 c3, vec3 c4, float s)
{
    float t = clamp(s, 0.0, 1.0) * 4.0;
    if (t < 1.0) return mix(c0, c1, t);
    if (t < 2.0) return mix(c1, c2, t - 1.0);
    if (t < 3.0) return mix(c2, c3, t - 2.0);
    return mix(c3, c4, t - 3.0);
}

vec3 vfSampleLUT(int pal, float share)
{
    if (pal == 1)
        return vfPalette5(vec3(0.14,0.09,0.11), vec3(0.27,0.41,0.94), vec3(0.13,0.88,0.77), vec3(0.89,0.88,0.18), vec3(0.91,0.20,0.09), share);
    if (pal == 2)
        return vfPalette5(vec3(0.07,0.04,0.32), vec3(0.49,0.08,0.62), vec3(0.80,0.28,0.47), vec3(0.97,0.58,0.25), vec3(0.96,0.96,0.43), share);
    if (pal == 3)
        return vfPalette5(vec3(0.17,0.48,0.89), vec3(0.42,0.71,0.95), vec3(0.86,0.89,0.91), vec3(0.94,0.60,0.34), vec3(0.88,0.23,0.20), share);
    return vfPalette5(vec3(0.06,0.11,0.15), vec3(0.18,0.43,0.49), vec3(0.35,0.65,0.43), vec3(0.83,0.71,0.40), vec3(0.91,0.42,0.31), share);
}
`;

export const GLSL_BAKE_DECODE = /* glsl */ `
uniform sampler2D uFieldBakeTex;
uniform int uBakeMode;
uniform int uSliceAxis;
uniform float uSliceHalfM;
uniform vec2 uBakeCenter;

vec4 vfDecodeBake(vec2 uv)
{
    vec4 raw = texture(uFieldBakeTex, clamp(uv, 0.004, 0.996));
    if (uBakeMode == 1) return vec4((raw.rgb * 2.0 - 1.0) * 80.0, raw.a);
    return raw;
}

vec2 vfWorldToBakeUv(vec3 p)
{
    float inv2H = 1.0 / (uSliceHalfM * 2.0);
    if (uSliceAxis == 0) return vec2((p.x - uBakeCenter.x) * inv2H + 0.5, -(p.z - uBakeCenter.y) * inv2H + 0.5);
    if (uSliceAxis == 1) return vec2((p.x - uBakeCenter.x) * inv2H + 0.5, (p.y - uBakeCenter.y) * inv2H + 0.5);
    return vec2((p.z - uBakeCenter.x) * inv2H + 0.5, (p.y - uBakeCenter.y) * inv2H + 0.5);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                           1A. 128x128 GPU FIELD BAKE SHADER (ONLY SHADER RUNNING 19-LAW + FORMULA LOOP)
//------------------------------------------------------------------------------------------------------------------------

export const SLICE_BAKE_VERT = /* glsl */ `
out vec2 vBakeUv;
void main()
{
    vBakeUv = uv;
    gl_Position = vec4(position.xy, 0.0, 1.0);
}
`;

export function BuildSliceBakeFrag(CustomSlots = [])
{
    return /* glsl */ `
in vec2 vBakeUv;
out vec4 FragColor;

uniform int   uSliceAxis;
uniform float uSliceHeight;
uniform float uSliceHalfM;
uniform vec2  uBakeCenter;
uniform int   uBakeMode;
${BuildGlslFieldCommon(CustomSlots)}

void main()
{
    float u = (vBakeUv.x * 2.0 - 1.0) * uSliceHalfM;
    float v = (vBakeUv.y * 2.0 - 1.0) * uSliceHalfM;
    vec3 p;
    if (uSliceAxis == 0) p = vec3(uBakeCenter.x + u, uSliceHeight, uBakeCenter.y - v);
    else if (uSliceAxis == 1) p = vec3(uBakeCenter.x + u, uBakeCenter.y + v, uSliceHeight);
    else p = vec3(uSliceHeight, uBakeCenter.y + v, uBakeCenter.x + u);

    vec3 vel, acc, tot;
    float damp, glow;
    vfEvalComposite(p, uTime, vel, acc, damp, tot, glow);

    if (uBakeMode == 1)
    {
        vec3 packedV = clamp(tot / 80.0, -1.0, 1.0) * 0.5 + 0.5;
        FragColor = vec4(packedV, clamp(glow, 0.0, 1.0));
    }
    else
    {
        FragColor = vec4(tot, glow);
    }
}
`;
}

export const SLICE_BAKE_FRAG = BuildSliceBakeFrag([]);

//------------------------------------------------------------------------------------------------------------------------
//                           1B. LIGHTWEIGHT 5-TAP DISPLAY HEATMAP + ISOLINES SHADER (120 FPS)
//------------------------------------------------------------------------------------------------------------------------

export const SLICE_DISPLAY_VERT = /* glsl */ `
out vec2 vPlaneUv;
void main()
{
    vPlaneUv = uv;
    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
`;

export const SLICE_DISPLAY_FRAG = /* glsl */ `
in vec2 vPlaneUv;
out vec4 FragColor;

uniform int   uQuantity;
uniform float uFixedMax;
uniform float uOpacity;
${GLSL_BAKE_DECODE}
${GLSL_COLORMAPS}

void main()
{
    vec4 center = vfDecodeBake(vPlaneUv);
    float glow = center.a;
    float val = length(center.rgb);

    if (uQuantity == 4)
    {
        val = (0.5 * 1.225 * dot(center.rgb, center.rgb)) / 20.0;
    }
    else if (uQuantity == 2 || uQuantity == 3)
    {
        float texelStep = 1.0 / 128.0;
        float worldStep = (uSliceHalfM * 2.0) * texelStep;
        vec3 xp = vfDecodeBake(vPlaneUv + vec2(texelStep, 0.0)).rgb;
        vec3 xm = vfDecodeBake(vPlaneUv - vec2(texelStep, 0.0)).rgb;
        vec3 yp = vfDecodeBake(vPlaneUv + vec2(0.0, texelStep)).rgb;
        vec3 ym = vfDecodeBake(vPlaneUv - vec2(0.0, texelStep)).rgb;
        float inv2 = 0.5 / worldStep;
        if (uQuantity == 3)
        {
            if (uSliceAxis == 0) val = ((xp.x - xm.x) - (yp.z - ym.z)) * inv2 * 22.0;
            else if (uSliceAxis == 1) val = ((xp.x - xm.x) + (yp.y - ym.y)) * inv2 * 22.0;
            else val = ((xp.z - xm.z) + (yp.y - ym.y)) * inv2 * 22.0;
        }
        else
        {
            float vort = uSliceAxis == 0
                ? ((xp.z - xm.z) + (yp.x - ym.x))
                : uSliceAxis == 1
                    ? ((xp.y - xm.y) - (yp.x - ym.x))
                    : ((xp.y - xm.y) - (yp.z - ym.z));
            val = abs(vort) * inv2 * 22.0;
        }
    }

    bool isSigned = (uQuantity == 3);
    float share = isSigned ? clamp(0.5 + val / max(1.0, uFixedMax), 0.0, 1.0) : clamp(val / max(1.0, uFixedMax), 0.0, 1.0);
    vec3 col = vfSampleLUT(isSigned ? 3 : uColormap, share);

    float isoCoord = share * 10.0;
    float isoDist = abs(fract(isoCoord - 0.5) - 0.5) / max(1e-4, fwidth(isoCoord));
    float isoLine = 1.0 - smoothstep(0.0, 1.35, isoDist);
    col = mix(col, vec3(0.95), isoLine * 0.16);

    col = mix(col, vec3(0.96, 0.50, 0.36), clamp(glow * 0.55, 0.0, 0.55));
    float alpha = isSigned ? uOpacity : mix(0.05, uOpacity, smoothstep(0.015, 0.14, share));
    alpha = max(alpha, clamp(glow * 0.72, 0.0, 0.78));
    FragColor = vec4(col, alpha);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                           2. O(1) GPU INSTANCED 3D VECTOR ARROWS SHADER (SAMPLES BAKED TEXTURE)
//------------------------------------------------------------------------------------------------------------------------

export const GPU_ARROWS_VERT = /* glsl */ `
in vec3 aSeedMetres;
out vec3 vColor;
out float vAlpha;

uniform float uWorldScale;
uniform float uFixedMax;
uniform float uQuietCutoff;
${GLSL_BAKE_DECODE}
${GLSL_COLORMAPS}

void main()
{
    vec4 sampleVal = vfDecodeBake(vfWorldToBakeUv(aSeedMetres));
    vec3 tot = sampleVal.rgb;
    float glow = sampleVal.a;
    float mag = length(tot);

    if (mag < uQuietCutoff)
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        vAlpha = 0.0;
        vColor = vec3(0.0);
        return;
    }

    vec3 dir = tot / mag;
    vec3 refUp = abs(dir.y) < 0.96 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(refUp, dir));
    vec3 fwd = cross(dir, right);

    float share = clamp(mag / max(1.0, uFixedMax), 0.0, 1.0);
    float lenScale = 0.85 + share * 2.55;
    float thickScale = 0.75 + share * 0.45;

    vec3 localPos = right * (position.x * thickScale) + dir * (position.y * lenScale) + fwd * (position.z * thickScale);
    vec3 worldPos = aSeedMetres * uWorldScale + localPos;

    vColor = vfSampleLUT(uColormap, share);
    vColor = mix(vColor, vec3(0.98, 0.62, 0.45), clamp(glow * 0.45, 0.0, 0.45));
    vAlpha = 0.94;
    gl_Position = projectionMatrix * viewMatrix * vec4(worldPos, 1.0);
}
`;

export const GPU_ARROWS_FRAG = /* glsl */ `
in vec3 vColor;
in float vAlpha;
out vec4 FragColor;

void main()
{
    if (vAlpha <= 0.01) discard;
    FragColor = vec4(vColor, vAlpha);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                      3. GPU OPTICAL RIBBON TRAIL STREAMLINES (SCREEN-SPACE GAUSSIAN CORE + HALO + STREAK)
//------------------------------------------------------------------------------------------------------------------------

export const STREAMLINE_VERT = /* glsl */ `
in vec3 aTangent;
in vec3 aColor;
in float aProgress;
in float aEdge;
in float aStrandSeed;
in float aMagShare;

uniform float uViewportHeightPx;
uniform float uStreamThickness;

out vec3 vColor;
out float vProgress;
out float vAcrossPx;
out float vSigmaPx;
out float vHaloPx;
out float vFade;
out float vStrandSeed;
out float vMagShare;

void main()
{
    float isOrtho = step(0.5, projectionMatrix[3][3]);
    vec3 perspViewDir = cameraPosition - position;
    float pLen = length(perspViewDir);
    perspViewDir = pLen > 1e-4 ? perspViewDir / pLen : vec3(0.0, 1.0, 0.0);
    vec3 orthoViewDir = normalize(vec3(viewMatrix[0][2], viewMatrix[1][2], viewMatrix[2][2]));
    vec3 viewDir = mix(perspViewDir, orthoViewDir, isOrtho);

    vec3 tangent = normalize(aTangent);
    vec3 lateral = cross(tangent, viewDir);
    float latLen = length(lateral);
    if (latLen < 1e-4)
    {
        vec3 camUp = vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);
        lateral = cross(tangent, camUp);
        latLen = length(lateral);
    }
    lateral = latLen > 1e-4 ? lateral / latLen : vec3(1.0, 0.0, 0.0);

    // Taper strand thickness smoothly at ends, scale cleanly by user Thickness control
    float endShape = pow(max(sin(3.14159265 * aProgress), 0.0), 0.35);
    float speedBoost = 0.86 + 0.34 * clamp(aMagShare, 0.0, 1.0);
    float thick = clamp(uStreamThickness, 0.2, 2.5);
    float sigmaPx = (0.58 + 0.38 * endShape) * speedBoost * thick;
    float haloPx = (2.10 + 1.15 * endShape) * speedBoost * thick;
    float envPx = 3.2 * haloPx;

    vec4 clipCenter = projectionMatrix * viewMatrix * vec4(position, 1.0);
    float denom = mix(max(abs(clipCenter.w), 0.05), 1.0, isOrtho);
    float pxPerWorld = max((0.5 * max(uViewportHeightPx, 64.0) * abs(projectionMatrix[1][1])) / denom, 0.25);

    vec3 worldPos = position + lateral * ((aEdge * envPx) / pxPerWorld);

    vColor = aColor;
    vProgress = aProgress;
    vAcrossPx = aEdge * envPx;
    vSigmaPx = sigmaPx;
    vHaloPx = haloPx;
    vFade = endShape;
    vStrandSeed = aStrandSeed;
    vMagShare = aMagShare;

    gl_Position = projectionMatrix * viewMatrix * vec4(worldPos, 1.0);
}
`;

export const STREAMLINE_FRAG = /* glsl */ `
in vec3 vColor;
in float vProgress;
in float vAcrossPx;
in float vSigmaPx;
in float vHaloPx;
in float vFade;
in float vStrandSeed;
in float vMagShare;

out vec4 FragColor;

uniform float uTime;
uniform float uStreamEmissive;
uniform float uStreamOpacity;

void main()
{
    // Sub-pixel AA dual Gaussian profile: sharp luminous spine + silky optical aura
    float core = exp(-0.5 * (vAcrossPx * vAcrossPx) / max(0.04, vSigmaPx * vSigmaPx));
    float halo = 0.42 * exp(-0.5 * (vAcrossPx * vAcrossPx) / max(0.25, vHaloPx * vHaloPx));

    // Per-strand travelling directional comet streak along flow direction (0 -> 1)
    float speed = 0.14 + 0.12 * fract(vStrandSeed * 7.13) + 0.08 * clamp(vMagShare, 0.0, 1.0);
    float win = 0.30;
    float cyc = fract(vStrandSeed + uTime * speed);
    float head = cyc * (1.0 + win);
    float dist = head - vProgress;
    float front = smoothstep(0.0, 0.035, dist);
    float tail = pow(clamp(1.0 - dist / win, 0.0, 1.0), 1.65);
    float streak = (dist >= 0.0 && dist <= win) ? front * tail : 0.0;

    // Bright pinpoint front bead right behind comet tip for clear flow-direction polarity
    float tipDist = (vProgress - clamp(head - 0.012, 0.0, 1.0)) / 0.022;
    float headSpark = exp(-0.5 * tipDist * tipDist) * step(head, 1.02) * step(0.01, head);

    float ends = smoothstep(0.0, 0.045, vProgress) * (1.0 - smoothstep(0.955, 1.0, vProgress));
    float vary = 0.80 + 0.32 * fract(vStrandSeed * 19.73);
    float baseDim = 0.58 + 0.28 * clamp(vMagShare, 0.0, 1.0);
    float emissive = clamp(uStreamEmissive, 0.15, 3.0);
    float lit = (baseDim + 0.85 * streak + 0.65 * headSpark) * emissive;

    vec3 highlightCol = min(vColor * (1.30 + 0.28 * emissive) + vec3(0.14), vec3(1.0));
    vec3 sparkCol = mix(vColor * min(1.0 + 0.22 * max(0.0, emissive - 1.0), 1.45), highlightCol, clamp(streak * 0.36 + headSpark * 0.68, 0.0, 0.80));
    float alpha = clamp((core * 0.95 + halo * 0.48) * lit * ends * vFade * vary * clamp(uStreamOpacity, 0.05, 1.0), 0.0, 0.98);
    if (alpha <= 0.012) discard;

    FragColor = vec4(clamp(sparkCol, 0.0, 1.0), alpha);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                      4. GPU BOKEH STREAM RIBBONS (TAPERED FADING WAKES + CREAMY / SHARP BOKEH BEADS)
//------------------------------------------------------------------------------------------------------------------------

export const BOKEH_RIBBON_VERT = /* glsl */ `
in vec3 aTangent;
in vec3 aColor;
in vec2 aUv;
in float aAlpha;

uniform float uViewportHeightPx;
uniform float uBokehThickness;

out vec3 vColor;
out vec2 vUv;
out float vAlpha;

void main()
{
    float isOrtho = step(0.5, projectionMatrix[3][3]);
    vec3 perspViewDir = cameraPosition - position;
    float pLen = length(perspViewDir);
    perspViewDir = pLen > 1e-4 ? perspViewDir / pLen : vec3(0.0, 1.0, 0.0);
    vec3 orthoViewDir = normalize(vec3(viewMatrix[0][2], viewMatrix[1][2], viewMatrix[2][2]));
    vec3 viewDir = mix(perspViewDir, orthoViewDir, isOrtho);

    vec3 tangent = normalize(aTangent);
    vec3 lateral = cross(tangent, viewDir);
    float latLen = length(lateral);
    if (latLen < 1e-4)
    {
        vec3 camUp = vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);
        lateral = cross(tangent, camUp);
        latLen = length(lateral);
    }
    lateral = latLen > 1e-4 ? lateral / latLen : vec3(1.0, 0.0, 0.0);

    vec4 clipCenter = projectionMatrix * viewMatrix * vec4(position, 1.0);
    float denom = mix(max(abs(clipCenter.w), 0.05), 1.0, isOrtho);
    float pxPerWorld = max((0.5 * max(uViewportHeightPx, 64.0) * abs(projectionMatrix[1][1])) / denom, 0.25);

    // Taper from sleek luminous head behind bead down to hairline tail
    float uLen = clamp(aUv.y, 0.0, 1.0);
    float thick = clamp(uBokehThickness, 0.25, 2.5);
    float halfPx = mix(3.8, 0.42, pow(max(uLen, 1e-4), 0.72)) * thick;
    float worldHalfW = max(halfPx / pxPerWorld, 0.006 * thick);

    vec3 worldPos = position + lateral * (aUv.x * worldHalfW);

    vColor = aColor;
    vUv = aUv;
    vAlpha = aAlpha;

    gl_Position = projectionMatrix * viewMatrix * vec4(worldPos, 1.0);
}
`;

export const BOKEH_RIBBON_FRAG = /* glsl */ `
in vec3 vColor;
in vec2 vUv;
in float vAlpha;

uniform float uBokehEmissive;

out vec4 FragColor;

void main()
{
    float edge = exp(-vUv.x * vUv.x * 4.2);
    float tailFade = pow(clamp(1.0 - vUv.y, 0.0, 1.0), 1.42);
    float headGlow = smoothstep(0.0, 0.035, vUv.y);
    float glow = clamp(uBokehEmissive, 0.25, 2.5);
    float a = clamp(edge * tailFade * (0.48 + 0.52 * headGlow) * vAlpha * 0.78 * glow, 0.0, 1.0);
    if (a <= 0.003) discard;

    FragColor = vec4(vColor * a, a);
}
`;

export const BOKEH_BEAD_VERT = /* glsl */ `
in vec2 aCorner;
in vec3 aColor;
in float aDof;
in float aRadiusPx;
in float aAlpha;

uniform float uViewportHeightPx;
uniform float uBokehThickness;

out vec2 vLocal;
out vec3 vColor;
out float vDof;
out float vAlpha;

void main()
{
    float isOrtho = step(0.5, projectionMatrix[3][3]);
    vec3 camRight = normalize(vec3(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]));
    vec3 camUp = normalize(vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]));

    vec4 clipCenter = projectionMatrix * viewMatrix * vec4(position, 1.0);
    float denom = mix(max(abs(clipCenter.w), 0.05), 1.0, isOrtho);
    float pxPerWorld = max((0.5 * max(uViewportHeightPx, 64.0) * abs(projectionMatrix[1][1])) / denom, 0.25);

    float thick = clamp(uBokehThickness, 0.25, 2.5);
    float worldR = (aRadiusPx * thick) / pxPerWorld;
    vec3 worldPos = position + (camRight * aCorner.x + camUp * aCorner.y) * worldR;

    vLocal = aCorner;
    vColor = aColor;
    vDof = aDof;
    vAlpha = aAlpha;

    gl_Position = projectionMatrix * viewMatrix * vec4(worldPos, 1.0);
}
`;

export const BOKEH_BEAD_FRAG = /* glsl */ `
in vec2 vLocal;
in vec3 vColor;
in float vDof;
in float vAlpha;

uniform float uBokehEmissive;

out vec4 FragColor;

void main()
{
    float r = length(vLocal);
    if (r > 1.0) discard;

    float dof = clamp(vDof, 0.0, 1.0);
    float dof2 = dof * dof;
    float sharpCore = exp(-r * r * mix(28.0, 6.2, dof2));
    float bokehDisc = smoothstep(1.0, 0.72, r) * (0.09 + 0.14 * dof) * mix(0.14, 0.68, dof2);
    float rimRing = exp(-pow((r - 0.76) * 5.6, 2.0)) * 0.18 * dof2 * dof;

    float glow = clamp(uBokehEmissive, 0.25, 2.5);
    float shapeAlpha = clamp(sharpCore * mix(1.0, 0.55, dof2) + bokehDisc + rimRing, 0.0, 1.0);
    float a = clamp(shapeAlpha * vAlpha * glow, 0.0, 1.0);
    if (a <= 0.004) discard;

    vec3 rgb = mix(vColor, vec3(1.0), clamp(sharpCore * 0.90, 0.0, 0.95));
    FragColor = vec4(rgb * a, a);
}
`;

