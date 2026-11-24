/*============================================================================================================================================
                                                     INSPECTORSECTIONPRIMITIVES.CPP
============================================================================================================================================*/
// 🧩 Implementation of generic 1:1 HTML-parity Inspector controls, retained RGBA32 preview surfaces, curves, tags & notes

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"

#include "Engine/Components/Controls/ControlTooltip.h"
#include "Engine/Components/Controls/ScalarSlider.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace Frontier
{

namespace
{

std::mutex                                    GlobalGpuBakeHookMutex  {};
InspectorPreviewSurface::GpuBakeHookFn        GlobalGpuBakeHook       {};
InspectorPreviewSurface::GpuDirectBakeHookFn  GlobalGpuDirectHook     {};
InspectorPreviewSurface::GpuDirectReleaseHookFn GlobalGpuReleaseHook  {};
std::atomic<uint64_t>                         GlobalNextSurfaceId     { 1ull };

struct InspectorCaptureFrameScope
{
    RetainedCanvasCache* ActiveCache          = nullptr;
    Rectangle2D          ScrollClipLogical    {};
    Point2D              SurfaceOriginPhys    {};
    float                ScaleFactor          = 1.0f;
    bool                 ScrollClipActive     = false;
    bool                 InteractiveDragNow   = false;
};

InspectorCaptureFrameScope& AccessCaptureScope() noexcept
{
    static InspectorCaptureFrameScope Scope {};
    return Scope;
}

[[nodiscard]] uint64_t QueryMonotonicNowNs() noexcept
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           ON-DEMAND RETAINED PREVIEW SURFACE
//------------------------------------------------------------------------------------------------------------------------

InspectorPreviewSurface::~InspectorPreviewSurface() noexcept
{
    if (SurfaceUniqueId != 0ull)
    {
        GpuDirectReleaseHookFn ReleaseHook {};
        {
            std::lock_guard<std::mutex> Lock(GlobalGpuBakeHookMutex);
            ReleaseHook = GlobalGpuReleaseHook;
        }
        if (ReleaseHook)
        {
            ReleaseHook(SurfaceUniqueId);
        }
        DirectGpuTextureHandle = 0ull;
    }
    ResidentTexture.DestroyPixels();
    ResidentTexture.SetStatus(ImTextureStatus_Destroyed);
}

void InspectorPreviewSurface::AssignGpuBakeHook(GpuBakeHookFn Hook)
{
    std::lock_guard<std::mutex> Lock(GlobalGpuBakeHookMutex);
    GlobalGpuBakeHook = std::move(Hook);
}

void InspectorPreviewSurface::ClearGpuBakeHook() noexcept
{
    std::lock_guard<std::mutex> Lock(GlobalGpuBakeHookMutex);
    GlobalGpuBakeHook = {};
}

bool InspectorPreviewSurface::QueryGpuBakeHookAttached() noexcept
{
    std::lock_guard<std::mutex> Lock(GlobalGpuBakeHookMutex);
    return static_cast<bool>(GlobalGpuBakeHook);
}

void InspectorPreviewSurface::AssignGpuDirectBakeHook(GpuDirectBakeHookFn Hook, GpuDirectReleaseHookFn ReleaseHook)
{
    std::lock_guard<std::mutex> Lock(GlobalGpuBakeHookMutex);
    GlobalGpuDirectHook  = std::move(Hook);
    GlobalGpuReleaseHook = std::move(ReleaseHook);
}

void InspectorPreviewSurface::ClearGpuDirectBakeHook() noexcept
{
    std::lock_guard<std::mutex> Lock(GlobalGpuBakeHookMutex);
    GlobalGpuDirectHook  = {};
    GlobalGpuReleaseHook = {};
}

bool InspectorPreviewSurface::QueryGpuDirectBakeHookAttached() noexcept
{
    std::lock_guard<std::mutex> Lock(GlobalGpuBakeHookMutex);
    return static_cast<bool>(GlobalGpuDirectHook);
}

void InspectorPreviewSurface::UploadStagingPixels(
    uint64_t RevisionSignature,
    uint32_t TargetWidthPx,
    uint32_t TargetHeightPx,
    bool     SizeMismatch,
    bool     BakedOnGpu)
{
    if (SizeMismatch)
    {
        ResidentTexture.DestroyPixels();
        ResidentTexture.SetStatus(ImTextureStatus_Destroyed);
        ResidentTexture.Create(ImTextureFormat_RGBA32, static_cast<int>(TargetWidthPx), static_cast<int>(TargetHeightPx));
        WidthPx  = TargetWidthPx;
        HeightPx = TargetHeightPx;
    }

    if (ResidentTexture.Pixels != nullptr && !StagingPixels.empty())
    {
        std::memcpy(ResidentTexture.Pixels, StagingPixels.data(), StagingPixels.size());
        if (!SizeMismatch && ResidentTexture.Status == ImTextureStatus_OK)
        {
            ResidentTexture.UpdateRect = { 0u, 0u, static_cast<unsigned short>(TargetWidthPx), static_cast<unsigned short>(TargetHeightPx) };
            ResidentTexture.SetStatus(ImTextureStatus_WantUpdates);
        }
    }
    RetainedSignature = RevisionSignature;
    LastBakeTimeNs    = QueryMonotonicNowNs();
    ++UploadCounter;
    if (BakedOnGpu) ++GpuUploadCounter;
    else            ++CpuUploadCounter;
}

void InspectorPreviewSurface::EnsureUpdated(
    uint64_t                                          RevisionSignature,
    uint32_t                                          TargetWidthPx,
    uint32_t                                          TargetHeightPx,
    const std::function<void(std::vector<uint8_t>&)>& RasterCallback)
{
    if (TargetWidthPx == 0u || TargetHeightPx == 0u || !RasterCallback)
    {
        return;
    }

    const bool SizeMismatch = (WidthPx != TargetWidthPx) || (HeightPx != TargetHeightPx) || (ResidentTexture.Pixels == nullptr);
    if (!SizeMismatch && RetainedSignature == RevisionSignature)
    {
        return;
    }

    // Drag-cadence throttle (30 Hz max while interactively scrubbing sliders; final value bakes immediately on release).
    if (!SizeMismatch && InspectorPrimitives::QueryInteractiveDragActive())
    {
        constexpr uint64_t DragMinIntervalNs = 33'000'000ull;
        if ((QueryMonotonicNowNs() - LastBakeTimeNs) < DragMinIntervalNs)
        {
            return;
        }
    }

    StagingPixels.resize(static_cast<std::size_t>(TargetWidthPx) * TargetHeightPx * 4u);
    RasterCallback(StagingPixels);
    UploadStagingPixels(RevisionSignature, TargetWidthPx, TargetHeightPx, SizeMismatch, false);
}

void InspectorPreviewSurface::EnsureUpdatedGpu(
    uint64_t                                          RevisionSignature,
    uint32_t                                          TargetWidthPx,
    uint32_t                                          TargetHeightPx,
    const InspectorGpuBakeSpec&                       GpuSpec,
    const std::function<void(std::vector<uint8_t>&)>& CpuFallbackCallback)
{
    if (TargetWidthPx == 0u || TargetHeightPx == 0u)
    {
        return;
    }

    const bool HasAnyResident = (DirectGpuTextureHandle != 0ull) || (ResidentTexture.Pixels != nullptr);
    const bool SizeMismatch   = (WidthPx != TargetWidthPx) || (HeightPx != TargetHeightPx) || !HasAnyResident;
    if (!SizeMismatch && RetainedSignature == RevisionSignature)
    {
        return;
    }

    if (!SizeMismatch && InspectorPrimitives::QueryInteractiveDragActive())
    {
        constexpr uint64_t DragMinIntervalNs = 33'000'000ull;
        if ((QueryMonotonicNowNs() - LastBakeTimeNs) < DragMinIntervalNs)
        {
            return;
        }
    }

    // 1. Zero-Copy Pure-GPU Direct ImGui Image Path (0 CPU readback, 0 memcpy, 0 ImGui texture re-upload!)
    GpuDirectBakeHookFn DirectHook {};
    GpuBakeHookFn       GpuHook {};
    {
        std::lock_guard<std::mutex> Lock(GlobalGpuBakeHookMutex);
        DirectHook = GlobalGpuDirectHook;
        GpuHook    = GlobalGpuBakeHook;
    }
    if (DirectHook)
    {
        if (SurfaceUniqueId == 0ull)
        {
            SurfaceUniqueId = GlobalNextSurfaceId.fetch_add(1ull, std::memory_order_relaxed);
        }
        uint64_t NativeHandle = 0ull;
        if (DirectHook(SurfaceUniqueId, GpuSpec, TargetWidthPx, TargetHeightPx, NativeHandle) && NativeHandle != 0ull)
        {
            DirectGpuTextureHandle = NativeHandle;
            WidthPx                = TargetWidthPx;
            HeightPx               = TargetHeightPx;
            RetainedSignature      = RevisionSignature;
            LastBakeTimeNs         = QueryMonotonicNowNs();
            ++UploadCounter;
            ++GpuUploadCounter;
            ++DirectGpuUploadCounter;
            return;
        }
    }

    // 2. Staging Pure-GPU Compute Path (when Vulkan device is attached without external ImGui presenter)
    StagingPixels.resize(static_cast<std::size_t>(TargetWidthPx) * TargetHeightPx * 4u);
    if (GpuHook && GpuHook(GpuSpec, TargetWidthPx, TargetHeightPx, StagingPixels))
    {
        DirectGpuTextureHandle = 0ull;
        UploadStagingPixels(RevisionSignature, TargetWidthPx, TargetHeightPx, SizeMismatch, true);
        return;
    }

    // 3. Deterministic CPU Fallback (headless validation tests without Vulkan GPU)
    if (CpuFallbackCallback)
    {
        DirectGpuTextureHandle = 0ull;
        CpuFallbackCallback(StagingPixels);
        UploadStagingPixels(RevisionSignature, TargetWidthPx, TargetHeightPx, SizeMismatch, false);
    }
}

void InspectorPreviewSurface::Present(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    float                   CornerRadius,
    ColorRgba               Tint,
    InspectorPreviewFitMode FitMode,
    ColorRgba               ChamberFill) noexcept
{
    if (!InspectorPrimitives::QueryVisibleInScrollClip(Bounds))
    {
        return;
    }

    const float SafeRadius = std::min(CornerRadius, std::min(10.0f, std::min(Bounds.Width, Bounds.Height) * 0.5f));
    if (!QueryResident())
    {
        if (ChamberFill.Alpha > 0u)
        {
            Surface.FillRectangle(Bounds, ChamberFill, SafeRadius);
        }
        return;
    }

    Rectangle2D DrawRect = Bounds;
    Point2D     UvMin { 0.0f, 0.0f };
    Point2D     UvMax { 1.0f, 1.0f };
    float       DrawRadius = SafeRadius;

    if (FitMode == InspectorPreviewFitMode::Contain)
    {
        DrawRect = InspectorPrimitives::ComputeAspectFitRect(Bounds, WidthPx, HeightPx);
        const bool HasLetterbox = (std::fabs(DrawRect.Width - Bounds.Width) > 1.0f ||
                                   std::fabs(DrawRect.Height - Bounds.Height) > 1.0f);
        if (HasLetterbox && SafeRadius > 0.25f && ChamberFill.Alpha > 0u)
        {
            Surface.FillRectangle(Bounds, ChamberFill, SafeRadius);
            Surface.StrokeRectangle(Bounds, ColorRgba{ 255u, 255u, 255u, 14u }, SafeRadius, 1.0f);
            DrawRadius = std::min(SafeRadius, std::min(DrawRect.Width, DrawRect.Height) * 0.5f);
        }
    }
    else if (FitMode == InspectorPreviewFitMode::Cover)
    {
        const float TexAspect = static_cast<float>(std::max(1u, WidthPx)) / static_cast<float>(std::max(1u, HeightPx));
        const float BoxAspect = Bounds.Width / std::max(1.0f, Bounds.Height);
        if (BoxAspect > TexAspect + 1.0e-3f)
        {
            const float VisibleV = TexAspect / BoxAspect;
            const float PadV     = (1.0f - VisibleV) * 0.5f;
            UvMin.Y = PadV;
            UvMax.Y = 1.0f - PadV;
        }
        else if (TexAspect > BoxAspect + 1.0e-3f)
        {
            const float VisibleU = BoxAspect / TexAspect;
            const float PadU     = (1.0f - VisibleU) * 0.5f;
            UvMin.X = PadU;
            UvMax.X = 1.0f - PadU;
        }
    }

    if (DirectGpuTextureHandle != 0ull)
    {
        Surface.FillTexturedRectangle(DrawRect, nullptr, DirectGpuTextureHandle, Tint, UvMin, UvMax, DrawRadius);
        return;
    }
    Surface.FillTexturedRectangle(DrawRect, &ResidentTexture, UvMin, UvMax, DrawRadius, Tint);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  INSPECTOR PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

namespace InspectorPrimitives
{

Rectangle2D ComputeAspectFitRect(Rectangle2D Bounds, uint32_t TextureWidthPx, uint32_t TextureHeightPx) noexcept
{
    if (Bounds.Width <= 1.0f || Bounds.Height <= 1.0f || TextureWidthPx == 0u || TextureHeightPx == 0u)
    {
        return Bounds;
    }
    const float TexAspect = static_cast<float>(TextureWidthPx) / static_cast<float>(TextureHeightPx);
    const float BoxAspect = Bounds.Width / Bounds.Height;
    Rectangle2D Fit = Bounds;
    if (BoxAspect > TexAspect)
    {
        Fit.Width = Bounds.Height * TexAspect;
        Fit.X     = Bounds.X + (Bounds.Width - Fit.Width) * 0.5f;
    }
    else
    {
        Fit.Height = Bounds.Width / TexAspect;
        Fit.Y      = Bounds.Y + (Bounds.Height - Fit.Height) * 0.5f;
    }
    return Fit;
}

uint64_t MixRevisionU64(uint64_t Seed, uint64_t Bits) noexcept
{
    Seed ^= Bits + 0x9E3779B97F4A7C15ull + (Seed << 6u) + (Seed >> 2u);
    return Seed;
}

uint64_t MixRevisionFloat(uint64_t Seed, float Reading) noexcept
{
    const int64_t Quantized = static_cast<int64_t>(std::llround(static_cast<double>(Reading) * 1000.0));
    return MixRevisionU64(Seed, static_cast<uint64_t>(Quantized));
}

ColorRgba EncodeLinearRgbToSrgb8(const Mathematics::LinearRGB& Linear) noexcept
{
    auto Encode = [](float Channel) noexcept -> uint8_t
    {
        const float Clamped = std::clamp(Channel, 0.0f, 1.0f);
        const float Srgb    = (Clamped <= 0.0031308f) ? (Clamped * 12.92f) : (1.055f * std::pow(Clamped, 1.0f / 2.4f) - 0.055f);
        return static_cast<uint8_t>(std::clamp(std::lround(Srgb * 255.0f), 0l, 255l));
    };
    return ColorRgba{ Encode(Linear.R), Encode(Linear.G), Encode(Linear.B), 255u };
}

void ConstructSwitchSymbol(
    PixelSpace&           Surface,
    InspectorSwitchSymbol Symbol,
    Point2D               Center,
    float                 Diameter,
    ColorRgba             Ink,
    ColorRgba             WellFill) noexcept
{
    const float R = Diameter * 0.5f;
    switch (Symbol)
    {
        case InspectorSwitchSymbol::Eye:
        {
            const std::array<Point2D, 7u> Upper = {{
                { Center.X - R * 0.85f, Center.Y },
                { Center.X - R * 0.45f, Center.Y - R * 0.42f },
                { Center.X,             Center.Y - R * 0.54f },
                { Center.X + R * 0.45f, Center.Y - R * 0.42f },
                { Center.X + R * 0.85f, Center.Y },
                { Center.X + R * 0.45f, Center.Y + R * 0.42f },
                { Center.X - R * 0.45f, Center.Y + R * 0.42f },
            }};
            Surface.StrokePolyline(Upper, Ink, 1.45f, true);
            Surface.FillCircle(Center, R * 0.28f, Ink);
            break;
        }
        case InspectorSwitchSymbol::Sun:
            Surface.FillCircle(Center, R * 0.34f, Ink);
            for (int32_t Ray = 0; Ray < 8; ++Ray)
            {
                const float Angle = static_cast<float>(Ray) * 0.78539816f;
                Surface.StrokeLine(
                    { Center.X + std::cos(Angle) * R * 0.54f, Center.Y + std::sin(Angle) * R * 0.54f },
                    { Center.X + std::cos(Angle) * R * 0.84f, Center.Y + std::sin(Angle) * R * 0.84f },
                    Ink, 1.45f);
            }
            break;
        case InspectorSwitchSymbol::Clock:
            Surface.StrokeCircle(Center, R * 0.72f, Ink, 1.5f);
            Surface.StrokeLine(Center, { Center.X, Center.Y - R * 0.44f }, Ink, 1.5f);
            Surface.StrokeLine(Center, { Center.X + R * 0.36f, Center.Y + R * 0.14f }, Ink, 1.5f);
            break;
        case InspectorSwitchSymbol::Cycle:
            Surface.StrokeCircle(Center, R * 0.66f, Ink, 1.5f);
            Surface.FillRectangle({ Center.X - R * 0.12f, Center.Y - R * 0.82f, R * 0.28f, R * 0.36f }, WellFill);
            Surface.StrokeLine({ Center.X + R * 0.14f, Center.Y - R * 0.68f }, { Center.X + R * 0.46f, Center.Y - R * 0.52f }, Ink, 1.5f);
            Surface.StrokeLine({ Center.X + R * 0.14f, Center.Y - R * 0.68f }, { Center.X + R * 0.34f, Center.Y - R * 0.36f }, Ink, 1.5f);
            break;
        case InspectorSwitchSymbol::Compass:
            Surface.StrokeCircle(Center, R * 0.74f, Ink, 1.45f);
            {
                const std::array<Point2D, 4u> Needle = {{
                    { Center.X + R * 0.36f, Center.Y - R * 0.36f },
                    { Center.X + R * 0.12f, Center.Y + R * 0.12f },
                    { Center.X - R * 0.36f, Center.Y + R * 0.36f },
                    { Center.X - R * 0.12f, Center.Y - R * 0.12f },
                }};
                Surface.FillPolygon(Needle, Ink);
            }
            break;
        case InspectorSwitchSymbol::Orbit:
            Surface.StrokeCircle(Center, R * 0.68f, Ink, 1.45f);
            Surface.FillCircle({ Center.X + R * 0.48f, Center.Y - R * 0.48f }, R * 0.22f, Ink);
            Surface.FillCircle(Center, R * 0.24f, Ink);
            break;
        case InspectorSwitchSymbol::LimbLine:
            Surface.StrokeLine({ Center.X - R * 0.80f, Center.Y + R * 0.26f }, { Center.X + R * 0.80f, Center.Y + R * 0.26f }, Ink, 1.5f);
            Surface.FillCircle({ Center.X, Center.Y + R * 0.16f }, R * 0.34f, Ink);
            Surface.FillRectangle({ Center.X - R * 0.50f, Center.Y + R * 0.32f, R * 1.0f, R * 0.45f }, WellFill);
            for (int32_t Ray = -2; Ray <= 2; ++Ray)
            {
                const float A = -1.5707963f + static_cast<float>(Ray) * 0.48f;
                Surface.StrokeLine(
                    { Center.X + std::cos(A) * R * 0.48f, Center.Y + R * 0.16f + std::sin(A) * R * 0.48f },
                    { Center.X + std::cos(A) * R * 0.74f, Center.Y + R * 0.16f + std::sin(A) * R * 0.74f },
                    Ink, 1.35f);
            }
            break;
        case InspectorSwitchSymbol::Cloud:
            Surface.FillCircle({ Center.X - R * 0.28f, Center.Y + R * 0.14f }, R * 0.32f, Ink);
            Surface.FillCircle({ Center.X + R * 0.26f, Center.Y + R * 0.14f }, R * 0.28f, Ink);
            Surface.FillCircle({ Center.X - R * 0.02f, Center.Y - R * 0.12f }, R * 0.38f, Ink);
            Surface.FillRectangle({ Center.X - R * 0.34f, Center.Y + R * 0.08f, R * 0.68f, R * 0.32f }, Ink, R * 0.12f);
            break;
        case InspectorSwitchSymbol::Stars:
        case InspectorSwitchSymbol::Twinkle:
        {
            const std::array<Point2D, 8u> Spark = {{
                { Center.X,             Center.Y - R * 0.76f },
                { Center.X + R * 0.20f, Center.Y - R * 0.20f },
                { Center.X + R * 0.76f, Center.Y },
                { Center.X + R * 0.20f, Center.Y + R * 0.20f },
                { Center.X,             Center.Y + R * 0.76f },
                { Center.X - R * 0.20f, Center.Y + R * 0.20f },
                { Center.X - R * 0.76f, Center.Y },
                { Center.X - R * 0.20f, Center.Y - R * 0.20f },
            }};
            Surface.FillPolygon(Spark, Ink);
            if (Symbol == InspectorSwitchSymbol::Stars)
            {
                Surface.FillCircle({ Center.X + R * 0.56f, Center.Y - R * 0.54f }, R * 0.14f, Ink);
            }
            break;
        }
        case InspectorSwitchSymbol::Galaxy:
            Surface.StrokeCircle(Center, R * 0.68f, Ink, 1.35f);
            Surface.StrokeCircle(Center, R * 0.36f, Ink, 1.35f);
            Surface.FillCircle(Center, R * 0.16f, Ink);
            break;
        case InspectorSwitchSymbol::Zodiac:
            Surface.StrokeLine({ Center.X - R * 0.72f, Center.Y + R * 0.38f }, { Center.X + R * 0.72f, Center.Y - R * 0.38f }, Ink, 1.6f);
            Surface.FillCircle(Center, R * 0.26f, Ink);
            break;
        case InspectorSwitchSymbol::Wind:
            Surface.StrokeLine({ Center.X - R * 0.74f, Center.Y - R * 0.26f }, { Center.X + R * 0.44f, Center.Y - R * 0.26f }, Ink, 1.5f);
            Surface.StrokeLine({ Center.X - R * 0.54f, Center.Y + R * 0.04f }, { Center.X + R * 0.74f, Center.Y + R * 0.04f }, Ink, 1.5f);
            Surface.StrokeLine({ Center.X - R * 0.74f, Center.Y + R * 0.34f }, { Center.X + R * 0.24f, Center.Y + R * 0.34f }, Ink, 1.5f);
            break;
        case InspectorSwitchSymbol::Fog:
            Surface.StrokeLine({ Center.X - R * 0.72f, Center.Y - R * 0.28f }, { Center.X + R * 0.72f, Center.Y - R * 0.28f }, Ink, 1.5f);
            Surface.StrokeLine({ Center.X - R * 0.56f, Center.Y + R * 0.04f }, { Center.X + R * 0.56f, Center.Y + R * 0.04f }, Ink, 1.5f);
            Surface.StrokeLine({ Center.X - R * 0.72f, Center.Y + R * 0.36f }, { Center.X + R * 0.72f, Center.Y + R * 0.36f }, Ink, 1.5f);
            break;
        case InspectorSwitchSymbol::LightBulb:
            Surface.StrokeCircle({ Center.X, Center.Y - R * 0.14f }, R * 0.46f, Ink, 1.5f);
            Surface.StrokeLine({ Center.X - R * 0.24f, Center.Y + R * 0.48f }, { Center.X + R * 0.24f, Center.Y + R * 0.48f }, Ink, 1.5f);
            Surface.StrokeLine({ Center.X - R * 0.18f, Center.Y + R * 0.68f }, { Center.X + R * 0.18f, Center.Y + R * 0.68f }, Ink, 1.5f);
            break;
        case InspectorSwitchSymbol::CameraLens:
            Surface.StrokeRectangle({ Center.X - R * 0.72f, Center.Y - R * 0.46f, R * 1.44f, R * 0.96f }, Ink, 3.0f, 1.45f);
            Surface.StrokeCircle({ Center.X, Center.Y + R * 0.02f }, R * 0.28f, Ink, 1.45f);
            break;
        case InspectorSwitchSymbol::Cube:
            Surface.StrokeRectangle({ Center.X - R * 0.54f, Center.Y - R * 0.54f, R * 1.08f, R * 1.08f }, Ink, 2.0f, 1.45f);
            Surface.StrokeLine({ Center.X - R * 0.54f, Center.Y - R * 0.14f }, { Center.X + R * 0.54f, Center.Y - R * 0.14f }, Ink, 1.2f);
            break;
        case InspectorSwitchSymbol::Shadow:
            Surface.FillCircle({ Center.X - R * 0.16f, Center.Y - R * 0.16f }, R * 0.42f, Ink);
            Surface.StrokeCircle({ Center.X + R * 0.22f, Center.Y + R * 0.22f }, R * 0.42f, Ink, 1.35f);
            break;
        case InspectorSwitchSymbol::Physics:
            Surface.StrokeCircle(Center, R * 0.66f, Ink, 1.45f);
            Surface.FillCircle({ Center.X + R * 0.24f, Center.Y - R * 0.18f }, R * 0.22f, Ink);
            break;
        case InspectorSwitchSymbol::PostFx:
            Surface.StrokeCircle(Center, R * 0.72f, Ink, 1.45f);
            Surface.StrokeCircle(Center, R * 0.34f, Ink, 1.45f);
            break;
    }
}

float ConstructInspectorHeader(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       InnerWidth,
    const char* Breadcrumb,
    const char* Title,
    const char* Subtitle) noexcept
{
    // Exact Chromium .inspector-heading measurements (height = 110px):
    // .breadcrumbs at TopY + 1 (9.5px, #919191), h1 at TopY + 42 (29px, #f0f0f0), .eyebrow at TopY + 80 (11.5px, #919191)
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { LeftX, TopY + 1.0f }, InnerWidth, 9.5f, InspectorMetrics::MutedInk, Breadcrumb);

    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { LeftX, TopY + 42.0f }, InnerWidth, 29.0f, ColorRgba{ 240u, 240u, 240u, 255u }, Title);

    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { LeftX, TopY + 80.0f }, InnerWidth, 11.5f, InspectorMetrics::MutedInk, Subtitle);

    return TopY + 110.0f;
}

float ConstructPageStrip(
    PixelSpace&                  Surface,
    float                        LeftX,
    float                        TopY,
    float                        InnerWidth,
    std::span<const char* const> PageLabels,
    uint32_t&                    InOutActivePage,
    const PaneInputReading&      Input,
    bool&                        OutPageSwitched) noexcept
{
    OutPageSwitched = false;
    const uint32_t PageCount = static_cast<uint32_t>(PageLabels.size());
    if (PageCount == 0u)
    {
        return TopY;
    }
    InOutActivePage = std::min(InOutActivePage, PageCount - 1u);

    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const float   StripH = InspectorMetrics::PageStripHeight; // 34px
    RegisterInteractiveRect({ LeftX, TopY, InnerWidth, StripH });
    const float   StepW  = 28.0f;
    const float   StepH  = 28.0f;
    const float   StepR  = StepH * 0.5f; // Full circular pill step buttons
    const float   StepY  = TopY + (StripH - StepH) * 0.5f;

    // 1. Left "<" circular pill button
    const Rectangle2D PrevBtn { LeftX, StepY, StepW, StepH };
    const bool        PrevDisabled = (InOutActivePage == 0u);
    const bool        PrevHov      = !PrevDisabled && PrevBtn.Contains(Cursor);
    Surface.FillRectangle(
        PrevBtn,
        PrevHov ? ColorRgba{ 41u, 41u, 45u, 255u } : InspectorMetrics::PageStepFill.ScaledAlpha(PrevDisabled ? 0.45f : 1.0f),
        StepR);
    Surface.StrokeRectangle(PrevBtn, ColorRgba{ 255u, 255u, 255u, 18u }, StepR, 1.0f);
    {
        const ColorRgba ChevInk = PrevDisabled ? ColorRgba{ 95u, 98u, 104u, 255u } : ColorRgba{ 201u, 204u, 209u, 255u };
        const Point2D   C       = PrevBtn.Center();
        const std::array<Point2D, 3u> Chev = {{
            { C.X + 2.2f, C.Y - 4.0f },
            { C.X - 2.0f, C.Y },
            { C.X + 2.2f, C.Y + 4.0f },
        }};
        Surface.StrokePolyline(Chev, ChevInk, 1.45f, false);
    }
    if (PrevHov && Input.PrimaryPointer.Pressed)
    {
        --InOutActivePage;
        OutPageSwitched = true;
    }

    // 2. Right counter ("1 / N") & ">" circular pill button
    char CounterBuf[24]{};
    std::snprintf(CounterBuf, sizeof(CounterBuf), "%u / %u", InOutActivePage + 1u, PageCount);
    const float CounterW  = std::max(28.0f, WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, CounterBuf));
    const float RightEdge = LeftX + InnerWidth;
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { RightEdge - CounterW, TopY + 10.5f }, 10.5f, ColorRgba{ 138u, 142u, 149u, 255u }, CounterBuf);

    const Rectangle2D NextBtn { RightEdge - CounterW - 6.0f - StepW, StepY, StepW, StepH };
    const bool        NextDisabled = (InOutActivePage + 1u >= PageCount);
    const bool        NextHov      = !NextDisabled && NextBtn.Contains(Cursor);
    Surface.FillRectangle(
        NextBtn,
        NextHov ? ColorRgba{ 41u, 41u, 45u, 255u } : InspectorMetrics::PageStepFill.ScaledAlpha(NextDisabled ? 0.45f : 1.0f),
        StepR);
    Surface.StrokeRectangle(NextBtn, ColorRgba{ 255u, 255u, 255u, 18u }, StepR, 1.0f);
    {
        const ColorRgba ChevInk = NextDisabled ? ColorRgba{ 95u, 98u, 104u, 255u } : ColorRgba{ 201u, 204u, 209u, 255u };
        const Point2D   C       = NextBtn.Center();
        const std::array<Point2D, 3u> Chev = {{
            { C.X - 2.2f, C.Y - 4.0f },
            { C.X + 2.0f, C.Y },
            { C.X - 2.2f, C.Y + 4.0f },
        }};
        Surface.StrokePolyline(Chev, ChevInk, 1.45f, false);
    }
    if (NextHov && Input.PrimaryPointer.Pressed)
    {
        ++InOutActivePage;
        OutPageSwitched = true;
    }

    // 3. Center tab rail — full pill (radius = StripH * 0.5)
    const float       RailX = PrevBtn.Right() + 6.0f;
    const float       RailW = std::max(56.0f, NextBtn.X - 6.0f - RailX);
    const float       RailR = StripH * 0.5f;
    const Rectangle2D RailRect { RailX, TopY, RailW, StripH };
    Surface.FillRectangle(RailRect, InspectorMetrics::PageRailFill, RailR);
    Surface.StrokeRectangle(RailRect, ColorRgba{ 255u, 255u, 255u, 18u }, RailR, 1.0f);

    const Rectangle2D RailInner { RailRect.X + 3.0f, RailRect.Y + 3.0f, std::max(24.0f, RailRect.Width - 6.0f), StripH - 6.0f };
    Surface.PushClip(RailInner);

    // Compute exact horizontal positions of all tabs, then scroll to center active tab inside RailInner (1:1 HTML .pc-rail)
    std::array<float, 16u> TabWidths{};
    std::array<float, 16u> TabLefts{};
    const uint32_t SafeCount = std::min(PageCount, 16u);
    float TotalTrackW = 0.0f;
    for (uint32_t Idx = 0u; Idx < SafeCount; ++Idx)
    {
        const float LabelW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 12.0f, PageLabels[Idx]);
        const float TabW   = std::min(134.0f, 4.0f + 18.0f + 8.0f + LabelW + 16.0f);
        TabLefts[Idx]      = TotalTrackW;
        TabWidths[Idx]     = TabW;
        TotalTrackW       += TabW + 2.0f;
    }
    if (TotalTrackW > 2.0f) TotalTrackW -= 2.0f;

    const float MaxRailScroll = std::max(0.0f, TotalTrackW - RailInner.Width);
    const float ActiveMid     = TabLefts[InOutActivePage] + TabWidths[InOutActivePage] * 0.5f;
    const float RailScrollX   = std::clamp(ActiveMid - RailInner.Width * 0.5f, 0.0f, MaxRailScroll);

    for (uint32_t Idx = 0u; Idx < SafeCount; ++Idx)
    {
        const float TabX = RailInner.X + TabLefts[Idx] - RailScrollX;
        const float TabW = TabWidths[Idx];
        if (TabX + TabW < RailInner.X - 2.0f || TabX > RailInner.Right() + 2.0f) continue;

        const bool Open = (Idx == InOutActivePage);
        const Rectangle2D TabRect { TabX, RailInner.Y, TabW, RailInner.Height };
        const float TabPillR = TabRect.Height * 0.5f;
        const bool Hovered = RailInner.Contains(Cursor) && TabRect.Contains(Cursor);

        if (Open)
        {
            Surface.FillRectangle(TabRect, InspectorMetrics::PageTabOpenFill, TabPillR);
            Surface.StrokeRectangle(TabRect, ColorRgba{ 255u, 255u, 255u, 20u }, TabPillR, 1.0f);
        }
        else if (Hovered)
        {
            Surface.FillRectangle(TabRect, ColorRgba{ 255u, 255u, 255u, 12u }, TabPillR);
        }

        // 18x18 circular number indicator
        const Point2D CircleCenter { TabRect.X + 13.0f, TabRect.Center().Y };
        Surface.FillCircle(CircleCenter, 9.0f, Open ? InspectorMetrics::PageNumOpenFill : InspectorMetrics::PageNumClosedFill);
        char NumBuf[16]{};
        std::snprintf(NumBuf, sizeof(NumBuf), "%u", Idx + 1u);
        const float NumW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::SemiBold, 10.5f, NumBuf);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::SemiBold,
            { CircleCenter.X - NumW * 0.5f, CircleCenter.Y - 5.5f },
            10.5f,
            Open ? ColorRgba{ 20u, 20u, 22u, 255u } : ColorRgba{ 201u, 204u, 209u, 255u },
            NumBuf);

        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { TabRect.X + 28.0f, TabRect.Y + 7.5f },
            std::max(10.0f, TabRect.Width - 36.0f),
            12.0f,
            Open ? ColorRgba{ 244u, 244u, 245u, 255u } : ColorRgba{ 154u, 158u, 165u, 255u },
            PageLabels[Idx]);

        if (Hovered && Input.PrimaryPointer.Pressed && !Open)
        {
            InOutActivePage = Idx;
            OutPageSwitched = true;
        }
    }

    // Soft edge fade masks when tabs overflow left or right of the capsule rail
    if (RailScrollX > 1.0f)
    {
        for (int32_t Band = 0; Band < 5; ++Band)
        {
            const float BandW = 3.5f;
            const float BandX = RailInner.X + static_cast<float>(Band) * BandW;
            const uint8_t A   = static_cast<uint8_t>(210u - Band * 42u);
            Surface.FillRectangle({ BandX, RailInner.Y, BandW, RailInner.Height }, InspectorMetrics::PageRailFill.WithAlpha(A));
        }
    }
    if (RailScrollX < MaxRailScroll - 1.0f)
    {
        for (int32_t Band = 0; Band < 5; ++Band)
        {
            const float BandW = 3.5f;
            const float BandX = RailInner.Right() - static_cast<float>(5 - Band) * BandW;
            const uint8_t A   = static_cast<uint8_t>(40u + Band * 42u);
            Surface.FillRectangle({ BandX, RailInner.Y, BandW, RailInner.Height }, InspectorMetrics::PageRailFill.WithAlpha(A));
        }
    }

    Surface.PopClip();
    return TopY + StripH + 12.0f;
}

