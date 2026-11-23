//============================================================================================================================================
//                                                             TYPEFACECODEC.H
//============================================================================================================================================
// 🧩 `10` §1 — typeface streams translated into glyph outlines and the metrics that position them.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateDocument/Document/VectorInterchange/Api/VectorInterchange.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     ONE ADJUSTMENT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One pair adjustment as the typeface declared it, between two glyphs named by their own ordinals.
/// note  📝 Held here rather than reached for through `TypefaceInterchange` because a decode produces the whole
///        set at once and the interchange takes them one at a time. The spelling matches its private sibling
///        deliberately: what crosses this seam is the same triple.
/// tag   nonallocating, nonthrowing
struct DecodedAdjustment
{
    std::uint32_t  EarlierGlyph = 0u;    // [-] - the preceding glyph, by its own ordinal
    std::uint32_t  LaterGlyph   = 0u;    // [-] - the following one
    double         Adjustment   = 0.0;   // [-] - in the typeface's own units
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT A DECODE YIELDS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One decoded typeface — its glyphs, its adjustments, and the unit scale they are all expressed in.
/// note  🔴 `52` §3: the glyphs carry their own ordinals and never a character. A codepoint is a property of the
///        text, not of the typeface, and resolving one to the other is substitution — which belongs at intake,
///        where it is recorded, rather than at every use.
/// tag   owning
struct DecodedTypeface
{
    std::vector<GlyphSpecification>   Glyphs      = {};       // [-] - in the typeface's own ordinal order
    std::vector<DecodedAdjustment>    Adjustments = {};       // [-] - as the typeface declared them
    double                            UnitsPerEm  = 1000.0;   // [-] - the units every metric above is in
    std::uint32_t                     GlyphCount  = 0u;       // [-] - what the typeface declares it holds
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE TRANSLATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Translates one typeface stream into glyph outlines, in the typeface's own units.
/// in    Stream        [-]  the whole stream, as `StorageExchange` drained it
/// in    GlyphLimit  [-]  how many glyph ordinals to translate; the typeface's own count where it is lower
/// out   Result       [-]  refuses with ContentUnsupported for a stream the reader rejected, and with
///                          ExtentExhausted for a typeface declaring no glyph at all
/// err   never throws; every shape the reader allocated is released before returning, on refusal included
/// cost  🔴
/// note  🔴 Outlines arrive in the typeface's own units and are not scaled to a pixel size here. A typeface
///        scaled at decode is a typeface that must be decoded again for every size the artist tries, and the
///        outline they placed at one size would not be the outline resolved at another.
/// note  📝 A quadratic contour is carried as `SegmentSubject::Quadratic` and a cubic as `Cubic`, so no
///        conversion between them happens at intake. Converting would put a tolerance into a translation, and
///        `52` §4's tolerance is resolution-relative and belongs to whoever flattens.
/// note  ⚠️ An empty glyph — a space, most often — decodes to a glyph carrying no path and is retained. Dropping
///        it would renumber every ordinal after it, and the glyph sequence `52` §3 stores indexes those ordinals.
/// tag   api, nonthrowing
Deliver<DecodedTypeface> Translate(const std::vector<std::uint8_t>& Stream, std::uint32_t GlyphLimit);

SLATE_DECLARES_PRECISION(PrecisionGuarantee::Exact,
                         PrecisionGuarantee::Exact);

/// 🧩 Resolves one codepoint to the glyph ordinal the typeface holds it under.
/// in    Stream     [-]  the same stream the translation was taken from
/// in    Codepoint  [-]  one Unicode scalar value
/// out   Result    [-]  refuses with ContentUnsupported when the typeface maps that codepoint to nothing
/// cost  🚩
/// note  🔴 Separate from the translation because substitution belongs at intake — `52` §3. Text is resolved to a
///        glyph sequence once and that sequence is what is stored; resolving at every use would mean replacing a
///        typeface silently reshapes text the artist has already positioned.
/// tag   api, nonthrowing
Deliver<std::uint32_t> ResolveCodepoint(const std::vector<std::uint8_t>& Stream, std::uint32_t Codepoint);

SLATE_DECLARES_PRECISION(PrecisionGuarantee::Exact,
                         PrecisionGuarantee::Exact);

}   // namespace Slate
