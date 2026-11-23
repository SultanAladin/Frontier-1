/*============================================================================================================================================
                                                          DESKTOPWORKSPACELAUNCH.CPP
============================================================================================================================================*/
// 🧩 Implementation of DesktopWorkspaceLaunch orchestrating GLFW 3.4, ImGuiOverlayRasterization, UVWorkspaceRasterizer & EditorLifecycle

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Lifecycle/DesktopWorkspaceLaunch.h"
#include "Engine/Lifecycle/ThoughtLog.h"

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/ControlCentre/ControlCentrePanel.h"
#include "Engine/Components/Diagnostics/ViewportTimingTelemetry.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometricRaster/Rasterization/ImGuiOverlayRasterization.h"
#include "Engine/GeometricRaster/UV/UVWorkspaceRasterizer.h"
#include "Engine/GeometricRaster/Vector/VectorCanvasRasterizer.h"
#include "Engine/GeometricRaster/Vector/VulkanVectorRenderMethod.h"
#include "Engine/Input/ImGuiInputAdapter.h"
#include "Engine/Input/InputExtension.h"
#include "Engine/Windowing/PlatformWindow.h"
#include "Engine/Workspaces/Fracture/FractureWorkspaceHost.h"
#include "Engine/Workspaces/Material/MaterialWorkspaceHost.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherWorkspaceHost.h"
#include "Engine/Workspaces/Texturing/BakingWorkspaceHost.h"
#include "Engine/Workspaces/Texturing/PatternStampWorkspaceHost.h"
#include "Engine/Workspaces/Texturing/TexturePaintWorkspaceHost.h"
#include "Engine/Workspaces/UV/UVWorkspaceHost.h"

#if FRONTIER_EDITOR_ENABLED
#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/Panes/InspectorPanel.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Workspaces/ComponentEditor/ComponentWorkspaceHost.h"
#include "Engine/Components/Panes/ViewportPanel.h"
#include "Engine/Editor/Explorer/ContentBrowser.h"
#include "Engine/Editor/Explorer/ProjectBrowser.h"
#include "Engine/Lifecycle/EditorLifecycle.h"
#endif

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <memory>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif
#include <vector>

namespace Frontier::Lifecycle
{

namespace
{

#if defined(_WIN32)
struct Win32HighResTimerScope
{
    using TimePeriodFn = UINT(WINAPI*)(UINT);
    HMODULE      WinMmLibrary = nullptr;
    TimePeriodFn EndPeriod    = nullptr;

    Win32HighResTimerScope() noexcept
    {
        WinMmLibrary = LoadLibraryW(L"winmm.dll");
        if (WinMmLibrary != nullptr)
        {
            const auto BeginPeriod = reinterpret_cast<TimePeriodFn>(GetProcAddress(WinMmLibrary, "timeBeginPeriod"));
            EndPeriod              = reinterpret_cast<TimePeriodFn>(GetProcAddress(WinMmLibrary, "timeEndPeriod"));
            if (BeginPeriod != nullptr && EndPeriod != nullptr)
            {
                BeginPeriod(1u);
            }
            else
            {
                EndPeriod = nullptr;
            }
        }
    }

    ~Win32HighResTimerScope() noexcept
    {
        if (EndPeriod != nullptr)
        {
            EndPeriod(1u);
        }
        if (WinMmLibrary != nullptr)
        {
            FreeLibrary(WinMmLibrary);
        }
    }
};
#endif

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
    auto RegisterPathHierarchy = [&](std::filesystem::path Path)
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

    RegisterPathHierarchy(ExecutablePath);
    RegisterPathHierarchy(std::filesystem::current_path());
    RegisterPathHierarchy(FallbackRepositoryRoot);
    RegisterPathHierarchy(FallbackShaderDirectory);

    DesktopResourcePaths Resolved { FallbackRepositoryRoot, FallbackShaderDirectory };
    bool RepoLocated   = false;
    bool ShaderLocated = false;
    for (const auto& Root : SearchRoots)
    {
        const bool GeometryPresent  = std::filesystem::exists(Root / "EngineContent/Geometry/ControlledIntegrator.GeometrySpace");
        const bool FontIndexPresent = std::filesystem::exists(Root / "EngineContent/FontsArchives/FontRepository.FontSpace");
        if (!RepoLocated && (GeometryPresent || FontIndexPresent))
        {
            Resolved.RepositoryRootPath = Root;
            RepoLocated                 = true;
        }

        const std::filesystem::path ShaderFolder = Root / "CompiledShaders";
        if (!ShaderLocated &&
            std::filesystem::exists(ShaderFolder / "SurfaceShading.vert.spv") &&
            std::filesystem::exists(ShaderFolder / "WorldViewportRadiometricCompute.comp.spv"))
        {
            Resolved.CompiledShaderDirectory = ShaderFolder;
            ShaderLocated                    = true;
        }
        else if (!ShaderLocated &&
                 std::filesystem::exists(ShaderFolder / "SurfaceShading.vert.spv") &&
                 std::filesystem::exists(ShaderFolder / "UVTileRasterization.frag.spv"))
        {
            Resolved.CompiledShaderDirectory = ShaderFolder;
        }

        if (!ShaderLocated && (GeometryPresent || FontIndexPresent) &&
            std::filesystem::exists(Root / "SurfaceShading.vert.spv") &&
            std::filesystem::exists(Root / "UVTileRasterization.frag.spv"))
        {
            Resolved.CompiledShaderDirectory = Root;
            if (std::filesystem::exists(Root / "WorldViewportRadiometricCompute.comp.spv"))
            {
                ShaderLocated = true;
            }
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
    Diagnostics::ThoughtLog::Initialize(); // RAM-only timing log; flushed once when the application closes
    std::set_terminate([]()
    {
        std::fprintf(stderr, "[FrontierHost] Unhandled C++ terminate invoked.\n");
        std::fflush(stderr);
        if (std::FILE* Log = std::fopen("FrontierCrash.log", "a"))
        {
            std::fprintf(Log, "[FrontierHost] Unhandled C++ terminate invoked.\n");
            std::fclose(Log);
        }
        std::abort();
    });
#if defined(_WIN32)
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* Info) -> LONG
    {
        const unsigned long Code = (Info && Info->ExceptionRecord) ? Info->ExceptionRecord->ExceptionCode : 0ul;
        const void* Addr = (Info && Info->ExceptionRecord) ? Info->ExceptionRecord->ExceptionAddress : nullptr;
        std::fprintf(stderr, "[FrontierHost] Windows unhandled exception 0x%08lX at %p\n", Code, Addr);
        std::fflush(stderr);
        if (std::FILE* Log = std::fopen("FrontierCrash.log", "a"))
        {
            std::fprintf(Log, "[FrontierHost] Windows unhandled exception 0x%08lX at %p\n", Code, Addr);
            std::fclose(Log);
        }
        return EXCEPTION_EXECUTE_HANDLER;
    });
#endif

    Configuration::EngineConfigurationDescriptor BaseDesc =
        InitialOptions.DefaultLaunchMode == Configuration::HostLaunchMode::StandaloneWorkspace
            ? Configuration::EngineConfiguration::ConstructStandaloneUVDefault()
            : Configuration::EngineConfiguration::ConstructFeatureSetDefault();
    if (InitialOptions.DefaultLaunchMode == Configuration::HostLaunchMode::StandaloneWorkspace)
    {
        BaseDesc.ActiveWorkspace  = InitialOptions.InitialWorkspace;
        BaseDesc.ApplicationTitle = std::string(InitialOptions.WindowTitle);
    }
    Configuration::EngineConfiguration EngineConfig(std::move(BaseDesc));

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

    Workspaces::PatternStampWorkspaceMode RequestedVectorMode = InitialOptions.InitialStampMode
        ? Workspaces::PatternStampWorkspaceMode::Stamp
        : Workspaces::PatternStampWorkspaceMode::Pattern;

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
            InitialOptions.OpenProjectBrowser = false;
        }
        else if (Arg == "--close-browser")
        {
            InitialOptions.OpenContentBrowser = false;
            InitialOptions.OpenProjectBrowser = false;
        }
        else if (Arg == "--world" || Arg == "--world-editor")
        {
            InitialOptions.OpenWorldEditor    = true;
            InitialOptions.OpenProjectBrowser = false;
        }
        else if (Arg == "--uv" || Arg == "--uv-workspace" || Arg == "--standalone-uv" || Arg == "--standalone=uv" || Arg == "--workspace=uv")
        {
            InitialOptions.OpenWorldEditor    = false;
            InitialOptions.OpenProjectBrowser = false;
            InitialOptions.DefaultLaunchMode  = Configuration::HostLaunchMode::StandaloneWorkspace;
            InitialOptions.WindowTitle        = "Frontier UV";
        }
        else if (Arg == "--standalone-pattern" || Arg == "--standalone=pattern" || Arg == "--workspace=pattern" || Arg == "--mode=pattern")
        {
            RequestedVectorMode               = Workspaces::PatternStampWorkspaceMode::Pattern;
            InitialOptions.OpenWorldEditor    = false;
            InitialOptions.OpenProjectBrowser = false;
            InitialOptions.WindowTitle        = "Frontier Pattern";
        }
        else if (Arg == "--standalone-stamp" || Arg == "--standalone=stamp" || Arg == "--workspace=stamp" || Arg == "--mode=stamp")
        {
            RequestedVectorMode               = Workspaces::PatternStampWorkspaceMode::Stamp;
            InitialOptions.OpenWorldEditor    = false;
            InitialOptions.OpenProjectBrowser = false;
            InitialOptions.WindowTitle        = "Frontier Stamp";
        }
        else if (Arg == "--standalone-vector" || Arg == "--standalone=vector" || Arg == "--workspace=vector")
        {
            InitialOptions.OpenWorldEditor    = false;
            InitialOptions.OpenProjectBrowser = false;
            InitialOptions.WindowTitle        = "Frontier Vector";
        }
        else if (Arg == "--standalone-baking" || Arg == "--standalone=baking" || Arg == "--workspace=baking" || Arg == "--baking")
        {
            InitialOptions.OpenWorldEditor    = false;
            InitialOptions.OpenProjectBrowser = false;
            InitialOptions.WindowTitle        = "Frontier Baking";
        }
        else if (Arg == "--standalone-material" || Arg == "--standalone=material" || Arg == "--workspace=material" || Arg == "--material")
        {
            InitialOptions.OpenWorldEditor    = false;
            InitialOptions.OpenProjectBrowser = false;
            InitialOptions.WindowTitle        = "Frontier Material";
        }
        else if (Arg == "--standalone-fracture" || Arg == "--standalone=fracture" || Arg == "--workspace=fracture" || Arg == "--fracture")
        {
            InitialOptions.OpenWorldEditor    = false;
            InitialOptions.OpenProjectBrowser = false;
            InitialOptions.WindowTitle        = "Frontier Fracture";
        }
        else if (Arg == "--standalone-sketcher" || Arg == "--standalone=sketcher" || Arg == "--workspace=sketcher" || Arg == "--sketcher" || Arg == "--cad" || Arg == "--solidarc")
        {
            InitialOptions.OpenWorldEditor    = false;
            InitialOptions.OpenProjectBrowser = false;
            InitialOptions.WindowTitle        = "Frontier Parametric Sketcher";
        }
        else if (Arg == "--project-browser")
        {
            InitialOptions.OpenProjectBrowser = true;
        }
        else if (Arg == "--project-reader")
        {
            InitialOptions.OpenProjectBrowser   = true;
            InitialOptions.PreviewReaderOnStart = true;
        }
        else if (Arg == "--new-project")
        {
            InitialOptions.OpenProjectBrowser  = true;
            InitialOptions.OpenNewProjectModal = true;
        }
        else if (Arg == "--apocalypse-city" || Arg == "--ruined-city")
        {
            InitialOptions.OpenProjectBrowser  = false;
            InitialOptions.OpenWorldEditor     = true;
            InitialOptions.OpenApocalypticCity = true;
            InitialOptions.OpenContentBrowser  = false;
            InitialOptions.WindowTitle         = "Frontier · Apocalyptic City Racing Level";
        }
        else if (Arg.starts_with("--project="))
        {
            InitialOptions.StartupProjectSpacePath = std::filesystem::path(Arg.substr(10u));
            InitialOptions.OpenProjectBrowser      = false;
            InitialOptions.OpenWorldEditor         = true;
        }
    }

