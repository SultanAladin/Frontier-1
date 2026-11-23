#include "SlateUI/Interface/TextComponent/Api/TextComponent.h"

namespace Slate
{

TypographyMetrics TextComponent::Measure(const RecordingSurface& Surface,
                                   const char* Text,
                                   const TextStyle& Style)
{
    const float Height = (Style.LineHeight > 0.0f) ? Style.LineHeight : Style.Size * 1.25f;
    return { Surface.MeasureRun(Text != nullptr ? Text : "", Style.Size, Style.Tracking), Height, Style.Size };
}

PlaneExtent TextComponent::Fit(const PlaneExtent& Origin,
                               const TypographyMetrics& Metrics,
                               float PaddingX,
                               float PaddingY)
{
    const float Width = Metrics.Width + PaddingX * 2.0f;
    const float Height = Metrics.Height + PaddingY * 2.0f;
    return PlaneExtent{Origin.MinimumX, Origin.MinimumY,
                       Origin.MinimumX + Width, Origin.MinimumY + Height};
}

void TextComponent::Draw(RecordingSurface& Surface,
                         const PlaneExtent& Bounds,
                         ThemeToken Colour,
                         const char* Text,
                         const TextStyle& Style)
{
    const TypographyMetrics Metrics = Measure(Surface, Text, Style);
    Surface.TextRun(Bounds.MinimumX + (Bounds.Width() - Metrics.Width) * 0.5f,
                    Bounds.MinimumY + (Bounds.Height() - Metrics.Height) * 0.5f,
                    Colour, Text != nullptr ? Text : "", Style.Size, Style.Tracking, true);
}

} // namespace Slate
