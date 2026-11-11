/*============================================================================================================================================
                                                       DROPDOWNSELECTOR.CPP
============================================================================================================================================*/
// 🧩 Reusable rounded dropdown button and overlay option sheet

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/DropdownSelector.h"

namespace Frontier
{

namespace DropdownSelector
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 DROPDOWN RENDERING
//------------------------------------------------------------------------------------------------------------------------

ControlContact DrawButton(
    PixelSpace&           Surface,
    Rectangle2D           Bounds,
    const char*           CurrentLabel,
    bool                  Open,
    const ControlPointer& Pointer,
    float                 Opacity) noexcept
{
    const ControlColorSet& Colors = ControlStyle::Colors();
    ControlContact         Contact{};
    Contact.Hovered = ControlStyle::QueryPointerOver(Bounds, Pointer);
    Contact.Pressed = Contact.Hovered && Pointer.Pressed;
    Contact.Clicked = Contact.Hovered && Pointer.Released;

    const float       Height = Bounds.Height;
    const float       Radius = Height * 0.5f;
    const float       CaretW = 44.0f;
    const Rectangle2D CaretCell{ Bounds.Right() - CaretW, Bounds.Y, CaretW, Height };

    Surface.FillRectangle(Bounds, Colors.Field.Faded(Opacity).ToRgba8(), Radius);
    Surface.FillRectangle(
        { CaretCell.X - Radius, Bounds.Y, CaretW + Radius, Height },
        (Contact.Hovered ? Colors.Raised : Colors.Inset).Faded(Opacity).ToRgba8(),
        Radius);
    Surface.FillRectangle({ CaretCell.X - Radius, Bounds.Y, Radius, Height }, Colors.Field.Faded(Opacity).ToRgba8(), 0.0f);
    Surface.FillRectangle({ CaretCell.X, Bounds.Y, 1.0f, Height }, Colors.Stroke.Faded(Opacity).ToRgba8(), 0.0f);
    ControlStyle::DrawRoundedOutline(Surface, Bounds, Colors.Stroke.Faded(Opacity), Radius);
    ControlStyle::TextLeading(Surface, Bounds, 16.0f, Colors.Text.Faded(Opacity), CurrentLabel, 13.5f);
    ControlStyle::DrawIconCentered(
        Surface,
        CaretCell,
        16.0f,
        Colors.TextDim.Faded(Opacity),
        Open ? ControlIcon::ChevronUp : ControlIcon::ChevronDown);
    return Contact;
}

Rectangle2D QuerySheetBounds(Rectangle2D ButtonBounds, uint32_t OptionCount) noexcept
{
    const float TotalHeight = 2.0f * ControlMetrics::DropdownPaddingY +
                              static_cast<float>(OptionCount) * ControlMetrics::DropdownOptionHeight +
                              (OptionCount > 0u ? static_cast<float>(OptionCount - 1u) * ControlMetrics::DropdownRowGap : 0.0f);
    return { ButtonBounds.X, ButtonBounds.Bottom() + ControlMetrics::DropdownPaddingY, ButtonBounds.Width, TotalHeight };
}

ControlContact DrawOptionSheet(
    PixelSpace&           Surface,
    Rectangle2D           ButtonBounds,
    const char* const*    Options,
    uint32_t              OptionCount,
    uint32_t              SelectedIndex,
    const ControlPointer& Pointer,
    uint32_t&             ChosenIndex,
    float                 Opacity) noexcept
{
    const ControlColorSet& Colors      = ControlStyle::Colors();
    const Rectangle2D      SheetBounds = QuerySheetBounds(ButtonBounds, OptionCount);
    ControlContact         Contact{};

    Surface.FillRectangle(SheetBounds, Colors.Field.Faded(Opacity).ToRgba8(), ControlMetrics::DropdownCornerRadius);
    ControlStyle::DrawRoundedOutline(Surface, SheetBounds, Colors.StrokeStrong.Faded(Opacity), ControlMetrics::DropdownCornerRadius);

    float CursorY = SheetBounds.Y + ControlMetrics::DropdownPaddingY;
    for (uint32_t Index = 0u; Index < OptionCount; ++Index)
    {
        const Rectangle2D Row{
            SheetBounds.X + ControlMetrics::DropdownPaddingX,
            CursorY,
            SheetBounds.Width - 2.0f * ControlMetrics::DropdownPaddingX,
            ControlMetrics::DropdownOptionHeight,
        };
        const bool Hover = ControlStyle::QueryPointerOver(Row, Pointer);
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

        ControlStyle::TextLeading(
            Surface,
            Row,
            14.0f,
            (Index == SelectedIndex || Hover ? Colors.Text : Colors.TextDim).Faded(Opacity),
            Options[Index],
            13.5f);

        CursorY += ControlMetrics::DropdownOptionHeight + ControlMetrics::DropdownRowGap;
    }

    return Contact;
}

} // namespace DropdownSelector

} // namespace Frontier
