//============================================================================================================================================
//                                                            VISIBILITYRASTER.CPP
//============================================================================================================================================
// 🧩 View composition, authoritative triangle residency, and the recording that writes `16` §4's targets.

#include "SlateCompute/Compute/VisibilityIndex/Api/VisibilityRaster.h"

#include <cstring>
#include <utility>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE COMPOSITION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📐 A column-major product, the outer operand applied second. Written here rather than reached for because
//    `02` holds no matrix product at all — it holds `Compound`, which multiplies decomposed transforms without
//    ever forming one, and the projection this composes with is not decomposable.
ProjectedTransform ComposeCoefficients(const ProjectedTransform& Outer, const ProjectedTransform& Inner)
{
    ProjectedTransform Composed;

    for (std::uint32_t Column = 0u; Column < 4u; ++Column)
    {
        for (std::uint32_t Row = 0u; Row < 4u; ++Row)
        {
            double Accumulated = 0.0;

            for (std::uint32_t Inner_ = 0u; Inner_ < 4u; ++Inner_)
                Accumulated += Outer.Coefficient[Inner_ * 4u + Row] * Inner.Coefficient[Column * 4u + Inner_];

            Composed.Coefficient[Column * 4u + Row] = Accumulated;
        }
    }

    return Composed;
}

}   // namespace

