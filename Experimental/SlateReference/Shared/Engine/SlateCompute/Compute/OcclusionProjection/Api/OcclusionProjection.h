//============================================================================================================================================
//                                                          OCCLUSIONPROJECTION.H
//============================================================================================================================================
// 🧩 `60`'s two occlusion terms, kept apart — the per-illuminant projection that attenuates the direct term, and the scalar hemisphere that attenuates the ambient.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "Shared/OcclusionProjection.slang.h"
#include "SlateDocument/Document/CameraProjection/Api/CameraProjection.h"
#include "SlateDocument/Document/IlluminantPopulation/Api/IlluminantPopulation.h"
#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"
#include "SlateVulkan/Device/RenderSchedule/Api/RenderSchedule.h"

#include <cstdint>
#include <vector>

namespace Slate
{

// 📝 No projection; never a valid projection ordinal. Declared per unit, matching `20`'s `AbsentTile` and `34`'s
//    `AbsentWork` — nothing reads two of them, and a shared spelling would be a dependency edge no traversal
//    can see.
inline constexpr std::uint32_t AbsentProjection = 0xFFFFFFFFu;   // [-] - the illuminant occupies none

// ⚠️ 🚧 `60`'s Position block names `02`, `06`, `08`, `16` and `44`, and does not name `46`. Its §3 sizes a
//    directional projection against "the camera's resolved depth range" and its §4 rebuilds that subdivision
//    when the camera moves, so the body reads `46` while the block does not declare it. Two closures were
//    available and neither is free: deriving a reversed-depth projection locally would be `46` §3's derivation
//    written a second time, which is `00` §2's case and the exact shape of the defect `Foundation/`'s two depth
//    constants exist to prevent. The edge is therefore declared and read, and the omission is recorded as an
//    addition to `00` §10 rather than worked around silently.

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE TWO TERMS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which of `60` §2's two terms a value belongs to.
/// note  🔴 They are **never merged into one scalar** — `60` §2. A single occlusion value applied to both
///        darkens a surface in shadow twice: once because the illuminant is hidden, and once because the
///        geometry hiding it also closes the hemisphere. The artist meets that as contact regions that go black
///        while everything around them is correctly lit, and no amount of adjusting either term fixes it.
/// note  🔴 Both are **scalar**. `18` §7 declares that no bent orientation is produced and that nothing consumes
///        one; this component is the producer that declaration binds, and a bent orientation's only consumer was
///        indirect lighting, which `00` §5.1 declares absent.
/// tag   guarantee
enum class OcclusionTerm : std::uint32_t
{
    Direct    = 0u,   // [-] - is *this illuminant* visible here — `DirectOcclusionSurface`
    Ambient   = 1u,   // [-] - how much of the hemisphere is closed — `OcclusionSurface`
    TermCount = 2u    // [-] - the closed count, never a term
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   ONE PROJECTION SET
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What shape one illuminant's projection takes, which follows from its emission shape and nothing else.
/// note  📝 Derived from `44` §3's shape rather than declared beside it. A projection shape declared separately
///        is a second answer to a question the illuminant already answered, and the two disagree the moment an
///        artist changes a point illuminant into a spot.
/// tag   guarantee
enum class ProjectionShape : std::uint32_t
{
    SixFaces      = 0u,   // [-] - a point illuminant; six faces about its position
    Subdivided    = 1u,   // [-] - a directional illuminant; parallel, split by the camera's depth range
    SingleCone    = 2u,   // [-] - a spot illuminant; one projection inside its declared cone
    SingleAxis    = 3u,   // [-] - an extended shape; one projection along its axis
    ShapeCount    = 4u    // [-] - the closed count, never a shape
};

/// 🧩 The projection shape one emission shape implies.
/// note  🔴 `60` §3's table, as one routine. It is trivial and it is a routine for the reason
///        `ProjectOcclusionSlot` is: a trivial mapping spelled at the derivation site, the rebuild site and the
///        metrics site is one that acquires a fourth case at exactly one of them.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr ProjectionShape ShapeOfEmission(EmissionShape Emission)
{
    return Emission == EmissionShape::Point       ? ProjectionShape::SixFaces
         : Emission == EmissionShape::Directional ? ProjectionShape::Subdivided
         : Emission == EmissionShape::Spot        ? ProjectionShape::SingleCone
         : Emission == EmissionShape::Extended    ? ProjectionShape::SingleAxis
         : ProjectionShape::ShapeCount;
}

// 📝 🚧 `60` §9 carries the projection extents and the directional subdivision count as open, and records that
//    the first blocks memory and the second quality against memory. Both are declared here rather than in
//    `Foundation/` because no second unit reads either — `00` §2's rule, applied to a number that is a tuning
//    figure rather than an agreement.
inline constexpr std::uint32_t ProjectionExtentTexels      = 1024u;   // [px] - per edge, per face
inline constexpr std::uint32_t DirectionalSubdivisionCount = 4u;      // [-]  - slices of the camera's range
inline constexpr std::uint32_t PenumbraSampleCount         = 16u;     // [-]  - `02` §6's planar pattern

/// 🧩 One face of one illuminant's projection.
/// note  🔴 A `ViewProjection` and not a matrix. `46` §3 derives the reversed-depth arrangement and extracts the
///        six planes from it; a projection carrying a matrix alone would have to extract them again, and the two
///        extractions are what `Foundation/`'s `NearPlaneDepth` exists to keep from disagreeing.
/// tag   owning
struct ProjectionFace
{
    ViewProjection  Projected     = {};      // [-]  - as `46` derived it
    FrustumSpace    Bounding      = {};      // [-]  - extracted from it, for what the face reaches
    double          NearestPlane  = 0.0;     // [mm] - what the face resolves from
    double          FurthestPlane = 0.0;     // [mm] - and to; the illuminant's declared extent, or a slice of it
};

/// 🧩 One illuminant's whole projection — every face, and what it was derived against.
/// note  🔴 One projection per **occlusion-registered** illuminant and none for the rest — `44` §2 gives the artist
///        the switch and `60` §3 declares that an unenrolled illuminant is integrated unattenuated. That is a
///        declared behaviour and not a failure: an artist lighting a workspace with six fill illuminants does
///        not want six projections, and deleting the illuminant is not the alternative they wanted either.
/// tag   owning
struct DerivedProjection
{
    std::vector<ProjectionFace>  Faces          = {};                          // [-]  - one, four or six
    OwnerIdentity             Illuminant     = {};                          // [-]  - who it projects for
    ProjectionShape              Shape          = ProjectionShape::ShapeCount;  // [-]
    double                       EmissionSize   = 0.0;                          // [mm] - drives `60` §3.2's penumbra
    std::uint32_t                ExtentTexels   = ProjectionExtentTexels;       // [px] - per edge, per face
    std::uint64_t                DerivedAt      = 0u;                           // [-]  - the rotation it was built
    bool                         RebuildOwed    = true;                         // [-]  - §4's conditions, accumulated
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT INVALIDATES ONE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What changed, so that only what it reaches is rebuilt.
/// note  🔴 `60` §4: a projection is world-referred and is rebuilt when **what it sees** changes, and not
///        otherwise. A camera move rebuilds nothing but the directional subdivision, and a texture stroke rebuilds
///        nothing at all — these are the two things the artist does constantly, and a projection set that
///        rebuilt on either is a workspace that stutters while being used rather than while being changed.
/// tag   guarantee
enum class InvalidationSubject : std::uint32_t
{
    IlluminantAmended  = 0u,   // [-] - moved or resized; its own projection only
    OwnerMoved      = 1u,   // [-] - every projection whose extent reaches it
    CameraMoved        = 2u,   // [-] - the directional subdivision, and nothing else
    RadiantIntensity   = 3u,   // [-] - nothing; the extent is declared, not derived from it
    OwnerTextured    = 4u,   // [-] - nothing; occlusion reads topology, not channels
    CutoutCoverage     = 5u,   // [-] - the exception `62` §2 declares; projections reaching it
    SubjectCount       = 6u    // [-] - the closed count, never a subject
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE PACKED INDEX
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which packed component each reaching illuminant occupies, per partition.
/// note  🔴 `60` §3.1: the set that reaches a pixel is `44` §5's `IlluminantIndex` and **not** the population.
///        `18` integrates that set and this projects it, so the two never disagree about which illuminant
///        occupies which position — which is the whole reason this is derived from that index rather than from
///        the population it was derived from.
/// note  ⚠️ The packed capacity is narrower than `44`'s reach capacity, so a partition may truncate here having
///        already truncated there. Both truncations are counted and both are reported, because they are
///        different losses: `44`'s drops the illuminant entirely and this one leaves it unshadowed.
/// tag   owning
class OcclusionIndex
{
public:

