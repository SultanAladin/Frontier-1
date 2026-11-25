/*============================================================================================================================================
                                                        PARTICLESHAPEANCHOR.SLANG
============================================================================================================================================*/
// 🧩 Pure .slang deterministic shape sampling (16 shapes), 11-capsule Mocap rig (4 clips), MoGraph Effector (4 modes), and Dissolve/Derez (5 modes)
// Reference: Experimental/FluidEditor/Engine/ParticleShaders.js L9-L521 (ParticleShapeChunk)

module ParticleShapeAnchor;

import ForceFieldNoise;

//------------------------------------------------------------------------------------------------------------------------
//                                                    UNIFORM BLOCK LAYOUT
//------------------------------------------------------------------------------------------------------------------------

/// Matches C++ ParticleShapeUniformBlock byte-for-byte (256 bytes, 16-byte aligned).
public struct ParticleShapeUniformBlock
{
    public float4 OriginRadius;         // xyz = origin (m), w = radius (m)
    public float4 AxisFill;             // xyz = unit axis, w = fill [0..1]
    public float4 ShapeCounts;          // x = shape (0..15), y = count, z = shapeDetail, w = mode (0=Stream, 1=Cloner)
    public float4 AnchorLayout;         // x = anchorWidth, y = anchorCount, z = textureSide, w = reserved
    public float4 MocapParams;          // x = clip (0..4), y = amplitude, z = speed, w = scale
    public float4 MocapKickEffector;    // x = trailKick, y = effectorMode (0..4), z = effectorStrength, w = effectorFrequency
    public float4 EffectorMotion;       // x = effectorSpeed, y = effectorPush, z = effectorScaleBoost, w = effectorColourShift
    public float4 EffectorAxisDerez;    // x = effectorAxis (0..2), y = derezMode (0..5), z = derezMix, w = derezProgress
    public float4 DerezFrontParams;     // x = derezAutoSpeed, y = derezEdgeWidth, z = derezEdgeGlow, w = derezShatter
    public float4 DerezEdgeColourWing;  // xyz = derezEdgeColour linear RGB, w = swarmWingRate
    public float4 GlitchParams;         // x = glitchAmount, y = glitchRate, z = glitchStepJitter, w = glitchScanlines
};

public struct DerezEvaluationResult
{
    public float WaveU;
    public float Progress;
    public float EdgeBand;
    public bool  Released;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    BASIS & ANCHORS
//------------------------------------------------------------------------------------------------------------------------

public void ShapeBasis(float3 axis, out float3 t, out float3 b)
{
    float3 n = axis;
    float3 helper = abs(n.y) < 0.95 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    t = normalize(cross(helper, n));
    b = cross(n, t);
}

/// 6-face voxelized box shell (ParticleShaders.js L67-L81).
public float3 BoxShellLocal(int index, float count)
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
    if (ax == 0) return float3(sg, cu, cv);
    if (ax == 1) return float3(cv, sg, cu);
    return float3(cu, cv, sg);
}

