/*============================================================================================================================================
                                                       GEOMETRYINSPECTORSHARED.H
============================================================================================================================================*/
// 🧩 Shared 1:1 HTML parity 8-tab Geometry Inspector renderer (matches cube__0..7: Overview, Components, Geometry, Material, UV, Fracture, SDF, LOD)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

namespace Frontier::GeometryUi
{

inline constexpr ColorRgba CardFill   { 26u, 26u, 27u, 255u };
inline constexpr ColorRgba CardBorder { 255u, 255u, 255u, 20u };
inline constexpr ColorRgba RowFill    { 17u, 17u, 18u, 255u };

inline void ConstructGeomCard(PixelSpace& Surface, const Rectangle2D& Card) noexcept
{
    Surface.FillRectangle(Card, CardFill, InspectorMetrics::SectionRadius);
    Surface.StrokeRectangle(Card, CardBorder, InspectorMetrics::SectionRadius, 1.0f);
}

inline void DrawIsometricCube(
    PixelSpace& Surface, Point2D Center, float Size,
    ColorRgba TopCol, ColorRgba LeftCol, ColorRgba RightCol, ColorRgba EdgeCol) noexcept
{
    const float DX = Size * 0.866f;
    const float DY = Size * 0.500f;
    const Point2D Top   { Center.X,      Center.Y - Size };
    const Point2D TopL  { Center.X - DX, Center.Y - DY };
    const Point2D TopR  { Center.X + DX, Center.Y - DY };
    const Point2D Mid   { Center.X,      Center.Y };
    const Point2D BotL  { Center.X - DX, Center.Y + DY };
    const Point2D BotR  { Center.X + DX, Center.Y + DY };
    const Point2D Bot   { Center.X,      Center.Y + Size };

    // Filled horizontal scanlines for clean matte faces
    const int Steps = static_cast<int>(Size);
    for (int I = 0; I < Steps; ++I)
    {
        const float T = static_cast<float>(I) / std::max(1.0f, static_cast<float>(Steps));
        // Top diamond
        const Point2D TL { Top.X + (TopL.X - Top.X) * T, Top.Y + (TopL.Y - Top.Y) * T };
        const Point2D TR { Top.X + (TopR.X - Top.X) * T, Top.Y + (TopR.Y - Top.Y) * T };
        Surface.StrokeLine(TL, TR, TopCol, 2.0f);
        const Point2D BL { TopL.X + (Mid.X - TopL.X) * T, TopL.Y + (Mid.Y - TopL.Y) * T };
        const Point2D BR { TopR.X + (Mid.X - TopR.X) * T, TopR.Y + (Mid.Y - TopR.Y) * T };
        Surface.StrokeLine(BL, BR, TopCol, 2.0f);
        // Left face
        Surface.StrokeLine({ TopL.X, TopL.Y + Size * T }, { Mid.X, Mid.Y + Size * T }, LeftCol, 2.0f);
        // Right face
        Surface.StrokeLine({ Mid.X, Mid.Y + Size * T }, { TopR.X, TopR.Y + Size * T }, RightCol, 2.0f);
    }

    // Crisp outer edges
    Surface.StrokeLine(Top, TopL, EdgeCol, 1.3f);
    Surface.StrokeLine(Top, TopR, EdgeCol, 1.3f);
    Surface.StrokeLine(TopL, Mid, EdgeCol, 1.3f);
    Surface.StrokeLine(TopR, Mid, EdgeCol, 1.3f);
    Surface.StrokeLine(TopL, BotL, EdgeCol, 1.3f);
    Surface.StrokeLine(TopR, BotR, EdgeCol, 1.3f);
    Surface.StrokeLine(Mid, Bot, EdgeCol, 1.3f);
    Surface.StrokeLine(BotL, Bot, EdgeCol, 1.3f);
    Surface.StrokeLine(BotR, Bot, EdgeCol, 1.3f);
}

inline void RenderUvAtlasPixels(std::vector<uint8_t>& Pixels, uint32_t W, uint32_t H) noexcept
{
    const float MainW = static_cast<float>(W) * 0.72f;
    const std::array<std::array<float, 3u>, 6u> IslandCols = {{
        { 0.34f, 0.56f, 0.58f }, { 0.58f, 0.32f, 0.48f },
        { 0.44f, 0.32f, 0.58f }, { 0.54f, 0.50f, 0.28f },
        { 0.56f, 0.30f, 0.34f }, { 0.30f, 0.54f, 0.40f },
    }};
    for (uint32_t Y = 0u; Y < H; ++Y)
    {
        for (uint32_t X = 0u; X < W; ++X)
        {
            float R = 16.0f, G = 16.0f, B = 18.0f;
            if (static_cast<float>(X) < MainW)
            {
                const float U = static_cast<float>(X) / MainW;
                const float V = static_cast<float>(Y) / static_cast<float>(H);
                const bool DarkChk = ((static_cast<int>(U * 8.0f) + static_cast<int>(V * 8.0f)) & 1) == 0;
                R = DarkChk ? 24.0f : 38.0f;
                G = DarkChk ? 25.0f : 39.0f;
                B = DarkChk ? 28.0f : 43.0f;

                if (U < 0.666f)
                {
                    const int Col = (U < 0.333f) ? 0 : 1;
                    const int Row = std::min(2, static_cast<int>(V * 3.0f));
                    const auto& IC = IslandCols[static_cast<std::size_t>(Row * 2 + Col)];
                    R = R * 0.42f + IC[0] * 255.0f * 0.58f;
                    G = G * 0.42f + IC[1] * 255.0f * 0.58f;
                    B = B * 0.42f + IC[2] * 255.0f * 0.58f;
                }
            }
            else if (static_cast<float>(X) > MainW + 14.0f)
            {
                // Right column: 3 UDIM checker thumbnails (1002, 1003, 1004)
                const float SubV = (static_cast<float>(Y) / static_cast<float>(H)) * 3.0f;
                const float Fv   = SubV - std::floor(SubV);
                if (Fv > 0.06f && Fv < 0.94f)
                {
                    const bool DarkChk = (((X / 10u) + (Y / 10u)) & 1u) == 0u;
                    R = DarkChk ? 22.0f : 44.0f;
                    G = DarkChk ? 22.0f : 44.0f;
                    B = DarkChk ? 24.0f : 46.0f;
                }
            }
            const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
            Pixels[Off + 0u] = static_cast<uint8_t>(std::clamp(R, 0.0f, 255.0f));
            Pixels[Off + 1u] = static_cast<uint8_t>(std::clamp(G, 0.0f, 255.0f));
            Pixels[Off + 2u] = static_cast<uint8_t>(std::clamp(B, 0.0f, 255.0f));
            Pixels[Off + 3u] = 255u;
        }
    }
}

inline void RenderSdfSlicePixels(std::vector<uint8_t>& Pixels, uint32_t W, uint32_t H) noexcept
{
    const float Cx = static_cast<float>(W) * 0.5f;
    const float Cy = static_cast<float>(H) * 0.5f;
    const float HalfS = static_cast<float>(std::min(W, H)) * 0.36f;

    for (uint32_t Y = 0u; Y < H; ++Y)
    {
        for (uint32_t X = 0u; X < W; ++X)
        {
            const float DX = std::fabs(static_cast<float>(X) - Cx);
            const float DY = std::fabs(static_cast<float>(Y) - Cy);
            const float SD = std::max(DX, DY) - HalfS;

            float R = 26.0f, G = 32.0f, B = 42.0f;
            if (SD < 0.0f)
            {
                const float Depth = std::clamp(-SD / (HalfS * 0.55f), 0.0f, 1.0f);
                R = 62.0f  + Depth * 42.0f;
                G = 182.0f + Depth * 34.0f;
                B = 224.0f + Depth * 24.0f;
            }
            // Vivid amber-orange iso-surface contour at SD == 0
            const float Iso = std::exp(-std::pow(SD / 3.8f, 2.0f));
            R = R * (1.0f - Iso) + 244.0f * Iso;
            G = G * (1.0f - Iso) + 174.0f * Iso;
            B = B * (1.0f - Iso) + 76.0f  * Iso;

            const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
            Pixels[Off + 0u] = static_cast<uint8_t>(std::clamp(R, 0.0f, 255.0f));
            Pixels[Off + 1u] = static_cast<uint8_t>(std::clamp(G, 0.0f, 255.0f));
            Pixels[Off + 2u] = static_cast<uint8_t>(std::clamp(B, 0.0f, 255.0f));
            Pixels[Off + 3u] = 255u;
        }
    }
}

struct GeometrySummaryDesc
{
    const char*                                ShapeName        = "Cube";
    const char*                                DomainLabel      = nullptr;
    float                                      WidthMeters      = 1.0f;
    float                                      HeightMeters     = 1.0f;
    float                                      DepthMeters      = 1.0f;
    uint32_t                                   TriangleCount    = 3072u;
    uint32_t                                   ClusterCount     = 48u;
    uint32_t                                   LodLevels        = 3u;
    uint32_t                                   SubPartCount     = 1u;
    bool                                       HasLocalSdf      = false;
    GeometrySurfaceBinding*                    SurfaceBinding   = nullptr;
    GeometricRaster::WorldSceneSpecification*  Scene            = nullptr;
    std::function<float(PixelSpace&, float, float, float, uint32_t&, const PaneInputReading&, bool&)> ConstructCategoryCard {};
};

// Lightweight live sync: updates matching RigidClusterInstance transform & GPU bindless material slot without rebuilding CPU BVH
inline uint32_t ResolveSceneClusterInstance(
    const OutlinerRecordEntry&                      Entry,
    const GeometricRaster::WorldSceneSpecification* Scene) noexcept
{
    if (Scene == nullptr || Scene->ClusterInstances.empty()) return 0xFFFFFFFFu;
    uint32_t BestIdx  = 0xFFFFFFFFu;
    float    BestDist = 0.75f;
    for (uint32_t I = 0u; I < static_cast<uint32_t>(Scene->ClusterInstances.size()); ++I)
    {
        const auto& Inst = Scene->ClusterInstances[I];
        const float DX = Inst.WorldTranslation.X - Entry.PositionX;
        const float DY = Inst.WorldTranslation.Y - Entry.PositionY;
        const float DZ = Inst.WorldTranslation.Z - Entry.PositionZ;
        const float D  = std::sqrt(DX * DX + DY * DY + DZ * DZ);
        if (D < BestDist)
        {
            BestDist = D;
            BestIdx  = I;
        }
    }
    return BestIdx;
}

inline void SyncGeometryLiveToScene(
    const OutlinerRecordEntry&                Entry,
    GeometrySurfaceBinding*                   Binding,
    GeometricRaster::WorldSceneSpecification* Scene,
    uint32_t                                  MatchedIdx,
    bool                                      SyncMaterial,
    bool                                      SyncTransform,
    bool                                      GlassMode = false) noexcept
{
    if (Scene == nullptr) return;
    if (MatchedIdx < Scene->ClusterInstances.size())
    {
        auto& Inst = Scene->ClusterInstances[MatchedIdx];
        if (SyncTransform)
        {
            Inst.PreviousTranslation = Inst.WorldTranslation;
            Inst.WorldTranslation    = { Entry.PositionX, Entry.PositionY, Entry.PositionZ };
            Inst.RotationZDegrees    = Entry.RotationZ;
            Inst.NonUniformScale     = {
                std::max(0.05f, Entry.ScaleX),
                std::max(0.05f, Entry.ScaleY),
                std::max(0.05f, Entry.ScaleZ)
            };
        }
        if (SyncMaterial && Binding != nullptr)
        {
            const uint16_t Slot = (Binding->MaterialSlotOrdinal > 0u && Binding->MaterialSlotOrdinal < Scene->MaterialRegister.QuerySlotCount())
                ? Binding->MaterialSlotOrdinal
                : Inst.MaterialSlot;
            if (Slot < Scene->MaterialRegister.QuerySlotCount())
            {
                MaterialSpace::SurfaceMaterial Mat = Scene->MaterialRegister.InspectAuthoredMaterial(Slot);
                Mat.Diffuse.BaseColor        = Binding->AlbedoTint;
                Mat.Specular.Roughness       = std::clamp(Binding->Roughness, 0.02f, 1.0f);
                Mat.Diffuse.DiffuseRoughness = std::clamp(Binding->Roughness, 0.02f, 1.0f);
                Mat.Specular.Metalness                  = std::clamp(Binding->Metallic, 0.0f, 1.0f);
                Mat.PorosityLight.EmissionLuminanceNits = std::max(0.0f, Binding->SelfGlow);
                Mat.PorosityLight.EmissionTint          = Binding->AlbedoTint;
                (void)Scene->MaterialRegister.UpdateMaterialSlot(Slot, Mat);
                Inst.TransparentGlass = GlassMode;
            }
        }
    }
    else if (SyncMaterial && Binding != nullptr &&
             Binding->MaterialSlotOrdinal < Scene->MaterialRegister.QuerySlotCount())
    {
        MaterialSpace::SurfaceMaterial Mat = Scene->MaterialRegister.InspectAuthoredMaterial(Binding->MaterialSlotOrdinal);
        Mat.Diffuse.BaseColor                   = Binding->AlbedoTint;
        Mat.Specular.Roughness                  = std::clamp(Binding->Roughness, 0.02f, 1.0f);
        Mat.Diffuse.DiffuseRoughness            = std::clamp(Binding->Roughness, 0.02f, 1.0f);
        Mat.Specular.Metalness                  = std::clamp(Binding->Metallic, 0.0f, 1.0f);
        Mat.PorosityLight.EmissionLuminanceNits = std::max(0.0f, Binding->SelfGlow);
        Mat.PorosityLight.EmissionTint          = Binding->AlbedoTint;
        (void)Scene->MaterialRegister.UpdateMaterialSlot(Binding->MaterialSlotOrdinal, Mat);
    }
}

// Shared 1:1 8-tab Geometry Inspector renderer
inline float ConstructGeometryPages(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, const GeometrySummaryDesc& Desc,
    uint32_t& ActivePage, uint32_t& ActiveDragId,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched) noexcept
{
    const float WideX = LeftX;
    const float WideW = InnerWidth;
    static constexpr std::array<const char*, 8u> Pages = {{
        "Overview", "Components", "Geometry", "Material", "UV", "Fracture", "SDF", "LOD"
    }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, WideX, TopY, WideW, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX  = std::clamp(WideW * 0.065f, 14.0f, 22.0f);
    const float BodyX = WideX + PadX;
    const float BodyW = std::max(100.0f, WideW - PadX * 2.0f);
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    const uint32_t MatchedIdx = ResolveSceneClusterInstance(Entry, Desc.Scene);
    uint32_t LiveTris     = Desc.TriangleCount;
    uint32_t LiveClusters = Desc.ClusterCount;
    uint32_t LiveLods     = std::max(1u, Desc.LodLevels);
    bool     LiveHasSdf   = Desc.HasLocalSdf;
    if (Desc.Scene != nullptr && MatchedIdx < Desc.Scene->ClusterInstances.size())
    {
        const auto& Inst = Desc.Scene->ClusterInstances[MatchedIdx];
        if (Inst.PyramidAsset != nullptr && !Inst.PyramidAsset->Clusters.empty())
        {
            LiveClusters = static_cast<uint32_t>(Inst.PyramidAsset->Clusters.size());
            uint32_t L0Tris = 0u;
            uint32_t MaxLod = 0u;
            for (const auto& Cl : Inst.PyramidAsset->Clusters)
            {
                MaxLod = std::max(MaxLod, static_cast<uint32_t>(Cl.HierarchyLevel));
                if (Cl.HierarchyLevel == 0u) L0Tris += Cl.TriangleCount;
            }
            if (L0Tris > 0u) LiveTris = L0Tris;
            LiveLods = std::max(1u, MaxLod + 1u);
        }
        LiveHasSdf = (Inst.LocalSdfVolume != nullptr);
    }

    const float Wx = std::max(0.01f, Desc.WidthMeters * Entry.ScaleX);
    const float Hy = std::max(0.01f, Desc.HeightMeters * Entry.ScaleY);
    const float Dz = std::max(0.01f, Desc.DepthMeters * Entry.ScaleZ);
    const float VolM3 = Wx * Hy * Dz;
    const float SurfM2 = 2.0f * (Wx * Hy + Hy * Dz + Dz * Wx);

    if (ActivePage == 0u)
    {
        // TAB 1: OVERVIEW — Checks passed / Warnings + Footprint + Components (matches cube__0)
        const float Gap = 10.0f;
        const float HalfW = std::floor((WideW - Gap) * 0.5f);
        {
            const Rectangle2D K0 { WideX, CursorY, HalfW, 102.0f };
            ConstructGeomCard(Surface, K0);
            Surface.FillCircle({ K0.X + 26.0f, K0.Y + 26.0f }, 10.0f, ColorRgba{ 62u, 206u, 118u, 255u });
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { K0.X + 44.0f, K0.Y + 20.0f }, 11.4f, InspectorMetrics::MutedInk, "Checks passed");
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { K0.X + 20.0f, K0.Y + 52.0f }, 36.0f, InspectorMetrics::HeadingInk, "1");

            const Rectangle2D K1 { WideX + HalfW + Gap, CursorY, HalfW, 102.0f };
            ConstructGeomCard(Surface, K1);
            Surface.FillRectangle({ K1.X + 16.0f, K1.Y + 16.0f, 20.0f, 20.0f }, ColorRgba{ 238u, 98u, 94u, 255u }, 6.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { K1.X + 24.0f, K1.Y + 20.0f }, 11.0f, ColorRgba{ 20u, 20u, 20u, 255u }, "!");
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { K1.X + 44.0f, K1.Y + 20.0f }, 11.4f, InspectorMetrics::MutedInk, "Warnings");
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { K1.X + 20.0f, K1.Y + 52.0f }, 36.0f, InspectorMetrics::HeadingInk, "0");
            CursorY += 102.0f + 12.0f;
        }

        // Card 2: Footprint (h = 242)
        {
            const Rectangle2D Card { WideX, CursorY, WideW, 242.0f };
            ConstructGeomCard(Surface, Card);
            Surface.FillCircle({ BodyX + 12.0f, Card.Y + 32.0f }, 13.0f, ColorRgba{ 30u, 52u, 42u, 255u });
            Surface.StrokeCircle({ BodyX + 12.0f, Card.Y + 32.0f }, 13.0f, ColorRgba{ 76u, 168u, 118u, 160u }, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + 34.0f, Card.Y + 23.0f }, 16.4f, InspectorMetrics::HeadingInk, "Footprint");
            const float WSW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.6f, "World size");
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - WSW, Card.Y + 27.0f }, 10.6f, InspectorMetrics::DimInk, "World size");

            // 1.000 m³
            char WholeB[16]{}, FracB[16]{};
            const int WholeI = static_cast<int>(std::floor(VolM3 + 1e-6f));
            const int FracI  = static_cast<int>(std::lround((VolM3 - static_cast<float>(WholeI)) * 1000.0f)) % 1000;
            std::snprintf(WholeB, sizeof(WholeB), "%d", WholeI);
            std::snprintf(FracB,  sizeof(FracB),  ".%03d", std::max(0, FracI));
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 62.0f }, 42.0f, ColorRgba{ 244u, 244u, 244u, 255u }, WholeB);
            const float W1 = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 42.0f, WholeB);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + W1, Card.Y + 62.0f }, 42.0f, ColorRgba{ 120u, 120u, 124u, 255u }, FracB);
            const float W2 = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 42.0f, FracB);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + W1 + W2 + 6.0f, Card.Y + 84.0f }, 14.0f, InspectorMetrics::MutedInk, "m\xC2\xB3");
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 114.0f }, 10.8f, InspectorMetrics::DimInk, "Volume");

            // 3 dimension tiles: WIDTH / HEIGHT / DEPTH
            const float TGap = 8.0f;
            const float TW = std::floor((BodyW - TGap * 2.0f) / 3.0f);
            const std::array<std::pair<const char*, float>, 3u> Dims = {{ { "WIDTH", Wx }, { "HEIGHT", Hy }, { "DEPTH", Dz } }};
            for (uint32_t I = 0u; I < 3u; ++I)
            {
                const Rectangle2D T { BodyX + static_cast<float>(I) * (TW + TGap), Card.Y + 136.0f, TW, 58.0f };
                Surface.FillRectangle(T, ColorRgba{ 32u, 32u, 33u, 255u }, 12.0f);
                Surface.StrokeRectangle(T, ColorRgba{ 255u, 255u, 255u, 14u }, 12.0f, 1.0f);
                WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { T.X + 10.0f, T.Y + 10.0f }, 8.6f, InspectorMetrics::DimInk, Dims[I].first);
                char DBuf[20]{};
                std::snprintf(DBuf, sizeof(DBuf), "%.2f m", Dims[I].second);
                WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { T.X + 10.0f, T.Y + 30.0f }, 14.0f, InspectorMetrics::HeadingInk, DBuf);
            }
            char SurfBuf[72]{};
            std::snprintf(SurfBuf, sizeof(SurfBuf), "Surface \xE2\x89\x88 %.2f m\xC2\xB2 \xC2\xB7 bounds include scale.", SurfM2);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 210.0f }, 10.0f, InspectorMetrics::DimInk, SurfBuf);
            CursorY = Card.Bottom() + 12.0f;
        }

        // Card 3: Components (h = 240)
        {
            const Rectangle2D Card { WideX, CursorY, WideW, 240.0f };
            ConstructGeomCard(Surface, Card);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 24.0f }, 16.4f, InspectorMetrics::HeadingInk, "Components");
            const float SW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.4f, "3 on this object");
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - SW, Card.Y + 27.0f }, 10.4f, InspectorMetrics::DimInk, "3 on this object");

            const std::array<std::tuple<const char*, const char*, ColorRgba>, 3u> Rows = {{
                { "Mass",       "950 kg \xC2\xB7 Plastic",     ColorRgba{ 168u, 174u, 184u, 255u } },
                { "Rigid Body", "Static \xC2\xB7 not simulated",ColorRgba{ 102u, 174u, 232u, 255u } },
                { "Collision",  "Box \xC2\xB7 collides",        ColorRgba{ 76u, 198u, 138u, 255u } },
            }};
            for (uint32_t I = 0u; I < 3u; ++I)
            {
                const Rectangle2D R { BodyX - 6.0f, Card.Y + 58.0f + static_cast<float>(I) * 58.0f, BodyW + 12.0f, 50.0f };
                Surface.FillRectangle(R, RowFill, 14.0f);
                Surface.StrokeRectangle(R, ColorRgba{ 255u, 255u, 255u, 14u }, 14.0f, 1.0f);
                Surface.FillCircle({ R.X + 24.0f, R.Center().Y }, 9.0f, std::get<2>(Rows[I]).ScaledAlpha(0.25f));
                Surface.StrokeCircle({ R.X + 24.0f, R.Center().Y }, 7.0f, std::get<2>(Rows[I]), 1.3f);
                WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { R.X + 46.0f, R.Y + 11.0f }, 12.4f, InspectorMetrics::HeadingInk, std::get<0>(Rows[I]));
                WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { R.X + 46.0f, R.Y + 28.0f }, 10.2f, InspectorMetrics::DimInk, std::get<1>(Rows[I]));
                const Point2D Ch { R.Right() - 16.0f, R.Center().Y };
                Surface.StrokeLine({ Ch.X - 3.0f, Ch.Y - 4.0f }, { Ch.X + 1.5f, Ch.Y }, InspectorMetrics::DimInk, 1.4f);
                Surface.StrokeLine({ Ch.X + 1.5f, Ch.Y }, { Ch.X - 3.0f, Ch.Y + 4.0f }, InspectorMetrics::DimInk, 1.4f);
            }
            CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
        }
    }
    else if (ActivePage == 1u)
    {
        // TAB 2: COMPONENTS — Component stack (h = 252) + footnote + Collision fit (h = 168)
        const Rectangle2D Card { WideX, CursorY, WideW, 252.0f };
        ConstructGeomCard(Surface, Card);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 24.0f }, 16.4f, InspectorMetrics::HeadingInk, "Component stack");
        const float SW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.4f, "3 of 3 on");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - SW, Card.Y + 27.0f }, 10.4f, InspectorMetrics::DimInk, "3 of 3 on");

        const std::array<std::tuple<const char*, const char*, ColorRgba>, 3u> Rows = {{
            { "Mass",       "950 kg \xC2\xB7 Pla...", ColorRgba{ 168u, 174u, 184u, 255u } },
            { "Rigid Body", "Static \xC2\xB7 not ...",ColorRgba{ 102u, 174u, 232u, 255u } },
            { "Collision",  "Box \xC2\xB7 collides",  ColorRgba{ 76u, 198u, 138u, 255u } },
        }};
        for (uint32_t I = 0u; I < 3u; ++I)
        {
            const Rectangle2D R { BodyX - 6.0f, Card.Y + 58.0f + static_cast<float>(I) * 60.0f, BodyW + 12.0f, 52.0f };
            Surface.FillRectangle(R, RowFill, 14.0f);
            Surface.StrokeRectangle(R, ColorRgba{ 255u, 255u, 255u, 14u }, 14.0f, 1.0f);
            // 6-dot grip handle
            for (int Gy = 0; Gy < 3; ++Gy)
                for (int Gx = 0; Gx < 2; ++Gx)
                    Surface.FillCircle({ R.X + 12.0f + Gx * 4.0f, R.Y + 19.0f + Gy * 6.0f }, 1.1f, InspectorMetrics::DimInk);
            Surface.StrokeCircle({ R.X + 38.0f, R.Center().Y }, 7.0f, std::get<2>(Rows[I]), 1.3f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { R.X + 58.0f, R.Y + 11.0f }, 12.2f, InspectorMetrics::HeadingInk, std::get<0>(Rows[I]));
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { R.X + 58.0f, R.Y + 29.0f }, 10.0f, InspectorMetrics::DimInk, std::get<1>(Rows[I]));
            // Green active toggle switch
            const Rectangle2D Tog { R.Right() - 108.0f, R.Center().Y - 11.0f, 36.0f, 22.0f };
            Surface.FillRectangle(Tog, ColorRgba{ 58u, 204u, 118u, 255u }, 11.0f);
            Surface.FillCircle({ Tog.Right() - 11.0f, Tog.Center().Y }, 8.5f, ColorRgba{ 246u, 246u, 246u, 255u });
            // Up/Down/Close glyph hints
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { R.Right() - 56.0f, R.Center().Y - 5.0f }, 10.0f, InspectorMetrics::DimInk, "^   v   x");
        }
        CursorY = Card.Bottom() + 10.0f;
        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, WideX + 4.0f, CursorY, WideW - 8.0f, 10.0f, 14.2f, InspectorMetrics::DimInk,
            "Values stay on the object, so a removed component comes back with its settings. Order is kept with the scene.");
        CursorY += 44.0f;

        const Rectangle2D FitCard { WideX, CursorY, WideW, 168.0f };
        ConstructGeomCard(Surface, FitCard);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, FitCard.Y + 24.0f }, 14.0f, InspectorMetrics::HeadingInk, "Collision fit");
        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, FitCard.Y + 64.0f, BodyW, 11.0f, 16.0f, InspectorMetrics::MutedInk,
            "Fit box, sphere and capsule proxies, compare how tightly each wraps the object and check the surface is closed.");
        const Rectangle2D FitBtn { BodyX, FitCard.Bottom() - 48.0f, BodyW, 32.0f };
        Surface.FillRectangle(FitBtn, ColorRgba{ 36u, 36u, 38u, 255u }, 16.0f);
        Surface.StrokeRectangle(FitBtn, ColorRgba{ 255u, 255u, 255u, 28u }, 16.0f, 1.0f);
        const float FBW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.4f, "Open Collision Fit");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { FitBtn.Center().X - FBW * 0.5f, FitBtn.Y + 9.5f }, 11.4f, InspectorMetrics::HeadingInk, "Open Collision Fit");
        CursorY = FitCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        // TAB 3: GEOMETRY — Transform (LOCAL SPACE, live sliders + reset buttons) + Shape + Category Controls
        const Rectangle2D TCard { WideX, CursorY, WideW, 536.0f };
        ConstructGeomCard(Surface, TCard);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, TCard.Y + 24.0f }, 16.4f, InspectorMetrics::HeadingInk, "Transform");
        const float LW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, "LIVE SCENE SYNC");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - LW, TCard.Y + 28.0f }, 9.6f, InspectorMetrics::DimInk, "LIVE SCENE SYNC");

        const float HeadY = TCard.Y + 62.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, HeadY }, 10.4f, InspectorMetrics::DimInk, "Component");
        const float Col0X = BodyX + 76.0f, ColGap = 6.0f;
        const float ColW = std::floor((BodyW - 76.0f - ColGap * 2.0f) / 3.0f);
        const std::array<std::pair<const char*, ColorRgba>, 3u> Axes = {{
            { "X", ColorRgba{ 216u, 118u, 114u, 255u } }, { "Y", ColorRgba{ 126u, 186u, 132u, 255u } }, { "Z", ColorRgba{ 124u, 152u, 212u, 255u } }
        }};
        for (uint32_t I = 0u; I < 3u; ++I)
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Col0X + static_cast<float>(I) * (ColW + ColGap) + ColW * 0.5f - 3.0f, HeadY }, 10.4f, Axes[I].second, Axes[I].first);

        const std::array<std::tuple<const char*, const char*, float, float, float>, 3u> Rows = {{
            { "Position", "m",        Entry.PositionX, Entry.PositionY, Entry.PositionZ },
            { "Rotation", "deg",      Entry.RotationX, Entry.RotationY, Entry.RotationZ },
            { "Scale",    "\xC3\x97", Entry.ScaleX,    Entry.ScaleY,    Entry.ScaleZ    },
        }};
        for (uint32_t R = 0u; R < 3u; ++R)
        {
            const float RY = HeadY + 24.0f + static_cast<float>(R) * 40.0f;
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, RY + 2.0f }, 11.4f, InspectorMetrics::HeadingInk, std::get<0>(Rows[R]));
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, RY + 16.0f }, 9.2f, InspectorMetrics::DimInk, std::get<1>(Rows[R]));
            const std::array<float, 3u> Vals = {{ std::get<2>(Rows[R]), std::get<3>(Rows[R]), std::get<4>(Rows[R]) }};
            for (uint32_t C = 0u; C < 3u; ++C)
            {
                const Rectangle2D Box { Col0X + static_cast<float>(C) * (ColW + ColGap), RY, ColW, 30.0f };
                Surface.FillRectangle(Box, ColorRgba{ 14u, 14u, 15u, 255u }, 8.0f);
                char Num[24]{};
                std::snprintf(Num, sizeof(Num), "%.4g", Vals[C]);
                const float NW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 12.2f, Num);
                WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { Box.X + std::max(4.0f, (Box.Width - NW) * 0.5f), Box.Y + 8.0f }, Box.Width - 8.0f, 12.2f, InspectorMetrics::HeadingInk, Num);
            }
        }
        bool TransformMutated = false;
        float TY = HeadY + 150.0f;
        float UniformScale = std::max(0.05f, (Entry.ScaleX + Entry.ScaleY + Entry.ScaleZ) / 3.0f);
        const float PrevScale = UniformScale;
        TY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, TY, BodyW, 8810u, "Uniform Scale", "x", "%.2f", 0.10f, 8.0f, UniformScale, ActiveDragId, Input, TransformMutated);
        if (TransformMutated && std::abs(UniformScale - PrevScale) > 1e-4f)
        {
            Entry.ScaleX = UniformScale;
            Entry.ScaleY = UniformScale;
            Entry.ScaleZ = UniformScale;
        }
        TY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, TY, BodyW, 8811u, "Yaw Rotation", "\xC2\xB0", "%.1f", -180.0f, 180.0f, Entry.RotationZ, ActiveDragId, Input, TransformMutated);
        TY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, TY, BodyW, 8812u, "Elevation Z", "m", "%.2f", -4.0f, 12.0f, Entry.PositionZ, ActiveDragId, Input, TransformMutated);
        TY = InspectorPrimitives::ConstructToggleFieldRow(Surface, BodyX, TY, BodyW, "Visible in viewport", Entry.VisibilityEnabled, Input, TransformMutated);

        const std::array<const char*, 3u> Rsts = {{ "Reset Pos", "Reset Rot", "Reset Scale" }};
        const float RW = std::floor((BodyW - 12.0f) / 3.0f);
        for (uint32_t I = 0u; I < 3u; ++I)
        {
            const Rectangle2D B { BodyX + static_cast<float>(I) * (RW + 6.0f), TY + 6.0f, RW, 28.0f };
            const bool Hov = B.Contains(Cursor);
            Surface.FillRectangle(B, Hov ? ColorRgba{ 46u, 48u, 52u, 255u } : ColorRgba{ 32u, 32u, 34u, 255u }, 14.0f);
            Surface.StrokeRectangle(B, ColorRgba{ 255u, 255u, 255u, static_cast<uint8_t>(Hov ? 48u : 20u) }, 14.0f, 1.0f);
            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.8f, Rsts[I]);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { B.Center().X - TW * 0.5f, B.Y + 8.5f }, 9.8f, InspectorMetrics::HeadingInk, Rsts[I]);
            if (Hov && Input.PrimaryPointer.Pressed)
            {
                if (I == 0u)      { Entry.PositionX = 0.0f; Entry.PositionY = 0.0f; Entry.PositionZ = 0.0f; }
                else if (I == 1u) { Entry.RotationX = 0.0f; Entry.RotationY = 0.0f; Entry.RotationZ = 0.0f; }
                else              { Entry.ScaleX = 1.0f; Entry.ScaleY = 1.0f; Entry.ScaleZ = 1.0f; }
                TransformMutated = true;
            }
        }
        if (TransformMutated)
        {
            OutMutated = true;
            SyncGeometryLiveToScene(Entry, Desc.SurfaceBinding, Desc.Scene, MatchedIdx, false, true);
        }
        CursorY = TCard.Bottom() + 14.0f;

        if (Desc.ConstructCategoryCard)
        {
            bool CatMut = false;
            CursorY = Desc.ConstructCategoryCard(Surface, WideX, CursorY, WideW, ActiveDragId, Input, CatMut);
            if (CatMut)
            {
                OutMutated = true;
                SyncGeometryLiveToScene(Entry, Desc.SurfaceBinding, Desc.Scene, MatchedIdx, false, true);
            }
        }

        // Shape summary card
        const Rectangle2D SCard { WideX, CursorY, WideW, 240.0f };
        ConstructGeomCard(Surface, SCard);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, SCard.Y + 24.0f }, 14.0f, InspectorMetrics::HeadingInk, Desc.DomainLabel ? Desc.DomainLabel : "Shape");
        char SzBuf[16]{};
        std::snprintf(SzBuf, sizeof(SzBuf), "%.2f", Wx);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, SCard.Y + 62.0f, SzBuf, "m \xC2\xB7 Size X", nullptr, 34.0f);
        char SubB[96]{};
        std::snprintf(SubB, sizeof(SubB), "%s \xC2\xB7 %.2f \xC3\x97 %.2f \xC3\x97 %.2f m · %u tris", Desc.ShapeName, Wx, Hy, Dz, LiveTris);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, SCard.Y + 114.0f }, 10.6f, InspectorMetrics::MutedInk, SubB);
        DrawIsometricCube(Surface, { SCard.Center().X - 10.0f, SCard.Y + 196.0f }, 44.0f,
            ColorRgba{ 36u, 36u, 38u, 255u }, ColorRgba{ 30u, 30u, 32u, 255u }, ColorRgba{ 26u, 26u, 28u, 255u }, ColorRgba{ 138u, 140u, 144u, 210u });
        CursorY = SCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        // TAB 4: MATERIAL — Material channels (h = 646)
        const Rectangle2D Card { WideX, CursorY, WideW, 646.0f };
        ConstructGeomCard(Surface, Card);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 24.0f }, 9.2f, InspectorMetrics::DimInk, "MATERIAL / SLOT 0");
        Surface.StrokeCircle({ BodyX + BodyW - 14.0f, Card.Y + 34.0f }, 14.0f, ColorRgba{ 255u, 255u, 255u, 28u }, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 44.0f }, 21.0f, InspectorMetrics::HeadingInk, "Material channels");
        std::string BoundStr = "Bound to " + Entry.Title + " \xC2\xB7 #cube";
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 78.0f }, 10.8f, InspectorMetrics::MutedInk, BoundStr);

        // Nested Material slots container
        const Rectangle2D Slots { BodyX, Card.Y + 102.0f, BodyW, 214.0f };
        Surface.FillRectangle(Slots, ColorRgba{ 22u, 22u, 23u, 255u }, 16.0f);
        Surface.StrokeRectangle(Slots, ColorRgba{ 255u, 255u, 255u, 16u }, 16.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Slots.X + 14.0f, Slots.Y + 14.0f }, 12.2f, InspectorMetrics::HeadingInk, "Material slots");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Slots.Right() - 32.0f, Slots.Y + 15.0f }, 10.0f, InspectorMetrics::DimInk, "1 / 8");

        const Rectangle2D S0 { Slots.X + 12.0f, Slots.Y + 42.0f, Slots.Width - 24.0f, 38.0f };
        Surface.FillRectangle(S0, ColorRgba{ 30u, 28u, 27u, 255u }, 12.0f);
        Surface.StrokeRectangle(S0, ColorRgba{ 165u, 154u, 142u, 140u }, 12.0f, 1.0f);
        Surface.FillCircle({ S0.X + 19.0f, S0.Center().Y }, 10.0f, ColorRgba{ 188u, 166u, 152u, 255u });
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { S0.X + 36.0f, S0.Y + 6.0f }, 8.2f, InspectorMetrics::DimInk, "SLOT 0");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { S0.X + 36.0f, S0.Y + 18.0f }, 11.4f, InspectorMetrics::HeadingInk, "Surface material");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { S0.Right() - 18.0f, S0.Y + 13.0f }, 9.6f, InspectorMetrics::DimInk, "all");

        const Rectangle2D AddS { Slots.X + 12.0f, S0.Bottom() + 8.0f, Slots.Width - 24.0f, 34.0f };
        Surface.StrokeRectangle(AddS, ColorRgba{ 255u, 255u, 255u, 28u }, 12.0f, 1.0f);
        const float ASW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, "+ Add slot");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { AddS.Center().X - ASW * 0.5f, AddS.Y + 10.5f }, 11.0f, InspectorMetrics::MutedInk, "+ Add slot");

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Slots.X + 12.0f, AddS.Bottom() + 12.0f }, 9.4f, InspectorMetrics::DimInk, "Slot 0 draws every part not given to another slot");
        const std::array<const char*, 6u> Faces = {{ "Right", "Left", "Top", "Bottom", "Front", "Back" }};
        float FX = Slots.X + 12.0f, FY = AddS.Bottom() + 28.0f;
        for (uint32_t I = 0u; I < 6u; ++I)
        {
            const float FW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, Faces[I]) + 18.0f;
            if (FX + FW > Slots.Right() - 10.0f) { FX = Slots.X + 12.0f; FY += 28.0f; }
            const Rectangle2D FP { FX, FY, FW, 24.0f };
            Surface.FillRectangle(FP, ColorRgba{ 30u, 30u, 32u, 255u }, 10.0f);
            Surface.StrokeRectangle(FP, ColorRgba{ 255u, 255u, 255u, 20u }, 10.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { FP.X + 9.0f, FP.Y + 6.0f }, 9.6f, InspectorMetrics::MutedInk, Faces[I]);
            FX += FW + 5.0f;
        }

        // Live OpenPBR material sliders directly syncing to GPU bindless CompactMaterialRegister
        float MY = Slots.Bottom() + 18.0f;
        bool MatMut = false;
        if (Desc.SurfaceBinding != nullptr)
        {
            MY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, MY, BodyW, 8820u, "Roughness",  "",  "%.2f", 0.02f, 1.0f, Desc.SurfaceBinding->Roughness, ActiveDragId, Input, MatMut);
            MY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, MY, BodyW, 8821u, "Metallic",   "",  "%.2f", 0.00f, 1.0f, Desc.SurfaceBinding->Metallic,  ActiveDragId, Input, MatMut);
            MY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, MY, BodyW, 8822u, "Self-glow",  "x", "%.2f", 0.00f, 8.0f, Desc.SurfaceBinding->SelfGlow,  ActiveDragId, Input, MatMut);
            if (MatMut)
            {
                OutMutated = true;
                SyncGeometryLiveToScene(Entry, Desc.SurfaceBinding, Desc.Scene, MatchedIdx, true, false);
            }
        }

        const float KpiY = MY + 8.0f;
        const float KpiW = std::floor((BodyW - 10.0f) * 0.5f);
        const auto DrawMK = [&](float X, ColorRgba Dot, const char* Lbl, const char* Val)
        {
            const Rectangle2D K { X, KpiY, KpiW, 74.0f };
            Surface.FillRectangle(K, ColorRgba{ 22u, 22u, 23u, 255u }, 16.0f);
            Surface.FillCircle({ K.X + 16.0f, K.Y + 18.0f }, 4.0f, Dot);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { K.X + 26.0f, K.Y + 13.0f }, 9.6f, InspectorMetrics::MutedInk, Lbl);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { K.X + 14.0f, K.Y + 38.0f }, 26.0f, InspectorMetrics::HeadingInk, Val);
        };
        char PartBuf[16]{};
        std::snprintf(PartBuf, sizeof(PartBuf), "%u", std::max(1u, Desc.SubPartCount));
        DrawMK(BodyX,                ColorRgba{ 88u, 198u, 122u, 255u }, "Sub-part slots", PartBuf);
        DrawMK(BodyX + KpiW + 10.0f, ColorRgba{ 204u, 162u, 114u, 255u }, "OpenPBR GPU",    "Live");
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 4u)
    {
        // TAB 5: UV — UV preview (h = 546)
        static InspectorPreviewSurface SharedUvSurface {};
        InspectorGpuBakeSpec UvSpec { InspectorGpuBakeMode::GeometryUvAtlas, 0u };
        SharedUvSurface.EnsureUpdatedGpu(0x70501001ull, 524u, 360u, UvSpec, [](std::vector<uint8_t>& Pix) { RenderUvAtlasPixels(Pix, 524u, 360u); });

        const Rectangle2D Card { WideX, CursorY, WideW, 546.0f };
        ConstructGeomCard(Surface, Card);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 24.0f }, 13.2f, InspectorMetrics::HeadingInk, "UV preview");
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, Card.Y + 62.0f, "64.1", "% of 1001 used", nullptr, 34.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 114.0f }, 10.6f, InspectorMetrics::MutedInk, "6 islands \xC2\xB7 12 seam edges \xC2\xB7 1 UDIM tile in use");

        const Rectangle2D Well { BodyX, Card.Y + 138.0f, BodyW, 180.0f };
        SharedUvSurface.Present(Surface, Well, 12.0f);
        Surface.StrokeRectangle(Well, ColorRgba{ 255u, 255u, 255u, 16u }, 12.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.X + 8.0f, Well.Bottom() - 16.0f }, 9.2f, ColorRgba{ 232u, 232u, 236u, 230u }, "1001");

        // 4 UDIM tile buttons
        const float UY = Well.Bottom() + 14.0f;
        const float UW = std::floor((BodyW - 18.0f) / 4.0f);
        const std::array<std::pair<const char*, const char*>, 4u> Udims = {{
            { "1001", "6 islands" }, { "1002", "empty" }, { "1003", "empty" }, { "1004", "empty" }
        }};
        for (uint32_t I = 0u; I < 4u; ++I)
        {
            const Rectangle2D U { BodyX + static_cast<float>(I) * (UW + 6.0f), UY, UW, 46.0f };
            Surface.FillRectangle(U, (I == 0u) ? ColorRgba{ 34u, 34u, 36u, 255u } : ColorRgba{ 22u, 22u, 23u, 255u }, 10.0f);
            Surface.StrokeRectangle(U, (I == 0u) ? ColorRgba{ 168u, 168u, 174u, 130u } : ColorRgba{ 255u, 255u, 255u, 16u }, 10.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { U.X + 8.0f, U.Y + 8.0f }, 10.8f, (I == 0u) ? InspectorMetrics::HeadingInk : InspectorMetrics::DimInk, Udims[I].first);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { U.X + 8.0f, U.Y + 26.0f }, 8.6f, InspectorMetrics::DimInk, Udims[I].second);
        }

        const float RowY = UY + 60.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, RowY }, 11.2f, InspectorMetrics::MutedInk, "Texel scale");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - 64.0f, RowY }, 11.2f, InspectorMetrics::HeadingInk, "0.327 UV/m");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, RowY + 26.0f }, 11.2f, InspectorMetrics::MutedInk, "Unwrap angle");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - 22.0f, RowY + 26.0f }, 11.2f, InspectorMetrics::HeadingInk, "60\xC2\xB0");

        const Rectangle2D Btn1 { BodyX, RowY + 54.0f, BodyW, 32.0f };
        Surface.FillRectangle(Btn1, ColorRgba{ 224u, 224u, 224u, 255u }, 16.0f);
        const float B1W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 11.4f, "Open in UV editor");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Btn1.Center().X - B1W * 0.5f, Btn1.Y + 9.5f }, 11.4f, ColorRgba{ 18u, 18u, 18u, 255u }, "Open in UV editor");

        const Rectangle2D Btn2 { BodyX, Btn1.Bottom() + 8.0f, BodyW, 32.0f };
        Surface.FillRectangle(Btn2, ColorRgba{ 26u, 26u, 28u, 255u }, 16.0f);
        Surface.StrokeRectangle(Btn2, ColorRgba{ 255u, 255u, 255u, 24u }, 16.0f, 1.0f);
        const float B2W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.4f, "Edit in UV workspace");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Btn2.Center().X - B2W * 0.5f, Btn2.Y + 9.5f }, 11.4f, InspectorMetrics::HeadingInk, "Edit in UV workspace");
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 5u)
    {
        // TAB 6: FRACTURE — Fracture toggle card (h = 142) + Fracture preview card (h = 454)
        static bool  FractureOn = false;
        static float ExplodeVal = 0.3f;

        const Rectangle2D FCard { WideX, CursorY, WideW, 142.0f };
        ConstructGeomCard(Surface, FCard);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, FCard.Y + 24.0f }, 16.4f, InspectorMetrics::HeadingInk, "Fracture");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, FCard.Y + 48.0f }, 9.2f, InspectorMetrics::DimInk, "PER-OBJECT GEOMETRY");
        (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, BodyX, FCard.Y + 86.0f, BodyW, "Enable fracture", FractureOn, Input, OutMutated);
        CursorY = FCard.Bottom() + 14.0f;

        const Rectangle2D PCard { WideX, CursorY, WideW, 454.0f };
        ConstructGeomCard(Surface, PCard);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, PCard.Y + 24.0f }, 13.0f, InspectorMetrics::HeadingInk, "Fracture preview");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, PCard.Y + 66.0f }, 36.0f, ColorRgba{ 244u, 244u, 244u, 255u }, FractureOn ? "On" : "Off");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, PCard.Y + 116.0f }, 10.6f, InspectorMetrics::MutedInk, "Fracture is off for this object. The preview shows it whole.");

        const Rectangle2D Stage { BodyX, PCard.Y + 144.0f, BodyW, 204.0f };
        Surface.FillRectangle(Stage, ColorRgba{ 16u, 16u, 17u, 255u }, 14.0f);
        DrawIsometricCube(Surface, Stage.Center(), 66.0f,
            ColorRgba{ 148u, 144u, 138u, 255u }, ColorRgba{ 102u, 98u, 92u, 255u }, ColorRgba{ 126u, 122u, 116u, 255u }, ColorRgba{ 36u, 36u, 38u, 255u });

        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, Stage.Bottom() + 20.0f, BodyW, 8801u, "Explode", "", "%.2g", 0.0f, 1.0f, ExplodeVal, ActiveDragId, Input, OutMutated);
        CursorY = PCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 6u)
    {
        // TAB 7: SDF — SDF preview (h = 598)
        static InspectorPreviewSurface SharedSdfSurface {};
        InspectorGpuBakeSpec SdfSpec { InspectorGpuBakeMode::GeometrySdfSlice, 0u };
        SharedSdfSurface.EnsureUpdatedGpu(0x70507007ull, 420u, 360u, SdfSpec, [](std::vector<uint8_t>& Pix) { RenderSdfSlicePixels(Pix, 420u, 360u); });
        static float SliceDepth = 50.0f;

        const Rectangle2D Card { WideX, CursorY, WideW, 598.0f };
        ConstructGeomCard(Surface, Card);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 24.0f }, 13.0f, InspectorMetrics::HeadingInk, "SDF preview");
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, Card.Y + 62.0f, "448", "of 512 bricks", nullptr, 34.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 114.0f }, 10.6f, InspectorMetrics::MutedInk, "64\xC2\xB3 voxels \xC2\xB7 13% of bricks pruned");

        const Rectangle2D Well { BodyX, Card.Y + 142.0f, BodyW, 212.0f };
        Surface.FillRectangle(Well, ColorRgba{ 16u, 16u, 17u, 255u }, 14.0f);
        SharedSdfSurface.Present(Surface, { Well.X + 26.0f, Well.Y + 6.0f, Well.Width - 52.0f, Well.Height - 12.0f }, 4.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.X + 10.0f, Well.Bottom() - 16.0f }, 9.2f, InspectorMetrics::MutedInk, "centre slice \xC2\xB7 XY");

        float SY = Well.Bottom() + 18.0f;
        SY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, SY, BodyW, 8802u, "Slice depth", "", "%.0f", 0.0f, 100.0f, SliceDepth, ActiveDragId, Input, OutMutated);

        char SourceRowBuf[48]{};
        std::snprintf(SourceRowBuf, sizeof(SourceRowBuf), "%u triangles \xC2\xB7 %s", LiveTris, LiveHasSdf ? "GPU SDF active" : "analytic");
        const std::array<std::pair<const char*, const char*>, 4u> Rows = {{
            { "Resolution",  "64\xC2\xB3 \xC2\xB7 2.1 cm voxel" },
            { "Narrow band", "3.5 voxels \xC2\xB7 7.2 cm"      },
            { "Memory",      "576 KB \xC2\xB7 dense 1.0 MB"    },
            { "Source",      SourceRowBuf                      },
        }};
        for (uint32_t I = 0u; I < 4u; ++I)
        {
            const float RY = SY + 6.0f + static_cast<float>(I) * 26.0f;
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, RY }, 11.0f, InspectorMetrics::MutedInk, Rows[I].first);
            const float VW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, Rows[I].second);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - VW, RY }, 11.0f, InspectorMetrics::HeadingInk, Rows[I].second);
        }
        Surface.FillCircle({ BodyX + 4.0f, SY + 122.0f }, 4.0f, LiveHasSdf ? ColorRgba{ 76u, 206u, 128u, 255u } : InspectorMetrics::DimInk);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + 16.0f, SY + 116.0f }, 11.0f, InspectorMetrics::MutedInk, LiveHasSdf ? "Local SDF resident on GPU." : "Analytic / clipmap fallback.");
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // TAB 8: LOD — LOD preview (live ClusterPyramid stats)
        static float PreviewLod = 0.0f;
        const Rectangle2D Card { WideX, CursorY, WideW, 614.0f };
        ConstructGeomCard(Surface, Card);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 24.0f }, 13.0f, InspectorMetrics::HeadingInk, "LOD preview");
        char LevB[16]{};
        std::snprintf(LevB, sizeof(LevB), "%u", LiveLods);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, Card.Y + 62.0f, LevB, "levels", nullptr, 34.0f);
        char LodSub[80]{};
        std::snprintf(LodSub, sizeof(LodSub), "%u clusters \xC2\xB7 %u source triangles", LiveClusters, LiveTris);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 114.0f }, 10.6f, InspectorMetrics::MutedInk, LodSub);

        // 6-level coloured bar chart
        const Rectangle2D BarWell { BodyX, Card.Y + 142.0f, BodyW, 130.0f };
        Surface.FillRectangle(BarWell, ColorRgba{ 16u, 16u, 17u, 255u }, 14.0f);
        const std::array<std::tuple<const char*, const char*, float, ColorRgba>, 6u> Bars = {{
            { "L0", "3,072", 1.00f, ColorRgba{ 118u, 178u, 248u, 255u } },
            { "L1", "1,536", 0.88f, ColorRgba{ 62u,  106u, 82u,  255u } },
            { "L2", "768",   0.78f, ColorRgba{ 114u, 94u,  68u,  255u } },
            { "L3", "384",   0.68f, ColorRgba{ 110u, 76u,  72u,  255u } },
            { "L4", "192",   0.58f, ColorRgba{ 92u,  78u,  122u, 255u } },
            { "L5", "96",    0.48f, ColorRgba{ 68u,  96u,  104u, 255u } },
        }};
        const float BGap = 5.0f;
        const float BW = std::floor((BarWell.Width - 28.0f - BGap * 5.0f) / 6.0f);
        for (uint32_t I = 0u; I < 6u; ++I)
        {
            const float BH = 86.0f * std::get<2>(Bars[I]);
            const float BX = BarWell.X + 14.0f + static_cast<float>(I) * (BW + BGap);
            const float BY = BarWell.Bottom() - 22.0f - BH;
            const Rectangle2D BR { BX, BY, BW, BH };
            Surface.FillRectangle(BR, std::get<3>(Bars[I]), 5.0f);
            if (I == 0u) Surface.StrokeRectangle(BR, ColorRgba{ 238u, 244u, 255u, 220u }, 5.0f, 1.2f);
            const float NW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.2f, std::get<1>(Bars[I]));
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BR.Center().X - NW * 0.5f, BY - 12.0f }, 8.2f, InspectorMetrics::MutedInk, std::get<1>(Bars[I]));
            const float LW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.6f, std::get<0>(Bars[I]));
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BR.Center().X - LW * 0.5f, BarWell.Bottom() - 15.0f }, 8.6f, InspectorMetrics::DimInk, std::get<0>(Bars[I]));
        }

        // Cluster-coloured isometric cube stage
        const Rectangle2D CubeWell { BodyX, BarWell.Bottom() + 12.0f, BodyW, 196.0f };
        Surface.FillRectangle(CubeWell, ColorRgba{ 16u, 16u, 17u, 255u }, 14.0f);
        DrawIsometricCube(Surface, { CubeWell.Center().X, CubeWell.Center().Y - 6.0f }, 68.0f,
            ColorRgba{ 138u, 146u, 98u, 255u }, ColorRgba{ 72u, 134u, 108u, 255u }, ColorRgba{ 136u, 82u, 134u, 255u }, ColorRgba{ 30u, 32u, 38u, 255u });
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { CubeWell.X + 10.0f, CubeWell.Bottom() - 16.0f }, 9.2f, InspectorMetrics::MutedInk, "L0 \xC2\xB7 3,072 triangles \xC2\xB7 24 clusters");

        float LY = CubeWell.Bottom() + 16.0f;
        LY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, LY, BodyW, 8803u, "Preview level", "of 5", "%.0f", 0.0f, 5.0f, PreviewLod, ActiveDragId, Input, OutMutated);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, LY + 6.0f }, 11.0f, InspectorMetrics::MutedInk, "Cluster size");
        const float CSW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, "128 triangles \xC2\xB7 4 per group");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - CSW, LY + 6.0f }, 11.0f, InspectorMetrics::HeadingInk, "128 triangles \xC2\xB7 4 per group");
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier::GeometryUi
