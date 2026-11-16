/*============================================================================================================================================
                                                          PROJECTEXECUTIONRUNNER.CPP
============================================================================================================================================*/
// 🧩 Implementation of FrontierRunShippingProjectLoop (Engine-only, zero Editor/UI) and Development/Editor project launch runners

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Lifecycle/ProjectExecutionRunner.h"

#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"

#include <algorithm>
#include <filesystem>
#include <string>

#if defined(FRONTIER_COMPILE_WORKSPACE_UI_RUNNER)
#include "Engine/Lifecycle/DesktopWorkspaceLaunch.h"
#include "Engine/Lifecycle/EditorLifecycle.h"

namespace
{

[[nodiscard]] std::filesystem::path ResolveSafePath(const char* RawPath)
{
    if (RawPath == nullptr || RawPath[0] == '\0')
    {
        return {};
    }
    return std::filesystem::path(RawPath);
}

} // namespace
#endif

extern "C"
{

#if !defined(FRONTIER_COMPILE_WORKSPACE_UI_RUNNER)

int FrontierRunShippingProjectLoop(const FrontierProjectLaunchDescriptor* Descriptor)
{
    if (Descriptor == nullptr || Descriptor->StructureVersion == 0u)
    {
        return 2;
    }

    // Shipping runtime executes strictly against FrontierEngine (TieredWorldRenderSequence + simulation tick) with zero Editor/UI code
    Frontier::GeometricRaster::WorldSceneSpecification   ShippingScene {};
    Frontier::GeometricRaster::TieredWorldRenderSequence WorldRenderer {};
    Frontier::GeometricRaster::PerspectiveCameraFrame    ShippingCam   {};
    ShippingCam.ViewportWidthPx  = std::clamp(Descriptor->InitialWidthPx / 4u, 160u, 640u);
    ShippingCam.ViewportHeightPx = std::clamp(Descriptor->InitialHeightPx / 4u, 90u, 360u);
    WorldRenderer.PrepareWorldScene(ShippingScene);

    const uint32_t TotalTicks = Descriptor->BoundedTickCount > 0u ? Descriptor->BoundedTickCount : 4u;
    constexpr double FixedDt  = 1.0 / 60.0;

    for (uint32_t TickIdx = 0u; TickIdx < TotalTicks; ++TickIdx)
    {
        if (Descriptor->NotifySimulationStep != nullptr)
        {
            Descriptor->NotifySimulationStep(FixedDt, Descriptor->UserPayload);
        }
        if (TickIdx == 0u)
        {
            const auto Frame = WorldRenderer.RenderWorldFrame(
                Frontier::GeometricRaster::WorldRenderTier::Tier1GtxCalibrated,
                ShippingCam,
                ShippingCam,
                ShippingScene);
            if (Frame.DisplayRgba8.empty())
            {
                return 3;
            }
        }
    }
    return 0;
}

#else

int FrontierRunDevelopmentProjectLoop(const FrontierProjectLaunchDescriptor* Descriptor)
{
    return FrontierRunEditorProjectLoop(Descriptor);
}

int FrontierRunEditorProjectLoop(const FrontierProjectLaunchDescriptor* Descriptor)
{
    if (Descriptor == nullptr || Descriptor->StructureVersion == 0u)
    {
        return 2;
    }

    const std::string Title = (Descriptor->ProjectTitle != nullptr && Descriptor->ProjectTitle[0] != '\0')
                                  ? std::string(Descriptor->ProjectTitle)
                                  : std::string("FrontierProject");

    if (Descriptor->BoundedTickCount > 0u)
    {
#if FRONTIER_EDITOR_ENABLED
        Frontier::Lifecycle::EditorLifecycle Session {};
        Frontier::Lifecycle::EditorStartupConfiguration Config {};
        Config.WindowDescriptor = Frontier::Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow(
            Title,
            std::max(640u, Descriptor->InitialWidthPx),
            std::max(400u, Descriptor->InitialHeightPx));
        Config.ProjectSpacePath      = ResolveSafePath(Descriptor->ProjectSpacePath);
        Config.PopulateShowcaseScene = true;
        if (!Session.Initialize(Config))
        {
            return 3;
        }
        Session.ConfigureActiveProjectTitle(Title);
        constexpr double StepDt = 1.0 / 60.0;
        for (uint32_t TickIdx = 0u; TickIdx < Descriptor->BoundedTickCount; ++TickIdx)
        {
            if (Descriptor->NotifySimulationStep != nullptr)
            {
                Descriptor->NotifySimulationStep(StepDt, Descriptor->UserPayload);
            }
            if (Descriptor->NotifyUserInterfaceStep != nullptr)
            {
                Descriptor->NotifyUserInterfaceStep(StepDt, Descriptor->UserPayload);
            }
            if (!Session.ExecuteAuthoringTick(StepDt))
            {
                break;
            }
        }
        Session.Finalize();
        return 0;
#else
        return 0;
#endif
    }

    const std::string WindowTitleStr = Title + " — Frontier";
    Frontier::Lifecycle::DesktopLaunchOptions Options {};
    Options.DefaultLaunchMode = (Descriptor->PreferUvWorkspace != 0u)
                                    ? Frontier::Configuration::HostLaunchMode::StandaloneWorkspace
                                    : Frontier::Configuration::HostLaunchMode::FeatureSet;
    Options.WindowTitle             = WindowTitleStr;
    Options.InitialWidthPx          = std::max(960u, Descriptor->InitialWidthPx);
    Options.InitialHeightPx         = std::max(600u, Descriptor->InitialHeightPx);
    Options.StartupImportPath       = ResolveSafePath(Descriptor->StartupUvPath);
    Options.StartupProjectSpacePath = ResolveSafePath(Descriptor->ProjectSpacePath);
    Options.OpenWorldEditor         = (Descriptor->PreferUvWorkspace == 0u);
    Options.OpenProjectBrowser      = false;

    const auto Paths = Frontier::Lifecycle::ResolveDesktopResourcePaths(
        std::filesystem::current_path(),
        std::filesystem::current_path(),
        std::filesystem::current_path() / "Binaries" / "Win64" / "CompiledShaders");
    return Frontier::Lifecycle::LaunchDesktopWorkspace(
        Options,
        {},
        Paths.RepositoryRootPath,
        Paths.CompiledShaderDirectory);
}

#endif

} // extern "C"
