/*============================================================================================================================================
                                                        UVBOUNDARYVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Boundary validation for hostile interchange payloads and high-chart UV packing inputs

#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/Configuration/WorkspaceConfiguration.h"
#include "Engine/GeometrySpace/Network/FacePartitionRegister.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Interchange/GeometryInterchange.h"
#include "Engine/Workspaces/Revision/OperationRecorder.h"
#include "Engine/Workspaces/Revision/RevisionTracker.h"
#include "Engine/Workspaces/UV/Solvers/ConcaveSkylinePacker.h"
#include "Engine/Workspaces/UV/Solvers/ConformalUnwrapSolver.h"
#include "Engine/Workspaces/UV/Solvers/ProgressiveStretchRelaxer.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace
{

bool Verify(bool Condition, const char* Description)
{
    if (!Condition)
    {
        std::fprintf(stderr, "[UVBoundaryValidation] FAILED: %s\n", Description);
    }
    return Condition;
}

uint32_t AppendPackingTestTriangle(
    Frontier::GeometrySpace::PolygonNetwork& Network,
    float U,
    float V,
    uint16_t LayerOrdinal = 0u)
{
    const float X = static_cast<float>(Network.Faces.size()) * 2.0f;
    const std::array<uint32_t, 3u> Vertices {{
        Network.AppendVertex({ X, 0.0f, 0.0f }),
        Network.AppendVertex({ X + 1.0f, 0.0f, 0.0f }),
        Network.AppendVertex({ X, 1.0f, 0.0f }),
    }};
    const std::array<Frontier::Mathematics::UVAxes, 3u> UVs {{
        { U, V }, { U + 0.12f, V }, { U, V + 0.12f },
    }};
    return Network.AppendFace(Vertices, UVs, {}, LayerOrdinal);
}

std::array<float, 4u> FaceUVBounds(const Frontier::GeometrySpace::PolygonNetwork& Network, uint32_t FaceIndex)
{
    const auto& Face = Network.Faces[FaceIndex];
    float MinU = std::numeric_limits<float>::max();
    float MinV = std::numeric_limits<float>::max();
    float MaxU = std::numeric_limits<float>::lowest();
    float MaxV = std::numeric_limits<float>::lowest();
    uint32_t EdgeIndex = Face.BoundaryLoopEdgeIndex;
    for (uint16_t Corner = 0u; Corner < Face.CornerCount; ++Corner)
    {
        const auto& UV = Network.Edges[EdgeIndex].ParameterUV;
        MinU = std::min(MinU, UV.U);
        MinV = std::min(MinV, UV.V);
        MaxU = std::max(MaxU, UV.U);
        MaxV = std::max(MaxV, UV.V);
        EdgeIndex = Network.Edges[EdgeIndex].NextEdgeIndex;
    }
    return { MinU, MinV, MaxU, MaxV };
}

} // namespace

int main()
{
    using namespace Frontier;

    bool Passed = true;
    std::string Diagnostic {};
    GeometrySpace::ConstructedSampleAsset Asset {};

    const std::string TruncatedGltf = R"json({"asset":{"version":"2.0"},"buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,AAAA"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]})json";
    Passed &= Verify(
        !Interchange::GeometryInterchange::ImportGltf2JsonText(TruncatedGltf, {}, "Truncated payload", Asset, Diagnostic),
        "short embedded glTF buffer is rejected before reading accessor data");
    const std::string HugeAccessorGltf = R"json({"buffers":[{"byteLength":3,"uri":"data:application/octet-stream;base64,AAAA"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":3}],"accessors":[{"bufferView":0,"componentType":5126,"count":2147483647,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]})json";
    Passed &= Verify(
        !Interchange::GeometryInterchange::ImportGltf2JsonText(HugeAccessorGltf, {}, "Huge accessor", Asset, Diagnostic),
        "oversized glTF accessor count is rejected with wide span arithmetic");

    Diagnostic.clear();
    const std::string NonFiniteObj =
        "v nan 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    Passed &= Verify(
        !Interchange::GeometryInterchange::ImportWavefrontObjText(NonFiniteObj, "Non-finite positions", Asset, Diagnostic),
        "OBJ NaN vertex cannot enter a polygon network");
    const std::string OverflowObj =
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 999999999999999999999999999999 2 3\n";
    Passed &= Verify(
        !Interchange::GeometryInterchange::ImportWavefrontObjText(OverflowObj, "Overflow index", Asset, Diagnostic),
        "overflowing OBJ vertex indices are rejected instead of invoking signed overflow");

    GeometrySpace::PolygonNetwork EmptyNetwork {};
    Passed &= Verify(
        Interchange::GeometryInterchange::ExportGltf2JsonText(EmptyNetwork).empty(),
        "empty network does not export bogus extreme bounds");
    Workspaces::ProgressiveStretchRelaxer Relaxer {};
    const auto EmptyRelax = Relaxer.RelaxStretch(EmptyNetwork, 8u);
    const auto EmptyUnwrap = Workspaces::ConformalUnwrapSolver::FlattenIslandsConformally(EmptyNetwork, nullptr, 8u);
    Passed &= Verify(
        EmptyRelax.WeldedCornersRelaxed == 0u && EmptyUnwrap.IslandsFlattened == 0u,
        "empty UV networks are safe no-ops for relaxation and conformal unwrap");

    bool MalformedContainersRejected = true;
    for (std::size_t ByteCount = 1u; ByteCount <= 96u; ++ByteCount)
    {
        std::vector<std::byte> Malformed(ByteCount);
        for (std::size_t ByteIdx = 0u; ByteIdx < ByteCount; ++ByteIdx)
        {
            Malformed[ByteIdx] = static_cast<std::byte>((ByteIdx * 73u + ByteCount * 19u) & 0xFFu);
        }
        Codecs::SpaceContainerPayload Parsed {};
        Diagnostic.clear();
        if (Codecs::SpaceContainerDecoder::DecodeFromBytes(Malformed, Parsed, Diagnostic))
        {
            MalformedContainersRejected = false;
            break;
        }
    }
    Passed &= Verify(MalformedContainersRejected, "96 malformed short .Space byte streams are rejected cleanly");

    Workspaces::RevisionTracker History(4096u);
    const std::array<std::byte, 512u> RevisionPayload {};
    bool RevisionStepsSucceeded = true;
    for (uint64_t OperationIdx = 1u; OperationIdx <= 80u; ++OperationIdx)
    {
        const auto Operation = Workspaces::OperationRecorder::ConstructUVTileTransform(
            0, 0, static_cast<double>(OperationIdx) * 0.001, 0.0, 1.0, 0.0, 1024.0, "BoundaryIteration");
        (void)History.RecordParametricOperation(Operation, OperationIdx, RevisionPayload);
        if (History.QueryBytesUsed() > History.QueryDocumentBudgetBytes())
        {
            RevisionStepsSucceeded = false;
            break;
        }
    }
    Passed &= Verify(RevisionStepsSucceeded, "revision-history churn never exceeds its configured 4 KiB budget");

    constexpr uint32_t IslandCount = 2001u;
    GeometrySpace::PolygonNetwork Network {};
    Network.ReserveStorage(IslandCount * 3u, IslandCount * 3u, IslandCount);
    const std::array<Mathematics::UVAxes, 3u> TriangleUV {{
        { 0.0f, 0.0f }, { 0.02f, 0.0f }, { 0.0f, 0.02f },
    }};
    for (uint32_t Island = 0u; Island < IslandCount; ++Island)
    {
        const float X = static_cast<float>(Island) * 2.0f;
        const std::array<uint32_t, 3u> TriangleVerts {{
            Network.AppendVertex({ X, 0.0f, 0.0f }),
            Network.AppendVertex({ X + 1.0f, 0.0f, 0.0f }),
            Network.AppendVertex({ X, 1.0f, 0.0f }),
        }};
        (void)Network.AppendFace(TriangleVerts, TriangleUV);
    }
    Network.LinkOppositeEdges(true);
    Network.RecomputeNormals();

    GeometrySpace::FacePartitionRegister Layers {};
    Configuration::UVWorkspacePreferences Preferences {};
    Preferences.DefaultTileResolutionPx = 4096u;
    Preferences.IslandMarginPixels = UINT32_MAX;
    Workspaces::ConcaveSkylinePacker Packer {};
    const Workspaces::ConcavePackReport Pack = Packer.PackConcaveSkyline(Network, Layers, Preferences, true);
    Passed &= Verify(
        Pack.IslandsPacked == IslandCount,
        "2,001 independent UV charts pack without gutter arithmetic overflow or lost islands");

    Configuration::UVWorkspacePreferences ScopePreferences {};
    ScopePreferences.IslandMarginPixels = 8u;
    ScopePreferences.RotationStepDegrees = 90u;

    GeometrySpace::PolygonNetwork ActiveTileNetwork {};
    (void)AppendPackingTestTriangle(ActiveTileNetwork, 0.10f, 0.10f);
    (void)AppendPackingTestTriangle(ActiveTileNetwork, 0.55f, 0.55f);
    ActiveTileNetwork.LinkOppositeEdges(true);
    ActiveTileNetwork.RecomputeNormals();
    GeometrySpace::FacePartitionRegister ActiveTileLayers {};
    const auto ActiveTilePack = Packer.PackConcaveSkyline(
        ActiveTileNetwork,
        ActiveTileLayers,
        ScopePreferences,
        false,
        nullptr,
        1003u,
        Configuration::UVPackingScopeMode::ActiveUDIMTile);
    Passed &= Verify(
        ActiveTilePack.IslandsPacked == 2u &&
            ActiveTileNetwork.DeriveFaceUDIMNumber(0u) == 1003u &&
            ActiveTileNetwork.DeriveFaceUDIMNumber(1u) == 1003u,
        "Active UDIM scope relocates every packed island to the chosen tile");

    GeometrySpace::PolygonNetwork AssignedTileNetwork {};
    GeometrySpace::FacePartitionRegister AssignedTileLayers {};
    const uint16_t Layer1001 = AssignedTileLayers.ConstructLayer("Tile One", { 0.8f, 0.7f, 0.4f }, 50.0f, 1001u);
    const uint16_t Layer1003 = AssignedTileLayers.ConstructLayer("Tile Three", { 0.4f, 0.7f, 0.9f }, 50.0f, 1003u);
    (void)AppendPackingTestTriangle(AssignedTileNetwork, 0.10f, 0.10f, Layer1001);
    (void)AppendPackingTestTriangle(AssignedTileNetwork, 0.30f, 0.10f, Layer1003);
    AssignedTileNetwork.LinkOppositeEdges(true);
    AssignedTileNetwork.RecomputeNormals();
    const auto PerAssignedPack = Packer.PackConcaveSkyline(
        AssignedTileNetwork,
        AssignedTileLayers,
        ScopePreferences,
        false,
        nullptr,
        1001u,
        Configuration::UVPackingScopeMode::PerAssignedUDIMTile);
    Passed &= Verify(
        PerAssignedPack.IslandsPacked == 2u &&
            AssignedTileNetwork.DeriveFaceUDIMNumber(0u) == 1001u &&
            AssignedTileNetwork.DeriveFaceUDIMNumber(1u) == 1003u,
        "Per-assigned UDIM scope routes each priority layer to its pinned tile");

    GeometrySpace::PolygonNetwork SelectedOnlyNetwork {};
    (void)AppendPackingTestTriangle(SelectedOnlyNetwork, 0.10f, 0.10f);
    (void)AppendPackingTestTriangle(SelectedOnlyNetwork, 0.55f, 0.55f);
    SelectedOnlyNetwork.LinkOppositeEdges(true);
    SelectedOnlyNetwork.RecomputeNormals();
    const auto StationaryBoundsBefore = FaceUVBounds(SelectedOnlyNetwork, 1u);
    const std::array<uint32_t, 1u> SelectedFaceIndices {{ 0u }};
    GeometrySpace::FacePartitionRegister SelectedOnlyLayers {};
    const auto SelectedOnlyPack = Packer.PackConcaveSkyline(
        SelectedOnlyNetwork,
        SelectedOnlyLayers,
        ScopePreferences,
        false,
        nullptr,
        1001u,
        Configuration::UVPackingScopeMode::SelectedIslandsOnly,
        SelectedFaceIndices);
    const auto StationaryBoundsAfter = FaceUVBounds(SelectedOnlyNetwork, 1u);
    const auto SelectedBoundsAfter = FaceUVBounds(SelectedOnlyNetwork, 0u);
    const bool StationaryUnchanged = StationaryBoundsBefore == StationaryBoundsAfter;
    const bool SelectedDoesNotOverlapStationary =
        SelectedBoundsAfter[2] <= StationaryBoundsAfter[0] || StationaryBoundsAfter[2] <= SelectedBoundsAfter[0] ||
        SelectedBoundsAfter[3] <= StationaryBoundsAfter[1] || StationaryBoundsAfter[3] <= SelectedBoundsAfter[1];
    Passed &= Verify(
        SelectedOnlyPack.IslandsPacked == 1u && StationaryUnchanged && SelectedDoesNotOverlapStationary,
        "Selected-only scope packs selected islands while preserving non-selected UVs without AABB overlap");

    if (Passed)
    {
        std::puts("[UVBoundaryValidation] SUCCEEDED");
        return 0;
    }
    return 1;
}
