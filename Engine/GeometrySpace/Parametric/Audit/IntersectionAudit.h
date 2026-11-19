/*====================================================================================================================================
                                                     INTERSECTIONAUDIT.H
====================================================================================================================================*/
// 🧩 Audit of a surface–surface intersection against the surfaces themselves and an independent dense reference lattice

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Audit/ModelAudit.h"
#include "Engine/GeometrySpace/Parametric/Inspection/IntersectionRepository.h"

#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct IntersectionAuditReport
{
    std::vector<AuditTally>   Tallies          = {};                                         // [-]   - I1 … I6
    std::vector<AuditFinding> Findings         = {};                                         // [-]   - Every failure
    double                    MaximumResidual  = 0.0;                                        // [len] - Worst chart point off a surface
    double                    MeasuredLength   = 0.0;                                        // [len] - Σ polyline lengths
    double                    LengthError      = 0.0;                                        // [-]   - Relative, NaN without reference
    int                       ReferencePoints  = 0;                                          // [-]   - Dense lattice crossings
    int                       ReferenceMisses  = 0;                                          // [-]   - Not near any branch / point
    double                    ReferenceReach   = 0.0;                                        // [len] - Worst reference → polyline distance
    int                       ClosedBranches   = 0;                                          // [-]   - Closed branch count

    [[nodiscard]] bool SoundCondition() const noexcept { return Findings.empty(); }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 I1 chart points on both surfaces · I2 chart points inside both boxes · I3 completeness: every crossing of a DENSE
//    iso-lattice on BOTH surfaces (48 × 48, independent of the tracer and of the solver's own 8 × 8 lattice) lies near a
//    branch or special point · I4 terminations consistent (no budget exhaustion; singular ends on special points) ·
//    I5 counts match expectations · I6 total length matches the closed-form / quadrature reference.
[[nodiscard]] IntersectionAuditReport AuditIntersection(const IntersectionScene& Scene, const SurfaceIntersectionOutcome& Outcome, int ReferenceDivisions = 48);

} // namespace Frontier::GeometrySpace::Parametric