/// Anchor fetch: two RGBA32F texels per anchor (pos.xyz|localY, normal.xyz|ridge).
public float3 ObjectAnchorPosition(
    StructuredBuffer<float4> anchorTexels,
    int anchorCount,
    int index,
    out float3 normal)
{
    if (anchorCount < 1)
    {
        normal = float3(0.0, 1.0, 0.0);
        return float3(0.0, 0.0, 0.0);
    }
    int slot = min(index, anchorCount - 1);
    float4 first  = anchorTexels[slot * 2];
    float4 second = anchorTexels[slot * 2 + 1];
    normal = second.xyz;
    return first.xyz;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              11-CAPSULE MOCAP RIG (L102-L246)
//------------------------------------------------------------------------------------------------------------------------

public void MocapBoneEndpoints(int bone, float phase, int clip, out float3 a, out float3 b, out float rad)
{
    float s = sin(phase);
    float c = cos(phase);
    float s2 = sin(phase * 2.0);
    float3 pelvis = float3(0.0, 0.05, 0.0);
    float3 chest  = float3(0.0, 0.52, 0.0);
    float3 neck   = float3(0.0, 0.68, 0.0);
    float3 head   = float3(0.0, 0.92, 0.0);
    float3 shL = float3(-0.22, 0.62, 0.0),  shR = float3(0.22, 0.62, 0.0);
    float3 elL = float3(-0.36, 0.34, 0.04), elR = float3(0.36, 0.34, 0.04);
    float3 wrL = float3(-0.44, 0.08, 0.10), wrR = float3(0.44, 0.08, 0.10);
    float3 hpL = float3(-0.12, 0.02, 0.0),  hpR = float3(0.12, 0.02, 0.0);
    float3 knL = float3(-0.14, -0.44, 0.03), knR = float3(0.14, -0.44, 0.03);
    float3 anL = float3(-0.15, -0.88, 0.0), anR = float3(0.15, -0.88, 0.0);

    if (clip == 1)
    {
        float bob = abs(s) * 0.07;
        pelvis = float3(s * 0.03, 0.04 + bob, 0.0);
        chest  = float3(0.0, 0.50 + bob, 0.12);
        neck   = float3(0.0, 0.66 + bob, 0.16);
        head   = float3(0.0, 0.89 + bob, 0.19);
        shL = chest + float3(-0.21, 0.10, -s * 0.06);
        shR = chest + float3( 0.21, 0.10,  s * 0.06);
        elL = shL + float3(-0.06, -0.24, -s * 0.22 - 0.04);
        wrL = elL + float3( 0.04,  0.06 + max(0.0, -s) * 0.12, -s * 0.18 + 0.22);
        elR = shR + float3( 0.06, -0.24,  s * 0.22 - 0.04);
        wrR = elR + float3(-0.04,  0.06 + max(0.0,  s) * 0.12,  s * 0.18 + 0.22);
        hpL = pelvis + float3(-0.12, -0.02,  s * 0.04);
        hpR = pelvis + float3( 0.12, -0.02, -s * 0.04);
        float liftL = max(0.0, s), liftR = max(0.0, -s);
        knL = hpL + float3(-0.01, -0.38 + liftL * 0.24,  s * 0.30 + 0.06);
        anL = knL + float3( 0.0,  -0.40 + liftL * 0.14,  s * 0.22 - 0.18);
        knR = hpR + float3( 0.01, -0.38 + liftR * 0.24, -s * 0.30 + 0.06);
        anR = knR + float3( 0.0,  -0.40 + liftR * 0.14, -s * 0.22 - 0.18);
    }
    else if (clip == 2)
    {
        float bounce = -abs(s2) * 0.08;
        float sway = s * 0.16;
        pelvis = float3(sway, 0.04 + bounce, 0.0);
        chest  = float3(sway * 0.55, 0.50 + bounce, c * 0.05);
        neck   = chest + float3(-sway * 0.15, 0.16, 0.02);
        head   = neck + float3(s2 * 0.05, 0.23, 0.02);
        shL = chest + float3(-0.22, 0.10 + s * 0.05, 0.0);
        shR = chest + float3( 0.22, 0.10 - s * 0.05, 0.0);
        elL = shL + float3(-0.20, 0.06 + s * 0.22, 0.12);
        wrL = elL + float3(-0.10 + c * 0.12, 0.20 + s * 0.22, 0.14);
        elR = shR + float3( 0.20, 0.06 - s * 0.22, 0.12);
        wrR = elR + float3( 0.10 - c * 0.12, 0.20 - s * 0.22, 0.14);
        hpL = pelvis + float3(-0.12, -0.02, 0.0);
        hpR = pelvis + float3( 0.12, -0.02, 0.0);
        knL = hpL + float3(-0.08 + max(0.0, -s) * 0.12, -0.40, 0.10 + max(0.0, s) * 0.14);
        anL = float3(-0.22 + sway * 0.4, -0.88 + max(0.0, s) * 0.16, s * 0.14);
        knR = hpR + float3( 0.08 - max(0.0,  s) * 0.12, -0.40, 0.10 + max(0.0, -s) * 0.14);
        anR = float3( 0.22 + sway * 0.4, -0.88 + max(0.0, -s) * 0.16, -s * 0.14);
    }
    else if (clip == 3)
    {
        float strike = pow(max(0.0, s), 2.0);
        float kick = pow(max(0.0, -s), 2.0);
        float yaw = c * 0.45;
        float cy = cos(yaw), sy = sin(yaw);
        pelvis = float3(0.0, -0.02, 0.0);
        chest  = float3(sy * 0.08, 0.46, cy * 0.06);
        neck   = chest + float3(0.0, 0.16, 0.02);
        head   = neck + float3(0.0, 0.22, 0.04);
        shL = chest + float3(-0.21 * cy, 0.10, -0.21 * sy);
        shR = chest + float3( 0.21 * cy, 0.10,  0.21 * sy);
        elL = shL + float3(-0.12, -0.16, 0.18 + strike * 0.18);
        wrL = elL + float3( 0.06,  0.04, 0.20 + strike * 0.36);
        elR = shR + float3( 0.14, -0.18, 0.12);
        wrR = elR + float3(-0.08,  0.12, 0.18);
        hpL = pelvis + float3(-0.13, -0.02, -0.05);
        hpR = pelvis + float3( 0.13, -0.02,  0.05);
        knL = hpL + float3(-0.08, -0.42, -0.08);
        anL = float3(-0.24, -0.88, -0.14);
        knR = hpR + float3( 0.08, -0.36 + kick * 0.42, 0.16 + kick * 0.22);
        anR = knR + float3( 0.08 * kick, -0.44 + kick * 0.48, 0.10 + kick * 0.38);
    }
    else if (clip == 4)
    {
        float hover = sin(phase * 0.7) * 0.14;
        pelvis = float3(0.0, 0.12 + hover, 0.0);
        chest  = float3(0.0, 0.58 + hover, -0.06);
        neck   = chest + float3(0.0, 0.16, -0.03);
        head   = neck + float3(0.0, 0.22, -0.04);
        shL = chest + float3(-0.22, 0.09, 0.0);
        shR = chest + float3( 0.22, 0.09, 0.0);
        elL = shL + float3(-0.22,  0.02 + s * 0.08, 0.08);
        wrL = elL + float3(-0.18, -0.06 + c * 0.10, 0.12);
        elR = shR + float3( 0.22,  0.02 - s * 0.08, 0.08);
        wrR = elR + float3( 0.18, -0.06 - c * 0.10, 0.12);
        hpL = pelvis + float3(-0.12, -0.03, 0.0);
        hpR = pelvis + float3( 0.12, -0.03, 0.0);
        knL = hpL + float3(-0.06, -0.38,  0.14 + s * 0.06);
        anL = knL + float3(-0.03, -0.38, -0.12);
        knR = hpR + float3( 0.06, -0.34,  0.22 - s * 0.06);
        anR = knR + float3( 0.03, -0.38, -0.10);
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

public float3 MocapSamplePoint(
    ParticleShapeUniformBlock shape,
    int index,
    float3 rnd,
    bool lattice,
    float phase,
    int clip)
{
    int slot = index - (index / 16) * 16;
    int bone = slot < 2 ? 0 : (slot < 5 ? 1 : (slot == 5 ? 2 : (slot == 6 ? 3 : (slot == 7 ? 4 : (slot == 8 ? 5 : (slot == 9 ? 6 : (slot < 12 ? 7 : (slot == 12 ? 8 : (slot < 15 ? 9 : 10)))))))));
    float3 a, b;
    float rad;
    MocapBoneEndpoints(bone, phase, clip, a, b, rad);
    float along = lattice ? frac(float(index) * 0.6180339 + float(bone) * 0.13) : rnd.x;
    float ang = lattice ? float(index) * 2.39996323 : rnd.y * 6.2831853;
    float shell = lerp(1.0, sqrt(lattice ? frac(float(index) * 0.381966) : rnd.z), shape.AxisFill.w);
    float3 seg = b - a;
    float3 dir = length(seg) > 1.0e-4 ? normalize(seg) : float3(0.0, 1.0, 0.0);
    float3 helper = abs(dir.y) < 0.92 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 u = normalize(cross(helper, dir));
    float3 v = cross(dir, u);
    float3 local = lerp(a, b, along) + (u * cos(ang) + v * sin(ang)) * (rad * shell);
    if (bone == 0)
    {
        float3 hc = lerp(a, b, 0.58);
        float hz = along * 2.0 - 1.0;
        float hr = sqrt(max(0.0, 1.0 - hz * hz));
        local = hc + float3(cos(ang) * hr, hz, sin(ang) * hr) * (rad * 1.08 * shell);
    }
    float figureScale = max(0.25, shape.OriginRadius.w * shape.MocapParams.w);
    return shape.OriginRadius.xyz + local * figureScale;
}

public float3 EvaluateMocapPose(
    ParticleShapeUniformBlock shape,
    int index,
    float3 rnd,
    bool lattice,
    float time,
    out float3 limbVelocity)
{
    limbVelocity = float3(0.0, 0.0, 0.0);
    int mocapClip = int(shape.MocapParams.x + 0.5);
    float mocapAmp = shape.MocapParams.y;
    float mocapSpeed = shape.MocapParams.z;
    float phase = time * mocapSpeed * 4.2;
    float3 rest = MocapSamplePoint(shape, index, rnd, lattice, 0.0, 0);
    if (mocapClip <= 0 || mocapAmp <= 0.0001) return rest;
    float3 posed = MocapSamplePoint(shape, index, rnd, lattice, phase, mocapClip);
    float3 prev  = MocapSamplePoint(shape, index, rnd, lattice, phase - 0.12 * max(mocapSpeed, 0.1), mocapClip);
    float amp = clamp(mocapAmp, 0.0, 2.0);
    limbVelocity = (posed - prev) * (8.0 * amp * shape.MocapKickEffector.x);
    return lerp(rest, posed, amp);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              16 SHAPE SAMPLERS (L249-L433)
//------------------------------------------------------------------------------------------------------------------------

public float3 ShapePointAtTime(
    ParticleShapeUniformBlock shape,
    StructuredBuffer<float4> anchorTexels,
    int index,
    float3 rnd,
    bool lattice,
    float time,
    out float3 shapeVelocity)
{
    shapeVelocity = float3(0.0, 0.0, 0.0);
    float3 origin = shape.OriginRadius.xyz;
    float radius  = shape.OriginRadius.w;
    float3 axis   = shape.AxisFill.xyz;
    float fill    = shape.AxisFill.w;
    int shapeCode = int(shape.ShapeCounts.x + 0.5);
    int countInt  = max(int(shape.ShapeCounts.y + 0.5), 1);
    float shapeDetail = shape.ShapeCounts.z;
    int anchorCount = int(shape.AnchorLayout.y + 0.5);

    float3 t, b;
    ShapeBasis(axis, t, b);
    float count = float(countInt);
    float fi = float(index);

    if (shapeCode == 9)
    {
        return EvaluateMocapPose(shape, index, rnd, lattice, time, shapeVelocity);
    }

    float3 basePos = origin;
    if (shapeCode == 1)
    {
        if (lattice)
        {
            float golden = 2.39996323;
            float y = 1.0 - (fi + 0.5) / count * 2.0;
            float r = sqrt(max(0.0, 1.0 - y * y));
            float a = golden * fi;
            float shell = lerp(1.0, pow(Hash31(float3(fi, 3.1, 7.7)), 1.0 / 3.0), fill);
            basePos = origin + float3(cos(a) * r, y, sin(a) * r) * radius * shell;
        }
        else
        {
            float3 d = normalize(rnd * 2.0 - 1.0 + 1.0e-4);
            basePos = origin + d * radius * pow(Hash31(rnd.zxy + 2.7), 1.0 / 3.0);
        }
    }
    else if (shapeCode == 2 || shapeCode == 6)
    {
        if (lattice || shapeCode == 6)
        {
            float side = ceil(pow(count, 1.0 / 3.0));
            float x = fi - side * floor(fi / side);
            float yRaw = floor(fi / side);
            float y = yRaw - side * floor(yRaw / side);
            float z = floor(fi / (side * side));
            float3 cell = (float3(x, y, z) + 0.5) / side * 2.0 - 1.0;
            basePos = origin + (t * cell.x + axis * cell.y + b * cell.z) * radius;
        }
        else
        {
            float3 cell = rnd * 2.0 - 1.0;
            basePos = origin + (t * cell.x + axis * cell.y + b * cell.z) * radius;
        }
    }
    else if (shapeCode == 3)
    {
        float a = lattice ? (2.39996323 * fi) : (rnd.x * 6.2831853);
        float r = lattice ? sqrt((fi + 0.5) / count) : sqrt(rnd.y);
        basePos = origin + (t * cos(a) + b * sin(a)) * r * radius;
    }
    else if (shapeCode == 4)
    {
        float a = lattice ? (fi / count * 6.2831853) : (rnd.x * 6.2831853);
        basePos = origin + (t * cos(a) + b * sin(a)) * radius;
    }
    else if (shapeCode == 5)
    {
        float s = lattice ? ((fi + 0.5) / count * 2.0 - 1.0) : (rnd.x * 2.0 - 1.0);
        basePos = origin + axis * s * radius;
    }
    else if (shapeCode == 7)
    {
        float u = lattice ? ((fi + 0.5) / count * 6.2831853) : (rnd.x * 6.2831853);
        float v = lattice ? (frac(fi * 0.6180339887) * 6.2831853) : (rnd.y * 6.2831853);
        float tube = radius * lerp(0.28, 0.44, clamp(fill, 0.0, 1.0));
        float shell = lattice ? 1.0 : lerp(1.0, sqrt(rnd.z), fill);
        float3 radial = t * cos(u) + b * sin(u);
        basePos = origin + radial * (radius + cos(v) * tube * shell) + axis * (sin(v) * tube * shell);
    }
    else if (shapeCode == 8)
    {
        float3 cell = lattice ? BoxShellLocal(index, count) : BoxShellLocal(int(rnd.x * count), count);
        basePos = origin + (t * cell.x + axis * cell.y + b * cell.z) * radius;
    }
    else if (shapeCode == 10)
    {
        float y = lattice ? (1.0 - (fi + 0.5) / count) : rnd.x;
        float r = sqrt(max(0.0, 1.0 - y * y));
        float a = lattice ? (2.39996323 * fi) : (rnd.y * 6.2831853);
        float shell = lerp(1.0, pow(lattice ? frac(fi * 0.381966) : rnd.z, 1.0 / 3.0), fill);
        basePos = origin + (t * (cos(a) * r) + axis * y + b * (sin(a) * r)) * radius * shell;
    }
    else if (shapeCode == 11)
    {
        float3 normal;
        float3 local = ObjectAnchorPosition(anchorTexels, anchorCount, index, normal);
        float3 outward = local + normal * fill * Hash31(rnd.zxy + 3.3) * 0.35;
        basePos = origin + (t * outward.x + axis * outward.y + b * outward.z) * radius;
    }
    else if (shapeCode == 12)
    {
        float detail = shapeDetail > 0.5 ? shapeDetail : 8.0;
        int spokes = int(clamp(detail, 4.0, 24.0));
        int rings = int(clamp(detail * 1.75, 3.0, 34.0));
        int spokeCount = spokes * (rings + 1);
        bool onSpoke = (index % 4) < 2;
        float angle, rVal;
        if (onSpoke)
        {
            angle = float(index % spokes) / float(spokes) * 6.2831853;
            float stepFrac = lattice ? frac(float(index / spokes) * 0.6180339887) : rnd.y;
            rVal = (float(index / max(spokeCount, 1)) + stepFrac) / float(rings + 1);
            rVal = clamp(rVal, 0.06, 1.0);
        }
        else
        {
            float ring = float((index / max(spokes, 1)) % max(rings, 1));
            angle = float(index % max(spokes, 1)) / float(spokes) * 6.2831853 + (lattice ? 0.0 : rnd.x * 0.12);
            rVal = (ring + 0.5) / float(rings);
        }
        float sag = fill * 0.42 * sin(3.14159265 * clamp(rVal, 0.0, 1.0));
        basePos = origin + (t * (cos(angle) * rVal) + b * (sin(angle) * rVal)) * radius - axis * sag * radius;
    }
    else if (shapeCode == 13)
    {
        float detail = shapeDetail > 0.5 ? shapeDetail : 6.0;
        float folds = max(2.0, detail);
        float side = max(2.0, floor(sqrt(count)));
        float fx = fi - side * floor(fi / side);
        float fy = floor(fi / side);
        float u = (fx + 0.5) / side * 2.0 - 1.0;
        float v = min((fy + 0.5) / side, 1.0);
        float sway = sin(u * 3.14159265 * folds) * (0.10 + v * 0.22) * (0.4 + fill);
        float scallop = sin(u * 3.14159265 * folds * 0.5) * 0.06 * v;
        basePos = origin + t * u * radius + axis * (0.5 - v) * radius * 1.6 + b * (sway + scallop) * radius;
    }
    else if (shapeCode == 14)
    {
        float blades = clamp(shapeDetail > 0.5 ? shapeDetail : 26.0, 3.0, 512.0);
        int perBlade = max(1, int(ceil(count / blades)));
        int blade = index / perBlade;
        float h = float(index - blade * perBlade) / float(perBlade);
        float rootAngle = Hash31(float3(float(blade), 1.7, 9.3)) * 6.2831853;
        float rootRadius = sqrt(Hash31(float3(float(blade), 4.1, 2.7))) * radius;
        float height = lerp(0.55, 1.65, Hash31(float3(float(blade), 7.7, 5.1))) * radius;
        float bend = (0.35 + fill) * pow(h, 1.6) * radius;
        float flutter = sin(h * 3.0 + rootAngle * 3.0 + time * 1.4) * 0.06 * h * radius;
        float3 root = t * (cos(rootAngle) * rootRadius) + b * (sin(rootAngle) * rootRadius);
        basePos = origin + root + axis * (h * height) + b * (bend + flutter);
    }
    else if (shapeCode == 15)
    {
        if (anchorCount > 0)
        {
            float3 fwTint;
            float3 target = ObjectAnchorPosition(anchorTexels, anchorCount, index, fwTint);
            basePos = origin + (t * target.x + axis * target.y + b * target.z) * radius;
        }
        else
        {
            float detail = shapeDetail > 0.5 ? shapeDetail : 6.0;
            float lobes = max(2.0, floor(detail));
            float y = lattice ? (1.0 - (fi + 0.5) / count * 2.0) : (rnd.x * 2.0 - 1.0);
            float r = sqrt(max(0.0, 1.0 - y * y));
            float a = lattice ? (2.39996323 * fi) : (rnd.y * 6.2831853);
            float lobe = 0.78 + 0.22 * abs(cos(a * lobes * 0.5));
            float shell = lerp(lobe, pow(max(rnd.z, 1.0e-4), 1.0 / 3.0), clamp(fill, 0.0, 1.0));
            basePos = origin + (t * (cos(a) * r) + axis * y + b * (sin(a) * r)) * radius * shell;
        }
    }

    int mocapClip = int(shape.MocapParams.x + 0.5);
    float mocapAmp = shape.MocapParams.y;
    if (mocapClip > 0 && mocapAmp > 0.001)
    {
        float3 limbVel;
        float3 fig = EvaluateMocapPose(shape, index, rnd, lattice, time, limbVel);
        shapeVelocity = limbVel;
        basePos = lerp(basePos, fig, clamp(mocapAmp, 0.0, 1.0));
    }
    return basePos;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           MOGRAPH EFFECTOR & DEREZ (L441-L520)
//------------------------------------------------------------------------------------------------------------------------

public float EvaluateEffector(ParticleShapeUniformBlock shape, float3 pos, float time)
{
    int effMode = int(shape.MocapKickEffector.y + 0.5);
    float effStrength = shape.MocapKickEffector.z;
    if (effMode <= 0 || effStrength <= 0.0001) return 0.0;
    float freq = max(0.15, shape.MocapKickEffector.w);
    float effSpeed = shape.EffectorMotion.x;
    float3 origin = shape.OriginRadius.xyz;
    float radius = shape.OriginRadius.w;
    float3 dVec = (pos - origin) * freq;
    float w = 0.0;

    if (effMode == 1)
    {
        float dist = length(pos - origin);
        float ph = dist * freq * 3.4 - time * effSpeed * 3.1;
        w = pow(max(0.0, 0.5 + 0.5 * sin(ph)), 1.35);
    }
    else if (effMode == 2)
    {
        float ph0 = time * effSpeed * 1.35;
        float ph1 = time * effSpeed * 1.05 + 2.4;
        float orbR = max(radius, 0.5) * 0.78;
        float3 e0 = origin + float3(cos(ph0) * orbR, sin(ph0 * 1.3) * orbR * 0.55, sin(ph0) * orbR);
        float3 e1 = origin + float3(cos(ph1) * orbR * 0.85, cos(ph1 * 0.9) * orbR * 0.5, sin(ph1 * 1.2) * orbR * 0.85);
        float radEff = max(0.45, 1.15 / max(0.35, freq));
        float g0 = 1.0 - smoothstep(radEff * 0.18, radEff, length(pos - e0));
        float g1 = 1.0 - smoothstep(radEff * 0.18, radEff, length(pos - e1));
        w = max(g0, g1);
    }
    else if (effMode == 3)
    {
        float tShift = time * effSpeed;
        float warp = ValueNoise(dVec * 0.78 + float3(0.0, -tShift * 0.35, tShift * 0.25)) * 2.4;
        float waveA = 0.5 + 0.5 * sin(dVec.x * 1.35 + dVec.y * 1.55 - tShift * 1.55 + warp);
        float blot = ValueNoise(dVec * 1.18 + float3(tShift * 0.42, -tShift * 0.28, 2.3));
        w = clamp(waveA * 0.62 + blot * 0.58 - 0.14, 0.0, 1.0);
    }
    else
    {
        float tS = time * effSpeed * 0.45;
        float n0 = ValueNoise(dVec * 1.15 + float3(tS, -tS * 0.7, tS * 0.5));
        float n1 = ValueNoise(dVec * 2.25 - float3(tS * 1.3, tS * 0.4, -tS * 0.8));
        w = clamp((n0 * 0.68 + n1 * 0.32 - 0.26) * 1.65, 0.0, 1.0);
    }
    return clamp(w * effStrength, 0.0, 2.0);
}

public DerezEvaluationResult EvaluateDerez(ParticleShapeUniformBlock shape, float3 restHome, float time)
{
    DerezEvaluationResult res;
    res.WaveU = 0.5;
    res.Progress = 0.0;
    res.EdgeBand = 0.0;
    res.Released = false;

    int derezMode = int(shape.EffectorAxisDerez.y + 0.5);
    float derezMix = shape.EffectorAxisDerez.z;
    if (derezMode <= 0 || derezMix <= 0.0001) return res;

    float span = max(shape.OriginRadius.w, 0.15);
    float3 u = clamp((restHome - shape.OriginRadius.xyz) / (2.0 * span) + 0.5, 0.0, 1.0);
    if (derezMode == 5)
    {
        float3 cell = floor(u * float3(7.0, 5.0, 7.0));
        float h = frac(sin(dot(cell, float3(12.9898, 78.233, 37.719))) * 43758.5453);
        res.WaveU = clamp(0.6 * h + 0.4 * u.y, 0.0, 1.0);
    }
    else if (derezMode == 3)
    {
        res.WaveU = clamp(0.5 + 0.3 * sin(u.x * 5.0 + u.y * 3.0 + 0.7) + 0.2 * sin(u.y * 7.0 - u.z * 4.0 + 2.1), 0.0, 1.0);
    }
    else
    {
        res.WaveU = (u.x + u.y + u.z) / 3.0;
    }

    float autoSpeed = shape.DerezFrontParams.x;
    float manualProg = shape.EffectorAxisDerez.w;
    float rawProg = autoSpeed > 0.0001 ? (frac(time * autoSpeed) * 1.32 - 0.12) : (manualProg * 1.24 - 0.06);
    res.Progress = rawProg * clamp(derezMix, 0.0, 1.0);
    float width = max(0.02, shape.DerezFrontParams.y);
    float dist = res.Progress - res.WaveU;
    res.EdgeBand = exp(-(dist * dist) / max(width * width * 0.55, 1.0e-4));
    res.Released = (derezMode == 2) ? (res.Progress < res.WaveU) : (res.Progress >= res.WaveU);
    return res;
}
