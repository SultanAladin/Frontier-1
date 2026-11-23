/*============================================================================================================================================
                                                          GEOMETRYINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance GeometryInspector (retained PBR specimen preview, Transform, OpenPBR Material, Physics)

#include "Engine/Editor/Inspectors/GeometryInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

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
} // namespace

void GeometryInspector::UpdateSpecimenSurface(const OutlinerRecordEntry& Entry, const GeometrySpecimenData& Instance)
{
    const bool BoxForm = (Entry.DomainTag.find("cube") != std::string::npos || Entry.DomainTag.find("box") != std::string::npos);
    uint64_t Sig = 0x0B008008ull;
    Sig = InspectorPrimitives::MixRevisionU64(Sig, BoxForm ? 1ull : 0ull);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.AlbedoTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.AlbedoTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.AlbedoTint.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Roughness);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Metallic);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.SelfGlow);

    const uint32_t W = 224u, H = 148u;
    SpecimenPreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float CX = static_cast<float>(W) * 0.5f, CY = static_cast<float>(H) * 0.50f, R = 46.0f;
        const float LX = -0.52f, LY = -0.58f, LZ = 0.63f;
        const float Rough   = std::clamp(Instance.Roughness, 0.06f, 1.0f);
        const float SpecPow = std::max(4.0f, 160.0f * (1.0f - Rough) * (1.0f - Rough));
        const float HLen    = std::sqrt(LX * LX + LY * LY + (LZ + 1.0f) * (LZ + 1.0f));

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

                const float FloorD = (DX * DX) / (R * R * 1.45f) + ((FY - (CY + R + 6.0f)) * (FY - (CY + R + 6.0f))) / 80.0f;
                if (FloorD < 1.0f) { const float Sh = 1.0f - (1.0f - FloorD) * 0.62f; OutR *= Sh; OutG *= Sh; OutB *= Sh; }

                bool Inside = false;
                float NX = 0.0f, NY = 0.0f, NZ = 1.0f;
                if (BoxForm)
                {
                    if (std::fabs(DX) <= R * 0.82f && std::fabs(DY) <= R * 0.82f)
                    {
                        Inside = true;
                        NX = (DX / (R * 0.82f)) * 0.45f;
                        NY = (DY / (R * 0.82f)) * 0.45f;
                        NZ = std::sqrt(std::max(0.05f, 1.0f - NX * NX - NY * NY));
                    }
                }
                else
                {
                    const float D2 = (DX * DX + DY * DY) / (R * R);
                    if (D2 <= 1.0f) { Inside = true; NX = DX / R; NY = DY / R; NZ = std::sqrt(std::max(0.0f, 1.0f - D2)); }
                }
                if (Inside)
                {
                    const float NDotL    = std::max(0.0f, NX * LX + NY * LY + NZ * LZ);
                    const float NDotH    = std::max(0.0f, (NX * LX + NY * LY + NZ * (LZ + 1.0f)) / HLen);
                    const float SpecTerm = std::pow(NDotH, SpecPow) * (0.25f + 0.85f * (1.0f - Rough));
                    const float Rim      = std::pow(1.0f - NZ, 3.0f) * 0.28f;
                    const float DiffGain = (1.0f - Instance.Metallic * 0.85f) * (0.16f + 0.84f * NDotL);
                    const float SpecR = (1.0f - Instance.Metallic) + Instance.Metallic * Instance.AlbedoTint.R;
                    const float SpecG = (1.0f - Instance.Metallic) + Instance.Metallic * Instance.AlbedoTint.G;
                    const float SpecB = (1.0f - Instance.Metallic) + Instance.Metallic * Instance.AlbedoTint.B;
                    OutR = Instance.AlbedoTint.R * DiffGain + SpecR * SpecTerm + Rim * 0.60f + Instance.AlbedoTint.R * Instance.SelfGlow;
                    OutG = Instance.AlbedoTint.G * DiffGain + SpecG * SpecTerm + Rim * 0.68f + Instance.AlbedoTint.G * Instance.SelfGlow;
                    OutB = Instance.AlbedoTint.B * DiffGain + SpecB * SpecTerm + Rim * 0.82f + Instance.AlbedoTint.B * Instance.SelfGlow;
                }
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(OutR); Pixels[Off + 1u] = ToneSrgb8(OutG); Pixels[Off + 2u] = ToneSrgb8(OutB); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float GeometryInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, GeometrySpecimenData& Instance,
    GeometricRaster::WorldSceneSpecification& Scene,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 4u> Pages = {{ "Overview", "Transform", "Material", "Physics" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        UpdateSpecimenSurface(Entry, Instance);
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 236.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Specimen preview", "Retained");
        SpecimenPreview.Present(Surface, { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, 148.0f }, 12.0f);
        char DimBuf[64]{};
        std::snprintf(DimBuf, sizeof(DimBuf), "Scale %.2f x %.2f x %.2f m", Entry.ScaleX, Entry.ScaleY, Entry.ScaleZ);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { LeftX + PadX, PrevSec.Bottom() - 24.0f }, BodyW, 10.0f, InspectorMetrics::MutedInk, DimBuf);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 4u> Quick = {{
            { "Visible",        InspectorSwitchSymbol::Eye,     Entry.VisibilityEnabled, false },
            { "Cast shadows",   InspectorSwitchSymbol::Shadow,  Entry.CastShadows,       false },
            { "Dynamic motion", InspectorSwitchSymbol::Physics, Entry.DynamicMotion,     false },
            { "Locked",         InspectorSwitchSymbol::Cube,    Entry.Locked,            false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Entry.CastShadows = !Entry.CastShadows; OutMutated = true; }
        else if (Clicked == 2) { Entry.DynamicMotion = !Entry.DynamicMotion; OutMutated = true; }
        else if (Clicked == 3) { Entry.Locked = !Entry.Locked; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "scene", "geometry" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 250.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Position");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7301u, "X", "m", "%.2f", -500.0f, 500.0f, Entry.PositionX, ActiveDragId, Input, OutMutated, Entry.Locked);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7302u, "Y", "m", "%.2f", -500.0f, 500.0f, Entry.PositionY, ActiveDragId, Input, OutMutated, Entry.Locked);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7303u, "Z", "m", "%.2f", -500.0f, 500.0f, Entry.PositionZ, ActiveDragId, Input, OutMutated, Entry.Locked);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 250.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Rotation");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7304u, "Pitch", "deg", "%.1f", -180.0f, 180.0f, Entry.RotationX, ActiveDragId, Input, OutMutated, Entry.Locked);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7305u, "Roll",  "deg", "%.1f", -180.0f, 180.0f, Entry.RotationY, ActiveDragId, Input, OutMutated, Entry.Locked);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7306u, "Yaw",   "deg", "%.1f", -180.0f, 180.0f, Entry.RotationZ, ActiveDragId, Input, OutMutated, Entry.Locked);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 250.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Scale");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7307u, "Scale X", "x", "%.2f", 0.05f, 50.0f, Entry.ScaleX, ActiveDragId, Input, OutMutated, Entry.Locked);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7308u, "Scale Y", "x", "%.2f", 0.05f, 50.0f, Entry.ScaleY, ActiveDragId, Input, OutMutated, Entry.Locked);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7309u, "Scale Z", "x", "%.2f", 0.05f, 50.0f, Entry.ScaleZ, ActiveDragId, Input, OutMutated, Entry.Locked);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 340.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "OpenPBR surface material");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 7401u, "Albedo tint", Instance.AlbedoTint, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7410u, "Roughness", "", "%.2f", 0.04f, 1.0f, Instance.Roughness, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7411u, "Metallic", "", "%.2f", 0.0f, 1.0f, Instance.Metallic, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7412u, "Self glow", "x", "%.2f", 0.0f, 4.0f, Instance.SelfGlow, ActiveDragId, Input, OutMutated);
        if (OutMutated && Scene.MaterialRegister.QuerySlotCount() > 0u)
        {
            auto Authored = Scene.MaterialRegister.InspectAuthoredMaterial(0u);
            Authored.Diffuse.BaseColor  = Instance.AlbedoTint;
            Authored.Specular.Roughness = Instance.Roughness;
            Authored.Specular.Metalness = Instance.Metallic;
            Scene.MaterialRegister.UpdateMaterialSlot(0u, Authored);
        }
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 232.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Physics & visibility");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Visible in viewport", Entry.VisibilityEnabled, Input, OutMutated);
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Cast shadows", Entry.CastShadows, Input, OutMutated);
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Dynamic motion", Entry.DynamicMotion, Input, OutMutated);
        (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Lock transform", Entry.Locked, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
