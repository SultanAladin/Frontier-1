/*============================================================================================================================================
                                                        IMGUIOVERLAYRASTERIZATION.H
============================================================================================================================================*/
// 🧩 Native Vulkan 1.2+ swapchain & offscreen presenter executing ImGuiOverlayRasterization.slang over GLFW OS windows or headless targets

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Windowing/PlatformWindow.h"

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct OverlayRasterizationStats
{
    bool     LiveWindowSwapchain = false; // [-]     - True when presenting to OS VkSwapchainKHR; false when offscreen
    uint32_t WidthPx             = 0u;    // [px]    - Active presentation width
    uint32_t HeightPx            = 0u;    // [px]    - Active presentation height
    uint64_t PresentedBatchCount = 0ull;  // [count] - Total frames rasterized through ImGuiOverlayRasterization.slang
    uint32_t LastDrawCommandCount = 0u;    // [count] - ImGui draw commands submitted during the latest frame
    uint32_t LastDrawVertexCount  = 0u;    // [count] - Expanded ImGui vertices submitted during the latest frame
    uint32_t ResidentTextureCount = 0u;    // [count] - ImGui texture records represented by Vulkan descriptors
    uint64_t TextureUploadCount   = 0ull;  // [count] - Completed ImGui texture create/update uploads
};

class ImGuiOverlayRasterization
{
public:
    ImGuiOverlayRasterization() = default;
    ~ImGuiOverlayRasterization();

    ImGuiOverlayRasterization(const ImGuiOverlayRasterization&)            = delete;
    ImGuiOverlayRasterization& operator=(const ImGuiOverlayRasterization&) = delete;

    [[nodiscard]] bool InitializeOverlay(
        VulkanInterchange*              TargetDevice,
        const std::filesystem::path&      CompiledShaderDir,
        Windowing::DesktopPlatformWindow* OptionalDesktopWindow,
        uint32_t                          InitialWidthPx,
        uint32_t                          InitialHeightPx,
        std::string*                      OptionalDiagnostic = nullptr);

    void TeardownOverlay() noexcept;
    void PrepareImGuiFrame() noexcept;

    [[nodiscard]] bool PresentImGuiBatches(
        const ImDrawData* OverlayGeometry,
        uint32_t          TargetWidthPx,
        uint32_t          TargetHeightPx);

    [[nodiscard]] bool ReadbackLastPresentedRgba(
        std::vector<uint8_t>& OutRgbaPixels,
        uint32_t&             OutWidthPx,
        uint32_t&             OutHeightPx);

    [[nodiscard]] OverlayRasterizationStats InspectStats() const noexcept
    {
        return Stats;
    }

    [[nodiscard]] std::string_view QueryLastPresentationDiagnostic() const noexcept
    {
        return LastPresentationDiagnostic;
    }

private:
    struct CachedImGuiTexture
    {
        DeviceImageResource Image       {};
        VkDescriptorSet     Descriptor  = VK_NULL_HANDLE;
        uint64_t            ContentHash = 0ull;
        uint64_t            LastTouched = 0ull;
        uint64_t            LastPresentSerial = 0ull;
    };

    [[nodiscard]] bool ConstructRasterResources(VkFormat TargetFormat, std::string* OptionalDiagnostic);
    [[nodiscard]] bool RebuildSwapchainOrOffscreenTarget(uint32_t WidthPx, uint32_t HeightPx);
    [[nodiscard]] bool InitializeUpstreamImGuiBackend(std::string* OptionalDiagnostic);
    void               TeardownUpstreamImGuiBackend() noexcept;
    void               DestroyTargetAttachments() noexcept;
    [[nodiscard]] VkDescriptorSet EnsureTextureUploaded(ImTextureData* SourceTex, bool FontAtlasTexture);

    VulkanInterchange*                                      BoundDevice           = nullptr;
    Windowing::DesktopPlatformWindow*                         DesktopWindow         = nullptr;
    std::filesystem::path                                     ShaderDirectory       {};
    VkSurfaceKHR                                              WindowSurface         = VK_NULL_HANDLE;
    VkSwapchainKHR                                            SwapchainHandle       = VK_NULL_HANDLE;
    VkFormat                                                  ActiveColorFormat     = VK_FORMAT_R8G8B8A8_UNORM;
    std::vector<VkImage>                                      SwapchainImages       {};
    std::vector<VkImageView>                                  SwapchainViews        {};
    std::vector<VkFramebuffer>                                TargetAttachments     {};
    DeviceImageResource                                       OffscreenTarget       {};
    VkSemaphore                                               ImageAcquireSemaphore = VK_NULL_HANDLE;
    VkSemaphore                                               RenderDoneSemaphore   = VK_NULL_HANDLE;

    VkSampler                                                 LinearSampler         = VK_NULL_HANDLE;
    VkRenderPass                                              OverlayRenderUnit     = VK_NULL_HANDLE;
    VkDescriptorSetLayout                                     DescriptorLayout      = VK_NULL_HANDLE;
    VkDescriptorPool                                          DescriptorAllocator   = VK_NULL_HANDLE;
    VkPipelineLayout                                          RasterLayout          = VK_NULL_HANDLE;
    VkPipeline                                                OverlayRasterUnit     = VK_NULL_HANDLE;
    bool                                                      UpstreamImGuiBackendReady = false;

    DeviceBufferResource                                      VertexStreamBuffer    {};
    DeviceBufferResource                                      UploadStagingBuffer   {};
    DeviceImageResource                                       FallbackWhiteTex      {};
    VkDescriptorSet                                           FallbackDescriptor    = VK_NULL_HANDLE;
    std::unordered_map<const ImTextureData*, CachedImGuiTexture> TextureCache       {};
    uint64_t                                                      TextureUseCounter    = 0ull;
    uint64_t                                                      TexturePresentSerial = 0ull;
    std::string                                                   LastPresentationDiagnostic {};
    OverlayRasterizationStats                                   Stats                 {};
};

} // namespace Frontier::GeometricRaster
