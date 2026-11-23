//============================================================================================================================================
//                                                          PREVIEWPROJECTION.H
//============================================================================================================================================
// 🧩 `82` — what an action will produce, shown before it is committed to, as one speculative extent four consumers share.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateCompute/Compute/AnalyticProjection/Api/AnalyticProjection.h"
#include "SlateCompute/Compute/ImpressionSequence/Api/ImpressionSequence.h"
#include "SlateCompute/Compute/SurfaceTileSpace/Api/SurfaceTileSpace.h"
#include "SlateDocument/Document/BrushSpecification/Api/BrushSpecification.h"
#include "SlateDocument/Document/SurfaceLayerSequence/Api/SurfaceLayerSequence.h"
#include "SlateDocument/Document/ToolSequence/Api/ToolSequence.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT IS BEING PREVIEWED
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 `82` §2's four consumers, as the one enumeration they are gathered under.
/// note  🔴 Distinct from `76`'s `PreviewSubject`, which is a **tool's** declaration of what it previews and
///        therefore carries no entry for the content preview — that one is a comparison the artist asks for
///        directly and belongs to no tool. Folding the two would have made the content preview a tool nobody
///        selects, or would have put an entry in `76`'s enum that no `ToolSpecification` ever names.
/// note  📝 The four differ in what they resolve through and in nothing else. `82` §1's properties are identical
///        across all four, which is the whole reason this document gathers them rather than letting each consumer
///        arrive at its own — and the gathering is only true if one enumeration covers them.
/// tag   guarantee
enum class SpeculativeSubject : std::uint32_t
{
    BrushImpression = 0u,   // [-] - the impression about to be applied, under the cursor — through `58` and `22`
    SurfaceContent  = 1u,   // [-] - the surface's current content over an extent — through `20`'s tiles
    PlacementDrag   = 2u,   // [-] - a placement under the manipulator, before release — through `70`
    ParameterDrag   = 3u,   // [-] - the result at the value currently being dragged
    SubjectCount    = 4u    // [-] - the closed count, never a subject
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 WHAT CARRIES A THUMBNAIL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The content a row may present — `82` §3's six entries.
/// tag   guarantee
enum class ThumbnailSubject : std::uint32_t
{
    Imagery       = 0u,   // [-] - `50`'s decoded content
    Tiling        = 1u,   // [-] - `54`'s pattern declaration
    Brush         = 2u,   // [-] - `58`'s shape and channels
    Material      = 3u,   // [-] - `42`'s channel set
    Text          = 4u,   // [-] - presented by the text itself
    VectorContent = 5u,   // [-] - presented by the source's name
    SubjectCount  = 6u    // [-] - the closed count, never a content
};

/// 🧩 Whether one content is presented by a rendered miniature at all — `82` §3's table, as a compile-time answer.
/// note  🔴 Text and vector content have **none**. Both are analytic, so a thumbnail is a resolution at a size
///        chosen for a row, and at row height a glyph outline and a vector outline both reduce to an indistinct
///        mark. A row of decal placements each showing an indistinct mark is worse than a row showing the text.
/// note  📝 Answered before the resolution is scheduled rather than after it produces something unreadable. A
///        thumbnail that is generated and then discarded by the panel has already been paid for, and it has been
///        paid for once per row per rotation for as long as the row is presented.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr bool ThumbnailDeclared(ThumbnailSubject Current)
{
    return Current == ThumbnailSubject::Imagery
        || Current == ThumbnailSubject::Tiling
        || Current == ThumbnailSubject::Brush
        || Current == ThumbnailSubject::Material;
}
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Exact, PrecisionGuarantee::Exact);

/// 🧩 Whether one content is presented by its source instead — the complement, stated rather than inferred.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr bool CurrentBySource(ThumbnailSubject Current)
{
    return Current != ThumbnailSubject::SubjectCount && !ThumbnailDeclared(Current);
}
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Exact, PrecisionGuarantee::Exact);

// 🔴 `82` §3's six rows, as six static assertions rather than as six lines of prose a reader has to trust. The
//    two rows that matter are the last two: they are the ones a later reader will be tempted to "fix" by adding
//    a rendered miniature, and the assertion is what makes that a compilation failure with a reason attached.
static_assert(ThumbnailDeclared(ThumbnailSubject::Imagery),        "imagery carries a thumbnail — `82` §3");
static_assert(ThumbnailDeclared(ThumbnailSubject::Tiling),         "a tiling carries a thumbnail — `82` §3");
static_assert(ThumbnailDeclared(ThumbnailSubject::Brush),          "a brush carries a thumbnail — `82` §3");
static_assert(ThumbnailDeclared(ThumbnailSubject::Material),       "a material carries a thumbnail — `82` §3");
static_assert(CurrentBySource(ThumbnailSubject::Text),           "text is presented by the text — `82` §3");
static_assert(CurrentBySource(ThumbnailSubject::VectorContent),  "vector content by its name — `82` §3");

