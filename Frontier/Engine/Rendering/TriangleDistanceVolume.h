/*==========================================================================================================================================
                                                        TRIANGLEDISTANCEVOLUME.H
==========================================================================================================================================*/
// 🧩 Unsigned sampled triangle distance, Lipschitz-bounded strides and exact surface refinement for open geometry.

#pragma once
#include "TriangleVisibilitySpace.h"
#include <stdexcept>

//------------------------------------------------------------------------------------------------------------------------
//                                                    SAMPLED TRIANGLE DISTANCE
//------------------------------------------------------------------------------------------------------------------------

namespace Frontier
{
class TriangleDistanceVolume
{
    TriangleVisibilitySpace Geometry;
    std::vector<double> Samples;
    SpatialVector Minimum{}, Maximum{}, Step{};
    std::array<uint32_t, 3> Resolution{};
    static double Coordinate(SpatialVector P, uint32_t Axis)
    {
        return Axis == 0 ? P.X : Axis == 1 ? P.Y : P.Z;
    }
    static bool Finite(SpatialVector P)
    {
        return std::isfinite(P.X) && std::isfinite(P.Y) && std::isfinite(P.Z);
    }

  public:
    void Bake(std::vector<std::array<SpatialVector, 3>> Facets, std::array<uint32_t, 3> Extent)
    {
        size_t Count = 1;
        for (auto Axis : Extent)
        {
            if (Axis < 2 || Axis > 256 || Count > 2097152 / Axis)
                throw std::runtime_error("Triangle distance volume extent rejected");
            Count *= Axis;
        }
        if (Facets.empty())
            throw std::runtime_error("Cannot bake empty triangle distance volume");
        TriangleDistanceVolume Replacement;
        Replacement.Geometry.Build(Facets);
        std::array<double, 3> Low{}, High{};
        Low.fill(INFINITY);
        High.fill(-INFINITY);
        for (auto Facet : Facets)
            for (auto Corner : Facet)
                for (uint32_t Axis = 0; Axis < 3; ++Axis)
                {
                    Low[Axis] = std::min(Low[Axis], Coordinate(Corner, Axis));
                    High[Axis] = std::max(High[Axis], Coordinate(Corner, Axis));
                }
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
        {
            Low[Axis] -= .15;
            High[Axis] += .15;
        }
        Replacement.Minimum = {Low[0], Low[1], Low[2]};
        Replacement.Maximum = {High[0], High[1], High[2]};
        Replacement.Step = {(High[0] - Low[0]) / (Extent[0] - 1), (High[1] - Low[1]) / (Extent[1] - 1),
                            (High[2] - Low[2]) / (Extent[2] - 1)};
        if (!Finite(Replacement.Step) || Replacement.Step.X <= 0 || Replacement.Step.Y <= 0 ||
            Replacement.Step.Z <= 0)
            throw std::runtime_error("Triangle distance bounds rejected");
        Replacement.Resolution = Extent;
        Replacement.Samples.resize(Count);
        for (uint32_t Z = 0; Z < Extent[2]; ++Z)
            for (uint32_t Y = 0; Y < Extent[1]; ++Y)
                for (uint32_t X = 0; X < Extent[0]; ++X)
                {
                    const SpatialVector P{Low[0] + X * Replacement.Step.X, Low[1] + Y * Replacement.Step.Y,
                                          Low[2] + Z * Replacement.Step.Z};
                    Replacement.Samples[(size_t(Z) * Extent[1] + Y) * Extent[0] + X] =
                        Replacement.Geometry.Nearest(P).Distance;
                }
        *this = std::move(Replacement);
    }
    uint32_t FacetCount() const
    {
        return Geometry.FacetCount();
    }
    size_t SampleCount() const
    {
        return Samples.size();
    }
    const std::vector<double> &Distances() const
    {
        return Samples;
    }
    SpatialVector MinimumBound() const noexcept { return Minimum; }
    SpatialVector MaximumBound() const noexcept { return Maximum; }
    SpatialVector CellStep() const noexcept { return Step; }
    std::array<uint32_t, 3> GridResolution() const noexcept { return Resolution; }
    TriangleVisibilitySpace::TriangleProximity ExactDistance(SpatialVector P) const
    {
        return Geometry.Nearest(P);
    }

