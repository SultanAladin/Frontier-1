/*============================================================================================================================================
                                                    INSPECTORVECTORGRAPHICS.CPP
============================================================================================================================================*/
// 🧩 Verbatim 1:1 pure-vector C++ port of ProjectZeroEditor HTML inspector graphics & SVG glyphs (zero CPU pixel loops, zero vkQueueWaitIdle)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/InspectorVectorGraphics.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace Frontier::InspectorVector
{

namespace
{

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kDegToRad = kPi / 180.0f;

ColorRgba MixRgba(ColorRgba A, ColorRgba B, float T) noexcept
{
    const float C = std::clamp(T, 0.0f, 1.0f);
    auto L = [&](uint8_t X, uint8_t Y) noexcept -> uint8_t
    {
        return static_cast<uint8_t>(std::clamp(std::lround(static_cast<float>(X) + (static_cast<float>(Y) - static_cast<float>(X)) * C), 0l, 255l));
    };
    return ColorRgba{ L(A.Red, B.Red), L(A.Green, B.Green), L(A.Blue, B.Blue), L(A.Alpha, B.Alpha) };
}

void FillEllipseVec(PixelSpace& Surface, Point2D Center, float Rx, float Ry, ColorRgba Fill, uint32_t Segments = 48u) noexcept
{
    if (Rx <= 0.25f || Ry <= 0.25f || Fill.Alpha == 0u) return;
    std::array<Point2D, 64u> Pts{};
    const uint32_t Count = std::min<uint32_t>(Segments, 64u);
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const float T = kTwoPi * (static_cast<float>(I) / static_cast<float>(Count));
        Pts[I] = { Center.X + Rx * std::cos(T), Center.Y + Ry * std::sin(T) };
    }
    Surface.FillPolygon(std::span<const Point2D>(Pts.data(), Count), Fill);
}

void StrokeEllipseVec(PixelSpace& Surface, Point2D Center, float Rx, float Ry, ColorRgba Stroke, float Thickness = 1.0f, uint32_t Segments = 48u) noexcept
{
    if (Rx <= 0.25f || Ry <= 0.25f || Stroke.Alpha == 0u) return;
    std::array<Point2D, 64u> Pts{};
    const uint32_t Count = std::min<uint32_t>(Segments, 64u);
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const float T = kTwoPi * (static_cast<float>(I) / static_cast<float>(Count));
        Pts[I] = { Center.X + Rx * std::cos(T), Center.Y + Ry * std::sin(T) };
    }
    Surface.StrokePolyline(std::span<const Point2D>(Pts.data(), Count), Stroke, Thickness, true);
}

void StrokeDashedLine(PixelSpace& Surface, Point2D A, Point2D B, ColorRgba Stroke, float Thickness, float DashLen, float GapLen) noexcept
{
    const float Dx = B.X - A.X;
    const float Dy = B.Y - A.Y;
    const float Len = std::hypot(Dx, Dy);
    if (Len <= 0.5f || DashLen <= 0.2f) return;
    const float Ux = Dx / Len;
    const float Uy = Dy / Len;
    const float Period = DashLen + GapLen;
    for (float D = 0.0f; D < Len; D += Period)
    {
        const float EndD = std::min(Len, D + DashLen);
        Surface.StrokeLine({ A.X + Ux * D, A.Y + Uy * D }, { A.X + Ux * EndD, A.Y + Uy * EndD }, Stroke, Thickness);
    }
}

void StrokeDashedPolyline(PixelSpace& Surface, std::span<const Point2D> Pts, ColorRgba Stroke, float Thickness, float DashLen, float GapLen) noexcept
{
    if (Pts.size() < 2u) return;
    const float Period = DashLen + GapLen;
    float Acc = 0.0f;
    for (std::size_t I = 1u; I < Pts.size(); ++I)
    {
        const float SegLen = std::hypot(Pts[I].X - Pts[I - 1u].X, Pts[I].Y - Pts[I - 1u].Y);
        if (SegLen <= 1.0e-3f) continue;
        const float Phase = std::fmod(Acc, Period);
        if (Phase < DashLen)
        {
            Surface.StrokeLine(Pts[I - 1u], Pts[I], Stroke, Thickness);
        }
        Acc += SegLen;
    }
}

void StrokeArcVec(PixelSpace& Surface, Point2D Center, float Radius, float StartRad, float EndRad, ColorRgba Stroke, float Thickness, uint32_t Segments = 28u) noexcept
{
    std::array<Point2D, 48u> Pts{};
    const uint32_t Count = std::min<uint32_t>(Segments + 1u, 48u);
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const float T = StartRad + (EndRad - StartRad) * (static_cast<float>(I) / static_cast<float>(Count - 1u));
        Pts[I] = { Center.X + Radius * std::cos(T), Center.Y + Radius * std::sin(T) };
    }
    Surface.StrokePolyline(std::span<const Point2D>(Pts.data(), Count), Stroke, Thickness, false);
}

Point2D RotatePoint(Point2D Local, Point2D Origin, float AngleRad) noexcept
{
    const float C = std::cos(AngleRad);
    const float S = std::sin(AngleRad);
    return { Origin.X + Local.X * C - Local.Y * S, Origin.Y + Local.X * S + Local.Y * C };
}

void FillRotatedRect(PixelSpace& Surface, Point2D Origin, float AngleRad, float X, float Y, float W, float H, ColorRgba Fill) noexcept
{
    const std::array<Point2D, 4u> Quad = {{
        RotatePoint({ X,     Y     }, Origin, AngleRad),
        RotatePoint({ X + W, Y     }, Origin, AngleRad),
        RotatePoint({ X + W, Y + H }, Origin, AngleRad),
        RotatePoint({ X,     Y + H }, Origin, AngleRad),
    }};
    Surface.FillPolygon(Quad, Fill);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   HTML SVG GLYPHS
//------------------------------------------------------------------------------------------------------------------------

void ConstructHtmlGlyph(
    PixelSpace& Surface,
    HtmlGlyph   Glyph,
    Point2D     Center,
    float       SizePx,
    ColorRgba   Ink) noexcept
{
    const float S = SizePx / 24.0f;
    const float Ox = Center.X - 12.0f * S;
    const float Oy = Center.Y - 12.0f * S;
    auto P = [&](float X, float Y) noexcept -> Point2D { return { Ox + X * S, Oy + Y * S }; };
    const float Sw = std::max(1.1f, 1.6f * S);

    switch (Glyph)
    {
    case HtmlGlyph::CelestialOrbit:
        // Exact Inspectors.jsx celestial-orbit: path arc + 2 orbital nodes (19,5) & (5,19) + center circle (12,12 r=3)
        StrokeArcVec(Surface, P(12.0f, 12.0f), 9.5f * S, -0.35f, 0.78f + kTwoPi - 1.55f, Ink, Sw, 26u);
        Surface.StrokeCircle(P(19.0f, 5.0f),  2.0f * S, Ink, Sw);
        Surface.StrokeCircle(P(5.0f,  19.0f), 2.0f * S, Ink, Sw);
        Surface.StrokeCircle(P(12.0f, 12.0f), 3.0f * S, Ink, Sw);
        break;

    case HtmlGlyph::BakeSparkle:
        // Exact Inspectors.jsx bake icon: frame M12 5h7l2 2v12l-2 2H7l-2-2v-6 + arrow into box + top-left sparkle + inner square
        Surface.StrokeLine(P(12.0f, 5.0f), P(19.0f, 5.0f), Ink, Sw);
        Surface.StrokeLine(P(19.0f, 5.0f), P(21.0f, 7.0f), Ink, Sw);
        Surface.StrokeLine(P(21.0f, 7.0f), P(21.0f, 19.0f), Ink, Sw);
        Surface.StrokeLine(P(21.0f, 19.0f), P(19.0f, 21.0f), Ink, Sw);
        Surface.StrokeLine(P(19.0f, 21.0f), P(7.0f, 21.0f), Ink, Sw);
        Surface.StrokeLine(P(7.0f, 21.0f), P(5.0f, 19.0f), Ink, Sw);
        Surface.StrokeLine(P(5.0f, 19.0f), P(5.0f, 13.0f), Ink, Sw);
        Surface.StrokeLine(P(2.0f, 9.0f), P(13.0f, 9.0f), Ink, Sw);
        Surface.StrokeLine(P(9.0f, 5.0f), P(13.0f, 9.0f), Ink, Sw);
        Surface.StrokeLine(P(13.0f, 9.0f), P(9.0f, 13.0f), Ink, Sw);
        Surface.StrokeLine(P(3.0f, 2.0f), P(3.0f, 5.0f), Ink, Sw);
        Surface.StrokeLine(P(1.5f, 3.5f), P(4.5f, 3.5f), Ink, Sw);
        Surface.StrokeRectangle({ Ox + 14.0f * S, Oy + 14.0f * S, 3.2f * S, 3.2f * S }, Ink, 0.5f, Sw);
        break;

    case HtmlGlyph::LoadUpload:
        Surface.StrokeLine(P(12.0f, 19.0f), P(12.0f, 7.0f), Ink, Sw);
        Surface.StrokeLine(P(8.0f, 11.0f), P(12.0f, 7.0f), Ink, Sw);
        Surface.StrokeLine(P(16.0f, 11.0f), P(12.0f, 7.0f), Ink, Sw);
        Surface.StrokeLine(P(6.0f, 4.5f), P(18.0f, 4.5f), Ink, Sw);
        break;

    case HtmlGlyph::Check:
        Surface.StrokeLine(P(5.0f, 12.0f), P(9.5f, 16.5f), Ink, Sw * 1.15f);
        Surface.StrokeLine(P(9.5f, 16.5f), P(19.0f, 6.5f), Ink, Sw * 1.15f);
        break;

    case HtmlGlyph::Rebake:
    case HtmlGlyph::ResetArc:
        StrokeArcVec(Surface, P(12.0f, 12.0f), 8.2f * S, -2.2f, 3.14f, Ink, Sw, 22u);
        Surface.StrokeLine(P(3.5f, 4.5f), P(3.5f, 9.5f), Ink, Sw);
        Surface.StrokeLine(P(3.5f, 9.5f), P(8.5f, 9.5f), Ink, Sw);
        break;

    case HtmlGlyph::SaveDownload:
        Surface.StrokeLine(P(12.0f, 4.0f), P(12.0f, 15.0f), Ink, Sw);
        Surface.StrokeLine(P(7.5f, 10.5f), P(12.0f, 15.0f), Ink, Sw);
        Surface.StrokeLine(P(16.5f, 10.5f), P(12.0f, 15.0f), Ink, Sw);
        Surface.StrokeLine(P(5.0f, 19.5f), P(19.0f, 19.5f), Ink, Sw);
        break;

    case HtmlGlyph::CameraIcon:
        Surface.FillRectangle({ Ox + 3.0f * S, Oy + 7.5f * S, 18.0f * S, 12.0f * S }, Ink, 2.5f * S);
        Surface.FillRectangle({ Ox + 7.5f * S, Oy + 5.0f * S, 9.0f * S, 3.0f * S }, Ink, 1.0f * S);
        Surface.FillCircle(P(12.0f, 13.5f), 3.4f * S, ColorRgba{ 22u, 22u, 25u, 255u });
        Surface.StrokeCircle(P(12.0f, 13.5f), 1.8f * S, Ink, 1.0f);
        break;

    case HtmlGlyph::Plus:
        Surface.StrokeLine(P(12.0f, 5.0f), P(12.0f, 19.0f), Ink, Sw);
        Surface.StrokeLine(P(5.0f, 12.0f), P(19.0f, 12.0f), Ink, Sw);
        break;

    case HtmlGlyph::ChevronRight:
        Surface.StrokeLine(P(9.0f, 5.5f), P(15.5f, 12.0f), Ink, Sw);
        Surface.StrokeLine(P(15.5f, 12.0f), P(9.0f, 18.5f), Ink, Sw);
        break;

    case HtmlGlyph::Sun:
        Surface.StrokeCircle(P(12.0f, 12.0f), 4.0f * S, Ink, Sw);
        for (int32_t I = 0; I < 8; ++I)
        {
            const float A = static_cast<float>(I) * (kPi * 0.25f);
            Surface.StrokeLine(
                P(12.0f + std::cos(A) * 7.2f, 12.0f + std::sin(A) * 7.2f),
                P(12.0f + std::cos(A) * 10.0f, 12.0f + std::sin(A) * 10.0f),
                Ink, Sw);
        }
        break;

    case HtmlGlyph::LuminousSurface:
        Surface.FillRectangle({ Ox + 4.0f * S, Oy + 5.0f * S, 16.0f * S, 10.0f * S }, Ink, 2.0f * S);
        Surface.StrokeLine(P(8.0f, 18.5f), P(16.0f, 18.5f), Ink, Sw);
        Surface.StrokeLine(P(12.0f, 15.0f), P(12.0f, 18.5f), Ink, Sw);
        break;

    case HtmlGlyph::PointLight:
        Surface.FillCircle(P(12.0f, 12.0f), 3.6f * S, Ink);
        Surface.StrokeCircle(P(12.0f, 12.0f), 7.5f * S, Ink.WithAlpha(130u), 1.1f);
        break;

    case HtmlGlyph::SpotLight:
    {
        const std::array<Point2D, 3u> Cone = {{ P(12.0f, 5.0f), P(4.0f, 19.0f), P(20.0f, 19.0f) }};
        Surface.StrokePolyline(Cone, Ink, Sw, true);
        Surface.FillCircle(P(12.0f, 5.5f), 2.3f * S, Ink);
        break;
    }

    case HtmlGlyph::IesLight:
        Surface.StrokeCircle(P(12.0f, 11.0f), 7.0f * S, Ink.WithAlpha(120u), 1.0f);
        StrokeArcVec(Surface, P(12.0f, 14.5f), 6.5f * S, 0.2f, kPi - 0.2f, Ink, Sw, 16u);
        Surface.StrokeLine(P(12.0f, 4.0f), P(12.0f, 20.0f), Ink.WithAlpha(140u), 1.0f);
        break;

    case HtmlGlyph::TubeLight:
        Surface.StrokeRectangle({ Ox + 3.0f * S, Oy + 9.0f * S, 18.0f * S, 6.0f * S }, Ink, 3.0f * S, Sw);
        Surface.FillRectangle({ Ox + 6.0f * S, Oy + 11.0f * S, 12.0f * S, 2.0f * S }, Ink, 1.0f * S);
        break;

    case HtmlGlyph::LedLight:
        StrokeArcVec(Surface, P(12.0f, 14.0f), 6.0f * S, kPi, kTwoPi, Ink, Sw, 16u);
        Surface.StrokeLine(P(6.0f, 14.0f), P(18.0f, 14.0f), Ink, Sw);
        Surface.StrokeLine(P(9.0f, 14.0f), P(9.0f, 20.0f), Ink, Sw);
        Surface.StrokeLine(P(15.0f, 14.0f), P(15.0f, 20.0f), Ink, Sw);
        break;

    case HtmlGlyph::LedStrip:
        Surface.StrokeRectangle({ Ox + 2.5f * S, Oy + 8.5f * S, 19.0f * S, 7.0f * S }, Ink, 2.0f * S, Sw);
        Surface.FillCircle(P(6.5f, 12.0f), 1.5f * S, Ink);
        Surface.FillCircle(P(12.0f, 12.0f), 1.5f * S, Ink);
        Surface.FillCircle(P(17.5f, 12.0f), 1.5f * S, Ink);
        break;

    case HtmlGlyph::Environment:
        StrokeArcVec(Surface, P(12.0f, 15.0f), 8.0f * S, kPi, kTwoPi, Ink, Sw, 18u);
        Surface.StrokeLine(P(3.0f, 15.0f), P(21.0f, 15.0f), Ink, Sw);
        Surface.StrokeLine(P(6.0f, 18.5f), P(18.0f, 18.5f), Ink.WithAlpha(140u), 1.1f);
        break;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SHARED CARD & HEADER PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

void ConstructSectionSurfaceWithIcon(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    HtmlGlyph   Icon,
    ColorRgba   IconInk,
    const char* Title,
    const char* RightText,
    ColorRgba   RightInk) noexcept
{
    Surface.FillRectangle(Bounds, InspectorMetrics::SectionFill, InspectorMetrics::SectionRadius);
    Surface.StrokeRectangle(Bounds, InspectorMetrics::SectionBorder, InspectorMetrics::SectionRadius, 1.0f);

    const float TitleY = Bounds.Y + InspectorMetrics::SectionPadTop;
    const float IconCx = Bounds.X + InspectorMetrics::SectionPadX + 8.0f;
    ConstructHtmlGlyph(Surface, Icon, { IconCx, TitleY + 7.0f }, 16.0f, IconInk);

    if (Title != nullptr && Title[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Light, { Bounds.X + InspectorMetrics::SectionPadX + 24.0f, TitleY }, 12.5f, InspectorMetrics::HeadingInk, Title);
    }
    if (RightText != nullptr && RightText[0] != '\0')
    {
        const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, RightText);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { Bounds.Right() - InspectorMetrics::SectionPadX - RW, TitleY + 1.0f }, 10.5f, RightInk, RightText);
    }
}

float ConstructSplitMetricReadout(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    const char* WholePart,
    const char* FractionPart,
    const char* UnitText,
    const char* CaptionText,
    float       NumberSize) noexcept
{
    float CursorX = LeftX;
    if (WholePart != nullptr && WholePart[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Light, { CursorX, TopY }, NumberSize, ColorRgba{ 242u, 242u, 242u, 255u }, WholePart);
        CursorX += WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, NumberSize, WholePart);
    }
    if (FractionPart != nullptr && FractionPart[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Light, { CursorX, TopY }, NumberSize, ColorRgba{ 138u, 143u, 152u, 255u }, FractionPart);
        CursorX += WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, NumberSize, FractionPart);
    }
    if (UnitText != nullptr && UnitText[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { CursorX + 5.0f, TopY + NumberSize * 0.44f }, 13.0f, InspectorMetrics::MutedInk, UnitText);
    }
    float NextY = TopY + NumberSize + 4.0f;
    if (CaptionText != nullptr && CaptionText[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { LeftX, NextY }, 10.5f, InspectorMetrics::MutedInk, CaptionText);
        NextY += 18.0f;
    }
    return NextY;
}

