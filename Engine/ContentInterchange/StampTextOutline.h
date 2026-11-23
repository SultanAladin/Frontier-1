/*============================================================================================================================================
                                                           STAMPTEXTOUTLINE.H
============================================================================================================================================*/
// 🧩 Convert one line of Stamp text into a real font-outline cubic contour, no placeholder geometry.

#pragma once
#include "VectorAuthoringDocument.h"
#include <string_view>

namespace Frontier
{
inline constexpr size_t StampTextCharacterLimit = 160; // [glyphs] - A decal caption, not a paragraph

// 📝 Reads actual glyph outlines from a bundled TrueType file via stb_truetype (no placeholder
// rectangles), lays them out left-to-right with real advance/kerning metrics, elevates every
// quadratic curve to the engine's cubic-only segment format, and appends ONE new CubicContour
// VectorCoverageInstruction (holding every glyph's closed sub-contours back to back, rendered
// under the nonzero winding fill rule so letterforms with counters such as "O" or "B" keep their
// holes for free — no Boolean Union/Difference tree is required) plus its backing segments to
// Document.Instructions/Document.Segments. Returns the new instruction's index, suitable for a
// Stamp text layer's Root field. Throws std::runtime_error if the typeface cannot be read, Text is
// empty or exceeds StampTextCharacterLimit, PointSize is not finite and positive, or the text
// resolves to no visible ink (e.g. only whitespace).
//
// WrapWidth <= 0 (the default) lays the whole string out on one line, exactly as before. WrapWidth
// > 0 greedily word-wraps onto as many additional lines as needed to keep every line within that
// width (in the same units as PointSize), stacking lines downward by the typeface's own line
// height — so a caption that keeps growing fills a box with more lines instead of shrinking onto
// one ever-thinner line.
uint32_t AppendStampTextOutline(VectorAuthoringDocument &Document, std::string_view Text, float PointSize,
    std::array<float, 4> Colour, std::string_view TypefacePath = "EngineContent/FontArchives/DMSans/DMSans-Regular.ttf",
    float WrapWidth = 0.f);
}
