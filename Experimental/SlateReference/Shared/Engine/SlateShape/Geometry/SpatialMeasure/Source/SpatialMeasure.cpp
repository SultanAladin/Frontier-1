//============================================================================================================================================
//                                                         SPATIALMEASURE.CPP
//============================================================================================================================================
// 📝 The spatial vocabulary is entirely `constexpr` and `inline`, so it carries no out-of-line definitions.
//    This translation unit exists so the unit is built and its header is compiled on its own, away from
//    every other include — which is what proves the header stands up without a curve, a profile or a
//    solid in scope. A header that is only ever reached through another one can acquire a hidden
//    dependency on it and nobody finds out until the day something includes it first.

#include "SlateShape/Geometry/SpatialMeasure/Api/SpatialMeasure.h"

namespace Slate
{

// 🔴 Checked where the arithmetic is declared rather than in a proof, so a change that breaks the algebra
//    cannot compile at all. These are the identities the sketch tools rely on every frame: a cross product
//    is perpendicular to both of its arguments, `Difference` runs FROM its left argument TO its right, and
//    a right-handed quarter turn about the up axis carries forward onto left.
namespace
{
    constexpr SpatialDirection LeftAxis    = { 1.0, 0.0, 0.0 };
    constexpr SpatialDirection UpAxis      = { 0.0, 1.0, 0.0 };
    constexpr SpatialDirection ForwardAxis = { 0.0, 0.0, 1.0 };

    static_assert(LengthSquared(ForwardAxis) == 1.0, "a unit axis measures one");
    static_assert(Dot(LeftAxis, UpAxis) == 0.0, "the axes stand square to one another");
    static_assert(Cross(LeftAxis, UpAxis).Forward == 1.0, "left crossed onto up gives forward");
    static_assert(Dot(Cross(LeftAxis, ForwardAxis), LeftAxis) == 0.0,
                  "a cross product leaves both of its arguments");
    static_assert(Difference(SpatialPoint{ 1.0, 0.0, 0.0 }, SpatialPoint{ 4.0, 0.0, 0.0 }).Left == 3.0,
                  "Difference runs from the left point to the right one");
    static_assert(Added(SpatialPoint{ 1.0, 2.0, 3.0 }, SpatialDirection{ 1.0, 1.0, 1.0 }).Up == 3.0,
                  "a point offset by a direction is a point");
    static_assert(Negated(Scaled(UpAxis, 2.0)).Up == -2.0, "scaling and negation compose");
}

} // namespace Slate
