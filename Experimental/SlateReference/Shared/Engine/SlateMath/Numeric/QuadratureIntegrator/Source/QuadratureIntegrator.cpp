//============================================================================================================================================
//                                                       QUADRATUREINTEGRATOR.CPP
//============================================================================================================================================
// 🧩 Newton on the Legendre recurrence, solved over half the interval and mirrored onto the other.

#include "SlateMath/Numeric/QuadratureIntegrator/Api/QuadratureIntegrator.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE DERIVATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> QuadratureRule::Derive(std::uint32_t Requested)
{
    if (Requested == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a rule of no abscissa integrates nothing" });

    if (Requested > AbscissaLimit)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the abscissa ceiling was reached" });

    DeclaredAbscissae.assign(Requested, 0.0);
    DeclaredWeights.assign(Requested, 0.0);

    const double      Order = static_cast<double>(Requested);
    const std::uint32_t Half = (Requested + 1u) / 2u;

    for (std::uint32_t Index = 0u; Index < Half; ++Index)
    {
        // 📐 The standard initial estimate for the Index-th root of the Legendre polynomial, accurate to within
        //    a few thousandths. Newton's quadratic convergence then reaches the last representable bit in four or
        //    five iterations; a poorer estimate would still converge but to whichever root it fell nearest, and
        //    two roots found for one ordinal is a rule with a duplicated abscissa and a doubled weight.
        double Root = std::cos(Pi * (static_cast<double>(Index) + 0.75) / (Order + 0.5));

        double Derivative = 1.0;

        for (std::uint32_t Passed = 0u; Passed < QuadratureIterationLimit; ++Passed)
        {
            // 📐 The three-term recurrence (j+1)P₍ⱼ₊₁₎ = (2j+1)xPⱼ − jP₍ⱼ₋₁₎, walked up to the declared order.
            //    Evaluating the closed form instead would need the binomial coefficients, which overflow long
            //    before the abscissa ceiling does.
            double Leading  = 1.0;   // Pⱼ
            double Trailing = 0.0;   // P₍ⱼ₋₁₎

            for (std::uint32_t Degree = 0u; Degree < Requested; ++Degree)
            {
                const double Preceding = Trailing;

                Trailing = Leading;
                Leading  = ((2.0 * static_cast<double>(Degree) + 1.0) * Root * Trailing
                          - static_cast<double>(Degree) * Preceding)
                         / (static_cast<double>(Degree) + 1.0);
            }

            // 📐 P′ₙ(x) = n(xPₙ − P₍ₙ₋₁₎)/(x²−1). The denominator vanishes only at the interval's own bounds, and
            //    no Legendre root sits there — the roots are strictly interior for every order.
            Derivative = Order * (Root * Leading - Trailing) / (Root * Root - 1.0);

            const double Step = Leading / Derivative;

            Root -= Step;

            if (std::fabs(Step) <= QuadratureConvergence)
                break;
        }

        // 📝 Mirrored rather than solved twice. The weight is even in the root, so both halves take the same one.
        DeclaredAbscissae[Index]                 = -Root;
        DeclaredAbscissae[Requested - 1u - Index] =  Root;

        const double Weighting = 2.0 / ((1.0 - Root * Root) * Derivative * Derivative);

        DeclaredWeights[Index]                 = Weighting;
        DeclaredWeights[Requested - 1u - Index] = Weighting;
    }

    RuleDerived = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

double QuadratureRule::Abscissa(std::uint32_t Index) const
{
    return Index < DeclaredAbscissae.size() ? DeclaredAbscissae[Index] : 0.0;
}

double QuadratureRule::Weight(std::uint32_t Index) const
{
    return Index < DeclaredWeights.size() ? DeclaredWeights[Index] : 0.0;
}

Deliver<bool> QuadratureRule::Project(std::uint32_t Index,
                                      double        Lower,
                                      double        Upper,
                                      double&       Position,
                                      double&       Weighting) const
{
    if (!RuleDerived)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the rule has not been derived" });

    if (Index >= DeclaredAbscissae.size())
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "no such abscissa" });

    const double HalfSpan = (Upper - Lower) * 0.5;
    const double Middle   = (Upper + Lower) * 0.5;

    Position  = Middle + HalfSpan * DeclaredAbscissae[Index];
    Weighting = HalfSpan * DeclaredWeights[Index];

    return Deliver<bool>::Result(true);
}

std::uint32_t QuadratureRule::DeclaredCount() const
{
    return static_cast<std::uint32_t>(DeclaredAbscissae.size());
}

bool QuadratureRule::Derived() const
{
    return RuleDerived;
}

}   // namespace Slate
