/*====================================================================================================================================
                                                    INTERSECTIONENCODING.H
====================================================================================================================================*/
// 🧩 JSON encoding of an intersection scene for the HTML viewer (same schema family as model specimens, Kind = Intersection)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Audit/IntersectionAudit.h"
#include "Engine/GeometrySpace/Parametric/Inspection/IntersectionRepository.h"

#include <string>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Faces = the two surface patches as iso-hatching (display-clipped around the curve), Edges = branch polylines,
//    Vertices = special points, Summary.Metrics = measured vs expected with a pass flag per row.
[[nodiscard]] std::string EncodeIntersection(const std::string&                SceneName,
                                             const IntersectionScene&          Scene,
                                             const SurfaceIntersectionOutcome& Outcome,
                                             const IntersectionAuditReport&    Audit,
                                             double                            SolveMilliseconds,
                                             double                            AuditMilliseconds);

} // namespace Frontier::GeometrySpace::Parametric
