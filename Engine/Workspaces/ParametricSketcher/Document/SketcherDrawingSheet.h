/*====================================================================================================================================
                                                        SOLIDARCDRAWINGSHEET.H
====================================================================================================================================*/
// 🧩 Drawing sheet (Alt+D): the shown solid bodies of the document as an ISO drawing — the kernel's Drawing/DrawingSheet
//    (views, hidden lines, automatic dimensions, title block) and its SVG / DXF R12 export; the settings are saved with
//    the document

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherMeasurement.h"

#include "Engine/GeometrySpace/Parametric/Drawing/DrawingSheet.h"

#include <cstdint>
#include <string>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr double SketcherModelUnitMillimetres = 1000.0;                               // [mm] - One model unit (metre) on paper

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 💡 One composition: the kernel sheet of the shown solid bodies (hidden bodies and sheet bodies left out) in
//    millimetres, cached by the document revision and the settings key.
struct SketcherDrawingSheet
{
    uint64_t                Revision     = 0;                                                // [-]  - Document revision drawn
    std::string             SettingsKey  = {};                                               // [-]  - SketcherDrawingSheetCacheKey drawn
    GeometrySpace::Parametric::DrawingSheet  Sheet        = {};                                               // [-]  - Kernel sheet (millimetres)
    int                     Bodies       = 0;                                                // [-]  - Solid bodies drawn
    int                     Sheets       = 0;                                                // [-]  - Sheet bodies left out
    double                  Milliseconds = 0.0;                                              // [ms] - Composition time
    std::string             Refusal      = {};                                               // [-]  - Empty on success
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The drawing sheet of the document's shown solid bodies.
[[nodiscard]] SketcherDrawingSheet ComposeSketcherDrawingSheet(const SketcherDocument& Document, const SketcherDrawingSheetSettings& Settings);

// Settings fingerprint (cache key and script readout).
[[nodiscard]] std::string SketcherDrawingSheetKey(const SketcherDrawingSheetSettings& Settings);

// Cache key of a composition: the settings key plus the hidden bodies (hiding is an appearance edit, not a revision).
[[nodiscard]] std::string SketcherDrawingSheetCacheKey(const SketcherDocument& Document, const SketcherDrawingSheetSettings& Settings);

// True when the settings equal the defaults (the codec then writes no [DrawingSheet] table).
[[nodiscard]] bool DefaultSketcherDrawingSheetCondition(const SketcherDrawingSheetSettings& Settings);

// Kernel section frame of a Section-card plane: the card keeps its +normal side, the sheet keeps the −ZAxis side.
[[nodiscard]] GeometrySpace::Parametric::OrthonormalBasis SketcherSheetSectionFrame(const GeometrySpace::Parametric::OrthonormalBasis& Plane);

// Where the sheet is exported: "Bracket.sketcher" → "Bracket.svg" / "Bracket.dxf" beside it; no document path →
// "Untitled.svg" in the working folder. Extension includes the dot.
[[nodiscard]] std::string SketcherDrawingSheetExportPath(const std::string& DocumentPath, const std::string& Extension);

// Writes the sheet as SVG or DXF R12 (by the path's extension); returns the refusal (empty on success).
[[nodiscard]] std::string ExportSketcherDrawingSheet(const SketcherDrawingSheet& Sheet, const std::string& Path);

// "A3", "First", "Y", … ↔ enums for the codec and scripts (false / empty for an unknown label).
[[nodiscard]] const char* SketcherProjectionAngleLabel(GeometrySpace::Parametric::DrawingProjectionAngle Angle) noexcept;
[[nodiscard]] bool        ParseSketcherPaperSize(const std::string& Label, GeometrySpace::Parametric::DrawingPaperSize& Paper) noexcept;
[[nodiscard]] bool        ParseSketcherProjectionAngle(const std::string& Label, GeometrySpace::Parametric::DrawingProjectionAngle& Angle) noexcept;

} // namespace Frontier::Workspaces::Sketcher
