/*============================================================================================================================================
                                                WORLDINSTANCEEDITORBRIDGEVALIDATION.CPP
============================================================================================================================================*/
// 🧪 End-to-end Editor bridge validation: WorldInstanceRegister <-> OutlinerPanel, .Instance spawning & WorldScene sync

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/InstanceCodec.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Editor/WorldInstanceEditorBridge.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/World/CategoryInstances.h"
#include "Engine/World/WorldInstanceRegister.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

bool ExpectCondition(bool Condition, const char* Message)
{
    if (!Condition)
    {
        std::cerr << "[FAIL] " << Message << "\n";
        return false;
    }
    return true;
}

bool ValidateLiveOutlinerBinding()
{
    OutlinerPanel                     Outliner{};
    World::WorldInstanceRegister      WorldRegister{};
    Editor::WorldInstanceEditorBridge Bridge{};
    Bridge.BindOutliner(WorldRegister, Outliner);

    bool Passed = true;

    // Editor-constructed Solar, Lunar & Surface instances must appear in OutlinerPanel automatically
    {
        World::SolarInstance      Sun(WorldRegister, "MainSun", World::SolarInstanceParameters{});
        World::LunarInstance      Moon(WorldRegister, "PrimaryMoon", World::LunarInstanceParameters{});
        World::SurfaceInstance    Mesh(WorldRegister, "TestChassis", World::SurfaceInstanceParameters{});

        Passed &= ExpectCondition(
            Outliner.QueryTotalCount() == 3u,
            "Three Editor-constructed instances must produce 3 OutlinerPanel rows");
        Passed &= ExpectCondition(
            Bridge.QueryBoundRowCount() == 3u,
            "Bridge must track 3 bidirectional InstanceToken <-> OutlinerSlotToken links");

        const OutlinerSlotToken MoonSlot = Bridge.QueryOutlinerSlot(Moon.QueryToken());
        const OutlinerRecordEntry* MoonRow = Outliner.FindEntry(MoonSlot);
        Passed &= ExpectCondition(
            (MoonRow != nullptr) &&
                (MoonRow->Title == "PrimaryMoon") &&
                (MoonRow->DomainTag == "env:moon") &&
                MoonRow->VisibilityEnabled,
            "LunarInstance Outliner row must carry title PrimaryMoon and env:moon tag");

        // Visibility assignment must propagate directly into OutlinerPanel
        Passed &= ExpectCondition(
            WorldRegister.AssignVisibility(Moon.QueryToken(), false),
            "AssignVisibility(Moon, false) must succeed");
        Passed &= ExpectCondition(
            (MoonRow != nullptr) && !MoonRow->VisibilityEnabled,
            "AssignVisibility must update live OutlinerPanel row visibility");

        // Reverse lookup must resolve back to the exact InstanceToken
        Passed &= ExpectCondition(
            Bridge.QueryInstanceToken(MoonSlot) == Moon.QueryToken(),
            "Reverse OutlinerSlotToken -> InstanceToken lookup must match");
    }

    // RAII destructors must unregister all 3 rows from OutlinerPanel automatically
    Passed &= ExpectCondition(
        Outliner.QueryTotalCount() == 0u,
        "RAII destructors must remove all 3 OutlinerPanel rows");
    Passed &= ExpectCondition(
        Bridge.QueryBoundRowCount() == 0u,
        "Bridge must hold 0 residual row links after RAII destruction");

    Bridge.Unbind();
    std::cout << "[PASS] Live WorldInstanceRegister <-> OutlinerPanel RAII binding verified\n";
    return Passed;
}

