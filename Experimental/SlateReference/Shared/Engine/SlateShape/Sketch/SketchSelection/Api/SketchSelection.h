//============================================================================================================================================
//                                                         SKETCHSELECTION.H
//============================================================================================================================================
// 🧩 CPU-side sketch selection seams — point, control and curve queries over the exact sketch declarations.
//    Rendering may be GPU-side later; semantic selection remains on the CPU.

#pragma once

#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

enum class SketchControlSubject : std::uint32_t
{
    Centre = 0u,
    Radius = 1u,
    MajorAxis = 2u,
    MinorAxis = 3u,
    Through = 4u,
    ControlPoint = 5u,
    StartTangent = 6u,
    EndTangent = 7u,
    SubjectCount = 8u
};

struct SketchPointPlacement
{
    SketchPointName Name = {};
    SketchCurveName SourceCurve = {};
    SpatialPoint Position = {};
};

struct SketchControlPlacement
{
    SketchControlName Name = {};
    SketchCurveName SourceCurve = {};
    SketchControlSubject Subject = SketchControlSubject::ControlPoint;
    std::uint32_t LocalIndex = 0u;
    SpatialPoint Position = {};
};

bool ResolveSketchPoints(const SketchStructure& Declared,
                         SketchCurveName SourceCurve,
                         std::vector<SketchPointPlacement>& Resolved);
bool ResolveSketchControls(const SketchStructure& Declared,
                           SketchCurveName SourceCurve,
                           std::vector<SketchControlPlacement>& Resolved);

bool ResolveNearestSketchPoint(const SketchStructure& Declared,
                               const SpatialPoint& Probe,
                               double MaximumDistance,
                               SketchPointPlacement& Resolved,
                               double& Distance);
bool ResolveNearestSketchControl(const SketchStructure& Declared,
                                 const SpatialPoint& Probe,
                                 double MaximumDistance,
                                 SketchControlPlacement& Resolved,
                                 double& Distance);
bool ResolveNearestSketchCurve(const SketchStructure& Declared,
                               const SpatialPoint& Probe,
                               double MaximumDistance,
                               SketchCurveName& Resolved,
                               double& Distance);

} // namespace Slate
