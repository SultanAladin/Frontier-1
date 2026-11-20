/*============================================================================================================================================
                                                        FRACTURESURFACERESOLVE.SLANG
============================================================================================================================================*/
// 🧩 OpenPBR Surface + Studio Softbox IBL (No GI) + Energy/Cells/Normals debug modes for fracture shards in RHR +Z Up

import OpenPbrSurfaceScattering;
import StudioEnvironmentRadiance;

struct FractureUniformBuffer
{
    float4x4                ViewProjectionMatrix;
    float4                  CameraPositionWorld;
    float4                  ImpactPointWorld;
    float4                  RenderSettings;   // x = Mode (0=StudioOpenPBR, 1=Energy, 2=Cells, 3=Normals), y = MaxStressMPa, z = Explode, w = TimeSec
    GpuCompactMaterialEntry ExteriorMaterial;
    GpuCompactMaterialEntry InteriorMaterial;
};

[[vk::binding(0, 1)]]
ConstantBuffer<FractureUniformBuffer> FractureUniforms;

struct FractureVertexInput
{
    [[vk::location(0)]] float3 PositionWorld : POSITION;
    [[vk::location(1)]] float3 NormalWorld   : NORMAL;
    [[vk::location(2)]] float4 ShardMetrics  : TEXCOORD0; // x = CellId, y = InteriorMask (0/1), z = StressRatio, w = Proximity
};

struct FractureVertexOutput
{
    float4 ClipPosition  : SV_Position;
    float3 PositionWorld : TEXCOORD0;
    float3 NormalWorld   : TEXCOORD1;
    float4 ShardMetrics  : TEXCOORD2;
};

float3 SwizzleRhrZUpToStudioYUp(float3 Dir)
{
    return float3(Dir.x, Dir.z, -Dir.y);
}

float3 EvaluateEnergyHeatmap(float NormalizedStress)
{
    float T = saturate(NormalizedStress);
    float3 C0 = float3(0.08, 0.24, 0.92);
    float3 C1 = float3(0.06, 0.78, 0.86);
    float3 C2 = float3(0.18, 0.86, 0.34);
    float3 C3 = float3(0.98, 0.84, 0.12);
    float3 C4 = float3(0.96, 0.14, 0.10);
    if (T < 0.25) return lerp(C0, C1, T / 0.25);
    if (T < 0.50) return lerp(C1, C2, (T - 0.25) / 0.25);
    if (T < 0.75) return lerp(C2, C3, (T - 0.50) / 0.25);
    return lerp(C3, C4, (T - 0.75) / 0.25);
}

