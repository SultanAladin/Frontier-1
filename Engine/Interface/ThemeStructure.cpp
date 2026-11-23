/*==========================================================================================================================================
                                                             THEMESTRUCTURE.CPP
==========================================================================================================================================*/
// 🧩 Theme tokens, named surface palettes, and opaque darkest-surface selection.

#include "ThemeStructure.h"
#include <algorithm>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

ThemeStructure::ThemeStructure() noexcept
    : ActiveTheme(ThemeCategory::Oled) // Default OLED (pure black #000000)
      ,
      ActiveAccent(AccentCategory::Blue) // Default Blue (#3B82F6)
      ,
      ActiveFontFamily(FontFamilyCategory::GeneralSans) // Default General Sans
      ,
      CornerRadiusPixels(24.0f) // Default 24px radius
      ,
      PaletteArray{}, AccentArray{}, TypographySizes{}
{
    // Values are Notch src/types.ts THEME_CONFIGS verbatim (Tailwind slate palette resolved to hex); the two Card*
    //    fields are engine additions used by nested surfaces that Notch does not define.
    // 0: OLED — bg-black / bg-[#0A0A0A] / border-white/[0.04] / text-white/95 / text-white/40 / bg-white/5 / activeBg white/[0.08]
    PaletteArray[static_cast<size_t>(ThemeCategory::Oled)] = ThemePalette{
        "OLED",
        ColorQuad{0.000f, 0.000f, 0.000f, 1.00f}, // MainBackground   (mainBg)
        ColorQuad{0.039f, 0.039f, 0.039f, 1.00f}, // PanelBackground  (panelBg)
        ColorQuad{0.078f, 0.078f, 0.082f, 1.00f}, // CardBackground   (engine)
        ColorQuad{0.110f, 0.110f, 0.118f, 1.00f}, // CardSubBackground(engine)
        ColorQuad{1.000f, 1.000f, 1.000f, 0.95f}, // TextMain         (text)
        ColorQuad{1.000f, 1.000f, 1.000f, 0.40f}, // TextMuted        (textMuted)
        ColorQuad{1.000f, 1.000f, 1.000f, 0.04f}, // PanelBorder      (panelBorder)
        ColorQuad{1.000f, 1.000f, 1.000f, 0.04f}, // DividerColor     (divider)
        ColorQuad{1.000f, 1.000f, 1.000f, 0.05f}, // InputBackground  (inputBg)
        ColorQuad{1.000f, 1.000f, 1.000f, 0.08f}  // ActiveBackground (activeBg)
    };

    // 1: Dark — bg-[#111111] / bg-[#1a1a1a] / border-white/[0.06] / text-white/90 / text-white/50 / bg-white/5 / activeBg white/[0.1]
    PaletteArray[static_cast<size_t>(ThemeCategory::Dark)] = ThemePalette{
        "Dark",
        ColorQuad{0.067f, 0.067f, 0.067f, 1.00f}, // MainBackground   (mainBg)
        ColorQuad{0.102f, 0.102f, 0.102f, 1.00f}, // PanelBackground  (panelBg)
        ColorQuad{0.133f, 0.133f, 0.145f, 1.00f}, // CardBackground   (engine)
        ColorQuad{0.173f, 0.173f, 0.188f, 1.00f}, // CardSubBackground(engine)
        ColorQuad{1.000f, 1.000f, 1.000f, 0.90f}, // TextMain         (text)
        ColorQuad{1.000f, 1.000f, 1.000f, 0.50f}, // TextMuted        (textMuted)
        ColorQuad{1.000f, 1.000f, 1.000f, 0.06f}, // PanelBorder      (panelBorder)
        ColorQuad{1.000f, 1.000f, 1.000f, 0.06f}, // DividerColor     (divider)
        ColorQuad{1.000f, 1.000f, 1.000f, 0.05f}, // InputBackground  (inputBg)
        ColorQuad{1.000f, 1.000f, 1.000f, 0.10f}  // ActiveBackground (activeBg)
    };

    // 2: Dim — bg-slate-900 / bg-slate-800 / border-slate-700/50 / text-slate-100 / text-slate-400 / bg-slate-950/50 / bg-slate-700
    PaletteArray[static_cast<size_t>(ThemeCategory::Dim)] = ThemePalette{
        "Dim",
        ColorQuad{0.059f, 0.090f, 0.165f, 1.00f}, // MainBackground   (mainBg)
        ColorQuad{0.118f, 0.161f, 0.231f, 1.00f}, // PanelBackground  (panelBg)
        ColorQuad{0.157f, 0.208f, 0.282f, 1.00f}, // CardBackground   (engine)
        ColorQuad{0.200f, 0.255f, 0.333f, 1.00f}, // CardSubBackground(engine)
        ColorQuad{0.945f, 0.961f, 0.976f, 1.00f}, // TextMain         (text)
        ColorQuad{0.580f, 0.639f, 0.722f, 1.00f}, // TextMuted        (textMuted)
        ColorQuad{0.200f, 0.255f, 0.333f, 0.50f}, // PanelBorder      (panelBorder)
        ColorQuad{0.200f, 0.255f, 0.333f, 0.50f}, // DividerColor     (divider)
        ColorQuad{0.008f, 0.024f, 0.090f, 0.50f}, // InputBackground  (inputBg)
        ColorQuad{0.200f, 0.255f, 0.333f, 1.00f}  // ActiveBackground (activeBg)
    };

    // 3: Light — bg-slate-100 / bg-white / border-slate-200 / text-slate-900 / text-slate-500 / bg-slate-50 / bg-slate-100
    PaletteArray[static_cast<size_t>(ThemeCategory::Light)] = ThemePalette{
        "Light",
        ColorQuad{0.945f, 0.961f, 0.976f, 1.00f}, // MainBackground   (mainBg)
        ColorQuad{1.000f, 1.000f, 1.000f, 1.00f}, // PanelBackground  (panelBg)
        ColorQuad{0.973f, 0.980f, 0.988f, 1.00f}, // CardBackground   (engine)
        ColorQuad{0.886f, 0.910f, 0.941f, 1.00f}, // CardSubBackground(engine)
        ColorQuad{0.059f, 0.090f, 0.165f, 1.00f}, // TextMain         (text)
        ColorQuad{0.392f, 0.455f, 0.545f, 1.00f}, // TextMuted        (textMuted)
        ColorQuad{0.886f, 0.910f, 0.941f, 1.00f}, // PanelBorder      (panelBorder)
        ColorQuad{0.886f, 0.910f, 0.941f, 1.00f}, // DividerColor     (divider)
        ColorQuad{0.973f, 0.980f, 0.988f, 1.00f}, // InputBackground  (inputBg)
        ColorQuad{0.945f, 0.961f, 0.976f, 1.00f}  // ActiveBackground (activeBg)
    };

    // 4: Sepia — bg-[#eaddcf] / bg-[#f4ebe1] / border-[#d8c3b0] / text-[#5c4b3a] / text-[#8c7a6b] / bg-[#f9f4ef] / bg-[#eaddcf]
    PaletteArray[static_cast<size_t>(ThemeCategory::Sepia)] = ThemePalette{
        "Sepia",
        ColorQuad{0.918f, 0.867f, 0.812f, 1.00f}, // MainBackground   (mainBg)
        ColorQuad{0.957f, 0.922f, 0.882f, 1.00f}, // PanelBackground  (panelBg)
        ColorQuad{0.980f, 0.933f, 0.851f, 1.00f}, // CardBackground   (engine)
        ColorQuad{0.933f, 0.863f, 0.765f, 1.00f}, // CardSubBackground(engine)
        ColorQuad{0.361f, 0.294f, 0.227f, 1.00f}, // TextMain         (text)
        ColorQuad{0.549f, 0.478f, 0.420f, 1.00f}, // TextMuted        (textMuted)
        ColorQuad{0.847f, 0.765f, 0.690f, 1.00f}, // PanelBorder      (panelBorder)
        ColorQuad{0.847f, 0.765f, 0.690f, 1.00f}, // DividerColor     (divider)
        ColorQuad{0.976f, 0.957f, 0.937f, 1.00f}, // InputBackground  (inputBg)
        ColorQuad{0.918f, 0.867f, 0.812f, 1.00f}  // ActiveBackground (activeBg)
    };

    // 5: Dracula — bg-[#282a36] / bg-[#44475a] / border-[#6272a4]/30 / text-[#f8f8f2] / text-[#6272a4] / bg-[#282a36] / bg-[#6272a4]/20
    PaletteArray[static_cast<size_t>(ThemeCategory::Dracula)] = ThemePalette{
        "Dracula",
        ColorQuad{0.157f, 0.165f, 0.212f, 1.00f}, // MainBackground   (mainBg)
        ColorQuad{0.267f, 0.278f, 0.353f, 1.00f}, // PanelBackground  (panelBg)
        ColorQuad{0.220f, 0.227f, 0.349f, 1.00f}, // CardBackground   (engine)
        ColorQuad{0.384f, 0.447f, 0.643f, 1.00f}, // CardSubBackground(engine)
        ColorQuad{0.973f, 0.973f, 0.949f, 1.00f}, // TextMain         (text)
        ColorQuad{0.384f, 0.447f, 0.643f, 1.00f}, // TextMuted        (textMuted)
        ColorQuad{0.384f, 0.447f, 0.643f, 0.30f}, // PanelBorder      (panelBorder)
        ColorQuad{0.384f, 0.447f, 0.643f, 0.30f}, // DividerColor     (divider)
        ColorQuad{0.157f, 0.165f, 0.212f, 1.00f}, // InputBackground  (inputBg)
        ColorQuad{0.384f, 0.447f, 0.643f, 0.20f}  // ActiveBackground (activeBg)
    };

    // 6: Nord — bg-[#2e3440] / bg-[#3b4252] / border-[#4c566a] / text-[#eceff4] / text-[#d8dee9] / bg-[#2e3440] / bg-[#434c5e]
    PaletteArray[static_cast<size_t>(ThemeCategory::Nord)] = ThemePalette{
        "Nord",
        ColorQuad{0.180f, 0.204f, 0.251f, 1.00f}, // MainBackground   (mainBg)
        ColorQuad{0.231f, 0.259f, 0.322f, 1.00f}, // PanelBackground  (panelBg)
        ColorQuad{0.263f, 0.298f, 0.369f, 1.00f}, // CardBackground   (engine)
        ColorQuad{0.298f, 0.337f, 0.416f, 1.00f}, // CardSubBackground(engine)
        ColorQuad{0.925f, 0.937f, 0.957f, 1.00f}, // TextMain         (text)
        ColorQuad{0.847f, 0.871f, 0.914f, 1.00f}, // TextMuted        (textMuted)
        ColorQuad{0.298f, 0.337f, 0.416f, 1.00f}, // PanelBorder      (panelBorder)
        ColorQuad{0.298f, 0.337f, 0.416f, 1.00f}, // DividerColor     (divider)
        ColorQuad{0.180f, 0.204f, 0.251f, 1.00f}, // InputBackground  (inputBg)
        ColorQuad{0.263f, 0.298f, 0.369f, 1.00f}  // ActiveBackground (activeBg)
    };

    // 7: GitHub — bg-[#0d1117] / bg-[#161b22] / border-[#30363d] / text-[#c9d1d9] / text-[#8b949e] / bg-[#0d1117] / bg-[#21262d]
    PaletteArray[static_cast<size_t>(ThemeCategory::GitHub)] = ThemePalette{
        "GitHub",
        ColorQuad{0.051f, 0.067f, 0.090f, 1.00f}, // MainBackground   (mainBg)
        ColorQuad{0.086f, 0.106f, 0.133f, 1.00f}, // PanelBackground  (panelBg)
        ColorQuad{0.129f, 0.149f, 0.176f, 1.00f}, // CardBackground   (engine)
        ColorQuad{0.188f, 0.212f, 0.239f, 1.00f}, // CardSubBackground(engine)
        ColorQuad{0.788f, 0.820f, 0.851f, 1.00f}, // TextMain         (text)
        ColorQuad{0.545f, 0.580f, 0.620f, 1.00f}, // TextMuted        (textMuted)
        ColorQuad{0.188f, 0.212f, 0.239f, 1.00f}, // PanelBorder      (panelBorder)
        ColorQuad{0.188f, 0.212f, 0.239f, 1.00f}, // DividerColor     (divider)
        ColorQuad{0.051f, 0.067f, 0.090f, 1.00f}, // InputBackground  (inputBg)
        ColorQuad{0.129f, 0.149f, 0.176f, 1.00f}  // ActiveBackground (activeBg)
    };

    // 10 Accent Color Swatches
    AccentArray[static_cast<size_t>(AccentCategory::White)]   = ColorQuad{1.000f, 1.000f, 1.000f, 1.0f}; // #FFFFFF
    AccentArray[static_cast<size_t>(AccentCategory::Orange)]  = ColorQuad{0.976f, 0.451f, 0.086f, 1.0f}; // #F97316
    AccentArray[static_cast<size_t>(AccentCategory::Amber)]   = ColorQuad{0.961f, 0.620f, 0.043f, 1.0f}; // #F59E0B
    AccentArray[static_cast<size_t>(AccentCategory::Lime)]    = ColorQuad{0.518f, 0.800f, 0.086f, 1.0f}; // #84CC16
    AccentArray[static_cast<size_t>(AccentCategory::Emerald)] = ColorQuad{0.063f, 0.725f, 0.506f, 1.0f}; // #10B981
    AccentArray[static_cast<size_t>(AccentCategory::Cyan)]    = ColorQuad{0.024f, 0.714f, 0.831f, 1.0f}; // #06B6D4
    AccentArray[static_cast<size_t>(AccentCategory::Blue)] =
        ColorQuad{0.231f, 0.510f, 0.965f, 1.0f}; // #3B82F6 (Default)
    AccentArray[static_cast<size_t>(AccentCategory::Violet)]  = ColorQuad{0.545f, 0.361f, 0.965f, 1.0f}; // #8B5CF6
    AccentArray[static_cast<size_t>(AccentCategory::Fuchsia)] = ColorQuad{0.851f, 0.275f, 0.937f, 1.0f}; // #D946EF
    AccentArray[static_cast<size_t>(AccentCategory::Rose)]    = ColorQuad{0.957f, 0.247f, 0.369f, 1.0f}; // #F43F5E

    // 8 Typography Role Standard Sizes [px]
    TypographySizes[static_cast<size_t>(TypographyRoleCategory::Title)]     = 24.0f;
    TypographySizes[static_cast<size_t>(TypographyRoleCategory::Header)]    = 20.0f;
    TypographySizes[static_cast<size_t>(TypographyRoleCategory::Subheader)] = 16.0f;
    TypographySizes[static_cast<size_t>(TypographyRoleCategory::Body)]      = 14.0f;
    TypographySizes[static_cast<size_t>(TypographyRoleCategory::Label)]     = 12.0f;
    TypographySizes[static_cast<size_t>(TypographyRoleCategory::Caption)]   = 11.0f;
    TypographySizes[static_cast<size_t>(TypographyRoleCategory::Warning)]   = 13.0f;
    TypographySizes[static_cast<size_t>(TypographyRoleCategory::Alert)]     = 13.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THEME ACCESSORS
//------------------------------------------------------------------------------------------------------------------------

void ThemeStructure::ConfigureTheme(ThemeCategory DesiredTheme) noexcept
{
    ActiveTheme = DesiredTheme;
}

void ThemeStructure::ConfigureAccent(AccentCategory DesiredAccent) noexcept
{
    ActiveAccent = DesiredAccent;
}

void ThemeStructure::ConfigureCornerRadius(float RadiusPixels) noexcept
{
    CornerRadiusPixels = std::clamp(RadiusPixels, 0.0f, 32.0f);
}

const ThemePalette &ThemeStructure::QueryPalette() const noexcept
{
    size_t Index = static_cast<size_t>(ActiveTheme);
    if (Index < PaletteArray.size())
    {
        return PaletteArray[Index];
    }
    return PaletteArray[0];
}

ColorQuad ThemeStructure::QueryDarkestSurface() const noexcept
{
    const auto &Palette   = QueryPalette();
    const auto  Luminance = [](ColorQuad Tone) { return .2126f * Tone.Red + .7152f * Tone.Green + .0722f * Tone.Blue; };
    auto        Darkest   = Palette.MainBackground;
    for (const auto Tone : {Palette.PanelBackground, Palette.CardBackground, Palette.CardSubBackground,
                            Palette.InputBackground, Palette.ActiveBackground})
        if (Luminance(Tone) < Luminance(Darkest))
            Darkest = Tone;
    // 📝 Surface RGB is a theme token; overlay opacity must never make the trapezoid transparent.
    Darkest.Alpha = 1;
    return Darkest;
}

ColorQuad ThemeStructure::QueryAccentColor() const noexcept
{
    size_t Index = static_cast<size_t>(ActiveAccent);
    if (Index < AccentArray.size())
    {
        return AccentArray[Index];
    }
    return AccentArray[static_cast<size_t>(AccentCategory::Blue)];
}

float ThemeStructure::QueryTypographySize(TypographyRoleCategory Role) const noexcept
{
    size_t Index = static_cast<size_t>(Role);
    if (Index < TypographySizes.size())
    {
        return TypographySizes[Index];
    }
    return 14.0f;
}

} // namespace Frontier
