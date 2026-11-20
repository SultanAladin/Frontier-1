/*============================================================================================================================================
                                                        OPENPBRSURFACESCATTERING.SLANG
============================================================================================================================================*/
// 🧩 Master 10-Lobe OpenPBR 1.1 + Constant-Time Glinty BSDF unpacking 128 B CompactMaterialEntry with wave-uniform lobe branching

import ThinFilmInterference;
import GlintMicroflakeScattering;
import LinearlyTransformedSheen;
import SubsurfaceTransmission;
import FiberScattering;

static const uint LobeDiffuseEON             = 1u << 0u;
static const uint LobeSpecularMetalGGX       = 1u << 1u;
static const uint LobeClearcoat              = 1u << 2u;
static const uint LobeThinFilmAiry           = 1u << 3u;
static const uint LobeGlintMicroflakes       = 1u << 4u;
static const uint LobeDielectricTransmission = 1u << 5u;
static const uint LobeSubsurfaceDiffusion    = 1u << 6u;
static const uint LobeClothSheenLTC          = 1u << 7u;
static const uint LobeFiberBCSDF             = 1u << 8u;
static const uint LobePorosityWetness        = 1u << 9u;
static const uint LobeBlackbodyEmission      = 1u << 10u;
static const uint FlagThinWalledSlab         = 1u << 16u;

// Exact 128-byte (32 x uint32) GPU bindless material register matching C++20 CompactMaterialEntry byte-for-byte
public struct GpuCompactMaterialEntry
{
    public uint  ActiveLobeMask;
    public uint  BaseColorRgHalf;
    public uint  BaseColorBAndWeightHalf;
    public uint  DiffuseSpecRoughnessHalf;

    public uint  MetalnessSpecWeightHalf;
    public uint  AnisotropyRotationHalf;
    public uint  DielectricIorEdgeTintRHalf;
    public uint  EdgeTintGbHalf;

    public uint  CoatWeightRoughnessHalf;
    public uint  CoatAnisoIorHalf;
    public uint  CoatTintRgHalf;
    public uint  CoatTintBAndThicknessHalf;

    public float ThinFilmThicknessNanometers;
    public uint  ThinFilmWeightIorHalf;
    public uint  FlakeCoverageRoughHalf;
    public uint  FlakeSpreadLogDensityHalf;

    public uint  FlakePrimaryRgHalf;
    public uint  FlakePrimaryBAndBurialHalf;
    public uint  FlakeHorizonRgHalf;
    public uint  FlakeHorizonBAndTransWeightHalf;

    public uint  AbbeVolumeAnisoHalf;
    public uint  AttenuationRgHalf;
    public uint  AttenuationBAndDistHalf;
    public uint  SubsurfaceWeightAnisoHalf;

    public uint  SubsurfaceColorRgHalf;
    public uint  SubsurfaceColorBAndMfpXHalf;
    public uint  SubsurfaceMfpYzHalf;
    public uint  SheenWeightRoughRgHalf;

    public uint  SheenBAndFiberParamsPacked;
    public uint  FiberPigmentPorosityPacked;
    public uint  PorosityKelvinHalf;
    public uint  EmissionNitsChannelMask;
};

float2 UnpackHalf2(uint Word)
{
    return float2(f16tof32(Word & 0xFFFFu), f16tof32((Word >> 16u) & 0xFFFFu));
}

float4 UnpackUnorm4(uint Word)
{
    return float4(
        float(Word & 0xFFu),
        float((Word >> 8u) & 0xFFu),
        float((Word >> 16u) & 0xFFu),
        float((Word >> 24u) & 0xFFu)) * (1.0 / 255.0);
}

float3 EvaluateEnergyPreservingOrenNayarGpu(float3 LocalWo, float3 LocalWi, float3 Albedo, float Roughness)
{
    float MuO    = saturate(LocalWo.z);
    float MuI    = saturate(LocalWi.z);
    float ConstF = 0.2877934;
    float CoeffA = 1.0 / (1.0 + Roughness * ConstF);
    float CoeffB = Roughness * CoeffA;
    float SVal   = dot(LocalWo, LocalWi) - MuO * MuI;
    float InvMax = (SVal > 0.0) ? (1.0 / max(1.0e-5, max(MuO, MuI))) : 1.0;
    float Single = 0.318309886 * (CoeffA + CoeffB * SVal * InvMax);

    float CompO  = 1.0 - MuO;
    float CompI  = 1.0 - MuI;
    float GFonO  = CompO * (0.0571085 + CompO * (0.4918819 + CompO * (-0.3321814 + 0.0714430 * CompO)));
    float GFonI  = CompI * (0.0571085 + CompI * (0.4918819 + CompI * (-0.3321814 + 0.0714430 * CompI)));
    float EO     = saturate(CoeffA + CoeffB * GFonO);
    float EI     = saturate(CoeffA + CoeffB * GFonI);
    float EAvg        = saturate(CoeffA * (1.0 + 0.07256259 * Roughness));
    float OneMinusAvg = max(0.0, 1.0 - EAvg);
    float MultiGeom   = (OneMinusAvg > 1.0e-6) ? (0.318309886 * (1.0 - EO) * (1.0 - EI) / OneMinusAvg) : 0.0;
    float3 MultiAlb   = (Albedo * Albedo * EAvg) / max(float3(1.0e-4), float3(1.0) - Albedo * OneMinusAvg);
    return Albedo * Single + MultiAlb * MultiGeom;
}