void ConstructSectionSurface(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    const char* Title,
    const char* RightText,
    ColorRgba   RightInk) noexcept
{
    Surface.FillRectangle(Bounds, InspectorMetrics::SectionFill, InspectorMetrics::SectionRadius);
    Surface.StrokeRectangle(Bounds, InspectorMetrics::SectionBorder, InspectorMetrics::SectionRadius, 1.0f);

    const float TitlePadX = std::clamp(Bounds.Width * 0.065f, 14.0f, 22.0f);
    float RightReserved   = 0.0f;
    if (RightText != nullptr && RightText[0] != '\0')
    {
        const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, RightText);
        RightReserved  = RW + 10.0f;
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { Bounds.Right() - TitlePadX - RW, Bounds.Y + 21.0f },
            11.0f,
            RightInk,
            RightText);
    }

    if (Title != nullptr && Title[0] != '\0')
    {
        const float MaxTitleW = std::max(40.0f, Bounds.Width - TitlePadX * 2.0f - RightReserved);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Light,
            { Bounds.X + TitlePadX, Bounds.Y + 20.0f },
            MaxTitleW,
            15.0f,
            ColorRgba{ 209u, 209u, 206u, 255u },
            Title);
    }
}

float ConstructMetricReadout(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    const char* NumberText,
    const char* UnitText,
    const char* CaptionText,
    float       NumberSize) noexcept
{
    // Split integer + fractional part so decimals render softly muted (1:1 HTML parity: "18" bright + ".3" muted)
    const std::string_view FullNum(NumberText != nullptr ? NumberText : "");
    const std::size_t      DotPos = FullNum.find('.');
    float                  NumAdvance = 0.0f;
    if (DotPos != std::string_view::npos)
    {
        const std::string IntPart(FullNum.substr(0u, DotPos));
        const std::string FracPart(FullNum.substr(DotPos));
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Light, { LeftX, TopY }, NumberSize, ColorRgba{ 233u, 233u, 233u, 255u }, IntPart);
        const float IntW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, NumberSize, IntPart);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Light, { LeftX + IntW, TopY }, NumberSize, ColorRgba{ 138u, 138u, 142u, 255u }, FracPart);
        NumAdvance = IntW + WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, NumberSize, FracPart);
    }
    else
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Light, { LeftX, TopY }, NumberSize, ColorRgba{ 233u, 233u, 233u, 255u }, FullNum);
        NumAdvance = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, NumberSize, FullNum);
    }

    if (UnitText != nullptr && UnitText[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { LeftX + NumAdvance + 7.0f, TopY + NumberSize * 0.42f },
            14.5f,
            ColorRgba{ 153u, 153u, 153u, 255u },
            UnitText);
    }

    float NextY = TopY + NumberSize * 1.18f + 4.0f;
    if (CaptionText != nullptr && CaptionText[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { LeftX, NextY }, 11.0f, InspectorMetrics::MutedInk, CaptionText);
        NextY += 18.0f;
    }
    return NextY;
}

