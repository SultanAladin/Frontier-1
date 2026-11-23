//============================================================================================================================================
//                                                       PIXELSPACE.H
//============================================================================================================================================
// 🧩 Primitives in, recorded draw commands out — the one seam between engine UI hosts and the immediate-mode backend.
//    Hosts (ControlCentreHost, future panels) speak only in pixels and colours; nothing above this header names ImGui.
//
// Coordinate convention: display pixels, origin top-left, +X right, +Y DOWN. This is a screen-space convention;
//    it does not impose a graphics API or a scene-space coordinate convention.

#pragma once

#include "ThemeStructure.h"
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     PLANE EXTENT
//------------------------------------------------------------------------------------------------------------------------

struct PlaneExtent
{
    float MinimumX = 0.0f;   // [px] leading (left) edge
    float MinimumY = 0.0f;   // [px] upper edge
    float MaximumX = 0.0f;   // [px] trailing (right) edge
    float MaximumY = 0.0f;   // [px] lower edge

    [[nodiscard]] constexpr float Width()  const noexcept { return MaximumX - MinimumX; }
    [[nodiscard]] constexpr float Height() const noexcept { return MaximumY - MinimumY; }
    [[nodiscard]] constexpr bool  Encloses(float X, float Y) const noexcept
    {
        return X >= MinimumX && X < MaximumX && Y >= MinimumY && Y < MaximumY;
    }
};

