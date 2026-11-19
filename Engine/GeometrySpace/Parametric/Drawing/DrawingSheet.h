/*====================================================================================================================================
                                                        DRAWINGSHEET.H
====================================================================================================================================*/
// 🧩 Drawing sheets: paper, first / third angle view layout, standard scales, automatic dimensions, section line, title block

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Drawing/DrawingProjection.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 ISO 5456-2. Third angle: the view from above sits above the front view, the view from the right on its right.
//    First angle: the view from above sits below, the view from the right on the left.
enum class DrawingProjectionAngle : uint8_t
{
    First = 0,                                                                               // ISO (Europe, Asia)
    Third = 1,                                                                               // ASME Y14.3 (USA, Canada)
};

enum class DrawingPaperSize : uint8_t                                                        // ISO 216, landscape
{
    A4 = 0,                                                                                  // 297 × 210 mm
    A3 = 1,                                                                                  // 420 × 297 mm
    A2 = 2,                                                                                  // 594 × 420 mm
    A1 = 3,                                                                                  // 841 × 594 mm
    A0 = 4,                                                                                  // 1189 × 841 mm
};

// 📝 Layers double as line styles (ISO 128-2): wide continuous for outlines, narrow dashed for hidden, narrow chain for
//    centre and cutting-plane lines, narrow continuous for dimensions and hatching.
enum class SheetLayer : uint8_t
{
    Visible        = 0,                                                                      // Visible edges and silhouettes (0.5 mm)
    Hidden         = 1,                                                                      // Hidden edges and silhouettes (0.25 mm dashed)
    Tangent        = 2,                                                                      // Tangent edges (0.18 mm)
    SectionOutline = 3,                                                                      // Section cap outlines (0.5 mm)
    Hatch          = 4,                                                                      // Section hatching (0.18 mm)
    Dimension      = 5,                                                                      // Dimension, extension and leader lines, arrows, values
    Centre         = 6,                                                                      // Centre marks (chain)
    CuttingPlane   = 7,                                                                      // Section line with arrows and letters (chain)
    Border         = 8,                                                                      // Sheet frame (0.7 mm)
    Title          = 9,                                                                      // Title block lines and texts, view labels
};

enum class SheetTextAnchor : uint8_t
{
    Start  = 0,                                                                              // Position is the left end of the baseline
    Middle = 1,                                                                              // Position is the middle of the baseline
    End    = 2,                                                                              // Position is the right end of the baseline
};

// 📝 Sheet coordinates are millimetres from the paper's lower-left corner, 𝑦 up.
struct SheetPolyline
{
    SheetLayer           Layer  = SheetLayer::Visible;                                       // [-]  - Style
    std::vector<Vector2> Points = {};                                                        // [mm] - Open polyline
};

struct SheetText
{
    SheetLayer      Layer    = SheetLayer::Title;                                            // [-]   - Style
    Vector2         Position = {};                                                           // [mm]  - Anchor on the baseline
    double          Height   = 3.5;                                                          // [mm]  - Cap height
    double          Angle    = 0.0;                                                          // [rad] - Baseline direction, counter-clockwise
    SheetTextAnchor Anchor   = SheetTextAnchor::Start;                                       // [-]   - Which baseline point Position is
    std::string     Content  = {};                                                           // [-]   - UTF-8 (⌀ allowed)
};

struct SheetSolid                                                                            // Filled triangle (arrowheads)
{
    SheetLayer             Layer   = SheetLayer::Dimension;                                  // [-]  - Style
    std::array<Vector2, 3> Corners = {};                                                     // [mm] - Tip first
};

struct DrawingTitleBlock
{
    std::string Title         = "Untitled";                                                  // [-] - Part name
    std::string DrawingNumber = "DWG-0001";                                                  // [-] - Identifier
    std::string Author        = "";                                                          // [-] - Drawn by
    std::string Date          = "";                                                          // [-] - Issue date
    std::string Material      = "";                                                          // [-] - Material
    std::string Revision      = "A";                                                         // [-] - Revision letter
    std::string Company       = "Frontier";                                                  // [-] - Organisation
};

enum class DrawingDimensionCategory : uint8_t
{
    Horizontal = 0,                                                                          // Along the sheet 𝑥
    Vertical   = 1,                                                                          // Along the sheet 𝑦
    Diameter   = 2,                                                                          // ⌀ leader to a full circle
    Radius     = 3,                                                                          // R leader to an arc
};

// 📝 Feature points and Value come from the projected geometry (exact); the rest is placement.
struct DrawingDimension
{
    DrawingDimensionCategory Kind         = DrawingDimensionCategory::Horizontal;                    // [-]   - Kind
    int                  View         = 0;                                                   // [-]   - Index into DrawingSheet::Views
    double               Value        = 0.0;                                                 // [len] - Model length (× ModelUnitMillimetres in the text)
    std::string          Text         = {};                                                  // [-]   - "40", "⌀6", "R3"
    Vector2              First        = {};                                                  // [mm]  - Feature point (circle / arc centre for leaders)
    Vector2              Second       = {};                                                  // [mm]  - Feature point (leaders: arrow tip on the curve)
    Vector2              LineStart    = {};                                                  // [mm]  - Dimension line between the arrow tips (leaders: tip → knee)
    Vector2              LineEnd      = {};                                                  // [mm]
    Vector2              TextLow      = {};                                                  // [mm]  - Value box
    Vector2              TextHigh     = {};                                                  // [mm]
};

