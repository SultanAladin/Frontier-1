//============================================================================================================================================
//                                                           PROFILEBOOLEAN.H
//============================================================================================================================================
// 🧩 Two-dimensional profile booleans over resolved sketch profiles. This stage executes a bounded useful subset:
//    convex intersections, contained subtract cutters that become holes, and disjoint/contained unions.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"
#include "SlateShape/Operation/BooleanSolver/Api/BooleanSolver.h"

#include <vector>

namespace Slate
{

enum class ProfileBooleanDisposition : std::uint32_t
{
    NotRequested = 0u,
    InvalidSpecification = 1u,
    UnsupportedGeometry = 2u,
    Produced = 3u
};

ProfileBooleanDisposition EvaluateProfileBoolean(const SketchStructure& Declared,
                                                 const std::vector<ProfileNameInFeature>& OperandSet,
                                                 BooleanSubject Subject);

Deliver<std::vector<ProfileNameInFeature>> ApplyProfileBoolean(SketchStructure& Declared,
                                                               const std::vector<ProfileNameInFeature>& OperandSet,
                                                               BooleanSubject Subject);

} // namespace Slate
