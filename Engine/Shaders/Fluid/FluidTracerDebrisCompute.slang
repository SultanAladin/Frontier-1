/*============================================================================================================================================
                                                    FLUIDTRACERDEBRISCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Stage D (Slice S18) 1:1 8,192 advected tracer particles & 8,192 buoyant/ballistic debris particles (Rubble, Ember, Paper)
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js TracerUpdateFragment (L951-L999) & DebrisUpdateFragment (L1589-L1677)

import FluidVolumeAccess;
import ForceFieldNoise;

//------------------------------------------------------------------------------------------------------------------------
//                                                    UNIFORM BLOCKS & BINDINGS
//------------------------------------------------------------------------------------------------------------------------

/// Matches C++ FluidTracerUpdateUniformBlock byte-for-byte (192 bytes, 16-byte aligned).
public struct FluidTracerUpdateUniformBlock
{
    public FluidVolumeUniformBlock Volume;       // 160 B
    public float4                  TracerTiming; // x = deltaTime, y = simTime, z = lifetime, w = resetFlag (0/1)
    public int4                    TracerDims;   // x = width, y = height, z = count, w = 0
};

/// Matches C++ FluidDebrisUpdateUniformBlock byte-for-byte (512 bytes, 16-byte aligned).
public struct FluidDebrisUpdateUniformBlock
{
    public FluidVolumeUniformBlock Volume;           // 160 B
    public float4                  DebrisTiming;     // x = deltaTime, y = simTime, z = lifetime, w = resetFlag
    public float4                  DebrisPhysics;    // x = gravity, y = drag, z = bounce, w = sourceCount
    public int4                    DebrisDims;       // x = width, y = height, z = count, w = 0
    public float4                  SourceA[6];       // xyz = position, w = radius
    public float4                  SourceB[6];       // xyz = velocity, w = density
    public float4                  SourceE[6];       // x = debrisAmount, y = debrisSpeed, z = envelope, w = kind (0/1/2)
};

[[vk::binding(0, 0)]] ConstantBuffer<FluidTracerUpdateUniformBlock> uTracerParams;
[[vk::binding(1, 0)]] ConstantBuffer<FluidDebrisUpdateUniformBlock> uDebrisParams;
[[vk::binding(2, 0)]] Texture3D<float4>                             gVelocityVolume;
[[vk::binding(3, 0)]] SamplerState                                  gLinearClamp;
[[vk::binding(4, 0)]] StructuredBuffer<float4>                      gTracersIn;
[[vk::binding(5, 0)]] RWStructuredBuffer<float4>                    gTracersOut;
[[vk::binding(6, 0)]] StructuredBuffer<float4>                      gDebrisPosIn;
[[vk::binding(7, 0)]] StructuredBuffer<float4>                      gDebrisVelIn;
[[vk::binding(8, 0)]] RWStructuredBuffer<float4>                    gDebrisPosOut;
[[vk::binding(9, 0)]] RWStructuredBuffer<float4>                    gDebrisVelOut;

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRACER UPDATE KERNEL
//------------------------------------------------------------------------------------------------------------------------

float3 SpawnTracerPosition(FluidVolumeUniformBlock vol, float2 pixel, float salt)
{
    float3 extent = float3(vol.GridBoundary.xyz) * vol.GridCentreCellSize.w;
    float3 u = float3(
        Hash31(float3(pixel, salt)),
        Hash31(float3(pixel.yx, salt + 7.3)),
        Hash31(float3(pixel + 3.1, salt + 19.7)));
    return vol.GridCentreCellSize.xyz + (u - 0.5) * extent * 0.96;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void TracerUpdateComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int index = int(dispatchThreadId.x);
    int count = uTracerParams.TracerDims.z;
    if (index < 0 || index >= count) return;

    int width = max(1, uTracerParams.TracerDims.x);
    float2 pixel = float2(float(index - (index / width) * width), float(index / width));

    float dt       = uTracerParams.TracerTiming.x;
    float simTime  = uTracerParams.TracerTiming.y;
    float lifetime = uTracerParams.TracerTiming.z;
    bool  doReset  = (uTracerParams.TracerTiming.w > 0.5);

    if (doReset)
    {
        gTracersOut[index] = float4(
            SpawnTracerPosition(uTracerParams.Volume, pixel, 0.0),
            Hash31(float3(pixel, 3.3)) * lifetime);
        return;
    }

    float4 tracer  = gTracersIn[index];
    float3 pos     = tracer.xyz;
    float  age     = tracer.w + dt;

    float3 v1  = SampleVolumeDense(gVelocityVolume, gLinearClamp, uTracerParams.Volume, CellFromWorld(uTracerParams.Volume, pos)).xyz;
    float3 mid = pos + 0.5 * dt * v1;
    float3 v2  = SampleVolumeDense(gVelocityVolume, gLinearClamp, uTracerParams.Volume, CellFromWorld(uTracerParams.Volume, mid)).xyz;
    pos += dt * v2;

    float3 halfExt = GridHalf(uTracerParams.Volume);
    float3 rel     = abs(pos - uTracerParams.Volume.GridCentreCellSize.xyz);
    bool   outside = (rel.x > halfExt.x) || (rel.y > halfExt.y) || (rel.z > halfExt.z);
    if (age > lifetime || outside)
    {
        pos = SpawnTracerPosition(uTracerParams.Volume, pixel, simTime);
        age = 0.0;
    }
    gTracersOut[index] = float4(pos, age);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DEBRIS UPDATE KERNEL
//------------------------------------------------------------------------------------------------------------------------

float3 RandomUnitDirection(float3 seed)
{
    float z = Hash31(seed) * 2.0 - 1.0;
    float a = Hash31(seed.zxy + 5.1) * 6.2831853;
    float r = sqrt(max(0.0, 1.0 - z * z));
    return float3(r * cos(a), z, r * sin(a));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void DebrisUpdateComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int index = int(dispatchThreadId.x);
    int count = uDebrisParams.DebrisDims.z;
    if (index < 0 || index >= count) return;

    int width = max(1, uDebrisParams.DebrisDims.x);
    float2 pixel = float2(float(index - (index / width) * width), float(index / width));

    float dt       = uDebrisParams.DebrisTiming.x;
    float simTime  = uDebrisParams.DebrisTiming.y;
    float lifetime = uDebrisParams.DebrisTiming.z;
    bool  doReset  = (uDebrisParams.DebrisTiming.w > 0.5);

    float initSeed = Hash31(float3(pixel, 11.7));
    if (doReset)
    {
        gDebrisPosOut[index] = float4(0.0, -1.0e4, 0.0, lifetime + 1.0);
        gDebrisVelOut[index] = float4(0.0, 0.0, 0.0, initSeed);
        return;
    }

    float4 p = gDebrisPosIn[index];
    float4 v = gDebrisVelIn[index];
    float3 pos = p.xyz;
    float  age = p.w;
    float3 vel = v.xyz;
    float  vw  = v.w;

    float  gravity     = uDebrisParams.DebrisPhysics.x;
    float  drag        = uDebrisParams.DebrisPhysics.y;
    float  bounce      = uDebrisParams.DebrisPhysics.z;
    int    sourceCount = int(uDebrisParams.DebrisPhysics.w + 0.5);
    float3 halfExt     = GridHalf(uDebrisParams.Volume);

    bool dead = (age >= lifetime);
    if (dead && sourceCount > 0)
    {
        int srcIdx = int(fmod(pixel.x + pixel.y * 7.0, float(sourceCount)));
        float4 e = uDebrisParams.SourceE[srcIdx];
        float chance = e.x * e.z * dt * 9.0;
        if (Hash31(float3(pixel, simTime)) < chance)
        {
            float4 a = uDebrisParams.SourceA[srcIdx];
            float3 dir = RandomUnitDirection(float3(pixel, simTime + 3.3));
            pos = a.xyz + dir * a.w * Hash31(float3(pixel.yx, simTime));
            float spd = e.y * (0.35 + 0.65 * Hash31(float3(pixel, simTime + 9.1)));
            vel = uDebrisParams.SourceB[srcIdx].xyz + dir * spd + float3(0.0, 0.45 * e.y, 0.0);
            vw  = floor(e.w + 0.5) + frac(vw);
            age = 0.0;
            dead = false;
        }
    }

    if (!dead)
    {
        age += dt;
        int   kind      = int(floor(vw));
        float seedValue = frac(vw);
        float3 gas      = SampleVolumeDense(gVelocityVolume, gLinearClamp, uDebrisParams.Volume, CellFromWorld(uDebrisParams.Volume, pos)).xyz;

        float gravScale = (kind == 1) ? 0.12 : ((kind == 2) ? 0.30 : 1.0);
        float dragScale = (kind == 1) ? 3.00 : ((kind == 2) ? 4.00 : 1.0);
        vel += float3(0.0, -gravity * gravScale, 0.0) * dt;
        if (kind == 1)
        {
            vel += float3(0.0, 0.9 + 0.6 * seedValue, 0.0) * dt * exp(-age * 0.6);
        }
        if (kind != 0)
        {
            vel += CurlNoise(pos * 1.7 + float3(simTime * 0.8, seedValue * 9.0, 0.0)) * ((kind == 2) ? 1.6 : 0.5) * dt;
        }
        vel += (gas - vel) * min(1.0, drag * dragScale * dt * (0.6 + 0.8 * seedValue));
        pos += vel * dt;

        float floorY = uDebrisParams.Volume.GridCentreCellSize.y - halfExt.y + uDebrisParams.Volume.GridCentreCellSize.w * 0.5;
        if (pos.y < floorY)
        {
            pos.y = floorY;
            vel.y = abs(vel.y) * bounce;
            vel.x *= 0.55;
            vel.z *= 0.55;
            if (vel.y < 0.25) vel = float3(0.0, 0.0, 0.0);
            if (kind == 1) age = lifetime;
        }
        float3 local = pos - uDebrisParams.Volume.GridCentreCellSize.xyz;
        if (abs(local.x) > halfExt.x || abs(local.z) > halfExt.z || local.y > halfExt.y)
        {
            age = lifetime;
        }
    }

    gDebrisPosOut[index] = float4(pos, age);
    gDebrisVelOut[index] = float4(vel, vw);
}