[[nodiscard]] constexpr PlaneExtent Spanning(float X, float Y, float Width, float Height) noexcept
{
    return PlaneExtent{ X, Y, X + Width, Y + Height };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      PLANE POINT
//------------------------------------------------------------------------------------------------------------------------

struct PlanePoint
{
    float X = 0.0f;          // [px]
    float Y = 0.0f;          // [px]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    LAYER SELECTION
//------------------------------------------------------------------------------------------------------------------------

enum class SurfaceLayer : uint32_t
{
    Beneath = 0u,            // behind every ImGui window (background list)
    Above   = 1u,            // in front of every ImGui window (foreground list) — overlays such as the notch
    // Inside the ImGui window that is current when Begin() is called. The fore/background lists are screen-space
    //    and belong to no window, which is why a host recorded onto them cannot be dragged, resized, docked or
    //    z-ordered: there is nothing under it to own those behaviours. Recording into the window's own list gives
    //    a panel every one of them for free, while its widgets keep drawing exactly as they do now.
    Window  = 2u
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   RECORDING SURFACE
//------------------------------------------------------------------------------------------------------------------------

class PixelSpace
{
public:
    PixelSpace() noexcept;
    ~PixelSpace() noexcept = default;

    PixelSpace(const PixelSpace&)            = delete;
    PixelSpace& operator=(const PixelSpace&) = delete;

    // Must be called once per frame after ImGui::NewFrame() and before ImGui::Render(); selects the draw list.
    //    Returns false when no ImGui context exists (e.g. headless proof generation without a backend).
    //    InterfaceScale (Display → UI Scale, 1.0 = 100 %): hosts record in LOGICAL pixels — QueryDisplayWidth/Height
    //    report physical ÷ scale — and every primitive is mapped to physical pixels on emission (positions, radii,
    //    stroke widths, font sizes, clip rectangles). Pointer input must be divided by the same factor by the caller.
    //    SurfaceLayer::Window additionally requires an ImGui window to be current — the caller wraps the record in
    //    ImGui::Begin/End. Begin() returns false if none is, rather than silently painting to a stale list.
    bool Begin(SurfaceLayer Layer, float DisplayWidth, float DisplayHeight, float InterfaceScale = 1.0f) noexcept;
    [[nodiscard]] float QueryInterfaceScale() const noexcept { return Scale; }

    // ── Primitives ───────────────────────────────────────────────────────────────────────────────────────────────────
    void FillRectangle (const PlaneExtent& Extent, ColorQuad Colour, float Radius = 0.0f) noexcept;
    void FillRectangleBottomRounded(const PlaneExtent& Extent, ColorQuad Colour, float Radius) noexcept;   // only the two lower corners rounded
    void FillPolygon   (const PlanePoint* Points, uint32_t PointCount, ColorQuad Colour) noexcept;   // convex or concave, anti-aliased
    void StrokePolyline(const PlanePoint* Points, uint32_t PointCount, ColorQuad Colour, float Thickness, bool Closed) noexcept;
    void Text          (float X, float Y, ColorQuad Colour, const char* Utf8, float FontSizePixels = 0.0f) noexcept;

    // ── Clipping ─────────────────────────────────────────────────────────────────────────────────────────────────────
    void PushClip(const PlaneExtent& Extent) noexcept;
    void PopClip() noexcept;

    // ── Groups ───────────────────────────────────────────────────────────────────────────────────────────────────────
    // A group is every primitive recorded between BeginGroup and EndGroup. EndGroup applies, in this order, a
    //    uniform scale about (PivotX, PivotY), a translation, and an alpha multiplier — the framer-motion
    //    "opacity / x / scale" triple that Notch animates on whole pages.
    [[nodiscard]] uint32_t BeginGroup() const noexcept;
    void EndGroup(uint32_t Mark, float OffsetX, float OffsetY, float GroupScale, float PivotX, float PivotY, float Alpha) noexcept;

    // ── Typeface ─────────────────────────────────────────────────────────────────────────────────────────────────────
    // Text / MeasureText use the face on top of this stack (nullptr → the backend default font). Handles come from
    //    TypefaceRegistry::QueryHandle. The stack is per-surface and reset by Begin().
    void PushTypeface(void* FaceHandle) noexcept;
    void PopTypeface() noexcept;
    [[nodiscard]] void* QueryTypeface() const noexcept { return TypefaceDepth > 0u ? TypefaceStack[TypefaceDepth - 1u] : nullptr; }

    // ── Measurement ──────────────────────────────────────────────────────────────────────────────────────────────────
    [[nodiscard]] PlanePoint MeasureText(const char* Utf8, float FontSizePixels = 0.0f) const noexcept;
    [[nodiscard]] float      QueryDisplayWidth()  const noexcept { return DisplayWidth;  }
    [[nodiscard]] float      QueryDisplayHeight() const noexcept { return DisplayHeight; }
    [[nodiscard]] bool       IsRecording()        const noexcept { return Commands != nullptr; }

private:
    void*   Commands;        // [-]  the backend draw list for the current frame (opaque above this seam)
    float   DisplayWidth;    // [px] logical
    float   DisplayHeight;   // [px] logical
    float   Scale;           // [-]  logical → physical multiplier
    void*   TypefaceStack[8];
    uint32_t TypefaceDepth;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    FLOATING PANEL
//------------------------------------------------------------------------------------------------------------------------

// A movable, resizable, dockable host window. This exists so a panel can be a real window without its owner
//    naming ImGui: the seam this header draws — hosts speak pixels and colours only — is what lets the whole
//    interface be retargeted later, and it would be undone by a single ImGui::Begin in project code.
//
//    Usage is a scope: construct, test IsOpen(), record through Surface(), let it destruct. The window is closed
//    on destruction whether or not it was visible, which is what ImGui requires.
class FloatingPanel
{
public:
    // Identity is the window title; the portion after "##" is ImGui's persistence key and never renders.
    // DefaultX/Y/Width/Height apply only the first time a layout is seen — after that the user's own position
    //    and size are restored from imgui.ini, which is the entire point of making this a window.
    //    MinimumWidth/Height are a floor on the user's own resize. A panel dragged narrower than its content can
    //    lay out has nowhere to put a row, and the result is controls painted outside their own card.
    FloatingPanel(const char* Identity, float DefaultX, float DefaultY, float DefaultWidth, float DefaultHeight,
                  float InterfaceScale, float MinimumWidth = 0.0f, float MinimumHeight = 0.0f) noexcept;
    ~FloatingPanel() noexcept;

    FloatingPanel(const FloatingPanel&)            = delete;
    FloatingPanel& operator=(const FloatingPanel&) = delete;

    [[nodiscard]] bool        IsOpen()      const noexcept { return Open; }
    [[nodiscard]] PixelSpace& Surface()           noexcept { return Recording; }
    [[nodiscard]] PlaneExtent ContentExtent() const noexcept { return Content; }

    // True while the cursor is inside the panel's own content, so the caller knows the pointer belongs to it.
    //    False over the title bar, the resize grip and the dock tab — ImGui owns those, and letting a widget
    //    also see them would move a slider while the window is being dragged.
    [[nodiscard]] bool PointerInsideContent() const noexcept { return ContentHovered; }
    // True while the cursor is anywhere over the window INCLUDING its chrome. This is the camera's gate.
    [[nodiscard]] bool PointerOverWindow()    const noexcept { return WindowHovered; }

private:
    PixelSpace  Recording;
    PlaneExtent Content{};
    float       Scale          = 1.0f;
    bool        Open           = false;
    bool        ContentHovered = false;
    bool        WindowHovered  = false;
    bool        StyleApplied   = false;
};


} // namespace Frontier
