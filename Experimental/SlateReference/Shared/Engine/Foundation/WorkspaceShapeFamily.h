//============================================================================================================================================
//                                                   WORKSPACESHAPEFAMILY.H
//============================================================================================================================================
// Shared vocabulary for classifying parametric shape records without coupling UI to the CAD record layer.

#pragma once

#include <cstdint>

namespace Slate
{

enum class WorkspaceShapeFamily : std::uint32_t
{
    Unknown = 0u,

    Point,

    Line,
    CircularArc,
    Bezier,
    Hermite,
    BasisSpline,
    Nurbs,

    Polygon,
    Rectangle,
    Slot,

    Circle,
    Ellipse,

    Profile,
    Surface,
    Solid,

    Dimension,
    Constraint,
    Workplane,
    Construction,

    ShapeFamilyCount
};

} // namespace Slate
