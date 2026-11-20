/*====================================================================================================================================
                                                          CURVENETWORK.H
====================================================================================================================================*/
// 🧩 Curve networks (vehicle plan V5): family sorting, crossing search, compatible reparametrisation, Gordon / Coons surface

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Feature/ProfileSweep.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/PanelJoining.h"
#include "Engine/GeometrySpace/Parametric/Connectivity/ModelPartition.h"

#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

struct NetworkSettings
{
    double MeetingGap      = 5.0e-4;                                                         // [len] - Two curves closer than this meet (0.5 mm)
    double Reapproximation = 1.0e-6;                                                         // [len] - Refitted curve vs reparametrised original (1 µm)
    int    SpanLimit       = 160;                                                            // [-]   - Spans per family before refusing
};

// 📝 One crossing of a first-family curve j with a second-family curve i: the parameters on both (each curve on [0, 1])
//    and the gap between the two foot points.
struct NetworkCrossing
{
    double First  = 0.0;                                                                     // [-]   - 𝑡 on the first-family curve
    double Second = 0.0;                                                                     // [-]   - 𝑠 on the second-family curve
    double Gap    = 0.0;                                                                     // [len] - |C(𝑡) − D(𝑠)|
};

struct NetworkSurfaceOutcome
{
    NurbsSurface            Surface              = {};                                       // [-]   - 𝑢 along the first family, 𝑣 along the second
    std::vector<int>        FirstOrder           = {};                                       // [-]   - Input indices of the first family, in 𝑣 order
    std::vector<int>        SecondOrder          = {};                                       // [-]   - Input indices of the second family, in 𝑢 order
    std::vector<NurbsCurve> First                = {};                                       // [-]   - Refitted first family: S(𝑢, 𝑣ⱼ) ≡ First[j]
    std::vector<NurbsCurve> Second               = {};                                       // [-]   - Refitted second family: S(𝑢ᵢ, 𝑣) ≡ Second[i]
    std::vector<double>     FirstParameters      = {};                                       // [-]   - 𝑣ⱼ
    std::vector<double>     SecondParameters     = {};                                       // [-]   - 𝑢ᵢ
    int                     Reversed             = 0;                                        // [-]   - Curves turned to run with their family
    double                  WorstGap             = 0.0;                                      // [len] - Largest crossing gap
    double                  WorstShift           = 0.0;                                      // [len] - Largest move of a curve to close its crossings
    double                  WorstReapproximation = 0.0;                                      // [len] - Largest refit error (beyond the shift)
    std::string             Refusal              = {};                                       // [-]   - "Network: …" when refused
};

struct NetworkSheetOutcome
{
    NetworkSurfaceOutcome Network = {};                                                      // [-] - The carrier and its families
    SweepOutcome          Sheet   = {};                                                      // [-] - Sheet body (one face) and its role
};

struct ClosedLoopEdge
{
    int  CurveIndex        = -1;                                                             // [-] - Index into input Curves
    bool ReversedCondition = false;                                                          // [-] - Traverse curve 1 -> 0 around the loop
};

struct ClosedCurveLoop
{
    std::vector<ClosedLoopEdge> Edges = {};                                                  // [-] - Consecutive loop edges (head-to-tail)
};

struct ClosedLoopSurfaceOutcome
{
    std::vector<SurfacePanel> Panels  = {};                                                  // [-] - 1 panel for N <= 4, N panels for N >= 5
    std::string               Refusal = {};                                                  // [-] - Non-empty when refused
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Closest pair of points between two curves (each mapped onto [0, 1]): lattice seeds, then Newton on |C(𝑡) − D(𝑠)|².
[[nodiscard]] NetworkCrossing FindCurveCrossing(const NurbsCurveDefinition& First, const NurbsCurveDefinition& Second);

// 💡 Gordon surface of two curve families (Gordon 1971; P&T § 10.5): ① every first-family curve must cross every
//    second-family curve within MeetingGap, the outer curves bounding the network (no curve runs past a boundary
//    curve — trimming 🚧); ② each family sorted by its crossings and turned to run one way; ③ common parameters 𝑢ᵢ, 𝑣ⱼ
//    (crossing averages), each curve reparametrised by a monotone (Fritsch–Carlson) map, shifted by a smooth spline so
//    it passes the crossing midpoint Pᵢⱼ, then refitted on one knot vector per family with the crossings interpolated
//    exactly (KKT); ④ S = S₁ + S₂ − T (skin of the first family + skin of the second − tensor interpolant of Pᵢⱼ),
//    made compatible by exact degree elevation and knot union. Two plus two curves give the Coons patch.
[[nodiscard]] NetworkSurfaceOutcome ConstructGordonSurface(const std::vector<NurbsCurve>& First, const std::vector<NurbsCurve>& Second,
                                                           const NetworkSettings& Settings, const std::vector<std::string>& FirstNames = {},
                                                           const std::vector<std::string>& SecondNames = {});

// 💡 Unsorted network: curves that meet are joined, the meeting relation must 2-colour into two families (an odd cycle
//    or a curve meeting nothing is refused, named), then ConstructGordonSurface. Also accepts closed 1..4-edge boundary
//    loops (triangles, squares, rectangles). Indices in the outcome refer to Curves.
[[nodiscard]] NetworkSurfaceOutcome ConstructNetworkSurface(const std::vector<NurbsCurve>& Curves, const NetworkSettings& Settings,
                                                            const std::vector<std::string>& Names = {});

// True when curves form a 2-family crossing network OR a single closed 1..4-edge boundary loop.
[[nodiscard]] bool DetectCurveNetwork(const std::vector<NurbsCurve>& Curves, const NetworkSettings& Settings);

// Detects minimal closed boundary loops (triangles, squares, rectangles, N-gons, single closed curves)
// from a set of curves, pruning dangling overhang stubs and rejecting composite outer perimeter cycles.
[[nodiscard]] std::vector<ClosedCurveLoop> DetectClosedCurveLoops(const std::vector<NurbsCurve>& Curves, double CornerTolerance = 1.0e-3);

// Builds NURBS surface panel(s) from one closed boundary loop (1-curve closed, 2-curve lens, 3-curve triangle,
// 4-curve square/rectangle/quad, or N-curve polygon).
[[nodiscard]] ClosedLoopSurfaceOutcome ConstructClosedLoopPanels(const std::vector<NurbsCurve>& OrderedEdges, double CornerTolerance = 1.0e-3,
                                                                 const std::vector<std::string>& Names = {});

// The network surface as a one-face sheet body (iso-curve boundary edges, exact iso-line chart curves).
[[nodiscard]] NetworkSheetOutcome ConstructNetworkSheet(ModelPartition& Partition, const std::vector<NurbsCurve>& Curves, const NetworkSettings& Settings,
                                                        const std::vector<std::string>& Names = {});

} // namespace Frontier::GeometrySpace::Parametric
