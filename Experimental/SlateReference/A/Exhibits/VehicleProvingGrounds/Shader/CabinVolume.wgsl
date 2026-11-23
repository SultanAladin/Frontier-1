//============================================================================================================================================
//                                                             CABINVOLUME.WGSL
//============================================================================================================================================
// 🧩 How far a car-body-frame point lies inside the ControlVehicle's cabin: 1 deep inside, 0 outside, soft over 4 cm.

// 📝 The car's shell is one facet thick — thinner than any distance-field voxel — and the cabin lies inside the body's
//    own distance volume, so the traced indirect light there is the sunlit exterior's cards seen from within, and the
//    cabin glowed brighter than the street. Lumen asks for thick walls and open interiors;
//    here the cabin is known, so the light that cannot be traced is attenuated instead: two boxes, the greenhouse up
//    to the headliner within the glass line and the lower tub forward to the footwells and back to the rear bench.
//    The sun through the glazing is direct light (the shadow raster lets it in) and is never touched.

fn ResolveBoxShare(Point: vec3f, Minimum: vec3f, Maximum: vec3f, Soft: f32) -> f32
{
    let Inside = min(Point - Minimum, Maximum - Point);
    return clamp(min(min(Inside.x, Inside.y), Inside.z) / Soft, 0.0, 1.0);
}

fn ResolveCabinShare(BodyLocal: vec3f) -> f32
{
    let Greenhouse = ResolveBoxShare(BodyLocal, vec3f(-0.95, -1.04, -0.12), vec3f(1.33, 1.04, 1.19), 0.04);
    let Tub = ResolveBoxShare(BodyLocal, vec3f(-1.85, -1.02, -0.12), vec3f(1.38, 1.02, 0.60), 0.04);
    return max(Greenhouse, Tub);
}
