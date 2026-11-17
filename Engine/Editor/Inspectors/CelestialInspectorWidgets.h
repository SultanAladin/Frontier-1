/*============================================================================================================================================
                                                        CELESTIALINSPECTORWIDGETS.H
============================================================================================================================================*/
// 🧩 Shared Frontier-styled Inspector layout, sliders, green switches, axis triplets, and dropdown triggers for Celestial & Cloud views

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/ScalarSlider.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace Frontier::Editor::Inspectors
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     SURFACE COLORS
//------------------------------------------------------------------------------------------------------------------------

namespace CelestialSurfaceColors
{
inline constexpr ColorRgba RootBg         { 18,  18,  18,  255 };
inline constexpr ColorRgba PanelBg        { 32,  32,  32,  255 };
inline constexpr ColorRgba SubWellBg      { 23,  23,  24,  255 };
inline constexpr ColorRgba RecessDark     { 13,  13,  15,  255 };
inline constexpr ColorRgba HairlineBorder { 255, 255, 255, 14  };
inline constexpr ColorRgba SubtleBorder   { 255, 255, 255, 24  };
inline constexpr ColorRgba TabInactiveBg  { 24,  24,  24,  255 };
inline constexpr ColorRgba TabActiveBg    { 39,  39,  39,  255 };
inline constexpr ColorRgba TabHoverBg     { 46,  46,  46,  255 };
inline constexpr ColorRgba InkPrimary     { 240, 240, 240, 255 };
inline constexpr ColorRgba InkHeading     { 222, 222, 222, 255 };
inline constexpr ColorRgba InkLabel       { 169, 175, 181, 255 };
inline constexpr ColorRgba InkMuted       { 132, 138, 144, 255 };
inline constexpr ColorRgba InkDim         { 102, 108, 114, 255 };
inline constexpr ColorRgba SwitchGreen    { 52,  199, 89,  255 };
inline constexpr ColorRgba SwitchOff      { 47,  47,  51,  255 };
inline constexpr ColorRgba AmberAccent    { 255, 180, 84,  255 };
inline constexpr ColorRgba SolarGold      { 244, 205, 117, 255 };
inline constexpr ColorRgba LunarBlue      { 157, 189, 214, 255 };
inline constexpr ColorRgba StarViolet     { 200, 164, 223, 255 };
inline constexpr ColorRgba CloudMist      { 207, 216, 227, 255 };
inline constexpr ColorRgba AxisRed        { 226, 116, 112, 255 };
inline constexpr ColorRgba AxisGreen      { 103, 196, 133, 255 };
inline constexpr ColorRgba AxisBlue       { 110, 167, 229, 255 };
} // namespace CelestialSurfaceColors

//------------------------------------------------------------------------------------------------------------------------
//                                                    WIDGET HELPERS
//------------------------------------------------------------------------------------------------------------------------

inline void ConstructInspectorGroupBox(PixelSpace& Surface, Rectangle2D Bounds) noexcept
{
    Surface.FillRectangle(Bounds, CelestialSurfaceColors::PanelBg, 18.0f);
    Surface.StrokeRectangle(Bounds, CelestialSurfaceColors::HairlineBorder, 18.0f, 1.0f);
}

inline void ConstructInspectorHeader(
    PixelSpace&      Surface,
    float            LeftX,
    float&           InOutY,
    float            InnerW,
    std::string_view Breadcrumb,
    std::string_view TitleText,
    std::string_view SubtitleText) noexcept
{
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, InOutY },
        InnerW,
        10.0f,
        CelestialSurfaceColors::InkMuted,
        Breadcrumb);
    InOutY += 16.0f;

    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::SemiBold,
        { LeftX, InOutY },
        InnerW,
        20.0f,
        CelestialSurfaceColors::InkPrimary,
        TitleText);
    InOutY += 25.0f;

    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, InOutY },
        InnerW,
        10.5f,
        CelestialSurfaceColors::InkMuted,
        SubtitleText);
    InOutY += 20.0f;
}

