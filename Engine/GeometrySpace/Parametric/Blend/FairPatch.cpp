/*====================================================================================================================================
                                                         FAIRPATCH.CPP
====================================================================================================================================*/
// 🧩 Energy-fair rational quads: homogeneous Bézier algebra, Boehm refinement, Coons start and the fairness solve

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Blend/FairPatch.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr int    QuadDegree      = 3;                                                        // [-] - Bicubic quads
constexpr double TangentWeight   = 1000.0;                                                     // [-] - G1 rows against fairness rows
constexpr double TensionWeight   = 30.0;                                                     // [-] - Cross-derivative magnitude rows (no collapse)
constexpr int    RimSamplesSpan  = 4;                                                        // [-] - G1 rows per span and side
constexpr double CornerMargin    = 0.04;                                                     // [-] - 𝑡 excluded at each side end
constexpr double CornerTolerance = 1e-9;                                                     // [len] - Sides must meet
constexpr int    BreakSamples    = 48;                                                       // [-] - Measurement samples per side

//------------------------------------------------------------------------------------------------------------------------
//                                                    HOMOGENEOUS POINTS
//------------------------------------------------------------------------------------------------------------------------

struct Homogeneous
{
    Vector3 Scaled = {};                                                                     // [len] - w·P
    double  Weight = 1.0;                                                                    // [-]   - w

    Homogeneous operator+(const Homogeneous& Other) const noexcept { return { Scaled + Other.Scaled, Weight + Other.Weight }; }
    Homogeneous operator-(const Homogeneous& Other) const noexcept { return { Scaled - Other.Scaled, Weight - Other.Weight }; }
    Homogeneous operator*(double Scale) const noexcept { return { Scaled * Scale, Weight * Scale }; }
    [[nodiscard]] Vector3 Point() const noexcept { return Scaled * (1.0 / Weight); }
};

std::vector<Homogeneous> Lift(const RationalBezier& Curve)
{
    std::vector<Homogeneous> Lifted(Curve.Poles.size());
    for (std::size_t Index = 0; Index < Lifted.size(); ++Index)
        Lifted[Index] = { Curve.Poles[Index] * Curve.Weights[Index], Curve.Weights[Index] };
    return Lifted;
}

RationalBezier Drop(const std::vector<Homogeneous>& Lifted)
{
    RationalBezier Curve;
    for (const Homogeneous& Pole : Lifted)
    {
        Curve.Poles.push_back(Pole.Point());
        Curve.Weights.push_back(Pole.Weight);
    }
    return Curve;
}

// Möbius reparametrisation to end weights 1: wᵢ′ = wᵢ·cⁱ/w₀ with c = (w₀/wₚ)^(1/p); poles and point set unchanged.
RationalBezier Normalise(RationalBezier Curve)
{
    const int    Degree = static_cast<int>(Curve.Poles.size()) - 1;
    const double First  = Curve.Weights.front();
    const double Ratio  = std::pow(First / Curve.Weights.back(), 1.0 / Degree);
    double       Factor = 1.0;
    for (int Index = 0; Index <= Degree; ++Index, Factor *= Ratio)
        Curve.Weights[Index] *= Factor / First;
    return Curve;
}

// Bézier degree elevation p → p + 1 in homogeneous space: Qᵢ = (i/(p+1))·Pᵢ₋₁ + (1 − i/(p+1))·Pᵢ.
std::vector<Homogeneous> Elevate(const std::vector<Homogeneous>& Poles)
{
    const int                Degree = static_cast<int>(Poles.size()) - 1;
    std::vector<Homogeneous> Raised(Poles.size() + 1);
    Raised.front() = Poles.front();
    Raised.back()  = Poles.back();
    for (int Index = 1; Index <= Degree; ++Index)
    {
        const double Alpha = static_cast<double>(Index) / (Degree + 1);
        Raised[Index]      = Poles[Index - 1] * Alpha + Poles[Index] * (1.0 - Alpha);
    }
    return Raised;
}

