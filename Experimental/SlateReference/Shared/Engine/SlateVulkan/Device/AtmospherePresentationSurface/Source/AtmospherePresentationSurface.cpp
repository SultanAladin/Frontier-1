//============================================================================================================================================
//                                                        ATMOSPHEREPRESENTATIONSURFACE.CPP
//============================================================================================================================================

#include "SlateVulkan/Device/AtmospherePresentationSurface/Api/AtmospherePresentationSurface.h"

namespace Slate
{

static_assert(sizeof(DynamicSkyParameters) == 68u,
              "the dynamic sky push constants must match the shader's sixteen FP32 fields and quality field");

namespace
{
std::uint32_t FindMemory(const VulkanExchange& Exchange, std::uint32_t Allowed, VkMemoryPropertyFlags Wanted)
{
    VkPhysicalDeviceMemoryProperties Properties = {};
    vkGetPhysicalDeviceMemoryProperties(Exchange.ScoredDevice(), &Properties);
    for (std::uint32_t Index = 0u; Index < Properties.memoryTypeCount; ++Index)
        if ((Allowed & (1u << Index)) != 0u &&
            (Properties.memoryTypes[Index].propertyFlags & Wanted) == Wanted)
            return Index;
    return 0xFFFFFFFFu;
}
}

AtmospherePresentationSurface::~AtmospherePresentationSurface() { Reclaim(); }

Deliver<bool> AtmospherePresentationSurface::ConstructAtmosphereSurface(const VulkanExchange& Exchange,
                                                                         const DiagnosticExtension& Naming,
                                                                         ShaderCodec& Streams)
{
    if (DeviceEdge != nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a dynamic sky already stands" });
    if (Exchange.ActiveDevice() == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    DeviceEdge = &Exchange;
    const VkDevice Device = Exchange.ActiveDevice();

    VkFormatProperties Format = {};
    vkGetPhysicalDeviceFormatProperties(Exchange.ScoredDevice(), VK_FORMAT_R16G16B16A16_SFLOAT, &Format);
    const VkFormatFeatureFlags Needed = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((Format.optimalTilingFeatures & Needed) != Needed)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "the device declines a sampled writable HDR sky" });
    }

    VkImageCreateInfo Image = {};
    Image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    Image.imageType = VK_IMAGE_TYPE_2D;
    Image.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    Image.extent = { SkyWidth, SkyHeight, 1u };
    Image.mipLevels = 1u;
    Image.arrayLayers = 1u;
    Image.samples = VK_SAMPLE_COUNT_1_BIT;
    Image.tiling = VK_IMAGE_TILING_OPTIMAL;
    Image.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    Image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    Image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(Device, &Image, nullptr, &ImageSlot) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the dynamic sky image was rejected" });
    }

    VkMemoryRequirements Requirements = {};
    vkGetImageMemoryRequirements(Device, ImageSlot, &Requirements);
    const std::uint32_t MemoryIndex = FindMemory(Exchange, Requirements.memoryTypeBits,
                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (MemoryIndex == 0xFFFFFFFFu)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "no device-local sky memory exists" });
    }

    VkMemoryAllocateInfo Allocation = {};
    Allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    Allocation.allocationSize = Requirements.size;
    Allocation.memoryTypeIndex = MemoryIndex;
    if (vkAllocateMemory(Device, &Allocation, nullptr, &ImageMemory) != VK_SUCCESS ||
        vkBindImageMemory(Device, ImageSlot, ImageMemory, 0u) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the dynamic sky memory was rejected" });
    }

    VkImageViewCreateInfo View = {};
    View.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    View.image = ImageSlot;
    View.viewType = VK_IMAGE_VIEW_TYPE_2D;
    View.format = Image.format;
    View.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    View.subresourceRange.levelCount = 1u;
    View.subresourceRange.layerCount = 1u;
    if (vkCreateImageView(Device, &View, nullptr, &ImageViewSlot) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the dynamic sky view was rejected" });
    }

    VkSamplerCreateInfo Sampler = {};
    Sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    Sampler.magFilter = VK_FILTER_LINEAR;
    Sampler.minFilter = VK_FILTER_LINEAR;
    Sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    Sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    Sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Sampler.maxLod = 0.0f;
    if (vkCreateSampler(Device, &Sampler, nullptr, &SamplerSlot) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the dynamic sky sampler was rejected" });
    }

    VkDescriptorSetLayoutBinding Binding = {};
    Binding.binding = 0u;
    Binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    Binding.descriptorCount = 1u;
    Binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo Layout = {};
    Layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    Layout.bindingCount = 1u;
    Layout.pBindings = &Binding;
    if (vkCreateDescriptorSetLayout(Device, &Layout, nullptr, &SkyLayout) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the dynamic sky layout was rejected" });
    }

    VkDescriptorPoolSize PoolSize = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1u };
    VkDescriptorPoolCreateInfo Pool = {};
    Pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    Pool.maxSets = 1u;
    Pool.poolSizeCount = 1u;
    Pool.pPoolSizes = &PoolSize;
    if (vkCreateDescriptorPool(Device, &Pool, nullptr, &SkyPool) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the dynamic sky descriptor pool was rejected" });
    }

    VkDescriptorSetAllocateInfo Set = {};
    Set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    Set.descriptorPool = SkyPool;
    Set.descriptorSetCount = 1u;
    Set.pSetLayouts = &SkyLayout;
    if (vkAllocateDescriptorSets(Device, &Set, &SkySet) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the dynamic sky descriptor was rejected" });
    }

    VkDescriptorImageInfo ImageInfo = {};
    ImageInfo.imageView = ImageViewSlot;
    ImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet Write = {};
    Write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Write.dstSet = SkySet;
    Write.dstBinding = 0u;
    Write.descriptorCount = 1u;
    Write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    Write.pImageInfo = &ImageInfo;
    vkUpdateDescriptorSets(Device, 1u, &Write, 0u, nullptr);

    const Deliver<std::uint32_t> Module = Streams.Resolve("SlateVulkan", "DynamicSkySurface");
    if (!Module.Resolved)
    {
        Reclaim();
        return Deliver<bool>::Refuse(Module.Error);
    }
    const Deliver<VkPipelineShaderStageCreateInfo> Stage =
        Streams.Stage(Module.Resolve(), VK_SHADER_STAGE_COMPUTE_BIT, {});
    if (!Stage.Resolved)
    {
        Reclaim();
        return Deliver<bool>::Refuse(Stage.Error);
    }

    VkPushConstantRange Push = {};
    Push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    Push.size = sizeof(DynamicSkyParameters);
    VkPipelineLayoutCreateInfo PipelineLayout = {};
    PipelineLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    PipelineLayout.setLayoutCount = 1u;
    PipelineLayout.pSetLayouts = &SkyLayout;
    PipelineLayout.pushConstantRangeCount = 1u;
    PipelineLayout.pPushConstantRanges = &Push;
    if (vkCreatePipelineLayout(Device, &PipelineLayout, nullptr, &SkyPipelineLayout) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the dynamic sky pipeline layout was rejected" });
    }

    VkComputePipelineCreateInfo Pipeline = {};
    Pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    Pipeline.stage = Stage.Resolve();
    Pipeline.layout = SkyPipelineLayout;
    if (vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1u, &Pipeline, nullptr, &SkyPipeline) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the dynamic sky compute pipeline was rejected" });
    }

    static_cast<void>(Naming.Declare(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(ImageSlot),
                                     "DynamicSkySurface"));
    static_cast<void>(Naming.Declare(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(SkyPipeline),
                                     "DynamicSkySurface.Compute"));
    return Deliver<bool>::Result(true);
}

