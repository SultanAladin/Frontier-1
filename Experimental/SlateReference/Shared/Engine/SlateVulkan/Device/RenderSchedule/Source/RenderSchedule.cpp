//============================================================================================================================================
//                                                            RENDERSCHEDULE.CPP
//============================================================================================================================================
// 🧩 Contribution gating and the ordering derived from declared reads and writes.

#include "SlateVulkan/Device/RenderSchedule/Api/RenderSchedule.h"

#include "Foundation/NumericTolerance.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  TARGET DECLARATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Extent relation per target, declared here once. A resize recreates every display-relative and
//    fraction-of-display target and no absolute one — `06` §4.1 ④ depends on this table being total.
namespace
{
    constexpr ExtentRelation RelationOf[static_cast<std::size_t>(SharedTarget::TargetCount)] =
    {
        ExtentRelation::DisplayRelative,    // DepthSurface
        ExtentRelation::DisplayRelative,    // VisibilityIndex
        ExtentRelation::DisplayRelative,    // OccupancySurface
        ExtentRelation::DisplayRelative,    // MotionSurface
        ExtentRelation::FractionOfDisplay,  // OcclusionSurface
        ExtentRelation::DisplayRelative,    // DirectOcclusionSurface
        ExtentRelation::DisplayRelative,    // TransmissionIndex
        ExtentRelation::DisplayRelative,    // RadianceSurface
        ExtentRelation::FractionOfDisplay,  // ReflectionSurface
        ExtentRelation::DisplayRelative,    // AccumulationSurface
        ExtentRelation::DisplayRelative,    // DisplaySurface
        ExtentRelation::DisplayRelative,    // OutlineSurface
        ExtentRelation::Absolute,           // TransmittanceSurface
        ExtentRelation::Absolute,           // MultiScatterSurface
        ExtentRelation::Absolute            // SkyViewSurface
    };

    constexpr std::size_t TargetSpan = static_cast<std::size_t>(SharedTarget::TargetCount);

    // 📝 Format per target, in the same order and read from `08` §2's table verbatim. The display surface's
    //    own format is the one exception — it is what the vendor's presentation surface declared and is
    //    therefore passed in rather than declared here.
    constexpr VkFormat FormatOf[TargetSpan] =
    {
        VK_FORMAT_D32_SFLOAT,             // DepthSurface
        VK_FORMAT_R32G32_UINT,            // VisibilityIndex — owner ordinal and primitive ordinal
        VK_FORMAT_R8_UNORM,               // OccupancySurface
        VK_FORMAT_R16G16_SFLOAT,          // MotionSurface
        VK_FORMAT_R8_UNORM,               // OcclusionSurface
        VK_FORMAT_R8G8B8A8_UNORM,         // DirectOcclusionSurface — DirectOcclusionCapacity illuminants
        VK_FORMAT_R32G32_UINT,            // TransmissionIndex — TransmissionDepth layers of it
        VK_FORMAT_R16G16B16A16_SFLOAT,    // RadianceSurface
        VK_FORMAT_R16G16B16A16_SFLOAT,    // ReflectionSurface
        VK_FORMAT_R16G16B16A16_SFLOAT,    // AccumulationSurface
        VK_FORMAT_UNDEFINED,              // DisplaySurface — the presentation surface's own, passed in
        VK_FORMAT_R8_UNORM,               // OutlineSurface
        VK_FORMAT_R16G16B16A16_SFLOAT,    // TransmittanceSurface
        VK_FORMAT_R16G16B16A16_SFLOAT,    // MultiScatterSurface
        VK_FORMAT_R16G16B16A16_SFLOAT     // SkyViewSurface
    };

    // 📝 Intent per target. `08` §2's producer column decides it: what writes a target is what its usage must
    //    admit, and the depth attachment is the one target the vendor spells differently from every other.
    constexpr ImageIntent IntentOf[TargetSpan] =
    {
        ImageIntent::DepthTarget,       // DepthSurface
        ImageIntent::ColourTarget,      // VisibilityIndex — the hardware raster writes it as an attachment
        ImageIntent::ColourTarget,      // OccupancySurface
        ImageIntent::ColourTarget,      // MotionSurface
        ImageIntent::ComputeWritable,   // OcclusionSurface
        ImageIntent::ComputeWritable,   // DirectOcclusionSurface
        ImageIntent::ComputeWritable,   // TransmissionIndex
        ImageIntent::ComputeWritable,   // RadianceSurface
        ImageIntent::ComputeWritable,   // ReflectionSurface
        ImageIntent::ComputeWritable,   // AccumulationSurface
        ImageIntent::ColourTarget,      // DisplaySurface
        ImageIntent::ComputeWritable,   // OutlineSurface
        ImageIntent::ComputeWritable,   // TransmittanceSurface
        ImageIntent::ComputeWritable,   // MultiScatterSurface
        ImageIntent::ComputeWritable    // SkyViewSurface
    };

