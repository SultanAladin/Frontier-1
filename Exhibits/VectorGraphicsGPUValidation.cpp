/*============================================================================================================================================
                                                    VECTORGRAPHICSGPUVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation suite for ThorVG 1.0.0 SVG/TTF/CFF-OTF frontend and 4-stage Vulkan .slang GPU VectorGraphics rasterizer.

#include "Engine/Codecs/VectorCodec.h"
#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/GeometricRaster/Gizmo/TransformGizmo.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometricRaster/Vector/VectorCanvasRasterizer.h"
#include "Engine/GeometricRaster/Vector/VulkanVectorRenderMethod.h"
#include "Engine/Workspaces/Texturing/PatternStampStackSession.h"
#include "Engine/Workspaces/Texturing/PatternStampWorkspaceHost.h"
#include "Engine/Workspaces/Texturing/VectorViewportInteractor.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

[[nodiscard]] bool VerifyCheck(bool ConditionMet, const char* Message) noexcept
{
    if (!ConditionMet)
    {
        std::fprintf(stderr, "[VectorGraphicsGPUValidation] FAILED: %s\n", Message);
        return false;
    }
    return true;
}

[[nodiscard]] bool WriteBufferToPng(
    const std::filesystem::path& OutPath,
    uint32_t                     WidthPx,
    uint32_t                     HeightPx,
    const std::vector<uint8_t>&  RgbaPixels)
{
    std::filesystem::create_directories(OutPath.parent_path());
    Frontier::Exhibits::SoftwareRasterCanvas Canvas(WidthPx, HeightPx, { 16u, 18u, 22u, 255u });
    Canvas.BlitRgbaRegion(0u, 0u, WidthPx, HeightPx, RgbaPixels.data());
    return Canvas.WritePngFile(OutPath);
}

[[nodiscard]] double ComputeMeanAbsoluteDifference(
    const std::vector<uint8_t>& BufferA,
    const std::vector<uint8_t>& BufferB) noexcept
{
    const std::size_t Count = std::min(BufferA.size(), BufferB.size());
    if (Count == 0u)
    {
        return 0.0;
    }
    double Acc = 0.0;
    for (std::size_t I = 0u; I < Count; I += 4u)
    {
        Acc += std::abs(static_cast<int>(BufferA[I + 0u]) - static_cast<int>(BufferB[I + 0u]));
        Acc += std::abs(static_cast<int>(BufferA[I + 1u]) - static_cast<int>(BufferB[I + 1u]));
        Acc += std::abs(static_cast<int>(BufferA[I + 2u]) - static_cast<int>(BufferB[I + 2u]));
    }
    return Acc / static_cast<double>((Count / 4u) * 3u);
}

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::GeometricRaster;
    using namespace Frontier::Workspaces;
    using namespace Frontier::Editor;

    const std::filesystem::path RepoRoot  = Frontier::Exhibits::ResolveRepositoryRootPath();
    const std::filesystem::path ShaderDir = std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);

    // 1. Validate ThorVG 1.0.0 SVG DOM, TrueType .ttf, and CFF .otf font extraction
    {
        ThorVGVectorFrontend Frontend {};
        std::vector<ExtractedVectorShape> SvgShapes {};
        std::string SvgDiag {};
        constexpr const char* TestSvg =
            R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 120 120">)"
            R"(<rect x="12" y="12" width="96" height="96" rx="18" fill="#E69A38"/>)"
            R"(<path d="M 24 60 L 96 60" stroke="#182030" stroke-width="8"/>)"
            R"(</svg>)";
        if (!VerifyCheck(Frontend.CompileSvgToGpuShapes(TestSvg, false, 240.0f, 256.0f, 256.0f, SvgShapes, &SvgDiag), SvgDiag.c_str()) ||
            !VerifyCheck(SvgShapes.size() >= 2u, "ThorVG SVG parser produced both rect fill and path stroke shapes") ||
            !VerifyCheck(!SvgShapes[0].FillFanTriangles.empty(), "ThorVG SVG rounded rect produced triangle-fan stencil geometry") ||
            !VerifyCheck(!SvgShapes[1].StrokeTriangles.empty(), "ThorVG SVG stroked line produced triangulated stroke geometry"))
        {
            return 1;
        }

        // TrueType (.ttf) outline extraction
        std::vector<ExtractedVectorShape> TtfShapes {};
        float TtfW = 0.0f;
        float TtfH = 0.0f;
        std::string TtfDiag {};
        const std::filesystem::path TtfPath = RepoRoot / "EngineContent/FontsArchives/Archivo/Archivo-Bold.ttf";
        if (!VerifyCheck(
                Frontend.CompileTextToGpuShapes(
                    TtfPath, "Archivo-Bold", "FRONTIER", 48.0f, 0.0f, 0.0f,
                    { 240u, 240u, 248u, 255u }, 0.0f, { 20u, 20u, 26u, 255u },
                    TtfShapes, &TtfW, &TtfH, &TtfDiag),
                TtfDiag.c_str()) ||
            !VerifyCheck(!TtfShapes.empty() && !TtfShapes[0].FillFanTriangles.empty(), "TrueType (.ttf) text compiled into vector fill triangles") ||
            !VerifyCheck(TtfW > 50.0f && TtfH > 15.0f, "TrueType (.ttf) text produced valid bounding metrics"))
        {
            return 1;
        }

        // OpenType CFF (.otf) outline extraction fallback via stb_truetype
        std::vector<ExtractedVectorShape> OtfShapes {};
        float OtfW = 0.0f;
        float OtfH = 0.0f;
        std::string OtfDiag {};
        const std::filesystem::path OtfPath = RepoRoot / "EngineContent/FontsArchives/Inter/Inter-Bold.otf";
        if (!VerifyCheck(
                Frontend.CompileTextToGpuShapes(
                    OtfPath, "Inter-Bold", "STAMP", 52.0f, 0.0f, 0.0f,
                    { 255u, 210u, 110u, 255u }, 2.0f, { 16u, 18u, 24u, 255u },
                    OtfShapes, &OtfW, &OtfH, &OtfDiag),
                OtfDiag.c_str()) ||
            !VerifyCheck(!OtfShapes.empty() && !OtfShapes[0].FillFanTriangles.empty(), "CFF OTTO (.otf) font compiled via stb_truetype outline extractor") ||
            !VerifyCheck(OtfW > 40.0f && OtfH > 15.0f, "CFF OTTO (.otf) text produced valid bounding metrics"))
        {
            return 1;
        }
    }

    // 2. Initialize Vulkan Interchange + VectorCanvasRasterizer
    VulkanInterchange VkInterchange {};
    std::string VkDiag {};
    if (!VerifyCheck(VkInterchange.InitializeInterchange(RepoRoot, &VkDiag), VkDiag.c_str()))
    {
        return 1;
    }

    VectorCanvasRasterizer Rasterizer {};
    if (!VerifyCheck(Rasterizer.InitializeRasterizer(&VkInterchange, ShaderDir), "Initialized VectorCanvasRasterizer (.slang compute + stencil-cover + PBR + overlay MRT)"))
    {
        return 1;
    }

    // 3. Build rich PatternStampWorkspaceSession (Ground gradient + Honeycomb grid array + PointField hero motif + CFF typography)
    PatternStampWorkspaceSession Session(PatternStampWorkspaceMode::Pattern);
    Session.LoadPresetByIndex(0u);
    if (auto* Sel = Session.InspectSelectedLayer())
    {
        Sel->FillSource = PatternStampFillSourceMode::PointField;
        PatternStampWorkspaceSession::InitializeDefaultPointFieldPins(*Sel);
        Sel->StrokeWidthPx = 4.0f;
        Sel->StrokeTint    = { 28u, 32u, 42u, 255u };
        Sel->PointFieldPins[0].Tint = { 235u, 148u, 52u, 255u };
        Sel->PointFieldPins[5].Tint = { 82u, 198u, 224u, 255u };
        Sel->PointFieldPins[9].Tint = { 238u, 76u, 118u, 255u };
    }
    Session.AddLayer(PatternStampLayerCategory::Typographic);
    if (auto* TextLayer = Session.InspectSelectedLayer())
    {
        TextLayer->TextContent   = "VECTOR";
        TextLayer->FontFilePath = "EngineContent/FontsArchives/Inter/Inter-Bold.otf";
        TextLayer->CenterY       = 410.0f;
        TextLayer->FontSizePx    = 44.0f;
        TextLayer->FillTint      = { 246u, 248u, 252u, 255u };
        TextLayer->StrokeWidthPx = 2.0f;
        TextLayer->StrokeTint    = { 18u, 22u, 30u, 255u };
    }
    Session.BumpRevision();

    TransformGizmo Gizmo2D {};
    Gizmo2D.ConfigureDimensionMode(GizmoDimensionMode::Planar2D);
    if (const auto* Sel = Session.InspectSelectedLayer())
    {
        Gizmo2D.ConfigureAnchor({ Sel->CenterX, -Sel->CenterY, 0.0f }, 74.0f);
    }
    Gizmo2D.AssignHoveredHandle(GizmoHandleOrdinal::TranslateX);

    constexpr uint32_t ViewW = 780u;
    constexpr uint32_t ViewH = 560u;

    std::vector<uint8_t> VectorPixels {};
    if (!VerifyCheck(Rasterizer.RasterizeVectorCanvas(ViewW, ViewH, Session, &Gizmo2D, &VectorPixels), "Rasterized VectorSVG viewport via GPU pipeline") ||
        !VerifyCheck(VectorPixels.size() == static_cast<std::size_t>(ViewW) * ViewH * 4u, "Output RGBA8 buffer dimensions match viewport"))
    {
        return 1;
    }
    std::vector<uint8_t> VectorPixelsRepeat {};
    if (!VerifyCheck(Rasterizer.RasterizeVectorCanvas(ViewW, ViewH, Session, &Gizmo2D, &VectorPixelsRepeat), "Re-rasterized VectorSVG viewport for determinism verification") ||
        !VerifyCheck(ComputeMeanAbsoluteDifference(VectorPixels, VectorPixelsRepeat) < 0.01, "Consecutive GPU vector rasterizations are bit-deterministic"))
    {
        return 1;
    }

    const VectorGpuEvaluationMetrics EvaluatedMetrics = Rasterizer.QueryLastMetrics();
    if (!VerifyCheck(EvaluatedMetrics.TotalInstancesEvaluated >= 1u, "GPU VectorModifierExpand evaluated active layer instances"))
    {
        return 1;
    }

    // Verify GPU Pick ID target across layers & gizmo handle
    bool FoundAnyLayerPick = false;
    ViewportPickHit CenterPick {};
    for (int32_t Dy = -36; Dy <= 36 && !FoundAnyLayerPick; Dy += 4)
    {
        for (int32_t Dx = -36; Dx <= 36; Dx += 4)
        {
            const ViewportPickHit CandidateHit = Rasterizer.Pick2DPixel(
                static_cast<int32_t>(ViewW / 2u) + Dx,
                static_cast<int32_t>(ViewH / 2u) + Dy);
            if (CandidateHit.InstanceSlot != 0u || CandidateHit.GizmoHandleOrdinal != 0u)
            {
                CenterPick        = CandidateHit;
                FoundAnyLayerPick = true;
                break;
            }
        }
    }
    if (!VerifyCheck(FoundAnyLayerPick, "GPU Pick ID buffer recorded valid layer or gizmo ordinal near canvas center"))
    {
        return 1;
    }

    // 4. Verify OpenPBR Roughness view channel renders distinct shading buffer
    Session.ViewChannel = PatternStampViewChannel::Roughness;
    std::vector<uint8_t> RoughPixels {};
    if (!VerifyCheck(Rasterizer.RasterizeVectorCanvas(ViewW, ViewH, Session, nullptr, &RoughPixels), "Rasterized OpenPBR Roughness diagnostic channel") ||
        !VerifyCheck(ComputeMeanAbsoluteDifference(VectorPixels, RoughPixels) > 8.0, "OpenPBR Roughness channel differs visibly from VectorSVG view"))
    {
        return 1;
    }

    // 5. Verify 128x128 GPU thumbnail bake
    Session.ViewChannel = PatternStampViewChannel::VectorSVG;
    std::vector<uint8_t> ThumbPixels {};
    if (!VerifyCheck(Rasterizer.RenderThumbnailRGBA(128u, 128u, Session, ThumbPixels), "Baked 128x128 GPU Vector thumbnail") ||
        !VerifyCheck(ThumbPixels.size() == 128u * 128u * 4u, "Thumbnail size equals 128x128 RGBA8"))
    {
        return 1;
    }

    // 6. Verify DomainWarp modifier executes deterministically across fill & stroke shaders
    if (auto* TextLayer = Session.InspectSelectedLayer())
    {
        PatternStampModifierSpec WarpMod {};
        WarpMod.Category      = PatternStampModifierCategory::DomainWarp;
        WarpMod.Enabled       = true;
        WarpMod.WarpMode      = 0u;
        WarpMod.WarpAmplitude = 18.0f;
        TextLayer->Modifiers.push_back(WarpMod);
        Session.BumpRevision();
    }
    std::vector<uint8_t> WarpedPixelsA {};
    std::vector<uint8_t> WarpedPixelsB {};
    if (!VerifyCheck(Rasterizer.RasterizeVectorCanvas(ViewW, ViewH, Session, &Gizmo2D, &WarpedPixelsA), "Rasterized DomainWarp fill + stroke scene (pass A)") ||
        !VerifyCheck(Rasterizer.RasterizeVectorCanvas(ViewW, ViewH, Session, &Gizmo2D, &WarpedPixelsB), "Rasterized DomainWarp fill + stroke scene (pass B)") ||
        !VerifyCheck(ComputeMeanAbsoluteDifference(WarpedPixelsA, WarpedPixelsB) < 0.01, "DomainWarp fill + stroke rasterization is deterministic") ||
        !VerifyCheck(ComputeMeanAbsoluteDifference(VectorPixels, WarpedPixelsA) > 0.1, "DomainWarp visibly displaces stroked typography"))
    {
        return 1;
    }

    // Export inspection PNGs
    (void)WriteBufferToPng(RepoRoot / "Exhibits/Images/Texturing/GPU_VectorStageCheck.png", ViewW, ViewH, VectorPixels);
    (void)WriteBufferToPng(RepoRoot / "Exhibits/Images/Texturing/GPU_VectorRoughnessCheck.png", ViewW, ViewH, RoughPixels);
    (void)WriteBufferToPng(RepoRoot / "Exhibits/Images/Texturing/GPU_VectorThumbCheck.png", 128u, 128u, ThumbPixels);

    // 7. Verify VectorCodec full workspace round-trip ([VectorWorkspace] + [[VectorLayers]] + .SvgXml)
    const std::filesystem::path ScratchDir = std::filesystem::temp_directory_path() / "frontier_vector_phase34_test";
    std::filesystem::create_directories(ScratchDir);
    const std::filesystem::path SavedSpacePath = ScratchDir / "RoundTripVector.VectorSpace";
    {
        const auto BuiltModel = Session.BuildVectorSpaceModel();
        std::string CodecDiag {};
        if (!VerifyCheck(Codecs::VectorEncoder::EncodeToFile(SavedSpacePath, BuiltModel, CodecDiag), "Encoded .VectorSpace with workspace layers & .SvgXml"))
        {
            std::filesystem::remove_all(ScratchDir);
            return 1;
        }
        Codecs::VectorSpaceDocumentModel ReloadedModel {};
        if (!VerifyCheck(Codecs::VectorDecoder::DecodeFromFile(SavedSpacePath, ReloadedModel, CodecDiag), "Decoded .VectorSpace file back from disk") ||
            !VerifyCheck(ReloadedModel.WorkspaceDocumentPresent && !ReloadedModel.EmbeddedSvgXml.empty(), "Decoded .VectorSpace preserves workspace flag and embedded .SvgXml") ||
            !VerifyCheck(ReloadedModel.WorkspaceLayers.size() == Session.Document.Layers.size(), "Decoded .VectorSpace preserves full layer stack"))
        {
            std::filesystem::remove_all(ScratchDir);
            return 1;
        }
        PatternStampWorkspaceSession RestoredSession(PatternStampWorkspaceMode::Pattern);
        if (!VerifyCheck(RestoredSession.LoadFromVectorSpaceModel(ReloadedModel), "Hydrated PatternStampWorkspaceSession from decoded .VectorSpace") ||
            !VerifyCheck(RestoredSession.Document.Layers.size() == Session.Document.Layers.size(), "Restored session layer count matches original") ||
            !VerifyCheck(!RestoredSession.Document.Layers.back().Modifiers.empty(), "Restored typographic layer retained its modifier stack"))
        {
            std::filesystem::remove_all(ScratchDir);
            return 1;
        }
    }

    // 8. Verify VectorViewportInteractor (Pen tool contour + Square creation + Cursor-anchored zoom + Direct layer drag)
    {
        PatternStampWorkspaceSession EditSession(PatternStampWorkspaceMode::Stamp);
        VectorViewportInteractor Interactor {};
        Interactor.AttachSession(&EditSession);
        Interactor.AttachRasterizer(&Rasterizer);
        (void)Rasterizer.RasterizeVectorCanvas(ViewW, ViewH, EditSession, &Interactor.InspectGizmo2D(), nullptr);

        const Rectangle2D CanvasBox { 0.0f, 0.0f, static_cast<float>(ViewW), static_cast<float>(ViewH) };
        const auto InitialTileMetrics = VectorCanvasRasterizer::ComputeTileLayoutMetrics(ViewW, ViewH, EditSession);
        const float ScreenCx   = InitialTileMetrics.TileMinX + InitialTileMetrics.TileSpanPx * 0.5f;
        const float ScreenCy   = InitialTileMetrics.TileMinY + InitialTileMetrics.TileSpanPx * 0.5f;

        // 8a. Cursor-anchored wheel zoom preserves document point under cursor
        PaneInputReading ZoomIn {};
        ZoomIn.PrimaryPointer = { ScreenCx + 40.0f, ScreenCy - 30.0f, false, false, false, true };
        ZoomIn.WheelDeltaY    = 2.0f;
        Interactor.ProcessViewportInput(ViewportInteractionReading{ CanvasBox, {}, ZoomIn });
        if (!VerifyCheck(EditSession.ZoomFraction > 1.05f, "Mouse wheel zooms vector canvas inward"))
        {
            std::filesystem::remove_all(ScratchDir);
            return 1;
        }

        // 8b. Pen tool authoring 3 knots into closed contour
        EditSession.ZoomFraction = 1.0f;
        EditSession.PanOffsetU   = 0.0f;
        EditSession.PanOffsetV   = 0.0f;
        EditSession.ActiveTool   = PatternStampToolCategory::Pen;
        const std::size_t LayersBeforePen = EditSession.Document.Layers.size();

        auto ClickCanvas = [&](float OffX, float OffY, bool Pressed, bool HeldDown)
        {
            PaneInputReading R {};
            R.PrimaryPointer = { ScreenCx + OffX, ScreenCy + OffY, HeldDown, Pressed, !HeldDown && !Pressed, true };
            Interactor.ProcessViewportInput(ViewportInteractionReading{ CanvasBox, {}, R });
        };
        ClickCanvas(-40.0f, -40.0f, true, true);
        ClickCanvas(-40.0f, -40.0f, false, false);
        ClickCanvas(40.0f, -40.0f, true, true);
        ClickCanvas(40.0f, -40.0f, false, false);
        ClickCanvas(0.0f, 45.0f, true, true);
        ClickCanvas(0.0f, 45.0f, false, false);
        ClickCanvas(-40.0f, -40.0f, true, true); // click near knot 0 closes contour

        if (!VerifyCheck(EditSession.Document.Layers.size() == LayersBeforePen + 1u, "Pen tool creates a new Bezier vector path layer") ||
            !VerifyCheck(EditSession.Document.Layers.back().ClosedCurveLoop && EditSession.Document.Layers.back().CubicSubpaths.front().Knots.size() == 3u, "Pen tool closes 3-knot Bezier contour when clicking origin knot"))
        {
            std::filesystem::remove_all(ScratchDir);
            return 1;
        }

        // 8c. Square shape creation drag
        EditSession.ActiveTool = PatternStampToolCategory::Square;
        ClickCanvas(-60.0f, -60.0f, true, true);
        ClickCanvas(50.0f, 30.0f, false, true);
        ClickCanvas(50.0f, 30.0f, false, false);
        if (!VerifyCheck(EditSession.Document.Layers.size() == LayersBeforePen + 2u, "Square tool drag creates new motif layer") ||
            !VerifyCheck(EditSession.Document.Layers.back().MotifShape == PatternStampMotifShape::Square && EditSession.Document.Layers.back().WidthPx > 20.0f, "Square motif layer sized from interactive drag"))
        {
            std::filesystem::remove_all(ScratchDir);
            return 1;
        }
    }

    // 9. Verify PatternStampWorkspaceHost + ContentBrowserPanel (Drop zones, Colour/Font/SVG drops, .VectorSpace save + GPU 128x128 thumbnail)
    {
        PatternStampWorkspaceHost Host(PatternStampWorkspaceMode::Pattern);
        Host.ConfigureRepositoryRoot(RepoRoot);
        Host.AttachVulkanRasterizer(&Rasterizer);

        ContentBrowserPanel BrowserPanel {};
        BrowserPanel.AccessThumbnailRepository().PopulateDefaultWorkspaceEntries();
        Host.AttachContentBrowserPanel(&BrowserPanel);

        const auto BrowserReading = BrowserPanel.InspectReading();
        if (!VerifyCheck(BrowserReading.ActiveWorkspace == WorkspaceCategory::Vector, "PatternStampWorkspaceHost locks ContentBrowser to Vector workspace") ||
            !VerifyCheck(ThumbnailSheetRepository::QueryCategoryPermitted(WorkspaceCategory::Vector, ContentCategory::Vectors), "Vector workspace permits Vectors category") ||
            !VerifyCheck(ThumbnailSheetRepository::QueryCategoryPermitted(WorkspaceCategory::Vector, ContentCategory::Fonts), "Vector workspace permits Fonts category") ||
            !VerifyCheck(ThumbnailSheetRepository::QueryCategoryPermitted(WorkspaceCategory::Vector, ContentCategory::Materials), "Vector workspace permits Materials category") ||
            !VerifyCheck(ThumbnailSheetRepository::QueryCategoryPermitted(WorkspaceCategory::Vector, ContentCategory::Textures), "Vector workspace permits Textures category") ||
            !VerifyCheck(!ThumbnailSheetRepository::QueryCategoryPermitted(WorkspaceCategory::Vector, ContentCategory::Geometry), "Vector workspace rejects irrelevant Geometry category"))
        {
            std::filesystem::remove_all(ScratchDir);
            return 1;
        }

        // Verify raw SVG file drop intake
        const std::filesystem::path SampleSvgPath = ScratchDir / "StarBadge.svg";
        {
            std::ofstream SvgOut(SampleSvgPath, std::ios::trunc);
            SvgOut << R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128"><circle cx="64" cy="64" r="44" fill="#d48850"/></svg>)svg";
        }
        std::string DropDiag {};
        const std::size_t LayersPreDrop = Host.InspectSession().Document.Layers.size();
        if (!VerifyCheck(Host.IntakeExternalFileDrop(SampleSvgPath, DropDiag, 256.0f, 256.0f), "External .svg file drop intakes cleanly into PatternStampWorkspaceHost") ||
            !VerifyCheck(Host.InspectSession().Document.Layers.size() > LayersPreDrop, "External .svg drop adds extracted editable vector layer(s)"))
        {
            std::filesystem::remove_all(ScratchDir);
            return 1;
        }

        // Verify .VectorSpace save registers project entry with GPU-rendered 128x128 thumbnail
        const std::filesystem::path ExportedVecPath = ScratchDir / "SavedHexPattern.VectorSpace";
        const std::size_t RepoCountBeforeSave = BrowserPanel.AccessThumbnailRepository().QueryAllEntries().size();
        if (!VerifyCheck(Host.SaveVectorSpaceToBrowser(ExportedVecPath, &DropDiag), "SaveVectorSpaceToBrowser writes .VectorSpace & GPU thumbnail") ||
            !VerifyCheck(std::filesystem::exists(ExportedVecPath), ".VectorSpace file exists on disk after save") ||
            !VerifyCheck(BrowserPanel.AccessThumbnailRepository().QueryAllEntries().size() == RepoCountBeforeSave + 1u, "Saved .VectorSpace registered inside ContentBrowser ThumbnailSheetRepository") ||
            !VerifyCheck(BrowserPanel.AccessThumbnailRepository().QueryAllEntries().back().TileResident, "Saved .VectorSpace entry has live GPU-baked thumbnail tile resident"))
        {
            std::filesystem::remove_all(ScratchDir);
            return 1;
        }

        // Verify ContentDropZone applies Font drop from ContentBrowser entries
        std::size_t FontIdx = SIZE_MAX;
        const auto AllEntries = BrowserPanel.AccessThumbnailRepository().QueryAllEntries();
        for (std::size_t E = 0u; E < AllEntries.size(); ++E)
        {
            if (static_cast<ContentCategory>(AllEntries[E].IndexHeader.CategoryCode) == ContentCategory::Fonts)
            {
                FontIdx = E;
                break;
            }
        }
        const std::size_t BeforeFontLayers = Host.InspectSession().Document.Layers.size();
        if (!VerifyCheck(FontIdx != SIZE_MAX, "ContentBrowser exposes stock Fonts in Vector workspace") ||
            !VerifyCheck(BrowserPanel.ApplyContentEntryToDropZone(FontIdx, "pattern.vector_canvas", { 600.0f, 400.0f }), "ContentBrowser Font drop applies cleanly to pattern.vector_canvas") ||
            !VerifyCheck(Host.InspectSession().Document.Layers.size() >= BeforeFontLayers, "Font drop updates/places Typographic layer") ||
            !VerifyCheck(BrowserPanel.ApplyColourIndexToSlot(0u, "pattern.layer_fill", { 600.0f, 400.0f }), "ContentBrowser Colour swatch drops cleanly onto pattern.layer_fill"))
        {
            std::filesystem::remove_all(ScratchDir);
            return 1;
        }

        const std::filesystem::path PngOut = ScratchDir / "ExportCheck.png";
        const std::filesystem::path SvgOut = ScratchDir / "ExportCheck.svg";
        if (!VerifyCheck(Host.ExportDocumentToPngFile(PngOut, &DropDiag) && std::filesystem::file_size(PngOut) > 100u, "ExportDocumentToPngFile writes non-empty PNG via GPU rasterizer") ||
            !VerifyCheck(Host.ExportDocumentToSvgFile(SvgOut, &DropDiag) && std::filesystem::file_size(SvgOut) > 50u, "ExportDocumentToSvgFile writes SVG XML from live layer stack"))
        {
            std::filesystem::remove_all(ScratchDir);
            return 1;
        }
    }

    std::error_code CleanupEc {};
    std::filesystem::remove_all(ScratchDir, CleanupEc);

    std::printf(
        "[VectorGraphicsGPUValidation] PASSED (Instances=%u, CenterPickLayer=%u, CenterPickGizmo=%u, ScaleRange=[%.2f, %.2f])\n",
        EvaluatedMetrics.TotalInstancesEvaluated,
        CenterPick.InstanceSlot,
        CenterPick.GizmoHandleOrdinal,
        EvaluatedMetrics.MinNoiseScale,
        EvaluatedMetrics.MaxNoiseScale);
    return 0;
}
