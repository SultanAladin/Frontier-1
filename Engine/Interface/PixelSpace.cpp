//============================================================================================================================================
//                                                      PIXELSPACE.CPP
//============================================================================================================================================
// 🧩 Immediate-mode adapter for PixelSpace primitives. Everything above hands in pixels and colours.

#include "PixelSpace.h"

#include <cfloat>

#include <algorithm>

#include <imgui.h>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace {

ImDrawList* List(void* Slot) noexcept
{
    return static_cast<ImDrawList*>(Slot);
}

ImU32 Pack(ColorQuad Colour) noexcept
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(Colour.Red, Colour.Green, Colour.Blue, Colour.Alpha));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

PixelSpace::PixelSpace() noexcept
    : Commands(nullptr)
    , DisplayWidth(0.0f)
    , DisplayHeight(0.0f)
    , Scale(1.0f)
    , TypefaceStack{}
    , TypefaceDepth(0u)
{
}

bool PixelSpace::Begin(SurfaceLayer Layer, float InDisplayWidth, float InDisplayHeight, float InterfaceScale) noexcept
{
    Scale         = InterfaceScale > 0.05f ? InterfaceScale : 1.0f;
    DisplayWidth  = InDisplayWidth  / Scale;
    DisplayHeight = InDisplayHeight / Scale;
    TypefaceDepth = 0u;

    if (ImGui::GetCurrentContext() == nullptr)
    {
        Commands = nullptr;
        return false;
    }

    // The foreground list sits in front of every ImGui window, so the notch and its shade cover the
    //    project's own panels when pulled down — exactly what a system overlay should do.
    if (Layer == SurfaceLayer::Window)
    {
        // GetWindowDrawList() is only meaningful inside a Begin/End pair. Outside one it returns the list of
        //    whatever window happened to be current last, which would paint this frame's panel into an unrelated
        //    window — a failure that looks like corruption rather than a misuse, so it is refused here.
        // ImGui::GetWindowDrawList() asserts rather than returning null when no window is current, so the guard
        //    has to be a query that is safe outside a Begin/End pair. A zero-size window region is the public
        //    signal that there is no current window.
        const ImVec2 Region = ImGui::GetContentRegionAvail();
        if (Region.x == 0.0f && Region.y == 0.0f)
        {
            Commands = nullptr;
            return false;
        }
        Commands = static_cast<void*>(ImGui::GetWindowDrawList());
        return true;
    }

    Commands = (Layer == SurfaceLayer::Above)
             ? static_cast<void*>(ImGui::GetForegroundDrawList())
             : static_cast<void*>(ImGui::GetBackgroundDrawList());
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

void PixelSpace::FillRectangle(const PlaneExtent& Extent, ColorQuad Colour, float Radius) noexcept
{
    if (!Commands) return;
    List(Commands)->AddRectFilled(ImVec2(Extent.MinimumX * Scale, Extent.MinimumY * Scale),
                                  ImVec2(Extent.MaximumX * Scale, Extent.MaximumY * Scale),
                                  Pack(Colour), Radius * Scale);
}

void PixelSpace::FillRectangleBottomRounded(const PlaneExtent& Extent, ColorQuad Colour, float Radius) noexcept
{
    if (!Commands) return;
    List(Commands)->AddRectFilled(ImVec2(Extent.MinimumX * Scale, Extent.MinimumY * Scale),
                                  ImVec2(Extent.MaximumX * Scale, Extent.MaximumY * Scale),
                                  Pack(Colour), Radius * Scale, ImDrawFlags_RoundCornersBottom);
}

void PixelSpace::FillPolygon(const PlanePoint* Points, uint32_t PointCount, ColorQuad Colour) noexcept
{
    if (!Commands || PointCount < 3u) return;

    // ImGui's AddConvexPolyFilled produces artefacts on concave outlines; the notch is concave (it narrows
    //    toward the bottom), so the concave-capable path is used. It expects ImVec2 storage.
    std::vector<ImVec2> Converted;
    Converted.reserve(PointCount);
    for (uint32_t Index = 0u; Index < PointCount; ++Index)
        Converted.emplace_back(Points[Index].X * Scale, Points[Index].Y * Scale);

#if IMGUI_VERSION_NUM >= 19100
    List(Commands)->AddConcavePolyFilled(Converted.data(), static_cast<int>(Converted.size()), Pack(Colour));
#else
    List(Commands)->AddConvexPolyFilled(Converted.data(), static_cast<int>(Converted.size()), Pack(Colour));
#endif
}

void PixelSpace::StrokePolyline(const PlanePoint* Points, uint32_t PointCount, ColorQuad Colour, float Thickness, bool Closed) noexcept
{
    if (!Commands || PointCount < 2u) return;

    std::vector<ImVec2> Converted;
    Converted.reserve(PointCount);
    for (uint32_t Index = 0u; Index < PointCount; ++Index)
        Converted.emplace_back(Points[Index].X * Scale, Points[Index].Y * Scale);

    List(Commands)->AddPolyline(Converted.data(), static_cast<int>(Converted.size()), Pack(Colour),
                                Closed ? ImDrawFlags_Closed : ImDrawFlags_None, Thickness * Scale);
}

void PixelSpace::Text(float X, float Y, ColorQuad Colour, const char* Utf8, float FontSizePixels) noexcept
{
    if (!Commands || !Utf8) return;
    ImFont* Font = QueryTypeface() ? static_cast<ImFont*>(QueryTypeface()) : ImGui::GetFont();
    const float Size = FontSizePixels > 0.0f ? FontSizePixels : ImGui::GetFontSize();
    List(Commands)->AddText(Font, Size * Scale, ImVec2(X * Scale, Y * Scale), Pack(Colour), Utf8);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       CLIPPING
//------------------------------------------------------------------------------------------------------------------------

void PixelSpace::PushClip(const PlaneExtent& Extent) noexcept
{
    if (!Commands) return;
    List(Commands)->PushClipRect(ImVec2(Extent.MinimumX * Scale, Extent.MinimumY * Scale), ImVec2(Extent.MaximumX * Scale, Extent.MaximumY * Scale), true);
}

void PixelSpace::PopClip() noexcept
{
    if (!Commands) return;
    List(Commands)->PopClipRect();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       TYPEFACE
//------------------------------------------------------------------------------------------------------------------------

void PixelSpace::PushTypeface(void* FaceHandle) noexcept
{
    if (TypefaceDepth < 8u) TypefaceStack[TypefaceDepth] = FaceHandle;
    ++TypefaceDepth;   // over-deep pushes are counted so the matching pops balance
}

void PixelSpace::PopTypeface() noexcept
{
    if (TypefaceDepth > 0u) --TypefaceDepth;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        GROUPS
//------------------------------------------------------------------------------------------------------------------------

uint32_t PixelSpace::BeginGroup() const noexcept
{
    if (!Commands) return 0u;
    return static_cast<uint32_t>(List(Commands)->VtxBuffer.Size);
}

void PixelSpace::EndGroup(uint32_t Mark, float OffsetX, float OffsetY, float GroupScale, float PivotX, float PivotY, float Alpha) noexcept
{
    if (!Commands) return;
    ImDrawList* Draw = List(Commands);
    const int End = Draw->VtxBuffer.Size;
    const float A = std::clamp(Alpha, 0.0f, 1.0f);
    // Vertices are already physical; the group parameters arrive in logical pixels.
    const float Px = PivotX * this->Scale, Py = PivotY * this->Scale, Ox = OffsetX * this->Scale, Oy = OffsetY * this->Scale;
    for (int Index = static_cast<int>(Mark); Index < End; ++Index)
    {
        ImDrawVert& Vertex = Draw->VtxBuffer[Index];
        Vertex.pos.x = Px + (Vertex.pos.x - Px) * GroupScale + Ox;
        Vertex.pos.y = Py + (Vertex.pos.y - Py) * GroupScale + Oy;
        if (A < 1.0f)
        {
            const ImU32 Colour = Vertex.col;
            const ImU32 Faded  = static_cast<ImU32>(static_cast<float>((Colour >> IM_COL32_A_SHIFT) & 0xFFu) * A + 0.5f);
            Vertex.col = (Colour & ~IM_COL32_A_MASK) | (Faded << IM_COL32_A_SHIFT);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     MEASUREMENT
//------------------------------------------------------------------------------------------------------------------------

PlanePoint PixelSpace::MeasureText(const char* Utf8, float FontSizePixels) const noexcept
{
    if (ImGui::GetCurrentContext() == nullptr || !Utf8) return {};
    ImFont* Font = QueryTypeface() ? static_cast<ImFont*>(QueryTypeface()) : ImGui::GetFont();
    const float Size = FontSizePixels > 0.0f ? FontSizePixels : ImGui::GetFontSize();
    const ImVec2 Measured = Font->CalcTextSizeA(Size * Scale, FLT_MAX, 0.0f, Utf8);
    return PlanePoint{ Measured.x / Scale, Measured.y / Scale };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FLOATING PANEL
//------------------------------------------------------------------------------------------------------------------------

FloatingPanel::FloatingPanel(const char* Identity, float DefaultX, float DefaultY,
                             float DefaultWidth, float DefaultHeight, float InterfaceScale,
                             float MinimumWidth, float MinimumHeight) noexcept
    : Scale(InterfaceScale > 0.05f ? InterfaceScale : 1.0f)
{
    if (ImGui::GetCurrentContext() == nullptr) return;

    // Defaults are seeded once. ImGui restores the user's own position and size from imgui.ini afterwards, which
    //    is the whole reason a movable panel is worth having: the layout survives the session.
    ImGui::SetNextWindowPos (ImVec2(DefaultX * Scale, DefaultY * Scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(DefaultWidth * Scale, DefaultHeight * Scale), ImGuiCond_FirstUseEver);

    // ⚠️ Unlike the defaults above, the minimum applies EVERY frame, including to a size restored from imgui.ini.
    //    A panel that was dragged too narrow in a previous session would otherwise come back too narrow.
    if (MinimumWidth > 0.0f || MinimumHeight > 0.0f)
        ImGui::SetNextWindowSizeConstraints(ImVec2(MinimumWidth * Scale, MinimumHeight * Scale),
                                            ImVec2(FLT_MAX, FLT_MAX));

    // The panel paints its own background and its own padding, so ImGui must contribute neither. Without this
    //    the kit's card radius sits inside a second, square frame.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    StyleApplied = true;

    Open = ImGui::Begin(Identity, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Hover is sampled here, while this window is current. Asking after End() would answer for whatever window
    //    happened to be current next.
    WindowHovered  = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows
                                          | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    // 🔴 AllowWhenBlockedByActiveItem here too, and then a GEOMETRIC test below. Without the flag this asks a
    //    question ImGui answers about its own widgets: it reports "not hovered" the instant any item is active,
    //    and the invisible button that stops the window being dragged by its body becomes active on the very
    //    press the panel is trying to read. The whole panel went dead — a click was discarded on release, and a
    //    slider drag died one frame after it began — because the host gates its pointer on this answer.
    ContentHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows
                                          | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (Open)
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const ImVec2 Avail  = ImGui::GetContentRegionAvail();
        Content = PlaneExtent{ Origin.x / Scale, Origin.y / Scale,
                               (Origin.x + Avail.x) / Scale, (Origin.y + Avail.y) / Scale };

        // 🔴 Reserve the whole content region as one ImGui item. The panel's widgets are painted straight into
        //    this window's draw list, so ImGui does not know they exist: from its point of view the entire body
        //    is empty background, and pressing empty background is how a window is dragged. That is why moving a
        //    slider also moved the window — every drag was doing both at once, and the widget was the one that
        //    looked broken.
        //
        //    An invisible button is the fix rather than NoMove, because NoMove would also disable the title bar
        //    and the panel could then never be moved at all. This claims presses in the BODY and leaves the title
        //    bar, the resize grip and the dock tab doing exactly what they did.
        ImGui::SetCursorScreenPos(Origin);
        ImGui::InvisibleButton("##PanelContent", ImVec2(Avail.x > 1.0f ? Avail.x : 1.0f,
                                                        Avail.y > 1.0f ? Avail.y : 1.0f),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight
                             | ImGuiButtonFlags_MouseButtonMiddle);
        // Put the cursor back so the recording origin is the one the caller was told about.
        ImGui::SetCursorScreenPos(Origin);

        // ⚠️ And the content test is the RECTANGLE, not ImGui's notion of hovering. The title bar, the resize
        //    grip and the dock tab all sit outside this rectangle, which is exactly the distinction the caller
        //    needs, and it stays true whatever ImGui believes about which item is active.
        const ImVec2 Mouse = ImGui::GetIO().MousePos;
        ContentHovered = ContentHovered
                      && Mouse.x >= Origin.x && Mouse.x < Origin.x + Avail.x
                      && Mouse.y >= Origin.y && Mouse.y < Origin.y + Avail.y;

        // A collapsed or fully clipped window has no content to record into; reporting it as open would have the
        //    caller lay out against a zero rectangle.
        if (Avail.x <= 1.0f || Avail.y <= 1.0f) Open = false;
        else if (!Recording.Begin(SurfaceLayer::Window, Avail.x, Avail.y, Scale)) Open = false;
    }
}

FloatingPanel::~FloatingPanel() noexcept
{
    if (ImGui::GetCurrentContext() == nullptr) return;
    // End() pairs with Begin() unconditionally — ImGui requires it even when Begin returned false.
    ImGui::End();
    if (StyleApplied)
    {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
}

} // namespace Frontier
