/*============================================================================================================================================
                                                  PLATFORMWINDOWSTUB.CPP
============================================================================================================================================*/
// 🧩 Headless deterministic window implementation supporting splash, windowed, maximized, and fullscreen mode transitions

#include "Engine/Windowing/PlatformWindow.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Windowing
{

namespace
{

constexpr uint32_t VirtualMonitorWidthPx  = 2560; // [px] - Simulated primary display width
constexpr uint32_t VirtualMonitorHeightPx = 1440; // [px] - Simulated primary display height
constexpr uint32_t VirtualWorkAreaHeight  = 1400; // [px] - Simulated work-area height excluding taskbar
constexpr uint32_t DefaultMonitorRefresh  = 60;   // [Hz] - Simulated native refresh rate

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  LIFECYCLE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

bool HeadlessPlatformWindow::Initialize(const WindowConfigurationDescriptor& Descriptor) noexcept
{
    if (Descriptor.PixelWidth == 0 || Descriptor.PixelHeight == 0)
    {
        return false;
    }

    PendingSyntheticExchange.Initialize();
    CaptionText.assign(Descriptor.TitleText.data(), Descriptor.TitleText.size());

    MinWidthLimit  = std::max<uint32_t>(1u, Descriptor.MinimumWidth);
    MinHeightLimit = std::max<uint32_t>(1u, Descriptor.MinimumHeight);
    MaxWidthLimit  = 0;
    MaxHeightLimit = 0;

    WindowedAttributes = Descriptor.AttributeMask;
    if (Descriptor.PresentationMode != WindowPresentationMode::Windowed &&
        Descriptor.PresentationMode != WindowPresentationMode::MaximizedWindowed &&
        !QueryAttributeEnabled(WindowedAttributes, WindowAttributeMask::EnableBorderDecoration))
    {
        WindowedAttributes = Descriptor.AttributeMask |
                             WindowAttributeMask::EnableBorderDecoration |
                             WindowAttributeMask::EnableInteractiveResize;
    }

    RestorableBounds.OriginX     = Descriptor.OriginX;
    RestorableBounds.OriginY     = Descriptor.OriginY;
    RestorableBounds.PixelWidth  = std::max(Descriptor.PixelWidth, MinWidthLimit);
    RestorableBounds.PixelHeight = std::max(Descriptor.PixelHeight, MinHeightLimit);

    if (QueryAttributeEnabled(Descriptor.AttributeMask, WindowAttributeMask::AssignMonitorCenter))
    {
        RestorableBounds.OriginX = static_cast<int32_t>((VirtualMonitorWidthPx > RestorableBounds.PixelWidth)
                                                            ? (VirtualMonitorWidthPx - RestorableBounds.PixelWidth) / 2u
                                                            : 0u);
        RestorableBounds.OriginY = static_cast<int32_t>((VirtualWorkAreaHeight > RestorableBounds.PixelHeight)
                                                            ? (VirtualWorkAreaHeight - RestorableBounds.PixelHeight) / 2u
                                                            : 0u);
    }

    ActiveExtent                = {};
    ActiveExtent.OriginX        = RestorableBounds.OriginX;
    ActiveExtent.OriginY        = RestorableBounds.OriginY;
    ActiveExtent.PixelWidth     = RestorableBounds.PixelWidth;
    ActiveExtent.PixelHeight    = RestorableBounds.PixelHeight;
    ActiveExtent.DpiScaleFactor = (Descriptor.DpiScaleFactor > 0.0f) ? Descriptor.DpiScaleFactor : 1.0f;
    ActiveExtent.RefreshRateHz  = (Descriptor.TargetRefreshRateHz > 0) ? Descriptor.TargetRefreshRateHz : DefaultMonitorRefresh;
    ActiveExtent.Visible        = QueryAttributeEnabled(Descriptor.AttributeMask, WindowAttributeMask::EnableInitialVisibility);
    ActiveExtent.Focused        = ActiveExtent.Visible &&
                           QueryAttributeEnabled(Descriptor.AttributeMask, WindowAttributeMask::AssignInitialFocus);
    ActiveExtent.Minimized      = false;
    ActiveExtent.Maximized      = false;

    CurrentMode          = WindowPresentationMode::Windowed;
    EffectiveAttributes  = Descriptor.AttributeMask;
    CurrentCursorMode    = Input::CursorConfinementMode::FreeVisible;
    DeliveredHapticCount = 0;
    ClosingRequested     = false;
    SurfaceInitialized   = true;

    return ApplyPresentationMode(
        Descriptor.PresentationMode,
        Descriptor.PixelWidth,
        Descriptor.PixelHeight,
        Descriptor.TargetRefreshRateHz);
}

void HeadlessPlatformWindow::PollOperatingEvents(Input::InputExtension& InputCoordinator) noexcept
{
    if (!SurfaceInitialized)
    {
        return;
    }

    struct ForwardToCoordinatorSink
    {
        Input::InputExtension* Coordinator;

        void IntakeSignalPacket(const Input::InputSignalPacket& Packet) noexcept
        {
            (void)Coordinator->IntakeHardwareSignal(Packet);
        }
    };

    ForwardToCoordinatorSink Forwarder{ &InputCoordinator };
    (void)PendingSyntheticExchange.DrainSignals(Forwarder);
    (void)InputCoordinator.FlushOutboundDirectives(*this);
}

void HeadlessPlatformWindow::Finalize() noexcept
{
    PendingSyntheticExchange.Finalize();
    CaptionText.clear();
    ActiveExtent         = {};
    RestorableBounds     = {};
    WindowedAttributes   = WindowAttributeMask::ApplicationPresetMask;
    EffectiveAttributes  = WindowAttributeMask::ApplicationPresetMask;
    CurrentMode          = WindowPresentationMode::Windowed;
    CurrentCursorMode    = Input::CursorConfinementMode::FreeVisible;
    DeliveredHapticCount = 0;
    ClosingRequested     = false;
    SurfaceInitialized   = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      PRESENTATION MODE & ATTRIBUTE MUTATIONS
//------------------------------------------------------------------------------------------------------------------------

bool HeadlessPlatformWindow::ApplyPresentationMode(
    WindowPresentationMode NewMode,
    uint32_t               FullscreenWidth,
    uint32_t               FullscreenHeight,
    uint32_t               RefreshRateHz) noexcept
{
    if (!SurfaceInitialized)
    {
        return false;
    }

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

    CurrentMode = NewMode;

    switch (NewMode)
    {
        case WindowPresentationMode::SplashBorderless:
        {
            const uint32_t TargetW  = (FullscreenWidth > 0) ? FullscreenWidth : RestorableBounds.PixelWidth;
            const uint32_t TargetH  = (FullscreenHeight > 0) ? FullscreenHeight : RestorableBounds.PixelHeight;
            EffectiveAttributes     = ReconcileAttributesForMode(NewMode, EffectiveAttributes);
            ActiveExtent.PixelWidth = TargetW;
            ActiveExtent.PixelHeight = TargetH;
            if (QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::AssignMonitorCenter))
            {
                ActiveExtent.OriginX = static_cast<int32_t>((VirtualMonitorWidthPx > TargetW)
                                                                ? (VirtualMonitorWidthPx - TargetW) / 2u
                                                                : 0u);
                ActiveExtent.OriginY = static_cast<int32_t>((VirtualWorkAreaHeight > TargetH)
                                                                ? (VirtualWorkAreaHeight - TargetH) / 2u
                                                                : 0u);
            }
            ActiveExtent.Maximized     = false;
            ActiveExtent.Minimized     = false;
            ActiveExtent.RefreshRateHz = DefaultMonitorRefresh;
            break;
        }

        case WindowPresentationMode::Windowed:
        {
            EffectiveAttributes      = ReconcileAttributesForMode(NewMode, WindowedAttributes);
            ActiveExtent.OriginX     = RestorableBounds.OriginX;
            ActiveExtent.OriginY     = RestorableBounds.OriginY;
            ActiveExtent.PixelWidth  = std::max(RestorableBounds.PixelWidth, MinWidthLimit);
            ActiveExtent.PixelHeight = std::max(RestorableBounds.PixelHeight, MinHeightLimit);
            if (MaxWidthLimit > 0)
            {
                ActiveExtent.PixelWidth = std::min(ActiveExtent.PixelWidth, MaxWidthLimit);
            }
            if (MaxHeightLimit > 0)
            {
                ActiveExtent.PixelHeight = std::min(ActiveExtent.PixelHeight, MaxHeightLimit);
            }
            ActiveExtent.Maximized     = false;
            ActiveExtent.Minimized     = false;
            ActiveExtent.RefreshRateHz = DefaultMonitorRefresh;
            break;
        }

        case WindowPresentationMode::MaximizedWindowed:
        {
            EffectiveAttributes        = ReconcileAttributesForMode(NewMode, WindowedAttributes);
            ActiveExtent.OriginX       = 0;
            ActiveExtent.OriginY       = 0;
            ActiveExtent.PixelWidth    = VirtualMonitorWidthPx;
            ActiveExtent.PixelHeight   = VirtualWorkAreaHeight;
            ActiveExtent.Maximized     = true;
            ActiveExtent.Minimized     = false;
            ActiveExtent.RefreshRateHz = DefaultMonitorRefresh;
            break;
        }

        case WindowPresentationMode::BorderlessFullscreen:
        {
            EffectiveAttributes        = ReconcileAttributesForMode(NewMode, WindowedAttributes);
            ActiveExtent.OriginX       = 0;
            ActiveExtent.OriginY       = 0;
            ActiveExtent.PixelWidth    = VirtualMonitorWidthPx;
            ActiveExtent.PixelHeight   = VirtualMonitorHeightPx;
            ActiveExtent.Maximized     = false;
            ActiveExtent.Minimized     = false;
            ActiveExtent.RefreshRateHz = DefaultMonitorRefresh;
            break;
        }

        case WindowPresentationMode::ExclusiveFullscreen:
        {
            EffectiveAttributes        = ReconcileAttributesForMode(NewMode, WindowedAttributes);
            ActiveExtent.OriginX       = 0;
            ActiveExtent.OriginY       = 0;
            ActiveExtent.PixelWidth    = (FullscreenWidth > 0) ? FullscreenWidth : VirtualMonitorWidthPx;
            ActiveExtent.PixelHeight   = (FullscreenHeight > 0) ? FullscreenHeight : VirtualMonitorHeightPx;
            ActiveExtent.Maximized     = false;
            ActiveExtent.Minimized     = false;
            ActiveExtent.RefreshRateHz = (RefreshRateHz > 0) ? RefreshRateHz : DefaultMonitorRefresh;
            break;
        }
    }

    RecomputeDrawableExtent();
    return true;
}

bool HeadlessPlatformWindow::ApplyAttributeMask(WindowAttributeMask NewAttributes) noexcept
{
    if (!SurfaceInitialized)
    {
        return false;
    }

    WindowedAttributes   = NewAttributes;
    EffectiveAttributes  = ReconcileAttributesForMode(CurrentMode, NewAttributes);
    ActiveExtent.Visible = QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::EnableInitialVisibility);
    if (ActiveExtent.Visible && QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::AssignInitialFocus))
    {
        ActiveExtent.Focused = true;
    }
    RecomputeDrawableExtent();
    return true;
}

