//============================================================================================================================================
//                                                            PACKINGCODEC.WGSL
//============================================================================================================================================
// 🧩 Octahedral unit-vector encoding and half-precision packing for reservoirs and surface attributes.

// 📐 Octahedral projection (Cigolle et al. 2014): the unit sphere folded onto the [−1, 1]² square.
fn EncodeOctahedral(Normal: vec3f) -> vec2f
{
    let Projected = Normal.xy / (abs(Normal.x) + abs(Normal.y) + abs(Normal.z));
    if (Normal.z >= 0.0)
    {
        return Projected;
    }
    let Folded = (1.0 - abs(Projected.yx)) * select(vec2f(-1.0), vec2f(1.0), Projected >= vec2f(0.0));
    return Folded;
}

fn DecodeOctahedral(Encoded: vec2f) -> vec3f
{
    var Normal = vec3f(Encoded.x, Encoded.y, 1.0 - abs(Encoded.x) - abs(Encoded.y));
    let Fold = max(-Normal.z, 0.0);
    Normal.x += select(Fold, -Fold, Normal.x >= 0.0);
    Normal.y += select(Fold, -Fold, Normal.y >= 0.0);
    return normalize(Normal);
}

fn PackOctahedral(Normal: vec3f) -> u32
{
    return pack2x16snorm(EncodeOctahedral(Normal));
}

fn UnpackOctahedral(Packed: u32) -> vec3f
{
    return DecodeOctahedral(unpack2x16snorm(Packed));
}

fn PackHalfTriple(Triple: vec3f, Fourth: f32) -> vec2u
{
    return vec2u(pack2x16float(Triple.xy), pack2x16float(vec2f(Triple.z, Fourth)));
}

fn UnpackHalfTriple(Packed: vec2u) -> vec4f
{
    return vec4f(unpack2x16float(Packed.x), unpack2x16float(Packed.y));
}
