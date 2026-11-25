/*============================================================================================================================================
                                                    PARTICLETRAILUPDATECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Pure .slang GPU compute ring-buffer history update for ParticleSpace spline ribbons (up to 96 samples/particle)
// Reference: Experimental/FluidEditor/Engine/ParticleShaders.js L967-L993 (TrailUpdateFragment)

//------------------------------------------------------------------------------------------------------------------------
//                                                    DATA & UNIFORM BLOCKS
//------------------------------------------------------------------------------------------------------------------------

public struct ParticleStateRecord
{
    public float4 PositionAge;  // xyz = world position (m), w = age (s)
    public float4 VelocitySeed; // xyz = velocity (m/s), w = seed [0..1]
};

/// Matches C++ ParticleTrailUpdateUniformBlock byte-for-byte (32 bytes, 16-byte aligned).
public struct ParticleTrailUpdateUniformBlock
{
    public float4 LayoutHeadAdvance; // x = capacity, y = trailLength (2..96), z = headSlot, w = advanceFlag (0/1)
    public float4 ResetLifetimePad;  // x = resetFlag (0/1), y = lifetime (0 for Cloner), z = reserved, w = reserved
};

[[vk::binding(0, 0)]] ConstantBuffer<ParticleTrailUpdateUniformBlock> uParams;
[[vk::binding(1, 0)]] StructuredBuffer<float4>                        gTrailRead;
[[vk::binding(2, 0)]] RWStructuredBuffer<float4>                      gTrailWrite;
[[vk::binding(3, 0)]] StructuredBuffer<ParticleStateRecord>           gStates;
[[vk::binding(4, 0)]] StructuredBuffer<float4>                        gPreviousPositions;

//------------------------------------------------------------------------------------------------------------------------
//                                                      COMPUTE ENTRY
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint capacity    = uint(max(uParams.LayoutHeadAdvance.x + 0.5, 1.0));
    uint trailLength = uint(max(uParams.LayoutHeadAdvance.y + 0.5, 2.0));
    uint totalSlots  = capacity * trailLength;
    uint idx = dispatchThreadId.x;
    if (idx >= totalSlots) return;

    uint particle = idx / trailLength;
    uint slot     = idx - particle * trailLength;
    uint head     = uint(max(uParams.LayoutHeadAdvance.z + 0.5, 0.0));
    bool advance  = (uParams.LayoutHeadAdvance.w > 0.5);
    bool reset    = (uParams.ResetLifetimePad.x > 0.5);
    float lifetime = uParams.ResetLifetimePad.y;

    float4 current  = gStates[particle].PositionAge;
    float4 previous = gPreviousPositions[particle];
    float4 old      = gTrailRead[idx];

    bool respawned = reset
                  || (current.w < previous.w)
                  || (old.w < 0.5)
                  || (distance(current.xyz, previous.xyz) > 2.0);
    if (respawned)
    {
        gTrailWrite[idx] = float4(current.xyz, 1.0);
        return;
    }

    bool frozen = (current.w >= lifetime) && (lifetime > 0.0);
    gTrailWrite[idx] = (advance && slot == head && !frozen) ? float4(current.xyz, 1.0) : old;
}
