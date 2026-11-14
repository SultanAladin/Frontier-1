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

inline void ResolveHistoryShortcuts(
    const PaneInputReading& Input,
    bool&                   OutUndo,
    bool&                   OutRedo) noexcept
{
    OutUndo = Input.ShortcutUndo;
    OutRedo = Input.ShortcutRedo;
    if (!OutUndo && !OutRedo && Input.ControlHeld && !Input.AltHeld)
    {
        for (const char Ch : Input.TypedUtf8)
        {
            if (Ch == 'z' || Ch == 'Z')
            {
                OutUndo = !Input.ShiftHeld;
                OutRedo = Input.ShiftHeld;
            }
            else if ((Ch == 'y' || Ch == 'Y') && !Input.ShiftHeld)
            {
                OutRedo = true;
            }
        }
    }
}

inline void RecordControlTooltip(
    Rectangle2D AnchorBounds,
    std::string_view Label,
    const OptionalTooltip& Tooltip,
    std::string_view Fallback)
{
    std::string TitleStorage{};
    std::string BodyStorage{};
    OptionalTooltip Effective = Tooltip;
    if (!Effective.Enabled)
    {
        TitleStorage.assign(Label);
        BodyStorage.assign(Fallback);
        Effective = OptionalTooltip::FromTitled(TitleStorage.c_str(), BodyStorage.c_str());
    }
    ControlAppearance::RecordHoveredTooltip(AnchorBounds, Effective);
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
    float                RadiusOverride = -1.0f,
    const OptionalTooltip& Tooltip = {})
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

    if (Hovered)
    {
        RecordControlTooltip(Rect, Label, Tooltip, "Activate this UV workspace control.");
    }
    return Hovered && Pressed;
}

inline bool ConstructSectionHeading(
    PixelSpace&          Surface,
    float                LeftX,
    float                TopY,
    float                Width,
    std::string_view     Title,
    const UVThemeColors& Theme,
    std::string_view     RightTag = {},
    ControlIcon          Icon = ControlIcon::Count)
{
    WorkspaceAppearance::ConstructSectionHeading(
        Surface, LeftX, TopY, Width, Title, RightTag, Icon, Theme.CautionInk);
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
    ColorRgba            FillTintOverride = ColorRgba{ 0, 0, 0, 0 },
    const OptionalTooltip& Tooltip = {})
{
    // Match StudioControls.SliderRow: label above, 88x28 numeric/unit pill, and a 26px range track.
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { RowRect.X, RowRect.Y }, 11.5f, Theme.TextMuted, Label);

    std::string_view Number = FormattedReading;
    std::string_view Unit{};
    const std::size_t Space = FormattedReading.find(' ');
    if (Space != std::string_view::npos)
    {
        Number = FormattedReading.substr(0u, Space);
        Unit = FormattedReading.substr(Space + 1u);
    }
    else if (!FormattedReading.empty() && FormattedReading.back() == '%')
    {
        Number = FormattedReading.substr(0u, FormattedReading.size() - 1u);
        Unit = "%";
    }

    constexpr float ControlY = 18.0f;
    constexpr float ValueW = 88.0f;
    constexpr float ValueH = 28.0f;
    const Rectangle2D ValuePill{ RowRect.X, RowRect.Y + ControlY, ValueW, ValueH };
    Surface.FillRectangle(ValuePill, ColorRgba{ 17, 17, 17, 255 }, 14.0f);
    Surface.StrokeRectangle(ValuePill, ColorRgba{ 255, 255, 255, 23 }, 14.0f, 1.0f);
    Surface.FillRectangle(
        { ValuePill.Right() - 32.0f, ValuePill.Y + 1.0f, 31.0f, ValuePill.Height - 2.0f },
        ColorRgba{ 23, 23, 23, 255 }, 0.0f);
    Surface.StrokeLine(
        { ValuePill.Right() - 32.0f, ValuePill.Y + 4.0f },
        { ValuePill.Right() - 32.0f, ValuePill.Bottom() - 4.0f },
        ColorRgba{ 255, 255, 255, 20 }, 1.0f);
    const float NumberW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.5f, Number);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular,
        { ValuePill.X + (ValueW - 32.0f - NumberW) * 0.5f, ValuePill.Y + 8.0f },
        11.5f, Theme.TextPrimary, Number);
    const float UnitW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, Unit);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular,
        { ValuePill.Right() - 16.0f - UnitW * 0.5f, ValuePill.Y + 9.0f },
        9.5f, Theme.TextMuted, Unit);

    const float TrackX = ValuePill.Right() + 9.0f;
    const float TrackW = std::max(36.0f, RowRect.Right() - TrackX);
    const Rectangle2D Track{ TrackX, RowRect.Y + ControlY + 1.0f, TrackW, 26.0f };
    const Rectangle2D HitBox{ Track.X, Track.Y - 4.0f, Track.Width, Track.Height + 8.0f };
    const bool Hovered = HitBox.Contains(Cursor);
    if (Hovered)
    {
        RecordControlTooltip({ RowRect.X, RowRect.Y, RowRect.Width, ControlY + ValueH }, Label, Tooltip,
            "Drag the slider to adjust this UV workspace setting.");
    }

    constexpr float ThumbRadius = 12.0f;
    const float InnerTrackW = std::max(1.0f, Track.Width - ThumbRadius * 2.0f);
    const float Clamped = std::clamp(InOutNormalized, 0.0f, 1.0f);
    const float ThumbX = Track.X + ThumbRadius + InnerTrackW * Clamped;
    const float CenterY = Track.Y + Track.Height * 0.5f;
    Surface.FillRectangle(Track, ColorRgba{ 35, 35, 35, 255 }, 13.0f);
    const ColorRgba Fill = (FillTintOverride.Alpha > 0u) ? FillTintOverride : ColorRgba{ 78, 78, 78, 255 };
    Surface.FillRectangle(
        { Track.X, Track.Y, std::max(0.0f, ThumbX - Track.X), Track.Height }, Fill, 13.0f);
    Surface.FillCircle({ ThumbX, CenterY }, ThumbRadius, ColorRgba{ 230, 230, 230, 255 });
    Surface.StrokeCircle({ ThumbX, CenterY }, ThumbRadius, ColorRgba{ 0, 0, 0, 92 }, 1.0f);

    bool Changed = false;
    if (Hovered && (PointerPressed || PointerDown))
    {
        const float NextNorm = std::clamp((Cursor.X - (Track.X + ThumbRadius)) / InnerTrackW, 0.0f, 1.0f);
        if (std::abs(NextNorm - InOutNormalized) > 1.0e-4f)
        {
            InOutNormalized = NextNorm;
            Changed = true;
        }
    }
    return Changed;
}

