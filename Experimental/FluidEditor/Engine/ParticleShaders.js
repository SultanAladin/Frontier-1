//------------------------------------------------------------------------------------------------------------------------
//                                                  PARTICLE SHADERS
//          GLSL ES 3.00 — Shape Sampling, Mocap Rig, MoGraph Effector, Dissolve/Derez, Swarm, Glitch & Sprite Rendering
//------------------------------------------------------------------------------------------------------------------------

import { VolumeChunk, NoiseChunk, ForceFieldChunk, RampChunk } from './FluidShaders.js';

/** Deterministic shape sampling + Phase 5 Mocap Rig, MoGraph Effector & Dissolve/Derez helpers shared by update & render. */
export const ParticleShapeChunk = `
uniform int   uShape;        // 0 Point, 1 Sphere, 2 Box, 3 Disc, 4 Ring, 5 Line, 6 Grid, 7 Torus, 8 SurfaceShell, 9 MocapFigure, 10 Dome,
                             // 11 ObjectSurface, 12 Spiderweb, 13 ClothGrid, 14 FloraBed, 15 FireworkBurst
uniform vec3  uOrigin;
uniform vec3  uAxis;
uniform float uRadius;
uniform int   uCount;
uniform float uFill;
uniform float uShapeDetail;   // spokes / fold count / blade count / burst lobes (0 = shape default)
uniform sampler2D uAnchors;   // RGBA32F, two texels per anchor: position.xyz|localY, normal.xyz|ridge
uniform int   uAnchorWidth;
uniform int   uAnchorCount;

uniform int   uMocapClip;        // 0 Off, 1 Sprint, 2 Dance, 3 Martial, 4 Levitate
uniform float uMocapAmplitude;
uniform float uMocapSpeed;
uniform float uMocapScale;
uniform float uMocapTrailKick;

uniform int   uEffectorMode;     // 0 Off, 1 Ripple, 2 Orbiters, 3 PlaneWave, 4 NoiseField
uniform float uEffectorStrength;
uniform float uEffectorFrequency;
uniform float uEffectorSpeed;
uniform float uEffectorPush;
uniform float uEffectorScaleBoost;
uniform float uEffectorColourShift;

uniform int   uDerezMode;        // 0 Off, 1 Derez, 2 ReverseDerez, 3 MeltDissolve, 4 ButterflyMorph, 5 CellGlitch
uniform float uDerezMix;
uniform float uDerezProgress;
uniform float uDerezAutoSpeed;
uniform float uDerezEdgeWidth;
uniform float uDerezEdgeGlow;
uniform vec3  uDerezEdgeColour;
uniform float uDerezShatter;

uniform float uSwarmWingRate;
uniform float uGlitchAmount;
uniform float uGlitchRate;
uniform float uGlitchStepJitter;
uniform float uGlitchScanlines;

void ShapeBasis(out vec3 t, out vec3 b)
{
    vec3 n = uAxis;
    vec3 helper = abs(n.y) < 0.95 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    t = normalize(cross(helper, n));
    b = cross(n, t);
}

// 6-face voxelized box shell (ported from Particle Editor surfaceLocal).
vec3 BoxShellLocal(int index, float count)
{
    int g = max(1, int(floor(sqrt(max(count, 6.0) / 6.0))));
    int cells = g * g;
    int face = index - (index / 6) * 6;
    int cell = (index / 6) % cells;
    int cx = cell - (cell / g) * g;
    int cy = cell / g;
    float cu = (float(cx) + 0.5) / float(g) * 2.0 - 1.0;
    float cv = (float(cy) + 0.5) / float(g) * 2.0 - 1.0;
    int ax = face / 2;
    float sg = (face - ax * 2) == 0 ? 1.0 : -1.0;
    if (ax == 0) return vec3(sg, cu, cv);
    if (ax == 1) return vec3(cv, sg, cu);
    return vec3(cu, cv, sg);
}

// Anchor fetch for the 3D Object spawn (two RGBA32F texels per anchor, uploaded from ObjectMeshCatalog.js).
vec4 AnchorTexel(int anchorSlot)
{
    int x = anchorSlot - (anchorSlot / uAnchorWidth) * uAnchorWidth;
    int y = anchorSlot / uAnchorWidth;
    return texelFetch(uAnchors, ivec2(x, y), 0);
}

// Object-surface spawn: the mesh anchors are baked on the CPU (EvenSurface / SurfaceVolume / RidgeAccent) and read per particle.
vec3 ObjectAnchorPosition(int index, out vec3 normal)
{
    if (uAnchorCount < 1) { normal = vec3(0.0, 1.0, 0.0); return vec3(0.0); }
    int slot = min(index, uAnchorCount - 1);
    vec4 first = AnchorTexel(slot * 2);
    vec4 second = AnchorTexel(slot * 2 + 1);
    normal = second.xyz;
    return first.xyz;
}

// 11-capsule / 15-joint articulated motion-capture performer rig evaluated in local performer space.
void MocapBoneEndpoints(int bone, float phase, int clip, out vec3 a, out vec3 b, out float rad)
{
    float s = sin(phase), c = cos(phase);
    float s2 = sin(phase * 2.0), c2 = cos(phase * 2.0);
    vec3 pelvis = vec3(0.0, 0.05, 0.0);
    vec3 chest  = vec3(0.0, 0.52, 0.0);
    vec3 neck   = vec3(0.0, 0.68, 0.0);
    vec3 head   = vec3(0.0, 0.92, 0.0);
    vec3 shL = vec3(-0.22, 0.62, 0.0), shR = vec3(0.22, 0.62, 0.0);
    vec3 elL = vec3(-0.36, 0.34, 0.04), elR = vec3(0.36, 0.34, 0.04);
    vec3 wrL = vec3(-0.44, 0.08, 0.10), wrR = vec3(0.44, 0.08, 0.10);
    vec3 hpL = vec3(-0.12, 0.02, 0.0), hpR = vec3(0.12, 0.02, 0.0);
    vec3 knL = vec3(-0.14, -0.44, 0.03), knR = vec3(0.14, -0.44, 0.03);
    vec3 anL = vec3(-0.15, -0.88, 0.0), anR = vec3(0.15, -0.88, 0.0);

    if (clip == 1)
    {
        // Sprint cycle: athletic forward lean, counter-rotating shoulders & hips, high knee drive.
        float bob = abs(s) * 0.07;
        pelvis = vec3(s * 0.03, 0.04 + bob, 0.0);
        chest  = vec3(0.0, 0.50 + bob, 0.12);
        neck   = vec3(0.0, 0.66 + bob, 0.16);
        head   = vec3(0.0, 0.89 + bob, 0.19);
        shL = chest + vec3(-0.21, 0.10, -s * 0.06);
        shR = chest + vec3( 0.21, 0.10,  s * 0.06);
        elL = shL + vec3(-0.06, -0.24, -s * 0.22 - 0.04);
        wrL = elL + vec3( 0.04,  0.06 + max(0.0, -s) * 0.12, -s * 0.18 + 0.22);
        elR = shR + vec3( 0.06, -0.24,  s * 0.22 - 0.04);
        wrR = elR + vec3(-0.04,  0.06 + max(0.0,  s) * 0.12,  s * 0.18 + 0.22);
        hpL = pelvis + vec3(-0.12, -0.02,  s * 0.04);
        hpR = pelvis + vec3( 0.12, -0.02, -s * 0.04);
        float liftL = max(0.0,  s), liftR = max(0.0, -s);
        knL = hpL + vec3(-0.01, -0.38 + liftL * 0.24,  s * 0.30 + 0.06);
        anL = knL + vec3( 0.0,  -0.40 + liftL * 0.14,  s * 0.22 - 0.18);
        knR = hpR + vec3( 0.01, -0.38 + liftR * 0.24, -s * 0.30 + 0.06);
        anR = knR + vec3( 0.0,  -0.40 + liftR * 0.14, -s * 0.22 - 0.18);
    }
    else if (clip == 2)
    {
        // Breakdance / groove cycle: lateral hip sway, bounce, alternating overhead arm wave & step-touch.
        float bounce = -abs(s2) * 0.08;
        float sway = s * 0.16;
        pelvis = vec3(sway, 0.04 + bounce, 0.0);
        chest  = vec3(sway * 0.55, 0.50 + bounce, c * 0.05);
        neck   = chest + vec3(-sway * 0.15, 0.16, 0.02);
        head   = neck + vec3(s2 * 0.05, 0.23, 0.02);
        shL = chest + vec3(-0.22, 0.10 + s * 0.05, 0.0);
        shR = chest + vec3( 0.22, 0.10 - s * 0.05, 0.0);
        elL = shL + vec3(-0.20, 0.06 + s * 0.22, 0.12);
        wrL = elL + vec3(-0.10 + c * 0.12, 0.20 + s * 0.22, 0.14);
        elR = shR + vec3( 0.20, 0.06 - s * 0.22, 0.12);
        wrR = elR + vec3( 0.10 - c * 0.12, 0.20 - s * 0.22, 0.14);
        hpL = pelvis + vec3(-0.12, -0.02, 0.0);
        hpR = pelvis + vec3( 0.12, -0.02, 0.0);
        knL = hpL + vec3(-0.08 + max(0.0, -s) * 0.12, -0.40, 0.10 + max(0.0, s) * 0.14);
        anL = vec3(-0.22 + sway * 0.4, -0.88 + max(0.0, s) * 0.16, s * 0.14);
        knR = hpR + vec3( 0.08 - max(0.0,  s) * 0.12, -0.40, 0.10 + max(0.0, -s) * 0.14);
        anR = vec3( 0.22 + sway * 0.4, -0.88 + max(0.0, -s) * 0.16, -s * 0.14);
    }
    else if (clip == 3)
    {
        // Martial kata: rotating fighting stance, snapping cross punch & roundhouse kick arc.
        float strike = pow(max(0.0, s), 2.0);
        float kick = pow(max(0.0, -s), 2.0);
        float yaw = c * 0.45;
        float cy = cos(yaw), sy = sin(yaw);
        pelvis = vec3(0.0, -0.02, 0.0);
        chest  = vec3(sy * 0.08, 0.46, cy * 0.06);
        neck   = chest + vec3(0.0, 0.16, 0.02);
        head   = neck + vec3(0.0, 0.22, 0.04);
        shL = chest + vec3(-0.21 * cy, 0.10, -0.21 * sy);
        shR = chest + vec3( 0.21 * cy, 0.10,  0.21 * sy);
        elL = shL + vec3(-0.12, -0.16, 0.18 + strike * 0.18);
        wrL = elL + vec3( 0.06,  0.04, 0.20 + strike * 0.36);
        elR = shR + vec3( 0.14, -0.18, 0.12);
        wrR = elR + vec3(-0.08,  0.12, 0.18);
        hpL = pelvis + vec3(-0.13, -0.02, -0.05);
        hpR = pelvis + vec3( 0.13, -0.02,  0.05);
        knL = hpL + vec3(-0.08, -0.42, -0.08);
        anL = vec3(-0.24, -0.88, -0.14);
        knR = hpR + vec3( 0.08, -0.36 + kick * 0.42, 0.16 + kick * 0.22);
        anR = knR + vec3( 0.08 * kick, -0.44 + kick * 0.48, 0.10 + kick * 0.38);
    }
    else if (clip == 4)
    {
        // Zero-G levitation: weightless arched spine, slow hover drift & open floating limbs.
        float hover = sin(phase * 0.7) * 0.14;
        pelvis = vec3(0.0, 0.12 + hover, 0.0);
        chest  = vec3(0.0, 0.58 + hover, -0.06);
        neck   = chest + vec3(0.0, 0.16, -0.03);
        head   = neck + vec3(0.0, 0.22, -0.04);
        shL = chest + vec3(-0.22, 0.09, 0.0);
        shR = chest + vec3( 0.22, 0.09, 0.0);
        elL = shL + vec3(-0.22,  0.02 + s * 0.08, 0.08);
        wrL = elL + vec3(-0.18, -0.06 + c * 0.10, 0.12);
        elR = shR + vec3( 0.22,  0.02 - s * 0.08, 0.08);
        wrR = elR + vec3( 0.18, -0.06 - c * 0.10, 0.12);
        hpL = pelvis + vec3(-0.12, -0.03, 0.0);
        hpR = pelvis + vec3( 0.12, -0.03, 0.0);
        knL = hpL + vec3(-0.06, -0.38,  0.14 + s * 0.06);
        anL = knL + vec3(-0.03, -0.38, -0.12);
        knR = hpR + vec3( 0.06, -0.34,  0.22 - s * 0.06);
        anR = knR + vec3( 0.03, -0.38, -0.10);
    }

    if (bone == 0)      { a = neck;   b = head;  rad = 0.105; }
    else if (bone == 1) { a = pelvis; b = chest; rad = 0.155; }
    else if (bone == 2) { a = hpL;    b = hpR;   rad = 0.125; }
    else if (bone == 3) { a = shL;    b = elL;   rad = 0.055; }
    else if (bone == 4) { a = elL;    b = wrL;   rad = 0.044; }
    else if (bone == 5) { a = shR;    b = elR;   rad = 0.055; }
    else if (bone == 6) { a = elR;    b = wrR;   rad = 0.044; }
    else if (bone == 7) { a = hpL;    b = knL;   rad = 0.075; }
    else if (bone == 8) { a = knL;    b = anL;   rad = 0.054; }
    else if (bone == 9) { a = hpR;    b = knR;   rad = 0.075; }
    else                { a = knR;    b = anR;   rad = 0.054; }
}

vec3 MocapSamplePoint(int index, vec3 rnd, bool lattice, float phase, int clip)
{
    // Weighted bone allocation so torso & thighs receive proportionally more particles than forearms.
    int slot = index - (index / 16) * 16;
    int bone = slot < 2 ? 0 : (slot < 5 ? 1 : (slot == 5 ? 2 : (slot == 6 ? 3 : (slot == 7 ? 4 : (slot == 8 ? 5 : (slot == 9 ? 6 : (slot < 12 ? 7 : (slot == 12 ? 8 : (slot < 15 ? 9 : 10)))))))));
    vec3 a, b; float rad;
    MocapBoneEndpoints(bone, phase, clip, a, b, rad);
    float along = lattice ? fract(float(index) * 0.6180339 + float(bone) * 0.13) : rnd.x;
    float ang = lattice ? float(index) * 2.39996323 : rnd.y * 6.2831853;
    float shell = mix(1.0, sqrt(lattice ? fract(float(index) * 0.381966) : rnd.z), uFill);
    vec3 seg = b - a;
    vec3 dir = length(seg) > 1e-4 ? normalize(seg) : vec3(0.0, 1.0, 0.0);
    vec3 helper = abs(dir.y) < 0.92 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 u = normalize(cross(helper, dir));
    vec3 v = cross(dir, u);
    vec3 local = mix(a, b, along) + (u * cos(ang) + v * sin(ang)) * (rad * shell);
    if (bone == 0)
    {
        // Spherical cranium volume around head midpoint.
        vec3 hc = mix(a, b, 0.58);
        float hz = along * 2.0 - 1.0;
        float hr = sqrt(max(0.0, 1.0 - hz * hz));
        local = hc + vec3(cos(ang) * hr, hz, sin(ang) * hr) * (rad * 1.08 * shell);
    }
    float figureScale = max(0.25, uRadius * uMocapScale);
    return uOrigin + local * figureScale;
}

vec3 EvaluateMocapPose(int index, vec3 rnd, bool lattice, float time, out vec3 limbVelocity)
{
    limbVelocity = vec3(0.0);
    float phase = time * uMocapSpeed * 4.2;
    vec3 rest = MocapSamplePoint(index, rnd, lattice, 0.0, 0);
    if (uMocapClip <= 0 || uMocapAmplitude <= 0.0001) return rest;
    vec3 posed = MocapSamplePoint(index, rnd, lattice, phase, uMocapClip);
    vec3 prev  = MocapSamplePoint(index, rnd, lattice, phase - 0.12 * max(uMocapSpeed, 0.1), uMocapClip);
    float amp = clamp(uMocapAmplitude, 0.0, 2.0);
    limbVelocity = (posed - prev) * (8.0 * amp * uMocapTrailKick);
    return mix(rest, posed, amp);
}

// index/count drive lattice placement for cloners; rnd (3 uniform randoms) drives emission spawns.
vec3 ShapePointAtTime(int index, vec3 rnd, bool lattice, float time, out vec3 shapeVelocity)
{
    shapeVelocity = vec3(0.0);
    vec3 t, b; ShapeBasis(t, b);
    float count = float(max(uCount, 1));
    float i = float(index);
    if (uShape == 9)
    {
        return EvaluateMocapPose(index, rnd, lattice, time, shapeVelocity);
    }
    vec3 basePos = uOrigin;
    if (uShape == 1)
    {
        if (lattice)
        {
            float golden = 2.39996323;
            float y = 1.0 - (i + 0.5) / count * 2.0;
            float r = sqrt(max(0.0, 1.0 - y * y));
            float a = golden * i;
            float shell = mix(1.0, pow(Hash31(vec3(i, 3.1, 7.7)), 1.0 / 3.0), uFill);
            basePos = uOrigin + vec3(cos(a) * r, y, sin(a) * r) * uRadius * shell;
        }
        else
        {
            vec3 d = normalize(rnd * 2.0 - 1.0 + 1e-4);
            basePos = uOrigin + d * uRadius * pow(Hash31(rnd.zxy + 2.7), 1.0 / 3.0);
        }
    }
    else if (uShape == 2 || uShape == 6)
    {
        if (lattice || uShape == 6)
        {
            float side = ceil(pow(count, 1.0 / 3.0));
            float x = mod(i, side), y = mod(floor(i / side), side), z = floor(i / (side * side));
            vec3 cell = (vec3(x, y, z) + 0.5) / side * 2.0 - 1.0;
            basePos = uOrigin + (t * cell.x + uAxis * cell.y + b * cell.z) * uRadius;
        }
        else
        {
            vec3 cell = rnd * 2.0 - 1.0;
            basePos = uOrigin + (t * cell.x + uAxis * cell.y + b * cell.z) * uRadius;
        }
    }
    else if (uShape == 3)
    {
        float a, r;
        if (lattice) { a = 2.39996323 * i; r = sqrt((i + 0.5) / count); }
        else { a = rnd.x * 6.2831853; r = sqrt(rnd.y); }
        basePos = uOrigin + (t * cos(a) + b * sin(a)) * r * uRadius;
    }
    else if (uShape == 4)
    {
        float a = lattice ? i / count * 6.2831853 : rnd.x * 6.2831853;
        basePos = uOrigin + (t * cos(a) + b * sin(a)) * uRadius;
    }
    else if (uShape == 5)
    {
        float s = lattice ? (i + 0.5) / count * 2.0 - 1.0 : rnd.x * 2.0 - 1.0;
        basePos = uOrigin + uAxis * s * uRadius;
    }
    else if (uShape == 7)
    {
        // Torus around uAxis: major radius uRadius, minor tube radius scaled by uFill.
        float u = lattice ? (i + 0.5) / count * 6.2831853 : rnd.x * 6.2831853;
        float v = lattice ? fract(i * 0.6180339887) * 6.2831853 : rnd.y * 6.2831853;
        float tube = uRadius * mix(0.28, 0.44, clamp(uFill, 0.0, 1.0));
        float shell = lattice ? 1.0 : mix(1.0, sqrt(rnd.z), uFill);
        vec3 radial = t * cos(u) + b * sin(u);
        basePos = uOrigin + radial * (uRadius + cos(v) * tube * shell) + uAxis * (sin(v) * tube * shell);
    }
    else if (uShape == 8)
    {
        // 6-face Box Shell (Derez / Reverse Derez / Voxel surface).
        vec3 cell = lattice ? BoxShellLocal(index, count) : BoxShellLocal(int(rnd.x * count), count);
        basePos = uOrigin + (t * cell.x + uAxis * cell.y + b * cell.z) * uRadius;
    }
    else if (uShape == 10)
    {
        // Hemisphere Dome oriented along uAxis (Energy Shield barrier canopy).
        float y = lattice ? 1.0 - (i + 0.5) / count : rnd.x;
        float r = sqrt(max(0.0, 1.0 - y * y));
        float a = lattice ? 2.39996323 * i : rnd.y * 6.2831853;
        float shell = mix(1.0, pow(lattice ? fract(i * 0.381966) : rnd.z, 1.0 / 3.0), uFill);
        basePos = uOrigin + (t * (cos(a) * r) + uAxis * y + b * (sin(a) * r)) * uRadius * shell;
    }
    else if (uShape == 11)
    {
        // Object Surface: baked mesh anchors (surface, interior volume or ridge accent) scaled by Radius and oriented by Direction.
        vec3 normal;
        vec3 local = ObjectAnchorPosition(index, normal);
        // A particle sits fractionally inside along its own normal when Fill > 0, so the cloud reads as a volume shell.
        vec3 outward = local + normal * uFill * Hash31(rnd.zxy + 3.3) * 0.35;
        basePos = uOrigin + (t * outward.x + uAxis * outward.y + b * outward.z) * uRadius;
    }
    else if (uShape == 12)
    {
        // Spiderweb: radial spokes stitched to concentric rings, each ring sagging on a catenary toward the hub.
        float detail = uShapeDetail > 0.5 ? uShapeDetail : 8.0;
        int spokes = int(clamp(detail, 4.0, 24.0));
        int rings = int(clamp(detail * 1.75, 3.0, 34.0));
        int spokeCount = spokes * (rings + 1);
        bool onSpoke = (index % 4) < 2;
        float angle, radius;
        if (onSpoke)
        {
            angle = float(index % spokes) / float(spokes) * 6.2831853;
            float step = lattice ? fract(float(index / spokes) * 0.6180339887) : rnd.y;
            radius = (float(index / max(spokeCount, 1)) + step) / float(rings + 1);
            radius = clamp(radius, 0.06, 1.0);
        }
        else
        {
            float ring = float((index / max(spokes, 1)) % max(rings, 1));
            angle = float(index % max(spokes, 1)) / float(spokes) * 6.2831853 + (lattice ? 0.0 : rnd.x * 0.12);
            radius = (ring + 0.5) / float(rings);
        }
        float sag = uFill * 0.42 * sin(3.14159265 * clamp(radius, 0.0, 1.0));
        basePos = uOrigin + (t * (cos(angle) * radius) + b * (sin(angle) * radius)) * uRadius - uAxis * sag * uRadius;
    }
    else if (uShape == 13)
    {
        // Cloth Grid: a hanging sheet of uCount villi with vertical drape folds and a scalloped free edge.
        float detail = uShapeDetail > 0.5 ? uShapeDetail : 6.0;
        float folds = max(2.0, detail);
        float side = max(2.0, floor(sqrt(count)));
        float fx = mod(i, side), fy = floor(i / side);
        float u = (fx + 0.5) / side * 2.0 - 1.0;
        float v = (fy + 0.5) / side;
        v = min(v, 1.0);
        float sway = sin(u * 3.14159265 * folds) * (0.10 + v * 0.22) * (0.4 + uFill);
        float scallop = sin(u * 3.14159265 * folds * 0.5) * 0.06 * v;
        basePos = uOrigin + t * u * uRadius + uAxis * (0.5 - v) * uRadius * 1.6 + b * (sway + scallop) * uRadius;
    }
    else if (uShape == 14)
    {
        // Flora Bed: blades rooted on a disc, each particle a fraction up a gently wind-bent stalk.
        float blades = clamp(uShapeDetail > 0.5 ? uShapeDetail : 26.0, 3.0, 512.0);
        int perBlade = max(1, int(ceil(count / blades)));
        int blade = index / perBlade;
        float h = float(index - blade * perBlade) / float(perBlade);
        float rootAngle = Hash31(vec3(float(blade), 1.7, 9.3)) * 6.2831853;
        float rootRadius = sqrt(Hash31(vec3(float(blade), 4.1, 2.7))) * uRadius;
        float height = mix(0.55, 1.65, Hash31(vec3(float(blade), 7.7, 5.1))) * uRadius;
        float bend = (0.35 + uFill) * pow(h, 1.6) * uRadius;
        float flutter = sin(h * 3.0 + rootAngle * 3.0 + time * 1.4) * 0.06 * h * uRadius;
        vec3 root = t * (cos(rootAngle) * rootRadius) + b * (sin(rootAngle) * rootRadius);
        basePos = uOrigin + root + uAxis * (h * height) + b * (bend + flutter);
    }
    else if (uShape == 15)
    {
        // Firework Burst: a lobed shell — chrysanthemum petals, palm fronds or a ringed sphere depending on Shape Detail.
        float detail = uShapeDetail > 0.5 ? uShapeDetail : 6.0;
        float lobes = max(2.0, floor(detail));
        float y = lattice ? 1.0 - (i + 0.5) / count * 2.0 : rnd.x * 2.0 - 1.0;
        float r = sqrt(max(0.0, 1.0 - y * y));
        float a = lattice ? 2.39996323 * i : rnd.y * 6.2831853;
        float lobe = 0.78 + 0.22 * abs(cos(a * lobes * 0.5));
        float shell = mix(lobe, pow(max(rnd.z, 1e-4), 1.0 / 3.0), clamp(uFill, 0.0, 1.0));
        basePos = uOrigin + (t * (cos(a) * r) + uAxis * y + b * (sin(a) * r)) * uRadius * shell;
    }

    // Optional Mocap performer modulation even when spawned on a non-figure shape: warps cloud around animated skeleton!
    if (uMocapClip > 0 && uMocapAmplitude > 0.001)
    {
        vec3 limbVel;
        vec3 fig = EvaluateMocapPose(index, rnd, lattice, time, limbVel);
        shapeVelocity = limbVel;
        basePos = mix(basePos, fig, clamp(uMocapAmplitude, 0.0, 1.0));
    }
    return basePos;
}

vec3 ShapePoint(int index, vec3 rnd, bool lattice)
{
    vec3 unusedVelocity;
    return ShapePointAtTime(index, rnd, lattice, 0.0, unusedVelocity);
}

// MoGraph spatial effector weight in [0, 1] (ported from Particle Editor evalGridEffector).
float EvaluateEffector(vec3 pos, float time)
{
    if (uEffectorMode <= 0 || uEffectorStrength <= 0.0001) return 0.0;
    float freq = max(0.15, uEffectorFrequency);
    vec3 dVec = (pos - uOrigin) * freq;
    float w = 0.0;
    if (uEffectorMode == 1)
    {
        float dist = length(pos - uOrigin);
        float ph = dist * freq * 3.4 - time * uEffectorSpeed * 3.1;
        w = pow(max(0.0, 0.5 + 0.5 * sin(ph)), 1.35);
    }
    else if (uEffectorMode == 2)
    {
        float ph0 = time * uEffectorSpeed * 1.35;
        float ph1 = time * uEffectorSpeed * 1.05 + 2.4;
        float orbR = max(uRadius, 0.5) * 0.78;
        vec3 e0 = uOrigin + vec3(cos(ph0) * orbR, sin(ph0 * 1.3) * orbR * 0.55, sin(ph0) * orbR);
        vec3 e1 = uOrigin + vec3(cos(ph1) * orbR * 0.85, cos(ph1 * 0.9) * orbR * 0.5, sin(ph1 * 1.2) * orbR * 0.85);
        float radEff = max(0.45, 1.15 / max(0.35, freq));
        float g0 = 1.0 - smoothstep(radEff * 0.18, radEff, length(pos - e0));
        float g1 = 1.0 - smoothstep(radEff * 0.18, radEff, length(pos - e1));
        w = max(g0, g1);
    }
    else if (uEffectorMode == 3)
    {
        float tShift = time * uEffectorSpeed;
        float warp = ValueNoise(dVec * 0.78 + vec3(0.0, -tShift * 0.35, tShift * 0.25)) * 2.4;
        float waveA = 0.5 + 0.5 * sin(dVec.x * 1.35 + dVec.y * 1.55 - tShift * 1.55 + warp);
        float blot = ValueNoise(dVec * 1.18 + vec3(tShift * 0.42, -tShift * 0.28, 2.3));
        w = clamp(waveA * 0.62 + blot * 0.58 - 0.14, 0.0, 1.0);
    }
    else
    {
        float tS = time * uEffectorSpeed * 0.45;
        float n0 = ValueNoise(dVec * 1.15 + vec3(tS, -tS * 0.7, tS * 0.5));
        float n1 = ValueNoise(dVec * 2.25 - vec3(tS * 1.3, tS * 0.4, -tS * 0.8));
        w = clamp((n0 * 0.68 + n1 * 0.32 - 0.26) * 1.65, 0.0, 1.0);
    }
    return clamp(w * uEffectorStrength, 0.0, 2.0);
}

// Dissolve & Derez wave evaluation: returns release threshold waveU in [0, 1], sweep front progress, edge glow [0, 1] and released state.
void EvaluateDerez(vec3 restHome, float time, out float waveU, out float progress, out float edgeBand, out bool released)
{
    waveU = 0.5;
    progress = 0.0;
    edgeBand = 0.0;
    released = false;
    if (uDerezMode <= 0 || uDerezMix <= 0.0001) return;
    float span = max(uRadius, 0.15);
    vec3 u = clamp((restHome - uOrigin) / (2.0 * span) + 0.5, 0.0, 1.0);
    if (uDerezMode == 5)
    {
        vec3 cell = floor(u * vec3(7.0, 5.0, 7.0));
        float h = fract(sin(dot(cell, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
        waveU = clamp(0.6 * h + 0.4 * u.y, 0.0, 1.0);
    }
    else if (uDerezMode == 3)
    {
        waveU = clamp(0.5 + 0.3 * sin(u.x * 5.0 + u.y * 3.0 + 0.7) + 0.2 * sin(u.y * 7.0 - u.z * 4.0 + 2.1), 0.0, 1.0);
    }
    else
    {
        waveU = (u.x + u.y + u.z) / 3.0;
    }
    float rawProg = uDerezAutoSpeed > 0.0001 ? fract(time * uDerezAutoSpeed) * 1.32 - 0.12 : uDerezProgress * 1.24 - 0.06;
    progress = rawProg * clamp(uDerezMix, 0.0, 1.0);
    float width = max(0.02, uDerezEdgeWidth);
    float dist = progress - waveU;
    edgeBand = exp(-(dist * dist) / max(width * width * 0.55, 1e-4));
    released = (uDerezMode == 2) ? (progress < waveU) : (progress >= waveU);
}
`;

