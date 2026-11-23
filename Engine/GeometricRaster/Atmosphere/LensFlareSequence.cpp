/*============================================================================================================================================
                                                          LENSFLARESEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Baked multi-archetype Lens Flare optical kernel atlas (Anamorphic, Streaks, Halos), GPU bake dispatch & CPU/GPU evaluation

#include "Engine/GeometricRaster/Atmosphere/LensFlareSequence.h"

#include "Engine/Codecs/ImageCodec.h"
#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] float SmoothStep(float Edge0, float Edge1, float X) noexcept
{
    const float T = std::clamp((X - Edge0) / std::max(1.0e-5f, Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] uint8_t EncodeLinearHdrToSrgbByte(float Lin) noexcept
{
    const float Norm = std::clamp(Lin / LensFlareSequence::AtlasHdrScale, 0.0f, 1.0f);
    const float Srgb = (Norm <= 0.0031308f) ? (Norm * 12.92f) : (1.055f * std::pow(Norm, 1.0f / 2.2f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::round(Srgb * 255.0f), 0.0f, 255.0f));
}

[[nodiscard]] float DecodeSrgbByteToLinearHdr(uint8_t ByteVal) noexcept
{
    const float S = static_cast<float>(ByteVal) * (1.0f / 255.0f);
    const float L = (S <= 0.04045f) ? (S / 12.92f) : std::pow((S + 0.055f) / 1.055f, 2.2f);
    return L * LensFlareSequence::AtlasHdrScale;
}

[[nodiscard]] Mathematics::LinearRGB SampleBakedTileBilinear(
    const std::vector<uint8_t>& Atlas,
    uint32_t                    StyleIndex,
    float                       U,
    float                       V) noexcept
{
    const uint32_t W = LensFlareSequence::AtlasTileWidthPx;
    const uint32_t H = LensFlareSequence::AtlasTileHeightPx;
    if (U <= 0.0f || U >= 1.0f || V <= 0.0f || V >= 1.0f ||
        Atlas.size() < static_cast<std::size_t>(W) * H * LensFlareSequence::AtlasStyleCount * 4u)
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    const uint32_t S = std::min(StyleIndex, LensFlareSequence::AtlasStyleCount - 1u);
    const float FX = std::clamp(U, 0.0f, 1.0f) * static_cast<float>(W - 1u);
    const float FY = std::clamp(V, 0.0f, 1.0f) * static_cast<float>(H - 1u);
    const uint32_t X0 = static_cast<uint32_t>(std::floor(FX));
    const uint32_t Y0 = static_cast<uint32_t>(std::floor(FY));
    const uint32_t X1 = std::min(X0 + 1u, W - 1u);
    const uint32_t Y1 = std::min(Y0 + 1u, H - 1u);
    const float TX = FX - static_cast<float>(X0);
    const float TY = FY - static_cast<float>(Y0);

    const uint32_t RowOff = S * H;
    const auto Fetch = [&](uint32_t IX, uint32_t IY) noexcept -> Mathematics::LinearRGB
    {
        const std::size_t Idx = (static_cast<std::size_t>(RowOff + IY) * W + IX) * 4u;
        return {
            DecodeSrgbByteToLinearHdr(Atlas[Idx + 0u]),
            DecodeSrgbByteToLinearHdr(Atlas[Idx + 1u]),
            DecodeSrgbByteToLinearHdr(Atlas[Idx + 2u]),
        };
    };

    const auto C00 = Fetch(X0, Y0);
    const auto C10 = Fetch(X1, Y0);
    const auto C01 = Fetch(X0, Y1);
    const auto C11 = Fetch(X1, Y1);
    const auto Lerp = [](const Mathematics::LinearRGB& A, const Mathematics::LinearRGB& B, float T) noexcept -> Mathematics::LinearRGB
    {
        return { A.R + (B.R - A.R) * T, A.G + (B.G - A.G) * T, A.B + (B.B - A.B) * T };
    };
    return Lerp(Lerp(C00, C10, TX), Lerp(C01, C11, TX), TY);
}

[[nodiscard]] float PolygonalApertureDistance(float LX, float LY, uint32_t Sides) noexcept
{
    const float R = std::sqrt(LX * LX + LY * LY);
    if (Sides < 3u)
    {
        return R;
    }
    const float Angle  = std::atan2(LY, LX) + 0.5235988f;
    const float Sector = Constants::TauFloat / static_cast<float>(Sides);
    const float ModAng = (Angle - Sector * std::floor(Angle / Sector)) - 0.5f * Sector;
    return R * std::cos(ModAng) / std::max(std::cos(0.5f * Sector), 0.25f);
}

[[nodiscard]] Mathematics::LinearRGB EvaluateBakedKernelPixel(uint32_t StyleIdx, float U, float V) noexcept
{
    // Centered coordinates: KX in [-1, 1] across 512px width, KY in [-1, 1] across 256px height
    const float KX = (U - 0.5f) * 2.0f;
    const float KY = (V - 0.5f) * 2.0f;
    // Aspect-corrected radial distance (2:1 tile ratio) for circularly symmetric diffraction & halos
    const float CX = KX * 2.0f;
    const float CY = KY;
    const float RC = std::sqrt(CX * CX + CY * CY);
    const float R  = std::sqrt(KX * KX + KY * KY);

    const float EdgeX = 1.0f - SmoothStep(0.84f, 0.99f, std::abs(KX));
    const float EdgeY = 1.0f - SmoothStep(0.82f, 0.99f, std::abs(KY));
    const float RectFade = EdgeX * EdgeY;
    const float CircFade = 1.0f - SmoothStep(0.78f, 0.98f, R);

    Mathematics::LinearRGB Col { 0.0f, 0.0f, 0.0f };

    if (StyleIdx == 0u) // Anamorphic: intense horizontal cyan-sapphire cinema streak + side fringes + soft anamorphic aura
    {
        const float Core      = std::exp(-RC * RC / 0.0042f);
        const float StreakFin = std::exp(-std::abs(KY) / 0.009f) * std::exp(-std::abs(KX) / 0.52f);
        const float StreakMed = std::exp(-std::abs(KY) / 0.032f) * std::exp(-std::abs(KX) / 0.72f);
        const float WingPeak  = std::exp(-std::abs(KY) / 0.018f) *
                                (std::exp(-std::pow(std::abs(KX) - 0.34f, 2.0f) / 0.016f));
        const float AnamAura  = std::exp(-RC * RC / 0.140f) * 0.16f;

        Col.R = (Core * 1.45f + StreakFin * 0.38f + StreakMed * 0.10f + WingPeak * 0.16f + AnamAura * 0.35f) * RectFade;
        Col.G = (Core * 1.55f + StreakFin * 0.95f + StreakMed * 0.34f + WingPeak * 0.42f + AnamAura * 0.65f) * RectFade;
        Col.B = (Core * 1.70f + StreakFin * 1.55f + StreakMed * 0.78f + WingPeak * 0.82f + AnamAura * 1.10f) * RectFade;
    }
    else if (StyleIdx == 1u) // Streaks: crisp 12-spoke starburst diffraction spikes + subtle spectral shimmer + halo
    {
        const float Theta     = std::atan2(KY, KX);
        const float Primary   = std::pow(std::abs(std::cos(Theta * 6.0f)), 84.0f) * std::exp(-R / 0.38f);
        const float Secondary = std::pow(std::abs(std::cos((Theta + 0.261799f) * 6.0f)), 128.0f) * std::exp(-R / 0.24f);
        const float Needle    = std::pow(std::abs(std::cos(Theta * 3.0f)), 140.0f) * std::exp(-R / 0.52f) * 0.60f;
        const float Core      = std::exp(-R * R / 0.0038f);
        const float Aureole   = std::exp(-R / 0.095f);
        const float HaloRing  = std::exp(-std::pow(R - 0.54f, 2.0f) / 0.0024f) * 0.14f;

        const float SpecPhase = R * 20.0f;
        const float SpecR = 0.90f + 0.16f * std::sin(SpecPhase);
        const float SpecG = 0.88f + 0.14f * std::sin(SpecPhase + 2.094f);
        const float SpecB = 0.85f + 0.16f * std::sin(SpecPhase + 4.188f);

        const float SpokeSum = Primary * 1.35f + Secondary * 0.95f + Needle;
        Col.R = (Core * 1.55f + Aureole * 0.42f + SpokeSum * SpecR + HaloRing * 0.95f) * CircFade;
        Col.G = (Core * 1.48f + Aureole * 0.34f + SpokeSum * SpecG + HaloRing * 0.85f) * CircFade;
        Col.B = (Core * 1.35f + Aureole * 0.24f + SpokeSum * SpecB + HaloRing * 0.72f) * CircFade;
    }
    else // Halos: delicate concentric rainbow thin-film diffraction halos + solar aureole + iris sparkle
    {
        const float Theta   = std::atan2(KY, KX);
        const float Core    = std::exp(-R * R / 0.0036f);
        const float Aureole = std::exp(-R / 0.11f);

        const float R1 = std::exp(-std::pow(R - 0.370f, 2.0f) / 0.00075f);
        const float G1 = std::exp(-std::pow(R - 0.342f, 2.0f) / 0.00075f);
        const float B1 = std::exp(-std::pow(R - 0.314f, 2.0f) / 0.00075f);

        const float R2 = std::exp(-std::pow(R - 0.640f, 2.0f) / 0.0012f);
        const float G2 = std::exp(-std::pow(R - 0.608f, 2.0f) / 0.0012f);
        const float B2 = std::exp(-std::pow(R - 0.576f, 2.0f) / 0.0012f);

        const float Sparkle = std::pow(std::abs(std::cos(Theta * 4.0f)), 64.0f) * std::exp(-R / 0.20f) * 0.24f;

        Col.R = (Core * 1.45f + Aureole * 0.38f + R1 * 0.58f + R2 * 0.36f + Sparkle * 1.00f) * CircFade;
        Col.G = (Core * 1.40f + Aureole * 0.31f + G1 * 0.54f + G2 * 0.33f + Sparkle * 0.88f) * CircFade;
        Col.B = (Core * 1.32f + Aureole * 0.24f + B1 * 0.62f + B2 * 0.38f + Sparkle * 0.78f) * CircFade;
    }

    return Col;
}

} // namespace

void LensFlareSequence::ApplyLensFlarePreset(
    LensFlareConfiguration& InOutConfig,
    LensFlareStyle          Style) noexcept
{
    InOutConfig.Style = Style;
    switch (Style)
    {
        case LensFlareStyle::Anamorphic:
            InOutConfig.FlareIntensity        = 0.88f;
            InOutConfig.FlareSpread           = 0.90f;
            InOutConfig.HaloIntensity         = 0.52f;
            InOutConfig.GhostIntensity        = 0.48f;
            InOutConfig.GhostSpacing          = 0.72f;
            InOutConfig.GhostCount            = 7u;
            InOutConfig.GhostSides            = 6u;
            InOutConfig.StreakRotationDegrees = 0.0f;
            break;
        case LensFlareStyle::Streaks:
            InOutConfig.FlareIntensity        = 0.92f;
            InOutConfig.FlareSpread           = 0.85f;
            InOutConfig.HaloIntensity         = 0.58f;
            InOutConfig.GhostIntensity        = 0.42f;
            InOutConfig.GhostSpacing          = 0.68f;
            InOutConfig.GhostCount            = 6u;
            InOutConfig.GhostSides            = 6u;
            InOutConfig.StreakRotationDegrees = 14.0f;
            break;
        case LensFlareStyle::Halos:
            InOutConfig.FlareIntensity        = 0.85f;
            InOutConfig.FlareSpread           = 0.95f;
            InOutConfig.HaloIntensity         = 0.95f;
            InOutConfig.GhostIntensity        = 0.72f;
            InOutConfig.GhostSpacing          = 0.82f;
            InOutConfig.GhostCount            = 9u;
            InOutConfig.GhostSides            = 8u;
            InOutConfig.StreakRotationDegrees = 22.0f;
            break;
    }
}

std::vector<uint8_t> LensFlareSequence::BakeOpticalKernelAtlasRgba8()
{
    const uint32_t TotalH = AtlasTileHeightPx * AtlasStyleCount;
    std::vector<uint8_t> Pixels(static_cast<std::size_t>(AtlasTileWidthPx) * TotalH * 4u, 255u);

    for (uint32_t S = 0u; S < AtlasStyleCount; ++S)
    {
        for (uint32_t Y = 0u; Y < AtlasTileHeightPx; ++Y)
        {
            const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(AtlasTileHeightPx);
            for (uint32_t X = 0u; X < AtlasTileWidthPx; ++X)
            {
                const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(AtlasTileWidthPx);
                const auto Lin = EvaluateBakedKernelPixel(S, U, V);
                const std::size_t Idx = (static_cast<std::size_t>(S * AtlasTileHeightPx + Y) * AtlasTileWidthPx + X) * 4u;
                Pixels[Idx + 0u] = EncodeLinearHdrToSrgbByte(Lin.R);
                Pixels[Idx + 1u] = EncodeLinearHdrToSrgbByte(Lin.G);
                Pixels[Idx + 2u] = EncodeLinearHdrToSrgbByte(Lin.B);
                Pixels[Idx + 3u] = 255u;
            }
        }
    }
    return Pixels;
}

bool LensFlareSequence::BakeOpticalAtlasOnGpu(
    VulkanInterchange&           Device,
    const std::filesystem::path& CompiledShaderDirectory,
    std::vector<uint8_t>&        OutAtlasRgba8)
{
    if (!Device.Ready())
    {
        return false;
    }

    ComputeDispatchEncoder Encoder {};
    if (!Encoder.InitializeEncoder(&Device, 4u))
    {
        return false;
    }

    const std::array<VkDescriptorType, 2u> SlotTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    const uint32_t Program = Encoder.RegisterProgram(
        CompiledShaderDirectory / "LensFlareBakeCompute.comp.spv",
        SlotTypes,
        "ComputeEntry");
    if (Program == UINT32_MAX)
    {
        Encoder.TeardownEncoder();
        return false;
    }

    struct BakeUniformStd140
    {
        uint32_t Dimensions[4] {}; // x = TileW, y = TileH, z = StyleCount, w = Pad
        float    Params[4]     {}; // x = HdrScale, y..w = Pad
    };
    const uint32_t TotalH = AtlasTileHeightPx * AtlasStyleCount;
    const uint64_t OutBytes = static_cast<uint64_t>(AtlasTileWidthPx) * TotalH * sizeof(uint32_t);

    auto UniBuf = Encoder.AllocateHostWritableBuffer(sizeof(BakeUniformStd140), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    auto OutBuf = Encoder.AllocateHostWritableBuffer(OutBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (UniBuf.BufferHandle == VK_NULL_HANDLE || OutBuf.BufferHandle == VK_NULL_HANDLE)
    {
        Device.ReleaseBuffer(UniBuf);
        Device.ReleaseBuffer(OutBuf);
        Encoder.TeardownEncoder();
        return false;
    }

    const std::array<BakeUniformStd140, 1u> UniData {{
        { { AtlasTileWidthPx, AtlasTileHeightPx, AtlasStyleCount, 0u }, { AtlasHdrScale, 0.0f, 0.0f, 0.0f } }
    }};
    Encoder.UploadSpanToBuffer<BakeUniformStd140>(UniBuf, UniData);

    Encoder.ResetTransientDescriptors();
    VkCommandBuffer Cmd = Device.BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        Device.ReleaseBuffer(UniBuf);
        Device.ReleaseBuffer(OutBuf);
        Encoder.TeardownEncoder();
        return false;
    }

    const std::array<ComputeSlotDescriptor, 2u> Slots {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, UniBuf.BufferHandle, sizeof(BakeUniformStd140) },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, OutBuf.BufferHandle, OutBytes },
    }};
    const uint32_t GX = (AtlasTileWidthPx + 7u) / 8u;
    const uint32_t GY = (TotalH + 7u) / 8u;
    if (!Encoder.EncodeComputeDispatch(Cmd, Program, Slots, GX, GY, 1u))
    {
        Device.ReleaseBuffer(UniBuf);
        Device.ReleaseBuffer(OutBuf);
        Encoder.TeardownEncoder();
        return false;
    }

    VkMemoryBarrier HostBarrier {};
    HostBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    HostBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    HostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(
        Cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0u,
        1u,
        &HostBarrier,
        0u,
        nullptr,
        0u,
        nullptr);

    const uint64_t SyncPoint = Device.DispatchCommandBufferAsync(Cmd);
    const bool Ok = Device.AwaitTimelineTick(SyncPoint, 60000000000ull);
    if (Ok && OutBuf.HostAddress != nullptr)
    {
        OutAtlasRgba8.resize(static_cast<std::size_t>(OutBytes));
        std::memcpy(OutAtlasRgba8.data(), OutBuf.HostAddress, static_cast<std::size_t>(OutBytes));
    }

    Device.ReleaseBuffer(UniBuf);
    Device.ReleaseBuffer(OutBuf);
    Encoder.TeardownEncoder();
    return Ok;
}

bool LensFlareSequence::SaveBakedAtlasToImageSpace(
    const std::filesystem::path& TargetPath,
    std::span<const uint8_t>     AtlasRgba8)
{
    const std::size_t Expected = static_cast<std::size_t>(AtlasTileWidthPx) * AtlasTileHeightPx * AtlasStyleCount * 4u;
    if (AtlasRgba8.size() != Expected)
    {
        return false;
    }
    std::filesystem::create_directories(TargetPath.parent_path());

    Codecs::ImageSpaceDocumentModel Doc {};
    Doc.DocumentTitle   = "LensFlareBakedAtlas";
    Doc.NominalWidthPx  = AtlasTileWidthPx;
    Doc.NominalHeightPx = AtlasTileHeightPx * AtlasStyleCount;
    Doc.Layers          = {
        { "Anamorphic", 1.0, true },
        { "Streaks",    1.0, true },
        { "Halos",      1.0, true },
    };
    Doc.LinearRgbaPreview.resize(Expected);
    for (std::size_t I = 0u; I < Expected; ++I)
    {
        Doc.LinearRgbaPreview[I] = static_cast<float>(AtlasRgba8[I]) * (1.0f / 255.0f);
    }
    std::string Diag {};
    return Codecs::ImageEncoder::EncodeToFile(TargetPath, Doc, Diag);
}

bool LensFlareSequence::LoadBakedAtlasFromImageSpace(
    const std::filesystem::path& SourcePath,
    std::vector<uint8_t>&        OutAtlasRgba8)
{
    const std::size_t Expected = static_cast<std::size_t>(AtlasTileWidthPx) * AtlasTileHeightPx * AtlasStyleCount * 4u;
    if (!std::filesystem::exists(SourcePath))
    {
        return false;
    }
    Codecs::ImageSpaceDocumentModel Doc {};
    std::string Diag {};
    if (!Codecs::ImageDecoder::DecodeFromFile(SourcePath, Doc, Diag) ||
        Doc.NominalWidthPx != AtlasTileWidthPx ||
        Doc.NominalHeightPx != AtlasTileHeightPx * AtlasStyleCount ||
        Doc.LinearRgbaPreview.size() != Expected)
    {
        return false;
    }
    OutAtlasRgba8.resize(Expected);
    for (std::size_t I = 0u; I < Expected; ++I)
    {
        OutAtlasRgba8[I] = static_cast<uint8_t>(std::clamp(std::round(Doc.LinearRgbaPreview[I] * 255.0f), 0.0f, 255.0f));
    }
    return true;
}

const std::vector<uint8_t>& LensFlareSequence::QueryBakedLensFlareAtlasRgba8()
{
    static const std::vector<uint8_t> Cached = []() -> std::vector<uint8_t>
    {
        const std::filesystem::path BakedPath =
            std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "EngineContent/Optics/LensFlareBakedAtlas.ImageSpace";
        std::vector<uint8_t> Loaded {};
        if (LoadBakedAtlasFromImageSpace(BakedPath, Loaded))
        {
            return Loaded;
        }
        std::vector<uint8_t> Baked = BakeOpticalKernelAtlasRgba8();
        (void)SaveBakedAtlasToImageSpace(BakedPath, Baked);
        return Baked;
    }();
    return Cached;
}

ProjectedLensFlareSlice LensFlareSequence::ResolveSolarFlareProjection(
    const Mathematics::Axis3D&                   CameraOrigin,
    const Mathematics::Axis3D&                   CameraForward,
    const Mathematics::Axis3D&                   CameraRight,
    const Mathematics::Axis3D&                   CameraUp,
    float                                        TanHalfFovX,
    float                                        TanHalfFovY,
    const WorldAtmosphereConfiguration&          Atmosphere,
    const SpatialAcceleration::WideBoundingTree* OptionalSceneTree) noexcept
{
    ProjectedLensFlareSlice Out {};
    if (!Atmosphere.Medium.AtmosphereEnabled || !Atmosphere.Solar.SolarEnabled || !Atmosphere.LensFlare.Enabled)
    {
        return Out;
    }

    const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(Atmosphere.Solar);
    const auto& SunDir     = SolarSolved.WorldDirectionToSun;
    const float ForwardDot = Mathematics::EvaluateDot(SunDir, CameraForward);
    if (ForwardDot <= 0.05f || SunDir.Z < -0.04f)
    {
        return Out;
    }

    const float NdcX = Mathematics::EvaluateDot(SunDir, CameraRight) / (ForwardDot * std::max(1.0e-4f, TanHalfFovX));
    const float NdcY = Mathematics::EvaluateDot(SunDir, CameraUp)    / (ForwardDot * std::max(1.0e-4f, TanHalfFovY));
    Out.ScreenU = 0.5f + 0.5f * NdcX;
    Out.ScreenV = 0.5f - 0.5f * NdcY;

    // Offscreen falloff outside [-0.30, 1.30]
    const float EdgeX = 1.0f - SmoothStep(0.95f, 1.35f, std::abs(NdcX));
    const float EdgeY = 1.0f - SmoothStep(0.95f, 1.35f, std::abs(NdcY));
    const float ScreenFade = EdgeX * EdgeY;
    if (ScreenFade <= 0.001f)
    {
        return Out;
    }

    // Check 5-point solar disc visibility against geometry BVH
    float GeomVis = 1.0f;
    if (OptionalSceneTree != nullptr && !OptionalSceneTree->WideEntries.empty())
    {
        const Mathematics::Axis3D UpRef = (std::abs(SunDir.Z) < 0.98f)
            ? Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f }
            : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
        const Mathematics::Axis3D T = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(UpRef, SunDir));
        const Mathematics::Axis3D B = Mathematics::EvaluateCross(SunDir, T);
        const float HalfR = std::max(0.002f, Atmosphere.Solar.AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat);

        static constexpr std::array<std::array<float, 2u>, 5u> Offs {{
            { 0.0f, 0.0f }, { 0.65f, 0.0f }, { -0.65f, 0.0f }, { 0.0f, 0.65f }, { 0.0f, -0.65f }
        }};
        uint32_t ClearCount = 0u;
        for (const auto& O : Offs)
        {
            SpatialAcceleration::RaySegmentQuery Q {};
            Q.Origin          = CameraOrigin;
            Q.UnitDirection   = Mathematics::NormalizeAxis(SunDir + T * (O[0] * HalfR) + B * (O[1] * HalfR));
            Q.MinimumDistance = 0.02f;
            Q.MaximumDistance = 200.0f;
            if (!OptionalSceneTree->QueryRayOccluded(Q))
            {
                ++ClearCount;
            }
        }
        GeomVis = static_cast<float>(ClearCount) * 0.2f;
    }

    const auto  SolarTrans = AtmosphericSkySequence::ComputeSolarTransmittance(SunDir, Atmosphere.Medium);
    const float HorizonVis = SmoothStep(-0.02f, 0.14f, SunDir.Z);
    const float CloudVis   = Atmosphere.Clouds.CloudsEnabled
        ? VolumetricCloudSequence::EvaluateAllSolarCloudTransmittance(CameraOrigin, SunDir, Atmosphere)
        : 1.0f;

    // Account for lunar angular eclipse attenuation over the solar disc
    float EclipseVis = 1.0f;
    const float SunR = std::max(0.002f, Atmosphere.Solar.AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat);
    for (std::size_t MIdx = 0u; MIdx < std::min<std::size_t>(Atmosphere.Moons.size(), LunarStarfieldSequence::MaxMoonSlots); ++MIdx)
    {
        const auto& M = Atmosphere.Moons[MIdx];
        if (!M.Visible) continue;
        const auto  MDir = LunarStarfieldSequence::ResolveMoonWorldDirection(M, SunDir);
        const float AngR = std::max(0.001f, M.AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat);
        const float Sep  = std::acos(std::clamp(Mathematics::EvaluateDot(SunDir, MDir), -1.0f, 1.0f));
        if (Sep < AngR + SunR)
        {
            const float AreaRatio = std::min(1.0f, (AngR * AngR) / std::max(1.0e-8f, SunR * SunR));
            const float Overlap   = 1.0f - SmoothStep(std::max(0.0f, AngR - SunR), AngR + SunR, Sep);
            EclipseVis *= std::clamp(1.0f - Overlap * AreaRatio, 0.0f, 1.0f);
        }
    }

    Out.EffectiveGain = Atmosphere.LensFlare.FlareIntensity * ScreenFade * GeomVis * HorizonVis * EclipseVis *
                        (0.30f + 0.70f * CloudVis) *
                        std::clamp(Atmosphere.Solar.SolarIntensityMultiplier * 0.055f, 0.1f, 2.5f);
    Out.SourceTint = {
        SolarSolved.EffectiveSolarTint.R * (0.35f + 0.65f * SolarTrans.R),
        SolarSolved.EffectiveSolarTint.G * (0.35f + 0.65f * SolarTrans.G),
        SolarSolved.EffectiveSolarTint.B * (0.35f + 0.65f * SolarTrans.B),
    };
    return Out;
}

Mathematics::LinearRGB LensFlareSequence::EvaluateBakedLensFlare(
    const LensFlareConfiguration&  Config,
    const ProjectedLensFlareSlice& Projection,
    float                          ScreenU,
    float                          ScreenV,
    float                          AspectRatio) noexcept
{
    if (!Config.Enabled || Projection.EffectiveGain <= 1.0e-4f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const auto& Atlas   = QueryBakedLensFlareAtlasRgba8();
    const float Gain    = Projection.EffectiveGain;
    const float Spread  = std::clamp(Config.FlareSpread, 0.2f, 2.2f);
    const uint32_t SIdx = static_cast<uint32_t>(Config.Style);

    // Aspect-corrected offset around projected light source
    const float DX = (ScreenU - Projection.ScreenU) * AspectRatio;
    const float DY = (ScreenV - Projection.ScreenV);

    // Apply rotation for Streaks/Halos (and optional subtle tilt for Anamorphic)
    const float RotRad = Config.StreakRotationDegrees * Constants::DegToRadFloat;
    const float CosR = std::cos(RotRad);
    const float SinR = std::sin(RotRad);
    const float RX   = DX * CosR + DY * SinR;
    const float RY   = -DX * SinR + DY * CosR;

    // Map into baked 512x256 tile space (2:1 tile ratio)
    const float KernelScaleX = (SIdx == 0u) ? (1.28f * Spread) : (0.82f * Spread);
    const float KernelScaleY = (SIdx == 0u) ? (0.48f * Spread) : (0.82f * Spread);
    const float KU = 0.5f + 0.5f * (RX / std::max(0.05f, KernelScaleX));
    const float KV = 0.5f + 0.5f * (RY / std::max(0.05f, KernelScaleY));

    Mathematics::LinearRGB Accum = SampleBakedTileBilinear(Atlas, SIdx, KU, KV);
    Accum.R *= Gain * 0.52f * Projection.SourceTint.R;
    Accum.G *= Gain * 0.52f * Projection.SourceTint.G;
    Accum.B *= Gain * 0.52f * Projection.SourceTint.B;

    // Chromatic spectral halo ring layer (modulated by HaloIntensity across all 3 archetypes)
    const float Dist = std::sqrt(DX * DX + DY * DY);
    if (Config.HaloIntensity > 1.0e-4f)
    {
        const float HaloRad = 0.21f * Spread;
        const float Sig     = 0.00065f * Spread * Spread;
        const float RingR   = std::exp(-std::pow(Dist - HaloRad * 1.055f, 2.0f) / Sig);
        const float RingG   = std::exp(-std::pow(Dist - HaloRad * 1.000f, 2.0f) / Sig);
        const float RingB   = std::exp(-std::pow(Dist - HaloRad * 0.945f, 2.0f) / Sig);
        const float HWeight = (SIdx == 2u) ? 0.26f : 0.16f;
        const float HGain   = Config.HaloIntensity * Gain * HWeight;
        Accum.R += RingR * HGain * Projection.SourceTint.R;
        Accum.G += RingG * HGain * Projection.SourceTint.G;
        Accum.B += RingB * HGain * Projection.SourceTint.B;
    }

    // Multi-coated polygonal aperture ghost reflections along optical axis
    if (Config.GhostIntensity > 1.0e-4f && Config.GhostCount > 0u)
    {
        static constexpr std::array<Mathematics::LinearRGB, 4u> CoatingTints {{
            { 0.32f, 0.82f, 0.56f },
            { 0.42f, 0.58f, 0.96f },
            { 0.95f, 0.64f, 0.34f },
            { 0.66f, 0.44f, 0.88f },
        }};
        const float AxisX   = 0.5f - Projection.ScreenU;
        const float AxisY   = 0.5f - Projection.ScreenV;
        const float Spacing = 0.75f + std::clamp(Config.GhostSpacing, 0.0f, 2.0f) * 1.55f;
        const uint32_t GCount = std::min<uint32_t>(Config.GhostCount, 16u);
        for (uint32_t Idx = 0u; Idx < GCount; ++Idx)
        {
            const float StepT = 0.48f + (static_cast<float>(Idx + 1u) / static_cast<float>(std::max(1u, GCount))) * Spacing;
            const float GU    = Projection.ScreenU + AxisX * StepT;
            const float GV    = Projection.ScreenV + AxisY * StepT;
            const float Rad   = (0.020f + static_cast<float>((Idx * 7u) % 11u) * 0.0055f) * (0.65f + 0.35f * Spread);
            const float LX    = ((ScreenU - GU) * AspectRatio) / std::max(Rad, 1.0e-4f);
            const float LY    = (ScreenV - GV) / std::max(Rad, 1.0e-4f);
            const float SD    = PolygonalApertureDistance(LX, LY, Config.GhostSides);
            if (SD <= 1.15f)
            {
                const float Rim  = SmoothStep(0.58f, 0.95f, SD) * (1.0f - SmoothStep(0.96f, 1.12f, SD));
                const float Fill = (1.0f - SmoothStep(0.0f, 1.02f, SD)) * 0.24f;
                const auto& Tint = CoatingTints[Idx & 3u];
                const float GVal = (Fill + Rim * 0.85f) * Config.GhostIntensity * Gain * 0.26f;
                Accum.R += Tint.R * GVal * Projection.SourceTint.R;
                Accum.G += Tint.G * GVal * Projection.SourceTint.G;
                Accum.B += Tint.B * GVal * Projection.SourceTint.B;
            }
        }
    }

    return Accum;
}

} // namespace Frontier::GeometricRaster
