/*============================================================================================================================================
                                                  WORLDSIMULATIONSESSIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase D34-D35 Validation of WorldSimulationSession (EditorLifecycle ComponentDefinitions + WorldPlacements launch
//    AND direct .VolumetricSpace scene file launch across all 5 coupled simulation domains).

#include "Engine/SimulationSpace/WorldSimulationSession.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

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

    // ---------------------------------------------------------------------------------------------------------
    // D34: Launch directly from EditorLifecycle-style ComponentDefinitions vector + WorldPlacements
    // ---------------------------------------------------------------------------------------------------------
    std::vector<ComponentClassDefinition> classDefs;
    classDefs.push_back(ComponentClassSimulationAssembler::BuildTracksideRigComponentClass());

    // Second class: standalone chain + radial gust prop
    ComponentClassDefinition chainClass {};
    chainClass.Title = "HangingChainRig";
    chainClass.ClassArchetype = ComponentClassArchetype::DynamicRig;
    chainClass.Storage.ResetWithSpatialOrigin("ChainOrigin");
    const auto chainTok = chainClass.Storage.AppendInstance(
        ComponentCategory::SoftBodyInstance, "SteelChain", chainClass.Storage.QueryOriginToken(),
        { { 0.0f, 2.2f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
    if (auto* sb = chainClass.Storage.ResolveSoftBody(chainTok))
    {
        sb->SoftBodyAssetPath = "EngineContent/Simulation/Deformables/CablesChains.SoftBody";
    }
    classDefs.push_back(std::move(chainClass));

    ComponentWorldPlacementRegister worldPlacements {};
    (void)worldPlacements.SpawnPlacement(
        ComponentClassToken { 1u, 1u }, "TracksideRig_01",
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
    (void)worldPlacements.SpawnPlacement(
        ComponentClassToken { 2u, 1u }, "HangingChain_01",
        { { -3.0f, 0.0f, 1.2f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });

    WorldSimulationSession session {};
    std::string err;
    Check(session.BeginPlayFromWorldPlacements(classDefs, worldPlacements, "FrontierWorldTest", &err) &&
          session.QueryPlaying(),
          "D34: WorldSimulationSession launches live 5-domain Play from multi-class WorldPlacements");
    Check(session.InspectAssemblyReport().PlacementsProcessed == 2u &&
          session.InspectAssemblyReport().SoftBodiesAssembled == 3u &&
          session.InspectAssemblyReport().ForcesAssembled == 1u &&
          session.InspectAssemblyReport().FluidSourcesAssembled == 1u &&
          session.InspectAssemblyReport().CloudsAssembled == 1u,
          "D34: Multi-class WorldPlacements assembly counts match both TracksideRig & HangingChainRig");

    for (uint32_t i = 0u; i < 20u; ++i)
    {
        session.Tick(1.0 / 60.0);
    }
    const auto telPlay = session.InspectTelemetry();
    Check(telPlay.CompletedTicks >= 19ull && telPlay.ActiveDeformableCount == 3u &&
          telPlay.FluidPeakDensity > 0.10f && telPlay.ActiveParticleCount > 80u &&
          telPlay.DeformableRigidContacts > 0u,
          "D34: Live WorldSimulationSession steps all 5 domains + 3 multi-class SoftBodies cleanly");

    // Pause -> frozen -> Resume -> continues
    Check(session.Pause() && session.QueryStatus() == PreviewRunStatus::Paused, "D34: Pause suspends world simulation");
    session.Tick(1.0 / 60.0);
    Check(session.InspectTelemetry().CompletedTicks == telPlay.CompletedTicks, "D34: Tick while Paused keeps ticks frozen");
    Check(session.Resume() && session.QueryPlaying(), "D34: Resume restores Running world simulation");
    session.Tick(1.0 / 60.0);
    Check(session.InspectTelemetry().CompletedTicks > telPlay.CompletedTicks, "D34: Resumed world advances cleanly");

    // Deformable surface extraction & pure-TOML scene export
    const auto surfaces = session.ExtractDeformableRenderSurfaces();
    bool surfacesOk = (surfaces.size() == 3u);
    for (const auto& s : surfaces)
    {
        if (s.Surface.Positions.empty() || s.Surface.Indices.empty()) surfacesOk = false;
    }
    Check(surfacesOk, "D34: ExtractDeformableRenderSurfaces builds live 3D surfaces for all 3 multi-class SoftBodies");

    const std::string exportedToml = session.ExportActiveSceneToToml();
    Check(exportedToml.find("HangingChain_01::SteelChain") != std::string::npos &&
          exportedToml.find("TracksideRig_01::TrackBannerCloth") != std::string::npos,
          "D34: ExportActiveSceneToToml serializes active multi-class world cleanly");

    session.Stop();
    Check(session.QueryStatus() == PreviewRunStatus::Idle && session.InspectCoordinator() == nullptr,
          "D34: Stop tears down world simulation cleanly");

    // ---------------------------------------------------------------------------------------------------------
    // D35: Launch directly from shipped .VolumetricSpace scene file on disk
    // ---------------------------------------------------------------------------------------------------------
    Check(session.BeginPlayFromSceneFile("EngineContent/Simulation/Scenes/TracksideCoupledShowcase.VolumetricSpace", &err) &&
          session.QueryPlaying(),
          "D35: BeginPlayFromSceneFile mounts shipped TracksideCoupledShowcase.VolumetricSpace cleanly");
    for (uint32_t i = 0u; i < 20u; ++i)
    {
        session.Tick(1.0 / 60.0);
    }
    const auto telScene = session.InspectTelemetry();
    Check(telScene.CompletedTicks >= 19ull && telScene.ActiveForceCount == 2u &&
          telScene.FluidPeakDensity > 0.10f && telScene.ActiveParticleCount > 80u &&
          telScene.ActiveDeformableCount == 2u && telScene.DeformableRigidContacts > 0u,
          "D35: Shipped .VolumetricSpace scene runs all 5 coupled domains live inside WorldSimulationSession");
    session.Stop();

    std::cout << "WorldSimulationSessionValidation: " << gPassed << " passed, " << gFailed << " failed.\n";
    return (gFailed == 0) ? 0 : 1;
}
