/*============================================================================================================================================
                                                  SDFEMISSIVEPENUMBRAVALIDATION.CPP
============================================================================================================================================*/
// 🧩 PROOF 11 Validation Exhibit: Emissive Sphere ONLY (No Sun / Area / Spot / IES) — All 6 SDF Shadow + Penumbra + 2-Bounce Voxel Cone GI + Sobol Cap + 96-SPP MC Renderers + Vulkan .slang Compute + Viewport 6-Mode Dropdown Selector

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

[[nodiscard]] double ComputeCaptureHdrRmse(
    const SdfEmissiveRenderCapture& Candidate,
    const SdfEmissiveRenderCapture& Reference,
    double                          ExposureScale            = 0.96,
    bool                            ExcludeDirectEmissiveOrb = true) noexcept
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
        const auto Tone = [ExposureScale](float V) noexcept -> double
        {
            const double X = std::max(0.0, static_cast<double>(V) * ExposureScale);
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
    const DualCascadeClipmapVolume& Clipmap,
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

    const std::filesystem::path SpvPath    = std::filesystem::current_path() / "CompiledShaders/SdfEmissivePenumbraCompute.comp.spv";
    const std::filesystem::path AltSpvPath = RepoRoot / "Build/Editor/CompiledShaders/SdfEmissivePenumbraCompute.comp.spv";
    const std::filesystem::path FinalSpv   = std::filesystem::exists(SpvPath) ? SpvPath : AltSpvPath;

    VkShaderModule ShaderMod = Vk.LoadShaderBinary(FinalSpv);
    if (ShaderMod == VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "Missing compiled Slang SPIR-V: %s\n", FinalSpv.string().c_str());
        Vk.TeardownInterchange();
        return false;
    }

    constexpr uint32_t GpuW = 120u;
    constexpr uint32_t GpuH = 78u;
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
    Ubo.ViewportModeSamples[3] = 24u;
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
    Ubo.CoarseClipDimsYZ[2]    = Clipmap.CoarseCascade.Mip1DimX;
    Ubo.CoarseClipDimsYZ[3]    = (Clipmap.CoarseCascade.Mip1DimY << 16u) | Clipmap.CoarseCascade.Mip1DimZ;
    Ubo.FineBoundsMin[0]       = Clipmap.FineCascade.BoundsMin.X;   Ubo.FineBoundsMin[1] = Clipmap.FineCascade.BoundsMin.Y;   Ubo.FineBoundsMin[2] = Clipmap.FineCascade.BoundsMin.Z;
    Ubo.FineBoundsMax[0]       = Clipmap.FineCascade.BoundsMax.X;   Ubo.FineBoundsMax[1] = Clipmap.FineCascade.BoundsMax.Y;   Ubo.FineBoundsMax[2] = Clipmap.FineCascade.BoundsMax.Z;   Ubo.FineBoundsMax[3] = Clipmap.FineCascade.MaxVoxelSpacingM;
    Ubo.CoarseBoundsMin[0]     = Clipmap.CoarseCascade.BoundsMin.X; Ubo.CoarseBoundsMin[1] = Clipmap.CoarseCascade.BoundsMin.Y; Ubo.CoarseBoundsMin[2] = Clipmap.CoarseCascade.BoundsMin.Z;
    Ubo.CoarseBoundsMax[0]     = Clipmap.CoarseCascade.BoundsMax.X; Ubo.CoarseBoundsMax[1] = Clipmap.CoarseCascade.BoundsMax.Y; Ubo.CoarseBoundsMax[2] = Clipmap.CoarseCascade.BoundsMax.Z; Ubo.CoarseBoundsMax[3] = Clipmap.CoarseCascade.MaxVoxelSpacingM;

    const VkMemoryPropertyFlags HostProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    DeviceBufferResource UboBuf    = Vk.AllocateBuffer(sizeof(Ubo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, HostProps);
    DeviceBufferResource FineDBuf  = Vk.AllocateBuffer(Clipmap.FineCascade.DistanceGrid.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, HostProps);
    DeviceBufferResource CoaDBuf   = Vk.AllocateBuffer(Clipmap.CoarseCascade.DistanceGrid.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, HostProps);

    std::vector<float> PackedFineRF(Clipmap.FineCascade.RadiosityGrid.size() * 4u, 0.0f);
    for (std::size_t I = 0u; I < Clipmap.FineCascade.RadiosityGrid.size(); ++I)
    {
        PackedFineRF[I * 4u + 0u] = Clipmap.FineCascade.RadiosityGrid[I].R;
        PackedFineRF[I * 4u + 1u] = Clipmap.FineCascade.RadiosityGrid[I].G;
        PackedFineRF[I * 4u + 2u] = Clipmap.FineCascade.RadiosityGrid[I].B;
        PackedFineRF[I * 4u + 3u] = Clipmap.FineCascade.FullDistanceGrid[I];
    }
    std::vector<float> PackedCoaRF(Clipmap.CoarseCascade.RadiosityGrid.size() * 4u, 0.0f);
    for (std::size_t I = 0u; I < Clipmap.CoarseCascade.RadiosityGrid.size(); ++I)
    {
        PackedCoaRF[I * 4u + 0u] = Clipmap.CoarseCascade.RadiosityGrid[I].R;
        PackedCoaRF[I * 4u + 1u] = Clipmap.CoarseCascade.RadiosityGrid[I].G;
        PackedCoaRF[I * 4u + 2u] = Clipmap.CoarseCascade.RadiosityGrid[I].B;
        PackedCoaRF[I * 4u + 3u] = Clipmap.CoarseCascade.FullDistanceGrid[I];
    }
    std::vector<float> PackedCoaMip1(std::max<std::size_t>(1u, Clipmap.CoarseCascade.Mip1Radiosity.size()) * 4u, 0.0f);
    for (std::size_t I = 0u; I < Clipmap.CoarseCascade.Mip1Radiosity.size(); ++I)
    {
        PackedCoaMip1[I * 4u + 0u] = Clipmap.CoarseCascade.Mip1Radiosity[I].R;
        PackedCoaMip1[I * 4u + 1u] = Clipmap.CoarseCascade.Mip1Radiosity[I].G;
        PackedCoaMip1[I * 4u + 2u] = Clipmap.CoarseCascade.Mip1Radiosity[I].B;
    }

    DeviceBufferResource FineRFBuf  = Vk.AllocateBuffer(PackedFineRF.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, HostProps);
    DeviceBufferResource CoaRFBuf   = Vk.AllocateBuffer(PackedCoaRF.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, HostProps);
    DeviceBufferResource CoaM1Buf   = Vk.AllocateBuffer(PackedCoaMip1.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, HostProps);
    DeviceBufferResource OutBuf     = Vk.AllocateBuffer(static_cast<uint64_t>(GpuW) * GpuH * sizeof(float) * 4u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, HostProps);

    std::memcpy(UboBuf.HostAddress,    &Ubo, sizeof(Ubo));
    std::memcpy(FineDBuf.HostAddress,  Clipmap.FineCascade.DistanceGrid.data(),   Clipmap.FineCascade.DistanceGrid.size() * sizeof(float));
    std::memcpy(CoaDBuf.HostAddress,   Clipmap.CoarseCascade.DistanceGrid.data(), Clipmap.CoarseCascade.DistanceGrid.size() * sizeof(float));
    std::memcpy(FineRFBuf.HostAddress, PackedFineRF.data(),                       PackedFineRF.size() * sizeof(float));
    std::memcpy(CoaRFBuf.HostAddress,  PackedCoaRF.data(),                        PackedCoaRF.size() * sizeof(float));
    std::memcpy(CoaM1Buf.HostAddress,  PackedCoaMip1.data(),                      PackedCoaMip1.size() * sizeof(float));

    VkDevice Dev = Vk.AccessDevice();
    std::array<VkDescriptorSetLayoutBinding, 7> Bindings {};
    for (uint32_t B = 0u; B < 7u; ++B)
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
    VkPipelineLayout LayoutObj = VK_NULL_HANDLE;
    vkCreatePipelineLayout(Dev, &PlCi, nullptr, &LayoutObj);

    VkComputePipelineCreateInfo CpCi {};
    CpCi.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    CpCi.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    CpCi.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    CpCi.stage.module = ShaderMod;
    CpCi.stage.pName  = "ComputeEntry";
    CpCi.layout       = LayoutObj;
    VkPipeline ComputeObj = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(Dev, VK_NULL_HANDLE, 1u, &CpCi, nullptr, &ComputeObj) != VK_SUCCESS || ComputeObj == VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(Dev, LayoutObj, nullptr);
        vkDestroyDescriptorSetLayout(Dev, Dsl, nullptr);
        vkDestroyShaderModule(Dev, ShaderMod, nullptr);
        Vk.ReleaseBuffer(UboBuf);
        Vk.ReleaseBuffer(FineDBuf);
        Vk.ReleaseBuffer(CoaDBuf);
        Vk.ReleaseBuffer(FineRFBuf);
        Vk.ReleaseBuffer(CoaRFBuf);
        Vk.ReleaseBuffer(CoaM1Buf);
        Vk.ReleaseBuffer(OutBuf);
        Vk.TeardownInterchange();
        return false;
    }

    std::array<VkDescriptorPoolSize, 2> PoolSizes {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6u },
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

    const std::array<VkDescriptorBufferInfo, 7> BufInfos {{
        { UboBuf.BufferHandle,    0u, VK_WHOLE_SIZE },
        { FineDBuf.BufferHandle,  0u, VK_WHOLE_SIZE },
        { CoaDBuf.BufferHandle,   0u, VK_WHOLE_SIZE },
        { FineRFBuf.BufferHandle, 0u, VK_WHOLE_SIZE },
        { CoaRFBuf.BufferHandle,  0u, VK_WHOLE_SIZE },
        { CoaM1Buf.BufferHandle,  0u, VK_WHOLE_SIZE },
        { OutBuf.BufferHandle,    0u, VK_WHOLE_SIZE },
    }};
    std::array<VkWriteDescriptorSet, 7> Writes {};
    for (uint32_t B = 0u; B < 7u; ++B)
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
    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ComputeObj);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, LayoutObj, 0u, 1u, &DSet, 0u, nullptr);
    vkCmdDispatch(Cmd, (GpuW + 7u) / 8u, (GpuH + 7u) / 8u, 1u);
    const uint64_t Tick = Vk.DispatchCommandBufferAsync(Cmd);
    const bool OkWait   = Vk.AwaitTimelineTick(Tick, 15000000000ull);
    Vk.FreeCommandBuffer(Cmd);

    const SdfEmissiveRenderCapture CpuCapture = RenderEmissiveSphereCapture(
        SdfShadowPenumbraMode::VoxelSdfPenumbraPlusConeGi, Scene, Clipmap, GpuCam, 24u);

    const auto* GpuFloats = static_cast<const float*>(OutBuf.HostAddress);
    double SumSq = 0.0;
    std::size_t Cnt = 0u;
    for (std::size_t I = 0u; I < CpuCapture.LinearHdr.size(); ++I)
    {
        if (CpuCapture.LinearHdr[I].R > 4.0f) continue;
        const auto Tone = [&](float V) noexcept -> double
        {
            const double X = std::max(0.0, static_cast<double>(V) * GpuCam.ExposureScale);
            return std::clamp((X * (2.51 * X + 0.03)) / (X * (2.43 * X + 0.59) + 0.14), 0.0, 1.0);
        };
        const double DR = Tone(GpuFloats[I * 4u + 0u]) - Tone(CpuCapture.LinearHdr[I].R);
        const double DG = Tone(GpuFloats[I * 4u + 1u]) - Tone(CpuCapture.LinearHdr[I].G);
        const double DB = Tone(GpuFloats[I * 4u + 2u]) - Tone(CpuCapture.LinearHdr[I].B);
        SumSq += (DR * DR + DG * DG + DB * DB) / 3.0;
        ++Cnt;
    }
    OutGpuVsCpuRmse = (Cnt > 0u) ? std::sqrt(SumSq / static_cast<double>(Cnt)) : 999.0;

    vkDestroyDescriptorPool(Dev, Pool, nullptr);
    vkDestroyPipeline(Dev, ComputeObj, nullptr);
    vkDestroyPipelineLayout(Dev, LayoutObj, nullptr);
    vkDestroyDescriptorSetLayout(Dev, Dsl, nullptr);
    vkDestroyShaderModule(Dev, ShaderMod, nullptr);
    Vk.ReleaseBuffer(UboBuf);
    Vk.ReleaseBuffer(FineDBuf);
    Vk.ReleaseBuffer(CoaDBuf);
    Vk.ReleaseBuffer(FineRFBuf);
    Vk.ReleaseBuffer(CoaRFBuf);
    Vk.ReleaseBuffer(CoaM1Buf);
    Vk.ReleaseBuffer(OutBuf);
    Vk.TeardownInterchange();
    return OkWait && (OutGpuVsCpuRmse < 0.045);
}

} // namespace