export const ParticleUpdateFragment = `#version 300 es
precision highp float; precision highp int; precision highp sampler2D;
${VolumeChunk}
${NoiseChunk}
${ForceFieldChunk}
${ParticleShapeChunk}
uniform sampler2D uPositions;    // xyz, age
uniform sampler2D uVelocities;   // xyz, seed
uniform sampler2D uVelocity;     // gas velocity volume
uniform int   uOutput;           // 0 positions, 1 velocities
uniform int   uReset;
uniform int   uMode;             // 0 emit, 1 cloner
uniform ivec2 uParticleSize;
uniform float uDeltaTime;
uniform float uEnvelope;         // timing-key envelope
uniform float uRate;
uniform float uLifetime;
uniform float uSpeed;
uniform float uSpread;
uniform float uRadial;
uniform float uGravity;
uniform float uDrag;
uniform float uNoise;
uniform float uFieldInfluence;
uniform float uGasInfluence;
uniform float uSpring;
uniform float uDamping;
uniform float uBounce;
uniform float uNoiseScale;
uniform float uNoiseSpeed;
uniform float uDissolve;
uniform float uLinger;
uniform float uSwarmStrength;
uniform float uSwarmCohesion;
uniform float uSwarmOrbit;
out vec4 fragColour;

vec3 RandomDirection(vec3 seed)
{
    float z = Hash31(seed) * 2.0 - 1.0;
    float a = Hash31(seed.zxy + 5.1) * 6.2831853;
    float r = sqrt(max(0.0, 1.0 - z * z));
    return vec3(r * cos(a), z, r * sin(a));
}

void main()
{
    vec2 pixel = floor(gl_FragCoord.xy);
    int index = int(pixel.x) + int(pixel.y) * uParticleSize.x;
    vec4 p = texelFetch(uPositions, ivec2(pixel), 0);
    vec4 v = texelFetch(uVelocities, ivec2(pixel), 0);
    float seed = Hash31(vec3(pixel, 4.2));
    vec3 limbVel;
    vec3 restHome = ShapePointAtTime(index, vec3(0.0), true, uTime, limbVel);
    vec3 home = restHome;
    if (uDissolve > 0.0) home += CurlNoise(home * uNoiseScale + vec3(uTime * uNoiseSpeed, 2.7, -uTime * uNoiseSpeed * 0.6)) * uDissolve;
    if (uEffectorMode > 0 && uEffectorStrength > 0.0001 && abs(uEffectorPush) > 0.0001)
    {
        float effW = EvaluateEffector(restHome, uTime);
        vec3 outDir = restHome - uOrigin;
        outDir = length(outDir) > 1e-4 ? normalize(outDir) : uAxis;
        home += outDir * (effW * uEffectorPush);
    }

    float waveU, derezProg, derezEdge; bool derezReleased;
    EvaluateDerez(restHome, uTime, waveU, derezProg, derezEdge, derezReleased);

    if (uReset == 1)
    {
        if (uMode == 1)
        {
            vec3 startPos = home;
            if (uDerezMode == 2 && uDerezMix > 0.01 && derezReleased)
            {
                vec3 scatter = RandomDirection(vec3(pixel, 9.4)) * (uRadius * (1.2 + 0.9 * seed));
                startPos = uOrigin + scatter + vec3(0.0, abs(scatter.y) * 0.5, 0.0);
            }
            fragColour = uOutput == 0 ? vec4(startPos, 0.0) : vec4(0.0, 0.0, 0.0, seed);
        }
        else fragColour = uOutput == 0 ? vec4(0.0, -1e4, 0.0, uLifetime + 1.0) : vec4(0.0, 0.0, 0.0, seed);
        return;
    }
    vec3 position = p.xyz;
    vec3 velocity = v.xyz;
    float age = p.w;
    vec3 half_ = GridHalf();
    bool alive = uMode == 1 || age < uLifetime;
    bool lingering = !alive && age < uLifetime + uLinger;
    if (index >= uCount) { fragColour = uOutput == 0 ? vec4(0.0, -1e4, 0.0, uLifetime + 1.0) : vec4(0.0, 0.0, 0.0, seed); return; }

    if (lingering) age += uDeltaTime;
    if (!alive && !lingering)
    {
        float chance = uRate * uEnvelope * uDeltaTime;
        if (Hash31(vec3(pixel, uTime)) < chance)
        {
            vec3 rnd = vec3(Hash31(vec3(pixel, uTime + 1.3)), Hash31(vec3(pixel.yx, uTime + 2.9)), Hash31(vec3(pixel + 7.0, uTime + 4.1)));
            vec3 spawnLimbVel;
            position = ShapePointAtTime(index, rnd, false, uTime, spawnLimbVel);
            vec3 random = RandomDirection(vec3(pixel, uTime + 8.8));
            vec3 outward = position - uOrigin;
            outward = length(outward) > 1e-4 ? normalize(outward) : random;
            vec3 launch = normalize(mix(uAxis, outward, uRadial) + 1e-4);
            velocity = normalize(mix(launch, random, uSpread) + 1e-4) * uSpeed * (0.75 + 0.5 * Hash31(vec3(pixel, uTime + 6.6))) + spawnLimbVel;
            age = 0.0;
            alive = true;
        }
    }
    if (alive)
    {
        // Derez / Dissolve state machine on Cloner or surface formations:
        if (uMode == 1 && uDerezMode > 0 && uDerezMix > 0.001)
        {
            if (!derezReleased)
            {
                // Intact side of the transition front: snap/spring crisply onto the animated home lattice.
                position = mix(position, home, clamp(uDeltaTime * 18.0, 0.0, 1.0));
                velocity *= exp(-8.0 * uDeltaTime);
                age = 0.0;
            }
            else
            {
                // Released side of the transition front!
                if (age <= 0.0001 && uDerezMode != 2)
                {
                    vec3 outDir = restHome - uOrigin;
                    outDir = length(outDir) > 1e-4 ? normalize(outDir) : RandomDirection(vec3(pixel, 3.3));
                    vec3 jitter = RandomDirection(vec3(pixel, 7.1)) * 0.55;
                    float upLift = uDerezMode == 4 ? 1.35 : 0.45;
                    velocity = normalize(outDir + jitter + vec3(0.0, upLift, 0.0)) * uDerezShatter * (0.65 + 0.7 * seed);
                }
                age += uDeltaTime;
                vec3 accel = vec3(0.0, -uGravity, 0.0);
                if (uDerezMode == 4)
                {
                    // Butterfly dissolve: buoyant upward flutter + spiral curl wander!
                    accel.y += 2.6 + 1.2 * sin(uTime * 3.5 + seed * 6.283);
                    accel += CurlNoise(position * 1.1 + vec3(uTime * 0.6, seed * 3.1, -uTime * 0.4)) * 3.4;
                }
                else if (uDerezMode == 2)
                {
                    // Reverse assemble waiting in hovering cloud before the assembly wave sweeps across it.
                    vec3 orbitTarget = uOrigin + CurlNoise(restHome * 0.9 + vec3(uTime * 0.35, seed * 4.0, 0.0)) * (uRadius * 1.6);
                    accel += (orbitTarget - position) * 2.2 - velocity * 1.8;
                }
                else
                {
                    if (uNoise != 0.0) accel += CurlNoise(position * uNoiseScale + vec3(uTime * uNoiseSpeed, v.w * 0.7, -uTime * uNoiseSpeed * 0.4)) * uNoise * 3.0;
                    if (uFieldInfluence != 0.0) accel += EvaluateForceField(position, velocity) * uFieldInfluence;
                }
                velocity += accel * uDeltaTime;
                velocity *= exp(-max(uDrag, 0.35) * uDeltaTime);
                position += velocity * uDeltaTime;
                float floorY = uGridCentre.y - half_.y + 0.01;
                if (position.y < floorY && uBounce >= 0.0)
                {
                    position.y = floorY;
                    // Derez floor shatter burst: scatter outward on bounce!
                    vec3 burst = RandomDirection(vec3(pixel, floor(uTime * 4.0) + 1.7));
                    velocity.y = abs(velocity.y) * max(uBounce, 0.35) + uDerezShatter * 0.22 * fract(seed * 13.7);
                    velocity.xz = velocity.xz * 0.65 + burst.xz * (uDerezShatter * 0.28);
                }
            }
            fragColour = uOutput == 0 ? vec4(position, age) : vec4(velocity, v.w);
            return;
        }

        age += uDeltaTime;
        vec3 acceleration = vec3(0.0, -uGravity, 0.0);
        if (uFieldInfluence != 0.0) acceleration += EvaluateForceField(position, velocity) * uFieldInfluence;
        if (uNoise != 0.0) acceleration += CurlNoise(position * uNoiseScale + vec3(uTime * uNoiseSpeed, v.w * 0.7, -uTime * uNoiseSpeed * 0.4)) * uNoise * 3.0;
        if (uGasInfluence != 0.0)
        {
            vec3 gas = SampleVolume(uVelocity, CellFromWorld(position)).xyz;
            acceleration += (gas - velocity) * uGasInfluence;
        }
        // Flock & Swarm steering (boids murmuration + butterfly flock):
        if (uSwarmStrength > 0.0001)
        {
            float flockGroup = seed > 0.5 ? 1.0 : -1.0;
            float st = uTime * (0.55 + 0.15 * flockGroup);
            float span = max(uRadius, 0.8);
            vec3 leader = uOrigin + vec3(
                sin(st * 1.1 + flockGroup) * span * 1.15,
                sin(st * 1.7 + flockGroup * 0.6) * span * 0.45,
                cos(st * 0.9 - flockGroup * 0.4) * span * 1.15
            );
            vec3 toLeader = leader - position;
            float distLeader = length(toLeader);
            vec3 cohesionDir = distLeader > 1e-4 ? toLeader / distLeader : vec3(0.0, 1.0, 0.0);
            vec3 tangent = normalize(cross(vec3(0.0, 1.0, 0.0), position - uOrigin + vec3(1e-4, 0.0, 1e-4)));
            vec3 flutter = CurlNoise(position * 1.25 + vec3(uTime * 0.8, seed * 5.3, -uTime * 0.6));
            float wingBob = sin(uTime * max(uSwarmWingRate, 1.0) + seed * 6.2831853) * 0.35;
            acceleration += (cohesionDir * uSwarmCohesion * min(distLeader, 3.0) + tangent * uSwarmOrbit + flutter * 1.1 + vec3(0.0, wingBob, 0.0)) * uSwarmStrength;
        }
        if (uMode == 1) acceleration += (home - position) * uSpring - velocity * uDamping + limbVel * 0.65;
        velocity += acceleration * uDeltaTime;
        velocity *= exp(-uDrag * uDeltaTime);
        position += velocity * uDeltaTime;
        float floorY = uGridCentre.y - half_.y + 0.01;
        if (position.y < floorY && uBounce >= 0.0)
        {
            position.y = floorY;
            velocity.y = abs(velocity.y) * uBounce;
            velocity.xz *= 0.7;
        }
        vec3 local = position - uGridCentre;
        if (uMode == 0 && (any(greaterThan(abs(local.xz), half_.xz * 1.05)) || local.y > half_.y * 1.2 || local.y < -half_.y * 1.5)) age = max(age, uLifetime);
    }
    fragColour = uOutput == 0 ? vec4(position, age) : vec4(velocity, v.w);
}
`;

