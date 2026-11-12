//============================================================================================================================================
//                                                        UVDISTORTIONINSPECTION.SLANG
//============================================================================================================================================
// Parallel GPU UV distortion, signed UV area, winding-flip & exact 2D SAT triangle-overlap inspection feeding TimelineTransferRegister

struct FaceMetricInput
{
    float4 Pos0AndU0; // xyz = P0, w = UV0.x
    float4 Pos1AndV0; // xyz = P1, w = UV0.y
    float4 Pos2AndU1; // xyz = P2, w = UV1.x
    float4 UVRest;    // x = UV1.y, y = UV2.x, z = UV2.y, w = Area3DFullFace
    float4 UVBounds;  // xy = FaceUVMin, zw = FaceUVMax
};

struct FaceMetricOutput
{
    float L2Stretch;    // Ideal isometric = 1.0 (normalized by global texel scale)
    float SignedAreaUV; // > 0 valid CCW, <= 0 flipped UV face
    uint  DistortionMask;  // bit 0 = FlippedWinding, bit 1 = OverlappingInterior
    uint  UDIMNumber;   // 1001 + TileU + 10 * TileV
};

struct MetricDispatchParams
{
    uint  FaceCount;
    float ReferenceUVTo3DScale;
    uint  CheckOverlaps;
    float MaxFaceSpanU;
};

[[vk::binding(0, 0)]] ConstantBuffer<MetricDispatchParams> Params;
[[vk::binding(1, 0)]] StructuredBuffer<FaceMetricInput>    FaceInputs;
[[vk::binding(2, 0)]] RWStructuredBuffer<FaceMetricOutput> FaceOutputs;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint>             AggregateCounters; // [0] = FlippedCount, [1] = OverlapCount, [2] = StretchScaledSum

bool AxisSeparatesTriangles(float2 EdgeStart, float2 EdgeEnd, float2 A0, float2 A1, float2 A2, float2 B0, float2 B1, float2 B2)
{
    float2 Edge = EdgeEnd - EdgeStart;
    float  Len  = length(Edge);
    if (Len < 1.0e-7)
    {
        return false;
    }
    float2 Normal = float2(-Edge.y, Edge.x) / Len;

    float ProjA0 = dot(A0, Normal);
    float ProjA1 = dot(A1, Normal);
    float ProjA2 = dot(A2, Normal);
    float MinA   = min(ProjA0, min(ProjA1, ProjA2));
    float MaxA   = max(ProjA0, max(ProjA1, ProjA2));

    float ProjB0 = dot(B0, Normal);
    float ProjB1 = dot(B1, Normal);
    float ProjB2 = dot(B2, Normal);
    float MinB   = min(ProjB0, min(ProjB1, ProjB2));
    float MaxB   = max(ProjB0, max(ProjB1, ProjB2));

    // Shared edges/vertices touch within 1e-4 UV tolerance; require > 2.5e-4 penetration to count as interior overlap
    const float PenetrationEps = 2.5e-4;
    return (MaxA <= MinB + PenetrationEps) || (MaxB <= MinA + PenetrationEps);
}

