/*============================================================================================================================================
                                                           COLOURINTERCHANGE.H
============================================================================================================================================*/
// 🧩 External colour & gradient interchange: HTML Studio JSON/TOML colour blocks, CSS linear-gradient, SVG <linearGradient>, GIMP .gpl/.ggr, Adobe .ase/.aco & .cube 1D LUT

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourDocument.h"
#include "Engine/ColourSpace/ColourSourceTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Interchange
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ExternalColourFormat : uint8_t
{
    Unknown         = 0u, // [-] - Unrecognized external colour format
    HtmlStudioJson  = 1u, // [-] - HTML Particle/Texture Studio JSON colour block
    CssGradient     = 2u, // [-] - CSS linear-gradient(...) specification
    SvgGradient     = 3u, // [-] - SVG <linearGradient><stop .../></linearGradient>
    GimpSwatchGpl   = 4u, // [-] - GIMP .gpl named RGB swatch file
    GimpGradientGgr = 5u, // [-] - GIMP .ggr multi-segment RGBA gradient file
    AdobeSwatchAse  = 6u, // [-] - Adobe Swatch Exchange .ase binary container
    AdobeColourAco  = 7u, // [-] - Adobe Photoshop .aco binary swatch file
    CubeLut1D       = 8u, // [-] - Iridas / Resolve .cube 1D colour LUT
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class ColourInterchange
{
public:
    [[nodiscard]] static ExternalColourFormat DetectFormatFromPath(const std::filesystem::path& FilePath) noexcept;

    // 📝 HTML Particle / Texture Studio JSON colour block (colorStops, colA/colB/colC, colorRampPreset, colorWrap, colorInterp, colorRepeat, colorPhase, blend)
    [[nodiscard]] static bool ImportHtmlStudioJsonBlock(
        std::string_view                  JsonText,
        ColourSpace::ColourSourceProfile& OutProfile,
        std::string&                      OutDiagnostic);

    [[nodiscard]] static std::string ExportHtmlStudioJsonBlock(const ColourSpace::ColourSourceProfile& Profile);

    // 📝 CSS linear-gradient(...)
    [[nodiscard]] static bool ImportCssGradientText(
        std::string_view                  CssText,
        ColourSpace::ColourSourceProfile& OutProfile,
        std::string&                      OutDiagnostic);

    [[nodiscard]] static std::string ExportCssGradientText(
        const ColourSpace::ColourSourceProfile& Profile,
        float                                   AngleDegrees = 90.0f);

    // 📝 SVG <linearGradient> / <stop>
    [[nodiscard]] static bool ImportSvgGradientText(
        std::string_view                  SvgText,
        ColourSpace::ColourSourceProfile& OutProfile,
        std::string&                      OutDiagnostic);

    [[nodiscard]] static std::string ExportSvgGradientText(
        const ColourSpace::ColourSourceProfile& Profile,
        std::string_view                        GradientIdentifier = "FrontierRamp");

    // 📝 GIMP .gpl swatches & .ggr gradients
    [[nodiscard]] static bool ImportGimpGplText(
        std::string_view             GplText,
        ColourSpace::ColourDocument& OutDocument,
        std::string&                 OutDiagnostic);

    [[nodiscard]] static std::string ExportGimpGplText(const ColourSpace::ColourDocument& Document);

    [[nodiscard]] static bool ImportGimpGgrText(
        std::string_view                  GgrText,
        ColourSpace::AuthoredColourRamp&  OutRamp,
        std::string&                      OutDiagnostic);

    [[nodiscard]] static std::string ExportGimpGgrText(const ColourSpace::AuthoredColourRamp& Ramp);

    // 📝 Adobe .ase (Swatch Exchange) & .aco binary swatches
    [[nodiscard]] static bool ImportAdobeAseBytes(
        std::span<const std::byte>   Bytes,
        ColourSpace::ColourDocument& OutDocument,
        std::string&                 OutDiagnostic);

    [[nodiscard]] static std::vector<std::byte> ExportAdobeAseBytes(const ColourSpace::ColourDocument& Document);

    [[nodiscard]] static bool ImportAdobeAcoBytes(
        std::span<const std::byte>   Bytes,
        ColourSpace::ColourDocument& OutDocument,
        std::string&                 OutDiagnostic);

    [[nodiscard]] static std::vector<std::byte> ExportAdobeAcoBytes(const ColourSpace::ColourDocument& Document);

    // 📝 Iridas / Resolve .cube 1D LUT
    [[nodiscard]] static bool ImportCubeLut1DText(
        std::string_view                  CubeText,
        ColourSpace::AuthoredColourRamp&  OutRamp,
        std::string&                      OutDiagnostic);

    [[nodiscard]] static std::string ExportCubeLut1DText(
        const ColourSpace::ColourSourceProfile& Profile,
        std::string_view                        Title     = "FrontierColourRamp",
        uint32_t                                SampleLen = 64u);

    // 📝 Unified file-level import/export dispatch
    [[nodiscard]] static bool ImportFromFile(
        const std::filesystem::path& SourcePath,
        ColourSpace::ColourDocument& OutDocument,
        std::string&                 OutDiagnostic);

    [[nodiscard]] static bool ExportToFile(
        const std::filesystem::path&       TargetPath,
        const ColourSpace::ColourDocument& Document,
        std::string&                       OutDiagnostic);
};

} // namespace Frontier::Interchange
