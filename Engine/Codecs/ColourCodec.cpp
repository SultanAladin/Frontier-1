/*============================================================================================================================================
                                                               COLOURCODEC.CPP
============================================================================================================================================*/
// 🧩 .ColourSpace, .ColourRamp & .ColourSwatch encoder/decoder implementation with inline manifest helpers for sibling codecs

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ColourCodec.h"

#include "Engine/ColourSpace/ChromaticitySpace.h"
#include "Engine/ColourSpace/ColourRampSampler.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

namespace Frontier::Codecs
{

namespace
{

[[nodiscard]] toml::array PackRgbaArray(ColourSpace::LinearRgba Tint)
{
    toml::array Arr;
    Arr.push_back(static_cast<double>(Tint.Red));
    Arr.push_back(static_cast<double>(Tint.Green));
    Arr.push_back(static_cast<double>(Tint.Blue));
    Arr.push_back(static_cast<double>(Tint.Alpha));
    return Arr;
}

[[nodiscard]] ColourSpace::LinearRgba ExtractRgbaArray(const toml::array* Arr, ColourSpace::LinearRgba Fallback = { 1.0f, 1.0f, 1.0f, 1.0f }) noexcept
{
    if (Arr == nullptr || Arr->size() < 3u)
    {
        return Fallback;
    }
    return {
        static_cast<float>((*Arr)[0].value_or(static_cast<double>(Fallback.Red))),
        static_cast<float>((*Arr)[1].value_or(static_cast<double>(Fallback.Green))),
        static_cast<float>((*Arr)[2].value_or(static_cast<double>(Fallback.Blue))),
        (Arr->size() >= 4u) ? static_cast<float>((*Arr)[3].value_or(static_cast<double>(Fallback.Alpha))) : Fallback.Alpha,
    };
}

[[nodiscard]] toml::array PackStopsArray(const std::vector<ColourSpace::ColourStop>& Stops)
{
    toml::array Out;
    for (const ColourSpace::ColourStop& Stop : Stops)
    {
        toml::array Row;
        Row.push_back(static_cast<double>(Stop.Position01));
        Row.push_back(static_cast<double>(Stop.LinearTint.Red));
        Row.push_back(static_cast<double>(Stop.LinearTint.Green));
        Row.push_back(static_cast<double>(Stop.LinearTint.Blue));
        Row.push_back(static_cast<double>(Stop.LinearTint.Alpha));
        Out.push_back(std::move(Row));
    }
    return Out;
}

[[nodiscard]] std::vector<ColourSpace::ColourStop> ExtractStopsArray(const toml::array* Arr)
{
    std::vector<ColourSpace::ColourStop> Stops {};
    if (Arr == nullptr) return Stops;
    Stops.reserve(Arr->size());
    for (const auto& Entry : *Arr)
    {
        if (const toml::array* Row = Entry.as_array(); Row != nullptr && Row->size() >= 4u)
        {
            ColourSpace::ColourStop Stop {};
            Stop.Position01       = static_cast<float>((*Row)[0].value_or(0.0));
            Stop.LinearTint.Red   = static_cast<float>((*Row)[1].value_or(1.0));
            Stop.LinearTint.Green = static_cast<float>((*Row)[2].value_or(1.0));
            Stop.LinearTint.Blue  = static_cast<float>((*Row)[3].value_or(1.0));
            Stop.LinearTint.Alpha = (Row->size() >= 5u) ? static_cast<float>((*Row)[4].value_or(1.0)) : 1.0f;
            Stops.push_back(Stop);
        }
    }
    return Stops;
}

[[nodiscard]] std::string LowercaseAscii(std::string_view Text)
{
    std::string Out;
    Out.reserve(Text.size());
    for (char Ch : Text)
    {
        Out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(Ch))));
    }
    return Out;
}

