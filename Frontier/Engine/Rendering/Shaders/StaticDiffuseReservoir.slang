/*==========================================================================================================================================
                                                    STATICDIFFUSERESERVOIR.SLANG
==========================================================================================================================================*/
// 🧩 Experimental cosine-sampled diffuse path reservoir, not general ReSTIR GI reconnection.
// 📝 Only identical receiver domains and unchanged opaque, untextured diffuse scenes are reusable.
#include "LightInfluence.slang"
uint DiffuseHistoryStart() { return Extent.Dimensions.x * 25u + 13889u + SolarSourceSampleCount; }
uint DiffuseSlot(uint Slot, uint Pixel) { return DiffuseHistoryStart() + Slot * Extent.Dimensions.x + Pixel; }
float3 CosineDirection(float3 Normal, inout uint State)
{
    float Radius = sqrt(RandomUnit(State)), Azimuth = 6.283185307179586 * RandomUnit(State);
    float3 Tangent = Unit(cross(abs(Normal.z) < .999 ? float3(0, 0, 1) : float3(0, 1, 0), Normal));
    float3 Bitangent = cross(Normal, Tangent);
    return Unit(Tangent * (Radius * cos(Azimuth)) + Bitangent * (Radius * sin(Azimuth)) +
                Normal * sqrt(max(0, 1 - Radius * Radius)));
}
// Two-sided triangle emission; no environment, analytic lights, glass, textures, specular or volumes.
float3 SampleDiffusePath(SurfacePoint Receiver, inout uint State)
{
    float3 Normal = Unit(Receiver.NormalRoughness.xyz);
    float3 Origin = Receiver.PositionHit.xyz + Normal * .001;
    float3 Throughput = clamp(Receiver.AlbedoIor.xyz, 0, 1), Radiance = 0;
    for (uint Bounce = 0; Bounce < Extent.Primary.z; ++Bounce)
    {
        float3 Direction = CosineDirection(Normal, State), Strike;
        uint Index = NearestFacet(Origin, Direction, .0001, 1e30, Strike);
        if (Index == 0xffffffffu) break;
        FacetCorners F = Facets[Index];
        if (F.Emissive.w > 0 || F.OpticalToken.x != 0) break;
        Radiance += Throughput * max(F.Emissive.xyz, 0);
        Throughput *= clamp(F.AlbedoRoughness.xyz, 0, 1);
        if (max(Throughput.x, max(Throughput.y, Throughput.z)) <= 0) break;
        Normal = Unit(F.NormalA.xyz * (1 - Strike.y - Strike.z) + F.NormalB.xyz * Strike.y + F.NormalC.xyz * Strike.z);
        if (dot(Normal, Direction) > 0) Normal = -Normal;
        Origin = Origin + Direction * Strike.x + Normal * .001;
    }
    return Radiance;
}
bool SameDiffuseDomain(SurfacePoint A, SurfacePoint B)
{
    return A.PositionHit.w != 0 && B.PositionHit.w != 0 && A.MotionPositionToken.w != 0 &&
           A.MotionPositionToken.w == B.MotionPositionToken.w && A.ViewMetallic.w == 0 && B.ViewMetallic.w == 0 &&
           all(A.PositionHit.xyz == B.PositionHit.xyz) && all(A.NormalRoughness.xyz == B.NormalRoughness.xyz) &&
           all(A.AlbedoIor.xyz == B.AlbedoIor.xyz);
}
[shader("compute")][numthreads(64, 1, 1)] void GenerateDiffusePaths(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint R = Receivers[Pixel];
    float4 Sample = 0;
    float Count = 0;
    uint State = Extent.Sampling.x ^ (Pixel * 747796405u + 2891336453u);
    if (R.PositionHit.w != 0 && R.MotionPositionToken.w != 0 && R.ViewMetallic.w == 0)
    {
        float3 Candidate = SampleDiffusePath(R, State);
        Sample = float4(Candidate, max(0, Power(Candidate)));
        Count = 1;
        if (Extent.Reconstruction.x != 0 && Extent.History.z != 0 && Extent.History.w == 0 && R.MotionPixel.w != 0)
        {
            int2 PreviousPixel = int2(floor(R.MotionPixel.xy + .5));
            if (all(PreviousPixel >= 0) && all(PreviousPixel < int2(Extent.History.xy)))
            {
                uint Previous = uint(PreviousPixel.y) * Extent.History.x + uint(PreviousPixel.x);
                float4 Position = OutputColour[DiffuseSlot(2, Previous)];
                float4 Normal = OutputColour[DiffuseSlot(3, Previous)];
                float4 Albedo = OutputColour[DiffuseSlot(4, Previous)];
                // Exact stored float domains; an epsilon can cross an occlusion edge.
                bool Same = Position.w == R.MotionPositionToken.w && Position.w != 0 &&
                            all(Position.xyz == R.MotionPositionToken.xyz) && all(Position.xyz == R.PositionHit.xyz) &&
                            all(Normal.xyz == R.NormalRoughness.xyz) && all(Albedo.xyz == R.AlbedoIor.xyz) &&
                            Albedo.w == R.ViewMetallic.w;
                if (Same)
                {
                    float4 Prior = OutputColour[DiffuseSlot(0, Previous)];
                    float PreviousCount = OutputColour[DiffuseSlot(1, Previous)].x;
                    float Retained = min(PreviousCount, 64.0);
                    float PriorSum = PreviousCount > 0 ? Prior.w * Retained / PreviousCount : 0;
                    Count += Retained;
                    float Sum = Sample.w + PriorSum;
                    if (PriorSum > 0 && RandomUnit(State) * Sum < PriorSum) Sample.xyz = Prior.xyz;
                    Sample.w = Sum;
                    InterlockedAdd(Counters[20], 1);
                }
                else InterlockedAdd(Counters[21], 1);
            }
        }
    }
    OutputColour[DiffuseSlot(5, Pixel)] = Sample;
    OutputColour[DiffuseSlot(6, Pixel)] = float4(Count, 0, 0, 0);
}
[shader("compute")][numthreads(64, 1, 1)] void ResolveDiffusePaths(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint R = Receivers[Pixel];
    float4 Local = OutputColour[DiffuseSlot(5, Pixel)];
    float LocalCount = OutputColour[DiffuseSlot(6, Pixel)].x;
    float3 Selected = 0;
    float WeightSum = 0, Count = 0;
    uint State = Extent.Sampling.x ^ (Pixel * 277803737u + 1597334677u);
    int2 Centre = int2(Pixel % Extent.History.x, Pixel / Extent.History.x);
    static const int2 Offsets[5] = {int2(0, 0), int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)};
    uint Trials = Extent.Reconstruction.y != 0 ? 5u : 1u;
    for (uint I = 0; I < Trials; ++I)
    {
        int2 P = Centre + Offsets[I];
        if (any(P < 0) || any(P >= int2(Extent.History.xy))) continue;
        uint Donor = uint(P.y) * Extent.History.x + uint(P.x);
        if (!SameDiffuseDomain(R, Receivers[Donor])) continue;
        float4 Source = OutputColour[DiffuseSlot(5, Donor)];
        float DonorCount = OutputColour[DiffuseSlot(6, Donor)].x;
        Count += DonorCount; // Include zero-weight paths in the proposal count.
        WeightSum += Source.w;
        if (Source.w > 0 && RandomUnit(State) * WeightSum < Source.w) Selected = Source.xyz;
        if (I != 0 && DonorCount > 0) InterlockedAdd(Counters[22], 1);
    }
    float Target = max(0, Power(Selected));
    float3 Estimate = Count > 0 && Target > 0 ? Selected * (WeightSum / (Count * Target)) : float3(0);
    OutputColour[Pixel] = float4(Estimate, 1);
    // Temporal state stays pre-spatial, avoiding duplicated aggregate domains next frame.
    OutputColour[DiffuseSlot(0, Pixel)] = Local;
    OutputColour[DiffuseSlot(1, Pixel)] = float4(LocalCount, 0, 0, 0);
    OutputColour[DiffuseSlot(2, Pixel)] = float4(R.PositionHit.xyz, R.PositionHit.w != 0 ? R.MotionPositionToken.w : 0);
    OutputColour[DiffuseSlot(3, Pixel)] = R.NormalRoughness;
    OutputColour[DiffuseSlot(4, Pixel)] = float4(R.AlbedoIor.xyz, R.ViewMetallic.w);
}
