//============================================================================================================================================
//                                                         WORKSPACECADPASS.H
//============================================================================================================================================
// 🧩 The parametric workspace's dedicated CAD pass — sketch/profile segments, fill triangles and markers drawn
//    in their own dynamic-rendering pass rather than through interface draw lists.

#pragma once

#include "Foundation/ExtentBands.h"
#include "Foundation/DeliveryGuarantee.h"
#include "Shared/WorkspaceCadPacket.slang.h"
#include "SlateVulkan/Device/DiagnosticExtension/Api/DiagnosticExtension.h"
#include "SlateVulkan/Device/ShaderCodec/Api/ShaderCodec.h"
#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace Slate
{

class WorkspaceCadPass
{
public:
    static constexpr std::uint32_t SegmentCapacity = WorkspaceCadPacket::SegmentLimit;
    static constexpr std::uint32_t FillCapacity = WorkspaceCadPacket::FillLimit;
    static constexpr std::uint32_t MarkerCapacity = WorkspaceCadPacket::MarkerLimit;

    WorkspaceCadPass() = default;
    WorkspaceCadPass(const WorkspaceCadPass&) = delete;
    WorkspaceCadPass& operator=(const WorkspaceCadPass&) = delete;
    ~WorkspaceCadPass();

    Deliver<bool> ConstructWorkspaceCadPass(const VulkanExchange& Exchange,
                                            const DiagnosticExtension& Naming,
                                            ShaderCodec& Streams,
                                            VkFormat ColourFormat);

    void Upload(const WorkspaceCadPacket& Packet);
    void Record(VkCommandBuffer Command, const WorkspaceCadProjection& Projection,
                float ClipX0, float ClipY0, float ClipX1, float ClipY1);

    /// 🧩 Records the sketch across the clip, keeping clear of one withheld box.
    /// note  🔴 Same defect and same cure as the overlay pass: this one records after the interface too,
    ///        so the sketch drew through an open menu as well. The clip IS the box here -- there is no
    ///        separate camera rectangle -- so the bands are passed straight through.
    /// note  ⚠️ Every ordinate here is PHYSICAL. Mixing logical and physical silently clips the wrong area
    ///        on any display whose scale is not one.
    /// note  At most four recordings, and exactly one whenever nothing is withheld.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void RecordAround(VkCommandBuffer Command, const WorkspaceCadProjection& Projection,
                      float ClipX0, float ClipY0, float ClipX1, float ClipY1,
                      float WithheldX0, float WithheldY0, float WithheldX1, float WithheldY1);

    bool Standing() const
    {
        return DeviceEdge != nullptr && CadPipeline != VK_NULL_HANDLE;
    }

    void Reclaim();

private:
    const VulkanExchange* DeviceEdge = nullptr;
    const DiagnosticExtension* NamingEdge = nullptr;

    VkBuffer VertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory VertexMemory = VK_NULL_HANDLE;
    std::uint8_t* MappedSlot = nullptr;

    VkDescriptorSetLayout CadLayout = VK_NULL_HANDLE;
    VkDescriptorPool CadPool = VK_NULL_HANDLE;
    VkDescriptorSet CadSet = VK_NULL_HANDLE;

    VkPipelineLayout CadPipelineLayout = VK_NULL_HANDLE;
    VkPipeline CadPipeline = VK_NULL_HANDLE;

    std::uint32_t SegmentBytes = 0u;
    std::uint32_t FillBytes = 0u;
    std::uint32_t MarkerBytes = 0u;

    float PacketMinimumAlong = -1.0f;
    float PacketMinimumAcross = -1.0f;
    float PacketMaximumAlong = 1.0f;
    float PacketMaximumAcross = 1.0f;

    std::uint32_t PacketSegmentCount = 0u;
    std::uint32_t PacketFillCount = 0u;
    std::uint32_t PacketMarkerCount = 0u;
};

} // namespace Slate
