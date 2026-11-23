//============================================================================================================================================
//                                                            SCENESTRUCTURE.CPP
//============================================================================================================================================
// 🧩 Enclosure ordering, gapped label assignment and repair, and downward attachment compounding.

#include "SlateDocument/Document/SceneStructure/Api/SceneStructure.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

std::uint32_t SceneStructure::Resolved(OwnerIdentity Subject) const
{
    if (!Subject.IdentityDeclared())
        return AbsentSlot;

    if (Subject.SlotIndex >= SlotGenerations.size())
        return AbsentSlot;

    if (SlotGenerations[Subject.SlotIndex] != Subject.SlotGeneration)
        return AbsentSlot;

    return Subject.SlotIndex;
}

OwnerIdentity SceneStructure::OwnerAt(std::uint32_t SlotIndex) const
{
    OwnerIdentity Occupying;

    if (SlotIndex >= SlotGenerations.size())
        return Occupying;

    Occupying.SlotIndex    = SlotIndex;
    Occupying.SlotGeneration = SlotGenerations[SlotIndex];

    return Occupying;
}

std::uint32_t SceneStructure::SpannedCount() const
{
    return static_cast<std::uint32_t>(SlotGenerations.size());
}

std::uint32_t SceneStructure::RootFirst() const
{
    return RootFirstSlot;
}

std::uint32_t SceneStructure::NextInOrder(std::uint32_t SlotIndex) const
{
    if (SlotIndex >= Enclosures.size())
        return AbsentSlot;

    return Enclosures[SlotIndex].NextInOrder;
}

std::uint32_t SceneStructure::FirstEnclosed(std::uint32_t SlotIndex) const
{
    if (SlotIndex >= Enclosures.size())
        return AbsentSlot;

    return Enclosures[SlotIndex].FirstEnclosed;
}

std::uint32_t SceneStructure::EnclosureDepth(std::uint32_t SlotIndex) const
{
    if (SlotIndex >= Enclosures.size())
        return 0u;

    return Enclosures[SlotIndex].EnclosureDepth;
}

bool SceneStructure::RelabelOwed() const
{
    return !ExhaustedEnclosures.empty();
}

//------------------------------------------------------------------------------------------------------------------------
//                                               ADMISSION AND RETIREMENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> SceneStructure::Accept(OwnerIdentity Incoming)
{
    if (!Incoming.IdentityDeclared())
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "an undeclared identity names no owner" });

    const std::size_t Required = static_cast<std::size_t>(Incoming.SlotIndex) + 1u;

    if (Required > SlotGenerations.size())
    {
        SlotGenerations.resize(Required, 0u);
        Enclosures.resize(Required);
        Attachments.resize(Required);
        AuthoredTransforms.resize(Required);
        CompoundedTransforms.resize(Required);
    }

    const std::uint32_t SlotIndex = Incoming.SlotIndex;

    // 📝 The slot is reset rather than merged into. A slot reused after a withdrawal carries the previous
    //    owner's ordering links, and inheriting them would enclose the arrival where the departed sat.
    SlotGenerations[SlotIndex]      = Incoming.SlotGeneration;
    Enclosures[SlotIndex]           = EnclosureRecord{};
    Attachments[SlotIndex]          = AttachmentRecord{};
    AuthoredTransforms[SlotIndex]   = DecomposedTransform{};
    CompoundedTransforms[SlotIndex] = DecomposedTransform{};

    Link(SlotIndex, AbsentSlot, RootCount);
    ++AcceptedCount;

    return Deliver<bool>::Result(true);
}