bool ValidateInstanceDocumentSpawning()
{
    OutlinerPanel                     Outliner{};
    World::WorldInstanceRegister      WorldRegister{};
    Editor::WorldInstanceEditorBridge Bridge{};
    Bridge.BindOutliner(WorldRegister, Outliner);

    // Encode a multi-format .Instance document and decode it back
    Codecs::InstanceDocumentModel SourceDoc{};
    SourceDoc.DocumentTitle   = "LanternFixture";
    SourceDoc.Category        = World::InstanceCategory::SurfaceGeometry;
    SourceDoc.PositionMeters  = { 3.25f, -1.50f, 2.00f };
    SourceDoc.RotationDegrees = { 0.0f, 0.0f, 45.0f };
    SourceDoc.ScaleScalar     = { 1.2f, 1.2f, 1.2f };
    SourceDoc.EditorDirectives.OutlinerTitle = "Brass Lantern";
    SourceDoc.EditorDirectives.CategoryLabel = "Composite Fixture";
    SourceDoc.EditorDirectives.Subtitle      = "Geometry + Material + Flame Particles";
    SourceDoc.SpaceSlots = {
        { "PrimarySurface",  ".GeometrySpace", "Content/Props/Lantern.GeometrySpace",   0ull },
        { "SurfaceMaterial", ".MaterialSpace", "Content/Materials/Brass.MaterialSpace", 0ull },
        { "FlameParticles",  ".ParticleSpace", "Content/Effects/Flame.ParticleSpace",   0ull }
    };

    std::vector<std::byte> EncodedBytes{};
    std::string Diagnostic{};
    bool Passed = true;
    Passed &= ExpectCondition(
        Codecs::InstanceEncoder::EncodeToBytes(SourceDoc, EncodedBytes, Diagnostic, false),
        "Encode .Instance document must succeed");

    Codecs::InstanceDocumentModel DecodedDoc{};
    Passed &= ExpectCondition(
        Codecs::InstanceDecoder::DecodeFromBytes(EncodedBytes, DecodedDoc, Diagnostic),
        "Decode .Instance document must succeed");

    const World::InstanceToken SpawnedToken = Bridge.SpawnInstanceFromDocument(WorldRegister, DecodedDoc);
    Passed &= ExpectCondition(SpawnedToken.QueryValid(), "SpawnInstanceFromDocument must return valid InstanceToken");

    const OutlinerSlotToken RowSlot = Bridge.QueryOutlinerSlot(SpawnedToken);
    const OutlinerRecordEntry* Row  = Outliner.FindEntry(RowSlot);
    Passed &= ExpectCondition(
        (Row != nullptr) &&
            (Row->Title == "Brass Lantern") &&
            (Row->CategoryLabel == "Composite Fixture") &&
            (std::fabs(Row->PositionX - 3.25f) < 1e-4f) &&
            (std::fabs(Row->RotationZ - 45.0f) < 1e-4f),
        "Spawned .Instance Outliner row must reflect title, category label, position and rotation");

    Passed &= ExpectCondition(
        Bridge.RemoveManagedInstance(SpawnedToken),
        "RemoveManagedInstance must reclaim spawned instance");
    Passed &= ExpectCondition(
        (Outliner.QueryTotalCount() == 0u) && !WorldRegister.AuthenticateToken(SpawnedToken),
        "Removing spawned instance must clear both OutlinerPanel row and WorldInstanceRegister slot");

    Bridge.Unbind();
    std::cout << "[PASS] Multi-format .Instance decode -> live Editor spawn -> reclaim verified\n";
    return Passed;
}

bool ValidateWorldSceneSynchronization()
{
    OutlinerPanel                            Outliner{};
    World::WorldInstanceRegister             WorldRegister{};
    Editor::WorldInstanceEditorBridge        Bridge{};
    GeometricRaster::WorldSceneSpecification Scene{};

    Scene.Atmosphere.Medium.AtmosphereEnabled      = true;
    Scene.Atmosphere.Solar.SolarEnabled            = true;
    Scene.Atmosphere.Solar.ManualAzimuthDegrees    = 140.0f;
    Scene.Atmosphere.Solar.ManualElevationDegrees  = 32.0f;
    Scene.Atmosphere.Solar.CycleAnimationEnabled   = true;
    Scene.Atmosphere.Solar.DayDurationHours        = 0.1f; // 1.0 deg/s
    Scene.Atmosphere.Clouds.CloudsEnabled          = true;
    Scene.Atmosphere.Clouds.WindSpeedMetersPerSecond = 9.0f;
    Scene.Atmosphere.Moons[0].Visible              = true;
    Scene.Atmosphere.Moons[0].AzimuthDegrees       = 250.0f;

    Bridge.BindOutliner(WorldRegister, Outliner);
    Bridge.AdoptWorldSceneIntoRegister(WorldRegister, Scene);

    bool Passed = true;

    // 1. Adoption must NOT duplicate Outliner rows managed by WorldEnvironmentRegistry
    Passed &= ExpectCondition(
        Outliner.QueryTotalCount() == 0u,
        "AdoptWorldSceneIntoRegister must not duplicate existing Outliner rows");

    // 2. Idle Edit Mode (ExecutionEnabled = false) must perform zero work & zero scene mutations
    const World::WorldLoopStepMetrics EditMetrics =
        World::ExecuteWorldSimulationSequence(WorldRegister, 1.0f, false);
    const bool EditMutated = Bridge.SynchronizeRegisterToWorldScene(WorldRegister, EditMetrics, Scene, 1.0f);
    Passed &= ExpectCondition(
        !EditMutated && (std::fabs(Scene.Atmosphere.Solar.ManualAzimuthDegrees - 140.0f) < 1e-4f),
        "Idle Edit Mode must not mutate WorldSceneSpecification");

    // 3. Play Mode (ExecutionEnabled = true, Δt = 2.0s) must advance Solar azimuth & Cloud simulation clock
    WorldRegister.AccessLunarRegister().OrbitalRateDegreesPerS[0] = 6.0f;
    const World::WorldLoopStepMetrics PlayMetrics =
        World::ExecuteWorldSimulationSequence(WorldRegister, 2.0f, true);
    const bool PlayMutated = Bridge.SynchronizeRegisterToWorldScene(WorldRegister, PlayMetrics, Scene, 2.0f);
    Passed &= ExpectCondition(
        PlayMutated &&
            (std::fabs(Scene.Atmosphere.Solar.ManualAzimuthDegrees - 142.0f) < 1e-3f) &&
            (std::fabs(Scene.Atmosphere.Moons[0].AzimuthDegrees - 262.0f) < 1e-3f) &&
            (std::fabs(Scene.Atmosphere.SimulationSeconds - 2.0f) < 1e-3f),
        "Play Mode must push Solar azimuth, Moon orbit and Cloud advection time into WorldSceneSpecification");

    Bridge.Unbind();
    std::cout << "[PASS] WorldScene adoption & Edit/Play SoA synchronization verified\n";
    return Passed;
}

