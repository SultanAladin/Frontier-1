/*====================================================================================================================================
                                                      SKETCHCATALOGUE.CPP
====================================================================================================================================*/
// 🧩 Sketch corpus: under-, well-, over-constrained and singular sketches with hand-derived expectations

#include "Engine/GeometrySpace/Parametric/Sketch/SketchCatalogue.h"

#include <cmath>
#include <limits>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    using Kind = SketchConstraintKind;

    constexpr double NoArea = std::numeric_limits<double>::quiet_NaN();                      // [len²] - Skip the area check

    // 📝 A user never draws exactly: every point is displaced by a deterministic offset of up to Amount.
    void Roughen(SketchModel& Sketch, double Amount)
    {
        uint64_t Seed = 0x2545F4914F6CDD1Dull;
        for (SketchPoint& Point : Sketch.Points)
        {
            Seed = Seed * 6364136223846793005ull + 1442695040888963407ull;
            const double First = static_cast<double>(Seed >> 11) / 9007199254740992.0 - 0.5;
            Seed = Seed * 6364136223846793005ull + 1442695040888963407ull;
            const double Second = static_cast<double>(Seed >> 11) / 9007199254740992.0 - 0.5;
            Point.Position = Point.Position + Vector2{ First, Second } * (2.0 * Amount);
        }
    }

    // Fully dimensioned 3 × 2 rectangle at the origin: H, V, H, V, Width, Height, Fixed corner.
    SketchSpecimen BuildRectangle(const std::string& Name, bool Dimensioned, bool Fixed)
    {
        SketchSpecimen Specimen;
        Specimen.Name = Name;
        const SketchRectangle Rectangle = AppendSketchRectangle(Specimen.Sketch, { 0.0, 0.0 }, { 3.0, 2.0 }, 1);
        const int Corner = Rectangle.FirstPoint;
        if (Dimensioned)
        {
            AppendSketchConstraint(Specimen.Sketch, Kind::HorizontalDistance, Corner, Corner + 1, -1, 3.0, "Width");
            AppendSketchConstraint(Specimen.Sketch, Kind::VerticalDistance, Corner, Corner + 3, -1, 2.0, "Height");
        }
        if (Fixed) AppendSketchConstraint(Specimen.Sketch, Kind::Fixed, Corner, -1, -1, 0.0, "Origin", 0.0);
        Specimen.ExpectedDegreesOfFreedom = 4 - (Dimensioned ? 2 : 0) - (Fixed ? 2 : 0);
        Specimen.ExpectedProfile          = true;
        Specimen.ExpectedArea             = Dimensioned ? 6.0 : NoArea;
        if (Dimensioned && Fixed)
        {
            Specimen.ExpectedPositions = { { 0, { 0.0, 0.0 } }, { 1, { 3.0, 0.0 } }, { 2, { 3.0, 2.0 } }, { 3, { 0.0, 2.0 } } };
            Specimen.ExpectedFixedPoints = { -1 };
        }
        if (!Fixed) Specimen.ExpectedFixedPoints = { -2 };
        return Specimen;
    }

    // Stadium slot: two semicircles of radius r on centres L apart, joined by tangent lines.
    SketchSpecimen BuildSlot(double Separation, double Radius)
    {
        SketchSpecimen Specimen;
        Specimen.Name        = "Slot";
        Specimen.Description = "stadium: 2 lines tangent to 2 arcs, r and centre distance dimensioned, DOF 0";
        SketchModel& Sketch  = Specimen.Sketch;
        const int LeftCentre  = AppendSketchPoint(Sketch, { 0.0, 0.0 });
        const int RightCentre = AppendSketchPoint(Sketch, { Separation, 0.0 });
        const int BottomLeft  = AppendSketchPoint(Sketch, { 0.0, -Radius });
        const int BottomRight = AppendSketchPoint(Sketch, { Separation, -Radius });
        const int TopRight    = AppendSketchPoint(Sketch, { Separation, Radius });
        const int TopLeft     = AppendSketchPoint(Sketch, { 0.0, Radius });
        const int Bottom      = AppendSketchLine(Sketch, BottomLeft, BottomRight, 1);
        const int RightArc    = AppendSketchArc(Sketch, RightCentre, BottomRight, TopRight, 2);
        const int Top         = AppendSketchLine(Sketch, TopRight, TopLeft, 3);
        const int LeftArc     = AppendSketchArc(Sketch, LeftCentre, TopLeft, BottomLeft, 4);
        AppendSketchConstraint(Sketch, Kind::Fixed, LeftCentre, -1, -1, 0.0, "Origin", 0.0);
        AppendSketchConstraint(Sketch, Kind::HorizontalDistance, LeftCentre, RightCentre, -1, Separation, "Separation");
        AppendSketchConstraint(Sketch, Kind::VerticalDistance, LeftCentre, RightCentre, -1, 0.0, "Level");
        AppendSketchConstraint(Sketch, Kind::Radius, LeftArc, -1, -1, Radius, "Radius");
        AppendSketchConstraint(Sketch, Kind::EqualRadius, LeftArc, RightArc, -1, 0.0, "EqualRadius");
        AppendSketchConstraint(Sketch, Kind::Tangent, Bottom, LeftArc, -1, 0.0, "T1");
        AppendSketchConstraint(Sketch, Kind::Tangent, Bottom, RightArc, -1, 0.0, "T2");
        AppendSketchConstraint(Sketch, Kind::Tangent, Top, RightArc, -1, 0.0, "T3");
        AppendSketchConstraint(Sketch, Kind::Tangent, Top, LeftArc, -1, 0.0, "T4");
        Specimen.ExpectedDegreesOfFreedom = 0;
        Specimen.ExpectedProfile          = true;
        Specimen.ExpectedArea             = 2.0 * Radius * Separation + std::numbers::pi * Radius * Radius;
        Specimen.ExpectedPositions        = { { BottomLeft, { 0.0, -Radius } }, { BottomRight, { Separation, -Radius } },
                                              { TopRight, { Separation, Radius } }, { TopLeft, { 0.0, Radius } }, { RightCentre, { Separation, 0.0 } } };
        Specimen.ExpectedFixedPoints      = { -1 };
        return Specimen;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SketchRectangle AppendSketchRectangle(SketchModel& Sketch, Vector2 Low, Vector2 High, int FirstIdentifier)
{
    SketchRectangle Rectangle;
    Rectangle.FirstPoint = AppendSketchPoint(Sketch, Low);
    AppendSketchPoint(Sketch, { High.X, Low.Y });
    AppendSketchPoint(Sketch, High);
    AppendSketchPoint(Sketch, { Low.X, High.Y });
    const int Corner = Rectangle.FirstPoint;
    Rectangle.FirstCurve = AppendSketchLine(Sketch, Corner, Corner + 1, FirstIdentifier);
    AppendSketchLine(Sketch, Corner + 1, Corner + 2, FirstIdentifier + 1);
    AppendSketchLine(Sketch, Corner + 2, Corner + 3, FirstIdentifier + 2);
    AppendSketchLine(Sketch, Corner + 3, Corner, FirstIdentifier + 3);
    const int Line = Rectangle.FirstCurve;
    AppendSketchConstraint(Sketch, Kind::Horizontal, Line, -1, -1, 0.0, "H1");
    AppendSketchConstraint(Sketch, Kind::Vertical, Line + 1, -1, -1, 0.0, "V1");
    AppendSketchConstraint(Sketch, Kind::Horizontal, Line + 2, -1, -1, 0.0, "H2");
    AppendSketchConstraint(Sketch, Kind::Vertical, Line + 3, -1, -1, 0.0, "V2");
    return Rectangle;
}

SketchModel ScaleSketch(const SketchModel& Sketch, double Factor)
{
    SketchModel Scaled = Sketch;
    for (SketchPoint& Point : Scaled.Points) Point.Position = Point.Position * Factor;
    for (SketchCurve& Curve : Scaled.Curves) Curve.Radius *= Factor;
    for (SketchConstraint& Constraint : Scaled.Constraints)
    {
        const bool Angular = Constraint.Kind == Kind::Angle || Constraint.Kind == Kind::Parallel || Constraint.Kind == Kind::Perpendicular;
        if (!Angular) Constraint.Value *= Factor;
        if (Constraint.Kind == Kind::Fixed) Constraint.SecondValue *= Factor;
    }
    return Scaled;
}

std::vector<SketchSpecimen> BuildSketchCatalogue()
{
    std::vector<SketchSpecimen> Catalogue;

    // ① Rectangle ladder: DOF 4 → 2 → 0.
    {
        SketchSpecimen Specimen = BuildRectangle("FreeRectangle", false, false);
        Specimen.Description = "H, V, H, V only: position (2) + width + height free, DOF 4";
        Catalogue.push_back(Specimen);
    }
    {
        SketchSpecimen Specimen = BuildRectangle("DimensionedRectangle", true, false);
        Specimen.Description = "width and height dimensioned, position free, DOF 2";
        Catalogue.push_back(Specimen);
    }
    {
        SketchSpecimen Specimen = BuildRectangle("FixedRectangle", true, true);
        Specimen.Description = "dimensioned and anchored, DOF 0";
        Catalogue.push_back(Specimen);
    }
    {
        SketchSpecimen Specimen = BuildRectangle("HalfFixedRectangle", false, true);
        AppendSketchConstraint(Specimen.Sketch, Kind::HorizontalDistance, 0, 1, -1, 3.0, "Width");
        Specimen.Description              = "anchored with width only: bottom corners fixed, top edge free to slide up, DOF 1";
        Specimen.ExpectedDegreesOfFreedom = 1;
        Specimen.ExpectedFixedPoints      = { 0, 1 };
        Specimen.ExpectedArea             = NoArea;
        Catalogue.push_back(Specimen);
    }

    // ② Over-constrained: a consistent duplicate (redundant) and an inconsistent one (conflict).
    {
        SketchSpecimen Specimen = BuildRectangle("RedundantRectangle", true, true);
        AppendSketchConstraint(Specimen.Sketch, Kind::Parallel, 0, 2, -1, 0.0, "Parallel");
        Specimen.Description       = "fixed rectangle + parallel(bottom, top): implied by H1, H2, so redundant";
        Specimen.ExpectedRedundant = 1;
        Catalogue.push_back(Specimen);
    }
    {
        SketchSpecimen Specimen = BuildRectangle("ConflictingRectangle", true, true);
        AppendSketchConstraint(Specimen.Sketch, Kind::Length, 2, -1, -1, 4.0, "TopLength");
        Specimen.Description            = "fixed 3-wide rectangle + top length 4: conflicts with Width";
        Specimen.ExpectedConflictGroups = 1;
        Specimen.ExpectedConflictLabels = { "Width", "TopLength" };
        Specimen.ExpectedArea           = NoArea;
        Specimen.ExpectedPositions      = {};
        Specimen.ExpectedFixedPoints    = {};
        Catalogue.push_back(Specimen);
    }

    // ③ Triangle from three sides (law of cosines): C = ((c² + b² − a²) / 2c, √(b² − x²)).
    {
        SketchSpecimen Specimen;
        Specimen.Name        = "TriangleSides";
        Specimen.Description = "sides 4, 3, 2 with A fixed and AB horizontal, DOF 0";
        SketchModel& Sketch  = Specimen.Sketch;
        const int A = AppendSketchPoint(Sketch, { 0.0, 0.0 }), B = AppendSketchPoint(Sketch, { 4.0, 0.0 }), C = AppendSketchPoint(Sketch, { 1.4, 1.5 });
        const int AB = AppendSketchLine(Sketch, A, B, 1), BC = AppendSketchLine(Sketch, B, C, 2), CA = AppendSketchLine(Sketch, C, A, 3);
        AppendSketchConstraint(Sketch, Kind::Fixed, A, -1, -1, 0.0, "Origin", 0.0);
        AppendSketchConstraint(Sketch, Kind::Horizontal, AB, -1, -1, 0.0, "Flat");
        AppendSketchConstraint(Sketch, Kind::Length, AB, -1, -1, 4.0, "c");
        AppendSketchConstraint(Sketch, Kind::Length, BC, -1, -1, 3.0, "a");
        AppendSketchConstraint(Sketch, Kind::Length, CA, -1, -1, 2.0, "b");
        const double X = (16.0 + 4.0 - 9.0) / 8.0, Y = std::sqrt(4.0 - X * X);
        Specimen.ExpectedProfile     = true;
        Specimen.ExpectedArea        = 0.5 * 4.0 * Y;
        Specimen.ExpectedPositions   = { { A, { 0.0, 0.0 } }, { B, { 4.0, 0.0 } }, { C, { X, Y } } };
        Specimen.ExpectedFixedPoints = { -1 };
        Catalogue.push_back(Specimen);
    }

    // ④ Equilateral by equal lengths; an added 120° angle is implied (redundant).
    for (int Variant = 0; Variant < 2; ++Variant)
    {
        SketchSpecimen Specimen;
        Specimen.Name        = Variant == 0 ? "Equilateral" : "RedundantAngle";
        Specimen.Description = Variant == 0 ? "base 2 fixed, two equal-length constraints, DOF 0" : "equilateral + exterior angle 120° (implied): redundant";
        SketchModel& Sketch  = Specimen.Sketch;
        const int A = AppendSketchPoint(Sketch, { 0.0, 0.0 }), B = AppendSketchPoint(Sketch, { 2.0, 0.0 }), C = AppendSketchPoint(Sketch, { 1.0, 1.7 });
        const int AB = AppendSketchLine(Sketch, A, B, 1), BC = AppendSketchLine(Sketch, B, C, 2), CA = AppendSketchLine(Sketch, C, A, 3);
        AppendSketchConstraint(Sketch, Kind::Fixed, A, -1, -1, 0.0, "Origin", 0.0);
        AppendSketchConstraint(Sketch, Kind::Horizontal, AB, -1, -1, 0.0, "Flat");
        AppendSketchConstraint(Sketch, Kind::Length, AB, -1, -1, 2.0, "Side");
        AppendSketchConstraint(Sketch, Kind::EqualLength, AB, BC, -1, 0.0, "E1");
        AppendSketchConstraint(Sketch, Kind::EqualLength, BC, CA, -1, 0.0, "E2");
        if (Variant == 1) AppendSketchConstraint(Sketch, Kind::Angle, AB, BC, -1, 2.0 * std::numbers::pi / 3.0, "Turn120");
        Specimen.ExpectedRedundant   = Variant;
        Specimen.ExpectedProfile     = true;
        Specimen.ExpectedArea        = std::sqrt(3.0);
        Specimen.ExpectedPositions   = { { C, { 1.0, std::sqrt(3.0) } } };
        Specimen.ExpectedFixedPoints = { -1 };
        Catalogue.push_back(Specimen);
    }

    // ⑤ Slot (tangent arcs) and a plate with a hole (nested loop).
    Catalogue.push_back(BuildSlot(4.0, 0.75));
    {
        SketchSpecimen Specimen = BuildRectangle("PlateWithHole", true, true);
        SketchModel& Sketch = Specimen.Sketch;
        const int Centre = AppendSketchPoint(Sketch, { 1.4, 1.1 });
        const int Hole   = AppendSketchCircle(Sketch, Centre, 0.4, 5);
        AppendSketchConstraint(Sketch, Kind::HorizontalDistance, 0, Centre, -1, 1.5, "HoleX");
        AppendSketchConstraint(Sketch, Kind::VerticalDistance, 0, Centre, -1, 1.0, "HoleY");
        AppendSketchConstraint(Sketch, Kind::Radius, Hole, -1, -1, 0.5, "HoleRadius");
        Specimen.Description = "fixed rectangle with a dimensioned circular hole, DOF 0";
        Specimen.ExpectedArea = 6.0 - std::numbers::pi * 0.25;
        Specimen.ExpectedPositions.push_back({ Centre, { 1.5, 1.0 } });
        Catalogue.push_back(Specimen);
    }

    // ⑥ Circles: free radius-dimensioned circle, external tangency, a radius conflict.
    {
        SketchSpecimen Specimen;
        Specimen.Name        = "FreeCircle";
        Specimen.Description = "circle with radius only: centre free, DOF 2";
        const int Centre = AppendSketchPoint(Specimen.Sketch, { 0.3, 0.2 });
        const int Circle = AppendSketchCircle(Specimen.Sketch, Centre, 0.8, 1);
        AppendSketchConstraint(Specimen.Sketch, Kind::Radius, Circle, -1, -1, 1.0, "Radius");
        Specimen.ExpectedDegreesOfFreedom = 2;
        Specimen.ExpectedProfile          = true;
        Specimen.ExpectedArea             = std::numbers::pi;
        Specimen.ExpectedFixedPoints      = { -2 };
        Catalogue.push_back(Specimen);
    }
    {
        SketchSpecimen Specimen;
        Specimen.Name        = "TangentCircles";
        Specimen.Description = "r 1 fixed at origin, r 0.5 tangent outside on the same level, DOF 0";
        SketchModel& Sketch  = Specimen.Sketch;
        const int First = AppendSketchPoint(Sketch, { 0.0, 0.0 }), Second = AppendSketchPoint(Sketch, { 1.7, 0.2 });
        const int Big = AppendSketchCircle(Sketch, First, 0.9, 1), Small = AppendSketchCircle(Sketch, Second, 0.6, 2, true);
        AppendSketchConstraint(Sketch, Kind::Fixed, First, -1, -1, 0.0, "Origin", 0.0);
        AppendSketchConstraint(Sketch, Kind::Radius, Big, -1, -1, 1.0, "R1");
        AppendSketchConstraint(Sketch, Kind::Radius, Small, -1, -1, 0.5, "R2");
        AppendSketchConstraint(Sketch, Kind::VerticalDistance, First, Second, -1, 0.0, "Level");
        AppendSketchConstraint(Sketch, Kind::Tangent, Big, Small, -1, 0.0, "Touch");
        Specimen.ExpectedProfile     = true;
        Specimen.ExpectedArea        = std::numbers::pi;
        Specimen.ExpectedPositions   = { { Second, { 1.5, 0.0 } } };
        Specimen.ExpectedFixedPoints = { -1 };
        Catalogue.push_back(Specimen);
    }
    {
        SketchSpecimen Specimen;
        Specimen.Name        = "RadiusConflict";
        Specimen.Description = "concentric circles r 1 and r 2 declared equal radius: conflict";
        SketchModel& Sketch  = Specimen.Sketch;
        const int First = AppendSketchPoint(Sketch, { 0.0, 0.0 }), Second = AppendSketchPoint(Sketch, { 0.1, 0.0 });
        const int Inner = AppendSketchCircle(Sketch, First, 1.0, 1), Outer = AppendSketchCircle(Sketch, Second, 2.0, 2);
        AppendSketchConstraint(Sketch, Kind::Fixed, First, -1, -1, 0.0, "Origin", 0.0);
        AppendSketchConstraint(Sketch, Kind::Concentric, Inner, Outer, -1, 0.0, "Concentric");
        AppendSketchConstraint(Sketch, Kind::Radius, Inner, -1, -1, 1.0, "R1");
        AppendSketchConstraint(Sketch, Kind::Radius, Outer, -1, -1, 2.0, "R2");
        AppendSketchConstraint(Sketch, Kind::EqualRadius, Inner, Outer, -1, 0.0, "Equal");
        Specimen.ExpectedConflictGroups = 1;
        Specimen.ExpectedConflictLabels = { "Equal" };
        Specimen.ExpectedProfile        = true;
        Specimen.ExpectedArea           = NoArea;
        Catalogue.push_back(Specimen);
    }

    // ⑦ Point placement: midpoint and mirror image; a point on a circle and a line.
    {
        SketchSpecimen Specimen;
        Specimen.Name        = "MidpointMirror";
        Specimen.Description = "midpoint of a 4-long base, Q mirrors P across the vertical through it, DOF 0";
        SketchModel& Sketch  = Specimen.Sketch;
        const int A = AppendSketchPoint(Sketch, { 0.0, 0.0 }), B = AppendSketchPoint(Sketch, { 4.0, 0.0 });
        const int M = AppendSketchPoint(Sketch, { 2.0, 0.0 }), N = AppendSketchPoint(Sketch, { 2.0, 1.0 });
        const int P = AppendSketchPoint(Sketch, { 0.5, 1.0 }), Q = AppendSketchPoint(Sketch, { 3.5, 1.0 });
        const int Footing = AppendSketchLine(Sketch, A, B, 1, true), Mirror = AppendSketchLine(Sketch, M, N, 2, true);
        AppendSketchConstraint(Sketch, Kind::Fixed, A, -1, -1, 0.0, "Origin", 0.0);
        AppendSketchConstraint(Sketch, Kind::Horizontal, Footing, -1, -1, 0.0, "Level");
        AppendSketchConstraint(Sketch, Kind::Length, Footing, -1, -1, 4.0, "Span");
        AppendSketchConstraint(Sketch, Kind::Midpoint, M, Footing, -1, 0.0, "Middle");
        AppendSketchConstraint(Sketch, Kind::Vertical, Mirror, -1, -1, 0.0, "Upright");
        AppendSketchConstraint(Sketch, Kind::Length, Mirror, -1, -1, 1.0, "MirrorLength");
        AppendSketchConstraint(Sketch, Kind::Fixed, P, -1, -1, 0.5, "P", 1.0);
        AppendSketchConstraint(Sketch, Kind::Symmetric, P, Q, Mirror, 0.0, "Mirror");
        Specimen.ExpectedPositions   = { { M, { 2.0, 0.0 } }, { N, { 2.0, 1.0 } }, { Q, { 3.5, 1.0 } } };
        Specimen.ExpectedFixedPoints = { -1 };
        Specimen.ExpectedArea        = NoArea;
        Catalogue.push_back(Specimen);
    }
    {
        SketchSpecimen Specimen;
        Specimen.Name        = "PointOnCurves";
        Specimen.Description = "point on the r 2 circle and on the line y = x (branch nearest the drawing), DOF 0";
        SketchModel& Sketch  = Specimen.Sketch;
        const int Centre = AppendSketchPoint(Sketch, { 0.0, 0.0 });
        const int Start = AppendSketchPoint(Sketch, { -3.0, -3.0 }), End = AppendSketchPoint(Sketch, { 3.0, 3.0 });
        const int P = AppendSketchPoint(Sketch, { 1.5, 1.3 });
        const int Circle = AppendSketchCircle(Sketch, Centre, 2.0, 1, true);
        const int Line = AppendSketchLine(Sketch, Start, End, 2, true);
        AppendSketchConstraint(Sketch, Kind::Fixed, Centre, -1, -1, 0.0, "Origin", 0.0);
        AppendSketchConstraint(Sketch, Kind::Radius, Circle, -1, -1, 2.0, "R");
        AppendSketchConstraint(Sketch, Kind::Fixed, Start, -1, -1, -3.0, "S", -3.0);
        AppendSketchConstraint(Sketch, Kind::Fixed, End, -1, -1, 3.0, "E", 3.0);
        AppendSketchConstraint(Sketch, Kind::PointOnCircle, P, Circle, -1, 0.0, "OnCircle");
        AppendSketchConstraint(Sketch, Kind::PointOnLine, P, Line, -1, 0.0, "OnLine");
        Specimen.ExpectedPositions   = { { P, { std::sqrt(2.0), std::sqrt(2.0) } } };
        Specimen.ExpectedFixedPoints = { -1 };
        Specimen.ExpectedArea        = NoArea;
        Catalogue.push_back(Specimen);
    }

    // ⑧ Positional singularity: a point on two circles that touch. Generic configurations have rank 8 = n; at the
    //    touching point both gradients are parallel, so the solution is a double root (structural DOF 0, ill-posed).
    {
        SketchSpecimen Specimen;
        Specimen.Name        = "TouchingCircles";
        Specimen.Description = "point on two r 1 circles 2 apart (they touch): double root, flagged singular (FPA #106)";
        SketchModel& Sketch  = Specimen.Sketch;
        const int First = AppendSketchPoint(Sketch, { 0.0, 0.0 }), Second = AppendSketchPoint(Sketch, { 2.0, 0.0 }), P = AppendSketchPoint(Sketch, { 1.1, 0.3 });
        const int Left = AppendSketchCircle(Sketch, First, 1.0, 1, true), Right = AppendSketchCircle(Sketch, Second, 1.0, 2, true);
        AppendSketchConstraint(Sketch, Kind::Fixed, First, -1, -1, 0.0, "C1", 0.0);
        AppendSketchConstraint(Sketch, Kind::Fixed, Second, -1, -1, 2.0, "C2", 0.0);
        AppendSketchConstraint(Sketch, Kind::Radius, Left, -1, -1, 1.0, "R1");
        AppendSketchConstraint(Sketch, Kind::Radius, Right, -1, -1, 1.0, "R2");
        AppendSketchConstraint(Sketch, Kind::PointOnCircle, P, Left, -1, 0.0, "On1");
        AppendSketchConstraint(Sketch, Kind::PointOnCircle, P, Right, -1, 0.0, "On2");
        Specimen.ExpectedSingular = true;
        Specimen.ExpectedArea     = NoArea;
        Catalogue.push_back(Specimen);
    }

    // ⑨ Profile refusal: a bow-tie quadrilateral crosses itself.
    {
        SketchSpecimen Specimen;
        Specimen.Name        = "BowTie";
        Specimen.Description = "4 lines whose diagonals cross: solves (DOF 8) but is refused as a profile";
        SketchModel& Sketch  = Specimen.Sketch;
        const int A = AppendSketchPoint(Sketch, { 0.0, 0.0 }), B = AppendSketchPoint(Sketch, { 2.0, 2.0 });
        const int C = AppendSketchPoint(Sketch, { 2.0, 0.0 }), D = AppendSketchPoint(Sketch, { 0.0, 2.0 });
        AppendSketchLine(Sketch, A, B, 1); AppendSketchLine(Sketch, B, C, 2); AppendSketchLine(Sketch, C, D, 3); AppendSketchLine(Sketch, D, A, 4);
        Specimen.ExpectedDegreesOfFreedom = 8;
        Specimen.ExpectedArea             = NoArea;
        Catalogue.push_back(Specimen);
    }

    for (SketchSpecimen& Specimen : Catalogue) if (Specimen.Name != "BowTie") Roughen(Specimen.Sketch, 0.08);
    return Catalogue;
}

} // namespace Frontier::GeometrySpace::Parametric
