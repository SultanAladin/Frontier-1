/*====================================================================================================================================
                                                         SKETCHSOLVER.H
====================================================================================================================================*/
// 🧩 2D sketch model and numerical constraint solver: minimum-norm Gauss–Newton on a rank-revealing QR, DOF = n − rank

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/VectorAlgebra.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENUMS
//------------------------------------------------------------------------------------------------------------------------

enum class SketchCurveKind : uint8_t
{
    Line   = 0,                                                                              // Start → End
    Circle = 1,                                                                              // Centre + own radius unknown
    Arc    = 2,                                                                              // Centre, Start → End counter-clockwise; |CS| = |CE| implied
    Spline = 3,                                                                              // Points (Start = first, End = last), Form
};

// 📝 Freeform sketch curves, numbered as Plasticity's curve command keys (1 … 4). Every form resolves to an exact
//    polynomial clamped B-spline (Sketch/SketchSpline.h); the defining points are ordinary solver unknowns.
enum class SketchSplineForm : uint8_t
{
    Hermite     = 1,                                                                         // Through the points, Catmull–Rom tangents · C¹
    Bezier      = 2,                                                                         // One Bézier on the control points · degree n
    Nurbs       = 3,                                                                         // Uniform clamped B-spline on the control points · degree ≤ 3
    CubicSpline = 4,                                                                         // Through the points, cubic · C² (natural or periodic ends)
};

// 📝 References per kind (First / Second / Third): P = point index, L = line, C = circle or arc, S = spline, K = any curve,
//    E = the point index of one end of the spline (its first or last defining point).
enum class SketchConstraintKind : uint8_t
{
    Coincident         = 0,                                                                  // P, P            · 2 equations
    Fixed              = 1,                                                                  // P at (Value, SecondValue) · 2
    Horizontal         = 2,                                                                  // L               · 1
    Vertical           = 3,                                                                  // L               · 1
    Distance           = 4,                                                                  // P, P, d         · 1
    HorizontalDistance = 5,                                                                  // P, P: x₂ − x₁ = d · 1
    VerticalDistance   = 6,                                                                  // P, P: y₂ − y₁ = d · 1
    Length             = 7,                                                                  // L, d            · 1
    Parallel           = 8,                                                                  // L, L            · 1
    Perpendicular      = 9,                                                                  // L, L            · 1
    Angle              = 10,                                                                 // L, L, θ (from first to second, CCW) · 1
    EqualLength        = 11,                                                                 // L, L            · 1
    Radius             = 12,                                                                 // C, r            · 1
    EqualRadius        = 13,                                                                 // C, C            · 1
    PointOnLine        = 14,                                                                 // P, L            · 1
    PointOnCircle      = 15,                                                                 // P, C            · 1
    Tangent            = 16,                                                                 // L, C or C, C (SecondValue < 0: internal) · 1
    Midpoint           = 17,                                                                 // P, L            · 2
    Symmetric          = 18,                                                                 // P, P, L         · 2
    Concentric         = 19,                                                                 // C, C            · 2
    ArcRule            = 20,                                                                 // C (arc): |CS| = |CE|, added implicitly · 1
    SplineTangent      = 21,                                                                 // S, K, E: S's end tangent ∥ K's tangent there (K a spline: SecondValue 0 start · 1 end) · 1
    SplineCurvature    = 22,                                                                 // S, K, E: tangent as 21 and equal signed curvature (G2) · 2
    PointOnSpline      = 23,                                                                 // P, S: signed distance to the closest point of S · 1
    SplineDirection    = 24,                                                                 // S, −, E: S's end tangent at angle Value (mod π; H 0, V π/2) · 1
};

