/*============================================================================================================================================
                                                          TEXTURINGPANEAPPEARANCE.H
============================================================================================================================================*/
// 🧩 Shared ControlCentre ThemeRepository theme colors, Studio UI rows, KPI trio, removable channel chips & icons for Texturing workspaces

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/Controls/ControlTooltip.h"
#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/ExclusiveSelector.h"
#include "Engine/Components/Controls/ScalarSlider.h"
#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Workspaces::TexturingAppearance
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr ColorRgba StudioCreamAccent    { 239u, 244u, 207u, 255u }; // [-] - #EFF4CF active workspace & highlight tile fill
inline constexpr ColorRgba StudioCreamInk       { 23u,  25u,  14u,  255u }; // [-] - #17190E crisp dark ink on StudioCreamAccent
inline constexpr ColorRgba StudioCreamSubInk    { 23u,  25u,  14u,  166u }; // [-] - Secondary caption ink on StudioCreamAccent
inline constexpr ColorRgba StudioBlockSurface   { 26u,  26u,  26u,  255u }; // [-] - #1A1A1A section & metric tile background
inline constexpr ColorRgba StudioSubBlockFill   { 20u,  20u,  20u,  255u }; // [-] - #141414 nested channel / modifier surface
inline constexpr ColorRgba StudioRowFill        { 23u,  23u,  23u,  255u }; // [-] - #171717 unselected layer box fill
inline constexpr ColorRgba StudioRowHover       { 30u,  30u,  30u,  255u }; // [-] - #1E1E1E hovered layer box fill
inline constexpr ColorRgba StudioRowSelected    { 38u,  38u,  38u,  255u }; // [-] - #262626 selected layer box fill
inline constexpr ColorRgba StudioWellFill       { 13u,  13u,  14u,  255u }; // [-] - #0D0D0E recessed input & dropdown capsule fill
inline constexpr ColorRgba StudioHairline       { 255u, 255u, 255u, 15u  }; // [-] - rgba(255,255,255,0.06) subtle hairline border
inline constexpr ColorRgba StudioHairlineStrong { 255u, 255u, 255u, 32u  }; // [-] - Emphasized capsule border
inline constexpr ColorRgba StudioReadyGreen     { 121u, 189u, 143u, 255u }; // [-] - #79BD8F ready indicator
inline constexpr ColorRgba StudioWarmGold       { 222u, 193u, 147u, 255u }; // [-] - #DEC193 warm highlight ink

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct TexturingThemeColors
{
    ColorRgba PaneBackground   {}; // [-] - Docked panel background synced from ControlCentre ThemeRepository
    ColorRgba SectionSurface   {}; // [-] - Rounded section block fill
    ColorRgba NestedSurface    {}; // [-] - Channel/modifier sub-block fill
    ColorRgba WellSurface      {}; // [-] - Recessed dark input / dropdown fill
    ColorRgba RaisedSurface    {}; // [-] - Neutral interactive button fill
    ColorRgba HoverSurface     {}; // [-] - Hovered interactive fill
    ColorRgba BorderSubtle     {}; // [-] - Hairline outline
    ColorRgba BorderStrong     {}; // [-] - Emphasized outline
    ColorRgba HighlightFill    {}; // [-] - Cream highlight fill (#EFF4CF in dark themes)
    ColorRgba HighlightInk     {}; // [-] - Ink over HighlightFill
    ColorRgba AccentFill       {}; // [-] - Live ControlCentre accent fill
    ColorRgba AccentSoft       {}; // [-] - Translucent accent wash
    ColorRgba AccentBorder     {}; // [-] - Accent perimeter stroke
    ColorRgba TextPrimary      {}; // [-] - Crisp primary text
    ColorRgba TextMuted        {}; // [-] - Secondary caption text
    ColorRgba TextFaint        {}; // [-] - Tertiary subtle text
    ColorRgba ReadyGreen       { 121u, 189u, 143u, 255u }; // [-] - Live active status green
    ColorRgba CautionWarm      { 219u, 171u, 92u,  255u }; // [-] - Caution amber ink
    ColorRgba DangerInk        { 217u, 108u, 108u, 255u }; // [-] - Destructive red ink
    ColorRgba DangerSoft       { 217u, 108u, 108u, 36u  }; // [-] - Destructive red hover tint
    bool      LightSurface     = false;                    // [-] - True when active theme uses light background
};

struct MetricTileReading
{
    std::string_view Caption   {};     // [-] - Upper small label ("Roughness", "Instances")
    std::string_view Reading   {};     // [-] - Large focal figure ("0.24", "125")
    std::string_view SubLabel  {};     // [-] - Lower detail caption ("Microfacet", "Evaluated")
    bool             Highlight = false;// [-] - Renders with #EFF4CF Studio cream fill when true
};

struct RemovableChipEntry
{
    std::string_view Label   {};                        // [-] - Visible chip text
    ColorRgba        DotTint { 158u, 162u, 170u, 255u };// [-] - Leading dot tint (#9ea2aa or live colour)
    bool             Active  = false;                   // [-] - Selected highlight toggle
};

struct ChipStripOutcome
{
    float   ConsumedHeight = 0.0f; // [px]  - Total wrapped height
    int32_t ClickedIndex   = -1;   // [idx] - Clicked chip ordinal (-1 = none)
};

