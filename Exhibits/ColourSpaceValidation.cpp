/*============================================================================================================================================
                                                          COLOURSPACEVALIDATION.CPP
============================================================================================================================================*/
// 🎨 Comprehensive validation & visual proof sheet for Engine/ColourSpace (colorimetry, transfer curves, Kelvin, gamut, 7 sources, HTML parity, LUTs, ColourExtension)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ChromaticitySpace.h"
#include "Engine/ColourSpace/ColourCodex.h"
#include "Engine/ColourSpace/ColourDocument.h"
#include "Engine/ColourSpace/ColourExtension.h"
#include "Engine/ColourSpace/ColourPresetRepository.h"
#include "Engine/ColourSpace/ColourRampSampler.h"
#include "Engine/ColourSpace/GamutCompression.h"
#include "Engine/ColourSpace/SpectralDistribution.h"
#include "Engine/ColourSpace/TransferFunction.h"
#include "Engine/ColourSpace/WhitePointAdaptation.h"
#include "Engine/Workspaces/WorkspaceExchange.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace
{

using namespace Frontier::ColourSpace;
using Frontier::Components::RgbaColor8;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;

bool VerifyCheck(bool Condition, const char* Message) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[ColourSpaceValidation] FAILED: %s\n", Message);
        return false;
    }
    return true;
}

bool NearEqual(float A, float B, float Tolerance = 1.0e-3f) noexcept
{
    return std::fabs(A - B) <= Tolerance;
}

