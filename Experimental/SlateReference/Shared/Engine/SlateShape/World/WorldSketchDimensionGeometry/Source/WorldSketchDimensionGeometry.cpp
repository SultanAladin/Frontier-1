//============================================================================================================================================
//                                                WORLDSKETCHDIMENSIONGEOMETRY.CPP
//============================================================================================================================================

#include "SlateShape/World/WorldSketchDimensionGeometry/Api/WorldSketchDimensionGeometry.h"

#include "SlateShape/World/WorldSketchPicking/Api/WorldSketchPicking.h"

#include <cmath>

namespace Slate
{

namespace
{

constexpr double MeasureTolerance = 1.0e-9;

/// 🧩 The plane a dimension is drawn in, taken from the curve it measures.
/// 📝 A dimension belongs to the same plane as its subject. Deriving it here rather than storing it means
///    re-planing a sketch carries its annotations along without touching them.
WorldPlacementFrame FrameOfCurve(const DeclaredWorldCurve& Curve, const SpatialPoint& Fallback)
{
    if (Curve.SupportFrameStanding && Curve.SupportFrame.Declared())
        return Curve.SupportFrame;
    return { Fallback, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 } };
}

/// 🧩 The perpendicular to a span, within the plane the span lies in.
/// 📐 The cross product of the plane's normal with the span. In the plane by construction, so the offset
///    it defines can never lift a dimension out of the drawing it annotates.
SpatialDirection PerpendicularInPlane(const WorldPlacementFrame& Frame, const SpatialDirection& Span)
{
    const SpatialDirection Normal = Normalize(Frame.Normal);
    const SpatialDirection Across = { Normal.Up      * Span.Forward - Normal.Forward * Span.Up,
                                      Normal.Forward * Span.Left    - Normal.Left    * Span.Forward,
                                      Normal.Left    * Span.Up      - Normal.Up      * Span.Left };
    if (LengthSquared(Across) <= MeasureTolerance)
        return Normalize(Frame.AlongDirection);
    return Normalize(Across);
}

/// 🧩 The two ends of whatever a dimension measures, and the plane they sit in.
/// note  🔴 THE SAME SPAN THE SOLVER MEASURES. Both the drawing and the solving ask the geometry, so a
///        dimension cannot show one number and drive another.
bool MeasuredSpan(const WorldSketchStructure& Declared,
                  const WorldDimensionSpecification& Dimension,
                  SpatialPoint& Start,
                  SpatialPoint& End,
                  WorldPlacementFrame& Frame)
{
    // ① Two named points.
    if (Dimension.Primary.Subject == WorldDimensionReferenceSubject::Point &&
        Dimension.Secondary.Subject == WorldDimensionReferenceSubject::Point)
    {
        const std::uint32_t FirstCurve = Dimension.Primary.Point >> 8u;
        const std::uint32_t SecondCurve = Dimension.Secondary.Point >> 8u;
        if (FirstCurve == 0u || FirstCurve > Declared.CurveCount() ||
            SecondCurve == 0u || SecondCurve > Declared.CurveCount())
            return false;

        std::vector<WorldPointPlacement> First;
        std::vector<WorldPointPlacement> Second;
        if (!ResolveWorldSketchPoints(Declared, WorldCurveName{ FirstCurve }, First) ||
            !ResolveWorldSketchPoints(Declared, WorldCurveName{ SecondCurve }, Second))
            return false;

        bool FoundStart = false;
        bool FoundEnd = false;
        for (const WorldPointPlacement& Point : First)
            if (Point.Name.IssuedIndex == Dimension.Primary.Point)
            {
                Start = Point.Position;
                FoundStart = true;
            }
        for (const WorldPointPlacement& Point : Second)
            if (Point.Name.IssuedIndex == Dimension.Secondary.Point)
            {
                End = Point.Position;
                FoundEnd = true;
            }
        if (!FoundStart || !FoundEnd)
            return false;

        const DeclaredWorldCurve* Held = Declared.Resolve(WorldCurveName{ FirstCurve });
        Frame = Held == nullptr ? WorldPlacementFrame{ Start, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 } }
                                : FrameOfCurve(*Held, Start);
        return true;
    }

