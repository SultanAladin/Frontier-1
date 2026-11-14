/*============================================================================================================================================
                                                          PLATFORMWINDOWDESKTOP.CPP
============================================================================================================================================*/
// 🧩 GLFW 3.4 cross-platform desktop window implementation (Win32, X11, Wayland, and offscreen GLFW_PLATFORM_NULL interface)

#if !defined(__ANDROID__)

#include "Engine/Windowing/PlatformWindow.h"

#ifndef GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace Frontier::Windowing
{

namespace
{

std::atomic<uint32_t> ActiveGlfwWindowCount{ 0 }; // [count] - Reference-counted GLFW library lifetime

[[nodiscard]] uint64_t CurrentMonotonicNanoseconds() noexcept
{
    const auto Now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Now).count());
}

[[nodiscard]] WindowPlatformCategory ResolveGlfwPlatformCategory() noexcept
{
    switch (glfwGetPlatform())
    {
        case GLFW_PLATFORM_WIN32:   return WindowPlatformCategory::Win32;
        case GLFW_PLATFORM_X11:     return WindowPlatformCategory::X11;
        case GLFW_PLATFORM_WAYLAND: return WindowPlatformCategory::Wayland;
        case GLFW_PLATFORM_NULL:
        default:                    return WindowPlatformCategory::GlfwNull;
    }
}

[[nodiscard]] bool EnsureGlfwInitialized() noexcept
{
    if (ActiveGlfwWindowCount.load(std::memory_order_acquire) > 0)
    {
        return true;
    }

#if defined(__linux__)
    const char* DisplayEnv             = std::getenv("DISPLAY");
    const char* WaylandDisplayEnv      = std::getenv("WAYLAND_DISPLAY");
    const bool  DisplayServerAvailable = (DisplayEnv != nullptr && DisplayEnv[0] != '\0') ||
                                         (WaylandDisplayEnv != nullptr && WaylandDisplayEnv[0] != '\0');
    if (!DisplayServerAvailable)
    {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    }
#endif

    if (glfwInit() == GLFW_TRUE)
    {
        return true;
    }

    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    return glfwInit() == GLFW_TRUE;
}

[[nodiscard]] GLFWmonitor* SelectTargetMonitor(uint32_t RequestedIndex) noexcept
{
    int           MonitorCount = 0;
    GLFWmonitor** Monitors     = glfwGetMonitors(&MonitorCount);
    if (Monitors == nullptr || MonitorCount <= 0)
    {
        return glfwGetPrimaryMonitor();
    }
    const uint32_t ClampedIndex = std::min<uint32_t>(RequestedIndex, static_cast<uint32_t>(MonitorCount - 1));
    return Monitors[ClampedIndex];
}

[[nodiscard]] uint16_t TranslateGlfwModifierMask(int Mods) noexcept
{
    uint16_t Mask = 0u;
    if ((Mods & GLFW_MOD_SHIFT) != 0)   Mask |= Input::InputModifierBits::Shift;
    if ((Mods & GLFW_MOD_CONTROL) != 0) Mask |= Input::InputModifierBits::Control;
    if ((Mods & GLFW_MOD_ALT) != 0)     Mask |= Input::InputModifierBits::Alt;
    if ((Mods & GLFW_MOD_SUPER) != 0)   Mask |= Input::InputModifierBits::Super;
    return Mask;
}