bool ValidateColorimetryRoundTrips() noexcept
{
    // 1. Transfer curves round-trip (PiecewiseSrgb, PureGamma22, ST2084 PQ, HLG)
    const TransferCurveCategory Curves[] = {
        TransferCurveCategory::PiecewiseSrgb,
        TransferCurveCategory::PureGamma22,
        TransferCurveCategory::PerceptualPq,
        TransferCurveCategory::HybridLogGamma,
    };
    for (TransferCurveCategory Curve : Curves)
    {
        for (float Sample : { 0.0f, 0.02f, 0.18f, 0.5f, 0.85f, 1.0f })
        {
            const float Encoded = EncodeTransferChannel(Sample, Curve);
            const float Decoded = DecodeTransferChannel(Encoded, Curve);
            if (!VerifyCheck(NearEqual(Sample, Decoded, 2.0e-4f), "TransferCurve EOTF/OETF round-trip"))
            {
                return false;
            }
        }
    }

    // 2. Primaries round-trip: LinearSrgb <-> ACEScg <-> Rec2020 <-> DisplayP3
    const LinearRgba Seed { 0.62f, 0.28f, 0.14f, 0.9f };
    const ChromaticityPrimaries Targets[] = {
        ChromaticityPrimaries::AcesCg,
        ChromaticityPrimaries::Rec2020,
        ChromaticityPrimaries::DisplayP3,
    };
    for (ChromaticityPrimaries Target : Targets)
    {
        const LinearRgba Forward = ConvertPrimaries(Seed, ChromaticityPrimaries::LinearSrgbRec709, Target);
        const LinearRgba Back    = ConvertPrimaries(Forward, Target, ChromaticityPrimaries::LinearSrgbRec709);
        if (!VerifyCheck(
                NearEqual(Seed.Red, Back.Red, 2.0e-3f)
                && NearEqual(Seed.Green, Back.Green, 2.0e-3f)
                && NearEqual(Seed.Blue, Back.Blue, 2.0e-3f)
                && NearEqual(Seed.Alpha, Back.Alpha, 1.0e-5f),
                "Primaries + Bradford adaptation round-trip"))
        {
            return false;
        }
    }

    // 3. White-point adaptation D65 -> D50 -> D65 (Bradford & CAT02)
    const CieXyzTriplet WhiteD65 = QueryIlluminantWhiteXyz(StandardIlluminant::D65);
    const CieXyzTriplet AdaptedD50 = AdaptXyzWhitePoint(WhiteD65, StandardIlluminant::D65, StandardIlluminant::D50, ChromaticAdaptationMethod::Bradford);
    const CieXyzTriplet RefD50     = QueryIlluminantWhiteXyz(StandardIlluminant::D50);
    if (!VerifyCheck(
            NearEqual(AdaptedD50.X, RefD50.X, 1.0e-3f)
            && NearEqual(AdaptedD50.Y, RefD50.Y, 1.0e-3f)
            && NearEqual(AdaptedD50.Z, RefD50.Z, 1.0e-3f),
            "Bradford D65 white maps to D50 white"))
    {
        return false;
    }

    // 4. Oklab / Oklch round-trip
    const OklabTriplet Lab   = ConvertLinearSrgbToOklab(Seed);
    const OklchTriplet Lch   = ConvertOklabToOklch(Lab);
    const OklabTriplet Lab2  = ConvertOklchToOklab(Lch);
    const LinearRgba   BackL = ConvertOklabToLinearSrgb(Lab2, Seed.Alpha);
    if (!VerifyCheck(
            NearEqual(Seed.Red, BackL.Red, 1.0e-3f)
            && NearEqual(Seed.Green, BackL.Green, 1.0e-3f)
            && NearEqual(Seed.Blue, BackL.Blue, 1.0e-3f),
            "LinearSrgb <-> Oklab <-> Oklch round-trip"))
    {
        return false;
    }

    // 5. Hex / HSL / CMYK / WCAG contrast ink
    SrgbRgba8 Parsed {};
    if (!VerifyCheck(TryParseHexSrgb("#2454FF", Parsed), "TryParseHexSrgb(#2454FF)")
        || !VerifyCheck(Parsed.Red == 0x24u && Parsed.Green == 0x54u && Parsed.Blue == 0xFFu, "Parsed #2454FF bytes exact")
        || !VerifyCheck(FormatHexUpper(Parsed) == "#2454FF", "FormatHexUpper round-trip"))
    {
        return false;
    }
    const HslTriplet Hsl     = ConvertSrgb8ToHsl(Parsed);
    const SrgbRgba8  HslBack = ConvertHslToSrgb8(Hsl);
    if (!VerifyCheck(
            std::abs(static_cast<int>(HslBack.Red) - static_cast<int>(Parsed.Red)) <= 2
            && std::abs(static_cast<int>(HslBack.Green) - static_cast<int>(Parsed.Green)) <= 2
            && std::abs(static_cast<int>(HslBack.Blue) - static_cast<int>(Parsed.Blue)) <= 2,
            "Srgb8 <-> HSL round-trip"))
    {
        return false;
    }

    const ContrastInkSet YellowInk = SelectContrastInk({ 255u, 230u, 32u, 255u });
    const ContrastInkSet NavyInk   = SelectContrastInk({ 18u, 22u, 58u, 255u });
    if (!VerifyCheck(YellowInk.DarkInk && !NavyInk.DarkInk, "WCAG SelectContrastInk distinguishes bright vs dark fills"))
    {
        return false;
    }

    // 6. Spectral Planckian curve & Gamut compression
    const SpectralCurveProfile WarmSpectrum = ConstructBlackbodySpectrum(3200.0f);
    const LinearRgba WarmRgb = ConvertSpectralToLinearRgb(WarmSpectrum);
    if (!VerifyCheck(WarmRgb.Red > WarmRgb.Blue && WarmRgb.Green > 0.0f, "3200K blackbody spectrum biases red > blue"))
    {
        return false;
    }

    const LinearRgba OutOfGamut { 1.35f, -0.22f, 0.15f, 1.0f };
    const LinearRgba CompressedAces = CompressWideGamutDistance(OutOfGamut);
    const LinearRgba CompressedLab  = CompressOklabChromaToSrgb({ 0.9f, -0.18f, 0.08f, 1.0f });
    if (!VerifyCheck(CompressedAces.Green >= 0.0f && VerifyInsideUnitGamut(CompressedLab), "Gamut compression brings negative channels cleanly into unit gamut"))
    {
        return false;
    }

    return true;
}