struct RemovableChipOutcome
{
    float       ConsumedHeight     = 0.0f; // [px]  - Total wrapped height including +Add trigger
    int32_t     RemovedIndex       = -1;   // [idx] - Chip whose trailing 'x' was clicked (-1 = none)
    int32_t     SelectedIndex      = -1;   // [idx] - Chip body clicked (-1 = none)
    bool        AddTriggerClicked  = false;// [-]   - True if the "+ Channel (N)" trigger was clicked
    Rectangle2D AddTriggerBounds   {};   // [px]  - Screen bounds of the "+ Channel (N)" trigger
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline ColorRgba MixSurfaceColors(ColorQuad First, ColorQuad Second, float BlendT, float AlphaScale = 1.0f) noexcept
{
    ColorQuad Mixed = ColorQuad::Blend(First, Second, std::clamp(BlendT, 0.0f, 1.0f));
    Mixed.Alpha     = std::clamp(Mixed.Alpha * AlphaScale, 0.0f, 1.0f);
    return Mixed.ToRgba8();
}

[[nodiscard]] inline TexturingThemeColors QueryTexturingThemeColors() noexcept
{
    const ControlColorSet& Colors = ControlAppearance::Colors();
    TexturingThemeColors Out {};
    Out.LightSurface   = Colors.LightSurface;
    Out.PaneBackground = WorkspaceAppearance::QueryDockedPaneBackground();
    Out.SectionSurface = Colors.LightSurface ? Colors.SectionSurface.ToRgba8() : StudioBlockSurface;
    Out.NestedSurface  = Colors.LightSurface ? MixSurfaceColors(Colors.SectionSurface, Colors.Panel, 0.35f) : StudioSubBlockFill;
    Out.WellSurface    = Colors.LightSurface ? Colors.Field.ToRgba8() : StudioWellFill;
    Out.RaisedSurface  = Colors.Selected.ToRgba8();
    Out.HoverSurface   = MixSurfaceColors(Colors.Selected, Colors.Text, Colors.LightSurface ? 0.08f : 0.11f);
    Out.BorderSubtle   = Colors.LightSurface ? Colors.Stroke.ToRgba8() : StudioHairline;
    Out.BorderStrong   = Colors.LightSurface ? Colors.StrokeStrong.ToRgba8() : StudioHairlineStrong;
    Out.HighlightFill  = Colors.LightSurface ? Colors.Accent.ToRgba8() : StudioCreamAccent;
    Out.HighlightInk   = Colors.LightSurface ? Colors.AccentInk.ToRgba8() : StudioCreamInk;
    Out.AccentFill     = Colors.Accent.ToRgba8();
    Out.AccentSoft     = Colors.LightSurface
        ? MixSurfaceColors(Colors.SectionSurface, Colors.Accent, 0.16f)
        : ColorRgba{ 239u, 244u, 207u, 28u };
    Out.AccentBorder   = Colors.LightSurface
        ? MixSurfaceColors(Colors.StrokeStrong, Colors.Accent, 0.68f)
        : ColorRgba{ 239u, 244u, 207u, 145u };
    Out.TextPrimary    = Colors.Text.ToRgba8();
    Out.TextMuted      = Colors.TextDim.ToRgba8();
    Out.TextFaint      = Colors.TextFaint.ToRgba8();
    return Out;
}

[[nodiscard]] inline ColorRgba ParseHexRgba(std::string_view Hex, ColorRgba Fallback = { 239u, 244u, 207u, 255u }) noexcept
{
    if (Hex.size() >= 7u && Hex[0] == '#')
    {
        auto DecodeNibble = [](char Ch) noexcept -> int
        {
            if (Ch >= '0' && Ch <= '9') return Ch - '0';
            if (Ch >= 'a' && Ch <= 'f') return 10 + (Ch - 'a');
            if (Ch >= 'A' && Ch <= 'F') return 10 + (Ch - 'A');
            return -1;
        };
        const int R0 = DecodeNibble(Hex[1]), R1 = DecodeNibble(Hex[2]);
        const int G0 = DecodeNibble(Hex[3]), G1 = DecodeNibble(Hex[4]);
        const int B0 = DecodeNibble(Hex[5]), B1 = DecodeNibble(Hex[6]);
        if (R0 >= 0 && R1 >= 0 && G0 >= 0 && G1 >= 0 && B0 >= 0 && B1 >= 0)
        {
            return ColorRgba{
                static_cast<uint8_t>((R0 << 4) | R1),
                static_cast<uint8_t>((G0 << 4) | G1),
                static_cast<uint8_t>((B0 << 4) | B1),
                255u,
            };
        }
    }
    return Fallback;
}

inline void FormatHexLower(ColorRgba Rgba, char* OutBuffer, std::size_t BufferSize) noexcept
{
    std::snprintf(OutBuffer, BufferSize, "#%02x%02x%02x", Rgba.Red, Rgba.Green, Rgba.Blue);
}

inline float ConstructPaneHeadingBar(
    PixelSpace&                 Surface,
    float                       LeftX,
    float                       TopY,
    float                       InnerW,
    std::string_view            Title,
    std::string_view            MetaRight,
    const TexturingThemeColors& Theme) noexcept
{
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX, TopY }, 12.5f, Theme.TextPrimary, Title);
    if (!MetaRight.empty())
    {
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface, FontWeightClass::Medium, { LeftX + InnerW, TopY + 2.0f }, 8.5f, Theme.TextMuted, MetaRight);
    }
    Surface.StrokeLine({ LeftX, TopY + 24.0f }, { LeftX + InnerW, TopY + 24.0f }, Theme.BorderSubtle, 1.0f);
    return 32.0f;
}

inline float ConstructSubjectHeadingBlock(
    PixelSpace&                 Surface,
    float                       LeftX,
    float                       TopY,
    float                       InnerW,
    ControlIcon                 SymbolIcon,
    std::string_view            PrimaryTitle,
    std::string_view            SubCaption,
    const TexturingThemeColors& Theme) noexcept
{
    const bool  IconPresent = (SymbolIcon != ControlIcon::Count);
    const float TextX   = IconPresent ? (LeftX + 42.0f) : LeftX;
    const float TextW   = std::max(40.0f, (LeftX + InnerW) - TextX);

    if (IconPresent)
    {
        const Rectangle2D IconTile{ LeftX, TopY, 32.0f, 32.0f };
        Surface.FillRectangle(IconTile, Theme.SectionSurface, 10.0f);
        Surface.StrokeRectangle(IconTile, Theme.BorderSubtle, 10.0f, 1.0f);
        VectorIconRepository::ConstructControl(
            Surface, SymbolIcon, VectorIconPlacement{ IconTile.X + 8.0f, IconTile.Y + 8.0f, 16.0f, Theme.TextPrimary, 1.6f });
    }

    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { TextX, TopY + 1.0f }, TextW, 13.5f, Theme.TextPrimary, PrimaryTitle);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { TextX, TopY + 18.0f }, TextW, 8.2f, Theme.TextMuted, SubCaption);
    return 40.0f;
}

