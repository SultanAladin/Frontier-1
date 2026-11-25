/*============================================================================================================================================
                                                         INSPECTORGPUBAKER.CPP
============================================================================================================================================*/
// 🧩 Implementation of pure-GPU Vulkan compute Inspector & Thumbnail baker (zero-copy GPU direct ImGui images + persistent buffers)

#include "Engine/Editor/Inspectors/InspectorGpuBaker.h"
#include "Engine/GeometricRaster/Atmosphere/LunarStarfieldSequence.h"
#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"
#include "Engine/GeometricRaster/Rasterization/ImGuiOverlayRasterization.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace Frontier
{

namespace
{

struct alignas(16) InspectorPreviewUniformStd140
{
    uint32_t DimensionsAndMode[4];
    float    ParamA[4];
    float    ParamB[4];
    float    ColourA[4];
    float    ColourB[4];
};
static_assert(sizeof(InspectorPreviewUniformStd140) == 80u, "InspectorPreviewUniformStd140 must be 80 bytes.");

struct alignas(16) ThumbnailSpecimenUniformStd140
{
    uint32_t DimCategoryMode[4];
    float    BaseTintAlpha[4];
    float    SurfaceParams[4];
};
static_assert(sizeof(ThumbnailSpecimenUniformStd140) == 48u, "ThumbnailSpecimenUniformStd140 must be 48 bytes.");

struct DirectGpuSurfaceEntry
{
    GeometricRaster::DeviceImageResource Image {};
    uint32_t                             WidthPx    = 0u;
    uint32_t                             HeightPx   = 0u;
    uint64_t                             Generation = 0ull;
    bool                                 InitializedLayout = false;
};

struct InspectorGpuBakerState
{
    std::mutex                                          Mutex {};
    GeometricRaster::VulkanInterchange*                 Device    = nullptr;
    GeometricRaster::ImGuiOverlayRasterization*         Presenter = nullptr;
    GeometricRaster::ComputeDispatchEncoder             Encoder {};
    uint32_t                                            ProgramHandle          = UINT32_MAX;
    uint32_t                                            ThumbnailProgramHandle = UINT32_MAX;
    GeometricRaster::DeviceBufferResource               UniformBuffer {};
    GeometricRaster::DeviceBufferResource               ThumbnailUniformBuffer {};
    GeometricRaster::DeviceBufferResource               OutputBuffer {};
    uint64_t                                            OutputCapacityBytes = 0ull;
    GeometricRaster::DeviceBufferResource               DeviceDirectScratchBuffer {};
    uint64_t                                            DeviceDirectScratchCapacity = 0ull;
    GeometricRaster::DeviceBufferResource               LunarAtlasBuffer {};
    uint64_t                                            LunarAtlasBytes = 0ull;
    uint64_t                                            LastDirectTimelineSignal = 0ull;
    VkCommandBuffer                                     LastDirectCmdBuffer      = VK_NULL_HANDLE;
    std::unordered_map<uint64_t, DirectGpuSurfaceEntry> DirectSurfaces {};
    std::atomic<uint64_t>                               GpuBakeCount { 0ull };
    std::atomic<uint64_t>                               DirectGpuBakeCount { 0ull };
    std::atomic<uint64_t>                               ThumbnailGpuBakeCount { 0ull };
    std::atomic<uint64_t>                               CpuFallbackCount { 0ull };
    std::atomic<uint32_t>                               LastGpuBakeMicros { 0u };
};

InspectorGpuBakerState& AccessState() noexcept
{
    static InspectorGpuBakerState State {};
    return State;
}

void WaitPendingDirectBakeLocked(InspectorGpuBakerState& S) noexcept
{
    if (S.LastDirectCmdBuffer != VK_NULL_HANDLE && S.Device != nullptr && S.Device->Ready())
    {
        if (S.LastDirectTimelineSignal != 0ull)
        {
            (void)S.Device->AwaitTimelineTick(S.LastDirectTimelineSignal, 60000000000ull);
        }
        S.Device->FreeCommandBuffer(S.LastDirectCmdBuffer);
    }
    S.LastDirectCmdBuffer      = VK_NULL_HANDLE;
    S.LastDirectTimelineSignal = 0ull;
}

void ReleaseStateResourcesLocked(InspectorGpuBakerState& S) noexcept
{
    WaitPendingDirectBakeLocked(S);
    if (S.Device != nullptr && S.Device->Ready())
    {
        for (auto& [Id, Entry] : S.DirectSurfaces)
        {
            (void)Id;
            S.Device->ReleaseImage2D(Entry.Image);
        }
        S.Device->ReleaseBuffer(S.UniformBuffer);
        S.Device->ReleaseBuffer(S.ThumbnailUniformBuffer);
        S.Device->ReleaseBuffer(S.OutputBuffer);
        S.Device->ReleaseBuffer(S.DeviceDirectScratchBuffer);
        S.Device->ReleaseBuffer(S.LunarAtlasBuffer);
    }
    S.DirectSurfaces.clear();
    S.UniformBuffer               = {};
    S.ThumbnailUniformBuffer      = {};
    S.OutputBuffer                = {};
    S.OutputCapacityBytes         = 0ull;
    S.DeviceDirectScratchBuffer   = {};
    S.DeviceDirectScratchCapacity = 0ull;
    S.LunarAtlasBuffer            = {};
    S.LunarAtlasBytes             = 0ull;
}

bool EnsureLunarAtlasBufferLocked(InspectorGpuBakerState& S)
{
    if (S.LunarAtlasBuffer.BufferHandle != VK_NULL_HANDLE && S.LunarAtlasBytes > 0ull)
    {
        return true;
    }
    if (S.Device == nullptr || !S.Device->Ready())
    {
        return false;
    }
    const auto& AtlasRgba = GeometricRaster::LunarStarfieldSequence::QueryLunarSurfaceAtlasRgba8();
    const uint64_t Bytes  = static_cast<uint64_t>(AtlasRgba.size());
    if (Bytes < 4ull)
    {
        return false;
    }
    S.LunarAtlasBuffer = S.Encoder.AllocateHostWritableBuffer(Bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (S.LunarAtlasBuffer.BufferHandle == VK_NULL_HANDLE || S.LunarAtlasBuffer.HostAddress == nullptr)
    {
        S.Device->ReleaseBuffer(S.LunarAtlasBuffer);
        S.LunarAtlasBuffer = {};
        S.LunarAtlasBytes  = 0ull;
        return false;
    }
    std::memcpy(S.LunarAtlasBuffer.HostAddress, AtlasRgba.data(), static_cast<std::size_t>(Bytes));
    S.LunarAtlasBytes = Bytes;
    return true;
}

void SyncDirectHookLocked(InspectorGpuBakerState& S)
{
    if (S.Device != nullptr && S.Device->Ready() && S.ProgramHandle != UINT32_MAX && S.Presenter != nullptr)
    {
        InspectorPreviewSurface::AssignGpuDirectBakeHook(
            [](uint64_t SurfaceId, const InspectorGpuBakeSpec& Spec, uint32_t W, uint32_t H, uint64_t& OutHandle) -> bool
            {
                return InspectorGpuBaker::DispatchBakeDirectToImage(SurfaceId, Spec, W, H, OutHandle);
            },
            [](uint64_t SurfaceId)
            {
                InspectorGpuBaker::ReleaseDirectSurface(SurfaceId);
            });
    }
    else
    {
        InspectorPreviewSurface::ClearGpuDirectBakeHook();
    }
}

} // namespace

bool InspectorGpuBaker::AttachDevice(
    GeometricRaster::VulkanInterchange* Device,
    const std::filesystem::path&        ShaderDirectory)
{
    DetachDevice();
    if (Device == nullptr || !Device->Ready())
    {
        return false;
    }

    auto& S = AccessState();
    std::lock_guard<std::mutex> Lock(S.Mutex);

    const std::filesystem::path SpvPath = ShaderDirectory / "InspectorPreviewCompute.comp.spv";
    if (!std::filesystem::exists(SpvPath))
    {
        return false;
    }

    if (!S.Encoder.InitializeEncoder(Device))
    {
        return false;
    }

    const std::array<VkDescriptorType, 3u> PreviewSlotTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    S.ProgramHandle = S.Encoder.RegisterProgram(SpvPath, PreviewSlotTypes, "ComputeEntry");
    if (S.ProgramHandle == UINT32_MAX)
    {
        S.Encoder.TeardownEncoder();
        return false;
    }

    const std::array<VkDescriptorType, 2u> ThumbSlotTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    const std::filesystem::path ThumbSpvPath = ShaderDirectory / "ThumbnailSpecimenCompute.comp.spv";
    if (std::filesystem::exists(ThumbSpvPath))
    {
        S.ThumbnailProgramHandle = S.Encoder.RegisterProgram(ThumbSpvPath, ThumbSlotTypes, "ComputeEntry");
    }

    S.UniformBuffer = S.Encoder.AllocateHostWritableBuffer(sizeof(InspectorPreviewUniformStd140), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    S.ThumbnailUniformBuffer = S.Encoder.AllocateHostWritableBuffer(sizeof(ThumbnailSpecimenUniformStd140), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    if (S.UniformBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        ReleaseStateResourcesLocked(S);
        S.Encoder.TeardownEncoder();
        S.ProgramHandle          = UINT32_MAX;
        S.ThumbnailProgramHandle = UINT32_MAX;
        return false;
    }

    S.Device = Device;

    InspectorPreviewSurface::AssignGpuBakeHook(
        [](const InspectorGpuBakeSpec& Spec, uint32_t WidthPx, uint32_t HeightPx, std::vector<uint8_t>& OutRgba8) -> bool
        {
            return InspectorGpuBaker::DispatchBake(Spec, WidthPx, HeightPx, OutRgba8);
        });
    SyncDirectHookLocked(S);

    if (S.ThumbnailProgramHandle != UINT32_MAX && S.ThumbnailUniformBuffer.BufferHandle != VK_NULL_HANDLE)
    {
        ThumbnailSheetRepository::AssignGpuThumbnailBaker(
            [](const ThumbnailSheetRepository::ThumbnailGpuBakeDesc& Desc, uint32_t DimPx, std::vector<uint8_t>& OutRgba8) -> bool
            {
                return InspectorGpuBaker::DispatchThumbnailBake(Desc, DimPx, OutRgba8);
            });
    }
    return true;
}

void InspectorGpuBaker::DetachDevice() noexcept
{
    InspectorPreviewSurface::ClearGpuDirectBakeHook();
    InspectorPreviewSurface::ClearGpuBakeHook();
    ThumbnailSheetRepository::ClearGpuThumbnailBaker();
    auto& S = AccessState();
    std::lock_guard<std::mutex> Lock(S.Mutex);
    ReleaseStateResourcesLocked(S);
    if (S.ProgramHandle != UINT32_MAX || S.ThumbnailProgramHandle != UINT32_MAX)
    {
        S.Encoder.TeardownEncoder();
        S.ProgramHandle          = UINT32_MAX;
        S.ThumbnailProgramHandle = UINT32_MAX;
    }
    S.Device = nullptr;
}

void InspectorGpuBaker::AttachImGuiPresenter(GeometricRaster::ImGuiOverlayRasterization* Presenter) noexcept
{
    auto& S = AccessState();
    std::lock_guard<std::mutex> Lock(S.Mutex);
    S.Presenter = Presenter;
    SyncDirectHookLocked(S);
}

void InspectorGpuBaker::DetachImGuiPresenter() noexcept
{
    InspectorPreviewSurface::ClearGpuDirectBakeHook();
    auto& S = AccessState();
    std::lock_guard<std::mutex> Lock(S.Mutex);
    WaitPendingDirectBakeLocked(S);
    if (S.Device != nullptr && S.Device->Ready())
    {
        for (auto& [Id, Entry] : S.DirectSurfaces)
        {
            (void)Id;
            S.Device->ReleaseImage2D(Entry.Image);
        }
    }
    S.DirectSurfaces.clear();
    S.Presenter = nullptr;
}

bool InspectorGpuBaker::QueryReady() noexcept
{
    auto& S = AccessState();
    std::lock_guard<std::mutex> Lock(S.Mutex);
    return S.Device != nullptr && S.Device->Ready() && S.ProgramHandle != UINT32_MAX;
}

bool InspectorGpuBaker::QueryDirectImageReady() noexcept
{
    auto& S = AccessState();
    std::lock_guard<std::mutex> Lock(S.Mutex);
    return S.Device != nullptr && S.Device->Ready() && S.ProgramHandle != UINT32_MAX && S.Presenter != nullptr;
}

bool InspectorGpuBaker::QueryThumbnailBakerReady() noexcept
{
    auto& S = AccessState();
    std::lock_guard<std::mutex> Lock(S.Mutex);
    return S.Device != nullptr && S.Device->Ready() && S.ThumbnailProgramHandle != UINT32_MAX;
}

void InspectorGpuBaker::ReleaseDirectSurface(uint64_t SurfaceId) noexcept
{
    auto& S = AccessState();
    std::lock_guard<std::mutex> Lock(S.Mutex);
    auto It = S.DirectSurfaces.find(SurfaceId);
    if (It == S.DirectSurfaces.end())
    {
        return;
    }
    WaitPendingDirectBakeLocked(S);
    if (S.Device != nullptr && S.Device->Ready())
    {
        S.Device->ReleaseImage2D(It->second.Image);
    }
    S.DirectSurfaces.erase(It);
}

bool InspectorGpuBaker::DispatchBakeDirectToImage(
    uint64_t                    SurfaceId,
    const InspectorGpuBakeSpec& Spec,
    uint32_t                    WidthPx,
    uint32_t                    HeightPx,
    uint64_t&                   OutNativeTexHandle)
{
    OutNativeTexHandle = 0ull;
    if (SurfaceId == 0ull || WidthPx == 0u || HeightPx == 0u)
    {
        return false;
    }

    auto& S = AccessState();
    std::lock_guard<std::mutex> Lock(S.Mutex);
    if (S.Device == nullptr || !S.Device->Ready() || S.ProgramHandle == UINT32_MAX || S.Presenter == nullptr ||
        S.UniformBuffer.HostAddress == nullptr)
    {
        return false;
    }

    const auto T0 = std::chrono::steady_clock::now();
    // Ensure previous direct bake finished with the shared scratch buffer (no global vkDeviceWaitIdle!)
    WaitPendingDirectBakeLocked(S);

    const uint64_t OutBytes = static_cast<uint64_t>(WidthPx) * HeightPx * 4ull;
    if (S.DeviceDirectScratchBuffer.BufferHandle == VK_NULL_HANDLE || S.DeviceDirectScratchCapacity < OutBytes)
    {
        S.Device->ReleaseBuffer(S.DeviceDirectScratchBuffer);
        const uint64_t AllocBytes = std::max<uint64_t>(OutBytes, 512ull * 512ull * 4ull);
        S.DeviceDirectScratchBuffer = S.Encoder.AllocateDeviceLocalBuffer(
            AllocBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        S.DeviceDirectScratchCapacity = (S.DeviceDirectScratchBuffer.BufferHandle != VK_NULL_HANDLE) ? AllocBytes : 0ull;
    }
    if (S.DeviceDirectScratchBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return false;
    }

    DirectGpuSurfaceEntry& Entry = S.DirectSurfaces[SurfaceId];
    if (Entry.Image.ImageHandle == VK_NULL_HANDLE || Entry.WidthPx != WidthPx || Entry.HeightPx != HeightPx)
    {
        S.Device->ReleaseImage2D(Entry.Image);
        Entry.Image = S.Device->AllocateImage2D(
            WidthPx,
            HeightPx,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        if (Entry.Image.ImageHandle == VK_NULL_HANDLE || Entry.Image.ViewHandle == VK_NULL_HANDLE)
        {
            return false;
        }
        Entry.WidthPx           = WidthPx;
        Entry.HeightPx          = HeightPx;
        Entry.InitializedLayout = false;
        ++Entry.Generation;
    }

    InspectorPreviewUniformStd140 Uni {};
    Uni.DimensionsAndMode[0] = WidthPx;
    Uni.DimensionsAndMode[1] = HeightPx;
    Uni.DimensionsAndMode[2] = static_cast<uint32_t>(Spec.Mode);
    Uni.DimensionsAndMode[3] = Spec.Flags;
    for (uint32_t I = 0u; I < 4u; ++I)
    {
        Uni.ParamA[I]  = Spec.ParamA[I];
        Uni.ParamB[I]  = Spec.ParamB[I];
        Uni.ColourA[I] = Spec.ColourA[I];
        Uni.ColourB[I] = Spec.ColourB[I];
    }
    std::memcpy(S.UniformBuffer.HostAddress, &Uni, sizeof(Uni));

    if (!EnsureLunarAtlasBufferLocked(S))
    {
        return false;
    }

    S.Encoder.ResetTransientDescriptors();
    VkCommandBuffer Cmd = S.Device->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return false;
    }

    const std::array<GeometricRaster::ComputeSlotDescriptor, 3u> Slots {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, S.UniformBuffer.BufferHandle,             sizeof(InspectorPreviewUniformStd140) },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, S.DeviceDirectScratchBuffer.BufferHandle, OutBytes },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, S.LunarAtlasBuffer.BufferHandle,          S.LunarAtlasBytes },
    }};
    const uint32_t GX = (WidthPx + 7u) / 8u;
    const uint32_t GY = (HeightPx + 7u) / 8u;
    if (!S.Encoder.EncodeComputeDispatch(Cmd, S.ProgramHandle, Slots, GX, GY, 1u))
    {
        S.Device->FreeCommandBuffer(Cmd);
        return false;
    }

    // Pure GPU-to-GPU barrier + copy into sampled VkImage (ZERO CPU readback, ZERO memcpy!)
    VkBufferMemoryBarrier BufBarrier {};
    BufBarrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    BufBarrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    BufBarrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    BufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    BufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    BufBarrier.buffer              = S.DeviceDirectScratchBuffer.BufferHandle;
    BufBarrier.offset              = 0ull;
    BufBarrier.size                = OutBytes;

    VkImageMemoryBarrier ImgToDst {};
    ImgToDst.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ImgToDst.oldLayout                       = Entry.InitializedLayout ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    ImgToDst.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ImgToDst.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    ImgToDst.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    ImgToDst.image                           = Entry.Image.ImageHandle;
    ImgToDst.subresourceRange                = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
    ImgToDst.srcAccessMask                   = Entry.InitializedLayout ? static_cast<VkAccessFlags>(VK_ACCESS_SHADER_READ_BIT) : static_cast<VkAccessFlags>(0u);
    ImgToDst.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        Cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | (Entry.InitializedLayout ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0u,
        0u, nullptr,
        1u, &BufBarrier,
        1u, &ImgToDst);

    VkBufferImageCopy CopyRegion {};
    CopyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
    CopyRegion.imageExtent      = { WidthPx, HeightPx, 1u };
    vkCmdCopyBufferToImage(
        Cmd,
        S.DeviceDirectScratchBuffer.BufferHandle,
        Entry.Image.ImageHandle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1u,
        &CopyRegion);

    VkImageMemoryBarrier ImgToRead {};
    ImgToRead.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ImgToRead.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ImgToRead.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ImgToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ImgToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ImgToRead.image               = Entry.Image.ImageHandle;
    ImgToRead.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
    ImgToRead.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    ImgToRead.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        Cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0u,
        0u, nullptr,
        0u, nullptr,
        1u, &ImgToRead);

    // Async GPU submission on the same queue before ImGui's renderpass — ZERO host wait stall!
    S.LastDirectTimelineSignal = S.Device->DispatchCommandBufferAsync(Cmd);
    S.LastDirectCmdBuffer      = Cmd;
    Entry.InitializedLayout    = true;

    const uint64_t ExternalKey = 0x494E535000000000ull | (SurfaceId & 0xFFFFFFFFull);
    OutNativeTexHandle = S.Presenter->RegisterOrUpdateExternalImage(ExternalKey, Entry.Generation, Entry.Image.ViewHandle);
    if (OutNativeTexHandle == 0ull)
    {
        return false;
    }

    const auto ElapsedUs = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - T0).count());
    S.LastGpuBakeMicros.store(ElapsedUs, std::memory_order_relaxed);
    S.GpuBakeCount.fetch_add(1ull, std::memory_order_relaxed);
    S.DirectGpuBakeCount.fetch_add(1ull, std::memory_order_relaxed);
    return true;
}

