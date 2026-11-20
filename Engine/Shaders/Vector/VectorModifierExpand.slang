/*============================================================================================================================================
                                                          VECTORMODIFIEREXPAND.SLANG
============================================================================================================================================*/
// 🧩 GPU Compute expansion of all 8 non-destructive layer modifiers into VectorInstanceEntry[] + atomic summary KPI metrics.
// Uses OutInstances[0..255] as scratch ping-pong (layer slots start at 1) to keep shader stack footprint near zero.

#include "VectorSharedLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<VectorModifierDispatchParams> DispatchParams;
[[vk::binding(1, 0)]] StructuredBuffer<VectorModifierEntry>          Modifiers;
[[vk::binding(2, 0)]] RWStructuredBuffer<VectorInstanceEntry>        OutInstances;
[[vk::binding(3, 0)]] RWStructuredBuffer<VectorSummaryMetrics>       SummaryBuffer;

[numthreads(1, 1, 1)]
[shader("compute")]
void ComputeEntry(uint3 DispatchID : SV_DispatchThreadID)
{
    if (DispatchID.x != 0u)
    {
        return;
    }

    uint MaxOut     = clamp(DispatchParams.CountsDimensionsPacked.w, 1u, 256u);
    uint FirstSlotOffset = max(256u, uint(max(0.0, DispatchParams.LayerExtentRotation.w) + 0.5));
    uint ModCount   = min(DispatchParams.CountsDimensionsPacked.x, 8u);
    uint LayerId    = DispatchParams.CountsDimensionsPacked.y;
    float DocExtent = max(1.0, DispatchParams.CanvasMetrics.x);
    float OriginCx    = DispatchParams.CanvasMetrics.z;
    float OriginCy    = DispatchParams.CanvasMetrics.w;
    float OriginRot   = DispatchParams.LayerExtentRotation.z;

    float Cr0 = cos(OriginRot);
    float Sr0 = sin(OriginRot);
    VectorInstanceEntry InitEntry;
    InitEntry.TransformRow0   = float4(Cr0, -Sr0, OriginCx, 1.0);
    InitEntry.TransformRow1   = float4(Sr0,  Cr0, OriginCy, 1.0);
    InitEntry.ColorOpacityPacked = float4(1.0, 1.0, 1.0, 0.0);
    InitEntry.ModParams       = float4(1.0, 1.0, 0.0, 0.0);
    InitEntry.PickMeta        = uint4(LayerId, 0u, 0u, 0u);
    OutInstances[FirstSlotOffset]  = InitEntry;
    uint Count                = 1u;

    for (uint MIdx = 0u; MIdx < ModCount; ++MIdx)
    {
        VectorModifierEntry Mod = Modifiers[MIdx];
        if (Mod.Header.y == 0u)
        {
            continue;
        }

        uint Cat = Mod.Header.x;
        if (Cat == 1u) // GridArray
        {
            uint Cols       = uint(clamp(int(Mod.ScalarBlockA.x + 0.5), 1, 16));
            uint Rows       = uint(clamp(int(Mod.ScalarBlockB.x + 0.5), 1, 16));
            uint LayoutMode = Mod.Header.z; // 0:Uniform, 1:Brick, 2:Hex
            float StepX     = DocExtent / float(Cols);
            float StepY     = DocExtent / float(Rows);

            for (uint I = 0u; I < Count; ++I) OutInstances[I] = OutInstances[FirstSlotOffset + I];

            uint NextCount = 0u;
            for (uint I = 0u; I < Count; ++I)
            {
                VectorInstanceEntry Src = OutInstances[I];
                for (uint R = 0u; R < Rows; ++R)
                {
                    float ShiftX = (LayoutMode != 0u && (R & 1u) != 0u) ? StepX * 0.5 : 0.0;
                    for (uint C = 0u; C < Cols; ++C)
                    {
                        if (NextCount >= 256u || NextCount >= MaxOut) break;
                        float Tx = (float(C) + 0.5) * StepX + ShiftX;
                        if (Tx > DocExtent) Tx -= DocExtent;
                        float Ty = (float(R) + 0.5) * StepY;
                        VectorInstanceEntry Copy = Src;
                        Copy.TransformRow0.z = Tx + (Src.TransformRow0.z - OriginCx);
                        Copy.TransformRow1.z = Ty + (Src.TransformRow1.z - OriginCy);
                        Copy.PickMeta.y      = NextCount;
                        OutInstances[FirstSlotOffset + NextCount] = Copy;
                        NextCount++;
                    }
                    if (NextCount >= 256u || NextCount >= MaxOut) break;
                }
            }
            Count = NextCount;
        }
        else if (Cat == 2u) // RadialArray
        {
            uint  Arms  = uint(clamp(int(Mod.ScalarBlockA.x + 0.5), 1, 32));
            float RadPx = max(4.0, Mod.ScalarBlockB.x);
            uint  Rings = uint(clamp(int(Mod.ScalarBlockC.x + 0.5), 1, 8));

            for (uint I = 0u; I < Count; ++I) OutInstances[I] = OutInstances[FirstSlotOffset + I];

            uint NextCount = 0u;
            for (uint I = 0u; I < Count; ++I)
            {
                VectorInstanceEntry Src = OutInstances[I];
                float Sx = Src.TransformRow0.w;
                for (uint Ring = 0u; Ring < Rings; ++Ring)
                {
                    float RDist = RadPx * float(Ring + 1u) / float(Rings);
                    for (uint Arm = 0u; Arm < Arms; ++Arm)
                    {
                        if (NextCount >= 256u || NextCount >= MaxOut) break;
                        float Theta = (float(Arm) / float(Arms)) * 6.28318530718;
                        float Cr    = cos(OriginRot + Theta);
                        float Sr    = sin(OriginRot + Theta);
                        VectorInstanceEntry Copy = Src;
                        Copy.TransformRow0 = float4(Cr * Sx, -Sr * Sx, Src.TransformRow0.z + cos(Theta) * RDist, Sx);
                        Copy.TransformRow1 = float4(Sr * Sx,  Cr * Sx, Src.TransformRow1.z + sin(Theta) * RDist, Sx);
                        Copy.PickMeta.y    = NextCount;
                        OutInstances[FirstSlotOffset + NextCount] = Copy;
                        NextCount++;
                    }
                    if (NextCount >= 256u || NextCount >= MaxOut) break;
                }
            }
            Count = NextCount;
        }
        else if (Cat == 3u) // MirrorSymmetry
        {
            uint AxisMode = Mod.Header.z; // 0:X, 1:Y, 2:BothXY
            for (uint I = 0u; I < Count; ++I) OutInstances[I] = OutInstances[FirstSlotOffset + I];

            uint NextCount = 0u;
            for (uint I = 0u; I < Count; ++I)
            {
                VectorInstanceEntry Src = OutInstances[I];
                for (uint CopyMask = 0u; CopyMask < 4u; ++CopyMask)
                {
                    if (AxisMode == 0u && CopyMask >= 2u) break;
                    if (AxisMode == 1u && (CopyMask == 1u || CopyMask == 3u)) continue;
                    if (NextCount >= 256u || NextCount >= MaxOut) break;
                    bool FlipX = (CopyMask & 1u) != 0u;
                    bool FlipY = (CopyMask & 2u) != 0u;
                    float Mx = FlipX ? -1.0 : 1.0;
                    float My = FlipY ? -1.0 : 1.0;
                    VectorInstanceEntry Copy = Src;
                    Copy.TransformRow0 = float4(Src.TransformRow0.x * Mx, Src.TransformRow0.y * Mx, DocExtent * 0.5 + (Src.TransformRow0.z - DocExtent * 0.5) * Mx, Src.TransformRow0.w);
                    Copy.TransformRow1 = float4(Src.TransformRow1.x * My, Src.TransformRow1.y * My, DocExtent * 0.5 + (Src.TransformRow1.z - DocExtent * 0.5) * My, Src.TransformRow1.w);
                    Copy.PickMeta.y    = NextCount;
                    OutInstances[FirstSlotOffset + NextCount] = Copy;
                    NextCount++;
                }
            }
            Count = NextCount;
        }
        else if (Cat == 4u) // InsetEcho
        {
            uint  Echoes  = uint(clamp(int(Mod.ScalarBlockA.x + 0.5), 1, 16));
            float InsetPx = Mod.ScalarBlockB.x;
            float RefDim  = max(16.0, max(DispatchParams.LayerExtentRotation.x, DispatchParams.LayerExtentRotation.y));
            for (uint I = 0u; I < Count; ++I) OutInstances[I] = OutInstances[FirstSlotOffset + I];

            uint NextCount = 0u;
            for (uint I = 0u; I < Count; ++I)
            {
                VectorInstanceEntry Src = OutInstances[I];
                for (uint E = 0u; E < Echoes; ++E)
                {
                    if (NextCount >= 256u || NextCount >= MaxOut) break;
                    float Shrink = max(0.08, 1.0 - (float(E) * InsetPx * 2.0) / RefDim);
                    VectorInstanceEntry Copy = Src;
                    Copy.TransformRow0 = float4(Src.TransformRow0.x * Shrink, Src.TransformRow0.y * Shrink, Src.TransformRow0.z, Src.TransformRow0.w * Shrink);
                    Copy.TransformRow1 = float4(Src.TransformRow1.x * Shrink, Src.TransformRow1.y * Shrink, Src.TransformRow1.z, Src.TransformRow1.w);
                    if ((E & 1u) != 0u)
                    {
                        Copy.ColorOpacityPacked = float4(Mod.TintBlock.rgb, 1.0);
                        Copy.PickMeta.w      = 1u;
                    }
                    Copy.PickMeta.y = NextCount;
                    OutInstances[FirstSlotOffset + NextCount] = Copy;
                    NextCount++;
                }
            }
            Count = NextCount;
        }
        else if (Cat == 0u) // NoiseScale
        {
            uint Alg    = Mod.Header.z;
            uint Target = Mod.Header.w;
            float MinS  = max(0.05, Mod.ScalarBlockA.x);
            float MaxS  = max(MinS, Mod.ScalarBlockB.x);
            float Freq  = max(0.1,  Mod.ScalarBlockB.y);
            for (uint I = 0u; I < Count; ++I)
            {
                VectorInstanceEntry Entry = OutInstances[FirstSlotOffset + I];
                float2 NormP = float2(Entry.TransformRow0.z, Entry.TransformRow1.z) / DocExtent;
                float N      = saturate(EvaluateNoiseAlgorithm(Alg, NormP, Freq, LayerId * 97u + 31u));
                float S      = lerp(MinS, MaxS, N);
                Entry.TransformRow0.x *= S; Entry.TransformRow0.y *= S; Entry.TransformRow0.w *= S;
                Entry.TransformRow1.x *= S; Entry.TransformRow1.y *= S;
                if (Target == 0u)
                {
                    Entry.ColorOpacityPacked = float4(Mod.TintBlock.rgb, N);
                    Entry.PickMeta.w      = 2u;
                }
                else if (Target == 2u)
                {
                    Entry.ModParams.x *= lerp(0.25, 1.0, N);
                }
                else if (Target == 3u)
                {
                    float Delta = (N - 0.5) * 1.5707963;
                    float Cd = cos(Delta);
                    float Sd = sin(Delta);
                    float A00 = Entry.TransformRow0.x, A01 = Entry.TransformRow0.y;
                    float A10 = Entry.TransformRow1.x, A11 = Entry.TransformRow1.y;
                    Entry.TransformRow0.x = A00 * Cd - A01 * Sd;
                    Entry.TransformRow0.y = A00 * Sd + A01 * Cd;
                    Entry.TransformRow1.x = A10 * Cd - A11 * Sd;
                    Entry.TransformRow1.y = A10 * Sd + A11 * Cd;
                }
                Entry.ModParams.z = N;
                OutInstances[FirstSlotOffset + I] = Entry;
            }
        }
        else if (Cat == 5u) // ScatterDropout
        {
            float PosJitter = Mod.ScalarBlockA.x;
            float RotJitter = Mod.ScalarBlockB.x * 0.0174532925;
            float DropProb  = saturate(Mod.ScalarBlockC.x);
            uint  Kept      = 0u;
            for (uint I = 0u; I < Count; ++I)
            {
                float DropRoll = float(VectorHashU32(I * 2654435761u ^ LayerId * 747796405u) & 0xFFFFu) / 65535.0;
                if (DropRoll < DropProb)
                {
                    continue;
                }
                float Jx = (float(VectorHashU32(I * 1597334677u ^ 0x123u) & 0xFFFFu) / 32767.5 - 1.0) * PosJitter;
                float Jy = (float(VectorHashU32(I * 3812015801u ^ 0x456u) & 0xFFFFu) / 32767.5 - 1.0) * PosJitter;
                float Jr = (float(VectorHashU32(I * 2798796415u ^ 0x789u) & 0xFFFFu) / 32767.5 - 1.0) * RotJitter;
                float Cj = cos(Jr);
                float Sj = sin(Jr);
                VectorInstanceEntry Entry = OutInstances[FirstSlotOffset + I];
                float A00 = Entry.TransformRow0.x, A01 = Entry.TransformRow0.y;
                float A10 = Entry.TransformRow1.x, A11 = Entry.TransformRow1.y;
                Entry.TransformRow0.x = A00 * Cj - A01 * Sj;
                Entry.TransformRow0.y = A00 * Sj + A01 * Cj;
                Entry.TransformRow0.z += Jx;
                Entry.TransformRow1.x = A10 * Cj - A11 * Sj;
                Entry.TransformRow1.y = A10 * Sj + A11 * Cj;
                Entry.TransformRow1.z += Jy;
                Entry.PickMeta.y       = Kept;
                OutInstances[Kept]     = Entry;
                Kept++;
            }
            for (uint K = 0u; K < Kept; ++K) OutInstances[FirstSlotOffset + K] = OutInstances[K];
            Count = Kept;
        }
        else if (Cat == 6u) // DomainWarp
        {
            uint  WMode = Mod.Header.z;
            float WAmp  = Mod.ScalarBlockA.x;
            for (uint I = 0u; I < Count; ++I)
            {
                VectorInstanceEntry Entry = OutInstances[FirstSlotOffset + I];
                Entry.ModParams.y = float(WMode);
                Entry.ModParams.w = WAmp;
                OutInstances[FirstSlotOffset + I] = Entry;
            }
        }
    }

    uint RawCount  = min(Count, MaxOut);
    uint KeptCount = 0u;
    for (uint I = 0u; I < RawCount; ++I)
    {
        VectorInstanceEntry Entry = OutInstances[FirstSlotOffset + I];
        if (Entry.ModParams.x > 0.0)
        {
            uint ScaleFixed = uint(clamp(Entry.TransformRow0.w * 10000.0, 100.0, 1000000.0));
            InterlockedMin(SummaryBuffer[0].MinScaleFixed, ScaleFixed);
            InterlockedMax(SummaryBuffer[0].MaxScaleFixed, ScaleFixed);
            KeptCount++;
        }
    }
    for (uint Pad = RawCount; Pad < MaxOut; ++Pad)
    {
        VectorInstanceEntry Culled;
        Culled.TransformRow0   = float4(0.0, 0.0, -10000.0, 0.0);
        Culled.TransformRow1   = float4(0.0, 0.0, -10000.0, 0.0);
        Culled.ColorOpacityPacked = float4(0.0, 0.0, 0.0, 0.0);
        Culled.ModParams       = float4(0.0, 0.0, 0.0, 0.0);
        Culled.PickMeta        = uint4(0u, 0u, 0u, 0u);
        OutInstances[FirstSlotOffset + Pad] = Culled;
    }
    InterlockedAdd(SummaryBuffer[0].InstanceCount, KeptCount);
}