ProjectedTransform ComposeVisibilityTransform(const ViewProjection&     Viewing,
                                              const ProjectedTransform& Placement,
                                              DocumentPosition          ObjectOrigin)
{
    // 🔴 The subtraction happens here, at 64 bits, and it is the whole reason the composition is on the host.
    //    `Rebase` narrows on its way out — which is admissible because what it returns is already relative to
    //    the camera — and the narrowed ordinates then re-enter a 64-bit composition as the fourth column.
    const DevicePosition Rebased = Rebase(ObjectOrigin, Viewing.ViewOrigin);

    ProjectedTransform Placed = Placement;

    Placed.Coefficient[12] = static_cast<double>(Rebased.PositionX);
    Placed.Coefficient[13] = static_cast<double>(Rebased.PositionY);
    Placed.Coefficient[14] = static_cast<double>(Rebased.PositionZ);
    Placed.Coefficient[15] = 1.0;

    return ComposeCoefficients(Viewing.Composed, Placed);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> VisibilityRaster::ConstructVisibilityRaster(SpanSpace&        Spans,
                                          ShaderCodec&      Modules,
                                          DescriptorIndex&  Descriptors,
                                          ProgramIndex&     Programs,
                                          AttachmentIndex&  Attachments)
{
    SpanEdge       = &Spans;
    ModuleEdge     = &Modules;
    DescriptorEdge = &Descriptors;
    ProgramEdge    = &Programs;
    AttachmentEdge = &Attachments;

    // 🔴 The four slots and their order are the shader's, verified against the lowered stream rather than
    //    assumed: `Projecting` at nought, `ObjectPositions` at one, `DrawnTriangles` at two, `SurvivingRun` at
    //    three. A layout declaring them in another order is one the vendor accepts and the device then reads a
    //    triangle out of the uniform for.
    std::vector<DescriptorSlot> Declared;

    DescriptorSlot Projecting;
    Projecting.SlotIndex    = 0u;
    Projecting.Carried        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    Projecting.CarriedCount   = 1u;
    Projecting.ReachingStages = VK_SHADER_STAGE_VERTEX_BIT;

    DescriptorSlot Positions;
    Positions.SlotIndex     = 1u;
    Positions.Carried         = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Positions.CarriedCount    = 1u;
    Positions.ReachingStages  = VK_SHADER_STAGE_VERTEX_BIT;

    DescriptorSlot Triangles;
    Triangles.SlotIndex     = 2u;
    Triangles.Carried         = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Triangles.CarriedCount    = 1u;
    Triangles.ReachingStages  = VK_SHADER_STAGE_VERTEX_BIT;

    // 📝 Bound on every route. The entry point names it statically, so the vendor requires it present whether
    //    or not `SurvivingResolved` routes the corner through it.
    DescriptorSlot Surviving;
    Surviving.SlotIndex     = 3u;
    Surviving.Carried         = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Surviving.CarriedCount    = 1u;
    Surviving.ReachingStages  = VK_SHADER_STAGE_VERTEX_BIT;

    Declared.push_back(Projecting);
    Declared.push_back(Positions);
    Declared.push_back(Triangles);
    Declared.push_back(Surviving);

    const Deliver<std::uint32_t> Layout = DescriptorEdge->Declare(Declared);

    if (!Layout.Resolved)
        return Deliver<bool>::Refuse(Layout.Error);

    LayoutIndex = Layout.Resolve();

    // 📝 The stems are the source file names without their extension, which is what the build lowers each
    //    `[shader(...)]` translation to. Each carries exactly one entry point, so each is named `main` and
    //    `ShaderCodec::Stage`'s declaration of that spelling holds.
    const Deliver<std::uint32_t> Corner = ModuleEdge->Resolve("SlateCompute", "VisibilityCorner");

    if (!Corner.Resolved)
        return Deliver<bool>::Refuse(Corner.Error);

    const Deliver<std::uint32_t> Surface = ModuleEdge->Resolve("SlateCompute", "VisibilitySurface");

    if (!Surface.Resolved)
        return Deliver<bool>::Refuse(Surface.Error);

    CornerModule  = Corner.Resolve();
    SurfaceModule = Surface.Resolve();

    // 🔴 The colour order is the fragment's own output order — `SV_Target0` is the visibility word and
    //    `SV_Target1` the occupancy. `AttachmentIndex` declares the run rather than deriving it from the
    //    produced set, and a run listed the other way round writes the occupancy into two unsigned integers.
    ConstructDeclaration Declaring;
    Declaring.ColourTargets = { SharedTarget::VisibilityIndex, SharedTarget::OccupancySurface };
    Declaring.DepthTarget   = static_cast<std::uint32_t>(SharedTarget::DepthSurface);

    const Deliver<std::uint32_t> Construct_ = AttachmentEdge->Declare(Declaring);

    if (!Construct_.Resolved)
        return Deliver<bool>::Refuse(Construct_.Error);

    ConstructIndex = Construct_.Resolve();

    const Deliver<VkRenderPass> Current = AttachmentEdge->ConstructOf(ConstructIndex);

    if (!Current.Resolved)
        return Deliver<bool>::Refuse(Current.Error);

    // 🔴 `MotionSurface` is the fourth target `16` §4.2 declares and it is not among these two. 🚧 It is the
    //    difference between this projection and the previous rotation's, and no previous projection crosses the
    //    device edge yet — the recording produces three targets until the second transform lands.
    GraphicsDeclaration Programmed;
    Programmed.VertexModule          = CornerModule;
    Programmed.FragmentModule        = SurfaceModule;
    Programmed.LayoutIndexs        = { LayoutIndex };
    Programmed.RenderConstruct       = Current.Resolve();
    Programmed.ColourAttachmentCount = 2u;
    Programmed.Assembled             = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    Programmed.FacingCulled          = VK_CULL_MODE_BACK_BIT;
    Programmed.Depth.DepthTested     = true;
    Programmed.Depth.DepthWritten    = true;
    Programmed.Depth.DepthComparison = VK_COMPARE_OP_GREATER;

    const Deliver<std::uint32_t> Program = ProgramEdge->DeclareGraphics(Programmed);

    if (!Program.Resolved)
        return Deliver<bool>::Refuse(Program.Error);

    ProgramSlotIndex = Program.Resolve();

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE TRIANGLE ARRANGEMENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::vector<UploadedTriangle>> VisibilityRaster::ArrangeTriangles(
    const PartitionStructure&        Registered,
    const GeometryRenderingSnapshot& Rendering,
    std::uint32_t                    RegistrationBase) const
{
    using Arranged = std::vector<UploadedTriangle>;

    const DerivedPartitioning& Current = Registered.Current();
    if (Rendering.Vertices.empty() || Rendering.Triangles.empty())
        return Deliver<Arranged>::Refuse({ RefusalReason::ContentUnsupported,
                                           "the rendering packet carries no drawable triangles" });

    std::uint32_t GreatestFace = 0u;
    for (const std::uint32_t Face : Current.OrderedFaces)
        GreatestFace = Face > GreatestFace ? Face : GreatestFace;

    std::vector<std::uint32_t> PartitionOfFace(static_cast<std::size_t>(GreatestFace) + 1u,
                                                AbsentPartition);
    for (std::uint32_t PartitionIndex = 0u;
         PartitionIndex < Current.Partitions.size(); ++PartitionIndex)
    {
        const MicroSurfacePartition& Partitioned = Current.Partitions[PartitionIndex];
        for (std::uint32_t Spanned = 0u; Spanned < Partitioned.FaceCount; ++Spanned)
        {
            const std::size_t Ordered = static_cast<std::size_t>(Partitioned.FirstFace) + Spanned;
            if (Ordered >= Current.OrderedFaces.size())
                return Deliver<Arranged>::Refuse({ RefusalReason::ContentUnsupported,
                                                   "a partition spans past the derived ordering" });

            const std::uint32_t Face = Current.OrderedFaces[Ordered];
            if (Face >= PartitionOfFace.size())
                return Deliver<Arranged>::Refuse({ RefusalReason::ContentUnsupported,
                                                   "the ordering names an unavailable source face" });
            PartitionOfFace[Face] = PartitionIndex;
        }
    }

    std::vector<Arranged> ByPartition(Current.Partitions.size());
    for (const GeometryRenderingTriangle& Source : Rendering.Triangles)
    {
        if (Source.SourceFace >= PartitionOfFace.size() ||
            PartitionOfFace[Source.SourceFace] == AbsentPartition)
        {
            return Deliver<Arranged>::Refuse({ RefusalReason::ContentUnsupported,
                                               "an Earcut triangle names a face outside the partitioning" });
        }
        if (Source.Corners[0] >= Rendering.Vertices.size() ||
            Source.Corners[1] >= Rendering.Vertices.size() ||
            Source.Corners[2] >= Rendering.Vertices.size())
        {
            return Deliver<Arranged>::Refuse({ RefusalReason::ContentUnsupported,
                                               "an Earcut triangle names an unavailable rendering corner" });
        }

        const std::uint32_t PartitionIndex = PartitionOfFace[Source.SourceFace];
        Arranged& PartitionTriangles = ByPartition[PartitionIndex];

        UploadedTriangle Triangle;
        Triangle.CornerVertex0  = Source.Corners[0];
        Triangle.CornerVertex1  = Source.Corners[1];
        Triangle.CornerVertex2  = Source.Corners[2];
        Triangle.PartitionIndex = RegistrationBase + PartitionIndex;
        Triangle.TriangleIndex  = static_cast<std::uint32_t>(PartitionTriangles.size());
        PartitionTriangles.push_back(Triangle);
    }

    Arranged Drawn;
    Drawn.reserve(Rendering.Triangles.size());
    for (std::uint32_t PartitionIndex = 0u;
         PartitionIndex < Current.Partitions.size(); ++PartitionIndex)
    {
        const Arranged& PartitionTriangles = ByPartition[PartitionIndex];
        if (PartitionTriangles.size() != Current.Partitions[PartitionIndex].TriangleCount)
        {
            return Deliver<Arranged>::Refuse({ RefusalReason::ContentUnsupported,
                                               "Earcut output disagrees with the partition triangle count" });
        }
        Drawn.insert(Drawn.end(), PartitionTriangles.begin(), PartitionTriangles.end());
    }

    return Deliver<Arranged>::Result(std::move(Drawn));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE STAGING
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> VisibilityRaster::Stage(const void*      Incoming,
                                               VkDeviceSize     IncomingBytes,
                                               SpanIntent       Intent,
                                               VkCommandBuffer  Recorded)
{
    SpanShape Staging;
    Staging.SpanBytes = IncomingBytes;
    Staging.Intent    = SpanIntent::TransferSource;
    Staging.Residency = ExtentResidency::HostWritable;

    const Deliver<SpanReservation> Staged = SpanEdge->Reserve(Staging);

    if (!Staged.Resolved)
        return Deliver<std::uint32_t>::Refuse(Staged.Error);

    const std::uint32_t StagedIndex = Staged.Resolve().SpanIndex;

    // 📝 Retained before the write, so that a refusal below still releases it at the next surrender. A staging
    //    span recorded nowhere is one nothing returns until the device is torn down.
    StagedSpans.push_back(StagedIndex);

    const Deliver<bool> Written = SpanEdge->Amend(StagedIndex, Incoming, IncomingBytes, 0u);

    if (!Written.Resolved)
        return Deliver<std::uint32_t>::Refuse(Written.Error);

    SpanShape Occupying;
    Occupying.SpanBytes = IncomingBytes;
    Occupying.Intent    = Intent;
    Occupying.Residency = ExtentResidency::DeviceLocal;

    const Deliver<SpanReservation> Reserved = SpanEdge->Reserve(Occupying);

    if (!Reserved.Resolved)
        return Deliver<std::uint32_t>::Refuse(Reserved.Error);

    const std::uint32_t ResidentIndex = Reserved.Resolve().SpanIndex;

    const Deliver<bool> Carried = SpanEdge->Transfer(Recorded, StagedIndex, ResidentIndex, IncomingBytes);

    if (!Carried.Resolved)
    {
        SpanEdge->Release(ResidentIndex);
        return Deliver<std::uint32_t>::Refuse(Carried.Error);
    }

    return Deliver<std::uint32_t>::Result(ResidentIndex);
}

void VisibilityRaster::Abandon(ResidentPartitioning& Abandoned)
{
    if (SpanEdge == nullptr)
        return;

    for (const std::uint32_t Uniform : Abandoned.UniformSpans)
    {
        if (Uniform != AbsentSpan)
            SpanEdge->Release(Uniform);
    }

    if (Abandoned.PositionSpan != AbsentSpan)
        SpanEdge->Release(Abandoned.PositionSpan);

    if (Abandoned.TriangleSpan != AbsentSpan)
        SpanEdge->Release(Abandoned.TriangleSpan);

    Abandoned = ResidentPartitioning{};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE RESIDENCY
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> VisibilityRaster::Resolve(const PartitionStructure&        Registered,
                                                 const GeometryRenderingSnapshot& Rendering,
                                                 std::uint32_t              RegistrationBase,
                                                 const OcclusionScheduler*  Culling,
                                                 std::uint32_t              CullingIndex,
                                                 VkCommandBuffer            Recorded)
{
    if (SpanEdge == nullptr || DescriptorEdge == nullptr)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::CapabilityAbsent, "nothing was constructed" });

    if (Recorded == VK_NULL_HANDLE)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "no recording was supplied" });

    if (!Registered.PartitioningCurrent())
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "no partitioning stands" });

    // 🔴 The two must describe one revision. A partitioning derived from an earlier seal can group source-face
    //    ordinals differently from this Earcut packet even though neither side renumbers the author's faces.
    if (Registered.DescribedRevision() != Rendering.TopologyRevision)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::IdentityStale, "the partitioning describes another revision of the topology" });
    }

    const Deliver<std::vector<UploadedTriangle>> Arranged =
        ArrangeTriangles(Registered, Rendering, RegistrationBase);

    if (!Arranged.Resolved)
        return Deliver<std::uint32_t>::Refuse(Arranged.Error);

    const std::vector<UploadedTriangle>& Drawn = Arranged.Resolve();

    // 🔴 Narrowed here and nowhere else, and narrowed in **object** space where the extent is the owner's own.
    //    `02`'s rule is that a document position never narrows; an object position is not one, and the rebasing
    //    it would otherwise need rides in the composed transform instead.
    std::vector<UploadedPosition> Narrowed;
    Narrowed.reserve(Rendering.Vertices.size());

    for (const GeometryRenderingVertex& Held : Rendering.Vertices)
    {
        UploadedPosition Uploaded;
        Uploaded.PositionX = static_cast<float>(Held.Position.PositionX);
        Uploaded.PositionY = static_cast<float>(Held.Position.PositionY);
        Uploaded.PositionZ = static_cast<float>(Held.Position.PositionZ);

        Narrowed.push_back(Uploaded);
    }

    ResidentPartitioning Incoming;
    Incoming.VertexCount    = static_cast<std::uint32_t>(Narrowed.size());
    Incoming.TriangleCount  = static_cast<std::uint32_t>(Drawn.size());
    Incoming.RegistrationBase  = RegistrationBase;
    Incoming.PartitionCount = Registered.PartitionCount();
    Incoming.CullingIndex = Culling != nullptr ? CullingIndex : AbsentSpan;

    const Deliver<std::uint32_t> Positions =
        Stage(Narrowed.data(),
              static_cast<VkDeviceSize>(Narrowed.size() * sizeof(UploadedPosition)),
              SpanIntent::StorageRead,
              Recorded);

    if (!Positions.Resolved)
        return Deliver<std::uint32_t>::Refuse(Positions.Error);

    Incoming.PositionSpan = Positions.Resolve();

    const Deliver<std::uint32_t> Triangles =
        Stage(Drawn.data(),
              static_cast<VkDeviceSize>(Drawn.size() * sizeof(UploadedTriangle)),
              SpanIntent::StorageRead,
              Recorded);

    if (!Triangles.Resolved)
    {
        Abandon(Incoming);
        return Deliver<std::uint32_t>::Refuse(Triangles.Error);
    }

    Incoming.TriangleSpan = Triangles.Resolve();

    // 📝 One host-writable uniform per cycle slot. `06` §2.1 accepts explicit sets per slot precisely so that
    //    the block a recording writes is never the block the previous rotation is still reading, and a single
    //    block shared across the depth would reintroduce that read at the one site the depth exists for.
    for (std::uint32_t SlotIndex = 0u; SlotIndex < RecordingSlotCount; ++SlotIndex)
    {
        SpanShape Uniform;
        Uniform.SpanBytes = static_cast<VkDeviceSize>(sizeof(UploadedProjection));
        Uniform.Intent    = SpanIntent::UniformRead;
        Uniform.Residency = ExtentResidency::HostWritable;

        const Deliver<SpanReservation> Reserved = SpanEdge->Reserve(Uniform);

        if (!Reserved.Resolved)
        {
            Abandon(Incoming);
            return Deliver<std::uint32_t>::Refuse(Reserved.Error);
        }

        Incoming.UniformSpans.push_back(Reserved.Resolve().SpanIndex);
    }

    // 📝 One claim for the direct route and one per culling phase. Three rather than one because slot three
    //    names a different span in each, and a set is written once at registration and never rewritten.
    for (std::uint32_t ReservationIndex = 0u; ReservationIndex < RasterReservationCount; ++ReservationIndex)
    {
        if (ReservationIndex != DirectReservationIndex && Incoming.CullingIndex == AbsentSpan)
            break;

        const Deliver<std::uint32_t> Reservation = DescriptorEdge->Reserve(LayoutIndex);

        if (!Reservation.Resolved)
        {
            Abandon(Incoming);
            return Deliver<std::uint32_t>::Refuse(Reservation.Error);
        }

        Incoming.ReservationIndexs.push_back(Reservation.Resolve());
    }

    // 🔴 Written once per cycle slot, here, and never inside a recording. Every span the set names stands for
    //    the residency's whole life, so a per-slot write would rewrite one arrangement with itself — and
    //    would write it into a set the previous rotation's recording still reads.
    for (std::uint32_t ReservationIndex = 0u; ReservationIndex < Incoming.ReservationIndexs.size(); ++ReservationIndex)
    {
        for (std::uint32_t SlotIndex = 0u; SlotIndex < RecordingSlotCount; ++SlotIndex)
        {
            const Deliver<SpanReservation> Uniform  = SpanEdge->Current(Incoming.UniformSpans[SlotIndex]);
            const Deliver<SpanReservation> Position = SpanEdge->Current(Incoming.PositionSpan);
            const Deliver<SpanReservation> Triangle = SpanEdge->Current(Incoming.TriangleSpan);

            if (!Uniform.Resolved || !Position.Resolved || !Triangle.Resolved)
            {
                Abandon(Incoming);
                return Deliver<std::uint32_t>::Refuse(
                    { RefusalReason::ContentUnsupported, "a span the set names no longer stands" });
            }

            // 📝 The direct claim binds the triangle span at slot three. It is a valid storage read the entry
            //    point never performs, which is what keeps one program serving both routes.
            VkBuffer     SurvivingExtent = Triangle.Resolve().Extent;
            VkDeviceSize SurvivingBytes  = Triangle.Resolve().SpanBytes;

            if (ReservationIndex != DirectReservationIndex)
            {
                const CullingPhase Phase = static_cast<CullingPhase>(ReservationIndex - 1u);

                const Deliver<VkBuffer> Compacted =
                    Culling->SurvivingOf(Incoming.CullingIndex, SlotIndex, Phase);

                if (!Compacted.Resolved)
                {
                    Abandon(Incoming);
                    return Deliver<std::uint32_t>::Refuse(Compacted.Error);
                }

                SurvivingExtent = Compacted.Resolve();
                SurvivingBytes  = VK_WHOLE_SIZE;
            }

            DescriptorContent Projecting;
            Projecting.SlotIndex = 0u;
            Projecting.SpanExtent  = Uniform.Resolve().Extent;
            Projecting.SpanBytes   = Uniform.Resolve().SpanBytes;

            DescriptorContent Positions_;
            Positions_.SlotIndex = 1u;
            Positions_.SpanExtent  = Position.Resolve().Extent;
            Positions_.SpanBytes   = Position.Resolve().SpanBytes;

            DescriptorContent Triangles_;
            Triangles_.SlotIndex = 2u;
            Triangles_.SpanExtent  = Triangle.Resolve().Extent;
            Triangles_.SpanBytes   = Triangle.Resolve().SpanBytes;

            DescriptorContent Surviving_;
            Surviving_.SlotIndex = 3u;
            Surviving_.SpanExtent  = SurvivingExtent;
            Surviving_.SpanBytes   = SurvivingBytes;

            const std::vector<DescriptorContent> Amending =
                { Projecting, Positions_, Triangles_, Surviving_ };

            const Deliver<bool> Amended =
                DescriptorEdge->Amend(Incoming.ReservationIndexs[ReservationIndex], SlotIndex, Amending);

            if (!Amended.Resolved)
            {
                Abandon(Incoming);
                return Deliver<std::uint32_t>::Refuse(Amended.Error);
            }
        }
    }

    const std::uint32_t ResidencyIndex = static_cast<std::uint32_t>(Resident.size());

    Resident.push_back(Incoming);

    return Deliver<std::uint32_t>::Result(ResidencyIndex);
}

