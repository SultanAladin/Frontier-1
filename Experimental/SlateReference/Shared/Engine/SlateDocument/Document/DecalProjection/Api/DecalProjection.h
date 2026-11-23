//============================================================================================================================================
//                                                            DECALPROJECTION.H
//============================================================================================================================================
// 🧩 Placed content — a source, a transform stored against the surface, and the extent it covers in the domain.

#pragma once

#include "Foundation/Combination.h"
#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateDocument/Document/SpatialSubdivision/Api/SpatialSubdivision.h"
#include "SlateDocument/Document/TopologyStructure/Api/TopologyStructure.h"
#include "SlateMath/Numeric/TransformProjection/Api/TransformProjection.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE TWO MODES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How a placement is positioned on its surface — `00` §10.1 ①'s two persistent modes.
/// note  🔴 Screen placement is **absent from this enumeration** and that is the ruling rather than an omission.
///        `00` §10.1 ① makes it a gesture: while the pointer is down the placing transform is recomputed each
///        rotation from the live camera, and on release the last computed transform simply stands. A placement
///        that stayed bound to the live camera would slide across the surface as the artist orbits, and the
///        artist reads that as the placement having moved.
/// note  🔴 A domain placement cannot cross a chart seam and a projected placement crosses them freely. That is
///        why both exist rather than one being the general case: a projected placement crossing a seam is
///        continuous on the surface and discontinuous in the domain, and a domain placement is the reverse.
/// tag   guarantee
enum class PlacementMode : std::uint32_t
{
    DomainPlaced    = 0u,   // [-] - positioned directly in the parametric domain; one chart only
    ProjectedPlaced = 1u,   // [-] - a projecting transform, attached through `AttachmentFollows`
    ModeCount       = 2u    // [-] - the closed count, never a mode
};