#if defined(_WIN32)
    Win32HighResTimerScope HighResTimer {};
#endif

    GeometricRaster::VulkanInterchange DeviceInterchange {};
    std::string                          VkDiag {};
    if (!DeviceInterchange.InitializeInterchange(RepositoryRootPath, &VkDiag))
    {
        std::fprintf(stderr, "[FrontierHost] Vulkan initialization failed: %s\n", VkDiag.c_str());
        return 1;
    }
    std::fprintf(
        stdout,
        "[FrontierHost] Active Vulkan Adapter: %.*s (Hardware GPU: %s)\n",
        static_cast<int>(DeviceInterchange.QueryAdapterName().size()),
        DeviceInterchange.QueryAdapterName().data(),
        DeviceInterchange.QueryHardwareAccelerated() ? "YES" : "NO (Software ICD)");
    std::fflush(stdout);

#if FRONTIER_EDITOR_ENABLED
    enum class ActiveHostSurface : uint8_t
    {
        ProjectBrowserSurface      = 0u,
        WorldEditorSurface         = 1u,
        UvWorkspaceSurface         = 2u,
        PatternWorkspaceSurface    = 3u,
        StampWorkspaceSurface      = 4u,
        BakingWorkspaceSurface     = 5u,
        MaterialWorkspaceSurface        = 6u,
        FractureWorkspaceSurface        = 7u,
        SketcherWorkspaceSurface        = 8u,
        ComponentEditorWorkspaceSurface = 9u,
        TexturePaintWorkspaceSurface    = 10u,
    };

    const bool StandaloneTexturePaintLaunch =
        (EngineConfig.QueryWorkspaceCategory() == WorkspaceCategory::TexturePaint) &&
        !InitialOptions.OpenWorldEditor &&
        !InitialOptions.OpenProjectBrowser;
    const bool StandaloneVectorLaunch =
        (EngineConfig.QueryWorkspaceCategory() == WorkspaceCategory::Vector) &&
        !InitialOptions.OpenWorldEditor &&
        !InitialOptions.OpenProjectBrowser;
    const bool StandaloneBakingLaunch =
        (EngineConfig.QueryWorkspaceCategory() == WorkspaceCategory::Baking) &&
        !InitialOptions.OpenWorldEditor &&
        !InitialOptions.OpenProjectBrowser;
    const bool StandaloneMaterialLaunch =
        (EngineConfig.QueryWorkspaceCategory() == WorkspaceCategory::Material) &&
        !InitialOptions.OpenWorldEditor &&
        !InitialOptions.OpenProjectBrowser;
    const bool StandaloneFractureLaunch =
        (EngineConfig.QueryWorkspaceCategory() == WorkspaceCategory::Fracture) &&
        !InitialOptions.OpenWorldEditor &&
        !InitialOptions.OpenProjectBrowser;
    const bool StandaloneSketcherLaunch =
        (EngineConfig.QueryWorkspaceCategory() == WorkspaceCategory::GeometryCad) &&
        !InitialOptions.OpenWorldEditor &&
        !InitialOptions.OpenProjectBrowser;
    const bool StandaloneUVLaunch =
        (EngineConfig.QueryLaunchMode() == Configuration::HostLaunchMode::StandaloneWorkspace) &&
        (EngineConfig.QueryWorkspaceCategory() == WorkspaceCategory::UV) &&
        !InitialOptions.OpenWorldEditor &&
        !InitialOptions.OpenProjectBrowser;

    ActiveHostSurface CurrentSurface = ActiveHostSurface::WorldEditorSurface;
    if (InitialOptions.OpenProjectBrowser)
    {
        CurrentSurface = ActiveHostSurface::ProjectBrowserSurface;
    }
    else if (StandaloneTexturePaintLaunch)
    {
        CurrentSurface = ActiveHostSurface::TexturePaintWorkspaceSurface;
    }
    else if (StandaloneMaterialLaunch)
    {
        CurrentSurface = ActiveHostSurface::MaterialWorkspaceSurface;
    }
    else if (StandaloneFractureLaunch)
    {
        CurrentSurface = ActiveHostSurface::FractureWorkspaceSurface;
    }
    else if (StandaloneSketcherLaunch)
    {
        CurrentSurface = ActiveHostSurface::SketcherWorkspaceSurface;
    }
    else if (StandaloneBakingLaunch)
    {
        CurrentSurface = ActiveHostSurface::BakingWorkspaceSurface;
    }
    else if (StandaloneVectorLaunch)
    {
        CurrentSurface = (RequestedVectorMode == Workspaces::PatternStampWorkspaceMode::Stamp)
                             ? ActiveHostSurface::StampWorkspaceSurface
                             : ActiveHostSurface::PatternWorkspaceSurface;
    }
    else if (StandaloneUVLaunch)
    {
        CurrentSurface = ActiveHostSurface::UvWorkspaceSurface;
    }
#else
    const bool StandaloneVectorLaunch = (EngineConfig.QueryWorkspaceCategory() == WorkspaceCategory::Vector);
    const bool StandaloneBakingLaunch = (EngineConfig.QueryWorkspaceCategory() == WorkspaceCategory::Baking);
    const bool StandaloneUVLaunch     = !StandaloneVectorLaunch && !StandaloneBakingLaunch;
