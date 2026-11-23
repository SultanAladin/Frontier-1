//============================================================================================================================================
//                                                      GEOMETRYDEVICEEXCHANGE.CPP
//============================================================================================================================================
// 🧩 Constructs the device estate before any real geometry source is admitted to visibility residency.

#include "SlateCompute/Compute/GeometryDeviceExchange/Api/GeometryDeviceExchange.h"

#include "Foundation/NumericTolerance.h"

namespace Slate
{

GeometryDeviceExchange::~GeometryDeviceExchange()
{
    Reclaim();
}

Deliver<bool> GeometryDeviceExchange::ConstructGeometryDeviceExchange(
    const VulkanExchange&      Exchange,
    const DiagnosticExtension& Naming,
    const char*                 ShaderLocation,
    std::uint32_t               DisplayWidth,
    std::uint32_t               DisplayHeight,
    VkFormat                     DisplayFormat)
{
    if (Constructed)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::HostDenied, "the geometry device exchange already stands" });
    }

    if (ShaderLocation == nullptr || ShaderLocation[0] == '\0')
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "no geometry shader stream location was supplied" });
    }

    DeviceEdge = &Exchange;

    if (!Bytes.ReserveByteSpace(Exchange, Naming).Resolved ||
        !Images.ConstructImageSpace(Exchange, Bytes, Naming).Resolved ||
        !Targets.Reserve(Images, DisplayWidth, DisplayHeight, DisplayFormat).Resolved ||
        !Spans.ConstructSpanSpace(Exchange, Bytes, Naming).Resolved ||
        !Streams.AttachShaderStreams(Exchange, ShaderLocation).Resolved ||
        !Descriptors.ConstructDescriptorIndex(Exchange, Naming).Resolved ||
        !Programs.ConstructProgramIndex(Exchange, Streams, Descriptors, Naming).Resolved ||
        !Attachments.ConstructAttachmentIndex(Exchange, Targets).Resolved)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                       "the geometry visibility device estate was rejected" });
    }

    VkSamplerCreateInfo Sampling = {};
    Sampling.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    Sampling.magFilter = VK_FILTER_LINEAR;
    Sampling.minFilter = VK_FILTER_LINEAR;
    Sampling.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    Sampling.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Sampling.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Sampling.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Sampling.maxLod = 0.0f;
    if (vkCreateSampler(Exchange.ActiveDevice(), &Sampling, nullptr, &RadianceSampling) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted,
                                       "the geometry radiance sampler was rejected" });
    }
    Discard(Naming.Declare(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<std::uint64_t>(RadianceSampling),
                           "GeometryDeviceExchange radiance sampler"));

    DescriptorSlot Visibility;
    Visibility.SlotIndex = 0u;
    Visibility.Carried = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    Visibility.ReachingStages = VK_SHADER_STAGE_COMPUTE_BIT;

    DescriptorSlot Radiance;
    Radiance.SlotIndex = 1u;
    Radiance.Carried = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    Radiance.ReachingStages = VK_SHADER_STAGE_COMPUTE_BIT;

    const Deliver<std::uint32_t> Layout = Descriptors.Declare({ Visibility, Radiance });
    const Deliver<std::uint32_t> Module = Streams.Resolve("SlateCompute", "FixedWhiteSurface");
    if (!Layout.Resolved || !Module.Resolved ||
        !Raster.ConstructVisibilityRaster(Spans, Streams, Descriptors, Programs, Attachments).Resolved)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                       "the geometry visibility declarations were rejected" });
    }

    ResolveLayout = Layout.Resolve();
    const Deliver<std::uint32_t> Program = Programs.DeclareCompute(
        ComputeDeclaration{ .ModuleIndex = Module.Resolve(), .LayoutIndexs = { ResolveLayout } });
    // One resolve set plus the direct descriptor claim for every independently resident geometry packet. The
    // declaration is fixed before the first recording, so a later import cannot resize a pool a submitted draw
    // still reads.
    if (!Program.Resolved || !Descriptors.Fix(GeometryResidencyLimit + 1u).Resolved)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                       "the fixed-white resolve program was rejected" });
    }

    ResolveProgram = Program.Resolve();
    const Deliver<std::uint32_t> Reserved = Descriptors.Reserve(ResolveLayout);
    if (!Reserved.Resolved || !Raster.Derive(DisplayWidth, DisplayHeight).Resolved)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                       "the geometry visibility targets could not be derived" });
    }

    ResolveReservation = Reserved.Resolve();
    Constructed = true;
    return Deliver<bool>::Result(true);
}

Deliver<bool> GeometryDeviceExchange::ReclaimDisplay(std::uint32_t DisplayWidth, std::uint32_t DisplayHeight)
{
    if (!Constructed)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::CapabilityAbsent, "the geometry device exchange does not stand" });
    }

    Attachments.Release();

    const Deliver<bool> Reclaimed = Targets.Reclaim(DisplayWidth, DisplayHeight);
    if (!Reclaimed.Resolved)
        return Reclaimed;

    return Raster.Derive(DisplayWidth, DisplayHeight);
}

Deliver<std::uint32_t> GeometryDeviceExchange::Admit(const PartitionStructure&        Partitioned,
                                                     const GeometryRenderingSnapshot& Rendering,
                                                     std::uint32_t                     RegistrationBase,
                                                     VkCommandBuffer                   Recorded)
{
    if (!Constructed || Recorded == VK_NULL_HANDLE)
    {
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::CapabilityAbsent,
                                                "the geometry device exchange does not stand" });
    }

    return Raster.Resolve(Partitioned, Rendering, RegistrationBase, nullptr, AbsentSpan, Recorded);
}

