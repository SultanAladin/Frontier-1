/*============================================================================================================================================
                                               COMPONENTSIMULATIONPREVIEWVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase D32-D33 Validation of ComponentSimulationPreviewDriver (Play -> Step -> Pause -> Resume ->
//    Live Deformable Render Surface Extraction -> Stop/Reset -> Clean Re-Play).

#include "Engine/SimulationSpace/ComponentSimulationPreviewDriver.h"

#include <cmath>
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

} // namespace

int main()
{
    using namespace Frontier::ComponentClass;
    using namespace Frontier::SimulationSpace;

    // 1. Non-simulation prop class incurs zero startup overhead and cleanly refuses preview start
    ComponentClassDefinition staticProp {};
    staticProp.Title = "PlainProp";
    staticProp.Storage.ResetWithSpatialOrigin("Origin");
    Check(!ComponentSimulationPreviewDriver::QueryHasSimulationComponents(staticProp),
          "D32: QueryHasSimulationComponents returns false for non-simulation static prop");

    ComponentSimulationPreviewDriver driver {};
    std::string err;
    Check(!driver.StartPreview(staticProp, &err) && driver.QueryStatus() == PreviewRunStatus::Idle,
          "D32: StartPreview returns false & remains Idle for static prop with no simulation components");

    // 2. Start live 5-domain preview on TracksideSimulationRig
    ComponentClassDefinition rig = ComponentClassSimulationAssembler::BuildTracksideRigComponentClass();
    Check(ComponentSimulationPreviewDriver::QueryHasSimulationComponents(rig),
          "D32: QueryHasSimulationComponents detects simulation components on TracksideSimulationRig");
    Check(driver.StartPreview(rig, &err) && driver.QueryStatus() == PreviewRunStatus::Running,
          "D32: StartPreview launches Running 5-domain simulation preview");
    Check(driver.QueryBoundRevision() == rig.RevisionNumber &&
          driver.InspectAssemblyReport().SoftBodiesAssembled == 2u &&
          driver.InspectAssemblyReport().ForcesAssembled == 1u &&
          driver.InspectAssemblyReport().FluidSourcesAssembled == 1u &&
          driver.InspectAssemblyReport().CloudsAssembled == 1u,
          "D32: Preview driver records exact bound revision & assembly counts");

    // 3. Step 18 frames and verify all 5 coupled domains advance
    for (uint32_t i = 0u; i < 18u; ++i)
    {
        driver.StepPreview(1.0 / 60.0);
    }
    const auto telBeforePause = driver.InspectTelemetry();
    Check(telBeforePause.CompletedTicks >= 17ull && telBeforePause.ActiveDeformableCount == 2u &&
          telBeforePause.FluidPeakDensity > 0.10f && telBeforePause.ActiveParticleCount > 80u,
          "D33: StepPreview advances ForceFields, Fluid, ParticleSpace, and Deformables live");

    // 4. Pause -> StepPreview is a true no-op -> Resume -> advances again
    Check(driver.PausePreview() && driver.QueryStatus() == PreviewRunStatus::Paused,
          "D33: PausePreview suspends preview cleanly");
    for (uint32_t i = 0u; i < 10u; ++i)
    {
        driver.StepPreview(1.0 / 60.0);
    }
    Check(driver.InspectTelemetry().CompletedTicks == telBeforePause.CompletedTicks,
          "D33: Stepping while Paused leaves simulation tick count frozen");

    Check(driver.ResumePreview() && driver.QueryStatus() == PreviewRunStatus::Running,
          "D33: ResumePreview restores Running status");
    for (uint32_t i = 0u; i < 12u; ++i)
    {
        driver.StepPreview(1.0 / 60.0);
    }
    const auto telAfterResume = driver.InspectTelemetry();
    Check(telAfterResume.CompletedTicks > telBeforePause.CompletedTicks &&
          telAfterResume.DeformableRigidContacts > 0u,
          "D33: Resumed preview continues coupled stepping with live Jolt contacts");

    // 5. Extract live deformed triangle render surfaces for viewport display
    const auto surfaces = driver.ExtractDeformableRenderSurfaces();
    bool surfacesValid = (surfaces.size() == 2u);
    for (const auto& s : surfaces)
    {
        if (s.Surface.Positions.empty() || s.Surface.Indices.empty()) surfacesValid = false;
        for (const float coord : s.Surface.Positions)
        {
            if (!std::isfinite(coord)) surfacesValid = false;
        }
    }
    Check(surfacesValid,
          "D33: ExtractDeformableRenderSurfaces produces valid finite 3D triangle surfaces for all active SoftBodies");

    // 6. Stop & Reset -> Idle -> Clean Re-Play
    driver.StopAndResetPreview();
    Check(driver.QueryStatus() == PreviewRunStatus::Idle && driver.InspectCoordinator() == nullptr &&
          driver.InspectTelemetry().CompletedTicks == 0ull,
          "D33: StopAndResetPreview cleanly finalizes LifecycleSequence and releases coordinator");

    Check(driver.StartPreview(rig, &err) && driver.QueryStatus() == PreviewRunStatus::Running,
          "D33: Preview driver cleanly restarts after StopAndResetPreview");
    driver.StepPreview(1.0 / 60.0);
    Check(driver.InspectTelemetry().CompletedTicks >= 1ull,
          "D33: Restarted preview steps cleanly from tick 0");
    driver.StopAndResetPreview();

    std::cout << "ComponentSimulationPreviewValidation: " << gPassed << " passed, " << gFailed << " failed.\n";
    return (gFailed == 0) ? 0 : 1;
}
