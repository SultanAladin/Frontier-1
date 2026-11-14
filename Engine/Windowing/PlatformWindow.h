/*============================================================================================================================================
                                                               PLATFORMWINDOW.H
============================================================================================================================================*/
// 🧩 Cross-platform window and surface interface with directive masks, application presets, and dynamic fullscreen mode transitions

#pragma once

#include "Engine/Input/InputExtension.h"

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Windowing
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class WindowPlatformCategory : uint8_t
{
    HeadlessStub = 0, // [-] - Deterministic offscreen validation surface
    Win32        = 1, // [-] - Windows HWND / DXGI / Vulkan surface
    X11          = 2, // [-] - Linux Xlib / XCB surface
    Wayland      = 3, // [-] - Linux Wayland XDG surface
    Android      = 4, // [-] - Android NDK ANativeWindow surface
    GlfwNull     = 5, // [-] - GLFW 3.4 offscreen desktop windowing interface
};

enum class WindowPresentationMode : uint8_t
{
    SplashBorderless     = 0, // [-] - Undecorated centered startup splash surface (non-resizable)
    Windowed             = 1, // [-] - Decorated movable/resizable desktop application surface
    MaximizedWindowed    = 2, // [-] - Decorated desktop surface occupying full monitor work-area
    BorderlessFullscreen = 3, // [-] - Undecorated surface spanning full monitor bounds without video-mode switch
    ExclusiveFullscreen  = 4, // [-] - Dedicated hardware video-mode fullscreen surface for game applications
};

enum class WindowAttributeMask : uint32_t
{
    EmptyAttributes         = 0u,       // [mask] - All optional window directives cleared
    EnableBorderDecoration  = 1u << 0,  // [mask] - Enable OS title bar, border chrome, and caption buttons (0 = borderless)
    EnableInteractiveResize = 1u << 1,  // [mask] - Enable interactive edge and corner resize grips
    AssignMonitorCenter     = 1u << 2,  // [mask] - Assign initial placement to target monitor work-area center
    EnableTopmostOrder      = 1u << 3,  // [mask] - Enable topmost Z-order above standard desktop windows
    EnableFocusMinimize     = 1u << 4,  // [mask] - Enable automatic minimize of exclusive fullscreen window upon focus loss
    EnableDpiScaling        = 1u << 5,  // [mask] - Enable per-monitor physical pixel DPI scaling
    EnableAlphaCompositing  = 1u << 6,  // [mask] - Enable alpha-composited client background (shaped splash surfaces)
    EnableExternalDrop      = 1u << 7,  // [mask] - Enable OS file drag-and-drop path intake for workspaces
    EnableInitialVisibility = 1u << 8,  // [mask] - Enable immediate surface visibility upon construction
    AssignInitialFocus      = 1u << 9,  // [mask] - Assign keyboard focus immediately upon visibility

    SplashPresetMask      = AssignMonitorCenter | EnableTopmostOrder | EnableDpiScaling | EnableInitialVisibility,
    ApplicationPresetMask = EnableBorderDecoration | EnableInteractiveResize | AssignMonitorCenter |
                            EnableDpiScaling | EnableExternalDrop | EnableInitialVisibility | AssignInitialFocus,
    GamePresetMask        = EnableBorderDecoration | EnableInteractiveResize | AssignMonitorCenter |
                            EnableFocusMinimize | EnableDpiScaling | EnableInitialVisibility | AssignInitialFocus,
};

[[nodiscard]] constexpr WindowAttributeMask operator|(WindowAttributeMask Left, WindowAttributeMask Right) noexcept
{
    return static_cast<WindowAttributeMask>(static_cast<uint32_t>(Left) | static_cast<uint32_t>(Right));
}

[[nodiscard]] constexpr WindowAttributeMask operator&(WindowAttributeMask Left, WindowAttributeMask Right) noexcept
{
    return static_cast<WindowAttributeMask>(static_cast<uint32_t>(Left) & static_cast<uint32_t>(Right));
}

