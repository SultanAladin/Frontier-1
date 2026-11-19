/*====================================================================================================================================
                                                       EXACTPREDICATES.CPP
====================================================================================================================================*/
// 🧩 Floating-point-filtered exact orientation predicates (Shewchuk expansion arithmetic) for combinatorial decisions

#include "Engine/GeometrySpace/Parametric/Precision/ExactPredicates.h"

#include <cmath>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    EXPANSION ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 An expansion is a sum of non-overlapping doubles ordered by increasing magnitude; its sum is exact.
    //    Zero components are eliminated, so the sign of the expansion is the sign of its last component.
    using Expansion = std::vector<double>;

    constexpr double UnitRoundoff = 1.1102230246251565e-16;                                  // [-] - ε = 2⁻⁵³
    constexpr double Orient2DBound = (3.0 + 16.0 * UnitRoundoff) * UnitRoundoff;             // [-] - Shewchuk ccwerrboundA
    constexpr double Orient3DBound = (7.0 + 56.0 * UnitRoundoff) * UnitRoundoff;             // [-] - Shewchuk o3derrboundA
    constexpr double InCircleBound = (10.0 + 96.0 * UnitRoundoff) * UnitRoundoff;            // [-] - Shewchuk iccerrboundA

    // Exact sum: Sum + Residual == First + Second.
    inline void TwoSum(double First, double Second, double& Sum, double& Residual) noexcept
    {
        Sum                    = First + Second;
        const double Virtual   = Sum - First;                                                // [-] - Recovered share of Second
        Residual               = (First - (Sum - Virtual)) + (Second - Virtual);
    }

    // Exact sum when |Larger| ≥ |Smaller|.
    inline void FastTwoSum(double Larger, double Smaller, double& Sum, double& Residual) noexcept
    {
        Sum      = Larger + Smaller;
        Residual = Smaller - (Sum - Larger);
    }

    // 💡 Fused multiply-add yields the exact rounding error of a product in one instruction.
    inline void TwoProduct(double First, double Second, double& Product, double& Residual) noexcept
    {
        Product  = First * Second;
        Residual = std::fma(First, Second, -Product);
    }

    [[nodiscard]] Expansion ExactDifference(double First, double Second)
    {
        double Sum = 0.0;                                                                    // [-] - Rounded difference
        double Residual = 0.0;                                                               // [-] - Its exact error
        TwoSum(First, -Second, Sum, Residual);
        Expansion Result;
        if (Residual != 0.0) Result.push_back(Residual);
        if (Sum != 0.0)      Result.push_back(Sum);
        return Result;
    }

    // Shewchuk GROW-EXPANSION: add one double to an expansion.
    [[nodiscard]] Expansion GrowExpansion(const Expansion& Source, double Addend)
    {
        Expansion Result;
        double Accumulator = Addend;                                                         // [-] - Running high part
        for (double Component : Source)
        {
            double Residual = 0.0;                                                           // [-] - Exact low part
            TwoSum(Accumulator, Component, Accumulator, Residual);
            if (Residual != 0.0) Result.push_back(Residual);
        }
        if (Accumulator != 0.0 || Result.empty()) Result.push_back(Accumulator);
        return Result;
    }

    // Shewchuk EXPANSION-SUM by repeated growth (exact; adequate for the rare fallback branch).
    [[nodiscard]] Expansion SumExpansions(const Expansion& First, const Expansion& Second)
    {
        Expansion Result = First;
        for (double Component : Second) Result = GrowExpansion(Result, Component);
        return Result;
    }

    // Shewchuk SCALE-EXPANSION: multiply an expansion by one double.
    [[nodiscard]] Expansion ScaleExpansion(const Expansion& Source, double Scale)
    {
        Expansion Result;
        if (Source.empty() || Scale == 0.0) return Result;
        double Accumulator = 0.0;                                                            // [-] - Running high part
        double Residual    = 0.0;                                                            // [-] - Exact low part
        TwoProduct(Source[0], Scale, Accumulator, Residual);
        if (Residual != 0.0) Result.push_back(Residual);
        for (size_t ComponentIndex = 1; ComponentIndex < Source.size(); ++ComponentIndex)
        {
            double ProductHigh = 0.0;                                                        // [-] - Rounded component product
            double ProductLow  = 0.0;                                                        // [-] - Its exact error
            double PartialSum  = 0.0;                                                        // [-] - Accumulator + ProductLow
            TwoProduct(Source[ComponentIndex], Scale, ProductHigh, ProductLow);
            TwoSum(Accumulator, ProductLow, PartialSum, Residual);
            if (Residual != 0.0) Result.push_back(Residual);
            FastTwoSum(ProductHigh, PartialSum, Accumulator, Residual);
            if (Residual != 0.0) Result.push_back(Residual);
        }
        if (Accumulator != 0.0) Result.push_back(Accumulator);
        return Result;
    }

    [[nodiscard]] Expansion MultiplyExpansions(const Expansion& First, const Expansion& Second)
    {
        Expansion Result;
        for (double Component : Second) Result = SumExpansions(Result, ScaleExpansion(First, Component));
        return Result;
    }

    [[nodiscard]] Expansion NegateExpansion(Expansion Source)
    {
        for (double& Component : Source) Component = -Component;
        return Source;
    }

    [[nodiscard]] int ExpansionSign(const Expansion& Source) noexcept
    {
        for (auto Component = Source.rbegin(); Component != Source.rend(); ++Component)
        {
            if (*Component > 0.0) return 1;
            if (*Component < 0.0) return -1;
        }
        return 0;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

double NaiveOrient2D(Vector2 A, Vector2 B, Vector2 C) noexcept
{
    return (A.X - C.X) * (B.Y - C.Y) - (A.Y - C.Y) * (B.X - C.X);
}

double NaiveInCircle(Vector2 A, Vector2 B, Vector2 C, Vector2 D) noexcept
{
    const Vector2 AD = A - D, BD = B - D, CD = C - D;                                        // [-] - Rounded differences
    const double  AL = AD.X * AD.X + AD.Y * AD.Y, BL = BD.X * BD.X + BD.Y * BD.Y, CL = CD.X * CD.X + CD.Y * CD.Y;
    return AL * (BD.X * CD.Y - CD.X * BD.Y) + BL * (CD.X * AD.Y - AD.X * CD.Y) + CL * (AD.X * BD.Y - BD.X * AD.Y);
}

double NaiveOrient3D(Vector3 A, Vector3 B, Vector3 C, Vector3 D) noexcept
{
    const Vector3 AD = A - D;                                                                // [len] - Rounded difference
    const Vector3 BD = B - D;                                                                // [len] - Rounded difference
    const Vector3 CD = C - D;                                                                // [len] - Rounded difference
    return AD.X * (BD.Y * CD.Z - BD.Z * CD.Y) + BD.X * (CD.Y * AD.Z - CD.Z * AD.Y) + CD.X * (AD.Y * BD.Z - AD.Z * BD.Y);
}

SignOutcome Orient2D(Vector2 A, Vector2 B, Vector2 C) noexcept
{
    // ① Filter: the rounded determinant is trusted when it exceeds Shewchuk's forward error bound.
    const double LeftProduct  = (A.X - C.X) * (B.Y - C.Y);                                   // [-] - Rounded first term
    const double RightProduct = (A.Y - C.Y) * (B.X - C.X);                                   // [-] - Rounded second term
    const double Determinant  = LeftProduct - RightProduct;                                  // [-] - Rounded determinant
    const double Magnitude    = std::fabs(LeftProduct) + std::fabs(RightProduct);            // [-] - Error-bound scale
    if (std::fabs(Determinant) > Orient2DBound * Magnitude)
    {
        return { Determinant > 0.0 ? 1 : -1, SignResolution::Filtered };
    }

    // ② Exact: every difference and product is carried as an expansion, so the sign is exact.
    const Expansion First  = MultiplyExpansions(ExactDifference(A.X, C.X), ExactDifference(B.Y, C.Y));
    const Expansion Second = MultiplyExpansions(ExactDifference(A.Y, C.Y), ExactDifference(B.X, C.X));
    return { ExpansionSign(SumExpansions(First, NegateExpansion(Second))), SignResolution::Expansion };
}

SignOutcome Orient3D(Vector3 A, Vector3 B, Vector3 C, Vector3 D) noexcept
{
    // ① Filter with Shewchuk's permanent-based bound.
    const Vector3 AD = A - D;                                                                // [len] - Rounded difference
    const Vector3 BD = B - D;                                                                // [len] - Rounded difference
    const Vector3 CD = C - D;                                                                // [len] - Rounded difference
    const double  BCMinor = BD.X * CD.Y - CD.X * BD.Y;                                       // [-] - Rounded 2×2 minor
    const double  CAMinor = CD.X * AD.Y - AD.X * CD.Y;                                       // [-] - Rounded 2×2 minor
    const double  ABMinor = AD.X * BD.Y - BD.X * AD.Y;                                       // [-] - Rounded 2×2 minor
    const double  Determinant = AD.Z * BCMinor + BD.Z * CAMinor + CD.Z * ABMinor;            // [-] - Rounded determinant
    const double  Permanent =
        (std::fabs(BD.X * CD.Y) + std::fabs(CD.X * BD.Y)) * std::fabs(AD.Z) +
        (std::fabs(CD.X * AD.Y) + std::fabs(AD.X * CD.Y)) * std::fabs(BD.Z) +
        (std::fabs(AD.X * BD.Y) + std::fabs(BD.X * AD.Y)) * std::fabs(CD.Z);                // [-] - Error-bound scale
    if (std::fabs(Determinant) > Orient3DBound * Permanent)
    {
        return { Determinant > 0.0 ? 1 : -1, SignResolution::Filtered };
    }

    // ② Exact cofactor expansion on exact differences.
    const Expansion ADx = ExactDifference(A.X, D.X), ADy = ExactDifference(A.Y, D.Y), ADz = ExactDifference(A.Z, D.Z);
    const Expansion BDx = ExactDifference(B.X, D.X), BDy = ExactDifference(B.Y, D.Y), BDz = ExactDifference(B.Z, D.Z);
    const Expansion CDx = ExactDifference(C.X, D.X), CDy = ExactDifference(C.Y, D.Y), CDz = ExactDifference(C.Z, D.Z);
    const auto ExactMinor = [](const Expansion& P, const Expansion& Q, const Expansion& R, const Expansion& S)
    {
        return SumExpansions(MultiplyExpansions(P, Q), NegateExpansion(MultiplyExpansions(R, S)));
    };
    const Expansion BC = ExactMinor(BDx, CDy, CDx, BDy);
    const Expansion CA = ExactMinor(CDx, ADy, ADx, CDy);
    const Expansion AB = ExactMinor(ADx, BDy, BDx, ADy);
    const Expansion Total = SumExpansions(SumExpansions(MultiplyExpansions(ADz, BC), MultiplyExpansions(BDz, CA)), MultiplyExpansions(CDz, AB));
    return { ExpansionSign(Total), SignResolution::Expansion };
}

SignOutcome InCircle(Vector2 A, Vector2 B, Vector2 C, Vector2 D) noexcept
{
    // ① Filter with Shewchuk's permanent-based bound.
    const Vector2 AD = A - D;                                                                // [-] - Rounded difference
    const Vector2 BD = B - D;                                                                // [-] - Rounded difference
    const Vector2 CD = C - D;                                                                // [-] - Rounded difference
    const double  ALift = AD.X * AD.X + AD.Y * AD.Y;                                         // [-] - |A − D|²
    const double  BLift = BD.X * BD.X + BD.Y * BD.Y;                                         // [-] - |B − D|²
    const double  CLift = CD.X * CD.X + CD.Y * CD.Y;                                         // [-] - |C − D|²
    const double  Determinant = ALift * (BD.X * CD.Y - CD.X * BD.Y) + BLift * (CD.X * AD.Y - AD.X * CD.Y) + CLift * (AD.X * BD.Y - BD.X * AD.Y);
    const double  Permanent =
        (std::fabs(BD.X * CD.Y) + std::fabs(CD.X * BD.Y)) * ALift +
        (std::fabs(CD.X * AD.Y) + std::fabs(AD.X * CD.Y)) * BLift +
        (std::fabs(AD.X * BD.Y) + std::fabs(BD.X * AD.Y)) * CLift;                          // [-] - Error-bound scale
    if (std::fabs(Determinant) > InCircleBound * Permanent)
    {
        return { Determinant > 0.0 ? 1 : -1, SignResolution::Filtered };
    }

    // ② Exact lifted determinant on exact differences.
    const Expansion ADx = ExactDifference(A.X, D.X), ADy = ExactDifference(A.Y, D.Y);
    const Expansion BDx = ExactDifference(B.X, D.X), BDy = ExactDifference(B.Y, D.Y);
    const Expansion CDx = ExactDifference(C.X, D.X), CDy = ExactDifference(C.Y, D.Y);
    const auto Lift  = [](const Expansion& X, const Expansion& Y) { return SumExpansions(MultiplyExpansions(X, X), MultiplyExpansions(Y, Y)); };
    const auto Minor = [](const Expansion& P, const Expansion& Q, const Expansion& R, const Expansion& S)
    {
        return SumExpansions(MultiplyExpansions(P, Q), NegateExpansion(MultiplyExpansions(R, S)));
    };
    const Expansion Total = SumExpansions(SumExpansions(MultiplyExpansions(Lift(ADx, ADy), Minor(BDx, CDy, CDx, BDy)),
                                                        MultiplyExpansions(Lift(BDx, BDy), Minor(CDx, ADy, ADx, CDy))),
                                          MultiplyExpansions(Lift(CDx, CDy), Minor(ADx, BDy, BDx, ADy)));
    return { ExpansionSign(Total), SignResolution::Expansion };
}

} // namespace Frontier::GeometrySpace::Parametric
