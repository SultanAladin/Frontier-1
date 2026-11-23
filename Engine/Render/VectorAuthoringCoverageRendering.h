/*============================================================================================================================================
                                                         VECTORAUTHORINGCOVERAGERENDERING.H
============================================================================================================================================*/
// 🧩 Reuse retired Vulkan coverage images for interactive authoring without modifying an image still sampled by an earlier submission.

#pragma once
#include "VectorCoverageRasterization.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Frontier
{
class VectorAuthoringCoverageRendering
{
public:
    void Initialize(const VulkanRasterAccess &Access, RasterShader Shader);
    void Initialize(const VulkanRasterAccess &Access, const std::filesystem::path &ShaderArchive);
    // 📝 Call during main-viewport UI construction; its chosen image is produced before that UI submission samples it.
    // 📝 DisplayExtent is an optional device-pixel hint (e.g. the live canvas quad at its current
    // zoom): when the authored content is unchanged but the hint now needs a different pooled tier,
    // the slot re-bakes at that tier so the shown image stays vector-crisp instead of being a
    // raster stretched past its baked resolution. Zero keeps the prior budget-only behaviour.
    int SelectProgram(const VectorCoverageProgram &Program, uint32_t DisplayExtent = 0);
    int SelectSurfaceProgram(const VectorSurfaceProgram &Program, uint32_t DisplayExtent = 0);
    void ConstructCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial);
    void ConfirmSubmission();
    void Finalize() noexcept;
    [[nodiscard]] VkImage QueryImage(size_t Slot) const;
    [[nodiscard]] VkImageView QueryImageView(size_t Slot) const;
    // 📝 Active bake edge extent of a slot (0 before its first fit); hosts retain one descriptor per slot/extent.
    [[nodiscard]] uint32_t QuerySlotExtent(size_t Slot) const;
    // 📝 True once the selected program's image has been recorded and its sampling submission retired.
    [[nodiscard]] bool QuerySelectedReady() const noexcept;
    [[nodiscard]] uint64_t QueryDispatchCount() const noexcept;
    // 📝 Pure pooled-tier decision (no GPU access, no allocation) extracted out of RefitSlot so it can be
    // exercised directly by a headless CPU-only regression test: given the slot's current pooled tier
    // index (5 == none fitted yet) and the requested Segments/DisplayExtent, returns which of the 5 pooled
    // tiers (128/256/512/1024/2048, index 0..4) the slot should sit at, applying the same one-tier-up
    // hysteresis guard RefitSlot uses against a jittering device-pixel hint near a tier boundary.
    [[nodiscard]] static size_t SelectPooledTier(size_t ActiveTier, size_t Segments, uint32_t DisplayExtent) noexcept;
    // 📝 Pure resubmission decision (no GPU access) extracted out of SelectProgram/SelectSurfaceProgram's
    // "content unchanged" branch so it can be exercised directly by a headless CPU-only regression test:
    // given whether this frame's RefitSlot call actually re-tiled the slot (a real change requiring a
    // fresh bake) and whether the slot has ever completed a submission before, decides whether this frame
    // still needs to ask the device for a new dispatch. Unchanged, already-baked content must NOT keep
    // asking: a pipelined device only advances its completed-serial a frame or two behind whatever was
    // just submitted, so resubmitting every single frame for no reason keeps the slot's SampledSerial
    // perpetually ahead of Completed and QuerySelectedReady() never reports ready — the "Baking..." badge
    // sticks forever even on a perfectly idle, unmodified canvas.
    [[nodiscard]] static bool RequiresUnchangedResubmission(bool Retiled, uint64_t SampledSerial) noexcept;
private:
    struct CoverageSlot
    {
        // 📝 One retained rasterization per bake extent (128/256/512/1024/2048/4096): reselecting a pattern
        // reuses its extent instead of destroying pipelines on the UI thread, and keeps the last valid
        // image live. 4096 matches the documented ceiling both canvas panels already clamp their
        // zoomed, device-pixel display size to (and VectorCoverageRasterization::Initialize's own
        // 4096x4096 image validation already allows) — below this, a sufficiently zoomed-in canvas
        // outgrows its own baked texture and visibly pixelates, a stretched raster past its resolution.
        std::array<std::unique_ptr<VectorCoverageRasterization>, 6> Extents;
        size_t Active = 6;
        std::optional<VectorCoverageProgram> Program;
        std::optional<VectorSurfaceProgram> SurfaceProgram;
        uint64_t SampledSerial = 0;
        bool Pending = false;
    };
    std::array<CoverageSlot, 3> Slots;
    VulkanRasterAccess RetainedAccess{};
    std::vector<uint32_t> RetainedShaderWords;
    std::string RetainedShaderEntry;
    uint64_t Completed = 0, PendingSerial = 0;
    int Selected = -1;
    bool SampleRequested = false;
    // 📝 Returns true when the slot's active pooled extent changed (a fresh dispatch is required
    // even if the authored content itself did not, e.g. a zoom-driven DisplayExtent change).
    bool RefitSlot(size_t Index, size_t Segments, uint32_t DisplayExtent = 0);
    VectorCoverageRasterization &ActiveRasterization(size_t Index);
    [[nodiscard]] const VectorCoverageRasterization *QueryActiveRasterization(size_t Slot) const;
};
}
