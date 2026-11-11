/*============================================================================================================================================
                                                     WINDOWVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation suite and CPU visual snapshot generator for splash, desktop application, and dynamic game fullscreen PlatformWindow modes

#include "Engine/Windowing/PlatformWindow.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <cstdio>

namespace
{

using Frontier::Components::RgbaColor8;
using Frontier::Exhibits::LoadProjectZeroDmSansFont;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;
using Frontier::Input::ButtonTransitionCategory;
using Frontier::Input::CursorConfinementMode;
using Frontier::Input::HapticFeedbackDirective;
using Frontier::Input::InputExtension;
using Frontier::Input::InputSignalCategory;
using Frontier::Input::InputSignalPacket;
using Frontier::Windowing::HeadlessPlatformWindow;
using Frontier::Windowing::PlatformWindowSurface;
using Frontier::Windowing::QueryAttributeEnabled;
using Frontier::Windowing::WindowAttributeMask;
using Frontier::Windowing::WindowConfigurationDescriptor;
using Frontier::Windowing::WindowPresentationMode;

bool VerifyAssertion(bool Condition, const char* Description) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[WindowValidation] FAILED: %s\n", Description);
        return false;
    }
    return true;
}

template <PlatformWindowSurface SurfaceType>
bool ValidateSplashPreset(const char* SurfaceName) noexcept
{
    SurfaceType SplashWindow{};
    const auto  SplashDesc = WindowConfigurationDescriptor::ConstructSplashWindow("Frontier — Initializing", 720, 420);

    if (!VerifyAssertion(SplashWindow.Initialize(SplashDesc), SurfaceName))
    {
        return false;
    }

    const auto Extent     = SplashWindow.InspectExtent();
    const auto Attributes = SplashWindow.ActiveAttributeMask();

    const bool Passed =
        VerifyAssertion(SplashWindow.ActivePresentationMode() == WindowPresentationMode::SplashBorderless, "Splash mode active") &&
        VerifyAssertion(!QueryAttributeEnabled(Attributes, WindowAttributeMask::EnableBorderDecoration), "Splash disables border chrome") &&
        VerifyAssertion(!QueryAttributeEnabled(Attributes, WindowAttributeMask::EnableInteractiveResize), "Splash disables interactive resize") &&
        VerifyAssertion(QueryAttributeEnabled(Attributes, WindowAttributeMask::EnableTopmostOrder), "Splash enables topmost order") &&
        VerifyAssertion(QueryAttributeEnabled(Attributes, WindowAttributeMask::AssignMonitorCenter), "Splash assigns monitor center") &&
        VerifyAssertion(Extent.PixelWidth == 720 && Extent.PixelHeight == 420, "Splash exact dimensions");

    SplashWindow.Finalize();
    return Passed;
}

template <PlatformWindowSurface SurfaceType>
bool ValidateDesktopApplicationPreset(const char* SurfaceName) noexcept
{
    SurfaceType AppWindow{};
    const auto  AppDesc = WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier Editor", 1600, 900, false);

    if (!VerifyAssertion(AppWindow.Initialize(AppDesc), SurfaceName))
    {
        return false;
    }

    const auto InitialExtent = AppWindow.InspectExtent();
    const auto InitialAttrs  = AppWindow.ActiveAttributeMask();

    bool Passed =
        VerifyAssertion(AppWindow.ActivePresentationMode() == WindowPresentationMode::Windowed, "Application starts Windowed") &&
        VerifyAssertion(QueryAttributeEnabled(InitialAttrs, WindowAttributeMask::EnableBorderDecoration), "Application enables border chrome") &&
        VerifyAssertion(QueryAttributeEnabled(InitialAttrs, WindowAttributeMask::EnableInteractiveResize), "Application enables interactive resize") &&
        VerifyAssertion(QueryAttributeEnabled(InitialAttrs, WindowAttributeMask::EnableExternalDrop), "Application enables external drop") &&
        VerifyAssertion(InitialExtent.PixelWidth == 1600 && InitialExtent.PixelHeight == 900, "Application 1600x900");

    Passed = Passed &&
        VerifyAssertion(AppWindow.ApplyPresentationMode(WindowPresentationMode::MaximizedWindowed), "Maximize application window") &&
        VerifyAssertion(AppWindow.ActivePresentationMode() == WindowPresentationMode::MaximizedWindowed, "Maximized mode recorded") &&
        VerifyAssertion(AppWindow.ApplyPresentationMode(WindowPresentationMode::Windowed), "Restore application window") &&
        VerifyAssertion(AppWindow.InspectExtent().PixelWidth == 1600 && AppWindow.InspectExtent().PixelHeight == 900,
                        "Restored bounds match pre-maximized 1600x900");

    AppWindow.Finalize();
    return Passed;
}

template <PlatformWindowSurface SurfaceType>
bool ValidateGameDynamicModeTransitions(const char* SurfaceName) noexcept
{
    SurfaceType    GameWindow{};
    InputExtension InputCoordinator{};
    InputCoordinator.Initialize();

    const auto GameDesc = WindowConfigurationDescriptor::ConstructGameWindow(
        "Frontier Runtime",
        1280,
        720,
        WindowPresentationMode::Windowed,
        0);

    if (!VerifyAssertion(GameWindow.Initialize(GameDesc), SurfaceName))
    {
        InputCoordinator.Finalize();
        return false;
    }

    GameWindow.MoveSurface(220, 140);
    const auto SavedBeforeFullscreen = GameWindow.InspectExtent();

    bool Passed =
        VerifyAssertion(SavedBeforeFullscreen.PixelWidth == 1280 && SavedBeforeFullscreen.PixelHeight == 720,
                        "Game initial 1280x720 windowed") &&
        VerifyAssertion(GameWindow.ApplyPresentationMode(WindowPresentationMode::BorderlessFullscreen),
                        "Transition Windowed -> BorderlessFullscreen") &&
        VerifyAssertion(!QueryAttributeEnabled(GameWindow.ActiveAttributeMask(), WindowAttributeMask::EnableBorderDecoration),
                        "BorderlessFullscreen strips border decoration") &&
        VerifyAssertion(!QueryAttributeEnabled(GameWindow.ActiveAttributeMask(), WindowAttributeMask::EnableInteractiveResize),
                        "BorderlessFullscreen strips interactive resize") &&
        VerifyAssertion(GameWindow.ApplyPresentationMode(WindowPresentationMode::ExclusiveFullscreen, 1920, 1080, 144),
                        "Transition BorderlessFullscreen -> ExclusiveFullscreen 1920x1080@144Hz") &&
        VerifyAssertion(GameWindow.ActivePresentationMode() == WindowPresentationMode::ExclusiveFullscreen,
                        "ExclusiveFullscreen active") &&
        VerifyAssertion(GameWindow.InspectExtent().RefreshRateHz == 144,
                        "ExclusiveFullscreen 144Hz refresh rate applied") &&
        VerifyAssertion(GameWindow.ApplyPresentationMode(WindowPresentationMode::Windowed),
                        "Transition ExclusiveFullscreen -> Windowed") &&
        VerifyAssertion(QueryAttributeEnabled(GameWindow.ActiveAttributeMask(), WindowAttributeMask::EnableBorderDecoration),
                        "Windowed restores border decoration") &&
        VerifyAssertion(QueryAttributeEnabled(GameWindow.ActiveAttributeMask(), WindowAttributeMask::EnableInteractiveResize),
                        "Windowed restores interactive resize") &&
        VerifyAssertion(GameWindow.InspectExtent().PixelWidth == 1280 && GameWindow.InspectExtent().PixelHeight == 720,
                        "Windowed restores exact 1280x720 client dimensions");

    InputCoordinator.RequestCursorConfinement(CursorConfinementMode::LockedRelativeHidden);
    (void)InputCoordinator.RequestHapticFeedback(HapticFeedbackDirective{
        .DeviceIdentifier       = 0,
        .LowFrequencyAmplitude  = 0.7f,
        .HighFrequencyAmplitude = 0.4f,
        .DurationMilliseconds   = 20u,
    });
    GameWindow.PollOperatingEvents(InputCoordinator);

    Passed = Passed &&
        VerifyAssertion(GameWindow.AppliedCursorMode() == CursorConfinementMode::LockedRelativeHidden,
                        "Cursor lock propagated to OS window") &&
        VerifyAssertion(GameWindow.AppliedHapticPulseCount() == 1,
                        "Haptic directive propagated through window poll");

    GameWindow.Finalize();
    InputCoordinator.Finalize();
    return Passed;
}

bool GenerateWindowModesVisualSnapshot()
{
    HeadlessPlatformWindow SampleWindow{};
    (void)SampleWindow.Initialize(WindowConfigurationDescriptor::ConstructSplashWindow("Frontier", 720, 420));
    const auto SplashExt = SampleWindow.InspectExtent();
    SampleWindow.Finalize();

    (void)SampleWindow.Initialize(WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier Editor", 1600, 900));
    const auto AppExt = SampleWindow.InspectExtent();
    (void)SampleWindow.ApplyPresentationMode(WindowPresentationMode::MaximizedWindowed);
    const auto MaxExt = SampleWindow.InspectExtent();
    (void)SampleWindow.ApplyPresentationMode(WindowPresentationMode::BorderlessFullscreen);
    const auto BorderlessExt = SampleWindow.InspectExtent();
    (void)SampleWindow.ApplyPresentationMode(WindowPresentationMode::ExclusiveFullscreen, 1920, 1080, 144);
    const auto ExclusiveExt = SampleWindow.InspectExtent();
    SampleWindow.Finalize();

    ImGuiContext* SnapshotCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(SnapshotCtx);
    ImGuiIO& IoInstance      = ImGui::GetIO();
    IoInstance.IniFilename   = nullptr;
    IoInstance.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    IoInstance.DisplaySize   = ImVec2(1440.0f, 820.0f);
    (void)LoadProjectZeroDmSansFont(IoInstance, 13.0f);
    Frontier::Components::ConfigureImGuiWorkspaceStyle(Frontier::Components::ConstructWorkspaceTabMetrics(1.0f));

    ImGui::NewFrame();
    ImDrawList* CanvasBuffer = ImGui::GetForegroundDrawList();
    CanvasBuffer->AddText(
        ImVec2(36.0f, 24.0f),
        IM_COL32(238, 238, 244, 255),
        "PlatformWindow Multi-Mode Surface Verification  —  Simulated 2560x1440 Display Geometry & Directive Masks");

    struct ModeTileSpec
    {
        const char*                       Heading;
        const char*                       DirectiveSummary;
        Frontier::Windowing::WindowExtentReading Extent;
        ImU32                             AccentColor;
    };

    const ModeTileSpec Tiles[5] = {
        { "1. SplashBorderless (ConstructSplashWindow)",
          "AssignMonitorCenter | EnableTopmostOrder | Borderless",
          SplashExt,
          IM_COL32(245, 158, 11, 255) },
        { "2. Windowed (ConstructApplicationWindow)",
          "EnableBorderDecoration | EnableInteractiveResize | EnableExternalDrop",
          AppExt,
          IM_COL32(34, 197, 94, 255) },
        { "3. MaximizedWindowed",
          "Full Monitor Work-Area (2560x1400) | Restores 1600x900 on Exit",
          MaxExt,
          IM_COL32(59, 130, 246, 255) },
        { "4. BorderlessFullscreen",
          "Full Monitor Span (2560x1440) | Chrome & Resize Automatically Masked",
          BorderlessExt,
          IM_COL32(168, 85, 247, 255) },
        { "5. ExclusiveFullscreen (ConstructGameWindow @ 144 Hz)",
          "1920x1080 Dedicated Video Mode @ 144 Hz | EnableFocusMinimize",
          ExclusiveExt,
          IM_COL32(236, 72, 153, 255) },
    };

    for (int Index = 0; Index < 5; ++Index)
    {
        const int   Col   = Index % 3;
        const int   Row   = Index / 3;
        const float TileX = 36.0f + static_cast<float>(Col) * 462.0f;
        const float TileY = 72.0f + static_cast<float>(Row) * 356.0f;
        const float TileW = 438.0f;
        const float TileH = 330.0f;

        CanvasBuffer->AddRectFilled(ImVec2(TileX, TileY), ImVec2(TileX + TileW, TileY + TileH), IM_COL32(20, 20, 24, 255), 6.0f);
        CanvasBuffer->AddRect(ImVec2(TileX, TileY), ImVec2(TileX + TileW, TileY + TileH), IM_COL32(44, 44, 52, 255), 6.0f);
        CanvasBuffer->AddText(ImVec2(TileX + 14.0f, TileY + 12.0f), Tiles[Index].AccentColor, Tiles[Index].Heading);
        CanvasBuffer->AddText(ImVec2(TileX + 14.0f, TileY + 34.0f), IM_COL32(150, 150, 160, 255), Tiles[Index].DirectiveSummary);

        // Scaled 2560x1440 monitor diagram inside tile
        const float MonX     = TileX + 22.0f;
        const float MonY     = TileY + 66.0f;
        const float MonW     = 394.0f;
        const float MonH     = 221.6f;
        const float ScaleX   = MonW / 2560.0f;
        const float ScaleY   = MonH / 1440.0f;
        const auto& Ext      = Tiles[Index].Extent;

        CanvasBuffer->AddRectFilled(ImVec2(MonX, MonY), ImVec2(MonX + MonW, MonY + MonH), IM_COL32(11, 13, 18, 255), 3.0f);
        CanvasBuffer->AddRect(ImVec2(MonX, MonY), ImVec2(MonX + MonW, MonY + MonH), IM_COL32(62, 66, 78, 255), 3.0f);

        const float WinX = MonX + static_cast<float>(Ext.OriginX) * ScaleX;
        const float WinY = MonY + static_cast<float>(Ext.OriginY) * ScaleY;
        const float WinW = static_cast<float>(Ext.PixelWidth) * ScaleX;
        const float WinH = static_cast<float>(Ext.PixelHeight) * ScaleY;

        CanvasBuffer->AddRectFilled(ImVec2(WinX, WinY), ImVec2(WinX + WinW, WinY + WinH), IM_COL32(32, 42, 58, 220), 2.0f);
        CanvasBuffer->AddRect(ImVec2(WinX, WinY), ImVec2(WinX + WinW, WinY + WinH), Tiles[Index].AccentColor, 2.0f, 0, 2.0f);

        char MetricsLine[128]{};
        std::snprintf(
            MetricsLine,
            sizeof(MetricsLine),
            "Origin (%d, %d)  |  Extent %ux%u px  |  Refresh %u Hz",
            Ext.OriginX,
            Ext.OriginY,
            Ext.PixelWidth,
            Ext.PixelHeight,
            Ext.RefreshRateHz);
        CanvasBuffer->AddText(ImVec2(TileX + 14.0f, TileY + 298.0f), IM_COL32(215, 215, 224, 255), MetricsLine);
    }

    ImGui::Render();
    SoftwareRasterCanvas OutputCanvas(1440, 820, RgbaColor8{ 12, 12, 14, 255 });
    OutputCanvas.RasterizeImGuiBatches(ImGui::GetDrawData(), 0, 0);
    ImGui::DestroyContext(SnapshotCtx);

    const auto OutputPath = ResolveRepositoryRootPath() / "Exhibits/Images/Windowing/WindowPresentationModes.png";
    return VerifyAssertion(OutputCanvas.EncodePngToFile(OutputPath), "Write WindowPresentationModes.png");
}

} // namespace

int main()
{
    if (!ValidateSplashPreset<HeadlessPlatformWindow>("HeadlessSplash") ||
        !ValidateDesktopApplicationPreset<HeadlessPlatformWindow>("HeadlessApplication") ||
        !ValidateGameDynamicModeTransitions<HeadlessPlatformWindow>("HeadlessGame"))
    {
        return 1;
    }

    HeadlessPlatformWindow SyntheticWindow{};
    InputExtension         Coordinator{};
    Coordinator.Initialize();
    if (!VerifyAssertion(
            SyntheticWindow.Initialize(WindowConfigurationDescriptor::ConstructApplicationWindow("SyntheticCheck", 1280, 720)),
            "Initialize synthetic headless window"))
    {
        return 1;
    }
    InputSignalPacket KeyPacket{};
    KeyPacket.TimestampNanoseconds = 1000ULL;
    KeyPacket.PrimaryAxis          = 1.0f;
    KeyPacket.DeviceIdentifier     = 0;
    KeyPacket.ControlCode          = 42;
    KeyPacket.Category             = InputSignalCategory::KeyboardKey;
    KeyPacket.Transition           = ButtonTransitionCategory::Pressed;
    (void)SyntheticWindow.QueueSyntheticSignal(KeyPacket);

    SyntheticWindow.PollOperatingEvents(Coordinator);
    (void)Coordinator.TickExchange();
    if (!VerifyAssertion(Coordinator.QueryScanCodeHeld(42), "Synthetic OS key signal forwarded to InputExtension"))
    {
        return 1;
    }
    SyntheticWindow.Finalize();
    Coordinator.Finalize();

#if !defined(__ANDROID__)
    if (!ValidateSplashPreset<Frontier::Windowing::DesktopPlatformWindow>("DesktopGlfwSplash") ||
        !ValidateDesktopApplicationPreset<Frontier::Windowing::DesktopPlatformWindow>("DesktopGlfwApplication") ||
        !ValidateGameDynamicModeTransitions<Frontier::Windowing::DesktopPlatformWindow>("DesktopGlfwGame"))
    {
        return 1;
    }
#endif

    if (!GenerateWindowModesVisualSnapshot())
    {
        return 1;
    }

    std::printf("[WindowValidation] All splash, application, and dynamic game window modes + visual snapshot PNG verified.\n");
    return 0;
}
