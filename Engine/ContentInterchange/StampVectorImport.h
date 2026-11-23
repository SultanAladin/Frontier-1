/*============================================================================================================================================
                                                          STAMPVECTORIMPORT.H
============================================================================================================================================*/
// 🧩 Convert one sanitized SVG document into real cubic-path geometry via ThorVG's software SVG parser.

#pragma once
#include "VectorAuthoringDocument.h"
#include <string_view>

namespace Frontier
{
inline constexpr size_t StampVectorMarkupLimit = VectorAuthoringSourceMarkupLimit;

// 📝 Parses SvgMarkup with ThorVG's software SVG loader (the same library the rest of Frontier's
// Pattern canvas is built on) and appends one instruction per solid-filled shape it finds — ThorVG
// already normalizes every arc/quadratic/smooth-curve command into Move/Line/Cubic/Close, so no
// curve elevation is needed, unlike the stb_truetype text path. Multiple shapes are combined
// back-to-front with the Over compositing category so a multi-coloured import keeps every shape's
// own colour (Union/Difference would collapse them to one). Returns the resulting root instruction
// index, suitable for a Stamp ImportedVector layer's Root field.
//
// Known v1 scope, disclosed rather than silently approximated:
//  - ThorVG's loader never executes <script>, never fetches <image>/external <use> references, and
//    CSS styling is not evaluated — only inline presentation attributes/style on shape elements.
//  - Only solid shape fills are ported; a gradient-filled shape falls back to an opaque mid-grey.
//  - A shape's own `transform` attribute is composed through its SVG ancestor <g> chain, but
//    gradient-space and clip-path transforms are not (clip-paths/masks are not ported in v1).
// Throws std::runtime_error if the markup cannot be parsed, exceeds StampVectorMarkupLimit, or
// contains no renderable shape.
uint32_t AppendStampVectorImport(VectorAuthoringDocument &Document, std::string_view SvgMarkup);
}