// 🚧 The extent a thumbnail resolves at. Whether it is **stored** is `82` §7's first open row and it blocks `48`'s
//    document size, so nothing here retains one: a thumbnail is re-resolved, exactly like every other preview, and
//    the day the answer arrives it is a depot keyed on content rather than an amendment to this.
inline constexpr std::uint32_t ThumbnailExtentTexels = 128u;   // [px] - per edge, square, re-resolved every time

//------------------------------------------------------------------------------------------------------------------------
//                                                 ONE SPECULATIVE EXTENT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One preview as it stands this rotation — `22` §4.1's speculative extent, unamended.
/// note  🔴 There is no member declaring whether this blocks eviction, and its absence is the mechanism rather
///        than an omission. `20`'s `DeclareUncommitted` is the only thing in the engine that blocks an eviction,
///        this component never calls it, and `22`'s resolution skips it for a speculative stroke — so the gate
///        holds by there being exactly one door and nothing here walking through it. A member would be a second
///        answer that could disagree with the first.
/// note  🔴 `ResolvedAt` is compared against the rotation, not against a revision. A preview is discarded and
///        re-resolved **each rotation** — it is not a cache with an invalidation rule, and giving it one would
///        make a stale preview a defect to diagnose rather than a state that cannot occur.
/// tag   nonallocating, nonthrowing
struct SpeculativeExtent
{
    SpeculativeSubject  Previewed        = SpeculativeSubject::SubjectCount;   // [-] - which consumer declared it
    std::uint64_t       ResolvedAt       = 0u;    // [-]  - the rotation it was resolved in; stale in any other
    std::uint32_t       SurfaceIndex   = 0u;    // [-]  - the surface it addresses
    std::uint32_t       ResolvedLevel    = 0u;    // [-]  - the level it actually resolved at; `20` may be coarser
    std::uint32_t       RequestedLevel   = 0u;    // [-]  - the level it asked for
    bool                ExtentHeld   = false; // [-]  - false where nothing is previewed at all
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHERE PREVIEWS READ
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What every preview resolves through, borrowed and never owned.
/// note  🔴 `82` §5: previews resolve on the **host**, through `70` §4's host path, and this is that path's own
///        component. A preview resolved by a second implementation is a preview that disagrees with the committed
///        result in exactly the way `00` §11's Tier B gate exists to catch — and where the two disagree the artist
///        blames the preview, because the preview is the thing that looks provisional.
/// tag   nonallocating, nonthrowing
struct PreviewSources
{
    const AnalyticProjection*  Resolution = nullptr;   // [-] - `70`, the one resolver both paths read
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE PREVIEWS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The four previews of `82` §2, resolved on the host and discarded each rotation.
/// note  🔴 Nothing here mutates a document, records a transaction or enters `RevisionSequence` — `82` §1 and §6.
///        The brush preview is the only one that touches a mutating component at all, and it holds that component
///        with `StrokeDeclaration::Speculative` declared, which is what makes `22`'s own Seal refuse it.
/// note  📝 The domain view of `82` §4 is **not** a fifth preview. It is the content preview over the whole domain,
///        presented two-dimensionally by `14`, and that is what discharges §6's gate that domain-view texturing and
///        placement go through the same paths as their three-dimensional forms: there is one path, so they cannot
///        differ. A separate domain resolution would have been the second path the gate forbids.
/// tag   owning
class PreviewProjection
{
public:

    /// 🧩 Takes the resolver every preview reads.
    /// in    Supplied  [-]  borrowed; outlives this component
    /// out   Result   [-]  refuses with ContentUnsupported for an absent resolver
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> ConstructPreviewProjection(const PreviewSources& Supplied);

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  THE BRUSH PREVIEW
    //--------------------------------------------------------------------------------------------------------------------

