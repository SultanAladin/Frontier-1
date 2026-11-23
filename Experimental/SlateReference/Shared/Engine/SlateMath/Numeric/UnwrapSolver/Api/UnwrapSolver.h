//============================================================================================================================================
//                                                             UNWRAPSOLVER.H
//============================================================================================================================================
// 🧩 Contour-first parameterisation — Convergent, and held to reporting which criterion terminated it.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateMath/Numeric/CurveSolver/Api/CurveSolver.h"
#include "SlateMath/Numeric/TransformProjection/Api/TransformProjection.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   WHAT IS SOLVED
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One chart handed to the solver — its positions, its triangulation, and its boundary loop.
/// note  🔴 Positions are chart-local and in the owner's own object space. `38` §5 keeps every derived
///        property in object space, so a chart flattens identically wherever the owner sits and an artist
///        moving a scene re-derives nothing here.
/// note  🔴 The boundary loop is supplied rather than discovered. A chart with more than one loop is not a disc
///        and cannot be flattened at all; the caller resolves that by subdividing, which is `68` §4.1's response
///        to a fold and is deliberately the same mechanism.
/// tag   owning
struct UnwrapSpecification
{
    std::vector<DocumentPosition>  Positions            = {};        // [mm] - chart-local, object space
    std::vector<std::uint32_t>     TriangleCorners      = {};        // [-]  - three per triangle, into Positions
    std::vector<std::uint32_t>     ContourLoop         = {};        // [-]  - ordered, closed, into Positions
    double                         ConvergenceCriterion = 1.0e-7;    // [-]  - relative to the boundary radius
    std::uint32_t                  IterationLimit     = 4096u;     // [-]  - iterations before termination
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE DISTORTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Area and angle distortion, measured apart from each other.
/// note  🔴 `68` §4 reports them **separately** and this structure is why it can. They trade against each other
///        and no single number expresses both: a surface flattened to preserve angles stretches in area, and an
///        artist texturing a repeating pattern cares about area while one placing a decal cares about angle.
/// note  📐 Area distortion is measured as a ratio against the chart's own mean ratio, so it is scale-free. A
///        raw ratio would report the packing scale as though it were a defect of the flattening.
/// tag   nonallocating, nonthrowing
struct DistortionMeasure
{
    double         MaximumAreaRatio      = 1.0;   // [-]   - worst stretch or compression, one is undistorted
    double         MaximumAngleDeviation = 0.0;   // [deg] - worst corner angle departure
    std::uint32_t  WorstAreaTriangle      = 0u;    // [-]   - where the worst area ratio occurred
    std::uint32_t  WorstAngleTriangle     = 0u;    // [-]   - where the worst angle deviation occurred
    bool           MeasureDeclared        = false; // [-]   - a non-degenerate triangle was measured at all
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SOLVER
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Flattens one chart, boundary first, and reports which condition terminated it.
/// in    Declaring  [-]  the chart, its triangulation and its boundary loop
/// out   Result    [-]  refuses with ContentUnsupported for a triangulation that is not a multiple of three,
///                       an out-of-range corner, a boundary loop shorter than three, or a boundary of no extent
/// note  🔴 Convergent, per `02` §5. The result carries its residual, its iteration count and its termination
///        cause, because a solver that returns its last iterate at the ceiling is indistinguishable from one
///        that converged — and `68` §4's specific consequence is an artist texturing on a domain whose distortion
///        nobody measured.
/// note  📐 The boundary is mapped to a circle by chord length and the interior is relaxed toward the mean-value
///        weighted average of its neighbours. Mean-value weights are strictly positive, so a convex boundary
///        gives a fold-free embedding by construction rather than by inspection — which is the whole reason the
///        boundary is mapped to a circle rather than to the chart's own silhouette.
/// note  ⚠️ Fold-free by construction still leaves folds reachable through degenerate source triangles, so
///        `68` §4.1 tests for them anyway. Construction narrows the failure; it does not remove it.
/// cost  🔴
/// tag   api, nonthrowing
Deliver<ConvergentResult<std::vector<PlanarPosition>>> Solve(const UnwrapSpecification& Declaring);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Convergent, PrecisionGuarantee::Convergent);

/// 🧩 Measures the area and angle distortion of a flattening against the topology it came from.
/// in    Positions        [mm]  the chart's spatial positions
/// in    TriangleCorners  [-]   its triangulation
/// in    Flattened        [-]   the planar positions the solve produced
/// out   Measured         [-]   both measures, and where each was worst
/// note  Degenerate triangles — zero spatial area or zero planar area — contribute nothing. `38` §3 registers them
///        rather than removing them, so they arrive here and would otherwise report an unbounded ratio.
/// cost  🚩
/// tag   api, nonthrowing
DistortionMeasure Measure(const std::vector<DocumentPosition>&  Positions,
                          const std::vector<std::uint32_t>&     TriangleCorners,
                          const std::vector<PlanarPosition>&    Flattened);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

}   // namespace Slate
