/*============================================================================================================================================
                                                           UVPANEAPPEARANCE.H
============================================================================================================================================*/
// 🧩 Shared ControlCentre ThemeRepository color set synchronization, typography controls & crisp vector vector icons for UVOutliner, UVViewport & UVInspector

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace Frontier::Workspaces::UVAppearance
{

//------------------------------------------------------------------------------------------------------------------------
//                                           CONTROLCENTRE THEME color set SNAPSHOT
//------------------------------------------------------------------------------------------------------------------------

struct UVThemeColors
{
    ColorRgba PanelBg       {}; // Pane background (--bg-panel, live from ControlCentre ThemeRepository)
    ColorRgba SectionBg     {}; // Rounded inspector/KPI container surface (#202020 in Dark)
    ColorRgba WellBg        {}; // Recessed input/tab-track well (#141414 in Dark)
    ColorRgba RaisedBg      {}; // Active segment / secondary capsule surface (#2a2a2a in Dark)
    ColorRgba HoverBg       {}; // Hovered control fill
    ColorRgba BorderSubtle  {}; // Hairline border (rgba(255,255,255,0.06) in Dark)
    ColorRgba BorderStrong  {}; // Emphasized control border
    ColorRgba PrimaryFill   {}; // Primary command button fill (#dededb in Dark)
    ColorRgba PrimaryHover  {}; // Hovered primary command fill
    ColorRgba PrimaryInk    {}; // Ink on primary command button (#181818 in Dark)
    ColorRgba AccentFill    {}; // Active ControlCentre accent colour
    ColorRgba AccentSoft    {}; // Translucent accent wash for selected capsules/rows
    ColorRgba AccentBorder  {}; // Emphasized accent stroke
    ColorRgba AccentInk     {}; // Ink on solid accent fill
    ColorRgba TextPrimary      {}; // Primary crisp foreground text
    ColorRgba TextMuted     {}; // Secondary label ink
    ColorRgba TextFaint     {}; // Tertiary hint / shortcut ink
    ColorRgba CautionInk    { 219, 171,  92, 255 }; // #dbab5c stretch caution
    ColorRgba CautionSoft   { 219, 171,  92,  36 };
    ColorRgba DangerInk     { 217, 108, 108, 255 }; // #d96c6c overlap alert
    ColorRgba DangerSoft    { 217, 108, 108,  36 };
    ColorRgba ReadyGreen    { 121, 189, 143, 255 }; // #79bd8f status green
    bool      LightSurface  = false;
};

[[nodiscard]] inline ColorRgba MixColors(ColorQuad A, ColorQuad B, float T, float AlphaScale = 1.0f) noexcept
{
    ColorQuad M = ColorQuad::Blend(A, B, std::clamp(T, 0.0f, 1.0f));
    M.Alpha     = std::clamp(M.Alpha * AlphaScale, 0.0f, 1.0f);
    return M.ToRgba8();
}

[[nodiscard]] inline UVThemeColors QueryThemeColors() noexcept
{
    const ControlColorSet& C = ControlAppearance::Colors();
    UVThemeColors Out {};
    Out.LightSurface = C.LightSurface;
    Out.PanelBg      = WorkspaceAppearance::QueryDockedPaneBackground();
    Out.SectionBg    = C.SectionSurface.ToRgba8();
    Out.WellBg       = MixColors(C.Field, C.Panel, 0.35f);
    Out.RaisedBg     = C.Selected.ToRgba8();
    Out.HoverBg      = MixColors(C.Selected, C.Text, C.LightSurface ? 0.08f : 0.10f);
    Out.BorderSubtle = C.Stroke.ToRgba8();
    Out.BorderStrong = MixColors(C.StrokeStrong, C.Text, 0.14f);
    Out.PrimaryFill  = C.LightSurface ? C.Accent.ToRgba8() : ColorQuad::FromHex(0xDEDEDBu).ToRgba8();
    Out.PrimaryHover = C.LightSurface ? MixColors(C.Accent, ColorQuad::FromHex(0xFFFFFFu), 0.18f) : ColorQuad::FromHex(0xF2F2EFu).ToRgba8();
    Out.PrimaryInk   = C.LightSurface ? C.AccentInk.ToRgba8() : ColorQuad::FromHex(0x181818u).ToRgba8();
    Out.AccentFill   = C.Accent.ToRgba8();
    Out.AccentSoft   = MixColors(C.SectionSurface, C.Accent, C.LightSurface ? 0.16f : 0.24f);
    Out.AccentBorder = MixColors(C.StrokeStrong, C.Accent, 0.68f);
    Out.AccentInk    = C.AccentInk.ToRgba8();
    Out.TextPrimary     = C.Text.ToRgba8();
    Out.TextMuted    = C.TextDim.ToRgba8();
    Out.TextFaint    = C.TextFaint.ToRgba8();
    return Out;
}

[[nodiscard]] inline ColorRgba LinearTintToRgba(Mathematics::Scalar3D Tint, uint8_t Alpha = 255u) noexcept
{
    return ColorRgba{
        static_cast<uint8_t>(std::clamp(Tint.X * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(Tint.Y * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(Tint.Z * 255.0f, 0.0f, 255.0f)),
        Alpha,
    };
}

[[nodiscard]] inline std::string FormatCountWithCommas(std::size_t Count)
{
    const std::string Raw = std::to_string(Count);
    std::string       Out {};
    Out.reserve(Raw.size() + Raw.size() / 3u);
    for (std::size_t Idx = 0u; Idx < Raw.size(); ++Idx)
    {
        if (Idx > 0u && ((Raw.size() - Idx) % 3u) == 0u)
        {
            Out.push_back(',');
        }
        Out.push_back(Raw[Idx]);
    }
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                         REUSABLE CAPSULE, SLIDER & SWITCH WIDGETS
//------------------------------------------------------------------------------------------------------------------------

inline bool ConstructCapsuleButton(
    PixelSpace&          Surface,
    Rectangle2D          Rect,
    std::string_view     Label,
    const UVThemeColors& Theme,
    Point2D              Cursor,
    bool                 Pressed,
    bool                 Active       = false,
    bool                 PrimaryStyle = false,
    float                FontPx       = 10.0f,
    float                RadiusOverride = -1.0f)
{
    const bool  Hovered = Rect.Contains(Cursor);
    const float Radius  = (RadiusOverride >= 0.0f) ? RadiusOverride : (Rect.Height * 0.5f);

    ColorRgba Fill   = Theme.WellBg;
    ColorRgba Border = Theme.BorderSubtle;
    ColorRgba Ink    = Theme.TextMuted;

    if (PrimaryStyle)
    {
        Fill   = Hovered ? Theme.PrimaryHover : Theme.PrimaryFill;
        Border = Fill;
        Ink    = Theme.PrimaryInk;
    }
    else if (Active)
    {
        Fill   = Theme.AccentSoft;
        Border = Theme.AccentBorder;
        Ink    = Theme.TextPrimary;
    }
    else if (Hovered)
    {
        Fill   = Theme.HoverBg;
        Border = Theme.BorderStrong;
        Ink    = Theme.TextPrimary;
    }
    else
    {
        Fill   = Theme.RaisedBg;
        Border = Theme.BorderSubtle;
        Ink    = Theme.TextPrimary;
    }

    Surface.FillRectangle(Rect, Fill, Radius);
    Surface.StrokeRectangle(Rect, Border, Radius, 1.0f);

    const float TextW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, FontPx, Label);
    const float TextX = Rect.X + std::max(6.0f, (Rect.Width - TextW) * 0.5f);
    const float TextY = Rect.Y + (Rect.Height - FontPx * 1.25f) * 0.5f;
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { TextX, TextY },
        std::max(8.0f, Rect.Width - 10.0f),
        FontPx,
        Ink,
        Label);

    return Hovered && Pressed;
}

inline bool ConstructSectionHeading(
    PixelSpace&          Surface,
    float                LeftX,
    float                TopY,
    float                Width,
    std::string_view     Title,
    const UVThemeColors& Theme,
    std::string_view     RightTag = {})
{
    Surface.FillRectangle({ LeftX, TopY + 2.0f, 3.0f, 14.0f }, Theme.AccentFill, 1.5f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX + 10.0f, TopY },
        12.0f,
        Theme.TextPrimary,
        Title);

    if (!RightTag.empty())
    {
        const float TagW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.5f, RightTag) + 12.0f;
        const Rectangle2D TagRect{ LeftX + Width - TagW, TopY - 1.0f, TagW, 18.0f };
        Surface.FillRectangle(TagRect, Theme.WellBg, 9.0f);
        Surface.StrokeRectangle(TagRect, Theme.BorderStrong, 9.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { TagRect.X + 6.0f, TagRect.Y + 3.5f },
            8.5f,
            Theme.TextMuted,
            RightTag);
    }
    return false;
}

