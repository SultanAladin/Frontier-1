/*============================================================================================================================================
                                                             DROPDOWNSELECTOR.CPP
============================================================================================================================================*/
// 🧩 Reusable Studio Controls rounded dropdown selector button, option sheet, and directive sheet

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/DropdownSelector.h"

#include <algorithm>

namespace Frontier
{

namespace DropdownSelector
{

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

Rectangle2D QuerySheetBounds(Rectangle2D ButtonBounds, uint32_t OptionCount) noexcept
{
    const float TotalHeight = 2.0f * ControlMetrics::DropdownPaddingY +
                              static_cast<float>(OptionCount) * ControlMetrics::DropdownOptionHeight +
                              (OptionCount > 0u ? static_cast<float>(OptionCount - 1u) * ControlMetrics::DropdownRowGap : 0.0f);
    return { ButtonBounds.X, ButtonBounds.Bottom() + ControlMetrics::DropdownPaddingY, ButtonBounds.Width, TotalHeight };
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
    const Rectangle2D      SheetBounds = QuerySheetBounds(ButtonBounds, OptionCount);
    ControlContact         Contact{};

    Surface.FillRectangle(SheetBounds, Colors.Field.Faded(Opacity).ToRgba8(), ControlMetrics::DropdownCornerRadius);
    ControlAppearance::ConstructRoundedOutline(
        Surface,
        SheetBounds,
        Colors.StrokeStrong.Faded(Opacity),
        ControlMetrics::DropdownCornerRadius);

    float CursorY = SheetBounds.Y + ControlMetrics::DropdownPaddingY;
    for (uint32_t Index = 0u; Index < OptionCount; ++Index)
    {
        const Rectangle2D Row{
            SheetBounds.X + ControlMetrics::DropdownPaddingX,
            CursorY,
            SheetBounds.Width - 2.0f * ControlMetrics::DropdownPaddingX,
            ControlMetrics::DropdownOptionHeight,
        };
        const bool Hover = ControlAppearance::QueryPointerOver(Row, Pointer);
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
            14.0f,
            (Index == SelectedIndex || Hover ? Colors.Text : Colors.TextDim).Faded(Opacity),
            Options[Index],
            13.0f);

        CursorY += ControlMetrics::DropdownOptionHeight + ControlMetrics::DropdownRowGap;
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

    Surface.FillRectangle(SheetBounds, Colors.Field.Faded(Opacity).ToRgba8(), ControlMetrics::DropdownCornerRadius);
    ControlAppearance::ConstructRoundedOutline(
        Surface,
        SheetBounds,
        Colors.StrokeStrong.Faded(Opacity),
        ControlMetrics::DropdownCornerRadius);

    float CursorY = SheetBounds.Y + ControlMetrics::DropdownPaddingY;
    for (std::size_t Index = 0u; Index < Entries.size(); ++Index)
    {
        const DropdownDirectiveEntry& Entry = Entries[Index];
        if (Entry.DividerAbove)
        {
            ControlAppearance::ConstructHairline(
                Surface,
                SheetBounds.X + 8.0f,
                CursorY + 4.0f,
                SheetBounds.Width - 16.0f,
                Colors.Divider.Faded(Opacity));
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
        const bool Hover = ControlAppearance::QueryPointerOver(Row, Pointer);
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

        if (Entry.Selected)
        {
            Surface.FillRectangle(Row, Colors.Raised.Faded(Opacity).ToRgba8(), ControlMetrics::DropdownRowRadius);
        }
        else if (Hover)
        {
            Surface.FillRectangle(Row, Colors.Inset.Faded(Opacity).ToRgba8(), ControlMetrics::DropdownRowRadius);
        }

        float PadX = 12.0f;
        if (Entry.LeadingTint.Alpha > 0u)
        {
            Surface.FillCircle({ Row.X + PadX + 4.0f, Row.Y + Row.Height * 0.5f }, 3.8f, Entry.LeadingTint.ScaledAlpha(Opacity));
            PadX += 16.0f;
        }
        else if (Entry.LeadingIcon != ControlIcon::Count)
        {
            ControlAppearance::ConstructIcon(
                Surface,
                Row.X + PadX,
                Row.Y + (Row.Height - 14.0f) * 0.5f,
                14.0f,
                (Entry.Destructive ? Colors.Danger : (Hover || Entry.Selected ? Colors.Text : Colors.TextDim)).Faded(Opacity),
                Entry.LeadingIcon,
                1.7f);
            PadX += 20.0f;
        }

        const ColorQuad LabelTone = Entry.Destructive
                                        ? Colors.Danger
                                        : ((Entry.Selected || Hover) ? Colors.Text : Colors.Text.Faded(0.76f));
        ControlAppearance::TextLeading(Surface, Row, PadX, LabelTone.Faded(Opacity), Entry.Label, 12.5f);

        if (Entry.ShortcutHint != nullptr && Entry.ShortcutHint[0] != '\0')
        {
            const Rectangle2D HintBounds{ Row.X, Row.Y, Row.Width - 12.0f, Row.Height };
            Surface.TextRightAligned(HintBounds, 10.5f, Colors.TextFaint.Faded(Opacity).ToRgba8(), Entry.ShortcutHint);
        }
        else if (Entry.Selected)
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

    return Contact;
}

} // namespace DropdownSelector

} // namespace Frontier