struct PlacedDrawingView
{
    std::string Label         = {};                                                          // [-]  - "Front", "Top", "Right", "Isometric", "Section A-A"
    DrawingView View          = {};                                                          // [-]  - Projection (model view coordinates)
    Vector2     Origin        = {};                                                          // [mm] - Sheet point of the view-frame origin
    Vector2     Low           = {};                                                          // [mm] - Sheet box of the strokes
    Vector2     High          = {};                                                          // [mm]
    Vector2     FootprintLow  = {};                                                          // [mm] - Box with its dimensions and label
    Vector2     FootprintHigh = {};                                                          // [mm]
};

struct DrawingSheetRequest
{
    DrawingPaperSize                Paper                = DrawingPaperSize::A3;              // [-]  - Paper
    DrawingProjectionAngle          Angle                = DrawingProjectionAngle::Third;     // [-]  - Layout and symbol
    double                          Scale                = 0.0;                               // [-]  - Paper : model, 0 → largest standard scale that fits
    double                          ModelUnitMillimetres = 1.0;                               // [mm] - Length of one model unit
    bool                            IsometricEnabled     = true;                              // [-]  - Isometric in the free quadrant (unless a section takes it)
    std::optional<OrthonormalBasis> Section              = {};                                // [-]  - Section A-A (material on −ZAxis kept, seen from +ZAxis)
    bool                            HiddenLinesEnabled   = true;                              // [-]  - Dashed hidden lines
    bool                            TangentEdgesEnabled  = true;                              // [-]  - Thin tangent edges
    bool                            DimensionsEnabled    = true;                              // [-]  - Automatic dimensions and centre marks
    double                          ViewGap              = 12.0;                              // [mm] - Clear space between view footprints
    double                          TextHeight           = 3.5;                               // [mm] - Dimension text
    double                          HatchSpacing         = 3.0;                               // [mm] - On paper
    DrawingTitleBlock               Title                = {};                                // [-]  - Title block fields
};

struct DrawingSheet
{
    double                         Width        = 0.0;                                       // [mm] - Paper
    double                         Height       = 0.0;                                       // [mm]
    double                         Scale        = 0.0;                                       // [-]  - Paper : model
    std::string                    ScaleLabel   = {};                                        // [-]  - "2:1", "1:5"
    DrawingProjectionAngle         Angle        = DrawingProjectionAngle::Third;             // [-]
    Vector2                        BorderLow    = {};                                        // [mm] - Frame
    Vector2                        BorderHigh   = {};                                        // [mm]
    Vector2                        TitleLow     = {};                                        // [mm] - Title block
    Vector2                        TitleHigh    = {};                                        // [mm]
    Vector2                        AreaLow      = {};                                        // [mm] - Space for views
    Vector2                        AreaHigh     = {};                                        // [mm]
    std::vector<PlacedDrawingView> Views        = {};                                        // [-]  - Front, Top, Right, then Isometric / Section
    std::vector<DrawingDimension>  Dimensions   = {};                                        // [-]
    std::vector<SheetPolyline>     Lines        = {};                                        // [-]  - Everything drawn, in drawing order
    std::vector<SheetText>         Texts        = {};                                        // [-]
    std::vector<SheetSolid>        Solids       = {};                                        // [-]
    double                         Milliseconds = 0.0;                                       // [ms]
    std::string                    Refusal      = {};                                        // [-]  - Empty on success
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Vector2     DrawingPaperDimensions(DrawingPaperSize Paper) noexcept;          // [mm] landscape
[[nodiscard]] const char* DrawingPaperLabel(DrawingPaperSize Paper) noexcept;
[[nodiscard]] const char* SheetLayerLabel(SheetLayer Layer) noexcept;                       // "VISIBLE", "HIDDEN", …
[[nodiscard]] std::string FormatDrawingScale(double Scale);                                 // 2 → "2:1", 0.2 → "1:5"
[[nodiscard]] std::string FormatDimensionValue(double Value);                               // ≤ 3 decimals, trailing zeros dropped
[[nodiscard]] std::vector<double> EnumerateStandardScales();                                // ISO 5455, largest first

// 💡 Composition: ① Front, Top and Right views (plus the isometric, or section A-A, in the free quadrant) projected once;
//    ② for each standard scale, largest first (or the requested one): automatic dimensions in view-local millimetres
//    (overall sizes from the extreme outline points, hole positions from the left / bottom outline, ⌀ / R leaders to
//    circles and arcs fitted exactly from the strokes), view footprints, and the layout — Top shares Front's sheet 𝑥
//    mapping and Right shares its 𝑦 mapping exactly, placed by the projection angle — centred in the drawing area; the
//    first scale whose footprints fit wins; ③ the section view is re-projected with the hatch spacing on paper, the
//    cutting-plane line is traced in the view where the plane is edge-on; ④ border, title block (ISO 7200 layout,
//    180 mm wide) and the projection symbol. Refused for no bodies, a non-positive unit or scale, or views that do
//    not fit the paper at the requested scale (or at 1:1000).
[[nodiscard]] DrawingSheet ComposeDrawingSheet(const ModelPartition& Partition, const std::vector<BodyToken>& Bodies, const DrawingSheetRequest& Request);

} // namespace Frontier::GeometrySpace::Parametric
