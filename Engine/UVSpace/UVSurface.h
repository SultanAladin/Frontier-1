/*============================================================================================================================================
                                                           UVSURFACE.H
============================================================================================================================================*/
// 🧩 Corner-indexed polygon surface with seam topology, UDIM coordinates and packed per-face material layers

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/SurfaceShading.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Frontier::UVSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SurfacePosition
{
    float X = 0.0f; // [m] - Geometric horizontal coordinate
    float Y = 0.0f; // [m] - Geometric vertical coordinate
    float Z = 0.0f; // [m] - Geometric depth coordinate
};

struct UVCoordinate
{
    float U = 0.0f; // [tile] - Horizontal unbounded texture coordinate
    float V = 0.0f; // [tile] - Vertical unbounded texture coordinate
};

struct SurfaceCorner
{
    uint32_t PositionIndex = 0u; // [idx] - Shared geometric position index
    uint32_t UVIndex       = 0u; // [idx] - Independent corner UV index
};

struct SurfaceFace
{
    std::string                 Identifier       {};    // [-] - Stable author-facing face identifier
    std::vector<SurfaceCorner>  Corners          {};    // [count] - Counter-clockwise polygon corners
    uint32_t                    MaterialFirst    = 0u;  // [idx] - First packed appearance layer
    uint32_t                    MaterialCount    = 0u;  // [count] - Consecutive appearance layers
};

struct SurfaceEdge
{
    uint32_t PositionA = 0u; // [idx] - Lower geometric endpoint index
    uint32_t PositionB = 0u; // [idx] - Higher geometric endpoint index

    [[nodiscard]] constexpr auto operator<=>(const SurfaceEdge&) const noexcept = default;
};

struct UVSelection
{
    std::vector<uint32_t> PositionIndices {}; // [idx] - Selected geometric positions
    std::vector<uint32_t> UVIndices       {}; // [idx] - Selected independent UV coordinates
    std::vector<uint32_t> FaceIndices     {}; // [idx] - Selected polygon faces
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr std::optional<uint32_t> ResolveUdimTile(UVCoordinate Coordinate) noexcept;
[[nodiscard]] constexpr UVCoordinate            ResolveUdimOrigin(uint32_t TileIdentifier) noexcept;
[[nodiscard]] constexpr SurfaceEdge             ConstructSurfaceEdge(uint32_t PositionA, uint32_t PositionB) noexcept;

class UVSurface
{
public:
    [[nodiscard]] bool Validate() const noexcept;
    [[nodiscard]] bool AssignFaceLayers(uint32_t FaceIndex, std::span<const GeometricRaster::MatcapLayer> Layers);
    [[nodiscard]] bool TransformUVSelection(
        std::span<const uint32_t> UVIndices,
        UVCoordinate              Pivot,
        UVCoordinate              Translation,
        UVCoordinate              Scale,
        float                     RotationRadians) noexcept;
    [[nodiscard]] bool ConfigureSeam(SurfaceEdge Edge, bool SeamEnabled);
    [[nodiscard]] bool QuerySeam(SurfaceEdge Edge) const noexcept;
    [[nodiscard]] std::vector<uint32_t> ExtractFaceChart(uint32_t SeedFaceIndex) const;

    std::string                                      Identifier     {}; // [-] - Surface document identifier
    std::vector<SurfacePosition>                     Positions      {}; // [count] - Shared geometric positions
    std::vector<UVCoordinate>                        Coordinates    {}; // [count] - Independent UV coordinates
    std::vector<SurfaceFace>                         Faces          {}; // [count] - Indexed polygon faces
    std::vector<GeometricRaster::MatcapLayer>        MaterialLayers {}; // [count] - Packed per-face appearance layers

private:
    std::vector<SurfaceEdge> Seams {}; // [count] - Sorted geometric edges splitting UV charts
};

} // namespace Frontier::UVSpace

//------------------------------------------------------------------------------------------------------------------------
//                                                 TEMPLATE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

namespace Frontier::UVSpace
{

constexpr std::optional<uint32_t> ResolveUdimTile(UVCoordinate Coordinate) noexcept
{
    constexpr float Epsilon = 1.0e-6f;
    const int32_t TileU = static_cast<int32_t>((Coordinate.U + Epsilon >= 0.0f)
            ? Coordinate.U + Epsilon
            : Coordinate.U + Epsilon - 1.0f);
    const int32_t TileV = static_cast<int32_t>((Coordinate.V + Epsilon >= 0.0f)
            ? Coordinate.V + Epsilon
            : Coordinate.V + Epsilon - 1.0f);
    if (TileU < 0 || TileU > 9 || TileV < 0 || TileV > 99)
    {
        return std::nullopt;
    }
    return static_cast<uint32_t>(1001 + TileU + TileV * 10);
}

constexpr UVCoordinate ResolveUdimOrigin(uint32_t TileIdentifier) noexcept
{
    const uint32_t Offset = TileIdentifier > 1001u ? TileIdentifier - 1001u : 0u;
    return UVCoordinate{static_cast<float>(Offset % 10u), static_cast<float>(Offset / 10u)};
}

constexpr SurfaceEdge ConstructSurfaceEdge(uint32_t PositionA, uint32_t PositionB) noexcept
{
    return PositionA < PositionB ? SurfaceEdge{PositionA, PositionB} : SurfaceEdge{PositionB, PositionA};
}

} // namespace Frontier::UVSpace