bool InspectorGpuBaker::DispatchBake(
    const InspectorGpuBakeSpec& Spec,
    uint32_t                    WidthPx,
    uint32_t                    HeightPx,
    std::vector<uint8_t>&       OutRgba8)
{
    if (WidthPx == 0u || HeightPx == 0u)
    {
        return false;
    }

    auto& S = AccessState();
    std::lock_guard<std::mutex> Lock(S.Mutex);
    if (S.Device == nullptr || !S.Device->Ready() || S.ProgramHandle == UINT32_MAX)
    {
        S.CpuFallbackCount.fetch_add(1ull, std::memory_order_relaxed);
        return false;
    }

    const auto T0 = std::chrono::steady_clock::now();
    const uint64_t OutBytes = static_cast<uint64_t>(WidthPx) * HeightPx * 4ull;
    if (S.OutputBuffer.BufferHandle == VK_NULL_HANDLE || S.OutputCapacityBytes < OutBytes)
    {
        S.Device->ReleaseBuffer(S.OutputBuffer);
        const uint64_t AllocBytes = std::max<uint64_t>(OutBytes, 512ull * 512ull * 4ull);
        S.OutputBuffer = S.Encoder.AllocateReadbackStagingBuffer(AllocBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        S.OutputCapacityBytes = (S.OutputBuffer.BufferHandle != VK_NULL_HANDLE) ? AllocBytes : 0ull;
    }
    if (S.OutputBuffer.BufferHandle == VK_NULL_HANDLE || S.UniformBuffer.HostAddress == nullptr)
    {
        S.CpuFallbackCount.fetch_add(1ull, std::memory_order_relaxed);
        return false;
    }

    InspectorPreviewUniformStd140 Uni {};
    Uni.DimensionsAndMode[0] = WidthPx;
    Uni.DimensionsAndMode[1] = HeightPx;
    Uni.DimensionsAndMode[2] = static_cast<uint32_t>(Spec.Mode);
    Uni.DimensionsAndMode[3] = Spec.Flags;
    for (uint32_t I = 0u; I < 4u; ++I)
    {
        Uni.ParamA[I]  = Spec.ParamA[I];
        Uni.ParamB[I]  = Spec.ParamB[I];
        Uni.ColourA[I] = Spec.ColourA[I];
        Uni.ColourB[I] = Spec.ColourB[I];
    }
    std::memcpy(S.UniformBuffer.HostAddress, &Uni, sizeof(Uni));

    if (!EnsureLunarAtlasBufferLocked(S))
    {
        S.CpuFallbackCount.fetch_add(1ull, std::memory_order_relaxed);
        return false;
    }

    bool Ok = false;
    S.Encoder.ResetTransientDescriptors();
    VkCommandBuffer Cmd = S.Device->BeginSingleTimeCommands();
    if (Cmd != VK_NULL_HANDLE)
    {
        const std::array<GeometricRaster::ComputeSlotDescriptor, 3u> Slots {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, S.UniformBuffer.BufferHandle, sizeof(InspectorPreviewUniformStd140) },
            { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, S.OutputBuffer.BufferHandle,  OutBytes },
            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, S.LunarAtlasBuffer.BufferHandle, S.LunarAtlasBytes },
        }};
        const uint32_t GX = (WidthPx + 7u) / 8u;
        const uint32_t GY = (HeightPx + 7u) / 8u;
        if (S.Encoder.EncodeComputeDispatch(Cmd, S.ProgramHandle, Slots, GX, GY, 1u))
        {
            VkMemoryBarrier Barrier {};
            Barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            Barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0u, 1u, &Barrier, 0u, nullptr, 0u, nullptr);
            const uint64_t SyncPoint = S.Device->DispatchCommandBufferAsync(Cmd);
            Ok = S.Device->AwaitTimelineTick(SyncPoint, 60000000000ull);
        }
        S.Device->FreeCommandBuffer(Cmd);
    }

    if (Ok && S.OutputBuffer.HostAddress != nullptr)
    {
        OutRgba8.resize(static_cast<std::size_t>(OutBytes));
        std::memcpy(OutRgba8.data(), S.OutputBuffer.HostAddress, static_cast<std::size_t>(OutBytes));
        const auto ElapsedUs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - T0).count());
        S.LastGpuBakeMicros.store(ElapsedUs, std::memory_order_relaxed);
        S.GpuBakeCount.fetch_add(1ull, std::memory_order_relaxed);
        return true;
    }

    S.CpuFallbackCount.fetch_add(1ull, std::memory_order_relaxed);
    return false;
}