[[nodiscard]] Input::InputLogicalKey TranslateGlfwLogicalKey(int GlfwKey) noexcept
{
    switch (GlfwKey)
    {
        case GLFW_KEY_ESCAPE:        return Input::InputLogicalKey::Escape;
        case GLFW_KEY_ENTER:         return Input::InputLogicalKey::Enter;
        case GLFW_KEY_KP_ENTER:      return Input::InputLogicalKey::KeypadEnter;
        case GLFW_KEY_TAB:           return Input::InputLogicalKey::Tab;
        case GLFW_KEY_BACKSPACE:     return Input::InputLogicalKey::Backspace;
        case GLFW_KEY_DELETE:        return Input::InputLogicalKey::Delete;
        case GLFW_KEY_SPACE:         return Input::InputLogicalKey::Space;
        case GLFW_KEY_LEFT:          return Input::InputLogicalKey::LeftArrow;
        case GLFW_KEY_RIGHT:         return Input::InputLogicalKey::RightArrow;
        case GLFW_KEY_UP:            return Input::InputLogicalKey::UpArrow;
        case GLFW_KEY_DOWN:          return Input::InputLogicalKey::DownArrow;
        case GLFW_KEY_HOME:          return Input::InputLogicalKey::Home;
        case GLFW_KEY_END:           return Input::InputLogicalKey::End;
        case GLFW_KEY_PAGE_UP:       return Input::InputLogicalKey::PageUp;
        case GLFW_KEY_PAGE_DOWN:     return Input::InputLogicalKey::PageDown;
        case GLFW_KEY_COMMA:         return Input::InputLogicalKey::Comma;
        case GLFW_KEY_PERIOD:        return Input::InputLogicalKey::Period;
        case GLFW_KEY_KP_DECIMAL:    return Input::InputLogicalKey::Period;
        case GLFW_KEY_SLASH:         return Input::InputLogicalKey::Slash;
        case GLFW_KEY_SEMICOLON:     return Input::InputLogicalKey::Semicolon;
        case GLFW_KEY_A:             return Input::InputLogicalKey::A;
        case GLFW_KEY_B:             return Input::InputLogicalKey::B;
        case GLFW_KEY_C:             return Input::InputLogicalKey::C;
        case GLFW_KEY_D:             return Input::InputLogicalKey::D;
        case GLFW_KEY_E:             return Input::InputLogicalKey::E;
        case GLFW_KEY_F:             return Input::InputLogicalKey::F;
        case GLFW_KEY_G:             return Input::InputLogicalKey::G;
        case GLFW_KEY_H:             return Input::InputLogicalKey::H;
        case GLFW_KEY_I:             return Input::InputLogicalKey::I;
        case GLFW_KEY_J:             return Input::InputLogicalKey::J;
        case GLFW_KEY_K:             return Input::InputLogicalKey::K;
        case GLFW_KEY_L:             return Input::InputLogicalKey::L;
        case GLFW_KEY_M:             return Input::InputLogicalKey::M;
        case GLFW_KEY_N:             return Input::InputLogicalKey::N;
        case GLFW_KEY_O:             return Input::InputLogicalKey::O;
        case GLFW_KEY_P:             return Input::InputLogicalKey::P;
        case GLFW_KEY_Q:             return Input::InputLogicalKey::Q;
        case GLFW_KEY_R:             return Input::InputLogicalKey::R;
        case GLFW_KEY_S:             return Input::InputLogicalKey::S;
        case GLFW_KEY_T:             return Input::InputLogicalKey::T;
        case GLFW_KEY_U:             return Input::InputLogicalKey::U;
        case GLFW_KEY_V:             return Input::InputLogicalKey::V;
        case GLFW_KEY_W:             return Input::InputLogicalKey::W;
        case GLFW_KEY_X:             return Input::InputLogicalKey::X;
        case GLFW_KEY_Y:             return Input::InputLogicalKey::Y;
        case GLFW_KEY_Z:             return Input::InputLogicalKey::Z;
        case GLFW_KEY_0:
        case GLFW_KEY_KP_0:          return Input::InputLogicalKey::Digit0;
        case GLFW_KEY_1:
        case GLFW_KEY_KP_1:          return Input::InputLogicalKey::Digit1;
        case GLFW_KEY_2:
        case GLFW_KEY_KP_2:          return Input::InputLogicalKey::Digit2;
        case GLFW_KEY_3:
        case GLFW_KEY_KP_3:          return Input::InputLogicalKey::Digit3;
        case GLFW_KEY_4:
        case GLFW_KEY_KP_4:          return Input::InputLogicalKey::Digit4;
        case GLFW_KEY_5:
        case GLFW_KEY_KP_5:          return Input::InputLogicalKey::Digit5;
        case GLFW_KEY_6:
        case GLFW_KEY_KP_6:          return Input::InputLogicalKey::Digit6;
        case GLFW_KEY_7:
        case GLFW_KEY_KP_7:          return Input::InputLogicalKey::Digit7;
        case GLFW_KEY_8:
        case GLFW_KEY_KP_8:          return Input::InputLogicalKey::Digit8;
        case GLFW_KEY_9:
        case GLFW_KEY_KP_9:          return Input::InputLogicalKey::Digit9;
        case GLFW_KEY_F1:            return Input::InputLogicalKey::F1;
        case GLFW_KEY_F2:            return Input::InputLogicalKey::F2;
        case GLFW_KEY_F3:            return Input::InputLogicalKey::F3;
        case GLFW_KEY_F4:            return Input::InputLogicalKey::F4;
        case GLFW_KEY_F5:            return Input::InputLogicalKey::F5;
        case GLFW_KEY_F6:            return Input::InputLogicalKey::F6;
        case GLFW_KEY_F7:            return Input::InputLogicalKey::F7;
        case GLFW_KEY_F8:            return Input::InputLogicalKey::F8;
        case GLFW_KEY_F9:            return Input::InputLogicalKey::F9;
        case GLFW_KEY_F10:           return Input::InputLogicalKey::F10;
        case GLFW_KEY_F11:           return Input::InputLogicalKey::F11;
        case GLFW_KEY_F12:           return Input::InputLogicalKey::F12;
        case GLFW_KEY_LEFT_SHIFT:    return Input::InputLogicalKey::LeftShift;
        case GLFW_KEY_RIGHT_SHIFT:   return Input::InputLogicalKey::RightShift;
        case GLFW_KEY_LEFT_CONTROL:  return Input::InputLogicalKey::LeftControl;
        case GLFW_KEY_RIGHT_CONTROL: return Input::InputLogicalKey::RightControl;
        case GLFW_KEY_LEFT_ALT:      return Input::InputLogicalKey::LeftAlt;
        case GLFW_KEY_RIGHT_ALT:     return Input::InputLogicalKey::RightAlt;
        case GLFW_KEY_LEFT_SUPER:    return Input::InputLogicalKey::LeftSuper;
        case GLFW_KEY_RIGHT_SUPER:   return Input::InputLogicalKey::RightSuper;
        default:                     return Input::InputLogicalKey::Unassigned;
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  LIFECYCLE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

DesktopPlatformWindow::~DesktopPlatformWindow() noexcept
{
    Finalize();
}

bool DesktopPlatformWindow::Initialize(const WindowConfigurationDescriptor& Descriptor) noexcept
{
    if (Descriptor.PixelWidth == 0 || Descriptor.PixelHeight == 0)
    {
        return false;
    }

    Finalize();

    if (!EnsureGlfwInitialized())
    {
        return false;
    }

    CallbackExchange.Initialize();
    CaptionText.assign(Descriptor.TitleText.data(), Descriptor.TitleText.size());
    TargetMonitorSlot = Descriptor.TargetMonitorIndex;
    MinWidthLimit     = std::max<uint32_t>(1u, Descriptor.MinimumWidth);
    MinHeightLimit    = std::max<uint32_t>(1u, Descriptor.MinimumHeight);
    MaxWidthLimit     = 0;
    MaxHeightLimit    = 0;

    WindowedAttributes = Descriptor.AttributeMask;
    if (Descriptor.PresentationMode != WindowPresentationMode::Windowed &&
        Descriptor.PresentationMode != WindowPresentationMode::MaximizedWindowed &&
        !QueryAttributeEnabled(WindowedAttributes, WindowAttributeMask::EnableBorderDecoration))
    {
        WindowedAttributes = Descriptor.AttributeMask |
                             WindowAttributeMask::EnableBorderDecoration |
                             WindowAttributeMask::EnableInteractiveResize;
    }

    EffectiveAttributes = ReconcileAttributesForMode(Descriptor.PresentationMode, Descriptor.AttributeMask);
    CurrentMode         = Descriptor.PresentationMode;

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_DECORATED,
                   QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::EnableBorderDecoration) ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE,
                   QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::EnableInteractiveResize) ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING,
                   QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::EnableTopmostOrder) ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_AUTO_ICONIFY,
                   QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::EnableFocusMinimize) ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR,
                   QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::EnableDpiScaling) ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER,
                   QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::EnableAlphaCompositing) ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE,
                   QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::EnableInitialVisibility) ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW,
                   QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::AssignInitialFocus) ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED,
                   (Descriptor.PresentationMode == WindowPresentationMode::MaximizedWindowed) ? GLFW_TRUE : GLFW_FALSE);

    GLFWmonitor*       Monitor   = SelectTargetMonitor(TargetMonitorSlot);
    const GLFWvidmode* VideoMode = (Monitor != nullptr) ? glfwGetVideoMode(Monitor) : nullptr;

    uint32_t     CreateWidth   = std::max(Descriptor.PixelWidth, MinWidthLimit);
    uint32_t     CreateHeight  = std::max(Descriptor.PixelHeight, MinHeightLimit);
    GLFWmonitor* FullscreenMon = nullptr;

    RestorableBounds.OriginX     = Descriptor.OriginX;
    RestorableBounds.OriginY     = Descriptor.OriginY;
    RestorableBounds.PixelWidth  = CreateWidth;
    RestorableBounds.PixelHeight = CreateHeight;

    if (Monitor != nullptr && QueryAttributeEnabled(Descriptor.AttributeMask, WindowAttributeMask::AssignMonitorCenter))
    {
        int WorkX = 0;
        int WorkY = 0;
        int WorkW = 1920;
        int WorkH = 1080;
        glfwGetMonitorWorkarea(Monitor, &WorkX, &WorkY, &WorkW, &WorkH);
        RestorableBounds.OriginX = WorkX + std::max(0, (WorkW - static_cast<int>(CreateWidth)) / 2);
        RestorableBounds.OriginY = WorkY + std::max(0, (WorkH - static_cast<int>(CreateHeight)) / 2);
    }

    if (Descriptor.PresentationMode == WindowPresentationMode::ExclusiveFullscreen && Monitor != nullptr)
    {
        FullscreenMon = Monitor;
        if (Descriptor.TargetRefreshRateHz > 0)
        {
            glfwWindowHint(GLFW_REFRESH_RATE, static_cast<int>(Descriptor.TargetRefreshRateHz));
        }
        else if (VideoMode != nullptr)
        {
            glfwWindowHint(GLFW_REFRESH_RATE, VideoMode->refreshRate);
        }
    }
    else if (Descriptor.PresentationMode == WindowPresentationMode::BorderlessFullscreen && Monitor != nullptr && VideoMode != nullptr)
    {
        CreateWidth  = static_cast<uint32_t>(VideoMode->width);
        CreateHeight = static_cast<uint32_t>(VideoMode->height);
    }

    GLFWwindow* CreatedWindow = glfwCreateWindow(
        static_cast<int>(CreateWidth),
        static_cast<int>(CreateHeight),
        CaptionText.c_str(),
        FullscreenMon,
        nullptr);
    if (CreatedWindow == nullptr)
    {
        CallbackExchange.Finalize();
        return false;
    }

    ActiveGlfwWindowCount.fetch_add(1u, std::memory_order_acq_rel);
    GlfwWindowHandle = CreatedWindow;
    SelectedPlatform = ResolveGlfwPlatformCategory();

    CursorPositionSeeded = false;
    LastCursorX          = 0.0f;
    LastCursorY          = 0.0f;
    PendingDroppedPaths.clear();

    glfwSetWindowUserPointer(CreatedWindow, this);
    if (SelectedPlatform != WindowPlatformCategory::GlfwNull)
    {
        glfwSetWindowSizeLimits(
            CreatedWindow,
            static_cast<int>(MinWidthLimit),
            static_cast<int>(MinHeightLimit),
            GLFW_DONT_CARE,
            GLFW_DONT_CARE);
    }

    if (Descriptor.PresentationMode == WindowPresentationMode::BorderlessFullscreen && Monitor != nullptr)
    {
        int MonX = 0;
        int MonY = 0;
        glfwGetMonitorPos(Monitor, &MonX, &MonY);
        glfwSetWindowPos(CreatedWindow, MonX, MonY);
    }
    else if (Descriptor.PresentationMode == WindowPresentationMode::Windowed ||
             Descriptor.PresentationMode == WindowPresentationMode::SplashBorderless)
    {
        glfwSetWindowPos(CreatedWindow, RestorableBounds.OriginX, RestorableBounds.OriginY);
    }

    glfwSetKeyCallback(CreatedWindow, [](GLFWwindow* Win, int Key, int Scancode, int KeyTransition, int Mods) {
        auto* Self = static_cast<DesktopPlatformWindow*>(glfwGetWindowUserPointer(Win));
        if (Self == nullptr) return;
        Input::InputSignalPacket Packet{};
        Packet.TimestampNanoseconds = CurrentMonotonicNanoseconds();
        Packet.Category             = Input::InputSignalCategory::KeyboardKey;
        Packet.ControlCode          = static_cast<uint32_t>(Scancode > 0 ? Scancode : (Key > 0 ? Key : 0));
        Packet.LogicalKey           = TranslateGlfwLogicalKey(Key);
        Packet.ModifierBitmask      = TranslateGlfwModifierMask(Mods);
        if (KeyTransition == GLFW_PRESS)
        {
            Packet.Transition  = Input::ButtonTransitionCategory::Pressed;
            Packet.PrimaryAxis = 1.0f;
        }
        else if (KeyTransition == GLFW_REPEAT)
        {
            Packet.Transition  = Input::ButtonTransitionCategory::Repeated;
            Packet.PrimaryAxis = 1.0f;
        }
        else
        {
            Packet.Transition  = Input::ButtonTransitionCategory::Released;
            Packet.PrimaryAxis = 0.0f;
        }
        (void)Self->CallbackExchange.IntakeSignal(Packet);
    });

    glfwSetCharCallback(CreatedWindow, [](GLFWwindow* Win, unsigned int Codepoint) {
        auto* Self = static_cast<DesktopPlatformWindow*>(glfwGetWindowUserPointer(Win));
        if (Self == nullptr) return;
        Input::InputSignalPacket Packet{};
        Packet.TimestampNanoseconds = CurrentMonotonicNanoseconds();
        Packet.Category             = Input::InputSignalCategory::TextCodepoint;
        Packet.ControlCode          = static_cast<uint32_t>(Codepoint);
        (void)Self->CallbackExchange.IntakeSignal(Packet);
    });

    glfwSetMouseButtonCallback(CreatedWindow, [](GLFWwindow* Win, int Button, int ButtonTransition, int Mods) {
        auto* Self = static_cast<DesktopPlatformWindow*>(glfwGetWindowUserPointer(Win));
        if (Self == nullptr) return;
        Input::InputSignalPacket Packet{};
        Packet.TimestampNanoseconds = CurrentMonotonicNanoseconds();
        Packet.Category             = Input::InputSignalCategory::PointerButton;
        Packet.ControlCode          = static_cast<uint32_t>(Button);
        Packet.ModifierBitmask      = TranslateGlfwModifierMask(Mods);
        Packet.Transition           = (ButtonTransition == GLFW_PRESS) ? Input::ButtonTransitionCategory::Pressed
                                                                       : Input::ButtonTransitionCategory::Released;
        Packet.PrimaryAxis          = (ButtonTransition == GLFW_PRESS) ? 1.0f : 0.0f;
        (void)Self->CallbackExchange.IntakeSignal(Packet);
    });

    glfwSetCursorPosCallback(CreatedWindow, [](GLFWwindow* Win, double PosX, double PosY) {
        auto* Self = static_cast<DesktopPlatformWindow*>(glfwGetWindowUserPointer(Win));
        if (Self == nullptr) return;
        const float NewX = static_cast<float>(PosX);
        const float NewY = static_cast<float>(PosY);
        Input::InputSignalPacket Packet{};
        Packet.TimestampNanoseconds = CurrentMonotonicNanoseconds();
        Packet.Category             = Input::InputSignalCategory::PointerMotion;
        Packet.PrimaryAxis          = NewX;
        Packet.SecondaryAxis        = NewY;
        Packet.DeltaPrimary         = Self->CursorPositionSeeded ? (NewX - Self->LastCursorX) : 0.0f;
        Packet.DeltaSecondary       = Self->CursorPositionSeeded ? (NewY - Self->LastCursorY) : 0.0f;
        Self->LastCursorX           = NewX;
        Self->LastCursorY           = NewY;
        Self->CursorPositionSeeded  = true;
        (void)Self->CallbackExchange.IntakeSignal(Packet);
    });

    glfwSetScrollCallback(CreatedWindow, [](GLFWwindow* Win, double ScrollX, double ScrollY) {
        auto* Self = static_cast<DesktopPlatformWindow*>(glfwGetWindowUserPointer(Win));
        if (Self == nullptr) return;
        Input::InputSignalPacket Packet{};
        Packet.TimestampNanoseconds = CurrentMonotonicNanoseconds();
        Packet.Category             = Input::InputSignalCategory::ScrollWheel;
        Packet.PrimaryAxis          = static_cast<float>(ScrollX);
        Packet.SecondaryAxis        = static_cast<float>(ScrollY);
        (void)Self->CallbackExchange.IntakeSignal(Packet);
    });

    glfwSetDropCallback(CreatedWindow, [](GLFWwindow* Win, int Count, const char** Paths) {
        auto* Self = static_cast<DesktopPlatformWindow*>(glfwGetWindowUserPointer(Win));
        if (Self == nullptr || Paths == nullptr) return;
        for (int Idx = 0; Idx < Count; ++Idx)
        {
            if (Paths[Idx] != nullptr)
            {
                Self->PendingDroppedPaths.emplace_back(Paths[Idx]);
            }
        }
    });

    RefreshCachedExtent();
    return true;
}

