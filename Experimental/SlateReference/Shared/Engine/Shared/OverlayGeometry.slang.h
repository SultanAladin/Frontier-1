//============================================================================================================================================
//                                                         OVERLAYGEOMETRY.SLANG.H
//============================================================================================================================================
// 🧩 The editor overlay's CPU-side geometry — compact screen-space primitives the
//    GPU overlay pass draws in its own pass.
//
//    🔴 WHY THIS EXISTS. The grid, the gizmo and (later) wireframe overlays used
//       to be recorded through the interface's ImGui draw lists, where every
//       polyline was tessellated on the CPU into anti-aliased triangles — a few
//       thousand segments bog the frame down, and the premultiplied blend washes
//       low-alpha colours out over a bright sky. These records are the SAME data
//       drawn on the GPU: the panel fills this record (cheap, a few hundred
//       primitives), the host uploads it when its generation changes, and the
//       overlay pass expands lines and dots in the VERTEX SHADER and blends
//       straight alpha, so the colours stay vivid and the CPU never tessellates.
//
//    The record lives in `Shared/` because two layers name it: `SlateUI`'s scene
//    directory fills it and `SlateVulkan`'s overlay pass consumes it, and neither
//    may name the other. It is compiled by both the host and the shader
//    toolchains, so it carries `SLATE_SHARED` and no engine dependency beyond
//    the prelude.

#pragma once

#include "Shared/ToolchainInterchange.slang.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One line segment, in display pixels, with its colour and thickness.
/// note  📐 The colour is packed 0xAARRGGBB with STRAIGHT alpha: the GPU pass blends
///        `src_alpha / one_minus_src_alpha`, so a low-alpha line over a bright sky
///        keeps its hue instead of the washed-out premultiplied read.
/// tag   guarantee, nonallocating, nonthrowing
struct OverlayLine
{
    float          X0 = 0.0f;        // [px] - leading endpoint
    float          Y0 = 0.0f;        // [px]
    float          X1 = 0.0f;        // [px] - trailing endpoint
    float          Y1 = 0.0f;        // [px]
    Unsigned32  Packed = 0xFFFFFFFFu;   // [-] - 0xAARRGGBB
    float          Thickness = 1.0f;       // [px] - the line's width
};

/// 🧩 One dot marker, in display pixels, with its colour and radius.
/// tag   guarantee, nonallocating, nonthrowing
struct OverlayDot
{
    float          X = 0.0f;         // [px]
    float          Y = 0.0f;         // [px]
    Unsigned32  Packed = 0xFFFFFFFFu;   // [-] - 0xAARRGGBB
    float          Radius = 1.0f;         // [px] - the dot's radius
};

/// 🧩 One filled triangle, in display pixels, with its colour.
/// tag   guarantee, nonallocating, nonthrowing
struct OverlayTriangle
{
    float          X0 = 0.0f;        // [px] - first vertex
    float          Y0 = 0.0f;        // [px]
    float          X1 = 0.0f;        // [px] - second vertex
    float          Y1 = 0.0f;        // [px]
    float          X2 = 0.0f;        // [px] - third vertex
    float          Y2 = 0.0f;        // [px]
    Unsigned32  Packed = 0xFFFFFFFFu;   // [-] - 0xAARRGGBB
};

