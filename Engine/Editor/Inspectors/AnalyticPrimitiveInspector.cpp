/*============================================================================================================================================
                                                      ANALYTICPRIMITIVEINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone AnalyticPrimitiveInspector (Category 1 Geometry: analytic shape preview + Shape/Transform/Material/Physics)

#include "Engine/Editor/Inspectors/AnalyticPrimitiveInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
constexpr std::array<const char*, 7u> FormLabels = {{
    "Sphere", "Box", "Cylinder", "Cone", "Torus", "Capsule", "Plane",
}};

inline uint8_t ToneSrgb8(float Hdr) noexcept
{
    const float M = std::clamp(Hdr / (1.0f + std::max(0.0f, Hdr)), 0.0f, 1.0f);
    const float S = (M <= 0.0031308f) ? (M * 12.92f) : (1.055f * std::pow(M, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
}

void SyncMaterialToScene(const GeometrySurfaceBinding& Binding, GeometricRaster::WorldSceneSpecification& Scene) noexcept
{
    if (Scene.MaterialRegister.QuerySlotCount() == 0u) return;
    const uint16_t Slot = std::min<uint16_t>(Binding.MaterialSlotOrdinal, static_cast<uint16_t>(Scene.MaterialRegister.QuerySlotCount() - 1u));
    auto Authored = Scene.MaterialRegister.InspectAuthoredMaterial(Slot);
    Authored.Diffuse.BaseColor  = Binding.AlbedoTint;
    Authored.Specular.Roughness = Binding.Roughness;
    Authored.Specular.Metalness = Binding.Metallic;
    Scene.MaterialRegister.UpdateMaterialSlot(Slot, Authored);
}
} // namespace

void AnalyticPrimitiveInspector::UpdatePrimitiveSurface(const AnalyticPrimitiveData& Instance)
{
    uint64_t Sig = 0xA1010001ull;
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.FormIndex);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiusMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.HeightMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.MinorRadiusMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.AlbedoTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.AlbedoTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.AlbedoTint.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.Roughness);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.Metallic);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.SelfGlow);

    const uint32_t W = 224u, H = 148u;
    PrimitivePreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float CX = static_cast<float>(W) * 0.5f, CY = static_cast<float>(H) * 0.50f, R = 44.0f;
        const float LX = -0.52f, LY = -0.58f, LZ = 0.63f;
        const float Rough   = std::clamp(Instance.Surface.Roughness, 0.06f, 1.0f);
        const float SpecPow = std::max(4.0f, 160.0f * (1.0f - Rough) * (1.0f - Rough));
        const float HLen    = std::sqrt(LX * LX + LY * LY + (LZ + 1.0f) * (LZ + 1.0f));
        const float Aspect  = std::clamp(Instance.HeightMeters / std::max(0.2f, Instance.RadiusMeters * 2.0f), 0.5f, 1.6f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y) + 0.5f;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) + 0.5f - CX, DY = FY - CY;
                const float RadDist = std::sqrt(DX * DX + DY * DY) / (static_cast<float>(W) * 0.65f);
                float OutR = std::max(0.03f, 0.14f - RadDist * 0.09f);
                float OutG = std::max(0.03f, 0.15f - RadDist * 0.09f);
                float OutB = std::max(0.04f, 0.17f - RadDist * 0.10f);

                const float FloorD = (DX * DX) / (R * R * 1.55f) + ((FY - (CY + R + 6.0f)) * (FY - (CY + R + 6.0f))) / 84.0f;
                if (FloorD < 1.0f) { const float Sh = 1.0f - (1.0f - FloorD) * 0.60f; OutR *= Sh; OutG *= Sh; OutB *= Sh; }

                bool Inside = false;
                float NX = 0.0f, NY = 0.0f, NZ = 1.0f;

                if (Instance.FormIndex == 0u) // Sphere
                {
                    const float D2 = (DX * DX + DY * DY) / (R * R);
                    if (D2 <= 1.0f) { Inside = true; NX = DX / R; NY = DY / R; NZ = std::sqrt(std::max(0.0f, 1.0f - D2)); }
                }
                else if (Instance.FormIndex == 1u) // Box
                {
                    const float HalfW = R * 0.82f, HalfH = R * 0.78f * std::clamp(Aspect, 0.65f, 1.15f);
                    if (std::fabs(DX) <= HalfW && std::fabs(DY) <= HalfH)
                    {
                        Inside = true;
                        NX = (DX / HalfW) * 0.42f; NY = (DY / HalfH) * 0.42f;
                        NZ = std::sqrt(std::max(0.08f, 1.0f - NX * NX - NY * NY));
                    }
                }
                else if (Instance.FormIndex == 2u) // Cylinder
                {
                    const float CW = R * 0.72f, CH = R * 0.92f;
                    const float TopEll = (DX * DX) / (CW * CW) + ((DY + CH * 0.75f) * (DY + CH * 0.75f)) / (CW * CW * 0.16f);
                    if (TopEll <= 1.0f) { Inside = true; NX = 0.0f; NY = -0.82f; NZ = 0.57f; }
                    else if (std::fabs(DX) <= CW && DY >= -CH * 0.75f && DY <= CH * 0.82f)
                    {
                        Inside = true;
                        NX = DX / CW; NY = 0.06f; NZ = std::sqrt(std::max(0.05f, 1.0f - NX * NX));
                    }
                }
                else if (Instance.FormIndex == 3u) // Cone
                {
                    const float CH = R * 0.96f;
                    if (DY >= -CH && DY <= CH * 0.85f)
                    {
                        const float T = (DY + CH) / (CH * 1.85f);
                        const float HalfW = std::max(1.5f, R * 0.92f * T);
                        if (std::fabs(DX) <= HalfW)
                        {
                            Inside = true;
                            NX = (DX / HalfW) * 0.88f; NY = -0.34f;
                            NZ = std::sqrt(std::max(0.05f, 1.0f - NX * NX - NY * NY));
                        }
                    }
                }
                else if (Instance.FormIndex == 4u) // Torus
                {
                    const float MajR = R * 0.68f, MinR = R * std::clamp(0.22f + Instance.MinorRadiusMeters * 0.45f, 0.18f, 0.36f);
                    const float Dist = std::sqrt(DX * DX + DY * DY);
                    const float RingD = std::fabs(Dist - MajR);
                    if (RingD <= MinR && Dist > 1.0e-3f)
                    {
                        Inside = true;
                        const float U = (Dist - MajR) / MinR;
                        NX = (DX / Dist) * U; NY = (DY / Dist) * U;
                        NZ = std::sqrt(std::max(0.05f, 1.0f - U * U));
                    }
                }
                else if (Instance.FormIndex == 5u) // Capsule
                {
                    const float CapR = R * 0.54f, HalfSeg = R * 0.42f;
                    const float ClampedY = std::clamp(DY, -HalfSeg, HalfSeg);
                    const float Cdy = DY - ClampedY;
                    const float D2 = (DX * DX + Cdy * Cdy) / (CapR * CapR);
                    if (D2 <= 1.0f) { Inside = true; NX = DX / CapR; NY = Cdy / CapR; NZ = std::sqrt(std::max(0.0f, 1.0f - D2)); }
                }
                else // Plane
                {
                    const float PerspY = DY / (R * 0.55f);
                    const float HalfW = R * (1.20f + PerspY * 0.35f);
                    if (DY >= -R * 0.38f && DY <= R * 0.62f && std::fabs(DX) <= HalfW)
                    {
                        Inside = true; NX = 0.0f; NY = -0.78f; NZ = 0.62f;
                    }
                }

                if (Inside)
                {
                    const float NDotL    = std::max(0.0f, NX * LX + NY * LY + NZ * LZ);
                    const float NDotH    = std::max(0.0f, (NX * LX + NY * LY + NZ * (LZ + 1.0f)) / HLen);
                    const float SpecTerm = std::pow(NDotH, SpecPow) * (0.25f + 0.85f * (1.0f - Rough));
                    const float Rim      = std::pow(1.0f - NZ, 3.0f) * 0.28f;
                    const float DiffGain = (1.0f - Instance.Surface.Metallic * 0.85f) * (0.16f + 0.84f * NDotL);
                    const float SpecR = (1.0f - Instance.Surface.Metallic) + Instance.Surface.Metallic * Instance.Surface.AlbedoTint.R;
                    const float SpecG = (1.0f - Instance.Surface.Metallic) + Instance.Surface.Metallic * Instance.Surface.AlbedoTint.G;
                    const float SpecB = (1.0f - Instance.Surface.Metallic) + Instance.Surface.Metallic * Instance.Surface.AlbedoTint.B;
                    OutR = Instance.Surface.AlbedoTint.R * DiffGain + SpecR * SpecTerm + Rim * 0.60f + Instance.Surface.AlbedoTint.R * Instance.Surface.SelfGlow;
                    OutG = Instance.Surface.AlbedoTint.G * DiffGain + SpecG * SpecTerm + Rim * 0.68f + Instance.Surface.AlbedoTint.G * Instance.Surface.SelfGlow;
                    OutB = Instance.Surface.AlbedoTint.B * DiffGain + SpecB * SpecTerm + Rim * 0.82f + Instance.Surface.AlbedoTint.B * Instance.Surface.SelfGlow;
                }
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(OutR); Pixels[Off + 1u] = ToneSrgb8(OutG); Pixels[Off + 2u] = ToneSrgb8(OutB); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float AnalyticPrimitiveInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, AnalyticPrimitiveData& Instance,
    GeometricRaster::WorldSceneSpecification& Scene,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 5u> Pages = {{ "Overview", "Shape", "Transform", "Material", "Physics" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;
    const char* ActiveFormLabel = FormLabels[std::min<std::size_t>(Instance.FormIndex, FormLabels.size() - 1u)];

    if (ActivePage == 0u)
    {
        UpdatePrimitiveSurface(Instance);
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 236.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Analytical primitive", ActiveFormLabel);
        PrimitivePreview.Present(Surface, { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, 148.0f }, 12.0f);
        char DimBuf[72]{};
        std::snprintf(DimBuf, sizeof(DimBuf), "R %.2f m  ·  H %.2f m  ·  %u seg", Instance.RadiusMeters, Instance.HeightMeters, Instance.RadialSegments);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { LeftX + PadX, PrevSec.Bottom() - 24.0f }, BodyW, 10.0f, InspectorMetrics::MutedInk, DimBuf);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 4u> Quick = {{
            { "Visible",        InspectorSwitchSymbol::Eye,     Entry.VisibilityEnabled, false },
            { "Cast shadows",   InspectorSwitchSymbol::Shadow,  Entry.CastShadows,       false },
            { "Watertight",     InspectorSwitchSymbol::Cube,    Instance.Watertight,     false },
            { "Dynamic motion", InspectorSwitchSymbol::Physics, Entry.DynamicMotion,     false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Entry.CastShadows = !Entry.CastShadows; OutMutated = true; }
        else if (Clicked == 2) { Instance.Watertight = !Instance.Watertight; OutMutated = true; }
        else if (Clicked == 3) { Entry.DynamicMotion = !Entry.DynamicMotion; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "analytical", "primitive" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 492.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Analytic form & dimensions");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        int32_t PickForm = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Primitive form", FormLabels, Instance.FormIndex, 3u, Input, PickForm);
        if (PickForm >= 0) { Instance.FormIndex = static_cast<uint32_t>(PickForm); OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7501u, "Radius",       "m",  "%.2f", 0.05f, 50.0f, Instance.RadiusMeters,      ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7502u, "Height",       "m",  "%.2f", 0.05f, 50.0f, Instance.HeightMeters,      ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7503u, "Minor radius", "m",  "%.2f", 0.02f, 10.0f, Instance.MinorRadiusMeters, ActiveDragId, Input, OutMutated);
        float Segs = static_cast<float>(Instance.RadialSegments);
        bool SegChanged = false;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7504u, "Segments",     "",   "%.0f", 8.0f,  128.0f, Segs, ActiveDragId, Input, SegChanged);
        if (SegChanged) { Instance.RadialSegments = static_cast<uint32_t>(std::clamp(std::lround(Segs), 8l, 128l)); OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7505u, "Edge bevel",   "mm", "%.1f", 0.0f,  40.0f, Instance.BevelMillimeters,  ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Watertight solid", Instance.Watertight, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 7520u, ActiveDragId, Input, OutMutated);
    }
    else if (ActivePage == 3u)
    {
        bool MatChanged = false;
        CursorY = InspectorPrimitives::ConstructMaterialSection(Surface, LeftX, CursorY, InnerWidth, Instance.Surface, 7540u, ActiveDragId, Input, MatChanged);
        if (MatChanged) { SyncMaterialToScene(Instance.Surface, Scene); OutMutated = true; }
    }
    else
    {
        CursorY = InspectorPrimitives::ConstructPhysicsSection(Surface, LeftX, CursorY, InnerWidth, Entry, "Exact analytic SDF", Input, OutMutated);
    }

    return CursorY;
}

} // namespace Frontier