inline bool ConstructToggleRow(
    PixelSpace&            Surface,
    Rectangle2D            RowRect,
    std::string_view       Label,
    bool&                  InOutEnabled,
    const UVThemeColors&,
    Point2D                Cursor,
    bool                   Pressed,
    const OptionalTooltip& Tooltip = {},
    std::string_view       DetailFirstLine = {},
    std::string_view       DetailSecondLine = {})
{
    return WorkspaceAppearance::ConstructCapsuleToggleRow(
        Surface,
        RowRect,
        Label,
        InOutEnabled,
        Cursor,
        Pressed,
        Tooltip,
        DetailFirstLine,
        DetailSecondLine);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  CRISP STUDIO VECTOR vector icons (MATCHING STUDIOCONTROLS.JS)
//------------------------------------------------------------------------------------------------------------------------

inline void ConstructComponentModeIcon(PixelSpace& Surface, Point2D Origin, float Size, uint32_t ModeIdx, ColorRgba Ink)
{
    WorkspaceAppearance::ConstructComponentModeIcon(Surface, Origin, Size, ModeIdx, Ink);
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
        case 3u: // Lasso Polygon
        {
            const std::array<Point2D, 7u> Loop {{
                { Origin.X + 3.0f,         Origin.Y + Size * 0.42f },
                { Origin.X + Size * 0.34f, Origin.Y + 2.2f },
                { Origin.X + Size - 2.6f,  Origin.Y + Size * 0.30f },
                { Origin.X + Size * 0.78f, Origin.Y + Size * 0.66f },
                { Origin.X + Size * 0.44f, Origin.Y + Size - 2.5f },
                { Origin.X + 2.6f,         Origin.Y + Size * 0.72f },
                { Origin.X + 3.0f,         Origin.Y + Size * 0.42f },
            }};
            Surface.StrokePolyline(Loop, Ink, 1.3f, false);
            Surface.FillCircle({ Origin.X + Size * 0.44f, Origin.Y + Size - 2.5f }, 1.6f, Ink);
            break;
        }
        case 4u: // Move (G)
            Surface.StrokeLine({ CX, Origin.Y + 2.0f }, { CX, Origin.Y + Size - 2.0f }, Ink, 1.35f);
            Surface.StrokeLine({ Origin.X + 2.0f, CY }, { Origin.X + Size - 2.0f, CY }, Ink, 1.35f);
            break;
        case 5u: // Rotate (R)
            Surface.StrokeCircle({ CX, CY }, Size * 0.34f, Ink, 1.35f);
            Surface.FillCircle({ CX + Size * 0.34f, CY }, 1.8f, Ink);
            break;
        case 6u: // Scale (S)
            Surface.StrokeRectangle({ Origin.X + 2.5f, Origin.Y + 5.5f, Size - 8.0f, Size - 8.0f }, Ink, 1.5f, 1.25f);
            Surface.StrokeLine({ Origin.X + 5.0f, Origin.Y + Size - 5.0f }, { Origin.X + Size - 2.5f, Origin.Y + 2.5f }, Ink, 1.35f);
            break;
        case 7u: // Split / Scissors
            Surface.StrokeCircle({ Origin.X + 4.2f, Origin.Y + 4.5f }, 2.2f, Ink, 1.25f);
            Surface.StrokeCircle({ Origin.X + 4.2f, Origin.Y + Size - 4.5f }, 2.2f, Ink, 1.25f);
            Surface.StrokeLine({ Origin.X + 6.0f, Origin.Y + 5.5f }, { Origin.X + Size - 2.5f, Origin.Y + Size - 3.5f }, Ink, 1.35f);
            Surface.StrokeLine({ Origin.X + 6.0f, Origin.Y + Size - 5.5f }, { Origin.X + Size - 2.5f, Origin.Y + 3.5f }, Ink, 1.35f);
            break;
        case 8u: // Seam Brush
            Surface.StrokeLine({ Origin.X + 3.0f, Origin.Y + Size - 3.0f }, { Origin.X + Size - 3.0f, Origin.Y + 3.0f }, Ink, 1.65f);
            Surface.FillCircle({ CX, CY }, 2.0f, Ink);
            break;
        case 9u: // Stitch / Weld
            Surface.StrokeLine({ Origin.X + 3.0f, Origin.Y + 3.0f }, { Origin.X + 3.0f, Origin.Y + Size - 3.0f }, Ink, 1.35f);
            Surface.StrokeLine({ Origin.X + Size - 3.0f, Origin.Y + 3.0f }, { Origin.X + Size - 3.0f, Origin.Y + Size - 3.0f }, Ink, 1.35f);
            Surface.StrokeLine({ Origin.X + 4.5f, CY }, { Origin.X + Size - 4.5f, CY }, Ink, 1.35f);
            break;
        case 10u: // Pin UV
            Surface.FillCircle({ CX, Origin.Y + 4.5f }, 2.6f, Ink);
            Surface.StrokeLine({ CX, Origin.Y + 7.0f }, { CX, Origin.Y + Size - 2.5f }, Ink, 1.45f);
            break;
        case 11u: // Relax Stretch
            Surface.StrokeCircle({ CX, CY }, Size * 0.36f, Ink, 1.25f);
            Surface.StrokeCircle({ CX, CY }, Size * 0.16f, Ink, 1.25f);
            break;
        default: // step / Reset View
            WorkspaceAppearance::ConstructFocusIcon(Surface, Origin, Size, Ink);
            break;
    }
}

