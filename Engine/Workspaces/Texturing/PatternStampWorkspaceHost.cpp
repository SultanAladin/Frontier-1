/*============================================================================================================================================
                                                        PATTERNSTAMPWORKSPACEHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of shared 3-column Pattern Studio & Stamp Studio workspace host + Vulkan VectorCanvasRasterizer + ContentBrowser bridge.

#include "Engine/Workspaces/Texturing/PatternStampWorkspaceHost.h"

#include "Engine/Codecs/FontCodec.h"
#include "Engine/Codecs/VectorCodec.h"
#include "Engine/GeometricRaster/Vector/VulkanVectorRenderMethod.h"
#include "Engine/Input/InputExtension.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <vector>

namespace Frontier::Workspaces
{

namespace
{

[[nodiscard]] std::string NormalizeLowerFileSuffix(const std::filesystem::path& FilePath)
{
    std::string Ext = FilePath.extension().string();
    for (char& Ch : Ext)
    {
        Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
    }
    return Ext;
}

void AppendBigEndian32(std::vector<uint8_t>& Buffer, uint32_t Word)
{
    Buffer.push_back(static_cast<uint8_t>((Word >> 24u) & 0xFFu));
    Buffer.push_back(static_cast<uint8_t>((Word >> 16u) & 0xFFu));
    Buffer.push_back(static_cast<uint8_t>((Word >> 8u) & 0xFFu));
    Buffer.push_back(static_cast<uint8_t>(Word & 0xFFu));
}

[[nodiscard]] uint32_t ComputeAdler32(const std::vector<uint8_t>& Bytes) noexcept
{
    uint32_t S1 = 1u;
    uint32_t S2 = 0u;
    for (const uint8_t Byte : Bytes)
    {
        S1 = (S1 + Byte) % 65521u;
        S2 = (S2 + S1) % 65521u;
    }
    return (S2 << 16u) | S1;
}

[[nodiscard]] uint32_t ComputeCrc32(const uint8_t* Bytes, std::size_t Length) noexcept
{
    uint32_t Crc = 0xFFFFFFFFu;
    for (std::size_t Idx = 0u; Idx < Length; ++Idx)
    {
        Crc ^= Bytes[Idx];
        for (uint32_t Bit = 0u; Bit < 8u; ++Bit)
        {
            Crc = (Crc & 1u) != 0u ? ((Crc >> 1u) ^ 0xEDB88320u) : (Crc >> 1u);
        }
    }
    return ~Crc;
}

void AppendPngChunk(std::vector<uint8_t>& Stream, const char Type[4], const std::vector<uint8_t>& Body)
{
    AppendBigEndian32(Stream, static_cast<uint32_t>(Body.size()));
    const std::size_t TypeStart = Stream.size();
    Stream.insert(Stream.end(), Type, Type + 4);
    Stream.insert(Stream.end(), Body.begin(), Body.end());
    AppendBigEndian32(Stream, ComputeCrc32(Stream.data() + TypeStart, 4u + Body.size()));
}

[[nodiscard]] bool WriteLosslessRgbaPng(
    const std::filesystem::path& OutputPath,
    const std::vector<uint8_t>&  RgbaPixels,
    uint32_t                     WidthPx,
    uint32_t                     HeightPx)
{
    if (WidthPx == 0u || HeightPx == 0u || RgbaPixels.size() < static_cast<std::size_t>(WidthPx) * HeightPx * 4u)
    {
        return false;
    }
    if (OutputPath.has_parent_path())
    {
        std::error_code Ec {};
        std::filesystem::create_directories(OutputPath.parent_path(), Ec);
    }

    std::vector<uint8_t> RawScanlines {};
    RawScanlines.reserve(static_cast<std::size_t>(HeightPx) * (1u + static_cast<std::size_t>(WidthPx) * 4u));
    for (uint32_t Row = 0u; Row < HeightPx; ++Row)
    {
        RawScanlines.push_back(0u);
        const std::size_t Start = static_cast<std::size_t>(Row) * WidthPx * 4u;
        RawScanlines.insert(
            RawScanlines.end(),
            RgbaPixels.begin() + static_cast<std::ptrdiff_t>(Start),
            RgbaPixels.begin() + static_cast<std::ptrdiff_t>(Start + WidthPx * 4u));
    }

    std::vector<uint8_t> Zlib { 0x78u, 0x01u };
    std::size_t          Offset = 0u;
    while (Offset < RawScanlines.size())
    {
        const uint16_t BlockLen = static_cast<uint16_t>(std::min<std::size_t>(RawScanlines.size() - Offset, 65535u));
        const bool     Last     = (Offset + BlockLen == RawScanlines.size());
        const uint16_t Neg      = static_cast<uint16_t>(~BlockLen);
        Zlib.push_back(Last ? 0x01u : 0x00u);
        Zlib.push_back(static_cast<uint8_t>(BlockLen & 0xFFu));
        Zlib.push_back(static_cast<uint8_t>((BlockLen >> 8u) & 0xFFu));
        Zlib.push_back(static_cast<uint8_t>(Neg & 0xFFu));
        Zlib.push_back(static_cast<uint8_t>((Neg >> 8u) & 0xFFu));
        Zlib.insert(Zlib.end(), RawScanlines.begin() + static_cast<std::ptrdiff_t>(Offset), RawScanlines.begin() + static_cast<std::ptrdiff_t>(Offset + BlockLen));
        Offset += BlockLen;
    }
    AppendBigEndian32(Zlib, ComputeAdler32(RawScanlines));

    std::vector<uint8_t> Png { 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u };
    std::vector<uint8_t> Ihdr {};
    AppendBigEndian32(Ihdr, WidthPx);
    AppendBigEndian32(Ihdr, HeightPx);
    Ihdr.insert(Ihdr.end(), { 8u, 6u, 0u, 0u, 0u });
    AppendPngChunk(Png, "IHDR", Ihdr);
    AppendPngChunk(Png, "IDAT", Zlib);
    AppendPngChunk(Png, "IEND", {});

    std::ofstream Out(OutputPath, std::ios::binary | std::ios::trunc);
    if (!Out)
    {
        return false;
    }
    Out.write(reinterpret_cast<const char*>(Png.data()), static_cast<std::streamsize>(Png.size()));
    return Out.good();
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

PatternStampWorkspaceHost::PatternStampWorkspaceHost(PatternStampWorkspaceMode InitialMode)
    : Session(InitialMode)
{
    LayerStackPane.AttachSession(&Session);
    ToolHeaderPane.AttachSession(&Session);
    InspectorPane.AttachSession(&Session);
    BindPaneCallbacks();
}

void PatternStampWorkspaceHost::BindPaneCallbacks()
{
    ToolHeaderPane.AssignBridgeCallback(
        [this](std::string_view CommandTag)
        {
            ExecuteCanvasCommandBridge(CommandTag);
        });

    ToolHeaderPane.AssignCanvasBoundsCallback(
        [this](Rectangle2D CanvasWindowLogicalBounds)
        {
            SynchronizeDropTargetBounds(CanvasWindowLogicalBounds);
        });

    ToolHeaderPane.AccessInteractor().AssignSvgPasteRequestCallback(
        [this](std::string_view ClipboardText, float DropDocX, float DropDocY) -> bool
        {
            std::string Diag {};
            if (Session.ImportSvgIntoSession(ClipboardText, false, DropDocX, DropDocY, "Clipboard SVG", 0u, &Diag))
            {
                Session.StatusNotice = "Pasted clipboard SVG vector";
                return true;
            }
            if (!Diag.empty())
            {
                Session.StatusNotice = Diag;
            }
            return false;
        });
}

void PatternStampWorkspaceHost::ConfigureMode(PatternStampWorkspaceMode TargetMode) noexcept
{
    Session.ConfigureMode(TargetMode);
    LayerStackPane.AttachSession(&Session);
    ToolHeaderPane.AttachSession(&Session);
    InspectorPane.AttachSession(&Session);
    ToolHeaderPane.AttachVulkanRasterizer(BoundRasterizer);
    if (BoundBrowserPanel != nullptr)
    {
        AttachContentBrowserPanel(BoundBrowserPanel);
    }
}

void PatternStampWorkspaceHost::ConfigureLaunchPolicy(const Configuration::EngineConfiguration& Config) noexcept
{
    ActiveEngineConfig = Config;
}

void PatternStampWorkspaceHost::ConfigureRepositoryRoot(const std::filesystem::path& RepoRoot) noexcept
{
    RepositoryRootPath = RepoRoot;
}

void PatternStampWorkspaceHost::AttachVulkanRasterizer(GeometricRaster::VectorCanvasRasterizer* TargetRasterizer) noexcept
{
    BoundRasterizer = TargetRasterizer;
    ToolHeaderPane.AttachVulkanRasterizer(TargetRasterizer);
    if (BoundBrowserPanel != nullptr && BoundRasterizer != nullptr)
    {
        PopulateBrowserStarterPresets();
    }
}

void PatternStampWorkspaceHost::PopulateBrowserStarterPresets()
{
    if (BoundBrowserPanel == nullptr)
    {
        return;
    }

    const auto& Presets = Session.QueryPresetDirectory();
    for (std::size_t Idx = 0u; Idx < Presets.size(); ++Idx)
    {
        const auto& Preset = Presets[Idx];
        std::vector<uint8_t> ThumbRgba {};
        if (BoundRasterizer != nullptr)
        {
            PatternStampWorkspaceSession PresetPreviewSession(Session.Mode);
            PresetPreviewSession.LoadPresetByIndex(Idx);
            (void)BoundRasterizer->RenderThumbnailRGBA(128u, 128u, PresetPreviewSession, ThumbRgba);
        }
        const std::filesystem::path VirtualPresetPath =
            std::filesystem::path((Session.Mode == PatternStampWorkspaceMode::Pattern) ? "preset://pattern" : "preset://stamp") /
            std::to_string(Idx);
        (void)BoundBrowserPanel->AccessThumbnailRepository().RegisterOrUpdateProjectEntry(
            "Preset · " + Preset.Title,
            ContentCategory::Vectors,
            Preset.SwatchA,
            VirtualPresetPath,
            ThumbRgba);
    }
}

void PatternStampWorkspaceHost::AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel)
{
    BoundBrowserPanel = TargetPanel;
    if (TargetPanel == nullptr)
    {
        return;
    }

    TargetPanel->ConfigureWorkspaceDomain(WorkspaceCategory::Vector);

    const Rectangle2D InitialCanvasBounds = ToolHeaderPane.QueryCanvasWindowLogicalBounds();

    TargetPanel->ClearColourSlots();
    const char* SlotId    = (Session.Mode == PatternStampWorkspaceMode::Pattern) ? "pattern.layer_fill" : "stamp.layer_fill";
    const char* SlotTitle = (Session.Mode == PatternStampWorkspaceMode::Pattern) ? "Pattern Vector Fill" : "Stamp Vector Fill";
    TargetPanel->RegisterColourSlot(ColourSlotRegistration{
        SlotId,
        SlotTitle,
        InitialCanvasBounds,
        nullptr,
        [this](ColorRgba AssignedRgba, const ColourSwatchEntry&)
        {
            if (Session.ApplyColourDrop(AssignedRgba, 0u, Session.ActivePointPinIndex, false))
            {
                Session.StatusNotice = "Applied colour swatch";
            }
        },
        [this](ColorRgba AssignedRgba, const ColourSwatchEntry& Entry, Point2D DropLogicalPos)
        {
            AssignBrowserColourDrop(AssignedRgba, Entry, DropLogicalPos);
        },
    });
    TargetPanel->FocusColourSlot(SlotId);

    TargetPanel->ClearContentDropZones();
    const char* DropZoneId    = (Session.Mode == PatternStampWorkspaceMode::Pattern) ? "pattern.vector_canvas" : "stamp.vector_canvas";
    const char* DropZoneTitle = (Session.Mode == PatternStampWorkspaceMode::Pattern) ? "Pattern Vector Canvas" : "Stamp Vector Canvas";
    TargetPanel->RegisterContentDropZone(ContentDropZoneRegistration{
        DropZoneId,
        DropZoneTitle,
        InitialCanvasBounds,
        ContentCategory::All,
        [this](const ContentRepositoryEntry& Entry, Point2D DropLogicalPos)
        {
            ApplyBrowserContentDrop(Entry, DropLogicalPos);
        },
    });

    TargetPanel->AssignContentActivationCallback(
        [this](const ContentRepositoryEntry& Entry)
        {
            ActivateBrowserEntry(Entry);
        });

    PopulateBrowserStarterPresets();
}

#if FRONTIER_EDITOR_ENABLED
void PatternStampWorkspaceHost::AttachContentBrowser(Editor::Explorer::ContentBrowser* TargetBrowser)
{
    BoundBrowser = TargetBrowser;
    AttachContentBrowserPanel(TargetBrowser != nullptr ? &TargetBrowser->AccessPanel() : nullptr);
}
#endif

void PatternStampWorkspaceHost::SynchronizeDropTargetBounds(Rectangle2D CanvasWindowLogicalBounds) noexcept
{
    if (BoundBrowserPanel == nullptr || CanvasWindowLogicalBounds.Width < 16.0f || CanvasWindowLogicalBounds.Height < 16.0f)
    {
        return;
    }
    const char* ColourSlotId = (Session.Mode == PatternStampWorkspaceMode::Pattern) ? "pattern.layer_fill" : "stamp.layer_fill";
    const char* DropZoneId   = (Session.Mode == PatternStampWorkspaceMode::Pattern) ? "pattern.vector_canvas" : "stamp.vector_canvas";
    BoundBrowserPanel->UpdateColourSlotBounds(ColourSlotId, CanvasWindowLogicalBounds);
    BoundBrowserPanel->UpdateContentDropZoneBounds(DropZoneId, CanvasWindowLogicalBounds);
}

void PatternStampWorkspaceHost::ResolveCanvasDropPick(
    Point2D   DropLogicalPos,
    float&    OutDocX,
    float&    OutDocY,
    uint32_t& OutPickedLayerId) const
{
    OutDocX          = 256.0f;
    OutDocY          = 256.0f;
    OutPickedLayerId = 0u;

    const Rectangle2D CanvasBounds = ToolHeaderPane.QueryCanvasWindowLogicalBounds();
    if (CanvasBounds.Width < 16.0f || CanvasBounds.Height < 16.0f)
    {
        return;
    }

    const float LocalX = DropLogicalPos.X - CanvasBounds.X;
    const float LocalY = DropLogicalPos.Y - CanvasBounds.Y;
    const uint32_t ViewW = static_cast<uint32_t>(std::max(1.0f, std::round(CanvasBounds.Width)));
    const uint32_t ViewH = static_cast<uint32_t>(std::max(1.0f, std::round(CanvasBounds.Height)));

    const auto Metrics = GeometricRaster::VectorCanvasRasterizer::ComputeTileLayoutMetrics(ViewW, ViewH, Session);
    GeometricRaster::VectorCanvasRasterizer::ConvertScreenToDocumentPx(Metrics, LocalX, LocalY, OutDocX, OutDocY);

    if (BoundRasterizer != nullptr && CanvasBounds.Contains(DropLogicalPos))
    {
        const int32_t PxX = std::clamp(static_cast<int32_t>(std::round(LocalX)), 0, static_cast<int32_t>(ViewW) - 1);
        const int32_t PxY = std::clamp(static_cast<int32_t>(std::round(LocalY)), 0, static_cast<int32_t>(ViewH) - 1);
        const auto Hit    = BoundRasterizer->Pick2DPixel(PxX, PxY);
        OutPickedLayerId  = Hit.InstanceSlot;
    }
}

void PatternStampWorkspaceHost::AssignBrowserColourDrop(
    ColorRgba              AssignedRgba,
    const ColourSwatchEntry& Entry,
    Point2D                DropLogicalPos)
{
    const auto& Snapshot     = Input::InputExtension::InspectActiveEditorSnapshot();
    const bool  StrokeTarget = Snapshot.AltHeld;
    const Rectangle2D CanvasBounds = ToolHeaderPane.QueryCanvasWindowLogicalBounds();

    uint32_t TargetLayerId = 0u;
    if (DropLogicalPos.X >= 0.0f && CanvasBounds.Contains(DropLogicalPos))
    {
        float DocX = 256.0f;
        float DocY = 256.0f;
        ResolveCanvasDropPick(DropLogicalPos, DocX, DocY, TargetLayerId);
        if (TargetLayerId == 0u)
        {
            // Dropped on empty canvas background: assign ground fill tint
            Session.PushUndoSnapshot();
            Session.Document.GroundEnabled = true;
            Session.Document.GroundTintA   = AssignedRgba;
            Session.BumpRevision();
            Session.RecomputeSummaryMetrics();
            Session.StatusNotice = "Applied " + Entry.AliasName + " to tile ground";
            return;
        }
    }

    if (Session.ApplyColourDrop(AssignedRgba, TargetLayerId, Session.ActivePointPinIndex, StrokeTarget))
    {
        Session.StatusNotice = "Applied " + Entry.AliasName + (StrokeTarget ? " to stroke" : " to vector fill");
    }
}

void PatternStampWorkspaceHost::ApplyBrowserContentDrop(
    const ContentRepositoryEntry& Entry,
    Point2D                       DropLogicalPos)
{
    float    DocX          = 256.0f;
    float    DocY          = 256.0f;
    uint32_t PickedLayerId = 0u;
    ResolveCanvasDropPick(DropLogicalPos, DocX, DocY, PickedLayerId);

    const auto Cat = static_cast<ContentCategory>(Entry.IndexHeader.CategoryCode);
    if (Cat == ContentCategory::Fonts)
    {
        std::filesystem::path ResolvedFont = Entry.DiskPath;
        if (ResolvedFont.is_relative() && !RepositoryRootPath.empty() && std::filesystem::exists(RepositoryRootPath / ResolvedFont))
        {
            ResolvedFont = RepositoryRootPath / ResolvedFont;
        }
        if (BoundRasterizer != nullptr && std::filesystem::exists(ResolvedFont))
        {
            const std::string Ext = NormalizeLowerFileSuffix(ResolvedFont);
            if (Ext == ".fontspace")
            {
                Codecs::FontSpaceDocumentModel FontDoc {};
                std::string FontDiag {};
                if (Codecs::FontDecoder::DecodeFromFile(ResolvedFont, FontDoc, FontDiag))
                {
                    std::error_code FontEc {};
                    for (const auto& FontCandidate : std::filesystem::directory_iterator(ResolvedFont.parent_path(), FontEc))
                    {
                        const std::string CandidateExt = NormalizeLowerFileSuffix(FontCandidate.path());
                        if (FontCandidate.is_regular_file(FontEc) && (CandidateExt == ".ttf" || CandidateExt == ".otf"))
                        {
                            ResolvedFont = FontCandidate.path();
                            break;
                        }
                    }
                }
            }
            (void)GeometricRaster::ThorVGVectorFrontend::RegisterTypefaceFile(ResolvedFont);
        }
        if (Session.ApplyFontDrop(ResolvedFont, Entry.DisplayTitle, DocX, DocY, PickedLayerId))
        {
            Session.StatusNotice = "Applied typeface " + Entry.DisplayTitle;
        }
        return;
    }

    if (Cat == ContentCategory::Vectors)
    {
        const std::string PathStr = Entry.DiskPath.generic_string();
        if (PathStr.starts_with("preset://"))
        {
            const std::size_t SlashPos = PathStr.find_last_of('/');
            if (SlashPos != std::string::npos && SlashPos + 1u < PathStr.size())
            {
                const std::size_t PresetIdx = static_cast<std::size_t>(std::strtoull(PathStr.c_str() + SlashPos + 1u, nullptr, 10));
                Session.LoadPresetByIndex(PresetIdx);
                ToolHeaderPane.AccessInteractor().SynchronizeGizmoAnchor();
                Session.StatusNotice = "Loaded starter preset: " + Session.Document.DocumentName;
                return;
            }
        }
        const std::string Ext = NormalizeLowerFileSuffix(Entry.DiskPath);
        if (Ext == ".vectorspace" && std::filesystem::exists(Entry.DiskPath))
        {
            std::string LoadDiag {};
            if (!LoadVectorSpaceFile(Entry.DiskPath, &LoadDiag))
            {
                Session.StatusNotice = LoadDiag;
            }
            return;
        }
        if (!Entry.DiskPath.empty() && std::filesystem::exists(Entry.DiskPath))
        {
            std::string SvgDiag {};
            if (Session.ImportSvgIntoSession(Entry.DiskPath.string(), true, DocX, DocY, Entry.DisplayTitle, PickedLayerId, &SvgDiag))
            {
                Session.StatusNotice = "Dropped vector " + Entry.DisplayTitle;
                return;
            }
        }
        // Procedural stock vector fallback when DiskPath is not an external file yet
        Session.AddLayer(PatternStampLayerCategory::Motif);
        if (PatternStampLayerSpec* Created = Session.InspectSelectedLayer())
        {
            Created->Name     = Entry.DisplayTitle;
            Created->CenterX  = DocX;
            Created->CenterY  = DocY;
            Created->FillTint = ColorRgba{
                Entry.IndexHeader.TintRed,
                Entry.IndexHeader.TintGreen,
                Entry.IndexHeader.TintBlue,
                255u };
            Created->CubicSubpaths.clear();
            PatternStampWorkspaceSession::SeedMotifCubicSubpathsIfEmpty(*Created);
            Session.BumpRevision();
            Session.RecomputeSummaryMetrics();
            Session.StatusNotice = "Placed vector motif " + Entry.DisplayTitle;
        }
        return;
    }

    if (Cat == ContentCategory::Materials || Cat == ContentCategory::Textures)
    {
        PatternStampLayerSpec* Target = (PickedLayerId != 0u) ? Session.FindLayerById(PickedLayerId) : Session.InspectSelectedLayer();
        if (Target == nullptr)
        {
            Session.AddLayer(PatternStampLayerCategory::Motif);
            Target = Session.InspectSelectedLayer();
            if (Target != nullptr)
            {
                Target->CenterX = DocX;
                Target->CenterY = DocY;
            }
        }
        if (Target != nullptr)
        {
            Session.PushUndoSnapshot();
            Target->FillTint = ColorRgba{
                Entry.IndexHeader.TintRed,
                Entry.IndexHeader.TintGreen,
                Entry.IndexHeader.TintBlue,
                255u };
            const auto& FinishLabels = PatternStampWorkspaceSession::InspectFinishLabels();
            for (std::size_t F = 0u; F < FinishLabels.size(); ++F)
            {
                if (Entry.DisplayTitle.find(FinishLabels[F]) != std::string::npos)
                {
                    Target->FinishPreset = static_cast<PatternStampFinishPreset>(F);
                    break;
                }
            }
            Session.BumpRevision();
            Session.RecomputeSummaryMetrics();
            Session.StatusNotice = "Applied surface preset " + Entry.DisplayTitle;
        }
        return;
    }
}

void PatternStampWorkspaceHost::ActivateBrowserEntry(const ContentRepositoryEntry& Entry)
{
    ApplyBrowserContentDrop(Entry, ToolHeaderPane.QueryCanvasWindowLogicalBounds().Center());
}

void PatternStampWorkspaceHost::ExecuteCanvasCommandBridge(std::string_view CommandTag)
{
    std::string Diag {};
    if (CommandTag == "save.vectorspace")
    {
        if (!SaveVectorSpaceToBrowser({}, &Diag) && !Diag.empty())
        {
            Session.StatusNotice = Diag;
        }
    }
    else if (CommandTag == "save.svg")
    {
        if (!ExportDocumentToSvgFile({}, &Diag) && !Diag.empty())
        {
            Session.StatusNotice = Diag;
        }
    }
    else if (CommandTag == "save.png")
    {
        if (!ExportDocumentToPngFile({}, &Diag) && !Diag.empty())
        {
            Session.StatusNotice = Diag;
        }
    }
    else if (CommandTag == "save.pbr")
    {
        const bool OkVec = SaveVectorSpaceToBrowser({}, &Diag);
        const bool OkSvg = ExportDocumentToSvgFile({}, &Diag);
        const bool OkPng = ExportDocumentToPngFile({}, &Diag);
        if (OkVec && OkSvg && OkPng)
        {
            Session.StatusNotice = "Exported .VectorSpace + SVG + RGBA raster";
        }
    }
    else if (CommandTag == "save.open")
    {
        if (BoundBrowserPanel != nullptr)
        {
            (void)BoundBrowserPanel->ActivateCategory(ContentCategory::Vectors);
            BoundBrowserPanel->ConfigureShelfExtent(ContentShelfExtent::SplitHalf, false);
            Session.StatusNotice = "Select or drag any .VectorSpace, .svg, or starter preset from ContentBrowser";
        }
    }
    else if (CommandTag == "bridge.material" || CommandTag == "bridge.paint_pattern" || CommandTag == "bridge.paint_stamp")
    {
        const std::filesystem::path PngOut = ResolveWorkspaceVectorDirectory() / (BuildSanitizedDocumentStem() + ".png");
        const bool OkVec = SaveVectorSpaceToBrowser({}, &Diag);
        const bool OkPng = ExportDocumentToPngFile(PngOut, &Diag);
        if (OkPng && BoundBrowserPanel != nullptr)
        {
            std::vector<uint8_t> ThumbRgba {};
            if (BoundRasterizer != nullptr)
            {
                (void)BoundRasterizer->RenderThumbnailRGBA(128u, 128u, Session, ThumbRgba);
            }
            ColorRgba PreviewTint = Session.Document.GroundEnabled
                ? Session.Document.GroundTintA
                : ColorRgba{ 239u, 244u, 207u, 255u };
            if (const auto* Sel = Session.InspectSelectedLayer())
            {
                PreviewTint = Sel->FillTint;
            }
            (void)BoundBrowserPanel->AccessThumbnailRepository().RegisterOrUpdateProjectEntry(
                Session.Document.DocumentName,
                ContentCategory::Textures,
                PreviewTint,
                PngOut,
                ThumbRgba);
        }
        if (OkVec && OkPng)
        {
            Session.StatusNotice = "Published " + Session.Document.DocumentName + " (.VectorSpace + RGBA Texture) to ContentBrowser";
        }
        else if (OkVec)
        {
            Session.StatusNotice = "Saved " + Session.Document.DocumentName + " (.VectorSpace) to ContentBrowser";
        }
    }
}

bool PatternStampWorkspaceHost::IntakeExternalFileDrop(
    const std::filesystem::path& DroppedPath,
    std::string&                 OutDiagnostic,
    float                        DropDocX,
    float                        DropDocY)
{
    std::error_code Ec {};
    if (!std::filesystem::exists(DroppedPath, Ec) || std::filesystem::is_directory(DroppedPath, Ec))
    {
        OutDiagnostic = "Dropped path does not exist: " + DroppedPath.string();
        return false;
    }

    const std::string Ext = NormalizeLowerFileSuffix(DroppedPath);
    if (Ext == ".vectorspace")
    {
        if (BoundBrowserPanel != nullptr)
        {
            (void)BoundBrowserPanel->IntakeExternalAssetDrop(DroppedPath);
        }
        return LoadVectorSpaceFile(DroppedPath, &OutDiagnostic);
    }

    if (Ext == ".svg")
    {
        if (BoundBrowserPanel != nullptr)
        {
            (void)BoundBrowserPanel->IntakeExternalAssetDrop(DroppedPath);
        }
        const bool Ok = Session.ImportSvgIntoSession(
            DroppedPath.string(),
            true,
            DropDocX,
            DropDocY,
            DroppedPath.stem().string(),
            0u,
            &OutDiagnostic);
        if (Ok)
        {
            Session.StatusNotice = "Imported SVG: " + DroppedPath.filename().string();
        }
        return Ok;
    }

    if (Ext == ".ttf" || Ext == ".otf" || Ext == ".ttc" || Ext == ".fontspace")
    {
        std::filesystem::path TypefaceFile = DroppedPath;
        if (Ext == ".fontspace")
        {
            Codecs::FontSpaceDocumentModel FontDoc {};
            if (Codecs::FontDecoder::DecodeFromFile(DroppedPath, FontDoc, OutDiagnostic))
            {
                for (const auto& FontCandidate : std::filesystem::directory_iterator(DroppedPath.parent_path(), Ec))
                {
                    const std::string CandidateExt = NormalizeLowerFileSuffix(FontCandidate.path());
                    if (FontCandidate.is_regular_file(Ec) && (CandidateExt == ".ttf" || CandidateExt == ".otf"))
                    {
                        TypefaceFile = FontCandidate.path();
                        break;
                    }
                }
            }
        }
        (void)GeometricRaster::ThorVGVectorFrontend::RegisterTypefaceFile(TypefaceFile);
        if (BoundBrowserPanel != nullptr)
        {
            (void)BoundBrowserPanel->IntakeExternalAssetDrop(DroppedPath);
        }
        const bool Ok = Session.ApplyFontDrop(TypefaceFile, DroppedPath.stem().string(), DropDocX, DropDocY, 0u);
        OutDiagnostic = Ok ? ("Applied font: " + DroppedPath.filename().string()) : "Failed to apply dropped font";
        return Ok;
    }

    if (BoundBrowserPanel != nullptr)
    {
        const auto Intake = BoundBrowserPanel->IntakeExternalAssetDrop(DroppedPath);
        OutDiagnostic = Intake.DiagnosticMessage;
        return Intake.Accepted;
    }

    OutDiagnostic = "Unsupported dropped vector asset extension: " + Ext;
    return false;
}

std::filesystem::path PatternStampWorkspaceHost::ResolveWorkspaceVectorDirectory() const
{
    if (BoundBrowserPanel != nullptr)
    {
        const auto& Policy = BoundBrowserPanel->AccessThumbnailRepository().QueryImportPolicy();
        if (!Policy.ActiveProjectRoot.empty())
        {
            return Policy.ActiveProjectRoot / "Vectors";
        }
        if (!Policy.ScratchSessionRoot.empty())
        {
            return Policy.ScratchSessionRoot / "Vectors";
        }
    }
    if (!RepositoryRootPath.empty())
    {
        return RepositoryRootPath / "ProjectContent/Vectors";
    }
    return std::filesystem::current_path() / "Saved/Vectors";
}

std::string PatternStampWorkspaceHost::BuildSanitizedDocumentStem() const
{
    std::string Stem {};
    for (const char Ch : Session.Document.DocumentName)
    {
        if (std::isalnum(static_cast<unsigned char>(Ch)) || Ch == '_' || Ch == '-')
        {
            Stem.push_back(Ch);
        }
        else if (Ch == ' ' && (Stem.empty() || Stem.back() != '_'))
        {
            Stem.push_back('_');
        }
    }
    if (Stem.empty())
    {
        Stem = (Session.Mode == PatternStampWorkspaceMode::Pattern) ? "PatternTile" : "StampVector";
    }
    return Stem;
}

bool PatternStampWorkspaceHost::SaveVectorSpaceToBrowser(
    const std::filesystem::path& TargetPath,
    std::string*                 OptionalDiagnostic)
{
    std::filesystem::path OutPath = TargetPath;
    if (OutPath.empty())
    {
        OutPath = ResolveWorkspaceVectorDirectory() / (BuildSanitizedDocumentStem() + ".VectorSpace");
    }
    if (OutPath.has_parent_path())
    {
        std::error_code Ec {};
        std::filesystem::create_directories(OutPath.parent_path(), Ec);
    }

    std::string LocalDiag {};
    const auto  Model = Session.BuildVectorSpaceModel();
    if (!Codecs::VectorEncoder::EncodeToFile(OutPath, Model, LocalDiag))
    {
        if (OptionalDiagnostic != nullptr) *OptionalDiagnostic = LocalDiag;
        return false;
    }

    std::vector<uint8_t> ThumbRgba {};
    if (BoundRasterizer != nullptr)
    {
        (void)BoundRasterizer->RenderThumbnailRGBA(128u, 128u, Session, ThumbRgba);
    }

    ColorRgba PreviewTint = Session.Document.GroundEnabled
        ? Session.Document.GroundTintA
        : ColorRgba{ 239u, 244u, 207u, 255u };
    if (const auto* Sel = Session.InspectSelectedLayer())
    {
        PreviewTint = Sel->FillTint;
    }

    if (BoundBrowserPanel != nullptr)
    {
        (void)BoundBrowserPanel->AccessThumbnailRepository().RegisterOrUpdateProjectEntry(
            Session.Document.DocumentName,
            ContentCategory::Vectors,
            PreviewTint,
            OutPath,
            ThumbRgba);
    }

    Session.StatusNotice = "Saved " + OutPath.filename().string();
    if (OptionalDiagnostic != nullptr) *OptionalDiagnostic = Session.StatusNotice;
    return true;
}

bool PatternStampWorkspaceHost::LoadVectorSpaceFile(
    const std::filesystem::path& SourcePath,
    std::string*                 OptionalDiagnostic)
{
    std::string LocalDiag {};
    Codecs::VectorSpaceDocumentModel Model {};
    if (!Codecs::VectorDecoder::DecodeFromFile(SourcePath, Model, LocalDiag))
    {
        if (OptionalDiagnostic != nullptr) *OptionalDiagnostic = LocalDiag;
        return false;
    }
    if (!Session.LoadFromVectorSpaceModel(Model))
    {
        if (OptionalDiagnostic != nullptr) *OptionalDiagnostic = "Failed to hydrate Pattern/Stamp session from " + SourcePath.string();
        return false;
    }
    Session.StatusNotice = "Loaded " + SourcePath.filename().string();
    if (OptionalDiagnostic != nullptr) *OptionalDiagnostic = Session.StatusNotice;
    return true;
}

bool PatternStampWorkspaceHost::ExportDocumentToSvgFile(
    const std::filesystem::path& TargetPath,
    std::string*                 OptionalDiagnostic) const
{
    std::filesystem::path OutPath = TargetPath;
    if (OutPath.empty())
    {
        OutPath = ResolveWorkspaceVectorDirectory() / (BuildSanitizedDocumentStem() + ".svg");
    }
    if (OutPath.has_parent_path())
    {
        std::error_code Ec {};
        std::filesystem::create_directories(OutPath.parent_path(), Ec);
    }

    const std::string SvgText = Session.ExportDocumentToSvgString();
    std::ofstream Out(OutPath, std::ios::binary | std::ios::trunc);
    if (!Out)
    {
        if (OptionalDiagnostic != nullptr) *OptionalDiagnostic = "Cannot open SVG export path: " + OutPath.string();
        return false;
    }
    Out.write(SvgText.data(), static_cast<std::streamsize>(SvgText.size()));
    if (!Out.good())
    {
        if (OptionalDiagnostic != nullptr) *OptionalDiagnostic = "Failed writing SVG export file: " + OutPath.string();
        return false;
    }
    if (OptionalDiagnostic != nullptr) *OptionalDiagnostic = "Exported " + OutPath.filename().string();
    return true;
}

bool PatternStampWorkspaceHost::ExportDocumentToPngFile(
    const std::filesystem::path& TargetPath,
    std::string*                 OptionalDiagnostic)
{
    if (BoundRasterizer == nullptr)
    {
        if (OptionalDiagnostic != nullptr) *OptionalDiagnostic = "PNG export requires an initialized Vulkan VectorCanvasRasterizer";
        return false;
    }

    std::filesystem::path OutPath = TargetPath;
    if (OutPath.empty())
    {
        OutPath = ResolveWorkspaceVectorDirectory() / (BuildSanitizedDocumentStem() + ".png");
    }

    const uint32_t ExportRes = std::clamp(Session.ResolutionPx, 128u, 2048u);
    PatternStampWorkspaceSession ExportSession = Session;
    ExportSession.ZoomFraction = 1.0f;
    ExportSession.PanOffsetU   = 0.0f;
    ExportSession.PanOffsetV   = 0.0f;
    std::vector<uint8_t> Pixels {};
    if (!BoundRasterizer->RenderThumbnailRGBA(ExportRes, ExportRes, ExportSession, Pixels) ||
        !WriteLosslessRgbaPng(OutPath, Pixels, ExportRes, ExportRes))
    {
        if (OptionalDiagnostic != nullptr) *OptionalDiagnostic = "Failed exporting RGBA PNG: " + OutPath.string();
        return false;
    }

    Session.StatusNotice = "Exported " + OutPath.filename().string();
    if (OptionalDiagnostic != nullptr) *OptionalDiagnostic = Session.StatusNotice;
    return true;
}

void PatternStampWorkspaceHost::RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer)
{
    using Components::DockPlacementDirection;
    using Components::PaneDirectiveMask;
    using Components::WorkspaceDockPaneDescriptor;

    RegisteredTabCount = 0u;

    // 3-Column Studio layout matching 01-HTML-pattern.png & 01-HTML-stamp.png: Left Layers | Center Pattern/Stamp | Right Inspector
    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Layers",
        DockPlacementDirection::SplitLeft,
        0.21f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { LayerStackPane.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });

    const char* CenterTitle = (Session.Mode == PatternStampWorkspaceMode::Pattern) ? "Pattern" : "Stamp";
    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        CenterTitle,
        DockPlacementDirection::CenterTabGroup,
        0.54f,
        PaneDirectiveMask::CentralViewportDirectives,
        [this]() { ToolHeaderPane.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });
    ++RegisteredTabCount;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Inspector",
        DockPlacementDirection::SplitRight,
        0.25f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { InspectorPane.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });
}

} // namespace Frontier::Workspaces
