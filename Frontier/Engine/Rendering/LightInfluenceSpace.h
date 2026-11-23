/*==========================================================================================================================================
                                                         LIGHTINFLUENCESPACE.H
==========================================================================================================================================*/
// 🧩 Conservative world-cell light lists with whole-list overflow and outside-grid fallback.

#pragma once
#include "ClusterNesting.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace Frontier
{
struct LightInfluence
{
    SpatialVector Position{};
    double        Radius = 0; // [m] - zero denotes unbounded support.
};
class LightInfluenceSpace
{
    SpatialVector                      Minimum{}, Maximum{};
    std::array<uint32_t, 3>            Resolution{};
    std::vector<std::vector<uint32_t>> Cells;
    std::vector<bool>                  Overflow;
    std::vector<uint32_t>              All;
    static double                      Coordinate(SpatialVector P, uint32_t Axis)
    {
        return Axis == 0 ? P.X : Axis == 1 ? P.Y : P.Z;
    }

  public:
    void Build(const std::vector<LightInfluence>& Lights, SpatialVector Low, SpatialVector High,
               std::array<uint32_t, 3> Extent, uint32_t Capacity)
    {
        size_t Count = 1;
        if (!Capacity || Capacity > 4096 || Lights.size() > 32768)
            throw std::runtime_error("Light-list capacity rejected");
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
        {
            if (!Extent[Axis] || Extent[Axis] > 256 || Count > 1048576 / Extent[Axis] ||
                !std::isfinite(Coordinate(Low, Axis)) || !std::isfinite(Coordinate(High, Axis)) ||
                !std::isfinite(Coordinate(High, Axis) - Coordinate(Low, Axis)) ||
                !(Coordinate(High, Axis) > Coordinate(Low, Axis)))
                throw std::runtime_error("Light grid bounds rejected");
            Count *= Extent[Axis];
        }
        if (Lights.size() && Count > 16777216 / Lights.size())
            throw std::runtime_error("Light-grid assignment budget exceeded");
        for (auto Light : Lights)
            if (!std::isfinite(Light.Position.X) || !std::isfinite(Light.Position.Y) ||
                !std::isfinite(Light.Position.Z) || !std::isfinite(Light.Radius) || Light.Radius < 0)
                throw std::runtime_error("Invalid light influence");
        LightInfluenceSpace Replacement;
        Replacement.Minimum    = Low;
        Replacement.Maximum    = High;
        Replacement.Resolution = Extent;
        Replacement.Cells.resize(Count);
        Replacement.Overflow.resize(Count);
        Replacement.All.resize(Lights.size());
        std::iota(Replacement.All.begin(), Replacement.All.end(), 0u);
        for (uint32_t Z = 0; Z < Extent[2]; ++Z)
            for (uint32_t Y = 0; Y < Extent[1]; ++Y)
                for (uint32_t X = 0; X < Extent[0]; ++X)
                {
                    const size_t            Cell = (size_t(Z) * Extent[1] + Y) * Extent[0] + X;
                    std::array<uint32_t, 3> Location{X, Y, Z};
                    for (uint32_t Index = 0; Index < Lights.size(); ++Index)
                    {
                        double Squared = 0;
                        for (uint32_t Axis = 0; Axis < 3; ++Axis)
                        {
                            const double Step =
                                (Coordinate(High, Axis) - Coordinate(Low, Axis)) / Extent[Axis];
                            const double A = Coordinate(Low, Axis) + Location[Axis] * Step, B = A + Step;
                            const double Delta = Coordinate(Lights[Index].Position, Axis) -
                                                 std::clamp(Coordinate(Lights[Index].Position, Axis), A, B);
                            Squared += Delta * Delta;
                        }
                        if (Lights[Index].Radius == 0 ||
                            Squared <=
                                Lights[Index].Radius * Lights[Index].Radius +
                                    64 * std::numeric_limits<double>::epsilon() *
                                        std::max({1.0, Squared, Lights[Index].Radius * Lights[Index].Radius}))
                        {
                            if (Replacement.Cells[Cell].size() < Capacity)
                                Replacement.Cells[Cell].push_back(Index);
                            else
                                Replacement.Overflow[Cell] = true;
                        }
                    }
                }
        *this = std::move(Replacement);
    }
    const std::vector<uint32_t>& Candidates(SpatialVector P, bool* Fallback = nullptr) const
    {
        if (Fallback)
            *Fallback = false;
        std::array<uint32_t, 3> Cell{};
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
        {
            double Q = Coordinate(P, Axis), Low = Coordinate(Minimum, Axis), High = Coordinate(Maximum, Axis);
            if (!std::isfinite(Q) || Q < Low || Q >= High || Cells.empty())
            {
                if (Fallback)
                    *Fallback = true;
                return All;
            }
            Cell[Axis] =
                std::min(uint32_t((Q - Low) / (High - Low) * Resolution[Axis]), Resolution[Axis] - 1);
        }
        const size_t Index = (size_t(Cell[2]) * Resolution[1] + Cell[1]) * Resolution[0] + Cell[0];
        if (Overflow[Index])
        {
            if (Fallback)
                *Fallback = true;
            return All;
        }
        return Cells[Index];
    }
};
} // namespace Frontier