bool InspectorGpuBaker::DispatchThumbnailBake(
    const ThumbnailSheetRepository::ThumbnailGpuBakeDesc& Desc,
    uint32_t                                              DimPx,
    std::vector<uint8_t>&                                 OutRgba8)
{
    if (DimPx == 0u)
    {
        return false;
    }

    auto& S = AccessState();
    std::lock_guard<std::mutex> Lock(S.Mutex);
    if (S.Device == nullptr || !S.Device->Ready() || S.ThumbnailProgramHandle == UINT32_MAX ||
        S.ThumbnailUniformBuffer.HostAddress == nullptr)
    {
        return false;
    }

    const uint64_t OutBytes = static_cast<uint64_t>(DimPx) * DimPx * 4ull;
    if (S.OutputBuffer.BufferHandle == VK_NULL_HANDLE || S.OutputCapacityBytes < OutBytes)
    {
        S.Device->ReleaseBuffer(S.OutputBuffer);
        const uint64_t AllocBytes = std::max<uint64_t>(OutBytes, 512ull * 512ull * 4ull);
        S.OutputBuffer = S.Encoder.AllocateReadbackStagingBuffer(AllocBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        S.OutputCapacityBytes = (S.OutputBuffer.BufferHandle != VK_NULL_HANDLE) ? AllocBytes : 0ull;
    }
    if (S.OutputBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return false;
    }

    ThumbnailSpecimenUniformStd140 Uni {};
    Uni.DimCategoryMode[0] = DimPx;
    Uni.DimCategoryMode[1] = DimPx;
    Uni.DimCategoryMode[2] = static_cast<uint32_t>(Desc.Category);
    Uni.DimCategoryMode[3] = Desc.SubMode;
    Uni.BaseTintAlpha[0]   = static_cast<float>(Desc.Tint.Red)   / 255.0f;
    Uni.BaseTintAlpha[1]   = static_cast<float>(Desc.Tint.Green) / 255.0f;
    Uni.BaseTintAlpha[2]   = static_cast<float>(Desc.Tint.Blue)  / 255.0f;
    Uni.BaseTintAlpha[3]   = Desc.Metallic;
    Uni.SurfaceParams[0]   = Desc.Roughness;
    Uni.SurfaceParams[1]   = Desc.Clearcoat;
    Uni.SurfaceParams[2]   = Desc.Transmission;
    Uni.SurfaceParams[3]   = Desc.Emission;
    std::memcpy(S.ThumbnailUniformBuffer.HostAddress, &Uni, sizeof(Uni));

    bool Ok = false;
    S.Encoder.ResetTransientDescriptors();
    VkCommandBuffer Cmd = S.Device->BeginSingleTimeCommands();
    if (Cmd != VK_NULL_HANDLE)
    {
        const std::array<GeometricRaster::ComputeSlotDescriptor, 2u> Slots {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, S.ThumbnailUniformBuffer.BufferHandle, sizeof(ThumbnailSpecimenUniformStd140) },
            { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, S.OutputBuffer.BufferHandle,           OutBytes },
        }};
        const uint32_t GX = (DimPx + 7u) / 8u;
        const uint32_t GY = (DimPx + 7u) / 8u;
        if (S.Encoder.EncodeComputeDispatch(Cmd, S.ThumbnailProgramHandle, Slots, GX, GY, 1u))
        {
            VkMemoryBarrier Barrier {};
            Barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            Barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0u, 1u, &Barrier, 0u, nullptr, 0u, nullptr);
            const uint64_t SyncPoint = S.Device->DispatchCommandBufferAsync(Cmd);
            Ok = S.Device->AwaitTimelineTick(SyncPoint, 60000000000ull);
        }
        S.Device->FreeCommandBuffer(Cmd);
    }

    if (Ok && S.OutputBuffer.HostAddress != nullptr)
    {
        OutRgba8.resize(static_cast<std::size_t>(OutBytes));
        std::memcpy(OutRgba8.data(), S.OutputBuffer.HostAddress, static_cast<std::size_t>(OutBytes));
        S.ThumbnailGpuBakeCount.fetch_add(1ull, std::memory_order_relaxed);
        return true;
    }
    return false;
}

uint64_t InspectorGpuBaker::QueryGpuBakeCount() noexcept
{
    return AccessState().GpuBakeCount.load(std::memory_order_relaxed);
}

uint64_t InspectorGpuBaker::QueryDirectGpuBakeCount() noexcept
{
    return AccessState().DirectGpuBakeCount.load(std::memory_order_relaxed);
}

uint64_t InspectorGpuBaker::QueryThumbnailGpuBakeCount() noexcept
{
    return AccessState().ThumbnailGpuBakeCount.load(std::memory_order_relaxed);
}

uint64_t InspectorGpuBaker::QueryCpuFallbackCount() noexcept
{
    return AccessState().CpuFallbackCount.load(std::memory_order_relaxed);
}

float InspectorGpuBaker::QueryLastGpuBakeMs() noexcept
{
    return static_cast<float>(AccessState().LastGpuBakeMicros.load(std::memory_order_relaxed)) * 0.001f;
}

} // namespace Frontier
