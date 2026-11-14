/*============================================================================================================================================
                                                       PROGRESSIVESTRETCHRELAXER.CPP
============================================================================================================================================*/
// 🧩 Implementation of ProgressiveStretchRelaxer dispatching StretchRelaxCompute.comp.spv on Vulkan 1.2 Timeline Semaphores

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/Solvers/ProgressiveStretchRelaxer.h"

#include "Engine/Workspaces/UV/Solvers/SeamCutGenerator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace Frontier::Workspaces
{

namespace
{

struct GPURelaxCornerRecord
{
    float    CurrentUV[2]         {};
    float    NeighborAvgUV[2]     {};
    float    IsometricTargetUV[2] {};
    uint32_t PinnedOrSeamLock     = 0u;
    float    DampingWeight        = 1.0f;
};

struct GPURelaxDispatchParams
{
    uint32_t CornerCount   = 0u;
    float    StepScale     = 0.35f;
    uint32_t PreserveSeams = 1u;
    uint32_t ReservedPad   = 0u;
};

} // namespace

ProgressiveStretchRelaxer::~ProgressiveStretchRelaxer()
{
    TeardownGPUCompute();
}

bool ProgressiveStretchRelaxer::InitializeGPUCompute(
    GeometricRaster::VulkanInterchange* BoundDevice,
    const std::filesystem::path&        CompiledShaderDir)
{
    TeardownGPUCompute();
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return false;
    }
    this->DeviceInterchange = BoundDevice;
    if (!Encoder.InitializeEncoder(BoundDevice, 256u))
    {
        this->DeviceInterchange = nullptr;
        return false;
    }

    const std::array<VkDescriptorType, 3u> Slots {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    RelaxProgramHandle = Encoder.RegisterProgram(CompiledShaderDir / "StretchRelaxCompute.comp.spv", Slots, "ComputeEntry");
    return RelaxProgramHandle != UINT32_MAX;
}

void ProgressiveStretchRelaxer::TeardownGPUCompute() noexcept
{
    Encoder.TeardownEncoder();
    RelaxProgramHandle = UINT32_MAX;
    DeviceInterchange          = nullptr;
}

StretchRelaxReport ProgressiveStretchRelaxer::RelaxStretch(
    GeometrySpace::PolygonNetwork& Network,
    uint32_t                       RelaxationIterations,
    float                          StepScale,
    bool                           PreserveSeams)
{
    const auto StartClock = std::chrono::steady_clock::now();
    StretchRelaxReport Report {};

    const std::vector<UVIslandPartition> Islands = SeamCutGenerator::PartitionConnectedUVIslands(Network);
    if (Islands.empty() || RelaxationIterations == 0u)
    {
        return Report;
    }

    // Group welded UV corners per (IslandOrdinal, OriginVertexIndex) so welded edges move coherently without tearing
    struct WeldedJunction
    {
        uint32_t              VertexIndex      = 0u;
        uint32_t              PinnedOrSeamLock = 0u;
        Mathematics::UVAxes   CurrentUV        {};
        std::vector<uint32_t> IncidentEdges      {};
        std::vector<uint32_t> NeighborJunctions    {};
        std::vector<float>    RestLengths3D    {};
        float                 UVPerMeter       = 0.25f;
    };

    std::vector<WeldedJunction> Junctions;
    for (const auto& Island : Islands)
    {
        const float IslandScale = (Island.TotalArea3D > 1.0e-6f && Island.TotalAreaUV > 1.0e-8f)
            ? std::sqrt(Island.TotalAreaUV / Island.TotalArea3D)
            : 0.25f;

        std::unordered_map<uint32_t, uint32_t> VertToJunction;
        for (const uint32_t EdgeIdx : Island.CornerEdgeIndices)
        {
            const auto&    Edge = Network.Edges[EdgeIdx];
            const uint32_t VIdx = Edge.OriginVertexIndex;
            auto [It, Inserted] = VertToJunction.try_emplace(VIdx, static_cast<uint32_t>(Junctions.size()));
            if (Inserted)
            {
                WeldedJunction Junction {};
                Junction.VertexIndex = VIdx;
                Junction.CurrentUV   = Edge.ParameterUV;
                Junction.UVPerMeter  = IslandScale;
                Junctions.push_back(std::move(Junction));
            }
            Junctions[It->second].IncidentEdges.push_back(EdgeIdx);
        }

        for (const uint32_t BVert : Island.OrderedBoundaryVertices)
        {
            const auto It = VertToJunction.find(BVert);
            if (It != VertToJunction.end())
            {
                Junctions[It->second].PinnedOrSeamLock = PreserveSeams ? 1u : 2u;
            }
        }

        for (const uint32_t EdgeIdx : Island.CornerEdgeIndices)
        {
            const auto& Edge = Network.Edges[EdgeIdx];
            if (GeometrySpace::QueryEdgeAttributeEnabled(Edge.AttributeMask, GeometrySpace::EdgeAttributeMask::EnablePinLock))
            {
                Junctions[VertToJunction[Edge.OriginVertexIndex]].PinnedOrSeamLock = 1u;
            }
        }

        for (const uint32_t EdgeIdx : Island.CornerEdgeIndices)
        {
            const auto&    Edge  = Network.Edges[EdgeIdx];
            const uint32_t V0    = Edge.OriginVertexIndex;
            const uint32_t V1    = Network.Edges[Edge.NextEdgeIndex].OriginVertexIndex;
            const uint32_t N0    = VertToJunction[V0];
            const uint32_t N1    = VertToJunction[V1];
            const auto&    P0    = Network.Vertices[V0].Axes;
            const auto&    P1    = Network.Vertices[V1].Axes;
            const float    Len3D = Mathematics::EvaluateDistance(P0, P1);

            Junctions[N0].NeighborJunctions.push_back(N1);
            Junctions[N0].RestLengths3D.push_back(Len3D);
            Junctions[N1].NeighborJunctions.push_back(N0);
            Junctions[N1].RestLengths3D.push_back(Len3D);
        }
    }

    const uint32_t JunctionCount = static_cast<uint32_t>(Junctions.size());
    if (JunctionCount == 0u)
    {
        return Report;
    }

    const bool UseGPU = (DeviceInterchange != nullptr && DeviceInterchange->Ready() && RelaxProgramHandle != UINT32_MAX);
    GeometricRaster::DeviceBufferResource ParamBuf {};
    GeometricRaster::DeviceBufferResource InputBuf {};
    GeometricRaster::DeviceBufferResource OutputBuf {};

    if (UseGPU)
    {
        ParamBuf  = Encoder.AllocateHostWritableBuffer(sizeof(GPURelaxDispatchParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        InputBuf  = Encoder.AllocateHostWritableBuffer(sizeof(GPURelaxCornerRecord) * JunctionCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        OutputBuf = Encoder.AllocateHostWritableBuffer(sizeof(float) * 2u * JunctionCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }

    std::vector<GPURelaxCornerRecord> Records(JunctionCount);
    const uint32_t ClampedIterations = std::clamp(RelaxationIterations, 1u, 64u);

    for (uint32_t IterIdx = 0u; IterIdx < ClampedIterations; ++IterIdx)
    {
        for (uint32_t Idx = 0u; Idx < JunctionCount; ++Idx)
        {
            const auto& Junction = Junctions[Idx];
            GPURelaxCornerRecord Rec {};
            Rec.CurrentUV[0]     = Junction.CurrentUV.U;
            Rec.CurrentUV[1]     = Junction.CurrentUV.V;
            Rec.PinnedOrSeamLock = Junction.PinnedOrSeamLock;
            Rec.DampingWeight    = 0.75f;

            if (Junction.NeighborJunctions.empty())
            {
                Rec.NeighborAvgUV[0]     = Junction.CurrentUV.U;
                Rec.NeighborAvgUV[1]     = Junction.CurrentUV.V;
                Rec.IsometricTargetUV[0] = Junction.CurrentUV.U;
                Rec.IsometricTargetUV[1] = Junction.CurrentUV.V;
            }
            else
            {
                float AvgU = 0.0f, AvgV = 0.0f;
                float IsoU = 0.0f, IsoV = 0.0f;
                for (std::size_t K = 0u; K < Junction.NeighborJunctions.size(); ++K)
                {
                    const auto& NbUV  = Junctions[Junction.NeighborJunctions[K]].CurrentUV;
                    AvgU += NbUV.U;
                    AvgV += NbUV.V;

                    const float dU   = Junction.CurrentUV.U - NbUV.U;
                    const float dV   = Junction.CurrentUV.V - NbUV.V;
                    const float CurL = std::hypot(dU, dV);
                    const float Rest = std::max(1.0e-5f, Junction.RestLengths3D[K] * Junction.UVPerMeter);
                    if (CurL > 1.0e-6f)
                    {
                        IsoU += NbUV.U + (dU / CurL) * Rest;
                        IsoV += NbUV.V + (dV / CurL) * Rest;
                    }
                    else
                    {
                        IsoU += Junction.CurrentUV.U;
                        IsoV += Junction.CurrentUV.V;
                    }
                }
                const float InvK = 1.0f / static_cast<float>(Junction.NeighborJunctions.size());
                Rec.NeighborAvgUV[0]     = AvgU * InvK;
                Rec.NeighborAvgUV[1]     = AvgV * InvK;
                Rec.IsometricTargetUV[0] = IsoU * InvK;
                Rec.IsometricTargetUV[1] = IsoV * InvK;
            }
            Records[Idx] = Rec;
        }

        if (UseGPU)
        {
            Encoder.ResetTransientDescriptors();
            const GPURelaxDispatchParams Params { JunctionCount, StepScale, PreserveSeams ? 1u : 0u, 0u };
            const std::array<GPURelaxDispatchParams, 1u> PSpan {{ Params }};
            Encoder.UploadSpanToBuffer<GPURelaxDispatchParams>(ParamBuf, PSpan);
            Encoder.UploadSpanToBuffer<GPURelaxCornerRecord>(InputBuf, Records);

            VkCommandBuffer Cmd = DeviceInterchange->BeginSingleTimeCommands();
            const std::array<GeometricRaster::ComputeSlotDescriptor, 3u> Slots {{
                { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ParamBuf.BufferHandle, sizeof(GPURelaxDispatchParams) },
                { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, InputBuf.BufferHandle, VK_WHOLE_SIZE },
                { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, OutputBuf.BufferHandle, VK_WHOLE_SIZE },
            }};
            (void)Encoder.EncodeComputeDispatch(Cmd, RelaxProgramHandle, Slots, (JunctionCount + 63u) / 64u);
            const uint64_t Signal = DeviceInterchange->DispatchCommandBufferAsync(Cmd);
            (void)DeviceInterchange->AwaitTimelineTick(Signal);
            vkFreeCommandBuffers(DeviceInterchange->AccessDevice(), DeviceInterchange->AccessCommandAllocator(), 1u, &Cmd);

            const auto* OutPairs = static_cast<const float*>(OutputBuf.HostAddress);
            for (uint32_t Idx = 0u; Idx < JunctionCount; ++Idx)
            {
                Junctions[Idx].CurrentUV = { OutPairs[Idx * 2u + 0u], OutPairs[Idx * 2u + 1u] };
            }
        }
        else
        {
            for (uint32_t Idx = 0u; Idx < JunctionCount; ++Idx)
            {
                const auto& Rec = Records[Idx];
                if ((Rec.PinnedOrSeamLock & 1u) != 0u)
                {
                    continue;
                }
                const float BlendU = Rec.NeighborAvgUV[0] * 0.45f + Rec.IsometricTargetUV[0] * 0.55f;
                const float BlendV = Rec.NeighborAvgUV[1] * 0.45f + Rec.IsometricTargetUV[1] * 0.55f;
                const float T      = std::clamp(StepScale * Rec.DampingWeight, 0.0f, 1.0f);
                Junctions[Idx].CurrentUV.U += (BlendU - Junctions[Idx].CurrentUV.U) * T;
                Junctions[Idx].CurrentUV.V += (BlendV - Junctions[Idx].CurrentUV.V) * T;
            }
        }
    }

    if (UseGPU)
    {
        DeviceInterchange->ReleaseBuffer(ParamBuf);
        DeviceInterchange->ReleaseBuffer(InputBuf);
        DeviceInterchange->ReleaseBuffer(OutputBuf);
    }

    // Finalize relaxed coordinates with strict positive CCW signed-area verification (zero flips guaranteed)
    for (const auto& Junction : Junctions)
    {
        std::vector<Mathematics::UVAxes> PreviousUVs;
        PreviousUVs.reserve(Junction.IncidentEdges.size());
        for (const uint32_t EdgeIdx : Junction.IncidentEdges)
        {
            PreviousUVs.push_back(Network.Edges[EdgeIdx].ParameterUV);
            Network.Edges[EdgeIdx].ParameterUV = Junction.CurrentUV;
        }

        bool InvertedAnyFace = false;
        for (const uint32_t EdgeIdx : Junction.IncidentEdges)
        {
            if (Network.EvaluateFaceSignedAreaUV(Network.Edges[EdgeIdx].EnclosingFaceIndex) <= 1.0e-7f)
            {
                InvertedAnyFace = true;
                break;
            }
        }
        if (InvertedAnyFace)
        {
            for (std::size_t K = 0u; K < Junction.IncidentEdges.size(); ++K)
            {
                Network.Edges[Junction.IncidentEdges[K]].ParameterUV = PreviousUVs[K];
            }
        }
    }

    ++Network.UVRevision;

    Report.WeldedCornersRelaxed = JunctionCount;
    Report.ExecutedIterations       = ClampedIterations;
    Report.ExecutedOnVulkanGPU  = UseGPU;
    Report.ElapsedMilliseconds  = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - StartClock).count();
    return Report;
}

} // namespace Frontier::Workspaces