inline float ConstructMetricTrioTiles(
    PixelSpace&                          Surface,
    float                                LeftX,
    float                                TopY,
    float                                InnerW,
    std::span<const MetricTileReading, 3u> Metrics,
    const TexturingThemeColors&          Theme) noexcept
{
    constexpr float TileGap = 8.0f;
    constexpr float TileH   = 64.0f;
    const float     TileW   = (InnerW - TileGap * 2.0f) / 3.0f;

    for (uint32_t Index = 0u; Index < 3u; ++Index)
    {
        const auto&       Tile    = Metrics[Index];
        const float       X       = LeftX + static_cast<float>(Index) * (TileW + TileGap);
        const Rectangle2D TileBox { X, TopY, TileW, TileH };

        const ColorRgba FillCol   = Tile.Highlight ? Theme.HighlightFill : Theme.SectionSurface;
        const ColorRgba StrokeCol = Tile.Highlight ? Theme.HighlightFill : Theme.BorderSubtle;
        const ColorRgba InkMain   = Tile.Highlight ? Theme.HighlightInk  : Theme.TextPrimary;
        const ColorRgba InkSub    = Tile.Highlight
            ? (Theme.LightSurface ? ColorRgba{ 255u, 255u, 255u, 212u } : StudioCreamSubInk)
            : Theme.TextMuted;

        Surface.FillRectangle(TileBox, FillCol, 16.0f);
        Surface.StrokeRectangle(TileBox, StrokeCol, 16.0f, 1.0f);

        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { X + 10.0f, TopY + 8.0f }, TileW - 20.0f, 8.2f, InkSub, Tile.Caption);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { X + 10.0f, TopY + 23.0f }, TileW - 20.0f, 17.0f, InkMain, Tile.Reading);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { X + 10.0f, TopY + 46.0f }, TileW - 20.0f, 8.0f, InkSub, Tile.SubLabel);
    }
    return TileH;
}

inline void BeginSectionContainer(
    PixelSpace&                 Surface,
    Rectangle2D                 Bounds,
    const TexturingThemeColors& Theme,
    float                       CornerRadius = 18.0f) noexcept
{
    Surface.FillRectangle(Bounds, Theme.SectionSurface, CornerRadius);
    Surface.StrokeRectangle(Bounds, Theme.BorderSubtle, CornerRadius, 1.0f);
}

inline bool ConstructSectionHeaderRow(
    PixelSpace&                 Surface,
    Rectangle2D                 HeaderBounds,
    ControlIcon                 Icon,
    std::string_view            Title,
    std::string_view            RightTag,
    bool                        Expanded,
    const TexturingThemeColors& Theme,
    Point2D                     Cursor,
    bool                        Pressed) noexcept
{
    const bool  Hovered = HeaderBounds.Contains(Cursor);
    const float IconW   = (Icon != ControlIcon::Count) ? 20.0f : 0.0f;

    if (Icon != ControlIcon::Count)
    {
        VectorIconRepository::ConstructControl(
            Surface,
            Icon,
            VectorIconPlacement{ HeaderBounds.X, HeaderBounds.Y + 2.0f, 14.0f, Hovered ? Theme.TextPrimary : Theme.TextMuted, 1.7f });
    }

    float TrailingReserved = 0.0f;
    if (!RightTag.empty())
    {
        const float TagW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.2f, RightTag);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { HeaderBounds.Right() - TagW, HeaderBounds.Y + 4.0f },
            8.2f,
            Theme.TextMuted,
            RightTag);
        TrailingReserved = TagW + 8.0f;
    }

    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { HeaderBounds.X + IconW, HeaderBounds.Y + 1.0f },
        std::max(40.0f, HeaderBounds.Width - IconW - TrailingReserved),
        12.0f,
        Theme.TextPrimary,
        Title);

    (void)Expanded;
    return Hovered && Pressed;
}

inline bool ConstructCapsuleButtonControl(
    PixelSpace&                 Surface,
    Rectangle2D                 Bounds,
    std::string_view            Label,
    const TexturingThemeColors& Theme,
    Point2D                     Cursor,
    bool                        Pressed,
    bool                        Active         = false,
    bool                        HighlightStyle = false,
    ControlIcon                 LeadingIcon    = ControlIcon::Count,
    float                       FontPx         = 10.0f,
    float                       RadiusOverride = -1.0f,
    const OptionalTooltip&      Tooltip        = {}) noexcept
{
    const bool  Hovered = Bounds.Contains(Cursor);
    const float Radius  = (RadiusOverride >= 0.0f) ? RadiusOverride : (Bounds.Height * 0.5f);

    ColorRgba Fill   = Theme.WellSurface;
    ColorRgba Border = Theme.BorderSubtle;
    ColorRgba Ink    = Theme.TextMuted;

    if (HighlightStyle)
    {
        Fill   = Theme.HighlightFill;
        Border = Theme.HighlightFill;
        Ink    = Theme.HighlightInk;
    }
    else if (Active)
    {
        Fill   = Theme.AccentSoft;
        Border = Theme.AccentBorder;
        Ink    = Theme.TextPrimary;
    }
    else if (Hovered)
    {
        Fill   = Theme.HoverSurface;
        Border = Theme.BorderStrong;
        Ink    = Theme.TextPrimary;
    }
    else
    {
        Fill   = Theme.RaisedSurface;
        Border = Theme.BorderSubtle;
        Ink    = Theme.TextPrimary;
    }

    Surface.FillRectangle(Bounds, Fill, Radius);
    Surface.StrokeRectangle(Bounds, Border, Radius, 1.0f);

    const float IconSpan = (LeadingIcon != ControlIcon::Count) ? (FontPx + 6.0f) : 0.0f;
    const float TextW    = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, FontPx, Label);
    const float TotalW   = IconSpan + TextW;
    const float StartX   = Bounds.X + std::max(8.0f, (Bounds.Width - TotalW) * 0.5f);
    const float IconSz   = std::max(11.0f, FontPx + 2.0f);

    if (LeadingIcon != ControlIcon::Count)
    {
        VectorIconRepository::ConstructControl(
            Surface,
            LeadingIcon,
            VectorIconPlacement{ StartX, Bounds.Y + (Bounds.Height - IconSz) * 0.5f, IconSz, Ink, 1.7f });
    }
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { StartX + IconSpan, Bounds.Y + (Bounds.Height - FontPx * 1.25f) * 0.5f },
        std::max(10.0f, Bounds.Right() - (StartX + IconSpan) - 6.0f),
        FontPx,
        Ink,
        Label);

    if (Hovered && Tooltip.Enabled)
    {
        ControlAppearance::RecordHoveredTooltip(Bounds, Tooltip);
    }
    return Hovered && Pressed;
}