    /// 🧩 Derives the packing for every partition of a reaching index.
    /// in    Reaching     [-]  `44` §5's index, already derived against this rotation's partitions
    /// in    Illuminants  [-]  the population, for each reaching illuminant's registration
    /// out   Result      [-]  refuses with ContentUnsupported when the reaching index spans no partition
    /// post  every partition carries a slot per occlusion-registered illuminant it can hold, and a truncation count
    /// note  🔴 An illuminant **not registered for occlusion** occupies no slot at all rather than occupying one
    ///        and writing unity into it. A slot spent on an illuminant that casts nothing is a slot the fifth
    ///        illuminant that does cast something cannot have, and the artist meets that as their key light
    ///        losing its shadow when they add a fill.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Derive(const IlluminantIndex& Reaching, const IlluminantPopulation& Illuminants);

    /// 🧩 The packed component one illuminant occupies in one partition.
    /// out   Result  [-]  refuses with ExtentExhausted where the illuminant reaches the partition and the word
    ///                     cannot carry it, and with ContentUnsupported where it does not reach it at all
    /// note  📝 The two refusals are different facts and are spelled apart. An illuminant that does not reach a
    ///        partition contributes nothing there and needs no shadow; one that reaches it and truncated
    ///        contributes its whole direct term unattenuated, and `18` must know which.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> SlotOf(std::uint32_t PartitionIndex, OwnerIdentity Illuminant) const;