inline bool ConstructSliderControl(
    PixelSpace&          Surface,
    Rectangle2D          RowRect,
    std::string_view     Label,
    std::string_view     FormattedReading,
    float&               InOutNormalized,
    const UVThemeColors& Theme,
    Point2D              Cursor,
    bool                 PointerDown,
    bool                 PointerPressed,
    ColorRgba            FillTintOverride = ColorRgba{ 0, 0, 0, 0 })
{
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { RowRect.X, RowRect.Y + 1.0f },
        10.0f,
        Theme.TextMuted,
        Label);

    const float ValW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, FormattedReading);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { RowRect.Right() - ValW, RowRect.Y + 1.0f },
        10.0f,
        Theme.TextPrimary,
        FormattedReading);

    const Rectangle2D Track{ RowRect.X, RowRect.Y + 18.0f, RowRect.Width, 8.0f };
    const Rectangle2D HitBox{ Track.X - 4.0f, Track.Y - 5.0f, Track.Width + 8.0f, 18.0f };
    const bool        Hovered = HitBox.Contains(Cursor);

    Surface.FillRectangle(Track, Theme.WellBg, 4.0f);
    Surface.StrokeRectangle(Track, Theme.BorderSubtle, 4.0f, 1.0f);

    bool Changed = false;
    if (Hovered && (PointerPressed || PointerDown))
    {
        const float NextNorm = std::clamp((Cursor.X - Track.X) / std::max(1.0f, Track.Width), 0.0f, 1.0f);
        if (std::abs(NextNorm - InOutNormalized) > 1.0e-4f)
        {
            InOutNormalized = NextNorm;
            Changed         = true;
        }
    }

    const float     Clamped = std::clamp(InOutNormalized, 0.0f, 1.0f);
    const ColorRgba BarFill = (FillTintOverride.Alpha > 0u) ? FillTintOverride : Theme.AccentFill;
    if (Clamped > 0.005f)
    {
        Surface.FillRectangle({ Track.X, Track.Y, std::max(6.0f, Track.Width * Clamped), Track.Height }, BarFill, 4.0f);
    }

    const float KnobX = Track.X + Track.Width * Clamped;
    Surface.FillCircle({ KnobX, Track.Y + 4.0f }, Hovered ? 5.8f : 5.0f, Theme.PrimaryFill);
    Surface.StrokeCircle({ KnobX, Track.Y + 4.0f }, Hovered ? 5.8f : 5.0f, ColorRgba{ 0, 0, 0, 120 }, 1.0f);
    return Changed;
}

