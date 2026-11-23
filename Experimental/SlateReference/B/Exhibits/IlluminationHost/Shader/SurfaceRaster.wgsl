//============================================================================================================================================
//                                                            SURFACERASTER.WGSL
//============================================================================================================================================
// 🧩 Primary visibility: every rastered occupant projected into depth, shading normal, material family and screen motion.

//! include "FrameDescriptor.wgsl"
//! include "PackingCodec.wgsl"
//! include "EffectRecords.wgsl"
//! include "VehicleVisual.wgsl"
//! include "DistanceRecords.wgsl"

@group(0) @binding(1) var<storage, read> Occupants : array<OccupantRecord>;
@group(0) @binding(2) var TyreMarks                 : texture_2d<f32>;   // r deposit [0, 1], g lateral coordinate (0: untouched)
@group(0) @binding(3) var TyreMarkSampler           : sampler;
@group(0) @binding(4) var<uniform> Effect           : EffectRecord;
@group(0) @binding(5) var<storage, read> VehicleState : VehicleRecord;

const VehicleOccupantCount : u32 = 5u;   // the body and four wheels lead the occupant list

struct SurfaceVarying
{
    @builtin(position) Position             : vec4f,
    @location(0) Normal                     : vec3f,
    @location(1) @interpolate(flat) Family  : u32,
    @location(2) CurrentClip                : vec4f,
    @location(3) PreviousClip               : vec4f,
    @location(4) WorldPosition              : vec3f,
    @location(5) @interpolate(flat) Vehicle : u32,
    @location(6) BodyLocal                  : vec3f,   // car-body frame, for the derez mask
    @location(7) @interpolate(flat) Identity : u32,   // occupant identity in the distance field's vocabulary
}

struct SurfaceTargets
{
    @location(0) Attributes : vec4f,   // xy octahedral shading normal, z material family, w 1 + glow (× albedo, as emission)
    @location(1) Motion     : vec2f,   // [px] current minus previous screen position
    @location(2) Identity   : f32,     // occupant identity for screen traces, GroundIdentity for the course plane
}

// 📝 The vehicle's own motion in the vertex stage: shredded rubber is hidden; rigid wheel meshes keep their authored form, the cabin's
//    steering wheel turns about its column and the cockpit takes its scale and offset (VehicleVisual.wgsl), before the
//    occupant transform.
fn DeformVehicleVertex(Index: u32, Family: u32, Position: vec3f, Normal: vec3f) -> DeformedVertex
{
    var Moved: DeformedVertex;
    Moved.Position = Position;
    Moved.Normal = Normal;
    if (Index >= 1u && Index < VehicleOccupantCount)
    {
        return ResolveWheelVertex(Index - 1u, Family, Position, Normal);
    }
    if (Index == 0u)
    {
        return DeformBodyVertex(Family, Position, Normal);
    }
    return Moved;
}

// 📝 OccupantOffset is zero for every facet but a composite set's: a fractured prop's fragments share one facet set
//    and one draw, each vertex naming its own fragment occupant relative to the draw's first instance.
@vertex
fn SurfaceVertex(@location(0) Position: vec3f, @location(1) Family: u32, @location(2) Normal: vec3f,
                 @location(3) OccupantOffset: u32, @builtin(instance_index) OccupantIndex: u32) -> SurfaceVarying
{
    let Index    = OccupantIndex + OccupantOffset;
    let Occupant = Occupants[Index];
    let Moved    = DeformVehicleVertex(Index, Family, Position, Normal);
    let World    = Occupant.LocalToWorld * vec4f(Moved.Position, 1.0);
    let Previous = Occupant.PreviousLocalToWorld * vec4f(Moved.Position, 1.0);
    var Varying: SurfaceVarying;
    Varying.Position      = Frame.ViewProjection * World;
    Varying.CurrentClip   = Frame.UnjitteredViewProjection * World;
    Varying.PreviousClip  = Frame.PreviousUnjitteredViewProjection * Previous;
    Varying.Normal        = (Occupant.LocalToWorld * vec4f(Moved.Normal, 0.0)).xyz;
    Varying.Family        = select(Family, Occupant.MaterialOverride, Occupant.MaterialOverride != NoMaterialOverride);
    Varying.WorldPosition = World.xyz;
    Varying.Vehicle       = select(0u, 1u, Index < VehicleOccupantCount);
    Varying.BodyLocal     = (Occupants[0].WorldToLocal * World).xyz;
    // 📝 The identity a screen trace reports for this surface, in the distance field's vocabulary: the occupant index for
    //    every carded occupant, GroundIdentity for the course plane, whose cards belong to the ground tiles. Identities
    //    stay below 2048, so the half-float attribute holds them exactly.
    Varying.Identity      = select(GroundIdentity, Index, (Occupant.Roles & RoleCards) != 0u);
    return Varying;
}

