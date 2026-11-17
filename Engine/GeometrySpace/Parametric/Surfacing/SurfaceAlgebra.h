/*====================================================================================================================================
                                                         SURFACEALGEBRA.H
====================================================================================================================================*/
// 🧩 Exact B-spline algebra for surfacing: degree elevation, knot union, pole rows / columns, skinning with end derivatives

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Derivative conditions at one end of a skinned family, one vector per pole of the (compatible) curves: First is
//    ∂S/∂𝑣 and Second ∂²S/∂𝑣² of each pole column at that end. Empty vectors mean "no condition".
struct FamilyEndCondition
{
    std::vector<Vector3> First  = {};                                                        // [len] - ∂𝑣 per pole column
    std::vector<Vector3> Second = {};                                                        // [len] - ∂𝑣𝑣 per pole column
};

struct FamilySkinning
{
    int                 Degree     = 3;                                                      // [-] - Degree across (lowered to what the rows allow)
    std::vector<double> Parameters = {};                                                     // [-] - 𝑣ₖ per curve, 𝑣₀ = 0 … 𝑣ₘ = 1, increasing
    FamilyEndCondition  Start      = {};                                                     // [-] - Conditions at 𝑣 = 0
    FamilyEndCondition  End        = {};                                                     // [-] - Conditions at 𝑣 = 1
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Degree elevation by exact collocation: the elevated space (each interior knot one multiplicity higher per added
//    degree) contains the curve, so interpolating its homogeneous form (w·P, w) at the Greville abscissae of that space
//    reproduces it — exact up to rounding (collocation at Greville points is well conditioned). Empty when the
//    collocation is singular or the target degree is out of range.
[[nodiscard]] std::optional<NurbsCurveDefinition> ElevateNurbsCurveDegree(const NurbsCurveDefinition& Definition, int Degree);

// Exact split at τ inside the open domain (knot insertion to multiplicity p): the halves share the pole at τ and keep
// their knot values (first half on [Low, τ], second on [τ, High]). Empty when τ is not strictly inside the domain.
[[nodiscard]] std::optional<std::pair<NurbsCurveDefinition, NurbsCurveDefinition>> SplitNurbsCurve(const NurbsCurveDefinition& Definition, double τ);

// 💡 One clamped curve through pieces joined end to start (piece k's start within Join of piece k − 1's end): every
//    piece raised exactly to the highest degree, piece k mapped onto a parameter span proportional to Spans[k] (e.g.
//    its length), its weights rescaled so the shared pole carries one weight (a uniform weight scale leaves a rational
//    curve unchanged), junctions at full multiplicity p (G0, exact). Empty on a gap, a bad span or a failed elevation.
[[nodiscard]] std::optional<NurbsCurveDefinition> JoinNurbsCurves(const std::vector<NurbsCurveDefinition>& Pieces, const std::vector<double>& Spans, double Join);

// Interior knots of a clamped vector, value → multiplicity.
[[nodiscard]] std::vector<std::pair<double, int>> CollectInteriorKnots(const std::vector<double>& Knots, int Degree);

// Values to insert into Knots so that it holds every interior knot of Target at least with Target's multiplicity.
[[nodiscard]] std::vector<double> MissingInteriorKnots(const std::vector<double>& Knots, const std::vector<double>& Target, int Degree);

// Clamped vector on [0, 1] holding every interior knot of every input at its largest multiplicity (same degree).
[[nodiscard]] std::vector<double> UniteKnotVectors(const std::vector<std::vector<double>>& Vectors, int Degree);

// Pole row j (the 𝑢-curve of poles Pᵢⱼ) and pole column i (the 𝑣-curve of poles Pᵢⱼ) of a surface, as curves.
[[nodiscard]] NurbsCurveDefinition ExtractPoleRow(const NurbsSurfaceDefinition& Definition, int Row);
[[nodiscard]] NurbsCurveDefinition ExtractPoleColumn(const NurbsSurfaceDefinition& Definition, int Column);

// Surface whose pole rows (𝑢-curves, AlongU) or pole columns (𝑣-curves) are the given compatible curves.
[[nodiscard]] NurbsSurfaceDefinition AssembleFromRows(const std::vector<NurbsCurveDefinition>& Rows, int DegreeV, const std::vector<double>& KnotsV);
[[nodiscard]] NurbsSurfaceDefinition AssembleFromColumns(const std::vector<NurbsCurveDefinition>& Columns, int DegreeU, const std::vector<double>& KnotsU);

// Exact surface edits: elevate to (DegreeU, DegreeV); insert knots in each direction; swap 𝑢 and 𝑣; reverse a direction.
[[nodiscard]] std::optional<NurbsSurfaceDefinition> ElevateNurbsSurfaceDegree(const NurbsSurfaceDefinition& Definition, int DegreeU, int DegreeV);
[[nodiscard]] NurbsSurfaceDefinition RefineNurbsSurfaceKnots(const NurbsSurfaceDefinition& Definition, const std::vector<double>& InsertedU,
                                                             const std::vector<double>& InsertedV);
[[nodiscard]] NurbsSurfaceDefinition TransposeNurbsSurface(const NurbsSurfaceDefinition& Definition);
[[nodiscard]] NurbsSurfaceDefinition ReverseNurbsSurface(const NurbsSurfaceDefinition& Definition, bool AlongU);

// Validated carrier from a definition (seed lattice and extent filled in); empty when the definition is invalid.
[[nodiscard]] std::optional<NurbsSurface> FinishNurbsSurface(const NurbsSurfaceDefinition& Definition);

// 💡 Skinning of compatible curves (same degree, same knots on [0, 1]) across 𝑣: every pole column interpolated in
//    homogeneous form at the 𝑣ₖ, optionally with ∂𝑣 / ∂𝑣𝑣 conditions at either end (non-rational curves only). Each
//    condition adds one pole per column; the 𝑣-knots average the parameter list with the conditioned ends repeated
//    (P&T § 9.2.2), so S(𝑢, 𝑣ₖ) ≡ curve k and the end derivatives hold exactly. Refusal is filled when empty.
[[nodiscard]] std::optional<NurbsSurfaceDefinition> SkinCurveFamily(const std::vector<NurbsCurveDefinition>& Curves, const FamilySkinning& Skinning,
                                                                    std::string& Refusal);

// Affine τ′ = (τ − Low) / (High − Low): the same curve on [0, 1] (clamped end knots exactly 0 and 1, periodic flag cleared).
[[nodiscard]] NurbsCurveDefinition MapCurveOntoUnit(const NurbsCurveDefinition& Definition);

} // namespace Frontier::GeometrySpace::Parametric
