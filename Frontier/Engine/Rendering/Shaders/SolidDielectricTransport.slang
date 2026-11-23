/*==========================================================================================================================================
                                                    SOLIDDIELECTRICTRANSPORT.SLANG
==========================================================================================================================================*/
// 🧩 Bounded smooth closed-dielectric paths in air; stochastic exact-Fresnel splitting and geometric-distance absorption.

#include "../../../../Engine/Render/Shader/DielectricRefraction.slang"

float3 TraceSolidDielectric(float3 Origin, float3 Direction, inout uint Bits, out uint InternalReflections,
                            out uint Capped, out uint Unsupported, out bool Hit, bool Compact, bool ExcludeUnscatteredEmission)
{
    const float Offset = .00002;
    float DistanceCorrection = 0;
    float3 Throughput = 1, Absorption = 0;
    float MediumIor = 1;
    uint MediumAddress = 0, CrossedPanes = 0;
    bool Inside = false, SolidSeen = false, Faint = false;
    uint SurvivalBits = Bits ^ 0xa511e9b3u;
    InternalReflections = Capped = Unsupported = 0;
    Hit = false;
    // 📝 Curved six-millimetre glazing can guide rays through hundreds of total internal reflections.
    for (uint Interaction = 0; Interaction < 2048; ++Interaction)
    {
        float3 Strike;
        uint Index = NearestFacet(Origin, Direction, Compact && Interaction == 0 ? 0 : -Offset * .25, 1e30, Strike);
        if (Index == 0xffffffffu)
        {
            if (Inside) { Unsupported = 1; return 0; }
            return Throughput * EnvironmentRadiance(Direction);
        }
        Hit = true;
        if (Inside) Throughput *= exp(-Absorption * max(0, Strike.x + DistanceCorrection));
        SurfacePoint R = FacetSurface(Index, Origin, Direction, Strike);
        uint Category = MaterialTransport(R);
        if (Category == 2)
        {
            // Retain the separate parallel-sheet approximation when a sheet is encountered in air.
            if (Inside) { Unsupported = 1; return 0; }
            ++CrossedPanes;
            float Cosine = max(.05, dot(R.NormalRoughness.xyz, -Direction));
            float F = .04 + .96 * pow(1 - Cosine, 5);
            Throughput *= (1 - F) / (1 + F) * exp(-float3(.8, .15, .4) * .006 / Cosine);
            Origin = R.PositionHit.xyz + Direction * Offset;
            continue;
        }
        // 📝 Straight segments across up to seven thin panes (eight shadow queries) are weighted by
        // SegmentShadowTransmittance; retain terminal BSDF radiance at eight or more panes so no energy is lost.
        if (Category != 4) return Throughput * ShadeRayStrike(R, ExcludeUnscatteredEmission && !SolidSeen && CrossedPanes < 8);
        SolidSeen = true;
        FacetCorners Facet = Facets[Index];
        float3 Outward = Unit(cross(Facet.B.xyz - Facet.A.xyz, Facet.C.xyz - Facet.A.xyz));
        bool Entering = dot(Direction, Outward) < 0;
        uint Address = MaterialAddress(R);
        if (Inside == Entering || (Inside && Address != MediumAddress))
        {
            // Nested/overlapping volumes, open or incorrectly wound surfaces and inside cameras are not inferred.
            Unsupported = 1;
            return 0;
        }
        float Ior = R.AlbedoIor.w;
        float3 Normal = Entering ? Outward : -Outward;
        DielectricRefraction Boundary = EvaluateDielectricRefraction(Direction, Normal, Inside ? MediumIor : 1, Inside ? 1 : Ior);
        if (!Boundary.Valid) { Unsupported = 1; return 0; }
        if (Boundary.TotalInternalReflection || RandomUnit(Bits) < Boundary.Reflectance)
        {
            InternalReflections += Boundary.TotalInternalReflection;
            Direction = reflect(Direction, Normal);
        }
        else
        {
            Throughput *= Boundary.RadianceScale;
            Direction = Boundary.Direction;
            Inside = !Inside;
            if (Inside)
            {
                MediumAddress = Address;
                MediumIor = Ior;
                Absorption = max(0, MaterialCoefficients(Address - 1 + 16).xyz);
            }
        }
        // 📝 Unbiased survival, NOT a radiance cutoff: retain each faint continuation with probability 1/2
        // and double its weight. Latch activation so compensation cannot disable subsequent trials.
        // Separate draws preserve the boundary proposal stream; ordinary short/bright paths are unchanged.
        Faint = Faint || (Interaction >= 32 && max(Throughput.x, max(Throughput.y, Throughput.z)) < 1e-6);
        if (Faint && (Interaction & 7u) == 7u)
        {
            if (RandomUnit(SurvivalBits) >= .5) return 0;
            Throughput *= 2;
        }
        // Offset along the geometric normal: a directional offset can cross an adjacent face at a corner.
        float3 Displacement = Outward * (Inside ? -Offset : Offset);
        Origin = R.PositionHit.xyz + Displacement;
        DistanceCorrection = dot(Displacement, Direction);
    }
    Capped = 1;
    return 0;
}