#endif

    GeometricRaster::UVWorkspaceRasterizer UVRasterizer {};
    bool UvStackInitialized = false;
    auto EnsureUvStackReady = [&]() -> bool
    {
        if (UvStackInitialized)
        {
            return true;
        }
        if (!UVRasterizer.InitializeRasterizer(&DeviceInterchange, CompiledShaderDir))
        {
            std::fprintf(stderr, "[FrontierHost] UVWorkspaceRasterizer initialization failed.\n");
            return false;
        }
        UvStackInitialized = true;
        return true;
    };
    if (StandaloneUVLaunch && !EnsureUvStackReady())
    {
        return 1;
    }

    GeometricRaster::VectorCanvasRasterizer VectorRasterizer {};
    bool VectorStackInitialized = false;
    auto EnsureVectorStackReady = [&]() -> bool
    {
        if (VectorStackInitialized)
        {
            return true;
        }
        if (!VectorRasterizer.InitializeRasterizer(&DeviceInterchange, CompiledShaderDir))
        {
            std::fprintf(stderr, "[FrontierHost] VectorCanvasRasterizer initialization failed.\n");
            return false;
        }
        std::error_code FontEc {};
        const std::filesystem::path FontArchiveRoot = RepositoryRootPath / "EngineContent/FontsArchives";
        if (std::filesystem::exists(FontArchiveRoot, FontEc))
        {
            for (const auto& Entry : std::filesystem::recursive_directory_iterator(FontArchiveRoot, FontEc))
            {
                if (!Entry.is_regular_file(FontEc)) continue;
                const std::string Ext = Entry.path().extension().string();
                if (Ext == ".ttf" || Ext == ".TTF" || Ext == ".otf" || Ext == ".OTF")
                {
                    (void)GeometricRaster::ThorVGVectorFrontend::RegisterTypefaceFile(Entry.path());
                }
            }
        }
        VectorStackInitialized = true;
        return true;
    };
    if (StandaloneVectorLaunch && !EnsureVectorStackReady())
    {
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

    std::unique_ptr<Workspaces::UVWorkspaceHost>           UVHostPtr;
    std::unique_ptr<Workspaces::PatternStampWorkspaceHost> PatternHostPtr;
    std::unique_ptr<Workspaces::PatternStampWorkspaceHost> StampHostPtr;
    std::unique_ptr<Workspaces::BakingWorkspaceHost>       BakingHostPtr;
    std::unique_ptr<Workspaces::TexturePaintWorkspaceHost> TexturePaintHostPtr;

    auto StandaloneDockPtr          = std::make_unique<Components::HeadlessWorkspaceDockContainer>();
    auto PatternDockPtr             = std::make_unique<Components::HeadlessWorkspaceDockContainer>();
    auto StampDockPtr               = std::make_unique<Components::HeadlessWorkspaceDockContainer>();
    auto BakingDockPtr              = std::make_unique<Components::HeadlessWorkspaceDockContainer>();
    auto StandaloneControlCentrePtr = std::make_unique<ControlCentrePanel>();
    auto InputParticipantPtr        = std::make_unique<Input::InputExtension>();

    auto& StandaloneDock          = *StandaloneDockPtr;
    auto& PatternDock             = *PatternDockPtr;
    auto& StampDock               = *StampDockPtr;
    auto& BakingDock              = *BakingDockPtr;
    auto& StandaloneControlCentre = *StandaloneControlCentrePtr;
    auto& InputParticipant        = *InputParticipantPtr;
    InputParticipant.Initialize();
    GeometricRaster::ImGuiOverlayRasterization SwapchainPresenter {};

#if FRONTIER_EDITOR_ENABLED
    std::unique_ptr<Workspaces::MaterialWorkspaceHost>           MaterialHostPtr;
    std::unique_ptr<Workspaces::Fracture::FractureWorkspaceHost>               FractureHostPtr;
    std::unique_ptr<Workspaces::ComponentEditor::ComponentWorkspaceHost>       ComponentEditorHostPtr;
    std::unique_ptr<Workspaces::SketcherWorkspaceHost>           SketcherHostPtr;
    std::unique_ptr<Components::HeadlessWorkspaceDockContainer>  MaterialDockPtr;
    std::unique_ptr<Components::HeadlessWorkspaceDockContainer>  SketcherDockPtr;
    std::unique_ptr<Lifecycle::EditorLifecycle>                  EditorLoopPtr;
    std::unique_ptr<Editor::Explorer::ContentBrowser>            StandaloneBrowserPtr;
    std::unique_ptr<Editor::Explorer::ContentBrowser>            PatternBrowserPtr;
    std::unique_ptr<Editor::Explorer::ContentBrowser>            StampBrowserPtr;
    std::unique_ptr<Editor::Explorer::ContentBrowser>            BakingBrowserPtr;
    std::unique_ptr<Editor::Explorer::ContentBrowser>            TexturePaintBrowserPtr;
    std::unique_ptr<Editor::Explorer::ContentBrowser>            MaterialBrowserPtr;
    std::unique_ptr<Editor::Explorer::ContentBrowser>            FractureBrowserPtr;
    std::unique_ptr<Editor::Explorer::ContentBrowser>            SketcherBrowserPtr;
    std::unique_ptr<Editor::Explorer::ProjectBrowser>            HostProjectBrowserPtr;
    bool                                               EditorStackReady          = false;
    bool                                               UvHostReady               = false;
    bool                                               PatternHostReady          = false;
    bool                                               StampHostReady            = false;
    bool                                               BakingHostReady           = false;
    bool                                               TexturePaintHostReady     = false;
    bool                                               MaterialHostReady         = false;
    bool                                               SketcherHostReady         = false;
    bool                                               FractureHostReady         = false;
    bool                                               ComponentEditorHostReady  = false;
    bool                                               ProjectBrowserInitialized = false;
    bool                                               SharedThemeInitialized    = false;

    auto EnsureSharedControlCentreReady = [&]()
    {
        if (SharedThemeInitialized)
        {
            return;
        }
        StandaloneControlCentre.AttachTypefaces(&Typography);
        StandaloneControlCentre.AccessDraftAppearance().Theme = ThemePreset::Dark;
        StandaloneControlCentre.ApplyDraftAppearanceImmediate();
        StandaloneControlCentre.AccessNotificationStream().ClearPending();
        SharedThemeInitialized = true;
    };

    auto EnsureProjectBrowserReady = [&]()
    {
        if (ProjectBrowserInitialized)
        {
            return;
        }
        if (!HostProjectBrowserPtr)
        {
            HostProjectBrowserPtr = std::make_unique<Editor::Explorer::ProjectBrowser>();
        }
        HostProjectBrowserPtr->AttachVulkanWorldRenderer(&DeviceInterchange, CompiledShaderDir);
        HostProjectBrowserPtr->InitializeDefaultCollection(
            RepositoryRootPath / "Projects",
            RepositoryRootPath,
            CompiledShaderDir.parent_path());
        if (InitialOptions.PreviewReaderOnStart)
        {
            HostProjectBrowserPtr->ActivateEntryByIndex(0);
        }
        if (InitialOptions.OpenNewProjectModal)
        {
            HostProjectBrowserPtr->OpenNewProjectSheet("AeroGallery", RepositoryRootPath / "Projects");
        }
        ProjectBrowserInitialized = true;
    };

    auto EnsureEditorLifecycleReady = [&](const std::filesystem::path& ProjectSpaceFile, std::string_view ProjectTitle, bool BlankPlaneSkyScene = false) -> bool
    {
        if (!EditorStackReady)
        {
            if (!EditorLoopPtr)
            {
                EditorLoopPtr = std::make_unique<Lifecycle::EditorLifecycle>();
            }
            Lifecycle::EditorStartupConfiguration EditorCfg {};
            EditorCfg.WindowDescriptor = Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
                InitialOptions.WindowTitle, InitialOptions.InitialWidthPx, InitialOptions.InitialHeightPx);
            EditorCfg.FontRepositoryRoot      = RepositoryRootPath / "EngineContent/FontsArchives";
            EditorCfg.ProjectSpacePath        = ProjectSpaceFile.empty() ? InitialOptions.StartupProjectSpacePath : ProjectSpaceFile;
            EditorCfg.PopulateShowcaseScene   = true;
            EditorCfg.PopulateBlankPlaneSkyScene = BlankPlaneSkyScene;
            EditorCfg.PopulateApocalypticCityScene = InitialOptions.OpenApocalypticCity;
            EditorCfg.VulkanDevice            = &DeviceInterchange;
            EditorCfg.ImGuiPresenter          = &SwapchainPresenter;
            EditorCfg.CompiledShaderDirectory = CompiledShaderDir;
            if (!EditorLoopPtr->Initialize(EditorCfg))
            {
                std::fprintf(stderr, "[FrontierHost] EditorLifecycle initialization failed.\n");
                return false;
            }
            EditorLoopPtr->AssignReturnToProjectBrowserCallback(
                [&]()
                {
                    EnsureProjectBrowserReady();
                    CurrentSurface = ActiveHostSurface::ProjectBrowserSurface;
                });
            auto& EditorBrowserPanel = EditorLoopPtr->AccessContentBrowser().AccessPanel();
            EditorBrowserPanel.ConfigureShelfExtent(
                InitialOptions.OpenContentBrowser ? ContentShelfExtent::SplitHalf : ContentShelfExtent::Closed,
                true);
            (void)EditorBrowserPanel.ActivateCategory(ContentCategory::Colours);
            EditorStackReady = true;
        }
        if (BlankPlaneSkyScene && EditorStackReady)
        {
            EditorLoopPtr->PopulateBlankPlaneSkySession();
        }
        if (!ProjectTitle.empty())
        {
            EditorLoopPtr->ConfigureActiveProjectTitle(ProjectTitle);
        }
        return true;
    };

    auto EnsureUvHostReady = [&](const std::filesystem::path& GeometryPath) -> bool
    {
        if (!EnsureUvStackReady())
        {
            return false;
        }
        EnsureSharedControlCentreReady();
        if (!UvHostReady)
        {
            if (!UVHostPtr) UVHostPtr = std::make_unique<Workspaces::UVWorkspaceHost>();
            if (!StandaloneBrowserPtr) StandaloneBrowserPtr = std::make_unique<Editor::Explorer::ContentBrowser>();
            UVHostPtr->ConfigureLaunchPolicy(EngineConfig);
            (void)UVHostPtr->AccessSession().InitializeGPUSolvers(&DeviceInterchange, CompiledShaderDir);
            (void)UVHostPtr->AttachVulkanRasterizer(&UVRasterizer);

            ThemeRepository ThemeRepo {};
            (void)StandaloneDock.Initialize(
                Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
                    "Frontier UV", InitialOptions.InitialWidthPx, InitialOptions.InitialHeightPx));
            Components::ConfigureImGuiWorkspaceStyle(StandaloneDock.ActiveTabMetrics());
            Components::ConfigureImGuiWorkspaceTheme(StandaloneDock.ActiveTabMetrics(), ThemeRepo);

            StandaloneBrowserPtr->AttachThemeRepository(&StandaloneControlCentre.QueryThemeRepository());
            UVHostPtr->AttachContentBrowser(StandaloneBrowserPtr.get());
            StandaloneBrowserPtr->AccessPanel().ConfigureShelfExtent(
                InitialOptions.OpenContentBrowser ? ContentShelfExtent::SplitHalf : ContentShelfExtent::Closed,
                true);
            (void)StandaloneBrowserPtr->AccessPanel().ActivateCategory(ContentCategory::Colours);
            UVHostPtr->RegisterDockPanes(StandaloneDock);
            UvHostReady = true;
        }
        const std::filesystem::path PathToLoad = GeometryPath.empty() ? InitialOptions.StartupImportPath : GeometryPath;
        if (!PathToLoad.empty())
        {
            std::string ImportDiag {};
            const std::string Ext = PathToLoad.extension().string();
            if (Ext == ".obj" || Ext == ".OBJ" || Ext == ".GeometrySpace" || Ext == ".geometryspace")
            {
                (void)UVHostPtr->IntakeExternalFileDrop(PathToLoad, ImportDiag);
            }
        }
        return true;
    };

    auto EnsurePatternHostReady = [&](const std::filesystem::path& VectorPath) -> bool
    {
        if (!EnsureVectorStackReady())
        {
            return false;
        }
        EnsureSharedControlCentreReady();
        if (!PatternHostReady)
        {
            if (!PatternHostPtr) PatternHostPtr = std::make_unique<Workspaces::PatternStampWorkspaceHost>(Workspaces::PatternStampWorkspaceMode::Pattern);
            if (!PatternBrowserPtr) PatternBrowserPtr = std::make_unique<Editor::Explorer::ContentBrowser>();
            PatternHostPtr->ConfigureLaunchPolicy(EngineConfig);
            PatternHostPtr->ConfigureRepositoryRoot(RepositoryRootPath);
            PatternHostPtr->AttachVulkanRasterizer(&VectorRasterizer);

            ThemeRepository ThemeRepo {};
            (void)PatternDock.Initialize(
                Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
                    "Frontier Pattern", InitialOptions.InitialWidthPx, InitialOptions.InitialHeightPx));
            Components::ConfigureImGuiWorkspaceStyle(PatternDock.ActiveTabMetrics());
            Components::ConfigureImGuiWorkspaceTheme(PatternDock.ActiveTabMetrics(), ThemeRepo);

            PatternBrowserPtr->AttachThemeRepository(&StandaloneControlCentre.QueryThemeRepository());
            PatternHostPtr->AttachContentBrowser(PatternBrowserPtr.get());
            PatternBrowserPtr->AccessPanel().ConfigureShelfExtent(
                InitialOptions.OpenContentBrowser ? ContentShelfExtent::SplitHalf : ContentShelfExtent::Closed,
                true);
            (void)PatternBrowserPtr->AccessPanel().ActivateCategory(ContentCategory::Vectors);
            PatternHostPtr->RegisterDockPanes(PatternDock);
            PatternHostReady = true;
        }
        const std::filesystem::path PathToLoad = VectorPath.empty() ? InitialOptions.StartupImportPath : VectorPath;
        if (!PathToLoad.empty() && std::filesystem::exists(PathToLoad))
        {
            std::string ImportDiag {};
            const std::string Ext = PathToLoad.extension().string();
            if (Ext == ".VectorSpace" || Ext == ".vectorspace" || Ext == ".svg" || Ext == ".SVG")
            {
                (void)PatternHostPtr->IntakeExternalFileDrop(PathToLoad, ImportDiag);
            }
        }
        return true;
    };

    auto EnsureStampHostReady = [&](const std::filesystem::path& VectorPath) -> bool
    {
        if (!EnsureVectorStackReady())
        {
            return false;
        }
        EnsureSharedControlCentreReady();
        if (!StampHostReady)
        {
            if (!StampHostPtr) StampHostPtr = std::make_unique<Workspaces::PatternStampWorkspaceHost>(Workspaces::PatternStampWorkspaceMode::Stamp);
            if (!StampBrowserPtr) StampBrowserPtr = std::make_unique<Editor::Explorer::ContentBrowser>();
            StampHostPtr->ConfigureLaunchPolicy(EngineConfig);
            StampHostPtr->ConfigureRepositoryRoot(RepositoryRootPath);
            StampHostPtr->AttachVulkanRasterizer(&VectorRasterizer);

            ThemeRepository ThemeRepo {};
            (void)StampDock.Initialize(
                Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
                    "Frontier Stamp", InitialOptions.InitialWidthPx, InitialOptions.InitialHeightPx));
            Components::ConfigureImGuiWorkspaceStyle(StampDock.ActiveTabMetrics());
            Components::ConfigureImGuiWorkspaceTheme(StampDock.ActiveTabMetrics(), ThemeRepo);

            StampBrowserPtr->AttachThemeRepository(&StandaloneControlCentre.QueryThemeRepository());
            StampHostPtr->AttachContentBrowser(StampBrowserPtr.get());
            StampBrowserPtr->AccessPanel().ConfigureShelfExtent(
                InitialOptions.OpenContentBrowser ? ContentShelfExtent::SplitHalf : ContentShelfExtent::Closed,
                true);
            (void)StampBrowserPtr->AccessPanel().ActivateCategory(ContentCategory::Vectors);
            StampHostPtr->RegisterDockPanes(StampDock);
            StampHostReady = true;
        }
        const std::filesystem::path PathToLoad = VectorPath.empty() ? InitialOptions.StartupImportPath : VectorPath;
        if (!PathToLoad.empty() && std::filesystem::exists(PathToLoad))
        {
            std::string ImportDiag {};
            const std::string Ext = PathToLoad.extension().string();
            if (Ext == ".VectorSpace" || Ext == ".vectorspace" || Ext == ".svg" || Ext == ".SVG")
            {
                (void)StampHostPtr->IntakeExternalFileDrop(PathToLoad, ImportDiag);
            }
        }
        return true;
    };

    auto EnsureBakingHostReady = [&](const std::filesystem::path& MeshPath) -> bool
    {
        EnsureSharedControlCentreReady();
        if (!BakingHostReady)
        {
            if (!BakingHostPtr) BakingHostPtr = std::make_unique<Workspaces::BakingWorkspaceHost>();
            if (!BakingBrowserPtr) BakingBrowserPtr = std::make_unique<Editor::Explorer::ContentBrowser>();
            BakingHostPtr->ConfigureLaunchPolicy(EngineConfig);
            BakingHostPtr->AttachVulkanDevice(&DeviceInterchange, CompiledShaderDir);

            ThemeRepository ThemeRepo {};
            (void)BakingDock.Initialize(
                Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
                    "Frontier Baking", InitialOptions.InitialWidthPx, InitialOptions.InitialHeightPx));
            Components::ConfigureImGuiWorkspaceStyle(BakingDock.ActiveTabMetrics());
            Components::ConfigureImGuiWorkspaceTheme(BakingDock.ActiveTabMetrics(), ThemeRepo);

            BakingBrowserPtr->AttachThemeRepository(&StandaloneControlCentre.QueryThemeRepository());
            BakingHostPtr->AttachContentBrowser(BakingBrowserPtr.get());
            BakingBrowserPtr->AccessPanel().ConfigureShelfExtent(
                InitialOptions.OpenContentBrowser ? ContentShelfExtent::SplitHalf : ContentShelfExtent::Closed,
                true);
            (void)BakingBrowserPtr->AccessPanel().ActivateCategory(ContentCategory::Colours);
            BakingHostPtr->RegisterDockPanes(BakingDock);
            BakingHostReady = true;
        }
        const std::filesystem::path PathToLoad = MeshPath.empty() ? InitialOptions.StartupImportPath : MeshPath;
        if (!PathToLoad.empty() && std::filesystem::exists(PathToLoad))
        {
            std::string ImportDiag {};
            const std::string Ext = PathToLoad.extension().string();
            if (Ext == ".obj" || Ext == ".OBJ" || Ext == ".stl" || Ext == ".STL")
            {
                (void)BakingHostPtr->IntakeExternalFileDrop(PathToLoad.string(), false, ImportDiag);
            }
        }
        return true;
    };

    auto EnsureTexturePaintHostReady = [&]() -> bool
    {
        EnsureSharedControlCentreReady();
        if (!TexturePaintHostReady)
        {
            if (!TexturePaintHostPtr) TexturePaintHostPtr = std::make_unique<Workspaces::TexturePaintWorkspaceHost>();
            if (!TexturePaintBrowserPtr) TexturePaintBrowserPtr = std::make_unique<Editor::Explorer::ContentBrowser>();
            TexturePaintHostPtr->Initialize(&EngineConfig);
            TexturePaintBrowserPtr->AttachThemeRepository(&StandaloneControlCentre.QueryThemeRepository());
            TexturePaintBrowserPtr->AccessPanel().ConfigureShelfExtent(
                InitialOptions.OpenContentBrowser ? ContentShelfExtent::SplitHalf : ContentShelfExtent::Closed,
                true);
            (void)TexturePaintBrowserPtr->AccessPanel().ActivateCategory(ContentCategory::Colours);
            TexturePaintHostReady = true;
        }
        return true;
    };

    auto EnsureMaterialHostReady = [&]() -> bool
    {
        EnsureSharedControlCentreReady();
        if (!MaterialHostReady)
        {
            if (!MaterialHostPtr) MaterialHostPtr = std::make_unique<Workspaces::MaterialWorkspaceHost>();
            if (!MaterialDockPtr) MaterialDockPtr = std::make_unique<Components::HeadlessWorkspaceDockContainer>();
            if (!MaterialBrowserPtr) MaterialBrowserPtr = std::make_unique<Editor::Explorer::ContentBrowser>();
            MaterialHostPtr->ConfigureLaunchPolicy(EngineConfig);
            (void)MaterialHostPtr->AttachVulkanDevice(&DeviceInterchange, CompiledShaderDir);

            ThemeRepository ThemeRepo {};
            (void)MaterialDockPtr->Initialize(
                Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
                    "Frontier Material", InitialOptions.InitialWidthPx, InitialOptions.InitialHeightPx));
            Components::ConfigureImGuiWorkspaceStyle(MaterialDockPtr->ActiveTabMetrics());
            Components::ConfigureImGuiWorkspaceTheme(MaterialDockPtr->ActiveTabMetrics(), ThemeRepo);

            MaterialBrowserPtr->AttachThemeRepository(&StandaloneControlCentre.QueryThemeRepository());
            MaterialBrowserPtr->AccessPanel().ConfigureOriginFilter(ContentOriginFilter::Project);
            MaterialHostPtr->AttachContentBrowser(MaterialBrowserPtr.get());
            MaterialBrowserPtr->AccessPanel().ConfigureShelfExtent(
                InitialOptions.OpenContentBrowser ? ContentShelfExtent::SplitHalf : ContentShelfExtent::Closed,
                true);
            MaterialHostPtr->RegisterDockPanes(*MaterialDockPtr);
            MaterialHostReady = true;
        }
        return true;
    };

    auto EnsureComponentEditorHostReady = [&]() -> bool
    {
        EnsureSharedControlCentreReady();
        if (!ComponentEditorHostReady)
        {
            if (!ComponentEditorHostPtr)
            {
                ComponentEditorHostPtr = std::make_unique<Workspaces::ComponentEditor::ComponentWorkspaceHost>();
            }
            ControlAppearance::ConfigureTheme(StandaloneControlCentre.QueryThemeRepository());
            ComponentEditorHostPtr->AttachVulkanDevice(DeviceInterchange, CompiledShaderDir, RepositoryRootPath);
            ComponentEditorHostReady = true;
        }
        return true;
    };

    auto EnsureFractureHostReady = [&]() -> bool
    {
        EnsureSharedControlCentreReady();
        if (!FractureHostReady)
        {
            if (!FractureHostPtr) FractureHostPtr = std::make_unique<Workspaces::Fracture::FractureWorkspaceHost>();
            if (!FractureBrowserPtr) FractureBrowserPtr = std::make_unique<Editor::Explorer::ContentBrowser>();
            ControlAppearance::ConfigureTheme(StandaloneControlCentre.QueryThemeRepository());
            FractureBrowserPtr->AttachThemeRepository(&StandaloneControlCentre.QueryThemeRepository());
            FractureBrowserPtr->AccessPanel().ConfigureWorkspaceDomain(WorkspaceCategory::Fracture);
            FractureBrowserPtr->AccessPanel().ConfigureShelfExtent(ContentShelfExtent::Closed, true);
            FractureHostPtr->AttachContentBrowserPanel(&FractureBrowserPtr->AccessPanel());
            FractureHostPtr->AttachVulkanDevice(DeviceInterchange, CompiledShaderDir);
            FractureHostReady = true;
        }
        return true;
    };

    auto EnsureSketcherHostReady = [&]() -> bool
    {
        EnsureSharedControlCentreReady();
        if (!SketcherHostReady)
        {
            if (!SketcherHostPtr) SketcherHostPtr = std::make_unique<Workspaces::SketcherWorkspaceHost>();
            if (!SketcherDockPtr) SketcherDockPtr = std::make_unique<Components::HeadlessWorkspaceDockContainer>();
            if (!SketcherBrowserPtr) SketcherBrowserPtr = std::make_unique<Editor::Explorer::ContentBrowser>();
            ControlAppearance::ConfigureTheme(StandaloneControlCentre.QueryThemeRepository());
            SketcherBrowserPtr->AttachThemeRepository(&StandaloneControlCentre.QueryThemeRepository());
            SketcherBrowserPtr->AccessPanel().ConfigureWorkspaceDomain(WorkspaceCategory::GeometryCad);
            SketcherBrowserPtr->AccessPanel().ConfigureShelfExtent(ContentShelfExtent::Closed, true);
            SketcherHostPtr->AttachContentBrowserPanel(&SketcherBrowserPtr->AccessPanel());

            (void)SketcherDockPtr->Initialize(
                Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
                    "Frontier Parametric Sketcher", InitialOptions.InitialWidthPx, InitialOptions.InitialHeightPx));
            Components::ConfigureImGuiWorkspaceStyle(SketcherDockPtr->ActiveTabMetrics());
            Components::ConfigureImGuiWorkspaceTheme(
                SketcherDockPtr->ActiveTabMetrics(),
                StandaloneControlCentre.QueryThemeRepository());

            std::string SketcherDiag {};
            const std::string InitialDoc = InitialOptions.StartupImportPath.empty() ? std::string{} : InitialOptions.StartupImportPath.string();
            (void)SketcherHostPtr->Initialize(
                &DeviceInterchange,
                CompiledShaderDir,
                RepositoryRootPath / "EngineContent/Icons",
                InitialDoc,
                &SketcherDiag);
            Components::ConfigureImGuiWorkspaceStyle(SketcherDockPtr->ActiveTabMetrics());
            Components::ConfigureImGuiWorkspaceTheme(
                SketcherDockPtr->ActiveTabMetrics(),
                StandaloneControlCentre.QueryThemeRepository());
            SketcherHostPtr->RegisterDockPanes(*SketcherDockPtr);
            SketcherHostReady = true;
        }
        return true;
    };

    auto BindProjectBrowserLaunchCallback = [&]()
    {
        if (!HostProjectBrowserPtr)
        {
            return;
        }
        HostProjectBrowserPtr->AssignLaunchWorkspaceCallback(
            [&](const Editor::Explorer::BrowserWorkspaceLaunchDirective& Directive)
            {
                if (Directive.TargetWorkspace == Workspaces::HostWorkspaceTarget::BakingWorkspace)
                {
                    if (EnsureBakingHostReady(Directive.SelectedFilePath))
                    {
                        CurrentSurface = ActiveHostSurface::BakingWorkspaceSurface;
                    }
                }
                else if (Directive.TargetWorkspace == Workspaces::HostWorkspaceTarget::PatternWorkspace)
                {
                    if (EnsurePatternHostReady(Directive.SelectedFilePath))
                    {
                        CurrentSurface = ActiveHostSurface::PatternWorkspaceSurface;
                    }
                }
                else if (Directive.TargetWorkspace == Workspaces::HostWorkspaceTarget::StampWorkspace)
                {
                    if (EnsureStampHostReady(Directive.SelectedFilePath))
                    {
                        CurrentSurface = ActiveHostSurface::StampWorkspaceSurface;
                    }
                }
                else if (Directive.TargetWorkspace == Workspaces::HostWorkspaceTarget::UVWorkspace)
                {
                    if (EnsureUvHostReady(Directive.SelectedFilePath))
                    {
                        CurrentSurface = ActiveHostSurface::UvWorkspaceSurface;
                    }
                }
                else if (Directive.TargetWorkspace == Workspaces::HostWorkspaceTarget::FractureWorkspace)
                {
                    if (EnsureFractureHostReady())
                    {
                        CurrentSurface = ActiveHostSurface::FractureWorkspaceSurface;
                    }
                }
                else if (Directive.TargetWorkspace == Workspaces::HostWorkspaceTarget::ComponentEditorWorkspace)
                {
                    if (EnsureComponentEditorHostReady())
                    {
                        CurrentSurface = ActiveHostSurface::ComponentEditorWorkspaceSurface;
                    }
                }
                else if (Directive.TargetWorkspace == Workspaces::HostWorkspaceTarget::SketcherWorkspace)
                {
                    if (!Directive.SelectedFilePath.empty())
                    {
                        InitialOptions.StartupImportPath = Directive.SelectedFilePath;
                    }
                    if (EnsureSketcherHostReady())
                    {
                        CurrentSurface = ActiveHostSurface::SketcherWorkspaceSurface;
                    }
                }
                else if (Directive.TargetWorkspace == Workspaces::HostWorkspaceTarget::MaterialWorkspace)
                {
                    if (EnsureMaterialHostReady())
                    {
                        CurrentSurface = ActiveHostSurface::MaterialWorkspaceSurface;
                    }
                }
                else
                {
                    if (EnsureEditorLifecycleReady(Directive.SelectedFilePath, Directive.ProjectTitle, Directive.BlankPlaneSkyScene))
                    {
                        CurrentSurface = ActiveHostSurface::WorldEditorSurface;
                    }
                }
            });
    };

    if (CurrentSurface == ActiveHostSurface::ProjectBrowserSurface)
    {
        EnsureProjectBrowserReady();
        BindProjectBrowserLaunchCallback();
    }
    else
    {
        BindProjectBrowserLaunchCallback();
    }
    if (CurrentSurface == ActiveHostSurface::ProjectBrowserSurface)
    {
    }
    else if (CurrentSurface == ActiveHostSurface::WorldEditorSurface)
    {
        if (!EnsureEditorLifecycleReady(InitialOptions.StartupProjectSpacePath, {}))
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }
    else if (CurrentSurface == ActiveHostSurface::BakingWorkspaceSurface)
    {
        if (!EnsureBakingHostReady(InitialOptions.StartupImportPath))
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }
    else if (CurrentSurface == ActiveHostSurface::TexturePaintWorkspaceSurface)
    {
        if (!EnsureTexturePaintHostReady())
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }
    else if (CurrentSurface == ActiveHostSurface::MaterialWorkspaceSurface)
    {
        if (!EnsureMaterialHostReady())
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }
    else if (CurrentSurface == ActiveHostSurface::FractureWorkspaceSurface)
    {
        if (!EnsureFractureHostReady())
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }
    else if (CurrentSurface == ActiveHostSurface::ComponentEditorWorkspaceSurface)
    {
        if (!EnsureComponentEditorHostReady())
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }
    else if (CurrentSurface == ActiveHostSurface::SketcherWorkspaceSurface)
    {
        if (!EnsureSketcherHostReady())
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }
    else if (CurrentSurface == ActiveHostSurface::PatternWorkspaceSurface)
    {
        if (!EnsurePatternHostReady(InitialOptions.StartupImportPath))
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }
    else if (CurrentSurface == ActiveHostSurface::StampWorkspaceSurface)
    {
        if (!EnsureStampHostReady(InitialOptions.StartupImportPath))
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }
    else
    {
        if (!EnsureUvHostReady(InitialOptions.StartupImportPath))
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }
#else
    if (!UVHostPtr) UVHostPtr = std::make_unique<Workspaces::UVWorkspaceHost>();
    if (!PatternHostPtr) PatternHostPtr = std::make_unique<Workspaces::PatternStampWorkspaceHost>(Workspaces::PatternStampWorkspaceMode::Pattern);
    if (!StampHostPtr) StampHostPtr = std::make_unique<Workspaces::PatternStampWorkspaceHost>(Workspaces::PatternStampWorkspaceMode::Stamp);
    if (!BakingHostPtr) BakingHostPtr = std::make_unique<Workspaces::BakingWorkspaceHost>();
    auto& UVHost = *UVHostPtr;
    auto& PatternHost = *PatternHostPtr;
    auto& StampHost = *StampHostPtr;
    auto& BakingHost = *BakingHostPtr;
    ContentBrowserPanel StandaloneBrowserPanel {};
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
    StandaloneBrowserPanel.AttachThemeRepository(&StandaloneControlCentre.QueryThemeRepository());

    Workspaces::PatternStampWorkspaceHost& ActiveDevVectorHost =
        (RequestedVectorMode == Workspaces::PatternStampWorkspaceMode::Stamp) ? StampHost : PatternHost;
    if (StandaloneBakingLaunch)
    {
        BakingHost.ConfigureLaunchPolicy(EngineConfig);
        BakingHost.AttachVulkanDevice(&DeviceInterchange, CompiledShaderDir);
        if (!InitialOptions.StartupImportPath.empty())
        {
            std::string ImportDiag {};
            (void)BakingHost.IntakeExternalFileDrop(InitialOptions.StartupImportPath.string(), false, ImportDiag);
        }
        BakingHost.AttachContentBrowserPanel(&StandaloneBrowserPanel);
        StandaloneBrowserPanel.ConfigureShelfExtent(
            InitialOptions.OpenContentBrowser ? ContentShelfExtent::SplitHalf : ContentShelfExtent::Closed,
            true);
        (void)StandaloneBrowserPanel.ActivateCategory(ContentCategory::Colours);
        BakingHost.RegisterDockPanes(StandaloneDock);
    }
    else if (StandaloneVectorLaunch)
    {
        ActiveDevVectorHost.ConfigureLaunchPolicy(EngineConfig);
        ActiveDevVectorHost.ConfigureRepositoryRoot(RepositoryRootPath);
        ActiveDevVectorHost.AttachVulkanRasterizer(&VectorRasterizer);
        if (!InitialOptions.StartupImportPath.empty())
        {
            std::string ImportDiag {};
            (void)ActiveDevVectorHost.IntakeExternalFileDrop(InitialOptions.StartupImportPath, ImportDiag);
        }
        ActiveDevVectorHost.AttachContentBrowserPanel(&StandaloneBrowserPanel);
        StandaloneBrowserPanel.ConfigureShelfExtent(
            InitialOptions.OpenContentBrowser ? ContentShelfExtent::SplitHalf : ContentShelfExtent::Closed,
            true);
        (void)StandaloneBrowserPanel.ActivateCategory(ContentCategory::Vectors);
        ActiveDevVectorHost.RegisterDockPanes(StandaloneDock);
    }
    else
    {
        UVHost.ConfigureLaunchPolicy(EngineConfig);
        (void)UVHost.AccessSession().InitializeGPUSolvers(&DeviceInterchange, CompiledShaderDir);
        (void)UVHost.AttachVulkanRasterizer(&UVRasterizer);
        if (!InitialOptions.StartupImportPath.empty())
        {
            std::string ImportDiag {};
            (void)UVHost.IntakeExternalFileDrop(InitialOptions.StartupImportPath, ImportDiag);
        }
        UVHost.AttachContentBrowserPanel(&StandaloneBrowserPanel);
        StandaloneBrowserPanel.ConfigureShelfExtent(
            InitialOptions.OpenContentBrowser ? ContentShelfExtent::SplitHalf : ContentShelfExtent::Closed,
            true);
        (void)StandaloneBrowserPanel.ActivateCategory(ContentCategory::Colours);
        UVHost.RegisterDockPanes(StandaloneDock);
    }
