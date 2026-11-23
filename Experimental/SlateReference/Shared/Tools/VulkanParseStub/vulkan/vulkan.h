// Minimal stand-in for the Vulkan headers, sufficient to PARSE Slate's device-facing
// declarations on a machine with no SDK. Not a build; a syntax check only.
#pragma once
#include <cstdint>
#include <cstddef>
#define VK_NULL_HANDLE nullptr
#define VK_DEFINE_HANDLE(name) typedef struct name##_T* name;
VK_DEFINE_HANDLE(VkInstance) VK_DEFINE_HANDLE(VkPhysicalDevice) VK_DEFINE_HANDLE(VkDevice)
VK_DEFINE_HANDLE(VkQueue) VK_DEFINE_HANDLE(VkCommandBuffer) VK_DEFINE_HANDLE(VkCommandPool)
VK_DEFINE_HANDLE(VkFence) VK_DEFINE_HANDLE(VkSemaphore) VK_DEFINE_HANDLE(VkImage)
VK_DEFINE_HANDLE(VkImageView) VK_DEFINE_HANDLE(VkSampler) VK_DEFINE_HANDLE(VkSurfaceKHR)
VK_DEFINE_HANDLE(VkSwapchainKHR) VK_DEFINE_HANDLE(VkDescriptorPool) VK_DEFINE_HANDLE(VkDescriptorSet)
VK_DEFINE_HANDLE(VkDebugUtilsMessengerEXT) VK_DEFINE_HANDLE(VkPipeline) VK_DEFINE_HANDLE(VkPipelineLayout)
VK_DEFINE_HANDLE(VkRenderPass) VK_DEFINE_HANDLE(VkFramebuffer) VK_DEFINE_HANDLE(VkBuffer)
VK_DEFINE_HANDLE(VkDeviceMemory) VK_DEFINE_HANDLE(VkDescriptorSetLayout) VK_DEFINE_HANDLE(VkShaderModule)
typedef std::uint32_t VkBool32; typedef std::uint32_t VkFlags; typedef std::uint64_t VkDeviceSize;
typedef enum VkFormat { VK_FORMAT_UNDEFINED = 0 } VkFormat;
typedef enum VkColorSpaceKHR { VK_COLOR_SPACE_SRGB_NONLINEAR_KHR = 0 } VkColorSpaceKHR;
typedef enum VkPresentModeKHR { VK_PRESENT_MODE_FIFO_KHR = 2 } VkPresentModeKHR;
typedef enum VkResult { VK_SUCCESS = 0, VK_ERROR_OUT_OF_DATE_KHR = -1000001004 } VkResult;
typedef enum VkObjectType { VK_OBJECT_TYPE_UNKNOWN = 0 } VkObjectType;
typedef enum VkDebugUtilsMessageSeverityFlagBitsEXT { VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT = 0x1000 } VkDebugUtilsMessageSeverityFlagBitsEXT;
typedef VkFlags VkDebugUtilsMessageTypeFlagsEXT; typedef VkFlags VkPipelineStageFlags;
#define VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT 0x00000001
typedef struct VkSurfaceFormatKHR { VkFormat format; VkColorSpaceKHR colorSpace; } VkSurfaceFormatKHR;
typedef struct VkDebugUtilsMessengerCallbackDataEXT { const char* pMessage; const char* pMessageIdName; std::int32_t messageIdNumber; } VkDebugUtilsMessengerCallbackDataEXT;
typedef void (*PFN_vkVoidFunction)(void);
typedef VkResult (*PFN_vkCreateDebugUtilsMessengerEXT)(VkInstance, const void*, const void*, VkDebugUtilsMessengerEXT*);
typedef void (*PFN_vkDestroyDebugUtilsMessengerEXT)(VkInstance, VkDebugUtilsMessengerEXT, const void*);
typedef VkResult (*PFN_vkSetDebugUtilsObjectNameEXT)(VkDevice, const void*);
#define VKAPI_ATTR
#define VKAPI_CALL
#define VKAPI_PTR
typedef VkFlags VkShaderStageFlags; typedef VkFlags VkSampleCountFlags; typedef VkFlags VkCullModeFlags;
typedef enum VkShaderStageFlagBits { VK_SHADER_STAGE_VERTEX_BIT = 1, VK_SHADER_STAGE_FRAGMENT_BIT = 16, VK_SHADER_STAGE_COMPUTE_BIT = 32 } VkShaderStageFlagBits;
typedef enum VkStructureType { VK_STRUCTURE_TYPE_APPLICATION_INFO = 0 } VkStructureType;
typedef enum VkSampleCountFlagBits { VK_SAMPLE_COUNT_1_BIT = 1 } VkSampleCountFlagBits;
typedef enum VkImageLayout { VK_IMAGE_LAYOUT_UNDEFINED = 0 } VkImageLayout;
typedef enum VkPrimitiveTopology { VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST = 3 } VkPrimitiveTopology;
typedef enum VkCullModeFlagBits { VK_CULL_MODE_NONE = 0, VK_CULL_MODE_BACK_BIT = 2 } VkCullModeFlagBits;
typedef enum VkCompareOp { VK_COMPARE_OP_LESS = 1 } VkCompareOp;
typedef enum VkPolygonMode { VK_POLYGON_MODE_FILL = 0 } VkPolygonMode;
typedef enum VkFrontFace { VK_FRONT_FACE_COUNTER_CLOCKWISE = 0 } VkFrontFace;
typedef enum VkDescriptorType { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER = 6 } VkDescriptorType;
typedef enum VkFilter { VK_FILTER_LINEAR = 1 } VkFilter;
typedef enum VkIndexType { VK_INDEX_TYPE_UINT32 = 1 } VkIndexType;
typedef struct VkSpecializationMapEntry { std::uint32_t constantID; std::uint32_t offset; std::size_t size; } VkSpecializationMapEntry;
typedef struct VkSpecializationInfo { std::uint32_t mapEntryCount; const VkSpecializationMapEntry* pMapEntries; std::size_t dataSize; const void* pData; } VkSpecializationInfo;
typedef struct VkPipelineShaderStageCreateInfo { VkStructureType sType; const void* pNext; VkFlags flags; VkShaderStageFlagBits stage; VkShaderModule module; const char* pName; const VkSpecializationInfo* pSpecializationInfo; } VkPipelineShaderStageCreateInfo;
typedef struct VkExtent2D { std::uint32_t width, height; } VkExtent2D;
typedef struct VkOffset2D { std::int32_t x, y; } VkOffset2D;
typedef struct VkRect2D { VkOffset2D offset; VkExtent2D extent; } VkRect2D;
typedef struct VkViewport { float x, y, width, height, minDepth, maxDepth; } VkViewport;
typedef struct VkClearValue { float placeholder[4]; } VkClearValue;
typedef VkFlags VkImageUsageFlags; typedef VkFlags VkImageAspectFlags; typedef VkFlags VkMemoryPropertyFlags;
typedef VkFlags VkBufferUsageFlags; typedef VkFlags VkAccessFlags; typedef VkFlags VkDependencyFlags;
typedef VkFlags VkColorComponentFlags; typedef VkFlags VkQueryPipelineStatisticFlags;
#define VK_WHOLE_SIZE (~0ULL)
#define VK_QUEUE_FAMILY_IGNORED (~0U)
#define VK_SUBPASS_EXTERNAL (~0U)
#define VK_TRUE 1
#define VK_FALSE 0
#define VK_MAX_MEMORY_TYPES 32
#define VK_MAX_MEMORY_HEAPS 16
VK_DEFINE_HANDLE(VkQueryPool) VK_DEFINE_HANDLE(VkPipelineCache) VK_DEFINE_HANDLE(VkEvent)
typedef struct VkMemoryType { VkFlags propertyFlags; std::uint32_t heapIndex; } VkMemoryType;
typedef struct VkMemoryHeap { VkDeviceSize size; VkFlags flags; } VkMemoryHeap;
typedef struct VkPhysicalDeviceMemoryProperties { std::uint32_t memoryTypeCount; VkMemoryType memoryTypes[VK_MAX_MEMORY_TYPES]; std::uint32_t memoryHeapCount; VkMemoryHeap memoryHeaps[VK_MAX_MEMORY_HEAPS]; } VkPhysicalDeviceMemoryProperties;
typedef enum VkPipelineBindPoint { VK_PIPELINE_BIND_POINT_GRAPHICS = 0, VK_PIPELINE_BIND_POINT_COMPUTE = 1 } VkPipelineBindPoint;
typedef enum VkVertexInputRate { VK_VERTEX_INPUT_RATE_VERTEX = 0 } VkVertexInputRate;
typedef enum VkBlendFactor { VK_BLEND_FACTOR_ONE = 1 } VkBlendFactor;
typedef enum VkBlendOp { VK_BLEND_OP_ADD = 0 } VkBlendOp;
typedef enum VkAttachmentLoadOp { VK_ATTACHMENT_LOAD_OP_CLEAR = 1 } VkAttachmentLoadOp;
typedef enum VkAttachmentStoreOp { VK_ATTACHMENT_STORE_OP_STORE = 0 } VkAttachmentStoreOp;
typedef enum VkSamplerAddressMode { VK_SAMPLER_ADDRESS_MODE_REPEAT = 0 } VkSamplerAddressMode;
typedef enum VkSamplerMipmapMode { VK_SAMPLER_MIPMAP_MODE_LINEAR = 1 } VkSamplerMipmapMode;
typedef enum VkImageTiling { VK_IMAGE_TILING_OPTIMAL = 0 } VkImageTiling;
typedef enum VkImageType { VK_IMAGE_TYPE_2D = 1 } VkImageType;
typedef enum VkImageViewType { VK_IMAGE_VIEW_TYPE_2D = 1 } VkImageViewType;
typedef enum VkSharingMode { VK_SHARING_MODE_EXCLUSIVE = 0 } VkSharingMode;
typedef enum VkDynamicState { VK_DYNAMIC_STATE_VIEWPORT = 0 } VkDynamicState;
typedef enum VkQueryType { VK_QUERY_TYPE_TIMESTAMP = 2 } VkQueryType;
#define VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ((VkDescriptorType)7)
#define VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ((VkDescriptorType)1)
#define VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ((VkDescriptorType)3)
#define VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ((VkDescriptorType)2)
#define VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ((VkImageLayout)5)
#define VK_IMAGE_LAYOUT_GENERAL ((VkImageLayout)1)
#define VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ((VkImageLayout)2)
#define VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ((VkImageLayout)7)
#define VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ((VkImageLayout)6)
#define VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ((VkImageLayout)1000001002)
#define VK_COMPARE_OP_GREATER ((VkCompareOp)4)
#define VK_COMPARE_OP_ALWAYS ((VkCompareOp)7)
#define VK_COMPARE_OP_LESS_OR_EQUAL ((VkCompareOp)3)
#define VK_FORMAT_R8G8B8A8_UNORM ((VkFormat)37)
#define VK_FORMAT_D32_SFLOAT ((VkFormat)126)
#define VK_FORMAT_R32G32B32A32_SFLOAT ((VkFormat)109)
#define VK_FORMAT_R32G32B32_SFLOAT ((VkFormat)106)
#define VK_FORMAT_R32G32_SFLOAT ((VkFormat)103)
#define VK_FORMAT_R32_SFLOAT ((VkFormat)100)
#define VK_FORMAT_B8G8R8A8_UNORM ((VkFormat)44)
#define VK_FORMAT_R16G16B16A16_SFLOAT ((VkFormat)97)
