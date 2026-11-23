/*============================================================================================================================================
                                                    COLOURCODECINTERCHANGEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation for Engine/Codecs/ColourCodec (.ColourSpace/.ColourRamp/inline) & Engine/Interchange/ColourInterchange (HTML/CSS/SVG/GPL/GGR/ASE/ACO/CUBE)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ColourCodec.h"
#include "Engine/ColourSpace/ChromaticitySpace.h"
#include "Engine/ColourSpace/ColourDocument.h"
#include "Engine/ColourSpace/ColourPresetRepository.h"
#include "Engine/ColourSpace/ColourRampSampler.h"
#include "Engine/ColourSpace/SpectralDistribution.h"
#include "Engine/Interchange/ColourInterchange.h"
#include "Engine/Workspaces/Revision/RevisionTracker.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace
{

using namespace Frontier::Codecs;
using namespace Frontier::ColourSpace;
using Frontier::Interchange::ColourInterchange;
using Frontier::Interchange::ExternalColourFormat;
using Frontier::Workspaces::RevisionTracker;

bool VerifyCheck(bool Condition, const char* Message) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[ColourCodecInterchangeValidation] FAILED: %s\n", Message);
        return false;
    }
    return true;
}

bool Near(float A, float B, float Tol = 5.0e-3f) noexcept
{
    return std::fabs(A - B) <= Tol;
}

bool ValidateColourSpaceTwoTierCodec(const std::filesystem::path& TempDir)
{
    ColourDocument SourceDoc {};
    SourceDoc.DocumentTitle        = "FrontierStudioMasterColours";
    SourceDoc.WorkingPrimaries     = ChromaticityPrimaries::AcesCg;
    SourceDoc.WorkingTransferCurve = TransferCurveCategory::Linear;
    SourceDoc.WhitePointKelvin     = 6504.0f;
    SourceDoc.PopulateDefaultStudioRamps();
    SourceDoc.UpsertSwatch({ "FrontierCobalt", "Studio", ConvertHexSrgbToLinear("#2454FF", 1.0f), false });
    SourceDoc.UpsertSwatch({ "EmberAmber",     "Thermal", ConvertHexSrgbToLinear("#FF8822", 1.0f), false });
    SourceDoc.SpectralDistribution = ConstructBlackbodySpectrum(5500.0f);

    // Add one entry of every remaining ColourSourceCategory to test complete 7-variant persistence
    {
        AuthoredColourRamp SolidR { "solid_ice", "Solid Ice", "Test", {} };
        SolidR.Profile.Source = SolidPigmentSource { { 0.45f, 0.82f, 1.0f, 0.9f } };
        SourceDoc.UpsertRamp(SolidR);

        AuthoredColourRamp FillR { "fill_fire", "Fire Fill", "Test", {} };
        FillR.Profile.Source = LinearFillSource { { 1.0f, 0.9f, 0.3f, 1.0f }, { 0.7f, 0.1f, 0.0f, 0.0f }, { 1.0f, 0.4f, 0.05f, 0.8f }, true };
        SourceDoc.UpsertRamp(FillR);

        AuthoredColourRamp QuantR { "quant_bands", "Quantized Bands", "Test", {} };
        QuantR.Profile.Source = QuantizedRampSource { SourceDoc.Ramps.front().Profile.Source.index() == 2u
            ? std::get<MultiStopGradientSource>(SourceDoc.Ramps.front().Profile.Source).Stops
            : std::vector<ColourStop>{} };
        SourceDoc.UpsertRamp(QuantR);

        AuthoredColourRamp ThermalR { "kelvin_arc", "Kelvin Arc", "Test", {} };
        ThermalR.Profile.Source = BlackbodyThermalSource { 1100.0f, 7200.0f, 1.4f, 0.05f };
        SourceDoc.UpsertRamp(ThermalR);

        AuthoredColourRamp DiscreteR { "chem_species", "Chemistry Species", "Test", {} };
        DiscreteR.Profile.Source = DiscreteTintSetSource {};
        SourceDoc.UpsertRamp(DiscreteR);

        AuthoredColourRamp NoiseR { "marble_noise", "Marble Noise", "Test", {} };
        NoiseR.Profile.Source = SpatialNoiseFillSource { 1.25f, 0.35f, ExtractOrderedStops(SourceDoc.Ramps.front().Profile.Source) };
        SourceDoc.UpsertRamp(NoiseR);
    }
    SourceDoc.RebuildBakedLuts64();

    RevisionTracker Revision {};
    std::string Diagnostic {};
    const std::filesystem::path ContainerPath = TempDir / "MasterPalette.ColourSpace";
    if (!VerifyCheck(ColourEncoder::EncodeDocumentToFile(ContainerPath, SourceDoc, Diagnostic, &Revision), "Encode .ColourSpace"))
    {
        std::fprintf(stderr, "  Diagnostic: %s\n", Diagnostic.c_str());
        return false;
    }

    ColourDocument DecodedDoc {};
    RevisionTracker DecodedRevision {};
    if (!VerifyCheck(ColourDecoder::DecodeDocumentFromFile(ContainerPath, DecodedDoc, Diagnostic, &DecodedRevision), "Decode .ColourSpace"))
    {
        std::fprintf(stderr, "  Diagnostic: %s\n", Diagnostic.c_str());
        return false;
    }

    if (!VerifyCheck(DecodedDoc.Swatches.size() == SourceDoc.Swatches.size(), ".ColourSpace Swatch count preserved")
        || !VerifyCheck(DecodedDoc.Ramps.size() == SourceDoc.Ramps.size(), ".ColourSpace Ramp count preserved (all 7 categories)")
        || !VerifyCheck(DecodedDoc.BakedLuts64.size() == SourceDoc.BakedLuts64.size(), ".ColourSpace .ColourLut binary section preserved")
        || !VerifyCheck(DecodedDoc.SpectralDistribution.RadiantPower.size() == SourceDoc.SpectralDistribution.RadiantPower.size(), ".ColourSpace .Spectrum binary section preserved"))
    {
        return false;
    }

    const AuthoredColourRamp* DecodedThermal = DecodedDoc.FindRamp("kelvin_arc");
    if (!VerifyCheck(DecodedThermal != nullptr && std::holds_alternative<BlackbodyThermalSource>(DecodedThermal->Profile.Source), "BlackbodyThermalSource survived round-trip"))
    {
        return false;
    }

    // Standalone .ColourRamp round-trip (case-insensitive load, canonical PascalCase write)
    const std::filesystem::path RampPath = TempDir / "AuroraPreset.colourramp";
    const AuthoredColourRamp* Aurora = SourceDoc.FindRamp("aurora");
    if (!VerifyCheck(Aurora != nullptr && ColourEncoder::EncodeStandaloneRampToFile(RampPath, *Aurora, Diagnostic), "Encode standalone .ColourRamp"))
    {
        return false;
    }
    const std::filesystem::path CanonicalRampPath = TempDir / "AuroraPreset.ColourRamp";
    AuthoredColourRamp DecodedRamp {};
    if (!VerifyCheck(std::filesystem::exists(CanonicalRampPath), "Canonical .ColourRamp PascalCase extension written")
        || !VerifyCheck(ColourDecoder::DecodeStandaloneRampFromFile(CanonicalRampPath, DecodedRamp, Diagnostic), "Decode standalone .ColourRamp")
        || !VerifyCheck(DecodedRamp.RampIdentifier == "aurora" && ExtractOrderedStops(DecodedRamp.Profile.Source).size() == 5u, "Standalone .ColourRamp stops exact"))
    {
        return false;
    }

    return true;
}

bool ValidateExternalColourInterchange(const std::filesystem::path& TempDir)
{
    std::string Diagnostic {};
    const ColourRampPresetEntry* Sunset = FindBuiltInColourRampPreset("sunset");
    ColourSourceProfile SourceProfile = ConstructProfileFromRampPreset(*Sunset);
    SourceProfile.Range.WrapBehaviour = ColourWrapBehaviour::Mirror;
    SourceProfile.Range.BlendCurve    = ColourBlendCurve::Smooth;
    SourceProfile.Range.RepeatCount   = 2.0f;

    // 1. HTML Studio JSON round-trip
    const std::string HtmlJson = ColourInterchange::ExportHtmlStudioJsonBlock(SourceProfile);
    ColourSourceProfile ParsedHtml {};
    if (!VerifyCheck(ColourInterchange::ImportHtmlStudioJsonBlock(HtmlJson, ParsedHtml, Diagnostic), "Import HTML Studio JSON colour block")
        || !VerifyCheck(ParsedHtml.Range.WrapBehaviour == ColourWrapBehaviour::Mirror && ParsedHtml.Range.BlendCurve == ColourBlendCurve::Smooth, "HTML wrap & blend curve preserved")
        || !VerifyCheck(ExtractOrderedStops(ParsedHtml.Source).size() == 6u, "HTML 6 stops preserved"))
    {
        return false;
    }

    // Legacy HTML colA / colB / colC fallback
    const char* LegacyJson = R"({ "colA": [1.0, 0.5, 0.1, 1.0], "colB": [0.2, 0.05, 0.0, 0.0], "blend": "alpha" })";
    ColourSourceProfile LegacyProfile {};
    if (!VerifyCheck(ColourInterchange::ImportHtmlStudioJsonBlock(LegacyJson, LegacyProfile, Diagnostic), "Import legacy colA/colB JSON")
        || !VerifyCheck(std::holds_alternative<LinearFillSource>(LegacyProfile.Source) && LegacyProfile.BlendEquation == OpticalBlendEquation::PremultipliedAlpha, "Legacy colA/colB mapped to LinearFillSource + alpha"))
    {
        return false;
    }

    // 2. CSS linear-gradient round-trip
    const std::string CssOut = ColourInterchange::ExportCssGradientText(SourceProfile, 90.0f);
    ColourSourceProfile ParsedCss {};
    if (!VerifyCheck(ColourInterchange::ImportCssGradientText(CssOut, ParsedCss, Diagnostic), "Import CSS linear-gradient")
        || !VerifyCheck(ExtractOrderedStops(ParsedCss.Source).size() == 6u, "CSS 6 stops preserved"))
    {
        return false;
    }

    // 3. SVG <linearGradient> round-trip
    const std::string SvgOut = ColourInterchange::ExportSvgGradientText(SourceProfile, "SunsetNeon");
    ColourSourceProfile ParsedSvg {};
    if (!VerifyCheck(ColourInterchange::ImportSvgGradientText(SvgOut, ParsedSvg, Diagnostic), "Import SVG <linearGradient>")
        || !VerifyCheck(ExtractOrderedStops(ParsedSvg.Source).size() == 6u, "SVG 6 stops preserved"))
    {
        return false;
    }

    // 4. GIMP .gpl swatches & .ggr gradients
    ColourDocument SwatchDoc {};
    SwatchDoc.DocumentTitle = "StudioSwatches";
    SwatchDoc.UpsertSwatch({ "Cobalt", "Studio", ConvertHexSrgbToLinear("#2454FF", 1.0f, TransferCurveCategory::PiecewiseSrgb), false });
    SwatchDoc.UpsertSwatch({ "Solar",  "Studio", ConvertHexSrgbToLinear("#FFB800", 1.0f, TransferCurveCategory::PiecewiseSrgb), false });
    const std::string GplText = ColourInterchange::ExportGimpGplText(SwatchDoc);
    ColourDocument ParsedGpl {};
    if (!VerifyCheck(ColourInterchange::ImportGimpGplText(GplText, ParsedGpl, Diagnostic), "Import GIMP .gpl")
        || !VerifyCheck(ParsedGpl.Swatches.size() == 2u && ParsedGpl.Swatches[0].SwatchName == "Cobalt", "GIMP .gpl swatches exact"))
    {
        return false;
    }

    AuthoredColourRamp SunsetRamp { "sunset", "Sunset Neon", "Studio", SourceProfile };
    const std::string GgrText = ColourInterchange::ExportGimpGgrText(SunsetRamp);
    AuthoredColourRamp ParsedGgr {};
    if (!VerifyCheck(ColourInterchange::ImportGimpGgrText(GgrText, ParsedGgr, Diagnostic), "Import GIMP .ggr")
        || !VerifyCheck(ExtractOrderedStops(ParsedGgr.Profile.Source).size() == 6u, "GIMP .ggr 6 stops exact"))
    {
        return false;
    }

    // 5. Adobe .ase & .aco binary containers
    const auto AseBytes = ColourInterchange::ExportAdobeAseBytes(SwatchDoc);
    ColourDocument ParsedAse {};
    if (!VerifyCheck(ColourInterchange::ImportAdobeAseBytes(AseBytes, ParsedAse, Diagnostic), "Import Adobe .ase")
        || !VerifyCheck(ParsedAse.Swatches.size() == 2u && ParsedAse.Swatches[1].SwatchName == "Solar", "Adobe .ase swatches exact"))
    {
        return false;
    }

    const auto AcoBytes = ColourInterchange::ExportAdobeAcoBytes(SwatchDoc);
    ColourDocument ParsedAco {};
    if (!VerifyCheck(ColourInterchange::ImportAdobeAcoBytes(AcoBytes, ParsedAco, Diagnostic), "Import Adobe .aco")
        || !VerifyCheck(ParsedAco.Swatches.size() == 2u && Near(ParsedAco.Swatches[0].LinearTint.Blue, SwatchDoc.Swatches[0].LinearTint.Blue, 0.01f), "Adobe .aco swatches exact"))
    {
        return false;
    }

    // 6. Iridas / Resolve .cube 1D LUT file-level dispatch
    SwatchDoc.UpsertRamp(SunsetRamp);
    const std::filesystem::path CubePath = TempDir / "Sunset64.cube";
    if (!VerifyCheck(ColourInterchange::DetectFormatFromPath(CubePath) == ExternalColourFormat::CubeLut1D, "DetectFormatFromPath(.cube)")
        || !VerifyCheck(ColourInterchange::ExportToFile(CubePath, SwatchDoc, Diagnostic), "ExportToFile(.cube)"))
    {
        return false;
    }
    ColourDocument ImportedCube {};
    if (!VerifyCheck(ColourInterchange::ImportFromFile(CubePath, ImportedCube, Diagnostic), "ImportFromFile(.cube)")
        || !VerifyCheck(ImportedCube.Ramps.size() == 1u && ExtractOrderedStops(ImportedCube.Ramps[0].Profile.Source).size() == 64u, ".cube 64-entry 1D LUT exact"))
    {
        return false;
    }

    return true;
}

} // namespace

int main()
{
    const std::filesystem::path TempDir = std::filesystem::temp_directory_path() / "FrontierColourCodecValidation";
    std::error_code Err {};
    std::filesystem::remove_all(TempDir, Err);
    std::filesystem::create_directories(TempDir, Err);

    const bool OkCodec       = ValidateColourSpaceTwoTierCodec(TempDir);
    const bool OkInterchange = OkCodec && ValidateExternalColourInterchange(TempDir);
    std::filesystem::remove_all(TempDir, Err);

    if (!OkCodec || !OkInterchange)
    {
        return 1;
    }
    std::printf("[ColourCodecInterchangeValidation] ALL CHECKS PASSED (.ColourSpace, .ColourRamp, HTML/CSS/SVG/GPL/GGR/ASE/ACO/CUBE).\n");
    return 0;
}
