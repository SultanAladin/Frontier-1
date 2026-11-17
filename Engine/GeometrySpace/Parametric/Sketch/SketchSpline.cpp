/*====================================================================================================================================
                                                       SKETCHSPLINE.CPP
====================================================================================================================================*/
// 🧩 Freeform sketch curves (Hermite, Bezier, NURBS, cubic spline) resolved to exact polynomial clamped B-splines

#include "Engine/GeometrySpace/Parametric/Sketch/SketchSpline.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr int BezierPointLimit = 16;                                                     // [-] - Degree ≤ 15 (NurbsDegreeLimit)

    // Hermite data (points Q, tangents T per unit span) → cubic Bézier chain with triple interior knots.
    [[nodiscard]] SketchSplineCurve ChainHermite(const std::vector<Vector2>& Points, const std::vector<Vector2>& Tangents)
    {
        SketchSplineCurve Curve;
        Curve.Degree = 3;
        const int Spans = static_cast<int>(Points.size()) - 1;
        for (int Span = 0; Span < Spans; ++Span)
        {
            if (Span == 0) Curve.Poles.push_back(Points[0]);
            Curve.Poles.push_back(Points[Span] + Tangents[Span] * (1.0 / 3.0));
            Curve.Poles.push_back(Points[Span + 1] - Tangents[Span + 1] * (1.0 / 3.0));
            Curve.Poles.push_back(Points[Span + 1]);
        }
        for (int Repeat = 0; Repeat < 4; ++Repeat) Curve.Knots.push_back(0.0);
        for (int Span = 1; Span < Spans; ++Span) for (int Repeat = 0; Repeat < 3; ++Repeat) Curve.Knots.push_back(static_cast<double>(Span));
        for (int Repeat = 0; Repeat < 4; ++Repeat) Curve.Knots.push_back(static_cast<double>(Spans));
        return Curve;
    }

    // Catmull–Rom tangents on unit spans.
    [[nodiscard]] std::vector<Vector2> HermiteTangents(const std::vector<Vector2>& Points, bool Closed)
    {
        const int Last = static_cast<int>(Points.size()) - 1;
        std::vector<Vector2> Tangents(Points.size());
        for (int Index = 1; Index < Last; ++Index) Tangents[Index] = (Points[Index + 1] - Points[Index - 1]) * 0.5;
        if (Closed) Tangents[0] = Tangents[Last] = (Points[1] - Points[Last - 1]) * 0.5;
        else
        {
            Tangents[0]    = Points[1] - Points[0];
            Tangents[Last] = Points[Last] - Points[Last - 1];
        }
        return Tangents;
    }

    // C² cubic interpolation tangents: tridiagonal (natural ends) or cyclic (closed, Sherman–Morrison) system.
    [[nodiscard]] std::vector<Vector2> SplineTangents(const std::vector<Vector2>& Points, bool Closed)
    {
        const int Last = static_cast<int>(Points.size()) - 1;
        if (!Closed)
        {
            const int Size = Last + 1;
            std::vector<double>  Lower(Size, 1.0), Diagonal(Size, 4.0), Upper(Size, 1.0);
            std::vector<Vector2> Right(Size);
            Diagonal[0] = Diagonal[Last] = 2.0;
            Right[0]    = (Points[1] - Points[0]) * 3.0;
            Right[Last] = (Points[Last] - Points[Last - 1]) * 3.0;
            for (int Index = 1; Index < Last; ++Index) Right[Index] = (Points[Index + 1] - Points[Index - 1]) * 3.0;
            for (int Index = 1; Index < Size; ++Index)                                       // Thomas elimination
            {
                const double Factor = Lower[Index] / Diagonal[Index - 1];
                Diagonal[Index] -= Factor * Upper[Index - 1];
                Right[Index]     = Right[Index] - Right[Index - 1] * Factor;
            }
            std::vector<Vector2> Tangents(Size);
            Tangents[Last] = Right[Last] * (1.0 / Diagonal[Last]);
            for (int Index = Last - 1; Index >= 0; --Index) Tangents[Index] = (Right[Index] - Tangents[Index + 1] * Upper[Index]) * (1.0 / Diagonal[Index]);
            return Tangents;
        }
        // Closed: unknowns T₀ … T_{m−1} (m = Last distinct points), Tᵢ₋₁ + 4Tᵢ + Tᵢ₊₁ = 3(Qᵢ₊₁ − Qᵢ₋₁) cyclically.
        const int Count = Last;
        auto Wrap = [&](int Index) { return Points[((Index % Count) + Count) % Count]; };
        std::vector<Vector2> Right(Count);
        for (int Index = 0; Index < Count; ++Index) Right[Index] = (Wrap(Index + 1) - Wrap(Index - 1)) * 3.0;
        // Sherman–Morrison: A = B + u·vᵀ with u = (γ, 0 … 0, 1), v = (1, 0 … 0, 1/γ), γ = −4.
        const double Gamma = -4.0;
        auto SolveBand = [&](std::vector<double> Diagonal, std::vector<Vector2> Values)
        {
            std::vector<double> Upper(Count, 1.0);
            for (int Index = 1; Index < Count; ++Index)
            {
                const double Factor = 1.0 / Diagonal[Index - 1];
                Diagonal[Index] -= Factor * Upper[Index - 1];
                Values[Index]    = Values[Index] - Values[Index - 1] * Factor;
            }
            std::vector<Vector2> Solution(Count);
            Solution[Count - 1] = Values[Count - 1] * (1.0 / Diagonal[Count - 1]);
            for (int Index = Count - 2; Index >= 0; --Index) Solution[Index] = (Values[Index] - Solution[Index + 1] * Upper[Index]) * (1.0 / Diagonal[Index]);
            return Solution;
        };
        std::vector<double> Diagonal(Count, 4.0);
        Diagonal[0]         = 4.0 - Gamma;
        Diagonal[Count - 1] = 4.0 - 1.0 / Gamma;
        const std::vector<Vector2> First = SolveBand(Diagonal, Right);
        std::vector<Vector2> Unit(Count, Vector2{ 0.0, 0.0 });
        Unit[0]         = { Gamma, Gamma };
        Unit[Count - 1] = { 1.0, 1.0 };
        const std::vector<Vector2> Second = SolveBand(Diagonal, Unit);                         // Both components carry u
        const double VX = First[0].X + First[Count - 1].X / Gamma, VY = First[0].Y + First[Count - 1].Y / Gamma;
        const double WX = Second[0].X + Second[Count - 1].X / Gamma, WY = Second[0].Y + Second[Count - 1].Y / Gamma;
        std::vector<Vector2> Tangents(Count + 1);
        for (int Index = 0; Index < Count; ++Index)
        {
            Tangents[Index] = { First[Index].X - Second[Index].X * VX / (1.0 + WX), First[Index].Y - Second[Index].Y * VY / (1.0 + WY) };
        }
        Tangents[Count] = Tangents[0];
        return Tangents;
    }

    // Hermite data on explicit parameters τᵢ (span i on [τᵢ, τᵢ₊₁], width hᵢ): Bézier handles Qᵢ ± hᵢ·Tᵢ/3.
    [[nodiscard]] SketchSplineCurve ChainHermiteSpans(const std::vector<Vector2>& Points, const std::vector<Vector2>& Tangents, const std::vector<double>& Parameters)
    {
        SketchSplineCurve Curve;
        Curve.Degree = 3;
        const int Spans = static_cast<int>(Points.size()) - 1;
        for (int Span = 0; Span < Spans; ++Span)
        {
            const double Width = Parameters[Span + 1] - Parameters[Span];                  // [-] - hᵢ
            if (Span == 0) Curve.Poles.push_back(Points[0]);
            Curve.Poles.push_back(Points[Span] + Tangents[Span] * (Width / 3.0));
            Curve.Poles.push_back(Points[Span + 1] - Tangents[Span + 1] * (Width / 3.0));
            Curve.Poles.push_back(Points[Span + 1]);
        }
        for (int Repeat = 0; Repeat < 4; ++Repeat) Curve.Knots.push_back(Parameters.front());
        for (int Span = 1; Span < Spans; ++Span) for (int Repeat = 0; Repeat < 3; ++Repeat) Curve.Knots.push_back(Parameters[Span]);
        for (int Repeat = 0; Repeat < 4; ++Repeat) Curve.Knots.push_back(Parameters.back());
        return Curve;
    }

    // Catmull–Rom tangents on uneven spans: Tᵢ = (Qᵢ₊₁ − Qᵢ₋₁)/(τᵢ₊₁ − τᵢ₋₁), ends one-sided (closed: wrapped).
    [[nodiscard]] std::vector<Vector2> HermiteSpanTangents(const std::vector<Vector2>& Points, const std::vector<double>& Parameters, bool Closed)
    {
        const int Last = static_cast<int>(Points.size()) - 1;
        std::vector<Vector2> Tangents(Points.size());
        for (int Index = 1; Index < Last; ++Index) Tangents[Index] = (Points[Index + 1] - Points[Index - 1]) * (1.0 / (Parameters[Index + 1] - Parameters[Index - 1]));
        if (Closed)
        {
            const double Across = (Parameters[1] - Parameters[0]) + (Parameters[Last] - Parameters[Last - 1]);
            Tangents[0] = Tangents[Last] = (Points[1] - Points[Last - 1]) * (1.0 / Across);
        }
        else
        {
            Tangents[0]    = (Points[1] - Points[0]) * (1.0 / (Parameters[1] - Parameters[0]));
            Tangents[Last] = (Points[Last] - Points[Last - 1]) * (1.0 / (Parameters[Last] - Parameters[Last - 1]));
        }
        return Tangents;
    }

    // Tridiagonal solve (Thomas): Lower[i]·xᵢ₋₁ + Diagonal[i]·xᵢ + Upper[i]·xᵢ₊₁ = Right[i].
    template <typename Value>
    [[nodiscard]] std::vector<Value> SolveTridiagonal(const std::vector<double>& Lower, std::vector<double> Diagonal, const std::vector<double>& Upper, std::vector<Value> Right)
    {
        const int Size = static_cast<int>(Diagonal.size());
        for (int Index = 1; Index < Size; ++Index)
        {
            const double Factor = Lower[Index] / Diagonal[Index - 1];
            Diagonal[Index] -= Factor * Upper[Index - 1];
            Right[Index]     = Right[Index] - Right[Index - 1] * Factor;
        }
        std::vector<Value> Solution(Size);
        Solution[Size - 1] = Right[Size - 1] * (1.0 / Diagonal[Size - 1]);
        for (int Index = Size - 2; Index >= 0; --Index) Solution[Index] = (Right[Index] - Solution[Index + 1] * Upper[Index]) * (1.0 / Diagonal[Index]);
        return Solution;
    }

    // C² cubic interpolation tangents on uneven spans hᵢ = τᵢ₊₁ − τᵢ (C″ continuous at every interior point):
    //    hᵢ·Tᵢ₋₁ + 2(hᵢ₋₁ + hᵢ)·Tᵢ + hᵢ₋₁·Tᵢ₊₁ = 3[hᵢ·(Qᵢ − Qᵢ₋₁)/hᵢ₋₁ + hᵢ₋₁·(Qᵢ₊₁ − Qᵢ)/hᵢ],
    //    natural ends 2T₀ + T₁ = 3(Q₁ − Q₀)/h₀ (and mirrored), or cyclic when closed (Sherman–Morrison).
    [[nodiscard]] std::vector<Vector2> SplineSpanTangents(const std::vector<Vector2>& Points, const std::vector<double>& Parameters, bool Closed)
    {
        const int Last = static_cast<int>(Points.size()) - 1;
        if (!Closed)
        {
            const int Size = Last + 1;
            std::vector<double>  Lower(Size, 0.0), Diagonal(Size, 0.0), Upper(Size, 0.0);
            std::vector<Vector2> Right(Size);
            const double First = Parameters[1] - Parameters[0], Final = Parameters[Last] - Parameters[Last - 1];
            Diagonal[0] = 2.0; Upper[0] = 1.0; Right[0] = (Points[1] - Points[0]) * (3.0 / First);
            Lower[Last] = 1.0; Diagonal[Last] = 2.0; Right[Last] = (Points[Last] - Points[Last - 1]) * (3.0 / Final);
            for (int Index = 1; Index < Last; ++Index)
            {
                const double Before = Parameters[Index] - Parameters[Index - 1], After = Parameters[Index + 1] - Parameters[Index];
                Lower[Index]    = After;
                Diagonal[Index] = 2.0 * (Before + After);
                Upper[Index]    = Before;
                Right[Index]    = (Points[Index] - Points[Index - 1]) * (3.0 * After / Before) + (Points[Index + 1] - Points[Index]) * (3.0 * Before / After);
            }
            return SolveTridiagonal(Lower, Diagonal, Upper, Right);
        }
        // Closed: unknowns T₀ … T_{m−1} (m = Last distinct points; span m−1 runs from point m−1 back to point 0).
        const int Count = Last;
        std::vector<double> Width(Count);
        for (int Index = 0; Index < Count; ++Index) Width[Index] = Parameters[Index + 1] - Parameters[Index];
        auto Wrap = [&](int Index) { return ((Index % Count) + Count) % Count; };
        std::vector<double>  Lower(Count), Diagonal(Count), Upper(Count);
        std::vector<Vector2> Right(Count);
        for (int Index = 0; Index < Count; ++Index)
        {
            const double Before = Width[Wrap(Index - 1)], After = Width[Index];
            Lower[Index]    = After;
            Diagonal[Index] = 2.0 * (Before + After);
            Upper[Index]    = Before;
            Right[Index]    = (Points[Index] - Points[Wrap(Index - 1)]) * (3.0 * After / Before) + (Points[Wrap(Index + 1)] - Points[Index]) * (3.0 * Before / After);
        }
        // Corners: row 0 reaches T_{m−1} (Lower[0]), row m−1 reaches T₀ (Upper[m−1]). A = B + u·vᵀ, u = (γ, 0 … 0, α),
        // v = (1, 0 … 0, β/γ) with β = Lower[0], α = Upper[m−1], γ = −Diagonal[0].
        const double Beta = Lower[0], Alpha = Upper[Count - 1], Gamma = -Diagonal[0];
        std::vector<double> Band = Diagonal, Below = Lower, Above = Upper;
        Band[0]         -= Gamma;
        Band[Count - 1] -= Alpha * Beta / Gamma;
        Below[0] = 0.0; Above[Count - 1] = 0.0;
        const std::vector<Vector2> Base = SolveTridiagonal(Below, Band, Above, Right);
        std::vector<double> Unit(Count, 0.0);
        Unit[0] = Gamma; Unit[Count - 1] = Alpha;
        const std::vector<double> Fix = SolveTridiagonal(Below, Band, Above, Unit);
        const double  Denominator = 1.0 + Fix[0] + Beta * Fix[Count - 1] / Gamma;
        const Vector2 Numerator   = Base[0] + Base[Count - 1] * (Beta / Gamma);
        std::vector<Vector2> Tangents(Count + 1);
        for (int Index = 0; Index < Count; ++Index) Tangents[Index] = Base[Index] - Numerator * (Fix[Index] / Denominator);
        Tangents[Count] = Tangents[0];
        return Tangents;
    }

    // Explicit knots of Form for Count defining points: "" when usable, else why not.
    [[nodiscard]] std::string ScreenKnots(SketchSplineForm Form, int Count, const std::vector<double>& Knots)
    {
        if (Knots.empty()) return {};
        for (double Knot : Knots) if (!std::isfinite(Knot)) return "a knot is not finite";
        if (Form == SketchSplineForm::Hermite || Form == SketchSplineForm::CubicSpline)
        {
            if (static_cast<int>(Knots.size()) != Count) return std::to_string(Knots.size()) + " parameters for " + std::to_string(Count) + " points";
            for (size_t Index = 0; Index + 1 < Knots.size(); ++Index) if (!(Knots[Index + 1] > Knots[Index])) return "parameters must increase";
            return {};
        }
        if (Form != SketchSplineForm::Nurbs) return "a Bezier takes no knots";
        const int Degree = static_cast<int>(Knots.size()) - Count - 1;
        if (Degree < 1 || Degree > 15 || Degree > Count - 1) return std::to_string(Knots.size()) + " knots do not fit " + std::to_string(Count) + " control points";
        for (size_t Index = 0; Index + 1 < Knots.size(); ++Index) if (Knots[Index + 1] < Knots[Index]) return "knots must not decrease";
        for (int Index = 1; Index <= Degree; ++Index)
            if (Knots[Index] != Knots[0] || Knots[Knots.size() - 1 - Index] != Knots.back()) return "knots must be clamped";
        if (!(Knots.back() > Knots.front())) return "the knot span is empty";
        for (int Index = Degree + 1, Run = 1; Index < Count; ++Index)
        {
            Run = Knots[Index] == Knots[Index - 1] ? Run + 1 : 1;
            if (Run > Degree) return "an interior knot repeats more than the degree";
        }
        return {};
    }

    // Cox–de Boor basis values and derivatives up to order 2 at Tau in span Span (Piegl & Tiller A2.3).
    void EvaluateBasis(const SketchSplineCurve& Curve, int Span, double Tau, double (&Values)[3][16])
    {
        const int Degree = Curve.Degree;
        double Table[16][16] = {};
        double Left[16] = {}, Right[16] = {};
        Table[0][0] = 1.0;
        for (int Order = 1; Order <= Degree; ++Order)
        {
            Left[Order]  = Tau - Curve.Knots[Span + 1 - Order];
            Right[Order] = Curve.Knots[Span + Order] - Tau;
            double Saved = 0.0;
            for (int Index = 0; Index < Order; ++Index)
            {
                Table[Order][Index] = Right[Index + 1] + Left[Order - Index];
                const double Temporary = Table[Index][Order - 1] / Table[Order][Index];
                Table[Index][Order] = Saved + Right[Index + 1] * Temporary;
                Saved = Left[Order - Index] * Temporary;
            }
            Table[Order][Order] = Saved;
        }
        for (int Index = 0; Index <= Degree; ++Index) Values[0][Index] = Table[Index][Degree];
        double Work[2][16] = {};
        for (int Index = 0; Index <= Degree; ++Index)
        {
            int First = 0, Second = 1;
            Work[0][0] = 1.0;
            for (int Order = 1; Order <= 2; ++Order)
            {
                double Value = 0.0;
                const int Shift = Index - Order, Power = Degree - Order;
                if (Index >= Order)
                {
                    Work[Second][0] = Work[First][0] / Table[Power + 1][Shift];
                    Value = Work[Second][0] * Table[Shift][Power];
                }
                const int Low  = Shift >= -1 ? 1 : -Shift;
                const int High = Index - 1 <= Power ? Order - 1 : Degree - Index;
                for (int Inner = Low; Inner <= High; ++Inner)
                {
                    Work[Second][Inner] = (Work[First][Inner] - Work[First][Inner - 1]) / Table[Power + 1][Shift + Inner];
                    Value += Work[Second][Inner] * Table[Shift + Inner][Power];
                }
                if (Index <= Power)
                {
                    Work[Second][Order] = -Work[First][Order - 1] / Table[Power + 1][Index];
                    Value += Work[Second][Order] * Table[Index][Power];
                }
                Values[Order][Index] = Order <= Degree ? Value : 0.0;
                std::swap(First, Second);
            }
        }
        for (int Index = 0; Index <= Degree; ++Index) { Values[1][Index] *= Degree; Values[2][Index] *= Degree * (Degree - 1); }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SketchSplineOutcome ResolveSketchSpline(SketchSplineForm Form, const std::vector<Vector2>& Points, double Scale, const std::vector<double>& Knots)
{
    SketchSplineOutcome Outcome;
    const char*  Label = SketchSplineFormLabel(Form);
    const double Join  = 1.0e-9 * std::max(Scale, 1.0e-300);                                 // [len] - Coincidence distance
    if (Points.size() < 2) { Outcome.Refusal = std::string(Label) + " needs at least 2 points"; return Outcome; }
    for (size_t Index = 0; Index + 1 < Points.size(); ++Index)
    {
        if (Distance(Points[Index], Points[Index + 1]) > Join) continue;
        Outcome.Refusal = std::string(Label) + " points " + std::to_string(Index + 1) + " and " + std::to_string(Index + 2) + " coincide";
        return Outcome;
    }
    std::vector<Vector2> Defining = Points;
    Outcome.Closed = Points.size() > 2 && Distance(Points.front(), Points.back()) <= Join;
    if (Outcome.Closed)
    {
        if (Points.size() < 4) { Outcome.Refusal = std::string("closed ") + Label + " needs at least 3 distinct points"; return Outcome; }
        Defining.back() = Defining.front();                                                  // Exactly closed
    }
    const int Last = static_cast<int>(Defining.size()) - 1;
    if (const std::string Screened = ScreenKnots(Form, Last + 1, Knots); !Screened.empty())
    {
        Outcome.Refusal = std::string(Label) + ": " + Screened;
        return Outcome;
    }
    if (!Knots.empty())
    {
        // Explicit parameters (Insert point): uneven spans, or a NURBS on its own knot vector.
        if (Form == SketchSplineForm::Hermite) Outcome.Curve = ChainHermiteSpans(Defining, HermiteSpanTangents(Defining, Knots, Outcome.Closed), Knots);
        else if (Form == SketchSplineForm::CubicSpline)
            Outcome.Curve = ChainHermiteSpans(Defining, Last == 1 ? HermiteSpanTangents(Defining, Knots, false) : SplineSpanTangents(Defining, Knots, Outcome.Closed), Knots);
        else
        {
            Outcome.Curve.Degree = static_cast<int>(Knots.size()) - Last - 2;
            Outcome.Curve.Poles  = Defining;
            Outcome.Curve.Knots  = Knots;
        }
        return Outcome;
    }
    switch (Form)
    {
    case SketchSplineForm::Hermite:     Outcome.Curve = ChainHermite(Defining, HermiteTangents(Defining, Outcome.Closed)); break;
    case SketchSplineForm::CubicSpline: Outcome.Curve = ChainHermite(Defining, Last == 1 ? HermiteTangents(Defining, false) : SplineTangents(Defining, Outcome.Closed)); break;
    case SketchSplineForm::Bezier:
    {
        if (static_cast<int>(Defining.size()) > BezierPointLimit)
        {
            Outcome.Refusal = "Bezier with " + std::to_string(Defining.size()) + " control points exceeds degree 15 (use NURBS)";
            return Outcome;
        }
        Outcome.Curve.Degree = Last;
        Outcome.Curve.Poles  = Defining;
        Outcome.Curve.Knots.assign(Last + 1, 0.0);
        Outcome.Curve.Knots.insert(Outcome.Curve.Knots.end(), Last + 1, 1.0);
        break;
    }
    case SketchSplineForm::Nurbs:
    {
        const int Degree = std::min(3, Last);
        Outcome.Curve.Degree = Degree;
        Outcome.Curve.Poles  = Defining;
        const int Interior = Last - Degree;                                                  // [-] - Interior knots
        Outcome.Curve.Knots.assign(Degree + 1, 0.0);
        for (int Index = 1; Index <= Interior; ++Index) Outcome.Curve.Knots.push_back(static_cast<double>(Index));
        Outcome.Curve.Knots.insert(Outcome.Curve.Knots.end(), Degree + 1, static_cast<double>(Interior + 1));
        break;
    }
    }
    if (Outcome.Curve.Poles.empty()) Outcome.Refusal = "unknown spline form " + std::to_string(static_cast<int>(Form));
    return Outcome;
}

SketchSplineWeights WeighSketchSpline(SketchSplineForm Form, int Count, bool Closed, double Tau, const std::vector<double>& Knots)
{
    SketchSplineWeights Weights;
    if (Count < 2 || (Closed && Count < 4)) { Weights.Refusal = "too few spline points"; return Weights; }
    Weights.Position.assign(Count, 0.0);
    Weights.First.assign(Count, 0.0);
    Weights.Second.assign(Count, 0.0);
    const int Last = Count - 1;
    for (int Slot = 0; Slot < Count; ++Slot)
    {
        if (Closed && Slot == Last) continue;                                                // Same point as slot 0
        // x carries the unit input; y keeps consecutive points apart (and the closing pair together).
        std::vector<Vector2> Points(Count);
        for (int Index = 0; Index < Count; ++Index)
        {
            const bool Unit = Index == Slot || (Closed && Slot == 0 && Index == Last);
            Points[Index] = { Unit ? 1.0 : 0.0, Closed && Index == Last ? 0.0 : static_cast<double>(Index) };
        }
        const SketchSplineOutcome Resolved = ResolveSketchSpline(Form, Points, static_cast<double>(Count), Knots);
        if (!Resolved.Refusal.empty()) { Weights.Refusal = Resolved.Refusal; return Weights; }
        Weights.Low  = Resolved.Curve.Knots.front();
        Weights.High = Resolved.Curve.Knots.back();
        const SketchSplineDerivatives At = EvaluateSketchSpline(Resolved.Curve, std::clamp(Tau, Weights.Low, Weights.High));
        Weights.Position[Slot] = At.Position.X;
        Weights.First[Slot]    = At.First.X;
        Weights.Second[Slot]   = At.Second.X;
    }
    return Weights;
}

SketchSplineOutcome ResolveSketchCurveSpline(const SketchModel& Sketch, const SketchCurve& Curve)
{
    std::vector<Vector2> Points;
    Vector2 Low{ 1e300, 1e300 }, High{ -1e300, -1e300 };
    for (int Index : Curve.Points)
    {
        if (Index < 0 || Index >= static_cast<int>(Sketch.Points.size()))
        {
            SketchSplineOutcome Outcome;
            Outcome.Refusal = SketchCurveName(Curve) + " references a missing point";
            return Outcome;
        }
        const Vector2 Position = Sketch.Points[Index].Position;
        Points.push_back(Position);
        Low  = { std::min(Low.X, Position.X), std::min(Low.Y, Position.Y) };
        High = { std::max(High.X, Position.X), std::max(High.Y, Position.Y) };
    }
    SketchSplineOutcome Outcome = ResolveSketchSpline(Curve.Form, Points, Points.empty() ? 1.0 : std::max(Distance(Low, High), 1.0e-300), Curve.Knots);
    if (!Outcome.Refusal.empty()) Outcome.Refusal = SketchCurveName(Curve) + ": " + Outcome.Refusal;
    if (Outcome.Refusal.empty() && Curve.Points.size() > 2 && Curve.Points.front() == Curve.Points.back()) Outcome.Closed = true;
    return Outcome;
}

SketchSplineDerivatives EvaluateSketchSpline(const SketchSplineCurve& Curve, double Tau) noexcept
{
    SketchSplineDerivatives Result;
    const int Degree = Curve.Degree, Count = static_cast<int>(Curve.Poles.size());
    if (Count == 0) return Result;
    const double Low = Curve.Knots[Degree], High = Curve.Knots[Count];
    Tau = std::clamp(Tau, Low, High);
    int Span = Degree;                                                                       // Last span with Knots[Span] ≤ τ < Knots[Span + 1]
    if (Tau >= High) Span = Count - 1;
    else while (Span + 1 < Count && Curve.Knots[Span + 1] <= Tau) ++Span;
    double Values[3][16] = {};
    EvaluateBasis(Curve, Span, Tau, Values);
    for (int Index = 0; Index <= Degree; ++Index)
    {
        const Vector2 Pole = Curve.Poles[Span - Degree + Index];
        Result.Position = Result.Position + Pole * Values[0][Index];
        Result.First    = Result.First + Pole * Values[1][Index];
        Result.Second   = Result.Second + Pole * Values[2][Index];
    }
    return Result;
}

void ReverseSketchSpline(SketchSplineCurve& Curve) noexcept
{
    std::reverse(Curve.Poles.begin(), Curve.Poles.end());
    const double Sum = Curve.Knots.front() + Curve.Knots.back();
    std::reverse(Curve.Knots.begin(), Curve.Knots.end());
    for (double& Knot : Curve.Knots) Knot = Sum - Knot;
}

std::vector<double> CollectSketchSplineBreaks(const SketchSplineCurve& Curve)
{
    std::vector<double> Breaks;
    for (double Knot : Curve.Knots) if (Breaks.empty() || Knot > Breaks.back()) Breaks.push_back(Knot);
    return Breaks;
}

std::vector<Vector2> SampleSketchCurve(const SketchModel& Sketch, const SketchCurve& Curve)
{
    std::vector<Vector2> Samples;
    const int PointCount = static_cast<int>(Sketch.Points.size());
    auto Position = [&](int Index) { return Index >= 0 && Index < PointCount ? Sketch.Points[Index].Position : Vector2{}; };
    if (Curve.Kind == SketchCurveKind::Line) return { Position(Curve.Start), Position(Curve.End) };
    if (Curve.Kind == SketchCurveKind::Spline)
    {
        const SketchSplineOutcome Resolved = ResolveSketchCurveSpline(Sketch, Curve);
        if (!Resolved.Refusal.empty())
        {
            for (int Index : Curve.Points) Samples.push_back(Position(Index));
            return Samples;
        }
        const std::vector<double> Breaks = CollectSketchSplineBreaks(Resolved.Curve);
        for (size_t Piece = 0; Piece + 1 < Breaks.size(); ++Piece)
        {
            for (int Index = Piece == 0 ? 0 : 1; Index <= 16; ++Index) Samples.push_back(EvaluateSketchSpline(Resolved.Curve, Breaks[Piece] + (Breaks[Piece + 1] - Breaks[Piece]) * Index / 16.0).Position);
        }
        return Samples;
    }
    const Vector2 Centre = Position(Curve.Centre);
    const double  Radius = MeasureSketchRadius(Sketch, Curve);
    double First = 0.0, Sweep = 2.0 * std::numbers::pi;
    if (Curve.Kind == SketchCurveKind::Arc)
    {
        const Vector2 Start = Position(Curve.Start) - Centre, End = Position(Curve.End) - Centre;
        First = std::atan2(Start.Y, Start.X);
        Sweep = std::atan2(End.Y, End.X) - First;
        while (Sweep <= 0.0) Sweep += 2.0 * std::numbers::pi;
    }
    const int Count = std::max(4, static_cast<int>(std::ceil(64.0 * Sweep / (2.0 * std::numbers::pi))));
    for (int Sample = 0; Sample <= Count; ++Sample)
    {
        const double Angle = First + Sweep * Sample / Count;
        Samples.push_back({ Centre.X + Radius * std::cos(Angle), Centre.Y + Radius * std::sin(Angle) });
    }
    return Samples;
}

} // namespace Frontier::GeometrySpace::Parametric