    // A weighted distance-to-corners correction bounds interpolation of any 1-Lipschitz unsigned distance.
    double LowerDistance(SpatialVector P) const noexcept
    {
        if (Samples.empty() || !Finite(P))
            return INFINITY;
        std::array<uint32_t, 3> Cell{};
        std::array<double, 3> Fraction{};
        double OutsideSquared = 0;
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
        {
            const double Low = Coordinate(Minimum, Axis), High = Coordinate(Maximum, Axis),
                         Q = Coordinate(P, Axis);
            const double Outside = std::max({Low - Q, 0.0, Q - High});
            OutsideSquared += Outside * Outside;
            const double T =
                std::clamp((Q - Low) / Coordinate(Step, Axis), 0.0, double(Resolution[Axis] - 1));
            Cell[Axis] = std::min(uint32_t(T), Resolution[Axis] - 2);
            Fraction[Axis] = T - Cell[Axis];
        }
        // All geometry lies inside this enclosure; distance to it is conservative outside the volume.
        if (OutsideSquared > 0)
            return std::sqrt(OutsideSquared);
        double Lower = 0;
        for (uint32_t Z = 0; Z < 2; ++Z)
            for (uint32_t Y = 0; Y < 2; ++Y)
                for (uint32_t X = 0; X < 2; ++X)
                {
                    const double Weight = (X ? Fraction[0] : 1 - Fraction[0]) *
                                          (Y ? Fraction[1] : 1 - Fraction[1]) *
                                          (Z ? Fraction[2] : 1 - Fraction[2]);
                    const double DX = (X - Fraction[0]) * Step.X, DY = (Y - Fraction[1]) * Step.Y,
                                 DZ = (Z - Fraction[2]) * Step.Z;
                    const auto Index =
                        (size_t(Cell[2] + Z) * Resolution[1] + Cell[1] + Y) * Resolution[0] + Cell[0] + X;
                    Lower += Weight * (Samples[Index] - std::sqrt(DX * DX + DY * DY + DZ * DZ));
                }
        return std::max(0.0, Lower - 1e-10);
    }
    TriangleVisibilitySpace::TriangleStrike Trace(SpatialVector Origin, SpatialVector Direction,
                                                  double MinimumTravel, double MaximumTravel) const noexcept
    {
        const double Speed = std::hypot(Direction.X, Direction.Y, Direction.Z);
        if (Samples.empty() || !Finite(Origin) || !Finite(Direction) || !std::isfinite(Speed) || Speed <= 0 ||
            !std::isfinite(MinimumTravel) || MinimumTravel < 0 || !(MaximumTravel > MinimumTravel))
            return {};
        double Near = MinimumTravel, Far = MaximumTravel;
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
        {
            const double O = Coordinate(Origin, Axis), D = Coordinate(Direction, Axis);
            if (D == 0)
            {
                if (O < Coordinate(Minimum, Axis) || O > Coordinate(Maximum, Axis))
                    return {};
            }
            else
            {
                double A = (Coordinate(Minimum, Axis) - O) / D, B = (Coordinate(Maximum, Axis) - O) / D;
                if (A > B)
                    std::swap(A, B);
                Near = std::max(Near, A);
                Far = std::min(Far, B);
            }
        }
        if (Near > Far)
            return {};
        double Travel = Near;
        for (uint32_t Stride = 0; Stride < 64; ++Stride)
        {
            const double Lower =
                LowerDistance({Origin.X + Travel * Direction.X, Origin.Y + Travel * Direction.Y,
                               Origin.Z + Travel * Direction.Z});
            if (Lower < 1e-5)
                break;
            Travel += Lower / Speed;
            if (Travel > Far + 1e-9)
                return {};
        }
        // Refine the original interval: neither a coarse-grid hit nor stride-budget exhaustion is a surface hit.
        return Geometry.Trace(Origin, Direction, MinimumTravel, MaximumTravel);
    }
};
} // namespace Frontier
