//============================================================================================================================================
//                                                           PRECISIONGUARANTEE.H
//============================================================================================================================================
// 🧩 The declared numerical guarantee every exported computation carries, and the transitivity assertion.

#pragma once

#include <cstdint>
#include <initializer_list>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                             THE FOUR DECLARED GUARANTEES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The guarantee a computation claims about its result. A greater ordinal is a weaker guarantee.
/// note  Spelled A, B, C and D in the specification; the ordinal is what the transitivity rule compares.
/// tag   guarantee, constexpr
enum class PrecisionGuarantee : std::uint32_t
{
    Exact      = 0u,   // [-] - A - bit-exact; host and device agree on every bit
    Bounded    = 1u,   // [-] - B - error bounded in units in the last place
    Convergent = 2u,   // [-] - C - bounded by a declared convergence criterion
    Perceptual = 3u    // [-] - D - perceptual; no numeric guarantee is claimed
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     TRANSITIVITY
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Returns the weaker of two guarantees, which is the one carrying the greater ordinal.
/// in    LeftGuarantee    [-]  one declared guarantee
/// in    RightGuarantee   [-]  the other declared guarantee
/// out   Weaker           [-]  the weaker of the two
/// cost  ✔️
/// tag   constexpr, nonallocating, nonthrowing
constexpr PrecisionGuarantee Weaken(PrecisionGuarantee LeftGuarantee, PrecisionGuarantee RightGuarantee)
{
    return static_cast<std::uint32_t>(LeftGuarantee) >= static_cast<std::uint32_t>(RightGuarantee)
        ? LeftGuarantee
        : RightGuarantee;
}

/// 🧩 Folds every consumed guarantee down to the weakest one present in the list.
/// in    ConsumedGuarantees [-]  every guarantee the computation reads
/// out   Weakest            [-]  Exact when the list is empty, which is the identity of the fold
/// cost  ✔️
/// tag   constexpr, nonallocating, nonthrowing
constexpr PrecisionGuarantee WeakestOf(std::initializer_list<PrecisionGuarantee> ConsumedGuarantees)
{
    PrecisionGuarantee Weakest = PrecisionGuarantee::Exact;

    for (const PrecisionGuarantee Consumed : ConsumedGuarantees)
    {
        Weakest = Weaken(Weakest, Consumed);
    }

    return Weakest;
}

}   // namespace Slate

//------------------------------------------------------------------------------------------------------------------------
//                                                THE DECLARATION MACRO
//------------------------------------------------------------------------------------------------------------------------

// 📝 Placed at namespace scope immediately below the declaration it describes. The first argument is the
//    guarantee claimed; every further argument is a guarantee consumed. At least one consumed guarantee is
//    required — a computation that reads nothing declares PrecisionGuarantee::Exact as its only consumption.
#define SLATE_DECLARES_PRECISION(DeclaredGuarantee, ...)                                                    \
    static_assert(static_cast<std::uint32_t>(DeclaredGuarantee) >=                                          \
                  static_cast<std::uint32_t>(::Slate::WeakestOf({ __VA_ARGS__ })),                         \
                  "A computation may not claim a guarantee stronger than the weakest guarantee it reads.")
