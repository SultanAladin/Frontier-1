/*==========================================================================================================================================
                                                   DYNAMICMATERIALREPOSITORY.H
==========================================================================================================================================*/
// 🧩 Curated repository of 13 modular, asset-agnostic dynamic & procedural OpenPBR material recipes

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/Dynamic/DynamicMaterialRecipe.h"

#include <string_view>
#include <vector>

namespace Frontier::MaterialSpace::Dynamic
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Construct any of the 13 built-in modular dynamic material recipes (attaches cleanly to any 3D asset)
[[nodiscard]] DynamicMaterialRecipe ConstructDynamicMaterialPreset(DynamicPresetIdentity Identity);

// 📝 Return all 13 built-in modular dynamic material recipes in showcase order
[[nodiscard]] std::vector<DynamicMaterialRecipe> BuildAllDynamicMaterialPresets();

// 📝 Look up a built-in dynamic material preset by its slug identifier
[[nodiscard]] bool FindDynamicMaterialPreset(
    std::string_view       RecipeId,
    DynamicMaterialRecipe& OutRecipe);

} // namespace Frontier::MaterialSpace::Dynamic
