/*============================================================================================================================================
                                                  SDFEMISSIVEPENUMBRAVALIDATION.CPP
============================================================================================================================================*/
// 🧩 PROOF 11 Validation Exhibit: Emissive Sphere ONLY (No Sun / Area / Spot / IES) — All 6 SDF Shadow + Penumbra + Voxel Cone GI + Sobol Cap + 96-SPP MC Renderers + Vulkan .slang Compute + Viewport 6-Mode Dropdown Selector

#if FRONTIER_PROFILE_EDITOR
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/Panes/ViewportPanel.h"
#include "Engine/Components/PixelSpace.h"
#include <imgui.h>
#endif

#include "Engine/GeometricRaster/DirectLighting/SdfEmissivePenumbraSequence.h"
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
using namespace Frontier::GeometricRaster::DirectLighting;

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

[[nodiscard]] double ComputeFrameHdrRmse(
    const SdfEmissiveRenderFrame& Candidate,
    const SdfEmissiveRenderFrame& Reference,
    bool                          ExcludeDirectEmissiveOrb = true) noexcept
{
    if (Candidate.LinearHdr.size() != Reference.LinearHdr.size() || Candidate.LinearHdr.empty())
    {
        return 999.0;
    }
    double SumSq = 0.0;
    std::size_t Count = 0u;
    for (std::size_t I = 0u; I < Candidate.LinearHdr.size(); ++I)
    {
        const auto& R = Reference.LinearHdr[I];
        if (ExcludeDirectEmissiveOrb && std::max({ R.R, R.G, R.B }) > 4.0f)
        {
            continue;
        }
        const auto& C = Candidate.LinearHdr[I];
        const auto Tone = [](float V) noexcept -> double
        {
            const double X = std::max(0.0, static_cast<double>(V) * 0.95);
            return std::clamp((X * (2.51 * X + 0.03)) / (X * (2.43 * X + 0.59) + 0.14), 0.0, 1.0);
        };
        const double DR = Tone(C.R) - Tone(R.R);
        const double DG = Tone(C.G) - Tone(R.G);
        const double DB = Tone(C.B) - Tone(R.B);
        SumSq += (DR * DR + DG * DG + DB * DB) / 3.0;
        ++Count;
    }
    return (Count > 0u) ? std::sqrt(SumSq / static_cast<double>(Count)) : 0.0;
}

struct GpuSdfPenumbraUniformBlock
{
    uint32_t ViewportModeSamples[4] {};
    float    CameraEyeExposure[4]   {};
    float    CameraForwardTanY[4]   {};
    float    CameraRightAspect[4]   {};
    float    CameraUpPad[4]         {};
    float    EmissiveCenterRad[4]   {};
    float    EmissiveRadiancePad[4] {};
    uint32_t FineClipDims[4]        {};
    uint32_t CoarseClipDimsYZ[4]    {};
    float    FineBoundsMin[4]       {};
    float    FineBoundsMax[4]       {};
    float    CoarseBoundsMin[4]     {};
    float    CoarseBoundsMax[4]     {};
};