void DesktopPlatformWindow::PollOperatingEvents(Input::InputExtension& InputCoordinator) noexcept
{
    if (GlfwWindowHandle == nullptr)
    {
        return;
    }

    glfwPollEvents();
    RefreshCachedExtent();

    struct ForwardToCoordinatorSink
    {
        Input::InputExtension* Coordinator;

        void IntakeSignalPacket(const Input::InputSignalPacket& Packet) noexcept
        {
            (void)Coordinator->IntakeHardwareSignal(Packet);
        }
    };

    ForwardToCoordinatorSink Forwarder{ &InputCoordinator };
    (void)CallbackExchange.DrainSignals(Forwarder);

    for (const std::string& PathEntry : PendingDroppedPaths)
    {
        InputCoordinator.IntakeExternalDroppedPath(PathEntry);
    }
    PendingDroppedPaths.clear();

    (void)InputCoordinator.FlushOutboundDirectives(*this);
}

void DesktopPlatformWindow::WaitOperatingEventsTimeout(double TimeoutSeconds) noexcept
{
    if (GlfwWindowHandle == nullptr)
    {
        return;
    }
    glfwWaitEventsTimeout(std::max(0.0, TimeoutSeconds));
    RefreshCachedExtent();
}

void DesktopPlatformWindow::Finalize() noexcept
{
    if (GlfwWindowHandle != nullptr)
    {
        auto* Window = static_cast<GLFWwindow*>(GlfwWindowHandle);
        glfwSetWindowUserPointer(Window, nullptr);
        glfwDestroyWindow(Window);
        GlfwWindowHandle = nullptr;

        if (ActiveGlfwWindowCount.fetch_sub(1u, std::memory_order_acq_rel) == 1u)
        {
            glfwTerminate();
        }
    }

    CallbackExchange.Finalize();
    PendingDroppedPaths.clear();
    CursorPositionSeeded = false;
    LastCursorX          = 0.0f;
    LastCursorY          = 0.0f;
    ActiveExtent         = {};
    RestorableBounds     = {};
    CurrentMode          = WindowPresentationMode::Windowed;
    SelectedPlatform     = WindowPlatformCategory::HeadlessStub;
    CurrentCursorMode    = Input::CursorConfinementMode::FreeVisible;
    DeliveredHapticCount = 0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        PRESENTATION MODE & ATTRIBUTE MUTATIONS
//------------------------------------------------------------------------------------------------------------------------

bool DesktopPlatformWindow::ApplyPresentationMode(
    WindowPresentationMode NewMode,
    uint32_t               FullscreenWidth,
    uint32_t               FullscreenHeight,
    uint32_t               RefreshRateHz) noexcept
{
    if (GlfwWindowHandle == nullptr)
    {
        return false;
    }

    auto* Window = static_cast<GLFWwindow*>(GlfwWindowHandle);
    RefreshCachedExtent();

    if (CurrentMode == WindowPresentationMode::Windowed &&
        NewMode != WindowPresentationMode::Windowed &&
        ActiveExtent.PixelWidth > 0 &&
        ActiveExtent.PixelHeight > 0)
    {
        RestorableBounds.OriginX     = ActiveExtent.OriginX;
        RestorableBounds.OriginY     = ActiveExtent.OriginY;
        RestorableBounds.PixelWidth  = ActiveExtent.PixelWidth;
        RestorableBounds.PixelHeight = ActiveExtent.PixelHeight;
    }

    GLFWmonitor*       Monitor   = SelectTargetMonitor(TargetMonitorSlot);
    const GLFWvidmode* VideoMode = (Monitor != nullptr) ? glfwGetVideoMode(Monitor) : nullptr;
    CurrentMode                  = NewMode;

    switch (NewMode)
    {
        case WindowPresentationMode::SplashBorderless:
        {
            EffectiveAttributes   = ReconcileAttributesForMode(NewMode, EffectiveAttributes);
            const uint32_t Width  = (FullscreenWidth > 0) ? FullscreenWidth : RestorableBounds.PixelWidth;
            const uint32_t Height = (FullscreenHeight > 0) ? FullscreenHeight : RestorableBounds.PixelHeight;
            glfwSetWindowMonitor(
                Window,
                nullptr,
                RestorableBounds.OriginX,
                RestorableBounds.OriginY,
                static_cast<int>(Width),
                static_cast<int>(Height),
                GLFW_DONT_CARE);
            glfwSetWindowAttrib(Window, GLFW_DECORATED, GLFW_FALSE);
            glfwSetWindowAttrib(Window, GLFW_RESIZABLE, GLFW_FALSE);
            break;
        }

        case WindowPresentationMode::Windowed:
        {
            EffectiveAttributes = ReconcileAttributesForMode(NewMode, WindowedAttributes);
            if (glfwGetWindowAttrib(Window, GLFW_MAXIMIZED) == GLFW_TRUE)
            {
                glfwRestoreWindow(Window);
            }
            glfwSetWindowAttrib(
                Window,
                GLFW_DECORATED,
                QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::EnableBorderDecoration) ? GLFW_TRUE : GLFW_FALSE);
            glfwSetWindowAttrib(
                Window,
                GLFW_RESIZABLE,
                QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::EnableInteractiveResize) ? GLFW_TRUE : GLFW_FALSE);
            glfwSetWindowMonitor(
                Window,
                nullptr,
                RestorableBounds.OriginX,
                RestorableBounds.OriginY,
                static_cast<int>(RestorableBounds.PixelWidth),
                static_cast<int>(RestorableBounds.PixelHeight),
                GLFW_DONT_CARE);
            break;
        }

        case WindowPresentationMode::MaximizedWindowed:
        {
            EffectiveAttributes = ReconcileAttributesForMode(NewMode, WindowedAttributes);
            glfwSetWindowMonitor(
                Window,
                nullptr,
                RestorableBounds.OriginX,
                RestorableBounds.OriginY,
                static_cast<int>(RestorableBounds.PixelWidth),
                static_cast<int>(RestorableBounds.PixelHeight),
                GLFW_DONT_CARE);
            glfwSetWindowAttrib(
                Window,
                GLFW_DECORATED,
                QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::EnableBorderDecoration) ? GLFW_TRUE : GLFW_FALSE);
            glfwSetWindowAttrib(
                Window,
                GLFW_RESIZABLE,
                QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::EnableInteractiveResize) ? GLFW_TRUE : GLFW_FALSE);
            glfwMaximizeWindow(Window);
            break;
        }

        case WindowPresentationMode::BorderlessFullscreen:
        {
            EffectiveAttributes = ReconcileAttributesForMode(NewMode, WindowedAttributes);
            int MonX  = 0;
            int MonY  = 0;
            int FullW = (VideoMode != nullptr) ? VideoMode->width : 1920;
            int FullH = (VideoMode != nullptr) ? VideoMode->height : 1080;
            if (Monitor != nullptr)
            {
                glfwGetMonitorPos(Monitor, &MonX, &MonY);
            }
            glfwSetWindowAttrib(Window, GLFW_DECORATED, GLFW_FALSE);
            glfwSetWindowAttrib(Window, GLFW_RESIZABLE, GLFW_FALSE);
            glfwSetWindowMonitor(Window, nullptr, MonX, MonY, FullW, FullH, GLFW_DONT_CARE);
            break;
        }

        case WindowPresentationMode::ExclusiveFullscreen:
        {
            EffectiveAttributes = ReconcileAttributesForMode(NewMode, WindowedAttributes);
            const int ModeW  = (FullscreenWidth > 0) ? static_cast<int>(FullscreenWidth)
                                                     : ((VideoMode != nullptr) ? VideoMode->width : 1920);
            const int ModeH  = (FullscreenHeight > 0) ? static_cast<int>(FullscreenHeight)
                                                      : ((VideoMode != nullptr) ? VideoMode->height : 1080);
            const int ModeHz = (RefreshRateHz > 0) ? static_cast<int>(RefreshRateHz)
                                                   : ((VideoMode != nullptr) ? VideoMode->refreshRate : 60);
            glfwSetWindowMonitor(Window, Monitor, 0, 0, ModeW, ModeH, ModeHz);
            if (RefreshRateHz > 0)
            {
                ActiveExtent.RefreshRateHz = RefreshRateHz;
            }
            break;
        }
    }

    RefreshCachedExtent();
    if (NewMode == WindowPresentationMode::ExclusiveFullscreen && RefreshRateHz > 0)
    {
        ActiveExtent.RefreshRateHz = RefreshRateHz;
    }
    return true;
}

