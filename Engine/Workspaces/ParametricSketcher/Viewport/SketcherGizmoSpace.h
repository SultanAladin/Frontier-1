/*==========================================================================================================================================
                                                                GIZMOSPACE.H
==========================================================================================================================================*/
// 🧩 GizmoSpace, the world the transform gizmo lives in, one for every workspace (Sketcher, the Frontier editor and the
//    games): the Slate GizmoPRO handles as CPU-built triangles, the pick on those same triangles and the drag maths.
//    Sketcher draws the triangles with its GPU program (SketcherGizmo.slang); the Frontier editor with GizmoRasterization.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/VectorAlgebra.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherOrbitCamera.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Mode bits: any combination (move + rotate, rotate + scale, all three …); 0 hides the gizmo.
constexpr uint32_t GizmoTranslate = 1u;                                                      // Cones + plane quads (+ centre ring: screen move)
constexpr uint32_t GizmoRotate    = 2u;                                                      // Arcs
constexpr uint32_t GizmoScale     = 4u;                                                      // Cylinders (+ centre ring: uniform, without move)

constexpr double GizmoPixels = 150.0;                                                        // [px] - Screen length of the unit axis L

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

enum class GizmoHandle : uint8_t
{
    None       = 0,
    TranslateX = 1,                                                                          // Cone on +x
    TranslateY = 2,
    TranslateZ = 3,
    PlaneYZ    = 4,                                                                          // Quad between y and z (normal x)
    PlaneXZ    = 5,
    PlaneXY    = 6,
    RotateX    = 7,                                                                          // Arc in the y–z plane (about x)
    RotateY    = 8,
    RotateZ    = 9,
    ScaleX     = 10,                                                                         // Cylinder on +x
    ScaleY     = 11,
    ScaleZ     = 12,
    Centre     = 13,                                                                         // White ring facing the eye
};

// 📝 32-byte vertex of Sketcher's gizmo program (UI/Sketcher/Shaders/SketcherGizmo.slang). Normal (0, 0, 0) = unlit
//    (quads, arcs, ring, edge strokes); Colour is RGBA8 (R in the low byte) read as UNORM; Handle is the owning handle.
struct GizmoVertex
{
    float    Position[3] = {};                                                               // [len] - World point
    float    Normal[3]   = {};                                                               // [-]   - Shading normal (0: unlit)
    uint32_t Colour      = 0;                                                                // [-]   - RGBA8
    uint32_t Handle      = 0;                                                                // [-]   - GizmoHandle
};
static_assert(sizeof(GizmoVertex) == 32, "GizmoVertex must stay 32 bytes");

struct GizmoPose
{
    GeometrySpace::Parametric::Vector3 Origin  = {};                                                // [len] - Gizmo centre (pivot)
    GeometrySpace::Parametric::Vector3 Axes[3] = { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }; // [-] - Right-handed unit frame
    double                      Size    = 1.0;                                               // [len] - World length of L (constant on screen)
    uint32_t                    Modes   = 0;                                                 // [-]   - GizmoTranslate | GizmoRotate | GizmoScale
    uint32_t                    Hidden  = 0;                                                 // [-]   - Bit (1 << handle): handle left out
};

// 📝 The CPU copy of everything the GPU draws: the same triangles serve the pick, so what is seen is what is grabbed.
struct GizmoGeometry
{
    std::vector<GizmoVertex> Vertices    = {};                                               // [-] - Triangle list: opaque first, then translucent
    uint32_t                 OpaqueCount = 0;                                                // [-] - Vertices before the translucent quads
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The camera of a perspective view given by its eye, forward and up directions (any up axis: Sketcher is y up, the
//    Frontier level z up) over a Width × Height pixel viewport. WorldFromClip stays empty: the gizmo never casts rays from it.
[[nodiscard]] OrbitCameraBasis ResolveGizmoCamera(const std::array<double, 3>& Eye, const std::array<double, 3>& Forward,
                                                 const std::array<double, 3>& Up, double VerticalFieldOfView, double Width, double Height);

// World length that spans Pixels on screen at Origin's depth (perspective or orthographic).
[[nodiscard]] double MeasureGizmoSize(const OrbitCameraBasis& Frame, GeometrySpace::Parametric::Vector3 Origin, double Pixels);

// Handle triangles in world space: Hover / Grabbed glow (Slate emissive #555555), the ring billboarded to Frame.
[[nodiscard]] GizmoGeometry BuildGizmoGeometry(const GizmoPose& Pose, const OrbitCameraBasis& Frame, GizmoHandle Hover, GizmoHandle Grabbed);

// Handle under the viewport pixel (local, top-left origin): inside a projected triangle or within Reach pixels of one
// (inside first, opaque handles before see-through plane quads, then nearest); None when the pointer misses the gizmo.
[[nodiscard]] GizmoHandle PickGizmoHandle(const GizmoGeometry& Geometry, const OrbitCameraBasis& Frame, double PixelX, double PixelY, double Reach);

// Axis index 0 / 1 / 2 a handle acts along / about (−1: plane-free or centre).
[[nodiscard]] int GizmoHandleAxis(GizmoHandle Handle) noexcept;
[[nodiscard]] const char* GizmoHandleLabel(GizmoHandle Handle) noexcept;

// 💡 Drag maths (Slate GizmoPRO): the axis parameter of the point on Origin + t·Axis closest to the pointer ray (skew
//    lines; empty when they run parallel), the ray / plane hit, and the angle of that hit about Axis measured from U.
[[nodiscard]] std::optional<double> MeasureAxisParameter(const OrbitRay& Ray, GeometrySpace::Parametric::Vector3 Origin, GeometrySpace::Parametric::Vector3 Axis);
[[nodiscard]] std::optional<GeometrySpace::Parametric::Vector3> IntersectGizmoPlane(const OrbitRay& Ray, GeometrySpace::Parametric::Vector3 Origin,
                                                                             GeometrySpace::Parametric::Vector3 Normal);
[[nodiscard]] std::optional<double> MeasureGizmoAngle(const OrbitRay& Ray, GeometrySpace::Parametric::Vector3 Origin, GeometrySpace::Parametric::Vector3 Axis,
                                                      GeometrySpace::Parametric::Vector3 Reference);

} // namespace Frontier
