//============================================================================================================================================
//                                                    ORTHOGRAPHICZOOMPROOF.CPP
//============================================================================================================================================
// ⭐ A WHEEL NOTCH IN A PARALLEL VIEW MUST CHANGE THE APPARENT SIZE OF THE WORLD.
//
// 🔴 It did not, in any of the four orthographic views. The cause was not the projection: `OrthoScale`
//    was written in exactly ONE place in the entire tree — `DriveViewport`, a function with no call
//    sites — so the value sat at its default of 3.0 for the whole session and the wheel drove nothing.
//
// 📝 The claim is stated the way an artist would check it: the distance ON SCREEN between two fixed
//    world points, measured through the real projection, before and after a notch. That is what
//    "zoom does not work" means, and measuring anything else would let the defect back in.

#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{

std::uint32_t Claims = 0u;
std::uint32_t Failures = 0u;

void Require(bool Held, const char* Naming)
{
    ++Claims;
    if (Held)
        return;
    ++Failures;
    std::printf("  FAILED  %s\n", Naming);
}

/// 📝 The host's own arm, stated once so the proof measures the rule the editor applies rather than a
///    paraphrase of it. A notch away from the artist magnifies; a notch towards shrinks.
/// note  🔴 THE TWO DIRECTIONS ARE EXACT RECIPROCALS. The unreached arm used 1.1 and 0.9, which are not
///        inverses: 1.1 x 0.9 = 0.99, so every zoom-in-then-out lost a further 1% and an artist rocking
///        the wheel to inspect something drifted steadily smaller. Dividing by the same factor that
///        multiplies makes the round trip exact.
double Notched(double Standing, float Wheel)
{
    if (Wheel == 0.0f)
        return Standing;
    constexpr double Step = 1.1;
    return std::clamp(Wheel > 0.0f ? Standing * Step : Standing / Step, 0.05, 40.0);
}

/// 🧩 How far apart two fixed world points appear, in display pixels.
double ApparentSpan(const Slate::SpatialBasis& Basis, const Slate::ViewportStanding& View,
                    bool Perspective, const Slate::PlaneExtent& Leaf,
                    const Slate::SpatialPoint& First, const Slate::SpatialPoint& Second)
{
    float FirstX = 0.0f, FirstY = 0.0f, SecondX = 0.0f, SecondY = 0.0f;
    if (!Slate::ProjectSpatialPoint(Basis, View, Perspective, Leaf, First, FirstX, FirstY))
        return -1.0;
    if (!Slate::ProjectSpatialPoint(Basis, View, Perspective, Leaf, Second, SecondX, SecondY))
        return -1.0;
    const double AcrossX = static_cast<double>(SecondX - FirstX);
    const double AcrossY = static_cast<double>(SecondY - FirstY);
    return std::sqrt(AcrossX * AcrossX + AcrossY * AcrossY);
}

}   // namespace

