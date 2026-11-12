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

#include <array>
#include <cstdio>
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

    if (Passed)
    {
        std::puts("[UVBoundaryValidation] SUCCEEDED");
        return 0;
    }
    return 1;
}
