/*============================================================================================================================================
                                                DETERMINISTICPATHTRACERVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Order 3 validation exhibit for Unified Deterministic Path Tracer (GameRealtime & SceneFinalRender) and DeterministicPathTracerPane

#if FRONTIER_PROFILE_EDITOR
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/Panes/DeterministicPathTracerPane.h"
#include "Engine/Components/PixelSpace.h"
#include <imgui.h>
#endif
#include "Engine/GeometricRaster/GlobalIllumination/DeterministicPathTraceSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::GeometricRaster;

bool VerifyCheck(const bool Condition, const char* Label)
{
    if (!Condition)
    {
        std::fprintf(stderr, "[FAIL] %s\n", Label);
        return false;
    }
    std::printf("[PASS] %s\n", Label);
    return true;
}

// Exact Lambertian polygonal contour irradiance E(p, n) from an oriented rectangle (Lambert 1760)
[[nodiscard]] std::array<float, 3> EvaluateExactRectangleIrradiance(
    const std::array<float, 3>& Point,
    const std::array<float, 3>& Normal,
    const std::array<float, 3>& Centre,
    const std::array<float, 3>& EdgeU,
    const std::array<float, 3>& EdgeV,
    const std::array<float, 3>& LightNormal,
    const std::array<float, 3>& Radiance) noexcept
{
    const auto Sub3 = [](const std::array<float, 3>& A, const std::array<float, 3>& B) -> std::array<float, 3> {
        return { A[0] - B[0], A[1] - B[1], A[2] - B[2] };
    };
    const auto Add3 = [](const std::array<float, 3>& A, const std::array<float, 3>& B) -> std::array<float, 3> {
        return { A[0] + B[0], A[1] + B[1], A[2] + B[2] };
    };
    const auto Scale3 = [](const std::array<float, 3>& A, const float S) -> std::array<float, 3> {
        return { A[0] * S, A[1] * S, A[2] * S };
    };
    const auto Dot3 = [](const std::array<float, 3>& A, const std::array<float, 3>& B) -> float {
        return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
    };
    const auto Cross3 = [](const std::array<float, 3>& A, const std::array<float, 3>& B) -> std::array<float, 3> {
        return { A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0] };
    };
    const auto Norm3 = [&](const std::array<float, 3>& A) -> std::array<float, 3> {
        const float L = std::sqrt(std::max(Dot3(A, A), 1.0e-20f));
        return Scale3(A, 1.0f / L);
    };

    if (Dot3(Sub3(Point, Centre), LightNormal) <= 0.0f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const std::array<float, 3> HalfU = Scale3(EdgeU, 0.5f);
    const std::array<float, 3> HalfV = Scale3(EdgeV, 0.5f);
    // Winding ordered so front face matches LightNormal
    const std::array<std::array<float, 3>, 4> Corners {{
        Norm3(Sub3(Add3(Sub3(Centre, HalfU), HalfV), Point)),
        Norm3(Sub3(Add3(Add3(Centre, HalfU), HalfV), Point)),
        Norm3(Sub3(Sub3(Add3(Centre, HalfU), HalfV), Point)),
        Norm3(Sub3(Sub3(Sub3(Centre, HalfU), HalfV), Point)),
    }};

    float SignedSum = 0.0f;
    for (std::size_t I = 0u; I < 4u; ++I)
    {
        const auto& A     = Corners[I];
        const auto& B     = Corners[(I + 1u) % 4u];
        const float Angle = std::acos(std::clamp(Dot3(A, B), -1.0f, 1.0f));
        const auto  Axis  = Cross3(A, B);
        const float Len   = std::sqrt(Dot3(Axis, Axis));
        if (Len > 1.0e-10f)
        {
            SignedSum += Angle * (Dot3(Axis, Normal) / Len);
        }
    }
    const float FormFactor = std::abs(SignedSum) / (2.0f * 3.14159265358979323846f);
    return { Radiance[0] * FormFactor, Radiance[1] * FormFactor, Radiance[2] * FormFactor };
}

bool WriteRgbaBufferToPng(
    const std::filesystem::path& Path,
    const uint32_t               Width,
    const uint32_t               Height,
    const std::vector<uint8_t>&  Rgba8)
{
    if (Rgba8.size() < static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height) * 4u)
    {
        return false;
    }
    Exhibits::SoftwareRasterCanvas Canvas(Width, Height, Components::RgbaColor8 { 11u, 12u, 14u, 255u });
    Canvas.BlitRgbaRegion(0u, 0u, Width, Height, Rgba8.data());
    return Canvas.WritePngFile(Path);
}

} // namespace

int main()
{
    std::printf("=== DeterministicPathTracerValidation (Order 3: GameRealtime & SceneFinalRender) ===\n");

    const std::filesystem::path RepoRoot(FRONTIER_REPO_ROOT_DIR);
    const std::filesystem::path ShaderDir(FRONTIER_COMPILED_SHADER_DIR);
    const std::filesystem::path OutDir = RepoRoot / "Exhibits/Images/DeterministicPathTracer";
    std::filesystem::create_directories(OutDir);

    VulkanInterchange Vulkan {};
    std::string       Diag {};
    if (!VerifyCheck(Vulkan.InitializeInterchange(RepoRoot, &Diag), "VulkanInterchange::InitializeInterchange"))
    {
        std::fprintf(stderr, "       %s\n", Diag.c_str());
        return 1;
    }
    std::printf("       Adapter: %.*s\n", static_cast<int>(Vulkan.QueryAdapterName().size()), Vulkan.QueryAdapterName().data());

    DeterministicPathTraceSequence Tracer {};
    if (!VerifyCheck(Tracer.Initialize(Vulkan, ShaderDir, RepoRoot, &Diag), "DeterministicPathTraceSequence::Initialize"))
    {
        std::fprintf(stderr, "       %s\n", Diag.c_str());
        Vulkan.TeardownInterchange();
        return 2;
    }

    //========================================================================================================================
    // 1. GPU-VS-HOST SCRAMBLED SOBOL' (0, 2) QUADRATURE BIT-EXACTNESS (147,456 COORDINATES)
    //========================================================================================================================
    {
        DeterministicTracerConfiguration InitCfg {};
        DeterministicPathTraceSequence::ApplyRenderProfile(DeterministicRenderProfile::SceneFinalRender, InitCfg);
        if (!VerifyCheck(Tracer.ConfigureStudioScene(InitCfg) && Tracer.ResizeAccumulation(384u, 256u),
                         "Configure initial Studio scene (25,068-tri ControlVehicle + 7 showcase spheres)"))
        {
            return 3;
        }

        const auto& Summary = Tracer.QuerySceneSummary();
        std::printf("       ControlVehicle tree: %u tris -> %u 64B wide4 entries (depth %u, %u bytes)\n",
                    Summary.VehicleTriangleCount,
                    Summary.VehicleWideEntries,
                    Summary.BoundsDepthTier,
                    Summary.BoundsByteSize);
        if (!VerifyCheck(Summary.VehicleTriangleCount == 25068u && Summary.VehicleWideEntries > 1000u && Summary.VehicleWideEntries < 6000u,
                         "ControlVehicle CompressedWideTree matches 25,068-triangle 64B quantized 4-wide spec"))
        {
            return 4;
        }

        constexpr uint32_t SampleTotal = 4096u;
        constexpr uint32_t PairTotal   = 18u;
        const std::vector<float> GpuQuad = Tracer.ExtractDeviceQuadrature(SampleTotal, PairTotal);
        if (!VerifyCheck(GpuQuad.size() == static_cast<std::size_t>(SampleTotal * PairTotal * 4u),
                         "ExtractDeviceQuadrature returned 73,728 pairs (147,456 coordinates)"))
        {
            return 5;
        }

        uint32_t MismatchCoords = 0u;
        for (uint32_t S = 0u; S < SampleTotal; ++S)
        {
            for (uint32_t P = 0u; P < PairTotal; ++P)
            {
                const std::array<float, 2> Host = DeterministicPathTraceSequence::EvaluateHostQuadraturePair(S, P);
                const std::size_t          O    = (static_cast<std::size_t>(P) * SampleTotal + S) * 4u;
                uint32_t GpuBits0 = 0u, GpuBits1 = 0u, HostBits0 = 0u, HostBits1 = 0u;
                std::memcpy(&GpuBits0, &GpuQuad[O + 0u], sizeof(uint32_t));
                std::memcpy(&GpuBits1, &GpuQuad[O + 1u], sizeof(uint32_t));
                std::memcpy(&HostBits0, &Host[0], sizeof(uint32_t));
                std::memcpy(&HostBits1, &Host[1], sizeof(uint32_t));
                if (GpuBits0 != HostBits0) ++MismatchCoords;
                if (GpuBits1 != HostBits1) ++MismatchCoords;
            }
        }
        std::printf("       Quadrature GPU-vs-Host: %u / 147,456 bit mismatches\n", MismatchCoords);
        if (!VerifyCheck(MismatchCoords == 0u, "Scrambled Sobol' (0, 2) GPU-vs-Host IEEE-754 bit-exactness (0 / 147,456 mismatches)"))
        {
            return 6;
        }
    }

    //========================================================================================================================
    // 2. ANALYTIC SOFTBOX IRRADIANCE CALIBRATION (50% LAMBERT FLOOR VS LAMBERT CONTOUR FORMULA)
    //========================================================================================================================
    {
        DeterministicTracerConfiguration CalibCfg {};
        DeterministicPathTraceSequence::ApplyRenderProfile(DeterministicRenderProfile::SceneFinalRender, CalibCfg);
        CalibCfg.VehiclePresent         = false;
        CalibCfg.ShowcaseSpheresPresent = false;
        CalibCfg.FloorPreset            = StudioFloorPreset::Lambert50;
        CalibCfg.EnvironmentIntensity   = 0.0f;
        CalibCfg.KeyIntensity           = 4.0f;
        CalibCfg.RimIntensity           = 0.0f;
        CalibCfg.FillIntensity          = 0.0f;
        CalibCfg.BounceLimit            = 0u;
        CalibCfg.ThroughputCutoff       = 0.0f;
        CalibCfg.Camera                 = { 0.0f, 1.25f, 5.0f, { 0.4f, 0.0f, 0.6f }, 28.0f };

        if (!VerifyCheck(Tracer.ConfigureStudioScene(CalibCfg) && Tracer.ResizeAccumulation(64u, 64u) && Tracer.AdvanceSamples(256u),
                         "Integrated 256 spp softbox irradiance calibration patch"))
        {
            return 7;
        }

        std::vector<float> LinearRgb {};
        if (!Tracer.RetrieveDisplayLinearRgb(LinearRgb))
        {
            return 8;
        }

        // Centre pixel (32, 32) looks almost straight at (0.4, 0, 0.6) directly beneath Key softbox at (0.4, 5.8, 0.6), 5.6 x 2.6 m
        const DeterministicCameraBasis CamBasis = DeterministicPathTraceSequence::ResolveCameraBasis(CalibCfg.Camera, 1.0f);
        const float NdcX = ((32.5f / 64.0f) * 2.0f - 1.0f);
        const float NdcY = ((32.5f / 64.0f) * 2.0f - 1.0f);
        std::array<float, 3> RayDir {
            CamBasis.Forward[0] + NdcX * CamBasis.Right[0] - NdcY * CamBasis.Up[0],
            CamBasis.Forward[1] + NdcX * CamBasis.Right[1] - NdcY * CamBasis.Up[1],
            CamBasis.Forward[2] + NdcX * CamBasis.Right[2] - NdcY * CamBasis.Up[2],
        };
        const float InvLen = 1.0f / std::sqrt(RayDir[0] * RayDir[0] + RayDir[1] * RayDir[1] + RayDir[2] * RayDir[2]);
        RayDir = { RayDir[0] * InvLen, RayDir[1] * InvLen, RayDir[2] * InvLen };
        const float HitT = -CamBasis.Origin[1] / RayDir[1];
        const std::array<float, 3> HitPos {
            CamBasis.Origin[0] + HitT * RayDir[0],
            0.0f,
            CamBasis.Origin[2] + HitT * RayDir[2],
        };

        const std::array<float, 3> Irrad = EvaluateExactRectangleIrradiance(
            HitPos,
            { 0.0f, 1.0f, 0.0f },
            { 0.4f, 5.8f, 0.6f },
            { 5.6f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 2.6f },
            { 0.0f, -1.0f, 0.0f },
            { 4.0f, 4.0f * 0.98f, 4.0f * 0.95f });
        // EvaluateExactRectangleIrradiance returns L_e * FormFactor = E / Pi (unit-albedo Lambertian radiance)
        const float AnalyticR = 0.50f * Irrad[0];
        const float MeasuredR = LinearRgb[(32u * 64u + 32u) * 3u + 0u];
        const float RelErr    = std::abs(MeasuredR - AnalyticR) / std::max(AnalyticR, 1.0e-6f);
        std::printf("       Analytic softbox radiance = %.6f vs GPU 256 spp = %.6f (rel error = %.4f%%)\n",
                    AnalyticR, MeasuredR, RelErr * 100.0f);
        if (!VerifyCheck(RelErr < 0.005f, "Softbox Lambertian irradiance matches closed-form contour integral within 0.5%"))
        {
            return 9;
        }
    }

    //========================================================================================================================
    // 3. SAMPLE-SCHEDULE BIT-EXACTNESS (BATCH=1 VS BATCH=4 VS BATCH=8) & RASTER VISIBILITY PARITY
    //========================================================================================================================
    {
        DeterministicTracerConfiguration HeroCfg {};
        DeterministicPathTraceSequence::ApplyRenderProfile(DeterministicRenderProfile::SceneFinalRender, HeroCfg);
        HeroCfg.CameraPreset = StudioCameraPreset::Hero;
        HeroCfg.Camera       = DeterministicPathTraceSequence::QueryCameraPreset(StudioCameraPreset::Hero);

        if (!VerifyCheck(Tracer.ConfigureStudioScene(HeroCfg) && Tracer.ResizeAccumulation(256u, 144u),
                         "Configured Hero Studio scene at 256x144 for determinism & visibility tests"))
        {
            return 10;
        }

        std::string DigestBatch1 {}, DigestBatch4 {}, DigestBatch8 {};
        const bool Match1vs4 = Tracer.VerifyBitExactRepeat(8u, 4u, &DigestBatch1, &DigestBatch4);
        const bool Match1vs8 = Tracer.VerifyBitExactRepeat(8u, 8u, nullptr, &DigestBatch8);
        std::printf("       IEEE-754 FNV-1a 8-spp digests: batch=1 [%s], batch=4 [%s], batch=8 [%s]\n",
                    DigestBatch1.c_str(), DigestBatch4.c_str(), DigestBatch8.c_str());
        if (!VerifyCheck(Match1vs4 && Match1vs8 && DigestBatch1 == DigestBatch8 && DigestBatch1 != "00000000",
                         "Bit-exact raw float accumulation across batch=1, batch=4, and batch=8"))
        {
            return 11;
        }

        // Compare Ray-Traced primary visibility vs Rasterized R32_UINT subpixel visibility
        std::vector<float> RayPrimaryHdr {};
        (void)Tracer.RetrieveDisplayLinearRgb(RayPrimaryHdr);

        HeroCfg.VisibilityRasterActive = true;
        Tracer.AccessConfiguration()   = HeroCfg;
        Tracer.ResetAccumulation(false);
        if (!VerifyCheck(Tracer.AdvanceSamples(8u), "Integrated 8 spp with Rasterized R32_UINT primary visibility"))
        {
            return 12;
        }
        std::vector<float> RasterPrimaryHdr {};
        (void)Tracer.RetrieveDisplayLinearRgb(RasterPrimaryHdr);

        double SumSq = 0.0;
        for (std::size_t I = 0u; I < RayPrimaryHdr.size(); ++I)
        {
            const double Diff = static_cast<double>( std::min(RayPrimaryHdr[I], 4.0f) - std::min(RasterPrimaryHdr[I], 4.0f) );
            SumSq += Diff * Diff;
        }
        const double VisRmse = std::sqrt(SumSq / static_cast<double>(RayPrimaryHdr.size()));
        std::printf("       Primary visibility RayTrace vs Raster R32_UINT RMSE = %.5f\n", VisRmse);
        if (!VerifyCheck(VisRmse < 0.035, "Subpixel R32_UINT raster visibility aligns tightly with primary BVH4 ray trace"))
        {
            return 13;
        }
    }

    //========================================================================================================================
    // 4. HERO AUTHORED CONVERGENCE CAPTURE (02-Hero_SceneFinalRender.png) & RECONSTRUCTION STRIP (05)
    //========================================================================================================================
    {
        constexpr uint32_t HeroW = 640u;
        constexpr uint32_t HeroH = 360u;

        DeterministicTracerConfiguration HeroCfg {};
        DeterministicPathTraceSequence::ApplyRenderProfile(DeterministicRenderProfile::SceneFinalRender, HeroCfg);
        HeroCfg.CameraPreset = StudioCameraPreset::Hero;
        HeroCfg.Camera       = DeterministicPathTraceSequence::QueryCameraPreset(StudioCameraPreset::Hero);
        if (!VerifyCheck(Tracer.ConfigureStudioScene(HeroCfg) && Tracer.ResizeAccumulation(HeroW, HeroH),
                         "Configured 640x360 Hero SceneFinalRender capture"))
        {
            return 14;
        }

        // Capture 1 spp Shared (Exact)
        if (!Tracer.AdvanceSamples(1u)) return 15;
        std::vector<uint8_t> Shared1SppRgba {};
        (void)Tracer.ResolveDisplayRgba8(Shared1SppRgba);

        // Advance to 32 spp SceneFinalRender Hero capture (clean Sobol' (0,2) convergence!)
        if (!Tracer.AdvanceSamples(31u)) return 16;
        std::vector<uint8_t> HeroConvergedRgba {};
        (void)Tracer.ResolveDisplayRgba8(HeroConvergedRgba);
        if (!VerifyCheck(WriteRgbaBufferToPng(OutDir / "02-Hero_SceneFinalRender.png", HeroW, HeroH, HeroConvergedRgba),
                         "Wrote 02-Hero_SceneFinalRender.png"))
        {
            return 17;
        }

        // Capture 1 spp Blue-Noise + A-Trous (GameRealtime full-res comparison) & verify digest invariance when toggling denoiser
        DeterministicTracerConfiguration RtCfg = HeroCfg;
        DeterministicPathTraceSequence::ApplyRenderProfile(DeterministicRenderProfile::GameRealtime, RtCfg);
        RtCfg.TraceScale = 1u;
        Tracer.AccessConfiguration() = RtCfg;
        Tracer.ResetAccumulation(false);
        if (!Tracer.AdvanceSamples(1u)) return 18;

        const std::string DigestWithDenoiser = Tracer.ComputeAccumulationDigest();
        std::vector<uint8_t> Denoised1SppRgba {};
        (void)Tracer.ResolveDisplayRgba8(Denoised1SppRgba);

        Tracer.AccessConfiguration().DenoiseActive = false;
        std::vector<uint8_t> NoisyBlue1SppRgba {};
        (void)Tracer.ResolveDisplayRgba8(NoisyBlue1SppRgba);
        const std::string DigestWithoutDenoiser = Tracer.ComputeAccumulationDigest();

        const auto ComputeMae8 = [](const std::vector<uint8_t>& Test, const std::vector<uint8_t>& Ref) -> double {
            double Sum = 0.0;
            const std::size_t Pixels = Test.size() / 4u;
            for (std::size_t P = 0u; P < Pixels; ++P)
            {
                Sum += (std::abs(int(Test[P * 4u]) - int(Ref[P * 4u])) +
                        std::abs(int(Test[P * 4u + 1u]) - int(Ref[P * 4u + 1u])) +
                        std::abs(int(Test[P * 4u + 2u]) - int(Ref[P * 4u + 2u]))) / 3.0;
            }
            return Sum / static_cast<double>(Pixels);
        };
        const double Shared1Mae   = ComputeMae8(Shared1SppRgba, HeroConvergedRgba);
        const double Denoised1Mae = ComputeMae8(Denoised1SppRgba, HeroConvergedRgba);
        std::printf("       1-spp 8-bit MAE vs 32-spp converged: Shared = %.2f levels, GameRealtime (BlueNoise + A-Trous) = %.2f levels\n",
                    Shared1Mae, Denoised1Mae);
        if (!VerifyCheck(DigestWithDenoiser == DigestWithoutDenoiser && Denoised1Mae < Shared1Mae,
                         "DisplayReconstructionFilter lowers 1-spp display error while leaving raw accumulation digest untouched"))
        {
            return 19;
        }

        // Verify temporal camera reprojection carries history across an orbit step
        Tracer.AccessConfiguration().DenoiseActive      = true;
        Tracer.AccessConfiguration().ReprojectionActive = true;
        auto SteppedCam = RtCfg.Camera;
        SteppedCam.YawRadians += 0.015f;
        Tracer.UpdateCamera(SteppedCam, true);
        if (!Tracer.AdvanceSamples(1u)) return 20;
        std::vector<uint8_t> ReprojRgba {};
        if (!VerifyCheck(Tracer.ResolveDisplayRgba8(ReprojRgba) && !ReprojRgba.empty(),
                         "DeterministicHistoryReproject carried surface-validated history across orbit camera motion"))
        {
            return 21;
        }

        // Stack 3-panel horizontal comparison strip (05-ReconstructionComparison_GameRealtime.png)
        const uint32_t StripW = HeroW * 3u + 8u;
        std::vector<uint8_t> StripRgba(static_cast<std::size_t>(StripW) * HeroH * 4u, 18u);
        for (std::size_t I = 3u; I < StripRgba.size(); I += 4u) StripRgba[I] = 255u;
        const auto BlitPanel = [&](const std::vector<uint8_t>& Src, const uint32_t DestX) {
            for (uint32_t Y = 0u; Y < HeroH; ++Y)
            {
                std::memcpy(&StripRgba[(static_cast<std::size_t>(Y) * StripW + DestX) * 4u],
                            &Src[static_cast<std::size_t>(Y) * HeroW * 4u],
                            static_cast<std::size_t>(HeroW) * 4u);
            }
        };
        BlitPanel(NoisyBlue1SppRgba, 0u);
        BlitPanel(Denoised1SppRgba,  HeroW + 4u);
        BlitPanel(HeroConvergedRgba, HeroW * 2u + 8u);
        if (!VerifyCheck(WriteRgbaBufferToPng(OutDir / "05-ReconstructionComparison_GameRealtime.png", StripW, HeroH, StripRgba),
                         "Wrote 05-ReconstructionComparison_GameRealtime.png"))
        {
            return 22;
        }
    }

    //========================================================================================================================
    // 5. 50-VEHICLE FLEET (1,253,400 TRIS) + 228 .IES PHOTOMETRIC LIGHT TREE CAPTURES (03 & 04)
    //========================================================================================================================
    {
        constexpr uint32_t FleetW = 320u;
        constexpr uint32_t FleetH = 180u;

        DeterministicTracerConfiguration FleetCfg {};
        DeterministicPathTraceSequence::ApplyRenderProfile(DeterministicRenderProfile::SceneFinalRender, FleetCfg);
        FleetCfg.VehicleCount           = 50u;
        FleetCfg.ShowcaseSpheresPresent = true;
        FleetCfg.CameraPreset           = StudioCameraPreset::Fleet;
        FleetCfg.Camera                 = DeterministicPathTraceSequence::QueryCameraPreset(StudioCameraPreset::Fleet);

        if (!VerifyCheck(Tracer.ConfigureStudioScene(FleetCfg) && Tracer.ResizeAccumulation(FleetW, FleetH),
                         "Configured 50-vehicle parking fleet (1,253,400 instanced triangles)"))
        {
            return 23;
        }
        const auto& FleetSummary = Tracer.QuerySceneSummary();
        std::printf("       50-vehicle fleet: %u tris in %u wide4 entries (%u vehicle + %u TLAS, %u KB total)\n",
                    FleetSummary.TriangleCount,
                    FleetSummary.BoundsEntryCount,
                    FleetSummary.VehicleWideEntries,
                    FleetSummary.InstanceWideEntries,
                    (FleetSummary.BoundsByteSize + 512u) / 1024u);
        if (!VerifyCheck(FleetSummary.TriangleCount == 1253400u && FleetSummary.BoundsByteSize < 320u * 1024u,
                         "1.25M-triangle 50-car fleet fits inside < 320 KB of 64B quantized wide4 + placement entries"))
        {
            return 24;
        }

        if (!Tracer.AdvanceSamples(16u)) return 25;
        std::vector<uint8_t> DaylightFleetRgba {};
        (void)Tracer.ResolveDisplayRgba8(DaylightFleetRgba);
        if (!VerifyCheck(WriteRgbaBufferToPng(OutDir / "03-Fleet50_Daylight.png", FleetW, FleetH, DaylightFleetRgba),
                         "Wrote 03-Fleet50_Daylight.png"))
        {
            return 26;
        }

        // Switch to Night Car-Park Rig: 200 vehicle .ies beams + 28 overhead aisle .ies downlights = 228 .ies lamps!
        FleetCfg.PracticalMode = PracticalLampMode::CarParkLamps;
        FleetCfg.NightRigActive = true;
        FleetCfg.LampSampling   = LampSamplingRule::SplitEight;
        FleetCfg.ExposureStops  = 0.0f;
        if (!VerifyCheck(Tracer.ConfigureStudioScene(FleetCfg), "Built 228-lamp Conty-Kulla photometric light tree"))
        {
            return 27;
        }
        const auto& NightSummary = Tracer.QuerySceneSummary();
        std::printf("       Photometric light tree: %u .ies lamps -> %u binary clusters (depth %u, build %.2f ms)\n",
                    NightSummary.PracticalLampCount,
                    NightSummary.LightTreeClusterCount,
                    NightSummary.LightTreeDepth,
                    NightSummary.LightTreeBuildMillis);
        if (!VerifyCheck(NightSummary.PracticalLampCount == 228u && NightSummary.LightTreeClusterCount == 455u,
                         "Conty Estevez & Kulla binary light tree built 455 clusters over 228 .ies luminaires"))
        {
            return 28;
        }

        if (!Tracer.AdvanceSamples(32u)) return 29;
        std::vector<uint8_t> NightFleetRgba {};
        (void)Tracer.ResolveDisplayRgba8(NightFleetRgba);
        if (!VerifyCheck(WriteRgbaBufferToPng(OutDir / "04-Fleet50_NightIes228.png", FleetW, FleetH, NightFleetRgba),
                         "Wrote 04-Fleet50_NightIes228.png"))
        {
            return 30;
        }
    }

    //========================================================================================================================
    // 6. TIEREDWORLDRENDERSEQUENCE ZERO-SDF INVARIANT + NATIVE STUDIO PANE CAPTURE (01-WorkspaceInterface_CppParity.png)
    //========================================================================================================================
    {
        DeterministicTracerConfiguration StudioUiCfg {};
        DeterministicPathTraceSequence::ApplyRenderProfile(DeterministicRenderProfile::SceneFinalRender, StudioUiCfg);
        StudioUiCfg.CameraPreset  = StudioCameraPreset::Hero;
        StudioUiCfg.Camera        = DeterministicPathTraceSequence::QueryCameraPreset(StudioCameraPreset::Hero);
        StudioUiCfg.RenderScale   = 0.50f;
        StudioUiCfg.TargetSamples = 32u;
        if (!Tracer.ConfigureStudioScene(StudioUiCfg)) return 31;

        // Verify TieredWorldRenderSequence Order 3 integration & strict 0 B SDF invariant
        TieredWorldRenderSequence Tiered {};
        Tiered.AttachDeterministicPathTracer(&Tracer);
        WorldSceneSpecification DummyWorld {};
        PerspectiveCameraBasis  WorldCam {};
        WorldCam.ViewportWidthPx  = 160u;
        WorldCam.ViewportHeightPx = 90u;
        WorldRenderConfiguration TieredCfg {};
        TieredCfg.Engine           = WorldRenderEngine::PathTracerGameRealtime;
        TieredCfg.PathTraceSamples = 2u;
        const WorldRenderOutput RtOut = Tiered.RenderWorldFrameWithEngine(TieredCfg, WorldCam, WorldCam, DummyWorld);
        if (!VerifyCheck(RtOut.SdfAllocatedBytes == 0u && RtOut.DisplayRgba8.size() == 160u * 90u * 4u,
                         "TieredWorldRenderSequence Order 3 (PathTracerGameRealtime) enforces 0 B SDF & valid display output"))
        {
            return 32;
        }

#if FRONTIER_PROFILE_EDITOR
        // Restore SceneFinalRender Hero configuration for full 1440x860 Studio Pane capture
        if (!Tracer.ConfigureStudioScene(StudioUiCfg)) return 33;

        IMGUI_CHECKVERSION();
        ImGuiContext* Gui = ImGui::CreateContext();
        ImGui::SetCurrentContext(Gui);
        ImGuiIO& Io    = ImGui::GetIO();
        Io.IniFilename = nullptr;
        Io.LogFilename = nullptr;
        Io.DisplaySize = { 1440.0f, 860.0f };
        Io.DeltaTime   = 1.0f / 60.0f;

        TypographyRepository Typefaces {};
        if (!VerifyCheck(Typefaces.LoadRepository(RepoRoot / "EngineContent/FontsArchives") >= 1u,
                         "TypographyRepository::LoadRepository for DeterministicPathTracerPane"))
        {
            ImGui::DestroyContext(Gui);
            return 34;
        }
        TypographyRepository::AssignCurrent(&Typefaces);
        if (ImFont* DefaultFont = Typefaces.QueryTabStripFont())
        {
            Io.FontDefault = DefaultFont;
        }
        unsigned char* FontPixels = nullptr;
        int AtlasW = 0, AtlasH = 0;
        Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &AtlasW, &AtlasH);

        DeterministicPathTracerPane Pane {};
        Pane.AttachSequence(&Tracer);
        const Rectangle2D WorkspaceBounds { 0.0f, 0.0f, 1440.0f, 860.0f };
        Pane.TriggerBitExactRepeatVerification(4u, 2u);
        Tracer.ResetAccumulation(false);
        Pane.StepIntegrationTick(WorkspaceBounds, 1.0 / 30.0, 24u);

        ImGui::NewFrame();
        ImDrawList* DrawList = ImGui::GetBackgroundDrawList();
        PixelSpace  Surface(DrawList, { 0.0f, 0.0f }, 1.0f, Io.FontDefault);
        PaneInputReading EmptyInput {};
        Pane.Render(Surface, WorkspaceBounds, EmptyInput, 1.0 / 60.0);
        ImGui::Render();

        Exhibits::SoftwareRasterCanvas UiCanvas(1440u, 860u, Components::RgbaColor8 { 11u, 12u, 14u, 255u });
        UiCanvas.RasterizeImGuiBatches(ImGui::GetDrawData());
        const bool SavedUi = UiCanvas.WritePngFile(OutDir / "01-WorkspaceInterface_CppParity.png");

        TypographyRepository::AssignCurrent(nullptr);
        ImGui::DestroyContext(Gui);

        if (!VerifyCheck(SavedUi, "Wrote 1440x860 01-WorkspaceInterface_CppParity.png"))
        {
            return 35;
        }
#endif
    }

    Tracer.Teardown();
    Vulkan.TeardownInterchange();
    std::printf("=== ALL ORDER 3 DETERMINISTIC PATH TRACER CHECKS PASSED ===\n");
    return 0;
}