enum class SketchConstraintStatus : uint8_t
{
    Satisfied   = 0,                                                                         // Independent and met
    Redundant   = 1,                                                                         // Dependent on others, consistent
    Conflicting = 2,                                                                         // Dependent on others, inconsistent
    Unmet       = 3,                                                                         // Independent but not met (no convergence)
    Unsupported = 4,                                                                         // A reference of the wrong kind (spline as line / circle) · left out
    Driven      = 5,                                                                         // A reference (driven) dimension: measured after the solve, no equation
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Geometry projected from another sketch (SolveSpace's reference geometry from an earlier group). The solver holds
//    linked unknowns where they are; the feature tree re-projects them from the source sketch at every regeneration.
struct SketchLink
{
    std::string Sketch = {};                                                                 // [-] - Source sketch step label (empty: not linked)
    int         Index  = -1;                                                                 // [-] - Source point index (point link) or curve index (circle radius link)
};

struct SketchPoint
{
    Vector2    Position = {};                                                                // [len] - Unknowns x, y
    SketchLink Link     = {};                                                                // [-]   - Projected from another sketch: held fixed
};

struct SketchCurve
{
    SketchCurveKind Kind         = SketchCurveKind::Line;                                     // [-]   - Curve type
    int             Identifier   = 0;                                                        // [-]   - Stable id (names: Line7, Arc3, Circle2)
    int             Start        = -1;                                                       // [-]   - Point index (line, arc)
    int             End          = -1;                                                       // [-]   - Point index (line, arc)
    int             Centre       = -1;                                                       // [-]   - Point index (circle, arc)
    double          Radius       = 1.0;                                                      // [len] - Circle radius unknown
    bool            Construction = false;                                                    // [-]   - Reference only, not profile
    SketchSplineForm Form        = SketchSplineForm::CubicSpline;                            // [-]   - Spline form
    std::vector<int> Points      = {};                                                       // [-]   - Spline defining point indices (closed: first = last)
    SketchLink       Link        = {};                                                       // [-]   - Circle projected from another sketch: radius held fixed
    // 📝 Spline parameters (empty: uniform). Through-point forms (Hermite, cubic spline): one τ per defining point,
    //    strictly increasing; NURBS: the whole clamped knot vector (degree = knots − points − 1, ≤ 15). Set by Insert point.
    std::vector<double> Knots    = {};                                                       // [-]   - Spline parameters
};

struct SketchConstraint
{
    SketchConstraintKind Kind        = SketchConstraintKind::Coincident;                     // [-]       - Equation family
    int                  First       = -1;                                                   // [-]       - Reference (see kind)
    int                  Second      = -1;                                                   // [-]       - Reference
    int                  Third       = -1;                                                   // [-]       - Reference
    double               Value       = 0.0;                                                  // [len|rad] - Dimension
    double               SecondValue = 0.0;                                                  // [len|-]   - Fixed y / tangent side
    std::string          Parameter   = {};                                                   // [-]       - Bound parameter name (empty: literal)
    std::string          Label       = {};                                                   // [-]       - Display label ("Width", "H1")
    bool                 DrivenCondition = false;                                            // [-]       - Reference dimension: no equation, Value = the measurement
};

struct SketchModel
{
    std::vector<SketchPoint>      Points      = {};                                          // [-] - Unknown points
    std::vector<SketchCurve>      Curves      = {};                                          // [-] - Lines, circles, arcs
    std::vector<SketchConstraint> Constraints = {};                                          // [-] - User constraints (ArcRule implicit)
};

struct SketchSolveSettings
{
    int    IterationLimit    = 100;                                                          // [-] - Gauss–Newton / LM steps
    double Tolerance         = 1.0e-12;                                                      // [-] - Max |residual| / L
    double RankTolerance     = 1.0e-9;                                                       // [-] - Remaining column norm / largest: rank cut
    double SingularTolerance = 1.0e-3;                                                       // [-] - Conditioning / generic conditioning below this: singular
};

struct SketchConstraintReport
{
    SketchConstraintStatus Status   = SketchConstraintStatus::Satisfied;                     // [-]       - Verdict
    double                 Residual = 0.0;                                                   // [len|rad] - Largest |equation| after the solve
    std::vector<int>       Partners = {};                                                    // [-]       - Constraints it depends on (redundant / conflicting)
};

struct SketchSolveOutcome
{
    bool                                Converged         = false;                           // [-]   - All independent equations met
    int                                 Iterations        = 0;                               // [-]   - Steps taken
    int                                 Unknowns          = 0;                               // [-]   - n (free unknowns: linked ones excluded)
    int                                 Linked            = 0;                               // [-]   - Unknowns held by links
    int                                 Equations         = 0;                               // [-]   - m (incl. implicit arc rules)
    int                                 Rank              = 0;                               // [-]   - Numerical rank of J at the solution
    int                                 StructuralRank    = 0;                               // [-]   - Rank at a generic perturbed configuration
    int                                 DegreesOfFreedom  = 0;                               // [-]   - n − Rank
    bool                                SingularCondition = false;                           // [-]   - Rank drop or conditioning collapse vs generic
    double                              Conditioning      = 1.0;                             // [-]   - min |R_kk| / |R_00| (largest-first pivots)
    double                              GenericConditioning = 1.0;                           // [-]   - The same at the generic configuration
    double                              MaxResidual       = 0.0;                             // [len] - Largest |equation| (lengths; angles × L)
    double                              Scale             = 1.0;                             // [len] - Characteristic length L
    std::vector<SketchConstraintReport> Constraints       = {};                              // [-]   - Per user constraint
    std::vector<int>                    Redundant         = {};                              // [-]   - Constraint indices (consistent dependants)
    std::vector<std::vector<int>>       ConflictGroups    = {};                              // [-]   - Each: a dependant + what it depends on
    std::vector<bool>                   PointFixed        = {};                              // [-]   - Point has no free direction
    std::vector<bool>                   CurveFixed        = {};                              // [-]   - All its points (and radius) fixed
    double                              Milliseconds      = 0.0;                             // [ms]  - Wall time
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Solves Sketch in place (research § M 🥇, SolveSpace / planegcs family):
//    ① unknowns x, y per point and r per circle, scaled by L; equations scaled to be dimensionless;
//    ② exact Jacobians by local forward-mode differentiation (≤ 8 unknowns per equation);
//    ③ step = minimum-norm solution of J·Δ = −F over the independent rows of a column-pivoted QR of Jᵀ (moves the
//       sketch as little as possible), backtracking line search, Levenberg–Marquardt when the line search fails;
//    ④ diagnosis at the solution: rank, DOF = n − rank, dependent rows expressed in the independent ones → redundant
//       (consistent) or conflicting groups, null space of J → which points are fully constrained, and the rank at a
//       generic perturbation (a lower rank at the solution is a positional singularity, not a structural one).
[[nodiscard]] SketchSolveOutcome SolveSketch(SketchModel& Sketch, const SketchSolveSettings& Settings = {});
// Writes the side each shared-endpoint tangency (line or arc into arc) stands on now into its Value (±1), so a later solve
// from moved positions keeps that side; a solve holds the starting side by itself. Rows already holding a side stay.
void HoldSketchTangentSides(SketchModel& Sketch);

// Point / curve helpers for builders: append and return the index.
int AppendSketchPoint(SketchModel& Sketch, Vector2 Position);
int AppendSketchLine(SketchModel& Sketch, int Start, int End, int Identifier, bool Construction = false);
int AppendSketchCircle(SketchModel& Sketch, int Centre, double Radius, int Identifier, bool Construction = false);
int AppendSketchArc(SketchModel& Sketch, int Centre, int Start, int End, int Identifier, bool Construction = false);
int AppendSketchSpline(SketchModel& Sketch, std::vector<int> Points, SketchSplineForm Form, int Identifier, bool Construction = false);
int AppendSketchConstraint(SketchModel& Sketch, SketchConstraintKind Kind, int First, int Second = -1, int Third = -1, double Value = 0.0,
                           const std::string& Label = {}, double SecondValue = 0.0);

// Current value of a dimension kind (length, distance, x / y distance, radius; angle in (−π, π] from the first line to
// the second); NaN for any other kind or a bad reference. The solver writes this into a driven dimension's Value.
[[nodiscard]] double MeasureSketchDimension(const SketchModel& Sketch, const SketchConstraint& Constraint) noexcept;

// Point index of the spline end nearest Position (its first or last defining point), −1 for a closed / bad spline.
[[nodiscard]] int NearestSketchSplineEnd(const SketchModel& Sketch, const SketchCurve& Curve, Vector2 Position) noexcept;

// Arc / circle radius as currently placed (arc: |Centre − Start|).
[[nodiscard]] double MeasureSketchRadius(const SketchModel& Sketch, const SketchCurve& Curve) noexcept;

// "Line7", "Arc3", "Circle2", "Spline4".
[[nodiscard]] std::string SketchCurveName(const SketchCurve& Curve);

[[nodiscard]] const char* SketchConstraintLabel(SketchConstraintKind Kind) noexcept;
[[nodiscard]] const char* SketchConstraintStatusLabel(SketchConstraintStatus Status) noexcept;
[[nodiscard]] const char* SketchSplineFormLabel(SketchSplineForm Form) noexcept;

} // namespace Frontier::GeometrySpace::Parametric
