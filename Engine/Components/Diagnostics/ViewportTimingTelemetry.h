/*============================================================================================================================================
                                                        FRAMETIMINGTELEMETRY.H
============================================================================================================================================*/
// 🧩 Per-stage CPU/GPU timing breakdown and debug timing log utilities for Frontier diagnostics

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ViewportTimingBreakdown
{
    uint64_t    FrameIndex          = 0ull;  // [idx] - Monotonic desktop frame index
    uint32_t    RenderWidthPx       = 0u;    // [px]  - Active world render width
    uint32_t    RenderHeightPx      = 0u;    // [px]  - Active world render height
    uint32_t    EngineMode          = 1u;    // [idx] - 0=MatCap, 1=OpenPBR
    uint32_t    ShadowAlgorithm     = 4u;    // [idx] - 0=ContactPCSS, 1=PlanePCSS, 2=SMRT, 3=PCF, 4=RayQuery
    uint32_t    QualityTier         = 2u;    // [idx] - 0=Low..4=Ultra
    bool        HardwareGpuActive   = false; // [-]   - True when running on hardware GPU (discrete/iGPU)
    bool        GpuTimestampsValid  = false; // [-]   - True when VkQueryPool timestamps populated GPU pass times
    bool        WorldDispatched     = false; // [-]   - True when world compute pipeline executed this frame
    std::string AdapterName         { "Unknown" }; // [-] - Active Vulkan adapter name
    std::string TimingLogPath       { "FrontierTiming.log" }; // [-] - Debug log file path

    // End-to-end host frame stages [ms]
    float TotalFrameMs              = 0.0f;  // [ms] - Full frame wall-clock delta
    float DesktopPollAndUiMs        = 0.0f;  // [ms] - GLFW poll + ImGui frame build
    float EditorTickMs              = 0.0f;  // [ms] - Scene/outliner tick before render
    float SceneSyncGpuMs            = 0.0f;  // [ms] - GPU scene upload & shadow cascade sync
    float GizmoOverlayMs            = 0.0f;  // [ms] - 3D Transform gizmo raster & composite
    float SwapchainPresentMs        = 0.0f;  // [ms] - ImGui draw + texture upload + vkQueuePresentKHR

    // Vulkan world compute GPU hardware pass breakdown [ms]
    float GpuTotalPassMs            = 0.0f;  // [ms] - VkQueryPool total GPU time across world passes
    float GpuSkyLutMs               = 0.0f;  // [ms] - SkyView transmittance LUT compute pass
    float GpuCloudsMs               = 0.0f;  // [ms] - Downsampled volumetric cloud deck compute pass
    float GpuPrimaryTraceMs         = 0.0f;  // [ms] - Primary raytrace/shade/shadow pass
    float GpuDenoiseMs              = 0.0f;  // [ms] - Spatial shadow denoise & composite passes
    float GpuReadbackCopyMs         = 0.0f;  // [ms] - VRAM -> host-cached staging copy
    float GpuHostSubmitWaitMs       = 0.0f;  // [ms] - CPU uniform pack, cmd encode, timeline wait & memcpy
};

[[nodiscard]] inline const char* QueryEngineShortLabel(uint32_t EngineMode) noexcept
{
    switch (EngineMode)
    {
    case 0u: return "MatCap";
    case 1u: return "OpenPBR Direct";
    case 2u: return "GTX GDF GI";
    case 3u: return "HighEnd BVH4";
    case 4u: return "PT Realtime";
    case 5u: return "PT Final";
    default: return "GTX GDF GI";
    }
}

[[nodiscard]] inline const char* QueryShadowShortLabel(uint32_t ShadowAlg) noexcept
{
    switch (ShadowAlg)
    {
    case 0u: return "ContactPCSS";
    case 1u: return "PlanePCSS";
    case 2u: return "SMRT";
    case 3u: return "PCF3x3";
    case 4u: return "RayQuery";
    default: return "Unknown";
    }
}

