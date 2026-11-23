//============================================================================================================================================
//                                                     OVERLAYEXCLUSIONPROOF.CPP
//============================================================================================================================================
// 🧩 Proves the band arithmetic that keeps the GPU overlay passes off an open menu.
//
// 🔴 THE DEFECT THIS ANSWERS FOR. The viewport footer's dropdowns were reported as transparent. Every
//    plate was already filled with an opaque theme token; nothing was ever drawn see-through. What
//    actually happened is that the overlay pass and the sketch pass both record AFTER the interface,
//    and both scissored to the WHOLE viewport leaf, so the grid, the axes and the sketch textured
//    straight over the menu. What showed through was the viewport behind it, which is exactly what a
//    transparent menu would look like.
//
// 🔴 AND THE CURE HAD TO NOT BE THE OBVIOUS ONE. `EditorPanel::AnyPopupStanding` already existed, with
//    a comment describing this defect precisely, and the host never called it -- the fourth time in
//    this codebase that a correct function sat with no call site. Suppressing the overlay whenever a
//    menu stands is what that predicate invites, and it is the same trade that once made the drawers
//    erase the whole sketch: a menu covers a few hundred pixels, and blanking a viewport's grid to
//    protect them swaps one visible defect for a worse one. So the menu's box is SUBTRACTED from the
//    scissor instead, and the remainder recorded as bands.
//
// A scissor is one rectangle, so "everything except this box" needs up to four. The claims below are
// about coverage and disjointness, not about the shape of the answer: a band decomposition is correct
// when every pixel of the clip that the menu does not cover is written exactly once, and every pixel
// it does cover is written not at all. That is measured here by rasterising the bands into a grid and
// counting each cell's writes, so a decomposition that is merely plausible cannot pass.

#include "Foundation/ExtentBands.h"

#include <cstdio>
#include <cstdint>
#include <vector>

using namespace Slate;

/// The proof states its own boxes; `PlaneExtent` belongs to the interface layer and this arithmetic
/// deliberately does not.
struct PlaneExtent
{
    float MinimumX = 0.0f, MinimumY = 0.0f, MaximumX = 0.0f, MaximumY = 0.0f;
    constexpr float Width() const  { return MaximumX - MinimumX; }
    constexpr float Height() const { return MaximumY - MinimumY; }
    constexpr bool Encloses(float X, float Y) const
    {
        return X >= MinimumX && X < MaximumX && Y >= MinimumY && Y < MaximumY;
    }
};

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

/// Counts how many bands cover each cell of `Clip`, at one-pixel resolution.
/// 🔴 This is the whole point of the proof. Comparing band COUNTS or band AREAS would accept a
///    decomposition whose pieces overlap -- the areas can still sum correctly while a fragment is
///    shaded twice, which a straight-alpha blend shows as a visible seam.
struct Coverage
{
    std::uint32_t Written   = 0u;   // cells covered exactly once, outside the withheld box
    std::uint32_t Twice     = 0u;   // cells covered more than once -- always a defect
    std::uint32_t Missed    = 0u;   // cells outside the withheld box that no band covered
    std::uint32_t Trespass  = 0u;   // cells inside the withheld box that a band covered
};

Coverage Measure(const PlaneExtent& Clip, const PlaneExtent& Withheld)
{
    ExtentBand Bands[4] = {};
    const std::uint32_t Count = ExtentBandsAround(Clip.MinimumX, Clip.MinimumY,
                                                  Clip.MaximumX, Clip.MaximumY,
                                                  Withheld.MinimumX, Withheld.MinimumY,
                                                  Withheld.MaximumX, Withheld.MaximumY, Bands);

    const int X0 = static_cast<int>(Clip.MinimumX);
    const int Y0 = static_cast<int>(Clip.MinimumY);
    const int X1 = static_cast<int>(Clip.MaximumX);
    const int Y1 = static_cast<int>(Clip.MaximumY);

    Coverage Result;

    for (int Y = Y0; Y < Y1; ++Y)
    {
        for (int X = X0; X < X1; ++X)
        {
            const float CentreX = static_cast<float>(X) + 0.5f;
            const float CentreY = static_cast<float>(Y) + 0.5f;

            std::uint32_t Hits = 0u;
            for (std::uint32_t Index = 0u; Index < Count; ++Index)
                if (CentreX >= Bands[Index].MinimumX && CentreX < Bands[Index].MaximumX &&
                    CentreY >= Bands[Index].MinimumY && CentreY < Bands[Index].MaximumY)
                    ++Hits;

            const bool Inside = Withheld.Encloses(CentreX, CentreY);

            if (Inside)
            {
                if (Hits > 0u)
                    ++Result.Trespass;
            }
            else if (Hits == 0u)
                ++Result.Missed;
            else if (Hits > 1u)
                ++Result.Twice;
            else
                ++Result.Written;
        }
    }

    return Result;
}