public float3 EvaluateCompactOpenPbrBsdf(
    GpuCompactMaterialEntry Entry,
    uint                    WaveCombinedLobeMask,
    float3                  LocalWo,
    float3                  LocalWi,
    float2                  SurfaceUV,
    float2                  DuDx,
    float2                  DuDy)
{
    if (LocalWo.z <= 1.0e-6 || abs(LocalWi.z) <= 1.0e-6)
    {
        return float3(0.0);
    }

    uint Mask = Entry.ActiveLobeMask & WaveCombinedLobeMask;

    float2 BaseRg   = UnpackHalf2(Entry.BaseColorRgHalf);
    float2 BaseBw   = UnpackHalf2(Entry.BaseColorBAndWeightHalf);
    float2 RoughDs  = UnpackHalf2(Entry.DiffuseSpecRoughnessHalf);
    float2 MetSpec  = UnpackHalf2(Entry.MetalnessSpecWeightHalf);
    float2 AnisoRot = UnpackHalf2(Entry.AnisotropyRotationHalf);
    float2 IorF82r  = UnpackHalf2(Entry.DielectricIorEdgeTintRHalf);
    float2 F82gb    = UnpackHalf2(Entry.EdgeTintGbHalf);

    float3 BaseColor     = float3(BaseRg.x, BaseRg.y, BaseBw.x);
    float  BaseWeight    = BaseBw.y;
    float  DiffRough     = RoughDs.x;
    float  SpecRough     = RoughDs.y;
    float  Metalness     = MetSpec.x;
    float  SpecWeight    = MetSpec.y;
    float  DielectricIOR = IorF82r.x;
    float3 EdgeTintF82   = float3(IorF82r.y, F82gb.x, F82gb.y);

    if (LocalWi.z < 0.0)
    {
        float3 LowerOut = float3(0.0);
        if ((Mask & LobeDielectricTransmission) != 0u)
        {
            float2 HorizonTr = UnpackHalf2(Entry.FlakeHorizonBAndTransWeightHalf);
            float2 AbbeAniso = UnpackHalf2(Entry.AbbeVolumeAnisoHalf);
            float2 AttRg     = UnpackHalf2(Entry.AttenuationRgHalf);
            float2 AttBd     = UnpackHalf2(Entry.AttenuationBAndDistHalf);
            bool   ThinSlab  = (Entry.ActiveLobeMask & FlagThinWalledSlab) != 0u;
            LowerOut += (1.0 - Metalness) * EvaluateDielectricTransmission(
                LocalWo, LocalWi, SpecRough, DielectricIOR, HorizonTr.y, AbbeAniso.x, ThinSlab, float3(AttRg.x, AttRg.y, AttBd.x), AttBd.y);
        }
        return LowerOut;
    }

    // Porosity wetness darkening (Lobe 10a)
    float3 WorkingAlbedo = BaseColor;
    float  WorkingIOR    = DielectricIOR;
    if ((Mask & LobePorosityWetness) != 0u)
    {
        float4 FiberPor = UnpackUnorm4(Entry.FiberPigmentPorosityPacked);
        float2 PorKelv  = UnpackHalf2(Entry.PorosityKelvinHalf);
        float  Sat      = saturate(PorKelv.x * FiberPor.w);
        float3 WetAlb   = (BaseColor * 0.522) / max(float3(1.0e-4), float3(1.0) - BaseColor * 0.478);
        WorkingAlbedo   = lerp(BaseColor, WetAlb, Sat);
        WorkingIOR      = lerp(DielectricIOR, max(1.005, DielectricIOR / 1.333), Sat);
    }

    // Clearcoat reflection & Beer-Lambert medium attenuation (Lobe 3)
    float3 CoatBrdf  = float3(0.0);
    float3 CoatTrans = float3(1.0);
    float  EffRough  = SpecRough;
    if ((Mask & LobeClearcoat) != 0u)
    {
        float2 CoatWr    = UnpackHalf2(Entry.CoatWeightRoughnessHalf);
        float2 CoatAi    = UnpackHalf2(Entry.CoatAnisoIorHalf);
        float2 CoatRg    = UnpackHalf2(Entry.CoatTintRgHalf);
        float2 CoatBt    = UnpackHalf2(Entry.CoatTintBAndThicknessHalf);
        float  CoatW     = CoatWr.x;
        float  CoatAlpha = max(1.0e-3, CoatWr.y * CoatWr.y);
        float3 HalfVec   = normalize(LocalWo + LocalWi);
        float  CoatF0    = ((CoatAi.y - 1.0) / (CoatAi.y + 1.0));
        CoatF0          *= CoatF0;
        float  CoatFh    = CoatF0 + (1.0 - CoatF0) * pow(1.0 - saturate(dot(LocalWo, HalfVec)), 5.0);
        float  Denom     = HalfVec.z * HalfVec.z * (CoatAlpha * CoatAlpha - 1.0) + 1.0;
        float  CoatD     = (CoatAlpha * CoatAlpha) / max(1.0e-6, 3.14159265359 * Denom * Denom);
        CoatBrdf         = float3(CoatW * CoatFh * CoatD / max(1.0e-5, 4.0 * LocalWo.z * LocalWi.z));
        CoatTrans        = pow(clamp(float3(CoatRg.x, CoatRg.y, CoatBt.x), float3(1.0e-3), float3(1.0)), float3(CoatW * CoatBt.y * 10.0));
        EffRough         = lerp(SpecRough, pow(min(1.0, pow(SpecRough, 4.0) + 2.0 * pow(CoatWr.y, 4.0)), 0.25), CoatW);
    }

    // Cloth Sheen LTC (Lobe 8)
    float4 SheenOut = float4(0.0, 0.0, 0.0, 1.0);
    if ((Mask & LobeClothSheenLTC) != 0u)
    {
        float4 SheenPack = UnpackUnorm4(Entry.SheenWeightRoughRgHalf);
        float4 FiberPack = UnpackUnorm4(Entry.SheenBAndFiberParamsPacked);
        SheenOut = EvaluateClothSheenLobe(
            LocalWo, LocalWi, SheenPack.x, SheenPack.y, float3(SheenPack.z, SheenPack.w, FiberPack.x));
    }

    // Fiber BCSDF (Lobe 9)
    float3 FiberBrdf  = float3(0.0);
    float  FiberBlend = 0.0;
    if ((Mask & LobeFiberBCSDF) != 0u)
    {
        float4 FiberPack = UnpackUnorm4(Entry.SheenBAndFiberParamsPacked);
        float4 Pigment   = UnpackUnorm4(Entry.FiberPigmentPorosityPacked);
        FiberBlend       = FiberPack.y;
        FiberBrdf        = EvaluateFiberStrandBcsdf(
            LocalWo, LocalWi, WorkingIOR, WorkingAlbedo, FiberBlend, FiberPack.z, FiberPack.w, Pigment.x * 0.70 - 0.35, Pigment.y, Pigment.z);
    }

    // Glint Microflakes (Lobe 5) + Specular/Thin-Film (Lobe 2 & Lobe 4) + Subsurface/Diffuse (Lobe 7 & Lobe 1)
    float3 SubstrateBrdf = float3(0.0);
    if (FiberBlend < 0.999)
    {
        float2 FlakeCr  = UnpackHalf2(Entry.FlakeCoverageRoughHalf);
        float  FlakeCov = ((Mask & LobeGlintMicroflakes) != 0u) ? FlakeCr.x : 0.0;
        float3 GlintVal = float3(0.0);
        if (FlakeCov > 1.0e-5)
        {
            float2 FlakeSl = UnpackHalf2(Entry.FlakeSpreadLogDensityHalf);
            float2 PrimRg  = UnpackHalf2(Entry.FlakePrimaryRgHalf);
            float2 PrimBb  = UnpackHalf2(Entry.FlakePrimaryBAndBurialHalf);
            float2 HorRg   = UnpackHalf2(Entry.FlakeHorizonRgHalf);
            float2 HorBt   = UnpackHalf2(Entry.FlakeHorizonBAndTransWeightHalf);
            GlintVal = EvaluateGlintMicroflakeLobe(
                LocalWo, LocalWi, FlakeCov, FlakeCr.y, FlakeSl.x, FlakeSl.y,
                float3(PrimRg.x, PrimRg.y, PrimBb.x), float3(HorRg.x, HorRg.y, HorBt.x), PrimBb.y,
                SurfaceUV, DuDx, DuDy);
        }

        float3 HalfVec = normalize(LocalWo + LocalWi);
        float  VoDotH  = saturate(dot(LocalWo, HalfVec));
        float  DielF0  = ((WorkingIOR - 1.0) / (WorkingIOR + 1.0));
        DielF0         = DielF0 * DielF0 * SpecWeight;
        float3 Fresnel = lerp(float3(DielF0 + (1.0 - DielF0) * pow(1.0 - VoDotH, 5.0)), WorkingAlbedo * EdgeTintF82, Metalness);

        if ((Mask & LobeThinFilmAiry) != 0u)
        {
            float2 FilmWi = UnpackHalf2(Entry.ThinFilmWeightIorHalf);
            Fresnel = EvaluateSpectralThinFilmFresnel(
                VoDotH, Entry.ThinFilmThicknessNanometers, FilmWi.y, 1.0, Fresnel, FilmWi.x, false);
        }

        float Alpha   = max(1.0e-3, EffRough * EffRough);
        float AlphaSq = Alpha * Alpha;
        float Denom   = HalfVec.z * HalfVec.z * (AlphaSq - 1.0) + 1.0;
        float NdfD    = AlphaSq / max(1.0e-6, 3.14159265359 * Denom * Denom);
        float TanO2   = (1.0 - LocalWo.z * LocalWo.z) / max(1.0e-6, LocalWo.z * LocalWo.z);
        float TanI2   = (1.0 - LocalWi.z * LocalWi.z) / max(1.0e-6, LocalWi.z * LocalWi.z);
        float G2      = 1.0 / (1.0 + 0.5 * (sqrt(1.0 + AlphaSq * TanO2) - 1.0) + 0.5 * (sqrt(1.0 + AlphaSq * TanI2) - 1.0));
        float3 SpecVal = Fresnel * (NdfD * G2 / max(1.0e-6, 4.0 * LocalWo.z * LocalWi.z));

        float2 HorBt  = UnpackHalf2(Entry.FlakeHorizonBAndTransWeightHalf);
        float2 SssWa  = UnpackHalf2(Entry.SubsurfaceWeightAnisoHalf);
        float  SubWin = (1.0 - Metalness) * (1.0 - HorBt.y) * (1.0 - DielF0);

        float3 DiffVal = float3(0.0);
        if ((Mask & LobeDiffuseEON) != 0u)
        {
            DiffVal = EvaluateEnergyPreservingOrenNayarGpu(LocalWo, LocalWi, WorkingAlbedo, DiffRough) * (SubWin * (1.0 - SssWa.x) * BaseWeight);
        }

        float3 SssVal = float3(0.0);
        if ((Mask & LobeSubsurfaceDiffusion) != 0u)
        {
            float2 SssRg = UnpackHalf2(Entry.SubsurfaceColorRgHalf);
            float2 SssBm = UnpackHalf2(Entry.SubsurfaceColorBAndMfpXHalf);
            float2 SssYz = UnpackHalf2(Entry.SubsurfaceMfpYzHalf);
            SssVal = EvaluateSubsurfaceLocalClosure(
                LocalWo, LocalWi, WorkingIOR, SssWa.x, float3(SssRg.x, SssRg.y, SssBm.x), float3(SssBm.y, SssYz.x, SssYz.y), SssWa.y) * SubWin;
        }

        SubstrateBrdf = GlintVal + (1.0 - FlakeCov) * (SpecVal + DiffVal + SssVal);
    }

    float3 CombinedUnderCoat = SheenOut.rgb + SheenOut.a * (FiberBrdf + (1.0 - FiberBlend) * SubstrateBrdf);
    return CoatBrdf + CoatTrans * CombinedUnderCoat;
}