VisibilityRaster& GeometryDeviceExchange::Visibility()
{
    return Raster;
}

Deliver<bool> GeometryDeviceExchange::Record(VkCommandBuffer Recorded,
                                            std::uint32_t SlotIndex,
                                            const ViewProjection& Viewing)
{
    if (!Constructed || Recorded == VK_NULL_HANDLE || Raster.ResidentCount() == 0u)
    {
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "no authoritative geometry residency is available to record" });
    }

    const Deliver<std::uint32_t> Visibility = Targets.IndexOf(SharedTarget::VisibilityIndex);
    const Deliver<std::uint32_t> Occupancy = Targets.IndexOf(SharedTarget::OccupancySurface);
    const Deliver<std::uint32_t> Depth = Targets.IndexOf(SharedTarget::DepthSurface);
    if (!Visibility.Resolved || !Occupancy.Resolved || !Depth.Resolved ||
        !Images.Transition(Recorded, Visibility.Resolve(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL).Resolved ||
        !Images.Transition(Recorded, Occupancy.Resolve(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL).Resolved ||
        !Images.Transition(Recorded, Depth.Resolve(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL).Resolved)
    {
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied,
                                       "the visibility targets could not enter their raster layouts" });
    }

    const Deliver<bool> Rasterised = Raster.Record(Recorded, SlotIndex, Viewing);
    if (!Rasterised.Resolved)
        return Rasterised;

    return ResolveFixedWhite(Recorded, SlotIndex);
}

Deliver<bool> GeometryDeviceExchange::ResolveFixedWhite(VkCommandBuffer Recorded, std::uint32_t SlotIndex)
{
    if (!Constructed || Recorded == VK_NULL_HANDLE || SlotIndex >= RecordingSlotCount)
    {
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "no fixed-white resolve recording stands for that cycle slot" });
    }

    const Deliver<ImageReservation> Visibility = Targets.Resolve(SharedTarget::VisibilityIndex);
    const Deliver<ImageReservation> Radiance = Targets.Resolve(SharedTarget::RadianceSurface);
    const Deliver<ConstructedProgram> Program = Programs.Resolve(ResolveProgram);
    if (!Visibility.Resolved || !Radiance.Resolved || !Program.Resolved)
    {
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                       "the fixed-white resolve target or program is unavailable" });
    }

    if (!Images.Transition(Recorded, Visibility.Resolve().ImageIndex, VK_IMAGE_LAYOUT_GENERAL).Resolved ||
        !Images.Transition(Recorded, Radiance.Resolve().ImageIndex, VK_IMAGE_LAYOUT_GENERAL).Resolved)
    {
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied,
                                       "the fixed-white resolve images could not enter general layout" });
    }

    DescriptorContent VisibilityContent;
    VisibilityContent.SlotIndex = 0u;
    VisibilityContent.ImageView = Visibility.Resolve().WholeView;
    VisibilityContent.ImageCurrent = VK_IMAGE_LAYOUT_GENERAL;

    DescriptorContent RadianceContent;
    RadianceContent.SlotIndex = 1u;
    RadianceContent.ImageView = Radiance.Resolve().WholeView;
    RadianceContent.ImageCurrent = VK_IMAGE_LAYOUT_GENERAL;

    if (!Descriptors.Amend(ResolveReservation, SlotIndex, { VisibilityContent, RadianceContent }).Resolved)
    {
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied,
                                       "the fixed-white resolve descriptors were rejected" });
    }

    const Deliver<VkDescriptorSet> Reached = Descriptors.Resolve(ResolveReservation, SlotIndex);
    if (!Reached.Resolved)
        return Deliver<bool>::Refuse(Reached.Error);

    vkCmdBindPipeline(Recorded, Program.Resolve().RecordedAs, Program.Resolve().Constructed);
    const VkDescriptorSet Set = Reached.Resolve();
    vkCmdBindDescriptorSets(Recorded, Program.Resolve().RecordedAs, Program.Resolve().ReachedLayout,
                            0u, 1u, &Set, 0u, nullptr);
    vkCmdDispatch(Recorded, (Radiance.Resolve().Shape.Width + 7u) / 8u,
                  (Radiance.Resolve().Shape.Height + 7u) / 8u, 1u);

    return Images.Transition(Recorded, Radiance.Resolve().ImageIndex, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

Deliver<ImageReservation> GeometryDeviceExchange::Radiance() const
{
    if (!Constructed)
    {
        return Deliver<ImageReservation>::Refuse(
            { RefusalReason::CapabilityAbsent, "the geometry device exchange does not stand" });
    }

    return Targets.Resolve(SharedTarget::RadianceSurface);
}

VkSampler GeometryDeviceExchange::RadianceSampler() const
{
    return RadianceSampling;
}

void GeometryDeviceExchange::Reclaim()
{
    if (DeviceEdge != nullptr && DeviceEdge->ActiveDevice() != VK_NULL_HANDLE &&
        RadianceSampling != VK_NULL_HANDLE)
    {
        vkDestroySampler(DeviceEdge->ActiveDevice(), RadianceSampling, nullptr);
    }

    RadianceSampling = VK_NULL_HANDLE;
    Raster.Reclaim();
    Attachments.Reclaim();
    Programs.Reclaim();
    Descriptors.Reclaim();
    Streams.Reclaim();
    Spans.Reclaim();
    Targets.Release();
    Images.Reclaim();
    Bytes.Reclaim();
    DeviceEdge = nullptr;
    Constructed = false;
}

bool GeometryDeviceExchange::Standing() const
{
    return Constructed;
}

}   // namespace Slate
