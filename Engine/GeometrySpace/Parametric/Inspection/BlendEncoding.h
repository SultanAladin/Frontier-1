/*====================================================================================================================================
                                                       BLENDENCODING.H
====================================================================================================================================*/
// 🧩 Blend result → JSON: shown body geometry, face provenance, spines / contacts and the closed-form volume check

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Audit/ModelAudit.h"
#include "Engine/GeometrySpace/Parametric/Blend/BlendRepository.h"
#include "Engine/GeometrySpace/Parametric/Blend/EdgeBlend.h"

#include <string>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct BlendInspection
{
    BlendSettings Applied            = {};                                                   // [-]    - Settings actually used
    bool          SourceShown        = false;                                                // [-]    - Viewer asked for the unblended body
    double        SourceVolume       = 0.0;                                                  // [len³] - V before
    double        ShownVolume        = 0.0;                                                  // [len³] - V of the shown body
    double        ConstructionMilliseconds = 0.0;                                            // [ms]   - Specimen construction
    double        AuditMilliseconds  = 0.0;                                                  // [ms]   - Audit of the shown body
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 The shown body is the blend result, or the source when the viewer asks for it or the blend refused (the refusal
//    travels in Blend.Refusal). ΔV* is reported only at the catalogue size — a user-chosen size has no closed form here.
[[nodiscard]] std::string EncodeBlend(const ModelPartition& Partition, const BlendSpecimen& Specimen, const BlendOutcome& Outcome, BodyToken Shown,
                                      const AuditReport& Audit, const BlendInspection& Inspection);

} // namespace Frontier::GeometrySpace::Parametric