float ConstructKpiMetricGrid(
    PixelSpace&                             Surface,
    float                                   LeftX,
    float                                   TopY,
    float                                   InnerWidth,
    std::span<const InspectorKpiMetricSpec> Metrics) noexcept
{
    if (Metrics.empty())
    {
        return TopY;
    }
    const float    Gap  = 10.0f;
    const float    BoxW = std::floor((InnerWidth - Gap) * 0.5f);
    const float    BoxH = 74.0f;
    const uint32_t Rows = (static_cast<uint32_t>(Metrics.size()) + 1u) / 2u;

    for (std::size_t Idx = 0u; Idx < Metrics.size(); ++Idx)
    {
        const uint32_t Col = static_cast<uint32_t>(Idx) % 2u;
        const uint32_t Row = static_cast<uint32_t>(Idx) / 2u;
        const Rectangle2D Box {
            LeftX + static_cast<float>(Col) * (BoxW + Gap),
            TopY + static_cast<float>(Row) * (BoxH + Gap),
            BoxW,
            BoxH,
        };
        Surface.FillRectangle(Box, InspectorMetrics::SectionFill, 16.0f);
        Surface.StrokeRectangle(Box, InspectorMetrics::SectionBorder, 16.0f, 1.0f);

        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Light, { Box.X + 16.0f, Box.Y + 14.0f }, 20.0f, ColorRgba{ 234u, 234u, 234u, 255u }, Metrics[Idx].ReadingText);
        if (Metrics[Idx].UnitText != nullptr && Metrics[Idx].UnitText[0] != '\0')
        {
            const float VW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 20.0f, Metrics[Idx].ReadingText);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { Box.X + 21.0f + VW, Box.Y + 21.0f }, 10.5f, InspectorMetrics::MutedInk, Metrics[Idx].UnitText);
        }
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { Box.X + 16.0f, Box.Bottom() - 21.0f }, 9.0f, InspectorMetrics::DimInk, Metrics[Idx].CaptionText);
    }

    return TopY + static_cast<float>(Rows) * BoxH + static_cast<float>(Rows > 0u ? Rows - 1u : 0u) * Gap + InspectorMetrics::SectionGap;
}

float ConstructQuickSwitchSection(
    PixelSpace&                               Surface,
    float                                     LeftX,
    float                                     TopY,
    float                                     InnerWidth,
    std::span<const InspectorQuickSwitchSpec> Switches,
    const PaneInputReading&                   Input,
    int32_t&                                  OutClickedIndex) noexcept
{
    OutClickedIndex = -1;
    if (Switches.empty())
    {
        return TopY;
    }

    uint32_t ActiveCount = 0u;
    uint32_t LiveCount   = 0u;
    for (const auto& Sw : Switches)
    {
        if (!Sw.Disabled)
        {
            ++LiveCount;
            if (Sw.Active) ++ActiveCount;
        }
    }

    // Responsive wrapping grid (Android-narrow 1-col compact rows -> 2-col -> 3-col -> 4-col wide desktop)
    const float    PadX      = std::clamp(InnerWidth * 0.065f, 14.0f, 22.0f);
    const float    PadTop    = InspectorMetrics::SectionPadTop;
    const float    PadBot    = InspectorMetrics::SectionPadBottom;
    const float    Gap       = 8.0f;
    const float    BodyW     = std::max(100.0f, InnerWidth - PadX * 2.0f);
    constexpr float MinTileW = 94.0f;
    const uint32_t MaxColsByWidth = std::max(1u, static_cast<uint32_t>(std::floor((BodyW + Gap) / (MinTileW + Gap))));
    const uint32_t Cols      = std::clamp(MaxColsByWidth, 1u, std::min<uint32_t>(4u, static_cast<uint32_t>(Switches.size())));
    const uint32_t Rows      = (static_cast<uint32_t>(Switches.size()) + Cols - 1u) / Cols;
    const float    TileW     = std::floor((BodyW - Gap * static_cast<float>(Cols - 1u)) / static_cast<float>(Cols));
    const bool     CompactRow = (Cols == 1u);
    const float    TileH     = CompactRow ? 48.0f : std::clamp(TileW * 0.78f, 76.0f, InspectorMetrics::QuickTileHeight);
    const float    HeaderH   = 30.0f;
    const float    BoxH      = PadTop + HeaderH + static_cast<float>(Rows) * TileH + static_cast<float>(Rows > 0u ? Rows - 1u : 0u) * Gap + PadBot;
    RegisterInteractiveRect({ LeftX + PadX, TopY + PadTop + HeaderH, BodyW, BoxH - PadTop - HeaderH - PadBot });

    const Rectangle2D Bounds { LeftX, TopY, InnerWidth, BoxH };
    ConstructSectionSurface(Surface, Bounds, "Quick controls");

    // Green active count ("2 / 3 on")
    char ActiveBuf[16]{};
    char TotalBuf[24]{};
    std::snprintf(ActiveBuf, sizeof(ActiveBuf), "%u", ActiveCount);
    std::snprintf(TotalBuf,  sizeof(TotalBuf),  " / %u on", LiveCount);
    const float AW     = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 11.0f, ActiveBuf);
    const float TW     = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, TotalBuf);
    const float RightX = Bounds.Right() - PadX - (AW + TW);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { RightX, Bounds.Y + PadTop + 1.5f }, 11.5f,
        InspectorMetrics::QuickActiveGreen, ActiveBuf);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { RightX + AW, Bounds.Y + PadTop + 1.5f }, 11.5f, InspectorMetrics::MutedInk, TotalBuf);

    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const float   GridY = Bounds.Y + PadTop + HeaderH;

    for (std::size_t Idx = 0u; Idx < Switches.size(); ++Idx)
    {
        const uint32_t    Col = static_cast<uint32_t>(Idx) % Cols;
        const uint32_t    Row = static_cast<uint32_t>(Idx) / Cols;
        const Rectangle2D Tile {
            LeftX + PadX + static_cast<float>(Col) * (TileW + Gap),
            GridY + static_cast<float>(Row) * (TileH + Gap),
            TileW,
            TileH,
        };
        const bool Hovered = !Switches[Idx].Disabled && Tile.Contains(Cursor);

        const ColorRgba TileBg = Hovered ? ColorRgba{ 46u, 46u, 49u, 255u } : ColorRgba{ 38u, 38u, 40u, 255u };
        Surface.FillRectangle(Tile, TileBg, InspectorMetrics::QuickTileRadius);
        Surface.StrokeRectangle(
            Tile,
            Hovered ? ColorRgba{ 255u, 255u, 255u, 32u } : ColorRgba{ 255u, 255u, 255u, 18u },
            InspectorMetrics::QuickTileRadius,
            1.0f);

        const ColorRgba WellFill = Switches[Idx].Active ? InspectorMetrics::QuickWellActiveFill : TileBg;
        const ColorRgba SymInk   = Switches[Idx].Active ? InspectorMetrics::QuickActiveGreen : ColorRgba{ 152u, 152u, 156u, 255u };
        const ColorRgba LabelInk = Switches[Idx].Active ? ColorRgba{ 230u, 230u, 230u, 255u } : ColorRgba{ 165u, 165u, 165u, 255u };

        if (CompactRow)
        {
            // Android / narrow 1-column horizontal switch row: icon pill left + clean truncated label right
            const Rectangle2D Well { Tile.X + 8.0f, Tile.Center().Y - 15.0f, 30.0f, 30.0f };
            if (Switches[Idx].Active)
            {
                Surface.FillRectangle(Well, WellFill, 7.0f);
            }
            ConstructSwitchSymbol(Surface, Switches[Idx].Symbol, Well.Center(), 16.0f, SymInk, WellFill);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Regular,
                { Well.Right() + 10.0f, Tile.Center().Y - 5.5f },
                std::max(24.0f, Tile.Right() - Well.Right() - 18.0f),
                11.0f,
                LabelInk,
                Switches[Idx].Label);
        }
        else
        {
            const float WellSize = std::min(34.0f, Tile.Height * 0.40f);
            const Rectangle2D Well { Tile.Center().X - WellSize * 0.5f, Tile.Y + std::max(10.0f, (Tile.Height - WellSize - 24.0f) * 0.5f), WellSize, WellSize };
            if (Switches[Idx].Active)
            {
                Surface.FillRectangle(Well, WellFill, 8.0f);
            }
            ConstructSwitchSymbol(Surface, Switches[Idx].Symbol, Well.Center(), WellSize * 0.52f, SymInk, WellFill);

            const float MaxW = std::max(24.0f, Tile.Width - 12.0f);
            const float LW   = std::min(MaxW, WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, Switches[Idx].Label));
            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Regular,
                { Tile.Center().X - LW * 0.5f, Tile.Bottom() - 21.0f },
                MaxW,
                10.5f,
                LabelInk,
                Switches[Idx].Label);
        }

        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            OutClickedIndex = static_cast<int32_t>(Idx);
        }
    }

    return Bounds.Bottom() + InspectorMetrics::SectionGap;
}

float ConstructScalarFieldRow(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    uint32_t                ControlId,
    const char*             Label,
    const char*             UnitText,
    const char*             FormatSpec,
    float                   Minimum,
    float                   Maximum,
    float&                  InOutReading,
    uint32_t&               InOutActiveDragId,
    const PaneInputReading& Input,
    bool&                   OutChanged,
    bool                    Disabled) noexcept
{
    const float Opacity = Disabled ? 0.42f : 1.0f;
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX, TopY }, 11.0f, InspectorMetrics::MutedInk.ScaledAlpha(Opacity), Label);

    char NumBuf[32]{};
    std::snprintf(NumBuf, sizeof(NumBuf), FormatSpec, InOutReading);

    // Exact HTML .field geometry: 26px tall readout capsule (92px wide = 56px dark value + 36px unit bay) + 10px gap + 120x26px slider
    const float RowY    = TopY + 20.0f;
    const float CapW    = 92.0f;
    const float CapH    = 26.0f;
    if (!Disabled)
    {
        RegisterInteractiveRect({ LeftX, RowY, InnerWidth, CapH });
    }
    if (InOutActiveDragId == ControlId && Input.PrimaryPointer.Down)
    {
        NoteInteractiveDragActive(true);
    }
    const float CapR    = CapH * 0.5f;
    const bool  HasUnit = (UnitText != nullptr && UnitText[0] != '\0');
    const float UnitW   = 36.0f;
    const float NumW    = CapW - UnitW;

    const Rectangle2D Whole { LeftX, RowY, CapW, CapH };
    const Rectangle2D NumCell { LeftX, RowY, NumW, CapH };
    const Rectangle2D UnitCell { LeftX + NumW, RowY, UnitW, CapH };

    Surface.FillRectangle(Whole, ColorRgba{ 38u, 38u, 38u, 255u }.ScaledAlpha(Opacity), CapR);
    Surface.FillRectangle(NumCell, ColorRgba{ 4u, 4u, 4u, 255u }.ScaledAlpha(Opacity), CapR);
    if (NumW > CapR)
    {
        Surface.FillRectangle({ LeftX + CapR, RowY, NumW - CapR, CapH }, ColorRgba{ 4u, 4u, 4u, 255u }.ScaledAlpha(Opacity), 0.0f);
    }
    Surface.StrokeRectangle(Whole, ColorRgba{ 255u, 255u, 255u, 14u }.ScaledAlpha(Opacity), CapR, 1.0f);

    const float NumTxtW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 12.0f, NumBuf);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular,
        { NumCell.Center().X - NumTxtW * 0.5f, RowY + 6.5f },
        12.0f, ColorRgba{ 236u, 236u, 236u, 255u }.ScaledAlpha(Opacity), NumBuf);
    if (HasUnit)
    {
        const float UnitTxtW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, UnitText);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular,
            { UnitCell.Center().X - UnitTxtW * 0.5f, RowY + 7.5f },
            10.0f, ColorRgba{ 126u, 126u, 126u, 255u }.ScaledAlpha(Opacity), UnitText);
    }

    // Exact HTML input[type=range]: x = LeftX + 102, w = InnerWidth - 102 (120px), h = 26px, #454545 fill / #242424 track, 22px white thumb
    const float Gap = 10.0f;
    const Rectangle2D TrackRect {
        LeftX + CapW + Gap,
        RowY,
        std::max(44.0f, InnerWidth - CapW - Gap),
        CapH,
    };

    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Hovered  = !Disabled && TrackRect.Contains(Cursor);
    const bool    Dragging = !Disabled && (InOutActiveDragId == ControlId);
    const float   Span     = std::max(Maximum - Minimum, 1.0e-6f);
    float         Fraction = std::clamp((InOutReading - Minimum) / Span, 0.0f, 1.0f);

    if (!Disabled)
    {
        if (Hovered && Input.PrimaryPointer.Pressed) InOutActiveDragId = ControlId;
        if (InOutActiveDragId == ControlId && Input.PrimaryPointer.Down)
        {
            const float Usable = std::max(1.0f, TrackRect.Width - 22.0f);
            Fraction = std::clamp((Cursor.X - (TrackRect.X + 11.0f)) / Usable, 0.0f, 1.0f);
            const float Next = Minimum + Fraction * Span;
            if (std::fabs(Next - InOutReading) > 1.0e-5f)
            {
                InOutReading = Next;
                OutChanged   = true;
            }
        }
        else if (Dragging && !Input.PrimaryPointer.Down)
        {
            InOutActiveDragId = 0u;
        }
    }

    const float TrackR = TrackRect.Height * 0.5f;
    Surface.FillRectangle(TrackRect, ColorRgba{ 36u, 36u, 36u, 255u }.ScaledAlpha(Opacity), TrackR);
    const float KnobR = 11.0f;
    const float KnobX = TrackRect.X + KnobR + 2.0f + Fraction * std::max(1.0f, TrackRect.Width - (KnobR + 2.0f) * 2.0f);
    const float FillW = std::clamp(KnobX - TrackRect.X + 2.0f, 0.0f, TrackRect.Width);
    if (FillW > 2.0f)
    {
        Surface.FillRectangle({ TrackRect.X, TrackRect.Y, FillW, TrackRect.Height }, ColorRgba{ 69u, 69u, 69u, 255u }.ScaledAlpha(Opacity), TrackR);
    }
    Surface.FillCircle({ KnobX, TrackRect.Center().Y }, KnobR, ColorRgba{ 236u, 236u, 236u, 255u }.ScaledAlpha(Opacity));

    return TopY + 72.0f;
}

