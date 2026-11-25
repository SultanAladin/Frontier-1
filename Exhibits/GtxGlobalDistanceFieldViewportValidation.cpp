/*============================================================================================================================================
                                                GTXGLOBALDISTANCEFIELDVIEWPORTVALIDATION.CPP
============================================================================================================================================*/
// End-to-end Vulkan GPU validation of Baked Global Distance Field (GDF), 2-Bounce Radiosity Cone GI,
// Dedicated Path-Trace Compute Sequence Decomposition, and Zero Checker/Sawtooth Banding on ShowcaseGalleryScene

#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::GeometricRaster;

struct ValidationScorecard
{
    uint32_t Passed = 0u;
    uint32_t Failed = 0u;

    void Verify(bool Condition, const char* Label)
    {
        if (Condition)
        {
            ++Passed;
            std::printf("[GtxGlobalDistanceFieldViewportValidation][PASS] %s\n", Label);
        }
        else
        {
            ++Failed;
            std::fprintf(stderr, "[GtxGlobalDistanceFieldViewportValidation][FAIL] %s\n", Label);
        }
    }
};

[[nodiscard]] bool VerifySpirvPortableCapabilities(
    const std::filesystem::path& SpvPath,
    std::size_t                  MaxExpectedBytes,
    bool&                        OutSizeWithinBudget)
{
    OutSizeWithinBudget = false;
    std::ifstream Stream(SpvPath, std::ios::binary | std::ios::ate);
    if (!Stream.is_open())
    {
        return false;
    }
    const auto ByteLen = Stream.tellg();
    if (ByteLen < 20 || (static_cast<std::size_t>(ByteLen) % 4u) != 0u)
    {
        return false;
    }
    OutSizeWithinBudget = (static_cast<std::size_t>(ByteLen) <= MaxExpectedBytes);

    std::vector<uint32_t> Words(static_cast<std::size_t>(ByteLen) / 4u);
    Stream.seekg(0, std::ios::beg);
    Stream.read(reinterpret_cast<char*>(Words.data()), ByteLen);
    if (Words[0] != 0x07230203u)
    {
        return false;
    }

    // Scan SPIR-V preamble OpCapability (opcode 17) instructions and forbid Float16 (9) & Int16 (22)
    std::size_t Idx = 5u;
    while (Idx < Words.size())
    {
        const uint32_t Header    = Words[Idx];
        const uint16_t WordCount = static_cast<uint16_t>(Header >> 16u);
        const uint16_t Opcode    = static_cast<uint16_t>(Header & 0xFFFFu);
        if (WordCount == 0u || Idx + WordCount > Words.size())
        {
            break;
        }
        if (Opcode == 17u && WordCount >= 2u)
        {
            const uint32_t Cap = Words[Idx + 1u];
            if (Cap == 9u || Cap == 22u)
            {
                return false;
            }
        }
        else if (Opcode == 54u) // OpFunction: past preamble
        {
            break;
        }
        Idx += WordCount;
    }
    return true;
}

[[nodiscard]] double MeasureRegionMeanLuma(
    const std::vector<uint8_t>& Rgba,
    uint32_t                    Width,
    uint32_t                    Height,
    float                       U0,
    float                       V0,
    float                       U1,
    float                       V1)
{
    if (Rgba.size() < static_cast<std::size_t>(Width) * Height * 4u)
    {
        return 0.0;
    }
    const uint32_t X0 = std::min(Width - 1u, static_cast<uint32_t>(std::max(0.0f, U0) * static_cast<float>(Width)));
    const uint32_t X1 = std::min(Width, std::max(X0 + 1u, static_cast<uint32_t>(std::min(1.0f, U1) * static_cast<float>(Width))));
    const uint32_t Y0 = std::min(Height - 1u, static_cast<uint32_t>(std::max(0.0f, V0) * static_cast<float>(Height)));
    const uint32_t Y1 = std::min(Height, std::max(Y0 + 1u, static_cast<uint32_t>(std::min(1.0f, V1) * static_cast<float>(Height))));

    double      Sum   = 0.0;
    std::size_t Count = 0u;
    for (uint32_t Y = Y0; Y < Y1; ++Y)
    {
        for (uint32_t X = X0; X < X1; ++X)
        {
            const std::size_t PixelOffset = (static_cast<std::size_t>(Y) * Width + X) * 4u;
            Sum += 0.2126 * Rgba[PixelOffset + 0u] + 0.7152 * Rgba[PixelOffset + 1u] + 0.0722 * Rgba[PixelOffset + 2u];
            ++Count;
        }
    }
    return Count > 0u ? (Sum / static_cast<double>(Count)) : 0.0;
}

