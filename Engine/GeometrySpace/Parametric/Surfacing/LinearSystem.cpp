/*====================================================================================================================================
                                                           LINEARSYSTEM.CPP
====================================================================================================================================*/
// 🧩 Dense square solves: Doolittle LU with partial pivoting and a relative pivot floor

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Surfacing/LinearSystem.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::optional<DenseMatrix> SolveDenseSystem(DenseMatrix Matrix, DenseMatrix RightSides, double PivotFloor)
{
    const int Size = Matrix.Rows, Width = RightSides.Columns;
    if (Matrix.Columns != Size || RightSides.Rows != Size) return std::nullopt;
    double Largest = 0.0;
    for (double Value : Matrix.Values) Largest = std::max(Largest, std::abs(Value));
    if (!(Largest > 0.0) || !std::isfinite(Largest)) return std::nullopt;
    for (int Column = 0; Column < Size; ++Column)
    {
        // ① Partial pivot: the largest magnitude at or below the diagonal.
        int Pivot = Column;
        for (int Row = Column + 1; Row < Size; ++Row) if (std::abs(Matrix.At(Row, Column)) > std::abs(Matrix.At(Pivot, Column))) Pivot = Row;
        if (std::abs(Matrix.At(Pivot, Column)) < PivotFloor * Largest) return std::nullopt;
        if (Pivot != Column)
        {
            for (int Index = 0; Index < Size; ++Index)  std::swap(Matrix.At(Pivot, Index), Matrix.At(Column, Index));
            for (int Index = 0; Index < Width; ++Index) std::swap(RightSides.At(Pivot, Index), RightSides.At(Column, Index));
        }
        // ② Eliminate below, carrying the right-hand sides.
        for (int Row = Column + 1; Row < Size; ++Row)
        {
            const double Factor = Matrix.At(Row, Column) / Matrix.At(Column, Column);
            if (Factor == 0.0) continue;
            for (int Index = Column; Index < Size; ++Index) Matrix.At(Row, Index) -= Factor * Matrix.At(Column, Index);
            for (int Index = 0; Index < Width; ++Index)     RightSides.At(Row, Index) -= Factor * RightSides.At(Column, Index);
        }
    }
    // ③ Back substitution.
    DenseMatrix Solution(Size, Width);
    for (int Row = Size - 1; Row >= 0; --Row)
    {
        for (int Index = 0; Index < Width; ++Index)
        {
            double Sum = RightSides.At(Row, Index);
            for (int Column = Row + 1; Column < Size; ++Column) Sum -= Matrix.At(Row, Column) * Solution.At(Column, Index);
            Solution.At(Row, Index) = Sum / Matrix.At(Row, Row);
        }
    }
    return Solution;
}

}
