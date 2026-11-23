//============================================================================================================================================
//                                                              CARDFINAL.WGSL
//============================================================================================================================================
// 🧩 Final card radiance: diffuse albedo over π times direct plus interpolated radiosity irradiance, plus authored emission.

//! include "FrameDescriptor.wgsl"
//! include "CardIrradiance.wgsl"

@group(0) @binding(1) var<storage, read> Cards : array<CardRecord>;
@group(0) @binding(2) var<storage, read> Tiles : array<vec2u>;
@group(0) @binding(3) var<uniform> Work        : CardWorkRecord;
@group(0) @binding(4) var CardAlbedo           : texture_2d<f32>;
@group(0) @binding(5) var CardEmission         : texture_2d<f32>;
@group(0) @binding(6) var CardDirect           : texture_2d<f32>;
@group(0) @binding(7) var CardDepth            : texture_2d<f32>;
@group(0) @binding(8) var<storage, read> Sites : array<vec4f>;
@group(0) @binding(9) var CardFinalTarget      : texture_storage_2d<rgba16float, write>;

// 📐 Radiosity sites are spatially filtered as they are interpolated: a Gaussian (σ = 0.9 site) over the 4×4 sites
//    around the texel, restricted to the texel's own card and to sites that have gathered at all. Indirect irradiance
//    is low-frequency, and 16 rays per site per gather is otherwise visible as a 4×4-texel speckle.
fn InterpolateSiteIrradiance(Card: CardRecord, Texel: vec2u) -> vec3f
{
    let Continuous = (vec2f(Texel) + 0.5) / 4.0 - 0.5;
    let Corner     = vec2i(floor(Continuous)) - vec2i(1);
    let Lowest     = vec2i(Card.TexelOrigin / 4u);
    let Highest    = vec2i((Card.TexelOrigin + Card.TexelExtent) / 4u) - vec2i(1);
    var Sum = vec4f(0.0);
    for (var Y = 0; Y < 4; Y++)
    {
        for (var X = 0; X < 4; X++)
        {
            let Site = Corner + vec2i(X, Y);
            if (any(Site < Lowest) || any(Site > Highest))
            {
                continue;
            }
            let Record = Sites[u32(Site.y) * Work.SiteStride + u32(Site.x)];
            if (Record.w <= 0.0)
            {
                continue;
            }
            let Offset = vec2f(Site) - Continuous;
            let Weight = exp(-dot(Offset, Offset) / (2.0 * 0.9 * 0.9));
            Sum += vec4f(Record.rgb * Weight, Weight);
        }
    }
    return select(vec3f(0.0), Sum.rgb / Sum.w, Sum.w > 1.0e-4);
}

@compute @workgroup_size(8, 8)
fn IntegrateCardFinal(@builtin(workgroup_id) Group: vec3u, @builtin(local_invocation_id) Local: vec3u)
{
    let WorkIndex = ResolveWorkIndex(Group);
    if (WorkIndex >= Work.TileCount)
    {
        return;
    }
    let Assignment = Tiles[WorkIndex];
    let Card = Cards[Assignment.x];
    let TileTexel = ResolveTileTexel(Assignment, Local.xy);
    if (any(TileTexel >= Card.TexelExtent))
    {
        return;
    }
    let Texel = Card.TexelOrigin + TileTexel;
    if (textureLoad(CardDepth, vec2i(Texel), 0).x > 1.5)
    {
        textureStore(CardFinalTarget, vec2i(Texel), vec4f(0.0));
        return;
    }
    let Albedo   = textureLoad(CardAlbedo, vec2i(Texel), 0).rgb;
    let Emission = textureLoad(CardEmission, vec2i(Texel), 0).rgb * Frame.EmissionScale;
    let Direct   = textureLoad(CardDirect, vec2i(Texel), 0).rgb;
    var Indirect = vec3f(0.0);
    if (FeatureEnabled(Frame, FeatureCardRadiosity))
    {
        Indirect = InterpolateSiteIrradiance(Card, Texel);
    }
    let Radiance = Albedo * InversePi * (Direct + Indirect) + Emission;
    textureStore(CardFinalTarget, vec2i(Texel), vec4f(Radiance, 1.0));
}
