/*====================================================================================================================================
                                                           CURVEFITTING.CPP
====================================================================================================================================*/
// 🧩 Space-curve fitting: centripetal parameters, faired least squares, foot-point correction, knot refinement

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Surfacing/CurveFitting.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/LinearSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PRIVATE FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double DuplicateGap = 1.0e-12;                                                 // [len] - Consecutive points closer than this merge
    constexpr std::array<double, 4> GaussAbscissae = { -0.8611363115940526, -0.3399810435848563, 0.3399810435848563, 0.8611363115940526 };
    constexpr std::array<double, 4> GaussWeights   = {  0.3478548451374538,  0.6521451548625461, 0.6521451548625461, 0.3478548451374538 };

    // Consecutive duplicates removed; Source maps each kept point back to its input index.
    [[nodiscard]] std::vector<Vector3> MergeDuplicates(const std::vector<Vector3>& Points, std::vector<int>& Source)
    {
        std::vector<Vector3> Kept;
        for (size_t Index = 0; Index < Points.size(); ++Index)
        {
            if (!Kept.empty() && Length(Points[Index] - Kept.back()) <= DuplicateGap) continue;
            Kept.push_back(Points[Index]);
            Source.push_back(static_cast<int>(Index));
        }
        return Kept;
    }

    // 𝑡ₖ ∝ Σ √|Qᵢ − Qᵢ₋₁| (Lee 1989), normalised onto [0, 1].
    [[nodiscard]] std::vector<double> CentripetalParameters(const std::vector<Vector3>& Points)
    {
        std::vector<double> Parameters(Points.size(), 0.0);
        for (size_t Index = 1; Index < Points.size(); ++Index) Parameters[Index] = Parameters[Index - 1] + std::sqrt(Length(Points[Index] - Points[Index - 1]));
        const double Total = Parameters.back();
        for (double& Parameter : Parameters) Parameter /= Total;
        Parameters.back() = 1.0;
        return Parameters;
    }

    [[nodiscard]] std::vector<double> ClampedKnots(int Degree, const std::vector<double>& Interior)
    {
        std::vector<double> Knots(Degree + 1, 0.0);
        Knots.insert(Knots.end(), Interior.begin(), Interior.end());
        Knots.insert(Knots.end(), Degree + 1, 1.0);
        return Knots;
    }

    // 📝 Normal equations for the interior poles P₁ … Pₙ₋₁ (P₀ = Q₀, Pₙ = Qₘ fixed):
    //    (NᵀN + λ·G)·P = Nᵀ·R − λ·G_boundary·P_boundary, Gᵢⱼ = ∫ Nᵢ″ Nⱼ″ d𝑡 (4-point Gauss per span: exact to degree 5).
    [[nodiscard]] std::optional<std::vector<Vector3>> SolvePoles(const std::vector<Vector3>& Points, const std::vector<double>& Parameters, int Degree,
                                                                 const std::vector<double>& Knots, double Fairing)
    {
        const int Count = static_cast<int>(Knots.size()) - Degree - 1;
        std::vector<Vector3> Poles(Count);
        Poles.front() = Points.front();
        Poles.back()  = Points.back();
        const int Unknowns = Count - 2;
        if (Unknowns <= 0) return Poles;
        DenseMatrix Matrix(Unknowns, Unknowns), RightSides(Unknowns, 3);
        auto Interior = [&](int Pole) { return Pole >= 1 && Pole <= Count - 2; };
        for (size_t Point = 0; Point < Points.size(); ++Point)
        {
            const NurbsBasisSample Basis = EvaluateNurbsBasis(Knots, Degree, Count, Parameters[Point]);
            Vector3 Target = Points[Point];
            for (int Row = 0; Row <= Degree; ++Row)
            {
                const int Pole = Basis.First + Row;
                if (!Interior(Pole)) Target = Target - Poles[Pole] * Basis.Values[0][Row];
            }
            for (int Row = 0; Row <= Degree; ++Row)
            {
                const int Pole = Basis.First + Row;
                if (!Interior(Pole)) continue;
                const double Value = Basis.Values[0][Row];
                RightSides.At(Pole - 1, 0) += Value * Target.X;
                RightSides.At(Pole - 1, 1) += Value * Target.Y;
                RightSides.At(Pole - 1, 2) += Value * Target.Z;
                for (int Column = 0; Column <= Degree; ++Column)
                {
                    const int Other = Basis.First + Column;
                    if (Interior(Other)) Matrix.At(Pole - 1, Other - 1) += Value * Basis.Values[0][Column];
                }
            }
        }
        const double λ = Fairing * static_cast<double>(Points.size());
        if (λ > 0.0 && Degree >= 2)
        {
            for (int Span = Degree; Span < Count; ++Span)
            {
                const double Low = Knots[Span], High = Knots[Span + 1];
                if (!(High > Low)) continue;
                for (size_t Abscissa = 0; Abscissa < GaussAbscissae.size(); ++Abscissa)
                {
                    const double Parameter = 0.5 * (Low + High) + 0.5 * (High - Low) * GaussAbscissae[Abscissa];
                    const double Weight    = 0.5 * (High - Low) * GaussWeights[Abscissa] * λ;
                    const NurbsBasisSample Basis = EvaluateNurbsBasis(Knots, Degree, Count, Parameter);
                    for (int Row = 0; Row <= Degree; ++Row)
                    {
                        const int Pole = Basis.First + Row;
                        if (!Interior(Pole)) continue;
                        for (int Column = 0; Column <= Degree; ++Column)
                        {
                            const int    Other = Basis.First + Column;
                            const double Term  = Weight * Basis.Values[2][Row] * Basis.Values[2][Column];
                            if (Interior(Other)) { Matrix.At(Pole - 1, Other - 1) += Term; continue; }
                            RightSides.At(Pole - 1, 0) -= Term * Poles[Other].X;
                            RightSides.At(Pole - 1, 1) -= Term * Poles[Other].Y;
                            RightSides.At(Pole - 1, 2) -= Term * Poles[Other].Z;
                        }
                    }
                }
            }
        }
        const std::optional<DenseMatrix> Solution = SolveDenseSystem(std::move(Matrix), std::move(RightSides));
        if (!Solution) return std::nullopt;
        for (int Pole = 1; Pole <= Count - 2; ++Pole) Poles[Pole] = { Solution->At(Pole - 1, 0), Solution->At(Pole - 1, 1), Solution->At(Pole - 1, 2) };
        return Poles;
    }

    // 📝 End-tangent constrained solve: each interior pole is Fixed + Σ zₐ·Dirₐ (three axes when free, one direction when
    //    P₁ / Pₙ₋₁ is tied to its end point), and the residual + fairing normal equations are written in z.
    struct EndTangents
    {
        std::optional<Vector3> Start = {};                                                   // [-] - d at P₀
        std::optional<Vector3> End   = {};                                                   // [-] - d at Pₙ
    };

    [[nodiscard]] std::optional<std::vector<Vector3>> SolveTangentPoles(const std::vector<Vector3>& Points, const std::vector<double>& Parameters, int Degree,
                                                                        const std::vector<double>& Knots, double Fairing, const EndTangents& Tangents)
    {
        const int Count = static_cast<int>(Knots.size()) - Degree - 1;
        if (Count < 4) return std::nullopt;
        struct PoleForm { Vector3 Fixed = {}; std::vector<std::pair<int, Vector3>> Terms = {}; };
        std::vector<PoleForm> Forms(Count);
        Forms.front().Fixed = Points.front();
        Forms.back().Fixed  = Points.back();
        int Unknowns = 0;
        for (int Pole = 1; Pole <= Count - 2; ++Pole)
        {
            if (Pole == 1 && Tangents.Start) { Forms[Pole].Fixed = Points.front(); Forms[Pole].Terms.push_back({ Unknowns++, *Tangents.Start }); continue; }
            if (Pole == Count - 2 && Tangents.End) { Forms[Pole].Fixed = Points.back(); Forms[Pole].Terms.push_back({ Unknowns++, *Tangents.End }); continue; }
            Forms[Pole].Terms = { { Unknowns, { 1.0, 0.0, 0.0 } }, { Unknowns + 1, { 0.0, 1.0, 0.0 } }, { Unknowns + 2, { 0.0, 0.0, 1.0 } } };
            Unknowns += 3;
        }
        DenseMatrix Matrix(Unknowns, Unknowns), RightSides(Unknowns, 1);
        // One row of the model: Σₖ cₖ·Pₖ ≈ Target with weight w.
        auto Accumulate = [&](const NurbsBasisSample& Basis, int Order, Vector3 Target, double Weight)
        {
            Vector3 Offset = Target * -1.0;
            std::vector<std::pair<int, Vector3>> Jacobian;
            for (int Row = 0; Row <= Degree; ++Row)
            {
                const PoleForm& Form = Forms[Basis.First + Row];
                const double Value = Basis.Values[Order][Row];
                Offset = Offset + Form.Fixed * Value;
                for (const auto& [Unknown, Direction] : Form.Terms) Jacobian.push_back({ Unknown, Direction * Value });
            }
            for (const auto& [First, FirstVector] : Jacobian)
            {
                RightSides.At(First, 0) -= Weight * Dot(FirstVector, Offset);
                for (const auto& [Second, SecondVector] : Jacobian) Matrix.At(First, Second) += Weight * Dot(FirstVector, SecondVector);
            }
        };
        for (size_t Point = 0; Point < Points.size(); ++Point) Accumulate(EvaluateNurbsBasis(Knots, Degree, Count, Parameters[Point]), 0, Points[Point], 1.0);
        const double λ = Fairing * static_cast<double>(Points.size());
        if (λ > 0.0 && Degree >= 2)
        {
            for (int Span = Degree; Span < Count; ++Span)
            {
                const double Low = Knots[Span], High = Knots[Span + 1];
                if (!(High > Low)) continue;
                for (size_t Abscissa = 0; Abscissa < GaussAbscissae.size(); ++Abscissa)
                {
                    const double Parameter = 0.5 * (Low + High) + 0.5 * (High - Low) * GaussAbscissae[Abscissa];
                    Accumulate(EvaluateNurbsBasis(Knots, Degree, Count, Parameter), 2, {}, 0.5 * (High - Low) * GaussWeights[Abscissa] * λ);
                }
            }
        }
        const std::optional<DenseMatrix> Solution = SolveDenseSystem(std::move(Matrix), std::move(RightSides));
        if (!Solution) return std::nullopt;
        std::vector<Vector3> Poles(Count);
        for (int Pole = 0; Pole < Count; ++Pole)
        {
            Poles[Pole] = Forms[Pole].Fixed;
            for (const auto& [Unknown, Direction] : Forms[Pole].Terms) Poles[Pole] = Poles[Pole] + Direction * Solution->At(Unknown, 0);
        }
        return Poles;
    }

    [[nodiscard]] NurbsCurveDefinition MakeDefinition(int Degree, const std::vector<double>& Knots, const std::vector<Vector3>& Poles)
    {
        NurbsCurveDefinition Definition;
        Definition.Degree  = Degree;
        Definition.Knots   = Knots;
        Definition.Poles   = Poles;
        Definition.Weights = std::vector<double>(Poles.size(), 1.0);
        return Definition;
    }

    // Hoschek correction: each interior 𝑡ₖ moves to the foot point of Qₖ (Newton on (C − Q)·C′ = 0), ends stay 0 and 1.
    void CorrectParameters(const NurbsCurveDefinition& Definition, const std::vector<Vector3>& Points, std::vector<double>& Parameters)
    {
        for (size_t Point = 1; Point + 1 < Points.size(); ++Point)
        {
            double Parameter = Parameters[Point];
            for (int Iteration = 0; Iteration < 4; ++Iteration)
            {
                const NurbsCurveDerivatives Sample = EvaluateNurbsCurveDerivatives(Definition, Parameter);
                const Vector3 Gap      = Sample.Position - Points[Point];
                const double  Slope    = Dot(Gap, Sample.First);
                const double  Steepness = Dot(Sample.First, Sample.First) + Dot(Gap, Sample.Second);
                if (!(Steepness > 0.0)) break;
                Parameter = std::clamp(Parameter - Slope / Steepness, 0.0, 1.0);
            }
            Parameters[Point] = Parameter;
        }
    }

    struct FitAttempt
    {
        std::vector<Vector3> Poles      = {};                                                // [len] - Solved poles
        std::vector<double>  Parameters = {};                                                // [-]   - Corrected 𝑡ₖ
        double               Worst      = 0.0;                                               // [len] - max |Qₖ − C(𝑡ₖ)|
    };

    [[nodiscard]] double MeasureWorst(const NurbsCurveDefinition& Definition, const std::vector<Vector3>& Points, const std::vector<double>& Parameters)
    {
        double Worst = 0.0;
        for (size_t Point = 0; Point < Points.size(); ++Point) Worst = std::max(Worst, Length(EvaluateNurbsCurveDerivatives(Definition, Parameters[Point]).Position - Points[Point]));
        return Worst;
    }

    // Solve, then alternate foot-point correction and re-solve (Rounds times) — one fit on fixed knots and λ.
    [[nodiscard]] std::optional<std::vector<Vector3>> SolveAnyPoles(const std::vector<Vector3>& Points, const std::vector<double>& Parameters, int Degree,
                                                                    const std::vector<double>& Knots, double Fairing, const EndTangents& Tangents)
    {
        if (Tangents.Start || Tangents.End) return SolveTangentPoles(Points, Parameters, Degree, Knots, Fairing, Tangents);
        return SolvePoles(Points, Parameters, Degree, Knots, Fairing);
    }

    [[nodiscard]] std::optional<FitAttempt> SolveCorrected(const std::vector<Vector3>& Points, std::vector<double> Parameters, int Degree,
                                                           const std::vector<double>& Knots, double Fairing, int Rounds, const EndTangents& Tangents)
    {
        std::optional<std::vector<Vector3>> Poles = SolveAnyPoles(Points, Parameters, Degree, Knots, Fairing, Tangents);
        if (!Poles) return std::nullopt;
        for (int Round = 0; Round < Rounds; ++Round)
        {
            std::vector<double> Candidate = Parameters;
            CorrectParameters(MakeDefinition(Degree, Knots, *Poles), Points, Candidate);
            // 📝 Foot points must keep the points' order: on a rough curve a point's nearest foot can sit on another hump
            //    (a wiggle), and a scrambled order would fit the scrambled curve — such a round is dropped.
            if (std::adjacent_find(Candidate.begin(), Candidate.end(), std::greater<double>()) != Candidate.end()) break;
            std::optional<std::vector<Vector3>> Corrected = SolveAnyPoles(Points, Candidate, Degree, Knots, Fairing, Tangents);
            if (!Corrected) break;
            Poles      = std::move(Corrected);
            Parameters = std::move(Candidate);
        }
        FitAttempt Attempt;
        Attempt.Poles      = std::move(*Poles);
        Attempt.Parameters = std::move(Parameters);
        // Final foot points on the final curve, so the reported deviation is a true distance.
        const NurbsCurveDefinition Definition = MakeDefinition(Degree, Knots, Attempt.Poles);
        CorrectParameters(Definition, Points, Attempt.Parameters);
        Attempt.Worst = MeasureWorst(Definition, Points, Attempt.Parameters);
        return Attempt;
    }

    void MeasureDeviation(const NurbsCurveDefinition& Definition, const std::vector<Vector3>& Points, const std::vector<double>& Parameters,
                          const std::vector<int>& Source, CurveFitOutcome& Outcome)
    {
        Outcome.WorstDeviation = 0.0;
        Outcome.MeanDeviation  = 0.0;
        for (size_t Point = 0; Point < Points.size(); ++Point)
        {
            const double Deviation = Length(EvaluateNurbsCurveDerivatives(Definition, Parameters[Point]).Position - Points[Point]);
            Outcome.MeanDeviation += Deviation;
            if (Deviation >= Outcome.WorstDeviation) { Outcome.WorstDeviation = Deviation; Outcome.WorstPoint = Source[Point]; }
        }
        Outcome.MeanDeviation /= static_cast<double>(Points.size());
    }

    // Interior maxima of κ = |C′ × C″| / |C′|³ on 400 samples, ignoring ripples below 10⁻⁶ of the largest κ.
    [[nodiscard]] int CountExtrema(const NurbsCurveDefinition& Definition)
    {
        constexpr int Samples = 400;
        std::vector<double> Curvature(Samples + 1, 0.0);
        double Largest = 0.0;
        for (int Index = 0; Index <= Samples; ++Index)
        {
            const NurbsCurveDerivatives Sample = EvaluateNurbsCurveDerivatives(Definition, static_cast<double>(Index) / Samples);
            const double Speed = Length(Sample.First);
            Curvature[Index] = Speed > 0.0 ? Length(Cross(Sample.First, Sample.Second)) / (Speed * Speed * Speed) : 0.0;
            Largest = std::max(Largest, Curvature[Index]);
        }
        int Extrema = 0;
        for (int Index = 1; Index < Samples; ++Index)
        {
            if (Curvature[Index] > Curvature[Index - 1] && Curvature[Index] >= Curvature[Index + 1] && Curvature[Index] > 1.0e-6 * Largest) ++Extrema;
        }
        return Extrema;
    }

    void Finish(CurveFitOutcome& Outcome, const NurbsCurveDefinition& Definition)
    {
        Outcome.Curve            = NurbsCurve{ std::make_shared<NurbsCurveDefinition>(Definition) };
        Outcome.Spans            = static_cast<int>(Definition.Poles.size()) - Definition.Degree;
        Outcome.Extrema = CountExtrema(Definition);
    }

    [[nodiscard]] std::string FormatLength(double Value)
    {
        char Text[48];
        std::snprintf(Text, sizeof Text, "%.3g mm", Value * 1000.0);
        return Text;
    }

    // Best plane (centre, unit normal): eigenvector of the smallest eigenvalue of the point covariance (cyclic Jacobi).
    // Normals within 10⁻¹² of a coordinate axis snap onto it, so axis-aligned sections stay exactly axis-aligned.
    [[nodiscard]] std::optional<std::pair<Vector3, Vector3>> FitBestPlane(const std::vector<Vector3>& Points, std::optional<Vector3> Within)
    {
        Vector3 Centre = {};
        for (const Vector3& Point : Points) Centre = Centre + Point * (1.0 / Points.size());
        double Matrix[3][3] = {}, Vectors[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
        for (const Vector3& Point : Points)
        {
            const double Offset[3] = { Point.X - Centre.X, Point.Y - Centre.Y, Point.Z - Centre.Z };
            for (int Row = 0; Row < 3; ++Row) for (int Column = 0; Column < 3; ++Column) Matrix[Row][Column] += Offset[Row] * Offset[Column];
        }
        if (Within)
        {
            // The plane must contain direction d (a mirror tangent): its normal lies in span{e₁, e₂} ⊥ d — a 2 × 2
            // eigenproblem. e₁ = d × r, e₂ = d × e₁ keep a zero component exactly where d is a coordinate axis.
            const Vector3 Direction = *Within * (1.0 / Length(*Within));
            const Vector3 Reference = std::abs(Direction.X) < 0.9 ? Vector3{ 1.0, 0.0, 0.0 } : Vector3{ 0.0, 1.0, 0.0 };
            Vector3 First = Cross(Direction, Reference);
            First = First * (1.0 / Length(First));
            const Vector3 Second = Cross(Direction, First);
            auto Quadratic = [&](Vector3 A, Vector3 B)
            {
                const double VA[3] = { A.X, A.Y, A.Z }, VB[3] = { B.X, B.Y, B.Z };
                double Sum = 0.0;
                for (int Row = 0; Row < 3; ++Row) for (int Column = 0; Column < 3; ++Column) Sum += VA[Row] * Matrix[Row][Column] * VB[Column];
                return Sum;
            };
            const double A = Quadratic(First, First), B = Quadratic(First, Second), C = Quadratic(Second, Second);
            const double Smallest = 0.5 * (A + C) - std::sqrt(0.25 * (A - C) * (A - C) + B * B);
            // Eigenvector of [[A, B], [B, C]] for the smallest eigenvalue.
            const double CoefficientFirst = std::abs(B) > 1.0e-300 ? B : (A <= C ? 1.0 : 0.0);
            const double CoefficientSecond = std::abs(B) > 1.0e-300 ? Smallest - A : (A <= C ? 0.0 : 1.0);
            Vector3 Normal = First * CoefficientFirst + Second * CoefficientSecond;
            if (!(Length(Normal) > 0.0)) return std::nullopt;
            Normal = Normal * (1.0 / Length(Normal));
            for (Vector3 Axis : { Vector3{ 1.0, 0.0, 0.0 }, Vector3{ 0.0, 1.0, 0.0 }, Vector3{ 0.0, 0.0, 1.0 } })
            {
                if (Length(Cross(Normal, Axis)) <= 1.0e-12) Normal = Axis * (Dot(Normal, Axis) < 0.0 ? -1.0 : 1.0);
            }
            if (!(Quadratic(Normal, Normal) < std::max(A, C))) return std::nullopt;          // Degenerate spread in the plane
            return std::pair{ Centre, Normal };
        }
        for (int Sweep = 0; Sweep < 50; ++Sweep)
        {
            const double OffDiagonal = std::abs(Matrix[0][1]) + std::abs(Matrix[0][2]) + std::abs(Matrix[1][2]);
            if (OffDiagonal < 1.0e-30) break;
            for (int P = 0; P < 2; ++P)
            {
                for (int Q = P + 1; Q < 3; ++Q)
                {
                    if (std::abs(Matrix[P][Q]) < 1.0e-300) continue;
                    const double Theta = 0.5 * std::atan2(2.0 * Matrix[P][Q], Matrix[Q][Q] - Matrix[P][P]);
                    const double C = std::cos(Theta), S = std::sin(Theta);
                    for (int K = 0; K < 3; ++K)
                    {
                        const double KP = Matrix[K][P], KQ = Matrix[K][Q];
                        Matrix[K][P] = C * KP - S * KQ;
                        Matrix[K][Q] = S * KP + C * KQ;
                    }
                    for (int K = 0; K < 3; ++K)
                    {
                        const double PK = Matrix[P][K], QK = Matrix[Q][K];
                        Matrix[P][K] = C * PK - S * QK;
                        Matrix[Q][K] = S * PK + C * QK;
                    }
                    for (int K = 0; K < 3; ++K)
                    {
                        const double KP = Vectors[K][P], KQ = Vectors[K][Q];
                        Vectors[K][P] = C * KP - S * KQ;
                        Vectors[K][Q] = S * KP + C * KQ;
                    }
                }
            }
        }
        int Smallest = 0;
        for (int Index = 1; Index < 3; ++Index) if (Matrix[Index][Index] < Matrix[Smallest][Smallest]) Smallest = Index;
        double Eigenvalues[3] = { Matrix[0][0], Matrix[1][1], Matrix[2][2] };
        std::sort(Eigenvalues, Eigenvalues + 3);
        if (!(Eigenvalues[1] > 1.0e-24 * Eigenvalues[2])) return std::nullopt;               // Collinear: two vanishing spreads
        Vector3 Normal = { Vectors[0][Smallest], Vectors[1][Smallest], Vectors[2][Smallest] };
        Normal = Normal * (1.0 / Length(Normal));
        for (Vector3 Axis : { Vector3{ 1.0, 0.0, 0.0 }, Vector3{ 0.0, 1.0, 0.0 }, Vector3{ 0.0, 0.0, 1.0 } })
        {
            if (Length(Cross(Normal, Axis)) <= 1.0e-12) Normal = Axis * (Dot(Normal, Axis) < 0.0 ? -1.0 : 1.0);
        }
        return std::pair{ Centre, Normal };
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

CurveFitOutcome FitSpaceCurve(const std::vector<Vector3>& Points, const CurveFitSettings& Settings)
{
    CurveFitOutcome Outcome;
    std::vector<int> Source;
    std::vector<Vector3> Kept = MergeDuplicates(Points, Source);
    if (Kept.size() < 2) { Outcome.Refusal = "Fit: fewer than 2 distinct points"; return Outcome; }
    if (!(Settings.Tolerance > 0.0)) { Outcome.Refusal = "Fit: tolerance must be positive"; return Outcome; }
    const int Degree = std::clamp(Settings.Degree, 1, std::min(NurbsDegreeLimit, static_cast<int>(Kept.size()) - 1));
    if (Settings.PlanarCondition)
    {
        if (Kept.size() < 3) { Outcome.Refusal = "Fit: a planar curve needs at least 3 distinct points"; return Outcome; }
        const std::optional<std::pair<Vector3, Vector3>> Plane = FitBestPlane(Kept, Settings.StartTangent ? Settings.StartTangent : Settings.EndTangent);
        if (!Plane) { Outcome.Refusal = "Fit: the points are collinear (no plane)"; return Outcome; }
        const auto& [Centre, Normal] = *Plane;
        for (const Vector3& Point : Kept) Outcome.PlanarOffset = std::max(Outcome.PlanarOffset, std::abs(Dot(Point - Centre, Normal)));
        if (Outcome.PlanarOffset > Settings.Tolerance)
        {
            Outcome.Refusal = "Fit: the points are " + FormatLength(Outcome.PlanarOffset) + " off their best plane (tolerance " + FormatLength(Settings.Tolerance) + ")";
            return Outcome;
        }
        for (Vector3& Point : Kept) Point = Point - Normal * Dot(Point - Centre, Normal);
        Outcome.PlaneNormal = Normal;
    }
    EndTangents Tangents;
    auto Unit = [](const std::optional<Vector3>& Direction) -> std::optional<Vector3>
    {
        if (!Direction || !(Length(*Direction) > 0.0)) return std::nullopt;
        return *Direction * (1.0 / Length(*Direction));
    };
    Tangents.Start = Unit(Settings.StartTangent);
    Tangents.End   = Unit(Settings.EndTangent);
    if ((Tangents.Start || Tangents.End) && Degree < 2) { Outcome.Refusal = "Fit: end tangents need degree ≥ 2 (at least 3 points)"; return Outcome; }
    std::vector<double> Parameters = CentripetalParameters(Kept);
    std::vector<double> Interior;
    // Tied end poles need P₁ and Pₙ₋₁ distinct from the ends and from each other: at least 4 poles from the start.
    while ((Tangents.Start || Tangents.End) && Degree + 1 + static_cast<int>(Interior.size()) < 4)
    {
        Interior.push_back((Interior.size() + 1.0) / (5.0 - Degree));
    }
    for (;;)
    {
        // ① Faired solve on the current knots (with foot-point correction); accept when every point is within tolerance.
        const std::vector<double> Knots = ClampedKnots(Degree, Interior);
        std::optional<FitAttempt> Faired = SolveCorrected(Kept, Parameters, Degree, Knots, Settings.Fairing, Settings.CorrectionRounds, Tangents);
        if (!Faired) { Outcome.Refusal = "Fit: singular system at " + std::to_string(Interior.size() + 1) + " spans (too few points per span)"; return Outcome; }
        auto Accept = [&](const FitAttempt& Attempt, double Fairing)
        {
            const NurbsCurveDefinition Definition = MakeDefinition(Degree, Knots, Attempt.Poles);
            MeasureDeviation(Definition, Kept, Attempt.Parameters, Source, Outcome);
            Outcome.Parameters.assign(Points.size(), 0.0);
            size_t KeptIndex = 0;                                                            // Merged duplicates share their survivor's 𝑡
            for (size_t Index = 0; Index < Points.size(); ++Index)
            {
                if (KeptIndex + 1 < Kept.size() && Source[KeptIndex + 1] == static_cast<int>(Index)) ++KeptIndex;
                Outcome.Parameters[Index] = Attempt.Parameters[KeptIndex];
            }
            Outcome.Fairing = Fairing;
            Finish(Outcome, Definition);
            return Outcome;
        };
        if (Faired->Worst <= Settings.Tolerance) return Accept(*Faired, Settings.Fairing);

        // ② Fairing must never cost the tolerance: when the unfaired fit passes on these knots, λ drops by decades until
        //    it passes too (the fairest curve that still honours the tolerance); knots are added only when λ = 0 fails.
        std::optional<FitAttempt> Plain = Settings.Fairing > 0.0 ? SolveCorrected(Kept, Parameters, Degree, Knots, 0.0, Settings.CorrectionRounds, Tangents) : Faired;
        if (Plain && Plain->Worst <= Settings.Tolerance)
        {
            for (double Fairing = 0.1 * Settings.Fairing; Fairing >= 1.0e-8 * Settings.Fairing; Fairing *= 0.1)
            {
                const std::optional<FitAttempt> Lighter = SolveCorrected(Kept, Parameters, Degree, Knots, Fairing, Settings.CorrectionRounds, Tangents);
                if (Lighter && Lighter->Worst <= Settings.Tolerance) return Accept(*Lighter, Fairing);
            }
            return Accept(*Plain, 0.0);
        }
        // 📝 The closer attempt guides the split and carries its foot points on: an unfaired solve can swing wild in spans
        //    holding few points, and its corrupted parameters would derail every later round (dense wrapped wiggles).
        const FitAttempt& Guide = Plain && Plain->Worst <= Faired->Worst ? *Plain : *Faired;
        const NurbsCurveDefinition Definition = MakeDefinition(Degree, Knots, Guide.Poles);
        MeasureDeviation(Definition, Kept, Guide.Parameters, Source, Outcome);
        if (std::adjacent_find(Guide.Parameters.begin(), Guide.Parameters.end(), std::greater<double>()) == Guide.Parameters.end()) Parameters = Guide.Parameters;

        // ③ Split every span holding a point beyond tolerance at its middle.
        std::set<int> Offending;
        for (size_t Point = 0; Point < Kept.size(); ++Point)
        {
            if (Length(EvaluateNurbsCurveDerivatives(Definition, Parameters[Point]).Position - Kept[Point]) <= Settings.Tolerance) continue;
            const int Span = static_cast<int>(std::upper_bound(Knots.begin() + Degree, Knots.end() - Degree - 1, Parameters[Point]) - Knots.begin()) - 1;
            Offending.insert(std::clamp(Span, Degree, static_cast<int>(Knots.size()) - Degree - 2));
        }
        const int Spans = static_cast<int>(Interior.size()) + 1;
        if (Spans + static_cast<int>(Offending.size()) > Settings.SpanLimit || Degree + Spans + static_cast<int>(Offending.size()) > static_cast<int>(Kept.size()))
        {
            Outcome.Refusal = "Fit: point " + std::to_string(Outcome.WorstPoint) + " stays " + FormatLength(Outcome.WorstDeviation) + " away at "
                            + std::to_string(Spans) + " spans (limit " + std::to_string(Settings.SpanLimit) + ", tolerance " + FormatLength(Settings.Tolerance) + ")";
            return Outcome;
        }
        for (int Span : Offending) Interior.push_back(0.5 * (Knots[Span] + Knots[Span + 1]));
        std::sort(Interior.begin(), Interior.end());
    }
}

CurveFitOutcome FitSpaceCurveOnKnots(const std::vector<Vector3>& Points, const std::vector<double>& Parameters, int Degree,
                                     const std::vector<double>& Knots, double Fairing)
{
    CurveFitOutcome Outcome;
    if (Points.size() < 2 || Parameters.size() != Points.size()) { Outcome.Refusal = "Fit: points and parameters differ in count"; return Outcome; }
    const std::optional<std::vector<Vector3>> Poles = SolvePoles(Points, Parameters, Degree, Knots, Fairing);
    if (!Poles) { Outcome.Refusal = "Fit: singular system"; return Outcome; }
    const NurbsCurveDefinition Definition = MakeDefinition(Degree, Knots, *Poles);
    std::vector<int> Source(Points.size());
    for (size_t Index = 0; Index < Points.size(); ++Index) Source[Index] = static_cast<int>(Index);
    MeasureDeviation(Definition, Points, Parameters, Source, Outcome);
    Outcome.Parameters = Parameters;
    Finish(Outcome, Definition);
    return Outcome;
}

std::vector<int> FindCurveCorners(const std::vector<Vector3>& Points, double Angle)
{
    std::vector<int> Corners;
    for (size_t Index = 1; Index + 1 < Points.size(); ++Index)
    {
        const Vector3 Before = Points[Index] - Points[Index - 1], After = Points[Index + 1] - Points[Index];
        const double  Lengths = Length(Before) * Length(After);
        if (!(Lengths > 0.0)) continue;
        const double Turning = std::acos(std::clamp(Dot(Before, After) / Lengths, -1.0, 1.0));
        if (Turning > Angle) Corners.push_back(static_cast<int>(Index));
    }
    return Corners;
}

std::vector<CurveFitOutcome> FitSpaceCurveRuns(const std::vector<Vector3>& Points, const CurveFitSettings& Settings)
{
    std::vector<int> Cuts = FindCurveCorners(Points, Settings.CornerAngle);
    Cuts.insert(Cuts.begin(), 0);
    Cuts.push_back(static_cast<int>(Points.size()) - 1);
    std::vector<CurveFitOutcome> Runs;
    for (size_t Index = 0; Index + 1 < Cuts.size(); ++Index)
    {
        const std::vector<Vector3> Run(Points.begin() + Cuts[Index], Points.begin() + Cuts[Index + 1] + 1);
        CurveFitOutcome Outcome = FitSpaceCurve(Run, Settings);
        if (Outcome.WorstPoint >= 0) Outcome.WorstPoint += Cuts[Index];
        Runs.push_back(std::move(Outcome));
    }
    return Runs;
}

}
