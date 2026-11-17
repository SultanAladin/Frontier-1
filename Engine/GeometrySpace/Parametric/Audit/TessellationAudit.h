/*====================================================================================================================================
                                                      TESSELLATIONAUDIT.H
====================================================================================================================================*/
// 🧩 Independent audit of a body tessellation: closure, Euler characteristic, deviation, orientation, volume, conformity

#pragma once

#include "Engine/GeometrySpace/Parametric/Tessellation/BodyTessellation.h"

#include <cstdint>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Everything is recomputed from the PolygonComplex and the B-rep; nothing is taken from the tessellator's counters.
struct TessellationAuditReport
{
    // ---- N1 closure and topology
    int64_t UnpairedSides    = 0;                                                            // [-]    - Undirected sides not used once in each direction
    int64_t LaminarSides     = 0;                                                            // [-]    - Sample pairs on laminar (sheet rim) edges — allowed unpaired
    int     EulerComplex     = 0;                                                            // [-]    - V − E + F of the complex
    int     EulerBrep        = 0;                                                            // [-]    - V − E + F − (L − F) of the body
    int     UnusedVertices   = 0;                                                            // [-]    - Positions no triangle references
    // ---- N2 deviation from the face surfaces
    double  PeakDeviation    = 0.0;                                                          // [len]  - Max |sample − surface|
    double  MeanDeviation    = 0.0;                                                          // [len]  - Mean over samples
    int64_t DeviationSamples = 0;                                                            // [-]    - Barycentric samples measured
    // ---- N3 orientation
    int64_t Inverted         = 0;                                                            // [-]    - Triangle normal · outward normal ≤ 0
    int64_t Degenerate       = 0;                                                            // [-]    - Zero-area triangles
    // ---- N4 volume and area
    double  ComplexVolume    = 0.0;                                                          // [len³] - Σ signed tetrahedra
    double  ExactVolume      = 0.0;                                                          // [len³] - B-rep flux integral
    double  ComplexArea      = 0.0;                                                          // [len²] - Σ triangle areas
    // ---- N6 edge conformity
    int64_t EdgeSides        = 0;                                                            // [-]    - Consecutive edge-sample pairs
    int64_t MissingEdgeSides = 0;                                                            // [-]    - Pairs not used once per fin direction
    // ---- N7 quality
    double  MinimumAngle     = 0.0;                                                          // [deg]  - Smallest 3D triangle angle
    double  MeanMinimumAngle = 0.0;                                                          // [deg]  - Mean of per-triangle minima
    double  ShareBelow20     = 0.0;                                                          // [-]    - Fraction with minimum angle < 20°
    double  ShareBelow20Free = 0.0;                                                          // [-]    - Same, triangles without a boundary side
    int     Triangles        = 0;                                                            // [-]    - Triangle count
    int     Vertices         = 0;                                                            // [-]    - Referenced vertex count
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] TessellationAuditReport AuditTessellation(const ModelPartition& Partition, BodyToken Body, const TessellationOutcome& Outcome);

// FNV-1a over positions and triangles: equal for bitwise-identical complexes (determinism gate).
[[nodiscard]] uint64_t DigestPolygonComplex(const PolygonComplex& Complex);

} // namespace Frontier::GeometrySpace::Parametric
