//============================================================================================================================================
//                                                         DISCREPANCYSEQUENCE.WGSL
//============================================================================================================================================
// 🧩 Per-pixel sample streams — a PCG hash for decorrelation and the R2 additive recurrence for temporal stratification.

// 📐 PCG-RXS-M-XS 32-bit permutation (O'Neill 2014), used as an integer hash.
fn PermuteUnsigned(Input: u32) -> u32
{
    let Advanced = Input * 747796405u + 2891336453u;
    let Word = ((Advanced >> ((Advanced >> 28u) + 4u)) ^ Advanced) * 277803737u;
    return (Word >> 22u) ^ Word;
}

fn ConstructPixelSeed(Pixel: vec2u, FrameIndex: u32, Stream: u32) -> u32
{
    return PermuteUnsigned(Pixel.x + PermuteUnsigned(Pixel.y + PermuteUnsigned(FrameIndex + PermuteUnsigned(Stream))));
}

fn AdvanceUniform(Seed: ptr<function, u32>) -> f32
{
    *Seed = PermuteUnsigned(*Seed);
    return f32(*Seed >> 8u) * (1.0 / 16777216.0);
}

// 📐 R2 sequence (Roberts 2018): x_n = frac(s + n·(1/φ₂, 1/φ₂²)), φ₂ the plastic constant. A per-pixel hashed start
//    turns it into a Cranley–Patterson rotated stream, so each pixel walks a stratified sequence over frames.
fn ProjectTemporalPair(Pixel: vec2u, FrameIndex: u32, Stream: u32) -> vec2f
{
    let Start = vec2f(f32(PermuteUnsigned((Pixel.x * 73856093u) ^ (Pixel.y * 19349663u) ^ (Stream * 83492791u)) >> 8u),
                      f32(PermuteUnsigned((Pixel.y * 73856093u) ^ (Pixel.x * 19349663u) ^ (Stream * 2654435761u)) >> 8u))
                * (1.0 / 16777216.0);
    let Step = vec2f(0.7548776662466927, 0.5698402909980532);
    return fract(Start + f32(FrameIndex % 65536u) * Step);
}

// 📐 Cosine-weighted hemisphere direction about +Z (Malley's method), pdf = cosθ / π.
fn ProjectCosineHemisphere(Pair: vec2f) -> vec3f
{
    let Radius = sqrt(Pair.x);
    let φ = 2.0 * 3.14159265358979 * Pair.y;
    return vec3f(Radius * cos(φ), Radius * sin(φ), sqrt(max(0.0, 1.0 - Pair.x)));
}

// 📐 Uniform disk sample (concentric would be lower distortion; uniform suffices for reuse offsets).
fn ProjectUniformDisk(Pair: vec2f) -> vec2f
{
    let Radius = sqrt(Pair.x);
    let φ = 2.0 * 3.14159265358979 * Pair.y;
    return vec2f(cos(φ), sin(φ)) * Radius;
}