inline void ConstructMatCapOrbPreview(
    PixelSpace& Surface,
    Point2D Center,
    float Radius,
    uint32_t SlotIdx,
    bool Selected,
    const UVThemeColors& Theme,
    ImTextureData* MatCapAtlas = nullptr)
{
    const uint32_t Slot = SlotIdx % 16u;
    const uint32_t Col = Slot % 4u;
    const uint32_t Row = Slot / 4u;
    const Rectangle2D Bounds{ Center.X - Radius, Center.Y - Radius, Radius * 2.0f, Radius * 2.0f };
    if (MatCapAtlas != nullptr)
    {
        Surface.FillTexturedRectangle(
            Bounds, MatCapAtlas,
            { static_cast<float>(Col) * 0.25f, static_cast<float>(Row) * 0.25f },
            { static_cast<float>(Col + 1u) * 0.25f, static_cast<float>(Row + 1u) * 0.25f },
            Radius, { 255u, 255u, 255u, 255u });
    }
    else
    {
        Surface.FillCircle(Center, Radius, ColorRgba{ 34, 36, 42, 255 });
    }
    Surface.StrokeCircle(Center, Radius, Selected ? Theme.AccentFill : Theme.BorderStrong, Selected ? 1.8f : 1.0f);
}

} // namespace Frontier::Workspaces::UVAppearance
