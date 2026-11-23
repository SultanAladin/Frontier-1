//============================================================================================================================================
//                                                       MENUPLACEMENTPROOF.CPP
//============================================================================================================================================
// 🧩 Proves that a context menu opens in a free corner and never on top of another widget.
//
// 🔴 THE REQUIREMENT. "Bevel's menu draws bottom-right/left/top, never on top of another widget." The
//    interesting half is the second clause: a menu that usually avoids the widget is not a fix, it is
//    the defect with better odds. So the claims below do not check that the chosen corner looks
//    sensible — they check that the returned box shares NO AREA with anything occupied, across every
//    arrangement the layout allows, including the ones where the answer must be "nowhere".
//
// 🔴 AND REFUSAL IS PART OF THE ANSWER. When every corner is blocked, `PlaceMenuClear` returns false
//    rather than the least-bad overlap. A search that always succeeds cannot be distinguished from one
//    that has quietly given up, so the proof asserts the refusal as hard as it asserts the placements.

#include "Foundation/ExtentBands.h"

#include <cstdio>
#include <cstdint>

using namespace Slate;

namespace
{

std::uint32_t Claims = 0u;
std::uint32_t Failures = 0u;

void Claim(bool Held, const char* Detail)
{
    ++Claims;
    if (!Held)
    {
        ++Failures;
        std::printf("  [FAIL] %s\n", Detail);
    }
}

constexpr float MenuWidth  = 180.0f;
constexpr float MenuHeight = 120.0f;
constexpr float MenuGap    = 6.0f;

/// The viewport leaf every case below is placed inside.
constexpr ExtentBand Leaf{ 0.0f, 0.0f, 800.0f, 600.0f };

const char* CornerName(const ExtentBand& Anchor, const ExtentBand& Placed)
{
    const bool Below    = Placed.MinimumY > Anchor.MinimumY;
    const bool Trailing = Placed.MinimumX >= Anchor.MinimumX;

    if (Below && Trailing)  return "below-trailing";
    if (Below && !Trailing) return "below-leading";
    if (!Below && Trailing) return "above-trailing";
    return "above-leading";
}

bool Inside(const ExtentBand& Outer, const ExtentBand& Inner)
{
    return Inner.MinimumX >= Outer.MinimumX && Inner.MaximumX <= Outer.MaximumX &&
           Inner.MinimumY >= Outer.MinimumY && Inner.MaximumY <= Outer.MaximumY;
}

/// Every claim that must hold of ANY successful placement, checked in one place so no case can
/// forget one of them.
void ClaimWellPlaced(const ExtentBand& Anchor, const ExtentBand& Placed,
                     const ExtentBand* Occupied, std::uint32_t Count)
{
    Claim(Inside(Leaf, Placed), "a placed menu must lie wholly inside the viewport leaf");

    bool Clear = true;
    for (std::uint32_t Index = 0u; Index < Count; ++Index)
        Clear = Clear && !ExtentsIntersect(Placed, Occupied[Index]);
    Claim(Clear, "a placed menu must not share any area with an occupied box");

    Claim(!ExtentsIntersect(Placed, Anchor),
          "a placed menu must not cover the tile that opened it");

    Claim(Placed.MaximumX - Placed.MinimumX == MenuWidth &&
          Placed.MaximumY - Placed.MinimumY == MenuHeight,
          "placing a menu must not resize it");
}

}   // namespace

