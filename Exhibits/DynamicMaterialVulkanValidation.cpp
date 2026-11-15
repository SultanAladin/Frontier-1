/*==========================================================================================================================================
                                                 DYNAMICMATERIALVULKANVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Headless Vulkan 1.3 SwiftShader validation executing DynamicMaterialResolve.comp.spv across 6 modular dynamic materials + live impact test

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/MaterialSpace/Dynamic/DynamicMaterialRepository.h"
#include "Engine/Workspaces/Material/MaterialPreviewRenderer.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <cmath>
#include <cstdio>
#include <filesystem>

namespace
{

using namespace Frontier;
using namespace Frontier::GeometricRaster;
using namespace Frontier::MaterialSpace::Dynamic;
using namespace Frontier::Workspaces;
using Frontier::Components::RgbaColor8;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;

bool VerifyCheck(bool Condition, const char* Label, uint32_t& Passed, uint32_t& Failed) noexcept
{
    if (Condition)
    {
        ++Passed;
        std::printf("  [PASS] %s\n", Label);
        return true;
    }
    ++Failed;
    std::fprintf(stderr, "  [FAIL] %s\n", Label);
    return false;
}

} // namespace

int main()
{
    uint32_t Passed = 0u;
    uint32_t Failed = 0u;
    std::printf("=== DynamicMaterialVulkanValidation ===\n");

    const std::filesystem::path RepoRoot  = ResolveRepositoryRootPath();
    const std::filesystem::path ShaderDir = RepoRoot / "build/CompiledShaders";
    const std::filesystem::path SpvPath   = ShaderDir / "DynamicMaterialResolve.comp.spv";

    VerifyCheck(std::filesystem::exists(SpvPath), "Compiled DynamicMaterialResolve.comp.spv exists", Passed, Failed);

    VulkanInterchange Device {};
    std::string Diag {};
    const bool DeviceOk = Device.InitializeInterchange(RepoRoot, &Diag);
    VerifyCheck(DeviceOk && Device.Ready(), "Vulkan 1.3 logical device initialized", Passed, Failed);
    if (!DeviceOk)
    {
        std::fprintf(stderr, "  Vulkan diag: %s\n", Diag.c_str());
        return 1;
    }

    MaterialPreviewRenderer GpuRenderer {};
    const bool RendererOk = GpuRenderer.InitializeVulkanPreview(Device, ShaderDir);
    VerifyCheck(RendererOk && GpuRenderer.QueryVulkanReady(), "MaterialPreviewRenderer bound DynamicMaterialResolve.comp.spv", Passed, Failed);
    if (!RendererOk)
    {
        return 1;
    }

    const auto Presets = BuildAllDynamicMaterialPresets();
    PreviewCameraSetup Cam {};
    Cam.OrbitYawRadians   = 0.32f;
    Cam.OrbitPitchRadians = 0.20f;
    Cam.OrbitDistanceM    = 2.65f;

    constexpr uint32_t TileS = 320u;
    constexpr uint32_t Pad   = 12u;
    SoftwareRasterCanvas GpuSheet(TileS * 3u + Pad * 4u, TileS * 2u + Pad * 3u, RgbaColor8 { 11u, 11u, 12u, 255u });

    for (uint32_t I = 0u; I < 6u; ++I)
    {
        auto R = Presets[I];
        R.Parameters.ClockSeconds = 1.45f;
        if (I == 0u) R.Parameters.TransitionProgress = 0.42f;
        if (I == 3u) R.Parameters.TransitionProgress = 0.50f;
        if (I == 4u) R.Parameters.TransitionProgress = 0.38f;

        PreviewRenderResult Out {};
        const bool Ok = GpuRenderer.RenderPreviewVulkanCompute(
            R.Parameters, PreviewSpecimenGeometry::ShaderballPedestal, PreviewRenderMode::DirectStudioOpenPbr, Cam, TileS, TileS, Out);
        VerifyCheck(Ok && Out.UsedVulkanCompute && Out.DisplayRgba8.size() == TileS * TileS * 4u,
            R.DisplayTitle.c_str(), Passed, Failed);
        if (Ok)
        {
            GpuSheet.BlitRgbaRegion(Pad + (I % 3u) * (TileS + Pad), Pad + (I / 3u) * (TileS + Pad), TileS, TileS, Out.DisplayRgba8.data());
        }
    }

    // Verify live zero-recompile slider & impact pulse delta on Vulkan GPU!
    auto ShieldA = ConstructDynamicMaterialPreset(DynamicPresetIdentity::HexagonalEnergyBarrier);
    auto ShieldB = ShieldA;
    TriggerImpactPulse(ShieldB.Parameters, { 0.0f, 0.1f, 0.99f });
    PreviewRenderResult ImgA {}, ImgB {};
    const bool BothRendered =
        GpuRenderer.RenderPreviewVulkanCompute(ShieldA.Parameters, PreviewSpecimenGeometry::ShaderballPedestal, PreviewRenderMode::DirectStudioOpenPbr, Cam, 128u, 128u, ImgA) &&
        GpuRenderer.RenderPreviewVulkanCompute(ShieldB.Parameters, PreviewSpecimenGeometry::ShaderballPedestal, PreviewRenderMode::DirectStudioOpenPbr, Cam, 128u, 128u, ImgB);
    uint64_t DiffSum = 0ull;
    if (BothRendered)
    {
        for (std::size_t K = 0u; K < ImgA.DisplayRgba8.size(); ++K)
        {
            DiffSum += static_cast<uint64_t>(std::abs(static_cast<int>(ImgA.DisplayRgba8[K]) - static_cast<int>(ImgB.DisplayRgba8[K])));
        }
    }
    VerifyCheck(BothRendered && DiffSum > 25000ull, "Live 3D impact pulse produces immediate GPU ripple change (zero shader recompile)", Passed, Failed);

    const std::filesystem::path OutDir = RepoRoot / "Exhibits/Images/Materials";
    std::filesystem::create_directories(OutDir);
    VerifyCheck(GpuSheet.WritePngFile(OutDir / "VulkanDirectPbr_Hero6_Proof.png"), "Write VulkanDirectPbr_Hero6_Proof.png", Passed, Failed);

    GpuRenderer.TeardownVulkanPreview();
    Device.TeardownInterchange();
    std::printf("=== Summary: %u PASSED, %u FAILED ===\n", Passed, Failed);
    return (Failed == 0u) ? 0 : 1;
}
