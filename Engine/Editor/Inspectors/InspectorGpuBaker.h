/*============================================================================================================================================
                                                          INSPECTORGPUBAKER.H
============================================================================================================================================*/
// 🧩 Pure-GPU Vulkan compute baker for Inspector previews (zero-copy direct GPU ImGui images) & Content Browser thumbnail specimens

#pragma once

#include "Engine/Components/ContentBrowser/ThumbnailSheetRepository.h"
#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Frontier::GeometricRaster
{
class ImGuiOverlayRasterization;
}

namespace Frontier
{

class InspectorGpuBaker
{
public:
    static bool     AttachDevice(GeometricRaster::VulkanInterchange* Device, const std::filesystem::path& ShaderDirectory);
    static void     DetachDevice() noexcept;

    // Optional zero-copy ImGui presenter hook: when attached, Inspector preview surfaces bake directly
    // into persistent device-local VkImages bound to ImGui (0 CPU readback, 0 memcpy, 0 re-upload!).
    static void     AttachImGuiPresenter(GeometricRaster::ImGuiOverlayRasterization* Presenter) noexcept;
    static void     DetachImGuiPresenter() noexcept;

    [[nodiscard]] static bool     QueryReady() noexcept;
    [[nodiscard]] static bool     QueryDirectImageReady() noexcept;
    [[nodiscard]] static bool     QueryThumbnailBakerReady() noexcept;

    [[nodiscard]] static bool     DispatchBake(
        const InspectorGpuBakeSpec& Spec,
        uint32_t                    WidthPx,
        uint32_t                    HeightPx,
        std::vector<uint8_t>&       OutRgba8);

    [[nodiscard]] static bool     DispatchBakeDirectToImage(
        uint64_t                    SurfaceId,
        const InspectorGpuBakeSpec& Spec,
        uint32_t                    WidthPx,
        uint32_t                    HeightPx,
        uint64_t&                   OutNativeTexHandle);

    static void                   ReleaseDirectSurface(uint64_t SurfaceId) noexcept;

    [[nodiscard]] static bool     DispatchThumbnailBake(
        const ThumbnailSheetRepository::ThumbnailGpuBakeDesc& Desc,
        uint32_t                                              DimPx,
        std::vector<uint8_t>&                                 OutRgba8);

    [[nodiscard]] static uint64_t QueryGpuBakeCount() noexcept;
    [[nodiscard]] static uint64_t QueryDirectGpuBakeCount() noexcept;
    [[nodiscard]] static uint64_t QueryThumbnailGpuBakeCount() noexcept;
    [[nodiscard]] static uint64_t QueryCpuFallbackCount() noexcept;
    [[nodiscard]] static float    QueryLastGpuBakeMs() noexcept;
};

} // namespace Frontier