float3 EvaluateGoldenAngleCellColor(float CellIndex)
{
    float Hue = frac(CellIndex * 0.61803398875);
    float3 P  = abs(frac(Hue + float3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    float3 Rgb = saturate(P - 1.0);
    return pow(lerp(float3(0.24), Rgb, 0.74), float3(2.2));
}

void BuildOrthonormalFrame(float3 NormalZ, out float3 TangentX, out float3 BitangentY)
{
    float3 Ref = (abs(NormalZ.z) < 0.85) ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    TangentX   = normalize(cross(Ref, NormalZ));
    BitangentY = cross(NormalZ, TangentX);
}

[shader("vertex")]
FractureVertexOutput FractureVertexEntry(FractureVertexInput Input)
{
    FractureVertexOutput Out;
    Out.ClipPosition  = mul(FractureUniforms.ViewProjectionMatrix, float4(Input.PositionWorld, 1.0));
    Out.PositionWorld = Input.PositionWorld;
    Out.NormalWorld   = Input.NormalWorld;
    Out.ShardMetrics  = Input.ShardMetrics;
    return Out;
}

[shader("fragment")]
float4 FractureFragmentEntry(FractureVertexOutput Input) : SV_Target
{
    float3 N = normalize(Input.NormalWorld);
    float3 V = normalize(FractureUniforms.CameraPositionWorld.xyz - Input.PositionWorld);
    if (dot(N, V) < 0.0)
    {
        N = -N;
    }

    int Mode = int(round(FractureUniforms.RenderSettings.x));

    if (Mode == 3)
    {
        // Normals debug render in RHR +Z Up
        float3 Encoded = N * 0.5 + 0.5;
        return float4(Encoded, 1.0);
    }

    float3 BaseRadiosity = float3(0.0);

    if (Mode == 1)
    {
        // Energy heatmap (log-compressed stress ratio)
        float LogStress = log(1.0 + max(0.0, Input.ShardMetrics.z) * 18.0) / log(19.0);
        float3 Heat     = pow(EvaluateEnergyHeatmap(LogStress), float3(2.2));
        float  Diff     = 0.35 + 0.65 * saturate(dot(N, normalize(float3(0.45, -0.55, 0.70))));
        BaseRadiosity   = Heat * Diff;
    }
    else if (Mode == 2)
    {
        // Voronoi/partition cell ID debug shading
        float3 CellCol = EvaluateGoldenAngleCellColor(Input.ShardMetrics.x);
        float  Diff    = 0.32 + 0.68 * saturate(dot(N, normalize(float3(0.45, -0.55, 0.70))));
        if (Input.ShardMetrics.y > 0.5)
        {
            CellCol *= 0.84;
        }
        BaseRadiosity = CellCol * Diff;
    }
    else
    {
        // Direct Studio OpenPBR (10-lobe OpenPBR + 3 Studio Softboxes + IBL, ZERO GI)
        GpuCompactMaterialEntry Mat = (Input.ShardMetrics.y > 0.5)
            ? FractureUniforms.InteriorMaterial
            : FractureUniforms.ExteriorMaterial;

        float3 Tx, Ty;
        BuildOrthonormalFrame(N, Tx, Ty);
        float3 LocalWo = float3(dot(V, Tx), dot(V, Ty), max(1.0e-4, dot(V, N)));

        // Key, Fill, and Rim studio softbox directions in RHR +Z Up
        float3 KeyDir  = normalize(float3(0.46, -0.52, 0.72));
        float3 FillDir = normalize(float3(-0.76, -0.58, 0.26));
        float3 RimDir  = normalize(float3(-0.18, 0.69, 0.70));

        float3 L1 = float3(dot(KeyDir, Tx),  dot(KeyDir, Ty),  dot(KeyDir, N));
        float3 L2 = float3(dot(FillDir, Tx), dot(FillDir, Ty), dot(FillDir, N));
        float3 L3 = float3(dot(RimDir, Tx),  dot(RimDir, Ty),  dot(RimDir, N));

        float2 ZeroUv = Input.PositionWorld.xy * 0.5;
        float3 Radiance = float3(0.0);

        if (L1.z > 0.0)
        {
            Radiance += EvaluateCompactOpenPbrBsdf(Mat, Mat.ActiveLobeMask, LocalWo, L1, ZeroUv, float2(0.001, 0.0), float2(0.0, 0.001))
                      * float3(1.06, 1.00, 0.93) * (2.65 * L1.z);
        }
        if (L2.z > 0.0)
        {
            Radiance += EvaluateCompactOpenPbrBsdf(Mat, Mat.ActiveLobeMask, LocalWo, L2, ZeroUv, float2(0.001, 0.0), float2(0.0, 0.001))
                      * float3(0.76, 0.90, 1.12) * (1.45 * L2.z);
        }
        if (L3.z > 0.0)
        {
            Radiance += EvaluateCompactOpenPbrBsdf(Mat, Mat.ActiveLobeMask, LocalWo, L3, ZeroUv, float2(0.001, 0.0), float2(0.0, 0.001))
                      * float3(0.84, 0.93, 1.08) * (1.60 * L3.z);
        }

        float3 ReflDir    = reflect(-V, N);
        float3 EnvRefl    = SampleCalibratedStudioEnvironment(SwizzleRhrZUpToStudioYUp(ReflDir), 0.35);
        float3 EnvDiffuse = SampleCalibratedStudioEnvironment(SwizzleRhrZUpToStudioYUp(N), 0.95);
        float  Fresnel    = 0.04 + 0.96 * pow(1.0 - saturate(dot(N, V)), 5.0);
        Radiance += EnvDiffuse * 0.22 + EnvRefl * (Fresnel * 0.45);
        BaseRadiosity = Radiance;
    }

    uint   PackedRgba = PackAcesSrgb8Rgba(BaseRadiosity);
    float4 SrgbOut = float4(
        float(PackedRgba & 0xFFu) / 255.0,
        float((PackedRgba >> 8u) & 0xFFu) / 255.0,
        float((PackedRgba >> 16u) & 0xFFu) / 255.0,
        1.0);
    return SrgbOut;
}
