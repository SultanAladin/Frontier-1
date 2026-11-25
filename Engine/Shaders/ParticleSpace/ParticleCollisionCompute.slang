/*============================================================================================================================================
                                                    PARTICLECOLLISIONCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Stage F (Slice S24) GPU particle collision solver against analytic spheres/ground + 80-byte Jolt RigidColliderProxy SSBO
//    (Sphere, OrientedBox, Capsule +Z, baked CompoundHull 3D SDF) with surface velocity, restitution, Coulomb friction & two-way atomic impulse readback
// Reference: BroadphaseSolver.{h,cpp} (80-byte RigidColliderProxy) & Experimental/FluidEditor/Engine/FluidShaders.js ColliderChunk (L494-L514)

//------------------------------------------------------------------------------------------------------------------------
//                                                    ABI STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleStateRecord
{
    float4 PositionAge;  // xyz = position (m), w = age (s)
    float4 VelocitySeed; // xyz = velocity (m/s), w = seed [0..1]
};

/// Exact 80-byte mirror of Frontier::SimulationSpace::PhysicsSpace::RigidColliderProxy.
struct RigidColliderProxyGpu
{
    float3 CenterMeters;
    uint   ShapeCode;          // 0=Sphere, 1=OrientedBox, 2=Capsule(+Z), 3=CompoundHull(3D SDF)
    float3 VelocityMps;
    float  RadiusMeters;
    float3 HalfExtentsMeters;
    float  Restitution;
    float4 OrientationQuat;    // (x, y, z, w)
    float  Friction;
    uint   BodySlotIndex;
    uint   ChannelMask;
    uint   DynamicResponseBit; // 1 = accumulate equal-and-opposite reaction impulse
};

/// Exact 48-byte mirror of CompoundHullSdfHeaderGpu (local-space 3D signed-distance grid).
struct CompoundHullSdfHeaderGpu
{
    float4 BoundsMinCellSize; // xyz = local AABB min (m), w = voxel cell size (m)
    uint4  GridDimOffset;     // xyz = voxel counts (nx, ny, nz), w = float offset into HullSdfVoxels
    uint4  BodySlotPad;       // x = matching BodySlotIndex, yzw = reserved
};

/// Exact 48-byte fixed-point atomic impulse accumulator per RigidColliderProxy slot (1 unit = 1 / FixedScale N*s).
struct RigidImpulseAtomicGpu
{
    int    LinearImpulseFixed[3];
    uint   ContactCount;
    int    AngularImpulseFixed[3];
    uint   BodySlotIndex;
    int    ContactPointSumFixed[3];
    uint   ReservedPad;
};

/// Exact 128-byte collision uniform block.
struct ParticleCollisionUniformBlock
{
    float4 AnalyticColliders[4];   // xyz = sphere centre (m), w = radius (m) (ColliderChunk parity)
    float4 GroundFlags;            // x = groundHeight (m), y = groundEnabled (0/1), z = analyticCount, w = particleCount
    float4 ParticleMaterial;       // x = particleRadius (m), y = particleMass (kg), z = defaultRestitution, w = defaultFriction
    uint4  CountsMasks;            // x = proxyCount, y = hullCount, z = channelMask, w = twoWayEnabled (0/1)
    float4 ScaleLifetime;          // x = fixedPointScale (100000.0), y = lifetime (0 = Cloner), z = maxImpulseClamp, w = pad
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    GPU BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]] ConstantBuffer<ParticleCollisionUniformBlock> U;
[[vk::binding(1, 0)]] RWStructuredBuffer<ParticleStateRecord>       ParticleStates;
[[vk::binding(2, 0)]] StructuredBuffer<RigidColliderProxyGpu>       RigidProxies;
[[vk::binding(3, 0)]] StructuredBuffer<float4>                      ProxyAngularVelocities; // xyz = omega (rad/s)
[[vk::binding(4, 0)]] StructuredBuffer<CompoundHullSdfHeaderGpu>    HullHeaders;
[[vk::binding(5, 0)]] StructuredBuffer<float>                       HullSdfVoxels;
[[vk::binding(6, 0)]] RWStructuredBuffer<RigidImpulseAtomicGpu>     ImpulseAccumulators;

//------------------------------------------------------------------------------------------------------------------------
//                                               QUATERNION & SDF HELPERS
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

float FetchHullVoxel(CompoundHullSdfHeaderGpu hdr, int3 c)
{
    int3 clamped = clamp(c, int3(0, 0, 0), int3(hdr.GridDimOffset.xyz) - int3(1, 1, 1));
    uint idx = hdr.GridDimOffset.w
             + uint(clamped.z) * (hdr.GridDimOffset.x * hdr.GridDimOffset.y)
             + uint(clamped.y) * hdr.GridDimOffset.x
             + uint(clamped.x);
    return HullSdfVoxels[idx];
}

float SampleHullSdfTrilinear(CompoundHullSdfHeaderGpu hdr, float3 localPos)
{
    float h = max(hdr.BoundsMinCellSize.w, 1.0e-5);
    float3 coord = (localPos - hdr.BoundsMinCellSize.xyz) / h - 0.5;
    float3 maxC  = float3(hdr.GridDimOffset.xyz) - 1.0001;
    coord = clamp(coord, float3(0.0, 0.0, 0.0), max(maxC, float3(0.0, 0.0, 0.0)));
    int3 i0 = int3(floor(coord));
    float3 f = coord - float3(i0);

    float c000 = FetchHullVoxel(hdr, i0 + int3(0, 0, 0));
    float c100 = FetchHullVoxel(hdr, i0 + int3(1, 0, 0));
    float c010 = FetchHullVoxel(hdr, i0 + int3(0, 1, 0));
    float c110 = FetchHullVoxel(hdr, i0 + int3(1, 1, 0));
    float c001 = FetchHullVoxel(hdr, i0 + int3(0, 0, 1));
    float c101 = FetchHullVoxel(hdr, i0 + int3(1, 0, 1));
    float c011 = FetchHullVoxel(hdr, i0 + int3(0, 1, 1));
    float c111 = FetchHullVoxel(hdr, i0 + int3(1, 1, 1));

    float c00 = lerp(c000, c100, f.x);
    float c10 = lerp(c010, c110, f.x);
    float c01 = lerp(c001, c101, f.x);
    float c11 = lerp(c011, c111, f.x);
    return lerp(lerp(c00, c10, f.y), lerp(c01, c11, f.y), f.z);
}

bool EvaluateCompoundHullLocal(
    uint bodySlot,
    float3 relLocal,
    float particleRadius,
    out float3 localNormal,
    out float3 localClosest,
    out float penetration)
{
    localNormal  = float3(0.0, 0.0, 1.0);
    localClosest = float3(0.0, 0.0, 0.0);
    penetration  = 0.0;

    uint hullCount = U.CountsMasks.y;
    for (uint hIdx = 0u; hIdx < hullCount; ++hIdx)
    {
        CompoundHullSdfHeaderGpu hdr = HullHeaders[hIdx];
        if (hdr.BodySlotPad.x != bodySlot) continue;

        float h = max(hdr.BoundsMinCellSize.w, 1.0e-5);
        float3 bMin = hdr.BoundsMinCellSize.xyz;
        float3 bMax = bMin + float3(hdr.GridDimOffset.xyz) * h;
        if (any(relLocal < bMin - particleRadius) || any(relLocal > bMax + particleRadius))
        {
            return false;
        }

        float sdf = SampleHullSdfTrilinear(hdr, relLocal);
        if (sdf >= particleRadius) return false;

        float eps = h * 0.5;
        float gx = SampleHullSdfTrilinear(hdr, relLocal + float3(eps, 0.0, 0.0))
                 - SampleHullSdfTrilinear(hdr, relLocal - float3(eps, 0.0, 0.0));
        float gy = SampleHullSdfTrilinear(hdr, relLocal + float3(0.0, eps, 0.0))
                 - SampleHullSdfTrilinear(hdr, relLocal - float3(0.0, eps, 0.0));
        float gz = SampleHullSdfTrilinear(hdr, relLocal + float3(0.0, 0.0, eps))
                 - SampleHullSdfTrilinear(hdr, relLocal - float3(0.0, 0.0, eps));
        float3 grad = float3(gx, gy, gz);
        float gLen  = length(grad);
        localNormal  = (gLen > 1.0e-6) ? (grad / gLen) : float3(0.0, 0.0, 1.0);
        penetration  = particleRadius - sdf;
        localClosest = relLocal - localNormal * sdf;
        return true;
    }
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    COMPUTE ENTRY
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint idx = dispatchThreadId.x;
    uint particleCount = uint(U.GroundFlags.w);
    if (idx >= particleCount) return;

    ParticleStateRecord state = ParticleStates[idx];
    float lifetime = U.ScaleLifetime.y;
    if (lifetime > 0.0 && state.PositionAge.w >= lifetime) return;

    float3 pos = state.PositionAge.xyz;
    float3 vel = state.VelocitySeed.xyz;
    float pRadius = max(U.ParticleMaterial.x, 1.0e-4);
    float pMass   = max(U.ParticleMaterial.y, 1.0e-5);
    float defRest = clamp(U.ParticleMaterial.z, 0.0, 1.0);
    float defFric = clamp(U.ParticleMaterial.w, 0.0, 2.0);

    // 1. Analytic Ground (ColliderChunk / Bounce layer parity)
    if (U.GroundFlags.y > 0.5)
    {
        float groundY = U.GroundFlags.x + pRadius;
        if (pos.y < groundY)
        {
            pos.y = groundY;
            if (vel.y < 0.0)
            {
                float normalDelta = -(1.0 + defRest) * vel.y;
                vel.y += normalDelta;
                float2 tanV = float2(vel.x, vel.z);
                float tanLen = length(tanV);
                if (tanLen > 1.0e-6 && defFric > 0.0)
                {
                    float maxF = min(tanLen, defFric * normalDelta);
                    tanV -= (tanV / tanLen) * maxF;
                    vel.x = tanV.x;
                    vel.z = tanV.y;
                }
            }
        }
    }

    // 2. Analytic 4 Spheres (exact FluidShaders.js ColliderChunk parity)
    uint analyticCount = min(uint(U.GroundFlags.z), 4u);
    for (uint s = 0u; s < analyticCount; ++s)
    {
        float4 sph = U.AnalyticColliders[s];
        float minD = max(sph.w, 1.0e-4) + pRadius;
        float3 d = pos - sph.xyz;
        float distSq = dot(d, d);
        if (distSq < minD * minD)
        {
            float dist = sqrt(max(distSq, 1.0e-12));
            float3 n = (dist > 1.0e-5) ? (d / dist) : float3(0.0, 1.0, 0.0);
            pos = sph.xyz + n * minD;
            float vn = dot(vel, n);
            if (vn < 0.0)
            {
                float normalDelta = -(1.0 + defRest) * vn;
                vel += n * normalDelta;
                float3 tanV = vel - n * dot(vel, n);
                float tanLen = length(tanV);
                if (tanLen > 1.0e-6 && defFric > 0.0)
                {
                    vel -= (tanV / tanLen) * min(tanLen, defFric * normalDelta);
                }
            }
        }
    }

    // 3. Live Jolt 80-byte RigidColliderProxy SSBO (Sphere, OrientedBox, Capsule +Z, CompoundHull 3D SDF)
    uint proxyCount  = U.CountsMasks.x;
    uint channelMask = U.CountsMasks.z;
    bool twoWay      = (U.CountsMasks.w != 0u);
    float fixedScale = max(U.ScaleLifetime.x, 1.0);
    float maxImpulse = max(U.ScaleLifetime.z, 1.0);

    for (uint p = 0u; p < proxyCount; ++p)
    {
        RigidColliderProxyGpu proxy = RigidProxies[p];
        if ((proxy.ChannelMask & channelMask) == 0u) continue;

        float4 quat = proxy.OrientationQuat;
        float3 relWorld = pos - proxy.CenterMeters;
        float3 relLocal = RotateByConjugateQuat(quat, relWorld);

        float3 localNormal  = float3(0.0, 0.0, 1.0);
        float3 localClosest = float3(0.0, 0.0, 0.0);
        float  penetration  = 0.0;
        bool   hit          = false;

        if (proxy.ShapeCode == 0u)
        {
            float minD = max(proxy.RadiusMeters, 1.0e-4) + pRadius;
            float distSq = dot(relLocal, relLocal);
            if (distSq < minD * minD)
            {
                float dist = sqrt(max(distSq, 1.0e-12));
                localNormal  = (dist > 1.0e-5) ? (relLocal / dist) : float3(0.0, 0.0, 1.0);
                localClosest = localNormal * proxy.RadiusMeters;
                penetration  = minD - dist;
                hit          = true;
            }
        }
        else if (proxy.ShapeCode == 1u)
        {
            float3 ext = max(proxy.HalfExtentsMeters, float3(1.0e-4, 1.0e-4, 1.0e-4));
            localClosest = clamp(relLocal, -ext, ext);
            float3 diff = relLocal - localClosest;
            float distSq = dot(diff, diff);
            if (distSq > 1.0e-10)
            {
                if (distSq < pRadius * pRadius)
                {
                    float dist = sqrt(distSq);
                    localNormal = diff / dist;
                    penetration = pRadius - dist;
                    hit = true;
                }
            }
            else
            {
                float3 depth = ext - abs(relLocal);
                if (depth.x <= depth.y && depth.x <= depth.z)
                {
                    localNormal    = float3(relLocal.x >= 0.0 ? 1.0 : -1.0, 0.0, 0.0);
                    localClosest.x = localNormal.x * ext.x;
                    penetration    = depth.x + pRadius;
                }
                else if (depth.y <= depth.z)
                {
                    localNormal    = float3(0.0, relLocal.y >= 0.0 ? 1.0 : -1.0, 0.0);
                    localClosest.y = localNormal.y * ext.y;
                    penetration    = depth.y + pRadius;
                }
                else
                {
                    localNormal    = float3(0.0, 0.0, relLocal.z >= 0.0 ? 1.0 : -1.0);
                    localClosest.z = localNormal.z * ext.z;
                    penetration    = depth.z + pRadius;
                }
                hit = true;
            }
        }
        else if (proxy.ShapeCode == 2u)
        {
            float halfH = max(proxy.HalfExtentsMeters.z, 0.0);
            float minD  = max(proxy.RadiusMeters, 1.0e-4) + pRadius;
            float3 segPt = float3(0.0, 0.0, clamp(relLocal.z, -halfH, halfH));
            float3 diff  = relLocal - segPt;
            float distSq = dot(diff, diff);
            if (distSq < minD * minD)
            {
                float dist = sqrt(max(distSq, 1.0e-12));
                localNormal  = (dist > 1.0e-5) ? (diff / dist) : float3(0.0, 0.0, 1.0);
                localClosest = segPt + localNormal * proxy.RadiusMeters;
                penetration  = minD - dist;
                hit          = true;
            }
        }
        else
        {
            hit = EvaluateCompoundHullLocal(proxy.BodySlotIndex, relLocal, pRadius, localNormal, localClosest, penetration);
        }

        if (!hit) continue;

        float3 worldNormal  = RotateByQuat(quat, localNormal);
        float3 leverArm     = RotateByQuat(quat, localClosest);
        float3 contactPoint = proxy.CenterMeters + leverArm;
        pos += worldNormal * penetration;

        // Surface velocity v_surf = v_body + omega_body x (contactPoint - center)
        float3 omega = ProxyAngularVelocities[p].xyz;
        float3 surfVel = proxy.VelocityMps + cross(omega, leverArm);
        float3 relVel = vel - surfVel;
        float  relVn  = dot(relVel, worldNormal);

        if (relVn < 0.0)
        {
            float rest = clamp(proxy.Restitution, 0.0, 1.0);
            float fric = clamp(proxy.Friction, 0.0, 2.0);
            float normalDelta = -(1.0 + rest) * relVn;
            float3 deltaV = worldNormal * normalDelta;

            float3 tanV = relVel - worldNormal * relVn;
            float tanLenSq = dot(tanV, tanV);
            if (tanLenSq > 1.0e-10 && fric > 0.0)
            {
                float tanLen = sqrt(tanLenSq);
                float maxF   = min(tanLen, fric * normalDelta);
                deltaV -= (tanV / tanLen) * maxF;
            }

            vel += deltaV;

            if (twoWay && proxy.DynamicResponseBit != 0u)
            {
                // Equal-and-opposite reaction impulse on the rigid body: J_body = -m_particle * deltaV
                float3 bodyLinJ = -deltaV * pMass;
                float jMag = length(bodyLinJ);
                if (jMag > maxImpulse)
                {
                    bodyLinJ *= (maxImpulse / jMag);
                }
                float3 bodyAngJ = cross(leverArm, bodyLinJ);

                InterlockedAdd(ImpulseAccumulators[p].LinearImpulseFixed[0], int(round(bodyLinJ.x * fixedScale)));
                InterlockedAdd(ImpulseAccumulators[p].LinearImpulseFixed[1], int(round(bodyLinJ.y * fixedScale)));
                InterlockedAdd(ImpulseAccumulators[p].LinearImpulseFixed[2], int(round(bodyLinJ.z * fixedScale)));
                InterlockedAdd(ImpulseAccumulators[p].AngularImpulseFixed[0], int(round(bodyAngJ.x * fixedScale)));
                InterlockedAdd(ImpulseAccumulators[p].AngularImpulseFixed[1], int(round(bodyAngJ.y * fixedScale)));
                InterlockedAdd(ImpulseAccumulators[p].AngularImpulseFixed[2], int(round(bodyAngJ.z * fixedScale)));
                InterlockedAdd(ImpulseAccumulators[p].ContactPointSumFixed[0], int(round(contactPoint.x * fixedScale)));
                InterlockedAdd(ImpulseAccumulators[p].ContactPointSumFixed[1], int(round(contactPoint.y * fixedScale)));
                InterlockedAdd(ImpulseAccumulators[p].ContactPointSumFixed[2], int(round(contactPoint.z * fixedScale)));
                InterlockedAdd(ImpulseAccumulators[p].ContactCount, 1u);
            }
        }
    }

    state.PositionAge.xyz  = pos;
    state.VelocitySeed.xyz = vel;
    ParticleStates[idx] = state;
}
