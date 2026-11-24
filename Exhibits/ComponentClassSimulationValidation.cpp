/*============================================================================================================================================
                                                COMPONENTCLASSSIMULATIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase D27-D29 Validation of ComponentClass simulation POD categories (SoftBody, ForceField, FluidRegion, ParticleCloud),
//    non-destructive WorldPlacement overrides, and direct ComponentClass -> SimulationCoordinatorExtension assembly.

#include "Engine/ComponentClass/ComponentClassValidator.h"
#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/SimulationSpace/ComponentClassSimulationAssembler.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

namespace
{

int gPassed = 0;
int gFailed = 0;

void Check(bool cond, const std::string& label)
{
    if (cond) ++gPassed;
    else
    {
        ++gFailed;
        std::cerr << "[FAIL] " << label << "\n";
    }
}

bool Near(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) <= eps;
}

} // namespace

int main()
{
    using namespace Frontier::ComponentClass;
    using namespace Frontier::Lifecycle;
    using namespace Frontier::SimulationSpace;

    // ---------------------------------------------------------------------------------------------------------
    // D27: All 4 new ComponentCategory simulation PODs + storage summary metrics + duplication
    // ---------------------------------------------------------------------------------------------------------
    ComponentClassDefinition rig = ComponentClassSimulationAssembler::BuildTracksideRigComponentClass();
    const auto metrics = rig.Storage.ComputeSummaryMetrics();
    Check(metrics.SoftBodyCount == 2u && metrics.ForceFieldCount == 1u &&
          metrics.FluidRegionCount == 1u && metrics.ParticleCloudCount == 1u &&
          metrics.CollisionCount == 1u,
          "D27: ComponentInstanceStorage tracks all 4 new simulation POD categories accurately");

    Check(std::string(QueryCategoryLabel(ComponentCategory::SoftBodyInstance)) == "Soft Body" &&
          std::string(QueryCategoryLabel(ComponentCategory::ForceFieldInstance)) == "Force Field" &&
          std::string(QueryCategoryLabel(ComponentCategory::FluidRegionInstance)) == "Fluid Region" &&
          std::string(QueryCategoryLabel(ComponentCategory::ParticleCloudInstance)) == "Particle Cloud",
          "D27: QueryCategoryLabel resolves all 4 new simulation categories");

    const auto bannerTok = rig.Storage.FindTokenByTitle("TrackBannerCloth");
    const auto dupTok    = rig.Storage.DuplicateInstance(bannerTok);
    Check(dupTok.Valid() && rig.Storage.InspectSoftBody(dupTok) != nullptr &&
          rig.Storage.InspectSoftBody(dupTok)->SoftBodyAssetPath == rig.Storage.InspectSoftBody(bannerTok)->SoftBodyAssetPath,
          "D27: DuplicateInstance clones SoftBodyComponentSettings cleanly");
    Check(rig.Storage.RemoveInstance(dupTok), "D27: RemoveInstance removes duplicated SoftBody cleanly");

    // ---------------------------------------------------------------------------------------------------------
    // D28: Multi-placement instantiation + non-destructive per-instance overrides (Transform & Visibility)
    // ---------------------------------------------------------------------------------------------------------
    ComponentWorldPlacementRegister placements {};
    const ComponentClassToken rigClassTok { 1u, 1u };

    const auto p0 = placements.SpawnPlacement(
        rigClassTok, "RigAlpha",
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });

    const auto p1 = placements.SpawnPlacement(
        rigClassTok, "RigBeta",
        { { 5.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });

    // On RigBeta: hide TracksideFerns and shift TrackBannerCloth +1.0m on Y via InstanceOverrideDelta
    const auto fernsTok = rig.Storage.FindTokenByTitle("TracksideFerns");
    auto& hideOv = placements.AcquireOverride(p1, fernsTok);
    hideOv.OverrideVisibility = true;
    hideOv.VisibilityEnabled  = false;

    auto& shiftOv = placements.AcquireOverride(p1, bannerTok);
    shiftOv.OverrideTransform = true;
    shiftOv.LocalTransform    = { { 0.30f, 2.78f, -2.05f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } };

    // Verify hierarchical world position for nested CampfireEmbers under CampfireAnchor
    const auto embersTok = rig.Storage.FindTokenByTitle("CampfireEmbers");
    const auto embersWorldP0 = ComponentClassSimulationAssembler::ComputeEffectiveWorldPosition(rig, placements, p0, embersTok);
    const auto embersWorldP1 = ComponentClassSimulationAssembler::ComputeEffectiveWorldPosition(rig, placements, p1, embersTok);
    Check(Near(embersWorldP0[0], -1.95f) && Near(embersWorldP0[1], 0.48f) && Near(embersWorldP0[2], -1.55f),
          "D28: Nested CampfireEmbers composes parent CampfireAnchor local transform");
    Check(Near(embersWorldP1[0], 3.05f) && Near(embersWorldP1[1], 0.48f) && Near(embersWorldP1[2], -1.55f),
          "D28: RigBeta world offset shifts nested CampfireEmbers by +5.0m on X");

    // Verify assembled scene reflects 2 placements, 1 hidden fern suppressed, and shifted banner position
    std::unordered_map<uint64_t, const ComponentClassDefinition*> catalog;
    const uint64_t packedKey = (static_cast<uint64_t>(rigClassTok.Generation) << 32u) | static_cast<uint64_t>(rigClassTok.SlotIndex);
    catalog.emplace(packedKey, &rig);

    PrefabAssemblyReport report {};
    const SimulationSceneDocument assembled = ComponentClassSimulationAssembler::BuildSceneFromPlacements(
        catalog, placements, "TracksideMultiRigWorld", &report);

    Check(report.PlacementsProcessed == 2u && report.SkippedHiddenCount == 1u &&
          report.ForcesAssembled == 2u && report.FluidSourcesAssembled == 2u &&
          report.CloudsAssembled == 2u && report.SoftBodiesAssembled == 3u &&
          report.CollidersAssembled == 2u,
          "D28: BuildSceneFromPlacements respects both placements & non-destructive visibility override");

    bool foundShiftedBetaBanner = false;
    for (const auto& sb : assembled.SoftBodies)
    {
        if (sb.Label == "RigBeta::TrackBannerCloth")
        {
            foundShiftedBetaBanner = Near(sb.WorldOrigin[0], 5.30f) && Near(sb.WorldOrigin[1], 2.78f) && Near(sb.WorldOrigin[2], -2.05f);
        }
    }
    Check(foundShiftedBetaBanner, "D28: Per-instance Transform override shifts RigBeta::TrackBannerCloth to [5.30, 2.78, -2.05]");

    // Round-trip assembled ComponentClass scene through pure-TOML SimulationSceneCodec
    const std::string toml = SimulationSceneCodec::EncodeToTomlText(assembled);
    SimulationSceneDocument decoded {};
    std::string err;
    Check(SimulationSceneCodec::DecodeFromTomlText(toml, decoded, &err) &&
          decoded.SoftBodies.size() == 3u && decoded.Forces.size() == 2u && decoded.FluidSources.size() == 2u,
          "D28: Assembled ComponentClass world round-trips cleanly through SimulationSceneCodec");

    // ---------------------------------------------------------------------------------------------------------
    // D29: Live mount into SimulationCoordinatorExtension inside LifecycleSequence & 25-tick coupled run
    // ---------------------------------------------------------------------------------------------------------
    SimulationCoordinatorExtension coord(SimulationSceneCodec::BuildCoordinatorParameters(assembled));
    LifecycleSequence seq {};
    Check(seq.RegisterParticipant("PrefabSimCoordinator", coord) == 0u && seq.Initialize(),
          "D29: LifecycleSequence initializes coordinator parameterized by assembled ComponentClass world");

    PrefabAssemblyReport liveReport {};
    Check(ComponentClassSimulationAssembler::AssembleIntoCoordinator(
              rigClassTok, rig, placements, coord, &liveReport, &err),
          "D29: AssembleIntoCoordinator mounts both RigAlpha & RigBeta directly into live coordinator");
    Check(coord.InspectDeformables().InspectTelemetry().HasShippedVertexAtlas,
          "D29: Coordinator loads SportTireFem.VertexAtlas declared on SoftBodyComponentSettings");

    for (uint32_t step = 0u; step < 25u; ++step)
    {
        seq.Tick(1.0 / 60.0);
    }

    const auto& tel = coord.InspectTelemetry();
    Check(tel.CompletedTicks >= 24ull && tel.ActiveForceCount == 2u &&
          tel.FluidPeakDensity > 0.10f && tel.ActiveParticleCount > 100u &&
          tel.ActiveDeformableCount == 3u && tel.DeformableRigidContacts > 0u,
          "D29: Assembled ComponentClass world runs all 5 coupled domains cleanly with live Jolt contacts");
    seq.Finalize();

    // ---------------------------------------------------------------------------------------------------------
    // D30: ComponentClassValidator rules across all 4 simulation POD categories (clean + fault injection)
    // ---------------------------------------------------------------------------------------------------------
    auto diskAssetExists = [](std::string_view p) -> bool { return std::filesystem::exists(std::filesystem::path(p)); };
    const auto cleanIssues = ComponentClassValidator::ValidateClass(rig, diskAssetExists);
    Check(cleanIssues.empty() && !ComponentClassValidator::QueryHasBlockingErrors(cleanIssues),
          "D30: ComponentClassValidator reports 0 issues on valid TracksideRigComponentClass against real disk assets");

    ComponentClassDefinition faultyRig = rig;
    if (auto* sb = faultyRig.Storage.ResolveSoftBody(faultyRig.Storage.FindTokenByTitle("TrackBannerCloth")))
    {
        sb->SoftBodyAssetPath = "EngineContent/Simulation/Deformables/NonExistent.SoftBody";
        sb->SleepDistanceMeters = 0.0f;
    }
    if (auto* ff = faultyRig.Storage.ResolveForceField(faultyRig.Storage.FindTokenByTitle("CrosswindField")))
    {
        ff->RadiusMeters = -1.0f;
        ff->Direction    = { 0.0f, 0.0f, 0.0f };
    }
    if (auto* fl = faultyRig.Storage.ResolveFluidRegion(faultyRig.Storage.FindTokenByTitle("CampfirePlume")))
    {
        fl->RadiusMeters = 0.0f;
        fl->Density = 0.0f; fl->Temperature = 0.0f; fl->Fuel = 0.0f;
    }
    if (auto* pc = faultyRig.Storage.ResolveParticleCloud(faultyRig.Storage.FindTokenByTitle("CampfireEmbers")))
    {
        pc->ParticleCount = 0u;
        pc->ReleaseRate   = 0.0f;
    }

    const auto faultIssues = ComponentClassValidator::ValidateClass(faultyRig, diskAssetExists);
    uint32_t errorCount = 0u, warningCount = 0u;
    for (const auto& issue : faultIssues)
    {
        if (issue.Severity == DiagnosticSeverity::Error) ++errorCount;
        else if (issue.Severity == DiagnosticSeverity::Warning) ++warningCount;
    }
    Check(ComponentClassValidator::QueryHasBlockingErrors(faultIssues) && errorCount == 4u && warningCount == 4u,
          "D30: ComponentClassValidator catches all 4 blocking errors & 4 warnings across SoftBody/Force/Fluid/Particles");

    std::cout << "ComponentClassSimulationValidation: " << gPassed << " passed, " << gFailed << " failed.\n";
    return (gFailed == 0) ? 0 : 1;
}
