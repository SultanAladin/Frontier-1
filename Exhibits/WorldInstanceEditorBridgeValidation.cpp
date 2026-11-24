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

#include <cmath>
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

} // namespace

int main()
{
    bool AllPassed = true;
    AllPassed &= ValidateLiveOutlinerBinding();
    AllPassed &= ValidateInstanceDocumentSpawning();
    AllPassed &= ValidateWorldSceneSynchronization();

    if (!AllPassed)
    {
        std::cerr << "[ERROR] WorldInstanceEditorBridgeValidation failed.\n";
        return 1;
    }

    std::cout << "[OK] All WorldInstanceEditorBridgeValidation checks passed.\n";
    return 0;
}
