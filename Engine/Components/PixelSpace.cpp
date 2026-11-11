/*============================================================================================================================================
                                                          PIXELSPACE.CPP
============================================================================================================================================*/
// 🧩 Logical-to-physical 2D drawing implementation over patched Dear ImGui draw lists.

#include "Engine/Components/PixelSpace.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier
{

namespace
{

[[nodiscard]] constexpr ImU32 PackColor(ColorRgba Color) noexcept
{
    return IM_COL32(Color.Red, Color.Green, Color.Blue, Color.Alpha);
}

[[nodiscard]] ImGuiKey TranslateKey(InterfaceKey Key) noexcept
{
    switch (Key)
    {
    case InterfaceKey::Escape:    return ImGuiKey_Escape;
    case InterfaceKey::Enter:     return ImGuiKey_Enter;
    case InterfaceKey::Backspace: return ImGuiKey_Backspace;
    case InterfaceKey::DeleteKey: return ImGuiKey_Delete;
    case InterfaceKey::Left:      return ImGuiKey_LeftArrow;
    case InterfaceKey::Right:     return ImGuiKey_RightArrow;
    case InterfaceKey::Up:        return ImGuiKey_UpArrow;
    case InterfaceKey::Down:      return ImGuiKey_DownArrow;
    case InterfaceKey::Home:      return ImGuiKey_Home;
    case InterfaceKey::End:       return ImGuiKey_End;
    }
    return ImGuiKey_None;
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

PixelSpace::PixelSpace(ImDrawList *DrawTarget, Point2D SurfaceOrigin, float SurfaceScale, ImFont *DefaultTypeface) noexcept
    : DrawBuffer(DrawTarget)
    , Origin(SurfaceOrigin)
    , Scale(SurfaceScale > 0.01f ? SurfaceScale : 1.0f)
    , ActiveTypeface(DefaultTypeface ? DefaultTypeface : ImGui::GetFont())
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
    if (!DrawBuffer || Bounds.Width <= 0.0f || Bounds.Height <= 0.0f || Fill.Alpha == 0)
        return;
    const Rectangle2D Phys = ToPhysical(Bounds);
    const float       MaxR = 0.5f * std::min(Phys.Width, Phys.Height);
    const float       Rad  = std::clamp(CornerRadius * Scale, 0.0f, MaxR);
    DrawBuffer->AddRectFilled(ImVec2(Phys.X, Phys.Y), ImVec2(Phys.Right(), Phys.Bottom()), PackColor(Fill), Rad);
}

void PixelSpace::FillRectangleBottomRounded(Rectangle2D Bounds, ColorRgba Fill, float CornerRadius) noexcept
{
    if (!DrawBuffer || Bounds.Width <= 0.0f || Bounds.Height <= 0.0f || Fill.Alpha == 0)
        return;
    const Rectangle2D Phys = ToPhysical(Bounds);
    const float       MaxR = 0.5f * std::min(Phys.Width, Phys.Height);
    const float       Rad  = std::clamp(CornerRadius * Scale, 0.0f, MaxR);
    DrawBuffer->AddRectFilled(ImVec2(Phys.X, Phys.Y), ImVec2(Phys.Right(), Phys.Bottom()), PackColor(Fill), Rad,
                            ImDrawFlags_RoundCornersBottom);
}

void PixelSpace::StrokeRectangle(Rectangle2D Bounds, ColorRgba Stroke, float CornerRadius, float Thickness) noexcept
{
    if (!DrawBuffer || Bounds.Width <= 0.0f || Bounds.Height <= 0.0f || Stroke.Alpha == 0 || Thickness <= 0.0f)
        return;
    const Rectangle2D Phys = ToPhysical(Bounds);
    const float       MaxR = 0.5f * std::min(Phys.Width, Phys.Height);
    const float       Rad  = std::clamp(CornerRadius * Scale, 0.0f, MaxR);
    DrawBuffer->AddRect(ImVec2(Phys.X, Phys.Y), ImVec2(Phys.Right(), Phys.Bottom()), PackColor(Stroke), Rad, 0,
                      std::max(1.0f, Thickness * Scale));
}

void PixelSpace::FillCircle(Point2D Center, float Radius, ColorRgba Fill) noexcept
{
    if (!DrawBuffer || Radius <= 0.0f || Fill.Alpha == 0)
        return;
    const Point2D Phys = ToPhysical(Center);
    DrawBuffer->AddCircleFilled(ImVec2(Phys.X, Phys.Y), Radius * Scale, PackColor(Fill), 0);
}

void PixelSpace::StrokeCircle(Point2D Center, float Radius, ColorRgba Stroke, float Thickness) noexcept
{
    if (!DrawBuffer || Radius <= 0.0f || Stroke.Alpha == 0 || Thickness <= 0.0f)
        return;
    const Point2D Phys = ToPhysical(Center);
    DrawBuffer->AddCircle(ImVec2(Phys.X, Phys.Y), Radius * Scale, PackColor(Stroke), 0, std::max(1.0f, Thickness * Scale));
}

void PixelSpace::FillPolygon(std::span<const Point2D> Vertices, ColorRgba Fill) noexcept
{
    if (!DrawBuffer || Vertices.size() < 3u || Fill.Alpha == 0)
        return;
    ImVec2 StackPoints[64];
    const std::size_t Count = std::min<std::size_t>(Vertices.size(), 64u);
    for (std::size_t Index = 0; Index < Count; ++Index)
    {
        const Point2D Phys = ToPhysical(Vertices[Index]);
        StackPoints[Index] = ImVec2(Phys.X, Phys.Y);
    }
    DrawBuffer->AddConcavePolyFilled(StackPoints, static_cast<int>(Count), PackColor(Fill));
}

void PixelSpace::StrokeLine(Point2D From, Point2D To, ColorRgba Stroke, float Thickness) noexcept
{
    if (!DrawBuffer || Stroke.Alpha == 0 || Thickness <= 0.0f)
        return;
    const Point2D A = ToPhysical(From);
    const Point2D B = ToPhysical(To);
    DrawBuffer->AddLine(ImVec2(A.X, A.Y), ImVec2(B.X, B.Y), PackColor(Stroke), std::max(1.0f, Thickness * Scale));
}

void PixelSpace::StrokePolyline(std::span<const Point2D> Vertices, ColorRgba Stroke, float Thickness, bool Closed) noexcept
{
    if (!DrawBuffer || Vertices.size() < 2u || Stroke.Alpha == 0 || Thickness <= 0.0f)
        return;
    ImVec2 StackPoints[128];
    const std::size_t Count = std::min<std::size_t>(Vertices.size(), 128u);
    for (std::size_t Index = 0; Index < Count; ++Index)
    {
        const Point2D Phys = ToPhysical(Vertices[Index]);
        StackPoints[Index] = ImVec2(Phys.X, Phys.Y);
    }
    DrawBuffer->AddPolyline(StackPoints, static_cast<int>(Count), PackColor(Stroke), Closed ? ImDrawFlags_Closed : 0,
                          std::max(1.0f, Thickness * Scale));
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
    if (!DrawBuffer || !Utf8 || Ink.Alpha == 0 || PixelSize <= 0.0f)
        return;
    ImFont *Font = ActiveTypeface ? ActiveTypeface : ImGui::GetFont();
    if (!Font)
        return;
    const Point2D Phys = ToPhysical(Position);
    DrawBuffer->AddText(Font, PixelSize * Scale, ImVec2(Phys.X, Phys.Y), PackColor(Ink), Utf8, Utf8End);
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
    ImFont *Font = ActiveTypeface ? ActiveTypeface : ImGui::GetFont();
    if (!Font)
        return {};
    const ImVec2 Raw = Font->CalcTextSizeA(PixelSize * Scale, 1.0e9f, 0.0f, Utf8, Utf8End);
    return {Raw.x / Scale, Raw.y / Scale};
}

void PixelSpace::PushClip(Rectangle2D Bounds, bool IntersectWithCurrent) noexcept
{
    if (!DrawBuffer || ClipDepth >= 16u)
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
    DrawBuffer->PushClipRect(ImVec2(Phys.X, Phys.Y), ImVec2(Phys.Right(), Phys.Bottom()), IntersectWithCurrent);
}

void PixelSpace::PopClip() noexcept
{
    if (!DrawBuffer || ClipDepth == 0u)
        return;
    --ClipDepth;
    DrawBuffer->PopClipRect();
}

PixelSpace::GroupMarker PixelSpace::BeginGroup(Point2D Pivot, Point2D GroupScale, Point2D Offset, float Alpha) noexcept
{
    GroupMarker Marker{};
    Marker.FirstVertex = DrawBuffer ? DrawBuffer->VtxBuffer.Size : 0;
    Marker.Pivot       = ToPhysical(Pivot);
    Marker.Scale       = GroupScale;
    Marker.Offset      = {Offset.X * Scale, Offset.Y * Scale};
    Marker.Alpha       = std::clamp(Alpha, 0.0f, 1.0f);
    return Marker;
}

void PixelSpace::EndGroup(const GroupMarker &Marker) noexcept
{
    if (!DrawBuffer)
        return;
    const bool TransformPos = Marker.Scale.X != 1.0f || Marker.Scale.Y != 1.0f || Marker.Offset.X != 0.0f || Marker.Offset.Y != 0.0f;
    const bool FadeAlpha    = Marker.Alpha < 0.999f;
    if (!TransformPos && !FadeAlpha)
        return;
    for (int Index = Marker.FirstVertex; Index < DrawBuffer->VtxBuffer.Size; ++Index)
    {
        ImDrawVert &Vert = DrawBuffer->VtxBuffer[Index];
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
    const ImVec2 Raw = ImGui::GetIO().MousePos;
    return {(Raw.x - Origin.X) / Scale, (Raw.y - Origin.Y) / Scale};
}

bool PixelSpace::QueryMouseInside(Rectangle2D Bounds) const noexcept
{
    if (ClipDepth > 0u && !ClipStack[ClipDepth - 1u].Contains(QueryMousePosition()))
        return false;
    return Bounds.Contains(QueryMousePosition());
}

bool PixelSpace::QueryMouseDown(int Button) const noexcept
{
    return ImGui::IsMouseDown(Button);
}

bool PixelSpace::QueryMouseClicked(int Button) const noexcept
{
    return ImGui::IsMouseClicked(Button);
}

bool PixelSpace::QueryMouseDoubleClicked(int Button) const noexcept
{
    return ImGui::IsMouseDoubleClicked(Button);
}

bool PixelSpace::QueryMouseReleased(int Button) const noexcept
{
    return ImGui::IsMouseReleased(Button);
}

float PixelSpace::QueryMouseWheelY() const noexcept
{
    return ImGui::GetIO().MouseWheel;
}

bool PixelSpace::QueryShiftHeld() const noexcept
{
    return ImGui::GetIO().KeyShift;
}

bool PixelSpace::QueryCtrlHeld() const noexcept
{
    return ImGui::GetIO().KeyCtrl;
}

bool PixelSpace::QueryKeyPressed(InterfaceKey Key, bool Repeat) const noexcept
{
    const ImGuiKey Mapped = TranslateKey(Key);
    return Mapped != ImGuiKey_None && ImGui::IsKeyPressed(Mapped, Repeat);
}

uint32_t PixelSpace::ReadTypedUtf8(char *Destination, uint32_t Capacity) const noexcept
{
    if (!Destination || Capacity == 0u)
        return 0u;
    const ImGuiIO &Io      = ImGui::GetIO();
    uint32_t       Written = 0u;
    char           Chunk[4]{};
    for (int Index = 0; Index < Io.InputQueueCharacters.Size; ++Index)
    {
        const ImWchar Codepoint = Io.InputQueueCharacters[Index];
        if (Codepoint < 0x20u || Codepoint == 0x7Fu)
            continue;
        const uint32_t Len = EncodeCodepointUtf8(static_cast<uint32_t>(Codepoint), Chunk);
        if (Written + Len + 1u > Capacity)
            break;
        std::memcpy(Destination + Written, Chunk, Len);
        Written += Len;
    }
    Destination[Written] = '\0';
    return Written;
}

} // namespace Frontier