Deliver<bool> SceneStructure::Retire(OwnerIdentity Departing)
{
    const std::uint32_t SlotIndex = Resolved(Departing);

    if (SlotIndex == AbsentSlot)
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "the identity no longer resolves here" });

    // 🔴 `12` §12: enclosed owners are re-enclosed by the departing owner's enclosure, not retired with
    //    it. Deleting a group deletes the group, not the work inside it. Each is placed immediately after the
    //    departing owner so the ordering the artist saw survives the retirement.
    const OwnerIdentity RisingEnclosure = Enclosures[SlotIndex].EnclosingOwner;
    const std::uint32_t    RisingSlot      = Resolved(RisingEnclosure);

    std::uint32_t Insertion = 0u;

    for (std::uint32_t Walking = RisingSlot == AbsentSlot ? RootFirstSlot : Enclosures[RisingSlot].FirstEnclosed;
         Walking != AbsentSlot && Walking != SlotIndex;
         Walking = Enclosures[Walking].NextInOrder)
    {
        ++Insertion;
    }

    std::uint32_t Enclosed = Enclosures[SlotIndex].FirstEnclosed;

    while (Enclosed != AbsentSlot)
    {
        const std::uint32_t Following = Enclosures[Enclosed].NextInOrder;

        Unlink(Enclosed);
        Link(Enclosed, RisingSlot, ++Insertion);

        if (Enclosures[Enclosed].EnclosedCount != 0u || !LabelBetween(Enclosed))
            DeclareExhausted(RisingSlot);

        Enclosed = Following;
    }

    // 📝 Attached owners retain the transform they were compounded to. Compounding the departing
    //    owner's authored transform into each of theirs and reattaching to its attachment reaches the same
    //    compounded result without ever inverting a transform, because compounding is associative.
    const OwnerIdentity RisingAttachment = Attachments[SlotIndex].AttachmentOwner;

    std::uint32_t Attached = Attachments[SlotIndex].FirstAttached;

    while (Attached != AbsentSlot)
    {
        const std::uint32_t Following = Attachments[Attached].NextAttached;

        AuthoredTransforms[Attached] = Compound(AuthoredTransforms[SlotIndex], AuthoredTransforms[Attached]);

        Attachments[Attached].AttachmentOwner = RisingAttachment;
        Attachments[Attached].NextAttached       = AbsentSlot;

        const std::uint32_t RisingAttachmentSlot = Resolved(RisingAttachment);

        if (RisingAttachmentSlot != AbsentSlot)
        {
            Attachments[Attached].NextAttached                     = Attachments[RisingAttachmentSlot].FirstAttached;
            Attachments[RisingAttachmentSlot].FirstAttached        = Attached;
        }

        Attached = Following;
    }

    const std::uint32_t DepartingAttachment = Resolved(RisingAttachment);

    if (DepartingAttachment != AbsentSlot)
    {
        std::uint32_t* Linking = &Attachments[DepartingAttachment].FirstAttached;

        while (*Linking != AbsentSlot && *Linking != SlotIndex)
            Linking = &Attachments[*Linking].NextAttached;

        if (*Linking == SlotIndex)
            *Linking = Attachments[SlotIndex].NextAttached;
    }

    Unlink(SlotIndex);

    SlotGenerations[SlotIndex] = 0u;
    Enclosures[SlotIndex]      = EnclosureRecord{};
    Attachments[SlotIndex]     = AttachmentRecord{};
    --AcceptedCount;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  ENCLOSURE ORDERING
//------------------------------------------------------------------------------------------------------------------------

void SceneStructure::Unlink(std::uint32_t SlotIndex)
{
    EnclosureRecord& Departing = Enclosures[SlotIndex];

    const std::uint32_t EnclosureSlot = Resolved(Departing.EnclosingOwner);

    std::uint32_t* HeadLink = EnclosureSlot == AbsentSlot ? &RootFirstSlot : &Enclosures[EnclosureSlot].FirstEnclosed;
    std::uint32_t* TailLink = EnclosureSlot == AbsentSlot ? &RootLastSlot  : &Enclosures[EnclosureSlot].LastEnclosed;
    std::uint32_t* Counting = EnclosureSlot == AbsentSlot ? &RootCount     : &Enclosures[EnclosureSlot].EnclosedCount;

    if (Departing.PriorInOrder != AbsentSlot)
        Enclosures[Departing.PriorInOrder].NextInOrder = Departing.NextInOrder;
    else
        *HeadLink = Departing.NextInOrder;

    if (Departing.NextInOrder != AbsentSlot)
        Enclosures[Departing.NextInOrder].PriorInOrder = Departing.PriorInOrder;
    else
        *TailLink = Departing.PriorInOrder;

    if (*Counting != 0u)
        --*Counting;

    Departing.PriorInOrder      = AbsentSlot;
    Departing.NextInOrder       = AbsentSlot;
    Departing.EnclosingOwner = OwnerIdentity{};
}

