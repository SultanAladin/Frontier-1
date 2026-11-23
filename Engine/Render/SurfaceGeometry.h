/*==========================================================================================================================================
                                                            SURFACEGEOMETRY.H
==========================================================================================================================================*/
// 🧩 Persistent surface corners and explicitly authored topology edges, independent of any viewport or file codec.

#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     GEOMETRY CONTRACT
//------------------------------------------------------------------------------------------------------------------------

struct SurfaceCorner
{
    std::array<float, 3> Position{};
    std::array<float, 3> Normal{};
    std::array<float, 2> Coordinates{};
    std::array<float, 4> Tangent{1,0,0,1};
};

struct SurfaceEdge
{
    std::array<float, 3> Start{};
    std::array<float, 3> End{};
};

struct SurfaceGeometry
{
    std::vector<SurfaceCorner> Corners;
    std::vector<uint32_t> TriangleCorners;
    std::vector<SurfaceEdge> Edges;
    bool MaterialCoordinatesEnabled = false;
};

// 📝 Column-major host/shader ABI. The caller must rebase large coordinates before narrowing; this unit does not decode or rebase documents.
struct alignas(16) SurfaceProjection
{
    std::array<float, 16> ClipFromSurface{};
    std::array<float, 4> SurfaceColour{0.12f, 0.15f, 0.16f, 1.0f};
    std::array<float, 4> EdgeColour{0.78f, 0.88f, 0.86f, 1.0f};
    std::array<float, 4> DisplayStroke{640, 480, 1.25f, 0};
};

static_assert(sizeof(SurfaceCorner) == 48);
static_assert(offsetof(SurfaceCorner,Coordinates) == 24);
static_assert(offsetof(SurfaceCorner,Tangent) == 32);
static_assert(sizeof(SurfaceEdge) == 24);
static_assert(sizeof(SurfaceProjection) == 112);
static_assert(offsetof(SurfaceProjection, SurfaceColour) == 64);
static_assert(offsetof(SurfaceProjection, EdgeColour) == 80);
static_assert(offsetof(SurfaceProjection, DisplayStroke) == 96);

inline bool ValidateSurfaceGeometry(const SurfaceGeometry &Geometry, std::string &Diagnostic)
{
    if (Geometry.Corners.empty() || Geometry.TriangleCorners.empty() || Geometry.TriangleCorners.size() % 3 != 0 ||
        Geometry.Corners.size() > 1048576 || Geometry.TriangleCorners.size() > 6291456 || Geometry.Edges.size() > 2097152)
    {
        Diagnostic = "Surface geometry is empty, malformed or exceeds the bounded upload capacity.";
        return false;
    }
    for (const auto Index : Geometry.TriangleCorners)
        if (Index >= Geometry.Corners.size())
        {
            Diagnostic = "Surface triangle references a missing corner.";
            return false;
        }
    for (const auto &Corner : Geometry.Corners)
    {
        float NormalLength = 0;
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            if (!std::isfinite(Corner.Position[Axis]) || !std::isfinite(Corner.Normal[Axis]))
            {
                Diagnostic = "Surface coordinates and normals must be finite.";
                return false;
            }
            NormalLength += Corner.Normal[Axis] * Corner.Normal[Axis];
        }
        if (!std::isfinite(NormalLength) || NormalLength < 1.0e-12f)
        {
            Diagnostic = "Surface normal has no finite direction.";
            return false;
        }
    }
    if (Geometry.MaterialCoordinatesEnabled)
        for (const auto &Corner : Geometry.Corners)
        {
            for (const auto Channel : Corner.Coordinates)
                if (!std::isfinite(Channel) || std::abs(Channel) > 1024)
                {
                    Diagnostic = "Authored UV coordinates must be finite and bounded.";
                    return false;
                }
            double TangentLength = 0, NormalLength = 0, Correlation = 0;
            for (size_t Axis = 0; Axis < 3; ++Axis)
            {
                TangentLength += double(Corner.Tangent[Axis])*Corner.Tangent[Axis];
                NormalLength += double(Corner.Normal[Axis])*Corner.Normal[Axis];
                Correlation += double(Corner.Normal[Axis])*Corner.Tangent[Axis];
            }
            if (!std::isfinite(TangentLength) || TangentLength < 1e-12 ||
                std::abs(Correlation)/std::sqrt(TangentLength*NormalLength) > 1e-3 || std::abs(Corner.Tangent[3]) != 1)
            {
                Diagnostic = "Authored tangent must be finite, normal-orthogonal and carry handedness +/-1.";
                return false;
            }
        }
    for (const auto &Edge : Geometry.Edges)
    {
        double LengthSquared = 0;
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            if (!std::isfinite(Edge.Start[Axis]) || !std::isfinite(Edge.End[Axis]))
            {
                Diagnostic = "Topology edge coordinates must be finite.";
                return false;
            }
            const double Difference = double(Edge.End[Axis]) - Edge.Start[Axis];
            LengthSquared += Difference * Difference;
        }
        if (LengthSquared < 1.0e-20)
        {
            Diagnostic = "Zero-length topology edge refused.";
            return false;
        }
    }
    Diagnostic.clear();
    return true;
}
} // namespace Frontier
