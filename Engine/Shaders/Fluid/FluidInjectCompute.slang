/*============================================================================================================================================
                                                        FLUIDINJECTCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Stage D (Slice S15) 1:1 Sphere/Disc/Box ReleaseSource injection with ValueNoise jitter & exponential pulse into velocity/state volumes
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js L597-L683 (InjectFragment)

import ForceFieldNoise;
import FluidVolumeAccess;

//------------------------------------------------------------------------------------------------------------------------
//                                                    UNIFORM BLOCK & BINDINGS
//------------------------------------------------------------------------------------------------------------------------

public static const int kMaxFluidReleaseSources = 6;

/// Matches C++ FluidInjectUniformBlock byte-for-byte (560 bytes, 16-byte aligned).
public struct FluidInjectUniformBlock
{
    public FluidVolumeUniformBlock Volume;
    public float4                  InjectTiming;           // x = mode (0=velocity, 1=state), y = sourceCount, z = deltaTime, w = simulationTime
    public float4                  SourcePosRadius[6];     // xyz = position (m), w = radius (m)
    public float4                  SourceVelDensity[6];    // xyz = velocity (m/s), w = density * envelope
    public float4                  SourceTempFuelShape[6]; // x = temp * env, y = fuel * env, z = jitter, w = shape (0=Sphere, 1=Disc, 2=Box)
    public float4                  SourceOrientPulse[6];   // xyz = orientation, w = pulse period (s)
};

[[vk::binding(0, 0)]] ConstantBuffer<FluidInjectUniformBlock> uParams;
[[vk::binding(1, 0)]] Texture3D<float4>                       gTarget;
[[vk::binding(2, 0)]] [format("rgba16f")] RWTexture3D<float4>                     gOutput;

//------------------------------------------------------------------------------------------------------------------------
//                                                      MASK EVALUATION
//------------------------------------------------------------------------------------------------------------------------

float EvaluateReleaseSourceMask(int index, float3 world)
{
    float4 a = uParams.SourcePosRadius[index];
    float4 c = uParams.SourceTempFuelShape[index];
    float4 d = uParams.SourceOrientPulse[index];

    float cellSize = max(uParams.Volume.GridCentreCellSize.w, 1.0e-5);
    float3 r       = world - a.xyz;
    float radius   = max(a.w, cellSize);
    int shape      = int(c.w + 0.5);
    float edge     = cellSize * 1.5;
    float mask     = 0.0;

    if (shape == 0)
    {
        mask = 1.0 - smoothstep(radius - edge, radius, length(r));
    }
    else if (shape == 1)
    {
        float nLen   = length(d.xyz);
        float3 n     = (nLen > 1.0e-5) ? (d.xyz / nLen) : float3(0.0, 1.0, 0.0);
        float along  = abs(dot(r, n));
        float radial = length(r - n * dot(r, n));
        mask = (1.0 - smoothstep(radius - edge, radius, radial)) * (1.0 - smoothstep(edge * 0.6, edge * 1.4, along));
    }
    else
    {
        float3 q = abs(r) - float3(radius, radius, radius);
        float boxDistance = max(q.x, max(q.y, q.z));
        mask = 1.0 - smoothstep(-edge, 0.0, boxDistance);
    }

    float simTime = uParams.InjectTiming.w;
    float jitter  = c.z;
    if (jitter > 0.0)
    {
        float n = ValueNoise(world * (2.2 / radius) + float3(0.0, simTime * 1.6, simTime * 0.9));
        mask *= lerp(1.0, smoothstep(0.25, 0.8, n) * 1.4, clamp(jitter, 0.0, 1.0));
    }

    float pulse = d.w;
    if (pulse > 0.0)
    {
        float phase = frac(simTime / pulse);
        mask *= exp(-phase * 7.0) * step(phase, 0.45);
    }

    return clamp(mask, 0.0, 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      COMPUTE ENTRY
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 4)]
void ComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int3 cell = int3(dispatchThreadId);
    if (!CellInsideDense(uParams.Volume, cell)) return;

    float3 world    = WorldFromCell(uParams.Volume, float3(cell) + 0.5);
    float4 value    = FetchCellDense(gTarget, uParams.Volume, cell);
    int mode        = int(uParams.InjectTiming.x + 0.5);
    int sourceCount = min(int(uParams.InjectTiming.y + 0.5), kMaxFluidReleaseSources);
    float dt        = uParams.InjectTiming.z;
    float blend     = min(1.0, dt * 10.0);

    for (int i = 0; i < sourceCount; ++i)
    {
        float mask = EvaluateReleaseSourceMask(i, world);
        if (mask > 0.0)
        {
            float4 b = uParams.SourceVelDensity[i];
            float4 c = uParams.SourceTempFuelShape[i];
            if (mode == 0)
            {
                if (dot(b.xyz, b.xyz) > 0.0)
                {
                    value.xyz = lerp(value.xyz, b.xyz, mask * blend);
                }
            }
            else
            {
                value.x = min(value.x + mask * b.w * dt * 6.0, max(value.x, b.w * 1.5));
                value.y = lerp(value.y, max(value.y, c.x), mask * blend);
                value.z = min(value.z + mask * c.y * dt * 6.0, max(value.z, c.y * 1.5));
            }
        }
    }

    gOutput[cell] = value;
}