float ConstructInspectorHeaderEx(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    const char*             Breadcrumb,
    const char*             Title,
    const char*             Subtitle,
    const char*             LinkedParentTitle,
    const char*             LinkedParentCaption,
    const PaneInputReading& Input,
    bool&                   OutAddNotesClicked,
    bool&                   OutParentLinkClicked) noexcept
{
    OutAddNotesClicked   = false;
    OutParentLinkClicked = false;

    const Point2D Cur { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Clicked = Input.PrimaryPointer.Pressed;

    // Top-right dashed "+ Add notes" pill (matches FollowUp67/68 CameraSettings exact header)
    const float NotesBtnW = 86.0f;
    const float NotesBtnH = 26.0f;
    const Rectangle2D NotesPill { LeftX + InnerWidth - NotesBtnW, TopY - 3.0f, NotesBtnW, NotesBtnH };
    const bool NotesHov = NotesPill.Contains(Cur);
    Surface.FillRectangle(NotesPill, NotesHov ? ColorRgba{ 32u, 32u, 36u, 255u } : ColorRgba{ 16u, 16u, 18u, 255u }, 13.0f);
    Surface.StrokeRectangle(NotesPill, ColorRgba{ 255u, 255u, 255u, static_cast<uint8_t>(NotesHov ? 54u : 28u) }, 13.0f, 1.0f);
    ConstructHtmlGlyph(Surface, HtmlGlyph::Plus, { NotesPill.X + 14.0f, NotesPill.Center().Y }, 11.0f, InspectorMetrics::MutedInk);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { NotesPill.X + 24.0f, NotesPill.Y + 7.0f }, 10.0f,
        NotesHov ? ColorRgba{ 238u, 238u, 238u, 255u } : InspectorMetrics::MutedInk, "Add notes");
    if (NotesHov && Clicked)
    {
        OutAddNotesClicked = true;
    }

    // Left breadcrumb
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { LeftX, TopY + 2.0f }, InnerWidth - NotesBtnW - 10.0f, 10.0f, InspectorMetrics::DimInk, Breadcrumb);

    // Light 25px Title + muted Subtitle
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Light, { LeftX, TopY + 24.0f }, InnerWidth, 25.0f, ColorRgba{ 242u, 242u, 242u, 255u }, Title);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { LeftX, TopY + 56.0f }, InnerWidth, 10.5f, InspectorMetrics::MutedInk, Subtitle);

    float NextY = TopY + 78.0f;

    // Optional parent-component link card (e.g. "Editor Camera · Component of this camera · values are stored on it >")
    if (LinkedParentTitle != nullptr && LinkedParentTitle[0] != '\0')
    {
        const Rectangle2D LinkCard { LeftX, NextY, InnerWidth, 52.0f };
        const bool LinkHov = LinkCard.Contains(Cur);
        Surface.FillRectangle(LinkCard, LinkHov ? ColorRgba{ 24u, 25u, 28u, 255u } : ColorRgba{ 14u, 15u, 17u, 255u }, 14.0f);
        Surface.StrokeRectangle(LinkCard, ColorRgba{ 255u, 255u, 255u, static_cast<uint8_t>(LinkHov ? 36u : 18u) }, 14.0f, 1.0f);
        ConstructHtmlGlyph(Surface, HtmlGlyph::CameraIcon, { LinkCard.X + 22.0f, LinkCard.Center().Y }, 18.0f, ColorRgba{ 130u, 134u, 142u, 255u });
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Medium, { LinkCard.X + 44.0f, LinkCard.Y + 9.0f }, LinkCard.Width - 72.0f, 12.0f, ColorRgba{ 238u, 238u, 240u, 255u }, LinkedParentTitle);
        if (LinkedParentCaption != nullptr && LinkedParentCaption[0] != '\0')
        {
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { LinkCard.X + 44.0f, LinkCard.Y + 28.0f }, LinkCard.Width - 72.0f, 9.8f, InspectorMetrics::MutedInk, LinkedParentCaption);
        }
        ConstructHtmlGlyph(Surface, HtmlGlyph::ChevronRight, { LinkCard.Right() - 16.0f, LinkCard.Center().Y }, 13.0f, InspectorMetrics::MutedInk);
        if (LinkHov && Clicked)
        {
            OutParentLinkClicked = true;
        }
        NextY = LinkCard.Bottom() + 12.0f;
    }

    return NextY;
}

//------------------------------------------------------------------------------------------------------------------------
//                                VERBATIM SUN ORBIT GIZMO (environment-graphics.jsx)
//------------------------------------------------------------------------------------------------------------------------