inline bool ConstructRoundIconButton(
    PixelSpace&                 Surface,
    Rectangle2D                 Bounds,
    ControlIcon                 Icon,
    const TexturingThemeColors& Theme,
    Point2D                     Cursor,
    bool                        Pressed,
    bool                        Active         = false,
    bool                        HighlightStyle = false,
    const OptionalTooltip&      Tooltip        = {}) noexcept
{
    const bool  Hovered = Bounds.Contains(Cursor);
    const float Radius  = std::min(Bounds.Width, Bounds.Height) * 0.5f;

    ColorRgba Fill   = HighlightStyle ? Theme.HighlightFill : (Active ? Theme.AccentSoft : (Hovered ? Theme.HoverSurface : Theme.RaisedSurface));
    ColorRgba Border = HighlightStyle ? Theme.HighlightFill : (Active ? Theme.AccentBorder : (Hovered ? Theme.BorderStrong : Theme.BorderSubtle));
    ColorRgba Ink    = HighlightStyle ? Theme.HighlightInk  : (Hovered || Active ? Theme.TextPrimary : Theme.TextMuted);

    Surface.FillRectangle(Bounds, Fill, Radius);
    Surface.StrokeRectangle(Bounds, Border, Radius, 1.0f);

    const float IconSz = std::min(15.0f, std::min(Bounds.Width, Bounds.Height) * 0.52f);
    VectorIconRepository::ConstructControl(
        Surface,
        Icon,
        VectorIconPlacement{
            Bounds.X + (Bounds.Width - IconSz) * 0.5f,
            Bounds.Y + (Bounds.Height - IconSz) * 0.5f,
            IconSz,
            Ink,
            1.7f,
        });

    if (Hovered && Tooltip.Enabled)
    {
        ControlAppearance::RecordHoveredTooltip(Bounds, Tooltip);
    }
    return Hovered && Pressed;
}

inline bool ConstructTextEditRow(
    PixelSpace&                 Surface,
    float                       LeftX,
    float                       TopY,
    float                       Width,
    std::string_view            Label,
    TextEditBuffer&             Buffer,
    const PaneInputReading&     Input,
    const TexturingThemeColors& Theme) noexcept
{
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX, TopY }, 10.5f, Theme.TextMuted, Label);

    const Rectangle2D BoxRect{ LeftX, TopY + 17.0f, Width, 28.0f };
    Surface.FillRectangle(BoxRect, Theme.WellSurface, 14.0f);
    Surface.StrokeRectangle(BoxRect, Buffer.Active ? Theme.AccentBorder : Theme.BorderSubtle, 14.0f, 1.0f);

    const ControlContact Contact = TextInputField::Construct(
        Surface, BoxRect, Buffer, Input.PrimaryPointer, 11.5f, 1.0f);
    return Contact.Clicked || Buffer.Finalized;
}

inline bool ConstructSwitchControlRow(
    PixelSpace&                 Surface,
    float                       LeftX,
    float                       TopY,
    float                       Width,
    std::string_view            Label,
    bool&                       InOutEnabled,
    const TexturingThemeColors& Theme,
    const ControlPointer&       Pointer,
    const OptionalTooltip&      Tooltip = {}) noexcept
{
    const Rectangle2D RowRect{ LeftX, TopY, Width, 28.0f };
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX, TopY + 6.0f }, 10.5f, Theme.TextMuted, Label);

    const Rectangle2D SwitchBounds{ LeftX + Width - 42.0f, TopY + 3.0f, 42.0f, 22.0f };
    const bool        Hovered = SwitchBounds.Contains({ Pointer.X, Pointer.Y });
    const ColorRgba   TrackFill = InOutEnabled
        ? (Theme.LightSurface ? Theme.AccentFill : ColorRgba{ 238u, 238u, 236u, 255u })
        : (Hovered ? Theme.HoverSurface : (Theme.LightSurface ? ColorRgba{ 206u, 212u, 222u, 255u } : ColorRgba{ 32u, 33u, 36u, 255u }));
    const ColorRgba   TrackBorder = InOutEnabled ? TrackFill : Theme.BorderStrong;
    const ColorRgba   KnobFill    = InOutEnabled
        ? (Theme.LightSurface ? Theme.HighlightInk : ColorRgba{ 21u, 22u, 24u, 255u })
        : (Theme.LightSurface ? ColorRgba{ 255u, 255u, 255u, 255u } : ColorRgba{ 165u, 168u, 176u, 255u });

    Surface.FillRectangle(SwitchBounds, TrackFill, 11.0f);
    Surface.StrokeRectangle(SwitchBounds, TrackBorder, 11.0f, 1.0f);
    const float KnobCenterX = InOutEnabled ? (SwitchBounds.Right() - 11.0f) : (SwitchBounds.X + 11.0f);
    Surface.FillCircle({ KnobCenterX, SwitchBounds.Y + 11.0f }, 7.5f, KnobFill);

    if (Hovered && Tooltip.Enabled)
    {
        ControlAppearance::RecordHoveredTooltip(SwitchBounds, Tooltip);
    }
    if (RowRect.Contains({ Pointer.X, Pointer.Y }) && Pointer.Pressed)
    {
        InOutEnabled = !InOutEnabled;
        return true;
    }
    return false;
}

