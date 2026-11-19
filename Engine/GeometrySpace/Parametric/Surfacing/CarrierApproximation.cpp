/*====================================================================================================================================
                                                   CARRIERAPPROXIMATION.CPP
====================================================================================================================================*/
// 🧩 Certified cubic B-spline stand-ins for procedural carriers: Greville interpolation, triple knots at C¹ breaks

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Surfacing/CarrierApproximation.h"

#include "Engine/GeometrySpace/Parametric/Surfacing/LinearSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr int                   SplineDegree = 3;                                        // [-] - Cubic
    constexpr std::array<double, 6> CellProbes   = { 0.0, 0.1, 0.3, 0.5, 0.7, 0.9 };         // [-] - Fractions per span (start included)
    constexpr int                   BundleCount  = 17;                                       // [-] - Iso-curves per direction for knot choice
    constexpr int                   TensorRounds = 12;                                       // [-] - Cell refinement rounds

    // 📝 Span layout of one direction: sorted breaks Low … High. Carrier C¹ breaks (spine / basis knots) are seeded as
    //    boundaries with multiplicity 2 (C¹ in the spline, matching the carrier: a traced spine's chart speed kinks by a
    //    few per cent of curvature at every trace knot, which a C² spline could only smooth with tiny spans); every
    //    other interior break has multiplicity 1 (C²). Measured on the r 0.2 rim at 10⁻⁶: 222 spans seeded vs 266 split
    //    on demand.
    struct SpanLayout
    {
        std::vector<double> Breaks     = {};                                                 // [-] - Span boundaries
        std::vector<double> Structural = {};                                                 // [-] - Carrier breaks, sorted
    };

    [[nodiscard]] SpanLayout OpenLayout(double Low, double High, const std::vector<double>& Breakpoints, int Pieces)
    {
        SpanLayout Layout;
        const double Margin = 1.0e-9 * (High - Low);                                          // [-] - Breaks this close to an end are the end
        for (double Break : Breakpoints)
            if (Break > Low + Margin && Break < High - Margin && (Layout.Structural.empty() || Break > Layout.Structural.back() + Margin)) Layout.Structural.push_back(Break);
        for (int Piece = 0; Piece < Pieces; ++Piece) Layout.Breaks.push_back(Low + (High - Low) * Piece / Pieces);
        Layout.Breaks.insert(Layout.Breaks.end(), Layout.Structural.begin(), Layout.Structural.end());
        Layout.Breaks.push_back(High);
        std::sort(Layout.Breaks.begin(), Layout.Breaks.end());
        Layout.Breaks.erase(std::unique(Layout.Breaks.begin(), Layout.Breaks.end(), [&](double First, double Second) { return Second - First <= Margin; }), Layout.Breaks.end());
        return Layout;
    }

    [[nodiscard]] bool StructuralCondition(const SpanLayout& Layout, double Value) { return std::binary_search(Layout.Structural.begin(), Layout.Structural.end(), Value); }

    [[nodiscard]] std::vector<double> AssembleKnots(const SpanLayout& Layout)
    {
        std::vector<double> Knots(SplineDegree + 1, Layout.Breaks.front());
        for (size_t Index = 1; Index + 1 < Layout.Breaks.size(); ++Index)
        {
            const int Multiplicity = StructuralCondition(Layout, Layout.Breaks[Index]) ? 2 : 1;
            Knots.insert(Knots.end(), Multiplicity, Layout.Breaks[Index]);
        }
        Knots.insert(Knots.end(), SplineDegree + 1, Layout.Breaks.back());
        return Knots;
    }

    [[nodiscard]] int PoleCount(const std::vector<double>& Knots) { return static_cast<int>(Knots.size()) - SplineDegree - 1; }

    // Greville abscissae ξᵢ = (tᵢ₊₁ + … + tᵢ₊ₚ)/p: one per pole, interpolation there is well posed (Schoenberg–Whitney).
    [[nodiscard]] std::vector<double> GrevilleAbscissae(const std::vector<double>& Knots)
    {
        std::vector<double> Abscissae(static_cast<size_t>(PoleCount(Knots)));
        for (size_t Index = 0; Index < Abscissae.size(); ++Index)
        {
            double Sum = 0.0;
            for (int Offset = 1; Offset <= SplineDegree; ++Offset) Sum += Knots[Index + Offset];
            Abscissae[Index] = Sum / SplineDegree;
        }
        return Abscissae;
    }

    [[nodiscard]] DenseLinearArray CollocationMatrix(const std::vector<double>& Knots, const std::vector<double>& Abscissae)
    {
        const int   Count = PoleCount(Knots);
        DenseLinearArray Matrix(Count, Count);
        for (int Row = 0; Row < Count; ++Row)
        {
            const NurbsBasisSample Basis = EvaluateNurbsBasis(Knots, SplineDegree, Count, Abscissae[Row]);
            for (size_t Offset = 0; Offset < Basis.Values[0].size(); ++Offset) Matrix.At(Row, Basis.First + static_cast<int>(Offset)) = Basis.Values[0][Offset];
        }
        return Matrix;
    }

    // Solves Matrix · X = Samples for Bundle columns of points at once (Samples[member][row]); nullopt when singular.
    [[nodiscard]] std::optional<std::vector<std::vector<Vector3>>> SolveBundle(const DenseLinearArray& Matrix, const std::vector<std::vector<Vector3>>& Samples)
    {
        const int   Count = Matrix.Rows;
        const int   Members = static_cast<int>(Samples.size());
        DenseLinearArray Sides(Count, 3 * Members);
        for (int Member = 0; Member < Members; ++Member)
            for (int Row = 0; Row < Count; ++Row)
            {
                Sides.At(Row, 3 * Member + 0) = Samples[Member][Row].X;
                Sides.At(Row, 3 * Member + 1) = Samples[Member][Row].Y;
                Sides.At(Row, 3 * Member + 2) = Samples[Member][Row].Z;
            }
        const std::optional<DenseLinearArray> Solution = SolveDenseSystem(Matrix, Sides);
        if (!Solution) return std::nullopt;
        std::vector<std::vector<Vector3>> Poles(static_cast<size_t>(Members), std::vector<Vector3>(static_cast<size_t>(Count)));
        for (int Member = 0; Member < Members; ++Member)
            for (int Row = 0; Row < Count; ++Row) Poles[Member][Row] = { Solution->At(Row, 3 * Member + 0), Solution->At(Row, 3 * Member + 1), Solution->At(Row, 3 * Member + 2) };
        return Poles;
    }

    [[nodiscard]] Vector3 EvaluateSpline(const std::vector<double>& Knots, const std::vector<Vector3>& Poles, double τ)
    {
        const NurbsBasisSample Basis = EvaluateNurbsBasis(Knots, SplineDegree, static_cast<int>(Poles.size()), τ);
        Vector3 Position = {};
        for (size_t Offset = 0; Offset < Basis.Values[0].size(); ++Offset) Position = Position + Poles[Basis.First + Offset] * Basis.Values[0][Offset];
        return Position;
    }

    [[nodiscard]] Vector3 EvaluateSplineDerivative(const std::vector<double>& Knots, const std::vector<Vector3>& Poles, double τ)
    {
        const NurbsBasisSample Basis = EvaluateNurbsBasis(Knots, SplineDegree, static_cast<int>(Poles.size()), τ);
        Vector3 Derivative = {};
        for (size_t Offset = 0; Offset < Basis.Values[1].size(); ++Offset) Derivative = Derivative + Poles[Basis.First + Offset] * Basis.Values[1][Offset];
        return Derivative;
    }

    // Angle between two directions as lines (0 when either vanishes: a collapsed row has no direction to keep).
    [[nodiscard]] double LineAngle(Vector3 First, Vector3 Second) noexcept
    {
        const double Scale = Length(First) * Length(Second);
        return Scale > 0.0 ? std::asin(std::min(1.0, Length(Cross(First, Second)) / Scale)) : 0.0;
    }

    [[nodiscard]] bool FiniteCondition(Vector3 Position) noexcept { return std::isfinite(Position.X) && std::isfinite(Position.Y) && std::isfinite(Position.Z); }

    // Each span flagged in Split is halved — at the carrier break nearest its middle when one lies well inside it.
    void SplitSpans(SpanLayout& Layout, const std::vector<char>& Split)
    {
        std::vector<double> Breaks = { Layout.Breaks.front() };
        for (size_t Span = 0; Span + 1 < Layout.Breaks.size(); ++Span)
        {
            const double Low = Layout.Breaks[Span], High = Layout.Breaks[Span + 1], Middle = 0.5 * (Low + High);
            if (Split[Span])
            {
                double Cut = Middle;
                const double Margin = 1.0e-3 * (High - Low);                                  // [-] - Keep both halves real
                const auto   First  = std::upper_bound(Layout.Structural.begin(), Layout.Structural.end(), Low + Margin);
                for (auto Break = First; Break != Layout.Structural.end() && *Break < High - Margin; ++Break)
                    if (Cut == Middle || std::fabs(*Break - Middle) < std::fabs(Cut - Middle)) Cut = *Break;
                Breaks.push_back(Cut);
            }
            Breaks.push_back(High);
        }
        Layout.Breaks = std::move(Breaks);
    }

    // 📝 Adaptive 1D interpolation of a bundle of curves sharing one knot vector: fit, probe each span against the exact
    //    bundle (position within Tolerance, tangent line within AngularTolerance), halve the failing spans, repeat.
    //    Sample fills one position and one derivative per member (false = not finite).
    struct BundleFit
    {
        SpanLayout                        Layout    = {};                                    // [-]
        std::vector<double>               Knots     = {};                                    // [-]
        std::vector<std::vector<Vector3>> Poles     = {};                                    // [len] - Per member
        double                            Deviation = 0.0;                                   // [len]
        std::string                       Refusal   = {};                                    // [-]
    };

    using BundleSample = std::function<bool(double, std::vector<Vector3>&, std::vector<Vector3>&)>;

    [[nodiscard]] BundleFit FitBundle(const BundleSample& Sample, SpanLayout Layout, double Tolerance, double AngularTolerance, int MaximumSpans)
    {
        BundleFit Fit;
        std::vector<Vector3> Exact, Tangents;
        while (true)
        {
            const int Spans = static_cast<int>(Layout.Breaks.size()) - 1;
            if (Spans > MaximumSpans) { Fit.Refusal = "approximation: more than " + std::to_string(MaximumSpans) + " spans needed for the tolerance"; return Fit; }
            const std::vector<double> Knots     = AssembleKnots(Layout);
            const std::vector<double> Abscissae = GrevilleAbscissae(Knots);
            std::vector<std::vector<Vector3>> Samples;
            for (size_t Row = 0; Row < Abscissae.size(); ++Row)
            {
                if (!Sample(Abscissae[Row], Exact, Tangents)) { Fit.Refusal = "approximation: the carrier is not finite on the window"; return Fit; }
                if (Samples.empty()) Samples.assign(Exact.size(), std::vector<Vector3>(Abscissae.size()));
                for (size_t Member = 0; Member < Exact.size(); ++Member) Samples[Member][Row] = Exact[Member];
            }
            const std::optional<std::vector<std::vector<Vector3>>> Poles = SolveBundle(CollocationMatrix(Knots, Abscissae), Samples);
            if (!Poles) { Fit.Refusal = "approximation: singular collocation system"; return Fit; }
            std::vector<char> Split(static_cast<size_t>(Spans), 0);
            double Deviation = 0.0;
            for (int Span = 0; Span < Spans; ++Span)
            {
                const double Low = Layout.Breaks[Span], High = Layout.Breaks[Span + 1];
                for (double Fraction : CellProbes)
                {
                    const double τ = Low + (High - Low) * Fraction;
                    if (!Sample(τ, Exact, Tangents)) { Fit.Refusal = "approximation: the carrier is not finite on the window"; return Fit; }
                    for (size_t Member = 0; Member < Exact.size(); ++Member)
                    {
                        const double Gap   = Distance(Exact[Member], EvaluateSpline(Knots, (*Poles)[Member], τ));
                        const double Angle = LineAngle(Tangents[Member], EvaluateSplineDerivative(Knots, (*Poles)[Member], τ));
                        Deviation = std::max(Deviation, Gap);
                        if (!(Gap <= Tolerance) || !(Angle <= AngularTolerance)) Split[Span] = 1;
                    }
                }
            }
            if (std::none_of(Split.begin(), Split.end(), [](char Flag) { return Flag != 0; }))
            {
                Fit.Layout = std::move(Layout);
                Fit.Knots = Knots;
                Fit.Poles = *Poles;
                Fit.Deviation = Deviation;
                return Fit;
            }
            SplitSpans(Layout, Split);
        }
    }

    [[nodiscard]] bool UsableSettings(const ApproximationSettings& Settings) noexcept
    {
        return std::isfinite(Settings.Tolerance) && Settings.Tolerance > 0.0 && std::isfinite(Settings.AngularTolerance) && Settings.AngularTolerance > 0.0 && Settings.MaximumSpans >= 1;
    }

    // The span holding Value in a sorted break list (last span for the end).
    [[nodiscard]] size_t LocateSpan(const std::vector<double>& Breaks, double Value)
    {
        const size_t Upper = static_cast<size_t>(std::upper_bound(Breaks.begin(), Breaks.end(), Value) - Breaks.begin());
        return std::min(Upper == 0 ? 0 : Upper - 1, Breaks.size() - 2);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SurfaceApproximation ApproximateDeformedSurface(const SurfaceGeometry& Exact,
                                                const std::function<SurfaceSample(const SurfaceSample&)>& Deform,
                                                Vector2 Low, Vector2 High, const ApproximationSettings& Settings)
{
    SurfaceApproximation Outcome;
    if (!UsableSettings(Settings)) { Outcome.Refusal = "approximation: the tolerance must be positive and finite"; return Outcome; }
    if (!(std::isfinite(Low.X) && std::isfinite(Low.Y) && std::isfinite(High.X) && std::isfinite(High.Y) && High.X > Low.X && High.Y > Low.Y))
    {
        Outcome.Refusal = "approximation: empty or non-finite parameter window";
        return Outcome;
    }
    const auto EvaluateMapped = [&](double U, double V) { return Deform ? Deform(EvaluateSurface(Exact, { U, V })) : EvaluateSurface(Exact, { U, V }); };
    const auto Position       = [&](double U, double V) { return EvaluateMapped(U, V).Position; };
    const auto Bundle         = [&](const std::vector<double>& Across, bool AlongU)
    {
        return [&EvaluateMapped, &Across, AlongU](double Value, std::vector<Vector3>& Positions, std::vector<Vector3>& Tangents)
        {
            Positions.resize(Across.size());
            Tangents.resize(Across.size());
            for (size_t Member = 0; Member < Across.size(); ++Member)
            {
                const SurfaceSample Sample = EvaluateMapped(AlongU ? Value : Across[Member], AlongU ? Across[Member] : Value);
                Positions[Member] = Sample.Position;
                Tangents[Member]  = AlongU ? Sample.DerivativeU : Sample.DerivativeV;
                if (!FiniteCondition(Positions[Member]) || !FiniteCondition(Tangents[Member])) return false;
            }
            return true;
        };
    };

    // ① Carrier breaks, then knots per direction from iso-curve bundles.
    SpanLayout LayoutU = OpenLayout(Low.X, High.X, CollectSurfaceBreakpoints(Exact, Low.X, High.X), 4);
    SpanLayout LayoutV = OpenLayout(Low.Y, High.Y, CollectSurfaceBreakpointsAlongV(Exact, Low.Y, High.Y), 4);
    const auto Across = [](double First, double Last)
    {
        std::vector<double> Values;
        for (int Index = 0; Index < BundleCount; ++Index) Values.push_back(First + (Last - First) * Index / (BundleCount - 1));
        return Values;
    };
    const std::vector<double> AcrossV = Across(Low.Y, High.Y), AcrossU = Across(Low.X, High.X);
    const BundleFit FitU = FitBundle(Bundle(AcrossV, true), LayoutU, 0.25 * Settings.Tolerance, 0.25 * Settings.AngularTolerance, Settings.MaximumSpans);
    if (!FitU.Refusal.empty()) { Outcome.Refusal = FitU.Refusal + " (along u)"; return Outcome; }
    const BundleFit FitV = FitBundle(Bundle(AcrossU, false), LayoutV, 0.25 * Settings.Tolerance, 0.25 * Settings.AngularTolerance, Settings.MaximumSpans);
    if (!FitV.Refusal.empty()) { Outcome.Refusal = FitV.Refusal + " (along v)"; return Outcome; }
    LayoutU = FitU.Layout;
    LayoutV = FitV.Layout;

    // ② Tensor interpolation at the Greville lattice (u solve, then v solve) and certification per knot cell.
    for (int Round = 1; Round <= TensorRounds; ++Round)
    {
        Outcome.Rounds = Round;
        const int SpansU = static_cast<int>(LayoutU.Breaks.size()) - 1, SpansV = static_cast<int>(LayoutV.Breaks.size()) - 1;
        if (SpansU > Settings.MaximumSpans || SpansV > Settings.MaximumSpans)
        {
            Outcome.Refusal = "approximation: more than " + std::to_string(Settings.MaximumSpans) + " spans needed for the tolerance";
            return Outcome;
        }
        const std::vector<double> KnotsU = AssembleKnots(LayoutU), KnotsV = AssembleKnots(LayoutV);
        const std::vector<double> GrevilleU = GrevilleAbscissae(KnotsU), GrevilleV = GrevilleAbscissae(KnotsV);
        const int CountU = PoleCount(KnotsU), CountV = PoleCount(KnotsV);
        std::vector<std::vector<Vector3>> Columns(static_cast<size_t>(CountV), std::vector<Vector3>(static_cast<size_t>(CountU)));
        for (int I = 0; I < CountU; ++I)
            for (int J = 0; J < CountV; ++J)
            {
                Columns[J][I] = Position(GrevilleU[I], GrevilleV[J]);
                if (!FiniteCondition(Columns[J][I])) { Outcome.Refusal = "approximation: the carrier is not finite on the window"; return Outcome; }
            }
        const std::optional<std::vector<std::vector<Vector3>>> Intermediate = SolveBundle(CollocationMatrix(KnotsU, GrevilleU), Columns);
        if (!Intermediate) { Outcome.Refusal = "approximation: singular collocation system"; return Outcome; }
        std::vector<std::vector<Vector3>> Rows(static_cast<size_t>(CountU), std::vector<Vector3>(static_cast<size_t>(CountV)));
        for (int I = 0; I < CountU; ++I)
            for (int J = 0; J < CountV; ++J) Rows[I][J] = (*Intermediate)[J][I];
        const std::optional<std::vector<std::vector<Vector3>>> Final = SolveBundle(CollocationMatrix(KnotsV, GrevilleV), Rows);
        if (!Final) { Outcome.Refusal = "approximation: singular collocation system"; return Outcome; }
        std::vector<Vector3> Poles;
        Poles.reserve(static_cast<size_t>(CountU) * CountV);
        for (int I = 0; I < CountU; ++I)
            for (int J = 0; J < CountV; ++J) Poles.push_back((*Final)[I][J]);
        std::optional<NurbsSurface> Surface = ConstructNurbsSurface(SplineDegree, SplineDegree, CountU, CountV, KnotsU, KnotsV, Poles, std::vector<double>(Poles.size(), 1.0));
        if (!Surface) { Outcome.Refusal = "approximation: the spline could not be constructed"; return Outcome; }

        std::vector<char> SplitU(static_cast<size_t>(SpansU), 0), SplitV(static_cast<size_t>(SpansV), 0);
        double Deviation = 0.0, Drift = 0.0;
        std::vector<double> ProbesU, ProbesV;
        for (int Span = 0; Span < SpansU; ++Span)
            for (double Fraction : CellProbes) ProbesU.push_back(LayoutU.Breaks[Span] + (LayoutU.Breaks[Span + 1] - LayoutU.Breaks[Span]) * Fraction);
        for (int Span = 0; Span < SpansV; ++Span)
            for (double Fraction : CellProbes) ProbesV.push_back(LayoutV.Breaks[Span] + (LayoutV.Breaks[Span + 1] - LayoutV.Breaks[Span]) * Fraction);
        ProbesU.push_back(High.X);
        ProbesV.push_back(High.Y);
        for (double U : ProbesU)
            for (double V : ProbesV)
            {
                const SurfaceSample Reference = EvaluateMapped(U, V);
                const SurfaceSample Spline    = EvaluateNurbsSurface(*Surface, { U, V });
                const double Gap   = Distance(Reference.Position, Spline.Position);
                const double Angle = LineAngle(Cross(Reference.DerivativeU, Reference.DerivativeV), Cross(Spline.DerivativeU, Spline.DerivativeV));
                Deviation = std::max(Deviation, Gap);
                Drift     = std::max(Drift, Angle);
                if (!(Gap <= Settings.Tolerance) || !(Angle <= 0.5 * Settings.AngularTolerance)) { SplitU[LocateSpan(LayoutU.Breaks, U)] = 1; SplitV[LocateSpan(LayoutV.Breaks, V)] = 1; }
            }
        Outcome.SpansU = SpansU;
        Outcome.SpansV = SpansV;
        Outcome.Deviation = Deviation;
        Outcome.Drift     = Drift;
        if (Deviation <= Settings.Tolerance && Drift <= 0.5 * Settings.AngularTolerance)
        {
            Outcome.Surface = std::move(Surface);
            return Outcome;
        }
        SplitSpans(LayoutU, SplitU);
        SplitSpans(LayoutV, SplitV);
    }
    Outcome.Refusal = "approximation: the tensor fit did not certify within " + std::to_string(TensorRounds) + " rounds";
    return Outcome;
}

SurfaceApproximation ApproximateSurfaceAsNurbs(const SurfaceGeometry& Exact, Vector2 Low, Vector2 High, const ApproximationSettings& Settings)
{
    return ApproximateDeformedSurface(Exact, {}, Low, High, Settings);
}

CurveApproximation ApproximateDeformedCurve(const CurveGeometry& Exact,
                                            const std::function<Vector3(Vector3)>& Deform,
                                            double Low, double High, const ApproximationSettings& Settings)
{
    CurveApproximation Outcome;
    if (!UsableSettings(Settings)) { Outcome.Refusal = "approximation: the tolerance must be positive and finite"; return Outcome; }
    if (!(std::isfinite(Low) && std::isfinite(High) && High > Low)) { Outcome.Refusal = "approximation: empty or non-finite interval"; return Outcome; }
    const auto EvaluatePoint = [&](double τ)
    {
        const Vector3 Point = EvaluateCurve(Exact, τ).Position;
        return Deform ? Deform(Point) : Point;
    };
    SpanLayout Layout = OpenLayout(Low, High, CollectCurveBreakpoints(Exact, Low, High), 2);
    const BundleFit Fit = FitBundle([&](double τ, std::vector<Vector3>& Point, std::vector<Vector3>& Tangent)
    {
        Point.assign(1, EvaluatePoint(τ));
        Tangent.assign(1, Vector3{});
        return FiniteCondition(Point[0]);
    }, Layout, 0.5 * Settings.Tolerance, std::numeric_limits<double>::infinity(), Settings.MaximumSpans);
    if (!Fit.Refusal.empty()) { Outcome.Refusal = Fit.Refusal; return Outcome; }
    std::optional<NurbsCurve> Curve = ConstructNurbsCurve(SplineDegree, Fit.Knots, Fit.Poles[0], std::vector<double>(Fit.Poles[0].size(), 1.0));
    if (!Curve) { Outcome.Refusal = "approximation: the spline could not be constructed"; return Outcome; }
    double Deviation = 0.0;
    for (size_t Span = 0; Span + 1 < Fit.Layout.Breaks.size(); ++Span)
        for (int Probe = 0; Probe <= 16; ++Probe)
        {
            const double τ = Fit.Layout.Breaks[Span] + (Fit.Layout.Breaks[Span + 1] - Fit.Layout.Breaks[Span]) * Probe / 16.0;
            Deviation = std::max(Deviation, Distance(EvaluatePoint(τ), EvaluateCurve(*Curve, τ).Position));
        }
    if (!(Deviation <= Settings.Tolerance)) { Outcome.Refusal = "approximation: the curve did not certify"; return Outcome; }
    Outcome.Curve     = std::move(Curve);
    Outcome.Deviation = Deviation;
    Outcome.Spans     = static_cast<int>(Fit.Layout.Breaks.size()) - 1;
    return Outcome;
}

CurveApproximation ApproximateCurveAsNurbs(const CurveGeometry& Exact, double Low, double High, const ApproximationSettings& Settings)
{
    return ApproximateDeformedCurve(Exact, {}, Low, High, Settings);
}

}