// 📝 Rubber on a marked road pixel, from the two-channel deposit: the deposit is filtered bilinearly, the lateral
//    coordinate among touched texels only (an untouched neighbour has no idea where any tread was), and the result
//    is cut to the tread — 0 ≤ lateral ≤ 1, antialiased over a pixel — with the tread's four grooves taken out of it.
fn SampleRoadMark(World: vec2f, PixelFootprint: f32, Rim: bool) -> f32
{
    let Extent = vec2f(TyreMarkExtentX, TyreMarkExtentY);
    let Texels = vec2f(textureDimensions(TyreMarks));
    let Coordinates = (World - vec2f(TyreMarkOriginX, TyreMarkOriginY)) / Extent;
    if (any(Coordinates <= vec2f(0.0)) || any(Coordinates >= vec2f(1.0)))
    {
        return 0.0;
    }
    let Position = Coordinates * Texels - 0.5;
    let Corner = vec2i(floor(Position));
    let Fraction = Position - floor(Position);
    var Deposit = 0.0;
    var Lateral = 0.0;
    var LateralWeight = 0.0;
    for (var Tap = 0u; Tap < 4u; Tap++)
    {
        let Offset = vec2i(i32(Tap & 1u), i32(Tap >> 1u));
        let Texel = clamp(Corner + Offset, vec2i(0), vec2i(Texels) - vec2i(1));
        let Weight = select(1.0 - Fraction.x, Fraction.x, Offset.x == 1) * select(1.0 - Fraction.y, Fraction.y, Offset.y == 1);
        let Channels = textureLoad(TyreMarks, Texel, 0);
        let Value = select(Channels.xy, Channels.zw, Rim);
        Deposit += Value.x * Weight;
        if (Value.y > 0.5 / 255.0)
        {
            Lateral += ((Value.y * 255.0 - 1.0) / 254.0 * 1.5 - 0.25) * Weight;
            LateralWeight += Weight;
        }
    }
    if (LateralWeight <= 1.0e-4 || Deposit <= 0.0)
    {
        return 0.0;
    }
    Lateral /= LateralWeight;
    // 📝 Edges: a pixel's footprint in lateral units, so the edge is one pixel wide at any distance.
    let Soft = max(PixelFootprint / select(TyreMarkPatchWidth, 0.06, Rim), 0.02);
    let Inside = smoothstep(-Soft, Soft, Lateral) * smoothstep(-Soft, Soft, 1.0 - Lateral);
    var Grooves = 0.0;
    for (var Groove = 0u; Groove < 4u; Groove++)
    {
        let Centre = 0.2 + 0.2 * f32(Groove);
        Grooves = max(Grooves, 1.0 - smoothstep(0.022, 0.022 + Soft, abs(Lateral - Centre)));
    }
    let Normalised = Deposit / max(LateralWeight, 0.25);
    return clamp(Normalised, 0.0, 1.0) * Inside * (1.0 - select(0.55 * Grooves, 0.0, Rim));
}

fn ProjectScreenPixel(Clip: vec4f) -> vec2f
{
    let Ndc = Clip.xy / Clip.w;
    return vec2f(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5) * Frame.ScreenExtent;
}