inline bool ConstructToggleRow(
    PixelSpace&          Surface,
    Rectangle2D          RowRect,
    std::string_view     Label,
    bool&                InOutEnabled,
    const UVThemeColors& Theme,
    Point2D              Cursor,
    bool                 Pressed)
{
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { RowRect.X, RowRect.Y + 4.0f },
        10.5f,
        Theme.TextPrimary,
        Label);

    const Rectangle2D TrackCapsule{ RowRect.Right() - 34.0f, RowRect.Y + 2.0f, 34.0f, 18.0f };
    const bool        Hovered = RowRect.Contains(Cursor);

    Surface.FillRectangle(TrackCapsule, InOutEnabled ? Theme.AccentFill : Theme.WellBg, 9.0f);
    Surface.StrokeRectangle(TrackCapsule, InOutEnabled ? Theme.AccentFill : Theme.BorderStrong, 9.0f, 1.0f);
    const float KnobX = InOutEnabled ? (TrackCapsule.Right() - 9.0f) : (TrackCapsule.X + 9.0f);
    Surface.FillCircle({ KnobX, TrackCapsule.Y + 9.0f }, 6.5f, InOutEnabled ? Theme.AccentInk : Theme.TextMuted);

    if (Hovered && Pressed)
    {
        InOutEnabled = !InOutEnabled;
        return true;
    }
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  CRISP STUDIO VECTOR vector icons (MATCHING STUDIOCONTROLS.JS)
//------------------------------------------------------------------------------------------------------------------------

