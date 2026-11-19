/*====================================================================================================================================
                                                      BODYTESSELLATION.H
====================================================================================================================================*/
// 🧩 Watertight body tessellation: one shared sampling per edge, constrained Delaunay refinement per face chart

#pragma once

#include "Engine/GeometrySpace/Parametric/Tessellation/ChartTriangulation.h"
#include "Engine/GeometrySpace/Parametric/Connectivity/ModelPartition.h"

#include <array>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct TessellationSettings
{
    double Sag             = 0.0;                                                            // [len] - Chordal deviation bound ε (0 → SagRatio × diagonal)
    double SagRatio        = 1.0e-3;                                                         // [-]   - ε relative to the body diagonal when Sag = 0
    double EdgeLengthRatio = 0.25;                                                           // [-]   - Longest unconstrained side relative to the diagonal
    double RadiusEdgeBound = 1.4142135623730951;                                             // [-]   - Circumradius / shortest side in the scaled chart
    int    TriangleLimit   = 4000000;                                                        // [-]   - Refusal threshold over the whole body
};

// 📝 One B-rep edge sampled once; both faces of the edge reuse these vertices, which makes the complex watertight.
struct PolygonNetEdge
{
    EdgeToken           Edge       = {};                                                     // [-] - Sampled edge
    std::vector<double> Parameters = {};                                                     // [-] - τ samples, IntervalLow → IntervalHigh
    std::vector<int>    Vertices   = {};                                                     // [-] - Complex vertex per sample
};

struct PolygonNetFace
{
    FaceToken Face               = {};                                                       // [-]  - Tessellated face
    int       FirstTriangle      = 0;                                                        // [-]  - Span start in Triangles
    int       TriangleCount      = 0;                                                        // [-]  - Span length
    int       BoundaryPoints     = 0;                                                        // [-]  - Chart points from loops
    int       Insertions         = 0;                                                        // [-]  - Refinement points
    int       CentroidFallbacks  = 0;                                                        // [-]  - Circumcentres replaced by centroids
    int       QualitySkips       = 0;                                                        // [-]  - Encroaching quality-only insertions declined
    int       SingularDropped    = 0;                                                        // [-]  - Triangles collapsed onto a pole / apex
    int       DelaunayViolations = 0;                                                        // [-]  - Residual before the final legalisation
    int       StarRepairs        = 0;                                                        // [-]  - Singular-vertex stars re-triangulated
    int       FoldFlips          = 0;                                                        // [-]  - Folded slivers repaired by 3D side flips
    int       ClaimedSplits      = 0;                                                        // [-]  - Interior sides split because another face already used them
    int       FoldedResidual     = 0;                                                        // [-]  - Emitted triangles failing the fold test
    double    ScaleU             = 1.0;                                                      // [len] - |∂P/∂𝑢| at the window centre
    double    ScaleV             = 1.0;                                                      // [len] - |∂P/∂𝑣| at the window centre
    double    Milliseconds       = 0.0;                                                      // [ms]  - Face time
};

// 💡 PolygonNetworkStructure = triangles over shared vertices. Positions[0 … TopologicalVertices) are the B-rep vertices.
struct PolygonNetworkStructure
{
    std::vector<Vector3>                Positions           = {};                            // [len] - Vertex positions
    std::vector<std::array<int, 3>>     Triangles           = {};                            // [-]   - Outward counter-clockwise corners
    std::vector<std::array<Vector2, 3>> TriangleCharts      = {};                            // [-]   - Chart (𝑢, 𝑣) per corner
    std::vector<int>                    TriangleFaces       = {};                            // [-]   - Index into Faces
    std::vector<PolygonNetFace>     Faces               = {};                            // [-]   - Per-face spans
    std::vector<PolygonNetEdge>     Edges               = {};                            // [-]   - Shared edge samplings
    int                                 TopologicalVertices = 0;                             // [-]   - B-rep vertex count
};

struct TessellationOutcome
{
    PolygonNetworkStructure               Complex       = {};                                         // [-]   - Result (partial on refusal)
    double                       Sag           = 0.0;                                        // [len] - Effective ε
    double                       EdgeLimit     = 0.0;                                        // [len] - Effective longest side
    double                       Diagonal      = 0.0;                                        // [len] - Body bounding diagonal
    double                       Milliseconds  = 0.0;                                        // [ms]  - Total time
    ChartTriangulationStatistics Triangulation = {};                                         // [-]   - Summed CDT counters
    std::string                  Refusal       = {};                                         // [-]   - Empty on success
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Tessellate every face of a body so that each triangle lies within Sag of its surface and adjacent faces share the
// vertices of their common edges exactly.
[[nodiscard]] TessellationOutcome TessellateBody(const ModelPartition& Partition, BodyToken Body, const TessellationSettings& Settings);

// Largest normal curvature of a surface at a chart point (1/len; 0 for planes; grows without bound at an apex).
[[nodiscard]] double MeasurePeakCurvature(const SurfaceGeometry& Surface, Vector2 Parameter) noexcept;

} // namespace Frontier::GeometrySpace::Parametric