void SceneStructure::Link(std::uint32_t SlotIndex, std::uint32_t EnclosureSlot, std::uint32_t OrderWithinEnclosure)
{
    EnclosureRecord& Incoming = Enclosures[SlotIndex];

    std::uint32_t* HeadLink = EnclosureSlot == AbsentSlot ? &RootFirstSlot : &Enclosures[EnclosureSlot].FirstEnclosed;
    std::uint32_t* TailLink = EnclosureSlot == AbsentSlot ? &RootLastSlot  : &Enclosures[EnclosureSlot].LastEnclosed;
    std::uint32_t* Counting = EnclosureSlot == AbsentSlot ? &RootCount     : &Enclosures[EnclosureSlot].EnclosedCount;

    std::uint32_t Preceding = AbsentSlot;
    std::uint32_t Walking   = *HeadLink;

    for (std::uint32_t Passed = 0u; Passed < OrderWithinEnclosure && Walking != AbsentSlot; ++Passed)
    {
        Preceding = Walking;
        Walking   = Enclosures[Walking].NextInOrder;
    }

    Incoming.PriorInOrder      = Preceding;
    Incoming.NextInOrder       = Walking;
    Incoming.EnclosingOwner = EnclosureSlot == AbsentSlot ? OwnerIdentity{} : OwnerAt(EnclosureSlot);
    Incoming.EnclosureDepth    = EnclosureSlot == AbsentSlot ? 0u : Enclosures[EnclosureSlot].EnclosureDepth + 1u;

    if (Preceding != AbsentSlot)
        Enclosures[Preceding].NextInOrder = SlotIndex;
    else
        *HeadLink = SlotIndex;

    if (Walking != AbsentSlot)
        Enclosures[Walking].PriorInOrder = SlotIndex;
    else
        *TailLink = SlotIndex;

    ++*Counting;
}

Deliver<bool> SceneStructure::Enclose(OwnerIdentity Subject,
                                      OwnerIdentity ProposedEnclosure,
                                      std::uint32_t    OrderWithinEnclosure)
{
    const std::uint32_t SlotIndex = Resolved(Subject);

    if (SlotIndex == AbsentSlot)
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "the enclosed owner no longer resolves" });

    std::uint32_t EnclosureSlot = AbsentSlot;

    if (ProposedEnclosure.IdentityDeclared())
    {
        EnclosureSlot = Resolved(ProposedEnclosure);

        if (EnclosureSlot == AbsentSlot)
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::IdentityStale, "the enclosing owner no longer resolves" });
        }

        // 🔴 Rejected at commit and never applied. `12` §9 requires the refusal to name both owners; both
        //    are the caller's own arguments, so it names them without this seam allocating a message.
        if (EnclosureCyclic(Subject, ProposedEnclosure))
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::RelationCyclic, "the owner already encloses its proposed enclosure" });
        }

        if (Enclosures[EnclosureSlot].EnclosureDepth + 1u >= EnclosureDepthLimit)
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::ExtentExhausted, "the enclosure reached the declared depth ceiling" });
        }
    }

    Unlink(SlotIndex);
    Link(SlotIndex, EnclosureSlot, OrderWithinEnclosure);

    // 📝 A leaf takes a label out of the gap between its neighbours and costs nothing further. An owner
    //    that encloses others carries a whole span with it, so its interior is relabelled at ④ instead.
    if (Enclosures[SlotIndex].EnclosedCount != 0u || !LabelBetween(SlotIndex))
        DeclareExhausted(EnclosureSlot);

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   LABEL ASSIGNMENT
//------------------------------------------------------------------------------------------------------------------------

IntervalLabel SceneStructure::EnclosureInterval(std::uint32_t EnclosureSlot) const
{
    IntervalLabel Interior;

    if (EnclosureSlot == AbsentSlot)
    {
        Interior.LabelBegin = 1u;
        Interior.LabelEnd   = RootLabelLimit - 1u;
        return Interior;
    }

    Interior.LabelBegin = Enclosures[EnclosureSlot].Label.LabelBegin + 1u;
    Interior.LabelEnd   = Enclosures[EnclosureSlot].Label.LabelEnd   - 1u;

    return Interior;
}

