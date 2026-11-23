/*====================================================================================================================================
                                                       PROJECTEDCURVE.H
====================================================================================================================================*/
// 🧩 SP-curves for section curves: exact line / ellipse when the chart image is affine, exact projection otherwise

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"

#include <optional>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 q(τ) = ProjectOntoSurface(S, C(τ), reference(τ)); the reference is the linear interpolation of an unwrap table so
//    the periodic image never jumps. dq/dτ = (JᵀJ)⁻¹ Jᵀ C′(τ) with J = [∂S/∂𝑢 ∂S/∂𝑣] — exact for a curve on the surface.
struct ProjectedParameterCurve
{
    CurveGeometry        Curve    = StraightLine{};                                          // [-] - 3D carrier (copy)
    SurfaceGeometry      Surface  = PlaneSurface{};                                          // [-] - Chart surface (copy)
    double               Low      = 0.0;                                                     // [-] - τ range of the table
    double               High     = 1.0;                                                     // [-] - τ range of the table
    std::vector<Vector2> Unwrap   = {};                                                      // [-] - Uniform-τ chart samples
};

// 📝 Window for the periodic image: the curve midpoint is folded into [Low, Low + period) per periodic direction.
struct ParameterWindow
{
    Vector2 Low  = {};                                                                       // [-] - Window corner
    Vector2 High = {};                                                                       // [-] - Window corner
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 SP-curve of a 3D curve lying on Surface over τ ∈ [Low, High]:
//    ① plane: exact ParameterLine / ParameterEllipse from the frame projection;
//    ② other surfaces: projected samples tested for an affine image (iso-lines, generators) → ParameterLine;
//    ③ otherwise a ParameterProjection (exact, closed-form inversion per evaluation).
//    The periodic image of the midpoint is the one nearest Anchor when given (re-fitting an existing fin), else the one
//    folded into the window.
[[nodiscard]] ParameterCurve FitSurfaceCurve(const SurfaceGeometry& Surface,
                                             const CurveGeometry&   Curve,
                                             double                 Low,
                                             double                 High,
                                             const ParameterWindow& Window,
                                             std::optional<Vector2> Anchor = std::nullopt);

// Largest |S(q(τ)) − C(τ)| over Samples uniform τ (validation of an SP-curve against its carrier).
[[nodiscard]] double MeasureSurfaceCurveDeviation(const SurfaceGeometry& Surface,
                                                  const CurveGeometry&   Curve,
                                                  const ParameterCurve&  SurfaceCurve,
                                                  double                 Low,
                                                  double                 High,
                                                  int                    Samples);

} // namespace Frontier::GeometrySpace::Parametric
