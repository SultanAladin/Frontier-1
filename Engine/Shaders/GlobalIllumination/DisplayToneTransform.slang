/*============================================================================================================================================
                                                        DISPLAYTONETRANSFORM.SLANG
============================================================================================================================================*/
// 🧩 Resolves accumulated or reconstructed radiance to packed RGBA8 display pixels: 2^EV exposure, Stephen Hill ACES RRT+ODT, sRGB

struct DisplayUniformBlock
{
    uint4  Extent;   // x = width, y = height, z = tone curve (0 ACES fitted, 1 linear clamp), w = history / reconstruction offset [px]
    float4 Exposure; // x = exposure multiplier (2^EV), y = history active (0/1), z = reconstructed (0/1), w = unused
};

[[vk::binding(0, 0)]] ConstantBuffer<DisplayUniformBlock> Display;
[[vk::binding(1, 0)]] StructuredBuffer<float4>            Accumulation;
[[vk::binding(2, 0)]] StructuredBuffer<float4>            Reconstructed;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint>            OutDisplayRgba8;

// 📝 Stephen Hill's ACES RRT + sRGB ODT fit using explicit row dot products (matches DisplayExposure.wgsl & ResolveDisplayPixels bit-for-bit).
float3 ToneCurveAcesFitted(float3 Linear)
{
    float3 V = float3(
        0.59719f * Linear.x + 0.35458f * Linear.y + 0.04823f * Linear.z,
        0.07600f * Linear.x + 0.90834f * Linear.y + 0.01566f * Linear.z,
        0.02840f * Linear.x + 0.13383f * Linear.y + 0.83777f * Linear.z);
    float3 A = V * (V + 0.0245786f) - 0.000090537f;
    float3 B = V * (0.983729f * V + 0.4329510f) + 0.238081f;
    float3 T = A / B;
    float3 Mapped = float3(
         1.60475f * T.x - 0.53108f * T.y - 0.07367f * T.z,
        -0.10208f * T.x + 1.10813f * T.y - 0.00605f * T.z,
        -0.00327f * T.x - 0.07276f * T.y + 1.07602f * T.z);
    return clamp(Mapped, float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f));
}

float EncodeSrgbChannel(float C)
{
    return (C <= 0.0031308f) ? (C * 12.92f) : (1.055f * pow(C, 1.0f / 2.4f) - 0.055f);
}

float3 EncodeSrgb(float3 Linear)
{
    return float3(EncodeSrgbChannel(Linear.x), EncodeSrgbChannel(Linear.y), EncodeSrgbChannel(Linear.z));
}

uint PackRgba8Srgb(float3 Srgb)
{
    uint R = uint(clamp(round(Srgb.x * 255.0f), 0.0f, 255.0f));
    uint G = uint(clamp(round(Srgb.y * 255.0f), 0.0f, 255.0f));
    uint B = uint(clamp(round(Srgb.z * 255.0f), 0.0f, 255.0f));
    return R | (G << 8u) | (B << 16u) | (255u << 24u);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 InvocationId : SV_DispatchThreadID)
{
    uint Width  = Display.Extent.x;
    uint Height = Display.Extent.y;
    if (InvocationId.x >= Width || InvocationId.y >= Height)
    {
        return;
    }

    uint   PixelIndex = InvocationId.y * Width + InvocationId.x;
    float4 Sum        = Accumulation[PixelIndex];
    if (Display.Exposure.z > 0.5f)
    {
        Sum = Reconstructed[Display.Extent.w + PixelIndex];
    }

    float4 History       = (Display.Exposure.y > 0.5f) ? Accumulation[Display.Extent.w + PixelIndex] : float4(0.0f, 0.0f, 0.0f, 0.0f);
    float  HistoryWeight = max(History.w - Sum.w, 0.0f);
    float  Weight        = Sum.w + HistoryWeight;
    if (Weight <= 0.0f)
    {
        OutDisplayRgba8[PixelIndex] = PackRgba8Srgb(float3(0.02f, 0.02f, 0.025f));
        return;
    }

    float3 Radiance = (Sum.rgb + History.rgb * HistoryWeight) / Weight * Display.Exposure.x;
    float3 Mapped   = (Display.Extent.z == 1u)
                          ? clamp(Radiance, float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f))
                          : ToneCurveAcesFitted(Radiance);
    OutDisplayRgba8[PixelIndex] = PackRgba8Srgb(EncodeSrgb(Mapped));
}