int main()
{
    using namespace Slate;

    std::printf("[OrthographicZoomProof]\n");

    const SpatialBasis Ground = { {}, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
    const PlaneExtent  Leaf   = Spanning(0.0f, 0.0f, 1200.0f, 900.0f);
    const SpatialPoint First  = { 0.0, 0.0, 0.0 };
    // 🔴 A PROBE SPAN MUST NOT LIE ALONG THE VIEW DIRECTION. A span of (10, 0, 0) is invisible from the
    //    Left and Right views — it points straight at the eye and projects to zero length, which looked
    //    like a zoom failure and was actually a badly chosen probe. A span with a component on all three
    //    axes has a visible length from every orientation, so what is measured is the zoom.
    const SpatialPoint Second = { 10.0, 10.0, 10.0 };

    // ① 🔴 THE CLAIM ITSELF, IN EVERY ORIENTATION THERE IS. The artist reported it as one defect; had
    //    only some orientations failed, the cause would have been the orientation and not the scale.
    //    All seven are checked rather than the four an artist is most likely to name, because the enum
    //    has seven and a gate that tests a subset invites the eighth to be added untested.
    {
        const ViewportOrientation Parallel[7] = { ViewportOrientation::Top,
                                                  ViewportOrientation::Bottom,
                                                  ViewportOrientation::Front,
                                                  ViewportOrientation::Back,
                                                  ViewportOrientation::Left,
                                                  ViewportOrientation::Right,
                                                  ViewportOrientation::Isometric };
        const char* const Named[7] = { "Top", "Bottom", "Front", "Back", "Left", "Right", "Isometric" };
        static_assert(static_cast<std::uint32_t>(ViewportOrientation::Isometric) + 1u == 7u,
                      "every orientation must be covered; add the new one to this proof");

        for (std::uint32_t Which = 0u; Which < 7u; ++Which)
        {
            ViewportStanding View;
            View.Orientation = Parallel[Which];
            View.OrthoScale  = 3.0;

            const double Before = ApparentSpan(Ground, View, false, Leaf, First, Second);

            View.OrthoScale = Notched(View.OrthoScale, 1.0f);
            const double Away = ApparentSpan(Ground, View, false, Leaf, First, Second);

            View.OrthoScale = Notched(View.OrthoScale, -1.0f);
            const double Back = ApparentSpan(Ground, View, false, Leaf, First, Second);

            std::printf("  %-10s %7.2f px  ->  %7.2f px  ->  %7.2f px\n", Named[Which], Before, Away, Back);

            Require(Before > 0.0, "the two points project at all");
            Require(Away > Before + 0.5,
                    "a notch away from the artist makes a fixed span measurably larger");
            Require(std::fabs(Back - Before) < 1.0e-6,
                    "and a notch back returns it EXACTLY to where it started, without drift");
        }
    }

    // ② 🔴 THE BOUNDS HOLD, AND A VIEW ALREADY AT A BOUND DOES NOT PRETEND TO MOVE. Turning the wheel
    //    forever must stop somewhere; the artist finding out by having the sketch vanish is not a bound.
    {
        double Scale = 3.0;
        for (std::uint32_t Turn = 0u; Turn < 200u; ++Turn)
            Scale = Notched(Scale, 1.0f);
        Require(std::fabs(Scale - 40.0) < 1.0e-9, "zooming in without end holds at the upper bound");

        for (std::uint32_t Turn = 0u; Turn < 400u; ++Turn)
            Scale = Notched(Scale, -1.0f);
        Require(std::fabs(Scale - 0.05) < 1.0e-9, "and zooming out holds at the lower one");

        // ⚠️ At the bound the apparent size must be stable rather than creeping.
        ViewportStanding Held;
        Held.OrthoScale = 40.0;
        const double AtBound = ApparentSpan(Ground, Held, false, Leaf, First, Second);
        Held.OrthoScale = Notched(Held.OrthoScale, 1.0f);
        Require(std::fabs(ApparentSpan(Ground, Held, false, Leaf, First, Second) - AtBound) < 1.0e-9,
                "a notch at the bound changes nothing at all");
    }

    // ③ 🔴 ROCKING THE WHEEL MUST NOT DRIFT. An artist inspecting something scrolls in and out
    //    repeatedly; with 1.1 and 0.9 as the two directions each pair lost 1%, so twenty round trips
    //    left the view 18% smaller than it started and the artist blamed the zoom for wandering.
    {
        double Scale = 3.0;
        for (std::uint32_t Pair = 0u; Pair < 40u; ++Pair)
        {
            Scale = Notched(Scale, 1.0f);
            Scale = Notched(Scale, -1.0f);
        }
        Require(std::fabs(Scale - 3.0) < 1.0e-9,
                "forty zoom-in-then-out pairs leave the scale exactly where it began");
    }

    // ④ A NOTCH WITH NO WHEEL IS NOT A NOTCH. The arm is entered only when the wheel moved, so a frame
    //    with no scroll must leave the scale exactly as it found it.
    {
        Require(Notched(7.25, 0.0f) == 7.25, "a frame with no wheel travel leaves the scale untouched");
    }

    // ⑤ 🔴 PERSPECTIVE IS NOT TOUCHED. The parallel arm must not have taken zoom away from the arm that
    //    already worked — flying the camera is how a perspective view zooms.
    {
        ViewportStanding View;
        View.Distance = 240.0;
        const double Before = ApparentSpan(Ground, View, true, Leaf, First, Second);
        View.Distance *= 0.9;   // what the perspective arm does
        const double Closer = ApparentSpan(Ground, View, true, Leaf, First, Second);
        Require(Closer > Before + 0.5, "flying closer still magnifies a perspective view");

        // And the scale the parallel arm drives has no effect there, which is why the two are separate.
        ViewportStanding Other = View;
        Other.OrthoScale = 40.0;
        Require(std::fabs(ApparentSpan(Ground, Other, true, Leaf, First, Second)
                          - ApparentSpan(Ground, View, true, Leaf, First, Second)) < 1.0e-9,
                "and the parallel scale does not disturb a perspective view");
    }

    // ⑥ THE SCALE IS PIXELS PER WORLD UNIT, which is the fact `ResolvePickTolerance` inverts. If this
    //    ever stopped being true, selection reach would silently stop meaning pixels.
    {
        // 📝 Measured along one plane axis, so the world length is unambiguous: the Top view's Along
        //    direction is world X, and a span of exactly 10 units on it must measure 10 x scale pixels.
        ViewportStanding View;
        View.Orientation = ViewportOrientation::Top;
        View.OrthoScale  = 4.0;
        const double Span = ApparentSpan(Ground, View, false, Leaf,
                                         { 0.0, 0.0, 0.0 }, { 10.0, 0.0, 0.0 });
        Require(std::fabs(Span - 40.0) < 1.0e-6,
                "a 10-unit span at 4 px per unit measures 40 px, so the scale IS pixels per unit");

        // ⚠️ And doubling the scale doubles it, which is what makes the relationship linear rather
        //    than merely increasing — `ResolvePickTolerance` divides by this number.
        View.OrthoScale = 8.0;
        Require(std::fabs(ApparentSpan(Ground, View, false, Leaf,
                                       { 0.0, 0.0, 0.0 }, { 10.0, 0.0, 0.0 }) - 80.0) < 1.0e-6,
                "and doubling the scale exactly doubles the apparent span");
    }

    std::printf("[OrthographicZoomProof] %u claims, %u failures\n", Claims, Failures);
    return Failures == 0u ? 0 : 1;
}