[[nodiscard]] constexpr WindowAttributeMask operator^(WindowAttributeMask Left, WindowAttributeMask Right) noexcept
{
    return static_cast<WindowAttributeMask>(static_cast<uint32_t>(Left) ^ static_cast<uint32_t>(Right));
}

[[nodiscard]] constexpr WindowAttributeMask operator~(WindowAttributeMask Mask) noexcept
{
    return static_cast<WindowAttributeMask>(~static_cast<uint32_t>(Mask));
}

constexpr WindowAttributeMask& operator|=(WindowAttributeMask& Left, WindowAttributeMask Right) noexcept
{
    Left = Left | Right;
    return Left;
}

constexpr WindowAttributeMask& operator&=(WindowAttributeMask& Left, WindowAttributeMask Right) noexcept
{
    Left = Left & Right;
    return Left;
}

[[nodiscard]] constexpr bool QueryAttributeEnabled(WindowAttributeMask Mask, WindowAttributeMask Directive) noexcept
{
    return (static_cast<uint32_t>(Mask) & static_cast<uint32_t>(Directive)) == static_cast<uint32_t>(Directive);
}

[[nodiscard]] constexpr WindowAttributeMask ReconcileAttributesForMode(
    WindowPresentationMode Mode,
    WindowAttributeMask    RequestedAttributes) noexcept
{
    switch (Mode)
    {
        case WindowPresentationMode::SplashBorderless:
        case WindowPresentationMode::BorderlessFullscreen:
        case WindowPresentationMode::ExclusiveFullscreen:
            return RequestedAttributes &
                   ~(WindowAttributeMask::EnableBorderDecoration | WindowAttributeMask::EnableInteractiveResize);

        case WindowPresentationMode::Windowed:
        case WindowPresentationMode::MaximizedWindowed:
        default:
            return RequestedAttributes;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WindowPlacementBounds
{
    int32_t  OriginX     = 120;  // [px] - Desktop screen-space X coordinate of top-left corner
    int32_t  OriginY     = 80;   // [px] - Desktop screen-space Y coordinate of top-left corner
    uint32_t PixelWidth  = 1600; // [px] - Client logical width in windowed mode
    uint32_t PixelHeight = 900;  // [px] - Client logical height in windowed mode
};

struct WindowConfigurationDescriptor
{
    std::string_view       TitleText           = "Frontier";                                  // [-]    - Surface caption text
    int32_t                OriginX             = 120;                                         // [px]   - Initial X position when not centered
    int32_t                OriginY             = 80;                                          // [px]   - Initial Y position when not centered
    uint32_t               PixelWidth          = 1600;                                        // [px]   - Initial client surface width
    uint32_t               PixelHeight         = 900;                                         // [px]   - Initial client surface height
    uint32_t               MinimumWidth        = 320;                                         // [px]   - Minimum interactive resize width
    uint32_t               MinimumHeight       = 200;                                         // [px]   - Minimum interactive resize height
    uint32_t               TargetRefreshRateHz = 0;                                           // [Hz]   - 0 = monitor native refresh rate
    uint32_t               TargetMonitorIndex  = 0;                                           // [idx]  - 0 = primary monitor
    float                  DpiScaleFactor      = 1.0f;                                        // [-]    - Logical-to-physical pixel ratio
    WindowPresentationMode PresentationMode    = WindowPresentationMode::Windowed;            // [-]    - Initial presentation mode
    WindowAttributeMask    AttributeMask       = WindowAttributeMask::ApplicationPresetMask;  // [mask] - Composable window directives

    [[nodiscard]] static constexpr WindowConfigurationDescriptor ConstructSplashWindow(
        std::string_view Title        = "Frontier",
        uint32_t         SplashWidth  = 720,
        uint32_t         SplashHeight = 420) noexcept
    {
        WindowConfigurationDescriptor Descriptor{};
        Descriptor.TitleText        = Title;
        Descriptor.PixelWidth       = SplashWidth;
        Descriptor.PixelHeight      = SplashHeight;
        Descriptor.MinimumWidth     = SplashWidth;
        Descriptor.MinimumHeight    = SplashHeight;
        Descriptor.PresentationMode = WindowPresentationMode::SplashBorderless;
        Descriptor.AttributeMask    = WindowAttributeMask::SplashPresetMask;
        return Descriptor;
    }

    [[nodiscard]] static constexpr WindowConfigurationDescriptor ConstructApplicationWindow(
        std::string_view Title                 = "Frontier",
        uint32_t         ClientWidth           = 1600,
        uint32_t         ClientHeight          = 900,
        bool             EnableInitialMaximize = false) noexcept
    {
        WindowConfigurationDescriptor Descriptor{};
        Descriptor.TitleText        = Title;
        Descriptor.PixelWidth       = ClientWidth;
        Descriptor.PixelHeight      = ClientHeight;
        Descriptor.MinimumWidth     = 960;
        Descriptor.MinimumHeight    = 540;
        Descriptor.PresentationMode = EnableInitialMaximize ? WindowPresentationMode::MaximizedWindowed
                                                            : WindowPresentationMode::Windowed;
        Descriptor.AttributeMask    = WindowAttributeMask::ApplicationPresetMask;
        return Descriptor;
    }

    [[nodiscard]] static constexpr WindowConfigurationDescriptor ConstructGameWindow(
        std::string_view       Title         = "Frontier Game",
        uint32_t               SurfaceWidth  = 1920,
        uint32_t               SurfaceHeight = 1080,
        WindowPresentationMode Mode          = WindowPresentationMode::Windowed,
        uint32_t               RefreshRateHz = 0) noexcept
    {
        WindowConfigurationDescriptor Descriptor{};
        Descriptor.TitleText           = Title;
        Descriptor.PixelWidth          = SurfaceWidth;
        Descriptor.PixelHeight         = SurfaceHeight;
        Descriptor.MinimumWidth        = 640;
        Descriptor.MinimumHeight       = 360;
        Descriptor.TargetRefreshRateHz = RefreshRateHz;
        Descriptor.PresentationMode    = Mode;
        Descriptor.AttributeMask       = ReconcileAttributesForMode(Mode, WindowAttributeMask::GamePresetMask);
        return Descriptor;
    }

    [[nodiscard]] static constexpr WindowConfigurationDescriptor ConstructDetachedPanelWindow(
        std::string_view Title       = "Frontier Panel",
        int32_t          PanelX      = 240,
        int32_t          PanelY      = 160,
        uint32_t         PanelWidth  = 460,
        uint32_t         PanelHeight = 620) noexcept
    {
        WindowConfigurationDescriptor Descriptor{};
        Descriptor.TitleText        = Title;
        Descriptor.OriginX          = PanelX;
        Descriptor.OriginY          = PanelY;
        Descriptor.PixelWidth       = PanelWidth;
        Descriptor.PixelHeight      = PanelHeight;
        Descriptor.MinimumWidth     = 260;
        Descriptor.MinimumHeight    = 180;
        Descriptor.PresentationMode = WindowPresentationMode::Windowed;
        Descriptor.AttributeMask    = WindowAttributeMask::EnableBorderDecoration |
                                      WindowAttributeMask::EnableInteractiveResize |
                                      WindowAttributeMask::EnableDpiScaling |
                                      WindowAttributeMask::EnableExternalDrop |
                                      WindowAttributeMask::EnableInitialVisibility |
                                      WindowAttributeMask::AssignInitialFocus;
        return Descriptor;
    }
};

struct WindowExtentReading
{
    int32_t  OriginX             = 0;     // [px] - Current desktop X position
    int32_t  OriginY             = 0;     // [px] - Current desktop Y position
    uint32_t PixelWidth          = 0;     // [px] - Current client logical width
    uint32_t PixelHeight         = 0;     // [px] - Current client logical height
    uint32_t SurfacePixelWidth  = 0;     // [px] - Current physical swapchain-surface width
    uint32_t SurfacePixelHeight = 0;     // [px] - Current physical swapchain-surface height
    uint32_t RefreshRateHz       = 60;    // [Hz] - Active display refresh rate
    float    DpiScaleFactor      = 1.0f;  // [-]  - Active display DPI scale factor
    bool     Minimized           = false; // [-]  - True when iconified or zero surface area
    bool     Maximized           = false; // [-]  - True when occupying maximized work-area
    bool     Focused             = true;  // [-]  - True when holding OS keyboard focus
    bool     Visible             = true;  // [-]  - True when mapped on screen
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONCEPTS
//------------------------------------------------------------------------------------------------------------------------

template <typename SurfaceType>
concept PlatformWindowSurface = requires(
    SurfaceType&                         Surface,
    const WindowConfigurationDescriptor& Descriptor,
    Input::InputExtension&               InputCoordinator,
    WindowPresentationMode               Mode,
    WindowAttributeMask                  Attributes)
{
    { Surface.Initialize(Descriptor) }                        noexcept -> std::same_as<bool>;
    { Surface.PollOperatingEvents(InputCoordinator) }         noexcept -> std::same_as<void>;
    { Surface.ApplyPresentationMode(Mode, 0u, 0u, 0u) }       noexcept -> std::same_as<bool>;
    { Surface.ApplyAttributeMask(Attributes) }                noexcept -> std::same_as<bool>;
    { Surface.Finalize() }                                    noexcept -> std::same_as<void>;
    { Surface.CloseRequested() }                              noexcept -> std::same_as<bool>;
    { Surface.InspectExtent() }                               noexcept -> std::same_as<WindowExtentReading>;
    { Surface.ActivePresentationMode() }                      noexcept -> std::same_as<WindowPresentationMode>;
    { Surface.ActiveAttributeMask() }                         noexcept -> std::same_as<WindowAttributeMask>;
};

//------------------------------------------------------------------------------------------------------------------------
//                                              HEADLESS VALIDATION SURFACE
//------------------------------------------------------------------------------------------------------------------------

class HeadlessPlatformWindow
{
public:
    HeadlessPlatformWindow() noexcept = default;

    //--------------------------------------------------------------------------------------------------------------------
    //                                                FUNCTION PROTOTYPES
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] bool Initialize(const WindowConfigurationDescriptor& Descriptor) noexcept;
    void               PollOperatingEvents(Input::InputExtension& InputCoordinator) noexcept;
    void               Finalize() noexcept;

    [[nodiscard]] bool ApplyPresentationMode(
        WindowPresentationMode NewMode,
        uint32_t               FullscreenWidth  = 0,
        uint32_t               FullscreenHeight = 0,
        uint32_t               RefreshRateHz    = 0) noexcept;
    [[nodiscard]] bool ApplyAttributeMask(WindowAttributeMask NewAttributes) noexcept;
    void               ApplyTitle(std::string_view NewTitle) noexcept;
    void               ApplySizeLimits(
        uint32_t MinWidth,
        uint32_t MinHeight,
        uint32_t MaxWidth  = 0,
        uint32_t MaxHeight = 0) noexcept;
    void               MoveSurface(int32_t NewOriginX, int32_t NewOriginY) noexcept;
    void               ResizeSurface(uint32_t NewWidth, uint32_t NewHeight, float NewDpiScale) noexcept;
    void               RequestClose() noexcept;
    [[nodiscard]] bool QueueSyntheticSignal(const Input::InputSignalPacket& Packet) noexcept;

    void ApplyCursorConfinement(Input::CursorConfinementMode Confinement) noexcept;
    void ApplyHapticFeedback(const Input::HapticFeedbackDirective& Haptic) noexcept;

    [[nodiscard]] bool                         CloseRequested()          const noexcept;
    [[nodiscard]] WindowExtentReading          InspectExtent()           const noexcept;
    [[nodiscard]] WindowPlatformCategory       PlatformCategory()        const noexcept;
    [[nodiscard]] WindowPresentationMode       ActivePresentationMode()  const noexcept;
    [[nodiscard]] WindowAttributeMask          ActiveAttributeMask()     const noexcept;
    [[nodiscard]] WindowPlacementBounds        SavedWindowedBounds()     const noexcept;
    [[nodiscard]] std::string_view             TitleText()               const noexcept;
    [[nodiscard]] Input::CursorConfinementMode AppliedCursorMode()       const noexcept;
    [[nodiscard]] uint32_t                     AppliedHapticPulseCount() const noexcept;

private:
    void RecomputeSurfaceExtent() noexcept;

    Input::InputExchange         PendingSyntheticExchange{};                                               // [-]     - Synthetic OS events queued for next poll
    std::string                  CaptionText          = "Frontier";                                        // [-]     - Current window caption
    WindowExtentReading          ActiveExtent{};                                                           // [px]    - Current simulated surface metrics
    WindowPlacementBounds        RestorableBounds{};                                                       // [px]    - Last windowed position/size before fullscreen
    WindowAttributeMask          WindowedAttributes   = WindowAttributeMask::ApplicationPresetMask;        // [mask]  - Windowed default attributes restored on exit
    WindowAttributeMask          EffectiveAttributes  = WindowAttributeMask::ApplicationPresetMask;        // [mask]  - Active attributes reconciled with mode
    WindowPresentationMode       CurrentMode          = WindowPresentationMode::Windowed;                  // [-]     - Current presentation mode
    uint32_t                     MinWidthLimit        = 1;                                                 // [px]    - Minimum resize clamp width
    uint32_t                     MinHeightLimit       = 1;                                                 // [px]    - Minimum resize clamp height
    uint32_t                     MaxWidthLimit        = 0;                                                 // [px]    - Maximum resize clamp width (0 = unbounded)
    uint32_t                     MaxHeightLimit       = 0;                                                 // [px]    - Maximum resize clamp height (0 = unbounded)
    Input::CursorConfinementMode CurrentCursorMode    = Input::CursorConfinementMode::FreeVisible;         // [-]     - Latest cursor confinement applied by Engine
    uint32_t                     DeliveredHapticCount = 0;                                                 // [count] - Outbound haptic directives received
    bool                         SurfaceInitialized   = false;                                             // [-]     - True while headless window is active
    bool                         ClosingRequested     = false;                                             // [-]     - True once RequestClose has been triggered
};

static_assert(PlatformWindowSurface<HeadlessPlatformWindow>,
              "HeadlessPlatformWindow must satisfy the PlatformWindowSurface C++20 concept.");
static_assert(Input::InputFeedbackTarget<HeadlessPlatformWindow>,
              "HeadlessPlatformWindow must satisfy the InputFeedbackTarget C++20 concept.");

#if !defined(__ANDROID__)

//------------------------------------------------------------------------------------------------------------------------
//                                       DESKTOP CROSS-PLATFORM SURFACE (GLFW 3.4)
//------------------------------------------------------------------------------------------------------------------------

class DesktopPlatformWindow
{
public:
    DesktopPlatformWindow() noexcept = default;
    ~DesktopPlatformWindow() noexcept;

    DesktopPlatformWindow(const DesktopPlatformWindow&)            = delete;
    DesktopPlatformWindow& operator=(const DesktopPlatformWindow&) = delete;

    //--------------------------------------------------------------------------------------------------------------------
    //                                                FUNCTION PROTOTYPES
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] bool Initialize(const WindowConfigurationDescriptor& Descriptor) noexcept;
    void               PollOperatingEvents(Input::InputExtension& InputCoordinator) noexcept;
    void               WaitOperatingEventsTimeout(double TimeoutSeconds) noexcept;
    void               Finalize() noexcept;

    [[nodiscard]] bool ApplyPresentationMode(
        WindowPresentationMode NewMode,
        uint32_t               FullscreenWidth  = 0,
        uint32_t               FullscreenHeight = 0,
        uint32_t               RefreshRateHz    = 0) noexcept;
    [[nodiscard]] bool ApplyAttributeMask(WindowAttributeMask NewAttributes) noexcept;
    void               ApplyTitle(std::string_view NewTitle) noexcept;
    void               ApplySizeLimits(
        uint32_t MinWidth,
        uint32_t MinHeight,
        uint32_t MaxWidth  = 0,
        uint32_t MaxHeight = 0) noexcept;
    void               MoveSurface(int32_t NewOriginX, int32_t NewOriginY) noexcept;
    void               ResizeSurface(uint32_t NewWidth, uint32_t NewHeight) noexcept;
    void               RequestClose() noexcept;

    void ApplyCursorConfinement(Input::CursorConfinementMode Confinement) noexcept;
    void ApplyHapticFeedback(const Input::HapticFeedbackDirective& Haptic) noexcept;

    [[nodiscard]] bool                         CloseRequested()          const noexcept;
    [[nodiscard]] WindowExtentReading          InspectExtent()           const noexcept;
    [[nodiscard]] WindowPlatformCategory       PlatformCategory()        const noexcept;
    [[nodiscard]] WindowPresentationMode       ActivePresentationMode()  const noexcept;
    [[nodiscard]] WindowAttributeMask          ActiveAttributeMask()     const noexcept;
    [[nodiscard]] WindowPlacementBounds        SavedWindowedBounds()     const noexcept;
    [[nodiscard]] std::string_view             TitleText()               const noexcept;
    [[nodiscard]] Input::CursorConfinementMode AppliedCursorMode()       const noexcept;
    [[nodiscard]] uint32_t                     AppliedHapticPulseCount() const noexcept;
    [[nodiscard]] void*                        NativeGlfwHandle()        const noexcept;

private:
    void RefreshCachedExtent() noexcept;

    void*                        GlfwWindowHandle     = nullptr;                                           // [-]     - Opaque GLFWwindow* instance
    Input::InputExchange         CallbackExchange{};                                                       // [-]     - OS callback events buffered until next poll
    std::string                  CaptionText          = "Frontier";                                        // [-]     - Current window caption
    WindowExtentReading          ActiveExtent{};                                                           // [px]    - Latest queried window & surface dimensions
    WindowPlacementBounds        RestorableBounds{};                                                       // [px]    - Saved windowed placement prior to fullscreen
    WindowAttributeMask          WindowedAttributes   = WindowAttributeMask::ApplicationPresetMask;        // [mask]  - Windowed default attributes restored on exit
    WindowAttributeMask          EffectiveAttributes  = WindowAttributeMask::ApplicationPresetMask;        // [mask]  - Active attributes reconciled with mode
    WindowPresentationMode       CurrentMode          = WindowPresentationMode::Windowed;                  // [-]     - Current presentation mode
    WindowPlatformCategory       SelectedPlatform     = WindowPlatformCategory::HeadlessStub;              // [-]     - Resolved runtime platform (Win32/X11/Wayland/Null)
    uint32_t                     TargetMonitorSlot    = 0;                                                 // [idx]   - Selected monitor index
    uint32_t                     MinWidthLimit        = 1;                                                 // [px]    - Minimum resize clamp width
    uint32_t                     MinHeightLimit       = 1;                                                 // [px]    - Minimum resize clamp height
    uint32_t                     MaxWidthLimit        = 0;                                                 // [px]    - Maximum resize clamp width
    uint32_t                     MaxHeightLimit       = 0;                                                 // [px]    - Maximum resize clamp height
    Input::CursorConfinementMode CurrentCursorMode    = Input::CursorConfinementMode::FreeVisible;         // [-]     - Latest cursor confinement applied
    uint32_t                     DeliveredHapticCount = 0;                                                 // [count] - Outbound haptic directives received
    float                        LastCursorX          = 0.0f;                                              // [px]    - Previous GLFW cursor X for delta computation
    float                        LastCursorY          = 0.0f;                                              // [px]    - Previous GLFW cursor Y for delta computation
    bool                         CursorPositionSeeded = false;                                             // [-]     - True after initial cursor sample
    std::vector<std::string>     PendingDroppedPaths{};                                                    // [-]     - External OS drop paths buffered until next poll
};

static_assert(PlatformWindowSurface<DesktopPlatformWindow>,
              "DesktopPlatformWindow must satisfy the PlatformWindowSurface C++20 concept.");
static_assert(Input::InputFeedbackTarget<DesktopPlatformWindow>,
              "DesktopPlatformWindow must satisfy the InputFeedbackTarget C++20 concept.");

#endif // !defined(__ANDROID__)

} // namespace Frontier::Windowing