    /// 🧩 The illuminant one packed component carries in one partition.
    /// out   Result  [-]  refuses with ExtentExhausted where the slot is unoccupied
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<OwnerIdentity> IlluminantAt(std::uint32_t PartitionIndex, std::uint32_t Slot) const;

    /// 🧩 How many illuminants one partition could not pack — the excess `18` integrates unattenuated.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t TruncatedCount(std::uint32_t PartitionIndex) const;

    std::uint32_t TruncatedTotal() const;
    std::uint32_t SpannedCount() const;

private:

    struct PackedPartition
    {
        OwnerIdentity  Occupying[DirectOcclusionCapacity] = {};   // [-] - by packed component
        std::uint32_t     OccupiedCount                      = 0u;   // [-] - components carrying an illuminant
        std::uint32_t     TruncatedCount                     = 0u;   // [-] - reaching, registered, and unpackable
    };

    std::vector<PackedPartition>  Packed               = {};   // [-] - by partition ordinal
    std::uint32_t                 TruncatedAccumulated = 0u;   // [-] - across every partition
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE AMBIENT TERM
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What the screen-space ambient term is resolved against.
/// note  🔴 `60` §5: resolved in screen space from `16`'s `DepthSurface`, at **half extent** as `08` §2 declares,
///        with positions reconstructed exactly as `18` §1 reconstructs them and the hemisphere sampled over
///        `02` §6's planar pattern. The pattern is `02` §6's and is never invented here — `64` accumulates
///        `RadianceSurface` across rotations, so a term sampled from a pattern that is not progressive converges
///        to a different value than the one `64` assumes it is averaging.
/// note  ⚠️ Screen space is a **declared limitation**, not an approximation to be improved silently. What is off
///        screen does not occlude, and a surface that leaves the workspace edge brightens. `86` does not report
///        it; it is a property of the term, and `00` §5.1's substitution accounting is where it belongs.
/// tag   nonallocating, nonthrowing
struct AmbientOcclusionSpecification
{
    double         SampleRadius   = 250.0;                // [mm] - the hemisphere's own radius in the scene
    double         Strength       = 1.0;                  // [-]  - applied to the resolved occlusion
    std::uint32_t  SampleCount    = 16u;                  // [-]  - hemisphere samples per coarse texel
    std::uint32_t  ExtentDivisor  = 2u;                   // [-]  - `08` §2's half extent
    bool           JitterDeclared = true;                 // [-]  - `60` §9's open row; `64` owns the resolve
};

/// 🧩 The screen-space ambient term's declaration and its own metrics.
/// note  🔴 The authored channel 6 and the term resolved here **multiply** — `18` §5 states it from the
///        consuming side and `60` §2 from this one. It is a product rather than a choice because they describe
///        different scales: channel 6 is detail the topology does not carry, and this resolves contact the
///        topology does carry.
/// tag   owning
class AmbientOcclusionSequence
{
public:

    /// 🧩 Declares what the term is resolved against.
    /// out   Result  [-]  refuses with ContentUnsupported for a radius or sample count of nothing, and for a
    ///                     divisor that is not two
    /// note  ⚠️ The divisor is rejected above two rather than accepted as a quality setting. `08` §2 claims
    ///        `OcclusionSurface` at half extent and `Shared/`'s upsample reads four taps against that claim; a
    ///        third of the extent would need a different tap count, and the two would be declared in two places.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Declare(const AmbientOcclusionSpecification& Declaring);

    /// 🧩 The extent the term is resolved at, from one display extent.
    /// out   Result  [-]  refuses with ContentUnsupported for a display extent of nothing
    /// note  📐 Rounded **up** on both ordinates, matching `RenderSchedule`'s own fraction-of-display claim.
    ///        Rounding down leaves the display's last column with no coarse texel above it, and the upsample
    ///        then reads outside its own target along one edge.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Resolve(std::uint32_t DisplayX,
                          std::uint32_t DisplayY,
                          std::uint32_t& ResolvedX,
                          std::uint32_t& ResolvedY) const;

    const AmbientOcclusionSpecification& Declared() const;

private:

    AmbientOcclusionSpecification  Specification = {};   // [-] - as Declare validated it
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE METRICS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What `60` reports through `86`.
/// note  🔴 The truncation is a **Truncation** and is appended; every other row is a Measure and overwrites.
///        `86` §2 draws that line, and a rebuild count appended once per rotation would bury the one truncation
///        the artist did not expect under a thousand readings nobody asked for.
/// tag   nonallocating, nonthrowing
struct OcclusionMetrics
{
    std::uint32_t  ProjectionCount    = 0u;   // [-] - illuminants carrying a projection
    std::uint32_t  FaceCount          = 0u;   // [-] - faces across every projection
    std::uint32_t  RebuiltThisRecording = 0u;  // [-] - projections rebuilt this rotation
    std::uint64_t  RebuiltTotal       = 0u;   // [-] - across the session
    std::uint32_t  TruncatedTotal     = 0u;   // [-] - `60` §3.1's excess, integrated unattenuated
    std::uint32_t  UnenrolledCount    = 0u;   // [-] - illuminants deliberately carrying no projection
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE PROJECTIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every occlusion-registered illuminant's projection, and the two terms `18` attenuates with.
/// note  🔴 `60` §6: this records at `08` §3 ③ — after `16` produced `DepthSurface`, before `18` reads either
///        term. It **produces** both targets and amends neither.
/// note  🔴 Nothing here reads `RadianceSurface`. Occlusion is a visibility question and is resolved before
///        anything is shaded; a term that read shading would be a one-rotation-stale term, and its staleness
///        would be visible exactly when the illuminant moves.
/// tag   owning
class OcclusionProjectionSpace
{
public:

    /// 🧩 Contributes `08` §3 ③'s recording.
    /// out   Result  [-]  refuses with whatever the schedule rejected
    /// note  📝 Both targets are produced by one recording rather than by two, because both are resolved from
    ///        one reconstruction of the same depth. Two recordings would reconstruct the position at every pixel
    ///        twice to write two scalars.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Contribute(RenderSchedule& Schedule) const;

    /// 🧩 Declares the camera every directional subdivision is sized against.
    /// in    Declaring  [-]  the presented camera, as `46` holds it
    /// out   Result    [-]  refuses with ContentUnsupported for a camera declaring no valid clipping interval
    /// post  a directional projection is owed a rebuild only where the camera moved materially
    /// note  🔴 Supplied rather than reached for through a held reference, so that a directional subdivision is
    ///        rebuilt at a declared moment on the tick rather than whenever something happened to read a camera
    ///        that had since moved. `28`'s sun direction is supplied for the same reason and by the same rule.
    /// note  ⚠️ A camera move owes the **directional** subdivision and nothing else — `60` §4. Point, spot and
    ///        extended shapes are world-referred and see what they saw before the camera moved.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareCamera(const CameraSpecification& Declaring);