bool ValidateDiskDropBidirectionalSyncShippingCook()
{
    namespace fs = std::filesystem;
    const fs::path TempDir     = fs::temp_directory_path();
    const fs::path SourcePath  = TempDir / "FrontierPhaseC_Source.Instance";
    const fs::path ExportPath  = TempDir / "FrontierPhaseC_Export.Instance";
    const fs::path CookedPath  = TempDir / "FrontierPhaseC_Cooked.Instance";

    Codecs::InstanceDocumentModel AuthorDoc{};
    AuthorDoc.DocumentTitle   = "CampfireLantern";
    AuthorDoc.Category        = World::InstanceCategory::SurfaceGeometry;
    AuthorDoc.PositionMeters  = { 0.0f, 0.0f, 0.0f };
    AuthorDoc.RotationDegrees = { 0.0f, 0.0f, 15.0f };
    AuthorDoc.ScaleScalar     = { 1.0f, 1.0f, 1.0f };
    AuthorDoc.EditorDirectives.OutlinerTitle = "Campfire Lantern";
    AuthorDoc.EditorDirectives.CategoryLabel = "Composite Fixture";
    AuthorDoc.EditorDirectives.Subtitle      = "Geometry + Material + Flame Particles";
    AuthorDoc.SpaceSlots = {
        { "PrimarySurface",  ".GeometrySpace", "Content/Props/Lantern.GeometrySpace",   0ull },
        { "SurfaceMaterial", ".MaterialSpace", "Content/Materials/Brass.MaterialSpace", 0ull },
        { "FlameParticles",  ".ParticleSpace", "Content/Effects/Flame.ParticleSpace",   0ull }
    };

    bool Passed = true;
    std::string Diagnostic{};
    Passed &= ExpectCondition(
        Codecs::InstanceEncoder::EncodeToFile(SourcePath, AuthorDoc, Diagnostic, false),
        "Writing source .Instance file to disk must succeed");

    OutlinerPanel                     Outliner{};
    World::WorldInstanceRegister      WorldRegister{};
    Editor::WorldInstanceEditorBridge Bridge{};
    Bridge.BindOutliner(WorldRegister, Outliner);

    // 1. Spawn from disk at raycasted drop coordinates
    const World::InstanceToken DroppedToken = Bridge.SpawnInstanceFromFile(
        WorldRegister,
        SourcePath,
        { 7.5f, -3.25f, 1.10f },
        {},
        &Diagnostic);
    Passed &= ExpectCondition(DroppedToken.QueryValid(), "SpawnInstanceFromFile must return valid InstanceToken");

    const OutlinerSlotToken RowSlot = Bridge.QueryOutlinerSlot(DroppedToken);
    Passed &= ExpectCondition(
        Outliner.QueryPrimarySelection() == RowSlot,
        "SpawnInstanceFromFile must select the newly created Outliner row");

    // Flush initial transform RequireRebuild
    (void)World::ExecuteTransformLoop(WorldRegister);

    // 2. Simulate Gizmo drag + Outliner visibility toggle -> bidirectional push back into WorldRegister
    OutlinerRecordEntry* Row = Outliner.AccessEntry(RowSlot);
    Passed &= ExpectCondition(Row != nullptr, "Spawned Outliner row must be accessible");
    if (Row != nullptr)
    {
        Row->PositionX         = 11.25f;
        Row->RotationZ         = 90.0f;
        Row->VisibilityEnabled = false;
    }

    const bool PushedBack = Bridge.SynchronizeOutlinerMutationsToRegister(WorldRegister);
    Passed &= ExpectCondition(PushedBack, "SynchronizeOutlinerMutationsToRegister must report mutation");
    const World::InstanceRecordEntry* RegEntry = WorldRegister.FindRecordEntry(DroppedToken);
    Passed &= ExpectCondition(
        (RegEntry != nullptr) &&
            !World::QueryDirectiveEnabled(RegEntry->DirectiveMask, World::InstanceDirectiveMask::EnableVisibility) &&
            (std::fabs(WorldRegister.InspectTransforms().PositionMetersX[RegEntry->TransformSlot] - 11.25f) < 1e-4f) &&
            (std::fabs(WorldRegister.InspectTransforms().RotationDegreesZ[RegEntry->TransformSlot] - 90.0f) < 1e-4f) &&
            WorldRegister.InspectTransforms().RequireRebuild,
        "Outliner transform & visibility edits must propagate into WorldRegister and set RequireRebuild");

    // 3. Duplicate via Outliner override -> must clone live transform (+1m X)
    const OutlinerSlotToken DuplicatedSlot = Bridge.DuplicateOutlinerBoundSlot(WorldRegister, RowSlot);
    const OutlinerRecordEntry* ClonedRow   = Outliner.FindEntry(DuplicatedSlot);
    Passed &= ExpectCondition(
        DuplicatedSlot.Valid() &&
            (ClonedRow != nullptr) &&
            (std::fabs(ClonedRow->PositionX - 12.25f) < 1e-4f) &&
            (Outliner.QueryTotalCount() == 2u),
        "DuplicateOutlinerBoundSlot must clone live transform with +1m X offset");

    // 4. Export mutated live instance & cook for Shipping on disk
    Passed &= ExpectCondition(
        Bridge.ExportInstanceToFile(WorldRegister, DroppedToken, ExportPath, false, Diagnostic),
        "ExportInstanceToFile (Editor) must succeed");
    Passed &= ExpectCondition(
        Codecs::InstanceEncoder::CookFileForShipping(ExportPath, CookedPath, Diagnostic),
        "InstanceEncoder::CookFileForShipping must succeed");

    const uintmax_t EditorFileSize   = fs::file_size(ExportPath);
    const uintmax_t ShippingFileSize = fs::file_size(CookedPath);
    Passed &= ExpectCondition(
        ShippingFileSize < EditorFileSize,
        "Cooked Shipping .Instance file must be smaller than Editor .Instance file");

    Codecs::InstanceDocumentModel DecodedCooked{};
    Passed &= ExpectCondition(
        Codecs::InstanceDecoder::DecodeFromFile(CookedPath, DecodedCooked, Diagnostic),
        "Decoding cooked Shipping .Instance file must succeed");
    Passed &= ExpectCondition(
        DecodedCooked.EditorDirectives.OutlinerTitle.empty() &&
            (DecodedCooked.SpaceSlots.size() == 3u) &&
            (std::fabs(DecodedCooked.PositionMeters[0] - 11.25f) < 1e-4f) &&
            (std::fabs(DecodedCooked.RotationDegrees[2] - 90.0f) < 1e-4f) &&
            !World::QueryDirectiveEnabled(DecodedCooked.DirectiveMask, World::InstanceDirectiveMask::EnableVisibility),
        "Cooked Shipping .Instance must strip [Editor] while preserving mutated transform, visibility & SpaceSlots");

    // 5. Delete both via Outliner delete override -> must reclaim both slots & remove both rows
    const std::array<OutlinerSlotToken, 2u> BothSlots{ RowSlot, DuplicatedSlot };
    const uint32_t ReclaimedCount = Bridge.RemoveOutlinerBoundSlots(BothSlots);
    Passed &= ExpectCondition(
        (ReclaimedCount == 2u) &&
            (Outliner.QueryTotalCount() == 0u) &&
            (Bridge.QueryBoundRowCount() == 0u) &&
            !WorldRegister.AuthenticateToken(DroppedToken),
        "RemoveOutlinerBoundSlots must cleanly remove Outliner rows and reclaim WorldRegister slots");

    Bridge.Unbind();
    std::error_code IgnoreError{};
    fs::remove(SourcePath, IgnoreError);
    fs::remove(ExportPath, IgnoreError);
    fs::remove(CookedPath, IgnoreError);

    std::cout << "[PASS] Disk .Instance drop, bidirectional gizmo/Outliner sync, duplicate & Shipping cook verified ("
              << EditorFileSize << " B Editor -> " << ShippingFileSize << " B Shipping)\n";
    return Passed;
}

} // namespace

int main()
{
    bool AllPassed = true;
    AllPassed &= ValidateLiveOutlinerBinding();
    AllPassed &= ValidateInstanceDocumentSpawning();
    AllPassed &= ValidateWorldSceneSynchronization();
    AllPassed &= ValidateDiskDropBidirectionalSyncShippingCook();

    if (!AllPassed)
    {
        std::cerr << "[ERROR] WorldInstanceEditorBridgeValidation failed.\n";
        return 1;
    }

    std::cout << "[OK] All WorldInstanceEditorBridgeValidation checks passed.\n";
    return 0;
}
