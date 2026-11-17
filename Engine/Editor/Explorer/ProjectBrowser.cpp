/*============================================================================================================================================
                                                              PROJECTBROWSER.CPP
============================================================================================================================================*/
// 🧩 Implementation of ProjectBrowser matching Experimental/ProjectBrowser 1:1 using PixelSpace, Studio Controls, & TieredWorldRenderSequence

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/Explorer/ProjectBrowser.h"

#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/ExclusiveSelector.h"
#include "Engine/Configuration/ProjectSpaceRepository.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <shobjidl.h>
    #include <windows.h>
#endif

namespace Frontier::Editor::Explorer
{

namespace
{

struct BrowserPalette
{
    ColorRgba MainBg   {};
    ColorRgba PanelBg  {};
    ColorRgba TileBg   {};
    ColorRgba NestedBg {};
    ColorRgba Line     {};
    ColorRgba Ink      {};
    ColorRgba Muted    {};
    ColorRgba Faint    {};
    ColorRgba Control  {};
    ColorRgba Selected {};
    ColorRgba Accent   {};
    ColorRgba DotProj  { 164u, 186u, 176u, 255u };
    ColorRgba DotCad   { 158u, 175u, 194u, 255u };
    ColorRgba DotGeom  { 180u, 167u, 189u, 255u };
};

[[nodiscard]] BrowserPalette QueryBrowserPalette(ThemePreset Preset) noexcept
{
    BrowserPalette P {};
    switch (Preset)
    {
    case ThemePreset::Light:
        P.MainBg   = { 241u, 245u, 249u, 255u };
        P.PanelBg  = { 255u, 255u, 255u, 255u };
        P.TileBg   = { 248u, 250u, 252u, 255u };
        P.NestedBg = { 226u, 232u, 240u, 255u };
        P.Line     = { 15u,  23u,  42u,  24u  };
        P.Ink      = { 15u,  23u,  42u,  255u };
        P.Muted    = { 100u, 116u, 139u, 255u };
        P.Faint    = { 137u, 149u, 166u, 255u };
        P.Control  = { 15u,  23u,  42u,  10u  };
        P.Selected = { 15u,  23u,  42u,  18u  };
        P.Accent   = { 77u,  111u, 101u, 255u };
        break;

    case ThemePreset::Dark:
        P.MainBg   = { 17u,  17u,  17u,  255u };
        P.PanelBg  = { 26u,  26u,  26u,  255u };
        P.TileBg   = { 34u,  34u,  37u,  255u };
        P.NestedBg = { 44u,  44u,  48u,  255u };
        P.Line     = { 255u, 255u, 255u, 22u  };
        P.Ink      = { 255u, 255u, 255u, 232u };
        P.Muted    = { 255u, 255u, 255u, 132u };
        P.Faint    = { 255u, 255u, 255u, 76u  };
        P.Control  = { 255u, 255u, 255u, 14u  };
        P.Selected = { 255u, 255u, 255u, 28u  };
        P.Accent   = { 183u, 198u, 189u, 255u };
        break;

    case ThemePreset::Oled:
    default:
        P.MainBg   = { 0u,   0u,   0u,   255u };
        P.PanelBg  = { 10u,  10u,  10u,  255u };
        P.TileBg   = { 20u,  20u,  21u,  255u };
        P.NestedBg = { 28u,  28u,  30u,  255u };
        P.Line     = { 255u, 255u, 255u, 20u  };
        P.Ink      = { 255u, 255u, 255u, 242u };
        P.Muted    = { 255u, 255u, 255u, 118u };
        P.Faint    = { 255u, 255u, 255u, 70u  };
        P.Control  = { 255u, 255u, 255u, 13u  };
        P.Selected = { 255u, 255u, 255u, 24u  };
        P.Accent   = { 183u, 198u, 189u, 255u };
        break;
    }
    return P;
}

[[nodiscard]] std::string LowercaseString(std::string_view Text)
{
    std::string Out(Text);
    for (char& Ch : Out)
    {
        Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
    }
    return Out;
}

void UploadRgbaToTextureData(ImTextureData& Tex, const uint8_t* RgbaPixels, uint32_t WidthPx, uint32_t HeightPx)
{
    if (RgbaPixels == nullptr || WidthPx == 0u || HeightPx == 0u)
    {
        return;
    }
    const bool Recreate =
        Tex.Width != static_cast<int>(WidthPx) ||
        Tex.Height != static_cast<int>(HeightPx) ||
        Tex.Pixels == nullptr;
    if (Recreate)
    {
        Tex.DestroyPixels();
        Tex.SetStatus(ImTextureStatus_Destroyed);
        Tex.Create(ImTextureFormat_RGBA32, static_cast<int>(WidthPx), static_cast<int>(HeightPx));
    }
    std::memcpy(Tex.Pixels, RgbaPixels, static_cast<std::size_t>(WidthPx) * static_cast<std::size_t>(HeightPx) * 4u);
    if (!Recreate && Tex.Status == ImTextureStatus_OK)
    {
        Tex.UpdateRect = { 0u, 0u, static_cast<unsigned short>(WidthPx), static_cast<unsigned short>(HeightPx) };
        Tex.SetStatus(ImTextureStatus_WantUpdates);
    }
}

[[nodiscard]] std::filesystem::path InvokeNativeOsDialog(bool SelectDirectory)
{
#if defined(_WIN32)
    std::filesystem::path SelectedPath {};
    const HRESULT CoHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog* Dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&Dialog))) && Dialog != nullptr)
    {
        DWORD Options = 0;
        if (SUCCEEDED(Dialog->GetOptions(&Options)))
        {
            Options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
            if (SelectDirectory)
            {
                Options |= FOS_PICKFOLDERS;
            }
            (void)Dialog->SetOptions(Options);
        }
        if (SUCCEEDED(Dialog->Show(nullptr)))
        {
            IShellItem* ResultItem = nullptr;
            if (SUCCEEDED(Dialog->GetResult(&ResultItem)) && ResultItem != nullptr)
            {
                PWSTR RawFilePath = nullptr;
                if (SUCCEEDED(ResultItem->GetDisplayName(SIGDN_FILESYSPATH, &RawFilePath)) && RawFilePath != nullptr)
                {
                    SelectedPath = std::filesystem::path(RawFilePath);
                    CoTaskMemFree(RawFilePath);
                }
                ResultItem->Release();
            }
        }
        Dialog->Release();
    }
    if (SUCCEEDED(CoHr))
    {
        CoUninitialize();
    }
    return SelectedPath;
#else
    (void)SelectDirectory;
    return {};
#endif
}

void ConstructStarPolygon(PixelSpace& Surface, Rectangle2D Box, bool Filled, ColorRgba StrokeInk) noexcept
{
    constexpr std::array<Point2D, 10> UnitStar = {{
        { 12.0f,  3.0f },
        { 14.7f,  8.6f },
        { 20.9f,  9.5f },
        { 16.4f, 13.8f },
        { 17.5f, 20.0f },
        { 12.0f, 17.1f },
        {  6.5f, 20.0f },
        {  7.6f, 13.8f },
        {  3.1f,  9.5f },
        {  9.3f,  8.6f },
    }};
    const float Scale = std::min(Box.Width, Box.Height) * 0.56f / 24.0f;
    const float OrigX = Box.X + (Box.Width - 24.0f * Scale) * 0.5f;
    const float OrigY = Box.Y + (Box.Height - 24.0f * Scale) * 0.5f;

    std::array<Point2D, 10> Pts {};
    for (std::size_t Idx = 0u; Idx < UnitStar.size(); ++Idx)
    {
        Pts[Idx] = { OrigX + UnitStar[Idx].X * Scale, OrigY + UnitStar[Idx].Y * Scale };
    }
    if (Filled)
    {
        const Point2D Center { OrigX + 12.0f * Scale, OrigY + 12.2f * Scale };
        const ColorRgba FillTint = StrokeInk.WithAlpha(92u);
        for (std::size_t Idx = 0u; Idx < Pts.size(); ++Idx)
        {
            const std::array<Point2D, 3> Tri = {{ Center, Pts[Idx], Pts[(Idx + 1u) % Pts.size()] }};
            Surface.FillPolygon(Tri, FillTint);
        }
    }
    Surface.StrokePolyline(Pts, StrokeInk, 1.35f, true);
}

[[nodiscard]] const char* QueryCategoryCaption(BrowserEntryCategory Cat) noexcept
{
    switch (Cat)
    {
    case BrowserEntryCategory::Project:  return "Project";
    case BrowserEntryCategory::Cad:      return "CAD";
    case BrowserEntryCategory::Geometry: return "Geometry";
    }
    return "Project";
}

