/*============================================================================================================================================
                                                       VIEWPORTTIMINGTELEMETRY.H
============================================================================================================================================*/
// 🧩 Per-stage CPU/GPU timing breakdown and RAM-buffered exit-flushed timing ledger for Frontier diagnostics

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ViewportTimingBreakdown
{
    uint64_t    FrameIndex                = 0ull;  // [idx] - Monotonic desktop step index
    uint32_t    RenderWidthPx             = 0u;    // [px]  - Active world render width
    uint32_t    RenderHeightPx            = 0u;    // [px]  - Active world render height
    uint32_t    EngineMode                = 1u;    // [idx] - 0=MatCap, 1=OpenPBR, 2=GTX GDF, 3=BVH4, 4=PT RT, 5=PT Final
    uint32_t    ShadowAlgorithm           = 4u;    // [idx] - 0=ContactPCSS, 1=PlanePCSS, 2=SMRT, 3=PCF, 4=RayQuery
    uint32_t    QualityTier               = 2u;    // [idx] - 0=Low..4=Ultra
    bool        HardwareGpuActive         = false; // [-]   - True when running on hardware GPU (discrete/iGPU)
    bool        GpuTimestampsValid        = false; // [-]   - True when VkQueryPool timestamps populated GPU pass times
    bool        WorldDispatched           = false; // [-]   - True when world compute pipeline executed this step
    std::string AdapterName               { "Unknown" }; // [-] - Active Vulkan adapter name
    std::string TimingLogPath             { "Logs/FrontierTiming.log" }; // [-] - Flushed once on exit

    // Cluster LOD & Local SDF live counters
    bool        ClusterLodActive          = false; // [-]   - True when Cluster LOD cut drove GPU BVH
    uint32_t    ActiveClusterCount        = 0u;    // [cnt] - Active clusters in current monotonic cut
    uint32_t    ActiveTriangleCount       = 0u;    // [cnt] - Active triangles in current monotonic cut
    uint32_t    FullTriangleCount         = 0u;    // [cnt] - Full-resolution scene triangle count
    uint32_t    LocalSdfInstanceCount     = 0u;    // [cnt] - Live per-object SDF instances composited on GPU

    // End-to-end host step stages [ms]
    float TotalFrameMs                    = 0.0f;  // [ms] - Full step wall-clock delta
    float DesktopPollAndUiMs              = 0.0f;  // [ms] - GLFW poll + ImGui build
    float EditorTickMs                    = 0.0f;  // [ms] - Scene/outliner step before render
    float SceneSyncGpuMs                  = 0.0f;  // [ms] - GPU scene upload & shadow cascade sync
    float GizmoOverlayMs                  = 0.0f;  // [ms] - 3D Transform gizmo raster & composite
    float SwapchainPresentMs              = 0.0f;  // [ms] - ImGui draw + texture upload + vkQueuePresentKHR

    // Vulkan world compute GPU hardware pass breakdown [ms]
    float GpuTotalPassMs                  = 0.0f;  // [ms] - VkQueryPool total GPU time across world passes
    float GpuSkyLutMs                     = 0.0f;  // [ms] - SkyView transmittance LUT compute pass
    float GpuCloudsMs                     = 0.0f;  // [ms] - Downsampled volumetric cloud deck compute pass
    float GpuPrimaryTraceMs               = 0.0f;  // [ms] - Primary raytrace/shade/shadow pass
    float GpuDenoiseMs                    = 0.0f;  // [ms] - Spatial shadow denoise & composite passes
    float GpuReadbackCopyMs               = 0.0f;  // [ms] - VRAM -> host-cached staging copy
    float GpuHostSubmitWaitMs             = 0.0f;  // [ms] - CPU uniform pack, cmd encode, timeline wait & memcpy
};

struct RamTimingLedgerBuffer
{
    static constexpr std::size_t MaxBufferedLines = 16384u;

