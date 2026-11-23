//============================================================================================================================================
//                                                          WORLDSKETCHSNAP.H
//============================================================================================================================================
// 🧩 Semantic snapping over the world-space sketch authoring model. This unit knows exact world geometry and
//    an explicit active support frame, but no camera, viewport, workplane catalogue, or compatibility sketch.

#pragma once

#include "SlateShape/World/WorldSketchPicking/Api/WorldSketchPicking.h"

#include <vector>

namespace Slate
{

enum class WorldSnapSubject : std::uint32_t
{
    None = 0u,
    Endpoint = 1u,
    Midpoint = 2u,
    Centre = 3u,
    Control = 4u,
    AlongCurve = 5u,
    Intersection = 6u,
    Grid = 7u,
    Perpendicular = 8u,
    Tangent = 9u,
    SubjectCount = 10u
};

struct WorldSnapMask
{
    bool EndpointAccepted = true;
    bool MidpointAccepted = true;
    bool CentreAccepted = true;
    bool ControlAccepted = true;
    bool AlongCurveAccepted = true;
    bool IntersectionAccepted = true;
    bool GridAccepted = true;
    bool PerpendicularAccepted = true;
    bool TangentAccepted = true;
};

struct WorldSnapPlacement
{
    WorldSnapSubject Subject = WorldSnapSubject::None;
    WorldCurveName SourceCurve = {};
    WorldPointName WorldPoint = {};
    WorldControlName WorldControl = {};
    SpatialPoint Position = {};
    double Distance = 0.0;

    bool Resolved() const { return Subject != WorldSnapSubject::None; }
};

/// 🧩 Resolve the nearest semantic world snap. The active support frame supplies grid origin and axes; it is
///    never inferred from a compatibility sketch. Pending anchors belong to the placement still in progress.
/// note 🔴 Geometry wins by semantic precedence, and the grid is consulted only when no drawn or pending
///       candidate is within reach. This keeps a grid corner from stealing a nearby endpoint.
WorldSnapPlacement ResolveNearestWorldSnap(const WorldSketchStructure& Declared,
                                           const WorldPlacementFrame& ActiveFrame,
                                           const SpatialPoint& Probe,
                                           double MaximumDistance,
                                           const WorldSnapMask& Accepted = {},
                                           double GridStep = 10.0,
                                           const std::vector<SpatialPoint>& PendingAnchors = {});

} // namespace Slate
