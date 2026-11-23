/*============================================================================================================================================
                                                    VECTORCOVERAGERASTERIZATION.CPP
============================================================================================================================================*/
// 🧩 Dispatch reusable Slang coverage programs without CPU pixel composition, queue waits or per-edit pipeline creation.

#include "VectorCoverageRasterization.h"
#include "VectorCoverageTileBinning.h"
#include "VectorSurfaceMaterialBridge.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include <chrono>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <utility>
#include <string_view>
#include <fstream>

namespace Frontier
{
namespace
{
void ConfirmCoverageResult(VkResult Result)
{
    if (Result != VK_SUCCESS) throw std::runtime_error("Vulkan vector coverage failed: " + std::to_string(int(Result)));
}
}
struct VectorCoverageRasterization::CoverageAllocation
{
    VulkanRasterAccess Access;
    VkPhysicalDeviceMemoryProperties Memory{};
    VkExtent2D Extent{};
    VkBuffer Instructions = VK_NULL_HANDLE, Segments = VK_NULL_HANDLE, ReadbackBuffer = VK_NULL_HANDLE;
    // 📝 CPU-binned per-tile leaf-instruction culling (see VectorCoverageTileBinning.h): rebuilt and
    // reuploaded alongside Instructions/Segments on every ConfigureRetired* call, sized once at Initialize
    // for this rasterization's fixed Width/Height to the worst case every leaf reaching every tile.
    VkBuffer TileDirectory = VK_NULL_HANDLE, TileIndices = VK_NULL_HANDLE;
    VkDeviceMemory TileDirectoryMemory = VK_NULL_HANDLE, TileIndexMemory = VK_NULL_HANDLE;
    uint32_t TilesPerRow = 1;
    VkDeviceMemory InstructionMemory = VK_NULL_HANDLE, SegmentMemory = VK_NULL_HANDLE, ImageMemory = VK_NULL_HANDLE;
    VkDeviceMemory ReadbackMemory = VK_NULL_HANDLE;
    VkDeviceSize ReadbackBytes = 0;
    VkImage Image = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VkDescriptorSetLayout DescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool Pool = VK_NULL_HANDLE;
    VkDescriptorSet Descriptor = VK_NULL_HANDLE;
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    uint32_t Count = 0, Root = 0, Channel = 0, Contributions = 0;
    VkFormat OutputFormat = VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t PushConstantBytes = 24; // 📝 Default matches the non-Float32 CoverageProjection ABI; Configure() overwrites this per OutputFormat below.
    uint64_t LastSubmission = 0, PendingSubmission = 0, Dispatches = 0, ReadbackSubmission = 0;
    bool Ready = false, Dirty = false, Recorded = false, ReadbackRecorded = false, ReadbackAvailable = false;
#define FRONTIER_COVERAGE_FUNCTIONS(Function) \
    Function(CreateBuffer) Function(DestroyBuffer) Function(GetBufferMemoryRequirements) Function(AllocateMemory) \
    Function(FreeMemory) Function(BindBufferMemory) Function(MapMemory) Function(UnmapMemory) \
    Function(CreateImage) Function(DestroyImage) Function(GetImageMemoryRequirements) Function(BindImageMemory) \
    Function(CreateImageView) Function(DestroyImageView) Function(CreateDescriptorSetLayout) Function(DestroyDescriptorSetLayout) \
    Function(CreateDescriptorPool) Function(DestroyDescriptorPool) Function(AllocateDescriptorSets) Function(UpdateDescriptorSets) \
    Function(CreatePipelineLayout) Function(DestroyPipelineLayout) Function(CreateShaderModule) Function(DestroyShaderModule) \
    Function(CreateComputePipelines) Function(DestroyPipeline) Function(CmdPipelineBarrier) Function(CmdBindPipeline) \
    Function(CmdBindDescriptorSets) Function(CmdPushConstants) Function(CmdDispatch) Function(CmdCopyImageToBuffer)
#define FRONTIER_COVERAGE_POINTER(Name) PFN_vk##Name Name = nullptr;
    FRONTIER_COVERAGE_FUNCTIONS(FRONTIER_COVERAGE_POINTER)
#undef FRONTIER_COVERAGE_POINTER
    ~CoverageAllocation()
    {
        if (!Access.Device) return;
        if (Pipeline) DestroyPipeline(Access.Device, Pipeline, nullptr);
        if (Layout) DestroyPipelineLayout(Access.Device, Layout, nullptr);
        if (Pool) DestroyDescriptorPool(Access.Device, Pool, nullptr);
        if (DescriptorLayout) DestroyDescriptorSetLayout(Access.Device, DescriptorLayout, nullptr);
        if (View) DestroyImageView(Access.Device, View, nullptr);
        if (Image) DestroyImage(Access.Device, Image, nullptr);
        if (Instructions) DestroyBuffer(Access.Device, Instructions, nullptr);
        if (Segments) DestroyBuffer(Access.Device, Segments, nullptr);
        if (TileDirectory) DestroyBuffer(Access.Device, TileDirectory, nullptr);
        if (TileIndices) DestroyBuffer(Access.Device, TileIndices, nullptr);
        if (ReadbackBuffer) DestroyBuffer(Access.Device, ReadbackBuffer, nullptr);
        for (auto Storage : {InstructionMemory, SegmentMemory, TileDirectoryMemory, TileIndexMemory, ImageMemory, ReadbackMemory})
            if (Storage) FreeMemory(Access.Device, Storage, nullptr);
    }
    void Allocate(VkMemoryRequirements Requirements, VkMemoryPropertyFlags Properties, VkDeviceMemory &Storage)
    {
        uint32_t Selection = UINT32_MAX;
        for (uint32_t Index = 0; Index < Memory.memoryTypeCount; ++Index)
            if ((Requirements.memoryTypeBits & (1u << Index)) && (Memory.memoryTypes[Index].propertyFlags & Properties) == Properties) { Selection = Index; break; }
        if (Selection == UINT32_MAX) throw std::runtime_error("Vulkan vector memory category is unavailable.");
        VkMemoryAllocateInfo Creation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        Creation.allocationSize = Requirements.size; Creation.memoryTypeIndex = Selection;
        ConfirmCoverageResult(AllocateMemory(Access.Device, &Creation, nullptr, &Storage));
    }
    void AllocateBuffer(VkDeviceSize Bytes, VkBuffer &Buffer, VkDeviceMemory &Storage)
    {
        VkBufferCreateInfo Creation{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        Creation.size = Bytes; Creation.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        ConfirmCoverageResult(CreateBuffer(Access.Device, &Creation, nullptr, &Buffer));
        VkMemoryRequirements Requirements; GetBufferMemoryRequirements(Access.Device, Buffer, &Requirements);
        Allocate(Requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, Storage);
        ConfirmCoverageResult(BindBufferMemory(Access.Device, Buffer, Storage, 0));
    }
    void AllocateReadbackBuffer()
    {
        ReadbackBytes = VkDeviceSize(Extent.width) * Extent.height * 4 * sizeof(float);
        VkBufferCreateInfo Creation{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        Creation.size = ReadbackBytes;
        Creation.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        ConfirmCoverageResult(CreateBuffer(Access.Device, &Creation, nullptr, &ReadbackBuffer));
        VkMemoryRequirements Requirements;
        GetBufferMemoryRequirements(Access.Device, ReadbackBuffer, &Requirements);
        Allocate(Requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ReadbackMemory);
        ConfirmCoverageResult(BindBufferMemory(Access.Device, ReadbackBuffer, ReadbackMemory, 0));
    }
    void Upload(VkDeviceMemory Storage, const void *Source, size_t Bytes)
    {
        if (!Bytes) return;
        void *Destination = nullptr;
        ConfirmCoverageResult(MapMemory(Access.Device, Storage, 0, Bytes, 0, &Destination));
        std::memcpy(Destination, Source, Bytes); UnmapMemory(Access.Device, Storage);
    }
    // 📝 Antialias must match VectorCoverageCompute/VectorSurfaceCompute's own 512/min(Width,Height) exactly
    // (VectorSelectionCompute's single pixel instead collapses to one tile covering the whole canvas
    // regardless of padding, so the same formula is safe to reuse there too). Rebuilding and reuploading
    // both buffers in full every call keeps Directory offsets always self-consistent with this call's
    // Indices contents, even though the Indices buffer is sized once for the worst case and never shrinks.
    void UploadTileBinning(const VectorCoverageProgram &Program)
    {
        const float AntiAlias = 512.f / float(std::min(Extent.width, Extent.height));
        const auto Binning = BuildVectorCoverageTileBinning(Program, Extent.width, Extent.height, AntiAlias);
        Upload(TileDirectoryMemory, Binning.Directory.data(), Binning.Directory.size() * sizeof(VectorCoverageTileDirectoryEntry));
        if (!Binning.Indices.empty())
            Upload(TileIndexMemory, Binning.Indices.data(), Binning.Indices.size() * sizeof(uint32_t));
    }
};
VectorCoverageRasterization::VectorCoverageRasterization() = default;
VectorCoverageRasterization::~VectorCoverageRasterization() = default;
void VectorCoverageRasterization::Initialize(const VulkanRasterAccess &Access, RasterShader Shader, uint32_t Width, uint32_t Height)
{
    if (Allocation || !Access.Device || !Access.PhysicalDevice || !Access.ResolveDevice || !Access.QueryMemory || !Access.QueryFormat || !Access.QueryLimits || Shader.Words.size() < 5 || Shader.Words.size() > 4 * 1024 * 1024 || Shader.Words[0] != 0x07230203 || !Shader.Entry || !Width || !Height || Width > 4096 || Height > 4096)
        throw std::runtime_error("Invalid Vulkan vector initialization.");
    if (!(Access.QueueCapabilities & VK_QUEUE_COMPUTE_BIT)) throw std::runtime_error("Vector coverage requires an explicitly verified compute-capable command queue.");
    bool EntryPresent = false;
    VkFormat OutputFormat = VK_FORMAT_UNDEFINED;
    for (size_t Offset = 5; Offset < Shader.Words.size();)
    {
        const size_t Words = Shader.Words[Offset] >> 16;
        if (!Words || Words > Shader.Words.size() - Offset) throw std::runtime_error("Malformed vector SPIR-V instruction.");
        if ((Shader.Words[Offset] & 65535) == 15 && Words >= 4 && Shader.Words[Offset + 1] == 5)
        {
            const auto *Name = reinterpret_cast<const char *>(Shader.Words.data() + Offset + 3);
            const size_t Bytes = (Words - 3) * sizeof(uint32_t);
            const auto *Terminator = static_cast<const char *>(std::memchr(Name, 0, Bytes));
            if (Terminator && std::string_view(Name, size_t(Terminator - Name)) == Shader.Entry) EntryPresent = true;
        }
        if ((Shader.Words[Offset] & 65535) == 25 && Words >= 9 && Shader.Words[Offset + 7] == 2)
        {
            const uint32_t ImageFormat = Shader.Words[Offset + 8];
            const VkFormat Declared = ImageFormat == 1 ? VK_FORMAT_R32G32B32A32_SFLOAT
                : ImageFormat == 4 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_UNDEFINED;
            if (Declared == VK_FORMAT_UNDEFINED || (OutputFormat != VK_FORMAT_UNDEFINED && OutputFormat != Declared))
                throw std::runtime_error("Unsupported or conflicting vector storage-image format.");
            OutputFormat = Declared;
        }
        Offset += Words;
    }
    if (!EntryPresent || OutputFormat == VK_FORMAT_UNDEFINED)
        throw std::runtime_error("Vector shader entry or typed storage image is absent.");
    auto Resident = std::make_unique<CoverageAllocation>();
    Resident->Access = Access;
#define FRONTIER_COVERAGE_RESOLVE(Name) Resident->Name = reinterpret_cast<PFN_vk##Name>(Access.ResolveDevice(Access.Device, "vk" #Name)); if (!Resident->Name) throw std::runtime_error("Missing Vulkan vector function: vk" #Name);
    FRONTIER_COVERAGE_FUNCTIONS(FRONTIER_COVERAGE_RESOLVE)
#undef FRONTIER_COVERAGE_RESOLVE
    Access.QueryMemory(Access.PhysicalDevice, &Resident->Memory);
    VkPhysicalDeviceProperties Limits{}; Access.QueryLimits(Access.PhysicalDevice, &Limits);
    if (Width > Limits.limits.maxImageDimension2D || Height > Limits.limits.maxImageDimension2D || Limits.limits.maxStorageBufferRange < 32768 * sizeof(VectorCubicSegment) || Limits.limits.maxPushConstantsSize < (OutputFormat == VK_FORMAT_R32G32B32A32_SFLOAT ? 32u : 24u) || Limits.limits.maxPerStageDescriptorStorageBuffers < 4 || Limits.limits.maxDescriptorSetStorageBuffers < 4 || Limits.limits.maxPerStageDescriptorStorageImages < 1 || Limits.limits.maxDescriptorSetStorageImages < 1 || Limits.limits.maxBoundDescriptorSets < 1 || (Width + 7) / 8 > Limits.limits.maxComputeWorkGroupCount[0] || (Height + 7) / 8 > Limits.limits.maxComputeWorkGroupCount[1] || Limits.limits.maxComputeWorkGroupInvocations < 64 || Limits.limits.maxComputeWorkGroupSize[0] < 8 || Limits.limits.maxComputeWorkGroupSize[1] < 8)
        throw std::runtime_error("Vulkan device cannot support bounded vector coverage.");
    VkFormatProperties Format{}; Access.QueryFormat(Access.PhysicalDevice, OutputFormat, &Format);
    const auto Required = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    if ((Format.optimalTilingFeatures & Required) != Required)
        throw std::runtime_error("Typed vector storage/sampling/readback/filtering is unavailable.");
    Resident->Extent = {Width, Height};
    Resident->OutputFormat = OutputFormat;
    Resident->PushConstantBytes = OutputFormat == VK_FORMAT_R32G32B32A32_SFLOAT ? 32u : 24u;
    Resident->AllocateBuffer((VectorCoverageInstructionCapacity + VectorCoverageExtensionCapacity) * sizeof(VectorCoverageInstruction),
        Resident->Instructions, Resident->InstructionMemory);
    Resident->AllocateBuffer(32768 * sizeof(VectorCubicSegment), Resident->Segments, Resident->SegmentMemory);
    // 📝 Fixed for this rasterization's lifetime (Width/Height never change after Initialize); the worst-case
    // Indices sizing (every one of the up to VectorCoverageInstructionCapacity leaves reaching every tile)
    // is cheap even at the largest pooled bake extent and is never exceeded by BuildVectorCoverageTileBinning.
    const uint32_t TilesPerRow = (Width + VectorCoverageTileSize - 1) / VectorCoverageTileSize;
    const uint32_t TilesPerColumn = (Height + VectorCoverageTileSize - 1) / VectorCoverageTileSize;
    Resident->TilesPerRow = TilesPerRow;
    Resident->AllocateBuffer(uint64_t(TilesPerRow) * TilesPerColumn * sizeof(VectorCoverageTileDirectoryEntry),
        Resident->TileDirectory, Resident->TileDirectoryMemory);
    Resident->AllocateBuffer(uint64_t(TilesPerRow) * TilesPerColumn * VectorCoverageInstructionCapacity * sizeof(uint32_t),
        Resident->TileIndices, Resident->TileIndexMemory);
    VkImageCreateInfo Image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    Image.imageType = VK_IMAGE_TYPE_2D; Image.format = OutputFormat;
    Image.extent = {Width, Height, 1}; Image.mipLevels = Image.arrayLayers = 1;
    Image.samples = VK_SAMPLE_COUNT_1_BIT; Image.tiling = VK_IMAGE_TILING_OPTIMAL;
    Image.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ConfirmCoverageResult(Resident->CreateImage(Access.Device, &Image, nullptr, &Resident->Image));
    VkMemoryRequirements Requirements; Resident->GetImageMemoryRequirements(Access.Device, Resident->Image, &Requirements);
    Resident->Allocate(Requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Resident->ImageMemory);
    ConfirmCoverageResult(Resident->BindImageMemory(Access.Device, Resident->Image, Resident->ImageMemory, 0));
    VkImageViewCreateInfo View{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    View.image = Resident->Image; View.viewType = VK_IMAGE_VIEW_TYPE_2D; View.format = OutputFormat;
    View.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    ConfirmCoverageResult(Resident->CreateImageView(Access.Device, &View, nullptr, &Resident->View));
    const VkDescriptorSetLayoutBinding Bindings[]{{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo Descriptor{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    Descriptor.bindingCount = 5; Descriptor.pBindings = Bindings;
    ConfirmCoverageResult(Resident->CreateDescriptorSetLayout(Access.Device, &Descriptor, nullptr, &Resident->DescriptorLayout));
    const VkDescriptorPoolSize PoolSizes[]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};
    VkDescriptorPoolCreateInfo Pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    Pool.maxSets = 1; Pool.poolSizeCount = 2; Pool.pPoolSizes = PoolSizes;
    ConfirmCoverageResult(Resident->CreateDescriptorPool(Access.Device, &Pool, nullptr, &Resident->Pool));
    VkDescriptorSetAllocateInfo Selection{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    Selection.descriptorPool = Resident->Pool; Selection.descriptorSetCount = 1; Selection.pSetLayouts = &Resident->DescriptorLayout;
    ConfirmCoverageResult(Resident->AllocateDescriptorSets(Access.Device, &Selection, &Resident->Descriptor));
    const VkDescriptorBufferInfo InstructionBuffer{Resident->Instructions, 0, VK_WHOLE_SIZE}, SegmentBuffer{Resident->Segments, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo TileDirectoryBuffer{Resident->TileDirectory, 0, VK_WHOLE_SIZE}, TileIndexBuffer{Resident->TileIndices, 0, VK_WHOLE_SIZE};
    const VkDescriptorImageInfo Output{VK_NULL_HANDLE, Resident->View, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet Writes[5]{};
    for (uint32_t Index = 0; Index < 5; ++Index) { Writes[Index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; Writes[Index].dstSet = Resident->Descriptor; Writes[Index].dstBinding = Index; Writes[Index].descriptorCount = 1; Writes[Index].descriptorType = Bindings[Index].descriptorType; }
    Writes[0].pBufferInfo = &InstructionBuffer; Writes[1].pImageInfo = &Output; Writes[2].pBufferInfo = &SegmentBuffer;
    Writes[3].pBufferInfo = &TileDirectoryBuffer; Writes[4].pBufferInfo = &TileIndexBuffer;
    Resident->UpdateDescriptorSets(Access.Device, 5, Writes, 0, nullptr);
    VkPushConstantRange Projection{VK_SHADER_STAGE_COMPUTE_BIT, 0, Resident->PushConstantBytes};
    VkPipelineLayoutCreateInfo Layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    Layout.setLayoutCount = 1; Layout.pSetLayouts = &Resident->DescriptorLayout; Layout.pushConstantRangeCount = 1; Layout.pPushConstantRanges = &Projection;
    ConfirmCoverageResult(Resident->CreatePipelineLayout(Access.Device, &Layout, nullptr, &Resident->Layout));
    VkShaderModule ShaderLibrary = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo Source{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    Source.codeSize = Shader.Words.size_bytes(); Source.pCode = Shader.Words.data();
    ConfirmCoverageResult(Resident->CreateShaderModule(Access.Device, &Source, nullptr, &ShaderLibrary));
    VkComputePipelineCreateInfo Pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    Pipeline.layout = Resident->Layout;
    Pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, ShaderLibrary, Shader.Entry, nullptr};
    const auto Result = Resident->CreateComputePipelines(Access.Device, VK_NULL_HANDLE, 1, &Pipeline, nullptr, &Resident->Pipeline);
    Resident->DestroyShaderModule(Access.Device, ShaderLibrary, nullptr); ConfirmCoverageResult(Result);
    Allocation = std::move(Resident);
}
void VectorCoverageRasterization::ConfigureRetiredProgram(const VectorCoverageProgram &Program, uint64_t CompletedSerial)
{
    ValidateVectorCoverage(Program);
    if (!Allocation || Allocation->LastSubmission > CompletedSerial || Allocation->Recorded || Allocation->ReadbackRecorded)
        throw std::runtime_error("Vector descriptors are not retired; defer the upload without waiting.");
    auto &Resident = *Allocation;
    Resident.Upload(Resident.InstructionMemory, Program.Instructions.data(), Program.Instructions.size() * sizeof(VectorCoverageInstruction));
    Resident.Upload(Resident.SegmentMemory, Program.Segments.data(), Program.Segments.size() * sizeof(VectorCubicSegment));
    Resident.UploadTileBinning(Program);
    Resident.Count = uint32_t(Program.Instructions.size()); Resident.Root = Program.Root; Resident.Dirty = true;
}
void VectorCoverageRasterization::ConfigureRetiredSurface(const VectorSurfaceProgram &Program, uint64_t CompletedSerial)
{
    ValidateVectorSurface(Program);
    if (!Allocation || Allocation->OutputFormat != VK_FORMAT_R32G32B32A32_SFLOAT || Allocation->PushConstantBytes != 32 ||
        Allocation->LastSubmission > CompletedSerial || Allocation->Recorded || Allocation->ReadbackRecorded)
        throw std::runtime_error("Float32 surface descriptors are unavailable or not retired.");
    auto &Resident = *Allocation;
    std::array<VectorCoverageInstruction, VectorCoverageInstructionCapacity + VectorCoverageExtensionCapacity> Upload{};
    std::copy(Program.Coverage.Instructions.begin(), Program.Coverage.Instructions.end(), Upload.begin());
    for (size_t Index = 0; Index < Program.Contributions.size(); ++Index)
    {
        const auto &Contribution = Program.Contributions[Index];
        auto &Instruction = Upload[VectorCoverageInstructionCapacity + Index];
        uint32_t Enabled = 0;
        std::array<float, 7> Strengths{};
        for (size_t ChannelIndex = 0; ChannelIndex < Strengths.size(); ++ChannelIndex)
        {
            const auto &Channel = Contribution.Channels[ChannelIndex];
            if (Channel.Enabled) Enabled |= 1u << ChannelIndex;
            Strengths[ChannelIndex] = Channel.Strength;
        }
        Instruction.Topology = {Contribution.Root, Contribution.Token, Enabled, 0};
        Instruction.Bounds = {Strengths[0], Strengths[1], Strengths[2], Strengths[3]};
        Instruction.Transform = {Strengths[4], Strengths[5], Strengths[6], Contribution.SurfaceRelief};
    }
    Resident.Upload(Resident.InstructionMemory, Upload.data(), sizeof(Upload));
    Resident.Upload(Resident.SegmentMemory, Program.Coverage.Segments.data(),
        Program.Coverage.Segments.size() * sizeof(VectorCubicSegment));
    Resident.UploadTileBinning(Program.Coverage);
    Resident.Count = uint32_t(Program.Coverage.Instructions.size());
    Resident.Root = Program.Coverage.Root;
    Resident.Channel = uint32_t(Program.Channel);
    Resident.Contributions = uint32_t(Program.Contributions.size());
    Resident.Dirty = true;
}

void VectorCoverageRasterization::ConfigureRetiredSelection(const VectorSelectionProgram &Program, std::array<float, 2> Pointer, uint64_t CompletedSerial)
{
    ValidateVectorSelection(Program);
    if (!std::isfinite(Pointer[0]) || !std::isfinite(Pointer[1])) throw std::runtime_error("Nonfinite vector selection pointer.");
    if (!Allocation || Allocation->Extent.width != 1 || Allocation->Extent.height != 1)
        throw std::runtime_error("Selection evaluation requires a one-texel output.");
    ConfigureRetiredProgram(Program.Coverage, CompletedSerial);
    std::array<VectorCoverageInstruction, VectorCoverageInstructionCapacity + VectorCoverageExtensionCapacity> Upload{};
    std::copy(Program.Coverage.Instructions.begin(), Program.Coverage.Instructions.end(), Upload.begin());
    for (size_t Index = 0; Index < Program.Candidates.size(); ++Index)
        Upload[VectorCoverageInstructionCapacity + Index].Topology = {Program.Candidates[Index][0], Program.Candidates[Index][1], 0, 0};
    Upload[VectorCoverageInstructionCapacity].Bounds = {Pointer[0], Pointer[1], 1, 0};
    Allocation->Upload(Allocation->InstructionMemory, Upload.data(), sizeof(Upload));
    Allocation->Root = uint32_t(Program.Candidates.size());
}
bool VectorCoverageRasterization::ConstructCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial)
{
    if (!Allocation || !Allocation->Dirty) return false;
    auto &Resident = *Allocation;
    if (!Commands || Resident.LastSubmission > CompletedSerial || SubmissionSerial <= CompletedSerial ||
        Resident.Recorded || Resident.ReadbackRecorded) throw std::runtime_error("Vector output is not retired or recording serial is invalid.");
    VkImageMemoryBarrier Barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    Barrier.srcAccessMask = Resident.Ready ? VK_ACCESS_SHADER_READ_BIT : 0; Barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    Barrier.oldLayout = Resident.Ready ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED; Barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    Barrier.srcQueueFamilyIndex = Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image = Resident.Image; Barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    Resident.CmdPipelineBarrier(Commands, Resident.Ready ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &Barrier);
    Resident.CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Resident.Pipeline);
    Resident.CmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Resident.Layout, 0, 1, &Resident.Descriptor, 0, nullptr);
    // 📝 Order must match both CoverageProjection layouts exactly: VectorCoverage.slang reads the first six
    // words (Width, Height, Count, Root, TileSize, TilesPerRow); VectorSurface.slang reads all eight.
    const uint32_t Projection[]{Resident.Extent.width, Resident.Extent.height, Resident.Count, Resident.Root,
        VectorCoverageTileSize, Resident.TilesPerRow, Resident.Channel, Resident.Contributions};
    Resident.CmdPushConstants(Commands, Resident.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
        Resident.PushConstantBytes, Projection);
    Resident.CmdDispatch(Commands, (Resident.Extent.width + 7) / 8, (Resident.Extent.height + 7) / 8, 1);
    Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    Barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL; Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    Resident.CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &Barrier);
    Resident.Recorded = true; Resident.PendingSubmission = SubmissionSerial;
    return true;
}
void VectorCoverageRasterization::ConstructReadbackCommands(VkCommandBuffer Commands,uint64_t CompletedSerial,
    uint64_t SubmissionSerial)
{
    if (!Allocation || Allocation->OutputFormat != VK_FORMAT_R32G32B32A32_SFLOAT || !Commands ||
        (!Allocation->Ready && !Allocation->Recorded) || Allocation->ReadbackRecorded || Allocation->ReadbackAvailable ||
        SubmissionSerial <= CompletedSerial || (!Allocation->Recorded && Allocation->LastSubmission > CompletedSerial) ||
        (Allocation->Recorded && Allocation->PendingSubmission != SubmissionSerial))
        throw std::runtime_error("Float32 vector output is not ready for a retired readback recording.");
    auto &Resident = *Allocation;
    if (!Resident.ReadbackBuffer) Resident.AllocateReadbackBuffer();
    VkImageMemoryBarrier Barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    Barrier.srcAccessMask = Resident.Recorded ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
    Barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    Barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    Barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    Barrier.srcQueueFamilyIndex = Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image = Resident.Image;
    Barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    Resident.CmdPipelineBarrier(Commands,Resident.Recorded ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&Barrier);
    VkBufferImageCopy Region{};
    Region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
    Region.imageExtent = {Resident.Extent.width,Resident.Extent.height,1};
    Resident.CmdCopyImageToBuffer(Commands,Resident.Image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        Resident.ReadbackBuffer,1,&Region);
    Barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    Barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    Resident.CmdPipelineBarrier(Commands,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,0,nullptr,0,nullptr,1,&Barrier);
    VkMemoryBarrier Host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    Host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    Host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    Resident.CmdPipelineBarrier(Commands,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
        0,1,&Host,0,nullptr,0,nullptr);
    if (!Resident.Recorded) Resident.PendingSubmission = SubmissionSerial;
    Resident.ReadbackRecorded = true;
}

std::vector<float> VectorCoverageRasterization::RetrieveFloatReadback(uint64_t CompletedSerial)
{
    if (!Allocation || !Allocation->ReadbackAvailable || Allocation->ReadbackSubmission > CompletedSerial)
        throw std::runtime_error("Float32 vector readback has not retired.");
    auto &Resident = *Allocation;
    void *Mapped = nullptr;
    ConfirmCoverageResult(Resident.MapMemory(Resident.Access.Device,Resident.ReadbackMemory,0,
        Resident.ReadbackBytes,0,&Mapped));
    std::vector<float> Samples(size_t(Resident.ReadbackBytes / sizeof(float)));
    std::memcpy(Samples.data(),Mapped,size_t(Resident.ReadbackBytes));
    Resident.UnmapMemory(Resident.Access.Device,Resident.ReadbackMemory);
    Resident.ReadbackAvailable = false;
    return Samples;
}

void VectorCoverageRasterization::ConfirmSubmission(uint64_t SampledSerial) noexcept
{
    if (!Allocation) return;
    auto &Resident = *Allocation;
    if (Resident.Recorded || Resident.ReadbackRecorded)
    {
        Resident.LastSubmission = Resident.PendingSubmission;
        if (Resident.Recorded)
        {
            Resident.Recorded = false;
            Resident.Ready = true;
            Resident.Dirty = false;
            ++Resident.Dispatches;
        }
        if (Resident.ReadbackRecorded)
        {
            Resident.ReadbackRecorded = false;
            Resident.ReadbackAvailable = true;
            Resident.ReadbackSubmission = Resident.PendingSubmission;
        }
    }
    if (Resident.Ready && SampledSerial > Resident.LastSubmission) Resident.LastSubmission = SampledSerial;
}
void VectorCoverageRasterization::Finalize() noexcept { Allocation.reset(); }
VkImage VectorCoverageRasterization::QueryImage() const noexcept { return Allocation ? Allocation->Image : VK_NULL_HANDLE; }
VkImageView VectorCoverageRasterization::QueryImageView() const noexcept { return Allocation ? Allocation->View : VK_NULL_HANDLE; }
VkExtent2D VectorCoverageRasterization::QueryExtent() const noexcept { return Allocation ? Allocation->Extent : VkExtent2D{}; }
uint64_t VectorCoverageRasterization::QueryDispatchCount() const noexcept { return Allocation ? Allocation->Dispatches : 0; }
}

namespace Frontier
{
namespace
{
constexpr std::array<VectorAuthoringSurfaceChannel,6> AuthoringMaterialChannels{
    VectorAuthoringSurfaceChannel::Colour,VectorAuthoringSurfaceChannel::Normal,
    VectorAuthoringSurfaceChannel::Height,VectorAuthoringSurfaceChannel::Roughness,
    VectorAuthoringSurfaceChannel::Metalness,VectorAuthoringSurfaceChannel::Occlusion};
}

uint32_t VectorCoverageRasterization::SelectBakeExtent(size_t Segments, uint32_t Channels, uint32_t DisplayExtent)
{
    if (!Channels) throw std::runtime_error("Bake budget requires at least one channel.");
    // 📝 ~500 FLOP per curve per pixel with no bounds rejection; 2e9 curve·
    // pixel·channels ≈ 1 TFLOP per submit, under the Windows watchdog even on
    // integrated GPUs. Empty/rect-only documents keep full resolution. The
    // single-channel canvas display may run up to 4096 (both Pattern's and
    // Stamp's own canvas already clamp their zoomed, device-pixel display size
    // to that same ceiling, and VectorCoverageRasterization::Initialize's own
    // image-size validation already allows up to 4096x4096) so a zoomed-in
    // canvas still bakes vector-crisp instead of a smaller raster being
    // stretched past its own resolution — this previously topped out at 2048,
    // one tier short of what the canvas could actually ask for: any zoom past
    // ~4x on a 512-unit canvas (the reported repro was 800%, ~4096 device
    // pixels) kept baking at a frozen 2048 while the displayed quad kept
    // growing, a stretched raster visibly pixelating past that point. The
    // six-channel bridge still tops out at 512 because its pyramid restores
    // material maps at that extent.
    uint32_t Affordable;
    if (!Segments) Affordable = Channels == 1 ? 4096 : 512;
    else
    {
        constexpr uint64_t Budget = 2000000000ull;
        const uint64_t Load = uint64_t(Segments) * Channels;
        Affordable = 128;
        if (Channels == 1)
        {
            for (uint32_t Extent : {4096u, 2048u, 1024u, 512u, 256u, 128u})
                if (uint64_t(Extent) * Extent * Load <= Budget) { Affordable = Extent; break; }
        }
        else
            for (uint32_t Extent : {512u, 256u, 128u})
                if (uint64_t(Extent) * Extent * Load <= Budget) { Affordable = Extent; break; }
    }
    if (!DisplayExtent) return Affordable;
    // 📝 A live, zoom-aware canvas asks for the device-pixel size it is actually displayed at: pick
    // the smallest pooled tier that still covers it, never exceeding what the budget above affords,
    // so zooming in re-bakes crisper (vector-accurate, not a stretched raster) and zooming out drops
    // back down instead of holding a needlessly large image.
    for (uint32_t Extent : {128u, 256u, 512u, 1024u, 2048u, 4096u})
        if (Extent >= DisplayExtent) return std::min(Extent, Affordable);
    return Affordable;
}

VectorSurfaceMaterialBridge::VectorSurfaceMaterialBridge() = default;
VectorSurfaceMaterialBridge::~VectorSurfaceMaterialBridge() = default;
void VectorSurfaceMaterialBridge::Initialize(const VulkanRasterAccess &Access, const std::filesystem::path &ShaderArchive, uint32_t Width, uint32_t Height)
{
    std::ifstream Input(ShaderArchive, std::ios::binary | std::ios::ate);
    const auto Bytes = Input.tellg();
    if (!Input || Bytes < 20 || Bytes > 16777216 || Bytes % 4)
        throw std::runtime_error("Missing or malformed Float32 material bridge shader archive.");
    std::vector<uint32_t> Words(size_t(Bytes) / 4);
    Input.seekg(0);
    if (!Input.read(reinterpret_cast<char *>(Words.data()), Bytes) || Words[0] != 0x07230203)
        throw std::runtime_error("Invalid Float32 material bridge SPIR-V archive.");
    Initialize(Access, RasterShader{Words, "main"}, Width, Height);
}

void VectorSurfaceMaterialBridge::Initialize(const VulkanRasterAccess &Access,RasterShader Shader,uint32_t Width,uint32_t Height)
{
    if (Initialized) throw std::runtime_error("Float32 material bridge already initialized.");
    if (Width != Height) throw std::runtime_error("Float32 material bridge requires a square bake extent.");
    RetainedAccess = Access;
    RetainedShaderWords.assign(Shader.Words.begin(), Shader.Words.end());
    RetainedShaderEntry = Shader.Entry ? Shader.Entry : "";
    size_t InitializedCount = 0;
    try
    {
        for (auto &Rasterization : Rasterizations)
        {
            Rasterization.Initialize(Access,Shader,Width,Height);
            ++InitializedCount;
        }
    }
    catch (...)
    {
        for (size_t Index = 0; Index < InitializedCount; ++Index) Rasterizations[Index].Finalize();
        throw;
    }
    BakeExtent = Width;
    Initialized = true;
}

bool VectorSurfaceMaterialBridge::ConfigureDocument(const VectorAuthoringDocument &Document,uint64_t CompletedSerial)
{
    if (!Initialized || Recorded || ReadbacksPending || LastSubmission > CompletedSerial)
        throw std::runtime_error("Float32 material bridge still owns an unretired authoring submission.");
    // 📝 Dense weaves would exceed the OS GPU watchdog at full resolution;
    // re-bracket retired rasterizations before compiling the new document.
    const uint32_t Needed = VectorCoverageRasterization::SelectBakeExtent(Document.Segments.size(), 6);
    if (Needed != BakeExtent)
    {
        for (auto &Rasterization : Rasterizations)
        {
            Rasterization.Finalize();
            Rasterization.Initialize(RetainedAccess, RasterShader{RetainedShaderWords, RetainedShaderEntry.c_str()}, Needed, Needed);
        }
        DiagnosticJournal::Append("PatternBake", "bake extent " + std::to_string(BakeExtent) + "->" + std::to_string(Needed) +
            " for " + std::to_string(Document.Segments.size()) + " segments");
        BakeExtent = Needed;
    }
    const auto CompileStarted = std::chrono::steady_clock::now();
    std::array<VectorSurfaceProgram,6> Replacement;
    for (size_t Index = 0; Index < Replacement.size(); ++Index)
        Replacement[Index] = CompileVectorSurface(Document,AuthoringMaterialChannels[Index]);
    DiagnosticJournal::Append("PatternBake", "compiled segments=" + std::to_string(Document.Segments.size()) +
        " extent=" + std::to_string(BakeExtent) + " compile_ms=" +
        std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - CompileStarted).count()));
    if (ProgramsConfigured && Replacement == Programs) return false;
    for (size_t Index = 0; Index < Replacement.size(); ++Index)
        Rasterizations[Index].ConfigureRetiredSurface(Replacement[Index],CompletedSerial);
    Programs = std::move(Replacement);
    ProgramsConfigured = true;
    CompiledMaps.reset();
    Dirty = true;
    return true;
}

bool VectorSurfaceMaterialBridge::ConstructCommands(VkCommandBuffer Commands,uint64_t CompletedSerial,uint64_t SubmissionSerial)
{
    if (!Initialized || !ProgramsConfigured || !Dirty) return false;
    if (!Commands || Recorded || LastSubmission > CompletedSerial || SubmissionSerial <= CompletedSerial)
        throw std::runtime_error("Float32 material bridge command serial is invalid or still in flight.");
    for (auto &Rasterization : Rasterizations)
    {
        if (!Rasterization.ConstructCommands(Commands,CompletedSerial,SubmissionSerial))
            throw std::runtime_error("A Float32 authoring channel did not dispatch with its material peers.");
        Rasterization.ConstructReadbackCommands(Commands,CompletedSerial,SubmissionSerial);
    }
    PendingSubmission = SubmissionSerial;
    Recorded = true;
    return true;
}

void VectorSurfaceMaterialBridge::ConfirmSubmission() noexcept
{
    if (!Recorded) return;
    for (auto &Rasterization : Rasterizations) Rasterization.ConfirmSubmission(PendingSubmission);
    LastSubmission = PendingSubmission;
    PendingSubmission = 0;
    Recorded = false;
    Dirty = false;
    ReadbacksPending = true;
}

std::vector<float> VectorSurfaceMaterialBridge::UpsampleFloatRgba(std::span<const float> Source, uint32_t Width,
    uint32_t Height, uint32_t TargetExtent, bool RenormalizeNormals)
{
    if (!Width || !Height || !TargetExtent || Width > 512 || Height > 512 || TargetExtent > 512 ||
        TargetExtent < Width || TargetExtent < Height || Source.size() != size_t(Width) * Height * 4)
        throw std::runtime_error("Float32 map upsample requires bounded source pixels and a covering square target.");
    for (float Sample : Source)
        if (!std::isfinite(Sample)) throw std::runtime_error("Float32 map upsample rejected non-finite source pixels.");
    std::vector<float> Target(size_t(TargetExtent) * TargetExtent * 4);
    for (uint32_t Row = 0; Row < TargetExtent; ++Row)
        for (uint32_t Column = 0; Column < TargetExtent; ++Column)
        {
            const double X = TargetExtent > 1 ? double(Column) * (Width - 1) / (TargetExtent - 1) : 0;
            const double Y = TargetExtent > 1 ? double(Row) * (Height - 1) / (TargetExtent - 1) : 0;
            const uint32_t Left = uint32_t(X), Top = uint32_t(Y);
            const uint32_t Right = std::min(Left + 1, Width - 1), Bottom = std::min(Top + 1, Height - 1);
            const double Horizontal = X - Left, Vertical = Y - Top;
            std::array<double, 4> Spectrum{};
            for (size_t Channel = 0; Channel < 4; ++Channel)
            {
                const auto Fetch = [&](uint32_t FetchColumn, uint32_t FetchRow)
                {
                    double Coefficient = Source[(size_t(FetchRow) * Width + FetchColumn) * 4 + Channel];
                    if (RenormalizeNormals && Channel < 3) Coefficient = Coefficient * 2 - 1;
                    return Coefficient;
                };
                Spectrum[Channel] = Fetch(Left, Top) * (1 - Horizontal) * (1 - Vertical) + Fetch(Right, Top) * Horizontal * (1 - Vertical) +
                    Fetch(Left, Bottom) * (1 - Horizontal) * Vertical + Fetch(Right, Bottom) * Horizontal * Vertical;
            }
            if (RenormalizeNormals)
            {
                const double Length = std::sqrt(Spectrum[0] * Spectrum[0] + Spectrum[1] * Spectrum[1] + Spectrum[2] * Spectrum[2]);
                for (size_t Channel = 0; Channel < 3; ++Channel)
                    Spectrum[Channel] = (Length > 1e-8 ? Spectrum[Channel] / Length : double(Channel == 2)) * .5 + .5;
            }
            for (size_t Channel = 0; Channel < 4; ++Channel)
                Target[(size_t(Row) * TargetExtent + Column) * 4 + Channel] = float(Spectrum[Channel]);
        }
    return Target;
}

std::array<MaterialTexturePyramid,6> VectorSurfaceMaterialBridge::RetrieveAuthoringMaps(uint64_t CompletedSerial)
{
    if (CompiledMaps) return *CompiledMaps;
    if (!ReadbacksPending || LastSubmission > CompletedSerial)
        throw std::runtime_error("Float32 authoring maps have not retired from their GPU submission.");
    const auto PyramidStarted = std::chrono::steady_clock::now();
    std::array<std::vector<float>,6> Samples;
    std::array<std::span<const float>,6> SampleSpans;
    for (size_t Index = 0; Index < Samples.size(); ++Index)
    {
        Samples[Index] = Rasterizations[Index].RetrieveFloatReadback(CompletedSerial);
        SampleSpans[Index] = Samples[Index];
    }
    const auto Extent = Rasterizations[0].QueryExtent();
    for (const auto &Rasterization : Rasterizations)
        if (Rasterization.QueryExtent().width != Extent.width || Rasterization.QueryExtent().height != Extent.height)
            throw std::runtime_error("Float32 material channel extents diverged.");
    // 📝 Reduced buckets are restored to full material resolution so pyramid
    // shapes never change beneath the preview and thumbnail samplers.
    std::array<std::vector<float>,6> Restored;
    std::array<std::span<const float>,6> PyramidSpans;
    for (size_t Index = 0; Index < Restored.size(); ++Index)
    {
        if (Extent.width == 512 && Extent.height == 512) PyramidSpans[Index] = SampleSpans[Index];
        else
        {
            Restored[Index] = UpsampleFloatRgba(SampleSpans[Index], Extent.width, Extent.height, 512,
                AuthoringMaterialChannels[Index] == VectorAuthoringSurfaceChannel::Normal);
            PyramidSpans[Index] = Restored[Index];
        }
    }
    CompiledMaps = ConstructAuthoringMaterialMaps(512,512,PyramidSpans);
    DiagnosticJournal::Append("PatternBake", "maps ready baked=" + std::to_string(Extent.width) + "x" + std::to_string(Extent.height) +
        " pyramid_ms=" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - PyramidStarted).count()));
    ReadbacksPending = false;
    return *CompiledMaps;
}

void VectorSurfaceMaterialBridge::Finalize() noexcept
{
    for (auto &Rasterization : Rasterizations) Rasterization.Finalize();
    Programs = {};
    CompiledMaps.reset();
    BakeExtent = 0;
    LastSubmission = PendingSubmission = 0;
    Initialized = ProgramsConfigured = Dirty = Recorded = ReadbacksPending = false;
}
}