Deliver<bool> AtmospherePresentationSurface::Record(VkCommandBuffer Command,
                                         const DynamicSkyParameters& Parameters, bool Dirty)
{
    if (!Standing() || Command == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "the dynamic sky does not stand" });
    if (!Dirty)
        return Deliver<bool>::Result(true);

    VkImageMemoryBarrier Before = {};
    Before.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    Before.srcAccessMask = CurrentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                         ? VK_ACCESS_SHADER_READ_BIT : 0u;
    Before.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    Before.oldLayout = CurrentLayout;
    Before.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    Before.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Before.image = ImageSlot;
    Before.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    Before.subresourceRange.levelCount = 1u;
    Before.subresourceRange.layerCount = 1u;
    vkCmdPipelineBarrier(Command,
                         CurrentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                             ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &Before);

    vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_COMPUTE, SkyPipeline);
    vkCmdBindDescriptorSets(Command, VK_PIPELINE_BIND_POINT_COMPUTE, SkyPipelineLayout,
                            0u, 1u, &SkySet, 0u, nullptr);
    vkCmdPushConstants(Command, SkyPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0u, sizeof(Parameters), &Parameters);
    vkCmdDispatch(Command, (SkyWidth + 7u) / 8u, (SkyHeight + 7u) / 8u, 1u);

    VkImageMemoryBarrier After = Before;
    After.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    After.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    After.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    After.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0u,
                         0u, nullptr, 0u, nullptr, 1u, &After);
    CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return Deliver<bool>::Result(true);
}

void AtmospherePresentationSurface::Reclaim()
{
    if (DeviceEdge != nullptr && DeviceEdge->ActiveDevice() != VK_NULL_HANDLE)
    {
        const VkDevice Device = DeviceEdge->ActiveDevice();
        if (SkyPipeline != VK_NULL_HANDLE) vkDestroyPipeline(Device, SkyPipeline, nullptr);
        if (SkyPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, SkyPipelineLayout, nullptr);
        if (SkyPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, SkyPool, nullptr);
        if (SkyLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, SkyLayout, nullptr);
        if (SamplerSlot != VK_NULL_HANDLE) vkDestroySampler(Device, SamplerSlot, nullptr);
        if (ImageViewSlot != VK_NULL_HANDLE) vkDestroyImageView(Device, ImageViewSlot, nullptr);
        if (ImageSlot != VK_NULL_HANDLE) vkDestroyImage(Device, ImageSlot, nullptr);
        if (ImageMemory != VK_NULL_HANDLE) vkFreeMemory(Device, ImageMemory, nullptr);
    }
    DeviceEdge = nullptr;
    ImageSlot = VK_NULL_HANDLE;
    ImageMemory = VK_NULL_HANDLE;
    ImageViewSlot = VK_NULL_HANDLE;
    SamplerSlot = VK_NULL_HANDLE;
    SkyLayout = VK_NULL_HANDLE;
    SkyPool = VK_NULL_HANDLE;
    SkySet = VK_NULL_HANDLE;
    SkyPipelineLayout = VK_NULL_HANDLE;
    SkyPipeline = VK_NULL_HANDLE;
    CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

} // namespace Slate
