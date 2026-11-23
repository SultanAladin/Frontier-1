//============================================================================================================================================
//                                                             LINEARSOLVER.H
//============================================================================================================================================
// 🧩 Dense and sparse factorisation — Bounded, and held to refusing a singular system rather than dividing by it.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT IS SOLVED
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One dense system — a square run of coefficients in row order, and the ordinates it is solved against.
/// note  🔴 Row order, and stated rather than inferred. A factorisation reading a column-ordered supply as a
///        row-ordered one solves the transposed system, which is a system that exists and has an answer — so
///        nothing refuses, nothing diverges, and the defect surfaces as a transfer that lands mirrored.
/// note  📝 Several right-hand sides are carried at once because `24` §2 solves one correspondence per channel
///        against one geometry. The factorisation is the expensive half and is shared across every coordinate run
///        rather than repeated per channel.
/// tag   owning
struct DenseSystem
{
    std::vector<double>  Coefficients = {};   // [-] - Order × Order, row order
    std::vector<double>  Configuration    = {};   // [-] - Order × CoordinateRuns, row order
    std::uint32_t        Order        = 0u;   // [-] - rows, and equally columns
    std::uint32_t        CoordinateRuns = 1u;   // [-] - right-hand sides solved against one factorisation
};

/// 🧩 One coefficient at a stated row and column. Everything absent from the supply is zero.
/// note  📝 Duplicates at one position accumulate rather than replace. A least-squares normal system is assembled
///        by adding one contribution per observation, and an assembler that had to search for its own earlier
///        entry before adding to it would be quadratic in the observation count.
/// tag   nonallocating, nonthrowing
struct SparseCoefficient
{
    std::uint32_t  Row      = 0u;    // [-] - into the system's order
    std::uint32_t  Column   = 0u;    // [-] - into the system's order
    double         Supplied = 0.0;   // [-] - accumulated when the position repeats
};

/// 🧩 One sparse system — the coefficients that are not zero, and the ordinates it is solved against.
/// note  🔴 Symmetric and positive-definite is **declared**, not detected. `68` §4's mean-value system is both by
///        construction and factorises in half the arithmetic and without pivoting; a general system is not, and
///        taking a square root of a negative pivot is where an undeclared asymmetry would first be noticed.
/// note  ⚠️ The declaration is checked before it is relied on. A supply that declares symmetry and is not
///        symmetric is rejected with ContentUnsupported rather than factorised into an answer to a system nobody
///        assembled — which would present as a solve that converged to the wrong surface.
/// tag   owning
struct SparseSystem
{
    std::vector<SparseCoefficient>  Coefficients      = {};      // [-] - every position that is not zero
    std::vector<double>             Configuration         = {};      // [-] - Order entries, one right-hand side
    std::uint32_t                   Order             = 0u;      // [-] - rows, and equally columns
    bool                            DefiniteSymmetry  = false;   // [-] - symmetric and positive-definite
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT IS REPORTED
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The solution, and what the factorisation observed about the system on the way to it.
/// note  🔴 `PivotRatio` is the whole reason this is a structure and not a bare run of doubles. A system that
///        factorised without refusing may still have been within a decimal place of refusing, and a solution
///        carried out of a nearly singular system is a solution whose error bound is nothing like the Tier B one
///        the caller was promised. `86` reads it; `24` §4 reports it beside its miss count.
/// note  📐 The ratio is the least pivot magnitude over the greatest, both taken after elimination. One is a
///        perfectly conditioned system; the pivot floor is where it is rejected.
/// tag   owning
struct SolvedSystem
{
    std::vector<double>  Solution      = {};    // [-] - Order × CoordinateRuns, row order
    double               PivotRatio    = 0.0;   // [-] - least over greatest pivot magnitude, after elimination
    std::uint32_t        ExchangedRows = 0u;    // [-] - row exchanges the pivoting performed
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE DENSE FORM
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Factorises a dense square system with partial pivoting and solves every coordinate run against it.
/// in    Declaring  [-]  the coefficients in row order, the ordinates, and the order of both
/// out   Result    [-]  refuses with ContentUnsupported for an order of zero or a supply whose extent is not
///                       the order squared, and with ExtentExhausted when a pivot falls below the declared floor
/// note  🔴 Bounded, per `02` §5. The bound is the one partial pivoting carries — growth in the eliminated
///        coefficients is bounded by two to the order — and it is claimed only because every consumed operation
///        is itself Bounded. Nothing here claims Exact: elimination is a sequence of roundings.
/// note  🔴 A pivot below `FactorisationPivotFloor` is **rejected** and never divided by. `02` §5's Tier C
///        components report a termination cause because their last iterate is still an answer; this is Tier B and
///        has no last iterate — a solution produced by dividing through a numerically absent pivot is arbitrary,
///        and delivering it would hand `24` a correspondence assembled out of the rounding.
/// note  📐 Partial pivoting only. Full pivoting exchanges columns as well and buys a tighter bound at the cost of
///        permuting the unknowns, and every system reaching here is assembled from a geometric neighbourhood where
///        the coefficients are already of one magnitude. The pivot ratio reports when that assumption failed.
/// cost  🔴
/// tag   api, nonthrowing
Deliver<SolvedSystem> Solve(const DenseSystem& Declaring);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE SPARSE FORM
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Factorises a sparse square system and solves its single coordinate run against it.
/// in    Declaring  [-]  the coefficients that are not zero, the ordinates, and whether definite symmetry holds
/// out   Result    [-]  refuses with ContentUnsupported for an order of zero, a coefficient addressing no row or
///                       column, an coordinate run that is not the order, or a declared symmetry the supply
///                       contradicts; refuses with ExtentExhausted when a pivot falls below the declared floor
/// note  🔴 The factorisation is performed against a filled square of the declared order rather than against a
///        sparse structure. `68` §4's system is one unknown per interior chart position and the systems reaching
///        `24` are per-neighbourhood, both well inside where the filled form costs less than maintaining the
///        occupancy would. The surface is the sparse one so that the day a system arrives that is not, only what
///        is behind this declaration changes.
/// note  ⚠️ Fill-in is exactly why the surface must be the sparse one now rather than later. An elimination on a
///        sparse structure creates coefficients at positions the supply left absent, and a caller that assembled
///        against a dense surface would have to be rewritten to assemble against a sparse one — which is the
///        rewrite this declaration is placed here to avoid.
/// cost  🔴
/// tag   api, nonthrowing
Deliver<SolvedSystem> Solve(const SparseSystem& Declaring);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RESIDUAL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Measures ‖A·x − b‖∞ for a solution against the dense system it was solved from.
/// in    Declaring  [-]  the system as it was supplied
/// in    Solved     [-]  the solution the factorisation produced
/// out   Residual   [-]  the greatest absolute departure across every row and every coordinate run; zero for a
///                       solution whose extent does not match the system, which no caller can reach
/// note  🔴 Measured against the **supplied** coefficients rather than against the factorised ones. A residual
///        taken against the eliminated coefficients measures the back substitution alone and reports near zero
///        for a factorisation that lost the system entirely — it would confirm the arithmetic against itself.
/// note  📝 The infinity norm rather than the Euclidean one. The caller wants the worst row, because one badly
///        satisfied correspondence is a visible artefact at one position and a Euclidean norm averages it away
///        against every row that was satisfied.
/// cost  🚩
/// tag   api, nonallocating, nonthrowing
double Measure(const DenseSystem& Declaring, const SolvedSystem& Solved);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

}   // namespace Slate