    /// 🧩 Opens the brush preview against a declared brush — `82` §2's first row.
    /// in    Declaring  [-]  the surface, the entry and the packing; Speculative is declared here, not by the caller
    /// in    Brushed    [-]  `58`'s declaration, resolved exactly as a committed stroke resolves it
    /// out   Result    [-]  refuses with HostDenied when a preview is already open, and with whatever `22` rejected
    /// post  🔴 the held stroke is speculative; it pins no tile and can never seal
    /// note  🔴 The preview shows the **resolved impression** — extent, coverage falloff, the combine specification
    ///        and the colour — and not an outline of the radius. `82` §6's fourth gate, and the reason it is a gate:
    ///        an outline answers "how big" and the artist is asking "what will this look like", which a soft brush
    ///        at low strength answers very differently from a hard one at the same radius.
    /// note  🔴 `Speculative` is declared **here** and the caller's own value is overwritten. A caller that could
    ///        pass false would be a caller that could commit a preview, and the refusal would then arrive from
    ///        `22`'s Seal rather than from the component whose whole subject is that it never commits.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> OpenImpression(const StrokeDeclaration& Declaring, const BrushSpecification& Brushed);

    /// 🧩 Moves the previewed impression to where the cursor now stands.
    /// in    Incoming  [-]  the pointer sample and the domain position `74` resolved it to
    /// out   Result   [-]  refuses with HostDenied before Open, and with whatever `22` rejected
    /// note  📝 The accumulation is reclaimed before the arrival is accepted, so the preview shows the impression
    ///        **at** the cursor rather than the trail of every position the cursor has passed through. A committed
    ///        stroke accumulates because the trail is the stroke; a preview accumulating would answer a question
    ///        about a stroke the artist has not made.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> AmendImpression(const StrokeArrival& Incoming);

    /// 🧩 Resolves the previewed impression against whatever residency accepts, demanding nothing it may pin.
    /// in    Residency        [-]  the surface's cells and tiles
    /// in    Requesting       [-]  where a demand for a non-resident cell is recorded
    /// in    RecordingIndex  [-]  the rotation resolving
    /// out   Result          [-]  refuses with HostDenied before Open
    /// post  🔴 nothing was pinned; `DeclareUncommitted` was not called and cannot have been
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<ResolvedRun> ResolveImpression(SurfaceTileSpace& Residency,
                                           RequestQueue&     Requesting,
                                           std::uint64_t     RecordingIndex);

    /// 🧩 Closes the brush preview. Nothing was recorded, so there is nothing to abandon beyond the accumulation.
    /// cost  🚩
    /// tag   api, nonthrowing
    void CloseImpression(SurfaceTileSpace& Residency);

    /// 🧩 The accumulated coverage the preview stands at, for whoever presents it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const StrokeSpace& ImpressionCoverage() const;

    /// 🧩 Whether a brush preview is open.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool ImpressionCurrent() const;

    //--------------------------------------------------------------------------------------------------------------------
    //                                              THE CONTENT PREVIEW
    //--------------------------------------------------------------------------------------------------------------------

    /// 🧩 The surface's current content at one domain position — `82` §2's second row, and `82` §4's domain view.
    /// in    Content         [-]  the surface's layer sequence
    /// in    Placements      [-]  where each channel sits among the components
    /// in    PositionX   [-]  the domain's first axis
    /// in    PositionY  [-]  its second
    /// in    Level           [-]  the reduction level the tolerance is taken at; zero is finest
    /// in    ComponentCount  [-]  components per texel
    /// out   Result         [-]  refuses with HostDenied before Construct, with ContentUnsupported outside the
    ///                            level count, and with whatever `70` rejected
    /// post  🔴 nothing was mutated; no transaction exists and no revision advanced
    /// note  🔴 This is a **read**. `82` §2 is explicit that its value is comparison — the artist wants to see what
    ///        something looked like a moment ago, or what a hidden entry contains, without changing anything to
    ///        find out — and a preview that mutated to answer would be a preview that has to be undone.
    /// note  📝 A hidden entry is previewed by the caller supplying a sequence with that entry present. The
    ///        presence rule is `56`'s and is not relaxed here; asking `70` to ignore it would have made hiding
    ///        mean two different things depending on who was asking.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<ResolvedSample> ProjectContentAt(const SurfaceLayerSequence&           Content,
                                             const std::vector<ChannelPlacement>&  Placements,
                                             double                                PositionX,
                                             double                                PositionY,
                                             std::uint32_t                         Level,
                                             std::uint32_t                         ComponentCount) const;

    //--------------------------------------------------------------------------------------------------------------------
    //                                             THE PLACEMENT PREVIEW
    //--------------------------------------------------------------------------------------------------------------------

