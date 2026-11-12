/*============================================================================================================================================
                                                         CONCAVESKYLINEPACKER.CPP
============================================================================================================================================*/
// 🧩 Implementation of ConcaveSkylinePacker using ConcaveSkylineCompute.comp.spv + ExecutionChannels parallel island profiles

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/Solvers/ConcaveSkylinePacker.h"

#include "Engine/Constants/MathConstants.h"
#include "Engine/Workspaces/UV/Solvers/SeamCutGenerator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace Frontier::Workspaces
{

namespace
{

constexpr uint32_t SkylineGridColumns = 256u;

struct GPUSkylineCandidateQuery
{
    uint32_t IslandFootprintCols = 0u;
    uint32_t RotationIndex       = 0u;
    uint32_t CandidateColumnX    = 0u;
    uint32_t GridColumns         = SkylineGridColumns;
};

struct GPUSkylineScoreResult
{
    float    PlacementScore = 1.0e9f;
    uint32_t LandingFloorY   = SkylineGridColumns;
    uint32_t CandidateIndex = 0u;
    uint32_t PlacementValid      = 0u;
};

struct GPUSkylineDispatchParams
{
    uint32_t CandidateCount      = 0u;
    uint32_t TileCeilingRows     = SkylineGridColumns;
    uint32_t IslandProfileOffset = 0u;
    uint32_t ReservedPad         = 0u;
};

struct RotatedCandidateProfile
{
    float                         AngleRadians   = 0.0f;
    Mathematics::UVAxes           MinRotated     {};
    Mathematics::UVAxes           MaxRotated     {};
    uint32_t                      FootprintCols  = 1u;
    uint32_t                      FootprintRows  = 1u;
    std::vector<std::array<int32_t, 2u>> ColSpans {}; // [SkylineGridColumns] { BottomRow, TopRow }
};

struct PreparedPackIsland
{
    UVIslandPartition                    Source      {};
    float                                ScaleFactor = 1.0f;
    Mathematics::UVAxes                  CentroidUV  {};
    std::vector<RotatedCandidateProfile> Rotations   {};
};

RotatedCandidateProfile BuildRotatedSpanProfile(
    const GeometrySpace::PolygonNetwork& Network,
    const UVIslandPartition&             Island,
    const Mathematics::UVAxes&           CentroidUV,
    float                                ScaleFactor,
    float                                AngleRadians,
    uint32_t                             GutterCols)
{
    RotatedCandidateProfile Prof {};
    Prof.AngleRadians = AngleRadians;
    Prof.ColSpans.assign(SkylineGridColumns, { 0, 0 });

    const float CosA = std::cos(AngleRadians);
    const float SinA = std::sin(AngleRadians);

    Prof.MinRotated = {  1.0e9f,  1.0e9f };
    Prof.MaxRotated = { -1.0e9f, -1.0e9f };

    auto TransformUV = [&](const Mathematics::UVAxes& P) noexcept -> Mathematics::UVAxes
    {
        const float dU = (P.U - CentroidUV.U) * ScaleFactor;
        const float dV = (P.V - CentroidUV.V) * ScaleFactor;
        return { dU * CosA - dV * SinA, dU * SinA + dV * CosA };
    };

    for (const uint32_t EdgeIdx : Island.CornerEdgeIndices)
    {
        const auto R = TransformUV(Network.Edges[EdgeIdx].ParameterUV);
        Prof.MinRotated.U = std::min(Prof.MinRotated.U, R.U);
        Prof.MinRotated.V = std::min(Prof.MinRotated.V, R.V);
        Prof.MaxRotated.U = std::max(Prof.MaxRotated.U, R.U);
        Prof.MaxRotated.V = std::max(Prof.MaxRotated.V, R.V);
    }

    const float SpanU = std::max(1.0e-4f, Prof.MaxRotated.U - Prof.MinRotated.U);
    const float SpanV = std::max(1.0e-4f, Prof.MaxRotated.V - Prof.MinRotated.V);

    const uint32_t RawCols = std::clamp(
        static_cast<uint32_t>(std::ceil(SpanU * static_cast<float>(SkylineGridColumns))),
        1u,
        SkylineGridColumns - 2u * GutterCols);
    const uint32_t RawRows = std::clamp(
        static_cast<uint32_t>(std::ceil(SpanV * static_cast<float>(SkylineGridColumns))),
        1u,
        SkylineGridColumns - 2u * GutterCols);

    Prof.FootprintCols = std::min(SkylineGridColumns, RawCols + 2u * GutterCols);
    Prof.FootprintRows = std::min(SkylineGridColumns, RawRows + 2u * GutterCols);

    // Initialize empty columns with the empty-span marker [Max, 0] and rasterize actual concave polygon face spans!
    std::vector<int32_t> ColBottom(Prof.FootprintCols, static_cast<int32_t>(Prof.FootprintRows));
    std::vector<int32_t> ColTop(Prof.FootprintCols, 0);

    for (const uint32_t FaceIdx : Island.FaceIndices)
    {
        const auto& Face = Network.Faces[FaceIdx];
        float FMinU =  1.0e9f, FMinV =  1.0e9f;
        float FMaxU = -1.0e9f, FMaxV = -1.0e9f;
        uint32_t EdgeCur = Face.BoundaryLoopEdgeIndex;
        for (uint16_t Corner = 0u; Corner < Face.CornerCount; ++Corner)
        {
            const auto R = TransformUV(Network.Edges[EdgeCur].ParameterUV);
            FMinU = std::min(FMinU, R.U - Prof.MinRotated.U);
            FMinV = std::min(FMinV, R.V - Prof.MinRotated.V);
            FMaxU = std::max(FMaxU, R.U - Prof.MinRotated.U);
            FMaxV = std::max(FMaxV, R.V - Prof.MinRotated.V);
            EdgeCur = Network.Edges[EdgeCur].NextEdgeIndex;
        }

        const int32_t C0 = std::max(0, static_cast<int32_t>(std::floor(FMinU * static_cast<float>(SkylineGridColumns))) - 1);
        const int32_t C1 = std::min(static_cast<int32_t>(Prof.FootprintCols) - 1,
            static_cast<int32_t>(std::ceil(FMaxU * static_cast<float>(SkylineGridColumns)) + 2 * static_cast<int32_t>(GutterCols)) + 1);
        const int32_t R0 = std::max(0, static_cast<int32_t>(std::floor(FMinV * static_cast<float>(SkylineGridColumns))) - 1);
        const int32_t R1 = std::min(static_cast<int32_t>(Prof.FootprintRows),
            static_cast<int32_t>(std::ceil(FMaxV * static_cast<float>(SkylineGridColumns)) + 2 * static_cast<int32_t>(GutterCols)) + 1);

        for (int32_t Col = C0; Col <= C1; ++Col)
        {
            ColBottom[static_cast<std::size_t>(Col)] = std::min(ColBottom[static_cast<std::size_t>(Col)], R0);
            ColTop[static_cast<std::size_t>(Col)]    = std::max(ColTop[static_cast<std::size_t>(Col)], R1);
        }
    }

    for (uint32_t Col = 0u; Col < Prof.FootprintCols; ++Col)
    {
        if (ColBottom[Col] >= ColTop[Col])
        {
            Prof.ColSpans[Col] = { 0, static_cast<int32_t>(Prof.FootprintRows) };
        }
        else
        {
            Prof.ColSpans[Col] = { ColBottom[Col], ColTop[Col] };
        }
    }

    return Prof;
}

} // namespace

ConcaveSkylinePacker::~ConcaveSkylinePacker()
{
    TeardownGPUCompute();
}

bool ConcaveSkylinePacker::InitializeGPUCompute(
    GeometricRaster::VulkanInterchange* BoundDevice,
    const std::filesystem::path&        CompiledShaderDir)
{
    TeardownGPUCompute();
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return false;
    }
    this->DeviceInterchange = BoundDevice;
    if (!Encoder.InitializeEncoder(BoundDevice, 2048u))
    {
        this->DeviceInterchange = nullptr;
        return false;
    }

    const std::array<VkDescriptorType, 5u> Slots {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    SkylineProgramHandle = Encoder.RegisterProgram(CompiledShaderDir / "ConcaveSkylineCompute.comp.spv", Slots, "ComputeEntry");
    return SkylineProgramHandle != UINT32_MAX;
}

void ConcaveSkylinePacker::TeardownGPUCompute() noexcept
{
    Encoder.TeardownEncoder();
    SkylineProgramHandle = UINT32_MAX;
    DeviceInterchange            = nullptr;
}

ConcavePackReport ConcaveSkylinePacker::PackConcaveSkyline(
    GeometrySpace::PolygonNetwork&               Network,
    const GeometrySpace::FacePartitionRegister&  LayerRegistry,
    const Configuration::UVWorkspacePreferences& Preferences,
    bool                                         AllowMultiUDIMOverflow,
    Threading::ExecutionChannels*                OptionalChannels)
{
    const auto StartClock = std::chrono::steady_clock::now();
    ConcavePackReport Report {};

    std::vector<UVIslandPartition> Islands = SeamCutGenerator::PartitionConnectedUVIslands(Network);
    if (Islands.empty())
    {
        return Report;
    }

    // 1. Compute Priority-Layer target UV area budgets (PackingFillBudget across single or multi-UDIM)
    std::unordered_map<uint16_t, float> LayerArea3D;
    std::unordered_map<uint16_t, float> LayerAreaUV;
    for (const auto& Isl : Islands)
    {
        LayerArea3D[Isl.PriorityLayerOrdinal] += std::max(1.0e-6f, Isl.TotalArea3D);
        LayerAreaUV[Isl.PriorityLayerOrdinal] += std::max(1.0e-6f, Isl.TotalAreaUV);
    }

    const float TotalBudgetUV = AllowMultiUDIMOverflow ? 0.92f : 0.46f;
    const float RemainderPct  = std::max(2.0f, LayerRegistry.DeriveRemainderSharePercent());

    auto ResolveLayerShareFraction = [&](uint16_t LayerOrd) noexcept -> float
    {
        if (const auto* Entry = LayerRegistry.InspectLayer(LayerOrd))
        {
            return std::max(0.02f, Entry->TargetSharePercent * 0.01f);
        }
        return RemainderPct * 0.01f;
    };

    const float RequestedGutterCols = std::ceil(
        (static_cast<float>(Preferences.IslandMarginPixels) / static_cast<float>(std::max(256u, Preferences.DefaultTileResolutionPx))) *
        static_cast<float>(SkylineGridColumns) * 0.5f);
    const uint32_t GutterCols = static_cast<uint32_t>(std::clamp(RequestedGutterCols, 1.0f, 6.0f));

    std::vector<float> CandidateAngles { 0.0f };
    if (Preferences.RotationStepDegrees == 90u)
    {
        CandidateAngles.push_back(0.5f * Constants::PiFloat);
    }
    else if (Preferences.RotationStepDegrees == 45u)
    {
        CandidateAngles.push_back(0.25f * Constants::PiFloat);
        CandidateAngles.push_back(0.5f * Constants::PiFloat);
        CandidateAngles.push_back(0.75f * Constants::PiFloat);
    }

    // 2. Prepare rotated concave span profiles in parallel across ExecutionChannels
    std::vector<PreparedPackIsland> Prepared(Islands.size());
    auto PrepareIslandFn = [&](std::size_t Idx)
    {
        PreparedPackIsland Piece {};
        Piece.Source = Islands[Idx];
        Piece.CentroidUV = {
            0.5f * (Piece.Source.BoundsMinUV.U + Piece.Source.BoundsMaxUV.U),
            0.5f * (Piece.Source.BoundsMinUV.V + Piece.Source.BoundsMaxUV.V),
        };

        const float TargetLayerUV = TotalBudgetUV * ResolveLayerShareFraction(Piece.Source.PriorityLayerOrdinal);
        if (Preferences.EqualizeIntraLayerScale)
        {
            const float DesiredIslandUV = TargetLayerUV * (Piece.Source.TotalArea3D / LayerArea3D[Piece.Source.PriorityLayerOrdinal]);
            Piece.ScaleFactor = std::sqrt(DesiredIslandUV / std::max(1.0e-7f, Piece.Source.TotalAreaUV));
        }
        else
        {
            Piece.ScaleFactor = std::sqrt(TargetLayerUV / std::max(1.0e-7f, LayerAreaUV[Piece.Source.PriorityLayerOrdinal]));
        }
        Piece.ScaleFactor = std::clamp(Piece.ScaleFactor, 0.05f, 4.0f);

        for (const float Ang : CandidateAngles)
        {
            Piece.Rotations.push_back(BuildRotatedSpanProfile(Network, Piece.Source, Piece.CentroidUV, Piece.ScaleFactor, Ang, GutterCols));
        }
        Prepared[Idx] = std::move(Piece);
    };

    if (OptionalChannels != nullptr && OptionalChannels->QueryActive() && Islands.size() > 1u)
    {
        Threading::CompletionCounter Counter(static_cast<uint32_t>(Islands.size()));
        for (std::size_t Idx = 0u; Idx < Islands.size(); ++Idx)
        {
            Threading::ScheduledTaskUnit Unit {};
            Unit.PriorityTier     = Threading::TaskPriorityTier::WorkspaceAuthoring;
            Unit.TaskIdentifier   = static_cast<uint32_t>(3000u + Idx);
            Unit.CompletionTarget = &Counter;
            Unit.Callable         = [&PrepareIslandFn, Idx]() { PrepareIslandFn(Idx); };
            if (!OptionalChannels->EnqueueTask(std::move(Unit)))
            {
                PrepareIslandFn(Idx);
                Counter.Decrement();
            }
        }
        OptionalChannels->AssistUntilComplete(Counter);
    }
    else
    {
        for (std::size_t Idx = 0u; Idx < Islands.size(); ++Idx)
        {
            PrepareIslandFn(Idx);
        }
    }

    // Sort largest footprint first so smaller islands nestle inside concave pockets!
    std::sort(Prepared.begin(), Prepared.end(), [](const PreparedPackIsland& A, const PreparedPackIsland& B)
    {
        const uint32_t AreaA = A.Rotations.front().FootprintCols * A.Rotations.front().FootprintRows;
        const uint32_t AreaB = B.Rotations.front().FootprintCols * B.Rotations.front().FootprintRows;
        return AreaA > AreaB;
    });

    // 3. Allocate GPU skyline candidate scoring buffers if Vulkan is active
    const bool UseGPU = (DeviceInterchange != nullptr && DeviceInterchange->Ready() && SkylineProgramHandle != UINT32_MAX);
    const uint32_t MaxRotations  = static_cast<uint32_t>(CandidateAngles.size());
    const uint32_t MaxCandidates = MaxRotations * SkylineGridColumns;

    GeometricRaster::DeviceBufferResource ParamBuf {};
    GeometricRaster::DeviceBufferResource SkyBuf {};
    GeometricRaster::DeviceBufferResource SpanBuf {};
    GeometricRaster::DeviceBufferResource QueryBuf {};
    GeometricRaster::DeviceBufferResource ScoreBuf {};

    if (UseGPU)
    {
        ParamBuf = Encoder.AllocateHostWritableBuffer(sizeof(GPUSkylineDispatchParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        SkyBuf   = Encoder.AllocateHostWritableBuffer(sizeof(uint32_t) * SkylineGridColumns, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        SpanBuf  = Encoder.AllocateHostWritableBuffer(sizeof(int32_t) * 2u * SkylineGridColumns * MaxRotations, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        QueryBuf = Encoder.AllocateHostWritableBuffer(sizeof(GPUSkylineCandidateQuery) * MaxCandidates, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        ScoreBuf = Encoder.AllocateHostWritableBuffer(sizeof(GPUSkylineScoreResult) * MaxCandidates, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }

    struct UDIMTileSkyline
    {
        int32_t               TileU = 0;
        std::vector<uint32_t> Columns;
        UDIMTileSkyline(int32_t InU, uint32_t Margin) : TileU(InU), Columns(SkylineGridColumns, Margin) {}
    };
    std::vector<UDIMTileSkyline> Tiles;
    Tiles.emplace_back(0, GutterCols);

    struct FinalIslandPlacement
    {
        std::size_t IslandIdx   = 0u;
        uint32_t    TileIdx     = 0u;
        uint32_t    RotIdx      = 0u;
        uint32_t    ColX        = 0u;
        uint32_t    RowFloorY    = 0u;
    };
    std::vector<FinalIslandPlacement> Placements;
    Placements.reserve(Prepared.size());

    auto TryPlaceOnTile = [&](std::size_t IslIdx, UDIMTileSkyline& Tile, uint32_t& OutRot, uint32_t& OutCol, uint32_t& OutRowY) -> bool
    {
        const auto& Piece = Prepared[IslIdx];
        std::vector<GPUSkylineCandidateQuery> Queries;
        Queries.reserve(MaxCandidates);

        for (uint32_t RIdx = 0u; RIdx < Piece.Rotations.size(); ++RIdx)
        {
            const uint32_t FCols = Piece.Rotations[RIdx].FootprintCols;
            if (FCols + GutterCols > SkylineGridColumns)
            {
                continue;
            }
            for (uint32_t ColX = GutterCols; ColX + FCols + GutterCols <= SkylineGridColumns; ++ColX)
            {
                Queries.push_back({ FCols, RIdx, ColX, SkylineGridColumns });
            }
        }
        if (Queries.empty())
        {
            return false;
        }

        const uint32_t CeilingRows = SkylineGridColumns - GutterCols;
        float    BestScore = 1.0e8f;
        bool     Found     = false;

        if (UseGPU)
        {
            Encoder.ResetTransientDescriptors();
            const GPUSkylineDispatchParams Params { static_cast<uint32_t>(Queries.size()), CeilingRows, 0u, 0u };
            const std::array<GPUSkylineDispatchParams, 1u> PSpan {{ Params }};
            Encoder.UploadSpanToBuffer<GPUSkylineDispatchParams>(ParamBuf, PSpan);
            Encoder.UploadSpanToBuffer<uint32_t>(SkyBuf, Tile.Columns);

            std::vector<std::array<int32_t, 2u>> FlatSpans(SkylineGridColumns * Piece.Rotations.size(), { 0, 0 });
            for (std::size_t RIdx = 0u; RIdx < Piece.Rotations.size(); ++RIdx)
            {
                std::copy(
                    Piece.Rotations[RIdx].ColSpans.begin(),
                    Piece.Rotations[RIdx].ColSpans.end(),
                    FlatSpans.begin() + static_cast<std::ptrdiff_t>(RIdx * SkylineGridColumns));
            }
            Encoder.UploadSpanToBuffer<std::array<int32_t, 2u>>(SpanBuf, FlatSpans);
            Encoder.UploadSpanToBuffer<GPUSkylineCandidateQuery>(QueryBuf, Queries);

            VkCommandBuffer Cmd = DeviceInterchange->BeginSingleTimeCommands();
            const std::array<GeometricRaster::ComputeSlotDescriptor, 5u> Slots {{
                { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ParamBuf.BufferHandle, sizeof(GPUSkylineDispatchParams) },
                { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SkyBuf.BufferHandle, VK_WHOLE_SIZE },
                { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SpanBuf.BufferHandle, VK_WHOLE_SIZE },
                { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, QueryBuf.BufferHandle, VK_WHOLE_SIZE },
                { 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ScoreBuf.BufferHandle, VK_WHOLE_SIZE },
            }};
            (void)Encoder.EncodeComputeDispatch(Cmd, SkylineProgramHandle, Slots, (static_cast<uint32_t>(Queries.size()) + 63u) / 64u);
            const uint64_t Signal = DeviceInterchange->DispatchCommandBufferAsync(Cmd);
            (void)DeviceInterchange->AwaitTimelineTick(Signal);
            vkFreeCommandBuffers(DeviceInterchange->AccessDevice(), DeviceInterchange->AccessCommandAllocator(), 1u, &Cmd);

            const auto* Scores = static_cast<const GPUSkylineScoreResult*>(ScoreBuf.HostAddress);
            for (std::size_t QIdx = 0u; QIdx < Queries.size(); ++QIdx)
            {
                if (Scores[QIdx].PlacementValid != 0u && Scores[QIdx].PlacementScore < BestScore)
                {
                    BestScore = Scores[QIdx].PlacementScore;
                    OutRot    = Queries[QIdx].RotationIndex;
                    OutCol    = Queries[QIdx].CandidateColumnX;
                    OutRowY   = Scores[QIdx].LandingFloorY;
                    Found     = true;
                }
            }
        }
        else
        {
            for (const auto& Q : Queries)
            {
                const auto& Prof = Piece.Rotations[Q.RotationIndex];
                uint32_t FloorY = 0u, PeakY = 0u, Wasted = 0u;
                for (uint32_t C = 0u; C < Q.IslandFootprintCols; ++C)
                {
                    const int32_t Req = static_cast<int32_t>(Tile.Columns[Q.CandidateColumnX + C]) - Prof.ColSpans[C][0];
                    if (Req > static_cast<int32_t>(FloorY)) FloorY = static_cast<uint32_t>(Req);
                }
                for (uint32_t C = 0u; C < Q.IslandFootprintCols; ++C)
                {
                    const uint32_t SkyH = Tile.Columns[Q.CandidateColumnX + C];
                    const uint32_t Bot  = static_cast<uint32_t>(std::max(0, static_cast<int32_t>(FloorY) + Prof.ColSpans[C][0]));
                    const uint32_t Top  = static_cast<uint32_t>(std::max(0, static_cast<int32_t>(FloorY) + Prof.ColSpans[C][1]));
                    if (Bot > SkyH) Wasted += (Bot - SkyH);
                    PeakY = std::max(PeakY, Top);
                }
                if (PeakY <= CeilingRows)
                {
                    const float Score = static_cast<float>(PeakY) + static_cast<float>(Wasted) * 0.28f + static_cast<float>(Q.CandidateColumnX) * 0.005f;
                    if (Score < BestScore)
                    {
                        BestScore = Score;
                        OutRot    = Q.RotationIndex;
                        OutCol    = Q.CandidateColumnX;
                        OutRowY   = FloorY;
                        Found     = true;
                    }
                }
            }
        }

        if (Found)
        {
            const auto& Prof = Piece.Rotations[OutRot];
            for (uint32_t C = 0u; C < Prof.FootprintCols; ++C)
            {
                const uint32_t NewH = static_cast<uint32_t>(std::max(0, static_cast<int32_t>(OutRowY) + Prof.ColSpans[C][1]));
                Tile.Columns[OutCol + C] = std::max(Tile.Columns[OutCol + C], NewH);
            }
        }
        return Found;
    };

    // Place every island; if single-UDIM overflows ceiling, uniformly downscale by 0.88 and retry cleanly!
    bool AllFit = false;
    for (uint32_t Attempt = 0u; Attempt < 16u && !AllFit; ++Attempt)
    {
        Tiles.clear();
        Tiles.emplace_back(0, GutterCols);
        Placements.clear();
        AllFit = true;

        for (std::size_t IslIdx = 0u; IslIdx < Prepared.size(); ++IslIdx)
        {
            uint32_t ChosenTile = 0u, ChosenRot = 0u, ChosenCol = 0u, ChosenRow = 0u;
            bool Placed = false;
            for (uint32_t TIdx = 0u; TIdx < Tiles.size(); ++TIdx)
            {
                if (TryPlaceOnTile(IslIdx, Tiles[TIdx], ChosenRot, ChosenCol, ChosenRow))
                {
                    ChosenTile = TIdx;
                    Placed     = true;
                    break;
                }
            }

            if (!Placed && AllowMultiUDIMOverflow && Tiles.size() < 10u)
            {
                const int32_t NextTileU = static_cast<int32_t>(Tiles.size());
                Tiles.emplace_back(NextTileU, GutterCols);
                ChosenTile = static_cast<uint32_t>(Tiles.size() - 1u);
                Placed     = TryPlaceOnTile(IslIdx, Tiles.back(), ChosenRot, ChosenCol, ChosenRow);
            }

            if (!Placed)
            {
                AllFit = false;
                for (std::size_t J = 0u; J < Prepared.size(); ++J)
                {
                    Prepared[J].ScaleFactor *= 0.86f;
                    Prepared[J].Rotations.clear();
                    for (const float Ang : CandidateAngles)
                    {
                        Prepared[J].Rotations.push_back(BuildRotatedSpanProfile(
                            Network, Prepared[J].Source, Prepared[J].CentroidUV, Prepared[J].ScaleFactor, Ang, GutterCols));
                    }
                }
                break;
            }

            Placements.push_back({ IslIdx, ChosenTile, ChosenRot, ChosenCol, ChosenRow });
        }
    }

    if (UseGPU)
    {
        DeviceInterchange->ReleaseBuffer(ParamBuf);
        DeviceInterchange->ReleaseBuffer(SkyBuf);
        DeviceInterchange->ReleaseBuffer(SpanBuf);
        DeviceInterchange->ReleaseBuffer(QueryBuf);
        DeviceInterchange->ReleaseBuffer(ScoreBuf);
    }

    if (!AllFit)
    {
        Report.IslandsPacked = 0u;
        Report.UDIMTilesOccupied = 0u;
        Report.EvaluatedOnVulkanGPU = UseGPU;
        Report.ElapsedMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - StartClock).count();
        return Report;
    }

    // 4. Finalize placed island transforms into PolygonNetwork::Edges[e].ParameterUV
    const float InvGrid = 1.0f / static_cast<float>(SkylineGridColumns);
    float PrimaryUVArea = 0.0f;

    for (const auto& P : Placements)
    {
        const auto& Piece = Prepared[P.IslandIdx];
        const auto& Prof = Piece.Rotations[P.RotIdx];
        const float CosA = std::cos(Prof.AngleRadians);
        const float SinA = std::sin(Prof.AngleRadians);
        const float DestMinU = static_cast<float>(Tiles[P.TileIdx].TileU) + static_cast<float>(P.ColX + GutterCols) * InvGrid;
        const float DestMinV = static_cast<float>(P.RowFloorY + GutterCols) * InvGrid;

        for (const uint32_t EdgeIdx : Piece.Source.CornerEdgeIndices)
        {
            const auto& Orig = Network.Edges[EdgeIdx].ParameterUV;
            const float dU   = (Orig.U - Piece.CentroidUV.U) * Piece.ScaleFactor;
            const float dV   = (Orig.V - Piece.CentroidUV.V) * Piece.ScaleFactor;
            const float RotU = (dU * CosA - dV * SinA) - Prof.MinRotated.U;
            const float RotV = (dU * SinA + dV * CosA) - Prof.MinRotated.V;
            Network.Edges[EdgeIdx].ParameterUV = { DestMinU + RotU, DestMinV + RotV };
        }

        if (P.TileIdx == 0u)
        {
            for (const uint32_t FaceIdx : Piece.Source.FaceIndices)
            {
                PrimaryUVArea += std::max(0.0f, Network.EvaluateFaceSignedAreaUV(FaceIdx));
            }
        }
    }

    ++Network.UVRevision;

    Report.IslandsPacked         = static_cast<uint32_t>(Placements.size());
    Report.UDIMTilesOccupied     = static_cast<uint32_t>(Tiles.size());
    Report.PrimaryTileEfficiency = std::clamp(PrimaryUVArea, 0.0f, 1.0f);
    Report.EvaluatedOnVulkanGPU  = UseGPU;
    for (const auto& T : Tiles)
    {
        Report.ActiveUDIMNumbers.push_back(static_cast<uint32_t>(1001 + T.TileU));
    }
    Report.ElapsedMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - StartClock).count();
    return Report;
}

} // namespace Frontier::Workspaces
