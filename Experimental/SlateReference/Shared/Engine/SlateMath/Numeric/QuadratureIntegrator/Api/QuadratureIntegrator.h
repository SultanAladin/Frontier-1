//============================================================================================================================================
//                                                        QUADRATUREINTEGRATOR.H
//============================================================================================================================================
// 🧩 Definite integral approximation over a declared domain — derived abscissae, ordered accumulation, no transcribed set.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE RULE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 A Gauss–Legendre rule over the reference interval, derived once and reused.
/// note  🔴 The abscissae are **derived**, never transcribed. A transcribed set is accurate to the digits
///        somebody typed, and the same three defects `ColourProjection` met apply here: a sign, a place, and a
///        normalisation, each individually plausible. Newton on the Legendre recurrence has one place to be
///        wrong and converges to the last representable bit from the standard initial estimate.
/// note  🔴 The rule is derived **once** and integrated against many times. Deriving it per integral would make
///        `28`'s three surface builds pay a root-finding solve at every one of a million cells, and would make
///        the abscissae a cost rather than a constant.
/// note  📐 Declared Bounded, per `02` §5. The guarantee is over the **accumulation**, not over the rule's
///        agreement with the true integral: given the abscissa magnitudes the weighted sum is accumulated in
///        ordinal order and is therefore the same number on every machine and every run — `02` §5's ordered
///        recombination. How well the rule approximates the integrand is fixed by the abscissa count the caller
///        declares, and is the caller's declaration rather than this component's promise.
/// tag   owning
class QuadratureRule
{
public:

    // 📝 Read by this component alone, so `00` §2 keeps it here. Beyond about a hundred abscissae the Legendre
    //    roots crowd the interval ends closely enough that the derivation's own conditioning, rather than the
    //    rule's order, decides the result — so the ceiling is a correctness bound and not merely a budget.
    static constexpr std::uint32_t AbscissaLimit = 128u;   // [-] - abscissae one rule may carry

    /// 🧩 Derives the rule of a declared abscissa count.
    /// in    Requested  [-]  abscissae; a rule of n integrates a polynomial of degree 2n−1 exactly
    /// out   Result    [-]  refuses with ContentUnsupported for zero, and with ExtentExhausted above the ceiling
    /// post  the abscissae are in ascending order and the weights sum to the reference interval's width
    /// note  📐 Only half the roots are solved for. The Legendre polynomials are even or odd, so the roots are
    ///        symmetric about the origin and the weights with them — solving both halves would be solving the
    ///        same equation twice and would let the two halves disagree in their last bit.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Derive(std::uint32_t Requested);

    /// 🧩 One abscissa of the reference interval, in ascending order.
    /// pre   Index is below DeclaredCount
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    double Abscissa(std::uint32_t Index) const;

    /// 🧩 The weight that abscissa carries.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    double Weight(std::uint32_t Index) const;

    /// 🧩 Projects one abscissa onto a declared interval, weight included.
    /// in    Index   [-]  the abscissa
    /// in    Lower     [-]  the interval's lower bound
    /// in    Upper     [-]  its upper bound
    /// out   Position  [-]  where the abscissa lands
    /// out   Weighting [-]  the weight scaled to the interval
    /// out   Result   [-]  refuses with ExtentExhausted outside the declared count, and with ContentUnsupported
    ///                      before the rule is derived
    /// note  📝 Exposed so that a caller integrating several components at once accumulates them side by side in
    ///        one walk, in ordinal order. `28` integrates three extinction components along one ray, and three
    ///        separate scalar integrations would evaluate the same density profile three times.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Project(std::uint32_t Index,
                          double        Lower,
                          double        Upper,
                          double&       Position,
                          double&       Weighting) const;

    /// 🧩 Integrates one scalar integrand over a declared interval.
    /// in    Lower     [-]  the interval's lower bound
    /// in    Upper     [-]  its upper bound
    /// in    Evaluate  [-]  the integrand; called once per abscissa, in ordinal order
    /// out   Integral  [-]  zero for a degenerate interval, and zero before the rule is derived
    /// note  🔴 Accumulated in ordinal order, never in whatever order a compiler finds convenient. `02` §5: a sum
    ///        in arrival order is a different number each run at Bounded, and here the difference reaches the
    ///        artist as an atmosphere whose horizon shifts between two runs of an unchanged document.
    /// note  ⚠️ An inverted interval integrates to the negation of the forward one rather than refusing, which is
    ///        what the definite integral means and what a caller reversing a ray direction relies on.
    /// cost  🚩
    /// tag   api, nonthrowing
    template <typename Integrand>
    double IntegrateInterval(double Lower, double Upper, Integrand Evaluate) const
    {
        if (!RuleDerived || Upper == Lower)
            return 0.0;

        const double HalfSpan = (Upper - Lower) * 0.5;
        const double Middle   = (Upper + Lower) * 0.5;

        double Accumulated = 0.0;

        for (std::uint32_t Index = 0u; Index < DeclaredAbscissae.size(); ++Index)
            Accumulated += DeclaredWeights[Index] * Evaluate(Middle + HalfSpan * DeclaredAbscissae[Index]);

        return Accumulated * HalfSpan;
    }

    /// 🧩 How many abscissae the rule carries.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t DeclaredCount() const;

    /// 🧩 Whether the rule has been derived at all.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Derived() const;

private:

    std::vector<double>  DeclaredAbscissae;       // [-] - ascending, on the reference interval
    std::vector<double>  DeclaredWeights;         // [-] - parallel to them
    bool                 RuleDerived = false;     // [-] - Derive delivered
};

// 📐 Both the derivation and the accumulation are continuous, so the component claims Bounded. Nothing here
//    claims Exact: the abscissae are Newton iterates and the weighted sum is a floating-point accumulation.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

}   // namespace Slate