bool ValidateModularSources()
{
    // Verify all 9 built-in ramp presets exist (including the 6 verbatim HTML Studio presets)
    const auto& BuiltIns = QueryBuiltInColourRampPresets();
    if (!VerifyCheck(BuiltIns.size() >= 9u, "ColourPresetRepository ships >= 9 built-in ramp presets"))
    {
        return false;
    }
    for (const char* StudioId : { "rainbow", "thermal", "aurora", "plasma", "ocean", "sunset" })
    {
        if (!VerifyCheck(FindBuiltInColourRampPreset(StudioId) != nullptr, "Built-in HTML Studio preset found"))
        {
            return false;
        }
    }

    // Verify bit-parity with HTML PE.evalColorRampAt on "rainbow" at t = 0.0, 0.5, 1.0
    const ColourRampPresetEntry* Rainbow = FindBuiltInColourRampPreset("rainbow");
    const ColourSourceProfile RainbowProfile = ConstructProfileFromRampPreset(*Rainbow);
    const LinearRgba StartCol = SampleColourSource(RainbowProfile, { 0.0f });
    const LinearRgba MidCol   = SampleColourSource(RainbowProfile, { 0.5f }); // #38e86b exact stop 3/6
    const LinearRgba EndCol   = SampleColourSource(RainbowProfile, { 1.0f });
    const LinearRgba RefMid   = ConvertHexSrgbToLinear("#38e86b", 1.0f, TransferCurveCategory::PureGamma22);
    if (!VerifyCheck(NearEqual(MidCol.Red, RefMid.Red, 1.0e-5f)
                  && NearEqual(MidCol.Green, RefMid.Green, 1.0e-5f)
                  && NearEqual(MidCol.Blue, RefMid.Blue, 1.0e-5f),
                     "HTML rainbow stop 3/6 (#38e86b) matches bit-exact"))
    {
        return false;
    }
    if (!VerifyCheck(StartCol.Red > 0.9f && EndCol.Blue > 0.8f, "Rainbow endpoints span red -> violet"))
    {
        return false;
    }

    // Verify Repeat & Mirror wrap behaviour
    ColourSourceProfile MirrorProfile = RainbowProfile;
    MirrorProfile.Range.WrapBehaviour = ColourWrapBehaviour::Mirror;
    const LinearRgba M0 = SampleColourSource(MirrorProfile, { 0.25f });
    const LinearRgba M1 = SampleColourSource(MirrorProfile, { 1.75f }); // Mirrors back to 0.25
    if (!VerifyCheck(NearEqual(M0.Red, M1.Red, 1.0e-4f) && NearEqual(M0.Green, M1.Green, 1.0e-4f), "Mirror wrap symmetry"))
    {
        return false;
    }

    // Verify all 7 ColourSourceCategory variants sample cleanly
    {
        ColourSourceProfile SolidP {};
        SolidP.Source = SolidPigmentSource { { 0.2f, 0.4f, 0.8f, 1.0f } };
        const LinearRgba S = SampleColourSource(SolidP, { 0.7f });
        if (!VerifyCheck(NearEqual(S.Blue, 0.8f, 1.0e-5f), "SolidPigmentSource sampling")) return false;

        ColourSourceProfile FillP {};
        FillP.Source = LinearFillSource { { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f }, true };
        const LinearRgba FMid = SampleColourSource(FillP, { 0.5f });
        if (!VerifyCheck(NearEqual(FMid.Green, 1.0f, 1.0e-4f), "LinearFillSource midpoint ColC sampling")) return false;

        ColourSourceProfile QuantP {};
        QuantP.Source = QuantizedRampSource { Rainbow->Stops };
        const LinearRgba Q0 = SampleColourSource(QuantP, { 0.01f });
        const LinearRgba Q1 = SampleColourSource(QuantP, { 0.14f });
        if (!VerifyCheck(NearEqual(Q0.Red, Q1.Red, 1.0e-5f), "QuantizedRampSource stepped band constancy")) return false;

        ColourSourceProfile ThermalP {};
        ThermalP.Source = BlackbodyThermalSource { 1000.0f, 5500.0f, 1.0f, 0.0f };
        const LinearRgba Hot  = SampleColourSource(ThermalP, { 0.0f });
        const LinearRgba Cool = SampleColourSource(ThermalP, { 1.0f });
        if (!VerifyCheck(Hot.Blue > Cool.Blue && Hot.Alpha > 0.9f && Cool.Alpha < 0.05f, "BlackbodyThermalSource progression")) return false;

        ColourSourceProfile DiscreteP {};
        DiscreteP.Source = DiscreteTintSetSource {};
        const LinearRgba D0 = SampleColourSource(DiscreteP, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0u });
        const LinearRgba D1 = SampleColourSource(DiscreteP, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1u });
        if (!VerifyCheck(!NearEqual(D0.Red, D1.Red, 0.1f), "DiscreteTintSetSource ordinal lookup")) return false;

        ColourSourceProfile NoiseP {};
        NoiseP.Source = SpatialNoiseFillSource { 0.85f, 0.18f, Rainbow->Stops };
        const LinearRgba N0 = SampleColourSource(NoiseP, { 0.0f, 1.25f, 0.5f, 2.1f, 0.0f, 0u });
        const LinearRgba N1 = SampleColourSource(NoiseP, { 0.0f, 1.25f, 0.5f, 2.1f, 0.0f, 0u });
        if (!VerifyCheck(NearEqual(N0.Red, N1.Red, 1.0e-6f) && NearEqual(N0.Green, N1.Green, 1.0e-6f), "SpatialNoiseFillSource determinism")) return false;
    }

    // Verify 64-entry & 256-entry LUT packing matches direct sampling
    const ColourRampLut64  Lut64  = PackColourRampLut64(RainbowProfile);
    const ColourRampLut256 Lut256 = PackColourRampLut256(RainbowProfile);
    const LinearRgba SampleDirect = SampleColourSource(RainbowProfile, { 0.37f });
    const LinearRgba SampleL256   = SamplePackedLut256(Lut256, 0.37f);
    const LinearRgba SampleL64    = SamplePackedLut64(Lut64, 0.37f);
    if (!VerifyCheck(NearEqual(SampleDirect.Red, SampleL256.Red, 5.0e-3f)
                  && NearEqual(SampleDirect.Green, SampleL64.Green, 2.0e-2f),
                     "LUT64 & LUT256 sampling parity"))
    {
        return false;
    }

    // Verify ColourExtension lifecycle + WorkspaceExchange publication
    Frontier::Workspaces::WorkspaceExchange ExchangeBus {};
    ColourExtension Extension {};
    if (!VerifyCheck(Extension.Initialize(&ExchangeBus), "ColourExtension::Initialize"))
    {
        return false;
    }
    const ColourExtensionMetrics InitialMetrics = Extension.InspectMetrics();
    if (!VerifyCheck(InitialMetrics.Initialized && InitialMetrics.BakedLutCount >= 9u, "ColourExtension pre-baked >= 9 LUT rows"))
    {
        return false;
    }

    ColourRampPresetEntry CustomRamp {};
    CustomRamp.Identifier   = "frontier_cobalt_gold";
    CustomRamp.DisplayTitle = "Frontier Cobalt Gold";
    CustomRamp.SeriesTitle  = "Project";
    CustomRamp.Stops        = {
        { 0.0f, ConvertHexSrgbToLinear("#0B1633", 1.0f) },
        { 0.5f, ConvertHexSrgbToLinear("#2454FF", 1.0f) },
        { 1.0f, ConvertHexSrgbToLinear("#FFD15C", 1.0f) },
    };
    Extension.RegisterOrUpdateRamp(CustomRamp);
    if (!VerifyCheck(Extension.QueryBakedLutByIdentifier("frontier_cobalt_gold") != nullptr, "Custom ramp LUT cached")
        || !VerifyCheck(Extension.QueryCodex().FindAlias("frontier_cobalt_gold") != nullptr, "ColourExtension synced custom ramp into ColourSpace::ColourCodex")
        || !VerifyCheck(ExchangeBus.QueryLatestGeneration() == 1ull, "ColourExtension publishes update to WorkspaceExchange"))
    {
        return false;
    }

    Extension.Suspend();
    Extension.Resume();
    Extension.Finalize();
    return VerifyCheck(!Extension.InspectMetrics().Initialized, "ColourExtension::Finalize");
}

