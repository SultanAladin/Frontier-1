/*============================================================================================================================================
                                                      FASTSURFACERASTER.SLANG
============================================================================================================================================*/
// 🧩 Multi-matcap face shading with normalized Blinn–Phong EON highlights and directional 3×3 PCF visibility

//------------------------------------------------------------------------------------------------------------------------
//                                                   RESOURCE DECLARATIONS
//------------------------------------------------------------------------------------------------------------------------

struct MatcapLayerDescription
{
    uint   TextureIndex;
    float  Weight;
    float2 NormalScaleBias;
    float4 Tint;
};

struct FastSurfaceParameters
{
    float3 LightDirection;
    float  AmbientIntensity;
    float3 LightRadiance;
    float  Shininess;
    float3 SpecularReflectance;
    float  SpecularIntensity;
    float2 DepthTextureExtent;
    float  DepthBias;
    uint   MaterialFirst;
    uint   MaterialCount;
    float3 Padding;
};

[[vk::binding(0, 0)]] ConstantBuffer<FastSurfaceParameters> SurfaceParameters;
[[vk::binding(1, 0)]] StructuredBuffer<MatcapLayerDescription> MaterialLayers;
[[vk::binding(2, 0)]] Texture2D<float4> MatcapTextures[];
[[vk::binding(3, 0)]] Texture2D<float> DirectionalDepthTexture;
[[vk::binding(4, 0)]] SamplerState LinearSampler;

struct SurfaceRasterInput
{
    float4 ClipPosition : SV_Position;
    float3 WorldNormal  : NORMAL;
    float3 ViewNormal   : VIEW_NORMAL;
    float3 ViewDirection : VIEW_DIRECTION;
    float2 UV           : TEXCOORD0;
    float3 LightCoordinate : TEXCOORD1;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  RADIANCE CALCULATIONS
//------------------------------------------------------------------------------------------------------------------------

float EvaluateEonSpecular(float3 Normal, float3 ViewDirection, float3 LightDirection, float Shininess)
{
    const float3 Halfway = normalize(ViewDirection + LightDirection);
    const float NormalDotHalfway = saturate(dot(Normal, Halfway));
    const float Normalization = (max(Shininess, 0.0) + 8.0) / (8.0 * 3.14159265358979323846);
    return Normalization * pow(NormalDotHalfway, max(Shininess, 0.0));
}

float SampleDirectionalVisibility(float3 LightCoordinate)
{
    const float2 LightUV = LightCoordinate.xy * 0.5 + 0.5;
    if (any(LightUV < 0.0) || any(LightUV > 1.0) || LightCoordinate.z > 1.0)
    {
        return 1.0;
    }

    float Visibility = 0.0;
    const float2 TexelStep = 1.0 / SurfaceParameters.DepthTextureExtent;
    [unroll]
    for (int OffsetY = -1; OffsetY <= 1; ++OffsetY)
    {
        [unroll]
        for (int OffsetX = -1; OffsetX <= 1; ++OffsetX)
        {
            const float2 SampleUV = LightUV + float2(OffsetX, OffsetY) * TexelStep;
            const float NearestDepth = DirectionalDepthTexture.SampleLevel(LinearSampler, SampleUV, 0.0);
            Visibility += LightCoordinate.z - SurfaceParameters.DepthBias <= NearestDepth ? 1.0 : 0.0;
        }
    }
    return Visibility / 9.0;
}

float3 AccumulateMatcapRadiance(float3 ViewNormal)
{
    float3 Radiance = 0.0;
    float TotalWeight = 0.0;
    for (uint LayerOffset = 0; LayerOffset < SurfaceParameters.MaterialCount; ++LayerOffset)
    {
        const MatcapLayerDescription Layer = MaterialLayers[SurfaceParameters.MaterialFirst + LayerOffset];
        const float Weight = max(Layer.Weight, 0.0);
        const float2 MatcapUV = ViewNormal.xy * Layer.NormalScaleBias.x + Layer.NormalScaleBias.y;
        Radiance += MatcapTextures[Layer.TextureIndex].SampleLevel(LinearSampler, MatcapUV, 0.0).rgb * Layer.Tint.rgb * Weight;
        TotalWeight += Weight;
    }
    return TotalWeight > 0.0 ? Radiance / TotalWeight : float3(0.18, 0.18, 0.18);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RASTER ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
SurfaceRasterInput VertexMain(
    float3 Position : POSITION,
    float3 Normal   : NORMAL,
    float2 UV       : TEXCOORD0)
{
    // 🚧 Camera and light transforms arrive with GeometricRaster viewport integration.
    SurfaceRasterInput Output;
    Output.ClipPosition = float4(Position, 1.0);
    Output.WorldNormal = Normal;
    Output.ViewNormal = Normal;
    Output.ViewDirection = float3(0.0, 0.0, 1.0);
    Output.UV = UV;
    Output.LightCoordinate = Position;
    return Output;
}

[shader("fragment")]
float4 FragmentMain(SurfaceRasterInput Input) : SV_Target
{
    const float3 Normal = normalize(Input.WorldNormal);
    const float3 ViewDirection = normalize(Input.ViewDirection);
    const float3 LightDirection = normalize(SurfaceParameters.LightDirection);
    const float Visibility = SampleDirectionalVisibility(Input.LightCoordinate);
    const float DiffuseCosine = saturate(dot(Normal, LightDirection));
    const float3 MatcapRadiance = AccumulateMatcapRadiance(normalize(Input.ViewNormal));
    const float SpecularEnergy = EvaluateEonSpecular(Normal, ViewDirection, LightDirection, SurfaceParameters.Shininess)
        * SurfaceParameters.SpecularIntensity * Visibility;
    const float3 Diffuse = MatcapRadiance * SurfaceParameters.LightRadiance
        * (DiffuseCosine * Visibility + SurfaceParameters.AmbientIntensity);
    const float3 Specular = SurfaceParameters.SpecularReflectance * SurfaceParameters.LightRadiance * SpecularEnergy;
    return float4(Diffuse + Specular, 1.0);
}