bool ConstructSunOrbitGizmo(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    float                   AzimuthDegrees,
    float                   ElevationDegrees,
    SunOrbitGizmoState&     InOutDragState,
    const PaneInputReading& Input,
    float&                  OutNewAzimuth,
    float&                  OutNewElevation) noexcept
{
    OutNewAzimuth   = AzimuthDegrees;
    OutNewElevation = ElevationDegrees;
    bool Changed    = false;

    const Point2D Cur { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    if (Input.PrimaryPointer.Pressed && Bounds.Contains(Cur))
    {
        InOutDragState.Dragging       = true;
        InOutDragState.DragOrigin     = Cur;
        InOutDragState.StartAzimuth   = AzimuthDegrees;
        InOutDragState.StartElevation = ElevationDegrees;
    }
    if (InOutDragState.Dragging)
    {
        if (Input.PrimaryPointer.Down)
        {
            const float Dx = Cur.X - InOutDragState.DragOrigin.X;
            const float Dy = Cur.Y - InOutDragState.DragOrigin.Y;
            float NextAz = std::fmod(InOutDragState.StartAzimuth + Dx * 0.65f, 360.0f);
            if (NextAz < 0.0f) NextAz += 360.0f;
            const float NextEl = std::clamp(InOutDragState.StartElevation - Dy * 0.60f, -90.0f, 90.0f);
            OutNewAzimuth   = std::round(NextAz * 10.0f) * 0.1f;
            OutNewElevation = std::round(NextEl * 10.0f) * 0.1f;
            Changed = (std::fabs(OutNewAzimuth - AzimuthDegrees) > 0.05f || std::fabs(OutNewElevation - ElevationDegrees) > 0.05f);
        }
        else
        {
            InOutDragState.Dragging = false;
        }
    }

    // Exact viewBox 0 0 360 280 uniform scaling inside Bounds
    const float Scale = std::min(Bounds.Width / 360.0f, Bounds.Height / 280.0f);
    const float Ox    = Bounds.Center().X - 180.0f * Scale;
    const float Oy    = Bounds.Center().Y - 140.0f * Scale;
    auto S = [&](float X, float Y) noexcept -> Point2D { return { Ox + X * Scale, Oy + Y * Scale }; };

    constexpr float kR  = 103.0f;
    constexpr float kCx = 180.0f;
    constexpr float kCy = 143.0f;
    constexpr float kRy = 49.0f;
    const float AngleRad = (OutNewAzimuth - 135.0f) * kDegToRad;
    auto OrbitPoint = [&](float T) noexcept -> Point2D
    {
        const float Px = kCx - kR * std::cos(T) * std::cos(AngleRad);
        const float Py = kCy - kR * std::sin(T) + kR * 0.45f * std::cos(T) * std::sin(AngleRad);
        return S(Px, Py);
    };

    // Horizon disc fill (#151515 opacity 0.6)
    FillEllipseVec(Surface, S(kCx, kCy), kR * Scale, kRy * Scale, ColorRgba{ 21u, 21u, 21u, 153u }, 56u);

    // Polar grid: concentric rings (.25, .5, .75) + 12 radial spokes (#ffffff12)
    const ColorRgba GridInk { 255u, 255u, 255u, 18u };
    for (float F : { 0.25f, 0.50f, 0.75f })
    {
        StrokeEllipseVec(Surface, S(kCx, kCy), kR * F * Scale, kRy * F * Scale, GridInk, 0.85f, 44u);
    }
    for (int32_t I = 0; I < 12; ++I)
    {
        const float T = static_cast<float>(I) * (kPi / 6.0f);
        Surface.StrokeLine(S(kCx, kCy), S(kCx + kR * std::cos(T), kCy + kRy * std::sin(T)), GridInk, 0.85f);
    }

    // Horizon rim (#707070 opacity 0.65)
    StrokeEllipseVec(Surface, S(kCx, kCy), kR * Scale, kRy * Scale, ColorRgba{ 112u, 112u, 112u, 166u }, 1.0f, 56u);

    // Night arc (PI..2PI, dashed #77736b opacity 0.5)
    std::array<Point2D, 65u> NightPts{};
    std::array<Point2D, 65u> DayPts{};
    for (uint32_t I = 0u; I <= 64u; ++I)
    {
        const float Frac = static_cast<float>(I) / 64.0f;
        NightPts[I] = OrbitPoint(kPi + Frac * kPi);
        DayPts[I]   = OrbitPoint(Frac * kPi);
    }
    StrokeDashedPolyline(Surface, NightPts, ColorRgba{ 119u, 115u, 107u, 128u }, 1.1f, 3.0f * Scale, 4.0f * Scale);

    // Day arc (0..PI, solid #e2ac5b)
    Surface.StrokePolyline(DayPts, ColorRgba{ 226u, 172u, 91u, 255u }, std::max(1.4f, 1.8f * Scale), false);

    // Current sun position + horizon crossing point
    const Point2D SunPt = OrbitPoint(OutNewElevation * kDegToRad);
    const Point2D HorPt = OrbitPoint(0.0f);
    const bool    Night = (OutNewElevation < 0.0f);

    // Dashed drop line (#b8aa8e opacity 0.55) + nadir shadow ellipse (#c5c0b6 opacity 0.32)
    StrokeDashedLine(Surface, SunPt, HorPt, ColorRgba{ 184u, 170u, 142u, 140u }, 0.9f, 2.5f * Scale, 4.0f * Scale);
    FillEllipseVec(Surface, { SunPt.X, HorPt.Y }, 6.0f * Scale, 2.3f * Scale, ColorRgba{ 197u, 192u, 182u, 82u }, 20u);

    // Hollow white horizon marker circle (#181818 fill, #f1efe7 stroke)
    Surface.FillCircle(HorPt, 5.5f * Scale, ColorRgba{ 24u, 24u, 24u, 255u });
    Surface.StrokeCircle(HorPt, 5.5f * Scale, ColorRgba{ 241u, 239u, 231u, 255u }, std::max(1.4f, 1.7f * Scale));

    // Radial golden halo when above horizon (#ffda87)
    if (!Night)
    {
        for (int32_t Ring = 5; Ring >= 1; --Ring)
        {
            const float T = static_cast<float>(Ring) / 5.0f;
            const uint8_t Alpha = static_cast<uint8_t>((1.0f - T * 0.78f) * 44.0f);
            Surface.FillCircle(SunPt, (8.0f + 16.0f * T) * Scale, ColorRgba{ 255u, 218u, 135u, Alpha });
        }
    }

    // Sun disc (#f4cd75 day / #9b805c night) + bright core (#fff4c7)
    Surface.FillCircle(SunPt, 8.0f * Scale, Night ? ColorRgba{ 155u, 128u, 92u, 255u } : ColorRgba{ 244u, 205u, 117u, 255u });
    Surface.StrokeCircle(SunPt, 8.0f * Scale, Night ? ColorRgba{ 155u, 128u, 92u, 255u } : ColorRgba{ 184u, 144u, 69u, 255u }, 1.0f);
    if (!Night)
    {
        Surface.FillCircle(SunPt, 4.4f * Scale, ColorRgba{ 255u, 244u, 199u, 255u });
    }

    // Cardinal & time labels (12h, 24h, N, S, W, E)
    auto DrawCenteredLabel = [&](float Vx, float Vy, const char* Txt, ColorRgba Ink) noexcept
    {
        const Point2D Pos = S(Vx, Vy);
        const float W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, Txt);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Pos.X - W * 0.5f, Pos.Y - 5.0f }, 9.5f, Ink, Txt);
    };
    const ColorRgba LabelInk { 148u, 148u, 148u, 255u };
    DrawCenteredLabel(180.0f,  24.0f, "12h", LabelInk);
    DrawCenteredLabel(180.0f, 267.0f, "24h", LabelInk);
    DrawCenteredLabel(180.0f,  80.0f, "N",   LabelInk);
    DrawCenteredLabel(180.0f, 210.0f, "S",   LabelInk);
    DrawCenteredLabel( 47.0f, 145.0f, "W",   LabelInk);
    DrawCenteredLabel(313.0f, 145.0f, "E",   LabelInk);

    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                     ILLUMINANCE, SCATTERING & WIND VECTOR GRAPHICS
//------------------------------------------------------------------------------------------------------------------------

void ConstructIlluminanceCurve(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    float       IntensityMultiplier,
    float       DirectGainMultiplier,
    float       MaxIntensity) noexcept
{
    const float Combined = IntensityMultiplier * DirectGainMultiplier;
    const float T = std::clamp(IntensityMultiplier / std::max(1.0f, MaxIntensity), 0.0f, 1.0f);

    // Header readout above chart: "22 × intensity" left, "Combined gain 55 ×" gold right
    char LeftTxt[32]{}, RightTxt[40]{};
    std::snprintf(LeftTxt,  sizeof(LeftTxt),  "%.0f × intensity", IntensityMultiplier);
    std::snprintf(RightTxt, sizeof(RightTxt), "Combined gain %.0f ×", Combined);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X, Bounds.Y }, 10.5f, InspectorMetrics::HeadingInk, LeftTxt);
    const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, RightTxt);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.Right() - RW, Bounds.Y }, 10.5f, ColorRgba{ 226u, 198u, 134u, 255u }, RightTxt);

    const Rectangle2D Plot { Bounds.X + 26.0f, Bounds.Y + 22.0f, Bounds.Width - 30.0f, Bounds.Height - 40.0f };
    for (int32_t Row = 0; Row <= 4; ++Row)
    {
        const float Frac = static_cast<float>(Row) * 0.25f;
        const float Gy = Plot.Y + Plot.Height * Frac;
        StrokeDashedLine(Surface, { Plot.X, Gy }, { Plot.Right(), Gy }, ColorRgba{ 255u, 255u, 255u, 14u }, 1.0f, 2.0f, 5.0f);
        char Tick[16]{};
        std::snprintf(Tick, sizeof(Tick), "%d", static_cast<int32_t>(std::lround((1.0f - Frac) * 300.0f)));
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X, Gy - 5.0f }, 8.5f, InspectorMetrics::DimInk, Tick);
    }

    const float MaxCombined = std::max(300.0f, MaxIntensity * DirectGainMultiplier);
    const float EndFracY = std::clamp((MaxIntensity * DirectGainMultiplier) / MaxCombined, 0.1f, 1.0f);
    const Point2D P0 { Plot.X, Plot.Bottom() };
    const Point2D P1 { Plot.Right(), Plot.Bottom() - Plot.Height * EndFracY };
    const Point2D ActivePt { Plot.X + Plot.Width * T, Plot.Bottom() - Plot.Height * (EndFracY * T) };

    const std::array<Point2D, 3u> AreaFill = {{ P0, ActivePt, { ActivePt.X, Plot.Bottom() } }};
    Surface.FillPolygon(AreaFill, ColorRgba{ 216u, 189u, 130u, 32u });
    Surface.StrokeLine(P0, P1, ColorRgba{ 89u, 82u, 70u, 255u }, 1.2f);
    Surface.StrokeLine(P0, ActivePt, ColorRgba{ 218u, 193u, 139u, 255u }, 1.8f);
    StrokeDashedLine(Surface, { ActivePt.X, Plot.Y }, { ActivePt.X, Plot.Bottom() }, ColorRgba{ 200u, 175u, 122u, 90u }, 1.0f, 2.0f, 4.0f);
    Surface.FillCircle(ActivePt, 8.0f, ColorRgba{ 232u, 199u, 106u, 36u });
    Surface.FillCircle(ActivePt, 3.4f, ColorRgba{ 239u, 226u, 194u, 255u });

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Plot.X, Plot.Bottom() + 5.0f }, 8.5f, InspectorMetrics::DimInk, "0 × intensity");
    const float MidW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.5f, "30 × intensity");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Plot.Center().X - MidW * 0.5f, Plot.Bottom() + 5.0f }, 8.5f, InspectorMetrics::DimInk, "30 × intensity");
    const float EndW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.5f, "60 × intensity");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Plot.Right() - EndW, Plot.Bottom() + 5.0f }, 8.5f, InspectorMetrics::DimInk, "60 × intensity");
}

