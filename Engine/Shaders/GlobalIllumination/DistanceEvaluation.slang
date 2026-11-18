/*============================================================================================================================================
                                                          DISTANCEEVALUATION.SLANG
============================================================================================================================================*/
// 🧩 Native Vulkan local distance baking, toroidal clipmap fill, conservative shadow queries and resident diffuse-cache gathering.

module DistanceEvaluation;

//------------------------------------------------------------------------------------------------------------------------
//                                                       FLOAT32 GPU FOOTPRINTS
//------------------------------------------------------------------------------------------------------------------------

struct EnclosureFootprint
{
    float4 Minimum;
    float4 Maximum;
    uint4  Links;
};
struct FacetFootprint
{
    float3 A;
    uint   Material;
    float4 B;
    float4 C;
};
struct BoxFootprint
{
    float4 Centre;
    float4 HalfExtent;
};
struct ProbeFootprint
{
    float4 OriginLimit;
    float4 Direction;
    float4 Position;
};
struct DistanceFootprint
{
    float4 LocalMinimum;
    float4 LocalSpacing;
    uint4  ExtentAndCount;
    float4 PoseCosineSine;
    float4 Translation;
    int4   ClipOrigin0;
    int4   ClipOrigin1;
    int4   ClipOrigin2;
};

[[vk::binding(0, 0)]] StructuredBuffer<EnclosureFootprint> Enclosures;
[[vk::binding(1, 0)]] StructuredBuffer<FacetFootprint>     Facets;
[[vk::binding(2, 0)]] RWStructuredBuffer<float>            LocalDistances;
[[vk::binding(3, 0)]] RWStructuredBuffer<float4>           ClipDistances;
[[vk::binding(4, 0)]] StructuredBuffer<ProbeFootprint>     Probes;
[[vk::binding(5, 0)]] RWStructuredBuffer<float4>           Results;
[[vk::binding(6, 0)]] StructuredBuffer<BoxFootprint>       Boxes;
[[vk::binding(7, 0)]] RWStructuredBuffer<float4>           SurfaceRadiance;
[[vk::push_constant]] ConstantBuffer<DistanceFootprint>    Parameters;

//------------------------------------------------------------------------------------------------------------------------
//                                                      TRIANGLE / BOX QUERIES
//------------------------------------------------------------------------------------------------------------------------