struct MaterialEvalQuery
{
    uint   MaterialSlot;
    float3 LocalWo;
    float3 LocalWi;
    float2 SurfaceUV;
    float2 FootprintScale;
};

[[vk::binding(0, 0)]] StructuredBuffer<GpuCompactMaterialEntry> CompactMaterials;
[[vk::binding(1, 0)]] StructuredBuffer<MaterialEvalQuery>       EvalQueries;
[[vk::binding(2, 0)]] RWStructuredBuffer<float4>                EvalOutputs;

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Count = 0u, Stride = 0u;
    EvalQueries.GetDimensions(Count, Stride);
    if (DispatchThreadID.x >= Count)
    {
        return;
    }

    MaterialEvalQuery Query       = EvalQueries[DispatchThreadID.x];
    GpuCompactMaterialEntry Entry = CompactMaterials[Query.MaterialSlot];
    uint WaveMask                 = WaveActiveBitOr(Entry.ActiveLobeMask);

    float3 Bsdf = EvaluateCompactOpenPbrBsdf(
        Entry,
        WaveMask,
        normalize(Query.LocalWo),
        normalize(Query.LocalWi),
        Query.SurfaceUV,
        float2(Query.FootprintScale.x, 0.0),
        float2(0.0, Query.FootprintScale.y));

    EvalOutputs[DispatchThreadID.x] = float4(Bsdf, float(WaveMask & 0xFFFFu));
}