void ConstructScatteringGraph(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    float       RayleighScale,
    float       HazeScale) noexcept
{
    Surface.FillRectangle(Bounds, ColorRgba{ 16u, 17u, 20u, 255u }, 12.0f);
    Surface.StrokeRectangle(Bounds, ColorRgba{ 255u, 255u, 255u, 14u }, 12.0f, 1.0f);

    const float Sx = Bounds.Width / 620.0f;
    const float Sy = Bounds.Height / 225.0f;
    auto P = [&](float X, float Y) noexcept -> Point2D { return { Bounds.X + X * Sx, Bounds.Y + Y * Sy }; };

    for (float Gy : { 35.0f, 80.0f, 125.0f, 170.0f })
    {
        StrokeDashedLine(Surface, P(24.0f, Gy), P(595.0f, Gy), ColorRgba{ 255u, 255u, 255u, 12u }, 1.0f, 2.0f, 6.0f);
    }
    for (float Gx : { 24.0f, 167.0f, 310.0f, 452.0f, 595.0f })
    {
        Surface.StrokeLine(P(Gx, 20.0f), P(Gx, 181.0f), ColorRgba{ 255u, 255u, 255u, 8u }, 1.0f);
    }

    const float ClampedRay = std::clamp(RayleighScale, 0.1f, 3.0f);
    const float ClampedHaze = std::clamp(HazeScale, 0.0f, 50.0f);
    std::array<Point2D, 33u> RayPts{};
    std::array<Point2D, 33u> HazePts{};
    for (uint32_t I = 0u; I <= 32u; ++I)
    {
        const float U = static_cast<float>(I) / 32.0f;
        const float LambdaNorm = 380.0f + U * 400.0f;
        const float Inv4 = std::pow(440.0f / LambdaNorm, 4.0f);
        const float Ry = std::clamp(174.0f - std::min(148.0f, Inv4 * 42.0f * ClampedRay), 24.0f, 178.0f);
        const float Hy = std::clamp(172.0f - ClampedHaze * (1.1f - 0.35f * U), 28.0f, 178.0f);
        RayPts[I]  = P(24.0f + U * 571.0f, Ry);
        HazePts[I] = P(24.0f + U * 571.0f, Hy);
    }
    for (uint32_t I = 0u; I < 32u; ++I)
    {
        const std::array<Point2D, 4u> Strip = {{
            RayPts[I], RayPts[I + 1u], { RayPts[I + 1u].X, P(0.0f, 181.0f).Y }, { RayPts[I].X, P(0.0f, 181.0f).Y }
        }};
        Surface.FillPolygon(Strip, ColorRgba{ 145u, 175u, 210u, 38u });
    }
    Surface.StrokePolyline(RayPts, ColorRgba{ 161u, 185u, 216u, 255u }, 1.8f, false);
    StrokeDashedPolyline(Surface, HazePts, ColorRgba{ 204u, 186u, 149u, 220u }, 1.2f, 4.0f, 4.0f);

    // Visible spectrum bar (380nm..780nm)
    static constexpr std::array<ColorRgba, 5u> SpecStops = {{
        { 146u, 127u, 176u, 170u },
        { 127u, 157u, 201u, 170u },
        { 148u, 181u, 169u, 170u },
        { 197u, 178u, 126u, 170u },
        { 194u, 139u, 124u, 170u },
    }};
    const float BarY = P(0.0f, 193.0f).Y;
    const float SegW = (571.0f * Sx) / 4.0f;
    for (uint32_t I = 0u; I < 4u; ++I)
    {
        Surface.FillRectangle({ P(24.0f, 0.0f).X + static_cast<float>(I) * SegW, BarY, SegW + 0.5f, 3.0f }, MixRgba(SpecStops[I], SpecStops[I + 1u], 0.5f), 1.0f);
    }

    const float TxtY = Bounds.Bottom() - 14.0f;
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + 10.0f, TxtY }, 8.5f, InspectorMetrics::DimInk, "380 nm");
    const float MidW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.5f, "Visible spectrum");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.Center().X - MidW * 0.5f, TxtY }, 8.5f, InspectorMetrics::DimInk, "Visible spectrum");
    const float EndW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.5f, "780 nm");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.Right() - 10.0f - EndW, TxtY }, 8.5f, InspectorMetrics::DimInk, "780 nm");
}

void ConstructWindCompass(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    float       BearingDegrees) noexcept
{
    const float Scale = std::min(Bounds.Width / 230.0f, Bounds.Height / 210.0f);
    const Point2D Center { Bounds.Center().X, Bounds.Center().Y };
    const float OuterR = 69.0f * Scale;
    const float InnerR = 51.0f * Scale;

    Surface.FillCircle(Center, OuterR, ColorRgba{ 28u, 28u, 28u, 255u });
    Surface.StrokeCircle(Center, OuterR, ColorRgba{ 69u, 69u, 69u, 255u }, 1.0f);
    StrokeEllipseVec(Surface, Center, InnerR, InnerR, ColorRgba{ 48u, 48u, 48u, 255u }, 1.0f, 36u);

    for (int32_t I = 0; I < 36; ++I)
    {
        const float A = static_cast<float>(I) * 10.0f * kDegToRad - kPi * 0.5f;
        const float Len = (I % 3 == 0) ? (8.0f * Scale) : (4.0f * Scale);
        Surface.StrokeLine(
            { Center.X + (OuterR - 3.0f * Scale) * std::cos(A), Center.Y + (OuterR - 3.0f * Scale) * std::sin(A) },
            { Center.X + (OuterR - 3.0f * Scale - Len) * std::cos(A), Center.Y + (OuterR - 3.0f * Scale - Len) * std::sin(A) },
            ColorRgba{ 107u, 107u, 107u, 255u }, 0.9f);
    }

    const float Rad = BearingDegrees * kDegToRad;
    const std::array<Point2D, 4u> Arrow = {{
        RotatePoint({  0.0f, -50.0f * Scale }, Center, Rad),
        RotatePoint({ 11.0f * Scale,  12.0f * Scale }, Center, Rad),
        RotatePoint({  0.0f,   5.0f * Scale }, Center, Rad),
        RotatePoint({ -11.0f * Scale, 12.0f * Scale }, Center, Rad),
    }};
    Surface.FillPolygon(Arrow, ColorRgba{ 184u, 201u, 207u, 255u });
    StrokeDashedLine(Surface, RotatePoint({ 0.0f, 9.0f * Scale }, Center, Rad), RotatePoint({ 0.0f, 47.0f * Scale }, Center, Rad), ColorRgba{ 112u, 134u, 142u, 255u }, 1.1f, 3.0f, 4.0f);
    Surface.FillCircle(Center, 3.0f * Scale, ColorRgba{ 237u, 237u, 237u, 255u });

    auto Card = [&](float Dx, float Dy, const char* Txt) noexcept
    {
        const float W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, Txt);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Center.X + Dx * Scale - W * 0.5f, Center.Y + Dy * Scale - 5.0f }, 9.5f, ColorRgba{ 139u, 139u, 139u, 255u }, Txt);
    };
    Card(  0.0f, -82.0f, "N");
    Card(  0.0f,  84.0f, "S");
    Card(-85.0f,   0.0f, "W");
    Card( 85.0f,   0.0f, "E");
}

//------------------------------------------------------------------------------------------------------------------------
//                                VERBATIM CAMERA MOUNT PICTURE (CameraMount.jsx)
//------------------------------------------------------------------------------------------------------------------------

void ConstructCameraMountPicture(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    bool        Floating,
    float       HeightMeters,
    float       TiltDegrees,
    float       RollDegrees,
    bool        SmallThumb) noexcept
{
    const float Vw = SmallThumb ? 72.0f : 300.0f;
    const float Vh = SmallThumb ? 52.0f : 170.0f;
    const float Scale = std::min(Bounds.Width / Vw, Bounds.Height / Vh);
    const float Ox = Bounds.Center().X - Vw * 0.5f * Scale;
    const float Oy = Bounds.Center().Y - Vh * 0.5f * Scale;
    auto P = [&](float X, float Y) noexcept -> Point2D { return { Ox + X * Scale, Oy + Y * Scale }; };

    const float Ground = SmallThumb ? 46.0f : 150.0f;
    const float HScale = SmallThumb ? 13.0f : (Floating ? 15.0f : 46.0f);
    const float Cy = Ground - std::min(SmallThumb ? 34.0f : 128.0f, std::max(0.2f, HeightMeters) * HScale);
    const float Cx = SmallThumb ? 36.0f : 150.0f;

    // Ground horizon line (#ffffff26)
    Surface.StrokeLine(P(SmallThumb ? 4.0f : 14.0f, Ground), P(SmallThumb ? 68.0f : 286.0f, Ground), ColorRgba{ 255u, 255u, 255u, 38u }, 1.0f);

    if (Floating)
    {
        const ColorRgba SkyBlue { 159u, 208u, 255u, 180u };
        StrokeDashedLine(Surface, P(Cx, Cy + (SmallThumb ? 6.0f : 16.0f)), P(Cx, Ground), SkyBlue, 1.1f, SmallThumb ? 2.0f : 4.0f, SmallThumb ? 2.0f : 4.0f);
        StrokeEllipseVec(Surface, P(Cx, Ground), (SmallThumb ? 8.0f : 26.0f) * Scale, (SmallThumb ? 2.0f : 6.0f) * Scale, SkyBlue, 1.1f, 28u);

        // Crane / drone curved approach arc
        std::array<Point2D, 20u> ArcPts{};
        for (uint32_t I = 0u; I < 20u; ++I)
        {
            const float T = static_cast<float>(I) / 19.0f;
            const float U = 1.0f - T;
            if (SmallThumb)
            {
                const float Ax = U * U * (Cx - 26.0f) + 2.0f * U * T * (Cx - 14.0f) + T * T * (Cx - 8.0f);
                const float Ay = U * U * (Cy + 10.0f) + 2.0f * U * T * (Cy - 10.0f) + T * T * Cy;
                ArcPts[I] = P(Ax, Ay);
            }
            else
            {
                const float Ax = U * U * U * (Cx - 120.0f) + 3.0f * U * U * T * (Cx - 90.0f) + 3.0f * U * T * T * (Cx - 50.0f) + T * T * T * (Cx - 30.0f);
                const float Ay = U * U * U * (Cy + 40.0f) + 3.0f * U * U * T * (Cy - 30.0f) + 3.0f * U * T * T * (Cy - 30.0f) + T * T * T * Cy;
                ArcPts[I] = P(Ax, Ay);
            }
        }
        StrokeDashedPolyline(Surface, ArcPts, ColorRgba{ 159u, 208u, 255u, 140u }, 1.1f, SmallThumb ? 1.5f : 3.0f, SmallThumb ? 2.5f : 5.0f);
    }
    else
    {
        // Tripod legs (#a9a49b)
        const ColorRgba LegInk { 169u, 164u, 155u, 255u };
        const float LegW = std::max(1.2f, (SmallThumb ? 1.3f : 3.0f) * Scale);
        const Point2D Head = P(Cx, Cy + (SmallThumb ? 5.0f : 14.0f));
        Surface.StrokeLine(Head, P(Cx - (SmallThumb ? 14.0f : 46.0f), Ground), LegInk, LegW);
        Surface.StrokeLine(Head, P(Cx + (SmallThumb ? 14.0f : 46.0f), Ground), LegInk, LegW);
        Surface.StrokeLine(Head, P(Cx + (SmallThumb ? 3.0f : 8.0f), Ground), ColorRgba{ 169u, 164u, 155u, 153u }, LegW * 0.9f);
    }

    // Cine camera body + lens + dual film reels (rotated by Roll - Tilt*0.6)
    const Point2D CamOrigin = P(Cx, Cy);
    const float CamAngle = (RollDegrees - TiltDegrees * 0.6f) * kDegToRad;
    const ColorRgba BodyFill { 217u, 212u, 202u, 255u };
    const ColorRgba LensFill { 155u, 150u, 141u, 255u };

    FillRotatedRect(Surface, CamOrigin, CamAngle,
        (SmallThumb ? -7.0f : -20.0f) * Scale, (SmallThumb ? -5.0f : -13.0f) * Scale,
        (SmallThumb ? 14.0f : 40.0f) * Scale,  (SmallThumb ?  9.0f : 25.0f) * Scale, BodyFill);
    FillRotatedRect(Surface, CamOrigin, CamAngle,
        (SmallThumb ? 7.0f : 20.0f) * Scale, (SmallThumb ? -3.0f : -8.0f) * Scale,
        (SmallThumb ? 6.0f : 17.0f) * Scale, (SmallThumb ?  5.0f : 15.0f) * Scale, LensFill);

    const Point2D ReelA = RotatePoint({ (SmallThumb ? -2.0f : -6.0f) * Scale, (SmallThumb ? -7.0f : -19.0f) * Scale }, CamOrigin, CamAngle);
    const Point2D ReelB = RotatePoint({ (SmallThumb ?  4.0f :  9.0f) * Scale, (SmallThumb ? -7.0f : -19.0f) * Scale }, CamOrigin, CamAngle);
    const float ReelR = (SmallThumb ? 3.0f : 8.0f) * Scale;
    const float ReelStroke = std::max(1.2f, (SmallThumb ? 1.4f : 3.0f) * Scale);
    Surface.StrokeCircle(ReelA, ReelR, BodyFill, ReelStroke);
    Surface.StrokeCircle(ReelB, ReelR, BodyFill, ReelStroke);

    // Gold height measurement bracket (#ffd38a) on large hero only
    if (!SmallThumb)
    {
        const ColorRgba Gold { 255u, 211u, 138u, 255u };
        Surface.StrokeLine(P(262.0f, Cy), P(262.0f, Ground), Gold, 1.0f);
        Surface.StrokeLine(P(256.0f, Cy), P(268.0f, Cy), Gold, 1.0f);
        Surface.StrokeLine(P(256.0f, Ground), P(268.0f, Ground), Gold, 1.0f);
        char HBuf[24]{};
        std::snprintf(HBuf, sizeof(HBuf), "%.2f m", HeightMeters);
        const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, HBuf);
        const Point2D LabelPt = P(252.0f, (Cy + Ground) * 0.5f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LabelPt.X - TW, LabelPt.Y - 5.0f }, 9.5f, Gold, HBuf);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                           VERBATIM RENDER TYPE SPHERE & STUDIES (RenderComponents/Studies.jsx)