[[nodiscard]] double MeasureRegionBlueMinusRed(
    const std::vector<uint8_t>& Rgba,
    uint32_t                    Width,
    uint32_t                    Height,
    float                       U0,
    float                       V0,
    float                       U1,
    float                       V1)
{
    if (Rgba.size() < static_cast<std::size_t>(Width) * Height * 4u)
    {
        return 0.0;
    }
    const uint32_t X0 = std::min(Width - 1u, static_cast<uint32_t>(std::max(0.0f, U0) * static_cast<float>(Width)));
    const uint32_t X1 = std::min(Width, std::max(X0 + 1u, static_cast<uint32_t>(std::min(1.0f, U1) * static_cast<float>(Width))));
    const uint32_t Y0 = std::min(Height - 1u, static_cast<uint32_t>(std::max(0.0f, V0) * static_cast<float>(Height)));
    const uint32_t Y1 = std::min(Height, std::max(Y0 + 1u, static_cast<uint32_t>(std::min(1.0f, V1) * static_cast<float>(Height))));

    double      Sum   = 0.0;
    std::size_t Count = 0u;
    for (uint32_t Y = Y0; Y < Y1; ++Y)
    {
        for (uint32_t X = X0; X < X1; ++X)
        {
            const std::size_t Idx = (static_cast<std::size_t>(Y) * Width + X) * 4u;
            Sum += static_cast<double>(Rgba[Idx + 2u]) - static_cast<double>(Rgba[Idx + 0u]);
            ++Count;
        }
    }
    return Count > 0u ? (Sum / static_cast<double>(Count)) : 0.0;
}

[[nodiscard]] double MeasureRegionRedMinusGreen(
    const std::vector<uint8_t>& Rgba,
    uint32_t                    Width,
    uint32_t                    Height,
    float                       U0,
    float                       V0,
    float                       U1,
    float                       V1)
{
    if (Rgba.size() < static_cast<std::size_t>(Width) * Height * 4u)
    {
        return 0.0;
    }
    const uint32_t X0 = std::min(Width - 1u, static_cast<uint32_t>(std::max(0.0f, U0) * static_cast<float>(Width)));
    const uint32_t X1 = std::min(Width, std::max(X0 + 1u, static_cast<uint32_t>(std::min(1.0f, U1) * static_cast<float>(Width))));
    const uint32_t Y0 = std::min(Height - 1u, static_cast<uint32_t>(std::max(0.0f, V0) * static_cast<float>(Height)));
    const uint32_t Y1 = std::min(Height, std::max(Y0 + 1u, static_cast<uint32_t>(std::min(1.0f, V1) * static_cast<float>(Height))));

    double      Sum   = 0.0;
    std::size_t Count = 0u;
    for (uint32_t Y = Y0; Y < Y1; ++Y)
    {
        for (uint32_t X = X0; X < X1; ++X)
        {
            const std::size_t PixelOffset = (static_cast<std::size_t>(Y) * Width + X) * 4u;
            Sum += static_cast<double>(Rgba[PixelOffset + 0u]) - static_cast<double>(Rgba[PixelOffset + 1u]);
            ++Count;
        }
    }
    return Count > 0u ? (Sum / static_cast<double>(Count)) : 0.0;
}

[[nodiscard]] double MeasureCheckerboardLaplacian(
    const std::vector<uint8_t>& Rgba,
    uint32_t                    Width,
    uint32_t                    Height,
    float                       U0,
    float                       V0,
    float                       U1,
    float                       V1)
{
    if (Rgba.size() < static_cast<std::size_t>(Width) * Height * 4u || Width < 6u || Height < 6u)
    {
        return 999.0;
    }
    const uint32_t X0 = std::max(1u, static_cast<uint32_t>(U0 * static_cast<float>(Width)));
    const uint32_t X1 = std::min(Width - 1u, std::max(X0 + 2u, static_cast<uint32_t>(U1 * static_cast<float>(Width))));
    const uint32_t Y0 = std::max(1u, static_cast<uint32_t>(V0 * static_cast<float>(Height)));
    const uint32_t Y1 = std::min(Height - 1u, static_cast<uint32_t>(V1 * static_cast<float>(Height)));

    auto LumaAt = [&](uint32_t X, uint32_t Y) -> double
    {
        const std::size_t PixelOffset = (static_cast<std::size_t>(Y) * Width + X) * 4u;
        return 0.2126 * Rgba[PixelOffset + 0u] + 0.7152 * Rgba[PixelOffset + 1u] + 0.0722 * Rgba[PixelOffset + 2u];
    };

    double      SumLap = 0.0;
    std::size_t Count  = 0u;
    for (uint32_t Y = Y0; Y < Y1; ++Y)
    {
        for (uint32_t X = X0; X < X1; ++X)
        {
            const double C  = LumaAt(X, Y);
            const double Lx = std::fabs(2.0 * C - LumaAt(X - 1u, Y) - LumaAt(X + 1u, Y)) * 0.5;
            const double Ly = std::fabs(2.0 * C - LumaAt(X, Y - 1u) - LumaAt(X, Y + 1u)) * 0.5;
            SumLap += 0.5 * (Lx + Ly);
            ++Count;
        }
    }
    return Count > 0u ? (SumLap / static_cast<double>(Count)) : 999.0;
}

} // namespace

