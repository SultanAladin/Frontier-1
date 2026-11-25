/*============================================================================================================================================
                                                      RUNTIMEWORLDHOSTVALIDATION.CPP
============================================================================================================================================*/
// 🧪 Standalone Shipping/Production validation for RuntimeWorldHost, RuntimeLifecycle integration & O(rebuilt) transform flush

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/InstanceCodec.h"
#include "Engine/Codecs/WorldSpaceCodec.h"
#include "Engine/Lifecycle/RuntimeLifecycle.h"
#include "Engine/Lifecycle/RuntimeWorldHost.h"

#include <chrono>
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

bool ValidateCookedShippingBoot()
{
    namespace fs = std::filesystem;
    const fs::path RootDir    = fs::temp_directory_path() / "FrontierPhaseD_CookedBoot";
    const fs::path SourceDir  = RootDir / "Authoring";
    const fs::path CookedDir  = RootDir / "Shipping";
    std::error_code DirErr{};
    fs::remove_all(RootDir, DirErr);
    fs::create_directories(SourceDir, DirErr);
    fs::create_directories(CookedDir, DirErr);

    auto AuthorCookFile = [&](const char* FileName, const Codecs::InstanceDocumentModel& Doc) -> bool
    {
        std::string Diagnostic{};
        const fs::path Src = SourceDir / FileName;
        const fs::path Dst = CookedDir / FileName;
        return Codecs::InstanceEncoder::EncodeToFile(Src, Doc, Diagnostic, false) &&
               Codecs::InstanceEncoder::CookFileForShipping(Src, Dst, Diagnostic);
    };

    bool Passed = true;

    // 1. Author & cook 4 distinct category .Instance files
    Codecs::InstanceDocumentModel SunDoc{};
    SunDoc.DocumentTitle = "SolarBody";
    SunDoc.Category      = World::InstanceCategory::SolarBody;
    SunDoc.DirectiveMask = static_cast<uint32_t>(
        World::InstanceDirectiveMask::EnableVisibility |
        World::InstanceDirectiveMask::EnableSimulationTick);
    Passed &= ExpectCondition(AuthorCookFile("01_Sun.Instance", SunDoc), "Cook 01_Sun.Instance must succeed");

    Codecs::InstanceDocumentModel MoonDoc{};
    MoonDoc.DocumentTitle = "LunarBody";
    MoonDoc.Category      = World::InstanceCategory::LunarBody;
    MoonDoc.DirectiveMask = static_cast<uint32_t>(
        World::InstanceDirectiveMask::EnableVisibility |
        World::InstanceDirectiveMask::EnableSimulationTick);
    Passed &= ExpectCondition(AuthorCookFile("02_Moon.Instance", MoonDoc), "Cook 02_Moon.Instance must succeed");

    Codecs::InstanceDocumentModel CloudDoc{};
    CloudDoc.DocumentTitle = "CloudDeck";
    CloudDoc.Category      = World::InstanceCategory::CloudDeck;
    CloudDoc.DirectiveMask = static_cast<uint32_t>(
        World::InstanceDirectiveMask::EnableVisibility |
        World::InstanceDirectiveMask::EnableSimulationTick);
    Passed &= ExpectCondition(AuthorCookFile("03_Clouds.Instance", CloudDoc), "Cook 03_Clouds.Instance must succeed");

    Codecs::InstanceDocumentModel SurfaceDoc{};
    SurfaceDoc.DocumentTitle  = "VehicleChassis";
    SurfaceDoc.Category       = World::InstanceCategory::SurfaceGeometry;
    SurfaceDoc.PositionMeters = { 2.0f, 3.0f, 0.5f };
    SurfaceDoc.SpaceSlots     = {
        { "PrimarySurface",  ".GeometrySpace", "Content/Vehicles/Chassis.GeometrySpace", 0ull },
        { "SurfaceMaterial", ".MaterialSpace", "Content/Materials/Paint.MaterialSpace",  0ull }
    };
    Passed &= ExpectCondition(AuthorCookFile("04_Chassis.Instance", SurfaceDoc), "Cook 04_Chassis.Instance must succeed");

    // 2. Boot RuntimeWorldHost inside RuntimeLifecycle (zero Editor symbols)
    Lifecycle::RuntimeLifecycle RuntimeLoop{};
    Lifecycle::RuntimeWorldHost WorldHost{};
    (void)RuntimeLoop.RegisterDomainParticipant("WorldHost", WorldHost);

    Lifecycle::RuntimeLoopConfiguration LoopSettings{};
    LoopSettings.PreferHeadlessWindow = true;
    Passed &= ExpectCondition(RuntimeLoop.Initialize(LoopSettings), "RuntimeLifecycle::Initialize must succeed");

    std::string Diagnostic{};
    const uint32_t MountedCount = WorldHost.MountCookedDirectory(CookedDir, Diagnostic);
    Passed &= ExpectCondition(MountedCount == 4u, "MountCookedDirectory must mount all 4 cooked .Instance files");
    Passed &= ExpectCondition(
        WorldHost.InspectActiveScene().ClusterInstances.size() == 1u,
        "Mounting 1 SurfaceGeometry .Instance must bind 1 RigidClusterInstance");

    // Enable continuous orbital/solar rates to verify 60 Hz deterministic fixed loop
    WorldHost.AccessWorldRegister().AccessSolarRegister().AzimuthDegrees[0]        = 100.0f;
    WorldHost.AccessWorldRegister().AccessSolarRegister().DayCycleSpeedDegreesS[0] = 12.0f;
    WorldHost.AccessWorldRegister().AccessLunarRegister().RightAscensionDegrees[0] = 200.0f;
    WorldHost.AccessWorldRegister().AccessLunarRegister().OrbitalRateDegreesPerS[0]= 18.0f;
    WorldHost.AccessWorldRegister().AccessCloudRegister().WindSpeedMetersPerS[0]   = 10.0f;

    // 3. Execute 60 fixed ticks of 1/60s = 1.0 simulated second
    const uint32_t ExecutedTicks = RuntimeLoop.ExecuteBoundedLoop(60u, 1.0 / 60.0);
    Passed &= ExpectCondition(ExecutedTicks == 60u, "RuntimeLifecycle must execute all 60 ticks");
    Passed &= ExpectCondition(WorldHost.QueryCompletedStepCount() == 60u, "RuntimeWorldHost must complete 60 steps");

    const GeometricRaster::WorldAtmosphereConfiguration& Atmo = WorldHost.InspectActiveScene().Atmosphere;
    Passed &= ExpectCondition(
        (std::fabs(Atmo.Solar.ManualAzimuthDegrees - 112.0f) < 1e-2f) &&
            (std::fabs(Atmo.Moons[0].AzimuthDegrees - 218.0f) < 1e-2f) &&
            (std::fabs(Atmo.SimulationSeconds - 1.0f) < 1e-2f),
        "60 Hz RuntimeLifecycle must advance Sun (+12 deg), Moon (+18 deg) & Cloud clock (+1.0s)");

    // 4. Verify Suspend/Resume stops ticks cleanly
    Passed &= ExpectCondition(RuntimeLoop.Suspend(), "RuntimeLifecycle::Suspend must succeed");
    WorldHost.Tick(1.0);
    Passed &= ExpectCondition(
        WorldHost.QueryCompletedStepCount() == 60u,
        "Suspended RuntimeWorldHost must ignore ticks");
    Passed &= ExpectCondition(RuntimeLoop.Resume(), "RuntimeLifecycle::Resume must succeed");

    RuntimeLoop.Finalize();
    fs::remove_all(RootDir, DirErr);

    std::cout << "[PASS] Standalone Shipping RuntimeLifecycle + RuntimeWorldHost 60 Hz boot verified\n";
    return Passed;
}

