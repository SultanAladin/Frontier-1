/*==========================================================================================================================================
                                                  RECTANGULARRECONNECTION.SLANG
==========================================================================================================================================*/
// 🧩 Experimental path-vertex reconnection for one unobstructed, uniformly emissive rectangle.
// 📝 All receivers face +Z below the rectangle. Only rigid rectangle translation is carried;
//    support is common to every accepted source domain. This is not general ReSTIR GI.
#include "LightInfluence.slang"
uint RectangleHistoryStart() { return Extent.Dimensions.x * 25u + 13889u + SolarSourceSampleCount; }
uint RectangleSlot(uint Slot, uint Pixel) { return RectangleHistoryStart() + Slot * Extent.Dimensions.x + Pixel; }
float3 RectangleDirection(inout uint State)
{
    float Radius = sqrt(RandomUnit(State)), Phi = 6.283185307179586 * RandomUnit(State);
    return float3(Radius * cos(Phi), Radius * sin(Phi), sqrt(max(0, 1 - Radius * Radius)));
}
float RectangleTarget(float3 Position, float3 Albedo, float3 Endpoint)
{
    float3 Delta = Endpoint - (Position + float3(0, 0, .001));
    float Distance = length(Delta);
    return Distance > 0 ? max(0, Power(Albedo * Facets[0].Emissive.xyz)) * max(0, Delta.z / Distance) /
                              3.141592653589793 : 0;
}
// dω_destination / dω_source for an area-preserving translated endpoint. Origin includes the ray bias.
float RectangleJacobian(float3 Source, float3 Destination, float3 OldEndpoint, float3 NewEndpoint)
{
    float3 A = OldEndpoint - (Source + float3(0, 0, .001));
    float3 B = NewEndpoint - (Destination + float3(0, 0, .001));
    float A2 = dot(A, A), B2 = dot(B, B);
    if (A2 <= 0 || B2 <= 0 || A.z <= 0 || B.z <= 0) return 0;
    float OldMeasure = A.z / (A2 * sqrt(A2));
    float NewMeasure = B.z / (B2 * sqrt(B2));
    return NewMeasure / OldMeasure;
}
[shader("compute")][numthreads(64, 1, 1)] void GenerateRectanglePaths(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint R = Receivers[Pixel];
    uint State = Extent.Sampling.x ^ (Pixel * 747796405u + 2891336453u);
    float3 Selected = 0;
    float Sum = 0, Count = 0;
    if (R.PositionHit.w != 0 && R.MotionPositionToken.w != 0)
    {
        float3 Direction = RectangleDirection(State), Strike;
        float3 Origin = R.PositionHit.xyz + float3(0, 0, .001);
        uint Hit = NearestFacet(Origin, Direction, .0001, 1e30, Strike);
        Count = 1; // An escaped ray is a zero-valued proposal, not a missing draw.
        if (Hit != 0xffffffffu)
        {
            Selected = Origin + Direction * Strike.x;
            Sum = max(0, Power(R.AlbedoIor.xyz * Facets[Hit].Emissive.xyz));
        }
        if (Extent.Reconstruction.x != 0 && Extent.History.z != 0 && Extent.History.w == 0 && R.MotionPixel.w != 0)
        {
            int2 PreviousPixel = int2(floor(R.MotionPixel.xy + .5));
            if (all(PreviousPixel >= 0) && all(PreviousPixel < int2(Extent.History.xy)))
            {
                uint Previous = uint(PreviousPixel.y) * Extent.History.x + uint(PreviousPixel.x);
                float4 Prior = OutputColour[RectangleSlot(0, Previous)];
                float4 Domain = OutputColour[RectangleSlot(1, Previous)];
                float4 Material = OutputColour[RectangleSlot(2, Previous)];
                bool Compatible = Domain.w > 0 && Material.w == R.MotionPositionToken.w &&
                                  all(Domain.xyz == R.MotionPositionToken.xyz) && all(Material.xyz == R.AlbedoIor.xyz);
                if (Compatible)
                {
                    float Retained = min(Domain.w, 64.0);
                    Count += Retained;
                    if (Prior.w > 0)
                    {
                        float3 Carried = Prior.xyz + Extent.Minimum.xyz;
                        float PreviousTarget = RectangleTarget(Domain.xyz, Material.xyz, Prior.xyz);
                        float CurrentTarget = RectangleTarget(R.PositionHit.xyz, R.AlbedoIor.xyz, Carried);
                        float Jacobian = Extent.Reconstruction.z == 0 ?
                            RectangleJacobian(Domain.xyz, R.PositionHit.xyz, Prior.xyz, Carried) : 1;
                        float Weight = PreviousTarget > 0 ? CurrentTarget * Prior.w * Retained * Jacobian /
                                                                   (Domain.w * PreviousTarget) : 0;
                        Sum += Weight;
                        if (Weight > 0 && RandomUnit(State) * Sum < Weight) Selected = Carried;
                    }
                    InterlockedAdd(Counters[20], 1);
                    if (any(Extent.Minimum.xyz != 0)) InterlockedAdd(Counters[23], 1);
                }
                else InterlockedAdd(Counters[21], 1);
            }
        }
    }
    OutputColour[RectangleSlot(3, Pixel)] = float4(Selected, Sum);
    OutputColour[RectangleSlot(4, Pixel)] = float4(Count, 0, 0, 0);
}
[shader("compute")][numthreads(64, 1, 1)] void ResolveRectanglePaths(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint R = Receivers[Pixel];
    float4 Local = OutputColour[RectangleSlot(3, Pixel)];
    float LocalCount = OutputColour[RectangleSlot(4, Pixel)].x;
    float3 Selected = 0;
    float Sum = 0, Count = 0;
    uint State = Extent.Sampling.x ^ (Pixel * 277803737u + 1597334677u);
    int2 Centre = int2(Pixel % Extent.History.x, Pixel / Extent.History.x);
    static const int2 Offsets[5] = {int2(0,0), int2(-1,0), int2(1,0), int2(0,-1), int2(0,1)};
    uint Trials = Extent.Reconstruction.y != 0 ? 5u : 1u;
    for (uint I = 0; I < Trials; ++I)
    {
        int2 P = Centre + Offsets[I];
        if (any(P < 0) || any(P >= int2(Extent.History.xy))) continue;
        uint DonorIndex = uint(P.y) * Extent.History.x + uint(P.x);
        SurfacePoint Donor = Receivers[DonorIndex];
        if (R.PositionHit.w == 0 || Donor.PositionHit.w == 0 || R.MotionPositionToken.w == 0 ||
            R.MotionPositionToken.w != Donor.MotionPositionToken.w || any(R.AlbedoIor.xyz != Donor.AlbedoIor.xyz))
            continue;
        float4 Candidate = OutputColour[RectangleSlot(3, DonorIndex)];
        float DonorCount = OutputColour[RectangleSlot(4, DonorIndex)].x;
        if (DonorCount <= 0) continue;
        Count += DonorCount;
        if (I != 0) InterlockedAdd(Counters[22], 1);
        if (Candidate.w <= 0) continue;
        float PreviousTarget = RectangleTarget(Donor.PositionHit.xyz, Donor.AlbedoIor.xyz, Candidate.xyz);
        float CurrentTarget = RectangleTarget(R.PositionHit.xyz, R.AlbedoIor.xyz, Candidate.xyz);
        float Jacobian = Extent.Reconstruction.z == 0 ?
            RectangleJacobian(Donor.PositionHit.xyz, R.PositionHit.xyz, Candidate.xyz, Candidate.xyz) : 1;
        float Weight = PreviousTarget > 0 ? CurrentTarget * Candidate.w * Jacobian / PreviousTarget : 0;
        Sum += Weight;
        if (Weight > 0 && RandomUnit(State) * Sum < Weight) Selected = Candidate.xyz;
    }
    float Target = RectangleTarget(R.PositionHit.xyz, R.AlbedoIor.xyz, Selected);
    float3 Direction = Unit(Selected - (R.PositionHit.xyz + float3(0, 0, .001)));
    float3 Contribution = R.AlbedoIor.xyz * Facets[0].Emissive.xyz * max(0, Direction.z) / 3.141592653589793;
    float3 Estimate = Count > 0 && Target > 0 ? Contribution * (Sum / (Count * Target)) : float3(0);
    OutputColour[Pixel] = float4(Estimate, 1);
    // Persist pre-spatial history so a donor's mixture of domains is never copied to each neighbor.
    OutputColour[RectangleSlot(0, Pixel)] = Local;
    OutputColour[RectangleSlot(1, Pixel)] = float4(R.PositionHit.xyz, LocalCount);
    OutputColour[RectangleSlot(2, Pixel)] = float4(R.AlbedoIor.xyz, R.PositionHit.w != 0 ? R.MotionPositionToken.w : 0);
}