//------------------------------------------------------------------------------------------------------------------------

void ConstructRenderTypeSphere(
    PixelSpace&         Surface,
    Rectangle2D         Bounds,
    RenderTypeThumbKind Kind,
    bool                LargeHero) noexcept
{
    const float Scale = std::min(Bounds.Width / 72.0f, Bounds.Height / 52.0f);
    const Point2D Center { Bounds.Center().X, Bounds.Center().Y - (LargeHero ? 3.0f : 1.0f) };
    const float R = (LargeHero ? 15.5f : 13.5f) * Scale;
    const float GroundY = Center.Y + R * 1.02f;

    // Ground line + cast shadow / motif matching FollowUp68_RenderTypeCard.png
    Surface.StrokeLine({ Center.X - R * 2.1f, GroundY }, { Center.X + R * 2.1f, GroundY }, ColorRgba{ 255u, 255u, 255u, 22u }, 1.0f);
    FillEllipseVec(Surface, { Center.X + R * 0.12f, GroundY }, R * 0.85f, R * 0.22f, ColorRgba{ 0u, 0u, 0u, 115u }, 24u);

    if (Kind == RenderTypeThumbKind::SdfConeGi)
    {
        // Concentric teal-mint SDF contour rings on ground
        StrokeEllipseVec(Surface, { Center.X + R * 0.18f, GroundY }, R * 0.72f, R * 0.19f, ColorRgba{ 123u, 224u, 173u, 145u }, 1.1f, 32u);
        StrokeEllipseVec(Surface, { Center.X + R * 0.18f, GroundY }, R * 1.15f, R * 0.30f, ColorRgba{ 123u, 224u, 173u, 85u }, 1.0f, 36u);
    }
    else if (Kind == RenderTypeThumbKind::RayTraced)
    {
        // Dashed diagonal ray-tracing bounce lines
        const ColorRgba RayInk { 201u, 184u, 255u, 165u };
        StrokeDashedLine(Surface, { Center.X - R * 1.75f, Center.Y - R * 1.05f }, { Center.X - R * 0.55f, Center.Y - R * 0.35f }, RayInk, 1.1f, 3.0f, 3.0f);
        StrokeDashedLine(Surface, { Center.X + R * 0.65f, Center.Y + R * 0.45f }, { Center.X + R * 1.80f, Center.Y + R * 1.05f }, RayInk, 1.1f, 3.0f, 3.0f);
    }

    // Concentric radial-gradient lit sphere (pure vector, zero CPU pixel loops!)
    ColorRgba ShadowCol{}, MidCol{}, SpecCol{};
    switch (Kind)
    {
    case RenderTypeThumbKind::PbrRaster:
        ShadowCol = { 34u,  54u,  82u,  255u };
        MidCol    = { 126u, 178u, 236u, 255u };
        SpecCol   = { 244u, 250u, 255u, 255u };
        break;
    case RenderTypeThumbKind::SdfConeGi:
        ShadowCol = { 28u,  68u,  54u,  255u };
        MidCol    = { 122u, 214u, 164u, 255u };
        SpecCol   = { 236u, 255u, 245u, 255u };
        break;
    case RenderTypeThumbKind::RayTraced:
        ShadowCol = { 54u,  44u,  86u,  255u };
        MidCol    = { 174u, 154u, 238u, 255u };
        SpecCol   = { 250u, 246u, 255u, 255u };
        break;
    case RenderTypeThumbKind::MatCap:
        ShadowCol = { 61u,  56u,  51u,  255u };
        MidCol    = { 169u, 157u, 143u, 255u };
        SpecCol   = { 241u, 231u, 218u, 255u };
        break;
    }

    const int32_t Layers = LargeHero ? 14 : 9;
    const Point2D Highlight { Center.X - R * 0.30f, Center.Y - R * 0.32f };
    for (int32_t I = 0; I < Layers; ++I)
    {
        const float T = static_cast<float>(I) / static_cast<float>(Layers - 1);
        const float LayerR = R * (1.0f - T * 0.84f);
        const Point2D C {
            Center.X + (Highlight.X - Center.X) * (T * 0.92f),
            Center.Y + (Highlight.Y - Center.Y) * (T * 0.92f),
        };
        const ColorRgba Col = (T < 0.55f) ? MixRgba(ShadowCol, MidCol, T / 0.55f) : MixRgba(MidCol, SpecCol, (T - 0.55f) / 0.45f);
        Surface.FillCircle(C, LayerR, Col);
    }
}

float ConstructRenderBudgetBar(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       Width,
    uint32_t    ShadowSamples,
    uint32_t    GiSamples,
    uint32_t    ReflectionSamples) noexcept
{
    const Rectangle2D Shell { LeftX, TopY, Width, 48.0f };
    Surface.FillRectangle(Shell, ColorRgba{ 18u, 19u, 22u, 255u }, 12.0f);
    Surface.StrokeRectangle(Shell, ColorRgba{ 255u, 255u, 255u, 12u }, 12.0f, 1.0f);

    const float BarX = Shell.X + 12.0f;
    const float BarY = Shell.Y + 11.0f;
    const float BarW = Shell.Width - 24.0f;
    const float BarH = 9.0f;
    Surface.FillRectangle({ BarX, BarY, BarW, BarH }, ColorRgba{ 32u, 33u, 38u, 255u }, 4.5f);

    const uint32_t Total = std::max(1u, ShadowSamples + GiSamples + ReflectionSamples);
    const ColorRgba Blue   { 122u, 184u, 255u, 255u }; // #7ab8ff
    const ColorRgba Green  { 123u, 224u, 173u, 255u }; // #7be0ad
    const ColorRgba Purple { 197u, 163u, 255u, 255u }; // #c5a3ff

    float CurX = BarX;
    auto DrawSeg = [&](uint32_t Count, ColorRgba Fill) noexcept
    {
        if (Count == 0u) return;
        const float W = std::max(6.0f, BarW * (static_cast<float>(Count) / static_cast<float>(Total)) - 2.0f);
        Surface.FillRectangle({ CurX, BarY, std::min(W, BarX + BarW - CurX), BarH }, Fill, 4.5f);
        CurX += W + 2.0f;
    };
    DrawSeg(ShadowSamples, Blue);
    DrawSeg(GiSamples, Green);
    DrawSeg(ReflectionSamples, Purple);

    const float LegY = Shell.Y + 29.0f;
    const float ColW = BarW / 3.0f;
    char B0[24]{}, B1[24]{}, B2[24]{};
    std::snprintf(B0, sizeof(B0), "Shadows %u", ShadowSamples);
    std::snprintf(B1, sizeof(B1), "GI %u", GiSamples);
    std::snprintf(B2, sizeof(B2), "Reflections %u", ReflectionSamples);
    auto DrawLegend = [&](float X, ColorRgba Dot, const char* Txt, bool Dim) noexcept
    {
        Surface.FillCircle({ X + 3.5f, LegY + 5.0f }, 3.0f, Dim ? Dot.WithAlpha(85u) : Dot);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { X + 10.0f, LegY }, 9.0f, Dim ? InspectorMetrics::DimInk : InspectorMetrics::MutedInk, Txt);
    };
    DrawLegend(BarX,            Blue,   B0, ShadowSamples == 0u);
    DrawLegend(BarX + ColW,     Green,  B1, GiSamples == 0u);
    DrawLegend(BarX + ColW * 2, Purple, B2, ReflectionSamples == 0u);

    return Shell.Bottom() + 10.0f;
}

