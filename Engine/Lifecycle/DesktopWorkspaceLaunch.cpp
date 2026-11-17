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
#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/ControlCentre/ControlCentrePanel.h"
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
#include "Engine/Workspaces/Fluid/FluidWorkspaceHost.h"
#include "Engine/Workspaces/Fracture/FractureWorkspaceHost.h"
#include "Engine/Workspaces/Material/MaterialWorkspaceHost.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherWorkspaceHost.h"
#include "Engine/Workspaces/Particle/ParticleWorkspaceHost.h"
#include "Engine/Workspaces/Texturing/BakingWorkspaceHost.h"
#include "Engine/Workspaces/Texturing/PatternStampWorkspaceHost.h"
#include "Engine/Workspaces/UV/UVWorkspaceHost.h"

#if FRONTIER_EDITOR_ENABLED
#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/Panes/InspectorPanel.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/Panes/ViewportPanel.h"
#include "Engine/Editor/Explorer/ContentBrowser.h"
#include "Engine/Editor/Explorer/ProjectBrowser.h"
#include "Engine/Editor/Workspaces/Simulation/VectorFieldViewportSession.h"
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
        else if (Arg == "--standalone-particle" || Arg == "--standalone=particle" || Arg == "--workspace=particle" || Arg == "--particle")
        {
            InitialOptions.OpenWorldEditor    = false;
            InitialOptions.OpenProjectBrowser = false;
            InitialOptions.WindowTitle        = "Frontier Particle";
        }
        else if (Arg == "--standalone-forcefield" || Arg == "--standalone=forcefield" || Arg == "--workspace=forcefield" || Arg == "--workspace=simulation" || Arg == "--forcefield")
        {
            InitialOptions.OpenWorldEditor    = false;
            InitialOptions.OpenProjectBrowser = false;
            InitialOptions.WindowTitle        = "Frontier ForceField";
        }
        else if (Arg == "--standalone-fluid" || Arg == "--standalone=fluid" || Arg == "--workspace=fluid" || Arg == "--fluid")
        {
            InitialOptions.OpenWorldEditor    = false;
            InitialOptions.OpenProjectBrowser = false;
            InitialOptions.WindowTitle        = "Frontier Fluid";
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
        else if (Arg.starts_with("--project="))
        {
            InitialOptions.StartupProjectSpacePath = std::filesystem::path(Arg.substr(10u));
            InitialOptions.OpenProjectBrowser      = false;
            InitialOptions.OpenWorldEditor         = true;
        }
    }

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
        MaterialWorkspaceSurface   = 6u,
        ParticleWorkspaceSurface   = 7u,
        ForceFieldWorkspaceSurface = 8u,
        FractureWorkspaceSurface   = 9u,
        FluidWorkspaceSurface      = 10u,
        SketcherWorkspaceSurface   = 11u,
    };

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
    const bool StandaloneParticleLaunch =
        (EngineConfig.QueryWorkspaceCategory() == WorkspaceCategory::Particle) &&
        !InitialOptions.OpenWorldEditor &&
        !InitialOptions.OpenProjectBrowser;
    const bool StandaloneForceFieldLaunch =
        (EngineConfig.QueryWorkspaceCategory() == WorkspaceCategory::Simulation) &&
        !InitialOptions.OpenWorldEditor &&
        !InitialOptions.OpenProjectBrowser;
    const bool StandaloneFractureLaunch =
        (EngineConfig.QueryWorkspaceCategory() == WorkspaceCategory::Fracture) &&
        !InitialOptions.OpenWorldEditor &&
        !InitialOptions.OpenProjectBrowser;
    const bool StandaloneFluidLaunch =
        (EngineConfig.QueryWorkspaceCategory() == WorkspaceCategory::Fluid) &&
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
    else if (StandaloneMaterialLaunch)
    {
        CurrentSurface = ActiveHostSurface::MaterialWorkspaceSurface;
    }
    else if (StandaloneParticleLaunch)
    {
        CurrentSurface = ActiveHostSurface::ParticleWorkspaceSurface;
    }
    else if (StandaloneForceFieldLaunch)
    {
        CurrentSurface = ActiveHostSurface::ForceFieldWorkspaceSurface;
    }
    else if (StandaloneFractureLaunch)
    {
        CurrentSurface = ActiveHostSurface::FractureWorkspaceSurface;
    }
    else if (StandaloneFluidLaunch)
    {
        CurrentSurface = ActiveHostSurface::FluidWorkspaceSurface;
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

    Workspaces::UVWorkspaceHost           UVHost {};
    Workspaces::PatternStampWorkspaceHost PatternHost(Workspaces::PatternStampWorkspaceMode::Pattern);
    Workspaces::PatternStampWorkspaceHost StampHost(Workspaces::PatternStampWorkspaceMode::Stamp);
    Workspaces::BakingWorkspaceHost       BakingHost {};

    Components::HeadlessWorkspaceDockContainer StandaloneDock {};
    Components::HeadlessWorkspaceDockContainer PatternDock {};
    Components::HeadlessWorkspaceDockContainer StampDock {};
    Components::HeadlessWorkspaceDockContainer BakingDock {};
    ControlCentrePanel                         StandaloneControlCentre {};
    Input::InputExtension                      InputParticipant {};
    InputParticipant.Initialize();

#if FRONTIER_EDITOR_ENABLED
    Workspaces::MaterialWorkspaceHost                  MaterialHost {};
    Workspaces::ParticleWorkspaceHost                  ParticleHost {};
    Workspaces::Fracture::FractureWorkspaceHost        FractureHost {};
    Workspaces::FluidWorkspaceHost                     FluidHost {};
    Workspaces::SketcherWorkspaceHost                  SketcherHost {};
    Components::HeadlessWorkspaceDockContainer         MaterialDock {};
    Components::HeadlessWorkspaceDockContainer         ForceFieldDock {};
    Components::HeadlessWorkspaceDockContainer         FluidDock {};
    Components::HeadlessWorkspaceDockContainer         SketcherDock {};
    Lifecycle::EditorLifecycle                         EditorLoop {};
    Editor::Explorer::ContentBrowser                   StandaloneBrowser {};
    Editor::Explorer::ContentBrowser                   PatternBrowser {};
    Editor::Explorer::ContentBrowser                   StampBrowser {};
    Editor::Explorer::ContentBrowser                   BakingBrowser {};
    Editor::Explorer::ContentBrowser                   MaterialBrowser {};
    Editor::Explorer::ContentBrowser                   ParticleBrowser {};
    Editor::Explorer::ContentBrowser                   FractureBrowser {};
    Editor::Explorer::ContentBrowser                   FluidBrowser {};
    Editor::Explorer::ContentBrowser                   SketcherBrowser {};
    Editor::Explorer::ProjectBrowser                   HostProjectBrowser {};
    OutlinerPanel                                      ForceOutliner {};
    ViewportPanel                                      ForceViewport {};
    InspectorPanel                                     ForceInspector {};
    Editor::Simulation::VectorFieldViewportSession ForceSession {};
    double                                             LastFrameDtSeconds        = 1.0 / 60.0;
    float                                              LastHostUiScale           = 1.0f;
    bool                                               EditorStackReady          = false;
    bool                                               UvHostReady               = false;
    bool                                               PatternHostReady          = false;
    bool                                               StampHostReady            = false;
    bool                                               BakingHostReady           = false;
    bool                                               MaterialHostReady         = false;
    bool                                               ParticleHostReady         = false;
    bool                                               ForceFieldHostReady       = false;
    bool                                               FluidHostReady            = false;
    bool                                               SketcherHostReady         = false;
    bool                                               FractureHostReady         = false;
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
        HostProjectBrowser.AttachVulkanWorldRenderer(&DeviceInterchange, CompiledShaderDir);
        HostProjectBrowser.InitializeDefaultCollection(
            RepositoryRootPath / "Projects",
            RepositoryRootPath,
            CompiledShaderDir.parent_path());
        if (InitialOptions.PreviewReaderOnStart)
        {
            HostProjectBrowser.ActivateEntryByIndex(0);
        }
        if (InitialOptions.OpenNewProjectModal)
        {
            HostProjectBrowser.OpenNewProjectSheet("AeroGallery", RepositoryRootPath / "Projects");
        }
        ProjectBrowserInitialized = true;
    };

    auto EnsureEditorLifecycleReady = [&](const std::filesystem::path& ProjectSpaceFile, std::string_view ProjectTitle) -> bool
    {
        if (!EditorStackReady)
        {
            Lifecycle::EditorStartupConfiguration EditorCfg {};
            EditorCfg.WindowDescriptor = Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
                InitialOptions.WindowTitle, InitialOptions.InitialWidthPx, InitialOptions.InitialHeightPx);
            EditorCfg.FontRepositoryRoot      = RepositoryRootPath / "EngineContent/FontsArchives";
            EditorCfg.ProjectSpacePath        = ProjectSpaceFile.empty() ? InitialOptions.StartupProjectSpacePath : ProjectSpaceFile;
            EditorCfg.PopulateShowcaseScene   = true;
            EditorCfg.VulkanDevice            = &DeviceInterchange;
            EditorCfg.CompiledShaderDirectory = CompiledShaderDir;
            if (!EditorLoop.Initialize(EditorCfg))
            {
                std::fprintf(stderr, "[FrontierHost] EditorLifecycle initialization failed.\n");
                return false;
            }
            EditorLoop.AssignReturnToProjectBrowserCallback(
                [&]()
                {
                    EnsureProjectBrowserReady();
                    CurrentSurface = ActiveHostSurface::ProjectBrowserSurface;
                });
            auto& EditorBrowserPanel = EditorLoop.AccessContentBrowser().AccessPanel();
            EditorBrowserPanel.ConfigureShelfExtent(
                InitialOptions.OpenContentBrowser ? ContentShelfExtent::SplitHalf : ContentShelfExtent::Closed,
                true);
            (void)EditorBrowserPanel.ActivateCategory(ContentCategory::Colours);
            EditorStackReady = true;
        }
        if (!ProjectTitle.empty())
        {
            EditorLoop.ConfigureActiveProjectTitle(ProjectTitle);
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
            UVHost.ConfigureLaunchPolicy(EngineConfig);
            (void)UVHost.AccessSession().InitializeGPUSolvers(&DeviceInterchange, CompiledShaderDir);
            (void)UVHost.AttachVulkanRasterizer(&UVRasterizer);

            ThemeRepository ThemeRepo {};
            (void)StandaloneDock.Initialize(
                Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
                    "Frontier UV", InitialOptions.InitialWidthPx, InitialOptions.InitialHeightPx));
            Components::ConfigureImGuiWorkspaceStyle(StandaloneDock.ActiveTabMetrics());
            Components::ConfigureImGuiWorkspaceTheme(StandaloneDock.ActiveTabMetrics(), ThemeRepo);

            StandaloneBrowser.AttachThemeRepository(&StandaloneControlCentre.QueryThemeRepository());
            UVHost.AttachContentBrowser(&StandaloneBrowser);
            StandaloneBrowser.AccessPanel().ConfigureShelfExtent(
                InitialOptions.OpenContentBrowser ? ContentShelfExtent::SplitHalf : ContentShelfExtent::Closed,
                true);
            (void)StandaloneBrowser.AccessPanel().ActivateCategory(ContentCategory::Colours);
            UVHost.RegisterDockPanes(StandaloneDock);
            UvHostReady = true;
        }
        const std::filesystem::path PathToLoad = GeometryPath.empty() ? InitialOptions.StartupImportPath : GeometryPath;
        if (!PathToLoad.empty())
        {
            std::string ImportDiag {};
            const std::string Ext = PathToLoad.extension().string();
            if (Ext == ".obj" || Ext == ".OBJ" || Ext == ".GeometrySpace" || Ext == ".geometryspace")
            {
                (void)UVHost.IntakeExternalFileDrop(PathToLoad, ImportDiag);
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
            PatternHost.ConfigureLaunchPolicy(EngineConfig);
            PatternHost.ConfigureRepositoryRoot(RepositoryRootPath);
            PatternHost.AttachVulkanRasterizer(&VectorRasterizer);

            ThemeRepository ThemeRepo {};
            (void)PatternDock.Initialize(
                Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
                    "Frontier Pattern", InitialOptions.InitialWidthPx, InitialOptions.InitialHeightPx));
            Components::ConfigureImGuiWorkspaceStyle(PatternDock.ActiveTabMetrics());
            Components::ConfigureImGuiWorkspaceTheme(PatternDock.ActiveTabMetrics(), ThemeRepo);

            PatternBrowser.AttachThemeRepository(&StandaloneControlCentre.QueryThemeRepository());
            PatternHost.AttachContentBrowser(&PatternBrowser);
            PatternBrowser.AccessPanel().ConfigureShelfExtent(
                InitialOptions.OpenContentBrowser ? ContentShelfExtent::SplitHalf : ContentShelfExtent::Closed,
                true);
            (void)PatternBrowser.AccessPanel().ActivateCategory(ContentCategory::Vectors);
            PatternHost.RegisterDockPanes(PatternDock);
            PatternHostReady = true;
        }
        const std::filesystem::path PathToLoad = VectorPath.empty() ? InitialOptions.StartupImportPath : VectorPath;
        if (!PathToLoad.empty() && std::filesystem::exists(PathToLoad))
        {
            std::string ImportDiag {};
            const std::string Ext = PathToLoad.extension().string();
            if (Ext == ".VectorSpace" || Ext == ".vectorspace" || Ext == ".svg" || Ext == ".SVG")
            {
                (void)PatternHost.IntakeExternalFileDrop(PathToLoad, ImportDiag);
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
            StampHost.ConfigureLaunchPolicy(EngineConfig);
            StampHost.ConfigureRepositoryRoot(RepositoryRootPath);
            StampHost.AttachVulkanRasterizer(&VectorRasterizer);

            ThemeRepository ThemeRepo {};
            (void)StampDock.Initialize(
                Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
                    "Frontier Stamp", InitialOptions.InitialWidthPx, InitialOptions.InitialHeightPx));
            Components::ConfigureImGuiWorkspaceStyle(StampDock.ActiveTabMetrics());
            Components::ConfigureImGuiWorkspaceTheme(StampDock.ActiveTabMetrics(), ThemeRepo);

            StampBrowser.AttachThemeRepository(&StandaloneControlCentre.QueryThemeRepository());
            StampHost.AttachContentBrowser(&StampBrowser);
            StampBrowser.AccessPanel().ConfigureShelfExtent(
                InitialOptions.OpenContentBrowser ? ContentShelfExtent::SplitHalf : ContentShelfExtent::Closed,
                true);
            (void)StampBrowser.AccessPanel().ActivateCategory(ContentCategory::Vectors);
            StampHost.RegisterDockPanes(StampDock);
            StampHostReady = true;
        }
        const std::filesystem::path PathToLoad = VectorPath.empty() ? InitialOptions.StartupImportPath : VectorPath;
        if (!PathToLoad.empty() && std::filesystem::exists(PathToLoad))
        {
            std::string ImportDiag {};
            const std::string Ext = PathToLoad.extension().string();
            if (Ext == ".VectorSpace" || Ext == ".vectorspace" || Ext == ".svg" || Ext == ".SVG")
            {
                (void)StampHost.IntakeExternalFileDrop(PathToLoad, ImportDiag);
            }
        }
        return true;
    };

    auto EnsureBakingHostReady = [&](const std::filesystem::path& MeshPath) -> bool
    {
        EnsureSharedControlCentreReady();
        if (!BakingHostReady)
        {
            BakingHost.ConfigureLaunchPolicy(EngineConfig);
            BakingHost.AttachVulkanDevice(&DeviceInterchange, CompiledShaderDir);

            ThemeRepository ThemeRepo {};
            (void)BakingDock.Initialize(
                Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
                    "Frontier Baking", InitialOptions.InitialWidthPx, InitialOptions.InitialHeightPx));
            Components::ConfigureImGuiWorkspaceStyle(BakingDock.ActiveTabMetrics());
            Components::ConfigureImGuiWorkspaceTheme(BakingDock.ActiveTabMetrics(), ThemeRepo);

            BakingBrowser.AttachThemeRepository(&StandaloneControlCentre.QueryThemeRepository());
            BakingHost.AttachContentBrowser(&BakingBrowser);
            BakingBrowser.AccessPanel().ConfigureShelfExtent(
                InitialOptions.OpenContentBrowser ? ContentShelfExtent::SplitHalf : ContentShelfExtent::Closed,
                true);
            (void)BakingBrowser.AccessPanel().ActivateCategory(ContentCategory::Colours);
            BakingHost.RegisterDockPanes(BakingDock);
            BakingHostReady = true;
        }
        const std::filesystem::path PathToLoad = MeshPath.empty() ? InitialOptions.StartupImportPath : MeshPath;
        if (!PathToLoad.empty() && std::filesystem::exists(PathToLoad))
        {
            std::string ImportDiag {};
            const std::string Ext = PathToLoad.extension().string();
            if (Ext == ".obj" || Ext == ".OBJ" || Ext == ".stl" || Ext == ".STL")
            {
                (void)BakingHost.IntakeExternalFileDrop(PathToLoad.string(), false, ImportDiag);
            }
        }
        return true;
    };

    auto EnsureMaterialHostReady = [&]() -> bool
    {
        EnsureSharedControlCentreReady();
        if (!MaterialHostReady)
        {
            MaterialHost.ConfigureLaunchPolicy(EngineConfig);
            (void)MaterialHost.AttachVulkanDevice(&DeviceInterchange, CompiledShaderDir);

            ThemeRepository ThemeRepo {};
            (void)MaterialDock.Initialize(
                Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
                    "Frontier Material", InitialOptions.InitialWidthPx, InitialOptions.InitialHeightPx));
            Components::ConfigureImGuiWorkspaceStyle(MaterialDock.ActiveTabMetrics());
            Components::ConfigureImGuiWorkspaceTheme(MaterialDock.ActiveTabMetrics(), ThemeRepo);

            MaterialBrowser.AttachThemeRepository(&StandaloneControlCentre.QueryThemeRepository());
            MaterialBrowser.AccessPanel().ConfigureOriginFilter(ContentOriginFilter::Project);
            MaterialHost.AttachContentBrowser(&MaterialBrowser);
            MaterialBrowser.AccessPanel().ConfigureShelfExtent(
                InitialOptions.OpenContentBrowser ? ContentShelfExtent::SplitHalf : ContentShelfExtent::Closed,
                true);
            MaterialHost.RegisterDockPanes(MaterialDock);
            MaterialHostReady = true;
        }
        return true;
    };

    auto EnsureParticleHostReady = [&]() -> bool
    {
        EnsureSharedControlCentreReady();
        if (!ParticleHostReady)
        {
            ControlAppearance::ConfigureTheme(StandaloneControlCentre.QueryThemeRepository());
            ParticleBrowser.AttachThemeRepository(&StandaloneControlCentre.QueryThemeRepository());
            ParticleBrowser.AccessPanel().ConfigureWorkspaceDomain(WorkspaceCategory::Particle);
            ParticleBrowser.AccessPanel().ConfigureShelfExtent(ContentShelfExtent::Closed, true);
            (void)ParticleBrowser.AccessPanel().ActivateCategory(ContentCategory::Colours);
            ParticleBrowser.AccessPanel().ConfigureColourCollectionScope(ColourCollectionScope::Ramps);
            ParticleHost.AttachContentBrowserPanel(&ParticleBrowser.AccessPanel());

            std::string ParticleVkDiag {};
            (void)ParticleHost.AccessSession().VulkanExecutor.Initialize(CompiledShaderDir, ParticleVkDiag);
            ParticleHostReady = true;
        }
        return true;
    };

    auto EnsureFractureHostReady = [&]() -> bool
    {
        EnsureSharedControlCentreReady();
        if (!FractureHostReady)
        {
            ControlAppearance::ConfigureTheme(StandaloneControlCentre.QueryThemeRepository());
            FractureBrowser.AttachThemeRepository(&StandaloneControlCentre.QueryThemeRepository());
            FractureBrowser.AccessPanel().ConfigureWorkspaceDomain(WorkspaceCategory::Fracture);
            FractureBrowser.AccessPanel().ConfigureShelfExtent(ContentShelfExtent::Closed, true);
            FractureHost.AttachContentBrowserPanel(&FractureBrowser.AccessPanel());
            FractureHost.AttachVulkanDevice(DeviceInterchange, CompiledShaderDir);
            FractureHostReady = true;
        }
        return true;
    };

    auto EnsureFluidHostReady = [&]() -> bool
    {
        EnsureSharedControlCentreReady();
        if (!FluidHostReady)
        {
            ControlAppearance::ConfigureTheme(StandaloneControlCentre.QueryThemeRepository());
            FluidBrowser.AttachThemeRepository(&StandaloneControlCentre.QueryThemeRepository());
            FluidBrowser.AccessPanel().ConfigureWorkspaceDomain(WorkspaceCategory::Fluid);
            FluidBrowser.AccessPanel().ConfigureShelfExtent(ContentShelfExtent::Closed, true);
            FluidHost.AttachContentBrowserPanel(&FluidBrowser.AccessPanel());

            (void)FluidDock.Initialize(
                Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
                    "Frontier Fluid", InitialOptions.InitialWidthPx, InitialOptions.InitialHeightPx));
            Components::ConfigureImGuiWorkspaceStyle(FluidDock.ActiveTabMetrics());
            Components::ConfigureImGuiWorkspaceTheme(
                FluidDock.ActiveTabMetrics(),
                StandaloneControlCentre.QueryThemeRepository());
            FluidHost.RegisterDockPanes(FluidDock);
            FluidHostReady = true;
        }
        return true;
    };

    auto EnsureSketcherHostReady = [&]() -> bool
    {
        EnsureSharedControlCentreReady();
        if (!SketcherHostReady)
        {
            ControlAppearance::ConfigureTheme(StandaloneControlCentre.QueryThemeRepository());
            SketcherBrowser.AttachThemeRepository(&StandaloneControlCentre.QueryThemeRepository());
            SketcherBrowser.AccessPanel().ConfigureWorkspaceDomain(WorkspaceCategory::GeometryCad);
            SketcherBrowser.AccessPanel().ConfigureShelfExtent(ContentShelfExtent::Closed, true);
            SketcherHost.AttachContentBrowserPanel(&SketcherBrowser.AccessPanel());

            (void)SketcherDock.Initialize(
                Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
                    "Frontier Parametric Sketcher", InitialOptions.InitialWidthPx, InitialOptions.InitialHeightPx));
            Components::ConfigureImGuiWorkspaceStyle(SketcherDock.ActiveTabMetrics());
            Components::ConfigureImGuiWorkspaceTheme(
                SketcherDock.ActiveTabMetrics(),
                StandaloneControlCentre.QueryThemeRepository());

            std::string SketcherDiag {};
            const std::string InitialDoc = InitialOptions.StartupImportPath.empty() ? std::string{} : InitialOptions.StartupImportPath.string();
            (void)SketcherHost.Initialize(
                &DeviceInterchange,
                CompiledShaderDir,
                RepositoryRootPath / "EngineContent/Icons",
                InitialDoc,
                &SketcherDiag);
            Components::ConfigureImGuiWorkspaceStyle(SketcherDock.ActiveTabMetrics());
            Components::ConfigureImGuiWorkspaceTheme(
                SketcherDock.ActiveTabMetrics(),
                StandaloneControlCentre.QueryThemeRepository());
            SketcherHost.RegisterDockPanes(SketcherDock);
            SketcherHostReady = true;
        }
        return true;
    };

    auto EnsureForceFieldHostReady = [&]() -> bool
    {
        EnsureSharedControlCentreReady();
        if (!ForceFieldHostReady)
        {
            std::string ForceDiag {};
            const std::filesystem::path SwiftShaderDir = RepositoryRootPath / "ExternalPackages/VulkanCompute/SwiftShader";
            if (!ForceSession.AttachInitializedSession(
                    ForceOutliner,
                    ForceViewport,
                    ForceInspector,
                    CompiledShaderDir,
                    SwiftShaderDir,
                    ForceDiag))
            {
                std::fprintf(stderr, "[FrontierHost] VectorFieldViewportSession initialization failed: %s\n", ForceDiag.c_str());
                return false;
            }

            // Populate canonical Wind Tunnel demo forces so standalone ForceField Studio opens with live 3D simulation
            using namespace SimulationSpace::ForceFields;
            const OutlinerSlotToken WindFolder = ForceOutliner.ConstructFolder("Wind Tunnel Forces");

            ForceSourceRecordEntry VortexDesc {};
            VortexDesc.Label                        = "Helical Vortex Path";
            VortexDesc.Model                        = ForceModel::RotationalColumn;
            VortexDesc.CentreMeters                 = { -6.0f, -2.0f, 5.0f };
            VortexDesc.RadiusMeters                 = 20.0f;
            VortexDesc.Strength                     = 22.0f;
            VortexDesc.SwirlRatio                   = 1.15f;
            VortexDesc.DynamicMotion                = true;
            VortexDesc.Motion.Topology              = MotionPathTopology::CatmullSpline;
            VortexDesc.Motion.CycleBehavior         = MotionCycleBehavior::PingPong;
            VortexDesc.Motion.SpeedMetresPerSecond  = 5.5f;
            VortexDesc.Motion.WaypointCount         = 3u;
            VortexDesc.Motion.Waypoints[0]          = { -14.0f, -6.0f, 4.5f };
            VortexDesc.Motion.Waypoints[1]          = { -3.0f, 10.0f, 8.0f };
            VortexDesc.Motion.Waypoints[2]          = { 12.0f, -4.0f, 6.0f };
            const OutlinerSlotToken VortexToken = ForceSession.AddForceSourceEntry(VortexDesc, WindFolder);

            ForceSourceRecordEntry JetDesc {};
            JetDesc.Label         = "Cantilever Jet Plume";
            JetDesc.Model         = ForceModel::FlowStream;
            JetDesc.StreamProfile = FlowStreamProfile::SlipstreamJet;
            JetDesc.CentreMeters  = { 14.0f, 8.0f, 7.0f };
            JetDesc.EulerDegrees  = { 0.0f, -12.0f, -145.0f };
            JetDesc.RadiusMeters  = 17.0f;
            JetDesc.Strength      = 26.0f;
            (void)ForceSession.AddForceSourceEntry(JetDesc, WindFolder);

            ForceSourceRecordEntry BurstDesc {};
            BurstDesc.Label                        = "Orbital Radial Burst";
            BurstDesc.Model                        = ForceModel::RadialBurst;
            BurstDesc.CentreMeters                 = { -12.0f, 15.0f, 6.0f };
            BurstDesc.RadiusMeters                 = 14.0f;
            BurstDesc.Strength                     = 20.0f;
            BurstDesc.DurationMode                 = ForceDurationMode::Modulated;
            BurstDesc.FrequencyHertz               = 0.4f;
            BurstDesc.DynamicMotion                = true;
            BurstDesc.Motion.Topology              = MotionPathTopology::HelicalOrbit;
            BurstDesc.Motion.SpeedMetresPerSecond  = 6.0f;
            BurstDesc.Motion.OrbitRadiusMeters     = 8.0f;
            BurstDesc.Motion.WaypointCount         = 2u;
            BurstDesc.Motion.Waypoints[0]          = { -12.0f, 15.0f, 6.0f };
            BurstDesc.Motion.Waypoints[1]          = { -4.0f, 15.0f, 6.0f };
            (void)ForceSession.AddForceSourceEntry(BurstDesc, WindFolder);

            ForceOutliner.SelectSingle(VortexToken);
            ForceOutliner.ConfigureFilterStripOpen(true);
            ForceSession.ConfigureSelectedWaypointIndex(1);

            ForceViewport.ConfigureExecutionMode(ViewportExecutionMode::Simulate, false);
            auto& Ov = ForceViewport.AccessForceOverlaySettings();
            Ov.Enabled                      = true;
            Ov.ShowHeatmap                  = true;
            Ov.ShowVectors                  = true;
            Ov.ShowStreamlines              = true;
            Ov.ShowBounds                   = true;
            Ov.ShowMotionCurves             = true;
            Ov.CrossSectionAnemometerActive = true;
            Ov.CrossSectionAxis             = CrossSectionPlaneAxis::XYHorizontal;
            Ov.CrossSectionOffsetMeters     = 5.0f;
            Ov.StreamColorMode              = StreamlineColorMode::SpeedRamp;
            Ov.StreamRibbonThickness        = 0.72f;
            Ov.StreamLineCount              = 128u;
            Ov.AnemometerWorldMeters        = { 1.5f, 2.0f, 5.5f };

            ThemeRepository ThemeRepo {};
            (void)ForceFieldDock.Initialize(
                Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
                    "Frontier ForceField", InitialOptions.InitialWidthPx, InitialOptions.InitialHeightPx));
            Components::ConfigureImGuiWorkspaceStyle(ForceFieldDock.ActiveTabMetrics());
            Components::ConfigureImGuiWorkspaceTheme(ForceFieldDock.ActiveTabMetrics(), ThemeRepo);

            (void)ForceFieldDock.RegisterPane(Components::WorkspaceDockPaneDescriptor{
                "Outliner",
                Components::DockPlacementDirection::SplitLeft,
                0.21f,
                Components::PaneDirectiveMask::StandardPaneDirectives,
                [&]() { ForceOutliner.PresentInImGuiPane(LastFrameDtSeconds, LastHostUiScale); } });
            (void)ForceFieldDock.RegisterPane(Components::WorkspaceDockPaneDescriptor{
                "Viewport",
                Components::DockPlacementDirection::CenterTabGroup,
                0.53f,
                Components::PaneDirectiveMask::CentralViewportDirectives,
                [&]() { ForceViewport.PresentInImGuiPane(LastFrameDtSeconds, LastHostUiScale); } });
            (void)ForceFieldDock.RegisterPane(Components::WorkspaceDockPaneDescriptor{
                "Inspector",
                Components::DockPlacementDirection::SplitRight,
                0.26f,
                Components::PaneDirectiveMask::StandardPaneDirectives,
                [&]() { ForceInspector.PresentInImGuiPane(LastFrameDtSeconds, LastHostUiScale); } });

            ForceFieldHostReady = true;
        }
        return true;
    };

    HostProjectBrowser.AssignLaunchWorkspaceCallback(
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
            else
            {
                if (EnsureEditorLifecycleReady(Directive.SelectedFilePath, Directive.ProjectTitle))
                {
                    CurrentSurface = ActiveHostSurface::WorldEditorSurface;
                }
            }
        });

    if (CurrentSurface == ActiveHostSurface::ProjectBrowserSurface)
    {
        EnsureProjectBrowserReady();
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
    else if (CurrentSurface == ActiveHostSurface::MaterialWorkspaceSurface)
    {
        if (!EnsureMaterialHostReady())
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }
    else if (CurrentSurface == ActiveHostSurface::ParticleWorkspaceSurface)
    {
        if (!EnsureParticleHostReady())
        {
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }
    else if (CurrentSurface == ActiveHostSurface::ForceFieldWorkspaceSurface)
    {
        if (!EnsureForceFieldHostReady())
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
    else if (CurrentSurface == ActiveHostSurface::FluidWorkspaceSurface)
    {
        if (!EnsureFluidHostReady())
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
        InputParticipant.BeginTick();
        DesktopWindow.PollOperatingEvents(InputParticipant);
        (void)InputParticipant.TickExchange();
        Input::InputExtension::AssignActiveExtension(&InputParticipant);

        const auto Extent = DesktopWindow.InspectExtent();
        if (Extent.SurfacePixelWidth == 0u || Extent.SurfacePixelHeight == 0u)
        {
            DesktopWindow.WaitOperatingEventsTimeout(0.01);
            continue;
        }
        SurfaceW = Extent.SurfacePixelWidth;
        SurfaceH = Extent.SurfacePixelHeight;
#if FRONTIER_EDITOR_ENABLED
        if (EditorStackReady)
        {
            if (auto* HostSurface = EditorLoop.AccessDockContainer().AccessOperatingSurface(Components::HostWindowToken{ 0u }))
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
        if (MaterialHostReady)
        {
            if (auto* HostSurface = MaterialDock.AccessOperatingSurface(Components::HostWindowToken{ 0u }))
            {
                HostSurface->ResizeSurface(SurfaceW, SurfaceH, Extent.DpiScaleFactor);
            }
        }
        if (ForceFieldHostReady)
        {
            if (auto* HostSurface = ForceFieldDock.AccessOperatingSurface(Components::HostWindowToken{ 0u }))
            {
                HostSurface->ResizeSurface(SurfaceW, SurfaceH, Extent.DpiScaleFactor);
            }
        }
        if (FluidHostReady)
        {
            if (auto* HostSurface = FluidDock.AccessOperatingSurface(Components::HostWindowToken{ 0u }))
            {
                HostSurface->ResizeSurface(SurfaceW, SurfaceH, Extent.DpiScaleFactor);
            }
        }
        if (SketcherHostReady)
        {
            if (auto* HostSurface = SketcherDock.AccessOperatingSurface(Components::HostWindowToken{ 0u }))
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
        if (CurrentSurface == ActiveHostSurface::ProjectBrowserSurface)
        {
            for (const std::string& DropFile : DroppedPaths)
            {
                (void)HostProjectBrowser.RegisterDiskPathInCollection(std::filesystem::path(DropFile), true);
            }
        }
        else if (CurrentSurface == ActiveHostSurface::BakingWorkspaceSurface && BakingHostReady)
        {
            for (const std::string& DropFile : DroppedPaths)
            {
                std::string DropDiag {};
                const bool HighTarget = Io.KeyShift;
                (void)BakingHost.IntakeExternalFileDrop(DropFile, HighTarget, DropDiag);
            }
        }
        else if (CurrentSurface == ActiveHostSurface::UvWorkspaceSurface && UvHostReady)
        {
            for (const std::string& DropFile : DroppedPaths)
            {
                std::string DropDiag {};
                (void)UVHost.IntakeExternalFileDrop(DropFile, DropDiag);
            }
        }
        else if (CurrentSurface == ActiveHostSurface::PatternWorkspaceSurface && PatternHostReady)
        {
            for (const std::string& DropFile : DroppedPaths)
            {
                std::string DropDiag {};
                (void)PatternHost.IntakeExternalFileDrop(DropFile, DropDiag);
            }
        }
        else if (CurrentSurface == ActiveHostSurface::StampWorkspaceSurface && StampHostReady)
        {
            for (const std::string& DropFile : DroppedPaths)
            {
                std::string DropDiag {};
                (void)StampHost.IntakeExternalFileDrop(DropFile, DropDiag);
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
#endif

        Io.DisplaySize = ImVec2(static_cast<float>(SurfaceW), static_cast<float>(SurfaceH));
        Io.DeltaTime   = static_cast<float>(Dt);

        SwapchainPresenter.PrepareImGuiFrame();
        ImGui::NewFrame();
        const Rectangle2D FullBounds { 0.0f, 0.0f, static_cast<float>(SurfaceW), static_cast<float>(SurfaceH) };
#if FRONTIER_EDITOR_ENABLED
        if (CurrentSurface == ActiveHostSurface::ProjectBrowserSurface)
        {
            HostProjectBrowser.Render(FullBounds, Dt, 1.0f);
        }
        else if (CurrentSurface == ActiveHostSurface::WorldEditorSurface)
        {
            (void)EditorLoop.ExecuteExternalDesktopTick(Dt);
            Input::InputExtension::AssignActiveExtension(&InputParticipant);
            EditorLoop.PresentPrimaryEditorImGui(Dt);
        }
        else if (CurrentSurface == ActiveHostSurface::ParticleWorkspaceSurface)
        {
            const float HostUiScale = std::clamp(StandaloneControlCentre.QueryInterfaceScaleFactor(), 0.75f, 2.0f);
            ControlAppearance::ConfigureTheme(StandaloneControlCentre.QueryThemeRepository());
            ParticleHost.AccessSession().StepSimulationClock(Dt);
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
            ParticleHost.RenderStudio(StudioSurface, LogicalBounds, PtrInput, Dt);
            StandaloneControlCentre.Render(FullBounds, Dt, static_cast<float>(1.0 / Dt));
        }
        else if (CurrentSurface == ActiveHostSurface::FractureWorkspaceSurface)
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
            FractureHost.RenderStudio(StudioSurface, LogicalBounds, PtrInput, Dt);
            StandaloneControlCentre.Render(FullBounds, Dt, static_cast<float>(1.0 / Dt));
        }
        else if (CurrentSurface == ActiveHostSurface::FluidWorkspaceSurface)
        {
            const float HostUiScale = std::clamp(StandaloneControlCentre.QueryInterfaceScaleFactor(), 0.75f, 2.0f);
            ControlAppearance::ConfigureTheme(StandaloneControlCentre.QueryThemeRepository());
            FluidHost.SynchronizeHostScale(HostUiScale);
            FluidHost.AccessSession().StepSimulation(Dt);

            Components::ConfigureImGuiWorkspaceTheme(
                FluidDock.ActiveTabMetrics(),
                StandaloneControlCentre.QueryThemeRepository());
            auto& FluidBrowserPanel = FluidBrowser.AccessPanel();
            FluidBrowserPanel.SynchronizeHostBounds(FullBounds, HostUiScale);
            StandaloneControlCentre.SynchronizeHostBounds(FullBounds, HostUiScale);
            if (StandaloneControlCentre.QueryFullScreenBlocking() || FluidBrowserPanel.QueryShelfDragging())
            {
                WorkspaceAppearance::AssignOverlayPointerShield(FullBounds);
            }
            else
            {
                WorkspaceAppearance::AssignOverlayPointerShield(
                    FluidBrowserPanel.QueryDrawerPhysicalBounds(),
                    FluidBrowserPanel.QueryNotchPhysicalBounds());
                WorkspaceAppearance::AppendOverlayPointerShield(StandaloneControlCentre.QueryShadePhysicalBounds());
                WorkspaceAppearance::AppendOverlayPointerShield(StandaloneControlCentre.QueryTabPhysicalBounds());
            }
            FluidDock.PresentWindowImGuiPanes(Components::HostWindowToken{ 0u });
            WorkspaceAppearance::ClearOverlayPointerShield();
            FluidBrowser.Render(FullBounds, Dt, HostUiScale);
            StandaloneControlCentre.Render(FullBounds, Dt, static_cast<float>(1.0 / Dt));
        }
        else if (CurrentSurface == ActiveHostSurface::SketcherWorkspaceSurface)
        {
            const float HostUiScale = std::clamp(StandaloneControlCentre.QueryInterfaceScaleFactor(), 0.75f, 2.0f);
            ControlAppearance::ConfigureTheme(StandaloneControlCentre.QueryThemeRepository());
            SketcherHost.SynchronizeHostScale(HostUiScale);

            Components::ConfigureImGuiWorkspaceTheme(
                SketcherDock.ActiveTabMetrics(),
                StandaloneControlCentre.QueryThemeRepository());
            auto& SketcherBrowserPanel = SketcherBrowser.AccessPanel();
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
            SketcherHost.BeginFrame(Dt);
            SketcherDock.PresentWindowImGuiPanes(Components::HostWindowToken{ 0u });
            SketcherHost.EndFrame(Dt, false);
            WorkspaceAppearance::ClearOverlayPointerShield();
            SketcherBrowser.Render(FullBounds, Dt, HostUiScale);
            StandaloneControlCentre.Render(FullBounds, Dt, static_cast<float>(1.0 / Dt));
        }
        else if (CurrentSurface == ActiveHostSurface::ForceFieldWorkspaceSurface)
        {
            const float HostUiScale = std::clamp(StandaloneControlCentre.QueryInterfaceScaleFactor(), 0.75f, 2.0f);
            LastFrameDtSeconds = Dt;
            LastHostUiScale    = HostUiScale;
            ForceSession.StepSimulation(Dt);
            Components::ConfigureImGuiWorkspaceTheme(
                ForceFieldDock.ActiveTabMetrics(),
                StandaloneControlCentre.QueryThemeRepository());
            StandaloneControlCentre.SynchronizeHostBounds(FullBounds, HostUiScale);
            if (StandaloneControlCentre.QueryFullScreenBlocking())
            {
                WorkspaceAppearance::AssignOverlayPointerShield(FullBounds);
            }
            else
            {
                WorkspaceAppearance::AssignOverlayPointerShield(
                    StandaloneControlCentre.QueryShadePhysicalBounds(),
                    StandaloneControlCentre.QueryTabPhysicalBounds());
            }
            ForceFieldDock.PresentWindowImGuiPanes(Components::HostWindowToken{ 0u });
            WorkspaceAppearance::ClearOverlayPointerShield();
            ForceSession.AccessContentBrowser().PresentInImGuiHost(Dt, HostUiScale);
            StandaloneControlCentre.Render(FullBounds, Dt, static_cast<float>(1.0 / Dt));
        }
        else
        {
            Components::HeadlessWorkspaceDockContainer& ActiveDock =
                (CurrentSurface == ActiveHostSurface::MaterialWorkspaceSurface) ? MaterialDock :
                ((CurrentSurface == ActiveHostSurface::BakingWorkspaceSurface) ? BakingDock :
                ((CurrentSurface == ActiveHostSurface::PatternWorkspaceSurface) ? PatternDock :
                ((CurrentSurface == ActiveHostSurface::StampWorkspaceSurface) ? StampDock : StandaloneDock)));
            Editor::Explorer::ContentBrowser& ActiveBrowser =
                (CurrentSurface == ActiveHostSurface::MaterialWorkspaceSurface) ? MaterialBrowser :
                ((CurrentSurface == ActiveHostSurface::BakingWorkspaceSurface) ? BakingBrowser :
                ((CurrentSurface == ActiveHostSurface::PatternWorkspaceSurface) ? PatternBrowser :
                ((CurrentSurface == ActiveHostSurface::StampWorkspaceSurface) ? StampBrowser : StandaloneBrowser)));

            const float HostUiScale = std::clamp(StandaloneControlCentre.QueryInterfaceScaleFactor(), 0.75f, 2.0f);
            Components::ConfigureImGuiWorkspaceTheme(
                ActiveDock.ActiveTabMetrics(),
                StandaloneControlCentre.QueryThemeRepository());
            auto& ActiveBrowserPanel = ActiveBrowser.AccessPanel();
            ActiveBrowserPanel.SynchronizeHostBounds(FullBounds, HostUiScale);
            StandaloneControlCentre.SynchronizeHostBounds(FullBounds, HostUiScale);
            if (CurrentSurface == ActiveHostSurface::MaterialWorkspaceSurface)
            {
                MaterialHost.SynchronizeContentBrowserOcclusion(HostUiScale);
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
                float PillX = std::max(320.0f, LogicalW * 0.5f + 46.0f);

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

                if (CapsuleButton::Construct(TopOverlay, { PillX, 3.0f, 76.0f, 23.0f }, ButtonDescriptor{ "Projects", ButtonTone::Secondary, 10.2f, 11.5f }, Ptr).Pressed)
                {
                    EnsureProjectBrowserReady();
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
    HostProjectBrowser.ReleaseGpuTextures();
    HostProjectBrowser.AttachVulkanWorldRenderer(nullptr, {});
    if (EditorStackReady)
    {
        EditorLoop.Finalize();
    }
    if (UvHostReady)
    {
        StandaloneDock.Finalize();
        UVHost.AccessSession().TeardownGPUSolvers();
    }
    if (PatternHostReady)
    {
        PatternDock.Finalize();
    }
    if (StampHostReady)
    {
        StampDock.Finalize();
    }
    if (BakingHostReady)
    {
        BakingDock.Finalize();
        BakingHost.AttachVulkanDevice(nullptr);
    }
    if (MaterialHostReady)
    {
        MaterialDock.Finalize();
        MaterialHost.AccessPreviewRenderer().TeardownVulkanPreview();
    }
    if (ForceFieldHostReady)
    {
        ForceFieldDock.Finalize();
    }
    if (FluidHostReady)
    {
        FluidDock.Finalize();
    }
    if (SketcherHostReady)
    {
        SketcherHost.Finalize();
        SketcherDock.Finalize();
    }
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
    return 0;
}

} // namespace Frontier::Lifecycle