float ConstructToggleFieldRow(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    const char*             Label,
    bool&                   InOutEnabled,
    const PaneInputReading& Input,
    bool&                   OutChanged,
    bool                    Disabled) noexcept
{
    const float Opacity = Disabled ? 0.42f : 1.0f;
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX, TopY + 5.0f }, 11.0f, ColorRgba{ 170u, 170u, 170u, 255u }.ScaledAlpha(Opacity), Label);

    const Rectangle2D Track {
        LeftX + InnerWidth - InspectorMetrics::ToggleWidth,
        TopY,
        InspectorMetrics::ToggleWidth,
        InspectorMetrics::ToggleHeight,
    };
    if (!Disabled)
    {
        RegisterInteractiveRect(Track);
    }
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Hovered = !Disabled && Track.Contains(Cursor);

    Surface.FillRectangle(
        Track,
        (InOutEnabled ? InspectorMetrics::ToggleOnFill : InspectorMetrics::ToggleOffFill).ScaledAlpha(Opacity),
        InspectorMetrics::ToggleHeight * 0.5f);

    const float KnobX = InOutEnabled ? (Track.Right() - 12.0f) : (Track.X + 12.0f);
    Surface.FillCircle({ KnobX, Track.Center().Y }, 10.0f, ColorRgba{ 238u, 238u, 238u, 255u }.ScaledAlpha(Opacity));

    if (Hovered && Input.PrimaryPointer.Pressed)
    {
        InOutEnabled = !InOutEnabled;
        OutChanged   = true;
    }

    return TopY + InspectorMetrics::ToggleHeight + 16.0f;
}

float ConstructColourFieldRow(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    uint32_t                ControlOriginId,
    const char*             Label,
    Mathematics::LinearRGB& InOutColour,
    uint32_t&               InOutActiveDragId,
    const PaneInputReading& Input,
    bool&                   OutChanged,
    bool                    Disabled) noexcept
{
    // Exact HTML .colour-field row: Label left (#999), bordered swatch pill (54x24) + uppercase #FFFFFF hex right (#777)
    const float     Opacity = Disabled ? 0.42f : 1.0f;
    const ColorRgba Swatch  = EncodeLinearRgbToSrgb8(InOutColour);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX, TopY + 5.0f }, 11.0f, ColorRgba{ 153u, 153u, 153u, 255u }.ScaledAlpha(Opacity), Label);

    char HexBuf[16]{};
    std::snprintf(HexBuf, sizeof(HexBuf), "#%02X%02X%02X", Swatch.Red, Swatch.Green, Swatch.Blue);
    const float HexW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, HexBuf);
    const float RightEdge = LeftX + InnerWidth;
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { RightEdge - HexW, TopY + 6.0f }, 10.0f, ColorRgba{ 119u, 119u, 119u, 255u }.ScaledAlpha(Opacity), HexBuf);

    const Rectangle2D OuterBezel { RightEdge - HexW - 12.0f - 58.0f, TopY - 1.0f, 58.0f, 26.0f };
    const Rectangle2D InnerFill  { OuterBezel.X + 4.0f, OuterBezel.Y + 4.0f, OuterBezel.Width - 8.0f, OuterBezel.Height - 8.0f };
    Surface.FillRectangle(OuterBezel, ColorRgba{ 0u, 0u, 0u, 255u }.ScaledAlpha(Opacity), 5.0f);
    Surface.FillRectangle(InnerFill, Swatch.ScaledAlpha(Opacity), 2.0f);
    Surface.StrokeRectangle(OuterBezel, ColorRgba{ 255u, 255u, 255u, 22u }.ScaledAlpha(Opacity), 5.0f, 1.0f);

    // Interactive click cycles physical atmospheric/tint swatches cleanly without inventing permanently visible RGB bars
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    if (!Disabled && OuterBezel.Contains(Cursor) && Input.PrimaryPointer.Pressed)
    {
        static constexpr std::array<Mathematics::LinearRGB, 5u> Presets = {{
            { 1.00f, 1.00f, 1.00f },
            { 0.84f, 0.90f, 0.98f },
            { 0.98f, 0.88f, 0.74f },
            { 0.76f, 0.84f, 0.88f },
            { 0.94f, 0.78f, 0.72f },
        }};
        const uint32_t NextIdx = (ControlOriginId + static_cast<uint32_t>(Swatch.Red / 40u) + 1u) % static_cast<uint32_t>(Presets.size());
        InOutColour = Presets[NextIdx];
        OutChanged  = true;
    }
    (void)InOutActiveDragId;

    return TopY + 38.0f;
}

float ConstructChoiceRow(
    PixelSpace&                  Surface,
    float                        LeftX,
    float                        TopY,
    float                        InnerWidth,
    const char*                  Label,
    std::span<const char* const> Choices,
    uint32_t                     ActiveIndex,
    uint32_t                     ColumnCount,
    const PaneInputReading&      Input,
    int32_t&                     OutChosenIndex) noexcept
{
    OutChosenIndex = -1;
    float CursorY = TopY;
    if (Label != nullptr && Label[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX, CursorY }, 11.0f, InspectorMetrics::MutedInk, Label);
        CursorY += 18.0f;
    }

    const uint32_t SafeCols = std::max(1u, ColumnCount);
    const float    Gap      = 6.0f;
    const float    BtnW     = std::floor((InnerWidth - Gap * static_cast<float>(SafeCols - 1u)) / static_cast<float>(SafeCols));
    const float    BtnH     = 28.0f;
    const uint32_t Rows     = (static_cast<uint32_t>(Choices.size()) + SafeCols - 1u) / SafeCols;
    const Point2D  Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    for (std::size_t Idx = 0u; Idx < Choices.size(); ++Idx)
    {
        const uint32_t    Col = static_cast<uint32_t>(Idx) % SafeCols;
        const uint32_t    Row = static_cast<uint32_t>(Idx) / SafeCols;
        const Rectangle2D Btn {
            LeftX + static_cast<float>(Col) * (BtnW + Gap),
            CursorY + static_cast<float>(Row) * (BtnH + Gap),
            BtnW,
            BtnH,
        };
        const bool Active  = (static_cast<uint32_t>(Idx) == ActiveIndex);
        const bool Hovered = Btn.Contains(Cursor);

        if (Active)
        {
            Surface.FillRectangle(Btn, ColorRgba{ 236u, 236u, 239u, 255u }, 9.0f);
        }
        else
        {
            Surface.FillRectangle(Btn, Hovered ? ColorRgba{ 46u, 46u, 50u, 255u } : ColorRgba{ 24u, 24u, 27u, 255u }, 9.0f);
            Surface.StrokeRectangle(Btn, ColorRgba{ 255u, 255u, 255u, 18u }, 9.0f, 1.0f);
        }

        const float MaxW = Btn.Width - 10.0f;
        const float LW   = std::min(MaxW, WorkspaceAppearance::MeasureWeightedTextWidth(Surface, Active ? FontWeightClass::Medium : FontWeightClass::Regular, 10.5f, Choices[Idx]));
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            Active ? FontWeightClass::Medium : FontWeightClass::Regular,
            { Btn.Center().X - LW * 0.5f, Btn.Y + 7.5f },
            MaxW,
            10.5f,
            Active ? ColorRgba{ 18u, 18u, 20u, 255u } : ColorRgba{ 196u, 196u, 200u, 255u },
            Choices[Idx]);

        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            OutChosenIndex = static_cast<int32_t>(Idx);
        }
    }

    return CursorY + static_cast<float>(Rows) * BtnH + static_cast<float>(Rows > 0u ? Rows - 1u : 0u) * Gap + 14.0f;
}

void ConstructCurvePlot(
    PixelSpace&            Surface,
    Rectangle2D            Bounds,
    std::span<const float> NormalizedY,
    ColorRgba              StrokeInk,
    ColorRgba              UnderFill,
    const char*            LeftCaption,
    const char*            RightCaption,
    float                  MarkerX01) noexcept
{
    Surface.FillRectangle(Bounds, ColorRgba{ 14u, 15u, 18u, 255u }, 12.0f);
    Surface.StrokeRectangle(Bounds, ColorRgba{ 255u, 255u, 255u, 14u }, 12.0f, 1.0f);

    // Subtle horizontal grid lines
    for (int32_t Line = 1; Line <= 3; ++Line)
    {
        const float GY = Bounds.Y + Bounds.Height * (static_cast<float>(Line) * 0.25f);
        Surface.StrokeLine({ Bounds.X + 8.0f, GY }, { Bounds.Right() - 8.0f, GY }, ColorRgba{ 255u, 255u, 255u, 12u }, 1.0f);
    }

    if (NormalizedY.size() >= 2u)
    {
        const float PadX   = 10.0f;
        const float PadTop = 10.0f;
        const float PadBot = (LeftCaption != nullptr || RightCaption != nullptr) ? 20.0f : 10.0f;
        const float PlotW  = std::max(8.0f, Bounds.Width - PadX * 2.0f);
        const float PlotH  = std::max(8.0f, Bounds.Height - PadTop - PadBot);
        const float FloorY = Bounds.Y + PadTop + PlotH;

        std::array<Point2D, 64u> Poly{};
        const std::size_t Count = std::min<std::size_t>(NormalizedY.size(), Poly.size());
        for (std::size_t I = 0u; I < Count; ++I)
        {
            const float T = static_cast<float>(I) / static_cast<float>(Count - 1u);
            const float V = std::clamp(NormalizedY[I], 0.0f, 1.0f);
            Poly[I] = { Bounds.X + PadX + T * PlotW, FloorY - V * PlotH };
        }

        // Under-curve vertical fill ribbons
        for (std::size_t I = 0u; I + 1u < Count; ++I)
        {
            const float MidY = (Poly[I].Y + Poly[I + 1u].Y) * 0.5f;
            const float SegW = std::max(1.0f, Poly[I + 1u].X - Poly[I].X + 0.5f);
            Surface.FillRectangle({ Poly[I].X, MidY, SegW, std::max(0.0f, FloorY - MidY) }, UnderFill);
        }

        Surface.StrokePolyline(std::span<const Point2D>(Poly.data(), Count), StrokeInk, 1.8f, false);

        if (MarkerX01 >= 0.0f && MarkerX01 <= 1.0f)
        {
            const float MX  = Bounds.X + PadX + MarkerX01 * PlotW;
            const std::size_t MIdx = std::min(Count - 1u, static_cast<std::size_t>(MarkerX01 * static_cast<float>(Count - 1u)));
            Surface.StrokeLine({ MX, Bounds.Y + PadTop }, { MX, FloorY }, ColorRgba{ 255u, 255u, 255u, 45u }, 1.0f);
            Surface.FillCircle({ MX, Poly[MIdx].Y }, 3.8f, ColorRgba{ 246u, 246u, 248u, 255u });
        }
    }

    if (LeftCaption != nullptr && LeftCaption[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + 10.0f, Bounds.Bottom() - 15.0f }, 9.0f, InspectorMetrics::DimInk, LeftCaption);
    }
    if (RightCaption != nullptr && RightCaption[0] != '\0')
    {
        const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, RightCaption);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.Right() - 10.0f - RW, Bounds.Bottom() - 15.0f }, 9.0f, InspectorMetrics::DimInk, RightCaption);
    }
}

namespace
{
struct QueuedTooltipData
{
    bool                              Active     = false;
    Rectangle2D                       Anchor     {};
    std::string                       Heading    {};
    std::string                       BodyText   {};
    std::vector<InspectorTooltipFact> Facts      {};
};

QueuedTooltipData ActiveTooltipQueue {};
} // namespace

void ResetTooltipQueue() noexcept
{
    ActiveTooltipQueue.Active = false;
    ActiveTooltipQueue.Heading.clear();
    ActiveTooltipQueue.BodyText.clear();
    ActiveTooltipQueue.Facts.clear();
}

void QueueTooltip(Rectangle2D AnchorRect, std::string_view Heading, std::string_view BodyText) noexcept
{
    ActiveTooltipQueue.Active   = true;
    ActiveTooltipQueue.Anchor   = AnchorRect;
    ActiveTooltipQueue.Heading  = std::string(Heading);
    ActiveTooltipQueue.BodyText = std::string(BodyText);
    ActiveTooltipQueue.Facts.clear();
}

void QueueFactTooltip(
    Rectangle2D                           AnchorRect,
    std::string_view                      Heading,
    std::span<const InspectorTooltipFact> Facts) noexcept
{
    ActiveTooltipQueue.Active = true;
    ActiveTooltipQueue.Anchor = AnchorRect;
    ActiveTooltipQueue.Heading = std::string(Heading);
    ActiveTooltipQueue.BodyText.clear();
    ActiveTooltipQueue.Facts.assign(Facts.begin(), Facts.end());
}

void FlushTooltipOverlay(PixelSpace& Surface, Rectangle2D PaneBounds) noexcept
{
    if (!ActiveTooltipQueue.Active) return;

    // Reuse Frontier's native ControlTooltip component (C1 filleted pointer tail, SplitHeader, semantic icon & auto viewport clamp)
    TooltipDescriptor Desc {};
    Desc.SurfaceMode    = TooltipSurfaceMode::DarkSurface;
    Desc.TailProfile    = TooltipTailProfile::FilletedCurve;
    Desc.TailAlignment  = TooltipTailAlignment::TargetAligned;
    Desc.Direction      = TooltipDirection::Automatic;
    Desc.PreferredWidth = std::min(312.0f, std::max(216.0f, PaneBounds.Width - 18.0f));
    Desc.CornerRadius   = 16.0f;
    Desc.AnchorGap      = 6.0f;
    Desc.Title          = ActiveTooltipQueue.Heading.c_str();

    std::string CombinedBody {};
    std::string HeaderTagStr {};
    std::vector<TooltipMetricToken> Tokens {};

    if (!ActiveTooltipQueue.Facts.empty())
    {
        Desc.Layout   = TooltipLayoutVariant::SplitHeader;
        Desc.Severity = TooltipSeverity::Success;

        for (const auto& Fact : ActiveTooltipQueue.Facts)
        {
            if (Fact.Label == "RESOLUTION" && HeaderTagStr.empty())
            {
                HeaderTagStr = Fact.Reading;
                continue;
            }
            if (Fact.Label == "SIZE")
            {
                TooltipMetricToken Tok {};
                Tok.Caption = Fact.Reading.c_str();
                Tok.Icon    = ControlIcon::CircleNotice;
                Tokens.push_back(Tok);
                continue;
            }
            if (!CombinedBody.empty()) CombinedBody.push_back('\n');
            CombinedBody += Fact.Label;
            CombinedBody += ": ";
            CombinedBody += Fact.Reading;
        }
        Desc.HeaderTag    = HeaderTagStr.c_str();
        Desc.BodyText     = CombinedBody.c_str();
        Desc.MetricTokens = Tokens;
    }
    else
    {
        Desc.Layout   = TooltipLayoutVariant::Standard;
        Desc.Severity = TooltipSeverity::Informational;
        Desc.BodyText = ActiveTooltipQueue.BodyText.c_str();
    }

    const ControlPointer NeutralPointer {};
    (void)ControlTooltip::ConstructAnchored(Surface, ActiveTooltipQueue.Anchor, PaneBounds, Desc, NeutralPointer, 1.0f);
}