void ConstructPenumbraStudy(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    float       Softness01,
    uint32_t    ShadowMapRes,
    uint32_t    RayCount,
    bool        UsesShadowMap) noexcept
{
    Surface.FillRectangle(Bounds, ColorRgba{ 22u, 22u, 22u, 255u }, 14.0f);
    Surface.StrokeRectangle(Bounds, ColorRgba{ 255u, 255u, 255u, 10u }, 14.0f, 1.0f);

    const float Sx = Bounds.Width / 300.0f;
    const float Sy = Bounds.Height / 132.0f;
    auto P = [&](float X, float Y) noexcept -> Point2D { return { Bounds.X + X * Sx, Bounds.Y + Y * Sy }; };

    const float Ground = 108.0f, Light = 30.0f, Blocker = 66.0f;
    const float Reach  = (Ground - Light) / (Blocker - Light);
    const float Width  = 14.0f + 34.0f * std::clamp(Softness01, 0.0f, 1.0f);
    const float Ll = 150.0f - Width * 0.5f, Lr = 150.0f + Width * 0.5f;
    const float O1 = 118.0f, O2 = 182.0f;
    auto At = [&](float L, float O) noexcept { return L + (O - L) * Reach; };
    const float OuterL = At(Lr, O1), InnerL = At(Ll, O1), InnerR = At(Lr, O2), OuterR = At(Ll, O2);
    const float UmbraL = std::min(InnerL, (InnerL + InnerR) * 0.5f);
    const float UmbraR = std::max(UmbraL, InnerR);

    Surface.FillRectangle({ P(Ll, Light - 3.0f).X, P(Ll, Light - 3.0f).Y, Width * Sx, 6.0f * Sy }, ColorRgba{ 242u, 226u, 184u, 255u }, 3.0f);
    const ColorRgba Guide { 255u, 255u, 255u, 34u };
    StrokeDashedLine(Surface, P(Lr, Light), P(OuterL, Ground), Guide, 0.8f, 2.0f, 3.0f);
    StrokeDashedLine(Surface, P(Ll, Light), P(InnerL, Ground), Guide, 0.8f, 2.0f, 3.0f);
    StrokeDashedLine(Surface, P(Ll, Light), P(OuterR, Ground), Guide, 0.8f, 2.0f, 3.0f);
    StrokeDashedLine(Surface, P(Lr, Light), P(InnerR, Ground), Guide, 0.8f, 2.0f, 3.0f);

    Surface.FillRectangle({ P(O1, Blocker - 3.0f).X, P(O1, Blocker - 3.0f).Y, (O2 - O1) * Sx, 6.0f * Sy }, ColorRgba{ 90u, 90u, 90u, 255u }, 2.0f);
    if (InnerL > OuterL)
        Surface.FillRectangle({ P(OuterL, Ground - 7.0f).X, P(OuterL, Ground - 7.0f).Y, (InnerL - OuterL) * Sx, 7.0f * Sy }, ColorRgba{ 122u, 184u, 255u, 72u }, 0.0f);
    if (UmbraR > UmbraL)
        Surface.FillRectangle({ P(UmbraL, Ground - 7.0f).X, P(UmbraL, Ground - 7.0f).Y, (UmbraR - UmbraL) * Sx, 7.0f * Sy }, ColorRgba{ 122u, 184u, 255u, 130u }, 0.0f);
    if (OuterR > InnerR)
        Surface.FillRectangle({ P(InnerR, Ground - 7.0f).X, P(InnerR, Ground - 7.0f).Y, (OuterR - InnerR) * Sx, 7.0f * Sy }, ColorRgba{ 122u, 184u, 255u, 72u }, 0.0f);
    Surface.StrokeLine(P(8.0f, Ground), P(292.0f, Ground), ColorRgba{ 106u, 106u, 106u, 255u }, 1.0f);

    if (UsesShadowMap)
    {
        const float Texel = (ShadowMapRes <= 1024u) ? 12.0f : 6.0f;
        for (float X = 8.0f; X <= 292.0f; X += Texel)
        {
            Surface.StrokeLine(P(X, Ground), P(X, Ground + 4.0f), ColorRgba{ 255u, 255u, 255u, 38u }, 1.0f);
        }
    }
    const uint32_t ShownRays = std::min(12u, RayCount);
    const float ProbeX = (OuterL + InnerL) * 0.5f;
    for (uint32_t I = 0u; I < ShownRays; ++I)
    {
        const float TargetX = Ll + ((static_cast<float>(I) + 0.5f) / static_cast<float>(ShownRays)) * Width;
        Surface.StrokeLine(P(ProbeX, Ground), P(TargetX, Light), ColorRgba{ 122u, 184u, 255u, 140u }, 0.8f);
    }

    char TopBuf[48]{}, BotBuf[64]{};
    std::snprintf(TopBuf, sizeof(TopBuf), "area light · softness %.2f", Softness01);
    std::snprintf(BotBuf, sizeof(BotBuf), "penumbra %.0f px · %s", std::max(0.0f, InnerL - OuterL), UsesShadowMap ? "cascade shadow map" : "ray-traced visibility");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + 8.0f, Bounds.Y + 6.0f }, 9.5f, InspectorMetrics::MutedInk, TopBuf);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + 8.0f, Bounds.Bottom() - 16.0f }, 9.5f, InspectorMetrics::MutedInk, BotBuf);
}

void ConstructHemisphereStudy(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    uint32_t    Mode0Off1Sdf2Bvh,
    uint32_t    ConeOrSampleCount,
    float       FillBoost) noexcept
{
    Surface.FillRectangle(Bounds, ColorRgba{ 22u, 22u, 22u, 255u }, 14.0f);
    Surface.StrokeRectangle(Bounds, ColorRgba{ 255u, 255u, 255u, 10u }, 14.0f, 1.0f);

    const Point2D C { Bounds.Center().X, Bounds.Bottom() - 26.0f };
    const float R = std::min(Bounds.Width * 0.30f, Bounds.Height - 46.0f);
    StrokeArcVec(Surface, C, R, kPi, kTwoPi, ColorRgba{ 255u, 255u, 255u, 28u }, 1.0f, 32u);
    Surface.StrokeLine({ Bounds.X + 14.0f, C.Y }, { Bounds.Right() - 14.0f, C.Y }, ColorRgba{ 106u, 106u, 106u, 255u }, 1.0f);

    const uint32_t Count = std::clamp(ConeOrSampleCount, 1u, 16u);
    if (Mode0Off1Sdf2Bvh == 1u)
    {
        for (uint32_t I = 0u; I < Count; ++I)
        {
            const float Angle = kPi * ((static_cast<float>(I) + 0.5f) / static_cast<float>(Count));
            const float Half  = (kPi / static_cast<float>(Count)) * 0.36f;
            const Point2D A { C.X + std::cos(kPi - (Angle - Half)) * R * 0.94f, C.Y - std::sin(Angle - Half) * R * 0.94f };
            const Point2D B { C.X + std::cos(kPi - (Angle + Half)) * R * 0.94f, C.Y - std::sin(Angle + Half) * R * 0.94f };
            const std::array<Point2D, 3u> Tri = {{ C, A, B }};
            Surface.FillPolygon(Tri, ColorRgba{ 123u, 224u, 173u, 46u });
            Surface.StrokePolyline(Tri, ColorRgba{ 123u, 224u, 173u, 128u }, 0.8f, true);
        }
    }
    else if (Mode0Off1Sdf2Bvh == 2u)
    {
        for (uint32_t I = 0u; I < Count; ++I)
        {
            const float U = (static_cast<float>(I) + 0.5f) / static_cast<float>(Count);
            const float Phi = static_cast<float>(I) * 2.39996f;
            const float Sin = std::sqrt(U);
            const float Up  = std::sqrt(std::max(0.0f, 1.0f - U));
            const float Len = R * (0.55f + 0.40f * Up);
            Surface.StrokeLine(C, { C.X + Sin * std::cos(Phi) * Len, C.Y - Up * Len }, ColorRgba{ 123u, 224u, 173u, 190u }, 1.0f);
        }
    }
    Surface.FillCircle(C, 3.0f, ColorRgba{ 230u, 230u, 230u, 255u });

    char TopBuf[64]{}, BotBuf[32]{};
    if (Mode0Off1Sdf2Bvh == 1u) std::snprintf(TopBuf, sizeof(TopBuf), "%u cones through the distance field", Count);
    else if (Mode0Off1Sdf2Bvh == 2u) std::snprintf(TopBuf, sizeof(TopBuf), "%u samples / pixel · 2 bounces", Count);
    else std::snprintf(TopBuf, sizeof(TopBuf), "no tracing · sky and ambient fill only");
    std::snprintf(BotBuf, sizeof(BotBuf), "fill × %.2f", FillBoost);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + 10.0f, Bounds.Y + 6.0f }, 9.5f, InspectorMetrics::MutedInk, TopBuf);
    const float BW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, BotBuf);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.Right() - 10.0f - BW, Bounds.Bottom() - 16.0f }, 9.5f, InspectorMetrics::MutedInk, BotBuf);
}

void ConstructBounceStudy(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    bool        TracedReflections,
    uint32_t    BounceCount,
    uint32_t    RayCount) noexcept
{
    Surface.FillRectangle(Bounds, ColorRgba{ 22u, 22u, 22u, 255u }, 14.0f);
    Surface.StrokeRectangle(Bounds, ColorRgba{ 255u, 255u, 255u, 10u }, 14.0f, 1.0f);

    const float FloorY = Bounds.Bottom() - 26.0f;
    const float WallX  = Bounds.Right() - 42.0f;
    Surface.StrokeLine({ Bounds.X + 14.0f, FloorY }, { WallX, FloorY }, ColorRgba{ 106u, 106u, 106u, 255u }, 1.1f);
    Surface.StrokeLine({ WallX, Bounds.Y + 26.0f }, { WallX, FloorY }, ColorRgba{ 106u, 106u, 106u, 255u }, 1.1f);

    const Point2D Eye { Bounds.X + 26.0f, Bounds.Y + 34.0f };
    const Point2D Hit1 { Bounds.Center().X - 10.0f, FloorY };
    const Point2D Hit2 { WallX, Bounds.Center().Y + 4.0f };
    const Point2D Hit3 { Bounds.Center().X + 18.0f, Bounds.Y + 28.0f };
    const ColorRgba Purple { 197u, 163u, 255u, 230u };
    Surface.StrokeLine(Eye, Hit1, ColorRgba{ 230u, 230u, 230u, 165u }, 1.1f);
    if (TracedReflections)
    {
        Surface.StrokeLine(Hit1, Hit2, Purple, 1.5f);
        if (BounceCount >= 2u) Surface.StrokeLine(Hit2, Hit3, ColorRgba{ 197u, 163u, 255u, 155u }, 1.2f);
        for (uint32_t I = 1u; I < std::min(4u, RayCount); ++I)
        {
            const float Off = static_cast<float>(I) * 9.0f;
            Surface.StrokeLine(Hit1, { WallX, Hit2.Y - Off }, ColorRgba{ 197u, 163u, 255u, 90u }, 0.9f);
        }
    }
    else
    {
        StrokeDashedLine(Surface, Hit1, { WallX - 10.0f, Bounds.Y + 28.0f }, ColorRgba{ 197u, 163u, 255u, 110u }, 1.0f, 3.0f, 4.0f);
    }
    Surface.FillCircle(Hit1, 3.2f, Purple);

    char Buf[64]{};
    if (TracedReflections) std::snprintf(Buf, sizeof(Buf), "traced · %u rays × %u bounces", RayCount, BounceCount);
    else std::snprintf(Buf, sizeof(Buf), "sky / environment lookup · 0 rays");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + 10.0f, Bounds.Y + 6.0f }, 9.5f, InspectorMetrics::MutedInk, Buf);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  VERBATIM OPTICAL SCHEMATIC (OpticalGraphic.jsx)
//------------------------------------------------------------------------------------------------------------------------

void ConstructOpticalSchematic(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    float       FocalLengthMm,
    float       SensorWidthMm,
    float       AspectRatio,
    float       ApertureFStop) noexcept
{
    const float SensorX = Bounds.X + 24.0f;
    const float LensX   = Bounds.X + Bounds.Width * 0.37f;
    const float FarX    = Bounds.Right() - 22.0f;
    const float MidY    = Bounds.Y + (Bounds.Height - 20.0f) * 0.5f;

    const float HalfSensorH = 18.0f;
    const float ClampedF    = std::clamp(FocalLengthMm, 12.0f, 200.0f);
    const float FovRad      = 2.0f * std::atan((SensorWidthMm / std::max(0.5f, AspectRatio)) / (2.0f * ClampedF));
    const float ConeHalfH   = std::clamp(std::tan(FovRad * 0.5f) * (FarX - LensX) * 0.85f, 10.0f, Bounds.Height * 0.36f);
    const float IrisGap     = std::clamp(16.0f / std::max(1.2f, ApertureFStop), 2.5f, 11.0f);

    // Subtle purple FOV cone polygon
    const std::array<Point2D, 3u> Cone = {{
        { LensX, MidY }, { FarX, MidY - ConeHalfH }, { FarX, MidY + ConeHalfH }
    }};
    Surface.FillPolygon(Cone, ColorRgba{ 158u, 146u, 198u, 30u });
    Surface.StrokeLine({ LensX, MidY }, { FarX, MidY - ConeHalfH }, ColorRgba{ 168u, 158u, 204u, 175u }, 1.1f);
    Surface.StrokeLine({ LensX, MidY }, { FarX, MidY + ConeHalfH }, ColorRgba{ 168u, 158u, 204u, 175u }, 1.1f);
    Surface.StrokeLine({ FarX, MidY - ConeHalfH }, { FarX, MidY + ConeHalfH }, ColorRgba{ 210u, 206u, 224u, 180u }, 1.4f);

    // Back rays (sensor to lens)
    Surface.StrokeLine({ SensorX, MidY - HalfSensorH }, { LensX, MidY }, ColorRgba{ 158u, 146u, 198u, 110u }, 0.9f);
    Surface.StrokeLine({ SensorX, MidY + HalfSensorH }, { LensX, MidY }, ColorRgba{ 158u, 146u, 198u, 110u }, 0.9f);
    StrokeDashedLine(Surface, { SensorX, MidY }, { FarX, MidY }, ColorRgba{ 255u, 255u, 255u, 24u }, 0.8f, 2.5f, 4.0f);

    // Sensor plate + aperture blades
    Surface.FillRectangle({ SensorX - 2.0f, MidY - HalfSensorH, 4.0f, HalfSensorH * 2.0f }, ColorRgba{ 120u, 124u, 134u, 255u }, 1.0f);
    Surface.FillRectangle({ LensX - 2.0f, MidY - 22.0f, 4.0f, 22.0f - IrisGap }, ColorRgba{ 186u, 174u, 224u, 220u }, 1.0f);
    Surface.FillRectangle({ LensX - 2.0f, MidY + IrisGap, 4.0f, 22.0f - IrisGap }, ColorRgba{ 186u, 174u, 224u, 220u }, 1.0f);

    // Exact bottom captions matching FollowUp55_Camera-Overview.png
    const float BotY = Bounds.Bottom() - 14.0f;
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + 4.0f, BotY }, 9.0f, InspectorMetrics::DimInk, "Optical schematic · not to scale");
    char GateBuf[32]{};
    std::snprintf(GateBuf, sizeof(GateBuf), "%.1f mm / %.3f", SensorWidthMm, AspectRatio);
    const float GW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, GateBuf);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.Right() - 4.0f - GW, BotY }, 9.0f, InspectorMetrics::DimInk, GateBuf);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  VERBATIM 3-COLUMN TRANSFORM TABLE (TransformPanel.jsx)