    std::mutex                Mutex       {};
    std::filesystem::path     TargetPath  { "Logs/FrontierTiming.log" };
    std::string               HeaderBlock {};
    std::vector<std::string>  RingLines   {};
    std::size_t               WriteCursor = 0u;
    bool                      Wrapped     = false;
    bool                      Dirty       = false;
    bool                      ExitHooked  = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                               INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline RamTimingLedgerBuffer& AccessRamTimingLedgerBuffer() noexcept
{
    static RamTimingLedgerBuffer Ledger {};
    return Ledger;
}

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

inline void FlushBufferedTimingLog()
{
    RamTimingLedgerBuffer& Ledger = AccessRamTimingLedgerBuffer();
    std::lock_guard<std::mutex> Lock(Ledger.Mutex);
    if (!Ledger.Dirty || Ledger.RingLines.empty())
    {
        return;
    }

    std::error_code DirErr {};
    if (Ledger.TargetPath.has_parent_path())
    {
        std::filesystem::create_directories(Ledger.TargetPath.parent_path(), DirErr);
    }
    std::ofstream Out(Ledger.TargetPath, std::ios::trunc);
    if (!Out)
    {
        return;
    }

    Out << Ledger.HeaderBlock;
    if (Ledger.Wrapped)
    {
        for (std::size_t Idx = Ledger.WriteCursor; Idx < Ledger.RingLines.size(); ++Idx)
        {
            Out << Ledger.RingLines[Idx];
        }
        for (std::size_t Idx = 0u; Idx < Ledger.WriteCursor; ++Idx)
        {
            Out << Ledger.RingLines[Idx];
        }
    }
    else
    {
        for (std::size_t Idx = 0u; Idx < Ledger.WriteCursor; ++Idx)
        {
            Out << Ledger.RingLines[Idx];
        }
    }
    Ledger.Dirty = false;
}

inline void InitializeTimingLogFile(const std::filesystem::path& LogPath, std::string_view AdapterName, bool HwGpu)
{
    RamTimingLedgerBuffer& Ledger = AccessRamTimingLedgerBuffer();
    std::lock_guard<std::mutex> Lock(Ledger.Mutex);
    Ledger.TargetPath  = LogPath;
    Ledger.HeaderBlock =
        "# Frontier Per-Stage Timing Log (RAM-buffered; flushed once on exit)\n"
        "# Adapter: " + std::string(AdapterName) + " | HardwareGPU: " + std::string(HwGpu ? "YES" : "NO") + "\n"
        "# Columns: Step | FPS | TotalMs | Engine | Shadow | Res | WorldGpuTotalMs | SkyLutMs | CloudsMs | TraceMs | DenoiseMs | CopyMs | HostWaitMs | SceneSyncMs | TickMs | GizmoMs | UiBuildMs | PresentMs\n";
    Ledger.RingLines.clear();
    Ledger.RingLines.reserve(1024u);
    Ledger.WriteCursor = 0u;
    Ledger.Wrapped     = false;
    Ledger.Dirty       = false;
    if (!Ledger.ExitHooked)
    {
        Ledger.ExitHooked = true;
        std::atexit(&FlushBufferedTimingLog);
    }
}

inline void AppendTimingLogLine(const std::filesystem::path& LogPath, const ViewportTimingBreakdown& T, const char* ReasonTag = "SAMPLE")
{
    RamTimingLedgerBuffer& Ledger = AccessRamTimingLedgerBuffer();
    std::lock_guard<std::mutex> Lock(Ledger.Mutex);
    if (!LogPath.empty())
    {
        Ledger.TargetPath = LogPath;
    }
    if (!Ledger.ExitHooked)
    {
        Ledger.ExitHooked = true;
        std::atexit(&FlushBufferedTimingLog);
    }

    const float Fps = (T.TotalFrameMs > 0.01f) ? (1000.0f / T.TotalFrameMs) : 0.0f;
    char Line[512]{};
    std::snprintf(
        Line,
        sizeof(Line),
        "[%s] S=%llu | %5.1f fps | Total=%6.2f ms | %s/%s Q%u | %ux%u | "
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

    if (Ledger.RingLines.size() < RamTimingLedgerBuffer::MaxBufferedLines)
    {
        Ledger.RingLines.emplace_back(Line);
        Ledger.WriteCursor = Ledger.RingLines.size();
    }
    else
    {
        if (Ledger.WriteCursor >= RamTimingLedgerBuffer::MaxBufferedLines)
        {
            Ledger.WriteCursor = 0u;
            Ledger.Wrapped     = true;
        }
        Ledger.RingLines[Ledger.WriteCursor++] = Line;
    }
    Ledger.Dirty = true;
}

} // namespace Frontier
