//============================================================================================================================================
//                                                            EXTENTBANDS.H
//============================================================================================================================================
// 🧩 Splitting a rectangle around a rectangle: the arithmetic that lets a one-rectangle scissor exclude a box.
//
// 🔴 WHY THIS IS IN FOUNDATION AND NOT BESIDE `PlaneExtent`. The two callers are the GPU passes in
//    `SlateVulkan`, and `SlateVulkan` requires `SlateMath` alone — deliberately, so nothing device-side
//    can name an interface type. `PlaneExtent` lives in `SlateUI`, so stating the boxes in its terms
//    would have quietly inverted that dependency. The arithmetic is pure numbers and belongs to no
//    layer, so it sits here and both sides convert at the edge.

#pragma once

#include <cstdint>

namespace Slate
{

/// 🧩 One axis-aligned box in the caller's own units, as its two corners.
/// note  Deliberately anonymous about which space it is in. The GPU passes hold physical pixels and the
///       interface holds logical ones; this arithmetic is correct in either and mixes neither.
struct ExtentBand
{
    float MinimumX = 0.0f;
    float MinimumY = 0.0f;
    float MaximumX = 0.0f;
    float MaximumY = 0.0f;

    constexpr bool Occupied() const { return MaximumX > MinimumX && MaximumY > MinimumY; }
};

/// 🧩 The at-most-four disjoint bands of a clip box that a withheld box does not cover.
/// in    ClipX0      [-] the box to record, leading edge
/// in    ClipY0      [-] upper edge
/// in    ClipX1      [-] trailing edge
/// in    ClipY1      [-] lower edge
/// in    WithheldX0  [-] the box to keep clear; a zero-area box yields the clip alone
/// in    WithheldY0  [-] upper edge of the withheld box
/// in    WithheldX1  [-] trailing edge of the withheld box
/// in    WithheldY1  [-] lower edge of the withheld box
/// out   Bands                   [-] filled from index zero
/// out   Result                  [-] how many bands were written: 0, 1, 2, 3 or 4
/// note  🔴 A SCISSOR IS ONE RECTANGLE. "Everything except this box" therefore has to be recorded as
///        several, and the GPU passes that record AFTER the interface — the grid, the axes and the
///        sketch — need exactly that to stop texturing over an open menu. Drawn over, an opaque menu
///        reads as a transparent one, because what shows through it is the viewport behind.
/// note  ⚠️ The bands are DISJOINT by construction: the upper and lower ones span the full width, and
///        the leading and trailing ones are cut to the withheld box's own rows. Overlapping bands would
///        shade a fragment twice, which a straight-alpha blend shows as a seam.
/// note  ⚠️ Both boxes must be in the same space. Mixing logical and physical ordinates clips the wrong
///        area on any display whose scale is not one, and does so silently.
/// cost  ✔️
/// tag   guarantee, nonallocating, nonthrowing
constexpr std::uint32_t ExtentBandsAround(float ClipX0, float ClipY0, float ClipX1, float ClipY1,
                                          float WithheldX0, float WithheldY0,
                                          float WithheldX1, float WithheldY1,
                                          ExtentBand (&Bands)[4])
{
    std::uint32_t Written = 0u;

    // 🔴 THE ONLY GUARD, DELIBERATELY. Every degenerate case reduces to a band with no area: an
    //    inverted clip, a zero-width clip, a withheld box flush against an edge, and a withheld box
    //    covering the clip entirely. Testing the clip separately at the top read as defensive and was
    //    measurably redundant -- removing it changed no claim in the proof -- so there is one gate here
    //    rather than two that could disagree.
    const auto Emit = [&](float X0, float Y0, float X1, float Y1)
    {
        if (X1 > X0 && Y1 > Y0)
            Bands[Written++] = ExtentBand{ X0, Y0, X1, Y1 };
    };

    const float HoldX0 = ClipX0 > WithheldX0 ? ClipX0 : WithheldX0;
    const float HoldY0 = ClipY0 > WithheldY0 ? ClipY0 : WithheldY0;
    const float HoldX1 = ClipX1 < WithheldX1 ? ClipX1 : WithheldX1;
    const float HoldY1 = ClipY1 < WithheldY1 ? ClipY1 : WithheldY1;

    // Nothing withheld, or it misses this box entirely.
    if (HoldX1 <= HoldX0 || HoldY1 <= HoldY0)
    {
        Emit(ClipX0, ClipY0, ClipX1, ClipY1);
        return Written;
    }

    Emit(ClipX0, ClipY0, ClipX1, HoldY0);    // above
    Emit(ClipX0, HoldY1, ClipX1, ClipY1);    // below
    Emit(ClipX0, HoldY0, HoldX0, HoldY1);    // leading
    Emit(HoldX1, HoldY0, ClipX1, HoldY1);    // trailing

    return Written;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                   PLACING A BOX CLEAR OF OTHERS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Where a menu is placed relative to the tile that opened it.
/// note  🔴 THE ORDER IS THE PREFERENCE ORDER and the enumerators are tried in it. Below-trailing is
///        first because it is where a pointer already is after pressing a tile, and because a menu that
///        drops down and to the right never covers the tile the artist is still looking at.
enum class MenuCorner : std::uint32_t
{
    BelowTrailing = 0u,   // [-] - down and to the right of the anchor; the default
    BelowLeading  = 1u,   // [-] - down and to the left
    AboveTrailing = 2u,   // [-] - up and to the right
    AboveLeading  = 3u,   // [-] - up and to the left
    CornerCount   = 4u    // [-] - the closed count, never a corner
};

/// 🧩 Whether two boxes share any area. Touching edges do not count as sharing.
/// note  ⚠️ Half-open on both axes, matching `Encloses`. Two boxes that merely abut are NOT intersecting,
///        so a menu may sit flush against a widget's edge -- which is the tightest legal placement and the
///        one a corner search should be allowed to find.
/// cost  ✔️
/// tag   guarantee, nonallocating, nonthrowing
constexpr bool ExtentsIntersect(const ExtentBand& Left, const ExtentBand& Right)
{
    return Left.MinimumX < Right.MaximumX && Right.MinimumX < Left.MaximumX &&
           Left.MinimumY < Right.MaximumY && Right.MinimumY < Left.MaximumY;
}

/// 🧩 The box a menu of the given span would occupy in one corner of an anchor.
/// in    AnchorX0  [-] the tile the menu belongs to, leading edge
/// in    AnchorY0  [-] upper edge
/// in    AnchorX1  [-] trailing edge
/// in    AnchorY1  [-] lower edge
/// in    Width     [-] the menu's own span
/// in    Height    [-] the menu's own height
/// in    Corner    [-] which side of the anchor to hang it from
/// in    Gap       [-] the clear distance between the anchor and the menu
/// cost  ✔️
/// tag   guarantee, nonallocating, nonthrowing
constexpr ExtentBand MenuAtCorner(float AnchorX0, float AnchorY0, float AnchorX1, float AnchorY1,
                                  float Width, float Height, MenuCorner Corner, float Gap)
{
    const bool Below   = Corner == MenuCorner::BelowTrailing || Corner == MenuCorner::BelowLeading;
    const bool Trailing = Corner == MenuCorner::BelowTrailing || Corner == MenuCorner::AboveTrailing;

    // 📐 The menu hangs from an EDGE of the anchor and is aligned to the opposite one, so a trailing
    //    menu starts at the anchor's leading edge and grows right. That keeps a menu visually attached to
    //    its tile instead of floating off one corner of it.
    const float X0 = Trailing ? AnchorX0 : AnchorX1 - Width;
    const float Y0 = Below ? AnchorY1 + Gap : AnchorY0 - Gap - Height;

    return ExtentBand{ X0, Y0, X0 + Width, Y0 + Height };
}

/// 🧩 Places a menu in the first corner of its anchor where it neither leaves the bounds nor touches
///    any occupied box, sliding it along the bounds when a corner hangs off the edge.
/// in    Bounds        [-] the extent the menu must stay inside -- the viewport leaf, not the window
/// in    Anchor        [-] the tile the menu was opened from
/// in    Width         [-] the menu's own span
/// in    Height        [-] the menu's own height
/// in    Occupied      [-] boxes already spoken for: the widget, the footer, other menus
/// in    OccupiedCount [-] how many of them
/// in    Gap           [-] the clear distance between the anchor and the menu
/// out   Placed        [-] the chosen box; untouched when the result is false
/// out   Result        [-] false when no corner fits, so the caller can decide rather than be given a lie
/// note  🔴 A MENU MUST NEVER DRAW ON TOP OF ANOTHER WIDGET. The corners are tried in preference order
///        and the FIRST clear one wins.
/// note  🔴 A CORNER THAT HANGS OFF THE EDGE IS SLID BACK ON, NOT DISCARDED. Four rigid corners is too
///        few: a tile near the top-left of the leaf has three of them off the edge, so a single blocked
///        corner meant refusing outright. Sliding along the offending axis keeps the menu attached to the
///        side of the anchor it was asked for while making the placement reachable, and turns four
///        candidates into four that can actually be used. The SIDE is the intent; the exact ordinate is not.
/// note  ⚠️ Sliding cannot rescue a menu larger than the bounds, which still refuses -- the clamp would
///        otherwise invert the box and hand back a negative span.
/// note  🔴 REFUSAL IS A REAL ANSWER. When every corner is blocked this returns false rather than
///        quietly returning the least-bad overlap -- a placement that "mostly" avoids a widget is the
///        defect this exists to prevent, wearing a hat. The caller falls back deliberately and visibly.
/// note  ⚠️ The anchor itself is not treated as occupied. A menu is allowed to sit flush against the tile
///        that opened it; that is the point of the gap.
/// note  When no corner of the anchor is usable the menu detaches and takes a free corner of the bounds.
/// note  📝 At most eight candidates tested against a short list.
/// cost  ✔️
/// tag   guarantee, nonallocating, nonthrowing
constexpr bool PlaceMenuClear(const ExtentBand& Bounds, const ExtentBand& Anchor,
                              float Width, float Height,
                              const ExtentBand* Occupied, std::uint32_t OccupiedCount,
                              float Gap, ExtentBand& Placed)
{
    // A menu wider or taller than the bounds can never be placed, and sliding it would invert the box.
    if (Width > Bounds.MaximumX - Bounds.MinimumX || Height > Bounds.MaximumY - Bounds.MinimumY)
        return false;

    for (std::uint32_t Index = 0u; Index < static_cast<std::uint32_t>(MenuCorner::CornerCount); ++Index)
    {
        ExtentBand Candidate = MenuAtCorner(Anchor.MinimumX, Anchor.MinimumY,
                                            Anchor.MaximumX, Anchor.MaximumY,
                                            Width, Height,
                                            static_cast<MenuCorner>(Index), Gap);

        // 📐 Slide back inside on each axis independently. The menu keeps the SIDE of the anchor it was
        //    asked for -- a below menu stays below -- and gives up only its exact ordinate along the edge.
        const float SlideX = Candidate.MinimumX < Bounds.MinimumX ? Bounds.MinimumX - Candidate.MinimumX
                           : Candidate.MaximumX > Bounds.MaximumX ? Bounds.MaximumX - Candidate.MaximumX
                           : 0.0f;
        const float SlideY = Candidate.MinimumY < Bounds.MinimumY ? Bounds.MinimumY - Candidate.MinimumY
                           : Candidate.MaximumY > Bounds.MaximumY ? Bounds.MaximumY - Candidate.MaximumY
                           : 0.0f;

        Candidate = ExtentBand{ Candidate.MinimumX + SlideX, Candidate.MinimumY + SlideY,
                                Candidate.MaximumX + SlideX, Candidate.MaximumY + SlideY };

        // 🔴 Sliding on the hanging axis can drag the menu back over its own tile -- a below-trailing
        //    menu slid up is simply on top of the anchor. The tile is never occupied, so this is tested
        //    here rather than left to the caller's list.
        if (ExtentsIntersect(Candidate, Anchor))
            continue;

        bool Clear = true;
        for (std::uint32_t Other = 0u; Other < OccupiedCount; ++Other)
            Clear = Clear && !ExtentsIntersect(Candidate, Occupied[Other]);

        if (Clear)
        {
            Placed = Candidate;
            return true;
        }
    }

    // 🔴 A MENU CAN GENUINELY HAVE NOWHERE TO HANG. A tile near the top edge with a widget filling the
    //    space below it blocks both lower corners, while both upper ones slide down onto the tile — every
    //    corner is spent and the artist has pressed a button that does nothing. Rather than refuse, the
    //    menu detaches from its anchor and takes a free corner of the LEAF instead. It stops being
    //    visually attached to its tile, which is a real cost, but it stays on screen and it still never
    //    covers another widget — and that is the rule that was actually asked for.
    const ExtentBand LeafCorners[4] = {
        { Bounds.MinimumX, Bounds.MinimumY, Bounds.MinimumX + Width, Bounds.MinimumY + Height },
        { Bounds.MaximumX - Width, Bounds.MinimumY, Bounds.MaximumX, Bounds.MinimumY + Height },
        { Bounds.MinimumX, Bounds.MaximumY - Height, Bounds.MinimumX + Width, Bounds.MaximumY },
        { Bounds.MaximumX - Width, Bounds.MaximumY - Height, Bounds.MaximumX, Bounds.MaximumY },
    };

    for (const ExtentBand& Candidate : LeafCorners)
    {
        if (ExtentsIntersect(Candidate, Anchor))
            continue;

        bool Clear = true;
        for (std::uint32_t Other = 0u; Other < OccupiedCount; ++Other)
            Clear = Clear && !ExtentsIntersect(Candidate, Occupied[Other]);

        if (Clear)
        {
            Placed = Candidate;
            return true;
        }
    }

    return false;
}

/// 🧩 Pins a readout to the bottom-right of the bounds, stepping it UP past anything it must not cover.
/// in    Bounds         [px] the leaf the readout must stay inside
/// in    Width, Height  [px] the readout's size
/// in    Occupied       [-]  boxes it must not overlap; may be null when the count is zero
/// in    Gap            [px] clear distance from the bounds' edges and from each obstruction
/// out   Placed         [-]  always written; the readout is placed even when it must overlap
/// note  🔴 UP, NOT AROUND, AND IT NEVER REFUSES. `PlaceMenuClear` searches every corner and refuses when
///        none is free, which is right for a menu hanging off a tile. A readout the artist is watching
///        while they drag must be in ONE known place and must always be visible: a readout that hops
///        corners has to be hunted for, and a readout that refuses has silently taken the figure away
///        mid-gesture. So this keeps the right edge, rises past obstructions, and clamps rather than
///        refusing.
/// note  ⚠️ Clamped to the top of the bounds as a last resort. Overlapping a widget is bad; vanishing
///        while the artist is reading it is worse, and that is a deliberate ordering.
/// note  📝 Bounded at one pass per obstruction. Each pass only ever moves the readout upward, so the
///        loop cannot cycle.
/// cost  ✔️
/// tag   guarantee, nonallocating, nonthrowing
constexpr void PlaceReadoutCorner(const ExtentBand& Bounds, float Width, float Height,
                                  const ExtentBand* Occupied, std::uint32_t OccupiedCount,
                                  float Gap, ExtentBand& Placed)
{
    const float LeftEdge = Bounds.MaximumX - Gap - Width;
    float TopEdge        = Bounds.MaximumY - Gap - Height;

    for (std::uint32_t Pass = 0u; Pass < OccupiedCount; ++Pass)
    {
        bool Moved = false;
        for (std::uint32_t Index = 0u; Index < OccupiedCount; ++Index)
        {
            const ExtentBand& Blocked = Occupied[Index];
            const bool Overlaps = LeftEdge < Blocked.MaximumX && LeftEdge + Width  > Blocked.MinimumX &&
                                  TopEdge  < Blocked.MaximumY && TopEdge  + Height > Blocked.MinimumY;
            if (Overlaps)
            {
                TopEdge = Blocked.MinimumY - Gap - Height;
                Moved = true;
            }
        }
        if (!Moved)
            break;
    }

    if (TopEdge < Bounds.MinimumY + Gap)
        TopEdge = Bounds.MinimumY + Gap;

    Placed.MinimumX = LeftEdge;
    Placed.MinimumY = TopEdge;
    Placed.MaximumX = LeftEdge + Width;
    Placed.MaximumY = TopEdge + Height;
}

}   // namespace Slate