[[nodiscard]] std::string_view QueryTransferToken(ColourSpace::TransferCurveCategory Curve) noexcept
{
    switch (Curve)
    {
        case ColourSpace::TransferCurveCategory::PiecewiseSrgb:  return "PiecewiseSrgb";
        case ColourSpace::TransferCurveCategory::PureGamma22:    return "PureGamma22";
        case ColourSpace::TransferCurveCategory::PerceptualPq:   return "PerceptualPq";
        case ColourSpace::TransferCurveCategory::HybridLogGamma: return "HybridLogGamma";
        default:                                                 return "Linear";
    }
}

[[nodiscard]] ColourSpace::TransferCurveCategory ParseTransferToken(std::string_view Token) noexcept
{
    if (Token == "PiecewiseSrgb")  return ColourSpace::TransferCurveCategory::PiecewiseSrgb;
    if (Token == "PureGamma22")    return ColourSpace::TransferCurveCategory::PureGamma22;
    if (Token == "PerceptualPq")   return ColourSpace::TransferCurveCategory::PerceptualPq;
    if (Token == "HybridLogGamma") return ColourSpace::TransferCurveCategory::HybridLogGamma;
    return ColourSpace::TransferCurveCategory::Linear;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           COLOUR DOCUMENT MODEL CONVERSION
//------------------------------------------------------------------------------------------------------------------------

ColourSpaceDocumentModel ColourSpaceDocumentModel::FromColourDocument(const ColourSpace::ColourDocument& Document)
{
    ColourSpaceDocumentModel Model {};
    Model.DocumentTitle             = Document.DocumentTitle;
    Model.ReferencePrimaries        = std::string(ColourSpace::QueryPrimariesToken(Document.WorkingPrimaries));
    Model.TransferCurve             = std::string(QueryTransferToken(Document.WorkingTransferCurve));
    Model.WhitePointKelvin          = static_cast<double>(Document.WhitePointKelvin);
    Model.Ramps                     = Document.Ramps;
    Model.SpectralSamplesNanometer  = Document.SpectralDistribution.RadiantPower;
    Model.BakedLuts64               = Document.BakedLuts64;

    Model.Swatches.reserve(Document.Swatches.size());
    for (const ColourSpace::NamedColourSwatchEntry& Swatch : Document.Swatches)
    {
        Model.Swatches.push_back({
            Swatch.SwatchName,
            static_cast<double>(Swatch.LinearTint.Red),
            static_cast<double>(Swatch.LinearTint.Green),
            static_cast<double>(Swatch.LinearTint.Blue),
            static_cast<double>(Swatch.LinearTint.Alpha),
            Swatch.SeriesTitle,
        });
    }
    return Model;
}

ColourSpace::ColourDocument ColourSpaceDocumentModel::ToColourDocument() const
{
    ColourSpace::ColourDocument Document {};
    Document.DocumentTitle                    = DocumentTitle;
    Document.WorkingPrimaries                 = ColourSpace::ParsePrimariesToken(ReferencePrimaries);
    Document.WorkingTransferCurve             = ParseTransferToken(TransferCurve);
    Document.WhitePointKelvin                 = static_cast<float>(WhitePointKelvin);
    Document.Ramps                            = Ramps;
    Document.SpectralDistribution.RadiantPower= SpectralSamplesNanometer;
    Document.BakedLuts64                      = BakedLuts64;

    Document.Swatches.reserve(Swatches.size());
    for (const RadiometricSwatchEntry& Swatch : Swatches)
    {
        ColourSpace::NamedColourSwatchEntry Entry {};
        Entry.SwatchName  = Swatch.SwatchName;
        Entry.SeriesTitle = Swatch.SeriesTitle;
        Entry.LinearTint  = {
            static_cast<float>(Swatch.LinearRed),
            static_cast<float>(Swatch.LinearGreen),
            static_cast<float>(Swatch.LinearBlue),
            static_cast<float>(Swatch.Opacity),
        };
        Document.Swatches.push_back(std::move(Entry));
    }
    return Document;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      INLINE MODULAR COLOUR SOURCE MANIFEST HELPERS
//------------------------------------------------------------------------------------------------------------------------

toml::table ColourEncoder::SerializeSourceToManifest(const ColourSpace::ColourSourceProfile& Profile)
{
    toml::table Manifest;
    const auto Category = ColourSpace::QueryColourSourceCategory(Profile.Source);
    Manifest.insert_or_assign("SourceCategory",     std::string(ColourSpace::QueryColourSourceCategoryToken(Category)));
    Manifest.insert_or_assign("RangeMin",           static_cast<double>(Profile.Range.RangeMin));
    Manifest.insert_or_assign("RangeMax",           static_cast<double>(Profile.Range.RangeMax));
    Manifest.insert_or_assign("RepeatCount",        static_cast<double>(Profile.Range.RepeatCount));
    Manifest.insert_or_assign("PhaseOffset",        static_cast<double>(Profile.Range.PhaseOffset));
    Manifest.insert_or_assign("WrapBehaviour",      std::string(ColourSpace::QueryColourWrapToken(Profile.Range.WrapBehaviour)));
    Manifest.insert_or_assign("BlendCurve",         std::string(ColourSpace::QueryColourBlendCurveToken(Profile.Range.BlendCurve)));
    Manifest.insert_or_assign("InterpolationSpace", std::string(ColourSpace::QueryInterpolationSpaceToken(Profile.Range.InterpolationSpace)));
    Manifest.insert_or_assign("OpticalBlend",       std::string(ColourSpace::QueryOpticalBlendToken(Profile.BlendEquation)));

    if (const auto* Solid = std::get_if<ColourSpace::SolidPigmentSource>(&Profile.Source))
    {
        Manifest.insert_or_assign("LinearTint", PackRgbaArray(Solid->LinearTint));
    }
    else if (const auto* Fill = std::get_if<ColourSpace::LinearFillSource>(&Profile.Source))
    {
        Manifest.insert_or_assign("PrimaryLinear",   PackRgbaArray(Fill->PrimaryLinear));
        Manifest.insert_or_assign("SecondaryLinear", PackRgbaArray(Fill->SecondaryLinear));
        Manifest.insert_or_assign("MidpointLinear",  PackRgbaArray(Fill->MidpointLinear));
        Manifest.insert_or_assign("MidpointEnabled", Fill->MidpointEnabled);
    }
    else if (const auto* Gradient = std::get_if<ColourSpace::MultiStopGradientSource>(&Profile.Source))
    {
        if (!Gradient->LinkedPresetIdentifier.empty())
        {
            Manifest.insert_or_assign("LinkedPresetIdentifier", Gradient->LinkedPresetIdentifier);
        }
        Manifest.insert_or_assign("Stops", PackStopsArray(Gradient->Stops));
    }
    else if (const auto* Quant = std::get_if<ColourSpace::QuantizedRampSource>(&Profile.Source))
    {
        Manifest.insert_or_assign("Stops", PackStopsArray(Quant->BandStops));
    }
    else if (const auto* Thermal = std::get_if<ColourSpace::BlackbodyThermalSource>(&Profile.Source))
    {
        Manifest.insert_or_assign("ColdKelvin",    static_cast<double>(Thermal->ColdKelvin));
        Manifest.insert_or_assign("HotKelvin",     static_cast<double>(Thermal->HotKelvin));
        Manifest.insert_or_assign("IntensityGain", static_cast<double>(Thermal->IntensityGain));
        Manifest.insert_or_assign("TailOpacity",   static_cast<double>(Thermal->TailOpacity));
    }
    else if (const auto* Discrete = std::get_if<ColourSpace::DiscreteTintSetSource>(&Profile.Source))
    {
        Manifest.insert_or_assign("ActiveCount", static_cast<int64_t>(Discrete->ActiveCount));
        toml::array TintArray;
        const uint32_t Limit = std::min(Discrete->ActiveCount, static_cast<uint32_t>(Discrete->Entries.size()));
        for (uint32_t I = 0u; I < Limit; ++I)
        {
            TintArray.push_back(PackRgbaArray(Discrete->Entries[I]));
        }
        Manifest.insert_or_assign("DiscreteTints", std::move(TintArray));
    }
    else if (const auto* Noise = std::get_if<ColourSpace::SpatialNoiseFillSource>(&Profile.Source))
    {
        Manifest.insert_or_assign("NoiseFrequency", static_cast<double>(Noise->NoiseFrequency));
        Manifest.insert_or_assign("EvolutionRate",  static_cast<double>(Noise->EvolutionRate));
        Manifest.insert_or_assign("Stops",          PackStopsArray(Noise->Stops));
    }

    return Manifest;
}

bool ColourDecoder::ExtractSourceFromManifest(
    const toml::table&                 SourceManifest,
    ColourSpace::ColourSourceProfile&  OutProfile,
    std::string&                       OutDiagnostic)
{
    OutProfile = ColourSpace::ColourSourceProfile {};
    const std::string CategoryToken = SourceManifest["SourceCategory"].value_or(std::string{ "LinearFill" });
    const auto Category             = ColourSpace::ParseColourSourceCategoryToken(CategoryToken);

    OutProfile.Range.RangeMin           = static_cast<float>(SourceManifest["RangeMin"].value_or(0.0));
    OutProfile.Range.RangeMax           = static_cast<float>(SourceManifest["RangeMax"].value_or(1.0));
    OutProfile.Range.RepeatCount        = static_cast<float>(SourceManifest["RepeatCount"].value_or(1.0));
    OutProfile.Range.PhaseOffset        = static_cast<float>(SourceManifest["PhaseOffset"].value_or(0.0));
    OutProfile.Range.WrapBehaviour      = ColourSpace::ParseColourWrapToken(SourceManifest["WrapBehaviour"].value_or(std::string{ "clamp" }));
    OutProfile.Range.BlendCurve         = ColourSpace::ParseColourBlendCurveToken(SourceManifest["BlendCurve"].value_or(std::string{ "linear" }));
    OutProfile.Range.InterpolationSpace = ColourSpace::ParseInterpolationSpaceToken(SourceManifest["InterpolationSpace"].value_or(std::string{ "LinearSrgb" }));
    OutProfile.BlendEquation            = ColourSpace::ParseOpticalBlendToken(SourceManifest["OpticalBlend"].value_or(std::string{ "additive" }));

    switch (Category)
    {
        case ColourSpace::ColourSourceCategory::SolidPigment:
        {
            ColourSpace::SolidPigmentSource Solid {};
            Solid.LinearTint  = ExtractRgbaArray(SourceManifest["LinearTint"].as_array());
            OutProfile.Source = Solid;
            break;
        }
        case ColourSpace::ColourSourceCategory::LinearFill:
        {
            ColourSpace::LinearFillSource Fill {};
            Fill.PrimaryLinear   = ExtractRgbaArray(SourceManifest["PrimaryLinear"].as_array(),   { 1.0f, 1.0f, 1.0f, 1.0f });
            Fill.SecondaryLinear = ExtractRgbaArray(SourceManifest["SecondaryLinear"].as_array(), { 1.0f, 1.0f, 1.0f, 0.0f });
            Fill.MidpointLinear  = ExtractRgbaArray(SourceManifest["MidpointLinear"].as_array(),  { 1.0f, 1.0f, 0.3f, 0.0f });
            Fill.MidpointEnabled = SourceManifest["MidpointEnabled"].value_or(false);
            OutProfile.Source    = Fill;
            break;
        }
        case ColourSpace::ColourSourceCategory::MultiStopGradient:
        {
            ColourSpace::MultiStopGradientSource Gradient {};
            Gradient.LinkedPresetIdentifier = SourceManifest["LinkedPresetIdentifier"].value_or(std::string{});
            Gradient.Stops                  = ExtractStopsArray(SourceManifest["Stops"].as_array());
            OutProfile.Source               = std::move(Gradient);
            break;
        }
        case ColourSpace::ColourSourceCategory::QuantizedRamp:
        {
            ColourSpace::QuantizedRampSource Quant {};
            Quant.BandStops   = ExtractStopsArray(SourceManifest["Stops"].as_array());
            OutProfile.Source = std::move(Quant);
            break;
        }
        case ColourSpace::ColourSourceCategory::BlackbodyThermal:
        {
            ColourSpace::BlackbodyThermalSource Thermal {};
            Thermal.ColdKelvin    = static_cast<float>(SourceManifest["ColdKelvin"].value_or(900.0));
            Thermal.HotKelvin     = static_cast<float>(SourceManifest["HotKelvin"].value_or(4600.0));
            Thermal.IntensityGain = static_cast<float>(SourceManifest["IntensityGain"].value_or(1.0));
            Thermal.TailOpacity   = static_cast<float>(SourceManifest["TailOpacity"].value_or(0.0));
            OutProfile.Source     = Thermal;
            break;
        }
        case ColourSpace::ColourSourceCategory::DiscreteTintSet:
        {
            ColourSpace::DiscreteTintSetSource Discrete {};
            Discrete.ActiveCount = static_cast<uint32_t>(std::clamp<int64_t>(SourceManifest["ActiveCount"].value_or(4), 1, 8));
            if (const toml::array* Arr = SourceManifest["DiscreteTints"].as_array())
            {
                const std::size_t Limit = std::min(Arr->size(), Discrete.Entries.size());
                for (std::size_t I = 0u; I < Limit; ++I)
                {
                    Discrete.Entries[I] = ExtractRgbaArray((*Arr)[I].as_array(), Discrete.Entries[I]);
                }
            }
            OutProfile.Source = Discrete;
            break;
        }
        case ColourSpace::ColourSourceCategory::SpatialNoiseFill:
        {
            ColourSpace::SpatialNoiseFillSource Noise {};
            Noise.NoiseFrequency = static_cast<float>(SourceManifest["NoiseFrequency"].value_or(0.85));
            Noise.EvolutionRate  = static_cast<float>(SourceManifest["EvolutionRate"].value_or(0.18));
            Noise.Stops          = ExtractStopsArray(SourceManifest["Stops"].as_array());
            OutProfile.Source    = std::move(Noise);
            break;
        }
        default:
        {
            OutDiagnostic = "Unrecognized ColourSourceCategory token in manifest.";
            return false;
        }
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           .COLOURSPACE CONTAINER CODEC
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload ColourEncoder::BuildPayload(
    const ColourSpaceDocumentModel&    Model,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload {};
    Payload.SuffixCategory  = SpaceSuffixCategory::ColourSpace;
    Payload.DocumentTitle      = Model.DocumentTitle;
    Payload.AuthoringWorkspace = "Frontier.ColourWorkspace";

    toml::table RampMeta;
    RampMeta.insert_or_assign("ReferencePrimaries",        Model.ReferencePrimaries);
    RampMeta.insert_or_assign("TransferCurve",             Model.TransferCurve);
    RampMeta.insert_or_assign("DefaultInterpolationSpace", Model.DefaultInterpolationSpace);
    RampMeta.insert_or_assign("WhitePointKelvin",          Model.WhitePointKelvin);
    Payload.DomainManifest.insert_or_assign(".Ramp", std::move(RampMeta));

    if (!Model.Swatches.empty())
    {
        toml::array SwatchArray;
        for (const RadiometricSwatchEntry& Swatch : Model.Swatches)
        {
            toml::table Entry;
            Entry.insert_or_assign("SwatchName",  Swatch.SwatchName);
            Entry.insert_or_assign("LinearRed",   Swatch.LinearRed);
            Entry.insert_or_assign("LinearGreen", Swatch.LinearGreen);
            Entry.insert_or_assign("LinearBlue",  Swatch.LinearBlue);
            Entry.insert_or_assign("Opacity",     Swatch.Opacity);
            if (!Swatch.SeriesTitle.empty())
            {
                Entry.insert_or_assign("SeriesTitle", Swatch.SeriesTitle);
            }
            SwatchArray.push_back(std::move(Entry));
        }
        Payload.DomainManifest.insert_or_assign(".Swatch", std::move(SwatchArray));
    }

    if (!Model.Ramps.empty())
    {
        toml::array RampArray;
        for (const ColourSpace::AuthoredColourRamp& Ramp : Model.Ramps)
        {
            toml::table Entry = SerializeSourceToManifest(Ramp.Profile);
            Entry.insert_or_assign("RampIdentifier", Ramp.RampIdentifier);
            Entry.insert_or_assign("DisplayTitle",   Ramp.DisplayTitle);
            Entry.insert_or_assign("SeriesTitle",    Ramp.SeriesTitle);
            RampArray.push_back(std::move(Entry));
        }
        Payload.DomainManifest.insert_or_assign(".ColourRamp", std::move(RampArray));
    }

    if (!Model.SpectralSamplesNanometer.empty())
    {
        BinarySectionBlock SpectrumSection {};
        SpectrumSection.SectionIdentifier = "SpectralDistribution";
        SpectrumSection.SubFormatTag      = ".Spectrum";
        const std::size_t ByteCount       = Model.SpectralSamplesNanometer.size() * sizeof(float);
        SpectrumSection.PayloadBytes.resize(ByteCount);
        std::memcpy(SpectrumSection.PayloadBytes.data(), Model.SpectralSamplesNanometer.data(), ByteCount);
        Payload.BinarySections.push_back(std::move(SpectrumSection));
    }

    if (!Model.BakedLuts64.empty())
    {
        BinarySectionBlock LutSection {};
        LutSection.SectionIdentifier = "BakedRampLuts64";
        LutSection.SubFormatTag      = ".ColourLut";
        const std::size_t ByteCount  = Model.BakedLuts64.size() * sizeof(ColourSpace::ColourRampLut64);
        LutSection.PayloadBytes.resize(ByteCount);
        std::memcpy(LutSection.PayloadBytes.data(), Model.BakedLuts64.data(), ByteCount);
        Payload.BinarySections.push_back(std::move(LutSection));
    }

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool ColourEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const ColourSpaceDocumentModel&    Model,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToFile(TargetPath, BuildPayload(Model, OptionalRevision), OutDiagnostic);
}

bool ColourEncoder::EncodeDocumentToFile(
    const std::filesystem::path&       TargetPath,
    const ColourSpace::ColourDocument& Document,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return EncodeToFile(TargetPath, ColourSpaceDocumentModel::FromColourDocument(Document), OutDiagnostic, OptionalRevision);
}

std::string ColourEncoder::EncodeStandaloneRampToToml(const ColourSpace::AuthoredColourRamp& Ramp)
{
    toml::table Root;
    Root.insert_or_assign("Format",        std::string{ "Frontier.ColourRamp" });
    Root.insert_or_assign("SchemaVersion", 1);

    toml::table Body = SerializeSourceToManifest(Ramp.Profile);
    Body.insert_or_assign("RampIdentifier", Ramp.RampIdentifier);
    Body.insert_or_assign("DisplayTitle",   Ramp.DisplayTitle);
    Body.insert_or_assign("SeriesTitle",    Ramp.SeriesTitle);
    Root.insert_or_assign("ColourRamp",     std::move(Body));

    std::ostringstream Stream;
    Stream << Root << '\n';
    return Stream.str();
}

bool ColourEncoder::EncodeStandaloneRampToFile(
    const std::filesystem::path&           TargetPath,
    const ColourSpace::AuthoredColourRamp& Ramp,
    std::string&                           OutDiagnostic)
{
    std::filesystem::path CanonicalPath = TargetPath;
    if (!ColourDecoder::MatchesRampExtension(CanonicalPath))
    {
        CanonicalPath.replace_extension(CanonicalRampSuffix);
    }
    else
    {
        CanonicalPath.replace_extension(CanonicalRampSuffix);
    }

    std::error_code FsError;
    if (CanonicalPath.has_parent_path())
    {
        std::filesystem::create_directories(CanonicalPath.parent_path(), FsError);
    }

    std::ofstream File(CanonicalPath, std::ios::binary | std::ios::trunc);
    if (!File.is_open())
    {
        OutDiagnostic = "Failed to open .ColourRamp destination path for writing.";
        return false;
    }
    const std::string Serialized = EncodeStandaloneRampToToml(Ramp);
    File.write(Serialized.data(), static_cast<std::streamsize>(Serialized.size()));
    if (!File.good())
    {
        OutDiagnostic = "Failed while writing .ColourRamp preset payload.";
        return false;
    }
    return true;
}

bool ColourDecoder::MatchesRampExtension(const std::filesystem::path& FilePath) noexcept
{
    return LowercaseAscii(FilePath.extension().string()) == ".colourramp";
}

bool ColourDecoder::MatchesSwatchSuffix(const std::filesystem::path& FilePath) noexcept
{
    return LowercaseAscii(FilePath.extension().string()) == ".colourswatch";
}

bool ColourDecoder::ExtractModel(
    const SpaceContainerPayload& Payload,
    ColourSpaceDocumentModel&    OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.SuffixCategory != SpaceSuffixCategory::ColourSpace)
    {
        OutDiagnostic = "Expected .ColourSpace container category.";
        return false;
    }

    OutModel               = ColourSpaceDocumentModel {};
    OutModel.DocumentTitle = Payload.DocumentTitle;

    if (const toml::table* RampMeta = Payload.DomainManifest[".Ramp"].as_table())
    {
        OutModel.ReferencePrimaries        = (*RampMeta)["ReferencePrimaries"].value_or(std::string{ "ACEScg" });
        OutModel.TransferCurve             = (*RampMeta)["TransferCurve"].value_or(std::string{ "Linear" });
        OutModel.DefaultInterpolationSpace = (*RampMeta)["DefaultInterpolationSpace"].value_or(std::string{ "LinearSrgb" });
        OutModel.WhitePointKelvin          = (*RampMeta)["WhitePointKelvin"].value_or(6504.0);
    }

    if (const toml::array* SwatchArray = Payload.DomainManifest[".Swatch"].as_array())
    {
        for (const auto& SwatchRef : *SwatchArray)
        {
            if (const toml::table* Entry = SwatchRef.as_table())
            {
                RadiometricSwatchEntry Swatch {};
                Swatch.SwatchName  = (*Entry)["SwatchName"].value_or(std::string{});
                Swatch.LinearRed   = (*Entry)["LinearRed"].value_or(0.0);
                Swatch.LinearGreen = (*Entry)["LinearGreen"].value_or(0.0);
                Swatch.LinearBlue  = (*Entry)["LinearBlue"].value_or(0.0);
                Swatch.Opacity     = (*Entry)["Opacity"].value_or(1.0);
                Swatch.SeriesTitle = (*Entry)["SeriesTitle"].value_or(std::string{ "Custom" });
                OutModel.Swatches.push_back(std::move(Swatch));
            }
        }
    }

    if (const toml::array* RampArray = Payload.DomainManifest[".ColourRamp"].as_array())
    {
        for (const auto& RampRef : *RampArray)
        {
            if (const toml::table* Entry = RampRef.as_table())
            {
                ColourSpace::AuthoredColourRamp Ramp {};
                Ramp.RampIdentifier = (*Entry)["RampIdentifier"].value_or(std::string{});
                Ramp.DisplayTitle   = (*Entry)["DisplayTitle"].value_or(Ramp.RampIdentifier);
                Ramp.SeriesTitle    = (*Entry)["SeriesTitle"].value_or(std::string{ "Custom" });
                if (!ExtractSourceFromManifest(*Entry, Ramp.Profile, OutDiagnostic))
                {
                    return false;
                }
                OutModel.Ramps.push_back(std::move(Ramp));
            }
        }
    }

    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SubFormatTag == ".Spectrum" && (Section.PayloadBytes.size() % sizeof(float)) == 0u)
        {
            const std::size_t SampleCount = Section.PayloadBytes.size() / sizeof(float);
            OutModel.SpectralSamplesNanometer.resize(SampleCount);
            if (SampleCount > 0u)
            {
                std::memcpy(OutModel.SpectralSamplesNanometer.data(), Section.PayloadBytes.data(), Section.PayloadBytes.size());
            }
        }
        else if (Section.SubFormatTag == ".ColourLut" && (Section.PayloadBytes.size() % sizeof(ColourSpace::ColourRampLut64)) == 0u)
        {
            const std::size_t LutCount = Section.PayloadBytes.size() / sizeof(ColourSpace::ColourRampLut64);
            OutModel.BakedLuts64.resize(LutCount);
            if (LutCount > 0u)
            {
                std::memcpy(OutModel.BakedLuts64.data(), Section.PayloadBytes.data(), Section.PayloadBytes.size());
            }
        }
    }

    if (OptionalOutRevision != nullptr)
    {
        return RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic);
    }
    return true;
}

