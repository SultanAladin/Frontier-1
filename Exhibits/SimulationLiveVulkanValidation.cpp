/*============================================================================================================================================
                                                SIMULATIONLIVEVULKANVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Real Vulkan 1.2 Live-GPU Validation & Visual Proof Exhibit (NO headless-only bypass!)
//    Executes on SwiftShader/Lavapipe in CI and real Vulkan 1.2 GPUs locally, proving:
//    1. Real VulkanComputeGpu execution across ALL 4 GPU pipelines (Fluid 3D, Particles, Deformable FEM/XPBD, VAT Baker)
//    2. Non-zero, finite, physically evolving GPU VRAM readback values across all 4 pipelines (no CPU mirror fallback)
//    3. EditorSimulationAdapter Play / Pause / Step / Stop lifecycle + ComponentSimulationPreviewDriver real Vulkan attachment
//    4. Real Vulkan 1.2 SimulationViewportOverlayPass rendering Fluid 3D raymarch + Deformable cage-skinned mesh +
//       Particle sprites directly from SimulationGpuRenderFeedPacket into visually inspected PNG proofs!

#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometrySpace/Deformation/CageWeightTypes.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasGpuBaker.h"
#include "Engine/SimulationSpace/ComponentSimulationPreviewDriver.h"
#include "Engine/SimulationSpace/Deformable/DeformablePresetRegistry.h"
#include "Engine/SimulationSpace/EditorSimulationAdapter.h"
#include "Engine/SimulationSpace/SimulationGpuRenderFeed.h"
#include "Engine/SimulationSpace/SimulationSceneDocument.h"
#include "Engine/SimulationSpace/SimulationViewportOverlayPass.h"
#include "Engine/SimulationSpace/WorldSimulationSession.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{

struct ValidationTracker
{
    uint32_t Passed = 0u;
    uint32_t Failed = 0u;

    void Check(bool condition, const char* label)
    {
        if (condition)
        {
            ++Passed;
            std::cout << "  [PASS] " << label << "\n";
        }
        else
        {
            ++Failed;
            std::cerr << "  [FAIL] " << label << "\n";
        }
    }
};

// IEEE-754 binary16 -> float32 decoder for RGBA16F 3D Fluid texture VRAM verification
float HalfToFloat(uint16_t h) noexcept
{
    const uint32_t sign = static_cast<uint32_t>(h >> 15u) << 31u;
    const uint32_t exp  = (h >> 10u) & 0x1Fu;
    const uint32_t mant = h & 0x03FFu;
    uint32_t fBits = 0u;
    if (exp == 0u)
    {
        if (mant == 0u)
        {
            fBits = sign;
        }
        else
        {
            float v = static_cast<float>(mant) / 1024.0f * (1.0f / 16384.0f);
            return (sign != 0u) ? -v : v;
        }
    }
    else if (exp == 31u)
    {
        fBits = sign | 0x7F800000u | (mant << 13u);
    }
    else
    {
        fBits = sign | ((exp + 112u) << 23u) | (mant << 13u);
    }
    float out = 0.0f;
    std::memcpy(&out, &fBits, sizeof(out));
    return out;
}

bool ReadbackBufferBytes(
    Frontier::GeometricRaster::VulkanInterchange& vk,
    VkBuffer srcBuffer,
    uint64_t byteCount,
    uint64_t waitTimeline,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (srcBuffer == VK_NULL_HANDLE || byteCount == 0ull) return false;
    if (waitTimeline > 0ull) (void)vk.AwaitTimelineTick(waitTimeline);

    auto staging = vk.AllocateBuffer(
        byteCount,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (staging.BufferHandle == VK_NULL_HANDLE || staging.HostAddress == nullptr)
    {
        if (staging.BufferHandle != VK_NULL_HANDLE) vk.ReleaseBuffer(staging);
        return false;
    }

    VkCommandBuffer cmd = vk.BeginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE)
    {
        vk.ReleaseBuffer(staging);
        return false;
    }

    VkBufferCopy region { 0ull, 0ull, byteCount };
    vkCmdCopyBuffer(cmd, srcBuffer, staging.BufferHandle, 1u, &region);

    const uint64_t tick = vk.DispatchCommandBufferAsync(cmd);
    (void)vk.AwaitTimelineTick(tick);
    vk.FreeCommandBuffer(cmd);

    outBytes.resize(static_cast<size_t>(byteCount));
    std::memcpy(outBytes.data(), staging.HostAddress, static_cast<size_t>(byteCount));
    vk.ReleaseBuffer(staging);
    return true;
}

bool ReadbackImageBytes(
    Frontier::GeometricRaster::VulkanInterchange& vk,
    VkImage srcImage,
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    uint32_t bytesPerPixel,
    uint64_t waitTimeline,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (srcImage == VK_NULL_HANDLE || width == 0u || height == 0u || depth == 0u) return false;
    if (waitTimeline > 0ull) (void)vk.AwaitTimelineTick(waitTimeline);

    const uint64_t totalBytes = static_cast<uint64_t>(width) * height * depth * bytesPerPixel;
    auto staging = vk.AllocateBuffer(
        totalBytes,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (staging.BufferHandle == VK_NULL_HANDLE || staging.HostAddress == nullptr)
    {
        if (staging.BufferHandle != VK_NULL_HANDLE) vk.ReleaseBuffer(staging);
        return false;
    }

    VkCommandBuffer cmd = vk.BeginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE)
    {
        vk.ReleaseBuffer(staging);
        return false;
    }

    VkImageSubresourceRange range { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
    VkImageMemoryBarrier toSrc {}; toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toSrc.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    toSrc.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    toSrc.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    toSrc.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.image               = srcImage;
    toSrc.subresourceRange    = range;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0u, 0u, nullptr, 0u, nullptr, 1u, &toSrc);

    VkBufferImageCopy region {};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
    region.imageExtent      = { width, height, depth };
    vkCmdCopyImageToBuffer(cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.BufferHandle, 1u, &region);

    VkImageMemoryBarrier backGen {}; backGen.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    backGen.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    backGen.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    backGen.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    backGen.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    backGen.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    backGen.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    backGen.image               = srcImage;
    backGen.subresourceRange    = range;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0u, 0u, nullptr, 0u, nullptr, 1u, &backGen);

    const uint64_t tick = vk.DispatchCommandBufferAsync(cmd);
    (void)vk.AwaitTimelineTick(tick);
    vk.FreeCommandBuffer(cmd);

    outBytes.resize(static_cast<size_t>(totalBytes));
    std::memcpy(outBytes.data(), staging.HostAddress, static_cast<size_t>(totalBytes));
    vk.ReleaseBuffer(staging);
    return true;
}

Frontier::GeometrySpace::Deformation::CageEmbeddedSurface BuildTetCageSurface(
    const Frontier::SimulationSpace::Deformable::DeformableBody& body)
{
    Frontier::GeometrySpace::Deformation::CageEmbeddedSurface surf {};
    surf.Mode = Frontier::GeometrySpace::Deformation::CageEmbedMode::Tetrahedral3D;
    auto appendTri = [&](uint32_t i0, uint32_t i1, uint32_t i2, uint32_t hostTet)
    {
        if (i0 >= body.PointCount || i1 >= body.PointCount || i2 >= body.PointCount) return;
        const float* p0 = &body.RestPositions[i0 * 3u];
        const float* p1 = &body.RestPositions[i1 * 3u];
        const float* p2 = &body.RestPositions[i2 * 3u];
        const float e1[3] { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
        const float e2[3] { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
        float n[3] {
            e1[1] * e2[2] - e1[2] * e2[1],
            e1[2] * e2[0] - e1[0] * e2[2],
            e1[0] * e2[1] - e1[1] * e2[0]
        };
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 1.0e-6f) { n[0] /= len; n[1] /= len; n[2] /= len; }
        else { n[0] = 0.0f; n[1] = 1.0f; n[2] = 0.0f; }

        const uint32_t baseV = static_cast<uint32_t>(surf.RestSurface.RestPositions.size() / 3u);
        const uint32_t ids[3] { i0, i1, i2 };
        for (uint32_t k = 0u; k < 3u; ++k)
        {
            const uint32_t pt = ids[k];
            surf.RestSurface.RestPositions.push_back(body.RestPositions[pt * 3u + 0u]);
            surf.RestSurface.RestPositions.push_back(body.RestPositions[pt * 3u + 1u]);
            surf.RestSurface.RestPositions.push_back(body.RestPositions[pt * 3u + 2u]);
            surf.RestSurface.RestNormals.push_back(n[0]);
            surf.RestSurface.RestNormals.push_back(n[1]);
            surf.RestSurface.RestNormals.push_back(n[2]);
            surf.CageIndices.push_back(pt); surf.CageIndices.push_back(0u); surf.CageIndices.push_back(0u); surf.CageIndices.push_back(0u);
            surf.CageWeights.push_back(1.0f); surf.CageWeights.push_back(0.0f); surf.CageWeights.push_back(0.0f); surf.CageWeights.push_back(0.0f);
            surf.ExtraParams.push_back(static_cast<float>(hostTet));
            surf.ExtraParams.push_back(0.0f);
            surf.ExtraParams.push_back(0.0f);
            surf.ExtraParams.push_back(0.0f);
            surf.RestSurface.Indices.push_back(baseV + k);
        }
    };

    for (uint32_t t = 0u; t < body.TetCount; ++t)
    {
        const uint32_t a = body.TetIndices[t * 4u + 0u];
        const uint32_t b = body.TetIndices[t * 4u + 1u];
        const uint32_t c = body.TetIndices[t * 4u + 2u];
        const uint32_t d = body.TetIndices[t * 4u + 3u];
        appendTri(a, c, b, t);
        appendTri(a, b, d, t);
        appendTri(b, c, d, t);
        appendTri(c, a, d, t);
    }
    return surf;
}

bool WritePngRgba8(const std::filesystem::path& path, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba)
{
    if (rgba.size() < static_cast<size_t>(w) * h * 4u) return false;
    return stbi_write_png(path.string().c_str(), static_cast<int>(w), static_cast<int>(h), 4,
                          rgba.data(), static_cast<int>(w * 4u)) != 0;
}

Frontier::SimulationSpace::SimulationSceneDocument BuildLiveProofScene()
{
    using namespace Frontier::SimulationSpace;
    SimulationSceneDocument doc {};
    doc.Title                = "LiveVulkanProofScene";
    doc.FixedStepHz          = 60.0f;
    doc.EnableVehicleCoupling = false;
    doc.FluidGridCells       = { 24u, 24u, 24u };
    doc.FluidCellSizeMeters  = 0.28f;
    doc.FluidCentreMeters    = { 0.0f, 0.45f, 0.0f };
    doc.FluidBuoyancy        = 2.8f;
    doc.FluidCombustion      = 1.7f;
    doc.FluidPressureIters   = 10u;

    // 1. Swirl vortex force field centered across the studio stage
    SceneForceEntry swirl {};
    swirl.Identifier   = "ProofVortex";
    swirl.ModelOrdinal = ForceFields::ForceModelOrdinal::RotationalColumn;
    swirl.Position     = { 1.85f, -0.4f, 0.0f };
    swirl.Direction    = { 0.0f, 1.0f, 0.0f };
    swirl.Strength     = 5.5f;
    swirl.RadiusMeters = 7.0f;
    doc.Forces.push_back(swirl);

    // 2. Fiery smoke + flame release source on left of stage (resting on y = -1.78m floor)
    SceneFluidSourceEntry firePlume {};
    firePlume.Identifier   = "FlamePlume";
    firePlume.Position     = { -2.0f, -1.25f, 0.0f };
    firePlume.RadiusMeters = 0.72f;
    firePlume.VelocityMps  = { 0.25f, 4.8f, 0.10f };
    firePlume.Density      = 2.8f;
    firePlume.Temperature  = 1.95f;
    firePlume.Fuel         = 1.5f;
    doc.FluidSources.push_back(firePlume);

    // 3. Particle stream rising & swirling on right of stage (resting on y = -1.78m floor)
    SceneParticleCloudEntry cloud {};
    cloud.Identifier     = "ProofSparks";
    cloud.Position       = { 1.85f, -1.35f, 0.0f };
    cloud.RadiusMeters   = 0.55f;
    cloud.ParticleCount  = 512u;
    cloud.ReleaseRate    = 240.0f;
    cloud.SpeedMps       = 3.4f;
    cloud.LifetimeSec    = 3.5f;
    cloud.GravityY       = -0.6f;
    cloud.GasInfluence   = 0.55f;
    cloud.FieldInfluence = 0.85f;
    doc.ParticleClouds.push_back(cloud);

    // 4. Deformable SoftBody instance
    SceneSoftBodyPlacement soft {};
    soft.Label        = "ProofFemTire";
    soft.SoftBodyPath = "EngineContent/Simulation/Deformables/SportTireFem.SoftBody";
    soft.WorldOrigin  = { 0.25f, 1.25f, 0.6f };
    doc.SoftBodies.push_back(soft);

    return doc;
}

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::SimulationSpace;

    ValidationTracker T {};
    std::cout << "=== Frontier SimulationLiveVulkanValidation (Real Vulkan 1.2 Live GPU + PNG Proofs) ===\n";

    const std::filesystem::path repoRoot { FRONTIER_REPO_ROOT_DIR };
    const std::filesystem::path shaderDir { FRONTIER_COMPILED_SHADER_DIR };
    const std::filesystem::path pngOutDir = repoRoot / "Exhibits/Images/SimulationVulkan";
    std::filesystem::create_directories(pngOutDir);

    // ------------------------------------------------------------------------------------------------
    // 1. Initialize REAL Vulkan 1.2 device (SwiftShader/Lavapipe in CI, hardware GPU locally — NO bypass!)
    // ------------------------------------------------------------------------------------------------
    GeometricRaster::VulkanInterchange vkDevice {};
    const bool vkReady = vkDevice.InitializeInterchange(repoRoot);
    T.Check(vkReady && vkDevice.Ready(), "Real Vulkan 1.2 VulkanInterchange initialized (no headless bypass)");
    if (!vkReady || !vkDevice.Ready())
    {
        std::cerr << "ABORT: VulkanInterchange could not initialize; real Vulkan 1.2 device required.\n";
        return 1;
    }

    // ------------------------------------------------------------------------------------------------
    // 2. Live WorldSimulationSession with real Vulkan 1.2 attachment + Overlay Pass
    // ------------------------------------------------------------------------------------------------
    const SimulationSceneDocument sceneDoc = BuildLiveProofScene();
    const std::filesystem::path sceneTomlPath =
        std::filesystem::temp_directory_path() / "FrontierLiveVulkanProofScene.VolumetricSpace";
    const std::string tomlText = SimulationSceneCodec::EncodeToTomlText(sceneDoc);
    {
        std::ofstream outToml(sceneTomlPath, std::ios::binary | std::ios::trunc);
        if (outToml.good())
        {
            outToml.write(tomlText.data(), static_cast<std::streamsize>(tomlText.size()));
        }
    }

    WorldSimulationSession worldSession {};
    EditorSimulationAdapter editorAdapter {};
    T.Check(editorAdapter.AttachVulkanDevice(worldSession, &vkDevice, shaderDir),
            "EditorSimulationAdapter attached VulkanInterchange to WorldSimulationSession + SimulationViewportOverlayPass");
    T.Check(editorAdapter.QueryOverlayReady(), "SimulationViewportOverlayPass initialized with Fluid/Particle/Deformable pipelines");

    std::string startErr {};
    T.Check(worldSession.BeginPlayFromSceneFile(sceneTomlPath, &startErr),
            "WorldSimulationSession started from 5-domain .VolumetricSpace scene");

    // Upload hero deformable surface to the Vulkan overlay pass
    const auto* coord = worldSession.InspectCoordinator();
    T.Check(coord != nullptr, "SimulationCoordinatorExtension live after BeginPlay");

    if (coord != nullptr)
    {
        const auto& heroBody = coord->InspectDeformables().InspectHeroGpuBody();
        const auto cageSurf  = BuildTetCageSurface(heroBody);
        T.Check(editorAdapter.AccessOverlayPass().UploadDeformableSurfaceMesh(
                    cageSurf, std::max(1u, heroBody.PointCount), std::max(1u, heroBody.TetCount)),
                "Uploaded hero deformable cage-embedded surface for zero-readback GPU skinning");
    }

    // Tick 6 frames and capture early-frame GPU readback + Frame 06 PNG proof
    const std::array<float, 3> camEye { 0.15f, 0.45f, 6.4f };
    for (uint32_t i = 0u; i < 6u; ++i)
    {
        worldSession.Tick(1.0 / 60.0, camEye);
    }

    const auto routesEarly = worldSession.InspectExecutionRoutes();
    T.Check(routesEarly.FluidRoute == Fluid::FluidExecutionRoute::VulkanComputeGpu,
            "Fluid route is VulkanComputeGpu (not HeadlessCpuMirror)");
    T.Check(routesEarly.ParticleRoute == ParticleSpace::ParticleExecutionRoute::VulkanComputeGpu,
            "Particle route is VulkanComputeGpu (not HeadlessCpuMirror)");
    T.Check(routesEarly.DeformableRoute == Deformable::DeformableExecutionRoute::VulkanComputeGpu,
            "Deformable route is VulkanComputeGpu (not HeadlessCpuMirror)");
    T.Check(routesEarly.AllRealGpuDispatched,
            "WorldSimulationSession::InspectExecutionRoutes().AllRealGpuDispatched == true");

    SimulationGpuRenderFeedPacket feedEarly {};
    worldSession.PopulateGpuRenderFeed(feedEarly);
    T.Check(feedEarly.QueueSync.AllBackendsGpuResident,
            "SimulationGpuRenderFeedPacket reports AllBackendsGpuResident == true");
    T.Check(feedEarly.FluidVolume.StateViewHandle != 0ull && feedEarly.FluidVolume.VelocityViewHandle != 0ull &&
            feedEarly.ParticleCloud.StateBufferHandle != 0ull && feedEarly.HeroDeformable.PositionBufferHandle != 0ull,
            "All zero-copy GPU VkImageView / VkBuffer render-feed handles are non-null");

    // Helper: read back real GPU VRAM state across Fluid, Particles, and Deformables
    struct GpuStateSnapshot
    {
        float    FluidDensitySum  = 0.0f;
        float    FluidTempMax     = 0.0f;
        float    FluidVelMagMax   = 0.0f;
        bool     FluidAllFinite   = true;
        uint32_t PartNonZeroCount = 0u;
        float    PartPosChecksum  = 0.0f;
        bool     PartAllFinite    = true;
        uint32_t DefNonZeroCount  = 0u;
        float    DefPosChecksum   = 0.0f;
        bool     DefAllFinite     = true;
    };

    auto captureGpuReadback = [&]() -> GpuStateSnapshot
    {
        GpuStateSnapshot snap {};
        const auto* c = worldSession.InspectCoordinator();
        if (c == nullptr) return snap;

        // Fluid 3D RGBA16F state + velocity readback
        {
            const auto& fExec = c->InspectFluidGpuExecutor();
            const auto& stImg = fExec.InspectActiveStateImage();
            const auto& vlImg = fExec.InspectActiveVelocityImage();
            const uint64_t waitT = fExec.InspectTelemetry().LastSubmittedTimeline;
            std::vector<uint8_t> stBytes {}, vlBytes {};
            if (ReadbackImageBytes(vkDevice, stImg.ImageHandle, 24u, 24u, 24u, 8u, waitT, stBytes))
            {
                const size_t nVox = stBytes.size() / 8u;
                for (size_t i = 0u; i < nVox; ++i)
                {
                    uint16_t h[4] {};
                    std::memcpy(h, stBytes.data() + i * 8u, 8u);
                    const float d = HalfToFloat(h[0]);
                    const float t = HalfToFloat(h[1]);
                    if (!std::isfinite(d) || !std::isfinite(t)) snap.FluidAllFinite = false;
                    snap.FluidDensitySum += std::max(0.0f, d);
                    snap.FluidTempMax     = std::max(snap.FluidTempMax, t);
                }
            }
            if (ReadbackImageBytes(vkDevice, vlImg.ImageHandle, 24u, 24u, 24u, 8u, waitT, vlBytes))
            {
                const size_t nVox = vlBytes.size() / 8u;
                for (size_t i = 0u; i < nVox; ++i)
                {
                    uint16_t h[4] {};
                    std::memcpy(h, vlBytes.data() + i * 8u, 8u);
                    const float vx = HalfToFloat(h[0]);
                    const float vy = HalfToFloat(h[1]);
                    const float vz = HalfToFloat(h[2]);
                    if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) snap.FluidAllFinite = false;
                    snap.FluidVelMagMax = std::max(snap.FluidVelMagMax, std::sqrt(vx * vx + vy * vy + vz * vz));
                }
            }
        }

        // Particle GPU SSBO readback
        {
            const auto& pExec = c->InspectParticleGpuExecutor();
            const auto& stBuf = pExec.InspectActiveStateBuffer();
            const uint64_t waitT = pExec.InspectTelemetry().LastSubmittedTimeline;
            const uint32_t count = std::min(512u, pExec.InspectTelemetry().ActiveParticleCount);
            std::vector<uint8_t> pBytes {};
            if (count > 0u && ReadbackBufferBytes(vkDevice, stBuf.BufferHandle, static_cast<uint64_t>(count) * 32ull, waitT, pBytes))
            {
                for (uint32_t i = 0u; i < count; ++i)
                {
                    float v[8] {};
                    std::memcpy(v, pBytes.data() + i * 32u, 32u);
                    for (float f : v) if (!std::isfinite(f)) snap.PartAllFinite = false;
                    const float mag = std::fabs(v[0]) + std::fabs(v[1]) + std::fabs(v[2]);
                    if (mag > 1.0e-4f) ++snap.PartNonZeroCount;
                    snap.PartPosChecksum += v[0] * 0.37f + v[1] * 0.61f + v[2] * 0.23f;
                }
            }
        }

        // Deformable GPU SSBO readback
        {
            const auto& dExec  = c->InspectDeformables().InspectVulkanExecutor();
            const auto& posBuf = dExec.InspectPositionsBuffer();
            const uint64_t waitT = dExec.InspectTelemetry().LastSubmittedTimeline;
            const uint32_t pts   = c->InspectDeformables().InspectHeroGpuBody().PointCount;
            std::vector<uint8_t> dBytes {};
            if (pts > 0u && ReadbackBufferBytes(vkDevice, posBuf.BufferHandle, static_cast<uint64_t>(pts) * 16ull, waitT, dBytes))
            {
                for (uint32_t i = 0u; i < pts; ++i)
                {
                    float v[4] {};
                    std::memcpy(v, dBytes.data() + i * 16u, 16u);
                    for (float f : v) if (!std::isfinite(f)) snap.DefAllFinite = false;
                    const float mag = std::fabs(v[0]) + std::fabs(v[1]) + std::fabs(v[2]);
                    if (mag > 1.0e-4f) ++snap.DefNonZeroCount;
                    snap.DefPosChecksum += v[0] * 0.29f + v[1] * 0.71f + v[2] * 0.43f;
                }
            }
        }
        return snap;
    };

    const GpuStateSnapshot snapEarly = captureGpuReadback();

    // Render Frame 06 live Vulkan PNG screenshot
    SimulationOverlayCamera proofCam {};
    proofCam.EyeMeters       = camEye;
    proofCam.TargetMeters    = { 0.0f, -0.45f, 0.0f };
    proofCam.UpAxis          = { 0.0f, 1.0f, 0.0f };
    proofCam.VerticalFovRad  = 0.78539816f;

    std::vector<uint8_t> rgbaFrame06 {};
    uint32_t w06 = 0u, h06 = 0u;
    {
        SimulationViewportCompositeRequest req {};
        req.TargetWidthPx      = 640u;
        req.TargetHeightPx     = 360u;
        req.Camera             = proofCam;
        req.RenderFeed         = feedEarly;
        req.GroundHeightMeters = -1.78f;
        req.Exposure           = 1.35f;
        req.ParticleSizeMeters = 0.24f;
        req.ShowStrainHeatmap  = false;

        SimulationViewportCompositeResult res {};
        const bool drawn = editorAdapter.AccessOverlayPass().CompositeOntoGpuViewport(req, res);
        T.Check(drawn && res.FluidPassesDrawn == 1u && res.DeformablePassesDrawn == 1u && res.ParticlePassesDrawn == 1u,
                "Vulkan SimulationViewportOverlayPass drew Fluid + Deformable + Particles at Frame 06");
        T.Check(editorAdapter.AccessOverlayPass().ReadbackLastCompositeRgba8(rgbaFrame06, w06, h06),
                "Readback Frame 06 Vulkan composite RGBA8 succeeded");
        T.Check(WritePngRgba8(pngOutDir / "01-LiveVulkanAllDomains_Frame06.png", w06, h06, rgbaFrame06),
                "Exported Exhibits/Images/SimulationVulkan/01-LiveVulkanAllDomains_Frame06.png");
    }

    // Advance 12 more frames (total 18 ticks) and capture Frame 18 GPU readback + PNG proof
    for (uint32_t i = 0u; i < 12u; ++i)
    {
        worldSession.Tick(1.0 / 60.0, camEye);
    }

    const GpuStateSnapshot snapLate = captureGpuReadback();

    std::cout << "  [METRIC] Fluid GPU VRAM:   densitySum " << snapEarly.FluidDensitySum << " -> " << snapLate.FluidDensitySum
              << " | tempMax " << snapEarly.FluidTempMax << " -> " << snapLate.FluidTempMax
              << " | velMagMax " << snapEarly.FluidVelMagMax << " -> " << snapLate.FluidVelMagMax << "\n";
    std::cout << "  [METRIC] Particle GPU VRAM: nonZero " << snapEarly.PartNonZeroCount << " -> " << snapLate.PartNonZeroCount
              << " | checksum " << snapEarly.PartPosChecksum << " -> " << snapLate.PartPosChecksum << "\n";
    std::cout << "  [METRIC] Deformable VRAM:  nonZero " << snapEarly.DefNonZeroCount << " -> " << snapLate.DefNonZeroCount
              << " | checksum " << snapEarly.DefPosChecksum << " -> " << snapLate.DefPosChecksum << "\n";

    T.Check(snapEarly.FluidAllFinite && snapLate.FluidAllFinite && snapLate.FluidDensitySum > 1.0f &&
            snapLate.FluidTempMax > 0.1f && snapLate.FluidVelMagMax > 0.1f &&
            std::fabs(snapLate.FluidDensitySum - snapEarly.FluidDensitySum) > 1.0e-2f,
            "Fluid 3D GPU RGBA16F textures contain finite, non-zero, physically evolving density/temp/velocity");
    T.Check(snapEarly.PartAllFinite && snapLate.PartAllFinite && snapLate.PartNonZeroCount >= 64u &&
            std::fabs(snapLate.PartPosChecksum - snapEarly.PartPosChecksum) > 1.0e-2f,
            "Particle GPU SSBO contains finite, non-zero, physically evolving particle positions");
    T.Check(snapEarly.DefAllFinite && snapLate.DefAllFinite && snapLate.DefNonZeroCount >= 16u &&
            std::fabs(snapLate.DefPosChecksum - snapEarly.DefPosChecksum) > 1.0e-3f,
            "Deformable GPU SSBO contains finite, non-zero, physically evolving FEM/XPBD vertex positions");

    SimulationGpuRenderFeedPacket feedLate {};
    worldSession.PopulateGpuRenderFeed(feedLate);
    std::vector<uint8_t> rgbaFrame18 {};
    uint32_t w18 = 0u, h18 = 0u;
    {
        SimulationViewportCompositeRequest req {};
        req.TargetWidthPx      = 640u;
        req.TargetHeightPx     = 360u;
        req.Camera             = proofCam;
        req.RenderFeed         = feedLate;
        req.GroundHeightMeters = -1.78f;
        req.Exposure           = 1.35f;
        req.ParticleSizeMeters = 0.24f;
        req.ShowStrainHeatmap  = false;

        SimulationViewportCompositeResult res {};
        const bool drawn = editorAdapter.AccessOverlayPass().CompositeOntoGpuViewport(req, res);
        T.Check(drawn && res.FluidPassesDrawn == 1u && res.DeformablePassesDrawn == 1u && res.ParticlePassesDrawn == 1u,
                "Vulkan SimulationViewportOverlayPass drew Fluid + Deformable + Particles at Frame 18");
        T.Check(editorAdapter.AccessOverlayPass().ReadbackLastCompositeRgba8(rgbaFrame18, w18, h18),
                "Readback Frame 18 Vulkan composite RGBA8 succeeded");
        T.Check(WritePngRgba8(pngOutDir / "02-LiveVulkanAllDomains_Frame18.png", w18, h18, rgbaFrame18),
                "Exported Exhibits/Images/SimulationVulkan/02-LiveVulkanAllDomains_Frame18.png");
    }

    // Verify rendered Vulkan PNGs have non-trivial visual content and evolve between Frame 06 and Frame 18
    {
        uint64_t brightPixels18 = 0ull;
        uint64_t changedPixels  = 0ull;
        const size_t pxCount = static_cast<size_t>(w18) * h18;
        for (size_t i = 0u; i < pxCount && (i * 4u + 2u) < rgbaFrame06.size() && (i * 4u + 2u) < rgbaFrame18.size(); ++i)
        {
            const int r0 = rgbaFrame06[i * 4u + 0u], g0 = rgbaFrame06[i * 4u + 1u], b0 = rgbaFrame06[i * 4u + 2u];
            const int r1 = rgbaFrame18[i * 4u + 0u], g1 = rgbaFrame18[i * 4u + 1u], b1 = rgbaFrame18[i * 4u + 2u];
            if ((r1 + g1 + b1) > 180) ++brightPixels18;
            if (std::abs(r1 - r0) + std::abs(g1 - g0) + std::abs(b1 - b0) > 12) ++changedPixels;
        }
        std::cout << "  [METRIC] Rendered Vulkan PNG: brightPixels=" << brightPixels18
                  << " | changedPixels(F06->F18)=" << changedPixels << "\n";
        T.Check(brightPixels18 > 1500ull && changedPixels > 1000ull,
                "Rendered Vulkan PNG frames contain bright simulated visual content and evolve across frames");
    }

    // ------------------------------------------------------------------------------------------------
    // 3. Real Vulkan VertexMotionAtlasGpuBaker RGBA32F GPU bake + VRAM pixel readback verification
    // ------------------------------------------------------------------------------------------------
    {
        GeometrySpace::Deformation::VertexMotionAtlasGpuBaker vatBaker {};
        T.Check(vatBaker.AttachVulkanInterchange(&vkDevice), "VertexMotionAtlasGpuBaker attached VulkanInterchange");
        T.Check(vatBaker.Initialize(shaderDir), "VertexMotionAtlasGpuBaker initialized with DeformableVatBake.comp.spv");

        Deformable::DeformableBody vatBody {};
        Deformable::DeformableSimulationConfiguration vatCfg {};
        Deformable::DeformablePresetRegistry::BuildPreset(Deformable::DeformablePresetKind::SportTireFem, vatBody, vatCfg);
        const auto vatSurf = BuildTetCageSurface(vatBody);

        GeometrySpace::Deformation::VertexMotionAtlasBakeSettings bakeCfg {};
        bakeCfg.SliceCount = 4u;
        bakeCfg.SimStepsPerSlice = 1u;
        const auto bakedAtlas = vatBaker.Bake(vatBody, vatCfg, vatSurf, bakeCfg);
        const auto vatTel = vatBaker.InspectTelemetry();

        T.Check(bakedAtlas.Ready() && vatTel.Route == GeometrySpace::Deformation::VatBakeExecutionRoute::VulkanComputeGpu &&
                vatTel.RealGpuDispatched,
                "VertexMotionAtlasGpuBaker executed real VulkanComputeGpu dispatch (not HeadlessCpuMirror)");

        std::vector<uint8_t> vatPosBytes {}, vatNrmBytes {};
        const auto& posImg = vatBaker.InspectVatPosImage();
        const auto& nrmImg = vatBaker.InspectVatNrmImage();
        T.Check(ReadbackImageBytes(vkDevice, posImg.ImageHandle, bakedAtlas.AtlasWidth, bakedAtlas.AtlasHeight, 1u, 16u,
                                   vatTel.LastSubmittedTimeline, vatPosBytes),
                "Readback VAT Position RGBA32F image from GPU VRAM succeeded");
        T.Check(ReadbackImageBytes(vkDevice, nrmImg.ImageHandle, bakedAtlas.AtlasWidth, bakedAtlas.AtlasHeight, 1u, 16u,
                                   vatTel.LastSubmittedTimeline, vatNrmBytes),
                "Readback VAT Normal RGBA32F image from GPU VRAM succeeded");

        uint32_t nonZeroPos = 0u;
        uint32_t unitNrm    = 0u;
        bool vatFinite      = true;
        const uint32_t checkVerts = std::min(bakedAtlas.VertexCount, bakedAtlas.AtlasWidth * bakedAtlas.AtlasHeight);
        for (uint32_t v = 0u; v < checkVerts; ++v)
        {
            const uint32_t px = v & (bakedAtlas.AtlasWidth - 1u);
            const uint32_t py = v / bakedAtlas.AtlasWidth;
            const size_t off = (static_cast<size_t>(py) * bakedAtlas.AtlasWidth + px) * 16u;
            if (off + 16u > vatPosBytes.size() || off + 16u > vatNrmBytes.size()) break;
            float p[4] {}, n[4] {};
            std::memcpy(p, vatPosBytes.data() + off, 16u);
            std::memcpy(n, vatNrmBytes.data() + off, 16u);
            for (int k = 0; k < 4; ++k) if (!std::isfinite(p[k]) || !std::isfinite(n[k])) vatFinite = false;
            if (std::fabs(p[0]) + std::fabs(p[1]) + std::fabs(p[2]) > 1.0e-4f) ++nonZeroPos;
            const float nLen = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (nLen > 0.90f && nLen < 1.10f) ++unitNrm;
        }
        std::cout << "  [METRIC] VAT GPU RGBA32F: nonZeroPos=" << nonZeroPos << "/" << checkVerts
                  << " | unitNormals=" << unitNrm << "/" << checkVerts << "\n";
        T.Check(vatFinite && nonZeroPos >= checkVerts / 2u && unitNrm >= checkVerts / 2u,
                "VAT GPU RGBA32F pixels contain finite, non-zero baked positions and unit normals");
    }

    // ------------------------------------------------------------------------------------------------
    // 4. Editor Play / Pause / Step / Stop Toolbar Controls & ComponentSimulationPreviewDriver GPU check
    // ------------------------------------------------------------------------------------------------
    {
        const uint64_t ticksBeforePause = worldSession.InspectTelemetry().CompletedTicks;
        EditorSimulationToolbarState tb {};
        tb.PlayOrSimulateActive = true;
        tb.Paused               = true;
        tb.DeltaSeconds         = 1.0 / 60.0;
        (void)editorAdapter.SynchronizeToolbarControls(worldSession, tb, {});
        T.Check(worldSession.QueryStatus() == PreviewRunStatus::Paused,
                "EditorSimulationAdapter toolbar Pause transitions WorldSimulationSession to Paused");

        tb.StepRequested = true;
        const bool stepped = editorAdapter.SynchronizeToolbarControls(worldSession, tb, {});
        T.Check(stepped && worldSession.InspectTelemetry().CompletedTicks == ticksBeforePause + 1ull,
                "EditorSimulationAdapter toolbar Step advances WorldSimulationSession by exactly 1 tick while Paused");

        tb.PlayOrSimulateActive = false;
        tb.Paused               = false;
        tb.StepRequested        = false;
        (void)editorAdapter.SynchronizeToolbarControls(worldSession, tb, {});
        T.Check(worldSession.QueryStatus() == PreviewRunStatus::Idle,
                "EditorSimulationAdapter toolbar Stop returns WorldSimulationSession to Idle");
    }

    // ComponentSimulationPreviewDriver real Vulkan GPU check
    {
        ComponentClass::ComponentClassDefinition compDef {};
        compDef.Title = "LiveGpuPreviewClass";
        compDef.Storage.ResetWithSpatialOrigin("Root");
        const auto root = compDef.Storage.QueryOriginToken();
        (void)compDef.Storage.AppendInstance(ComponentClass::ComponentCategory::FluidRegionInstance, "FlameJet", root);
        (void)compDef.Storage.AppendInstance(ComponentClass::ComponentCategory::ParticleCloudInstance, "SparkStream", root);
        (void)compDef.Storage.AppendInstance(ComponentClass::ComponentCategory::SoftBodyInstance, "FemCarcass", root);

        ComponentSimulationPreviewDriver preview {};
        T.Check(preview.AttachVulkanInterchange(&vkDevice, shaderDir),
                "ComponentSimulationPreviewDriver attached real VulkanInterchange");
        std::string previewErr {};
        T.Check(preview.StartPreview(compDef, &previewErr),
                "ComponentSimulationPreviewDriver started live 5-domain preview");
        for (uint32_t i = 0u; i < 5u; ++i)
        {
            preview.StepPreview(1.0 / 60.0, camEye);
        }
        const auto prevRoutes = preview.InspectExecutionRoutes();
        T.Check(prevRoutes.AllRealGpuDispatched,
                "ComponentSimulationPreviewDriver::InspectExecutionRoutes().AllRealGpuDispatched == true");
        preview.StopAndResetPreview();
    }

    editorAdapter.Teardown();
    worldSession.Stop();
    vkDevice.TeardownInterchange();

    std::cout << "=== SimulationLiveVulkanValidation Summary: " << T.Passed << " passed, " << T.Failed << " failed ===\n";
    return (T.Failed == 0u) ? 0 : 1;
}
