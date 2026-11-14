/*============================================================================================================================================
                                                        COLOURRAMPSAMPLING.SLANG
============================================================================================================================================*/
// 🎨 Shared pure .slang GPU colour ramp LUT sampling, wrap/curve math, Oklab/LinearSrgb conversions, Kelvin blackbody & 3D lattice noise

module ColourRampSampling;

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

public static const uint ColourRampLut64Length  = 64u;
public static const uint ColourWrapClamp        = 0u;
public static const uint ColourWrapRepeat       = 1u;
public static const uint ColourWrapMirror       = 2u;

public static const uint ColourCurveLinear      = 0u;
public static const uint ColourCurveSmooth      = 1u;
public static const uint ColourCurveStepped     = 2u;

public static const uint ColourSpaceLinearSrgb  = 0u;
public static const uint ColourSpaceOklab       = 1u;

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

public struct ColourRampLut64Buffer
{
    public float4 Samples[64];
};

public struct ColourRangeUniform
{
    public float RangeMin;
    public float RangeMax;
    public float RepeatCount;
    public float PhaseOffset;
    public uint  WrapBehaviour;
    public uint  BlendCurve;
    public uint  InterpolationSpace;
    public uint  ReservedPad;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 COORDINATE WRAP & NOISE
//------------------------------------------------------------------------------------------------------------------------

public float ApplyColourCoordinateWrap(float Coordinate, uint WrapBehaviour)
{
    if (WrapBehaviour == ColourWrapRepeat)
    {
        return Coordinate - floor(Coordinate);
    }
    if (WrapBehaviour == ColourWrapMirror)
    {
        float M2 = Coordinate - 2.0 * floor(Coordinate * 0.5);
        return (M2 <= 1.0) ? M2 : (2.0 - M2);
    }
    return clamp(Coordinate, 0.0, 1.0);
}

public float NormalizeColourScalar(float RawReading, ColourRangeUniform Range)
{
    float Span     = Range.RangeMax - Range.RangeMin;
    float InvSpan  = 1.0 / max(1.0e-4, abs(Span));
    float SignSpan = (Span >= 0.0) ? 1.0 : -1.0;
    float Scaled   = (RawReading - Range.RangeMin) * InvSpan * SignSpan;
    return (Range.WrapBehaviour == ColourWrapClamp) ? clamp(Scaled, 0.0, 1.0) : Scaled;
}

float HashLatticeGrid3U(int Ix, int Iy, int Iz)
{
    uint H = uint(Ix) * 374761393u + uint(Iy) * 668265263u + uint(Iz) * 2147483647u;
    H      = (H ^ (H >> 13u)) * 1274126177u;
    return float(H ^ (H >> 16u)) / 4294967296.0;
}

public float SampleLatticeNoise3D(float3 Coord)
{
    int3   I = int3(floor(Coord));
    float3 F = Coord - float3(I);
    float3 U = F * F * (3.0 - 2.0 * F);

    float C000 = HashLatticeGrid3U(I.x,     I.y,     I.z);
    float C100 = HashLatticeGrid3U(I.x + 1, I.y,     I.z);
    float C010 = HashLatticeGrid3U(I.x,     I.y + 1, I.z);
    float C110 = HashLatticeGrid3U(I.x + 1, I.y + 1, I.z);
    float C001 = HashLatticeGrid3U(I.x,     I.y,     I.z + 1);
    float C101 = HashLatticeGrid3U(I.x + 1, I.y,     I.z + 1);
    float C011 = HashLatticeGrid3U(I.x,     I.y + 1, I.z + 1);
    float C111 = HashLatticeGrid3U(I.x + 1, I.y + 1, I.z + 1);

    float X00 = lerp(C000, C100, U.x);
    float X10 = lerp(C010, C110, U.x);
    float X01 = lerp(C001, C101, U.x);
    float X11 = lerp(C011, C111, U.x);
    float Y0  = lerp(X00,  X10,  U.y);
    float Y1  = lerp(X01,  X11,  U.y);
    return lerp(Y0, Y1, U.z);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           OKLAB & PLANCKIAN KELVIN COLORIMETRY
//------------------------------------------------------------------------------------------------------------------------

float SignedCubeRoot(float X)
{
    return sign(X) * pow(abs(X), 1.0 / 3.0);
}

public float3 ConvertLinearSrgbToOklab(float3 LinearRgb)
{
    float L = 0.4122214708 * LinearRgb.r + 0.5363325363 * LinearRgb.g + 0.0514459929 * LinearRgb.b;
    float M = 0.2119034982 * LinearRgb.r + 0.6806995451 * LinearRgb.g + 0.1073969566 * LinearRgb.b;
    float S = 0.0883024619 * LinearRgb.r + 0.2817188376 * LinearRgb.g + 0.6299787005 * LinearRgb.b;

    float LR = SignedCubeRoot(L);
    float MR = SignedCubeRoot(M);
    float SR = SignedCubeRoot(S);

    return float3(
        0.2104542553 * LR + 0.7936177850 * MR - 0.0040720468 * SR,
        1.9779984951 * LR - 2.4285922050 * MR + 0.4505937099 * SR,
        0.0259040371 * LR + 0.7827717662 * MR - 0.8086757660 * SR);
}

public float3 ConvertOklabToLinearSrgb(float3 Lab)
{
    float LR = Lab.x + 0.3963377774 * Lab.y + 0.2158037573 * Lab.z;
    float MR = Lab.x - 0.1055613458 * Lab.y - 0.0638541728 * Lab.z;
    float SR = Lab.x - 0.0894841775 * Lab.y - 1.2914855480 * Lab.z;

    float L = LR * LR * LR;
    float M = MR * MR * MR;
    float S = SR * SR * SR;

    return float3(
         4.0767416621 * L - 3.3077115913 * M + 0.2309699292 * S,
        -1.2684380046 * L + 2.6097574011 * M - 0.3413193965 * S,
        -0.0041960863 * L - 0.7034186147 * M + 1.7076147010 * S);
}

public float4 ConvertKelvinToLinearSrgb(float Kelvin, float Alpha)
{
    float Temp = clamp(Kelvin, 700.0, 15000.0) * 0.01;
    float R    = 1.0;
    float G    = 1.0;
    float B    = 1.0;
    if (Temp <= 66.0)
    {
        G = clamp((99.4708025861 * log(max(1.0, Temp)) - 161.1195681661) / 255.0, 0.0, 1.0);
        B = (Temp <= 19.0) ? 0.0 : clamp((138.5177312231 * log(Temp - 10.0) - 305.0447927307) / 255.0, 0.0, 1.0);
    }
    else
    {
        R = clamp((329.698727446 * pow(Temp - 60.0, -0.1332047592)) / 255.0, 0.0, 1.0);
        G = clamp((288.1221695283 * pow(Temp - 60.0, -0.0755148492)) / 255.0, 0.0, 1.0);
    }
    return float4(pow(R, 2.2), pow(G, 2.2), pow(B, 2.2), Alpha);
}

//------------------------------------------------------------------------------------------------------------------------
//                                               64-ENTRY GPU LUT SAMPLING
//------------------------------------------------------------------------------------------------------------------------

public float4 SampleColourRampLut64(
    StructuredBuffer<float4> LutSamples,
    uint                     RampRowOffset,
    float                    RawCoordinate,
    ColourRangeUniform       Range)
{
    float Repeat = max(0.05, Range.RepeatCount);
    float T      = ApplyColourCoordinateWrap(RawCoordinate * Repeat + Range.PhaseOffset, Range.WrapBehaviour);
    float Scaled = T * float(ColourRampLut64Length - 1u);
    uint  Low    = min(uint(floor(Scaled)), ColourRampLut64Length - 1u);
    uint  High   = min(Low + 1u, ColourRampLut64Length - 1u);
    float Frac   = Scaled - float(Low);

    if (Range.BlendCurve == ColourCurveSmooth)
    {
        Frac = Frac * Frac * (3.0 - 2.0 * Frac);
    }
    else if (Range.BlendCurve == ColourCurveStepped)
    {
        Frac = 0.0;
    }

    float4 First  = LutSamples[RampRowOffset + Low];
    float4 Second = LutSamples[RampRowOffset + High];
    float  Alpha  = lerp(First.a, Second.a, Frac);

    if (Range.InterpolationSpace == ColourSpaceOklab)
    {
        float3 MixLab = lerp(ConvertLinearSrgbToOklab(First.rgb), ConvertLinearSrgbToOklab(Second.rgb), Frac);
        return float4(ConvertOklabToLinearSrgb(MixLab), Alpha);
    }
    return float4(lerp(First.rgb, Second.rgb, Frac), Alpha);
}

//------------------------------------------------------------------------------------------------------------------------
//                                      STANDALONE COMPUTE VERIFICATION ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]] StructuredBuffer<float4>   VerificationLutInput;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> VerificationOutput;
[[vk::binding(2, 0)]] ConstantBuffer<ColourRangeUniform> VerificationRange;

[shader("compute")]
[numthreads(64, 1, 1)]
void SampleColourRampLutComputeEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint Index = DispatchThreadId.x;
    if (Index >= ColourRampLut64Length)
    {
        return;
    }
    float T = float(Index) / float(ColourRampLut64Length - 1u);
    VerificationOutput[Index] = SampleColourRampLut64(VerificationLutInput, 0u, T, VerificationRange);
}
