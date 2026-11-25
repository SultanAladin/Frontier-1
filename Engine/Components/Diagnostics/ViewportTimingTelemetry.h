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
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if __has_include("FrontierBuildInfo.h")
#include "FrontierBuildInfo.h"
#endif

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
    float    TotalFrameMs           = 0.0f;  // [ms]  - Full frame wall-clock delta
    float    DesktopPollAndUiMs     = 0.0f;  // [ms]  - GLFW poll + ImGui frame build
    float    EditorTickMs           = 0.0f;  // [ms]  - Scene/outliner tick before render
    float    EnvSyncMs              = 0.0f;  // [ms]  - Environment registry + active inspector sync
    float    InspectorUiMs          = 0.0f;  // [ms]  - Active selected inspector card build or retained replay
    bool     InspectorCacheReplayed = false; // [-]   - True when Inspector replayed retained canvas without redraw
    uint64_t InspectorCaptureCount  = 0ull;  // [cnt] - Cumulative Inspector rebuild count
    uint64_t InspectorReplayCount   = 0ull;  // [cnt] - Cumulative Inspector zero-redraw replay count
    float    SceneSyncGpuMs         = 0.0f;  // [ms]  - GPU scene upload & shadow cascade sync
    float    GizmoOverlayMs         = 0.0f;  // [ms]  - 3D Transform gizmo raster & composite
    float    SwapchainPresentMs     = 0.0f;  // [ms]  - ImGui draw + texture upload + vkQueuePresentKHR

    // Vulkan world compute GPU hardware pass breakdown [ms]
    float GpuTotalPassMs            = 0.0f;  // [ms] - VkQueryPool total GPU time across world passes
    float GpuSkyLutMs               = 0.0f;  // [ms] - SkyView transmittance LUT compute pass
    float GpuCloudsMs               = 0.0f;  // [ms] - Downsampled volumetric cloud deck compute pass
    float GpuPrimaryTraceMs         = 0.0f;  // [ms] - Primary raytrace/shade/shadow pass
    float GpuDenoiseMs              = 0.0f;  // [ms] - Spatial shadow denoise & composite passes
    float GpuReadbackCopyMs         = 0.0f;  // [ms] - VRAM -> host-cached staging copy
    float GpuHostSubmitWaitMs       = 0.0f;  // [ms] - CPU uniform pack, cmd encode, timeline wait & memcpy
};

struct PhysicsTelemetryReading
{
    bool                     PhysicsActive         = false;        // [-]   - True when Jolt PhysicsExtension is active
    uint32_t                 ActiveBodies          = 0u;           // [cnt] - Active Jolt rigid bodies
    uint32_t                 ActiveConstraints     = 0u;           // [cnt] - Active Jolt constraints
    uint32_t                 ActiveTriggerOverlaps = 0u;           // [cnt] - Active sensor trigger overlaps
    uint32_t                 RegisteredMaterials   = 8u;           // [cnt] - Registered PhysicsMaterial presets
    float                    StepComputeMs         = 0.0f;         // [ms]  - Fixed-step Jolt + Vehicle solver compute time
    float                    FixedStepHz           = 120.0f;       // [Hz]  - Fixed-step simulation frequency
    uint64_t                 CompletedSteps        = 0ull;         // [cnt] - Completed physics steps
    uint64_t                 DebugLogSequence      = 0ull;         // [seq] - Total PhysicsDebugLog events recorded
    std::string              LatestLogSummary      {};             // [-]   - Most recent PhysicsDebugLog line
    std::vector<std::string> RecentLogLines        {};             // [-]   - Latest PhysicsDebugLog entries (up to 4 shown in F3)

    // Optional live Vehicle telemetry (populated when a VehiclePhysicsRig is active)
    bool                     VehicleActive         = false;        // [-]   - True when VehiclePhysicsRig telemetry is populated
    float                    SpeedKmh              = 0.0f;         // [km/h]- Vehicle ground speed
    float                    EngineRpm             = 0.0f;         // [RPM] - Crankshaft speed
    float                    BoostBar              = 0.0f;         // [bar] - Combined turbo/supercharger boost
    int32_t                  ActiveGear            = 1;            // [idx] - Active transmission gear
    float                    DownforceNewtons      = 0.0f;         // [N]   - Total aerodynamic downforce
    float                    FuelLiters            = 0.0f;         // [L]   - Remaining fuel volume
    float                    FuelCapacityLiters    = 58.0f;        // [L]   - Tank capacity
    float                    AvgTreadTempC         = 20.0f;        // [C]   - Mean tire tread temperature
    float                    AvgSurfaceGrip        = 1.0f;         // [x]   - Mean PhysicsMaterial tire grip multiplier
    uint32_t                 WheelsInContact       = 0u;           // [cnt] - Wheels currently touching ground (0..4)
    std::string              WheelContactModeLabel { "MultiRayArc" }; // [-] - SingleRay / MultiRayArc / CylinderShapeCast
    std::string              ActiveSurfaceLabel    { "Asphalt" };  // [-]   - Active surface PhysicsMaterial label
    bool                     AbsActive             = false;        // [-]   - ABS intervening
    bool                     TcsActive             = false;        // [-]   - TCS intervening
    bool                     EscActive             = false;        // [-]   - ESC intervening
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
    std::filesystem::path    TargetPath    {};
    std::string              HeaderText    {};
    std::vector<std::string> BufferedLines {};
    bool                     PendingFlush  = false;

