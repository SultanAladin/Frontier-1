//============================================================================================================================================
//                                                            CARDSAMPLING.WGSL
//============================================================================================================================================
// 🧩 Surface-card lookup — the radiance a distance-field hit point carries, read from its occupant's cards (the surface cache).

//! include "FrameDescriptor.wgsl"
//! include "DistanceTraversal.wgsl"
//! include "SkyProjection.wgsl"
//! include "CardProjection.wgsl"

// 📝 Group 2 is the card depot as the tracers see it: occupants, cards, card depth (2.0+ marks a vacant texel) and the
//    card final radiance, plus the sky-view radiance a ray that escapes reads instead.
@group(2) @binding(0) var<storage, read> Occupants : array<OccupantRecord>;
@group(2) @binding(1) var<storage, read> Cards     : array<CardRecord>;
@group(2) @binding(2) var CardDepth                : texture_2d<f32>;
@group(2) @binding(3) var CardFinal                : texture_2d<f32>;
@group(2) @binding(4) var SkyView                  : texture_2d<f32>;
@group(2) @binding(5) var SkySampler               : sampler;

//------------------------------------------------------------------------------------------------------------------------
//                                                     GROUND CARDS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Ground tiles own one +Z card each, allocated consecutively, so a ground hit resolves its card from x, y alone.
fn ResolveGroundCard(Planar: vec2f) -> u32
{
    let PadMinimum = vec2f(GroundPadMinimumX, GroundPadMinimumY);
    let TileCount  = vec2u(GroundTileCountX, GroundTileCountY);
    let PadMaximum = PadMinimum + GroundTileExtent * vec2f(TileCount);
    if (all(Planar >= PadMinimum) && all(Planar < PadMaximum))
    {
        let Tile = min(vec2u((Planar - PadMinimum) / GroundTileExtent), TileCount - vec2u(1u));
        return GroundFirstCard + Tile.x * TileCount.y + Tile.y;
    }
    if (Planar.x < PadMinimum.x)
    {
        return GroundStripCard;
    }
    if (Planar.x >= PadMaximum.x)
    {
        return GroundStripCard + 1u;
    }
    if (Planar.y < PadMinimum.y)
    {
        return GroundStripCard + 2u;
    }
    return GroundStripCard + 3u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CARD RADIANCE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Four bilinear taps, each validated on its own captured depth. Returns (Σw·L, Σw) for depth-validated taps in the
//    first vector and the same for every occupied tap in the second, which is the fallback when validation rejects all.
struct CardTaps
{
    Validated : vec4f,
    Occupied  : vec4f,
}

fn SampleCardTaps(Card: CardRecord, Coordinates: vec3f, Tolerance: f32) -> CardTaps
{
    var Taps: CardTaps;
    Taps.Validated = vec4f(0.0);
    Taps.Occupied  = vec4f(0.0);
    let Lowest   = vec2i(Card.TexelOrigin);
    let Highest  = vec2i(Card.TexelOrigin + Card.TexelExtent) - vec2i(1);
    let Position = vec2f(Card.TexelOrigin) + clamp(Coordinates.xy, vec2f(0.0), vec2f(1.0)) * vec2f(Card.TexelExtent) - 0.5;
    let Corner   = floor(Position);
    let Fraction = Position - Corner;
    for (var Tap = 0u; Tap < 4u; Tap++)
    {
        let Offset   = vec2u(Tap & 1u, Tap >> 1u);
        let Texel    = clamp(vec2i(Corner) + vec2i(Offset), Lowest, Highest);
        let Bilinear = select(1.0 - Fraction.x, Fraction.x, Offset.x == 1u) * select(1.0 - Fraction.y, Fraction.y, Offset.y == 1u);
        let Depth    = textureLoad(CardDepth, Texel, 0).x;
        if (Depth > 1.5)
        {
            continue;
        }
        let Radiance = textureLoad(CardFinal, Texel, 0).rgb;
        let Weight   = max(Bilinear, 1.0e-3);
        Taps.Occupied += vec4f(Radiance * Weight, Weight);
        if (abs(Depth - Coordinates.z) <= Tolerance)
        {
            Taps.Validated += vec4f(Radiance * Weight, Weight);
        }
    }
    return Taps;
}

// 📝 Radiance leaving a scene point toward the tracer, from the cards of the occupant the distance field names there.
//    Each card is weighted by how squarely it faces the hit normal; a card that cannot see the point fails its depth
//    test and drops out, which is how a concave occupant's six cards stay correct. Tolerance [m] scales with the
//    cascade that produced the hit, since the hit point is only as precise as that cascade's voxel.
//    Returns (radiance, coverage); coverage 0 means no card covers the point at all.
fn SampleCardRadiance(Position: vec3f, Normal: vec3f, Identity: u32, Tolerance: f32) -> vec4f
{
    if (Identity == GroundIdentity)
    {
        let Card = Cards[ResolveGroundCard(Position.xy)];
        let Coordinates = ProjectCardCoordinates(Card, vec3f(Position.xy, Distance.GroundHeight));
        let Taps = SampleCardTaps(Card, Coordinates, 2.0);
        if (Taps.Occupied.w > 0.0)
        {
            return vec4f(Taps.Occupied.rgb / Taps.Occupied.w, 1.0);
        }
        return vec4f(0.0);
    }
    if (Identity >= arrayLength(&Occupants))
    {
        return vec4f(0.0);
    }
    let Occupant    = Occupants[Identity];
    if ((Occupant.Roles & RoleFallback) != 0u)
    {
        return vec4f(ResolveFallbackRadiance(Occupant, Position, Normal), 1.0);
    }
    let Local       = (Occupant.WorldToLocal * vec4f(Position, 1.0)).xyz;
    let LocalNormal = normalize((Occupant.WorldToLocal * vec4f(Normal, 0.0)).xyz);
    var Validated = vec4f(0.0);
    var Occupied  = vec4f(0.0);
    for (var Index = 0u; Index < Occupant.CardCount; Index++)
    {
        let Card   = Cards[Occupant.FirstCard + Index];
        let Facing = dot(LocalNormal, ResolveCardAxis(Card.Axis));
        if (Facing <= 0.0)
        {
            continue;
        }
        let Coordinates = ProjectCardCoordinates(Card, Local);
        if (any(Coordinates.xy < vec2f(-0.02)) || any(Coordinates.xy > vec2f(1.02)))
        {
            continue;
        }
        let Taps = SampleCardTaps(Card, Coordinates, Tolerance / ResolveCardDepthExtent(Card));
        let AxisWeight = Facing * Facing;
        Validated += vec4f(Taps.Validated.rgb, Taps.Validated.w) * AxisWeight;
        Occupied  += vec4f(Taps.Occupied.rgb, Taps.Occupied.w) * AxisWeight;
    }
    if (Validated.w > 1.0e-4)
    {
        return vec4f(Validated.rgb / Validated.w, 1.0);
    }
    if (Occupied.w > 1.0e-4)
    {
        return vec4f(Occupied.rgb / Occupied.w, 0.5);
    }
    return vec4f(0.0);
}

// 📝 A card-less occupant (a fragment: born mid-frame, gone into the field the same frame, never captured) shades its
//    hit from its packed albedo, under the sun with a short traced shadow and the sky's hemisphere toward the normal:
//    L = ρ/π · (E_sun · cosθ · V + π · L_sky(n') · (½ + ½ n_z)). Rough, and enough for a second bounce.
fn ResolveFallbackRadiance(Occupant: OccupantRecord, Position: vec3f, Normal: vec3f) -> vec3f
{
    let Albedo = unpack4x8unorm(Occupant.FirstCard).rgb;
    var Irradiance = vec3f(0.0);
    let Cosine = dot(Normal, Frame.SunDirection);
    if (Cosine > 0.0)
    {
        let Bias = ResolveSurfaceBias(Position);
        Irradiance += Frame.SunIlluminance * Cosine * TraverseSceneShadow(Position + Normal * Bias, Frame.SunDirection, 0.5 * Bias, 60.0, 6.0);
    }
    let Upward = normalize(Normal + vec3f(0.0, 0.0, 1.0));
    Irradiance += 3.14159265 * SampleSkyRadiance(Upward) * (0.5 + 0.5 * Normal.z);
    return Albedo * InversePi * Irradiance;
}

// 📝 Tolerance for a hit answered by cascade Level: one and a half of its voxels, never below 6 cm.
fn ResolveCardTolerance(Level: u32) -> f32
{
    let Clamped = min(Level, Distance.ClipCount - 1u);
    return max(1.5 * Distance.Clips[Clamped].VoxelExtent, 0.06);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SKY RADIANCE
//------------------------------------------------------------------------------------------------------------------------

fn SampleSkyRadiance(Direction: vec3f) -> vec3f
{
    let Coordinates = ProjectSkyCoordinates(normalize(Direction));
    return textureSampleLevel(SkyView, SkySampler, Coordinates, 0.0).rgb * Frame.SkyScale + Frame.NightSkyRadiance;
}

// 📝 What a traced ray brings back: card radiance at a surface, sky radiance on an escape. A surface no card covers
//    returns nothing — an uncovered point is treated as an absorber rather than guessed, which can only darken.
fn ResolveTraversalRadiance(Hit: TraversalHit, Direction: vec3f) -> vec3f
{
    if (!Hit.Hit)
    {
        return SampleSkyRadiance(Direction);
    }
    let Card = SampleCardRadiance(Hit.Position, Hit.Normal, Hit.Identity, ResolveCardTolerance(Hit.Level));
    if (Card.w > 0.0)
    {
        return Card.rgb;
    }
    return vec3f(0.0);
}
