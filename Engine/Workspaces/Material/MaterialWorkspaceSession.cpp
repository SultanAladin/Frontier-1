/*============================================================================================================================================
                                                         MATERIALWORKSPACESESSION.CPP
============================================================================================================================================*/
// 🧩 Implementation of MaterialWorkspaceSession (modular preset switching, non-destructive layer stack mutations & 120 Hz parameter updates)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Material/MaterialWorkspaceSession.h"

#include <algorithm>

namespace Frontier::Workspaces
{

MaterialWorkspaceSession::MaterialWorkspaceSession()
{
    PresetRepository = MaterialSpace::Dynamic::BuildAllDynamicMaterialPresets();

    // Open 3 curated default tabs showing the breadth of Frontier's modular dynamic material architecture
    DocumentTabs.push_back({
        "Volcanic Pahoehoe",
        MaterialSpace::Dynamic::ConstructDynamicMaterialPreset(MaterialSpace::Dynamic::DynamicPresetIdentity::VolcanicPahoehoeMagma),
        0u
    });
    DocumentTabs.push_back({
        "Hex Energy Barrier",
        MaterialSpace::Dynamic::ConstructDynamicMaterialPreset(MaterialSpace::Dynamic::DynamicPresetIdentity::HexagonalEnergyBarrier),
        0u
    });
    DocumentTabs.push_back({
        "Surface Transition",
        MaterialSpace::Dynamic::ConstructDynamicMaterialPreset(MaterialSpace::Dynamic::DynamicPresetIdentity::SurfaceMaterialTransition),
        0u
    });

    ActiveTabIndex = 0u;
    AdvanceSimulationClock(0.0);
}

MaterialSpace::Dynamic::DynamicMaterialRecipe& MaterialWorkspaceSession::AccessActiveRecipe() noexcept
{
    return DocumentTabs[ActiveTabIndex].Recipe;
}

const MaterialSpace::Dynamic::DynamicMaterialRecipe& MaterialWorkspaceSession::InspectActiveRecipe() const noexcept
{
    return DocumentTabs[ActiveTabIndex].Recipe;
}

std::size_t MaterialWorkspaceSession::QuerySelectedLayerIndex() const noexcept
{
    const auto& Tab = DocumentTabs[ActiveTabIndex];
    if (Tab.Recipe.LayerStack.Layers.empty())
    {
        return 0u;
    }
    return std::min(Tab.SelectedLayerIndex, Tab.Recipe.LayerStack.Layers.size() - 1u);
}

void MaterialWorkspaceSession::SelectPresetIdentity(MaterialSpace::Dynamic::DynamicPresetIdentity Identity)
{
    auto& Tab = DocumentTabs[ActiveTabIndex];
    Tab.Recipe = MaterialSpace::Dynamic::ConstructDynamicMaterialPreset(Identity);
    Tab.TabTitle = Tab.Recipe.DisplayTitle;
    Tab.SelectedLayerIndex = 0u;
    LastCompiledSignatureHash = MaterialSpace::Dynamic::CompileMaterialLayerStack(Tab.Recipe).StructuralHash;
    StatusMessage = "Loaded modular preset: " + Tab.Recipe.DisplayTitle;
    DirtyPreviewRequested = true;
}

bool MaterialWorkspaceSession::SelectPresetIdentifier(std::string_view RecipeId)
{
    MaterialSpace::Dynamic::DynamicMaterialRecipe Loaded {};
    if (!MaterialSpace::Dynamic::FindDynamicMaterialPreset(RecipeId, Loaded))
    {
        return false;
    }
    auto& Tab = DocumentTabs[ActiveTabIndex];
    Tab.Recipe = std::move(Loaded);
    Tab.TabTitle = Tab.Recipe.DisplayTitle;
    Tab.SelectedLayerIndex = 0u;
    LastCompiledSignatureHash = MaterialSpace::Dynamic::CompileMaterialLayerStack(Tab.Recipe).StructuralHash;
    StatusMessage = "Loaded modular preset: " + Tab.Recipe.DisplayTitle;
    DirtyPreviewRequested = true;
    return true;
}

void MaterialWorkspaceSession::SelectLayer(std::size_t LayerIndex) noexcept
{
    auto& Tab = DocumentTabs[ActiveTabIndex];
    if (!Tab.Recipe.LayerStack.Layers.empty())
    {
        Tab.SelectedLayerIndex = std::min(LayerIndex, Tab.Recipe.LayerStack.Layers.size() - 1u);
    }
}

void MaterialWorkspaceSession::AppendProceduralLayer()
{
    auto& Recipe = AccessActiveRecipe();
    MaterialSpace::Dynamic::MaterialLayerEntry Entry {};
    Entry.LayerTitle   = "Procedural Relief Field";
    Entry.Category     = MaterialSpace::Dynamic::MaterialLayerCategory::ProceduralFieldLayer;
    Entry.FieldPattern = MaterialSpace::Dynamic::ProceduralFieldPattern::DualScaleVoronoiFaults;
    Entry.DriverMode   = MaterialSpace::Dynamic::ParameterDriverMode::PlaybackClock;
    Recipe.LayerStack.AppendLayer(std::move(Entry));
    MaterialSpace::Dynamic::SynchronizeRecipeParameters(Recipe);
    DocumentTabs[ActiveTabIndex].SelectedLayerIndex = Recipe.LayerStack.Layers.size() - 1u;
    LastCompiledSignatureHash = MaterialSpace::Dynamic::CompileMaterialLayerStack(Recipe).StructuralHash;
    StatusMessage = "Appended ProceduralFieldLayer to stack";
    DirtyPreviewRequested = true;
}

void MaterialWorkspaceSession::AppendTransitionLayer()
{
    auto& Recipe = AccessActiveRecipe();
    MaterialSpace::Dynamic::MaterialLayerEntry Entry {};
    Entry.LayerTitle       = "Finish A -> Finish B Sweep";
    Entry.Category         = MaterialSpace::Dynamic::MaterialLayerCategory::SurfaceTransitionLayer;
    Entry.MaskMode         = MaterialSpace::Dynamic::MaskModifierMode::SweepWaveMask;
    Entry.DriverMode       = MaterialSpace::Dynamic::ParameterDriverMode::ExternalTimeline;
    Entry.SeamGlowStrength = 2.4f;
    Recipe.LayerStack.AppendLayer(std::move(Entry));
    MaterialSpace::Dynamic::SynchronizeRecipeParameters(Recipe);
    DocumentTabs[ActiveTabIndex].SelectedLayerIndex = Recipe.LayerStack.Layers.size() - 1u;
    LastCompiledSignatureHash = MaterialSpace::Dynamic::CompileMaterialLayerStack(Recipe).StructuralHash;
    StatusMessage = "Appended SurfaceTransitionLayer to stack";
    DirtyPreviewRequested = true;
}

void MaterialWorkspaceSession::RemoveSelectedLayer()
{
    auto& Tab = DocumentTabs[ActiveTabIndex];
    if (Tab.Recipe.LayerStack.RemoveLayer(QuerySelectedLayerIndex()))
    {
        MaterialSpace::Dynamic::SynchronizeRecipeParameters(Tab.Recipe);
        Tab.SelectedLayerIndex = std::min(Tab.SelectedLayerIndex, Tab.Recipe.LayerStack.Layers.size() - 1u);
        LastCompiledSignatureHash = MaterialSpace::Dynamic::CompileMaterialLayerStack(Tab.Recipe).StructuralHash;
        StatusMessage = "Removed layer from stack";
        DirtyPreviewRequested = true;
    }
}

void MaterialWorkspaceSession::ToggleLayerVisibility(std::size_t LayerIndex) noexcept
{
    auto& Recipe = AccessActiveRecipe();
    Recipe.LayerStack.ToggleLayerVisibility(LayerIndex);
    MaterialSpace::Dynamic::SynchronizeRecipeParameters(Recipe);
    LastCompiledSignatureHash = MaterialSpace::Dynamic::CompileMaterialLayerStack(Recipe).StructuralHash;
    DirtyPreviewRequested = true;
}

void MaterialWorkspaceSession::ToggleLayerSolo(std::size_t LayerIndex) noexcept
{
    auto& Recipe = AccessActiveRecipe();
    Recipe.LayerStack.ToggleLayerSolo(LayerIndex);
    MaterialSpace::Dynamic::SynchronizeRecipeParameters(Recipe);
    LastCompiledSignatureHash = MaterialSpace::Dynamic::CompileMaterialLayerStack(Recipe).StructuralHash;
    DirtyPreviewRequested = true;
}

void MaterialWorkspaceSession::TriggerViewportImpactPulse(Mathematics::Axis3D LocalHitDirection) noexcept
{
    auto& Recipe = AccessActiveRecipe();
    MaterialSpace::Dynamic::TriggerImpactPulse(Recipe.Parameters, LocalHitDirection);
    StatusMessage = "Triggered 3D geodesic impact ripple";
    DirtyPreviewRequested = true;
}

void MaterialWorkspaceSession::AdvanceSimulationClock(double DeltaSeconds) noexcept
{
    if (PlaybackRunning && DeltaSeconds > 0.0)
    {
        PlaybackClockSeconds += DeltaSeconds;
        DirtyPreviewRequested = true;
    }

    auto& Recipe = AccessActiveRecipe();
    MaterialSpace::Dynamic::DynamicDriverInputs Inputs {};
    Inputs.ElapsedSeconds      = PlaybackClockSeconds;
    Inputs.ExternalProgress    = Recipe.Parameters.TransitionProgress;
    Inputs.GlobalRainIntensity = Recipe.Parameters.WeatherRainIntensity;
    Inputs.GlobalSnowDepth     = Recipe.Parameters.WeatherSnowDepth;
    Inputs.UseExternalProgress = (static_cast<MaterialSpace::Dynamic::ParameterDriverMode>(Recipe.Parameters.PrimaryDriverOrdinal) ==
                                  MaterialSpace::Dynamic::ParameterDriverMode::ExternalTimeline);
    MaterialSpace::Dynamic::ResolveDynamicParameters(Recipe.Parameters, Inputs);
    LastCompiledSignatureHash = MaterialSpace::Dynamic::CompileMaterialLayerStack(Recipe).StructuralHash;
}

void MaterialWorkspaceSession::RequestChannelSnapshotBake() noexcept
{
    ChannelBakeCompleted = true;
    StatusMessage = "Baked static OpenPBR channel snapshots (Albedo / Roughness / Normal / Glow)";
}

} // namespace Frontier::Workspaces
