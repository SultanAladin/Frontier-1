/*============================================================================================================================================
                                                          DYNAMICMATERIALCOMPILER.H
============================================================================================================================================*/
// 🧩 Lowers a linear MaterialLayerStack into a flat MaterialInstructionSequence & Slang snippet (zero forced node authoring)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/Dynamic/DynamicMaterialRecipe.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::MaterialSpace::Dynamic
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct MaterialInstructionOp
{
    MaterialLayerCategory  Category            = MaterialLayerCategory::FinishLayer;
    ProceduralFieldPattern FieldPattern        = ProceduralFieldPattern::None;
    MaskModifierMode       MaskMode            = MaskModifierMode::FullSurface;
    uint32_t               ChannelMask         = 0xFFu;
    uint32_t               SourceLayerIndex    = 0u;
};

struct CompiledMaterialSequence
{
    uint64_t                           StructuralHash   = 0u; // Cache key: unchanged when numeric sliders move
    std::vector<MaterialInstructionOp> Instructions     {};
    std::string                        GeneratedSlang   {};
    bool                               RequiresAlphaCut = false;
    bool                               RequiresTransm   = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Compile any MaterialLayerStack into an immutable flat instruction sequence + Slang dispatch snippet
[[nodiscard]] CompiledMaterialSequence CompileMaterialLayerStack(const DynamicMaterialRecipe& Recipe);

} // namespace Frontier::MaterialSpace::Dynamic
