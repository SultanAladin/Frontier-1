/*====================================================================================================================================
                                                     INSPECTIONENCODING.H
====================================================================================================================================*/
// 🧩 One-line JSON description of a body for the HTML inspection viewport (topology tree, exact samples, audit)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Audit/ModelAudit.h"
#include "Engine/GeometrySpace/Parametric/Inspection/JsonWriter.h"
#include "Engine/GeometrySpace/Parametric/Inspection/SpecimenCatalogue.h"

#include <string>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct InspectionTiming
{
    double ConstructionMilliseconds = 0.0;                                                   // [ms] - Euler edits + geometry
    double AuditMilliseconds        = 0.0;                                                   // [ms] - Full audit
    double VolumeMilliseconds       = 0.0;                                                   // [ms] - Green volume
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Curves and surfaces are sampled from the exact geometry (edge polylines and trimmed iso-parameter hatching),
//    never from a tessellation — the viewport shows what the model IS. Tessellation is a later bone (research § N).
[[nodiscard]] std::string EncodeInspection(const ModelPartition&  Partition,
                                           const std::string&     SpecimenName,
                                           const SpecimenOutcome& Specimen,
                                           const AuditReport&     Audit,
                                           double                 Volume,
                                           const InspectionTiming& Timing);

// Shared JSON sections (also used by the Boolean encoder): audit tallies + findings; tree, vertices, edges, hatched faces.
void EncodeAuditSection(JsonWriter& Writer, const AuditReport& Audit);
void EncodeBodyGeometry(JsonWriter& Writer, const ModelPartition& Partition, BodyToken Body);

[[nodiscard]] std::string EncodeRefusal(const std::string& SpecimenName, const std::string& Refusal);

[[nodiscard]] std::string EncodeCatalogue();

} // namespace Frontier::GeometrySpace::Parametric
