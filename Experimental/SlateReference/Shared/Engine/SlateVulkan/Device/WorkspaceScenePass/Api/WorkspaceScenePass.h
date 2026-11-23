//============================================================================================================================================
//                                                        WORKSPACESCENEPASS.H
//============================================================================================================================================
// 🧩 Dedicated scene-mesh GPU pass seam for imported workspace polygon objects. The pass owns scene-triangle
//    upload state separately from the CAD and overlay passes so mesh rendering can evolve independently.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateVulkan/Device/DiagnosticExtension/Api/DiagnosticExtension.h"
#include "SlateVulkan/Device/ShaderCodec/Api/ShaderCodec.h"
#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Slate
{

struct WorkspaceSceneProjection
{
    float DisplayWidth = 1.0f;
    float DisplayHeight = 1.0f;
    float ViewProjection[16] = {};
};

enum class WorkspaceSceneViewMode : std::uint32_t
{
    Lit = 0u,
    Matcap = 1u,
    SourceWire = 2u,
    TriangulatedWire = 3u,
    Points = 4u,
    Normal = 5u,
    Metallic = 6u,
    Illumination = 7u
};

struct WorkspaceSceneTriangle
{
    float Position[9] = {};
    float Colour[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    std::uint32_t MaterialSlot = 0u;
};

struct WorkspaceSceneMaterial
{
    float Albedo[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float NormalIncidenceReflectance[4] = { 0.04f, 0.04f, 0.04f, 1.0f };
    float Scalars[4] = { 0.0f, 0.5f, 1.0f, 0.0f }; // metallic, roughness, opacity, transmission
    float RefractionRatio = 1.5f;
    std::uint32_t ActiveChannelMask = 0u;
    std::uint32_t Closure = 0u;
    std::uint32_t Features = 0u;
    std::uint32_t Coverage = 0u;
    std::uint32_t Wall = 0u;
    std::uint32_t Interface = 0u;
    std::uint32_t TwoSided = 0u;
    std::uint32_t Reserved = 0u;
    float RegisterPadding[3] = {};
    std::uint64_t DirtyFingerprint = 0u;
};

class WorkspaceScenePass
{
public:
    static constexpr std::uint32_t TriangleCapacity = 65536u;
    static constexpr std::uint32_t MaterialCapacity = 256u;

    WorkspaceScenePass() = default;
    WorkspaceScenePass(const WorkspaceScenePass&) = delete;
    WorkspaceScenePass& operator=(const WorkspaceScenePass&) = delete;
    ~WorkspaceScenePass();

    Deliver<bool> ConstructWorkspaceScenePass(const VulkanExchange& Exchange,
                                              const DiagnosticExtension& Naming,
                                              ShaderCodec& Streams,
                                              VkFormat ColourFormat);

    void Upload(const WorkspaceSceneTriangle* Triangles, std::uint32_t TriangleCount);
    void UploadMaterials(const WorkspaceSceneMaterial* Materials, std::uint32_t MaterialCount);
    void Record(VkCommandBuffer Command, const WorkspaceSceneProjection& Projection,
                float ClipX0, float ClipY0, float ClipX1, float ClipY1,
                WorkspaceSceneViewMode ViewMode = WorkspaceSceneViewMode::Lit);

    bool Standing() const { return DeviceEdge != nullptr && ScenePipeline != VK_NULL_HANDLE; }
    std::uint32_t TriangleCount() const { return UploadedTriangleCount; }
    std::uint32_t MaterialCount() const { return UploadedMaterialCount; }

    void Reclaim();

private:
    const VulkanExchange* DeviceEdge = nullptr;
    const DiagnosticExtension* NamingEdge = nullptr;
    VkFormat TargetFormat = VK_FORMAT_UNDEFINED;

    VkBuffer SceneBuffer = VK_NULL_HANDLE;
    VkDeviceMemory SceneMemory = VK_NULL_HANDLE;
    std::uint8_t* MappedSlot = nullptr;

    VkDescriptorSetLayout SceneLayout = VK_NULL_HANDLE;
    VkDescriptorPool ScenePool = VK_NULL_HANDLE;
    VkDescriptorSet SceneSet = VK_NULL_HANDLE;

    VkPipelineLayout ScenePipelineLayout = VK_NULL_HANDLE;
    VkPipeline ScenePipeline = VK_NULL_HANDLE;

    std::uint32_t TriangleBytes = 0u;
    std::uint32_t MaterialBytes = 0u;
    std::uint32_t UploadedTriangleCount = 0u;
    std::uint32_t UploadedMaterialCount = 0u;

    std::vector<WorkspaceSceneTriangle> UploadedTriangles = {};
    std::vector<WorkspaceSceneMaterial> UploadedMaterials = {};
};

} // namespace Slate
