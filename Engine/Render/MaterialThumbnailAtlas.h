/*==========================================================================================================================================
                                                       MATERIALTHUMBNAILATLAS.H
==========================================================================================================================================*/
// 🧩 One device-local sampled image receives GPU-rendered material tiles with replicated gutters and no pixel readback.

#pragma once
#include "SurfaceRasterization.h"
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     GPU THUMBNAIL ATLAS
//------------------------------------------------------------------------------------------------------------------------

struct ThumbnailUvRectangle
{
    float MinimumU = 0, MinimumV = 0, MaximumU = 0, MaximumV = 0;
};

// ⚠️ The caller serializes one graphics queue, records at most one tile per submission, confirms accepted submission
// and retires all uses before destruction. An aborted recording must be discarded with the atlas.
class MaterialThumbnailAtlas
{
  public:
    MaterialThumbnailAtlas();
    ~MaterialThumbnailAtlas();
    MaterialThumbnailAtlas(const MaterialThumbnailAtlas &) = delete;
    MaterialThumbnailAtlas &operator=(const MaterialThumbnailAtlas &) = delete;
    void Initialize(const VulkanRasterAccess &Access, uint32_t TileExtent = 128, uint32_t Columns = 8);
    void ConstructCopy(VkCommandBuffer Commands, SurfaceRasterization &Source, uint32_t Slot);
    // 📝 CPU-rasterized pages (pattern gallery) stage full-atlas RGBA8, then record one buffer upload per page flip.
    void StagePixels(std::span<const uint8_t> Rgba);
    void ConstructUpload(VkCommandBuffer Commands);
    // 📝 One-shot whole-atlas readback to host memory, used only to persist a settled bake to disk.
    // Independent of the single-tile-write contract above: it never touches Recorded/Submitted and may
    // be recorded on any submission where the atlas is not mid-write. Requires at least one prior write
    // or upload (the atlas must already be in its settled SHADER_READ_ONLY_OPTIMAL layout).
    void ConstructReadback(VkCommandBuffer Commands, uint64_t SubmissionSerial);
    [[nodiscard]] bool QueryReadbackReady(uint64_t CompletedSerial) const noexcept;
    // 📝 Consumes the pending readback; throws if QueryReadbackReady was not true for this CompletedSerial.
    [[nodiscard]] std::vector<uint8_t> RetrieveReadback(uint64_t CompletedSerial);
    void ConfirmSubmission() noexcept;
    void Finalize() noexcept;
    [[nodiscard]] VkImage QueryImage() const noexcept;
    [[nodiscard]] VkImageView QueryImageView() const noexcept;
    [[nodiscard]] uint32_t QueryExtent() const noexcept;
    [[nodiscard]] uint32_t QueryCapacity() const noexcept;
    [[nodiscard]] ThumbnailUvRectangle QueryUv(uint32_t Slot) const;

  private:
    struct AtlasAllocation;
    std::unique_ptr<AtlasAllocation> Allocation;
};
} // namespace Frontier
