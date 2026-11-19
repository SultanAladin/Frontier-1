/*====================================================================================================================================
                                                      DRAWINGPROJECTION.H
====================================================================================================================================*/
// 🧩 Orthographic drawing views: projected edges, exact silhouettes, ray-cast hidden lines, section views with hatching

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Connectivity/ModelPartition.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The six principal views plus the isometric. View frames are right-handed: XAxis is sheet right, YAxis sheet up and
//    ZAxis points at the viewer (the viewing direction is −ZAxis).
enum class DrawingStandardView : uint8_t
{
    Front     = 0,                                                                           // Viewer at −y: x right, z up
    Back      = 1,                                                                           // Viewer at +y: −x right, z up
    Top       = 2,                                                                           // Viewer at +z: x right, y up
    Bottom    = 3,                                                                           // Viewer at −z: x right, −y up
    Right     = 4,                                                                           // Viewer at +x: y right, z up
    Left      = 5,                                                                           // Viewer at −x: −y right, z up
    Isometric = 6,                                                                           // Viewer along (1, −1, 1): z up
};

enum class DrawingStrokeCategory : uint8_t
{
    Edge           = 0,                                                                      // A model edge between faces meeting at an angle
    TangentEdge    = 1,                                                                      // A model edge between tangent faces (fillet run-out)
    Silhouette     = 2,                                                                      // Where a curved face turns away (N·view = 0)
    SectionOutline = 3,                                                                      // An edge of a section cap
    Hatch          = 4,                                                                      // Section hatching inside a cap
};

enum class DrawingVisibility : uint8_t
{
    Visible = 0,                                                                             // Nothing between it and the viewer
    Hidden  = 1,                                                                             // Behind material (dashed on the sheet)
};

// 📝 One polyline of a view in view coordinates (𝑥 = (P − O)·XAxis, 𝑦 = (P − O)·YAxis). Straight model edges carry their
//    end points only (plus the points where their visibility changes).
struct DrawingStroke
{
    DrawingStrokeCategory    Kind       = DrawingStrokeCategory::Edge;                               // [-]   - What drew it
    DrawingVisibility    Visibility = DrawingVisibility::Visible;                            // [-]   - Solid or dashed
    std::vector<Vector2> Points     = {};                                                    // [len] - View coordinates
    BodyToken            Body       = {};                                                    // [-]   - Body of the source entity
    uint32_t             Source     = 0;                                                     // [-]   - Edge slot (edges) or face slot (silhouette, hatch)
};

struct DrawingViewRequest
{
    OrthonormalBasis                Frame               = {};                                // [-]   - View frame (ZAxis at the viewer)
    bool                            HiddenLinesEnabled  = true;                              // [-]   - Keep hidden strokes (else dropped)
    bool                            TangentEdgesEnabled = true;                              // [-]   - Keep tangent edges
    std::optional<OrthonormalBasis> Section             = {};                                // [-]   - Cut plane: material on its −ZAxis side stays
    double                          HatchSpacing        = 0.0;                               // [len] - 0 → 2.5 % of the reach
    double                          HatchAngle          = 0.785398163397448;                 // [rad] - From the view 𝑥 axis (45°)
    double                          ChordStep           = 0.0;                               // [len] - Curve polyline step, 0 → 0.5 % of the reach
    int                             SilhouetteDensity   = 64;                                // [-]   - Contour grid cells per face direction
};

struct DrawingView
{
    OrthonormalBasis           Frame             = {};                                       // [-]    - View frame used
    std::vector<DrawingStroke> Strokes           = {};                                       // [-]    - All strokes
    Vector2                    Low               = {};                                       // [len]  - View-space box
    Vector2                    High              = {};                                       // [len]  - View-space box
    double                     Reach             = 0.0;                                      // [len]  - Model radius (scale of the defaults)
    double                     SectionArea       = 0.0;                                      // [len²] - Σ cap areas (section views)
    int                        SectionCaps       = 0;                                        // [-]    - Cap faces hatched
    long                       VisibilityProbes  = 0;                                        // [-]    - Rays cast toward the viewer
    double                     Milliseconds      = 0.0;                                      // [ms]   - Projection time
    std::string                Refusal           = {};                                       // [-]    - Empty on success
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] OrthonormalBasis StandardDrawingFrame(DrawingStandardView View) noexcept;
[[nodiscard]] const char*      DrawingStandardViewLabel(DrawingStandardView View) noexcept;
[[nodiscard]] const char*      DrawingStrokeKindLabel(DrawingStrokeCategory Kind) noexcept;
[[nodiscard]] Vector2          ProjectDrawingPoint(const OrthonormalBasis& Frame, Vector3 Position) noexcept;
[[nodiscard]] double           MeasureStrokeLength(const DrawingStroke& Stroke) noexcept;

// 💡 Hidden-line projection (research: Appel 1967 quantitative invisibility, here by exact rays):
//    ① strokes: every model edge except seams (tangent edges flagged by the face normals at the midpoint), plus the
//       silhouette of each curved face — the zero set of N(𝑢, 𝑣)·ZAxis traced by marching squares on the face chart,
//       every contour point bisected onto N·ZAxis = 0, then trimmed to the face by its exact chart winding number;
//    ② visibility: a ray from the stroke point toward the viewer against every face whose view box covers it, through
//       IntersectCurveSurface (exact for every carrier) and the face's chart classification; touching and boundary hits
//       do not hide. Changes of visibility are bisected along the stroke's own parameter;
//    ③ section views: each body is cut on a scratch partition (CutBodyAtPlane, material on −Section.ZAxis kept); cap
//       edges become section outlines and caps are hatched with parallel lines clipped by the even-odd rule.
//    Refused for no bodies, a view frame that is not orthonormal, or a section plane that misses every body.
[[nodiscard]] DrawingView ProjectDrawingView(const ModelPartition& Partition, const std::vector<BodyToken>& Bodies, const DrawingViewRequest& Request);

} // namespace Frontier::GeometrySpace::Parametric
