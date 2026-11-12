/*============================================================================================================================================
                                                        COMPUTEDISPATCHENCODER.CPP
============================================================================================================================================*/
// 🧩 Implementation of ComputeDispatchEncoder managing .slang SPIR-V compute pipelines, descriptor sets, and UAV barriers

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"

#include <algorithm>
#include <array>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

ComputeDispatchEncoder::~ComputeDispatchEncoder()
{
    TeardownEncoder();
}

bool ComputeDispatchEncoder::InitializeEncoder(VulkanInterchange* BoundDevice, uint32_t MaxDispatchesPerAllocator)
{
    TeardownEncoder();
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return false;
    }

    DeviceInterchange = BoundDevice;
    MaxDispatchesPerAllocator = std::clamp(MaxDispatchesPerAllocator, 1u, 4096u);
    const std::array<VkDescriptorPoolSize, 2u> AllocatorCapacities {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MaxDispatchesPerAllocator * 2u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MaxDispatchesPerAllocator * 6u },
    }};

    VkDescriptorPoolCreateInfo AllocatorSpec {};
    AllocatorSpec.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    AllocatorSpec.maxSets       = MaxDispatchesPerAllocator;
    AllocatorSpec.poolSizeCount = static_cast<uint32_t>(AllocatorCapacities.size());
    AllocatorSpec.pPoolSizes    = AllocatorCapacities.data();

    VkDescriptorPool InitialAllocator = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(DeviceInterchange->AccessDevice(), &AllocatorSpec, nullptr, &InitialAllocator) != VK_SUCCESS)
    {
        DeviceInterchange = nullptr;
        return false;
    }
    DescriptorAllocators.push_back(InitialAllocator);
    return true;
}

