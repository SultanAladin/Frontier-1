/*====================================================================================================================================
                                                        SKETCHSOLVER.CPP
====================================================================================================================================*/
// 🧩 2D sketch model and numerical constraint solver: minimum-norm Gauss–Newton on a rank-revealing QR, DOF = n − rank

#include "Engine/GeometrySpace/Parametric/Sketch/SketchSolver.h"

#include "Engine/GeometrySpace/Parametric/Diagnostics/OperationTrace.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSpline.h"

#include <algorithm>
#include <array>
#include <chrono>
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
    constexpr int LocalUnknowns = 8;                                                         // [-] - Max unknowns one equation touches

    // 📝 Forward-mode value + gradient over an equation's local unknowns (exact Jacobian rows, no finite differences).
    struct LocalDual
    {
        double                              Value      = 0.0;                                // [-] - f
        std::array<double, LocalUnknowns>   Derivative = {};                                 // [-] - ∂f/∂(local unknown)
    };

    LocalDual operator+(LocalDual First, const LocalDual& Second)
    {
        First.Value += Second.Value;
        for (int Slot = 0; Slot < LocalUnknowns; ++Slot) First.Derivative[Slot] += Second.Derivative[Slot];
        return First;
    }
    LocalDual operator-(LocalDual First, const LocalDual& Second)
    {
        First.Value -= Second.Value;
        for (int Slot = 0; Slot < LocalUnknowns; ++Slot) First.Derivative[Slot] -= Second.Derivative[Slot];
        return First;
    }
    LocalDual operator*(const LocalDual& First, const LocalDual& Second)
    {
        LocalDual Product;
        Product.Value = First.Value * Second.Value;
        for (int Slot = 0; Slot < LocalUnknowns; ++Slot) Product.Derivative[Slot] = First.Derivative[Slot] * Second.Value + First.Value * Second.Derivative[Slot];
        return Product;
    }
    LocalDual operator*(LocalDual First, double Factor)
    {
        First.Value *= Factor;
        for (double& Entry : First.Derivative) Entry *= Factor;
        return First;
    }
    LocalDual operator-(LocalDual First, double Offset) { First.Value -= Offset; return First; }
    LocalDual operator/(const LocalDual& First, const LocalDual& Second)
    {
        LocalDual Quotient;
        const double Inverse = 1.0 / Second.Value;
        Quotient.Value = First.Value * Inverse;
        for (int Slot = 0; Slot < LocalUnknowns; ++Slot) Quotient.Derivative[Slot] = (First.Derivative[Slot] - Quotient.Value * Second.Derivative[Slot]) * Inverse;
        return Quotient;
    }
    // ⚠️ √0 has no derivative: a zero gradient there (subgradient) makes the rank drop, which the diagnosis reports as a
    //    positional singularity (e.g. a distance dimension of 0 standing in for a coincidence).
    LocalDual SquareRoot(const LocalDual& Argument)
    {
        LocalDual Root;
        Root.Value = std::sqrt(std::max(0.0, Argument.Value));
        if (Root.Value > 1.0e-300) for (int Slot = 0; Slot < LocalUnknowns; ++Slot) Root.Derivative[Slot] = 0.5 * Argument.Derivative[Slot] / Root.Value;
        return Root;
    }
    LocalDual Magnitude(const LocalDual& Argument) { return Argument.Value < 0.0 ? Argument * -1.0 : Argument; }
    // atan2(y, x) ∈ (−π, π]: ∂ = (x·∂y − y·∂x) / (x² + y²); the cut at ±π stays far from the roots it is used for.
    LocalDual Bearing(const LocalDual& Rise, const LocalDual& Run)
    {
        LocalDual Angle;
        Angle.Value = std::atan2(Rise.Value, Run.Value);
        const double Square = Rise.Value * Rise.Value + Run.Value * Run.Value;
        if (Square > 1.0e-300) for (int Slot = 0; Slot < LocalUnknowns; ++Slot) Angle.Derivative[Slot] = (Run.Value * Rise.Derivative[Slot] - Rise.Value * Run.Derivative[Slot]) / Square;
        return Angle;
    }

    struct DualPoint
    {
        LocalDual X = {};                                                                    // [len] - x
        LocalDual Y = {};                                                                    // [len] - y
    };

    LocalDual CrossProduct(const DualPoint& First, const DualPoint& Second) { return First.X * Second.Y - First.Y * Second.X; }
    LocalDual DotProduct(const DualPoint& First, const DualPoint& Second) { return First.X * Second.X + First.Y * Second.Y; }
    DualPoint Difference(const DualPoint& First, const DualPoint& Second) { return { First.X - Second.X, First.Y - Second.Y }; }
    LocalDual Norm(const DualPoint& Vector) { return SquareRoot(DotProduct(Vector, Vector)); }

    // 📝 One scalar equation of one constraint; Variables are global unknown indices (x₀ y₀ x₁ y₁ … then circle radii).
    struct SketchEquation
    {
        int                               Constraint      = -1;                              // [-] - Index into the extended constraint list
        int                               Row             = 0;                               // [-] - 0 or 1 within the constraint
        bool                              LengthCondition = true;                            // [-] - Residual is a length (else dimensionless)
        std::array<int, LocalUnknowns>    Variables       = {};                              // [-] - Global unknowns touched
        int                               Count           = 0;                               // [-] - Used slots
        bool                              InverseCondition = false;                          // [-] - Residual is a curvature (1 / len)
        bool                              WideCondition   = false;                           // [-] - Spline row: dense gradient over WideVariables
        std::vector<int>                  WideVariables   = {};                              // [-] - Global unknowns of a spline row (sorted)
    };

    // 📝 Evaluation context: in Collect mode every unknown touched is registered; otherwise unknowns are seeded.
    class EquationContext
    {
    public:
        EquationContext(const SketchModel& Sketch, const std::vector<double>& Unknowns, SketchEquation& Equation, bool CollectCondition)
            : Sketch(Sketch), Unknowns(Unknowns), Equation(Equation), CollectCondition(CollectCondition) {}

        LocalDual Variable(int Index)
        {
            LocalDual Result;
            Result.Value = Unknowns[Index];
            int Slot = -1;
            for (int Probe = 0; Probe < Equation.Count; ++Probe) if (Equation.Variables[Probe] == Index) Slot = Probe;
            if (Slot < 0 && CollectCondition && Equation.Count < LocalUnknowns)
            {
                Slot = Equation.Count++;
                Equation.Variables[Slot] = Index;
            }
            if (Slot >= 0) Result.Derivative[Slot] = 1.0;
            return Result;
        }
        DualPoint Point(int Index) { return { Variable(2 * Index), Variable(2 * Index + 1) }; }
        DualPoint LineDirection(int Curve)
        {
            const SketchCurve& Line = Sketch.Curves[Curve];
            return Difference(Point(Line.End), Point(Line.Start));
        }
        LocalDual Radius(int Curve)
        {
            const SketchCurve& Shape = Sketch.Curves[Curve];
            if (Shape.Kind == SketchCurveKind::Arc) return Norm(Difference(Point(Shape.Start), Point(Shape.Centre)));
            return Variable(2 * static_cast<int>(Sketch.Points.size()) + RadiusSlot(Curve));
        }
        int RadiusSlot(int Curve) const
        {
            int Slot = 0;
            for (int Index = 0; Index < Curve; ++Index) Slot += Sketch.Curves[Index].Kind == SketchCurveKind::Circle ? 1 : 0;
            return Slot;
        }

        const SketchModel&         Sketch;
        const std::vector<double>& Unknowns;
        SketchEquation&            Equation;
        bool                       CollectCondition;
    };

    [[nodiscard]] int EquationCount(SketchConstraintKind Kind) noexcept
    {
        switch (Kind)
        {
        case SketchConstraintKind::Coincident:
        case SketchConstraintKind::Fixed:
        case SketchConstraintKind::Midpoint:
        case SketchConstraintKind::Symmetric:
        case SketchConstraintKind::Concentric:
        case SketchConstraintKind::SplineCurvature: return 2;
        default:                               return 1;
        }
    }

    [[nodiscard]] bool RatioCondition(SketchConstraintKind Kind) noexcept
    {
        return Kind == SketchConstraintKind::Parallel || Kind == SketchConstraintKind::Perpendicular || Kind == SketchConstraintKind::Angle;
    }

    // Point index shared as an end by two open curves (line / arc), else −1.
    [[nodiscard]] int SharedEndpoint(const SketchCurve& First, const SketchCurve& Second) noexcept
    {
        if (First.Kind == SketchCurveKind::Circle || Second.Kind == SketchCurveKind::Circle) return -1;
        for (int Point : { First.Start, First.End }) if (Point >= 0 && (Point == Second.Start || Point == Second.End)) return Point;
        return -1;
    }

    // 📝 Tangency at a shared endpoint is posed as an angle (radius ⟂ line, or centres collinear with the joint): the
    //    distance form "line at r from the centre" plus "endpoint on the arc" meets at a double root, where J is singular
    //    and Newton converges only linearly.
    [[nodiscard]] bool RatioCondition(const SketchModel& Sketch, const SketchConstraint& Constraint) noexcept
    {
        if (RatioCondition(Constraint.Kind)) return true;
        return Constraint.Kind == SketchConstraintKind::Tangent && SharedEndpoint(Sketch.Curves[Constraint.First], Sketch.Curves[Constraint.Second]) >= 0;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                SPLINE ROWS
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] bool SplineKind(SketchConstraintKind Kind) noexcept
    {
        return Kind == SketchConstraintKind::SplineTangent || Kind == SketchConstraintKind::SplineCurvature || Kind == SketchConstraintKind::PointOnSpline
            || Kind == SketchConstraintKind::SplineDirection;
    }

    // 📝 A spline row touches every defining point (a cubic spline's end tangent depends on all of them), so it carries a
    //    dense gradient over its own variable list instead of the 8-slot local dual. An empty Derivative is a constant.
    struct WideDual
    {
        double              Value      = 0.0;                                                // [-] - f
        std::vector<double> Derivative = {};                                                 // [-] - ∂f / ∂(row variable)
    };

    [[nodiscard]] WideDual Blend(const WideDual& First, double FirstFactor, const WideDual& Second, double SecondFactor, double Value)
    {
        WideDual Result;
        Result.Value = Value;
        const size_t Size = std::max(First.Derivative.size(), Second.Derivative.size());
        Result.Derivative.assign(Size, 0.0);
        for (size_t Slot = 0; Slot < First.Derivative.size(); ++Slot) Result.Derivative[Slot] += FirstFactor * First.Derivative[Slot];
        for (size_t Slot = 0; Slot < Second.Derivative.size(); ++Slot) Result.Derivative[Slot] += SecondFactor * Second.Derivative[Slot];
        return Result;
    }
    WideDual operator+(const WideDual& First, const WideDual& Second) { return Blend(First, 1.0, Second, 1.0, First.Value + Second.Value); }
    WideDual operator-(const WideDual& First, const WideDual& Second) { return Blend(First, 1.0, Second, -1.0, First.Value - Second.Value); }
    WideDual operator*(const WideDual& First, const WideDual& Second) { return Blend(First, Second.Value, Second, First.Value, First.Value * Second.Value); }
    WideDual operator*(const WideDual& First, double Factor) { return Blend(First, Factor, {}, 0.0, First.Value * Factor); }
    WideDual operator/(const WideDual& First, const WideDual& Second)
    {
        const double Quotient = First.Value / Second.Value;
        return Blend(First, 1.0 / Second.Value, Second, -Quotient / Second.Value, Quotient);
    }
    WideDual SquareRoot(const WideDual& Argument)
    {
        const double Root = std::sqrt(std::max(0.0, Argument.Value));
        return Blend(Argument, Root > 1.0e-300 ? 0.5 / Root : 0.0, {}, 0.0, Root);
    }

    struct WidePoint
    {
        WideDual X = {};                                                                     // [len] - x
        WideDual Y = {};                                                                     // [len] - y
    };

    WideDual  CrossProduct(const WidePoint& First, const WidePoint& Second) { return First.X * Second.Y - First.Y * Second.X; }
    WideDual  DotProduct(const WidePoint& First, const WidePoint& Second) { return First.X * Second.X + First.Y * Second.Y; }
    WidePoint Difference(const WidePoint& First, const WidePoint& Second) { return { First.X - Second.X, First.Y - Second.Y }; }
    WidePoint Perpendicular(const WidePoint& Vector) { return { Vector.Y * -1.0, Vector.X }; }
    WideDual  Norm(const WidePoint& Vector) { return SquareRoot(DotProduct(Vector, Vector)); }

    [[nodiscard]] int CircleRadiusSlot(const SketchModel& Sketch, int Curve) noexcept
    {
        int Slot = 0;
        for (int Index = 0; Index < Curve; ++Index) Slot += Sketch.Curves[Index].Kind == SketchCurveKind::Circle ? 1 : 0;
        return 2 * static_cast<int>(Sketch.Points.size()) + Slot;
    }

    // Spline closed by its point list (first index = last index).
    [[nodiscard]] bool ClosedSpline(const SketchCurve& Curve) noexcept
    {
        return Curve.Points.size() > 2 && Curve.Points.front() == Curve.Points.back();
    }

    // Global unknowns a spline row touches: the points of every curve it names, circle radii, the lone point.
    [[nodiscard]] std::vector<int> CollectWideVariables(const SketchModel& Sketch, const SketchConstraint& Constraint)
    {
        std::vector<int> Variables;
        auto AddPoint = [&](int Point) { if (Point >= 0) { Variables.push_back(2 * Point); Variables.push_back(2 * Point + 1); } };
        auto AddCurve = [&](int Index)
        {
            if (Index < 0 || Index >= static_cast<int>(Sketch.Curves.size())) return;
            const SketchCurve& Curve = Sketch.Curves[Index];
            if (Curve.Kind == SketchCurveKind::Spline) { for (int Point : Curve.Points) AddPoint(Point); return; }
            for (int Point : { Curve.Start, Curve.End, Curve.Centre }) AddPoint(Point);
            if (Curve.Kind == SketchCurveKind::Circle) Variables.push_back(CircleRadiusSlot(Sketch, Index));
        };
        switch (Constraint.Kind)
        {
        case SketchConstraintKind::SplineTangent:
        case SketchConstraintKind::SplineCurvature: AddCurve(Constraint.First); AddCurve(Constraint.Second); AddPoint(Constraint.Third); break;
        case SketchConstraintKind::PointOnSpline:   AddPoint(Constraint.First); AddCurve(Constraint.Second); break;
        case SketchConstraintKind::SplineDirection: AddCurve(Constraint.First); break;
        default:                                    break;
        }
        std::sort(Variables.begin(), Variables.end());
        Variables.erase(std::unique(Variables.begin(), Variables.end()), Variables.end());
        return Variables;
    }

    class WideContext
    {
    public:
        WideContext(const SketchModel& Sketch, const std::vector<double>& Raw, const std::vector<int>& Variables)
            : Sketch(Sketch), Raw(Raw), Variables(Variables) {}

        WideDual Variable(int Index) const
        {
            WideDual Result;
            Result.Value = Raw[Index];
            const auto Found = std::lower_bound(Variables.begin(), Variables.end(), Index);
            if (Found != Variables.end() && *Found == Index)
            {
                Result.Derivative.assign(Variables.size(), 0.0);
                Result.Derivative[static_cast<size_t>(Found - Variables.begin())] = 1.0;
            }
            return Result;
        }
        WidePoint Point(int Index) const { return { Variable(2 * Index), Variable(2 * Index + 1) }; }
        Vector2   Position(int Index) const { return { Raw[2 * Index], Raw[2 * Index + 1] }; }
        WideDual  Radius(int Curve) const
        {
            const SketchCurve& Shape = Sketch.Curves[Curve];
            if (Shape.Kind == SketchCurveKind::Arc) return Norm(Difference(Point(Shape.Start), Point(Shape.Centre)));
            return Variable(CircleRadiusSlot(Sketch, Curve));
        }
        // Σ wⱼ·Qⱼ over the spline's defining slots.
        WidePoint Combination(const SketchCurve& Spline, const std::vector<double>& Weights) const
        {
            WidePoint Sum;
            for (size_t Slot = 0; Slot < Spline.Points.size() && Slot < Weights.size(); ++Slot)
            {
                if (Weights[Slot] == 0.0) continue;
                const WidePoint Term = Point(Spline.Points[Slot]);
                Sum.X = Sum.X + Term.X * Weights[Slot];
                Sum.Y = Sum.Y + Term.Y * Weights[Slot];
            }
            return Sum;
        }

        const SketchModel&         Sketch;
        const std::vector<double>& Raw;
        const std::vector<int>&    Variables;
    };

    // 📝 C′ and C″ of a spline at one end (StartCondition: τ = first knot) as weighted sums of its defining points.
    struct WideEnd
    {
        WidePoint   Tangent   = {};                                                          // [len] - C′
        WidePoint   Bending   = {};                                                          // [len] - C″
        bool        ValidCondition = false;                                                  // [-]   - Weights resolved
    };

    [[nodiscard]] WideEnd SplineEnd(const WideContext& Context, const SketchCurve& Spline, bool StartCondition)
    {
        WideEnd End;
        const double Infinity = std::numeric_limits<double>::infinity();
        const SketchSplineWeights Weights = WeighSketchSpline(Spline.Form, static_cast<int>(Spline.Points.size()), ClosedSpline(Spline), StartCondition ? -Infinity : Infinity, Spline.Knots);
        if (!Weights.Refusal.empty()) return End;
        End.Tangent        = Context.Combination(Spline, Weights.First);
        End.Bending        = Context.Combination(Spline, Weights.Second);
        End.ValidCondition = true;
        return End;
    }

    // Signed curvature κ = (C′ × C″) / |C′|³.
    [[nodiscard]] WideDual SignedCurvature(const WideEnd& End)
    {
        const WideDual Speed = Norm(End.Tangent);
        return CrossProduct(End.Tangent, End.Bending) / (Speed * Speed * Speed);
    }

    // 📝 Spline residual in raw units (tangent / direction: sin of an angle · curvature: 1 / len · on-spline: length).
    WideDual EvaluateSplineEquation(const WideContext& Context, const SketchConstraint& Constraint, int Row)
    {
        const SketchModel& Sketch = Context.Sketch;
        switch (Constraint.Kind)
        {
        case SketchConstraintKind::SplineDirection:
        {
            const SketchCurve& Spline = Sketch.Curves[Constraint.First];
            const WideEnd      End    = SplineEnd(Context, Spline, Constraint.Third == Spline.Points.front());
            if (!End.ValidCondition) return {};
            return (End.Tangent.Y * std::cos(Constraint.Value) - End.Tangent.X * std::sin(Constraint.Value)) / Norm(End.Tangent);
        }
        case SketchConstraintKind::SplineTangent:
        case SketchConstraintKind::SplineCurvature:
        {
            const SketchCurve& Spline = Sketch.Curves[Constraint.First];
            const SketchCurve& Other  = Sketch.Curves[Constraint.Second];
            const WideEnd      End    = SplineEnd(Context, Spline, Constraint.Third == Spline.Points.front());
            if (!End.ValidCondition) return {};
            WidePoint Direction;
            WideDual  OtherCurvature;
            if (Other.Kind == SketchCurveKind::Line) Direction = Difference(Context.Point(Other.End), Context.Point(Other.Start));
            else if (Other.Kind == SketchCurveKind::Spline)
            {
                const WideEnd OtherEnd = SplineEnd(Context, Other, Constraint.SecondValue < 0.5);
                if (!OtherEnd.ValidCondition) return {};
                Direction      = OtherEnd.Tangent;
                OtherCurvature = SignedCurvature(OtherEnd);
            }
            else
            {
                // Circle / arc: counter-clockwise tangent at the spline's end point, curvature +1 / r.
                Direction      = Perpendicular(Difference(Context.Point(Constraint.Third), Context.Point(Other.Centre)));
                OtherCurvature = WideDual{ 1.0 } / Context.Radius(Constraint.Second);
            }
            if (Row == 0) return CrossProduct(End.Tangent, Direction) / (Norm(End.Tangent) * Norm(Direction));
            // Opposite traversal at the joint flips the other curvature's sign (a constant on each side of the root).
            const double Orientation = DotProduct(End.Tangent, Direction).Value < 0.0 ? -1.0 : 1.0;
            return SignedCurvature(End) - OtherCurvature * Orientation;
        }
        case SketchConstraintKind::PointOnSpline:
        {
            // Signed distance n·(P − C(τ*)) with τ* the closest parameter: ∂/∂τ vanishes there (envelope theorem), so τ*
            // and n enter as constants and the gradient is exact: n for P, −wⱼ(τ*)·n for each defining point.
            const SketchCurve&   Spline = Sketch.Curves[Constraint.Second];
            std::vector<Vector2> Points;
            Vector2              Low{ 1e300, 1e300 }, High{ -1e300, -1e300 };
            for (int Index : Spline.Points)
            {
                const Vector2 Position = Context.Position(Index);
                Points.push_back(Position);
                Low  = { std::min(Low.X, Position.X), std::min(Low.Y, Position.Y) };
                High = { std::max(High.X, Position.X), std::max(High.Y, Position.Y) };
            }
            const SketchSplineOutcome Resolved = ResolveSketchSpline(Spline.Form, Points, std::max(Distance(Low, High), 1.0e-300), Spline.Knots);
            if (!Resolved.Refusal.empty()) return {};
            const Vector2 Target = Context.Position(Constraint.First);
            const std::vector<double> Breaks = CollectSketchSplineBreaks(Resolved.Curve);
            double Best = Breaks.front(), BestDistance = 1e300;
            for (size_t Span = 0; Span + 1 < Breaks.size(); ++Span)
            {
                for (int Sample = 0; Sample <= 32; ++Sample)
                {
                    const double Tau = Breaks[Span] + (Breaks[Span + 1] - Breaks[Span]) * Sample / 32.0;
                    const double Gap = Distance(EvaluateSketchSpline(Resolved.Curve, Tau).Position, Target);
                    if (Gap < BestDistance) { BestDistance = Gap; Best = Tau; }
                }
            }
            for (int Iteration = 0; Iteration < 40; ++Iteration)
            {
                const SketchSplineDerivatives At = EvaluateSketchSpline(Resolved.Curve, Best);
                const Vector2 Offset = At.Position - Target;
                const double  Slope  = Dot(At.First, At.First) + Dot(Offset, At.Second);
                if (std::fabs(Slope) < 1.0e-300) break;
                const double Next = std::clamp(Best - Dot(Offset, At.First) / Slope, Breaks.front(), Breaks.back());
                const bool   Done = std::fabs(Next - Best) <= 1.0e-15 * std::max(1.0, std::fabs(Best));
                Best = Next;
                if (Done) break;
            }
            const SketchSplineWeights Weights = WeighSketchSpline(Spline.Form, static_cast<int>(Spline.Points.size()), Resolved.Closed || ClosedSpline(Spline), Best, Spline.Knots);
            if (!Weights.Refusal.empty()) return {};
            const Vector2 Velocity = EvaluateSketchSpline(Resolved.Curve, Best).First;
            const double  Speed    = std::sqrt(Dot(Velocity, Velocity));
            if (Speed < 1.0e-300) return {};
            const Vector2   Normal = { -Velocity.Y / Speed, Velocity.X / Speed };
            const WidePoint Foot   = Context.Combination(Spline, Weights.Position);
            const WidePoint Offset = Difference(Context.Point(Constraint.First), Foot);
            return Offset.X * Normal.X + Offset.Y * Normal.Y;
        }
        default: return {};
        }
    }

    // 📝 Residual of one equation in raw units (length, or dimensionless for the angular family).
    LocalDual EvaluateEquation(EquationContext& Context, const SketchConstraint& Constraint, int Row)
    {
        const SketchModel& Sketch = Context.Sketch;
        switch (Constraint.Kind)
        {
        case SketchConstraintKind::Coincident:
        {
            const DualPoint First = Context.Point(Constraint.First), Second = Context.Point(Constraint.Second);
            return Row == 0 ? First.X - Second.X : First.Y - Second.Y;
        }
        case SketchConstraintKind::Fixed:
        {
            const DualPoint Point = Context.Point(Constraint.First);
            return Row == 0 ? Point.X - Constraint.Value : Point.Y - Constraint.SecondValue;
        }
        case SketchConstraintKind::Horizontal: return Context.LineDirection(Constraint.First).Y;
        case SketchConstraintKind::Vertical:   return Context.LineDirection(Constraint.First).X;
        case SketchConstraintKind::Distance:   return Norm(Difference(Context.Point(Constraint.Second), Context.Point(Constraint.First))) - Constraint.Value;
        case SketchConstraintKind::HorizontalDistance: return Context.Point(Constraint.Second).X - Context.Point(Constraint.First).X - Constraint.Value;
        case SketchConstraintKind::VerticalDistance:   return Context.Point(Constraint.Second).Y - Context.Point(Constraint.First).Y - Constraint.Value;
        case SketchConstraintKind::Length:     return Norm(Context.LineDirection(Constraint.First)) - Constraint.Value;
        case SketchConstraintKind::Parallel:
        case SketchConstraintKind::Perpendicular:
        case SketchConstraintKind::Angle:
        {
            const DualPoint First = Context.LineDirection(Constraint.First), Second = Context.LineDirection(Constraint.Second);
            const LocalDual Scale = Norm(First) * Norm(Second);
            if (Constraint.Kind == SketchConstraintKind::Parallel) return CrossProduct(First, Second) / Scale;
            if (Constraint.Kind == SketchConstraintKind::Perpendicular) return DotProduct(First, Second) / Scale;
            // sin(φ − θ) with φ the CCW angle from the first line to the second.
            return (CrossProduct(First, Second) * std::cos(Constraint.Value) - DotProduct(First, Second) * std::sin(Constraint.Value)) / Scale;
        }
        case SketchConstraintKind::EqualLength: return Norm(Context.LineDirection(Constraint.First)) - Norm(Context.LineDirection(Constraint.Second));
        case SketchConstraintKind::Radius:      return Context.Radius(Constraint.First) - Constraint.Value;
        case SketchConstraintKind::EqualRadius: return Context.Radius(Constraint.First) - Context.Radius(Constraint.Second);
        case SketchConstraintKind::PointOnLine:
        {
            const SketchCurve& Line = Sketch.Curves[Constraint.Second];
            const DualPoint Direction = Context.LineDirection(Constraint.Second);
            return CrossProduct(Direction, Difference(Context.Point(Constraint.First), Context.Point(Line.Start))) / Norm(Direction);
        }
        case SketchConstraintKind::PointOnCircle:
        {
            const SketchCurve& Shape = Sketch.Curves[Constraint.Second];
            return Norm(Difference(Context.Point(Constraint.First), Context.Point(Shape.Centre))) - Context.Radius(Constraint.Second);
        }
        case SketchConstraintKind::Tangent:
        {
            const SketchCurve& First = Sketch.Curves[Constraint.First];
            const SketchCurve& Second = Sketch.Curves[Constraint.Second];
            const int          Joint  = SharedEndpoint(First, Second);
            if (Joint >= 0)
            {
                // 📝 Side kept (Value = ±1, set by the solver from the starting positions): radius ⟂ line has two roots,
                //    the centre on either side of the line, and the far one turns a fillet into its 2π − sweep complement
                //    (a corner drag flipped the arc inside out). The bearing from the line to the radius, minus ±π/2, has
                //    only the starting side as a root and the same slope there as the cosine form. Value 0: either side.
                const DualPoint Contact = Context.Point(Joint);
                const DualPoint Radial  = Difference(Contact, Context.Point(Second.Centre));
                if (First.Kind == SketchCurveKind::Line)
                {
                    const DualPoint Direction = Context.LineDirection(Constraint.First);
                    if (Constraint.Value != 0.0) return Bearing(CrossProduct(Direction, Radial), DotProduct(Direction, Radial)) - Constraint.Value * 0.5 * std::numbers::pi;
                    return DotProduct(Radial, Direction) / (Norm(Radial) * Norm(Direction));
                }
                // Arc to arc: centres on one line through the joint, on the same side (Value +1) or opposite (−1).
                const DualPoint Other = Difference(Contact, Context.Point(First.Centre));
                if (Constraint.Value != 0.0) return Bearing(CrossProduct(Other, Radial) * Constraint.Value, DotProduct(Other, Radial) * Constraint.Value);
                return CrossProduct(Other, Radial) / (Norm(Other) * Norm(Radial));
            }
            if (First.Kind == SketchCurveKind::Line)
            {
                const DualPoint Direction = Context.LineDirection(Constraint.First);
                const LocalDual Offset    = CrossProduct(Direction, Difference(Context.Point(Second.Centre), Context.Point(First.Start))) / Norm(Direction);
                return Magnitude(Offset) - Context.Radius(Constraint.Second);
            }
            const LocalDual Separation = Norm(Difference(Context.Point(Second.Centre), Context.Point(First.Centre)));
            if (Constraint.SecondValue < 0.0) return Separation - Magnitude(Context.Radius(Constraint.First) - Context.Radius(Constraint.Second));
            return Separation - (Context.Radius(Constraint.First) + Context.Radius(Constraint.Second));
        }
        case SketchConstraintKind::Midpoint:
        {
            const SketchCurve& Line = Sketch.Curves[Constraint.Second];
            const DualPoint Point = Context.Point(Constraint.First), Start = Context.Point(Line.Start), End = Context.Point(Line.End);
            return Row == 0 ? Point.X * 2.0 - Start.X - End.X : Point.Y * 2.0 - Start.Y - End.Y;
        }
        case SketchConstraintKind::Symmetric:
        {
            const SketchCurve& Line = Sketch.Curves[Constraint.Third];
            const DualPoint First = Context.Point(Constraint.First), Second = Context.Point(Constraint.Second);
            const DualPoint Direction = Context.LineDirection(Constraint.Third);
            if (Row == 0)
            {
                const DualPoint Middle = { (First.X + Second.X) * 0.5, (First.Y + Second.Y) * 0.5 };
                return CrossProduct(Direction, Difference(Middle, Context.Point(Line.Start))) / Norm(Direction);
            }
            return DotProduct(Difference(Second, First), Direction) / Norm(Direction);
        }
        case SketchConstraintKind::Concentric:
        {
            const DualPoint First = Context.Point(Sketch.Curves[Constraint.First].Centre), Second = Context.Point(Sketch.Curves[Constraint.Second].Centre);
            return Row == 0 ? First.X - Second.X : First.Y - Second.Y;
        }
        case SketchConstraintKind::ArcRule:
        {
            const SketchCurve& Arc = Sketch.Curves[Constraint.First];
            const DualPoint Centre = Context.Point(Arc.Centre);
            return Norm(Difference(Context.Point(Arc.Start), Centre)) - Norm(Difference(Context.Point(Arc.End), Centre));
        }
        case SketchConstraintKind::SplineTangent:
        case SketchConstraintKind::SplineCurvature:
        case SketchConstraintKind::PointOnSpline:
        case SketchConstraintKind::SplineDirection:
            break;                                                                           // Spline kinds: dense gradients (above)
        }
        return {};
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                DENSE LINEAR ALGEBRA
    //--------------------------------------------------------------------------------------------------------------------

    struct DenseMatrix
    {
        int                 Rows    = 0;                                                     // [-] - Row count
        int                 Columns = 0;                                                     // [-] - Column count
        std::vector<double> Values  = {};                                                    // [-] - Row-major

        DenseMatrix() = default;
        DenseMatrix(int RowCount, int ColumnCount) : Rows(RowCount), Columns(ColumnCount), Values(static_cast<size_t>(RowCount) * ColumnCount, 0.0) {}
        double& At(int Row, int Column) { return Values[static_cast<size_t>(Row) * Columns + Column]; }
        double  At(int Row, int Column) const { return Values[static_cast<size_t>(Row) * Columns + Column]; }
    };

    // 📝 A·P = Q·R by Householder reflections with column pivoting (largest remaining column first): the first Rank
    //    pivots are linearly independent columns, Q[:, Rank:] spans the orthogonal complement of their span.
    struct PivotedDecomposition
    {
        DenseMatrix      Q      = {};                                                        // [-] - Rows × Rows orthogonal
        DenseMatrix      R      = {};                                                        // [-] - Rows × Columns upper trapezoidal (pivoted)
        std::vector<int> Pivots = {};                                                        // [-] - Column order
        int              Rank   = 0;                                                         // [-] - |R_kk| above the cut
    };

    // 📝 OrderedCondition: take the first column (authoring order) still above the cut instead of the largest one, so the
    //    earliest constraints form the independent set and the later ones are reported as redundant / conflicting.
    [[nodiscard]] PivotedDecomposition DecomposeColumnPivoted(DenseMatrix Matrix, double RankTolerance, bool OrderedCondition = true)
    {
        PivotedDecomposition Result;
        const int Rows = Matrix.Rows, Columns = Matrix.Columns;
        Result.Q = DenseMatrix(Rows, Rows);
        for (int Index = 0; Index < Rows; ++Index) Result.Q.At(Index, Index) = 1.0;
        Result.Pivots.resize(Columns);
        for (int Column = 0; Column < Columns; ++Column) Result.Pivots[Column] = Column;
        std::vector<double> Reflector(Rows, 0.0);
        const int Steps = std::min(Rows, Columns);
        double Leading = 0.0;                                                                // [-] - Largest column norm (cut reference)
        for (int Column = 0; Column < Columns; ++Column)
        {
            double Sum = 0.0;
            for (int Row = 0; Row < Rows; ++Row) Sum += Matrix.At(Row, Column) * Matrix.At(Row, Column);
            Leading = std::max(Leading, std::sqrt(Sum));
        }
        const double Cut = RankTolerance * std::max(1.0, Leading);                           // [-] - Remaining-norm threshold
        for (int Step = 0; Step < Steps; ++Step)
        {
            int    Chosen = Step;
            double Best   = -1.0;
            for (int Column = Step; Column < Columns; ++Column)
            {
                double Sum = 0.0;
                for (int Row = Step; Row < Rows; ++Row) Sum += Matrix.At(Row, Column) * Matrix.At(Row, Column);
                if (OrderedCondition && std::sqrt(Sum) > Cut) { Best = Sum; Chosen = Column; break; }
                if (Sum > Best) { Best = Sum; Chosen = Column; }
            }
            if (Chosen != Step)
            {
                for (int Row = 0; Row < Rows; ++Row) std::swap(Matrix.At(Row, Step), Matrix.At(Row, Chosen));
                std::swap(Result.Pivots[Step], Result.Pivots[Chosen]);
            }
            const double ColumnNorm = std::sqrt(std::max(0.0, Best));
            if (ColumnNorm <= Cut || ColumnNorm < 1.0e-300) break;
            ++Result.Rank;
            const double Alpha = Matrix.At(Step, Step) > 0.0 ? -ColumnNorm : ColumnNorm;
            double ReflectorNorm = 0.0;
            for (int Row = Step; Row < Rows; ++Row)
            {
                Reflector[Row] = Matrix.At(Row, Step) - (Row == Step ? Alpha : 0.0);
                ReflectorNorm += Reflector[Row] * Reflector[Row];
            }
            if (ReflectorNorm <= 1.0e-300) continue;
            const double InverseNorm = 1.0 / std::sqrt(ReflectorNorm);
            for (int Row = Step; Row < Rows; ++Row) Reflector[Row] *= InverseNorm;
            for (int Column = Step; Column < Columns; ++Column)
            {
                double Projection = 0.0;
                for (int Row = Step; Row < Rows; ++Row) Projection += Reflector[Row] * Matrix.At(Row, Column);
                for (int Row = Step; Row < Rows; ++Row) Matrix.At(Row, Column) -= 2.0 * Projection * Reflector[Row];
            }
            for (int Row = 0; Row < Rows; ++Row)
            {
                double Projection = 0.0;
                for (int Inner = Step; Inner < Rows; ++Inner) Projection += Result.Q.At(Row, Inner) * Reflector[Inner];
                for (int Inner = Step; Inner < Rows; ++Inner) Result.Q.At(Row, Inner) -= 2.0 * Projection * Reflector[Inner];
            }
        }
        Result.R = std::move(Matrix);
        return Result;
    }

    // Solves (A + λI)·x = b by Gaussian elimination with partial pivoting (A symmetric positive semi-definite here).
    [[nodiscard]] std::vector<double> SolveDense(DenseMatrix Matrix, std::vector<double> Right)
    {
        const int Size = Matrix.Rows;
        for (int Step = 0; Step < Size; ++Step)
        {
            int Chosen = Step;
            for (int Row = Step + 1; Row < Size; ++Row) if (std::fabs(Matrix.At(Row, Step)) > std::fabs(Matrix.At(Chosen, Step))) Chosen = Row;
            if (std::fabs(Matrix.At(Chosen, Step)) < 1.0e-300) continue;
            if (Chosen != Step)
            {
                for (int Column = 0; Column < Size; ++Column) std::swap(Matrix.At(Step, Column), Matrix.At(Chosen, Column));
                std::swap(Right[Step], Right[Chosen]);
            }
            for (int Row = Step + 1; Row < Size; ++Row)
            {
                const double Factor = Matrix.At(Row, Step) / Matrix.At(Step, Step);
                if (Factor == 0.0) continue;
                for (int Column = Step; Column < Size; ++Column) Matrix.At(Row, Column) -= Factor * Matrix.At(Step, Column);
                Right[Row] -= Factor * Right[Step];
            }
        }
        std::vector<double> Solution(Size, 0.0);
        for (int Row = Size - 1; Row >= 0; --Row)
        {
            double Sum = Right[Row];
            for (int Column = Row + 1; Column < Size; ++Column) Sum -= Matrix.At(Row, Column) * Solution[Column];
            Solution[Row] = std::fabs(Matrix.At(Row, Row)) < 1.0e-300 ? 0.0 : Sum / Matrix.At(Row, Row);
        }
        return Solution;
    }

    [[nodiscard]] DenseMatrix Transpose(const DenseMatrix& Matrix)
    {
        DenseMatrix Result(Matrix.Columns, Matrix.Rows);
        for (int Row = 0; Row < Matrix.Rows; ++Row) for (int Column = 0; Column < Matrix.Columns; ++Column) Result.At(Column, Row) = Matrix.At(Row, Column);
        return Result;
    }

    [[nodiscard]] double MeasureConditioning(const DenseMatrix& Jacobian, double RankTolerance)
    {
        const PivotedDecomposition Largest = DecomposeColumnPivoted(Transpose(Jacobian), RankTolerance, false);
        double Conditioning = 1.0;
        for (int Index = 1; Index < Largest.Rank; ++Index) Conditioning = std::min(Conditioning, std::fabs(Largest.R.At(Index, Index)) / std::fabs(Largest.R.At(0, 0)));
        return Conditioning;
    }

    // Every reference exists and has the kind its slot expects (P point · L line · C circle or arc · S spline · E an end of
    // an open spline); a spline is never a line or circle reference.
    [[nodiscard]] bool ReferenceCondition(const SketchModel& Sketch, const SketchConstraint& Constraint)
    {
        const int PointCount = static_cast<int>(Sketch.Points.size()), CurveCount = static_cast<int>(Sketch.Curves.size());
        auto PointSlot  = [&](int Index) { return Index >= 0 && Index < PointCount; };
        auto LineSlot   = [&](int Index) { return Index >= 0 && Index < CurveCount && Sketch.Curves[Index].Kind == SketchCurveKind::Line; };
        auto CircleSlot = [&](int Index)
        {
            return Index >= 0 && Index < CurveCount && (Sketch.Curves[Index].Kind == SketchCurveKind::Circle || Sketch.Curves[Index].Kind == SketchCurveKind::Arc);
        };
        auto SplineSlot = [&](int Index)
        {
            if (Index < 0 || Index >= CurveCount || Sketch.Curves[Index].Kind != SketchCurveKind::Spline || Sketch.Curves[Index].Points.size() < 2) return false;
            for (int Point : Sketch.Curves[Index].Points) if (!PointSlot(Point)) return false;
            return true;
        };
        auto EndSlot = [&](int Spline, int Point)
        {
            if (!SplineSlot(Spline) || ClosedSpline(Sketch.Curves[Spline])) return false;
            return Point == Sketch.Curves[Spline].Points.front() || Point == Sketch.Curves[Spline].Points.back();
        };
        switch (Constraint.Kind)
        {
        case SketchConstraintKind::Coincident:
        case SketchConstraintKind::Distance:
        case SketchConstraintKind::HorizontalDistance:
        case SketchConstraintKind::VerticalDistance:   return PointSlot(Constraint.First) && PointSlot(Constraint.Second);
        case SketchConstraintKind::Fixed:              return PointSlot(Constraint.First);
        case SketchConstraintKind::Horizontal:
        case SketchConstraintKind::Vertical:
        case SketchConstraintKind::Length:             return LineSlot(Constraint.First);
        case SketchConstraintKind::Parallel:
        case SketchConstraintKind::Perpendicular:
        case SketchConstraintKind::Angle:
        case SketchConstraintKind::EqualLength:        return LineSlot(Constraint.First) && LineSlot(Constraint.Second);
        case SketchConstraintKind::Radius:             return CircleSlot(Constraint.First);
        case SketchConstraintKind::EqualRadius:
        case SketchConstraintKind::Concentric:         return CircleSlot(Constraint.First) && CircleSlot(Constraint.Second);
        case SketchConstraintKind::PointOnLine:
        case SketchConstraintKind::Midpoint:           return PointSlot(Constraint.First) && LineSlot(Constraint.Second);
        case SketchConstraintKind::PointOnCircle:      return PointSlot(Constraint.First) && CircleSlot(Constraint.Second);
        case SketchConstraintKind::Tangent:            return (LineSlot(Constraint.First) || CircleSlot(Constraint.First)) && CircleSlot(Constraint.Second);
        case SketchConstraintKind::Symmetric:          return PointSlot(Constraint.First) && PointSlot(Constraint.Second) && LineSlot(Constraint.Third);
        case SketchConstraintKind::ArcRule:            return CircleSlot(Constraint.First);
        case SketchConstraintKind::SplineTangent:
        case SketchConstraintKind::SplineCurvature:
        {
            if (!EndSlot(Constraint.First, Constraint.Third) || Constraint.Second == Constraint.First) return false;
            if (LineSlot(Constraint.Second) || CircleSlot(Constraint.Second)) return true;
            return SplineSlot(Constraint.Second) && !ClosedSpline(Sketch.Curves[Constraint.Second]);
        }
        case SketchConstraintKind::PointOnSpline:      return PointSlot(Constraint.First) && SplineSlot(Constraint.Second);
        case SketchConstraintKind::SplineDirection:    return EndSlot(Constraint.First, Constraint.Third);
        }
        return false;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                SYSTEM ASSEMBLY
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 The scaled equation division: unknowns ξ = x / L, residuals F / s with s = L for length rows and 1 for angular rows, so J is
    //    dimensionless and the rank cut is scale-free.
    class ScaledEquationDivision
    {
    public:
        ScaledEquationDivision(const SketchModel& Sketch, const std::vector<SketchConstraint>& Constraints, double Scale, const std::vector<bool>& Excluded)
            : Sketch(Sketch), Constraints(Constraints), Scale(Scale)
        {
            Unknowns = 2 * static_cast<int>(Sketch.Points.size());
            for (const SketchCurve& Curve : Sketch.Curves) Unknowns += Curve.Kind == SketchCurveKind::Circle ? 1 : 0;
            // Linked unknowns stay where the feature tree projected them: their Jacobian columns are left at zero.
            Frozen.assign(Unknowns, false);
            for (size_t Point = 0; Point < Sketch.Points.size(); ++Point)
            {
                if (Sketch.Points[Point].Link.Sketch.empty()) continue;
                Frozen[2 * Point] = Frozen[2 * Point + 1] = true;
            }
            for (int Curve = 0; Curve < static_cast<int>(Sketch.Curves.size()); ++Curve)
            {
                if (Sketch.Curves[Curve].Kind == SketchCurveKind::Circle && !Sketch.Curves[Curve].Link.Sketch.empty()) Frozen[CircleRadiusSlot(Sketch, Curve)] = true;
            }
            FrozenCount = static_cast<int>(std::count(Frozen.begin(), Frozen.end(), true));
            std::vector<double> Probe = Pack();
            for (int Index = 0; Index < static_cast<int>(Constraints.size()); ++Index)
            {
                if (Index < static_cast<int>(Excluded.size()) && Excluded[Index]) continue;
                for (int Row = 0; Row < EquationCount(Constraints[Index].Kind); ++Row)
                {
                    SketchEquation Equation;
                    Equation.Constraint      = Index;
                    Equation.Row             = Row;
                    if (SplineKind(Constraints[Index].Kind))
                    {
                        const SketchConstraintKind Kind = Constraints[Index].Kind;
                        Equation.WideCondition    = true;
                        Equation.WideVariables    = CollectWideVariables(Sketch, Constraints[Index]);
                        Equation.LengthCondition  = Kind == SketchConstraintKind::PointOnSpline;
                        Equation.InverseCondition = Kind == SketchConstraintKind::SplineCurvature && Row == 1;
                        Equations.push_back(Equation);
                        continue;
                    }
                    Equation.LengthCondition = !RatioCondition(Sketch, Constraints[Index]);
                    EquationContext Context(Sketch, Probe, Equation, true);
                    (void)EvaluateEquation(Context, Constraints[Index], Row);
                    Equations.push_back(Equation);
                }
            }
        }

        // Raw value of a scaled residual: × L for lengths, ÷ L for curvatures.
        [[nodiscard]] double RowUnit(const SketchEquation& Equation) const noexcept
        {
            return Equation.LengthCondition ? Scale : Equation.InverseCondition ? 1.0 / Scale : 1.0;
        }

        [[nodiscard]] std::vector<double> Pack() const
        {
            std::vector<double> Values;
            Values.reserve(Unknowns);
            for (const SketchPoint& Point : Sketch.Points) { Values.push_back(Point.Position.X); Values.push_back(Point.Position.Y); }
            for (const SketchCurve& Curve : Sketch.Curves) if (Curve.Kind == SketchCurveKind::Circle) Values.push_back(Curve.Radius);
            return Values;
        }

        static void Unpack(SketchModel& Target, const std::vector<double>& Values)
        {
            size_t Cursor = 0;
            for (SketchPoint& Point : Target.Points) { Point.Position.X = Values[Cursor]; Point.Position.Y = Values[Cursor + 1]; Cursor += 2; }
            for (SketchCurve& Curve : Target.Curves) if (Curve.Kind == SketchCurveKind::Circle) Curve.Radius = Values[Cursor++];
        }

        // Residuals (scaled) and, when wanted, the scaled Jacobian (equations × unknowns).
        void Evaluate(const std::vector<double>& Raw, std::vector<double>& Residuals, DenseMatrix* Jacobian) const
        {
            Residuals.assign(Equations.size(), 0.0);
            if (Jacobian) *Jacobian = DenseMatrix(static_cast<int>(Equations.size()), Unknowns);
            for (size_t Index = 0; Index < Equations.size(); ++Index)
            {
                const double Divisor = RowUnit(Equations[Index]);
                if (Equations[Index].WideCondition)
                {
                    const SketchEquation& Wide = Equations[Index];
                    const WideContext     Context(Sketch, Raw, Wide.WideVariables);
                    const WideDual        Result = EvaluateSplineEquation(Context, Constraints[Wide.Constraint], Wide.Row);
                    Residuals[Index] = Result.Value / Divisor;
                    if (!Jacobian) continue;
                    for (size_t Slot = 0; Slot < Result.Derivative.size(); ++Slot)
                    {
                        if (!Frozen[Wide.WideVariables[Slot]]) Jacobian->At(static_cast<int>(Index), Wide.WideVariables[Slot]) += Result.Derivative[Slot] * Scale / Divisor;
                    }
                    continue;
                }
                SketchEquation  Equation = Equations[Index];
                EquationContext Context(Sketch, Raw, Equation, false);
                const LocalDual Result = EvaluateEquation(Context, Constraints[Equation.Constraint], Equation.Row);
                Residuals[Index] = Result.Value / Divisor;
                if (!Jacobian) continue;
                for (int Slot = 0; Slot < Equation.Count; ++Slot)
                {
                    if (!Frozen[Equation.Variables[Slot]]) Jacobian->At(static_cast<int>(Index), Equation.Variables[Slot]) += Result.Derivative[Slot] * Scale / Divisor;
                }
            }
        }

        const SketchModel&                   Sketch;
        const std::vector<SketchConstraint>& Constraints;
        double                               Scale     = 1.0;
        int                                  Unknowns  = 0;
        std::vector<SketchEquation>          Equations = {};
        std::vector<bool>                    Frozen    = {};                                 // [-] - Per unknown: held by a link
        int                                  FrozenCount = 0;                                // [-] - How many
    };

    [[nodiscard]] double SquaredNorm(const std::vector<double>& Values, const std::vector<int>& Rows)
    {
        double Sum = 0.0;
        for (int Row : Rows) Sum += Values[Row] * Values[Row];
        return Sum;
    }

    [[nodiscard]] double MaxMagnitude(const std::vector<double>& Values)
    {
        double Largest = 0.0;
        for (double Value : Values) Largest = std::max(Largest, std::fabs(Value));
        return Largest;
    }

    [[nodiscard]] double MeasureScale(const SketchModel& Sketch, const std::vector<SketchConstraint>& Constraints)
    {
        Vector2 Low{ 1e300, 1e300 }, High{ -1e300, -1e300 };
        for (const SketchPoint& Point : Sketch.Points)
        {
            Low  = { std::min(Low.X, Point.Position.X), std::min(Low.Y, Point.Position.Y) };
            High = { std::max(High.X, Point.Position.X), std::max(High.Y, Point.Position.Y) };
        }
        double Scale = Sketch.Points.empty() ? 0.0 : Distance(Low, High);
        for (const SketchCurve& Curve : Sketch.Curves) if (Curve.Kind == SketchCurveKind::Circle) Scale = std::max(Scale, 2.0 * std::fabs(Curve.Radius));
        for (const SketchConstraint& Constraint : Constraints)
        {
            if (!RatioCondition(Constraint.Kind) && Constraint.Kind != SketchConstraintKind::Fixed) Scale = std::max(Scale, std::fabs(Constraint.Value));
        }
        return Scale > 1.0e-300 ? Scale : 1.0;
    }

    // 📝 Minimum-norm step over the independent rows: J = P·Rᵀ·Qᵀ from Jᵀ·P = Q·R, so R₁₁ᵀ·y = −(PᵀF)₁..ᵣ, Δ = Q₁·y.
    [[nodiscard]] std::vector<double> MinimumNormStep(const DenseMatrix& Jacobian, const std::vector<double>& Residuals, double RankTolerance,
                                                      std::vector<int>& IndependentRows)
    {
        const PivotedDecomposition Decomposition = DecomposeColumnPivoted(Transpose(Jacobian), RankTolerance);
        const int Rank = Decomposition.Rank;
        IndependentRows.assign(Decomposition.Pivots.begin(), Decomposition.Pivots.begin() + Rank);
        std::vector<double> Intermediate(Rank, 0.0);
        for (int Index = 0; Index < Rank; ++Index)
        {
            double Sum = -Residuals[Decomposition.Pivots[Index]];
            for (int Inner = 0; Inner < Index; ++Inner) Sum -= Decomposition.R.At(Inner, Index) * Intermediate[Inner];
            Intermediate[Index] = Sum / Decomposition.R.At(Index, Index);
        }
        std::vector<double> Step(Jacobian.Columns, 0.0);
        for (int Row = 0; Row < Jacobian.Columns; ++Row) for (int Index = 0; Index < Rank; ++Index) Step[Row] += Decomposition.Q.At(Row, Index) * Intermediate[Index];
        return Step;
    }

    [[nodiscard]] std::vector<double> DampedStep(const DenseMatrix& Jacobian, const std::vector<double>& Residuals, double Damping)
    {
        const int Unknowns = Jacobian.Columns;
        DenseMatrix Normal(Unknowns, Unknowns);
        std::vector<double> Right(Unknowns, 0.0);
        for (int Row = 0; Row < Jacobian.Rows; ++Row)
        {
            for (int First = 0; First < Unknowns; ++First)
            {
                const double Entry = Jacobian.At(Row, First);
                if (Entry == 0.0) continue;
                Right[First] -= Entry * Residuals[Row];
                for (int Second = 0; Second < Unknowns; ++Second) Normal.At(First, Second) += Entry * Jacobian.At(Row, Second);
            }
        }
        for (int Index = 0; Index < Unknowns; ++Index) Normal.At(Index, Index) += Damping;
        return SolveDense(std::move(Normal), std::move(Right));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SOLVER
//------------------------------------------------------------------------------------------------------------------------

// Side of each shared-endpoint tangency as it stands (±1 into Value; already held rows and other kinds untouched).
static void HoldTangentSides(const SketchModel& Sketch, std::vector<SketchConstraint>& Constraints, int Count)
{
    for (int Index = 0; Index < Count; ++Index)
    {
        SketchConstraint& Constraint = Constraints[Index];
        if (Constraint.Kind != SketchConstraintKind::Tangent || Constraint.Value != 0.0 || !ReferenceCondition(Sketch, Constraint)) continue;
        const SketchCurve& First  = Sketch.Curves[Constraint.First];
        const SketchCurve& Second = Sketch.Curves[Constraint.Second];
        const int          Joint  = SharedEndpoint(First, Second);
        if (Joint < 0 || Second.Centre < 0) continue;
        const Vector2 Contact = Sketch.Points[Joint].Position;
        const Vector2 Radial  = Contact - Sketch.Points[Second.Centre].Position;
        if (First.Kind == SketchCurveKind::Line)
        {
            const Vector2 Direction = Sketch.Points[First.End].Position - Sketch.Points[First.Start].Position;
            const double  Turn      = Cross(Direction, Radial);
            const double  Reach     = Length(Direction) * Length(Radial);
            if (Reach <= 1.0e-300) continue;
            if (std::fabs(Turn) > 1.0e-6 * Reach) { Constraint.Value = Turn > 0.0 ? 1.0 : -1.0; continue; }
            // Radius along the line (no side to keep yet): the smooth join. With the line running into the joint, the centre
            // of a counter-clockwise arc lies on its left when the joint starts the arc and on its right when it ends it.
            const double IntoArc = Joint == Second.Start ? -1.0 : 1.0;                       // sign of (into-joint direction × radius)
            Constraint.Value = Joint == First.End ? IntoArc : -IntoArc;
        }
        else if (First.Centre >= 0)
        {
            const Vector2 Other = Contact - Sketch.Points[First.Centre].Position;
            const double  Along = Dot(Other, Radial);
            if (std::fabs(Along) > 1.0e-6 * Length(Other) * Length(Radial)) Constraint.Value = Along > 0.0 ? 1.0 : -1.0;
        }
    }
}

void HoldSketchTangentSides(SketchModel& Sketch)
{
    HoldTangentSides(Sketch, Sketch.Constraints, static_cast<int>(Sketch.Constraints.size()));
}

static SketchSolveOutcome SolveSketchUntraced(SketchModel& Sketch, const SketchSolveSettings& Settings)
{
    const auto Started = std::chrono::steady_clock::now();
    SketchSolveOutcome Outcome;

    // ① Extended constraint list: user constraints, then one implicit ArcRule per arc.
    std::vector<SketchConstraint> Constraints = Sketch.Constraints;
    const int UserConstraints = static_cast<int>(Constraints.size());
    for (int Curve = 0; Curve < static_cast<int>(Sketch.Curves.size()); ++Curve)
    {
        if (Sketch.Curves[Curve].Kind != SketchCurveKind::Arc) continue;
        SketchConstraint Rule;
        Rule.Kind  = SketchConstraintKind::ArcRule;
        Rule.First = Curve;
        Rule.Label = "ArcRule(" + SketchCurveName(Sketch.Curves[Curve]) + ")";
        Constraints.push_back(Rule);
    }
    // Tangency at a shared endpoint keeps the side it starts on (EvaluateEquation, Tangent): Value = ±1 in this copy.
    HoldTangentSides(Sketch, Constraints, UserConstraints);
    std::vector<bool> Excluded(Constraints.size(), false);
    for (int Index = 0; Index < UserConstraints; ++Index) Excluded[Index] = !ReferenceCondition(Sketch, Constraints[Index]) || Constraints[Index].DrivenCondition;
    const double Scale = MeasureScale(Sketch, Constraints);
    const ScaledEquationDivision Division(Sketch, Constraints, Scale, Excluded);
    Outcome.Scale     = Scale;
    Outcome.Unknowns  = Division.Unknowns - Division.FrozenCount;
    Outcome.Linked    = Division.FrozenCount;
    Outcome.Equations = static_cast<int>(Division.Equations.size());

    // ② Minimum-norm Gauss–Newton with backtracking; Levenberg–Marquardt when the line search fails. The merit counts the
    //    independent rows only, so conflicting dependants are left unmet instead of smeared into a least-squares compromise.
    std::vector<double> Raw = Division.Pack();
    std::vector<double> Residuals, Trial, TrialResiduals;
    DenseMatrix         Jacobian;
    double              Damping = 1.0e-3;
    for (int Iteration = 0; Iteration < Settings.IterationLimit && Division.Unknowns > 0 && !Division.Equations.empty(); ++Iteration)
    {
        Division.Evaluate(Raw, Residuals, &Jacobian);
        if (MaxMagnitude(Residuals) <= Settings.Tolerance) break;
        Outcome.Iterations = Iteration + 1;
        std::vector<int> IndependentRows;
        const std::vector<double> Step = MinimumNormStep(Jacobian, Residuals, Settings.RankTolerance, IndependentRows);
        const double Merit = SquaredNorm(Residuals, IndependentRows);
        if (Merit <= Settings.Tolerance * Settings.Tolerance) break;                         // Only dependants remain unmet
        bool Accepted = false;
        for (double Fraction = 1.0; Fraction >= 1.0 / 1024.0 && !Accepted; Fraction *= 0.5)
        {
            Trial = Raw;
            for (int Index = 0; Index < Division.Unknowns; ++Index) Trial[Index] += Scale * Fraction * Step[Index];
            Division.Evaluate(Trial, TrialResiduals, nullptr);
            if (SquaredNorm(TrialResiduals, IndependentRows) <= (1.0 - 1.0e-4 * Fraction) * Merit) { Raw = Trial; Accepted = true; }
        }
        DenseMatrix         Reduced = Jacobian;
        std::vector<double> ReducedResiduals(Residuals.size(), 0.0);
        std::vector<bool>   Keep(Residuals.size(), false);
        for (int Row : IndependentRows) { Keep[Row] = true; ReducedResiduals[Row] = Residuals[Row]; }
        for (int Row = 0; Row < Reduced.Rows; ++Row) if (!Keep[Row]) for (int Column = 0; Column < Reduced.Columns; ++Column) Reduced.At(Row, Column) = 0.0;
        for (int Attempt = 0; Attempt < 12 && !Accepted; ++Attempt, Damping *= 10.0)
        {
            const std::vector<double> Damped = DampedStep(Reduced, ReducedResiduals, Damping);
            Trial = Raw;
            for (int Index = 0; Index < Division.Unknowns; ++Index) Trial[Index] += Scale * Damped[Index];
            Division.Evaluate(Trial, TrialResiduals, nullptr);
            if (SquaredNorm(TrialResiduals, IndependentRows) < Merit) { Raw = Trial; Accepted = true; Damping = std::max(1.0e-9, Damping * 0.1); }
        }
        if (!Accepted) break;                                                                // Stalled: conflicting rows remain
    }
    ScaledEquationDivision::Unpack(Sketch, Raw);

    // ③ Diagnosis at the final configuration.
    Division.Evaluate(Raw, Residuals, &Jacobian);
    Outcome.Converged = MaxMagnitude(Residuals) <= 10.0 * Settings.Tolerance;
    for (size_t Index = 0; Index < Residuals.size(); ++Index) Outcome.MaxResidual = std::max(Outcome.MaxResidual, std::fabs(Residuals[Index]) * Scale);
    // Driven dimensions take the solved measurement.
    for (int Index = 0; Index < UserConstraints; ++Index)
    {
        if (!Constraints[Index].DrivenCondition || !ReferenceCondition(Sketch, Constraints[Index])) continue;
        const double Measured = MeasureSketchDimension(Sketch, Constraints[Index]);
        if (!std::isnan(Measured)) Sketch.Constraints[Index].Value = Measured;
    }
    Outcome.Constraints.assign(UserConstraints, {});
    std::vector<int> Severity(Constraints.size(), 0);                                        // 0 satisfied · 1 redundant · 2 unmet · 3 conflicting
    std::vector<SketchConstraintStatus> RowStatus(Division.Equations.size(), SketchConstraintStatus::Satisfied);
    for (size_t Index = 0; Index < Division.Equations.size(); ++Index)
    {
        const SketchEquation& Equation = Division.Equations[Index];
        if (Equation.Constraint < UserConstraints)
        {
            const double RawResidual = Residuals[Index] * Division.RowUnit(Equation);
            SketchConstraintReport& Report = Outcome.Constraints[Equation.Constraint];
            Report.Residual = std::max(Report.Residual, std::fabs(RawResidual));
        }
    }
    const PivotedDecomposition Decomposition = DecomposeColumnPivoted(Transpose(Jacobian), Settings.RankTolerance);
    Outcome.Rank             = Decomposition.Rank;
    Outcome.DegreesOfFreedom = Division.Unknowns - Division.FrozenCount - Decomposition.Rank;
    const int Rank = Decomposition.Rank;
    std::vector<std::vector<int>> PartnerSets(Constraints.size());
    for (int Position = 0; Position < static_cast<int>(Decomposition.Pivots.size()); ++Position)
    {
        const int Row = Decomposition.Pivots[Position];
        const int Owner = Division.Equations[Row].Constraint;
        if (Position < Rank)
        {
            if (std::fabs(Residuals[Row]) > 10.0 * Settings.Tolerance) { RowStatus[Row] = SketchConstraintStatus::Unmet; Severity[Owner] = std::max(Severity[Owner], 2); }
            continue;
        }
        // Dependent row: express it in the independent rows (R₁₁·c = R[0:r, p]) and compare residuals.
        std::vector<double> Coefficients(Rank, 0.0);
        for (int Index = Rank - 1; Index >= 0; --Index)
        {
            double Sum = Decomposition.R.At(Index, Position);
            for (int Inner = Index + 1; Inner < Rank; ++Inner) Sum -= Decomposition.R.At(Index, Inner) * Coefficients[Inner];
            Coefficients[Index] = Sum / Decomposition.R.At(Index, Index);
        }
        double Expected = 0.0, Largest = 0.0;
        for (int Index = 0; Index < Rank; ++Index)
        {
            Expected += Coefficients[Index] * Residuals[Decomposition.Pivots[Index]];
            Largest = std::max(Largest, std::fabs(Coefficients[Index]));
        }
        const bool Conflicting = std::fabs(Residuals[Row] - Expected) > 1.0e-9;
        RowStatus[Row] = Conflicting ? SketchConstraintStatus::Conflicting : SketchConstraintStatus::Redundant;
        Severity[Owner] = std::max(Severity[Owner], Conflicting ? 3 : 1);
        for (int Index = 0; Index < Rank; ++Index)
        {
            if (std::fabs(Coefficients[Index]) <= 1.0e-9 * std::max(1.0, Largest)) continue;
            const int Partner = Division.Equations[Decomposition.Pivots[Index]].Constraint;
            if (Partner != Owner) PartnerSets[Owner].push_back(Partner);
        }
        if (Conflicting)
        {
            std::vector<int> Group = { Owner };
            Group.insert(Group.end(), PartnerSets[Owner].begin(), PartnerSets[Owner].end());
            std::vector<int> UserGroup;
            for (int Member : Group) if (Member < UserConstraints) UserGroup.push_back(Member);
            std::sort(UserGroup.begin(), UserGroup.end());
            UserGroup.erase(std::unique(UserGroup.begin(), UserGroup.end()), UserGroup.end());
            if (!UserGroup.empty() && std::find(Outcome.ConflictGroups.begin(), Outcome.ConflictGroups.end(), UserGroup) == Outcome.ConflictGroups.end())
            {
                Outcome.ConflictGroups.push_back(UserGroup);
            }
        }
    }
    for (int Index = 0; Index < UserConstraints; ++Index)
    {
        SketchConstraintReport& Report = Outcome.Constraints[Index];
        Report.Status = Severity[Index] == 3 ? SketchConstraintStatus::Conflicting : Severity[Index] == 2 ? SketchConstraintStatus::Unmet
                      : Severity[Index] == 1 ? SketchConstraintStatus::Redundant : SketchConstraintStatus::Satisfied;
        if (Excluded[Index]) Report.Status = Constraints[Index].DrivenCondition && ReferenceCondition(Sketch, Constraints[Index]) ? SketchConstraintStatus::Driven
                                                                                                                                    : SketchConstraintStatus::Unsupported;
        for (int Partner : PartnerSets[Index]) if (Partner < UserConstraints) Report.Partners.push_back(Partner);
        std::sort(Report.Partners.begin(), Report.Partners.end());
        Report.Partners.erase(std::unique(Report.Partners.begin(), Report.Partners.end()), Report.Partners.end());
        if (Report.Status == SketchConstraintStatus::Redundant) Outcome.Redundant.push_back(Index);
    }

    // ④ Null space of J = Q[:, Rank:] (Jᵀ = Q·R·Pᵀ): an unknown with no component there cannot move.
    std::vector<bool> UnknownFixed(Division.Unknowns, true);
    for (int Unknown = 0; Unknown < Division.Unknowns; ++Unknown)
    {
        double Sum = 0.0;
        for (int Column = Rank; Column < Division.Unknowns; ++Column) Sum += Decomposition.Q.At(Unknown, Column) * Decomposition.Q.At(Unknown, Column);
        UnknownFixed[Unknown] = Division.Frozen[Unknown] || std::sqrt(Sum) <= 1.0e-7;
    }
    Outcome.PointFixed.resize(Sketch.Points.size());
    for (size_t Point = 0; Point < Sketch.Points.size(); ++Point) Outcome.PointFixed[Point] = UnknownFixed[2 * Point] && UnknownFixed[2 * Point + 1];
    Outcome.CurveFixed.resize(Sketch.Curves.size());
    int RadiusCursor = 2 * static_cast<int>(Sketch.Points.size());
    for (size_t Curve = 0; Curve < Sketch.Curves.size(); ++Curve)
    {
        const SketchCurve& Shape = Sketch.Curves[Curve];
        bool Fixed = true;
        for (int Point : { Shape.Start, Shape.End, Shape.Centre }) if (Point >= 0) Fixed = Fixed && Outcome.PointFixed[Point];
        for (int Point : Shape.Points) if (Point >= 0) Fixed = Fixed && Outcome.PointFixed[Point];
        if (Shape.Kind == SketchCurveKind::Circle) Fixed = Fixed && UnknownFixed[RadiusCursor++];
        Outcome.CurveFixed[Curve] = Fixed;
    }

    // ⑤ Structural rank: the same equations at a generic (deterministically perturbed) configuration.
    std::vector<double> Perturbed = Raw;
    uint64_t            Seed      = 0x9E3779B97F4A7C15ull;
    for (size_t Unknown = 0; Unknown < Perturbed.size(); ++Unknown)
    {
        Seed = Seed * 6364136223846793005ull + 1442695040888963407ull;
        if (!Division.Frozen[Unknown]) Perturbed[Unknown] += Scale * 0.01 * (static_cast<double>(Seed >> 11) / 9007199254740992.0 - 0.5);
    }
    DenseMatrix GenericJacobian;
    std::vector<double> GenericResiduals;
    Division.Evaluate(Perturbed, GenericResiduals, &GenericJacobian);
    Outcome.StructuralRank = DecomposeColumnPivoted(Transpose(GenericJacobian), Settings.RankTolerance).Rank;

    // ⑥ Conditioning from a largest-first pivoted QR (min |R_kk| / |R_00| over the kept pivots), here and at the generic
    //    configuration. A double root (gradients parallel only at this configuration) collapses it to ~√ε at the
    //    solution while the generic value stays healthy; a long but well-posed chain is equally modest at both, so the
    //    ratio, not the absolute value, is the singularity test.
    Outcome.Conditioning        = MeasureConditioning(Jacobian, Settings.RankTolerance);
    Outcome.GenericConditioning = MeasureConditioning(GenericJacobian, Settings.RankTolerance);
    Outcome.SingularCondition   = Outcome.StructuralRank > Outcome.Rank || Outcome.Conditioning < Settings.SingularTolerance * Outcome.GenericConditioning;
    Outcome.Milliseconds      = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Started).count();
    return Outcome;
}

// 📝 Traced entry (Diagnostics/OperationTrace.h): the request on entry, the outcome before the duration line.

SketchSolveOutcome SolveSketch(SketchModel& Sketch, const SketchSolveSettings& Settings)
{
    FRONTIER_TRACE_SCOPE("Sketch", "SolveSketch · " + std::to_string(Sketch.Curves.size()) + " curve(s) · " + std::to_string(Sketch.Constraints.size()) + " constraint(s)");
    SketchSolveOutcome Outcome = SolveSketchUntraced(Sketch, Settings);
    FRONTIER_TRACE("Sketch", std::string(Outcome.Converged ? "converged" : "NOT converged") + " · " + std::to_string(Outcome.Iterations) + " iteration(s) · DoF " +
                             std::to_string(Outcome.DegreesOfFreedom));
    return Outcome;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    BUILDERS AND LABELS
//------------------------------------------------------------------------------------------------------------------------

int AppendSketchPoint(SketchModel& Sketch, Vector2 Position)
{
    Sketch.Points.push_back({ Position });
    return static_cast<int>(Sketch.Points.size()) - 1;
}

int AppendSketchLine(SketchModel& Sketch, int Start, int End, int Identifier, bool Construction)
{
    SketchCurve Curve;
    Curve.Kind = SketchCurveKind::Line; Curve.Identifier = Identifier; Curve.Start = Start; Curve.End = End; Curve.Construction = Construction;
    Sketch.Curves.push_back(Curve);
    return static_cast<int>(Sketch.Curves.size()) - 1;
}

int AppendSketchCircle(SketchModel& Sketch, int Centre, double Radius, int Identifier, bool Construction)
{
    SketchCurve Curve;
    Curve.Kind = SketchCurveKind::Circle; Curve.Identifier = Identifier; Curve.Centre = Centre; Curve.Radius = Radius; Curve.Construction = Construction;
    Sketch.Curves.push_back(Curve);
    return static_cast<int>(Sketch.Curves.size()) - 1;
}

int AppendSketchArc(SketchModel& Sketch, int Centre, int Start, int End, int Identifier, bool Construction)
{
    SketchCurve Curve;
    Curve.Kind = SketchCurveKind::Arc; Curve.Identifier = Identifier; Curve.Centre = Centre; Curve.Start = Start; Curve.End = End; Curve.Construction = Construction;
    Sketch.Curves.push_back(Curve);
    return static_cast<int>(Sketch.Curves.size()) - 1;
}

int AppendSketchSpline(SketchModel& Sketch, std::vector<int> Points, SketchSplineForm Form, int Identifier, bool Construction)
{
    SketchCurve Curve;
    Curve.Kind = SketchCurveKind::Spline; Curve.Identifier = Identifier; Curve.Form = Form; Curve.Construction = Construction;
    Curve.Start  = Points.empty() ? -1 : Points.front();
    Curve.End    = Points.empty() ? -1 : Points.back();
    Curve.Points = std::move(Points);
    Sketch.Curves.push_back(Curve);
    return static_cast<int>(Sketch.Curves.size()) - 1;
}

int AppendSketchConstraint(SketchModel& Sketch, SketchConstraintKind Kind, int First, int Second, int Third, double Value, const std::string& Label, double SecondValue)
{
    SketchConstraint Constraint;
    Constraint.Kind = Kind; Constraint.First = First; Constraint.Second = Second; Constraint.Third = Third;
    Constraint.Value = Value; Constraint.SecondValue = SecondValue; Constraint.Label = Label;
    Sketch.Constraints.push_back(Constraint);
    return static_cast<int>(Sketch.Constraints.size()) - 1;
}

double MeasureSketchDimension(const SketchModel& Sketch, const SketchConstraint& Constraint) noexcept
{
    const double Missing    = std::numeric_limits<double>::quiet_NaN();
    const int    PointCount = static_cast<int>(Sketch.Points.size()), CurveCount = static_cast<int>(Sketch.Curves.size());
    auto PointSlot = [&](int Index) { return Index >= 0 && Index < PointCount; };
    auto CurveSlot = [&](int Index, SketchCurveKind Kind) { return Index >= 0 && Index < CurveCount && Sketch.Curves[Index].Kind == Kind; };
    auto Position  = [&](int Index) { return Sketch.Points[Index].Position; };
    auto Direction = [&](int Line) { return Position(Sketch.Curves[Line].End) - Position(Sketch.Curves[Line].Start); };
    switch (Constraint.Kind)
    {
    case SketchConstraintKind::Length:
        return CurveSlot(Constraint.First, SketchCurveKind::Line) ? std::sqrt(Dot(Direction(Constraint.First), Direction(Constraint.First))) : Missing;
    case SketchConstraintKind::Distance:
        return PointSlot(Constraint.First) && PointSlot(Constraint.Second) ? Distance(Position(Constraint.First), Position(Constraint.Second)) : Missing;
    case SketchConstraintKind::HorizontalDistance:
        return PointSlot(Constraint.First) && PointSlot(Constraint.Second) ? Position(Constraint.Second).X - Position(Constraint.First).X : Missing;
    case SketchConstraintKind::VerticalDistance:
        return PointSlot(Constraint.First) && PointSlot(Constraint.Second) ? Position(Constraint.Second).Y - Position(Constraint.First).Y : Missing;
    case SketchConstraintKind::Radius:
        if (CurveSlot(Constraint.First, SketchCurveKind::Circle) || CurveSlot(Constraint.First, SketchCurveKind::Arc)) return MeasureSketchRadius(Sketch, Sketch.Curves[Constraint.First]);
        return Missing;
    case SketchConstraintKind::Angle:
    {
        if (!CurveSlot(Constraint.First, SketchCurveKind::Line) || !CurveSlot(Constraint.Second, SketchCurveKind::Line)) return Missing;
        const Vector2 First = Direction(Constraint.First), Second = Direction(Constraint.Second);
        return std::atan2(First.X * Second.Y - First.Y * Second.X, Dot(First, Second));
    }
    default: return Missing;
    }
}

int NearestSketchSplineEnd(const SketchModel& Sketch, const SketchCurve& Curve, Vector2 Position) noexcept
{
    if (Curve.Kind != SketchCurveKind::Spline || Curve.Points.size() < 2 || ClosedSpline(Curve)) return -1;
    const int Front = Curve.Points.front(), Back = Curve.Points.back();
    const int Count = static_cast<int>(Sketch.Points.size());
    if (Front < 0 || Back < 0 || Front >= Count || Back >= Count) return -1;
    return Distance(Sketch.Points[Front].Position, Position) <= Distance(Sketch.Points[Back].Position, Position) ? Front : Back;
}

double MeasureSketchRadius(const SketchModel& Sketch, const SketchCurve& Curve) noexcept
{
    if (Curve.Kind == SketchCurveKind::Arc) return Distance(Sketch.Points[Curve.Start].Position, Sketch.Points[Curve.Centre].Position);
    return Curve.Radius;
}

std::string SketchCurveName(const SketchCurve& Curve)
{
    const char* Prefix = Curve.Kind == SketchCurveKind::Line ? "Line" : Curve.Kind == SketchCurveKind::Arc ? "Arc" : Curve.Kind == SketchCurveKind::Spline ? "Spline" : "Circle";
    return Prefix + std::to_string(Curve.Identifier);
}

const char* SketchConstraintLabel(SketchConstraintKind Kind) noexcept
{
    switch (Kind)
    {
    case SketchConstraintKind::Coincident:         return "Coincident";
    case SketchConstraintKind::Fixed:              return "Fixed";
    case SketchConstraintKind::Horizontal:         return "Horizontal";
    case SketchConstraintKind::Vertical:           return "Vertical";
    case SketchConstraintKind::Distance:           return "Distance";
    case SketchConstraintKind::HorizontalDistance: return "HorizontalDistance";
    case SketchConstraintKind::VerticalDistance:   return "VerticalDistance";
    case SketchConstraintKind::Length:             return "Length";
    case SketchConstraintKind::Parallel:           return "Parallel";
    case SketchConstraintKind::Perpendicular:      return "Perpendicular";
    case SketchConstraintKind::Angle:              return "Angle";
    case SketchConstraintKind::EqualLength:        return "EqualLength";
    case SketchConstraintKind::Radius:             return "Radius";
    case SketchConstraintKind::EqualRadius:        return "EqualRadius";
    case SketchConstraintKind::PointOnLine:        return "PointOnLine";
    case SketchConstraintKind::PointOnCircle:      return "PointOnCircle";
    case SketchConstraintKind::Tangent:            return "Tangent";
    case SketchConstraintKind::Midpoint:           return "Midpoint";
    case SketchConstraintKind::Symmetric:          return "Symmetric";
    case SketchConstraintKind::Concentric:         return "Concentric";
    case SketchConstraintKind::ArcRule:            return "ArcRule";
    case SketchConstraintKind::SplineTangent:      return "SplineTangent";
    case SketchConstraintKind::SplineCurvature:    return "SplineCurvature";
    case SketchConstraintKind::PointOnSpline:      return "PointOnSpline";
    case SketchConstraintKind::SplineDirection:    return "SplineDirection";
    }
    return "?";
}

const char* SketchConstraintStatusLabel(SketchConstraintStatus Status) noexcept
{
    switch (Status)
    {
    case SketchConstraintStatus::Satisfied:   return "Satisfied";
    case SketchConstraintStatus::Redundant:   return "Redundant";
    case SketchConstraintStatus::Conflicting: return "Conflicting";
    case SketchConstraintStatus::Unmet:       return "Unmet";
    case SketchConstraintStatus::Unsupported: return "Unsupported";
    case SketchConstraintStatus::Driven:      return "Driven";
    }
    return "?";
}

const char* SketchSplineFormLabel(SketchSplineForm Form) noexcept
{
    switch (Form)
    {
    case SketchSplineForm::Hermite:     return "Hermite";
    case SketchSplineForm::Bezier:      return "Bezier";
    case SketchSplineForm::Nurbs:       return "NURBS";
    case SketchSplineForm::CubicSpline: return "Cubic spline";
    }
    return "Spline";
}

} // namespace Frontier::GeometrySpace::Parametric