inline bool ConstructCapabilityTile(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const char*             Caption,
    const char*             ActiveTagText,
    const char*             InactiveTagText,
    bool&                   ToggleBit,
    bool                    Interactive,
    const PaneInputReading& Input) noexcept
{
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Hovered = Interactive && Bounds.Contains(Cursor);

    const ColorRgba Bg = ToggleBit
        ? ColorRgba{ 30, 44, 34, 255 }
        : (Hovered ? ColorRgba{ 34, 34, 38, 255 } : CelestialSurfaceColors::SubWellBg);
    const ColorRgba Border = ToggleBit
        ? ColorRgba{ 52, 199, 89, 130 }
        : CelestialSurfaceColors::HairlineBorder;

    Surface.FillRectangle(Bounds, Bg, 14.0f);
    Surface.StrokeRectangle(Bounds, Border, 14.0f, 1.0f);

    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Medium,
        { Bounds.X + 12.0f, Bounds.Y + (Bounds.Height - 11.5f) * 0.5f },
        std::max(28.0f, Bounds.Width - 76.0f),
        10.5f,
        ToggleBit ? CelestialSurfaceColors::InkPrimary : CelestialSurfaceColors::InkLabel,
        Caption);

    const char* TagText = ToggleBit ? ActiveTagText : InactiveTagText;
    const ColorRgba TagInk = ToggleBit ? CelestialSurfaceColors::SwitchGreen : CelestialSurfaceColors::InkDim;
    WorkspaceAppearance::ConstructRightAlignedText(
        Surface,
        FontWeightClass::SemiBold,
        { Bounds.Right() - 12.0f, Bounds.Y + (Bounds.Height - 10.0f) * 0.5f },
        9.0f,
        TagInk,
        TagText);

    if (Hovered && Input.PrimaryPointer.Pressed)
    {
        ToggleBit = !ToggleBit;
        return true;
    }
    return false;
}

inline bool ConstructGreenSwitchRow(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const char*             Caption,
    bool&                   ToggleBit,
    bool                    Interactive,
    const PaneInputReading& Input) noexcept
{
    const bool Hovered = Interactive && Bounds.Contains({ Input.PrimaryPointer.X, Input.PrimaryPointer.Y });
    Surface.FillRectangle(Bounds, Hovered ? ColorRgba{ 29, 29, 32, 255 } : CelestialSurfaceColors::SubWellBg, 14.0f);
    Surface.StrokeRectangle(Bounds, CelestialSurfaceColors::HairlineBorder, 14.0f, 1.0f);

    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { Bounds.X + 11.0f, Bounds.Y + (Bounds.Height - 11.0f) * 0.5f },
        std::max(24.0f, Bounds.Width - 54.0f),
        10.0f,
        ToggleBit ? CelestialSurfaceColors::InkPrimary : CelestialSurfaceColors::InkMuted,
        Caption);

    const Rectangle2D Track{ Bounds.Right() - 40.0f, Bounds.Center().Y - 9.0f, 30.0f, 18.0f };
    Surface.FillRectangle(Track, ToggleBit ? CelestialSurfaceColors::SwitchGreen : CelestialSurfaceColors::SwitchOff, 9.0f);
    const float KnobX = ToggleBit ? (Track.Right() - 9.0f) : (Track.X + 9.0f);
    Surface.FillCircle({ KnobX, Track.Center().Y }, 7.0f, ColorRgba{ 248, 248, 250, 255 });

    if (Hovered && Input.PrimaryPointer.Pressed)
    {
        ToggleBit = !ToggleBit;
        return true;
    }
    return false;
}

inline bool ConstructSelectTrigger(
    PixelSpace&           Surface,
    Rectangle2D           Bounds,
    const char*           Text,
    bool                  Open,
    bool                  Interactive,
    const ControlPointer& Pointer) noexcept
{
    const bool Hovered = Interactive && Bounds.Contains({ Pointer.X, Pointer.Y });
    Surface.FillRectangle(Bounds, Hovered ? ColorRgba{ 24, 24, 28, 255 } : CelestialSurfaceColors::RecessDark, 10.0f);
    Surface.StrokeRectangle(Bounds, Open ? CelestialSurfaceColors::AmberAccent : CelestialSurfaceColors::SubtleBorder, 10.0f, 1.0f);

    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { Bounds.X + 10.0f, Bounds.Y + 8.5f },
        std::max(16.0f, Bounds.Width - 26.0f),
        10.5f,
        CelestialSurfaceColors::InkHeading,
        Text);

    VectorIconRepository::ConstructControl(
        Surface,
        Open ? ControlIcon::ChevronUp : ControlIcon::ChevronDown,
        VectorIconPlacement{ Bounds.Right() - 18.0f, Bounds.Y + 9.0f, 12.0f, CelestialSurfaceColors::InkMuted, 1.6f });

    return Hovered && Pointer.Pressed;
}

inline ColorRgba ScaleColorOpacity(ColorRgba Color, float Opacity) noexcept
{
    return Color.ScaledAlpha(Opacity);
}