int main()
{
    std::printf("\n[MenuPlacement] a context menu opens clear of every other widget\n\n");

    // ① NOTHING IN THE WAY. The menu takes the preferred corner: below and trailing, which is where the
    //    pointer already is after pressing a tile.
    {
        const ExtentBand Anchor{ 100.0f, 100.0f, 160.0f, 130.0f };
        ExtentBand Placed{};
        const bool Found = PlaceMenuClear(Leaf, Anchor, MenuWidth, MenuHeight, nullptr, 0u, MenuGap, Placed);

        std::printf("  ① an empty leaf\n    chose %s\n", Found ? CornerName(Anchor, Placed) : "nothing");
        Claim(Found, "a menu on an empty leaf must find a corner");
        ClaimWellPlaced(Anchor, Placed, nullptr, 0u);
        Claim(Placed.MinimumY >= Anchor.MaximumY && Placed.MinimumX >= Anchor.MinimumX,
              "with nothing in the way the preferred corner is below and trailing");
        Claim(Placed.MinimumY - Anchor.MaximumY == MenuGap, "the gap below the anchor is honoured");
    }

    // ② THE SELECT WIDGET IS IN THE PREFERRED CORNER. This is the case the plan names: the widget is
    //    already placed, and the menu must go somewhere else without being told where.
    {
        const ExtentBand Anchor{ 100.0f, 100.0f, 160.0f, 130.0f };
        const ExtentBand Widget{ 90.0f, 140.0f, 390.0f, 420.0f };   // squarely in below-trailing
        ExtentBand Placed{};
        const bool Found = PlaceMenuClear(Leaf, Anchor, MenuWidth, MenuHeight, &Widget, 1u, MenuGap, Placed);

        std::printf("  ② the Select widget occupies below-trailing\n    chose %s\n",
                    Found ? CornerName(Anchor, Placed) : "nothing");
        Claim(Found, "a blocked preferred corner must not stop the menu opening");
        ClaimWellPlaced(Anchor, Placed, &Widget, 1u);
        Claim(!ExtentsIntersect(Placed, Widget),
              "THE REQUIREMENT: the menu does not draw on top of the Select widget");
    }

    // ③ EVERY CORNER OF THE LEAF. A tile in a corner has half its corners off the edge, and the search
    //    must reject those before it even considers overlap — a menu half off the leaf is unusable
    //    whether or not it covers anything.
    {
        struct Placement { const char* Title; ExtentBand Anchor; };

        const Placement Placements[] = {
            { "upper-leading tile",  {  10.0f,  10.0f,  70.0f,  40.0f } },
            { "upper-trailing tile", { 730.0f,  10.0f, 790.0f,  40.0f } },
            { "lower-leading tile",  {  10.0f, 560.0f,  70.0f, 590.0f } },
            { "lower-trailing tile", { 730.0f, 560.0f, 790.0f, 590.0f } },
            { "middle tile",         { 380.0f, 290.0f, 440.0f, 320.0f } },
        };

        std::printf("  ③ a tile in each corner of the leaf\n");
        for (const Placement& Current : Placements)
        {
            ExtentBand Placed{};
            const bool Found = PlaceMenuClear(Leaf, Current.Anchor, MenuWidth, MenuHeight,
                                              nullptr, 0u, MenuGap, Placed);

            std::printf("    %-22s %s\n", Current.Title,
                        Found ? CornerName(Current.Anchor, Placed) : "nothing");

            Claim(Found, "a tile against an edge must still find a corner somewhere");
            ClaimWellPlaced(Current.Anchor, Placed, nullptr, 0u);
        }
    }

    // ④ THREE CORNERS BLOCKED. The search must find the one remaining corner rather than stopping at
    //    the first failure — an early return here would look identical on an empty leaf.
    {
        const ExtentBand Anchor{ 300.0f, 250.0f, 360.0f, 280.0f };
        // Each blocker covers exactly one candidate box and leaves above-leading clear. The trailing
        // blockers start past the leading candidate's trailing edge (360) so they cannot spill onto it.
        const ExtentBand Occupied[3] = {
            { 361.0f, 286.0f, 560.0f, 406.0f },    // below-trailing  (300..480, 286..406)
            { 180.0f, 286.0f, 360.0f, 406.0f },    // below-leading   (180..360, 286..406)
            { 361.0f, 124.0f, 560.0f, 244.0f },    // above-trailing  (300..480, 124..244)
        };
        ExtentBand Placed{};
        const bool Found = PlaceMenuClear(Leaf, Anchor, MenuWidth, MenuHeight, Occupied, 3u, MenuGap, Placed);

        std::printf("  ④ only one corner left free\n    chose %s\n",
                    Found ? CornerName(Anchor, Placed) : "nothing");
        Claim(Found, "the search must try every corner, not stop at the first blocked one");
        ClaimWellPlaced(Anchor, Placed, Occupied, 3u);
        Claim(Placed.MaximumY <= Anchor.MinimumY && Placed.MaximumX <= Anchor.MaximumX,
              "the one free corner is above-leading, and that is what must be chosen");
    }

    // ⑤ NOWHERE TO GO. Every corner blocked must REFUSE. This is the claim that separates a real search
    //    from one that returns its least-bad guess and lets the caller believe it succeeded.
    {
        const ExtentBand Anchor{ 380.0f, 290.0f, 440.0f, 320.0f };
        const ExtentBand Swamped{ 0.0f, 0.0f, 800.0f, 600.0f };   // the whole leaf is spoken for
        ExtentBand Placed{ -1.0f, -1.0f, -1.0f, -1.0f };
        const bool Found = PlaceMenuClear(Leaf, Anchor, MenuWidth, MenuHeight, &Swamped, 1u, MenuGap, Placed);

        std::printf("  ⑤ every corner blocked\n    %s\n", Found ? "PLACED ANYWAY" : "refused, correctly");
        Claim(!Found, "with every corner blocked the placement must refuse, not overlap");
        Claim(Placed.MinimumX == -1.0f && Placed.MinimumY == -1.0f,
              "a refusal must leave the caller's box untouched rather than half-written");
    }

    // ⑤·b NO CORNER OF THE ANCHOR IS USABLE, BUT THE LEAF HAS ROOM. A tile near the top edge with a
    //    widget filling the space below it blocks both lower corners, while both upper ones slide down
    //    onto the tile. The menu detaches and takes a free corner of the leaf rather than refusing --
    //    it stops being attached to its tile, but it opens, and it still covers nothing.
    {
        const ExtentBand Anchor{ 100.0f, 100.0f, 160.0f, 130.0f };
        const ExtentBand Widget{ 90.0f, 140.0f, 390.0f, 420.0f };
        ExtentBand Placed{};
        const bool Found = PlaceMenuClear(Leaf, Anchor, MenuWidth, MenuHeight, &Widget, 1u, MenuGap, Placed);

        std::printf("  ⑤·b every corner of the anchor spent\n    %s\n",
                    Found ? "detached to a leaf corner" : "refused");
        Claim(Found, "a menu with no usable anchor corner must still open somewhere");
        ClaimWellPlaced(Anchor, Placed, &Widget, 1u);
        Claim(Placed.MinimumX == Leaf.MinimumX || Placed.MaximumX == Leaf.MaximumX ||
              Placed.MinimumY == Leaf.MinimumY || Placed.MaximumY == Leaf.MaximumY,
              "the detached placement sits against an edge of the leaf");
    }

    // ⑥ A MENU LARGER THAN THE LEAF. Nothing fits anywhere, and the refusal must come from the bounds
    //    test rather than from an overlap that happens not to exist.
    {
        const ExtentBand Anchor{ 380.0f, 290.0f, 440.0f, 320.0f };
        ExtentBand Placed{};
        const bool Found = PlaceMenuClear(Leaf, Anchor, 900.0f, 700.0f, nullptr, 0u, MenuGap, Placed);

        std::printf("  ⑥ a menu larger than the leaf\n    %s\n", Found ? "PLACED ANYWAY" : "refused, correctly");
        Claim(!Found, "a menu too large for the leaf must refuse rather than hang off it");
    }

    // ⑦ FLUSH IS NOT OVERLAPPING. A menu whose edge exactly meets a widget's edge shares no area, and
    //    the search must be allowed to find that placement. Getting this wrong makes the search reject
    //    the tightest legal answer and drift to a worse corner for no visible reason.
    {
        const ExtentBand Anchor{ 100.0f, 100.0f, 160.0f, 130.0f };
        // Its leading edge sits exactly where the below-trailing menu's trailing edge lands.
        const ExtentBand Abutting{ 100.0f + MenuWidth, 136.0f, 500.0f, 400.0f };
        ExtentBand Placed{};
        const bool Found = PlaceMenuClear(Leaf, Anchor, MenuWidth, MenuHeight, &Abutting, 1u, MenuGap, Placed);

        std::printf("  ⑦ a widget flush against the preferred corner\n    chose %s\n",
                    Found ? CornerName(Anchor, Placed) : "nothing");
        Claim(Found, "a flush neighbour must not block a placement");
        Claim(Placed.MaximumX == Abutting.MinimumX,
              "touching edges do not overlap: the tightest legal placement must still be chosen");
        ClaimWellPlaced(Anchor, Placed, &Abutting, 1u);
    }

    // ⑧ TWO MENUS OPEN AT ONCE. The second must treat the first as occupied, which is the whole reason
    //    the occupied list is a list and not a single widget.
    {
        const ExtentBand FirstAnchor{ 100.0f, 100.0f, 160.0f, 130.0f };
        ExtentBand First{};
        Claim(PlaceMenuClear(Leaf, FirstAnchor, MenuWidth, MenuHeight, nullptr, 0u, MenuGap, First),
              "the first menu opens");

        const ExtentBand SecondAnchor{ 500.0f, 100.0f, 560.0f, 130.0f };
        ExtentBand Second{};
        const bool Found = PlaceMenuClear(Leaf, SecondAnchor, MenuWidth, MenuHeight, &First, 1u, MenuGap, Second);

        std::printf("  ⑧ a second menu while the first stands\n    chose %s\n",
                    Found ? CornerName(SecondAnchor, Second) : "nothing");
        Claim(Found, "a second menu must find its own corner");
        Claim(!ExtentsIntersect(First, Second), "two open menus must not overlap each other");
        ClaimWellPlaced(SecondAnchor, Second, &First, 1u);
    }

    // ⑨ THE INTERSECTION TEST ITSELF. Everything above rests on it, so it is measured directly rather
    //    than trusted: containment, partial overlap, shared edge, shared corner and disjoint.
    {
        const ExtentBand Base{ 100.0f, 100.0f, 200.0f, 200.0f };
        std::printf("  ⑨ the intersection test\n");

        Claim(ExtentsIntersect(Base, ExtentBand{ 150.0f, 150.0f, 250.0f, 250.0f }),
              "partial overlap intersects");
        Claim(ExtentsIntersect(Base, ExtentBand{ 120.0f, 120.0f, 180.0f, 180.0f }),
              "a box wholly inside intersects");
        Claim(ExtentsIntersect(Base, ExtentBand{ 50.0f, 50.0f, 250.0f, 250.0f }),
              "a box wholly containing it intersects");
        Claim(!ExtentsIntersect(Base, ExtentBand{ 200.0f, 100.0f, 300.0f, 200.0f }),
              "a shared vertical edge does not intersect");
        Claim(!ExtentsIntersect(Base, ExtentBand{ 100.0f, 200.0f, 200.0f, 300.0f }),
              "a shared horizontal edge does not intersect");
        Claim(!ExtentsIntersect(Base, ExtentBand{ 200.0f, 200.0f, 300.0f, 300.0f }),
              "a shared corner does not intersect");
        Claim(!ExtentsIntersect(Base, ExtentBand{ 300.0f, 300.0f, 400.0f, 400.0f }),
              "disjoint boxes do not intersect");
        std::printf("    containment, overlap, edges, corner and disjoint all agree\n");
    }

    // ⑩ IT FOLDS AT COMPILE TIME. The whole search is constexpr, so a fixed layout can be resolved by
    //    the compiler — and the compiler checks there is no path that reads uninitialised storage.
    {
        constexpr bool Folded = []
        {
            constexpr ExtentBand Bounds{ 0.0f, 0.0f, 800.0f, 600.0f };
            constexpr ExtentBand Anchor{ 100.0f, 100.0f, 160.0f, 130.0f };
            ExtentBand Placed{};
            return PlaceMenuClear(Bounds, Anchor, 180.0f, 120.0f, nullptr, 0u, 6.0f, Placed);
        }();
        static_assert(Folded, "the placement search must be usable in a constant expression");
        std::printf("  ⑩ the search folds at compile time\n");
        Claim(Folded, "the placement search is a constant expression");
    }

    // ⑪ THE READOUT CORNER. `PlaceReadoutCorner` is a different contract from `PlaceMenuClear` and is
    //    proven as one: it is PINNED bottom-right, it steps UP rather than hopping corners, and it never
    //    refuses -- because a figure the artist is reading mid-drag must not move or disappear.
    {
        std::printf("  \u246a the readout pins bottom-right, rises past widgets, and never refuses\n");
        constexpr ExtentBand Leaf{ 0.0f, 0.0f, 800.0f, 600.0f };
        constexpr float Gap = 12.0f;
        constexpr float Width = 196.0f;
        constexpr float Height = 120.0f;

        // ① Unobstructed: hard against the bottom-right, one gap clear of both edges.
        {
            ExtentBand Placed{};
            PlaceReadoutCorner(Leaf, Width, Height, nullptr, 0u, Gap, Placed);
            Claim(Placed.MaximumX == 800.0f - Gap, "the readout's right edge is one gap from the leaf's");
            Claim(Placed.MaximumY == 600.0f - Gap, "and its bottom edge likewise");
            Claim(Placed.MaximumX - Placed.MinimumX == Width, "at the width asked for");
            Claim(Placed.MaximumY - Placed.MinimumY == Height, "and the height");
        }

        // ② A widget in that corner pushes it UP, and it keeps the same right edge.
        {
            const ExtentBand Widget{ 560.0f, 420.0f, 800.0f, 600.0f };
            ExtentBand Placed{};
            PlaceReadoutCorner(Leaf, Width, Height, &Widget, 1u, Gap, Placed);
            Claim(!ExtentsIntersect(Placed, Widget), "an obstructed readout does not overlap the widget");
            Claim(Placed.MaximumX == 800.0f - Gap,
                  "and it holds the right edge rather than hopping to another corner");
            Claim(Placed.MaximumY == 420.0f - Gap, "having risen to sit just above the widget");
        }

        // ③ TWO STACKED WIDGETS. One pass is not enough -- rising past the lower one lands it on the
        //    upper -- which is why the placement iterates. A single-pass implementation fails here.
        {
            // 🔴 THE ORDER IS THE POINT. The higher widget is listed FIRST, so one sweep of the list
            //    rises past it and then past the lower one -- landing at 420-12-120 = 288, which is
            //    inside the higher widget it had already cleared. Only a second pass catches that, and
            //    a single-pass implementation passes a fixture whose widgets happen to be listed
            //    bottom-up. This ordering is what makes the claim mean anything.
            const ExtentBand Widgets[2] = { { 560.0f, 250.0f, 800.0f, 400.0f },
                                            { 560.0f, 420.0f, 800.0f, 600.0f } };
            ExtentBand Placed{};
            PlaceReadoutCorner(Leaf, Width, Height, Widgets, 2u, Gap, Placed);
            Claim(!ExtentsIntersect(Placed, Widgets[0]) && !ExtentsIntersect(Placed, Widgets[1]),
                  "it clears BOTH of two stacked widgets, not merely the first it met");
            Claim(Placed.MaximumY == 250.0f - Gap, "coming to rest above the higher one");
        }

        // ④ IT NEVER REFUSES. A widget covering the whole leaf leaves nowhere clear, and the readout is
        //    clamped inside the bounds rather than vanishing. This is the deliberate difference from
        //    `PlaceMenuClear`, which returns false in exactly this case.
        {
            const ExtentBand Everything{ 0.0f, 0.0f, 800.0f, 600.0f };
            ExtentBand Placed{};
            PlaceReadoutCorner(Leaf, Width, Height, &Everything, 1u, Gap, Placed);
            Claim(Placed.MinimumY == Gap, "with nowhere clear it clamps to the top of the leaf");
            Claim(Placed.MinimumY >= Leaf.MinimumY && Placed.MaximumX <= Leaf.MaximumX,
                  "and stays inside the leaf rather than disappearing off it");
        }

        // ⑤ And it folds at compile time, like its neighbour.
        constexpr ExtentBand Folded = []
        {
            ExtentBand Placed{};
            PlaceReadoutCorner(ExtentBand{ 0.0f, 0.0f, 800.0f, 600.0f }, 196.0f, 120.0f,
                               nullptr, 0u, 12.0f, Placed);
            return Placed;
        }();
        static_assert(Folded.MaximumX == 788.0f, "the readout placement must fold in a constant expression");
        Claim(Folded.MaximumX == 788.0f, "the readout placement is a constant expression");
    }

    std::printf("\n[MenuPlacement] %u claims, %u failures\n\n", Claims, Failures);
    return Failures == 0u ? 0 : 1;
}
