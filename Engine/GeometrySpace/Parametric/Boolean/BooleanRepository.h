/*====================================================================================================================================
                                                       BOOLEANCATALOGUE.H
====================================================================================================================================*/
// 🧩 Boolean scenes: argument pairs with closed-form volumes, shared by BooleanValidation and the inspection viewer

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Boolean/GeneralFuse.h"

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Volumes are derived by hand (NaN = no closed form; identities and the membership oracle still apply). Shells and
//    solids are the expected counts of each result (−1 = not asserted); an expected refusal is marked by Shells = 0.
struct BooleanExpectation
{
    double Volume = std::nan("");                                                            // [len³] - Closed form
    int    Solids = -1;                                                                      // [-]    - Solid regions
    int    Shells = -1;                                                                      // [-]    - Shells
};

struct BooleanScene
{
    std::string                       Name        = {};                                      // [-] - Scene label
    std::string                       Description = {};                                      // [-] - What it stresses
    BodyToken                         First       = {};                                      // [-] - Argument A
    BodyToken                         Second      = {};                                      // [-] - Argument B
    double                            FirstVolume  = 0.0;                                    // [len³] - V(A) closed form
    double                            SecondVolume = 0.0;                                    // [len³] - V(B) closed form
    std::array<BooleanExpectation, 4> Expected    = {};                                      // [-] - Per BooleanOperation
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::vector<std::string> EnumerateBooleanScenes();

// Builds both arguments of the named scene into the partition (empty Name on an unknown scene).
[[nodiscard]] BooleanScene ConstructBooleanScene(ModelPartition& Partition, const std::string& Name);

} // namespace Frontier::GeometrySpace::Parametric