[[nodiscard]] ColorRgba QueryCategoryDot(BrowserEntryCategory Cat, const BrowserPalette& P) noexcept
{
    switch (Cat)
    {
    case BrowserEntryCategory::Project:  return P.DotProj;
    case BrowserEntryCategory::Cad:      return P.DotCad;
    case BrowserEntryCategory::Geometry: return P.DotGeom;
    }
    return P.DotProj;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             PROJECTBROWSER IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

ProjectBrowser::ProjectBrowser()
{
    SearchInput.Begin("");
    SearchInput.Active = false;
    CommentInput.Begin("");
    CommentInput.Active = false;
    DraftProjectNameInput.Begin("AeroGallery");
    DraftProjectNameInput.Active = false;
    DraftDestinationDirInput.Begin("");
    DraftDestinationDirInput.Active = false;
    DiskPathInput.Begin("");
    DiskPathInput.Active = false;
}

ProjectBrowser::~ProjectBrowser()
{
    ReleaseGpuTextures();
}

void ProjectBrowser::ReleaseGpuTextures() noexcept
{
    for (auto& Entry : Collection)
    {
        if (Entry.ThumbnailTex != nullptr)
        {
            Entry.ThumbnailTex->DestroyPixels();
            Entry.ThumbnailTex->SetStatus(ImTextureStatus_Destroyed);
            Entry.ThumbnailTex.reset();
        }
    }
    if (ReaderPreviewTex != nullptr)
    {
        ReaderPreviewTex->DestroyPixels();
        ReaderPreviewTex->SetStatus(ImTextureStatus_Destroyed);
        ReaderPreviewTex.reset();
    }
    for (auto& ViewTex : ReaderViewThumbs)
    {
        if (ViewTex != nullptr)
        {
            ViewTex->DestroyPixels();
            ViewTex->SetStatus(ImTextureStatus_Destroyed);
            ViewTex.reset();
        }
    }
    ReaderViewThumbs.clear();
    GpuPreviewRenderer.reset();
}

void ProjectBrowser::AttachVulkanWorldRenderer(
    GeometricRaster::VulkanInterchange* DeviceInterchange,
    const std::filesystem::path&        CompiledShaderDirectory)
{
    GpuPreviewRenderer.reset();
    if (DeviceInterchange == nullptr || !DeviceInterchange->Ready())
    {
        return;
    }
    auto Candidate = std::make_unique<GeometricRaster::VulkanWorldRenderSequence>();
    if (Candidate->InitializeSequence(*DeviceInterchange, CompiledShaderDirectory))
    {
        GpuPreviewRenderer = std::move(Candidate);
        if (PreviewSceneBundle != nullptr)
        {
            GpuPreviewRenderer->SynchronizeSceneResources(PreviewSceneBundle->Scene, nullptr, true);
        }
    }
}

void ProjectBrowser::AssignLaunchWorkspaceCallback(LaunchWorkspaceCallback Callback)
{
    LaunchCallback = std::move(Callback);
}

void ProjectBrowser::ConfigureThemePreset(ThemePreset Preset) noexcept
{
    ActiveTheme = Preset;
}

void ProjectBrowser::EnsurePreviewRendererReady()
{
    if (PreviewSceneBundle == nullptr || PreviewWorldRenderer == nullptr)
    {
        PreviewSceneBundle   = std::make_unique<GeometricRaster::ShowcaseWorldSceneBundle>();
        PreviewWorldRenderer = std::make_unique<GeometricRaster::TieredWorldRenderSequence>();
        GeometricRaster::BuildShowcaseGalleryScene(*PreviewSceneBundle, 320u, 194u);
        if (GpuPreviewRenderer != nullptr && GpuPreviewRenderer->QueryReady())
        {
            GpuPreviewRenderer->SynchronizeSceneResources(PreviewSceneBundle->Scene, nullptr, true);
        }
        else
        {
            PreviewWorldRenderer->PrepareWorldScene(PreviewSceneBundle->Scene);
        }
    }
}

void ProjectBrowser::RenderThumbnailForEntry(BrowserCollectionEntry& Entry, uint32_t ViewOrdinal)
{
    EnsurePreviewRendererReady();

    GeometricRaster::PerspectiveCameraFrame Cam = PreviewSceneBundle->EditorCamera;
    Cam.ViewportWidthPx  = 264u;
    Cam.ViewportHeightPx = 160u;

    const uint32_t Preset = (Entry.CameraPresetIdx + ViewOrdinal) % 5u;
    switch (Preset)
    {
    case 0u:
        Cam.CameraOrigin = { 0.00f, -3.55f, 0.42f };
        Cam.TargetPoint  = { 0.00f,  0.55f, 0.15f };
        break;
    case 1u:
        Cam.CameraOrigin = { -1.65f, -2.75f, 0.65f };
        Cam.TargetPoint  = {  0.15f,  0.40f, 0.05f };
        break;
    case 2u:
        Cam.CameraOrigin = {  1.70f, -2.65f, 0.58f };
        Cam.TargetPoint  = { -0.15f,  0.45f, 0.08f };
        break;
    case 3u:
        Cam.CameraOrigin = { -0.85f, -1.95f, 0.92f };
        Cam.TargetPoint  = { -0.45f,  0.20f, -0.35f };
        break;
    default:
        Cam.CameraOrigin = {  0.90f, -2.10f, 0.25f };
        Cam.TargetPoint  = {  0.30f,  0.55f, -0.20f };
        break;
    }

    std::vector<uint8_t> DisplayRgba {};
    uint32_t             OutW        = Cam.ViewportWidthPx;
    uint32_t             OutH        = Cam.ViewportHeightPx;
    bool                 GpuRendered = false;
    if (GpuPreviewRenderer != nullptr && GpuPreviewRenderer->QueryReady())
    {
        GpuRendered = GpuPreviewRenderer->RenderWorldViewportRgba8(
            GeometricRaster::WorldRenderTier::Tier1GtxCalibrated,
            Cam,
            PreviewSceneBundle->Scene,
            0u,
            DisplayRgba);
    }
    if (!GpuRendered)
    {
        auto CpuOutput = PreviewWorldRenderer->RenderWorldFrame(
            GeometricRaster::WorldRenderTier::Tier1GtxCalibrated,
            Cam,
            Cam,
            PreviewSceneBundle->Scene);
        OutW        = CpuOutput.WidthPx;
        OutH        = CpuOutput.HeightPx;
        DisplayRgba = std::move(CpuOutput.DisplayRgba8);
    }

    Entry.ThumbnailRgba = std::move(DisplayRgba);
    Entry.ThumbnailW    = OutW;
    Entry.ThumbnailH    = OutH;
    if (Entry.ThumbnailTex == nullptr)
    {
        Entry.ThumbnailTex = std::make_unique<ImTextureData>();
    }
    UploadRgbaToTextureData(*Entry.ThumbnailTex, Entry.ThumbnailRgba.data(), Entry.ThumbnailW, Entry.ThumbnailH);
}

void ProjectBrowser::RefreshReaderPreviewTexture()
{
    if (ActiveEntryIndex < 0 || static_cast<std::size_t>(ActiveEntryIndex) >= Collection.size())
    {
        return;
    }
    EnsurePreviewRendererReady();

    GeometricRaster::PerspectiveCameraFrame Cam = PreviewSceneBundle->EditorCamera;
    Cam.ViewportWidthPx  = 560u;
    Cam.ViewportHeightPx = 384u;

    const float YawShift = static_cast<float>(ActiveViewIndex) * 0.48f - static_cast<float>(ActiveRevisionIndex) * 0.22f;
    const float Dist     = 3.45f;
    Cam.CameraOrigin     = {
        std::sin(YawShift) * Dist * 0.65f,
        -std::cos(YawShift) * Dist,
        0.42f + static_cast<float>(ActiveViewIndex) * 0.16f,
    };
    Cam.TargetPoint = { 0.00f, 0.48f, 0.08f };

    std::vector<uint8_t> MainRgba {};
    uint32_t             MainW    = Cam.ViewportWidthPx;
    uint32_t             MainH    = Cam.ViewportHeightPx;
    bool                 GpuReady = (GpuPreviewRenderer != nullptr && GpuPreviewRenderer->QueryReady());
    bool                 MainDone = false;
    if (GpuReady)
    {
        MainDone = GpuPreviewRenderer->RenderWorldViewportRgba8(
            GeometricRaster::WorldRenderTier::Tier1GtxCalibrated,
            Cam,
            PreviewSceneBundle->Scene,
            0u,
            MainRgba);
    }
    if (!MainDone)
    {
        auto CpuOutput = PreviewWorldRenderer->RenderWorldFrame(
            GeometricRaster::WorldRenderTier::Tier1GtxCalibrated,
            Cam,
            Cam,
            PreviewSceneBundle->Scene);
        MainW    = CpuOutput.WidthPx;
        MainH    = CpuOutput.HeightPx;
        MainRgba = std::move(CpuOutput.DisplayRgba8);
    }

    ReaderPreviewRgba = std::move(MainRgba);
    ReaderPreviewW    = MainW;
    ReaderPreviewH    = MainH;
    if (ReaderPreviewTex == nullptr)
    {
        ReaderPreviewTex = std::make_unique<ImTextureData>();
    }
    UploadRgbaToTextureData(*ReaderPreviewTex, ReaderPreviewRgba.data(), ReaderPreviewW, ReaderPreviewH);

    // Populate distinct camera angles for each view in the left Reader sequence rail
    const auto& ActiveEntry = Collection[static_cast<std::size_t>(ActiveEntryIndex)];
    if (ReaderViewThumbs.size() < ActiveEntry.ViewTitles.size())
    {
        ReaderViewThumbs.resize(ActiveEntry.ViewTitles.size());
    }
    for (std::size_t VIdx = 0u; VIdx < ActiveEntry.ViewTitles.size(); ++VIdx)
    {
        GeometricRaster::PerspectiveCameraFrame ThumbCam = PreviewSceneBundle->EditorCamera;
        ThumbCam.ViewportWidthPx  = 220u;
        ThumbCam.ViewportHeightPx = 148u;
        const float VYaw          = static_cast<float>(VIdx) * 0.52f - static_cast<float>(ActiveRevisionIndex) * 0.22f;
        ThumbCam.CameraOrigin     = {
            std::sin(VYaw) * 3.45f * 0.65f,
            -std::cos(VYaw) * 3.45f,
            0.42f + static_cast<float>(VIdx) * 0.16f,
        };
        ThumbCam.TargetPoint = { 0.00f, 0.48f, 0.08f };
        std::vector<uint8_t> ThumbRgba {};
        uint32_t             ThumbW    = ThumbCam.ViewportWidthPx;
        uint32_t             ThumbH    = ThumbCam.ViewportHeightPx;
        bool                 ThumbDone = false;
        if (GpuReady)
        {
            ThumbDone = GpuPreviewRenderer->RenderWorldViewportRgba8(
                GeometricRaster::WorldRenderTier::Tier1GtxCalibrated,
                ThumbCam,
                PreviewSceneBundle->Scene,
                0u,
                ThumbRgba);
        }
        if (!ThumbDone)
        {
            const auto ThumbOutput = PreviewWorldRenderer->RenderWorldFrame(
                GeometricRaster::WorldRenderTier::Tier1GtxCalibrated,
                ThumbCam,
                ThumbCam,
                PreviewSceneBundle->Scene);
            ThumbW    = ThumbOutput.WidthPx;
            ThumbH    = ThumbOutput.HeightPx;
            ThumbRgba = ThumbOutput.DisplayRgba8;
        }
        if (ReaderViewThumbs[VIdx] == nullptr)
        {
            ReaderViewThumbs[VIdx] = std::make_unique<ImTextureData>();
        }
        UploadRgbaToTextureData(
            *ReaderViewThumbs[VIdx],
            ThumbRgba.data(),
            ThumbW,
            ThumbH);
    }
}

void ProjectBrowser::InitializeDefaultCollection(
    const std::filesystem::path& DefaultProjectsDirectory,
    const std::filesystem::path& FrontierRootDirectory,
    const std::filesystem::path& FrontierBinariesDirectory)
{
    DefaultProjectsDir  = DefaultProjectsDirectory.empty() ? std::filesystem::current_path() / "Projects" : DefaultProjectsDirectory;
    FrontierRootDir     = FrontierRootDirectory.empty() ? std::filesystem::current_path() : FrontierRootDirectory;
    FrontierBinariesDir = FrontierBinariesDirectory.empty() ? (FrontierRootDir / "Binaries" / "Win64") : FrontierBinariesDirectory;

    DraftDestinationDirInput.Begin(DefaultProjectsDir.generic_string().c_str());
    DraftDestinationDirInput.Active = false;
    DiskPathInput.Begin(DefaultProjectsDir.generic_string().c_str());
    DiskPathInput.Active = false;

    for (auto& Entry : Collection)
    {
        if (Entry.ThumbnailTex != nullptr)
        {
            Entry.ThumbnailTex->DestroyPixels();
            Entry.ThumbnailTex->SetStatus(ImTextureStatus_Destroyed);
            Entry.ThumbnailTex.reset();
        }
    }
    if (ReaderPreviewTex != nullptr)
    {
        ReaderPreviewTex->DestroyPixels();
        ReaderPreviewTex->SetStatus(ImTextureStatus_Destroyed);
        ReaderPreviewTex.reset();
    }
    for (auto& ViewTex : ReaderViewThumbs)
    {
        if (ViewTex != nullptr)
        {
            ViewTex->DestroyPixels();
            ViewTex->SetStatus(ImTextureStatus_Destroyed);
            ViewTex.reset();
        }
    }
    ReaderViewThumbs.clear();
    Collection.clear();

    auto AddSample = [&](
        std::string          Id,
        std::string          Title,
        std::string          Ext,
        BrowserEntryCategory Cat,
        std::string          Summary,
        std::string          Desc,
        std::string          Opened,
        std::filesystem::path Path,
        std::vector<std::string> Views,
        std::vector<std::string> Revs,
        std::vector<BrowserReviewNote> Notes,
        bool                 Starred,
        uint32_t             CamPreset)
    {
        BrowserCollectionEntry Entry {};
        Entry.Identifier      = std::move(Id);
        Entry.Title           = std::move(Title);
        Entry.Suffix       = std::move(Ext);
        Entry.Category        = Cat;
        Entry.SummaryCaption  = std::move(Summary);
        Entry.Description     = std::move(Desc);
        Entry.LastOpenedText  = std::move(Opened);
        Entry.FilePath        = std::move(Path);
        Entry.ViewTitles      = std::move(Views);
        Entry.RevisionTitles  = std::move(Revs);
        Entry.Notes           = std::move(Notes);
        Entry.Starred         = Starred;
        Entry.CameraPresetIdx = CamPreset;
        RenderThumbnailForEntry(Entry, 0u);
        Collection.push_back(std::move(Entry));
    };

    AddSample(
        "cornell",
        "Cornell-Studio Gallery",
        ".projectspace",
        BrowserEntryCategory::Project,
        "3 levels · 24 props · Tier 1 GTX & Tier 2 RTX",
        "Calibrated Cornell-Studio OpenPBR showcase with dielectric glass, brushed copper, LTC quad lighting, and ForceField authoring.",
        "Just now",
        DefaultProjectsDir / "CornellStudioGallery" / "CornellStudioGallery.projectspace",
        { "Studio", "Courtyard", "Workshop" },
        { "Tier 1 GTX + Tier 2 RTX & ForceFields", "OpenPBR 3D specimens & ColourCodex", "Initial Cornell-Studio gallery" },
        { { "Sam", "S", "Studio cameras, LTC lights and OpenPBR specimens ready in Construct & ContentBrowser.", "Just now" } },
        true,
        0u);

    AddSample(
        "valley",
        "Actuator studio",
        ".projectspace",
        BrowserEntryCategory::Project,
        "3 levels · 1 document",
        "Compact reference studio scene calibrated around metallic & dielectric inspection specimens.",
        "Today, 10:42",
        DefaultProjectsDir / "ActuatorStudio" / "ActuatorStudio.projectspace",
        { "Studio", "Courtyard", "Workshop" },
        { "Copper winding and studio props", "Shaft and fasteners", "Housing blockout" },
        {
            { "Sam", "S", "Review the clearance around the central specimen.", "2 hours ago" },
            { "Alex", "A", "Alternate studio camera angle verified.", "Yesterday" },
        },
        true,
        1u);

    AddSample(
        "stone",
        "Actuator housing",
        ".GeometrySpace",
        BrowserEntryCategory::Geometry,
        "Geometry · UV · Texture",
        "Surface geometry and UDIM UV islands ready for conformal unwrap and concave packing in UVWorkspace.",
        "Yesterday",
        FrontierRootDir / "Assets" / "ActuatorHousing.obj",
        { "Geometry", "UV layout", "Texture" },
        { "Full housing proportions", "UV seam relaxation", "Initial compact housing" },
        { { "Alex", "A", "Keep the primary UV seams tucked along the underside.", "Yesterday" } },
        false,
        3u);

    AddSample(
        "mount",
        "Precision mount",
        ".projectspace",
        BrowserEntryCategory::Cad,
        "2 assemblies · 2 views",
        "Precision mounting assembly with anodized metallic sleeves and studio area lighting.",
        "Today, 09:18",
        DefaultProjectsDir / "PrecisionMount" / "PrecisionMount.projectspace",
        { "Main assembly", "Sleeve assembly", "Orthographic" },
        { "Copper winding rings", "Shaft and fasteners", "Initial housing" },
        { { "Sam", "S", "Check sleeve tolerance before next build.", "45 minutes ago" } },
        false,
        2u);

    AddSample(
        "faceted",
        "Housing UV study",
        ".UVSpace",
        BrowserEntryCategory::Geometry,
        "UDIM 1001 · 2K Texel Density",
        "Dual-viewport UV unwrap & texel-density study targeting UVWorkspace and Frontier World.",
        "Oct 12",
        FrontierRootDir / "Assets" / "HousingStudy.UVSpace",
        { "UV layout", "Geometry", "Texel density" },
        { "Surface colour study", "Conformal UV unwrap", "Initial geometry" },
        {},
        false,
        4u);

    std::error_code Ec {};
    if (std::filesystem::exists(DefaultProjectsDir, Ec))
    {
        (void)ScanFolderForEntries(DefaultProjectsDir);
    }
}

void ProjectBrowser::OpenNewProjectSheet(std::string_view InitialName, const std::filesystem::path& DestinationFolder)
{
    DraftProjectNameInput.Begin(std::string(InitialName).c_str());
    if (!DestinationFolder.empty())
    {
        DraftDestinationDirInput.Begin(DestinationFolder.generic_string().c_str());
    }
    else if (DraftDestinationDirInput.Length == 0u)
    {
        DraftDestinationDirInput.Begin(DefaultProjectsDir.generic_string().c_str());
    }
    DraftEnableFrontier = true;
    DraftEnableUv       = true;
    NewProjectModalOpen = true;
    DiskPathModalOpen   = false;
}

Configuration::ProjectScaffoldOutcome ProjectBrowser::TriggerCreateProjectFromDraft(bool LaunchImmediately)
{
    Configuration::ProjectScaffoldSpecification Spec {};
    Spec.DisplayName          = DraftProjectNameInput.Text;
    Spec.DestinationDirectory = std::filesystem::path(DraftDestinationDirInput.Text);
    Spec.FrontierRootPath     = FrontierRootDir;
    Spec.FrontierBinariesPath = FrontierBinariesDir;
    Spec.InitialTheme         = ActiveTheme;
    Spec.EnableFrontierWorld  = DraftEnableFrontier;
    Spec.EnableUvWorkspace    = DraftEnableUv;

    auto Outcome = Configuration::ProjectGenerator::GenerateProject(Spec);
    if (Outcome.Succeeded)
    {
        NewProjectModalOpen   = false;
        StatusBannerMessage   = "Created project " + Outcome.PascalIdentifier + " at " + Outcome.ProjectRootDirectory.generic_string();
        StatusBannerRemaining = 5.0;
        (void)RegisterDiskPathInCollection(Outcome.ProjectSpaceFile, !LaunchImmediately);

        if (LaunchImmediately && LaunchCallback)
        {
            BrowserWorkspaceLaunchDirective Directive {};
            Directive.TargetWorkspace  = DraftEnableFrontier
                                             ? Workspaces::HostWorkspaceTarget::FrontierWorld
                                             : Workspaces::HostWorkspaceTarget::UVWorkspace;
            Directive.ProjectTitle     = Outcome.PascalIdentifier;
            Directive.SelectedFilePath = Outcome.ProjectSpaceFile;
            LaunchCallback(Directive);
        }
    }
    else
    {
        StatusBannerMessage   = Outcome.Diagnostic;
        StatusBannerRemaining = 5.0;
    }
    return Outcome;
}

bool ProjectBrowser::RegisterDiskPathInCollection(const std::filesystem::path& DiskPath, bool ActivateInReader)
{
    if (DiskPath.empty())
    {
        return false;
    }

    std::error_code Ec {};
    if (std::filesystem::is_directory(DiskPath, Ec))
    {
        const std::filesystem::path CandidateSpace = DiskPath / (DiskPath.filename().string() + ProjectSpaceRepository::ContainerSuffix);
        if (std::filesystem::exists(CandidateSpace, Ec))
        {
            return RegisterDiskPathInCollection(CandidateSpace, ActivateInReader);
        }
        return ScanFolderForEntries(DiskPath) > 0u;
    }

    for (std::size_t Idx = 0u; Idx < Collection.size(); ++Idx)
    {
        if (Collection[Idx].FilePath == DiskPath)
        {
            if (ActivateInReader)
            {
                ActivateEntryByIndex(static_cast<int32_t>(Idx));
            }
            return true;
        }
    }

    const std::string ExtLower = LowercaseString(DiskPath.extension().string());
    BrowserEntryCategory Cat   = BrowserEntryCategory::Project;
    if (ExtLower == ".solidarc" || ExtLower == ".step" || ExtLower == ".stp" || ExtLower == ".iges" || ExtLower == ".igs")
    {
        Cat = BrowserEntryCategory::Cad;
    }
    else if (ExtLower == ".obj" || ExtLower == ".uvspace" || ExtLower == ".geometryspace" || ExtLower == ".geometry")
    {
        Cat = BrowserEntryCategory::Geometry;
    }

    std::string Title = DiskPath.stem().string();
    if (ExtLower == ".projectspace")
    {
        ProjectSpaceDocument LoadedDoc {};
        std::string          Diag      {};
        if (ProjectSpaceRepository::Load(DiskPath, LoadedDoc, Diag) && !LoadedDoc.ProjectTitle.empty())
        {
            Title = LoadedDoc.ProjectTitle;
        }
    }

    BrowserCollectionEntry Entry {};
    Entry.Identifier      = "disk_" + std::to_string(Collection.size() + 1u);
    Entry.Title           = Title.empty() ? "LocalEntry" : Title;
    Entry.Suffix       = DiskPath.extension().empty() ? ".projectspace" : DiskPath.extension().string();
    Entry.Category        = Cat;
    Entry.SummaryCaption  = (Cat == BrowserEntryCategory::Cad)
                                ? "Local Parametric CAD · Sketcher & Frontier"
                                : (Cat == BrowserEntryCategory::Geometry)
                                      ? "Local Geometry · UVWorkspace & Frontier"
                                      : "Local C++20 Frontier Project · 3 Targets";
    Entry.Description     = "Mounted from local filesystem (" + DiskPath.generic_string() + ").";
    Entry.LastOpenedText  = "Just now";
    Entry.FilePath        = DiskPath;
    Entry.ViewTitles      = (Cat == BrowserEntryCategory::Cad)
                                ? std::vector<std::string>{ "Isometric", "Front", "Drawing Sheet" }
                                : (Cat == BrowserEntryCategory::Geometry)
                                      ? std::vector<std::string>{ "Geometry", "UV layout", "Surface" }
                                      : std::vector<std::string>{ "Studio", "Courtyard", "Workshop" };
    Entry.RevisionTitles  = { "Local working tree · Current" };
    Entry.Starred         = true;
    Entry.LocalDiskEntry  = true;
    Entry.CameraPresetIdx = static_cast<uint32_t>(Collection.size() % 5u);
    RenderThumbnailForEntry(Entry, 0u);

    Collection.insert(Collection.begin(), std::move(Entry));
    if (ActivateInReader)
    {
        ActivateEntryByIndex(0);
    }
    return true;
}

uint32_t ProjectBrowser::ScanFolderForEntries(const std::filesystem::path& FolderPath)
{
    std::error_code Ec {};
    if (FolderPath.empty() || !std::filesystem::exists(FolderPath, Ec))
    {
        return 0u;
    }

    uint32_t Added = 0u;
    auto InspectFile = [&](const std::filesystem::path& Candidate)
    {
        const std::string Ext = LowercaseString(Candidate.extension().string());
        if (Ext == ".projectspace" || Ext == ".obj" || Ext == ".uvspace" || Ext == ".geometryspace")
        {
            if (RegisterDiskPathInCollection(Candidate, false))
            {
                ++Added;
            }
        }
    };

    for (const auto& DirEntry : std::filesystem::directory_iterator(FolderPath, Ec))
    {
        if (Ec)
        {
            break;
        }
        if (DirEntry.is_regular_file(Ec))
        {
            InspectFile(DirEntry.path());
        }
        else if (DirEntry.is_directory(Ec))
        {
            for (const auto& Sub : std::filesystem::directory_iterator(DirEntry.path(), Ec))
            {
                if (!Ec && Sub.is_regular_file(Ec))
                {
                    InspectFile(Sub.path());
                }
            }
        }
    }
    return Added;
}

void ProjectBrowser::ActivateEntryByIndex(int32_t EntryIndex) noexcept
{
    if (EntryIndex < 0 || static_cast<std::size_t>(EntryIndex) >= Collection.size())
    {
        ActiveEntryIndex = -1;
        return;
    }
    ActiveEntryIndex     = EntryIndex;
    ActiveViewIndex      = 0u;
    ActiveRevisionIndex  = 0u;
    ReaderZoomPercent    = 100.0f;
    ActiveInspectorTab   = BrowserInspectorTab::Details;
    RevisionDropdownOpen = false;
    RefreshReaderPreviewTexture();
}

void ProjectBrowser::ReturnToLibrary() noexcept
{
    ActiveEntryIndex     = -1;
    RevisionDropdownOpen = false;
}

void ProjectBrowser::Render(Rectangle2D ViewportBounds, double DeltaSeconds, float DpiScale)
{
    if (ImGui::GetCurrentContext() == nullptr || ViewportBounds.Width < 64.0f || ViewportBounds.Height < 64.0f)
    {
        return;
    }
    if (Collection.empty())
    {
        InitializeDefaultCollection();
    }

    ThemeRepository BrowserThemeRepo {};
    BrowserThemeRepo.ConfigureTheme(ActiveTheme);
    ControlAppearance::ConfigureTheme(BrowserThemeRepo);

    const float SafeScale = (DpiScale > 0.5f) ? DpiScale : 1.0f;
    PixelSpace  Surface(ImGui::GetBackgroundDrawList(), Point2D{ ViewportBounds.X, ViewportBounds.Y }, SafeScale);

    const Rectangle2D LogicalBounds {
        0.0f,
        0.0f,
        ViewportBounds.Width / SafeScale,
        ViewportBounds.Height / SafeScale,
    };

    const Point2D MousePos = Surface.QueryMousePosition();
    ControlPointer Pointer {};
    Pointer.X        = MousePos.X;
    Pointer.Y        = MousePos.Y;
    Pointer.Down     = Surface.QueryMouseDown(0);
    Pointer.Pressed  = Surface.QueryMouseClicked(0);
    Pointer.Released = Surface.QueryMouseReleased(0);

    // Block background click-through when modal sheets are open
    ControlPointer BackgroundPointer = Pointer;
    if (NewProjectModalOpen || DiskPathModalOpen)
    {
        BackgroundPointer.Down     = false;
        BackgroundPointer.Pressed  = false;
        BackgroundPointer.Released = false;
        BackgroundPointer.Enabled  = false;
    }

    if (StatusBannerRemaining > 0.0)
    {
        StatusBannerRemaining = std::max(0.0, StatusBannerRemaining - std::max(0.0, DeltaSeconds));
    }

    if (ActiveEntryIndex >= 0 && static_cast<std::size_t>(ActiveEntryIndex) < Collection.size())
    {
        RenderReaderMode(Surface, LogicalBounds, BackgroundPointer, DeltaSeconds);
    }
    else
    {
        RenderLibraryMode(Surface, LogicalBounds, BackgroundPointer, DeltaSeconds);
    }

    if (NewProjectModalOpen)
    {
        PixelSpace ModalSurface(ImGui::GetForegroundDrawList(), Point2D{ ViewportBounds.X, ViewportBounds.Y }, SafeScale);
        RenderNewProjectModal(ModalSurface, LogicalBounds, Pointer);
    }
    else if (DiskPathModalOpen)
    {
        PixelSpace ModalSurface(ImGui::GetForegroundDrawList(), Point2D{ ViewportBounds.X, ViewportBounds.Y }, SafeScale);
        RenderDiskPathModal(ModalSurface, LogicalBounds, Pointer);
    }

    if (StatusBannerRemaining > 0.0 && !StatusBannerMessage.empty())
    {
        const BrowserPalette P = QueryBrowserPalette(ActiveTheme);
        PixelSpace ToastSurface(ImGui::GetForegroundDrawList(), Point2D{ ViewportBounds.X, ViewportBounds.Y }, SafeScale);
        const float ToastW = std::min(560.0f, LogicalBounds.Width - 40.0f);
        const Rectangle2D ToastRect { (LogicalBounds.Width - ToastW) * 0.5f, LogicalBounds.Height - 54.0f, ToastW, 34.0f };
        ToastSurface.FillRectangle(ToastRect, P.NestedBg, 9.0f);
        ToastSurface.StrokeRectangle(ToastRect, P.Line, 9.0f, 1.0f);
        ToastSurface.TextCentered(ToastRect, 11.0f, P.Ink, StatusBannerMessage.c_str());
    }
}

void ProjectBrowser::RenderLibraryMode(
    PixelSpace&           Surface,
    Rectangle2D           LogicalBounds,
    const ControlPointer& Pointer,
    double                DeltaSeconds)
{
    (void)DeltaSeconds;
    const BrowserPalette P = QueryBrowserPalette(ActiveTheme);
    Surface.FillRectangle(LogicalBounds, P.MainBg, 0.0f);

    constexpr float HeaderH = 64.0f;
    const float     LeftW   = LeftRailCollapsed ? 44.0f : 204.0f;
    const float     RightW  = RightPaneCollapsed ? 44.0f : 302.0f;
    const float     MainX   = LeftW;
    const float     MainW   = std::max(260.0f, LogicalBounds.Width - LeftW - RightW);
    const float     RightX  = MainX + MainW;

    // 1. Left Rail (204px / 44px collapsed)
    const Rectangle2D LeftPane { 0.0f, 0.0f, LeftW, LogicalBounds.Height };
    Surface.FillRectangle(LeftPane, P.PanelBg, 0.0f);
    Surface.StrokeLine({ LeftW, 0.0f }, { LeftW, LogicalBounds.Height }, P.Line, 1.0f);
    Surface.StrokeLine({ 0.0f, HeaderH }, { LeftW, HeaderH }, P.Line, 1.0f);

    if (LeftRailCollapsed)
    {
        ButtonDescriptor ExpandBtn {};
        ExpandBtn.Label    = ">";
        ExpandBtn.Tone     = ButtonTone::Ghost;
        ExpandBtn.FontSize = 13.0f;
        if (CapsuleButton::Construct(Surface, { 8.0f, 18.0f, 28.0f, 28.0f }, ExpandBtn, Pointer).Pressed)
        {
            LeftRailCollapsed = false;
        }
    }
    else
    {
        Surface.Text({ 16.0f, 24.0f }, 12.5f, P.Ink, "Files");
        ButtonDescriptor CollapseBtn {};
        CollapseBtn.Label    = "<";
        CollapseBtn.Tone     = ButtonTone::Ghost;
        CollapseBtn.FontSize = 12.5f;
        if (CapsuleButton::Construct(Surface, { LeftW - 36.0f, 18.0f, 26.0f, 28.0f }, CollapseBtn, Pointer).Pressed)
        {
            LeftRailCollapsed = true;
        }

        float NavY = HeaderH + 24.0f;
        Surface.Text({ 24.0f, NavY }, 9.0f, P.Faint, "WORKSPACE");
        NavY += 22.0f;

        auto ConstructNavRow = [&](const char* Label, BrowserFilterMode Mode, const ColorRgba* FormatDot, int32_t TrailingCount)
        {
            const Rectangle2D RowRect { 14.0f, NavY, LeftW - 28.0f, 34.0f };
            const bool Active  = (ActiveFilter == Mode);
            const bool Hovered = RowRect.Contains({ Pointer.X, Pointer.Y });
            if (Active || Hovered)
            {
                Surface.FillRectangle(RowRect, Active ? P.Selected : P.Control, 7.0f);
                if (Active)
                {
                    Surface.StrokeRectangle(RowRect, P.Line, 7.0f, 1.0f);
                }
            }
            float TextX = RowRect.X + 12.0f;
            if (FormatDot != nullptr)
            {
                Surface.FillRectangle({ TextX, RowRect.Y + 13.5f, 7.0f, 7.0f }, *FormatDot, 2.0f);
                TextX += 16.0f;
            }
            Surface.Text({ TextX, RowRect.Y + 10.0f }, 11.5f, Active ? P.Ink : P.Muted, Label);
            if (TrailingCount >= 0)
            {
                char CountBuf[16]{};
                std::snprintf(CountBuf, sizeof(CountBuf), "%d", TrailingCount);
                Surface.TextRightAligned({ RowRect.X, RowRect.Y, RowRect.Width - 10.0f, RowRect.Height }, 10.0f, P.Faint, CountBuf);
            }
            if (Hovered && Pointer.Pressed)
            {
                ActiveFilter   = Mode;
                LibraryScrollY = 0.0f;
            }
            NavY += 38.0f;
        };

        ConstructNavRow("All files", BrowserFilterMode::AllFiles, nullptr, static_cast<int32_t>(Collection.size()));
        ConstructNavRow("Recent",    BrowserFilterMode::Recent,   nullptr, -1);
        ConstructNavRow("Starred",   BrowserFilterMode::Starred,  nullptr, -1);

        NavY += 20.0f;
        Surface.Text({ 24.0f, NavY }, 9.0f, P.Faint, "FORMATS");
        NavY += 22.0f;
        ConstructNavRow("Projects", BrowserFilterMode::Projects, &P.DotProj, -1);
        ConstructNavRow("CAD",      BrowserFilterMode::Cad,      &P.DotCad,  -1);
        ConstructNavRow("Geometry", BrowserFilterMode::Geometry, &P.DotGeom, -1);

        const float BottomY = std::max(NavY + 24.0f, LogicalBounds.Height - 134.0f);
        Surface.Text({ 20.0f, BottomY }, 11.0f, P.Ink, "Your files. Your folders.");
        Surface.Text({ 20.0f, BottomY + 20.0f }, 10.0f, P.Muted, "Browse from any location.");
        Surface.Text({ 20.0f, BottomY + 35.0f }, 10.0f, P.Muted, "No fixed project directory.");

        ButtonDescriptor BrowseLink {};
        BrowseLink.Label    = "Browse folder";
        BrowseLink.Tone     = ButtonTone::Secondary;
        BrowseLink.FontSize = 10.5f;
        if (CapsuleButton::Construct(Surface, { 18.0f, BottomY + 60.0f, LeftW - 36.0f, 30.0f }, BrowseLink, Pointer).Pressed)
        {
            const auto ChosenDir = InvokeNativeOsDialog(true);
            if (!ChosenDir.empty())
            {
                const uint32_t Count = ScanFolderForEntries(ChosenDir);
                StatusBannerMessage   = "Scanned folder (" + std::to_string(Count) + " entries mounted)";
                StatusBannerRemaining = 4.0;
            }
            else
            {
                DiskPathModalOpen = true;
            }
        }
    }

    // 2. Center Header (64px)
    Surface.FillRectangle({ MainX, 0.0f, MainW, HeaderH }, P.PanelBg, 0.0f);
    Surface.StrokeLine({ MainX, HeaderH }, { MainX + MainW, HeaderH }, P.Line, 1.0f);
    Surface.Text({ MainX + 20.0f, 22.0f }, 16.0f, P.Ink, "Project browser");

    const float HeaderRight = MainX + MainW - 18.0f;
    const Rectangle2D OpenDiskRect  { HeaderRight - 118.0f, 16.0f, 118.0f, 32.0f };
    const Rectangle2D NewProjRect   { OpenDiskRect.X - 126.0f, 16.0f, 118.0f, 32.0f };
    const Rectangle2D ThemeSegRect  { NewProjRect.X - 166.0f, 18.0f, 156.0f, 28.0f };

    if (ThemeSegRect.X > MainX + 165.0f)
    {
        constexpr std::array<const char*, 3> ThemeLabels = {{ "OLED", "Dark", "Light" }};
        const uint32_t ActiveThemeIdx = (ActiveTheme == ThemePreset::Oled) ? 0u : ((ActiveTheme == ThemePreset::Dark) ? 1u : 2u);
        uint32_t ChosenTheme = ActiveThemeIdx;
        if (CapsuleButton::ConstructSegmented(Surface, ThemeSegRect, ThemeLabels.data(), 3u, ActiveThemeIdx, Pointer, ChosenTheme).Pressed)
        {
            ActiveTheme = (ChosenTheme == 0u) ? ThemePreset::Oled : ((ChosenTheme == 1u) ? ThemePreset::Dark : ThemePreset::Light);
        }
    }

    {
        ButtonDescriptor NewProjBtn {};
        NewProjBtn.Label    = "+ New Project";
        NewProjBtn.Tone     = ButtonTone::Secondary;
        NewProjBtn.FontSize = 11.0f;
        NewProjBtn.PaddingX = 14.0f;
        if (CapsuleButton::Construct(Surface, NewProjRect, NewProjBtn, Pointer).Pressed)
        {
            OpenNewProjectSheet();
        }
    }
    {
        ButtonDescriptor OpenDiskBtn {};
        OpenDiskBtn.Label    = "Open from disk";
        OpenDiskBtn.Tone     = ButtonTone::Primary;
        OpenDiskBtn.FontSize = 11.0f;
        if (CapsuleButton::Construct(Surface, OpenDiskRect, OpenDiskBtn, Pointer).Pressed)
        {
            const auto ChosenFile = InvokeNativeOsDialog(false);
            if (!ChosenFile.empty())
            {
                (void)RegisterDiskPathInCollection(ChosenFile, true);
            }
            else
            {
                DiskPathModalOpen = true;
            }
        }
    }

    // 3. Filter & Sort Collection
    const std::string QueryLower = LowercaseString(SearchInput.Text);
    std::vector<std::size_t> FilteredIndices {};
    FilteredIndices.reserve(Collection.size());
    for (std::size_t Idx = 0u; Idx < Collection.size(); ++Idx)
    {
        const auto& E = Collection[Idx];
        if (!QueryLower.empty())
        {
            const std::string Haystack = LowercaseString(E.Title + " " + E.Suffix + " " + QueryCategoryCaption(E.Category));
            if (Haystack.find(QueryLower) == std::string::npos)
            {
                continue;
            }
        }
        if (ActiveFilter == BrowserFilterMode::Starred && !E.Starred) continue;
        if (ActiveFilter == BrowserFilterMode::Projects && E.Category != BrowserEntryCategory::Project) continue;
        if (ActiveFilter == BrowserFilterMode::Cad && E.Category != BrowserEntryCategory::Cad) continue;
        if (ActiveFilter == BrowserFilterMode::Geometry && E.Category != BrowserEntryCategory::Geometry) continue;
        FilteredIndices.push_back(Idx);
    }
    if (SortModeIndex == 1u)
    {
        std::sort(
            FilteredIndices.begin(),
            FilteredIndices.end(),
            [&](std::size_t A, std::size_t B) { return Collection[A].Title < Collection[B].Title; });
    }

    // 4. Center Library Body
    const Rectangle2D MainBody { MainX, HeaderH, MainW, LogicalBounds.Height - HeaderH };
    Surface.PushClip(MainBody);

    if (MainBody.Contains({ Pointer.X, Pointer.Y }))
    {
        const float Wheel = Surface.QueryMouseWheelY();
        if (std::fabs(Wheel) > 0.01f)
        {
            LibraryScrollY = std::max(0.0f, LibraryScrollY - Wheel * 38.0f);
        }
    }

    const float PadX     = std::clamp(MainW * 0.04f, 22.0f, 44.0f);
    const float ContentX = MainX + PadX;
    const float ContentW = MainW - PadX * 2.0f;
    const float HeadingY = HeaderH + 28.0f - LibraryScrollY;

    const char* SectionTitle = "Your workspace";
    switch (ActiveFilter)
    {
    case BrowserFilterMode::Recent:   SectionTitle = "Recently opened"; break;
    case BrowserFilterMode::Starred:  SectionTitle = "Starred files";   break;
    case BrowserFilterMode::Projects: SectionTitle = "Projects";        break;
    case BrowserFilterMode::Cad:      SectionTitle = "CAD";             break;
    case BrowserFilterMode::Geometry: SectionTitle = "Geometry";        break;
    default: break;
    }
    Surface.Text({ ContentX, HeadingY + 4.0f }, 17.0f, P.Ink, SectionTitle);

    char CountCaption[48]{};
    std::snprintf(
        CountCaption,
        sizeof(CountCaption),
        "%zu %s",
        FilteredIndices.size(),
        FilteredIndices.size() == 1u ? "file" : "files");
    const float TitleWidth = Surface.MeasureTextWidth(17.0f, SectionTitle);
    Surface.Text({ ContentX + TitleWidth + 14.0f, HeadingY + 9.0f }, 10.5f, P.Muted, CountCaption);

    const Rectangle2D SortBtnRect   { ContentX + ContentW - 126.0f, HeadingY, 126.0f, 30.0f };
    const Rectangle2D SearchBoxRect { SortBtnRect.X - 188.0f, HeadingY, 178.0f, 30.0f };

    (void)TextInputField::Construct(Surface, SearchBoxRect, SearchInput, Pointer, 11.5f);
    if (!SearchInput.Active && SearchInput.Length == 0u)
    {
        Surface.Text({ SearchBoxRect.X + 11.0f, SearchBoxRect.Y + 8.5f }, 11.0f, P.Faint, "Search files...");
    }

    constexpr std::array<const char*, 2> SortLabels = {{ "Last opened", "Name A-Z" }};
    if (DropdownSelector::ConstructButton(Surface, SortBtnRect, SortLabels[SortModeIndex], SortDropdownOpen, Pointer).Pressed)
    {
        SortDropdownOpen = !SortDropdownOpen;
    }

    // 2-Column Tile Grid matching PanelLayout.css (.cards { grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 22px })
    const float GridTopY = HeadingY + 54.0f;
    const float Gap      = 22.0f;
    const uint32_t Columns = (ContentW >= 520.0f) ? 2u : 1u;
    const float TileW    = (ContentW - Gap * static_cast<float>(Columns - 1u)) / static_cast<float>(Columns);
    const float PreviewH = std::round(TileW / 1.65f);
    const float TileH    = PreviewH + 86.0f;

    ControlPointer GridPointer = Pointer;
    if (SortDropdownOpen)
    {
        GridPointer.Pressed = false;
    }

    if (FilteredIndices.empty())
    {
        const Rectangle2D EmptyBox { ContentX, GridTopY + 30.0f, ContentW, 130.0f };
        Surface.TextCentered({ EmptyBox.X, EmptyBox.Y, EmptyBox.Width, 28.0f }, 15.0f, P.Ink, "No matching files");
        Surface.TextCentered({ EmptyBox.X, EmptyBox.Y + 30.0f, EmptyBox.Width, 22.0f }, 11.5f, P.Muted, "Try another name or choose a different format.");
        ButtonDescriptor ClearBtn {};
        ClearBtn.Label = "Clear search";
        ClearBtn.Tone  = ButtonTone::Secondary;
        if (CapsuleButton::Construct(Surface, { ContentX + (ContentW - 120.0f) * 0.5f, EmptyBox.Y + 66.0f, 120.0f, 30.0f }, ClearBtn, GridPointer).Pressed)
        {
            SearchInput.Begin("");
            ActiveFilter = BrowserFilterMode::AllFiles;
        }
    }
    else
    {
        for (std::size_t Pos = 0u; Pos < FilteredIndices.size(); ++Pos)
        {
            const std::size_t EntryIdx = FilteredIndices[Pos];
            auto& Entry                = Collection[EntryIdx];

            const uint32_t Col = static_cast<uint32_t>(Pos) % Columns;
            const uint32_t Row = static_cast<uint32_t>(Pos) / Columns;
            const Rectangle2D TileRect {
                ContentX + static_cast<float>(Col) * (TileW + Gap),
                GridTopY + static_cast<float>(Row) * (TileH + Gap),
                TileW,
                TileH,
            };

            if (TileRect.Y + TileRect.Height < HeaderH || TileRect.Y > LogicalBounds.Height)
            {
                continue;
            }

            const Rectangle2D StarRect { TileRect.X + TileRect.Width - 36.0f, TileRect.Y + 9.0f, 27.0f, 27.0f };
            const bool StarHover = StarRect.Contains({ GridPointer.X, GridPointer.Y });
            const bool TileHover = TileRect.Contains({ GridPointer.X, GridPointer.Y }) && !StarHover;

            Surface.FillRectangle(TileRect, P.PanelBg, 12.0f);

            const Rectangle2D ImgRect { TileRect.X + 1.0f, TileRect.Y + 1.0f, TileRect.Width - 2.0f, PreviewH };
            Surface.FillRectangle(ImgRect, P.TileBg, 11.0f);
            if (Entry.ThumbnailTex != nullptr)
            {
                Surface.FillTexturedRectangle(ImgRect, Entry.ThumbnailTex.get(), { 0.0f, 0.0f }, { 1.0f, 1.0f }, 10.0f);
            }

            if (Entry.LocalDiskEntry)
            {
                const Rectangle2D LocalTag { ImgRect.X + 10.0f, ImgRect.Y + ImgRect.Height - 24.0f, 68.0f, 17.0f };
                Surface.FillRectangle(LocalTag, { 0u, 0u, 0u, 170u }, 4.0f);
                Surface.TextCentered(LocalTag, 8.5f, { 235u, 240u, 235u, 230u }, "LOCAL FILE");
            }

            // Star toggle button (crisp 10-vertex SVG star matching Experimental/ProjectBrowser/index.html)
            Surface.FillRectangle(StarRect, { 0u, 0u, 0u, 150u }, 7.0f);
            Surface.StrokeRectangle(StarRect, { 255u, 255u, 255u, 36u }, 7.0f, 1.0f);
            ConstructStarPolygon(
                Surface,
                StarRect,
                Entry.Starred,
                Entry.Starred ? ColorRgba{ 227u, 217u, 181u, 255u } : ColorRgba{ 255u, 255u, 255u, 175u });
            if (StarHover && GridPointer.Pressed)
            {
                Entry.Starred = !Entry.Starred;
            }
            else if (TileHover && GridPointer.Pressed)
            {
                ActivateEntryByIndex(static_cast<int32_t>(EntryIdx));
            }

            // Tile text block
            const float TextY = TileRect.Y + PreviewH + 13.0f;
            Surface.Text({ TileRect.X + 15.0f, TextY }, 13.0f, P.Ink, Entry.Title.c_str());
            const float TitleW = Surface.MeasureTextWidth(13.0f, Entry.Title.c_str());
            Surface.Text({ TileRect.X + 15.0f + TitleW + 8.0f, TextY + 3.0f }, 9.5f, P.Faint, Entry.Suffix.c_str());
            Surface.Text({ TileRect.X + 15.0f, TextY + 21.0f }, 10.0f, P.Muted, Entry.SummaryCaption.c_str());

            const float MetaLineY = TileRect.Y + TileRect.Height - 28.0f;
            Surface.StrokeLine({ TileRect.X + 15.0f, MetaLineY }, { TileRect.X + TileRect.Width - 15.0f, MetaLineY }, P.Line, 1.0f);
            Surface.FillRectangle({ TileRect.X + 15.0f, MetaLineY + 11.0f, 5.5f, 5.5f }, QueryCategoryDot(Entry.Category, P), 2.0f);
            Surface.Text({ TileRect.X + 26.0f, MetaLineY + 7.0f }, 9.5f, P.Faint, QueryCategoryCaption(Entry.Category));
            Surface.TextRightAligned({ TileRect.X, MetaLineY + 7.0f, TileRect.Width - 15.0f, 16.0f }, 9.5f, P.Faint, Entry.LastOpenedText.c_str());

            Surface.StrokeRectangle(TileRect, TileHover ? P.Faint : P.Line, 12.0f, 1.0f);
        }
    }

    if (SortDropdownOpen)
    {
        uint32_t ChosenSort = SortModeIndex;
        const auto DropContact = DropdownSelector::ConstructOptionSheet(
            Surface,
            SortBtnRect,
            SortLabels.data(),
            2u,
            SortModeIndex,
            Pointer,
            ChosenSort);
        if (DropContact.Pressed)
        {
            SortModeIndex    = ChosenSort;
            SortDropdownOpen = false;
        }
        else if (Pointer.Pressed && !SortBtnRect.Contains({ Pointer.X, Pointer.Y }))
        {
            SortDropdownOpen = false;
        }
    }

    Surface.PopClip();

    // 5. Right Inspector Pane (302px / 44px collapsed)
    const Rectangle2D RightPane { RightX, 0.0f, RightW, LogicalBounds.Height };
    Surface.FillRectangle(RightPane, P.PanelBg, 0.0f);
    Surface.StrokeLine({ RightX, 0.0f }, { RightX, LogicalBounds.Height }, P.Line, 1.0f);
    Surface.StrokeLine({ RightX, HeaderH }, { RightX + RightW, HeaderH }, P.Line, 1.0f);

    if (RightPaneCollapsed)
    {
        ButtonDescriptor ExpandRight {};
        ExpandRight.Label    = "<";
        ExpandRight.Tone     = ButtonTone::Ghost;
        ExpandRight.FontSize = 13.0f;
        if (CapsuleButton::Construct(Surface, { RightX + 8.0f, 18.0f, 28.0f, 28.0f }, ExpandRight, Pointer).Pressed)
        {
            RightPaneCollapsed = false;
        }
    }
    else
    {
        Surface.Text({ RightX + 20.0f, 25.0f }, 10.5f, P.Muted, "Details");
        Surface.Text({ RightX + 74.0f, 25.0f }, 10.5f, P.Faint, "Comments");
        Surface.Text({ RightX + 144.0f, 25.0f }, 10.5f, P.Faint, "History");

        ButtonDescriptor CollapseRight {};
        CollapseRight.Label    = ">";
        CollapseRight.Tone     = ButtonTone::Ghost;
        CollapseRight.FontSize = 12.5f;
        if (CapsuleButton::Construct(Surface, { RightX + RightW - 36.0f, 18.0f, 26.0f, 28.0f }, CollapseRight, Pointer).Pressed)
        {
            RightPaneCollapsed = true;
        }

        Surface.Text({ RightX + 24.0f, HeaderH + 36.0f }, 12.0f, P.Ink, "Select a file to preview it.");
        Surface.Text({ RightX + 24.0f, HeaderH + 60.0f }, 10.5f, P.Muted, "Click any project or geometry tile to");
        Surface.Text({ RightX + 24.0f, HeaderH + 76.0f }, 10.5f, P.Muted, "inspect levels, notes, and revisions, or");
        Surface.Text({ RightX + 24.0f, HeaderH + 92.0f }, 10.5f, P.Muted, "launch directly into Frontier / UVWorkspace.");

        Surface.Text({ RightX + 24.0f, HeaderH + 134.0f }, 9.0f, P.Faint, "ACTIVE HOST WORKSPACES");
        float CapY = HeaderH + 156.0f;
        for (const auto& Cap : CapabilityRegister.QueryAllCapabilities())
        {
            const Rectangle2D CapBox { RightX + 20.0f, CapY, RightW - 40.0f, 48.0f };
            Surface.FillRectangle(CapBox, P.Control, 7.0f);
            Surface.StrokeRectangle(CapBox, P.Line, 7.0f, 1.0f);
            Surface.Text({ CapBox.X + 12.0f, CapBox.Y + 9.0f }, 11.0f, P.Ink, Cap.ActionTitle.c_str());
            Surface.Text({ CapBox.X + 12.0f, CapBox.Y + 27.0f }, 9.0f, P.Muted, Cap.Identifier == "Frontier" ? "World GI + Outliner + ForceFields" : "Dual 2D UDIM + 3D Surface");
            CapY += 56.0f;
        }
    }
}

void ProjectBrowser::RenderReaderMode(
    PixelSpace&           Surface,
    Rectangle2D           LogicalBounds,
    const ControlPointer& Pointer,
    double                DeltaSeconds)
{
    (void)DeltaSeconds;
    auto& Entry = Collection[static_cast<std::size_t>(ActiveEntryIndex)];
    const BrowserPalette P = QueryBrowserPalette(ActiveTheme);
    Surface.FillRectangle(LogicalBounds, P.MainBg, 0.0f);

    constexpr float HeaderH = 64.0f;
    const float     LeftW   = LeftRailCollapsed ? 44.0f : 204.0f;
    const float     RightW  = RightPaneCollapsed ? 44.0f : 302.0f;
    const float     MainX   = LeftW;
    const float     MainW   = std::max(280.0f, LogicalBounds.Width - LeftW - RightW);
    const float     RightX  = MainX + MainW;

    // 1. Left Sequence Rail (Levels / Views)
    Surface.FillRectangle({ 0.0f, 0.0f, LeftW, LogicalBounds.Height }, P.PanelBg, 0.0f);
    Surface.StrokeLine({ LeftW, 0.0f }, { LeftW, LogicalBounds.Height }, P.Line, 1.0f);
    Surface.StrokeLine({ 0.0f, HeaderH }, { LeftW, HeaderH }, P.Line, 1.0f);

    if (LeftRailCollapsed)
    {
        ButtonDescriptor ExpandBtn {};
        ExpandBtn.Label = ">";
        ExpandBtn.Tone  = ButtonTone::Ghost;
        if (CapsuleButton::Construct(Surface, { 8.0f, 18.0f, 28.0f, 28.0f }, ExpandBtn, Pointer).Pressed)
        {
            LeftRailCollapsed = false;
        }
    }
    else
    {
        const char* RailHeading = (Entry.Category == BrowserEntryCategory::Geometry) ? "Views" : "Levels";
        Surface.Text({ 16.0f, 24.0f }, 12.0f, P.Ink, RailHeading);
        char SeqCount[16]{};
        std::snprintf(SeqCount, sizeof(SeqCount), "%zu", Entry.ViewTitles.size());
        Surface.Text({ 64.0f, 25.5f }, 10.0f, P.Faint, SeqCount);

        ButtonDescriptor CollapseBtn {};
        CollapseBtn.Label = "<";
        CollapseBtn.Tone  = ButtonTone::Ghost;
        if (CapsuleButton::Construct(Surface, { LeftW - 36.0f, 18.0f, 26.0f, 28.0f }, CollapseBtn, Pointer).Pressed)
        {
            LeftRailCollapsed = true;
        }

        float SeqY = HeaderH + 22.0f;
        const float ThumbW = LeftW - 32.0f;
        const float ThumbH = std::round(ThumbW / 1.48f);
        for (std::size_t VIdx = 0u; VIdx < Entry.ViewTitles.size(); ++VIdx)
        {
            const Rectangle2D ThumbRect { 16.0f, SeqY, ThumbW, ThumbH };
            const bool Current = (ActiveViewIndex == VIdx);
            const bool Hovered = ThumbRect.Contains({ Pointer.X, Pointer.Y });

            Surface.FillRectangle(ThumbRect, P.TileBg, 6.0f);
            ImTextureData* SeqTex = (VIdx < ReaderViewThumbs.size() && ReaderViewThumbs[VIdx] != nullptr)
                                        ? ReaderViewThumbs[VIdx].get()
                                        : Entry.ThumbnailTex.get();
            if (SeqTex != nullptr)
            {
                Surface.FillTexturedRectangle(ThumbRect, SeqTex, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 6.0f);
            }
            Surface.StrokeRectangle(ThumbRect, Current ? P.Ink : (Hovered ? P.Muted : P.Line), 6.0f, Current ? 2.0f : 1.0f);
            Surface.TextCentered({ 16.0f, SeqY + ThumbH + 6.0f, ThumbW, 16.0f }, 10.5f, Current ? P.Ink : P.Muted, Entry.ViewTitles[VIdx].c_str());

            if (Hovered && Pointer.Pressed)
            {
                ActiveViewIndex = static_cast<uint32_t>(VIdx);
                RefreshReaderPreviewTexture();
            }
            SeqY += ThumbH + 34.0f;
        }
    }

    // 2. Center Reader Header + Capability-Filtered Launch Actions
    Surface.FillRectangle({ MainX, 0.0f, MainW, HeaderH }, P.PanelBg, 0.0f);
    Surface.StrokeLine({ MainX, HeaderH }, { MainX + MainW, HeaderH }, P.Line, 1.0f);

    ButtonDescriptor BackBtn {};
    BackBtn.Label    = "<-";
    BackBtn.Tone     = ButtonTone::Secondary;
    BackBtn.FontSize = 11.0f;
    if (CapsuleButton::Construct(Surface, { MainX + 16.0f, 17.0f, 36.0f, 30.0f }, BackBtn, Pointer).Pressed)
    {
        ReturnToLibrary();
        return;
    }

    const std::string FullName = Entry.Title + Entry.Suffix;
    Surface.Text({ MainX + 62.0f, 24.0f }, 12.5f, P.Ink, FullName.c_str());

    const auto PermittedCaps = CapabilityRegister.QueryPermittedForPath(Entry.FilePath);
    float ActionRightX = MainX + MainW - 16.0f;

    for (std::size_t CapIdx = 0u; CapIdx < PermittedCaps.size(); ++CapIdx)
    {
        const auto& Cap = PermittedCaps[PermittedCaps.size() - 1u - CapIdx];
        const bool PrimaryAction = (CapIdx == PermittedCaps.size() - 1u);
        const float BtnW =
            (Cap.Target == Workspaces::HostWorkspaceTarget::PatternWorkspace ||
             Cap.Target == Workspaces::HostWorkspaceTarget::StampWorkspace ||
             Cap.Target == Workspaces::HostWorkspaceTarget::BakingWorkspace)
                ? 172.0f
                : ((Cap.Target == Workspaces::HostWorkspaceTarget::UVWorkspace) ? 156.0f : 138.0f);
        ActionRightX -= BtnW;
        const Rectangle2D LaunchRect { ActionRightX, 16.0f, BtnW, 32.0f };

        ButtonDescriptor LaunchBtn {};
        LaunchBtn.Label    = Cap.ActionTitle.c_str();
        LaunchBtn.Tone     = PrimaryAction ? ButtonTone::Primary : ButtonTone::Secondary;
        LaunchBtn.FontSize = 10.5f;
        if (CapsuleButton::Construct(Surface, LaunchRect, LaunchBtn, Pointer).Pressed && LaunchCallback)
        {
            BrowserWorkspaceLaunchDirective Directive {};
            Directive.TargetWorkspace  = Cap.Target;
            Directive.ProjectTitle     = Entry.Title;
            Directive.SelectedFilePath = Entry.FilePath;
            LaunchCallback(Directive);
            return;
        }
        ActionRightX -= 8.0f;
    }

    // Revision selector
    const Rectangle2D RevBtnRect { ActionRightX - 134.0f, 17.0f, 134.0f, 30.0f };
    if (RevBtnRect.X > MainX + 210.0f && !Entry.RevisionTitles.empty())
    {
        char RevCaption[48]{};
        const std::size_t RevNumber = Entry.RevisionTitles.size() - std::min<std::size_t>(ActiveRevisionIndex, Entry.RevisionTitles.size() - 1u);
        std::snprintf(RevCaption, sizeof(RevCaption), "Version %zu%s", RevNumber, ActiveRevisionIndex == 0u ? " · Current" : "");
        if (DropdownSelector::ConstructButton(Surface, RevBtnRect, RevCaption, RevisionDropdownOpen, Pointer).Pressed)
        {
            RevisionDropdownOpen = !RevisionDropdownOpen;
        }
    }

    // 3. Center Reader Preview Canvas & Zoom Toolbar
    const float PadX     = std::clamp(MainW * 0.04f, 20.0f, 40.0f);
    const float PreviewX = MainX + PadX;
    const float PreviewW = MainW - PadX * 2.0f;
    const float EyebrowY = HeaderH + 22.0f;

    char EyebrowBuf[64]{};
    std::snprintf(
        EyebrowBuf,
        sizeof(EyebrowBuf),
        "%s %02u · VERSION %zu",
        (Entry.Category == BrowserEntryCategory::Geometry) ? "VIEW" : "LEVEL",
        ActiveViewIndex + 1u,
        Entry.RevisionTitles.empty() ? std::size_t{ 1u } : (Entry.RevisionTitles.size() - ActiveRevisionIndex));
    Surface.Text({ PreviewX, EyebrowY }, 9.0f, P.Muted, EyebrowBuf);

    const char* ViewName = (ActiveViewIndex < Entry.ViewTitles.size()) ? Entry.ViewTitles[ActiveViewIndex].c_str() : Entry.Title.c_str();
    Surface.Text({ PreviewX, EyebrowY + 16.0f }, 20.0f, P.Ink, ViewName);
    Surface.TextRightAligned({ PreviewX, EyebrowY + 20.0f, PreviewW, 18.0f }, 10.0f, P.Muted, "Tier 1 GTX Calibrated Preview");

    const float CanvasTopY = EyebrowY + 50.0f;
    const float MaxCanvasH = std::max(210.0f, LogicalBounds.Height - CanvasTopY - 118.0f);
    const float CanvasH    = std::min(MaxCanvasH, std::round(PreviewW / 1.52f));
    const Rectangle2D CanvasRect { PreviewX, CanvasTopY, PreviewW, CanvasH };

    Surface.FillRectangle(CanvasRect, P.TileBg, 9.0f);
    if (ReaderPreviewTex != nullptr)
    {
        const float ZoomFactor = std::clamp(ReaderZoomPercent / 100.0f, 0.5f, 2.0f);
        const float HalfSpan   = 0.5f / ZoomFactor;
        const Point2D UvMin { std::clamp(0.5f - HalfSpan, 0.0f, 0.45f), std::clamp(0.5f - HalfSpan, 0.0f, 0.45f) };
        const Point2D UvMax { std::clamp(0.5f + HalfSpan, 0.55f, 1.0f), std::clamp(0.5f + HalfSpan, 0.55f, 1.0f) };
        Surface.FillTexturedRectangle(CanvasRect, ReaderPreviewTex.get(), UvMin, UvMax, 8.0f);
    }
    Surface.StrokeRectangle(CanvasRect, P.Line, 9.0f, 1.0f);

    const float CaptionY = CanvasRect.Y + CanvasRect.Height + 10.0f;
    Surface.Text({ PreviewX, CaptionY }, 9.5f, P.Faint, Entry.Description.c_str());

    // Viewer navigation & zoom bar (<- | 1 / 3 | -> | - | 100% | + | Fit)
    const float BarW = 308.0f;
    const Rectangle2D BarRect { MainX + (MainW - BarW) * 0.5f, CaptionY + 24.0f, BarW, 34.0f };
    Surface.FillRectangle(BarRect, P.PanelBg, 10.0f);
    Surface.StrokeRectangle(BarRect, P.Line, 10.0f, 1.0f);

    ButtonDescriptor PrevBtn { "<", ButtonTone::Ghost, 11.0f, 6.0f };
    if (CapsuleButton::Construct(Surface, { BarRect.X + 6.0f, BarRect.Y + 4.0f, 28.0f, 26.0f }, PrevBtn, Pointer).Pressed && !Entry.ViewTitles.empty())
    {
        ActiveViewIndex = (ActiveViewIndex == 0u) ? static_cast<uint32_t>(Entry.ViewTitles.size() - 1u) : (ActiveViewIndex - 1u);
        RefreshReaderPreviewTexture();
    }
    char StepBuf[24]{};
    std::snprintf(StepBuf, sizeof(StepBuf), "%u / %zu", ActiveViewIndex + 1u, std::max<std::size_t>(1u, Entry.ViewTitles.size()));
    Surface.TextCentered({ BarRect.X + 36.0f, BarRect.Y, 48.0f, BarRect.Height }, 10.0f, P.Ink, StepBuf);

    ButtonDescriptor NextBtn { ">", ButtonTone::Ghost, 11.0f, 6.0f };
    if (CapsuleButton::Construct(Surface, { BarRect.X + 86.0f, BarRect.Y + 4.0f, 28.0f, 26.0f }, NextBtn, Pointer).Pressed && !Entry.ViewTitles.empty())
    {
        ActiveViewIndex = (ActiveViewIndex + 1u) % static_cast<uint32_t>(Entry.ViewTitles.size());
        RefreshReaderPreviewTexture();
    }

    Surface.StrokeLine({ BarRect.X + 122.0f, BarRect.Y + 8.0f }, { BarRect.X + 122.0f, BarRect.Y + 26.0f }, P.Line, 1.0f);

    ButtonDescriptor ZoomOutBtn { "-", ButtonTone::Ghost, 12.0f, 6.0f };
    if (CapsuleButton::Construct(Surface, { BarRect.X + 128.0f, BarRect.Y + 4.0f, 28.0f, 26.0f }, ZoomOutBtn, Pointer).Pressed)
    {
        ReaderZoomPercent = std::max(50.0f, ReaderZoomPercent - 25.0f);
    }
    char ZoomBuf[16]{};
    std::snprintf(ZoomBuf, sizeof(ZoomBuf), "%.0f%%", ReaderZoomPercent);
    Surface.TextCentered({ BarRect.X + 158.0f, BarRect.Y, 48.0f, BarRect.Height }, 10.0f, P.Ink, ZoomBuf);

    ButtonDescriptor ZoomInBtn { "+", ButtonTone::Ghost, 12.0f, 6.0f };
    if (CapsuleButton::Construct(Surface, { BarRect.X + 208.0f, BarRect.Y + 4.0f, 28.0f, 26.0f }, ZoomInBtn, Pointer).Pressed)
    {
        ReaderZoomPercent = std::min(200.0f, ReaderZoomPercent + 25.0f);
    }

    ButtonDescriptor FitBtn { "Fit", ButtonTone::Ghost, 10.0f, 8.0f };
    if (CapsuleButton::Construct(Surface, { BarRect.X + 244.0f, BarRect.Y + 4.0f, 56.0f, 26.0f }, FitBtn, Pointer).Pressed)
    {
        ReaderZoomPercent = 100.0f;
    }

    // 4. Right Inspector (Details | Comments | History)
    Surface.FillRectangle({ RightX, 0.0f, RightW, LogicalBounds.Height }, P.PanelBg, 0.0f);
    Surface.StrokeLine({ RightX, 0.0f }, { RightX, LogicalBounds.Height }, P.Line, 1.0f);
    Surface.StrokeLine({ RightX, HeaderH }, { RightX + RightW, HeaderH }, P.Line, 1.0f);

    if (RightPaneCollapsed)
    {
        ButtonDescriptor ExpandRight {};
        ExpandRight.Label = "<";
        ExpandRight.Tone  = ButtonTone::Ghost;
        if (CapsuleButton::Construct(Surface, { RightX + 8.0f, 18.0f, 28.0f, 28.0f }, ExpandRight, Pointer).Pressed)
        {
            RightPaneCollapsed = false;
        }
    }
    else
    {
        constexpr std::array<const char*, 3> TabNames = {{ "Details", "Comments", "History" }};
        uint32_t ChosenTab = static_cast<uint32_t>(ActiveInspectorTab);
        if (CapsuleButton::ConstructSegmented(
                Surface,
                { RightX + 14.0f, 18.0f, RightW - 56.0f, 28.0f },
                TabNames.data(),
                3u,
                ChosenTab,
                Pointer,
                ChosenTab).Pressed)
        {
            ActiveInspectorTab = static_cast<BrowserInspectorTab>(ChosenTab);
        }

        ButtonDescriptor CollapseRight {};
        CollapseRight.Label = ">";
        CollapseRight.Tone  = ButtonTone::Ghost;
        if (CapsuleButton::Construct(Surface, { RightX + RightW - 36.0f, 18.0f, 26.0f, 28.0f }, CollapseRight, Pointer).Pressed)
        {
            RightPaneCollapsed = true;
        }

        const float InspX = RightX + 20.0f;
        const float InspW = RightW - 40.0f;
        float       InspY = HeaderH + 22.0f;

        if (ActiveInspectorTab == BrowserInspectorTab::Details)
        {
            if (Entry.ThumbnailTex != nullptr)
            {
                const Rectangle2D HeroRect { InspX, InspY, InspW, std::round(InspW / 1.85f) };
                Surface.FillTexturedRectangle(HeroRect, Entry.ThumbnailTex.get(), { 0.0f, 0.0f }, { 1.0f, 1.0f }, 6.0f);
                Surface.StrokeRectangle(HeroRect, P.Line, 6.0f, 1.0f);
                InspY += HeroRect.Height + 16.0f;
            }

            Surface.Text({ InspX, InspY }, 14.0f, P.Ink, Entry.Title.c_str());
            InspY += 22.0f;
            Surface.Text({ InspX, InspY }, 10.0f, P.Muted, Entry.SummaryCaption.c_str());
            InspY += 28.0f;

            auto ConstructDetailPair = [&](const char* Key, const char* Val)
            {
                Surface.Text({ InspX, InspY }, 9.5f, P.Muted, Key);
                Surface.TextRightAligned({ InspX, InspY, InspW, 15.0f }, 9.5f, P.Ink, Val);
                InspY += 22.0f;
            };
            ConstructDetailPair("Format", Entry.Suffix.c_str());
            ConstructDetailPair("Category", QueryCategoryCaption(Entry.Category));
            ConstructDetailPair("Last opened", Entry.LastOpenedText.c_str());

            InspY += 10.0f;
            Surface.Text({ InspX, InspY }, 9.0f, P.Faint, "FILE PATH");
            InspY += 16.0f;
            const Rectangle2D PathBox { InspX, InspY, InspW, 38.0f };
            Surface.FillRectangle(PathBox, P.Control, 5.0f);
            Surface.StrokeRectangle(PathBox, P.Line, 5.0f, 1.0f);
            Surface.PushClip({ PathBox.X + 6.0f, PathBox.Y + 4.0f, PathBox.Width - 12.0f, PathBox.Height - 8.0f });
            const std::string PathStr = Entry.FilePath.generic_string();
            Surface.Text({ PathBox.X + 8.0f, PathBox.Y + 12.0f }, 9.0f, P.Muted, PathStr.c_str());
            Surface.PopClip();
            InspY += 52.0f;

            Surface.Text({ InspX, InspY }, 9.0f, P.Faint, "OPEN WITH");
            InspY += 18.0f;
            for (const auto& Cap : PermittedCaps)
            {
                ButtonDescriptor CapBtn {};
                CapBtn.Label    = Cap.ActionTitle.c_str();
                CapBtn.Tone     = (Cap.Target == PermittedCaps.front().Target) ? ButtonTone::Primary : ButtonTone::Secondary;
                CapBtn.FontSize = 10.5f;
                if (CapsuleButton::Construct(Surface, { InspX, InspY, InspW, 32.0f }, CapBtn, Pointer).Pressed && LaunchCallback)
                {
                    BrowserWorkspaceLaunchDirective Directive {};
                    Directive.TargetWorkspace  = Cap.Target;
                    Directive.ProjectTitle     = Entry.Title;
                    Directive.SelectedFilePath = Entry.FilePath;
                    LaunchCallback(Directive);
                    return;
                }
                InspY += 40.0f;
            }
        }
        else if (ActiveInspectorTab == BrowserInspectorTab::Comments)
        {
            for (const auto& Note : Entry.Notes)
            {
                const Rectangle2D Avatar { InspX, InspY, 24.0f, 24.0f };
                Surface.FillRectangle(Avatar, P.NestedBg, 6.0f);
                Surface.TextCentered(Avatar, 9.5f, P.Ink, Note.Initial.c_str());
                Surface.Text({ InspX + 32.0f, InspY + 1.0f }, 10.0f, P.Ink, Note.AuthorName.c_str());
                Surface.TextRightAligned({ InspX, InspY + 2.0f, InspW, 14.0f }, 8.5f, P.Faint, Note.Timestamp.c_str());
                Surface.Text({ InspX + 32.0f, InspY + 18.0f }, 9.5f, P.Muted, Note.BodyText.c_str());
                InspY += 48.0f;
            }

            const float FormY = LogicalBounds.Height - 104.0f;
            Surface.StrokeLine({ RightX, FormY - 12.0f }, { RightX + RightW, FormY - 12.0f }, P.Line, 1.0f);
            Surface.Text({ InspX, FormY }, 9.5f, P.Muted, "Add a review note");
            (void)TextInputField::Construct(Surface, { InspX, FormY + 18.0f, InspW - 68.0f, 30.0f }, CommentInput, Pointer, 10.5f);
            ButtonDescriptor SendBtn {};
            SendBtn.Label    = "Send";
            SendBtn.Tone     = ButtonTone::Primary;
            SendBtn.FontSize = 10.0f;
            if ((CapsuleButton::Construct(Surface, { InspX + InspW - 62.0f, FormY + 18.0f, 62.0f, 30.0f }, SendBtn, Pointer).Pressed ||
                 CommentInput.Finalized) &&
                CommentInput.Length > 0u)
            {
                Entry.Notes.push_back({ "You", "Y", CommentInput.Text, "Just now" });
                CommentInput.Begin("");
            }
        }
        else
        {
            for (std::size_t RIdx = 0u; RIdx < Entry.RevisionTitles.size(); ++RIdx)
            {
                const Rectangle2D RevRow { InspX, InspY, InspW, 52.0f };
                const bool SelectedRev = (ActiveRevisionIndex == RIdx);
                const bool HoveredRev  = RevRow.Contains({ Pointer.X, Pointer.Y });
                if (SelectedRev || HoveredRev)
                {
                    Surface.FillRectangle(RevRow, SelectedRev ? P.Selected : P.Control, 6.0f);
                }
                char VerTitle[48]{};
                std::snprintf(
                    VerTitle,
                    sizeof(VerTitle),
                    "Version %zu%s",
                    Entry.RevisionTitles.size() - RIdx,
                    RIdx == 0u ? " · CURRENT" : "");
                Surface.Text({ RevRow.X + 12.0f, RevRow.Y + 9.0f }, 10.5f, P.Ink, VerTitle);
                Surface.Text({ RevRow.X + 12.0f, RevRow.Y + 27.0f }, 9.5f, P.Muted, Entry.RevisionTitles[RIdx].c_str());
                if (HoveredRev && Pointer.Pressed)
                {
                    ActiveRevisionIndex = static_cast<uint32_t>(RIdx);
                    RefreshReaderPreviewTexture();
                }
                InspY += 58.0f;
            }
        }
    }

    if (RevisionDropdownOpen && !Entry.RevisionTitles.empty())
    {
        std::vector<const char*> RevPtrs {};
        RevPtrs.reserve(Entry.RevisionTitles.size());
        for (const auto& R : Entry.RevisionTitles)
        {
            RevPtrs.push_back(R.c_str());
        }
        uint32_t ChosenRev = ActiveRevisionIndex;
        if (DropdownSelector::ConstructOptionSheet(
                Surface,
                RevBtnRect,
                RevPtrs.data(),
                static_cast<uint32_t>(RevPtrs.size()),
                ActiveRevisionIndex,
                Pointer,
                ChosenRev).Pressed)
        {
            ActiveRevisionIndex  = ChosenRev;
            RevisionDropdownOpen = false;
            RefreshReaderPreviewTexture();
        }
        else if (Pointer.Pressed && !RevBtnRect.Contains({ Pointer.X, Pointer.Y }))
        {
            RevisionDropdownOpen = false;
        }
    }
}

void ProjectBrowser::RenderNewProjectModal(
    PixelSpace&           Surface,
    Rectangle2D           LogicalBounds,
    const ControlPointer& Pointer)
{
    const BrowserPalette P = QueryBrowserPalette(ActiveTheme);
    Surface.FillRectangle(LogicalBounds, { 0u, 0u, 0u, 175u }, 0.0f);

    const float ModalW = std::min(540.0f, LogicalBounds.Width - 40.0f);
    const float ModalH = 384.0f;
    const Rectangle2D Modal {
        (LogicalBounds.Width - ModalW) * 0.5f,
        (LogicalBounds.Height - ModalH) * 0.5f,
        ModalW,
        ModalH,
    };

    Surface.FillRectangle(Modal, P.PanelBg, 16.0f);
    Surface.StrokeRectangle(Modal, P.Line, 16.0f, 1.0f);

    const float X = Modal.X + 28.0f;
    const float W = Modal.Width - 56.0f;
    float       Y = Modal.Y + 24.0f;

    Surface.Text({ X, Y }, 9.0f, P.Muted, "NEW C++20 FRONTIER PROJECT");
    Y += 18.0f;
    Surface.Text({ X, Y }, 20.0f, P.Ink, "Create Project");
    Y += 34.0f;

    Surface.Text({ X, Y }, 10.0f, P.Muted, "Project Name (generates namespace & 3 CMake targets)");
    Y += 18.0f;
    (void)TextInputField::Construct(Surface, { X, Y, W, 32.0f }, DraftProjectNameInput, Pointer, 12.0f);
    Y += 44.0f;

    Surface.Text({ X, Y }, 10.0f, P.Muted, "Destination Directory (no fixed folder required)");
    Y += 18.0f;
    (void)TextInputField::Construct(Surface, { X, Y, W - 92.0f, 32.0f }, DraftDestinationDirInput, Pointer, 11.5f);
    ButtonDescriptor PickDirBtn {};
    PickDirBtn.Label    = "Browse...";
    PickDirBtn.Tone     = ButtonTone::Secondary;
    PickDirBtn.FontSize = 10.5f;
    if (CapsuleButton::Construct(Surface, { X + W - 84.0f, Y, 84.0f, 32.0f }, PickDirBtn, Pointer).Pressed)
    {
        const auto Picked = InvokeNativeOsDialog(true);
        if (!Picked.empty())
        {
            DraftDestinationDirInput.Begin(Picked.generic_string().c_str());
        }
    }
    Y += 46.0f;

    // Capability switches
    Surface.Text({ X, Y + 4.0f }, 11.0f, P.Ink, "Frontier World Editor");
    if (ExclusiveSelector::ConstructCompact(Surface, { X + 148.0f, Y, 44.0f, 22.0f }, DraftEnableFrontier, Pointer).Pressed)
    {
        DraftEnableFrontier = !DraftEnableFrontier;
    }
    Surface.Text({ X + 216.0f, Y + 4.0f }, 11.0f, P.Ink, "UVWorkspace");
    if (ExclusiveSelector::ConstructCompact(Surface, { X + 308.0f, Y, 44.0f, 22.0f }, DraftEnableUv, Pointer).Pressed)
    {
        DraftEnableUv = !DraftEnableUv;
    }
    Y += 36.0f;

    const std::string Pascal = Configuration::ProjectGenerator::SanitizePascalIdentifier(DraftProjectNameInput.Text);
    const std::string TargetPreview =
        (std::filesystem::path(DraftDestinationDirInput.Text) / Pascal / (Pascal + ".projectspace")).generic_string();
    const Rectangle2D PreviewBox { X, Y, W, 44.0f };
    Surface.FillRectangle(PreviewBox, P.Control, 7.0f);
    Surface.StrokeRectangle(PreviewBox, P.Line, 7.0f, 1.0f);
    Surface.Text({ X + 10.0f, Y + 7.0f }, 9.0f, P.Faint, ("Targets: " + Pascal + " | " + Pascal + "Editor | " + Pascal + "Shipping  (SessionOutput/)").c_str());
    Surface.Text({ X + 10.0f, Y + 24.0f }, 9.5f, P.Muted, TargetPreview.c_str());
    Y += 60.0f;

    ButtonDescriptor CancelBtn {};
    CancelBtn.Label    = "Cancel";
    CancelBtn.Tone     = ButtonTone::Secondary;
    CancelBtn.FontSize = 11.0f;
    if (CapsuleButton::Construct(Surface, { X, Y, 92.0f, 34.0f }, CancelBtn, Pointer).Pressed ||
        Surface.QueryKeyPressed(InterfaceKey::Escape, false))
    {
        NewProjectModalOpen = false;
        return;
    }

    ButtonDescriptor CreateOnlyBtn {};
    CreateOnlyBtn.Label    = "Create Only";
    CreateOnlyBtn.Tone     = ButtonTone::Secondary;
    CreateOnlyBtn.FontSize = 11.0f;
    if (CapsuleButton::Construct(Surface, { X + W - 284.0f, Y, 108.0f, 34.0f }, CreateOnlyBtn, Pointer).Pressed)
    {
        (void)TriggerCreateProjectFromDraft(false);
        return;
    }

    ButtonDescriptor CreateOpenBtn {};
    CreateOpenBtn.Label    = "Create & Open in Frontier";
    CreateOpenBtn.Tone     = ButtonTone::Primary;
    CreateOpenBtn.FontSize = 11.0f;
    if (CapsuleButton::Construct(Surface, { X + W - 168.0f, Y, 168.0f, 34.0f }, CreateOpenBtn, Pointer).Pressed)
    {
        (void)TriggerCreateProjectFromDraft(true);
        return;
    }
}

void ProjectBrowser::RenderDiskPathModal(
    PixelSpace&           Surface,
    Rectangle2D           LogicalBounds,
    const ControlPointer& Pointer)
{
    const BrowserPalette P = QueryBrowserPalette(ActiveTheme);
    Surface.FillRectangle(LogicalBounds, { 0u, 0u, 0u, 175u }, 0.0f);

    const float ModalW = std::min(500.0f, LogicalBounds.Width - 40.0f);
    const float ModalH = 224.0f;
    const Rectangle2D Modal {
        (LogicalBounds.Width - ModalW) * 0.5f,
        (LogicalBounds.Height - ModalH) * 0.5f,
        ModalW,
        ModalH,
    };

    Surface.FillRectangle(Modal, P.PanelBg, 16.0f);
    Surface.StrokeRectangle(Modal, P.Line, 16.0f, 1.0f);

    const float X = Modal.X + 28.0f;
    const float W = Modal.Width - 56.0f;
    float       Y = Modal.Y + 24.0f;

    Surface.Text({ X, Y }, 9.0f, P.Muted, "LOCAL FILESYSTEM");
    Y += 18.0f;
    Surface.Text({ X, Y }, 18.0f, P.Ink, "Open file or folder from disk");
    Y += 32.0f;
    Surface.Text({ X, Y }, 10.0f, P.Muted, "Enter path to a .projectspace, .obj, .UVSpace, or project directory:");
    Y += 18.0f;
    (void)TextInputField::Construct(Surface, { X, Y, W, 32.0f }, DiskPathInput, Pointer, 11.5f);
    Y += 50.0f;

    ButtonDescriptor CancelBtn {};
    CancelBtn.Label = "Cancel";
    CancelBtn.Tone  = ButtonTone::Secondary;
    if (CapsuleButton::Construct(Surface, { X, Y, 92.0f, 34.0f }, CancelBtn, Pointer).Pressed ||
        Surface.QueryKeyPressed(InterfaceKey::Escape, false))
    {
        DiskPathModalOpen = false;
        return;
    }

    ButtonDescriptor MountBtn {};
    MountBtn.Label = "Open Path";
    MountBtn.Tone  = ButtonTone::Primary;
    if (CapsuleButton::Construct(Surface, { X + W - 116.0f, Y, 116.0f, 34.0f }, MountBtn, Pointer).Pressed ||
        DiskPathInput.Finalized)
    {
        const std::filesystem::path Candidate(DiskPathInput.Text);
        if (RegisterDiskPathInCollection(Candidate, true))
        {
            DiskPathModalOpen = false;
        }
        else
        {
            StatusBannerMessage   = "No .projectspace or geometry files found at: " + Candidate.generic_string();
            StatusBannerRemaining = 4.5;
        }
    }
}

} // namespace Frontier::Editor::Explorer