inline void ConstructSplitNumericReadout(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    const char* NumberText,
    const char* UnitText,
    float       Opacity = 1.0f) noexcept
{
    const float Radius   = Bounds.Height * 0.5f;
    const float UnitW    = (UnitText != nullptr && UnitText[0] != '\0') ? 32.0f : 0.0f;
    const float DividerX = Bounds.Right() - UnitW;

    Surface.FillRectangle(Bounds, ScaleColorOpacity(CelestialSurfaceColors::RecessDark, Opacity), Radius);
    Surface.StrokeRectangle(Bounds, ScaleColorOpacity(CelestialSurfaceColors::HairlineBorder, Opacity), Radius, 1.0f);
    if (UnitW > 0.0f)
    {
        Surface.StrokeLine(
            { DividerX, Bounds.Y + 4.0f },
            { DividerX, Bounds.Bottom() - 4.0f },
            ColorRgba{ 255, 255, 255, static_cast<uint8_t>(18.0f * Opacity) },
            1.0f);
        const float Uw = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, UnitText);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { DividerX + (UnitW - Uw) * 0.5f, Bounds.Y + 7.5f },
            9.0f,
            ScaleColorOpacity(CelestialSurfaceColors::InkMuted, Opacity),
            UnitText);
    }

    const float NumW     = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, NumberText);
    const float LeftSpan = std::max(12.0f, DividerX - Bounds.X);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { Bounds.X + std::max(6.0f, (LeftSpan - NumW) * 0.5f), Bounds.Y + 7.0f },
        LeftSpan - 8.0f,
        10.5f,
        ScaleColorOpacity(CelestialSurfaceColors::InkPrimary, Opacity),
        NumberText);
}

inline bool ConstructScalarSliderRow(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerW,
    const char*             Caption,
    const char*             UnitHint,
    float                   Minimum,
    float                   Maximum,
    float&                  ScalarReading,
    uint32_t                DecimalDigits,
    int32_t                 SliderId,
    int32_t&                ActiveSliderId,
    bool                    Interactive,
    const PaneInputReading& Input,
    bool                    Locked = false) noexcept
{
    const float Opacity = Locked ? 0.42f : 1.0f;
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, TopY },
        10.5f,
        ScaleColorOpacity(CelestialSurfaceColors::InkLabel, Opacity),
        Caption);
    if (UnitHint != nullptr && UnitHint[0] != '\0')
    {
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface,
            FontWeightClass::Regular,
            { LeftX + InnerW, TopY + 0.5f },
            9.5f,
            ScaleColorOpacity(CelestialSurfaceColors::InkMuted, Opacity),
            UnitHint);
    }

    const float       SliderTop = TopY + 17.0f;
    constexpr float   ReadoutW  = 92.0f;
    constexpr float   Gap       = 8.0f;
    const float       SliderW   = std::max(40.0f, InnerW - ReadoutW - Gap);
    const Rectangle2D TrackRect{ LeftX, SliderTop, SliderW, 26.0f };

    ControlPointer AdjPtr = Input.PrimaryPointer;
    AdjPtr.Enabled        = Interactive && !Locked;
    float Updated         = ScalarReading;
    const ControlContact Hit = ScalarSlider::Construct(
        Surface,
        TrackRect,
        Minimum,
        Maximum,
        ScalarReading,
        ActiveSliderId == SliderId,
        AdjPtr,
        Updated,
        Opacity,
        26.0f,
        24.0f);

    bool Changed = false;
    if (!Locked && Hit.Pressed)
    {
        ActiveSliderId = SliderId;
    }
    if (!Locked && ActiveSliderId == SliderId && Input.PrimaryPointer.Down && Interactive)
    {
        const float Clamped = std::clamp(Updated, std::min(Minimum, Maximum), std::max(Minimum, Maximum));
        if (std::fabs(Clamped - ScalarReading) > 1.0e-5f)
        {
            ScalarReading = Clamped;
            Changed       = true;
        }
    }

    char NumBuf[32]{};
    std::snprintf(NumBuf, sizeof(NumBuf), "%.*f", static_cast<int>(DecimalDigits), ScalarReading);
    ConstructSplitNumericReadout(Surface, { LeftX + SliderW + Gap, SliderTop, ReadoutW, 26.0f }, NumBuf, UnitHint, Opacity);
    return Changed;
}

