/*============================================================================================================================================
                                                             THEMEREPOSITORY.CPP
============================================================================================================================================*/
// 🧩 Exact 1:1 surface themes, accent swatches, semantic alert swatches, and typography scale slots.

#include "Engine/Components/Appearance/ThemeRepository.h"

#include <algorithm>

namespace Frontier
{

namespace
{

constexpr uint32_t AccentHexSwatches[10] = {0xFFFFFFu, 0xF97316u, 0xF59E0Bu, 0x84CC16u, 0x10B981u,
                                            0x06B6D4u, 0x3B82F6u, 0x8B5CF6u, 0xD946EFu, 0xF43F5Eu};

constexpr uint32_t SemanticHexGrid[4][4] = {
    {0xF59E0Bu, 0xEAB308u, 0xFBBF24u, 0xF97316u}, // Warning
    {0x10B981u, 0x22C55Eu, 0x34D399u, 0x059669u}, // Success
    {0x3B82F6u, 0x0EA5E9u, 0x60A5FAu, 0x2563EBu}, // Notice
    {0xEAB308u, 0xFACC15u, 0xFDE047u, 0xCA8A04u}, // Caution
};

struct TypeSlotSpecimen
{
    const char *Label;
    const char *Sample;
};

constexpr TypeSlotSpecimen SlotSpecimens[AppearanceSettings::TypeSlotCount] = {
    {"Title", "Display Title"},
    {"Header", "Section Header"},
    {"Subheader", "Section Subheader"},
    {"Body", "The quick brown fox jumps over the lazy dog."},
    {"Label", "Form Label"},
    {"Caption", "Small caption text"},
};

} // namespace

ThemeRepository::ThemeRepository() noexcept
{
    SurfaceSets[static_cast<std::size_t>(ThemePreset::Oled)] = ThemeSurfaceSet{
        "OLED",
        ColorQuad{0.000f, 0.000f, 0.000f, 1.00f},
        ColorQuad{0.039f, 0.039f, 0.039f, 1.00f},
        ColorQuad{0.078f, 0.078f, 0.082f, 1.00f},
        ColorQuad{0.110f, 0.110f, 0.118f, 1.00f},
        ColorQuad{1.000f, 1.000f, 1.000f, 0.95f},
        ColorQuad{1.000f, 1.000f, 1.000f, 0.40f},
        ColorQuad{1.000f, 1.000f, 1.000f, 0.04f},
        ColorQuad{1.000f, 1.000f, 1.000f, 0.04f},
        ColorQuad{1.000f, 1.000f, 1.000f, 0.05f},
        ColorQuad{1.000f, 1.000f, 1.000f, 0.08f},
    };

    SurfaceSets[static_cast<std::size_t>(ThemePreset::Dark)] = ThemeSurfaceSet{
        "Dark",
        ColorQuad{0.067f, 0.067f, 0.067f, 1.00f},
        ColorQuad{0.102f, 0.102f, 0.102f, 1.00f},
        ColorQuad{0.133f, 0.133f, 0.145f, 1.00f},
        ColorQuad{0.173f, 0.173f, 0.188f, 1.00f},
        ColorQuad{1.000f, 1.000f, 1.000f, 0.90f},
        ColorQuad{1.000f, 1.000f, 1.000f, 0.50f},
        ColorQuad{1.000f, 1.000f, 1.000f, 0.06f},
        ColorQuad{1.000f, 1.000f, 1.000f, 0.06f},
        ColorQuad{1.000f, 1.000f, 1.000f, 0.05f},
        ColorQuad{1.000f, 1.000f, 1.000f, 0.10f},
    };

    SurfaceSets[static_cast<std::size_t>(ThemePreset::Dim)] = ThemeSurfaceSet{
        "Dim",
        ColorQuad{0.059f, 0.090f, 0.165f, 1.00f},
        ColorQuad{0.118f, 0.161f, 0.231f, 1.00f},
        ColorQuad{0.157f, 0.208f, 0.282f, 1.00f},
        ColorQuad{0.200f, 0.255f, 0.333f, 1.00f},
        ColorQuad{0.945f, 0.961f, 0.976f, 1.00f},
        ColorQuad{0.580f, 0.639f, 0.722f, 1.00f},
        ColorQuad{0.200f, 0.255f, 0.333f, 0.50f},
        ColorQuad{0.200f, 0.255f, 0.333f, 0.50f},
        ColorQuad{0.008f, 0.024f, 0.090f, 0.50f},
        ColorQuad{0.200f, 0.255f, 0.333f, 1.00f},
    };

    SurfaceSets[static_cast<std::size_t>(ThemePreset::Light)] = ThemeSurfaceSet{
        "Light",
        ColorQuad{0.945f, 0.961f, 0.976f, 1.00f},
        ColorQuad{1.000f, 1.000f, 1.000f, 1.00f},
        ColorQuad{0.973f, 0.980f, 0.988f, 1.00f},
        ColorQuad{0.886f, 0.910f, 0.941f, 1.00f},
        ColorQuad{0.059f, 0.090f, 0.165f, 1.00f},
        ColorQuad{0.392f, 0.455f, 0.545f, 1.00f},
        ColorQuad{0.886f, 0.910f, 0.941f, 1.00f},
        ColorQuad{0.886f, 0.910f, 0.941f, 1.00f},
        ColorQuad{0.973f, 0.980f, 0.988f, 1.00f},
        ColorQuad{0.945f, 0.961f, 0.976f, 1.00f},
    };

    SurfaceSets[static_cast<std::size_t>(ThemePreset::Sepia)] = ThemeSurfaceSet{
        "Sepia",
        ColorQuad{0.918f, 0.867f, 0.812f, 1.00f},
        ColorQuad{0.957f, 0.922f, 0.882f, 1.00f},
        ColorQuad{0.980f, 0.933f, 0.851f, 1.00f},
        ColorQuad{0.933f, 0.863f, 0.765f, 1.00f},
        ColorQuad{0.361f, 0.294f, 0.227f, 1.00f},
        ColorQuad{0.549f, 0.478f, 0.420f, 1.00f},
        ColorQuad{0.847f, 0.765f, 0.690f, 1.00f},
        ColorQuad{0.847f, 0.765f, 0.690f, 1.00f},
        ColorQuad{0.976f, 0.957f, 0.937f, 1.00f},
        ColorQuad{0.918f, 0.867f, 0.812f, 1.00f},
    };

    SurfaceSets[static_cast<std::size_t>(ThemePreset::Dracula)] = ThemeSurfaceSet{
        "Dracula",
        ColorQuad{0.157f, 0.165f, 0.212f, 1.00f},
        ColorQuad{0.267f, 0.278f, 0.353f, 1.00f},
        ColorQuad{0.220f, 0.227f, 0.349f, 1.00f},
        ColorQuad{0.384f, 0.447f, 0.643f, 1.00f},
        ColorQuad{0.973f, 0.973f, 0.949f, 1.00f},
        ColorQuad{0.384f, 0.447f, 0.643f, 1.00f},
        ColorQuad{0.384f, 0.447f, 0.643f, 0.30f},
        ColorQuad{0.384f, 0.447f, 0.643f, 0.30f},
        ColorQuad{0.157f, 0.165f, 0.212f, 1.00f},
        ColorQuad{0.384f, 0.447f, 0.643f, 0.20f},
    };

    SurfaceSets[static_cast<std::size_t>(ThemePreset::Nord)] = ThemeSurfaceSet{
        "Nord",
        ColorQuad{0.180f, 0.204f, 0.251f, 1.00f},
        ColorQuad{0.231f, 0.259f, 0.322f, 1.00f},
        ColorQuad{0.263f, 0.298f, 0.369f, 1.00f},
        ColorQuad{0.298f, 0.337f, 0.416f, 1.00f},
        ColorQuad{0.925f, 0.937f, 0.957f, 1.00f},
        ColorQuad{0.847f, 0.871f, 0.914f, 1.00f},
        ColorQuad{0.298f, 0.337f, 0.416f, 1.00f},
        ColorQuad{0.298f, 0.337f, 0.416f, 1.00f},
        ColorQuad{0.180f, 0.204f, 0.251f, 1.00f},
        ColorQuad{0.263f, 0.298f, 0.369f, 1.00f},
    };

    SurfaceSets[static_cast<std::size_t>(ThemePreset::GitHub)] = ThemeSurfaceSet{
        "GitHub",
        ColorQuad{0.051f, 0.067f, 0.090f, 1.00f},
        ColorQuad{0.086f, 0.106f, 0.133f, 1.00f},
        ColorQuad{0.129f, 0.149f, 0.176f, 1.00f},
        ColorQuad{0.188f, 0.212f, 0.239f, 1.00f},
        ColorQuad{0.788f, 0.820f, 0.851f, 1.00f},
        ColorQuad{0.545f, 0.580f, 0.620f, 1.00f},
        ColorQuad{0.188f, 0.212f, 0.239f, 1.00f},
        ColorQuad{0.188f, 0.212f, 0.239f, 1.00f},
        ColorQuad{0.051f, 0.067f, 0.090f, 1.00f},
        ColorQuad{0.129f, 0.149f, 0.176f, 1.00f},
    };
}

void ThemeRepository::ConfigureTheme(ThemePreset DesiredTheme) noexcept
{
    if (DesiredTheme < ThemePreset::Count)
        ActiveTheme = DesiredTheme;
}

void ThemeRepository::ConfigureAccent(AccentPreset DesiredAccent) noexcept
{
    if (DesiredAccent < AccentPreset::Count)
        ActiveAccent = DesiredAccent;
}

void ThemeRepository::ConfigureCornerRadius(float RadiusPixels) noexcept
{
    CornerRadiusPixels = std::clamp(RadiusPixels, 0.0f, 32.0f);
}

const ThemeSurfaceSet &ThemeRepository::QuerySurfaceSet() const noexcept
{
    return QuerySurfaceSet(ActiveTheme);
}

const ThemeSurfaceSet &ThemeRepository::QuerySurfaceSet(ThemePreset Preset) const noexcept
{
    const std::size_t Index = static_cast<std::size_t>(Preset);
    return Index < SurfaceSets.size() ? SurfaceSets[Index] : SurfaceSets[0];
}

ColorQuad ThemeRepository::QueryDarkestSurface() const noexcept
{
    const ThemeSurfaceSet &Set     = QuerySurfaceSet();
    ColorQuad              Darkest = Set.MainBackground;
    for (const ColorQuad Tone :
         {Set.PanelBackground, Set.SectionBackground, Set.SubtleBackground, Set.InputBackground, Set.ActiveBackground})
    {
        if (Tone.Luminance() < Darkest.Luminance())
            Darkest = Tone;
    }
    Darkest.Alpha = 1.0f;
    return Darkest;
}

ColorQuad ThemeRepository::QueryAccentColor() const noexcept
{
    return QueryAccentColor(ActiveAccent);
}

ColorQuad ThemeRepository::QueryAccentColor(AccentPreset Accent) noexcept
{
    const uint32_t Index = std::min<uint32_t>(static_cast<uint32_t>(Accent), 9u);
    return ColorQuad::FromHex(AccentHexSwatches[Index]);
}

ColorQuad ThemeRepository::QuerySemanticColor(uint32_t RowIndex, uint32_t SwatchIndex) noexcept
{
    return ColorQuad::FromHex(SemanticHexGrid[std::min(RowIndex, 3u)][std::min(SwatchIndex, 3u)]);
}

const char *ThemeRepository::QueryThemeLabel(ThemePreset Preset) noexcept
{
    switch (Preset)
    {
    case ThemePreset::Oled:    return "OLED";
    case ThemePreset::Dark:    return "Dark";
    case ThemePreset::Dim:     return "Dim";
    case ThemePreset::Light:   return "Light";
    case ThemePreset::Sepia:   return "Sepia";
    case ThemePreset::Dracula: return "Dracula";
    case ThemePreset::Nord:    return "Nord";
    case ThemePreset::GitHub:  return "GitHub";
    case ThemePreset::Count:   break;
    }
    return "OLED";
}

const char *ThemeRepository::QueryTypeSlotLabel(uint32_t SlotOrdinal) noexcept
{
    return SlotSpecimens[std::min(SlotOrdinal, AppearanceSettings::TypeSlotCount - 1u)].Label;
}

const char *ThemeRepository::QueryTypeSlotSample(uint32_t SlotOrdinal) noexcept
{
    return SlotSpecimens[std::min(SlotOrdinal, AppearanceSettings::TypeSlotCount - 1u)].Sample;
}

} // namespace Frontier
