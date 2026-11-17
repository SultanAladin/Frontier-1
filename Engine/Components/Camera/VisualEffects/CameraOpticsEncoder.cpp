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
#include "Engine/Components/Camera/VisualEffects/VignetteEffect.h"
#include "Engine/GeometricRaster/Atmosphere/LensFlareSequence.h"

#include <algorithm>
#include <array>
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
