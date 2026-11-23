/*====================================================================================================================================
                                                     ASSEMBLYANALYSIS.H
====================================================================================================================================*/
// 🧩 Assembly checks (priority 11, A1): mass properties of several bodies with densities, clearance and interference

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Measurement/MassProperties.h"
#include "Engine/GeometrySpace/Parametric/Connectivity/ModelPartition.h"

#include <array>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENUMS
//------------------------------------------------------------------------------------------------------------------------

enum class InterferenceCondition : uint8_t
{
    Clear     = 0,                                                                           // Apart by Clearance > 0
    Contact   = 1,                                                                           // Touching, no shared volume
    Overlap   = 2,                                                                           // Shared volume (Body = the overlap)
    Contained = 3,                                                                           // One body inside the other (Body = the inner one)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ArrangementBody
{
    BodyToken Body    = {};                                                                  // [-]          - Solid body
    double    Density = 1.0;                                                                 // [mass/len³]  - Material density
};

// 📝 Mass-weighted totals: Inertia = Σ ρᵢ ∭ (|r|² E − r rᵀ) dV about the common centre of mass (Ixx Iyy Izz Ixy Iyz Izx).
struct ArrangementMassProperties
{
    double                Mass          = 0.0;                                               // [mass]      - Σ ρᵢ Vᵢ
    double                Volume        = 0.0;                                               // [len³]      - Σ Vᵢ
    double                SurfaceArea   = 0.0;                                               // [len²]      - Σ Aᵢ
    Vector3               CentreOfMass  = {};                                                // [len]       - Σ mᵢ cᵢ / M
    std::array<double, 6> Inertia       = {};                                                // [mass·len²] - About the centre of mass
    std::string           Refusal       = {};                                                // [-]         - Empty on success
};

struct BodyClearance
{
    double      Distance = 0.0;                                                              // [len] - Smallest face-to-face distance
    Vector3     First    = {};                                                               // [len] - Closest point on the first body
    Vector3     Second   = {};                                                               // [len] - Closest point on the second body
    int         Pairs    = 0;                                                                // [-]   - Face pairs measured
    std::string Refusal  = {};                                                               // [-]   - Empty on success
};

struct InterferenceMeasure
{
    InterferenceCondition Condition = InterferenceCondition::Clear;                          // [-]    - Verdict
    double                Volume    = 0.0;                                                   // [len³] - Shared volume (overlap / contained)
    double                Clearance = 0.0;                                                   // [len]  - Gap when clear
    BodyToken             Body      = {};                                                    // [-]    - Overlap body (new) or the contained body
    Vector3               First     = {};                                                    // [len]  - Closest points when clear / touching
    Vector3               Second    = {};                                                    // [len]
    std::string           Refusal   = {};                                                    // [-]    - Empty on success
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Combines each body's exact unit-density properties (MeasureBodyMass) — no re-integration: M = Σ ρᵢVᵢ, the centre of
//    mass is the mass-weighted centroid and every tensor moves to it by the parallel-axis theorem
//    I = Σ ρᵢ (Iᵢ + Vᵢ(|d|² E − d dᵀ)), d = cᵢ − c. Refused for an empty list, a non-positive density or any refused body.
[[nodiscard]] ArrangementMassProperties MeasureAssemblyMass(const ModelPartition& Partition, const std::vector<ArrangementBody>& Members);

// Smallest distance between the faces of two bodies (MeasureMinimumDistance over every face pair, stopping at contact).
[[nodiscard]] BodyClearance MeasureBodyClearance(const ModelPartition& Partition, BodyToken First, BodyToken Second);

// 💡 Interference between two solids (Fusion "Interference"): faces apart → Clear (gap = the clearance) unless one body
//    lies inside the other (Contained, volume = its volume, by the winding number of one of its vertices); faces meeting
//    → the exact Boolean intersection: an empty result is Contact, else Overlap with the intersection body and its volume.
[[nodiscard]] InterferenceMeasure DetectBodyInterference(ModelPartition& Partition, BodyToken First, BodyToken Second);

[[nodiscard]] const char* InterferenceConditionLabel(InterferenceCondition Condition) noexcept;

} // namespace Frontier::GeometrySpace::Parametric