bool ValidateColourCodexMultiFormat()
{
    ColourCodex Codex {};
    if (!VerifyCheck(Codex.QueryAliases().size() >= 60u, "ColourSpace::ColourCodex ships >= 60 unified aliases (solids + ramps + illuminants)"))
    {
        return false;
    }

    // 1. Short colour alias ("Blue") resolves to Royal Blue (#2454FF) in RGB, Linear, Oklab, HSL, CMYK & ACEScg
    SrgbRgba8 BlueRgb {};
    LinearRgba BlueLin {};
    LinearRgba BlueAces {};
    OklabTriplet BlueLab {};
    if (!VerifyCheck(Codex.ResolveAliasToSrgb8("Blue", BlueRgb)
                  && BlueRgb.Red == 0x24u && BlueRgb.Green == 0x54u && BlueRgb.Blue == 0xFFu,
                     "ColourCodex resolves 'Blue' -> #2454FF (Royal Blue)"))
    {
        return false;
    }
    if (!VerifyCheck(Codex.ResolveAliasToLinear("royal_blue", BlueLin)
                  && Codex.ResolveAliasInPrimaries("BLUE", ChromaticityPrimaries::AcesCg, BlueAces)
                  && Codex.ResolveAliasToOklab("Blue", BlueLab)
                  && BlueLin.Blue > 0.9f && BlueAces.Blue > 0.5f && BlueLab.AxisB < -0.1f,
                     "ColourCodex case/underscore-insensitive multi-space resolution (Linear, ACEScg, Oklab)"))
    {
        return false;
    }

    // Exact technical primary ("Pure Blue" -> #0000FF)
    SrgbRgba8 PureBlue {};
    if (!VerifyCheck(Codex.ResolveAliasToSrgb8("Pure Blue", PureBlue)
                  && PureBlue.Red == 0u && PureBlue.Green == 0u && PureBlue.Blue == 255u,
                     "ColourCodex resolves 'Pure Blue' -> #0000FF"))
    {
        return false;
    }

    // 2. Gradient / Ramp aliases ("Aurora", "Fire" -> thermal, "Spectrum" -> rainbow) resolve as MultiStopGradientSource
    ColourSourceProfile AuroraProfile {};
    ColourSourceProfile FireProfile {};
    if (!VerifyCheck(Codex.ResolveAliasToProfile("Aurora", AuroraProfile)
                  && QueryColourSourceCategory(AuroraProfile.Source) == ColourSourceCategory::MultiStopGradient
                  && Codex.ResolveAliasToProfile("Fire", FireProfile)
                  && QueryColourSourceCategory(FireProfile.Source) == ColourSourceCategory::MultiStopGradient,
                     "ColourCodex resolves gradient aliases ('Aurora', 'Fire') as MultiStopGradientSource"))
    {
        return false;
    }
    LinearRgba FireStart {}, FireEnd {};
    if (!VerifyCheck(Codex.ResolveAliasToLinear("Fire", FireStart, 0.0f)
                  && Codex.ResolveAliasToLinear("Fire", FireEnd, 1.0f)
                  && FireEnd.Blue > FireStart.Blue,
                     "ColourCodex samples gradient alias at arbitrary normalized t"))
    {
        return false;
    }

    // 3. Blackbody / Illuminant aliases ("Tungsten" 3200K, "D65" 6504K, dynamic "4500K") + physical spectrum
    ColourSourceProfile TungstenProfile {};
    SpectralCurveProfile TungstenSpectrum {};
    LinearRgba Kelvin4500 {};
    if (!VerifyCheck(Codex.ResolveAliasToProfile("Tungsten", TungstenProfile)
                  && QueryColourSourceCategory(TungstenProfile.Source) == ColourSourceCategory::BlackbodyThermal
                  && Codex.ResolveAliasToSpectrum("Tungsten", TungstenSpectrum)
                  && TungstenSpectrum.RadiantPower.size() == 81u
                  && Codex.ResolveAliasToLinear("4500K", Kelvin4500)
                  && Kelvin4500.Red > Kelvin4500.Blue,
                     "ColourCodex resolves named & dynamic Kelvin blackbody aliases + 81-bin spectrum"))
    {
        return false;
    }

    // 4. Direct hex literal & custom modular LinearFill / QuantizedRamp / DiscreteTintSet registration
    SrgbRgba8 HexLit {};
    if (!VerifyCheck(Codex.ResolveAliasToSrgb8("#FF8800", HexLit) && HexLit.Red == 255u && HexLit.Green == 136u && HexLit.Blue == 0u,
                     "ColourCodex resolves direct #RRGGBB literal"))
    {
        return false;
    }

    ColourSourceProfile CustomFill {};
    CustomFill.Source = LinearFillSource { { 1.0f, 0.2f, 0.0f, 1.0f }, { 0.1f, 0.4f, 1.0f, 1.0f }, {}, false };
    (void)Codex.RegisterSourceAlias("Frostfire Fill", CustomFill, "Project", { "Frostfire" }, false);
    ColourSourceProfile ResolvedFill {};
    if (!VerifyCheck(Codex.ResolveAliasToProfile("Frostfire", ResolvedFill)
                  && QueryColourSourceCategory(ResolvedFill.Source) == ColourSourceCategory::LinearFill,
                     "ColourCodex registers & resolves custom modular LinearFillSource alias"))
    {
        return false;
    }

    return true;
}