/** Shared per-particle shading (colour rules, palette, effector shift, derez leading-edge glow, flicker, life fade). */
export const ParticleShadeChunk = `
uniform int   uMode;          // 0 emit, 1 cloner
uniform int   uColourMode;    // 0 life, 1 speed, 2 displacement, 3 height, 4 random hue, 5 swatch, 6 cycle
uniform int   uRampMode;
uniform int   uRampSource;    // 0 palette gradient, 1 analytic ramp
uniform vec3  uPalette[8];
uniform int   uPaletteCount;
uniform float uHueJitter;
uniform float uBrightnessJitter;
uniform float uCycleSpeed;
uniform float uBrightness;
uniform float uLifetime;
uniform float uLinger;
uniform float uMaxSpeed;
uniform float uDisplacementRange;
uniform float uFlicker;
uniform float uTime;
uniform float uDomainHeight;
float gLinger = 1.0;
float gDerezReleased = 0.0;
float gEffectorWeight = 0.0;
vec3 HueToRgb(float h)
{
    vec3 k = mod(vec3(5.0, 3.0, 1.0) + h * 6.0, 6.0);
    return 1.0 - clamp(min(k, 4.0 - k), 0.0, 1.0) * 0.85;
}
vec3 PaletteGradient(float t)
{
    int count = max(uPaletteCount, 1);
    if (count == 1) return uPalette[0];
    float x = clamp(t, 0.0, 1.0) * float(count - 1);
    int i = int(min(floor(x), float(count - 2)));
    return mix(uPalette[i], uPalette[i + 1], x - float(i));
}
vec3 PaletteSwatch(float seed)
{
    return uPalette[int(min(floor(seed * float(uPaletteCount)), float(uPaletteCount - 1)))];
}
vec3 Gradient(float t) { return uRampSource == 1 ? Ramp(uRampMode, t) : PaletteGradient(t); }
vec3 HueRotate(vec3 colour, float angle)
{
    const vec3 k = vec3(0.57735);
    float c = cos(angle), s = sin(angle);
    return colour * c + cross(k, colour) * s + k * dot(k, colour) * (1.0 - c);
}
// Returns rgb; writes life (0..1), alpha (alive × life fade × flicker) and flicker.
vec3 ParticleShade(int particle, vec4 current, vec4 velocity, out float life, out float alpha, out float flicker)
{
    vec3 limbVel;
    vec3 restHome = ShapePointAtTime(particle, vec3(0.0), true, uTime, limbVel);
    gEffectorWeight = EvaluateEffector(restHome, uTime);
    float waveU, derezProg, derezEdge; bool derezReleased;
    EvaluateDerez(restHome, uTime, waveU, derezProg, derezEdge, derezReleased);
    gDerezReleased = derezReleased ? 1.0 : 0.0;

    float alive = (uMode == 1 || current.w < uLifetime) && particle < uCount ? 1.0 : 0.0;
    life = uMode == 1 ? 0.5 : clamp(current.w / max(uLifetime, 1e-3), 0.0, 1.0);
    float speed = length(velocity.xyz);
    float t;
    vec3 colour;
    if (uColourMode == 1) { t = clamp(speed / max(uMaxSpeed, 1e-3), 0.0, 1.0); colour = Gradient(t); }
    else if (uColourMode == 2) { t = clamp(distance(current.xyz, restHome) / max(uDisplacementRange, 1e-3), 0.0, 1.0); colour = Gradient(t); }
    else if (uColourMode == 3) { t = clamp(current.y / max(uDomainHeight, 1e-3) + 0.5, 0.0, 1.0); colour = Gradient(t); }
    else if (uColourMode == 4) { colour = HueToRgb(velocity.w); t = life; }
    else if (uColourMode == 5) { colour = PaletteSwatch(fract(velocity.w * 7.31 + 0.137)); t = life; }
    else if (uColourMode == 6) { t = fract(uTime * uCycleSpeed + velocity.w * 0.35 + float(particle) / float(max(uCount, 1)) * 0.5); colour = PaletteGradient(abs(t * 2.0 - 1.0)); }
    else { t = life; colour = PaletteGradient(life); }

    if (uEffectorColourShift > 0.001 && gEffectorWeight > 0.001)
    {
        float shifted = clamp(gEffectorWeight * uEffectorColourShift, 0.0, 1.0);
        colour = mix(colour, PaletteGradient(shifted), clamp(gEffectorWeight * uEffectorColourShift, 0.0, 1.0));
    }
    if (uHueJitter > 0.0) colour = HueRotate(colour, (fract(velocity.w * 13.7) - 0.5) * uHueJitter * 6.2831853);
    if (uBrightnessJitter > 0.0) colour *= 1.0 + (fract(velocity.w * 29.3) - 0.5) * 2.0 * uBrightnessJitter;
    colour = max(colour, 0.0) * uBrightness;

    // Dissolve & Derez incandescent transition front + post-release fade:
    float derezFade = 1.0;
    if (uMode == 1 && uDerezMode > 0 && uDerezMix > 0.001)
    {
        colour += uDerezEdgeColour * (derezEdge * uDerezEdgeGlow * uDerezMix);
        if (derezReleased && uDerezMode != 2)
        {
            float releaseLife = clamp(current.w / max(uLifetime, 0.25), 0.0, 1.0);
            if (uDerezMode == 3) derezFade = 1.0 - smoothstep(0.25, 0.85, releaseLife);
            else derezFade = 1.0 - smoothstep(0.55, 1.0, releaseLife);
        }
        else if (derezReleased && uDerezMode == 2)
        {
            derezFade = 0.55 + 0.45 * derezEdge;
        }
    }

    flicker = uFlicker > 0.0 ? mix(1.0, 0.35 + 0.65 * (0.5 + 0.5 * sin(uTime * (3.0 + 5.0 * velocity.w) + velocity.w * 50.0)), uFlicker) : 1.0;
    if (uGlitchAmount > 0.001 && uGlitchScanlines > 0.001)
    {
        float gTick = floor(uTime * max(uGlitchRate, 1.0));
        float gHash = fract(sin(dot(vec2(float(particle) * 0.17, gTick), vec2(12.9898, 78.233))) * 43758.5453);
        flicker *= mix(1.0, step(0.18, gHash) * (0.65 + 0.7 * gHash), clamp(uGlitchAmount * uGlitchScanlines, 0.0, 1.0));
    }
    float fade = uMode == 1 ? derezFade : smoothstep(0.0, 0.05, life) * (1.0 - smoothstep(0.75, 1.0, life));
    alpha = alive * fade * flicker;
    // Trails outlive the particle: expose the linger fade (1 while alive → 0 at lifetime + linger).
    gLinger = uMode == 1 ? derezFade : (particle < uCount ? clamp(1.0 - (current.w - uLifetime) / max(uLinger, 1e-3), 0.0, 1.0) : 0.0);
    if (uColourMode != 4) colour *= 0.85 + 0.3 * flicker;
    return colour;
}
`;