bool ValidateWorldSpaceBoot()
{
    namespace fs = std::filesystem;
    const fs::path WorldPath = fs::temp_directory_path() / "FrontierPhaseD_Level.WorldSpace";

    Codecs::WorldSpaceDocumentModel Level{};
    Level.DocumentTitle = "ShippingTrack";
    for (uint32_t i = 0u; i < 3u; ++i)
    {
        Codecs::WorldGeometryPlacementEntry Placement{};
        Placement.PlacementTitle    = "TrackPiece_" + std::to_string(i);
        Placement.GeometrySpacePath = "Content/Track/Segment.GeometrySpace";
        Placement.CenterWorld       = { static_cast<float>(i) * 5.0f, 0.0f, 0.0f };
        Placement.DimensionsMeters  = { 4.0f, 2.0f, 1.0f };
        Placement.RotationZDegrees  = static_cast<float>(i) * 15.0f;
        Level.GeometryPlacements.push_back(std::move(Placement));
    }

    Codecs::WorldMeasuredLuminairePlacement Lamp{};
    Lamp.PlacementTitle = "TrackFloodLight";
    Lamp.PositionWorld  = { 0.0f, 0.0f, 6.0f };
    Lamp.RangeMeters    = 32.0f;
    Level.MeasuredLuminairePlacements.push_back(std::move(Lamp));

    std::string Diagnostic{};
    bool Passed = true;
    Passed &= ExpectCondition(
        Codecs::WorldSpaceEncoder::EncodeToFile(WorldPath, Level, Diagnostic),
        "Encode .WorldSpace file must succeed");

    Lifecycle::RuntimeWorldHost WorldHost{};
    Passed &= ExpectCondition(WorldHost.Initialize(), "RuntimeWorldHost::Initialize must succeed");
    Passed &= ExpectCondition(
        WorldHost.LoadWorldSpaceFile(WorldPath, Diagnostic),
        "RuntimeWorldHost::LoadWorldSpaceFile must succeed");
    Passed &= ExpectCondition(
        (WorldHost.InspectWorldRegister().InspectSurfaceRegister().QueryActiveCount() == 3u) &&
            (WorldHost.InspectWorldRegister().InspectLuminaireRegister().QueryActiveCount() == 1u) &&
            (WorldHost.InspectActiveScene().ClusterInstances.size() == 3u) &&
            (WorldHost.InspectSceneSyncBridge().QueryActiveBindingCount() == 3u),
        "LoadWorldSpaceFile must register 3 surfaces, 1 luminaire and 3 bound ClusterInstances");

    WorldHost.Finalize();
    std::error_code IgnoreErr{};
    fs::remove(WorldPath, IgnoreErr);

    std::cout << "[PASS] .WorldSpace Production boot into WorldRegister & ClusterInstances verified\n";
    return Passed;
}

