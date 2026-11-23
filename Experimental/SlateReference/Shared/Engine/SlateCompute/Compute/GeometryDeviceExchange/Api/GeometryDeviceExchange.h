//============================================================================================================================================
//                                                        GEOMETRYDEVICEEXCHANGE.H
//============================================================================================================================================
// 🧩 Device residency and display-sized targets for authoritative geometry visibility and radiance.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateCompute/Compute/VisibilityIndex/Api/VisibilityRaster.h"
#include "SlateVulkan/Device/AttachmentIndex/Api/AttachmentIndex.h"
#include "SlateVulkan/Device/ByteSpace/Api/ByteSpace.h"
#include "SlateVulkan/Device/DescriptorIndex/Api/DescriptorIndex.h"
#include "SlateVulkan/Device/ImageSpace/Api/ImageSpace.h"
#include "SlateVulkan/Device/ProgramIndex/Api/ProgramIndex.h"
#include "SlateVulkan/Device/RenderSchedule/Api/RenderSchedule.h"
#include "SlateVulkan/Device/ShaderCodec/Api/ShaderCodec.h"
#include "SlateVulkan/Device/SpanSpace/Api/SpanSpace.h"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace Slate
{

/// 🧩 The device-side rendering estate held independently of any imported geometry.
/// note  The exchange is deliberately constructed before a geometry packet arrives. Device targets, programs and
///       descriptor declarations belong to the device lifetime; authoritative geometry residency belongs to the
///       geometry lifetime and is admitted only after the caller has selected and decoded a real source stream.
class GeometryDeviceExchange
{
public:

    GeometryDeviceExchange()                                  = default;
    GeometryDeviceExchange(const GeometryDeviceExchange&)      = delete;
    GeometryDeviceExchange& operator=(const GeometryDeviceExchange&) = delete;
    ~GeometryDeviceExchange();

    /// 🧩 Constructs the device estate and derives its display-sized visibility targets.
    /// in    Exchange       [-]  the active Vulkan device
    /// in    Naming         [-]  the diagnostic naming extension
    /// in    ShaderLocation  [-]  the folder containing lowered shader streams
    /// in    DisplayWidth    [px]  target width
    /// in    DisplayHeight   [px]  target height
    /// in    DisplayFormat   [-]  the current presentation format
    /// out   Result          [-]  refuses at the first rejected device claim and reclaims prior claims
    Deliver<bool> ConstructGeometryDeviceExchange(const VulkanExchange&      Exchange,
                                                  const DiagnosticExtension& Naming,
                                                  const char*                 ShaderLocation,
                                                  std::uint32_t               DisplayWidth,
                                                  std::uint32_t               DisplayHeight,
                                                  VkFormat                     DisplayFormat);

    /// 🧩 Re-derives display-relative targets after the host re-established its display chain.
    /// pre   the device is idle; HostLifecycle has completed the recovery before this call
    Deliver<bool> ReclaimDisplay(std::uint32_t DisplayWidth, std::uint32_t DisplayHeight);

    /// 🧩 Makes one partitioned, authoritative Earcut packet device-resident through the supplied recording.
    /// in    Partitioned      [-]  source-face partitioning for the packet's topology revision
    /// in    Rendering        [-]  immutable corner-expanded Earcut packet
    /// in    RegistrationBase [-]  document-wide first partition ordinal
    /// in    Recorded         [-]  open recording that later surrenders the staging transfer
    /// out   Result           [-]  the resident geometry ordinal
    Deliver<std::uint32_t> Admit(const PartitionStructure&        Partitioned,
                                 const GeometryRenderingSnapshot& Rendering,
                                 std::uint32_t                     RegistrationBase,
                                 VkCommandBuffer                   Recorded);

    /// 🧩 The authoritative hardware visibility raster, ready to receive a real decoded geometry packet.
    VisibilityRaster& Visibility();

    /// 🧩 Records hardware visibility followed by transparent fixed-white radiance resolve.
    /// in    Recorded  [-]  the open pre-display command recording
    /// in    SlotIndex [-]  the completion-gated recording slot this command belongs to
    /// in    Viewing   [-]  the current reversed-depth camera projection
    /// out   Result    [-]  refuses until at least one authoritative geometry residency stands
    Deliver<bool> Record(VkCommandBuffer Recorded, std::uint32_t SlotIndex, const ViewProjection& Viewing);

    /// 🧩 Resolves visibility into transparent black or an initial fixed-white dielectric radiance.
    /// in    Recorded  [-]  the open pre-display command recording
    /// in    SlotIndex [-]  the completion-gated recording slot this command belongs to
    /// out   Result    [-]  refuses before the device estate or its resolve program stands
    Deliver<bool> ResolveFixedWhite(VkCommandBuffer Recorded, std::uint32_t SlotIndex);

    /// 🧩 The transparent fixed-white radiance target after ResolveFixedWhite has written it.
    Deliver<ImageReservation> Radiance() const;

    /// 🧩 The linear sampler through which the interface samples the resolved radiance target.
    VkSampler RadianceSampler() const;

    /// 🧩 Releases every device claim while the device is still alive.
    void Reclaim();

    bool Standing() const;

private:

    static constexpr std::uint32_t GeometryResidencyLimit = 256u; // [-] - independently resident imported geometry packets

    const VulkanExchange* DeviceEdge = nullptr; // [-] - borrowed device; outlives every owned claim
    VkSampler          RadianceSampling = VK_NULL_HANDLE; // [-] - transparent radiance sampling
    ByteSpace          Bytes        = {};   // [-] - backing extents for images and spans
    ImageSpace         Images       = {};   // [-] - target images and their layout record
    TargetSpace        Targets      = {};   // [-] - shared target claims
    SpanSpace          Spans        = {};   // [-] - visibility positions, triangles and uniforms
    ShaderCodec        Streams      = {};   // [-] - lowered visibility shaders
    DescriptorIndex    Descriptors  = {};   // [-] - visibility descriptor layouts and sets
    ProgramIndex       Programs     = {};   // [-] - visibility graphics program
    AttachmentIndex    Attachments  = {};   // [-] - classic visibility render construct
    VisibilityRaster   Raster       = {};   // [-] - authoritative Earcut triangle residency
    std::uint32_t      ResolveLayout = AbsentDescriptor; // [-] - fixed-white image descriptor declaration
    std::uint32_t      ResolveReservation = AbsentDescriptor; // [-] - per-slot fixed-white descriptors
    std::uint32_t      ResolveProgram = AbsentProgram; // [-] - fixed-white compute program
    bool               Constructed  = false;// [-] - every dependency stands
};

}   // namespace Slate