bool SceneStructure::LabelBetween(std::uint32_t SlotIndex)
{
    const EnclosureRecord& Placed        = Enclosures[SlotIndex];
    const std::uint32_t    EnclosureSlot = Resolved(Placed.EnclosingOwner);

    if (EnclosureSlot != AbsentSlot && Enclosures[EnclosureSlot].Label.LabelEnd == 0u)
        return false;

    const IntervalLabel Interior = EnclosureInterval(EnclosureSlot);

    std::uint64_t LowerBound = Interior.LabelBegin;
    std::uint64_t UpperBound = Interior.LabelEnd;

    if (Placed.PriorInOrder != AbsentSlot)
    {
        if (Enclosures[Placed.PriorInOrder].Label.LabelEnd == 0u)
            return false;

        LowerBound = Enclosures[Placed.PriorInOrder].Label.LabelEnd + 1u;
    }

    if (Placed.NextInOrder != AbsentSlot)
    {
        if (Enclosures[Placed.NextInOrder].Label.LabelBegin == 0u)
            return false;

        UpperBound = Enclosures[Placed.NextInOrder].Label.LabelBegin - 1u;
    }

    if (UpperBound < LowerBound || UpperBound - LowerBound < 1u)
        return false;

    // 📐 The free run is halved and centred rather than consumed whole, so that an insertion on either side of
    //    this owner still finds a gap. Taking the whole run makes the next insertion a relabel.
    const std::uint64_t FreeSpan = UpperBound - LowerBound + 1u;
    std::uint64_t       TakenSpan = FreeSpan / 2u;

    if (TakenSpan < 2u)
        TakenSpan = FreeSpan;

    Enclosures[SlotIndex].Label.LabelBegin = LowerBound + (FreeSpan - TakenSpan) / 2u;
    Enclosures[SlotIndex].Label.LabelEnd   = Enclosures[SlotIndex].Label.LabelBegin + TakenSpan - 1u;

    return true;
}

void SceneStructure::DeclareExhausted(std::uint32_t EnclosureSlot)
{
    for (const std::uint32_t Declared : ExhaustedEnclosures)
    {
        if (Declared == EnclosureSlot)
            return;
    }

    ExhaustedEnclosures.push_back(EnclosureSlot);
}

Deliver<bool> SceneStructure::AssignLabels(std::uint32_t EnclosureSlot, IntervalLabel Available, std::uint32_t Depth)
{
    if (Depth >= EnclosureDepthLimit)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the enclosure exceeded the depth ceiling" });

    const std::uint32_t Population = EnclosureSlot == AbsentSlot ? RootCount
                                                                 : Enclosures[EnclosureSlot].EnclosedCount;

    if (Population == 0u)
        return Deliver<bool>::Result(true);

    if (Available.LabelEnd < Available.LabelBegin)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the span holds no ordinal to divide" });

    const std::uint64_t SpanWidth = Available.LabelEnd - Available.LabelBegin + 1u;
    const std::uint64_t EachWidth = SpanWidth / Population;

    if (EachWidth < 2u)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the span cannot hold its ordering" });

    std::uint32_t Walking  = EnclosureSlot == AbsentSlot ? RootFirstSlot : Enclosures[EnclosureSlot].FirstEnclosed;
    std::uint64_t Issuing  = Available.LabelBegin;

    while (Walking != AbsentSlot)
    {
        Enclosures[Walking].Label.LabelBegin = Issuing;
        Enclosures[Walking].Label.LabelEnd   = Issuing + EachWidth - 1u;
        Enclosures[Walking].EnclosureDepth   = Depth;

        if (Enclosures[Walking].EnclosedCount != 0u)
        {
            IntervalLabel Interior;
            Interior.LabelBegin = Enclosures[Walking].Label.LabelBegin + 1u;
            Interior.LabelEnd   = Enclosures[Walking].Label.LabelEnd   - 1u;

            const Deliver<bool> Nested = AssignLabels(Walking, Interior, Depth + 1u);

            if (!Nested.Resolved)
                return Nested;
        }

        Issuing += EachWidth;
        Walking  = Enclosures[Walking].NextInOrder;
    }

    return Deliver<bool>::Result(true);
}

Deliver<bool> SceneStructure::RepairLabels()
{
    // 📝 Repair covers the exhausted span and escalates outward only while the span above it also refuses. A
    //    whole-population relabel is the last resort here rather than the first, which is the entire reason
    //    labels are registered with gaps.
    while (!ExhaustedEnclosures.empty())
    {
        std::uint32_t EnclosureSlot = ExhaustedEnclosures.back();
        ExhaustedEnclosures.pop_back();

        for (;;)
        {
            const std::uint32_t Depth = EnclosureSlot == AbsentSlot
                                      ? 0u
                                      : Enclosures[EnclosureSlot].EnclosureDepth + 1u;

            const Deliver<bool> Assigned = AssignLabels(EnclosureSlot, EnclosureInterval(EnclosureSlot), Depth);

            if (Assigned.Resolved)
                break;

            if (EnclosureSlot == AbsentSlot)
            {
                return Deliver<bool>::Refuse(
                    { RefusalReason::ExtentExhausted, "the root span cannot hold the enclosure ordering" });
            }

            EnclosureSlot = Resolved(Enclosures[EnclosureSlot].EnclosingOwner);
        }
    }

    return Deliver<bool>::Result(true);
}