/// 🧩 Packs four channels into the overlay's 0xAARRGGBB spelling.
/// cost  ✔️
/// tag   shared, nonallocating, nonthrowing
SLATE_SHARED Unsigned32 PackOverlayColour(Unsigned32 Red, Unsigned32 Green,
                                             Unsigned32 Blue, Unsigned32 Alpha)
{
    return (Alpha << 24u) | (Red << 16u) | (Green << 8u) | Blue;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE RECORD
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One tick's overlay primitives, bounded and filled by the panel.
/// note  🔴 Bounded, never allocated: the panel fills inside a recording, and a ceiling is what keeps
///        the upload extent fixed at bring-up. The ceilings hold a dense lattice (65 x 65 lines),
///        a two-thousand-dot field and a thousand-triangle overlay; a caller that needs more grows the
///        ceilings here and the pass's buffer follows automatically.
/// note  📝 `Generation` is the host's upload key: the panel increments it whenever it fills the
///        record, and the host uploads only when it changed — the grid and gizmo are static between
///        camera or settings changes, so there is no per-frame upload.
/// tag   guarantee, nonallocating, nonthrowing
/// 🧩 The camera pose the analytic ground reads — mode 3 of the overlay pass.
/// note  📐 This is what replaced 1828 lines of CPU line-marching: the host hands
///        over ONE pose per leaf and the fragment stage solves the plane per
///        pixel. There is no segment budget to exhaust; an authored radial fade bounds presentation.
/// tag   guarantee, nonallocating, nonthrowing
struct OverlayGroundPose
{
    Real32  EyeX = 0.0f, EyeY = 1.5f, EyeZ = 0.0f;          // [m]
    Real32  ForwardX = 0.0f, ForwardY = 0.0f, ForwardZ = 1.0f;
    Real32  RightX = 1.0f, RightY = 0.0f, RightZ = 0.0f;
    Real32  UpX = 0.0f, UpY = 1.0f, UpZ = 0.0f;
    Real32  TanHalfH = 1.0f;    // [-] - the frustum's tangents
    Real32  TanHalfV = 0.577f;
    Real32  Cell = 1.0f;        // [m] - the minor grid cell
    Real32  LineWeight = 1.0f;  // [px]
    Real32  DotRadius = 2.0f;   // [px]
    Real32  Subdivisions = 10.0f; // [-] - major line every N minor cells
    Real32  ExtentMetres = 100.0f; // [m] - finite radius around world centre
    Real32  FadeRadiusMetres = 40.0f; // [m] - camera-relative sharp-to-absent radius
    Unsigned32 AxisMask = 7u;   // [-] - bit 0: X, bit 1: Y, bit 2: Z
    Unsigned32 ViewedOrientation = 0u; // [-] - ViewportOrientation, when the host knows it exactly
    Unsigned32 Presentation = 1u;   // [-] - PanelLatticePresentation
    // 🔴 The viewport footer's Ortho/Perspective choice. The fragment stage ray-marched a pinhole
    //    camera unconditionally, so the grid stayed in perspective while the geometry drawn on it
    //    went orthographic -- the shapes and the surface they sit on disagreed.
    Real32  OrthoScale = 0.0f;  // [px/unit] - zero means perspective; positive selects parallel rays
    bool    Standing = false;   // [-] - the leaf draws a ground at all
};

struct OverlayGeometry
{
    /// 📐 The analytic ground's pose. Reset does NOT clear it: the pose is the
    ///    camera, written once per leaf per tick, not an accumulated record.
    OverlayGroundPose Ground = {};

    static constexpr Unsigned32 LineLimit     = 1024u;   // [-] - line segments; a 128-cell lattice is 514
    static constexpr Unsigned32 DotLimit      = 2048u;   // [-] - dot markers
    static constexpr Unsigned32 TriangleLimit = 2048u;   // [-] - filled triangles; enough for the HTML-port torus gizmo plus selection overlays

    OverlayLine     Lines[LineLimit]         = {};
    Unsigned32   LineCount                  = 0u;
    OverlayDot      Dots[DotLimit]           = {};
    Unsigned32   DotCount                   = 0u;
    OverlayTriangle Triangles[TriangleLimit] = {};
    Unsigned32   TriangleCount              = 0u;
    Unsigned32   Generation                 = 0u;         // [-] - the host's upload key

    /// 🧩 Empties the record and marks it changed.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Reset()
    {
        LineCount     = 0u;
        DotCount      = 0u;
        TriangleCount = 0u;
        ++Generation;
    }

    /// 🧩 Appends one line segment; a full record declines silently.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void AddLine(float X0, float Y0, float X1, float Y1, Unsigned32 Packed, float Thickness = 1.0f)
    {
        if (LineCount >= LineLimit)
            return;

        OverlayLine& Written = Lines[LineCount++];
        Written.X0 = X0;
        Written.Y0 = Y0;
        Written.X1 = X1;
        Written.Y1 = Y1;
        Written.Packed = Packed;
        Written.Thickness = Thickness;
        ++Generation;
    }

    /// 🧩 Appends one dot marker; a full record declines silently.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void AddDot(float X, float Y, Unsigned32 Packed, float Radius = 1.0f)
    {
        if (DotCount >= DotLimit)
            return;

        OverlayDot& Written = Dots[DotCount++];
        Written.X = X;
        Written.Y = Y;
        Written.Packed = Packed;
        Written.Radius = Radius;
        ++Generation;
    }

    /// 🧩 Appends one filled triangle; a full record declines silently.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void AddTriangle(float X0, float Y0, float X1, float Y1, float X2, float Y2, Unsigned32 Packed)
    {
        if (TriangleCount >= TriangleLimit)
            return;

        OverlayTriangle& Written = Triangles[TriangleCount++];
        Written.X0 = X0;
        Written.Y0 = Y0;
        Written.X1 = X1;
        Written.Y1 = Y1;
        Written.X2 = X2;
        Written.Y2 = Y2;
        Written.Packed = Packed;
        ++Generation;
    }
};

} // namespace Slate
