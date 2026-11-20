/*============================================================================================================================================
                                                               SCALARSLIDER.CPP
============================================================================================================================================*/
// 🧩 Neutral-track scalar slider and two-cell readout capsule implementation.

#include "Engine/Components/Controls/ScalarSlider.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace ScalarSlider
{

ControlContact Construct(PixelSpace &Surface, Rectangle2D Bounds, float Minimum, float Maximum, float CurrentPosition,
                    bool DraggingActive, const ControlPointer &Pointer, float &UpdatedPosition, float Opacity,
                    float RailHeight, float KnobDiameter, const OptionalTooltip &Tooltip) noexcept
{
    ControlContact        Contact{};
    const ControlColorSet &Colors = ControlAppearance::Colors();

    const float TrackHeight   = std::clamp(RailHeight, 4.0f, 48.0f);
    const float ThumbDiameter = std::min(std::clamp(KnobDiameter, 4.0f, 48.0f), std::max(1.0f, Bounds.Width));
    const float CenterY       = Bounds.Y + Bounds.Height * 0.5f;
    const Rectangle2D Track{Bounds.X, CenterY - TrackHeight * 0.5f, Bounds.Width, TrackHeight};
    const Rectangle2D HitBand{Bounds.X, CenterY - ThumbDiameter * 0.5f - 2.0f, Bounds.Width, ThumbDiameter + 4.0f};

    Contact.Hovered  = ControlAppearance::QueryPointerOver(HitBand, Pointer);
    Contact.Pressed  = Contact.Hovered && Pointer.Pressed;
    Contact.Dragging = DraggingActive || Contact.Pressed;

    const float Span     = std::max(Maximum - Minimum, 1.0e-6f);
    float       Fraction = std::clamp((CurrentPosition - Minimum) / Span, 0.0f, 1.0f);
    if (Contact.Dragging && Pointer.Down && Pointer.Enabled)
    {
        const float Usable = std::max(Bounds.Width - ThumbDiameter, 1.0f);
        Fraction           = std::clamp((Pointer.X - Bounds.X - ThumbDiameter * 0.5f) / Usable, 0.0f, 1.0f);
        UpdatedPosition    = Minimum + Fraction * Span;
    }

    const float Radius = TrackHeight * 0.5f;
    Surface.FillRectangle(Track, Colors.SliderTrack.Faded(Opacity).ToRgba8(), Radius);

    const float ThumbCenterX = Bounds.X + ThumbDiameter * 0.5f + Fraction * (Bounds.Width - ThumbDiameter);
    const float FillWidth    = std::max(ThumbCenterX - Track.X, 0.0f);
    if (FillWidth > 1.0f)
    {
        Surface.FillRectangle({Track.X, Track.Y, std::min(FillWidth, Track.Width), Track.Height},
                              Colors.SliderFill.Faded(Opacity).ToRgba8(), Radius);
    }

    const float ThumbRadius = ThumbDiameter * 0.5f * (Contact.Dragging && Pointer.Down ? 1.12f : 1.0f);
    ControlAppearance::ConstructCircleFill(Surface, ThumbCenterX, CenterY, ThumbRadius, Colors.SliderThumb.Faded(Opacity));
    if (Contact.Hovered && Tooltip.Enabled)
    {
        ControlAppearance::RecordHoveredTooltip(Track, Tooltip);
    }
    return Contact;
}

void ConstructReadoutCapsule(PixelSpace &Surface, float X, float Y, const char *NumberText, const char *UnitText,
                        float Opacity, float TotalWidth, float UnitCellWidth) noexcept
{
    const ControlColorSet &Colors = ControlAppearance::Colors();
    const float            Height  = ControlMetrics::ControlHeight;
    UnitCellWidth                  = std::clamp(UnitCellWidth, 12.0f, std::max(TotalWidth - 24.0f, 12.0f));
    const float Radius             = Height * 0.5f;
    const float EffectiveUnitW     = (UnitText && UnitText[0] != '\0') ? UnitCellWidth : 0.0f;
    const float SeamX              = X + TotalWidth - EffectiveUnitW;

    const Rectangle2D Whole{X, Y, TotalWidth, Height};
    const Rectangle2D NumberCell{X, Y, std::max(TotalWidth - EffectiveUnitW, 1.0f), Height};
    const Rectangle2D UnitCell{SeamX, Y, EffectiveUnitW, Height};

    Surface.FillRectangle(Whole, Colors.Inset.Faded(Opacity).ToRgba8(), Radius);
    Surface.FillRectangle(NumberCell, Colors.Field.Faded(Opacity).ToRgba8(), Radius);
    if (EffectiveUnitW > 0.0f && NumberCell.Width > Radius)
    {
        Surface.FillRectangle({X + Radius, Y, NumberCell.Width - Radius, Height},
                              Colors.Field.Faded(Opacity).ToRgba8(), 0.0f);
        Surface.FillRectangle({SeamX, Y, 1.0f, Height}, Colors.Stroke.Faded(Opacity).ToRgba8(), 0.0f);
    }

    ControlAppearance::ConstructRoundedOutline(Surface, Whole, Colors.Stroke.Faded(Opacity), Radius);
    ControlAppearance::TextCentered(Surface, NumberCell, Colors.Text.Faded(Opacity), NumberText, 13.5f);
    if (EffectiveUnitW > 0.0f)
        ControlAppearance::TextCentered(Surface, UnitCell, Colors.TextFaint.Faded(Opacity), UnitText, 11.5f);
}

ControlContact ConstructWithReadout(PixelSpace &Surface, Rectangle2D RowBounds, const char *NumberText, const char *UnitText,
                               float Minimum, float Maximum, float CurrentPosition, bool DraggingActive,
                               const ControlPointer &Pointer, float &UpdatedPosition, float Opacity,
                               const OptionalTooltip &Tooltip) noexcept
{
    ConstructReadoutCapsule(Surface, RowBounds.X, RowBounds.Y, NumberText, UnitText, Opacity);
    const float SliderLeft = RowBounds.X + ControlMetrics::ReadoutCapsuleWidth + ControlMetrics::RowGap;
    const Rectangle2D SliderBounds{SliderLeft, RowBounds.Y, std::max(1.0f, RowBounds.Right() - SliderLeft),
                                   RowBounds.Height};
    const ControlContact Contact = Construct(
        Surface, SliderBounds, Minimum, Maximum, CurrentPosition, DraggingActive, Pointer, UpdatedPosition, Opacity);
    if (Tooltip.Enabled && ControlAppearance::QueryPointerOver(RowBounds, Pointer))
    {
        ControlAppearance::RecordHoveredTooltip(RowBounds, Tooltip);
    }
    return Contact;
}

} // namespace ScalarSlider

} // namespace Frontier
