//============================================================================================================================================
//                                                            TYPEFACECODEC.CPP
//============================================================================================================================================
// 🧩 `10` §1 — typeface streams translated into glyph outlines and the metrics that position them.

#include "SlateDocument/Format/TypefaceCodec/Api/TypefaceCodec.h"

#include <cstddef>

// 📝 The vendored reader is compiled into this translation unit only. STBTT_STATIC keeps every symbol it
//    declares internal, so the copy ImGui compiles inside `SlateUI` and this one never reach the same link.
#pragma warning(push, 0)
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"
#pragma warning(pop)

namespace Slate
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE UNIT RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 A typeface declares its own units per em in its heading, and every metric below is expressed in them.
//    Where the heading is unreadable the common default stands in, because a unit scale of nothing would make
//    every advance infinite once divided through.
constexpr double AssumedUnitsPerEm = 1000.0;   // [-] - the conventional scale where the heading declares none

/// 🧩 Reads a typeface's units per em from its heading, falling back where the heading does not carry one.
double ResolveUnitsPerEm(const stbtt_fontinfo& Reading)
{
    const float Scaled = stbtt_ScaleForMappingEmToPixels(&Reading, 1.0f);

    if (!(Scaled > 0.0f)) { return AssumedUnitsPerEm; }

    return 1.0 / static_cast<double>(Scaled);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE GLYPH SHAPE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Converts one glyph's contour run into the path run `52` §2 accepts, in the typeface's own units.
/// note  🔴 A contour is closed on the move that begins the next one, and on the end of the run. A typeface
///        contour is closed by construction — an open one would have no interior — so the fill rule below is
///        NonZero for every path, which is the rule outlines are authored under.
void TranslateShape(const stbtt_vertex* Contours, int ContourCount, std::vector<OutlinePath>& Appending)
{
    OutlinePath  Constructing;
    bool         PathOccupied = false;

    for (int Index = 0; Index < ContourCount; ++Index)
    {
        const stbtt_vertex& Carried = Contours[Index];

        if (Carried.type == STBTT_vmove)
        {
            if (PathOccupied && !Constructing.Segments.empty())
            {
                Appending.push_back(Constructing);
            }

            Constructing           = OutlinePath{};
            Constructing.Origin    = { static_cast<double>(Carried.x), static_cast<double>(Carried.y) };
            Constructing.Rule      = FillRule::NonZero;
            Constructing.ClosedRun = true;
            PathOccupied           = true;

            continue;
        }

        if (!PathOccupied) { continue; }

        PathSegment Placed;
        Placed.Terminus = { static_cast<double>(Carried.x), static_cast<double>(Carried.y) };

        if (Carried.type == STBTT_vline)
        {
            Placed.Subject = SegmentSubject::Line;
        }
        else if (Carried.type == STBTT_vcurve)
        {
            Placed.Subject      = SegmentSubject::Quadratic;
            Placed.FirstControl = { static_cast<double>(Carried.cx), static_cast<double>(Carried.cy) };
        }
        else if (Carried.type == STBTT_vcubic)
        {
            Placed.Subject       = SegmentSubject::Cubic;
            Placed.FirstControl  = { static_cast<double>(Carried.cx),  static_cast<double>(Carried.cy) };
            Placed.SecondControl = { static_cast<double>(Carried.cx1), static_cast<double>(Carried.cy1) };
        }
        else
        {
            continue;
        }

        Constructing.Segments.push_back(Placed);
    }

    if (PathOccupied && !Constructing.Segments.empty())
    {
        Appending.push_back(Constructing);
    }
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE TRANSLATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<DecodedTypeface> Translate(const std::vector<std::uint8_t>& Stream, std::uint32_t GlyphLimit)
{
    if (Stream.empty())
    {
        return Deliver<DecodedTypeface>::Refuse(
            { RefusalReason::ContentUnsupported, "a typeface stream of no bytes carries no typeface" });
    }

    stbtt_fontinfo Reading;

    const int Offset = stbtt_GetFontOffsetForIndex(Stream.data(), 0);

    if (Offset < 0 || stbtt_InitFont(&Reading, Stream.data(), Offset) == 0)
    {
        return Deliver<DecodedTypeface>::Refuse(
            { RefusalReason::ContentUnsupported, "the reader rejected the typeface stream" });
    }

    const int DeclaredGlyphCount = Reading.numGlyphs;

    if (DeclaredGlyphCount <= 0)
    {
        return Deliver<DecodedTypeface>::Refuse(
            { RefusalReason::ExtentExhausted, "the typeface declares no glyph — `10` §1" });
    }

    DecodedTypeface Produced;
    Produced.UnitsPerEm = ResolveUnitsPerEm(Reading);
    Produced.GlyphCount = static_cast<std::uint32_t>(DeclaredGlyphCount);

    const std::uint32_t Translating = GlyphLimit < Produced.GlyphCount ? GlyphLimit : Produced.GlyphCount;

    Produced.Glyphs.reserve(Translating);

    for (std::uint32_t Index = 0u; Index < Translating; ++Index)
    {
        GlyphSpecification Constructing;
        Constructing.GlyphIdentity = Index;

        int Advance = 0;
        int Bearing = 0;
        stbtt_GetGlyphHMetrics(&Reading, static_cast<int>(Index), &Advance, &Bearing);

        Constructing.Advance      = static_cast<double>(Advance);
        Constructing.BearingX = static_cast<double>(Bearing);

        int GlyphBox[4] = { 0, 0, 0, 0 };

        if (stbtt_GetGlyphBox(&Reading, static_cast<int>(Index), &GlyphBox[0], &GlyphBox[1], &GlyphBox[2], &GlyphBox[3]) != 0)
        {
            Constructing.BearingY = static_cast<double>(GlyphBox[1]);
        }

        stbtt_vertex* Contours     = nullptr;
        const int     ContourCount = stbtt_GetGlyphShape(&Reading, static_cast<int>(Index), &Contours);

        if (Contours != nullptr)
        {
            TranslateShape(Contours, ContourCount, Constructing.Paths);
            stbtt_FreeShape(&Reading, Contours);
        }

        // ⚠️ A glyph carrying no path — a space, most often — is retained rather than skipped. Dropping it
        //    would renumber every ordinal above it, and `52` §3's stored glyph sequence indexes those ordinals.
        Produced.Glyphs.push_back(Constructing);
    }

    const int AdjustmentCount = stbtt_GetKerningTableLength(&Reading);

    if (AdjustmentCount > 0)
    {
        std::vector<stbtt_kerningentry> Carried(static_cast<std::size_t>(AdjustmentCount));

        const int Written = stbtt_GetKerningTable(&Reading, Carried.data(), AdjustmentCount);

        Produced.Adjustments.reserve(static_cast<std::size_t>(Written > 0 ? Written : 0));

        for (int Index = 0; Index < Written; ++Index)
        {
            const stbtt_kerningentry& Declaring = Carried[static_cast<std::size_t>(Index)];

            DecodedAdjustment Placed;
            Placed.EarlierGlyph = static_cast<std::uint32_t>(Declaring.glyph1);
            Placed.LaterGlyph   = static_cast<std::uint32_t>(Declaring.glyph2);
            Placed.Adjustment   = static_cast<double>(Declaring.advance);

            Produced.Adjustments.push_back(Placed);
        }
    }

    return Deliver<DecodedTypeface>::Result(Produced);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE SUBSTITUTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> ResolveCodepoint(const std::vector<std::uint8_t>& Stream, std::uint32_t Codepoint)
{
    if (Stream.empty())
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "a typeface stream of no bytes maps no codepoint" });
    }

    stbtt_fontinfo Reading;

    const int Offset = stbtt_GetFontOffsetForIndex(Stream.data(), 0);

    if (Offset < 0 || stbtt_InitFont(&Reading, Stream.data(), Offset) == 0)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "the reader rejected the typeface stream" });
    }

    const int Resolved = stbtt_FindGlyphIndex(&Reading, static_cast<int>(Codepoint));

    // 🔴 Index zero is the typeface's own absent glyph and is a refusal here rather than a delivered result.
    //    Delivering it would put a visible substitute into a stored glyph sequence, and `52` §3 stores that
    //    sequence — so the artist's text would carry the substitute permanently, with nothing recording why.
    if (Resolved <= 0)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "the typeface maps that codepoint to no glyph — `52` §3" });
    }

    return Deliver<std::uint32_t>::Result(static_cast<std::uint32_t>(Resolved));
}

}   // namespace Slate