export const ParticleRenderVertex = `#version 300 es
precision highp float; precision highp int; precision highp sampler2D;
${NoiseChunk}
${RampChunk}
${ParticleShapeChunk}
${ParticleShadeChunk}
uniform sampler2D uPositions;
uniform sampler2D uVelocities;
uniform sampler2D uPrevious;
uniform mat4  uViewProjection;
uniform ivec2 uParticleSize;
uniform int   uTrail;
uniform int   uSprite;
uniform float uSizeStart;
uniform float uSizeEnd;
uniform float uPixelRatio;
uniform float uSizeRandom;
uniform int   uSizeMode;      // 0 world metres, 1 pixels
uniform float uFocalPixels;   // viewport height / (2 tan(fov/2))
uniform vec3  uCameraPosition;
uniform vec3  uCameraForward;
uniform float uFocusDistance; // 0 = DOF off
uniform float uAperture;
uniform float uHaloScale;     // 1 = main pass, >1 = glow halo pass
uniform float uHaloAlpha;
out vec4 vColour;
out float vSoft;
out float vAcross;
out vec4 vExtra;
void main()
{
    vAcross = 0.0;
    int particle = uTrail == 1 ? gl_VertexID / 2 : gl_VertexID;
    int end = uTrail == 1 ? gl_VertexID - particle * 2 : 0;
    ivec2 pixel = ivec2(particle - (particle / uParticleSize.x) * uParticleSize.x, particle / uParticleSize.x);
    vec4 current = texelFetch(uPositions, pixel, 0);
    vec4 previous = texelFetch(uPrevious, pixel, 0);
    vec4 velocity = texelFetch(uVelocities, pixel, 0);
    bool respawned = current.w < previous.w || distance(current.xyz, previous.xyz) > 1.0;
    vec3 position = (end == 1 && !respawned) ? previous.xyz : current.xyz;
    float life, alpha, flicker;
    vec3 colour = ParticleShade(particle, current, velocity, life, alpha, flicker);

    // 3D staccato Glitch jumps (12–30 Hz quantized steps, plus 7x5x7 cell derez jitter):
    if ((uGlitchAmount > 0.001 && uGlitchStepJitter > 0.001) || (uDerezMode == 5 && gDerezReleased > 0.5))
    {
        float rate = max(uGlitchRate, 1.0);
        float stepTick = floor(uTime * rate);
        vec3 jSeed = vec3(float(particle) * 0.13 + stepTick, velocity.w * 31.7 + stepTick, stepTick * 0.73);
        float gate = step(0.42 - uGlitchAmount * 0.35, Hash31(jSeed + 4.1));
        float amp = uGlitchAmount * uGlitchStepJitter * gate + (uDerezMode == 5 && gDerezReleased > 0.5 ? 0.09 * uDerezMix : 0.0);
        position += (vec3(Hash31(jSeed), Hash31(jSeed + 7.3), Hash31(jSeed + 13.9)) - 0.5) * 2.0 * amp;
    }

    float speed = length(velocity.xyz);
    if (end == 1) alpha *= 0.3 * smoothstep(0.2, 2.5, speed);
    float sizeSeed = fract(velocity.w * 41.7);
    float sizeRandom = exp2((sizeSeed - 0.5) * 3.0 * uSizeRandom);
    float size = mix(uSizeStart, uSizeEnd, life) * sizeRandom;
    if (uEffectorMode > 0 && uEffectorStrength > 0.0001 && abs(uEffectorScaleBoost) > 0.0001)
    {
        size *= max(0.08, 1.0 + gEffectorWeight * uEffectorScaleBoost);
    }

    // Morph released pieces into flapping butterflies when Dissolve & Derez is in ButterflyMorph (4) mode!
    int effectiveSprite = uSprite;
    if (uHaloScale <= 1.05 && uDerezMode == 4 && uDerezMix > 0.01 && gDerezReleased > 0.5)
    {
        effectiveSprite = 6;
        size *= 1.85;
    }

    float wingRate = max(uSwarmWingRate, 2.0) * (0.85 + 0.35 * fract(velocity.w * 11.3));
    float flap = cos(uTime * wingRate + velocity.w * 6.2831853);
    float wingW = 0.22 + 0.78 * abs(flap);
    float isScanBar = fract(velocity.w * 19.7) > 0.74 ? 1.0 : 0.0;

    float depth = dot(position - uCameraPosition, uCameraForward);
    float pixels = uSizeMode == 0 ? size * uFocalPixels / max(depth, 0.05) : size * uPixelRatio;
    float coc = uFocusDistance > 0.0 ? clamp(abs(depth - uFocusDistance) / max(uFocusDistance, 0.1) * uAperture * 3.0, 0.0, 1.0) : 0.0;
    float blurGrow = 1.0 + coc * 6.0;
    pixels *= blurGrow * uHaloScale;
    alpha *= uHaloAlpha / (blurGrow * blurGrow * 0.6 + 0.4);
    if (depth <= 0.05) alpha = 0.0;
    vColour = vec4(colour, alpha);
    vSoft = uTrail == 1 ? 0.0 : 1.0;
    vExtra = vec4(float(effectiveSprite), wingW, velocity.w, isScanBar);
    gl_Position = uViewProjection * vec4(position, 1.0);
    gl_PointSize = clamp(pixels, 1.0, 256.0);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                  TRAILS (X-Particles-style spline ribbons)
//------------------------------------------------------------------------------------------------------------------------

/** History ring buffer: texture (side × side·L). Texel (x, y) = particle (x, y / L), slot y % L. */
export const TrailUpdateFragment = `#version 300 es
precision highp float; precision highp int; precision highp sampler2D;
uniform sampler2D uTrails;
uniform sampler2D uPositions;
uniform sampler2D uPrevious;
uniform int   uTrailLength;
uniform int   uHead;
uniform int   uAdvance;
uniform int   uReset;
uniform float uLifetime;
out vec4 fragColour;
void main()
{
    ivec2 texel = ivec2(floor(gl_FragCoord.xy));
    ivec2 pixel = ivec2(texel.x, texel.y / uTrailLength);
    int slot = texel.y - pixel.y * uTrailLength;
    vec4 current = texelFetch(uPositions, pixel, 0);
    vec4 previous = texelFetch(uPrevious, pixel, 0);
    vec4 old = texelFetch(uTrails, texel, 0);
    bool respawned = uReset == 1 || current.w < previous.w || old.w < 0.5 || distance(current.xyz, previous.xyz) > 2.0;
    if (respawned) { fragColour = vec4(current.xyz, 1.0); return; }
    bool frozen = current.w >= uLifetime && uLifetime > 0.0;
    fragColour = (uAdvance == 1 && slot == uHead && !frozen) ? vec4(current.xyz, 1.0) : old;
}
`;

/** Screen-space extruded Catmull-Rom ribbons. 6 vertices per sub-segment; spans = TrailLength, each split into uSubdivisions. */
export const TrailVertex = `#version 300 es
precision highp float; precision highp int; precision highp sampler2D;
${NoiseChunk}
${RampChunk}
${ParticleShapeChunk}
${ParticleShadeChunk}
uniform sampler2D uPositions;
uniform sampler2D uVelocities;
uniform sampler2D uTrails;
uniform mat4  uViewProjection;
uniform ivec2 uParticleSize;
uniform int   uTrailLength;
uniform int   uHead;
uniform int   uSubdivisions;
uniform int   uStride;         // render every Nth particle
uniform vec2  uViewport;       // pixels
uniform float uWidth;          // pixels (or metres in world mode)
uniform int   uSizeMode;
uniform float uFocalPixels;
uniform vec3  uCameraPosition;
uniform vec3  uCameraForward;
uniform float uTaper;
uniform float uTrailFade;
uniform int   uTrailColour;    // 0 particle colour, 1 palette along trail
uniform float uHaloScale;
uniform float uHaloAlpha;
out vec4 vColour;
out float vSoft;
out float vAcross;
out vec4 vExtra;

