#pragma once

#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"

namespace Slate
{

struct TextStyle
{
    float Size = 14.0f;
    float Tracking = 0.0f;
    float LineHeight = 0.0f;
    FontWeight Weight = FontWeight::Regular;
    FontSlant Slant = FontSlant::Upright;
};

/// One available family face shown in a typography strip.
struct TypefaceOption
{
    const char* Family = nullptr;
    FontWeight Weight = FontWeight::Regular;
    FontSlant Slant = FontSlant::Upright;
    ImFont* Face = nullptr;
};

/// One three-line typography control: face strip, size control, and sample.
struct TypographySection
{
    const char* Caption = nullptr;
    TypefaceOption Typeface = {};
    float Size = 14.0f;
};

struct TypefaceStrip
{
    TypefaceOption Options[16] = {};
    std::uint32_t Count = 0u;
    std::uint32_t Selected = 0u;
};

struct TextScaleControl
{
    float Minimum = 8.0f;
    float Maximum = 48.0f;
    float Current = 14.0f;
};

struct TypographySample
{
    const char* Text = "The quick brown fox jumps over the lazy dog";
    TextStyle Style = {};
};

struct TypographyMetrics
{
    float Width = 0.0f;
    float Height = 0.0f;
    float Baseline = 0.0f;
};

/// Shared measurement and drawing helpers for text-dependent controls.
class TextComponent
{
public:
    static TypographyMetrics Measure(const RecordingSurface& Surface,
                               const char* Text,
                               const TextStyle& Style);

    static PlaneExtent Fit(const PlaneExtent& Origin,
                           const TypographyMetrics& Metrics,
                           float PaddingX,
                           float PaddingY);

    static void Draw(RecordingSurface& Surface,
                     const PlaneExtent& Bounds,
                     ThemeToken Colour,
                     const char* Text,
                     const TextStyle& Style);
};

} // namespace Slate
