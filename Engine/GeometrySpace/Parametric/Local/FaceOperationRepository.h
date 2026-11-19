/*====================================================================================================================================
                                                    FACEOPERATIONCATALOGUE.H
====================================================================================================================================*/
// 🧩 Face-operation specimens: corpus section E (Block / HoleSlab / Can × 12 verbs), chains, extras and refusals

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Local/FaceOperations.h"

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Families: CorpusE (FeatureCorpus.arc section E, same bodies, sizes and face picks), Chain (verbs composed, checked
//    against an independent reference construction), Extra (more faces, curved targets, inward offsets) and Refusal
//    (the only correct answer is a refusal). Expected volumes are derived by hand — prisms, frusta, wedges, cones —
//    never from the kernel.
struct FaceOperationSpecimen
{
    std::string                        Name             = {};                                // [-]    - Specimen label
    std::string                        Family           = {};                                // [-]    - CorpusE / Chain / Extra / Refusal
    std::string                        Description      = {};                                // [-]    - What it exercises
    std::string                        Corpus           = {};                                // [-]    - Corpus command (CorpusE rows)
    std::string                        LegacyStatus     = {};                                // [-]    - Old FaceEditSolver result on that row
    BodyToken                          Body             = {};                                // [-]    - Source body
    std::vector<FaceOperationSettings> Steps            = {};                                // [-]    - Applied in order (chains: several)
    std::vector<FaceOperationSettings> Reference        = {};                                // [-]    - Independent construction to compare with
    double                             ExpectedVolume   = std::nan("");                      // [len³] - V after (NaN: sheet)
    BodyCategory                       ExpectedCategory = BodyCategory::Solid;               // [-]    - Solid or Sheet
    int                                ExpectedGenus    = -1;                                // [-]    - Genus after (−1: not asserted)
    std::array<int, 3>                 ExpectedCounts   = { -1, -1, -1 };                    // [-]    - Faces, edges, vertices (−1: not asserted)
    bool                               RefusalExpected  = false;                             // [-]    - Must refuse
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::vector<std::string> EnumerateFaceOperationSpecimens();

// Builds the named specimen into the partition (empty Name on an unknown specimen); Scale multiplies every length.
[[nodiscard]] FaceOperationSpecimen ConstructFaceOperationSpecimen(ModelPartition& Partition, const std::string& Name, double Scale = 1.0);

// Source bodies of section E at Scale: 0 Block 2 × 1.5 × 1, 1 HoleSlab 3 × 2 × 0.6 − bore r 0.45, 2 Can r 0.6 h 1.
[[nodiscard]] BodyToken ConstructCorpusBody(ModelPartition& Partition, int Which, double Scale = 1.0);

} // namespace Frontier::GeometrySpace::Parametric
