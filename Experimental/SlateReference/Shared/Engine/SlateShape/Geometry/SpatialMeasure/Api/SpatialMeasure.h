//============================================================================================================================================
//                                                          SPATIALMEASURE.H
//============================================================================================================================================
// 🧩 The engine's spatial vocabulary: a point, a direction, and the arithmetic on them. Nothing here knows
//    what a curve, a profile or a solid is.
//
// 🔴 These declarations lived inside `CurveSpecification.h`, so every unit that needed to name a POINT had
//    to include the header that declares nine curve subjects, their storage and their evaluation. The
//    dependency ran the wrong way round: a point does not depend on a curve, a curve depends on a point.
//    `CurveSpecification.h` includes this header and re-exports it, so the 33 files that reach these names
//    through it keep compiling unchanged.

#pragma once

#include <cmath>

namespace Slate
{

struct PlanarPoint
{
    double Along = 0.0;
    double Across = 0.0;
};

struct SpatialPoint
{
    double Left = 0.0;
    double Up = 0.0;
    double Forward = 0.0;
};

struct SpatialDirection
{
    double Left = 0.0;
    double Up = 0.0;
    double Forward = 1.0;
};

// 🧩 Arithmetic on the two spatial declarations above.
//
// 🔴 These were copied, byte for byte, into the anonymous namespace of 22 separate translation units —
//    119 definitions of nine functions, every one of them identical. They live here because this is the
//    header that declares `SpatialPoint` and `SpatialDirection`; arithmetic on a type belongs with the
//    type, and no new unit or role suffix is needed to say so.
//
// ⚠️ A file may not keep a local copy AND include this header: the local copy in an anonymous namespace
//    and the one found by argument-dependent lookup are equally good candidates, so every call becomes
//    ambiguous and the file stops compiling. The sweep is therefore all-or-nothing per file, which is why
//    it was done in one commit rather than a few files at a time.

constexpr double LengthSquared(const SpatialDirection& Direction)
{
    return Direction.Left * Direction.Left
         + Direction.Up * Direction.Up
         + Direction.Forward * Direction.Forward;
}

constexpr double Dot(const SpatialDirection& LeftDirection,
                     const SpatialDirection& RightDirection)
{
    return LeftDirection.Left * RightDirection.Left
         + LeftDirection.Up * RightDirection.Up
         + LeftDirection.Forward * RightDirection.Forward;
}

constexpr SpatialDirection Cross(const SpatialDirection& LeftDirection,
                                 const SpatialDirection& RightDirection)
{
    return {
        LeftDirection.Up * RightDirection.Forward - LeftDirection.Forward * RightDirection.Up,
        LeftDirection.Forward * RightDirection.Left - LeftDirection.Left * RightDirection.Forward,
        LeftDirection.Left * RightDirection.Up - LeftDirection.Up * RightDirection.Left
    };
}

constexpr SpatialDirection Scaled(const SpatialDirection& Direction, double Amount)
{
    return { Direction.Left * Amount, Direction.Up * Amount, Direction.Forward * Amount };
}

constexpr SpatialDirection Negated(const SpatialDirection& Direction)
{
    return { -Direction.Left, -Direction.Up, -Direction.Forward };
}

constexpr SpatialDirection Added(const SpatialDirection& LeftDirection,
                                 const SpatialDirection& RightDirection)
{
    return { LeftDirection.Left + RightDirection.Left,
             LeftDirection.Up + RightDirection.Up,
             LeftDirection.Forward + RightDirection.Forward };
}

constexpr SpatialPoint Added(const SpatialPoint& Position, const SpatialDirection& Offset)
{
    return { Position.Left + Offset.Left, Position.Up + Offset.Up, Position.Forward + Offset.Forward };
}

/// 📝 The direction FROM the left point TO the right one. The argument order reads backwards against the
///    name and always has; it is preserved exactly because 15 files already depend on it.
constexpr SpatialDirection Difference(const SpatialPoint& LeftPoint, const SpatialPoint& RightPoint)
{
    return { RightPoint.Left - LeftPoint.Left,
             RightPoint.Up - LeftPoint.Up,
             RightPoint.Forward - LeftPoint.Forward };
}

/// ⚠️ A direction of no length normalises to the left-hand axis rather than refusing. Every copy behaved
///    this way, and callers rely on always receiving a usable direction.
inline SpatialDirection Normalize(const SpatialDirection& Direction)
{
    const double Length = std::sqrt(LengthSquared(Direction));
    return Length > 0.0 ? SpatialDirection{ Direction.Left / Length,
                                            Direction.Up / Length,
                                            Direction.Forward / Length }
                        : SpatialDirection{ 1.0, 0.0, 0.0 };
}

/// 🧩 Rotates a direction about an axis, by Rodrigues' formula.
/// in    Subject  [-]  the direction turned
/// in    Axis     [-]  what it turns about; normalised here, so it need not arrive unit
/// in    Radians  [-]  how far, right-handed about the axis
/// note 🔴 Ten copies of this existed in FIVE different spellings — some calling `Negated`, some
///    subtracting members by hand, some inlining the dot product — and all ten computed the same thing.
///    The component that lies along the axis is unchanged; the part perpendicular to it turns in the
///    plane spanned by itself and its cross with the axis.
/// note ⚠️ An axis of no length normalises to the left-hand axis rather than refusing, following
///    `Normalize` above. A rotation about nothing is a defect in the caller, not something to resolve here.

inline SpatialDirection RotateAroundAxis(const SpatialDirection& Subject,
                                         const SpatialDirection& Axis,
                                         double Radians)
{
    const SpatialDirection UnitAxis      = Normalize(Axis);
    const double           Cosine        = std::cos(Radians);
    const double           Sine          = std::sin(Radians);
    const SpatialDirection Parallel      = Scaled(UnitAxis, Dot(UnitAxis, Subject));
    const SpatialDirection Perpendicular = Added(Subject, Negated(Parallel));
    const SpatialDirection Crossed       = Cross(UnitAxis, Subject);
    return Added(Added(Scaled(Perpendicular, Cosine), Scaled(Crossed, Sine)), Parallel);
}

} // namespace Slate