bool DesktopPlatformWindow::ApplyAttributeMask(WindowAttributeMask NewAttributes) noexcept
{
    if (GlfwWindowHandle == nullptr)
    {
        return false;
    }

    auto* Window        = static_cast<GLFWwindow*>(GlfwWindowHandle);
    WindowedAttributes  = NewAttributes;
    EffectiveAttributes = ReconcileAttributesForMode(CurrentMode, NewAttributes);

    glfwSetWindowAttrib(
        Window,
        GLFW_DECORATED,
        QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::EnableBorderDecoration) ? GLFW_TRUE : GLFW_FALSE);
    glfwSetWindowAttrib(
        Window,
        GLFW_RESIZABLE,
        QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::EnableInteractiveResize) ? GLFW_TRUE : GLFW_FALSE);
    glfwSetWindowAttrib(
        Window,
        GLFW_FLOATING,
        QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::EnableTopmostOrder) ? GLFW_TRUE : GLFW_FALSE);
    glfwSetWindowAttrib(
        Window,
        GLFW_AUTO_ICONIFY,
        QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::EnableFocusMinimize) ? GLFW_TRUE : GLFW_FALSE);
    glfwSetWindowAttrib(
        Window,
        GLFW_FOCUS_ON_SHOW,
        QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::AssignInitialFocus) ? GLFW_TRUE : GLFW_FALSE);

    if (QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::EnableInitialVisibility))
    {
        glfwShowWindow(Window);
    }
    else
    {
        glfwHideWindow(Window);
    }

    RefreshCachedExtent();
    return true;
}