Deliver<IntervalLabel> SceneStructure::Label(OwnerIdentity Subject) const
{
    const std::uint32_t SlotIndex = Resolved(Subject);

    if (SlotIndex == AbsentSlot)
    {
        return Deliver<IntervalLabel>::Refuse(
            { RefusalReason::IdentityStale, "the identity no longer resolves here" });
    }

    return Deliver<IntervalLabel>::Result(Enclosures[SlotIndex].Label);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                ATTACHMENT COMPOUNDING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> SceneStructure::Attach(OwnerIdentity Subject, OwnerIdentity ProposedAttachment)
{
    const std::uint32_t SlotIndex = Resolved(Subject);

    if (SlotIndex == AbsentSlot)
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "the following owner no longer resolves" });

    std::uint32_t AttachmentSlot = AbsentSlot;

    if (ProposedAttachment.IdentityDeclared())
    {
        AttachmentSlot = Resolved(ProposedAttachment);

        if (AttachmentSlot == AbsentSlot)
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::IdentityStale, "the proposed attachment no longer resolves" });
        }

        if (AttachmentCyclic(Subject, ProposedAttachment))
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::RelationCyclic, "the owner is already followed by its proposed attachment" });
        }
    }

    const std::uint32_t PriorAttachment = Resolved(Attachments[SlotIndex].AttachmentOwner);

    if (PriorAttachment != AbsentSlot)
    {
        std::uint32_t* Linking = &Attachments[PriorAttachment].FirstAttached;

        while (*Linking != AbsentSlot && *Linking != SlotIndex)
            Linking = &Attachments[*Linking].NextAttached;

        if (*Linking == SlotIndex)
            *Linking = Attachments[SlotIndex].NextAttached;
    }

    Attachments[SlotIndex].AttachmentOwner = AttachmentSlot == AbsentSlot ? OwnerIdentity{}
                                                                              : OwnerAt(AttachmentSlot);
    Attachments[SlotIndex].NextAttached       = AbsentSlot;

    if (AttachmentSlot != AbsentSlot)
    {
        Attachments[SlotIndex].NextAttached      = Attachments[AttachmentSlot].FirstAttached;
        Attachments[AttachmentSlot].FirstAttached  = SlotIndex;
    }

    return Deliver<bool>::Result(true);
}

Deliver<bool> SceneStructure::AuthorTransform(OwnerIdentity Subject, const DecomposedTransform& Authored)
{
    const std::uint32_t SlotIndex = Resolved(Subject);

    if (SlotIndex == AbsentSlot)
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "the identity no longer resolves here" });

    AuthoredTransforms[SlotIndex] = Authored;

    return Deliver<bool>::Result(true);
}

Deliver<bool> SceneStructure::CompoundFrom(std::uint32_t SlotIndex, std::uint32_t Depth)
{
    if (Depth >= EnclosureDepthLimit)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the attachment chain exceeded the ceiling" });

    for (std::uint32_t Following = Attachments[SlotIndex].FirstAttached;
         Following != AbsentSlot;
         Following = Attachments[Following].NextAttached)
    {
        // 📐 The attachment is the outer transform and the owner's own is the inner one, so the compounded
        //    result depends only on the path back to the attachment root — invariant 7, stated as arithmetic.
        CompoundedTransforms[Following]      = Compound(CompoundedTransforms[SlotIndex],
                                                       AuthoredTransforms[Following]);
        Attachments[Following].AttachmentDepth = Depth + 1u;

        const Deliver<bool> Deeper = CompoundFrom(Following, Depth + 1u);

        if (!Deeper.Resolved)
            return Deeper;
    }

    return Deliver<bool>::Result(true);
}

Deliver<bool> SceneStructure::CompoundAttachments()
{
    for (std::uint32_t SlotIndex = 0u; SlotIndex < SlotGenerations.size(); ++SlotIndex)
    {
        if (SlotGenerations[SlotIndex] == 0u)
            continue;

        if (Attachments[SlotIndex].AttachmentOwner.IdentityDeclared())
            continue;

        // 📝 An attachment root compounds to its authored transform unchanged. `12` §1: enclosure composes no
        //    transform, so an owner indented under another and attached to nothing moves alone.
        CompoundedTransforms[SlotIndex]        = AuthoredTransforms[SlotIndex];
        Attachments[SlotIndex].AttachmentDepth = 0u;

        const Deliver<bool> Compounded = CompoundFrom(SlotIndex, 0u);

        if (!Compounded.Resolved)
            return Compounded;
    }

    return Deliver<bool>::Result(true);
}