bool ColourDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    ColourSpaceDocumentModel&    OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    SpaceContainerPayload Payload {};
    if (!SpaceContainerDecoder::DecodeFromFile(SourcePath, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractModel(Payload, OutModel, OutDiagnostic, OptionalOutRevision);
}

bool ColourDecoder::DecodeDocumentFromFile(
    const std::filesystem::path& SourcePath,
    ColourSpace::ColourDocument& OutDocument,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    ColourSpaceDocumentModel Model {};
    if (!DecodeFromFile(SourcePath, Model, OutDiagnostic, OptionalOutRevision))
    {
        return false;
    }
    OutDocument = Model.ToColourDocument();
    return true;
}

bool ColourDecoder::DecodeStandaloneRampFromToml(
    std::string_view                 TomlText,
    ColourSpace::AuthoredColourRamp& OutRamp,
    std::string&                     OutDiagnostic)
{
    try
    {
        const toml::table Parsed = toml::parse(TomlText);
        const toml::table* Body  = Parsed["ColourRamp"].as_table();
        if (Body == nullptr)
        {
            OutDiagnostic = "Missing [ColourRamp] table in .ColourRamp file.";
            return false;
        }
        OutRamp                = ColourSpace::AuthoredColourRamp {};
        OutRamp.RampIdentifier = (*Body)["RampIdentifier"].value_or(std::string{ "custom" });
        OutRamp.DisplayTitle   = (*Body)["DisplayTitle"].value_or(OutRamp.RampIdentifier);
        OutRamp.SeriesTitle    = (*Body)["SeriesTitle"].value_or(std::string{ "Custom" });
        return ExtractSourceFromManifest(*Body, OutRamp.Profile, OutDiagnostic);
    }
    catch (const toml::parse_error& Error)
    {
        OutDiagnostic = std::string(Error.description());
        return false;
    }
}

bool ColourDecoder::DecodeStandaloneRampFromFile(
    const std::filesystem::path&     SourcePath,
    ColourSpace::AuthoredColourRamp& OutRamp,
    std::string&                     OutDiagnostic)
{
    std::ifstream File(SourcePath, std::ios::binary);
    if (!File.is_open())
    {
        OutDiagnostic = "Unable to open .ColourRamp source file.";
        return false;
    }
    std::ostringstream Stream;
    Stream << File.rdbuf();
    return DecodeStandaloneRampFromToml(Stream.str(), OutRamp, OutDiagnostic);
}

} // namespace Frontier::Codecs