    /// 🧩 A placement under the manipulator at one domain position — `82` §2's third row, `72` §3's drag seen.
    /// in    Content         [-]  the sequence carrying the placed entry, with the dragged transform already in it
    /// in    Placements      [-]  where each channel sits among the components
    /// in    PositionX   [-]  the domain's first axis
    /// in    PositionY  [-]  its second
    /// in    CoarseDeclared  [-]  true resolves at the coarse level `70` §5 permits, for the drag itself
    /// in    ComponentCount  [-]  components per texel
    /// out   Result         [-]  refuses as `ProjectContentAt` does
    /// note  📝 Coarse **then** refine, exactly as `70` §5 permits and no further. The coarse pass is what keeps a
    ///        placement drag responsive while the pointer is moving; the refinement is one resolution at the drag's
    ///        end, and it is the same routine at a different tolerance rather than a second path.
    /// note  ⚠️ The dragged transform is in the **supplied sequence** and is not a parameter here. `78`'s amendment
    ///        is composed against the placement's surface by its caller before this is asked, because `78` §1's
    ///        four targets each compose it differently and a preview choosing one would preview the wrong one for
    ///        the other three.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<ResolvedSample> ProjectPlacementAt(const SurfaceLayerSequence&           Content,
                                               const std::vector<ChannelPlacement>&  Placements,
                                               double                                PositionX,
                                               double                                PositionY,
                                               bool                                  CoarseDeclared,
                                               std::uint32_t                         ComponentCount) const;

    //--------------------------------------------------------------------------------------------------------------------
    //                                             THE PARAMETER PREVIEW
    //--------------------------------------------------------------------------------------------------------------------

    /// 🧩 Declares that a dragged parameter has moved, so the standing extent is owed a re-resolution.
    /// in    RecordingIndex  [-]  the rotation the amendment arrived in
    /// out   Result          [-]  refuses with HostDenied when no extent stands
    /// post  🔴 nothing is recorded; `10` §2.4's transaction stays open and its Seal is the caller's
    /// note  🔴 `82` §2's fourth row: every Amend is a re-resolution and **none** of them is recorded. The
    ///        amendment count is carried so `86` can measure how many re-resolutions one drag cost, and for no
    ///        other reason — it is not a revision and nothing keys on it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> AmendParameter(std::uint64_t RecordingIndex);

    /// 🧩 How many re-resolutions the standing parameter drag has asked for.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t AmendmentCount() const;

    //--------------------------------------------------------------------------------------------------------------------
    //                                              THE STANDING EXTENT
    //--------------------------------------------------------------------------------------------------------------------

    /// 🧩 Declares which of the four is being previewed, and in which rotation.
    /// in    Previewed        [-]  the consumer
    /// in    SurfaceIndex   [-]  the surface it addresses
    /// in    RequestedLevel   [-]  the level it asks for
    /// in    RecordingIndex  [-]  the rotation declaring it
    /// out   Result          [-]  refuses with ContentUnsupported for the closed count and outside the level count
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> DeclareExtent(SpeculativeSubject Previewed,
                                std::uint32_t      SurfaceIndex,
                                std::uint32_t      RequestedLevel,
                                std::uint64_t      RecordingIndex);

    /// 🧩 The extent as it stands.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const SpeculativeExtent& Current() const;

    /// 🧩 Whether the standing extent was resolved in the rotation asking.
    /// note  🔴 `22` §4.1: a speculative extent is discarded and re-resolved each rotation, so an extent from any
    ///        other rotation is not stale content to refresh — it is content that must not be presented at all.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool ExtentCurrent(std::uint64_t RecordingIndex) const;

    /// 🧩 Discards the standing extent. Called each rotation, and at every consumer's end.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void ReclaimExtent();

private:

    ImpressionSequence  Previewing;                     // [-] - the brush preview's own stroke, always speculative
    SpeculativeExtent   CurrentExtent   = {};          // [-] - what is previewed, and in which rotation
    const AnalyticProjection*  Resolution = nullptr;    // [-] - `70`, borrowed
    std::uint32_t       AmendedCount     = 0u;          // [-] - re-resolutions the standing parameter drag asked for
    bool                ImpressionOpen   = false;       // [-] - false until OpenImpression delivered
    bool                SourcesDeclared  = false;       // [-] - false until Construct delivered
};

// 📐 Every preview is a resolution through `70`, which is Bounded, and a residency read, which is Exact. The
//    weaker of the two is what this claims. Nothing here is Convergent — a preview that iterated would present a
//    different answer per rotation for an unchanged parameter, and the artist would read that as flicker.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Exact);

}   // namespace Slate
