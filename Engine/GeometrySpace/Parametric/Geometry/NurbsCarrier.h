/*====================================================================================================================================
                                                         NURBSCARRIER.H
====================================================================================================================================*/
// 🧩 Rational B-spline curves and surfaces as first-class carriers: exact rational derivatives, inversion, iso-curves, affine images

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"

#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    DEFINITIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Option A of the freeform plan, transplanted from the Sketcher NURBS organ (Piegl & Tiller A2.1, A2.3, A3.2, A4.2,
//    A4.4) and extended with exact second derivatives. Clamped knot vectors only; weights strictly positive. Poles are
//    stored Euclidean with a separate weight, the homogeneous form wᵢ·Pᵢ is formed on evaluation.
//    A carrier is periodic in a direction when its first and last pole rows coincide and ∂P matches across the seam
//    (full rational circles and revolutions): its parameter then folds by the domain length like an analytic angle.

inline constexpr int NurbsDegreeLimit = 15;                                                  // [-] - Highest supported degree

struct NurbsCurveDefinition
{
    int                  Degree  = 1;                                                        // [-]   - p
    std::vector<double>  Knots   = {};                                                       // [-]   - Clamped, n + p + 2 values
    std::vector<Vector3> Poles   = {};                                                       // [len] - Pᵢ, n + 1 of them
    std::vector<double>  Weights = {};                                                       // [-]   - wᵢ > 0
    double               Period  = 0.0;                                                      // [-]   - Domain length when periodic
};