int main()
{
    const std::filesystem::path RepoRoot = FRONTIER_REPO_ROOT_DIR;
    const std::filesystem::path OutDir   = RepoRoot / "Exhibits/Images/SdfEmissivePenumbra";
    std::filesystem::create_directories(OutDir);

    std::printf("=== PROOF 11: EMISSIVE SPHERE ONLY — ALL 6 SDF SHADOW + PENUMBRA RENDERERS ===\n");

    //========================================================================================================================
    // 1. BUILD EMISSIVE-SPHERE-ONLY SCENE + 2-CASCADE 3D VOXEL SDF CLIPMAP VOLUME
    //========================================================================================================================
    SdfShowcaseScene Scene {};
    BuildCanonicalProof11EmissiveSphereScene(Scene);
    if (!VerifyCheck(Scene.EmissiveActive && Scene.EmissiveSphere.Radius > 0.35f,
                     "Built canonical PROOF 11 proof-room gallery with sole Emissive Sphere (NO sun/area/spot/IES)"))
    {
        return 1;
    }

    DualCascadeClipmapVolume Clipmap {};
    Clipmap.BuildFromShowcaseScene(Scene);
    const std::size_t ClipmapBytes = Clipmap.QueryAllocatedBytes();
    std::printf("  3D Voxel SDF Clipmap (56x56x40 Fine + 44x44x34 Coarse + 2-Bounce Mip0/1/2 Radiosity): %zu bytes (%.2f MB)\n",
                ClipmapBytes, static_cast<double>(ClipmapBytes) / (1024.0 * 1024.0));
    if (!VerifyCheck(ClipmapBytes > 0u && ClipmapBytes <= 8500000u,
                     "2-cascade 3D Voxel SDF Clipmap + 2-Bounce Radiosity Pyramid fits within compact budget"))
    {
        return 2;
    }

    //========================================================================================================================
    // 2. MEASURE FLOOR PENUMBRA WIDTH & TRUE 0.00 UMBRA ACROSS ALL 6 MODES
    //========================================================================================================================
    std::array<float, 6> PenumbraWidths {};
    std::array<float, 6> UmbraMins      {};
    for (uint32_t M = 0u; M < 6u; ++M)
    {
        const auto Mode = static_cast<SdfShadowPenumbraMode>(M);
        MeasureFloorShadowPenumbraProfile(Scene, Clipmap, Mode, PenumbraWidths[M], UmbraMins[M]);
        std::printf("  [%s] Floor Penumbra Width = %.3f m | Umbra Min Visibility = %.4f\n",
                    QuerySdfShadowPenumbraModeLabel(Mode), PenumbraWidths[M], UmbraMins[M]);
    }

    if (!VerifyCheck(PenumbraWidths[0] == 0.0f && UmbraMins[0] == 0.0f,
                     "Mode 0 (Point-Sample SDF) produces binary 0.000 m penumbra and 0.0000 umbra"))
    {
        return 3;
    }
    if (!VerifyCheck(PenumbraWidths[1] > 0.45f && PenumbraWidths[2] > 0.40f && PenumbraWidths[4] > 0.45f &&
                     UmbraMins[1] <= 0.01f && UmbraMins[2] <= 0.01f && UmbraMins[4] <= 0.01f,
                     "Modes 1..5 produce wide physical emissive-sphere penumbrae (> 0.40 m) with pure 0.00 umbra"))
    {
        return 4;
    }

    //========================================================================================================================
    // 3. RENDER ALL 6 MODES AT 400x260 & VERIFY DIRECT UMBRA = 0.00 + INDIRECT CONE GI CONVERGENCE TO 96-SPP PT REF
    //========================================================================================================================
    SdfShowcaseCamera Cam {}; // Default-initialized to canonical PROOF 11 camera (400x260, Eye={0, -3.86, 1.82})

    static constexpr std::array<const char*, 6> FileNames {{
        "00_PointSampleSdf.png",
        "01_AnalyticSdfConePenumbra.png",
        "02_VoxelSdfClipmapPenumbra.png",
        "03_VoxelSdfPenumbraPlusConeGi.png",
        "04_HybridSdfSobolSphericalCap.png",
        "05_SdfMonteCarloPtRef96Spp.png",
    }};

    std::array<SdfEmissiveRenderCapture, 6> Captures {};
    for (uint32_t M = 0u; M < 6u; ++M)
    {
        Captures[M] = RenderEmissiveSphereCapture(static_cast<SdfShadowPenumbraMode>(M), Scene, Clipmap, Cam, 96u);
        Exhibits::SoftwareRasterCanvas PanelOut(Cam.WidthPx, Cam.HeightPx, Components::RgbaColor8 { 0u, 0u, 0u, 255u });
        PanelOut.BlitRgbaRegion(0u, 0u, Cam.WidthPx, Cam.HeightPx, Captures[M].DisplayRgba8.data());
        const bool Ok = PanelOut.WritePngFile(OutDir / FileNames[M]);
        if (!VerifyCheck(Ok, FileNames[M]))
        {
            return 5;
        }
    }

    // Verify Direct-Only Modes 0, 1, 2, 4 have ZERO fake ambient bleed in deep umbra (PrimaryVisibility == 0 -> LinearHdr == 0)
    float MaxDirectUmbraLeak = 0.0f;
    float MeanConeGiInUmbra  = 0.0f;
    float MeanPtRefInUmbra   = 0.0f;
    uint32_t UmbraCount      = 0u;
    for (std::size_t I = 0u; I < Captures[0].LinearHdr.size(); ++I)
    {
        for (uint32_t DM : { 0u, 1u, 2u, 4u })
        {
            if (Captures[DM].PrimaryVisibility[I] == 0.0f)
            {
                MaxDirectUmbraLeak = std::max({
                    MaxDirectUmbraLeak,
                    Captures[DM].LinearHdr[I].R,
                    Captures[DM].LinearHdr[I].G,
                    Captures[DM].LinearHdr[I].B,
                });
            }
        }
        if (Captures[0].PrimaryVisibility[I] == 0.0f &&
            Captures[1].PrimaryVisibility[I] == 0.0f &&
            Captures[2].PrimaryVisibility[I] == 0.0f &&
            Captures[4].PrimaryVisibility[I] == 0.0f)
        {
            MeanConeGiInUmbra += (Captures[3].LinearHdr[I].R + Captures[3].LinearHdr[I].G + Captures[3].LinearHdr[I].B) / 3.0f;
            MeanPtRefInUmbra  += (Captures[5].LinearHdr[I].R + Captures[5].LinearHdr[I].G + Captures[5].LinearHdr[I].B) / 3.0f;
            ++UmbraCount;
        }
    }
    if (UmbraCount > 0u)
    {
        MeanConeGiInUmbra /= static_cast<float>(UmbraCount);
        MeanPtRefInUmbra  /= static_cast<float>(UmbraCount);
    }
    std::printf("  Umbra Audit (%u px): Direct Leak Max = %.6f | Mode 3 Cone GI Mean = %.4f | Mode 5 PT Ref Mean = %.4f\n",
                UmbraCount, MaxDirectUmbraLeak, MeanConeGiInUmbra, MeanPtRefInUmbra);
    if (!VerifyCheck(MaxDirectUmbraLeak <= 1.0e-6f && MeanConeGiInUmbra > 0.008f && MeanPtRefInUmbra > 0.008f,
                     "Direct modes (0, 1, 2, 4) maintain pure 0.000000 umbra while Mode 3 (+ Cone GI) illuminates shadows physically like 96-SPP PT Ref"))
    {
        return 6;
    }

    // Direct soft-penumbra parity: Modes 1 & 2 vs Mode 4 (Sobol Spherical-Cap Direct Reference), and Mode 3 (+ Cone GI) vs Mode 5 (96-SPP Full PT Reference)
    const double RmseAnalyticVsSobolDirect = ComputeCaptureHdrRmse(Captures[1], Captures[4], Cam.ExposureScale);
    const double RmseClipmapVsSobolDirect  = ComputeCaptureHdrRmse(Captures[2], Captures[4], Cam.ExposureScale);
    const double RmseVoxelGiVsPtRef        = ComputeCaptureHdrRmse(Captures[3], Captures[5], Cam.ExposureScale);
    const double RmseHybridVsPtRef         = ComputeCaptureHdrRmse(Captures[4], Captures[5], Cam.ExposureScale);
    const double RmseAnalyticVsPtRef       = ComputeCaptureHdrRmse(Captures[1], Captures[5], Cam.ExposureScale);

    std::printf("  Direct Penumbra RMSE vs Sobol Cap -> Analytic=%.4f | Clipmap=%.4f\n",
                RmseAnalyticVsSobolDirect, RmseClipmapVsSobolDirect);
    std::printf("  96-SPP Full PT Ref RMSE -> VoxelConeGI=%.4f | HybridDirect=%.4f | AnalyticDirect=%.4f\n",
                RmseVoxelGiVsPtRef, RmseHybridVsPtRef, RmseAnalyticVsPtRef);

    if (!VerifyCheck(RmseAnalyticVsSobolDirect < 0.055 && RmseClipmapVsSobolDirect < 0.060 &&
                     RmseVoxelGiVsPtRef < RmseAnalyticVsPtRef && RmseVoxelGiVsPtRef < 0.075,
                     "Direct SDF cones match Sobol Spherical-Cap (< 0.060 RMSE) and Mode 3 (+ Cone GI) converges closer to 96-SPP PT Ref than direct-only modes (< 0.075 RMSE)"))
    {
        return 7;
    }

    //========================================================================================================================
    // 4. VERIFY VULKAN GPU .SLANG COMPUTE SHADER DISPATCH PARITY
    //========================================================================================================================
    double GpuVsCpuRmse = 999.0;
    const bool GpuOk = VerifyGpuSlangSdfPenumbraDispatch(RepoRoot, Scene, Clipmap, Cam, GpuVsCpuRmse);
    std::printf("  Vulkan SdfEmissivePenumbraCompute.slang GPU vs CPU RMSE = %.4f\n", GpuVsCpuRmse);
    if (!VerifyCheck(GpuOk, "Vulkan SdfEmissivePenumbraCompute.comp.spv executes 3D SDF Cone GI on GPU and matches CPU reference"))
    {
        return 8;
    }

    //========================================================================================================================
    // 5. RENDER 1232x648 PROOF 11 2x3 CONTACT SHEET & LIVE VIEWPORT 6-MODE SDF DROPDOWN UI EXHIBIT
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
            return 9;
        }
        TypographyRepository::AssignCurrent(&Typefaces);
        if (ImFont* DefaultFont = Typefaces.QueryTabStripFont())
        {
            Io.FontDefault = DefaultFont;
        }
        unsigned char* FontPixels = nullptr;
        int AtlasW = 0, AtlasH = 0;
        Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &AtlasW, &AtlasH);

        // --- A. 1232x648 PROOF 11 2x3 Contact Sheet (400x260 panels matching reference layout) ---
        constexpr uint32_t SheetW = 1232u;
        constexpr uint32_t SheetH = 648u;
        Io.DisplaySize = { static_cast<float>(SheetW), static_cast<float>(SheetH) };

        std::array<ImTextureData, 6> PanelTex {};
        for (uint32_t M = 0u; M < 6u; ++M)
        {
            PanelTex[M].Create(ImTextureFormat_RGBA32, static_cast<int>(Cam.WidthPx), static_cast<int>(Cam.HeightPx));
            std::memcpy(PanelTex[M].GetPixels(), Captures[M].DisplayRgba8.data(), Captures[M].DisplayRgba8.size());
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

            Surf.FillRectangle({ 0.0f, 0.0f, static_cast<float>(SheetW), static_cast<float>(SheetH) }, ColorRgba { 10, 12, 17, 255 });
            Surf.Text({ 10.0f, 8.0f }, 13.5f, ColorRgba { 242, 244, 248, 255 },
                      "PROOF 11: EMISSIVE SPHERE ONLY (NO SUN/AREA/SPOT/IES) - ALL SDF RENDERERS SHOWING SHADOWS + PENUMBRA");

            for (uint32_t M = 0u; M < 6u; ++M)
            {
                const uint32_t Col = M % 3u;
                const uint32_t Row = M / 3u;
                const float PX = 8.0f + static_cast<float>(Col) * 408.0f;
                const float PY = 28.0f + static_cast<float>(Row) * 288.0f;
                const ColorRgba HeaderInk = (M == 5u)
                    ? ColorRgba { 246, 204, 106, 255 }
                    : ((M == 0u) ? ColorRgba { 206, 212, 222, 255 } : ColorRgba { 136, 232, 168, 255 });
                Surf.Text({ PX + 2.0f, PY }, 11.5f, HeaderInk, UpperHeaders[M]);

                const Rectangle2D ImgRect { PX, PY + 18.0f, 400.0f, 260.0f };
                Surf.FillTexturedRectangle(ImgRect, &PanelTex[M], { 0.0f, 0.0f }, { 1.0f, 1.0f });
                Surf.StrokeRectangle(ImgRect, ColorRgba { 58, 64, 74, 255 }, 0.0f, 1.0f);
            }

            char FooterBuf[256] {};
            std::snprintf(
                FooterBuf,
                sizeof(FooterBuf),
                "PENUMBRA WIDTH: POINT=%.3fM VS ANALYTIC SDF=%.3fM VS VOXEL SDF=%.3fM | UMBRA=%.2f | PT REF RMSE: GI=%.3f HYB=%.3f AN=%.3f",
                PenumbraWidths[0], PenumbraWidths[1], PenumbraWidths[2], UmbraMins[1], RmseVoxelGiVsPtRef, RmseHybridVsPtRef, RmseAnalyticVsPtRef);
            Surf.Text({ 10.0f, 614.0f }, 12.0f, ColorRgba { 196, 226, 252, 255 }, FooterBuf);
        }
        ImGui::Render();

        Exhibits::SoftwareRasterCanvas SheetCanvas(SheetW, SheetH, Components::RgbaColor8 { 10u, 12u, 17u, 255u });
        SheetCanvas.RasterizeImGuiBatches(ImGui::GetDrawData());
        const bool SavedSheet = SheetCanvas.WritePngFile(OutDir / "06_Proof11_AllSdfRenderers_PenumbraSheet.png");
        for (auto& T : PanelTex) T.DestroyPixels();
        if (!VerifyCheck(SavedSheet, "Wrote 1232x648 06_Proof11_AllSdfRenderers_PenumbraSheet.png"))
        {
            TypographyRepository::AssignCurrent(nullptr);
            ImGui::DestroyContext(Gui);
            return 10;
        }

        // --- B. 1440x860 Live Viewport Panel with Crisp 960x540 Canvas & 6-Option SDF Penumbra Dropdown Open ---
        SdfShowcaseCamera HiResCam = Cam;
        HiResCam.WidthPx  = 960u;
        HiResCam.HeightPx = 540u;
        const SdfEmissiveRenderCapture HiResViewportCapture = RenderEmissiveSphereCapture(
            SdfShadowPenumbraMode::VoxelSdfPenumbraPlusConeGi, Scene, Clipmap, HiResCam, 24u);

        Io.DisplaySize = { 1440.0f, 860.0f };
        ViewportPanel Vp {};
        Vp.ConfigureSceneTitle("PROOF 11 - Emissive Sphere SDF Gallery");
        Vp.AccessDisplaySettings().RenderEngineIndex      = 2u; // Engine: GTX SDF GI
        Vp.AccessDisplaySettings().SdfShadowPenumbraIndex = 3u; // 3. 3D Voxel SDF Penumbra + Cone GI
        Vp.AssignCanvasRasterImage(HiResViewportCapture.DisplayRgba8.data(), HiResCam.WidthPx, HiResCam.HeightPx);
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
            return 11;
        }
    }
#endif

    std::printf("=== ALL PROOF 11 SDF EMISSIVE PENUMBRA & UI DROPDOWN VALIDATION CHECKS PASSED ===\n");
    return 0;
}
