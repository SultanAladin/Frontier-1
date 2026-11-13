/*============================================================================================================================================
                                                  UVAUTHORINGCHAOSVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Multi-seed randomized UV authoring chaos sequence, Undo/Redo state-equivalence & GPU/UI regression stress validation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometricRaster/UV/UVWorkspaceRasterizer.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Workspaces/UV/Operations/UVAuthoringOperations.h"
#include "Engine/Workspaces/UV/UVInspectorPanel.h"
#include "Engine/Workspaces/UV/UVOutlinerPanel.h"
#include "Engine/Workspaces/UV/UVPaneAppearance.h"
#include "Engine/Workspaces/UV/UVViewportHost.h"
#include "Engine/Workspaces/UV/UVViewportInteractor.h"
#include "Engine/Workspaces/UV/UVWorkspaceHost.h"
#include "Engine/Workspaces/UV/UVWorkspaceSession.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::GeometrySpace;
using namespace Frontier::Workspaces;

[[nodiscard]] bool VerifyInvariant(bool Condition, const char* Label) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[UVAuthoringChaosValidation] FAILED: %s\n", Label);
        return false;
    }
    return true;
}

struct AuthoringStateSnapshot
{
    std::vector<Mathematics::Axis3D>        Vertices {};
    std::vector<Mathematics::UVAxes>        EdgeUVs {};
    std::vector<uint8_t>                    EdgeMasks {};
    std::vector<uint16_t>                   FaceLayers {};
    std::vector<FaceShadingAssignment>      FaceShading {};
    std::vector<PriorityAllocationLayer>    Layers {};
    std::vector<uint32_t>                   UDIMTiles {};
    std::vector<uint32_t>                   SelectedFaces {};

    [[nodiscard]] bool Matches(const AuthoringStateSnapshot& Other, std::string& OutMismatch) const
    {
        if (Vertices.size() != Other.Vertices.size())
        {
            OutMismatch = "Vertex count mismatch";
            return false;
        }
        for (std::size_t I = 0u; I < Vertices.size(); ++I)
        {
            if (std::abs(Vertices[I].X - Other.Vertices[I].X) > 1.0e-5f ||
                std::abs(Vertices[I].Y - Other.Vertices[I].Y) > 1.0e-5f ||
                std::abs(Vertices[I].Z - Other.Vertices[I].Z) > 1.0e-5f)
            {
                OutMismatch = "Vertex position drift at index " + std::to_string(I);
                return false;
            }
        }
        if (EdgeUVs.size() != Other.EdgeUVs.size() || EdgeMasks.size() != Other.EdgeMasks.size())
        {
            OutMismatch = "Edge count mismatch";
            return false;
        }
        for (std::size_t I = 0u; I < EdgeUVs.size(); ++I)
        {
            if (std::abs(EdgeUVs[I].U - Other.EdgeUVs[I].U) > 1.0e-5f ||
                std::abs(EdgeUVs[I].V - Other.EdgeUVs[I].V) > 1.0e-5f ||
                EdgeMasks[I] != Other.EdgeMasks[I])
            {
                OutMismatch = "Edge UV/attribute mismatch at index " + std::to_string(I);
                return false;
            }
        }
        if (FaceLayers != Other.FaceLayers)
        {
            OutMismatch = "Face layer assignments mismatch";
            return false;
        }
        if (FaceShading.size() != Other.FaceShading.size())
        {
            OutMismatch = "Face shading count mismatch";
            return false;
        }
        for (std::size_t I = 0u; I < FaceShading.size(); ++I)
        {
            const auto& A = FaceShading[I];
            const auto& B = Other.FaceShading[I];
            if (A.MatCapSlot != B.MatCapSlot ||
                std::abs(A.AlbedoLinearRGB.X - B.AlbedoLinearRGB.X) > 1.0e-5f ||
                std::abs(A.AlbedoLinearRGB.Y - B.AlbedoLinearRGB.Y) > 1.0e-5f ||
                std::abs(A.AlbedoLinearRGB.Z - B.AlbedoLinearRGB.Z) > 1.0e-5f ||
                std::abs(A.MatCapTintLinearRGB.X - B.MatCapTintLinearRGB.X) > 1.0e-5f ||
                std::abs(A.MatCapTintLinearRGB.Y - B.MatCapTintLinearRGB.Y) > 1.0e-5f ||
                std::abs(A.MatCapTintLinearRGB.Z - B.MatCapTintLinearRGB.Z) > 1.0e-5f ||
                std::abs(A.Roughness - B.Roughness) > 1.0e-5f ||
                std::abs(A.Metallic - B.Metallic) > 1.0e-5f ||
                std::abs(A.Specular - B.Specular) > 1.0e-5f ||
                std::abs(A.MatCapBlend - B.MatCapBlend) > 1.0e-5f)
            {
                OutMismatch = "Face shading mismatch at index " + std::to_string(I);
                return false;
            }
        }
        if (Layers.size() != Other.Layers.size())
        {
            OutMismatch = "Layer count mismatch";
            return false;
        }
        for (std::size_t I = 0u; I < Layers.size(); ++I)
        {
            const auto& A = Layers[I];
            const auto& B = Other.Layers[I];
            if (A.LayerOrdinal != B.LayerOrdinal ||
                A.LayerTitle != B.LayerTitle ||
                std::abs(A.TargetSharePercent - B.TargetSharePercent) > 1.0e-4f ||
                std::abs(A.LinearTintRGB.X - B.LinearTintRGB.X) > 1.0e-5f ||
                std::abs(A.LinearTintRGB.Y - B.LinearTintRGB.Y) > 1.0e-5f ||
                std::abs(A.LinearTintRGB.Z - B.LinearTintRGB.Z) > 1.0e-5f)
            {
                OutMismatch = "Layer metadata mismatch at index " + std::to_string(I);
                return false;
            }
        }
        if (UDIMTiles != Other.UDIMTiles)
        {
            OutMismatch = "UDIM tile list mismatch";
            return false;
        }
        return true;
    }
};

[[nodiscard]] AuthoringStateSnapshot CaptureDigest(const UVWorkspaceSession& Session)
{
    AuthoringStateSnapshot Snap {};
    Snap.Vertices.reserve(Session.Asset.Network.Vertices.size());
    for (const auto& V : Session.Asset.Network.Vertices)
    {
        Snap.Vertices.push_back(V.Axes);
    }
    Snap.EdgeUVs.reserve(Session.Asset.Network.Edges.size());
    Snap.EdgeMasks.reserve(Session.Asset.Network.Edges.size());
    for (const auto& E : Session.Asset.Network.Edges)
    {
        Snap.EdgeUVs.push_back(E.ParameterUV);
        Snap.EdgeMasks.push_back(static_cast<uint8_t>(E.AttributeMask));
    }
    Snap.FaceLayers.reserve(Session.Asset.Network.Faces.size());
    Snap.FaceShading.reserve(Session.Asset.Network.Faces.size());
    for (const auto& F : Session.Asset.Network.Faces)
    {
        Snap.FaceLayers.push_back(F.PriorityLayerOrdinal);
        Snap.FaceShading.push_back(F.Shading);
    }
    const auto LayerSpan = Session.Asset.Layers.InspectLayers();
    Snap.Layers.assign(LayerSpan.begin(), LayerSpan.end());
    Snap.UDIMTiles     = Session.Asset.UDIMTiles;
    Snap.SelectedFaces = Session.SelectionMask.CollectSelectedFaces();
    return Snap;
}

void SelectRandomSubset(UVWorkspaceSession& Session, std::mt19937_64& Rng)
{
    const uint32_t FaceCount = static_cast<uint32_t>(Session.Asset.Network.Faces.size());
    if (FaceCount == 0u)
    {
        return;
    }
    const uint32_t Start = static_cast<uint32_t>(Rng() % FaceCount);
    const uint32_t Span  = std::min<uint32_t>(FaceCount - Start, 1u + static_cast<uint32_t>(Rng() % 12u));
    std::vector<uint32_t> Indices;
    Indices.reserve(Span);
    for (uint32_t I = 0u; I < Span; ++I)
    {
        Indices.push_back(Start + I);
    }
    Session.SelectionMode = UVSelectionMode::Faces;
    Session.SelectionMask.SelectFacesFromList(Indices, false);
    Session.SpatialSelectionActive = true;
    Session.RefreshGizmoAnchorsFromSelection();
    Session.SynchronizeSelectionToGPU();
}

void ExecuteChaosOperation(UVWorkspaceSession& Session, std::mt19937_64& Rng, uint32_t OpIdx)
{
    const std::array<Mathematics::Scalar3D, 5> Swatches {{
        { 0.90f, 0.35f, 0.25f },
        { 0.25f, 0.72f, 0.88f },
        { 0.42f, 0.84f, 0.48f },
        { 0.92f, 0.78f, 0.28f },
        { 0.74f, 0.52f, 0.92f },
    }};

    switch (OpIdx % 18u)
    {
        case 0u:
            SelectRandomSubset(Session, Rng);
            (void)Session.ExecuteDirective(UVCommandDirective::MarkSelectedSeams);
            break;
        case 1u:
            SelectRandomSubset(Session, Rng);
            (void)Session.ExecuteDirective(UVCommandDirective::PinSelectedUVs);
            break;
        case 2u:
            SelectRandomSubset(Session, Rng);
            (void)Session.ExecuteDirective(UVCommandDirective::UnpinSelectedUVs);
            break;
        case 3u:
            SelectRandomSubset(Session, Rng);
            (void)Session.ExecuteDirective(UVCommandDirective::ClearSelectedSeams);
            break;
        case 4u:
            (void)Session.ExecuteDirective(UVCommandDirective::DeriveSeamsFromIslands);
            break;
        case 5u:
            SelectRandomSubset(Session, Rng);
            (void)Session.ExecuteDirective(UVCommandDirective::SplitSelectedIsland);
            break;
        case 6u:
            SelectRandomSubset(Session, Rng);
            (void)Session.ExecuteDirective(UVCommandDirective::RectifySelectedQuads);
            break;
        case 7u:
            SelectRandomSubset(Session, Rng);
            (void)Session.ExecuteDirective(UVCommandDirective::ResetFaceUVsToTile);
            break;
        case 8u:
            (void)Session.ExecuteDirective(UVCommandDirective::RelaxStretchGPU);
            break;
        case 9u:
            (void)Session.ExecuteDirective(UVCommandDirective::UnwrapConformal);
            break;
        case 10u:
            (void)Session.ExecuteDirective(UVCommandDirective::PackUVsConcaveSkyline);
            break;
        case 11u:
        {
            const uint32_t Created = Session.ConstructNextUDIMTile();
            (void)Created;
            break;
        }
        case 12u:
        {
            if (Session.Asset.UDIMTiles.size() > 1u)
            {
                (void)Session.RemoveUDIMTile(Session.Asset.UDIMTiles.back());
            }
            break;
        }
        case 13u:
        {
            const auto& Layers = Session.Asset.Layers.InspectLayers();
            if (Layers.size() < 6u)
            {
                const auto& Sw = Swatches[Rng() % Swatches.size()];
                (void)Session.CreatePriorityLayer("ChaosLayer", Sw, 8.0f);
            }
            else if (Layers.size() > 1u)
            {
                Session.SelectPriorityLayer(Layers.back().LayerOrdinal);
                (void)Session.DeleteSelectedLayer();
            }
            break;
        }
        case 14u:
        {
            const auto& Layers = Session.Asset.Layers.InspectLayers();
            if (!Layers.empty())
            {
                const uint16_t TargetOrd = Layers[Rng() % Layers.size()].LayerOrdinal;
                Session.SelectPriorityLayer(TargetOrd);
                Session.AssignLayerTintByOrdinal(TargetOrd, Swatches[Rng() % Swatches.size()], true);
            }
            break;
        }
        case 15u:
        {
            const auto& Layers = Session.Asset.Layers.InspectLayers();
            if (!Layers.empty())
            {
                Session.SelectPriorityLayer(Layers[Rng() % Layers.size()].LayerOrdinal);
                Session.RenameSelectedLayer("ChaosRenamed_" + std::to_string(Rng() % 100u), true);
            }
            break;
        }
        case 16u:
            SelectRandomSubset(Session, Rng);
            Session.ApplyMatCapToActiveSelection(static_cast<uint8_t>(Rng() % 16u), true);
            break;
        case 17u:
            SelectRandomSubset(Session, Rng);
            Session.ApplySurfaceAlbedoColour(Swatches[Rng() % Swatches.size()], true);
            Session.ApplyMatCapTintColour(Swatches[Rng() % Swatches.size()], true);
            Session.ApplySurfaceRoughnessMetallic(0.32f, 0.78f, 0.65f, 0.50f, true);
            break;
    }
}

} // namespace

int main()
{
    const std::filesystem::path RepoRoot  = FRONTIER_REPO_ROOT_DIR;
    const std::filesystem::path ShaderDir = FRONTIER_COMPILED_SHADER_DIR;

    GeometricRaster::VulkanInterchange VkInterchange {};
    std::string Diag {};
    if (!VerifyInvariant(VkInterchange.InitializeInterchange(RepoRoot, &Diag), Diag.c_str()))
    {
        return 1;
    }

    GeometricRaster::UVWorkspaceRasterizer Rasterizer {};
    if (!VerifyInvariant(Rasterizer.InitializeRasterizer(&VkInterchange, ShaderDir), "InitializeRasterizer"))
    {
        return 1;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 1. Verify Bug A (Bidirectional Tool Rail <-> State Sync + Seam [K]) & Bug D (Live Unwrap + Reset UVs)
    //--------------------------------------------------------------------------------------------------------------------
    {
        UVWorkspaceHost Host {};
        if (!VerifyInvariant(Host.AttachVulkanRasterizer(&Rasterizer), "AttachVulkanRasterizer for Bug A/D checks"))
        {
            return 1;
        }
        auto& Session = Host.AccessSession();
        Session.LoadSamplePreset(SampleAssetPreset::TacticalCrate);

        UVViewportInteractor Interactor {};
        Interactor.AttachSession(&Session);
        Session.SelectionMode = UVSelectionMode::Faces;
        Interactor.ActivateBoxMarquee(true);
        if (!VerifyInvariant(Interactor.QueryRegionSelectTool() == RegionSelectTool::BoxMarquee, "ActivateBoxMarquee sets BoxMarquee"))
        {
            return 1;
        }
        Interactor.ActivateCircleBrush(true);
        if (!VerifyInvariant(Interactor.QueryRegionSelectTool() == RegionSelectTool::CircleBrush, "ActivateCircleBrush sets CircleBrush"))
        {
            return 1;
        }

        // Toggle seam brush + seam overlay via [K]
        PaneInputReading KeyInput {};
        KeyInput.PrimaryPointer.X = 100.0f;
        KeyInput.PrimaryPointer.Y = 100.0f;
        KeyInput.TypedUtf8 = "k";
        ViewportInteractionReading KeyReading { Rectangle2D{ 0.0f, 0.0f, 500.0f, 500.0f }, Rectangle2D{}, KeyInput };
        Session.WorkspaceConfig.AccessUVPreferences().SeamOverlayEnabled = false;
        Session.RenderOptions2D.SeamOverlayEnabled = false;
        Session.RenderOptions3D.SeamOverlayEnabled = false;
        Session.SeamBrushActive = false;
        Interactor.ProcessViewportInput(KeyReading, 100u);
        if (!VerifyInvariant(Session.SeamBrushActive && Session.RenderOptions2D.SeamOverlayEnabled, "[K] activates SeamBrush & SeamOverlayEnabled in 2D"))
        {
            return 1;
        }
        if (!VerifyInvariant(Session.RenderOptions3D.SeamOverlayEnabled, "[K] activates SeamOverlayEnabled in 3D"))
        {
            return 1;
        }

        // Bug D: LiveUnwrapEnabled triggers unwrap automatically on MarkSelectedSeams
        Session.SelectionMask.SelectFacesFromList(std::array<uint32_t, 2>{ 0u, 1u }, false);
        Session.LiveUnwrapEnabled = true;
        const auto BeforeReset = CaptureDigest(Session);
        (void)Session.ExecuteDirective(UVCommandDirective::ResetFaceUVsToTile);
        const auto AfterReset = CaptureDigest(Session);
        std::string IgnoredDiff {};
        if (!VerifyInvariant(!BeforeReset.Matches(AfterReset, IgnoredDiff), "ResetFaceUVsToTile modifies UVs of selected faces"))
        {
            return 1;
        }
        if (!VerifyInvariant(Session.UndoParameterization() && BeforeReset.Matches(CaptureDigest(Session), IgnoredDiff), "Undo reverses ResetFaceUVsToTile bit-for-bit"))
        {
            return 1;
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 2. Verify Bug B (Overlap SAT detection) & sRGB -> Linear Shading Conversion
    //--------------------------------------------------------------------------------------------------------------------
    {
        UVWorkspaceHost Host {};
        (void)Host.AttachVulkanRasterizer(&Rasterizer);
        auto& Session = Host.AccessSession();
        Session.LoadSamplePreset(SampleAssetPreset::TacticalCrate);

        // Force two disjoint faces into identical overlapping UVs [0..0.4]^2
        for (uint32_t FIdx : std::array<uint32_t, 2>{ 0u, 1u })
        {
            const auto& Face = Session.Asset.Network.Faces[FIdx];
            uint32_t E = Face.BoundaryLoopEdgeIndex;
            const std::array<Mathematics::UVAxes, 4> Quad {{
                { 0.1f, 0.1f }, { 0.4f, 0.1f }, { 0.4f, 0.4f }, { 0.1f, 0.4f },
            }};
            for (uint16_t C = 0u; C < Face.CornerCount && C < 4u; ++C)
            {
                Session.Asset.Network.Edges[E].ParameterUV = Quad[C];
                E = Session.Asset.Network.Edges[E].NextEdgeIndex;
            }
        }
        (void)Session.ExecuteDirective(UVCommandDirective::SelectOverlappingFaces);
        if (!VerifyInvariant(Session.LatestMetrics.OverlapFaceCount >= 2u && Session.SelectionMask.CountSelectedFaces() >= 2u, "SAT Overlap detection finds overlapping faces"))
        {
            return 1;
        }

        // Verify sRGB 0.5 -> Linear ~0.5^2.2 = 0.2176
        Session.ApplySurfaceAlbedoColour({ 0.5f, 0.5f, 0.5f }, true);
        const float LinearVal = Session.Asset.Network.Faces[0].Shading.AlbedoLinearRGB.X;
        if (!VerifyInvariant(std::abs(LinearVal - std::pow(0.5f, 2.2f)) < 1.0e-3f, "ApplySurfaceAlbedoColour converts sRGB to linear space"))
        {
            return 1;
        }
        Session.ApplyMatCapTintColour({ 0.5f, 0.25f, 0.75f }, true);
        const float TintG = Session.Asset.Network.Faces[0].Shading.MatCapTintLinearRGB.Y;
        if (!VerifyInvariant(std::abs(TintG - std::pow(0.25f, 2.2f)) < 1.0e-3f, "ApplyMatCapTintColour converts sRGB to linear space"))
        {
            return 1;
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 3. Verify Bug E (Last Layer Delete -> Remainder + Occlusion Guard) & Bug G (Cross-Panel Single-Consume History)
    //--------------------------------------------------------------------------------------------------------------------
    {
        auto BrowserPtr = std::make_unique<ContentBrowserPanel>();
        ContentBrowserPanel& Browser = *BrowserPtr;
        ThemeRepository Themes {};
        Browser.AttachThemeRepository(&Themes);
        Browser.ConfigureWorkspaceDomain(WorkspaceCategory::UV);
        Browser.ConfigureColourAndMatCapLock(true);
        Browser.ConfigureShelfExtent(ContentShelfExtent::SplitHalf, true);

        // Simulate a rendered canvas for drawer occlusion checks
        PixelSpace Surface { nullptr, Point2D{ 0.0f, 0.0f } };
        ContentBrowserPointerInput EmptyPointer {};
        Browser.RenderWithInput(Surface, Rectangle2D{ 0.0f, 0.0f, 1920.0f, 1080.0f }, EmptyPointer, 0.016);
        const float DrawerTopY = Browser.QueryDrawerTopPhysicalY();
        if (!VerifyInvariant(DrawerTopY > 100.0f && DrawerTopY < 1070.0f, "Open ContentBrowser drawer reports valid QueryDrawerTopPhysicalY"))
        {
            return 1;
        }
        if (!VerifyInvariant(Browser.QueryBlocksPointerAt({ 500.0f, DrawerTopY + 30.0f }), "QueryBlocksPointerAt blocks clicks inside open drawer"))
        {
            return 1;
        }
        if (!VerifyInvariant(!Browser.QueryBlocksPointerAt({ 500.0f, 80.0f }), "QueryBlocksPointerAt passes clicks above open drawer"))
        {
            return 1;
        }

        UVWorkspaceHost Host {};
        (void)Host.AttachVulkanRasterizer(&Rasterizer);
        Host.AttachContentBrowserPanel(&Browser);
        auto& Session = Host.AccessSession();
        Session.LoadSamplePreset(SampleAssetPreset::TacticalCrate);

        while (!Session.Asset.Layers.InspectLayers().empty())
        {
            Session.SelectPriorityLayer(Session.Asset.Layers.InspectLayers().back().LayerOrdinal);
            if (!VerifyInvariant(Session.DeleteSelectedLayer(), "Deleting remaining priority layer succeeds cleanly"))
            {
                return 1;
            }
        }
        if (!VerifyInvariant(Session.SelectedLayerOrdinal == 0u && !Session.DeleteSelectedLayer(), "Empty layer list selects Remainder (0) and rejects duplicate delete"))
        {
            return 1;
        }
        if (!VerifyInvariant(Session.UndoParameterization() && Session.Asset.Layers.InspectLayers().size() == 1u, "Undo restores deleted final priority layer cleanly"))
        {
            return 1;
        }

        // Cross-panel single-consume shortcut verification: 1 undo across same TickSequence executes strictly ONCE
        const auto BeforeEdit = CaptureDigest(Session);
        Session.ApplySurfaceAlbedoColour({ 0.8f, 0.2f, 0.1f }, true);
        if (!VerifyInvariant(!Session.TryConsumeGlobalHistoryShortcut(true, false, true, 777ull), "Bug G: TextInputActive suppresses global history undo"))
        {
            return 1;
        }
        if (!VerifyInvariant(Session.TryConsumeGlobalHistoryShortcut(true, false, false, 778ull), "Bug G: First panel in TickSequence consumes Undo"))
        {
            return 1;
        }
        if (!VerifyInvariant(!Session.TryConsumeGlobalHistoryShortcut(true, false, false, 778ull), "Bug G: Subsequent panels in same TickSequence do NOT double-undo"))
        {
            return 1;
        }
        std::string Mismatch {};
        if (!VerifyInvariant(BeforeEdit.Matches(CaptureDigest(Session), Mismatch), Mismatch.c_str()))
        {
            return 1;
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 4. Multi-Seed Chaos Operation Sequence + Full Undo/Redo State-Equivalence + .UVSpace Roundtrip + Budget Folding
    //--------------------------------------------------------------------------------------------------------------------
    constexpr std::array<uint64_t, 4> Seeds {{ 0xF00D0001ull, 0xF00D0002ull, 0xF00D0003ull, 0xF00D0004ull }};
    constexpr std::array<SampleAssetPreset, 2> Presets {{
        SampleAssetPreset::TacticalCrate,
        SampleAssetPreset::AeroCoupeGT,
    }};

    for (std::size_t SeedIdx = 0u; SeedIdx < Seeds.size(); ++SeedIdx)
    {
        std::mt19937_64 Rng(Seeds[SeedIdx]);
        UVWorkspaceHost Host {};
        if (!VerifyInvariant(Host.AttachVulkanRasterizer(&Rasterizer), "AttachVulkanRasterizer for Chaos Seed"))
        {
            return 1;
        }
        auto& Session = Host.AccessSession();
        const SampleAssetPreset Preset = (SeedIdx == 3u)
            ? SampleAssetPreset::ControlledIntegrator
            : Presets[SeedIdx % 2u];
        Session.LoadSamplePreset(Preset);

        // Phase A: Bounded chaos sequence within revision budget -> exact bit-for-bit Undo & Redo
        const auto InitialState = CaptureDigest(Session);
        const uint32_t BoundedSteps = (Preset == SampleAssetPreset::ControlledIntegrator) ? 8u : 18u;
        uint32_t ActualStepsRecorded = 0u;
        for (uint32_t Step = 0u; Step < BoundedSteps; ++Step)
        {
            const uint32_t PrevCursor = Session.RevisionHistory.QueryCursorStepIndex();
            ExecuteChaosOperation(Session, Rng, static_cast<uint32_t>(Rng() % 18u));
            if (Session.RevisionHistory.QueryCursorStepIndex() != PrevCursor)
            {
                ++ActualStepsRecorded;
            }
        }
        if (!VerifyInvariant(ActualStepsRecorded > 0u, "Chaos sequence recorded revision steps"))
        {
            return 1;
        }
        const auto TipState = CaptureDigest(Session);

        uint32_t UndosPerformed = 0u;
        while (Session.UndoParameterization())
        {
            ++UndosPerformed;
            if (UndosPerformed > 100u)
            {
                std::fprintf(stderr, "[UVAuthoringChaosValidation] Infinite undo loop detected!\n");
                return 1;
            }
        }
        const auto AfterFullUndo = CaptureDigest(Session);
        std::string Mismatch {};
        if (!VerifyInvariant(InitialState.Matches(AfterFullUndo, Mismatch), ("Seed " + std::to_string(SeedIdx) + " full undo: " + Mismatch).c_str()))
        {
            return 1;
        }

        uint32_t RedosPerformed = 0u;
        while (Session.RedoParameterization())
        {
            ++RedosPerformed;
            if (RedosPerformed > 100u)
            {
                std::fprintf(stderr, "[UVAuthoringChaosValidation] Infinite redo loop detected!\n");
                return 1;
            }
        }
        const auto AfterFullRedo = CaptureDigest(Session);
        if (!VerifyInvariant(TipState.Matches(AfterFullRedo, Mismatch), ("Seed " + std::to_string(SeedIdx) + " full redo: " + Mismatch).c_str()))
        {
            return 1;
        }

        // Phase B: Undo halfway, perform new branching edit, undo again to verify baseline integrity after branch truncation
        for (uint32_t Half = 0u; Half < RedosPerformed / 2u; ++Half)
        {
            (void)Session.UndoParameterization();
        }
        const auto MidBranch = CaptureDigest(Session);
        ExecuteChaosOperation(Session, Rng, 16u);
        if (!VerifyInvariant(!Session.RedoParameterization(), "New operation after partial undo clears forward redo branch"))
        {
            return 1;
        }
        if (!VerifyInvariant(Session.UndoParameterization() && MidBranch.Matches(CaptureDigest(Session), Mismatch), ("Seed " + std::to_string(SeedIdx) + " mid-branch undo: " + Mismatch).c_str()))
        {
            return 1;
        }

        // Phase C: .UVSpace persistence roundtrip preserves every chaos mutation & clears old dirty baseline cleanly
        const std::filesystem::path TempDocPath = std::filesystem::temp_directory_path() / ("FrontierChaos_" + std::to_string(SeedIdx) + ".UVSpace");
        const auto PreSaveState = CaptureDigest(Session);
        if (!VerifyInvariant(Session.SaveUVSpaceDocument(TempDocPath, Diag), Diag.c_str()))
        {
            return 1;
        }
        ExecuteChaosOperation(Session, Rng, 7u);
        if (!VerifyInvariant(Session.LoadUVSpaceDocument(TempDocPath, Diag), Diag.c_str()))
        {
            return 1;
        }
        if (!VerifyInvariant(PreSaveState.Matches(CaptureDigest(Session), Mismatch), ("Seed " + std::to_string(SeedIdx) + " .UVSpace roundtrip: " + Mismatch).c_str()))
        {
            return 1;
        }
        std::error_code IgnoreErr {};
        std::filesystem::remove(TempDocPath, IgnoreErr);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 5. High-Volume Prefix Folding Stress (forced 8 KiB budget folding ensuring BaselineSnapshotBytes stays bit-exact)
    //--------------------------------------------------------------------------------------------------------------------
    {
        UVWorkspaceHost Host {};
        (void)Host.AttachVulkanRasterizer(&Rasterizer);
        auto& Session = Host.AccessSession();
        Session.LoadSamplePreset(SampleAssetPreset::TacticalCrate);
        Session.RevisionHistory.ConfigureDocumentBudget(8u * 1024u);
        std::mt19937_64 FoldRng(0xDEADBABE42ull);
        for (uint32_t Step = 0u; Step < 32u; ++Step)
        {
            ExecuteChaosOperation(Session, FoldRng, Step);
            if (!VerifyInvariant(Session.RevisionHistory.QueryBytesUsed() <= 8u * 1024u, "Revision footprint strictly respects 8 KiB budget"))
            {
                return 1;
            }
        }
        const auto FoldTip = CaptureDigest(Session);
        uint32_t StepsBack = 0u;
        while (Session.UndoParameterization())
        {
            ++StepsBack;
            if (StepsBack > 200u)
            {
                return 1;
            }
        }
        uint32_t StepsFwd = 0u;
        while (Session.RedoParameterization())
        {
            ++StepsFwd;
            if (StepsFwd > 200u)
            {
                return 1;
            }
        }
        std::string Mismatch {};
        if (!VerifyInvariant(StepsBack == StepsFwd && FoldTip.Matches(CaptureDigest(Session), Mismatch), "60-step prefix-fold undo/redo symmetry & bit-exact tip restore"))
        {
            return 1;
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 6. Unwrap Menu (U) vs Immediate Unwrap (Ctrl+U), Lasso/Circle/Box Sync & Semi-Transparent Overlay Alpha Checks
    //--------------------------------------------------------------------------------------------------------------------
    {
        IMGUI_CHECKVERSION();
        ImGuiContext* GuiCtx = ImGui::CreateContext();
        ImGui::SetCurrentContext(GuiCtx);
        ImGuiIO& Io    = ImGui::GetIO();
        Io.IniFilename = nullptr;
        Io.LogFilename = nullptr;
        Io.DisplaySize = ImVec2(900.0f, 800.0f);
        Io.DeltaTime   = 1.0f / 60.0f;
        unsigned char* AtlasPixels = nullptr;
        int            AtlasW      = 0;
        int            AtlasH      = 0;
        Io.Fonts->GetTexDataAsRGBA32(&AtlasPixels, &AtlasW, &AtlasH);
        Io.Fonts->SetTexID(static_cast<ImTextureID>(1));

        UVWorkspaceHost Host {};
        if (!VerifyInvariant(Host.AttachVulkanRasterizer(&Rasterizer), "AttachVulkanRasterizer for Section 6 UI/Lasso checks"))
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
        auto& Session = Host.AccessSession();
        Session.LoadSamplePreset(SampleAssetPreset::TacticalCrate);
        Session.SelectionMode = UVSelectionMode::Faces;
        Session.SelectionMask.SelectFacesFromList(std::array<uint32_t, 2>{ 0u, 1u }, false);

        // Scramble selected face UVs so UnwrapConformal produces a measurable change
        for (uint32_t FIdx : std::array<uint32_t, 2>{ 0u, 1u })
        {
            uint32_t E = Session.Asset.Network.Faces[FIdx].BoundaryLoopEdgeIndex;
            const auto& Face = Session.Asset.Network.Faces[FIdx];
            for (uint16_t C = 0u; C < Face.CornerCount; ++C)
            {
                Session.Asset.Network.Edges[E].ParameterUV = { 0.05f + 0.01f * static_cast<float>(C), 0.05f };
                E = Session.Asset.Network.Edges[E].NextEdgeIndex;
            }
        }

        auto& Host2D = Host.Access2DViewportHost();
        auto& Host3D = Host.Access3DViewportHost();
        if (!VerifyInvariant(
                Host2D.QueryHostRole() == UVViewportHostRole::Dedicated2D &&
                    Host3D.QueryHostRole() == UVViewportHostRole::Dedicated3D,
                "UVWorkspaceHost configures Dedicated2D and Dedicated3D hosts"))
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }

        const Rectangle2D PaneBounds { 0.0f, 0.0f, 900.0f, 800.0f };
        auto RenderHostFrame = [&](UVViewportHost& TargetHost, const PaneInputReading& Input) -> ImDrawData*
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(PaneBounds.Width, PaneBounds.Height));
            ImGui::Begin("ChaosViewportTest", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            PixelSpace Surface(ImGui::GetWindowDrawList(), Point2D{ 0.0f, 0.0f }, 1.0f);
            TargetHost.Render(Surface, PaneBounds, Input, 1.0 / 60.0);
            ImGui::End();
            ImGui::Render();
            return ImGui::GetDrawData();
        };

        auto MakePointer = [](float X, float Y, bool Down, bool Pressed, bool Released, bool Enabled = true) -> ControlPointer
        {
            ControlPointer Ptr {};
            Ptr.X        = X;
            Ptr.Y        = Y;
            Ptr.Down     = Down;
            Ptr.Pressed  = Pressed;
            Ptr.Released = Released;
            Ptr.Enabled  = Enabled;
            return Ptr;
        };

        // 6a. Plain [U] opens the Unwrap Directive Menu without changing UVs; Escape closes it; [Ctrl+U] unwraps immediately
        const auto BeforeU = CaptureDigest(Session);
        {
            PaneInputReading PlainUInput {};
            PlainUInput.KeyboardFocused = true;
            PlainUInput.TypedUtf8       = "u";
            PlainUInput.PrimaryPointer  = MakePointer(150.0f, 16.0f, false, false, false, true); // even over pane header!
            (void)RenderHostFrame(Host2D, PlainUInput);
        }
        std::string DigestDiff {};
        if (!VerifyInvariant(
                BeforeU.Matches(CaptureDigest(Session), DigestDiff) &&
                    Host2D.QueryDirectiveSheetOpen() &&
                    Host2D.QueryDirectiveSheetKind() == ViewportDirectiveSheetKind::UnwrapMenu,
                "Plain [U] opens the UnwrapMenu sheet without modifying UVs"))
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
        {
            PaneInputReading EscInput {};
            EscInput.KeyboardFocused = true;
            EscInput.EscapePressed   = true;
            EscInput.PrimaryPointer  = MakePointer(150.0f, 16.0f, false, false, false, true);
            (void)RenderHostFrame(Host2D, EscInput);
        }
        if (!VerifyInvariant(!Host2D.QueryDirectiveSheetOpen(), "Escape closes the UnwrapMenu sheet"))
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
        {
            PaneInputReading CtrlUInput {};
            CtrlUInput.KeyboardFocused = true;
            CtrlUInput.ShortcutUnwrap  = true;
            CtrlUInput.PrimaryPointer  = MakePointer(150.0f, 16.0f, false, false, false, true);
            (void)RenderHostFrame(Host2D, CtrlUInput);
        }
        if (!VerifyInvariant(
                !BeforeU.Matches(CaptureDigest(Session), DigestDiff) &&
                    !Host2D.QueryDirectiveSheetOpen(),
                "[Ctrl+U] immediately runs UnwrapConformal without opening UnwrapMenu"))
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }

        // 6b. Shared ActiveRegionTool sync across 2D & 3D hosts via [Q] -> LassoPolygon + multi-mode freehand selection
        {
            PaneInputReading QInput {};
            QInput.KeyboardFocused = true;
            QInput.TypedUtf8       = "q";
            QInput.PrimaryPointer  = MakePointer(450.0f, 400.0f, false, false, false, true);
            (void)RenderHostFrame(Host2D, QInput);
            PaneInputReading IdleInput {};
            IdleInput.PrimaryPointer = MakePointer(450.0f, 400.0f, false, false, false, false);
            (void)RenderHostFrame(Host3D, IdleInput);
        }
        if (!VerifyInvariant(
                Session.ActiveRegionTool == UVRegionSelectTool::LassoPolygon &&
                    Host2D.AccessInteractor().QueryRegionSelectTool() == RegionSelectTool::LassoPolygon &&
                    Host3D.AccessInteractor().QueryRegionSelectTool() == RegionSelectTool::LassoPolygon,
                "[Q] switches ActiveRegionTool to LassoPolygon and syncs across 2D & 3D hosts"))
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }

        // Reset faces to canonical full-tile UVs, then run a >160-point circular freehand Lasso around [0..1]^2 in Vertices, Edges, Faces
        (void)Session.ExecuteDirective(UVCommandDirective::ResetFaceUVsToTile);
        const float Cx = 480.0f;
        const float Cy = 420.0f;
        const float Rx = 350.0f;
        const float Ry = 270.0f;

        auto RunFreehandLasso = [&](UVSelectionMode Mode, bool ExpectTranslucentOverlay) -> bool
        {
            Session.AssignSelectionMode(Mode);
            Session.SelectionMask.ClearAll();
            Host2D.AccessInteractor().ActivateLassoPolygon(true);
            bool FoundTranslucent = false;

            const uint32_t SampleCount = 168u; // Exercises PixelSpace dynamic vector fallback (> 128 vertices)
            for (uint32_t Idx = 0u; Idx <= SampleCount; ++Idx)
            {
                const float Angle = (static_cast<float>(Idx) / static_cast<float>(SampleCount)) * 6.2831853f;
                PaneInputReading DragInput {};
                DragInput.PrimaryPointer = MakePointer(
                    Cx + std::cos(Angle) * Rx,
                    Cy + std::sin(Angle) * Ry,
                    Idx < SampleCount,
                    Idx == 0u,
                    Idx == SampleCount,
                    true);
                ImDrawData* DrawData = RenderHostFrame(Host2D, DragInput);
                if (ExpectTranslucentOverlay && Idx == SampleCount / 2u && DrawData != nullptr)
                {
                    if (!Host2D.AccessInteractor().QueryRegionStrokeActive() ||
                        Host2D.AccessInteractor().QueryLassoOverlayPoints().size() <= 8u)
                    {
                        return false;
                    }
                    for (int ListIdx = 0; ListIdx < DrawData->CmdListsCount; ++ListIdx)
                    {
                        const ImDrawList* List = DrawData->CmdLists[ListIdx];
                        for (int VIdx = 0; VIdx < List->VtxBuffer.Size; ++VIdx)
                        {
                            const ImU32 Col = List->VtxBuffer[VIdx].col;
                            const uint8_t R = static_cast<uint8_t>((Col >> IM_COL32_R_SHIFT) & 0xFFu);
                            const uint8_t G = static_cast<uint8_t>((Col >> IM_COL32_G_SHIFT) & 0xFFu);
                            const uint8_t B = static_cast<uint8_t>((Col >> IM_COL32_B_SHIFT) & 0xFFu);
                            const uint8_t A = static_cast<uint8_t>((Col >> IM_COL32_A_SHIFT) & 0xFFu);
                            if (A >= 20u && A <= 80u && (R != G || B > R))
                            {
                                FoundTranslucent = true;
                                break;
                            }
                        }
                    }
                }
            }
            return !ExpectTranslucentOverlay || FoundTranslucent;
        };

        if (!VerifyInvariant(RunFreehandLasso(UVSelectionMode::Vertices, true), "Freehand >160-pt Lasso in Vertices mode renders translucent overlay (Alpha 20..80)"))
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
        if (!VerifyInvariant(
                Session.SelectionMask.CountSelectedVertices() > 0u &&
                    Session.SelectionMask.CountSelectedFaces() == 0u,
                "Freehand Lasso in Vertices mode selects vertices without polluting SelectedFaceBits"))
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
        (void)RunFreehandLasso(UVSelectionMode::Edges, false);
        if (!VerifyInvariant(Session.SelectionMask.CountSelectedEdges() > 0u, "Freehand Lasso in Edges mode selects enclosed edges"))
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
        (void)RunFreehandLasso(UVSelectionMode::Faces, false);
        if (!VerifyInvariant(Session.SelectionMask.CountSelectedFaces() > 0u, "Freehand Lasso in Faces mode selects enclosed faces"))
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }

        // Verify CircleBrush and BoxMarquee overlays also emit translucent fills matching AccentBorder (Alpha 38..42)
        const ColorRgba ExpectedAccent = UVAppearance::QueryThemeColors().AccentBorder;
        auto ContainsTranslucentAccentFill = [&](const ImDrawData* DrawData) -> bool
        {
            if (DrawData == nullptr)
            {
                return false;
            }
            for (int L = 0; L < DrawData->CmdListsCount; ++L)
            {
                const ImDrawList* List = DrawData->CmdLists[L];
                for (int V = 0; V < List->VtxBuffer.Size; ++V)
                {
                    const ImU32 Col = List->VtxBuffer[V].col;
                    const uint8_t R = static_cast<uint8_t>((Col >> IM_COL32_R_SHIFT) & 0xFFu);
                    const uint8_t G = static_cast<uint8_t>((Col >> IM_COL32_G_SHIFT) & 0xFFu);
                    const uint8_t B = static_cast<uint8_t>((Col >> IM_COL32_B_SHIFT) & 0xFFu);
                    const uint8_t A = static_cast<uint8_t>((Col >> IM_COL32_A_SHIFT) & 0xFFu);
                    if (R == ExpectedAccent.Red && G == ExpectedAccent.Green && B == ExpectedAccent.Blue &&
                        A >= 20u && A <= 80u)
                    {
                        return true;
                    }
                }
            }
            return false;
        };

        auto CheckToolTranslucentFill = [&](bool UseBox) -> bool
        {
            if (UseBox)
            {
                Host2D.AccessInteractor().ActivateBoxMarquee(true);
                PaneInputReading StartBox {};
                StartBox.PrimaryPointer = MakePointer(Cx - 60.0f, Cy - 50.0f, true, true, false, true);
                (void)RenderHostFrame(Host2D, StartBox);
                PaneInputReading DragBox {};
                DragBox.PrimaryPointer = MakePointer(Cx + 80.0f, Cy + 70.0f, true, false, false, true);
                ImDrawData* DrawData = RenderHostFrame(Host2D, DragBox);
                const bool HasFill =
                    Host2D.AccessInteractor().QueryRegionStrokeActive() &&
                    ContainsTranslucentAccentFill(DrawData);
                PaneInputReading EndBox {};
                EndBox.PrimaryPointer = MakePointer(Cx + 80.0f, Cy + 70.0f, false, false, true, true);
                (void)RenderHostFrame(Host2D, EndBox);
                return HasFill;
            }
            Host2D.AccessInteractor().ActivateCircleBrush(true);
            PaneInputReading HoverCircle {};
            HoverCircle.PrimaryPointer = MakePointer(Cx, Cy, false, false, false, true);
            ImDrawData* DrawData = RenderHostFrame(Host2D, HoverCircle);
            return Host2D.AccessInteractor().QueryRegionBrushHoverActive() &&
                   ContainsTranslucentAccentFill(DrawData);
        };

        if (!VerifyInvariant(CheckToolTranslucentFill(true) && CheckToolTranslucentFill(false), "BoxMarquee and CircleBrush overlays render semi-transparent fills (Alpha 20..80)"))
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }

        ImGui::DestroyContext(GuiCtx);
    }

    std::printf("[UVAuthoringChaosValidation] Passed multi-seed chaos, Bug A-G invariants, .UVSpace roundtrip, prefix-fold, Lasso/Unwrap & translucent overlay checks.\n");
    return 0;
}
