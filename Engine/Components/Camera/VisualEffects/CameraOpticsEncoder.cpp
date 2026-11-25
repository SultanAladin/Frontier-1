/*============================================================================================================================================
                                                           CAMERAOPTICSENCODER.CPP
============================================================================================================================================*/
// 🧩 Implementation of GPU .slang CameraOpticsEncoder executing CameraVisualEffects.comp.spv

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/VisualEffects/CameraOpticsEncoder.h"

#include "Engine/Components/Camera/VisualEffects/ChromaticAberration.h"
#include "Engine/Components/Camera/VisualEffects/DepthOfField.h"
#include "Engine/Components/Camera/VisualEffects/LensFlareEffect.h"
#include "Engine/Components/Camera/VisualEffects/MotionBlurEffect.h"
#include "Engine/Components/Camera/VisualEffects/VignetteEffect.h"
#include "Engine/Components/Camera/VisualEffects/WorldDistortionEffect.h"
#include "Engine/GeometricRaster/Atmosphere/LensFlareSequence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Frontier::Components::VisualEffects
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

CameraOpticsEncoder::~CameraOpticsEncoder()
{
    TeardownOpticsEncoder();
}

bool CameraOpticsEncoder::InitializeOpticsEncoder(
    GeometricRaster::VulkanInterchange* BoundDevice,
    const std::filesystem::path&        CompiledShaderDirectory)
{
    TeardownOpticsEncoder();
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return false;
    }

    DeviceInterchange = BoundDevice;
    if (!ComputeEncoder.InitializeEncoder(DeviceInterchange, 32u) ||
        !TransferRegister.InitializeRegister(DeviceInterchange, 8ull * 1024ull * 1024ull))
    {
        TeardownOpticsEncoder();
        return false;
    }

    const std::array<VkDescriptorType, 5u> SlotCategories {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    OpticsProgram = ComputeEncoder.RegisterProgram(
        CompiledShaderDirectory / "CameraVisualEffects.comp.spv",
        SlotCategories);
    if (OpticsProgram == UINT32_MAX)
    {
        TeardownOpticsEncoder();
        return false;
    }

    UniformBuffer = ComputeEncoder.AllocateHostWritableBuffer(
        sizeof(GPUCameraOpticsUniforms),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    std::vector<uint8_t> FlareAtlasRgba {};
    if (!GeometricRaster::LensFlareSequence::BakeOpticalAtlasOnGpu(*DeviceInterchange, CompiledShaderDirectory, FlareAtlasRgba))
    {
        FlareAtlasRgba = GeometricRaster::LensFlareSequence::QueryBakedLensFlareAtlasRgba8();
    }
    const uint64_t FlareAtlasBytes = std::max<uint64_t>(4ull, static_cast<uint64_t>(FlareAtlasRgba.size()));
    BakedFlareAtlasBuffer = ComputeEncoder.AllocateHostWritableBuffer(
        FlareAtlasBytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (BakedFlareAtlasBuffer.HostAddress != nullptr && !FlareAtlasRgba.empty())
    {
        std::memcpy(BakedFlareAtlasBuffer.HostAddress, FlareAtlasRgba.data(), FlareAtlasRgba.size());
    }
    return UniformBuffer.BufferHandle != VK_NULL_HANDLE && BakedFlareAtlasBuffer.BufferHandle != VK_NULL_HANDLE;
}

void CameraOpticsEncoder::TeardownOpticsEncoder() noexcept
{
    if (DeviceInterchange != nullptr)
    {
        DeviceInterchange->ReleaseBuffer(UniformBuffer);
        DeviceInterchange->ReleaseBuffer(SourceBuffer);
        DeviceInterchange->ReleaseBuffer(DepthBuffer);
        DeviceInterchange->ReleaseBuffer(OutputBuffer);
        DeviceInterchange->ReleaseBuffer(BakedFlareAtlasBuffer);
    }
    TransferRegister.TeardownRegister();
    ComputeEncoder.TeardownEncoder();
    OpticsProgram     = UINT32_MAX;
    DeviceInterchange = nullptr;
}

uint32_t CameraOpticsEncoder::EvaluateActiveEffectMask(const CameraConfiguration& Config) noexcept
{
    uint32_t Mask = 0u;
    Mask |= PackDepthOfFieldMask(Config.DepthOfField);
    Mask |= PackChromaticAberrationMask(Config.ChromaticAberration);
    Mask |= PackLensFlareEffectMask(Config.LensFlare);
    Mask |= PackVignetteMask(Config.Vignette);
    Mask |= PackMotionBlurMask(Config.MotionBlur);
    Mask |= PackHeatShimmerMask(Config.HeatShimmer);
    Mask |= PackBlackHoleLensMask(Config.BlackHoleLens);
    Mask |= PackGlitchEffectMask(Config.Glitch);
#if !FRONTIER_PROFILE_SHIPPING
    if (Config.Diagnostics.ShowFocusPlane)
    {
        Mask |= 128u;
    }
#endif
    return Mask;
}

bool CameraOpticsEncoder::ExecuteGPUOptics(
    uint32_t                   WidthPx,
    uint32_t                   HeightPx,
    const CameraConfiguration& Config,
    std::span<const float>     OptionalLinearDepthMeters,
    std::vector<uint8_t>&      InOutRGBA8)
{
    return ExecuteGPUOpticsWithPosture(
        WidthPx,
        HeightPx,
        Config,
        nullptr,
        OptionalLinearDepthMeters,
        InOutRGBA8);
}

bool CameraOpticsEncoder::ExecuteGPUOpticsWithPosture(
    uint32_t                   WidthPx,
    uint32_t                   HeightPx,
    const CameraConfiguration& Config,
    const CameraPosture*       OptionalPosture,
    std::span<const float>     OptionalLinearDepthMeters,
    std::vector<uint8_t>&      InOutRGBA8)
{
    const uint64_t PixelCount = static_cast<uint64_t>(WidthPx) * static_cast<uint64_t>(HeightPx);
    if (DeviceInterchange == nullptr || OpticsProgram == UINT32_MAX ||
        PixelCount == 0u || InOutRGBA8.size() < PixelCount * 4u)
    {
        return false;
    }

    const uint32_t EffectMask = EvaluateActiveEffectMask(Config);
    uint32_t       GuideStyle = 0u;
#if !FRONTIER_PROFILE_SHIPPING
    GuideStyle = static_cast<uint32_t>(Config.Diagnostics.CompositionGuide);
#endif
    if (EffectMask == 0u && GuideStyle == 0u)
    {
        return true;
    }

    const uint64_t PixelBytes = PixelCount * sizeof(uint32_t);
    const uint64_t DepthBytes = PixelCount * sizeof(float);

    if (SourceBuffer.ByteCapacity < PixelBytes)
    {
        DeviceInterchange->ReleaseBuffer(SourceBuffer);
        SourceBuffer = ComputeEncoder.AllocateHostWritableBuffer(PixelBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }
    if (DepthBuffer.ByteCapacity < DepthBytes)
    {
        DeviceInterchange->ReleaseBuffer(DepthBuffer);
        DepthBuffer = ComputeEncoder.AllocateHostWritableBuffer(DepthBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }
    if (OutputBuffer.ByteCapacity < PixelBytes)
    {
        DeviceInterchange->ReleaseBuffer(OutputBuffer);
        OutputBuffer = ComputeEncoder.AllocateHostWritableBuffer(
            PixelBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    }

    std::memcpy(SourceBuffer.HostAddress, InOutRGBA8.data(), PixelBytes);

    if (OptionalLinearDepthMeters.size() >= PixelCount)
    {
        std::memcpy(DepthBuffer.HostAddress, OptionalLinearDepthMeters.data(), DepthBytes);
    }
    else
    {
        std::vector<float> DefaultDepth(static_cast<std::size_t>(PixelCount), Config.Optics.FocusDistanceMeters);
        std::memcpy(DepthBuffer.HostAddress, DefaultDepth.data(), DepthBytes);
    }

    GPUCameraOpticsUniforms Uni {};
    Uni.ResolutionMaskPacked[0] = WidthPx;
    Uni.ResolutionMaskPacked[1] = HeightPx;
    Uni.ResolutionMaskPacked[2] = EffectMask;
    Uni.ResolutionMaskPacked[3] = PackGhostStarburstWord(Config.LensFlare);

    Uni.LensFocusPacked[0] = Config.Optics.FocalLengthMillimeters;
    Uni.LensFocusPacked[1] = Config.Optics.ApertureFStop;
    Uni.LensFocusPacked[2] = Config.Optics.FocusDistanceMeters;
    Uni.LensFocusPacked[3] = Config.Optics.SensorHeightMillimeters;

    Uni.FlarePositionGain[0] = Config.LensFlare.SourceNormalizedU;
    Uni.FlarePositionGain[1] = Config.LensFlare.SourceNormalizedV;
    Uni.FlarePositionGain[2] = Config.LensFlare.FlareIntensity;
    Uni.FlarePositionGain[3] = Config.LensFlare.FlareSpread;

    Uni.GhostAberrationPacked[0] = Config.LensFlare.GhostIntensity;
    Uni.GhostAberrationPacked[1] = Config.LensFlare.GhostSpacing;
    Uni.GhostAberrationPacked[2] = Config.LensFlare.StarburstRotationRad;
    Uni.GhostAberrationPacked[3] = ResolveChromaticDispersionStrength(Config.ChromaticAberration);

    Uni.VignetteBlurPacked[0] = ResolveVignetteIntensity(Config.Vignette);
    Uni.VignetteBlurPacked[1] = std::clamp(Config.Vignette.Roundness, 0.0f, 1.0f);
    Uni.VignetteBlurPacked[2] = std::max(1.0f, Config.DepthOfField.MaxBokehRadiusPx);
    Uni.VignetteBlurPacked[3] = static_cast<float>(GuideStyle);

    Uni.HaloTintPacked[0] = std::clamp(Config.LensFlare.HaloIntensity, 0.0f, 2.5f);
    Uni.HaloTintPacked[1] = 1.00f;
    Uni.HaloTintPacked[2] = 0.96f;
    Uni.HaloTintPacked[3] = 0.90f;

    const float Aspect = static_cast<float>(WidthPx) / static_cast<float>(std::max(1u, HeightPx));
    const auto  MotionVelUv = ResolveMotionBlurVelocityUv(Config, OptionalPosture, Aspect);
    Uni.MotionBlurPacked0[0] = static_cast<float>(static_cast<uint32_t>(Config.MotionBlur.Style));
    Uni.MotionBlurPacked0[1] = ResolveMotionBlurIntensity(Config.MotionBlur);
    Uni.MotionBlurPacked0[2] = static_cast<float>(std::clamp(Config.MotionBlur.SampleCount, 4u, 24u));
    Uni.MotionBlurPacked0[3] = std::clamp(Config.MotionBlur.MaxBlurRadiusPx, 2.0f, 64.0f);

    Uni.MotionBlurPacked1[0] = MotionVelUv.U;
    Uni.MotionBlurPacked1[1] = MotionVelUv.V;
    Uni.MotionBlurPacked1[2] = ResolveMotionBlurShutterNorm(Config.MotionBlur);
    Uni.MotionBlurPacked1[3] = Config.MotionBlur.DirectionalAngleRad;

    Uni.MotionBlurPacked2[0] = std::clamp(Config.MotionBlur.ZoomCenterU, 0.0f, 1.0f);
    Uni.MotionBlurPacked2[1] = std::clamp(Config.MotionBlur.ZoomCenterV, 0.0f, 1.0f);
    Uni.MotionBlurPacked2[2] = std::clamp(Config.MotionBlur.RadialSpinAmount, 0.0f, 1.5f);
    Uni.MotionBlurPacked2[3] = Aspect;

    if (OptionalPosture != nullptr)
    {
        const float TanY = std::max(1.0e-3f, std::tan(0.5f * OptionalPosture->VerticalFovDegrees * Constants::DegToRadFloat));
        const float TanX = TanY * std::max(0.25f, Aspect);
        for (int R = 0; R < 4; ++R)
        {
            for (int C = 0; C < 4; ++C)
            {
                Uni.PrevViewClipRows[R][C] = OptionalPosture->PreviousViewClip.Rows[R][C];
            }
        }
        Uni.CameraOriginTanX[0]    = OptionalPosture->CurrentEyePosition.X;
        Uni.CameraOriginTanX[1]    = OptionalPosture->CurrentEyePosition.Y;
        Uni.CameraOriginTanX[2]    = OptionalPosture->CurrentEyePosition.Z;
        Uni.CameraOriginTanX[3]    = TanX;
        Uni.CameraForwardTanY[0]   = OptionalPosture->ForwardAxis.X;
        Uni.CameraForwardTanY[1]   = OptionalPosture->ForwardAxis.Y;
        Uni.CameraForwardTanY[2]   = OptionalPosture->ForwardAxis.Z;
        Uni.CameraForwardTanY[3]   = TanY;
        Uni.CameraRightClock[0]    = OptionalPosture->RightAxis.X;
        Uni.CameraRightClock[1]    = OptionalPosture->RightAxis.Y;
        Uni.CameraRightClock[2]    = OptionalPosture->RightAxis.Z;
        Uni.CameraRightClock[3]    = Config.EffectClockSeconds;
        Uni.CameraUpReprojValid[0] = OptionalPosture->UpAxis.X;
        Uni.CameraUpReprojValid[1] = OptionalPosture->UpAxis.Y;
        Uni.CameraUpReprojValid[2] = OptionalPosture->UpAxis.Z;
        Uni.CameraUpReprojValid[3] = (OptionalPosture->HasPreviousFrame &&
                                      Config.ProjectionMode == CameraProjectionMode::Perspective &&
                                      std::abs(Config.MotionBlur.VelocityUvOverrideX) <= 1.0e-5f &&
                                      std::abs(Config.MotionBlur.VelocityUvOverrideY) <= 1.0e-5f)
            ? 1.0f
            : 0.0f;
    }
    else
    {
        Uni.CameraRightClock[3]    = Config.EffectClockSeconds;
        Uni.CameraUpReprojValid[3] = 0.0f;
    }

    Uni.HeatShimmerPacked[0] = std::clamp(Config.HeatShimmer.Intensity, 0.0f, 2.0f);
    Uni.HeatShimmerPacked[1] = std::clamp(Config.HeatShimmer.Frequency, 4.0f, 48.0f);
    Uni.HeatShimmerPacked[2] = std::clamp(Config.HeatShimmer.RiseSpeed, 0.0f, 12.0f);
    Uni.HeatShimmerPacked[3] = std::clamp(Config.HeatShimmer.ChromaticDispersion, 0.0f, 1.0f);

    Uni.BlackHolePacked0[0] = std::clamp(Config.BlackHoleLens.LensingStrength, 0.0f, 3.0f);
    Uni.BlackHolePacked0[1] = std::clamp(Config.BlackHoleLens.EventHorizonScale, 0.08f, 0.75f);
    Uni.BlackHolePacked0[2] = std::clamp(Config.BlackHoleLens.AccretionGlowIntensity, 0.0f, 3.0f);
    Uni.BlackHolePacked0[3] = std::clamp(Config.BlackHoleLens.ChromaticSplit, 0.0f, 1.0f);
    Uni.BlackHolePacked1[0] = std::clamp(Config.BlackHoleLens.SwirlTwist, 0.0f, 2.0f);
    Uni.BlackHolePacked1[1] = Config.BlackHoleLens.AccretionTint.X;
    Uni.BlackHolePacked1[2] = Config.BlackHoleLens.AccretionTint.Y;
    Uni.BlackHolePacked1[3] = Config.BlackHoleLens.AccretionTint.Z;

    Uni.GlitchPacked[0] = std::clamp(Config.Glitch.Intensity, 0.0f, 2.0f);
    Uni.GlitchPacked[1] = std::clamp(Config.Glitch.BlockJitter, 0.0f, 1.5f);
    Uni.GlitchPacked[2] = std::clamp(Config.Glitch.ScanlineTear, 0.0f, 1.5f);
    Uni.GlitchPacked[3] = std::clamp(Config.Glitch.RgbSplit, 0.0f, 1.5f);

    uint32_t ActiveAnchors = 0u;
    if (OptionalPosture != nullptr)
    {
        for (const auto& RawAnchor : Config.WorldDistortionAnchors)
        {
            if (ActiveAnchors >= 8u)
            {
                break;
            }
            const auto Proj = ProjectWorldDistortionAnchor(Config, *OptionalPosture, RawAnchor, Aspect);
            if (!Proj.Visible)
            {
                continue;
            }
            auto& Dst = Uni.Anchors[ActiveAnchors++];
            Dst.PosRadKind[0]         = Proj.ScreenU;
            Dst.PosRadKind[1]         = Proj.ScreenV;
            Dst.PosRadKind[2]         = Proj.ScreenRadiusUv;
            Dst.PosRadKind[3]         = static_cast<float>(static_cast<uint32_t>(Proj.Kind));
            Dst.DepthStrengthParam[0] = Proj.LinearDepthMeters;
            Dst.DepthStrengthParam[1] = Proj.Strength;
            Dst.DepthStrengthParam[2] = Proj.SecondaryParam;
            Dst.DepthStrengthParam[3] = 1.0f;
        }
    }

    Uni.DistortionMetaPacked[0] = static_cast<float>(ActiveAnchors);
    Uni.DistortionMetaPacked[1] = Config.HeatShimmer.UseWorldAnchors ? 1.0f : 0.0f;
    Uni.DistortionMetaPacked[2] = Config.Glitch.LocalizeToWorldAnchors ? 1.0f : 0.0f;
    Uni.DistortionMetaPacked[3] = std::clamp(Config.Glitch.RateHz, 1.0f, 60.0f);

    const std::array<GPUCameraOpticsUniforms, 1u> UniSpan {{ Uni }};
    ComputeEncoder.UploadSpanToBuffer<GPUCameraOpticsUniforms>(UniformBuffer, UniSpan);

    ComputeEncoder.ResetTransientDescriptors();
    VkCommandBuffer Cmd = DeviceInterchange->BeginSingleTimeCommands();
    const std::array<GeometricRaster::ComputeSlotDescriptor, 5u> Slots {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, UniformBuffer.BufferHandle,        sizeof(GPUCameraOpticsUniforms) },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SourceBuffer.BufferHandle,         PixelBytes },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DepthBuffer.BufferHandle,          DepthBytes },
        { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, OutputBuffer.BufferHandle,         PixelBytes },
        { 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BakedFlareAtlasBuffer.BufferHandle,VK_WHOLE_SIZE },
    }};
    const uint32_t GroupsX = (WidthPx + 7u) / 8u;
    const uint32_t GroupsY = (HeightPx + 7u) / 8u;
    if (!ComputeEncoder.EncodeComputeDispatch(Cmd, OpticsProgram, Slots, GroupsX, GroupsY, 1u))
    {
        DeviceInterchange->FreeCommandBuffer(Cmd);
        return false;
    }

    GeometricRaster::ReadbackTicket Ticket = TransferRegister.EncodeBufferReadback(Cmd, OutputBuffer.BufferHandle, 0ull, PixelBytes);
    const uint64_t                  Signal = DeviceInterchange->DispatchCommandBufferAsync(Cmd);
    TransferRegister.BindTicketTimelineSignal(Ticket, Signal);

    std::span<uint8_t> OutBytes(InOutRGBA8.data(), static_cast<std::size_t>(PixelBytes));
    const bool Ok = TransferRegister.AwaitReadbackTyped<uint8_t>(Ticket, OutBytes);
    DeviceInterchange->FreeCommandBuffer(Cmd);
    return Ok;
}

} // namespace Frontier::Components::VisualEffects
