/*==========================================================================================================================================
                                                  DISCREPANCY SEQUENCE DEFINITION
==========================================================================================================================================*/

#include "DiscrepancySequence.h"

#include <cmath>

namespace Frontier
{

double RadicalInverse(uint32_t Base, uint64_t Index) noexcept
{
    if (Base < 2)
    {
        return 0.0;
    }
    const double InverseBase = 1.0 / static_cast<double>(Base);
    double       Factor      = InverseBase;
    double       Result      = 0.0;
    while (Index > 0)
    {
        Result += Factor * static_cast<double>(Index % Base);
        Index /= Base;
        Factor *= InverseBase;
    }
    return Result;
}

SequencePoint HaltonPoint(uint64_t Index) noexcept
{
    return {RadicalInverse(2, Index), RadicalInverse(3, Index)};
}

SequencePoint HammersleyPoint(uint64_t Index, uint64_t Count) noexcept
{
    if (Count == 0)
    {
        return {};
    }
    return {static_cast<double>(Index) / static_cast<double>(Count), RadicalInverse(2, Index)};
}

SequencePoint CranleyPatterson(SequencePoint Point, double ShiftX, double ShiftY) noexcept
{
    double X = Point.X + ShiftX;
    double Y = Point.Y + ShiftY;
    X -= std::floor(X);
    Y -= std::floor(Y);
    return {X, Y};
}

} // namespace Frontier
