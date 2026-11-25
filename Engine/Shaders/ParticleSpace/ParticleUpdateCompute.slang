/*============================================================================================================================================
                                                      PARTICLEUPDATECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Pure .slang GPU compute update for ParticleSpace clouds (Stream/Cloner, Derez, Mocap, Effectors, Swarm, ForceFields & Gas Coupling)
// Reference: Experimental/FluidEditor/Engine/ParticleShaders.js L522-L752 (ParticleUpdateFragment)

import ForceFieldNoise;
import ForceModelEvaluation;
import ParticleShapeAnchor;

//------------------------------------------------------------------------------------------------------------------------
//                                                    DATA & UNIFORM BLOCKS
//------------------------------------------------------------------------------------------------------------------------

public struct ParticleStateRecord
{
    public float4 PositionAge;  // xyz = world position (m), w = age (s)
    public float4 VelocitySeed; // xyz = velocity (m/s), w = deterministic seed [0..1]
};

public struct ParticleUpdateUniformBlock
{
    public ParticleShapeUniformBlock Shape;
    public float4 GridCentreHalfX;      // xyz = gridCentre, w = gridHalf.x
    public float4 GridHalfYZTimeDt;     // x = gridHalf.y, y = gridHalf.z, z = simulationTime, w = deltaTime
    public float4 SpawnKinematics;      // x = envelope, y = rate, z = lifetime, w = speed
    public float4 LaunchForces;         // x = spread, y = radial, z = gravity, w = drag
    public float4 CouplingNoise;        // x = noise, y = fieldInfluence, z = gasInfluence, w = noiseScale
    public float4 SpringBounce;         // x = spring, y = damping, z = bounce, w = noiseSpeed
    public float4 DissolveSwarm;        // x = dissolve, y = linger, z = swarmStrength, w = swarmCohesion
    public float4 SwarmControlFlags;    // x = swarmOrbit, y = resetFlag (0/1), z = forceRecordCount, w = hasGasVolume (0/1)
};

[[vk::binding(0, 0)]] ConstantBuffer<ParticleUpdateUniformBlock> uParams;
[[vk::binding(1, 0)]] StructuredBuffer<ParticleStateRecord>      gReadStates;
[[vk::binding(2, 0)]] RWStructuredBuffer<ParticleStateRecord>    gWriteStates;
[[vk::binding(3, 0)]] RWStructuredBuffer<float4>                 gPreviousPositions;
[[vk::binding(4, 0)]] StructuredBuffer<float4>                   gAnchorTexels;
[[vk::binding(5, 0)]] StructuredBuffer<ForceModelGpuRecord>      gForceRecords;
[[vk::binding(6, 0)]] Texture3D<float4>                          gGasVelocityVolume;
[[vk::binding(7, 0)]] SamplerState                               gLinearClampSampler;

//------------------------------------------------------------------------------------------------------------------------
//                                                         HELPERS
//------------------------------------------------------------------------------------------------------------------------

float3 RandomDirection(float3 seed)
{
    float z = Hash31(seed) * 2.0 - 1.0;
    float a = Hash31(seed.zxy + 5.1) * 6.2831853;
    float r = sqrt(max(0.0, 1.0 - z * z));
    return float3(r * cos(a), z, r * sin(a));
}

float3 SampleGasVelocity(float3 worldPos, float3 gridCentre, float3 gridHalf, float hasGas)
{
    if (hasGas < 0.5) return float3(0.0, 0.0, 0.0);
    float3 uvw = (worldPos - (gridCentre - gridHalf)) / max(gridHalf * 2.0, float3(1.0e-4, 1.0e-4, 1.0e-4));
    if (any(uvw < 0.0) || any(uvw > 1.0)) return float3(0.0, 0.0, 0.0);
    return gGasVelocityVolume.SampleLevel(gLinearClampSampler, uvw, 0.0).xyz;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      COMPUTE ENTRY
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int side = max(1, int(uParams.Shape.AnchorLayout.z + 0.5));
    int capacity = side * side;
    int index = int(dispatchThreadId.x);
    if (index >= capacity) return;

    float2 pixel = float2(float(index % side), float(index / side));
    ParticleStateRecord prevState = gReadStates[index];
    gPreviousPositions[index] = prevState.PositionAge;

    float seed = Hash31(float3(pixel, 4.2));
    float simTime = uParams.GridHalfYZTimeDt.z;
    float dt      = uParams.GridHalfYZTimeDt.w;
    int mode      = int(uParams.Shape.ShapeCounts.w + 0.5);
    int count     = int(uParams.Shape.ShapeCounts.y + 0.5);
    float radius  = uParams.Shape.OriginRadius.w;
    float3 origin = uParams.Shape.OriginRadius.xyz;
    float3 axis   = uParams.Shape.AxisFill.xyz;
    float lifetime = uParams.SpawnKinematics.z;
    float linger   = uParams.DissolveSwarm.y;

    float3 limbVel;
    float3 restHome = ShapePointAtTime(uParams.Shape, gAnchorTexels, index, float3(0.0, 0.0, 0.0), true, simTime, limbVel);
    float3 home = restHome;

    float dissolve = uParams.DissolveSwarm.x;
    float noiseScale = uParams.CouplingNoise.w;
    float noiseSpeed = uParams.SpringBounce.w;
    if (dissolve > 0.0)
    {
        home += CurlNoise(home * noiseScale + float3(simTime * noiseSpeed, 2.7, -simTime * noiseSpeed * 0.6)) * dissolve;
    }

    int effMode = int(uParams.Shape.MocapKickEffector.y + 0.5);
    float effStrength = uParams.Shape.MocapKickEffector.z;
    float effPush = uParams.Shape.EffectorMotion.y;
    int effAxis = int(uParams.Shape.EffectorAxisDerez.x + 0.5);
    int anchorCount = int(uParams.Shape.AnchorLayout.y + 0.5);

    if (effMode > 0 && effStrength > 0.0001 && abs(effPush) > 0.0001)
    {
        float effW = EvaluateEffector(uParams.Shape, restHome, simTime);
        float3 outDir;
        if (effAxis == 1)
        {
            outDir = float3(0.0, 1.0, 0.0);
        }
        else if (effAxis == 2 && anchorCount > 0)
        {
            float3 aNorm;
            ObjectAnchorPosition(gAnchorTexels, anchorCount, index, aNorm);
            float3 tA, bA;
            ShapeBasis(axis, tA, bA);
            float3 worldNorm = tA * aNorm.x + axis * aNorm.y + bA * aNorm.z;
            outDir = length(worldNorm) > 1.0e-4 ? normalize(worldNorm) : axis;
        }
        else
        {
            outDir = restHome - origin;
            outDir = length(outDir) > 1.0e-4 ? normalize(outDir) : axis;
        }
        home += outDir * (effW * effPush);
    }

    DerezEvaluationResult derez = EvaluateDerez(uParams.Shape, restHome, simTime);
    int derezMode = int(uParams.Shape.EffectorAxisDerez.y + 0.5);
    float derezMix = uParams.Shape.EffectorAxisDerez.z;
    float derezShatter = uParams.Shape.DerezFrontParams.w;

    if (uParams.SwarmControlFlags.y > 0.5)
    {
        ParticleStateRecord outReset;
        if (mode == 1)
        {
            float3 startPos = home;
            if (derezMode == 2 && derezMix > 0.01 && derez.Released)
            {
                float3 scatter = RandomDirection(float3(pixel, 9.4)) * (radius * (1.2 + 0.9 * seed));
                startPos = origin + scatter + float3(0.0, abs(scatter.y) * 0.5, 0.0);
            }
            outReset.PositionAge  = float4(startPos, 0.0);
            outReset.VelocitySeed = float4(0.0, 0.0, 0.0, seed);
        }
        else
        {
            outReset.PositionAge  = float4(0.0, -1.0e4, 0.0, lifetime + 1.0);
            outReset.VelocitySeed = float4(0.0, 0.0, 0.0, seed);
        }
        gWriteStates[index] = outReset;
        gPreviousPositions[index] = outReset.PositionAge;
        return;
    }

    if (index >= count)
    {
        ParticleStateRecord parked;
        parked.PositionAge  = float4(0.0, -1.0e4, 0.0, lifetime + 1.0);
        parked.VelocitySeed = float4(0.0, 0.0, 0.0, seed);
        gWriteStates[index] = parked;
        return;
    }

    float3 position = prevState.PositionAge.xyz;
    float3 velocity = prevState.VelocitySeed.xyz;
    float age = prevState.PositionAge.w;
    float vSeed = prevState.VelocitySeed.w;

    float3 gridCentre = uParams.GridCentreHalfX.xyz;
    float3 gridHalf = float3(uParams.GridCentreHalfX.w, uParams.GridHalfYZTimeDt.x, uParams.GridHalfYZTimeDt.y);

    bool alive = (mode == 1) || (age < lifetime);
    bool lingering = !alive && (age < lifetime + linger);
    if (lingering) age += dt;

    if (!alive && !lingering)
    {
        float chance = uParams.SpawnKinematics.y * uParams.SpawnKinematics.x * dt;
        if (Hash31(float3(pixel, simTime)) < chance)
        {
            float3 rnd = float3(
                Hash31(float3(pixel, simTime + 1.3)),
                Hash31(float3(pixel.yx, simTime + 2.9)),
                Hash31(float3(pixel + 7.0, simTime + 4.1)));
            float3 spawnLimbVel;
            position = ShapePointAtTime(uParams.Shape, gAnchorTexels, index, rnd, false, simTime, spawnLimbVel);
            float3 random = RandomDirection(float3(pixel, simTime + 8.8));
            float3 outward = position - origin;
            outward = length(outward) > 1.0e-4 ? normalize(outward) : random;
            float3 launch = normalize(lerp(axis, outward, uParams.LaunchForces.y) + 1.0e-4);
            velocity = normalize(lerp(launch, random, uParams.LaunchForces.x) + 1.0e-4)
                     * uParams.SpawnKinematics.w
                     * (0.75 + 0.5 * Hash31(float3(pixel, simTime + 6.6)))
                     + spawnLimbVel;
            age = 0.0;
            alive = true;
        }
    }

    uint forceCount = uint(uParams.SwarmControlFlags.z + 0.5);
    float gravity = uParams.LaunchForces.z;
    float drag = uParams.LaunchForces.w;
    float noiseAmt = uParams.CouplingNoise.x;
    float fieldInf = uParams.CouplingNoise.y;
    float gasInf = uParams.CouplingNoise.z;
    float bounce = uParams.SpringBounce.z;

    if (alive)
    {
        if (mode == 1 && derezMode > 0 && derezMix > 0.001)
        {
            if (!derez.Released)
            {
                position = lerp(position, home, clamp(dt * 18.0, 0.0, 1.0));
                velocity *= exp(-8.0 * dt);
                age = 0.0;
            }
            else
            {
                if (age <= 0.0001 && derezMode != 2)
                {
                    float3 outDir = restHome - origin;
                    outDir = length(outDir) > 1.0e-4 ? normalize(outDir) : RandomDirection(float3(pixel, 3.3));
                    float3 jitter = RandomDirection(float3(pixel, 7.1)) * 0.55;
                    float upLift = (derezMode == 4) ? 1.35 : 0.45;
                    velocity = normalize(outDir + jitter + float3(0.0, upLift, 0.0)) * derezShatter * (0.65 + 0.7 * seed);
                }
                age += dt;
                float3 accel = float3(0.0, -gravity, 0.0);
                if (derezMode == 4)
                {
                    accel.y += 2.6 + 1.2 * sin(simTime * 3.5 + seed * 6.283);
                    accel += CurlNoise(position * 1.1 + float3(simTime * 0.6, seed * 3.1, -simTime * 0.4)) * 3.4;
                }
                else if (derezMode == 2)
                {
                    float3 orbitTarget = origin + CurlNoise(restHome * 0.9 + float3(simTime * 0.35, seed * 4.0, 0.0)) * (radius * 1.6);
                    accel += (orbitTarget - position) * 2.2 - velocity * 1.8;
                }
                else
                {
                    if (noiseAmt != 0.0) accel += CurlNoise(position * noiseScale + float3(simTime * noiseSpeed, vSeed * 0.7, -simTime * noiseSpeed * 0.4)) * noiseAmt * 3.0;
                    if (fieldInf != 0.0 && forceCount > 0u) accel += EvaluateTotalForceField(gForceRecords, forceCount, position, velocity, simTime, kForceChannelParticle) * fieldInf;
                }
                velocity += accel * dt;
                velocity *= exp(-max(drag, 0.35) * dt);
                position += velocity * dt;
                float floorY = gridCentre.y - gridHalf.y + 0.01;
                if (position.y < floorY && bounce >= 0.0)
                {
                    position.y = floorY;
                    float3 burst = RandomDirection(float3(pixel, floor(simTime * 4.0) + 1.7));
                    velocity.y = abs(velocity.y) * max(bounce, 0.35) + derezShatter * 0.22 * frac(seed * 13.7);
                    velocity.xz = velocity.xz * 0.65 + burst.xz * (derezShatter * 0.28);
                }
            }
            ParticleStateRecord outDerez;
            outDerez.PositionAge = float4(position, age);
            outDerez.VelocitySeed = float4(velocity, vSeed);
            gWriteStates[index] = outDerez;
            return;
        }

        age += dt;
        float3 acceleration = float3(0.0, -gravity, 0.0);
        if (fieldInf != 0.0 && forceCount > 0u)
        {
            acceleration += EvaluateTotalForceField(gForceRecords, forceCount, position, velocity, simTime, kForceChannelParticle) * fieldInf;
        }
        if (noiseAmt != 0.0)
        {
            acceleration += CurlNoise(position * noiseScale + float3(simTime * noiseSpeed, vSeed * 0.7, -simTime * noiseSpeed * 0.4)) * noiseAmt * 3.0;
        }
        if (gasInf != 0.0)
        {
            float3 gas = SampleGasVelocity(position, gridCentre, gridHalf, uParams.SwarmControlFlags.w);
            acceleration += (gas - velocity) * gasInf;
        }
        float swarmStrength = uParams.DissolveSwarm.z;
        if (swarmStrength > 0.0001)
        {
            float flockGroup = seed > 0.5 ? 1.0 : -1.0;
            float st = simTime * (0.55 + 0.15 * flockGroup);
            float span = max(radius, 0.8);
            float3 leader = origin + float3(
                sin(st * 1.1 + flockGroup) * span * 1.15,
                sin(st * 1.7 + flockGroup * 0.6) * span * 0.45,
                cos(st * 0.9 - flockGroup * 0.4) * span * 1.15);
            float3 toLeader = leader - position;
            float distLeader = length(toLeader);
            float3 cohesionDir = distLeader > 1.0e-4 ? (toLeader / distLeader) : float3(0.0, 1.0, 0.0);
            float3 tangent = normalize(cross(float3(0.0, 1.0, 0.0), position - origin + float3(1.0e-4, 0.0, 1.0e-4)));
            float3 flutter = CurlNoise(position * 1.25 + float3(simTime * 0.8, seed * 5.3, -simTime * 0.6));
            float wingRate = uParams.Shape.DerezEdgeColourWing.w;
            float wingBob = sin(simTime * max(wingRate, 1.0) + seed * 6.2831853) * 0.35;
            acceleration += (cohesionDir * uParams.DissolveSwarm.w * min(distLeader, 3.0)
                          + tangent * uParams.SwarmControlFlags.x
                          + flutter * 1.1
                          + float3(0.0, wingBob, 0.0)) * swarmStrength;
        }
        if (mode == 1)
        {
            acceleration += (home - position) * uParams.SpringBounce.x - velocity * uParams.SpringBounce.y + limbVel * 0.65;
        }
        velocity += acceleration * dt;
        velocity *= exp(-drag * dt);
        position += velocity * dt;

        float floorY = gridCentre.y - gridHalf.y + 0.01;
        if (position.y < floorY && bounce >= 0.0)
        {
            position.y = floorY;
            velocity.y = abs(velocity.y) * bounce;
            velocity.xz *= 0.7;
        }

        float3 local = position - gridCentre;
        if (mode == 0 && (abs(local.x) > gridHalf.x * 1.05 || abs(local.z) > gridHalf.z * 1.05 || local.y > gridHalf.y * 1.2 || local.y < -gridHalf.y * 1.5))
        {
            age = max(age, lifetime);
        }
    }

    ParticleStateRecord outFinal;
    outFinal.PositionAge  = float4(position, age);
    outFinal.VelocitySeed = float4(velocity, vSeed);
    gWriteStates[index] = outFinal;
}