    // ② A whole curve, measured end to end.
    if (Dimension.Primary.Subject == WorldDimensionReferenceSubject::Curve)
    {
        const DeclaredWorldCurve* Held = Declared.Resolve(Dimension.Primary.Curve);
        if (Held == nullptr)
            return false;

        std::vector<WorldPointPlacement> Points;
        if (!ResolveWorldSketchPoints(Declared, Dimension.Primary.Curve, Points) || Points.size() < 2u)
            return false;

        Start = Points.front().Position;
        End = Points.back().Position;
        Frame = FrameOfCurve(*Held, Start);
        return true;
    }

    return false;
}

/// 🧩 The centre and radius of whatever round thing a dimension names.
bool MeasuredCircle(const WorldSketchStructure& Declared,
                    const WorldDimensionSpecification& Dimension,
                    SpatialPoint& Centre,
                    double& Radius,
                    WorldPlacementFrame& Frame)
{
    WorldCurveName Named = Dimension.Primary.Curve;
    if (Dimension.Primary.Subject == WorldDimensionReferenceSubject::Control)
    {
        const std::uint32_t CurveIndex = Dimension.Primary.Control >> 12u;
        if (CurveIndex == 0u || CurveIndex > Declared.CurveCount())
            return false;
        Named = WorldCurveName{ CurveIndex };
    }

    const DeclaredWorldCurve* Held = Declared.Resolve(Named);
    if (Held == nullptr)
        return false;

    if (Held->Geometry.Subject() == CurveSubject::Circle)
    {
        Centre = Held->Geometry.HeldCircle().Centre;
        Radius = Held->Geometry.HeldCircle().Radius;
    }
    else if (Held->Geometry.Subject() == CurveSubject::CircularArc)
    {
        Centre = Held->Geometry.HeldCircularArc().Centre;
        Radius = Held->Geometry.HeldCircularArc().Radius;
    }
    else
    {
        return false;
    }

    Frame = FrameOfCurve(*Held, Centre);
    return true;
}

