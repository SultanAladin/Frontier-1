/*====================================================================================================================================
                                                       FEATURECATALOGUE.H
====================================================================================================================================*/
// 🧩 Feature-tree specimens with closed-form volumes, parameter sweeps and expected refusals

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Feature/FeatureTree.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

using FeatureAssignment = std::map<std::string, double>;

// 📝 Sweep[0] is the authoring configuration (references are captured there); later entries re-regenerate the same
//    tree with only the listed parameters changed. The oracles read the tree's current parameter values.
struct FeatureSpecimen
{
    FeatureTree                                                 Tree             = {};      // [-]     - Authored tree
    std::vector<FeatureAssignment>                              Sweep            = {};      // [-]     - Parameter sets to regenerate at
    std::function<double(const FeatureTree&)>                   ExpectedVolume   = {};      // [len³]  - Closed form
    std::function<std::vector<std::string>(const FeatureTree&)> ExpectedRefusals = {};      // [-]     - Step labels expected refused
    std::string                                                 Oracle           = {};      // [-]     - Formula text for the exhibit
    std::function<std::vector<Vector3>(const FeatureTree&)>     ExpectedVertices = {};      // [len]   - Points that must be vertices of the final body
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::vector<std::string> EnumerateFeatureSpecimens();

// Empty tree name when unknown.
[[nodiscard]] FeatureSpecimen BuildFeatureSpecimen(const std::string& Name);

// Applies an assignment; false when a key names no parameter.
bool AssignFeatureParameters(FeatureTree& Tree, const FeatureAssignment& Assignment);

[[nodiscard]] double FeatureParameterValue(const FeatureTree& Tree, const std::string& Name);

} // namespace Frontier::GeometrySpace::Parametric
