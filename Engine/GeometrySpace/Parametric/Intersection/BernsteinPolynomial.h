/*====================================================================================================================================
                                                     BERNSTEINPOLYNOMIAL.H
====================================================================================================================================*/
// 🧩 Univariate Bernstein polynomials on [0, 1]: arithmetic, subdivision, and root isolation by Bézier clipping

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 f(𝑡) = Σ cᵢ·Bᵢⁿ(𝑡), Bᵢⁿ(𝑡) = C(n, i)·𝑡ⁱ·(1 − 𝑡)ⁿ⁻ⁱ, 𝑡 ∈ [0, 1]. The control polygon (i/n, cᵢ) bounds the graph
//    (convex-hull property) — the whole root solver rests on that one fact.
struct BernsteinPolynomial
{
    std::vector<double> Coefficients = {};                                                   // [-] - c₀ … cₙ

    [[nodiscard]] int Degree() const noexcept { return static_cast<int>(Coefficients.size()) - 1; }
};

struct BernsteinSplit
{
    BernsteinPolynomial Lower = {};                                                          // [-] - Restriction to [0, 𝑡]
    BernsteinPolynomial Upper = {};                                                          // [-] - Restriction to [𝑡, 1]
};

struct RootIsolation
{
    std::vector<double> Roots         = {};                                                  // [-] - Ascending, merged within tolerance
    int                 ClipSteps     = 0;                                                   // [-] - Convex-hull clips applied
    int                 Subdivisions  = 0;                                                   // [-] - Halvings (clip shrank < 20 %)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] double              EvaluateBernstein(const BernsteinPolynomial& Polynomial, double 𝑡);
[[nodiscard]] BernsteinSplit      SubdivideBernstein(const BernsteinPolynomial& Polynomial, double 𝑡);
[[nodiscard]] BernsteinPolynomial RestrictBernstein(const BernsteinPolynomial& Polynomial, double Low, double High);
[[nodiscard]] BernsteinPolynomial ElevateBernstein(const BernsteinPolynomial& Polynomial, int Degree);
[[nodiscard]] BernsteinPolynomial AddBernstein(const BernsteinPolynomial& First, const BernsteinPolynomial& Second);
[[nodiscard]] BernsteinPolynomial ScaleBernstein(const BernsteinPolynomial& Polynomial, double Scale);
[[nodiscard]] BernsteinPolynomial MultiplyBernstein(const BernsteinPolynomial& First, const BernsteinPolynomial& Second);
[[nodiscard]] BernsteinPolynomial DifferentiateBernstein(const BernsteinPolynomial& Polynomial);

// Product of linear factors (𝑡 − rᵢ) in Bernstein form — the validation's polynomials with known roots.
[[nodiscard]] BernsteinPolynomial ConstructFromRoots(const std::vector<double>& Roots, double Scale);

// 💡 Bézier clipping (Sederberg–Nishita 1990): clip [0, 1] to the interval where the control polygon's convex hull meets
//    𝑓 = 0; halve when a clip removes < 20 %. Quadratic convergence at simple roots. Every root where 𝑓 CHANGES SIGN is
//    found; an even-multiplicity root may be missed when rounding lifts the hull off the axis — callers add the roots of
//    𝑓′ as tangency candidates (curve–surface intersection does).
[[nodiscard]] RootIsolation IsolateBernsteinRoots(const BernsteinPolynomial& Polynomial, double Tolerance);

} // namespace Frontier::GeometrySpace::Parametric
