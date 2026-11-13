/*============================================================================================================================================
                                                                PIXELSPACE.CPP
============================================================================================================================================*/
// 🧩 Logical-to-physical 2D canvas implementation over patched Dear ImGui construct lists.

#include "Engine/Components/PixelSpace.h"
#include "Engine/Input/InputExtension.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace Frontier
{

namespace
{

[[nodiscard]] constexpr ImU32 PackColor(ColorRgba Color) noexcept
{
    return IM_COL32(Color.Red, Color.Green, Color.Blue, Color.Alpha);
}

[[nodiscard]] Input::InputLogicalKey TranslateKey(InterfaceKey Key) noexcept
{
    switch (Key)
    {
    case InterfaceKey::Escape:    return Input::InputLogicalKey::Escape;
    case InterfaceKey::Enter:     return Input::InputLogicalKey::Enter;
    case InterfaceKey::Backspace: return Input::InputLogicalKey::Backspace;
    case InterfaceKey::DeleteKey: return Input::InputLogicalKey::Delete;
    case InterfaceKey::Left:      return Input::InputLogicalKey::LeftArrow;
    case InterfaceKey::Right:     return Input::InputLogicalKey::RightArrow;
    case InterfaceKey::Up:        return Input::InputLogicalKey::UpArrow;
    case InterfaceKey::Down:      return Input::InputLogicalKey::DownArrow;
    case InterfaceKey::Home:      return Input::InputLogicalKey::Home;
    case InterfaceKey::End:       return Input::InputLogicalKey::End;
    }
    return Input::InputLogicalKey::Unassigned;
}

uint32_t EncodeCodepointUtf8(uint32_t Codepoint, char *Out) noexcept
{
    if (Codepoint < 0x80u)
    {
        Out[0] = static_cast<char>(Codepoint);
        return 1u;
    }
    if (Codepoint < 0x800u)
    {
        Out[0] = static_cast<char>(0xC0u | (Codepoint >> 6));
        Out[1] = static_cast<char>(0x80u | (Codepoint & 0x3Fu));
        return 2u;
    }
    if (Codepoint < 0x10000u)
    {
        Out[0] = static_cast<char>(0xE0u | (Codepoint >> 12));
        Out[1] = static_cast<char>(0x80u | ((Codepoint >> 6) & 0x3Fu));
        Out[2] = static_cast<char>(0x80u | (Codepoint & 0x3Fu));
        return 3u;
    }
    if (Codepoint <= 0x10FFFFu)
    {
        Out[0] = static_cast<char>(0xF0u | (Codepoint >> 18));
        Out[1] = static_cast<char>(0x80u | ((Codepoint >> 12) & 0x3Fu));
        Out[2] = static_cast<char>(0x80u | ((Codepoint >> 6) & 0x3Fu));
        Out[3] = static_cast<char>(0x80u | (Codepoint & 0x3Fu));
        return 4u;
    }
    return 0u;
}

} // namespace

PixelSpace::PixelSpace(ImDrawList *CanvasTarget, Point2D SurfaceOrigin, float SurfaceScale, ImFont *DefaultTypeface) noexcept
    : CanvasBuffer(CanvasTarget)
    , Origin(SurfaceOrigin)
    , Scale(SurfaceScale > 0.01f ? SurfaceScale : 1.0f)
    , ActiveTypeface(DefaultTypeface ? DefaultTypeface : (ImGui::GetCurrentContext() ? ImGui::GetFont() : nullptr))
{
}

PixelSpace::~PixelSpace() noexcept
{
    while (ClipDepth > 0u)
        PopClip();
}

Point2D PixelSpace::ToPhysical(Point2D Logical) const noexcept
{
    return {Origin.X + Logical.X * Scale, Origin.Y + Logical.Y * Scale};
}

Rectangle2D PixelSpace::ToPhysical(Rectangle2D Logical) const noexcept
{
    return {Origin.X + Logical.X * Scale, Origin.Y + Logical.Y * Scale, Logical.Width * Scale, Logical.Height * Scale};
}

void PixelSpace::FillRectangle(Rectangle2D Bounds, ColorRgba Fill, float CornerRadius) noexcept
{
    if (!CanvasBuffer || Bounds.Width <= 0.0f || Bounds.Height <= 0.0f || Fill.Alpha == 0)
        return;
    const Rectangle2D Phys = ToPhysical(Bounds);
    const float       MaxR = 0.5f * std::min(Phys.Width, Phys.Height);
    const float       Rad  = std::clamp(CornerRadius * Scale, 0.0f, MaxR);
    CanvasBuffer->AddRectFilled(ImVec2(Phys.X, Phys.Y), ImVec2(Phys.Right(), Phys.Bottom()), PackColor(Fill), Rad);
}

void PixelSpace::FillRectangleBottomRounded(Rectangle2D Bounds, ColorRgba Fill, float CornerRadius) noexcept
{
    if (!CanvasBuffer || Bounds.Width <= 0.0f || Bounds.Height <= 0.0f || Fill.Alpha == 0)
        return;
    const Rectangle2D Phys = ToPhysical(Bounds);
    const float       MaxR = 0.5f * std::min(Phys.Width, Phys.Height);
    const float       Rad  = std::clamp(CornerRadius * Scale, 0.0f, MaxR);
    CanvasBuffer->AddRectFilled(ImVec2(Phys.X, Phys.Y), ImVec2(Phys.Right(), Phys.Bottom()), PackColor(Fill), Rad,
                            ImDrawFlags_RoundCornersBottom);
}

void PixelSpace::FillTexturedRectangle(
    Rectangle2D    Bounds,
    ImTextureData* TextureSource,
    Point2D        UvMin,
    Point2D        UvMax,
    float          CornerRadius,
    ColorRgba      Modulation) noexcept
{
    if (!CanvasBuffer || !TextureSource || Bounds.Width <= 0.0f || Bounds.Height <= 0.0f || Modulation.Alpha == 0)
        return;
    const Rectangle2D Phys = ToPhysical(Bounds);
    const float       MaxR = 0.5f * std::min(Phys.Width, Phys.Height);
    const float       Rad  = std::clamp(CornerRadius * Scale, 0.0f, MaxR);
    CanvasBuffer->AddImageRounded(
        TextureSource->GetTexRef(),
        ImVec2(Phys.X, Phys.Y),
        ImVec2(Phys.Right(), Phys.Bottom()),
        ImVec2(UvMin.X, UvMin.Y),
        ImVec2(UvMax.X, UvMax.Y),
        PackColor(Modulation),
        Rad);
}

void PixelSpace::StrokeRectangle(Rectangle2D Bounds, ColorRgba Stroke, float CornerRadius, float Thickness) noexcept
{
    if (!CanvasBuffer || Bounds.Width <= 0.0f || Bounds.Height <= 0.0f || Stroke.Alpha == 0 || Thickness <= 0.0f)
        return;
    const Rectangle2D Phys = ToPhysical(Bounds);
    const float       MaxR = 0.5f * std::min(Phys.Width, Phys.Height);
    const float       Rad  = std::clamp(CornerRadius * Scale, 0.0f, MaxR);
    CanvasBuffer->AddRect(ImVec2(Phys.X, Phys.Y), ImVec2(Phys.Right(), Phys.Bottom()), PackColor(Stroke), Rad, 0,
                      std::max(1.0f, Thickness * Scale));
}

void PixelSpace::FillCircle(Point2D Center, float Radius, ColorRgba Fill) noexcept
{
    if (!CanvasBuffer || Radius <= 0.0f || Fill.Alpha == 0)
        return;
    const Point2D Phys = ToPhysical(Center);
    CanvasBuffer->AddCircleFilled(ImVec2(Phys.X, Phys.Y), Radius * Scale, PackColor(Fill), 0);
}

void PixelSpace::StrokeCircle(Point2D Center, float Radius, ColorRgba Stroke, float Thickness) noexcept
{
    if (!CanvasBuffer || Radius <= 0.0f || Stroke.Alpha == 0 || Thickness <= 0.0f)
        return;
    const Point2D Phys = ToPhysical(Center);
    CanvasBuffer->AddCircle(ImVec2(Phys.X, Phys.Y), Radius * Scale, PackColor(Stroke), 0, std::max(1.0f, Thickness * Scale));
}

void PixelSpace::FillPolygon(std::span<const Point2D> Vertices, ColorRgba Fill) noexcept
{
    if (!CanvasBuffer || Vertices.size() < 3u || Fill.Alpha == 0)
        return;
    const ImU32 Packed = PackColor(Fill);
    if (Vertices.size() <= 128u)
    {
        ImVec2 StackPoints[128];
        for (std::size_t Index = 0; Index < Vertices.size(); ++Index)
        {
            const Point2D Phys = ToPhysical(Vertices[Index]);
            StackPoints[Index] = ImVec2(Phys.X, Phys.Y);
        }
        CanvasBuffer->AddConcavePolyFilled(StackPoints, static_cast<int>(Vertices.size()), Packed);
    }
    else
    {
        std::vector<ImVec2> HeapPoints(Vertices.size());
        for (std::size_t Index = 0; Index < Vertices.size(); ++Index)
        {
            const Point2D Phys = ToPhysical(Vertices[Index]);
            HeapPoints[Index] = ImVec2(Phys.X, Phys.Y);
        }
        CanvasBuffer->AddConcavePolyFilled(HeapPoints.data(), static_cast<int>(HeapPoints.size()), Packed);
    }
}

void PixelSpace::StrokeLine(Point2D From, Point2D To, ColorRgba Stroke, float Thickness) noexcept
{
    if (!CanvasBuffer || Stroke.Alpha == 0 || Thickness <= 0.0f)
        return;
    const Point2D A = ToPhysical(From);
    const Point2D B = ToPhysical(To);
    CanvasBuffer->AddLine(ImVec2(A.X, A.Y), ImVec2(B.X, B.Y), PackColor(Stroke), std::max(1.0f, Thickness * Scale));
}

void PixelSpace::StrokePolyline(std::span<const Point2D> Vertices, ColorRgba Stroke, float Thickness, bool Closed) noexcept
{
    if (!CanvasBuffer || Vertices.size() < 2u || Stroke.Alpha == 0 || Thickness <= 0.0f)
        return;
    const ImU32     Packed     = PackColor(Stroke);
    const ImDrawFlags Flags    = Closed ? ImDrawFlags_Closed : 0;
    const float     StrokePx   = std::max(1.0f, Thickness * Scale);
    if (Vertices.size() <= 128u)
    {
        ImVec2 StackPoints[128];
        for (std::size_t Index = 0; Index < Vertices.size(); ++Index)
        {
            const Point2D Phys = ToPhysical(Vertices[Index]);
            StackPoints[Index] = ImVec2(Phys.X, Phys.Y);
        }
        CanvasBuffer->AddPolyline(StackPoints, static_cast<int>(Vertices.size()), Packed, Flags, StrokePx);
    }
    else
    {
        std::vector<ImVec2> HeapPoints(Vertices.size());
        for (std::size_t Index = 0; Index < Vertices.size(); ++Index)
        {
            const Point2D Phys = ToPhysical(Vertices[Index]);
            HeapPoints[Index] = ImVec2(Phys.X, Phys.Y);
        }
        CanvasBuffer->AddPolyline(HeapPoints.data(), static_cast<int>(HeapPoints.size()), Packed, Flags, StrokePx);
    }
}

void PixelSpace::PushTypeface(ImFont *Typeface) noexcept
{
    if (TypefaceDepth < 8u)
    {
        TypefaceStack[TypefaceDepth++] = ActiveTypeface;
        if (Typeface)
            ActiveTypeface = Typeface;
    }
}

void PixelSpace::PopTypeface() noexcept
{
    if (TypefaceDepth > 0u)
        ActiveTypeface = TypefaceStack[--TypefaceDepth];
}

void PixelSpace::Text(Point2D Position, float PixelSize, ColorRgba Ink, const char *Utf8, const char *Utf8End) noexcept
{
    if (!CanvasBuffer || !Utf8 || Ink.Alpha == 0 || PixelSize <= 0.0f)
        return;
    ImFont *Font = ActiveTypeface ? ActiveTypeface : (ImGui::GetCurrentContext() ? ImGui::GetFont() : nullptr);
    if (!Font)
        return;
    const Point2D Phys = ToPhysical(Position);
    CanvasBuffer->AddText(Font, PixelSize * Scale, ImVec2(Phys.X, Phys.Y), PackColor(Ink), Utf8, Utf8End);
}

void PixelSpace::TextVerticalUp(
    Point2D     BottomLeftAnchor,
    float       PixelSize,
    ColorRgba   Ink,
    const char* Utf8,
    const char* Utf8End) noexcept
{
    if (!CanvasBuffer || !Utf8 || Ink.Alpha == 0 || PixelSize <= 0.0f)
    {
        return;
    }
    ImFont* Font = ActiveTypeface ? ActiveTypeface : (ImGui::GetCurrentContext() ? ImGui::GetFont() : nullptr);
    if (!Font)
    {
        return;
    }
    const Point2D Phys        = ToPhysical(BottomLeftAnchor);
    const int     StartVertex = CanvasBuffer->VtxBuffer.Size;
    CanvasBuffer->AddText(Font, PixelSize * Scale, ImVec2(Phys.X, Phys.Y), PackColor(Ink), Utf8, Utf8End);
    for (int Index = StartVertex; Index < CanvasBuffer->VtxBuffer.Size; ++Index)
    {
        const float Dx = CanvasBuffer->VtxBuffer[Index].pos.x - Phys.X;
        const float Dy = CanvasBuffer->VtxBuffer[Index].pos.y - Phys.Y;
        CanvasBuffer->VtxBuffer[Index].pos.x = Phys.X + Dy;
        CanvasBuffer->VtxBuffer[Index].pos.y = Phys.Y - Dx;
    }
}

void PixelSpace::TextCentered(Rectangle2D Bounds, float PixelSize, ColorRgba Ink, const char *Utf8) noexcept
{
    if (!Utf8)
        return;
    const Point2D Extent = MeasureText(PixelSize, Utf8);
    Text({Bounds.X + (Bounds.Width - Extent.X) * 0.5f, Bounds.Y + (Bounds.Height - Extent.Y) * 0.5f}, PixelSize, Ink, Utf8);
}

void PixelSpace::TextRightAligned(Rectangle2D Bounds, float PixelSize, ColorRgba Ink, const char *Utf8) noexcept
{
    if (!Utf8)
        return;
    const Point2D Extent = MeasureText(PixelSize, Utf8);
    Text({Bounds.Right() - Extent.X, Bounds.Y + (Bounds.Height - Extent.Y) * 0.5f}, PixelSize, Ink, Utf8);
}

float PixelSpace::MeasureTextWidth(float PixelSize, const char *Utf8, const char *Utf8End) const noexcept
{
    return MeasureText(PixelSize, Utf8, Utf8End).X;
}

Point2D PixelSpace::MeasureText(float PixelSize, const char *Utf8, const char *Utf8End) const noexcept
{
    if (!Utf8 || PixelSize <= 0.0f)
        return {};
    ImFont *Font = ActiveTypeface ? ActiveTypeface : (ImGui::GetCurrentContext() ? ImGui::GetFont() : nullptr);
    if (!Font)
        return {};
    const ImVec2 Raw = Font->CalcTextSizeA(PixelSize * Scale, 1.0e9f, 0.0f, Utf8, Utf8End);
    return {Raw.x / Scale, Raw.y / Scale};
}

void PixelSpace::PushClip(Rectangle2D Bounds, bool IntersectWithCurrent) noexcept
{
    if (!CanvasBuffer || ClipDepth >= 16u)
        return;
    Rectangle2D Effective = Bounds;
    if (IntersectWithCurrent && ClipDepth > 0u)
    {
        const Rectangle2D Prev = ClipStack[ClipDepth - 1u];
        const float       Left = std::max(Prev.X, Bounds.X);
        const float       Top  = std::max(Prev.Y, Bounds.Y);
        const float       R    = std::min(Prev.Right(), Bounds.Right());
        const float       B    = std::min(Prev.Bottom(), Bounds.Bottom());
        Effective              = {Left, Top, std::max(0.0f, R - Left), std::max(0.0f, B - Top)};
    }
    ClipStack[ClipDepth++] = Effective;
    const Rectangle2D Phys = ToPhysical(Effective);
    CanvasBuffer->PushClipRect(ImVec2(Phys.X, Phys.Y), ImVec2(Phys.Right(), Phys.Bottom()), IntersectWithCurrent);
}

void PixelSpace::PopClip() noexcept
{
    if (!CanvasBuffer || ClipDepth == 0u)
        return;
    --ClipDepth;
    CanvasBuffer->PopClipRect();
}

PixelSpace::GroupMarker PixelSpace::BeginGroup(Point2D Pivot, Point2D GroupScale, Point2D Offset, float Alpha) noexcept
{
    GroupMarker Marker{};
    Marker.FirstVertex = CanvasBuffer ? CanvasBuffer->VtxBuffer.Size : 0;
    Marker.Pivot       = ToPhysical(Pivot);
    Marker.Scale       = GroupScale;
    Marker.Offset      = {Offset.X * Scale, Offset.Y * Scale};
    Marker.Alpha       = std::clamp(Alpha, 0.0f, 1.0f);
    return Marker;
}

void PixelSpace::EndGroup(const GroupMarker &Marker) noexcept
{
    if (!CanvasBuffer)
        return;
    const bool TransformPos = Marker.Scale.X != 1.0f || Marker.Scale.Y != 1.0f || Marker.Offset.X != 0.0f || Marker.Offset.Y != 0.0f;
    const bool FadeAlpha    = Marker.Alpha < 0.999f;
    if (!TransformPos && !FadeAlpha)
        return;
    for (int Index = Marker.FirstVertex; Index < CanvasBuffer->VtxBuffer.Size; ++Index)
    {
        ImDrawVert &Vert = CanvasBuffer->VtxBuffer[Index];
        if (TransformPos)
        {
            Vert.pos.x = Marker.Pivot.X + (Vert.pos.x - Marker.Pivot.X) * Marker.Scale.X + Marker.Offset.X;
            Vert.pos.y = Marker.Pivot.Y + (Vert.pos.y - Marker.Pivot.Y) * Marker.Scale.Y + Marker.Offset.Y;
        }
        if (FadeAlpha)
        {
            const uint32_t OldA = (Vert.col >> IM_COL32_A_SHIFT) & 0xFFu;
            const uint32_t NewA = static_cast<uint32_t>(std::lround(static_cast<float>(OldA) * Marker.Alpha));
            Vert.col            = (Vert.col & ~(0xFFu << IM_COL32_A_SHIFT)) | (NewA << IM_COL32_A_SHIFT);
        }
    }
}

Point2D PixelSpace::QueryMousePosition() const noexcept
{
    const auto& Snapshot = Input::InputExtension::InspectActiveEditorSnapshot();
    return {(Snapshot.PositionX - Origin.X) / Scale, (Snapshot.PositionY - Origin.Y) / Scale};
}

bool PixelSpace::QueryMouseInside(Rectangle2D Bounds) const noexcept
{
    if (ClipDepth > 0u && !ClipStack[ClipDepth - 1u].Contains(QueryMousePosition()))
        return false;
    return Bounds.Contains(QueryMousePosition());
}

bool PixelSpace::QueryMouseDown(int Button) const noexcept
{
    return Button >= 0 && Input::InputExtension::InspectActiveEditorSnapshot().QueryButtonDown(static_cast<uint32_t>(Button));
}

bool PixelSpace::QueryMouseClicked(int Button) const noexcept
{
    return Button >= 0 && Input::InputExtension::InspectActiveEditorSnapshot().QueryButtonPressed(static_cast<uint32_t>(Button));
}

bool PixelSpace::QueryMouseDoubleClicked(int Button) const noexcept
{
    return Button >= 0 && Input::InputExtension::InspectActiveEditorSnapshot().QueryButtonDoubleClicked(static_cast<uint32_t>(Button));
}

bool PixelSpace::QueryMouseReleased(int Button) const noexcept
{
    return Button >= 0 && Input::InputExtension::InspectActiveEditorSnapshot().QueryButtonReleased(static_cast<uint32_t>(Button));
}

float PixelSpace::QueryMouseWheelY() const noexcept
{
    return Input::InputExtension::InspectActiveEditorSnapshot().ScrollDeltaY;
}

bool PixelSpace::QueryShiftHeld() const noexcept
{
    return Input::InputExtension::InspectActiveEditorSnapshot().ShiftHeld;
}

bool PixelSpace::QueryCtrlHeld() const noexcept
{
    return Input::InputExtension::InspectActiveEditorSnapshot().ControlHeld;
}

bool PixelSpace::QueryKeyPressed(InterfaceKey Key, bool Repeat) const noexcept
{
    const Input::InputLogicalKey Mapped = TranslateKey(Key);
    return Mapped != Input::InputLogicalKey::Unassigned &&
           Input::InputExtension::InspectActiveEditorSnapshot().QueryKeyPressed(Mapped, Repeat);
}

uint32_t PixelSpace::ReadTypedUtf8(char *Destination, uint32_t Capacity) const noexcept
{
    if (!Destination || Capacity == 0u)
        return 0u;
    const auto&    Snapshot = Input::InputExtension::InspectActiveEditorSnapshot();
    uint32_t       Written  = 0u;
    char           Chunk[4]{};
    for (const uint32_t Codepoint : Snapshot.QueryTypedCodepoints())
    {
        if (Codepoint < 0x20u || Codepoint == 0x7Fu)
            continue;
        const uint32_t Len = EncodeCodepointUtf8(Codepoint, Chunk);
        if (Written + Len + 1u > Capacity)
            break;
        std::memcpy(Destination + Written, Chunk, Len);
        Written += Len;
    }
    Destination[Written] = '\0';
    return Written;
}

} // namespace Frontier
