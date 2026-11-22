/*============================================================================================================================================
                                                            COLOURINTERCHANGE.CPP
============================================================================================================================================*/
// 🧩 Implementation of external colour & gradient interchange (HTML Studio JSON, CSS, SVG, GIMP .gpl/.ggr, Adobe .ase/.aco, .cube 1D LUT)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Interchange/ColourInterchange.h"

#include "Engine/ColourSpace/ChromaticitySpace.h"
#include "Engine/ColourSpace/ColourPresetRepository.h"
#include "Engine/ColourSpace/ColourRampSampler.h"
#include "Engine/ColourSpace/TransferFunction.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Frontier::Interchange
{

namespace
{

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

[[nodiscard]] std::string TrimAscii(std::string_view Text)
{
    std::size_t Begin = 0u;
    std::size_t End   = Text.size();
    while (Begin < End && std::isspace(static_cast<unsigned char>(Text[Begin]))) ++Begin;
    while (End > Begin && std::isspace(static_cast<unsigned char>(Text[End - 1u]))) --End;
    return std::string(Text.substr(Begin, End - Begin));
}

[[nodiscard]] bool TryFindJsonKeyColon(std::string_view Json, std::string_view Key, std::size_t& OutPayloadPos) noexcept
{
    const std::string Quoted = "\"" + std::string(Key) + "\"";
    const std::size_t Found  = Json.find(Quoted);
    if (Found == std::string_view::npos) return false;
    std::size_t Pos = Found + Quoted.size();
    while (Pos < Json.size() && std::isspace(static_cast<unsigned char>(Json[Pos]))) ++Pos;
    if (Pos >= Json.size() || Json[Pos] != ':') return false;
    ++Pos;
    while (Pos < Json.size() && std::isspace(static_cast<unsigned char>(Json[Pos]))) ++Pos;
    OutPayloadPos = Pos;
    return true;
}

[[nodiscard]] bool TryParseJsonString(std::string_view Json, std::string_view Key, std::string& OutString)
{
    std::size_t Pos = 0u;
    if (!TryFindJsonKeyColon(Json, Key, Pos) || Pos >= Json.size() || Json[Pos] != '"') return false;
    ++Pos;
    std::string Result;
    while (Pos < Json.size() && Json[Pos] != '"')
    {
        if (Json[Pos] == '\\' && Pos + 1u < Json.size())
        {
            ++Pos;
        }
        Result.push_back(Json[Pos++]);
    }
    OutString = std::move(Result);
    return true;
}

[[nodiscard]] bool TryParseJsonNumber(std::string_view Json, std::string_view Key, float& OutNumber) noexcept
{
    std::size_t Pos = 0u;
    if (!TryFindJsonKeyColon(Json, Key, Pos) || Pos >= Json.size()) return false;
    const char* StartPtr = Json.data() + Pos;
    char* EndPtr         = nullptr;
    const float Parsed   = std::strtof(StartPtr, &EndPtr);
    if (EndPtr == StartPtr) return false;
    OutNumber = Parsed;
    return true;
}

[[nodiscard]] bool TryParseJsonFloatArray4(std::string_view Json, std::size_t OpenBracketPos, ColourSpace::LinearRgba& OutTint) noexcept
{
    if (OpenBracketPos >= Json.size() || Json[OpenBracketPos] != '[') return false;
    std::size_t Pos = OpenBracketPos + 1u;
    float Channels[4] { 1.0f, 1.0f, 1.0f, 1.0f };
    int Count = 0;
    while (Pos < Json.size() && Count < 4)
    {
        while (Pos < Json.size() && (std::isspace(static_cast<unsigned char>(Json[Pos])) || Json[Pos] == ',')) ++Pos;
        if (Pos >= Json.size() || Json[Pos] == ']') break;
        const char* StartPtr = Json.data() + Pos;
        char* EndPtr         = nullptr;
        const float Num      = std::strtof(StartPtr, &EndPtr);
        if (EndPtr == StartPtr) break;
        Channels[Count++] = Num;
        Pos               = static_cast<std::size_t>(EndPtr - Json.data());
    }
    if (Count < 3) return false;
    OutTint = { Channels[0], Channels[1], Channels[2], (Count >= 4) ? Channels[3] : 1.0f };
    return true;
}

[[nodiscard]] bool TryParseJsonRgbaKey(std::string_view Json, std::string_view Key, ColourSpace::LinearRgba& OutTint) noexcept
{
    std::size_t Pos = 0u;
    if (!TryFindJsonKeyColon(Json, Key, Pos)) return false;
    return TryParseJsonFloatArray4(Json, Pos, OutTint);
}

[[nodiscard]] bool TryParseCssColourToken(std::string_view Token, ColourSpace::LinearRgba& OutLinear) noexcept
{
    const std::string Trimmed = TrimAscii(Token);
    if (Trimmed.empty()) return false;

    if (Trimmed[0] == '#')
    {
        ColourSpace::SrgbRgba8 Display {};
        if (!ColourSpace::TryParseHexSrgb(Trimmed, Display)) return false;
        OutLinear = ColourSpace::ConvertSrgb8ToLinear(Display, ColourSpace::TransferCurveCategory::PiecewiseSrgb);
        return true;
    }

    const std::string Lower = LowercaseAscii(Trimmed);
    if (Lower.rfind("rgb(", 0u) == 0u || Lower.rfind("rgba(", 0u) == 0u)
    {
        const std::size_t Open  = Trimmed.find('(');
        const std::size_t Close = Trimmed.find(')');
        if (Open == std::string::npos || Close == std::string::npos || Close <= Open + 1u) return false;
        std::string Inner = Trimmed.substr(Open + 1u, Close - Open - 1u);
        for (char& Ch : Inner)
        {
            if (Ch == ',' || Ch == '/') Ch = ' ';
        }
        std::istringstream Stream(Inner);
        float R = 0.0f, G = 0.0f, B = 0.0f, A = 1.0f;
        if (!(Stream >> R >> G >> B)) return false;
        if (Stream >> A)
        {
            A = std::clamp(A, 0.0f, 1.0f);
        }
        const ColourSpace::SrgbRgba8 Display {
            static_cast<uint8_t>(std::clamp(std::lround(R), 0L, 255L)),
            static_cast<uint8_t>(std::clamp(std::lround(G), 0L, 255L)),
            static_cast<uint8_t>(std::clamp(std::lround(B), 0L, 255L)),
            static_cast<uint8_t>(std::clamp(std::lround(A * 255.0f), 0L, 255L)),
        };
        OutLinear = ColourSpace::ConvertSrgb8ToLinear(Display, ColourSpace::TransferCurveCategory::PiecewiseSrgb);
        return true;
    }
    return false;
}

void AppendBigEndianU16(std::vector<std::byte>& Buffer, uint16_t Word)
{
    Buffer.push_back(static_cast<std::byte>((Word >> 8u) & 0xffu));
    Buffer.push_back(static_cast<std::byte>(Word & 0xffu));
}

void AppendBigEndianU32(std::vector<std::byte>& Buffer, uint32_t Word)
{
    Buffer.push_back(static_cast<std::byte>((Word >> 24u) & 0xffu));
    Buffer.push_back(static_cast<std::byte>((Word >> 16u) & 0xffu));
    Buffer.push_back(static_cast<std::byte>((Word >> 8u) & 0xffu));
    Buffer.push_back(static_cast<std::byte>(Word & 0xffu));
}

void AppendBigEndianFloat(std::vector<std::byte>& Buffer, float Scalar)
{
    uint32_t Bits = 0u;
    std::memcpy(&Bits, &Scalar, sizeof(Bits));
    AppendBigEndianU32(Buffer, Bits);
}

[[nodiscard]] bool ReadBigEndianU16(std::span<const std::byte> Bytes, std::size_t& Offset, uint16_t& OutWord) noexcept
{
    if (Offset + 2u > Bytes.size()) return false;
    OutWord = static_cast<uint16_t>((std::to_integer<uint16_t>(Bytes[Offset]) << 8u)
                                  | std::to_integer<uint16_t>(Bytes[Offset + 1u]));
    Offset += 2u;
    return true;
}

[[nodiscard]] bool ReadBigEndianU32(std::span<const std::byte> Bytes, std::size_t& Offset, uint32_t& OutWord) noexcept
{
    if (Offset + 4u > Bytes.size()) return false;
    OutWord = (std::to_integer<uint32_t>(Bytes[Offset + 0u]) << 24u)
            | (std::to_integer<uint32_t>(Bytes[Offset + 1u]) << 16u)
            | (std::to_integer<uint32_t>(Bytes[Offset + 2u]) << 8u)
            |  std::to_integer<uint32_t>(Bytes[Offset + 3u]);
    Offset += 4u;
    return true;
}

[[nodiscard]] bool ReadBigEndianFloat(std::span<const std::byte> Bytes, std::size_t& Offset, float& OutScalar) noexcept
{
    uint32_t Bits = 0u;
    if (!ReadBigEndianU32(Bytes, Offset, Bits)) return false;
    std::memcpy(&OutScalar, &Bits, sizeof(OutScalar));
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 FORMAT DETECTION
//------------------------------------------------------------------------------------------------------------------------

ExternalColourFormat ColourInterchange::DetectFormatFromPath(const std::filesystem::path& FilePath) noexcept
{
    const std::string Ext = LowercaseAscii(FilePath.extension().string());
    if (Ext == ".json") return ExternalColourFormat::HtmlStudioJson;
    if (Ext == ".css")  return ExternalColourFormat::CssGradient;
    if (Ext == ".svg")  return ExternalColourFormat::SvgGradient;
    if (Ext == ".gpl")  return ExternalColourFormat::GimpSwatchGpl;
    if (Ext == ".ggr")  return ExternalColourFormat::GimpGradientGgr;
    if (Ext == ".ase")  return ExternalColourFormat::AdobeSwatchAse;
    if (Ext == ".aco")  return ExternalColourFormat::AdobeColourAco;
    if (Ext == ".cube") return ExternalColourFormat::CubeLut1D;
    return ExternalColourFormat::Unknown;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           HTML STUDIO JSON COLOUR BLOCK
//------------------------------------------------------------------------------------------------------------------------

bool ColourInterchange::ImportHtmlStudioJsonBlock(
    std::string_view                  JsonText,
    ColourSpace::ColourSourceProfile& OutProfile,
    std::string&                      OutDiagnostic)
{
    OutProfile = ColourSpace::ColourSourceProfile {};

    float Number = 0.0f;
    if (TryParseJsonNumber(JsonText, "colorMin",    Number)) OutProfile.Range.RangeMin    = Number;
    if (TryParseJsonNumber(JsonText, "colorMax",    Number)) OutProfile.Range.RangeMax    = Number;
    if (TryParseJsonNumber(JsonText, "colorRepeat", Number)) OutProfile.Range.RepeatCount = std::max(0.05f, Number);
    if (TryParseJsonNumber(JsonText, "colorPhase",  Number)) OutProfile.Range.PhaseOffset = Number;

    std::string Token {};
    if (TryParseJsonString(JsonText, "colorWrap", Token))
    {
        OutProfile.Range.WrapBehaviour = ColourSpace::ParseColourWrapToken(Token);
    }
    if (TryParseJsonString(JsonText, "colorInterp", Token))
    {
        OutProfile.Range.BlendCurve = ColourSpace::ParseColourBlendCurveToken(Token);
    }
    if (TryParseJsonString(JsonText, "blend", Token))
    {
        OutProfile.BlendEquation = ColourSpace::ParseOpticalBlendToken(Token);
    }

    // 📝 Check arbitrary colorStops array first: [ { "pos": 0.0, "color": [r,g,b,a] }, ... ]
    std::size_t StopsPos = 0u;
    if (TryFindJsonKeyColon(JsonText, "colorStops", StopsPos) && StopsPos < JsonText.size() && JsonText[StopsPos] == '[')
    {
        std::vector<ColourSpace::ColourStop> ParsedStops {};
        std::size_t Cursor = StopsPos + 1u;
        int BracketDepth   = 1;
        while (Cursor < JsonText.size() && BracketDepth > 0)
        {
            if (JsonText[Cursor] == '[')      ++BracketDepth;
            else if (JsonText[Cursor] == ']') --BracketDepth;
            else if (JsonText[Cursor] == '{')
            {
                const std::size_t ObjStart = Cursor;
                int BraceDepth = 0;
                while (Cursor < JsonText.size())
                {
                    if (JsonText[Cursor] == '{')      ++BraceDepth;
                    else if (JsonText[Cursor] == '}') --BraceDepth;
                    ++Cursor;
                    if (BraceDepth == 0) break;
                }
                const std::string_view BlockView = JsonText.substr(ObjStart, Cursor - ObjStart);
                ColourSpace::ColourStop Stop {};
                float PosScalar = 0.0f;
                if (TryParseJsonNumber(BlockView, "pos", PosScalar) && TryParseJsonRgbaKey(BlockView, "color", Stop.LinearTint))
                {
                    Stop.Position01 = std::clamp(PosScalar, 0.0f, 1.0f);
                    ParsedStops.push_back(Stop);
                }
                continue;
            }
            ++Cursor;
        }
        if (!ParsedStops.empty())
        {
            ColourSpace::MultiStopGradientSource Gradient {};
            if (TryParseJsonString(JsonText, "colorRampPreset", Token))
            {
                Gradient.LinkedPresetIdentifier = Token;
            }
            Gradient.Stops    = std::move(ParsedStops);
            OutProfile.Source = std::move(Gradient);
            return true;
        }
    }

    // 📝 Check named preset token (rainbow, thermal, aurora, plasma, ocean, sunset)
    if (TryParseJsonString(JsonText, "colorRampPreset", Token))
    {
        if (const auto* Preset = ColourSpace::FindBuiltInColourRampPreset(Token))
        {
            ColourSpace::MultiStopGradientSource Gradient {};
            Gradient.LinkedPresetIdentifier = Preset->Identifier;
            Gradient.Stops                  = Preset->Stops;
            OutProfile.Source               = std::move(Gradient);
            return true;
        }
    }

    // 📝 Fallback to classic 2-stop / 3-stop LinearFill (colA / colB / colC)
    ColourSpace::LinearFillSource Fill {};
    const bool HasA = TryParseJsonRgbaKey(JsonText, "colA", Fill.PrimaryLinear);
    const bool HasB = TryParseJsonRgbaKey(JsonText, "colB", Fill.SecondaryLinear);
    const bool HasC = TryParseJsonRgbaKey(JsonText, "colC", Fill.MidpointLinear);
    if (HasA || HasB)
    {
        Fill.MidpointEnabled = HasC && (Fill.MidpointLinear.Alpha > 0.001f);
        OutProfile.Source    = Fill;
        return true;
    }

    OutDiagnostic = "No recognizable HTML Studio colour keys (colorStops, colorRampPreset, colA/colB) found in JSON block.";
    return false;
}

std::string ColourInterchange::ExportHtmlStudioJsonBlock(const ColourSpace::ColourSourceProfile& Profile)
{
    const std::vector<ColourSpace::ColourStop> Stops = ColourSpace::ExtractOrderedStops(Profile.Source);
    std::ostringstream Stream;
    Stream << std::fixed << std::setprecision(5);
    Stream << "{\n";
    Stream << "  \"colorMin\": "    << Profile.Range.RangeMin    << ",\n";
    Stream << "  \"colorMax\": "    << Profile.Range.RangeMax    << ",\n";
    Stream << "  \"colorRepeat\": " << Profile.Range.RepeatCount << ",\n";
    Stream << "  \"colorPhase\": "  << Profile.Range.PhaseOffset << ",\n";
    Stream << "  \"colorWrap\": \"" << ColourSpace::QueryColourWrapToken(Profile.Range.WrapBehaviour) << "\",\n";
    Stream << "  \"colorInterp\": \"" << ColourSpace::QueryColourBlendCurveToken(Profile.Range.BlendCurve) << "\",\n";
    Stream << "  \"blend\": \"" << ColourSpace::QueryOpticalBlendToken(Profile.BlendEquation) << "\",\n";
    if (const auto* Grad = std::get_if<ColourSpace::MultiStopGradientSource>(&Profile.Source); Grad != nullptr && !Grad->LinkedPresetIdentifier.empty())
    {
        Stream << "  \"colorRampPreset\": \"" << Grad->LinkedPresetIdentifier << "\",\n";
    }
    Stream << "  \"colorStops\": [\n";
    for (std::size_t I = 0u; I < Stops.size(); ++I)
    {
        const auto& S = Stops[I];
        Stream << "    { \"pos\": " << S.Position01
               << ", \"color\": [" << S.LinearTint.Red << ", " << S.LinearTint.Green << ", " << S.LinearTint.Blue << ", " << S.LinearTint.Alpha << "] }";
        if (I + 1u < Stops.size()) Stream << ",";
        Stream << "\n";
    }
    Stream << "  ]\n}\n";
    return Stream.str();
}

//------------------------------------------------------------------------------------------------------------------------
//                                               CSS LINEAR GRADIENT
//------------------------------------------------------------------------------------------------------------------------

bool ColourInterchange::ImportCssGradientText(
    std::string_view                  CssText,
    ColourSpace::ColourSourceProfile& OutProfile,
    std::string&                      OutDiagnostic)
{
    const std::size_t Open  = CssText.find('(');
    const std::size_t Close = CssText.rfind(')');
    if (Open == std::string_view::npos || Close == std::string_view::npos || Close <= Open + 1u)
    {
        OutDiagnostic = "Invalid CSS linear-gradient(...) syntax.";
        return false;
    }

    // Split top-level comma arguments (respecting rgb(...) / rgba(...) parentheses)
    std::vector<std::string> Tokens {};
    int ParenDepth       = 0;
    std::size_t SegStart = Open + 1u;
    for (std::size_t I = Open + 1u; I <= Close; ++I)
    {
        const char Ch = (I < Close) ? CssText[I] : ',';
        if (Ch == '(')      ++ParenDepth;
        else if (Ch == ')') --ParenDepth;
        else if (Ch == ',' && ParenDepth == 0)
        {
            Tokens.push_back(TrimAscii(CssText.substr(SegStart, I - SegStart)));
            SegStart = I + 1u;
        }
    }

    std::vector<ColourSpace::ColourStop> Stops {};
    std::vector<bool> HasExplicitPos {};

    for (const std::string& RawArg : Tokens)
    {
        if (RawArg.empty()) continue;
        // Skip direction prefix ("90deg", "to right", etc.)
        const std::string Lower = LowercaseAscii(RawArg);
        if (Lower.rfind("to ", 0u) == 0u || Lower.find("deg") != std::string::npos || Lower.find("turn") != std::string::npos)
        {
            continue;
        }

        // Separate colour token from optional trailing percentage
        std::string ColourPart = RawArg;
        float StopPos          = -1.0f;
        const std::size_t LastSpace = RawArg.find_last_of(" \t");
        if (LastSpace != std::string::npos)
        {
            const std::string Tail = TrimAscii(RawArg.substr(LastSpace + 1u));
            if (!Tail.empty() && Tail.back() == '%')
            {
                StopPos    = std::clamp(std::strtof(Tail.c_str(), nullptr) * 0.01f, 0.0f, 1.0f);
                ColourPart = TrimAscii(RawArg.substr(0u, LastSpace));
            }
        }

        ColourSpace::LinearRgba Linear {};
        if (TryParseCssColourToken(ColourPart, Linear))
        {
            Stops.push_back({ std::max(0.0f, StopPos), Linear });
            HasExplicitPos.push_back(StopPos >= 0.0f);
        }
    }

    if (Stops.empty())
    {
        OutDiagnostic = "No valid colour stops parsed from CSS gradient.";
        return false;
    }

    for (std::size_t I = 0u; I < Stops.size(); ++I)
    {
        if (!HasExplicitPos[I])
        {
            Stops[I].Position01 = (Stops.size() > 1u) ? static_cast<float>(I) / static_cast<float>(Stops.size() - 1u) : 0.0f;
        }
    }

    OutProfile = ColourSpace::ColourSourceProfile {};
    ColourSpace::MultiStopGradientSource Gradient {};
    Gradient.Stops    = std::move(Stops);
    OutProfile.Source = std::move(Gradient);
    return true;
}

std::string ColourInterchange::ExportCssGradientText(const ColourSpace::ColourSourceProfile& Profile, float AngleDegrees)
{
    const std::vector<ColourSpace::ColourStop> Stops = ColourSpace::ExtractOrderedStops(Profile.Source);
    std::ostringstream Stream;
    Stream << "linear-gradient(" << static_cast<int>(std::lround(AngleDegrees)) << "deg";
    for (const ColourSpace::ColourStop& Stop : Stops)
    {
        const ColourSpace::SrgbRgba8 Srgb = ColourSpace::ConvertLinearToSrgb8(Stop.LinearTint, ColourSpace::TransferCurveCategory::PiecewiseSrgb);
        Stream << ", ";
        if (Srgb.Alpha < 255u)
        {
            Stream << "rgba(" << static_cast<int>(Srgb.Red) << ", " << static_cast<int>(Srgb.Green) << ", "
                   << static_cast<int>(Srgb.Blue) << ", " << std::fixed << std::setprecision(3) << (static_cast<float>(Srgb.Alpha) / 255.0f) << ")";
        }
        else
        {
            Stream << ColourSpace::FormatHexUpper(Srgb, false);
        }
        Stream << " " << std::fixed << std::setprecision(1) << (Stop.Position01 * 100.0f) << "%";
    }
    Stream << ")";
    return Stream.str();
}

//------------------------------------------------------------------------------------------------------------------------
//                                               SVG <linearGradient>
//------------------------------------------------------------------------------------------------------------------------

bool ColourInterchange::ImportSvgGradientText(
    std::string_view                  SvgText,
    ColourSpace::ColourSourceProfile& OutProfile,
    std::string&                      OutDiagnostic)
{
    auto ReadXmlAttribute = [](std::string_view Tag, std::string_view AttrName, std::string& OutAttr) -> bool {
        const std::string Pattern = std::string(AttrName) + "=";
        const std::size_t Found   = Tag.find(Pattern);
        if (Found == std::string_view::npos) return false;
        std::size_t Pos = Found + Pattern.size();
        if (Pos >= Tag.size()) return false;
        const char Quote = Tag[Pos];
        if (Quote != '"' && Quote != '\'') return false;
        ++Pos;
        const std::size_t Close = Tag.find(Quote, Pos);
        if (Close == std::string_view::npos) return false;
        OutAttr = std::string(Tag.substr(Pos, Close - Pos));
        return true;
    };

    std::vector<ColourSpace::ColourStop> Stops {};
    std::size_t SearchPos = 0u;
    while (SearchPos < SvgText.size())
    {
        const std::size_t TagStart = SvgText.find("<stop", SearchPos);
        if (TagStart == std::string_view::npos) break;
        const std::size_t TagEnd = SvgText.find('>', TagStart);
        if (TagEnd == std::string_view::npos) break;
        const std::string_view Tag = SvgText.substr(TagStart, TagEnd - TagStart + 1u);

        std::string OffsetStr {}, ColourStr {}, OpacityStr {};
        if (ReadXmlAttribute(Tag, "stop-color", ColourStr))
        {
            ColourSpace::LinearRgba Linear {};
            if (TryParseCssColourToken(ColourStr, Linear))
            {
                float Pos01 = 0.0f;
                if (ReadXmlAttribute(Tag, "offset", OffsetStr))
                {
                    Pos01 = std::strtof(OffsetStr.c_str(), nullptr);
                    if (OffsetStr.find('%') != std::string::npos) Pos01 *= 0.01f;
                    Pos01 = std::clamp(Pos01, 0.0f, 1.0f);
                }
                if (ReadXmlAttribute(Tag, "stop-opacity", OpacityStr))
                {
                    Linear.Alpha = std::clamp(std::strtof(OpacityStr.c_str(), nullptr), 0.0f, 1.0f);
                }
                Stops.push_back({ Pos01, Linear });
            }
        }
        SearchPos = TagEnd + 1u;
    }

    if (Stops.empty())
    {
        OutDiagnostic = "No <stop stop-color=...> elements found inside SVG gradient text.";
        return false;
    }

    OutProfile = ColourSpace::ColourSourceProfile {};
    ColourSpace::MultiStopGradientSource Gradient {};
    Gradient.Stops    = std::move(Stops);
    OutProfile.Source = std::move(Gradient);
    return true;
}

std::string ColourInterchange::ExportSvgGradientText(const ColourSpace::ColourSourceProfile& Profile, std::string_view GradientIdentifier)
{
    const std::vector<ColourSpace::ColourStop> Stops = ColourSpace::ExtractOrderedStops(Profile.Source);
    std::ostringstream Stream;
    Stream << "<linearGradient id=\"" << GradientIdentifier << "\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"0%\">\n";
    Stream << std::fixed << std::setprecision(2);
    for (const ColourSpace::ColourStop& Stop : Stops)
    {
        const ColourSpace::SrgbRgba8 Srgb = ColourSpace::ConvertLinearToSrgb8(Stop.LinearTint, ColourSpace::TransferCurveCategory::PiecewiseSrgb);
        Stream << "  <stop offset=\"" << (Stop.Position01 * 100.0f) << "%\" stop-color=\""
               << ColourSpace::FormatHexUpper(Srgb, false) << "\" stop-opacity=\""
               << std::setprecision(4) << std::clamp(Stop.LinearTint.Alpha, 0.0f, 1.0f) << "\"/>\n";
        Stream << std::setprecision(2);
    }
    Stream << "</linearGradient>\n";
    return Stream.str();
}

//------------------------------------------------------------------------------------------------------------------------
//                                           GIMP .GPL SWATCHES & .GGR GRADIENTS
//------------------------------------------------------------------------------------------------------------------------

bool ColourInterchange::ImportGimpGplText(
    std::string_view             GplText,
    ColourSpace::ColourDocument& OutDocument,
    std::string&                 OutDiagnostic)
{
    std::istringstream Stream { std::string(GplText) };
    std::string Line;
    bool SawHeader = false;
    OutDocument.Swatches.clear();

    while (std::getline(Stream, Line))
    {
        const std::string Trimmed = TrimAscii(Line);
        if (Trimmed.empty() || Trimmed[0] == '#') continue;
        if (!SawHeader)
        {
            if (Trimmed.rfind("GIMP Palette", 0u) == 0u)
            {
                SawHeader = true;
                continue;
            }
            OutDiagnostic = "Missing 'GIMP Palette' header in .gpl file.";
            return false;
        }
        if (Trimmed.rfind("Name:", 0u) == 0u)
        {
            OutDocument.DocumentTitle = TrimAscii(Trimmed.substr(5u));
            continue;
        }
        if (Trimmed.rfind("Columns:", 0u) == 0u)
        {
            continue;
        }

        std::istringstream Row(Trimmed);
        int R = 0, G = 0, B = 0;
        if (Row >> R >> G >> B)
        {
            std::string Rest;
            std::getline(Row, Rest);
            const std::string Name = TrimAscii(Rest);
            const ColourSpace::SrgbRgba8 Display {
                static_cast<uint8_t>(std::clamp(R, 0, 255)),
                static_cast<uint8_t>(std::clamp(G, 0, 255)),
                static_cast<uint8_t>(std::clamp(B, 0, 255)),
                255u,
            };
            ColourSpace::NamedColourSwatchEntry Swatch {};
            Swatch.SwatchName  = Name.empty() ? ColourSpace::FormatHexUpper(Display, false) : Name;
            Swatch.SeriesTitle = "ImportedGPL";
            Swatch.LinearTint  = ColourSpace::ConvertSrgb8ToLinear(Display, ColourSpace::TransferCurveCategory::PiecewiseSrgb);
            OutDocument.Swatches.push_back(std::move(Swatch));
        }
    }

    if (OutDocument.Swatches.empty())
    {
        OutDiagnostic = "No swatches parsed from GIMP .gpl document.";
        return false;
    }
    return true;
}

std::string ColourInterchange::ExportGimpGplText(const ColourSpace::ColourDocument& Document)
{
    std::ostringstream Stream;
    Stream << "GIMP Palette\n";
    Stream << "Name: " << Document.DocumentTitle << "\n";
    Stream << "Columns: 8\n#\n";
    for (const ColourSpace::NamedColourSwatchEntry& Swatch : Document.Swatches)
    {
        const ColourSpace::SrgbRgba8 Display = ColourSpace::ConvertLinearToSrgb8(Swatch.LinearTint, ColourSpace::TransferCurveCategory::PiecewiseSrgb);
        Stream << std::setw(3) << static_cast<int>(Display.Red) << " "
               << std::setw(3) << static_cast<int>(Display.Green) << " "
               << std::setw(3) << static_cast<int>(Display.Blue) << "\t"
               << Swatch.SwatchName << "\n";
    }
    return Stream.str();
}

bool ColourInterchange::ImportGimpGgrText(
    std::string_view                  GgrText,
    ColourSpace::AuthoredColourRamp&  OutRamp,
    std::string&                      OutDiagnostic)
{
    std::istringstream Stream { std::string(GgrText) };
    std::string Header, NameLine, CountLine;
    if (!std::getline(Stream, Header) || Header.rfind("GIMP Gradient", 0u) != 0u)
    {
        OutDiagnostic = "Missing 'GIMP Gradient' header in .ggr file.";
        return false;
    }
    if (!std::getline(Stream, NameLine) || !std::getline(Stream, CountLine))
    {
        OutDiagnostic = "Truncated GIMP .ggr header.";
        return false;
    }

    OutRamp = ColourSpace::AuthoredColourRamp {};
    const std::size_t Colon = NameLine.find(':');
    OutRamp.DisplayTitle    = (Colon != std::string::npos) ? TrimAscii(NameLine.substr(Colon + 1u)) : "ImportedGradient";
    OutRamp.RampIdentifier  = LowercaseAscii(OutRamp.DisplayTitle);
    OutRamp.SeriesTitle     = "ImportedGGR";

    const int SegmentCount = std::max(0, std::atoi(CountLine.c_str()));
    std::vector<ColourSpace::ColourStop> Stops {};
    std::string SegLine;
    for (int I = 0; I < SegmentCount && std::getline(Stream, SegLine); ++I)
    {
        std::istringstream Row(SegLine);
        float Left = 0.0f, Mid = 0.5f, Right = 1.0f;
        float Lr = 0.0f, Lg = 0.0f, Lb = 0.0f, La = 1.0f;
        float Rr = 1.0f, Rg = 1.0f, Rb = 1.0f, Ra = 1.0f;
        if (Row >> Left >> Mid >> Right >> Lr >> Lg >> Lb >> La >> Rr >> Rg >> Rb >> Ra)
        {
            if (Stops.empty())
            {
                Stops.push_back({
                    std::clamp(Left, 0.0f, 1.0f),
                    { ColourSpace::DecodePiecewiseSrgbChannel(Lr), ColourSpace::DecodePiecewiseSrgbChannel(Lg), ColourSpace::DecodePiecewiseSrgbChannel(Lb), La },
                });
            }
            Stops.push_back({
                std::clamp(Right, 0.0f, 1.0f),
                { ColourSpace::DecodePiecewiseSrgbChannel(Rr), ColourSpace::DecodePiecewiseSrgbChannel(Rg), ColourSpace::DecodePiecewiseSrgbChannel(Rb), Ra },
            });
        }
    }

    if (Stops.empty())
    {
        OutDiagnostic = "No valid segments parsed from GIMP .ggr file.";
        return false;
    }

    ColourSpace::MultiStopGradientSource Gradient {};
    Gradient.Stops          = std::move(Stops);
    OutRamp.Profile.Source  = std::move(Gradient);
    return true;
}

std::string ColourInterchange::ExportGimpGgrText(const ColourSpace::AuthoredColourRamp& Ramp)
{
    const std::vector<ColourSpace::ColourStop> Stops = ColourSpace::ExtractOrderedStops(Ramp.Profile.Source);
    const std::size_t SegCount = (Stops.size() >= 2u) ? (Stops.size() - 1u) : 1u;
    std::ostringstream Stream;
    Stream << "GIMP Gradient\n";
    Stream << "Name: " << Ramp.DisplayTitle << "\n";
    Stream << SegCount << "\n";
    Stream << std::fixed << std::setprecision(6);

    for (std::size_t I = 0u; I < SegCount; ++I)
    {
        const ColourSpace::ColourStop& A = Stops[std::min(I, Stops.size() - 1u)];
        const ColourSpace::ColourStop& B = Stops[std::min(I + 1u, Stops.size() - 1u)];
        const float Mid = 0.5f * (A.Position01 + B.Position01);
        Stream << A.Position01 << " " << Mid << " " << B.Position01 << " "
               << ColourSpace::EncodePiecewiseSrgbChannel(A.LinearTint.Red)   << " "
               << ColourSpace::EncodePiecewiseSrgbChannel(A.LinearTint.Green) << " "
               << ColourSpace::EncodePiecewiseSrgbChannel(A.LinearTint.Blue)  << " "
               << A.LinearTint.Alpha << " "
               << ColourSpace::EncodePiecewiseSrgbChannel(B.LinearTint.Red)   << " "
               << ColourSpace::EncodePiecewiseSrgbChannel(B.LinearTint.Green) << " "
               << ColourSpace::EncodePiecewiseSrgbChannel(B.LinearTint.Blue)  << " "
               << B.LinearTint.Alpha << " 0 0\n";
    }
    return Stream.str();
}

//------------------------------------------------------------------------------------------------------------------------
//                                        ADOBE .ASE & .ACO BINARY SWATCHES
//------------------------------------------------------------------------------------------------------------------------

bool ColourInterchange::ImportAdobeAseBytes(
    std::span<const std::byte>   Bytes,
    ColourSpace::ColourDocument& OutDocument,
    std::string&                 OutDiagnostic)
{
    if (Bytes.size() < 12u
        || Bytes[0] != std::byte{ 'A' } || Bytes[1] != std::byte{ 'S' }
        || Bytes[2] != std::byte{ 'E' } || Bytes[3] != std::byte{ 'F' })
    {
        OutDiagnostic = "Invalid Adobe .ase signature (expected 'ASEF').";
        return false;
    }

    std::size_t Offset  = 4u;
    uint16_t MajorVer   = 0u, MinorVer = 0u;
    uint32_t BlockCount = 0u;
    if (!ReadBigEndianU16(Bytes, Offset, MajorVer)
        || !ReadBigEndianU16(Bytes, Offset, MinorVer)
        || !ReadBigEndianU32(Bytes, Offset, BlockCount))
    {
        OutDiagnostic = "Truncated Adobe .ase header.";
        return false;
    }

    OutDocument.Swatches.clear();
    for (uint32_t BlockIdx = 0u; BlockIdx < BlockCount && Offset < Bytes.size(); ++BlockIdx)
    {
        uint16_t BlockType   = 0u;
        uint32_t BlockLength = 0u;
        if (!ReadBigEndianU16(Bytes, Offset, BlockType) || !ReadBigEndianU32(Bytes, Offset, BlockLength))
        {
            break;
        }
        const std::size_t BlockEnd = std::min(Bytes.size(), Offset + static_cast<std::size_t>(BlockLength));
        if (BlockType == 0x0001u && Offset + 2u <= BlockEnd)
        {
            uint16_t NameLenChars = 0u;
            if (!ReadBigEndianU16(Bytes, Offset, NameLenChars))
            {
                break;
            }
            std::string SwatchName {};
            for (uint16_t C = 0u; C < NameLenChars && Offset + 2u <= BlockEnd; ++C)
            {
                uint16_t CodeUnit = 0u;
                if (!ReadBigEndianU16(Bytes, Offset, CodeUnit))
                {
                    break;
                }
                if (CodeUnit != 0u && CodeUnit < 128u)
                {
                    SwatchName.push_back(static_cast<char>(CodeUnit));
                }
            }
            if (Offset + 4u <= BlockEnd)
            {
                const char ModelTag[4] = {
                    static_cast<char>(Bytes[Offset + 0u]),
                    static_cast<char>(Bytes[Offset + 1u]),
                    static_cast<char>(Bytes[Offset + 2u]),
                    static_cast<char>(Bytes[Offset + 3u]),
                };
                Offset += 4u;
                ColourSpace::LinearRgba Linear { 1.0f, 1.0f, 1.0f, 1.0f };
                bool Decoded = false;
                if (std::memcmp(ModelTag, "RGB ", 4u) == 0)
                {
                    float R = 0.0f, G = 0.0f, B = 0.0f;
                    if (ReadBigEndianFloat(Bytes, Offset, R) && ReadBigEndianFloat(Bytes, Offset, G) && ReadBigEndianFloat(Bytes, Offset, B))
                    {
                        Linear  = { ColourSpace::DecodePiecewiseSrgbChannel(R), ColourSpace::DecodePiecewiseSrgbChannel(G), ColourSpace::DecodePiecewiseSrgbChannel(B), 1.0f };
                        Decoded = true;
                    }
                }
                else if (std::memcmp(ModelTag, "CMYK", 4u) == 0)
                {
                    float C = 0.0f, M = 0.0f, Y = 0.0f, K = 0.0f;
                    if (ReadBigEndianFloat(Bytes, Offset, C) && ReadBigEndianFloat(Bytes, Offset, M)
                        && ReadBigEndianFloat(Bytes, Offset, Y) && ReadBigEndianFloat(Bytes, Offset, K))
                    {
                        const ColourSpace::SrgbRgba8 Srgb = ColourSpace::ConvertCmykToSrgb8({
                            static_cast<uint8_t>(std::clamp(std::lround(C * 100.0f), 0L, 100L)),
                            static_cast<uint8_t>(std::clamp(std::lround(M * 100.0f), 0L, 100L)),
                            static_cast<uint8_t>(std::clamp(std::lround(Y * 100.0f), 0L, 100L)),
                            static_cast<uint8_t>(std::clamp(std::lround(K * 100.0f), 0L, 100L)),
                        });
                        Linear  = ColourSpace::ConvertSrgb8ToLinear(Srgb, ColourSpace::TransferCurveCategory::PiecewiseSrgb);
                        Decoded = true;
                    }
                }
                else if (std::memcmp(ModelTag, "Gray", 4u) == 0)
                {
                    float Gray = 0.0f;
                    if (ReadBigEndianFloat(Bytes, Offset, Gray))
                    {
                        const float Lin = ColourSpace::DecodePiecewiseSrgbChannel(Gray);
                        Linear          = { Lin, Lin, Lin, 1.0f };
                        Decoded         = true;
                    }
                }

                if (Decoded)
                {
                    OutDocument.Swatches.push_back({ SwatchName, "ImportedASE", Linear, false });
                }
            }
        }
        Offset = BlockEnd;
    }

    if (OutDocument.Swatches.empty())
    {
        OutDiagnostic = "No colour entries found inside Adobe .ase container.";
        return false;
    }
    return true;
}

std::vector<std::byte> ColourInterchange::ExportAdobeAseBytes(const ColourSpace::ColourDocument& Document)
{
    std::vector<std::byte> Out {};
    Out.push_back(std::byte{ 'A' });
    Out.push_back(std::byte{ 'S' });
    Out.push_back(std::byte{ 'E' });
    Out.push_back(std::byte{ 'F' });
    AppendBigEndianU16(Out, 1u);
    AppendBigEndianU16(Out, 0u);
    AppendBigEndianU32(Out, static_cast<uint32_t>(Document.Swatches.size()));

    for (const ColourSpace::NamedColourSwatchEntry& Swatch : Document.Swatches)
    {
        std::vector<std::byte> Body {};
        const uint16_t CharCount = static_cast<uint16_t>(Swatch.SwatchName.size() + 1u);
        AppendBigEndianU16(Body, CharCount);
        for (char Ch : Swatch.SwatchName)
        {
            AppendBigEndianU16(Body, static_cast<uint8_t>(Ch));
        }
        AppendBigEndianU16(Body, 0u);
        Body.push_back(std::byte{ 'R' });
        Body.push_back(std::byte{ 'G' });
        Body.push_back(std::byte{ 'B' });
        Body.push_back(std::byte{ ' ' });
        AppendBigEndianFloat(Body, ColourSpace::EncodePiecewiseSrgbChannel(Swatch.LinearTint.Red));
        AppendBigEndianFloat(Body, ColourSpace::EncodePiecewiseSrgbChannel(Swatch.LinearTint.Green));
        AppendBigEndianFloat(Body, ColourSpace::EncodePiecewiseSrgbChannel(Swatch.LinearTint.Blue));
        AppendBigEndianU16(Body, 0u); // Global swatch marker

        AppendBigEndianU16(Out, 0x0001u);
        AppendBigEndianU32(Out, static_cast<uint32_t>(Body.size()));
        Out.insert(Out.end(), Body.begin(), Body.end());
    }
    return Out;
}

bool ColourInterchange::ImportAdobeAcoBytes(
    std::span<const std::byte>   Bytes,
    ColourSpace::ColourDocument& OutDocument,
    std::string&                 OutDiagnostic)
{
    std::size_t Offset = 0u;
    uint16_t Version   = 0u;
    uint16_t Count     = 0u;
    if (!ReadBigEndianU16(Bytes, Offset, Version) || !ReadBigEndianU16(Bytes, Offset, Count) || (Version != 1u && Version != 2u))
    {
        OutDiagnostic = "Invalid Adobe .aco header.";
        return false;
    }

    OutDocument.Swatches.clear();
    for (uint16_t I = 0u; I < Count; ++I)
    {
        uint16_t SpaceId = 0u, W = 0u, X = 0u, Y = 0u, Z = 0u;
        if (!ReadBigEndianU16(Bytes, Offset, SpaceId)
            || !ReadBigEndianU16(Bytes, Offset, W) || !ReadBigEndianU16(Bytes, Offset, X)
            || !ReadBigEndianU16(Bytes, Offset, Y) || !ReadBigEndianU16(Bytes, Offset, Z))
        {
            break;
        }
        std::string Name = "AcoSwatch_" + std::to_string(I);
        if (Version == 2u)
        {
            uint32_t NameLen = 0u;
            if (ReadBigEndianU32(Bytes, Offset, NameLen))
            {
                Name.clear();
                for (uint32_t C = 0u; C < NameLen; ++C)
                {
                    uint16_t Unit = 0u;
                    if (!ReadBigEndianU16(Bytes, Offset, Unit)) break;
                    if (Unit != 0u && Unit < 128u) Name.push_back(static_cast<char>(Unit));
                }
            }
        }
        if (SpaceId == 0u) // RGB [0..65535]
        {
            const float R = static_cast<float>(W) / 65535.0f;
            const float G = static_cast<float>(X) / 65535.0f;
            const float B = static_cast<float>(Y) / 65535.0f;
            OutDocument.Swatches.push_back({
                Name,
                "ImportedACO",
                { ColourSpace::DecodePiecewiseSrgbChannel(R), ColourSpace::DecodePiecewiseSrgbChannel(G), ColourSpace::DecodePiecewiseSrgbChannel(B), 1.0f },
                false,
            });
        }
    }

    if (OutDocument.Swatches.empty())
    {
        OutDiagnostic = "No RGB swatches parsed from Adobe .aco file.";
        return false;
    }
    return true;
}

std::vector<std::byte> ColourInterchange::ExportAdobeAcoBytes(const ColourSpace::ColourDocument& Document)
{
    std::vector<std::byte> Out {};
    const uint16_t Count = static_cast<uint16_t>(std::min<std::size_t>(Document.Swatches.size(), 65535u));
    // Version 2 section (includes swatch names)
    AppendBigEndianU16(Out, 2u);
    AppendBigEndianU16(Out, Count);
    for (uint16_t I = 0u; I < Count; ++I)
    {
        const auto& S = Document.Swatches[I];
        const auto Quant16 = [](float Linear) noexcept -> uint16_t {
            return static_cast<uint16_t>(std::clamp(std::lround(ColourSpace::EncodePiecewiseSrgbChannel(Linear) * 65535.0f), 0L, 65535L));
        };
        AppendBigEndianU16(Out, 0u); // RGB space
        AppendBigEndianU16(Out, Quant16(S.LinearTint.Red));
        AppendBigEndianU16(Out, Quant16(S.LinearTint.Green));
        AppendBigEndianU16(Out, Quant16(S.LinearTint.Blue));
        AppendBigEndianU16(Out, 0u);
        AppendBigEndianU32(Out, static_cast<uint32_t>(S.SwatchName.size() + 1u));
        for (char Ch : S.SwatchName)
        {
            AppendBigEndianU16(Out, static_cast<uint8_t>(Ch));
        }
        AppendBigEndianU16(Out, 0u);
    }
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           IRIDAS / RESOLVE .CUBE 1D LUT
//------------------------------------------------------------------------------------------------------------------------

bool ColourInterchange::ImportCubeLut1DText(
    std::string_view                  CubeText,
    ColourSpace::AuthoredColourRamp&  OutRamp,
    std::string&                      OutDiagnostic)
{
    std::istringstream Stream { std::string(CubeText) };
    std::string Line;
    OutRamp                = ColourSpace::AuthoredColourRamp {};
    OutRamp.DisplayTitle   = "ImportedCubeLut";
    OutRamp.RampIdentifier = "imported_cube";
    OutRamp.SeriesTitle    = "ImportedCUBE";

    std::vector<ColourSpace::LinearRgba> Samples {};
    while (std::getline(Stream, Line))
    {
        const std::string Trimmed = TrimAscii(Line);
        if (Trimmed.empty() || Trimmed[0] == '#') continue;
        if (Trimmed.rfind("TITLE", 0u) == 0u)
        {
            const std::size_t FirstQ = Trimmed.find('"');
            const std::size_t LastQ  = Trimmed.rfind('"');
            if (FirstQ != std::string::npos && LastQ != std::string::npos && LastQ > FirstQ + 1u)
            {
                OutRamp.DisplayTitle   = Trimmed.substr(FirstQ + 1u, LastQ - FirstQ - 1u);
                OutRamp.RampIdentifier = LowercaseAscii(OutRamp.DisplayTitle);
            }
            continue;
        }
        if (Trimmed.rfind("LUT_1D_SIZE", 0u) == 0u || Trimmed.rfind("DOMAIN_MIN", 0u) == 0u || Trimmed.rfind("DOMAIN_MAX", 0u) == 0u)
        {
            continue;
        }
        std::istringstream Row(Trimmed);
        float R = 0.0f, G = 0.0f, B = 0.0f;
        if (Row >> R >> G >> B)
        {
            Samples.push_back({ R, G, B, 1.0f });
        }
    }

    if (Samples.size() < 2u)
    {
        OutDiagnostic = "No 1D RGB triplets found in .cube LUT file.";
        return false;
    }

    std::vector<ColourSpace::ColourStop> Stops {};
    Stops.reserve(Samples.size());
    for (std::size_t I = 0u; I < Samples.size(); ++I)
    {
        const float T = static_cast<float>(I) / static_cast<float>(Samples.size() - 1u);
        Stops.push_back({ T, Samples[I] });
    }

    ColourSpace::MultiStopGradientSource Gradient {};
    Gradient.Stops         = std::move(Stops);
    OutRamp.Profile.Source = std::move(Gradient);
    return true;
}

std::string ColourInterchange::ExportCubeLut1DText(
    const ColourSpace::ColourSourceProfile& Profile,
    std::string_view                        Title,
    uint32_t                                SampleLen)
{
    const uint32_t Count = std::clamp(SampleLen, 2u, 256u);
    const std::vector<ColourSpace::ColourStop> Stops = ColourSpace::ExtractOrderedStops(Profile.Source);
    std::ostringstream Stream;
    Stream << "TITLE \"" << Title << "\"\n";
    Stream << "LUT_1D_SIZE " << Count << "\n";
    Stream << "DOMAIN_MIN 0.0 0.0 0.0\n";
    Stream << "DOMAIN_MAX 1.0 1.0 1.0\n";
    Stream << std::fixed << std::setprecision(6);

    for (uint32_t I = 0u; I < Count; ++I)
    {
        const float T = static_cast<float>(I) / static_cast<float>(Count - 1u);
        const ColourSpace::LinearRgba C = ColourSpace::SampleColourStopsAt(Stops, T, Profile.Range);
        Stream << C.Red << " " << C.Green << " " << C.Blue << "\n";
    }
    return Stream.str();
}

//------------------------------------------------------------------------------------------------------------------------
//                                           UNIFIED FILE-LEVEL DISPATCH
//------------------------------------------------------------------------------------------------------------------------

bool ColourInterchange::ImportFromFile(
    const std::filesystem::path& SourcePath,
    ColourSpace::ColourDocument& OutDocument,
    std::string&                 OutDiagnostic)
{
    const ExternalColourFormat Format = DetectFormatFromPath(SourcePath);
    if (Format == ExternalColourFormat::Unknown)
    {
        OutDiagnostic = "Unrecognized external colour file extension.";
        return false;
    }

    std::ifstream File(SourcePath, std::ios::binary);
    if (!File.is_open())
    {
        OutDiagnostic = "Failed to open external colour file for reading.";
        return false;
    }

    if (Format == ExternalColourFormat::AdobeSwatchAse || Format == ExternalColourFormat::AdobeColourAco)
    {
        const std::vector<char> Raw((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
        const std::span<const std::byte> ByteSpan(reinterpret_cast<const std::byte*>(Raw.data()), Raw.size());
        return (Format == ExternalColourFormat::AdobeSwatchAse)
            ? ImportAdobeAseBytes(ByteSpan, OutDocument, OutDiagnostic)
            : ImportAdobeAcoBytes(ByteSpan, OutDocument, OutDiagnostic);
    }

    std::ostringstream TextStream;
    TextStream << File.rdbuf();
    const std::string Text = TextStream.str();

    if (Format == ExternalColourFormat::GimpSwatchGpl)
    {
        return ImportGimpGplText(Text, OutDocument, OutDiagnostic);
    }
    if (Format == ExternalColourFormat::GimpGradientGgr)
    {
        ColourSpace::AuthoredColourRamp Ramp {};
        if (!ImportGimpGgrText(Text, Ramp, OutDiagnostic)) return false;
        OutDocument.UpsertRamp(std::move(Ramp));
        return true;
    }
    if (Format == ExternalColourFormat::CubeLut1D)
    {
        ColourSpace::AuthoredColourRamp Ramp {};
        if (!ImportCubeLut1DText(Text, Ramp, OutDiagnostic)) return false;
        OutDocument.UpsertRamp(std::move(Ramp));
        return true;
    }

    ColourSpace::AuthoredColourRamp Ramp {};
    Ramp.RampIdentifier = LowercaseAscii(SourcePath.stem().string());
    Ramp.DisplayTitle   = SourcePath.stem().string();
    Ramp.SeriesTitle    = "Imported";

    bool Parsed = false;
    if (Format == ExternalColourFormat::HtmlStudioJson)    Parsed = ImportHtmlStudioJsonBlock(Text, Ramp.Profile, OutDiagnostic);
    else if (Format == ExternalColourFormat::CssGradient)  Parsed = ImportCssGradientText(Text, Ramp.Profile, OutDiagnostic);
    else if (Format == ExternalColourFormat::SvgGradient)  Parsed = ImportSvgGradientText(Text, Ramp.Profile, OutDiagnostic);
    if (!Parsed) return false;

    OutDocument.UpsertRamp(std::move(Ramp));
    return true;
}

bool ColourInterchange::ExportToFile(
    const std::filesystem::path&       TargetPath,
    const ColourSpace::ColourDocument& Document,
    std::string&                       OutDiagnostic)
{
    const ExternalColourFormat Format = DetectFormatFromPath(TargetPath);
    if (Format == ExternalColourFormat::Unknown)
    {
        OutDiagnostic = "Unsupported external colour export extension.";
        return false;
    }

    std::error_code FsError;
    if (TargetPath.has_parent_path())
    {
        std::filesystem::create_directories(TargetPath.parent_path(), FsError);
    }

    std::ofstream File(TargetPath, std::ios::binary | std::ios::trunc);
    if (!File.is_open())
    {
        OutDiagnostic = "Unable to open external colour export path for writing.";
        return false;
    }

    if (Format == ExternalColourFormat::AdobeSwatchAse || Format == ExternalColourFormat::AdobeColourAco)
    {
        const auto Bytes = (Format == ExternalColourFormat::AdobeSwatchAse)
            ? ExportAdobeAseBytes(Document)
            : ExportAdobeAcoBytes(Document);
        File.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
        return File.good();
    }

    std::string Payload {};
    const ColourSpace::AuthoredColourRamp FallbackRamp { "default", Document.DocumentTitle, "Export", {} };
    const ColourSpace::AuthoredColourRamp& PrimaryRamp = Document.Ramps.empty() ? FallbackRamp : Document.Ramps.front();

    if (Format == ExternalColourFormat::HtmlStudioJson)      Payload = ExportHtmlStudioJsonBlock(PrimaryRamp.Profile);
    else if (Format == ExternalColourFormat::CssGradient)    Payload = ExportCssGradientText(PrimaryRamp.Profile);
    else if (Format == ExternalColourFormat::SvgGradient)    Payload = ExportSvgGradientText(PrimaryRamp.Profile, PrimaryRamp.RampIdentifier);
    else if (Format == ExternalColourFormat::GimpSwatchGpl)  Payload = ExportGimpGplText(Document);
    else if (Format == ExternalColourFormat::GimpGradientGgr)Payload = ExportGimpGgrText(PrimaryRamp);
    else if (Format == ExternalColourFormat::CubeLut1D)      Payload = ExportCubeLut1DText(PrimaryRamp.Profile, PrimaryRamp.DisplayTitle, 64u);

    File.write(Payload.data(), static_cast<std::streamsize>(Payload.size()));
    if (!File.good())
    {
        OutDiagnostic = "Failed while writing external colour export file.";
        return false;
    }
    return true;
}

} // namespace Frontier::Interchange