inline bool ConstructDropdownBarRow(
    PixelSpace&                 Surface,
    float                       LeftX,
    float                       TopY,
    float                       Width,
    std::string_view            Label,
    std::string_view            SelectedDisplay,
    bool                        Open,
    const TexturingThemeColors& Theme,
    const ControlPointer&       Pointer,
    Rectangle2D&                OutButtonBounds,
    ControlIcon                 LeadingIcon = ControlIcon::Count,
    const OptionalTooltip&      Tooltip     = {}) noexcept
{
    float ControlTop = TopY;
    if (!Label.empty())
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { LeftX, TopY }, 10.5f, Theme.TextMuted, Label);
        ControlTop += 17.0f;
    }

    OutButtonBounds = Rectangle2D{ LeftX, ControlTop, Width, 30.0f };
    const bool Hovered = OutButtonBounds.Contains({ Pointer.X, Pointer.Y });
    Surface.FillRectangle(OutButtonBounds, Hovered ? Theme.HoverSurface : Theme.WellSurface, 15.0f);
    Surface.StrokeRectangle(OutButtonBounds, Open ? Theme.AccentBorder : Theme.BorderSubtle, 15.0f, 1.0f);

    float TextX = OutButtonBounds.X + 12.0f;
    if (LeadingIcon != ControlIcon::Count)
    {
        VectorIconRepository::ConstructControl(
            Surface, LeadingIcon, VectorIconPlacement{ TextX, OutButtonBounds.Y + 8.0f, 14.0f, Theme.TextMuted, 1.6f });
        TextX += 20.0f;
    }

    const float TextMaxW = std::max(32.0f, (OutButtonBounds.Right() - 28.0f) - TextX);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { TextX, OutButtonBounds.Y + 8.0f }, TextMaxW, 11.0f, Theme.TextPrimary, SelectedDisplay);
    VectorIconRepository::ConstructControl(
        Surface,
        Open ? ControlIcon::ChevronUp : ControlIcon::ChevronDown,
        VectorIconPlacement{ OutButtonBounds.Right() - 20.0f, OutButtonBounds.Y + 9.0f, 12.0f, Theme.TextMuted, 1.8f });

    if (Hovered && Tooltip.Enabled)
    {
        ControlAppearance::RecordHoveredTooltip(OutButtonBounds, Tooltip);
    }
    return Hovered && Pointer.Pressed;
}

inline bool ConstructSliderReadoutRow(
    PixelSpace&                 Surface,
    float                       LeftX,
    float                       TopY,
    float                       Width,
    std::string_view            Label,
    std::string_view            NumberText,
    std::string_view            UnitText,
    float&                      InOutNormalized,
    const TexturingThemeColors& Theme,
    const ControlPointer&       Pointer,
    bool&                       InOutDragging,
    const OptionalTooltip&      Tooltip = {}) noexcept
{
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX, TopY }, 10.5f, Theme.TextMuted, Label);

    constexpr float CapsuleW = 88.0f;
    constexpr float CapsuleH = 28.0f;
    constexpr float ControlY = 17.0f;

    const Rectangle2D ReadoutCapsule{ LeftX, TopY + ControlY, CapsuleW, CapsuleH };
    Surface.FillRectangle(ReadoutCapsule, Theme.WellSurface, 14.0f);
    Surface.StrokeRectangle(ReadoutCapsule, Theme.BorderSubtle, 14.0f, 1.0f);
    Surface.FillRectangle(
        { ReadoutCapsule.Right() - 30.0f, ReadoutCapsule.Y + 1.0f, 29.0f, ReadoutCapsule.Height - 2.0f },
        Theme.NestedSurface,
        0.0f);
    Surface.StrokeLine(
        { ReadoutCapsule.Right() - 30.0f, ReadoutCapsule.Y + 4.0f },
        { ReadoutCapsule.Right() - 30.0f, ReadoutCapsule.Bottom() - 4.0f },
        Theme.BorderSubtle,
        1.0f);

    const float NumW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, NumberText);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { ReadoutCapsule.X + (CapsuleW - 30.0f - NumW) * 0.5f, ReadoutCapsule.Y + 7.5f },
        11.0f,
        Theme.TextPrimary,
        NumberText);
    const float UnitW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, UnitText);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { ReadoutCapsule.Right() - 15.0f - UnitW * 0.5f, ReadoutCapsule.Y + 8.5f },
        9.0f,
        Theme.TextMuted,
        UnitText);

    const float       TrackX = ReadoutCapsule.Right() + 9.0f;
    const float       TrackW = std::max(36.0f, (LeftX + Width) - TrackX);
    const Rectangle2D Track  { TrackX, TopY + ControlY + 1.0f, TrackW, 26.0f };
    const Rectangle2D HitBox { Track.X - 2.0f, Track.Y - 4.0f, Track.Width + 4.0f, Track.Height + 8.0f };
    const bool        Hover  = HitBox.Contains({ Pointer.X, Pointer.Y });

    if (Hover && Tooltip.Enabled)
    {
        ControlAppearance::RecordHoveredTooltip({ LeftX, TopY, Width, ControlY + CapsuleH }, Tooltip);
    }

    constexpr float ThumbRadius = 11.5f;
    const float     InnerSpan   = std::max(1.0f, Track.Width - ThumbRadius * 2.0f);
    const float     Clamped     = std::clamp(InOutNormalized, 0.0f, 1.0f);
    const float     ThumbX      = Track.X + ThumbRadius + InnerSpan * Clamped;
    const float     CenterY     = Track.Y + Track.Height * 0.5f;

    const ColorRgba RailColor = Theme.LightSurface ? ColorRgba{ 212u, 217u, 225u, 255u } : ColorRgba{ 36u, 36u, 38u, 255u };
    const ColorRgba FillColor = Theme.LightSurface ? ColorRgba{ 162u, 171u, 186u, 255u } : ColorRgba{ 68u, 68u, 72u, 255u };
    const ColorRgba KnobColor = Theme.LightSurface ? ColorRgba{ 255u, 255u, 255u, 255u } : ColorRgba{ 238u, 238u, 236u, 255u };
    const ColorRgba KnobRing  = Theme.LightSurface ? ColorRgba{ 15u,  23u,  42u,  65u }  : ColorRgba{ 0u,  0u,  0u,  96u };

    Surface.FillRectangle(Track, RailColor, 13.0f);
    Surface.FillRectangle({ Track.X, Track.Y, std::max(24.0f, ThumbX + ThumbRadius - Track.X), Track.Height }, FillColor, 13.0f);
    Surface.FillCircle({ ThumbX, CenterY }, ThumbRadius, KnobColor);
    Surface.StrokeCircle({ ThumbX, CenterY }, ThumbRadius, KnobRing, 1.0f);

    bool Changed = false;
    if (Pointer.Pressed && Hover)
    {
        InOutDragging = true;
    }
    if (!Pointer.Down)
    {
        InOutDragging = false;
    }
    if (InOutDragging && (Pointer.Down || Pointer.Pressed))
    {
        const float Next = std::clamp((Pointer.X - (Track.X + ThumbRadius)) / InnerSpan, 0.0f, 1.0f);
        if (std::abs(Next - InOutNormalized) > 1.0e-4f)
        {
            InOutNormalized = Next;
            Changed         = true;
        }
    }
    return Changed;
}

