/*============================================================================================================================================
                                                        DESKTOPWORKSPACELAUNCH.CPP
============================================================================================================================================*/
// 🧩 Implementation of DesktopWorkspaceLaunch orchestrating GLFW 3.4, ImGuiOverlayRasterization, UVWorkspaceRasterizer & EditorLifecycle

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Lifecycle/DesktopWorkspaceLaunch.h"

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ControlCentre/ControlCentrePanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometricRaster/Rasterization/ImGuiOverlayRasterization.h"
#include "Engine/GeometricRaster/UV/UVWorkspaceRasterizer.h"
#include "Engine/Input/ImGuiInputAdapter.h"
#include "Engine/Input/InputExtension.h"
#include "Engine/Windowing/PlatformWindow.h"
#include "Engine/Workspaces/UV/UVWorkspaceHost.h"

#if FRONTIER_EDITOR_ENABLED
#include "Engine/Editor/Explorer/ContentBrowser.h"
#include "Engine/Lifecycle/EditorLifecycle.h"
#endif

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace Frontier::Lifecycle
{

namespace
{

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

DesktopResourcePaths ResolveDesktopResourcePaths(
    const std::filesystem::path& ExecutablePath,
    const std::filesystem::path& FallbackRepositoryRoot,
    const std::filesystem::path& FallbackShaderDirectory)
{
    std::vector<std::filesystem::path> SearchRoots {};
    auto AddPathAndAncestors = [&](std::filesystem::path Path)
    {
        if (Path.empty())
        {
            return;
        }
        std::error_code Error {};
        Path = std::filesystem::absolute(Path, Error).lexically_normal();
        if (Error)
        {
            return;
        }
        if (!std::filesystem::is_directory(Path, Error))
        {
            Path = Path.parent_path();
        }
        for (uint32_t Depth = 0u; !Path.empty() && Depth < 12u; ++Depth)
        {
            if (std::find(SearchRoots.begin(), SearchRoots.end(), Path) == SearchRoots.end())
            {
                SearchRoots.push_back(Path);
            }
            const std::filesystem::path Parent = Path.parent_path();
            if (Parent == Path)
            {
                break;
            }
            Path = Parent;
        }
    };

    AddPathAndAncestors(ExecutablePath);
    AddPathAndAncestors(std::filesystem::current_path());
    AddPathAndAncestors(FallbackRepositoryRoot);
    AddPathAndAncestors(FallbackShaderDirectory);

    DesktopResourcePaths Resolved { FallbackRepositoryRoot, FallbackShaderDirectory };
    for (const auto& Root : SearchRoots)
    {
        const bool HasGeometry = std::filesystem::exists(Root / "EngineContent/Geometry/ControlledIntegrator.GeometrySpace");
        const bool HasFontIndex = std::filesystem::exists(Root / "EngineContent/FontsArchives/FontRepository.FontSpace");
        if (HasGeometry || HasFontIndex)
        {
            Resolved.RepositoryRootPath = Root;
        }

        const std::filesystem::path ShaderFolder = Root / "CompiledShaders";
        if (std::filesystem::exists(ShaderFolder / "SurfaceShading.vert.spv") &&
            std::filesystem::exists(ShaderFolder / "UVTileRasterization.frag.spv"))
        {
            Resolved.CompiledShaderDirectory = ShaderFolder;
        }
        if ((HasGeometry || HasFontIndex) &&
            std::filesystem::exists(Root / "SurfaceShading.vert.spv") &&
            std::filesystem::exists(Root / "UVTileRasterization.frag.spv"))
        {
            Resolved.CompiledShaderDirectory = Root;
        }
    }
    return Resolved;
}

int LaunchDesktopWorkspace(
    DesktopLaunchOptions          InitialOptions,
    std::span<const char* const>  CommandLineArgs,
    const std::filesystem::path&  RepositoryRootPath,
    const std::filesystem::path&  CompiledShaderDir)
{
    Configuration::EngineConfiguration EngineConfig(
        InitialOptions.DefaultLaunchMode == Configuration::HostLaunchMode::StandaloneWorkspace
            ? Configuration::EngineConfiguration::ConstructStandaloneUVDefault()
            : Configuration::EngineConfiguration::ConstructFeatureSetDefault());

    std::vector<std::string_view> ArgViews {};
    ArgViews.reserve(CommandLineArgs.size());
    for (const char* Ptr : CommandLineArgs)
    {
        if (Ptr != nullptr)
        {
            ArgViews.emplace_back(Ptr);
        }
    }
    EngineConfig.ConfigureFromCommandLine(ArgViews);

    for (const char* RawArg : CommandLineArgs)
    {
        if (RawArg == nullptr)
        {
            continue;
        }
        const std::string_view Arg(RawArg);
        if (Arg.starts_with("--frames="))
        {
            InitialOptions.BoundedStepLimit = static_cast<uint32_t>(std::max(1, std::atoi(std::string(Arg.substr(9u)).c_str())));
        }
        else if (Arg.starts_with("--import="))
        {
            InitialOptions.StartupImportPath = std::filesystem::path(Arg.substr(9u));
        }
        else if (Arg.starts_with("--capture-png="))
        {
            InitialOptions.CapturePngOutputPath = std::filesystem::path(Arg.substr(14u));
        }
        else if (Arg == "--open-browser")
        {
            InitialOptions.OpenContentBrowser = true;
        }
    }

    GeometricRaster::VulkanInterchange DeviceInterchange {};
    std::string                          VkDiag {};
    if (!DeviceInterchange.InitializeInterchange(RepositoryRootPath, &VkDiag))
    {
        std::fprintf(stderr, "[FrontierHost] Vulkan initialization failed: %s\n", VkDiag.c_str());
        return 1;
    }

    GeometricRaster::UVWorkspaceRasterizer UVRasterizer {};
    if (!UVRasterizer.InitializeRasterizer(&DeviceInterchange, CompiledShaderDir))
    {
        std::fprintf(stderr, "[FrontierHost] UVWorkspaceRasterizer initialization failed.\n");
        return 1;
    }

#if !defined(__ANDROID__)
    Windowing::DesktopPlatformWindow DesktopWindow {};
    const auto WinDesc = Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
        InitialOptions.WindowTitle,
        InitialOptions.InitialWidthPx,
        InitialOptions.InitialHeightPx);
    if (!DesktopWindow.Initialize(WinDesc))
    {
        std::fprintf(stderr, "[FrontierHost] DesktopPlatformWindow initialization failed.\n");
        return 1;
    }

    // In headless GLFW_PLATFORM_NULL environments without an interactive display server, bound frame loop unless specified
    if (DesktopWindow.PlatformCategory() == Windowing::WindowPlatformCategory::GlfwNull &&
        InitialOptions.BoundedStepLimit == 0u)
    {
        InitialOptions.BoundedStepLimit = 4u;
    }
#endif

    IMGUI_CHECKVERSION();
    ImGuiContext* GuiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(GuiCtx);
    ImGuiIO& Io = ImGui::GetIO();
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // The Vulkan renderer is initialized after the presenter creates its render pass.
    Io.IniFilename  = nullptr;

    TypographyRepository Typography {};
    if (Typography.LoadRepository(RepositoryRootPath / "EngineContent/FontsArchives") > 0u)
    {
        TypographyRepository::AssignCurrent(&Typography);
        if (ImFont* TabFont = Typography.QueryTabStripFont())
        {
            Io.FontDefault = TabFont;
        }
    }

    Workspaces::UVWorkspaceHost UVHost {};
    UVHost.ConfigureLaunchPolicy(EngineConfig);
    (void)UVHost.AccessSession().InitializeGPUSolvers(&DeviceInterchange, CompiledShaderDir);
    (void)UVHost.AttachVulkanRasterizer(&UVRasterizer);

    if (!InitialOptions.StartupImportPath.empty())
    {
        std::string ImportDiag {};
        if (!UVHost.IntakeExternalFileDrop(InitialOptions.StartupImportPath, ImportDiag))
        {
            std::fprintf(stderr, "[FrontierHost] Startup import failed: %s\n", ImportDiag.c_str());
        }
    }

#if FRONTIER_EDITOR_ENABLED
    const bool UseFullEditorLifecycle = (EngineConfig.QueryLaunchMode() == Configuration::HostLaunchMode::FeatureSet);
    Lifecycle::EditorLifecycle EditorLoop {};
    Editor::Explorer::ContentBrowser StandaloneBrowser {};
#endif

    Components::HeadlessWorkspaceDockContainer StandaloneDock {};
    ControlCentrePanel                         StandaloneControlCentre {};
    Input::InputExtension                      InputCoordinator {};
    InputCoordinator.Initialize();

#if FRONTIER_EDITOR_ENABLED
    if (UseFullEditorLifecycle)
    {
        Lifecycle::EditorStartupConfiguration EditorCfg {};
        EditorCfg.WindowDescriptor   = Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
            InitialOptions.WindowTitle, InitialOptions.InitialWidthPx, InitialOptions.InitialHeightPx);
        EditorCfg.FontRepositoryRoot = RepositoryRootPath / "EngineContent/FontsArchives";
        EditorLoop.AttachUVWorkspaceHost(&UVHost);
        if (!EditorLoop.Initialize(EditorCfg))
        {
            std::fprintf(stderr, "[FrontierHost] EditorLifecycle initialization failed.\n");
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
        if (InitialOptions.OpenContentBrowser)
        {
            EditorLoop.AccessContentBrowser().AccessPanel().ConfigureShelfExtent(ContentShelfExtent::SplitHalf, true);
        }
    }
    else
#endif
    {
        ThemeRepository ThemeRepo {};
        (void)StandaloneDock.Initialize(
            Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
                InitialOptions.WindowTitle, InitialOptions.InitialWidthPx, InitialOptions.InitialHeightPx));
        Components::ConfigureImGuiWorkspaceStyle(StandaloneDock.ActiveTabMetrics());
        Components::ConfigureImGuiWorkspaceTheme(StandaloneDock.ActiveTabMetrics(), ThemeRepo);
        StandaloneControlCentre.AttachTypefaces(&Typography);
        StandaloneControlCentre.AccessDraftAppearance().Theme = ThemePreset::Dark;
        StandaloneControlCentre.ApplyDraftAppearanceImmediate();
        StandaloneControlCentre.AccessNotificationStream().ClearPending();
#if FRONTIER_EDITOR_ENABLED
        UVHost.AttachContentBrowser(&StandaloneBrowser);
        if (InitialOptions.OpenContentBrowser)
        {
            StandaloneBrowser.AccessPanel().ConfigureShelfExtent(ContentShelfExtent::SplitHalf, true);
        }
#endif
        UVHost.RegisterDockPanes(StandaloneDock);
    }

    GeometricRaster::ImGuiOverlayRasterization SwapchainPresenter {};
#if !defined(__ANDROID__)
    Windowing::DesktopPlatformWindow* PresenterWindow = &DesktopWindow;
#else
    Windowing::DesktopPlatformWindow* PresenterWindow = nullptr;
#endif
    if (!SwapchainPresenter.InitializeOverlay(
            &DeviceInterchange,
            CompiledShaderDir,
            PresenterWindow,
            InitialOptions.InitialWidthPx,
            InitialOptions.InitialHeightPx,
            &VkDiag))
    {
        std::fprintf(stderr, "[FrontierHost] ImGuiOverlayRasterization initialization failed: %s\n", VkDiag.c_str());
        ImGui::DestroyContext(GuiCtx);
        return 1;
    }

    uint32_t ExecutedSteps = 0u;
    auto     LastTick       = std::chrono::steady_clock::now();

    while (true)
    {
#if !defined(__ANDROID__)
        if (DesktopWindow.CloseRequested())
        {
            break;
        }
#endif
        if (InitialOptions.BoundedStepLimit > 0u && ExecutedSteps >= InitialOptions.BoundedStepLimit)
        {
            break;
        }

        const auto   NowTick = std::chrono::steady_clock::now();
        const double Dt      = std::clamp(std::chrono::duration<double>(NowTick - LastTick).count(), 1.0 / 240.0, 0.050);
        LastTick             = NowTick;

        uint32_t SurfaceW = InitialOptions.InitialWidthPx;
        uint32_t SurfaceH = InitialOptions.InitialHeightPx;

#if !defined(__ANDROID__)
        InputCoordinator.BeginTick();
        DesktopWindow.PollOperatingEvents(InputCoordinator);
        (void)InputCoordinator.TickExchange();
        Input::InputExtension::AssignActiveCoordinator(&InputCoordinator);

        const auto Extent = DesktopWindow.InspectExtent();
        if (Extent.SurfacePixelWidth == 0u || Extent.SurfacePixelHeight == 0u)
        {
            DesktopWindow.WaitOperatingEventsTimeout(0.01);
            continue;
        }
        SurfaceW = Extent.SurfacePixelWidth;
        SurfaceH = Extent.SurfacePixelHeight;
#if FRONTIER_EDITOR_ENABLED
        if (UseFullEditorLifecycle)
        {
            if (auto* HostSurface = EditorLoop.AccessDockContainer().AccessOperatingSurface(Components::HostWindowToken{ 0u }))
            {
                HostSurface->ResizeSurface(SurfaceW, SurfaceH, Extent.DpiScaleFactor);
            }
        }
        else
#endif
        {
            if (auto* HostSurface = StandaloneDock.AccessOperatingSurface(Components::HostWindowToken{ 0u }))
            {
                HostSurface->ResizeSurface(SurfaceW, SurfaceH, Extent.DpiScaleFactor);
            }
        }

        (void)Input::ImGuiInputAdapter::ForwardEditorExchangeToImGui(InputCoordinator, Io);

        for (const std::string& DropFile : InputCoordinator.ConsumeDroppedFilePaths())
        {
            std::string DropDiag {};
            (void)UVHost.IntakeExternalFileDrop(DropFile, DropDiag);
        }
#endif

        Io.DisplaySize = ImVec2(static_cast<float>(SurfaceW), static_cast<float>(SurfaceH));
        Io.DeltaTime   = static_cast<float>(Dt);

        SwapchainPresenter.PrepareImGuiFrame();
        ImGui::NewFrame();
#if FRONTIER_EDITOR_ENABLED
        if (UseFullEditorLifecycle)
        {
            (void)EditorLoop.ExecuteAuthoringTick(Dt);
            Input::InputExtension::AssignActiveCoordinator(&InputCoordinator);
            EditorLoop.PresentPrimaryEditorImGui(Dt);
        }
        else
#endif
        {
            Components::ConfigureImGuiWorkspaceTheme(
                StandaloneDock.ActiveTabMetrics(),
                StandaloneControlCentre.QueryThemeRepository());
            StandaloneDock.PresentWindowImGuiPanes(Components::HostWindowToken{ 0u });
            const Rectangle2D FullBounds { 0.0f, 0.0f, static_cast<float>(SurfaceW), static_cast<float>(SurfaceH) };
#if FRONTIER_EDITOR_ENABLED
            StandaloneBrowser.Render(FullBounds, Dt, 1.0f);
#endif
            StandaloneControlCentre.Render(FullBounds, Dt, static_cast<float>(1.0 / Dt));
        }
        ImGui::Render();

        if (!SwapchainPresenter.PresentImGuiBatches(ImGui::GetDrawData(), SurfaceW, SurfaceH))
        {
            const std::string Diagnostic(SwapchainPresenter.QueryLastPresentationDiagnostic());
            std::fprintf(stderr, "[FrontierHost] ImGui Vulkan presentation failed: %s\n", Diagnostic.c_str());
            SwapchainPresenter.TeardownOverlay();
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
        ++ExecutedSteps;
    }

    if (!InitialOptions.CapturePngOutputPath.empty())
    {
        std::vector<uint8_t> CapturedRgba {};
        uint32_t             CapW = 0u;
        uint32_t             CapH = 0u;
        if (!SwapchainPresenter.ReadbackLastPresentedRgba(CapturedRgba, CapW, CapH) ||
            !WriteLosslessRgbaPng(InitialOptions.CapturePngOutputPath, CapturedRgba, CapW, CapH))
        {
            std::fprintf(stderr, "[FrontierHost] Failed to capture presented swapchain PNG.\n");
            SwapchainPresenter.TeardownOverlay();
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }

    SwapchainPresenter.TeardownOverlay();
#if FRONTIER_EDITOR_ENABLED
    if (UseFullEditorLifecycle)
    {
        EditorLoop.Finalize();
    }
    else
#endif
    {
        StandaloneDock.Finalize();
    }
    UVHost.AccessSession().TeardownGPUSolvers();
    UVRasterizer.TeardownRasterizer();
    InputCoordinator.Finalize();
#if !defined(__ANDROID__)
    DesktopWindow.Finalize();
#endif
    TypographyRepository::AssignCurrent(nullptr);
    ImGui::DestroyContext(GuiCtx);
    return 0;
}

} // namespace Frontier::Lifecycle