void ConstructTooltipTrigger(
    PixelSpace&             Surface,
    Point2D                 Center,
    std::string_view        Heading,
    std::string_view        BodyText,
    const PaneInputReading& Input) noexcept
{
    const Rectangle2D Hit { Center.X - 8.0f, Center.Y - 8.0f, 16.0f, 16.0f };
    const Point2D     Cur { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool        Hov = Hit.Contains(Cur);
    Surface.FillCircle(Center, 7.0f, Hov ? ColorRgba{ 58u, 72u, 92u, 255u } : ColorRgba{ 36u, 37u, 42u, 255u });
    Surface.StrokeCircle(Center, 7.0f, Hov ? ColorRgba{ 156u, 198u, 246u, 190u } : ColorRgba{ 255u, 255u, 255u, 32u }, 1.0f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { Center.X - 2.6f, Center.Y - 5.2f }, 9.2f,
        Hov ? ColorRgba{ 228u, 238u, 255u, 255u } : InspectorMetrics::MutedInk, "?");
    if (Hov)
    {
        QueueTooltip(Hit, Heading, BodyText);
    }
}

float ConstructBakedLutSection(
    PixelSpace&              Surface,
    float                    LeftX,
    float                    TopY,
    float                    InnerWidth,
    const char*              Title,
    const char*              ResolutionText,
    const char*              FormatText,
    const char*              AxisCaption,
    InspectorPreviewSurface& Preview,
    float                    PreviewHeightPx,
    const PaneInputReading*  OptionalInput,
    bool*                    OptionalSettingsOpen,
    uint32_t*                OptionalScaleTier,
    bool*                    OptionalRebakeClicked,
    bool*                    OptionalSaveClicked) noexcept
{
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    const Point2D Cur = OptionalInput ? Point2D{ OptionalInput->PrimaryPointer.X, OptionalInput->PrimaryPointer.Y } : Point2D{ -9999.0f, -9999.0f };
    const bool Clicked = OptionalInput && OptionalInput->PrimaryPointer.Pressed;

    // Bake Settings face (F76): resolution tier selector + texel/memory facts + Cancel / Bake buttons
    if (OptionalSettingsOpen && *OptionalSettingsOpen)
    {
        const float BoxH = InspectorMetrics::SectionPadTop + 28.0f + 56.0f + 114.0f + 40.0f + InspectorMetrics::SectionPadBottom;
        const Rectangle2D Section { LeftX, TopY, InnerWidth, BoxH };
        ConstructSectionSurface(Surface, Section, "Bake settings", Title, InspectorMetrics::MutedInk);

        float Y = TopY + InspectorMetrics::SectionPadTop + 28.0f;
        uint32_t Tier = OptionalScaleTier ? *OptionalScaleTier : 1u;
        static constexpr std::array<const char*, 3u> TierLabels = {{ "Half", "Default", "2x Fine" }};
        const float SegW = (BodyW - 12.0f) / 3.0f;
        for (uint32_t I = 0u; I < 3u; ++I)
        {
            const Rectangle2D Seg { LeftX + PadX + static_cast<float>(I) * (SegW + 6.0f), Y, SegW, 44.0f };
            const bool On  = (Tier == I);
            const bool Hov = Seg.Contains(Cur);
            Surface.FillRectangle(Seg, On ? ColorRgba{ 46u, 52u, 62u, 255u } : (Hov ? ColorRgba{ 36u, 37u, 42u, 255u } : ColorRgba{ 28u, 29u, 32u, 255u }), 9.0f);
            Surface.StrokeRectangle(Seg, On ? ColorRgba{ 146u, 190u, 245u, 140u } : ColorRgba{ 255u, 255u, 255u, 18u }, 9.0f, 1.0f);
            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.5f, TierLabels[I]);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Seg.Center().X - TW * 0.5f, Seg.Y + 9.0f }, 10.5f, InspectorMetrics::HeadingInk, TierLabels[I]);
            const char* Sub = (I == 0u) ? "0.5x res" : (I == 1u ? "default" : "2.0x res");
            const float SW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, Sub);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Seg.Center().X - SW * 0.5f, Seg.Y + 25.0f }, 9.0f, InspectorMetrics::DimInk, Sub);
            if (Hov && Clicked)
            {
                Tier = I;
                if (OptionalScaleTier) *OptionalScaleTier = I;
            }
        }
        Y += 54.0f;

        const uint32_t SourceW = std::max(1u, Preview.QueryWidthPx());
        const uint32_t SourceH = std::max(1u, Preview.QueryHeightPx());
        const uint32_t MulNum = (Tier == 0u) ? 1u : (Tier == 1u ? 2u : 4u);
        const uint32_t OutW = std::max(16u, (SourceW * MulNum) / 2u);
        const uint32_t OutH = std::max(16u, (SourceH * MulNum) / 2u);
        const uint32_t BytesTotal = OutW * OutH * 4u;
        char ResLine[32]{}, TexLine[32]{}, MemLine[32]{};
        std::snprintf(ResLine, sizeof(ResLine), "%u x %u", OutW, OutH);
        std::snprintf(TexLine, sizeof(TexLine), "%u texels", OutW * OutH);
        std::snprintf(MemLine, sizeof(MemLine), "%u KB (RGBA8)", std::max(1u, BytesTotal / 1024u));

        const Rectangle2D FactBox { LeftX + PadX, Y, BodyW, 104.0f };
        Surface.FillRectangle(FactBox, ColorRgba{ 255u, 255u, 255u, 8u }, 8.0f);
        Surface.StrokeRectangle(FactBox, ColorRgba{ 255u, 255u, 255u, 14u }, 8.0f, 1.0f);
        float FY = FactBox.Y + 8.0f;
        FY = ConstructMetadataRow(Surface, FactBox.X + 10.0f, FY, FactBox.Width - 20.0f, "Resolution", ResLine);
        FY = ConstructMetadataRow(Surface, FactBox.X + 10.0f, FY, FactBox.Width - 20.0f, "Format", FormatText);
        FY = ConstructMetadataRow(Surface, FactBox.X + 10.0f, FY, FactBox.Width - 20.0f, "Texels", TexLine);
        (void)ConstructMetadataRow(Surface, FactBox.X + 10.0f, FY, FactBox.Width - 20.0f, "Memory", MemLine);
        Y = FactBox.Bottom() + 10.0f;

        const float HalfBtnW = (BodyW - 8.0f) * 0.5f;
        if (OptionalInput && ConstructSectionActionButton(Surface, LeftX + PadX, Y, HalfBtnW, "Cancel", *OptionalInput, false))
        {
            *OptionalSettingsOpen = false;
        }
        if (OptionalInput && ConstructSectionActionButton(Surface, LeftX + PadX + HalfBtnW + 8.0f, Y, HalfBtnW, "Bake now", *OptionalInput, true))
        {
            *OptionalSettingsOpen = false;
            if (OptionalRebakeClicked) *OptionalRebakeClicked = true;
        }
        return Section.Bottom() + InspectorMetrics::SectionGap;
    }

    // Standard Baked face (1:1 HTML BakedMapCards.jsx + BakedMapCards.css):
    // Title, Baked status bar (green Baked pill + native ControlTooltip + res + Rebake/Save icons),
    // Y-axis + X-axis plotted LUT image, and live "Point at the image to read it" readout.
    const float BarH    = 26.0f;
    const float AxisW   = 32.0f;
    const float AxisX_H = 18.0f;
    const float BoxH    = InspectorMetrics::SectionPadTop + 26.0f + BarH + 8.0f + PreviewHeightPx + AxisX_H + 26.0f + InspectorMetrics::SectionPadBottom;
    const Rectangle2D Section { LeftX, TopY, InnerWidth, BoxH };
    ConstructSectionSurface(Surface, Section, Title, "", InspectorMetrics::MutedInk);

    const float BarY = TopY + InspectorMetrics::SectionPadTop + 24.0f;
    // Green "Baked" capsule (hover opens F76 details native ControlTooltip)
    const Rectangle2D BakedCap { LeftX + PadX, BarY + 2.0f, 68.0f, 22.0f };
    const bool BakedHover = BakedCap.Contains(Cur);
    Surface.FillRectangle(BakedCap, BakedHover ? ColorRgba{ 28u, 62u, 42u, 255u } : ColorRgba{ 22u, 46u, 33u, 255u }, 11.0f);
    Surface.StrokeRectangle(BakedCap, ColorRgba{ 88u, 214u, 141u, 110u }, 11.0f, 1.0f);
    Surface.StrokeLine({ BakedCap.X + 9.0f, BakedCap.Y + 11.5f }, { BakedCap.X + 12.0f, BakedCap.Y + 14.5f }, ColorRgba{ 118u, 232u, 162u, 255u }, 1.5f);
    Surface.StrokeLine({ BakedCap.X + 12.0f, BakedCap.Y + 14.5f }, { BakedCap.X + 17.5f, BakedCap.Y + 8.0f }, ColorRgba{ 118u, 232u, 162u, 255u }, 1.5f);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { BakedCap.X + 23.0f, BakedCap.Y + 5.0f }, 10.0f, ColorRgba{ 150u, 238u, 184u, 255u }, "Baked");

    // Monospace resolution readout beside Baked capsule
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { BakedCap.Right() + 8.0f, BarY + 7.0f }, BodyW - 134.0f, 9.8f, InspectorMetrics::MutedInk, ResolutionText);

    // Right-side Rebake (↺) & Save PNG (↓) icon buttons
    const Rectangle2D SaveBtn   { LeftX + PadX + BodyW - 24.0f, BarY + 1.0f, 24.0f, 24.0f };
    const Rectangle2D RebakeBtn { SaveBtn.X - 28.0f,            BarY + 1.0f, 24.0f, 24.0f };
    const bool RebakeHov = RebakeBtn.Contains(Cur);
    const bool SaveHov   = SaveBtn.Contains(Cur);

    Surface.FillRectangle(RebakeBtn, RebakeHov ? ColorRgba{ 46u, 48u, 54u, 255u } : ColorRgba{ 30u, 31u, 35u, 255u }, 7.0f);
    Surface.StrokeRectangle(RebakeBtn, ColorRgba{ 255u, 255u, 255u, static_cast<uint8_t>(RebakeHov ? 42u : 18u) }, 7.0f, 1.0f);
    Surface.StrokeCircle(RebakeBtn.Center(), 5.0f, ColorRgba{ 210u, 214u, 222u, 255u }, 1.3f);
    Surface.StrokeLine({ RebakeBtn.Center().X - 5.5f, RebakeBtn.Center().Y - 4.0f }, { RebakeBtn.Center().X - 2.0f, RebakeBtn.Center().Y - 4.0f }, ColorRgba{ 210u, 214u, 222u, 255u }, 1.3f);

    Surface.FillRectangle(SaveBtn, SaveHov ? ColorRgba{ 46u, 48u, 54u, 255u } : ColorRgba{ 30u, 31u, 35u, 255u }, 7.0f);
    Surface.StrokeRectangle(SaveBtn, ColorRgba{ 255u, 255u, 255u, static_cast<uint8_t>(SaveHov ? 42u : 18u) }, 7.0f, 1.0f);
    Surface.StrokeLine({ SaveBtn.Center().X, SaveBtn.Y + 6.0f }, { SaveBtn.Center().X, SaveBtn.Y + 14.0f }, ColorRgba{ 210u, 214u, 222u, 255u }, 1.3f);
    Surface.StrokeLine({ SaveBtn.Center().X - 3.5f, SaveBtn.Y + 11.0f }, { SaveBtn.Center().X, SaveBtn.Y + 14.5f }, ColorRgba{ 210u, 214u, 222u, 255u }, 1.3f);
    Surface.StrokeLine({ SaveBtn.Center().X + 3.5f, SaveBtn.Y + 11.0f }, { SaveBtn.Center().X, SaveBtn.Y + 14.5f }, ColorRgba{ 210u, 214u, 222u, 255u }, 1.3f);
    Surface.StrokeLine({ SaveBtn.X + 6.5f, SaveBtn.Bottom() - 6.0f }, { SaveBtn.Right() - 6.5f, SaveBtn.Bottom() - 6.0f }, ColorRgba{ 210u, 214u, 222u, 255u }, 1.3f);

    if (BakedHover)
    {
        const uint32_t Wpx = std::max(1u, Preview.QueryWidthPx());
        const uint32_t Hpx = std::max(1u, Preview.QueryHeightPx());
        char SizeBuf[32]{}, CountBuf[32]{};
        std::snprintf(SizeBuf,  sizeof(SizeBuf),  "%u KB · 4 B/texel", std::max(1u, (Wpx * Hpx * 4u) / 1024u));
        std::snprintf(CountBuf, sizeof(CountBuf), "Retained bake #%u", Preview.QueryUploadCount());
        const std::array<InspectorTooltipFact, 5u> Facts = {{
            { "MAP",        Title ? Title : "Baked LUT" },
            { "RESOLUTION", ResolutionText ? ResolutionText : "-" },
            { "FORMAT",     FormatText ? FormatText : "RGBA8" },
            { "SIZE",       SizeBuf },
            { "STATUS",     CountBuf },
        }};
        QueueFactTooltip(BakedCap, "Baked LUT details", Facts);
    }
    else if (RebakeHov)
    {
        QueueTooltip(RebakeBtn, "Rebake", "Open resolution settings & re-bake this LUT");
    }
    else if (SaveHov)
    {
        QueueTooltip(SaveBtn, "Save PNG", "Export current baked LUT image to disk");
    }

    if (RebakeHov && Clicked)
    {
        if (OptionalSettingsOpen) *OptionalSettingsOpen = true;
        else if (OptionalRebakeClicked) *OptionalRebakeClicked = true;
    }
    if (SaveHov && Clicked && OptionalSaveClicked)
    {
        *OptionalSaveClicked = true;
    }

    // Exact 1:1 HTML .baked-map-plot.with-axes: 32px left Y-axis rule + rounded LUT plot + 18px bottom X-axis rule
    const float       PlotTopY = BarY + BarH + 8.0f;
    const Rectangle2D ImgBox { LeftX + PadX + AxisW, PlotTopY, std::max(40.0f, BodyW - AxisW), PreviewHeightPx };

    // Y-axis ticks & hairline rule (#ffffff14)
    const ColorRgba AxisInk  { 138u, 143u, 152u, 255u };
    const ColorRgba AxisRule { 255u, 255u, 255u, 20u  };
    Surface.StrokeLine({ ImgBox.X - 4.0f, ImgBox.Y }, { ImgBox.X - 4.0f, ImgBox.Bottom() }, AxisRule, 1.0f);
    static constexpr std::array<const char*, 5u> YTicks = {{ "90°", "45°", "0°", "-45°", "-90°" }};
    for (uint32_t I = 0u; I < 5u; ++I)
    {
        const float Frac = static_cast<float>(I) / 4.0f;
        const float TW   = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.5f, YTicks[I]);
        float       TY   = ImgBox.Y + Frac * (ImgBox.Height - 9.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { ImgBox.X - 8.0f - TW, TY }, 8.5f, AxisInk, YTicks[I]);
    }

    Preview.Present(Surface, ImgBox, 10.0f);
    Surface.StrokeRectangle(ImgBox, ColorRgba{ 255u, 255u, 255u, 18u }, 10.0f, 1.0f);

    // X-axis ticks & hairline rule (#ffffff14)
    Surface.StrokeLine({ ImgBox.X, ImgBox.Bottom() + 4.0f }, { ImgBox.Right(), ImgBox.Bottom() + 4.0f }, AxisRule, 1.0f);
    static constexpr std::array<const char*, 5u> XTicks = {{ "0°", "90°", "180°", "270°", "360°" }};
    for (uint32_t I = 0u; I < 5u; ++I)
    {
        const float Frac = static_cast<float>(I) / 4.0f;
        const float TW   = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.5f, XTicks[I]);
        float       TX   = ImgBox.X + Frac * ImgBox.Width;
        if (I == 4u)      TX -= TW;
        else if (I > 0u)  TX -= TW * 0.5f;
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { TX, ImgBox.Bottom() + 7.5f }, 8.5f, AxisInk, XTicks[I]);
    }

    // Live cursor reticle & "Point at the image to read it" footer line
    const float ReadoutY = ImgBox.Bottom() + AxisX_H + 8.0f;
    if (ImgBox.Contains(Cur))
    {
        Surface.StrokeCircle(Cur, 6.2f, ColorRgba{ 0u, 0u, 0u, 175u }, 1.5f);
        Surface.StrokeCircle(Cur, 5.2f, ColorRgba{ 255u, 255u, 255u, 240u }, 1.2f);
        const float U   = std::clamp((Cur.X - ImgBox.X) / std::max(1.0f, ImgBox.Width), 0.0f, 1.0f);
        const float V   = std::clamp((Cur.Y - ImgBox.Y) / std::max(1.0f, ImgBox.Height), 0.0f, 1.0f);
        const float Az  = U * 360.0f;
        const float El  = 90.0f - V * 180.0f;
        char UvBuf[64]{};
        std::snprintf(UvBuf, sizeof(UvBuf), "Azimuth %.0f° · Elevation %+.0f° · %s", Az, El, AxisCaption ? AxisCaption : "");
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { LeftX + PadX, ReadoutY }, BodyW, 9.8f, ColorRgba{ 226u, 232u, 240u, 255u }, UvBuf);
    }
    else
    {
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { LeftX + PadX, ReadoutY }, BodyW, 9.8f, InspectorMetrics::DimInk, "Point at the image to read it");
    }

    return Section.Bottom() + InspectorMetrics::SectionGap;
}

float ConstructTagsSection(
    PixelSpace&                  Surface,
    float                        LeftX,
    float                        TopY,
    float                        InnerWidth,
    std::span<const char* const> TagLabels) noexcept
{
    const float PadX = InspectorMetrics::SectionPadX;
    char CountBuf[16]{};
    std::snprintf(CountBuf, sizeof(CountBuf), "%zu", TagLabels.size());

    // Measure wrapped chip height
    float ChipX = LeftX + PadX;
    float ChipY = TopY + InspectorMetrics::SectionPadTop + 28.0f;
    const float MaxX = LeftX + InnerWidth - PadX;
    const float ChipH = 26.0f;
    for (const char* Tag : TagLabels)
    {
        const float W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, Tag) + 28.0f;
        if (ChipX + W > MaxX && ChipX > LeftX + PadX)
        {
            ChipX = LeftX + PadX;
            ChipY += ChipH + 6.0f;
        }
        ChipX += W + 6.0f;
    }

    // Add-tag input row below tag chips (matches HTML .tags-card h=138.0)
    const float AddY  = ChipY + ChipH + 8.0f;
    const float AddH  = 26.0f;
    const float BoxH  = (AddY + AddH + InspectorMetrics::SectionPadBottom) - TopY;
    const Rectangle2D Section { LeftX, TopY, InnerWidth, BoxH };
    ConstructSectionSurface(Surface, Section, "Tags", CountBuf, InspectorMetrics::MutedInk);

    ChipX = LeftX + PadX;
    ChipY = TopY + InspectorMetrics::SectionPadTop + 28.0f;
    for (const char* Tag : TagLabels)
    {
        const float W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, Tag) + 28.0f;
        if (ChipX + W > MaxX && ChipX > LeftX + PadX)
        {
            ChipX = LeftX + PadX;
            ChipY += ChipH + 6.0f;
        }
        const Rectangle2D Chip { ChipX, ChipY, W, ChipH };
        Surface.FillRectangle(Chip, InspectorMetrics::TagFill, ChipH * 0.5f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Chip.X + 11.0f, Chip.Y + 6.5f }, 11.0f, InspectorMetrics::TagInk, Tag);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Chip.Right() - 14.0f, Chip.Y + 6.0f }, 11.0f, InspectorMetrics::TagInk.ScaledAlpha(0.65f), "x");
        ChipX += W + 6.0f;
    }

    const Rectangle2D AddPill { LeftX + PadX, AddY, 92.0f, AddH };
    Surface.StrokeRectangle(AddPill, ColorRgba{ 255u, 255u, 255u, 28u }, AddH * 0.5f, 1.0f);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { AddPill.X + 12.0f, AddPill.Y + 6.5f }, 10.6f, InspectorMetrics::DimInk, "Add tag");

    return Section.Bottom() + InspectorMetrics::SectionGap;
}