#endif

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

    uint32_t ExecutedSteps          = 0u;
    uint32_t SettleTicksRemaining   = 6u;
    uint32_t LastPresentedSurfaceW  = 0u;
    uint32_t LastPresentedSurfaceH  = 0u;
    float    LastPresentedDpiScale  = 0.0f;
#if FRONTIER_EDITOR_ENABLED
    ActiveHostSurface LastPresentedSurface = CurrentSurface;
#endif
    auto     LastTick               = std::chrono::steady_clock::now();

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

        bool ContinuousRefreshRequired = false;
#if FRONTIER_EDITOR_ENABLED
        if (CurrentSurface == ActiveHostSurface::WorldEditorSurface && EditorLoopPtr)
        {
            ContinuousRefreshRequired = EditorLoopPtr->QueryRequiresContinuousRefresh();
        }
        else if (StandaloneBrowserPtr != nullptr && StandaloneBrowserPtr->AccessPanel().QueryShelfAnimating())
        {
            ContinuousRefreshRequired = true;
        }
#endif
#if !defined(__ANDROID__)
        if (DesktopWindow.CloseRequested())
        {
            break;
        }
#endif

        FRONTIER_THOUGHT_SCOPE("Host.Frame");
        const auto   NowTick = std::chrono::steady_clock::now();
        const double Dt      = std::clamp(std::chrono::duration<double>(NowTick - LastTick).count(), 1.0e-4, 0.050);
        LastTick             = NowTick;
        float        UiBuildMs = 0.0f;

        uint32_t SurfaceW = InitialOptions.InitialWidthPx;
        uint32_t SurfaceH = InitialOptions.InitialHeightPx;