void HeadlessPlatformWindow::ApplyTitle(std::string_view NewTitle) noexcept
{
    CaptionText.assign(NewTitle.data(), NewTitle.size());
}

void HeadlessPlatformWindow::ApplySizeLimits(
    uint32_t MinWidth,
    uint32_t MinHeight,
    uint32_t MaxWidth,
    uint32_t MaxHeight) noexcept
{
    MinWidthLimit  = std::max<uint32_t>(1u, MinWidth);
    MinHeightLimit = std::max<uint32_t>(1u, MinHeight);
    MaxWidthLimit  = MaxWidth;
    MaxHeightLimit = MaxHeight;

    if (CurrentMode == WindowPresentationMode::Windowed)
    {
        ActiveExtent.PixelWidth  = std::max(ActiveExtent.PixelWidth, MinWidthLimit);
        ActiveExtent.PixelHeight = std::max(ActiveExtent.PixelHeight, MinHeightLimit);
        if (MaxWidthLimit > 0)
        {
            ActiveExtent.PixelWidth = std::min(ActiveExtent.PixelWidth, MaxWidthLimit);
        }
        if (MaxHeightLimit > 0)
        {
            ActiveExtent.PixelHeight = std::min(ActiveExtent.PixelHeight, MaxHeightLimit);
        }
        RestorableBounds.PixelWidth  = ActiveExtent.PixelWidth;
        RestorableBounds.PixelHeight = ActiveExtent.PixelHeight;
        RecomputeDrawableExtent();
    }
}