inline bool ConstructColourSwatchRow(
    PixelSpace&                 Surface,
    float                       LeftX,
    float                       TopY,
    float                       Width,
    std::string_view            Label,
    std::string_view            HexText,
    ColorRgba                   SwatchColour,
    const TexturingThemeColors& Theme,
    const ControlPointer&       Pointer,
    Rectangle2D&                OutRowBounds,
    const OptionalTooltip&      Tooltip = {}) noexcept
{
    OutRowBounds = Rectangle2D{ LeftX, TopY, Width, 30.0f };
    const bool Hovered = OutRowBounds.Contains({ Pointer.X, Pointer.Y });
    Surface.FillRectangle(OutRowBounds, Hovered ? Theme.HoverSurface : Theme.WellSurface, 15.0f);
    Surface.StrokeRectangle(OutRowBounds, Hovered ? Theme.BorderStrong : Theme.BorderSubtle, 15.0f, 1.0f);

    const Point2D SwatchCenter{ OutRowBounds.X + 16.0f, OutRowBounds.Y + 15.0f };
    Surface.FillCircle(SwatchCenter, 9.0f, SwatchColour);
    Surface.StrokeCircle(SwatchCenter, 9.0f, ColorRgba{ 255u, 255u, 255u, 85u }, 1.0f);

    const float HexW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, HexText, true);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { OutRowBounds.Right() - 12.0f - HexW, OutRowBounds.Y + 8.5f }, 10.0f, Theme.TextMuted, HexText, true);

    const float LabelMaxW = std::max(40.0f, (OutRowBounds.Right() - 18.0f - HexW) - (OutRowBounds.X + 34.0f));
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { OutRowBounds.X + 34.0f, OutRowBounds.Y + 8.0f }, LabelMaxW, 10.5f, Theme.TextPrimary, Label);

    if (Hovered && Tooltip.Enabled)
    {
        ControlAppearance::RecordHoveredTooltip(OutRowBounds, Tooltip);
    }
    return Hovered && Pointer.Pressed;
}

inline bool ConstructSegmentedCapsuleRow(
    PixelSpace&                 Surface,
    float                       LeftX,
    float                       TopY,
    float                       Width,
    std::string_view            Label,
    const char* const*          OptionLabels,
    uint32_t                    OptionCount,
    uint32_t                    ActiveIndex,
    uint32_t&                   OutChosenIndex,
    const TexturingThemeColors& Theme,
    const ControlPointer&       Pointer) noexcept
{
    float ControlY = TopY;
    if (!Label.empty())
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { LeftX, TopY }, 10.5f, Theme.TextMuted, Label);
        ControlY += 17.0f;
    }

    const Rectangle2D TrackRect{ LeftX, ControlY, Width, 30.0f };
    Surface.FillRectangle(TrackRect, Theme.WellSurface, 15.0f);
    Surface.StrokeRectangle(TrackRect, Theme.BorderSubtle, 15.0f, 1.0f);

    const float SegW = (Width - 6.0f) / static_cast<float>(std::max(1u, OptionCount));
    bool Changed   = false;
    OutChosenIndex = ActiveIndex;

    for (uint32_t Idx = 0u; Idx < OptionCount; ++Idx)
    {
        const Rectangle2D SegRect{ TrackRect.X + 3.0f + static_cast<float>(Idx) * SegW, TrackRect.Y + 3.0f, SegW, 24.0f };
        const bool Active  = (Idx == ActiveIndex);
        const bool Hovered = SegRect.Contains({ Pointer.X, Pointer.Y });
        if (Active || Hovered)
        {
            Surface.FillRectangle(SegRect, Active ? Theme.RaisedSurface : Theme.HoverSurface, 12.0f);
            if (Active)
            {
                Surface.StrokeRectangle(SegRect, Theme.BorderStrong, 12.0f, 1.0f);
            }
        }
        const float LabelW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, OptionLabels[Idx]);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { SegRect.X + std::max(4.0f, (SegRect.Width - LabelW) * 0.5f), SegRect.Y + 6.0f },
            std::max(8.0f, SegRect.Width - 8.0f),
            9.5f,
            Active || Hovered ? Theme.TextPrimary : Theme.TextMuted,
            OptionLabels[Idx]);
        if (Hovered && Pointer.Pressed)
        {
            OutChosenIndex = Idx;
            Changed = true;
        }
    }
    return Changed;
}

[[nodiscard]] inline ChipStripOutcome ConstructWrappedChipStrip(
    PixelSpace&                 Surface,
    float                       LeftX,
    float                       TopY,
    float                       MaxWidth,
    std::span<const char* const> Labels,
    int32_t                     ActiveIndex,
    const TexturingThemeColors& Theme,
    Point2D                     Cursor,
    bool                        Pressed,
    float                       HeightPx = 24.0f,
    float                       FontPx   = 9.5f) noexcept
{
    ChipStripOutcome Result {};
    float CurX = LeftX;
    float CurY = TopY;
    constexpr float GapX = 6.0f;
    constexpr float GapY = 6.0f;

    for (std::size_t Idx = 0u; Idx < Labels.size(); ++Idx)
    {
        const std::string_view Text = Labels[Idx];
        const float ChipW = std::min(MaxWidth, WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, FontPx, Text) + 26.0f);
        if (CurX > LeftX && (CurX + ChipW) > (LeftX + MaxWidth + 0.5f))
        {
            CurX = LeftX;
            CurY += HeightPx + GapY;
        }
        const Rectangle2D ChipRect{ CurX, CurY, ChipW, HeightPx };
        const bool Active  = (static_cast<int32_t>(Idx) == ActiveIndex);
        const bool Hovered = ChipRect.Contains(Cursor);

        Surface.FillRectangle(ChipRect, Active ? Theme.AccentSoft : (Hovered ? Theme.HoverSurface : Theme.RaisedSurface), HeightPx * 0.5f);
        Surface.StrokeRectangle(ChipRect, Active ? Theme.AccentBorder : (Hovered ? Theme.BorderStrong : Theme.BorderSubtle), HeightPx * 0.5f, 1.0f);
        const float LabelMeasuredW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, FontPx, Text);
        const float LabelStartX    = ChipRect.X + std::max(8.0f, (ChipRect.Width - LabelMeasuredW) * 0.5f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { LabelStartX, ChipRect.Y + (HeightPx - FontPx * 1.2f) * 0.5f },
            std::max(10.0f, ChipRect.Width - 16.0f),
            FontPx,
            Active || Hovered ? Theme.TextPrimary : Theme.TextMuted,
            Text);
        if (Hovered && Pressed)
        {
            Result.ClickedIndex = static_cast<int32_t>(Idx);
        }
        CurX += ChipW + GapX;
    }

    Result.ConsumedHeight = (Labels.empty()) ? 0.0f : ((CurY - TopY) + HeightPx);
    return Result;
}