#if !defined(__ANDROID__)
        InputParticipant.BeginTick();
        DesktopWindow.PollOperatingEvents(InputParticipant);
        (void)InputParticipant.TickExchange();
        Input::InputExtension::AssignActiveExtension(&InputParticipant);

        const auto Extent = DesktopWindow.InspectExtent();
        if (Extent.SurfacePixelWidth == 0u || Extent.SurfacePixelHeight == 0u)
        {
            DesktopWindow.WaitOperatingEventsTimeout(0.05);
            continue;
        }
        SurfaceW = Extent.SurfacePixelWidth;
        SurfaceH = Extent.SurfacePixelHeight;
#if FRONTIER_EDITOR_ENABLED
        if (EditorStackReady && EditorLoopPtr)
        {
            if (auto* HostSurface = EditorLoopPtr->AccessDockContainer().AccessOperatingSurface(Components::HostWindowToken{ 0u }))
            {
                HostSurface->ResizeSurface(SurfaceW, SurfaceH, Extent.DpiScaleFactor);
            }
        }
        if (UvHostReady)
        {
            if (auto* HostSurface = StandaloneDock.AccessOperatingSurface(Components::HostWindowToken{ 0u }))
            {
                HostSurface->ResizeSurface(SurfaceW, SurfaceH, Extent.DpiScaleFactor);
            }
        }
        if (PatternHostReady)
        {
            if (auto* HostSurface = PatternDock.AccessOperatingSurface(Components::HostWindowToken{ 0u }))
            {
                HostSurface->ResizeSurface(SurfaceW, SurfaceH, Extent.DpiScaleFactor);
            }
        }
        if (StampHostReady)
        {
            if (auto* HostSurface = StampDock.AccessOperatingSurface(Components::HostWindowToken{ 0u }))
            {
                HostSurface->ResizeSurface(SurfaceW, SurfaceH, Extent.DpiScaleFactor);
            }
        }
        if (BakingHostReady)
        {
            if (auto* HostSurface = BakingDock.AccessOperatingSurface(Components::HostWindowToken{ 0u }))
            {
                HostSurface->ResizeSurface(SurfaceW, SurfaceH, Extent.DpiScaleFactor);
            }
        }
        if (MaterialHostReady && MaterialDockPtr)
        {
            if (auto* HostSurface = MaterialDockPtr->AccessOperatingSurface(Components::HostWindowToken{ 0u }))
            {
                HostSurface->ResizeSurface(SurfaceW, SurfaceH, Extent.DpiScaleFactor);
            }
        }
        if (SketcherHostReady && SketcherDockPtr)
        {
            if (auto* HostSurface = SketcherDockPtr->AccessOperatingSurface(Components::HostWindowToken{ 0u }))
            {
                HostSurface->ResizeSurface(SurfaceW, SurfaceH, Extent.DpiScaleFactor);
            }
        }