float ConstructNotesSection(
    PixelSpace&  Surface,
    float        LeftX,
    float        TopY,
    float        InnerWidth,
    std::string& NotesText) noexcept
{
    const float PadX = InspectorMetrics::SectionPadX;
    const float AreaH = 93.0f;
    const float BoxH  = InspectorMetrics::SectionPadTop + 28.0f + AreaH + InspectorMetrics::SectionPadBottom;
    const Rectangle2D Section { LeftX, TopY, InnerWidth, BoxH };
    ConstructSectionSurface(Surface, Section, "Notes");

    const Rectangle2D Area { LeftX + PadX, TopY + InspectorMetrics::SectionPadTop + 28.0f, InnerWidth - PadX * 2.0f, AreaH };
    Surface.FillRectangle(Area, InspectorMetrics::NotesFill, 12.0f);
    Surface.StrokeRectangle(Area, InspectorMetrics::NotesBorder, 12.0f, 1.0f);

    const char* Shown = NotesText.empty() ? "Purpose, ownership, review notes..." : NotesText.c_str();
    const ColorRgba Ink = NotesText.empty() ? InspectorMetrics::DimInk : ColorRgba{ 214u, 214u, 214u, 255u };
    WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { Area.X + 12.0f, Area.Y + 11.0f }, Area.Width - 24.0f, 11.5f, Ink, Shown);

    return Section.Bottom() + InspectorMetrics::SectionGap;
}

float ConstructTransformSections(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    OutlinerRecordEntry&    Entry,
    uint32_t                ControlOriginId,
    uint32_t&               InOutActiveDragId,
    const PaneInputReading& Input,
    bool&                   OutMutated) noexcept
{
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    float CursorY     = TopY;
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 250.0f };
        ConstructSectionSurface(Surface, S, "Position");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 1u, "X", "m", "%.2f", -500.0f, 500.0f, Entry.PositionX, InOutActiveDragId, Input, OutMutated, Entry.Locked);
        Y = ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 2u, "Y", "m", "%.2f", -500.0f, 500.0f, Entry.PositionY, InOutActiveDragId, Input, OutMutated, Entry.Locked);
        (void)ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 3u, "Z", "m", "%.2f", -500.0f, 500.0f, Entry.PositionZ, InOutActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 250.0f };
        ConstructSectionSurface(Surface, S, "Rotation");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 4u, "Pitch", "deg", "%.1f", -180.0f, 180.0f, Entry.RotationX, InOutActiveDragId, Input, OutMutated, Entry.Locked);
        Y = ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 5u, "Roll",  "deg", "%.1f", -180.0f, 180.0f, Entry.RotationY, InOutActiveDragId, Input, OutMutated, Entry.Locked);
        (void)ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 6u, "Yaw",   "deg", "%.1f", -180.0f, 180.0f, Entry.RotationZ, InOutActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 250.0f };
        ConstructSectionSurface(Surface, S, "Scale");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 7u, "Scale X", "x", "%.2f", 0.05f, 50.0f, Entry.ScaleX, InOutActiveDragId, Input, OutMutated, Entry.Locked);
        Y = ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 8u, "Scale Y", "x", "%.2f", 0.05f, 50.0f, Entry.ScaleY, InOutActiveDragId, Input, OutMutated, Entry.Locked);
        (void)ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 9u, "Scale Z", "x", "%.2f", 0.05f, 50.0f, Entry.ScaleZ, InOutActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    return CursorY;
}

float ConstructMaterialSection(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    GeometrySurfaceBinding& Binding,
    uint32_t                ControlOriginId,
    uint32_t&               InOutActiveDragId,
    const PaneInputReading& Input,
    bool&                   OutMutated) noexcept
{
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    char SlotTxt[24]{};
    std::snprintf(SlotTxt, sizeof(SlotTxt), "Slot %u", static_cast<unsigned>(Binding.MaterialSlotOrdinal));
    const Rectangle2D S { LeftX, TopY, InnerWidth, 344.0f };
    ConstructSectionSurface(Surface, S, "OpenPBR surface material", SlotTxt);
    float Y = TopY + InspectorMetrics::SectionPadTop + 28.0f;
    Y = ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 1u,  "Albedo tint", Binding.AlbedoTint, InOutActiveDragId, Input, OutMutated);
    Y = ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 10u, "Roughness",   "",  "%.2f", 0.04f, 1.0f, Binding.Roughness, InOutActiveDragId, Input, OutMutated);
    Y = ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 11u, "Metallic",    "",  "%.2f", 0.0f,  1.0f, Binding.Metallic,  InOutActiveDragId, Input, OutMutated);
    (void)ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 12u, "Self glow", "x", "%.2f", 0.0f,  4.0f, Binding.SelfGlow,  InOutActiveDragId, Input, OutMutated);
    return S.Bottom() + InspectorMetrics::SectionGap;
}

float ConstructPhysicsSection(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    OutlinerRecordEntry&    Entry,
    const char*             CollisionSummary,
    const PaneInputReading& Input,
    bool&                   OutMutated) noexcept
{
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    const Rectangle2D S { LeftX, TopY, InnerWidth, 256.0f };
    ConstructSectionSurface(Surface, S, "Physics & visibility", CollisionSummary);
    float Y = TopY + InspectorMetrics::SectionPadTop + 28.0f;
    Y = ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Visible in viewport", Entry.VisibilityEnabled, Input, OutMutated);
    Y = ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Cast shadows",        Entry.CastShadows,       Input, OutMutated);
    Y = ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Dynamic motion",      Entry.DynamicMotion,     Input, OutMutated);
    (void)ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Lock transform",    Entry.Locked,            Input, OutMutated);
    return S.Bottom() + InspectorMetrics::SectionGap;
}

float ConstructMetadataRow(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       InnerWidth,
    const char* Label,
    const char* ReadingText) noexcept
{
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX, TopY }, 11.0f, InspectorMetrics::MutedInk, Label);
    const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 11.0f, ReadingText);
    const float MaxW = std::max(40.0f, InnerWidth - 96.0f);
    const float TextX = LeftX + InnerWidth - std::min(RW, MaxW);
    WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Medium, { TextX, TopY }, MaxW, 11.0f, ColorRgba{ 220u, 222u, 228u, 255u }, ReadingText);
    return TopY + 24.0f;
}

bool ConstructSectionActionButton(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    const char*             Label,
    const PaneInputReading& Input,
    bool                    Accent) noexcept
{
    const Rectangle2D Btn { LeftX, TopY, InnerWidth, 30.0f };
    const Point2D     Cur { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool        Hov = Btn.Contains(Cur);
    const ColorRgba   Bg  = Accent
        ? (Hov ? ColorRgba{ 42u, 62u, 88u, 255u } : ColorRgba{ 32u, 46u, 64u, 255u })
        : (Hov ? ColorRgba{ 48u, 48u, 54u, 255u } : ColorRgba{ 34u, 34u, 38u, 255u });
    const ColorRgba   Bdr = Accent ? ColorRgba{ 138u, 184u, 240u, 72u } : ColorRgba{ 255u, 255u, 255u, 20u };
    const ColorRgba   Ink = Accent ? ColorRgba{ 196u, 224u, 255u, 255u } : InspectorMetrics::HeadingInk;
    Surface.FillRectangle(Btn, Bg, 12.0f);
    Surface.StrokeRectangle(Btn, Bdr, 12.0f, 1.0f);
    const float LW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 11.5f, Label);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Btn.Center().X - LW * 0.5f, Btn.Y + 8.0f }, 11.5f, Ink, Label);
    return Hov && Input.PrimaryPointer.Pressed;
}

float ConstructSplitMetricReadout(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    const char* WholePart,
    const char* FractionalPart,
    const char* UnitText,
    const char* CaptionText) noexcept
{
    const float BigPx = 30.0f;
    float X = LeftX;
    if (WholePart != nullptr && WholePart[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { X, TopY }, BigPx, ColorRgba{ 244u, 246u, 250u, 255u }, WholePart);
        X += WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, BigPx, WholePart);
    }
    if (FractionalPart != nullptr && FractionalPart[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { X, TopY }, BigPx, ColorRgba{ 118u, 122u, 130u, 255u }, FractionalPart);
        X += WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, BigPx, FractionalPart);
    }
    if (UnitText != nullptr && UnitText[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { X + 5.0f, TopY + 13.0f }, 12.0f, ColorRgba{ 166u, 170u, 178u, 255u }, UnitText);
    }
    float NextY = TopY + 36.0f;
    if (CaptionText != nullptr && CaptionText[0] != '\0')
    {
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { LeftX, NextY }, 272.0f, 10.2f, InspectorMetrics::DimInk, CaptionText);
        NextY += 19.0f;
    }
    return NextY;
}

void ConstructStatTile(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    const char* TopCaption,
    const char* PrimaryReading,
    const char* UnitSuffix,
    bool        CaptionBelow) noexcept
{
    Surface.FillRectangle(Bounds, ColorRgba{ 18u, 19u, 22u, 255u }, 11.0f);
    Surface.StrokeRectangle(Bounds, ColorRgba{ 255u, 255u, 255u, 12u }, 11.0f, 1.0f);

    const float PadX = 11.0f;
    if (CaptionBelow)
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Bounds.X + PadX, Bounds.Y + 10.0f }, 14.0f, ColorRgba{ 240u, 242u, 246u, 255u }, PrimaryReading);
        if (UnitSuffix != nullptr && UnitSuffix[0] != '\0')
        {
            const float PW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 14.0f, PrimaryReading);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + PadX + PW + 3.0f, Bounds.Y + 14.0f }, 9.8f, ColorRgba{ 142u, 146u, 154u, 255u }, UnitSuffix);
        }
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Medium, { Bounds.X + PadX, Bounds.Bottom() - 18.0f }, Bounds.Width - PadX * 2.0f, 8.8f, ColorRgba{ 122u, 126u, 134u, 255u }, TopCaption);
    }
    else
    {
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Medium, { Bounds.X + PadX, Bounds.Y + 9.0f }, Bounds.Width - PadX * 2.0f, 8.6f, ColorRgba{ 122u, 126u, 134u, 255u }, TopCaption);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Bounds.X + PadX, Bounds.Y + 25.0f }, 13.0f, ColorRgba{ 240u, 242u, 246u, 255u }, PrimaryReading);
        if (UnitSuffix != nullptr && UnitSuffix[0] != '\0')
        {
            const float PW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 13.0f, PrimaryReading);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + PadX + PW + 3.0f, Bounds.Y + 28.0f }, 9.6f, ColorRgba{ 142u, 146u, 154u, 255u }, UnitSuffix);
        }
    }
}

float ConstructDualStatusBoxes(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       InnerWidth,
    const char* LeftLabel,
    const char* LeftCount,
    ColorRgba   LeftAccent,
    bool        LeftCheckMark,
    const char* RightLabel,
    const char* RightCount,
    ColorRgba   RightAccent,
    bool        RightExclaimMark) noexcept
{
    const float Gap  = 10.0f;
    const float BoxW = std::floor((InnerWidth - Gap) * 0.5f);
    const float BoxH = 88.0f;

    auto PaintBox = [&](Rectangle2D B, const char* Lab, const char* Num, ColorRgba Accent, bool CheckIcon, bool ExclaimIcon)
    {
        Surface.FillRectangle(B, InspectorMetrics::SectionFill, 16.0f);
        Surface.StrokeRectangle(B, InspectorMetrics::SectionBorder, 16.0f, 1.0f);
        float TextX = B.X + 14.0f;
        if (CheckIcon)
        {
            const Point2D C { B.X + 21.0f, B.Y + 21.0f };
            Surface.FillCircle(C, 8.0f, Accent);
            Surface.StrokeLine({ C.X - 3.2f, C.Y + 0.2f }, { C.X - 0.8f, C.Y + 2.8f }, ColorRgba{ 14u, 24u, 18u, 255u }, 1.6f);
            Surface.StrokeLine({ C.X - 0.8f, C.Y + 2.8f }, { C.X + 3.6f, C.Y - 2.4f }, ColorRgba{ 14u, 24u, 18u, 255u }, 1.6f);
            TextX = B.X + 36.0f;
        }
        else if (ExclaimIcon)
        {
            const Rectangle2D R { B.X + 14.0f, B.Y + 13.0f, 16.0f, 16.0f };
            Surface.FillRectangle(R, Accent, 4.5f);
            Surface.StrokeLine({ R.Center().X, R.Y + 3.5f }, { R.Center().X, R.Y + 9.2f }, ColorRgba{ 24u, 14u, 14u, 255u }, 1.7f);
            Surface.FillCircle({ R.Center().X, R.Y + 12.2f }, 1.0f, ColorRgba{ 24u, 14u, 14u, 255u });
            TextX = B.X + 36.0f;
        }
        else
        {
            Surface.FillCircle({ B.X + 18.0f, B.Y + 21.0f }, 3.5f, Accent);
            TextX = B.X + 28.0f;
        }
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { TextX, B.Y + 15.0f }, B.Right() - TextX - 8.0f, 10.6f, ColorRgba{ 208u, 212u, 218u, 255u }, Lab);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { B.X + 14.0f, B.Y + 42.0f }, 28.0f, ColorRgba{ 244u, 246u, 250u, 255u }, Num);
    };

    PaintBox({ LeftX, TopY, BoxW, BoxH }, LeftLabel, LeftCount, LeftAccent, LeftCheckMark, false);
    PaintBox({ LeftX + BoxW + Gap, TopY, BoxW, BoxH }, RightLabel, RightCount, RightAccent, false, RightExclaimMark);
    return TopY + BoxH + InspectorMetrics::SectionGap;
}

float ConstructSegmentTrack(
    PixelSpace&                  Surface,
    float                        LeftX,
    float                        TopY,
    float                        InnerWidth,
    const char*                  UppercaseCaption,
    std::span<const char* const> Choices,
    uint32_t                     ActiveIndex,
    const PaneInputReading&      Input,
    int32_t&                     OutChosenIndex) noexcept
{
    OutChosenIndex = -1;
    float Y = TopY;
    if (UppercaseCaption != nullptr && UppercaseCaption[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { LeftX, Y }, 9.8f, ColorRgba{ 148u, 155u, 165u, 255u }, UppercaseCaption);
        Y += 16.0f;
    }

    const float TrackH = 34.0f;
    const Rectangle2D Track { LeftX, Y, InnerWidth, TrackH };
    Surface.FillRectangle(Track, ColorRgba{ 18u, 18u, 19u, 255u }, TrackH * 0.5f);
    Surface.StrokeRectangle(Track, ColorRgba{ 255u, 255u, 255u, 14u }, TrackH * 0.5f, 1.0f);

    const uint32_t Count = std::max(1u, static_cast<uint32_t>(Choices.size()));
    const float    Pad   = 3.0f;
    const float    Gap   = 3.0f;
    const float    SegW  = (InnerWidth - Pad * 2.0f - Gap * static_cast<float>(Count - 1u)) / static_cast<float>(Count);
    const float    SegH  = TrackH - Pad * 2.0f;
    const float    FontPx = (Count >= 5u) ? 9.4f : 10.5f;
    const Point2D  Cur { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    for (uint32_t I = 0u; I < Count; ++I)
    {
        const Rectangle2D Seg { LeftX + Pad + static_cast<float>(I) * (SegW + Gap), Y + Pad, SegW, SegH };
        const bool Active = (I == ActiveIndex);
        const bool Hov    = Seg.Contains(Cur);
        if (Active)
        {
            Surface.FillRectangle(Seg, ColorRgba{ 230u, 230u, 230u, 255u }, SegH * 0.5f);
        }
        else if (Hov)
        {
            Surface.FillRectangle(Seg, ColorRgba{ 36u, 37u, 41u, 255u }, SegH * 0.5f);
        }
        const ColorRgba Ink = Active ? ColorRgba{ 20u, 20u, 20u, 255u } : (Hov ? ColorRgba{ 220u, 220u, 220u, 255u } : ColorRgba{ 154u, 154u, 154u, 255u });
        const FontWeightClass Wgt = Active ? FontWeightClass::Medium : FontWeightClass::Regular;
        const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, Wgt, FontPx, Choices[I]);
        const float LabelOriginX = std::max(Seg.X + 4.0f, Seg.Center().X - TW * 0.5f);
        WorkspaceAppearance::ConstructTruncatedText(Surface, Wgt, { LabelOriginX, Seg.Center().Y - FontPx * 0.52f }, Seg.Width - 6.0f, FontPx, Ink, Choices[I]);
        if (Hov && Input.PrimaryPointer.Pressed)
        {
            OutChosenIndex = static_cast<int32_t>(I);
        }
    }
    return Track.Bottom() + 12.0f;
}