inline bool ConstructAxisTripletRow(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerW,
    std::array<float, 3>&   Coord,
    float                   MinLimit,
    float                   MaxLimit,
    float                   StepSensitivity,
    int32_t                 FirstAxisTagId,
    int32_t&                ActiveAxisDragId,
    float&                  DragStartX,
    float&                  DragStartVal,
    bool                    Interactive,
    const PaneInputReading& Input) noexcept
{
    static constexpr std::array<const char*, 3> Axes = {{ "X", "Y", "Z" }};
    static constexpr std::array<ColorRgba, 3>   AxisInk = {{
        CelestialSurfaceColors::AxisRed,
        CelestialSurfaceColors::AxisGreen,
        CelestialSurfaceColors::AxisBlue,
    }};

    const float   Gap   = 6.0f;
    const float   CellW = (InnerW - Gap * 2.0f) / 3.0f;
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    bool          Changed = false;

    for (std::size_t Ax = 0u; Ax < 3u; ++Ax)
    {
        const Rectangle2D Box{ LeftX + static_cast<float>(Ax) * (CellW + Gap), TopY, CellW, 28.0f };
        const int32_t     AxisTag = FirstAxisTagId + static_cast<int32_t>(Ax);
        const bool        Hovered = Interactive && Box.Contains(Cursor);

        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            ActiveAxisDragId = AxisTag;
            DragStartX       = Cursor.X;
            DragStartVal     = Coord[Ax];
        }
        if (ActiveAxisDragId == AxisTag && Input.PrimaryPointer.Down && Interactive)
        {
            const float Sensitivity = Input.ShiftHeld ? (StepSensitivity * 0.2f) : StepSensitivity;
            const float NextVal     = std::clamp(DragStartVal + (Cursor.X - DragStartX) * Sensitivity, MinLimit, MaxLimit);
            if (std::fabs(NextVal - Coord[Ax]) > 1.0e-4f)
            {
                Coord[Ax] = NextVal;
                Changed   = true;
            }
        }

        Surface.FillRectangle(Box, Hovered ? ColorRgba{ 22, 22, 26, 255 } : CelestialSurfaceColors::RecessDark, 12.0f);
        Surface.StrokeRectangle(
            Box,
            (ActiveAxisDragId == AxisTag) ? CelestialSurfaceColors::AmberAccent : CelestialSurfaceColors::SubtleBorder,
            12.0f,
            1.0f);

        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::SemiBold,
            { Box.X + 9.0f, Box.Y + 7.5f },
            10.0f,
            AxisInk[Ax],
            Axes[Ax]);

        char NumBuf[24]{};
        std::snprintf(NumBuf, sizeof(NumBuf), "%.1f", Coord[Ax]);
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface,
            FontWeightClass::Regular,
            { Box.Right() - 9.0f, Box.Y + 7.5f },
            10.5f,
            CelestialSurfaceColors::InkPrimary,
            NumBuf);
    }
    return Changed;
}

inline void ConstructReadoutCell(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    const char* LabelText,
    const char* ReadingText) noexcept
{
    Surface.FillRectangle(Bounds, CelestialSurfaceColors::SubWellBg, 10.0f);
    Surface.StrokeRectangle(Bounds, CelestialSurfaceColors::HairlineBorder, 10.0f, 1.0f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { Bounds.X + 9.0f, Bounds.Y + 6.0f },
        8.0f,
        CelestialSurfaceColors::InkMuted,
        LabelText);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Medium,
        { Bounds.X + 9.0f, Bounds.Y + 18.0f },
        std::max(16.0f, Bounds.Width - 16.0f),
        11.0f,
        CelestialSurfaceColors::InkPrimary,
        ReadingText);
}

inline void HandleInspectorScroll(
    Rectangle2D             Bounds,
    float                   ContentHeight,
    float&                  InOutScrollOffsetY,
    float&                  OutLastContentHeight,
    const PaneInputReading& Input) noexcept
{
    OutLastContentHeight = ContentHeight;
    const float MaxScroll = std::max(0.0f, ContentHeight - Bounds.Height);
    if (Bounds.Contains({ Input.PrimaryPointer.X, Input.PrimaryPointer.Y }) && std::fabs(Input.WheelDeltaY) > 1.0e-3f)
    {
        InOutScrollOffsetY = std::clamp(InOutScrollOffsetY - Input.WheelDeltaY * 32.0f, 0.0f, MaxScroll);
    }
    else
    {
        InOutScrollOffsetY = std::clamp(InOutScrollOffsetY, 0.0f, MaxScroll);
    }
}

} // namespace Frontier::Editor::Inspectors
