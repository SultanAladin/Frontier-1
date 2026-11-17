/*====================================================================================================================================
                                                     CHARTTRIANGULATION.H
====================================================================================================================================*/
// 🧩 Constrained Delaunay triangulation of a face chart: Bowyer–Watson insertion, Sloan constraint recovery, parity interior

#pragma once

#include "Engine/GeometrySpace/Parametric/Geometry/VectorAlgebra.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Corners counter-clockwise. Side k lies opposite corner k, from Corners[(k + 1) % 3] to Corners[(k + 2) % 3];
//    Neighbours[k] shares side k (−1 on the hull of the enclosing triangle).
struct ChartTriangle
{
    std::array<int, 3>  Corners     = { -1, -1, -1 };                                        // [-] - Point indices
    std::array<int, 3>  Neighbours  = { -1, -1, -1 };                                        // [-] - Triangle across side k
    std::array<bool, 3> Constrained = { false, false, false };                               // [-] - Side k is a boundary segment
    bool                Alive       = true;                                                  // [-] - Not replaced by an insertion
    bool                Interior    = false;                                                 // [-] - Inside the face (after classification)
};

struct ChartTriangulationStatistics
{
    int64_t InCircleTests   = 0;                                                             // [-] - Exact in-circle evaluations
    int64_t LocateSteps     = 0;                                                             // [-] - Walk steps
    int     LocateFallbacks = 0;                                                             // [-] - Walks that fell back to a scan
    int     CavityRepairs   = 0;                                                             // [-] - Cavity triangles removed to stay star-shaped
    int     ConstraintFlips = 0;                                                             // [-] - Flips recovering boundary segments
    int     LegalizingFlips = 0;                                                             // [-] - Lawson flips after recovery
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRIANGULATION
//------------------------------------------------------------------------------------------------------------------------

// 💡 Points live in a (metric-scaled) chart. Every orientation and in-circle decision uses the exact predicates, so the
//    combinatorics stay valid on collinear chart rows and cocircular grids where rounded determinants flip sign.
class ChartTriangulation
{
public:
    // Encloses [Low, High] in a large triangle; its three points are indices 0, 1, 2.
    ChartTriangulation(Vector2 Low, Vector2 High);

    // Bowyer–Watson insertion whose cavity never crosses a constrained side. Returns the new point index, the index of
    // an existing point at the same position, or −1 when the point lies on a constrained side.
    int InsertPoint(Vector2 Position);

    // Cavity of a prospective point inside triangle Container (from LocateTriangle): triangles whose circumcircle
    // strictly contains it, connected to Container without crossing constrained sides. Empty when the point lies on a
    // constrained side.
    [[nodiscard]] std::vector<int> CollectCavity(Vector2 Position, int Container);

    // Inserts a point into a cavity from CollectCavity. Returns the new point index.
    int CommitCavity(Vector2 Position, const std::vector<int>& Cavity, int Container);

    // Recovers segment A–B as a triangulation side by flips (Sloan 1993) and marks it constrained; a point lying exactly
    // on the segment splits it. False when a constrained side crosses the segment.
    bool ConstrainSegment(int First, int Second);

    // Lawson flips until every unconstrained side is locally Delaunay.
    void Legalize();

    // Interior by crossing parity from the enclosing triangle: crossing a constrained side toggles inside / outside.
    void ClassifyInterior();

    // Triangle containing Position (walk from the most recent triangle, scan as fallback); −1 outside the hull.
    [[nodiscard]] int LocateTriangle(Vector2 Position);

    // Unconstrained interior sides whose opposite point lies strictly inside the circumcircle (should be 0).
    [[nodiscard]] int CountDelaunayViolations();

    [[nodiscard]] const std::vector<Vector2>&       Points() const noexcept { return PointList; }
    [[nodiscard]] const std::vector<ChartTriangle>& Triangles() const noexcept { return TriangleList; }
    [[nodiscard]] const std::vector<int>&           RecentTriangles() const noexcept { return Recent; }
    [[nodiscard]] const ChartTriangulationStatistics& Statistics() const noexcept { return Counters; }

private:
    [[nodiscard]] int  FindSide(int From, int To, int& Side);
    [[nodiscard]] bool InsideCircumcircle(int Triangle, Vector2 Position);
    void               FlipSide(int Triangle, int Side);
    void               LinkNeighbour(int Triangle, int Side, int Neighbour);
    [[nodiscard]] uint32_t NextStamp();

    std::vector<Vector2>         PointList;                                                  // [-] - Chart points
    std::vector<ChartTriangle>   TriangleList;                                               // [-] - All triangles (dead ones kept)
    std::vector<int>             PointTriangle;                                              // [-] - One live triangle per point
    std::vector<int>             Recent;                                                     // [-] - Triangles made by the last insertion
    std::vector<uint32_t>        Stamp;                                                      // [-] - Per-triangle visit stamps
    uint32_t                     StampValue   = 0;                                           // [-] - Current stamp
    int                          LastTriangle = 0;                                           // [-] - Walk start
    ChartTriangulationStatistics Counters;                                                   // [-] - Work counters
};

} // namespace Frontier::GeometrySpace::Parametric