Deliver<DecomposedTransform> SceneStructure::CompoundedTransform(OwnerIdentity Subject) const
{
    const std::uint32_t SlotIndex = Resolved(Subject);

    if (SlotIndex == AbsentSlot)
    {
        return Deliver<DecomposedTransform>::Refuse(
            { RefusalReason::IdentityStale, "the identity no longer resolves here" });
    }

    return Deliver<DecomposedTransform>::Result(CompoundedTransforms[SlotIndex]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CYCLE REJECTION
//------------------------------------------------------------------------------------------------------------------------

bool SceneStructure::EnclosureCyclic(OwnerIdentity Subject, OwnerIdentity ProposedEnclosure) const
{
    const std::uint32_t SlotIndex = Resolved(Subject);

    if (SlotIndex == AbsentSlot)
        return false;

    // 📝 Walked outward rather than compared against labels. A change incoming before ④ would otherwise be
    //    gated against labels ④ has not yet repaired, and the gate would pass a cycle through.
    std::uint32_t Walking = Resolved(ProposedEnclosure);

    for (std::uint32_t Passed = 0u; Walking != AbsentSlot && Passed <= EnclosureDepthLimit; ++Passed)
    {
        if (Walking == SlotIndex)
            return true;

        Walking = Resolved(Enclosures[Walking].EnclosingOwner);
    }

    return false;
}

bool SceneStructure::AttachmentCyclic(OwnerIdentity Subject, OwnerIdentity ProposedAttachment) const
{
    const std::uint32_t SlotIndex = Resolved(Subject);

    if (SlotIndex == AbsentSlot)
        return false;

    std::uint32_t Walking = Resolved(ProposedAttachment);

    for (std::uint32_t Passed = 0u; Walking != AbsentSlot && Passed <= EnclosureDepthLimit; ++Passed)
    {
        if (Walking == SlotIndex)
            return true;

        Walking = Resolved(Attachments[Walking].AttachmentOwner);
    }

    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  INVARIANTS 3 AND 4
//------------------------------------------------------------------------------------------------------------------------

bool SceneStructure::RelationsAcyclic() const
{
    for (std::uint32_t SlotIndex = 0u; SlotIndex < SlotGenerations.size(); ++SlotIndex)
    {
        if (SlotGenerations[SlotIndex] == 0u)
            continue;

        std::uint32_t Walking = Resolved(Enclosures[SlotIndex].EnclosingOwner);
        std::uint32_t Passed  = 0u;

        while (Walking != AbsentSlot)
        {
            if (Walking == SlotIndex || ++Passed > EnclosureDepthLimit)
                return false;

            Walking = Resolved(Enclosures[Walking].EnclosingOwner);
        }

        Walking = Resolved(Attachments[SlotIndex].AttachmentOwner);
        Passed  = 0u;

        while (Walking != AbsentSlot)
        {
            if (Walking == SlotIndex || ++Passed > EnclosureDepthLimit)
                return false;

            Walking = Resolved(Attachments[Walking].AttachmentOwner);
        }
    }

    return true;
}

bool SceneStructure::LabelsNested() const
{
    for (std::uint32_t SlotIndex = 0u; SlotIndex < SlotGenerations.size(); ++SlotIndex)
    {
        if (SlotGenerations[SlotIndex] == 0u)
            continue;

        const EnclosureRecord& Held = Enclosures[SlotIndex];

        if (Held.Label.LabelBegin == 0u || Held.Label.LabelEnd <= Held.Label.LabelBegin)
            return false;

        const std::uint32_t EnclosureSlot = Resolved(Held.EnclosingOwner);

        if (EnclosureSlot != AbsentSlot && !EnclosureContains(Enclosures[EnclosureSlot].Label, Held.Label))
            return false;

        // 📝 Disjoint enclosures never overlap, which over one ordering is exactly that each label begins
        //    after the one before it ended. Checking the ordering covers every disjoint pair at once.
        if (Held.NextInOrder != AbsentSlot
         && Enclosures[Held.NextInOrder].Label.LabelBegin <= Held.Label.LabelEnd)
        {
            return false;
        }
    }

    return true;
}

}   // namespace Slate
