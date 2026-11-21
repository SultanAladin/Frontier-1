/*====================================================================================================================================
                                                            SKETCHFLOW.H
====================================================================================================================================*/
// 🧩 Flow along curve (Rhino Flow, Blender curve deform): a 2D shape bent so its long axis runs along a rail curve; Wrap: a
//    2D shape laid round the surface a rail sweeps along its normal (a label round a can when the rail is a circle)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/VectorAlgebra.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchProfile.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSolver.h"

#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENUMS
//------------------------------------------------------------------------------------------------------------------------

enum class SketchWrapCategory : uint8_t
{
    Wrap    = 0,                                                                             // Lengths kept: across = arc length round the rail (a label on a can)
    Project = 1,                                                                             // Straight along the direction: unchanged seen from it
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct SketchRailDeformSettings
{
    int  AlongAxis        = -1;                                                              // [-] - Shape axis laid on the rail: −1 the longer side · 0 x · 1 y
    bool StretchEnabled   = true;                                                            // [-] - The shape's length fitted to the rail's (else kept, from the rail start)
    bool ReverseCondition = false;                                                           // [-] - Run from the rail's other end
};

struct SketchRailDeformOutcome
{
    SketchModel Sketch      = {};                                                            // [-]   - The bent shape, in the rail's coordinates
    int         AlongAxis   = 0;                                                             // [-]   - Axis used (0 x · 1 y)
    double      ShapeLength = 0.0;                                                           // [len] - Shape extent along that axis
    double      RailLength  = 0.0;                                                           // [len] - Rail arc length
    int         Lines       = 0;                                                             // [-]   - Curves kept straight (across the axis)
    int         Splines     = 0;                                                             // [-]   - Curves bent into cubic splines
    std::string Refusal     = {};                                                            // [-]   - Why nothing flowed (empty: flowed)
};

struct SketchWrapSettings
{
    SketchWrapCategory Kind      = SketchWrapCategory::Wrap;                                         // [-] - Wrap (lengths kept) or project
    Vector3        Direction = { 0.0, 1.0, 0.0 };                                            // [-] - Projection direction, world (the view, or the chosen axis)
    Vector3        Axis      = {};                                                           // [-] - Wrap axis (zero: the rail plane normal; else the rail's lone circle / arc turned onto it)
};

struct SketchWrapShape
{
    SketchModel      Sketch = {};                                                            // [-] - Solved shape
    OrthonormalBasis Plane  = {};                                                            // [-] - Its (elevated) plane
};

struct SketchWrapOutcome
{
    std::vector<std::vector<Vector3>> Curves     = {};                                       // [len] - Wrapped samples per shape curve (world; a closed curve does not repeat its first)
    std::vector<bool>                 Closed     = {};                                       // [-]   - The curve closes
    std::vector<std::string>          Names      = {};                                       // [-]   - Source curve names
    std::vector<int>                  Owners     = {};                                       // [-]   - Source shape index per curve
    Vector3                           Axis       = {};                                       // [-]   - Wrap axis used
    Vector3                           Direction  = {};                                       // [-]   - Projection direction used (⊥ Axis)
    double                            RailLength = 0.0;                                      // [len] - Rail arc length
    double                            Radius     = 0.0;                                      // [len] - A lone circle / arc rail: its radius (else 0)
    double                            Extent     = 0.0;                                      // [len] - Shape width across the direction
    std::string                       Refusal    = {};                                       // [-]   - Why nothing wrapped (empty: wrapped)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Flow: the shape's box gives the axis (a, b) — a along AlongAxis from the box start, b across from the box middle.
//    The rail (the non-construction curves of Rail chained end to end, one open or closed chain) is measured by arc length
//    s (Gauss–Legendre); a shape point goes to C(s) + b·N(s), s = a·(rail / shape length when stretched), N the rail's left
//    normal, past an open rail's ends along its end tangents. A shape line across the axis stays an exact line; every other
//    curve becomes a cubic spline through dense mapped samples (chord-length parameters); curves sharing an end still share
//    it. Construction curves of the shape are left out. Refused: no shape curves, a shape with no extent along the axis,
//    a rail that is not one chain or has no length.
[[nodiscard]] SketchRailDeformOutcome FlowSketchModel(const SketchModel& Shape, const SketchModel& Rail, const SketchRailDeformSettings& Settings);

// 💡 Wrap: the rail (one chain, in RailPlane) swept along the axis n (its plane normal; or, for a lone circle / arc rail, any
//    Axis — the circle's centre and radius turned onto it) is the surface. Seen along the projection direction d (⊥ n),
//    a shape point P has across x = (P − O)·(d × n) and height h = (P − O)·n. The front hit s₀ of the shape's centre line
//    (the rail point the line along d meets first) anchors it. Wrap: P ↦ C(s₀ ± (x − x꜀)) + h·n — across lengths become arc
//    lengths, so a label keeps its size (open rails continue straight past their ends, loops wrap round); Project:
//    P ↦ C(s) + h·n with s the front hit of P's own line (unchanged seen along d). Hits are bracketed on a 2048-sample
//    table and bisected to machine precision. All shapes share one centre (letters stay apart). Each non-construction
//    shape curve gives dense world samples (fit them as 3D curves). Refused: no shape curves, a rail that is not one chain, d along n, the centre line missing the rail,
//    (Project) a point outside the rail as seen along d, an Axis off the rail normal for a rail that is not a lone arc.
[[nodiscard]] SketchWrapOutcome WrapSketchModels(const std::vector<SketchWrapShape>& Shapes, const SketchModel& Rail, const OrthonormalBasis& RailPlane,
                                                 const SketchWrapSettings& Settings);

} // namespace Frontier::GeometrySpace::Parametric