float ConstructNavigationRow(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    const char*             LeftLabel,
    const char*             RightSummary,
    bool                    ShowBottomDivider,
    const PaneInputReading& Input,
    bool&                   OutClicked) noexcept
{
    OutClicked = false;
    const Rectangle2D Row { LeftX, TopY, InnerWidth, 30.0f };
    const Point2D     Cur { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool        Hov = Row.Contains(Cur);
    if (Hov)
    {
        Surface.FillRectangle({ Row.X - 6.0f, Row.Y + 1.0f, Row.Width + 12.0f, Row.Height - 2.0f }, ColorRgba{ 255u, 255u, 255u, 10u }, 8.0f);
    }

    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX, TopY + 8.0f }, 10.8f, ColorRgba{ 150u, 154u, 162u, 255u }, LeftLabel);

    // Right chevron ">"
    const float ChevX = LeftX + InnerWidth - 7.0f;
    const float ChevY = TopY + 15.0f;
    const ColorRgba ChevInk = Hov ? ColorRgba{ 235u, 238u, 244u, 255u } : ColorRgba{ 126u, 130u, 138u, 255u };
    Surface.StrokeLine({ ChevX - 3.5f, ChevY - 4.0f }, { ChevX + 1.0f, ChevY }, ChevInk, 1.3f);
    Surface.StrokeLine({ ChevX + 1.0f, ChevY }, { ChevX - 3.5f, ChevY + 4.0f }, ChevInk, 1.3f);

    const float MaxRightW = std::max(40.0f, InnerWidth * 0.66f);
    const float RW = std::min(MaxRightW, WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.8f, RightSummary));
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Medium, { ChevX - 10.0f - RW, TopY + 8.0f }, MaxRightW, 10.8f, ColorRgba{ 234u, 236u, 240u, 255u }, RightSummary);

    if (ShowBottomDivider)
    {
        Surface.StrokeLine({ LeftX, Row.Bottom() }, { LeftX + InnerWidth, Row.Bottom() }, ColorRgba{ 255u, 255u, 255u, 14u }, 1.0f);
    }

    if (Hov && Input.PrimaryPointer.Pressed)
    {
        OutClicked = true;
    }
    return Row.Bottom();
}

Mathematics::LinearRGB ComputeKelvinRgb(float Kelvin) noexcept
{
    const float Heat = std::clamp(Kelvin, 1000.0f, 40000.0f) * 0.01f;
    float R = 255.0f, G = 255.0f, B = 255.0f;
    if (Heat > 66.0f)
    {
        R = 329.6987f * std::pow(Heat - 60.0f, -0.1332f);
        G = 288.1222f * std::pow(Heat - 60.0f, -0.0755f);
    }
    else
    {
        G = 99.4708f * std::log(std::max(1.0f, Heat)) - 161.1196f;
        B = (Heat <= 19.0f) ? 0.0f : (138.5177f * std::log(std::max(1.0f, Heat - 10.0f)) - 305.0448f);
    }
    return {
        std::clamp(R / 255.0f, 0.0f, 1.0f),
        std::clamp(G / 255.0f, 0.0f, 1.0f),
        std::clamp(B / 255.0f, 0.0f, 1.0f),
    };
}

float ConstructLuminaireShadowsSection(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    OutlinerRecordEntry&    Entry,
    bool                    SupportsShadowCasting,
    const char*             HelperToggleLabel,
    bool&                   InOutHelperVisible,
    const PaneInputReading& Input,
    bool&                   OutMutated) noexcept
{
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    const float H     = SupportsShadowCasting ? 204.0f : 164.0f;
    const Rectangle2D S { LeftX, TopY, InnerWidth, H };
    ConstructSectionSurface(Surface, S, "Scene participation");
    float Y = TopY + InspectorMetrics::SectionPadTop + 28.0f;
    if (SupportsShadowCasting)
    {
        Y = ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Cast shadows", Entry.CastShadows, Input, OutMutated);
    }
    Y = ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, HelperToggleLabel, InOutHelperVisible, Input, OutMutated);
    (void)ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Lock luminaire", Entry.Locked, Input, OutMutated);
    return S.Bottom() + InspectorMetrics::SectionGap;
}

float ConstructUvWorkspaceSection(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       InnerWidth,
    uint32_t    UvChannelCount,
    const char* ProjectionLabel) noexcept
{
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;
    const Rectangle2D S { LeftX, TopY, InnerWidth, 276.0f };
    ConstructSectionSurface(Surface, S, "UV layout", ProjectionLabel);
    float Y = TopY + InspectorMetrics::SectionPadTop + 28.0f;

    const Rectangle2D Well { LeftX + PadX, Y, BodyW, 132.0f };
    Surface.FillRectangle(Well, ColorRgba{ 19u, 20u, 24u, 255u }, 10.0f);
    Surface.PushClip(Well);
    // Subtle 8x4 UV tile checker + packed UV island outlines
    const float CellW = Well.Width / 8.0f, CellH = Well.Height / 4.0f;
    for (int R = 0; R < 4; ++R)
    {
        for (int C = 0; C < 8; ++C)
        {
            if (((R + C) & 1) == 0)
            {
                Surface.FillRectangle({ Well.X + static_cast<float>(C) * CellW, Well.Y + static_cast<float>(R) * CellH, CellW, CellH }, ColorRgba{ 255u, 255u, 255u, 8u }, 0.0f);
            }
        }
    }
    const std::array<Rectangle2D, 3u> Islands = {{
        { Well.X + 14.0f, Well.Y + 14.0f, Well.Width * 0.42f, Well.Height * 0.56f },
        { Well.X + Well.Width * 0.50f, Well.Y + 14.0f, Well.Width * 0.42f, Well.Height * 0.34f },
        { Well.X + Well.Width * 0.50f, Well.Y + Well.Height * 0.54f, Well.Width * 0.38f, Well.Height * 0.32f },
    }};
    for (const auto& Isl : Islands)
    {
        Surface.FillRectangle(Isl, ColorRgba{ 122u, 184u, 255u, 42u }, 4.0f);
        Surface.StrokeRectangle(Isl, ColorRgba{ 146u, 202u, 255u, 215u }, 4.0f, 1.2f);
    }
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Well.X + 8.0f, Well.Bottom() - 14.0f }, 8.0f, InspectorMetrics::DimInk, "UV [0..1] ATLAS · PACKED ISLANDS");
    Surface.PopClip();
    Surface.StrokeRectangle(Well, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);
    Y = Well.Bottom() + 12.0f;

    char ChBuf[16]{};
    std::snprintf(ChBuf, sizeof(ChBuf), "%u %s", UvChannelCount, UvChannelCount == 1u ? "set" : "sets");
    Y = ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "UV channels", ChBuf);
    Y = ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Texel packing", "84% efficiency");
    (void)ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Projection", ProjectionLabel);
    return S.Bottom() + InspectorMetrics::SectionGap;
}

float ConstructFractureWorkspaceSection(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       InnerWidth,
    uint32_t    CellCount,
    const char* ModeLabel) noexcept
{
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;
    const Rectangle2D S { LeftX, TopY, InnerWidth, 276.0f };
    ConstructSectionSurface(Surface, S, "Fracture cells", ModeLabel);
    float Y = TopY + InspectorMetrics::SectionPadTop + 28.0f;

    const Rectangle2D Well { LeftX + PadX, Y, BodyW, 132.0f };
    Surface.FillRectangle(Well, ColorRgba{ 20u, 19u, 22u, 255u }, 10.0f);
    Surface.PushClip(Well);
    const Point2D C = Well.Center();
    // Stylized Voronoi fracture cell shards radiating around impact centre
    const std::array<Point2D, 7u> Ring = {{
        { C.X - 46.0f, C.Y - 28.0f }, { C.X - 4.0f,  C.Y - 40.0f }, { C.X + 44.0f, C.Y - 24.0f },
        { C.X + 54.0f, C.Y + 14.0f }, { C.X + 12.0f, C.Y + 38.0f }, { C.X - 36.0f, C.Y + 30.0f },
        { C.X - 56.0f, C.Y + 2.0f  },
    }};
    for (std::size_t I = 0u; I < Ring.size(); ++I)
    {
        const Point2D& A = Ring[I];
        const Point2D& B = Ring[(I + 1u) % Ring.size()];
        const std::array<Point2D, 3u> Tri = {{ C, A, B }};
        const uint8_t Shade = static_cast<uint8_t>(26 + (I % 3u) * 14);
        Surface.FillPolygon(Tri, ColorRgba{ static_cast<uint8_t>(Shade + 18u), Shade, static_cast<uint8_t>(Shade + 4u), 165u });
        Surface.StrokePolyline(Tri, ColorRgba{ 246u, 118u, 118u, 175u }, 1.1f, true);
    }
    Surface.FillCircle(C, 3.0f, ColorRgba{ 255u, 138u, 138u, 255u });
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Well.X + 8.0f, Well.Bottom() - 14.0f }, 8.0f, InspectorMetrics::DimInk, "VORONOI SHARDS · INTERIOR SEAMS");
    Surface.PopClip();
    Surface.StrokeRectangle(Well, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);
    Y = Well.Bottom() + 12.0f;

    char CellsBuf[20]{};
    std::snprintf(CellsBuf, sizeof(CellsBuf), "%u pieces", CellCount);
    Y = ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Fracture cells", CellsBuf);
    Y = ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Interior roughness", "0.78");
    (void)ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Mode", ModeLabel);
    return S.Bottom() + InspectorMetrics::SectionGap;
}

float ConstructSdfWorkspaceSection(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       InnerWidth,
    bool        SdfActive,
    const char* BoundsLabel) noexcept
{
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;
    const Rectangle2D S { LeftX, TopY, InnerWidth, 276.0f };
    ConstructSectionSurface(Surface, S, "Signed distance field", SdfActive ? "Baked" : "Analytic");
    float Y = TopY + InspectorMetrics::SectionPadTop + 28.0f;

    const Rectangle2D Well { LeftX + PadX, Y, BodyW, 132.0f };
    Surface.FillRectangle(Well, ColorRgba{ 16u, 22u, 21u, 255u }, 10.0f);
    Surface.PushClip(Well);
    const Point2D C = Well.Center();
    // Concentric SDF iso-distance contours (interior teal, zero iso-surface bright sage, exterior fading)
    for (int Ring = 5; Ring >= 1; --Ring)
    {
        const float Rad = 12.0f + static_cast<float>(Ring) * 11.5f;
        const bool IsZero = (Ring == 2);
        const ColorRgba StrokeCol = IsZero ? ColorRgba{ 156u, 224u, 188u, 240u }
                                           : (Ring < 2 ? ColorRgba{ 106u, 186u, 162u, 150u }
                                                       : ColorRgba{ 138u, 188u, 168u, static_cast<uint8_t>(110 - Ring * 14) });
        Surface.StrokeCircle(C, Rad, StrokeCol, IsZero ? 1.8f : 1.0f);
    }
    Surface.FillCircle(C, 22.0f, ColorRgba{ 118u, 198u, 166u, 42u });
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Well.X + 8.0f, Well.Bottom() - 14.0f }, 8.0f, InspectorMetrics::DimInk, "ISO-DISTANCE SLICE · ZERO CONTOUR");
    Surface.PopClip();
    Surface.StrokeRectangle(Well, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);
    Y = Well.Bottom() + 12.0f;

    Y = ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Local SDF field", SdfActive ? "Active (64^3)" : "Analytic exact");
    Y = ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Bounds", BoundsLabel);
    (void)ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Raymarch AO", "Enabled");
    return S.Bottom() + InspectorMetrics::SectionGap;
}

float ConstructLodWorkspaceSection(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       InnerWidth,
    uint32_t    TriangleCount,
    uint32_t    ClusterCount) noexcept
{
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;
    const Rectangle2D S { LeftX, TopY, InnerWidth, 276.0f };
    ConstructSectionSurface(Surface, S, "Cluster LOD pyramid", "4 levels");
    float Y = TopY + InspectorMetrics::SectionPadTop + 28.0f;

    const Rectangle2D Well { LeftX + PadX, Y, BodyW, 132.0f };
    Surface.FillRectangle(Well, ColorRgba{ 21u, 20u, 19u, 255u }, 10.0f);
    Surface.PushClip(Well);
    static constexpr std::array<float, 4u> Fracs = {{ 1.00f, 0.52f, 0.24f, 0.09f }};
    static constexpr std::array<const char*, 4u> Names = {{ "LOD 0", "LOD 1", "LOD 2", "LOD 3" }};
    const float BarAreaW = Well.Width - 68.0f;
    for (std::size_t I = 0u; I < 4u; ++I)
    {
        const float RowY = Well.Y + 14.0f + static_cast<float>(I) * 26.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Well.X + 10.0f, RowY + 2.0f }, 8.8f, InspectorMetrics::MutedInk, Names[I]);
        const float BW = std::max(10.0f, BarAreaW * Fracs[I]);
        Surface.FillRectangle({ Well.X + 52.0f, RowY, BarAreaW, 14.0f }, ColorRgba{ 32u, 32u, 36u, 255u }, 4.0f);
        Surface.FillRectangle({ Well.X + 52.0f, RowY, BW, 14.0f }, ColorRgba{ 237u, 193u, 129u, static_cast<uint8_t>(220 - I * 36) }, 4.0f);
    }
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Well.X + 10.0f, Well.Bottom() - 14.0f }, 8.0f, InspectorMetrics::DimInk, "CLUSTER DECIMATION · PIXEL ERROR");
    Surface.PopClip();
    Surface.StrokeRectangle(Well, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);
    Y = Well.Bottom() + 12.0f;

    char TriBuf[24]{}, ClsBuf[24]{};
    std::snprintf(TriBuf, sizeof(TriBuf), "%u tris", TriangleCount);
    std::snprintf(ClsBuf, sizeof(ClsBuf), "%u clusters", ClusterCount);
    Y = ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Base triangles (LOD 0)", TriBuf);
    Y = ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Meshlet clusters", ClsBuf);
    (void)ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "LOD transition", "Screen-space error");
    return S.Bottom() + InspectorMetrics::SectionGap;
}

void ConstructDashedSegment(
    PixelSpace& Surface,
    Point2D     Start,
    Point2D     End,
    ColorRgba   Ink,
    float       DashLength,
    float       GapLength,
    float       StrokeWidth) noexcept
{
    const float DX  = End.X - Start.X;
    const float DY  = End.Y - Start.Y;
    const float Len = std::sqrt(DX * DX + DY * DY);
    if (Len < 0.5f) return;
    const float UX = DX / Len;
    const float UY = DY / Len;
    float Pos = 0.0f;
    while (Pos < Len)
    {
        const float SegEnd = std::min(Len, Pos + DashLength);
        Surface.StrokeLine(
            { Start.X + UX * Pos, Start.Y + UY * Pos },
            { Start.X + UX * SegEnd, Start.Y + UY * SegEnd },
            Ink, StrokeWidth);
        Pos += DashLength + GapLength;
    }
}

float ConstructWrappedParagraph(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       MaxWidth,
    float       FontSize,
    float       LineStep,
    ColorRgba   Ink,
    const char* Text) noexcept
{
    if (Text == nullptr || Text[0] == '\0') return TopY;
    std::string_view Rem(Text);
    std::string Line;
    float Y = TopY;
    while (!Rem.empty())
    {
        while (!Rem.empty() && Rem.front() == ' ') Rem.remove_prefix(1u);
        if (Rem.empty()) break;
        const std::size_t Sp = Rem.find(' ');
        const std::string_view Word = (Sp == std::string_view::npos) ? Rem : Rem.substr(0u, Sp);
        Rem = (Sp == std::string_view::npos) ? std::string_view{} : Rem.substr(Sp + 1u);

        std::string Candidate = Line.empty() ? std::string(Word) : (Line + " " + std::string(Word));
        if (!Line.empty() && WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, FontSize, Candidate) > MaxWidth)
        {
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX, Y }, FontSize, Ink, Line);
            Y += LineStep;
            Line.assign(Word);
        }
        else
        {
            Line = std::move(Candidate);
        }
    }
    if (!Line.empty())
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX, Y }, FontSize, Ink, Line);
        Y += LineStep;
    }
    return Y;
}

