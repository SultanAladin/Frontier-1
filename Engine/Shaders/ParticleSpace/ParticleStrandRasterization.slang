/*============================================================================================================================================
                                                   PARTICLESTRANDRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Pure .slang fluid-coupled GPU strands, catenary cables, hanging vines, flexing flora, spiderwebs & electrical arcs (11 modes x 2 variants)
// Reference: Experimental/FluidEditor/Engine/ParticleShaders.js L1552-L2073 (StrandVertex & StrandFragment)

import ForceFieldNoise;
import ForceModelEvaluation;

//------------------------------------------------------------------------------------------------------------------------
//                                                    DATA & UNIFORM BLOCKS
//------------------------------------------------------------------------------------------------------------------------

/// Matches C++ ParticleStrandUniformBlock byte-for-byte (352 bytes, 16-byte aligned).
public struct ParticleStrandUniformBlock
{
    public float4x4 ViewProjection;              // Row-major world -> clip
    public float4   SwatchStops[8];              // xyz = linear sRGB swatch stops
    public float4   CameraPositionFocal;         // xyz = cameraPosition, w = focalPixels
    public float4   CameraForwardViewportX;      // xyz = cameraForward, w = viewportWidthPx
    public float4   OriginRadius;                // xyz = origin, w = radius
    public float4   AxisTime;                    // xyz = unit axis, w = simulationTime
    public float4   GridCentreHalfX;             // xyz = gridCentre, w = gridHalf.x
    public float4   GridHalfYZForceGas;          // x = gridHalf.y, y = gridHalf.z, z = forceCount, w = hasGasVolume
    public float4   ModeVariantCounts;           // x = strandMode (1..11), y = strandVariant (0/1), z = strandCount, w = strandTotal
    public float4   SegmentsDetailViewportSwatch;// x = strandSegments, y = strandDetail, z = viewportHeightPx, w = swatchCount
    public float4   SagThicknessSwaySpan;        // x = strandSag, y = strandThickness, z = strandSway, w = strandSpan
    public float4   GlowBrightnessEnvelope;      // x = strandGlow, y = brightness, z = envelope, w = reserved
};

public struct StrandVertexOutput
{
    float4 ClipPosition : SV_Position;
    float3 Colour       : COLOR0;
    float4 StrandData   : TEXCOORD0; // x = along (0..1), y = across (-1..1), z = styleTag, w = seed
    float  Alpha        : TEXCOORD1;
};

[[vk::binding(0, 0)]] ConstantBuffer<ParticleStrandUniformBlock> uParams;
[[vk::binding(1, 0)]] StructuredBuffer<ForceModelGpuRecord>      gForceRecords;
[[vk::binding(2, 0)]] Texture3D<float4>                          gGasVelocityVolume;
[[vk::binding(3, 0)]] SamplerState                               gLinearClampSampler;

//------------------------------------------------------------------------------------------------------------------------
//                                                   STRAND MATH HELPERS
//------------------------------------------------------------------------------------------------------------------------

float3 SampleStrandSwatch(float t)
{
    int n = max(int(uParams.SegmentsDetailViewportSwatch.w + 0.5), 1);
    if (n == 1) return uParams.SwatchStops[0].xyz;
    float scaled = clamp(t, 0.0, 1.0) * float(n - 1);
    int i0 = int(floor(scaled));
    int i1 = min(i0 + 1, n - 1);
    return lerp(uParams.SwatchStops[i0].xyz, uParams.SwatchStops[i1].xyz, frac(scaled));
}

float StrandHash(float n, float salt)
{
    return frac(sin(n * 127.1 + salt * 311.7) * 43758.5453123);
}

void StrandBasis(out float3 rAxis, out float3 uVec, out float3 fAxis)
{
    float3 axis = uParams.AxisTime.xyz;
    uVec = (length(axis) > 1.0e-4) ? normalize(axis) : float3(0.0, 1.0, 0.0);
    float3 refAxis = (abs(uVec.z) < 0.92) ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    rAxis = normalize(cross(uVec, refAxis));
    fAxis = cross(rAxis, uVec);
}

float2 OrbAnchor(int idx, float R)
{
    float a  = float(idx % 6) * 1.04719755 + 0.28 + (StrandHash(float(idx), 2.0) - 0.5) * 0.14;
    float rr = R * (0.94 + 0.14 * StrandHash(float(idx), 3.0));
    return float2(cos(a) * rr, sin(a) * rr);
}

float3 SampleGasVelocityStrand(float3 worldPos)
{
    if (uParams.GridHalfYZForceGas.w < 0.5) return float3(0.0, 0.0, 0.0);
    float3 gridCentre = uParams.GridCentreHalfX.xyz;
    float3 gridHalf   = float3(uParams.GridCentreHalfX.w, uParams.GridHalfYZForceGas.x, uParams.GridHalfYZForceGas.y);
    float3 uvw = (worldPos - (gridCentre - gridHalf)) / max(gridHalf * 2.0, float3(1.0e-4, 1.0e-4, 1.0e-4));
    if (any(uvw < 0.0) || any(uvw > 1.0)) return float3(0.0, 0.0, 0.0);
    return gGasVelocityVolume.SampleLevel(gLinearClampSampler, uvw, 0.0).xyz;
}

float3 FluidWindAt(float3 p)
{
    uint forceCount = uint(max(0, int(uParams.GridHalfYZForceGas.z + 0.5)));
    float simTime   = uParams.AxisTime.w;
    float3 f = (forceCount > 0u)
        ? (EvaluateTotalForceField(gForceRecords, forceCount, p, float3(0.0, 0.0, 0.0), simTime, kForceChannelParticle) * 0.20)
        : float3(0.0, 0.0, 0.0);
    float3 g = SampleGasVelocityStrand(p) * 0.32;
    float3 breeze = CurlNoise(p * 0.65 + float3(simTime * 0.45, 0.0, -simTime * 0.3)) * 0.22;
    return (f + g + breeze) * uParams.SagThicknessSwaySpan.z;
}

void EvalStrandEndpoints(
    int slot,
    float3 rAxis,
    float3 uVec,
    float3 fAxis,
    out float3 pA,
    out float3 pB,
    out float sag,
    out float widthScale,
    out float pinB,
    out float styleTag,
    out float colT,
    out float vis)
{
    float sd          = float(slot) * 37.0 + 13.0;
    float3 origin     = uParams.OriginRadius.xyz;
    float R           = max(0.45, uParams.OriginRadius.w);
    float span        = max(0.35, uParams.SagThicknessSwaySpan.w);
    float baseSag     = uParams.SagThicknessSwaySpan.x;
    float simTime     = uParams.AxisTime.w;
    int strandMode    = int(uParams.ModeVariantCounts.x + 0.5);
    int strandVariant = int(uParams.ModeVariantCounts.y + 0.5);
    int strandCount   = int(uParams.ModeVariantCounts.z + 0.5);
    int strandTotal   = int(uParams.ModeVariantCounts.w + 0.5);
    int strandDetail  = int(uParams.SegmentsDetailViewportSwatch.y + 0.5);

    pA = origin;
    pB = origin + uVec * 0.5;
    sag = baseSag;
    widthScale = 1.0;
    pinB = 0.0;
    styleTag = 0.0;
    colT = StrandHash(sd, 9.0);
    vis = 1.0;

    if (strandMode == 1)
    {
        // 1: Orb-Weave Spiderweb (6 outer cords + 6 guy lines + spokes + Archimedean capture spiral)
        int spokes = clamp(strandCount, 8, 36);
        int rings  = clamp(strandDetail, 6, 24);
        float3 hub = origin - uVec * (0.04 * R);
        pinB = 1.0;
        styleTag = 1.0;
        if (slot < 6)
        {
            float2 la = OrbAnchor(slot, R);
            float2 lb = OrbAnchor((slot + 1) % 6, R);
            pA = origin + rAxis * la.x + uVec * la.y;
            pB = origin + rAxis * lb.x + uVec * lb.y;
            sag = baseSag * 0.22;
            widthScale = 0.65;
            colT = 0.15;
        }
        else if (slot < 12)
        {
            float2 la = OrbAnchor(slot - 6, R);
            pA = origin + rAxis * la.x + uVec * la.y;
            pB = origin + rAxis * (la.x * 1.24) + uVec * (la.y * 1.24);
            sag = baseSag * 0.08;
            widthScale = 0.58;
            colT = 0.1;
        }
        else if (slot < 12 + spokes)
        {
            int si = slot - 12;
            float th = (float(si) / float(spokes)) * 6.2831853 + (StrandHash(float(si), 1.0) - 0.5) * 0.03;
            pA = hub;
            pB = origin + (rAxis * cos(th) + uVec * sin(th)) * (R * 0.95);
            sag = baseSag * 0.12;
            widthScale = 0.52;
            colT = 0.3;
        }
        else
        {
            int spIdx = slot - 12 - spokes;
            int totalSpiral = spokes * rings;
            if (spIdx >= totalSpiral) { vis = 0.0; return; }
            int s0 = spIdx % spokes;
            int s1 = (spIdx + 1) % spokes;
            float f0 = float(spIdx) / float(max(totalSpiral, 1));
            float f1 = float(spIdx + 1) / float(max(totalSpiral, 1));
            float r0 = lerp(0.14, 0.92, pow(f0, 0.92)) * R;
            float r1 = lerp(0.14, 0.92, pow(f1, 0.92)) * R;
            float th0 = (float(s0) / float(spokes)) * 6.2831853 + (StrandHash(float(s0), 1.0) - 0.5) * 0.03;
            float th1 = (float(s1) / float(spokes)) * 6.2831853 + (StrandHash(float(s1), 1.0) - 0.5) * 0.03;
            pA = hub + (rAxis * cos(th0) + uVec * sin(th0)) * r0;
            pB = hub + (rAxis * cos(th1) + uVec * sin(th1)) * r1;
            sag = baseSag * (0.14 + 0.32 * f0) * clamp(length(pB - pA) / max(0.3, R * 0.35), 0.3, 1.2);
            widthScale = 0.42;
            colT = lerp(0.45, 0.95, f0);
            styleTag = 1.5;
        }
    }
    else if (strandMode == 2)
    {
        // 2: Messy Attic Cobweb (3D corner anchor chords + crisscrossed lacy tangle + dangling wisps)
        int nChords = max(12, strandTotal / 3);
        int nTangle = nChords + max(12, (strandTotal * 4) / 10);
        styleTag = 2.0;
        if (slot < nChords)
        {
            int pocket = slot % 3;
            float ph = float(pocket) * 2.0943951;
            float3 dirA = float3(cos(ph), 0.35 + 0.65 * StrandHash(sd, 1.0), sin(ph));
            float3 dirB = float3(cos(ph + 1.35 + 0.5 * StrandHash(sd, 2.0)), -0.35 + 1.15 * StrandHash(sd, 3.0), sin(ph + 1.35));
            pA = origin + dirA * (R * lerp(0.55, 1.15, StrandHash(sd, 4.0)));
            pB = origin + dirB * (R * lerp(0.55, 1.15, StrandHash(sd, 5.0)));
            sag = baseSag * (0.45 + 0.65 * StrandHash(sd, 6.0));
            pinB = 1.0;
            widthScale = 0.50;
            colT = 0.25;
        }
        else if (slot < nTangle)
        {
            float a0 = StrandHash(sd, 1.0) * 6.2831853;
            float a1 = a0 + lerp(0.35, 1.45, StrandHash(sd, 2.0));
            float r0 = R * lerp(0.20, 0.95, StrandHash(sd, 3.0));
            float r1 = R * lerp(0.20, 0.95, StrandHash(sd, 4.0));
            pA = origin + float3(cos(a0) * r0, (StrandHash(sd, 5.0) - 0.35) * span * 0.8, sin(a0) * r0);
            pB = origin + float3(cos(a1) * r1, (StrandHash(sd, 6.0) - 0.35) * span * 0.8, sin(a1) * r1);
            sag = baseSag * (0.35 + 0.75 * StrandHash(sd, 7.0));
            pinB = 1.0;
            widthScale = 0.38;
            colT = 0.58;
        }
        else
        {
            float a0 = StrandHash(sd, 1.0) * 6.2831853;
            float r0 = R * lerp(0.18, 0.85, StrandHash(sd, 2.0));
            pA = origin + float3(cos(a0) * r0, (StrandHash(sd, 3.0) - 0.1) * span * 0.7, sin(a0) * r0);
            float dLen = span * lerp(0.32, 0.82, StrandHash(sd, 4.0));
            pB = pA + float3((StrandHash(sd, 5.0) - 0.5) * 0.22, -dLen, (StrandHash(sd, 6.0) - 0.5) * 0.22);
            sag = 0.04;
            pinB = 0.0;
            widthScale = 0.34;
            colT = 0.85;
        }
    }
    else if (strandMode == 3)
    {
        // 3: Swinging Cables & Heavy Industrial Chains
        int tier = slot % 3;
        float lane = float(slot / 3) / max(1.0, float(max(strandTotal / 3, 1) - 1));
        float yTier = span * (0.38 + 0.26 * float(tier)) - span * 0.25;
        float zOff = lerp(-R, R, lane) + (StrandHash(sd, 1.0) - 0.5) * 0.22;
        float xJit = (StrandHash(sd, 2.0) - 0.5) * 0.18;
        pA = origin + float3(-span * 0.95 + xJit, yTier, zOff);
        pB = origin + float3( span * 0.95 - xJit, yTier + (StrandHash(sd, 3.0) - 0.5) * 0.22, zOff + (StrandHash(sd, 4.0) - 0.5) * 0.18);
        sag = baseSag * (0.72 + 0.65 * StrandHash(sd, 5.0));
        pinB = 1.0;
        widthScale = (strandVariant == 1) ? 1.55 : 1.0;
        styleTag   = (strandVariant == 1) ? 3.5  : 3.0;
        colT = float(tier) * 0.42;
    }
    else if (strandMode == 4)
    {
        // 4: Hanging Vines & Braided Overhead Ropes
        float uX = StrandHash(sd, 1.0) * 2.0 - 1.0;
        float uZ = StrandHash(sd, 2.0) * 2.0 - 1.0;
        float yTop = origin.y + span * (0.55 + 0.22 * StrandHash(sd, 3.0));
        float vLen = span * lerp(0.62, 1.25, StrandHash(sd, 4.0));
        pA = float3(origin.x + uX * R, yTop, origin.z + uZ * R);
        pB = pA + float3((StrandHash(sd, 5.0) - 0.5) * 0.30, -vLen, (StrandHash(sd, 6.0) - 0.5) * 0.30);
        sag = 0.05;
        pinB = 0.0;
        widthScale = (strandVariant == 1) ? 2.15 : 1.05;
        styleTag   = (strandVariant == 1) ? 4.5  : 4.0;
        colT = StrandHash(sd, 7.0);
    }
    else if (strandMode == 5)
    {
        // 5: Tall Lakeside Reeds with brown velvet cattail spikes
        int clump = slot % 6;
        float cSd = float(clump) * 23.0 + 7.0;
        float cx = (StrandHash(cSd, 1.0) * 2.0 - 1.0) * R * 0.72;
        float cz = (StrandHash(cSd, 2.0) * 2.0 - 1.0) * R * 0.72;
        float ang = StrandHash(sd, 1.0) * 6.2831853;
        float rOff = sqrt(StrandHash(sd, 2.0)) * R * 0.38;
        float hReed = span * lerp(0.72, 1.22, StrandHash(sd, 3.0));
        float archOut = (0.24 + 0.26 * StrandHash(sd, 4.0)) * hReed;
        pA = float3(origin.x + cx + cos(ang) * rOff, origin.y - span * 0.45, origin.z + cz + sin(ang) * rOff);
        pB = pA + float3(cos(ang) * archOut, hReed, sin(ang) * archOut);
        sag = -max(0.08, baseSag * 0.42) * hReed;
        pinB = 0.0;
        widthScale = (slot % 3 == 0) ? 1.55 : 0.95;
        styleTag   = (slot % 3 == 0) ? 5.5  : 5.0;
        colT = StrandHash(sd, 5.0);
    }
    else if (strandMode == 6)
    {
        // 6: Lush Woodland Broad Fern
        int tier = slot % 3;
        float tierFrac = float(tier) / 2.0;
        float az = float(slot) * 2.3999632 + (StrandHash(sd, 1.0) - 0.5) * 0.16;
        float fLen = max(0.65, R * 1.15) * lerp(0.54, 1.08, tierFrac) * lerp(0.88, 1.14, StrandHash(sd, 2.0));
        float tipY = span * lerp(0.88, 0.32, tierFrac) * lerp(0.88, 1.14, StrandHash(sd, 3.0));
        pA = float3(origin.x + cos(az) * 0.08, origin.y - span * 0.38, origin.z + sin(az) * 0.08);
        pB = pA + float3(cos(az) * fLen, tipY, sin(az) * fLen);
        sag = -max(0.18, baseSag) * lerp(0.72, 1.22, tierFrac);
        pinB = 0.0;
        widthScale = lerp(3.2, 4.8, tierFrac);
        styleTag = 6.0;
        colT = tierFrac;
    }
    else if (strandMode == 7)
    {
        // 7: Underwater Giant Kelp Forest
        float gx = (StrandHash(sd, 1.0) * 2.0 - 1.0) * R;
        float gz = (StrandHash(sd, 2.0) * 2.0 - 1.0) * R;
        float kelpH = span * lerp(0.68, 1.25, StrandHash(sd, 3.0));
        float leanX = (StrandHash(sd, 4.0) - 0.5) * 0.35;
        float leanZ = (StrandHash(sd, 5.0) - 0.5) * 0.35;
        pA = float3(origin.x + gx, origin.y - span * 0.52, origin.z + gz);
        pB = pA + float3(leanX, kelpH, leanZ);
        sag = -baseSag * 0.18;
        pinB = 0.0;
        widthScale = 2.65;
        styleTag = 7.0;
        colT = 0.2 + 0.65 * StrandHash(sd, 6.0);
    }
    else if (strandMode == 8)
    {
        // 8: Bioluminescent Sea Anemone Tentacle Clusters
        int cluster = slot % 3;
        float cPhi = float(cluster) * 2.0943951 + 0.4;
        float cRad = (cluster == 0) ? 0.0 : (0.55 * R);
        float3 mound = float3(origin.x + cos(cPhi) * cRad, origin.y - span * 0.35, origin.z + sin(cPhi) * cRad);
        float ringR = lerp(0.05, ((cluster == 0) ? 0.38 : 0.28) * R, sqrt(StrandHash(sd, 1.0)));
        float az = float(slot) * 2.3999632 + StrandHash(sd, 2.0) * 0.38;
        float tLen = span * lerp(0.55, 1.05, StrandHash(sd, 3.0));
        float flare = ringR * 1.45 + 0.42 * tLen;
        pA = mound + float3(cos(az) * ringR, 0.06 + (0.28 - min(ringR, 0.25)) * 0.45, sin(az) * ringR);
        pB = mound + float3(cos(az) * flare, tLen, sin(az) * flare);
        sag = -max(0.14, baseSag * 0.62);
        pinB = 0.0;
        widthScale = 1.65;
        styleTag = 8.0;
        colT = 0.55 + 0.45 * StrandHash(sd, 4.0);
    }
    else if (strandMode == 9)
    {
        // 9: Tesla Sphere Lightning (Variant 0) & Static Shock Corona Arcs (Variant 1)
        float halfSpan = span * 0.85;
        float3 poleA = origin - rAxis * halfSpan;
        float3 poleB = origin + rAxis * halfSpan;
        float spread = (strandVariant == 1) ? 0.42 : 0.26;
        float3 offA = float3(StrandHash(sd, 1.0) - 0.5, StrandHash(sd, 2.0) - 0.5, StrandHash(sd, 3.0) - 0.5) * spread;
        float3 offB = float3(StrandHash(sd, 4.0) - 0.5, StrandHash(sd, 5.0) - 0.5, StrandHash(sd, 6.0) - 0.5) * spread;
        pA = poleA + offA;
        pB = poleB + offB;
        if (slot > 2 && strandVariant == 0)
        {
            float bf = lerp(0.22, 0.78, StrandHash(sd, 7.0));
            pA = lerp(poleA, poleB, bf);
            pB = pA + normalize(float3(StrandHash(sd, 8.0) - 0.5, StrandHash(sd, 9.0) - 0.35, StrandHash(sd, 10.0) - 0.5))
                    * (span * lerp(0.28, 0.65, StrandHash(sd, 11.0)));
        }
        sag = -baseSag * (0.25 + 0.55 * (StrandHash(sd, 12.0) - 0.5));
        pinB = 1.0;
        widthScale = (slot < 2) ? 1.45 : ((strandVariant == 1) ? 0.72 : 0.65);
        styleTag = 9.0;
        colT = (strandVariant == 1) ? 0.78 : 0.22;
    }
    else if (strandMode == 10)
    {
        // 10: Plasma Cage & Arc Collector
        float tick = floor(simTime * 10.0);
        float a = float(slot) / float(max(strandTotal, 1)) * 6.2831853 + StrandHash(float(slot) + tick, 1.0) * 0.45;
        float ringY = (StrandHash(float(slot) + tick, 2.0) - 0.5) * span * 0.75;
        pA = origin + float3(cos(a) * 0.14, sin(a * 2.0) * 0.08, sin(a) * 0.14);
        pB = origin + float3(cos(a) * R, ringY, sin(a) * R);
        sag = -baseSag * 0.25;
        pinB = 1.0;
        widthScale = 1.05;
        styleTag = 9.0;
        colT = frac(float(slot) * 0.17);
    }
    else if (strandMode == 11)
    {
        // 11: Dynamic Lightning Web & Storm Grid
        int nNodes = max(8, strandDetail);
        float tick = floor(simTime * 8.0);
        int aIdx = slot % nNodes;
        int bIdx = (aIdx + 1 + int(StrandHash(float(slot) + tick * 0.5, 1.0) * float(max(nNodes / 2, 1)))) % nNodes;
        float sa = float(aIdx) * 19.0 + 3.0;
        float sb = float(bIdx) * 19.0 + 3.0;
        pA = origin + float3(StrandHash(sa, 1.0) - 0.5, StrandHash(sa, 2.0) - 0.5, StrandHash(sa, 3.0) - 0.5) * (2.0 * R);
        pB = origin + float3(StrandHash(sb, 1.0) - 0.5, StrandHash(sb, 2.0) - 0.5, StrandHash(sb, 3.0) - 0.5) * (2.0 * R);
        sag = 0.0;
        pinB = 1.0;
        widthScale = 0.92;
        styleTag = 9.0;
        colT = frac(float(aIdx) * 0.23);
    }
}

float3 EvalStrandPoint(int slot, float3 pA, float3 pB, float sag, float pinB, float u, float styleTag)
{
    float3 p = lerp(pA, pB, u);
    float archWeight = (pinB > 0.5) ? (4.0 * u * (1.0 - u)) : (u * u);
    if (pinB > 0.5) p.y -= sag * archWeight;
    else p.y -= sag * 4.0 * u * (1.0 - u) * 0.65;

    float swayMask = (pinB > 0.5) ? (4.0 * u * (1.0 - u)) : pow(u, 1.35);
    float3 wind = FluidWindAt(p);
    float simTime = uParams.AxisTime.w;
    int strandMode = int(uParams.ModeVariantCounts.x + 0.5);
    float phase = float(slot) * 1.37 + u * 2.8;
    float waveSpeed = (strandMode == 7 || strandMode == 8) ? 1.35 : 2.25;
    float3 harmonic = float3(
        sin(simTime * waveSpeed + phase),
        cos(simTime * waveSpeed * 0.7 + phase) * 0.28,
        cos(simTime * waveSpeed * 0.9 + phase * 1.2)) * (0.14 * uParams.SagThicknessSwaySpan.z);
    p += (wind + harmonic) * swayMask;

    if (styleTag > 8.5)
    {
        int strandVariant = int(uParams.ModeVariantCounts.y + 0.5);
        int strandDetail  = int(uParams.SegmentsDetailViewportSwatch.y + 0.5);
        float rate = (strandVariant == 1) ? 26.0 : 15.0;
        float tick = floor(simTime * rate);
        float jitterAmp = (0.12 + 0.015 * float(strandDetail)) * max(0.35, uParams.SagThicknessSwaySpan.w * 0.45);
        float env = sin(u * 3.14159265);
        float3 jA = float3(
            StrandHash(float(slot) * 31.0 + floor(u * 12.0) + tick * 7.0, 1.0) - 0.5,
            StrandHash(float(slot) * 31.0 + floor(u * 12.0) + tick * 7.0, 2.0) - 0.5,
            StrandHash(float(slot) * 31.0 + floor(u * 12.0) + tick * 7.0, 3.0) - 0.5);
        float3 jB = float3(
            StrandHash(float(slot) * 53.0 + floor(u * 24.0) + tick * 13.0, 4.0) - 0.5,
            StrandHash(float(slot) * 53.0 + floor(u * 24.0) + tick * 13.0, 5.0) - 0.5,
            StrandHash(float(slot) * 53.0 + floor(u * 24.0) + tick * 13.0, 6.0) - 0.5);
        p += (jA * 0.72 + jB * 0.28) * (jitterAmp * env);
    }
    return p;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                VERTEX ENTRY (L1890-L1947)
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
StrandVertexOutput ParticleStrandVertexEntry(uint vertexId : SV_VertexID)
{
    int segs = max(int(uParams.SegmentsDetailViewportSwatch.x + 0.5), 4);
    int vertsPerStrand = segs * 6;
    int slot = int(vertexId) / vertsPerStrand;
    int local = int(vertexId) - slot * vertsPerStrand;
    int seg = local / 6;
    int corner = local - seg * 6;

    float3 rAxis, uVec, fAxis;
    StrandBasis(rAxis, uVec, fAxis);

    float3 pA, pB;
    float sag, widthScale, pinB, styleTag, colT, vis;
    EvalStrandEndpoints(slot, rAxis, uVec, fAxis, pA, pB, sag, widthScale, pinB, styleTag, colT, vis);

    int strandTotal = int(uParams.ModeVariantCounts.w + 0.5);
    if (vis < 0.5 || slot >= strandTotal)
    {
        StrandVertexOutput culled;
        culled.ClipPosition = float4(2.0, 2.0, 2.0, 1.0);
        culled.Colour       = float3(0.0, 0.0, 0.0);
        culled.StrandData   = float4(0.0, 0.0, 0.0, 0.0);
        culled.Alpha        = 0.0;
        return culled;
    }

    float u0 = float(seg) / float(segs);
    float u1 = float(seg + 1) / float(segs);
    bool isEnd = (corner == 2 || corner == 4 || corner == 5);
    float side = (corner == 0 || corner == 3 || corner == 5) ? -1.0 : 1.0;
    float along = isEnd ? u1 : u0;

    float3 a = EvalStrandPoint(slot, pA, pB, sag, pinB, u0, styleTag);
    float3 b = EvalStrandPoint(slot, pA, pB, sag, pinB, u1, styleTag);
    float3 world = isEnd ? b : a;

    float4 clipA = mul(uParams.ViewProjection, float4(a, 1.0));
    float4 clipB = mul(uParams.ViewProjection, float4(b, 1.0));
    float4 clip  = isEnd ? clipB : clipA;

    float2 viewport = max(float2(uParams.CameraForwardViewportX.w, uParams.SegmentsDetailViewportSwatch.z), float2(1.0, 1.0));
    float2 ndcA = clipA.xy / max(clipA.w, 1.0e-4);
    float2 ndcB = clipB.xy / max(clipB.w, 1.0e-4);
    float2 sDir = (ndcB - ndcA) * viewport;
    float sLen  = length(sDir);
    float2 normal = (sLen > 1.0e-4) ? (float2(-sDir.y, sDir.x) / sLen) : float2(0.0, 1.0);

    float depth = dot(world - uParams.CameraPositionFocal.xyz, uParams.CameraForwardViewportX.xyz);
    int strandMode = int(uParams.ModeVariantCounts.x + 0.5);
    float taper = (strandMode == 5 || strandMode == 8) ? lerp(1.0, 0.22, along)
        : (strandMode == 6 ? sin(pow(clamp(along, 0.02, 0.99), 0.72) * 3.14159265)
        : (strandMode == 7 ? (0.45 + 0.55 * sin(along * 3.14159265)) : 1.0));
    float widthMetres = max(0.003, uParams.SagThicknessSwaySpan.y * widthScale * max(taper, 0.14));
    float widthPixels = max(widthMetres * uParams.CameraPositionFocal.w / max(depth, 0.08), (styleTag < 2.5) ? 1.65 : 2.0);
    clip.xy += normal * side * widthPixels * 0.5 / viewport * 2.0 * clip.w;

    float3 colour = SampleStrandSwatch(clamp(lerp(colT, along, (styleTag > 4.5) ? 0.55 : 0.25), 0.0, 1.0))
                  * uParams.GlowBrightnessEnvelope.y;

    StrandVertexOutput o;
    o.ClipPosition = clip;
    o.Colour       = colour;
    o.StrandData   = float4(along, side, styleTag, StrandHash(float(slot), 11.0));
    o.Alpha        = (depth > 0.05) ? uParams.GlowBrightnessEnvelope.z : 0.0;
    return o;
}

//------------------------------------------------------------------------------------------------------------------------
//                                               FRAGMENT ENTRY (L1948-L2073)
//------------------------------------------------------------------------------------------------------------------------

[shader("fragment")]
float4 ParticleStrandFragmentEntry(StrandVertexOutput input) : SV_Target
{
    if (input.Alpha <= 0.002) discard;

    float u      = input.StrandData.x;
    float across = input.StrandData.y;
    float aAbs   = abs(across);
    float tag    = input.StrandData.z;
    float seed   = input.StrandData.w;
    float glow   = uParams.GlowBrightnessEnvelope.x;
    float simTime = uParams.AxisTime.w;

    float3 rgb   = input.Colour;
    float  alpha = input.Alpha;

    if (tag < 2.5)
    {
        // Spiderweb / Cobweb: backlit gossamer silk filament + twinkling morning dewdrop beads
        float core = exp(-aAbs * aAbs * 3.2);
        float dOff = frac(u * 4.0 + seed) - 0.5;
        float dew  = (tag > 1.25) ? (exp(-(dOff * dOff) * 64.0) * glow) : 0.0;
        rgb   = lerp(rgb * 0.95, float3(1.0, 1.0, 1.0), clamp(core * 0.55 + dew, 0.0, 1.0));
        alpha *= clamp(core * 0.92 + dew, 0.0, 1.0);
    }
    else if (tag < 3.8)
    {
        // Cables (3.0) & Heavy Industrial Chains (3.5)
        float cyl = sqrt(max(0.0, 1.0 - aAbs * aAbs));
        if (tag > 3.25)
        {
            float link = abs(sin(u * 48.0));
            float hole = smoothstep(0.28, 0.45, aAbs) + smoothstep(0.70, 0.92, link);
            if (hole < 0.38) discard;
            rgb = rgb * (0.45 + 0.75 * cyl * link) + float3(0.35 * pow(cyl, 6.0) * (0.5 + glow));
        }
        else
        {
            float braid = 0.85 + 0.15 * sin(u * 80.0 + across * 8.0);
            rgb = rgb * (0.42 + 0.68 * cyl) * braid + float3(0.28 * pow(cyl, 8.0));
        }
        alpha *= 1.0 - smoothstep(0.78, 1.0, aAbs);
    }
    else if (tag < 4.8)
    {
        // Braided Ropes (4.0) & Leafy Jungle Vines (4.5)
        if (tag > 4.25)
        {
            float stem = 1.0 - smoothstep(0.14, 0.24, aAbs);
            float leafPhase = frac(u * 14.0 + seed * 3.0) - 0.5;
            float sideAlt = (sin(floor(u * 14.0 + seed * 3.0) * 3.14159 + 1.57) > 0.0) ? 1.0 : -1.0;
            float2 lp = float2((across * sideAlt - 0.48) / 0.48, leafPhase / 0.34);
            float leaf = 1.0 - smoothstep(0.75, 1.0, length(lp));
            float mask = max(stem, leaf);
            if (mask < 0.05) discard;
            rgb = lerp(rgb * 0.52, rgb * (0.95 + 0.35 * (1.0 - abs(leafPhase))), leaf);
            alpha *= mask;
        }
        else
        {
            float cyl = sqrt(max(0.0, 1.0 - aAbs * aAbs));
            float twist = 0.80 + 0.20 * sin(u * 65.0 + across * 5.0);
            rgb *= (0.48 + 0.60 * cyl) * twist;
            alpha *= 1.0 - smoothstep(0.78, 1.0, aAbs);
        }
    }
    else if (tag < 5.8)
    {
        // Tall Lakeside Reeds (5.0) + velvet brown cattail spike (5.5)
        float isCattail = (tag > 5.25 && u > 0.70 && u < 0.91) ? 1.0 : 0.0;
        float halfW = (isCattail > 0.5) ? 0.92 : 0.38;
        if (aAbs > halfW) discard;
        float ratio = aAbs / halfW;
        float cyl = sqrt(max(0.0, 1.0 - ratio * ratio));
        float3 velvet = float3(0.30, 0.14, 0.06) * (0.65 + 0.55 * cyl);
        rgb = lerp(rgb * (0.55 + 0.55 * cyl), velvet, isCattail);
        alpha *= 1.0 - smoothstep(halfW * 0.75, halfW, aAbs);
    }
    else if (tag < 6.5)
    {
        // Lush Woodland Fern: feathered pinnate compound leaflets + glowing central rachis vein
        float vein = 1.0 - smoothstep(0.04, 0.11, aAbs);
        float leaflet = sin(u * 46.0 + aAbs * 6.0);
        float tipEnv = sin(clamp(u, 0.0, 1.0) * 3.14159265);
        float bladeEdge = (0.35 + 0.65 * smoothstep(-0.35, 0.55, leaflet)) * max(tipEnv, 0.12);
        float mask = max(vein, 1.0 - smoothstep(bladeEdge - 0.08, bladeEdge + 0.04, aAbs));
        if (mask < 0.06) discard;
        rgb = lerp(rgb * (0.72 + 0.35 * (1.0 - aAbs)), rgb * 1.45 + float3(0.18, 0.24, 0.08) * glow, vein);
        alpha *= mask;
    }
    else if (tag < 7.5)
    {
        // Underwater Giant Kelp: stipe + ruffled side blades + glowing golden gas bladders
        float stipe = 1.0 - smoothstep(0.10, 0.19, aAbs);
        float ruffle = 0.58 + 0.38 * sin(u * 26.0 + seed * 12.0 + simTime * 1.4) * sin(u * 3.14159);
        float blade = 1.0 - smoothstep(ruffle - 0.10, ruffle + 0.04, aAbs);
        float bOff = frac(u * 9.0 + seed) - 0.5;
        float bladder = exp(-(bOff * bOff) * 70.0) * (1.0 - smoothstep(0.12, 0.30, aAbs));
        float mask = max(stipe, blade * 0.85);
        if (mask < 0.06) discard;
        rgb = lerp(rgb * (0.65 + 0.35 * (1.0 - aAbs)), float3(0.95, 0.78, 0.28) * (1.0 + glow * 0.4), clamp(bladder, 0.0, 1.0));
        alpha *= mask * 0.92;
    }
    else if (tag < 8.5)
    {
        // Bioluminescent Sea Anemone Tentacles: translucent stalk + glowing neon tip
        float cyl = sqrt(max(0.0, 1.0 - aAbs * aAbs));
        float tipGlow = smoothstep(0.62, 0.98, u) * (0.65 + glow);
        rgb = rgb * (0.48 + 0.58 * cyl) + lerp(rgb, float3(0.75, 0.96, 1.0), 0.55) * tipGlow;
        alpha *= (1.0 - smoothstep(0.72, 1.0, aAbs)) * (0.75 + 0.25 * tipGlow);
    }
    else
    {
        // Electrical Arcs (Tesla Sphere, Static Shock, Plasma Cage, Storm Web): white-hot core + vibrant corona
        float core = exp(-aAbs * aAbs * 14.0);
        float corona = exp(-aAbs * aAbs * 2.4) * (0.55 + glow * 0.55);
        float flicker = 0.82 + 0.18 * sin(simTime * 42.0 + seed * 31.0 + u * 12.0);
        rgb = (rgb * corona + float3(1.0, 0.98, 1.0) * core * 1.65) * flicker;
        alpha *= clamp(core + corona * 0.65, 0.0, 1.0);
    }

    if (alpha <= 0.005) discard;
    return float4(rgb, alpha);
}
