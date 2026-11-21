/*============================================================================================================================================
                                                            EXCLUSIVESELECTOR.CPP
============================================================================================================================================*/
// 🧩 Reusable rounded toggle switch implementations.

#include "Engine/Components/Controls/ExclusiveSelector.h"

namespace Frontier
{

namespace ExclusiveSelector
{

ControlContact Construct(PixelSpace &Surface, float X, float Y, bool Active, const ControlPointer &Pointer,
                    float Opacity, const OptionalTooltip &Tooltip) noexcept
{
    const ControlColorSet &Colors = ControlAppearance::Colors();
    const Rectangle2D      Bounds{X, Y, ControlMetrics::SwitchWidth, ControlMetrics::SwitchHeight};

    ControlContact Contact{};
    Contact.Hovered = ControlAppearance::QueryPointerOver(Bounds, Pointer);
    Contact.Pressed = Contact.Hovered && Pointer.Pressed;
    Contact.Clicked = Contact.Hovered && Pointer.Released;

    const float Radius = ControlMetrics::SwitchHeight * 0.5f;
    Surface.FillRectangle(Bounds, (Active ? Colors.Accent : Colors.Raised).Faded(Opacity).ToRgba8(), Radius);
    if (!Active)
        ControlAppearance::ConstructRoundedOutline(Surface, Bounds, Colors.Stroke.Faded(Opacity), Radius);

    const float KnobLeft = X + 3.0f + (Active ? 20.0f : 0.0f);
    ControlAppearance::ConstructCircleFill(Surface, KnobLeft + 10.0f, Y + 13.0f, 10.0f,
                                 (Active ? Colors.AccentInk : Colors.SwitchKnobOff).Faded(Opacity));
    if (Contact.Hovered && Tooltip.Enabled)
    {
        ControlAppearance::RecordHoveredTooltip(Bounds, Tooltip);
    }
    return Contact;
}

ControlContact ConstructCompact(PixelSpace &Surface, Rectangle2D Bounds, bool Active, const ControlPointer &Pointer,
                           float Opacity, const OptionalTooltip &Tooltip) noexcept
{
    const ControlColorSet &Colors = ControlAppearance::Colors();
    ControlContact         Contact{};
    Contact.Hovered = ControlAppearance::QueryPointerOver(Bounds, Pointer);
    Contact.Pressed = Contact.Hovered && Pointer.Pressed;
    Contact.Clicked = Contact.Hovered && Pointer.Released;

    const ColorQuad InactiveFill = Colors.LightSurface ? ColorQuad{0.0f, 0.0f, 0.0f, 0.10f}
                                                        : ColorQuad{1.0f, 1.0f, 1.0f, 0.10f};
    Surface.FillRectangle(Bounds, (Active ? Colors.Accent : InactiveFill).Faded(Opacity).ToRgba8(),
                          Bounds.Height * 0.5f);
    const float KnobLeft = Bounds.X + (Active ? Bounds.Width - 22.0f : 2.0f);
    ControlAppearance::ConstructCircleFill(Surface, KnobLeft + 10.0f, Bounds.Y + Bounds.Height * 0.5f, 10.0f,
                                 ColorQuad{1.0f, 1.0f, 1.0f, Opacity});
    if (Contact.Hovered && Tooltip.Enabled)
    {
        ControlAppearance::RecordHoveredTooltip(Bounds, Tooltip);
    }
    return Contact;
}

} // namespace ExclusiveSelector

} // namespace Frontier