void VisibilityRaster::Release()
{
    if (SpanEdge == nullptr)
        return;

    for (const std::uint32_t Staged : StagedSpans)
        SpanEdge->Release(Staged);

    StagedSpans.clear();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DERIVATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> VisibilityRaster::Derive(std::uint32_t DisplayX, std::uint32_t DisplayY)
{
    if (AttachmentEdge == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "nothing was constructed" });

    return AttachmentEdge->Derive(DisplayX, DisplayY);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE RECORDING
//------------------------------------------------------------------------------------------------------------------------

Deliver<ConstructedSpan> VisibilityRaster::Open(VkCommandBuffer Recorded, ConstructedProgram& Constructed)
{
    const Deliver<ConstructedSpan> Spanned = AttachmentEdge->Resolve(ConstructIndex);

    if (!Spanned.Resolved)
        return Spanned;

    const Deliver<ConstructedProgram> Program = ProgramEdge->Resolve(ProgramSlotIndex);

    if (!Program.Resolved)
        return Deliver<ConstructedSpan>::Refuse(Program.Error);

    const ConstructedSpan& Covering = Spanned.Resolve();

    Constructed = Program.Resolve();

    // 🔴 Three clears in the construct's own attachment order — the two colour targets, then the depth. The
    //    visibility target clears to `AbsentPartition`, which is `16` §5's unoccupied class and is recognised
    //    downstream by exactly this magnitude; the depth clears to `FarPlaneDepth`, which is nought under the
    //    reversed convention and unity under the ordinary one. Clearing to unity against a greater-than
    //    comparison resolves nothing at all, and the image is empty rather than sorted wrongly.
    VkClearValue Cleared[3] = {};
    Cleared[0].color.uint32[0] = AbsentPartition;
    Cleared[0].color.uint32[1] = 0u;
    Cleared[1].color.float32[0] = 0.0f;
    Cleared[2].depthStencil.depth   = static_cast<float>(FarPlaneDepth);
    Cleared[2].depthStencil.stencil = 0u;

    VkRenderPassBeginInfo Opening = {};
    Opening.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    Opening.renderPass               = Covering.RenderConstruct;
    Opening.framebuffer              = Covering.SpannedTargets;
    Opening.renderArea.offset.x      = 0;
    Opening.renderArea.offset.y      = 0;
    Opening.renderArea.extent.width  = Covering.SpannedWidth;
    Opening.renderArea.extent.height = Covering.SpannedHeight;
    Opening.clearValueCount          = 3u;
    Opening.pClearValues             = Cleared;

    vkCmdBeginRenderPass(Recorded, &Opening, VK_SUBPASS_CONTENTS_INLINE);

    // 📝 The extent is the derived one and not what the display currently reports. `AttachmentIndex` delivers it
    //    beside the span for this reason: a viewport derived a second time from the display is the one number
    //    that can disagree with the span a resize has not yet re-derived.
    VkViewport Displayed = {};
    Displayed.x        = 0.0f;
    Displayed.y        = 0.0f;
    Displayed.width    = static_cast<float>(Covering.SpannedWidth);
    Displayed.height   = static_cast<float>(Covering.SpannedHeight);
    Displayed.minDepth = 0.0f;
    Displayed.maxDepth = 1.0f;

    VkRect2D Bounded = {};
    Bounded.offset.x      = 0;
    Bounded.offset.y      = 0;
    Bounded.extent.width  = Covering.SpannedWidth;
    Bounded.extent.height = Covering.SpannedHeight;

    vkCmdSetViewport(Recorded, 0u, 1u, &Displayed);
    vkCmdSetScissor(Recorded, 0u, 1u, &Bounded);

    vkCmdBindPipeline(Recorded, Constructed.RecordedAs, Constructed.Constructed);

    return Deliver<ConstructedSpan>::Result(Covering);
}

Deliver<bool> VisibilityRaster::Project(const ResidentPartitioning& Current,
                                        std::uint32_t               SlotIndex,
                                        const ViewProjection&       Viewing,
                                        const ConstructedSpan&      Covering,
                                        bool                        SurvivingResolved)
{
    // ⚠️ 🚧 Every owner is composed at the identity placement, because nothing yet supplies one. `56` holds
    //    the placements and `ComposeVisibilityTransform` already accepts one — the argument arrives with it.
    const ProjectedTransform Composed =
        ComposeVisibilityTransform(Viewing, ProjectedTransform{}, DocumentPosition{});

    UploadedProjection Projecting;

    for (std::uint32_t Coefficient = 0u; Coefficient < 16u; ++Coefficient)
        Projecting.ComposedCoefficient[Coefficient] = static_cast<float>(Composed.Coefficient[Coefficient]);

    Projecting.DisplayX        = Covering.SpannedWidth;
    Projecting.DisplayY       = Covering.SpannedHeight;
    Projecting.RegistrationBase       = Current.RegistrationBase;
    Projecting.DrawnPartitionCount = Current.PartitionCount;
    Projecting.SurvivingResolved   = SurvivingResolved ? 1u : 0u;

    return SpanEdge->Amend(Current.UniformSpans[SlotIndex],
                           &Projecting,
                           static_cast<VkDeviceSize>(sizeof(Projecting)),
                           0u);
}

Deliver<bool> VisibilityRaster::Record(VkCommandBuffer        Recorded,
                                       std::uint32_t          SlotIndex,
                                       const ViewProjection&  Viewing)
{
    if (SpanEdge == nullptr || ProgramEdge == nullptr || AttachmentEdge == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "nothing was constructed" });

    if (Recorded == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no recording was supplied" });

    if (SlotIndex >= RecordingSlotCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the cycle slot is outside the depth" });

    ConstructedProgram Constructed;

    const Deliver<ConstructedSpan> Opened = Open(Recorded, Constructed);

    if (!Opened.Resolved)
        return Deliver<bool>::Refuse(Opened.Error);

    const ConstructedSpan& Covering = Opened.Resolve();

    for (const ResidentPartitioning& Current : Resident)
    {
        if (Current.TriangleCount == 0u)
            continue;

        const Deliver<bool> Written = Project(Current, SlotIndex, Viewing, Covering, false);

        if (!Written.Resolved)
        {
            vkCmdEndRenderPass(Recorded);
            return Deliver<bool>::Refuse(Written.Error);
        }

        const Deliver<VkDescriptorSet> Reaching =
            DescriptorEdge->Resolve(Current.ReservationIndexs[DirectReservationIndex], SlotIndex);

        if (!Reaching.Resolved)
        {
            vkCmdEndRenderPass(Recorded);
            return Deliver<bool>::Refuse(Reaching.Error);
        }

        const VkDescriptorSet Reached = Reaching.Resolve();

        vkCmdBindDescriptorSets(Recorded, Constructed.RecordedAs, Constructed.ReachedLayout,
                                0u, 1u, &Reached, 0u, nullptr);

        // 🔴 Three corners per triangle and no index span. The vertex entry point reaches its corner by division
        //    and remainder over `SV_VertexID`, which is what lets one program serve every partitioning — `16` §4
        //    forbids a per-topology input declaration and this is the draw that shape implies.
        vkCmdDraw(Recorded, Current.TriangleCount * 3u, 1u, 0u, 0u);
    }

    vkCmdEndRenderPass(Recorded);

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE INDIRECT RECORDING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> VisibilityRaster::RecordIndirect(VkCommandBuffer           Recorded,
                                               std::uint32_t             SlotIndex,
                                               const ViewProjection&     Viewing,
                                               const OcclusionScheduler& Culling,
                                               CullingPhase              Phase)
{
    if (SpanEdge == nullptr || ProgramEdge == nullptr || AttachmentEdge == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "nothing was constructed" });

    if (Recorded == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no recording was supplied" });

    if (SlotIndex >= RecordingSlotCount || Phase == CullingPhase::PhaseCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such cycle slot or phase" });

    ConstructedProgram Constructed;

    const Deliver<ConstructedSpan> Opened = Open(Recorded, Constructed);

    if (!Opened.Resolved)
        return Deliver<bool>::Refuse(Opened.Error);

    const ConstructedSpan& Covering       = Opened.Resolve();
    const std::uint32_t    ReservationIndex   = 1u + static_cast<std::uint32_t>(Phase);

    for (const ResidentPartitioning& Current : Resident)
    {
        if (Current.TriangleCount == 0u)
            continue;

        // 🔴 A residency that declared no culling ordinal is rejected rather than drawn directly. Falling back
        //    would draw every triangle of it beside the compacted survivors of its neighbours, and the artist
        //    would meet one owner of a scene costing what the whole scene costs with no way to see why.
        if (Current.CullingIndex == AbsentSpan || Current.ReservationIndexs.size() <= ReservationIndex)
        {
            vkCmdEndRenderPass(Recorded);
            return Deliver<bool>::Refuse(
                { RefusalReason::ContentUnsupported, "the residency declared no culling ordinal" });
        }

        const Deliver<bool> Written = Project(Current, SlotIndex, Viewing, Covering, true);

        if (!Written.Resolved)
        {
            vkCmdEndRenderPass(Recorded);
            return Deliver<bool>::Refuse(Written.Error);
        }

        const Deliver<VkBuffer> Recording =
            Culling.RecordOf(Current.CullingIndex, SlotIndex, Phase);

        if (!Recording.Resolved)
        {
            vkCmdEndRenderPass(Recorded);
            return Deliver<bool>::Refuse(Recording.Error);
        }

        const Deliver<VkDescriptorSet> Reaching =
            DescriptorEdge->Resolve(Current.ReservationIndexs[ReservationIndex], SlotIndex);

        if (!Reaching.Resolved)
        {
            vkCmdEndRenderPass(Recorded);
            return Deliver<bool>::Refuse(Reaching.Error);
        }

        const VkDescriptorSet Reached = Reaching.Resolve();

        vkCmdBindDescriptorSets(Recorded, Constructed.RecordedAs, Constructed.ReachedLayout,
                                0u, 1u, &Reached, 0u, nullptr);

        // 🔴 One draw and not one per partition. The compaction wrote a contiguous run of triangle ordinals and
        //    advanced one corner count, so the whole surviving set of a residency issues as a single draw whose
        //    extent the host never learns.
        vkCmdDrawIndirect(Recorded, Recording.Resolve(), 0u, 1u, 0u);
    }

    vkCmdEndRenderPass(Recorded);

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

void VisibilityRaster::Reclaim()
{
    Release();

    for (ResidentPartitioning& Current : Resident)
        Abandon(Current);

    Resident.clear();
}

std::uint32_t VisibilityRaster::ResidentCount() const
{
    return static_cast<std::uint32_t>(Resident.size());
}

std::uint32_t VisibilityRaster::DrawnTriangleCount() const
{
    std::uint32_t Drawn = 0u;

    for (const ResidentPartitioning& Current : Resident)
        Drawn += Current.TriangleCount;

    return Drawn;
}

bool VisibilityRaster::ProgramCurrent() const
{
    return ProgramSlotIndex != AbsentProgram;
}

}   // namespace Slate
