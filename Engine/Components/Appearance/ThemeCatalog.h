/*============================================================================================================================================
                                                          THEMECATALOG.H
============================================================================================================================================*/
// 🧩 Built-in interface themes, accent swatches, semantic alert swatches, and typography scale slots

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/PixelSpace.h"

#include <array>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ThemePreset : uint8_t
{
    Oled    = 0, // [-] - Pure black OLED dark theme
    Dark    = 1, // [-] - Neutral charcoal dark theme
    Dim     = 2, // [-] - Slate blue-tinted dim theme
    Light   = 3, // [-] - Daylight light theme
    Sepia   = 4, // [-] - Warm paper sepia theme
    Dracula = 5, // [-] - Dracula purple-slate theme
    Nord    = 6, // [-] - Arctic Nord slate theme
    GitHub  = 7, // [-] - Deep navy contrast theme
    Count   = 8, // [count] - Total built-in theme presets
};

enum class AccentPreset : uint8_t
{
    White   = 0,  // [-] - Pure white accent
    Orange  = 1,  // [-] - Warm orange accent
    Amber   = 2,  // [-] - Golden amber accent
    Lime    = 3,  // [-] - Vivid lime accent
    Emerald = 4,  // [-] - Emerald green accent
    Cyan    = 5,  // [-] - Bright cyan accent
    Blue    = 6,  // [-] - Studio blue accent (default)
    Violet  = 7,  // [-] - Royal violet accent
    Fuchsia = 8,  // [-] - Electric fuchsia accent
    Rose    = 9,  // [-] - Crimson rose accent
    Count   = 10, // [count] - Total built-in accent presets
};

enum class FontWeightClass : uint16_t
{
    Thin       = 100, // [-] - CSS 100 Thin weight
    ExtraLight = 200, // [-] - CSS 200 ExtraLight weight
    Light      = 300, // [-] - CSS 300 Light weight
    Regular    = 400, // [-] - CSS 400 Regular weight
    Medium     = 500, // [-] - CSS 500 Medium weight
    SemiBold   = 600, // [-] - CSS 600 SemiBold weight
    Bold       = 700, // [-] - CSS 700 Bold weight
    ExtraBold  = 800, // [-] - CSS 800 ExtraBold weight
    Black      = 900, // [-] - CSS 900 Black weight
};

enum class TypeScaleSlot : uint8_t
{
    Title     = 0, // [-] - Display title typography slot
    Header    = 1, // [-] - Section header typography slot
    Subheader = 2, // [-] - Subheader typography slot
    Body      = 3, // [-] - Body copy typography slot
    Label     = 4, // [-] - Form label typography slot
    Caption   = 5, // [-] - Small caption typography slot
    Count     = 6, // [count] - Total desktop type-scale slots
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ColorQuad
{
    float Red   = 0.0f; // [0-1] - Normalized red channel
    float Green = 0.0f; // [0-1] - Normalized green channel
    float Blue  = 0.0f; // [0-1] - Normalized blue channel
    float Alpha = 1.0f; // [0-1] - Normalized opacity channel

    [[nodiscard]] constexpr ColorRgba        ToRgba8() const noexcept;
    [[nodiscard]] constexpr ColorQuad        Faded(float Opacity) const noexcept;
    [[nodiscard]] static constexpr ColorQuad FromHex(uint32_t Rgb, float AlphaChannel = 1.0f) noexcept;
    [[nodiscard]] static constexpr ColorQuad Blend(ColorQuad From, ColorQuad To, float Factor) noexcept;
    [[nodiscard]] constexpr float            Luminance() const noexcept;
    [[nodiscard]] constexpr bool             operator==(const ColorQuad&) const noexcept = default;
};

struct ThemeSurfaceSet
{
    const char* Name              = "OLED"; // [-] - Display label of the theme preset
    ColorQuad   MainBackground    = {};     // [-] - Root workspace canvas fill
    ColorQuad   PanelBackground   = {};     // [-] - Control Centre sheet fill
    ColorQuad   SectionBackground = {};     // [-] - Grouped section container fill
    ColorQuad   SubtleBackground  = {};     // [-] - Subtle raised surface fill
    ColorQuad   TextMain          = {};     // [-] - Primary foreground ink
    ColorQuad   TextMuted         = {};     // [-] - Secondary muted ink
    ColorQuad   PanelBorder       = {};     // [-] - Sheet perimeter hairline stroke
    ColorQuad   DividerColor      = {};     // [-] - Horizontal row separator ink
    ColorQuad   InputBackground   = {};     // [-] - Input field and dropdown fill
    ColorQuad   ActiveBackground  = {};     // [-] - Active or hovered control fill
};

struct AppearanceSettings
{
    static constexpr uint32_t TypeSlotCount = static_cast<uint32_t>(TypeScaleSlot::Count); // [count] - Number of desktop type slots

