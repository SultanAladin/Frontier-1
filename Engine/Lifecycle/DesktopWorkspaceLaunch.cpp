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
#include "Engine/Input/InputExtension.h"
#include "Engine/Windowing/PlatformWindow.h"
#include "Engine/Workspaces/UV/UVWorkspaceHost.h"

#if FRONTIER_EDITOR_ENABLED
#include "Engine/Editor/Explorer/ContentBrowser.h"
#include "Engine/Lifecycle/EditorLifecycle.h"
#endif

#if !defined(__ANDROID__)
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#endif

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace Frontier::Lifecycle
{

namespace
{

struct GlfwSupplementalQueue
{
    GLFWcharfun                ChainCharCallback = nullptr;
    GLFWdropfun                ChainDropCallback = nullptr;
    std::vector<unsigned int>  CodePoints        {};
    std::vector<std::string>   DroppedPaths      {};
};

GlfwSupplementalQueue* ActiveSupplementalQueue = nullptr;

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

#if !defined(__ANDROID__)
void SynchronizeGlfwToImGui(GLFWwindow* Win, ImGuiIO& Io, GlfwSupplementalQueue& Queue)
{
    if (Win == nullptr)
    {
        return;
    }
    double MouseX = 0.0;
    double MouseY = 0.0;
    glfwGetCursorPos(Win, &MouseX, &MouseY);
    Io.AddMousePosEvent(static_cast<float>(MouseX), static_cast<float>(MouseY));
    Io.AddMouseButtonEvent(0, glfwGetMouseButton(Win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
    Io.AddMouseButtonEvent(1, glfwGetMouseButton(Win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
    Io.AddMouseButtonEvent(2, glfwGetMouseButton(Win, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS);

    const bool Ctrl  = glfwGetKey(Win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(Win, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    const bool Shift = glfwGetKey(Win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(Win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    const bool Alt   = glfwGetKey(Win, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(Win, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
    Io.AddKeyEvent(ImGuiMod_Ctrl, Ctrl);
    Io.AddKeyEvent(ImGuiMod_Shift, Shift);
    Io.AddKeyEvent(ImGuiMod_Alt, Alt);
    Io.AddKeyEvent(ImGuiKey_Escape, glfwGetKey(Win, GLFW_KEY_ESCAPE) == GLFW_PRESS);
    Io.AddKeyEvent(ImGuiKey_Enter, glfwGetKey(Win, GLFW_KEY_ENTER) == GLFW_PRESS);
    Io.AddKeyEvent(ImGuiKey_Backspace, glfwGetKey(Win, GLFW_KEY_BACKSPACE) == GLFW_PRESS);
    Io.AddKeyEvent(ImGuiKey_Delete, glfwGetKey(Win, GLFW_KEY_DELETE) == GLFW_PRESS);
    Io.AddKeyEvent(ImGuiKey_LeftArrow, glfwGetKey(Win, GLFW_KEY_LEFT) == GLFW_PRESS);
    Io.AddKeyEvent(ImGuiKey_RightArrow, glfwGetKey(Win, GLFW_KEY_RIGHT) == GLFW_PRESS);

    for (const unsigned int Cp : Queue.CodePoints)
    {
        Io.AddInputCharacter(Cp);
    }
    Queue.CodePoints.clear();
}
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                PACKAGED LAYOUT RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

// Redirects the content root and SPIR-V directory beside the executable when a packaged CompiledShaders/ directory exists there
[[nodiscard]] bool ResolvePackagedLayout(
    std::span<const char* const> CommandLineArgs,
    std::filesystem::path&       RepositoryRootPath,
    std::filesystem::path&       CompiledShaderDir)
{
    if (CommandLineArgs.empty() || CommandLineArgs[0] == nullptr)
    {
        return false;
    }

    std::error_code             PathFailure {};
    const std::filesystem::path ExecutablePath =
        std::filesystem::weakly_canonical(std::filesystem::absolute(std::filesystem::path(CommandLineArgs[0]), PathFailure), PathFailure);
    if (PathFailure || !ExecutablePath.has_parent_path())
    {
        return false;
    }

    const std::filesystem::path ExecutableDir     = ExecutablePath.parent_path();
    const std::filesystem::path PackagedShaderDir = ExecutableDir / "CompiledShaders";
    if (!std::filesystem::is_directory(PackagedShaderDir, PathFailure))
    {
        return false;
    }

    RepositoryRootPath = ExecutableDir;
    CompiledShaderDir  = PackagedShaderDir;
    return true;
}

// Confirms every packaged shader, geometry container, and typeface is present without requiring a Vulkan device
[[nodiscard]] int VerifyPackagedLayout(
    bool                         PackagedLayoutResolved,
    const std::filesystem::path& RepositoryRootPath,
    const std::filesystem::path& CompiledShaderDir)
{
    static constexpr const char* RequiredShaderBinaries[] = {
        "SurfaceShading.vert.spv",            "SurfaceShading.frag.spv",
        "UVTileRasterization.vert.spv",       "UVTileRasterization.frag.spv",
        "GizmoRasterization.vert.spv",        "GizmoRasterization.frag.spv",
        "ImGuiOverlayRasterization.vert.spv", "ImGuiOverlayRasterization.frag.spv",
        "SelectionMaskCompute.comp.spv",      "ViewportPickReduction.comp.spv",
        "UVDistortionInspection.comp.spv",    "StretchRelaxCompute.comp.spv",
        "ConcaveSkylineCompute.comp.spv",
    };

    if (!PackagedLayoutResolved)
    {
        std::fprintf(stderr, "[FrontierHost] Packaged layout missing: no CompiledShaders directory beside the executable\n");
        return 1;
    }

    std::error_code PathFailure {};
    uint32_t        MissingCount = 0u;
    for (const char* ShaderBinary : RequiredShaderBinaries)
    {
        if (!std::filesystem::is_regular_file(CompiledShaderDir / ShaderBinary, PathFailure))
        {
            std::fprintf(stderr, "[FrontierHost] Packaged shader missing: %s\n", ShaderBinary);
            ++MissingCount;
        }
    }

    const std::filesystem::path DefaultGeometry = RepositoryRootPath / "EngineContent/Geometry/ControlledIntegrator.GeometrySpace";
    if (!std::filesystem::is_regular_file(DefaultGeometry, PathFailure))
    {
        std::fprintf(stderr, "[FrontierHost] Packaged geometry missing: %s\n", DefaultGeometry.string().c_str());
        ++MissingCount;
    }

    // 📝 TypographyRepository needs a live ImGui font sheet, so the packaged typeface files are counted on disk instead
    uint32_t TypefaceCount = 0u;
    for (const auto& DirectoryEntry :
         std::filesystem::recursive_directory_iterator(RepositoryRootPath / "EngineContent/FontsArchives", PathFailure))
    {
        const std::filesystem::path Suffix = DirectoryEntry.path().extension();
        if (DirectoryEntry.is_regular_file(PathFailure) && (Suffix == ".ttf" || Suffix == ".otf"))
        {
            ++TypefaceCount;
        }
    }
    if (TypefaceCount == 0u)
    {
        std::fprintf(stderr, "[FrontierHost] Packaged typefaces missing under EngineContent/FontsArchives\n");
        ++MissingCount;
    }

    std::fprintf(
        stdout,
        "[FrontierHost] Packaged layout at %s: %u shaders, %u typefaces, %u missing\n",
        RepositoryRootPath.string().c_str(),
        static_cast<unsigned>(std::size(RequiredShaderBinaries)),
        static_cast<unsigned>(TypefaceCount),
        static_cast<unsigned>(MissingCount));
    return MissingCount == 0u ? 0 : 1;
}

} // namespace

int LaunchDesktopWorkspace(
    DesktopLaunchOptions          InitialOptions,
    std::span<const char* const>  CommandLineArgs,
    const std::filesystem::path&  BuildRepositoryRootPath,
    const std::filesystem::path&  BuildCompiledShaderDir)
{
    // Packaged binaries carry CompiledShaders/ and EngineContent/ beside the executable; in-place builds keep the CMake paths.
    std::filesystem::path RepositoryRootPath = BuildRepositoryRootPath;
    std::filesystem::path CompiledShaderDir  = BuildCompiledShaderDir;
    const bool            PackagedLayoutResolved =
        ResolvePackagedLayout(CommandLineArgs, RepositoryRootPath, CompiledShaderDir);

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

    if (PackagedLayoutResolved)
    {
        // Relative command-line paths stay anchored to the launch directory before content look-ups move beside the executable
        std::error_code PathFailure {};
        if (!InitialOptions.StartupImportPath.empty())
        {
            InitialOptions.StartupImportPath = std::filesystem::absolute(InitialOptions.StartupImportPath, PathFailure);
        }
        if (!InitialOptions.CapturePngOutputPath.empty())
        {
            InitialOptions.CapturePngOutputPath = std::filesystem::absolute(InitialOptions.CapturePngOutputPath, PathFailure);
        }
        std::filesystem::current_path(RepositoryRootPath, PathFailure);
    }

    for (const char* RawArg : CommandLineArgs)
    {
        if (RawArg != nullptr && std::string_view(RawArg) == "--verify-packaged-layout")
        {
            return VerifyPackagedLayout(PackagedLayoutResolved, RepositoryRootPath, CompiledShaderDir);
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

    GlfwSupplementalQueue SupplementalQueue {};
    ActiveSupplementalQueue = &SupplementalQueue;
    if (auto* GlfwWin = static_cast<GLFWwindow*>(DesktopWindow.NativeGlfwHandle()))
    {
        SupplementalQueue.ChainCharCallback = glfwSetCharCallback(
            GlfwWin,
            [](GLFWwindow* W, unsigned int Codepoint)
            {
                if (ActiveSupplementalQueue != nullptr)
                {
                    ActiveSupplementalQueue->CodePoints.push_back(Codepoint);
                    if (ActiveSupplementalQueue->ChainCharCallback != nullptr)
                    {
                        ActiveSupplementalQueue->ChainCharCallback(W, Codepoint);
                    }
                }
            });
        SupplementalQueue.ChainDropCallback = glfwSetDropCallback(
            GlfwWin,
            [](GLFWwindow* W, int Count, const char** Paths)
            {
                if (ActiveSupplementalQueue != nullptr && Paths != nullptr)
                {
                    for (int Idx = 0; Idx < Count; ++Idx)
                    {
                        if (Paths[Idx] != nullptr)
                        {
                            ActiveSupplementalQueue->DroppedPaths.emplace_back(Paths[Idx]);
                        }
                    }
                    if (ActiveSupplementalQueue->ChainDropCallback != nullptr)
                    {
                        ActiveSupplementalQueue->ChainDropCallback(W, Count, Paths);
                    }
                }
            });
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

    unsigned char* FontPixels = nullptr;
    int            FontW      = 0;
    int            FontH      = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &FontW, &FontH);

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
        const auto Extent = DesktopWindow.InspectExtent();
        if (Extent.SurfacePixelWidth == 0u || Extent.SurfacePixelHeight == 0u)
        {
            glfwWaitEventsTimeout(0.01);
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
#endif
        if (auto* GlfwWin = static_cast<GLFWwindow*>(DesktopWindow.NativeGlfwHandle()))
        {
            SynchronizeGlfwToImGui(GlfwWin, Io, SupplementalQueue);
        }
        Io.AddMouseWheelEvent(
            InputCoordinator.InspectPointerSnapshot().ScrollDeltaX,
            InputCoordinator.InspectPointerSnapshot().ScrollDeltaY);

        for (const std::string& DropFile : SupplementalQueue.DroppedPaths)
        {
            std::string DropDiag {};
            (void)UVHost.IntakeExternalFileDrop(DropFile, DropDiag);
        }
        SupplementalQueue.DroppedPaths.clear();
#endif

        Io.DisplaySize = ImVec2(static_cast<float>(SurfaceW), static_cast<float>(SurfaceH));
        Io.DeltaTime   = static_cast<float>(Dt);

        ImGui::NewFrame();
#if FRONTIER_EDITOR_ENABLED
        if (UseFullEditorLifecycle)
        {
            (void)EditorLoop.ExecuteAuthoringTick(Dt);
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

        (void)SwapchainPresenter.PresentImGuiBatches(ImGui::GetDrawData(), SurfaceW, SurfaceH);
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
    ActiveSupplementalQueue = nullptr;
    DesktopWindow.Finalize();
#endif
    TypographyRepository::AssignCurrent(nullptr);
    ImGui::DestroyContext(GuiCtx);
    return 0;
}

} // namespace Frontier::Lifecycle
