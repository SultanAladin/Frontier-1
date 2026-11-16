/*============================================================================================================================================
                                                        MATERIALSPECIMENRASTERIZER.CPP
============================================================================================================================================*/
// 🧩 Implementation of calibrated studio shader-ball lighting (key sun + cool rim + QMC sky dome + refractive glass backdrop)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/Baking/MaterialSpecimenRasterizer.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/MaterialSpace/Scattering/OpenPbrSurfaceScattering.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::MaterialSpace
{

namespace
{

[[nodiscard]] Mathematics::LinearRGB SampleStudioEnvironmentRadiance(Mathematics::Axis3D WorldDir) noexcept
{
    // Studio softbox horizon + zenith sky gradient + warm floor bounce + crisp studio light strips for metal/coat/glass reflections
    const float UpT = std::clamp(0.5f * (WorldDir.Y + 1.0f), 0.0f, 1.0f);
    Mathematics::LinearRGB Env {
        0.10f * (1.0f - UpT) + 0.24f * UpT,
        0.11f * (1.0f - UpT) + 0.30f * UpT,
        0.13f * (1.0f - UpT) + 0.42f * UpT,
    };

    // Overhead studio softbox strip
    const Mathematics::Axis3D KeyStrip = Mathematics::NormalizeAxis({ 0.48f, 0.76f, 0.43f });
    const float StripDot = std::max(0.0f, Mathematics::EvaluateDot(WorldDir, KeyStrip));
    const float StripGlow = std::pow(StripDot, 28.0f) * 3.8f;
    Env.R += StripGlow * 1.05f;
    Env.G += StripGlow * 0.98f;
    Env.B += StripGlow * 0.90f;

    // Cool rim strip
    const Mathematics::Axis3D RimStrip = Mathematics::NormalizeAxis({ -0.78f, 0.38f, -0.49f });
    const float RimDot  = std::max(0.0f, Mathematics::EvaluateDot(WorldDir, RimStrip));
    const float RimGlow = std::pow(RimDot, 18.0f) * 1.6f;
    Env.R += RimGlow * 0.65f;
    Env.G += RimGlow * 0.85f;
    Env.B += RimGlow * 1.15f;

    return Env;
}

[[nodiscard]] Mathematics::LinearRGB SampleCheckerBackdrop(Mathematics::Axis3D RayOrigin, Mathematics::Axis3D RayDir) noexcept
{
    // Studio checkerboard plane behind the shader ball at Z = -1.45 m so refractive glass & Abbe dispersion are visibly crisp!
    if (std::fabs(RayDir.Z) > 1.0e-4f)
    {
        const float HitT = (-1.45f - RayOrigin.Z) / RayDir.Z;
        if (HitT > 0.0f)
        {
            const Mathematics::Axis3D HitP = RayOrigin + RayDir * HitT;
            if (std::fabs(HitP.X) < 1.8f && std::fabs(HitP.Y) < 1.8f)
            {
                const int32_t CellX = static_cast<int32_t>(std::floor(HitP.X * 4.5f));
                const int32_t CellY = static_cast<int32_t>(std::floor(HitP.Y * 4.5f));
                const bool Bright   = ((CellX + CellY) & 1) == 0;
                const float Val     = Bright ? 0.42f : 0.12f;
                return { Val, Val * 1.02f, Val * 1.06f };
            }
        }
    }
    return SampleStudioEnvironmentRadiance(RayDir);
}

[[nodiscard]] uint8_t EncodeAcesSrgbByte(float LinearVal) noexcept
{
    const float X = std::max(0.0f, LinearVal * 0.85f);
    // Narkowicz ACES filmic curve
    const float Mapped = std::clamp((X * (2.51f * X + 0.03f)) / (X * (2.43f * X + 0.59f) + 0.14f), 0.0f, 1.0f);
    const float Srgb   = (Mapped <= 0.0031308f) ? (12.92f * Mapped) : (1.055f * std::pow(Mapped, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::lround(std::clamp(Srgb, 0.0f, 1.0f) * 255.0f));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                        STUDIO SHADER-BALL SPECIMEN RASTERIZER
//------------------------------------------------------------------------------------------------------------------------

SpecimenPreviewImage RasterizeMaterialSpecimenPreview(
    const SurfaceMaterial& RawMaterial,
    uint32_t               ResolutionPixels)
{
    SurfaceMaterial Mat = RawMaterial;
    SanitizeSurfaceMaterial(Mat);
    const uint32_t ActiveMask = DetermineActiveLobeMask(Mat);
    const auto& AlbedoSheet   = InspectGlobalDirectionalAlbedoSheet();
    const auto& SheenSheet    = InspectGlobalSheenLinearCosineSheet();

    const uint32_t Res = std::clamp(ResolutionPixels, 16u, 512u);
    SpecimenPreviewImage Image {};
    Image.Width  = Res;
    Image.Height = Res;
    Image.Rgba8.resize(static_cast<std::size_t>(Res) * Res * 4u, 0u);

    const Mathematics::Axis3D CameraOrigin { 0.0f, 0.18f, 2.55f };
    const Mathematics::Axis3D TargetPoint  { 0.0f, 0.0f, 0.0f };
    const Mathematics::Axis3D CamForward   = Mathematics::NormalizeAxis(TargetPoint - CameraOrigin);
    const Mathematics::Axis3D CamRight     = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(CamForward, { 0.0f, 1.0f, 0.0f }));
    const Mathematics::Axis3D CamUp        = Mathematics::EvaluateCross(CamRight, CamForward);
    constexpr float SphereRadius           = 0.86f;
    constexpr float FocalScale             = 0.42f;

    struct DirectionalStudioLight
    {
        Mathematics::Axis3D    Direction;
        Mathematics::LinearRGB Radiance;
    };
    const std::array<DirectionalStudioLight, 3u> Lights {{
        { Mathematics::NormalizeAxis({  0.52f,  0.72f,  0.46f }), { 4.20f, 4.05f, 3.85f } }, // Key warm-daylight sun/softbox
        { Mathematics::NormalizeAxis({ -0.78f,  0.34f,  0.52f }), { 1.15f, 1.38f, 1.72f } }, // Cool fill
        { Mathematics::NormalizeAxis({ -0.25f,  0.62f, -0.74f }), { 2.40f, 2.55f, 2.85f } }, // Back rim (lights sheen, fiber TT & SSS!)
    }};

    const float InvRes = 1.0f / static_cast<float>(Res);

    for (uint32_t PY = 0u; PY < Res; ++PY)
    {
        for (uint32_t PX = 0u; PX < Res; ++PX)
        {
            const float ScreenX = (2.0f * (static_cast<float>(PX) + 0.5f) * InvRes - 1.0f) * FocalScale;
            const float ScreenY = (1.0f - 2.0f * (static_cast<float>(PY) + 0.5f) * InvRes) * FocalScale;
            const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(CamForward + CamRight * ScreenX + CamUp * ScreenY);

            // Analytic ray-sphere intersection at origin
            const float BTerm = Mathematics::EvaluateDot(CameraOrigin, RayDir);
            const float CTerm = Mathematics::EvaluateDot(CameraOrigin, CameraOrigin) - SphereRadius * SphereRadius;
            const float Disc  = BTerm * BTerm - CTerm;

            const std::size_t PixelBase = (static_cast<std::size_t>(PY) * Res + PX) * 4u;

            if (Disc <= 0.0f)
            {
                // Subtle dark vignette background card
                const float RadSq = ScreenX * ScreenX + ScreenY * ScreenY;
                const uint8_t Bg  = static_cast<uint8_t>(std::clamp(28.0f - 36.0f * RadSq, 16.0f, 30.0f));
                Image.Rgba8[PixelBase + 0u] = Bg;
                Image.Rgba8[PixelBase + 1u] = static_cast<uint8_t>(Bg + 4u);
                Image.Rgba8[PixelBase + 2u] = static_cast<uint8_t>(Bg + 11u);
                Image.Rgba8[PixelBase + 3u] = 255u;
                continue;
            }

            const float HitT = -BTerm - std::sqrt(Disc);
            const Mathematics::Axis3D HitPos    = CameraOrigin + RayDir * HitT;
            const Mathematics::Axis3D Normal    = HitPos * (1.0f / SphereRadius);
            const Mathematics::Axis3D Outgoing  = RayDir * -1.0f;

            // Construct continuous spherical tangent frame (+X = azimuthal tangent / brushed & fiber direction, +Y = latitude)
            Mathematics::Axis3D TangentX = Mathematics::NormalizeAxis({ -Normal.Z, 0.0f, Normal.X }, { 1.0f, 0.0f, 0.0f });
            if ((ActiveMask & LobeFiberBCSDF) != 0u)
            {
                // Orient strand fibers meridionally from top pole to bottom pole for classic hair highlight bands!
                TangentX = Mathematics::NormalizeAxis(
                    Mathematics::EvaluateCross({ -Normal.Z, 0.0f, Normal.X }, Normal),
                    { 0.0f, 1.0f, 0.0f });
            }
            const Mathematics::Axis3D BitangentY = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Normal, TangentX));
            const Scattering::SurfaceShadingFrame Frame { TangentX, BitangentY, Normal };
            const Mathematics::Axis3D LocalWo = Frame.WorldToLocal(Outgoing);

            // Spherical UV + screen footprint Jacobian for Kemppinen 2025 multiscale glints!
            const float SphericalU = 0.5f + std::atan2(Normal.X, Normal.Z) / Constants::TauFloat;
            const float SphericalV = 0.5f - std::asin(std::clamp(Normal.Y, -1.0f, 1.0f)) / Constants::PiFloat;
            const float PixelFoot  = std::max(0.0008f, InvRes * 0.65f / std::max(0.20f, LocalWo.Z));
            Scattering::TextureFootprintJacobian Footprint {};
            Footprint.SurfaceUV      = { SphericalU * 2.0f, SphericalV * 2.0f };
            Footprint.DerivativeDuDx = { PixelFoot, 0.0f };
            Footprint.DerivativeDuDy = { 0.0f, PixelFoot };

            Mathematics::LinearRGB PixelLinear = Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);

            // 1. Direct studio lights evaluated through full 10-lobe OpenPBR BSDF
            for (const auto& Light : Lights)
            {
                const Mathematics::Axis3D LocalWi = Frame.WorldToLocal(Light.Direction);
                const float CosThetaI             = std::fabs(LocalWi.Z);
                if (CosThetaI <= 1.0e-5f)
                {
                    continue;
                }
                const auto Bsdf = Scattering::EvaluateOpenPbrLocalBsdf(
                    Mat, ActiveMask, LocalWo, LocalWi, Footprint, AlbedoSheet, SheenSheet);
                PixelLinear.R += Bsdf.R * Light.Radiance.R * CosThetaI;
                PixelLinear.G += Bsdf.G * Light.Radiance.G * CosThetaI;
                PixelLinear.B += Bsdf.B * Light.Radiance.B * CosThetaI;
            }

            // 2. Deterministic 12-sample VNDF/Cosine QMC environment reflection + Abbe dispersed refraction backdrop
            constexpr uint32_t EnvSamples = 12u;
            for (uint32_t SIdx = 0u; SIdx < EnvSamples; ++SIdx)
            {
                const float U0 = std::fmod((static_cast<float>(SIdx) + 0.5f) / static_cast<float>(EnvSamples) + 0.37f * SphericalU, 1.0f);
                const float U1 = (static_cast<float>(SIdx) + 0.5f) / static_cast<float>(EnvSamples);
                const float U2 = std::fmod(static_cast<float>(SIdx) * 0.6180339887f + 0.23f * SphericalV, 1.0f);

                const auto Sample = Scattering::SampleOpenPbrScattering(Mat, ActiveMask, Frame, Outgoing, U0, U1, U2, Footprint);
                if (Sample.PdfSolidAngle > 1.0e-6f && !Sample.SampledTransmission)
                {
                    const auto EnvRad = SampleStudioEnvironmentRadiance(Sample.IncidentDirectionWorld);
                    const float InvN  = 1.0f / static_cast<float>(EnvSamples);
                    PixelLinear.R += std::min(6.0f, Sample.ThroughputWeight.R) * EnvRad.R * InvN;
                    PixelLinear.G += std::min(6.0f, Sample.ThroughputWeight.G) * EnvRad.G * InvN;
                    PixelLinear.B += std::min(6.0f, Sample.ThroughputWeight.B) * EnvRad.B * InvN;
                }
            }

            // 3. Analytic double-interface dispersed sphere refraction against studio checker backdrop for transmissive dielectrics!
            if ((ActiveMask & LobeDielectricTransmission) != 0u && Mat.Transmission.TransmissionWeight > 0.01f)
            {
                const auto DispersedIor = Scattering::ComputeAbbeDispersedIor(
                    Mat.Specular.DielectricIOR, Mat.Transmission.AbbeDispersionNumber);
                const float FresnelR = Scattering::EvaluateExactDielectricFresnel(LocalWo.Z, Mat.Specular.DielectricIOR);
                const float TransWeight = (1.0f - Mat.Specular.Metalness) * Mat.Transmission.TransmissionWeight * (1.0f - FresnelR);

                const auto TraceRefractedChannel = [&](float Eta, int ChannelIdx) noexcept -> float
                {
                    const float InvEta = 1.0f / Eta;
                    const float CosI   = std::clamp(-Mathematics::EvaluateDot(RayDir, Normal), 0.0f, 1.0f);
                    const float SinTSq = InvEta * InvEta * (1.0f - CosI * CosI);
                    if (SinTSq >= 1.0f) return 0.0f;
                    const Mathematics::Axis3D InDir = Mathematics::NormalizeAxis(
                        RayDir * InvEta + Normal * (InvEta * CosI - std::sqrt(1.0f - SinTSq)));
                    const float ChordLen = 2.0f * SphereRadius * std::max(0.1f, -Mathematics::EvaluateDot(InDir, Normal));
                    const Mathematics::Axis3D ExitPos    = HitPos + InDir * ChordLen;
                    const Mathematics::Axis3D ExitNormal = ExitPos * (-1.0f / SphereRadius); // Inward normal at exit
                    const float ExitCosI   = std::clamp(-Mathematics::EvaluateDot(InDir, ExitNormal), 0.0f, 1.0f);
                    const float ExitSinTSq = Eta * Eta * (1.0f - ExitCosI * ExitCosI);
                    const Mathematics::Axis3D OutRay = (ExitSinTSq < 1.0f)
                        ? Mathematics::NormalizeAxis(InDir * Eta + ExitNormal * (Eta * ExitCosI - std::sqrt(1.0f - ExitSinTSq)))
                        : InDir;
                    const auto BackCol = SampleCheckerBackdrop(ExitPos, OutRay);
                    const float PathScale = ChordLen / std::max(1.0e-3f, Mat.Transmission.AttenuationDistanceMeter * 4.0f);
                    const float AttenC = (ChannelIdx == 0)
                        ? Mat.Transmission.AttenuationColor.R
                        : ((ChannelIdx == 1) ? Mat.Transmission.AttenuationColor.G : Mat.Transmission.AttenuationColor.B);
                    const float BackC = (ChannelIdx == 0) ? BackCol.R : ((ChannelIdx == 1) ? BackCol.G : BackCol.B);
                    return BackC * std::pow(std::clamp(AttenC, 1.0e-3f, 1.0f), PathScale);
                };

                PixelLinear.R += TransWeight * TraceRefractedChannel(DispersedIor.X, 0);
                PixelLinear.G += TransWeight * TraceRefractedChannel(DispersedIor.Y, 1);
                PixelLinear.B += TransWeight * TraceRefractedChannel(DispersedIor.Z, 2);
            }

            Image.Rgba8[PixelBase + 0u] = EncodeAcesSrgbByte(PixelLinear.R);
            Image.Rgba8[PixelBase + 1u] = EncodeAcesSrgbByte(PixelLinear.G);
            Image.Rgba8[PixelBase + 2u] = EncodeAcesSrgbByte(PixelLinear.B);
            Image.Rgba8[PixelBase + 3u] = 255u;
        }
    }

    return Image;
}

} // namespace Frontier::MaterialSpace