/// 🧩 A point on a circle's rim, at an angle measured within the circle's own plane.
SpatialPoint RimAt(const WorldPlacementFrame& Frame,
                   const SpatialPoint& Centre,
                   double Radius,
                   double Angle)
{
    const SpatialDirection Along = Normalize(Frame.AlongDirection);
    const SpatialDirection Across = PerpendicularInPlane(Frame, Along);
    return Added(Centre, Added(Scaled(Along, std::cos(Angle) * Radius),
                               Scaled(Across, std::sin(Angle) * Radius)));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------

Deliver<DimensionGeometry> ResolveDimensionGeometry(const WorldSketchStructure& Declared,
                                                    WorldDimensionName Subject)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > Declared.DimensionCount())
        return Deliver<DimensionGeometry>::Refuse(
            { RefusalReason::ContentUnsupported, "no such world dimension is declared" });

    const WorldDimensionSpecification& Dimension = Declared.Dimensions()[Subject.IssuedIndex - 1u];
    DimensionGeometry Resolved = {};

    //------------------------------------------------------------------------------------------------------------------------
    // ① Round dimensions, placed in polar terms.
    //------------------------------------------------------------------------------------------------------------------------
    if (Dimension.Subject == WorldDimensionSubject::Radius ||
        Dimension.Subject == WorldDimensionSubject::Diameter)
    {
        SpatialPoint Centre = {};
        double Radius = 0.0;
        WorldPlacementFrame Frame = {};
        if (!MeasuredCircle(Declared, Dimension, Centre, Radius, Frame))
            return Deliver<DimensionGeometry>::Refuse(
                { RefusalReason::ContentUnsupported, "the dimensioned curve is not round" });

        const bool Whole = Dimension.Subject == WorldDimensionSubject::Diameter;
        Resolved.Drawing = Whole ? DimensionDrawing::Diameter : DimensionDrawing::Radial;
        Resolved.Frame = Frame;

        // 🔴 A DIAMETER IS A CHORD THROUGH THE CENTRE; A RADIUS IS A LEADER FROM IT. Drawing a diameter as
        //    two radii, or a radius as half a chord, is what makes an annotation ambiguous to read.
        const SpatialPoint Far = RimAt(Frame, Centre, Radius, Dimension.Angle);
        const SpatialPoint Near = RimAt(Frame, Centre, Radius, Dimension.Angle + 3.14159265358979323846);

        Resolved.MeasuredStart = Whole ? Near : Centre;
        Resolved.MeasuredEnd = Far;
        Resolved.Measured = Whole ? Radius * 2.0 : Radius;

        // 📝 `Offset` stands the figure off the rim, so the text clears the geometry it describes.
        const SpatialPoint Standoff = RimAt(Frame, Centre, Radius + Dimension.Offset, Dimension.Angle);
        Resolved.LineStart = Resolved.MeasuredStart;
        Resolved.LineEnd = Standoff;
        Resolved.TextAt = Standoff;

        const SpatialDirection Outward = Normalize(Difference(Centre, Far));
        Resolved.TextAlong = Outward;
        Resolved.ArrowStart = Outward;
        Resolved.ArrowEnd = Negated(Outward);
        return Deliver<DimensionGeometry>::Result(Resolved);
    }

    //------------------------------------------------------------------------------------------------------------------------
    // ② Everything linear.
    //------------------------------------------------------------------------------------------------------------------------
    SpatialPoint Start = {};
    SpatialPoint End = {};
    WorldPlacementFrame Frame = {};
    if (!MeasuredSpan(Declared, Dimension, Start, End, Frame))
        return Deliver<DimensionGeometry>::Refuse(
            { RefusalReason::ContentUnsupported, "the dimensioned geometry is absent" });

    // 🔴 HORIZONTAL AND VERTICAL MEASURE A PROJECTION, NOT THE SPAN. An aligned dimension measures the
    //    true distance; a horizontal one measures only how far apart the ends are ALONG the plane's own
    //    axis, which for a sloping edge is a different and smaller number. Measuring the span for all
    //    three would make the three subjects indistinguishable, which they are not.
    if (Dimension.Subject == WorldDimensionSubject::Horizontal ||
        Dimension.Subject == WorldDimensionSubject::Vertical)
    {
        double StartAlong = 0.0, StartAcross = 0.0, EndAlong = 0.0, EndAcross = 0.0;
        ResolveWorldPlacementCoordinates(Frame, Start, StartAlong, StartAcross);
        ResolveWorldPlacementCoordinates(Frame, End, EndAlong, EndAcross);

        if (Dimension.Subject == WorldDimensionSubject::Horizontal)
            End = ResolveWorldPlacementPosition(Frame, EndAlong, StartAcross);
        else
            End = ResolveWorldPlacementPosition(Frame, StartAlong, EndAcross);
    }

    const SpatialDirection Span = Difference(Start, End);
    const double Length = std::sqrt(LengthSquared(Span));
    if (!(Length > MeasureTolerance))
        return Deliver<DimensionGeometry>::Refuse(
            { RefusalReason::ContentUnsupported, "the dimensioned span has no length" });

    const SpatialDirection Along = Normalize(Span);
    const SpatialDirection Across = PerpendicularInPlane(Frame, Along);

    Resolved.Drawing = DimensionDrawing::Linear;
    Resolved.Frame = Frame;
    Resolved.MeasuredStart = Start;
    Resolved.MeasuredEnd = End;
    Resolved.Measured = Length;

    // 🔴 THE OFFSET IS APPLIED WITH ITS SIGN AND NOTHING ELSE. Negative offsets carry the dimension to
    //    the other side of the edge automatically -- there is no "which side" branch anywhere, because
    //    the number already says.
    const SpatialDirection Shift = Scaled(Across, Dimension.Offset);
    Resolved.LineStart = Added(Start, Shift);
    Resolved.LineEnd = Added(End, Shift);

    Resolved.TextAt = { (Resolved.LineStart.Left    + Resolved.LineEnd.Left)    * 0.5,
                        (Resolved.LineStart.Up      + Resolved.LineEnd.Up)      * 0.5,
                        (Resolved.LineStart.Forward + Resolved.LineEnd.Forward) * 0.5 };
    Resolved.TextAlong = Along;

    // 🔴 ARROWS TURN OUTWARD WHEN THEY WOULD NOT FIT. Two three-millimetre arrowheads inside a
    //    two-millimetre dimension overlap into an unreadable blob, so below that width they point in from
    //    outside instead. Every drafting standard does this, and a reader expects it.
    Resolved.ArrowsOutward = Length < DimensionArrowReach * 2.0;
    Resolved.ArrowStart = Resolved.ArrowsOutward ? Negated(Along) : Along;
    Resolved.ArrowEnd = Resolved.ArrowsOutward ? Along : Negated(Along);

    return Deliver<DimensionGeometry>::Result(Resolved);
}

