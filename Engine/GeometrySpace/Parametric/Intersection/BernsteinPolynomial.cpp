/*====================================================================================================================================
                                                    BERNSTEINPOLYNOMIAL.CPP
====================================================================================================================================*/
// 🧩 Bernstein arithmetic (de Casteljau, elevation, products) and Bézier-clipping root isolation

#include "Engine/GeometrySpace/Parametric/Intersection/BernsteinPolynomial.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr int    RecursionLimit  = 96;                                                   // [-] - Nesting guard
    constexpr double ClipShrinkLimit = 0.8;                                                  // [-] - Halve when a clip keeps more

    [[nodiscard]] double Binomial(int Count, int Choice)
    {
        double Product = 1.0;                                                                // [-] - C(n, k) built incrementally
        for (int Step = 1; Step <= Choice; ++Step) Product = Product * (Count - Choice + Step) / Step;
        return Product;
    }

    // 📝 The convex hull of {(i/n, cᵢ)} meets the axis in [Low, High]; its end points lie on hull edges, i.e. on segments
    //    between control points of opposite sign — so scanning all such pairs (O(n²), n ≤ 8) gives the exact interval.
    struct HullCrossing
    {
        bool   CrossingCondition = false;                                                    // [-] - Hull touches 𝑓 = 0
        double Low               = 1.0;                                                      // [-] - Leftmost crossing
        double High              = 0.0;                                                      // [-] - Rightmost crossing
    };

    [[nodiscard]] HullCrossing IntersectHullWithAxis(const std::vector<double>& Coefficients)
    {
        HullCrossing Crossing;
        const int    Degree = static_cast<int>(Coefficients.size()) - 1;                     // [-] - n
        for (int First = 0; First <= Degree; ++First)
        {
            const double Abscissa = static_cast<double>(First) / Degree;                     // [-] - i/n
            if (Coefficients[First] == 0.0)
            {
                Crossing.CrossingCondition = true;
                Crossing.Low  = std::min(Crossing.Low, Abscissa);
                Crossing.High = std::max(Crossing.High, Abscissa);
            }
            for (int Second = First + 1; Second <= Degree; ++Second)
            {
                if ((Coefficients[First] < 0.0) == (Coefficients[Second] < 0.0) || Coefficients[First] == 0.0 || Coefficients[Second] == 0.0) continue;
                const double λ    = Coefficients[First] / (Coefficients[First] - Coefficients[Second]);   // [-] - Zero along the segment
                const double Root = Abscissa + λ * (static_cast<double>(Second - First) / Degree);
                Crossing.CrossingCondition = true;
                Crossing.Low  = std::min(Crossing.Low, Root);
                Crossing.High = std::max(Crossing.High, Root);
            }
        }
        return Crossing;
    }

    void ClipRecursively(const BernsteinPolynomial& Polynomial, double Low, double High, double Tolerance, int Depth, RootIsolation& Isolation)
    {
        const HullCrossing Crossing = IntersectHullWithAxis(Polynomial.Coefficients);
        if (!Crossing.CrossingCondition) return;
        if (High - Low <= Tolerance || Depth >= RecursionLimit)
        {
            Isolation.Roots.push_back(0.5 * (Low + High));
            return;
        }
        const double Span = High - Low;                                                      // [-] - Current interval width
        if (Crossing.High - Crossing.Low > ClipShrinkLimit)
        {
            Isolation.Subdivisions += 1;
            const BernsteinSplit Split = SubdivideBernstein(Polynomial, 0.5);
            ClipRecursively(Split.Lower, Low, Low + 0.5 * Span, Tolerance, Depth + 1, Isolation);
            ClipRecursively(Split.Upper, Low + 0.5 * Span, High, Tolerance, Depth + 1, Isolation);
            return;
        }
        Isolation.ClipSteps += 1;
        // ⚠️ Widen the clip by a few ulps of the interval so rounding in the hull cannot cut a root off.
        const double Margin   = 4.0 * std::numeric_limits<double>::epsilon();
        const double ClipLow  = std::max(0.0, Crossing.Low - Margin);
        const double ClipHigh = std::min(1.0, Crossing.High + Margin);
        if (ClipHigh - ClipLow >= 1.0)
        {
            Isolation.Roots.push_back(0.5 * (Low + High));
            return;
        }
        ClipRecursively(RestrictBernstein(Polynomial, ClipLow, ClipHigh), Low + ClipLow * Span, Low + ClipHigh * Span, Tolerance, Depth + 1, Isolation);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

double EvaluateBernstein(const BernsteinPolynomial& Polynomial, double 𝑡)
{
    std::vector<double> Ladder = Polynomial.Coefficients;                                    // [-] - de Casteljau rows
    for (size_t Row = 1; Row < Ladder.size(); ++Row)
    {
        for (size_t Column = 0; Column + Row < Ladder.size(); ++Column) Ladder[Column] = (1.0 - 𝑡) * Ladder[Column] + 𝑡 * Ladder[Column + 1];
    }
    return Ladder.empty() ? 0.0 : Ladder.front();
}

BernsteinSplit SubdivideBernstein(const BernsteinPolynomial& Polynomial, double 𝑡)
{
    const size_t        Count  = Polynomial.Coefficients.size();                             // [-] - n + 1
    std::vector<double> Ladder = Polynomial.Coefficients;                                    // [-] - de Casteljau working row
    BernsteinSplit      Split;
    Split.Lower.Coefficients.resize(Count);
    Split.Upper.Coefficients.resize(Count);
    for (size_t Row = 0; Row < Count; ++Row)
    {
        Split.Lower.Coefficients[Row]             = Ladder.front();
        Split.Upper.Coefficients[Count - 1 - Row] = Ladder[Count - 1 - Row];
        for (size_t Column = 0; Column + Row + 1 < Count; ++Column) Ladder[Column] = (1.0 - 𝑡) * Ladder[Column] + 𝑡 * Ladder[Column + 1];
    }
    return Split;
}

BernsteinPolynomial RestrictBernstein(const BernsteinPolynomial& Polynomial, double Low, double High)
{
    if (High >= 1.0) return Low <= 0.0 ? Polynomial : SubdivideBernstein(Polynomial, Low).Upper;
    const BernsteinPolynomial Lower = SubdivideBernstein(Polynomial, High).Lower;            // [-] - On [0, High]
    return Low <= 0.0 ? Lower : SubdivideBernstein(Lower, Low / High).Upper;
}

BernsteinPolynomial ElevateBernstein(const BernsteinPolynomial& Polynomial, int Degree)
{
    BernsteinPolynomial Elevated = Polynomial;
    while (Elevated.Degree() < Degree)
    {
        const int           Current = Elevated.Degree();                                     // [-] - n
        std::vector<double> Raised(static_cast<size_t>(Current) + 2);
        Raised.front() = Elevated.Coefficients.front();
        Raised.back()  = Elevated.Coefficients.back();
        for (int Index = 1; Index <= Current; ++Index)
        {
            const double α = static_cast<double>(Index) / (Current + 1);                     // [-] - Elevation weight
            Raised[Index] = α * Elevated.Coefficients[Index - 1] + (1.0 - α) * Elevated.Coefficients[Index];
        }
        Elevated.Coefficients = std::move(Raised);
    }
    return Elevated;
}

BernsteinPolynomial AddBernstein(const BernsteinPolynomial& First, const BernsteinPolynomial& Second)
{
    const int           Degree = std::max(First.Degree(), Second.Degree());                  // [-] - Common degree
    BernsteinPolynomial Sum    = ElevateBernstein(First, Degree);
    const BernsteinPolynomial Other = ElevateBernstein(Second, Degree);
    for (size_t Index = 0; Index < Sum.Coefficients.size(); ++Index) Sum.Coefficients[Index] += Other.Coefficients[Index];
    return Sum;
}

BernsteinPolynomial ScaleBernstein(const BernsteinPolynomial& Polynomial, double Scale)
{
    BernsteinPolynomial Scaled = Polynomial;
    for (double& Coefficient : Scaled.Coefficients) Coefficient *= Scale;
    return Scaled;
}

BernsteinPolynomial MultiplyBernstein(const BernsteinPolynomial& First, const BernsteinPolynomial& Second)
{
    // 📝 c_k = Σ_{i+j=k} C(m,i)·C(n,j)/C(m+n,k) · aᵢ·bⱼ
    const int           FirstDegree  = First.Degree();
    const int           SecondDegree = Second.Degree();
    BernsteinPolynomial Product;
    Product.Coefficients.assign(static_cast<size_t>(FirstDegree + SecondDegree) + 1, 0.0);
    for (int FirstIndex = 0; FirstIndex <= FirstDegree; ++FirstIndex)
    {
        for (int SecondIndex = 0; SecondIndex <= SecondDegree; ++SecondIndex)
        {
            const double Weight = Binomial(FirstDegree, FirstIndex) * Binomial(SecondDegree, SecondIndex) / Binomial(FirstDegree + SecondDegree, FirstIndex + SecondIndex);
            Product.Coefficients[FirstIndex + SecondIndex] += Weight * First.Coefficients[FirstIndex] * Second.Coefficients[SecondIndex];
        }
    }
    return Product;
}

BernsteinPolynomial DifferentiateBernstein(const BernsteinPolynomial& Polynomial)
{
    const int           Degree = Polynomial.Degree();                                        // [-] - n
    BernsteinPolynomial Derivative;
    if (Degree < 1) return { { 0.0 } };
    Derivative.Coefficients.resize(static_cast<size_t>(Degree));
    for (int Index = 0; Index < Degree; ++Index) Derivative.Coefficients[Index] = Degree * (Polynomial.Coefficients[Index + 1] - Polynomial.Coefficients[Index]);
    return Derivative;
}

BernsteinPolynomial ConstructFromRoots(const std::vector<double>& Roots, double Scale)
{
    BernsteinPolynomial Product{ { Scale } };
    for (double Root : Roots) Product = MultiplyBernstein(Product, BernsteinPolynomial{ { -Root, 1.0 - Root } });
    return Product;
}

RootIsolation IsolateBernsteinRoots(const BernsteinPolynomial& Polynomial, double Tolerance)
{
    RootIsolation Isolation;
    if (Polynomial.Coefficients.size() < 2) return Isolation;
    // ⚠️ The zero polynomial has no isolated roots (every subinterval would report one); callers treat it as coincidence.
    if (std::all_of(Polynomial.Coefficients.begin(), Polynomial.Coefficients.end(), [](double Coefficient) { return Coefficient == 0.0; })) return Isolation;
    ClipRecursively(Polynomial, 0.0, 1.0, Tolerance, 0, Isolation);
    std::sort(Isolation.Roots.begin(), Isolation.Roots.end());
    // 📝 A root on a subdivision point is reported by both halves; merge neighbours closer than the tolerance.
    std::vector<double> Merged;
    for (double Root : Isolation.Roots)
    {
        if (!Merged.empty() && Root - Merged.back() <= 2.0 * Tolerance) continue;
        Merged.push_back(Root);
    }
    Isolation.Roots = std::move(Merged);
    return Isolation;
}

} // namespace Frontier::GeometrySpace::Parametric
