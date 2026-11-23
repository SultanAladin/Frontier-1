/*============================================================================================================================================
                                                       NATIVEGEOMETRYINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone NativeGeometryInspector (Category 2 Geometry: Native .GeometrySpace, Cluster LOD, Local SDF, OpenPBR)

#include "Engine/Editor/Inspectors/NativeGeometryInspector.h"

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

void NativeGeometryInspector::UpdateSpecimenSurface(const NativeGeometryData& Instance)
{
    uint64_t Sig = 0x4A710002ull;
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.ClusterCount);
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.LocalSdfEnabled ? 1ull : 0ull);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.AlbedoTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.AlbedoTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.AlbedoTint.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.Roughness);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.Metallic);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.SelfGlow);

    const uint32_t W = 224u, H = 148u;
    SpecimenPreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float CX = static_cast<float>(W) * 0.5f, CY = static_cast<float>(H) * 0.52f;
        const float LX = -0.48f, LY = -0.60f, LZ = 0.64f;
        const float Rough   = std::clamp(Instance.Surface.Roughness, 0.06f, 1.0f);
        const float SpecPow = std::max(4.0f, 160.0f * (1.0f - Rough) * (1.0f - Rough));
        const float HLen    = std::sqrt(LX * LX + LY * LY + (LZ + 1.0f) * (LZ + 1.0f));

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y) + 0.5f;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) + 0.5f - CX, DY = FY - CY;
                const float RadDist = std::sqrt(DX * DX + DY * DY) / (static_cast<float>(W) * 0.65f);
                float OutR = std::max(0.03f, 0.13f - RadDist * 0.08f);
                float OutG = std::max(0.03f, 0.14f - RadDist * 0.08f);
                float OutB = std::max(0.04f, 0.16f - RadDist * 0.09f);

                const float FloorD = (DX * DX) / (58.0f * 58.0f) + ((FY - (CY + 44.0f)) * (FY - (CY + 44.0f))) / 72.0f;
                if (FloorD < 1.0f) { const float Sh = 1.0f - (1.0f - FloorD) * 0.58f; OutR *= Sh; OutG *= Sh; OutB *= Sh; }

                // Multi-part bevelled native assembly silhouette (lower hull + upper turret + hub)
                bool Inside = false;
                float NX = 0.0f, NY = 0.0f, NZ = 1.0f;
                const float HullD = std::max(std::fabs(DX) - 42.0f, std::fabs(DY - 10.0f) - 18.0f);
                const float RoofD = std::max(std::fabs(DX + 4.0f) - 24.0f, std::fabs(DY + 16.0f) - 14.0f);
                const float HubD  = std::sqrt((DX - 18.0f) * (DX - 18.0f) + (DY + 12.0f) * (DY + 12.0f)) - 14.0f;
                if (HubD <= 0.0f)
                {
                    Inside = true;
                    NX = (DX - 18.0f) / 14.0f; NY = (DY + 12.0f) / 14.0f;
                    NZ = std::sqrt(std::max(0.05f, 1.0f - NX * NX - NY * NY));
                }
                else if (RoofD <= 0.0f)
                {
                    Inside = true;
                    NX = ((DX + 4.0f) / 24.0f) * 0.45f; NY = ((DY + 16.0f) / 14.0f) * 0.45f - 0.18f;
                    NZ = std::sqrt(std::max(0.08f, 1.0f - NX * NX - NY * NY));
                }
                else if (HullD <= 0.0f)
                {
                    Inside = true;
                    NX = (DX / 42.0f) * 0.42f; NY = ((DY - 10.0f) / 18.0f) * 0.40f;
                    NZ = std::sqrt(std::max(0.08f, 1.0f - NX * NX - NY * NY));
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
                    OutR = Instance.Surface.AlbedoTint.R * DiffGain + SpecR * SpecTerm + Rim * 0.56f + Instance.Surface.AlbedoTint.R * Instance.Surface.SelfGlow;
                    OutG = Instance.Surface.AlbedoTint.G * DiffGain + SpecG * SpecTerm + Rim * 0.68f + Instance.Surface.AlbedoTint.G * Instance.Surface.SelfGlow;
                    OutB = Instance.Surface.AlbedoTint.B * DiffGain + SpecB * SpecTerm + Rim * 0.86f + Instance.Surface.AlbedoTint.B * Instance.Surface.SelfGlow;
                }
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(OutR); Pixels[Off + 1u] = ToneSrgb8(OutG); Pixels[Off + 2u] = ToneSrgb8(OutB); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float NativeGeometryInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, NativeGeometryData& Instance,
    GeometricRaster::WorldSceneSpecification& Scene,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 5u> Pages = {{ "Overview", "Geometry", "Transform", "Material", "Physics" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        UpdateSpecimenSurface(Instance);
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 236.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Native geometry", ".GeometrySpace");
        SpecimenPreview.Present(Surface, { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, 148.0f }, 12.0f);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { LeftX + PadX, PrevSec.Bottom() - 24.0f }, BodyW, 10.0f, InspectorMetrics::MutedInk, Instance.PackageReference);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        char TriTxt[16]{}, CluTxt[16]{}, LodTxt[16]{}, SltTxt[16]{};
        std::snprintf(TriTxt, sizeof(TriTxt), "%.1f", static_cast<float>(Instance.TriangleCount) * 0.001f);
        std::snprintf(CluTxt, sizeof(CluTxt), "%u", Instance.ClusterCount);
        std::snprintf(LodTxt, sizeof(LodTxt), "%u", Instance.LodTierCount);
        std::snprintf(SltTxt, sizeof(SltTxt), "%u", static_cast<unsigned>(Instance.Surface.MaterialSlotOrdinal));
        const std::array<InspectorKpiMetricSpec, 4u> Kpis = {{
            { TriTxt, "k",  "TRIANGLES" },
            { CluTxt, "",   "CLUSTERS"  },
            { LodTxt, "",   "LOD TIERS" },
            { SltTxt, "",   "MAT SLOT"  },
        }};
        CursorY = InspectorPrimitives::ConstructKpiMetricGrid(Surface, LeftX, CursorY, InnerWidth, Kpis);

        const std::array<InspectorQuickSwitchSpec, 4u> Quick = {{
            { "Visible",        InspectorSwitchSymbol::Eye,     Entry.VisibilityEnabled, false },
            { "Cast shadows",   InspectorSwitchSymbol::Shadow,  Entry.CastShadows,       false },
            { "Local SDF",      InspectorSwitchSymbol::Cube,    Instance.LocalSdfEnabled,false },
            { "Dynamic motion", InspectorSwitchSymbol::Physics, Entry.DynamicMotion,     false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Entry.CastShadows = !Entry.CastShadows; OutMutated = true; }
        else if (Clicked == 2) { Instance.LocalSdfEnabled = !Instance.LocalSdfEnabled; OutMutated = true; }
        else if (Clicked == 3) { Entry.DynamicMotion = !Entry.DynamicMotion; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "native", "cluster-lod" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 314.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Native cluster & SDF");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Package", Instance.PackageReference.c_str());
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y + 4.0f, BodyW, 7601u, "LOD pixel error", "px",  "%.2f", 0.25f, 8.0f,  Instance.LodPixelError,      ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y,        BodyW, 7602u, "Crease angle",    "deg", "%.0f", 0.0f,  180.0f, Instance.CreaseAngleDegrees, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y,        BodyW, "Local SDF volume", Instance.LocalSdfEnabled, Input, OutMutated);
        (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y,      BodyW, "Double-sided",     Instance.DoubleSided,     Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 7620u, ActiveDragId, Input, OutMutated);
    }
    else if (ActivePage == 3u)
    {
        bool MatChanged = false;
        CursorY = InspectorPrimitives::ConstructMaterialSection(Surface, LeftX, CursorY, InnerWidth, Instance.Surface, 7640u, ActiveDragId, Input, MatChanged);
        if (MatChanged) { SyncMaterialToScene(Instance.Surface, Scene); OutMutated = true; }
    }
    else
    {
        CursorY = InspectorPrimitives::ConstructPhysicsSection(Surface, LeftX, CursorY, InnerWidth, Entry, Instance.LocalSdfEnabled ? "Local SDF hull" : "Convex bounds", Input, OutMutated);
    }

    return CursorY;
}

} // namespace Frontier
