/*============================================================================================================================================
                                                             DROPDOWNSELECTOR.CPP
============================================================================================================================================*/
// 🧩 Reusable Studio Controls rounded dropdown selector button, option sheet, and directive sheet

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/SmoothScrollView.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Frontier
{

namespace DropdownSelector
{

namespace
{

constexpr uint32_t MaxVisibleSheetRows    = 10u;
constexpr float    OptionFontSizePx       = 13.0f;
constexpr float    OptionTextPadLeftPx    = 14.0f;
constexpr float    OptionTextPadRightPx   = 18.0f;
constexpr float    ScrollbarLaneReservePx = 12.0f;

struct DropdownScrollSlot
{
    uint64_t         Key              = 0ull;
    uint64_t         LastSeenFrame    = 0ull;
    Rectangle2D      LastSheetBounds  {};
    bool             HasSheetBounds   = false;
    SmoothScrollView Scroll           {};
};

constexpr std::size_t                                 ScrollSlotCapacity = 16u;
std::array<DropdownScrollSlot, ScrollSlotCapacity>    ScrollSlots {};
uint64_t                                              ScrollFrameCounter = 0ull;

[[nodiscard]] uint64_t ComputeDropdownKey(Rectangle2D ButtonBounds, uint32_t OptionCount) noexcept
{
    const uint64_t Qx = static_cast<uint64_t>(std::llround(ButtonBounds.X * 2.0f)) & 0xFFFFull;
    const uint64_t Qy = static_cast<uint64_t>(std::llround(ButtonBounds.Y * 2.0f)) & 0xFFFFull;
    const uint64_t Qw = static_cast<uint64_t>(std::llround(ButtonBounds.Width * 2.0f)) & 0xFFFFull;
    return (Qx) | (Qy << 16u) | (Qw << 32u) | (static_cast<uint64_t>(OptionCount & 0xFFFFu) << 48u);
}

[[nodiscard]] DropdownScrollSlot* FindDropdownSlot(uint64_t Key) noexcept
{
    for (DropdownScrollSlot& Slot : ScrollSlots)
    {
        if (Slot.Key == Key)
        {
            return &Slot;
        }
    }
    return nullptr;
}

[[nodiscard]] DropdownScrollSlot& AcquireDropdownSlot(uint64_t Key, bool& OutFreshlyOpened) noexcept
{
    const uint64_t FrameIdx = (ImGui::GetCurrentContext() != nullptr)
        ? static_cast<uint64_t>(ImGui::GetFrameCount())
        : ++ScrollFrameCounter;

    DropdownScrollSlot* Oldest = &ScrollSlots[0];
    for (DropdownScrollSlot& Slot : ScrollSlots)
    {
        if (Slot.Key == Key)
        {
            OutFreshlyOpened   = (FrameIdx > Slot.LastSeenFrame + 2ull);
            Slot.LastSeenFrame = FrameIdx;
            return Slot;
        }
        if (Slot.LastSeenFrame < Oldest->LastSeenFrame)
        {
            Oldest = &Slot;
        }
    }

    *Oldest               = {};
    Oldest->Key           = Key;
    Oldest->LastSeenFrame = FrameIdx;
    OutFreshlyOpened      = true;
    return *Oldest;
}

[[nodiscard]] float ComputeTotalOptionContentHeight(uint32_t OptionCount) noexcept
{
    if (OptionCount == 0u)
    {
        return 0.0f;
    }
    return static_cast<float>(OptionCount) * ControlMetrics::DropdownOptionHeight +
           static_cast<float>(OptionCount - 1u) * ControlMetrics::DropdownRowGap;
}

[[nodiscard]] float ComputeSheetWidthForOptions(
    PixelSpace&        Surface,
    Rectangle2D        ButtonBounds,
    const char* const* Options,
    uint32_t           OptionCount) noexcept
{
    const bool NeedsScroll = OptionCount > MaxVisibleSheetRows;
    float      WidestText  = 0.0f;
    if (Options != nullptr)
    {
        for (uint32_t Index = 0u; Index < OptionCount; ++Index)
        {
            if (Options[Index] != nullptr)
            {
                WidestText = std::max(WidestText, Surface.MeasureTextWidth(OptionFontSizePx, Options[Index]));
            }
        }
    }
    const float RequiredWidth =
        WidestText +
        OptionTextPadLeftPx +
        OptionTextPadRightPx +
        2.0f * ControlMetrics::DropdownPaddingX +
        (NeedsScroll ? ScrollbarLaneReservePx : 0.0f);
    return std::max(ButtonBounds.Width, std::ceil(RequiredWidth));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                DROPDOWN CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

ControlContact ConstructButton(
    PixelSpace&            Surface,
    Rectangle2D            Bounds,
    const char*            CurrentLabel,
    bool                   Open,
    const ControlPointer&  Pointer,
    float                  Opacity,
    ControlIcon            LeadingIcon,
    const OptionalTooltip& Tooltip) noexcept
{
    const ControlColorSet& Colors = ControlAppearance::Colors();
    ControlContact         Contact{};
    Contact.Hovered = ControlAppearance::QueryPointerOver(Bounds, Pointer);
    Contact.Pressed = Contact.Hovered && Pointer.Pressed;
    Contact.Clicked = Contact.Hovered && Pointer.Released;

    const float       Height   = Bounds.Height;
    const float       Radius   = Height * 0.5f;
    const float       CaretW   = (Height >= 38.0f) ? 40.0f : std::clamp(Height * 0.96f, 28.0f, 36.0f);
    const float       FontSize = (Height >= 38.0f) ? 13.5f : std::clamp(Height * 0.39f, 11.5f, 12.5f);
    const float       IconSize = (Height >= 38.0f) ? 16.0f : std::clamp(Height * 0.45f, 12.0f, 15.0f);
    const Rectangle2D CaretCell{ Bounds.Right() - CaretW, Bounds.Y, CaretW, Height };

    Surface.FillRectangle(Bounds, Colors.Field.Faded(Opacity).ToRgba8(), Radius);
    Surface.FillRectangle(
        { CaretCell.X - Radius, Bounds.Y, CaretW + Radius, Height },
        (Contact.Hovered || Open ? Colors.Raised : Colors.Inset).Faded(Opacity).ToRgba8(),
        Radius);
    Surface.FillRectangle({ CaretCell.X - Radius, Bounds.Y, Radius, Height }, Colors.Field.Faded(Opacity).ToRgba8(), 0.0f);
    Surface.FillRectangle({ CaretCell.X, Bounds.Y, 1.0f, Height }, Colors.Stroke.Faded(Opacity).ToRgba8(), 0.0f);
    ControlAppearance::ConstructRoundedOutline(
        Surface,
        Bounds,
        (Open ? Colors.StrokeStrong : Colors.Stroke).Faded(Opacity),
        Radius);

    float TextPadX = (Height >= 36.0f) ? 16.0f : 12.0f;
    if (LeadingIcon != ControlIcon::Count)
    {
        const float IconSide = std::clamp(Height * 0.44f, 12.0f, 15.0f);
        ControlAppearance::ConstructIcon(
            Surface,
            Bounds.X + TextPadX,
            Bounds.Y + (Height - IconSide) * 0.5f,
            IconSide,
            Colors.TextDim.Faded(Opacity),
            LeadingIcon,
            1.7f);
        TextPadX += IconSide + 7.0f;
    }

    const Rectangle2D LabelBounds{
        Bounds.X,
        Bounds.Y,
        std::max(16.0f, CaretCell.X - Bounds.X - 4.0f),
        Height,
    };
    ControlAppearance::TextLeading(Surface, LabelBounds, TextPadX, Colors.Text.Faded(Opacity), CurrentLabel, FontSize);
    ControlAppearance::ConstructIconCentered(
        Surface,
        CaretCell,
        IconSize,
        Colors.TextDim.Faded(Opacity),
        Open ? ControlIcon::ChevronUp : ControlIcon::ChevronDown);
    if (Contact.Hovered && !Open && Tooltip.Enabled)
    {
        ControlAppearance::RecordHoveredTooltip(Bounds, Tooltip);
    }
    return Contact;
}

bool QueryAnyScrollAnimating() noexcept
{
    const uint64_t CurrentFrame = (ImGui::GetCurrentContext() != nullptr)
        ? static_cast<uint64_t>(ImGui::GetFrameCount())
        : ScrollFrameCounter;
    for (const DropdownScrollSlot& Slot : ScrollSlots)
    {
        if (Slot.Key != 0ull && CurrentFrame <= Slot.LastSeenFrame + 2ull && Slot.Scroll.QueryAnimating())
        {
            return true;
        }
    }
    return false;
}

Rectangle2D QuerySheetBounds(Rectangle2D ButtonBounds, uint32_t OptionCount) noexcept
{
    if (const DropdownScrollSlot* Slot = FindDropdownSlot(ComputeDropdownKey(ButtonBounds, OptionCount)))
    {
        if (Slot->HasSheetBounds && Slot->LastSheetBounds.Width >= ButtonBounds.Width && Slot->LastSheetBounds.Height > 0.0f)
        {
            return Slot->LastSheetBounds;
        }
    }
    const uint32_t VisibleRows   = std::min(OptionCount, MaxVisibleSheetRows);
    const float    VisibleHeight = ComputeTotalOptionContentHeight(VisibleRows);
    const float    TotalHeight   = 2.0f * ControlMetrics::DropdownPaddingY + VisibleHeight;
    return { ButtonBounds.X, ButtonBounds.Bottom() + ControlMetrics::DropdownPaddingY, ButtonBounds.Width, TotalHeight };
}

Rectangle2D QuerySheetBounds(
    PixelSpace&        Surface,
    Rectangle2D        ButtonBounds,
    const char* const* Options,
    uint32_t           OptionCount) noexcept
{
    const uint32_t VisibleRows   = std::min(OptionCount, MaxVisibleSheetRows);
    const float    VisibleHeight = ComputeTotalOptionContentHeight(VisibleRows);
    const float    TotalHeight   = 2.0f * ControlMetrics::DropdownPaddingY + VisibleHeight;
    const float    SheetWidth    = ComputeSheetWidthForOptions(Surface, ButtonBounds, Options, OptionCount);

    float SheetX = ButtonBounds.X;
    float SheetY = ButtonBounds.Bottom() + ControlMetrics::DropdownPaddingY;

    if (ImGui::GetCurrentContext() != nullptr)
    {
        const float   SafeScale  = std::max(0.25f, Surface.QueryScale());
        const Point2D Origin     = Surface.QueryOrigin();
        const ImVec2  DispSize   = ImGui::GetIO().DisplaySize;
        if (DispSize.x > 64.0f && DispSize.y > 64.0f)
        {
            const float LogicalLeft   = -Origin.X / SafeScale + 6.0f;
            const float LogicalRight  = (DispSize.x - Origin.X) / SafeScale - 6.0f;
            const float LogicalTop    = -Origin.Y / SafeScale + 6.0f;
            const float LogicalBottom = (DispSize.y - Origin.Y) / SafeScale - 6.0f;
            if (SheetX + SheetWidth > LogicalRight)
            {
                SheetX = std::max(LogicalLeft, LogicalRight - SheetWidth);
            }
            if (SheetY + TotalHeight > LogicalBottom)
            {
                const float AboveY = ButtonBounds.Y - ControlMetrics::DropdownPaddingY - TotalHeight;
                if (AboveY >= LogicalTop)
                {
                    SheetY = AboveY;
                }
                else
                {
                    SheetY = std::max(LogicalTop, LogicalBottom - TotalHeight);
                }
            }
        }
    }

    return { SheetX, SheetY, SheetWidth, TotalHeight };
}

ControlContact ConstructOptionSheet(
    PixelSpace&           Surface,
    Rectangle2D           ButtonBounds,
    const char* const*    Options,
    uint32_t              OptionCount,
    uint32_t              SelectedIndex,
    const ControlPointer& Pointer,
    uint32_t&             ChosenIndex,
    float                 Opacity) noexcept
{
    const ControlColorSet& Colors      = ControlAppearance::Colors();
    const Rectangle2D      SheetBounds = QuerySheetBounds(Surface, ButtonBounds, Options, OptionCount);
    const bool             Scrollable  = OptionCount > MaxVisibleSheetRows;
    const float            ContentH    = ComputeTotalOptionContentHeight(OptionCount);
    const Rectangle2D      ListViewport{
        SheetBounds.X + ControlMetrics::DropdownPaddingX,
        SheetBounds.Y + ControlMetrics::DropdownPaddingY,
        std::max(16.0f, SheetBounds.Width - 2.0f * ControlMetrics::DropdownPaddingX),
        std::max(16.0f, SheetBounds.Height - 2.0f * ControlMetrics::DropdownPaddingY),
    };
    const float RowStride = ControlMetrics::DropdownOptionHeight + ControlMetrics::DropdownRowGap;
    const float RowWidth  = std::max(16.0f, ListViewport.Width - (Scrollable ? ScrollbarLaneReservePx : 0.0f));

    bool                FreshlyOpened = false;
    DropdownScrollSlot& Slot          = AcquireDropdownSlot(ComputeDropdownKey(ButtonBounds, OptionCount), FreshlyOpened);
    Slot.LastSheetBounds              = SheetBounds;
    Slot.HasSheetBounds               = true;

    if (Scrollable && FreshlyOpened && SelectedIndex < OptionCount)
    {
        const float SelTop    = static_cast<float>(SelectedIndex) * RowStride;
        const float SelBottom = SelTop + ControlMetrics::DropdownOptionHeight;
        Slot.Scroll.ConfigureImmediateOffset(0.0f);
        Slot.Scroll.EnsureRangeVisible(SelTop, SelBottom, ListViewport.Height, true);
    }

    float WheelY = Surface.QueryMouseWheelY();
    if (std::fabs(WheelY) <= 1.0e-4f && ImGui::GetCurrentContext() != nullptr)
    {
        WheelY = ImGui::GetIO().MouseWheel;
    }
    const SmoothScrollUpdateResult ScrollRes = Scrollable
        ? Slot.Scroll.Advance(ListViewport, ContentH, Pointer, WheelY, RowStride * 1.45f)
        : SmoothScrollUpdateResult{};

    ControlContact Contact{};
    const Point2D  Cursor { Pointer.X, Pointer.Y };
    if (ControlAppearance::QueryPointerOver(SheetBounds, Pointer))
    {
        Contact.Hovered = true;
    }

    Surface.FillRectangle(SheetBounds, Colors.Field.Faded(Opacity).ToRgba8(), ControlMetrics::DropdownCornerRadius);
    ControlAppearance::ConstructRoundedOutline(
        Surface,
        SheetBounds,
        Colors.StrokeStrong.Faded(Opacity),
        ControlMetrics::DropdownCornerRadius);

    Surface.PushClip(ListViewport);
    const bool PointerInList = Pointer.Enabled && ListViewport.Contains(Cursor) && !ScrollRes.ConsumedPointer && !Slot.Scroll.QueryDraggingThumb();
    for (uint32_t Index = 0u; Index < OptionCount; ++Index)
    {
        const float RowY = ListViewport.Y + static_cast<float>(Index) * RowStride - ScrollRes.DisplayOffsetY;
        if (RowY + ControlMetrics::DropdownOptionHeight < ListViewport.Y - 2.0f || RowY > ListViewport.Bottom() + 2.0f)
        {
            continue;
        }

        const Rectangle2D Row{
            ListViewport.X,
            RowY,
            RowWidth,
            ControlMetrics::DropdownOptionHeight,
        };
        const bool Hover = PointerInList && Row.Contains(Cursor);
        if (Hover)
        {
            Contact.Hovered = true;
            if (Pointer.Pressed)
            {
                Contact.Pressed = true;
            }
            if (Pointer.Released)
            {
                Contact.Clicked = true;
                ChosenIndex     = Index;
            }
        }
        if (Index == SelectedIndex)
        {
            Surface.FillRectangle(Row, Colors.Raised.Faded(Opacity).ToRgba8(), ControlMetrics::DropdownRowRadius);
        }
        else if (Hover)
        {
            Surface.FillRectangle(Row, Colors.Inset.Faded(Opacity).ToRgba8(), ControlMetrics::DropdownRowRadius);
        }

        ControlAppearance::TextLeading(
            Surface,
            Row,
            OptionTextPadLeftPx,
            (Index == SelectedIndex || Hover ? Colors.Text : Colors.TextDim).Faded(Opacity),
            Options[Index],
            OptionFontSizePx);
    }
    Surface.PopClip();

    if (Scrollable)
    {
        Slot.Scroll.RenderIndicator(Surface, ListViewport, ContentH, Cursor, Opacity);
    }

    return Contact;
}

Rectangle2D QueryDirectiveSheetBounds(
    Point2D                                 AnchorTopLeft,
    float                                   SheetWidth,
    std::span<const DropdownDirectiveEntry> Entries) noexcept
{
    float Height = 2.0f * ControlMetrics::DropdownPaddingY;
    for (std::size_t Index = 0u; Index < Entries.size(); ++Index)
    {
        const DropdownDirectiveEntry& Entry = Entries[Index];
        if (Entry.DividerAbove)
        {
            Height += 9.0f;
        }
        Height += Entry.SectionCaption ? 22.0f : 34.0f;
        if (Index + 1u < Entries.size() && !Entry.SectionCaption)
        {
            Height += ControlMetrics::DropdownRowGap;
        }
    }
    return { AnchorTopLeft.X, AnchorTopLeft.Y, SheetWidth, Height };
}

ControlContact ConstructDirectiveSheet(
    PixelSpace&                             Surface,
    Rectangle2D                             SheetBounds,
    std::span<const DropdownDirectiveEntry> Entries,
    const ControlPointer&                   Pointer,
    int32_t&                                ChosenIndex,
    float                                   Opacity) noexcept
{
    const ControlColorSet& Colors = ControlAppearance::Colors();
    ControlContact         Contact{};
    ChosenIndex = -1;

    constexpr float     SheetCornerRadius = 14.0f; // [px] - HTML .popup-menu border-radius
    constexpr float     RowCornerRadius   = 8.0f;  // [px] - HTML .popup-menu button border-radius
    constexpr float     DisabledAlpha     = 0.40f; // [0-1] - HTML button:disabled opacity
    constexpr ColorRgba HtmlSheetFill     { 26u,  26u,  26u,  255u }; // [-] - #1A1A1A
    constexpr ColorRgba HtmlSheetBorder   { 255u, 255u, 255u, 19u  }; // [-] - #FFFFFF13
    constexpr ColorRgba HtmlHoverFill     { 44u,  44u,  44u,  255u }; // [-] - #2C2C2C
    constexpr ColorRgba HtmlDividerStroke { 255u, 255u, 255u, 12u  }; // [-] - #FFFFFF0B

    Surface.FillRectangle(SheetBounds, HtmlSheetFill.ScaledAlpha(Opacity), SheetCornerRadius);
    Surface.StrokeRectangle(SheetBounds, HtmlSheetBorder.ScaledAlpha(Opacity), SheetCornerRadius, 1.0f);

    Rectangle2D HoveredDisabledRow   {};
    const char* HoveredDisabledTip   = nullptr;

    float CursorY = SheetBounds.Y + ControlMetrics::DropdownPaddingY;
    for (std::size_t Index = 0u; Index < Entries.size(); ++Index)
    {
        const DropdownDirectiveEntry& Entry = Entries[Index];
        if (Entry.DividerAbove)
        {
            Surface.StrokeLine(
                { SheetBounds.X + 8.0f, CursorY + 4.0f },
                { SheetBounds.Right() - 8.0f, CursorY + 4.0f },
                HtmlDividerStroke.ScaledAlpha(Opacity),
                1.0f);
            CursorY += 9.0f;
        }

        if (Entry.SectionCaption)
        {
            const Rectangle2D CaptionRow{
                SheetBounds.X + ControlMetrics::DropdownPaddingX,
                CursorY,
                SheetBounds.Width - 2.0f * ControlMetrics::DropdownPaddingX,
                22.0f,
            };
            ControlAppearance::TextLeading(Surface, CaptionRow, 12.0f, Colors.TextFaint.Faded(Opacity), Entry.Label, 9.5f);
            CursorY += 22.0f;
            continue;
        }

        const Rectangle2D Row{
            SheetBounds.X + ControlMetrics::DropdownPaddingX,
            CursorY,
            SheetBounds.Width - 2.0f * ControlMetrics::DropdownPaddingX,
            34.0f,
        };
        const bool RawHover = ControlAppearance::QueryPointerOver(Row, Pointer);
        const bool Hover    = !Entry.Disabled && RawHover;
        if (RawHover && Entry.Disabled && Entry.DisabledReason != nullptr && Entry.DisabledReason[0] != '\0')
        {
            HoveredDisabledRow = Row;
            HoveredDisabledTip = Entry.DisabledReason;
        }
        if (Hover)
        {
            Contact.Hovered = true;
            if (Pointer.Pressed)
            {
                Contact.Pressed = true;
                Contact.Clicked = true;
                ChosenIndex     = static_cast<int32_t>(Index);
            }
            else if (Pointer.Released)
            {
                Contact.Clicked = true;
                ChosenIndex     = static_cast<int32_t>(Index);
            }
        }

        if (!Entry.Disabled && Entry.Selected)
        {
            Surface.FillRectangle(Row, Colors.Raised.Faded(Opacity).ToRgba8(), RowCornerRadius);
        }
        else if (Hover)
        {
            Surface.FillRectangle(Row, HtmlHoverFill.ScaledAlpha(Opacity), RowCornerRadius);
        }

        const float RowOpacity = Entry.Disabled ? (Opacity * DisabledAlpha) : Opacity;

        float PadX = 12.0f;
        if (Entry.LeadingSymbol > 0u)
        {
            WorkspaceAppearance::ConstructEnvironmentSymbolIcon(
                Surface,
                static_cast<EnvironmentSymbolCategory>(Entry.LeadingSymbol),
                { Row.X + PadX - 1.0f, Row.Y + (Row.Height - 16.0f) * 0.5f },
                16.0f,
                RowOpacity);
            PadX += 22.0f;
        }
        else if (Entry.LeadingTint.Alpha > 0u)
        {
            Surface.FillCircle({ Row.X + PadX + 4.0f, Row.Y + Row.Height * 0.5f }, 3.8f, Entry.LeadingTint.ScaledAlpha(RowOpacity));
            PadX += 16.0f;
        }
        else if (Entry.LeadingIcon != ControlIcon::Count)
        {
            const ColorQuad IconTone = Entry.Disabled
                                           ? Colors.TextDim
                                           : (Entry.Destructive ? Colors.Danger : (Hover || Entry.Selected ? Colors.Text : Colors.TextDim));
            ControlAppearance::ConstructIcon(
                Surface,
                Row.X + PadX,
                Row.Y + (Row.Height - 14.0f) * 0.5f,
                14.0f,
                IconTone.Faded(RowOpacity),
                Entry.LeadingIcon,
                1.7f);
            PadX += 20.0f;
        }

        const ColorQuad DangerQuad         = ColorQuad{ 1.0f, 138.0f / 255.0f, 128.0f / 255.0f, 1.0f };
        const ColorQuad DisabledDangerQuad = ColorQuad{ 107.0f / 255.0f, 107.0f / 255.0f, 107.0f / 255.0f, 1.0f };
        const ColorQuad LabelTone          = Entry.Destructive
                                                 ? (Entry.Disabled ? DisabledDangerQuad : DangerQuad)
                                                 : ((Entry.Selected || Hover) ? Colors.Text : Colors.Text.Faded(0.92f));
        ControlAppearance::TextLeading(Surface, Row, PadX, LabelTone.Faded(RowOpacity), Entry.Label, 12.0f);

        if (Entry.ShortcutHint != nullptr && Entry.ShortcutHint[0] != '\0')
        {
            const Rectangle2D HintBounds{ Row.X, Row.Y, Row.Width - 12.0f, Row.Height };
            Surface.TextRightAligned(HintBounds, 10.0f, Colors.TextFaint.Faded(RowOpacity).ToRgba8(), Entry.ShortcutHint);
        }
        else if (Entry.Selected && !Entry.Disabled)
        {
            const Point2D CheckCenter{ Row.Right() - 18.0f, Row.Y + Row.Height * 0.5f };
            const Point2D CheckMarks[3] = {
                { CheckCenter.X - 4.5f, CheckCenter.Y + 0.2f },
                { CheckCenter.X - 1.2f, CheckCenter.Y + 3.5f },
                { CheckCenter.X + 5.0f, CheckCenter.Y - 3.2f },
            };
            Surface.StrokePolyline(CheckMarks, Colors.Success.Faded(Opacity).ToRgba8(), 1.8f, false);
        }

        CursorY += 34.0f + ControlMetrics::DropdownRowGap;
    }

    if (HoveredDisabledTip != nullptr)
    {
        const float       TipTextW = Surface.MeasureTextWidth(10.0f, HoveredDisabledTip);
        const float       TipW     = std::clamp(TipTextW + 18.0f, 90.0f, 280.0f);
        const Rectangle2D TipRect{
            SheetBounds.Right() + 6.0f,
            HoveredDisabledRow.Y + 4.0f,
            TipW,
            26.0f,
        };
        Surface.FillRectangle(TipRect, ColorRgba{ 18u, 18u, 18u, 248u }, 6.0f);
        Surface.StrokeRectangle(TipRect, ColorRgba{ 66u, 66u, 66u, 255u }, 6.0f, 1.0f);
        ControlAppearance::TextLeading(Surface, TipRect, 9.0f, ColorQuad{ 196.0f / 255.0f, 196.0f / 255.0f, 196.0f / 255.0f, 1.0f }, HoveredDisabledTip, 10.0f);
    }

    return Contact;
}

} // namespace DropdownSelector

} // namespace Frontier
