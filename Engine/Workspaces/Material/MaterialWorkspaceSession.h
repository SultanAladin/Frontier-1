/*============================================================================================================================================
                                                          MATERIALWORKSPACESESSION.H
============================================================================================================================================*/
// 🧩 Authoring session for Frontier's Material Workspace (modular dynamic recipes, layer stack editing, playback & multi-renderer config)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/Dynamic/DynamicMaterialCompiler.h"
#include "Engine/MaterialSpace/Dynamic/DynamicMaterialRecipe.h"
#include "Engine/MaterialSpace/Dynamic/DynamicMaterialRepository.h"
#include "Engine/Workspaces/Material/MaterialPreviewRenderer.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct MaterialDocumentTab
{
    std::string                                   TabTitle {};
    MaterialSpace::Dynamic::DynamicMaterialRecipe Recipe   {};
    std::size_t                                   SelectedLayerIndex = 0u;
};

struct MaterialWorkspaceSession
{
    std::vector<MaterialSpace::Dynamic::DynamicMaterialRecipe> PresetRepository {};
    std::vector<MaterialDocumentTab>                           DocumentTabs     {};
    std::size_t                                                ActiveTabIndex   = 0u;
    std::string                                                CategoryFilter   { "All" };

    // Live turntable playback & preview controls
    bool                    PlaybackRunning      = true;
    double                  PlaybackClockSeconds = 1.45;
    PreviewSpecimenGeometry ActiveSpecimen       = PreviewSpecimenGeometry::ShaderballPedestal;
    PreviewRenderMode       ActiveRenderMode     = PreviewRenderMode::DirectStudioOpenPbr;
    PreviewCameraSetup      Camera               {};

    // Telemetry & diagnostics
    uint64_t LastCompiledSignatureHash = 0u;
    uint32_t LastRenderWidthPx         = 0u;
    uint32_t LastRenderHeightPx        = 0u;
    float    LastRenderMillisec        = 0.0f;
    uint32_t LastActiveLobeMask        = 0u;
    bool     LastRenderUsedGpu         = false;
    bool     DirtyPreviewRequested     = true;
    bool     ChannelBakeCompleted      = false;
    std::string StatusMessage          { "Direct Studio OpenPBR Ready (No GI)" };

    MaterialWorkspaceSession();

    [[nodiscard]] MaterialSpace::Dynamic::DynamicMaterialRecipe&       AccessActiveRecipe() noexcept;
    [[nodiscard]] const MaterialSpace::Dynamic::DynamicMaterialRecipe& InspectActiveRecipe() const noexcept;
    [[nodiscard]] std::size_t                                          QuerySelectedLayerIndex() const noexcept;

    void SelectPresetIdentity(MaterialSpace::Dynamic::DynamicPresetIdentity Identity);
    bool SelectPresetIdentifier(std::string_view RecipeId);
    void SelectLayer(std::size_t LayerIndex) noexcept;
    void AppendProceduralLayer();
    void AppendTransitionLayer();
    void RemoveSelectedLayer();
    void ToggleLayerVisibility(std::size_t LayerIndex) noexcept;
    void ToggleLayerSolo(std::size_t LayerIndex) noexcept;
    void TriggerViewportImpactPulse(Mathematics::Axis3D LocalHitDirection) noexcept;
    void AdvanceSimulationClock(double DeltaSeconds) noexcept;
    void RequestChannelSnapshotBake() noexcept;
};

} // namespace Frontier::Workspaces