void HeadlessPlatformWindow::MoveSurface(int32_t NewOriginX, int32_t NewOriginY) noexcept
{
    ActiveExtent.OriginX = NewOriginX;
    ActiveExtent.OriginY = NewOriginY;
    if (CurrentMode == WindowPresentationMode::Windowed)
    {
        RestorableBounds.OriginX = NewOriginX;
        RestorableBounds.OriginY = NewOriginY;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                            OUTBOUND OS FEEDBACK APPLICATION
//------------------------------------------------------------------------------------------------------------------------

void HeadlessPlatformWindow::ApplyCursorConfinement(Input::CursorConfinementMode Confinement) noexcept
{
    CurrentCursorMode = Confinement;
}

void HeadlessPlatformWindow::ApplyHapticFeedback(const Input::HapticFeedbackDirective& Haptic) noexcept
{
    (void)Haptic;
    ++DeliveredHapticCount;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SYNTHETIC SURFACE CONTROLS
//------------------------------------------------------------------------------------------------------------------------

void HeadlessPlatformWindow::ResizeSurface(uint32_t NewWidth, uint32_t NewHeight, float NewDpiScale) noexcept
{
    ActiveExtent.PixelWidth     = NewWidth;
    ActiveExtent.PixelHeight    = NewHeight;
    ActiveExtent.DpiScaleFactor = (NewDpiScale > 0.0f) ? NewDpiScale : 1.0f;
    ActiveExtent.Minimized      = (NewWidth == 0 || NewHeight == 0);
    if (!ActiveExtent.Minimized && CurrentMode == WindowPresentationMode::Windowed)
    {
        RestorableBounds.PixelWidth  = NewWidth;
        RestorableBounds.PixelHeight = NewHeight;
    }
    RecomputeDrawableExtent();
}

void HeadlessPlatformWindow::RequestClose() noexcept
{
    ClosingRequested = true;
}

bool HeadlessPlatformWindow::QueueSyntheticSignal(const Input::InputSignalPacket& Packet) noexcept
{
    if (!SurfaceInitialized)
    {
        return false;
    }
    return PendingSyntheticExchange.IntakeSignal(Packet);
}

void HeadlessPlatformWindow::RecomputeDrawableExtent() noexcept
{
    const float ScaleFactor = QueryAttributeEnabled(EffectiveAttributes, WindowAttributeMask::EnableDpiScaling)
                                  ? ActiveExtent.DpiScaleFactor
                                  : 1.0f;
    ActiveExtent.DrawablePixelWidth  = static_cast<uint32_t>(std::lround(static_cast<double>(ActiveExtent.PixelWidth) * ScaleFactor));
    ActiveExtent.DrawablePixelHeight = static_cast<uint32_t>(std::lround(static_cast<double>(ActiveExtent.PixelHeight) * ScaleFactor));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   INSPECTION QUERIES
//------------------------------------------------------------------------------------------------------------------------

bool HeadlessPlatformWindow::CloseRequested() const noexcept
{
    return ClosingRequested;
}

WindowExtentReading HeadlessPlatformWindow::InspectExtent() const noexcept
{
    return ActiveExtent;
}

WindowPlatformCategory HeadlessPlatformWindow::PlatformCategory() const noexcept
{
    return WindowPlatformCategory::HeadlessStub;
}

WindowPresentationMode HeadlessPlatformWindow::ActivePresentationMode() const noexcept
{
    return CurrentMode;
}

WindowAttributeMask HeadlessPlatformWindow::ActiveAttributeMask() const noexcept
{
    return EffectiveAttributes;
}

WindowPlacementBounds HeadlessPlatformWindow::SavedWindowedBounds() const noexcept
{
    return RestorableBounds;
}

std::string_view HeadlessPlatformWindow::TitleText() const noexcept
{
    return CaptionText;
}

Input::CursorConfinementMode HeadlessPlatformWindow::AppliedCursorMode() const noexcept
{
    return CurrentCursorMode;
}

uint32_t HeadlessPlatformWindow::AppliedHapticPulseCount() const noexcept
{
    return DeliveredHapticCount;
}

} // namespace Frontier::Windowing