    /// 🧩 Records that something changed, so that only what it reaches is owed a rebuild.
    /// in    Declared  [-]  which of `60` §4's rows
    /// in    Subject   [-]  the illuminant that changed, or the moved owner's identity; may be undeclared
    /// in    Extent    [mm] what the change reaches, for the rows that test reach
    /// out   Result   [-]  refuses with ContentUnsupported for a subject outside the declared set
    /// note  🔴 `RadiantIntensity` and `OwnerTextured` invalidate **nothing**, and both are accepted rather
    ///        than rejected. They are the two rows an artist triggers constantly, and accepting them here is what
    ///        lets a caller declare every change it makes without knowing which ones matter — which is the only
    ///        arrangement where the ones that do not matter stay free.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Invalidate(InvalidationSubject Declared,
                             OwnerIdentity    Subject,
                             PartitionExtent     Extent);

    /// 🧩 Rebuilds whatever the declared conditions owe, and nothing else.
    /// in    Illuminants      [-]  the population; every occlusion-registered member is projected
    /// in    RecordingIndex  [-]  the rotation rebuilding
    /// out   Result          [-]  refuses with ContentUnsupported before a camera is declared, and carries a
    ///                             face derivation's own refusal
    /// post  🔴 with nothing owed, nothing is rebuilt and nothing is recorded
    /// note  🔴 An illuminant not registered for occlusion is **counted** and skipped rather than silently
    ///        ignored, so `86` can present how many of a scene's illuminants cast nothing. An artist who
    ///        disabled occlusion on their key light six months ago has no other way to find out.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Rebuild(const IlluminantPopulation& Illuminants, std::uint64_t RecordingIndex);

    /// 🧩 One illuminant's standing projection.
    /// out   Result  [-]  refuses with ExtentExhausted where the illuminant carries none
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<const DerivedProjection*> Current(OwnerIdentity Illuminant) const;

    /// 🧩 Whether anything is owed a rebuild.
    /// note  🔴 What the schedule's contributor reads to decide whether ③ records at all. `60` §4's table exists
    ///        so that the ordinary rotation — a camera the artist is orbiting, a stroke they are texturing —
    ///        rebuilds nothing.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    bool RebuildOwed() const;

    /// 🧩 Appends `60` §3.1's truncation and declares every measure beside it.
    /// note  🔴 The truncation **appends** and the counts **overwrite** — `86` §2. Coalesced by partition
    ///        ordinal as the subject, so twelve partitions that each truncated present as twelve entries rather
    ///        than as one with a count of twelve; `86` §6 refuses the second shape by name.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Report(ReportSequence& Reporting,
                MeasureIndex&   Measured,
                TickPoint       Sampled) const;

    OcclusionIndex&                Index();
    const OcclusionIndex&          Index() const;
    AmbientOcclusionSequence&      Ambient();
    const AmbientOcclusionSequence& Ambient() const;
    const OcclusionMetrics&        Metrics() const;

    std::uint32_t ProjectionCount() const;

private:

    Deliver<DerivedProjection> Derive(const IlluminantSpecification& Declared,
                                      OwnerIdentity               Illuminant,
                                      std::uint64_t                  RecordingIndex) const;

    std::size_t Located(OwnerIdentity Illuminant) const;

    std::vector<DerivedProjection>  Projections     = {};      // [-] - one per registered illuminant
    OcclusionIndex                  PackedIndex     = {};      // [-] - who occupies which component
    AmbientOcclusionSequence        AmbientTerm     = {};      // [-] - the half-extent term
    OcclusionMetrics                Reported        = {};      // [-] - what `86` presents
    CameraSpecification             ReferenceCamera  = {};      // [-] - what the subdivision is sized against
    bool                            CameraDeclared  = false;   // [-] - DeclareCamera has delivered
    bool                            SubdivisionOwed = true;    // [-] - the directional shapes alone
};

// 📐 Illuminant identity, the packed slot and the registration test are Exact; the projections, the penumbra width
//    and the hemisphere accumulation are Bounded, and `60` §7 declares the last of the three Perceptual because
//    it attenuates a Tier D term and claims nothing more. `00` §3's transitivity rule folds them to the weakest.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Perceptual,
                         PrecisionGuarantee::Perceptual,
                         PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Exact);

}   // namespace Slate
