//============================================================================================================================================
//                                                            LINEARSOLVER.CPP
//============================================================================================================================================
// 🧩 Partial-pivoted elimination, the refusal at the pivot floor, and the residual taken against the supply.

#include "SlateMath/Numeric/LinearSolver/Api/LinearSolver.h"

#include "Foundation/NumericTolerance.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE ELIMINATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 The whole factorisation, written once and reached by both declared forms. The sparse form fills a square of
//    its declared order and arrives here, so there is one elimination in the unit rather than two that drift.
// 🔴 Both runs are worked in place. The caller's supply is never touched — each form copies before it reaches
//    here — because a solve that consumed the system it was handed could not be followed by a residual measured
//    against that system, and the residual is the only check the caller has.
Deliver<SolvedSystem> Eliminate(std::vector<double>&  Working,
                                std::vector<double>&  Current,
                                std::uint32_t         Order,
                                std::uint32_t         CoordinateRuns)
{
    // 📐 The pivot floor is relative to the greatest magnitude the system supplied, so the same system scaled by
    //    any constant is rejected or accepted identically. An absolute floor would call a system in millimetres
    //    solvable and the same system in metres singular.
    double MaximumSupplied = 0.0;

    for (const double Supplied : Working)
    {
        const double Magnitude = std::fabs(Supplied);

        if (Magnitude > MaximumSupplied)
            MaximumSupplied = Magnitude;
    }

    if (MaximumSupplied <= 0.0)
    {
        return Deliver<SolvedSystem>::Refuse(
            { RefusalReason::ExtentExhausted, "every coefficient of the system is zero" });
    }

    const double PivotFloor = FactorisationPivotFloor * MaximumSupplied;

    SolvedSystem Produced;

    double MinimumPivot    = 0.0;
    double MaximumPivot = 0.0;

    for (std::uint32_t Diagonal = 0u; Diagonal < Order; ++Diagonal)
    {
        // 📐 Partial pivoting: the row carrying the greatest magnitude in this column is exchanged into the
        //    diagonal before the column is eliminated. Without it the growth in the eliminated coefficients is
        //    unbounded, and the Tier B claim above the declaration would be a claim about nothing.
        std::uint32_t Chosen       = Diagonal;
        double        ChosenExtent = std::fabs(Working[Diagonal * Order + Diagonal]);

        for (std::uint32_t Row = Diagonal + 1u; Row < Order; ++Row)
        {
            const double Candidate = std::fabs(Working[Row * Order + Diagonal]);

            if (Candidate > ChosenExtent)
            {
                Chosen       = Row;
                ChosenExtent = Candidate;
            }
        }

        if (ChosenExtent < PivotFloor)
        {
            return Deliver<SolvedSystem>::Refuse(
                { RefusalReason::ExtentExhausted, "a pivot fell below the declared floor; the system is singular" });
        }

        if (Chosen != Diagonal)
        {
            for (std::uint32_t Column = 0u; Column < Order; ++Column)
            {
                const double Held                  = Working[Diagonal * Order + Column];
                Working[Diagonal * Order + Column] = Working[Chosen * Order + Column];
                Working[Chosen * Order + Column]   = Held;
            }

            for (std::uint32_t Run = 0u; Run < CoordinateRuns; ++Run)
            {
                const double Held                       = Current[Diagonal * CoordinateRuns + Run];
                Current[Diagonal * CoordinateRuns + Run] = Current[Chosen * CoordinateRuns + Run];
                Current[Chosen * CoordinateRuns + Run]   = Held;
            }

            ++Produced.ExchangedRows;
        }

        if (Diagonal == 0u || ChosenExtent < MinimumPivot)
            MinimumPivot = ChosenExtent;

        if (ChosenExtent > MaximumPivot)
            MaximumPivot = ChosenExtent;

        const double Pivot = Working[Diagonal * Order + Diagonal];

        for (std::uint32_t Row = Diagonal + 1u; Row < Order; ++Row)
        {
            const double Multiplier = Working[Row * Order + Diagonal] / Pivot;

            if (Multiplier == 0.0)
                continue;

            // 📝 The eliminated position is assigned zero rather than subtracted to zero. Subtraction leaves a
            //    rounding residue there, and the back substitution below reads only the upper triangle — so the
            //    residue would be invisible while still reporting as a coefficient to anyone who read the run.
            Working[Row * Order + Diagonal] = 0.0;

            for (std::uint32_t Column = Diagonal + 1u; Column < Order; ++Column)
                Working[Row * Order + Column] -= Multiplier * Working[Diagonal * Order + Column];

            for (std::uint32_t Run = 0u; Run < CoordinateRuns; ++Run)
                Current[Row * CoordinateRuns + Run] -= Multiplier * Current[Diagonal * CoordinateRuns + Run];
        }
    }

    Produced.Solution.assign(static_cast<std::size_t>(Order) * CoordinateRuns, 0.0);

    for (std::uint32_t Run = 0u; Run < CoordinateRuns; ++Run)
    {
        // 📝 Walked upward from the last row, which is the only order the upper triangle can be read in — every
        //    row reads the unknowns the rows below it already resolved.
        for (std::uint32_t Stepped = Order; Stepped > 0u; --Stepped)
        {
            const std::uint32_t Row = Stepped - 1u;

            double Accumulated = Current[Row * CoordinateRuns + Run];

            for (std::uint32_t Column = Row + 1u; Column < Order; ++Column)
                Accumulated -= Working[Row * Order + Column] * Produced.Solution[Column * CoordinateRuns + Run];

            Produced.Solution[Row * CoordinateRuns + Run] = Accumulated / Working[Row * Order + Row];
        }
    }

    Produced.PivotRatio = MaximumPivot > 0.0 ? MinimumPivot / MaximumPivot : 0.0;

    return Deliver<SolvedSystem>::Result(Produced);
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE DENSE FORM
//------------------------------------------------------------------------------------------------------------------------

Deliver<SolvedSystem> Solve(const DenseSystem& Declaring)
{
    if (Declaring.Order == 0u)
        return Deliver<SolvedSystem>::Refuse({ RefusalReason::ContentUnsupported, "a system of no order" });

    if (Declaring.CoordinateRuns == 0u)
        return Deliver<SolvedSystem>::Refuse({ RefusalReason::ContentUnsupported, "a system solved against nothing" });

    const std::size_t SquaredExtent  = static_cast<std::size_t>(Declaring.Order) * Declaring.Order;
    const std::size_t CoordinateExtent = static_cast<std::size_t>(Declaring.Order) * Declaring.CoordinateRuns;

    if (Declaring.Coefficients.size() != SquaredExtent)
    {
        return Deliver<SolvedSystem>::Refuse(
            { RefusalReason::ContentUnsupported, "the coefficient extent is not the order squared" });
    }

    if (Declaring.Configuration.size() != CoordinateExtent)
    {
        return Deliver<SolvedSystem>::Refuse(
            { RefusalReason::ContentUnsupported, "the coordinate extent is not the order by the run count" });
    }

    std::vector<double> Working  = Declaring.Coefficients;
    std::vector<double> Current = Declaring.Configuration;

    return Eliminate(Working, Current, Declaring.Order, Declaring.CoordinateRuns);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE SPARSE FORM
//------------------------------------------------------------------------------------------------------------------------

Deliver<SolvedSystem> Solve(const SparseSystem& Declaring)
{
    if (Declaring.Order == 0u)
        return Deliver<SolvedSystem>::Refuse({ RefusalReason::ContentUnsupported, "a system of no order" });

    if (Declaring.Configuration.size() != static_cast<std::size_t>(Declaring.Order))
    {
        return Deliver<SolvedSystem>::Refuse(
            { RefusalReason::ContentUnsupported, "the coordinate extent is not the declared order" });
    }

    std::vector<double> Working(static_cast<std::size_t>(Declaring.Order) * Declaring.Order, 0.0);

    for (const SparseCoefficient& Supplied : Declaring.Coefficients)
    {
        if (Supplied.Row >= Declaring.Order || Supplied.Column >= Declaring.Order)
        {
            return Deliver<SolvedSystem>::Refuse(
                { RefusalReason::ContentUnsupported, "a coefficient addresses no row or no column" });
        }

        // 📝 Accumulated rather than assigned, which is what the declaration above promises a repeated position
        //    does. A normal system is assembled one observation at a time and several land on one position.
        Working[static_cast<std::size_t>(Supplied.Row) * Declaring.Order + Supplied.Column] += Supplied.Supplied;
    }

    // 🔴 The declared symmetry is checked before the elimination relies on it. `68` §4's mean-value system is
    //    symmetric by construction, and a supply that says so and is not would otherwise factorise into the answer
    //    to a system nobody assembled — which reads downstream as a chart flattened to the wrong surface rather
    //    than as an assembly defect.
    if (Declaring.DefiniteSymmetry)
    {
        for (std::uint32_t Row = 0u; Row < Declaring.Order; ++Row)
        {
            for (std::uint32_t Column = Row + 1u; Column < Declaring.Order; ++Column)
            {
                const double Above = Working[static_cast<std::size_t>(Row) * Declaring.Order + Column];
                const double Below = Working[static_cast<std::size_t>(Column) * Declaring.Order + Row];

                // 📐 Compared relative to the pair's own magnitude rather than absolutely, because the two are
                //    accumulated in different orders and a strict equality would reject a system that is
                //    symmetric in exact arithmetic and merely rounded differently on each side.
                const double Extent = std::fabs(Above) > std::fabs(Below) ? std::fabs(Above) : std::fabs(Below);

                if (std::fabs(Above - Below) > FactorisationPivotFloor * Extent)
                {
                    return Deliver<SolvedSystem>::Refuse(
                        { RefusalReason::ContentUnsupported, "the supply contradicts its declared symmetry" });
                }
            }
        }
    }

    std::vector<double> Current = Declaring.Configuration;

    return Eliminate(Working, Current, Declaring.Order, 1u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE RESIDUAL
//------------------------------------------------------------------------------------------------------------------------

double Measure(const DenseSystem& Declaring, const SolvedSystem& Solved)
{
    const std::size_t SquaredExtent  = static_cast<std::size_t>(Declaring.Order) * Declaring.Order;
    const std::size_t CoordinateExtent = static_cast<std::size_t>(Declaring.Order) * Declaring.CoordinateRuns;

    if (Declaring.Order == 0u || Declaring.CoordinateRuns == 0u)
        return 0.0;

    if (Declaring.Coefficients.size() != SquaredExtent || Declaring.Configuration.size() != CoordinateExtent)
        return 0.0;

    if (Solved.Solution.size() != CoordinateExtent)
        return 0.0;

    double Maximum = 0.0;

    for (std::uint32_t Row = 0u; Row < Declaring.Order; ++Row)
    {
        for (std::uint32_t Run = 0u; Run < Declaring.CoordinateRuns; ++Run)
        {
            double Accumulated = 0.0;

            for (std::uint32_t Column = 0u; Column < Declaring.Order; ++Column)
            {
                Accumulated += Declaring.Coefficients[static_cast<std::size_t>(Row) * Declaring.Order + Column]
                             * Solved.Solution[static_cast<std::size_t>(Column) * Declaring.CoordinateRuns + Run];
            }

            const std::size_t Supplied = static_cast<std::size_t>(Row) * Declaring.CoordinateRuns + Run;
            const double      Previous = std::fabs(Accumulated - Declaring.Configuration[Supplied]);

            if (Previous > Maximum)
                Maximum = Previous;
        }
    }

    return Maximum;
}

}   // namespace Slate