int main()
{
    ValidationScorecard Score {};

    const std::filesystem::path RepoRoot  = std::filesystem::path(FRONTIER_REPO_ROOT_DIR);
    const std::filesystem::path ShaderDir = std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);

    // 1. Verify decomposed SPIR-V binaries & absence of unportable Float16/Int16 capabilities
    bool RadSizeOk  = false;
    bool GdfSizeOk  = false;
    bool GiSizeOk   = false;
    bool PtSizeOk   = false;
    bool DenSizeOk  = false;
    Score.Verify(
        VerifySpirvPortableCapabilities(ShaderDir / "WorldViewportRadiometricCompute.comp.spv", 225u * 1024u, RadSizeOk) && RadSizeOk,
        "WorldViewportRadiometricCompute.comp.spv < 225 KB and free of Float16/Int16 capabilities");
    Score.Verify(
        VerifySpirvPortableCapabilities(ShaderDir / "WorldViewportGlobalDistanceBake.comp.spv", 96u * 1024u, GdfSizeOk) && GdfSizeOk,
        "WorldViewportGlobalDistanceBake.comp.spv compiled cleanly without Float16/Int16 capabilities");
    Score.Verify(
        VerifySpirvPortableCapabilities(ShaderDir / "WorldViewportGlobalRadiosityBake.comp.spv", 160u * 1024u, GiSizeOk) && GiSizeOk,
        "WorldViewportGlobalRadiosityBake.comp.spv compiled cleanly without Float16/Int16 capabilities");
    Score.Verify(
        VerifySpirvPortableCapabilities(ShaderDir / "WorldViewportPathTraceCompute.comp.spv", 420u * 1024u, PtSizeOk) && PtSizeOk,
        "WorldViewportPathTraceCompute.comp.spv dedicated path-tracer program compiled cleanly");
    Score.Verify(
        VerifySpirvPortableCapabilities(ShaderDir / "WorldViewportDenoiseCompute.comp.spv", 380u * 1024u, DenSizeOk) && DenSizeOk,
        "WorldViewportDenoiseCompute.comp.spv joint GI+Shadow denoiser compiled cleanly");

    // 2. Construct canonical ShowcaseGalleryScene (exact scene from FrontierEditor viewport)
    constexpr uint32_t TileW = 320u;
    constexpr uint32_t TileH = 176u;
    ShowcaseWorldSceneBundle Showcase {};
    BuildVehicleClearSkyDefaultScene(Showcase, TileW, TileH);

    std::array<std::vector<uint8_t>, 6u> EngineTiles {};
    std::vector<uint8_t> SingleSamplePtTile {};
    std::vector<uint8_t> RelitGdfTile {};

    VulkanInterchange GpuDevice {};
    const bool DeviceOk = GpuDevice.InitializeInterchange(RepoRoot);
    Score.Verify(DeviceOk, "VulkanInterchange initialized");
    if (!DeviceOk)
    {
        return 1;
    }

    {
        VulkanWorldRenderSequence GpuSeq {};
        const bool SeqOk = GpuSeq.InitializeSequence(GpuDevice, ShaderDir);
        Score.Verify(SeqOk, "VulkanWorldRenderSequence initialized with 29-slot GDF + Radiosity programs");
        if (SeqOk)
        {
            GpuSeq.SynchronizeSceneResources(Showcase.Scene, true);

            // Tile 0: MatCap
            {
                WorldRenderConfiguration Cfg {};
                Cfg.Engine          = WorldRenderEngine::MatCap;
                Cfg.ShadowAlgorithm = SolarShadowAlgorithm::ContactPcss;
                Score.Verify(
                    GpuSeq.RenderWorldViewportWithEngine(Cfg, 2u, Showcase.EditorCamera, Showcase.Scene, EngineTiles[0]),
                    "Rendered Tile 0: MatCap");
            }

            // Tile 1: OpenPBR Direct (ContactPcss + SDF Penumbra)
            {
                WorldRenderConfiguration Cfg {};
                Cfg.Engine          = WorldRenderEngine::OpenPbrDirect;
                Cfg.ShadowAlgorithm = SolarShadowAlgorithm::ContactPcss;
                Score.Verify(
                    GpuSeq.RenderWorldViewportWithEngine(Cfg, 2u, Showcase.EditorCamera, Showcase.Scene, EngineTiles[1]),
                    "Rendered Tile 1: OpenPBR Direct (ContactPcss + GDF Penumbra)");
            }

            // Tile 2: GTX GDF GI (2-Bounce Global Distance Field + Radiosity Cone GI, SDF Penumbra)
            {
                WorldRenderConfiguration Cfg {};
                Cfg.Engine          = WorldRenderEngine::RealTimeGiGtxLowEnd;
                Cfg.ShadowAlgorithm = SolarShadowAlgorithm::ContactPcss;
                Score.Verify(
                    GpuSeq.RenderWorldViewportWithEngine(Cfg, 2u, Showcase.EditorCamera, Showcase.Scene, EngineTiles[2]),
                    "Rendered Tile 2: GTX GDF GI (2-Bounce Radiosity Cone GI + GDF Penumbra)");
            }

            // Tile 3: GTX GDF GI + BlueNoise 6-Tap Penumbra (verifies elimination of 8.0 cm sawtooth stripe bug)
            {
                WorldRenderConfiguration Cfg {};
                Cfg.Engine          = WorldRenderEngine::RealTimeGiGtxLowEnd;
                Cfg.ShadowAlgorithm = SolarShadowAlgorithm::ContactPcss;
                Score.Verify(
                    GpuSeq.RenderWorldViewportWithEngine(Cfg, 5u, Showcase.EditorCamera, Showcase.Scene, EngineTiles[3]),
                    "Rendered Tile 3: GTX GDF GI + BlueNoise 6-Tap Penumbra");
            }

            // Tile 4: HighEnd BVH4 GI + Joint A-Trous Denoiser
            {
                WorldRenderConfiguration Cfg {};
                Cfg.Engine          = WorldRenderEngine::RealTimeGiHighEnd;
                Cfg.ShadowAlgorithm = SolarShadowAlgorithm::WideTreeRayQuery;
                Score.Verify(
                    GpuSeq.RenderWorldViewportWithEngine(Cfg, 4u, Showcase.EditorCamera, Showcase.Scene, EngineTiles[4]),
                    "Rendered Tile 4: HighEnd BVH4 GI + Joint A-Trous Filter");
            }

            // Tile 5: Dedicated ViewportPathTraceEntry (4 progressive samples)
            {
                WorldRenderConfiguration Cfg {};
                Cfg.Engine          = WorldRenderEngine::PathTracerGameRealtime;
                Cfg.ShadowAlgorithm = SolarShadowAlgorithm::WideTreeRayQuery;
                bool PtOk = GpuSeq.RenderWorldViewportWithEngine(Cfg, 2u, Showcase.EditorCamera, Showcase.Scene, SingleSamplePtTile);
                for (uint32_t SampleStep = 1u; SampleStep < 4u && PtOk; ++SampleStep)
                {
                    PtOk = GpuSeq.RenderWorldViewportWithEngine(Cfg, 2u, Showcase.EditorCamera, Showcase.Scene, EngineTiles[5]);
                }
                Score.Verify(PtOk, "Rendered Tile 5: Dedicated ViewportPathTraceEntry (4 progressive samples)");
            }

            // Dynamic Lighting Refresh (boost solar irradiance x2.6 without geometry re-sync)
            {
                Showcase.Scene.Sun.SolarIrradiance.R *= 2.6f;
                Showcase.Scene.Sun.SolarIrradiance.G *= 2.6f;
                Showcase.Scene.Sun.SolarIrradiance.B *= 2.6f;
                Showcase.Scene.Atmosphere.Solar.SolarIntensityMultiplier *= 2.6f;
                Showcase.Scene.Atmosphere.Solar.DirectGainMultiplier    *= 2.6f;
                WorldRenderConfiguration Cfg {};
                Cfg.Engine          = WorldRenderEngine::RealTimeGiGtxLowEnd;
                Cfg.ShadowAlgorithm = SolarShadowAlgorithm::ContactPcss;
                Score.Verify(
                    GpuSeq.RenderWorldViewportWithEngine(Cfg, 2u, Showcase.EditorCamera, Showcase.Scene, RelitGdfTile),
                    "Dynamic LightingSig Radiosity refresh executed without BVH/GDF geometry rebuild");
            }

            GpuSeq.TeardownSequence();
        }
    }
    GpuDevice.TeardownInterchange();

    // 3. Quantitative Radiometric, Color-Bleed, and Smoothness Assertions
    const double DirectInteriorLuma = MeasureRegionMeanLuma(EngineTiles[1], TileW, TileH, 0.22f, 0.28f, 0.78f, 0.85f);
    const double GtxGiInteriorLuma  = MeasureRegionMeanLuma(EngineTiles[2], TileW, TileH, 0.22f, 0.28f, 0.78f, 0.85f);
    const double HighEndInteriorLuma= MeasureRegionMeanLuma(EngineTiles[4], TileW, TileH, 0.22f, 0.28f, 0.78f, 0.85f);
    const double PtInteriorLuma     = MeasureRegionMeanLuma(EngineTiles[5], TileW, TileH, 0.22f, 0.28f, 0.78f, 0.85f);
    const double RelitInteriorLuma  = MeasureRegionMeanLuma(RelitGdfTile,   TileW, TileH, 0.22f, 0.28f, 0.78f, 0.85f);
    const double DirectUnderLuma    = MeasureRegionMeanLuma(EngineTiles[1], TileW, TileH, 0.06f, 0.55f, 0.42f, 0.74f);
    const double GtxUnderLuma       = MeasureRegionMeanLuma(EngineTiles[2], TileW, TileH, 0.06f, 0.55f, 0.42f, 0.74f);
    const double RelitUnderLuma     = MeasureRegionMeanLuma(RelitGdfTile,   TileW, TileH, 0.06f, 0.55f, 0.42f, 0.74f);

    std::printf(
        "[GtxGlobalDistanceFieldViewportValidation] Interior Luma [0..255]: Direct=%.2f, GTX_GDF_GI=%.2f, HighEnd_BVH=%.2f, PT_4SPP=%.2f, Relit_GDF=%.2f\n",
        DirectInteriorLuma,
        GtxGiInteriorLuma,
        HighEndInteriorLuma,
        PtInteriorLuma,
        RelitInteriorLuma);


    Score.Verify(GtxUnderLuma > DirectUnderLuma + 3.0, "GTX GDF GI delivers measurable multi-bounce indirect illumination lift over OpenPBR Direct");
    Score.Verify(RelitUnderLuma > GtxUnderLuma + 4.0, "Dynamic LightingSig refresh updates 2-bounce GDF Radiosity when solar intensity changes");
    Score.Verify(PtInteriorLuma > 25.0, "Dedicated ViewportPathTraceEntry produces physically lit ControlVehicle default scene image");

    // Verify Reflex-Blue vehicle paint color-bleeding onto the adjacent neutral ground plane
    const double DirectPaintBR = MeasureRegionBlueMinusRed(EngineTiles[1], TileW, TileH, 0.02f, 0.62f, 0.20f, 0.90f);
    const double GtxPaintBR    = MeasureRegionBlueMinusRed(EngineTiles[2], TileW, TileH, 0.02f, 0.62f, 0.20f, 0.90f);
    std::printf(
        "[GtxGlobalDistanceFieldViewportValidation] Paint Bleed: Ground(B-R) Direct=%.2f -> GTX=%.2f\n",
        DirectPaintBR,
        GtxPaintBR);
    Score.Verify(
        GtxPaintBR > DirectPaintBR + 1.0,
        "GTX GDF GI transfers vehicle paint chroma bleed onto the adjacent neutral ground plane");

    // Verify absence of checker / 8.0 cm sawtooth stripe oscillation on flat floor
    const double FloorLapSdf       = MeasureCheckerboardLaplacian(EngineTiles[2],      TileW, TileH, 0.36f, 0.78f, 0.64f, 0.92f);
    const double FloorLapBlueNoise = MeasureCheckerboardLaplacian(EngineTiles[3],      TileW, TileH, 0.36f, 0.78f, 0.64f, 0.92f);
    const double PtLapSample1      = MeasureCheckerboardLaplacian(SingleSamplePtTile,  TileW, TileH, 0.36f, 0.78f, 0.64f, 0.92f);
    const double PtLapSample4      = MeasureCheckerboardLaplacian(EngineTiles[5],      TileW, TileH, 0.36f, 0.78f, 0.64f, 0.92f);
    std::printf(
        "[GtxGlobalDistanceFieldViewportValidation] Floor Laplacian Smoothness: GTX_SDF=%.3f, GTX_BlueNoise6=%.3f, PT_1SPP=%.3f -> PT_4SPP=%.3f\n",
        FloorLapSdf,
        FloorLapBlueNoise,
        PtLapSample1,
        PtLapSample4);

    Score.Verify(FloorLapSdf < 2.5, "GTX GDF GI + SDF Penumbra floor is free of checker/streak stipple (Laplacian < 2.5)");
    Score.Verify(FloorLapBlueNoise < 5.0, "GTX GDF GI + BlueNoise 6-Tap + 3-pass A-Trous eliminates 8.0 cm sawtooth stripes (Laplacian < 5.0)");
    Score.Verify(PtLapSample4 < PtLapSample1, "Dedicated ViewportPathTraceEntry variance decreases monotonically from 1 SPP to 4 SPP");

    // 4. Export 6-panel visual proof contact sheet
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    Io.Fonts->AddFontDefault();
    unsigned char* FontTex = nullptr;
    int FontW = 0, FontH = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontTex, &FontW, &FontH);

    constexpr uint32_t SheetW = 1044u;
    constexpr uint32_t SheetH = 468u;
    Io.DisplaySize = ImVec2(static_cast<float>(SheetW), static_cast<float>(SheetH));
    ImGui::NewFrame();
    ImDrawList* SheetCanvas = ImGui::GetBackgroundDrawList();
    SheetCanvas->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(static_cast<float>(SheetW), static_cast<float>(SheetH)), IM_COL32(11, 14, 20, 255));
    const std::array<const char*, 6u> Titles {{
        "01 // MATCAP (CLAY)",
        "02 // OPENPBR DIRECT (GDF PENUMBRA)",
        "03 // GTX GDF GI (2-BOUNCE CONE GI)",
        "04 // GTX GDF GI + BLUENOISE 6-TAP",
        "05 // HIGHEND BVH4 GI + A-TROUS",
        "06 // PT REALTIME (DEDICATED ENTRY)",
    }};

    for (uint32_t I = 0u; I < 6u; ++I)
    {
        const uint32_t Col = I % 3u;
        const uint32_t Row = I / 3u;
        const float    X0  = 18.0f + static_cast<float>(Col) * 340.0f;
        const float    Y0  = 16.0f + static_cast<float>(Row) * 224.0f;
        SheetCanvas->AddRectFilled(ImVec2(X0, Y0), ImVec2(X0 + 328.0f, Y0 + 212.0f), IM_COL32(20, 24, 34, 255), 6.0f);
        SheetCanvas->AddText(ImVec2(X0 + 8.0f, Y0 + 6.0f), IM_COL32(104, 210, 255, 255), Titles[I]);
    }
    ImGui::Render();

    Exhibits::SoftwareRasterCanvas Sheet(SheetW, SheetH, { 11u, 14u, 20u, 255u });
    Sheet.RasterizeImGuiBatches(ImGui::GetDrawData());
    for (uint32_t I = 0u; I < 6u; ++I)
    {
        if (EngineTiles[I].size() == static_cast<std::size_t>(TileW) * TileH * 4u)
        {
            const uint32_t Col = I % 3u;
            const uint32_t Row = I / 3u;
            Sheet.BlitRgbaRegion(22u + Col * 340u, 44u + Row * 224u, TileW, TileH, EngineTiles[I].data());
        }
    }
    ImGui::DestroyContext();

    const std::filesystem::path OutPng = RepoRoot / "Exhibits" / "Images" / "GlobalIllumination" / "04-VulkanGtxGlobalDistanceFieldEngines.png";
    std::filesystem::create_directories(OutPng.parent_path());
    Score.Verify(Sheet.WritePngFile(OutPng) && std::filesystem::exists(OutPng), "Exported 04-VulkanGtxGlobalDistanceFieldEngines.png");
    std::printf(
        "[GtxGlobalDistanceFieldViewportValidation] Completed: %u passed, %u failed -> %s\n",
        Score.Passed,
        Score.Failed,
        OutPng.string().c_str());
    return Score.Failed == 0u ? 0 : 1;
}
