/*====================================================================================================================================
                                                      SPECIMENCATALOGUE.H
====================================================================================================================================*/
// 🧩 Named specimen bodies with independently derived expectations (counts, genus, analytic volume) for the gates

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Topology/ModelPartition.h"

#include <map>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct SpecimenParameters
{
    std::map<std::string, double> Values = {};                                               // [-] - Caller overrides

    [[nodiscard]] double Resolve(const std::string& Key, double Default) const
    {
        const auto Found = Values.find(Key);
        return Found == Values.end() ? Default : Found->second;
    }
};

// 📝 Expectations are derived by hand from the construction (not read back from the model), so they can disagree.
struct SpecimenExpectation
{
    int    Vertices = 0;                                                                     // [-]    - V
    int    Edges    = 0;                                                                     // [-]    - E
    int    Faces    = 0;                                                                     // [-]    - F
    int    Loops    = 0;                                                                     // [-]    - L
    int    Shells   = 0;                                                                     // [-]    - S
    int    Genus    = 0;                                                                     // [-]    - G
    double Volume   = 0.0;                                                                   // [len³] - Closed-form volume
};

struct SpecimenOutcome
{
    BodyToken                     Body        = {};                                          // [-] - Constructed body
    std::string                   Refusal     = {};                                          // [-] - Non-empty when construction refused
    SpecimenExpectation           Expectation = {};                                          // [-] - Independent expectations
    std::map<std::string, double> Parameters  = {};                                          // [-] - Parameters actually used
    std::string                   Description = {};                                          // [-] - What the specimen exercises
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::vector<std::string> EnumerateSpecimens();

[[nodiscard]] SpecimenOutcome ConstructSpecimen(ModelPartition& Partition, const std::string& Name, const SpecimenParameters& Parameters);

} // namespace Frontier::GeometrySpace::Parametric
