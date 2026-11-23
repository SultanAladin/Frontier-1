//============================================================================================================================================
//                                                            CARDRADIOSITY.WGSL
//============================================================================================================================================
// 🧩 Hemispherical gather for card radiosity sites: 16 stratified cosine rays per site, hits read last frame's card radiance.

// 📝 A site stands for a 4×4 block of card texels. Its 16 lanes first elect the occupied texel nearest the block centre,
//    then each traces one stratum of a 4×4 cosine-weighted hemisphere from it. E = π·mean(L) is folded into the site's
//    running estimate with an exponential weight that never falls below a floor, so the depot keeps responding to
//    moving light and moving occupants instead of freezing once converged.

//! include "CardSampling.wgsl"
//! include "CardIrradiance.wgsl"
//! include "DiscrepancySequence.wgsl"

@group(0) @binding(1) var<storage, read> Tiles      : array<vec2u>;
@group(0) @binding(2) var<uniform> Work             : CardWorkRecord;
@group(0) @binding(3) var CardNormal                : texture_2d<f32>;
@group(0) @binding(4) var<storage, read_write> Sites : array<vec4f>;

var<workgroup> SiteKeys     : array<atomic<u32>, 4>;
var<workgroup> LaneRadiance : array<vec3f, 64>;

@compute @workgroup_size(64)
fn IntegrateCardRadiosity(@builtin(workgroup_id) Group: vec3u, @builtin(local_invocation_index) Lane: u32)
{
    let WorkIndex = ResolveWorkIndex(Group);
    if (WorkIndex >= Work.TileCount)
    {
        return;
    }
    let Site = Lane >> 4u;
    let Ray  = Lane & 15u;
    if (Lane < 4u)
    {
        atomicStore(&SiteKeys[Lane], 0xFFFFFFFFu);
    }
    workgroupBarrier();

    let Assignment = Tiles[WorkIndex];
    let Card = Cards[Assignment.x];
    let SiteOrigin = ResolveTileTexel(Assignment, vec2u(Site & 1u, Site >> 1u) * 4u);
    let CandidateTile = SiteOrigin + vec2u(Ray & 3u, Ray >> 2u);

    // ① elect the occupied texel nearest the block centre
    if (all(CandidateTile < Card.TexelExtent))
    {
        let Depth = textureLoad(CardDepth, vec2i(Card.TexelOrigin + CandidateTile), 0).x;
        if (Depth <= 1.5)
        {
            let Offset = vec2f(f32(Ray & 3u), f32(Ray >> 2u)) - vec2f(1.5);
            let Key = (u32(dot(Offset, Offset) * 4.0) << 4u) | Ray;
            atomicMin(&SiteKeys[Site], Key);
        }
    }
    workgroupBarrier();
    let Elected = atomicLoad(&SiteKeys[Site]);

    var Radiance = vec3f(0.0);
    if (Elected != 0xFFFFFFFFu)
    {
        let ElectedRay  = Elected & 15u;
        let TileTexel   = SiteOrigin + vec2u(ElectedRay & 3u, ElectedRay >> 2u);
        let Texel       = vec2i(Card.TexelOrigin + TileTexel);
        let Depth       = textureLoad(CardDepth, Texel, 0).x;
        let Occupant    = Occupants[Card.Occupant];
        let Coordinates = vec3f((vec2f(TileTexel) + 0.5) / vec2f(Card.TexelExtent), Depth);
        let Position    = (Occupant.LocalToWorld * vec4f(ReconstructCardLocal(Card, Coordinates), 1.0)).xyz;
        let LocalNormal = textureLoad(CardNormal, Texel, 0).xyz * 2.0 - 1.0;
        let Normal      = normalize((Occupant.LocalToWorld * vec4f(LocalNormal, 0.0)).xyz);

        // ② one stratum of a jittered 4×4 cosine hemisphere per lane
        var Seed = ConstructPixelSeed(vec2u(Texel), Frame.FrameIndex, Ray);
        let Pair = (vec2f(f32(Ray & 3u), f32(Ray >> 2u)) + vec2f(AdvanceUniform(&Seed), AdvanceUniform(&Seed))) * 0.25;
        let Direction = ConstructTangentFrame(Normal) * ProjectCosineHemisphere(Pair);
        let Bias = ResolveSurfaceBias(Position);
        let Hit = TraverseSceneDistance(Position + Normal * Bias, Direction, 0.5 * Bias, 120.0, 0.02);
        Radiance = ResolveTraversalRadiance(Hit, Direction);
    }
    LaneRadiance[Lane] = Radiance;
    workgroupBarrier();

    // ③ fold E = π·mean(L) into the site estimate
    if (Ray == 0u)
    {
        let SiteTexel = Card.TexelOrigin + SiteOrigin;
        let SiteIndex = (SiteTexel.y / 4u) * Work.SiteStride + SiteTexel.x / 4u;
        if (Elected == 0xFFFFFFFFu)
        {
            Sites[SiteIndex] = vec4f(0.0);
            return;
        }
        var Sum = vec3f(0.0);
        for (var Index = 0u; Index < 16u; Index++)
        {
            Sum += LaneRadiance[Site * 16u + Index];
        }
        let Estimate = Pi * Sum / 16.0;
        let Prior    = Sites[SiteIndex];
        let Dynamic  = (Occupants[Card.Occupant].Roles & RoleDynamic) != 0u;
        let Floor    = select(Work.StaticWeight, Work.DynamicWeight, Dynamic);
        let Fraction = max(1.0 / (Prior.w + 1.0), Floor);
        Sites[SiteIndex] = vec4f(mix(Prior.rgb, Estimate, Fraction), min(Prior.w + 1.0, 64.0));
    }
}
