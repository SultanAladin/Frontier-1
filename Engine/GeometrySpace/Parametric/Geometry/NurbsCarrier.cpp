/*====================================================================================================================================
                                                        NURBSCARRIER.CPP
====================================================================================================================================*/
// 🧩 Rational B-spline curves and surfaces as first-class carriers: exact rational derivatives, inversion, iso-curves, affine images

#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"

#include <algorithm>
#include <array>
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
    constexpr int    BasisWidth       = NurbsDegreeLimit + 1;                                // [-]   - Largest p + 1
    constexpr int    NewtonLimit      = 60;                                                  // [-]   - Inversion iterations
    constexpr double ClosureSlack     = 1.0e-12;                                             // [-]   - Relative pole coincidence for closure
    constexpr double SeamSlack        = 1.0e-9;                                              // [-]   - Relative ∂P agreement across a seam
    constexpr double QuarterTurn      = 0.5 * std::numbers::pi;                              // [rad] - Largest rational-quadratic piece
    constexpr double FullTurn         = 2.0 * std::numbers::pi;                              // [rad] - 2π

    // Basis values and derivatives Nᵢ⁽ᵏ⁾ for k ≤ 2: Rows[k][r] is the k-th derivative of N_{Span−p+r, p}.
    using BasisRows = std::array<std::array<double, BasisWidth>, 3>;

    // A2.1 — knot span containing τ (clamped vector; ends map to the first / last non-empty span, outside extrapolates).
    [[nodiscard]] int FindSpan(const std::vector<double>& Knots, int Degree, int Count, double τ) noexcept
    {
        const int Last = Count - 1;                                                          // [-] - n
        if (τ >= Knots[Last + 1])
        {
            int Span = Last;
            while (Span > Degree && Knots[Span] >= Knots[Span + 1]) --Span;                  // Skip empty spans at the end
            return Span;
        }
        if (!std::isfinite(τ) || τ <= Knots[Degree])
        {
            int Span = Degree;
            while (Span < Last && Knots[Span] >= Knots[Span + 1]) ++Span;
            return Span;
        }
        int Low = Degree, High = Last + 1, Middle = (Low + High) / 2;
        while (High - Low > 1 && (τ < Knots[Middle] || τ >= Knots[Middle + 1]))
        {
            if (τ < Knots[Middle]) High = Middle; else Low = Middle;
            Middle = (Low + High) / 2;
        }
        return Middle;
    }

    // A2.3 — basis functions and their derivatives up to Order (≤ 2), Rows[k][r] as above.
    void ComputeBasisRows(const std::vector<double>& Knots, int Degree, int Span, double τ, int Order, BasisRows& Rows) noexcept
    {
        double Table[BasisWidth][BasisWidth];                                                // [-] - Ndu of A2.3
        double Left[BasisWidth], Right[BasisWidth];                                          // [-] - τ − uⱼ, uⱼ − τ
        double Coefficient[2][BasisWidth];                                                   // [-] - a_{k,j} rows
        Table[0][0] = 1.0;
        for (int J = 1; J <= Degree; ++J)
        {
            Left[J]  = τ - Knots[Span + 1 - J];
            Right[J] = Knots[Span + J] - τ;
            double Saved = 0.0;
            for (int R = 0; R < J; ++R)
            {
                Table[J][R] = Right[R + 1] + Left[J - R];
                const double Temporary = Table[R][J - 1] / Table[J][R];
                Table[R][J] = Saved + Right[R + 1] * Temporary;
                Saved       = Left[J - R] * Temporary;
            }
            Table[J][J] = Saved;
        }
        for (int J = 0; J <= Degree; ++J) Rows[0][J] = Table[J][Degree];
        for (int K = 1; K <= 2; ++K) for (int J = 0; J <= Degree; ++J) Rows[K][J] = 0.0;
        const int Highest = std::min(Order, Degree);                                         // [-] - Derivatives above p vanish
        for (int R = 0; R <= Degree; ++R)
        {
            int Current = 0, Next = 1;
            Coefficient[0][0] = 1.0;
            for (int K = 1; K <= Highest; ++K)
            {
                double Value = 0.0;
                const int RK = R - K, PK = Degree - K;
                if (R >= K)
                {
                    Coefficient[Next][0] = Coefficient[Current][0] / Table[PK + 1][RK];
                    Value = Coefficient[Next][0] * Table[RK][PK];
                }
                const int First = RK >= -1 ? 1 : -RK;
                const int Final = (R - 1 <= PK) ? K - 1 : Degree - R;
                for (int J = First; J <= Final; ++J)
                {
                    Coefficient[Next][J] = (Coefficient[Current][J] - Coefficient[Current][J - 1]) / Table[PK + 1][RK + J];
                    Value += Coefficient[Next][J] * Table[RK + J][PK];
                }
                if (R <= PK)
                {
                    Coefficient[Next][K] = -Coefficient[Current][K - 1] / Table[PK + 1][R];
                    Value += Coefficient[Next][K] * Table[R][PK];
                }
                Rows[K][R] = Value;
                std::swap(Current, Next);
            }
        }
        double Factor = Degree;                                                              // [-] - p!/(p − k)!
        for (int K = 1; K <= Highest; ++K)
        {
            for (int J = 0; J <= Degree; ++J) Rows[K][J] *= Factor;
            Factor *= Degree - K;
        }
    }

    [[nodiscard]] double FoldInto(double Value, double Low, double Period) noexcept
    {
        double Folded = Low + std::fmod(Value - Low, Period);
        if (Folded < Low) Folded += Period;
        return Folded;
    }

    [[nodiscard]] double FoldNearest(double Value, double Reference, double Period) noexcept
    {
        return Value + Period * std::round((Reference - Value) / Period);
    }

    // Clamped, non-decreasing knots with end multiplicity p + 1 and matching counts.
    [[nodiscard]] bool ValidKnots(const std::vector<double>& Knots, int Degree, int Count) noexcept
    {
        if (Degree < 1 || Degree > NurbsDegreeLimit || Count < Degree + 1) return false;
        if (static_cast<int>(Knots.size()) != Count + Degree + 1) return false;
        for (size_t Index = 1; Index < Knots.size(); ++Index) if (!(Knots[Index] >= Knots[Index - 1])) return false;
        for (int Index = 1; Index <= Degree; ++Index)
            if (Knots[Index] != Knots[0] || Knots[Knots.size() - 1 - Index] != Knots.back()) return false;
        return Knots.back() > Knots.front();
    }

    // Distinct interior knots of a clamped vector, plus the seam when periodic, repeated over (Low, High).
    [[nodiscard]] std::vector<double> CollectBreaks(const std::vector<double>& Knots, int Degree, double Period, double Low, double High)
    {
        std::vector<double> Interior;
        const double DomainLow = Knots[Degree], DomainHigh = Knots[Knots.size() - 1 - Degree];
        for (size_t Index = Degree + 1; Index + Degree + 1 < Knots.size(); ++Index)
            if (Knots[Index] > DomainLow && Knots[Index] < DomainHigh && (Interior.empty() || Knots[Index] > Interior.back())) Interior.push_back(Knots[Index]);
        if (Period > 0.0) Interior.push_back(DomainLow);
        const double Lowest = std::min(Low, High), Highest = std::max(Low, High);
        std::vector<double> Breaks;
        for (double Knot : Interior)
        {
            if (Period > 0.0)
            {
                for (double Value = Knot + Period * std::ceil((Lowest - Knot) / Period); Value < Highest; Value += Period)
                    if (Value > Lowest) Breaks.push_back(Value);
            }
            else if (Knot > Lowest && Knot < Highest) Breaks.push_back(Knot);
        }
        std::sort(Breaks.begin(), Breaks.end());
        return Breaks;
    }

    // Rational arc of a conic from angle Low to High in ≤ π/2 pieces, knots spread over [Low, High] at the joints.
    struct RationalArc
    {
        std::vector<double>  Knots   = {};
        std::vector<Vector3> Poles   = {};
        std::vector<double>  Weights = {};
    };

    [[nodiscard]] RationalArc ConstructRationalArc(const ConicForm& Conic, double Low, double High)
    {
        RationalArc Arc;
        const int    PieceCount = std::max(1, static_cast<int>(std::ceil((High - Low) / QuarterTurn - 1.0e-12)));
        const double Sweep      = (High - Low) / PieceCount;                                 // [rad] - Δ per piece
        const double Weight     = std::cos(0.5 * Sweep);                                     // [-]   - Middle weight
        const auto   Point      = [&](double Angle, double Scale) { return Conic.Centre + (Conic.CosineAxis * std::cos(Angle) + Conic.SineAxis * std::sin(Angle)) * Scale; };
        Arc.Knots = { Low, Low, Low };
        for (int Piece = 0; Piece < PieceCount; ++Piece)
        {
            const double Start = Low + Piece * Sweep;                                        // [rad] - Piece start
            if (Piece == 0) { Arc.Poles.push_back(Point(Start, 1.0)); Arc.Weights.push_back(1.0); }
            Arc.Poles.push_back(Point(Start + 0.5 * Sweep, 1.0 / Weight));
            Arc.Weights.push_back(Weight);
            const bool LastPiece = Piece + 1 == PieceCount;
            Arc.Poles.push_back(Point(LastPiece ? High : Start + Sweep, 1.0));
            Arc.Weights.push_back(1.0);
            const double Joint = LastPiece ? High : Start + Sweep;                           // [-] - Knot at the joint
            Arc.Knots.push_back(Joint);
            Arc.Knots.push_back(Joint);
        }
        Arc.Knots.push_back(High);
        return Arc;
    }

    // Seed lattice and extent of a surface (rebuilt whenever the poles change).
    void PrepareSurface(NurbsSurfaceDefinition& Definition)
    {
        Vector3 Low{ INFINITY, INFINITY, INFINITY }, High{ -INFINITY, -INFINITY, -INFINITY };
        for (const Vector3& Pole : Definition.Poles)
        {
            Low  = { std::min(Low.X, Pole.X), std::min(Low.Y, Pole.Y), std::min(Low.Z, Pole.Z) };
            High = { std::max(High.X, Pole.X), std::max(High.Y, Pole.Y), std::max(High.Z, Pole.Z) };
        }
        Definition.Extent = std::max(Length(High - Low), 1.0e-300);
        const NurbsDomain U = MeasureNurbsSurfaceDomain(Definition, true), V = MeasureNurbsSurfaceDomain(Definition, false);

        // 📝 Periodic when the end pole rows coincide (weights too) and ∂P agrees across the seam at sampled rows.
        const auto Pole   = [&](int I, int J) { return Definition.Poles[I * Definition.CountV + J]; };
        const auto Weight = [&](int I, int J) { return Definition.Weights[I * Definition.CountV + J]; };
        const double Slack = ClosureSlack * Definition.Extent;                              // [len] - Coincidence slack
        const auto SeamDerivativesAgree = [&](bool AlongU)
        {
            for (int Sample = 0; Sample <= 4; ++Sample)
            {
                const double Across = AlongU ? V.Low + (V.High - V.Low) * Sample / 4.0 : U.Low + (U.High - U.Low) * Sample / 4.0;
                const NurbsSurfaceDerivatives Start = EvaluateNurbsSurfaceDerivatives(Definition, AlongU ? Vector2{ U.Low, Across } : Vector2{ Across, V.Low });
                const NurbsSurfaceDerivatives End   = EvaluateNurbsSurfaceDerivatives(Definition, AlongU ? Vector2{ U.High, Across } : Vector2{ Across, V.High });
                const Vector3 First = AlongU ? Start.U : Start.V, Last = AlongU ? End.U : End.V;
                if (Length(First - Last) > SeamSlack * std::max({ Length(First), Length(Last), Definition.Extent * 1.0e-12 })) return false;
            }
            return true;
        };
        bool ClosedU = true, ClosedV = true;
        for (int J = 0; J < Definition.CountV && ClosedU; ++J)
            ClosedU = Distance(Pole(0, J), Pole(Definition.CountU - 1, J)) <= Slack && std::fabs(Weight(0, J) - Weight(Definition.CountU - 1, J)) <= ClosureSlack * Weight(0, J);
        for (int I = 0; I < Definition.CountU && ClosedV; ++I)
            ClosedV = Distance(Pole(I, 0), Pole(I, Definition.CountV - 1)) <= Slack && std::fabs(Weight(I, 0) - Weight(I, Definition.CountV - 1)) <= ClosureSlack * Weight(I, 0);
        Definition.PeriodU = ClosedU && SeamDerivativesAgree(true) ? U.High - U.Low : 0.0;
        Definition.PeriodV = ClosedV && SeamDerivativesAgree(false) ? V.High - V.Low : 0.0;

        // Seed lattice: 4 samples per pole interval, 9 … 65 per direction (knot spans resolved at least twice).
        Definition.SeedCountU = std::clamp(4 * (Definition.CountU - 1) + 1, 9, 65);
        Definition.SeedCountV = std::clamp(4 * (Definition.CountV - 1) + 1, 9, 65);
        Definition.Seeds.resize(static_cast<size_t>(Definition.SeedCountU) * Definition.SeedCountV);
        for (int I = 0; I < Definition.SeedCountU; ++I)
            for (int J = 0; J < Definition.SeedCountV; ++J)
            {
                const Vector2 Parameter{ U.Low + (U.High - U.Low) * I / (Definition.SeedCountU - 1.0), V.Low + (V.High - V.Low) * J / (Definition.SeedCountV - 1.0) };
                Definition.Seeds[I * Definition.SeedCountV + J] = EvaluateNurbsSurfaceDerivatives(Definition, Parameter).Position;
            }
    }

    [[nodiscard]] double MeasureCurvePeriod(const NurbsCurveDefinition& Definition)
    {
        const NurbsDomain Domain = MeasureNurbsCurveDomain(Definition);
        double Extent = 0.0;
        for (const Vector3& Pole : Definition.Poles) Extent = std::max(Extent, Distance(Pole, Definition.Poles.front()));
        if (Distance(Definition.Poles.front(), Definition.Poles.back()) > ClosureSlack * std::max(Extent, 1.0e-300)) return 0.0;
        if (std::fabs(Definition.Weights.front() - Definition.Weights.back()) > ClosureSlack * Definition.Weights.front()) return 0.0;
        const Vector3 First = EvaluateNurbsCurveDerivatives(Definition, Domain.Low).First;
        const Vector3 Last  = EvaluateNurbsCurveDerivatives(Definition, Domain.High).First;
        return Length(First - Last) <= SeamSlack * std::max(Length(First), Length(Last)) ? Domain.High - Domain.Low : 0.0;
    }

    // Surface derivatives clamped into the domain (periodic directions folded) for inversion.
    [[nodiscard]] Vector2 ConfineParameter(const NurbsSurfaceDefinition& Definition, Vector2 Parameter) noexcept
    {
        const NurbsDomain U = MeasureNurbsSurfaceDomain(Definition, true), V = MeasureNurbsSurfaceDomain(Definition, false);
        Parameter.X = Definition.PeriodU > 0.0 ? FoldInto(Parameter.X, U.Low, Definition.PeriodU) : std::clamp(Parameter.X, U.Low, U.High);
        Parameter.Y = Definition.PeriodV > 0.0 ? FoldInto(Parameter.Y, V.Low, Definition.PeriodV) : std::clamp(Parameter.Y, V.Low, V.High);
        return Parameter;
    }

    // Newton on ½|S − X|² from a seed; a coordinate held at its bound by an outward step continues as 1D Newton.
    // ⚠️ Settle (local inversion only): Newton converges quadratically, so one more iteration after a 3D step of
    //    ≤ 10⁻⁹·extent lands at rounding and the loop stops there. The global search keeps the full schedule (its starts
    //    are far, and an early exit there once cost accuracy); an off-surface target never meets its 10⁻¹⁶ floor.
    [[nodiscard]] Vector2 PolishSurfaceFoot(const NurbsSurfaceDefinition& Definition, Vector3 Target, Vector2 Parameter, bool Settle = false) noexcept
    {
        const NurbsDomain U = MeasureNurbsSurfaceDomain(Definition, true), V = MeasureNurbsSurfaceDomain(Definition, false);
        const double      Floor = 1.0e-16 * Definition.Extent;                              // [len] - Step that no longer moves
        bool              Final = false;                                                     // [-]   - Settle: this is the last step
        for (int Iteration = 0; Iteration < NewtonLimit; ++Iteration)
        {
            const NurbsSurfaceDerivatives S = EvaluateNurbsSurfaceDerivatives(Definition, Parameter);
            const Vector3 Residual = S.Position - Target;                                    // [len] - S − X
            const double  GradientU = Dot(S.U, Residual), GradientV = Dot(S.V, Residual);
            double A = Dot(S.U, S.U) + Dot(S.UU, Residual), B = Dot(S.U, S.V) + Dot(S.UV, Residual), D = Dot(S.V, S.V) + Dot(S.VV, Residual);
            const double Determinant = A * D - B * B;
            double StepU = 0.0, StepV = 0.0;
            if (std::fabs(Determinant) > 1.0e-300 && A > 0.0 && D > 0.0 && Determinant > 0.0)
            {
                StepU = (-GradientU * D + GradientV * B) / Determinant;
                StepV = (-A * GradientV + B * GradientU) / Determinant;
            }
            else
            {
                // Indefinite Hessian (far from the foot, or a collapsed row): Gauss–Newton with the first fundamental form.
                A = Dot(S.U, S.U); B = Dot(S.U, S.V); D = Dot(S.V, S.V);
                const double Gram = A * D - B * B;
                if (Gram > 1.0e-30 * A * D)
                {
                    StepU = (-GradientU * D + GradientV * B) / Gram;
                    StepV = (-A * GradientV + B * GradientU) / Gram;
                }
                else if (A > 1.0e-300) StepU = -GradientU / A;
                else if (D > 1.0e-300) StepV = -GradientV / D;
            }
            // Bounds: hold a coordinate whose step leaves the domain and redo the other one alone.
            const bool HoldU = Definition.PeriodU <= 0.0 && ((Parameter.X <= U.Low && StepU < 0.0) || (Parameter.X >= U.High && StepU > 0.0));
            const bool HoldV = Definition.PeriodV <= 0.0 && ((Parameter.Y <= V.Low && StepV < 0.0) || (Parameter.Y >= V.High && StepV > 0.0));
            if (HoldU) { StepU = 0.0; const double Curve = Dot(S.V, S.V) + Dot(S.VV, Residual); StepV = HoldV ? 0.0 : (Curve > 0.0 ? -GradientV / Curve : -GradientV / std::max(Dot(S.V, S.V), 1.0e-300)); }
            if (HoldV && !HoldU) { StepV = 0.0; const double Curve = Dot(S.U, S.U) + Dot(S.UU, Residual); StepU = Curve > 0.0 ? -GradientU / Curve : -GradientU / std::max(Dot(S.U, S.U), 1.0e-300); }
            // Damping: never accept a step that increases the distance (halve up to 12 times).
            const double Current = Dot(Residual, Residual);
            Vector2 Next = ConfineParameter(Definition, { Parameter.X + StepU, Parameter.Y + StepV });
            double  Scale = 1.0;
            for (int Halving = 0; Halving < 12; ++Halving)
            {
                const Vector3 Offset = EvaluateNurbsSurfaceDerivatives(Definition, Next).Position - Target;
                if (Dot(Offset, Offset) <= Current * (1.0 + 1.0e-14) + Floor * Floor) break;
                Scale *= 0.5;
                Next = ConfineParameter(Definition, { Parameter.X + StepU * Scale, Parameter.Y + StepV * Scale });
            }
            const double Moved = Length(S.U * (StepU * Scale) + S.V * (StepV * Scale));      // [len] - 3D length of the step
            Parameter = Next;
            if (Moved <= Floor || Length(Residual) <= Floor || Final) break;
            Final = Settle && Moved <= 1.0e-9 * Definition.Extent;
        }
        return Parameter;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS — CURVES
//------------------------------------------------------------------------------------------------------------------------

std::optional<NurbsCurve> ConstructNurbsCurve(int Degree, std::vector<double> Knots, std::vector<Vector3> Poles, std::vector<double> Weights)
{
    const int Count = static_cast<int>(Poles.size());
    if (!ValidKnots(Knots, Degree, Count) || static_cast<int>(Weights.size()) != Count) return std::nullopt;
    for (double Weight : Weights) if (!(Weight > 0.0) || !std::isfinite(Weight)) return std::nullopt;
    auto Definition = std::make_shared<NurbsCurveDefinition>();
    Definition->Degree  = Degree;
    Definition->Knots   = std::move(Knots);
    Definition->Poles   = std::move(Poles);
    Definition->Weights = std::move(Weights);
    Definition->Period  = MeasureCurvePeriod(*Definition);
    return NurbsCurve{ std::move(Definition) };
}

NurbsDomain MeasureNurbsCurveDomain(const NurbsCurveDefinition& Definition) noexcept
{
    return { Definition.Knots[Definition.Degree], Definition.Knots[Definition.Knots.size() - 1 - Definition.Degree] };
}

NurbsCurveDerivatives EvaluateNurbsCurveDerivatives(const NurbsCurveDefinition& Definition, double τ) noexcept
{
    const int Degree = Definition.Degree, Count = static_cast<int>(Definition.Poles.size());
    if (Definition.Period > 0.0) τ = FoldInto(τ, Definition.Knots[Degree], Definition.Period);
    const int Span = FindSpan(Definition.Knots, Degree, Count, τ);
    BasisRows Rows;
    ComputeBasisRows(Definition.Knots, Degree, Span, τ, 2, Rows);
    // 📝 Homogeneous derivatives A⁽ᵏ⁾ = Σ Nᵢ⁽ᵏ⁾·wᵢPᵢ, w⁽ᵏ⁾ = Σ Nᵢ⁽ᵏ⁾·wᵢ, then the quotient rule (A4.2):
    //    C = A/w, C′ = (A′ − w′C)/w, C″ = (A″ − 2w′C′ − w″C)/w.
    Vector3 Homogeneous[3] = {};
    double  Weight[3]      = {};
    for (int R = 0; R <= Degree; ++R)
    {
        const int     Index = Span - Degree + R;
        const double  W     = Definition.Weights[Index];
        const Vector3 P     = Definition.Poles[Index] * W;
        for (int K = 0; K <= 2; ++K) { Homogeneous[K] = Homogeneous[K] + P * Rows[K][R]; Weight[K] += W * Rows[K][R]; }
    }
    NurbsCurveDerivatives Result;
    Result.Position = Homogeneous[0] * (1.0 / Weight[0]);
    Result.First    = (Homogeneous[1] - Result.Position * Weight[1]) * (1.0 / Weight[0]);
    Result.Second   = (Homogeneous[2] - Result.First * (2.0 * Weight[1]) - Result.Position * Weight[2]) * (1.0 / Weight[0]);
    return Result;
}

CurveSample EvaluateNurbsCurve(const NurbsCurveDefinition& Definition, double τ) noexcept
{
    const NurbsCurveDerivatives Derivatives = EvaluateNurbsCurveDerivatives(Definition, τ);
    return { Derivatives.Position, Derivatives.First };
}

double ProjectOntoNurbsCurve(const NurbsCurveDefinition& Definition, Vector3 Position, double IntervalLow) noexcept
{
    const NurbsDomain Domain = MeasureNurbsCurveDomain(Definition);
    const int         SampleCount = std::max(32, 8 * static_cast<int>(Definition.Poles.size()));
    // Three best seeds (a closed or doubled-back curve has several local feet).
    std::array<std::pair<double, double>, 3> Seeds;
    Seeds.fill({ INFINITY, Domain.Low });
    for (int Index = 0; Index <= SampleCount; ++Index)
    {
        const double τ = Domain.Low + (Domain.High - Domain.Low) * Index / SampleCount;
        const double Gap = Distance(EvaluateNurbsCurveDerivatives(Definition, τ).Position, Position);
        if (Gap < Seeds[2].first)
        {
            Seeds[2] = { Gap, τ };
            std::sort(Seeds.begin(), Seeds.end());
        }
    }
    double Best = Domain.Low, BestGap = INFINITY;
    for (const auto& [SeedGap, Seed] : Seeds)
    {
        if (!std::isfinite(SeedGap)) continue;
        double τ = Seed;
        for (int Iteration = 0; Iteration < NewtonLimit; ++Iteration)
        {
            const NurbsCurveDerivatives C = EvaluateNurbsCurveDerivatives(Definition, τ);
            const Vector3 Residual = C.Position - Position;
            const double  Slope    = Dot(C.First, C.First) + Dot(C.Second, Residual);
            const double  Value    = Dot(C.First, Residual);
            const double  Step     = Slope > 0.0 ? -Value / Slope : -Value / std::max(Dot(C.First, C.First), 1.0e-300);
            double Next = τ + Step;
            if (Definition.Period > 0.0) Next = FoldInto(Next, Domain.Low, Definition.Period);
            else Next = std::clamp(Next, Domain.Low, Domain.High);
            const double Moved = std::fabs(Step) * Length(C.First);
            τ = Next;
            if (Moved <= 1.0e-16 * std::max(Length(C.Position), 1.0)) break;
        }
        const double Gap = Distance(EvaluateNurbsCurveDerivatives(Definition, τ).Position, Position);
        if (Gap < BestGap) { BestGap = Gap; Best = τ; }
    }
    if (Definition.Period > 0.0)
    {
        Best = FoldInto(Best, IntervalLow, Definition.Period);
        if (Best - IntervalLow > Definition.Period - 1.0e-12) Best = IntervalLow;
    }
    return Best;
}

std::vector<double> CollectNurbsCurveKnots(const NurbsCurveDefinition& Definition, double Low, double High)
{
    return CollectBreaks(Definition.Knots, Definition.Degree, Definition.Period, Low, High);
}

double MeasureNurbsCurveBulge(const NurbsCurveDefinition& Definition, double Low, double High, int Pieces) noexcept
{
    const int    Count = static_cast<int>(Definition.Poles.size());
    const int    PieceCount = std::max(Pieces, 1);
    double       Bulge = 0.0;
    for (int Piece = 0; Piece < PieceCount; ++Piece)
    {
        const double  Start = Low + (High - Low) * Piece / PieceCount, End = Low + (High - Low) * (Piece + 1) / PieceCount;
        const Vector3 A = EvaluateNurbsCurveDerivatives(Definition, Start).Position, B = EvaluateNurbsCurveDerivatives(Definition, End).Position;
        const Vector3 Chord = B - A;
        const double  ChordLength = Length(Chord);
        // 📝 Convex hull: the piece lies inside the hull of the poles of its covering spans (positive weights), and
        //    distance to a line is convex, so the farthest pole bounds the sagitta. A periodic piece crossing the seam
        //    (or reaching outside the domain) falls back to all poles.
        int First = 0, Last = Count - 1;
        const NurbsDomain Domain = MeasureNurbsCurveDomain(Definition);
        if (Start >= Domain.Low && End <= Domain.High)
        {
            First = FindSpan(Definition.Knots, Definition.Degree, Count, Start) - Definition.Degree;
            Last  = FindSpan(Definition.Knots, Definition.Degree, Count, End);
        }
        for (int Index = std::max(First, 0); Index <= std::min(Last, Count - 1); ++Index)
        {
            const Vector3 Offset = Definition.Poles[Index] - A;
            const double  Gap = ChordLength > 0.0 ? Length(Cross(Offset, Chord)) / ChordLength : Length(Offset);
            Bulge = std::max(Bulge, Gap);
        }
    }
    return Bulge;
}

double MeasureNurbsCurveScale(const NurbsCurveDefinition& Definition) noexcept
{
    double Polygon = 0.0;
    for (size_t Index = 1; Index < Definition.Poles.size(); ++Index) Polygon += Distance(Definition.Poles[Index], Definition.Poles[Index - 1]);
    const NurbsDomain Domain = MeasureNurbsCurveDomain(Definition);
    return Polygon > 0.0 ? Polygon / (Domain.High - Domain.Low) : 1.0;
}

NurbsCurve ReverseNurbsCurve(const NurbsCurveDefinition& Definition)
{
    auto Image = std::make_shared<NurbsCurveDefinition>(Definition);
    std::reverse(Image->Poles.begin(), Image->Poles.end());
    std::reverse(Image->Weights.begin(), Image->Weights.end());
    for (size_t Index = 0; Index < Definition.Knots.size(); ++Index) Image->Knots[Index] = -Definition.Knots[Definition.Knots.size() - 1 - Index];
    return NurbsCurve{ std::move(Image) };
}

NurbsCurve MapNurbsCurve(const NurbsCurveDefinition& Definition, const PointMap& Map)
{
    auto Image = std::make_shared<NurbsCurveDefinition>(Definition);
    for (Vector3& Pole : Image->Poles) Pole = Map(Pole);
    return NurbsCurve{ std::move(Image) };
}

NurbsBasisSample EvaluateNurbsBasis(const std::vector<double>& Knots, int Degree, int Count, double τ)
{
    NurbsBasisSample Sample;
    const int Span = FindSpan(Knots, Degree, Count, τ);
    BasisRows Rows;
    ComputeBasisRows(Knots, Degree, Span, τ, 2, Rows);
    Sample.First = Span - Degree;
    for (int Order = 0; Order < 3; ++Order) Sample.Values[Order].assign(Rows[Order].begin(), Rows[Order].begin() + Degree + 1);
    return Sample;
}

NurbsCurveDefinition RefineNurbsCurveKnots(const NurbsCurveDefinition& Definition, const std::vector<double>& Inserted)
{
    NurbsCurveDefinition Result = Definition;
    const int Degree = Definition.Degree;
    for (double Value : Inserted)
    {
        const int Count = static_cast<int>(Result.Poles.size());
        if (!(Value > Result.Knots[Degree] && Value < Result.Knots[Count])) continue;          // Outside the open domain
        const int Multiplicity = static_cast<int>(std::count(Result.Knots.begin(), Result.Knots.end(), Value));
        if (Multiplicity >= Degree) continue;
        const int Span = FindSpan(Result.Knots, Degree, Count, Value);
        std::vector<Vector3> Poles(Count + 1);
        std::vector<double>  Weights(Count + 1);
        for (int Index = 0; Index <= Count; ++Index)
        {
            if (Index <= Span - Degree) { Poles[Index] = Result.Poles[Index]; Weights[Index] = Result.Weights[Index]; continue; }
            if (Index > Span)           { Poles[Index] = Result.Poles[Index - 1]; Weights[Index] = Result.Weights[Index - 1]; continue; }
            const double α = (Value - Result.Knots[Index]) / (Result.Knots[Index + Degree] - Result.Knots[Index]);
            const double Weight = α * Result.Weights[Index] + (1.0 - α) * Result.Weights[Index - 1];
            const Vector3 Homogeneous = Result.Poles[Index] * (α * Result.Weights[Index]) + Result.Poles[Index - 1] * ((1.0 - α) * Result.Weights[Index - 1]);
            Poles[Index]   = Homogeneous * (1.0 / Weight);
            Weights[Index] = Weight;
        }
        Result.Knots.insert(Result.Knots.begin() + Span + 1, Value);
        Result.Poles   = std::move(Poles);
        Result.Weights = std::move(Weights);
    }
    return Result;
}

std::vector<NurbsBezierPiece> DecomposeNurbsCurve(const NurbsCurveDefinition& Definition, double Low, double High)
{
    std::vector<NurbsBezierPiece> Pieces;
    const NurbsDomain Domain = MeasureNurbsCurveDomain(Definition);
    if (Definition.Period > 0.0)
    {
        if (!(Domain.High > Domain.Low) || !std::isfinite(Low) || !std::isfinite(High) || !(High > Low)) return Pieces;
        // Fold the interval start into the domain and split at every seam crossing.
        const double Shift = FoldInto(Low, Domain.Low, Definition.Period) - Low;            // [-] - Multiple of the period
        double Start = Low + Shift, Remaining = High - Low;
        if (Start >= Domain.High) Start = Domain.Low;
        int Wraps = 0;
        while (Remaining > 1.0e-15 * Definition.Period && Wraps++ < 256)
        {
            const double End = std::min(Domain.High, Start + Remaining);
            if (!(End > Start)) { Start = Domain.Low; continue; }
            NurbsCurveDefinition Open = Definition;
            Open.Period = 0.0;
            for (NurbsBezierPiece& Piece : DecomposeNurbsCurve(Open, Start, End))
            {
                Piece.PieceLow  += (Low + (High - Low - Remaining)) - Start;
                Piece.PieceHigh += (Low + (High - Low - Remaining)) - Start;
                Pieces.push_back(std::move(Piece));
            }
            Remaining -= End - Start;
            Start = Domain.Low;
        }
        return Pieces;
    }
    Low  = std::max(Low, Domain.Low);
    High = std::min(High, Domain.High);
    if (!(High > Low)) return Pieces;
    const int Degree = Definition.Degree;
    std::vector<double>  Knots = Definition.Knots;
    std::vector<Vector3> Homogeneous(Definition.Poles.size());
    std::vector<double>  Weights = Definition.Weights;
    for (size_t Index = 0; Index < Homogeneous.size(); ++Index) Homogeneous[Index] = Definition.Poles[Index] * Weights[Index];
    // Boehm single insertion (A5.1 with r = 1), homogeneous: Qᵢ = αᵢ·Pᵢ + (1 − αᵢ)·Pᵢ₋₁ on the p affected poles.
    const auto Insert = [&](double τ)
    {
        const int Count = static_cast<int>(Homogeneous.size());
        int Span = Degree;
        while (Span + 1 < static_cast<int>(Knots.size()) && Knots[Span + 1] <= τ && Span < Count - 1) ++Span;
        std::vector<Vector3> NextPoles(Count + 1);
        std::vector<double>  NextWeights(Count + 1);
        for (int Index = 0; Index <= Count; ++Index)
        {
            if (Index <= Span - Degree) { NextPoles[Index] = Homogeneous[Index]; NextWeights[Index] = Weights[Index]; }
            else if (Index > Span) { NextPoles[Index] = Homogeneous[Index - 1]; NextWeights[Index] = Weights[Index - 1]; }
            else
            {
                const double α = (τ - Knots[Index]) / (Knots[Index + Degree] - Knots[Index]);
                NextPoles[Index]   = Homogeneous[Index] * α + Homogeneous[Index - 1] * (1.0 - α);
                NextWeights[Index] = Weights[Index] * α + Weights[Index - 1] * (1.0 - α);
            }
        }
        Knots.insert(Knots.begin() + Span + 1, τ);
        Homogeneous = std::move(NextPoles);
        Weights     = std::move(NextWeights);
    };
    const auto Multiplicity = [&](double τ) { return static_cast<int>(std::count(Knots.begin(), Knots.end(), τ)); };
    std::vector<double> Breaks = { Low };
    for (double Knot : CollectBreaks(Definition.Knots, Degree, 0.0, Low, High)) Breaks.push_back(Knot);
    Breaks.push_back(High);
    for (double Break : Breaks) for (int Existing = Multiplicity(Break); Existing < Degree; ++Existing) Insert(Break);
    for (size_t Piece = 0; Piece + 1 < Breaks.size(); ++Piece)
    {
        // The span [Breaks[Piece], Breaks[Piece + 1]) starts at the last copy of its low knot.
        int Span = Degree;
        while (Span + 1 < static_cast<int>(Knots.size()) && Knots[Span + 1] <= Breaks[Piece]) ++Span;
        NurbsBezierPiece Bezier;
        for (int Index = Span - Degree; Index <= Span; ++Index)
        {
            Bezier.Weights.push_back(Weights[Index]);
            Bezier.Poles.push_back(Homogeneous[Index] * (1.0 / Weights[Index]));
        }
        Bezier.PieceLow  = Breaks[Piece];
        Bezier.PieceHigh = Breaks[Piece + 1];
        Pieces.push_back(std::move(Bezier));
    }
    return Pieces;
}

NurbsCurve ConvertLineToNurbs(const StraightLine& Line, double Low, double High)
{
    return *ConstructNurbsCurve(1, { Low, Low, High, High }, { Line.Origin + Line.Direction * Low, Line.Origin + Line.Direction * High }, { 1.0, 1.0 });
}

NurbsCurve ConvertConicToNurbs(const ConicForm& Conic, double Low, double High)
{
    RationalArc Arc = ConstructRationalArc(Conic, Low, High);
    if (High - Low >= FullTurn - 1.0e-12) Arc.Poles.back() = Arc.Poles.front();            // Exact closure for the period test
    return *ConstructNurbsCurve(2, std::move(Arc.Knots), std::move(Arc.Poles), std::move(Arc.Weights));
}

std::optional<NurbsCurve> ConvertCurveToNurbs(const CurveGeometry& Curve, double Low, double High)
{
    if (const auto* Line = std::get_if<StraightLine>(&Curve)) return ConvertLineToNurbs(*Line, Low, High);
    if (const auto* Spline = std::get_if<NurbsCurve>(&Curve)) return *Spline;
    if (const std::optional<ConicForm> Conic = ResolveConicForm(Curve)) return ConvertConicToNurbs(*Conic, Low, High);
    return std::nullopt;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS — SURFACES
//------------------------------------------------------------------------------------------------------------------------

std::optional<NurbsSurface> ConstructNurbsSurface(int DegreeU, int DegreeV, int CountU, int CountV, std::vector<double> KnotsU,
                                                  std::vector<double> KnotsV, std::vector<Vector3> Poles, std::vector<double> Weights)
{
    if (!ValidKnots(KnotsU, DegreeU, CountU) || !ValidKnots(KnotsV, DegreeV, CountV)) return std::nullopt;
    const size_t Count = static_cast<size_t>(CountU) * CountV;
    if (Poles.size() != Count || Weights.size() != Count) return std::nullopt;
    for (double Weight : Weights) if (!(Weight > 0.0) || !std::isfinite(Weight)) return std::nullopt;
    auto Definition = std::make_shared<NurbsSurfaceDefinition>();
    Definition->DegreeU = DegreeU;
    Definition->DegreeV = DegreeV;
    Definition->CountU  = CountU;
    Definition->CountV  = CountV;
    Definition->KnotsU  = std::move(KnotsU);
    Definition->KnotsV  = std::move(KnotsV);
    Definition->Poles   = std::move(Poles);
    Definition->Weights = std::move(Weights);
    PrepareSurface(*Definition);
    return NurbsSurface{ std::move(Definition) };
}

NurbsDomain MeasureNurbsSurfaceDomain(const NurbsSurfaceDefinition& Definition, bool AlongU) noexcept
{
    const std::vector<double>& Knots  = AlongU ? Definition.KnotsU : Definition.KnotsV;
    const int                  Degree = AlongU ? Definition.DegreeU : Definition.DegreeV;
    return { Knots[Degree], Knots[Knots.size() - 1 - Degree] };
}

NurbsSurfaceDerivatives EvaluateNurbsSurfaceDerivatives(const NurbsSurfaceDefinition& Definition, Vector2 Parameter) noexcept
{
    const int p = Definition.DegreeU, q = Definition.DegreeV;
    double 𝑢 = Parameter.X, 𝑣 = Parameter.Y;
    if (Definition.PeriodU > 0.0) 𝑢 = FoldInto(𝑢, Definition.KnotsU[p], Definition.PeriodU);
    if (Definition.PeriodV > 0.0) 𝑣 = FoldInto(𝑣, Definition.KnotsV[q], Definition.PeriodV);
    const int SpanU = FindSpan(Definition.KnotsU, p, Definition.CountU, 𝑢);
    const int SpanV = FindSpan(Definition.KnotsV, q, Definition.CountV, 𝑣);
    BasisRows RowsU, RowsV;
    ComputeBasisRows(Definition.KnotsU, p, SpanU, 𝑢, 2, RowsU);
    ComputeBasisRows(Definition.KnotsV, q, SpanV, 𝑣, 2, RowsV);
    // 📝 Homogeneous A_kl = ΣΣ N⁽ᵏ⁾M⁽ˡ⁾·wP and w_kl for k + l ≤ 2, then A4.4:
    //    S𝑢 = (A₁₀ − w₁₀S)/w, S𝑢𝑢 = (A₂₀ − 2w₁₀S𝑢 − w₂₀S)/w, S𝑢𝑣 = (A₁₁ − w₁₀S𝑣 − w₀₁S𝑢 − w₁₁S)/w (and 𝑣 alike).
    Vector3 A00{}, A10{}, A01{}, A20{}, A11{}, A02{};
    double  W00 = 0.0, W10 = 0.0, W01 = 0.0, W20 = 0.0, W11 = 0.0, W02 = 0.0;
    for (int K = 0; K <= p; ++K)
    {
        const int Row = (SpanU - p + K) * Definition.CountV;
        for (int L = 0; L <= q; ++L)
        {
            const int     Index = Row + SpanV - q + L;
            const double  W     = Definition.Weights[Index];
            const Vector3 P     = Definition.Poles[Index] * W;
            const double  B00 = RowsU[0][K] * RowsV[0][L], B10 = RowsU[1][K] * RowsV[0][L], B01 = RowsU[0][K] * RowsV[1][L];
            const double  B20 = RowsU[2][K] * RowsV[0][L], B11 = RowsU[1][K] * RowsV[1][L], B02 = RowsU[0][K] * RowsV[2][L];
            A00 = A00 + P * B00; A10 = A10 + P * B10; A01 = A01 + P * B01; A20 = A20 + P * B20; A11 = A11 + P * B11; A02 = A02 + P * B02;
            W00 += W * B00; W10 += W * B10; W01 += W * B01; W20 += W * B20; W11 += W * B11; W02 += W * B02;
        }
    }
    const double Inverse = 1.0 / W00;                                                        // [-] - 1/w
    NurbsSurfaceDerivatives S;
    S.Position = A00 * Inverse;
    S.U        = (A10 - S.Position * W10) * Inverse;
    S.V        = (A01 - S.Position * W01) * Inverse;
    S.UU       = (A20 - S.U * (2.0 * W10) - S.Position * W20) * Inverse;
    S.VV       = (A02 - S.V * (2.0 * W01) - S.Position * W02) * Inverse;
    S.UV       = (A11 - S.V * W10 - S.U * W01 - S.Position * W11) * Inverse;
    return S;
}

SurfaceSample EvaluateNurbsSurface(const NurbsSurface& Surface, Vector2 Parameter) noexcept
{
    const NurbsSurfaceDerivatives S = EvaluateNurbsSurfaceDerivatives(*Surface.Definition, Parameter);
    return { S.Position, S.U, S.V };
}

Vector3 EvaluateNurbsNormal(const NurbsSurface& Surface, Vector2 Parameter) noexcept
{
    const NurbsSurfaceDefinition& Definition = *Surface.Definition;
    const NurbsSurfaceDerivatives S = EvaluateNurbsSurfaceDerivatives(Definition, Parameter);
    const double LengthU = Length(S.U), LengthV = Length(S.V);
    const double Small   = 1.0e-10 * Definition.Extent;                                     // [len] - Collapsed-row threshold
    const Vector3 Natural = Cross(S.U, S.V);
    if (LengthU > Small && LengthV > Small && Length(Natural) > 1.0e-12 * LengthU * LengthV) return Normalize(Natural);
    // 📝 Collapsed row 𝑣 = 𝑣₀ (S𝑢 ≡ 0 along it): S𝑢 ≈ S𝑢𝑣·(𝑣 − 𝑣₀), so n → sign(𝑣 − 𝑣₀)·S𝑢𝑣 × S𝑣; and symmetrically for 𝑢.
    const NurbsDomain U = MeasureNurbsSurfaceDomain(Definition, true), V = MeasureNurbsSurfaceDomain(Definition, false);
    if (LengthU <= Small && LengthV > Small)
    {
        const double Inward = Parameter.Y <= 0.5 * (V.Low + V.High) ? 1.0 : -1.0;
        const Vector3 Limit = Cross(S.UV, S.V) * Inward;
        if (Length(Limit) > 1.0e-12 * Length(S.UV) * LengthV) return Normalize(Limit);
    }
    if (LengthV <= Small && LengthU > Small)
    {
        const double Inward = Parameter.X <= 0.5 * (U.Low + U.High) ? 1.0 : -1.0;
        const Vector3 Limit = Cross(S.U, S.UV) * Inward;
        if (Length(Limit) > 1.0e-12 * Length(S.UV) * LengthU) return Normalize(Limit);
    }
    // Last resort: a small inward step (both directions).
    const Vector2 Inside{ Parameter.X + (Parameter.X <= 0.5 * (U.Low + U.High) ? 1.0 : -1.0) * 1.0e-7 * (U.High - U.Low),
                          Parameter.Y + (Parameter.Y <= 0.5 * (V.Low + V.High) ? 1.0 : -1.0) * 1.0e-7 * (V.High - V.Low) };
    const NurbsSurfaceDerivatives Near = EvaluateNurbsSurfaceDerivatives(Definition, Inside);
    return Normalize(Cross(Near.U, Near.V));
}

Vector2 ProjectOntoNurbsSurface(const NurbsSurface& Surface, Vector3 Position, Vector2 Reference) noexcept
{
    const NurbsSurfaceDefinition& Definition = *Surface.Definition;
    const NurbsDomain U = MeasureNurbsSurfaceDomain(Definition, true), V = MeasureNurbsSurfaceDomain(Definition, false);
    // ① Seeds: the reference itself (when inside the domain) and the four nearest lattice samples.
    constexpr int SeedLimit = 4;
    std::array<std::pair<double, int>, SeedLimit> Nearest;
    Nearest.fill({ INFINITY, -1 });
    for (int Index = 0; Index < static_cast<int>(Definition.Seeds.size()); ++Index)
    {
        const Vector3 Offset = Definition.Seeds[Index] - Position;
        const double  Gap    = Dot(Offset, Offset);
        if (Gap < Nearest[SeedLimit - 1].first)
        {
            Nearest[SeedLimit - 1] = { Gap, Index };
            std::sort(Nearest.begin(), Nearest.end());
        }
    }
    std::vector<Vector2> Starts;
    const bool ReferenceInside = (Definition.PeriodU > 0.0 || (Reference.X >= U.Low && Reference.X <= U.High))
                              && (Definition.PeriodV > 0.0 || (Reference.Y >= V.Low && Reference.Y <= V.High));
    if (ReferenceInside) Starts.push_back(ConfineParameter(Definition, Reference));
    for (const auto& [Gap, Index] : Nearest)
    {
        if (Index < 0) continue;
        const int I = Index / Definition.SeedCountV, J = Index % Definition.SeedCountV;
        Starts.push_back({ U.Low + (U.High - U.Low) * I / (Definition.SeedCountU - 1.0), V.Low + (V.High - V.Low) * J / (Definition.SeedCountV - 1.0) });
    }
    // ② Polish each; keep the nearest foot (ties keep the earlier start, so the reference wins when it is a foot).
    Vector2 Best = Starts.front();
    double  BestGap = INFINITY;
    for (const Vector2& Start : Starts)
    {
        const Vector2 Foot = PolishSurfaceFoot(Definition, Position, Start);
        const double  Gap  = Distance(EvaluateNurbsSurfaceDerivatives(Definition, Foot).Position, Position);
        if (Gap < BestGap - 1.0e-15 * Definition.Extent) { BestGap = Gap; Best = Foot; }
    }
    // ③ Periodic directions take the representative nearest to the reference.
    if (Definition.PeriodU > 0.0) Best.X = FoldNearest(Best.X, Reference.X, Definition.PeriodU);
    if (Definition.PeriodV > 0.0) Best.Y = FoldNearest(Best.Y, Reference.Y, Definition.PeriodV);
    return Best;
}

std::optional<Vector2> PolishNurbsFootLocally(const NurbsSurface& Surface, Vector3 Position, Vector2 Start) noexcept
{
    const NurbsSurfaceDefinition& Definition = *Surface.Definition;
    const NurbsDomain U = MeasureNurbsSurfaceDomain(Definition, true), V = MeasureNurbsSurfaceDomain(Definition, false);
    const Vector2 Origin = ConfineParameter(Definition, Start);                               // [-] - Start inside the domain
    const Vector2 Foot   = PolishSurfaceFoot(Definition, Position, Origin, true);
    // Chart jump: the foot must stay in the basin of the start (periodic coordinates compared on the circle).
    const auto Jump = [](double From, double To, double Period) { const double Step = To - From; return Period > 0.0 ? std::fabs(Step - Period * std::round(Step / Period)) : std::fabs(Step); };
    if (Jump(Origin.X, Foot.X, Definition.PeriodU) > 0.1 * (U.High - U.Low) || Jump(Origin.Y, Foot.Y, Definition.PeriodV) > 0.1 * (V.High - V.Low)) return std::nullopt;
    const NurbsSurfaceDerivatives S = EvaluateNurbsSurfaceDerivatives(Definition, Foot);
    const Vector3 Residual = S.Position - Position;                                           // [len] - S − X
    // 💡 A residual at the rounding level of the coordinates means the point lies ON the surface: no foot is nearer, and
    //    the residual's direction is noise, so neither the domain-edge rule nor the stationarity test below applies.
    //    SP-curves on fair / loft patches are such points (rims on the domain edges, interior spokes): 57 % of their
    //    evaluations failed the stationarity test on noise and 100 % of rim points the edge rule, each going global (5
    //    polished starts, ~136 evaluations) — 78 % of an all-edge fillet of a pocketed block (12.6 s). Collapsed rows
    //    (S𝑢 = 0) still go global through the Hessian test.
    const double Rounding  = 64.0 * std::numeric_limits<double>::epsilon() * (Length(Position) + Definition.Extent);   // [len] - Coordinate rounding
    const bool   OnSurface = Length(Residual) <= Rounding;
    // Interior: a foot held on a domain edge is a constrained minimum that the global search must arbitrate.
    const double EdgeU = 1.0e-12 * (U.High - U.Low), EdgeV = 1.0e-12 * (V.High - V.Low);
    const bool   EdgeFoot = (Definition.PeriodU <= 0.0 && (Foot.X <= U.Low + EdgeU || Foot.X >= U.High - EdgeU)) || (Definition.PeriodV <= 0.0 && (Foot.Y <= V.Low + EdgeV || Foot.Y >= V.High - EdgeV));
    if (EdgeFoot && !OnSurface) return std::nullopt;
    // Stationary and a strict minimum of |X − S|².
    const double  Slack    = 1.0e-10 * (Length(Residual) + 1.0e-6 * Definition.Extent);       // [len] - Rounding allowance
    if (!OnSurface && (std::fabs(Dot(S.U, Residual)) > Slack * Length(S.U) || std::fabs(Dot(S.V, Residual)) > Slack * Length(S.V))) return std::nullopt;
    const double A = Dot(S.U, S.U) + Dot(S.UU, Residual), B = Dot(S.U, S.V) + Dot(S.UV, Residual), D = Dot(S.V, S.V) + Dot(S.VV, Residual);
    if (!(A > 0.0) || !(D > 0.0) || !(A * D - B * B > 0.0)) return std::nullopt;
    return Foot;
}

SurfacePeriodicity ResolveNurbsPeriodicity(const NurbsSurface& Surface) noexcept
{
    return { Surface.Definition->PeriodU, Surface.Definition->PeriodV };
}

std::vector<double> CollectNurbsSurfaceKnots(const NurbsSurface& Surface, bool AlongU, double Low, double High)
{
    const NurbsSurfaceDefinition& Definition = *Surface.Definition;
    return AlongU ? CollectBreaks(Definition.KnotsU, Definition.DegreeU, Definition.PeriodU, Low, High)
                  : CollectBreaks(Definition.KnotsV, Definition.DegreeV, Definition.PeriodV, Low, High);
}

double MeasureNurbsCurvature(const NurbsSurface& Surface, Vector2 Parameter) noexcept
{
    const NurbsSurfaceDefinition& Definition = *Surface.Definition;
    const auto Peak = [&](Vector2 At, double& Result)
    {
        const NurbsSurfaceDerivatives S = EvaluateNurbsSurfaceDerivatives(Definition, At);
        const double E = Dot(S.U, S.U), F = Dot(S.U, S.V), G = Dot(S.V, S.V);
        const double Gram = E * G - F * F;
        if (!(Gram > 1.0e-24 * E * G) || !(E > 0.0) || !(G > 0.0)) return false;
        const Vector3 n = Normalize(Cross(S.U, S.V));
        const double L = Dot(S.UU, n), M = Dot(S.UV, n), N = Dot(S.VV, n);
        const double Gaussian = (L * N - M * M) / Gram;                                      // [1/len²] - K
        const double Mean     = (E * N - 2.0 * F * M + G * L) / (2.0 * Gram);                // [1/len]  - H
        Result = std::fabs(Mean) + std::sqrt(std::max(Mean * Mean - Gaussian, 0.0));
        return true;
    };
    double Result = 0.0;
    if (Peak(Parameter, Result)) return Result;
    // A collapsed row: the limit from just inside (the principal curvatures stay finite at a sphere pole).
    const NurbsDomain U = MeasureNurbsSurfaceDomain(Definition, true), V = MeasureNurbsSurfaceDomain(Definition, false);
    const Vector2 Inside{ Parameter.X + (Parameter.X <= 0.5 * (U.Low + U.High) ? 1.0 : -1.0) * 1.0e-6 * (U.High - U.Low),
                          Parameter.Y + (Parameter.Y <= 0.5 * (V.Low + V.High) ? 1.0 : -1.0) * 1.0e-6 * (V.High - V.Low) };
    return Peak(Inside, Result) ? Result : INFINITY;
}

NurbsCurve ExtractNurbsIsoCurve(const NurbsSurface& Surface, bool FixedU, double Value)
{
    // 📝 Exact: blend the poles of the fixed direction with its basis at Value in homogeneous form (Qⱼ·ωⱼ = Σ Nᵢ wᵢⱼ Pᵢⱼ).
    const NurbsSurfaceDefinition& Definition = *Surface.Definition;
    const int    FixedDegree = FixedU ? Definition.DegreeU : Definition.DegreeV;
    const int    FixedCount  = FixedU ? Definition.CountU : Definition.CountV;
    const int    FreeCount   = FixedU ? Definition.CountV : Definition.CountU;
    const auto&  FixedKnots  = FixedU ? Definition.KnotsU : Definition.KnotsV;
    const double FixedPeriod = FixedU ? Definition.PeriodU : Definition.PeriodV;
    if (FixedPeriod > 0.0) Value = FoldInto(Value, FixedKnots[FixedDegree], FixedPeriod);
    const int Span = FindSpan(FixedKnots, FixedDegree, FixedCount, Value);
    BasisRows Rows;
    ComputeBasisRows(FixedKnots, FixedDegree, Span, Value, 0, Rows);
    std::vector<Vector3> Poles(FreeCount);
    std::vector<double>  Weights(FreeCount, 0.0);
    for (int Free = 0; Free < FreeCount; ++Free)
    {
        Vector3 Homogeneous{};
        for (int K = 0; K <= FixedDegree; ++K)
        {
            const int Fixed = Span - FixedDegree + K;
            const int Index = FixedU ? Fixed * Definition.CountV + Free : Free * Definition.CountV + Fixed;
            Homogeneous = Homogeneous + Definition.Poles[Index] * (Definition.Weights[Index] * Rows[0][K]);
            Weights[Free] += Definition.Weights[Index] * Rows[0][K];
        }
        Poles[Free] = Homogeneous * (1.0 / Weights[Free]);
    }
    auto Iso = std::make_shared<NurbsCurveDefinition>();
    Iso->Degree  = FixedU ? Definition.DegreeV : Definition.DegreeU;
    Iso->Knots   = FixedU ? Definition.KnotsV : Definition.KnotsU;
    Iso->Poles   = std::move(Poles);
    Iso->Weights = std::move(Weights);
    Iso->Period  = FixedU ? Definition.PeriodV : Definition.PeriodU;                         // Inherited, exact
    return NurbsCurve{ std::move(Iso) };
}

NurbsSurface MapNurbsSurface(const NurbsSurfaceDefinition& Definition, const PointMap& Map)
{
    auto Image = std::make_shared<NurbsSurfaceDefinition>(Definition);
    for (Vector3& Pole : Image->Poles) Pole = Map(Pole);
    PrepareSurface(*Image);
    return NurbsSurface{ std::move(Image) };
}

namespace
{
    using HomogeneousPole = std::array<double, 4>;                                           // [-] - (w·P, w)

    // Blossom of the polynomial on span [Knots[Span], Knots[Span + 1]] (poles Span − p … Span) at Arguments (p values).
    [[nodiscard]] HomogeneousPole BlossomSpan(const std::vector<double>& Knots, int Degree, int Span, const std::vector<HomogeneousPole>& Poles, const std::vector<double>& Arguments) noexcept
    {
        std::vector<HomogeneousPole> Work(Poles.begin() + (Span - Degree), Poles.begin() + (Span + 1));
        for (int Level = 1; Level <= Degree; ++Level)
        {
            for (int Index = Degree; Index >= Level; --Index)
            {
                const int    Knot  = Span - Degree + Index;                                 // [-] - j
                const double Width = Knots[Knot + Degree + 1 - Level] - Knots[Knot];
                const double Alpha = Width > 0.0 ? (Arguments[Level - 1] - Knots[Knot]) / Width : 0.0;
                for (int Axis = 0; Axis < 4; ++Axis) Work[Index][Axis] = (1.0 - Alpha) * Work[Index - 1][Axis] + Alpha * Work[Index][Axis];
            }
        }
        return Work[Degree];
    }

    // Re-expresses one open B-spline row on the Grown knots (end knots moved outward): the first / last p poles become
    // the blossoms of the end spans at the new knot windows; every other pole is kept.
    void ExtendRow(const std::vector<double>& Knots, const std::vector<double>& Grown, int Degree, std::vector<HomogeneousPole>& Poles)
    {
        const int Count = static_cast<int>(Poles.size());
        int First = Degree, Last = Count - 1;                                                // [-] - End spans (non-empty)
        while (First + 1 < Count && !(Knots[First + 1] > Knots[First])) ++First;
        while (Last > Degree && !(Knots[Last + 1] > Knots[Last])) --Last;
        std::vector<HomogeneousPole> Result = Poles;
        for (int Index = 0; Index < Degree; ++Index)
        {
            const std::vector<double> Head(Grown.begin() + Index + 1, Grown.begin() + Index + 1 + Degree);
            Result[Index] = BlossomSpan(Knots, Degree, First, Poles, Head);
            const std::vector<double> Tail(Grown.begin() + (Count - 1 - Index) + 1, Grown.begin() + (Count - 1 - Index) + 1 + Degree);
            Result[Count - 1 - Index] = BlossomSpan(Knots, Degree, Last, Poles, Tail);
        }
        Poles.swap(Result);
    }
}

std::optional<NurbsSurface> ExtendNurbsSurface(const NurbsSurfaceDefinition& Definition, double Extension)
{
    auto Image = std::make_shared<NurbsSurfaceDefinition>(Definition);
    for (const bool AlongU : { true, false })
    {
        if ((AlongU ? Image->PeriodU : Image->PeriodV) > 0.0) continue;
        const int            Degree = AlongU ? Image->DegreeU : Image->DegreeV;
        const int            Count  = AlongU ? Image->CountU : Image->CountV;
        const int            Across = AlongU ? Image->CountV : Image->CountU;
        std::vector<double>& Knots  = AlongU ? Image->KnotsU : Image->KnotsV;
        const double         Grow   = Extension * (Knots[Knots.size() - 1 - Degree] - Knots[Degree]);   // [-] - Parameter growth per end
        const std::vector<double> Original = Knots;
        std::vector<double>       Grown    = Knots;
        for (int Index = 0; Index <= Degree; ++Index)
        {
            Grown[Index]                    = Original[Degree] - Grow;
            Grown[Grown.size() - 1 - Index] = Original[Original.size() - 1 - Degree] + Grow;
        }
        for (int Line = 0; Line < Across; ++Line)
        {
            std::vector<HomogeneousPole> Row(Count);
            for (int Index = 0; Index < Count; ++Index)
            {
                const size_t Slot   = AlongU ? static_cast<size_t>(Index) * Image->CountV + Line : static_cast<size_t>(Line) * Image->CountV + Index;
                const double Weight = Image->Weights[Slot];
                Row[Index] = { Image->Poles[Slot].X * Weight, Image->Poles[Slot].Y * Weight, Image->Poles[Slot].Z * Weight, Weight };
            }
            ExtendRow(Original, Grown, Degree, Row);
            for (int Index = 0; Index < Count; ++Index)
            {
                const size_t Slot = AlongU ? static_cast<size_t>(Index) * Image->CountV + Line : static_cast<size_t>(Line) * Image->CountV + Index;
                if (!(Row[Index][3] > 0.0) || !std::isfinite(Row[Index][3])) return std::nullopt;   // A rational end span turned over
                Image->Weights[Slot] = Row[Index][3];
                Image->Poles[Slot]   = { Row[Index][0] / Row[Index][3], Row[Index][1] / Row[Index][3], Row[Index][2] / Row[Index][3] };
            }
        }
        Knots = Grown;
    }
    PrepareSurface(*Image);
    return NurbsSurface{ std::move(Image) };
}

NurbsSurface ConstructNurbsRevolution(const NurbsCurveDefinition& Profile, const OrthonormalBasis& Axis, double StartAngle, double Sweep)
{
    // 📝 Every ring pole is an affine image of the profile pole Q = O + a·X + b·Y + h·Z: rotated by the joint angle θₖ
    //    (the radial part only), scaled by 1/cos(Δ/2) at the middle poles, height kept. That map is linear in Q and the
    //    same for every profile pole, so the tensor product is exactly the swept profile (rational circles are exact).
    const ConicForm Unit{ {}, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
    const RationalArc Ring = ConstructRationalArc(Unit, StartAngle, StartAngle + Sweep);    // Poles are (cos θ, sin θ)/scale
    const bool Closed = std::fabs(Sweep) >= FullTurn - 1.0e-12;
    const int  CountU = static_cast<int>(Ring.Poles.size()), CountV = static_cast<int>(Profile.Poles.size());
    std::vector<Vector3> Poles(static_cast<size_t>(CountU) * CountV);
    std::vector<double>  Weights(Poles.size());
    for (int I = 0; I < CountU; ++I)
    {
        // Ring pole I as (c, s) = r·(cos θ, sin θ) with r = 1 or 1/cos(Δ/2); the closing pole reuses the first exactly.
        const Vector3 RingPole = Closed && I == CountU - 1 ? Ring.Poles.front() : Ring.Poles[I];
        for (int J = 0; J < CountV; ++J)
        {
            const Vector3 Offset = Profile.Poles[J] - Axis.Origin;
            const double  a = Dot(Offset, Axis.XAxis), b = Dot(Offset, Axis.YAxis), h = Dot(Offset, Axis.ZAxis);
            const double  RotatedX = RingPole.X * a - RingPole.Y * b;                        // [len] - Radial part rotated and scaled
            const double  RotatedY = RingPole.Y * a + RingPole.X * b;
            Poles[I * CountV + J]   = Axis.Origin + Axis.XAxis * RotatedX + Axis.YAxis * RotatedY + Axis.ZAxis * h;
            Weights[I * CountV + J] = Ring.Weights[I] * Profile.Weights[J];
        }
    }
    return *ConstructNurbsSurface(2, Profile.Degree, CountU, CountV, Ring.Knots, Profile.Knots, std::move(Poles), std::move(Weights));
}

std::optional<NurbsSurface> ConvertSurfaceToNurbs(const SurfaceGeometry& Surface, Vector2 BoxLow, Vector2 BoxHigh)
{
    if (const auto* Spline = std::get_if<NurbsSurface>(&Surface)) return *Spline;
    if (std::holds_alternative<RollingBallSurface>(Surface) || std::holds_alternative<ParallelSurface>(Surface)) return std::nullopt;   // Not rational
    // Linear directions get a 5 % margin so every SP-curve stays strictly inside the patch.
    const auto Widen = [](double Low, double High)
    {
        const double Margin = 0.05 * std::max(High - Low, 1.0e-6) + 1.0e-9;
        return std::pair<double, double>{ Low - Margin, High + Margin };
    };
    if (const auto* Plane = std::get_if<PlaneSurface>(&Surface))
    {
        const auto [U0, U1] = Widen(BoxLow.X, BoxHigh.X);
        const auto [V0, V1] = Widen(BoxLow.Y, BoxHigh.Y);
        const OrthonormalBasis& Frame = Plane->Frame;
        const auto Point = [&](double 𝑢, double 𝑣) { return Frame.Origin + Frame.XAxis * 𝑢 + Frame.YAxis * 𝑣; };
        return ConstructNurbsSurface(1, 1, 2, 2, { U0, U0, U1, U1 }, { V0, V0, V1, V1 }, { Point(U0, V0), Point(U0, V1), Point(U1, V0), Point(U1, V1) }, { 1.0, 1.0, 1.0, 1.0 });
    }
    // Revolution kinds: 𝑢 is the full turn from the box's low azimuth, the profile lies in the meridian half-plane at 𝑢 = 0.
    const double Start = BoxLow.X;                                                           // [rad] - 𝑢-domain low end
    if (const auto* Cylinder = std::get_if<CylinderSurface>(&Surface))
    {
        const auto [V0, V1] = Widen(BoxLow.Y, BoxHigh.Y);
        const OrthonormalBasis& Frame = Cylinder->Frame;
        const NurbsCurve Profile = *ConstructNurbsCurve(1, { V0, V0, V1, V1 }, { Frame.Origin + Frame.XAxis * Cylinder->Radius + Frame.ZAxis * V0,
                                                                                 Frame.Origin + Frame.XAxis * Cylinder->Radius + Frame.ZAxis * V1 }, { 1.0, 1.0 });
        return ConstructNurbsRevolution(*Profile.Definition, Frame, Start, FullTurn);
    }
    if (const auto* Cone = std::get_if<ConeSurface>(&Surface))
    {
        const auto [V0, V1] = Widen(BoxLow.Y, BoxHigh.Y);
        const OrthonormalBasis& Frame = Cone->Frame;
        // 📝 Signed radius: a profile crossing the apex sweeps both nappes, exactly as the analytic chart does.
        const auto Point = [&](double 𝑣) { return Frame.Origin + Frame.XAxis * (Cone->OriginRadius + 𝑣 * Cone->RadialSlope) + Frame.ZAxis * 𝑣; };
        const NurbsCurve Profile = *ConstructNurbsCurve(1, { V0, V0, V1, V1 }, { Point(V0), Point(V1) }, { 1.0, 1.0 });
        return ConstructNurbsRevolution(*Profile.Definition, Frame, Start, FullTurn);
    }
    if (const auto* Sphere = std::get_if<SphereSurface>(&Surface))
    {
        const OrthonormalBasis& Frame = Sphere->Frame;
        const ConicForm Meridian{ Frame.Origin, Frame.XAxis * Sphere->Radius, Frame.ZAxis * Sphere->Radius, Frame.YAxis * -1.0 };
        const NurbsCurve Profile = ConvertConicToNurbs(Meridian, -QuarterTurn, QuarterTurn);
        return ConstructNurbsRevolution(*Profile.Definition, Frame, Start, FullTurn);
    }
    const auto& Torus = std::get<TorusSurface>(Surface);
    const OrthonormalBasis& Frame = Torus.Frame;
    const ConicForm Tube{ Frame.Origin + Frame.XAxis * Torus.MajorRadius, Frame.XAxis * Torus.MinorRadius, Frame.ZAxis * Torus.MinorRadius, Frame.YAxis * -1.0 };
    const NurbsCurve Profile = ConvertConicToNurbs(Tube, BoxLow.Y, BoxLow.Y + FullTurn);
    return ConstructNurbsRevolution(*Profile.Definition, Frame, Start, FullTurn);
}

} // namespace Frontier::GeometrySpace::Parametric