//------------------------------------------------------------------------------------------------------------------------

Deliver<double> ResolveDimensionOffsetFor(const WorldSketchStructure& Declared,
                                          WorldDimensionName Subject,
                                          const SpatialPoint& Probe)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > Declared.DimensionCount())
        return Deliver<double>::Refuse(
            { RefusalReason::ContentUnsupported, "no such world dimension is declared" });

    const WorldDimensionSpecification& Dimension = Declared.Dimensions()[Subject.IssuedIndex - 1u];

    // 📝 A round dimension's stand-off is radial: how far past the rim the pointer has gone.
    if (Dimension.Subject == WorldDimensionSubject::Radius ||
        Dimension.Subject == WorldDimensionSubject::Diameter)
    {
        SpatialPoint Centre = {};
        double Radius = 0.0;
        WorldPlacementFrame Frame = {};
        if (!MeasuredCircle(Declared, Dimension, Centre, Radius, Frame))
            return Deliver<double>::Refuse(
                { RefusalReason::ContentUnsupported, "the dimensioned curve is not round" });
        return Deliver<double>::Result(std::sqrt(LengthSquared(Difference(Centre, Probe))) - Radius);
    }

    SpatialPoint Start = {};
    SpatialPoint End = {};
    WorldPlacementFrame Frame = {};
    if (!MeasuredSpan(Declared, Dimension, Start, End, Frame))
        return Deliver<double>::Refuse(
            { RefusalReason::ContentUnsupported, "the dimensioned geometry is absent" });

    const SpatialDirection Span = Difference(Start, End);
    if (LengthSquared(Span) <= MeasureTolerance)
        return Deliver<double>::Refuse(
            { RefusalReason::ContentUnsupported, "the dimensioned span has no length" });

    const SpatialDirection Across = PerpendicularInPlane(Frame, Normalize(Span));
    const SpatialPoint Middle = { (Start.Left + End.Left) * 0.5,
                                  (Start.Up + End.Up) * 0.5,
                                  (Start.Forward + End.Forward) * 0.5 };

    // 🔴 A SIGNED PROJECTION, AND THE SIGN IS THE POINT. Taking the magnitude here is the whole bug this
    //    function exists to avoid: the dimension would then stick to one side and the artist could never
    //    drag it across the edge.
    return Deliver<double>::Result(Dot(Difference(Middle, Probe), Across));
}

//------------------------------------------------------------------------------------------------------------------------

Deliver<double> ResolveDimensionAngleFor(const WorldSketchStructure& Declared,
                                         WorldDimensionName Subject,
                                         const SpatialPoint& Probe)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > Declared.DimensionCount())
        return Deliver<double>::Refuse(
            { RefusalReason::ContentUnsupported, "no such world dimension is declared" });

    const WorldDimensionSpecification& Dimension = Declared.Dimensions()[Subject.IssuedIndex - 1u];

    SpatialPoint Centre = {};
    double Radius = 0.0;
    WorldPlacementFrame Frame = {};
    if (!MeasuredCircle(Declared, Dimension, Centre, Radius, Frame))
        return Deliver<double>::Refuse(
            { RefusalReason::ContentUnsupported, "the dimensioned curve is not round" });

    // 📝 Measured in the circle's OWN plane, so the angle means the same thing whatever way the sketch is
    //    tilted in the world. An atan2 against world axes would spin the dimension when the plane moved.
    const SpatialDirection Along = Normalize(Frame.AlongDirection);
    const SpatialDirection Across = PerpendicularInPlane(Frame, Along);
    const SpatialDirection Reach = Difference(Centre, Probe);

    return Deliver<double>::Result(std::atan2(Dot(Reach, Across), Dot(Reach, Along)));
}

} // namespace Slate
