/*============================================================================================================================================
                                                          PARTICLEVULKANEXECUTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleVulkanExecutor: 21-module SPIR-V 1.3 verifier & real Vulkan 1.3 / SwiftShader compute dispatcher

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Compute/ParticleVulkanExecutor.h"

#include "Engine/ColourSpace/ColourRampSampler.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"

#include <volk.h>

#if defined(__linux__)
    #include <cstdlib>
    #include <dlfcn.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <vector>

namespace Frontier::ParticleSpace
{

namespace
{

bool ValidateSpirvBinary(const std::filesystem::path& Path, uint32_t& OutBytes) noexcept
{
    std::error_code Ec;
    if (!std::filesystem::exists(Path, Ec)) return false;
    const auto Size = std::filesystem::file_size(Path, Ec);
    if (Ec || Size < 20u || (Size % 4u) != 0u) return false;

    std::ifstream Stream(Path, std::ios::binary);
    if (!Stream) return false;
    uint32_t Magic = 0u;
    Stream.read(reinterpret_cast<char*>(&Magic), sizeof(Magic));
    if (!Stream || Magic != 0x07230203u) return false;

    OutBytes = static_cast<uint32_t>(Size);
    return true;
}

bool ReadSpirvWords(const std::filesystem::path& Path, std::vector<uint32_t>& OutWords)
{
    std::ifstream Stream(Path, std::ios::binary | std::ios::ate);
    if (!Stream) return false;
    const std::streamsize Size = Stream.tellg();
    if (Size <= 0 || (Size % 4) != 0) return false;
    Stream.seekg(0, std::ios::beg);
    OutWords.resize(static_cast<std::size_t>(Size / 4));
    Stream.read(reinterpret_cast<char*>(OutWords.data()), Size);
    return Stream.good();
}

struct HostMappedBuffer
{
    VkBuffer       Buffer   = VK_NULL_HANDLE;
    VkDeviceMemory Memory   = VK_NULL_HANDLE;
    VkDeviceSize   Capacity = 0u;
};

/// Bit-exact 192-byte match to ParticleBuffers.slang ParticleGlobalUniformGpu
struct alignas(16) GpuGlobalUniformBlock
{
    float    ViewProjectionMatrix[16] = {};
    float    CameraPositionMeters[4]  = { 0.0f, 3.0f, 9.0f, 1.0f };
    float    CameraRightMeters[4]     = { 1.0f, 0.0f, 0.0f, 0.0f };
    float    CameraUpMeters[4]        = { 0.0f, 1.0f, 0.0f, 0.0f };
    float    AmbientWindVector[4]     = { 0.0f, 0.0f, 0.0f, 0.0f };
    float    DeltaSeconds             = 1.0f / 60.0f;
    float    ElapsedSeconds           = 0.0f;
    uint32_t FrameIndex               = 0u;
    uint32_t ActiveForceCount         = 0u;
    float    ColliderBallCenterM[3]   = { 0.0f, 1.4f, 0.0f };
    float    ColliderBallRadiusM      = 0.55f;
    float    ColliderBallVelocity[3]  = { 0.0f, 0.0f, 0.0f };
    uint32_t ColliderFlags            = 0u;
    float    ViewportDimensions[4]    = { 1440.0f, 900.0f, 1.0f / 1440.0f, 1.0f / 900.0f };
};
static_assert(sizeof(GpuGlobalUniformBlock) == 192u);

/// Bit-exact 256-byte match to ParticleBuffers.slang ParticleRigUniformGpu
struct alignas(16) GpuRigUniformBlock
{
    uint32_t SlotOffset          = 0u;
    uint32_t SlotCapacity        = 0u;
    uint32_t ReleaseOriginCode   = 0u;
    uint32_t VisualCarrierCode   = 0u;
    uint32_t SpriteCutoutCode    = 0u;
    uint32_t BokehApertureCode   = 0u;
    uint32_t ScalarChannelCode   = 0u;
    uint32_t OpticalBlendCode    = 0u;
    float    ReleaseRatePerSec   = 240.0f;
    float    BurstCount          = 0.0f;
    uint32_t StrandSegments      = 16u;
    uint32_t SubdivisionMode     = 0u;
    float    OriginCenter[3]     = { 0.0f, 1.0f, 0.0f };
    float    OriginRadius        = 0.25f;
    float    OriginHalfExtents[3]= { 1.0f, 1.0f, 1.0f };
    float    OriginPitchRadians  = 0.0f;
    float    LaunchDirection[3]  = { 0.0f, 1.0f, 0.0f };
    float    SpreadRadians       = 0.5f;
    float    SpeedMinMps         = 1.0f;
    float    SpeedMaxMps         = 3.0f;
    float    LifeMinSeconds      = 1.0f;
    float    LifeMaxSeconds      = 2.0f;
    float    SizeBirthMeters     = 0.05f;
    float    SizeDeathMeters     = 0.05f;
    float    SizeMultiplier      = 1.0f;
    float    StreakThinness      = 1.0f;
    float    GravityScale        = 1.0f;
    float    AerodynamicDrag     = 0.2f;
    float    WindCoupling        = 0.0f;
    float    BuoyancyMps2        = 0.0f;
    float    FloorBounce         = 0.0f;
    float    FlutterAmplitude    = 0.0f;
    float    CurlFrequency       = 0.85f;
    float    CurlAmplitude       = 2.2f;
    float    CurlEvolutionRate   = 0.45f;
    float    ManifoldRadius      = 1.5f;
    float    ColourRangeMin      = 0.0f;
    float    ColourRangeMax      = 1.0f;
    float    ColourRepeat        = 1.0f;
    float    ColourPhase         = 0.0f;
    uint32_t ColourWrapCode      = 0u;
    uint32_t ColourCurveCode     = 0u;
    float    PrimaryTintLinear[4]= { 1.0f, 1.0f, 1.0f, 1.0f };
    float    MidTintLinear[4]    = { 1.0f, 1.0f, 1.0f, 1.0f };
    float    EndTintLinear[4]    = { 1.0f, 1.0f, 1.0f, 1.0f };
    uint32_t DynamicsBitmask     = 0u;
    uint32_t ReceiverChannelMask = 0xFFFFFFFFu;
    uint32_t AnchorBufferOffset  = 0u;
    uint32_t AnchorBufferCount   = 0u;
};
static_assert(sizeof(GpuRigUniformBlock) == 256u);

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               VULKAN DEVICE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

struct ParticleVulkanExecutor::VulkanDeviceState
{
    VkInstance            Instance            = VK_NULL_HANDLE;
    VkPhysicalDevice      PhysicalDevice      = VK_NULL_HANDLE;
    VkDevice              Device              = VK_NULL_HANDLE;
    VkQueue               ComputeQueue        = VK_NULL_HANDLE;
    uint32_t              ComputeQueueFamily  = 0u;

    VkDescriptorSetLayout EmptySet0Layout     = VK_NULL_HANDLE;
    VkDescriptorSetLayout AdvectSet1Layout    = VK_NULL_HANDLE;
    VkDescriptorSetLayout ReduceSet1Layout    = VK_NULL_HANDLE;
    VkPipelineLayout      AdvectPipeLayout    = VK_NULL_HANDLE;
    VkPipelineLayout      ReducePipeLayout    = VK_NULL_HANDLE;
    VkPipeline            AdvectPipeline      = VK_NULL_HANDLE;
    VkPipeline            ReducePipeline      = VK_NULL_HANDLE;
    VkDescriptorPool      DescPool            = VK_NULL_HANDLE;
    VkDescriptorSet       AdvectDescSet       = VK_NULL_HANDLE;
    VkDescriptorSet       ReduceDescSet       = VK_NULL_HANDLE;
    VkCommandPool         CmdPool             = VK_NULL_HANDLE;
    VkCommandBuffer       CmdBuf              = VK_NULL_HANDLE;

    HostMappedBuffer      SlotsBuf            {};
    HostMappedBuffer      GlobalUbo           {};
    HostMappedBuffer      RigUbo              {};
    HostMappedBuffer      ForceBuf            {};
    HostMappedBuffer      WaypointBuf         {};
    HostMappedBuffer      ColourLutBuf        {};
    HostMappedBuffer      MetricsBuf          {};

#if defined(__linux__)
    void*                 VulkanLoaderLibrary        = nullptr;
#endif

    uint32_t FindHostVisibleMemType(uint32_t TypeMask) const noexcept
    {
        VkPhysicalDeviceMemoryProperties Props {};
        vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &Props);
        const VkMemoryPropertyFlags Want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t I = 0u; I < Props.memoryTypeCount; ++I)
        {
            if ((TypeMask & (1u << I)) != 0u && (Props.memoryTypes[I].propertyFlags & Want) == Want)
            {
                return I;
            }
        }
        return UINT32_MAX;
    }

    bool EnsureBuffer(HostMappedBuffer& Dst, VkDeviceSize MinBytes, VkBufferUsageFlags Usage) noexcept
    {
        const VkDeviceSize SafeBytes = std::max<VkDeviceSize>(MinBytes, 256u);
        if (Dst.Buffer != VK_NULL_HANDLE && Dst.Capacity >= SafeBytes) return true;

        if (Dst.Buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(Device, Dst.Buffer, nullptr);
            vkFreeMemory(Device, Dst.Memory, nullptr);
            Dst = {};
        }

        VkBufferCreateInfo Bci {};
        Bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        Bci.size        = SafeBytes;
        Bci.usage       = Usage;
        Bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(Device, &Bci, nullptr, &Dst.Buffer) != VK_SUCCESS) return false;

        VkMemoryRequirements Req {};
        vkGetBufferMemoryRequirements(Device, Dst.Buffer, &Req);
        const uint32_t MemIdx = FindHostVisibleMemType(Req.memoryTypeBits);
        if (MemIdx == UINT32_MAX) return false;

        VkMemoryAllocateInfo Mai {};
        Mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        Mai.allocationSize  = Req.size;
        Mai.memoryTypeIndex = MemIdx;
        if (vkAllocateMemory(Device, &Mai, nullptr, &Dst.Memory) != VK_SUCCESS) return false;
        if (vkBindBufferMemory(Device, Dst.Buffer, Dst.Memory, 0u) != VK_SUCCESS) return false;
        Dst.Capacity = SafeBytes;
        return true;
    }

    bool Upload(HostMappedBuffer& Dst, const void* SrcData, VkDeviceSize Bytes) noexcept
    {
        void* Mapped = nullptr;
        if (vkMapMemory(Device, Dst.Memory, 0u, Bytes, 0u, &Mapped) != VK_SUCCESS) return false;
        std::memcpy(Mapped, SrcData, static_cast<std::size_t>(Bytes));
        vkUnmapMemory(Device, Dst.Memory);
        return true;
    }

    bool Download(const HostMappedBuffer& Src, void* DstData, VkDeviceSize Bytes) noexcept
    {
        void* Mapped = nullptr;
        if (vkMapMemory(Device, Src.Memory, 0u, Bytes, 0u, &Mapped) != VK_SUCCESS) return false;
        std::memcpy(DstData, Mapped, static_cast<std::size_t>(Bytes));
        vkUnmapMemory(Device, Src.Memory);
        return true;
    }

    void DestroyAll() noexcept
    {
        if (Device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(Device);
            HostMappedBuffer* Bufs[7] = { &SlotsBuf, &GlobalUbo, &RigUbo, &ForceBuf, &WaypointBuf, &ColourLutBuf, &MetricsBuf };
            for (HostMappedBuffer* B : Bufs)
            {
                if (B->Buffer != VK_NULL_HANDLE)
                {
                    vkDestroyBuffer(Device, B->Buffer, nullptr);
                    vkFreeMemory(Device, B->Memory, nullptr);
                    *B = {};
                }
            }
            if (CmdPool          != VK_NULL_HANDLE) vkDestroyCommandPool(Device, CmdPool, nullptr);
            if (DescPool         != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, DescPool, nullptr);
            if (AdvectPipeline   != VK_NULL_HANDLE) vkDestroyPipeline(Device, AdvectPipeline, nullptr);
            if (ReducePipeline   != VK_NULL_HANDLE) vkDestroyPipeline(Device, ReducePipeline, nullptr);
            if (AdvectPipeLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, AdvectPipeLayout, nullptr);
            if (ReducePipeLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, ReducePipeLayout, nullptr);
            if (AdvectSet1Layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, AdvectSet1Layout, nullptr);
            if (ReduceSet1Layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, ReduceSet1Layout, nullptr);
            if (EmptySet0Layout  != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, EmptySet0Layout, nullptr);
            vkDestroyDevice(Device, nullptr);
        }
        if (Instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(Instance, nullptr);
        }
#if defined(__linux__)
        if (VulkanLoaderLibrary != nullptr)
        {
            dlclose(VulkanLoaderLibrary);
            VulkanLoaderLibrary = nullptr;
        }
#endif
        *this = {};
    }
};

ParticleVulkanExecutor::ParticleVulkanExecutor() = default;
ParticleVulkanExecutor::~ParticleVulkanExecutor() { Shutdown(); }

bool ParticlePipelineArtifacts::AllShadersPresentOnDisk() const noexcept
{
    std::error_code Ec;
    const std::filesystem::path All[21] = {
        ReleaseComputeSpv, AdvectComputeSpv, ForceCouplingComputeSpv, SpatialGridComputeSpv, ReductionComputeSpv,
        SpriteVertexSpv, SpriteFragmentSpv, RibbonVertexSpv, RibbonFragmentSpv,
        SolidVertexSpv, SolidFragmentSpv, WingedVertexSpv, WingedFragmentSpv,
        LineArcVertexSpv, LineArcFragmentSpv, LensVertexSpv, LensFragmentSpv,
        SurfaceBackingVertexSpv, SurfaceBackingFragmentSpv, OverlayVertexSpv, OverlayFragmentSpv
    };
    for (const auto& P : All)
    {
        if (!std::filesystem::exists(P, Ec)) return false;
    }
    return true;
}

bool ParticleVulkanExecutor::Initialize(const std::filesystem::path& CompiledShaderDirectory, std::string& OutDiagnostic)
{
    Artifacts.ReleaseComputeSpv         = CompiledShaderDirectory / "ParticleReleaseCompute.comp.spv";
    Artifacts.AdvectComputeSpv          = CompiledShaderDirectory / "ParticleAdvectCompute.comp.spv";
    Artifacts.ForceCouplingComputeSpv   = CompiledShaderDirectory / "ParticleForceCoupling.comp.spv";
    Artifacts.SpatialGridComputeSpv     = CompiledShaderDirectory / "ParticleSpatialGridCompute.comp.spv";
    Artifacts.ReductionComputeSpv       = CompiledShaderDirectory / "ParticleReductionCompute.comp.spv";
    Artifacts.SpriteVertexSpv           = CompiledShaderDirectory / "ParticleSpriteRaster.vert.spv";
    Artifacts.SpriteFragmentSpv         = CompiledShaderDirectory / "ParticleSpriteRaster.frag.spv";
    Artifacts.RibbonVertexSpv           = CompiledShaderDirectory / "ParticleRibbonRaster.vert.spv";
    Artifacts.RibbonFragmentSpv         = CompiledShaderDirectory / "ParticleRibbonRaster.frag.spv";
    Artifacts.SolidVertexSpv            = CompiledShaderDirectory / "ParticleSolidRasterization.vert.spv";
    Artifacts.SolidFragmentSpv          = CompiledShaderDirectory / "ParticleSolidRasterization.frag.spv";
    Artifacts.WingedVertexSpv           = CompiledShaderDirectory / "ParticleWingedRasterization.vert.spv";
    Artifacts.WingedFragmentSpv         = CompiledShaderDirectory / "ParticleWingedRasterization.frag.spv";
    Artifacts.LineArcVertexSpv          = CompiledShaderDirectory / "ParticleLineArcRasterization.vert.spv";
    Artifacts.LineArcFragmentSpv        = CompiledShaderDirectory / "ParticleLineArcRasterization.frag.spv";
    Artifacts.LensVertexSpv             = CompiledShaderDirectory / "ParticleLensRasterization.vert.spv";
    Artifacts.LensFragmentSpv           = CompiledShaderDirectory / "ParticleLensRasterization.frag.spv";
    Artifacts.SurfaceBackingVertexSpv   = CompiledShaderDirectory / "ParticleSurfaceBackingRasterization.vert.spv";
    Artifacts.SurfaceBackingFragmentSpv = CompiledShaderDirectory / "ParticleSurfaceBackingRasterization.frag.spv";
    Artifacts.OverlayVertexSpv          = CompiledShaderDirectory / "ParticleOverlayRasterization.vert.spv";
    Artifacts.OverlayFragmentSpv        = CompiledShaderDirectory / "ParticleOverlayRasterization.frag.spv";

    const std::filesystem::path Paths[21] = {
        Artifacts.ReleaseComputeSpv, Artifacts.AdvectComputeSpv, Artifacts.ForceCouplingComputeSpv,
        Artifacts.SpatialGridComputeSpv, Artifacts.ReductionComputeSpv,
        Artifacts.SpriteVertexSpv, Artifacts.SpriteFragmentSpv, Artifacts.RibbonVertexSpv, Artifacts.RibbonFragmentSpv,
        Artifacts.SolidVertexSpv, Artifacts.SolidFragmentSpv, Artifacts.WingedVertexSpv, Artifacts.WingedFragmentSpv,
        Artifacts.LineArcVertexSpv, Artifacts.LineArcFragmentSpv, Artifacts.LensVertexSpv, Artifacts.LensFragmentSpv,
        Artifacts.SurfaceBackingVertexSpv, Artifacts.SurfaceBackingFragmentSpv, Artifacts.OverlayVertexSpv, Artifacts.OverlayFragmentSpv
    };

    uint32_t ValidCount = 0u;
    uint32_t TotalBytes = 0u;
    for (const auto& P : Paths)
    {
        uint32_t Bytes = 0u;
        if (ValidateSpirvBinary(P, Bytes))
        {
            ++ValidCount;
            TotalBytes += Bytes;
        }
    }

    Telemetry.CompiledShaderCount = ValidCount;
    Telemetry.TotalSpirvBytes     = TotalBytes;
    Telemetry.ShadersVerified     = (ValidCount == 21u);

    if (!Telemetry.ShadersVerified)
    {
        OutDiagnostic = "ParticleVulkanExecutor verified " + std::to_string(ValidCount) + "/21 SPIR-V modules in " + CompiledShaderDirectory.string();
        return false;
    }

    OutDiagnostic = "ParticleVulkanExecutor verified all 21 SPIR-V 1.3 modules (" + std::to_string(TotalBytes) + " bytes).";
    return true;
}

bool ParticleVulkanExecutor::InitializeGpuComputeDevice(const std::filesystem::path& SwiftShaderIcdDir, std::string& OutDiagnostic)
{
    if (!Telemetry.ShadersVerified)
    {
        OutDiagnostic = "Call Initialize(CompiledShaderDirectory) before InitializeGpuComputeDevice().";
        return false;
    }

    if (DeviceState)
    {
        DeviceState->DestroyAll();
        DeviceState.reset();
    }
    DeviceState = std::make_unique<VulkanDeviceState>();

    std::vector<uint32_t> AdvectWords, ReduceWords;
    if (!ReadSpirvWords(Artifacts.AdvectComputeSpv, AdvectWords) || !ReadSpirvWords(Artifacts.ReductionComputeSpv, ReduceWords))
    {
        OutDiagnostic = "Failed to read Advect / Reduction SPIR-V compute binaries.";
        DeviceState.reset();
        return false;
    }

#if defined(__linux__)
    const std::filesystem::path IcdJson  = SwiftShaderIcdDir / "vk_swiftshader_icd.json";
    const std::filesystem::path LoaderSo = SwiftShaderIcdDir / "libvulkan.so.1";
    if (std::filesystem::exists(IcdJson) && std::getenv("VK_ICD_FILENAMES") == nullptr)
    {
        const std::string AbsIcd = std::filesystem::weakly_canonical(IcdJson).string();
        setenv("VK_ICD_FILENAMES", AbsIcd.c_str(), 0);
        setenv("VK_DRIVER_FILES",  AbsIcd.c_str(), 0);
    }
#else
    (void)SwiftShaderIcdDir;
#endif

    if (volkInitialize() != VK_SUCCESS)
    {
#if defined(__linux__)
        if (std::filesystem::exists(LoaderSo))
        {
            DeviceState->VulkanLoaderLibrary = dlopen(std::filesystem::weakly_canonical(LoaderSo).c_str(), RTLD_NOW | RTLD_LOCAL);
            if (DeviceState->VulkanLoaderLibrary != nullptr)
            {
                auto GetProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(DeviceState->VulkanLoaderLibrary, "vkGetInstanceProcAddr"));
                if (GetProc != nullptr) volkInitializeCustom(GetProc);
            }
        }
#endif
    }

    if (vkCreateInstance == nullptr)
    {
        OutDiagnostic = "Vulkan loader unavailable.";
        DeviceState->DestroyAll();
        DeviceState.reset();
        return false;
    }

    VkApplicationInfo App {};
    App.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    App.pApplicationName   = "FrontierParticleSpace";
    App.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    App.pEngineName        = "Frontier";
    App.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    App.apiVersion         = VK_API_VERSION_1_3;

    VkInstanceCreateInfo Ici {};
    Ici.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    Ici.pApplicationInfo = &App;
    if (vkCreateInstance(&Ici, nullptr, &DeviceState->Instance) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateInstance failed.";
        DeviceState->DestroyAll();
        DeviceState.reset();
        return false;
    }
    volkLoadInstanceOnly(DeviceState->Instance);

    uint32_t PhysCount = 0u;
    vkEnumeratePhysicalDevices(DeviceState->Instance, &PhysCount, nullptr);
    if (PhysCount == 0u)
    {
        OutDiagnostic = "No Vulkan physical device found.";
        DeviceState->DestroyAll();
        DeviceState.reset();
        return false;
    }
    std::vector<VkPhysicalDevice> Devs(PhysCount);
    vkEnumeratePhysicalDevices(DeviceState->Instance, &PhysCount, Devs.data());
    DeviceState->PhysicalDevice = Devs[0];

    VkPhysicalDeviceProperties DevProps {};
    vkGetPhysicalDeviceProperties(DeviceState->PhysicalDevice, &DevProps);
    Telemetry.DeviceName = DevProps.deviceName;

    uint32_t QfCount = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties(DeviceState->PhysicalDevice, &QfCount, nullptr);
    std::vector<VkQueueFamilyProperties> Qfs(QfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(DeviceState->PhysicalDevice, &QfCount, Qfs.data());
    uint32_t ComputeFamily = UINT32_MAX;
    for (uint32_t I = 0u; I < QfCount; ++I)
    {
        if ((Qfs[I].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u) { ComputeFamily = I; break; }
    }
    if (ComputeFamily == UINT32_MAX)
    {
        OutDiagnostic = "No Vulkan compute queue family found.";
        DeviceState->DestroyAll();
        DeviceState.reset();
        return false;
    }
    DeviceState->ComputeQueueFamily = ComputeFamily;

    const float Priority = 1.0f;
    VkDeviceQueueCreateInfo Qci {};
    Qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    Qci.queueFamilyIndex = ComputeFamily;
    Qci.queueCount       = 1u;
    Qci.pQueuePriorities = &Priority;

    VkDeviceCreateInfo Dci {};
    Dci.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    Dci.queueCreateInfoCount = 1u;
    Dci.pQueueCreateInfos    = &Qci;
    if (vkCreateDevice(DeviceState->PhysicalDevice, &Dci, nullptr, &DeviceState->Device) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateDevice failed.";
        DeviceState->DestroyAll();
        DeviceState.reset();
        return false;
    }
    volkLoadDevice(DeviceState->Device);
    vkGetDeviceQueue(DeviceState->Device, ComputeFamily, 0u, &DeviceState->ComputeQueue);

    // Descriptor set 0 (empty placeholder matching Slang ColourRampSampling set 0)
    VkDescriptorSetLayoutCreateInfo EmptyLci {};
    EmptyLci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    if (vkCreateDescriptorSetLayout(DeviceState->Device, &EmptyLci, nullptr, &DeviceState->EmptySet0Layout) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to create empty set 0 layout.";
        DeviceState->DestroyAll();
        DeviceState.reset();
        return false;
    }

    // Advect compute bindings on set 1: 0=SSBO(Slots), 1=UBO(Global), 2=UBO(Rig), 3=SSBO(Forces), 4=SSBO(Waypoints), 5=SSBO(ColourLut)
    const VkDescriptorSetLayoutBinding AdvectBindings[6] = {
        { 0u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 2u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
    };
    VkDescriptorSetLayoutCreateInfo AdvLci {};
    AdvLci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    AdvLci.bindingCount = 6u;
    AdvLci.pBindings    = AdvectBindings;
    if (vkCreateDescriptorSetLayout(DeviceState->Device, &AdvLci, nullptr, &DeviceState->AdvectSet1Layout) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to create Advect descriptor layout.";
        DeviceState->DestroyAll();
        DeviceState.reset();
        return false;
    }

    // Reduction compute bindings on set 1: 0=SSBO(Slots), 1=UBO(Rig), 2=SSBO(MetricsOut)
    const VkDescriptorSetLayoutBinding ReduceBindings[3] = {
        { 0u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
    };
    VkDescriptorSetLayoutCreateInfo RedLci {};
    RedLci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    RedLci.bindingCount = 3u;
    RedLci.pBindings    = ReduceBindings;
    if (vkCreateDescriptorSetLayout(DeviceState->Device, &RedLci, nullptr, &DeviceState->ReduceSet1Layout) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to create Reduction descriptor layout.";
        DeviceState->DestroyAll();
        DeviceState.reset();
        return false;
    }

    const VkDescriptorSetLayout AdvSets[2] = { DeviceState->EmptySet0Layout, DeviceState->AdvectSet1Layout };
    VkPipelineLayoutCreateInfo AdvPlci {};
    AdvPlci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    AdvPlci.setLayoutCount = 2u;
    AdvPlci.pSetLayouts    = AdvSets;
    if (vkCreatePipelineLayout(DeviceState->Device, &AdvPlci, nullptr, &DeviceState->AdvectPipeLayout) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to create Advect pipeline layout.";
        DeviceState->DestroyAll();
        DeviceState.reset();
        return false;
    }

    const VkDescriptorSetLayout RedSets[2] = { DeviceState->EmptySet0Layout, DeviceState->ReduceSet1Layout };
    VkPipelineLayoutCreateInfo RedPlci {};
    RedPlci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    RedPlci.setLayoutCount = 2u;
    RedPlci.pSetLayouts    = RedSets;
    if (vkCreatePipelineLayout(DeviceState->Device, &RedPlci, nullptr, &DeviceState->ReducePipeLayout) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to create Reduction pipeline layout.";
        DeviceState->DestroyAll();
        DeviceState.reset();
        return false;
    }

    auto BuildComputePipe = [&](const std::vector<uint32_t>& Words, const char* Entry, VkPipelineLayout Layout, VkPipeline& OutPipe) -> bool
    {
        VkShaderModuleCreateInfo Smci {};
        Smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        Smci.codeSize = Words.size() * sizeof(uint32_t);
        Smci.pCode    = Words.data();
        VkShaderModule Mod = VK_NULL_HANDLE;
        if (vkCreateShaderModule(DeviceState->Device, &Smci, nullptr, &Mod) != VK_SUCCESS) return false;

        VkComputePipelineCreateInfo Cpci {};
        Cpci.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        Cpci.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        Cpci.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        Cpci.stage.module = Mod;
        Cpci.stage.pName  = Entry;
        Cpci.layout       = Layout;
        const VkResult Res = vkCreateComputePipelines(DeviceState->Device, VK_NULL_HANDLE, 1u, &Cpci, nullptr, &OutPipe);
        vkDestroyShaderModule(DeviceState->Device, Mod, nullptr);
        return Res == VK_SUCCESS;
    };

    if (!BuildComputePipe(AdvectWords, "ParticleAdvectComputeEntry", DeviceState->AdvectPipeLayout, DeviceState->AdvectPipeline)
     || !BuildComputePipe(ReduceWords, "ParticleReductionComputeEntry", DeviceState->ReducePipeLayout, DeviceState->ReducePipeline))
    {
        OutDiagnostic = "Failed to compile Vulkan 1.3 Advect/Reduction compute pipelines.";
        DeviceState->DestroyAll();
        DeviceState.reset();
        return false;
    }

    const VkDescriptorPoolSize PoolSizes[2] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 12u },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,  6u }
    };
    VkDescriptorPoolCreateInfo Dpci {};
    Dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    Dpci.maxSets       = 4u;
    Dpci.poolSizeCount = 2u;
    Dpci.pPoolSizes    = PoolSizes;
    if (vkCreateDescriptorPool(DeviceState->Device, &Dpci, nullptr, &DeviceState->DescPool) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to create descriptor pool.";
        DeviceState->DestroyAll();
        DeviceState.reset();
        return false;
    }

    const VkDescriptorSetLayout AllocLayouts[2] = { DeviceState->AdvectSet1Layout, DeviceState->ReduceSet1Layout };
    VkDescriptorSet AllocSets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDescriptorSetAllocateInfo Dsai {};
    Dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    Dsai.descriptorPool     = DeviceState->DescPool;
    Dsai.descriptorSetCount = 2u;
    Dsai.pSetLayouts        = AllocLayouts;
    if (vkAllocateDescriptorSets(DeviceState->Device, &Dsai, AllocSets) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to allocate Vulkan descriptor sets.";
        DeviceState->DestroyAll();
        DeviceState.reset();
        return false;
    }
    DeviceState->AdvectDescSet = AllocSets[0];
    DeviceState->ReduceDescSet = AllocSets[1];

    VkCommandPoolCreateInfo Cpool {};
    Cpool.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    Cpool.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    Cpool.queueFamilyIndex = ComputeFamily;
    if (vkCreateCommandPool(DeviceState->Device, &Cpool, nullptr, &DeviceState->CmdPool) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to create compute command pool.";
        DeviceState->DestroyAll();
        DeviceState.reset();
        return false;
    }

    VkCommandBufferAllocateInfo Cbai {};
    Cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    Cbai.commandPool        = DeviceState->CmdPool;
    Cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    Cbai.commandBufferCount = 1u;
    if (vkAllocateCommandBuffers(DeviceState->Device, &Cbai, &DeviceState->CmdBuf) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to allocate compute command buffer.";
        DeviceState->DestroyAll();
        DeviceState.reset();
        return false;
    }

    Telemetry.HardwareDeviceActive = true;
    OutDiagnostic = "Vulkan 1.3 compute pipelines ready on device '" + Telemetry.DeviceName + "'.";
    return true;
}

void ParticleVulkanExecutor::Shutdown() noexcept
{
    if (DeviceState)
    {
        DeviceState->DestroyAll();
        DeviceState.reset();
    }
    Telemetry = {};
}

uint32_t ParticleVulkanExecutor::ExecuteRigStep(
    const ParticleRig&         Rig,
    const ParticleStepContext& Context,
    std::span<ParticleSlot>    RigSlots,
    std::span<StrandVertex>    OptionalStrandHistory)
{
    Telemetry.TotalDispatches += 1u;
    return ExecuteParticleRigStepCpu(Rig, Context, RigSlots, OptionalStrandHistory);
}

bool ParticleVulkanExecutor::DispatchAdvectComputeGpu(
    const ParticleRig&         Rig,
    const ParticleStepContext& Context,
    std::span<ParticleSlot>    RigSlots,
    std::string&               OutDiagnostic)
{
    if (!DeviceState || !Telemetry.HardwareDeviceActive || RigSlots.empty())
    {
        OutDiagnostic = "Vulkan compute device not initialized or empty slot span.";
        return false;
    }

    const VkDeviceSize SlotsBytes = RigSlots.size() * sizeof(ParticleSlot);
    GpuGlobalUniformBlock Global {};
    Global.ElapsedSeconds   = Context.ElapsedSeconds;
    Global.DeltaSeconds     = Context.DeltaSeconds;
    Global.FrameIndex       = static_cast<uint32_t>(Telemetry.TotalDispatches);
    Global.ActiveForceCount = 0u;
    if (Context.Document.AmbientWind.Enabled)
    {
        const float Az = Context.Document.AmbientWind.AzimuthDegrees * 0.0174532925f;
        Global.AmbientWindVector[0] = std::cos(Az) * Context.Document.AmbientWind.SpeedMps;
        Global.AmbientWindVector[2] = std::sin(Az) * Context.Document.AmbientWind.SpeedMps;
    }
    Global.ColliderBallCenterM[0] = Context.Document.Collider.BallCenterM[0];
    Global.ColliderBallCenterM[1] = Context.Document.Collider.BallCenterM[1];
    Global.ColliderBallCenterM[2] = Context.Document.Collider.BallCenterM[2];
    Global.ColliderBallRadiusM    = Context.Document.Collider.BallRadiusMeters;
    Global.ColliderFlags          = Context.Document.Collider.BallActive ? 1u : 0u;

    GpuRigUniformBlock RigU {};
    RigU.SlotOffset        = 0u;
    RigU.SlotCapacity      = static_cast<uint32_t>(RigSlots.size());
    RigU.DynamicsBitmask   = Rig.Dynamics.ComputeActiveBitmask();
    RigU.GravityScale      = Rig.Dynamics.Ballistic.GravityScale;
    RigU.AerodynamicDrag   = Rig.Dynamics.Ballistic.AerodynamicDrag;
    RigU.BuoyancyMps2      = Rig.Dynamics.Ballistic.BuoyancyMps2;
    RigU.FloorBounce       = Rig.Dynamics.Ballistic.FloorRestitution;
    RigU.WindCoupling      = Rig.Dynamics.ForceCouple.WindCouplingRate;
    RigU.SizeBirthMeters   = Rig.Kinematics.SizeBirthMeters;
    RigU.SizeDeathMeters   = Rig.Kinematics.SizeDeathMeters;
    RigU.SizeMultiplier    = Rig.Kinematics.SizeMultiplier;
    RigU.ScalarChannelCode = static_cast<uint32_t>(Rig.TintDriver.Channel);
    RigU.ColourRangeMin    = Rig.ColourProfile.Range.RangeMin;
    RigU.ColourRangeMax    = Rig.ColourProfile.Range.RangeMax;
    if (Rig.Dynamics.Curl.has_value())
    {
        RigU.CurlFrequency     = Rig.Dynamics.Curl->FrequencyPerM;
        RigU.CurlAmplitude     = Rig.Dynamics.Curl->DriveSpeedMps;
        RigU.CurlEvolutionRate = Rig.Dynamics.Curl->EvolutionRateHz;
        RigU.ManifoldRadius    = Rig.Dynamics.Curl->ShellRadiusM;
    }

    // Pack ForceFields + 64-entry ColourRampLut64
    std::vector<SimulationSpace::ForceFields::ForceSourceShaderEntry> ShaderForces;
    std::vector<std::array<float, 4>> Waypoints;
    for (const auto& Src : Context.ForceAccumulator.QuerySources())
    {
        if (!Src.Enabled) continue;
        const uint32_t StartWp = static_cast<uint32_t>(Waypoints.size());
        const uint32_t WpCount = std::min(Src.Motion.WaypointCount, SimulationSpace::ForceFields::MaxForceMotionWaypoints);
        for (uint32_t W = 0u; W < WpCount; ++W)
        {
            const auto& Wp = Src.Motion.Waypoints[W];
            Waypoints.push_back(std::array<float, 4> { Wp.X, Wp.Y, Wp.Z, 1.0f });
        }
        ShaderForces.push_back(SimulationSpace::ForceFields::PackForceSourceForShader(Src, Context.ElapsedSeconds, StartWp));
    }
    Global.ActiveForceCount = static_cast<uint32_t>(ShaderForces.size());
    if (ShaderForces.empty()) ShaderForces.resize(1u);
    if (Waypoints.empty())    Waypoints.push_back(std::array<float, 4> { 0.0f, 0.0f, 0.0f, 0.0f });

    const ColourSpace::ColourRampLut64 Lut = ColourSpace::PackColourRampLut64(Rig.ColourProfile);

    const bool OkAlloc =
        DeviceState->EnsureBuffer(DeviceState->SlotsBuf,     SlotsBytes,                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
     && DeviceState->EnsureBuffer(DeviceState->GlobalUbo,    sizeof(Global),                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
     && DeviceState->EnsureBuffer(DeviceState->RigUbo,       sizeof(RigU),                                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
     && DeviceState->EnsureBuffer(DeviceState->ForceBuf,     ShaderForces.size() * sizeof(ShaderForces[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
     && DeviceState->EnsureBuffer(DeviceState->WaypointBuf,  Waypoints.size() * sizeof(Waypoints[0]),       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
     && DeviceState->EnsureBuffer(DeviceState->ColourLutBuf, sizeof(Lut),                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (!OkAlloc)
    {
        OutDiagnostic = "Failed to allocate Vulkan compute buffers.";
        return false;
    }

    DeviceState->Upload(DeviceState->SlotsBuf,     RigSlots.data(),     SlotsBytes);
    DeviceState->Upload(DeviceState->GlobalUbo,    &Global,             sizeof(Global));
    DeviceState->Upload(DeviceState->RigUbo,       &RigU,               sizeof(RigU));
    DeviceState->Upload(DeviceState->ForceBuf,     ShaderForces.data(), ShaderForces.size() * sizeof(ShaderForces[0]));
    DeviceState->Upload(DeviceState->WaypointBuf,  Waypoints.data(),    Waypoints.size() * sizeof(Waypoints[0]));
    DeviceState->Upload(DeviceState->ColourLutBuf, &Lut,                sizeof(Lut));

    const VkDescriptorBufferInfo BInfos[6] = {
        { DeviceState->SlotsBuf.Buffer,     0u, SlotsBytes },
        { DeviceState->GlobalUbo.Buffer,    0u, sizeof(Global) },
        { DeviceState->RigUbo.Buffer,       0u, sizeof(RigU) },
        { DeviceState->ForceBuf.Buffer,     0u, ShaderForces.size() * sizeof(ShaderForces[0]) },
        { DeviceState->WaypointBuf.Buffer,  0u, Waypoints.size() * sizeof(Waypoints[0]) },
        { DeviceState->ColourLutBuf.Buffer, 0u, sizeof(Lut) },
    };
    const VkDescriptorType BTypes[6] = {
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    };
    VkWriteDescriptorSet Writes[6] {};
    for (uint32_t I = 0u; I < 6u; ++I)
    {
        Writes[I].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[I].dstSet          = DeviceState->AdvectDescSet;
        Writes[I].dstBinding      = I;
        Writes[I].descriptorCount = 1u;
        Writes[I].descriptorType  = BTypes[I];
        Writes[I].pBufferInfo     = &BInfos[I];
    }
    vkUpdateDescriptorSets(DeviceState->Device, 6u, Writes, 0u, nullptr);

    vkResetCommandBuffer(DeviceState->CmdBuf, 0u);
    VkCommandBufferBeginInfo Begin {};
    Begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(DeviceState->CmdBuf, &Begin);
    vkCmdBindPipeline(DeviceState->CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceState->AdvectPipeline);
    vkCmdBindDescriptorSets(DeviceState->CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceState->AdvectPipeLayout, 1u, 1u, &DeviceState->AdvectDescSet, 0u, nullptr);
    const uint32_t Groups = static_cast<uint32_t>((RigSlots.size() + 63u) / 64u);
    vkCmdDispatch(DeviceState->CmdBuf, Groups, 1u, 1u);
    vkEndCommandBuffer(DeviceState->CmdBuf);

    VkSubmitInfo Submit {};
    Submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Submit.commandBufferCount = 1u;
    Submit.pCommandBuffers    = &DeviceState->CmdBuf;
    if (vkQueueSubmit(DeviceState->ComputeQueue, 1u, &Submit, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        OutDiagnostic = "vkQueueSubmit failed for Advect compute shader.";
        return false;
    }
    vkQueueWaitIdle(DeviceState->ComputeQueue);

    DeviceState->Download(DeviceState->SlotsBuf, RigSlots.data(), SlotsBytes);
    Telemetry.GpuComputeDispatches += 1u;
    Telemetry.TotalDispatches      += 1u;
    OutDiagnostic = "Dispatched ParticleAdvectCompute.comp.spv on " + Telemetry.DeviceName;
    return true;
}

bool ParticleVulkanExecutor::DispatchReductionComputeGpu(
    std::span<const ParticleSlot> RigSlots,
    ParticleSimulationMetrics&    OutMetrics,
    std::string&                  OutDiagnostic)
{
    if (!DeviceState || !Telemetry.HardwareDeviceActive || RigSlots.empty())
    {
        OutDiagnostic = "Vulkan compute device not initialized or empty slot span.";
        return false;
    }

    const auto StartClock = std::chrono::steady_clock::now();
    const VkDeviceSize SlotsBytes = RigSlots.size() * sizeof(ParticleSlot);
    GpuRigUniformBlock RigU {};
    RigU.SlotOffset   = 0u;
    RigU.SlotCapacity = static_cast<uint32_t>(RigSlots.size());

    ParticleMetricsReductionGpuBuffer ZeroMetrics {};
    if (!DeviceState->EnsureBuffer(DeviceState->SlotsBuf,   SlotsBytes,          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
     || !DeviceState->EnsureBuffer(DeviceState->RigUbo,     sizeof(RigU),        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
     || !DeviceState->EnsureBuffer(DeviceState->MetricsBuf, sizeof(ZeroMetrics), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
    {
        OutDiagnostic = "Failed to allocate Reduction GPU buffers.";
        return false;
    }

    DeviceState->Upload(DeviceState->SlotsBuf,   RigSlots.data(), SlotsBytes);
    DeviceState->Upload(DeviceState->RigUbo,     &RigU,           sizeof(RigU));
    DeviceState->Upload(DeviceState->MetricsBuf, &ZeroMetrics,    sizeof(ZeroMetrics));

    const VkDescriptorBufferInfo BInfos[3] = {
        { DeviceState->SlotsBuf.Buffer,   0u, SlotsBytes },
        { DeviceState->RigUbo.Buffer,     0u, sizeof(RigU) },
        { DeviceState->MetricsBuf.Buffer, 0u, sizeof(ZeroMetrics) },
    };
    const VkDescriptorType BTypes[3] = {
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    };
    VkWriteDescriptorSet Writes[3] {};
    for (uint32_t I = 0u; I < 3u; ++I)
    {
        Writes[I].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[I].dstSet          = DeviceState->ReduceDescSet;
        Writes[I].dstBinding      = I;
        Writes[I].descriptorCount = 1u;
        Writes[I].descriptorType  = BTypes[I];
        Writes[I].pBufferInfo     = &BInfos[I];
    }
    vkUpdateDescriptorSets(DeviceState->Device, 3u, Writes, 0u, nullptr);

    vkResetCommandBuffer(DeviceState->CmdBuf, 0u);
    VkCommandBufferBeginInfo Begin {};
    Begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(DeviceState->CmdBuf, &Begin);
    vkCmdBindPipeline(DeviceState->CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceState->ReducePipeline);
    vkCmdBindDescriptorSets(DeviceState->CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceState->ReducePipeLayout, 1u, 1u, &DeviceState->ReduceDescSet, 0u, nullptr);
    const uint32_t Groups = static_cast<uint32_t>((RigSlots.size() + 63u) / 64u);
    vkCmdDispatch(DeviceState->CmdBuf, Groups, 1u, 1u);
    vkEndCommandBuffer(DeviceState->CmdBuf);

    VkSubmitInfo Submit {};
    Submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Submit.commandBufferCount = 1u;
    Submit.pCommandBuffers    = &DeviceState->CmdBuf;
    if (vkQueueSubmit(DeviceState->ComputeQueue, 1u, &Submit, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        OutDiagnostic = "vkQueueSubmit failed for Reduction compute shader.";
        return false;
    }
    vkQueueWaitIdle(DeviceState->ComputeQueue);

    ParticleMetricsReductionGpuBuffer Result {};
    DeviceState->Download(DeviceState->MetricsBuf, &Result, sizeof(Result));
    const float ElapsedMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - StartClock).count();

    OutMetrics = ParticleMetricsCollector::DecodeFromGpuReduction(Result, static_cast<uint32_t>(RigSlots.size()), ElapsedMs);
    Telemetry.GpuComputeDispatches += 1u;
    OutDiagnostic = "Dispatched ParticleReductionCompute.comp.spv on " + Telemetry.DeviceName;
    return true;
}

const ParticlePipelineArtifacts& ParticleVulkanExecutor::InspectArtifacts() const noexcept { return Artifacts; }
const ParticleVulkanTelemetry&   ParticleVulkanExecutor::InspectTelemetry() const noexcept { return Telemetry; }

} // namespace Frontier::ParticleSpace
