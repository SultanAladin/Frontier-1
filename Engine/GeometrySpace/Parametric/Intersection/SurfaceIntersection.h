/*====================================================================================================================================
                                                    SURFACEINTERSECTION.H
====================================================================================================================================*/
// 🧩 Surface ∩ surface over parameter boxes: exact analytic branches where they exist, hybrid lattice + marching elsewhere

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"
#include "Engine/GeometrySpace/Parametric/Precision/PrecisionSpecification.h"

#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Parameter rectangle of a face's chart. A periodic direction spanning a full period has no boundary there.
struct ParameterBox
{
    Vector2 Low  = {};                                                                       // [-] - (𝑢, 𝑣) lower corner
    Vector2 High = {};                                                                       // [-] - (𝑢, 𝑣) upper corner
};

enum class IntersectionPolicy
{
    Hybrid       = 0,                                                                        // Analytic first, marching otherwise
    MarchingOnly = 1,                                                                        // Skip the analytic dispatch (validation)
};

struct SurfaceIntersectionSettings
{
    IntersectionPolicy Policy               = IntersectionPolicy::Hybrid;                    // [-]   - Dispatch policy
    double             ChordTolerance       = 1.0e-3;                                        // [len] - Polyline to true curve
    double             MaximumStep          = 0.25;                                          // [len] - Marching step cap
    double             MaximumTurn          = 0.2;                                           // [rad] - Tangent turn per step
    double             SingularSine         = 2.0e-4;                                        // [-]   - |N₁ × N₂| ending a trace
    double             Precision            = SessionPrecision;                              // [len] - Point acceptance
    int                LatticeDivisions     = 8;                                             // [-]   - Iso-lattice cells per direction
    int                LoopDepth            = 3;                                             // [-]   - Normal-cone subdivision levels
    bool               LoopDetectionEnabled = true;                                          // [-]   - Small-loop search
    int                PointLimit           = 20000;                                         // [-]   - Per branch
};

enum class BranchCategory
{
    Line   = 0,                                                                              // Exact straight line
    Circle = 1,                                                                              // Exact circle / arc
    Traced = 2,                                                                              // Marched polyline on both surfaces
};

enum class BranchTermination
{
    Closed   = 0,                                                                            // Loop, no ends
    Boundary = 1,                                                                            // Leaves a parameter box
    Singular = 2,                                                                            // Ends at a tangency point (N₁ ∥ N₂)
    Limit    = 3,                                                                            // Point budget exhausted (defect)
};

struct IntersectionChartPoint
{
    Vector3 Position = {};                                                                   // [len] - On both surfaces
    Vector2 First    = {};                                                                   // [-]   - (𝑢, 𝑣) on the first surface
    Vector2 Second   = {};                                                                   // [-]   - (𝑢, 𝑣) on the second surface
};

struct IntersectionBranch
{
    BranchCategory                      Category            = BranchCategory::Traced;        // [-] - Exact or traced
    CurveGeometry                       Curve               = StraightLine{};                // [-] - Exact carrier (Line / Circle)
    double                              CurveLow            = 0.0;                           // [-] - τ range on Curve
    double                              CurveHigh           = 0.0;                           // [-] - τ range on Curve
    std::vector<IntersectionChartPoint> Chart               = {};                            // [-] - Ordered points
    bool                                ClosedCondition     = false;                         // [-] - Last point joins the first
    bool                                TangentialCondition = false;                         // [-] - Surfaces tangent along it
    BranchTermination                   StartTermination    = BranchTermination::Closed;     // [-] - How the first end arises
    BranchTermination                   EndTermination      = BranchTermination::Closed;     // [-] - How the last end arises
};

enum class SpecialPointCategory
{
    Singular = 0,                                                                            // Branches meet / cross (N₁ ∥ N₂)
    Touch    = 1,                                                                            // Isolated contact point
};

struct IntersectionSpecialPoint
{
    Vector3              Position = {};                                                      // [len] - On both surfaces
    SpecialPointCategory Category = SpecialPointCategory::Singular;                          // [-]   - Kind
};

struct IntersectionStatistics
{
    int LatticeCurves    = 0;                                                                // [-] - Iso-curves intersected
    int LatticeCrossings = 0;                                                                // [-] - Start candidates from them
    int LoopCellPairs    = 0;                                                                // [-] - Leaf pairs with overlapping normal cones
    int LoopSeeds        = 0;                                                                // [-] - Start candidates from them
    int SeedsTraced      = 0;                                                                // [-] - Candidates that started a branch
    int SeedsSkipped     = 0;                                                                // [-] - Candidates already on a branch
    int Corrections      = 0;                                                                // [-] - Corrector solves
    int StepRejections   = 0;                                                                // [-] - Steps halved
};

struct SurfaceIntersectionOutcome
{
    std::vector<IntersectionBranch>       Branches            = {};                          // [-] - Curves
    std::vector<IntersectionSpecialPoint> SpecialPoints       = {};                          // [-] - Singular and touch points
    bool                                  CoincidentCondition = false;                       // [-] - Surfaces are the same
    std::string                           Method              = {};                          // [-] - Which path solved it
    IntersectionStatistics                Statistics          = {};                          // [-] - Work counters
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Hybrid (research § F):
//    ① Analytic dispatch — plane–plane; coaxial surfaces of revolution (incl. plane ⟂ axis, sphere–sphere) by
//       intersecting their meridians in the (ρ, z) half-plane; parallel cylinders / plane ∥ axis by cross-section;
//       plane through a cone / torus axis. Exact lines and circles, clipped to both boxes.
//    ② Otherwise — start points from exact curve–surface intersection of the first surface's iso-lattice and the
//       second box's boundary curves; normal-cone subdivision (Sederberg–Meyers) seeds loops smaller than a lattice cell;
//       marching with a 3D corrector on both signed distances; boundary, closure and singular-point termination.
[[nodiscard]] SurfaceIntersectionOutcome IntersectSurfaces(const SurfaceGeometry&             First,
                                                           const ParameterBox&                FirstBox,
                                                           const SurfaceGeometry&             Second,
                                                           const ParameterBox&                SecondBox,
                                                           const SurfaceIntersectionSettings& Settings = {});

// Box membership with periodic folding (exposed for validation and encoding).
[[nodiscard]] bool InsideParameterBox(const SurfaceGeometry& Surface, const ParameterBox& Box, Vector2 Parameter, double Slack) noexcept;

[[nodiscard]] const char* BranchTerminationLabel(BranchTermination Termination) noexcept;

} // namespace Frontier::GeometrySpace::Parametric