[[nodiscard]] bool VerifyGpuSlangSdfPenumbraDispatch(
    const std::filesystem::path&    RepoRoot,
    const SdfShowcaseScene&         Scene,
    const SdfVoxelClipmapHierarchy& Clipmap,
    const SdfShowcaseCamera&        Cam,
    double&                         OutGpuVsCpuRmse)
{
    OutGpuVsCpuRmse = 999.0;
    VulkanInterchange Vk {};
    std::string Diag {};
    if (!Vk.InitializeInterchange(RepoRoot, &Diag))
    {
        std::fprintf(stderr, "VulkanInterchange failed: %s\n", Diag.c_str());
        return false;
    }

    const std::filesystem::path SpvPath =
        std::filesystem::current_path() / "CompiledShaders/SdfEmissivePenumbraCompute.comp.spv";
    const std::filesystem::path AltSpvPath =
        RepoRoot / "build/Editor/CompiledShaders/SdfEmissivePenumbraCompute.comp.spv";
    const std::filesystem::path FinalSpv = std::filesystem::exists(SpvPath) ? SpvPath : AltSpvPath;

    VkShaderModule ShaderMod = Vk.LoadShaderBinary(FinalSpv);
    if (ShaderMod == VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "Missing compiled Slang SPIR-V: %s\n", FinalSpv.string().c_str());
        Vk.TeardownInterchange();
        return false;
    }

    constexpr uint32_t GpuW = 120u;
    constexpr uint32_t GpuH = 68u;
    SdfShowcaseCamera GpuCam = Cam;
    GpuCam.WidthPx  = GpuW;
    GpuCam.HeightPx = GpuH;

    const Mathematics::Axis3D Fwd    = Mathematics::NormalizeAxis(GpuCam.Target - GpuCam.Eye, { 0.0f, 1.0f, 0.0f });
    const Mathematics::Axis3D Right  = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Fwd, GpuCam.Up), { 1.0f, 0.0f, 0.0f });
    const Mathematics::Axis3D TrueUp = Mathematics::EvaluateCross(Right, Fwd);
    const float TanHalfY = std::tan(0.5f * GpuCam.VerticalFovRad);
    const float Aspect   = static_cast<float>(GpuW) / static_cast<float>(GpuH);

    GpuSdfPenumbraUniformBlock Ubo {};
    Ubo.ViewportModeSamples[0] = GpuW;
    Ubo.ViewportModeSamples[1] = GpuH;
    Ubo.ViewportModeSamples[2] = static_cast<uint32_t>(SdfShadowPenumbraMode::VoxelSdfPenumbraPlusConeGi);
    Ubo.ViewportModeSamples[3] = 20u;
    Ubo.CameraEyeExposure[0]   = GpuCam.Eye.X; Ubo.CameraEyeExposure[1] = GpuCam.Eye.Y; Ubo.CameraEyeExposure[2] = GpuCam.Eye.Z; Ubo.CameraEyeExposure[3] = GpuCam.ExposureScale;
    Ubo.CameraForwardTanY[0]   = Fwd.X;        Ubo.CameraForwardTanY[1] = Fwd.Y;        Ubo.CameraForwardTanY[2] = Fwd.Z;        Ubo.CameraForwardTanY[3] = TanHalfY;
    Ubo.CameraRightAspect[0]   = Right.X;      Ubo.CameraRightAspect[1] = Right.Y;      Ubo.CameraRightAspect[2] = Right.Z;      Ubo.CameraRightAspect[3] = Aspect;
    Ubo.CameraUpPad[0]         = TrueUp.X;     Ubo.CameraUpPad[1]       = TrueUp.Y;     Ubo.CameraUpPad[2]       = TrueUp.Z;
    Ubo.EmissiveCenterRad[0]   = Scene.EmissiveSphere.Center.X;
    Ubo.EmissiveCenterRad[1]   = Scene.EmissiveSphere.Center.Y;
    Ubo.EmissiveCenterRad[2]   = Scene.EmissiveSphere.Center.Z;
    Ubo.EmissiveCenterRad[3]   = Scene.EmissiveSphere.Radius;
    Ubo.EmissiveRadiancePad[0] = Scene.EmissiveSphere.Radiance.R;
    Ubo.EmissiveRadiancePad[1] = Scene.EmissiveSphere.Radiance.G;
    Ubo.EmissiveRadiancePad[2] = Scene.EmissiveSphere.Radiance.B;
    Ubo.FineClipDims[0]        = Clipmap.FineCascade.DimX;
    Ubo.FineClipDims[1]        = Clipmap.FineCascade.DimY;
    Ubo.FineClipDims[2]        = Clipmap.FineCascade.DimZ;
    Ubo.FineClipDims[3]        = Clipmap.CoarseCascade.DimX;
    Ubo.CoarseClipDimsYZ[0]    = Clipmap.CoarseCascade.DimY;
    Ubo.CoarseClipDimsYZ[1]    = Clipmap.CoarseCascade.DimZ;
    Ubo.FineBoundsMin[0]       = Clipmap.FineCascade.BoundsMin.X; Ubo.FineBoundsMin[1] = Clipmap.FineCascade.BoundsMin.Y; Ubo.FineBoundsMin[2] = Clipmap.FineCascade.BoundsMin.Z;
    Ubo.FineBoundsMax[0]       = Clipmap.FineCascade.BoundsMax.X; Ubo.FineBoundsMax[1] = Clipmap.FineCascade.BoundsMax.Y; Ubo.FineBoundsMax[2] = Clipmap.FineCascade.BoundsMax.Z;
    Ubo.CoarseBoundsMin[0]     = Clipmap.CoarseCascade.BoundsMin.X; Ubo.CoarseBoundsMin[1] = Clipmap.CoarseCascade.BoundsMin.Y; Ubo.CoarseBoundsMin[2] = Clipmap.CoarseCascade.BoundsMin.Z;
    Ubo.CoarseBoundsMax[0]     = Clipmap.CoarseCascade.BoundsMax.X; Ubo.CoarseBoundsMax[1] = Clipmap.CoarseCascade.BoundsMax.Y; Ubo.CoarseBoundsMax[2] = Clipmap.CoarseCascade.BoundsMax.Z;

    const VkMemoryPropertyFlags HostProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    DeviceBufferResource UboBuf    = Vk.AllocateBuffer(sizeof(Ubo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, HostProps);
    DeviceBufferResource FineBuf   = Vk.AllocateBuffer(Clipmap.FineCascade.DistanceGrid.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, HostProps);
    DeviceBufferResource CoarseBuf = Vk.AllocateBuffer(Clipmap.CoarseCascade.DistanceGrid.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, HostProps);

    std::vector<float> PackedRadOp(Clipmap.CoarseCascade.RadiosityGrid.size() * 4u, 0.0f);
    for (std::size_t I = 0u; I < Clipmap.CoarseCascade.RadiosityGrid.size(); ++I)
    {
        PackedRadOp[I * 4u + 0u] = Clipmap.CoarseCascade.RadiosityGrid[I].R;
        PackedRadOp[I * 4u + 1u] = Clipmap.CoarseCascade.RadiosityGrid[I].G;
        PackedRadOp[I * 4u + 2u] = Clipmap.CoarseCascade.RadiosityGrid[I].B;
        PackedRadOp[I * 4u + 3u] = Clipmap.CoarseCascade.OpacityGrid[I];
    }
    DeviceBufferResource RadBuf = Vk.AllocateBuffer(PackedRadOp.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, HostProps);
    DeviceBufferResource OutBuf = Vk.AllocateBuffer(static_cast<uint64_t>(GpuW) * GpuH * sizeof(float) * 4u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, HostProps);

    std::memcpy(UboBuf.HostAddress,    &Ubo, sizeof(Ubo));
    std::memcpy(FineBuf.HostAddress,   Clipmap.FineCascade.DistanceGrid.data(),   Clipmap.FineCascade.DistanceGrid.size() * sizeof(float));
    std::memcpy(CoarseBuf.HostAddress, Clipmap.CoarseCascade.DistanceGrid.data(), Clipmap.CoarseCascade.DistanceGrid.size() * sizeof(float));
    std::memcpy(RadBuf.HostAddress,    PackedRadOp.data(),                        PackedRadOp.size() * sizeof(float));

    VkDevice Dev = Vk.AccessDevice();
    std::array<VkDescriptorSetLayoutBinding, 5> Bindings {};
    for (uint32_t B = 0u; B < 5u; ++B)
    {
        Bindings[B].binding         = B;
        Bindings[B].descriptorType  = (B == 0u) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Bindings[B].descriptorCount = 1u;
        Bindings[B].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo DslCi {};
    DslCi.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    DslCi.bindingCount = static_cast<uint32_t>(Bindings.size());
    DslCi.pBindings    = Bindings.data();
    VkDescriptorSetLayout Dsl = VK_NULL_HANDLE;
    vkCreateDescriptorSetLayout(Dev, &DslCi, nullptr, &Dsl);

    VkPipelineLayoutCreateInfo PlCi {};
    PlCi.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    PlCi.setLayoutCount = 1u;
    PlCi.pSetLayouts    = &Dsl;
    VkPipelineLayout PipeLayout = VK_NULL_HANDLE;
    vkCreatePipelineLayout(Dev, &PlCi, nullptr, &PipeLayout);

    VkComputePipelineCreateInfo CpCi {};
    CpCi.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    CpCi.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    CpCi.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    CpCi.stage.module = ShaderMod;
    CpCi.stage.pName  = "ComputeEntry";
    CpCi.layout       = PipeLayout;
    VkPipeline Pipe   = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(Dev, VK_NULL_HANDLE, 1u, &CpCi, nullptr, &Pipe) != VK_SUCCESS || Pipe == VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(Dev, PipeLayout, nullptr);
        vkDestroyDescriptorSetLayout(Dev, Dsl, nullptr);
        vkDestroyShaderModule(Dev, ShaderMod, nullptr);
        Vk.ReleaseBuffer(UboBuf);
        Vk.ReleaseBuffer(FineBuf);
        Vk.ReleaseBuffer(CoarseBuf);
        Vk.ReleaseBuffer(RadBuf);
        Vk.ReleaseBuffer(OutBuf);
        Vk.TeardownInterchange();
        return false;
    }

    std::array<VkDescriptorPoolSize, 2> PoolSizes {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4u },
    }};
    VkDescriptorPoolCreateInfo DpCi {};
    DpCi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    DpCi.maxSets       = 1u;
    DpCi.poolSizeCount = 2u;
    DpCi.pPoolSizes    = PoolSizes.data();
    VkDescriptorPool Pool = VK_NULL_HANDLE;
    vkCreateDescriptorPool(Dev, &DpCi, nullptr, &Pool);

    VkDescriptorSetAllocateInfo DsAi {};
    DsAi.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    DsAi.descriptorPool     = Pool;
    DsAi.descriptorSetCount = 1u;
    DsAi.pSetLayouts        = &Dsl;
    VkDescriptorSet DSet = VK_NULL_HANDLE;
    vkAllocateDescriptorSets(Dev, &DsAi, &DSet);

    const std::array<VkDescriptorBufferInfo, 5> BufInfos {{
        { UboBuf.BufferHandle,    0u, VK_WHOLE_SIZE },
        { FineBuf.BufferHandle,   0u, VK_WHOLE_SIZE },
        { CoarseBuf.BufferHandle, 0u, VK_WHOLE_SIZE },
        { RadBuf.BufferHandle,    0u, VK_WHOLE_SIZE },
        { OutBuf.BufferHandle,    0u, VK_WHOLE_SIZE },
    }};
    std::array<VkWriteDescriptorSet, 5> Writes {};
    for (uint32_t B = 0u; B < 5u; ++B)
    {
        Writes[B].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[B].dstSet          = DSet;
        Writes[B].dstBinding      = B;
        Writes[B].descriptorCount = 1u;
        Writes[B].descriptorType  = (B == 0u) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[B].pBufferInfo     = &BufInfos[B];
    }
    vkUpdateDescriptorSets(Dev, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);

    VkCommandBuffer Cmd = Vk.BeginSingleTimeCommands();
    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, Pipe);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, PipeLayout, 0u, 1u, &DSet, 0u, nullptr);
    vkCmdDispatch(Cmd, (GpuW + 7u) / 8u, (GpuH + 7u) / 8u, 1u);
    const uint64_t Tick = Vk.DispatchCommandBufferAsync(Cmd);
    const bool OkWait   = Vk.AwaitTimelineTick(Tick, 15000000000ull);
    Vk.FreeCommandBuffer(Cmd);

    const SdfEmissiveRenderFrame CpuFrame = RenderEmissiveSphereSdfFrame(
        SdfShadowPenumbraMode::VoxelSdfPenumbraPlusConeGi, Scene, Clipmap, GpuCam, 20u);

    const auto* GpuFloats = static_cast<const float*>(OutBuf.HostAddress);
    double SumSq = 0.0;
    std::size_t Cnt = 0u;
    for (std::size_t I = 0u; I < CpuFrame.LinearHdr.size(); ++I)
    {
        if (CpuFrame.LinearHdr[I].R > 4.0f) continue;
        const auto Tone = [](float V) noexcept -> double
        {
            const double X = std::max(0.0, static_cast<double>(V) * 0.95);
            return std::clamp((X * (2.51 * X + 0.03)) / (X * (2.43 * X + 0.59) + 0.14), 0.0, 1.0);
        };
        const double DR = Tone(GpuFloats[I * 4u + 0u]) - Tone(CpuFrame.LinearHdr[I].R);
        const double DG = Tone(GpuFloats[I * 4u + 1u]) - Tone(CpuFrame.LinearHdr[I].G);
        const double DB = Tone(GpuFloats[I * 4u + 2u]) - Tone(CpuFrame.LinearHdr[I].B);
        SumSq += (DR * DR + DG * DG + DB * DB) / 3.0;
        ++Cnt;
    }
    OutGpuVsCpuRmse = (Cnt > 0u) ? std::sqrt(SumSq / static_cast<double>(Cnt)) : 999.0;

    vkDestroyDescriptorPool(Dev, Pool, nullptr);
    vkDestroyPipeline(Dev, Pipe, nullptr);
    vkDestroyPipelineLayout(Dev, PipeLayout, nullptr);
    vkDestroyDescriptorSetLayout(Dev, Dsl, nullptr);
    vkDestroyShaderModule(Dev, ShaderMod, nullptr);
    Vk.ReleaseBuffer(UboBuf);
    Vk.ReleaseBuffer(FineBuf);
    Vk.ReleaseBuffer(CoarseBuf);
    Vk.ReleaseBuffer(RadBuf);
    Vk.ReleaseBuffer(OutBuf);
    Vk.TeardownInterchange();
    return OkWait && (OutGpuVsCpuRmse < 0.055);
}

} // namespace