inline void ConstructSceneCubeIcon(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink)
{
    const float CX = Origin.X + Size * 0.5f;
    const float CY = Origin.Y + Size * 0.5f;
    const float R  = Size * 0.42f;
    const Point2D Top  { CX, CY - R };
    const Point2D TR   { CX + R * 0.86f, CY - R * 0.48f };
    const Point2D BR   { CX + R * 0.86f, CY + R * 0.48f };
    const Point2D Bot  { CX, CY + R };
    const Point2D BL   { CX - R * 0.86f, CY + R * 0.48f };
    const Point2D TL   { CX - R * 0.86f, CY - R * 0.48f };
    const Point2D Mid  { CX, CY };

    Surface.StrokeLine(Top, TR, Ink, 1.35f);
    Surface.StrokeLine(TR,  BR, Ink, 1.35f);
    Surface.StrokeLine(BR,  Bot, Ink, 1.35f);
    Surface.StrokeLine(Bot, BL, Ink, 1.35f);
    Surface.StrokeLine(BL,  TL, Ink, 1.35f);
    Surface.StrokeLine(TL,  Top, Ink, 1.35f);
    Surface.StrokeLine(TL,  Mid, Ink, 1.25f);
    Surface.StrokeLine(TR,  Mid, Ink, 1.25f);
    Surface.StrokeLine(Bot, Mid, Ink, 1.25f);
}

inline void ConstructPackGridIcon(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink)
{
    const float S = Size;
    Surface.StrokeRectangle({ Origin.X + S * 0.08f, Origin.Y + S * 0.08f, S * 0.44f, S * 0.44f }, Ink, 1.5f, 1.3f);
    Surface.StrokeRectangle({ Origin.X + S * 0.60f, Origin.Y + S * 0.08f, S * 0.32f, S * 0.32f }, Ink, 1.2f, 1.3f);
    Surface.StrokeRectangle({ Origin.X + S * 0.08f, Origin.Y + S * 0.60f, S * 0.32f, S * 0.32f }, Ink, 1.2f, 1.3f);
    Surface.StrokeRectangle({ Origin.X + S * 0.48f, Origin.Y + S * 0.48f, S * 0.44f, S * 0.44f }, Ink, 1.5f, 1.3f);
}

inline void ConstructComponentModeIcon(PixelSpace& Surface, Point2D Origin, float Size, uint32_t ModeIdx, ColorRgba Ink)
{
    const float L = Origin.X + Size * 0.18f;
    const float R = Origin.X + Size * 0.82f;
    const float T = Origin.Y + Size * 0.18f;
    const float B = Origin.Y + Size * 0.82f;
    if (ModeIdx == 0u) // Vertex
    {
        Surface.FillCircle({ L, T }, 1.9f, Ink);
        Surface.FillCircle({ R, T }, 1.9f, Ink);
        Surface.FillCircle({ L, B }, 1.9f, Ink);
        Surface.FillCircle({ R, B }, 1.9f, Ink);
    }
    else if (ModeIdx == 1u) // Edge
    {
        Surface.StrokeLine({ L, B }, { R, T }, Ink, 1.6f);
        Surface.FillCircle({ L, B }, 1.8f, Ink);
        Surface.FillCircle({ R, T }, 1.8f, Ink);
    }
    else if (ModeIdx == 2u) // Face
    {
        ColorRgba Soft = Ink;
        Soft.Alpha     = 72u;
        Surface.FillRectangle({ L, T, R - L, B - T }, Soft, 1.5f);
        Surface.StrokeRectangle({ L, T, R - L, B - T }, Ink, 1.5f, 1.3f);
    }
    else // Island
    {
        Surface.StrokeRectangle({ Origin.X + Size * 0.12f, Origin.Y + Size * 0.14f, Size * 0.42f, Size * 0.42f }, Ink, 1.5f, 1.25f);
        Surface.StrokeRectangle({ Origin.X + Size * 0.46f, Origin.Y + Size * 0.44f, Size * 0.42f, Size * 0.42f }, Ink, 1.5f, 1.25f);
    }
}