#else
        if (auto* HostSurface = StandaloneDock.AccessOperatingSurface(Components::HostWindowToken{ 0u }))
        {
            HostSurface->ResizeSurface(SurfaceW, SurfaceH, Extent.DpiScaleFactor);
        }
#endif

        (void)Input::ImGuiInputAdapter::ForwardEditorExchangeToImGui(InputParticipant, Io);

        const auto DroppedPaths = InputParticipant.ConsumeDroppedFilePaths();
#if FRONTIER_EDITOR_ENABLED
        if (CurrentSurface == ActiveHostSurface::ProjectBrowserSurface && HostProjectBrowserPtr)
        {
            for (const std::string& DropFile : DroppedPaths)
            {
                (void)HostProjectBrowserPtr->RegisterDiskPathInCollection(std::filesystem::path(DropFile), true);
            }
        }
        else if (CurrentSurface == ActiveHostSurface::BakingWorkspaceSurface && BakingHostReady && BakingHostPtr)
        {
            for (const std::string& DropFile : DroppedPaths)
            {
                std::string DropDiag {};
                const bool HighTarget = Io.KeyShift;
                (void)BakingHostPtr->IntakeExternalFileDrop(DropFile, HighTarget, DropDiag);
            }
        }
        else if (CurrentSurface == ActiveHostSurface::UvWorkspaceSurface && UvHostReady && UVHostPtr)
        {
            for (const std::string& DropFile : DroppedPaths)
            {
                std::string DropDiag {};
                (void)UVHostPtr->IntakeExternalFileDrop(DropFile, DropDiag);
            }
        }
        else if (CurrentSurface == ActiveHostSurface::PatternWorkspaceSurface && PatternHostReady && PatternHostPtr)
        {
            for (const std::string& DropFile : DroppedPaths)
            {
                std::string DropDiag {};
                (void)PatternHostPtr->IntakeExternalFileDrop(DropFile, DropDiag);
            }
        }
        else if (CurrentSurface == ActiveHostSurface::StampWorkspaceSurface && StampHostReady && StampHostPtr)
        {
            for (const std::string& DropFile : DroppedPaths)
            {
                std::string DropDiag {};
                (void)StampHostPtr->IntakeExternalFileDrop(DropFile, DropDiag);
            }
        }
        else if (CurrentSurface == ActiveHostSurface::SketcherWorkspaceSurface && SketcherHostReady && SketcherHostPtr)
        {
            for (const std::string& DropFile : DroppedPaths)
            {
                std::string DropDiag {};
                (void)SketcherHostPtr->IntakeExternalFileDrop(DropFile, &DropDiag);
            }
        }
#else
        for (const std::string& DropFile : DroppedPaths)
        {
            std::string DropDiag {};
            if (StandaloneBakingLaunch)
            {
                (void)BakingHost.IntakeExternalFileDrop(DropFile, Io.KeyShift, DropDiag);
            }
            else if (StandaloneVectorLaunch)
            {
                (void)ActiveDevVectorHost.IntakeExternalFileDrop(DropFile, DropDiag);
            }
            else
            {
                (void)UVHost.IntakeExternalFileDrop(DropFile, DropDiag);
            }
        }
#endif
        {
            const auto& Snap = InputParticipant.InspectEditorSnapshot();
            bool AnyKeyEdgeOrHold = false;
            for (std::size_t K = 0u; K < Input::LogicalKeySlotCount; ++K)
            {
                if (Snap.HeldLogicalKeys[K] || Snap.PressedLogicalKeys[K] ||
                    Snap.RepeatedLogicalKeys[K] || Snap.ReleasedLogicalKeys[K])
                {
                    AnyKeyEdgeOrHold = true;
                    break;
                }
            }
            const bool UserInputArrived =
                std::fabs(Snap.TickDeltaX) > 0.01f ||
                std::fabs(Snap.TickDeltaY) > 0.01f ||
                std::fabs(Snap.ScrollDeltaX) > 1.0e-4f ||
                std::fabs(Snap.ScrollDeltaY) > 1.0e-4f ||
                Snap.HeldButtonsMask != 0u ||
                Snap.PressedButtonsMask != 0u ||
                Snap.ReleasedButtonsMask != 0u ||
                Snap.DoubleClickedButtonsMask != 0u ||
                Snap.TypedUtf8Length > 0u ||
                Snap.TypedCodepointCount > 0u ||
                AnyKeyEdgeOrHold ||
                !DroppedPaths.empty();
            const bool SurfaceModified =
                SurfaceW != LastPresentedSurfaceW ||
                SurfaceH != LastPresentedSurfaceH ||
                std::fabs(Extent.DpiScaleFactor - LastPresentedDpiScale) > 1.0e-4f
#if FRONTIER_EDITOR_ENABLED
                || CurrentSurface != LastPresentedSurface
#endif
                ;
            if (UserInputArrived || SurfaceModified || ContinuousRefreshRequired)
            {
                SettleTicksRemaining = 3u;
            }
            else if (InitialOptions.BoundedStepLimit == 0u &&
                     SettleTicksRemaining == 0u &&
                     !Io.WantTextInput &&
                     !DesktopWindow.CloseRequested())
            {
                continue;
            }
        }
#endif

        Io.DisplaySize = ImVec2(static_cast<float>(SurfaceW), static_cast<float>(SurfaceH));
        Io.DeltaTime   = static_cast<float>(Dt);

        SwapchainPresenter.PrepareImGuiFrame();
        ImGui::NewFrame();
        const Rectangle2D FullBounds { 0.0f, 0.0f, static_cast<float>(SurfaceW), static_cast<float>(SurfaceH) };
