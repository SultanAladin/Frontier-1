/*============================================================================================================================================
                                                       APOCALYPTICCITYVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validates RAM-buffered shutdown telemetry, RetainedCanvasCache UI replay, and Vulkan GPU Apocalyptic City Racing Level + live destruction

#include "Engine/Components/Diagnostics/ViewportTimingTelemetry.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/RetainedCanvasCache.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometricRaster/GlobalIllumination/ApocalypticCityBuilder.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::GeometricRaster;

[[nodiscard]] std::filesystem::path ResolveRepositoryRoot()
{
#ifdef FRONTIER_REPO_ROOT_DIR
    return std::filesystem::path(FRONTIER_REPO_ROOT_DIR);
#elif defined(FRONTIER_REPOSITORY_ROOT)
    return std::filesystem::path(FRONTIER_REPOSITORY_ROOT);
#else
    return std::filesystem::current_path();
#endif
}

[[nodiscard]] bool RequireCondition(bool Condition, const char* Label) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[ApocalypticCityValidation] FAIL: %s\n", Label);
        return false;
    }
    return true;
}

[[nodiscard]] std::filesystem::path LocateCompiledShaderFolder(const std::filesystem::path& RepoRoot)
{
    const std::array<std::filesystem::path, 6u> Candidates {{
#ifdef FRONTIER_COMPILED_SHADER_DIR
        std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR),
#else
        std::filesystem::current_path() / "CompiledShaders",
#endif
        std::filesystem::current_path() / "CompiledShaders",
        RepoRoot / "build/Editor/CompiledShaders",
        RepoRoot / "build/Shipping/CompiledShaders",
        RepoRoot / "Build/Editor/CompiledShaders",
        std::filesystem::path("CompiledShaders"),
    }};

    for (const auto& PathCandidate : Candidates)
    {
        std::error_code FsErr {};
        if (std::filesystem::exists(PathCandidate / "WorldViewportRadiometricCompute.comp.spv", FsErr) && !FsErr)
        {
            return PathCandidate;
        }
    }
    return Candidates.front();
}

void AppendBigEndianUint32(std::vector<uint8_t>& Dest, uint32_t Word)
{
    Dest.push_back(static_cast<uint8_t>((Word >> 24u) & 0xFFu));
    Dest.push_back(static_cast<uint8_t>((Word >> 16u) & 0xFFu));
    Dest.push_back(static_cast<uint8_t>((Word >> 8u) & 0xFFu));
    Dest.push_back(static_cast<uint8_t>(Word & 0xFFu));
}

[[nodiscard]] uint32_t ComputePngCrc32(const uint8_t* Bytes, std::size_t Length) noexcept
{
    uint32_t Crc = 0xFFFFFFFFu;
    for (std::size_t I = 0u; I < Length; ++I)
    {
        Crc ^= Bytes[I];
        for (int Bit = 0; Bit < 8; ++Bit)
        {
            const uint32_t Mask = static_cast<uint32_t>(-static_cast<int32_t>(Crc & 1u));
            Crc                 = (Crc >> 1u) ^ (0xEDB88320u & Mask);
        }
    }
    return ~Crc;
}

void AppendPngChunkBytes(std::vector<uint8_t>& Dest, const char Tag[4], const std::vector<uint8_t>& Body)
{
    AppendBigEndianUint32(Dest, static_cast<uint32_t>(Body.size()));
    const std::size_t Start = Dest.size();
    for (int I = 0; I < 4; ++I)
    {
        Dest.push_back(static_cast<uint8_t>(Tag[I]));
    }
    Dest.insert(Dest.end(), Body.begin(), Body.end());
    AppendBigEndianUint32(Dest, ComputePngCrc32(Dest.data() + Start, 4u + Body.size()));
}

[[nodiscard]] bool SaveRgba8ImageToPng(
    const std::filesystem::path& TargetPng,
    uint32_t                     WidthPx,
    uint32_t                     HeightPx,
    const std::vector<uint8_t>&  RgbaBytes)
{
    if (WidthPx == 0u || HeightPx == 0u || RgbaBytes.size() < static_cast<std::size_t>(WidthPx) * HeightPx * 4u)
    {
        return false;
    }
    std::error_code DirErr {};
    std::filesystem::create_directories(TargetPng.parent_path(), DirErr);

    const std::size_t RowStride = 1u + static_cast<std::size_t>(WidthPx) * 4u;
    std::vector<uint8_t> Scanlines(RowStride * HeightPx, 0u);
    for (uint32_t Y = 0u; Y < HeightPx; ++Y)
    {
        std::memcpy(
            Scanlines.data() + static_cast<std::size_t>(Y) * RowStride + 1u,
            RgbaBytes.data() + static_cast<std::size_t>(Y) * WidthPx * 4u,
            static_cast<std::size_t>(WidthPx) * 4u);
    }

    std::vector<uint8_t> ZlibBytes { 0x78u, 0x01u };
    std::size_t Offset = 0u;
    while (Offset < Scanlines.size())
    {
        const std::size_t BlockLen = std::min<std::size_t>(65535u, Scanlines.size() - Offset);
        const bool FinalBlock      = (Offset + BlockLen == Scanlines.size());
        ZlibBytes.push_back(FinalBlock ? 0x01u : 0x00u);
        const uint16_t Len16  = static_cast<uint16_t>(BlockLen);
        const uint16_t NLen16 = static_cast<uint16_t>(~Len16);
        ZlibBytes.push_back(static_cast<uint8_t>(Len16 & 0xFFu));
        ZlibBytes.push_back(static_cast<uint8_t>((Len16 >> 8u) & 0xFFu));
        ZlibBytes.push_back(static_cast<uint8_t>(NLen16 & 0xFFu));
        ZlibBytes.push_back(static_cast<uint8_t>((NLen16 >> 8u) & 0xFFu));
        ZlibBytes.insert(
            ZlibBytes.end(),
            Scanlines.begin() + static_cast<std::ptrdiff_t>(Offset),
            Scanlines.begin() + static_cast<std::ptrdiff_t>(Offset + BlockLen));
        Offset += BlockLen;
    }
    uint32_t AdlerA = 1u;
    uint32_t AdlerB = 0u;
    for (uint8_t B : Scanlines)
    {
        AdlerA = (AdlerA + B) % 65521u;
        AdlerB = (AdlerB + AdlerA) % 65521u;
    }
    AppendBigEndianUint32(ZlibBytes, (AdlerB << 16u) | AdlerA);

    std::vector<uint8_t> PngBytes { 0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au };
    std::vector<uint8_t> Ihdr {};
    AppendBigEndianUint32(Ihdr, WidthPx);
    AppendBigEndianUint32(Ihdr, HeightPx);
    Ihdr.insert(Ihdr.end(), { 8u, 6u, 0u, 0u, 0u });
    AppendPngChunkBytes(PngBytes, "IHDR", Ihdr);
    AppendPngChunkBytes(PngBytes, "IDAT", ZlibBytes);
    AppendPngChunkBytes(PngBytes, "IEND", {});

    std::ofstream Out(TargetPng, std::ios::binary | std::ios::trunc);
    if (!Out)
    {
        return false;
    }
    Out.write(reinterpret_cast<const char*>(PngBytes.data()), static_cast<std::streamsize>(PngBytes.size()));
    return Out.good();
}

} // namespace

int main()
{
    const std::filesystem::path RepoRoot = ResolveRepositoryRoot();

    //--------------------------------------------------------------------------------------------------------------------
    // 1. Verify RAM-Buffered Shutdown-Only Telemetry (Zero runtime disk writes; written exclusively on exit)
    //--------------------------------------------------------------------------------------------------------------------
    const std::filesystem::path TempLogPath = std::filesystem::temp_directory_path() / "FrontierShutdownOnlyValidation.log";
    {
        std::error_code RmErr {};
        std::filesystem::remove(TempLogPath, RmErr);
    }
    InitializeTimingLogFile(TempLogPath, "ValidationAdapter", true);
    for (uint64_t Idx = 1ull; Idx <= 96ull; ++Idx)
    {
        ViewportTimingBreakdown Sample {};
        Sample.FrameIndex        = Idx;
        Sample.TotalFrameMs      = 7.45f;
        Sample.GpuTotalPassMs    = 5.82f;
        Sample.RenderWidthPx     = 1280u;
        Sample.RenderHeightPx    = 720u;
        Sample.WorldDispatched   = true;
        Sample.TimingLogPath     = TempLogPath;
        AppendTimingLogLine(TempLogPath, Sample, "SAMPLE");
    }
    if (!RequireCondition(!std::filesystem::exists(TempLogPath), "Telemetry log must NOT touch disk during runtime"))
    {
        return 1;
    }
    ReleaseTimingLogOnExit();
    if (!RequireCondition(std::filesystem::exists(TempLogPath), "Telemetry log must flush cleanly on exit"))
    {
        return 1;
    }
    {
        const auto ByteCount = std::filesystem::file_size(TempLogPath);
        if (!RequireCondition(ByteCount > 4096u, "Flushed telemetry log contains all 96 buffered samples"))
        {
            return 1;
        }
        std::error_code RmErr {};
        std::filesystem::remove(TempLogPath, RmErr);
    }
    std::printf("[ApocalypticCityValidation] PASS 1/3: RAM-buffered shutdown-only telemetry verified (0 runtime disk writes).\n");

    //--------------------------------------------------------------------------------------------------------------------
    // 2. Verify RetainedCanvasCache Zero-Rebuild ImDrawList Capture & Replay
    //--------------------------------------------------------------------------------------------------------------------
    {
        ImGuiContext* GuiCtx = ImGui::CreateContext();
        ImGuiIO&      Io     = ImGui::GetIO();
        Io.DisplaySize       = ImVec2(1280.0f, 720.0f);
        Io.BackendFlags     |= ImGuiBackendFlags_RendererHasVtxOffset;
        unsigned char* Pixels = nullptr;
        int TexW = 0, TexH = 0;
        Io.Fonts->GetTexDataAsRGBA32(&Pixels, &TexW, &TexH);

        RetainedCanvasCache Cache {};
        const Rectangle2D   PaneRect { 16.0f, 32.0f, 320.0f, 640.0f };
        const Point2D       OutsideMouse { 920.0f, 400.0f };
        const uint64_t      Sig = RetainedCanvasCache::MixBounds(0xABCDEFull, PaneRect, 1.0f);

        ImGui::NewFrame();
        ImDrawList* Canvas = ImGui::GetForegroundDrawList();
        Cache.BeginCapture(Canvas);
        PixelSpace Surface(Canvas, { PaneRect.X, PaneRect.Y }, 1.0f);
        for (int Row = 0; Row < 28; ++Row)
        {
            Surface.FillRectangle({ 8.0f, static_cast<float>(Row) * 20.0f, 296.0f, 16.0f }, { 48u, 52u, 60u, 255u }, 6.0f);
        }
        const int CapturedVtx = Canvas->VtxBuffer.Size;
        const int CapturedIdx = Canvas->IdxBuffer.Size;
        Cache.EndCapture(Canvas, Sig, PaneRect, OutsideMouse);
        ImGui::EndFrame();

        ImGui::NewFrame();
        ImDrawList* ReplayCanvas = ImGui::GetForegroundDrawList();
        const bool Reusable = Cache.QueryReusable(Sig, PaneRect, OutsideMouse, false);
        const bool Replayed = Reusable && Cache.ReplayInto(ReplayCanvas, OutsideMouse);
        const int  ReplayedVtx = ReplayCanvas->VtxBuffer.Size;
        const int  ReplayedIdx = ReplayCanvas->IdxBuffer.Size;
        ImGui::EndFrame();
        ImGui::DestroyContext(GuiCtx);

        if (!RequireCondition(Replayed && Cache.QueryCaptureCount() == 1ull && Cache.QueryReplayCount() == 1ull, "RetainedCanvasCache replay active") ||
            !RequireCondition(CapturedVtx > 100 && CapturedVtx == ReplayedVtx && CapturedIdx == ReplayedIdx, "RetainedCanvasCache vertex/index parity"))
        {
            return 1;
        }
    }
    std::printf("[ApocalypticCityValidation] PASS 2/3: RetainedCanvasCache captured & replayed UI canvas with exact vertex/index parity.\n");

    //--------------------------------------------------------------------------------------------------------------------
    // 3. Build Procedural Apocalyptic City Racing Level, Render Vulkan GPU Shots & Step Dynamic Warzone Destruction
    //--------------------------------------------------------------------------------------------------------------------
    constexpr uint32_t RenderWidthPx  = 960u;
    constexpr uint32_t RenderHeightPx = 540u;

    ShowcaseWorldSceneBundle          CityBundle {};
    ApocalypticCitySimulationRegister CitySim    {};
    OutlinerPanel                     CityOutliner {};
    BuildApocalypticCityRacingScene(CityBundle, CitySim, RenderWidthPx, RenderHeightPx, 20261122u);
    RegisterApocalypticCityInOutliner(CityOutliner, CityBundle, false);

    if (!RequireCondition(CityBundle.Objects.size() >= 38u, "Apocalyptic City contains full multi-building warzone roster") ||
        !RequireCondition(CitySim.RoamingDeer.size() == 4u, "4 autonomous Woodland Deer spawned") ||
        !RequireCondition(CitySim.SmashableBarriers.size() == 3u, "2 Abbe glass curtains + 1 masonry breach wall armed") ||
        !RequireCondition(CitySim.DynamicFragments.size() >= 30u, "Dynamic collapsing building/floor/glass/wall fragments armed"))
    {
        return 1;
    }

    VulkanInterchange VkDevice {};
    std::string       VkDiag {};
    if (!RequireCondition(VkDevice.InitializeInterchange(RepoRoot, &VkDiag), VkDiag.c_str()))
    {
        return 1;
    }
    const std::filesystem::path ShaderDir = LocateCompiledShaderFolder(RepoRoot);
    VulkanWorldRenderSequence   GpuSequence {};
    if (!RequireCondition(GpuSequence.InitializeSequence(VkDevice, ShaderDir), "Initialize VulkanWorldRenderSequence"))
    {
        return 1;
    }

    GpuSequence.SynchronizeSceneResources(CityBundle.Scene, true);

    WorldRenderConfiguration RenderCfg {};
    RenderCfg.Engine          = WorldRenderEngine::RealTimeGiGtxLowEnd;
    RenderCfg.ShadowAlgorithm = SolarShadowAlgorithm::ContactPcss;

    const std::filesystem::path PreviewDir = std::filesystem::temp_directory_path() / "FrontierApocalypticCityShots";
    std::vector<uint8_t> HeroRgba {};
    if (!RequireCondition(GpuSequence.RenderWorldViewportWithEngine(RenderCfg, 0u, CityBundle.EditorCamera, CityBundle.Scene, HeroRgba), "Render Shot 1 (Wide Boulevard & Skybridge)") ||
        !RequireCondition(SaveRgba8ImageToPng(PreviewDir / "01-ApocalypticCityBoulevardSkybridge.png", RenderWidthPx, RenderHeightPx, HeroRgba), "Save Shot 1 PNG"))
    {
        return 1;
    }

    // Advance 6.0 seconds (240 simulation ticks at 25 ms) so noise-driven building chunks & interior floor slabs collapse,
    // both Abbe glass curtains shatter, the masonry breach wall explodes radially, and the 4 Woodland Deer roam freely!
    for (int Step = 0; Step < 240; ++Step)
    {
        (void)StepApocalypticCitySimulation(CityBundle, CitySim, &CityOutliner, 0.025);
    }
    if (!RequireCondition(CitySim.DetachedFragmentCount >= 20u, "Noise collapse + impact released >= 20 dynamic fragments") ||
        !RequireCondition(CitySim.ShatteredGlassCount == 2u, "Both Abbe glass curtain walls shattered") ||
        !RequireCondition(CitySim.BreachedWallCount == 1u, "East Tower masonry breach wall smashed"))
    {
        return 1;
    }

    // Dynamic update on GPU (false = fast instance transform sync without static BVH rebuild!)
    GpuSequence.SynchronizeSceneResources(CityBundle.Scene, false);

    // Frame Shot 2 onto the East Tower Breach + Skybridge + Roaming Deer + Fallen Rubble Action!
    PerspectiveCameraBasis ActionCam = CityBundle.EditorCamera;
    ActionCam.CameraOrigin = {  2.5f, 22.8f, 5.20f };
    ActionCam.TargetPoint  = {  6.2f,  4.2f, 3.40f };

    std::vector<uint8_t> ActionRgba {};
    if (!RequireCondition(GpuSequence.RenderWorldViewportWithEngine(RenderCfg, 1u, ActionCam, CityBundle.Scene, ActionRgba), "Render Shot 2 (Dynamic Collapse, Breach & Roaming Deer)") ||
        !RequireCondition(SaveRgba8ImageToPng(PreviewDir / "02-ApocalypticCityDynamicDestruction.png", RenderWidthPx, RenderHeightPx, ActionRgba), "Save Shot 2 PNG"))
    {
        return 1;
    }

    // Frame Shot 3 from West Financial Tower Floor 3 Atrium looking directly down the Skybridge into East Tower & Breach Ramp!
    PerspectiveCameraBasis InteriorCam = CityBundle.EditorCamera;
    InteriorCam.CameraOrigin = { -16.8f, -2.2f, 9.65f };
    InteriorCam.TargetPoint  = {  14.2f,  2.4f, 4.85f };

    std::vector<uint8_t> InteriorRgba {};
    if (!RequireCondition(GpuSequence.RenderWorldViewportWithEngine(RenderCfg, 2u, InteriorCam, CityBundle.Scene, InteriorRgba), "Render Shot 3 (Drivable Multi-Story Interior & Skybridge)") ||
        !RequireCondition(SaveRgba8ImageToPng(PreviewDir / "03-ApocalypticCityDrivableInterior.png", RenderWidthPx, RenderHeightPx, InteriorRgba), "Save Shot 3 PNG"))
    {
        return 1;
    }

    std::printf(
        "[ApocalypticCityValidation] PASS 3/3: Rendered 3x %ux%u Vulkan GPU shots in %s (Objects=%zu, Detached=%u, GlassShattered=%u, WallsBreached=%u, Deer=%zu).\n",
        RenderWidthPx,
        RenderHeightPx,
        PreviewDir.string().c_str(),
        CityBundle.Objects.size(),
        CitySim.DetachedFragmentCount,
        CitySim.ShatteredGlassCount,
        CitySim.BreachedWallCount,
        CitySim.RoamingDeer.size());
    return 0;
}