bool RenderVisualProofSheet()
{
    const auto& Presets = QueryBuiltInColourRampPresets();
    constexpr uint32_t CanvasW = 1120u;
    constexpr uint32_t CanvasH = 740u;
    SoftwareRasterCanvas Canvas(CanvasW, CanvasH, RgbaColor8 { 16u, 18u, 24u, 255u });

    auto FillStrip = [&](uint32_t X0, uint32_t Y0, uint32_t W, uint32_t H, const ColourSourceProfile& Profile) {
        std::vector<uint8_t> StripRgba(static_cast<std::size_t>(W) * H * 4u, 255u);
        const auto Stops = ExtractOrderedStops(Profile.Source);
        ColourRangeTransform Range = Profile.Range;
        if (std::holds_alternative<QuantizedRampSource>(Profile.Source))
        {
            Range.BlendCurve = ColourBlendCurve::Stepped;
        }
        for (uint32_t X = 0u; X < W; ++X)
        {
            const float T = static_cast<float>(X) / static_cast<float>(W - 1u);
            const LinearRgba Lin = SampleColourStopsAt(Stops, T, Range);
            const SrgbRgba8  Pix = ConvertLinearToSrgb8(Lin, TransferCurveCategory::PureGamma22);
            for (uint32_t Y = 0u; Y < H; ++Y)
            {
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                StripRgba[Off + 0u] = Pix.Red;
                StripRgba[Off + 1u] = Pix.Green;
                StripRgba[Off + 2u] = Pix.Blue;
                StripRgba[Off + 3u] = 255u;
            }
        }
        Canvas.BlitRgbaRegion(X0, Y0, W, H, StripRgba.data());
    };

    // Left column: All 9 built-in ramps in LinearSrgb (top strip) & Oklab perceptual (bottom strip)
    constexpr uint32_t ColW = 500u;
    for (std::size_t I = 0u; I < Presets.size() && I < 9u; ++I)
    {
        const uint32_t RowY = 28u + static_cast<uint32_t>(I) * 76u;
        ColourSourceProfile LinProf   = ConstructProfileFromRampPreset(Presets[I]);
        ColourSourceProfile OklabProf = LinProf;
        OklabProf.Range.InterpolationSpace = ColourInterpolationSpace::Oklab;
        FillStrip(28u,  RowY,        ColW, 28u, LinProf);
        FillStrip(28u,  RowY + 32u,  ColW, 28u, OklabProf);
    }

    // Right column: Special modular sources (Blackbody Thermal, Quantized Rainbow, Mirror-Repeated Plasma, 3D Lattice Noise Slice)
    ColourSourceProfile ThermalProf {};
    ThermalProf.Source = BlackbodyThermalSource { 1100.0f, 9500.0f, 1.0f, 1.0f };
    FillStrip(568u, 28u, 520u, 58u, ThermalProf);

    ColourSourceProfile QuantProf {};
    QuantProf.Source = QuantizedRampSource { Presets[0].Stops };
    FillStrip(568u, 108u, 520u, 58u, QuantProf);

    ColourSourceProfile MirrorProf = ConstructProfileFromRampPreset(Presets[3]);
    MirrorProf.Range.RepeatCount   = 3.0f;
    MirrorProf.Range.WrapBehaviour = ColourWrapBehaviour::Mirror;
    FillStrip(568u, 188u, 520u, 58u, MirrorProf);

    ColourSourceProfile SmoothAurora = ConstructProfileFromRampPreset(Presets[2]);
    SmoothAurora.Range.BlendCurve    = ColourBlendCurve::Smooth;
    FillStrip(568u, 268u, 520u, 58u, SmoothAurora);

    // 2D Cross-Section of 3D SpatialNoiseFill driven by Aurora stops
    constexpr uint32_t NoiseW = 520u;
    constexpr uint32_t NoiseH = 350u;
    ColourSourceProfile NoiseProfile {};
    NoiseProfile.Source = SpatialNoiseFillSource { 0.025f, 0.0f, Presets[2].Stops };
    std::vector<uint8_t> NoiseImg(static_cast<std::size_t>(NoiseW) * NoiseH * 4u, 255u);
    for (uint32_t Y = 0u; Y < NoiseH; ++Y)
    {
        for (uint32_t X = 0u; X < NoiseW; ++X)
        {
            const LinearRgba Lin = SampleColourSource(NoiseProfile, { 0.0f, static_cast<float>(X), static_cast<float>(Y), 12.0f, 0.0f, 0u });
            const SrgbRgba8  Pix = ConvertLinearToSrgb8(Lin, TransferCurveCategory::PureGamma22);
            const std::size_t Off = (static_cast<std::size_t>(Y) * NoiseW + X) * 4u;
            NoiseImg[Off + 0u] = Pix.Red;
            NoiseImg[Off + 1u] = Pix.Green;
            NoiseImg[Off + 2u] = Pix.Blue;
            NoiseImg[Off + 3u] = 255u;
        }
    }
    Canvas.BlitRgbaRegion(568u, 352u, NoiseW, NoiseH, NoiseImg.data());

    const std::filesystem::path OutDir = ResolveRepositoryRootPath() / "Exhibits/Images/ColourSpace";
    std::filesystem::create_directories(OutDir);
    return VerifyCheck(Canvas.WritePngFile(OutDir / "ColourRampPresetsSheet.png"), "Write ColourRampPresetsSheet.png");
}

} // namespace

int main()
{
    if (!ValidateColorimetryRoundTrips())
    {
        return 1;
    }
    if (!ValidateModularSources())
    {
        return 1;
    }
    if (!ValidateColourCodexMultiFormat())
    {
        return 1;
    }
    if (!RenderVisualProofSheet())
    {
        return 1;
    }
    std::printf("[ColourSpaceValidation] ALL CHECKS PASSED (colorimetry, 7 modular sources, HTML parity, LUT64/256, ColourExtension, PNG sheet).\n");
    return 0;
}
