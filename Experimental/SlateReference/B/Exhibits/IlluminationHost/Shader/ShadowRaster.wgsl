//============================================================================================================================================
//                                                            SHADOWRASTER.WGSL
//============================================================================================================================================
// 🧩 Depth-only projection of every rastered occupant into one cascade of the sun's shadow depth.

//! include "SceneRecords.wgsl"
//! include "EffectRecords.wgsl"
//! include "VehicleVisual.wgsl"

struct CascadeRecord
{
    LightViewProjection : mat4x4f,
}

@group(0) @binding(0) var<uniform> Cascade             : CascadeRecord;
@group(0) @binding(1) var<storage, read> Occupants     : array<OccupantRecord>;
@group(0) @binding(2) var<uniform> Effect              : EffectRecord;   // the vehicle program only
@group(0) @binding(3) var<storage, read> VehicleState  : VehicleRecord;  // the vehicle program only

// 📝 OccupantOffset: as in SurfaceRaster.wgsl, a composite fragment set's vertices name their own occupant.
@vertex
fn ShadowVertex(@location(0) Position: vec3f, @location(1) OccupantOffset: u32,
                @builtin(instance_index) OccupantIndex: u32) -> @builtin(position) vec4f
{
    let World = Occupants[OccupantIndex + OccupantOffset].LocalToWorld * vec4f(Position, 1.0);
    return Cascade.LightViewProjection * World;
}

// 📝 The vehicle program, for the car's runs only: rigid wheel damage visibility and the steering wheel's turn as in
//    the surface raster; see-through glazing casts no shadow (the sun reaches the cabin through it); and while the
//    car derezzes or rezzes its shadow comes apart with it — the fragment stage drops exactly the cells the surface
//    raster drops.
struct MaskedShadowVarying
{
    @builtin(position) Position         : vec4f,
    @location(0) BodyLocal              : vec3f,
    @location(1) @interpolate(flat) Glass : u32,
}

@vertex
fn MaskedShadowVertex(@location(0) Position: vec3f, @location(1) OccupantOffset: u32, @location(2) Family: u32,
                      @builtin(instance_index) OccupantIndex: u32) -> MaskedShadowVarying
{
    let Index = OccupantIndex + OccupantOffset;
    var Local = Position;
    if (Index >= 1u && Index < 5u)
    {
        Local = ResolveWheelVertex(Index - 1u, Family, Position, vec3f(0.0, 0.0, 1.0)).Position;
    }
    else if (Index == 0u)
    {
        Local = DeformBodyVertex(Family, Position, vec3f(0.0, 0.0, 1.0)).Position;
    }
    let World = Occupants[Index].LocalToWorld * vec4f(Local, 1.0);
    var Varying: MaskedShadowVarying;
    Varying.Position = Cascade.LightViewProjection * World;
    Varying.BodyLocal = (Occupants[0].WorldToLocal * World).xyz;
    Varying.Glass = select(0u, 1u, Index == 0u && Family == VehicleGlassFamily);
    return Varying;
}

@fragment
fn MaskedShadowFragment(Varying: MaskedShadowVarying)
{
    if (Varying.Glass == 1u && VehicleState.Cabin.y > 0.5)
    {
        discard;
    }
    if (Effect.MaskMode == 0u)
    {
        return;
    }
    if (Effect.MaskMode == 3u)
    {
        discard;
    }
    let Unit = (Varying.BodyLocal - Effect.BodyMinimum.xyz) / max(Effect.BodyMaximum.xyz - Effect.BodyMinimum.xyz, vec3f(1.0e-3));
    let Elapsed = Effect.Clock - Effect.MaskStart;
    let Seed = u32(Effect.Emitters[0].Centre.w);
    if (Effect.MaskMode == 1u && Elapsed >= ResolveDerezRelease(Unit, Seed, Effect.Style, 1.0))
    {
        discard;
    }
    if (Effect.MaskMode == 2u && Elapsed < ResolveRezArrival(Unit, Seed, Effect.Style, 1.0))
    {
        discard;
    }
}
