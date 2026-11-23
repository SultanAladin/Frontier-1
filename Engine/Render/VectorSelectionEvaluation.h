/*============================================================================================================================================
                                                       VECTORSELECTIONEVALUATION.H
============================================================================================================================================*/
// 🧩 Asynchronous one-point Slang coverage selection with four-byte token readback and caller-retired Vulkan resources.

#pragma once
#include "VectorCoverageRasterization.h"
#include <optional>

namespace Frontier
{
class VectorSelectionEvaluation
{
public:
    VectorSelectionEvaluation();
    ~VectorSelectionEvaluation();
    VectorSelectionEvaluation(const VectorSelectionEvaluation &) = delete;
    VectorSelectionEvaluation &operator=(const VectorSelectionEvaluation &) = delete;
    void Initialize(const VulkanRasterAccess &Access, RasterShader Shader);
    void Initialize(const VulkanRasterAccess &Access, const std::filesystem::path &ShaderArchive);
    // 📝 Nullopt means pending; token zero is an evaluated miss. No CPU contour or bounds fallback.
    std::optional<uint32_t> QueryToken(const VectorSelectionProgram &Program, std::array<float, 2> Pointer);
    void ConstructCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial);
    void ConfirmSubmission();
    // ⚠️ Retire every submitted command before finalization/destruction; this class never waits on a queue.
    void Finalize() noexcept;
    [[nodiscard]] uint64_t QueryDispatchCount() const noexcept;
private:
    struct SelectionAllocation;
    std::unique_ptr<SelectionAllocation> Allocation;
};
}