inline void ConstructRailToolIcon(PixelSpace& Surface, Point2D Origin, float Size, uint32_t ToolSlot, ColorRgba Ink)
{
    const float CX = Origin.X + Size * 0.5f;
    const float CY = Origin.Y + Size * 0.5f;
    switch (ToolSlot)
    {
        case 0u: // Cursor Select
            Surface.StrokeLine({ Origin.X + 4.0f, Origin.Y + 2.5f }, { Origin.X + 4.0f, Origin.Y + Size - 3.0f }, Ink, 1.45f);
            Surface.StrokeLine({ Origin.X + 4.0f, Origin.Y + 2.5f }, { Origin.X + Size - 3.0f, Origin.Y + Size * 0.62f }, Ink, 1.45f);
            Surface.StrokeLine({ Origin.X + 4.0f, Origin.Y + Size - 3.0f }, { Origin.X + Size * 0.52f, Origin.Y + Size * 0.68f }, Ink, 1.35f);
            Surface.StrokeLine({ Origin.X + Size - 3.0f, Origin.Y + Size * 0.62f }, { Origin.X + Size * 0.52f, Origin.Y + Size * 0.68f }, Ink, 1.35f);
            break;
        case 1u: // Box Marquee
            Surface.StrokeRectangle({ Origin.X + 2.5f, Origin.Y + 2.5f, Size - 5.0f, Size - 5.0f }, Ink, 2.0f, 1.3f);
            break;
        case 2u: // Circle Brush
            Surface.StrokeCircle({ CX, CY }, Size * 0.36f, Ink, 1.35f);
            Surface.FillCircle({ CX, CY }, 1.4f, Ink);
            break;
        case 3u: // Move (G)
            Surface.StrokeLine({ CX, Origin.Y + 2.0f }, { CX, Origin.Y + Size - 2.0f }, Ink, 1.35f);
            Surface.StrokeLine({ Origin.X + 2.0f, CY }, { Origin.X + Size - 2.0f, CY }, Ink, 1.35f);
            break;
        case 4u: // Rotate (R)
            Surface.StrokeCircle({ CX, CY }, Size * 0.34f, Ink, 1.35f);
            Surface.FillCircle({ CX + Size * 0.34f, CY }, 1.8f, Ink);
            break;
        case 5u: // Scale (S)
            Surface.StrokeRectangle({ Origin.X + 2.5f, Origin.Y + 5.5f, Size - 8.0f, Size - 8.0f }, Ink, 1.5f, 1.25f);
            Surface.StrokeLine({ Origin.X + 5.0f, Origin.Y + Size - 5.0f }, { Origin.X + Size - 2.5f, Origin.Y + 2.5f }, Ink, 1.35f);
            break;
        case 6u: // Split / Scissors
            Surface.StrokeCircle({ Origin.X + 4.2f, Origin.Y + 4.5f }, 2.2f, Ink, 1.25f);
            Surface.StrokeCircle({ Origin.X + 4.2f, Origin.Y + Size - 4.5f }, 2.2f, Ink, 1.25f);
            Surface.StrokeLine({ Origin.X + 6.0f, Origin.Y + 5.5f }, { Origin.X + Size - 2.5f, Origin.Y + Size - 3.5f }, Ink, 1.35f);
            Surface.StrokeLine({ Origin.X + 6.0f, Origin.Y + Size - 5.5f }, { Origin.X + Size - 2.5f, Origin.Y + 3.5f }, Ink, 1.35f);
            break;
        case 7u: // Seam Brush
            Surface.StrokeLine({ Origin.X + 3.0f, Origin.Y + Size - 3.0f }, { Origin.X + Size - 3.0f, Origin.Y + 3.0f }, Ink, 1.65f);
            Surface.FillCircle({ CX, CY }, 2.0f, Ink);
            break;
        case 8u: // Stitch / Weld
            Surface.StrokeLine({ Origin.X + 3.0f, Origin.Y + 3.0f }, { Origin.X + 3.0f, Origin.Y + Size - 3.0f }, Ink, 1.35f);
            Surface.StrokeLine({ Origin.X + Size - 3.0f, Origin.Y + 3.0f }, { Origin.X + Size - 3.0f, Origin.Y + Size - 3.0f }, Ink, 1.35f);
            Surface.StrokeLine({ Origin.X + 4.5f, CY }, { Origin.X + Size - 4.5f, CY }, Ink, 1.35f);
            break;
        case 9u: // Pin UV
            Surface.FillCircle({ CX, Origin.Y + 4.5f }, 2.6f, Ink);
            Surface.StrokeLine({ CX, Origin.Y + 7.0f }, { CX, Origin.Y + Size - 2.5f }, Ink, 1.45f);
            break;
        case 10u: // Relax Stretch
            Surface.StrokeCircle({ CX, CY }, Size * 0.36f, Ink, 1.25f);
            Surface.StrokeCircle({ CX, CY }, Size * 0.16f, Ink, 1.25f);
            break;
        default: // step / Reset View
            WorkspaceAppearance::ConstructFocusIcon(Surface, Origin, Size, Ink);
            break;
    }
}

