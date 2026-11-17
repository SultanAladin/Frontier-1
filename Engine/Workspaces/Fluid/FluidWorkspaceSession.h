/*============================================================================================================================================
                                                          FLUIDWORKSPACESESSION.H
============================================================================================================================================*/
// 🧩 Native C++ Fluid Studio session owning FluidSimulationSequence, ForceFields, orbit camera, tool mode, modals, and preview framebuffer

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
    EmitterSource  = 1u,
    GiLighting     = 2u,
    SceneObstacle  = 3u,
    ForceFields    = 4u,
    WorldDriftGrid = 5u,
    ParticleEmbers = 6u,
};

enum class FluidOutlinerFilter : uint8_t
{
    All      = 0u,
    Gas      = 1u,
    Geometry = 2u,
    Lights   = 3u,
};

enum class FluidPresetFilter : uint8_t
{
    All   = 0u,
    Fire  = 1u,
    Smoke = 2u,
    Blast = 3u,
};

enum class FluidInspectorTab : uint8_t
{
    Source     = 0u,
    Simulation = 1u,
    Rendering  = 2u,
};

enum class FluidViewportTool : uint8_t
{
    OrbitCamera   = 0u,
    PyroBrush     = 1u,
    DetonateBlast = 2u,
    CrossSection  = 3u,
};

enum class FluidCameraPreset : uint8_t
{
    Perspective = 0u,
    Front       = 1u,
    Side        = 2u,
    Top         = 3u,
};

enum class FluidNavView : uint8_t
{
    Workspace = 0u,
    Presets   = 1u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FluidOrbitCameraState
{
    float               YawRadians     = 0.72f;
    float               PitchRadians   = 0.36f;
    float               DistanceMeters = 4.25f;
    Mathematics::Axis3D TargetWorld    { 0.0f, 0.0f, 0.65f };
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FLUID WORKSPACE SESSION
//------------------------------------------------------------------------------------------------------------------------

class FluidWorkspaceSession
{
public:
    FluidWorkspaceSession();

    void LoadPreset(std::string_view PresetId);
    void ApplyCameraPreset(FluidCameraPreset Preset);
    void TriggerBlastNow();
    void TriggerReset();
    void StepSimulation(double DeltaSeconds);
    void RenderPreviewFramebuffer(uint32_t WidthPx = 160u, uint32_t HeightPx = 120u);

    [[nodiscard]] bool ExportSceneToml(std::string_view FilePath) const;
    [[nodiscard]] bool ImportSceneToml(std::string_view FilePath);

    [[nodiscard]] SimulationSpace::FluidSpace::FluidSimulationSequence&       AccessSimulation() noexcept       { return Simulation; }
    [[nodiscard]] const SimulationSpace::FluidSpace::FluidSimulationSequence& InspectSimulation() const noexcept { return Simulation; }
    [[nodiscard]] SimulationSpace::ForceFields::ForceFieldAccumulator&        AccessForceFields() noexcept      { return ForceFields; }
    [[nodiscard]] const SimulationSpace::ForceFields::ForceFieldAccumulator&  InspectForceFields() const noexcept { return ForceFields; }

    FluidOrbitCameraState     Camera               {};
    FluidCameraPreset         CameraPreset         = FluidCameraPreset::Perspective;
    FluidOutlinerNode         SelectedNode         = FluidOutlinerNode::EmitterSource;
    FluidOutlinerFilter       OutlinerFilter       = FluidOutlinerFilter::All;
    FluidPresetFilter         PresetFilter         = FluidPresetFilter::All;
    FluidInspectorTab         ActiveTab            = FluidInspectorTab::Source;
    FluidViewportTool         ActiveTool           = FluidViewportTool::OrbitCamera;
    FluidNavView              ActiveNavView        = FluidNavView::Workspace;
    std::string               ActivePresetId       { "ue5_pyro_default" };
    std::string               DocumentTitle        { "Ember study" };
    bool                      DocumentDirty        = false;
    bool                      CompactOutliner      = false;
    bool                      PresetBrowserOpen    = false;
    bool                      AtlasMinimapOpen     = false;
    bool                      ShowDiagnostics      = false;
    bool                      ShowDomainBounds     = false;
    bool                      TurntableEnabled     = false;
    bool                      FlipbookModalOpen    = false;
    bool                      HelpModalOpen        = false;
    float                     PlaybackSpeed        = 1.0f;
    uint32_t                  SelectedBurstPattern = 0u; // 0=Single burst, 1=Shrapnel, 2=Salvo, 3=Collide, 4=Mushroom
    uint32_t                  FlipbookFrameCols    = 4u;
    uint32_t                  FlipbookFrameRows    = 4u;
    uint32_t                  FlipbookCellSizePx   = 96u;
    std::string               StatusMessage        { "Simulation ready" };
    uint32_t                  PreviewWidthPx       = 0u;
    uint32_t                  PreviewHeightPx      = 0u;
    std::vector<uint8_t>      PreviewRgba8         {};

private:
    SimulationSpace::FluidSpace::FluidSimulationSequence Simulation   {};
    SimulationSpace::ForceFields::ForceFieldAccumulator  ForceFields  {};
    SimulationSpace::ForceFields::FieldLatticeCascade    WindLattice  {};
};

} // namespace Frontier::Workspaces