vec3 TrailPoint(ivec2 pixel, int k)
{
    if (k <= 0) return texelFetch(uPositions, pixel, 0).xyz;
    int slot = ((uHead - (k - 1)) % uTrailLength + uTrailLength) % uTrailLength;
    return texelFetch(uTrails, ivec2(pixel.x, pixel.y * uTrailLength + slot), 0).xyz;
}
vec3 CatmullRom(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float u)
{
    float u2 = u * u, u3 = u2 * u;
    return 0.5 * ((2.0 * p1) + (-p0 + p2) * u + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * u2 + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * u3);
}
void main()
{
    int segments = uTrailLength * uSubdivisions;
    int perParticle = segments * 6;
    int slotIndex = gl_VertexID / perParticle;
    int particle = slotIndex * uStride;
    int rest = gl_VertexID - slotIndex * perParticle;
    int segment = rest / 6;
    int corner = rest - segment * 6;
    ivec2 pixel = ivec2(particle - (particle / uParticleSize.x) * uParticleSize.x, particle / uParticleSize.x);
    vec4 current = texelFetch(uPositions, pixel, 0);
    vec4 velocity = texelFetch(uVelocities, pixel, 0);
    float life, alpha, flicker;
    vec3 colour = ParticleShade(particle, current, velocity, life, alpha, flicker);
    alpha = gLinger * flicker * smoothstep(0.0, 0.03, life);

    int span = segment / uSubdivisions;
    int sub = segment - span * uSubdivisions;
    float uA = float(sub) / float(uSubdivisions);
    float uB = float(sub + 1) / float(uSubdivisions);
    int kPrev = max(span - 1, 0), kNext = min(span + 2, uTrailLength);
    vec3 p0 = TrailPoint(pixel, kPrev), p1 = TrailPoint(pixel, span), p2 = TrailPoint(pixel, span + 1), p3 = TrailPoint(pixel, kNext);
    vec3 a = CatmullRom(p0, p1, p2, p3, uA);
    vec3 b = CatmullRom(p0, p1, p2, p3, uB);
    bool endVertex = corner == 2 || corner == 4 || corner == 5;
    float side = (corner == 0 || corner == 3 || corner == 5) ? -1.0 : 1.0;
    float along = (float(span) + (endVertex ? uB : uA)) / float(uTrailLength);
    vec3 world = endVertex ? b : a;

    vec4 clipA = uViewProjection * vec4(a, 1.0);
    vec4 clipB = uViewProjection * vec4(b, 1.0);
    vec4 clip = endVertex ? clipB : clipA;
    vec2 ndcA = clipA.xy / max(clipA.w, 1e-4), ndcB = clipB.xy / max(clipB.w, 1e-4);
    vec2 screenDirection = (ndcB - ndcA) * uViewport;
    float directionLength = length(screenDirection);
    vec2 normal = directionLength > 1e-4 ? vec2(-screenDirection.y, screenDirection.x) / directionLength : vec2(0.0, 1.0);
    float depth = dot(world - uCameraPosition, uCameraForward);
    float widthPixels = uSizeMode == 0 ? uWidth * uFocalPixels / max(depth, 0.05) : uWidth;
    widthPixels = max(widthPixels * mix(1.0, 1.0 - along, uTaper) * uHaloScale, 0.75);
    clip.xy += normal * side * widthPixels * 0.5 / uViewport * 2.0 * clip.w;

    if (uTrailColour == 1) colour = PaletteGradient(along) * uBrightness * (0.85 + 0.3 * flicker);
    alpha *= pow(1.0 - along, max(uTrailFade, 0.05)) * uHaloAlpha;
    if (depth <= 0.05 || distance(p1, p2) > 2.0) alpha = 0.0;
    vColour = vec4(colour, alpha);
    vSoft = -1.0;
    vAcross = side;
    vExtra = vec4(4.0, 1.0, 0.0, 0.0);
    gl_Position = clip;
}
`;

export const ParticleFragment = `#version 300 es
precision highp float; precision highp int;
in vec4 vColour;
in float vSoft;
in float vAcross;
in vec4 vExtra;
uniform int uSprite;   // 0 glow, 1 disc, 2 bokeh, 3 star, 4 square, 5 cube3d, 6 butterfly, 7 hextile, 8 hologlyph,
                       // 9 snowcrystal, 10 raindrop, 11 leafblade, 12 blossompetal, 13 crystalshard2d, 14 shockring,
                       // 15 radararc, 16 auroracurtain, 17 coindisc
