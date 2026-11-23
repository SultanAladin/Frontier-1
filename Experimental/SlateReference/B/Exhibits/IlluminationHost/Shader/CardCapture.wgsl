//============================================================================================================================================
//                                                             CARDCAPTURE.WGSL
//============================================================================================================================================
// 🧩 Orthographic capture of an occupant's facets into one card tile: diffuse albedo, local normal, emission, card depth.

// 📝 Capture runs once per card at bring-up, in the occupant's own frame, so a moving occupant never recaptures —
//    only its lighting is refreshed. The card index arrives as the instance index of the draw.

//! include "CardProjection.wgsl"

@group(0) @binding(0) var<storage, read> Cards     : array<CardRecord>;
@group(0) @binding(1) var<storage, read> Occupants : array<OccupantRecord>;
@group(0) @binding(2) var<storage, read> Materials : array<MaterialRecord>;

struct CaptureVarying
{
    @builtin(position) Position             : vec4f,
    @location(0) Normal                     : vec3f,
    @location(1) @interpolate(flat) Family  : u32,
    @location(2) @interpolate(flat) Card    : u32,
}

struct CaptureTargets
{
    @location(0) Albedo   : vec4f,
    @location(1) Normal   : vec4f,
    @location(2) Emission : vec4f,
    @location(3) Depth    : vec4f,
}

@vertex
fn CaptureVertex(@location(0) Position: vec3f, @location(1) Family: u32, @location(2) Normal: vec3f,
                 @builtin(instance_index) CardIndex: u32) -> CaptureVarying
{
    let Card     = Cards[CardIndex];
    let Occupant = Occupants[Card.Occupant];
    let Coordinates = ProjectCardCoordinates(Card, Position);
    var Varying: CaptureVarying;
    Varying.Position = vec4f(Coordinates.x * 2.0 - 1.0, 1.0 - Coordinates.y * 2.0, Coordinates.z, 1.0);
    Varying.Normal   = Normal;
    Varying.Family   = select(Family, Occupant.MaterialOverride, Occupant.MaterialOverride != NoMaterialOverride);
    Varying.Card     = CardIndex;
    return Varying;
}

@fragment
fn CaptureFragment(Varying: CaptureVarying) -> CaptureTargets
{
    let Card     = Cards[Varying.Card];
    let Material = Materials[Varying.Family];
    var Normal   = normalize(Varying.Normal);
    // 📝 An open surface seen from behind still faces the card that saw it; the captured normal is turned toward it.
    if (dot(Normal, ResolveCardAxis(Card.Axis)) < 0.0)
    {
        Normal = -Normal;
    }
    var Targets: CaptureTargets;
    Targets.Albedo   = vec4f(Material.Albedo * (1.0 - Material.Metalness), 1.0);
    Targets.Normal   = vec4f(Normal * 0.5 + 0.5, 1.0);
    Targets.Emission = vec4f(Material.Emission, 1.0);
    Targets.Depth    = vec4f(Varying.Position.z, 0.0, 0.0, 1.0);
    return Targets;
}
