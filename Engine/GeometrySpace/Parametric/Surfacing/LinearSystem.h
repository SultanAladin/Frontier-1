/*====================================================================================================================================
                                                            LINEARSYSTEM.H
====================================================================================================================================*/
// 🧩 Dense square solves for fitting and interpolation: LU with partial pivoting, several right-hand sides at once

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <optional>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Row-major dense matrix: entry (Row, Column) at Values[Row · Columns + Column].
struct DenseLinearArray
{
    int                 Rows    = 0;                                                         // [-] - Row count
    int                 Columns = 0;                                                         // [-] - Column count
    std::vector<double> Values  = {};                                                        // [-] - Entries

    DenseLinearArray() = default;
    DenseLinearArray(int RowCount, int ColumnCount) : Rows(RowCount), Columns(ColumnCount), Values(static_cast<size_t>(RowCount) * ColumnCount, 0.0) {}

    [[nodiscard]] double& At(int Row, int Column)       noexcept { return Values[static_cast<size_t>(Row) * Columns + Column]; }
    [[nodiscard]] double  At(int Row, int Column) const noexcept { return Values[static_cast<size_t>(Row) * Columns + Column]; }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 A·X = B for square A (n × n) and B (n × k). Refuses (nullopt) when a pivot falls below PivotFloor · max |Aᵢⱼ|, so
//    a singular or numerically rank-deficient system is never answered with garbage.
[[nodiscard]] std::optional<DenseLinearArray> SolveDenseSystem(DenseLinearArray Matrix, DenseLinearArray RightSides, double PivotFloor = 1.0e-13);

}
