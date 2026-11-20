/*====================================================================================================================================
                                                       DRAWINGEXPORT.H
====================================================================================================================================*/
// 🧩 Drawing sheet export: SVG (millimetre viewBox, one group per layer) and DXF R12 (AC1009 ASCII: layers, line types, entities)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Drawing/DrawingSheet.h"

#include <string>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 SVG: width / height in mm, viewBox in mm with 𝑦 flipped (SVG 𝑦 runs down); groups in SheetLayer order with the
//    ISO 128-2 widths (0.5 / 0.25 / 0.18 mm), dashes 3 1.5 for hidden and 8 1.5 1 1.5 for chain lines; coordinates to
//    10⁻⁶ mm; texts in a monospace face (the composer's width estimate is 0.6·h per glyph).
[[nodiscard]] std::string WriteDrawingSvg(const DrawingSheet& Sheet);

// 📝 DXF R12 (AC1009): HEADER ($ACADVER, $EXTMIN / $EXTMAX, $LIMMIN / $LIMMAX), TABLES (LTYPE CONTINUOUS / HIDDEN /
//    CENTER, one LAYER per SheetLayer, STYLE STANDARD), ENTITIES (LINE for two points, POLYLINE / VERTEX / SEQEND
//    otherwise, TEXT with justification and rotation, SOLID for arrowheads); 15 significant digits; ⌀ → %%c.
[[nodiscard]] std::string WriteDrawingDxf(const DrawingSheet& Sheet);

// Writes Sheet to Path as SVG or DXF by its extension. Returns an empty string or the refusal.
[[nodiscard]] std::string ExportDrawingFile(const DrawingSheet& Sheet, const std::string& Path);

} // namespace Frontier::GeometrySpace::Parametric