[[nodiscard]] inline RemovableChipOutcome ConstructRemovableChannelChips(
    PixelSpace&                          Surface,
    float                                LeftX,
    float                                TopY,
    float                                MaxWidth,
    std::span<const RemovableChipEntry>  Chips,
    std::string_view                     AddTriggerLabel,
    bool                                 AddTriggerOpen,
    const TexturingThemeColors&          Theme,
    Point2D                              Cursor,
    bool                                 Pressed) noexcept
{
    RemovableChipOutcome Outcome {};
    constexpr float ChipH = 22.0f;
    constexpr float GapX  = 5.0f;
    constexpr float GapY  = 5.0f;

    float CurX = LeftX;
    float CurY = TopY;

    for (std::size_t Idx = 0u; Idx < Chips.size(); ++Idx)
    {
        const auto& Entry = Chips[Idx];
        const float LabelW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, Entry.Label);
        const float ChipW  = std::min(MaxWidth, 18.0f + LabelW + 18.0f);
        if (CurX > LeftX && (CurX + ChipW) > (LeftX + MaxWidth + 0.5f))
        {
            CurX = LeftX;
            CurY += ChipH + GapY;
        }
        const Rectangle2D ChipR{ CurX, CurY, ChipW, ChipH };
        const bool Hovered = ChipR.Contains(Cursor);

        Surface.FillRectangle(ChipR, Entry.Active ? Theme.AccentSoft : (Hovered ? Theme.HoverSurface : Theme.WellSurface), ChipH * 0.5f);
        Surface.StrokeRectangle(ChipR, Entry.Active ? Theme.AccentBorder : Theme.BorderSubtle, ChipH * 0.5f, 1.0f);

        Surface.FillCircle({ ChipR.X + 9.0f, ChipR.Y + ChipH * 0.5f }, 2.7f, Entry.DotTint);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { ChipR.X + 16.0f, ChipR.Y + 5.0f },
            std::max(12.0f, ChipW - 32.0f),
            9.0f,
            Entry.Active || Hovered ? Theme.TextPrimary : Theme.TextMuted,
            Entry.Label);

        const Rectangle2D CloseHit{ ChipR.Right() - 16.0f, ChipR.Y, 16.0f, ChipH };
        const bool CloseHover = CloseHit.Contains(Cursor);
        VectorIconRepository::ConstructControl(
            Surface,
            ControlIcon::CloseCross,
            VectorIconPlacement{
                CloseHit.X + 3.5f,
                ChipR.Y + 7.5f,
                7.5f,
                CloseHover ? Theme.DangerInk : Theme.TextFaint,
                1.7f,
            });

        if (Pressed)
        {
            if (CloseHover)
            {
                Outcome.RemovedIndex = static_cast<int32_t>(Idx);
            }
            else if (Hovered)
            {
                Outcome.SelectedIndex = static_cast<int32_t>(Idx);
            }
        }
        CurX += ChipW + GapX;
    }

    if (!AddTriggerLabel.empty())
    {
        const float TrigW = std::min(
            MaxWidth,
            WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.2f, AddTriggerLabel) + 28.0f);
        if (CurX > LeftX && (CurX + TrigW) > (LeftX + MaxWidth + 0.5f))
        {
            CurX = LeftX;
            CurY += ChipH + GapY;
        }
        const Rectangle2D AddR{ CurX, CurY, TrigW, ChipH };
        const bool AddHover = AddR.Contains(Cursor);
        Surface.FillRectangle(AddR, AddTriggerOpen ? Theme.HoverSurface : (AddHover ? Theme.RaisedSurface : Theme.WellSurface), ChipH * 0.5f);
        Surface.StrokeRectangle(AddR, AddTriggerOpen ? Theme.AccentBorder : Theme.BorderStrong, ChipH * 0.5f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { AddR.X + 9.0f, AddR.Y + 5.0f }, 9.2f, Theme.TextPrimary, AddTriggerLabel);
        VectorIconRepository::ConstructControl(
            Surface,
            AddTriggerOpen ? ControlIcon::ChevronUp : ControlIcon::ChevronDown,
            VectorIconPlacement{ AddR.Right() - 16.0f, AddR.Y + 6.5f, 9.0f, Theme.TextMuted, 1.7f });
        Outcome.AddTriggerBounds = AddR;
        if (AddHover && Pressed)
        {
            Outcome.AddTriggerClicked = true;
        }
    }

    Outcome.ConsumedHeight = (CurY - TopY) + ChipH;
    return Outcome;
}

inline void ConstructPaneStatusFooter(
    PixelSpace&                 Surface,
    Rectangle2D                 Bounds,
    std::string_view            LeftLabel,
    std::string_view            RightLabel,
    const TexturingThemeColors& Theme,
    bool                        ShowReadyDot = false) noexcept
{
    Surface.FillRectangle(Bounds, Theme.LightSurface ? Theme.NestedSurface : WorkspaceSurfaceColors::FooterBackground);
    Surface.StrokeLine({ Bounds.X, Bounds.Y }, { Bounds.Right(), Bounds.Y }, Theme.BorderSubtle, 1.0f);

    float LeftTextX = Bounds.X + 12.0f;
    if (ShowReadyDot)
    {
        Surface.FillCircle({ Bounds.X + 14.0f, Bounds.Y + Bounds.Height * 0.5f }, 3.2f, Theme.ReadyGreen);
        LeftTextX += 10.0f;
    }
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { LeftTextX, Bounds.Y + 6.0f }, Bounds.Width * 0.55f, 8.8f, Theme.TextMuted, LeftLabel);
    if (!RightLabel.empty())
    {
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface, FontWeightClass::Regular, { Bounds.Right() - 12.0f, Bounds.Y + 6.0f }, 8.8f, Theme.TextFaint, RightLabel);
    }
}

