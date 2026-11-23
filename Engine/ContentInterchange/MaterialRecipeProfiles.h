/*==========================================================================================================================================
                                                    MATERIALRECIPEPROFILES.H
==========================================================================================================================================*/
// 🧩 Native port of the Alloy recipe profiles (materialProfiles.js): the designer-visible control set
// for every shipped material family. Values only - the baker and the shader remain the single source
// of truth, so every slider here projects into the same coefficients the HTML controls write.

#pragma once
#include "Engine/ContentInterchange/MaterialLibrary.h"
#include <string_view>
#include <vector>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     MATERIAL RECIPE PROFILES
//------------------------------------------------------------------------------------------------------------------------

struct MaterialRecipeProfileControl
{
    std::string Id, Label, Group;
    bool Direct = false;
    std::string Key;               // direct-control parameter address; empty for macro projections
    float Minimum = 0, Maximum = 1;
    float Step = .01f;
    std::string Unit;
    bool Logarithmic = false, Extend = false;
    std::string Hint;
    std::vector<MaterialRecipeTarget> Targets;
};

struct MaterialRecipeProfile
{
    std::string Identity;          // recipeId as stored in the catalogue SourceRecipe
    uint32_t Type = 0;
    std::string Title, Caption;
    bool Woven = false, SupportsMetalScratches = false;
    std::vector<std::pair<std::string, double>> Fixed;
    std::vector<std::pair<std::string, std::string>> Colours;
    std::vector<MaterialRecipeProfileControl> Controls;
    // 📝 Appended only while the scratch overlay is enabled; keeps its own independent names and
    // ranges so finish and relief tuning never share an address.
    std::vector<MaterialRecipeProfileControl> ScratchControls;
};

// 📝 Null when the recipeId is unknown to this build; callers fall back to the generic scalars.
[[nodiscard]] const MaterialRecipeProfile *QueryMaterialRecipeProfile(std::string_view RecipeId);

// 📝 Base controls plus, when ScratchesEnabled, the overlay the HTML appends for the same material.
[[nodiscard]] std::vector<MaterialRecipeControl> QueryMaterialRecipeControls(const MaterialCatalogueEntry &Entry,
    bool ScratchesEnabled);

} // namespace Frontier
