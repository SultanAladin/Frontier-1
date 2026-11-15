/*==========================================================================================================================================
                                                     DYNAMICMATERIALRECIPE.H
==========================================================================================================================================*/
// 🧩 Authorable non-destructive MaterialLayerStack recipe (FinishLayer, ProceduralFieldLayer, MaskModifierLayer, SurfaceTransitionLayer)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/Dynamic/DynamicMaterialDefinitions.h"
#include "Engine/MaterialSpace/Dynamic/DynamicParameterBlock.h"
#include "Engine/MaterialSpace/SurfaceMaterial.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::MaterialSpace::Dynamic
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct MaterialLayerEntry
{
    std::string            LayerTitle          { "Physical Finish" };
    MaterialLayerCategory  Category            = MaterialLayerCategory::FinishLayer;
    bool                   VisibilityEnabled   = true;
    bool                   SoloEnabled         = false;
    float                  LayerOpacity        = 1.0f;                                          // [0-1]
    uint32_t               ChannelMask         = static_cast<uint32_t>(BlendChannelBitmask::ChannelAllOptical);
    ProceduralFieldPattern FieldPattern        = ProceduralFieldPattern::None;
    MaskModifierMode       MaskMode            = MaskModifierMode::FullSurface;
    ParameterDriverMode    DriverMode          = ParameterDriverMode::ConstantScalar;
    SurfaceMaterial        FinishMaterial      {};                                              // Used by FinishLayer & Transition A
    SurfaceMaterial        SecondaryFinish     {};                                              // Used by SurfaceTransitionLayer B
    float                  ScaleMultiplier     = 1.0f;                                          // [-]
    float                  IntensityMultiplier = 1.0f;                                          // [-]
    float                  SeamGlowStrength    = 0.0f;                                          // [-]
    Mathematics::LinearRGB AccentTint          { 0.13f, 0.72f, 1.00f };                         // [-]
};

struct NamedParameterSlot
{
    std::string         SlotKey      {};
    std::string         DisplayLabel {};
    float               MinimumLimit = 0.0f;
    float               MaximumLimit = 1.0f;
    float               InitialScalar = 0.5f;
    ParameterDriverMode DriverMode   = ParameterDriverMode::ConstantScalar;
};

struct MaterialLayerStack
{
    std::vector<MaterialLayerEntry> Layers {}; // Ordered Bottom -> Top

    void AppendLayer(MaterialLayerEntry Entry);
    bool RemoveLayer(std::size_t Index);
    bool ReorderLayer(std::size_t SourceIndex, std::size_t TargetIndex);
    void ToggleLayerVisibility(std::size_t Index) noexcept;
    void ToggleLayerSolo(std::size_t Index) noexcept;

    [[nodiscard]] bool     QueryLayerEffectiveVisibility(std::size_t Index) const noexcept;
    [[nodiscard]] uint32_t CountVisibleLayers() const noexcept;
    [[nodiscard]] uint64_t ComputeStructuralSignatureHash() const noexcept;
};

struct DynamicMaterialRecipe
{
    std::string                     RecipeId           { "dynamic-finish-default" };
    std::string                     DisplayTitle       { "Dynamic OpenPBR Finish" };
    std::string                     CategoryLabel      { "Dynamic" };
    std::string                     SummaryNote        {};
    DynamicPresetIdentity           PresetIdentity     = DynamicPresetIdentity::LatticeVoxelDissolve;
    SurfaceMaterial                 PrimaryFinish      {};
    SurfaceMaterial                 SecondaryFinish    {};
    MaterialLayerStack              LayerStack         {};
    DynamicParameterBlock           Parameters         {};
    bool                            TransparentSurface = false;
    bool                            DoubleSidedSurface = false;
    std::vector<NamedParameterSlot> ExposedSliders     {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Synchronize ParameterBlock metadata (ActiveLayerCount, ActiveChannelMask, PresetOrdinal) from the recipe's LayerStack
void SynchronizeRecipeParameters(DynamicMaterialRecipe& TargetRecipe) noexcept;

// 📝 Serialize a DynamicMaterialRecipe into deterministic plain-text recipe syntax for disk storage & revision control
[[nodiscard]] std::string SerializeDynamicMaterialRecipe(const DynamicMaterialRecipe& Recipe);

// 📝 Parse a serialized DynamicMaterialRecipe text block back into an exact runtime recipe
[[nodiscard]] bool DeserializeDynamicMaterialRecipe(
    std::string_view       SerializedText,
    DynamicMaterialRecipe& OutRecipe);

} // namespace Frontier::MaterialSpace::Dynamic
