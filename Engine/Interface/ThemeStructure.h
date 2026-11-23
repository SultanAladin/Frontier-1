/*==========================================================================================================================================
                                                              THEMESTRUCTURE.H
==========================================================================================================================================*/
// 🧩 Theme surface palettes, accents, typography, and opaque darkest-surface selection.

#pragma once

#if defined(_MSC_VER)
#pragma warning(disable : 4324) // Disable structure padding alignment warning under /WX
#endif

#include <array>
#include <cstdint>
#include <string_view>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  THEME CATEGORY
//------------------------------------------------------------------------------------------------------------------------

enum class ThemeCategory : uint32_t
{
    Oled    = 0, // Pure black (#000000) default
    Dark    = 1, // Deep dark (#111111)
    Dim     = 2, // Slate navy (#0F172A)
    Light   = 3, // Bright white (#F1F5F9)
    Sepia   = 4, // Warm parchment (#EADDCF)
    Dracula = 5, // Purple tinted (#282A36)
    Nord    = 6, // Arctic frost (#2E3440)
    GitHub  = 7, // GitHub dark (#0D1117)
    Count   = 8
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 ACCENT CATEGORY
//------------------------------------------------------------------------------------------------------------------------

enum class AccentCategory : uint32_t
{
    White   = 0, // #FFFFFF
    Orange  = 1, // #F97316
    Amber   = 2, // #F59E0B
    Lime    = 3, // #84CC16
    Emerald = 4, // #10B981
    Cyan    = 5, // #06B6D4
    Blue    = 6, // #3B82F6 (Default)
    Violet  = 7, // #8B5CF6
    Fuchsia = 8, // #D946EF
    Rose    = 9, // #F43F5E
    Count   = 10
};

//------------------------------------------------------------------------------------------------------------------------
//                                               FONT FAMILY CATEGORY
//------------------------------------------------------------------------------------------------------------------------

enum class FontFamilyCategory : uint32_t
{
    GeneralSans   = 0, // General Sans (Default - Clean & Modern)
    Inter         = 1, // Inter (Author)
    Archivo       = 2, // Archivo (Technical & Solid)
    SpaceGrotesk  = 3, // Space Grotesk (Grotesque Display)
    ClashDisplay  = 4, // Clash Display (Bold & Distinct)
    Montserrat    = 5, // Montserrat (Geometric & Wide)
    Poppins       = 6, // Poppins (Friendly & Round)
    JetBrainsMono = 7, // JetBrains Mono (Monospaced Code)
    Count         = 8
};

//------------------------------------------------------------------------------------------------------------------------
//                                               TYPOGRAPHY ROLE CATEGORY
//------------------------------------------------------------------------------------------------------------------------

enum class TypographyRoleCategory : uint32_t
{
    Title     = 0, // 24px Bold (700)
    Header    = 1, // 20px Semibold (600)
    Subheader = 2, // 16px Medium (500)
    Body      = 3, // 14px Regular (400)
    Label     = 4, // 12px Medium (500)
    Caption   = 5, // 11px Light (300)
    Warning   = 6, // 13px Semibold (600)
    Alert     = 7, // 13px Bold (700)
    Count     = 8
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 COLOR QUAD VECTOR
//------------------------------------------------------------------------------------------------------------------------

struct ColorQuad
{
    float Red;   // [0..1] red channel
    float Green; // [0..1] green channel
    float Blue;  // [0..1] blue channel
    float Alpha; // [0..1] alpha opacity channel
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  THEME PALETTE
//------------------------------------------------------------------------------------------------------------------------

struct ThemePalette
{
    const char *TitleText;         // [text] display label
    ColorQuad   MainBackground;    // [color] main window canvas color
    ColorQuad   PanelBackground;   // [color] top notch & drawer background
    ColorQuad   CardBackground;    // [color] container card background
    ColorQuad   CardSubBackground; // [color] nested control background
    ColorQuad   TextMain;          // [color] primary text color
    ColorQuad   TextMuted;         // [color] secondary muted text color
    ColorQuad   PanelBorder;       // [color] border stroke color
    ColorQuad   DividerColor;      // [color] line separator color
    ColorQuad   InputBackground;   // [color] Notch colors.inputBg  — section cards, fields
    ColorQuad   ActiveBackground;  // [color] Notch colors.activeBg — hover / selected rows
};

//------------------------------------------------------------------------------------------------------------------------
//                                                THEME STRUCTURE
//------------------------------------------------------------------------------------------------------------------------

class ThemeStructure
{
  public:
    ThemeStructure() noexcept;
    ~ThemeStructure() noexcept = default;

    ThemeStructure(const ThemeStructure &)            = default;
    ThemeStructure &operator=(const ThemeStructure &) = default;

    void                        ConfigureTheme(ThemeCategory DesiredTheme) noexcept;
    [[nodiscard]] ThemeCategory QueryActiveTheme() const noexcept
    {
        return ActiveTheme;
    }

    void                         ConfigureAccent(AccentCategory DesiredAccent) noexcept;
    [[nodiscard]] AccentCategory QueryActiveAccent() const noexcept
    {
        return ActiveAccent;
    }

    void                ConfigureCornerRadius(float RadiusPixels) noexcept;
    [[nodiscard]] float QueryCornerRadius() const noexcept
    {
        return CornerRadiusPixels;
    }

    void ConfigureFontFamily(FontFamilyCategory DesiredFamily) noexcept
    {
        ActiveFontFamily = DesiredFamily;
    }
    [[nodiscard]] FontFamilyCategory QueryActiveFontFamily() const noexcept
    {
        return ActiveFontFamily;
    }

    [[nodiscard]] const ThemePalette &QueryPalette() const noexcept;
    [[nodiscard]] ColorQuad           QueryAccentColor() const noexcept;
    [[nodiscard]] ColorQuad           QueryDarkestSurface() const noexcept;
    [[nodiscard]] float               QueryTypographySize(TypographyRoleCategory Role) const noexcept;

    // Single unified conversion operator for active theme category
    template <typename TargetType> [[nodiscard]] TargetType Convert() const noexcept;

  private:
    ThemeCategory      ActiveTheme;        // [category] active surface theme (default OLED)
    AccentCategory     ActiveAccent;       // [category] active accent swatch (default Blue)
    FontFamilyCategory ActiveFontFamily;   // [category] active font family (default GeneralSans)
    float              CornerRadiusPixels; // [px] UI corner roundness (default 24.0px)
    std::array<ThemePalette, static_cast<size_t>(ThemeCategory::Count)>
                                                                      PaletteArray; // [palettes] 8 surface definitions
    std::array<ColorQuad, static_cast<size_t>(AccentCategory::Count)> AccentArray;  // [accents] 10 accent swatches
    std::array<float, static_cast<size_t>(TypographyRoleCategory::Count)> TypographySizes; // [px] 8 role sizes
};

template <> inline ThemeCategory ThemeStructure::Convert<ThemeCategory>() const noexcept
{
    return ActiveTheme;
}

template <> inline AccentCategory ThemeStructure::Convert<AccentCategory>() const noexcept
{
    return ActiveAccent;
}

template <> inline float ThemeStructure::Convert<float>() const noexcept
{
    return CornerRadiusPixels;
}

} // namespace Frontier