/// 🧩 Which content library a placement's source ordinal indexes.
/// note  ⚠️ `Text` is held apart from `VectorOutline` even though a glyph is an outline. `52` §3 stores text as
///        a glyph sequence beside its characters, and the positioning walk over advances and pair adjustments is
///        the placement's rather than the outline's — a text placement resolves several outlines at offsets the
///        typeface declares, and a vector placement resolves one.
/// tag   guarantee
enum class PlacedSource : std::uint32_t
{
    VectorOutline = 0u,   // [-] - an `OutlineSpecification` from `52`
    Imagery       = 1u,   // [-] - a decoded image from `50`
    Tiling        = 2u,   // [-] - a pattern declaration from `54`
    Text          = 3u,   // [-] - a `ResolvedText` from `52`, positioned by its own sequence
    SourceCount   = 4u    // [-] - the closed count, never a source
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     ONE PLACEMENT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One placed source and everything `72` §1 declares about it.
/// note  🔴 `PlacingTransform` is stored **relative to the surface the placement is attached to**, never in
///        document space. `00` §10.1 ②'s first two rows depend on it entirely: a camera move re-resolves nothing
///        and an owner move re-resolves nothing, because neither changes a transform expressed against the
///        surface. Stored absolutely, both rows become "re-resolve everything" and the invalidation table
///        degenerates into a list of things that always change.
/// note  🔴 `RevisionCounter` is advanced by `00` §10.1 ②'s **third row alone** — the placing transform changing
///        relative to the surface, or the source being replaced. It is not advanced by the owner moving, by
///        the camera moving, or by a promotion. `70` §2 compares it as one integer per tile, and the comparison's
///        answer is almost always "no work" precisely because the counter is this quiet.
/// note  ⚠️ The channel mask is `42`'s channel ordinals as bits, matching `56`'s `LayerSpecification`. A logo
///        carrying roughness as well as albedo is one placement writing two channels, and `72` §1 makes undo
///        restore both — the same rule `22` §5 states for a stroke.
/// tag   owning
struct PlacementSpecification
{
    PlacedSource          Source              = PlacedSource::VectorOutline;   // [-]  - which library
    std::uint32_t         SourceIndex       = 0u;                            // [-]  - into `52`, `50` or `54`
    PlacementMode         Mode                = PlacementMode::DomainPlaced;   // [-]
    DecomposedTransform   PlacingTransform    = {};                            // [-]  - relative to the surface
    OwnerIdentity      Owner            = {};                            // [-]  - what it is attached to
    std::uint32_t         ChannelMask         = 0u;                            // [-]  - one bit per `42` channel
    CombineSpecification  Combination         = CombineSpecification::Over;    // [-]  - `22` §3's, unamended
    double                ProjectedHalfX  = 0.5;                           // [mm] - Projected; the volume's half extent
    double                ProjectedHalfY = 0.5;                           // [mm]
    double                ProjectedReach      = 1000.0;                        // [mm] - Projected; how far it reaches
    bool                  BackFacingAccepted  = false;                         // [-]  - 🚧 `72` §6's open row
    std::uint64_t         RevisionCounter     = 1u;                            // [-]  - `00` §10.1 ② row three only
};

/// 🧩 Whether a placement writes one of `42`'s channels.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr bool PlacementWritesChannel(const PlacementSpecification& Placed, std::uint32_t ChannelIndex)
{
    return (Placed.ChannelMask & (1u << ChannelIndex)) != 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE DERIVED EXTENT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Derives the domain extent one placement covers on a surface.
/// in    Placed            [-]   the placement
/// in    PlacementIndex  [-]   an ordinal `PlacementIndex` registered; carried into the extent
/// in    SequenceIndex   [-]   the placement's position in `56`'s sequence — `00` §10.1 ③'s one ordinal
/// in    Imported          [-]   the sealed topology the surface carries
/// in    CornerCoordinates [-]   one domain coordinate per imported corner
/// out   Result           [-]   refuses with ContentUnsupported for an unsealed topology, a coordinate run
///                               disagreeing with the corner count, and a placement reaching no corner at all
/// note  🔴 The two modes derive their extent by different routes and neither is the other's special case. A
///        **domain** placement covers the transformed unit square directly, because that is where it was
///        positioned. A **projected** placement covers whatever the projecting volume reaches, which is a
///        question about the topology and cannot be answered from the transform alone.
/// note  🔴 The coordinates are **supplied** rather than read from `68`. A surface carrying an imported domain
///        addresses `TopologyStructure::Coordinates` and one that was unwrapped addresses `ChartPartition`; the
///        caller knows which and this component does not. Reaching for `68` would also put a `SlateCompute`
///        component in this file's Upstream, which the peer partition forbids outright.
/// note  ⚠️ The derived extent is conservative **outward**, matching `38` §6 and `40` §6. An inward-rounded
///        extent means `74` §3's precedence-1 test misses along one edge of the placement, and the artist meets
///        it as a decal whose border cannot be clicked.
/// note  ⚠️ 🚧 A projected placement accepts or refuses back-facing corners by its own declaration, and `72` §6
///        carries what the right rule is as open. Refusing is the conservative reading — a decal projected onto
///        a shoulder should not also appear on the far side of the arm — and it is what the default declares.
/// cost  🔴
/// tag   api, nonthrowing
Deliver<DomainExtent> ProjectPlacementExtent(const PlacementSpecification&         Placed,
                                             std::uint32_t                         PlacementIndex,
                                             std::uint32_t                         SequenceIndex,
                                             const TopologyStructure&              Imported,
                                             const std::vector<DomainCoordinate>&  CornerCoordinates);

// 📐 The transform composition is Bounded and the corner walk is a comparison over supplied coordinates. The
//    outward rounding only ever widens, so it introduces no standing the guarantee does not already admit.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

/// 🧩 Carries one domain position into a placement's own source space.
/// in    Placed          [-]  the placement
/// in    PositionX   [-]  the domain's first axis
/// in    PositionY  [-]  its second
/// out   SourceX     [-]  the position in the source's own unit square
/// out   SourceY    [-]
/// out   Covered         [-]  false where the position lies outside the source's unit square
/// note  🔴 This is the whole of `70` §3's placement row. A placement is **not** a fourth resolution mechanism;
///        it is a transform into a source space, and the source is then one of the other three. That is why text,
///        imagery, vector content and tiling all place identically and why `70` needs no per-source placement
///        path.
/// note  📐 The inverse of the placing transform, applied to a domain position. The rotation is conjugated and
///        the scale reciprocated rather than a matrix being inverted, because `02` §3.1 keeps a transform
///        decomposed and a general inversion would reintroduce the drift the decomposition exists to avoid.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
bool ProjectIntoSource(const PlacementSpecification& Placed,
                       double                        PositionX,
                       double                        PositionY,
                       double&                       SourceX,
                       double&                       SourceY);

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE PLACEMENTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 No placement; never a valid placement ordinal. Sibling of `ByteSpace`'s `AbsentExtent`.
inline constexpr std::uint32_t AbsentPlacement = 0xFFFFFFFFu;   // [-] - the resolution names no placement

/// 🧩 Every placement the document holds, addressed by the ordinal `56`'s layer entries carry.
/// note  🔴 A placement holds no sequence position of its own. `00` §10.1 ③ rules that enclosure order and layer
///        order are the **same stored ordinal**, and that ordinal is `56`'s sequence position — so a second copy
///        here would be a second ordering, and the two would disagree the first time a row was dragged. The
///        caller reads the position from `56` and hands it to `ProjectPlacementExtent`.
/// note  ⚠️ A released slot is reused rather than erased, so an ordinal a `56` entry recorded keeps naming what
///        it named. Erasing would renumber every placement above it and `56` would not observe it.
/// tag   owning
class PlacementIndex
{
public:

    /// 🧩 Declares one placement and issues the ordinal `56` refers to it by.
    /// out   Result  [-]  refuses with IdentityStale for an undeclared owner, with ContentUnsupported for a
    ///                     source outside the declared set or a projected volume of no extent, and with
    ///                     ExtentExhausted at the declared ceiling
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Declare(const PlacementSpecification& Declaring);

    /// 🧩 Amends one placement, advancing its revision only where `00` §10.1 ② requires it.
    /// in    PlacementIndex  [-]  an ordinal this component registered
    /// in    Amending          [-]  the amended specification
    /// out   Result           [-]  refuses with ContentUnsupported for an unclaimed ordinal
    /// note  🔴 The revision advances when the placing transform, the source or the channel mask changed, and
    ///        **not** when the combination or the back-facing rule did. `70` §2's comparison is what re-resolves
    ///        a tile, and re-resolving on a combination change would re-resolve a value the combination is
    ///        applied to afterwards.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Amend(std::uint32_t PlacementIndex, const PlacementSpecification& Amending);

    /// 🧩 One declared placement.
    /// out   Result  [-]  refuses with ContentUnsupported for an unclaimed ordinal
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<const PlacementSpecification*> Resolve(std::uint32_t PlacementIndex) const;

    /// 🧩 Withdraws one placement, returning its slot for reuse.
    /// note  🔴 Called from `12` §12's retirement cascade, inside that cascade's single transaction. A placement
    ///        is enclosed under the owner it is attached to — `00` §10.1 ③ — so it retires with that owner
    ///        rather than surviving it as an orphaned reference `56` still names.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Withdraw(std::uint32_t PlacementIndex);

    /// 🧩 One placement's revision, for `70` §2's per-tile comparison.
    /// out   Revision  [-]  zero for an unclaimed ordinal, which no resolved tile ever recorded
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t Revision(std::uint32_t PlacementIndex) const;

    std::uint32_t DeclaredCount() const;

private:

    struct HeldPlacement
    {
        PlacementSpecification  Declared     = {};      // [-] - as declared and amended
        bool                    SlotOccupied = false;   // [-] - false once withdrawn
    };

    static constexpr std::uint32_t PlacementLimit = 65536u;   // [-] - placements one document may hold

    std::vector<HeldPlacement>  Placements       = {};   // [-] - released slots are reused, never erased
    std::vector<std::uint32_t>  ReleasedIndexs = {};   // [-] - slots free for reuse
    std::uint32_t               OccupiedCount    = 0u;   // [-] - placements currently declared
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE POSITIONING DRAG
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One placement being positioned, following `10` §2.4's lifecycle exactly.
/// note  🔴 `72` §3: a positioning drag records **no** transaction until release. `10` §2.4 gives the reason and
///        it is not weakened here — a transaction per pointer sample fills `RevisionSequence` with positions the
///        artist never intended to stop at, and undo then steps back one pixel at a time.
/// note  🔴 Between Open and Seal the placement is a `22` §4.1 **speculative extent**: display-only, discarded
///        and re-resolved each rotation, never entering `RevisionSequence`, and — the property that separates it
///        from an uncommitted stroke — never blocking eviction. A positioning drag that pinned every tile it
///        passed over would exhaust residency while the artist was still deciding where to put the decal.
/// note  🔴 `CameraFollowing` is the whole of `00` §10.1 ①'s screen gesture, and it is held **here** rather than
///        as a third `PlacementMode`. `78` recomputes the projecting transform from the live camera at each
///        Amend while it holds; Seal simply stops asking, so the last computed transform is the frozen one and no
///        freezing step exists to be forgotten. The gesture never existed as a persistent mode, exactly as
///        `00` §10.1 ① requires.
/// note  ⚠️ The camera itself is **not** read here. `78` owns the drag arithmetic and hands the composed
///        transform over; a camera edge declared in this file would be an Upstream edge nothing reads, which is
///        the defect `00` §10 conflict 41 records.
/// tag   owning
class PlacementSequence
{
public:

    /// 🧩 Opens a positioning drag against a declared placement.
    /// in    PlacementIndex  [-]  the placement being positioned
    /// in    Current          [-]  its specification as it stands; restored by Abandon
    /// in    CameraFollowed    [-]  true for the screen gesture, false for a domain or projected drag
    /// out   Result           [-]  refuses with HostDenied when a drag is already open
    /// post  nothing is recorded; the placement stands unamended until Seal
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Open(std::uint32_t PlacementIndex, const PlacementSpecification& Current, bool CameraFollowed);

    /// 🧩 Amends the open drag's placing transform.
    /// out   Result  [-]  refuses with HostDenied when no drag is open
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Amend(const DecomposedTransform& Amending);

    /// 🧩 Ends the drag with no effect, returning the specification that stood at Open.
    /// out   Result  [-]  refuses with HostDenied when no drag is open
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<PlacementSpecification> Abandon();

    /// 🧩 Ends the drag, returning the specification the caller commits as one transaction.
    /// out   Result  [-]  refuses with HostDenied when no drag is open
    /// post  🔴 the returned specification carries an advanced revision; `70` re-resolves against it
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<PlacementSpecification> Seal();

    /// 🧩 The placement as the drag has amended it, for `82`'s speculative resolution.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const PlacementSpecification& Amended() const;

    std::uint32_t Subject() const;
    bool          GestureOpen() const;
    bool          CameraFollowing() const;

private:

    PlacementSpecification  PriorPlacement   = {};                 // [-] - held at Open, restored at Abandon
    PlacementSpecification  AmendedPlacement = {};                 // [-] - what Amend writes
    std::uint32_t           SubjectIndex   = AbsentPlacement;    // [-] - the placement being positioned
    bool                    OpenDeclared     = false;              // [-] - Open delivered, Seal has not
    bool                    CameraFollowed   = false;              // [-] - the screen gesture is open
};

// 📐 Placement ordinals, channel masks and revisions are Exact; the placing transform, the derived extent and
//    the source-space projection are Bounded. `00` §3's transitivity rule folds the two to the weaker.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded, PrecisionGuarantee::Exact);

}   // namespace Slate
