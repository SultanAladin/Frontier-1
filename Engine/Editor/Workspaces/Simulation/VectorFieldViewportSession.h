/*============================================================================================================================================
                                                         VECTORFIELDVIEWPORTSESSION.H
============================================================================================================================================*/
// 🧩 Editor session binding ForceFieldAccumulator, FieldLatticeVulkanExecutor, VectorFieldOverlaySequence & 3-dock UI panes

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/Workspaces/Simulation/ForcePresetBrowserPanel.h"
#include "Engine/Components/Panes/InspectorPanel.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/Panes/ViewportPanel.h"
#include "Engine/Editor/Workspaces/Simulation/ForceInspectorView.h"
#include "Engine/GeometricRaster/Simulation/VectorFieldOverlaySequence.h"
#include "Engine/SimulationSpace/ForceFields/FieldLatticeCascade.h"
#include "Engine/GeometricRaster/Compute/FieldLatticeVulkanExecutor.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::Editor::Simulation
{

//------------------------------------------------------------------------------------------------------------------------
//                                              VECTORFIELDVIEWPORTSESSION
//------------------------------------------------------------------------------------------------------------------------

class VectorFieldViewportSession
{
public:
    VectorFieldViewportSession() noexcept = default;
    ~VectorFieldViewportSession() noexcept = default;

    [[nodiscard]] bool AttachInitializedSession(
        OutlinerPanel&               Outliner,
        ViewportPanel&               Viewport,
        InspectorPanel&              Inspector,
        const std::filesystem::path& ShaderBinaryDirectory,
        const std::filesystem::path& SwiftShaderIcdDir,
        std::string&                 OutDiagnostic) noexcept;

    void StepSimulation(double ΔtSeconds) noexcept;

    [[nodiscard]] OutlinerSlotToken AddForceSourceEntry(
        SimulationSpace::ForceFields::ForceSourceRecordEntry Descriptor,
        OutlinerSlotToken                                    EnclosingFolder = {});

    [[nodiscard]] SimulationSpace::ForceFields::ForceFieldAccumulator&       AccessAccumulator() noexcept;
    [[nodiscard]] const SimulationSpace::ForceFields::ForceFieldAccumulator& QueryAccumulator() const noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::FieldLatticeCascade&         AccessLatticeCascade() noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::FieldLatticeVulkanExecutor&  AccessVulkanExecutor() noexcept;
    [[nodiscard]] VectorFieldOverlaySequence&                                AccessOverlaySequence() noexcept;
    [[nodiscard]] ForceInspectorView&                                        AccessForceInspectorView() noexcept;
    [[nodiscard]] ForcePresetBrowserPanel&                                       AccessContentBrowser() noexcept;
    [[nodiscard]] OverlayCameraBasis&                                        AccessCamera(uint32_t PaneIndex) noexcept;

    [[nodiscard]] int32_t QuerySelectedWaypointIndex() const noexcept;
    void                  ConfigureSelectedWaypointIndex(int32_t WaypointIdx) noexcept;
    [[nodiscard]] uint32_t QueryMappedForceToken(OutlinerSlotToken OutlinerToken) const noexcept;
    [[nodiscard]] double   QuerySimulationTimeSeconds() const noexcept;

private:
    void  SynchronizeForceAccumulator();
    void  RecordAnemometerSample(double SampleTimeSec) noexcept;
    bool  RenderFullForceInspector(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        OutlinerPanel*          Outliner,
        OutlinerRecordEntry*    PrimaryEntry,
        const PaneInputReading& Input,
        double                  Δt);
    ViewportRasterSurface DispatchViewportRaster(const ViewportPaneSurfaceSpecification& Spec);
    void                  ProcessViewportInteraction(uint32_t PaneIndex, Rectangle2D PaneBounds, const PaneInputReading& Input);

    OutlinerPanel*  BoundOutliner  = nullptr;
    ViewportPanel*  BoundViewport  = nullptr;
    InspectorPanel* BoundInspector = nullptr;

    SimulationSpace::ForceFields::ForceFieldAccumulator      Accumulator    {};
    SimulationSpace::ForceFields::FieldLatticeCascade        LatticeCascade {};
    SimulationSpace::ForceFields::FieldLatticeVulkanExecutor VulkanExecutor {};
    VectorFieldOverlaySequence                               OverlaySequence{};
    ForceInspectorView                                       InspectorView  {};
    ForcePresetBrowserPanel                                      ContentBrowser {};

    std::vector<SimulationSpace::ForceFields::ForceSourceRecordEntry> AuthoredSources{};
    OutlinerSlotToken                                                 DefaultForceFolderToken{};
    uint32_t                                                          NextForceToken = 1u;

    std::array<OverlayCameraBasis, 2> Cameras            {};
    std::vector<float>                AnemometerHistoryMs{};
    int32_t                           SelectedWaypointIdx = -1;
    double                            SimulationTimeSec   = 0.0;

    bool     OrbitDragging   = false;
    Point2D  LastDragCursor  {};
    uint32_t DragPaneIndex   = 0u;
};

} // namespace Frontier::Editor::Simulation