    ThemePreset  Theme          = ThemePreset::Oled;  // [-]  - Selected workspace color scheme
    AccentPreset Accent         = AccentPreset::Blue; // [-]  - Selected accent swatch
    uint32_t     TypefaceIndex  = 0u;                 // [idx] - Selected typeface group index
    float        InterfaceScale = 100.0f;             // [%]  - Logical interface density percentage [75..150]
    float        CornerRadius   = 16.0f;              // [px] - Section corner radius [0..32]
    uint32_t     WarningSwatch  = 0u;                 // [idx] - Selected warning semantic swatch [0..3]
    uint32_t     SuccessSwatch  = 1u;                 // [idx] - Selected success semantic swatch [0..3]
    uint32_t     NoticeSwatch   = 0u;                 // [idx] - Selected notice semantic swatch [0..3]
    uint32_t     CautionSwatch  = 0u;                 // [idx] - Selected caution semantic swatch [0..3]

    std::array<float, TypeSlotCount>           SlotSize   = { 32.0f, 24.0f, 18.0f, 14.0f, 12.0f, 10.0f }; // [px] - Desktop slot sizes
    std::array<FontWeightClass, TypeSlotCount> SlotWeight = {
        FontWeightClass::Bold,
        FontWeightClass::SemiBold,
        FontWeightClass::Medium,
        FontWeightClass::Regular,
        FontWeightClass::Medium,
        FontWeightClass::Regular,
    }; // [-] - Desktop slot weights

    [[nodiscard]] constexpr bool operator==(const AppearanceSettings&) const noexcept = default;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ThemeCatalog
{
public:
    ThemeCatalog() noexcept;

    void ConfigureTheme(ThemePreset DesiredTheme) noexcept;
    void ConfigureAccent(AccentPreset DesiredAccent) noexcept;
    void ConfigureCornerRadius(float RadiusPixels) noexcept;

    [[nodiscard]] ThemePreset            QueryActiveTheme() const noexcept;
    [[nodiscard]] AccentPreset           QueryActiveAccent() const noexcept;
    [[nodiscard]] float                  QueryCornerRadius() const noexcept;
    [[nodiscard]] const ThemeSurfaceSet& QuerySurfaceSet() const noexcept;
    [[nodiscard]] const ThemeSurfaceSet& QuerySurfaceSet(ThemePreset Preset) const noexcept;
    [[nodiscard]] ColorQuad              QueryDarkestSurface() const noexcept;
    [[nodiscard]] ColorQuad              QueryAccentColor() const noexcept;

    [[nodiscard]] static ColorQuad   QueryAccentColor(AccentPreset Accent) noexcept;
    [[nodiscard]] static ColorQuad   QuerySemanticColor(uint32_t RowIndex, uint32_t SwatchIndex) noexcept;
    [[nodiscard]] static const char* QueryThemeLabel(ThemePreset Preset) noexcept;
    [[nodiscard]] static const char* QueryTypeSlotLabel(uint32_t SlotOrdinal) noexcept;
    [[nodiscard]] static const char* QueryTypeSlotSample(uint32_t SlotOrdinal) noexcept;

private:
    ThemePreset                                                               ActiveTheme        = ThemePreset::Oled;  // [-]  - Active theme preset
    AccentPreset                                                              ActiveAccent       = AccentPreset::Blue; // [-]  - Active accent preset
    float                                                                     CornerRadiusPixels = 16.0f;              // [px] - Active corner radius
    std::array<ThemeSurfaceSet, static_cast<std::size_t>(ThemePreset::Count)> SurfaceSets        {};                   // [-]  - Built-in surface definitions
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

constexpr ColorRgba ColorQuad::ToRgba8() const noexcept
{
    auto Quantize = [](float Channel) constexpr noexcept -> uint8_t
    {
        const float Clamped = Channel < 0.0f ? 0.0f : (Channel > 1.0f ? 1.0f : Channel);
        return static_cast<uint8_t>(Clamped * 255.0f + 0.5f);
    };
    return { Quantize(Red), Quantize(Green), Quantize(Blue), Quantize(Alpha) };
}

constexpr ColorQuad ColorQuad::Faded(float Opacity) const noexcept
{
    return { Red, Green, Blue, Alpha * Opacity };
}

constexpr ColorQuad ColorQuad::FromHex(uint32_t Rgb, float AlphaChannel) noexcept
{
    return {
        static_cast<float>((Rgb >> 16) & 0xFFu) / 255.0f,
        static_cast<float>((Rgb >> 8) & 0xFFu) / 255.0f,
        static_cast<float>(Rgb & 0xFFu) / 255.0f,
        AlphaChannel,
    };
}

constexpr ColorQuad ColorQuad::Blend(ColorQuad From, ColorQuad To, float Factor) noexcept
{
    return {
        From.Red + (To.Red - From.Red) * Factor,
        From.Green + (To.Green - From.Green) * Factor,
        From.Blue + (To.Blue - From.Blue) * Factor,
        From.Alpha + (To.Alpha - From.Alpha) * Factor,
    };
}

constexpr float ColorQuad::Luminance() const noexcept
{
    return 0.2126f * Red + 0.7152f * Green + 0.0722f * Blue;
}

inline ThemePreset ThemeCatalog::QueryActiveTheme() const noexcept
{
    return ActiveTheme;
}

inline AccentPreset ThemeCatalog::QueryActiveAccent() const noexcept
{
    return ActiveAccent;
}

inline float ThemeCatalog::QueryCornerRadius() const noexcept
{
    return CornerRadiusPixels;
}

} // namespace Frontier
