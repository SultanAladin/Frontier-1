/*============================================================================================================================================
                                                        QUADRATURESEQUENCE.SLANG
============================================================================================================================================*/
// 🧩 Fixed Owen-scrambled Sobol' (0,2) pairs shared by DeterministicRadianceIntegration.slang and VisibilityTriangleRasterization.slang

#pragma once

// 📝 Fixed integer avalanche (lowbias32). Pure deterministic bijection: identical inputs produce identical keys.
uint AvalancheKey(uint SeedInput)
{
    uint Mixed = SeedInput;
    Mixed ^= Mixed >> 16u;
    Mixed *= 0x7feb352du;
    Mixed ^= Mixed >> 15u;
    Mixed *= 0x846ca68bu;
    Mixed ^= Mixed >> 16u;
    return Mixed;
}

// 📝 Laine-Karras digit permutation: bijection on 32-bit integers where each bit depends only on less-significant bits.
//    Applied in bit-reversed order it forms a nested uniform (Owen) permutation preserving every (0,m,2)-net prefix.
uint LaineKarrasPermutation(uint SeedInput, uint ScrambleKey)
{
    uint Permuted = SeedInput + ScrambleKey;
    Permuted ^= Permuted * 0x6c50b47cu;
    Permuted ^= Permuted * 0xb82f1e52u;
    Permuted ^= Permuted * 0xc7afe638u;
    Permuted ^= Permuted * 0x8d22f6e6u;
    return Permuted;
}

uint NestedDigitPermutation(uint SeedInput, uint ScrambleKey)
{
    return reversebits(LaineKarrasPermutation(reversebits(SeedInput), ScrambleKey));
}

// 📝 Second Sobol' dimension (primitive polynomial x + 1): branchless 5-step GF(2) Pascal operator product + bit reverse.
uint SobolSecondDimension(uint SampleIndex)
{
    uint Bits = SampleIndex;
    Bits ^= (Bits & 0xaaaaaaaau) >> 1u;
    Bits ^= (Bits & 0xccccccccu) >> 2u;
    Bits ^= (Bits & 0xf0f0f0f0u) >> 4u;
    Bits ^= (Bits & 0xff00ff00u) >> 8u;
    Bits ^= (Bits & 0xffff0000u) >> 16u;
    return reversebits(Bits);
}

// 📝 Point SampleIndex of quadrature pair PairIndex in [0,1)^2. PixelKey 0 is the shared point set; DigitShift applies
//    an optional 12-bit void-and-cluster blue-noise digital XOR shift that preserves the (0,m,2)-net stratification.
float2 ScrambledQuadraturePair(uint SampleIndex, uint PairIndex, uint PixelKey, uint2 DigitShift)
{
    uint PairKey       = AvalancheKey(PairIndex * 0x9e3779b9u + 0x85ebca6bu) ^ PixelKey;
    uint ShuffledIndex = NestedDigitPermutation(SampleIndex, PairKey);
    uint FirstDigits   = NestedDigitPermutation(reversebits(ShuffledIndex), AvalancheKey(PairKey ^ 0xa511e9b3u));
    uint SecondDigits  = NestedDigitPermutation(SobolSecondDimension(ShuffledIndex), AvalancheKey(PairKey ^ 0x63d83595u));
    return float2(float((FirstDigits ^ DigitShift.x) >> 8u), float((SecondDigits ^ DigitShift.y) >> 8u)) * (1.0f / 16777216.0f);
}