    void FlushOnShutdown() noexcept
    {
        if (!PendingFlush || TargetPath.empty())
        {
            return;
        }
        PendingFlush = false;
        std::ofstream OutFile(TargetPath, std::ios::out | std::ios::trunc);
        if (!OutFile)
        {
            return;
        }
        if (!HeaderText.empty())
        {
            OutFile.write(HeaderText.data(), static_cast<std::streamsize>(HeaderText.size()));
        }
        for (const std::string& EntryLine : BufferedLines)
        {
            OutFile.write(EntryLine.data(), static_cast<std::streamsize>(EntryLine.size()));
        }
        OutFile.flush();
        BufferedLines.clear();
    }

    ~PersistentTimingLogChannel() noexcept
    {
        FlushOnShutdown();
    }
};

inline PersistentTimingLogChannel& AccessTimingLogChannel()
{
    static PersistentTimingLogChannel Instance {};
    return Instance;
}
} // namespace Detail

inline void ReleaseTimingLogOnExit() noexcept
{
    Detail::AccessTimingLogChannel().FlushOnShutdown();
}

inline void InitializeTimingLogFile(const std::filesystem::path& LogPath, std::string_view AdapterName, bool HwGpu)
{
    auto& LogChannel = Detail::AccessTimingLogChannel();
    LogChannel.TargetPath = LogPath;
    LogChannel.BufferedLines.clear();
    LogChannel.BufferedLines.reserve(8192u);
    LogChannel.HeaderText.clear();
    LogChannel.HeaderText.reserve(420u);

    char DateTimeBuf[64] = "unknown";
    const std::time_t NowEpoch = std::time(nullptr);
    std::tm LocalTm {};
#if defined(_WIN32)
    if (localtime_s(&LocalTm, &NowEpoch) == 0)
    {
        std::strftime(DateTimeBuf, sizeof(DateTimeBuf), "%Y-%m-%d %H:%M:%S", &LocalTm);
    }
#else
    if (localtime_r(&NowEpoch, &LocalTm) != nullptr)
    {
        std::strftime(DateTimeBuf, sizeof(DateTimeBuf), "%Y-%m-%d %H:%M:%S", &LocalTm);
    }
#endif
#ifndef FRONTIER_GIT_COMMIT_HASH
#define FRONTIER_GIT_COMMIT_HASH "unknown"
#endif

    LogChannel.HeaderText += "# Frontier Per-Stage Timing Log\n";
    LogChannel.HeaderText += "# BuildCommit: ";
    LogChannel.HeaderText += FRONTIER_GIT_COMMIT_HASH;
    LogChannel.HeaderText += " | RunDateTime: ";
    LogChannel.HeaderText += DateTimeBuf;
    LogChannel.HeaderText += "\n";
    LogChannel.HeaderText += "# Adapter: ";
    LogChannel.HeaderText.append(AdapterName.data(), AdapterName.size());
    LogChannel.HeaderText += " | HardwareGPU: ";
    LogChannel.HeaderText += (HwGpu ? "YES\n" : "NO\n");
    LogChannel.HeaderText += "# Columns: Frame | FPS | TotalMs | Engine | Shadow | Res | WorldGpuTotalMs | SkyLutMs | CloudsMs | TraceMs | DenoiseMs | CopyMs | HostWaitMs | SceneSyncMs | TickMs | EnvSyncMs | GizmoMs | UiBuildMs | InspectorMs(HIT/BUILD) | PresentMs\n";
    LogChannel.PendingFlush = false;
}

inline void AppendTimingLogLine(const std::filesystem::path& LogPath, const ViewportTimingBreakdown& T, const char* ReasonTag = "SAMPLE")
{
    auto& LogChannel = Detail::AccessTimingLogChannel();
    if (!LogPath.empty() && LogChannel.TargetPath != LogPath)
    {
        LogChannel.TargetPath = LogPath;
    }
    if (LogChannel.BufferedLines.capacity() < 8192u)
    {
        LogChannel.BufferedLines.reserve(8192u);
    }
    // Cap RAM buffer capacity to 65,536 entries (~10 MB) so multi-hour sessions stay bounded in memory
    if (LogChannel.BufferedLines.size() >= 65536u)
    {
        return;
    }
    const float Fps = (T.TotalFrameMs > 0.01f) ? (1000.0f / T.TotalFrameMs) : 0.0f;
    char Line[640]{};
    const int Written = std::snprintf(
        Line,
        sizeof(Line),
        "[%s] F=%llu | %5.1f fps | Total=%6.2f ms | %s/%s Q%u | %ux%u | "
        "GPU(Tot=%5.2f Sky=%4.2f Cld=%4.2f Trc=%5.2f Den=%4.2f Cpy=%4.2f HostWait=%5.2f) | "
        "SceneSync=%5.2f Tick=%4.2f EnvSync=%4.2f Gizmo=%4.2f UI=%4.2f Insp=%4.2f(%s %llu/%llu) Present=%5.2f\n",
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
        T.EnvSyncMs,
        T.GizmoOverlayMs,
        T.DesktopPollAndUiMs,
        T.InspectorUiMs,
        T.InspectorCacheReplayed ? "HIT" : "BUILD",
        static_cast<unsigned long long>(T.InspectorReplayCount),
        static_cast<unsigned long long>(T.InspectorCaptureCount),
        T.SwapchainPresentMs);
    if (Written > 0)
    {
        LogChannel.BufferedLines.emplace_back(Line, static_cast<std::size_t>(std::min<int>(Written, static_cast<int>(sizeof(Line) - 1))));
        LogChannel.PendingFlush = true;
    }
}

} // namespace Frontier