void DesktopPlatformWindow::ApplyTitle(std::string_view NewTitle) noexcept
{
    CaptionText.assign(NewTitle.data(), NewTitle.size());
    if (GlfwWindowHandle != nullptr)
    {
        glfwSetWindowTitle(static_cast<GLFWwindow*>(GlfwWindowHandle), CaptionText.c_str());
    }
}

void DesktopPlatformWindow::ApplySizeLimits(
    uint32_t MinWidth,
    uint32_t MinHeight,
    uint32_t MaxWidth,
    uint32_t MaxHeight) noexcept
{
    MinWidthLimit  = std::max<uint32_t>(1u, MinWidth);
    MinHeightLimit = std::max<uint32_t>(1u, MinHeight);
    MaxWidthLimit  = MaxWidth;
    MaxHeightLimit = MaxHeight;

    if (GlfwWindowHandle != nullptr && SelectedPlatform != WindowPlatformCategory::GlfwNull)
    {
        glfwSetWindowSizeLimits(
            static_cast<GLFWwindow*>(GlfwWindowHandle),
            static_cast<int>(MinWidthLimit),
            static_cast<int>(MinHeightLimit),
            (MaxWidthLimit > 0) ? static_cast<int>(MaxWidthLimit) : GLFW_DONT_CARE,
            (MaxHeightLimit > 0) ? static_cast<int>(MaxHeightLimit) : GLFW_DONT_CARE);
        RefreshCachedExtent();
    }
}

