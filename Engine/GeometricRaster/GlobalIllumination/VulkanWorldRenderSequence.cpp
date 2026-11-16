/*============================================================================================================================================
                                                        VULKANWORLDRENDERSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Implementation of VulkanWorldRenderSequence executing WorldViewportRadiometricCompute.comp.spv on hardware Vulkan GPUs

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier::GeometricRaster
{

namespace
{

struct GpuAreaQuadLightStd140
{
    float CenterHalfWidth[4] {};
    float RightHalfHeight[4]  {};
    float UpTwoSided[4]  {};
    float RadiancePacked[4] {};
};

struct GpuGlassSphereStd140
{
    float CenterRadiusPacked[4] {};
    float OpticsSlotPacked[4]   {};
};

struct WorldViewportUniformBlockStd140
{
    float                  CameraOriginTanX[4]    {};
    float                  CameraRightTanY[4]     {};
    float                  CameraUpWidth[4]       {};
    float                  CameraForwardHeight[4] {};
    float                  SunDirConeTan[4]       {};
    float                  SolarIrradianceTier[4] {};
    float                  SkyRadianceBvhCount[4] {};
    float                  SdfMinSpacing[4]       {};
    uint32_t               SdfDimsLightCount[4]   {};
    float                  LatticeMinGlassCount[4]  {};
    float                  LatticeMaxDebugMode[4] {};
    uint32_t               LatticeDimsMaterialCount[4] {};
    GpuAreaQuadLightStd140 AreaLights[4]             {};
    GpuGlassSphereStd140   GlassSpheres[4]           {};
};

static_assert(sizeof(WorldViewportUniformBlockStd140) == 576u, "WorldViewportUniformBlockStd140 must equal 576 bytes.");

struct SignedDistanceGridUniformStd140
{
    float    GridMinSpacing[4]   {};
    uint32_t GridDimsBvhCount[4] {};
    float    CutoffConfig[4]     {};
};

static_assert(sizeof(SignedDistanceGridUniformStd140) == 48u, "SignedDistanceGridUniformStd140 must equal 48 bytes.");

struct GpuShadedTriangle96
{
    float Corner0Pad[4]  {};
    float Edge01Pad[4]   {};
    float Edge02Pad[4]   {};
    float Normal0Pad[4]  {};
    float Normal1Pad[4]  {};
    float Normal2Slot[4] {};
};

static_assert(sizeof(GpuShadedTriangle96) == 96u, "GpuShadedTriangle96 must equal 96 bytes.");

struct GpuWorldMaterial64
{
    float DiffuseAlbedoRoughness[4] {};
    float SpecParams[4]            {};
    float EdgeTintCoatWeight[4] {};
    float CoatRoughnessOptics[4]    {};
};

static_assert(sizeof(GpuWorldMaterial64) == 64u, "GpuWorldMaterial64 must equal 64 bytes.");

struct QuantizedPositionKey
{
    int32_t  QX          = 0;
    int32_t  QY          = 0;
    int32_t  QZ          = 0;
    uint16_t MaterialOrd = 0u;

    [[nodiscard]] bool operator==(const QuantizedPositionKey& Rhs) const noexcept
    {
        return QX == Rhs.QX && QY == Rhs.QY && QZ == Rhs.QZ && MaterialOrd == Rhs.MaterialOrd;
    }

    [[nodiscard]] bool operator<(const QuantizedPositionKey& Rhs) const noexcept
    {
        if (QX != Rhs.QX) return QX < Rhs.QX;
        if (QY != Rhs.QY) return QY < Rhs.QY;
        if (QZ != Rhs.QZ) return QZ < Rhs.QZ;
        return MaterialOrd < Rhs.MaterialOrd;
    }
};

struct QuantizedCornerTriangleEntry
{
    QuantizedPositionKey Key      {};
    uint32_t             TriIndex = 0u;

    [[nodiscard]] bool operator<(const QuantizedCornerTriangleEntry& Rhs) const noexcept
    {
        if (Key == Rhs.Key)
        {
            return TriIndex < Rhs.TriIndex;
        }
        return Key < Rhs.Key;
    }
};

[[nodiscard]] QuantizedPositionKey QuantizeVertexPosition(Mathematics::Axis3D Pos, uint16_t MatOrd) noexcept
{
    return {
        static_cast<int32_t>(std::lround(Pos.X * 2048.0f)),
        static_cast<int32_t>(std::lround(Pos.Y * 2048.0f)),
        static_cast<int32_t>(std::lround(Pos.Z * 2048.0f)),
        MatOrd,
    };
}

[[nodiscard]] float ComputePyramidLocalHalfExtent(const SpatialAcceleration::ClusterPyramid* Pyramid) noexcept
{
    if (Pyramid == nullptr || Pyramid->Positions.empty())
    {
        return 0.40f;
    }
    float MaxRadius = 0.05f;
    for (const Mathematics::Axis3D& Pos : Pyramid->Positions)
    {
        const float Len = std::sqrt(Pos.X * Pos.X + Pos.Y * Pos.Y + Pos.Z * Pos.Z);
        MaxRadius       = std::max(MaxRadius, Len);
    }
    return MaxRadius;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

VulkanWorldRenderSequence::~VulkanWorldRenderSequence()
{
    TeardownSequence();
}

bool VulkanWorldRenderSequence::InitializeSequence(
    VulkanInterchange&           TargetDevice,
    const std::filesystem::path& CompiledShaderDirectory)
{
    TeardownSequence();
    if (!TargetDevice.Ready())
    {
        return false;
    }

    BoundDevice = &TargetDevice;
    if (!DispatchEncoder.InitializeEncoder(BoundDevice, 128u))
    {
        BoundDevice = nullptr;
        return false;
    }

    constexpr std::array<VkDescriptorType, 7u> SlotTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};

    const std::filesystem::path ShaderBinaryPath = CompiledShaderDirectory / "WorldViewportRadiometricCompute.comp.spv";
    RadiometricProgramHandle = DispatchEncoder.RegisterProgram(
        ShaderBinaryPath,
        SlotTypes,
        "ComputeEntry");
    if (RadiometricProgramHandle == UINT32_MAX)
    {
        const std::string PathText = ShaderBinaryPath.generic_string();
        std::fprintf(
            stderr,
            "[FrontierWorld] WARNING: Failed to load GPU compute shader %s — falling back to CPU GI.\n",
            PathText.c_str());
        TeardownSequence();
        return false;
    }

    constexpr std::array<VkDescriptorType, 4u> SdfSlotTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    const std::filesystem::path SdfShaderPath = CompiledShaderDirectory / "SignedDistanceGridCompute.comp.spv";
    SignedDistanceProgramHandle = DispatchEncoder.RegisterProgram(
        SdfShaderPath,
        SdfSlotTypes,
        "ComputeEntry");

    EnsureBufferCapacity(UniformBuffer, sizeof(WorldViewportUniformBlockStd140), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    EnsureBufferCapacity(SdfUniformBuffer, sizeof(SignedDistanceGridUniformStd140), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    if (UniformBuffer.BufferHandle != VK_NULL_HANDLE)
    {
        std::fprintf(
            stdout,
            "[FrontierWorld] VulkanWorldRenderSequence active on %.*s (Hardware GPU: %s, GPU SDF Compute: %s)\n",
            static_cast<int>(TargetDevice.QueryAdapterName().size()),
            TargetDevice.QueryAdapterName().data(),
            TargetDevice.QueryHardwareAccelerated() ? "YES" : "NO (Software ICD)",
            SignedDistanceProgramHandle != UINT32_MAX ? "YES" : "NO");
        std::fflush(stdout);
        return true;
    }
    return false;
}

void VulkanWorldRenderSequence::TeardownSequence() noexcept
{
    if (BoundDevice != nullptr && BoundDevice->Ready())
    {
        vkDeviceWaitIdle(BoundDevice->AccessDevice());
        BoundDevice->ReleaseBuffer(UniformBuffer);
        BoundDevice->ReleaseBuffer(SdfUniformBuffer);
        BoundDevice->ReleaseBuffer(BvhEntryBuffer);
        BoundDevice->ReleaseBuffer(ShadedTriangleBuffer);
        BoundDevice->ReleaseBuffer(MaterialEntryBuffer);
        BoundDevice->ReleaseBuffer(SignedDistanceBuffer);
        BoundDevice->ReleaseBuffer(IrradianceLatticeBuffer);
        BoundDevice->ReleaseBuffer(OutputPixelBuffer);
    }
    DispatchEncoder.TeardownEncoder();
    RadiometricProgramHandle    = UINT32_MAX;
    SignedDistanceProgramHandle = UINT32_MAX;
    BoundDevice                 = nullptr;
    SceneUploadedOnce           = false;
    UploadedBvhCount            = 0u;
    UploadedMaterialCount       = 0u;
    CachedSdfDimX               = 0u;
    CachedSdfDimY               = 0u;
    CachedSdfDimZ               = 0u;
    CachedLatticeDimX           = 0u;
    CachedLatticeDimY           = 0u;
    CachedLatticeDimZ           = 0u;
}

bool VulkanWorldRenderSequence::QueryReady() const noexcept
{
    return BoundDevice != nullptr && BoundDevice->Ready() && RadiometricProgramHandle != UINT32_MAX;
}

bool VulkanWorldRenderSequence::QueryHardwareGpuActive() const noexcept
{
    return BoundDevice != nullptr && BoundDevice->QueryHardwareAccelerated();
}

std::string_view VulkanWorldRenderSequence::QueryAdapterTitle() const noexcept
{
    return BoundDevice != nullptr ? BoundDevice->QueryAdapterName() : std::string_view{};
}

void VulkanWorldRenderSequence::EnsureBufferCapacity(
    DeviceBufferResource& TargetBuffer,
    uint64_t              MinimumBytes,
    VkBufferUsageFlags    UsageMask)
{
    const uint64_t SafeBytes = std::max<uint64_t>(64ull, MinimumBytes);
    if (TargetBuffer.BufferHandle != VK_NULL_HANDLE && TargetBuffer.ByteCapacity >= SafeBytes)
    {
        return;
    }
    if (BoundDevice == nullptr)
    {
        return;
    }
    BoundDevice->ReleaseBuffer(TargetBuffer);
    const uint64_t PaddedBytes = std::max<uint64_t>(SafeBytes, (SafeBytes * 5ull) / 4ull);
    TargetBuffer               = DispatchEncoder.AllocateHostWritableBuffer(PaddedBytes, UsageMask);
}

//------------------------------------------------------------------------------------------------------------------------
//                                               SCENE BUFFER UPLOAD
//------------------------------------------------------------------------------------------------------------------------

void VulkanWorldRenderSequence::UploadIrradianceLattice(const IrradianceLatticeSequence& RtxLattice)
{
    if (!QueryReady())
    {
        return;
    }

    const auto Cells = RtxLattice.InspectCells();
    if (Cells.empty())
    {
        CachedLatticeDimX = CachedLatticeDimY = CachedLatticeDimZ = 0u;
        const float ZeroVec[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
        EnsureBufferCapacity(IrradianceLatticeBuffer, sizeof(ZeroVec), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (IrradianceLatticeBuffer.HostAddress != nullptr)
        {
            std::memcpy(IrradianceLatticeBuffer.HostAddress, ZeroVec, sizeof(ZeroVec));
        }
        return;
    }

    const auto& Cfg   = RtxLattice.InspectConfig();
    CachedLatticeMin  = Cfg.GridMinimumWorld;
    CachedLatticeMax  = Cfg.GridMaximumWorld;
    CachedLatticeDimX = Cfg.CountX;
    CachedLatticeDimY = Cfg.CountY;
    CachedLatticeDimZ = Cfg.CountZ;

    std::vector<float> PackedTexels(Cells.size() * 64u * 4u, 0.0f);
    for (std::size_t CellIdx = 0u; CellIdx < Cells.size(); ++CellIdx)
    {
        for (std::size_t Tex = 0u; Tex < 64u; ++Tex)
        {
            const std::size_t TexelOffset  = (CellIdx * 64u + Tex) * 4u;
            const auto        Irr          = Cells[CellIdx].IrradianceOverPi[Tex];
            PackedTexels[TexelOffset + 0u] = Irr.R;
            PackedTexels[TexelOffset + 1u] = Irr.G;
            PackedTexels[TexelOffset + 2u] = Irr.B;
            PackedTexels[TexelOffset + 3u] = 1.0f;
        }
    }
    const uint64_t LatBytes = PackedTexels.size() * sizeof(float);
    EnsureBufferCapacity(IrradianceLatticeBuffer, LatBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (IrradianceLatticeBuffer.HostAddress != nullptr)
    {
        std::memcpy(IrradianceLatticeBuffer.HostAddress, PackedTexels.data(), LatBytes);
    }
}

bool VulkanWorldRenderSequence::DispatchGpuSignedDistanceGrid(const WorldSceneSpecification& Scene)
{
    if (!QueryReady() || SignedDistanceProgramHandle == UINT32_MAX || UploadedBvhCount == 0u ||
        SdfUniformBuffer.HostAddress == nullptr)
    {
        return false;
    }

    constexpr float PaddingMeters = 0.45f;
    const Mathematics::Axis3D PadVec { PaddingMeters, PaddingMeters, PaddingMeters };
    const Mathematics::Axis3D BoundsMin = Scene.SceneTree.RootBoundsMin - PadVec;
    const Mathematics::Axis3D BoundsMax = Scene.SceneTree.RootBoundsMax + PadVec;
    const Mathematics::Axis3D Span      = BoundsMax - BoundsMin;
    if (Span.X <= 1.0e-4f || Span.Y <= 1.0e-4f || Span.Z <= 1.0e-4f)
    {
        return false;
    }

    constexpr uint32_t TargetX = 44u;
    constexpr uint32_t TargetY = 40u;
    constexpr uint32_t TargetZ = 24u;
    const float StepX = Span.X / static_cast<float>(TargetX - 1u);
    const float StepY = Span.Y / static_cast<float>(TargetY - 1u);
    const float StepZ = Span.Z / static_cast<float>(TargetZ - 1u);

    CachedSdfSpacing   = std::max({ StepX, StepY, StepZ, 0.05f });
    CachedSdfMinCorner = BoundsMin;
    CachedSdfDimX      = static_cast<uint32_t>(std::max(2.0f, std::floor(Span.X / CachedSdfSpacing) + 1.0f));
    CachedSdfDimY      = static_cast<uint32_t>(std::max(2.0f, std::floor(Span.Y / CachedSdfSpacing) + 1.0f));
    CachedSdfDimZ      = static_cast<uint32_t>(std::max(2.0f, std::floor(Span.Z / CachedSdfSpacing) + 1.0f));

    const uint64_t TotalVoxels = static_cast<uint64_t>(CachedSdfDimX) * CachedSdfDimY * CachedSdfDimZ;
    const uint64_t SdfBytes    = TotalVoxels * sizeof(float);
    EnsureBufferCapacity(SignedDistanceBuffer, SdfBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (SignedDistanceBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return false;
    }

    SignedDistanceGridUniformStd140 Uniforms {};
    Uniforms.GridMinSpacing[0]   = CachedSdfMinCorner.X;
    Uniforms.GridMinSpacing[1]   = CachedSdfMinCorner.Y;
    Uniforms.GridMinSpacing[2]   = CachedSdfMinCorner.Z;
    Uniforms.GridMinSpacing[3]   = CachedSdfSpacing;
    Uniforms.GridDimsBvhCount[0] = CachedSdfDimX;
    Uniforms.GridDimsBvhCount[1] = CachedSdfDimY;
    Uniforms.GridDimsBvhCount[2] = CachedSdfDimZ;
    Uniforms.GridDimsBvhCount[3] = UploadedBvhCount;
    Uniforms.CutoffConfig[0]     = 1.80f;
    std::memcpy(SdfUniformBuffer.HostAddress, &Uniforms, sizeof(Uniforms));

    DispatchEncoder.ResetTransientDescriptors();
    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return false;
    }

    const std::array<ComputeSlotDescriptor, 4u> Slots {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SdfUniformBuffer.BufferHandle,     sizeof(SignedDistanceGridUniformStd140) },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BvhEntryBuffer.BufferHandle,       VK_WHOLE_SIZE },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ShadedTriangleBuffer.BufferHandle, VK_WHOLE_SIZE },
        { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SignedDistanceBuffer.BufferHandle, SdfBytes      },
    }};

    const uint32_t GroupsX = (CachedSdfDimX + 3u) / 4u;
    const uint32_t GroupsY = (CachedSdfDimY + 3u) / 4u;
    const uint32_t GroupsZ = (CachedSdfDimZ + 3u) / 4u;
    if (!DispatchEncoder.EncodeComputeDispatch(Cmd, SignedDistanceProgramHandle, Slots, GroupsX, GroupsY, GroupsZ))
    {
        return false;
    }

    VkMemoryBarrier ComputeBarrier {};
    ComputeBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    ComputeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ComputeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        Cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0u,
        1u,
        &ComputeBarrier,
        0u,
        nullptr,
        0u,
        nullptr);

    const uint64_t SyncPoint = BoundDevice->DispatchCommandBufferAsync(Cmd);
    return BoundDevice->AwaitTimelineTick(SyncPoint);
}

void VulkanWorldRenderSequence::SynchronizeSceneResources(
    const WorldSceneSpecification&   Scene,
    const IrradianceLatticeSequence* OptionalRtxLattice,
    bool                             RebuildSdfGrid)
{
    if (!QueryReady())
    {
        return;
    }

    // 1. Upload 128-byte 4-Wide BVH4 entries
    if (!Scene.SceneTree.WideEntries.empty())
    {
        const uint64_t BvhBytes = Scene.SceneTree.WideEntries.size() * sizeof(SpatialAcceleration::WideTreeEntry);
        EnsureBufferCapacity(BvhEntryBuffer, BvhBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (BvhEntryBuffer.HostAddress != nullptr)
        {
            std::memcpy(BvhEntryBuffer.HostAddress, Scene.SceneTree.WideEntries.data(), BvhBytes);
        }
        UploadedBvhCount = static_cast<uint32_t>(Scene.SceneTree.WideEntries.size());
    }
    else
    {
        SpatialAcceleration::WideTreeEntry DummyEntry {};
        EnsureBufferCapacity(BvhEntryBuffer, sizeof(DummyEntry), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (BvhEntryBuffer.HostAddress != nullptr)
        {
            std::memcpy(BvhEntryBuffer.HostAddress, &DummyEntry, sizeof(DummyEntry));
        }
        UploadedBvhCount = 0u;
    }

    // 2. Upload 96-byte shaded triangles — O(N) fast path when precomputed per-object smooth normals are present!
    const auto& SrcPrims   = Scene.SceneTree.Primitives;
    const auto& SrcNormals = Scene.SceneTree.PrimitiveNormals;
    std::vector<GpuShadedTriangle96> ShadedTris(std::max<std::size_t>(1u, SrcPrims.size()));
    if (!SrcPrims.empty())
    {
        if (SrcNormals.size() == SrcPrims.size())
        {
            for (std::size_t Idx = 0u; Idx < SrcPrims.size(); ++Idx)
            {
                const auto& Prim   = SrcPrims[Idx];
                const auto& Smooth = SrcNormals[Idx];

                GpuShadedTriangle96 OutTri {};
                OutTri.Corner0Pad[0]  = Prim.Corner0.X;
                OutTri.Corner0Pad[1]  = Prim.Corner0.Y;
                OutTri.Corner0Pad[2]  = Prim.Corner0.Z;
                OutTri.Edge01Pad[0]   = Prim.Edge01.X;
                OutTri.Edge01Pad[1]   = Prim.Edge01.Y;
                OutTri.Edge01Pad[2]   = Prim.Edge01.Z;
                OutTri.Edge02Pad[0]   = Prim.Edge02.X;
                OutTri.Edge02Pad[1]   = Prim.Edge02.Y;
                OutTri.Edge02Pad[2]   = Prim.Edge02.Z;
                OutTri.Normal0Pad[0]  = Smooth.Normal0.X;
                OutTri.Normal0Pad[1]  = Smooth.Normal0.Y;
                OutTri.Normal0Pad[2]  = Smooth.Normal0.Z;
                OutTri.Normal1Pad[0]  = Smooth.Normal1.X;
                OutTri.Normal1Pad[1]  = Smooth.Normal1.Y;
                OutTri.Normal1Pad[2]  = Smooth.Normal1.Z;
                OutTri.Normal2Slot[0] = Smooth.Normal2.X;
                OutTri.Normal2Slot[1] = Smooth.Normal2.Y;
                OutTri.Normal2Slot[2] = Smooth.Normal2.Z;
                OutTri.Normal2Slot[3] = static_cast<float>(Prim.MaterialOrdinal);
                ShadedTris[Idx]       = OutTri;
            }
        }
        else
        {
            std::vector<Mathematics::Axis3D>          AreaWeightedNormals(SrcPrims.size());
            std::vector<QuantizedCornerTriangleEntry> SharedCornerEntries;
            SharedCornerEntries.reserve(SrcPrims.size() * 3u);

            for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(SrcPrims.size()); ++Idx)
            {
                const auto& Prim            = SrcPrims[Idx];
                AreaWeightedNormals[Idx]    = Mathematics::EvaluateCross(Prim.Edge01, Prim.Edge02);
                const Mathematics::Axis3D C0 = Prim.Corner0;
                const Mathematics::Axis3D C1 = Prim.Corner0 + Prim.Edge01;
                const Mathematics::Axis3D C2 = Prim.Corner0 + Prim.Edge02;
                SharedCornerEntries.push_back({ QuantizeVertexPosition(C0, Prim.MaterialOrdinal), Idx });
                SharedCornerEntries.push_back({ QuantizeVertexPosition(C1, Prim.MaterialOrdinal), Idx });
                SharedCornerEntries.push_back({ QuantizeVertexPosition(C2, Prim.MaterialOrdinal), Idx });
            }
            std::sort(SharedCornerEntries.begin(), SharedCornerEntries.end());

            for (std::size_t Idx = 0u; Idx < SrcPrims.size(); ++Idx)
            {
                const auto& Prim = SrcPrims[Idx];
                const Mathematics::Axis3D UnitFaceN = Mathematics::NormalizeAxis(AreaWeightedNormals[Idx], { 0.0f, 0.0f, 1.0f });
                const std::array<Mathematics::Axis3D, 3u> Corners {{
                    Prim.Corner0,
                    Prim.Corner0 + Prim.Edge01,
                    Prim.Corner0 + Prim.Edge02,
                }};

                std::array<Mathematics::Axis3D, 3u> SmoothCorners {};
                for (std::size_t CornerIdx = 0u; CornerIdx < 3u; ++CornerIdx)
                {
                    Mathematics::Axis3D SumNormal { 0.0f, 0.0f, 0.0f };
                    const QuantizedCornerTriangleEntry SearchCorner { QuantizeVertexPosition(Corners[CornerIdx], Prim.MaterialOrdinal), 0u };
                    auto Iter = std::lower_bound(
                        SharedCornerEntries.begin(),
                        SharedCornerEntries.end(),
                        SearchCorner,
                        [](const QuantizedCornerTriangleEntry& Lhs, const QuantizedCornerTriangleEntry& Rhs) noexcept
                        {
                            return Lhs.Key < Rhs.Key;
                        });
                    while (Iter != SharedCornerEntries.end() && Iter->Key == SearchCorner.Key)
                    {
                        const uint32_t PeerTriIdx = Iter->TriIndex;
                        const Mathematics::Axis3D PeerUnit = Mathematics::NormalizeAxis(AreaWeightedNormals[PeerTriIdx], UnitFaceN);
                        if (Mathematics::EvaluateDot(UnitFaceN, PeerUnit) > 0.65f)
                        {
                            SumNormal = SumNormal + AreaWeightedNormals[PeerTriIdx];
                        }
                        ++Iter;
                    }
                    SmoothCorners[CornerIdx] = Mathematics::NormalizeAxis(SumNormal, UnitFaceN);
                }

                GpuShadedTriangle96 OutTri {};
                OutTri.Corner0Pad[0]  = Prim.Corner0.X;
                OutTri.Corner0Pad[1]  = Prim.Corner0.Y;
                OutTri.Corner0Pad[2]  = Prim.Corner0.Z;
                OutTri.Edge01Pad[0]   = Prim.Edge01.X;
                OutTri.Edge01Pad[1]   = Prim.Edge01.Y;
                OutTri.Edge01Pad[2]   = Prim.Edge01.Z;
                OutTri.Edge02Pad[0]   = Prim.Edge02.X;
                OutTri.Edge02Pad[1]   = Prim.Edge02.Y;
                OutTri.Edge02Pad[2]   = Prim.Edge02.Z;
                OutTri.Normal0Pad[0]  = SmoothCorners[0u].X;
                OutTri.Normal0Pad[1]  = SmoothCorners[0u].Y;
                OutTri.Normal0Pad[2]  = SmoothCorners[0u].Z;
                OutTri.Normal1Pad[0]  = SmoothCorners[1u].X;
                OutTri.Normal1Pad[1]  = SmoothCorners[1u].Y;
                OutTri.Normal1Pad[2]  = SmoothCorners[1u].Z;
                OutTri.Normal2Slot[0] = SmoothCorners[2u].X;
                OutTri.Normal2Slot[1] = SmoothCorners[2u].Y;
                OutTri.Normal2Slot[2] = SmoothCorners[2u].Z;
                OutTri.Normal2Slot[3] = static_cast<float>(Prim.MaterialOrdinal);
                ShadedTris[Idx]       = OutTri;
            }
        }
    }
    const uint64_t TriBytes = ShadedTris.size() * sizeof(GpuShadedTriangle96);
    EnsureBufferCapacity(ShadedTriangleBuffer, TriBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (ShadedTriangleBuffer.HostAddress != nullptr)
    {
        std::memcpy(ShadedTriangleBuffer.HostAddress, ShadedTris.data(), TriBytes);
    }

    // 3. Upload 64-byte OpenPBR surface material entries
    const uint32_t MatCount = std::max(1u, Scene.MaterialRegister.QuerySlotCount());
    std::vector<GpuWorldMaterial64> GpuMats(MatCount);
    for (uint32_t Slot = 0u; Slot < Scene.MaterialRegister.QuerySlotCount(); ++Slot)
    {
        const auto& Src = Scene.MaterialRegister.InspectAuthoredMaterial(static_cast<uint16_t>(Slot));
        GpuWorldMaterial64 Dst {};
        Dst.DiffuseAlbedoRoughness[0] = Src.Diffuse.BaseColor.R * Src.Diffuse.BaseWeight;
        Dst.DiffuseAlbedoRoughness[1] = Src.Diffuse.BaseColor.G * Src.Diffuse.BaseWeight;
        Dst.DiffuseAlbedoRoughness[2] = Src.Diffuse.BaseColor.B * Src.Diffuse.BaseWeight;
        Dst.DiffuseAlbedoRoughness[3] = Src.Diffuse.DiffuseRoughness;
        Dst.SpecParams[0]            = Src.Specular.Roughness;
        Dst.SpecParams[1]            = Src.Specular.Metalness;
        Dst.SpecParams[2]            = Src.Specular.DielectricIOR;
        Dst.SpecParams[3]            = Src.Specular.Anisotropy;
        Dst.EdgeTintCoatWeight[0] = Src.Specular.EdgeTintF82.R;
        Dst.EdgeTintCoatWeight[1] = Src.Specular.EdgeTintF82.G;
        Dst.EdgeTintCoatWeight[2] = Src.Specular.EdgeTintF82.B;
        Dst.EdgeTintCoatWeight[3] = Src.Clearcoat.CoatWeight;
        Dst.CoatRoughnessOptics[0]    = Src.Clearcoat.CoatRoughness;
        Dst.CoatRoughnessOptics[1]    = Src.Transmission.TransmissionWeight;
        Dst.CoatRoughnessOptics[2]    = Src.Transmission.AbbeDispersionNumber;
        Dst.CoatRoughnessOptics[3]    = 0.0f;
        GpuMats[Slot]                = Dst;
    }
    const uint64_t MatBytes = GpuMats.size() * sizeof(GpuWorldMaterial64);
    EnsureBufferCapacity(MaterialEntryBuffer, MatBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (MaterialEntryBuffer.HostAddress != nullptr)
    {
        std::memcpy(MaterialEntryBuffer.HostAddress, GpuMats.data(), MatBytes);
    }
    UploadedMaterialCount = MatCount;

    // 4. Generate dense 3D SDF float grid on GPU compute shader (falling back to CPU SceneSdf only if compute shader unavailable)
    if (RebuildSdfGrid || !SceneUploadedOnce || CachedSdfDimX == 0u)
    {
        const bool BuiltOnGpu = DispatchGpuSignedDistanceGrid(Scene);
        if (!BuiltOnGpu)
        {
            if (Scene.SceneSdf.BrickCountX > 0u && Scene.SceneSdf.BrickCountY > 0u && Scene.SceneSdf.BrickCountZ > 0u)
            {
                const Mathematics::Axis3D Span = Scene.SceneSdf.VolumeBoundsMax - Scene.SceneSdf.VolumeBoundsMin;
                constexpr uint32_t TargetX = 44u;
                constexpr uint32_t TargetY = 40u;
                constexpr uint32_t TargetZ = 24u;
                const float StepX = Span.X / static_cast<float>(TargetX - 1u);
                const float StepY = Span.Y / static_cast<float>(TargetY - 1u);
                const float StepZ = Span.Z / static_cast<float>(TargetZ - 1u);
                CachedSdfSpacing   = std::max({ StepX, StepY, StepZ, 0.05f });
                CachedSdfMinCorner = Scene.SceneSdf.VolumeBoundsMin;
                CachedSdfDimX      = static_cast<uint32_t>(std::max(2.0f, std::floor(Span.X / CachedSdfSpacing) + 1.0f));
                CachedSdfDimY      = static_cast<uint32_t>(std::max(2.0f, std::floor(Span.Y / CachedSdfSpacing) + 1.0f));
                CachedSdfDimZ      = static_cast<uint32_t>(std::max(2.0f, std::floor(Span.Z / CachedSdfSpacing) + 1.0f));

                const std::size_t TotalVoxels = static_cast<std::size_t>(CachedSdfDimX) * CachedSdfDimY * CachedSdfDimZ;
                std::vector<float> DenseSdf(TotalVoxels, 2.5f);
                for (uint32_t IZ = 0u; IZ < CachedSdfDimZ; ++IZ)
                {
                    const float WZ = CachedSdfMinCorner.Z + static_cast<float>(IZ) * CachedSdfSpacing;
                    for (uint32_t IY = 0u; IY < CachedSdfDimY; ++IY)
                    {
                        const float WY = CachedSdfMinCorner.Y + static_cast<float>(IY) * CachedSdfSpacing;
                        const std::size_t RowOffset = (static_cast<std::size_t>(IZ) * CachedSdfDimY + IY) * CachedSdfDimX;
                        for (uint32_t IX = 0u; IX < CachedSdfDimX; ++IX)
                        {
                            const float WX = CachedSdfMinCorner.X + static_cast<float>(IX) * CachedSdfSpacing;
                            DenseSdf[RowOffset + IX] = Scene.SceneSdf.SampleSignedDistance({ WX, WY, WZ });
                        }
                    }
                }
                const uint64_t SdfBytes = DenseSdf.size() * sizeof(float);
                EnsureBufferCapacity(SignedDistanceBuffer, SdfBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                if (SignedDistanceBuffer.HostAddress != nullptr)
                {
                    std::memcpy(SignedDistanceBuffer.HostAddress, DenseSdf.data(), SdfBytes);
                }
            }
            else
            {
                CachedSdfDimX = CachedSdfDimY = CachedSdfDimZ = 0u;
                const float FallbackDist = 4.0f;
                EnsureBufferCapacity(SignedDistanceBuffer, sizeof(float) * 4u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                if (SignedDistanceBuffer.HostAddress != nullptr)
                {
                    std::memcpy(SignedDistanceBuffer.HostAddress, &FallbackDist, sizeof(float));
                }
            }
        }
    }

    // 5. Upload Tier 2 RTX Irradiance Lattice independently of RebuildSdfGrid (never skipped when baked lattice arrives!)
    if (OptionalRtxLattice != nullptr && !OptionalRtxLattice->InspectCells().empty())
    {
        UploadIrradianceLattice(*OptionalRtxLattice);
    }
    else if (IrradianceLatticeBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        CachedLatticeDimX = CachedLatticeDimY = CachedLatticeDimZ = 0u;
        const float ZeroVec[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
        EnsureBufferCapacity(IrradianceLatticeBuffer, sizeof(ZeroVec), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (IrradianceLatticeBuffer.HostAddress != nullptr)
        {
            std::memcpy(IrradianceLatticeBuffer.HostAddress, ZeroVec, sizeof(ZeroVec));
        }
    }

    SceneUploadedOnce = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              GPU COMPUTE DISPATCH
//------------------------------------------------------------------------------------------------------------------------

bool VulkanWorldRenderSequence::RenderWorldViewportRgba8(
    WorldRenderTier                Tier,
    const PerspectiveCameraFrame&  Camera,
    const WorldSceneSpecification& Scene,
    uint32_t                       DebugChannel,
    std::vector<uint8_t>&          OutDisplayRgba8)
{
    if (!QueryReady())
    {
        return false;
    }

    if (!SceneUploadedOnce)
    {
        SynchronizeSceneResources(Scene, nullptr, true);
    }

    const auto StartClock = std::chrono::steady_clock::now();

    const uint32_t WidthPx  = std::max(1u, Camera.ViewportWidthPx);
    const uint32_t HeightPx = std::max(1u, Camera.ViewportHeightPx);
    const uint64_t OutBytes = static_cast<uint64_t>(WidthPx) * HeightPx * 4ull;
    EnsureBufferCapacity(OutputPixelBuffer, OutBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (OutputPixelBuffer.HostAddress == nullptr)
    {
        return false;
    }

    const OrthonormalCameraBasis Basis = ComputeCameraBasis(Camera);

    WorldViewportUniformBlockStd140 Uniforms {};
    Uniforms.CameraOriginTanX[0]    = Camera.CameraOrigin.X;
    Uniforms.CameraOriginTanX[1]    = Camera.CameraOrigin.Y;
    Uniforms.CameraOriginTanX[2]    = Camera.CameraOrigin.Z;
    Uniforms.CameraOriginTanX[3]    = Basis.TanHalfFovX;

    Uniforms.CameraRightTanY[0]     = Basis.RightAxis.X;
    Uniforms.CameraRightTanY[1]     = Basis.RightAxis.Y;
    Uniforms.CameraRightTanY[2]     = Basis.RightAxis.Z;
    Uniforms.CameraRightTanY[3]     = Basis.TanHalfFovY;

    Uniforms.CameraUpWidth[0]       = Basis.UpAxis.X;
    Uniforms.CameraUpWidth[1]       = Basis.UpAxis.Y;
    Uniforms.CameraUpWidth[2]       = Basis.UpAxis.Z;
    Uniforms.CameraUpWidth[3]       = static_cast<float>(WidthPx);

    Uniforms.CameraForwardHeight[0] = Basis.ForwardAxis.X;
    Uniforms.CameraForwardHeight[1] = Basis.ForwardAxis.Y;
    Uniforms.CameraForwardHeight[2] = Basis.ForwardAxis.Z;
    Uniforms.CameraForwardHeight[3] = static_cast<float>(HeightPx);

    const Mathematics::Axis3D SunDir   = Mathematics::NormalizeAxis(Scene.Sun.DirectionToSun, { 0.42f, -0.55f, 0.72f });
    Uniforms.SunDirConeTan[0]       = SunDir.X;
    Uniforms.SunDirConeTan[1]       = SunDir.Y;
    Uniforms.SunDirConeTan[2]       = SunDir.Z;
    Uniforms.SunDirConeTan[3]       = std::tan(std::max(0.004f, Scene.Sun.SunAngularRadiusRadians));

    Uniforms.SolarIrradianceTier[0] = Scene.Sun.SolarIrradiance.R;
    Uniforms.SolarIrradianceTier[1] = Scene.Sun.SolarIrradiance.G;
    Uniforms.SolarIrradianceTier[2] = Scene.Sun.SolarIrradiance.B;
    Uniforms.SolarIrradianceTier[3] = (Tier == WorldRenderTier::Tier2RtxDeterministic && CachedLatticeDimX >= 2u) ? 1.0f : 0.0f;

    Uniforms.SkyRadianceBvhCount[0] = Scene.SkyRadiance.R;
    Uniforms.SkyRadianceBvhCount[1] = Scene.SkyRadiance.G;
    Uniforms.SkyRadianceBvhCount[2] = Scene.SkyRadiance.B;
    Uniforms.SkyRadianceBvhCount[3] = static_cast<float>(UploadedBvhCount);

    Uniforms.SdfMinSpacing[0]       = CachedSdfMinCorner.X;
    Uniforms.SdfMinSpacing[1]       = CachedSdfMinCorner.Y;
    Uniforms.SdfMinSpacing[2]       = CachedSdfMinCorner.Z;
    Uniforms.SdfMinSpacing[3]       = CachedSdfSpacing;

    const uint32_t LightCount = static_cast<uint32_t>(std::min<std::size_t>(4u, Scene.AreaLights.size()));
    Uniforms.SdfDimsLightCount[0]   = CachedSdfDimX;
    Uniforms.SdfDimsLightCount[1]   = CachedSdfDimY;
    Uniforms.SdfDimsLightCount[2]   = CachedSdfDimZ;
    Uniforms.SdfDimsLightCount[3]   = LightCount;

    for (uint32_t Idx = 0u; Idx < LightCount; ++Idx)
    {
        const auto& Src = Scene.AreaLights[Idx];
        auto&       Dst = Uniforms.AreaLights[Idx];
        Dst.CenterHalfWidth[0] = Src.CenterWorld.X;
        Dst.CenterHalfWidth[1] = Src.CenterWorld.Y;
        Dst.CenterHalfWidth[2] = Src.CenterWorld.Z;
        Dst.CenterHalfWidth[3] = Src.HalfWidthMeters;
        Dst.RightHalfHeight[0]  = Src.RightTangent.X;
        Dst.RightHalfHeight[1]  = Src.RightTangent.Y;
        Dst.RightHalfHeight[2]  = Src.RightTangent.Z;
        Dst.RightHalfHeight[3]  = Src.HalfHeightMeters;
        Dst.UpTwoSided[0]  = Src.UpBitangent.X;
        Dst.UpTwoSided[1]  = Src.UpBitangent.Y;
        Dst.UpTwoSided[2]  = Src.UpBitangent.Z;
        Dst.UpTwoSided[3]  = Src.TwoSidedEmission ? 1.0f : 0.0f;
        Dst.RadiancePacked[0] = Src.EmittedRadiance.R;
        Dst.RadiancePacked[1] = Src.EmittedRadiance.G;
        Dst.RadiancePacked[2] = Src.EmittedRadiance.B;
    }

    uint32_t GlassCount = 0u;
    for (const RigidClusterInstance& Inst : Scene.ClusterInstances)
    {
        if (!Inst.TransparentGlass || Inst.UniformScale <= 1.0e-4f || GlassCount >= 4u)
        {
            continue;
        }
        const float LocalRadius = ComputePyramidLocalHalfExtent(Inst.PyramidAsset);
        const auto& GlassMat    = Scene.MaterialRegister.InspectAuthoredMaterial(Inst.MaterialSlot);
        auto&       Dst         = Uniforms.GlassSpheres[GlassCount++];
        Dst.CenterRadiusPacked[0]  = Inst.WorldTranslation.X;
        Dst.CenterRadiusPacked[1]  = Inst.WorldTranslation.Y;
        Dst.CenterRadiusPacked[2]  = Inst.WorldTranslation.Z;
        Dst.CenterRadiusPacked[3]  = std::max(0.04f, LocalRadius * Inst.UniformScale);
        Dst.OpticsSlotPacked[0]    = GlassMat.Specular.DielectricIOR;
        Dst.OpticsSlotPacked[1]    = GlassMat.Transmission.AbbeDispersionNumber;
        Dst.OpticsSlotPacked[2]    = static_cast<float>(Inst.MaterialSlot);
        Dst.OpticsSlotPacked[3]    = GlassMat.Specular.Roughness;
    }

    Uniforms.LatticeMinGlassCount[0]  = CachedLatticeMin.X;
    Uniforms.LatticeMinGlassCount[1]  = CachedLatticeMin.Y;
    Uniforms.LatticeMinGlassCount[2]  = CachedLatticeMin.Z;
    Uniforms.LatticeMinGlassCount[3]  = static_cast<float>(GlassCount);

    Uniforms.LatticeMaxDebugMode[0] = CachedLatticeMax.X;
    Uniforms.LatticeMaxDebugMode[1] = CachedLatticeMax.Y;
    Uniforms.LatticeMaxDebugMode[2] = CachedLatticeMax.Z;
    Uniforms.LatticeMaxDebugMode[3] = static_cast<float>(DebugChannel);

    Uniforms.LatticeDimsMaterialCount[0] = CachedLatticeDimX;
    Uniforms.LatticeDimsMaterialCount[1] = CachedLatticeDimY;
    Uniforms.LatticeDimsMaterialCount[2] = CachedLatticeDimZ;
    Uniforms.LatticeDimsMaterialCount[3] = UploadedMaterialCount;

    std::memcpy(UniformBuffer.HostAddress, &Uniforms, sizeof(Uniforms));

    DispatchEncoder.ResetTransientDescriptors();
    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return false;
    }

    const std::array<ComputeSlotDescriptor, 7u> Slots {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, UniformBuffer.BufferHandle,           sizeof(WorldViewportUniformBlockStd140) },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BvhEntryBuffer.BufferHandle,          VK_WHOLE_SIZE },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ShadedTriangleBuffer.BufferHandle,    VK_WHOLE_SIZE },
        { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MaterialEntryBuffer.BufferHandle,     VK_WHOLE_SIZE },
        { 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SignedDistanceBuffer.BufferHandle,    VK_WHOLE_SIZE },
        { 5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, IrradianceLatticeBuffer.BufferHandle, VK_WHOLE_SIZE },
        { 6u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, OutputPixelBuffer.BufferHandle,       OutBytes      },
    }};

    const uint32_t GroupsX = (WidthPx  + 7u) / 8u;
    const uint32_t GroupsY = (HeightPx + 7u) / 8u;
    if (!DispatchEncoder.EncodeComputeDispatch(Cmd, RadiometricProgramHandle, Slots, GroupsX, GroupsY, 1u))
    {
        return false;
    }

    VkMemoryBarrier HostReadBarrier {};
    HostReadBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    HostReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    HostReadBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(
        Cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0u,
        1u,
        &HostReadBarrier,
        0u,
        nullptr,
        0u,
        nullptr);

    const uint64_t SyncPoint = BoundDevice->DispatchCommandBufferAsync(Cmd);
    if (!BoundDevice->AwaitTimelineTick(SyncPoint))
    {
        return false;
    }

    OutDisplayRgba8.resize(static_cast<std::size_t>(OutBytes));
    std::memcpy(OutDisplayRgba8.data(), OutputPixelBuffer.HostAddress, static_cast<std::size_t>(OutBytes));

    const auto EndClock      = std::chrono::steady_clock::now();
    LastDispatchMilliseconds = static_cast<float>(std::chrono::duration<double, std::milli>(EndClock - StartClock).count());
    return true;
}

} // namespace Frontier::GeometricRaster
