//============================================================================================================================================
//                                                         PICKCLASSIFIER.H
//============================================================================================================================================
// 🧩 Promotion of a resolved geometric hit to the authoring scope the active tool asked for.

#pragma once

#include "SlateShape/Reference/ReferenceSpecification/Api/ReferenceSpecification.h"
#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"

#include <cstdint>

namespace Slate
{

enum class PickSubject : std::uint32_t
{
    SketchPoint = 0u,
    SketchControl = 1u,
    SketchCurve = 2u,
    Vertex = 3u,
    Edge = 4u,
    EdgeSpan = 5u,
    Loop = 6u,
    Face = 7u,
    Solid = 8u,
    Occurrence = 9u,
    Feature = 10u,
    SubjectCount = 11u
};

struct PickResolution
{
    PickSubject Subject = PickSubject::Face;
    FeatureName Feature = {};
    SketchPointName SketchPoint = {};
    SketchControlName SketchControl = {};
    SketchCurveName SketchCurve = {};
    VertexNameInFeature Vertex = {};
    EdgeNameInFeature Edge = {};
    EdgeSpanNameInFeature EdgeSpan = {};
    LoopNameInFeature Loop = {};
    FaceNameInFeature Face = {};
    SolidNameInFeature Solid = {};
    OccurrenceNameInFeature Occurrence = {};
    SpatialPoint Position = {};
    SpatialDirection Perpendicular = {};

    bool Declared() const;
};

ReferenceSpecification PromotePick(const PickResolution& Resolved);

} // namespace Slate
