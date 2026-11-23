/*============================================================================================================================================
                                                       CAMERASETTINGSINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance CameraSettingsInspector (F77/F78: Render type hero, per-renderer carousel, Quality budget, Output)

#include "Engine/Editor/Inspectors/CameraSettingsInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace Frontier
{

namespace
{
inline uint8_t ToneSrgb8(float Hdr) noexcept
{
    const float M = std::clamp(Hdr / (1.0f + std::max(0.0f, Hdr)), 0.0f, 1.0f);
    const float S = (M <= 0.0031308f) ? (M * 12.92f) : (1.055f * std::pow(M, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
}

[[nodiscard]] float ComputeSampleBudget(const CameraSettingsData& S) noexcept
{
    const float ScaleMul = (S.RenderScalePercent / 100.0f) * (S.RenderScalePercent / 100.0f);
    float CoreSamples = 1.0f;
    switch (S.RendererIndex)
    {
        case 0u: CoreSamples = (S.PbrShadowIndex == 2u ? 6.0f : (S.PbrShadowIndex == 1u ? 4.0f : 2.0f)) + (S.PbrAoEnabled ? 2.0f : 0.0f); break;
        case 1u: CoreSamples = (S.SdfConeGiEnabled ? S.SdfConeCount : 1.0f) + (S.SdfAoEnabled ? 2.0f : 0.0f) + 2.0f; break;
        case 2u: CoreSamples = (S.RayGiEnabled ? S.RayGiSamples : 0.0f) + S.RayShadowRays + (S.RayReflectionsEnabled ? S.RayReflectionBounces * 2.0f : 0.0f) + 1.0f; break;
        default: CoreSamples = 1.0f + (S.MatcapShadowIndex > 0u ? 1.0f : 0.0f); break;
    }
    return std::max(1.0f, CoreSamples * ScaleMul);
}
} // namespace

void CameraSettingsInspector::UpdateRendererHeroSurface(const CameraSettingsData& Instance)
{
    uint64_t Sig = 0xC5010001ull;
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.RendererIndex);
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.MatcapChannelIndex);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.MatcapLightAngleDeg);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.ExposureGain);

    const uint32_t W = 240u, H = 148u;
    RendererHeroPreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float CX = static_cast<float>(W) * 0.5f, CY = 68.0f, SphereR = 40.0f, FloorY = 120.0f;
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y) + 0.5f;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float FX = static_cast<float>(X) + 0.5f;
                float R = 0.028f, G = 0.030f, B = 0.035f;

                // Subtle horizon baseline
                if (std::fabs(FY - FloorY) < 0.7f && FX > 16.0f && FX < 224.0f)
                {
                    R += 0.09f; G += 0.10f; B += 0.11f;
                }

                // Renderer-specific ground signature (PBR shadow oval / SDF contour rings / Ray bounce rays / MatCap contact)
                if (Instance.RendererIndex == 0u)
                {
                    const float EX = (FX - (CX + 18.0f)) / 42.0f, EY = (FY - FloorY) / 8.0f;
                    const float E2 = EX * EX + EY * EY;
                    if (E2 < 1.0f) { const float A = (1.0f - E2) * 0.65f; R *= (1.0f - A); G *= (1.0f - A); B *= (1.0f - A); }
                }
                else if (Instance.RendererIndex == 1u)
                {
                    const float EX = (FX - (CX + 12.0f)) / 48.0f, EY = (FY - FloorY) / 9.5f;
                    const float E  = std::sqrt(EX * EX + EY * EY);
                    if (std::fabs(E - 1.0f) < 0.08f || std::fabs(E - 0.66f) < 0.08f)
                    {
                        R += 0.14f; G += 0.42f; B += 0.28f;
                    }
                }
                else if (Instance.RendererIndex == 2u)
                {
                    // Dashed traced bounce rays across upper studio
                    const float D1 = std::fabs((FY - 22.0f) - 0.55f * (FX - 36.0f));
                    const float D2 = std::fabs((FY - 58.0f) + 0.62f * (FX - 102.0f));
                    if ((D1 < 1.0f && FX > 36.0f && FX < 104.0f && (static_cast<int>(X) % 7) < 4) ||
                        (D2 < 1.0f && FX > 100.0f && FX < 180.0f && (static_cast<int>(X) % 7) < 4))
                    {
                        R += 0.48f; G += 0.36f; B += 0.72f;
                    }
                }
                else
                {
                    const float EX = (FX - CX) / 36.0f, EY = (FY - FloorY) / 6.5f;
                    const float E2 = EX * EX + EY * EY;
                    if (E2 < 1.0f) { const float A = (1.0f - E2) * 0.45f; R *= (1.0f - A); G *= (1.0f - A); B *= (1.0f - A); }
                }

                // Shaded central sphere matching F78 TypeThumb palette (Blue PBR / Mint SDF / Violet Ray / Clay MatCap)
                const float NX = (FX - CX) / SphereR, NY = (CY - FY) / SphereR;
                const float S2 = NX * NX + NY * NY;
                if (S2 <= 1.0f)
                {
                    const float NZ = std::sqrt(std::max(0.0f, 1.0f - S2));
                    const float Key = std::max(0.0f, -0.45f * NX + 0.58f * NY + 0.68f * NZ);
                    const float Spec = std::pow(std::max(0.0f, -0.30f * NX + 0.46f * NY + 0.83f * NZ), 26.0f);
                    const float Rim  = std::pow(1.0f - NZ, 3.0f);
                    if (Instance.RendererIndex == 0u) // PBR cool blue-white
                    {
                        R = 0.08f + Key * 0.56f + Spec * 0.95f;
                        G = 0.14f + Key * 0.72f + Spec * 0.98f;
                        B = 0.24f + Key * 0.96f + Spec * 1.00f;
                    }
                    else if (Instance.RendererIndex == 1u) // SDF emerald mint
                    {
                        R = 0.08f + Key * 0.46f + Spec * 0.85f + Rim * 0.12f;
                        G = 0.22f + Key * 0.86f + Spec * 0.95f + Rim * 0.34f;
                        B = 0.16f + Key * 0.64f + Spec * 0.90f + Rim * 0.22f;
                    }
                    else if (Instance.RendererIndex == 2u) // Ray traced amethyst
                    {
                        R = 0.16f + Key * 0.66f + Spec * 1.05f + Rim * 0.30f;
                        G = 0.12f + Key * 0.54f + Spec * 0.95f + Rim * 0.20f;
                        B = 0.26f + Key * 0.94f + Spec * 1.15f + Rim * 0.48f;
                    }
                    else // MatCap warm clay
                    {
                        R = 0.22f + Key * 0.64f + Rim * 0.28f;
                        G = 0.20f + Key * 0.59f + Rim * 0.26f;
                        B = 0.17f + Key * 0.52f + Rim * 0.23f;
                    }
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float CameraSettingsInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, CameraSettingsData& Instance,
    ViewportDisplaySettings* DisplaySettings,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 4u> RendererLabels = {{ "A · PBR", "B · SDF", "C · Ray", "D · MatCap" }};
    static constexpr std::array<const char*, 4u> RendererNames  = {{ "PBR Raster", "SDF Tracing", "Ray Traced", "MatCap Studio" }};
    static constexpr std::array<const char*, 4u> RendererLines  = {{
        "Raster with shadow maps · sky fill",
        "Distance-field shadows · real-time cone GI",
        "BVH / path-traced light, shadows & bounce",
        "Clay studio shading to read shape",
    }};
    static constexpr std::array<const char*, 4u> QualityLabels = {{ "Low", "Medium", "High", "Ultra" }};

    // Per-renderer dynamic page carousel (F77/F78)
    std::vector<const char*> DynamicPages {};
    switch (Instance.RendererIndex)
    {
        case 0u: DynamicPages = { "Overview", "Shadows", "Lighting", "AO", "Output" }; break;
        case 1u: DynamicPages = { "Overview", "Shadows", "GI", "AO", "Output" }; break;
        case 2u: DynamicPages = { "Overview", "Tracer", "GI", "Shadows", "AO", "Reflections", "Output" }; break;
        default: DynamicPages = { "Overview", "Shadows", "Lighting", "MatCap", "Output" }; break;
    }
    if (ActivePage >= DynamicPages.size()) ActivePage = 0u;

    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, DynamicPages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const uint32_t RIdx = std::min<uint32_t>(Instance.RendererIndex, 3u);
    const float    BudgetSpp = ComputeSampleBudget(Instance);
    const uint32_t OutputPageIdx = static_cast<uint32_t>(DynamicPages.size() - 1u);

    if (ActivePage == 0u)
    {
        // Render type Hero Card (F78: large preview sphere + A/B/C/D switcher + one-line description)
        UpdateRendererHeroSurface(Instance);
        const Rectangle2D HeroSec { LeftX, CursorY, InnerWidth, 326.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, HeroSec, "Render type", RendererNames[RIdx]);
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        RendererHeroPreview.Present(Surface, { LeftX + PadX, Y, BodyW, 148.0f }, 12.0f);
        Y += 156.0f;
        int32_t PickRen = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Active render pipeline", RendererLabels, Instance.RendererIndex, 2u, Input, PickRen);
        if (PickRen >= 0 && PickRen < 4)
        {
            Instance.RendererIndex = static_cast<uint32_t>(PickRen);
            OutMutated = true;
        }
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { LeftX + PadX, HeroSec.Bottom() - 24.0f }, BodyW, 10.0f, InspectorMetrics::MutedInk, RendererLines[RIdx]);
        CursorY = HeroSec.Bottom() + InspectorMetrics::SectionGap;

        // Quality & sample budget summary card
        const Rectangle2D QualSec { LeftX, CursorY, InnerWidth, 198.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, QualSec, "Quality budget", QualityLabels[std::min<uint32_t>(Instance.QualityTierIndex, 3u)]);
        float QY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char SppBuf[20]{};
        std::snprintf(SppBuf, sizeof(SppBuf), "%.1f", BudgetSpp);
        QY = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, QY, SppBuf, "spp", "Estimated samples per pixel");
        int32_t PickTier = -1;
        (void)InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, QY, BodyW, "Master quality tier", QualityLabels, Instance.QualityTierIndex, 4u, Input, PickTier);
        if (PickTier >= 0 && PickTier < 4) { Instance.QualityTierIndex = static_cast<uint32_t>(PickTier); OutMutated = true; }
        CursorY = QualSec.Bottom() + InspectorMetrics::SectionGap;

        // Active renderer summary readout
        const Rectangle2D SumSec { LeftX, CursorY, InnerWidth, 146.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, SumSec, RendererNames[RIdx], "Pages");
        float SY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char ScaleLine[48]{};
        std::snprintf(ScaleLine, sizeof(ScaleLine), "%.0f %% · exp %.2fx", Instance.RenderScalePercent, Instance.ExposureGain);
        SY = InspectorPrimitives::ConstructMetadataRow(Surface, LeftX + PadX, SY, BodyW, "Pipeline", RendererNames[RIdx]);
        SY = InspectorPrimitives::ConstructMetadataRow(Surface, LeftX + PadX, SY, BodyW, "Output",   ScaleLine);
        (void)InspectorPrimitives::ConstructMetadataRow(Surface, LeftX + PadX, SY, BodyW, "Temporal AA", Instance.TemporalAntiAliasing ? "Enabled" : "Off");
        CursorY = SumSec.Bottom() + InspectorMetrics::SectionGap;

        static constexpr std::array<const char*, 2u> Tags = {{ "camera-settings", "renderer" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == OutputPageIdx)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 260.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Camera output");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8810u, "Render scale", "%", "%.0f", 50.0f, 200.0f, Instance.RenderScalePercent, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8811u, "Exposure gain", "x", "%.2f", 0.1f, 4.0f, Instance.ExposureGain, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Temporal anti-aliasing", Instance.TemporalAntiAliasing, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (Instance.RendererIndex == 0u) // PBR pages: Shadows(1), Lighting(2), AO(3)
    {
        if (ActivePage == 1u)
        {
            static constexpr std::array<const char*, 3u> ShLabels = {{ "PCF", "PCSS", "SMRT" }};
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 232.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "PBR shadow maps");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            int32_t Pick = -1;
            Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Shadow kernel", ShLabels, Instance.PbrShadowIndex, 3u, Input, Pick);
            if (Pick >= 0) { Instance.PbrShadowIndex = static_cast<uint32_t>(Pick); OutMutated = true; }
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8820u, "Cascade resolution", "px", "%.0f", 512.0f, 4096.0f, Instance.PbrShadowCascadeRes, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 2u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 186.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "PBR direct lighting");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "LTC polygonal area lights", Instance.PbrAreaLights, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8821u, "Sky fill gain", "x", "%.2f", 0.0f, 3.0f, Instance.PbrFillGain, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 150.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Ambient occlusion");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "XeGTAO screen-space AO", Instance.PbrAoEnabled, Input, OutMutated);
            if (DisplaySettings) DisplaySettings->XeGtaoEnabled = Instance.PbrAoEnabled;
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }
    else if (Instance.RendererIndex == 1u) // SDF pages: Shadows(1), GI(2), AO(3)
    {
        if (ActivePage == 1u)
        {
            static constexpr std::array<const char*, 3u> SdfSh = {{ "Hard SDF", "Soft Cone", "Hybrid" }};
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 232.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "SDF shadows");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            int32_t Pick = -1;
            Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "SDF penumbra mode", SdfSh, Instance.SdfShadowIndex, 3u, Input, Pick);
            if (Pick >= 0) { Instance.SdfShadowIndex = static_cast<uint32_t>(Pick); OutMutated = true; }
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8830u, "Trace steps", "", "%.0f", 16.0f, 128.0f, Instance.SdfTraceSteps, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 2u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 236.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "SDF cone GI");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Real-time cone GI", Instance.SdfConeGiEnabled, Input, OutMutated);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8831u, "Diffuse cones", "", "%.0f", 2.0f, 16.0f, Instance.SdfConeCount, ActiveDragId, Input, OutMutated, !Instance.SdfConeGiEnabled);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8832u, "Ambient fill", "x", "%.2f", 0.0f, 3.0f, Instance.SdfFillGain, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 150.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Distance-field AO");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Enable DFAO", Instance.SdfAoEnabled, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }
    else if (Instance.RendererIndex == 2u) // Ray traced pages: Tracer(1), GI(2), Shadows(3), AO(4), Reflections(5)
    {
        if (ActivePage == 1u)
        {
            static constexpr std::array<const char*, 3u> Tracers = {{ "WideTree BVH", "ReSTIR GI", "Path Tracer" }};
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 186.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Ray tracer core");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            int32_t Pick = -1;
            (void)InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Tracer algorithm", Tracers, Instance.RayTracerIndex, 2u, Input, Pick);
            if (Pick >= 0) { Instance.RayTracerIndex = static_cast<uint32_t>(Pick); OutMutated = true; }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 2u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 188.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Traced GI");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Enable traced GI", Instance.RayGiEnabled, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8840u, "GI samples", "spp", "%.0f", 1.0f, 32.0f, Instance.RayGiSamples, ActiveDragId, Input, OutMutated, !Instance.RayGiEnabled);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 3u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 152.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "BVH shadows");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8841u, "Shadow rays", "spp", "%.0f", 1.0f, 16.0f, Instance.RayShadowRays, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 4u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 150.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Short-ray cavity AO");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Ray-traced AO", Instance.RayAoEnabled, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 188.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Traced reflections");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Enable reflections", Instance.RayReflectionsEnabled, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8842u, "Max bounces", "", "%.0f", 1.0f, 6.0f, Instance.RayReflectionBounces, ActiveDragId, Input, OutMutated, !Instance.RayReflectionsEnabled);
            if (DisplaySettings) DisplaySettings->ReflectionsEnabled = Instance.RayReflectionsEnabled;
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }
    else // MatCap pages: Shadows(1), Lighting(2), MatCap(3)
    {
        if (ActivePage == 1u)
        {
            static constexpr std::array<const char*, 3u> MSh = {{ "None", "Contact", "Soft" }};
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 164.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "MatCap floor shadow");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            int32_t Pick = -1;
            (void)InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Shadow mode", MSh, Instance.MatcapShadowIndex, 3u, Input, Pick);
            if (Pick >= 0) { Instance.MatcapShadowIndex = static_cast<uint32_t>(Pick); OutMutated = true; }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 2u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 192.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Clay key & rim");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8850u, "Key angle", "deg", "%.0f", 0.0f, 360.0f, Instance.MatcapLightAngleDeg, ActiveDragId, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8851u, "Rim highlight", "", "%.2f", 0.0f, 1.0f, Instance.MatcapRimGain, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else
        {
            static constexpr std::array<const char*, 4u> Channels = {{ "Clay", "Ceramic", "Metallic", "Normal" }};
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 186.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "MatCap channel");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            int32_t Pick = -1;
            (void)InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Shading sphere", Channels, Instance.MatcapChannelIndex, 2u, Input, Pick);
            if (Pick >= 0) { Instance.MatcapChannelIndex = static_cast<uint32_t>(Pick); OutMutated = true; }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }

    return CursorY;
}

} // namespace Frontier
