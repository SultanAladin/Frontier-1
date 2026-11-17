/*====================================================================================================================================
                                                         CURVENETWORK.CPP
====================================================================================================================================*/
// 🧩 Curve networks: crossing search, 2-colour family sorting, monotone reparametrisation + KKT refit, Gordon S₁ + S₂ − T

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Surfacing/CurveNetwork.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/LinearSystem.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/PanelAssembly.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/SurfaceAlgebra.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <optional>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PRIVATE FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr int    CrossingLattice = 48;                                                   // [-] - Seed samples per curve
    constexpr double KnotMerge       = 1.0e-9;                                               // [-] - Knots closer than this merge
    constexpr double OrderGap        = 1.0e-9;                                               // [-] - Crossings must advance by more than this

    [[nodiscard]] std::string Millimetres(double Value)
    {
        char Text[48];
        std::snprintf(Text, sizeof Text, "%.3g mm", Value * 1000.0);
        return Text;
    }

    [[nodiscard]] Vector3 PointAt(const NurbsCurveDefinition& Curve, double Parameter) { return EvaluateNurbsCurveDerivatives(Curve, Parameter).Position; }

    //--------------------------------------------------------------------------------------------------------------------
    //                                            ONE-DIMENSIONAL MAPS
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Monotone piecewise-cubic Hermite map through (Xₖ, Yₖ) (Fritsch & Carlson 1980): slopes from the secant harmonic
    //    rule, zero where the data turns, limited to 3·secant — strictly increasing data gives a strictly increasing map.
    struct MonotoneMap
    {
        std::vector<double> Nodes  = {};                                                     // [-] - Xₖ
        std::vector<double> Values = {};                                                     // [-] - Yₖ
        std::vector<double> Slopes = {};                                                     // [-] - dY/dX at the nodes

        [[nodiscard]] double operator()(double X) const
        {
            const size_t Span = std::min<size_t>(std::upper_bound(Nodes.begin(), Nodes.end(), X) - Nodes.begin(), Nodes.size() - 1);
            const size_t Low  = Span == 0 ? 0 : Span - 1;
            const double Width = Nodes[Low + 1] - Nodes[Low];
            const double T = (X - Nodes[Low]) / Width, T2 = T * T, T3 = T2 * T;
            return (2.0 * T3 - 3.0 * T2 + 1.0) * Values[Low] + (T3 - 2.0 * T2 + T) * Width * Slopes[Low]
                 + (-2.0 * T3 + 3.0 * T2) * Values[Low + 1] + (T3 - T2) * Width * Slopes[Low + 1];
        }

        [[nodiscard]] double Inverse(double Y) const
        {
            double Low = 0.0, High = 1.0;
            for (int Iteration = 0; Iteration < 80; ++Iteration)
            {
                const double Middle = 0.5 * (Low + High);
                ((*this)(Middle) < Y ? Low : High) = Middle;
            }
            return 0.5 * (Low + High);
        }
    };

    [[nodiscard]] MonotoneMap BuildMonotoneMap(const std::vector<double>& Nodes, const std::vector<double>& Values)
    {
        MonotoneMap Map{ Nodes, Values, std::vector<double>(Nodes.size(), 0.0) };
        const size_t Count = Nodes.size();
        std::vector<double> Secants(Count - 1);
        for (size_t Index = 0; Index + 1 < Count; ++Index) Secants[Index] = (Values[Index + 1] - Values[Index]) / (Nodes[Index + 1] - Nodes[Index]);
        if (Count == 2) { Map.Slopes = { Secants[0], Secants[0] }; return Map; }
        for (size_t Index = 1; Index + 1 < Count; ++Index)
        {
            const double Before = Secants[Index - 1], After = Secants[Index];
            if (Before * After <= 0.0) continue;
            const double WidthBefore = Nodes[Index] - Nodes[Index - 1], WidthAfter = Nodes[Index + 1] - Nodes[Index];
            const double First = 2.0 * WidthAfter + WidthBefore, Second = WidthAfter + 2.0 * WidthBefore;   // Brodlie weighting
            Map.Slopes[Index] = (First + Second) / (First / Before + Second / After);
        }
        // Ends: one-sided three-point estimate, kept on the side of the secant and below 3·secant.
        auto EndSlope = [](double Width0, double Width1, double Secant0, double Secant1)
        {
            double Slope = ((2.0 * Width0 + Width1) * Secant0 - Width0 * Secant1) / (Width0 + Width1);
            if (Slope * Secant0 <= 0.0) Slope = 0.0;
            else if (Secant0 * Secant1 <= 0.0 && std::abs(Slope) > 3.0 * std::abs(Secant0)) Slope = 3.0 * Secant0;
            return Slope;
        };
        Map.Slopes.front() = EndSlope(Nodes[1] - Nodes[0], Nodes[2] - Nodes[1], Secants[0], Secants[1]);
        Map.Slopes.back()  = EndSlope(Nodes[Count - 1] - Nodes[Count - 2], Nodes[Count - 2] - Nodes[Count - 3], Secants[Count - 2], Secants[Count - 3]);
        for (size_t Index = 0; Index + 1 < Count; ++Index)
        {
            if (Secants[Index] == 0.0) { Map.Slopes[Index] = Map.Slopes[Index + 1] = 0.0; continue; }
            const double Alpha = Map.Slopes[Index] / Secants[Index], Beta = Map.Slopes[Index + 1] / Secants[Index];
            const double Radius = std::hypot(Alpha, Beta);
            if (Radius > 3.0)
            {
                Map.Slopes[Index]     = 3.0 * Alpha / Radius * Secants[Index];
                Map.Slopes[Index + 1] = 3.0 * Beta / Radius * Secants[Index];
            }
        }
        return Map;
    }

    // 📝 Natural cubic spline of 3D offsets through (Xₖ, Dₖ): the smooth shift that closes a curve's crossings.
    struct OffsetSpline
    {
        std::vector<double>  Nodes    = {};                                                  // [-]   - Xₖ
        std::vector<Vector3> Values   = {};                                                  // [len] - Dₖ
        std::vector<Vector3> Moments  = {};                                                  // [len] - Second derivatives at the nodes

        [[nodiscard]] Vector3 operator()(double X) const
        {
            const size_t Span = std::min<size_t>(std::upper_bound(Nodes.begin(), Nodes.end(), X) - Nodes.begin(), Nodes.size() - 1);
            const size_t Low  = Span == 0 ? 0 : Span - 1;
            const double Width = Nodes[Low + 1] - Nodes[Low];
            const double A = (Nodes[Low + 1] - X) / Width, B = 1.0 - A;
            return Values[Low] * A + Values[Low + 1] * B
                 + (Moments[Low] * (A * A * A - A) + Moments[Low + 1] * (B * B * B - B)) * (Width * Width / 6.0);
        }
    };

    [[nodiscard]] OffsetSpline BuildOffsetSpline(const std::vector<double>& Nodes, const std::vector<Vector3>& Values)
    {
        OffsetSpline Spline{ Nodes, Values, std::vector<Vector3>(Nodes.size(), Vector3{}) };
        const int Count = static_cast<int>(Nodes.size());
        if (Count < 3) return Spline;
        // Thomas algorithm on the interior moments (natural ends: M₀ = Mₙ = 0).
        std::vector<double>  Diagonal(Count, 0.0), Upper(Count, 0.0);
        std::vector<Vector3> Right(Count, Vector3{});
        for (int Index = 1; Index < Count - 1; ++Index)
        {
            const double Before = Nodes[Index] - Nodes[Index - 1], After = Nodes[Index + 1] - Nodes[Index];
            const double Lower  = Before / 6.0;
            Diagonal[Index] = (Before + After) / 3.0;
            Upper[Index]    = After / 6.0;
            Right[Index]    = (Values[Index + 1] - Values[Index]) * (1.0 / After) - (Values[Index] - Values[Index - 1]) * (1.0 / Before);
            if (Index > 1)
            {
                const double Factor = Lower / Diagonal[Index - 1];
                Diagonal[Index] -= Factor * Upper[Index - 1];
                Right[Index]     = Right[Index] - Right[Index - 1] * Factor;
            }
        }
        for (int Index = Count - 2; Index >= 1; --Index)
        {
            const Vector3 Next = Index + 1 < Count - 1 ? Spline.Moments[Index + 1] : Vector3{};
            Spline.Moments[Index] = (Right[Index] - Next * Upper[Index]) * (1.0 / Diagonal[Index]);
        }
        return Spline;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                            FAMILY REFIT
    //--------------------------------------------------------------------------------------------------------------------

    struct FamilyRefit
    {
        std::vector<NurbsCurveDefinition> Curves               = {};                         // [-]   - On one knot vector
        double                            WorstShift           = 0.0;                        // [len] - max |δ|
        double                            WorstReapproximation = 0.0;                        // [len] - max |refit − target|
        std::string                       Refusal              = {};
    };

    // 💡 Each curve j becomes Tⱼ(𝑢) = Cⱼ(φⱼ(𝑢)) + δⱼ(𝑢), then one shared knot vector carries a least-squares fit of every
    //    Tⱼ with Tⱼ(𝑈ₖ) = Pⱼₖ imposed exactly (Lagrange multipliers). Spans holding an error above the reapproximation
    //    tolerance split at their middle, for the whole family, until the limit.
    [[nodiscard]] FamilyRefit RefitFamily(const std::vector<NurbsCurveDefinition>& Originals, const std::vector<std::vector<double>>& Crossings,
                                          const std::vector<double>& Nodes, const std::vector<std::vector<Vector3>>& Targets, const NetworkSettings& Settings,
                                          const std::string& FamilyName)
    {
        FamilyRefit Refit;
        const int CountCurves = static_cast<int>(Originals.size()), CountNodes = static_cast<int>(Nodes.size());
        int Degree = 1;
        for (const NurbsCurveDefinition& Curve : Originals) Degree = std::max(Degree, Curve.Degree);
        std::vector<MonotoneMap>  Maps;
        std::vector<OffsetSpline> Shifts;
        for (int Curve = 0; Curve < CountCurves; ++Curve)
        {
            Maps.push_back(BuildMonotoneMap(Nodes, Crossings[Curve]));
            std::vector<Vector3> Offsets;
            for (int Node = 0; Node < CountNodes; ++Node) Offsets.push_back(Targets[Curve][Node] - PointAt(Originals[Curve], Crossings[Curve][Node]));
            Shifts.push_back(BuildOffsetSpline(Nodes, Offsets));
        }
        auto Target = [&](int Curve, double U) { return PointAt(Originals[Curve], std::clamp(Maps[Curve](U), 0.0, 1.0)) + Shifts[Curve](U); };

        // ① Initial interior knots: every curve's own knots pulled back through its map, plus the crossing nodes.
        std::vector<std::pair<double, int>> Interior;
        auto AddKnot = [&](double Value, int Count)
        {
            for (auto& [Existing, Multiplicity] : Interior)
            {
                if (std::abs(Existing - Value) > KnotMerge) continue;
                Multiplicity = std::min(Degree, std::max(Multiplicity, Count));
                return;
            }
            Interior.push_back({ Value, std::min(Degree, Count) });
        };
        for (int Curve = 0; Curve < CountCurves; ++Curve)
        {
            for (const auto& [Value, Count] : CollectInteriorKnots(Originals[Curve].Knots, Originals[Curve].Degree)) AddKnot(Maps[Curve].Inverse(Value), Count + Degree - Originals[Curve].Degree);
        }
        for (int Node = 1; Node + 1 < CountNodes; ++Node) AddKnot(Nodes[Node], 1);
        std::sort(Interior.begin(), Interior.end());

        for (;;)
        {
            std::vector<double> Knots(Degree + 1, 0.0);
            for (const auto& [Value, Count] : Interior) Knots.insert(Knots.end(), Count, Value);
            Knots.insert(Knots.end(), Degree + 1, 1.0);
            const int Count = static_cast<int>(Knots.size()) - Degree - 1;
            std::vector<double> Breaks = { 0.0 };
            for (const auto& [Value, Multiplicity] : Interior) Breaks.push_back(Value);
            Breaks.push_back(1.0);
            const int Spans = static_cast<int>(Breaks.size()) - 1;
            const int PerSpan = std::max(8, 2 * Degree + 2);
            std::vector<double> Samples;
            for (int Span = 0; Span < Spans; ++Span)
            {
                for (int Index = 0; Index < PerSpan; ++Index) Samples.push_back(Breaks[Span] + (Breaks[Span + 1] - Breaks[Span]) * (Index + 0.5) / PerSpan);
            }

            // ② Normal matrix NᵀN (shared by the family) bordered by the crossing rows.
            const int Size = Count + CountNodes;
            DenseMatrix Normal(Size, Size);
            std::vector<NurbsBasisSample> Bases;
            for (double Sample : Samples)
            {
                Bases.push_back(EvaluateNurbsBasis(Knots, Degree, Count, Sample));
                const NurbsBasisSample& Basis = Bases.back();
                for (int Row = 0; Row <= Degree; ++Row)
                {
                    for (int Column = 0; Column <= Degree; ++Column) Normal.At(Basis.First + Row, Basis.First + Column) += Basis.Values[0][Row] * Basis.Values[0][Column];
                }
            }
            for (int Node = 0; Node < CountNodes; ++Node)
            {
                const NurbsBasisSample Basis = EvaluateNurbsBasis(Knots, Degree, Count, Nodes[Node]);
                for (int Column = 0; Column <= Degree; ++Column)
                {
                    Normal.At(Count + Node, Basis.First + Column) = Basis.Values[0][Column];
                    Normal.At(Basis.First + Column, Count + Node) = Basis.Values[0][Column];
                }
            }
            DenseMatrix RightSides(Size, 3 * CountCurves);
            for (int Curve = 0; Curve < CountCurves; ++Curve)
            {
                for (size_t Sample = 0; Sample < Samples.size(); ++Sample)
                {
                    const Vector3 Point = Target(Curve, Samples[Sample]);
                    const NurbsBasisSample& Basis = Bases[Sample];
                    for (int Row = 0; Row <= Degree; ++Row)
                    {
                        RightSides.At(Basis.First + Row, 3 * Curve + 0) += Basis.Values[0][Row] * Point.X;
                        RightSides.At(Basis.First + Row, 3 * Curve + 1) += Basis.Values[0][Row] * Point.Y;
                        RightSides.At(Basis.First + Row, 3 * Curve + 2) += Basis.Values[0][Row] * Point.Z;
                    }
                }
                for (int Node = 0; Node < CountNodes; ++Node)
                {
                    RightSides.At(Count + Node, 3 * Curve + 0) = Targets[Curve][Node].X;
                    RightSides.At(Count + Node, 3 * Curve + 1) = Targets[Curve][Node].Y;
                    RightSides.At(Count + Node, 3 * Curve + 2) = Targets[Curve][Node].Z;
                }
            }
            const std::optional<DenseMatrix> Solution = SolveDenseSystem(std::move(Normal), std::move(RightSides));
            if (!Solution) { Refit.Refusal = FamilyName + ": singular refit at " + std::to_string(Spans) + " spans"; return Refit; }
            Refit.Curves.clear();
            for (int Curve = 0; Curve < CountCurves; ++Curve)
            {
                NurbsCurveDefinition Definition;
                Definition.Degree  = Degree;
                Definition.Knots   = Knots;
                Definition.Weights = std::vector<double>(Count, 1.0);
                for (int Pole = 0; Pole < Count; ++Pole) Definition.Poles.push_back({ Solution->At(Pole, 3 * Curve + 0), Solution->At(Pole, 3 * Curve + 1), Solution->At(Pole, 3 * Curve + 2) });
                // Clamped ends interpolate: pin the end poles to the end targets bit-exactly.
                Definition.Poles.front() = Targets[Curve].front();
                Definition.Poles.back()  = Targets[Curve].back();
                Refit.Curves.push_back(std::move(Definition));
            }

            // ③ Error per span on points between the fitting samples; split the offending spans.
            std::vector<double> SpanError(Spans, 0.0);
            Refit.WorstShift = 0.0;
            for (int Curve = 0; Curve < CountCurves; ++Curve)
            {
                for (int Span = 0; Span < Spans; ++Span)
                {
                    for (int Index = 0; Index <= 2 * PerSpan; ++Index)
                    {
                        const double U = Breaks[Span] + (Breaks[Span + 1] - Breaks[Span]) * Index / (2.0 * PerSpan);
                        SpanError[Span]  = std::max(SpanError[Span], Length(PointAt(Refit.Curves[Curve], U) - Target(Curve, U)));
                        Refit.WorstShift = std::max(Refit.WorstShift, Length(Shifts[Curve](U)));
                    }
                }
            }
            Refit.WorstReapproximation = *std::max_element(SpanError.begin(), SpanError.end());
            if (Refit.WorstReapproximation <= Settings.Reapproximation) return Refit;
            std::vector<double> Splits;
            for (int Span = 0; Span < Spans; ++Span) if (SpanError[Span] > Settings.Reapproximation) Splits.push_back(0.5 * (Breaks[Span] + Breaks[Span + 1]));
            if (Spans + static_cast<int>(Splits.size()) > Settings.SpanLimit)
            {
                Refit.Refusal = FamilyName + ": refit stays " + Millimetres(Refit.WorstReapproximation) + " away at " + std::to_string(Spans) + " spans (limit "
                              + std::to_string(Settings.SpanLimit) + ", tolerance " + Millimetres(Settings.Reapproximation) + ")";
                return Refit;
            }
            for (double Split : Splits) Interior.push_back({ Split, 1 });
            std::sort(Interior.begin(), Interior.end());
        }
    }

    // Interpolating curve through points at parameters (the same averaged knots SkinCurveFamily uses with no conditions).
    [[nodiscard]] std::optional<NurbsCurveDefinition> InterpolatePoints(const std::vector<Vector3>& Points, const std::vector<double>& Parameters, int Degree)
    {
        NurbsCurveDefinition Point;                                                          // Each point as a constant degree-1 "curve"
        std::vector<NurbsCurveDefinition> Rows;
        for (const Vector3& Position : Points)
        {
            Point.Degree  = 1;
            Point.Knots   = { 0.0, 0.0, 1.0, 1.0 };
            Point.Poles   = { Position, Position };
            Point.Weights = { 1.0, 1.0 };
            Rows.push_back(Point);
        }
        FamilySkinning Skinning;
        Skinning.Degree     = Degree;
        Skinning.Parameters = Parameters;
        std::string Refusal;
        const std::optional<NurbsSurfaceDefinition> Strip = SkinCurveFamily(Rows, Skinning, Refusal);
        if (!Strip) return std::nullopt;
        return ExtractPoleColumn(*Strip, 0);
    }

    [[nodiscard]] std::string NameOf(const std::vector<std::string>& Names, size_t Index, const char* Fallback)
    {
        return Index < Names.size() ? Names[Index] : std::string(Fallback) + " " + std::to_string(Index);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

NetworkCrossing FindCurveCrossing(const NurbsCurveDefinition& First, const NurbsCurveDefinition& Second)
{
    // ① Lattice seeds: the four closest sample pairs.
    std::vector<Vector3> FirstSamples(CrossingLattice + 1), SecondSamples(CrossingLattice + 1);
    for (int Index = 0; Index <= CrossingLattice; ++Index)
    {
        FirstSamples[Index]  = PointAt(First, static_cast<double>(Index) / CrossingLattice);
        SecondSamples[Index] = PointAt(Second, static_cast<double>(Index) / CrossingLattice);
    }
    struct Seed { double Distance; int FirstIndex; int SecondIndex; };
    std::vector<Seed> Seeds;
    for (int FirstIndex = 0; FirstIndex <= CrossingLattice; ++FirstIndex)
    {
        for (int SecondIndex = 0; SecondIndex <= CrossingLattice; ++SecondIndex) Seeds.push_back({ Length(FirstSamples[FirstIndex] - SecondSamples[SecondIndex]), FirstIndex, SecondIndex });
    }
    std::partial_sort(Seeds.begin(), Seeds.begin() + 4, Seeds.end(), [](const Seed& A, const Seed& B) { return A.Distance < B.Distance; });

    // ② Projected Newton on f = ½|C(𝑡) − D(𝑠)|²: a variable at its bound with the step pushing outward stays fixed.
    NetworkCrossing Best{ 0.0, 0.0, 1.0e300 };
    for (int SeedIndex = 0; SeedIndex < 4; ++SeedIndex)
    {
        double T = static_cast<double>(Seeds[SeedIndex].FirstIndex) / CrossingLattice, S = static_cast<double>(Seeds[SeedIndex].SecondIndex) / CrossingLattice;
        for (int Iteration = 0; Iteration < 40; ++Iteration)
        {
            const NurbsCurveDerivatives A = EvaluateNurbsCurveDerivatives(First, T), B = EvaluateNurbsCurveDerivatives(Second, S);
            const Vector3 Gap = A.Position - B.Position;
            const double GradientT = Dot(Gap, A.First), GradientS = -Dot(Gap, B.First);
            const double HessianTT = Dot(A.First, A.First) + Dot(Gap, A.Second), HessianSS = Dot(B.First, B.First) - Dot(Gap, B.Second);
            const double HessianTS = -Dot(A.First, B.First);
            const double Determinant = HessianTT * HessianSS - HessianTS * HessianTS;
            double StepT = 0.0, StepS = 0.0;
            if (Determinant > 1.0e-300)
            {
                StepT = -(HessianSS * GradientT - HessianTS * GradientS) / Determinant;
                StepS = -(HessianTT * GradientS - HessianTS * GradientT) / Determinant;
            }
            const bool FixedT = (T <= 0.0 && StepT < 0.0) || (T >= 1.0 && StepT > 0.0);
            const bool FixedS = (S <= 0.0 && StepS < 0.0) || (S >= 1.0 && StepS > 0.0);
            if (FixedT && FixedS) break;
            if (FixedT) { StepT = 0.0; StepS = HessianSS > 0.0 ? -GradientS / HessianSS : 0.0; }
            if (FixedS) { StepS = 0.0; StepT = HessianTT > 0.0 ? -GradientT / HessianTT : 0.0; }
            if (Determinant <= 1.0e-300 && !FixedT && !FixedS) break;
            const double NextT = std::clamp(T + StepT, 0.0, 1.0), NextS = std::clamp(S + StepS, 0.0, 1.0);
            const double Moved = std::abs(NextT - T) + std::abs(NextS - S);
            T = NextT; S = NextS;
            if (Moved < 1.0e-15) break;
        }
        const double Distance = Length(PointAt(First, T) - PointAt(Second, S));
        if (Distance < Best.Gap) Best = { T, S, Distance };
    }
    return Best;
}

NetworkSurfaceOutcome ConstructGordonSurface(const std::vector<NurbsCurve>& FirstInput, const std::vector<NurbsCurve>& SecondInput, const NetworkSettings& Settings,
                                             const std::vector<std::string>& FirstNames, const std::vector<std::string>& SecondNames)
{
    NetworkSurfaceOutcome Outcome;
    auto Refuse = [&](const std::string& Reason) { Outcome.Refusal = "Network: " + Reason; return Outcome; };
    if (FirstInput.size() < 2 || SecondInput.size() < 2) return Refuse("needs at least 2 curves in each family (" + std::to_string(FirstInput.size()) + " + " + std::to_string(SecondInput.size()) + ")");

    // ① Curves on [0, 1].
    std::vector<NurbsCurveDefinition> First, Second;
    for (const auto& [Input, Output, Names, Label] : { std::tuple{ &FirstInput, &First, &FirstNames, "first curve" }, std::tuple{ &SecondInput, &Second, &SecondNames, "second curve" } })
    {
        for (size_t Index = 0; Index < Input->size(); ++Index)
        {
            if (!(*Input)[Index].Definition) return Refuse(NameOf(*Names, Index, Label) + " is empty");
            const NurbsCurveDefinition& Definition = *(*Input)[Index].Definition;
            const size_t Last = Definition.Knots.size() - 1;
            if (Definition.Period > 0.0 && !(Definition.Knots[0] == Definition.Knots[Definition.Degree] && Definition.Knots[Last] == Definition.Knots[Last - Definition.Degree]))
                return Refuse(NameOf(*Names, Index, Label) + " is periodic with unclamped knots (🚧)");
            Output->push_back(MapCurveOntoUnit(Definition));
        }
    }
    const int CountFirst = static_cast<int>(First.size()), CountSecond = static_cast<int>(Second.size());

    // ② Every crossing; a pair farther apart than the meeting gap refuses, named.
    std::vector<std::vector<NetworkCrossing>> Crossings(CountFirst, std::vector<NetworkCrossing>(CountSecond));
    for (int Row = 0; Row < CountFirst; ++Row)
    {
        for (int Column = 0; Column < CountSecond; ++Column)
        {
            Crossings[Row][Column] = FindCurveCrossing(First[Row], Second[Column]);
            if (Crossings[Row][Column].Gap > Settings.MeetingGap)
                return Refuse(NameOf(FirstNames, Row, "first curve") + " and " + NameOf(SecondNames, Column, "second curve") + " do not meet (gap "
                              + Millimetres(Crossings[Row][Column].Gap) + " > " + Millimetres(Settings.MeetingGap) + ")");
        }
    }

    // ③ Order: the second family along the first curve, the first family along the first second-family curve.
    std::vector<int> OrderSecond(CountSecond), OrderFirst(CountFirst);
    std::iota(OrderSecond.begin(), OrderSecond.end(), 0);
    std::iota(OrderFirst.begin(), OrderFirst.end(), 0);
    std::stable_sort(OrderSecond.begin(), OrderSecond.end(), [&](int A, int B) { return Crossings[0][A].First < Crossings[0][B].First; });
    std::stable_sort(OrderFirst.begin(), OrderFirst.end(), [&](int A, int B) { return Crossings[A][OrderSecond.front()].Second < Crossings[B][OrderSecond.front()].Second; });

    // ④ Orientation: each first-family curve runs from the first to the last second-family curve, and vice versa.
    for (int Row = 0; Row < CountFirst; ++Row)
    {
        if (Crossings[Row][OrderSecond.front()].First <= Crossings[Row][OrderSecond.back()].First) continue;
        First[Row] = MapCurveOntoUnit(*ReverseNurbsCurve(First[Row]).Definition);
        for (NetworkCrossing& Crossing : Crossings[Row]) Crossing.First = 1.0 - Crossing.First;
        ++Outcome.Reversed;
    }
    for (int Column = 0; Column < CountSecond; ++Column)
    {
        if (Crossings[OrderFirst.front()][Column].Second <= Crossings[OrderFirst.back()][Column].Second) continue;
        Second[Column] = MapCurveOntoUnit(*ReverseNurbsCurve(Second[Column]).Definition);
        for (int Row = 0; Row < CountFirst; ++Row) Crossings[Row][Column].Second = 1.0 - Crossings[Row][Column].Second;
        ++Outcome.Reversed;
    }

    // ⑤ A grid: crossings strictly advance along every curve, and the outer curves are the boundary (crossings at the ends).
    for (int Row : OrderFirst)
    {
        for (int Index = 1; Index < CountSecond; ++Index)
        {
            if (Crossings[Row][OrderSecond[Index]].First > Crossings[Row][OrderSecond[Index - 1]].First + OrderGap) continue;
            return Refuse(NameOf(FirstNames, Row, "first curve") + " crosses " + NameOf(SecondNames, OrderSecond[Index - 1], "second curve") + " and "
                          + NameOf(SecondNames, OrderSecond[Index], "second curve") + " out of order (not a grid)");
        }
    }
    for (int Column : OrderSecond)
    {
        for (int Index = 1; Index < CountFirst; ++Index)
        {
            if (Crossings[OrderFirst[Index]][Column].Second > Crossings[OrderFirst[Index - 1]][Column].Second + OrderGap) continue;
            return Refuse(NameOf(SecondNames, Column, "second curve") + " crosses " + NameOf(FirstNames, OrderFirst[Index - 1], "first curve") + " and "
                          + NameOf(FirstNames, OrderFirst[Index], "first curve") + " out of order (not a grid)");
        }
    }
    for (int Row = 0; Row < CountFirst; ++Row)
    {
        for (const auto& [Column, End] : { std::pair{ OrderSecond.front(), 0.0 }, std::pair{ OrderSecond.back(), 1.0 } })
        {
            const double Overrun = Length(PointAt(First[Row], End) - PointAt(First[Row], Crossings[Row][Column].First));
            if (Overrun > Settings.MeetingGap)
                return Refuse(NameOf(FirstNames, Row, "first curve") + " runs " + Millimetres(Overrun) + " past the boundary curve " + NameOf(SecondNames, Column, "second curve")
                              + " (open network; trimming 🚧)");
            Crossings[Row][Column].First = End;
        }
    }
    for (int Column = 0; Column < CountSecond; ++Column)
    {
        for (const auto& [Row, End] : { std::pair{ OrderFirst.front(), 0.0 }, std::pair{ OrderFirst.back(), 1.0 } })
        {
            const double Overrun = Length(PointAt(Second[Column], End) - PointAt(Second[Column], Crossings[Row][Column].Second));
            if (Overrun > Settings.MeetingGap)
                return Refuse(NameOf(SecondNames, Column, "second curve") + " runs " + Millimetres(Overrun) + " past the boundary curve " + NameOf(FirstNames, Row, "first curve")
                              + " (open network; trimming 🚧)");
            Crossings[Row][Column].Second = End;
        }
    }

    // ⑥ Common parameters (crossing averages) and crossing targets Pᵢⱼ (foot-point midpoints).
    std::vector<double> ParametersU(CountSecond, 0.0), ParametersV(CountFirst, 0.0);
    for (int Index = 0; Index < CountSecond; ++Index)
    {
        for (int Row : OrderFirst) ParametersU[Index] += Crossings[Row][OrderSecond[Index]].First / CountFirst;
    }
    for (int Index = 0; Index < CountFirst; ++Index)
    {
        for (int Column : OrderSecond) ParametersV[Index] += Crossings[OrderFirst[Index]][Column].Second / CountSecond;
    }
    ParametersU.front() = ParametersV.front() = 0.0;
    ParametersU.back()  = ParametersV.back()  = 1.0;
    std::vector<std::vector<Vector3>> Points(CountFirst, std::vector<Vector3>(CountSecond));     // [j][i] in surface order
    for (int Index = 0; Index < CountFirst; ++Index)
    {
        for (int Other = 0; Other < CountSecond; ++Other)
        {
            const NetworkCrossing& Crossing = Crossings[OrderFirst[Index]][OrderSecond[Other]];
            Points[Index][Other] = (PointAt(First[OrderFirst[Index]], Crossing.First) + PointAt(Second[OrderSecond[Other]], Crossing.Second)) * 0.5;
            Outcome.WorstGap = std::max(Outcome.WorstGap, Crossing.Gap);
        }
    }

    // ⑦ Refit each family on one knot vector, crossings interpolated.
    std::vector<NurbsCurveDefinition> SortedFirst, SortedSecond;
    std::vector<std::vector<double>>  FirstCrossings, SecondCrossings;
    std::vector<std::vector<Vector3>> FirstTargets, SecondTargets;
    for (int Index = 0; Index < CountFirst; ++Index)
    {
        SortedFirst.push_back(First[OrderFirst[Index]]);
        FirstCrossings.emplace_back();
        for (int Column : OrderSecond) FirstCrossings.back().push_back(Crossings[OrderFirst[Index]][Column].First);
        FirstTargets.push_back(Points[Index]);
    }
    for (int Index = 0; Index < CountSecond; ++Index)
    {
        SortedSecond.push_back(Second[OrderSecond[Index]]);
        SecondCrossings.emplace_back();
        SecondTargets.emplace_back();
        for (int Other = 0; Other < CountFirst; ++Other)
        {
            SecondCrossings.back().push_back(Crossings[OrderFirst[Other]][OrderSecond[Index]].Second);
            SecondTargets.back().push_back(Points[Other][Index]);
        }
    }
    const FamilyRefit RefitFirst = RefitFamily(SortedFirst, FirstCrossings, ParametersU, FirstTargets, Settings, "first family");
    if (!RefitFirst.Refusal.empty()) return Refuse(RefitFirst.Refusal);
    const FamilyRefit RefitSecond = RefitFamily(SortedSecond, SecondCrossings, ParametersV, SecondTargets, Settings, "second family");
    if (!RefitSecond.Refusal.empty()) return Refuse(RefitSecond.Refusal);
    Outcome.WorstShift           = std::max(RefitFirst.WorstShift, RefitSecond.WorstShift);
    Outcome.WorstReapproximation = std::max(RefitFirst.WorstReapproximation, RefitSecond.WorstReapproximation);

    // ⑧ S₁ (first family skinned across 𝑣), S₂ (second family skinned across 𝑢), T (Pᵢⱼ interpolated in both).
    std::string Reason;
    FamilySkinning AcrossV;
    AcrossV.Degree     = 3;
    AcrossV.Parameters = ParametersV;
    FamilySkinning AcrossU;
    AcrossU.Degree     = 3;
    AcrossU.Parameters = ParametersU;
    const std::optional<NurbsSurfaceDefinition> SkinFirst = SkinCurveFamily(RefitFirst.Curves, AcrossV, Reason);
    if (!SkinFirst) return Refuse("first family skin: " + Reason);
    const std::optional<NurbsSurfaceDefinition> SkinSecondRaw = SkinCurveFamily(RefitSecond.Curves, AcrossU, Reason);
    if (!SkinSecondRaw) return Refuse("second family skin: " + Reason);
    const NurbsSurfaceDefinition SkinSecond = TransposeNurbsSurface(*SkinSecondRaw);
    std::vector<NurbsCurveDefinition> TensorRows;
    for (int Index = 0; Index < CountFirst; ++Index)
    {
        const std::optional<NurbsCurveDefinition> Row = InterpolatePoints(Points[Index], ParametersU, 3);
        if (!Row) return Refuse("singular crossing interpolation along 𝑢");
        TensorRows.push_back(*Row);
    }
    const std::optional<NurbsSurfaceDefinition> Tensor = SkinCurveFamily(TensorRows, AcrossV, Reason);
    if (!Tensor) return Refuse("crossing tensor: " + Reason);

    // ⑨ Compatible by exact elevation and knot union, then S = S₁ + S₂ − T pole by pole.
    std::vector<NurbsSurfaceDefinition> Parts = { *SkinFirst, SkinSecond, *Tensor };
    int DegreeU = 1, DegreeV = 1;
    for (const NurbsSurfaceDefinition& Part : Parts) { DegreeU = std::max(DegreeU, Part.DegreeU); DegreeV = std::max(DegreeV, Part.DegreeV); }
    for (NurbsSurfaceDefinition& Part : Parts)
    {
        std::optional<NurbsSurfaceDefinition> Elevated = ElevateNurbsSurfaceDegree(Part, DegreeU, DegreeV);
        if (!Elevated) return Refuse("degree elevation failed");
        Part = std::move(*Elevated);
    }
    std::vector<std::vector<double>> KnotsU, KnotsV;
    for (const NurbsSurfaceDefinition& Part : Parts) { KnotsU.push_back(Part.KnotsU); KnotsV.push_back(Part.KnotsV); }
    const std::vector<double> UnionU = UniteKnotVectors(KnotsU, DegreeU), UnionV = UniteKnotVectors(KnotsV, DegreeV);
    for (NurbsSurfaceDefinition& Part : Parts) Part = RefineNurbsSurfaceKnots(Part, MissingInteriorKnots(Part.KnotsU, UnionU, DegreeU), MissingInteriorKnots(Part.KnotsV, UnionV, DegreeV));
    for (const NurbsSurfaceDefinition& Part : Parts)
    {
        if (Part.CountU != Parts[0].CountU || Part.CountV != Parts[0].CountV) return Refuse("knot union failed (a knot exceeds multiplicity p)");
    }
    NurbsSurfaceDefinition Sum = Parts[0];
    for (size_t Index = 0; Index < Sum.Poles.size(); ++Index) Sum.Poles[Index] = Parts[0].Poles[Index] + Parts[1].Poles[Index] - Parts[2].Poles[Index];
    std::fill(Sum.Weights.begin(), Sum.Weights.end(), 1.0);
    const std::optional<NurbsSurface> Surface = FinishNurbsSurface(Sum);
    if (!Surface) return Refuse("surface construction refused the Gordon net");

    Outcome.Surface          = *Surface;
    Outcome.FirstOrder       = OrderFirst;
    Outcome.SecondOrder      = OrderSecond;
    Outcome.FirstParameters  = ParametersV;
    Outcome.SecondParameters = ParametersU;
    for (const NurbsCurveDefinition& Curve : RefitFirst.Curves)  Outcome.First.push_back(NurbsCurve{ std::make_shared<NurbsCurveDefinition>(Curve) });
    for (const NurbsCurveDefinition& Curve : RefitSecond.Curves) Outcome.Second.push_back(NurbsCurve{ std::make_shared<NurbsCurveDefinition>(Curve) });
    return Outcome;
}

NetworkSurfaceOutcome ConstructNetworkSurface(const std::vector<NurbsCurve>& Curves, const NetworkSettings& Settings, const std::vector<std::string>& Names)
{
    NetworkSurfaceOutcome Outcome;
    auto Refuse = [&](const std::string& Reason) { Outcome.Refusal = "Network: " + Reason; return Outcome; };
    const int Count = static_cast<int>(Curves.size());
    if (Count < 4) return Refuse("needs at least 4 curves (2 + 2), got " + std::to_string(Count));
    std::vector<NurbsCurveDefinition> Mapped;
    for (int Index = 0; Index < Count; ++Index)
    {
        if (!Curves[Index].Definition) return Refuse(NameOf(Names, Index, "curve") + " is empty");
        Mapped.push_back(MapCurveOntoUnit(*Curves[Index].Definition));
    }

    // ① Meeting relation, ② breadth-first 2-colouring from the lowest index (colour 0 = first family).
    std::vector<std::vector<int>> Meets(Count);
    for (int First = 0; First < Count; ++First)
    {
        for (int Second = First + 1; Second < Count; ++Second)
        {
            if (FindCurveCrossing(Mapped[First], Mapped[Second]).Gap > Settings.MeetingGap) continue;
            Meets[First].push_back(Second);
            Meets[Second].push_back(First);
        }
    }
    for (int Index = 0; Index < Count; ++Index)
    {
        if (Meets[Index].empty()) return Refuse(NameOf(Names, Index, "curve") + " meets no other curve (within " + Millimetres(Settings.MeetingGap) + ")");
    }
    std::vector<int> Colour(Count, -1);
    Colour[0] = 0;
    std::vector<int> Queue = { 0 };
    for (size_t Cursor = 0; Cursor < Queue.size(); ++Cursor)
    {
        const int Current = Queue[Cursor];
        for (int Other : Meets[Current])
        {
            if (Colour[Other] == Colour[Current])
                return Refuse(NameOf(Names, Current, "curve") + " and " + NameOf(Names, Other, "curve") + " meet but fall in the same family (the curves do not split into two crossing families)");
            if (Colour[Other] >= 0) continue;
            Colour[Other] = 1 - Colour[Current];
            Queue.push_back(Other);
        }
    }
    for (int Index = 0; Index < Count; ++Index)
    {
        if (Colour[Index] < 0) return Refuse(NameOf(Names, Index, "curve") + " is not connected to " + NameOf(Names, 0, "curve") + " (the network splits)");
    }
    std::vector<NurbsCurve> First, Second;
    std::vector<std::string> FirstNames, SecondNames;
    std::vector<int> FirstIndices, SecondIndices;
    for (int Index = 0; Index < Count; ++Index)
    {
        (Colour[Index] == 0 ? First : Second).push_back(Curves[Index]);
        (Colour[Index] == 0 ? FirstNames : SecondNames).push_back(NameOf(Names, Index, "curve"));
        (Colour[Index] == 0 ? FirstIndices : SecondIndices).push_back(Index);
    }
    Outcome = ConstructGordonSurface(First, Second, Settings, FirstNames, SecondNames);
    for (int& Index : Outcome.FirstOrder)  Index = FirstIndices[Index];
    for (int& Index : Outcome.SecondOrder) Index = SecondIndices[Index];
    return Outcome;
}

bool DetectCurveNetwork(const std::vector<NurbsCurve>& Curves, const NetworkSettings& Settings)
{
    const int Count = static_cast<int>(Curves.size());
    if (Count < 4) return false;
    std::vector<NurbsCurveDefinition> Mapped;
    Mapped.reserve(Count);
    for (const NurbsCurve& Curve : Curves)
    {
        if (!Curve.Definition) return false;
        Mapped.push_back(MapCurveOntoUnit(*Curve.Definition));
    }
    std::vector<std::vector<int>> Meets(Count);
    for (int First = 0; First < Count; ++First)
    {
        for (int Second = First + 1; Second < Count; ++Second)
        {
            if (FindCurveCrossing(Mapped[First], Mapped[Second]).Gap > Settings.MeetingGap) continue;
            Meets[First].push_back(Second);
            Meets[Second].push_back(First);
        }
    }
    for (int Index = 0; Index < Count; ++Index)
    {
        if (Meets[Index].empty()) return false;
    }
    std::vector<int> Colour(Count, -1);
    Colour[0] = 0;
    std::vector<int> Queue = { 0 };
    for (size_t Cursor = 0; Cursor < Queue.size(); ++Cursor)
    {
        const int Current = Queue[Cursor];
        for (int Other : Meets[Current])
        {
            if (Colour[Other] == Colour[Current]) return false;
            if (Colour[Other] >= 0) continue;
            Colour[Other] = 1 - Colour[Current];
            Queue.push_back(Other);
        }
    }
    int FirstCount = 0, SecondCount = 0;
    for (int Assigned : Colour)
    {
        if (Assigned < 0) return false;
        if (Assigned == 0) ++FirstCount;
        else ++SecondCount;
    }
    return FirstCount >= 2 && SecondCount >= 2;
}

NetworkSheetOutcome ConstructNetworkSheet(ModelPartition& Partition, const std::vector<NurbsCurve>& Curves, const NetworkSettings& Settings, const std::vector<std::string>& Names)
{
    NetworkSheetOutcome Outcome;
    Outcome.Network = ConstructNetworkSurface(Curves, Settings, Names);
    if (!Outcome.Network.Refusal.empty()) { Outcome.Sheet.Refusal = Outcome.Network.Refusal; return Outcome; }
    const PanelAssemblyOutcome Assembly = AssemblePanels(Partition, { SurfacePanel{ Outcome.Network.Surface } }, PanelAssemblySettings{}, "Network");
    if (!Assembly.Refusal.empty()) { Outcome.Sheet.Refusal = Assembly.Refusal; return Outcome; }
    Outcome.Sheet.Body  = Assembly.Body;
    Outcome.Sheet.Faces = Assembly.Faces;
    Outcome.Sheet.Roles = { SweepFace{ SweepFaceRole::Side, "Surface", 0 } };
    return Outcome;
}

} // namespace Frontier::GeometrySpace::Parametric
