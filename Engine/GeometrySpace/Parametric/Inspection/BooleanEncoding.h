/*====================================================================================================================================
                                                       BOOLEANENCODING.H
====================================================================================================================================*/
// 🧩 General Fuse + Boolean inspection as JSON: result body, history, ghost arguments, all four operations, identities

#pragma once

#include "Engine/GeometrySpace/Parametric/Audit/ModelAudit.h"
#include "Engine/GeometrySpace/Parametric/Boolean/BooleanRepository.h"
#include "Engine/GeometrySpace/Parametric/Boolean/GeneralFuse.h"

#include <array>
#include <string>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// One assembled operation of the scene (all four are assembled so the viewer can show the identities).
struct BooleanInspectionResult
{
    BooleanOutcome Outcome      = {};                                                        // [-]    - Body or refusal
    double         Volume       = 0.0;                                                       // [len³] - Green volume (0 when refused)
    double         Milliseconds = 0.0;                                                       // [ms]   - Selection + assembly
};

struct BooleanInspectionTiming
{
    double ConstructionMilliseconds = 0.0;                                                   // [ms] - Both arguments
    double FuseMilliseconds         = 0.0;                                                   // [ms] - General Fuse
    double AuditMilliseconds        = 0.0;                                                   // [ms] - Audit of the shown result
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::string EncodeBoolean(const ModelPartition&                         Partition,
                                        const BooleanScene&                           Scene,
                                        BooleanOperation                              Shown,
                                        const GeneralFuseOutcome&                     Fuse,
                                        const std::array<BooleanInspectionResult, 4>& Results,
                                        const AuditReport&                            Audit,
                                        const BooleanInspectionTiming&                Timing);

} // namespace Frontier::GeometrySpace::Parametric
