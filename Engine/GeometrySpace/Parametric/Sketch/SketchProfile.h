/*====================================================================================================================================
                                                        SKETCHPROFILE.H
====================================================================================================================================*/
// 🧩 Closed profile loops from a solved sketch: chaining, nesting, orientation (outer CCW, holes CW), exact area

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Sketch/SketchSolver.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSpline.h"

#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

enum class ProfileSegmentCategory : uint8_t
{
    Line   = 0,                                                                              // Start → End
    Arc    = 1,                                                                              // Centre, StartAngle + Sweep
    Circle = 2,                                                                              // Full turn from angle 0
    Spline = 3,                                                                              // Spline, traversed over its knot domain
};

// 📝 One oriented piece of a loop, in traversal order. Arcs: P(φ) = Centre + r·(cos φ, sin φ), φ from StartAngle by
//    Sweep (Sweep > 0: counter-clockwise traversal). Splines: the fraction maps linearly onto the knot domain and the
//    curve is stored already in traversal order (reversal mirrors its knots).
struct ProfileSegment
{
    ProfileSegmentCategory Kind       = ProfileSegmentCategory::Line;                                // [-]   - Piece type
    Vector2            Start      = {};                                                      // [len] - Traversal start
    Vector2            End        = {};                                                      // [len] - Traversal end
    Vector2            Centre     = {};                                                      // [len] - Arc / circle centre
    double             Radius     = 0.0;                                                     // [len] - Arc / circle radius
    double             StartAngle = 0.0;                                                     // [rad] - φ at Start
    double             Sweep      = 0.0;                                                     // [rad] - Signed angular extent
    int                Curve      = -1;                                                      // [-]   - Sketch curve index
    std::string        Name       = {};                                                      // [-]   - Sketch curve name (Line7)
    SketchSplineCurve  Spline     = {};                                                      // [-]   - Spline carrier (Kind = Spline)
};

struct ProfileLoop
{
    std::vector<ProfileSegment> Segments   = {};                                             // [-]     - Traversal order
    double                      SignedArea = 0.0;                                            // [len²]  - > 0 outer, < 0 hole (after orientation)
    int                         Depth      = 0;                                              // [-]     - Loops enclosing it (even: outer)
    int                         Enclosure  = -1;                                             // [-]     - Directly enclosing loop (holes)
};

struct SketchProfile
{
    std::vector<ProfileLoop> Loops    = {};                                                  // [-]    - Outer loops and holes
    double                   Area     = 0.0;                                                 // [len²] - Net enclosed area
    Vector2                  Centroid = {};                                                  // [len]  - Area centroid
    double                   Scale    = 1.0;                                                 // [len]  - Characteristic length
    std::string              Refusal  = {};                                                  // [-]    - Why no profile (empty: valid)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Chains the non-construction curves of a solved sketch into closed loops: endpoints within 1e-9·L join, every
//    junction must join exactly two curves (else refused: open or branching), loops must not cross (polyline test on
//    arcs and splines, a spline also against itself) and are oriented by nesting depth. Area and centroid are exact
//    (Green's theorem; Gauss–Legendre per arc piece and per spline knot span, exact for the polynomial integrands).
[[nodiscard]] SketchProfile ExtractSketchProfile(const SketchModel& Sketch);
// One sketch curve as a segment in its natural direction (circles: a full turn from angle 0); Refusal set when degenerate.
[[nodiscard]] ProfileSegment ResolveSketchCurveSegment(const SketchModel& Sketch, int CurveIndex, double Join, std::string& Refusal);
// 💡 Steps ④–⑤ of ExtractSketchProfile on loops already chained (Profile.Loops, Profile.Scale): refuses crossings, then
//    nests, orients (outer CCW, holes CW) and integrates area and centroid exactly. Profile.Refusal is set on a crossing.
void ArrangeProfileLoops(SketchProfile& Profile);

[[nodiscard]] Vector2 EvaluateProfileSegment(const ProfileSegment& Segment, double Fraction) noexcept;

// d/dFraction of EvaluateProfileSegment.
[[nodiscard]] Vector2 EvaluateProfileTangent(const ProfileSegment& Segment, double Fraction) noexcept;

// Polynomial pieces of a segment as fraction breaks [0 … 1] (one piece for lines; knot spans for splines; ≤ 45° for arcs).
[[nodiscard]] std::vector<double> CollectProfileBreaks(const ProfileSegment& Segment);

// Segment reversed in place (start ↔ end, sweep negated).
void ReverseProfileSegment(ProfileSegment& Segment) noexcept;

} // namespace Frontier::GeometrySpace::Parametric
