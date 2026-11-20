/*====================================================================================================================================
                                                     QUADRATUREREFERENCE.MJS
====================================================================================================================================*/
// 🧩 Host replica of QuadraturePair (RadianceIntegration.wgsl) and the (0,m,2)-net / cross-pair stratification checks

//------------------------------------------------------------------------------------------------------------------------
//                                                    SEQUENCE REPLICA
//------------------------------------------------------------------------------------------------------------------------

function ReverseBits(Input)
{
    let Bits = Input >>> 0;
    Bits = ((Bits >>> 1) & 0x55555555) | ((Bits & 0x55555555) << 1);
    Bits = ((Bits >>> 2) & 0x33333333) | ((Bits & 0x33333333) << 2);
    Bits = ((Bits >>> 4) & 0x0f0f0f0f) | ((Bits & 0x0f0f0f0f) << 4);
    Bits = ((Bits >>> 8) & 0x00ff00ff) | ((Bits & 0x00ff00ff) << 8);
    return ((Bits >>> 16) | (Bits << 16)) >>> 0;
}

export function AvalancheKey(Input)
{
    let Mixed = Input >>> 0;
    Mixed = (Mixed ^ (Mixed >>> 16)) >>> 0;
    Mixed = Math.imul(Mixed, 0x7feb352d) >>> 0;
    Mixed = (Mixed ^ (Mixed >>> 15)) >>> 0;
    Mixed = Math.imul(Mixed, 0x846ca68b) >>> 0;
    Mixed = (Mixed ^ (Mixed >>> 16)) >>> 0;
    return Mixed;
}

function LaineKarrasPermutation(Input, Key)
{
    let Permuted = (Input + Key) >>> 0;
    Permuted = (Permuted ^ Math.imul(Permuted, 0x6c50b47c)) >>> 0;
    Permuted = (Permuted ^ Math.imul(Permuted, 0xb82f1e52)) >>> 0;
    Permuted = (Permuted ^ Math.imul(Permuted, 0xc7afe638)) >>> 0;
    Permuted = (Permuted ^ Math.imul(Permuted, 0x8d22f6e6)) >>> 0;
    return Permuted;
}

function NestedDigitPermutation(Input, Key)
{
    return ReverseBits(LaineKarrasPermutation(ReverseBits(Input), Key));
}

function SobolSecondDimension(Index)
{
    let Result = 0, Direction = 0x80000000, Bits = Index >>> 0;
    while (Bits !== 0)
    {
        if (Bits & 1) Result = (Result ^ Direction) >>> 0;
        Bits >>>= 1;
        Direction = (Direction ^ (Direction >>> 1)) >>> 0;
    }
    return Result >>> 0;
}

// 📝 Returns the 24-bit integer lattice coordinates; the shader's float is exactly Digits × 2⁻²⁴.
export function QuadraturePairDigits(SampleIndex, PairIndex)
{
    const PairKey       = AvalancheKey((Math.imul(PairIndex, 0x9e3779b9) + 0x85ebca6b) >>> 0);
    const ShuffledIndex = NestedDigitPermutation(SampleIndex >>> 0, PairKey);
    const FirstDigits   = NestedDigitPermutation(ReverseBits(ShuffledIndex), AvalancheKey((PairKey ^ 0xa511e9b3) >>> 0));
    const SecondDigits  = NestedDigitPermutation(SobolSecondDimension(ShuffledIndex), AvalancheKey((PairKey ^ 0x63d83595) >>> 0));
    return [FirstDigits >>> 8, SecondDigits >>> 8];
}

export function QuadraturePair(SampleIndex, PairIndex)
{
    const [X, Y] = QuadraturePairDigits(SampleIndex, PairIndex);
    return [Math.fround(X / 16777216), Math.fround(Y / 16777216)];
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    STRATIFICATION CHECKS
//------------------------------------------------------------------------------------------------------------------------

// 📝 (0,m,2)-net: for every split a + b = m, each of the 2^m elementary boxes 2^-a × 2^-b holds exactly one of the
//    first 2^m points. Returns the largest m (≤ MaximumExponent) for which every prefix 2^1 … 2^m passes.
export function MeasureNetExponent(PairIndex, MaximumExponent)
{
    let AchievedExponent = 0;
    for (let Exponent = 1; Exponent <= MaximumExponent; Exponent++)
    {
        const Count = 1 << Exponent;
        const Digits = Array.from({ length: Count }, (_, Index) => QuadraturePairDigits(Index, PairIndex));
        for (let ColumnBits = 0; ColumnBits <= Exponent; ColumnBits++)
        {
            const RowBits = Exponent - ColumnBits;
            const Occupancy = new Uint8Array(Count);
            for (const [X, Y] of Digits)
            {
                const Cell = ((X >>> (24 - ColumnBits)) << RowBits) | (RowBits ? (Y >>> (24 - RowBits)) : 0);
                if (Occupancy[Cell]++) return AchievedExponent;
            }
        }
        AchievedExponent = Exponent;
    }
    return AchievedExponent;
}

// 📝 Cross-pair independence proxy: first coordinates of two different pairs over N points, binned 16 × 16.
//    Returns the worst |count − expected| / expected and the Pearson correlation.
export function MeasureCrossPairStratification(PairA, PairB, Count)
{
    const Bins = new Uint32Array(256);
    let Sx = 0, Sy = 0, Sxx = 0, Syy = 0, Sxy = 0;
    for (let Index = 0; Index < Count; Index++)
    {
        const X = QuadraturePair(Index, PairA)[0], Y = QuadraturePair(Index, PairB)[0];
        Bins[Math.floor(X * 16) * 16 + Math.floor(Y * 16)]++;
        Sx += X; Sy += Y; Sxx += X * X; Syy += Y * Y; Sxy += X * Y;
    }
    const Expected = Count / 256;
    let WorstDeviation = 0;
    for (const Tally of Bins) WorstDeviation = Math.max(WorstDeviation, Math.abs(Tally - Expected) / Expected);
    const Covariance = Sxy / Count - (Sx / Count) * (Sy / Count);
    const Correlation = Covariance / Math.sqrt((Sxx / Count - (Sx / Count) ** 2) * (Syy / Count - (Sy / Count) ** 2));
    return { WorstDeviation, Correlation };
}
