/*==========================================================================================================================================
                                                   TYREPROJECTIONVULKAN.H
==========================================================================================================================================*/
// 🧩 Record a caller-owned Vulkan dispatch for the standalone GPU tyre projection.
#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

namespace Frontier
{

enum class TyreProjectionOperation { Reset, Advance };

struct TyreProjectionVulkanAccess
{
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline ResetPipeline = VK_NULL_HANDLE;
    VkPipeline AdvancePipeline = VK_NULL_HANDLE;
    VkDescriptorSet Descriptors = VK_NULL_HANDLE;
    PFN_vkCmdBindPipeline BindPipeline = nullptr;
    PFN_vkCmdBindDescriptorSets BindDescriptors = nullptr;
    PFN_vkCmdPushConstants PushConstants = nullptr;
    PFN_vkCmdDispatch Dispatch = nullptr;
    std::uint32_t MaxGroupCountX = 0;
};

// 📝 Projection bindings 0/1/2 are command/particle/result storage buffers; the solid additionally uses
//    binding 3 for its fixed reference cells and a different result stride. Both pipeline layouts require a
//    four-byte compute push-constant range at offset zero. Groups contain 32 invocations; the recorded count
//    excludes padded lanes, even if bound buffers contain additional tyres. The caller supplies valid pipelines,
//    descriptor sets, correctly sized buffers, barriers and object lifetimes through completion. Zero tyres records nothing.
void RecordTyreProjection(VkCommandBuffer Commands, const TyreProjectionVulkanAccess &Access,
                          TyreProjectionOperation Operation, std::uint32_t TyreCount);

} // namespace Frontier