    // 📝 The absolute extents come from `Foundation/NumericTolerance.h` because `28` reads them too — one set
    //    of numbers, two units, which is where `00` §2 places them. A zero here means the target is not
    //    absolute and its extent is derived from the display instead.
    constexpr std::uint32_t AbsoluteX[TargetSpan] =
    {
        0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
        TransmittanceExtentX,
        MultiScatterExtentX,
        SkyViewExtentX
    };

    constexpr std::uint32_t AbsoluteY[TargetSpan] =
    {
        0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
        TransmittanceExtentY,
        MultiScatterExtentY,
        SkyViewExtentY
    };

    // 📝 🔴 `TransmissionIndex` is `TransmissionDepth` sorted pairs per pixel and is claimed as that many
    //    array layers rather than as that many separate targets. One target, one claim, one view — which is
    //    what makes the depth a constant the shader reads rather than a count of descriptor slots.
    constexpr std::uint32_t LayersOf[TargetSpan] =
    {
        1u, 1u, 1u, 1u, 1u, 1u,
        TransmissionDepth,
        1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u
    };
}

ExtentRelation RelationOfTarget(SharedTarget Target)
{
    const std::size_t TargetIndex = static_cast<std::size_t>(Target);

    // 📝 A target outside the closed enumeration reads as absolute, which is the relation that touches
    //    nothing on a resize. The refusal for such a target is raised where it is claimed, not here.
    return TargetIndex < TargetSpan ? RelationOf[TargetIndex] : ExtentRelation::Absolute;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE SHAPES
//------------------------------------------------------------------------------------------------------------------------

Deliver<ImageShape> TargetSpace::ShapeOf(SharedTarget Target) const
{
    const std::size_t TargetIndex = static_cast<std::size_t>(Target);

    if (TargetIndex >= TargetSpan)
        return Deliver<ImageShape>::Refuse({ RefusalReason::ContentUnsupported, "no such shared target" });

    ImageShape Declared;
    Declared.Format     = TargetIndex == static_cast<std::size_t>(SharedTarget::DisplaySurface)
                            ? DisplayCarries
                            : FormatOf[TargetIndex];
    Declared.Intent     = IntentOf[TargetIndex];
    Declared.LayerCount = LayersOf[TargetIndex];
    Declared.LevelCount = 1u;

    switch (RelationOf[TargetIndex])
    {
        case ExtentRelation::DisplayRelative:
        {
            Declared.Width  = CurrentWidth;
            Declared.Height = CurrentHeight;
            break;
        }

        case ExtentRelation::FractionOfDisplay:
        {
            // 📝 Half per edge, rounded up. Rounding down leaves the last column of the display with no
            //    coarse texel over it, and `60` then samples outside its own target along one edge.
            Declared.Width  = (CurrentWidth  + 1u) / 2u;
            Declared.Height = (CurrentHeight + 1u) / 2u;
            break;
        }

        case ExtentRelation::Absolute:
        default:
        {
            Declared.Width  = AbsoluteX[TargetIndex];
            Declared.Height = AbsoluteY[TargetIndex];
            break;
        }
    }

    if (Declared.Width == 0u || Declared.Height == 0u)
        return Deliver<ImageShape>::Refuse({ RefusalReason::ContentUnsupported, "the target resolves to a zero extent" });

    if (Declared.Width > DisplayExtentLimit || Declared.Height > DisplayExtentLimit)
    {
        return Deliver<ImageShape>::Refuse(
            { RefusalReason::ContentUnsupported, "the target resolves above the declared display extent ceiling" });
    }

    if (Declared.Format == VK_FORMAT_UNDEFINED)
        return Deliver<ImageShape>::Refuse({ RefusalReason::ContentUnsupported, "the target resolves to no format" });

    return Deliver<ImageShape>::Result(Declared);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE CLAIM
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> TargetSpace::Reserve(ImageSpace&    Images,
                                 std::uint32_t  DisplayWidth,
                                 std::uint32_t  DisplayHeight,
                                 VkFormat       DisplayFormat)
{
    if (DisplayWidth == 0u || DisplayHeight == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a display extent of zero" });

    if (DisplayWidth > DisplayExtentLimit || DisplayHeight > DisplayExtentLimit)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a display extent above the declared ceiling" });
    }

    if (DisplayFormat == VK_FORMAT_UNDEFINED)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the display surface declares no format" });

    // 📝 A second claim over a standing one surrenders first rather than claiming twice. The alternative
    //    leaks fifteen images per call and reports as memory growth attributable to nothing in particular.
    if (ImageEdge != nullptr)
        Release();

    ImageEdge      = &Images;
    CurrentWidth  = DisplayWidth;
    CurrentHeight = DisplayHeight;
    DisplayCarries = DisplayFormat;

    for (std::size_t TargetIndex = 0u; TargetIndex < TargetSpan; ++TargetIndex)
    {
        const Deliver<ImageShape> Declared = ShapeOf(static_cast<SharedTarget>(TargetIndex));

        if (!Declared.Resolved)
        {
            Release();
            return Deliver<bool>::Refuse(Declared.Error);
        }

        const Deliver<ImageReservation> Reserved = Images.Reserve(Declared.Resolve());

        // 🔴 Rejected in full. Every target claimed so far is surrendered, so the caller is left with nothing
        //    rather than with a set that is complete up to whichever target the device rejected.
        if (!Reserved.Resolved)
        {
            Release();
            return Deliver<bool>::Refuse(Reserved.Error);
        }

        ReservedFor[TargetIndex]    = Reserved.Resolve().ImageIndex;
        TargetReserved[TargetIndex] = true;
    }

    return Deliver<bool>::Result(true);
}

Deliver<bool> TargetSpace::Reclaim(std::uint32_t DisplayWidth, std::uint32_t DisplayHeight)
{
    if (ImageEdge == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no target set stands to be reclaimed" });

    if (DisplayWidth == 0u || DisplayHeight == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a display extent of zero" });

    if (DisplayWidth > DisplayExtentLimit || DisplayHeight > DisplayExtentLimit)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a display extent above the declared ceiling" });

    CurrentWidth  = DisplayWidth;
    CurrentHeight = DisplayHeight;

    // 🔴 `06` §7: **every** display-relative and fraction-of-display target, and no absolute one. The two
    //    passes are separate — every affected target is released before any is re-claimed — so that the peak
    //    residency of a resize is one target set and not two.
    for (std::size_t TargetIndex = 0u; TargetIndex < TargetSpan; ++TargetIndex)
    {
        if (RelationOf[TargetIndex] == ExtentRelation::Absolute || !TargetReserved[TargetIndex])
            continue;

        ImageEdge->Release(ReservedFor[TargetIndex]);

        ReservedFor[TargetIndex]    = AbsentImage;
        TargetReserved[TargetIndex] = false;
    }

    for (std::size_t TargetIndex = 0u; TargetIndex < TargetSpan; ++TargetIndex)
    {
        if (RelationOf[TargetIndex] == ExtentRelation::Absolute)
            continue;

        const Deliver<ImageShape> Declared = ShapeOf(static_cast<SharedTarget>(TargetIndex));

        if (!Declared.Resolved)
        {
            Release();
            return Deliver<bool>::Refuse(Declared.Error);
        }

        const Deliver<ImageReservation> Reserved = ImageEdge->Reserve(Declared.Resolve());

        if (!Reserved.Resolved)
        {
            Release();
            return Deliver<bool>::Refuse(Reserved.Error);
        }

        ReservedFor[TargetIndex]    = Reserved.Resolve().ImageIndex;
        TargetReserved[TargetIndex] = true;
    }

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   WHAT IS CLAIMED
//------------------------------------------------------------------------------------------------------------------------

Deliver<ImageReservation> TargetSpace::Resolve(SharedTarget Target) const
{
    const Deliver<std::uint32_t> Index = IndexOf(Target);

    if (!Index.Resolved)
        return Deliver<ImageReservation>::Refuse(Index.Error);

    return ImageEdge->Current(Index.Resolve());
}

Deliver<std::uint32_t> TargetSpace::IndexOf(SharedTarget Target) const
{
    const std::size_t TargetIndex = static_cast<std::size_t>(Target);

    if (TargetIndex >= TargetSpan)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "no such shared target" });

    if (ImageEdge == nullptr || !TargetReserved[TargetIndex])
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "the target is not claimed" });

    return Deliver<std::uint32_t>::Result(ReservedFor[TargetIndex]);
}

void TargetSpace::Release()
{
    if (ImageEdge != nullptr)
    {
        for (std::size_t TargetIndex = 0u; TargetIndex < TargetSpan; ++TargetIndex)
        {
            if (TargetReserved[TargetIndex])
                ImageEdge->Release(ReservedFor[TargetIndex]);
        }
    }

    for (std::size_t TargetIndex = 0u; TargetIndex < TargetSpan; ++TargetIndex)
    {
        ReservedFor[TargetIndex]    = AbsentImage;
        TargetReserved[TargetIndex] = false;
    }

    // 📝 🔴 `06` §7: no persistent extent is carried across a change. The standing extent is forgotten with
    //    the images, so a re-claim that refuses cannot leave a later reader deriving a shape from the extent
    //    the surrendered set was claimed at.
    ImageEdge      = nullptr;
    CurrentWidth  = 0u;
    CurrentHeight = 0u;
    DisplayCarries = VK_FORMAT_UNDEFINED;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONTRIBUTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> RenderSchedule::Contribute(const DeclaredRecording& Incoming)
{
    if (OrderingFixed)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the ordering is already fixed" });

    // 📝 🔴 A capability requirement with no substitution is rejected here rather than discovered at the
    //    recording site. The substitution is a design decision belonging to the contributing document.
    if (Incoming.CapabilityRequired && (Incoming.Substitution == nullptr || Incoming.Substitution[0] == '\0'))
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::CapabilityAbsent, "a capability is required with no declared substitution" });
    }

    for (const SharedTarget Produced : Incoming.Produces)
    {
        const std::size_t TargetIndex = static_cast<std::size_t>(Produced);

        if (TargetIndex >= TargetSpan)
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such shared target" });

        // 📝 One producing recording per target. An amender declares itself in Amends and takes its place
        //    in the ordered amendment list instead — `08` §2's Amended by column.
        if (ProducerOf[TargetIndex].IdentityDeclared())
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::HostDenied, "the target already declares a producing recording" });
        }

        ProducerOf[TargetIndex].SlotIndex    = static_cast<std::uint32_t>(ContributedOrder.size());
        ProducerOf[TargetIndex].SlotGeneration = 1u;
    }

    ContributedOrder.push_back(Incoming);
    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       ORDERING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> RenderSchedule::Fix()
{
    if (OrderingFixed)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the ordering is already fixed" });

    OrderedRecordings.clear();
    OrderedRecordings.reserve(ContributedOrder.size());

    std::vector<bool> Placed(ContributedOrder.size(), false);
    std::vector<bool> Available(TargetSpan, false);

    // 📝 The order is derived rather than authored: a recording is placed once every target it reads is
    //    either produced already or produced by nothing at all. Scene-referred recordings are exhausted
    //    before any display-referred one is placed, which is the tone line in `08` §3.1.
    for (int DisplayPhase = 0; DisplayPhase < 2; ++DisplayPhase)
    {
        const bool PlacingDisplayReferred = DisplayPhase == 1;
        bool       Advanced               = true;

        while (Advanced)
        {
            Advanced = false;

            // 📝 The placeable candidate with the least amendment ordinal is taken, rather than the first one
            //    found. Every recording that declares no ordinal carries nought, so a schedule of recordings
            //    that predate the field is placed in exactly the order it was before.
            std::size_t   Preferred        = ContributedOrder.size();
            std::uint32_t PreferredIndex = 0u;

            for (std::size_t Index = 0u; Index < ContributedOrder.size(); ++Index)
            {
                if (Placed[Index])
                    continue;

                const DeclaredRecording& Candidate = ContributedOrder[Index];

                if (Candidate.DisplayReferred != PlacingDisplayReferred)
                    continue;

                bool ReadsSatisfied = true;

                for (const SharedTarget Consumed : Candidate.Reads)
                {
                    const std::size_t TargetIndex = static_cast<std::size_t>(Consumed);

                    if (ProducerOf[TargetIndex].IdentityDeclared() && !Available[TargetIndex])
                    {
                        ReadsSatisfied = false;
                        break;
                    }
                }

                if (!ReadsSatisfied)
                    continue;

                if (Preferred == ContributedOrder.size() || Candidate.AmendmentIndex < PreferredIndex)
                {
                    Preferred        = Index;
                    PreferredIndex = Candidate.AmendmentIndex;
                }
            }

            if (Preferred == ContributedOrder.size())
                continue;

            const DeclaredRecording& Placing = ContributedOrder[Preferred];

            OrderedRecordings.push_back(Placing);
            Placed[Preferred] = true;
            Advanced          = true;

            for (const SharedTarget Produced : Placing.Produces)
                Available[static_cast<std::size_t>(Produced)] = true;
        }
    }

    if (OrderedRecordings.size() != ContributedOrder.size())
    {
        // 📝 A recording that never became placeable reads a target whose producer reads it back. The
        //    orderer reports it here rather than emitting an ordering that silently drops the recording.
        OrderedRecordings.clear();
        return Deliver<bool>::Refuse(
            { RefusalReason::HostDenied, "a recording reads a target no ordering makes available" });
    }

    OrderingFixed = true;
    return Deliver<bool>::Result(true);
}

const std::vector<DeclaredRecording>& RenderSchedule::Ordered() const
{
    return OrderedRecordings;
}

}   // namespace Slate
