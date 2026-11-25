/*============================================================================================================================================
                                                      PARTICLEXPBDCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Stage F (S26) Zero-Atomic Conflict-Colored GPU Orthotropic XPBD Compute (Distance, Seam, Warp/Weft/Shear, Quadratic Hinge, Rigid Contacts)
// Reference: Experimental/ClothEditor/Engine/WebGpuClothAccelerator.js L201-L435, L609-L653

//------------------------------------------------------------------------------------------------------------------------
//                                                    ABI STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

/// Exact 128-byte uniform block matching Frontier::ParticleSpace::ParticleXpbdUniformBlock.
struct XpbdUniformBlock
{
    float4 TimeControl;     // x = dt, y = invDt, z = invDt2, w = zipAlpha
    float4 GravityDampWind; // x = gravityY, y = damping, z = windX, w = windZ
    float4 MembraneModuli;  // x = warpModulus, y = weftModulus, z = shearModulus, w = wallModulus
    float4 CrimpCompShear;  // x = warpCrimp, y = weftCrimp, z = compRatio, w = maxShearS
    float4 BendFriction;    // x = bendStiff, y = frictionMu, z = groundY, w = maxSpeed
    uint4  CountsBatch;     // x = pointCount, y = batchStart, z = batchEnd, w = proxyCount
    uint4  DualOffsets;     // x = faceCount, y = hingeDualOff, z = seamDualOff, w = distDualOff
    float4 WindAeroFlags;   // x = windEnabled (0/1), y = aeroDragRate, z = contactRadius, w = windY
};

/// Exact 80-byte Jolt RigidColliderProxy mirror (BroadphaseSolver.h).
struct RigidColliderProxyGpu
{
    float3 CenterMeters;
    uint   ShapeCode;
    float3 VelocityMps;
    float  RadiusMeters;
    float3 HalfExtentsMeters;
    float  Restitution;
    float4 OrientationQuat;
    float  Friction;
    uint   BodySlotIndex;
    uint   ChannelMask;
    uint   DynamicResponseBit;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    GPU BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]] ConstantBuffer<XpbdUniformBlock>        U;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4>              PositionsInvMass;  // xyz = position (m), w = invMass (0 = pinned)
[[vk::binding(2, 0)]] RWStructuredBuffer<float4>              PrevPositions;     // xyz = prevPos (m), w = pad
[[vk::binding(3, 0)]] RWStructuredBuffer<float4>              Velocities;        // xyz = velocity (m/s), w = pad
[[vk::binding(4, 0)]] StructuredBuffer<uint>                  Topology;          // Packed U32Topology (24-word header)
[[vk::binding(5, 0)]] StructuredBuffer<float>                 RestTables;        // Packed F32Rest
[[vk::binding(6, 0)]] RWStructuredBuffer<float>               Duals;             // Persistent XPBD Lagrange multipliers
[[vk::binding(7, 0)]] StructuredBuffer<RigidColliderProxyGpu> RigidProxies;      // Live Jolt 80-byte colliders

//------------------------------------------------------------------------------------------------------------------------
//                                               RIGID PROXY SDF HELPER
//------------------------------------------------------------------------------------------------------------------------

float3 RotateByQuat(float4 q, float3 v)
{
    float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

float3 RotateByConjugateQuat(float4 q, float3 v)
{
    return RotateByQuat(float4(-q.xyz, q.w), v);
}

bool QueryRigidProxySdf(RigidColliderProxyGpu proxy, float3 worldPos, float contactRadius, out float3 outNormal, out float outPen)
{
    outNormal = float3(0.0, 1.0, 0.0);
    outPen    = 0.0;
    float4 q = proxy.OrientationQuat;
    float3 rel = RotateByConjugateQuat(q, worldPos - proxy.CenterMeters);
    float3 localN = float3(0.0, 1.0, 0.0);
    float  pen    = 0.0;
    bool   hit    = false;

    if (proxy.ShapeCode == 0u)
    {
        float minD = max(proxy.RadiusMeters, 1.0e-4) + contactRadius;
        float d2 = dot(rel, rel);
        if (d2 < minD * minD)
        {
            float d = sqrt(max(d2, 1.0e-12));
            localN = (d > 1.0e-5) ? (rel / d) : float3(0.0, 1.0, 0.0);
            pen = minD - d;
            hit = true;
        }
    }
    else if (proxy.ShapeCode == 1u)
    {
        float3 ext = max(proxy.HalfExtentsMeters, float3(1.0e-4, 1.0e-4, 1.0e-4));
        float3 cl  = clamp(rel, -ext, ext);
        float3 df  = rel - cl;
        float  d2  = dot(df, df);
        if (d2 > 1.0e-10)
        {
            if (d2 < contactRadius * contactRadius)
            {
                float d = sqrt(d2);
                localN = df / d;
                pen = contactRadius - d;
                hit = true;
            }
        }
        else
        {
            float3 depth = ext - abs(rel);
            if (depth.x <= depth.y && depth.x <= depth.z) { localN = float3(rel.x >= 0.0 ? 1.0 : -1.0, 0.0, 0.0); pen = depth.x + contactRadius; }
            else if (depth.y <= depth.z)                  { localN = float3(0.0, rel.y >= 0.0 ? 1.0 : -1.0, 0.0); pen = depth.y + contactRadius; }
            else                                          { localN = float3(0.0, 0.0, rel.z >= 0.0 ? 1.0 : -1.0); pen = depth.z + contactRadius; }
            hit = true;
        }
    }
    else
    {
        float halfH = max(proxy.HalfExtentsMeters.z, 0.0);
        float minD  = max(proxy.RadiusMeters, 1.0e-4) + contactRadius;
        float3 seg  = float3(0.0, 0.0, clamp(rel.z, -halfH, halfH));
        float3 df   = rel - seg;
        float  d2   = dot(df, df);
        if (d2 < minD * minD)
        {
            float d = sqrt(max(d2, 1.0e-12));
            localN = (d > 1.0e-5) ? (df / d) : float3(0.0, 0.0, 1.0);
            pen = minD - d;
            hit = true;
        }
    }

    if (!hit) return false;
    outNormal = RotateByQuat(q, localN);
    outPen    = pen;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                       ENTRY 1 & 2: PREDICT INERTIA & RESET DUALS
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void PredictInertiaComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint i = dispatchThreadId.x;
    if (i >= U.CountsBatch.x) return;

    float4 pm = PositionsInvMass[i];
    PrevPositions[i] = float4(pm.xyz, 0.0);

    if (pm.w <= 0.0)
    {
        Velocities[i] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float dt = U.TimeControl.x;
    float3 v = Velocities[i].xyz * U.GravityDampWind.y;
    v.y += U.GravityDampWind.x * dt;

    if (U.WindAeroFlags.x > 0.5)
    {
        float3 windV = float3(U.GravityDampWind.z, U.WindAeroFlags.w, U.GravityDampWind.w);
        v += (windV - v) * (U.WindAeroFlags.y * dt);
    }

    Velocities[i]    = float4(v, 0.0);
    PositionsInvMass[i] = float4(pm.xyz + v * dt, pm.w);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ResetDualsComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint i = dispatchThreadId.x;
    uint totalDuals = U.DualOffsets.w + Topology[21];
    if (i < totalDuals)
    {
        Duals[i] = 0.0;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                  ENTRY 3: CONFLICT-COLORED DISTANCE / STRAND BATCH
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void SolveDistanceBatchComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint idx = U.CountsBatch.y + dispatchThreadId.x;
    if (idx >= U.CountsBatch.z) return;

    uint offDBat = Topology[7];
    uint offDIdx = Topology[8];
    uint offDLen = Topology[22];
    uint offDStf = Topology[23];

    uint e = Topology[offDBat + idx];
    uint a = Topology[offDIdx + e * 2u];
    uint b = Topology[offDIdx + e * 2u + 1u];
    float4 pA = PositionsInvMass[a];
    float4 pB = PositionsInvMass[b];
    float wSum = pA.w + pB.w;
    if (wSum <= 0.0) return;

    float3 dVec = pA.xyz - pB.xyz;
    float d = length(dVec);
    if (d < 1.0e-8) return;

    float C      = d - RestTables[offDLen + e];
    float aTilde = U.TimeControl.z / max(1.0e-4, RestTables[offDStf + e]);
    uint  dIdx   = U.DualOffsets.w + e;
    float dLam   = (-C - aTilde * Duals[dIdx]) / (wSum + aTilde);
    Duals[dIdx]  = Duals[dIdx] + dLam;
    float3 corr  = dVec * (dLam / d);

    if (pA.w > 0.0) PositionsInvMass[a] = float4(pA.xyz + pA.w * corr, pA.w);
    if (pB.w > 0.0) PositionsInvMass[b] = float4(pB.xyz - pB.w * corr, pB.w);
}

//------------------------------------------------------------------------------------------------------------------------
//                                   ENTRY 4: CONFLICT-COLORED SEAM STITCH BATCH
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void SolveSeamBatchComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint idx = U.CountsBatch.y + dispatchThreadId.x;
    if (idx >= U.CountsBatch.z) return;

    uint offSBat  = Topology[3];
    uint offSIdx  = Topology[6];
    uint offSGap0 = Topology[17];

    uint s = Topology[offSBat + idx];
    uint a = Topology[offSIdx + s * 2u];
    uint b = Topology[offSIdx + s * 2u + 1u];
    float4 pA = PositionsInvMass[a];
    float4 pB = PositionsInvMass[b];
    float wSum = pA.w + pB.w;
    if (wSum <= 0.0) return;

    float3 dVec = pA.xyz - pB.xyz;
    float d = length(dVec);
    if (d < 1.0e-8) return;

    float target = (1.0 - U.TimeControl.w) * min(RestTables[offSGap0 + s], d);
    float C      = d - target;
    float aTilde = U.TimeControl.z / 1400.0;
    uint  dIdx   = U.DualOffsets.z + s;
    float dLam   = (-C - aTilde * Duals[dIdx]) / (wSum + aTilde);
    Duals[dIdx]  = Duals[dIdx] + dLam;
    float3 corr  = dVec * (dLam / d);

    if (pA.w > 0.0) PositionsInvMass[a] = float4(pA.xyz + pA.w * corr, pA.w);
    if (pB.w > 0.0) PositionsInvMass[b] = float4(pB.xyz - pB.w * corr, pB.w);
}

//------------------------------------------------------------------------------------------------------------------------
//                            ENTRY 5: CONFLICT-COLORED ORTHOTROPIC FACE BATCH (WARP/WEFT/SHEAR)
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void SolveFaceBatchComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint idx = U.CountsBatch.y + dispatchThreadId.x;
    if (idx >= U.CountsBatch.z) return;

    uint offFBat = Topology[1];
    uint offFIdx = Topology[4];
    uint offDmIn = Topology[13];
    uint offFAre = Topology[14];

    uint f  = Topology[offFBat + idx];
    uint ia = Topology[offFIdx + f * 3u];
    uint ib = Topology[offFIdx + f * 3u + 1u];
    uint ic = Topology[offFIdx + f * 3u + 2u];
    float4 pmA = PositionsInvMass[ia];
    float4 pmB = PositionsInvMass[ib];
    float4 pmC = PositionsInvMass[ic];
    float wa = pmA.w, wb = pmB.w, wc = pmC.w;
    if (wa + wb + wc <= 0.0) return;

    float q00 = RestTables[offDmIn + f * 4u];
    float q01 = RestTables[offDmIn + f * 4u + 1u];
    float q10 = RestTables[offDmIn + f * 4u + 2u];
    float q11 = RestTables[offDmIn + f * 4u + 3u];
    float area = RestTables[offFAre + f];

    float cuA = -(q00 + q10), cuB = q00, cuC = q10;
    float cvA = -(q01 + q11), cvB = q01, cvC = q11;
    uint  d3  = f * 3u;

    float3 pA = pmA.xyz, pB = pmB.xyz, pC = pmC.xyz;

    // 1. Warp
    float3 fu = (pB - pA) * q00 + (pC - pA) * q10;
    float  lu = length(fu);
    if (lu > 1.0e-6)
    {
        float wG = wa * cuA * cuA + wb * cuB * cuB + wc * cuC * cuC;
        if (wG > 1.0e-12)
        {
            float epsU = lu - 1.0;
            float d2Psi = U.MembraneModuli.x * area;
            if (epsU < 0.0) d2Psi *= U.CrimpCompShear.z;
            else if (epsU > U.CrimpCompShear.x)
            {
                float ov = epsU - U.CrimpCompShear.x;
                d2Psi += 3.0 * U.MembraneModuli.w * area * ov * ov;
            }
            float aTilde = U.TimeControl.z / max(1.0e-4, d2Psi);
            float dLam   = (-epsU - aTilde * Duals[d3]) / (wG + aTilde);
            Duals[d3]    = Duals[d3] + dLam;
            float3 n     = fu / lu;
            pA += (wa * cuA * dLam) * n;
            pB += (wb * cuB * dLam) * n;
            pC += (wc * cuC * dLam) * n;
        }
    }

    // 2. Weft
    float3 fv = (pB - pA) * q01 + (pC - pA) * q11;
    float  lv = length(fv);
    if (lv > 1.0e-6)
    {
        float wG = wa * cvA * cvA + wb * cvB * cvB + wc * cvC * cvC;
        if (wG > 1.0e-12)
        {
            float epsV = lv - 1.0;
            float d2Psi = U.MembraneModuli.y * area;
            if (epsV < 0.0) d2Psi *= U.CrimpCompShear.z;
            else if (epsV > U.CrimpCompShear.y)
            {
                float ov = epsV - U.CrimpCompShear.y;
                d2Psi += 3.0 * U.MembraneModuli.w * area * ov * ov;
            }
            float aTilde = U.TimeControl.z / max(1.0e-4, d2Psi);
            float dLam   = (-epsV - aTilde * Duals[d3 + 1u]) / (wG + aTilde);
            Duals[d3 + 1u] = Duals[d3 + 1u] + dLam;
            float3 n     = fv / lv;
            pA += (wa * cvA * dLam) * n;
            pB += (wb * cvB * dLam) * n;
            pC += (wc * cvC * dLam) * n;
        }
    }

    // 3. Shear (weave locking angle)
    float3 fu2 = (pB - pA) * q00 + (pC - pA) * q10;
    float3 fv2 = (pB - pA) * q01 + (pC - pA) * q11;
    float  lu2 = length(fu2);
    float  lv2 = length(fv2);
    if (lu2 > 1.0e-6 && lv2 > 1.0e-6)
    {
        float  invN = 1.0 / (lu2 * lv2);
        float3 ga   = (cuA * fv2 + cvA * fu2) * invN;
        float3 gb   = (cuB * fv2 + cvB * fu2) * invN;
        float3 gc   = (cuC * fv2 + cvC * fu2) * invN;
        float  wG   = wa * dot(ga, ga) + wb * dot(gb, gb) + wc * dot(gc, gc);
        if (wG > 1.0e-12)
        {
            float cs = dot(fu2, fv2) * invN;
            float kS = U.MembraneModuli.z * area;
            if (abs(cs) > U.CrimpCompShear.w)
            {
                float ov = abs(cs) - U.CrimpCompShear.w;
                kS += U.MembraneModuli.w * area * 0.35 * (1.0 + 18.0 * ov * ov);
            }
            float aTilde = U.TimeControl.z / max(1.0e-4, kS);
            float dLam   = (-cs - aTilde * Duals[d3 + 2u]) / (wG + aTilde);
            Duals[d3 + 2u] = Duals[d3 + 2u] + dLam;
            pA += (wa * dLam) * ga;
            pB += (wb * dLam) * gb;
            pC += (wc * dLam) * gc;
        }
    }

    PositionsInvMass[ia] = float4(pA, wa);
    PositionsInvMass[ib] = float4(pB, wb);
    PositionsInvMass[ic] = float4(pC, wc);
}

//------------------------------------------------------------------------------------------------------------------------
//                                 ENTRY 6: CONFLICT-COLORED QUADRATIC HINGE BATCH
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void SolveHingeBatchComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint idx = U.CountsBatch.y + dispatchThreadId.x;
    if (idx >= U.CountsBatch.z) return;

    uint offHBat = Topology[2];
    uint offHIdx = Topology[5];
    uint offHW   = Topology[15];
    uint offHAre = Topology[16];

    uint hi = Topology[offHBat + idx];
    uint hb = hi * 4u;
    uint i0 = Topology[offHIdx + hb];
    uint i1 = Topology[offHIdx + hb + 1u];
    uint i2 = Topology[offHIdx + hb + 2u];
    uint i3 = Topology[offHIdx + hb + 3u];
    float4 pm0 = PositionsInvMass[i0];
    float4 pm1 = PositionsInvMass[i1];
    float4 pm2 = PositionsInvMass[i2];
    float4 pm3 = PositionsInvMass[i3];
    float c0 = RestTables[offHW + hb];
    float c1 = RestTables[offHW + hb + 1u];
    float c2 = RestTables[offHW + hb + 2u];
    float c3 = RestTables[offHW + hb + 3u];

    float denomBase = pm0.w * c0 * c0 + pm1.w * c1 * c1 + pm2.w * c2 * c2 + pm3.w * c3 * c3;
    if (denomBase <= 1.0e-12) return;

    float3 bVec = c0 * pm0.xyz + c1 * pm1.xyz + c2 * pm2.xyz + c3 * pm3.xyz;
    float  bLen = length(bVec);
    if (bLen < 1.0e-9) return;

    float aTilde = U.TimeControl.z / max(1.0e-5, U.BendFriction.x * RestTables[offHAre + hi]);
    uint  dIdx   = U.DualOffsets.y + hi;
    float dLam   = (-bLen - aTilde * Duals[dIdx]) / (denomBase + aTilde);
    Duals[dIdx]  = Duals[dIdx] + dLam;
    float3 n     = bVec * (dLam / bLen);

    if (pm0.w > 0.0) PositionsInvMass[i0] = float4(pm0.xyz + (pm0.w * c0) * n, pm0.w);
    if (pm1.w > 0.0) PositionsInvMass[i1] = float4(pm1.xyz + (pm1.w * c1) * n, pm1.w);
    if (pm2.w > 0.0) PositionsInvMass[i2] = float4(pm2.xyz + (pm2.w * c2) * n, pm2.w);
    if (pm3.w > 0.0) PositionsInvMass[i3] = float4(pm3.xyz + (pm3.w * c3) * n, pm3.w);
}

//------------------------------------------------------------------------------------------------------------------------
//                           ENTRY 7: FINALIZE SUBSTEP + UNILATERAL RIGID PROXY CONTACT
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void FinalizeSubstepComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint i = dispatchThreadId.x;
    if (i >= U.CountsBatch.x) return;

    float4 pm   = PositionsInvMass[i];
    float3 pPre = pm.xyz;
    float3 p    = pPre;
    if (p.y < U.BendFriction.z) p.y = U.BendFriction.z;

    if (pm.w <= 0.0)
    {
        PositionsInvMass[i] = float4(p, pm.w);
        Velocities[i]       = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float3 xPrev = PrevPositions[i].xyz;
    uint   proxyCount = U.CountsBatch.w;
    float  contactR   = U.WindAeroFlags.z;
    float  mu         = U.BendFriction.y;

    for (uint pi = 0u; pi < proxyCount; ++pi)
    {
        float3 n;
        float pen;
        if (QueryRigidProxySdf(RigidProxies[pi], p, contactR, n, pen))
        {
            p += n * pen;
            float3 disp = p - xPrev;
            float3 tang = disp - dot(disp, n) * n;
            float  tLen = length(tang);
            if (tLen > 1.0e-7)
            {
                float slip = min(1.0, (mu * pen) / tLen);
                p -= tang * slip;
            }
        }
    }

    PositionsInvMass[i] = float4(p, pm.w);

    float  invDt = U.TimeControl.y;
    float3 v     = (p - xPrev) * invDt;
    float3 colDelta = p - pPre;
    float  cLen     = length(colDelta);
    if (cLen > 1.0e-6)
    {
        float3 cn = colDelta / cLen;
        float  vn = dot(v, cn);
        float  sv = min(vn, cLen * invDt);
        if (sv > 0.0) v -= cn * sv;
    }

    float spd = length(v);
    if (spd > U.BendFriction.w)
    {
        v *= (U.BendFriction.w / spd);
    }
    Velocities[i] = float4(v, 0.0);
}
