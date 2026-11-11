/*============================================================================================================================================
                                                        BOOLEANSWITCH.CPP
============================================================================================================================================*/
// 🧩 Reusable rounded toggle switch implementations.

#include "Engine/Components/Controls/BooleanSwitch.h"

namespace Frontier
{

namespace BooleanSwitch
{

ControlContact Draw(PixelSpace &Surface, float X, float Y, bool Active, const ControlPointer &Pointer,
                    float Opacity) noexcept
{
    const ControlColorSet &Colors = ControlStyle::Colors();
    const Rectangle2D      Bounds{X, Y, ControlMetrics::SwitchWidth, ControlMetrics::SwitchHeight};

    ControlContact Contact{};
    Contact.Hovered = ControlStyle::QueryPointerOver(Bounds, Pointer);
    Contact.Pressed = Contact.Hovered && Pointer.Pressed;
    Contact.Clicked = Contact.Hovered && Pointer.Released;

    const float Radius = ControlMetrics::SwitchHeight * 0.5f;
    Surface.FillRectangle(Bounds, (Active ? Colors.Accent : Colors.Raised).Faded(Opacity).ToRgba8(), Radius);
    if (!Active)
        ControlStyle::DrawRoundedOutline(Surface, Bounds, Colors.Stroke.Faded(Opacity), Radius);

    const float KnobLeft = X + 3.0f + (Active ? 20.0f : 0.0f);
    ControlStyle::DrawCircleFill(Surface, KnobLeft + 10.0f, Y + 13.0f, 10.0f,
                                 (Active ? Colors.AccentInk : Colors.SwitchKnobOff).Faded(Opacity));
    return Contact;
}

ControlContact DrawCompact(PixelSpace &Surface, Rectangle2D Bounds, bool Active, const ControlPointer &Pointer,
                           float Opacity) noexcept
{
    const ControlColorSet &Colors = ControlStyle::Colors();
    ControlContact         Contact{};
    Contact.Hovered = ControlStyle::QueryPointerOver(Bounds, Pointer);
    Contact.Pressed = Contact.Hovered && Pointer.Pressed;
    Contact.Clicked = Contact.Hovered && Pointer.Released;

    const ColorQuad InactiveFill = Colors.LightSurface ? ColorQuad{0.0f, 0.0f, 0.0f, 0.10f}
                                                        : ColorQuad{1.0f, 1.0f, 1.0f, 0.10f};
    Surface.FillRectangle(Bounds, (Active ? Colors.Accent : InactiveFill).Faded(Opacity).ToRgba8(),
                          Bounds.Height * 0.5f);
    const float KnobLeft = Bounds.X + (Active ? Bounds.Width - 22.0f : 2.0f);
    ControlStyle::DrawCircleFill(Surface, KnobLeft + 10.0f, Bounds.Y + Bounds.Height * 0.5f, 10.0f,
                                 ColorQuad{1.0f, 1.0f, 1.0f, Opacity});
    return Contact;
}

} // namespace BooleanSwitch

} // namespace Frontier