float3 Rotate(float3 P)
{
    return float3(Parameters.PoseCosineSine.x * P.x - Parameters.PoseCosineSine.y * P.y,
                  Parameters.PoseCosineSine.y * P.x + Parameters.PoseCosineSine.x * P.y,
                  P.z);
}
float3 Unrotate(float3 P)
{
    return float3(Parameters.PoseCosineSine.x * P.x + Parameters.PoseCosineSine.y * P.y,
                  -Parameters.PoseCosineSine.y * P.x + Parameters.PoseCosineSine.x * P.y,
                  P.z);
}
float3 ToLocal(float3 P)
{
    return Unrotate(P - Parameters.Translation.xyz);
}
float EnclosureSquared(float3 P, float3 Low, float3 High)
{
    const float3 Delta = max(max(Low - P, P - High), float3(0));
    return dot(Delta, Delta);
}
bool IntersectBox(float3 Origin, float3 Direction, float3 Low, float3 High, float Limit, out float Near)
{
    Near      = 0;
    float Far = Limit;
    for (uint Axis = 0; Axis < 3; ++Axis)
    {
        if (Direction[Axis] == 0)
        {
            if (Origin[Axis] < Low[Axis] || Origin[Axis] > High[Axis]) return false;
        }
        else
        {
            const float A = (Low[Axis] - Origin[Axis]) / Direction[Axis];
            const float B = (High[Axis] - Origin[Axis]) / Direction[Axis];
            Near          = max(Near, min(A, B));
            Far           = min(Far, max(A, B));
        }
    }
    return Near <= Far;
}
float TriangleSquared(float3 P, float3 A, float3 B, float3 C)
{
    const float3 EdgeAB = B - A, EdgeAC = C - A, Difference = P - A;
    const float3 Perpendicular = cross(EdgeAB, EdgeAC);
    const float  NormalSquared = dot(Perpendicular, Perpendicular);
    if (NormalSquared > 1e-22 && dot(cross(EdgeAB, Difference), Perpendicular) >= 0 &&
        dot(cross(C - B, P - B), Perpendicular) >= 0 && dot(cross(A - C, P - C), Perpendicular) >= 0)
    {
        const float Height = dot(Difference, Perpendicular);
        return Height * Height / NormalSquared;
    }
    float        Squared    = 1e30;
    const float3 Corners[3] = {A, B, C};
    for (uint Edge = 0; Edge < 3; ++Edge)
    {
        const float3 Direction   = Corners[(Edge + 1) % 3] - Corners[Edge];
        const float3 Offset      = P - Corners[Edge];
        const float  Denominator = dot(Direction, Direction);
        const float  Fraction    = Denominator > 0 ? clamp(dot(Offset, Direction) / Denominator, 0.0, 1.0) : 0;
        const float3 Separation  = Offset - Direction * Fraction;
        Squared                  = min(Squared, dot(Separation, Separation));
    }
    return Squared;
}
float Nearest(float3 P)
{
    float BestSquared = 1e30;
    uint  Pending[64];
    Pending[0] = 0;
    uint Count = 1;
    while (Count > 0)
    {
        const EnclosureFootprint Region = Enclosures[Pending[--Count]];
        if (EnclosureSquared(P, Region.Minimum.xyz, Region.Maximum.xyz) > BestSquared) continue;
        if (Region.Links.z == 0)
        {
            const EnclosureFootprint LowerRegion = Enclosures[Region.Links.x];
            const EnclosureFootprint UpperRegion = Enclosures[Region.Links.y];
            const float              Lower = EnclosureSquared(P, LowerRegion.Minimum.xyz, LowerRegion.Maximum.xyz);
            const float              Upper = EnclosureSquared(P, UpperRegion.Minimum.xyz, UpperRegion.Maximum.xyz);
            if (max(Lower, Upper) < BestSquared) Pending[Count++] = Region.Links[Lower < Upper ? 1 : 0];
            if (min(Lower, Upper) < BestSquared) Pending[Count++] = Region.Links[Lower < Upper ? 0 : 1];
            continue;
        }
        for (uint Slot = Region.Links.x; Slot < Region.Links.x + Region.Links.y; ++Slot)
        {
            const FacetFootprint Triangle = Facets[Slot];
            BestSquared = min(BestSquared, TriangleSquared(P, Triangle.A.xyz, Triangle.B.xyz, Triangle.C.xyz));
        }
    }
    return sqrt(BestSquared);
}
uint Trace(float3 Origin, float3 Direction, float Limit, bool Any)
{
    uint Found = 0xffffffff;
    uint Pending[64];
    Pending[0] = 0;
    uint Count = 1;
    while (Count > 0)
    {
        const EnclosureFootprint Region = Enclosures[Pending[--Count]];
        float                    Near;
        if (!IntersectBox(Origin, Direction, Region.Minimum.xyz, Region.Maximum.xyz, Limit, Near)) continue;
        if (Region.Links.z == 0)
        {
            float                    LowerNear, UpperNear;
            const EnclosureFootprint LowerRegion = Enclosures[Region.Links.x];
            const EnclosureFootprint UpperRegion = Enclosures[Region.Links.y];
            const bool               LowerHit =
                IntersectBox(Origin, Direction, LowerRegion.Minimum.xyz, LowerRegion.Maximum.xyz, Limit, LowerNear);
            const bool UpperHit =
                IntersectBox(Origin, Direction, UpperRegion.Minimum.xyz, UpperRegion.Maximum.xyz, Limit, UpperNear);
            if (LowerHit && UpperHit)
            {
                Pending[Count++] = Region.Links[LowerNear < UpperNear ? 1 : 0];
                Pending[Count++] = Region.Links[LowerNear < UpperNear ? 0 : 1];
            }
            else if (LowerHit)
                Pending[Count++] = Region.Links.x;
            else if (UpperHit)
                Pending[Count++] = Region.Links.y;
            continue;
        }
        for (uint Slot = Region.Links.x; Slot < Region.Links.x + Region.Links.y; ++Slot)
        {
            const FacetFootprint Triangle = Facets[Slot];
            const float3         EdgeAB = Triangle.B.xyz - Triangle.A.xyz, EdgeAC = Triangle.C.xyz - Triangle.A.xyz;
            const float3         Perpendicular = cross(Direction, EdgeAC);
            const float          Determinant   = dot(EdgeAB, Perpendicular);
            if (abs(Determinant) < 1e-12) continue;
            const float  Reciprocal   = 1 / Determinant;
            const float3 Displacement = Origin - Triangle.A.xyz;
            const float  U            = dot(Displacement, Perpendicular) * Reciprocal;
            if (U < 0 || U > 1) continue;
            const float3 SecondCross = cross(Displacement, EdgeAB);
            const float  V           = dot(Direction, SecondCross) * Reciprocal;
            if (V < 0 || U + V > 1) continue;
            const float Travel = dot(EdgeAC, SecondCross) * Reciprocal;
            if (Travel > 1e-5 && Travel < Limit)
            {
                if (Any) return Slot;
                Limit = Travel;
                Found = Slot;
            }
        }
    }
    return Found;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 CONSERVATIVE DISTANCE INTERVALS
//------------------------------------------------------------------------------------------------------------------------

uint LocalAddress(uint3 Coordinate)
{
    return (Coordinate.z * Parameters.ExtentAndCount.y + Coordinate.y) * Parameters.ExtentAndCount.x + Coordinate.x;
}
float2 SampleLocal(float3 P)
{
    const uint3  Extent = Parameters.ExtentAndCount.xyz;
    const float3 Low = Parameters.LocalMinimum.xyz, Spacing = Parameters.LocalSpacing.xyz;
    const float3 High    = Low + Spacing * float3(Extent - 1);
    const float  Outside = sqrt(EnclosureSquared(P, Low, High));
    if (Outside > 0) return float2(Outside, Outside);
    const float3 Q        = clamp((P - Low) / Spacing, float3(0), float3(Extent - 1));
    const uint3  Cell     = min(uint3(Q), Extent - 2);
    const float3 Fraction = Q - float3(Cell);
    float        Raw = 0, Correction = 0;
    for (uint Z = 0; Z < 2; ++Z)
        for (uint Y = 0; Y < 2; ++Y)
            for (uint X = 0; X < 2; ++X)
            {
                const float Weight = (X != 0 ? Fraction.x : 1 - Fraction.x) * (Y != 0 ? Fraction.y : 1 - Fraction.y) *
                                     (Z != 0 ? Fraction.z : 1 - Fraction.z);
                Raw += Weight * LocalDistances[LocalAddress(Cell + uint3(X, Y, Z))];
                Correction += Weight * length((float3(X, Y, Z) - Fraction) * Spacing);
            }
    return float2(Raw, max(0.0, Raw - Correction - 2e-5));
}
uint Wrap(int Coordinate)
{
    return uint((Coordinate % 48 + 48) % 48);
}
uint ClipAddress(int3 Coordinate)
{
    return (Wrap(Coordinate.z) * 48 + Wrap(Coordinate.y)) * 48 + Wrap(Coordinate.x);
}
int3 ClipOrigin(uint Tier)
{
    return Tier == 0 ? Parameters.ClipOrigin0.xyz : Tier == 1 ? Parameters.ClipOrigin1.xyz : Parameters.ClipOrigin2.xyz;
}
float SignedBox(float3 P, BoxFootprint Box)
{
    const float3 Q = abs(P - Box.Centre.xyz) - Box.HalfExtent.xyz;
    return length(max(Q, float3(0))) + min(max(Q.x, max(Q.y, Q.z)), 0.0);
}
float2 SampleClip(float3 P)
{
    for (uint Tier = 0; Tier < 3; ++Tier)
    {
        const float  Spacing = .125 * float(1u << Tier);
        const float3 Q       = P / Spacing;
        const int3   Cell = int3(floor(Q)), Origin = ClipOrigin(Tier);
        if (any(Cell < Origin) || any(Cell + 1 >= Origin + 48)) continue;
        const float3 Fraction = Q - float3(Cell);
        float        Raw = 0, Lower = 0;
        for (uint Z = 0; Z < 2; ++Z)
            for (uint Y = 0; Y < 2; ++Y)
                for (uint X = 0; X < 2; ++X)
                {
                    const float4 Texel  = ClipDistances[Tier * 48 * 48 * 48 + ClipAddress(Cell + int3(X, Y, Z))];
                    const float  Weight = (X != 0 ? Fraction.x : 1 - Fraction.x) *
                                         (Y != 0 ? Fraction.y : 1 - Fraction.y) *
                                         (Z != 0 ? Fraction.z : 1 - Fraction.z);
                    Raw += Weight * Texel.x;
                    Lower += Weight * (Texel.y - length(float3(X, Y, Z) - Fraction) * Spacing);
                }
        return float2(Raw, max(0.0, Lower - 2e-5));
    }
    return float2(0);
}
bool HybridOccluded(float3 Origin, float3 Direction, float Limit, out uint Steps)
{
    Steps                                = 0;
    const float3             LocalOrigin = ToLocal(Origin), LocalDirection = Unrotate(Direction);
    const EnclosureFootprint Enclosure = Enclosures[0];
    float                    Travel;
    if (!IntersectBox(LocalOrigin, LocalDirection, Enclosure.Minimum.xyz, Enclosure.Maximum.xyz, Limit, Travel))
        return false;
    for (uint Step = 0; Step < 96; ++Step)
    {
        ++Steps;
        const float Lower = SampleClip(Origin + Direction * Travel).y;
        if (Lower < 1e-4) break;
        Travel += Lower;
        if (Travel > Limit) return false;
        const float3 P = LocalOrigin + LocalDirection * Travel;
        if (any(P < Enclosure.Minimum.xyz - 1e-4) || any(P > Enclosure.Maximum.xyz + 1e-4)) return false;
    }
    return Trace(LocalOrigin, LocalDirection, Limit, true) != 0xffffffff;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       COMPUTE ENTRIES
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")][numthreads(64, 1, 1)] void BakeLocalDistance(uint3 DispatchCoordinate
                                                                 : SV_DispatchThreadID)
{
    const uint  Index  = DispatchCoordinate.x;
    const uint3 Extent = Parameters.ExtentAndCount.xyz;
    if (Index >= Extent.x * Extent.y * Extent.z) return;
    const uint3  Coordinate = uint3(Index % Extent.x, (Index / Extent.x) % Extent.y, Index / (Extent.x * Extent.y));
    const float3 P          = Parameters.LocalMinimum.xyz + float3(Coordinate) * Parameters.LocalSpacing.xyz;
    LocalDistances[Index]   = Nearest(P);
}

    [shader("compute")][numthreads(64, 1, 1)] void FillClipmap(uint3 DispatchCoordinate
                                                               : SV_DispatchThreadID)
{
    const uint Index = DispatchCoordinate.x;
    if (Index >= 3 * 48 * 48 * 48) return;
    const uint   Tier = Index / (48 * 48 * 48), CellIndex = Index % (48 * 48 * 48);
    const int3   Cell     = int3(CellIndex % 48, (CellIndex / 48) % 48, CellIndex / (48 * 48)) + ClipOrigin(Tier);
    const float3 P        = float3(Cell) * (.125 * float(1u << Tier));
    const float2 Local    = SampleLocal(ToLocal(P));
    float4       Distance = float4(min(1.0, Local.x), min(1.0, Local.y), 1, 0);
    for (uint IndexBox = 0; IndexBox < uint(Parameters.LocalMinimum.w); ++IndexBox)
    {
        const float Signed = SignedBox(P, Boxes[IndexBox]);
        Distance.z         = min(Distance.z, Signed);
        Distance.x         = min(Distance.x, abs(Signed));
        Distance.y         = min(Distance.y, max(0.0, abs(Signed) - 2e-5));
    }
    Distance.w                                             = Distance.x < .08 ? 1 : 0;
    ClipDistances[Tier * 48 * 48 * 48 + ClipAddress(Cell)] = Distance;
}

[shader("compute")][numthreads(64, 1, 1)] void EvaluateDistanceProbes(uint3 DispatchCoordinate
                                                                      : SV_DispatchThreadID)
{
    const uint Index = DispatchCoordinate.x;
    if (Index >= Parameters.ExtentAndCount.w) return;
    const ProbeFootprint Probe = Probes[Index];
    uint                 Steps;
    const bool Occluded = HybridOccluded(Probe.OriginLimit.xyz, Probe.Direction.xyz, Probe.OriginLimit.w, Steps);
    Results[Index]      = float4(
        Occluded ? 1 : 0, SampleLocal(ToLocal(Probe.Position.xyz)).x, SampleClip(Probe.Position.xyz).y, float(Steps));
}

float3 Reflectance(uint Material)
{
    switch (Material)
    {
    case 6:
        return float3(.045, .25, .12);
    case 7:
        return float3(.035, .05, .065);
    case 8:
        return float3(.12, .13, .14);
    case 9:
        return float3(.03741);
    case 10:
        return float3(.06, .22, .62);
    case 11:
        return float3(.30, .075, .025);
    case 12:
    case 13:
    case 14:
        return float3(.8);
    default:
        return float3(.65, .65, .63);
    }
}

[shader("compute")][numthreads(64, 1, 1)] void RelightVehicleCache(uint3 DispatchCoordinate
                                                                   : SV_DispatchThreadID)
{
    const uint Index      = DispatchCoordinate.x;
    const uint FacetCount = uint(Parameters.PoseCosineSine.z);
    if (Index >= FacetCount * 2) return;
    const FacetFootprint Triangle = Facets[Index % FacetCount];
    const float3 P = Rotate((Triangle.A.xyz + Triangle.B.xyz + Triangle.C.xyz) / 3) + Parameters.Translation.xyz;
    const float3 Perpendicular = cross(Triangle.B.xyz - Triangle.A.xyz, Triangle.C.xyz - Triangle.A.xyz);
    const float  Magnitude     = length(Perpendicular);
    const float3 N = Magnitude > 0 ? Rotate(Perpendicular / Magnitude) * (Index >= FacetCount ? -1.0 : 1.0) : float3(0);
    const float3 Delta   = float3(1.5, -3.2, 3.6) - P;
    const float  Squared = dot(Delta, Delta), Travel = sqrt(Squared);
    const float3 Direction       = Delta / Travel;
    const float  Cosine          = max(0.0, dot(N, Direction));
    const float3 Origin          = P + N * 2e-4;
    const float3 ShadowDelta     = float3(1.5, -3.2, 3.6) - Origin;
    const float  ShadowTravel    = length(ShadowDelta);
    const float3 ShadowDirection = ShadowDelta / ShadowTravel;
    bool         Occluded        = false;
    if (Cosine > 0)
    {
        Occluded = Trace(ToLocal(Origin), Unrotate(ShadowDirection), ShadowTravel - 2e-4, true) != 0xffffffff;
        for (uint BoxIndex = 0; BoxIndex < uint(Parameters.LocalMinimum.w) && !Occluded; ++BoxIndex)
        {
            const BoxFootprint Box = Boxes[BoxIndex];
            float              Near;
            Occluded = IntersectBox(Origin,
                                    ShadowDirection,
                                    Box.Centre.xyz - Box.HalfExtent.xyz,
                                    Box.Centre.xyz + Box.HalfExtent.xyz,
                                    ShadowTravel - 2e-4,
                                    Near);
        }
    }
    const float3 Radiance =
        Occluded ? float3(0)
                 : Reflectance(Triangle.Material) * float3(70, 56, 42) * (Cosine / (Squared * 3.14159265358979323846));
    SurfaceRadiance[Index] = float4(Radiance, 1);
}

    [shader("compute")][numthreads(64, 1, 1)] void GatherVehicleCache(uint3 DispatchCoordinate
                                                                      : SV_DispatchThreadID)
{
    const uint Index = DispatchCoordinate.x;
    if (Index >= Parameters.ExtentAndCount.w) return;
    const ProbeFootprint Probe          = Probes[Index];
    const float3         LocalDirection = Unrotate(Probe.Direction.xyz);
    const uint           Slot = Trace(ToLocal(Probe.OriginLimit.xyz), LocalDirection, Probe.OriginLimit.w, false);
    if (Slot == 0xffffffff)
    {
        Results[Index] = float4(0);
        return;
    }
    const FacetFootprint Triangle = Facets[Slot];
    const bool           ReverseSide =
        dot(cross(Triangle.B.xyz - Triangle.A.xyz, Triangle.C.xyz - Triangle.A.xyz), LocalDirection) > 0;
    const uint CacheSlot = Slot + (ReverseSide ? uint(Parameters.PoseCosineSine.z) : 0u);
    Results[Index]       = float4(SurfaceRadiance[CacheSlot].xyz, 1);
}

[shader("compute")][numthreads(64, 1, 1)] void ClearVehicleCache(uint3 DispatchCoordinate
                                                                 : SV_DispatchThreadID)
{
    if (DispatchCoordinate.x < uint(Parameters.PoseCosineSine.z) * 2) SurfaceRadiance[DispatchCoordinate.x] = float4(0);
}