struct NurbsSurfaceDefinition
{
    int                  DegreeU    = 1;                                                     // [-]   - p (along 𝑢)
    int                  DegreeV    = 1;                                                     // [-]   - q (along 𝑣)
    int                  CountU     = 2;                                                     // [-]   - Poles along 𝑢
    int                  CountV     = 2;                                                     // [-]   - Poles along 𝑣
    std::vector<double>  KnotsU     = {};                                                    // [-]   - Clamped
    std::vector<double>  KnotsV     = {};                                                    // [-]   - Clamped
    std::vector<Vector3> Poles      = {};                                                    // [len] - 𝑢-major: Pᵢⱼ at i·CountV + j
    std::vector<double>  Weights    = {};                                                    // [-]   - wᵢⱼ > 0, same layout
    double               PeriodU    = 0.0;                                                   // [-]   - Domain length when periodic in 𝑢
    double               PeriodV    = 0.0;                                                   // [-]   - Domain length when periodic in 𝑣
    int                  SeedCountU = 0;                                                     // [-]   - Inversion seed lattice size
    int                  SeedCountV = 0;                                                     // [-]
    std::vector<Vector3> Seeds      = {};                                                    // [len] - Lattice positions, 𝑢-major
    double               Extent     = 1.0;                                                   // [len] - Pole bounding diagonal
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct NurbsCurveDerivatives
{
    Vector3 Position = {};                                                                   // [len] - C(τ)
    Vector3 First    = {};                                                                   // [len] - C′(τ)
    Vector3 Second   = {};                                                                   // [len] - C″(τ)
};

struct NurbsSurfaceDerivatives
{
    Vector3 Position = {};                                                                   // [len] - S
    Vector3 U        = {};                                                                   // [len] - S𝑢
    Vector3 V        = {};                                                                   // [len] - S𝑣
    Vector3 UU       = {};                                                                   // [len] - S𝑢𝑢
    Vector3 UV       = {};                                                                   // [len] - S𝑢𝑣
    Vector3 VV       = {};                                                                   // [len] - S𝑣𝑣
};

struct NurbsDomain
{
    double Low  = 0.0;                                                                       // [-] - First clamped knot
    double High = 1.0;                                                                       // [-] - Last clamped knot
};

using PointMap = std::function<Vector3(Vector3)>;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS — CURVES
//------------------------------------------------------------------------------------------------------------------------

// Validated construction (clamped, non-decreasing knots, positive weights, matching counts); empty when invalid.
[[nodiscard]] std::optional<NurbsCurve> ConstructNurbsCurve(int Degree, std::vector<double> Knots, std::vector<Vector3> Poles, std::vector<double> Weights);

[[nodiscard]] NurbsDomain           MeasureNurbsCurveDomain(const NurbsCurveDefinition& Definition) noexcept;
[[nodiscard]] NurbsCurveDerivatives EvaluateNurbsCurveDerivatives(const NurbsCurveDefinition& Definition, double τ) noexcept;
[[nodiscard]] CurveSample           EvaluateNurbsCurve(const NurbsCurveDefinition& Definition, double τ) noexcept;

// Seeded Newton (Piegl & Tiller 6.1) on |C(τ) − X|²; periodic results are folded into [IntervalLow, IntervalLow + T).
[[nodiscard]] double                ProjectOntoNurbsCurve(const NurbsCurveDefinition& Definition, Vector3 Position, double IntervalLow) noexcept;

// Distinct interior knots in (Low, High), repeated per period; quadrature and marching split there.
[[nodiscard]] std::vector<double>   CollectNurbsCurveKnots(const NurbsCurveDefinition& Definition, double Low, double High);

// Rigorous sagitta bound: the convex hull of the poles of the covering spans against each piece's chord.
[[nodiscard]] double                MeasureNurbsCurveBulge(const NurbsCurveDefinition& Definition, double Low, double High, int Pieces) noexcept;
[[nodiscard]] double                MeasureNurbsCurveScale(const NurbsCurveDefinition& Definition) noexcept;

// τ′ = −τ (poles reversed, knots negated and reversed): the same point set traversed backwards, the convention of every
// reversed carrier (lines, conics, rails), so a reversed edge keeps the interval [−High, −Low].
[[nodiscard]] NurbsCurve            ReverseNurbsCurve(const NurbsCurveDefinition& Definition);

// 📝 Affine maps act on the poles and keep weights and knots: exact, including non-uniform scale and shear.
[[nodiscard]] NurbsCurve            MapNurbsCurve(const NurbsCurveDefinition& Definition, const PointMap& Map);

// 📝 Bézier extraction (Boehm insertion to multiplicity p at every knot in [Low, High], homogeneous so it stays exact):
//    each piece is the curve on [PieceLow, PieceHigh] with τ = PieceLow + 𝑡·(PieceHigh − PieceLow). Periodic intervals
//    may cross the seam; they are split there.
struct NurbsBezierPiece
{
    std::vector<Vector3> Poles     = {};                                                     // [len] - Pᵢ
    std::vector<double>  Weights   = {};                                                     // [-]   - wᵢ
    double               PieceLow  = 0.0;                                                    // [-]   - τ at 𝑡 = 0
    double               PieceHigh = 0.0;                                                    // [-]   - τ at 𝑡 = 1
};
[[nodiscard]] std::vector<NurbsBezierPiece> DecomposeNurbsCurve(const NurbsCurveDefinition& Definition, double Low, double High);

// 📝 Non-zero B-spline basis functions at τ (clamped vector, Count poles) and their first two derivatives:
//    Values[k][r] = N⁽ᵏ⁾_{First + r, p}(τ) for r = 0 … p — the rows of fitting and interpolation systems.
struct NurbsBasisSample
{
    int                                First  = 0;                                           // [-] - Index of the first non-zero function
    std::array<std::vector<double>, 3> Values = {};                                          // [-] - Value, first, second derivative
};
[[nodiscard]] NurbsBasisSample EvaluateNurbsBasis(const std::vector<double>& Knots, int Degree, int Count, double τ);

// 📝 Knot refinement (Boehm insertion of each value, A5.1, homogeneous so it stays exact): the same curve on more knots.
//    Values must lie inside the domain; a value already present raises its multiplicity (capped at p).
[[nodiscard]] NurbsCurveDefinition RefineNurbsCurveKnots(const NurbsCurveDefinition& Definition, const std::vector<double>& Inserted);

// Exact conversions with the τ-domain [Low, High] kept: a line is degree 1 (τ unchanged), a circle or ellipse is
// rational quadratic in ≤ π/2 pieces whose joints sit at the conic angles Low + k·Δ (τ matches the angle there).
[[nodiscard]] NurbsCurve            ConvertLineToNurbs(const StraightLine& Line, double Low, double High);
[[nodiscard]] NurbsCurve            ConvertConicToNurbs(const ConicForm& Conic, double Low, double High);
[[nodiscard]] std::optional<NurbsCurve> ConvertCurveToNurbs(const CurveGeometry& Curve, double Low, double High);

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS — SURFACES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::optional<NurbsSurface> ConstructNurbsSurface(int DegreeU, int DegreeV, int CountU, int CountV, std::vector<double> KnotsU,
                                                                std::vector<double> KnotsV, std::vector<Vector3> Poles, std::vector<double> Weights);

[[nodiscard]] NurbsDomain             MeasureNurbsSurfaceDomain(const NurbsSurfaceDefinition& Definition, bool AlongU) noexcept;
[[nodiscard]] NurbsSurfaceDerivatives EvaluateNurbsSurfaceDerivatives(const NurbsSurfaceDefinition& Definition, Vector2 Parameter) noexcept;
[[nodiscard]] SurfaceSample           EvaluateNurbsSurface(const NurbsSurface& Surface, Vector2 Parameter) noexcept;

// Unit S𝑢 × S𝑣; at a collapsed pole row (sphere pole, cone apex) the limit S𝑢𝑣 × S𝑣 (or S𝑢 × S𝑢𝑣) oriented inward.
[[nodiscard]] Vector3                 EvaluateNurbsNormal(const NurbsSurface& Surface, Vector2 Parameter) noexcept;

// Global closest point: best seeds of the pole-lattice samples, Newton with exact second derivatives, clamped to the
// domain (periodic directions wrap and take the representative nearest to Reference).
[[nodiscard]] Vector2                 ProjectOntoNurbsSurface(const NurbsSurface& Surface, Vector3 Position, Vector2 Reference) noexcept;
// 💡 Local inversion: Newton from Start alone. The foot is returned only when it is certified locally — X − S ⟂ S𝑢, S𝑣
//    to rounding, a strict minimum (positive-definite Hessian), interior, and within a tenth of the domain of Start —
//    else nullopt and the caller inverts globally. For points stepped in small moves from a globally inverted one.
[[nodiscard]] std::optional<Vector2>  PolishNurbsFootLocally(const NurbsSurface& Surface, Vector3 Position, Vector2 Start) noexcept;
[[nodiscard]] SurfacePeriodicity      ResolveNurbsPeriodicity(const NurbsSurface& Surface) noexcept;

// Distinct interior knots of one direction inside (Low, High), repeated per period.
[[nodiscard]] std::vector<double>     CollectNurbsSurfaceKnots(const NurbsSurface& Surface, bool AlongU, double Low, double High);

// Largest |principal curvature| from the fundamental forms (exact second derivatives).
[[nodiscard]] double                  MeasureNurbsCurvature(const NurbsSurface& Surface, Vector2 Parameter) noexcept;

// Exact iso-curves: FixedU gives the 𝑣-curve at 𝑢 = Value (τ = 𝑣), otherwise the 𝑢-curve at 𝑣 = Value (τ = 𝑢).
[[nodiscard]] NurbsCurve              ExtractNurbsIsoCurve(const NurbsSurface& Surface, bool FixedU, double Value);

[[nodiscard]] NurbsSurface            MapNurbsSurface(const NurbsSurfaceDefinition& Definition, const PointMap& Map);

// 📝 Natural extension: every open (non-periodic) direction grows by Extension × its domain length at both ends, the end
//    spans continued as the same polynomials (blossoming: the new end poles are Qᵢ = blossom of the end span at
//    (s × (p − i), k_{p+1} … k_{p+i}) in homogeneous form). Inside the old domain the surface and its chart are unchanged
//    — a blend can then roll past the end of a support (Parasolid's surface extension) while its rails stay on the face.
//    nullopt when a rational end span would turn a weight non-positive.
[[nodiscard]] std::optional<NurbsSurface> ExtendNurbsSurface(const NurbsSurfaceDefinition& Definition, double Extension);

// 📝 Surface of revolution: the profile (in any half-plane through the axis) swept by exact rational circles about
//    Axis.ZAxis, 𝑢 counter-clockwise from Axis.XAxis + StartAngle over Sweep, 𝑢-domain [StartAngle, StartAngle + Sweep];
//    the profile's τ becomes 𝑣. Poles on the axis collapse to a point row (sphere poles, cone apex).
[[nodiscard]] NurbsSurface            ConstructNurbsRevolution(const NurbsCurveDefinition& Profile, const OrthonormalBasis& Axis, double StartAngle, double Sweep);

// Exact NURBS image of an analytic carrier covering Box: same chart orientation (S𝑢 × S𝑣 along the analytic normal);
// planes, cylinders and cones keep (𝑢, 𝑣) exactly, angular directions match at the ≤ π/2 joints only. Empty for
// procedural carriers.
[[nodiscard]] std::optional<NurbsSurface> ConvertSurfaceToNurbs(const SurfaceGeometry& Surface, Vector2 BoxLow, Vector2 BoxHigh);

} // namespace Frontier::GeometrySpace::Parametric
