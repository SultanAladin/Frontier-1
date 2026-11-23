/*============================================================================================================================================
                                                         VECTORAUTHORINGARRAYPLACEMENT.CPP
============================================================================================================================================*/
// 🧩 Deterministic array placement and scaling without vector, texture, GPU, document or UI dependencies.

#include "VectorAuthoringArrayPlacement.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace Frontier
{
namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     BOUNDED ARRAY EVALUATION
//------------------------------------------------------------------------------------------------------------------------

void ValidateQuantity(float Quantity, float Minimum, float Maximum)
{
    if (!std::isfinite(Quantity) || Quantity < Minimum || Quantity > Maximum)
        throw std::runtime_error("VectorAuthoring array quantity exceeds its finite authoring interval.");
}

std::vector<VectorAuthoringArrayPlacement> ConstructLocations(const VectorAuthoringArraySpecification &Specification, float OriginX, float OriginY)
{
    if (uint32_t(Specification.Category) > 3 || uint32_t(Specification.ScaleProfile) > 4)
        throw std::runtime_error("Unsupported vector array classification.");
    ValidateQuantity(Specification.StartScale, 1, 400);
    if (Specification.ScaleProfile != VectorAuthoringScaleProfile::Uniform) ValidateQuantity(Specification.EndScale, 1, 400);
    if (Specification.ScaleProfile == VectorAuthoringScaleProfile::Noise && Specification.Seed > 999999)
        throw std::runtime_error("VectorAuthoring noise seed exceeds 999999.");
    const bool Grid     = Specification.Category == VectorAuthoringArrayCategory::Grid;
    const bool Circular = Specification.Category == VectorAuthoringArrayCategory::Circular || Specification.Category == VectorAuthoringArrayCategory::Radial;
    const uint32_t Columns = Grid ? Specification.Columns : Specification.Count;
    const uint32_t Rows    = Grid ? Specification.Rows : Specification.Category == VectorAuthoringArrayCategory::Radial ? Specification.Rings : 1;
    if (!Columns || Columns > (Grid ? 8u : 64u) || !Rows || Rows > 8 || Columns * Rows < 2 || Columns * Rows > 64 || (!Grid && Columns < 2))
        throw std::runtime_error("VectorAuthoring array population exceeds supported bounds.");
    if (Circular)
    {
        ValidateQuantity(Specification.CentreX, -512, 1024);
        ValidateQuantity(Specification.CentreY, -512, 1024);
        ValidateQuantity(Specification.Radius, 0, 512);
        ValidateQuantity(Specification.StartAngle, -360, 360);
        ValidateQuantity(Specification.Sweep, -360, 360);
        if (Specification.Sweep == 0) throw std::runtime_error("VectorAuthoring arc sweep cannot be zero.");
        if (Specification.Category == VectorAuthoringArrayCategory::Radial) ValidateQuantity(Specification.RadiusStep, 1, 512);
    }
    else
    {
        ValidateQuantity(Specification.StepX, -512, 512);
        ValidateQuantity(Specification.StepY, -512, 512);
    }
    std::vector<VectorAuthoringArrayPlacement> Locations;
    for (uint32_t Row = 0; Row < Rows; ++Row)
    {
        for (uint32_t Column = 0; Column < Columns; ++Column)
        {
            if (Circular)
            {
                const float Phase = float(Column) / float(std::abs(Specification.Sweep) == 360 ? Columns : Columns - 1);
                const float Angle = Specification.StartAngle + Specification.Sweep * Phase;
                const float θ     = Angle * std::numbers::pi_v<float> / 180;
                const float ρ     = Specification.Radius + float(Row) * (Rows > 1 ? Specification.RadiusStep : 0);
                Locations.push_back({Specification.CentreX + std::cos(θ) * ρ, Specification.CentreY + std::sin(θ) * ρ, Angle, Phase});
            }
            else
            {
                const float ColumnOffset = float(Column) - float(Columns - 1) * .5f;
                const float RowOffset    = Grid ? float(Row) - float(Rows - 1) * .5f : ColumnOffset;
                Locations.push_back({OriginX + ColumnOffset * Specification.StepX, OriginY + RowOffset * Specification.StepY, 0, float(Locations.size()) / float(Columns * Rows)});
            }
        }
    }
    return Locations;
}

float EvaluateArrayScale(
    const VectorAuthoringArraySpecification &Specification,
    const VectorAuthoringArrayPlacement     &Location,
    size_t                           Index,
    size_t                           Population,
    float                            DistanceRatio)
{
    float Progress = 0;
    switch (Specification.ScaleProfile)
    {
    case VectorAuthoringScaleProfile::Uniform: break;
    case VectorAuthoringScaleProfile::Linear: Progress = float(Index) / float(Population - 1); break;
    case VectorAuthoringScaleProfile::Circular: Progress = .5f - .5f * std::cos(2 * std::numbers::pi_v<float> * Location.Phase); break;
    case VectorAuthoringScaleProfile::Radial: Progress = DistanceRatio; break;
    case VectorAuthoringScaleProfile::Noise:
    {
        uint32_t Hash = (Specification.Seed ^ uint32_t(Index + 1)) * 0x45d9f3bu;
        Hash = (Hash ^ (Hash >> 16)) * 0x45d9f3bu;
        Progress = float(double(Hash ^ (Hash >> 16)) / 4294967295.0);
        break;
    }
    }
    const float EndScale = Specification.ScaleProfile == VectorAuthoringScaleProfile::Uniform ? Specification.StartScale : Specification.EndScale;
    return (Specification.StartScale + (EndScale - Specification.StartScale) * Progress) / 100;
}

}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WORKSPACE-INDEPENDENT PLACEMENTS
//------------------------------------------------------------------------------------------------------------------------

std::vector<VectorAuthoringArrayPlacement> ConstructVectorAuthoringArrayPlacements(const VectorAuthoringArraySpecification &Specification, float OriginX, float OriginY)
{
    if (!std::isfinite(OriginX) || !std::isfinite(OriginY)) throw std::runtime_error("Array origin must be finite.");
    auto Locations = ConstructLocations(Specification, OriginX, OriginY);
    const bool Circular = Specification.Category == VectorAuthoringArrayCategory::Circular || Specification.Category == VectorAuthoringArrayCategory::Radial;
    const float CentreX = Circular ? Specification.CentreX : OriginX;
    const float CentreY = Circular ? Specification.CentreY : OriginY;
    float MaximumDistance = 1;
    for (const auto &Location : Locations) MaximumDistance = std::max(MaximumDistance, std::hypot(Location.X - CentreX, Location.Y - CentreY));
    for (size_t Index = 0; Index < Locations.size(); ++Index)
    {
        auto &Location = Locations[Index];
        Location.Scale = EvaluateArrayScale(Specification, Location, Index, Locations.size(), std::hypot(Location.X - CentreX, Location.Y - CentreY) / MaximumDistance);
    }
    return Locations;
}
}
