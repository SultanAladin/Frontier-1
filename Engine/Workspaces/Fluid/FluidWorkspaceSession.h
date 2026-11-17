/*============================================================================================================================================
                                                          FLUIDWORKSPACESESSION.H
============================================================================================================================================*/
// 🧩 Native C++ Fluid Studio session owning FluidSimulationSequence, ForceFields, orbit camera, tool mode, and preview framebuffer

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/FluidSpace/FluidCodec.h"
#include "Engine/SimulationSpace/FluidSpace/FluidDescriptors.h"
#include "Engine/SimulationSpace/FluidSpace/FluidPresets.h"
#include "Engine/SimulationSpace/FluidSpace/FluidSimulationSequence.h"
#include "Engine/SimulationSpace/ForceFields/FieldLatticeCascade.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class FluidOutlinerNode : uint8_t
{
    HeroVolume     = 0u,
    WorldDriftGrid = 1u,
    EmitterSource  = 2u,
    SceneObstacle  = 3u,
    ForceFields    = 4u,
    ParticleEmbers = 5u,
    GiLighting     = 6u,
};

enum class FluidInspectorTab : uint8_t
{
    EmitterBlast   = 0u,
    SolverBounds   = 1u,
    ShadingPalette = 2u,
    ForcesGi       = 3u,
};

enum class FluidViewportTool : uint8_t
{
    OrbitCamera    = 0u,
    PyroBrush      = 1u,
    DetonateBlast  = 2u,
    CrossSection   = 3u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FluidOrbitCameraState
{
    float               YawRadians     = 0.65f;
    float               PitchRadians   = 0.32f;
    float               DistanceMeters = 3.85f;
    Mathematics::Axis3D TargetWorld    { 0.0f, 0.0f, 0.85f };
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FLUID WORKSPACE SESSION
//------------------------------------------------------------------------------------------------------------------------

class FluidWorkspaceSession
{
public:
    FluidWorkspaceSession();

    void LoadPreset(std::string_view PresetId);
    void TriggerBlastNow();
    void TriggerReset();
    void StepSimulation(double DeltaSeconds);
    void RenderPreviewFramebuffer(uint32_t WidthPx = 128u, uint32_t HeightPx = 96u);

    [[nodiscard]] bool ExportSceneToml(std::string_view FilePath) const;
    [[nodiscard]] bool ImportSceneToml(std::string_view FilePath);

    [[nodiscard]] SimulationSpace::FluidSpace::FluidSimulationSequence&       AccessSimulation() noexcept       { return Simulation; }
    [[nodiscard]] const SimulationSpace::FluidSpace::FluidSimulationSequence& InspectSimulation() const noexcept { return Simulation; }
    [[nodiscard]] SimulationSpace::ForceFields::ForceFieldAccumulator&        AccessForceFields() noexcept      { return ForceFields; }
    [[nodiscard]] const SimulationSpace::ForceFields::ForceFieldAccumulator&  InspectForceFields() const noexcept { return ForceFields; }

    FluidOrbitCameraState Camera               {};
    FluidOutlinerNode     SelectedNode         = FluidOutlinerNode::HeroVolume;
    FluidInspectorTab     ActiveTab            = FluidInspectorTab::EmitterBlast;
    FluidViewportTool     ActiveTool           = FluidViewportTool::OrbitCamera;
    std::string           ActivePresetId       { "ue5_pyro_default" };
    bool                  PresetBrowserOpen    = false;
    bool                  AtlasMinimapOpen     = false;
    std::string           StatusMessage        { "Ready" };
    uint32_t              PreviewWidthPx       = 0u;
    uint32_t              PreviewHeightPx      = 0u;
    std::vector<uint8_t>  PreviewRgba8         {};

private:
    SimulationSpace::FluidSpace::FluidSimulationSequence Simulation   {};
    SimulationSpace::ForceFields::ForceFieldAccumulator  ForceFields  {};
    SimulationSpace::ForceFields::FieldLatticeCascade    WindLattice  {};
};

} // namespace Frontier::Workspaces
