/*==========================================================================================================================================
                                               PATHREPLAYSAMPLEHISTORY.SLANG
==========================================================================================================================================*/
// 🧩 Retain unselected random seeds and retrace their paths at the current diffuse surface.

#include "PathReplayReservoir.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                SEED BANK INTAKE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Seed-bank validity helpers live in PathReplayReservoir.slang; generation also consumes them.

// 📝 This gather finishes before resolve overwrites the prior guides and before any thread replaces a seed bank.
[shader("compute")][numthreads(64, 1, 1)] void GatherReplaySampleHistory(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint R = Receivers[Pixel];
    float4 First = 0, Second = 0;
    if ((Extent.Primary.w & 524288u) == 0 && Extent.History.w == 0 && Extent.History.z != 0 &&
        R.PositionHit.w != 0 && R.MotionPositionToken.w > 0 && R.MotionPixel.w != 0 &&
        all(R.PositionHit.xyz == R.MotionPositionToken.xyz) &&
        all(R.NormalRoughness.xyz == R.MotionNormal.xyz) && isfinite(R.MotionPixel.z) &&
        ReplayMotionConvertible(R.MotionPixel.xy))
    {
        int2 P = int2(floor(R.MotionPixel.xy + .5));
        if (all(P >= 0) && all(P < int2(Extent.History.xy)))
        {
            uint Previous = uint(P.y) * Extent.History.x + uint(P.x);
            float4 Position = OutputColour[ReplaySlot(1, Previous)];
            float3 Normal = OutputColour[ReplaySlot(4, Previous)].xyz;
            float4 OldFirst = OutputColour[ReplaySlot(11, Previous)];
            float4 OldSecond = OutputColour[ReplaySlot(12, Previous)];
            float NormalLength = dot(Normal, Normal), CurrentLength = dot(R.MotionNormal.xyz, R.MotionNormal.xyz);
            float3 Delta = Position.xyz - R.MotionPositionToken.xyz;
            float Radius = clamp(R.MotionPixel.z, 0, .25);
            bool Compatible = Position.w == R.MotionPositionToken.w && dot(Delta, Delta) <= Radius * Radius &&
                NormalLength > 1e-12 && CurrentLength > 1e-12 &&
                dot(Normal, R.MotionNormal.xyz) >= .99 * sqrt(NormalLength * CurrentLength) &&
                ReplaySeedBankValid(OldFirst, OldSecond);
            if (Compatible)
            {
                First = OldFirst;
                Second = OldSecond;
                InterlockedAdd(Counters[23], 1);
            }
        }
    }
    OutputColour[ReplaySlot(13, Pixel)] = First;
    OutputColour[ReplaySlot(14, Pixel)] = Second;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             CURRENT-SURFACE RETRACING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Without reservoir reuse, only the one fresh, unselected seed and up to three gathered fresh seeds
//    enter this arithmetic mean. With temporal or spatial reuse, generation has already entered the
//    gathered seeds as reservoir candidates and resolve owns the output, so this stage only shifts
//    the bank. No prior colour, reservoir-selected seed, visibility answer or path contribution is
//    retained in either mode.
[shader("compute")][numthreads(64, 1, 1)] void FinalizeReplaySampleHistory(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint R = Receivers[Pixel];
    float4 Current = OutputColour[ReplaySlot(2, Pixel)];
    float4 First = OutputColour[ReplaySlot(13, Pixel)];
    float4 Second = OutputColour[ReplaySlot(14, Pixel)];
    bool Combined = Extent.Reconstruction.x != 0 || Extent.Reconstruction.y != 0;
    if (R.PositionHit.w == 0 || R.MotionPositionToken.w <= 0 || (Combined ? Current.w <= 0 : Current.w != 1))
    {
        OutputColour[ReplaySlot(11, Pixel)] = 0;
        OutputColour[ReplaySlot(12, Pixel)] = 0;
        return;
    }
    uint Count = ReplaySeedBankValid(First, Second) ? uint(Second.z) : 0;
    if (Combined)
    {
        // 📝 The bank stores the generator stream's next unused draw, never a seed that entered this
        //    frame's reservoir (neither the fresh candidate nor the selected seed). A seed that competed
        //    in a reservoir is correlated with that reservoir's stored weight; banking an unused draw
        //    keeps later retracing statistically independent of it (ReplaySeedCorrelation.md).
        uint FreshBits = Extent.Sampling.x ^ (Pixel * 747796405u + 2891336453u);
        for (uint Draw = 0; Draw < max(Extent.Sampling.y, 1u); ++Draw) NextRandom(FreshBits);
        float2 FreshSeed = EncodeReplaySeed(NextRandom(FreshBits));
        float4 ShiftedFirst = float4(FreshSeed, 0, 0), ShiftedSecond = float4(0, 0, 1, 0);
        if (Count > 0) ShiftedFirst.zw = First.xy;
        if (Count > 1) ShiftedSecond.xy = First.zw;
        ShiftedSecond.z = float(min(Count + 1, 3u));
        OutputColour[ReplaySlot(11, Pixel)] = ShiftedFirst;
        OutputColour[ReplaySlot(12, Pixel)] = ShiftedSecond;
        return;
    }
    if (Count > 0)
    {
        float3 Total = OutputColour[ReplaySlot(3, Pixel)].xyz;
        Total += EvaluateReplayPath(R, DecodeReplaySeed(First.xy));
        if (Count > 1) Total += EvaluateReplayPath(R, DecodeReplaySeed(First.zw));
        if (Count > 2) Total += EvaluateReplayPath(R, DecodeReplaySeed(Second.xy));
        float Alpha = OutputColour[Pixel].w;
        OutputColour[Pixel] = float4(Total / float(Count + 1) + R.ResidualSpecular.xyz, Alpha);
    }
    float4 NewFirst = float4(Current.xy, 0, 0), NewSecond = float4(0, 0, 1, 0);
    if (Count > 0) NewFirst.zw = First.xy;
    if (Count > 1) NewSecond.xy = First.zw;
    NewSecond.z = float(min(Count + 1, 3u));
    OutputColour[ReplaySlot(11, Pixel)] = NewFirst;
    OutputColour[ReplaySlot(12, Pixel)] = NewSecond;
}
