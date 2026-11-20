/*============================================================================================================================================
                                                           PHOTOMETRICPROFILE.CPP
============================================================================================================================================*/
// 🧩 ANSI/IESNA LM-63-1995/2002 Type C (.ies) photometric profile parser, symmetry folder, and 128x32 angular candela resampler

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/PhotometricProfile.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace Frontier::Codecs
{

namespace
{

constexpr int32_t Lm63PhotometricTypeC = 1;

[[nodiscard]] std::string TrimAsciiWhitespace(std::string_view Input)
{
    std::size_t First = 0u;
    while (First < Input.size() && std::isspace(static_cast<unsigned char>(Input[First])) != 0)
    {
        ++First;
    }
    std::size_t Last = Input.size();
    while (Last > First && std::isspace(static_cast<unsigned char>(Input[Last - 1u])) != 0)
    {
        --Last;
    }
    return std::string(Input.substr(First, Last - First));
}

[[nodiscard]] std::string ToUpperAscii(std::string_view Input)
{
    std::string Upper(Input);
    for (char& Ch : Upper)
    {
        Ch = static_cast<char>(std::toupper(static_cast<unsigned char>(Ch)));
    }
    return Upper;
}

[[nodiscard]] float FoldHorizontalAngleDegrees(const std::vector<float>& HorizontalAngles, float WrappedDegrees) noexcept
{
    if (HorizontalAngles.empty())
    {
        return 0.0f;
    }
    if (HorizontalAngles.size() == 1u)
    {
        return HorizontalAngles.front();
    }
    const float First = HorizontalAngles.front();
    const float Last  = HorizontalAngles.back();

    if ( std::fabs(First - 0.0f) < 1.0e-4f && std::fabs(Last - 90.0f) < 1.0e-4f)
    {
        const float Mod180 = std::fmod(WrappedDegrees, 180.0f);
        return (Mod180 > 90.0f) ? (180.0f - Mod180) : Mod180;
    }
    if (std::fabs(First - 0.0f) < 1.0e-4f && std::fabs(Last - 180.0f) < 1.0e-4f)
    {
        return (WrappedDegrees > 180.0f) ? (360.0f - WrappedDegrees) : WrappedDegrees;
    }
    if (std::fabs(First - 90.0f) < 1.0e-4f && std::fabs(Last - 270.0f) < 1.0e-4f)
    {
        if (WrappedDegrees < 90.0f)
        {
            return 180.0f - WrappedDegrees;
        }
        if (WrappedDegrees > 270.0f)
        {
            return 540.0f - WrappedDegrees;
        }
        return WrappedDegrees;
    }
    return WrappedDegrees;
}

struct AngleBracketSpan
{
    std::size_t LowerIndex = 0u;
    std::size_t UpperIndex = 0u;
    float       Fraction   = 0.0f;
};

[[nodiscard]] AngleBracketSpan BracketSortedAngles(const std::vector<float>& Angles, float TargetDegrees) noexcept
{
    if (Angles.empty())
    {
        return {};
    }
    if (TargetDegrees <= Angles.front())
    {
        return { 0u, 0u, 0.0f };
    }
    const std::size_t Last = Angles.size() - 1u;
    if (TargetDegrees >= Angles[Last])
    {
        return { Last, Last, 0.0f };
    }
    const auto It          = std::upper_bound(Angles.begin(), Angles.end(), TargetDegrees);
    const std::size_t High = static_cast<std::size_t>(std::distance(Angles.begin(), It));
    const std::size_t Low  = (High > 0u) ? (High - 1u) : 0u;
    const float Denom      = std::max(1.0e-6f, Angles[High] - Angles[Low]);
    return { Low, High, std::clamp((TargetDegrees - Angles[Low]) / Denom, 0.0f, 1.0f) };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  PUBLIC IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

bool PhotometricProfileCodec::ParseLm63Text(
    std::string_view            SourceText,
    std::string_view            ProfileLabel,
    RawPhotometricDistribution& OutDistribution,
    std::string&                OutDiagnostic)
{
    OutDistribution              = {};
    OutDistribution.ProfileLabel = std::string(ProfileLabel);
    OutDiagnostic.clear();

    std::vector<std::string> Lines;
    {
        std::string Normalized;
        Normalized.reserve(SourceText.size());
        for (char Ch : SourceText)
        {
            if (Ch != '\r')
            {
                Normalized.push_back(Ch);
            }
        }
        std::istringstream LineStream(Normalized);
        std::string        Line;
        while (std::getline(LineStream, Line))
        {
            Lines.push_back(std::move(Line));
        }
    }

    std::size_t TiltLineIndex = Lines.size();
    std::string TiltDirective {};
    for (std::size_t Index = 0u; Index < Lines.size(); ++Index)
    {
        const std::string Trimmed = TrimAsciiWhitespace(Lines[Index]);
        const std::string Upper   = ToUpperAscii(Trimmed);
        if (Upper.rfind("TILT", 0u) == 0u)
        {
            const std::size_t EqPos = Trimmed.find('=');
            if (EqPos != std::string::npos)
            {
                TiltLineIndex = Index;
                TiltDirective = ToUpperAscii(TrimAsciiWhitespace(std::string_view(Trimmed).substr(EqPos + 1u)));
                break;
            }
        }
    }

    if (TiltLineIndex == Lines.size())
    {
        OutDiagnostic = OutDistribution.ProfileLabel + ": missing TILT= directive (not an LM-63 photometric file)";
        return false;
    }

    for (std::size_t Index = 0u; Index < TiltLineIndex; ++Index)
    {
        const std::string Trimmed = TrimAsciiWhitespace(Lines[Index]);
        if (!Trimmed.empty() && Trimmed.front() == '[')
        {
            const std::size_t ClosePos = Trimmed.find(']');
            if (ClosePos != std::string::npos && ClosePos > 1u)
            {
                std::string Key = ToUpperAscii(TrimAsciiWhitespace(std::string_view(Trimmed).substr(1u, ClosePos - 1u)));
                std::string Txt = TrimAsciiWhitespace(std::string_view(Trimmed).substr(ClosePos + 1u));
                OutDistribution.KeywordEntries.emplace_back(std::move(Key), std::move(Txt));
            }
        }
    }

    std::vector<double> NumericTokens;
    NumericTokens.reserve(1024u);
    for (std::size_t Index = TiltLineIndex + 1u; Index < Lines.size(); ++Index)
    {
        const std::string& Line = Lines[Index];
        std::size_t Pos = 0u;
        while (Pos < Line.size())
        {
            while (Pos < Line.size() && (std::isspace(static_cast<unsigned char>(Line[Pos])) != 0 || Line[Pos] == ','))
            {
                ++Pos;
            }
            if (Pos >= Line.size())
            {
                break;
            }
            std::size_t EndPos = Pos;
            while (EndPos < Line.size() && std::isspace(static_cast<unsigned char>(Line[EndPos])) == 0 && Line[EndPos] != ',')
            {
                ++EndPos;
            }
            const std::string TokenStr = Line.substr(Pos, EndPos - Pos);
            char* ParseEnd             = nullptr;
            const double ParsedScalar  = std::strtod(TokenStr.c_str(), &ParseEnd);
            if (ParseEnd == TokenStr.c_str() || !std::isfinite(ParsedScalar))
            {
                OutDiagnostic = OutDistribution.ProfileLabel + ": malformed numeric token '" + TokenStr + "'";
                return false;
            }
            NumericTokens.push_back(ParsedScalar);
            Pos = EndPos;
        }
    }

    std::size_t Cursor = 0u;
    const auto ConsumeScalars = [&](std::size_t Count, std::vector<double>& OutSlice) -> bool
    {
        if (Cursor + Count > NumericTokens.size())
        {
            OutDiagnostic = OutDistribution.ProfileLabel + ": truncated numeric block in LM-63 body";
            return false;
        }
        OutSlice.assign(
            NumericTokens.begin() + static_cast<std::ptrdiff_t>(Cursor),
            NumericTokens.begin() + static_cast<std::ptrdiff_t>(Cursor + Count));
        Cursor += Count;
        return true;
    };

    std::vector<double> HeaderSlice;
    if (TiltDirective == "INCLUDE")
    {
        if (!ConsumeScalars(2u, HeaderSlice))
        {
            return false;
        }
        const int32_t TiltPairs = static_cast<int32_t>(std::lround(HeaderSlice[1]));
        if (TiltPairs < 0 || !ConsumeScalars(static_cast<std::size_t>(TiltPairs) * 2u, HeaderSlice))
        {
            OutDiagnostic = OutDistribution.ProfileLabel + ": invalid TILT=INCLUDE table";
            return false;
        }
    }
    else if (TiltDirective != "NONE")
    {
        OutDiagnostic = OutDistribution.ProfileLabel + ": external TILT file references are unsupported";
        return false;
    }

    if (!ConsumeScalars(13u, HeaderSlice))
    {
        return false;
    }

    const double  Multiplier      = HeaderSlice[2];
    const int32_t VerticalCount   = static_cast<int32_t>(std::lround(HeaderSlice[3]));
    const int32_t HorizontalCount = static_cast<int32_t>(std::lround(HeaderSlice[4]));
    const int32_t PhotometricType = static_cast<int32_t>(std::lround(HeaderSlice[5]));
    const double  BallastFactor   = (HeaderSlice[10] != 0.0) ? HeaderSlice[10] : 1.0;
    const double  LampFactor      = (HeaderSlice[11] != 0.0) ? HeaderSlice[11] : 1.0;

    if (PhotometricType != Lm63PhotometricTypeC)
    {
        OutDiagnostic = OutDistribution.ProfileLabel + ": unsupported LM-63 photometric type (only Type C = 1 is supported)";
        return false;
    }
    if (VerticalCount < 2 || HorizontalCount < 1)
    {
        OutDiagnostic = OutDistribution.ProfileLabel + ": invalid angular sample counts";
        return false;
    }

    std::vector<double> VertDouble;
    std::vector<double> HorizDouble;
    if (!ConsumeScalars(static_cast<std::size_t>(VerticalCount), VertDouble) ||
        !ConsumeScalars(static_cast<std::size_t>(HorizontalCount), HorizDouble))
    {
        return false;
    }

    OutDistribution.VerticalAngles.resize(static_cast<std::size_t>(VerticalCount));
    for (int32_t V = 0; V < VerticalCount; ++V)
    {
        OutDistribution.VerticalAngles[static_cast<std::size_t>(V)] = static_cast<float>(VertDouble[static_cast<std::size_t>(V)]);
    }
    OutDistribution.HorizontalAngles.resize(static_cast<std::size_t>(HorizontalCount));
    for (int32_t H = 0; H < HorizontalCount; ++H)
    {
        OutDistribution.HorizontalAngles[static_cast<std::size_t>(H)] = static_cast<float>(HorizDouble[static_cast<std::size_t>(H)]);
    }

    const double ScaleFactor = Multiplier * BallastFactor * LampFactor;
    const std::size_t TotalEntries = static_cast<std::size_t>(HorizontalCount) * static_cast<std::size_t>(VerticalCount);
    std::vector<double> RawCandela;
    if (!ConsumeScalars(TotalEntries, RawCandela))
    {
        return false;
    }

    OutDistribution.CandelaMatrix.resize(TotalEntries, 0.0f);
    float Peak = 0.0f;
    for (std::size_t Index = 0u; Index < TotalEntries; ++Index)
    {
        const float Scaled = static_cast<float>(std::max(0.0, RawCandela[Index] * ScaleFactor));
        OutDistribution.CandelaMatrix[Index] = Scaled;
        Peak = std::max(Peak, Scaled);
    }
    OutDistribution.PeakCandela = Peak;
    return true;
}

bool PhotometricProfileCodec::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    RawPhotometricDistribution&  OutDistribution,
    std::string&                 OutDiagnostic)
{
    std::ifstream FileInput(SourcePath, std::ios::binary);
    if (!FileInput.is_open())
    {
        OutDiagnostic = "Unable to open photometric file: " + SourcePath.generic_string();
        return false;
    }
    std::ostringstream Buffer;
    Buffer << FileInput.rdbuf();
    return ParseLm63Text(Buffer.str(), SourcePath.filename().generic_string(), OutDistribution, OutDiagnostic);
}

float PhotometricProfileCodec::EvaluateCandelaAtDegrees(
    const RawPhotometricDistribution& Distribution,
    float                             VerticalDegrees,
    float                             HorizontalDegrees) noexcept
{
    const auto& Vert = Distribution.VerticalAngles;
    const auto& Horz = Distribution.HorizontalAngles;
    if (Vert.empty() || Horz.empty() || Distribution.CandelaMatrix.empty())
    {
        return 0.0f;
    }
    if (VerticalDegrees < Vert.front() - 1.0e-4f || VerticalDegrees > Vert.back() + 1.0e-4f)
    {
        return 0.0f;
    }

    float WrappedHorz = std::fmod(HorizontalDegrees, 360.0f);
    if (WrappedHorz < 0.0f)
    {
        WrappedHorz += 360.0f;
    }
    const float FoldedHorz = FoldHorizontalAngleDegrees(Horz, WrappedHorz);

    const AngleBracketSpan VSpan = BracketSortedAngles(Vert, VerticalDegrees);
    const AngleBracketSpan HSpan = BracketSortedAngles(Horz, FoldedHorz);
    const std::size_t VCount     = Vert.size();

    const auto SampleRow = [&](std::size_t HRow) noexcept -> float
    {
        const float C0 = Distribution.CandelaMatrix[HRow * VCount + VSpan.LowerIndex];
        const float C1 = Distribution.CandelaMatrix[HRow * VCount + VSpan.UpperIndex];
        return C0 * (1.0f - VSpan.Fraction) + C1 * VSpan.Fraction;
    };

    const float NearRow = SampleRow(HSpan.LowerIndex);
    const float FarRow  = SampleRow(HSpan.UpperIndex);
    return NearRow * (1.0f - HSpan.Fraction) + FarRow * HSpan.Fraction;
}

bool PhotometricProfileCodec::ResampleToCanonicalGrid(
    const RawPhotometricDistribution& Distribution,
    ResampledPhotometricProfile&      OutProfile,
    std::string&                      OutDiagnostic)
{
    OutProfile              = {};
    OutProfile.ProfileLabel = Distribution.ProfileLabel;
    OutDiagnostic.clear();

    float Peak = 0.0f;
    for (uint32_t H = 0u; H < PhotometricHorizontalCount; ++H)
    {
        const float AzimuthDeg = static_cast<float>(H) * (360.0f / static_cast<float>(PhotometricHorizontalCount));
        for (uint32_t V = 0u; V < PhotometricVerticalCount; ++V)
        {
            const float PolarDeg = static_cast<float>(V) * (180.0f / static_cast<float>(PhotometricVerticalCount - 1u));
            const float Candela  = EvaluateCandelaAtDegrees(Distribution, PolarDeg, AzimuthDeg);
            OutProfile.NormalizedCandela[H * PhotometricVerticalCount + V] = Candela;
            Peak = std::max(Peak, Candela);
        }
    }

    if (Peak <= 0.0f)
    {
        OutDiagnostic = Distribution.ProfileLabel + ": resampled photometric table has zero luminous output";
        return false;
    }

    OutProfile.PeakCandela = Peak;
    uint32_t LastNonZeroPolar = 0u;
    double   SolidAngleFlux   = 0.0;
    const double PolarStepRad = Constants::Pi / static_cast<double>(PhotometricVerticalCount - 1u);

    for (uint32_t V = 0u; V < PhotometricVerticalCount; ++V)
    {
        double RowSum = 0.0;
        for (uint32_t H = 0u; H < PhotometricHorizontalCount; ++H)
        {
            const uint32_t TableIdx = H * PhotometricVerticalCount + V;
            OutProfile.NormalizedCandela[TableIdx] /= Peak;
            RowSum += static_cast<double>(OutProfile.NormalizedCandela[TableIdx]);
            if (OutProfile.NormalizedCandela[TableIdx] > 0.0f)
            {
                LastNonZeroPolar = V;
            }
        }
        const double EndpointWeight = (V == 0u || V == PhotometricVerticalCount - 1u) ? 0.5 : 1.0;
        const double MeanRow        = RowSum / static_cast<double>(PhotometricHorizontalCount);
        SolidAngleFlux += EndpointWeight * MeanRow * Constants::Tau *
                          std::sin(static_cast<double>(V) * PolarStepRad) * PolarStepRad;
    }

    OutProfile.NormalizedFluxSr     = static_cast<float>(SolidAngleFlux);
    OutProfile.EmissionHalfAngleRad = static_cast<float>(
        std::min(Constants::Pi, static_cast<double>(LastNonZeroPolar + 1u) * PolarStepRad));
    return true;
}

bool PhotometricProfileCodec::LoadResampledProfileFromFile(
    const std::filesystem::path& SourcePath,
    ResampledPhotometricProfile& OutProfile,
    std::string&                 OutDiagnostic)
{
    RawPhotometricDistribution Raw {};
    if (!DecodeFromFile(SourcePath, Raw, OutDiagnostic))
    {
        return false;
    }
    return ResampleToCanonicalGrid(Raw, OutProfile, OutDiagnostic);
}

float PhotometricProfileCodec::EvaluateNormalizedIntensity(
    const ResampledPhotometricProfile& Profile,
    Mathematics::Axis3D                UnitLightToSurface,
    Mathematics::Axis3D                UnitBeamAxis,
    Mathematics::Axis3D                UnitZeroAzimuthAxis) noexcept
{
    const float CosTheta    = std::clamp(Mathematics::EvaluateDot(UnitBeamAxis, UnitLightToSurface), -1.0f, 1.0f);
    const float VerticalPos = std::acos(CosTheta) * (static_cast<float>(PhotometricVerticalCount - 1u) / Constants::PiFloat);

    const Mathematics::Axis3D OrthoBitangent = Mathematics::EvaluateCross(UnitBeamAxis, UnitZeroAzimuthAxis);
    float HorizontalPos = std::atan2(
                              Mathematics::EvaluateDot(UnitLightToSurface, OrthoBitangent),
                              Mathematics::EvaluateDot(UnitLightToSurface, UnitZeroAzimuthAxis)) *
                          (static_cast<float>(PhotometricHorizontalCount) / Constants::TauFloat);
    if (HorizontalPos < 0.0f)
    {
        HorizontalPos += static_cast<float>(PhotometricHorizontalCount);
    }

    const uint32_t V0 = std::min(static_cast<uint32_t>(VerticalPos), PhotometricVerticalCount - 2u);
    const float    Vt = std::clamp(VerticalPos - static_cast<float>(V0), 0.0f, 1.0f);
    const uint32_t H0 = std::min(static_cast<uint32_t>(HorizontalPos), PhotometricHorizontalCount - 1u);
    const float    Ht = std::clamp(HorizontalPos - static_cast<float>(H0), 0.0f, 1.0f);
    const uint32_t H1 = (H0 + 1u) % PhotometricHorizontalCount;

    const uint32_t I00 = H0 * PhotometricVerticalCount + V0;
    const uint32_t I10 = H1 * PhotometricVerticalCount + V0;

    const float NearVal = Profile.NormalizedCandela[I00] * (1.0f - Vt) + Profile.NormalizedCandela[I00 + 1u] * Vt;
    const float FarVal  = Profile.NormalizedCandela[I10] * (1.0f - Vt) + Profile.NormalizedCandela[I10 + 1u] * Vt;
    return NearVal * (1.0f - Ht) + FarVal * Ht;
}

bool PhotometricProfileCodec::LoadCanonicalPhotometrySuite(
    const std::filesystem::path&              RepositoryRoot,
    std::vector<ResampledPhotometricProfile>& OutSuite,
    std::string&                              OutDiagnostic)
{
    OutSuite.clear();
    OutDiagnostic.clear();

    constexpr std::array<const char*, 3u> CanonicalFiles {{
        "LowBeamHeadlamp.ies",
        "TailLamp.ies",
        "CarParkDownlight.ies",
    }};

    const std::array<std::filesystem::path, 4u> SearchDirectories {{
        RepositoryRoot / "EngineContent/Photometry",
        RepositoryRoot / "Experimental/DeterministicPathTracer/Assets/Photometry",
        std::filesystem::path("EngineContent/Photometry"),
        std::filesystem::path("Experimental/DeterministicPathTracer/Assets/Photometry"),
    }};

    OutSuite.reserve(CanonicalFiles.size());
    for (const char* FileName : CanonicalFiles)
    {
        std::filesystem::path FoundPath {};
        for (const auto& Dir : SearchDirectories)
        {
            const std::filesystem::path Candidate = Dir / FileName;
            std::error_code FsErr {};
            if (std::filesystem::exists(Candidate, FsErr) && !FsErr)
            {
                FoundPath = Candidate;
                break;
            }
        }
        if (FoundPath.empty())
        {
            OutDiagnostic = std::string("Missing canonical photometric profile: ") + FileName;
            return false;
        }
        ResampledPhotometricProfile Loaded {};
        if (!LoadResampledProfileFromFile(FoundPath, Loaded, OutDiagnostic))
        {
            return false;
        }
        OutSuite.push_back(std::move(Loaded));
    }
    return true;
}

} // namespace Frontier::Codecs
