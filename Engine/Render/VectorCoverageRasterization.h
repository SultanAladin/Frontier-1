/*============================================================================================================================================
                                                      VECTORCOVERAGERASTERIZATION.H
============================================================================================================================================*/
// 🧩 Borrowed-device Vulkan compute coverage with immutable programs, caller-retired uploads and cached output images.

#pragma once
#include "SurfaceRasterization.h"
#include "VectorCoverage.h"
#include "VectorSurfaceCompilation.h"

namespace Frontier
{
class VectorCoverageRasterization
{
public:
    VectorCoverageRasterization();
    ~VectorCoverageRasterization();
    VectorCoverageRasterization(const VectorCoverageRasterization &) = delete;
    VectorCoverageRasterization &operator=(const VectorCoverageRasterization &) = delete;
    void Initialize(const VulkanRasterAccess &Access, RasterShader Shader, uint32_t Width, uint32_t Height);
    // ⚠️ CompletedSerial must retire every earlier GPU read of the uploaded descriptors and output image.
    void ConfigureRetiredProgram(const VectorCoverageProgram &Program, uint64_t CompletedSerial);
    void ConfigureRetiredSurface(const VectorSurfaceProgram &Program, uint64_t CompletedSerial);
    // 📝 Selection shader uses one output texel; metadata occupies reserved upload slots, never authoring source.
    void ConfigureRetiredSelection(const VectorSelectionProgram &Program, std::array<float, 2> Pointer, uint64_t CompletedSerial);
    bool ConstructCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial);
    void ConstructReadbackCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial);
    [[nodiscard]] std::vector<float> RetrieveFloatReadback(uint64_t CompletedSerial);
    // 📝 Pass the serial of every submission that samples the cached image, even when no recomputation was necessary.
    void ConfirmSubmission(uint64_t SampledSerial = 0) noexcept;
    // ⚠️ Borrowed-device ownership: retire every GPU reference before Finalize or destruction; neither inserts queue waits.
    void Finalize() noexcept;
    // 📝 Adaptive bake resolution: dense weaves cost ~500 FLOP per curve per
    // pixel with no bounds rejection, so a single 512px six-channel submit
    // exceeds the Windows watchdog. Picks the largest square extent whose
    // curve-pixel-channel product fits a ~2e9 budget (~1 TFLOP per submit).
    // 📝 DisplayExtent is an optional hint (device pixels the result will actually be shown
    // at, e.g. a live canvas tracking zoom): the smallest pooled tier covering that hint is
    // chosen, still capped by the GPU budget above. Zero keeps the old budget-only behaviour,
    // which the material-bake path (fixed final texture resolution) always wants.
    [[nodiscard]] static uint32_t SelectBakeExtent(size_t Segments, uint32_t Channels, uint32_t DisplayExtent = 0);
    [[nodiscard]] VkImage QueryImage() const noexcept;
    [[nodiscard]] VkImageView QueryImageView() const noexcept;
    [[nodiscard]] VkExtent2D QueryExtent() const noexcept;
    [[nodiscard]] uint64_t QueryDispatchCount() const noexcept;
private:
    struct CoverageAllocation;
    std::unique_ptr<CoverageAllocation> Allocation;
};
}