//------------------------------------------------------------------------------------------------------------------------

float ConstructTransformTable(
    PixelSpace&                  Surface,
    float                        LeftX,
    float                        TopY,
    float                        InnerWidth,
    const char*                  HeadingTitle,
    const char*                  SpaceBadgeText,
    std::span<TransformTableRow> Rows,
    uint32_t                     ControlOriginId,
    TransformTableDragState&     InOutDragState,
    const PaneInputReading&      Input,
    int32_t&                     OutResetRowIndex,
    bool&                        OutMutated) noexcept
{
    OutResetRowIndex = -1;
    const Point2D Cur { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Clicked = Input.PrimaryPointer.Pressed;

    float Y = TopY;
    // Title left + uppercase space badge right
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX, Y }, 11.0f, InspectorMetrics::HeadingInk, HeadingTitle);
    if (SpaceBadgeText != nullptr && SpaceBadgeText[0] != '\0')
    {
        const float SW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.5f, SpaceBadgeText);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { LeftX + InnerWidth - SW, Y + 2.0f }, 8.5f, InspectorMetrics::DimInk, SpaceBadgeText);
    }
    Y += 24.0f;

    // Column header row: Component | X (red) | Y (green) | Z (blue)
    const float LabelColW = 74.0f;
    const float AxisGap   = 5.0f;
    const float CellW     = (InnerWidth - LabelColW - AxisGap * 2.0f) / 3.0f;
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX, Y }, 9.0f, InspectorMetrics::DimInk, "Component");
    static constexpr std::array<const char*, 3u> AxisNames = {{ "X", "Y", "Z" }};
    static constexpr std::array<ColorRgba, 3u>   AxisInks  = {{
        { 228u, 118u, 114u, 255u },
        { 126u, 208u, 146u, 255u },
        { 122u, 174u, 236u, 255u },
    }};
    for (uint32_t A = 0u; A < 3u; ++A)
    {
        const float Cx = LeftX + LabelColW + static_cast<float>(A) * (CellW + AxisGap) + CellW * 0.5f;
        const float AW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.5f, AxisNames[A]);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Cx - AW * 0.5f, Y }, 9.5f, AxisInks[A], AxisNames[A]);
    }
    Y += 18.0f;

    // Data rows (draggable numeric pills)
    for (std::size_t R = 0u; R < Rows.size(); ++R)
    {
        TransformTableRow& Row = Rows[R];
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { LeftX, Y + 1.0f }, LabelColW - 4.0f, 10.0f, InspectorMetrics::HeadingInk, Row.Title);
        if (Row.Sublabel != nullptr && Row.Sublabel[0] != '\0')
        {
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { LeftX, Y + 15.0f }, LabelColW - 4.0f, 8.2f, InspectorMetrics::DimInk, Row.Sublabel);
        }

        float* Vals[3] = { &Row.X, &Row.Y, &Row.Z };
        for (uint32_t A = 0u; A < 3u; ++A)
        {
            const uint32_t CellId = ControlOriginId + static_cast<uint32_t>(R) * 4u + A;
            const Rectangle2D Cell { LeftX + LabelColW + static_cast<float>(A) * (CellW + AxisGap), Y, CellW, 26.0f };
            const bool Hov  = Cell.Contains(Cur);
            const bool Drag = (InOutDragState.ActiveCellId == CellId);
            if (Clicked && Hov)
            {
                InOutDragState.ActiveCellId = CellId;
                InOutDragState.DragOriginX  = Cur.X;
                InOutDragState.StartValue   = *Vals[A];
            }
            if (Drag)
            {
                if (Input.PrimaryPointer.Down)
                {
                    const float Delta = Cur.X - InOutDragState.DragOriginX;
                    const float ScaleStep = Input.ShiftHeld ? (Row.Step * 0.25f) : Row.Step;
                    const float NextVal = std::clamp(InOutDragState.StartValue + Delta * ScaleStep, Row.MinValue, Row.MaxValue);
                    if (std::fabs(NextVal - *Vals[A]) > 1.0e-4f)
                    {
                        *Vals[A] = NextVal;
                        OutMutated = true;
                    }
                }
                else
                {
                    InOutDragState.ActiveCellId = 0u;
                }
            }

            Surface.FillRectangle(Cell, Drag ? ColorRgba{ 24u, 26u, 32u, 255u } : ColorRgba{ 12u, 12u, 14u, 255u }, 7.0f);
            Surface.StrokeRectangle(Cell, Drag ? AxisInks[A] : ColorRgba{ 255u, 255u, 255u, static_cast<uint8_t>(Hov ? 32u : 14u) }, 7.0f, 1.0f);
            char NumBuf[24]{};
            if (Row.Decimals <= 0) std::snprintf(NumBuf, sizeof(NumBuf), "%.0f", *Vals[A]);
            else if (Row.Decimals == 1) std::snprintf(NumBuf, sizeof(NumBuf), "%.1f", *Vals[A]);
            else std::snprintf(NumBuf, sizeof(NumBuf), "%.2f", *Vals[A]);
            const float NW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, NumBuf);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Cell.Center().X - NW * 0.5f, Cell.Y + 6.5f }, 10.5f, ColorRgba{ 238u, 238u, 240u, 255u }, NumBuf);
        }
        Y += 34.0f;
    }

    // Reset pill row (side-by-side "↺ Camera Position" | "↺ Camera Angle")
    if (!Rows.empty())
    {
        const float Gap = 6.0f;
        const float BtnW = (InnerWidth - Gap * static_cast<float>(Rows.size() - 1u)) / static_cast<float>(Rows.size());
        for (std::size_t R = 0u; R < Rows.size(); ++R)
        {
            const Rectangle2D Btn { LeftX + static_cast<float>(R) * (BtnW + Gap), Y, BtnW, 28.0f };
            const bool Hov = Btn.Contains(Cur);
            Surface.FillRectangle(Btn, Hov ? ColorRgba{ 42u, 42u, 46u, 255u } : ColorRgba{ 28u, 28u, 30u, 255u }, 14.0f);
            Surface.StrokeRectangle(Btn, ColorRgba{ 255u, 255u, 255u, static_cast<uint8_t>(Hov ? 34u : 16u) }, 14.0f, 1.0f);
            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.8f, Rows[R].Title);
            const float StartX = Btn.Center().X - (TW + 14.0f) * 0.5f;
            ConstructHtmlGlyph(Surface, HtmlGlyph::ResetArc, { StartX + 5.0f, Btn.Center().Y }, 10.0f, InspectorMetrics::MutedInk);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { StartX + 14.0f, Btn.Y + 8.0f }, 9.8f, InspectorMetrics::HeadingInk, Rows[R].Title);
            if (Hov && Clicked)
            {
                OutResetRowIndex = static_cast<int32_t>(R);
            }
        }
        Y += 36.0f;
    }

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX, Y }, 9.0f, InspectorMetrics::DimInk, "Type or drag values · Shift for precision.");
    return Y + 18.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                          PURE-VECTOR PHOTOMETRIC LIGHT BLUEPRINTS (LightPanel.js / LightProjection.js)
//------------------------------------------------------------------------------------------------------------------------

