/*============================================================================================================================================
                                                      WORKSPACEREVISIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation suite for Two-Tier .Space Codecs, 215/750 MiB Parametric Revision, Priority-Budgeted ExecutionChannels, & WorkspaceThread

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ColourCodec.h"
#include "Engine/Codecs/FontCodec.h"
#include "Engine/Codecs/ImageCodec.h"
#include "Engine/Codecs/MaterialCodec.h"
#include "Engine/Codecs/ProjectCodec.h"
#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/Codecs/TextureCodec.h"
#include "Engine/Codecs/UVCodec.h"
#include "Engine/Codecs/VectorCodec.h"
#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Components/Docking/WorkspaceTabStrip.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Constants/EngineConstants.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/Constants/PhysicsConstants.h"
#include "Engine/Constants/TimeConstants.h"
#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/Threading/Sync/AtomicQueue.h"
#include "Engine/Threading/Sync/CompletionCounter.h"
#include "Engine/Threading/Sync/ThreadProgressionTracker.h"
#include "Engine/Threading/Tasks/ExecutionChannels.h"
#include "Engine/Threading/Tasks/TaskChannel.h"
#include "Engine/Threading/Tasks/TaskScheduler.h"
#include "Engine/Threading/Threads/Workspace/WorkspaceThread.h"
#include "Engine/Workspaces/Revision/RevisionExtension.h"
#include "Engine/Workspaces/Revision/RevisionTracker.h"
#include "Engine/Workspaces/WorkspaceExtension.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 VERIFICATION UTILITIES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool VerifyCondition(bool ConditionMet, const char* CheckDescription) noexcept
{
    if (!ConditionMet)
    {
        std::fprintf(stderr, "[WorkspaceRevisionValidation] FAILED: %s\n", CheckDescription);
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                              SUITE 1: TWO-TIER HYBRID .SPACE CODECS & 64-BYTE ALIGNMENT
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool ValidateSpaceContainerAndDomainCodecs(const std::filesystem::path& ScratchDir)
{
    using namespace Frontier::Codecs;
    using namespace Frontier::Constants;

    std::filesystem::create_directories(ScratchDir);
    std::string Diagnostic;

    // 1. Verify EngineConstants defaults (215 MiB per-document, 750 MiB global, 64B alignment).
    if (!VerifyCondition(ContainerBinaryAlignmentBytes == 64u, "ContainerBinaryAlignmentBytes == 64B") ||
        !VerifyCondition(DefaultDocumentRevisionBudgetBytes == 215ull * 1024ull * 1024ull, "215 MiB per-document ceiling") ||
        !VerifyCondition(DefaultGlobalRevisionBudgetBytes == 750ull * 1024ull * 1024ull, "750 MiB global ceiling") ||
        !VerifyCondition(std::abs(StandardGravity - 9.80665) < Epsilon, "PhysicsConstants & MathConstants precision"))
    {
        return false;
    }

    // 2. Verify Option 2A rejection of .toml, .UVChart, and .Surface.
    SpaceExtensionCategory RejectedCat{};
    std::string            CanonicalOut;
    if (!VerifyCondition(
            !SpaceContainerDecoder::ResolveExtensionCaseInsensitive(".toml", RejectedCat, CanonicalOut, Diagnostic),
            "Rejects raw .toml extension") ||
        !VerifyCondition(
            !SpaceContainerDecoder::ResolveExtensionCaseInsensitive(".UVChart", RejectedCat, CanonicalOut, Diagnostic),
            "Rejects prohibited .UVChart extension") ||
        !VerifyCondition(
            !SpaceContainerDecoder::ResolveExtensionCaseInsensitive(".Surface", RejectedCat, CanonicalOut, Diagnostic),
            "Rejects prohibited .Surface extension") ||
        !VerifyCondition(
            !SpaceContainerEncoder::NormalizeSubFormatTag(".uvchart", CanonicalOut, Diagnostic),
            "Rejects prohibited .uvchart sub-format tag") ||
        !VerifyCondition(
            !SpaceContainerEncoder::NormalizeSubFormatTag(".surface", CanonicalOut, Diagnostic),
            "Rejects prohibited .surface sub-format tag"))
    {
        return false;
    }

    // 3. Verify Option 1A + Option 2A case-insensitive recognition & canonical PascalCase normalization.
    SpaceExtensionCategory ResolvedUV{};
    SpaceExtensionCategory ResolvedPigment{};
    if (!VerifyCondition(
            SpaceContainerDecoder::ResolveExtensionCaseInsensitive(".uvspace", ResolvedUV, CanonicalOut, Diagnostic) &&
                ResolvedUV == SpaceExtensionCategory::UVSpace && CanonicalOut == ".UVSpace",
            "Resolves lowercase .uvspace -> canonical .UVSpace") ||
        !VerifyCondition(
            SpaceContainerDecoder::ResolveExtensionCaseInsensitive(".PIGMENT", ResolvedPigment, CanonicalOut, Diagnostic) &&
                ResolvedPigment == SpaceExtensionCategory::Pigment && CanonicalOut == ".Pigment",
            "Resolves uppercase .PIGMENT -> Option 1A canonical .Pigment") ||
        !VerifyCondition(
            SpaceContainerEncoder::NormalizeSubFormatTag(".uvtile", CanonicalOut, Diagnostic) && CanonicalOut == ".UVTile",
            "Normalizes .uvtile -> canonical .UVTile"))
    {
        return false;
    }

    // 4. Round-trip .UVSpace (.UDIM + .UVTile, 64B-aligned coordinates, embedded .Revision) + corruption check.
    UVSpaceDocumentModel UVSource{};
    UVSource.DocumentTitle        = "TurbineHousingUV";
    UVSource.GridColumns          = 10u;
    UVSource.ReferenceScaleMeters = 2.5;
    UVSource.Tiles.push_back({ 0, 0, UVEncoder::ComputeUDIMNumber(0, 0), 1024.0, 8.0 });
    UVSource.Tiles.push_back({ 1, 0, UVEncoder::ComputeUDIMNumber(1, 0), 1024.0, 8.0 });
    UVSource.Tiles.push_back({ 1, 1, UVEncoder::ComputeUDIMNumber(1, 1), 512.0,  4.0 });
    UVSource.ParameterizedUVPairs = { 0.125f, 0.250f, 0.875f, 0.250f, 0.875f, 0.750f, 0.125f, 0.750f };

    Frontier::Workspaces::RevisionTracker UVRevision;
    (void)UVRevision.RecordParametricOperation(
        Frontier::Workspaces::OperationRecorder::ConstructUVTileTransform(0, 0, 0.05, 0.10, 1.0, 0.0, 1024.0, "PackUDIM1001"),
        1ull);

    const std::filesystem::path LowercaseUVPath = ScratchDir / "turbine_housing.uvspace";
    if (!VerifyCondition(UVEncoder::EncodeToFile(LowercaseUVPath, UVSource, Diagnostic, &UVRevision), "UVEncoder writes .uvspace"))
    {
        return false;
    }

    SpaceContainerPayload RawUVPayload{};
    if (!VerifyCondition(SpaceContainerDecoder::DecodeFromFile(LowercaseUVPath, RawUVPayload, Diagnostic), "DecodeFromFile .uvspace") ||
        !VerifyCondition((RawUVPayload.Tier2StartOffset % 64u) == 0u, "Tier2StartOffset is 64-byte aligned") ||
        !VerifyCondition(RawUVPayload.BinarySections.size() == 3u, "3 binary sections (.UVTile + .Baseline + .Revision)"))
    {
        return false;
    }
    for (const BinarySectionBlock& Section : RawUVPayload.BinarySections)
    {
        if (!VerifyCondition((Section.DecodedAbsoluteByteOffset % 64u) == 0u, "Every Tier 2 binary section is 64-byte aligned"))
        {
            return false;
        }
    }

    UVSpaceDocumentModel                  UVDecoded{};
    Frontier::Workspaces::RevisionTracker UVDecodedRevision;
    if (!VerifyCondition(UVDecoder::ExtractModel(RawUVPayload, UVDecoded, Diagnostic, &UVDecodedRevision), "UVDecoder::ExtractModel") ||
        !VerifyCondition(UVDecoded.Tiles.size() == 3u && UVDecoded.Tiles[2].UDIMNumber == 1012u, "UDIM 1012 reconstructed") ||
        !VerifyCondition(UVDecoded.ParameterizedUVPairs.size() == 8u, "64B-aligned UV coordinate stream preserved") ||
        !VerifyCondition(UVDecodedRevision.QueryLiveStepCount() == 2u, "Embedded .Revision restored"))
    {
        return false;
    }

    // Flip 1 bit inside the first 64B-aligned Tier 2 binary payload and verify FNV-1a 64-bit checksum catches corruption.
    std::vector<std::byte> EncodedUVBytes;
    if (!VerifyCondition(SpaceContainerEncoder::EncodeToBytes(RawUVPayload, EncodedUVBytes, Diagnostic), "EncodeToBytes succeeds"))
    {
        return false;
    }
    EncodedUVBytes[RawUVPayload.BinarySections[0].DecodedAbsoluteByteOffset] ^= std::byte{ 0x01 };
    SpaceContainerPayload CorruptedCheck{};
    if (!VerifyCondition(
            !SpaceContainerDecoder::DecodeFromBytes(EncodedUVBytes, CorruptedCheck, Diagnostic),
            "64-bit FNV-1a checksum detects single-bit corruption"))
    {
        return false;
    }

    // 5. Round-trip remaining workspace codecs (.ProjectSpace, .ColourSpace, .FontSpace, .ImageSpace, .TextureSpace, .MaterialSpace, .VectorSpace).
    ProjectSpaceDocumentModel ProjectModel{};
    ProjectModel.ProjectTitle     = "FrontierDemo";
    ProjectModel.StartupSpacePath = "Turbine.UVSpace";
    ProjectModel.DependencyLocks.push_back({ "Frontier.StandardMaterials", "1.0.0" });
    ProjectSpaceDocumentModel ProjectDecoded{};
    if (!VerifyCondition(ProjectEncoder::EncodeToFile(ScratchDir / "Demo.ProjectSpace", ProjectModel, Diagnostic), "Encode .ProjectSpace") ||
        !VerifyCondition(ProjectDecoder::DecodeFromFile(ScratchDir / "Demo.ProjectSpace", ProjectDecoded, Diagnostic), "Decode .ProjectSpace") ||
        !VerifyCondition(ProjectDecoded.DependencyLocks.size() == 1u, ".ProjectSpace dependency lock preserved"))
    {
        return false;
    }

    ColourSpaceDocumentModel ColourModel{};
    ColourModel.Swatches.push_back({ "CobaltAccent", 0.231, 0.510, 0.965, 1.0 });
    ColourModel.SpectralSamplesNanometer = { 0.12f, 0.48f, 0.91f, 0.64f };
    ColourSpaceDocumentModel ColourDecoded{};
    if (!VerifyCondition(ColourEncoder::EncodeToFile(ScratchDir / "Theme.ColourSpace", ColourModel, Diagnostic), "Encode .ColourSpace") ||
        !VerifyCondition(ColourDecoder::DecodeFromFile(ScratchDir / "Theme.ColourSpace", ColourDecoded, Diagnostic), "Decode .ColourSpace") ||
        !VerifyCondition(ColourDecoded.Swatches.size() == 1u && ColourDecoded.SpectralSamplesNanometer.size() == 4u, ".ColourSpace round-trip"))
    {
        return false;
    }

    FontSpaceDocumentModel FontModel{};
    FontModel.KerningPairs.push_back({ 65u, 86u, -0.075 });
    FontModel.SignedDistanceField = { -0.5f, 0.0f, 0.5f, 1.0f };
    FontSpaceDocumentModel FontDecoded{};
    if (!VerifyCondition(FontEncoder::EncodeToFile(ScratchDir / "Display.FontSpace", FontModel, Diagnostic), "Encode .FontSpace") ||
        !VerifyCondition(FontDecoder::DecodeFromFile(ScratchDir / "Display.FontSpace", FontDecoded, Diagnostic), "Decode .FontSpace") ||
        !VerifyCondition(FontDecoded.KerningPairs.size() == 1u && FontDecoded.SignedDistanceField.size() == 4u, ".FontSpace round-trip"))
    {
        return false;
    }

    ImageSpaceDocumentModel ImageModel{};
    ImageModel.Layers.push_back({ "PrimaryLayer", 1.0, true });
    ImageModel.LinearRgbaPreview = { 0.23f, 0.51f, 0.96f, 1.0f };
    ImageSpaceDocumentModel ImageDecoded{};
    if (!VerifyCondition(ImageEncoder::EncodeToFile(ScratchDir / "Layered.ImageSpace", ImageModel, Diagnostic), "Encode .ImageSpace") ||
        !VerifyCondition(ImageDecoder::DecodeFromFile(ScratchDir / "Layered.ImageSpace", ImageDecoded, Diagnostic), "Decode .ImageSpace") ||
        !VerifyCondition(ImageDecoded.Layers.size() == 1u && ImageDecoded.LinearRgbaPreview.size() == 4u, ".ImageSpace round-trip"))
    {
        return false;
    }

    TextureSpaceDocumentModel TextureModel{};
    TextureModel.TileLayers.push_back({ 1001u, 11u, 2048u });
    TextureModel.PackedTexelBytes = { 16u, 32u, 64u, 128u, 255u };
    TextureSpaceDocumentModel TextureDecoded{};
    if (!VerifyCondition(TextureEncoder::EncodeToFile(ScratchDir / "Albedo.TextureSpace", TextureModel, Diagnostic), "Encode .TextureSpace") ||
        !VerifyCondition(TextureDecoder::DecodeFromFile(ScratchDir / "Albedo.TextureSpace", TextureDecoded, Diagnostic), "Decode .TextureSpace") ||
        !VerifyCondition(TextureDecoded.TileLayers.size() == 1u && TextureDecoded.PackedTexelBytes.size() == 5u, ".TextureSpace round-trip"))
    {
        return false;
    }

    MaterialSpaceDocumentModel MaterialModel{};
    MaterialModel.ShaderSourceTag = "DirectionalLitShading.slang";
    MaterialModel.ParameterSlots.push_back({ "Roughness", 0.28, "Albedo.TextureSpace" });
    MaterialSpaceDocumentModel MaterialDecoded{};
    if (!VerifyCondition(MaterialEncoder::EncodeToFile(ScratchDir / "Hull.MaterialSpace", MaterialModel, Diagnostic), "Encode .MaterialSpace") ||
        !VerifyCondition(MaterialDecoder::DecodeFromFile(ScratchDir / "Hull.MaterialSpace", MaterialDecoded, Diagnostic), "Decode .MaterialSpace") ||
        !VerifyCondition(MaterialDecoded.ShaderSourceTag == "DirectionalLitShading.slang", ".MaterialSpace preserves DirectionalLitShading.slang"))
    {
        return false;
    }

    VectorSpaceDocumentModel VectorModel{};
    VectorModel.Curves.push_back({ "GuideSpline", false, 2u });
    VectorModel.ControlPointKnots = { 0.1, 0.2, 0.15, 0.25, 0.8, 0.9, 0.75, 0.85 };
    VectorSpaceDocumentModel VectorDecoded{};
    if (!VerifyCondition(VectorEncoder::EncodeToFile(ScratchDir / "Diagram.VectorSpace", VectorModel, Diagnostic), "Encode .VectorSpace") ||
        !VerifyCondition(VectorDecoder::DecodeFromFile(ScratchDir / "Diagram.VectorSpace", VectorDecoded, Diagnostic), "Decode .VectorSpace") ||
        !VerifyCondition(VectorDecoded.Curves.size() == 1u && VectorDecoded.ControlPointKnots.size() == 8u, ".VectorSpace round-trip"))
    {
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                     SUITE 2: 215 MiB / 750 MiB PARAMETRIC REVISION, BRANCHING, & MULTI-RES REPLAY
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool ValidateParametricRevisionAndCompaction()
{
    using namespace Frontier::Workspaces;

    // 1. Resolution-independent replay across 512x512, 2048x2048, and 8192x8192 + non-kinship branching.
    RevisionTracker Tracker;
    if (!VerifyCondition(
            Tracker.RecordParametricOperation(
                OperationRecorder::ConstructNormalizedBrush(0.25, 0.50, 0.75, 0.50, 0.02, 0.2, 0.5, 0.95, "StrokeA"),
                1ull) &&
                Tracker.RecordParametricOperation(
                    OperationRecorder::ConstructUVTileTransform(1, 0, 0.10, -0.05, 1.25, 0.25, 512.0, "UVTile1002"),
                    2ull) &&
                Tracker.RecordParametricOperation(
                    OperationRecorder::ConstructVectorControlPoint(0u, 0.50, 0.25, 0.55, 0.30, 1.27, "BezierA"),
                    3ull),
            "Recorded 3 resolution-independent parametric operations"))
    {
        return false;
    }

    const ReconstructedDocumentProjection Res512  = Tracker.ReconstructAtCursor({ 512u,  512u,  150.0, 1e-4 });
    const ReconstructedDocumentProjection Res2048 = Tracker.ReconstructAtCursor({ 2048u, 2048u, 300.0, 1e-4 });
    const ReconstructedDocumentProjection Res8192 = Tracker.ReconstructAtCursor({ 8192u, 8192u, 600.0, 1e-4 });

    if (!VerifyCondition(Res512.BrushSegments.size() == 1u && Res8192.BrushSegments.size() == 1u, "Brush reconstructed at all resolutions") ||
        !VerifyCondition(std::abs(Res512.BrushSegments[0].StartPixelX - 128.0) < 1e-6, "512px StartPixelX == 128.0") ||
        !VerifyCondition(std::abs(Res2048.BrushSegments[0].StartPixelX - 512.0) < 1e-6, "2048px StartPixelX == 512.0") ||
        !VerifyCondition(std::abs(Res8192.BrushSegments[0].StartPixelX - 2048.0) < 1e-6, "8192px StartPixelX == 2048.0") ||
        !VerifyCondition(Res2048.UVTiles.size() == 1u && Res2048.UVTiles[0].UDIMNumber == 1002u, "UVTile UDIM 1002 reconstructed") ||
        !VerifyCondition(std::abs(Res8192.UVTiles[0].EffectiveTexelDensity - 4.0 * Res2048.UVTiles[0].EffectiveTexelDensity) < 1e-6,
                         "EffectiveTexelDensity scales proportionally from 2048 to 8192"))
    {
        return false;
    }

    // 2. Non-kinship branching: undo 1 step and author an alternate branch, then verify both forward branches exist.
    if (!VerifyCondition(Tracker.Undo(), "Step backward to step 2"))
    {
        return false;
    }
    const uint32_t BranchPointStep = Tracker.QueryCursorStepIndex();
    if (!VerifyCondition(
            Tracker.RecordParametricOperation(
                OperationRecorder::ConstructNormalizedBrush(0.10, 0.10, 0.90, 0.90, 0.01, 0.9, 0.3, 0.2, "AlternateBranchB"),
                4ull),
            "Recorded divergent forward branch"))
    {
        return false;
    }
    if (!VerifyCondition(Tracker.Undo() && Tracker.QueryCursorStepIndex() == BranchPointStep, "Returned to branch point"))
    {
        return false;
    }
    const std::vector<uint32_t> ForwardBranches = Tracker.QueryForwardBranchesAtCursor();
    if (!VerifyCondition(ForwardBranches.size() == 2u, "NonLinearHistory preserves 2 peer forward branches via NextPeerIndex") ||
        !VerifyCondition(Tracker.Redo(ForwardBranches[1]), "Selected second forward branch cleanly"))
    {
        return false;
    }

    // 3. Verify Redo Purge -> Prefix Fold under tight per-document ceiling & lossless reconstruction after Prefix Fold!
    RevisionTracker BoundedTracker(512u); // Tight 512-byte ceiling (4 x 128B aligned operations max)
    for (uint32_t Step = 0u; Step < 3u; ++Step)
    {
        (void)BoundedTracker.RecordParametricOperation(
            OperationRecorder::ConstructNormalizedBrush(0.1 * Step, 0.2, 0.3, 0.4, 0.01, 1.0, 1.0, 1.0, "InitialStep"),
            Step + 1ull);
    }
    // Undo 1 step so an inactive redo branch exists, then push 6 more operations exceeding 512 bytes.
    (void)BoundedTracker.Undo();
    uint32_t TotalPurged = 0u;
    uint32_t TotalFolded = 0u;
    for (uint32_t Step = 0u; Step < 6u; ++Step)
    {
        CompactionReport Report{};
        if (!VerifyCondition(
                BoundedTracker.RecordParametricOperation(
                    OperationRecorder::ConstructNormalizedBrush(0.05 * Step, 0.5, 0.8, 0.5, 0.01, 0.4, 0.7, 1.0, "OverflowStep"),
                    10ull + Step,
                    {},
                    &Report),
                "BoundedTracker compacts instead of failing under overflow"))
        {
            return false;
        }
        TotalPurged += Report.PurgedRedoSteps;
        TotalFolded += Report.FoldedPrefixSteps;
        if (!VerifyCondition(BoundedTracker.QueryBytesUsed() <= 512u, "Per-document ContentRegion never exceeds ceiling"))
        {
            return false;
        }
    }

    const ReconstructedDocumentProjection CompactedProjection = BoundedTracker.ReconstructAtCursor({ 2048u, 2048u, 300.0, 1e-4 });
    if (!VerifyCondition(TotalPurged >= 1u, "Step 1 Redo Purge evicted inactive branch first") ||
        !VerifyCondition(TotalFolded >= 2u, "Step 2 Prefix Fold absorbed oldest steps into ParametricBaselineSummary") ||
        !VerifyCondition(CompactedProjection.BrushSegments.size() == 8u, "All 2 initial + 6 active brush strokes losslessly reconstructed after Prefix Fold"))
    {
        return false;
    }

    // 4. Verify RevisionExtension cross-tab Global Sequence Undo/Redo & global budget enforcement.
    RevisionExtension GlobalCoordinator(1024u);
    if (!VerifyCondition(GlobalCoordinator.Initialize(), "RevisionExtension Initialize"))
    {
        return false;
    }

    RevisionTracker TabA(1024u);
    RevisionTracker TabB(1024u);
    (void)GlobalCoordinator.RegisterDocumentTracker(1u, &TabA);
    (void)GlobalCoordinator.RegisterDocumentTracker(2u, &TabB);

    (void)GlobalCoordinator.RecordGlobalOperation(
        1u,
        OperationRecorder::ConstructNormalizedBrush(0.1, 0.1, 0.2, 0.2, 0.01, 1.0, 0.0, 0.0, "TabA_Edit1"));
    (void)GlobalCoordinator.RecordGlobalOperation(
        2u,
        OperationRecorder::ConstructUVTileTransform(0, 0, 0.2, 0.0, 1.0, 0.0, 1024.0, "TabB_Edit2"));

    if (!VerifyCondition(TabB.QueryUndoAvailable() && TabA.QueryUndoAvailable(), "Both tabs recorded edits") ||
        !VerifyCondition(GlobalCoordinator.UndoGlobalLatest() && !TabB.QueryUndoAvailable() && TabA.QueryUndoAvailable(),
                         "UndoGlobalLatest undoes TabB first in reverse global sequence") ||
        !VerifyCondition(GlobalCoordinator.UndoGlobalLatest() && !TabA.QueryUndoAvailable(),
                         "Second UndoGlobalLatest undoes TabA") ||
        !VerifyCondition(GlobalCoordinator.RedoGlobalNext() && TabA.QueryUndoAvailable(),
                         "RedoGlobalNext restores TabA first"))
    {
        GlobalCoordinator.Finalize();
        return false;
    }

    // Trigger global ceiling compaction across TabA (LRU background) while editing TabB (foreground).
    for (uint32_t Iter = 0u; Iter < 8u; ++Iter)
    {
        if (!VerifyCondition(
                GlobalCoordinator.RecordGlobalOperation(
                    2u,
                    OperationRecorder::ConstructUVTileTransform(0, 0, 0.01, 0.01, 1.0, 0.0, 1024.0, "TabB_Pressure")),
                "GlobalCoordinator compacts LRU background document under ceiling pressure") ||
            !VerifyCondition(
                GlobalCoordinator.QueryGlobalBytesUsed() <= GlobalCoordinator.QueryGlobalBudgetBytes(),
                "Aggregate global bytes never exceed GlobalCoordinator ceiling"))
        {
            GlobalCoordinator.Finalize();
            return false;
        }
    }

    GlobalCoordinator.Finalize();
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//             SUITE 3: PRIORITY-BUDGETED TASKSCHEDULER, ANTI-HANG EXECUTIONCHANNELS, & WORKSPACETHREAD
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool ValidatePriorityThreadingAndWorkspaceThread(
    const std::filesystem::path&              ScratchDir,
    Frontier::Threading::BudgetWindowReport&  OutWindowReport,
    Frontier::Threading::ChannelsTelemetrySnapshot& OutClusterTelemetry,
    Frontier::Threading::PublishedWorkspaceSnapshot& OutWorkspaceSnapshot)
{
    using namespace Frontier::Threading;
    using namespace Frontier::Workspaces;

    // 1. Verify TaskScheduler executes Tier 0 ImmediateCritical FIRST and bounds Tier 3 OpportunisticSlack by remaining budget.
    TaskChannel            Channel(64u);
    TaskScheduler          Scheduler(Channel);
    std::vector<int>       ExecutionOrder;

    ScheduledTaskUnit SlackFast{};
    SlackFast.TaskIdentifier      = 301u;
    SlackFast.PriorityTier        = TaskPriorityTier::OpportunisticSlack;
    SlackFast.EstimatedDurationUs = 100ull;
    SlackFast.Callable            = [&ExecutionOrder]() { ExecutionOrder.push_back(3); };

    ScheduledTaskUnit SlackTooHeavy{};
    SlackTooHeavy.TaskIdentifier      = 302u;
    SlackTooHeavy.PriorityTier        = TaskPriorityTier::OpportunisticSlack;
    SlackTooHeavy.EstimatedDurationUs = 50000ull; // Exceeds 4000 us window -> must be deferred!
    SlackTooHeavy.Callable            = [&ExecutionOrder]() { ExecutionOrder.push_back(99); };

    ScheduledTaskUnit AuthoringItem{};
    AuthoringItem.TaskIdentifier      = 201u;
    AuthoringItem.PriorityTier        = TaskPriorityTier::WorkspaceAuthoring;
    AuthoringItem.EstimatedDurationUs = 50ull;
    AuthoringItem.Callable            = [&ExecutionOrder]() { ExecutionOrder.push_back(2); };

    ScheduledTaskUnit DeterministicItem{};
    DeterministicItem.TaskIdentifier      = 101u;
    DeterministicItem.PriorityTier        = TaskPriorityTier::DeterministicTick;
    DeterministicItem.EstimatedDurationUs = 50ull;
    DeterministicItem.Callable            = [&ExecutionOrder]() { ExecutionOrder.push_back(1); };

    ScheduledTaskUnit ReplicationCritical{};
    ReplicationCritical.TaskIdentifier      = 1u;
    ReplicationCritical.PriorityTier        = TaskPriorityTier::ImmediateCritical;
    ReplicationCritical.EstimatedDurationUs = 20ull;
    ReplicationCritical.Callable            = [&ExecutionOrder]() { ExecutionOrder.push_back(0); };

    // Enqueue in reverse priority order (Tier 3 -> Tier 2 -> Tier 1 -> Tier 0) and verify strict Tier 0 -> 1 -> 2 -> 3 execution!
    (void)Scheduler.Enqueue(std::move(SlackFast));
    (void)Scheduler.Enqueue(std::move(SlackTooHeavy));
    (void)Scheduler.Enqueue(std::move(AuthoringItem));
    (void)Scheduler.Enqueue(std::move(DeterministicItem));
    (void)Scheduler.Enqueue(std::move(ReplicationCritical));

    OutWindowReport = Scheduler.ExecuteBudgetedWindow(4000ull, 250ull);
    const std::vector<int> ExpectedOrder = { 0, 1, 2, 3 };
    if (!VerifyCondition(ExecutionOrder == ExpectedOrder, "ImmediateCritical -> Deterministic -> Authoring -> Fitting Slack order") ||
        !VerifyCondition(OutWindowReport.ImmediateExecuted == 1u && OutWindowReport.OpportunisticExecuted == 1u,
                         "Executed 1 ImmediateCritical and 1 fitting OpportunisticSlack task") ||
        !VerifyCondition(OutWindowReport.DeferredOpportunisticCount == 1u,
                         "Over-budget Tier 3 task deferred cleanly to preserve tick deadline"))
    {
        return false;
    }

    // 2. Verify anti-hang cooperative AssistUntilComplete + CompletionCounter continuation on a 1-lane ExecutionChannels!
    ThreadProgressionTracker Monitor;
    ExecutionChannels SingleLaneCluster;
    if (!VerifyCondition(SingleLaneCluster.Initialize(1u, &Monitor, 128u), "1-lane ExecutionChannels Initialize"))
    {
        return false;
    }

    std::atomic<uint32_t> NestedChainAccumulator{ 0u };
    CompletionCounter     RootChainCounter(1u);

    ScheduledTaskUnit OuterTask{};
    OuterTask.TaskIdentifier = 500u;
    OuterTask.PriorityTier   = TaskPriorityTier::WorkspaceAuthoring;
    OuterTask.Callable       = [&]()
    {
        // Inside the ONLY execution channel lane, spawn 4 dependent tasks AND wait via AssistUntilComplete (would deadlock a sleep-wait setup!).
        CompletionCounter InnerCounter(4u);
        CompletionCounter ContinuationDone(1u);
        InnerCounter.AttachContinuation(
            [&]()
            {
                NestedChainAccumulator.fetch_add(100u, std::memory_order_acq_rel);
                ContinuationDone.Decrement();
            });

        for (uint32_t Sub = 0u; Sub < 4u; ++Sub)
        {
            ScheduledTaskUnit DependentUnit{};
            DependentUnit.TaskIdentifier   = 510u + Sub;
            DependentUnit.PriorityTier     = TaskPriorityTier::WorkspaceAuthoring;
            DependentUnit.CompletionTarget = &InnerCounter;
            DependentUnit.Callable         = [&NestedChainAccumulator]()
            {
                NestedChainAccumulator.fetch_add(1u, std::memory_order_acq_rel);
            };
            (void)SingleLaneCluster.EnqueueTask(std::move(DependentUnit));
        }

        SingleLaneCluster.AssistUntilComplete(ContinuationDone);
        RootChainCounter.Decrement();
    };

    (void)SingleLaneCluster.EnqueueTask(std::move(OuterTask));
    SingleLaneCluster.AssistUntilComplete(RootChainCounter);
    OutClusterTelemetry = SingleLaneCluster.InspectTelemetry();
    SingleLaneCluster.Finalize();

    if (!VerifyCondition(NestedChainAccumulator.load() == 104u, "Nested dependent tasks + continuation completed with zero deadlock") ||
        !VerifyCondition(OutClusterTelemetry.TasksExecuted >= 5ull, "ExecutionChannels executed outer + nested dependent tasks"))
    {
        return false;
    }

    // 3. Verify WorkspaceExtension + LifecycleSequence + asynchronous WorkspaceThread authoring, save, & WorkspaceExchange!
    Frontier::Lifecycle::LifecycleSequence Sequence;
    WorkspaceExtension                     Workspaces(Frontier::Constants::DefaultGlobalRevisionBudgetBytes, 2u);
    const uint32_t                         LifecycleSlot = Sequence.RegisterParticipant("WorkspaceExtension", Workspaces);
    if (!VerifyCondition(LifecycleSlot == 0u && Sequence.Initialize(), "WorkspaceExtension registered & initialized in LifecycleSequence"))
    {
        return false;
    }

    const std::filesystem::path AsyncUVPath = ScratchDir / "AsyncWing.UVSpace";
    const WorkspaceToken        UVToken     = Workspaces.CreateWorkspaceDocument(
        Frontier::Codecs::SpaceExtensionCategory::UVSpace,
        "AsyncWingParameterization",
        AsyncUVPath);

    CompletionCounter AsyncOpsDone(4u);
    (void)Workspaces.EnqueueAsyncDocumentOperation(
        UVToken,
        OperationRecorder::ConstructUVTileTransform(0, 0, 0.02, 0.04, 1.0, 0.0, 1024.0, "AsyncPack_1001"),
        &AsyncOpsDone);
    (void)Workspaces.EnqueueAsyncDocumentOperation(
        UVToken,
        OperationRecorder::ConstructUVTileTransform(1, 0, 0.05, 0.00, 1.0, 0.1, 1024.0, "AsyncPack_1002"),
        &AsyncOpsDone);
    (void)Workspaces.EnqueueAsyncDocumentOperation(
        UVToken,
        OperationRecorder::ConstructUVTileTransform(0, 1, 0.00, 0.08, 0.95, 0.0, 2048.0, "AsyncPack_1011"),
        &AsyncOpsDone);
    (void)Workspaces.EnqueueAsyncDocumentSave(UVToken, &AsyncOpsDone);

    Workspaces.AccessWorkspaceThread().AssistUntilComplete(AsyncOpsDone);
    Sequence.Tick(Frontier::Constants::FixedDeltaTime);

    // Wait briefly if the immediate snapshot task launched after save is still completing.
    for (int Spin = 0; Spin < 50 && !std::filesystem::exists(AsyncUVPath); ++Spin)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const CrossWorkspacePublication LatestPub = Workspaces.InspectExchange().InspectLatestPublication();
    const uint32_t                  HbSlot    = Workspaces.InspectWorkspaceThread().QueryProgressionSlot();
    const ThreadProgressionReading          HbReading = Workspaces.InspectThreadProgressionTracker().InspectSlot(HbSlot);
    OutWorkspaceSnapshot                      = Workspaces.InspectPublishedSnapshot();

    const bool ValidatedAsync =
        VerifyCondition(std::filesystem::exists(AsyncUVPath), "WorkspaceThread saved AsyncWing.UVSpace to disk") &&
        VerifyCondition(LatestPub.PublicationGeneration >= 3ull && LatestPub.AffectedUDIMNumber == 1011u,
                        "WorkspaceExchange published live UDIM 1011 update") &&
        VerifyCondition(HbReading.Condition == ThreadLivenessCondition::Healthy &&
                            Workspaces.InspectThreadProgressionTracker().QueryStalledSlotCount() == 0u,
                        "ThreadProgressionTracker reports Healthy WorkspaceThread & 0 stalled slots") &&
        VerifyCondition(OutWorkspaceSnapshot.GenerationStamp >= 2ull && OutWorkspaceSnapshot.ActiveDocumentCount == 1u,
                        "Lock-free PublishedWorkspaceSnapshot reflects active document");

    Sequence.Finalize();
    return ValidatedAsync;
}

//------------------------------------------------------------------------------------------------------------------------
//                         SUITE 4: DETERMINISTIC CPU VISUAL TELEMETRY DASHBOARD SNAPSHOT
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool RenderVisualValidationDashboard(
    const std::filesystem::path&                            OutputDirectory,
    const Frontier::Threading::BudgetWindowReport&          WindowReport,
    const Frontier::Threading::ChannelsTelemetrySnapshot&    ClusterTelemetry,
    const Frontier::Threading::PublishedWorkspaceSnapshot&  WorkspaceSnapshot)
{
    std::filesystem::create_directories(OutputDirectory);

    IMGUI_CHECKVERSION();
    ImGuiContext* GuiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(GuiCtx);
    ImGuiIO& Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.LogFilename = nullptr;
    Io.DisplaySize = ImVec2(1720.0f, 940.0f);

    Frontier::TypographyRepository Typefaces{};
    const auto FontArchiveRoot = Frontier::Exhibits::ResolveRepositoryRootPath() / "EngineContent/FontsArchives";
    (void)Typefaces.LoadRepository(FontArchiveRoot);
    Frontier::TypographyRepository::AssignCurrent(&Typefaces);
    if (ImFont* DefaultFace = Typefaces.QueryFont(0u, Frontier::FontWeightClass::Regular))
    {
        Io.FontDefault = DefaultFace;
    }
    unsigned char* FontPixels = nullptr;
    int            FontWidth  = 0;
    int            FontHeight = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &FontWidth, &FontHeight);

    Frontier::ThemeRepository Themes{};
    Themes.ConfigureTheme(Frontier::ThemePreset::Dark);
    Frontier::Components::ConfigureImGuiWorkspaceTheme(
        Frontier::Components::ConstructWorkspaceTabMetrics(1.0f),
        Themes);

    Frontier::Exhibits::SoftwareRasterCanvas Canvas(1720, 940, { 14, 14, 16, 255 });

    for (int Warmup = 0; Warmup < 2; ++Warmup)
    {
        Io.DisplaySize = ImVec2(1720.0f, 940.0f);
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f));
        ImGui::SetNextWindowSize(ImVec2(1680.0f, 900.0f));
        ImGui::Begin(
            "Frontier Workspace, Two-Tier .Space Codecs, Parametric Revision & WorkspaceThread Validation",
            nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        ImDrawList* CanvasList = ImGui::GetWindowDrawList();
        const ImU32 AccentBlue = IM_COL32(59, 130, 246, 255);
        const ImU32 EmeraldOk  = IM_COL32(34, 197, 94, 255);
        const ImU32 PanelFill  = IM_COL32(24, 24, 28, 255);
        const ImU32 BorderCol  = IM_COL32(46, 48, 56, 255);
        const ImU32 MutedText  = IM_COL32(168, 174, 190, 255);
        const ImU32 CrispText  = IM_COL32(236, 240, 248, 255);

        // Left Column: Two-Tier .Space Container & UDIM .UVTile Layout
        const ImVec2 Col1Min(40.0f, 72.0f);
        const ImVec2 Col1Max(572.0f, 896.0f);
        CanvasList->AddRectFilled(Col1Min, Col1Max, PanelFill, 8.0f);
        CanvasList->AddRect(Col1Min, Col1Max, BorderCol, 8.0f);
        CanvasList->PushClipRect(Col1Min, Col1Max, true);
        CanvasList->AddText(ImVec2(60.0f, 92.0f), AccentBlue, "1. TWO-TIER .SPACE CODECS (64B ALIGNED)");
        CanvasList->AddText(ImVec2(60.0f, 122.0f), CrispText, "Option 1A + 2A: PascalCase Write / Any-Case Load");
        CanvasList->AddText(ImVec2(60.0f, 148.0f), EmeraldOk, "OK: .ProjectSpace .ColourSpace .FontSpace .UVSpace");
        CanvasList->AddText(ImVec2(60.0f, 172.0f), EmeraldOk, "OK: .ImageSpace .TextureSpace .MaterialSpace");
        CanvasList->AddText(ImVec2(60.0f, 196.0f), EmeraldOk, "OK: .VectorSpace  |  FNV-1a 64-bit Checksums");
        CanvasList->AddText(ImVec2(60.0f, 220.0f), MutedText, "Shader: DirectionalLitShading.slang");
        CanvasList->AddText(ImVec2(60.0f, 244.0f), MutedText, "Blocked Legacy: .toml / .UVChart / .Surface");

        CanvasList->AddText(ImVec2(60.0f, 268.0f), CrispText, "UDIM .UVTile Grid (Zero .UVChart):");
        const char* TileLabels[4] = {
            "UDIM 1001 [0,0]\n1024 px/m",
            "UDIM 1002 [1,0]\n1024 px/m",
            "UDIM 1011 [0,1]\n2048 px/m",
            "UDIM 1012 [1,1]\n512 px/m"
        };
        for (int TileIdx = 0; TileIdx < 4; ++TileIdx)
        {
            const int   GridX = TileIdx % 2;
            const int   GridY = TileIdx / 2;
            const float X0    = 60.0f + GridX * 246.0f;
            const float Y0    = 302.0f + GridY * 168.0f;
            CanvasList->AddRectFilled(ImVec2(X0, Y0), ImVec2(X0 + 230.0f, Y0 + 150.0f), IM_COL32(31, 33, 40, 255), 6.0f);
            CanvasList->AddRect(ImVec2(X0, Y0), ImVec2(X0 + 230.0f, Y0 + 150.0f), AccentBlue, 6.0f, 0, 1.5f);
            CanvasList->AddRectFilled(ImVec2(X0 + 18.0f, Y0 + 62.0f), ImVec2(X0 + 116.0f, Y0 + 132.0f), IM_COL32(59, 130, 246, 70), 4.0f);
            CanvasList->AddRect(ImVec2(X0 + 18.0f, Y0 + 62.0f), ImVec2(X0 + 116.0f, Y0 + 132.0f), AccentBlue, 4.0f);
            CanvasList->AddText(ImVec2(X0 + 16.0f, Y0 + 14.0f), CrispText, TileLabels[TileIdx]);
        }

        CanvasList->AddText(ImVec2(60.0f, 662.0f), CrispText, "Binary Section 64-Byte Alignment:");
        CanvasList->AddRectFilled(ImVec2(60.0f, 694.0f), ImVec2(552.0f, 740.0f), IM_COL32(34, 46, 68, 255), 4.0f);
        CanvasList->AddText(ImVec2(74.0f, 708.0f), CrispText, "Tier 1 TOML -> 0x00 Pad -> Offset % 64 == 0");
        CanvasList->AddRectFilled(ImVec2(60.0f, 752.0f), ImVec2(552.0f, 798.0f), IM_COL32(28, 56, 44, 255), 4.0f);
        CanvasList->AddText(ImVec2(74.0f, 766.0f), EmeraldOk, "[.UVTile]  |  [.Baseline]  |  [.Revision]  (64B OK)");
        CanvasList->PopClipRect();

        // Center Column: 215 MiB / 750 MiB Parametric Revision & Arbitrary-Resolution Replay
        const ImVec2 Col2Min(592.0f, 72.0f);
        const ImVec2 Col2Max(1128.0f, 896.0f);
        CanvasList->AddRectFilled(Col2Min, Col2Max, PanelFill, 8.0f);
        CanvasList->AddRect(Col2Min, Col2Max, BorderCol, 8.0f);
        CanvasList->PushClipRect(Col2Min, Col2Max, true);
        CanvasList->AddText(ImVec2(612.0f, 92.0f), AccentBlue, "2. PARAMETRIC REVISION (215 / 750 MiB)");
        CanvasList->AddText(ImVec2(612.0f, 122.0f), CrispText, "Per-Doc Cap: 215 MiB  |  Global Cap: 750 MiB");
        CanvasList->AddText(ImVec2(612.0f, 148.0f), EmeraldOk, "Overflow: 1. Redo Purge -> 2. Prefix Fold");
        CanvasList->AddText(ImVec2(612.0f, 174.0f), MutedText, "Links: EnclosingIndex / FirstNested / NextPeer");

        CanvasList->AddText(ImVec2(612.0f, 222.0f), CrispText, "Arbitrary-Resolution Reconstruction:");
        const char* ResTitles[3] = { "512 x 512\nX = 128 px", "2048 x 2048\nX = 512 px", "8192 x 8192\nX = 2048 px" };
        for (int ResIdx = 0; ResIdx < 3; ++ResIdx)
        {
            const float RX = 612.0f + ResIdx * 168.0f;
            const float RY = 256.0f;
            CanvasList->AddRectFilled(ImVec2(RX, RY), ImVec2(RX + 152.0f, RY + 150.0f), IM_COL32(31, 33, 40, 255), 6.0f);
            CanvasList->AddRect(ImVec2(RX, RY), ImVec2(RX + 152.0f, RY + 150.0f), AccentBlue, 6.0f);
            CanvasList->AddLine(ImVec2(RX + 24.0f, RY + 108.0f), ImVec2(RX + 128.0f, RY + 108.0f), AccentBlue, 3.0f + ResIdx * 1.5f);
            CanvasList->AddText(ImVec2(RX + 16.0f, RY + 16.0f), CrispText, ResTitles[ResIdx]);
        }

        CanvasList->AddText(ImVec2(612.0f, 438.0f), CrispText, "NonLinearHistory Directed Tree:");
        const ImVec2 RootPt(650.0f, 524.0f);
        const ImVec2 Step1Pt(764.0f, 524.0f);
        const ImVec2 BranchAPt(894.0f, 488.0f);
        const ImVec2 BranchBPt(894.0f, 564.0f);
        CanvasList->AddLine(RootPt, Step1Pt, EmeraldOk, 2.5f);
        CanvasList->AddLine(Step1Pt, BranchAPt, MutedText, 2.0f);
        CanvasList->AddLine(Step1Pt, BranchBPt, AccentBlue, 3.0f);
        CanvasList->AddCircleFilled(RootPt, 9.0f, EmeraldOk);
        CanvasList->AddCircleFilled(Step1Pt, 9.0f, AccentBlue);
        CanvasList->AddCircleFilled(BranchAPt, 8.0f, MutedText);
        CanvasList->AddCircleFilled(BranchBPt, 10.0f, AccentBlue);
        CanvasList->AddText(ImVec2(618.0f, 542.0f), EmeraldOk, "Baseline");
        CanvasList->AddText(ImVec2(740.0f, 542.0f), CrispText, "Step 1");
        CanvasList->AddText(ImVec2(912.0f, 480.0f), MutedText, "Inactive Peer (Purged)");
        CanvasList->AddText(ImVec2(912.0f, 556.0f), AccentBlue, "Active Cursor");

        CanvasList->AddRectFilled(ImVec2(612.0f, 642.0f), ImVec2(1108.0f, 798.0f), IM_COL32(30, 32, 38, 255), 6.0f);
        CanvasList->AddText(ImVec2(628.0f, 660.0f), CrispText, "Global Coordinator: RevisionExtension");
        CanvasList->AddText(ImVec2(628.0f, 690.0f), EmeraldOk, "- Per-Tab & Global Sequence Undo/Redo: VERIFIED");
        CanvasList->AddText(ImVec2(628.0f, 718.0f), EmeraldOk, "- LRU Background Compaction: VERIFIED");
        CanvasList->AddText(ImVec2(628.0f, 746.0f), EmeraldOk, "- Lossless Multi-Resolution Replay: VERIFIED");
        CanvasList->PopClipRect();

        // Right Column: Option A Priority-Budgeted ExecutionChannels & WorkspaceThread
        const ImVec2 Col3Min(1148.0f, 72.0f);
        const ImVec2 Col3Max(1680.0f, 896.0f);
        CanvasList->AddRectFilled(Col3Min, Col3Max, PanelFill, 8.0f);
        CanvasList->AddRect(Col3Min, Col3Max, BorderCol, 8.0f);
        CanvasList->PushClipRect(Col3Min, Col3Max, true);
        CanvasList->AddText(ImVec2(1168.0f, 92.0f), AccentBlue, "3. OPTION A & WORKSPACETHREAD");
        CanvasList->AddText(ImVec2(1168.0f, 122.0f), CrispText, "Priority-Budgeted Window (Zero Sleep Hangs)");

        const char* TierRows[4] = {
            "Tier 0 ImmediateCritical   [Runs 1st — Preempts]",
            "Tier 1 DeterministicTick   [Runs 2nd — Required]",
            "Tier 2 WorkspaceAuthoring  [Runs 3rd — .Space]",
            "Tier 3 OpportunisticSlack  [Remaining Budget]"
        };
        for (int Tier = 0; Tier < 4; ++Tier)
        {
            const float TY   = 166.0f + Tier * 56.0f;
            const ImU32 Fill = (Tier == 0) ? IM_COL32(36, 62, 108, 255) : IM_COL32(31, 33, 40, 255);
            CanvasList->AddRectFilled(ImVec2(1168.0f, TY), ImVec2(1660.0f, TY + 44.0f), Fill, 5.0f);
            CanvasList->AddRect(ImVec2(1168.0f, TY), ImVec2(1660.0f, TY + 44.0f), Tier == 0 ? AccentBlue : BorderCol, 5.0f);
            CanvasList->AddText(ImVec2(1182.0f, TY + 13.0f), CrispText, TierRows[Tier]);
        }

        char SliceSummary[128]{};
        std::snprintf(
            SliceSummary,
            sizeof(SliceSummary),
            "Window: Tier0=%u | SlackRun=%u | Deferred=%u",
            WindowReport.ImmediateExecuted,
            WindowReport.OpportunisticExecuted,
            WindowReport.DeferredOpportunisticCount);
        CanvasList->AddText(ImVec2(1168.0f, 410.0f), EmeraldOk, SliceSummary);

        char ClusterSummary[128]{};
        std::snprintf(
            ClusterSummary,
            sizeof(ClusterSummary),
            "1-Lane Anti-Hang: Run=%llu | Assist=%llu | Hang=0",
            static_cast<unsigned long long>(ClusterTelemetry.TasksExecuted),
            static_cast<unsigned long long>(ClusterTelemetry.CooperativeAssists));
        CanvasList->AddText(ImVec2(1168.0f, 440.0f), EmeraldOk, ClusterSummary);

        CanvasList->AddRectFilled(ImVec2(1168.0f, 492.0f), ImVec2(1660.0f, 798.0f), IM_COL32(30, 32, 38, 255), 6.0f);
        CanvasList->AddText(ImVec2(1184.0f, 514.0f), AccentBlue, "Dedicated Thread: WorkspaceThread");
        char SnapshotLine1[128]{};
        std::snprintf(
            SnapshotLine1,
            sizeof(SnapshotLine1),
            "Published Gen: #%llu  |  Open .Space Docs: %u",
            static_cast<unsigned long long>(WorkspaceSnapshot.GenerationStamp),
            WorkspaceSnapshot.ActiveDocumentCount);
        CanvasList->AddText(ImVec2(1184.0f, 548.0f), CrispText, SnapshotLine1);
        CanvasList->AddText(ImVec2(1184.0f, 580.0f), EmeraldOk, "ThreadProgressionTracker: Healthy (0 Stalled)");
        CanvasList->AddText(ImVec2(1184.0f, 612.0f), EmeraldOk, "WorkspaceExchange: UDIM 1011 Published");
        CanvasList->AddText(ImVec2(1184.0f, 644.0f), MutedText, "Other Domain Threads: Deferred per Scope");
        CanvasList->PopClipRect();

        ImGui::End();
        ImGui::Render();

        if (Warmup == 1)
        {
            Canvas.Clear({ 14, 14, 16, 255 });
            Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
        }
    }

    const bool SavedPng = Canvas.WritePngFile(OutputDirectory / "01-WorkspaceCodecsRevisionAndThreading.png");
    ImGui::DestroyContext(GuiCtx);
    return VerifyCondition(SavedPng, "Wrote 01-WorkspaceCodecsRevisionAndThreading.png");
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    const std::filesystem::path RepoRoot   = Frontier::Exhibits::ResolveRepositoryRootPath();
    const std::filesystem::path ScratchDir = std::filesystem::temp_directory_path() / "FrontierWorkspaceValidationScratch";
    const std::filesystem::path ImageDir   = RepoRoot / "Exhibits/Images/Workspaces";

    std::error_code CleanupError;
    std::filesystem::remove_all(ScratchDir, CleanupError);

    if (!ValidateSpaceContainerAndDomainCodecs(ScratchDir))
    {
        return 1;
    }

    if (!ValidateParametricRevisionAndCompaction())
    {
        return 1;
    }

    Frontier::Threading::BudgetWindowReport         WindowReport{};
    Frontier::Threading::ChannelsTelemetrySnapshot   ClusterTelemetry{};
    Frontier::Threading::PublishedWorkspaceSnapshot WorkspaceSnapshot{};
    if (!ValidatePriorityThreadingAndWorkspaceThread(ScratchDir, WindowReport, ClusterTelemetry, WorkspaceSnapshot))
    {
        return 1;
    }

    if (!RenderVisualValidationDashboard(ImageDir, WindowReport, ClusterTelemetry, WorkspaceSnapshot))
    {
        return 1;
    }

    std::filesystem::remove_all(ScratchDir, CleanupError);
    std::printf("[WorkspaceRevisionValidation] All .Space Codecs, 215/750 MiB Revision, ExecutionChannels, and WorkspaceThread checks passed.\n");
    return 0;
}
