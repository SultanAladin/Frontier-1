/*====================================================================================================================================
                                                  CURVESURFACEINTERSECTION.H
====================================================================================================================================*/
// 🧩 Curve ∩ surface for lines/arcs against the analytic surfaces: all crossings, tangencies and coincident stretches

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"
#include "Engine/GeometrySpace/Parametric/Precision/PrecisionSpecification.h"

#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

enum class CrossingCategory
{
    Transversal = 0,                                                                         // Curve passes through the surface
    Tangent     = 1,                                                                         // Curve touches (within precision)
};

struct CurveSurfaceCrossing
{
    double           CurveParameter   = 0.0;                                                 // [-]   - τ on the curve
    Vector2          SurfaceParameter = {};                                                  // [-]   - (𝑢, 𝑣) on the surface
    Vector3          Position         = {};                                                  // [len] - C(τ)
    CrossingCategory Category         = CrossingCategory::Transversal;                       // [-]   - Through or touching
    double           Residual         = 0.0;                                                 // [len] - |signed distance| at C(τ)
};

struct CoincidentInterval
{
    double Low  = 0.0;                                                                       // [-] - τ where the curve joins the surface
    double High = 0.0;                                                                       // [-] - τ where it leaves
};

struct CurveSurfaceOutcome
{
    std::vector<CurveSurfaceCrossing> Crossings    = {};                                     // [-] - Ascending τ
    std::vector<CoincidentInterval>   Coincidences = {};                                     // [-] - Curve lies in the surface
    int                               Candidates   = 0;                                      // [-] - Roots of F and F′ examined
    int                               ClipSteps    = 0;                                      // [-] - Bézier clips
    int                               Subdivisions = 0;                                      // [-] - Halvings
};

struct CurveSurfaceSettings
{
    double Precision          = SessionPrecision;                                            // [len] - Touch / accept distance
    double ParameterTolerance = 1.0e-13;                                                     // [-]   - Bézier-clip width on [0, 1]
    double ScanSpacing        = 0.0;                                                         // [len] - Fixed scan spacing (0 = automatic; set when
                                                                                             //         a caller clips the interval, so clipping keeps
                                                                                             //         the density of the whole curve)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Algorithm (research § E): exact rational Bézier form of the curve → implicit equation composed into a Bernstein
//    polynomial (degree ≤ 8) → Bézier clipping for every sign change, roots of F′ for touches → Newton polishing on the
//    EXACT signed distance d(τ) with d′ = N·C′ → tolerant merge: crossings joined by a stretch within precision of the
//    surface become one Tangent crossing. The surface is untrimmed; callers filter by parameter boxes.
[[nodiscard]] CurveSurfaceOutcome IntersectCurveSurface(const CurveGeometry&        Curve,
                                                        double                      Low,
                                                        double                      High,
                                                        const SurfaceGeometry&      Surface,
                                                        const CurveSurfaceSettings& Settings = {});

} // namespace Frontier::GeometrySpace::Parametric
