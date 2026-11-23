/*============================================================================================================================================
                                                      IMPORTEDGEOMETRYINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone ImportedGeometryInspector (Category 3 Geometry: glTF/FBX/OBJ/USD/STEP/STL exchange import & re-import)

#include "Engine/Editor/Inspectors/ImportedGeometryInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
constexpr std::array<const char*, 6u> ExchangeLabels = {{ "glTF", "FBX", "OBJ", "USD", "STEP", "STL" }};
constexpr std::array<const char*, 4u> UnitLabels     = {{ "mm", "cm", "m", "in" }};
constexpr std::array<const char*, 2u> AxisLabels     = {{ "Z-Up", "Y-Up" }};

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

void ImportedGeometryInspector::UpdateExchangeSurface(const ImportedGeometryData& Instance)
{
    uint64_t Sig = 0x1A900003ull;
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.ExchangeFormatIndex);
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.ImportRevision);
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.RecomputeNormals ? 1ull : 0ull);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.AlbedoTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.AlbedoTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.AlbedoTint.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.Roughness);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.Metallic);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.SelfGlow);

    const uint32_t W = 224u, H = 148u;
    ExchangePreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float CX = static_cast<float>(W) * 0.5f, CY = static_cast<float>(H) * 0.50f;
        const float LX = -0.50f, LY = -0.58f, LZ = 0.64f;
        const float Rough   = std::clamp(Instance.Surface.Roughness, 0.06f, 1.0f);
        const float SpecPow = std::max(4.0f, 160.0f * (1.0f - Rough) * (1.0f - Rough));
        const float HLen    = std::sqrt(LX * LX + LY * LY + (LZ + 1.0f) * (LZ + 1.0f));
        const bool  CadMode = (Instance.ExchangeFormatIndex >= 4u); // STEP / STL subtle technical grid

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y) + 0.5f;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) + 0.5f - CX, DY = FY - CY;
                const float RadDist = std::sqrt(DX * DX + DY * DY) / (static_cast<float>(W) * 0.65f);
                float OutR = std::max(0.03f, (CadMode ? 0.10f : 0.13f) - RadDist * 0.07f);
                float OutG = std::max(0.03f, (CadMode ? 0.13f : 0.14f) - RadDist * 0.07f);
                float OutB = std::max(0.04f, (CadMode ? 0.17f : 0.16f) - RadDist * 0.08f);

                if (CadMode && ((X % 16u == 0u) || (Y % 16u == 0u)))
                {
                    OutR += 0.025f; OutG += 0.035f; OutB += 0.050f;
                }

                // Imported bracket / flange silhouette with central bore
                const float OuterR = std::sqrt(DX * DX + DY * DY);
                const bool  InFlange = (OuterR <= 46.0f && OuterR >= 16.0f);
                const bool  InLug    = (std::fabs(DX) <= 56.0f && std::fabs(DY) <= 18.0f && OuterR >= 16.0f);
                if (InFlange || InLug)
                {
                    const float NX = std::clamp(DX / 56.0f, -0.85f, 0.85f) * 0.62f;
                    const float NY = std::clamp(DY / 46.0f, -0.85f, 0.85f) * 0.62f;
                    const float NZ = std::sqrt(std::max(0.08f, 1.0f - NX * NX - NY * NY));
                    const float NDotL    = std::max(0.0f, NX * LX + NY * LY + NZ * LZ);
                    const float NDotH    = std::max(0.0f, (NX * LX + NY * LY + NZ * (LZ + 1.0f)) / HLen);
                    const float SpecTerm = std::pow(NDotH, SpecPow) * (0.25f + 0.85f * (1.0f - Rough));
                    const float Rim      = std::pow(1.0f - NZ, 3.0f) * 0.30f;
                    const float DiffGain = (1.0f - Instance.Surface.Metallic * 0.85f) * (0.18f + 0.82f * NDotL);
                    const float SpecR = (1.0f - Instance.Surface.Metallic) + Instance.Surface.Metallic * Instance.Surface.AlbedoTint.R;
                    const float SpecG = (1.0f - Instance.Surface.Metallic) + Instance.Surface.Metallic * Instance.Surface.AlbedoTint.G;
                    const float SpecB = (1.0f - Instance.Surface.Metallic) + Instance.Surface.Metallic * Instance.Surface.AlbedoTint.B;
                    OutR = Instance.Surface.AlbedoTint.R * DiffGain + SpecR * SpecTerm + Rim * 0.55f + Instance.Surface.AlbedoTint.R * Instance.Surface.SelfGlow;
                    OutG = Instance.Surface.AlbedoTint.G * DiffGain + SpecG * SpecTerm + Rim * 0.72f + Instance.Surface.AlbedoTint.G * Instance.Surface.SelfGlow;
                    OutB = Instance.Surface.AlbedoTint.B * DiffGain + SpecB * SpecTerm + Rim * 0.90f + Instance.Surface.AlbedoTint.B * Instance.Surface.SelfGlow;
                    if (std::fabs(OuterR - 45.2f) < 1.0f || std::fabs(OuterR - 16.8f) < 1.0f)
                    {
                        OutR += 0.14f; OutG += 0.22f; OutB += 0.32f;
                    }
                }
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(OutR); Pixels[Off + 1u] = ToneSrgb8(OutG); Pixels[Off + 2u] = ToneSrgb8(OutB); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float ImportedGeometryInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, ImportedGeometryData& Instance,
    GeometricRaster::WorldSceneSpecification& Scene,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 5u> Pages = {{ "Overview", "Import", "Transform", "Material", "Physics" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;
    const char* FmtLabel  = ExchangeLabels[std::min<std::size_t>(Instance.ExchangeFormatIndex, ExchangeLabels.size() - 1u)];
    const char* UnitLabel = UnitLabels[std::min<std::size_t>(Instance.SourceUnitIndex, UnitLabels.size() - 1u)];

    if (ActivePage == 0u)
    {
        UpdateExchangeSurface(Instance);
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 236.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Imported geometry", FmtLabel);
        ExchangePreview.Present(Surface, { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, 148.0f }, 12.0f);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { LeftX + PadX, PrevSec.Bottom() - 24.0f }, BodyW, 10.0f, InspectorMetrics::MutedInk, Instance.SourcePath);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        char SclTxt[16]{}, TriTxt[16]{}, UvTxt[16]{};
        std::snprintf(SclTxt, sizeof(SclTxt), "%.2f", Instance.ImportScaleFactor);
        std::snprintf(TriTxt, sizeof(TriTxt), "%.1f", static_cast<float>(Instance.TriangleCount) * 0.001f);
        std::snprintf(UvTxt,  sizeof(UvTxt),  "%u",   Instance.UvChannelCount);
        const std::array<InspectorKpiMetricSpec, 4u> Kpis = {{
            { FmtLabel, "",  "FORMAT"    },
            { SclTxt,   "x", "SCALE"     },
            { TriTxt,   "k", "TRIANGLES" },
            { UvTxt,    "",  "UV SETS"   },
        }};
        CursorY = InspectorPrimitives::ConstructKpiMetricGrid(Surface, LeftX, CursorY, InnerWidth, Kpis);

        const std::array<InspectorQuickSwitchSpec, 4u> Quick = {{
            { "Visible",       InspectorSwitchSymbol::Eye,     Entry.VisibilityEnabled,      false },
            { "Cast shadows",  InspectorSwitchSymbol::Shadow,  Entry.CastShadows,            false },
            { "Cluster LOD",   InspectorSwitchSymbol::Cube,    Instance.BuildClusterPyramid, false },
            { "Local SDF",     InspectorSwitchSymbol::Orbit,   Instance.BuildLocalSdf,       false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Entry.CastShadows = !Entry.CastShadows; OutMutated = true; }
        else if (Clicked == 2) { Instance.BuildClusterPyramid = !Instance.BuildClusterPyramid; OutMutated = true; }
        else if (Clicked == 3) { Instance.BuildLocalSdf = !Instance.BuildLocalSdf; OutMutated = true; }

        const std::array<const char*, 2u> Tags = {{ "imported", FmtLabel }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        char RevBuf[24]{};
        std::snprintf(RevBuf, sizeof(RevBuf), "Rev %u", Instance.ImportRevision);
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 558.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Exchange import settings", RevBuf);
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Source file", Instance.SourcePath.c_str());
        Y = InspectorPrimitives::ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Source unit", UnitLabel);
        int32_t PickFmt = -1, PickUnit = -1, PickAxis = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y + 2.0f, BodyW, "Exchange format", ExchangeLabels, Instance.ExchangeFormatIndex, 3u, Input, PickFmt);
        if (PickFmt >= 0) { Instance.ExchangeFormatIndex = static_cast<uint32_t>(PickFmt); OutMutated = true; }
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y,        BodyW, "Source units",    UnitLabels,     Instance.SourceUnitIndex,     4u, Input, PickUnit);
        if (PickUnit >= 0) { Instance.SourceUnitIndex = static_cast<uint32_t>(PickUnit); OutMutated = true; }
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y,        BodyW, "Up axis",         AxisLabels,     Instance.UpAxisIndex,         2u, Input, PickAxis);
        if (PickAxis >= 0) { Instance.UpAxisIndex = static_cast<uint32_t>(PickAxis); OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y,        BodyW, 7701u, "Import scale", "x",   "%.2f", 0.01f, 100.0f, Instance.ImportScaleFactor,  ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y,        BodyW, 7702u, "Crease angle", "deg", "%.0f", 0.0f,  180.0f, Instance.CreaseAngleDegrees, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y,        BodyW, "Recompute normals", Instance.RecomputeNormals, Input, OutMutated);
        if (InspectorPrimitives::ConstructSectionActionButton(Surface, LeftX + PadX, Y + 4.0f, BodyW, "Re-import from source", Input, true))
        {
            ++Instance.ImportRevision;
            OutMutated = true;
        }
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 7720u, ActiveDragId, Input, OutMutated);
    }
    else if (ActivePage == 3u)
    {
        bool MatChanged = false;
        CursorY = InspectorPrimitives::ConstructMaterialSection(Surface, LeftX, CursorY, InnerWidth, Instance.Surface, 7740u, ActiveDragId, Input, MatChanged);
        if (MatChanged) { SyncMaterialToScene(Instance.Surface, Scene); OutMutated = true; }
    }
    else
    {
        CursorY = InspectorPrimitives::ConstructPhysicsSection(Surface, LeftX, CursorY, InnerWidth, Entry, Instance.BuildLocalSdf ? "Baked SDF hull" : "Imported hull", Input, OutMutated);
    }

    return CursorY;
}

} // namespace Frontier
