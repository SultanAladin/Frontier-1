//============================================================================================================================================
//                                                        PROFILEAREA.H
//============================================================================================================================================
// 🧩 CPU-authoritative profile-area analysis for sketch validation, closed-chain discovery, fill preview, and
//    polygon handoff seams. Exact curves remain in SketchStructure; this module works from bounded polylines.

#pragma once

#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

enum class ProfileAreaIssueSubject : std::uint32_t
{
    OpenLoop = 0u,
    Gap = 1u,
    SelfIntersection = 2u
};

enum class ProfileAreaLoopRole : std::uint32_t
{
    Outer = 0u,
    Hole = 1u
};

struct ProfileAreaIssue
{
    ProfileAreaIssueSubject Subject = ProfileAreaIssueSubject::OpenLoop;
    SpatialPoint Primary = {};
    SpatialPoint Secondary = {};
    SketchCurveName FirstCurve = {};
    SketchCurveName SecondCurve = {};
    double Distance = 0.0;
};

struct ProfileAreaLoop
{
    std::vector<SketchCurveName> Curves = {};
    std::vector<SpatialPoint> Points = {};
    ProfileAreaLoopRole Role = ProfileAreaLoopRole::Outer;
    double SignedArea = 0.0;
    bool SelfIntersecting = false;
};

struct ProfileAreaTriangle
{
    SpatialPoint A = {};
    SpatialPoint B = {};
    SpatialPoint C = {};
    ProfileAreaLoopRole Role = ProfileAreaLoopRole::Outer;
};

struct ProfileAreaAnalysis
{
    std::vector<ProfileAreaLoop> Loops = {};
    std::vector<ProfileAreaIssue> Issues = {};
    std::vector<ProfileAreaTriangle> Triangles = {};
    bool Clipper2BackendAvailable = false;
    bool EarcutBackendAvailable = false;
};

ProfileAreaAnalysis AnalyzeProfileAreas(const SketchStructure& Declared,
                                             double ClosureTolerance = 0.01);

Deliver<std::vector<ProfileNameInFeature>> AutoDeclareClosedAreaProfiles(SketchStructure& Declared,
                                                                            double ClosureTolerance = 0.01);

} // namespace Slate
