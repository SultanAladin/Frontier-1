/*==========================================================================================================================================
                                                        BROWSERPREVIEWRENDERING.H
==========================================================================================================================================*/
// 🧩 Dedicated Project Browser preview unit: MSAA EON/GGX scene rasterization plus depth-tested drafting contours.
// 📝 Source: Experimental/Project browser/ReferenceRenderer.js (Configure/Rasterize/Capture, Scene + Drafting programs).
// ⚠️ Borrows one device. The submission owner serializes a single graphics queue; no fences or idle waits are created here.
// Serialize calls through the submission owner. Record only after this unit's completion fence; retire replaced targets
// after every submission reading them completes. Geometry/extents are immutable: replace and retire at that boundary.

#pragma once
#include "SurfaceRasterization.h"
#include "BrowserPreviewGeometry.h"
#include "Engine/ContentInterchange/ProjectBrowserCatalogue.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vulkan/vulkan.h>

namespace Frontier
{
// 📝 Column-major GL-style product remapped to Vulkan depth on upload; Y is flipped in the matrix (Vulkan 1.0 core).
struct BrowserPreviewProjection
{
    std::array<float, 16> ClipFromSurface{};
    std::array<float, 4> EyeChannel{};
    std::array<float, 4> RasterOptions{};
    std::array<float, 4> ViewDirection{};
};

struct BrowserPreviewThumbnail
{
    uint64_t Generation = 0;
    bool Ready = false;
};

struct BrowserPreviewImport
{
    uint64_t Generation = 0;
    uint32_t Width = 0, Height = 0;
    bool Ready = false;
    std::string Diagnostic;
};

class BrowserPreviewRendering
{
  public:
    BrowserPreviewRendering();
    ~BrowserPreviewRendering();
    BrowserPreviewRendering(const BrowserPreviewRendering &) = delete;
    BrowserPreviewRendering &operator=(const BrowserPreviewRendering &) = delete;
    // 📝 ShaderDirectory holds BrowserPreviewVertex/Fragment.spv + BrowserDraftingVertex/Fragment.spv.
    void Initialize(const VulkanRasterAccess &Access, const std::filesystem::path &ShaderDirectory);
    void Finalize() noexcept;
    // 📝 Live canvas follows the panel box, clamped to 1600x1200 exactly like the reference Rasterize.
    void ConfigureLive(const BrowserPreviewConfiguration &Configuration, const BrowserPreviewCamera &Camera,
        int Channel, int Width, int Height);
    // 📝 Tiles render 384x246 with the default camera (HTML Capture) unless recaptured with a live camera.
    void RequestThumbnail(const std::string &Key, const BrowserPreviewConfiguration &Configuration);
    void CaptureThumbnail(
        const std::string &Key, const BrowserPreviewConfiguration &Configuration, const BrowserPreviewCamera &Camera,
        int Channel);
    void RequestImportImage(const std::string &Key, const std::vector<uint8_t> &ImageBytes);
    void ConstructCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial);
    void ConfirmSubmission() noexcept;
    [[nodiscard]] bool AuthenticateThumbnail(const std::string &Key, uint64_t Generation) const;
    [[nodiscard]] BrowserPreviewThumbnail QueryThumbnail(const std::string &Key) const;
    [[nodiscard]] BrowserPreviewImport QueryImportImage(const std::string &Key) const;
    [[nodiscard]] VkImage QueryLiveImage() const noexcept;
    [[nodiscard]] VkImageView QueryLiveView() const noexcept;
    [[nodiscard]] VkImage QueryThumbnailImage(const std::string &Key) const noexcept;
    [[nodiscard]] VkImageView QueryThumbnailView(const std::string &Key) const noexcept;
    [[nodiscard]] VkImage QueryImportVkImage(const std::string &Key) const noexcept;
    [[nodiscard]] VkImageView QueryImportView(const std::string &Key) const noexcept;
    [[nodiscard]] bool QueryLiveReady() const noexcept;
    [[nodiscard]] const std::string &QueryDiagnostic() const noexcept;

  private:
    struct RenderingAllocation;
    std::unique_ptr<RenderingAllocation> Allocation;
};
} // namespace Frontier