void ComputeDispatchEncoder::TeardownEncoder() noexcept
{
    if (DeviceInterchange != nullptr && DeviceInterchange->Ready())
    {
        VkDevice DeviceHandle = DeviceInterchange->AccessDevice();
        vkDeviceWaitIdle(DeviceHandle);
        for (RegisteredComputeProgram& Prog : Programs)
        {
            if (Prog.ComputeProgram != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(DeviceHandle, Prog.ComputeProgram, nullptr);
            }
            if (Prog.ProgramLayout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(DeviceHandle, Prog.ProgramLayout, nullptr);
            }
            if (Prog.SlotLayout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(DeviceHandle, Prog.SlotLayout, nullptr);
            }
            if (Prog.ShaderBinary != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(DeviceHandle, Prog.ShaderBinary, nullptr);
            }
        }
        Programs.clear();

        for (VkDescriptorPool Allocator : DescriptorAllocators)
        {
            if (Allocator != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(DeviceHandle, Allocator, nullptr);
            }
        }
        DescriptorAllocators.clear();
    }
    DeviceInterchange = nullptr;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           PROGRAM REGISTRATION & DISPATCH
//------------------------------------------------------------------------------------------------------------------------

uint32_t ComputeDispatchEncoder::RegisterProgram(
    const std::filesystem::path&      SpirvPath,
    std::span<const VkDescriptorType> SlotCategories,
    const char*                       EntryPointName)
{
    if (DeviceInterchange == nullptr || !DeviceInterchange->Ready())
    {
        return UINT32_MAX;
    }

    VkDevice DeviceHandle = DeviceInterchange->AccessDevice();
    RegisteredComputeProgram Prog {};
    Prog.ShaderBinary = DeviceInterchange->LoadShaderBinary(SpirvPath);
    if (Prog.ShaderBinary == VK_NULL_HANDLE)
    {
        return UINT32_MAX;
    }

    std::vector<VkDescriptorSetLayoutBinding> SlotDefinitions(SlotCategories.size());
    for (std::size_t Idx = 0u; Idx < SlotCategories.size(); ++Idx)
    {
        SlotDefinitions[Idx].binding            = static_cast<uint32_t>(Idx);
        SlotDefinitions[Idx].descriptorType     = SlotCategories[Idx];
        SlotDefinitions[Idx].descriptorCount    = 1u;
        SlotDefinitions[Idx].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
        SlotDefinitions[Idx].pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo SlotLayoutSpec {};
    SlotLayoutSpec.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SlotLayoutSpec.bindingCount = static_cast<uint32_t>(SlotDefinitions.size());
    SlotLayoutSpec.pBindings    = SlotDefinitions.data();
    if (vkCreateDescriptorSetLayout(DeviceHandle, &SlotLayoutSpec, nullptr, &Prog.SlotLayout) != VK_SUCCESS)
    {
        vkDestroyShaderModule(DeviceHandle, Prog.ShaderBinary, nullptr);
        return UINT32_MAX;
    }

    VkPipelineLayoutCreateInfo ProgramLayoutSpec {};
    ProgramLayoutSpec.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ProgramLayoutSpec.setLayoutCount = 1u;
    ProgramLayoutSpec.pSetLayouts    = &Prog.SlotLayout;
    if (vkCreatePipelineLayout(DeviceHandle, &ProgramLayoutSpec, nullptr, &Prog.ProgramLayout) != VK_SUCCESS)
    {
        vkDestroyDescriptorSetLayout(DeviceHandle, Prog.SlotLayout, nullptr);
        vkDestroyShaderModule(DeviceHandle, Prog.ShaderBinary, nullptr);
        return UINT32_MAX;
    }

    VkComputePipelineCreateInfo PipeCreate {};
    PipeCreate.sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    PipeCreate.stage.sType        = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    PipeCreate.stage.stage        = VK_SHADER_STAGE_COMPUTE_BIT;
    PipeCreate.stage.module       = Prog.ShaderBinary;
    PipeCreate.stage.pName        = EntryPointName;
    PipeCreate.layout             = Prog.ProgramLayout;

    if (vkCreateComputePipelines(DeviceHandle, VK_NULL_HANDLE, 1u, &PipeCreate, nullptr, &Prog.ComputeProgram) != VK_SUCCESS)
    {
        vkDestroyPipelineLayout(DeviceHandle, Prog.ProgramLayout, nullptr);
        vkDestroyDescriptorSetLayout(DeviceHandle, Prog.SlotLayout, nullptr);
        vkDestroyShaderModule(DeviceHandle, Prog.ShaderBinary, nullptr);
        return UINT32_MAX;
    }

    Prog.SlotCount = static_cast<uint32_t>(SlotCategories.size());
    const uint32_t AssignedIndex = static_cast<uint32_t>(Programs.size());
    Programs.push_back(Prog);
    return AssignedIndex;
}

void ComputeDispatchEncoder::ResetTransientDescriptors() noexcept
{
    if (DeviceInterchange != nullptr)
    {
        const VkDevice DeviceHandle = DeviceInterchange->AccessDevice();
        for (VkDescriptorPool Allocator : DescriptorAllocators)
        {
            if (Allocator != VK_NULL_HANDLE)
            {
                vkResetDescriptorPool(DeviceHandle, Allocator, 0u);
            }
        }
    }
}

bool ComputeDispatchEncoder::EncodeComputeDispatch(
    VkCommandBuffer                           CmdBuffer,
    uint32_t                                  ProgramHandle,
    std::span<const ComputeSlotDescriptor> Slots,
    uint32_t                                  GroupCountX,
    uint32_t                                  GroupCountY,
    uint32_t                                  GroupCountZ)
{
    if (DeviceInterchange == nullptr || ProgramHandle >= Programs.size() || GroupCountX == 0u)
    {
        return false;
    }

    VkDevice DeviceHandle = DeviceInterchange->AccessDevice();
    const RegisteredComputeProgram& Prog = Programs[ProgramHandle];

    VkDescriptorSetAllocateInfo AllocSpec {};
    AllocSpec.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    AllocSpec.descriptorSetCount = 1u;
    AllocSpec.pSetLayouts        = &Prog.SlotLayout;

    VkDescriptorSet DescSet = VK_NULL_HANDLE;
    for (VkDescriptorPool Allocator : DescriptorAllocators)
    {
        AllocSpec.descriptorPool = Allocator;
        const VkResult AllocStatus = vkAllocateDescriptorSets(DeviceHandle, &AllocSpec, &DescSet);
        if (AllocStatus == VK_SUCCESS)
        {
            break;
        }
        if (AllocStatus != VK_ERROR_OUT_OF_POOL_MEMORY && AllocStatus != VK_ERROR_FRAGMENTED_POOL)
        {
            return false;
        }
    }
    if (DescSet == VK_NULL_HANDLE)
    {
        if (DescriptorAllocators.size() >= 64u)
        {
            return false;
        }
        const std::array<VkDescriptorPoolSize, 2u> AllocatorSizes {{
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MaxDispatchesPerAllocator * 2u },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MaxDispatchesPerAllocator * 6u },
        }};
        VkDescriptorPoolCreateInfo AllocatorSpecNew {};
        AllocatorSpecNew.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        AllocatorSpecNew.maxSets       = MaxDispatchesPerAllocator;
        AllocatorSpecNew.poolSizeCount = static_cast<uint32_t>(AllocatorSizes.size());
        AllocatorSpecNew.pPoolSizes    = AllocatorSizes.data();
        VkDescriptorPool NewAllocator = VK_NULL_HANDLE;
        if (vkCreateDescriptorPool(DeviceHandle, &AllocatorSpecNew, nullptr, &NewAllocator) != VK_SUCCESS)
        {
            return false;
        }
        AllocSpec.descriptorPool = NewAllocator;
        if (vkAllocateDescriptorSets(DeviceHandle, &AllocSpec, &DescSet) != VK_SUCCESS)
        {
            vkDestroyDescriptorPool(DeviceHandle, NewAllocator, nullptr);
            return false;
        }
        DescriptorAllocators.push_back(NewAllocator);
    }

    std::vector<VkDescriptorBufferInfo> BufInfos(Slots.size());
    std::vector<VkWriteDescriptorSet>   Writes(Slots.size());
    for (std::size_t Idx = 0u; Idx < Slots.size(); ++Idx)
    {
        BufInfos[Idx].buffer = Slots[Idx].BufferHandle;
        BufInfos[Idx].offset = 0u;
        BufInfos[Idx].range  = Slots[Idx].ByteRange;

        Writes[Idx].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Idx].dstSet          = DescSet;
        Writes[Idx].dstBinding      = Slots[Idx].SlotIndex;
        Writes[Idx].descriptorCount = 1u;
        Writes[Idx].descriptorType  = Slots[Idx].SlotCategory;
        Writes[Idx].pBufferInfo     = &BufInfos[Idx];
    }

    vkUpdateDescriptorSets(DeviceHandle, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);

    vkCmdBindPipeline(CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Prog.ComputeProgram);
    vkCmdBindDescriptorSets(CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Prog.ProgramLayout, 0u, 1u, &DescSet, 0u, nullptr);
    vkCmdDispatch(CmdBuffer, GroupCountX, GroupCountY, GroupCountZ);

    VkMemoryBarrier ComputeToShaderBarrier {};
    ComputeToShaderBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    ComputeToShaderBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ComputeToShaderBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(
        CmdBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        0u,
        1u,
        &ComputeToShaderBarrier,
        0u,
        nullptr,
        0u,
        nullptr);

    return true;
}

DeviceBufferResource ComputeDispatchEncoder::AllocateHostWritableBuffer(
    uint64_t           ByteSize,
    VkBufferUsageFlags UsageMask) const
{
    if (DeviceInterchange == nullptr)
    {
        return {};
    }
    return DeviceInterchange->AllocateBuffer(
        ByteSize,
        UsageMask | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
}

} // namespace Frontier::GeometricRaster