#if FRONTIER_EDITOR_ENABLED
        if (CurrentSurface == ActiveHostSurface::ProjectBrowserSurface)
        {
            EnsureProjectBrowserReady();
            BindProjectBrowserLaunchCallback();
            HostProjectBrowserPtr->Render(FullBounds, Dt, 1.0f);
        }
        else if (CurrentSurface == ActiveHostSurface::WorldEditorSurface && EditorLoopPtr)
        {
            {
                FRONTIER_THOUGHT_SCOPE("Host.EditorTick");
                (void)EditorLoopPtr->ExecuteExternalDesktopTick(Dt);
            }
            Input::InputExtension::AssignActiveExtension(&InputParticipant);
            const auto UiStart = std::chrono::steady_clock::now();
            EditorLoopPtr->PresentPrimaryEditorImGui(Dt);
#if !defined(__ANDROID__)
            (void)InputParticipant.FlushOutboundDirectives(DesktopWindow);
#endif
            UiBuildMs = static_cast<float>(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - UiStart).count());
        }
        else if (CurrentSurface == ActiveHostSurface::FractureWorkspaceSurface && FractureHostPtr)
        {
            const float HostUiScale = std::clamp(StandaloneControlCentre.QueryInterfaceScaleFactor(), 0.75f, 2.0f);
            ControlAppearance::ConfigureTheme(StandaloneControlCentre.QueryThemeRepository());
            PixelSpace StudioSurface(ImGui::GetForegroundDrawList(), Point2D{ 0.0f, 0.0f }, HostUiScale);
            const Point2D MouseLogical = StudioSurface.QueryMousePosition();
            ContentBrowserPointerInput PtrInput {};
            if (!StandaloneControlCentre.QueryOpen())
            {
                PtrInput.CursorX       = MouseLogical.X;
                PtrInput.CursorY       = MouseLogical.Y;
                PtrInput.Pressed       = StudioSurface.QueryMouseClicked(0);
                PtrInput.HeldDown      = StudioSurface.QueryMouseDown(0);
                PtrInput.Released      = StudioSurface.QueryMouseReleased(0);
                PtrInput.DoubleClicked = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                PtrInput.WheelDeltaY   = Io.MouseWheel;
            }
            const Rectangle2D LogicalBounds {
                0.0f,
                0.0f,
                FullBounds.Width / HostUiScale,
                FullBounds.Height / HostUiScale,
            };
            FractureHostPtr->RenderStudio(StudioSurface, LogicalBounds, PtrInput, Dt);
            StandaloneControlCentre.Render(FullBounds, Dt, static_cast<float>(1.0 / Dt));
        }
        else if (CurrentSurface == ActiveHostSurface::ComponentEditorWorkspaceSurface && ComponentEditorHostPtr)
        {
            const float HostUiScale = std::clamp(StandaloneControlCentre.QueryInterfaceScaleFactor(), 0.75f, 2.0f);
            ControlAppearance::ConfigureTheme(StandaloneControlCentre.QueryThemeRepository());
            PixelSpace StudioSurface(ImGui::GetForegroundDrawList(), Point2D{ 0.0f, 0.0f }, HostUiScale);
            const Point2D MouseLogical = StudioSurface.QueryMousePosition();
            PaneInputReading PtrInput {};
            if (!StandaloneControlCentre.QueryOpen())
            {
                PtrInput.PrimaryPointer.X        = MouseLogical.X;
                PtrInput.PrimaryPointer.Y        = MouseLogical.Y;
                PtrInput.PrimaryPointer.Pressed  = StudioSurface.QueryMouseClicked(0);
                PtrInput.PrimaryPointer.Down     = StudioSurface.QueryMouseDown(0);
                PtrInput.PrimaryPointer.Released = StudioSurface.QueryMouseReleased(0);
                PtrInput.DoubleClicked           = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                PtrInput.SecondaryPressed        = StudioSurface.QueryMouseClicked(1);
                PtrInput.SecondaryDown           = StudioSurface.QueryMouseDown(1);
                PtrInput.SecondaryReleased       = StudioSurface.QueryMouseReleased(1);
                PtrInput.MiddlePressed           = StudioSurface.QueryMouseClicked(2);
                PtrInput.MiddleDown              = StudioSurface.QueryMouseDown(2);
                PtrInput.MiddleReleased          = StudioSurface.QueryMouseReleased(2);
                PtrInput.PointerDeltaX           = Io.MouseDelta.x;
                PtrInput.PointerDeltaY           = Io.MouseDelta.y;
                PtrInput.WheelDeltaY             = Io.MouseWheel;
                PtrInput.ShiftHeld               = Io.KeyShift;
                PtrInput.ControlHeld             = Io.KeyCtrl;
                PtrInput.AltHeld                 = Io.KeyAlt;
            }
            const Rectangle2D LogicalBounds {
                0.0f,
                0.0f,
                FullBounds.Width / HostUiScale,
                FullBounds.Height / HostUiScale,
            };
            ComponentEditorHostPtr->RenderWorkspace(StudioSurface, LogicalBounds, PtrInput, Dt);
            StandaloneControlCentre.Render(FullBounds, Dt, static_cast<float>(1.0 / Dt));
        }
        else if (CurrentSurface == ActiveHostSurface::SketcherWorkspaceSurface && SketcherHostPtr && SketcherDockPtr && SketcherBrowserPtr)
        {
            const float HostUiScale = std::clamp(StandaloneControlCentre.QueryInterfaceScaleFactor(), 0.75f, 2.0f);
            ControlAppearance::ConfigureTheme(StandaloneControlCentre.QueryThemeRepository());
            SketcherHostPtr->SynchronizeHostScale(HostUiScale);

            Components::ConfigureImGuiWorkspaceTheme(
                SketcherDockPtr->ActiveTabMetrics(),
                StandaloneControlCentre.QueryThemeRepository());
            auto& SketcherBrowserPanel = SketcherBrowserPtr->AccessPanel();
            SketcherBrowserPanel.SynchronizeHostBounds(FullBounds, HostUiScale);
            StandaloneControlCentre.SynchronizeHostBounds(FullBounds, HostUiScale);
            if (StandaloneControlCentre.QueryFullScreenBlocking() || SketcherBrowserPanel.QueryShelfDragging())
            {
                WorkspaceAppearance::AssignOverlayPointerShield(FullBounds);
            }
            else
            {
                WorkspaceAppearance::AssignOverlayPointerShield(
                    SketcherBrowserPanel.QueryDrawerPhysicalBounds(),
                    SketcherBrowserPanel.QueryNotchPhysicalBounds());
                WorkspaceAppearance::AppendOverlayPointerShield(StandaloneControlCentre.QueryShadePhysicalBounds());
                WorkspaceAppearance::AppendOverlayPointerShield(StandaloneControlCentre.QueryTabPhysicalBounds());
            }
            SketcherHostPtr->BeginFrame(Dt);
            SketcherDockPtr->PresentWindowImGuiPanes(Components::HostWindowToken{ 0u });
            SketcherHostPtr->EndFrame(Dt, true, true);
            WorkspaceAppearance::ClearOverlayPointerShield();
            SketcherBrowserPtr->Render(FullBounds, Dt, HostUiScale);
            StandaloneControlCentre.Render(FullBounds, Dt, static_cast<float>(1.0 / Dt));
        }
        else if (CurrentSurface == ActiveHostSurface::TexturePaintWorkspaceSurface && TexturePaintHostPtr && TexturePaintBrowserPtr)
        {
            const float HostUiScale = std::clamp(StandaloneControlCentre.QueryInterfaceScaleFactor(), 0.75f, 2.0f);
            ControlAppearance::ConfigureTheme(StandaloneControlCentre.QueryThemeRepository());
            auto& PaintBrowserPanel = TexturePaintBrowserPtr->AccessPanel();
            PaintBrowserPanel.SynchronizeHostBounds(FullBounds, HostUiScale);
            StandaloneControlCentre.SynchronizeHostBounds(FullBounds, HostUiScale);
            if (StandaloneControlCentre.QueryFullScreenBlocking() || PaintBrowserPanel.QueryShelfDragging())
            {
                WorkspaceAppearance::AssignOverlayPointerShield(FullBounds);
            }
            else
            {
                WorkspaceAppearance::AssignOverlayPointerShield(
                    PaintBrowserPanel.QueryDrawerPhysicalBounds(),
                    PaintBrowserPanel.QueryNotchPhysicalBounds());
                WorkspaceAppearance::AppendOverlayPointerShield(StandaloneControlCentre.QueryShadePhysicalBounds());
                WorkspaceAppearance::AppendOverlayPointerShield(StandaloneControlCentre.QueryTabPhysicalBounds());
            }
            TexturePaintHostPtr->PresentDockedLayout(Dt);
            WorkspaceAppearance::ClearOverlayPointerShield();
            TexturePaintBrowserPtr->Render(FullBounds, Dt, HostUiScale);
            StandaloneControlCentre.Render(FullBounds, Dt, static_cast<float>(1.0 / Dt));
        }
        else
        {
            Components::HeadlessWorkspaceDockContainer& ActiveDock =
                (CurrentSurface == ActiveHostSurface::MaterialWorkspaceSurface && MaterialDockPtr) ? *MaterialDockPtr :
                ((CurrentSurface == ActiveHostSurface::BakingWorkspaceSurface) ? BakingDock :
                ((CurrentSurface == ActiveHostSurface::PatternWorkspaceSurface) ? PatternDock :
                ((CurrentSurface == ActiveHostSurface::StampWorkspaceSurface) ? StampDock : StandaloneDock)));
            Editor::Explorer::ContentBrowser& ActiveBrowser =
                (CurrentSurface == ActiveHostSurface::MaterialWorkspaceSurface && MaterialBrowserPtr) ? *MaterialBrowserPtr :
                ((CurrentSurface == ActiveHostSurface::BakingWorkspaceSurface && BakingBrowserPtr) ? *BakingBrowserPtr :
                ((CurrentSurface == ActiveHostSurface::PatternWorkspaceSurface && PatternBrowserPtr) ? *PatternBrowserPtr :
                ((CurrentSurface == ActiveHostSurface::StampWorkspaceSurface && StampBrowserPtr) ? *StampBrowserPtr : *StandaloneBrowserPtr)));

            const float HostUiScale = std::clamp(StandaloneControlCentre.QueryInterfaceScaleFactor(), 0.75f, 2.0f);
            Components::ConfigureImGuiWorkspaceTheme(
                ActiveDock.ActiveTabMetrics(),
                StandaloneControlCentre.QueryThemeRepository());
            auto& ActiveBrowserPanel = ActiveBrowser.AccessPanel();
            ActiveBrowserPanel.SynchronizeHostBounds(FullBounds, HostUiScale);
            StandaloneControlCentre.SynchronizeHostBounds(FullBounds, HostUiScale);
            if (CurrentSurface == ActiveHostSurface::MaterialWorkspaceSurface && MaterialHostPtr)
            {
                MaterialHostPtr->SynchronizeContentBrowserOcclusion(HostUiScale);
            }
            if (StandaloneControlCentre.QueryFullScreenBlocking() || ActiveBrowserPanel.QueryShelfDragging())
            {
                WorkspaceAppearance::AssignOverlayPointerShield(FullBounds);
            }
            else
            {
                WorkspaceAppearance::AssignOverlayPointerShield(
                    ActiveBrowserPanel.QueryDrawerPhysicalBounds(),
                    ActiveBrowserPanel.QueryNotchPhysicalBounds());
                WorkspaceAppearance::AppendOverlayPointerShield(StandaloneControlCentre.QueryShadePhysicalBounds());
                WorkspaceAppearance::AppendOverlayPointerShield(StandaloneControlCentre.QueryTabPhysicalBounds());
            }
            ActiveDock.PresentWindowImGuiPanes(Components::HostWindowToken{ 0u });
            WorkspaceAppearance::ClearOverlayPointerShield();
            ActiveBrowser.Render(FullBounds, Dt, HostUiScale);
            StandaloneControlCentre.Render(FullBounds, Dt, static_cast<float>(1.0 / Dt));

            if (!StandaloneControlCentre.QueryOpen() && !InitialOptions.IsolateStandaloneWorkspace)
            {
                PixelSpace TopOverlay(ImGui::GetForegroundDrawList(), Point2D{ 0.0f, 0.0f }, HostUiScale);
                const Point2D MouseLogical = TopOverlay.QueryMousePosition();
                ControlPointer Ptr { MouseLogical.X, MouseLogical.Y, TopOverlay.QueryMouseDown(0), TopOverlay.QueryMouseClicked(0), TopOverlay.QueryMouseReleased(0) };
                const float LogicalW = FullBounds.Width / HostUiScale;
                float PillX = std::max(280.0f, LogicalW * 0.5f + 10.0f);

                const ButtonTone PaintTone = (CurrentSurface == ActiveHostSurface::TexturePaintWorkspaceSurface) ? ButtonTone::Primary : ButtonTone::Secondary;
                if (CapsuleButton::Construct(TopOverlay, { PillX, 3.0f, 56.0f, 23.0f }, ButtonDescriptor{ "Paint", PaintTone, 10.2f, 11.5f }, Ptr).Pressed &&
                    CurrentSurface != ActiveHostSurface::TexturePaintWorkspaceSurface)
                {
                    if (EnsureTexturePaintHostReady())
                    {
                        CurrentSurface = ActiveHostSurface::TexturePaintWorkspaceSurface;
                    }
                }
                PillX += 62.0f;

                const ButtonTone BakeTone = (CurrentSurface == ActiveHostSurface::BakingWorkspaceSurface) ? ButtonTone::Primary : ButtonTone::Secondary;
                if (CapsuleButton::Construct(TopOverlay, { PillX, 3.0f, 56.0f, 23.0f }, ButtonDescriptor{ "Bake", BakeTone, 10.2f, 11.5f }, Ptr).Pressed &&
                    CurrentSurface != ActiveHostSurface::BakingWorkspaceSurface)
                {
                    if (EnsureBakingHostReady({}))
                    {
                        CurrentSurface = ActiveHostSurface::BakingWorkspaceSurface;
                    }
                }
                PillX += 62.0f;

                const ButtonTone PatTone = (CurrentSurface == ActiveHostSurface::PatternWorkspaceSurface) ? ButtonTone::Primary : ButtonTone::Secondary;
                if (CapsuleButton::Construct(TopOverlay, { PillX, 3.0f, 68.0f, 23.0f }, ButtonDescriptor{ "Pattern", PatTone, 10.2f, 11.5f }, Ptr).Pressed &&
                    CurrentSurface != ActiveHostSurface::PatternWorkspaceSurface)
                {
                    if (EnsurePatternHostReady({}))
                    {
                        CurrentSurface = ActiveHostSurface::PatternWorkspaceSurface;
                    }
                }
                PillX += 74.0f;

                const ButtonTone StTone = (CurrentSurface == ActiveHostSurface::StampWorkspaceSurface) ? ButtonTone::Primary : ButtonTone::Secondary;
                if (CapsuleButton::Construct(TopOverlay, { PillX, 3.0f, 62.0f, 23.0f }, ButtonDescriptor{ "Stamp", StTone, 10.2f, 11.5f }, Ptr).Pressed &&
                    CurrentSurface != ActiveHostSurface::StampWorkspaceSurface)
                {
                    if (EnsureStampHostReady({}))
                    {
                        CurrentSurface = ActiveHostSurface::StampWorkspaceSurface;
                    }
                }
                PillX += 68.0f;

                const ButtonTone UvTone = (CurrentSurface == ActiveHostSurface::UvWorkspaceSurface) ? ButtonTone::Primary : ButtonTone::Secondary;
                if (CapsuleButton::Construct(TopOverlay, { PillX, 3.0f, 46.0f, 23.0f }, ButtonDescriptor{ "UV", UvTone, 10.2f, 11.5f }, Ptr).Pressed &&
                    CurrentSurface != ActiveHostSurface::UvWorkspaceSurface)
                {
                    if (EnsureUvHostReady({}))
                    {
                        CurrentSurface = ActiveHostSurface::UvWorkspaceSurface;
                    }
                }
                PillX += 52.0f;

                const ButtonTone FracTone = (CurrentSurface == ActiveHostSurface::FractureWorkspaceSurface) ? ButtonTone::Primary : ButtonTone::Secondary;
                if (CapsuleButton::Construct(TopOverlay, { PillX, 3.0f, 70.0f, 23.0f }, ButtonDescriptor{ "Fracture", FracTone, 10.2f, 11.5f }, Ptr).Pressed &&
                    CurrentSurface != ActiveHostSurface::FractureWorkspaceSurface)
                {
                    if (EnsureFractureHostReady())
                    {
                        CurrentSurface = ActiveHostSurface::FractureWorkspaceSurface;
                    }
                }
                PillX += 76.0f;

                const ButtonTone CeTone = (CurrentSurface == ActiveHostSurface::ComponentEditorWorkspaceSurface) ? ButtonTone::Primary : ButtonTone::Secondary;
                if (CapsuleButton::Construct(TopOverlay, { PillX, 3.0f, 92.0f, 23.0f }, ButtonDescriptor{ "Components", CeTone, 10.2f, 11.5f }, Ptr).Pressed &&
                    CurrentSurface != ActiveHostSurface::ComponentEditorWorkspaceSurface)
                {
                    if (EnsureComponentEditorHostReady())
                    {
                        CurrentSurface = ActiveHostSurface::ComponentEditorWorkspaceSurface;
                    }
                }
                PillX += 98.0f;

                if (CapsuleButton::Construct(TopOverlay, { PillX, 3.0f, 76.0f, 23.0f }, ButtonDescriptor{ "Projects", ButtonTone::Secondary, 10.2f, 11.5f }, Ptr).Pressed)
                {
                    EnsureProjectBrowserReady();
                    BindProjectBrowserLaunchCallback();
                    CurrentSurface = ActiveHostSurface::ProjectBrowserSurface;
                }
            }
        }
