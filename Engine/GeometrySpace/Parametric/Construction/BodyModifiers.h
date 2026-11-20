/*====================================================================================================================================
                                                        BODYMODIFIERS.H
====================================================================================================================================*/
// 🧩 Non-destructive CAD modifier kernels: multi-instance assembly, linear/circular/curve/surface arrays, and certified 3D curve bend

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Construction/BodyTransform.h"
#include "Engine/GeometrySpace/Parametric/Containment/FaceDomain.h"
#include "Engine/GeometrySpace/Parametric/Feature/TopologicalNaming.h"
#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/CarrierApproximation.h"
#include "Engine/GeometrySpace/Parametric/Connectivity/ModelPartition.h"

#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ModifierBodyOutcome
{
    BodyToken   Body       = {};                                                             // [-]   - Resulting body
    BodyNaming  Naming     = {};                                                             // [-]   - Persistent face/edge/vertex names
    int         Instances  = 0;                                                              // [-]   - Copies / stations placed
    double      Deviation  = 0.0;                                                            // [len] - Certified max NURBS stand-in deviation (CurveBend)
    std::string Refusal    = {};                                                             // [-]   - Non-empty when refused
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Combines N solid or sheet instance bodies into one body (fast multi-region assembly when disjoint, Boolean Union when overlapping / MergeCondition).
[[nodiscard]] ModifierBodyOutcome CombineModifierInstances(ModelPartition& Partition, const std::vector<BodyToken>& Instances,
                                                           const std::vector<BodyNaming>& Namings, const std::string& Label,
                                                           bool MergeCondition);

// 1D / 2D linear grid array: CountX × CountY instances stepped along StepX and StepY.
[[nodiscard]] ModifierBodyOutcome ArrayBodyGrid(ModelPartition& Partition, BodyToken Source, const BodyNaming& SourceNaming,
                                                const std::string& Label, Vector3 StepX, int CountX,
                                                Vector3 StepY, int CountY, bool MergeCondition);

// Radial / helical circular array around AxisOrigin + AxisDirection over SweepAngle [rad] with optional AxialOffset.
[[nodiscard]] ModifierBodyOutcome ArrayBodyRadial(ModelPartition& Partition, BodyToken Source, const BodyNaming& SourceNaming,
                                                  const std::string& Label, Vector3 AxisOrigin, Vector3 AxisDirection,
                                                  int Count, double SweepAngle, double AxialOffset, bool MergeCondition);

// Pattern along a 3D NURBS rail curve using rotation-minimising frames (AlignCondition) or pure translation.
// Spacing ≤ 0 distributes Count instances evenly over the rail's arc length; Spacing > 0 steps by fixed arc length.
[[nodiscard]] ModifierBodyOutcome ArrayBodyAlongCurve(ModelPartition& Partition, BodyToken Source, const BodyNaming& SourceNaming,
                                                      const std::string& Label, const NurbsCurve& Rail, bool ClosedRailCondition,
                                                      int Count, double Spacing, bool AlignCondition, bool MergeCondition);

// Pattern across a target shape face's trimmed UV domain (holes automatically skipped), aligned to the outward surface normal.
[[nodiscard]] ModifierBodyOutcome ArrayBodyOnSurface(ModelPartition& Partition, BodyToken Source, const BodyNaming& SourceNaming,
                                                     const std::string& Label, const FaceDomain& Domain,
                                                     int CountU, int CountV, double Margin, double NormalOffset,
                                                     bool AlignCondition, bool MergeCondition);

// Certified 3D spatial B-rep curve deformation (Blender Curve Modifier / Rhino FlowAlongCurve): maps the body's DeformAxis
// (−1 = longest bounding axis, 0 = X, 1 = Y, 2 = Z) along Rail's arc-length rotation-minimising frames.
[[nodiscard]] ModifierBodyOutcome DeformBodyAlongCurve(ModelPartition& Partition, BodyToken Source, const BodyNaming& SourceNaming,
                                                       const std::string& Label, const NurbsCurve& Rail,
                                                       int DeformAxis = -1, bool StretchCondition = true,
                                                       const ApproximationSettings& Settings = {});

} // namespace Frontier::GeometrySpace::Parametric
