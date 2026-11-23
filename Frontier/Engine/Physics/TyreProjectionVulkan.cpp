/*==========================================================================================================================================
                                                  TYREPROJECTIONVULKAN.CPP
==========================================================================================================================================*/
// 🧩 Borrow the submission owner's pipeline and descriptors; never allocate or synchronize here.

#include "TyreProjectionVulkan.h"

#include <stdexcept>

namespace Frontier
{
void RecordTyreProjection(VkCommandBuffer Commands, const TyreProjectionVulkanAccess &Access,
                          TyreProjectionOperation Operation, std::uint32_t TyreCount)
{
    if (TyreCount == 0) return;
    const std::uint32_t GroupCount = TyreCount / 32 + (TyreCount % 32 != 0 ? 1 : 0);
    const VkPipeline Pipeline = Operation == TyreProjectionOperation::Reset ? Access.ResetPipeline : Access.AdvancePipeline;
    if ((Operation != TyreProjectionOperation::Reset && Operation != TyreProjectionOperation::Advance) ||
        Commands == VK_NULL_HANDLE || Access.PipelineLayout == VK_NULL_HANDLE || Pipeline == VK_NULL_HANDLE ||
        Access.Descriptors == VK_NULL_HANDLE || !Access.BindPipeline || !Access.BindDescriptors || !Access.PushConstants ||
        !Access.Dispatch || GroupCount > Access.MaxGroupCountX)
        throw std::invalid_argument("Tyre projection dispatch has missing objects or exceeds the device group limit");
    Access.BindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
    Access.BindDescriptors(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Access.PipelineLayout, 0, 1,
                           &Access.Descriptors, 0, nullptr);
    Access.PushConstants(Commands, Access.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TyreCount), &TyreCount);
    Access.Dispatch(Commands, GroupCount, 1, 1);
}
} // namespace Frontier