inline void ConstructMatCapOrbPreview(PixelSpace& Surface, Point2D Center, float Radius, uint32_t SlotIdx, bool Selected, const UVThemeColors& Theme)
{
    struct OrbColors { ColorRgba Diffuse; ColorRgba Spec; };
    constexpr std::array<OrbColors, 16u> OrbSwatches {{
        { { 158, 160, 166, 255 }, { 242, 244, 248, 255 } }, // 0 Clay
        { {  86,  90,  98, 255 }, { 196, 202, 214, 255 } }, // 1 Graphite
        { { 174, 184, 198, 255 }, { 255, 255, 255, 255 } }, // 2 Chrome
        { { 196, 152,  76, 255 }, { 255, 236, 178, 255 } }, // 3 Gold
        { { 184,  64,  68, 255 }, { 255, 198, 198, 255 } }, // 4 Carmine Car Paint
        { { 216, 212, 206, 255 }, { 255, 255, 252, 255 } }, // 5 Pearl Ceramic
        { {  44,  62,  86, 255 }, { 184, 222, 255, 255 } }, // 6 Smoked Glass
        { {  76, 138, 116, 255 }, { 196, 245, 224, 255 } }, // 7 Jade
        { {  52,  54,  58, 255 }, { 132, 136, 144, 255 } }, // 8 Vulcanized Rubber
        { { 166, 148, 128, 255 }, { 236, 224, 206, 255 } }, // 9 Warm Stone
        { {  72, 112, 176, 255 }, { 206, 228, 255, 255 } }, // 10 Cobalt Clearcoat
        { { 168, 102,  64, 255 }, { 248, 204, 172, 255 } }, // 11 Anodized Copper
        { { 118,  88, 156, 255 }, { 226, 204, 255, 255 } }, // 12 Iridescent Violet
        { { 126, 148,  88, 255 }, { 224, 242, 186, 255 } }, // 13 Olive Drab
        { { 192, 118, 128, 255 }, { 255, 218, 224, 255 } }, // 14 Subsurface Skin
        { {  94, 156, 176, 255 }, { 218, 248, 255, 255 } }, // 15 Arctic Ice
    }};
    const auto& C = OrbSwatches[SlotIdx % OrbSwatches.size()];
    Surface.FillCircle(Center, Radius, ColorRgba{ 18, 18, 20, 255 });
    Surface.FillCircle({ Center.X - Radius * 0.06f, Center.Y - Radius * 0.06f }, Radius * 0.90f, C.Diffuse);
    Surface.FillCircle({ Center.X - Radius * 0.28f, Center.Y - Radius * 0.30f }, Radius * 0.36f, C.Spec);
    if (Selected)
    {
        Surface.StrokeCircle(Center, Radius + 2.0f, Theme.AccentFill, 1.8f);
    }
    else
    {
        Surface.StrokeCircle(Center, Radius, Theme.BorderStrong, 1.0f);
    }
}

} // namespace Frontier::Workspaces::UVAppearance