@fragment
fn SurfaceFragment(Varying: SurfaceVarying) -> SurfaceTargets
{
    // 📝 Derivatives first, in uniform control flow: the pixel's footprint on the ground for the tyre-mark edges, and
    //    the facet's own orientation for the soft tyre's guard below.
    let Footprint = length(fwidth(Varying.WorldPosition.xy));
    let Across = cross(dpdx(Varying.WorldPosition), dpdy(Varying.WorldPosition));
    var Normal = normalize(Varying.Normal);
    let View = normalize(Frame.CameraPosition - Varying.WorldPosition);
    // 📝 Every surface shades two-sided: an open facet seen from behind turns its normal toward the viewer.
    if (dot(Normal, View) < 0.0)
    {
        Normal = -Normal;
    }
    // 📝 A soft tyre squashed nearly to its rim folds tighter than its facets can follow: where the deformed shading
    //    normal turns more than ~80° from the facet it lies on, it is bent back toward the facet's own orientation.
    if (Varying.Vehicle == 1u && Varying.Family == TyreFamily && dot(Across, Across) > 1.0e-20)
    {
        let Facet = normalize(Across) * select(1.0, -1.0, dot(Across, View) < 0.0);
        if (dot(Normal, Facet) < 0.17)
        {
            Normal = normalize(mix(Facet, Normal, 0.3));
        }
    }
    // 📝 Tyre rubber: a road pixel becomes the rubber family with probability equal to the deposit under it. The
    //    threshold is an interleaved gradient that changes every frame, so temporal antialiasing averages the coin
    //    flips into a smooth darkening — and no lighting program needs to know the marks exist.
    var Family = Varying.Family;
    var Glow = 0.0;
    // 📝 See-through glazing: the car's glass leaves the opaque raster (the cabin overlay draws it over what lies
    //    behind, tinted and reflecting the sky).
    if (Varying.Vehicle == 1u && Family == VehicleGlassFamily && VehicleState.Cabin.y > 0.5)
    {
        discard;
    }
    let Marked = (Family >= MarkedFamilyFirst && Family <= MarkedFamilyLast) || (Family >= MarkedFamilyExtraFirst && Family <= MarkedFamilyExtraLast);
    if (Marked)
    {
        let Deposit = SampleRoadMark(Varying.WorldPosition.xy, Footprint, false);
        let RimDeposit = SampleRoadMark(Varying.WorldPosition.xy, Footprint, true);
        let Pixel = Varying.Position.xy + vec2f(5.588238 * f32(Frame.FrameIndex % 32u));
        let Threshold = fract(52.9829189 * fract(dot(Pixel, vec2f(0.06711056, 0.00583715))));
        if (Deposit > Threshold)
        {
            Family = TyreMarkFamily;
        }
        if (RimDeposit > Threshold) { Family = RimMarkFamily; }
    }
    // 📝 The derez mask: a car pixel whose cell has been released is gone (its cubes have left, EffectRaster), and the
    //    cells about to go glow — white-hot at the edge, cyan behind it. Rez is the same clock run backwards.
    if (Varying.Vehicle == 1u && Effect.MaskMode != 0u)
    {
        if (Effect.MaskMode == 3u)
        {
            discard;
        }
        let Unit = (Varying.BodyLocal - Effect.BodyMinimum.xyz) / max(Effect.BodyMaximum.xyz - Effect.BodyMinimum.xyz, vec3f(1.0e-3));
        let Elapsed = Effect.Clock - Effect.MaskStart;
        let Seed = u32(Effect.Emitters[0].Centre.w);
        // 📝 With face colours the glowing cells keep their own family and glow in its colour; otherwise the
        //    palette's families stand in.
        let Faces = Effect.Colour.x > 0.5;
        if (Effect.MaskMode == 1u)
        {
            let Release = ResolveDerezRelease(Unit, Seed, Effect.Style, 1.0);
            if (Elapsed >= Release)
            {
                discard;
            }
            if (Elapsed > Release - EffectHotSeconds)
            {
                Family = select(DerezHotFamily, Family, Faces);
                Glow = select(0.0, 3.0 * Effect.Colour.y, Faces);
            }
            else if (Elapsed > Release - EffectGlowSeconds)
            {
                Family = select(select(DerezGlowFamily, DerezGlitchFamily, Effect.Style == 1u), Family, Faces);
                Glow = select(0.0, Effect.Colour.y, Faces);
            }
        }
        else
        {
            let Arrival = ResolveRezArrival(Unit, Seed, Effect.Style, 1.0);
            if (Elapsed < Arrival)
            {
                discard;
            }
            if (Elapsed < Arrival + EffectHotSeconds)
            {
                Family = select(DerezHotFamily, Family, Faces);
                Glow = select(0.0, 3.0 * Effect.Colour.y, Faces);
            }
            else if (Elapsed < Arrival + EffectGlowSeconds)
            {
                Family = select(select(DerezGlowFamily, DerezGlitchFamily, Effect.Style == 1u), Family, Faces);
                Glow = select(0.0, Effect.Colour.y, Faces);
            }
        }
    }
    var Targets: SurfaceTargets;
    Targets.Attributes = vec4f(EncodeOctahedral(Normal), f32(Family), 1.0 + Glow);
    Targets.Identity   = f32(Varying.Identity);
    Targets.Motion     = ProjectScreenPixel(Varying.CurrentClip) - ProjectScreenPixel(Varying.PreviousClip);
    return Targets;
}
