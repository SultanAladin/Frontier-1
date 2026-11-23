//============================================================================================================================================
//                                                       PICKCLASSIFIER.CPP
//============================================================================================================================================

#include "SlateShape/Reference/PickClassifier/Api/PickClassifier.h"

namespace Slate
{

bool PickResolution::Declared() const
{
    switch (Subject)
    {
        case PickSubject::SketchPoint:   return SketchPoint.Assigned();
        case PickSubject::SketchControl: return SketchControl.Assigned();
        case PickSubject::SketchCurve:   return SketchCurve.Assigned();
        case PickSubject::Vertex:        return Vertex.Assigned();
        case PickSubject::Edge:          return Edge.Assigned();
        case PickSubject::EdgeSpan:      return EdgeSpan.Assigned();
        case PickSubject::Loop:          return Loop.Assigned();
        case PickSubject::Face:          return Face.Assigned();
        case PickSubject::Solid:         return Solid.Assigned();
        case PickSubject::Occurrence:    return Occurrence.Assigned();
        case PickSubject::Feature:       return Feature.Assigned();
        case PickSubject::SubjectCount:  return false;
    }
    return false;
}

ReferenceSpecification PromotePick(const PickResolution& Resolved)
{
    ReferenceSpecification Declared;

    switch (Resolved.Subject)
    {
        case PickSubject::SketchPoint:
            Declared.Subject = ReferenceSubject::SketchPoint;
            Declared.SketchPoint = Resolved.SketchPoint;
            break;
        case PickSubject::SketchControl:
            Declared.Subject = ReferenceSubject::SketchControl;
            Declared.SketchControl = Resolved.SketchControl;
            break;
        case PickSubject::SketchCurve:
            Declared.Subject = ReferenceSubject::SketchCurve;
            Declared.SketchCurve = Resolved.SketchCurve;
            break;
        case PickSubject::Vertex:
            Declared.Subject = ReferenceSubject::Vertex;
            Declared.Vertex = Resolved.Vertex;
            break;
        case PickSubject::Edge:
            Declared.Subject = ReferenceSubject::Edge;
            Declared.Edge = Resolved.Edge;
            break;
        case PickSubject::EdgeSpan:
            Declared.Subject = ReferenceSubject::EdgeSpan;
            Declared.EdgeSpan = Resolved.EdgeSpan;
            break;
        case PickSubject::Loop:
            Declared.Subject = ReferenceSubject::Loop;
            Declared.Loop = Resolved.Loop;
            break;
        case PickSubject::Face:
            Declared.Subject = ReferenceSubject::Face;
            Declared.Face = Resolved.Face;
            break;
        case PickSubject::Solid:
            Declared.Subject = ReferenceSubject::Solid;
            Declared.Solid = Resolved.Solid;
            break;
        case PickSubject::Occurrence:
            Declared.Subject = ReferenceSubject::Occurrence;
            Declared.Occurrence = Resolved.Occurrence;
            break;
        case PickSubject::Feature:
        case PickSubject::SubjectCount:
            break;
    }

    return Declared;
}

} // namespace Slate
