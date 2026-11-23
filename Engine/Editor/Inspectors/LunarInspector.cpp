/*============================================================================================================================================
                                                            LUNARINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance LunarInspector (real ray-cast LunarSurfaceAtlas globe, 6 presets, night-sky halo & rotation)

#include "Engine/Editor/Inspectors/LunarInspector.h"

#include "Engine/Components/Controls/PolarDirectionDial.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/GeometricRaster/Atmosphere/LunarStarfieldSequence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
constexpr float PiF = 3.14159265359f;

inline void PutPixel(std::vector<uint8_t>& Buf, uint32_t W, uint32_t X, uint32_t Y, uint8_t R, uint8_t G, uint8_t B, uint8_t A) noexcept
{
    const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
    Buf[Off + 0u] = R; Buf[Off + 1u] = G; Buf[Off + 2u] = B; Buf[Off + 3u] = A;
}
} // namespace

void LunarInspector::RasterizeLunarGlobe(
    InspectorPreviewSurface&                       Target,
    const GeometricRaster::LunarBodyConfiguration& Moon,
    uint32_t                                       WidthPx,
    uint32_t                                       HeightPx,
    float                                          DiscRadiusPx,
    bool                                           ApplyPhaseShadow,
    bool                                           IncludeNightBackdrop,
    float                                          NaturalRingRadiusPx)
{
    uint64_t Sig = 0x4D005005ull;
    Sig = InspectorPrimitives::MixRevisionU64(Sig, static_cast<uint64_t>(Moon.SurfacePreset));
    Sig = InspectorPrimitives::MixRevisionU64(Sig, (ApplyPhaseShadow ? 1ull : 0ull) | (IncludeNightBackdrop ? 2ull : 0ull) | (Moon.HasAtmosphere ? 4ull : 0ull));
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Moon.SynodicPhaseFraction);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Moon.BrightnessMultiplier);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Moon.HaloGlowMultiplier);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Moon.SurfaceRollDegrees);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Moon.SurfacePitchDegrees);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Moon.AtmosphereIntensity);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Moon.AtmosphereThicknessScale);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Moon.AtmosphereTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Moon.AtmosphereTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Moon.AtmosphereTint.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, DiscRadiusPx);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, NaturalRingRadiusPx);

    Target.EnsureUpdated(Sig, WidthPx, HeightPx, [&](std::vector<uint8_t>& Pixels)
    {
        const auto& SurfaceSheet = GeometricRaster::LunarStarfieldSequence::QueryLunarSurfaceAtlasRgba8();
        const uint32_t TileW     = GeometricRaster::LunarStarfieldSequence::AtlasTileWidthPx;
        const uint32_t TileH     = GeometricRaster::LunarStarfieldSequence::AtlasTileHeightPx;
        const uint32_t PresetIdx = std::min<uint32_t>(static_cast<uint32_t>(Moon.SurfacePreset), GeometricRaster::LunarStarfieldSequence::AtlasPresetCount - 1u);

        const float CX = static_cast<float>(WidthPx) * 0.5f;
        const float CY = static_cast<float>(HeightPx) * 0.5f;
        const float RollRad  = Moon.SurfaceRollDegrees  * (PiF / 180.0f);
        const float PitchRad = Moon.SurfacePitchDegrees * (PiF / 180.0f);
        const float CR = std::cos(RollRad),  SR = std::sin(RollRad);
        const float CP = std::cos(PitchRad), SP = std::sin(PitchRad);

        const float PhaseRad  = (ApplyPhaseShadow ? Moon.SynodicPhaseFraction : 0.5f) * 2.0f * PiF;
        const float LX        = std::sin(PhaseRad), LZ = -std::cos(PhaseRad);
        const float Gain      = std::min(1.55f, 0.55f + 0.34f * Moon.BrightnessMultiplier);
        const float RimSpan   = Moon.HasAtmosphere ? (0.06f + 0.08f * Moon.AtmosphereThicknessScale) : 0.0f;
        const float LitFactor = ApplyPhaseShadow ? (0.25f + 0.75f * (1.0f - std::cos(PhaseRad)) * 0.5f) : 1.0f;

        for (uint32_t Y = 0u; Y < HeightPx; ++Y)
        {
            const float FY = static_cast<float>(Y) + 0.5f;
            const float T  = FY / static_cast<float>(HeightPx);
            for (uint32_t X = 0u; X < WidthPx; ++X)
            {
                const float FX   = static_cast<float>(X) + 0.5f;
                const float DX   = FX - CX, DY = FY - CY;
                const float Dist = std::sqrt(DX * DX + DY * DY);

                float R = IncludeNightBackdrop ? (0.015f + 0.06f * T * T) : (32.0f / 255.0f);
                float G = IncludeNightBackdrop ? (0.025f + 0.09f * T * T) : (32.0f / 255.0f);
                float B = IncludeNightBackdrop ? (0.055f + 0.16f * T * T) : (32.0f / 255.0f);
                uint8_t OutA = IncludeNightBackdrop ? 255u : 0u;

                if (Moon.HaloGlowMultiplier > 0.01f && IncludeNightBackdrop)
                {
                    const float Reach = DiscRadiusPx * (1.25f + Moon.HaloGlowMultiplier * 0.85f);
                    if (Dist < Reach)
                    {
                        const float H01   = 1.0f - std::clamp((Dist - DiscRadiusPx * 0.88f) / std::max(1.0f, Reach - DiscRadiusPx * 0.88f), 0.0f, 1.0f);
                        const float HaloA = H01 * H01 * std::min(0.70f, 0.14f * Moon.BrightnessMultiplier * std::min(1.5f, Moon.HaloGlowMultiplier)) * LitFactor;
                        R += 0.84f * HaloA; G += 0.89f * HaloA; B += 1.00f * HaloA;
                    }
                }
                if (Moon.HasAtmosphere && Dist >= DiscRadiusPx * 0.96f && Dist < DiscRadiusPx * (1.0f + RimSpan))
                {
                    const float RimT = 1.0f - (Dist - DiscRadiusPx * 0.96f) / std::max(1.0f, DiscRadiusPx * (RimSpan + 0.04f));
                    const float RimA = RimT * RimT * std::min(0.85f, 0.55f * Moon.AtmosphereIntensity);
                    R += Moon.AtmosphereTint.R * RimA; G += Moon.AtmosphereTint.G * RimA; B += Moon.AtmosphereTint.B * RimA;
                    OutA = std::max<uint8_t>(OutA, static_cast<uint8_t>(std::clamp(RimA * 255.0f, 0.0f, 255.0f)));
                }
                if (NaturalRingRadiusPx > 2.0f && std::fabs(Dist - NaturalRingRadiusPx) < 0.95f)
                {
                    if (std::fmod(std::atan2(DY, DX) + PiF, 0.24f) < 0.12f) { R = 0.72f; G = 0.76f; B = 0.84f; OutA = 255u; }
                }

                const float NX = DX / std::max(1.0f, DiscRadiusPx), NY = -DY / std::max(1.0f, DiscRadiusPx);
                const float D2 = NX * NX + NY * NY;
                if (D2 <= 1.0f)
                {
                    const float NZ = std::sqrt(std::max(0.0f, 1.0f - D2));
                    const float X1 = NX * CR + NY * SR, Y1 = -NX * SR + NY * CR;
                    const float Y2 = Y1 * CP - NZ * SP, Z2 = Y1 * SP + NZ * CP;
                    const float U = std::clamp(0.5f + std::atan2(X1, Z2) / (2.0f * PiF), 0.0f, 0.9999f);
                    const float V = std::clamp(0.5f - std::asin(std::clamp(Y2, -1.0f, 1.0f)) / PiF, 0.0f, 0.9999f);

                    float TexR = 0.65f, TexG = 0.65f, TexB = 0.65f;
                    if (SurfaceSheet.size() >= static_cast<std::size_t>(TileW) * TileH * (PresetIdx + 1u) * 4u)
                    {
                        const uint32_t TX = std::min(TileW - 1u, static_cast<uint32_t>(U * static_cast<float>(TileW)));
                        const uint32_t TY = PresetIdx * TileH + std::min(TileH - 1u, static_cast<uint32_t>(V * static_cast<float>(TileH)));
                        const std::size_t Off = (static_cast<std::size_t>(TY) * TileW + TX) * 4u;
                        TexR = static_cast<float>(SurfaceSheet[Off + 0u]) / 255.0f;
                        TexG = static_cast<float>(SurfaceSheet[Off + 1u]) / 255.0f;
                        TexB = static_cast<float>(SurfaceSheet[Off + 2u]) / 255.0f;
                    }

                    const float NDotL    = NX * LX + NZ * LZ;
                    const float Shade    = ApplyPhaseShadow
                                               ? (0.045f + 0.955f * std::clamp((NDotL + 0.08f) / 0.24f, 0.0f, 1.0f) * std::pow(std::max(0.0f, NDotL), 0.48f))
                                               : (0.28f + 0.72f * std::pow(NZ, 0.45f));
                    const float LimbDark = 0.78f + 0.22f * std::pow(NZ, 0.35f);
                    R = TexR * Shade * LimbDark * Gain;
                    G = TexG * Shade * LimbDark * Gain;
                    B = TexB * Shade * LimbDark * Gain;

                    if (Moon.HasAtmosphere)
                    {
                        const float Fresnel = std::pow(1.0f - NZ, 3.0f) * std::min(0.85f, 0.55f * Moon.AtmosphereIntensity);
                        R += Moon.AtmosphereTint.R * Fresnel; G += Moon.AtmosphereTint.G * Fresnel; B += Moon.AtmosphereTint.B * Fresnel;
                    }
                    const float EdgeAA = std::clamp((DiscRadiusPx - Dist) * 1.4f, 0.0f, 1.0f);
                    OutA = IncludeNightBackdrop ? 255u : static_cast<uint8_t>(std::clamp(EdgeAA * 255.0f, 0.0f, 255.0f));
                }

                PutPixel(Pixels, WidthPx, X, Y,
                    static_cast<uint8_t>(std::clamp(std::lround(R * 255.0f), 0l, 255l)),
                    static_cast<uint8_t>(std::clamp(std::lround(G * 255.0f), 0l, 255l)),
                    static_cast<uint8_t>(std::clamp(std::lround(B * 255.0f), 0l, 255l)), OutA);
            }
        }
        if (IncludeNightBackdrop)
        {
            uint32_t Seed = 7u;
            auto Next01 = [&]() noexcept -> float { Seed = (Seed * 16807u) % 2147483647u; return static_cast<float>(Seed) / 2147483647.0f; };
            for (int32_t S = 0; S < 70; ++S)
            {
                const uint32_t SX = std::min(WidthPx - 1u, static_cast<uint32_t>(Next01() * static_cast<float>(WidthPx)));
                const uint32_t SY = std::min(HeightPx - 1u, static_cast<uint32_t>(Next01() * static_cast<float>(HeightPx) * 0.88f));
                const float DX = static_cast<float>(SX) - CX, DY = static_cast<float>(SY) - CY;
                if (DX * DX + DY * DY > DiscRadiusPx * DiscRadiusPx * 1.08f)
                {
                    const float Br = 0.35f + Next01() * 0.65f;
                    PutPixel(Pixels, WidthPx, SX, SY, static_cast<uint8_t>(215.0f * Br), static_cast<uint8_t>(228.0f * Br), static_cast<uint8_t>(255.0f * Br), 255u);
                }
            }
        }
    });
}

float LunarInspector::Construct(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    OutlinerRecordEntry&    Entry,
    LunarInstanceData&      Instance,
    uint32_t                DisplayOrdinal,
    const PaneInputReading& Input,
    bool&                   OutMutated,
    bool&                   OutPageSwitched)
{
    static constexpr std::array<const char*, 3u> Pages = {{ "Overview", "Appearance", "Position" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    auto& Moon = Instance.Moon;
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    static constexpr std::array<const char*, 6u> PresetNames = {{ "Luna", "Ember", "Glacier", "Sulfur", "Shroud", "Shard" }};

    if (ActivePage == 0u)
    {
        const Rectangle2D SlotSec { LeftX, CursorY, InnerWidth, 106.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, SlotSec, "Moon slot");
        char SlotLabel[32]{}; std::snprintf(SlotLabel, sizeof(SlotLabel), "Moon %u", DisplayOrdinal + 1u);
        const Rectangle2D SlotBtn { SlotSec.Right() - PadX - 96.0f, CursorY + InspectorMetrics::SectionPadTop - 5.0f, 96.0f, 26.0f };
        Surface.FillRectangle(SlotBtn, ColorRgba{ 16u, 16u, 18u, 255u }, 13.0f);
        Surface.StrokeRectangle(SlotBtn, ColorRgba{ 255u, 255u, 255u, 24u }, 13.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { SlotBtn.X + 14.0f, SlotBtn.Y + 6.5f }, 11.0f, ColorRgba{ 238u, 238u, 238u, 255u }, SlotLabel);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { LeftX + PadX, CursorY + 58.0f }, BodyW, 10.5f, InspectorMetrics::MutedInk, "Multiple moons can coexist across any folder.");
        CursorY = SlotSec.Bottom() + InspectorMetrics::SectionGap;

        RasterizeLunarGlobe(PhaseGlobe, Moon, 164u, 164u, 76.0f, true, false, 0.0f);
        const Rectangle2D PhaseSec { LeftX, CursorY, InnerWidth, 386.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PhaseSec, "Lunar phase");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        const float Days  = Moon.SynodicPhaseFraction * 29.53f;
        const float Illum = (1.0f - std::cos(Moon.SynodicPhaseFraction * 2.0f * PiF)) * 50.0f;
        char DaysBuf[16]{}, IllumBuf[48]{};
        std::snprintf(DaysBuf,  sizeof(DaysBuf),  "%.1f", Days);
        std::snprintf(IllumBuf, sizeof(IllumBuf), "%.0f%% illuminated", Illum);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, DaysBuf, "days", IllumBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + PadX, Y + 2.0f }, 10.5f, InspectorMetrics::MutedInk, "Position in the 29.53-day synodic cycle");
        Y += 20.0f;
        const Rectangle2D MoonBox { PhaseSec.Center().X - 78.0f, Y + 4.0f, 156.0f, 156.0f };
        PhaseGlobe.Present(Surface, MoonBox, 78.0f);
        Y = MoonBox.Bottom() + 14.0f;
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 4001u, "Phase", "", "%.2f", 0.0f, 1.0f, Moon.SynodicPhaseFraction, ActiveDragId, Input, OutMutated, Moon.FollowSkyOppositeSun);
        CursorY = PhaseSec.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 3u> Quick = {{
            { "Visible",        InspectorSwitchSymbol::Eye,   Entry.VisibilityEnabled,   false },
            { "Follow Sky",     InspectorSwitchSymbol::Orbit, Moon.FollowSkyOppositeSun, false },
            { "Has atmosphere", InspectorSwitchSymbol::Cloud, Moon.HasAtmosphere,        false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Moon.FollowSkyOppositeSun = !Moon.FollowSkyOppositeSun; OutMutated = true; }
        else if (Clicked == 2) { Moon.HasAtmosphere = !Moon.HasAtmosphere; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "lunar", "celestial" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        {
            const Rectangle2D GalSec { LeftX, CursorY, InnerWidth, 232.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, GalSec, "Surface presets");
            const float Gap = 8.0f, TileW = std::floor((BodyW - Gap * 2.0f) / 3.0f), TileH = 74.0f;
            const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
            for (uint32_t P = 0u; P < 6u; ++P)
            {
                auto SampleMoon = Moon;
                SampleMoon.SurfacePreset = static_cast<GeometricRaster::LunarSurfacePreset>(P);
                SampleMoon.HasAtmosphere = false;
                RasterizeLunarGlobe(PresetGlobes[P], SampleMoon, 64u, 64u, 28.0f, false, false, 0.0f);
                const Rectangle2D Tile {
                    LeftX + PadX + static_cast<float>(P % 3u) * (TileW + Gap),
                    CursorY + InspectorMetrics::SectionPadTop + 28.0f + static_cast<float>(P / 3u) * (TileH + Gap),
                    TileW, TileH,
                };
                const bool Sel = (static_cast<uint32_t>(Moon.SurfacePreset) == P);
                Surface.FillRectangle(Tile, Sel ? ColorRgba{ 42u, 44u, 48u, 255u } : ColorRgba{ 26u, 26u, 28u, 255u }, 12.0f);
                Surface.StrokeRectangle(Tile, Sel ? ColorRgba{ 158u, 184u, 216u, 255u } : ColorRgba{ 255u, 255u, 255u, 12u }, 12.0f, 1.0f);
                PresetGlobes[P].Present(Surface, { Tile.Center().X - 22.0f, Tile.Y + 6.0f, 44.0f, 44.0f }, 22.0f);
                const float LW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, PresetNames[P]);
                WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Tile.Center().X - LW * 0.5f, Tile.Bottom() - 18.0f }, 10.0f, ColorRgba{ 222u, 222u, 226u, 255u }, PresetNames[P]);
                if (Tile.Contains(Cursor) && Input.PrimaryPointer.Pressed) { Moon.SurfacePreset = static_cast<GeometricRaster::LunarSurfacePreset>(P); OutMutated = true; }
            }
            CursorY = GalSec.Bottom() + InspectorMetrics::SectionGap;
        }
        {
            const float MoonR = std::min(62.0f, 12.0f + 22.0f * std::sqrt(std::max(0.05f, Moon.AngularDiameterDegrees)));
            const float NatR  = 12.0f + 22.0f * std::sqrt(0.52f);
            RasterizeLunarGlobe(LookGlobe, Moon, 224u, 160u, MoonR, true, true, NatR);
            const Rectangle2D LookSec { LeftX, CursorY, InnerWidth, 580.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, LookSec, "Moonlight, size & atmosphere");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            LookGlobe.Present(Surface, { LeftX + PadX, Y, BodyW, 152.0f }, 12.0f);
            Y += 166.0f;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 4101u, "Bright", "x", "%.2f", 0.0f, 6.0f, Moon.BrightnessMultiplier, ActiveDragId, Input, OutMutated);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 4102u, "Glow", "x", "%.2f", 0.0f, 3.0f, Moon.HaloGlowMultiplier, ActiveDragId, Input, OutMutated);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 4103u, "Size", "deg", "%.2f", 0.1f, 15.0f, Moon.AngularDiameterDegrees, ActiveDragId, Input, OutMutated);
            Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Has atmosphere", Moon.HasAtmosphere, Input, OutMutated);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 4104u, "Atmosphere intensity", "x", "%.2f", 0.0f, 3.0f, Moon.AtmosphereIntensity, ActiveDragId, Input, OutMutated, !Moon.HasAtmosphere);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 4105u, "Thickness scale", "x", "%.2f", 0.05f, 1.0f, Moon.AtmosphereThicknessScale, ActiveDragId, Input, OutMutated, !Moon.HasAtmosphere);
            (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 4110u, "Atmosphere tint", Moon.AtmosphereTint, ActiveDragId, Input, OutMutated, !Moon.HasAtmosphere);
            CursorY = LookSec.Bottom() + InspectorMetrics::SectionGap;
        }
    }
    else
    {
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 344.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Lunar position");
            bool DialChanged = false;
            PolarDirectionDial::Construct(Surface, { S.Center().X, CursorY + 116.0f }, 62.0f, Moon.AzimuthDegrees, Moon.ElevationDegrees, true, Input, Moon.AzimuthDegrees, Moon.ElevationDegrees, DialChanged, !Moon.FollowSkyOppositeSun);
            if (DialChanged) OutMutated = true;
            float Y = CursorY + 194.0f;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 4201u, "Azimuth", "deg", "%.1f", 0.0f, 360.0f, Moon.AzimuthDegrees, ActiveDragId, Input, OutMutated, Moon.FollowSkyOppositeSun);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 4202u, "Elevation", "deg", "%.1f", -90.0f, 90.0f, Moon.ElevationDegrees, ActiveDragId, Input, OutMutated, Moon.FollowSkyOppositeSun);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        {
            RasterizeLunarGlobe(TurnGlobe, Moon, 140u, 140u, 64.0f, false, false, 0.0f);
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 340.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Moon rotation");
            TurnGlobe.Present(Surface, { S.Center().X - 64.0f, CursorY + InspectorMetrics::SectionPadTop + 26.0f, 128.0f, 128.0f }, 64.0f);
            float Y = CursorY + 186.0f;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 4203u, "Roll", "deg", "%.1f", 0.0f, 360.0f, Moon.SurfaceRollDegrees, ActiveDragId, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 4204u, "Pitch", "deg", "%.1f", -180.0f, 180.0f, Moon.SurfacePitchDegrees, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }

    return CursorY;
}

} // namespace Frontier