bool ValidateIncrementalTransformFlush()
{
    Lifecycle::RuntimeWorldHost WorldHost{};
    bool Passed = ExpectCondition(WorldHost.Initialize(), "RuntimeWorldHost::Initialize must succeed");

    constexpr uint32_t TotalInstances  = 1000u;
    constexpr uint32_t MovingInstances = 50u;
    WorldHost.AccessWorldRegister().ReserveInstanceCapacity(TotalInstances);

    std::vector<World::InstanceToken> SurfaceTokens{};
    SurfaceTokens.reserve(TotalInstances);

    for (uint32_t i = 0u; i < TotalInstances; ++i)
    {
        Codecs::InstanceDocumentModel Doc{};
        Doc.DocumentTitle  = "Prop_" + std::to_string(i);
        Doc.Category       = World::InstanceCategory::SurfaceGeometry;
        Doc.PositionMeters = { static_cast<float>(i), 0.0f, 0.0f };
        SurfaceTokens.push_back(WorldHost.MountCookedInstanceModel(Doc));
    }

    // Flush initial placement transforms
    const Lifecycle::RuntimeWorldStepReport InitialReport = WorldHost.StepRuntimeWorld(1.0f / 60.0f);
    Passed &= ExpectCondition(
        InitialReport.SyncedClusterTransforms == TotalInstances,
        "Initial step must flush all 1,000 initial cluster transforms");

    // Idle step with zero movements must flush 0 transforms
    const Lifecycle::RuntimeWorldStepReport IdleReport = WorldHost.StepRuntimeWorld(1.0f / 60.0f);
    Passed &= ExpectCondition(
        (IdleReport.SyncedClusterTransforms == 0u) && !IdleReport.SceneMutated,
        "Idle step must flush strictly 0 cluster transforms");

    // Move strictly 50 out of 1,000 instances
    for (uint32_t i = 0u; i < MovingInstances; ++i)
    {
        (void)WorldHost.AccessWorldRegister().AssignWorldTransform(
            SurfaceTokens[i],
            static_cast<float>(i) + 2.5f,
            1.0f,
            0.5f,
            0.0f,
            0.0f,
            30.0f,
            1.0f,
            1.0f,
            1.0f);
    }

    const auto StartClock = std::chrono::steady_clock::now();
    const Lifecycle::RuntimeWorldStepReport MoveReport = WorldHost.StepRuntimeWorld(1.0f / 60.0f);
    const auto EndClock   = std::chrono::steady_clock::now();
    const double FlushMs  = std::chrono::duration<double, std::milli>(EndClock - StartClock).count();

    Passed &= ExpectCondition(
        (MoveReport.SyncedClusterTransforms == MovingInstances) && MoveReport.SceneMutated,
        "Incremental flush must touch strictly 50 moved clusters out of 1,000");

    const GeometricRaster::RigidClusterInstance& MovedCluster =
        WorldHost.InspectActiveScene().ClusterInstances[0];
    const GeometricRaster::RigidClusterInstance& StaticCluster =
        WorldHost.InspectActiveScene().ClusterInstances[MovingInstances];

    Passed &= ExpectCondition(
        (std::fabs(MovedCluster.PreviousTranslation.X - 0.0f) < 1e-4f) &&
            (std::fabs(MovedCluster.WorldTranslation.X - 2.5f) < 1e-4f) &&
            (std::fabs(MovedCluster.RotationZDegrees - 30.0f) < 1e-4f),
        "Moved cluster must preserve PreviousTranslation for motion vectors & update WorldTranslation/RotationZ");
    Passed &= ExpectCondition(
        std::fabs(StaticCluster.WorldTranslation.X - static_cast<float>(MovingInstances)) < 1e-4f,
        "Unmoved cluster must remain untouched");

    WorldHost.Finalize();
    std::cout << "[PASS] O(rebuilt) incremental GPU transform flush (50 / 1,000) completed in " << FlushMs << " ms\n";
    return Passed;
}

} // namespace

int main()
{
    bool AllPassed = true;
    AllPassed &= ValidateCookedShippingBoot();
    AllPassed &= ValidateWorldSpaceBoot();
    AllPassed &= ValidateIncrementalTransformFlush();

    if (!AllPassed)
    {
        std::cerr << "[ERROR] RuntimeWorldHostValidation failed.\n";
        return 1;
    }

    std::cout << "[OK] All RuntimeWorldHostValidation checks passed.\n";
    return 0;
}