float ConstructLivePlot(
    PixelSpace&                                Surface,
    float                                      LeftX,
    float                                      TopY,
    float                                      InnerWidth,
    uint32_t                                   ControlId,
    float                                      DomainMin,
    float                                      DomainMax,
    float                                      RangeMin,
    float                                      RangeMax,
    std::span<const InspectorLivePlotSeries>   Series,
    float&                                     InOutProbeX,
    const char*                                UnitX,
    const char*                                UnitY,
    const char*                                NoteText,
    uint32_t&                                  InOutActiveDragId,
    const PaneInputReading&                    Input,
    bool&                                      OutChanged) noexcept
{
    auto FormatCompact = [](float Num, char* Buf, std::size_t Cap)
    {
        const float AbsN = std::fabs(Num);
        if (AbsN > 0.0f && AbsN < 0.001f)
        {
            std::snprintf(Buf, Cap, "%.2e", Num);
            return;
        }
        std::snprintf(Buf, Cap, "%.3f", Num);
        // Strip trailing zeros & decimal point to match JS toLocaleString({maximumFractionDigits:3})
        char* Dot = std::strchr(Buf, '.');
        if (Dot != nullptr)
        {
            char* End = Buf + std::strlen(Buf) - 1;
            while (End > Dot && *End == '0') *End-- = '\0';
            if (End == Dot) *End = '\0';
        }
    };

    const float DSpan = std::max(1.0e-6f, DomainMax - DomainMin);
    const float RSpan = std::max(1.0e-6f, RangeMax - RangeMin);
    const float ProbeX = std::clamp(InOutProbeX, DomainMin, DomainMax);

    // 1. Live plot header readout (left: ProbeX UnitX at 22px Light; right: baseline-aligned colored series readouts)
    char ProbeBuf[32]{};
    FormatCompact(ProbeX, ProbeBuf, sizeof(ProbeBuf));
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { LeftX, TopY + 2.0f }, 22.0f, ColorRgba{ 238u, 238u, 238u, 255u }, ProbeBuf);
    const float PW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 22.0f, ProbeBuf);
    if (UnitX != nullptr && UnitX[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + PW + 5.0f, TopY + 11.5f }, 11.2f, ColorRgba{ 138u, 140u, 134u, 255u }, UnitX);
    }

    float SeriesY = TopY + 11.5f;
    for (const auto& Line : Series)
    {
        if (!Line.Sample) continue;
        char ValBuf[32]{}, FullBuf[96]{};
        FormatCompact(Line.Sample(ProbeX), ValBuf, sizeof(ValBuf));
        if (UnitY != nullptr && UnitY[0] != '\0')
            std::snprintf(FullBuf, sizeof(FullBuf), "%s %s %s", Line.Name, ValBuf, UnitY);
        else
            std::snprintf(FullBuf, sizeof(FullBuf), "%s %s", Line.Name, ValBuf);
        const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.2f, FullBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + InnerWidth - RW, SeriesY }, 11.2f, Line.Ink, FullBuf);
        SeriesY += 15.0f;
    }

    // 2. Exact LiveGraph.jsx SVG area (width = InnerWidth, height = 215px -> SVG top at TopY + 28px)
    const float SvgY = TopY + 28.0f;
    const Rectangle2D SvgRect { LeftX, SvgY, InnerWidth, 215.0f };
    const float LeftPad  = 42.0f;
    const float RightPad = 18.0f;
    const float PlotW    = std::max(40.0f, InnerWidth - LeftPad - RightPad);
    auto PX = [&](float V) noexcept -> float { return LeftX + LeftPad + ((V - DomainMin) / DSpan) * PlotW; };
    auto PY = [&](float V) noexcept -> float { return SvgY + 174.0f - ((std::clamp(V, RangeMin, RangeMax) - RangeMin) / RSpan) * 146.0f; };

    // Pointer scrubbing across plot domain
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    if (SvgRect.Contains(Cursor) && Input.PrimaryPointer.Pressed) InOutActiveDragId = ControlId;
    if (InOutActiveDragId == ControlId && Input.PrimaryPointer.Down)
    {
        const float Frac = std::clamp((Cursor.X - (LeftX + LeftPad)) / PlotW, 0.0f, 1.0f);
        const float Next = DomainMin + Frac * DSpan;
        if (std::fabs(Next - InOutProbeX) > 1.0e-5f)
        {
            InOutProbeX = Next;
            OutChanged  = true;
        }
    }
    else if (InOutActiveDragId == ControlId && !Input.PrimaryPointer.Down)
    {
        InOutActiveDragId = 0u;
    }

    // 5 horizontal dashed grid lines & Y-axis labels
    const ColorRgba AxisInk { 142u, 145u, 139u, 255u };
    for (int Tick = 0; Tick <= 4; ++Tick)
    {
        const float T = static_cast<float>(Tick) * 0.25f;
        const float Y = SvgY + 28.0f + 146.0f * T;
        ConstructDashedSegment(Surface, { LeftX + LeftPad, Y }, { LeftX + InnerWidth - RightPad, Y }, ColorRgba{ 255u, 255u, 255u, 22u }, 4.0f, 7.0f, 1.0f);
        char YBuf[24]{};
        FormatCompact(RangeMax - RSpan * T, YBuf, sizeof(YBuf));
        const float YW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, YBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + 35.0f - YW, Y - 5.0f }, 10.5f, AxisInk, YBuf);
    }

    // Curves + subtle shaded area under series 0
    constexpr std::size_t Steps = 81u;
    std::array<Point2D, Steps> CurvePts{};
    const float BaseY = SvgY + 174.0f;
    for (std::size_t SIdx = 0u; SIdx < Series.size(); ++SIdx)
    {
        const auto& Line = Series[SIdx];
        if (!Line.Sample) continue;
        for (std::size_t I = 0u; I < Steps; ++I)
        {
            const float Xv = DomainMin + DSpan * (static_cast<float>(I) / static_cast<float>(Steps - 1u));
            CurvePts[I] = { PX(Xv), PY(Line.Sample(Xv)) };
        }
        if (SIdx == 0u)
        {
            // Smooth 24-band vertical gradient fill (0.28 -> 0.02 matching LiveGraph.jsx linearGradient)
            for (std::size_t I = 0u; I + 1u < Steps; ++I)
            {
                const Point2D P0 = CurvePts[I];
                const Point2D P1 = CurvePts[I + 1u];
                constexpr int GradBands = 24;
                for (int B = 0; B < GradBands; ++B)
                {
                    const float F0 = static_cast<float>(B) / static_cast<float>(GradBands);
                    const float F1 = static_cast<float>(B + 1) / static_cast<float>(GradBands);
                    const float Y0L = P0.Y + (BaseY - P0.Y) * F0;
                    const float Y1L = P0.Y + (BaseY - P0.Y) * F1;
                    const float Y0R = P1.Y + (BaseY - P1.Y) * F0;
                    const float Y1R = P1.Y + (BaseY - P1.Y) * F1;
                    const float AlphaF = (1.0f - 0.5f * (F0 + F1));
                    const uint8_t A = static_cast<uint8_t>(std::clamp(30.0f * AlphaF + 2.0f, 1.0f, 36.0f));
                    const std::array<Point2D, 4u> Quad = {{ { P0.X, Y0L }, { P1.X, Y0R }, { P1.X, Y1R }, { P0.X, Y1L } }};
                    Surface.FillPolygon(Quad, Line.Ink.WithAlpha(A));
                }
            }
        }
        Surface.StrokePolyline(CurvePts, Line.Ink, 1.6f, false);
        Surface.FillCircle({ PX(ProbeX), PY(Line.Sample(ProbeX)) }, 4.0f, Line.Ink);
    }

    // Vertical dashed probe line
    ConstructDashedSegment(Surface, { PX(ProbeX), SvgY + 28.0f }, { PX(ProbeX), SvgY + 174.0f }, ColorRgba{ 229u, 215u, 189u, 119u }, 3.0f, 5.0f, 1.0f);

    // 3 X-axis labels at y = SvgY + 190
    for (int Tick = 0; Tick <= 2; ++Tick)
    {
        const float T = static_cast<float>(Tick) * 0.5f;
        char XBuf[32]{}, XLbl[48]{};
        FormatCompact(DomainMin + DSpan * T, XBuf, sizeof(XBuf));
        if (UnitX != nullptr && UnitX[0] != '\0') std::snprintf(XLbl, sizeof(XLbl), "%s %s", XBuf, UnitX);
        else                                       std::snprintf(XLbl, sizeof(XLbl), "%s", XBuf);
        const float XW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, XLbl);
        const float Cx = LeftX + LeftPad + PlotW * T;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Cx - XW * 0.5f, SvgY + 191.0f }, 10.5f, AxisInk, XLbl);
    }

    float NextY = SvgRect.Bottom() + 12.0f;
    if (NoteText != nullptr && NoteText[0] != '\0')
    {
        NextY = ConstructWrappedParagraph(Surface, LeftX, NextY, InnerWidth, 12.4f, 16.4f, ColorRgba{ 136u, 141u, 135u, 255u }, NoteText);
        NextY += 8.0f;
    }
    return NextY;
}

float ConstructBakeImageCard(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    bool                    UseBakedImage,
    const char*             StatusCaption,
    const char*             RightCaption,
    const char*             FooterNote,
    const PaneInputReading& Input,
    bool&                   OutBakeClicked,
    bool&                   OutLoadClicked) noexcept
{
    OutBakeClicked = false;
    OutLoadClicked = false;
    const Rectangle2D S { LeftX, TopY, InnerWidth, 240.5f };
    ConstructSectionSurface(Surface, S, "Bake / image");

    const float BodyX = LeftX + 25.0f;
    const float BodyW = InnerWidth - 50.0f;
    const float Gap   = 12.0f;
    const float HalfW = std::floor((BodyW - Gap) * 0.5f);
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    // Left tall tile: "Use baked image" + UNAVAILABLE/READY badge
    const Rectangle2D Tile { BodyX, S.Y + 64.0f, HalfW, 114.0f };
    Surface.FillRectangle(Tile, ColorRgba{ 30u, 30u, 30u, 255u }, 14.0f);
    Surface.StrokeRectangle(Tile, ColorRgba{ 255u, 255u, 255u, 16u }, 14.0f, 1.0f);

    // Download/tray icon badge centered at top of tile
    const Rectangle2D Badge { Tile.Center().X - 18.0f, Tile.Y + 14.0f, 36.0f, 36.0f };
    Surface.FillRectangle(Badge, UseBakedImage ? ColorRgba{ 34u, 72u, 48u, 255u } : ColorRgba{ 46u, 46u, 46u, 255u }, 9.0f);
    const Point2D Bc = Badge.Center();
    const ColorRgba IconInk = UseBakedImage ? ColorRgba{ 98u, 220u, 134u, 255u } : ColorRgba{ 165u, 165u, 165u, 255u };
    Surface.StrokeLine({ Bc.X, Bc.Y - 7.0f }, { Bc.X, Bc.Y + 3.0f }, IconInk, 1.8f);
    Surface.StrokeLine({ Bc.X - 4.0f, Bc.Y - 1.0f }, { Bc.X, Bc.Y + 3.5f }, IconInk, 1.8f);
    Surface.StrokeLine({ Bc.X + 4.0f, Bc.Y - 1.0f }, { Bc.X, Bc.Y + 3.5f }, IconInk, 1.8f);
    Surface.StrokeLine({ Bc.X - 6.0f, Bc.Y + 6.5f }, { Bc.X + 6.0f, Bc.Y + 6.5f }, IconInk, 1.8f);

    const float L1W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.2f, "Use baked");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Tile.Center().X - L1W * 0.5f, Tile.Y + 58.0f }, 11.2f, ColorRgba{ 196u, 196u, 194u, 255u }, "Use baked");
    const float L2W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.2f, "image");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Tile.Center().X - L2W * 0.5f, Tile.Y + 72.0f }, 11.2f, ColorRgba{ 196u, 196u, 194u, 255u }, "image");
    const char* StatusStr = (StatusCaption != nullptr && StatusCaption[0] != '\0') ? StatusCaption : "UNAVAILABLE";
    const float StW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.6f, StatusStr);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Tile.Center().X - StW * 0.5f, Tile.Bottom() - 16.0f }, 8.6f, InspectorMetrics::DimInk, StatusStr);

    // Right column: Bake button + Load image button + caption
    const float RightX = BodyX + HalfW + Gap;
    const Rectangle2D BakeBtn { RightX, S.Y + 64.0f, HalfW, 33.0f };
    RegisterInteractiveRect(Tile);
    RegisterInteractiveRect(BakeBtn);
    RegisterInteractiveRect({ RightX, S.Y + 105.0f, HalfW, 33.0f });
    Surface.FillRectangle(BakeBtn, ColorRgba{ 36u, 36u, 36u, 255u }, 9.0f);
    Surface.StrokeRectangle(BakeBtn, ColorRgba{ 255u, 255u, 255u, 22u }, 9.0f, 1.0f);
    const float BakeW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.2f, "Bake");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BakeBtn.Center().X - BakeW * 0.5f + 6.0f, BakeBtn.Y + 10.5f }, 11.2f, ColorRgba{ 165u, 165u, 165u, 255u }, "Bake");
    if (BakeBtn.Contains(Cursor) && Input.PrimaryPointer.Pressed) OutBakeClicked = true;

    const Rectangle2D LoadBtn { RightX, S.Y + 105.0f, HalfW, 33.0f };
    Surface.FillRectangle(LoadBtn, ColorRgba{ 36u, 36u, 36u, 255u }, 9.0f);
    Surface.StrokeRectangle(LoadBtn, ColorRgba{ 255u, 255u, 255u, 22u }, 9.0f, 1.0f);
    const float LoadW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.2f, "Load image");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LoadBtn.Center().X - LoadW * 0.5f + 4.0f, LoadBtn.Y + 10.5f }, 11.2f, ColorRgba{ 165u, 165u, 165u, 255u }, "Load image");
    if (LoadBtn.Contains(Cursor) && Input.PrimaryPointer.Pressed) OutLoadClicked = true;

    const char* RightText = (RightCaption != nullptr && RightCaption[0] != '\0') ? RightCaption : "No image source connected.";
    (void)ConstructWrappedParagraph(Surface, RightX, S.Y + 148.0f, HalfW, 10.2f, 14.0f, InspectorMetrics::DimInk, RightText);

    // Bottom hairline divider + card footer note
    Surface.StrokeLine({ BodyX, S.Y + 194.0f }, { BodyX + BodyW, S.Y + 194.0f }, ColorRgba{ 255u, 255u, 255u, 16u }, 1.0f);
    const char* Foot = (FooterNote != nullptr && FooterNote[0] != '\0') ? FooterNote : "Renderer / imported-image binding unavailable";
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, S.Y + 206.0f }, 9.8f, InspectorMetrics::MutedInk, Foot);

    return S.Bottom() + InspectorMetrics::SectionGap;
}

void BeginInspectorCaptureScope(
    RetainedCanvasCache* ActiveCache,
    Rectangle2D          ScrollClipLogical,
    Point2D              SurfaceOriginPhysical,
    float                ScaleFactor) noexcept
{
    auto& Scope             = AccessCaptureScope();
    Scope.ActiveCache       = ActiveCache;
    Scope.ScrollClipLogical = ScrollClipLogical;
    Scope.SurfaceOriginPhys = SurfaceOriginPhysical;
    Scope.ScaleFactor       = std::max(0.25f, ScaleFactor);
    Scope.ScrollClipActive  = (ScrollClipLogical.Width > 1.0f && ScrollClipLogical.Height > 1.0f);
    Scope.InteractiveDragNow = false;
}

void EndInspectorCaptureScope() noexcept
{
    auto& Scope            = AccessCaptureScope();
    Scope.ActiveCache      = nullptr;
    Scope.ScrollClipActive = false;
}

void RegisterInteractiveRect(Rectangle2D LogicalBounds) noexcept
{
    auto& Scope = AccessCaptureScope();
    if (Scope.ActiveCache == nullptr || LogicalBounds.Width <= 0.5f || LogicalBounds.Height <= 0.5f)
    {
        return;
    }
    if (Scope.ScrollClipActive && !QueryVisibleInScrollClip(LogicalBounds))
    {
        return;
    }
    const Rectangle2D Physical {
        Scope.SurfaceOriginPhys.X + LogicalBounds.X * Scope.ScaleFactor,
        Scope.SurfaceOriginPhys.Y + LogicalBounds.Y * Scope.ScaleFactor,
        LogicalBounds.Width * Scope.ScaleFactor,
        LogicalBounds.Height * Scope.ScaleFactor,
    };
    Scope.ActiveCache->RegisterInteractiveZone(Physical);
}

bool QueryVisibleInScrollClip(Rectangle2D LogicalBounds) noexcept
{
    const auto& Scope = AccessCaptureScope();
    if (!Scope.ScrollClipActive)
    {
        return true;
    }
    return LogicalBounds.Bottom() >= (Scope.ScrollClipLogical.Y - 8.0f) &&
           LogicalBounds.Y <= (Scope.ScrollClipLogical.Bottom() + 8.0f);
}

void NoteInteractiveDragActive(bool Active) noexcept
{
    if (Active)
    {
        AccessCaptureScope().InteractiveDragNow = true;
    }
}

bool QueryInteractiveDragActive() noexcept
{
    return AccessCaptureScope().InteractiveDragNow;
}

void ConstructInspectorFooter(
    PixelSpace& Surface,
    Rectangle2D FooterBounds,
    const char* LeftCaption,
    const char* RightCaption) noexcept
{
    Surface.FillRectangle(FooterBounds, ColorRgba{ 20u, 20u, 20u, 255u }, 0.0f);
    Surface.StrokeLine({ FooterBounds.X, FooterBounds.Y + 0.5f }, { FooterBounds.Right(), FooterBounds.Y + 0.5f }, ColorRgba{ 28u, 28u, 28u, 255u }, 1.0f);
    const float TextY = FooterBounds.Center().Y - 5.5f;
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { FooterBounds.X + 18.0f, TextY }, FooterBounds.Width - 110.0f, 11.0f, InspectorMetrics::DimInk, LeftCaption);
    const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, RightCaption);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { FooterBounds.Right() - 18.0f - RW, TextY }, 11.0f, InspectorMetrics::DimInk, RightCaption);
}

} // namespace InspectorPrimitives

} // namespace Frontier