inline void RenderFittedDropdownSheet(
    PixelSpace&                          Surface,
    Rectangle2D                          ButtonBounds,
    Rectangle2D                          ViewportBounds,
    const char* const*                   Options,
    uint32_t                             OptionCount,
    uint32_t                             SelectedIndex,
    bool&                                InOutOpen,
    const PaneInputReading&              Input,
    const std::function<void(uint32_t)>& SelectionCallback,
    float*                               InOutScrollY = nullptr)
{
    if (!InOutOpen || OptionCount == 0u)
    {
        return;
    }
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const float   MinSheetW = std::clamp(std::max(ButtonBounds.Width, 206.0f), 140.0f, std::max(140.0f, ViewportBounds.Width - 16.0f));
    const float   SheetX    = std::clamp(ButtonBounds.X, ViewportBounds.X + 8.0f, std::max(ViewportBounds.X + 8.0f, ViewportBounds.Right() - MinSheetW - 8.0f));

    if (OptionCount <= 8u)
    {
        Rectangle2D SheetAnchor{ SheetX, ButtonBounds.Y, MinSheetW, ButtonBounds.Height };
        Rectangle2D SheetBounds = DropdownSelector::QuerySheetBounds(SheetAnchor, OptionCount);

        if (SheetBounds.Bottom() > ViewportBounds.Bottom() - 8.0f)
        {
            const float DesiredTop = ButtonBounds.Y - 4.0f - SheetBounds.Height;
            if (DesiredTop >= ViewportBounds.Y + 8.0f)
            {
                SheetAnchor.Y = DesiredTop - ControlMetrics::DropdownPaddingY - ButtonBounds.Height;
                SheetBounds   = DropdownSelector::QuerySheetBounds(SheetAnchor, OptionCount);
            }
            else
            {
                SheetAnchor.Y -= std::max(0.0f, SheetBounds.Bottom() - (ViewportBounds.Bottom() - 8.0f));
                SheetBounds    = DropdownSelector::QuerySheetBounds(SheetAnchor, OptionCount);
            }
        }

        uint32_t ChosenIndex = SelectedIndex;
        const ControlContact Contact = DropdownSelector::ConstructOptionSheet(
            Surface, SheetAnchor, Options, OptionCount, SelectedIndex, Input.PrimaryPointer, ChosenIndex);
        if (Contact.Clicked)
        {
            SelectionCallback(ChosenIndex);
            InOutOpen = false;
        }
        else if (Input.PrimaryPointer.Released && !ButtonBounds.Contains(Cursor) && !SheetBounds.Contains(Cursor))
        {
            InOutOpen = false;
        }
        return;
    }

    // Compact scrollable dropdown sheet for large lists (e.g. 29 PBR channels)
    const TexturingThemeColors Theme = QueryTexturingThemeColors();
    constexpr float RowH      = 24.0f;
    constexpr float PadY      = 6.0f;
    const float     FullH     = PadY * 2.0f + static_cast<float>(OptionCount) * RowH;
    const float     MaxSheetH = std::min(264.0f, std::max(96.0f, ViewportBounds.Height * 0.45f));
    const float     SheetH    = std::min(FullH, MaxSheetH);
    float           SheetY    = ButtonBounds.Bottom() + 4.0f;
    if (SheetY + SheetH > ViewportBounds.Bottom() - 8.0f)
    {
        SheetY = std::max(ViewportBounds.Y + 8.0f, ButtonBounds.Y - SheetH - 4.0f);
    }

    const Rectangle2D SheetR{ SheetX, SheetY, MinSheetW, SheetH };
    float LocalScroll = (InOutScrollY != nullptr) ? *InOutScrollY : 0.0f;
    if (SheetR.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 1.0e-4f)
    {
        LocalScroll = ControlAppearance::AdvanceScrollOffset(LocalScroll, Input.WheelDeltaY * 24.0f, FullH, SheetH);
        if (InOutScrollY != nullptr)
        {
            *InOutScrollY = LocalScroll;
        }
    }
    LocalScroll = std::clamp(LocalScroll, 0.0f, std::max(0.0f, FullH - SheetH));

    Surface.FillRectangle(SheetR, Theme.NestedSurface, 12.0f);
    Surface.StrokeRectangle(SheetR, Theme.BorderStrong, 12.0f, 1.0f);

    Surface.PushClip({ SheetR.X + 2.0f, SheetR.Y + 2.0f, SheetR.Width - 4.0f, SheetR.Height - 4.0f });
    for (uint32_t Idx = 0u; Idx < OptionCount; ++Idx)
    {
        const Rectangle2D RowR{ SheetR.X + 5.0f, SheetR.Y + PadY + static_cast<float>(Idx) * RowH - LocalScroll, SheetR.Width - 10.0f, RowH - 2.0f };
        if (RowR.Bottom() < SheetR.Y || RowR.Y > SheetR.Bottom())
        {
            continue;
        }
        const bool Active = (Idx == SelectedIndex);
        const bool Hover  = SheetR.Contains(Cursor) && RowR.Contains(Cursor);
        if (Active || Hover)
        {
            Surface.FillRectangle(RowR, Active ? Theme.AccentSoft : Theme.HoverSurface, 7.0f);
        }
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { RowR.X + 8.0f, RowR.Y + 5.0f },
            RowR.Width - 16.0f,
            10.0f,
            Active || Hover ? Theme.TextPrimary : Theme.TextMuted,
            Options[Idx]);
        if (Hover && Input.PrimaryPointer.Released)
        {
            SelectionCallback(Idx);
            InOutOpen = false;
            Surface.PopClip();
            return;
        }
    }
    Surface.PopClip();

    if (Input.PrimaryPointer.Released && !ButtonBounds.Contains(Cursor) && !SheetR.Contains(Cursor))
    {
        InOutOpen = false;
    }
}

} // namespace Frontier::Workspaces::TexturingAppearance