out vec4 fragColour;
void main()
{
    if (vColour.a <= 0.0) discard;
    vec3 rgb = vColour.rgb;
    float alpha = vColour.a;
    bool butterflyMorph = int(floor(vExtra.x + 0.5)) == 6 && uSprite == 5;
    if (vSoft > 0.5)
    {
        vec2 q = gl_PointCoord - 0.5;
        float d = length(q) * 2.0;
        if (uSprite == 0) { if (d > 1.0) discard; alpha *= exp(-d * d * 4.5) * (1.0 - smoothstep(0.8, 1.0, d)); }
        else if (uSprite == 1) { if (d > 1.0) discard; alpha *= 1.0 - smoothstep(0.6, 1.0, d); }
        else if (uSprite == 2) { if (d > 1.0) discard; alpha *= 0.22 + 0.78 * smoothstep(0.5, 0.78, d) * (1.0 - smoothstep(0.86, 1.0, d)); }
        else if (uSprite == 3) { float arms = max(exp(-abs(q.x) * 14.0), exp(-abs(q.y) * 14.0)); alpha *= max(arms * (1.0 - d * 0.9), exp(-d * d * 10.0)); if (alpha < 0.01) discard; }
        else if (uSprite == 6 || butterflyMorph)
        {
            // 6: Flapping dual-wing butterfly silhouette (ported from Particle Editor fsFly).
            vec2 uv = vec2(q.x, -q.y) * 2.0;
            float wingW = max(vExtra.y, 0.16);
            float x = abs(uv.x);
            float xs = x / wingW;
            float dF = length(vec2((xs - 0.50) / 0.50, (uv.y - 0.28) / 0.36));
            float dH = length(vec2((xs - 0.40) / 0.36, (uv.y + 0.28) / 0.26));
            float dMin = min(dF, dH);
            float wing = 1.0 - smoothstep(0.84, 1.0, dMin);
            float body = (1.0 - smoothstep(0.035, 0.065, x)) * step(abs(uv.y), 0.62);
            if (wing < 0.01 && body < 0.01) discard;
            float edge = 0.76 + 0.48 * (1.0 - min(dMin, 1.0));
            rgb = mix(rgb * edge, vec3(0.09), body * 0.85);
            alpha *= max(wing, body);
        }
        else if (uSprite == 5)
        {
            // 5: Isometric 3D shaded voxel cube with directional face normals & crisp bevel highlight (Derez / MoGraph cubes).
            vec2 p = vec2(q.x, -q.y) * 2.15;
            float hex = max(abs(p.x) * 0.8660254 + abs(p.y) * 0.5, abs(p.y));
            if (hex > 0.96) discard;
            float mask = 1.0 - smoothstep(0.88, 0.96, hex);
            bool topFace = p.y > abs(p.x) * 0.57735;
            float shade = topFace ? 1.14 : (p.x < 0.0 ? 0.80 : 0.54);
            float seam = min(abs(p.y - abs(p.x) * 0.57735), p.y < abs(p.x) * 0.57735 ? abs(p.x) : 1.0);
            float rim = smoothstep(0.80, 0.90, hex) * 0.28 + (1.0 - smoothstep(0.0, 0.06, seam)) * 0.22;
            rgb = rgb * shade + rgb * rim;
            alpha *= mask;
        }
        else if (uSprite == 7)
        {
            // 7: Hexagonal Energy Shield barrier tile with glowing rim & translucent plasma core.
            vec2 p = abs(q * 2.1);
            float hex = max(p.x * 0.8660254 + p.y * 0.5, p.y);
            if (hex > 0.98) discard;
            float rim = smoothstep(0.70, 0.88, hex) * (1.0 - smoothstep(0.90, 0.98, hex));
            float core = (1.0 - smoothstep(0.0, 0.88, hex)) * 0.34;
            rgb = rgb * (0.65 + 1.15 * rim);
            alpha *= clamp(core + rim * 1.15, 0.0, 1.0);
        }
        else if (uSprite == 8)
        {
            // 8: Hologram Matrix Glyph + horizontal scanline glitch bar (ported from Particle Editor shape 12).
            vec2 uv = q * 2.0;
            if (vExtra.w > 0.5)
            {
                if (abs(uv.y) > 0.24 || abs(uv.x) > 0.96) discard;
                float scan = 0.7 + 0.3 * sin(uv.x * 18.0 + vExtra.z * 40.0);
                alpha *= (1.0 - smoothstep(0.14, 0.24, abs(uv.y))) * scan;
            }
            else
            {
                vec2 grid = floor((q + 0.5) * 5.0);
                float symX = min(grid.x, 4.0 - grid.x);
                float cellHash = fract(sin(dot(vec2(symX, grid.y) + floor(vExtra.z * 31.0), vec2(12.9898, 78.233))) * 43758.5453);
                vec2 local = fract((q + 0.5) * 5.0) - 0.5;
                float box = step(max(abs(local.x), abs(local.y)), 0.38);
                float on = step(0.36, cellHash) * box;
                if (on < 0.01) discard;
                alpha *= on * (0.82 + 0.18 * sin(q.y * 36.0));
            }
        }
        else if (uSprite == 9)
        {
            // 9: Six-fold dendritic snow crystal — hexagonal hub, six arms with side branches and a soft ice core.
            vec2 uv = q * 2.0;
            float r = length(uv);
            if (r > 0.98) discard;
            float a = atan(uv.y, uv.x) + 3.14159265;
            float arm = pow(abs(cos(a * 3.0)), (r > 0.16 ? 22.0 : 2.0));
            float branch = pow(abs(cos(a * 3.0 + 1.05)), 26.0) * smoothstep(0.26, 0.52, r) * (1.0 - smoothstep(0.62, 0.94, r));
            float hub = 1.0 - smoothstep(0.06, 0.20, r);
            float arms = max(arm * (1.0 - smoothstep(0.72, 0.98, r)), branch * 0.82);
            float mask = max(max(arms, hub), 0.0);
            if (mask < 0.012) discard;
            rgb = mix(rgb, mix(rgb, vec3(1.0), 0.35), hub);
            alpha *= clamp(mask, 0.0, 1.0) * (0.72 + 0.55 * mask);
        }
        else if (uSprite == 10)
        {
            // 10: Rain drop — teardrop with a bright specular bead at the heavy end.
            vec2 uv = vec2(q.x * 2.0, -q.y * 2.0);
            float body = length(vec2(uv.x / 0.46, (uv.y - 0.10) / 0.54));
            float tailX = uv.x * (1.0 - smoothstep(-0.1, 0.95, uv.y)) * 1.4;
            float tail = length(vec2(tailX / 0.30, (uv.y - 0.10) / 0.92));
            float mask = 1.0 - smoothstep(0.80, 1.0, min(body, tail));
            if (mask < 0.01) discard;
            float bead = 1.0 - smoothstep(0.0, 0.30, length(uv - vec2(0.14, 0.30)));
            rgb = mix(rgb * 0.82, vec3(1.0), bead * 0.55);
            alpha *= mask;
        }
        else if (uSprite == 11)
        {
            // 11: Leaf blade — pointed almond with a midrib shadow and a serrated edge.
            vec2 uv = vec2(q.x, -q.y) * 2.0;
            float width = (1.0 - uv.y * uv.y) * 0.62;
            float serration = 0.055 * sin(uv.y * 26.0);
            float across = abs(uv.x) - width - serration * step(0.0, width);
            float mask = 1.0 - smoothstep(-0.03, 0.05, across);
            mask *= step(abs(uv.y), 1.0);
            if (mask < 0.01) discard;
            float rib = 1.0 - smoothstep(0.0, 0.05 + abs(uv.y) * 0.03, abs(uv.x));
            rgb = mix(rgb * (0.72 + 0.42 * (1.0 - abs(uv.x) / max(width, 1e-3))), rgb * 1.5 + 0.10, rib * 0.7);
            alpha *= mask;
        }
        else if (uSprite == 12)
        {
            // 12: Blossom petal — rounded lobe with a small notch at the tip and a soft centre vein.
            vec2 uv = vec2(q.x, -q.y) * 2.0;
            float lobe = length(vec2(uv.x / 0.66, (uv.y - 0.02) / 0.90));
            float notch = length(vec2(uv.x / 0.16, (uv.y - 0.96) / 0.22));
            float mask = (1.0 - smoothstep(0.86, 1.0, lobe)) * smoothstep(0.30, 0.62, notch + 0.34);
            if (mask < 0.01) discard;
            float vein = 1.0 - smoothstep(0.0, 0.09, abs(uv.x + sin(uv.y * 3.0) * 0.05));
            rgb = mix(rgb * 0.90, rgb * 1.35 + 0.06, vein * 0.6);
            alpha *= mask;
        }
        else if (uSprite == 13)
        {
            // 13: Crystal shard (2D) — hexagonal prism silhouette with three lit facets and a sharp highlight.
            vec2 p = vec2(q.x * 2.0, -q.y * 2.0);
            float hex = max(abs(p.x) * 0.8660254 + abs(p.y) * 0.5, abs(p.y));
            float taper = 1.0 - smoothstep(0.55, 1.0, abs(p.y)) * 0.22;
            if (hex > 0.96 * taper) discard;
            float mask = 1.0 - smoothstep(0.88, 0.96 * taper, hex);
            float facetA = smoothstep(0.0, 0.55, p.x + 0.30);
            float facetB = smoothstep(0.0, 0.55, -p.x + 0.30);
            float facetC = smoothstep(0.10, 0.35, p.y);
            float shade = 0.62 + 0.62 * facetA + 0.28 * facetB + 0.34 * facetC;
            float edge = smoothstep(0.78, 0.92, hex) * 0.55;
            rgb = rgb * shade + vec3(edge) * rgb;
            alpha *= mask;
        }
        else if (uSprite == 14)
        {
            // 14: Shock ring — thin bright annulus with an expanding inner pressure front.
            vec2 uv = q * 2.0;
            float r = length(uv);
            if (r > 1.0) discard;
            float ring = exp(-pow((r - 0.78) / 0.10, 2.0));
            float inner = exp(-pow((r - 0.46) / 0.20, 2.0)) * 0.28;
            float mask = ring + inner;
            if (mask < 0.012) discard;
            rgb = mix(rgb * 0.65, vec3(1.0), ring * 0.55);
            alpha *= clamp(mask, 0.0, 1.0);
        }
        else if (uSprite == 15)
        {
            // 15: Radar arc — swept sector with a hot leading edge and fading range rings.
            vec2 uv = q * 2.0;
            float r = length(uv);
            if (r > 1.0) discard;
            float a = atan(uv.y, uv.x);
            float sweep = vExtra.z * 6.2831853;
            float wrapped = mod(a - sweep + 12.5663706, 6.2831853);
            float wake = smoothstep(0.0, 1.9, wrapped) * step(wrapped, 2.6);
            float leading = exp(-pow(wrapped / 0.14, 2.0));
            float rings = 0.20 * smoothstep(0.35, 0.5, abs(fract(r * 3.0) - 0.5));
            float mask = max(leading, wake * 0.42) + rings * step(0.2, r);
            if (mask < 0.01) discard;
            rgb = mix(rgb, vec3(0.55, 1.0, 0.85), leading * 0.55);
            alpha *= clamp(mask, 0.0, 1.0) * (1.0 - smoothstep(0.92, 1.0, r));
        }
        else if (uSprite == 16)
        {
            // 16: Aurora curtain — vertical ribbons with drifting curtain folds and a soft base falloff.
            vec2 uv = vec2(q.x * 2.0, -q.y * 2.0);
            float ribbonA = sin(uv.x * 5.0 + vExtra.z * 4.0) * 0.30;
            float ribbonB = sin(uv.x * 9.0 - vExtra.z * 6.5) * 0.16;
            float band = 1.0 - smoothstep(0.10, 0.62, abs(uv.y - ribbonA - ribbonB));
            float verticalFade = smoothstep(-1.0, 0.55, uv.y) * (1.0 - smoothstep(0.72, 1.0, uv.y));
            float mask = band * verticalFade;
            if (mask < 0.012) discard;
            rgb = mix(rgb, mix(vec3(0.22, 0.95, 0.72), vec3(0.55, 0.42, 1.0), smoothstep(-0.4, 0.8, uv.y)), 0.45);
            alpha *= mask * 0.92;
        }
        else if (uSprite == 17)
        {
            // 17: Coin disc — struck metal disc with a raised rim, milled edge and a centre boss.
            vec2 uv = q * 2.0;
            float r = length(uv);
            if (r > 1.0) discard;
            float rim = smoothstep(0.72, 0.82, r) * (1.0 - smoothstep(0.88, 0.96, r));
            float milled = 0.5 + 0.5 * sin(atan(uv.y, uv.x) * 44.0);
            float boss = 1.0 - smoothstep(0.16, 0.34, r);
            float face = 1.0 - smoothstep(0.94, 1.0, r);
            float relief = 0.86 + 0.24 * smoothstep(0.18, 0.66, r) + 0.26 * boss;
            rgb = rgb * relief + vec3(rim * milled * 0.42) + vec3(boss * 0.22);
            alpha *= face;
        }
        // 4: square — full footprint.
    }
    else if (vSoft < -0.5) alpha *= 1.0 - vAcross * vAcross;
    fragColour = vec4(rgb, alpha);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                   3D OBJECT CARRIER + BACKING SURFACE (gl_VertexID instancing)
//------------------------------------------------------------------------------------------------------------------------
// Every particle draws the whole mesh: gl_VertexID / verticesPerParticle = particle, the remainder = mesh corner. Mesh corners are
// texelFetch'd from an RGBA32F texture (two texels per corner: position.xyz|normal.x, normal.y|normal.z), so no vertex buffers or
// divisors are required — the same emptyVao + gl.drawArrays(TRIANGLES, …) path the trail ribbons use.

export const ObjectRenderVertex = `#version 300 es
precision highp float; precision highp int; precision highp sampler2D;
${NoiseChunk}
${RampChunk}
${ParticleShapeChunk}
${ParticleShadeChunk}
uniform sampler2D uPositions;
uniform sampler2D uVelocities;
uniform sampler2D uMesh;
uniform mat4  uViewProjection;
uniform ivec2 uParticleSize;
uniform int   uMeshWidth;
uniform int   uMeshVertexCount;
uniform int   uMeshTriangleCount;
uniform int   uWire;             // 1 = draw triangle edges (wireframe), 0 = filled triangles
uniform int   uBacking;          // 1 = backing surface pass (single centred instance, no spin seed)
uniform float uBackingScale;     // backing radius in metres
uniform float uSizeStart;
uniform float uSizeEnd;
uniform float uSizeRandom;
uniform float uPixelRatio;
uniform int   uSizeMode;         // 0 world metres, 1 pixels
uniform float uFocalPixels;
uniform vec3  uCameraPosition;
uniform vec3  uCameraForward;
uniform float uObjectSpin;       // radians / second
uniform float uObjectTilt;
out vec3  vNormal;
out vec3  vColour;
out vec3  vWorld;
out vec3  vLocal;
out float vAlpha;
out float vRidge;
vec4 MeshTexel(int texelSlot)
{
    int x = texelSlot - (texelSlot / uMeshWidth) * uMeshWidth;
    int y = texelSlot / uMeshWidth;
    return texelFetch(uMesh, ivec2(x, y), 0);
}
void main()
{
    int triangles = max(uMeshTriangleCount, 1);
    int vertices = max(uMeshVertexCount, 3);
    int stride = uWire == 1 ? triangles * 6 : vertices;
    int particle = gl_VertexID / stride;
    int slot = gl_VertexID - particle * stride;
    int vertex;
    if (uWire == 1)
    {
        int triangle = slot / 6;
        int edge = slot - triangle * 6;
        int corner = (edge + 1) / 2;
        if (corner > 2) corner = 0;
        vertex = triangle * 3 + corner;
    }
    else vertex = slot;

    vec4 first = MeshTexel(vertex * 2);
    vec4 second = MeshTexel(vertex * 2 + 1);
    vec3 local = first.xyz;
    vec3 normal = vec3(first.w, second.xy);

    int safeParticle = clamp(particle, 0, uParticleSize.x * uParticleSize.y - 1);
    ivec2 pixel = ivec2(safeParticle - (safeParticle / uParticleSize.x) * uParticleSize.x, safeParticle / uParticleSize.x);
    vec4 current = texelFetch(uPositions, pixel, 0);
    vec4 velocity = texelFetch(uVelocities, pixel, 0);
    float life, alpha, flicker;
    vec3 colour = ParticleShade(uBacking == 1 ? 0 : safeParticle, current, velocity, life, alpha, flicker);
    vec3 base = uBacking == 1 ? uOrigin : current.xyz;

    float sizeSeed = fract(velocity.w * 41.7);
    float sizeRandom = exp2((sizeSeed - 0.5) * 3.0 * uSizeRandom);
    float radius = uBacking == 1 ? uBackingScale : mix(uSizeStart, uSizeEnd, life) * sizeRandom;
    float depth = dot(base - uCameraPosition, uCameraForward);
    if (uBacking == 0 && uSizeMode == 1) radius = radius * max(depth, 0.05) / max(uFocalPixels, 1.0);

    float angle = uObjectSpin * uTime + (uBacking == 1 ? 0.0 : velocity.w * 6.2831853);
    float ca = cos(angle), sa = sin(angle);
    float tilt = uObjectTilt * (uBacking == 1 ? 0.0 : (fract(velocity.w * 3.7) - 0.5) * 1.6);
    float ct = cos(tilt), st = sin(tilt);
    vec3 spun = vec3(local.x * ca + local.z * sa, local.y, -local.x * sa + local.z * ca);
    vec3 spunNormal = vec3(normal.x * ca + normal.z * sa, normal.y, -normal.x * sa + normal.z * ca);
    spun = vec3(spun.x, spun.y * ct - spun.z * st, spun.y * st + spun.z * ct);
    spunNormal = vec3(spunNormal.x, spunNormal.y * ct - spunNormal.z * st, spunNormal.y * st + spunNormal.z * ct);

    vec3 world = base + spun * radius;
    vWorld = world;
    vNormal = spunNormal;
    vLocal = local;
    vColour = colour;
    vAlpha = uBacking == 1 ? 1.0 : alpha;
    vRidge = second.w;
    gl_Position = vAlpha <= 0.002 ? vec4(2.0, 2.0, 2.0, 1.0) : uViewProjection * vec4(world, 1.0);
}
`;

export const ObjectFragment = `#version 300 es
precision highp float; precision highp int;
in vec3  vNormal;
in vec3  vColour;
in vec3  vWorld;
in vec3  vLocal;
in float vAlpha;
in float vRidge;
uniform vec3  uCameraPosition;
uniform float uObjectRoughness;
uniform float uObjectMetallic;
uniform int   uBackingMode;      // 0 carrier object, 1 obsidian gloss, 2 brushed chrome, 3 ghost wireframe
uniform vec3  uBackingColour;
uniform int   uWire;
uniform float uObjectGlow;
out vec4 fragColour;
void main()
{
    if (vAlpha <= 0.002) discard;
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCameraPosition - vWorld);
    if (dot(N, V) < 0.0) N = -N;
    vec3 keyLight = normalize(vec3(0.42, 0.82, 0.46));
    vec3 rimLight = normalize(vec3(-0.55, 0.28, -0.62));
    float fresnel = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 4.0);
    vec3 rgb;

    if (uBackingMode == 0)
    {
        // Solid PBR-ish carrier: Blinn-Phong wrapped diffuse, metallic specular boost and a grazing Fresnel rim.
        float roughness = clamp(uObjectRoughness, 0.02, 1.0);
        float metallic = clamp(uObjectMetallic, 0.0, 1.0);
        float specPower = mix(96.0, 12.0, roughness);
        vec3 halfKey = normalize(keyLight + V);
        vec3 halfRim = normalize(rimLight + V);
        float diffuse = max(dot(N, keyLight), 0.0) * 0.72 + max(dot(N, rimLight), 0.0) * 0.24 + 0.10;
        float specular = (pow(max(dot(N, halfKey), 0.0), specPower) + pow(max(dot(N, halfRim), 0.0), specPower * 0.55) * 0.45);
        float metallicBoost = mix(0.35, 1.15, metallic);
        vec3 albedo = mix(vColour, vec3(dot(vColour, vec3(0.30, 0.59, 0.11))), metallic * 0.55);
        rgb = albedo * diffuse * (1.0 - metallic * 0.35) + vec3(specular * metallicBoost) * mix(0.55, 1.0, metallic);
        rgb += vColour * fresnel * (0.24 + uObjectGlow);
        rgb += vColour * vRidge * 0.22;
    }
    else if (uBackingMode == 1)
    {
        // Jet-black obsidian studio plinth with two overhead softbox panels and a deep Fresnel edge.
        vec3 base = vec3(0.022, 0.026, 0.036);
        vec3 boxA = normalize(vec3(-0.35, 0.92, 0.28));
        vec3 boxB = normalize(vec3(0.48, 0.74, -0.44));
        float softA = pow(max(dot(N, normalize(boxA + V)), 0.0), 120.0);
        float softB = pow(max(dot(N, normalize(boxB + V)), 0.0), 84.0);
        float edge = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 4.5);
        rgb = base + vec3(softA * 1.5 + softB * 0.9) * uBackingColour + vec3(edge * 0.42) * uBackingColour;
        rgb += vColour * 0.06;
    }
    else if (uBackingMode == 2)
    {
        // Brushed chrome: horizontal anisotropy from the local mesh height, bright horizon reflection band.
        float brushed = 0.5 + 0.5 * sin(vLocal.y * 210.0);
        float horizon = smoothstep(0.15, 0.9, abs(N.y));
        float spec = pow(max(dot(N, normalize(keyLight + V)), 0.0), 64.0);
        rgb = mix(vec3(0.30, 0.33, 0.38), vec3(0.86, 0.90, 0.96), horizon) * (0.55 + 0.45 * brushed);
        rgb += vec3(spec) * 0.9 + vec3(fresnel * 0.35) * uBackingColour;
    }
    else
    {
        // Ghost wireframe: emissive silhouette only, brightest where the surface turns away.
        rgb = uBackingColour * (0.45 + 1.35 * fresnel) * (uWire == 1 ? 1.15 : 0.85);
        rgb *= 0.85 + 0.3 * vRidge;
    }

    float alpha = vAlpha;
    if (uBackingMode == 3) alpha *= 0.55 + 0.45 * fresnel;
    fragColour = vec4(rgb, alpha);
}
`;