void DesktopPlatformWindow::MoveSurface(int32_t NewOriginX, int32_t NewOriginY) noexcept
{
    if (GlfwWindowHandle == nullptr)
    {
        return;
    }
    glfwSetWindowPos(static_cast<GLFWwindow*>(GlfwWindowHandle), NewOriginX, NewOriginY);
    if (CurrentMode == WindowPresentationMode::Windowed)
    {
        RestorableBounds.OriginX = NewOriginX;
        RestorableBounds.OriginY = NewOriginY;
    }
    RefreshCachedExtent();
}

void DesktopPlatformWindow::ResizeSurface(uint32_t NewWidth, uint32_t NewHeight) noexcept
{
    if (GlfwWindowHandle == nullptr)
    {
        return;
    }
    glfwSetWindowSize(
        static_cast<GLFWwindow*>(GlfwWindowHandle),
        static_cast<int>(NewWidth),
        static_cast<int>(NewHeight));
    if (CurrentMode == WindowPresentationMode::Windowed && NewWidth > 0 && NewHeight > 0)
    {
        RestorableBounds.PixelWidth  = NewWidth;
        RestorableBounds.PixelHeight = NewHeight;
    }
    RefreshCachedExtent();
}

void DesktopPlatformWindow::RequestClose() noexcept
{
    if (GlfwWindowHandle != nullptr)
    {
        glfwSetWindowShouldClose(static_cast<GLFWwindow*>(GlfwWindowHandle), GLFW_TRUE);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                            OUTBOUND OS FEEDBACK APPLICATION
//------------------------------------------------------------------------------------------------------------------------

void DesktopPlatformWindow::ApplyCursorConfinement(Input::CursorConfinementMode Confinement) noexcept
{
    CurrentCursorMode = Confinement;
    if (GlfwWindowHandle == nullptr)
    {
        return;
    }

    auto* Window = static_cast<GLFWwindow*>(GlfwWindowHandle);
    switch (Confinement)
    {
        case Input::CursorConfinementMode::FreeVisible:
            glfwSetInputMode(Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            break;

        case Input::CursorConfinementMode::ConfinedVisible:
            glfwSetInputMode(Window, GLFW_CURSOR, GLFW_CURSOR_CAPTURED);
            break;

        case Input::CursorConfinementMode::LockedRelativeHidden:
            glfwSetInputMode(Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            if (glfwRawMouseMotionSupported() == GLFW_TRUE)
            {
                glfwSetInputMode(Window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
            }
            break;
    }
}

void DesktopPlatformWindow::ApplyHapticFeedback(const Input::HapticFeedbackDirective& Haptic) noexcept
{
    (void)Haptic;
    ++DeliveredHapticCount;
}

void DesktopPlatformWindow::RefreshCachedExtent() noexcept
{
    if (GlfwWindowHandle == nullptr)
    {
        return;
    }

    auto* Window = static_cast<GLFWwindow*>(GlfwWindowHandle);
    int   PosX   = 0;
    int   PosY   = 0;
    int   WinW   = 0;
    int   WinH   = 0;
    int   PixelW  = 0;
    int   PixelH  = 0;
    float ScaleX = 1.0f;
    float ScaleY = 1.0f;

    glfwGetWindowPos(Window, &PosX, &PosY);
    glfwGetWindowSize(Window, &WinW, &WinH);
    glfwGetFramebufferSize(Window, &PixelW, &PixelH);
    glfwGetWindowContentScale(Window, &ScaleX, &ScaleY);

    ActiveExtent.OriginX             = PosX;
    ActiveExtent.OriginY             = PosY;
    ActiveExtent.PixelWidth          = static_cast<uint32_t>(std::max(0, WinW));
    ActiveExtent.PixelHeight         = static_cast<uint32_t>(std::max(0, WinH));
    ActiveExtent.SurfacePixelWidth  = static_cast<uint32_t>(std::max(0, PixelW));
    ActiveExtent.SurfacePixelHeight = static_cast<uint32_t>(std::max(0, PixelH));
    ActiveExtent.DpiScaleFactor      = std::max(ScaleX, ScaleY);
    ActiveExtent.Minimized           = (glfwGetWindowAttrib(Window, GLFW_ICONIFIED) == GLFW_TRUE) || (PixelW == 0 || PixelH == 0);
    ActiveExtent.Maximized           = (glfwGetWindowAttrib(Window, GLFW_MAXIMIZED) == GLFW_TRUE);
    ActiveExtent.Focused             = (glfwGetWindowAttrib(Window, GLFW_FOCUSED) == GLFW_TRUE);
    ActiveExtent.Visible             = (glfwGetWindowAttrib(Window, GLFW_VISIBLE) == GLFW_TRUE);

    GLFWmonitor* Monitor = glfwGetWindowMonitor(Window);
    if (Monitor == nullptr)
    {
        Monitor = SelectTargetMonitor(TargetMonitorSlot);
    }
    if (Monitor != nullptr)
    {
        if (const GLFWvidmode* Mode = glfwGetVideoMode(Monitor))
        {
            ActiveExtent.RefreshRateHz = static_cast<uint32_t>(std::max(1, Mode->refreshRate));
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   INSPECTION QUERIES
//------------------------------------------------------------------------------------------------------------------------

bool DesktopPlatformWindow::CloseRequested() const noexcept
{
    if (GlfwWindowHandle == nullptr)
    {
        return true;
    }
    return glfwWindowShouldClose(static_cast<GLFWwindow*>(GlfwWindowHandle)) == GLFW_TRUE;
}

WindowExtentReading DesktopPlatformWindow::InspectExtent() const noexcept
{
    return ActiveExtent;
}

WindowPlatformCategory DesktopPlatformWindow::PlatformCategory() const noexcept
{
    return SelectedPlatform;
}

WindowPresentationMode DesktopPlatformWindow::ActivePresentationMode() const noexcept
{
    return CurrentMode;
}

WindowAttributeMask DesktopPlatformWindow::ActiveAttributeMask() const noexcept
{
    return EffectiveAttributes;
}

WindowPlacementBounds DesktopPlatformWindow::SavedWindowedBounds() const noexcept
{
    return RestorableBounds;
}

std::string_view DesktopPlatformWindow::TitleText() const noexcept
{
    return CaptionText;
}

Input::CursorConfinementMode DesktopPlatformWindow::AppliedCursorMode() const noexcept
{
    return CurrentCursorMode;
}

uint32_t DesktopPlatformWindow::AppliedHapticPulseCount() const noexcept
{
    return DeliveredHapticCount;
}

void* DesktopPlatformWindow::NativeGlfwHandle() const noexcept
{
    return GlfwWindowHandle;
}

} // namespace Frontier::Windowing

#endif // !defined(__ANDROID__)
