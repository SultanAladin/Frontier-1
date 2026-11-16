/*============================================================================================================================================
                                                                 PIXELSPACE.H
============================================================================================================================================*/
// 🧩 Logical-to-physical 2D canvas surface for reusable engine UI controls, panels, and overlays

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <span>

struct ImDrawList;
struct ImFont;
struct ImTextureData;

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class InterfaceKey : uint8_t
{
    Escape    = 0, // [-] - Escape navigation key
    Enter     = 1, // [-] - Return / confirm key
    Backspace = 2, // [-] - Delete preceding character key
    DeleteKey = 3, // [-] - Delete following character key
    Left      = 4, // [-] - Left cursor arrow key
    Right     = 5, // [-] - Right cursor arrow key
    Up        = 6, // [-] - Up cursor arrow key
    Down      = 7, // [-] - Down cursor arrow key
    Home      = 8, // [-] - Line-start navigation key
    End       = 9, // [-] - Line-end navigation key
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ColorRgba
{
    uint8_t Red   = 0u;   // [-] - Red channel intensity [0..255]
    uint8_t Green = 0u;   // [-] - Green channel intensity [0..255]
    uint8_t Blue  = 0u;   // [-] - Blue channel intensity [0..255]
    uint8_t Alpha = 255u; // [-] - Opacity channel intensity [0..255]

    [[nodiscard]] constexpr ColorRgba        WithAlpha(uint8_t NewAlpha) const noexcept;
    [[nodiscard]] constexpr ColorRgba        ScaledAlpha(float Opacity) const noexcept;
    [[nodiscard]] static constexpr ColorRgba Blend(ColorRgba First, ColorRgba Second, float Factor) noexcept;
    [[nodiscard]] constexpr float            Luminance() const noexcept;
    [[nodiscard]] constexpr bool             operator==(const ColorRgba&) const noexcept = default;
};

struct Point2D
{
    float X = 0.0f; // [px] - Horizontal logical coordinate
    float Y = 0.0f; // [px] - Vertical logical coordinate
};

struct Rectangle2D
{
    float X      = 0.0f; // [px] - Left logical coordinate
    float Y      = 0.0f; // [px] - Top logical coordinate
    float Width  = 0.0f; // [px] - Horizontal logical extent
    float Height = 0.0f; // [px] - Vertical logical extent

    [[nodiscard]] constexpr float       Right() const noexcept;
    [[nodiscard]] constexpr float       Bottom() const noexcept;
    [[nodiscard]] constexpr Point2D     Center() const noexcept;
    [[nodiscard]] constexpr bool        Contains(Point2D Point) const noexcept;
    [[nodiscard]] constexpr Rectangle2D Inset(float Margin) const noexcept;
    [[nodiscard]] constexpr Rectangle2D InsetXY(float MarginX, float MarginY) const noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class PixelSpace
{
public:
    struct GroupMarker
    {
        int     FirstVertex = 0;               // [idx] - Starting canvas-buffer vertex offset
        Point2D Pivot       {};                // [px]  - Transform pivot in logical coordinates
        Point2D Scale       { 1.0f, 1.0f };    // [-]   - Horizontal and vertical scale factors
        Point2D Offset      {};                // [px]  - Post-pivot logical translation
        float   Alpha       = 1.0f;            // [0-1] - Group vertex alpha multiplier
    };

    PixelSpace(ImDrawList* CanvasTarget, Point2D SurfaceOrigin, float SurfaceScale = 1.0f, ImFont* DefaultTypeface = nullptr) noexcept;
    ~PixelSpace() noexcept;

    PixelSpace(const PixelSpace&)            = delete;
    PixelSpace& operator=(const PixelSpace&) = delete;

    [[nodiscard]] float       QueryScale() const noexcept;
    [[nodiscard]] Point2D     QueryOrigin() const noexcept;
    [[nodiscard]] ImDrawList* QueryCanvasBuffer() const noexcept;

    void FillRectangle(Rectangle2D Bounds, ColorRgba Fill, float CornerRadius = 0.0f) noexcept;
    void FillRectangleBottomRounded(Rectangle2D Bounds, ColorRgba Fill, float CornerRadius) noexcept;
    void FillTexturedRectangle(
        Rectangle2D    Bounds,
        ImTextureData* TextureSource,
        Point2D        UvMin,
        Point2D        UvMax,
        float          CornerRadius = 0.0f,
        ColorRgba      Modulation   = { 255u, 255u, 255u, 255u }) noexcept;
    void StrokeRectangle(Rectangle2D Bounds, ColorRgba Stroke, float CornerRadius = 0.0f, float Thickness = 1.0f) noexcept;
    void FillCircle(Point2D Center, float Radius, ColorRgba Fill) noexcept;
    void StrokeCircle(Point2D Center, float Radius, ColorRgba Stroke, float Thickness = 1.0f) noexcept;
    void FillPolygon(std::span<const Point2D> Vertices, ColorRgba Fill) noexcept;
    void FillTexturedRectangle(
        Rectangle2D Bounds,
        const void* SoftwareTextureData = nullptr,
        uint64_t    NativeTextureHandle = 0ull,
        ColorRgba   Tint                = { 255u, 255u, 255u, 255u }) noexcept;
    void StrokeLine(Point2D From, Point2D To, ColorRgba Stroke, float Thickness = 1.0f) noexcept;
    void StrokePolyline(std::span<const Point2D> Vertices, ColorRgba Stroke, float Thickness = 1.0f, bool Closed = false) noexcept;

    void                  PushTypeface(ImFont* Typeface) noexcept;
    void                  PopTypeface() noexcept;
    [[nodiscard]] ImFont* QueryTypeface() const noexcept;

    void Text(Point2D Position, float PixelSize, ColorRgba Ink, const char* Utf8, const char* Utf8End = nullptr) noexcept;
    void TextVerticalUp(Point2D BottomLeftAnchor, float PixelSize, ColorRgba Ink, const char* Utf8, const char* Utf8End = nullptr) noexcept;
    void TextCentered(Rectangle2D Bounds, float PixelSize, ColorRgba Ink, const char* Utf8) noexcept;
    void TextRightAligned(Rectangle2D Bounds, float PixelSize, ColorRgba Ink, const char* Utf8) noexcept;
    [[nodiscard]] float   MeasureTextWidth(float PixelSize, const char* Utf8, const char* Utf8End = nullptr) const noexcept;
    [[nodiscard]] Point2D MeasureText(float PixelSize, const char* Utf8, const char* Utf8End = nullptr) const noexcept;

    void PushClip(Rectangle2D Bounds, bool IntersectWithCurrent = true) noexcept;
    void PopClip() noexcept;

    [[nodiscard]] GroupMarker BeginGroup(Point2D Pivot, Point2D GroupScale, Point2D Offset, float Alpha = 1.0f) noexcept;
    void                      EndGroup(const GroupMarker& Marker) noexcept;

    [[nodiscard]] Point2D  QueryMousePosition() const noexcept;
    [[nodiscard]] bool     QueryMouseInside(Rectangle2D Bounds) const noexcept;
    [[nodiscard]] bool     QueryMouseDown(int Button = 0) const noexcept;
    [[nodiscard]] bool     QueryMouseClicked(int Button = 0) const noexcept;
    [[nodiscard]] bool     QueryMouseDoubleClicked(int Button = 0) const noexcept;
    [[nodiscard]] bool     QueryMouseReleased(int Button = 0) const noexcept;
    [[nodiscard]] float    QueryMouseWheelY() const noexcept;
    [[nodiscard]] bool     QueryShiftHeld() const noexcept;
    [[nodiscard]] bool     QueryCtrlHeld() const noexcept;
    [[nodiscard]] bool     QueryKeyPressed(InterfaceKey Key, bool Repeat = true) const noexcept;
    [[nodiscard]] uint32_t ReadTypedUtf8(char* Destination, uint32_t Capacity) const noexcept;

    [[nodiscard]] Point2D     ToPhysical(Point2D Logical) const noexcept;
    [[nodiscard]] Rectangle2D ToPhysical(Rectangle2D Logical) const noexcept;

private:
    ImDrawList* CanvasBuffer        = nullptr; // [-]     - Target Dear ImGui command buffer
    Point2D     Origin            {};        // [px]    - Physical origin offset
    float       Scale             = 1.0f;    // [-]     - Logical-to-physical DPI scale factor
    ImFont*     ActiveTypeface    = nullptr; // [-]     - Currently bound font face
    ImFont*     TypefaceStack[8]  {};        // [-]     - Nested pushed font faces
    uint32_t    TypefaceDepth     = 0u;      // [count] - Active typeface stack depth
    uint32_t    ClipDepth         = 0u;      // [count] - Active clip rectangle stack depth
    Rectangle2D ClipStack[16]     {};        // [px]    - Nested physical clip rectangles
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

constexpr ColorRgba ColorRgba::WithAlpha(uint8_t NewAlpha) const noexcept
{
    return { Red, Green, Blue, NewAlpha };
}

constexpr ColorRgba ColorRgba::ScaledAlpha(float Opacity) const noexcept
{
    const float Clamped = Opacity < 0.0f ? 0.0f : (Opacity > 1.0f ? 1.0f : Opacity);
    return { Red, Green, Blue, static_cast<uint8_t>(static_cast<float>(Alpha) * Clamped + 0.5f) };
}

constexpr ColorRgba ColorRgba::Blend(ColorRgba First, ColorRgba Second, float Factor) noexcept
{
    const float T  = Factor < 0.0f ? 0.0f : (Factor > 1.0f ? 1.0f : Factor);
    const float U  = 1.0f - T;
    auto        Ch = [U, T](uint8_t Left, uint8_t Right) constexpr noexcept -> uint8_t
    {
        return static_cast<uint8_t>(static_cast<float>(Left) * U + static_cast<float>(Right) * T + 0.5f);
    };
    return {
        Ch(First.Red, Second.Red),
        Ch(First.Green, Second.Green),
        Ch(First.Blue, Second.Blue),
        Ch(First.Alpha, Second.Alpha),
    };
}

constexpr float ColorRgba::Luminance() const noexcept
{
    return (0.2126f * static_cast<float>(Red) + 0.7152f * static_cast<float>(Green) + 0.0722f * static_cast<float>(Blue)) / 255.0f;
}

constexpr float Rectangle2D::Right() const noexcept
{
    return X + Width;
}

constexpr float Rectangle2D::Bottom() const noexcept
{
    return Y + Height;
}

constexpr Point2D Rectangle2D::Center() const noexcept
{
    return { X + Width * 0.5f, Y + Height * 0.5f };
}

constexpr bool Rectangle2D::Contains(Point2D Point) const noexcept
{
    return Point.X >= X && Point.X < X + Width && Point.Y >= Y && Point.Y < Y + Height;
}

constexpr Rectangle2D Rectangle2D::Inset(float Margin) const noexcept
{
    return {
        X + Margin,
        Y + Margin,
        Width > Margin * 2.0f ? Width - Margin * 2.0f : 0.0f,
        Height > Margin * 2.0f ? Height - Margin * 2.0f : 0.0f,
    };
}

constexpr Rectangle2D Rectangle2D::InsetXY(float MarginX, float MarginY) const noexcept
{
    return {
        X + MarginX,
        Y + MarginY,
        Width > MarginX * 2.0f ? Width - MarginX * 2.0f : 0.0f,
        Height > MarginY * 2.0f ? Height - MarginY * 2.0f : 0.0f,
    };
}

inline float PixelSpace::QueryScale() const noexcept
{
    return Scale;
}

inline Point2D PixelSpace::QueryOrigin() const noexcept
{
    return Origin;
}

inline ImDrawList* PixelSpace::QueryCanvasBuffer() const noexcept
{
    return CanvasBuffer;
}

inline ImFont* PixelSpace::QueryTypeface() const noexcept
{
    return ActiveTypeface;
}

} // namespace Frontier
