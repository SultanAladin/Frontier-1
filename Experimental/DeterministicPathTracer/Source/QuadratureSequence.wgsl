/*====================================================================================================================================
                                                    QUADRATURESEQUENCE.WGSL
====================================================================================================================================*/
// 🧩 Fixed Owen-scrambled Sobol' (0,2) pairs shared by RadianceIntegration.wgsl and VisibilityRasterization.wgsl

// 📝 Prepended to both shader sources by TracingDivision.js, so the raster subpixel offset and the traced camera ray
//    use bit-identical points. No random number generator exists here.

// 📝 Fixed integer avalanche (lowbias32). Derives constant permutation keys from the pair index and, only when per-pixel
//    decorrelation is enabled, from the pixel coordinate. It is a pure function: the same inputs give the same key.
fn AvalancheKey(Input: u32) -> u32
{
    var Mixed = Input;
    Mixed ^= Mixed >> 16u;
    Mixed *= 0x7feb352du;
    Mixed ^= Mixed >> 15u;
    Mixed *= 0x846ca68bu;
    Mixed ^= Mixed >> 16u;
    return Mixed;
}

// 📝 Laine–Karras digit permutation: a bijection on 32-bit integers in which each bit depends only on less-significant
//    bits. Applied in bit-reversed order it is a nested uniform (Owen) digit permutation, which preserves the
//    (0,m,2)-net property of every power-of-two prefix of the Sobol' pair.
fn LaineKarrasPermutation(Input: u32, Key: u32) -> u32
{
    var Permuted = Input + Key;
    Permuted ^= Permuted * 0x6c50b47cu;
    Permuted ^= Permuted * 0xb82f1e52u;
    Permuted ^= Permuted * 0xc7afe638u;
    Permuted ^= Permuted * 0x8d22f6e6u;
    return Permuted;
}

fn NestedDigitPermutation(Input: u32, Key: u32) -> u32
{
    return reverseBits(LaineKarrasPermutation(reverseBits(Input), Key));
}

// 📝 Second Sobol' dimension (primitive polynomial x + 1): direction numbers v₁ = 2³¹, vⱼ = vⱼ₋₁ ⊕ (vⱼ₋₁ >> 1).
//    By Lucas's theorem over GF(2), C(j, i) mod 2 = 1 iff (i & j) == i, so the 32×32 Pascal matrix-vector product
//    reduces to a branchless 5-stage bit-parallel zeta transform followed by reverseBits — bit-exact for all 2³² inputs.
fn SobolSecondDimension(Index: u32) -> u32
{
    var Bits = Index;
    Bits ^= (Bits & 0xaaaaaaaau) >> 1u;
    Bits ^= (Bits & 0xccccccccu) >> 2u;
    Bits ^= (Bits & 0xf0f0f0f0u) >> 4u;
    Bits ^= (Bits & 0xff00ff00u) >> 8u;
    Bits ^= (Bits & 0xffff0000u) >> 16u;
    return reverseBits(Bits);
}

// 📝 Point SampleIndex of quadrature pair PairIndex in [0,1)². The index permutation decorrelates pairs from each other;
//    the two coordinate permutations keep each pair a (0,2)-sequence. PixelKey 0 is the shared point set; any other
//    key is a different fixed Owen scramble of the same sequence (every power-of-two prefix stays a (0,m,2)-net).
//    DigitShift XORs both coordinates' digits (a digital shift, which also preserves the net property); 0 = none.
fn ScrambledQuadraturePair(SampleIndex: u32, PairIndex: u32, PixelKey: u32, DigitShift: vec2u) -> vec2f
{
    let PairKey        = AvalancheKey(PairIndex * 0x9e3779b9u + 0x85ebca6bu) ^ PixelKey;
    let ShuffledIndex  = NestedDigitPermutation(SampleIndex, PairKey);
    let FirstDigits    = NestedDigitPermutation(reverseBits(ShuffledIndex), AvalancheKey(PairKey ^ 0xa511e9b3u));
    let SecondDigits   = NestedDigitPermutation(SobolSecondDimension(ShuffledIndex), AvalancheKey(PairKey ^ 0x63d83595u));
    return vec2f(f32((FirstDigits ^ DigitShift.x) >> 8u), f32((SecondDigits ^ DigitShift.y) >> 8u)) * (1.0 / 16777216.0);
}