void Report(const char* Title, const Coverage& Seen, std::uint32_t Bands)
{
    std::printf("    %-22s bands %u · written %u · twice %u · missed %u · trespass %u\n",
                Title, Bands, Seen.Written, Seen.Twice, Seen.Missed, Seen.Trespass);
}

/// Folded at compile time, so the compiler itself checks the arithmetic reads nothing uninitialised.
constexpr std::uint32_t FoldedBands = []
{
    ExtentBand Bands[4] = {};
    return ExtentBandsAround(0.0f, 0.0f, 100.0f, 100.0f, 40.0f, 40.0f, 60.0f, 60.0f, Bands);
}();

std::uint32_t BandCount(const PlaneExtent& Clip, const PlaneExtent& Withheld)
{
    ExtentBand Bands[4] = {};
    return ExtentBandsAround(Clip.MinimumX, Clip.MinimumY, Clip.MaximumX, Clip.MaximumY,
                             Withheld.MinimumX, Withheld.MinimumY,
                             Withheld.MaximumX, Withheld.MaximumY, Bands);
}

}   // namespace

int main()
{
    std::printf("\n[OverlayExclusion] the bands that keep the grid off an open menu\n\n");

    const PlaneExtent Leaf{ 100.0f, 80.0f, 500.0f, 400.0f };

    // ① A MENU FULLY INSIDE THE LEAF. The ordinary case: a dropdown opened from the footer, sitting
    //    clear of every edge. Four bands, and every pixel outside it written exactly once.
    {
        const PlaneExtent Menu{ 300.0f, 250.0f, 420.0f, 360.0f };
        const Coverage Seen = Measure(Leaf, Menu);
        const std::uint32_t Bands = BandCount(Leaf, Menu);
        std::printf("  ① a menu clear of every edge\n");
        Report("interior", Seen, Bands);

        Claim(Bands == 4u, "a menu clear of every edge leaves four bands");
        Claim(Seen.Twice == 0u, "no pixel may be recorded twice: a straight-alpha blend shows the seam");
        Claim(Seen.Missed == 0u, "every pixel the menu does not cover must still be recorded");
        Claim(Seen.Trespass == 0u, "no band may touch the menu -- that is the whole defect");

        const std::uint32_t Expected =
            static_cast<std::uint32_t>(Leaf.Width() * Leaf.Height()) -
            static_cast<std::uint32_t>(Menu.Width() * Menu.Height());
        Claim(Seen.Written == Expected, "the written area is the leaf less the menu, exactly");
    }

    // ② NO MENU STANDING. The overwhelmingly common frame. It must cost exactly one recording -- a
    //    decomposition that emitted four degenerate bands would quadruple the pass for no reason.
    {
        const PlaneExtent None{};
        const Coverage Seen = Measure(Leaf, None);
        const std::uint32_t Bands = BandCount(Leaf, None);
        std::printf("  ② nothing withheld\n");
        Report("no menu", Seen, Bands);

        Claim(Bands == 1u, "with no menu standing the leaf records as ONE band, not four empty ones");
        Claim(Seen.Missed == 0u && Seen.Twice == 0u, "the whole leaf is written once");
        Claim(Seen.Written == static_cast<std::uint32_t>(Leaf.Width() * Leaf.Height()),
              "the written area is the whole leaf");
    }

    // ③ A MENU THAT MISSES THIS LEAF ENTIRELY. In a split viewport a menu belongs to one leaf; the
    //    others must not pay for it, and must not be carved by a box that does not touch them.
    {
        const PlaneExtent Elsewhere{ 700.0f, 600.0f, 820.0f, 700.0f };
        const Coverage Seen = Measure(Leaf, Elsewhere);
        const std::uint32_t Bands = BandCount(Leaf, Elsewhere);
        std::printf("  ③ a menu over a different leaf\n");
        Report("disjoint", Seen, Bands);

        Claim(Bands == 1u, "a menu that misses this leaf costs it one recording, not four");
        Claim(Seen.Written == static_cast<std::uint32_t>(Leaf.Width() * Leaf.Height()),
              "a leaf the menu misses is recorded whole");
    }

    // ④ EVERY EDGE AND EVERY CORNER. A menu opened from the footer sits AGAINST the lower edge, and one
    //    opened near a split sits against a side. These are the cases where a naive four-band split
    //    emits inverted or overlapping rectangles.
    {
        struct Placement
        {
            const char* Title;
            PlaneExtent Menu;
            std::uint32_t Bands;
        };

        const Placement Placements[] = {
            { "against the bottom", { 300.0f, 320.0f, 420.0f, 400.0f }, 3u },
            { "against the top",    { 300.0f,  80.0f, 420.0f, 160.0f }, 3u },
            { "against the left",   { 100.0f, 200.0f, 220.0f, 300.0f }, 3u },
            { "against the right",  { 380.0f, 200.0f, 500.0f, 300.0f }, 3u },
            { "lower-left corner",  { 100.0f, 320.0f, 220.0f, 400.0f }, 2u },
            { "lower-right corner", { 380.0f, 320.0f, 500.0f, 400.0f }, 2u },
            { "upper-left corner",  { 100.0f,  80.0f, 220.0f, 160.0f }, 2u },
            { "upper-right corner", { 380.0f,  80.0f, 500.0f, 160.0f }, 2u },
            { "a full-width strip", { 100.0f, 320.0f, 500.0f, 400.0f }, 1u },
            { "a full-height strip",{ 380.0f,  80.0f, 500.0f, 400.0f }, 1u },
        };

        std::printf("  ④ every edge and every corner\n");
        for (const Placement& Current : Placements)
        {
            const Coverage Seen = Measure(Leaf, Current.Menu);
            const std::uint32_t Bands = BandCount(Leaf, Current.Menu);
            Report(Current.Title, Seen, Bands);

            Claim(Seen.Twice == 0u, "an edge-adjacent menu must not produce overlapping bands");
            Claim(Seen.Trespass == 0u, "an edge-adjacent menu must still be kept clear");
            Claim(Seen.Missed == 0u, "an edge-adjacent menu must not blank anything beside it");
            Claim(Bands == Current.Bands,
                  "an edge-adjacent menu needs no degenerate band: the count must drop");

            const std::uint32_t Expected =
                static_cast<std::uint32_t>(Leaf.Width() * Leaf.Height()) -
                static_cast<std::uint32_t>(Current.Menu.Width() * Current.Menu.Height());
            Claim(Seen.Written == Expected, "the written area is the leaf less the menu");
        }
    }

    // ⑤ A MENU HANGING OVER THE EDGE. The footer menus are placed by `FitExtent`, which clamps them to
    //    the leaf -- but a menu is also allowed to be taller than the space above its button. The part
    //    outside the leaf must be ignored rather than carving a phantom band.
    {
        const PlaneExtent Overhanging{ 380.0f, 300.0f, 620.0f, 520.0f };
        const Coverage Seen = Measure(Leaf, Overhanging);
        const std::uint32_t Bands = BandCount(Leaf, Overhanging);
        std::printf("  ⑤ a menu hanging past the leaf's corner\n");
        Report("overhanging", Seen, Bands);

        Claim(Seen.Twice == 0u && Seen.Missed == 0u && Seen.Trespass == 0u,
              "a menu clipped by the leaf's edge still yields a clean decomposition");
        Claim(Bands == 2u, "only the overlapping part is carved out");

        const float OverlapWidth  = Leaf.MaximumX - Overhanging.MinimumX;
        const float OverlapHeight = Leaf.MaximumY - Overhanging.MinimumY;
        const std::uint32_t Expected =
            static_cast<std::uint32_t>(Leaf.Width() * Leaf.Height()) -
            static_cast<std::uint32_t>(OverlapWidth * OverlapHeight);
        Claim(Seen.Written == Expected, "only the part of the menu inside the leaf is withheld");
    }

    // ⑥ A MENU COVERING THE WHOLE LEAF. Nothing may be recorded at all. This is the only case in which
    //    the overlay legitimately disappears, and it must arise from the arithmetic rather than a
    //    special case bolted on beside it.
    {
        const PlaneExtent Everything{ 0.0f, 0.0f, 900.0f, 900.0f };
        const Coverage Seen = Measure(Leaf, Everything);
        const std::uint32_t Bands = BandCount(Leaf, Everything);
        std::printf("  ⑥ a menu covering the leaf entirely\n");
        Report("total", Seen, Bands);

        Claim(Bands == 0u, "a menu covering the leaf leaves no band to record");
        Claim(Seen.Written == 0u && Seen.Trespass == 0u, "nothing at all is recorded beneath it");
    }

    // ⑦ A DEGENERATE CLIP. Two drawers meeting in the middle leave a leaf no uncovered rows at all.
    //    Recording an inverted box is a validation error, not an empty draw.
    {
        const PlaneExtent Inverted{ 100.0f, 300.0f, 500.0f, 200.0f };
        const PlaneExtent Empty{ 100.0f, 200.0f, 100.0f, 400.0f };
        std::printf("  ⑦ degenerate clips\n");

        Claim(BandCount(Inverted, PlaneExtent{}) == 0u,
              "an inverted clip records nothing rather than an invalid scissor");
        Claim(BandCount(Empty, PlaneExtent{}) == 0u,
              "a zero-width clip records nothing rather than an invalid scissor");
        std::printf("    inverted and zero-width clips both record no bands\n");
    }

    // ⑧ THE BANDS STAY INSIDE THE CLIP. A band that escaped the leaf would texture the outliner or the
    //    properties -- the defect the scissor exists to prevent in the first place.
    {
        const PlaneExtent Menu{ 300.0f, 250.0f, 420.0f, 360.0f };
        ExtentBand Bands[4] = {};
        const std::uint32_t Count = ExtentBandsAround(Leaf.MinimumX, Leaf.MinimumY,
                                                  Leaf.MaximumX, Leaf.MaximumY,
                                                  Menu.MinimumX, Menu.MinimumY,
                                                  Menu.MaximumX, Menu.MaximumY, Bands);

        bool Contained = true;
        for (std::uint32_t Index = 0u; Index < Count; ++Index)
        {
            Contained = Contained &&
                        Bands[Index].MinimumX >= Leaf.MinimumX && Bands[Index].MaximumX <= Leaf.MaximumX &&
                        Bands[Index].MinimumY >= Leaf.MinimumY && Bands[Index].MaximumY <= Leaf.MaximumY;
        }
        std::printf("  ⑧ every band lies within the leaf\n");
        Claim(Contained, "a band outside the leaf would texture over the neighbouring panels");
    }

    // ⑨ IT IS USABLE AT COMPILE TIME. The arithmetic is constexpr, so a caller can fold a fixed layout
    //    and, more usefully, the compiler checks it has no path that reads uninitialised storage.
    {
        static_assert(FoldedBands == 4u, "the band arithmetic must be usable in a constant expression");
        std::printf("  ⑨ the arithmetic folds at compile time\n");
        Claim(FoldedBands == 4u, "the band arithmetic is a constant expression");
    }

    std::printf("\n[OverlayExclusion] %u claims, %u failures\n\n", Claims, Failures);
    return Failures == 0u ? 0 : 1;
}