namespace Detail
{
struct PersistentTimingLogChannel
{
    std::ofstream         Stream {};
    std::filesystem::path OpenPath {};
    uint32_t              UnflushedLines = 0u;
};

inline PersistentTimingLogChannel& AccessTimingLogChannel()
{
    static PersistentTimingLogChannel Instance {};
    return Instance;
}
} // namespace Detail

inline void InitializeTimingLogFile(const std::filesystem::path& LogPath, std::string_view AdapterName, bool HwGpu)
{
    auto& LogChannel = Detail::AccessTimingLogChannel();
    if (LogChannel.Stream.is_open())
    {
        LogChannel.Stream.close();
    }
    LogChannel.Stream.open(LogPath, std::ios::out | std::ios::trunc);
    LogChannel.OpenPath       = LogPath;
    LogChannel.UnflushedLines = 0u;
    if (!LogChannel.Stream)
    {
        return;
    }
    LogChannel.Stream << "# Frontier Per-Stage Timing Log\n";
    LogChannel.Stream << "# Adapter: " << AdapterName << " | HardwareGPU: " << (HwGpu ? "YES" : "NO") << "\n";
    LogChannel.Stream << "# Columns: Frame | FPS | TotalMs | Engine | Shadow | Res | WorldGpuTotalMs | SkyLutMs | CloudsMs | TraceMs | DenoiseMs | CopyMs | HostWaitMs | SceneSyncMs | TickMs | GizmoMs | UiBuildMs | PresentMs\n";
    LogChannel.Stream.flush();
}

inline void AppendTimingLogLine(const std::filesystem::path& LogPath, const ViewportTimingBreakdown& T, const char* ReasonTag = "SAMPLE")
{
    auto& LogChannel = Detail::AccessTimingLogChannel();
    if (!LogChannel.Stream.is_open() || LogChannel.OpenPath != LogPath)
    {
        if (LogChannel.Stream.is_open())
        {
            LogChannel.Stream.close();
        }
        LogChannel.Stream.open(LogPath, std::ios::out | std::ios::app);
        LogChannel.OpenPath       = LogPath;
        LogChannel.UnflushedLines = 0u;
    }
    if (!LogChannel.Stream)
    {
        return;
    }
    const float Fps = (T.TotalFrameMs > 0.01f) ? (1000.0f / T.TotalFrameMs) : 0.0f;
    char Line[512]{};
    std::snprintf(
        Line,
        sizeof(Line),
        "[%s] F=%llu | %5.1f fps | Total=%6.2f ms | %s/%s Q%u | %ux%u | "
        "GPU(Tot=%5.2f Sky=%4.2f Cld=%4.2f Trc=%5.2f Den=%4.2f Cpy=%4.2f HostWait=%5.2f) | "
        "SceneSync=%5.2f Tick=%4.2f Gizmo=%4.2f UI=%4.2f Present=%5.2f\n",
        ReasonTag != nullptr ? ReasonTag : "SAMPLE",
        static_cast<unsigned long long>(T.FrameIndex),
        Fps,
        T.TotalFrameMs,
        QueryEngineShortLabel(T.EngineMode),
        QueryShadowShortLabel(T.ShadowAlgorithm),
        T.QualityTier,
        T.RenderWidthPx,
        T.RenderHeightPx,
        T.GpuTotalPassMs,
        T.GpuSkyLutMs,
        T.GpuCloudsMs,
        T.GpuPrimaryTraceMs,
        T.GpuDenoiseMs,
        T.GpuReadbackCopyMs,
        T.GpuHostSubmitWaitMs,
        T.SceneSyncGpuMs,
        T.EditorTickMs,
        T.GizmoOverlayMs,
        T.DesktopPollAndUiMs,
        T.SwapchainPresentMs);
    LogChannel.Stream.write(Line, static_cast<std::streamsize>(std::char_traits<char>::length(Line)));
    if (++LogChannel.UnflushedLines >= 24u)
    {
        LogChannel.Stream.flush();
        LogChannel.UnflushedLines = 0u;
    }
}

} // namespace Frontier
