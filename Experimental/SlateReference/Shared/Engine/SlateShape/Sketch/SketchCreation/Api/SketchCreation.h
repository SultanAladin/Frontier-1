//============================================================================================================================================
//                                                         SKETCHCREATION.H
//============================================================================================================================================
// 🧩 Mouse-driven primitive creation flow. The caller feeds snapped positions to one standing tool declaration,
//    and this component decides when a curve or profile is ready to be authored into the exact sketch.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/Sketch/SketchSnap/Api/SketchSnap.h"

#include <vector>

namespace Slate
{

enum class SketchCreationSubject : std::uint32_t
{
    Line = 0u,
    Polyline = 1u,
    ThreePointArc = 2u,
    Circle = 3u,
    Ellipse = 4u,
    Oval = 5u,
    Polygon = 6u,
    Slot = 7u,
    Bezier = 8u,
    BasisSpline = 9u,
    RationalSpline = 10u,
    Hermite = 11u,
    SubjectCount = 12u
};

struct SketchCreationSpecification
{
    SketchCreationSubject Subject = SketchCreationSubject::Line;
    std::uint32_t PolygonSideCount = 6u;
    std::uint32_t CurveDegree = 3u;
    bool CloseLoop = false;
};

struct SketchCreationContext
{
    bool Engaged = false;
    bool PreviewStanding = false;
    SketchCreationSpecification Requested = {};
    std::vector<SpatialPoint> Anchors = {};
    SpatialPoint Preview = {};
};

struct SketchCreationResult
{
    std::vector<SketchCurveName> CurveSet = {};
    ProfileNameInFeature Profile = {};
    bool Produced = false;
};

void BeginSketchCreation(SketchCreationContext& Context,
                         const SketchCreationSpecification& Requested);
void PreviewSketchCreation(SketchCreationContext& Context,
                           const SpatialPoint& Position);
void AppendSketchCreation(SketchCreationContext& Context,
                          const SpatialPoint& Position);
void CancelSketchCreation(SketchCreationContext& Context);
bool CreationReady(const SketchCreationContext& Context);
Deliver<SketchCreationResult> FinishSketchCreation(SketchStructure& Declared,
                                                   SketchCreationContext& Context);

} // namespace Slate