// Boehm insertion of one knot into a clamped homogeneous B-spline (Piegl & Tiller A5.1, r = 1).
void InsertKnot(std::vector<double>& Knots, std::vector<Homogeneous>& Poles, int Degree, double Knot)
{
    int Span = Degree;
    while (Span + 1 < static_cast<int>(Poles.size()) && Knots[Span + 1] <= Knot)
        ++Span;
    std::vector<Homogeneous> Refined(Poles.size() + 1);
    for (int Index = 0; Index < static_cast<int>(Refined.size()); ++Index)
    {
        if (Index <= Span - Degree)
            Refined[Index] = Poles[Index];
        else if (Index > Span)
            Refined[Index] = Poles[Index - 1];
        else
        {
            const double Alpha = (Knot - Knots[Index]) / (Knots[Index + Degree] - Knots[Index]);
            Refined[Index]     = Poles[Index] * Alpha + Poles[Index - 1] * (1.0 - Alpha);
        }
    }
    Knots.insert(Knots.begin() + Span + 1, Knot);
    Poles = std::move(Refined);
}

// All basis values Nᵢ,ₚ(𝑡) over the clamped knot vector (dense, Count entries).
std::vector<double> EvaluateBasis(const std::vector<double>& Knots, int Degree, int Count, double 𝑡)
{
    int Span = Degree;
    while (Span + 1 < Count && Knots[Span + 1] <= 𝑡)
        ++Span;
    std::vector<double> Local(Degree + 1, 0.0), Left(Degree + 1, 0.0), Right(Degree + 1, 0.0);
    Local[0] = 1.0;
    for (int Level = 1; Level <= Degree; ++Level)
    {
        Left[Level]  = 𝑡 - Knots[Span + 1 - Level];
        Right[Level] = Knots[Span + Level] - 𝑡;
        double Saved = 0.0;
        for (int Index = 0; Index < Level; ++Index)
        {
            const double Term = Local[Index] / (Right[Index + 1] + Left[Level - Index]);
            Local[Index]      = Saved + Right[Index + 1] * Term;
            Saved             = Left[Level - Index] * Term;
        }
        Local[Level] = Saved;
    }
    std::vector<double> Values(Count, 0.0);
    for (int Index = 0; Index <= Degree; ++Index)
        Values[Span - Degree + Index] = Local[Index];
    return Values;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    NORMAL EQUATIONS
//------------------------------------------------------------------------------------------------------------------------

struct LeastSquares
{
    int                 Count = 0;                                                           // [-] - Scalar unknowns
    std::vector<double> Matrix = {};                                                         // [-] - AᵀA, row-major
    std::vector<double> Vector = {};                                                         // [-] - Aᵀb

    explicit LeastSquares(int Unknowns) : Count(Unknowns), Matrix(static_cast<std::size_t>(Unknowns) * Unknowns, 0.0), Vector(Unknowns, 0.0) {}

    void Accumulate(const std::vector<std::pair<int, double>>& Row, double Target, double Weight)
    {
        const double Square = Weight * Weight;
        for (const auto& [First, FirstValue] : Row)
        {
            Vector[First] += Square * FirstValue * Target;
            for (const auto& [Second, SecondValue] : Row)
                Matrix[static_cast<std::size_t>(First) * Count + Second] += Square * FirstValue * SecondValue;
        }
    }

    // Cholesky factorisation and solve; false when the system is not positive definite.
    bool Solve(std::vector<double>& Solution)
    {
        std::vector<double> Lower(Matrix.size(), 0.0);
        for (int Row = 0; Row < Count; ++Row)
            for (int Column = 0; Column <= Row; ++Column)
            {
                double Sum = Matrix[static_cast<std::size_t>(Row) * Count + Column];
                for (int Inner = 0; Inner < Column; ++Inner)
                    Sum -= Lower[static_cast<std::size_t>(Row) * Count + Inner] * Lower[static_cast<std::size_t>(Column) * Count + Inner];
                if (Row == Column)
                {
                    if (!(Sum > 0.0))
                        return false;
                    Lower[static_cast<std::size_t>(Row) * Count + Row] = std::sqrt(Sum);
                }
                else
                    Lower[static_cast<std::size_t>(Row) * Count + Column] = Sum / Lower[static_cast<std::size_t>(Column) * Count + Column];
            }
        Solution.assign(Count, 0.0);
        for (int Row = 0; Row < Count; ++Row)
        {
            double Sum = Vector[Row];
            for (int Inner = 0; Inner < Row; ++Inner)
                Sum -= Lower[static_cast<std::size_t>(Row) * Count + Inner] * Solution[Inner];
            Solution[Row] = Sum / Lower[static_cast<std::size_t>(Row) * Count + Row];
        }
        for (int Row = Count - 1; Row >= 0; --Row)
        {
            double Sum = Solution[Row];
            for (int Inner = Row + 1; Inner < Count; ++Inner)
                Sum -= Lower[static_cast<std::size_t>(Inner) * Count + Row] * Solution[Inner];
            Solution[Row] = Sum / Lower[static_cast<std::size_t>(Row) * Count + Row];
        }
        return true;
    }
};

double Component(Vector3 Vector, int Axis) noexcept
{
    return Axis == 0 ? Vector.X : (Axis == 1 ? Vector.Y : Vector.Z);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    RATIONAL BÉZIER
//------------------------------------------------------------------------------------------------------------------------

Vector3 EvaluateRationalBezier(const RationalBezier& Curve, double 𝑡) noexcept
{
    std::vector<Homogeneous> Work = Lift(Curve);
    for (std::size_t Level = 1; Level < Work.size(); ++Level)
        for (std::size_t Index = 0; Index + Level < Work.size(); ++Index)
            Work[Index] = Work[Index] * (1.0 - 𝑡) + Work[Index + 1] * 𝑡;
    return Work.front().Point();
}

std::pair<RationalBezier, RationalBezier> SplitRationalBezier(const RationalBezier& Curve, double 𝑡)
{
    std::vector<Homogeneous> Work = Lift(Curve);
    const std::size_t        Size = Work.size();
    std::vector<Homogeneous> Front(Size), Back(Size);
    Front[0]        = Work.front();
    Back[Size - 1]  = Work.back();
    for (std::size_t Level = 1; Level < Size; ++Level)
    {
        for (std::size_t Index = 0; Index + Level < Size; ++Index)
            Work[Index] = Work[Index] * (1.0 - 𝑡) + Work[Index + 1] * 𝑡;
        Front[Level]            = Work.front();
        Back[Size - 1 - Level]  = Work[Size - 1 - Level];
    }
    return { Normalise(Drop(Front)), Normalise(Drop(Back)) };
}

RationalBezier ReverseRationalBezier(const RationalBezier& Curve)
{
    RationalBezier Reversed = Curve;
    std::reverse(Reversed.Poles.begin(), Reversed.Poles.end());
    std::reverse(Reversed.Weights.begin(), Reversed.Weights.end());
    return Reversed;
}

RationalBezier ConstructArcBezier(Vector3 Centre, Vector3 From, Vector3 To)
{
    const Vector3 First   = From - Centre;
    const Vector3 Second  = To - Centre;
    const double  Radius  = Length(First);
    const double  Sweep   = std::atan2(Length(Cross(First, Second)), Dot(First, Second));       // [rad] φ < π
    const double  Cosine  = std::cos(0.5 * Sweep);
    const Vector3 Control = Centre + Normalize(First + Second) * (Radius / Cosine);
    return { { From, Control, To }, { 1.0, Cosine, 1.0 } };
}

RationalBezier ConstructConicBezier(Vector3 From, Vector3 Control, Vector3 To, double Weight)
{
    return { { From, Control, To }, { 1.0, Weight, 1.0 } };
}

NurbsCurve ConvertBezierToNurbs(const RationalBezier& Curve)
{
    const int           Degree = static_cast<int>(Curve.Poles.size()) - 1;
    std::vector<double> Knots(Degree + 1, 0.0);
    Knots.insert(Knots.end(), Degree + 1, 1.0);
    return *ConstructNurbsCurve(Degree, std::move(Knots), Curve.Poles, Curve.Weights);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FAIR QUAD
//------------------------------------------------------------------------------------------------------------------------

FairQuadOutcome BuildFairQuad(const FairSide& Bottom, const FairSide& Right, const FairSide& Top, const FairSide& Left, int Spans)
{
    FairQuadOutcome Outcome;
    const FairSide* Sides[4] = { &Bottom, &Right, &Top, &Left };

    // ① corners meet with weight 1
    const auto Ends = [](const FairSide& Side) { return std::pair{ Side.Curve.Poles.front(), Side.Curve.Poles.back() }; };
    const bool Corners = Distance(Ends(Bottom).first, Ends(Left).first) <= CornerTolerance && Distance(Ends(Bottom).second, Ends(Right).first) <= CornerTolerance &&
                         Distance(Ends(Top).first, Ends(Left).second) <= CornerTolerance && Distance(Ends(Top).second, Ends(Right).second) <= CornerTolerance;
    if (!Corners)
    {
        Outcome.Refusal = "fair quad sides do not meet at their corners";
        return Outcome;
    }
    for (const FairSide* Side : Sides)
        if (Side->Curve.Poles.size() < 2 || static_cast<int>(Side->Curve.Poles.size()) > QuadDegree + 1 ||
            std::abs(Side->Curve.Weights.front() - 1.0) > 1e-12 || std::abs(Side->Curve.Weights.back() - 1.0) > 1e-12)
        {
            Outcome.Refusal = "fair quad side is not a normalised rational Bézier of degree ≤ 3";
            return Outcome;
        }

    // ② elevate to degree 3, refine to Spans uniform spans (opposite sides share the knot vector)
    std::vector<double>      Knots;
    std::vector<Homogeneous> Rows[4];
    for (int Index = 0; Index < 4; ++Index)
    {
        std::vector<Homogeneous> Poles = Lift(Sides[Index]->Curve);
        while (static_cast<int>(Poles.size()) < QuadDegree + 1)
            Poles = Elevate(Poles);
        std::vector<double> Local(QuadDegree + 1, 0.0);
        Local.insert(Local.end(), QuadDegree + 1, 1.0);
        for (int Span = 1; Span < Spans; ++Span)
            InsertKnot(Local, Poles, QuadDegree, static_cast<double>(Span) / Spans);
        Rows[Index] = std::move(Poles);
        Knots       = std::move(Local);
    }
    const int Count = Spans + QuadDegree;
    std::vector<double> Greville(Count);
    for (int Index = 0; Index < Count; ++Index)
        Greville[Index] = (Knots[Index + 1] + Knots[Index + 2] + Knots[Index + 3]) / 3.0;

    // ③ net: boundary rows fixed, interior from the discrete homogeneous Coons blend
    std::vector<Homogeneous> Net(static_cast<std::size_t>(Count) * Count);
    const auto At = [&](int I, int J) -> Homogeneous& { return Net[static_cast<std::size_t>(I) * Count + J]; };
    for (int I = 0; I < Count; ++I)
        for (int J = 0; J < Count; ++J)
        {
            const double U = Greville[I], V = Greville[J];
            const Homogeneous Ruled = Rows[0][I] * (1.0 - V) + Rows[2][I] * V + Rows[3][J] * (1.0 - U) + Rows[1][J] * U;
            const Homogeneous Bilinear = Rows[0][0] * ((1.0 - U) * (1.0 - V)) + Rows[0][Count - 1] * (U * (1.0 - V)) + Rows[2][0] * ((1.0 - U) * V) + Rows[2][Count - 1] * (U * V);
            At(I, J) = Ruled - Bilinear;
        }
    for (int K = 0; K < Count; ++K)
    {
        At(K, 0)         = Rows[0][K];
        At(K, Count - 1) = Rows[2][K];
        At(0, K)         = Rows[3][K];
        At(Count - 1, K) = Rows[1][K];
    }
    for (const Homogeneous& Pole : Net)
        if (!(Pole.Weight > 0.0))
        {
            Outcome.Refusal = "fair quad Coons weights are not positive";
            return Outcome;
        }

    const std::vector<Homogeneous> Start = Net;                                              // Coons net (tension reference)

    // ④ unknowns: interior scaled poles (weights fixed) — 3 scalars each
    const int  Inner   = Count - 2;
    const auto Unknown = [&](int I, int J) { return (I > 0 && I < Count - 1 && J > 0 && J < Count - 1) ? ((I - 1) * Inner + (J - 1)) : -1; };
    LeastSquares System(3 * Inner * Inner);

    // Fairness: second differences along 𝑢, 𝑣 and the twist of the scaled net, per coordinate.
    const auto Fair = [&](const std::vector<std::tuple<int, int, double>>& Stencil)
    {
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            std::vector<std::pair<int, double>> Row;
            double                              Target = 0.0;
            for (const auto& [I, J, Coefficient] : Stencil)
            {
                const int Index = Unknown(I, J);
                if (Index >= 0)
                    Row.push_back({ 3 * Index + Axis, Coefficient });
                else
                    Target -= Coefficient * Component(At(I, J).Scaled, Axis);
            }
            if (!Row.empty())
                System.Accumulate(Row, Target, 1.0);
        }
    };
    for (int I = 0; I < Count; ++I)
        for (int J = 0; J < Count; ++J)
        {
            if (I > 0 && I < Count - 1)
                Fair({ { I - 1, J, 1.0 }, { I, J, -2.0 }, { I + 1, J, 1.0 } });
            if (J > 0 && J < Count - 1)
                Fair({ { I, J - 1, 1.0 }, { I, J, -2.0 }, { I, J + 1, 1.0 } });
            if (I < Count - 1 && J < Count - 1)
                Fair({ { I, J, 1.0 }, { I + 1, J, -1.0 }, { I, J + 1, -1.0 }, { I + 1, J + 1, 1.0 } });
        }

    // G1 rows: Σᵢ Nᵢ(𝑡)·[(Q_in − Q_rim) − S(𝑡)·(w_in − w_rim)]·n(𝑡) = 0 along each supported side.
    //    Side walk: (rim index, inner index) for lattice position K along the side.
    const auto Walk = [&](int Side, int K, int Depth) -> std::pair<int, int>
    {
        switch (Side)
        {
            case 0:  return { K, Depth };
            case 1:  return { Count - 1 - Depth, K };
            case 2:  return { K, Count - 1 - Depth };
            default: return { Depth, K };
        }
    };
    const int Samples = RimSamplesSpan * Spans;
    for (int Side = 0; Side < 4; ++Side)
    {
        if (!Sides[Side]->Normal)
            continue;
        for (int Sample = 0; Sample <= Samples; ++Sample)
        {
            const double        𝑡      = CornerMargin + (1.0 - 2.0 * CornerMargin) * Sample / Samples;
            const Vector3       Normal = Sides[Side]->Normal(𝑡);
            const Vector3       Rim    = EvaluateRationalBezier(Sides[Side]->Curve, 𝑡);
            const std::vector<double> Basis = EvaluateBasis(Knots, QuadDegree, Count, 𝑡);
            std::vector<std::pair<int, double>> Row;
            double                              Target = 0.0;
            for (int K = 0; K < Count; ++K)
            {
                if (Basis[K] == 0.0)
                    continue;
                const auto [RimI, RimJ]     = Walk(Side, K, 0);
                const auto [InnerI, InnerJ] = Walk(Side, K, 1);
                const Homogeneous& Outer    = At(RimI, RimJ);
                const Homogeneous& Next     = At(InnerI, InnerJ);
                Target += Basis[K] * (Dot(Outer.Scaled, Normal) + Dot(Rim, Normal) * (Next.Weight - Outer.Weight));
                const int Index = Unknown(InnerI, InnerJ);
                if (Index >= 0)
                {
                    Row.push_back({ 3 * Index + 0, Basis[K] * Normal.X });
                    Row.push_back({ 3 * Index + 1, Basis[K] * Normal.Y });
                    Row.push_back({ 3 * Index + 2, Basis[K] * Normal.Z });
                }
                else
                    Target -= Basis[K] * Dot(Next.Scaled, Normal);
            }
            if (!Row.empty())
                System.Accumulate(Row, Target, TangentWeight);

            // Tension: the same cross expression as a vector, held at the Coons value laid into the tangent plane, so the
            //    G1 rows cannot be met by collapsing the cross derivative (a degenerate rim).
            Vector3 Coons{}, Known{};
            std::vector<std::pair<int, double>> Vectorial;
            for (int K = 0; K < Count; ++K)
            {
                if (Basis[K] == 0.0)
                    continue;
                const auto [RimI, RimJ]     = Walk(Side, K, 0);
                const auto [InnerI, InnerJ] = Walk(Side, K, 1);
                const Homogeneous& Outer    = At(RimI, RimJ);
                const Homogeneous& Next     = Start[static_cast<std::size_t>(InnerI) * Count + InnerJ];
                Coons = Coons + (Next.Scaled - Outer.Scaled - Rim * (Next.Weight - Outer.Weight)) * Basis[K];
                Known = Known + (Outer.Scaled + Rim * (Next.Weight - Outer.Weight)) * Basis[K];
                const int Index = Unknown(InnerI, InnerJ);
                if (Index >= 0)
                    Vectorial.push_back({ Index, Basis[K] });
                else
                    Known = Known - Next.Scaled * Basis[K];
            }
            const Vector3 Wanted = Known + (Coons - Normal * Dot(Coons, Normal));
            for (int Axis = 0; Axis < 3 && !Vectorial.empty(); ++Axis)
            {
                std::vector<std::pair<int, double>> Component3;
                for (const auto& [Index, Value] : Vectorial)
                    Component3.push_back({ 3 * Index + Axis, Value });
                System.Accumulate(Component3, Component(Wanted, Axis), TensionWeight);
            }
        }
    }

    std::vector<double> Solution;
    if (!System.Solve(Solution))
    {
        Outcome.Refusal = "fair quad normal equations are singular";
        return Outcome;
    }
    for (int I = 1; I < Count - 1; ++I)
        for (int J = 1; J < Count - 1; ++J)
        {
            const int Index    = Unknown(I, J);
            At(I, J).Scaled    = { Solution[3 * Index], Solution[3 * Index + 1], Solution[3 * Index + 2] };
        }

    // ⑤ carrier
    std::vector<Vector3> Poles;
    std::vector<double>  Weights;
    for (const Homogeneous& Pole : Net)
    {
        Poles.push_back(Pole.Point());
        Weights.push_back(Pole.Weight);
    }
    Outcome.Surface = ConstructNurbsSurface(QuadDegree, QuadDegree, Count, Count, Knots, Knots, std::move(Poles), std::move(Weights));
    if (!Outcome.Surface)
    {
        Outcome.Refusal = "fair quad carrier is invalid";
        return Outcome;
    }

    // ⑥ measurement: G0 gap on every side, G1 break on supported sides (corners excluded)
    const NurbsSurfaceDefinition& Definition = *Outcome.Surface->Definition;
    for (int Side = 0; Side < 4; ++Side)
        for (int Sample = 0; Sample <= BreakSamples; ++Sample)
        {
            const double  𝑡         = static_cast<double>(Sample) / BreakSamples;
            const Vector2 Parameter = Side == 0 ? Vector2{ 𝑡, 0.0 } : (Side == 1 ? Vector2{ 1.0, 𝑡 } : (Side == 2 ? Vector2{ 𝑡, 1.0 } : Vector2{ 0.0, 𝑡 }));
            const NurbsSurfaceDerivatives Derivatives = EvaluateNurbsSurfaceDerivatives(Definition, Parameter);
            Outcome.RimGap = std::max(Outcome.RimGap, Distance(Derivatives.Position, EvaluateRationalBezier(Sides[Side]->Curve, 𝑡)));
            if (!Sides[Side]->Normal || 𝑡 < CornerMargin || 𝑡 > 1.0 - CornerMargin)
                continue;
            const Vector3 Surface = Cross(Derivatives.U, Derivatives.V);
            const Vector3 Target  = Sides[Side]->Normal(𝑡);
            const bool    Degenerate = Length(Surface) <= 1.0e-9 * Length(Derivatives.U) * Length(Derivatives.V) || Length(Surface) == 0.0;
            const double  Angle   = Degenerate ? 0.5 * std::numbers::pi : std::atan2(Length(Cross(Surface, Target)), std::abs(Dot(Surface, Target)));
            Outcome.SideBreaks[Side] = std::max(Outcome.SideBreaks[Side], Angle);
            Outcome.TangentBreak     = std::max(Outcome.TangentBreak, Angle);
        }
    return Outcome;
}

} // namespace Frontier::GeometrySpace::Parametric