bool TrianglesOverlapInterior(float2 A0, float2 A1, float2 A2, float2 B0, float2 B1, float2 B2)
{
    if (AxisSeparatesTriangles(A0, A1, A0, A1, A2, B0, B1, B2)) return false;
    if (AxisSeparatesTriangles(A1, A2, A0, A1, A2, B0, B1, B2)) return false;
    if (AxisSeparatesTriangles(A2, A0, A0, A1, A2, B0, B1, B2)) return false;
    if (AxisSeparatesTriangles(B0, B1, A0, A1, A2, B0, B1, B2)) return false;
    if (AxisSeparatesTriangles(B1, B2, A0, A1, A2, B0, B1, B2)) return false;
    if (AxisSeparatesTriangles(B2, B0, A0, A1, A2, B0, B1, B2)) return false;
    return true;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint FaceIdx = DispatchThreadID.x;
    if (FaceIdx >= Params.FaceCount)
    {
        return;
    }

    FaceMetricInput In = FaceInputs[FaceIdx];
    float3 P0 = In.Pos0AndU0.xyz;
    float3 P1 = In.Pos1AndV0.xyz;
    float3 P2 = In.Pos2AndU1.xyz;

    float2 T0 = float2(In.Pos0AndU0.w, In.Pos1AndV0.w);
    float2 T1 = float2(In.Pos2AndU1.w, In.UVRest.x);
    float2 T2 = float2(In.UVRest.y, In.UVRest.z);

    float2 dT1         = T1 - T0;
    float2 dT2         = T2 - T0;
    float  TwoSignedUV = dT1.x * dT2.y - dT1.y * dT2.x;
    float  SignedUV    = 0.5 * TwoSignedUV;

    float StretchL2 = 1.0;
    if (abs(TwoSignedUV) > 1.0e-12)
    {
        float3 Q1     = P1 - P0;
        float3 Q2     = P2 - P0;
        float  InvDet = 1.0 / TwoSignedUV;
        float3 Ss     = (Q1 * dT2.y - Q2 * dT1.y) * InvDet;
        float3 St     = (Q2 * dT1.x - Q1 * dT2.x) * InvDet;
        float  RawL2  = sqrt(max(0.5 * (dot(Ss, Ss) + dot(St, St)), 1.0e-8));
        StretchL2     = RawL2 * max(Params.ReferenceUVTo3DScale, 1.0e-5);
    }
    else
    {
        StretchL2 = 8.0;
    }

    uint InspectionMask = 0u;
    if (SignedUV <= 0.0)
    {
        InspectionMask |= 1u;
        InterlockedAdd(AggregateCounters[0u], 1u);
    }

    float2 CentroidUV = (In.UVBounds.xy + In.UVBounds.zw) * 0.5;
    int    TileU      = clamp(int(floor(CentroidUV.x)), 0, 9);
    int    TileV      = max(int(floor(CentroidUV.y)), 0);
    uint   UDIMCode   = uint(1001 + TileU + 10 * TileV);

    if (Params.CheckOverlaps != 0u)
    {
        float2 MyMin        = In.UVBounds.xy;
        float2 MyMax        = In.UVBounds.zw;
        float  SearchFloorU = MyMin.x - Params.MaxFaceSpanU - 1.0e-4;
        uint   Lo           = 0u;
        uint   Hi           = Params.FaceCount;
        while (Lo < Hi)
        {
            uint Mid = (Lo + Hi) >> 1u;
            if (FaceInputs[Mid].UVBounds.x < SearchFloorU)
            {
                Lo = Mid + 1u;
            }
            else
            {
                Hi = Mid;
            }
        }

        bool FoundOverlap = false;
        for (uint OtherIdx = Lo; OtherIdx < Params.FaceCount; ++OtherIdx)
        {
            FaceMetricInput OtherIn = FaceInputs[OtherIdx];
            float4          OtherB  = OtherIn.UVBounds;
            if (OtherB.x >= MyMax.x - 1.0e-4)
            {
                break;
            }
            if (OtherIdx != FaceIdx)
            {
                bool BoxSeparated =
                    (MyMax.x <= OtherB.x + 1.0e-4) || (MyMin.x >= OtherB.z - 1.0e-4) ||
                    (MyMax.y <= OtherB.y + 1.0e-4) || (MyMin.y >= OtherB.w - 1.0e-4);
                if (!BoxSeparated)
                {
                    float2 U0 = float2(OtherIn.Pos0AndU0.w, OtherIn.Pos1AndV0.w);
                    float2 U1 = float2(OtherIn.Pos2AndU1.w, OtherIn.Pos2AndU1.w * 0.0 + OtherIn.UVRest.x);
                    float2 U2 = float2(OtherIn.UVRest.y, OtherIn.UVRest.z);
                    if (TrianglesOverlapInterior(T0, T1, T2, U0, U1, U2))
                    {
                        FoundOverlap = true;
                        break;
                    }
                }
            }
        }
        if (FoundOverlap)
        {
            InspectionMask |= 2u;
            InterlockedAdd(AggregateCounters[1u], 1u);
        }
    }

    InterlockedAdd(AggregateCounters[2u], uint(clamp(StretchL2, 0.0, 100.0) * 1000.0));

    uint OriginalFaceIdx = asuint(In.UVRest.w);
    if (OriginalFaceIdx >= Params.FaceCount)
    {
        OriginalFaceIdx = FaceIdx;
    }

    FaceMetricOutput Out;
    Out.L2Stretch                = StretchL2;
    Out.SignedAreaUV             = SignedUV;
    Out.DistortionMask              = InspectionMask;
    Out.UDIMNumber               = UDIMCode;
    FaceOutputs[OriginalFaceIdx] = Out;
}
