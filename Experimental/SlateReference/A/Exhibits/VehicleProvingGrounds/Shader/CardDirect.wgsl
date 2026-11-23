//============================================================================================================================================
//                                                             CARDDIRECT.WGSL
//============================================================================================================================================
// 🧩 Direct sun and clustered light irradiance on every scheduled card texel, feeding the surface cache and indirect GI radiosity.

//! include "FrameDescriptor.wgsl"
//! include "DistanceTraversal.wgsl"
//! include "CardProjection.wgsl"
//! include "CardIrradiance.wgsl"
//! include "FireIllumination.wgsl"
//! include "ClusteredLights.wgsl"

@group(0) @binding(1) var<storage, read> Cards        : array<CardRecord>;
@group(0) @binding(2) var<storage, read> Occupants    : array<OccupantRecord>;
@group(0) @binding(3) var<storage, read> Tiles        : array<vec2u>;
@group(0) @binding(4) var<uniform> Work               : CardWorkRecord;
@group(0) @binding(5) var CardNormal                  : texture_2d<f32>;
@group(0) @binding(6) var CardDepth                   : texture_2d<f32>;
@group(0) @binding(7) var CardDirectTarget            : texture_storage_2d<rgba16float, write>;
@group(0) @binding(8) var<storage, read> SceneLights  : array<LightRecord>;
@group(0) @binding(9) var<storage, read> ClusterCells : array<vec4u>;
@group(0) @binding(10) var<storage, read> ClusterIndices : array<u32>;

@compute @workgroup_size(8, 8)
fn IntegrateCardDirect(@builtin(workgroup_id) Group: vec3u, @builtin(local_invocation_id) Local: vec3u)
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
    let Depth = textureLoad(CardDepth, vec2i(Texel), 0).x;
    if (Depth > 1.5)
    {
        textureStore(CardDirectTarget, Texel, vec4f(0.0));
        return;
    }

    let Occupant    = Occupants[Card.Occupant];
    let Coordinates = vec3f((vec2f(TileTexel) + 0.5) / vec2f(Card.TexelExtent), Depth);
    let Position    = (Occupant.LocalToWorld * vec4f(ReconstructCardLocal(Card, Coordinates), 1.0)).xyz;
    let LocalNormal = textureLoad(CardNormal, vec2i(Texel), 0).xyz * 2.0 - 1.0;
    let Normal      = normalize((Occupant.LocalToWorld * vec4f(LocalNormal, 0.0)).xyz);

    let Cosine = dot(Normal, Frame.SunDirection);
    var Irradiance = vec3f(0.0);
    if (Cosine > 0.0)
    {
        let Bias = ResolveSurfaceBias(Position);
        var Visibility = 1.0;
        if (FeatureEnabled(Frame, FeatureDirectShadow))
        {
            Visibility = TraverseSceneShadow(Position + Normal * Bias, Frame.SunDirection, 0.5 * Bias, 400.0, 6.0);
        }
        Irradiance = Frame.SunIlluminance * Cosine * Visibility;
    }
    let Fire = ResolveFireArrival(Position, Normal);
    Irradiance += Fire.Illuminance * max(dot(Normal, Fire.Direction), 0.0);

    // 📝 Clustered light irradiance feeding surface cards (which bounce into ReSTIR GI & multi-bounce radiosity!):
    //    only the position's cell is evaluated, never the whole light list.
    let Range = ResolveClusterRange(&ClusterCells, Position);
    for (var Li = 0u; Li < Range.y; Li++)
    {
        let Light = SceneLights[ClusterIndices[Range.x + Li]];
        Irradiance += EvaluateLightArrival(Light, Position, Normal);
    }

    textureStore(CardDirectTarget, Texel, vec4f(Irradiance, 1.0));
}
