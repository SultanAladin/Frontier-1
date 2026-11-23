//============================================================================================================================================
//                                                     WORLDSKETCHANALYSIS.H
//============================================================================================================================================
// 🧩 Derived loop analysis for the world-space sketch replacement path. Closed loops stay as topology even when
//    they are no longer planar; planarity is derived afterwards to decide whether a fill, profile handoff, or
//    later solid operation is honest.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/Geometry/ProfileSpecification/Api/ProfileSpecification.h"
#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

enum class WorldLoopIssueSubject : std::uint32_t
{
    MissingCurve = 0u,
    Gap = 1u,
    OpenLoop = 2u,
    NonCoplanar = 3u,
    DegeneratePlane = 4u
};

struct WorldLoopIssue
{
    WorldLoopName Loop = {};
    WorldLoopIssueSubject Subject = WorldLoopIssueSubject::MissingCurve;
    WorldCurveName FirstCurve = {};
    WorldCurveName SecondCurve = {};
    SpatialPoint Primary = {};
    SpatialPoint Secondary = {};
    double Distance = 0.0;
};

struct WorldLoopAnalysisRecord
{
    WorldLoopName Loop = {};
    std::vector<SpatialPoint> Outline = {};
    bool Closed = false;
    bool Coplanar = false;

    /// 🧩 Whether this loop's face is drawn.
    /// note  🔴 THREE SEPARATE QUESTIONS, and conflating them is what made a circle inside a circle draw
    ///        as a solid disc. ① CAN it be filled -- closed and planar, geometry alone. ② Does the
    ///        artist WANT it filled -- `DeclaredWorldLoop::FillWanted`, which the Fill tool toggles.
    ///        ③ Is it a HOLE in something else -- `Nesting`, below. Only a loop that answers yes, yes
    ///        and even is drawn.
    bool FillEligible = false;

    /// 🧩 How many other loops of this sketch enclose this one.
    /// note  🔴 THIS IS WHAT MAKES A TUBE A TUBE. A circle inside a circle is two loops, both closed and
    ///        both planar, and filling each on its own merit paints the inner disc over the hole it is
    ///        supposed to be. Depth decides it by the oldest rule in the trade: even depth is material,
    ///        odd depth is a hole. An island inside a hole -- depth two -- is material again, which
    ///        falls out of the same rule rather than needing another.
    std::uint32_t Nesting = 0u;

    /// 🧩 Whether this loop is a hole rather than a face. `Nesting` odd.
    bool Hole = false;

    /// 🧩 The loop this one sits directly inside, if any.
    /// note  📝 The innermost of its enclosers, so a hole names the face it is a hole IN rather than
    ///        merely something it happens to be inside. The renderer needs this to know which outline to
    ///        cut the hole out of when several faces are nested.
    WorldLoopName Container = {};

    WorldPlacementFrame SupportFrame = {};
    double MaximumDeviation = 0.0;
};

struct WorldSketchAnalysis
{
    std::vector<WorldLoopAnalysisRecord> Loops = {};
    std::vector<WorldLoopIssue> Issues = {};
};

WorldSketchAnalysis AnalyzeWorldSketch(const WorldSketchStructure& Declared,
                                     std::uint32_t StepFloor = 48u,
                                     double ClosureTolerance = 0.01,
                                     double CoplanarTolerance = 0.01);

Deliver<ProfileSpecification> ResolvePlanarWorldLoopProfile(const WorldSketchStructure& Declared,
                                                            WorldLoopName Subject,
                                                            std::uint32_t StepFloor = 48u,
                                                            double ClosureTolerance = 0.01,
                                                            double CoplanarTolerance = 0.01);

} // namespace Slate