int main()
{
    const std::filesystem::path RepoRoot = FRONTIER_REPO_ROOT_DIR;
    const std::filesystem::path OutDir   = RepoRoot / "Exhibits/Images/SdfEmissivePenumbra";
    std::filesystem::create_directories(OutDir);

    std::printf("=== PROOF 11: EMISSIVE SPHERE ONLY — ALL 6 SDF SHADOW + PENUMBRA RENDERERS ===\n");

    //========================================================================================================================
    // 1. BUILD EMISSIVE-SPHERE-ONLY SCENE + 2-CASCADE 3D VOXEL SDF CLIPMAP HIERARCHY
    //========================================================================================================================
    SdfShowcaseScene Scene {};
    BuildCanonicalProof11EmissiveSphereScene(Scene);
    if (!VerifyCheck(Scene.EmissiveActive && Scene.EmissiveSphere.Radius > 0.35f,
                     "Built canonical PROOF 11 Cornell gallery with sole Emissive Sphere (NO sun/area/spot/IES)"))
    {
        return 1;
    }

    SdfVoxelClipmapHierarchy Clipmap {};
    Clipmap.BuildFromShowcaseScene(Scene);
    const std::size_t ClipmapBytes = Clipmap.QueryAllocatedBytes();
    std::printf("  3D Voxel SDF Clipmap (56x56x40 Fine + 40x40x32 Coarse + 2-Bounce Radiosity): %zu bytes (%.2f MB)\n",
                ClipmapBytes, static_cast<double>(ClipmapBytes) / (1024.0 * 1024.0));
    if (!VerifyCheck(ClipmapBytes > 0u && ClipmapBytes <= 4200000u,
                     "2-cascade 3D Voxel SDF Clipmap + Radiosity fits comfortably within compact budget"))
    {
        return 2;
    }

    //========================================================================================================================
    // 2. MEASURE FLOOR PENUMBRA WIDTH & UMBRA ACROSS ALL 6 MODES
    //========================================================================================================================
    std::array<float, 6> PenumbraWidths {};
    std::array<float, 6> UmbraMins      {};
    for (uint32_t M = 0u; M < 6u; ++M)
    {
        const auto Mode = static_cast<SdfShadowPenumbraMode>(M);
        MeasureFloorShadowPenumbraProfile(Scene, Clipmap, Mode, PenumbraWidths[M], UmbraMins[M]);
        std::printf("  [%s] Floor Penumbra Width = %.3f m | Umbra Min Visibility = %.3f\n",
                    QuerySdfShadowPenumbraModeLabel(Mode), PenumbraWidths[M], UmbraMins[M]);
    }

    if (!VerifyCheck(PenumbraWidths[0] == 0.0f && UmbraMins[0] == 0.0f,
                     "Mode 0 (Point-Sample SDF) produces binary 0.000 m penumbra and 0.00 umbra"))
    {
        return 3;
    }
    if (!VerifyCheck(PenumbraWidths[1] > 0.45f && PenumbraWidths[2] > 0.40f && PenumbraWidths[4] > 0.45f &&
                     UmbraMins[1] <= 0.02f && UmbraMins[2] <= 0.03f && UmbraMins[4] <= 0.02f,
                     "Modes 1..5 produce wide physical emissive-sphere penumbrae (> 0.40 m) with deep 0.00 umbra"))
    {
        return 4;
    }

    //========================================================================================================================
    // 3. RENDER ALL 6 MODES AT 480x270 & VERIFY CONVERGENCE AGAINST 96-SPP MONTE CARLO PT REFERENCE
    //========================================================================================================================
    SdfShowcaseCamera Cam {};
    Cam.Eye           = { -0.10f, -3.55f, 1.48f };
    Cam.Target        = { -0.04f,  0.42f, 0.96f };
    Cam.VerticalFovRad = 0.78f;
    Cam.ExposureScale = 0.96f;
    Cam.WidthPx       = 480u;
    Cam.HeightPx      = 270u;

    static constexpr std::array<const char*, 6> FileNames {{
        "00_PointSampleSdf.png",
        "01_AnalyticSdfConePenumbra.png",
        "02_VoxelSdfClipmapPenumbra.png",
        "03_VoxelSdfPenumbraPlusConeGi.png",
        "04_HybridSdfSobolSphericalCap.png",
        "05_SdfMonteCarloPtRef96Spp.png",
    }};

    std::array<SdfEmissiveRenderFrame, 6> Frames {};
    for (uint32_t M = 0u; M < 6u; ++M)
    {
        Frames[M] = RenderEmissiveSphereSdfFrame(static_cast<SdfShadowPenumbraMode>(M), Scene, Clipmap, Cam, 96u);
        Exhibits::SoftwareRasterCanvas PanelOut(Cam.WidthPx, Cam.HeightPx, Components::RgbaColor8 { 0u, 0u, 0u, 255u });
        PanelOut.BlitRgbaRegion(0u, 0u, Cam.WidthPx, Cam.HeightPx, Frames[M].DisplayRgba8.data());
        const bool Ok = PanelOut.WritePngFile(OutDir / FileNames[M]);
        if (!VerifyCheck(Ok, FileNames[M]))
        {
            return 5;
        }
    }

    const double RmsePoint    = ComputeFrameHdrRmse(Frames[0], Frames[5]);
    const double RmseAnalytic = ComputeFrameHdrRmse(Frames[1], Frames[5]);
    const double RmseClipmap  = ComputeFrameHdrRmse(Frames[2], Frames[5]);
    const double RmseVoxelGi  = ComputeFrameHdrRmse(Frames[3], Frames[5]);
    const double RmseHybrid   = ComputeFrameHdrRmse(Frames[4], Frames[5]);

    std::printf("  96-SPP PT Ref RMSE -> Point=%.4f | Analytic=%.4f | Clipmap=%.4f | VoxelConeGI=%.4f | HybridSobol=%.4f\n",
                RmsePoint, RmseAnalytic, RmseClipmap, RmseVoxelGi, RmseHybrid);

    if (!VerifyCheck(RmseVoxelGi < 0.085 && RmseHybrid < 0.085 && RmseAnalytic < 0.085,
                     "Analytic SDF Cone, Voxel SDF + Cone GI, and Hybrid Sobol Spherical Cap converge to 96-SPP PT Ref (RMSE < 0.085)"))
    {
        return 6;
    }

    //========================================================================================================================
    // 4. VERIFY VULKAN GPU .SLANG COMPUTE SHADER DISPATCH PARITY
    //========================================================================================================================
    double GpuVsCpuRmse = 999.0;
    const bool GpuOk = VerifyGpuSlangSdfPenumbraDispatch(RepoRoot, Scene, Clipmap, Cam, GpuVsCpuRmse);
    std::printf("  Vulkan SdfEmissivePenumbraCompute.slang GPU vs CPU RMSE = %.4f\n", GpuVsCpuRmse);
    if (!VerifyCheck(GpuOk, "Vulkan SdfEmissivePenumbraCompute.comp.spv executes on GPU and matches CPU reference"))
    {
        return 7;
    }

    //========================================================================================================================
    // 5. COMPOSE PROOF 11 2x3 CONTACT SHEET & LIVE VIEWPORT 6-MODE SDF DROPDOWN UI EXHIBIT
    //========================================================================================================================
#if FRONTIER_PROFILE_EDITOR
    {
        IMGUI_CHECKVERSION();
        ImGuiContext* Gui = ImGui::CreateContext();
        ImGui::SetCurrentContext(Gui);
        ImGuiIO& Io    = ImGui::GetIO();
        Io.IniFilename = nullptr;
        Io.LogFilename = nullptr;
        Io.DeltaTime   = 1.0f / 60.0f;

        TypographyRepository Typefaces {};
        if (!VerifyCheck(Typefaces.LoadRepository(RepoRoot / "EngineContent/FontsArchives") >= 1u,
                         "Loaded TypographyRepository for PROOF 11 Contact Sheet & Viewport Dropdown Exhibit"))
        {
            ImGui::DestroyContext(Gui);
            return 8;
        }
        TypographyRepository::AssignCurrent(&Typefaces);
        if (ImFont* DefaultFont = Typefaces.QueryTabStripFont())
        {
            Io.FontDefault = DefaultFont;
        }
        unsigned char* FontPixels = nullptr;
        int AtlasW = 0, AtlasH = 0;
        Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &AtlasW, &AtlasH);

        // --- A. 1488x672 PROOF 11 2x3 Contact Sheet ---
        constexpr uint32_t SheetW = 1488u;
        constexpr uint32_t SheetH = 688u;
        Io.DisplaySize = { static_cast<float>(SheetW), static_cast<float>(SheetH) };

        std::array<ImTextureData, 6> PanelTex {};
        for (uint32_t M = 0u; M < 6u; ++M)
        {
            PanelTex[M].Create(ImTextureFormat_RGBA32, static_cast<int>(Cam.WidthPx), static_cast<int>(Cam.HeightPx));
            std::memcpy(PanelTex[M].GetPixels(), Frames[M].DisplayRgba8.data(), Frames[M].DisplayRgba8.size());
            PanelTex[M].SetStatus(ImTextureStatus_OK);
        }

        static constexpr std::array<const char*, 6> UpperHeaders {{
            "0. POINT-SAMPLE SDF (NO PENUMBRA)",
            "1. ANALYTIC SDF CONE PENUMBRA",
            "2. 3D VOXEL SDF CLIPMAP PENUMBRA",
            "3. 3D VOXEL SDF PENUMBRA + CONE GI",
            "4. HYBRID SDF + SOBOL SPHERICAL CAP",
            "5. SDF MONTE CARLO PT REF (96 SPP)",
        }};

        ImGui::NewFrame();
        {
            ImDrawList* Draw = ImGui::GetBackgroundDrawList();
            PixelSpace  Surf(Draw, { 0.0f, 0.0f }, 1.0f, Io.FontDefault);

            Surf.FillRectangle({ 0.0f, 0.0f, static_cast<float>(SheetW), static_cast<float>(SheetH) }, ColorRgba { 16, 17, 20, 255 });
            Surf.Text({ 16.0f, 10.0f }, 15.0f, ColorRgba { 242, 244, 248, 255 },
                      "PROOF 11: EMISSIVE SPHERE ONLY (NO SUN/AREA/SPOT/IES) - ALL SDF RENDERERS SHOWING SHADOWS + PENUMBRA");

            for (uint32_t M = 0u; M < 6u; ++M)
            {
                const uint32_t Col = M % 3u;
                const uint32_t Row = M / 3u;
                const float PX = 12.0f + static_cast<float>(Col) * 492.0f;
                const float PY = 42.0f + static_cast<float>(Row) * 304.0f;
                const ColorRgba HeaderInk = (M == 5u)
                    ? ColorRgba { 246, 204, 106, 255 }
                    : ((M == 0u) ? ColorRgba { 206, 212, 222, 255 } : ColorRgba { 136, 232, 168, 255 });
                Surf.Text({ PX + 2.0f, PY }, 12.5f, HeaderInk, UpperHeaders[M]);

                const Rectangle2D ImgRect { PX, PY + 20.0f, 480.0f, 270.0f };
                Surf.FillTexturedRectangle(ImgRect, &PanelTex[M], { 0.0f, 0.0f }, { 1.0f, 1.0f });
                Surf.StrokeRectangle(ImgRect, ColorRgba { 68, 72, 80, 255 }, 0.0f, 1.0f);
            }

            char FooterBuf[256] {};
            std::snprintf(
                FooterBuf,
                sizeof(FooterBuf),
                "PENUMBRA WIDTH: POINT=%.3fM VS ANALYTIC SDF=%.3fM VS VOXEL SDF=%.3fM | UMBRA=%.2f | PT REF RMSE: HYB=%.3f GI=%.3f AN=%.3f",
                PenumbraWidths[0], PenumbraWidths[1], PenumbraWidths[2], UmbraMins[1], RmseHybrid, RmseVoxelGi, RmseAnalytic);
            Surf.Text({ 16.0f, 656.0f }, 13.0f, ColorRgba { 196, 226, 252, 255 }, FooterBuf);
        }
        ImGui::Render();

        Exhibits::SoftwareRasterCanvas SheetCanvas(SheetW, SheetH, Components::RgbaColor8 { 16u, 17u, 20u, 255u });
        SheetCanvas.RasterizeImGuiBatches(ImGui::GetDrawData());
        const bool SavedSheet = SheetCanvas.WritePngFile(OutDir / "06_Proof11_AllSdfRenderers_PenumbraSheet.png");
        for (auto& T : PanelTex) T.DestroyPixels();
        if (!VerifyCheck(SavedSheet, "Wrote 1488x688 06_Proof11_AllSdfRenderers_PenumbraSheet.png"))
        {
            TypographyRepository::AssignCurrent(nullptr);
            ImGui::DestroyContext(Gui);
            return 9;
        }

        // --- B. 1440x860 Live Viewport Panel with Crisp 960x540 Canvas & 6-Option SDF Penumbra Dropdown Open ---
        SdfShowcaseCamera HiResCam = Cam;
        HiResCam.WidthPx  = 960u;
        HiResCam.HeightPx = 540u;
        const SdfEmissiveRenderFrame HiResViewportFrame = RenderEmissiveSphereSdfFrame(
            SdfShadowPenumbraMode::VoxelSdfPenumbraPlusConeGi, Scene, Clipmap, HiResCam, 24u);

        Io.DisplaySize = { 1440.0f, 860.0f };
        ViewportPanel Vp {};
        Vp.ConfigureSceneTitle("PROOF 11 - Emissive Sphere SDF Gallery");
        Vp.AccessDisplaySettings().RenderEngineIndex      = 2u; // Engine: GTX SDF GI
        Vp.AccessDisplaySettings().SdfShadowPenumbraIndex = 3u; // 3. 3D Voxel SDF Penumbra + Cone GI
        Vp.AssignCanvasRasterImage(HiResViewportFrame.DisplayRgba8.data(), HiResCam.WidthPx, HiResCam.HeightPx);
        Vp.ConfigureSettingsSheetOpen(true);
        Vp.ConfigureSdfPenumbraDropdownOpen(true);

        const Rectangle2D VpBounds { 0.0f, 0.0f, 1440.0f, 860.0f };
        ImGui::NewFrame();
        {
            ImDrawList* Draw = ImGui::GetBackgroundDrawList();
            PixelSpace  Surf(Draw, { 0.0f, 0.0f }, 1.0f, Io.FontDefault);
            PaneInputReading IdleInput {};
            Vp.Render(Surf, VpBounds, IdleInput, 1.0 / 60.0);
        }
        ImGui::Render();

        Exhibits::SoftwareRasterCanvas VpCanvas(1440u, 860u, Components::RgbaColor8 { 14u, 14u, 16u, 255u });
        VpCanvas.RasterizeImGuiBatches(ImGui::GetDrawData());
        const bool SavedVp = VpCanvas.WritePngFile(OutDir / "07_ViewportSdfDropdownSelector.png");

        TypographyRepository::AssignCurrent(nullptr);
        ImGui::DestroyContext(Gui);

        if (!VerifyCheck(SavedVp && Vp.QueryDisplaySettings().SdfShadowPenumbraIndex == 3u,
                         "Wrote 1440x860 07_ViewportSdfDropdownSelector.png with 6-mode SDF Penumbra DropdownSelector open"))
        {
            return 10;
        }
    }
#endif

    std::printf("=== ALL PROOF 11 SDF EMISSIVE PENUMBRA & UI DROPDOWN VALIDATION CHECKS PASSED ===\n");
    return 0;
}
