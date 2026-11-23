//============================================================================================================================================
//                                                  REFERENCESPECIFICATION.CPP
//============================================================================================================================================

#include "SlateShape/Reference/ReferenceSpecification/Api/ReferenceSpecification.h"

namespace Slate
{

bool ReferenceSpecification::Declared() const
{
    switch (Subject)
    {
        case ReferenceSubject::Sketch:        return Sketch.Assigned();
        case ReferenceSubject::SketchPoint:   return SketchPoint.Assigned();
        case ReferenceSubject::SketchControl: return SketchControl.Assigned();
        case ReferenceSubject::SketchCurve:   return SketchCurve.Assigned();
        case ReferenceSubject::Profile:       return Profile.Assigned();
        case ReferenceSubject::Solid:         return Solid.Assigned();
        case ReferenceSubject::Occurrence:    return Occurrence.Assigned();
        case ReferenceSubject::Face:          return Face.Assigned();
        case ReferenceSubject::Loop:          return Loop.Assigned();
        case ReferenceSubject::Edge:          return Edge.Assigned();
        case ReferenceSubject::EdgeSpan:      return EdgeSpan.Assigned();
        case ReferenceSubject::Vertex:        return Vertex.Assigned();
        case ReferenceSubject::SubjectCount:  return false;
    }
    return false;
}

} // namespace Slate