#else
        const float HostUiScale = std::clamp(StandaloneControlCentre.QueryInterfaceScaleFactor(), 0.75f, 2.0f);
        Components::ConfigureImGuiWorkspaceTheme(
            StandaloneDock.ActiveTabMetrics(),
            StandaloneControlCentre.QueryThemeRepository());
        StandaloneBrowserPanel.SynchronizeHostBounds(FullBounds, HostUiScale);
        StandaloneControlCentre.SynchronizeHostBounds(FullBounds, HostUiScale);
        if (StandaloneControlCentre.QueryFullScreenBlocking() || StandaloneBrowserPanel.QueryShelfDragging())
        {
            WorkspaceAppearance::AssignOverlayPointerShield(FullBounds);
        }
        else
        {
            WorkspaceAppearance::AssignOverlayPointerShield(
                StandaloneBrowserPanel.QueryDrawerPhysicalBounds(),
                StandaloneBrowserPanel.QueryNotchPhysicalBounds());
            WorkspaceAppearance::AppendOverlayPointerShield(StandaloneControlCentre.QueryShadePhysicalBounds());
            WorkspaceAppearance::AppendOverlayPointerShield(StandaloneControlCentre.QueryTabPhysicalBounds());
        }
        StandaloneDock.PresentWindowImGuiPanes(Components::HostWindowToken{ 0u });
        WorkspaceAppearance::ClearOverlayPointerShield();
        StandaloneBrowserPanel.Render(FullBounds, Dt, HostUiScale);
        StandaloneControlCentre.Render(FullBounds, Dt, static_cast<float>(1.0 / Dt));
#endif
        ImGui::Render();

        const auto PresentStart = std::chrono::steady_clock::now();
        if (!SwapchainPresenter.PresentImGuiBatches(ImGui::GetDrawData(), SurfaceW, SurfaceH))
        {
            const std::string Diagnostic(SwapchainPresenter.QueryLastPresentationDiagnostic());
            std::fprintf(stderr, "[FrontierHost] ImGui Vulkan presentation failed: %s\n", Diagnostic.c_str());
            SwapchainPresenter.TeardownOverlay();
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
        const auto PresentEnd = std::chrono::steady_clock::now();
#if FRONTIER_EDITOR_ENABLED
        bool WorldDispatchedThisTick = false;
        if (CurrentSurface == ActiveHostSurface::WorldEditorSurface && EditorLoopPtr)
        {
            const float PresentMs = static_cast<float>(
                std::chrono::duration<double, std::milli>(PresentEnd - PresentStart).count());
            const float TotalFrameMs = static_cast<float>(
                std::chrono::duration<double, std::milli>(PresentEnd - NowTick).count());
            EditorLoopPtr->RecordFramePresentationTiming(UiBuildMs, PresentMs, TotalFrameMs);
            WorldDispatchedThisTick = EditorLoopPtr->QueryLatestViewportTiming().WorldDispatched;
        }
#else
        constexpr bool WorldDispatchedThisTick = false;
#endif
#if !defined(__ANDROID__)
        LastPresentedSurfaceW = SurfaceW;
        LastPresentedSurfaceH = SurfaceH;
        LastPresentedDpiScale = DesktopWindow.InspectExtent().DpiScaleFactor;
#if FRONTIER_EDITOR_ENABLED
        const bool SurfaceSwitchedThisTick = (CurrentSurface != LastPresentedSurface);
        LastPresentedSurface               = CurrentSurface;
#else
        constexpr bool SurfaceSwitchedThisTick = false;
#endif
        if (WorldDispatchedThisTick || SurfaceSwitchedThisTick)
        {
            SettleTicksRemaining = std::max(SettleTicksRemaining, 2u);
        }
        else if (SettleTicksRemaining > 0u)
        {
            --SettleTicksRemaining;
        }
        (void)WorldDispatchedThisTick;
#endif
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
    ReleaseTimingLogOnExit();
#if FRONTIER_EDITOR_ENABLED
    if (HostProjectBrowserPtr)
    {
        HostProjectBrowserPtr->ReleaseGpuTextures();
        HostProjectBrowserPtr->AttachVulkanWorldRenderer(nullptr, {});
    }
    if (EditorStackReady && EditorLoopPtr)
    {
        EditorLoopPtr->Finalize();
    }
    if (UvHostReady && UVHostPtr)
    {
        StandaloneDock.Finalize();
        UVHostPtr->AccessSession().TeardownGPUSolvers();
    }
    if (PatternHostReady)
    {
        PatternDock.Finalize();
    }
    if (StampHostReady)
    {
        StampDock.Finalize();
    }
    if (BakingHostReady && BakingHostPtr)
    {
        BakingDock.Finalize();
        BakingHostPtr->AttachVulkanDevice(nullptr);
    }
    if (TexturePaintHostReady && TexturePaintHostPtr)
    {
        TexturePaintHostPtr->Shutdown();
    }
    if (MaterialHostReady && MaterialDockPtr && MaterialHostPtr)
    {
        MaterialDockPtr->Finalize();
        MaterialHostPtr->AccessPreviewRenderer().TeardownVulkanPreview();
    }
    if (SketcherHostReady && SketcherHostPtr && SketcherDockPtr)
    {
        SketcherHostPtr->Finalize();
        SketcherDockPtr->Finalize();
    }
    FractureHostPtr.reset();
    SketcherHostPtr.reset();
    MaterialHostPtr.reset();
    TexturePaintHostPtr.reset();
    BakingHostPtr.reset();
    StampHostPtr.reset();
    PatternHostPtr.reset();
    UVHostPtr.reset();
    EditorLoopPtr.reset();
    HostProjectBrowserPtr.reset();
#else
    StandaloneDock.Finalize();
    if (StandaloneBakingLaunch)
    {
        BakingHost.AttachVulkanDevice(nullptr);
    }
    if (StandaloneUVLaunch)
    {
        UVHost.AccessSession().TeardownGPUSolvers();
    }
#endif
    if (UvStackInitialized)
    {
        UVRasterizer.TeardownRasterizer();
    }
    if (VectorStackInitialized)
    {
        VectorRasterizer.TeardownRasterizer();
    }
    InputParticipant.Finalize();
#if !defined(__ANDROID__)
    DesktopWindow.Finalize();
#endif
    TypographyRepository::AssignCurrent(nullptr);
    ImGui::DestroyContext(GuiCtx);
    Diagnostics::ThoughtLog::Shutdown(); // Application closing: flush the thought log and close it
    return 0;
}

} // namespace Frontier::Lifecycle
