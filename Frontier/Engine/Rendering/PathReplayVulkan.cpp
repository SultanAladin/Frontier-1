/*==========================================================================================================================================
                                                    PATHREPLAYVULKAN.CPP
==========================================================================================================================================*/
// 🧩 Validate the bounded replay dispatch and record its dependent Vulkan compute stages.

#include "PathReplayVulkan.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace Frontier
{
namespace
{
constexpr std::uint32_t ReplayFlags = PathReplayRoughOpaque | PathReplaySampleFacets | PathReplayAuditDoubleCount |
    PathReplayAuditStrictMotion | PathReplayBalanceSampling | PathReplayTexturedDiffuse | PathReplayImportanceRough |
    PathReplayPowerWeightedFacets | PathReplayUniformFacetMixture | PathReplayReconstruct | PathReplayResetReconstruction |
    PathReplayDiffuseCameraReprojection | PathReplaySampleHistory | PathReplayResetSampleHistory;

void RequireReplay(bool Condition)
{
    if (!Condition) throw std::invalid_argument("Invalid or undersized path replay Vulkan dispatch");
}
} // namespace

void RecordPathReplay(VkCommandBuffer Commands, const PathReplayVulkanAccess &Access,
                      const PathReplayVulkanBuffers &Buffers, const LightComputeFootprint &Extent)
{
    const std::uint64_t Count = Extent.Dimensions[0];
    const std::uint64_t Groups = Count / 64u + (Count % 64u != 0 ? 1u : 0u);
    const std::uint32_t Flags = Extent.Primary[3];
    const bool Primary = Extent.Primary[0] != 0, Sources = (Flags & PathReplaySampleFacets) != 0;
    const bool Weighted = (Flags & PathReplayPowerWeightedFacets) != 0, Balance = (Flags & PathReplayBalanceSampling) != 0;
    const bool DoubleCount = (Flags & PathReplayAuditDoubleCount) != 0;
    const bool Reconstruct = (Flags & PathReplayReconstruct) != 0;
    const bool SampleHistory = (Flags & PathReplaySampleHistory) != 0;
    RequireReplay(Commands != VK_NULL_HANDLE && Access.PipelineLayout != VK_NULL_HANDLE &&
        Access.Descriptors != VK_NULL_HANDLE && Access.Generate != VK_NULL_HANDLE && Access.Resolve != VK_NULL_HANDLE &&
        (!Primary || Access.Primary != VK_NULL_HANDLE) &&
        (!Reconstruct || (Access.GatherReconstruction != VK_NULL_HANDLE && Access.FinalizeReconstruction != VK_NULL_HANDLE)) &&
        (!SampleHistory || (Access.GatherSampleHistory != VK_NULL_HANDLE && Access.FinalizeSampleHistory != VK_NULL_HANDLE)) &&
        Access.BindPipeline && Access.BindDescriptors && Access.PushConstants && Access.Dispatch && Access.PipelineBarrier &&
        Access.MaxPushConstantBytes >= sizeof(LightComputeFootprint));
    RequireReplay(Count > 0 && Groups <= Access.MaxGroupCountX && Extent.History[0] > 0 && Extent.History[1] > 0 &&
        std::uint64_t(Extent.History[0]) * Extent.History[1] == Count && Extent.Dimensions[1] == 0 &&
        Extent.Primary[0] <= 1 && Extent.Primary[2] > 0 && Extent.Primary[2] <= 8 &&
        Extent.Sampling[1] <= PathReplayMaximumInitialCandidates &&
        Extent.Sampling[3] > 0 && Extent.Sampling[3] <= 64 && Extent.Reconstruction[0] <= 1 &&
        Extent.Reconstruction[1] <= 1 && Extent.Reconstruction[3] > 0 && Extent.Reconstruction[3] <= 64);
    RequireReplay((Flags & ~ReplayFlags) == 0 && (!DoubleCount || Sources) && (!Balance || (Sources && !DoubleCount)) &&
        (!Weighted || Sources) && (!(Flags & PathReplayUniformFacetMixture) || Weighted) &&
        (!(Flags & PathReplayImportanceRough) || (Flags & PathReplayRoughOpaque)) &&
        (!(Flags & PathReplayResetReconstruction) || Reconstruct) &&
        (!(Flags & PathReplayDiffuseCameraReprojection) ||
            (Reconstruct && Primary && !(Flags & (PathReplayRoughOpaque | PathReplayTexturedDiffuse)))) &&
        (!(Flags & PathReplayResetSampleHistory) || SampleHistory) &&
        (!SampleHistory || (Primary && !Reconstruct &&
            !(Flags & (PathReplayRoughOpaque | PathReplayTexturedDiffuse | PathReplayAuditDoubleCount)) &&
            Extent.Sampling[1] <= 1)) &&
        (Sources || Extent.Reconstruction[2] == 0));
    for (std::uint32_t Axis = 0; Axis < 3; ++Axis)
        RequireReplay(std::isfinite(Extent.Minimum[Axis]) && Extent.Minimum[Axis] >= 0 && Extent.Minimum[Axis] <= 256);
    std::uint32_t DistanceOffset = 0, FogOffset = 0;
    std::memcpy(&DistanceOffset, &Extent.Maximum[3], sizeof(DistanceOffset));
    std::memcpy(&FogOffset, &Extent.Minimum[3], sizeof(FogOffset));
    RequireReplay(std::isfinite(Extent.Maximum[0]) && Extent.Maximum[0] >= 1e-4f && Extent.Maximum[0] <= 10 &&
        DistanceOffset == 0 && FogOffset == 0 && Buffers.ReceiverRecords >= Count && Buffers.FacetRecords >= Extent.Sampling[2] &&
        Buffers.EnclosureRecords >= Extent.Grid[3] && ((Extent.Sampling[2] == 0) == (Extent.Grid[3] == 0)) &&
        Extent.Reconstruction[2] <= Extent.Sampling[2] &&
        Buffers.CounterWords >= ((Reconstruct || SampleHistory) ? PathReplayReconstructionCounterWords : PathReplayCounterWords));
    // 📝 ReplaySlot(4), slot 10 for colour, or slot 14 for the seed bank is the last shader index.
    const std::uint32_t MaximumReceivers = SampleHistory ? PathReplayMaxSampleHistoryReceivers :
        (Reconstruct ? PathReplayMaxReconstructedReceivers : PathReplayMaxReceivers);
    const std::uint64_t RequiredOutput = SampleHistory ?
        PathReplayRequiredSampleHistoryOutputRecords(static_cast<std::uint32_t>(Count)) :
        (Reconstruct ? PathReplayRequiredReconstructedOutputRecords(static_cast<std::uint32_t>(Count)) :
                       PathReplayRequiredOutputRecords(static_cast<std::uint32_t>(Count)));
    RequireReplay(Count <= MaximumReceivers && Buffers.OutputColourRecords >= RequiredOutput);
    // Camera and source offsets are added as uint32 in Slang, even when the Vulkan range is larger.
    constexpr std::uint64_t ShaderAddressLimit = std::uint64_t(UINT32_MAX) + 1u;
    // Textured secondary hits use CameraProjection for their ray footprint even without primary tracing.
    if (Primary || (Flags & PathReplayTexturedDiffuse) != 0)
    {
        const std::uint64_t CameraEnd = std::uint64_t(Extent.Primary[1]) + PathReplayCameraFloats;
        RequireReplay(CameraEnd <= Buffers.AngleFloats && CameraEnd <= ShaderAddressLimit);
    }
    if (Sources)
    {
        std::uint32_t Offset = 0;
        static_assert(sizeof(Offset) == sizeof(Extent.Maximum[1]));
        std::memcpy(&Offset, &Extent.Maximum[1], sizeof(Offset));
        const std::uint64_t Required = Weighted ? 1u + std::uint64_t(Extent.Reconstruction[2]) * 4u
                                                : Extent.Reconstruction[2];
        const std::uint64_t SourceEnd = std::uint64_t(Offset) + Required;
        RequireReplay(SourceEnd <= Buffers.AngleFloats && SourceEnd <= ShaderAddressLimit);
    }

    Access.BindDescriptors(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Access.PipelineLayout, 0, 1,
                           &Access.Descriptors, 0, nullptr);
    Access.PushConstants(Commands, Access.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Extent), &Extent);
    VkMemoryBarrier Barrier{};
    Barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    const auto Dispatch = [&](VkPipeline Pipeline, VkAccessFlags SourceAccess)
    {
        Access.BindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
        Access.Dispatch(Commands, static_cast<std::uint32_t>(Groups), 1, 1);
        if (SourceAccess != 0)
        {
            Barrier.srcAccessMask = SourceAccess;
            Access.PipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   0, 1, &Barrier, 0, nullptr, 0, nullptr);
        }
    };
    if (Primary) Dispatch(Access.Primary, VK_ACCESS_SHADER_WRITE_BIT);
    if (SampleHistory)
    {
        // 📝 Gather must finish reading old guides and seed banks before resolve overwrites them, and
        //    generation now consumes the gathered bank, so gather precedes generation in this mode.
        Dispatch(Access.GatherSampleHistory, VK_ACCESS_SHADER_WRITE_BIT);
        Dispatch(Access.Generate, VK_ACCESS_SHADER_WRITE_BIT);
        Dispatch(Access.Resolve, VK_ACCESS_SHADER_WRITE_BIT);
        Dispatch(Access.FinalizeSampleHistory, 0);
    }
    else
    {
        Dispatch(Access.Generate, VK_ACCESS_SHADER_WRITE_BIT);
        if (Reconstruct)
        {
            // 📝 Gather must finish reading old guides before either is overwritten.
            Dispatch(Access.GatherReconstruction, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            Dispatch(Access.Resolve, VK_ACCESS_SHADER_WRITE_BIT);
            Dispatch(Access.FinalizeReconstruction, 0);
        }
        else Dispatch(Access.Resolve, 0);
    }
}
} // namespace Frontier
