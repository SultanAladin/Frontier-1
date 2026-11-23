//============================================================================================================================================
//                                                         PREVIEWPROJECTION.CPP
//============================================================================================================================================
// 🧩 `82` — the four previews resolved on the host, each one discarded and re-resolved every rotation.

#include "SlateCompute/Compute/PreviewProjection/Api/PreviewProjection.h"

namespace Slate
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT A DRAG RESOLVES AT
//------------------------------------------------------------------------------------------------------------------------

// 📝 The level a placement drag resolves at while the pointer is still moving. `70` §5 permits coarse-then-refine
//    and this is the coarse half; the refinement is the same routine at level zero, asked once at the drag's end.
//    Two levels coarser is a sixteenth of the sample positions and is where a drag stops feeling like a wait.
constexpr std::uint32_t CoarseDragLevel = 2u;   // [-] - reduction level a moving placement drag resolves at

// 📝 The level a settled resolution takes its tolerance at. Named rather than spelled `0u` at three call sites,
//    because a reader meeting a bare zero cannot tell whether it is a level, a surface or a component.
constexpr std::uint32_t FinestLevel = 0u;       // [-] - the finest reduction level; the refinement resolves here

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IT READS
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> PreviewProjection::ConstructPreviewProjection(const PreviewSources& Supplied)
{
    // 🔴 `82` §5: previews resolve through `70`'s host path and through nothing else. An absent resolver is not
    //    a preview that degrades to something simpler — it is a preview that would have to invent a second
    //    implementation, which is the disagreement `00` §11's Tier B gate exists to catch.
    if (Supplied.Resolution == nullptr)
    {
        return Deliver<bool>::Refuse({RefusalReason::ContentUnsupported,
                                      "a preview has no resolver; `70` is the only path `82` §5 permits"});
    }

    Resolution      = Supplied.Resolution;
    SourcesDeclared = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE BRUSH PREVIEW
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> PreviewProjection::OpenImpression(const StrokeDeclaration& Declaring, const BrushSpecification& Brushed)
{
    if (ImpressionOpen)
    {
        return Deliver<bool>::Refuse({RefusalReason::HostDenied,
                                      "a brush preview already stands; close it before opening another"});
    }

    // 🔴 The caller's own declaration is overwritten here rather than checked. A caller that could pass false
    //    would be a caller that could commit a preview, and the refusal would then arrive from `22`'s Seal —
    //    from the component that commits — rather than from the component whose whole subject is that it does not.
    StrokeDeclaration Speculating = Declaring;
    Speculating.Speculative       = true;

    const Deliver<bool> Opened = Previewing.Open(Speculating, Brushed);
    if (!Opened.Resolved)
    {
        return Opened;
    }

    ImpressionOpen = true;

    return Deliver<bool>::Result(true);
}

Deliver<bool> PreviewProjection::AmendImpression(const StrokeArrival& Incoming)
{
    if (!ImpressionOpen)
    {
        return Deliver<bool>::Refuse({RefusalReason::HostDenied,
                                      "no brush preview stands; Open before amending"});
    }

    // 📝 The accumulation is reclaimed **before** the arrival is accepted, so what stands after this is the
    //    impression at the cursor rather than the trail of every position the cursor has passed through. A
    //    committed stroke accumulates because the trail is the stroke; a preview accumulating would answer a
    //    question about a stroke the artist has not made.
    const Deliver<bool> Reclaimed = Previewing.ReclaimSpeculative();
    if (!Reclaimed.Resolved)
    {
        return Reclaimed;
    }

    return Previewing.Amend(Incoming);
}

Deliver<ResolvedRun> PreviewProjection::ResolveImpression(SurfaceTileSpace& Residency,
                                                          RequestQueue&     Requesting,
                                                          std::uint64_t     RecordingIndex)
{
    if (!ImpressionOpen)
    {
        return Deliver<ResolvedRun>::Refuse({RefusalReason::HostDenied,
                                             "no brush preview stands; Open before resolving"});
    }

    // 🔴 `82` §1's separating property is discharged here and it is discharged by **absence**. `20`'s
    //    `DeclareUncommitted` is the only thing in the engine that blocks an eviction, it is not called on this
    //    path, and `22`'s own resolution skips it for a speculative stroke. A preview that pinned would exhaust
    //    residency while the artist hovers across a surface without texturing anything.
    return Previewing.Resolve(Residency, Requesting, RecordingIndex);
}

void PreviewProjection::CloseImpression(SurfaceTileSpace& Residency)
{
    Previewing.Abandon(Residency);
    ImpressionOpen = false;
}

const StrokeSpace& PreviewProjection::ImpressionCoverage() const
{
    return Previewing.Accumulation();
}

bool PreviewProjection::ImpressionCurrent() const
{
    return ImpressionOpen;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE CONTENT PREVIEW
//------------------------------------------------------------------------------------------------------------------------

Deliver<ResolvedSample> PreviewProjection::ProjectContentAt(const SurfaceLayerSequence&           Content,
                                                            const std::vector<ChannelPlacement>&  Placements,
                                                            double                                PositionX,
                                                            double                                PositionY,
                                                            std::uint32_t                         Level,
                                                            std::uint32_t                         ComponentCount) const
{
    if (!SourcesDeclared)
    {
        return Deliver<ResolvedSample>::Refuse({RefusalReason::HostDenied,
                                                "no resolver was declared; Construct before previewing content"});
    }

    if (Level >= ReductionLevelCount)
    {
        return Deliver<ResolvedSample>::Refuse({RefusalReason::ContentUnsupported,
                                                "the level lies outside `20`'s reduction levels"});
    }

    // 🔴 A read, and only a read. `82` §2's second row exists so the artist can see what something looked like a
    //    moment ago, or what a hidden entry carries, without changing anything to find out — a preview that
    //    mutated to answer would be a preview that has to be undone.
    return Resolution->ResolveAt(Content,
                                 Placements,
                                 PositionX,
                                 PositionY,
                                 ToleranceAtLevel(Level),
                                 ComponentCount);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE PLACEMENT PREVIEW
//------------------------------------------------------------------------------------------------------------------------

Deliver<ResolvedSample> PreviewProjection::ProjectPlacementAt(const SurfaceLayerSequence&           Content,
                                                              const std::vector<ChannelPlacement>&  Placements,
                                                              double                                PositionX,
                                                              double                                PositionY,
                                                              bool                                  CoarseDeclared,
                                                              std::uint32_t                         ComponentCount) const
{
    // 📝 Coarse and refined are the **same routine** at two tolerances, not two paths. `70` §5 permits exactly
    //    this and nothing further; a separate coarse resolver would be the second implementation `82` §5 forbids,
    //    and the drag would then settle onto an answer that disagrees with the one it was showing.
    const std::uint32_t Taken = CoarseDeclared ? CoarseDragLevel : FinestLevel;

    return ProjectContentAt(Content, Placements, PositionX, PositionY, Taken, ComponentCount);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE PARAMETER PREVIEW
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> PreviewProjection::AmendParameter(std::uint64_t RecordingIndex)
{
    if (!CurrentExtent.ExtentHeld)
    {
        return Deliver<bool>::Refuse({RefusalReason::HostDenied,
                                      "no extent stands; declare one before amending a dragged parameter"});
    }

    // 🔴 Nothing is recorded. `10` §2.4's transaction stays open across every amendment and its Seal belongs to
    //    the caller; the count exists so `86` can measure what one drag cost in re-resolutions and for no other
    //    reason. Nothing keys on it and no revision advances from it.
    ++AmendedCount;
    CurrentExtent.ResolvedAt = RecordingIndex;

    return Deliver<bool>::Result(true);
}

std::uint32_t PreviewProjection::AmendmentCount() const
{
    return AmendedCount;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE STANDING EXTENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> PreviewProjection::DeclareExtent(SpeculativeSubject Previewed,
                                               std::uint32_t      SurfaceIndex,
                                               std::uint32_t      RequestedLevel,
                                               std::uint64_t      RecordingIndex)
{
    if (Previewed == SpeculativeSubject::SubjectCount)
    {
        return Deliver<bool>::Refuse({RefusalReason::ContentUnsupported,
                                      "the closed count is not one of `82` §2's four consumers"});
    }

    if (RequestedLevel >= ReductionLevelCount)
    {
        return Deliver<bool>::Refuse({RefusalReason::ContentUnsupported,
                                      "the requested level lies outside `20`'s reduction levels"});
    }

    // 📝 `ResolvedLevel` opens equal to the requested one and is amended by whoever resolves. `20` may only ever
    //    answer coarser, so the two agreeing is the ordinary case and the difference is what `14` reads to know
    //    it is presenting something not yet at the extent that was asked for.
    CurrentExtent.Previewed      = Previewed;
    CurrentExtent.ResolvedAt     = RecordingIndex;
    CurrentExtent.SurfaceIndex = SurfaceIndex;
    CurrentExtent.ResolvedLevel  = RequestedLevel;
    CurrentExtent.RequestedLevel = RequestedLevel;
    CurrentExtent.ExtentHeld = true;

    AmendedCount = 0u;

    return Deliver<bool>::Result(true);
}

const SpeculativeExtent& PreviewProjection::Current() const
{
    return CurrentExtent;
}

bool PreviewProjection::ExtentCurrent(std::uint64_t RecordingIndex) const
{
    // 🔴 `22` §4.1: a speculative extent is discarded and re-resolved each rotation. An extent carrying any other
    //    rotation is therefore not stale content to refresh — it is content that must not be presented at all,
    //    and answering false is what keeps that a state `14` cannot reach rather than a defect to diagnose.
    return CurrentExtent.ExtentHeld && CurrentExtent.ResolvedAt == RecordingIndex;
}

void PreviewProjection::ReclaimExtent()
{
    CurrentExtent = {};
    AmendedCount   = 0u;
}

}   // namespace Slate