void ConstructLightBlueprint(
    PixelSpace&                 Surface,
    Rectangle2D                 Bounds,
    const LightBlueprintParams& Params) noexcept
{
    Surface.FillRectangle(Bounds, ColorRgba{ 22u, 22u, 24u, 255u }, 12.0f);
    Surface.StrokeRectangle(Bounds, ColorRgba{ 255u, 255u, 255u, 12u }, 12.0f, 1.0f);

    const ColorRgba Tint = InspectorPrimitives::EncodeLinearRgbToSrgb8(Params.TintRgb);
    const ColorRgba TintSoft = Tint.WithAlpha(52u);
    const ColorRgba TintLine = Tint.WithAlpha(195u);
    const ColorRgba DimLine  { 255u, 255u, 255u, 40u };

    switch (Params.Kind)
    {
    case LightBlueprintKind::Area:
    {
        // Exact FollowUp55_Light-Overview.png: "APERTURE / FRONT ELEVATION" dotted emitter grid + W/H dimension bars
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + 12.0f, Bounds.Y + 10.0f }, 8.5f, InspectorMetrics::DimInk, "APERTURE / FRONT ELEVATION");
        const float Aspect = std::clamp(Params.WidthMeters / std::max(0.1f, Params.HeightMeters), 0.35f, 3.2f);
        const float MaxW = Bounds.Width - 76.0f;
        const float MaxH = Bounds.Height - 62.0f;
        float DrawW = MaxW, DrawH = MaxW / Aspect;
        if (DrawH > MaxH) { DrawH = MaxH; DrawW = DrawH * Aspect; }
        const Rectangle2D Rect { Bounds.Center().X - DrawW * 0.5f, Bounds.Center().Y - DrawH * 0.5f + 2.0f, DrawW, DrawH };

        Surface.FillRectangle(Rect, Tint.WithAlpha(28u ), 1.0f);
        // Dotted emitter grid inside aperture
        const float Step = 8.0f;
        for (float Y = Rect.Y + 5.0f; Y < Rect.Bottom() - 3.0f; Y += Step)
        {
            for (float X = Rect.X + 5.0f; X < Rect.Right() - 3.0f; X += Step)
            {
                Surface.FillCircle({ X, Y }, 0.85f, Tint.WithAlpha(130u ));
            }
        }
        Surface.StrokeRectangle(Rect, TintLine, 1.0f, 1.3f);

        // Horizontal width dimension bar below
        const float DimY = Rect.Bottom() + 10.0f;
        Surface.StrokeLine({ Rect.X, DimY }, { Rect.Right(), DimY }, DimLine, 1.0f);
        Surface.StrokeLine({ Rect.X, DimY - 3.0f }, { Rect.X, DimY + 3.0f }, DimLine, 1.0f);
        Surface.StrokeLine({ Rect.Right(), DimY - 3.0f }, { Rect.Right(), DimY + 3.0f }, DimLine, 1.0f);
        char WBuf[24]{}, HBuf[24]{}, SpreadBuf[32]{};
        std::snprintf(WBuf, sizeof(WBuf), "%.1f m", Params.WidthMeters);
        std::snprintf(HBuf, sizeof(HBuf), "%.1f m", Params.HeightMeters);
        std::snprintf(SpreadBuf, sizeof(SpreadBuf), "%.0f° SPREAD", Params.SpreadDegrees);
        const float WW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.5f, WBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Rect.Center().X - WW * 0.5f, DimY + 4.0f }, 8.5f, InspectorMetrics::MutedInk, WBuf);

        // Vertical height dimension bar right
        const float DimX = Rect.Right() + 10.0f;
        Surface.StrokeLine({ DimX, Rect.Y }, { DimX, Rect.Bottom() }, DimLine, 1.0f);
        Surface.StrokeLine({ DimX - 3.0f, Rect.Y }, { DimX + 3.0f, Rect.Y }, DimLine, 1.0f);
        Surface.StrokeLine({ DimX - 3.0f, Rect.Bottom() }, { DimX + 3.0f, Rect.Bottom() }, DimLine, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { DimX + 5.0f, Rect.Center().Y - 4.0f }, 8.5f, InspectorMetrics::MutedInk, HBuf);

        // Bottom captions: "FRONT FACE" / "TWO-SIDED" left, "120° SPREAD" right
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + 12.0f, Bounds.Bottom() - 16.0f }, 8.2f, InspectorMetrics::DimInk, Params.TwoSided ? "TWO-SIDED" : "FRONT FACE");
        const float SW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.2f, SpreadBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.Right() - 12.0f - SW, Bounds.Bottom() - 16.0f }, 8.2f, InspectorMetrics::DimInk, SpreadBuf);
        break;
    }

    case LightBlueprintKind::Point:
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + 12.0f, Bounds.Y + 10.0f }, 8.5f, InspectorMetrics::DimInk, "RADIAL FALLOFF / OMNIDIRECTIONAL");
        const Point2D C { Bounds.Center().X, Bounds.Center().Y + 4.0f };
        const float MaxR = std::min(Bounds.Width, Bounds.Height) * 0.34f;
        for (int32_t I = 4; I >= 1; --I)
        {
            const float T = static_cast<float>(I) / 4.0f;
            Surface.FillCircle(C, MaxR * T, Tint.WithAlpha(static_cast<uint8_t>(18 + (4 - I) * 14) ));
            StrokeEllipseVec(Surface, C, MaxR * T, MaxR * T, DimLine, 0.9f, 36u);
        }
        for (int32_t I = 0; I < 8; ++I)
        {
            const float A = static_cast<float>(I) * (kPi * 0.25f);
            StrokeDashedLine(Surface, C, { C.X + MaxR * std::cos(A), C.Y + MaxR * std::sin(A) }, ColorRgba{ 255u, 255u, 255u, 28u }, 0.8f, 3.0f, 3.0f);
        }
        Surface.FillCircle(C, 6.0f, Tint);
        Surface.FillCircle(C, 3.2f, ColorRgba{ 255u, 255u, 255u, 240u });
        break;
    }

    case LightBlueprintKind::Spot:
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + 12.0f, Bounds.Y + 10.0f }, 8.5f, InspectorMetrics::DimInk, "BEAM CONE / SECTION");
        const Point2D Apex { Bounds.Center().X, Bounds.Y + 28.0f };
        const float FloorY = Bounds.Bottom() - 20.0f;
        const float ReachH = FloorY - Apex.Y;
        const float OuterHalf = std::min(Bounds.Width * 0.42f, std::tan(std::clamp(Params.SpreadDegrees * 0.5f, 4.0f, 75.0f) * kDegToRad) * ReachH);
        const float InnerHalf = std::min(OuterHalf * 0.95f, std::tan(std::clamp(Params.InnerDegrees * 0.5f, 1.0f, 70.0f) * kDegToRad) * ReachH);
        const std::array<Point2D, 3u> OuterTri = {{ Apex, { Apex.X - OuterHalf, FloorY }, { Apex.X + OuterHalf, FloorY } }};
        const std::array<Point2D, 3u> InnerTri = {{ Apex, { Apex.X - InnerHalf, FloorY }, { Apex.X + InnerHalf, FloorY } }};
        Surface.FillPolygon(OuterTri, Tint.WithAlpha(34u ));
        Surface.FillPolygon(InnerTri, Tint.WithAlpha(56u ));
        Surface.StrokePolyline(OuterTri, TintLine, 1.2f, false);
        StrokeDashedLine(Surface, Apex, { Apex.X - InnerHalf, FloorY }, TintLine, 1.0f, 3.0f, 3.0f);
        StrokeDashedLine(Surface, Apex, { Apex.X + InnerHalf, FloorY }, TintLine, 1.0f, 3.0f, 3.0f);
        Surface.StrokeLine({ Bounds.X + 16.0f, FloorY }, { Bounds.Right() - 16.0f, FloorY }, DimLine, 1.0f);
        Surface.FillCircle(Apex, 4.5f, Tint);
        break;
    }

    case LightBlueprintKind::Ies:
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + 12.0f, Bounds.Y + 10.0f }, 8.5f, InspectorMetrics::DimInk, "POLAR CANDELA WEB (LM-63)");
        const Point2D Origin { Bounds.Center().X, Bounds.Y + 34.0f };
        const float MaxR = Bounds.Height - 52.0f;
        for (int32_t R = 1; R <= 3; ++R)
        {
            StrokeArcVec(Surface, Origin, MaxR * (static_cast<float>(R) / 3.0f), 0.0f, kPi, DimLine, 0.85f, 28u);
        }
        Surface.StrokeLine({ Origin.X - MaxR, Origin.Y }, { Origin.X + MaxR, Origin.Y }, DimLine, 0.85f);
        Surface.StrokeLine(Origin, { Origin.X, Origin.Y + MaxR }, DimLine, 0.85f);
        std::array<Point2D, 37u> Lobe{};
        for (uint32_t I = 0u; I <= 36u; ++I)
        {
            const float Gamma = (static_cast<float>(I) / 36.0f) * kPi - kPi * 0.5f; // -90..+90 around nadir
            const float C = std::max(0.0f, std::cos(Gamma));
            float Gain = std::pow(C, 2.2f);
            if (Params.CountOrPreset == 1u) Gain = std::pow(C, 5.5f);
            else if (Params.CountOrPreset == 2u) Gain = std::pow(std::sin(std::fabs(Gamma) * 1.6f), 1.8f) * 0.72f + 0.25f * C;
            const float Rad = MaxR * std::clamp(Gain, 0.08f, 0.96f);
            Lobe[I] = { Origin.X + std::sin(Gamma) * Rad, Origin.Y + std::cos(Gamma) * Rad };
        }
        Surface.FillPolygon(Lobe, TintSoft);
        Surface.StrokePolyline(Lobe, TintLine, 1.5f, true);
        Surface.FillCircle(Origin, 3.5f, Tint);
        break;
    }

    case LightBlueprintKind::Tube:
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + 12.0f, Bounds.Y + 10.0f }, 8.5f, InspectorMetrics::DimInk, "LINEAR CAPSULE / ELEVATION");
        const float TubeW = Bounds.Width - 64.0f;
        const float TubeH = std::clamp(Params.HeightMeters * 220.0f, 10.0f, 26.0f);
        const Rectangle2D Glow { Bounds.Center().X - TubeW * 0.5f - 8.0f, Bounds.Center().Y - TubeH * 0.5f - 8.0f, TubeW + 16.0f, TubeH + 16.0f };
        const Rectangle2D Tube { Bounds.Center().X - TubeW * 0.5f, Bounds.Center().Y - TubeH * 0.5f, TubeW, TubeH };
        Surface.FillRectangle(Glow, Tint.WithAlpha(34u ), Glow.Height * 0.5f);
        Surface.FillRectangle(Tube, Tint.WithAlpha(175u ), Tube.Height * 0.5f);
        Surface.StrokeRectangle(Tube, Tint, Tube.Height * 0.5f, 1.2f);
        Surface.FillRectangle({ Tube.X + 6.0f, Tube.Center().Y - 1.5f, Tube.Width - 12.0f, 3.0f }, ColorRgba{ 255u, 255u, 255u, 210u }, 1.5f);
        break;
    }

    case LightBlueprintKind::Led:
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + 12.0f, Bounds.Y + 10.0f }, 8.5f, InspectorMetrics::DimInk, "SMD DIODE / EMISSION DOME");
        const Point2D Base { Bounds.Center().X, Bounds.Bottom() - 28.0f };
        const float DomeR = std::min(Bounds.Width * 0.30f, Bounds.Height - 54.0f);
        Surface.FillCircle({ Base.X, Base.Y - 4.0f }, DomeR * 0.72f, Tint.WithAlpha(38u ));
        StrokeArcVec(Surface, Base, DomeR, kPi, kTwoPi, TintLine, 1.4f, 28u);
        StrokeArcVec(Surface, Base, DomeR * 0.60f, kPi, kTwoPi, DimLine, 1.0f, 22u);
        Surface.FillRectangle({ Base.X - 24.0f, Base.Y, 48.0f, 10.0f }, ColorRgba{ 62u, 64u, 70u, 255u }, 2.0f);
        Surface.FillRectangle({ Base.X - 10.0f, Base.Y - 4.0f, 20.0f, 5.0f }, Tint, 1.5f);
        break;
    }

    case LightBlueprintKind::LedStrip:
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + 12.0f, Bounds.Y + 10.0f }, 8.5f, InspectorMetrics::DimInk, "RIBBON / DIODE PITCH");
        const float StripW = Bounds.Width - 44.0f;
        const Rectangle2D Ribbon { Bounds.Center().X - StripW * 0.5f, Bounds.Center().Y - 9.0f, StripW, 18.0f };
        Surface.FillRectangle(Ribbon, ColorRgba{ 36u, 37u, 42u, 255u }, 4.0f);
        Surface.StrokeRectangle(Ribbon, DimLine, 4.0f, 1.0f);
        const uint32_t Nodes = std::clamp(Params.CountOrPreset > 0u ? Params.CountOrPreset : 8u, 4u, 14u);
        for (uint32_t I = 0u; I < Nodes; ++I)
        {
            const float U = (static_cast<float>(I) + 0.5f) / static_cast<float>(Nodes);
            const Point2D Dp { Ribbon.X + Ribbon.Width * U, Ribbon.Center().Y };
            Surface.FillCircle(Dp, 8.5f, Tint.WithAlpha(34u ));
            Surface.FillRectangle({ Dp.X - 3.0f, Dp.Y - 3.0f, 6.0f, 6.0f }, Tint, 1.0f);
        }
        break;
    }

    case LightBlueprintKind::Environment:
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + 12.0f, Bounds.Y + 10.0f }, 8.5f, InspectorMetrics::DimInk, "HEMISPHERE / ZENITH TO GROUND");
        const Point2D C { Bounds.Center().X, Bounds.Center().Y + 12.0f };
        const float R = std::min(Bounds.Width * 0.34f, Bounds.Height - 48.0f);
        Surface.FillCircle({ C.X, C.Y - R * 0.25f }, R * 0.72f, Tint.WithAlpha(36u ));
        StrokeArcVec(Surface, C, R, kPi, kTwoPi, TintLine, 1.5f, 32u);
        StrokeArcVec(Surface, C, R * 0.55f, kPi, kTwoPi, DimLine, 1.0f, 24u);
        Surface.StrokeLine({ C.X - R * 1.15f, C.Y }, { C.X + R * 1.15f, C.Y }, ColorRgba{ 130u, 134u, 142u, 190u }, 1.2f);
        StrokeEllipseVec(Surface, C, R, R * 0.22f, DimLine, 1.0f, 32u);
        break;
    }
    }
}

} // namespace Frontier::InspectorVector
