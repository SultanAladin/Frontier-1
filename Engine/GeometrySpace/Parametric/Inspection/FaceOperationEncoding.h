/*====================================================================================================================================
                                                       FACEOPERATIONENCODING.H
====================================================================================================================================*/
// 🧩 Face-operation result → JSON: shown body geometry, applied steps, face provenance and the closed-form volume check

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Audit/ModelAudit.h"
#include "Engine/GeometrySpace/Parametric/Local/FaceOperationCatalogue.h"
#include "Engine/GeometrySpace/Parametric/Local/FaceOperations.h"

#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct FaceOperationInspection
{
    std::vector<FaceOperationSettings> Applied                  = {};                      // [-]    - Steps actually run (Amount applied)
    double                             Amount                   = 1.0;                       // [-]    - Multiplier on the last step
    bool                               SourceShown              = false;                     // [-]    - Viewer asked for the body before
    int                                CompletedSteps           = 0;                         // [-]    - Steps that succeeded
    double                             SourceVolume             = 0.0;                       // [len³] - V before
    double                             ShownVolume              = 0.0;                       // [len³] - V of the shown body (NaN: sheet)
    double                             OperationMilliseconds    = 0.0;                       // [ms]   - Σ step wall time
    double                             ConstructionMilliseconds = 0.0;                       // [ms]   - Specimen construction
    double                             AuditMilliseconds        = 0.0;                       // [ms]   - Audit of the shown body
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 The shown body is the result, or the source when the viewer asks for it or a step refused (the refusal travels in
//    FaceOperation.Refusal). V* is reported only at Amount = 1 — other amounts have no hand-derived closed form here.
[[nodiscard]] std::string EncodeFaceOperation(const ModelPartition& Partition, const FaceOperationSpecimen& Specimen, const FaceOperationOutcome& Outcome,
                                              BodyToken Shown, const AuditReport& Audit, const FaceOperationInspection& Inspection);

} // namespace Frontier::GeometrySpace::Parametric
